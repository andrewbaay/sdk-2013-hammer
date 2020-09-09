//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef RICHEDITCTRLEX_H
#define RICHEDITCTRLEX_H
#ifdef _WIN32
#pragma once
#endif

#pragma warning(push, 1)
#undef _tzcnt_u32
#include <list>
#include <stack>
#pragma warning(pop)

#define RICHED_DECL


class CRTFBuilder;
class CStringManip;
class CIntManip;


using RTFSM_PFUNC = CRTFBuilder&(*)( CRTFBuilder& );
using RTFSM_STRINGPFUNC = CRTFBuilder&(*)( CRTFBuilder&, const CString& );
using RTFSM_INTPFUNC = CRTFBuilder&(*)( CRTFBuilder&, int );
using RTFSM_BOOLPFUNC = CRTFBuilder&(*)( CRTFBuilder&, bool );
using RTFSM_CONTROLPFUNC = CRTFBuilder&(*)( CRTFBuilder&, CRichEditCtrl& );


class CBoolString
{
private:
	bool m_b;
	CString m_strOn;
	CString m_strOff;

public:

	CBoolString( CString strOn, CString strOff = "" )
	{
		m_strOn = strOn;
		m_strOff = strOff;
		m_b = false;
	}

	void operator=( bool b )
	{
		m_b = b;
	}

	operator CString() const
	{
		return m_b ? m_strOn : m_strOff;
	}
};


class CTextAttributes
{
protected:
	int m_nFontSize;

	CBoolString m_bsBold;
	CBoolString m_bsUnderline;
	CBoolString m_bsItalic;
	CBoolString m_bsStrike;

	int m_nFontNumber;
	int m_nColorFground;
	int m_nColorBground;

public:

	CTextAttributes()
		: m_bsBold( "\\b" ),
		  m_bsUnderline( "\\ul" ),
		  m_bsItalic( "\\i" ),
		  m_bsStrike( "\\strike" )
	{
		m_nColorBground = m_nColorFground = m_nFontNumber = m_nFontSize = 0;
		m_bsBold = false;
	}

	operator CString() const
	{
		CString s;
		s.Format( R"(\plain%s%s%s%s\f%d\fs%d\cb%d\cf%d )",
				  (LPCTSTR)(CString)m_bsBold,
				  (LPCTSTR)(CString)m_bsUnderline, (LPCTSTR)(CString)m_bsItalic, (LPCTSTR)(CString)m_bsStrike,
				  m_nFontNumber,
				  m_nFontSize,
				  m_nColorBground,
				  m_nColorFground );
		return s;
	}

	friend class CRTFBuilder;
};


class CFontList : public std::list<CString>
{
public:
	operator CString() const
	{
		CString s = "{\\fonttbl";

		int nCount = 0;
		for ( const auto& i : *this )
		{
			CString s2;
			s2.Format( "{\\f%d %s;}", nCount++, static_cast<LPCTSTR>( i ) );
			s += s2;
		}

		s += '}';
		return s;
	}

	void add( const CString& s )
	{
		push_back( s );
	}
};


class CColorList : public std::list<COLORREF>
{
public:
	int add( COLORREF c )
	{
		push_back( c );
		return size() - 1;
	}

	int find( COLORREF c )
	{
		int n = 0;
		for ( auto i = begin(); i != end(); ++i, n++ )
		{
			const COLORREF cComp( *i );
			if ( cComp == c )
				return n;
		}

		return -1;
	}


	operator CString() const
	{
		CString s( "{\\colortbl" );
		for ( const COLORREF& c : *this )
		{
			const int r( ( c & 0x000000ff ) );
			const int g( ( c >> 8 ) & 0x000000ff );
			const int b( ( c >> 16 ) & 0x000000ff );

			CString s2;
			s2.Format( R"(\red%d\green%d\blue%d;)", r, g, b );
			s += s2;
		}

		s += '}';
		return s;
	}
};


class RICHED_DECL CManip
{
protected:
	using BaseFuncPtr = void(*)();
	CString m_strVal;
	int m_nVal;
	BaseFuncPtr m_pFunc;
	bool m_bVal;

public:
	virtual CRTFBuilder& go( CRTFBuilder& ) const = 0;

	CManip()
	{
		m_pFunc = NULL;
		m_nVal = 0;
		m_strVal = "";
	}

	CManip( BaseFuncPtr p, CString s )
	{
		m_pFunc = p;
		m_strVal = s;
	}

	CManip( BaseFuncPtr p, int n )
	{
		m_pFunc = p;
		m_nVal = n;
	}

	CManip( BaseFuncPtr p, bool b )
	{
		m_pFunc = p;
		m_bVal = b;
	}
};


class RICHED_DECL CStringManip : public CManip
{
public:
	CStringManip( RTFSM_STRINGPFUNC p, const CString& s = "" ) : CManip( reinterpret_cast<BaseFuncPtr>(p), s )
	{
	}

	CRTFBuilder& go( CRTFBuilder& b ) const override
	{
		return reinterpret_cast<RTFSM_STRINGPFUNC>(m_pFunc)( b, m_strVal );
	}
};


class RICHED_DECL CControlManip : public CManip
{
protected:

	CRichEditCtrl& m_control;

public:

	CControlManip( RTFSM_CONTROLPFUNC p, CRichEditCtrl& c ) : CManip( reinterpret_cast<BaseFuncPtr>(p), "" ), m_control( c )
	{
	}

	CRTFBuilder& go( CRTFBuilder& b ) const override
	{
		return reinterpret_cast<RTFSM_CONTROLPFUNC>(m_pFunc)( b, m_control );
	}
};


class RICHED_DECL CIntManip : public CManip
{
public:

	CIntManip( RTFSM_INTPFUNC p, int n = 0 ) : CManip( reinterpret_cast<BaseFuncPtr>(p), n )
	{
	}

	CRTFBuilder& go( CRTFBuilder& b ) const override
	{
		return reinterpret_cast<RTFSM_INTPFUNC>(m_pFunc)( b, m_nVal );
	}
};


class RICHED_DECL CBoolManip : public CManip
{
public:

	CBoolManip( RTFSM_BOOLPFUNC p, bool b ) : CManip( reinterpret_cast<BaseFuncPtr>(p), b )
	{
	}

	CRTFBuilder& go( CRTFBuilder& b ) const override
	{
		return reinterpret_cast<RTFSM_BOOLPFUNC>(m_pFunc)( b, m_bVal );
	}
};


class RICHED_DECL CRTFBuilder
{
protected:
	CString m_string;
	CTextAttributes m_attr;
	CFontList m_fontList;
	CColorList m_colorList;
	std::stack<CTextAttributes> m_attrStack;

public:

	void bold( bool b = true );
	void strike( bool b = true );
	void italic( bool b = true );
	void underline( bool b = true );
	void normal();
	void size( int n );
	void font( const CString& i );
	void black();
	void blue();
	void green();
	void red();
	void color( COLORREF );
	void backColor( COLORREF );

	void push();
	void pop();

	CRTFBuilder();
	virtual ~CRTFBuilder();

	void addFont( const CString& s )
	{
		m_fontList.add( s );
	}

	void addColor( COLORREF c )
	{
		m_colorList.add( c );
	}

	CRTFBuilder& operator+=( const CString& s );
	CRTFBuilder& operator+=( LPCTSTR p );

	operator CString() const
	{
		return m_string;
	}

	void write( CRichEditCtrl& );

	int colorCount() const
	{
		return m_colorList.size();
	}

public:

	CRTFBuilder& operator<<( LPCTSTR );
	CRTFBuilder& operator<<( int );
	CRTFBuilder& operator>>( CRichEditCtrl& );

	friend RICHED_DECL CRTFBuilder& normal( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& push( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& pop( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& black( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& red( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& green( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& blue( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& bold( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& strike( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& italic( CRTFBuilder& );
	friend RICHED_DECL CRTFBuilder& underline( CRTFBuilder& );
};


RICHED_DECL CControlManip write( CRichEditCtrl& );
RICHED_DECL CIntManip normal( int = 0 );
RICHED_DECL CIntManip push( int = 0 );
RICHED_DECL CIntManip pop( int = 0 );
RICHED_DECL CIntManip size( int );
RICHED_DECL CIntManip color( int );
RICHED_DECL CIntManip backColor( int );
RICHED_DECL CIntManip addColor( int );
RICHED_DECL CIntManip font( int );
RICHED_DECL CStringManip font( LPCTSTR );
RICHED_DECL CStringManip addFont( LPCTSTR );
RICHED_DECL CBoolManip bold( bool );
RICHED_DECL CBoolManip strike( bool );
RICHED_DECL CBoolManip italic( bool );
RICHED_DECL CBoolManip underline( bool );

RICHED_DECL CRTFBuilder& operator<<( CRTFBuilder&, RTFSM_PFUNC );
RICHED_DECL CRTFBuilder& operator<<( CRTFBuilder&, const CManip& m );


class RICHED_DECL CRichEditCtrlEx : public CRichEditCtrl
{
public:

	// Construction
	CRichEditCtrlEx();

public:

	void enable( bool b = true )
	{
		ModifyStyle( b ? WS_DISABLED : 0, b ? 0 : WS_DISABLED, 0 );
	}

	void disable( bool b = true )
	{
		enable( !b );
	}

	void readOnly( bool b = true )
	{
		SetReadOnly( b );
	}

	void writable( bool b = true )
	{
		readOnly( !b );
	}

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRichEditCtrlEx)
protected:
	virtual void PreSubclassWindow();
	//}}AFX_VIRTUAL

public:
	virtual ~CRichEditCtrlEx();

protected:
	//{{AFX_MSG(CRichEditCtrlEx)
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
};

#endif // RICHEDITCTRLEX_H