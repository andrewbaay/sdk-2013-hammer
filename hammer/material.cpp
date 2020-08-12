//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Implementation of IEditorTexture interface for materials.
//
//			Materials are kept in a directory tree containing pairs of VMT
//			and VTF files. Each pair of files represents a material.
//
//=============================================================================//

#include "stdafx.h"
#include <afxtempl.h>
#include "hammer.h"
#include "MapDoc.h"
#include "Material.h"
#include "GlobalFunctions.h"
#include "BSPFile.h"
#include "materialsystem/IMaterialSystem.h"
#include "materialsystem/MaterialSystem_Config.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/IMaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/ishaderapi.h"
#include "FileSystem.h"
#include "tier1/strtools.h"
#include "tier1/fmtstr.h"
#include "tier0/dbg.h"
#include "TextureSystem.h"
#include "materialproxyfactory_wc.h"
#include "options.h"
#include "pixelwriter.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


#pragma warning(disable:4244)

MaterialSystem_Config_t g_materialSystemConfig;


//-----------------------------------------------------------------------------
// Purpose:
// This class speeds up the call to IMaterial::GetPreviewImageProperties because
// we call it thousands of times per level load when there are detail props.
//-----------------------------------------------------------------------------
class CPreviewImagePropertiesCache
{
public:
	//-----------------------------------------------------------------------------
	// Purpose: Anyone can call this instead of IMaterial::GetPreviewImageProperties
	// and it'll be a lot faster if there are redundant calls to it.
	//-----------------------------------------------------------------------------
	static PreviewImageRetVal_t GetPreviewImageProperties( IMaterial *pMaterial, int *width, int *height, ImageFormat *imageFormat, bool* isTranslucent )
	{
		int i = s_PreviewImagePropertiesCache.Find( pMaterial );
		if ( i == s_PreviewImagePropertiesCache.InvalidIndex() )
		{
			// Add an entry to the cache.
			CPreviewImagePropertiesCache::CEntry entry;
			entry.m_RetVal = pMaterial->GetPreviewImageProperties( &entry.m_Width, &entry.m_Height, &entry.m_ImageFormat, &entry.m_bIsTranslucent );
			i = s_PreviewImagePropertiesCache.Insert( pMaterial, entry );
		}

		const CPreviewImagePropertiesCache::CEntry& entry = s_PreviewImagePropertiesCache[i];
		*width = entry.m_Width;
		*height = entry.m_Height;
		*imageFormat = entry.m_ImageFormat;
		*isTranslucent = entry.m_bIsTranslucent && pMaterial->GetMaterialVarFlag( MATERIAL_VAR_TRANSLUCENT );

		return entry.m_RetVal;
	}

	static void InvalidateMaterial( IMaterial* pMaterial )
	{
		int i = s_PreviewImagePropertiesCache.Find( pMaterial );
		if ( i != s_PreviewImagePropertiesCache.InvalidIndex() )
			s_PreviewImagePropertiesCache.RemoveAt( i );
	}

private:

	class CEntry
	{
	public:
		int m_Width;
		int m_Height;
		ImageFormat m_ImageFormat;
		bool m_bIsTranslucent;
		PreviewImageRetVal_t m_RetVal;
	};

	static bool PreviewImageLessFunc( IMaterial* const& a, IMaterial* const& b )
	{
		return a < b;
	}

	static CUtlMap<IMaterial*, CPreviewImagePropertiesCache::CEntry> s_PreviewImagePropertiesCache;
};
CUtlMap<IMaterial*, CPreviewImagePropertiesCache::CEntry> CPreviewImagePropertiesCache::s_PreviewImagePropertiesCache( 64, 64, &CPreviewImagePropertiesCache::PreviewImageLessFunc );


//-----------------------------------------------------------------------------
// Purpose: stuff for caching textures in memory.
//-----------------------------------------------------------------------------
class CMaterialImageCache
{
public:
	CMaterialImageCache( int maxNumGraphicsLoaded );
	~CMaterialImageCache();
	void EnCache( CMaterial* pMaterial );

protected:

	CMaterial** pool;
	int cacheSize;
	int currentID;  // next one to get killed.
};


//-----------------------------------------------------------------------------
// Purpose: Constructor. Allocates a pool of material pointers.
// Input  : maxNumGraphicsLoaded -
//-----------------------------------------------------------------------------
CMaterialImageCache::CMaterialImageCache( int maxNumGraphicsLoaded )
{
	cacheSize = maxNumGraphicsLoaded;
	pool = new CMaterialPtr[cacheSize];
	memset( pool, 0, sizeof( CMaterialPtr ) * cacheSize );
	currentID = 0;
}


//-----------------------------------------------------------------------------
// Purpose: Destructor. Frees the pool memory.
//-----------------------------------------------------------------------------
CMaterialImageCache::~CMaterialImageCache()
{
	delete[] pool;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pMaterial -
//-----------------------------------------------------------------------------
void CMaterialImageCache::EnCache( CMaterial* pMaterial )
{
	if ( pMaterial->m_pData != NULL )
		// Already cached.
		return;

	// kill currentID
	if ( pool[currentID] && pool[currentID]->HasData() )
		pool[currentID]->FreeData();

	pool[currentID] = pMaterial;
	pMaterial->LoadMaterialImage();
	currentID = ( currentID + 1 ) % cacheSize;

#if 0
	OutputDebugString( "CMaterialCache::Encache: " );
	OutputDebugString( pMaterial->m_szName );
	OutputDebugString( "\n" );
#endif
}


static CMaterialImageCache* g_pMaterialImageCache = NULL;


//-----------------------------------------------------------------------------
// Purpose: Constructor. Initializes data members.
//-----------------------------------------------------------------------------
CMaterial::CMaterial()
{
	memset( m_szName, 0, sizeof( m_szName ) );
	memset( m_szKeywords, 0, sizeof( m_szKeywords ) );

	m_nWidth = 0;
	m_nHeight = 0;
	m_pData = NULL;
	m_bLoaded = false;
	m_pMaterial = NULL;
	m_TranslucentBaseTexture = false;
	m_bIsWater = TRS_NONE;
}


//-----------------------------------------------------------------------------
// Purpose: Destructor. Frees texture image data and palette.
//-----------------------------------------------------------------------------
CMaterial::~CMaterial()
{
	//
	// Free image data.
	//
	if (m_pData != NULL)
	{
		free( m_pData );
		m_pData = NULL;
	}

	/* FIXME: Texture manager shuts down after the material system
	if (m_pMaterial)
	{
		m_pMaterial->DecrementReferenceCount();
		m_pMaterial = NULL;
	}
	*/
}


#define MATERIAL_PREFIX_LEN	10
//-----------------------------------------------------------------------------
// Finds all .VMT files in a particular directory
//-----------------------------------------------------------------------------
bool CMaterial::LoadMaterialsInDirectory( char const* pDirectoryName, IMaterialEnumerator* pEnum, int nContext, int nFlags )
{
	CFmtStrN<MAX_PATH> pWildCard( "%s/*.vmt", pDirectoryName );
	if ( !g_pFullFileSystem )
		return false;

	FileFindHandle_t findHandle;
	const char* pFileName = g_pFullFileSystem->FindFirstEx( pWildCard, "GAME", &findHandle );
	while ( pFileName )
	{
		if ( IsIgnoredMaterial( pFileName ) )
		{
			pFileName = g_pFullFileSystem->FindNext( findHandle );
			continue;
		}

		if ( !g_pFullFileSystem->FindIsDirectory( findHandle ) )
		{
			// Strip off the 'materials/' part of the material name.
			CFmtStrN<MAX_PATH> pFileNameWithPath( "%s/%s", &pDirectoryName[MATERIAL_PREFIX_LEN], pFileName );
			V_strnlwr( pFileNameWithPath.Access(), pFileNameWithPath.Length() );

			// Strip off the extension...
			if ( char* pExt = V_strrchr( pFileNameWithPath.Access(), '.' ) )
				*pExt = 0;

			if ( !pEnum->EnumMaterial( pFileNameWithPath, nContext ) )
				return false;
		}
		pFileName = g_pFullFileSystem->FindNext( findHandle );
	}
	g_pFullFileSystem->FindClose( findHandle );
	return true;
}


//-----------------------------------------------------------------------------
// Discovers all .VMT files lying under a particular directory
// It only finds their names so we can generate shell materials for them
// that we can load up at a later time
//-----------------------------------------------------------------------------
bool CMaterial::InitDirectoryRecursive( char const* pDirectoryName, IMaterialEnumerator* pEnum, int nContext, int nFlags )
{
	// Make sure this is an ok directory, otherwise don't bother
	if ( ShouldSkipMaterial( pDirectoryName + MATERIAL_PREFIX_LEN, nFlags ) )
		return true;

	if ( !LoadMaterialsInDirectory( pDirectoryName, pEnum, nContext, nFlags ) )
		return false;

	FileFindHandle_t findHandle;
	CFmtStrN<MAX_PATH> pWildCard( "%s/*.*", pDirectoryName );
	const char* pFileName = g_pFullFileSystem->FindFirstEx( pWildCard, "GAME", &findHandle );
	while ( pFileName )
	{
		if ( IsIgnoredMaterial( pFileName ) )
		{
			pFileName = g_pFullFileSystem->FindNext( findHandle );
			continue;
		}

		if ( pFileName[0] != '.' || ( pFileName[1] != '.' && pFileName[1] != 0 ) )
		{
			if ( g_pFullFileSystem->FindIsDirectory( findHandle ) )
			{
				CFmtStrN<MAX_PATH> pFileNameWithPath( "%s/%s", pDirectoryName, pFileName );
				if ( !InitDirectoryRecursive( pFileNameWithPath, pEnum, nContext, nFlags ) )
					return false;
			}
		}

		pFileName = g_pFullFileSystem->FindNext( findHandle );
	}

	return true;
}


//-----------------------------------------------------------------------------
// Discovers all .VMT files lying under a particular directory
// It only finds their names so we can generate shell materials for them
// that we can load up at a later time
//-----------------------------------------------------------------------------
void CMaterial::EnumerateMaterials( IMaterialEnumerator* pEnum, const char* szRoot, int nContext, int nFlags )
{
	InitDirectoryRecursive( szRoot, pEnum, nContext, nFlags );
}


//-----------------------------------------------------------------------------
// Purpose: Called from GetFirst/GetNextMaterialName to skip unwanted materials.
// Input  : pszName - Name of material to evaluate.
//			nFlags - One or more of the following:
//				INCLUDE_ALL_MATERIALS
//				INCLUDE_WORLD_MATERIALS
//				INCLUDE_MODEL_MATERIALS
// Output : Returns true to skip, false to not skip this material.
//-----------------------------------------------------------------------------
bool CMaterial::ShouldSkipMaterial( const char* pszName, int nFlags )
{
	//static char szStrippedName[MAX_PATH];

	// if NULL skip it
	if ( !pszName )
		return true;

	//
	// check against the list of exclusion directories
	//
	for ( int i = 0; i < g_pGameConfig->m_MaterialExclusions.Count(); i++ )
	{
		// This will guarantee the match is at the start of the string
		const char* pMatchFound = V_stristr( pszName, g_pGameConfig->m_MaterialExclusions[i].szDirectory );
		if ( pMatchFound == pszName )
			return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Factory. Creates a material by name.
// Input  : pszMaterialName - Name of material, ie "brick/brickfloor01".
// Output : Returns a pointer to the new material object, NULL if the given
//			material did not exist.
//-----------------------------------------------------------------------------
CMaterial* CMaterial::CreateMaterial( const char* pszMaterialName, bool bLoadImmediately, bool* pFound )
{
	Assert( pszMaterialName );
	CMaterial* pMaterial = new CMaterial;

	// Store off the material name so we can load it later if we need to
	V_sprintf_safe( pMaterial->m_szName, pszMaterialName );

	//
	// Find the material by name and load it.
	//
	if ( bLoadImmediately )
	{
		// Returns if the material was found or not
		if ( bool bFound = pMaterial->LoadMaterial(); pFound )
			*pFound = bFound;
	}

	return pMaterial;
}

bool CMaterial::IsIgnoredMaterial( const char* pName )
{
	// TODO: make this a customizable user option?
	if ( !V_strnicmp( pName, ".svn", 4 ) || V_strstr( pName, ".svn" ) ||
		!V_strnicmp( pName, "models", 6 ) || V_strstr( pName, "models" ) )
		return true;

	return false;
}
//-----------------------------------------------------------------------------
// Will actually load the material bits
// We don't want to load them all at once because it takes way too long
//-----------------------------------------------------------------------------
bool CMaterial::LoadMaterial()
{
	bool bFound = true;
	if ( !m_bLoaded )
	{
		if ( IsIgnoredMaterial( m_szName ) )
			return false;

		m_bLoaded = true;

		IMaterial* pMat = materials->FindMaterial( m_szName, TEXTURE_GROUP_OTHER );
		if ( IsErrorMaterial( pMat ) )
			bFound = false;

		Assert( pMat );

		if ( !pMat )
			return false;

		if ( !LoadMaterialHeader( pMat ) )
		{
			bFound = false;
			if ( ( pMat = materials->FindMaterial( "debug/debugempty", TEXTURE_GROUP_OTHER ) ) != nullptr )
				LoadMaterialHeader( pMat );
		}
	}

	return bFound;
}


//-----------------------------------------------------------------------------
// Reloads owing to a material change
//-----------------------------------------------------------------------------
void CMaterial::Reload( bool bFullReload )
{
	// Don't bother if we're not loaded yet
	if ( !m_bLoaded )
		return;

	FreeData();

	if ( m_pMaterial )
	{
		CPreviewImagePropertiesCache::InvalidateMaterial( m_pMaterial );
		m_pMaterial->DecrementReferenceCount();
	}
	m_pMaterial = materials->FindMaterial( m_szName, TEXTURE_GROUP_OTHER );
	Assert( m_pMaterial );

	if ( bFullReload )
		m_pMaterial->Refresh();

	bool translucentBaseTexture;
	ImageFormat eImageFormat;
	int width, height;
	PreviewImageRetVal_t retVal = CPreviewImagePropertiesCache::GetPreviewImageProperties( m_pMaterial, &width, &height, &eImageFormat, &translucentBaseTexture );
	if ( retVal == MATERIAL_PREVIEW_IMAGE_OK )
	{
		m_nWidth = width;
		m_nHeight = height;
		m_TranslucentBaseTexture = translucentBaseTexture;
	}

	m_bIsWater = TRS_NONE;

	// Find the keywords for this material from the vmt file.
	bool bFound;
	IMaterialVar* pVar = m_pMaterial->FindVar( "%keywords", &bFound, false );
	if ( bFound )
	{
		V_strcpy_safe( m_szKeywords, pVar->GetStringValue() );

		// Register the keywords
		g_Textures.RegisterTextureKeywords( this );
	}

	// Make sure to bump the refcount again. Not sure why this wasn't always done (check for leaks).
	if ( m_pMaterial )
		m_pMaterial->IncrementReferenceCount();
}


//-----------------------------------------------------------------------------
// Returns the material
//-----------------------------------------------------------------------------
IMaterial* CMaterial::GetMaterial( bool bForceLoad )
{
	if ( bForceLoad )
		LoadMaterial();

	return m_pMaterial;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterial::DrawIcon( CDC* pDC, CMaterial* pIcon, RECT& dstRect )
{
	if ( !pIcon )
		return;

	g_pMaterialImageCache->EnCache( pIcon );

	RECT rect, dst;
	rect.left = 0; rect.right = pIcon->GetWidth();

	// FIXME: Workaround the fact that materials must be power of 2, I want 12 bite
	rect.top = 2; rect.bottom = pIcon->GetHeight() - 2;

	dst = dstRect;
	float dstHeight = dstRect.bottom - dstRect.top;
	float srcAspect = (float)(rect.right - rect.left) / (float)(rect.bottom - rect.top);
	dst.right = dst.left + (dstHeight * srcAspect);
	pIcon->DrawBitmap( pDC, rect, dst );

	dstRect.left += dst.right - dst.left;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : pDC -
//			dstRect -
//			detectErrors -
//-----------------------------------------------------------------------------
void CMaterial::DrawBrowserIcons( CDC* pDC, RECT& dstRect, bool detectErrors )
{
	static CMaterial* pTranslucentIcon = nullptr;
	static CMaterial* pOpaqueIcon = nullptr;
	static CMaterial* pSelfIllumIcon = nullptr;
	static CMaterial* pBaseAlphaEnvMapMaskIcon = nullptr;
	static CMaterial* pErrorIcon = nullptr;

	if ( !pTranslucentIcon )
	{
		if ( ( pTranslucentIcon = CreateMaterial( "editor/translucenticon", true ) ) != nullptr )
			pTranslucentIcon->m_TranslucentBaseTexture = false;
		if ( ( pOpaqueIcon = CreateMaterial( "editor/opaqueicon", true ) ) != nullptr )
			pOpaqueIcon->m_TranslucentBaseTexture = false;
		if ( ( pSelfIllumIcon = CreateMaterial( "editor/selfillumicon", true ) ) != nullptr )
			pSelfIllumIcon->m_TranslucentBaseTexture = false;
		if ( ( pBaseAlphaEnvMapMaskIcon = CreateMaterial( "editor/basealphaenvmapmaskicon", true ) ) != nullptr )
			pBaseAlphaEnvMapMaskIcon->m_TranslucentBaseTexture = false;
		if ( ( pErrorIcon = CreateMaterial( "editor/erroricon", true ) ) != nullptr )
			pErrorIcon->m_TranslucentBaseTexture = false;

		Assert( pTranslucentIcon && pOpaqueIcon && pSelfIllumIcon && pBaseAlphaEnvMapMaskIcon && pErrorIcon );
	}

	bool error = false;
	IMaterial* pMaterial = GetMaterial();
	if ( pMaterial->GetMaterialVarFlag( MATERIAL_VAR_TRANSLUCENT ) )
	{
		DrawIcon( pDC, pTranslucentIcon, dstRect );
		if ( detectErrors )
			error = error || !m_TranslucentBaseTexture;
	}
	else
		DrawIcon( pDC, pOpaqueIcon, dstRect );

	if ( pMaterial->GetMaterialVarFlag( MATERIAL_VAR_SELFILLUM ))
	{
		DrawIcon( pDC, pSelfIllumIcon, dstRect );
		if ( detectErrors )
			error = error || !m_TranslucentBaseTexture;
	}

	if ( pMaterial->GetMaterialVarFlag( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
	{
		DrawIcon( pDC, pBaseAlphaEnvMapMaskIcon, dstRect );
		if ( detectErrors )
			error = error || !m_TranslucentBaseTexture;
	}

	if ( error )
		DrawIcon( pDC, pErrorIcon, dstRect );
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : pDC -
//			srcRect -
//			dstRect -
//-----------------------------------------------------------------------------
void CMaterial::DrawBitmap( CDC* pDC, const RECT& srcRect, const RECT& dstRect )
{
	int srcWidth = srcRect.right - srcRect.left;
	int srcHeight = srcRect.bottom - srcRect.top;

	BITMAPINFO bmi;
	memset( &bmi, 0, sizeof( bmi ) );
	BITMAPINFOHEADER& bmih = bmi.bmiHeader;
	bmih.biSize = sizeof( BITMAPINFOHEADER );
	bmih.biWidth = srcWidth;
	bmih.biHeight = -srcHeight;
	bmih.biCompression = BI_RGB;
	bmih.biPlanes = 1;

	bmih.biBitCount = m_TranslucentBaseTexture ? 32 : 24;
    bmih.biSizeImage = m_nWidth * m_nHeight * ( m_TranslucentBaseTexture ? 4 : 3 );

	int dest_width = dstRect.right - dstRect.left;
	int dest_height = dstRect.bottom - dstRect.top;

	if ( m_TranslucentBaseTexture )
	{
		void* data;
		CDC hdc;
		hdc.CreateCompatibleDC( pDC );

		auto bitmap = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, &data, NULL, 0x0 );
		CPixelWriter writer;
		writer.SetPixelMemory( IMAGE_FORMAT_BGRA8888, data, m_nWidth * 4 );

		constexpr int boxSize = 8;
		for ( int y = 0; y < m_nHeight; ++y )
		{
			writer.Seek( 0, y );
			for ( int x = 0; x < m_nWidth; ++x )
			{
				if ( ( x & boxSize ) ^ ( y & boxSize ) )
					writer.WritePixel( 102, 102, 102, 255 );
				else
					writer.WritePixel( 153, 153, 153, 255 );
			}
		}

		hdc.SelectObject( bitmap );
		pDC->BitBlt( dstRect.left, dstRect.top, dest_width, dest_height, &hdc, srcRect.left, -srcRect.top, SRCCOPY );
		DeleteObject( bitmap );

		bitmap = CreateBitmap( srcWidth, srcHeight, 1, 32, m_pData );
		hdc.SelectObject( bitmap );

		constexpr BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		if ( !pDC->AlphaBlend( dstRect.left, dstRect.top, dest_width, dest_height, &hdc, srcRect.left, -srcRect.top, srcWidth, srcHeight, bf ) )
			Msg( mwError, "CMaterial::Draw(): AlphaBlend failed." );

		DeleteObject( bitmap );
		return;
	}

	// ** bits **
	pDC->SetStretchBltMode( COLORONCOLOR );
	if ( StretchDIBits( pDC->m_hDC, dstRect.left, dstRect.top, dest_width, dest_height,
						srcRect.left, -srcRect.top, srcWidth, srcHeight, m_pData, &bmi, DIB_RGB_COLORS, SRCCOPY ) == GDI_ERROR )
		Msg( mwError, "CMaterial::Draw(): StretchDIBits failed." );
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pDC -
//			rect -
//			iFontHeight -
//			dwFlags -
//-----------------------------------------------------------------------------
void CMaterial::Draw( CDC* pDC, const RECT& rect, int iFontHeight, int iIconHeight, const DrawTexData_t& DrawTexData )
{
	g_pMaterialImageCache->EnCache( this );
	if ( !HasData() )
	{
NoData:
		// draw "no data"
		CFont* pOldFont = (CFont*)pDC->SelectStockObject( ANSI_VAR_FONT );
		COLORREF cr = pDC->SetTextColor( RGB( 0xff, 0xff, 0xff ) );
		COLORREF cr2 = pDC->SetBkColor( RGB( 0, 0, 0 ) );

		// draw black rect first
		pDC->FillRect( &rect, CBrush::FromHandle( HBRUSH( GetStockObject( BLACK_BRUSH ) ) ) );

		// then text
		pDC->TextOut( rect.left + 2, rect.top + 2, "No Image", 8 );
		pDC->SelectObject( pOldFont );
		pDC->SetTextColor( cr );
		pDC->SetBkColor( cr2 );
		return;
	}

	// no data -
	if ( !m_pData && !Load() )
	{
		// can't load -
		goto NoData;
	}

	// Draw the material image
	RECT srcRect, dstRect;
	srcRect.left = 0;
	srcRect.top = 0;
	srcRect.right = m_nWidth;
	srcRect.bottom = m_nHeight;
	dstRect = rect;

	if ( DrawTexData.nFlags & drawCaption )
		dstRect.bottom -= iFontHeight + 4;
	if ( DrawTexData.nFlags & drawIcons )
		dstRect.bottom -= iIconHeight;

	if ( !( DrawTexData.nFlags & drawResizeAlways ) )
	{
		if ( m_nWidth < dstRect.right - dstRect.left )
			dstRect.right = dstRect.left + m_nWidth;

		if ( m_nHeight < dstRect.bottom - dstRect.top )
			dstRect.bottom = dstRect.top + m_nHeight;
	}
	DrawBitmap( pDC, srcRect, dstRect );

	// Draw the icons
	if ( DrawTexData.nFlags & drawIcons )
	{
		dstRect = rect;
		if ( DrawTexData.nFlags & drawCaption )
			dstRect.bottom -= iFontHeight + 5;
		dstRect.top = dstRect.bottom - iIconHeight;
		DrawBrowserIcons( pDC, dstRect, ( DrawTexData.nFlags & drawErrors ) != 0 );
	}

	// ** caption **
	if ( DrawTexData.nFlags & drawCaption )
	{
		// draw background for name
		CBrush brCaption( RGB( 0, 0, 255 ) );
		CRect  rcCaption( rect );

		rcCaption.top = rcCaption.bottom - ( iFontHeight + 5 );
		pDC->FillRect( rcCaption, &brCaption );

		// draw name
		char szShortName[MAX_PATH];
		int iLen = GetShortName( szShortName );
		pDC->TextOut( rect.left, rect.bottom - ( iFontHeight + 4 ), szShortName, iLen );

		// draw usage count
		if ( DrawTexData.nFlags & drawUsageCount )
		{
			CString str;
			str.Format( "%d", DrawTexData.nUsageCount );
			CSize size = pDC->GetTextExtent( str );
			pDC->TextOut( rect.right - size.cx, rect.bottom - ( iFontHeight + 4 ), str );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterial::FreeData()
{
	free( m_pData );
	m_pData = NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Returns a string of comma delimited keywords associated with this
//			material.
// Input  : pszKeywords - Buffer to receive keywords, NULL to query string length.
// Output : Returns the number of characters in the keyword string.
//-----------------------------------------------------------------------------
int CMaterial::GetKeywords( char* pszKeywords ) const
{
	// To access keywords, we have to have the header loaded
	const_cast<CMaterial*>( this )->Load();
	if ( pszKeywords == NULL )
		return V_strlen( m_szKeywords );

	V_strcpy( pszKeywords, m_szKeywords );
	return V_strlen( m_szKeywords );
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pszName -
// Output : int
//-----------------------------------------------------------------------------
int CMaterial::GetShortName( char* pszName ) const
{
	if ( pszName == NULL )
		return V_strlen( m_szName );

	V_strcpy( pszName, m_szName );
	return V_strlen( m_szName );
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : material -
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CMaterial::LoadMaterialHeader( IMaterial* pMat )
{
	PreviewImageRetVal_t retVal;
	bool translucentBaseTexture;
	ImageFormat eImageFormat;
	int width, height;
	retVal = CPreviewImagePropertiesCache::GetPreviewImageProperties( pMat, &width, &height, &eImageFormat, &translucentBaseTexture);
	if ( retVal == MATERIAL_PREVIEW_IMAGE_BAD )
		return false;

	m_pMaterial = pMat;
	m_pMaterial->IncrementReferenceCount();

	m_nWidth = width;
	m_nHeight = height;
	m_TranslucentBaseTexture = translucentBaseTexture;

	// Find the keywords for this material from the vmt file.
	bool bFound;
	IMaterialVar* pVar = pMat->FindVar( "%keywords", &bFound, false );
	if ( bFound )
	{
		V_strcpy_safe( m_szKeywords, pVar->GetStringValue() );

		// Register the keywords
		g_Textures.RegisterTextureKeywords( this );
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the full path of the file from which this material was loaded.
//-----------------------------------------------------------------------------
const char* CMaterial::GetFileName() const
{
	return m_szName;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMaterial::IsWater() const
{
	if ( m_bIsWater != TRS_NONE )
		return static_cast<bool>( m_bIsWater );
	bool bFound;
	IMaterialVar* pVar = m_pMaterial->FindVar( "$surfaceprop", &bFound, false );
	if ( bFound )
	{
		if ( !V_stricmp( "water", pVar->GetStringValue() ) )
		{
			const_cast<CMaterial*>( this )->m_bIsWater = TRS_TRUE;
			return true;
		}
	}

	const_cast<CMaterial*>( this )->m_bIsWater = TRS_FALSE;
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Loads this material's image from disk if it is not already loaded.
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CMaterial::Load()
{
	LoadMaterial();
	return true;
}


//-----------------------------------------------------------------------------
// cache in the image size only when we need to
//-----------------------------------------------------------------------------
int CMaterial::GetImageWidth() const
{
	const_cast<CMaterial*>( this )->Load();
	return m_nWidth;
}

int CMaterial::GetImageHeight() const
{
	const_cast<CMaterial*>( this )->Load();
	return m_nHeight;
}

int CMaterial::GetWidth() const
{
	const_cast<CMaterial*>( this )->Load();
	return m_nWidth;
}

int CMaterial::GetHeight() const
{
	const_cast<CMaterial*>( this )->Load();
	return m_nHeight;
}

float CMaterial::GetDecalScale() const
{
	const_cast<CMaterial*>( this )->Load();

	bool found;
	IMaterialVar* decalScaleVar = m_pMaterial->FindVar( "$decalScale", &found, false );
	return !found ? 1.0f : decalScaleVar->GetFloatValue();
}

//-----------------------------------------------------------------------------
// Purpose:
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CMaterial::LoadMaterialImage()
{
	Load();

	if ( !m_nWidth || !m_nHeight )
		return false;

	m_pData = malloc( m_nWidth * m_nHeight * ( m_TranslucentBaseTexture ? 4 : 3 ) );
	Assert( m_pData );

	ImageFormat imageFormat = m_TranslucentBaseTexture ? IMAGE_FORMAT_BGRA8888 : IMAGE_FORMAT_BGR888;

	PreviewImageRetVal_t retVal = m_pMaterial->GetPreviewImage( (unsigned char*)m_pData, m_nWidth, m_nHeight, imageFormat );
	if ( retVal == MATERIAL_PREVIEW_IMAGE_OK && m_TranslucentBaseTexture )
	{
		auto data = (unsigned char*)m_pData;
		auto size = m_nWidth * size_t( m_nHeight ) * 4;
		for ( size_t i = 0; i < size; i += 4 )
		{
			auto a = data[i + 3];
			data[i + 0] = ( data[i + 0] * a ) / 255;
			data[i + 1] = ( data[i + 1] * a ) / 255;
			data[i + 2] = ( data[i + 2] * a ) / 255;
		}
	}
	return retVal != MATERIAL_PREVIEW_IMAGE_BAD;
}


static void InitMaterialSystemConfig( MaterialSystem_Config_t& pConfig )
{
	pConfig.bEditMode = true;
	pConfig.m_nAASamples = 0;
	pConfig.SetFlag( MATSYS_VIDCFG_FLAGS_DISABLE_BUMPMAP, true);
	// When I do this the model browser layout is horked...
	// pConfig->SetFlag( MATSYS_VIDCFG_FLAGS_USING_MULTIPLE_WINDOWS, true );
}


static constexpr const char* const s_rt_names[] = { "_rt_albedo", "_rt_normal", "_rt_position", "_rt_accbuf" };
constexpr const ImageFormat s_rt_formats[]={ IMAGE_FORMAT_RGBA32323232F, IMAGE_FORMAT_RGBA32323232F,
											 IMAGE_FORMAT_RGBA32323232F, IMAGE_FORMAT_RGBA16161616F };

static CTextureReference sg_ExtraFP16Targets[NELEMS( s_rt_names )];

void AllocateLightingPreviewtextures()
{
	constexpr int RT_SIZE = 1024;
	static bool bHaveAllocated=false;
	if ( !bHaveAllocated )
	{
		bHaveAllocated = true;
		MaterialSystemInterface()->BeginRenderTargetAllocation();
		for ( uint idx = 0; idx < NELEMS( sg_ExtraFP16Targets ); idx++ )
			sg_ExtraFP16Targets[idx].Init(
				materials->CreateNamedRenderTargetTextureEx2(
					s_rt_names[idx],
					RT_SIZE, RT_SIZE, RT_SIZE_LITERAL,
					s_rt_formats[idx], idx % 3 ? MATERIAL_RT_DEPTH_NONE : MATERIAL_RT_DEPTH_SEPARATE,
					TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
					CREATERENDERTARGETFLAGS_HDR )
				);

		// End block in which all render targets should be allocated (kicking off an Alt-Tab type
		// behavior)
		MaterialSystemInterface()->EndRenderTargetAllocation();
	}
}

abstract_class IShaderSystem
{
public:
	virtual ShaderAPITextureHandle_t GetShaderAPITextureBindHandle( ITexture *pTexture, int nFrameVar, int nTextureChannel = 0 ) =0;

	// Binds a texture
	virtual void BindTexture( Sampler_t sampler1, ITexture *pTexture, int nFrameVar = 0 ) = 0;
	virtual void BindTexture( Sampler_t sampler1, Sampler_t sampler2, ITexture *pTexture, int nFrameVar = 0 ) = 0;

	// Takes a snapshot
	virtual void TakeSnapshot( ) = 0;

	// Draws a snapshot
	virtual void DrawSnapshot( bool bMakeActualDrawCall = true ) = 0;

	// Are we using graphics?
	virtual bool IsUsingGraphics() const = 0;

	// Are we using graphics?
	virtual bool CanUseEditorMaterials() const = 0;
};

abstract_class IShaderSystemInternal : public IShaderInit, public IShaderSystem
{
public:
	// Initialization, shutdown
	virtual void		Init() = 0;
	virtual void		Shutdown() = 0;
	virtual void		ModInit() = 0;
	virtual void		ModShutdown() = 0;

	// Methods related to reading in shader DLLs
	virtual bool		LoadShaderDLL( const char *pFullPath ) = 0;
	virtual void		UnloadShaderDLL( const char* pFullPath ) = 0;

	// ...
};

abstract_class ILoadShader
{
public:
	virtual void LoadShaderDll( const char* fullDllPath ) = 0;
};


//-----------------------------------------------------------------------------
// Purpose:
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CMaterial::Initialize( HWND hwnd )
{
	{
		auto shaderSystem = dynamic_cast<IShaderSystemInternal*>( static_cast<IShaderSystem*>( materials->QueryInterface( "ShaderSystem002" ) ) );
		char szGameDir[_MAX_PATH], szBaseDir[_MAX_PATH];
		APP()->GetDirectory( DIR_MOD, szGameDir );

		V_strcat_safe( szGameDir, "\\bin" );
		V_FixSlashes( szGameDir );
		V_FixDoubleSlashes( szGameDir );
		V_strcpy_safe( szBaseDir, szGameDir );
		V_strcat_safe( szGameDir, "\\*.dll" );

		FileFindHandle_t find = 0;

		CSysModule* module = nullptr;
		ILoadShader* load = nullptr;
		Sys_LoadInterface( "hammer/bin/hammer_shader_dx9.dll", "ILoadShaderDll001", &module, reinterpret_cast<void**>( &load ) );

		for ( const char* name = g_pFullFileSystem->FindFirstEx( szGameDir, nullptr, &find ); name; name = g_pFullFileSystem->FindNext( find ) )
		{
			if ( !V_stristr( name, "shader" ) )
				continue;

			CFmtStrN<MAX_PATH> path( "%s\\%s", szBaseDir, name );

			if ( Sys_LoadModule( path, SYS_NOLOAD ) )
				continue;

			load->LoadShaderDll( path );
		}

		g_pFullFileSystem->FindClose( find );

		g_pFullFileSystem->AddSearchPath( "hammer", "GAME" );
		shaderSystem->LoadShaderDLL( "hammer/bin/hammer_shader_dx9.dll" );

		Sys_UnloadModule( module ); // decrement ref
	}

	// NOTE: This gets set to true later upon creating a 3d view.
	g_materialSystemConfig = materials->GetCurrentConfigForVideoCard();
	InitMaterialSystemConfig( g_materialSystemConfig );

	// Create a cache for material images (for browsing and uploading to the driver).
	if (g_pMaterialImageCache == NULL)
	{
		g_pMaterialImageCache = new CMaterialImageCache(500);
		if (g_pMaterialImageCache == NULL)
			return false;
	}

	materials->OverrideConfig( g_materialSystemConfig, false );

	// Set the mode
	// When setting the mode, we need to grab the parent window
	// since that's going to enclose all our little render windows
	g_materialSystemConfig.m_VideoMode.m_Width = g_materialSystemConfig.m_VideoMode.m_Height = 0;
	g_materialSystemConfig.m_VideoMode.m_Format = IMAGE_FORMAT_BGRA8888;
	g_materialSystemConfig.m_VideoMode.m_RefreshRate = 0;
	g_materialSystemConfig.SetFlag( MATSYS_VIDCFG_FLAGS_WINDOWED, true );
	g_materialSystemConfig.SetFlag( MATSYS_VIDCFG_FLAGS_RESIZING, true );
	g_materialSystemConfig.SetFlag(	MATSYS_VIDCFG_FLAGS_STENCIL, true );
	g_materialSystemConfig.SetFlag(	MATSYS_VIDCFG_FLAGS_USING_MULTIPLE_WINDOWS, true );

	if ( Options.general.bMaterialProxies )
		materials->SetMaterialProxyFactory( GetHammerMaterialProxyFactory() );

	bool res = materials->SetMode( hwnd, g_materialSystemConfig );

	materials->ReloadMaterials();

	return res;
}



//-----------------------------------------------------------------------------
// Purpose: Restores the material system to an uninitialized state.
//-----------------------------------------------------------------------------
void CMaterial::ShutDown()
{
	for ( int i = 0; i < NELEMS( sg_ExtraFP16Targets ); ++i )
		sg_ExtraFP16Targets[i].Shutdown();

	if ( materials != NULL )
		materials->UncacheAllMaterials();

	delete g_pMaterialImageCache;
	g_pMaterialImageCache = NULL;
}