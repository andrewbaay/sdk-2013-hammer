
#include "stdafx.h"

#include "stocksolids.h"
#include "mapsolid.h"
#include "mapgroup.h"
#include "globalfunctions.h"
#include "options.h"
#include "tier0/icommandline.h"
#include "tier1/fmtstr.h"
#include "filesystem.h"

#include "scriptarray.h"
#include "scriptbuilder.h"
#include "scriptdictionary.h"
#include "scriptgrid.h"
#include "scripthelper.h"
#include "scriptmath.h"
#include "scriptstring.h"
#include "asbind.h"
#include "as_jit.h"
#ifdef DEBUG
#include "docgen.h"
#endif

#include "tier0/memdbgon.h"

#pragma comment( lib, "angelscript.lib" )

ASBIND_TYPE( Vector, Vector );
ASBIND_TYPE( QAngle, QAngle );
ASBIND_TYPE( Vector2D, Vector2D );
ASBIND_TYPE( Vector4D, Vector4D );
ASBIND_TYPE( BoundBox, BoundBox );
ASBIND_TYPE( CMapClass, CMapClass );
ASBIND_TYPE( CMapGroup, CMapGroup );
ASBIND_TYPE( CMapSolid, CMapSolid );
ASBIND_TYPE( CMapFace, CMapFace );
ASBIND_TYPE( TextureAlignment_t, TextureAlignment_t );
ASBIND_ARRAY_TYPE( CScriptArrayT<Vector>, Vector );

static asIScriptEngine* engine = nullptr;
static asCJITCompiler* compiler = nullptr;
static CUtlVector<asIScriptModule*> modules;

static void msgCallback( const asSMessageInfo* msg, void* )
{
	Color clr;
	switch ( msg->type )
	{
	case asMSGTYPE_ERROR:
		clr.SetColor( 255, 64, 64, 255 );
		break;
	case asMSGTYPE_WARNING:
		clr.SetColor( 255, 178, 0, 255 );
		break;
	case asMSGTYPE_INFORMATION:
	default:
		clr.SetColor( 153, 204, 255, 255 );
		break;
	}

	ConColorMsg( clr, "%s(%d:%d): %s\n", msg->section, msg->row, msg->col, msg->message );
}

static void WrapCreateFace( CMapFace* pThis, CScriptArrayT<Vector>* points, int nPoints )
{
	Assert( (unsigned)abs( nPoints ) <= points->GetSize() );
	int size = Min<unsigned>( nPoints, points->GetSize() );
	CUtlVector<Vector> copy;
	copy.SetCount( size );
	for ( int i = 0; i < size; i++ )
		copy[i] = *points->At( i );
	pThis->CreateFace( copy.Base(), nPoints );
}

static void WrapAddFace( CMapSolid* pThis, const CMapFace& face )
{
	pThis->AddFace( const_cast<CMapFace*>( &face ) );
}

template <typename T>
static void WrapCalcBounds( T* pThis, bool full )
{
	pThis->CalcBounds( full );
}

template <typename T>
static CMapClass* WrapCastMapClass( T* pThis )
{
	return pThis;
}

template <typename T>
static T* WrapCreateMapClass()
{
	return new T();
}

class ScriptSolid
{
public:
	ScriptSolid( const ScriptString& name, asIScriptObject* instance )
		: m_name( name ), m_pScriptInstance( instance )
	{
		m_pEngineCtx = instance->GetEngine()->RequestContext();
		asITypeInfo* type = instance->GetObjectType();
		m_setData = type->GetMethodByName( "SetData" );
		m_createMapSolid = type->GetMethodByName( "CreateMapSolid" );
		Assert( m_setData.isValid() && m_createMapSolid.isValid() );

		m_setData.setContext( m_pEngineCtx );
		m_createMapSolid.setContext( m_pEngineCtx );

		m_setData.setObject( instance );
		m_createMapSolid.setObject( instance );

		m_setData.addref();
		m_createMapSolid.addref();
	}

	~ScriptSolid()
	{
		m_pEngineCtx->Unprepare();

		m_setData.release();
		m_createMapSolid.release();

		if ( auto eng = m_pScriptInstance->GetEngine() )
			eng->ReturnContext( m_pEngineCtx );
		m_pScriptInstance->Release();
	}

	void SetData( const BoundBox* box ) { m_setData( box ); }
	CMapClass* CreateMapSolid( TextureAlignment_t align ) { return m_createMapSolid( align ); }

	const ScriptString& Name() const { return m_name; }

private:
	ScriptString		m_name;
	asIScriptContext*	m_pEngineCtx;
	asIScriptObject*	m_pScriptInstance;

	ASBind::FunctionPtr<void( const BoundBox* )> m_setData;
	ASBind::FunctionPtr<CMapClass*( TextureAlignment_t )> m_createMapSolid;
};

static CUtlVector<ScriptSolid*> scriptSolids;
static void RegisterScriptSolid( const ScriptString& name, asIScriptObject* solidClass )
{
	scriptSolids.AddToTail( new ScriptSolid( name, solidClass ) );
}

void ScriptInit()
{
	asPrepareMultithread();
	engine = asCreateScriptEngine();
	engine->SetMessageCallback( asFUNCTION( msgCallback ), nullptr, asCALL_CDECL );
	compiler = new asCJITCompiler();
	engine->SetEngineProperty( asEP_ALWAYS_IMPL_DEFAULT_CONSTRUCT, 1 );
	engine->SetEngineProperty( asEP_BUILD_WITHOUT_LINE_CUES, 1 );
	engine->SetEngineProperty( asEP_COMPILER_WARNINGS, 1 );
	engine->SetEngineProperty( asEP_INCLUDE_JIT_INSTRUCTIONS, 1 );
	engine->SetJITCompiler( compiler );
	engine->SetDefaultAccessMask( 0xFFFFFFFF );

	RegisterScriptMath( engine );
	RegisterScriptArray( engine, true );
	RegisterString( engine );
	RegisterScriptDictionary( engine );
	RegisterScriptGrid( engine );
	RegisterExceptionRoutines( engine );

	ASBind::Class<Vector2D, ASBind::class_pod_allfloats>{ engine, asOBJ_APP_CLASS_MORE_CONSTRUCTORS }
		.member( &Vector2D::x, "x" )
		.member( &Vector2D::y, "y" )
		.constructor<void()>()
		.constructor<void( float, float )>()
		.constructor<void( const Vector2D& )>()
		.constmethod( &Vector2D::operator[], "opIndex" )
		.method<vec_t&( Vector2D::* )( int )>( &Vector2D::operator[], "opIndex" )
		.method( &Vector2D::Init, "Init" )
		.method( &Vector2D::IsValid, "IsValid" )
		.method( &Vector2D::Random, "Random" )
		.method( &Vector2D::Negate, "Negate" )
		.method( &Vector2D::Length, "Length" )
		.method( &Vector2D::LengthSqr, "LengthSqr" )
		.method( &Vector2D::IsZero, "IsZero" )
		.method( &Vector2D::NormalizeInPlace, "NormalizeInPlace" )
		.method( &Vector2D::IsLengthGreaterThan, "IsLengthGreaterThan" )
		.method( &Vector2D::IsLengthLessThan, "IsLengthLessThan" )
		.method( &Vector2D::DistTo, "DistTo" )
		.method( &Vector2D::DistToSqr, "DistToSqr" )
		.method( &Vector2D::MulAdd, "MulAdd" )
		.method( &Vector2D::Dot, "Dot" )
		.method( &Vector2D::Min, "Min" )
		.method( &Vector2D::Max, "Max" )
		.method( &Vector2D::operator==, "opEquals" )
		.method<Vector2D&( Vector2D::* )( const Vector2D& )>( &Vector2D::operator+=, "opAddAssign" )
		.method<Vector2D&( Vector2D::* )( const Vector2D& )>( &Vector2D::operator-=, "opSubAssign" )
		.method<Vector2D&( Vector2D::* )( const Vector2D& )>( &Vector2D::operator*=, "opMulAssign" )
		.method<Vector2D&( Vector2D::* )( float )>( &Vector2D::operator*=, "opMulAssign" )
		.method<Vector2D&( Vector2D::* )( const Vector2D& )>( &Vector2D::operator/=, "opDivAssign" )
		.method<Vector2D&( Vector2D::* )( float )>( &Vector2D::operator/=, "opDivAssign" )
		.method( &Vector2D::operator=, "opAssign" )
		.constmethod<Vector2D, Vector2D>( &Vector2D::operator-, "opNeg" )
		.constmethod<Vector2D, Vector2D, const Vector2D&>( &Vector2D::operator+, "opAdd" )
		.constmethod<Vector2D, Vector2D, const Vector2D&>( &Vector2D::operator-, "opSub" )
		.constmethod<Vector2D, Vector2D, const Vector2D&>( &Vector2D::operator*, "opMul" )
		.constmethod<Vector2D, Vector2D, const Vector2D&>( &Vector2D::operator/, "opDiv" )
		.constmethod<Vector2D, Vector2D, float>( &Vector2D::operator*, "opMul" )
		.constmethod<Vector2D, Vector2D, float>( &Vector2D::operator/, "opDiv" )
		;

	ASBind::Class<Vector, ASBind::class_pod_allfloats>{ engine, asOBJ_APP_CLASS_MORE_CONSTRUCTORS }
		.member( &Vector::x, "x" )
		.member( &Vector::y, "y" )
		.member( &Vector::z, "z" )
		.constructor<void()>()
		.constructor<void( float, float, float )>()
		.constructor<void( float )>()
		.constmethod( &Vector::operator[], "opIndex" )
		.method<vec_t&( Vector::* )( int )>( &Vector::operator[], "opIndex" )
		.method( &Vector::Init, "Init" )
		.method( &Vector::IsValid, "IsValid" )
		.method( &Vector::Invalidate, "Invalidate" )
		.method( &Vector::Random, "Random" )
		.method( &Vector::Zero, "Zero" )
		.method( &Vector::NormalizeInPlace, "NormalizeInPlace" )
		.method( &Vector::Normalized, "Normalized" )
		.method( &Vector::Negate, "Negate" )
		.method( &Vector::Length, "Length" )
		.method( &Vector::Length2D, "Length2D" )
		.method( &Vector::Length2DSqr, "Length2DSqr" )
		.method( &Vector::LengthSqr, "LengthSqr" )
		.method( &Vector::IsZero, "IsZero" )
		.method( &Vector::IsLengthGreaterThan, "IsLengthGreaterThan" )
		.method( &Vector::IsLengthLessThan, "IsLengthLessThan" )
		.method( &Vector::WithinAABox, "WithinAABox" )
		.method( &Vector::DistTo, "DistTo" )
		.method( &Vector::DistToSqr, "DistToSqr" )
		.method( &Vector::MulAdd, "MulAdd" )
		.method( &Vector::Dot, "Dot" )
		.method( &Vector::Cross, "Cross" )
		.method( &Vector::Min, "Min" )
		.method( &Vector::Max, "Max" )
		.constmethod( &Vector::AsVector2D, "AsVector2D" )
		.method<Vector2D&( Vector::* )()>( &Vector::AsVector2D, "AsVector2D" )
		.method( &Vector::operator==, "opEquals" )
		.method<Vector&( Vector::* )( const Vector& )>( &Vector::operator+=, "opAddAssign" )
		.method<Vector&( Vector::* )( float )>( &Vector::operator+=, "opAddAssign" )
		.method<Vector&( Vector::* )( const Vector& )>( &Vector::operator-=, "opSubAssign" )
		.method<Vector&( Vector::* )( float )>( &Vector::operator-=, "opSubAssign" )
		.method<Vector&( Vector::* )( const Vector& )>( &Vector::operator*=, "opMulAssign" )
		.method<Vector&( Vector::* )( float )>( &Vector::operator*=, "opMulAssign" )
		.method<Vector&( Vector::* )( const Vector& )>( &Vector::operator/=, "opDivAssign" )
		.method<Vector&( Vector::* )( float )>( &Vector::operator/=, "opDivAssign" )
		.method( &Vector::operator=, "opAssign" )
		.constmethod<Vector, Vector>( &Vector::operator-, "opNeg" )
		.constmethod<Vector, Vector, const Vector&>( &Vector::operator+, "opAdd" )
		.constmethod<Vector, Vector, const Vector&>( &Vector::operator-, "opSub" )
		.constmethod<Vector, Vector, const Vector&>( &Vector::operator*, "opMul" )
		.constmethod<Vector, Vector, const Vector&>( &Vector::operator/, "opDiv" )
		.constmethod<Vector, Vector, float>( &Vector::operator*, "opMul" )
		.constmethod<Vector, Vector, float>( &Vector::operator/, "opDiv" )
		;

	ASBind::Class<QAngle, ASBind::class_pod_allfloats>{ engine, asOBJ_APP_CLASS_MORE_CONSTRUCTORS }
		.member( &QAngle::x, "x" )
		.member( &QAngle::y, "y" )
		.member( &QAngle::z, "z" )
		.constructor<void()>()
		.constructor<void( float, float, float )>()
		.method( &QAngle::Init, "Init" )
		.method( &QAngle::Random, "Random" )
		.method( &QAngle::IsValid, "IsValid" )
		.method( &QAngle::Invalidate, "Invalidate" )
		.method( &QAngle::Length, "Length" )
		.method( &QAngle::LengthSqr, "LengthSqr" )
		.constmethod( &QAngle::operator[], "opIndex" )
		.method<vec_t&( QAngle::* )( int )>( &QAngle::operator[], "opIndex" )
		.method( &QAngle::operator==, "opEquals" )
		.method( &QAngle::operator=, "opAssign" )
		.method( &QAngle::operator+=, "opAddAssign" )
		.method( &QAngle::operator-=, "opSubAssign" )
		.method( &QAngle::operator*=, "opMulAssign" )
		.method( &QAngle::operator/=, "opDivAssign" )
		.constmethod<QAngle, QAngle>( &QAngle::operator-, "opNeg" )
		.constmethod<QAngle, QAngle, const QAngle&>( &QAngle::operator+, "opAdd" )
		.constmethod<QAngle, QAngle, const QAngle&>( &QAngle::operator-, "opSub" )
		.constmethod<QAngle, QAngle, float>( &QAngle::operator*, "opMul" )
		.constmethod<QAngle, QAngle, float>( &QAngle::operator/, "opDiv" )
		;

	ASBind::Class<Vector4D, ASBind::class_pod_allfloats>{ engine, asOBJ_APP_CLASS_MORE_CONSTRUCTORS }
		.member( &Vector4D::x, "x" )
		.member( &Vector4D::y, "y" )
		.member( &Vector4D::z, "z" )
		.member( &Vector4D::w, "w" )
		.constructor<void()>()
		.constructor<void( float, float, float, float )>()
		.constructor<void( const Vector&, float )>()
		.constructor<void( const Vector4D& )>()
		.method<void( Vector4D::* )( float, float, float, float )>( &Vector4D::Init, "Init" )
		.method<void( Vector4D::* )( const Vector&, float )>( &Vector4D::Init, "Init" )
		.method( &Vector4D::IsValid, "IsValid" )
		.method( &Vector4D::Negate, "Negate" )
		.method( &Vector4D::Length, "Length" )
		.method( &Vector4D::LengthSqr, "LengthSqr" )
		.method( &Vector4D::IsZero, "IsZero" )
		.method( &Vector4D::DistTo, "DistTo" )
		.method( &Vector4D::DistToSqr, "DistToSqr" )
		.method( &Vector4D::MulAdd, "MulAdd" )
		.method( &Vector4D::Dot, "Dot" )
		.constmethod( &Vector4D::operator[], "opIndex" )
		.method<vec_t&( Vector4D::* )( int )>( &Vector4D::operator[], "opIndex" )
		.constmethod( &Vector4D::AsVector3D, "AsVector3D" )
		.method<Vector&( Vector4D::* )()>( &Vector4D::AsVector3D, "AsVector3D" )
		.constmethod( &Vector4D::AsVector2D, "AsVector2D" )
		.method<Vector2D&( Vector4D::* )()>( &Vector4D::AsVector2D, "AsVector2D" )
		.method( &Vector4D::Random, "Random" )
		.method( &Vector4D::operator==, "opEquals" )
		.method<Vector4D&( Vector4D::* )( const Vector4D& )>( &Vector4D::operator+=, "opAddAssign" )
		.method<Vector4D&( Vector4D::* )( const Vector4D& )>( &Vector4D::operator-=, "opSubAssign" )
		.method<Vector4D&( Vector4D::* )( const Vector4D& )>( &Vector4D::operator*=, "opMulAssign" )
		.method<Vector4D&( Vector4D::* )( float )>( &Vector4D::operator*=, "opMulAssign" )
		.method<Vector4D&( Vector4D::* )( const Vector4D& )>( &Vector4D::operator/=, "opDivAssign" )
		.method<Vector4D&( Vector4D::* )( float )>( &Vector4D::operator/=, "opDivAssign" )
		;

	ASBind::Class<BoundBox, ASBind::class_nocount>{ engine }
		.constmember( &BoundBox::bmaxs, "maxs" )
		.constmember( &BoundBox::bmins, "mins" )
		.constmethod( &BoundBox::GetBoundsCenter, "GetBoundsCenter" )
		.constmethod( &BoundBox::ContainsPoint, "ContainsPoint" )
		.constmethod( &BoundBox::GetBoundsSize, "GetBoundsSize" )
		;

#define __ENUM( n ) .add( #n, n )

	ASBind::Enum{ engine, "TextureAlignment_t" }
		__ENUM( TEXTURE_ALIGN_NONE )
		.all_flags<TextureAlignment_t>()
		;

	ASBind::Class<CMapFace, ASBind::class_class>{ engine }
		.constructor<void()>()
		.destructor()
		.method( &WrapCreateFace, "CreateFace" )
		;

	ASBind::Enum{ engine, "InitTexFlags_t" }
		__ENUM( INIT_TEXTURE_FORCE )
		__ENUM( INIT_TEXTURE_AXES )
		__ENUM( INIT_TEXTURE_ROTATION )
		__ENUM( INIT_TEXTURE_SHIFT )
		__ENUM( INIT_TEXTURE_SCALE )
		__ENUM( INIT_TEXTURE_ALL )
		;

#undef __ENUM

	ASBind::Class<CMapClass, ASBind::class_nocount>{ engine }
		;

	ASBind::Class<CMapGroup, ASBind::class_nocount>{ engine }
		.method( &CMapGroup::AddChild, "AddChild" )
		.method( &WrapCalcBounds<CMapGroup>, "CalcBounds" )
		.method( &CMapClass::GetBoundsSize, "GetBoundsSize" )
		.method( &CMapClass::TransMove, "TransMove" )
		.method( &CMapClass::TransRotate, "TransRotate" )
		.method( &CMapClass::TransScale, "TransScale" )
		.refcast( &WrapCastMapClass<CMapGroup>, true )
		;

	ASBind::Class<CMapSolid, ASBind::class_nocount>{ engine }
		.method( &WrapAddFace, "AddFace" )
		.method( &CMapSolid::InitializeTextureAxes, "InitializeTextureAxes" )
		.method( &WrapCalcBounds<CMapSolid>, "CalcBounds" )
		.method( &CMapClass::GetBoundsSize, "GetBoundsSize" )
		.method( &CMapClass::TransMove, "TransMove" )
		.method( &CMapClass::TransRotate, "TransRotate" )
		.method( &CMapClass::TransScale, "TransScale" )
		.refcast( &WrapCastMapClass<CMapSolid>, true )
		;

	ASBind::Interface{ engine, "ScriptSolid" }
		.method<void( const BoundBox* )>( "SetData" )
		.method<CMapClass*( TextureAlignment_t )>( "CreateMapSolid" )
		;

	ASBind::Global{ engine }
		.constvar( vec3_origin, "vec3_origin" )
		.constvar( vec3_invalid, "vec3_invalid" )
		.constvar( vec3_angle, "vec3_angle" )
		.constvar( vec4_origin, "vec4_origin" )
		.function( &WrapCreateMapClass<CMapGroup>, "CreateMapGroup" )
		.function( &WrapCreateMapClass<CMapSolid>, "CreateSolid" )
		.function2( &RegisterScriptSolid, "void RegisterScriptSolid( const string &in name, ScriptSolid@ instance )" )
		;

	CScriptBuilder builder;
	asIScriptContext* ctx = nullptr;
	FileFindHandle_t h = 0;
	const char* find = g_pFullFileSystem->FindFirstEx( "scripts/*.as", "hammer", &h );

	while ( find )
	{
		const CFmtStr fullScript( "scripts/%s", find );

		CScriptBuilder builder;
		if ( !ctx )
			ctx = engine->RequestContext();
		builder.StartNewModule( engine, find );
		builder.AddSectionFromFile( fullScript );
		builder.BuildModule();
		asIScriptModule* module = builder.GetModule();

		auto reg = ASBind::CreateFunctionPtr<void()>( "RegisterCallback", module );
		if ( reg.isValid() )
		{
			reg.setContext( ctx );
			reg();
			modules.AddToTail( module );
		}
		else
		{
			Warning( "Script '%s' doesn't have RegisterCallback function. Destroying!\n", find );
			module->Discard();
		}

		find = g_pFullFileSystem->FindNext( h );
	}

	g_pFullFileSystem->FindClose( h );

#ifdef DEBUG
	if ( CommandLine_Tier0()->FindParm( "-doc" ) )
	{

		DocumentationGenerator doc{ engine, { "scripts/api_reference.html", true, true, false, false, false, true, true, true, true, "0.1", "Hammer", "AS Interface" } };
		doc.Generate();
	}
#endif
}

void ScriptShutdown()
{
	for ( auto& solid : scriptSolids )
		delete solid;

	for ( auto module : modules )
		module->Discard();
	modules.Purge();

	engine->ShutDownAndRelease();
	delete compiler;
	asUnprepareMultithread();
}

static void SetDefTexture( CMapClass* pThis )
{
	if ( pThis->IsMapClass( MAPCLASS_TYPE( CMapGroup ) ) )
	{
		auto& children = *pThis->GetChildren();
		for ( CMapClass* c : children )
			SetDefTexture( c );
		return;
	}
	Assert( pThis->IsMapClass( MAPCLASS_TYPE( CMapSolid ) ) );
	static_cast<CMapSolid*>( pThis )->SetTexture( GetDefaultTextureName() );
}

CMapClass* ScriptableSolid_Create( int index, const BoundBox* box )
{
	if ( !scriptSolids.IsValidIndex( index ) )
		return nullptr;
	auto solid = scriptSolids[index];
	solid->SetData( box );
	CMapClass* ret = solid->CreateMapSolid( Options.GetTextureAlignment() );
	if ( ret )
		SetDefTexture( ret );

	// return new solid
	return ret;
}

int ScriptableSolid_Count()
{
	return scriptSolids.Count();
}

void ScriptableSolid_GetNames( CUtlVector<CString>& names )
{
	for ( auto solid : scriptSolids )
	{
		auto& name = solid->Name();
		names.AddToTail( name.String(), name.Length() );
	}
}

int ScriptableSolid_Find( const char* text )
{
	return scriptSolids.FindMatch( [text]( const ScriptSolid* pSolid ) { return pSolid->Name().IsEqual_CaseInsensitive( text ); } );
}