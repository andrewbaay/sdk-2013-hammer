//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef COLOR_H
#define COLOR_H

#ifdef _WIN32
#pragma once
#endif

#include "basetypes.h"

//-----------------------------------------------------------------------------
// Purpose: Basic handler for an rgb set of colors
//			This class is fully inline
//-----------------------------------------------------------------------------
class Color
{
public:
	// constructors
	constexpr Color() : raw_color( 0 ) {}
	constexpr Color( byte _r, byte _g, byte _b, byte _a = 0 ) : _color{ _r, _g, _b, _a } {}
	constexpr Color( const Color& clr ) : raw_color( clr.raw_color ) {}
	constexpr Color( Color&& clr ) = default;

	// set the color
	// r - red component (0-255)
	// g - green component (0-255)
	// b - blue component (0-255)
	// a - alpha component, controls transparency (0 - transparent, 255 - opaque);
	constexpr void SetColor( int _r, int _g, int _b, int _a = 0 )
	{
		_color[0] = (unsigned char)_r;
		_color[1] = (unsigned char)_g;
		_color[2] = (unsigned char)_b;
		_color[3] = (unsigned char)_a;
	}

	constexpr void GetColor(int &_r, int &_g, int &_b, int &_a) const
	{
		_r = _color[0];
		_g = _color[1];
		_b = _color[2];
		_a = _color[3];
	}

	constexpr void SetRawColor( uint32 color32 )
	{
		raw_color = color32;
	}

	constexpr uint32 GetRawColor() const
	{
		return raw_color;
	}

	constexpr int r() const	{ return _color[0]; }
	constexpr int g() const	{ return _color[1]; }
	constexpr int b() const	{ return _color[2]; }
	constexpr int a() const	{ return _color[3]; }

	constexpr unsigned char &operator[](int index)
	{
		return _color[index];
	}

	constexpr const unsigned char &operator[](int index) const
	{
		return _color[index];
	}

	constexpr bool operator == (const Color &rhs) const
	{
		return raw_color == rhs.raw_color;
	}

	constexpr bool operator != (const Color &rhs) const
	{
		return !(operator==(rhs));
	}

	constexpr Color &operator=( const Color &rhs )
	{
		SetRawColor( rhs.GetRawColor() );
		return *this;
	}

	constexpr Color &operator=( const color32 &rhs )
	{
		_color[0] = rhs.r;
		_color[1] = rhs.g;
		_color[2] = rhs.b;
		_color[3] = rhs.a;
		return *this;
	}

	constexpr color32 ToColor32() const
	{
		return { _color[0], _color[1], _color[2], _color[3] };
	}

private:
	union
	{
		byte _color[4];
		uint32 raw_color;
	};
};


#endif // COLOR_H