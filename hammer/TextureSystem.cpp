//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Texture management functions. Exposes a list of available textures,
//			texture groups, and Most Recently Used textures.
//
//			There is one texture context per game configuration in GameCfg.ini.
//
//=============================================================================//

#include "stdafx.h"
#include "DummyTexture.h"		// Specific IEditorTexture implementation
#include "GlobalFunctions.h"
#include "MainFrm.h"
#include "MapDoc.h"
#include "Material.h"			// Specific IEditorTexture implementation
#include "Options.h"
#include "TextureSystem.h"
#include "hammer.h"
#include "filesystem.h"
#include "materialsystem/ITexture.h"
#include "materialsystem/imaterialvar.h"
#include "tier1/utldict.h"
#include "FaceEditSheet.h"

#include "tier1/KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


#pragma warning(disable:4244)


#define IsSortChr(ch) ((ch == '-') || (ch == '+'))



//-----------------------------------------------------------------------------
// List of global graphics
//-----------------------------------------------------------------------------
CTextureSystem g_Textures;



//-----------------------------------------------------------------------------
// CMaterialFileChangeWatcher implementation.
//-----------------------------------------------------------------------------
void CMaterialFileChangeWatcher::Init( CTextureSystem* pSystem, int context )
{
	m_pTextureSystem = pSystem;
	m_Context = context;

	m_Watcher.Init( this );

	char searchPaths[1024 * 16];
	if ( g_pFullFileSystem->GetSearchPath_safe( "GAME", false, searchPaths ) > 0 )
	{
		CUtlVector<char*> searchPathList;
		V_SplitString( searchPaths, ";", searchPathList );

		for ( int i = 0; i < searchPathList.Count(); i++ )
		{
			if ( V_stristr( searchPathList[i], ".vpk" ) )
				continue; // no vpks
			m_Watcher.AddDirectory( searchPathList[i], "materials", true );
		}

		searchPathList.PurgeAndDeleteElements();
	}
	else
		Warning( "Error in GetSearchPath. Dynamic material list updating will not be available." );
}

void CMaterialFileChangeWatcher::OnFileChange( const char* pRelativeFilename, const char* pFullFilename )
{
	//Msg( "OnNewFile: %s\n", pRelativeFilename );

	CTextureSystem::EFileType eFileType;
	if ( CTextureSystem::GetFileTypeFromFilename( pRelativeFilename, eFileType ) )
		m_pTextureSystem->OnFileChange( pRelativeFilename, m_Context, eFileType );
}

void CMaterialFileChangeWatcher::Update()
{
	m_Watcher.Update();
}


//-----------------------------------------------------------------------------
// Purpose: Constructor. Creates the "All" group and sets it as the active group.
//-----------------------------------------------------------------------------
CTextureSystem::CTextureSystem()
{
	m_pLastTex = nullptr;
	m_nLastIndex = 0;
	m_pActiveContext = nullptr;
	m_pActiveGroup = nullptr;
	m_pCubemapTexture = nullptr;
	m_pNoDrawTexture = nullptr;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTextureSystem::FreeAllTextures()
{
	if ( m_pCubemapTexture )
	{
	 	m_pCubemapTexture->DecrementReferenceCount();
		m_pCubemapTexture = nullptr;
	}

	int nContextCount = m_TextureContexts.Count();
	for ( int nContext = 0; nContext < nContextCount; nContext++ )
	{
		TextureContext_t& context = m_TextureContexts.Element( nContext );

		//
		// Delete all the texture groups for this context.
		//
		context.Groups.PurgeAndDeleteElements();

		//
		// Delete dummy textures.
		//
		context.Dummies.PurgeAndDeleteElements();
	}

	//
	// Delete all the textures from the master list.
	//
	m_Textures.PurgeAndDeleteElements();

	m_pLastTex = nullptr;
	m_nLastIndex = -1;

	CDummyTexture::DestroyDummyMaterial();

	// Delete the keywords.
	m_Keywords.PurgeAndDeleteElements();
	m_ChangeWatchers.PurgeAndDeleteElements();
}


//-----------------------------------------------------------------------------
// Purpose: Adds a texture to the master list of textures.
// Input  : pTexture - Pointer to texture to add.
// Output : Returns the index of the texture in the master texture list.
//-----------------------------------------------------------------------------
int CTextureSystem::AddTexture( IEditorTexture* pTexture )
{
	return m_Textures.AddToTail( pTexture );
}


//-----------------------------------------------------------------------------
// Purpose: Begins iterating the list of texture/material keywords.
//-----------------------------------------------------------------------------
int CTextureSystem::GetNumKeywords() const
{
	return m_Keywords.Count();
}


//-----------------------------------------------------------------------------
// Purpose: Continues iterating the list of texture/material keywords.
//-----------------------------------------------------------------------------
const char* CTextureSystem::GetKeyword( int pos ) const
{
	return m_Keywords.Element( pos );
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *piIndex -
//			bUseMRU -
// Output :
//-----------------------------------------------------------------------------
IEditorTexture* CTextureSystem::EnumActiveTextures( int& piIndex ) const
{
	if ( m_pActiveGroup != nullptr )
	{
		IEditorTexture* pTex = nullptr;
		do
		{
			pTex = m_pActiveGroup->GetTexture( piIndex );
			if ( pTex != nullptr )
			{
				++piIndex;
				return pTex;
			}
		} while ( pTex != nullptr );
	}

	return nullptr;
}


//-----------------------------------------------------------------------------
// Purpose: Initializes the texture system.
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CTextureSystem::Initialize( HWND hwnd )
{
	return CMaterial::Initialize( hwnd );
}


//-----------------------------------------------------------------------------
// Purpose: Shuts down the texture system.
//-----------------------------------------------------------------------------
void CTextureSystem::ShutDown()
{
	if ( m_pActiveContext )
	{
		KeyValuesAD history( "Texture History" );
		CNumStr n;
		for ( int i = 0; i < m_pActiveContext->MRU.Count(); i++ )
			history->SetString( ( n.SetInt32( i ), n.String() ), m_pActiveContext->MRU[i]->GetFileName() );
		*APP()->GetProfileKeyValues( "Texture Browser" )->FindKey( "Texture History", true ) = *history;
	}

	CMaterial::ShutDown();
	FreeAllTextures();
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : pszName -
//			piIndex -
//			bDummy -
// Output :
//-----------------------------------------------------------------------------
IEditorTexture *CTextureSystem::FindActiveTexture( LPCSTR pszInputName, int* piIndex, BOOL bDummy )
{

	// The .vmf file format gets confused if there are backslashes in material names,
	// so make sure they're all using forward slashes here.
	char szName[MAX_PATH];
	V_StrSubst( pszInputName, "\\", "/", szName, sizeof( szName ) );
	const char *pszName = szName;
	IEditorTexture *pTex = nullptr;
	//
	// Check the cache first.
	//
	if ( m_pLastTex && !V_stricmp( pszName, m_pLastTex->GetName() ) )
	{
		if ( piIndex )
			*piIndex = m_nLastIndex;

		return m_pLastTex;
	}

	int iIndex = 0;

	// We're finding by name, so we don't care what the format is as long as the name matches.
	if ( m_pActiveGroup )
	{
		pTex = m_pActiveGroup->FindTextureByName( pszName, &iIndex );
		if ( pTex )
		{
			if ( piIndex )
				*piIndex = iIndex;

			m_pLastTex = pTex;
			m_nLastIndex = iIndex;

			return pTex;
		}
	}

	//
	// Caller doesn't want dummies.
	//
	if ( !bDummy )
		return nullptr;

	Assert( !piIndex );

	//
	// Check the list of dummies for a texture with the same name and texture format.
	//
	if ( m_pActiveContext )
	{
		int nDummyCount = m_pActiveContext->Dummies.Count();
		for ( int nDummy = 0; nDummy < nDummyCount; nDummy++ )
		{
			IEditorTexture* pTex = m_pActiveContext->Dummies.Element( nDummy );
			if ( !V_stricmp( pszName, pTex->GetName() ) )
			{
				m_pLastTex = pTex;
				m_nLastIndex = -1;
				return pTex;
			}
		}

		//
		// Not found; add a dummy as a placeholder for the missing texture.
		//
		pTex = AddDummy( pszName );
	}

	if ( pTex != nullptr )
	{
		m_pLastTex = pTex;
		m_nLastIndex = -1;
	}

	return pTex;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pTex -
//-----------------------------------------------------------------------------
void CTextureSystem::AddMRU( IEditorTexture* pTex )
{
	if ( !m_pActiveContext )
		return;

	int nIndex = m_pActiveContext->MRU.Find( pTex );
	if ( nIndex != -1 )
		m_pActiveContext->MRU.Remove( nIndex );
	else if ( m_pActiveContext->MRU.Count() == 8 )
		m_pActiveContext->MRU.Remove( 7 );

	m_pActiveContext->MRU.AddToHead( pTex );
}


//-----------------------------------------------------------------------------
// Purpose: Returns the texture context that corresponds to the given game config.
//-----------------------------------------------------------------------------
TextureContext_t* CTextureSystem::FindTextureContextForConfig( CGameConfig* pConfig )
{
	for ( int i = 0; i < m_TextureContexts.Count(); i++ )
	{
		if ( m_TextureContexts.Element( i ).pConfig == pConfig )
			return &m_TextureContexts.Element( i );
	}

	return nullptr;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTextureSystem::SetActiveConfig( CGameConfig* pConfig )
{
	if ( TextureContext_t* pContext = FindTextureContextForConfig( pConfig ) )
	{
		m_pActiveContext = pContext;
		m_pActiveGroup = m_pActiveContext->pAllGroup;

		if ( auto history = APP()->GetProfileKeyValues( "Texture Browser" )->FindKey( "Texture History" ); m_pActiveContext && history )
		{
			CUtlVector<const char*> names;
			FOR_EACH_SUBKEY( history, i )
				names.AddToTail( i->GetString() );
			for ( int i = names.Count() - 1; i >= 0; i-- )
				AddMRU( FindActiveTexture( names[i] ) );
		}
	}
	else
		m_pActiveContext = nullptr;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : char *pcszName -
//-----------------------------------------------------------------------------
void CTextureSystem::SetActiveGroup( const char* pcszName )
{
	if ( !m_pActiveContext )
		return;

	char szBuf[MAX_PATH];
	V_sprintf_safe( szBuf, "textures\\%s", pcszName );

	int iCount = m_pActiveContext->Groups.Count();
	for ( int i = 0; i < iCount; i++ )
	{
		CTextureGroup *pGroup = m_pActiveContext->Groups.Element(i);
		if ( !V_stricmp( pGroup->GetName(), pcszName ) )
		{
			m_pActiveGroup = pGroup;
			return;
		}

		if ( V_strstr( pGroup->GetName(), pcszName ) )
		{
			m_pActiveGroup = pGroup;
			return;
		}

	}

	TRACE0("No Group Found!");
}


void HammerFileSystem_ReportSearchPath( const char* szPathID )
{
	char szSearchPath[ 4096 ];
	g_pFullFileSystem->GetSearchPath( szPathID, true, szSearchPath, sizeof( szSearchPath ) );

	Msg( mwStatus, "------------------------------------------------------------------" );

	char* pszOnePath = strtok( szSearchPath, ";" );
	while ( pszOnePath )
	{
		Msg( mwStatus, "Search Path (%s): %s", szPathID, pszOnePath );
		pszOnePath = strtok( nullptr, ";" );
	}
}


//-----------------------------------------------------------------------------
// FIXME: Make this work correctly, using the version in filesystem_tools.cpp
// (it doesn't work currently owing to filesystem setup issues)
//-----------------------------------------------------------------------------
void HammerFileSystem_SetGame( const char* pExeDir, const char* pModDir )
{
	static bool s_bOnce = false;
	Assert( !s_bOnce );
	s_bOnce = true;

	char buf[MAX_PATH];

	V_sprintf_safe( buf, "%s\\hl2", pExeDir );
	if ( g_pFullFileSystem->FileExists( buf ) )
		g_pFullFileSystem->AddSearchPath( buf, "GAME", PATH_ADD_TO_HEAD );

	if ( pModDir && *pModDir != '\0' )
		g_pFullFileSystem->AddSearchPath( pModDir, "GAME", PATH_ADD_TO_HEAD );

	HammerFileSystem_ReportSearchPath( "GAME" );
}


//-----------------------------------------------------------------------------
// Purpose: Loads textures from all texture files.
//-----------------------------------------------------------------------------
void CTextureSystem::LoadAllGraphicsFiles()
{
	FreeAllTextures();

	// For each game config...
	// dvs: Disabled for single-config running.
	//for (int nConfig = 0; nConfig < Options.configs.GetGameConfigCount(); nConfig++)
	{
		//CGameConfig *pConfig = Options.configs.GetGameConfig(nConfig);
		CGameConfig* pConfig = g_pGameConfig;

		// Create a new texture context with the materials for that config.
		TextureContext_t* pContext = AddTextureContext();

		// Bind it to this config.
		pContext->pConfig = pConfig;

		// Create a group to hold all the textures for this context.
		pContext->pAllGroup = new CTextureGroup( "All Textures" );
		pContext->Groups.AddToTail( pContext->pAllGroup );

		HammerFileSystem_SetGame( pConfig->m_szGameExeDir, pConfig->m_szModDir );

		// Set the new context as the active context.
		m_pActiveContext = pContext;

		// Load the materials for this config.
		// Do this unconditionally so that we get necessary editor materials.
		LoadMaterials( pConfig );

		m_pActiveContext->pAllGroup->Sort();
	}
}


//-----------------------------------------------------------------------------
// Purpose: Loads all the materials for the given game config.
//-----------------------------------------------------------------------------
void CTextureSystem::LoadMaterials( CGameConfig* pConfig )
{
	CTextureGroup* pGroup = new CTextureGroup( "Materials" );
	m_pActiveContext->Groups.AddToTail( pGroup );

	// Add all the materials to the group.
	CMaterial::EnumerateMaterials( this, "materials", (int)pGroup, INCLUDE_WORLD_MATERIALS );

	// Watch the materials directory recursively...
	CMaterialFileChangeWatcher* pWatcher = new CMaterialFileChangeWatcher;
	pWatcher->Init( this, (int)pGroup );
	m_ChangeWatchers.AddToTail( pWatcher );

	Assert( m_pCubemapTexture == nullptr );

	m_pCubemapTexture = MaterialSystemInterface()->FindTexture( "editor/cubemap", nullptr, true );

	if ( m_pCubemapTexture )
	{
		m_pCubemapTexture->IncrementReferenceCount();
		CMatRenderContextPtr pRenderContext( MaterialSystemInterface() );
		pRenderContext->BindLocalCubemap( m_pCubemapTexture );
	}

	// Get the nodraw texture.
	m_pNoDrawTexture = nullptr;
	for ( int i = 0; i < m_Textures.Count(); i++ )
	{
		if ( V_stricmp( m_Textures[i]->GetName(), "tools/toolsnodraw" ) == 0 )
		{
			m_pNoDrawTexture = m_Textures[i];
			break;
		}
	}
	if ( !m_pNoDrawTexture )
		m_pNoDrawTexture = CMaterial::CreateMaterial( "tools/toolsnodraw", true );
}

void CTextureSystem::RebindDefaultCubeMap()
{
	// rebind with the default cubemap
	if (  m_pCubemapTexture )
	{
		CMatRenderContextPtr pRenderContext( MaterialSystemInterface() );
		pRenderContext->BindLocalCubemap( m_pCubemapTexture );
	}
}


void CTextureSystem::UpdateFileChangeWatchers()
{
	for ( int i = 0; i < m_ChangeWatchers.Count(); i++ )
		m_ChangeWatchers[i]->Update();
}


void CTextureSystem::OnFileChange( const char *pFilename, int context, CTextureSystem::EFileType eFileType )
{
	// It requires the forward slashes later...
	char fixedSlashes[MAX_PATH];
	V_StrSubst( pFilename, "\\", "/", fixedSlashes, sizeof( fixedSlashes ) );

	// Get rid of the extension.
	if ( V_strlen( fixedSlashes ) < 5 )
	{
		Assert( false );
		return;
	}
	fixedSlashes[V_strlen( fixedSlashes ) - 4] = 0;


	// Handle it based on what type of file we've got.
	if ( eFileType == k_eFileTypeVMT )
	{
		IEditorTexture* pTex = FindActiveTexture( fixedSlashes, nullptr, FALSE );
		if ( pTex )
			pTex->Reload( true );
		else
		{
			EnumMaterial( fixedSlashes, context );
			IEditorTexture* pTex = FindActiveTexture( fixedSlashes, nullptr, FALSE );
			if ( pTex )
			{
				GetMainWnd()->m_TextureBar.NotifyNewMaterial( pTex );
				GetMainWnd()->GetFaceEditSheet()->NotifyNewMaterial( pTex );
			}
		}
	}
	else if ( eFileType == k_eFileTypeVTF )
	{
		// Whether a VTF was added, removed, or modified, we do the same thing.. refresh it and any materials that reference it.
		ITexture* pTexture = materials->FindTexture( fixedSlashes, TEXTURE_GROUP_UNACCOUNTED, false );
		if ( pTexture )
		{
			pTexture->Download( nullptr );
			ReloadMaterialsUsingTexture( pTexture );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Load any materials that reference this texture. Used so we can refresh a
// material's preview image if a relevant .vtf changes.
//-----------------------------------------------------------------------------
void CTextureSystem::ReloadMaterialsUsingTexture( ITexture* pTestTexture )
{
	for ( int i = 0; i < m_Textures.Count(); i++ )
	{
		IEditorTexture* pEditorTex = m_Textures[i];
		IMaterial* pMat = pEditorTex->GetMaterial( false );
		if ( !pMat )
			continue;

		IMaterialVar** pParams = pMat->GetShaderParams();
		int nParams = pMat->ShaderParamCount();
		for ( int iParam=0; iParam < nParams; iParam++ )
		{
			if ( pParams[iParam]->GetType() != MATERIAL_VAR_TYPE_TEXTURE )
				continue;

			ITexture* pTex = pParams[iParam]->GetTextureValue();
			if ( !pTex )
				continue;

			if ( pTex == pTestTexture )
				pEditorTex->Reload( true );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Figure out the file type from its extension. Returns false if we don't have an enum for that extension.
//-----------------------------------------------------------------------------
bool CTextureSystem::GetFileTypeFromFilename( const char* pFilename, CTextureSystem::EFileType& pFileType )
{
	char strRight[16];
	V_StrRight( pFilename, 4, strRight, sizeof( strRight ) );
	if ( V_stricmp( strRight, ".vmt" ) == 0 )
	{
		pFileType = CTextureSystem::k_eFileTypeVMT;
		return true;
	}
	else if ( V_stricmp( strRight, ".vtf" ) == 0 )
	{
		pFileType = CTextureSystem::k_eFileTypeVTF;
		return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Loads textures from all texture files.
//-----------------------------------------------------------------------------
void CTextureSystem::ReloadTextures( const char* pFilterName )
{
	MaterialSystemInterface()->ReloadMaterials( pFilterName );

	for ( int i = 0; i < m_Textures.Count(); i++ )
	{
		if ( !V_stristr( pFilterName, m_Textures[i]->GetName() ) )
			continue;

		m_Textures[i]->Reload( false );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Adds a placeholder texture for a texture that exists in the map, but
//			was not found on disk.
// Input  : pszName - Name of missing texture.
// Output : Returns a pointer to the new dummy texture.
//-----------------------------------------------------------------------------
IEditorTexture* CTextureSystem::AddDummy( LPCTSTR pszName )
{
	if ( !m_pActiveContext )
		return nullptr;

	IEditorTexture* pTex = new CDummyTexture( pszName );
	m_pActiveContext->Dummies.AddToTail( pTex );

	return pTex;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : elem1 -
//			elem2 -
// Output : static int __cdecl
//-----------------------------------------------------------------------------
static int __cdecl SortTexturesProc( IEditorTexture* const* elem1, IEditorTexture* const* elem2 )
{
	auto pElem1 = *elem1;
	auto pElem2 = *elem2;

	Assert( pElem1 != nullptr && pElem2 != nullptr );
	if ( pElem1 == nullptr || pElem2 == nullptr )
		return 0;

	const char* pszName1 = pElem1->GetName();
	const char* pszName2 = pElem2->GetName();

	char ch1 = pszName1[0];
	char ch2 = pszName2[0];

	if ( IsSortChr( ch1 ) && !IsSortChr( ch2 ) )
	{
		int iFamilyLen = V_strlen( pszName1 + 2 );
		int iFamily = V_strnicmp( pszName1 + 2, pszName2, iFamilyLen );
		if ( !iFamily )
			return -1; // same family - put elem1 before elem2
		return iFamily; // sort normally
	}
	else if ( !IsSortChr( ch1 ) && IsSortChr( ch2 ) )
	{
		int iFamilyLen = V_strlen( pszName2 + 2 );
		int iFamily = V_strnicmp( pszName1, pszName2 + 2, iFamilyLen );
		if ( !iFamily )
			return 1; // same family - put elem2 before elem1
		return iFamily; // sort normally
	}
	else if ( IsSortChr( ch1 ) && IsSortChr( ch2 ) )
	{
		// do family name sorting
		int iFamily = V_stricmp( pszName1 + 2, pszName2 + 2 );
		if ( !iFamily )
		{
			// same family - sort by number
			return pszName1[1] - pszName2[1];
		}

		// different family
		return iFamily;
	}

	return V_stricmp( pszName1, pszName2 );
}


//-----------------------------------------------------------------------------
// Purpose: Determines whether or not there is at least one available texture
//			group for a given texture format.
// Input  : format - Texture format to look for.
// Output : Returns TRUE if textures of a given format are available, FALSE if not.
//-----------------------------------------------------------------------------
bool CTextureSystem::HasTexturesForConfig( CGameConfig* pConfig )
{
	if ( !pConfig )
		return false;

	TextureContext_t* pContext = FindTextureContextForConfig( pConfig );
	if ( !pContext )
		return false;

	return !pContext->Groups.IsEmpty();
}


//-----------------------------------------------------------------------------
// Used to add all the world materials into the material list
//-----------------------------------------------------------------------------
bool CTextureSystem::EnumMaterial( const char* pMaterialName, int nContext )
{
	CTextureGroup* pGroup = (CTextureGroup*)nContext;
	CMaterial* pMaterial = CMaterial::CreateMaterial( pMaterialName, false );
	if ( pMaterial )
	{
		// Add it to the master list of textures.
		AddTexture( pMaterial );

		// Add the texture's index to the given group and to the "All" group.
		pGroup->AddTexture( pMaterial );
		if ( pGroup != m_pActiveContext->pAllGroup )
			m_pActiveContext->pAllGroup->AddTexture( pMaterial );
	}
	return true;
}


//-----------------------------------------------------------------------------
// Registers the keywords as existing in a particular material
//-----------------------------------------------------------------------------
void CTextureSystem::RegisterTextureKeywords( IEditorTexture* pTexture )
{
	//
	// Add any new keywords from this material to the list of keywords.
	//
	char szKeywords[MAX_PATH];
	pTexture->GetKeywords( szKeywords );
	if ( szKeywords[0] != '\0' )
	{
		char* pch = strtok( szKeywords, " ,;" );
		while ( pch )
		{
			// dvs: hide in a Find function
			bool bFound = false;
			for ( int pos = 0; pos < m_Keywords.Count(); pos++ )
			{
				const char* pszTest = m_Keywords.Element( pos );
				if ( !V_stricmp( pszTest, pch ) )
				{
					bFound = true;
					break;
				}
			}

			if ( !bFound )
				m_Keywords.AddToTail( V_strdup( pch ) );

			pch = strtok( nullptr, " ,;" );
		}
	}
}


//-----------------------------------------------------------------------------
// Used to lazily load in all the textures
//-----------------------------------------------------------------------------
void CTextureSystem::LazyLoadTextures()
{
	if ( m_pActiveContext && m_pActiveContext->pAllGroup )
		m_pActiveContext->pAllGroup->LazyLoadTextures();
}


//-----------------------------------------------------------------------------
// Purpose:
// Output : TextureContext_t
//-----------------------------------------------------------------------------
TextureContext_t* CTextureSystem::AddTextureContext()
{
	// Allocate a new texture context.
	int nIndex = m_TextureContexts.AddToTail();

	// Add the group to this config's list of texture groups.
	return &m_TextureContexts.Element( nIndex );
}


//-----------------------------------------------------------------------------
// Opens the source file associated with a material
//-----------------------------------------------------------------------------
void CTextureSystem::OpenSource( const char* pMaterialName )
{
	if ( !pMaterialName )
		return;

	char pRelativePath[MAX_PATH];
	V_sprintf_safe( pRelativePath, "materials/%s.vmt", pMaterialName );

	char pFullPath[MAX_PATH];
	if ( g_pFullFileSystem->GetLocalPath_safe( pRelativePath, pFullPath ) )
		ShellExecute( nullptr, "open", pFullPath, nullptr, nullptr, SW_SHOWNORMAL );
}

//-----------------------------------------------------------------------------
// Opens explorer dialog and selects the source file
//-----------------------------------------------------------------------------
void CTextureSystem::ExploreToSource( const char* pMaterialName )
{
	if ( !pMaterialName )
		return;

	char pRelativePath[MAX_PATH];
	V_sprintf_safe( pRelativePath, "materials/%s.vmt", pMaterialName );

	char pFullPath[MAX_PATH];
	if ( g_pFullFileSystem->GetLocalPath_safe( pRelativePath, pFullPath ) )
	{
		CString strSel = "/select, ";
		strSel += pFullPath;

		ShellExecute( nullptr, "open", "explorer", strSel, nullptr, SW_SHOWNORMAL );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Constructor.
// Input  : pszName - Name of group, ie "Materials" or "u:\hl\tfc\tfc.wad".
//-----------------------------------------------------------------------------
CTextureGroup::CTextureGroup( const char* pszName )
{
	V_strcpy_safe( m_szName, pszName );
	m_nTextureToLoad = 0;
}


//-----------------------------------------------------------------------------
// Purpose: Adds a texture to this group.
// Input  : pTexture - Texture to add.
//-----------------------------------------------------------------------------
void CTextureGroup::AddTexture( IEditorTexture* pTexture )
{
	int index = m_Textures.AddToTail( pTexture );
	m_TextureNameMap.Insert( pTexture->GetName(), index );
}


//-----------------------------------------------------------------------------
// Purpose: Sorts the group.
//-----------------------------------------------------------------------------
void CTextureGroup::Sort()
{
	m_Textures.Sort( SortTexturesProc );

	// Redo the name map.
	m_TextureNameMap.RemoveAll();
	for ( int i = 0; i < m_Textures.Count(); i++ )
	{
		IEditorTexture* pTex = m_Textures[i];
		m_TextureNameMap.Insert( pTex->GetName(), i );
	}

	// Changing the order means we don't know where we should be loading from
	m_nTextureToLoad = 0;
}


//-----------------------------------------------------------------------------
// Purpose: Retrieves a texture by index.
// Input  : nIndex - Index of the texture in this group.
//-----------------------------------------------------------------------------
IEditorTexture* CTextureGroup::GetTexture( int nIndex )
{
	if ( nIndex >= m_Textures.Count() || nIndex < 0 )
		return nullptr;
	return m_Textures[nIndex];
}


//-----------------------------------------------------------------------------
// finds a texture by name
//-----------------------------------------------------------------------------
IEditorTexture* CTextureGroup::GetTexture( char const* pName )
{
	for ( int i = 0; i < m_Textures.Count(); i++ )
		if ( auto tex = m_Textures[i]; !V_strcmp( pName, tex->GetName() ) )
			return tex;
	return nullptr;
}


//-----------------------------------------------------------------------------
// Quickly find a texture by name.
//-----------------------------------------------------------------------------
IEditorTexture* CTextureGroup::FindTextureByName( const char* pName, int* piIndex )
{
	int iMapEntry = m_TextureNameMap.Find( pName );
	if ( iMapEntry == m_TextureNameMap.InvalidIndex() )
		return nullptr;

	auto tex = m_Textures[m_TextureNameMap[iMapEntry]];
	if ( piIndex )
		*piIndex = (int)std::distance( m_Textures.begin(), &tex );
	return tex;
}


//-----------------------------------------------------------------------------
// Used to lazily load in all the textures
//-----------------------------------------------------------------------------
void CTextureGroup::LazyLoadTextures()
{
	// Load at most once per call
	while ( m_nTextureToLoad < m_Textures.Count() )
	{
		if ( !m_Textures[m_nTextureToLoad]->IsLoaded() )
		{
			m_Textures[m_nTextureToLoad]->Load();
			++m_nTextureToLoad;
			return;
		}

		// This one was already loaded; skip it
		++m_nTextureToLoad;
	}
}