//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Defines a connection (output-to-input) between two entities.
//
//			The behavior in-game is as follows:
//
//			When the given output in the source entity is triggered, the given
//			input in the target entity is called after a specified delay, and
//			the parameter override (if any) is passed to the input handler. If
//			there is no parameter override, the default parameter is passed.
//
//			This behavior will occur a specified number of times before the
//			connection between the two entities is removed.
//
//=============================================================================//

#ifndef ENTITYCONNECTION_H
#define ENTITYCONNECTION_H
#ifdef _WIN32
#pragma once
#endif

#include "utlvector.h"
#include "fgdlib/inputoutput.h"
#include "tier1/utlobjectreference.h"


#define EVENT_FIRE_ALWAYS	-1

enum SortDirection_t
{
	Sort_Ascending = 0,
	Sort_Descending,
};

enum
{
	CONNECTION_NONE,	// if entity list has no outputs
	CONNECTION_GOOD,	// if all entity outpus are good
	CONNECTION_BAD,	// if any entity output is bad
};

class CMapDoc;
class CMapEntity;
typedef CUtlReferenceVector<CMapEntity> CMapEntityList;

class CEntityConnection
{
public:
	CEntityConnection(void);
	CEntityConnection(const CEntityConnection &Other );
	~CEntityConnection();

	CEntityConnection &operator =(const CEntityConnection &Other);

	bool CompareConnection(CEntityConnection *pConnection);

	float GetDelay(void) { return(m_fDelay); }
	void SetDelay(float fDelay) { m_fDelay = fDelay; }

	const char *GetInputName(void) { return(m_szInput); }
	void SetInputName(const char *pszName) { lstrcpyn(m_szInput, pszName, sizeof(m_szInput)); }

	const char *GetOutputName(void) { return(m_szOutput); }
	void SetOutputName(const char *pszName) { lstrcpyn(m_szOutput, pszName, sizeof(m_szOutput)); }

	const char *GetTargetName(void) { return(m_szTargetEntity); }
	void SetTargetName(const char *pszName);

	const char *GetSourceName(void) { return(m_szSourceEntity); }
	void SetSourceName(const char *pszName);

	void LinkSourceEntities();
	void LinkTargetEntities();

	bool AreAnyTargetEntitiesVisible();

	CMapEntityList *GetSourceEntityList() { return m_pSourceEntityList; }
	CMapEntityList *GetTargetEntityList() { return m_pTargetEntityList; }

	int GetTimesToFire(void) { return(m_nTimesToFire); }
	void SetTimesToFire(int nTimesToFire) { m_nTimesToFire = nTimesToFire; }

	const char *GetParam(void) { return(m_szParam); }
	void SetParam(const char *pszParam) { lstrcpyn(m_szParam, pszParam, sizeof(m_szParam)); }

	// Sorting functions
	static int CALLBACK CompareDelaysSecondary(CEntityConnection *pConn1, CEntityConnection *pConn2, SortDirection_t eDirection);
	static int CALLBACK CompareDelays(CEntityConnection *pConn1, CEntityConnection *pConn2, SortDirection_t eDirection);
	static int CALLBACK CompareOutputNames(CEntityConnection *pConn1, CEntityConnection *pConn2, SortDirection_t eDirection);
	static int CALLBACK CompareInputNames(CEntityConnection *pConn1, CEntityConnection *pConn2, SortDirection_t eDirection);
	static int CALLBACK CompareSourceNames(CEntityConnection *pConn1, CEntityConnection *pConn2, SortDirection_t eDirection);
	static int CALLBACK CompareTargetNames(CEntityConnection *pConn1, CEntityConnection *pConn2, SortDirection_t eDirection);

	// Validation functions
	static bool ValidateOutput(CMapEntity *pEntity, const char* pszOutput);
	static bool ValidateOutput(const CMapEntityList *pEntityList, const char* pszOutput);
	static bool ValidateTarget(const CMapEntityList *pEntityList, bool bVisibilityCheck, const char* pszTarget);
	static bool ValidateInput( const char *pszTarget, const char *pszInput, bool bVisiblesOnly, CMapDoc *pDoc = NULL );

	static int  ValidateOutputConnections( CMapEntity *pEntity, bool bVisibilityCheck, bool bIgnoreHiddenTargets=false, bool CheckAllDocuments = false );
	static int  ValidateInputConnections(CMapEntity *pEntity, bool bVisibilityCheck);

	static void FindBadConnections( CMapEntity *pEntity, bool bVisibilityCheck, CUtlVector<CEntityConnection *> &BadConnectionList, bool bIgnoreHiddenTargets=false, bool CheckAllDocuments = false );
	static void FixBadConnections(CMapEntity *pEntity, bool bVisibilityCheck);

protected:
	char m_szSourceEntity[MAX_ENTITY_NAME_LEN];		// Targetname of the source entity
	CMapEntityList *m_pSourceEntityList;
	char m_szOutput[MAX_IO_NAME_LEN];				// Name of the output in the source entity.
	char m_szTargetEntity[MAX_ENTITY_NAME_LEN];		// Targetname of the target entity.
	CMapEntityList *m_pTargetEntityList;
	char m_szInput[MAX_IO_NAME_LEN];				// Name of the input to fire in the target entity.
	char m_szParam[MAX_IO_NAME_LEN];				// Parameter override, if any.

	float m_fDelay;									// Delay before firing this outout.
	int m_nTimesToFire;								// Maximum times to fire this output or EVENT_FIRE_ALWAYS.
};

typedef CUtlVector<CEntityConnection *> CEntityConnectionList;




#endif // ENTITYCONNECTION_H