//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Implementation of IEditorTexture interface for placeholder textures.
//			Placeholder textures are used for textures that are referenced in
//			the map file but not found in storage.
//
//=============================================================================//

#ifndef DUMMYTEXTURE_H
#define DUMMYTEXTURE_H
#ifdef _WIN32
#pragma once
#endif


#include "IEditorTexture.h"

class CDummyTexture : public IEditorTexture
{
	public:

		CDummyTexture(const char *pszName);
		virtual ~CDummyTexture();

		inline const char *GetName() const
		{
			return(m_szName);
		}
		int GetShortName(char *pszName) const;

		int GetKeywords(char *pszKeywords) const;

		void Draw(CDC *pDC, RECT &rect, int iFontHeight, int iIconHeight, DrawTexData_t &DrawTexData);

		const char *GetFileName(void) const;

		void GetSize(SIZE &size) const;

		inline bool IsDummy() const
		{
			return(true);
		}

		inline int GetImageWidth() const
		{
			return 256;
		}

		inline int GetImageHeight() const
		{
			return 256;
		}

		inline float GetDecalScale() const
		{
			return(1.0f);
		}

		CPalette *GetPalette() const
		{
			return(NULL);
		}

		inline int GetWidth() const
		{
			return 256;
		}

		inline int GetHeight() const
		{
			return 256;
		}

		inline int GetTextureID() const
		{
			return(0);
		}

		inline int GetSurfaceAttributes() const
		{
			return(0);
		}

		inline int GetSurfaceContents() const
		{
			return(0);
		}

		inline int GetSurfaceValue() const
		{
			return(0);
		}

		inline bool HasAlpha() const
		{
			return(false);
		}

		inline bool HasData() const
		{
			return(true);
		}

		inline bool HasPalette() const
		{
			return(false);
		}

		bool Load( void );
		void Reload( bool bFullReload ) {}

		inline bool IsLoaded() const
		{
			return true;
		}

		inline void SetTextureID( int nTextureID )
		{
		}

		bool IsWater( void ) const
		{
			return false;
		}

		IMaterial* GetMaterial( bool bForceLoad=true )
		{
			return errorMaterial;
		}

	protected:

		char m_szName[MAX_PATH];

		static IMaterial* errorMaterial;
};


#endif // DUMMYTEXTURE_H
