//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ====
//
// Purpose: Implements a factory for entity helpers. When an entity is created,
//			the helpers from its class definition in the FGD are each instantiated
//			and added as children of the entity.
//
//=============================================================================

#include "stdafx.h"
#include "helperfactory.h"
#include "fgdlib/helperinfo.h"
#include "mapalignedbox.h"
#include "mapanimator.h"
#include "MapAxisHandle.h"
#include "mapdecal.h"
#include "mapfrustum.h"
#include "mapkeyframe.h"
#include "maplightcone.h"
#include "mapline.h"
#include "mapsprite.h"
#include "mapsphere.h"
#include "mapstudiomodel.h"
#include "mapoverlay.h"
#include "mappointhandle.h"
#include "mapquadbounds.h"
#include "maplight.h"
#include "mapsidelist.h"
#include "mapcylinder.h"
#include "MapInstance.h"
#include "mapsweptplayerhull.h"
#include "mapworldtext.h"
#include "DispShore.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


using HELPERFACTORY = CMapClass*(*)( CHelperInfo *, CMapEntity *);

typedef struct
{
	const char*	pszName;
	HELPERFACTORY pfnFactory;
} HelperFactoryMap_t;


static const constexpr HelperFactoryMap_t HelperFactoryMap[] =
{
	{ "sprite", CMapSprite::CreateMapSprite },					// Sprite, gets its render mode from the SPR file, has a selection handle.
	{ "iconsprite", CMapSprite::CreateMapSprite },				// Masked alpha sprite, no selection handle.
	{ "studio", CMapStudioModel::CreateMapStudioModel },		// Studio model with an axial bounding box.
	{ "studioprop", CMapStudioModel::CreateMapStudioModel },	// Studio model with an oriented bounding box.
	{ "lightprop", CMapStudioModel::CreateMapStudioModel },		// Studio model with an oriented bounding box, reverses pitch.
	{ "quadbounds", CMapQuadBounds::CreateQuadBounds },			// Extracts the verts from a quad face of a brush.
	{ "animator", CMapAnimator::CreateMapAnimator },			// Previews the motion of a moving entity (keyframe follower).
	{ "keyframe", CMapKeyFrame::CreateMapKeyFrame },			// Autonames when cloned and draws lines to the next keyframe.
	{ "decal", CMapDecal::CreateMapDecal },						// Decal that automatically attaches to nearby faces.
	{ "wirebox", CMapAlignedBox::Create },						// Wireframe box that can extract its mins & maxs.
	{ "line", CMapLine::Create },								// Line drawn between any two entities.
	{ "nodelink", CMapLine::Create },							// Line drawn between any two navigation nodes.
	{ "lightcone", CMapLightCone::Create },						// Faceted cone showing light angles and falloff.
	{ "frustum", CMapFrustum::Create },							// Wireframe frustum.
	{ "sphere", CMapSphere::Create },							// Wireframe sphere indicating a radius.
	{ "overlay", CMapOverlay::CreateMapOverlay },				// A decal with manipulatable vertices.
	{ "light", CMapLight::CreateMapLight },						// Light source for lighting preview.
	{ "sidelist", CMapSideList::CreateMapSideList },			// List of CMapFace pointers set by face ID.
	{ "origin", CMapPointHandle::Create },						// Handle used to manipulate the origin of an entity.
	{ "vecline", CMapPointHandle::Create },						// Handle used to manipulate another point that draws a line back to its parent entity.
	{ "axis", CMapAxisHandle::Create },							// Handle used to manipulate the axis of an entity.
	{ "cylinder", CMapCylinder::Create },						// Wireframe cylinder with separate radii at each end
	{ "sweptplayerhull", CMapSweptPlayerHull::Create },			// A swept player sized hull between two points (ladders)
	{ "overlay_transition", CMapOverlayTransition::Create },	// Notes!!
	{ "instance", CMapInstance::Create },						// A map instance used for rendering the sub-map
	{ "worldtext", CWorldTextHelper::CreateWorldText }			// Text string oriented in world space
};


//-----------------------------------------------------------------------------
// Purpose: Creates a helper from a helper info object.
// Input  : pHelperInfo - holds information about the helper to be created and
//				arguments to be passed to the helper when it is created.
//			pParent - The entity that will be the helper's parent.
// Output : Returns a pointer to the newly created helper.
//-----------------------------------------------------------------------------
CMapClass *CHelperFactory::CreateHelper(CHelperInfo *pHelperInfo, CMapEntity *pParent)
{
	//
	// Look up the helper factory function in the factory function table.
	//
	for ( const HelperFactoryMap_t& i : HelperFactoryMap )
	{
		//
		// If the function was found in the table, create the helper and return it.
		//
		if (!stricmp( i.pszName, pHelperInfo->GetName()))
		{
			CMapClass *pHelper = i.pfnFactory(pHelperInfo, pParent);
			return pHelper;
		}
	}

	return(NULL);
}

