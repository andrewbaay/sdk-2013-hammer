#define RAPIDJSON_SSE2

#include "rpc_connection.h"
#include "serialization.h"

static const int RpcVersion = 1;
static RpcConnection Instance;

RpcConnection* RpcConnection::Create( const char* applicationId )
{
	Instance.connection = BaseConnection::Create();
	StringCopy( Instance.appId, applicationId );
	return &Instance;
}

void RpcConnection::Destroy( RpcConnection*& c )
{
	c->Close();
	BaseConnection::Destroy( c->connection );
	c = nullptr;
}

void RpcConnection::Open()
{
	if ( state == State::Connected )
		return;

	if ( state == State::Disconnected && !connection->Open() )
		return;

	if ( state == State::SentHandshake )
	{
		JsonDocument message;
		if ( Read( message ) )
		{
			const char* cmd = GetStrMember( &message, "cmd" );
			const char* evt = GetStrMember( &message, "evt" );
			if ( cmd && evt && !strcmp( cmd, "DISPATCH" ) && !strcmp( evt, "READY" ) )
			{
				state = State::Connected;
				if ( onConnect )
					onConnect( message );
			}
		}
	}
	else
	{
		sendFrame.opcode = Opcode::Handshake;
		sendFrame.length = static_cast< uint32 >( JsonWriteHandshakeObj(
			sendFrame.message, sizeof( sendFrame.message ), RpcVersion, appId ) );

		if ( connection->Write( &sendFrame, sizeof( MessageFrameHeader ) + sendFrame.length ) )
			state = State::SentHandshake;
		else
			Close();
	}
}

void RpcConnection::Close()
{
	if ( onDisconnect && ( state == State::Connected || state == State::SentHandshake ) )
		onDisconnect( static_cast< int >( lastErrorCode ), lastErrorMessage );
	connection->Close();
	state = State::Disconnected;
}

bool RpcConnection::Write( const void* data, size_t length )
{
	sendFrame.opcode = Opcode::Frame;
	memcpy( sendFrame.message, data, length );
	sendFrame.length = static_cast< uint32 >( length );
	if ( !connection->Write( &sendFrame, sizeof( MessageFrameHeader ) + length ) )
	{
		Close();
		return false;
	}
	return true;
}

bool RpcConnection::Read( JsonDocument& message )
{
	if ( state != State::Connected && state != State::SentHandshake )
	{
		return false;
	}
	MessageFrame readFrame;
	for ( ;;)
	{
		bool didRead = connection->Read( &readFrame, sizeof( MessageFrameHeader ) );
		if ( !didRead )
		{
			if ( !connection->isOpen )
			{
				lastErrorCode = ErrorCode::PipeClosed;
				StringCopy( lastErrorMessage, "Pipe closed" );
				Close();
			}
			return false;
		}

		if ( readFrame.length > 0 )
		{
			didRead = connection->Read( readFrame.message, readFrame.length );
			if ( !didRead )
			{
				lastErrorCode = ErrorCode::ReadCorrupt;
				StringCopy( lastErrorMessage, "Partial data in frame" );
				Close();
				return false;
			}
			readFrame.message[readFrame.length] = 0;
		}

		switch ( readFrame.opcode )
		{
		case Opcode::Close:
		{
			message.ParseInsitu( readFrame.message );
			lastErrorCode = static_cast< ErrorCode >( GetIntMember( &message, "code" ) );
			StringCopy( lastErrorMessage, GetStrMember( &message, "message", "" ) );
			Close();
			return false;
		}
		case Opcode::Frame:
			message.ParseInsitu( readFrame.message );
			return true;
		case Opcode::Ping:
			readFrame.opcode = Opcode::Pong;
			if ( !connection->Write( &readFrame, sizeof( MessageFrameHeader ) + readFrame.length ) )
				Close();
			break;
		case Opcode::Pong:
			break;
		case Opcode::Handshake:
		default:
			// something bad happened
			lastErrorCode = ErrorCode::ReadCorrupt;
			StringCopy( lastErrorMessage, "Bad ipc frame" );
			Close();
			return false;
		}
	}
}
