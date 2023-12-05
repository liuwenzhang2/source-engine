//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef ENTITYLIST_H
#define ENTITYLIST_H

#ifdef _WIN32
#pragma once
#endif

#include "entitylist_base.h"
#include "baseentity.h"
#include "collisionutils.h"
#include "datacache/imdlcache.h"
#include "tier0/vprof.h"

//class CBaseEntity;

// Implement this class and register with gEntList to receive entity create/delete notification
class IEntityListener
{
public:
	virtual void OnEntityCreated(CBaseEntity* pEntity) {};
	virtual void OnEntitySpawned(CBaseEntity* pEntity) {};
	virtual void OnEntityDeleted(CBaseEntity* pEntity) {};
};

abstract_class CBaseEntityClassList
{
public:
	CBaseEntityClassList();
	~CBaseEntityClassList();
	virtual void LevelShutdownPostEntity() = 0;

	CBaseEntityClassList *m_pNextClassList;
};

class CAimTargetManager : public IEntityListener
{
public:
	// Called by CEntityListSystem
	void LevelInitPreEntity();
	void LevelShutdownPostEntity();
	void Clear();
	void ForceRepopulateList();
	bool ShouldAddEntity(CBaseEntity* pEntity);
	// IEntityListener
	virtual void OnEntityCreated(CBaseEntity* pEntity);
	virtual void OnEntityDeleted(CBaseEntity* pEntity);
	void AddEntity(CBaseEntity* pEntity);
	void RemoveEntity(CBaseEntity* pEntity);
	int ListCount();
	int ListCopy(CBaseEntity* pList[], int listMax);

private:
	CUtlVector<CBaseEntity*>	m_targetList;
};

template< class T >
class CEntityClassList : public CBaseEntityClassList
{
public:
	virtual void LevelShutdownPostEntity()  { m_pClassList = NULL; }

	void Insert( T *pEntity )
	{
		pEntity->m_pNext = m_pClassList;
		m_pClassList = pEntity;
	}

	void Remove( T *pEntity )
	{
		T **pPrev = &m_pClassList;
		T *pCur = *pPrev;
		while ( pCur )
		{
			if ( pCur == pEntity )
			{
				*pPrev = pCur->m_pNext;
				return;
			}
			pPrev = &pCur->m_pNext;
			pCur = *pPrev;
		}
	}

	static T *m_pClassList;
};

// Derive a class from this if you want to filter entity list searches
abstract_class IEntityFindFilter
{
public:
	virtual bool ShouldFindEntity( CBaseEntity *pEntity ) = 0;
	virtual CBaseEntity *GetFilterResult( void ) = 0;
};


extern CUtlVector<IServerNetworkable*> g_DeleteList;
extern bool g_fInCleanupDelete;
extern void PhysOnCleanupDeleteList();

//-----------------------------------------------------------------------------
// Purpose: a global list of all the entities in the game.  All iteration through
//			entities is done through this object.
//-----------------------------------------------------------------------------
template<class T>
class CGlobalEntityList : public CBaseEntityList<T>
{
	typedef CBaseEntityList<T> BaseClass;
public:
private:
	int m_iHighestEnt; // the topmost used array index
	int m_iNumEnts;
	int m_iNumEdicts;

	bool m_bClearingEntities;
	CUtlVector<IEntityListener *>	m_entityListeners;

public:
	IServerNetworkable* GetServerNetworkable( CBaseHandle hEnt ) const;
	CBaseNetworkable* GetBaseNetworkable( CBaseHandle hEnt ) const;
	CBaseEntity* GetBaseEntity( CBaseHandle hEnt ) const;
	edict_t* GetEdict( CBaseHandle hEnt ) const;
	
	int NumberOfEntities( void );
	int NumberOfEdicts( void );

	// mark an entity as deleted
	void AddToDeleteList( IServerNetworkable *ent );
	// call this before and after each frame to delete all of the marked entities.
	void CleanupDeleteList( void );
	int ResetDeleteList( void );

	// frees all entities in the game
	void Clear( void );

	// Returns true while in the Clear() call.
	bool	IsClearingEntities()	{return m_bClearingEntities;}
	
	// add a class that gets notified of entity events
	void AddListenerEntity( IEntityListener *pListener );
	void RemoveListenerEntity( IEntityListener *pListener );

	void ReportEntityFlagsChanged( CBaseEntity *pEntity, unsigned int flagsOld, unsigned int flagsNow );

	// entity is about to be removed, notify the listeners
	void NotifyCreateEntity( CBaseEntity *pEnt );
	void NotifySpawn( CBaseEntity *pEnt );
	void NotifyRemoveEntity( CBaseHandle hEnt );
	// iteration functions

	// returns the next entity after pCurrentEnt;  if pCurrentEnt is NULL, return the first entity
	CBaseEntity *NextEnt( CBaseEntity *pCurrentEnt );
	CBaseEntity *FirstEnt() { return NextEnt(NULL); }

	// returns the next entity of the specified class, using RTTI
	template< class U >
	U *NextEntByClass( U *start )
	{
		for ( CBaseEntity *x = NextEnt( start ); x; x = NextEnt( x ) )
		{
			start = dynamic_cast<U*>( x );
			if ( start )
				return start;
		}
		return NULL;
	}

	// search functions
	bool		 IsEntityPtr( void *pTest );
	CBaseEntity *FindEntityByClassname( CBaseEntity *pStartEntity, const char *szName );
	CBaseEntity *FindEntityByName( CBaseEntity *pStartEntity, const char *szName, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL, IEntityFindFilter *pFilter = NULL );
	CBaseEntity *FindEntityByName( CBaseEntity *pStartEntity, string_t iszName, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL, IEntityFindFilter *pFilter = NULL )
	{
		return FindEntityByName( pStartEntity, STRING(iszName), pSearchingEntity, pActivator, pCaller, pFilter );
	}
	CBaseEntity *FindEntityInSphere( CBaseEntity *pStartEntity, const Vector &vecCenter, float flRadius );
	CBaseEntity *FindEntityByTarget( CBaseEntity *pStartEntity, const char *szName );
	CBaseEntity *FindEntityByModel( CBaseEntity *pStartEntity, const char *szModelName );

	CBaseEntity *FindEntityByNameNearest( const char *szName, const Vector &vecSrc, float flRadius, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL );
	CBaseEntity *FindEntityByNameWithin( CBaseEntity *pStartEntity, const char *szName, const Vector &vecSrc, float flRadius, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL );
	CBaseEntity *FindEntityByClassnameNearest( const char *szName, const Vector &vecSrc, float flRadius );
	CBaseEntity *FindEntityByClassnameWithin( CBaseEntity *pStartEntity , const char *szName, const Vector &vecSrc, float flRadius );
	CBaseEntity *FindEntityByClassnameWithin( CBaseEntity *pStartEntity , const char *szName, const Vector &vecMins, const Vector &vecMaxs );

	CBaseEntity *FindEntityGeneric( CBaseEntity *pStartEntity, const char *szName, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL );
	CBaseEntity *FindEntityGenericWithin( CBaseEntity *pStartEntity, const char *szName, const Vector &vecSrc, float flRadius, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL );
	CBaseEntity *FindEntityGenericNearest( const char *szName, const Vector &vecSrc, float flRadius, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL );
	
	CBaseEntity *FindEntityNearestFacing( const Vector &origin, const Vector &facing, float threshold);
	CBaseEntity *FindEntityClassNearestFacing( const Vector &origin, const Vector &facing, float threshold, char *classname);
	CBaseEntity *FindEntityByNetname( CBaseEntity *pStartEntity, const char *szModelName );

	CBaseEntity *FindEntityProcedural( const char *szName, CBaseEntity *pSearchingEntity = NULL, CBaseEntity *pActivator = NULL, CBaseEntity *pCaller = NULL );
	
	CGlobalEntityList();

// CBaseEntityList overrides.
protected:

	virtual void OnAddEntity( T *pEnt, CBaseHandle handle );
	virtual void OnRemoveEntity( T *pEnt, CBaseHandle handle );

};

extern CGlobalEntityList<CBaseEntity> gEntList;

//-----------------------------------------------------------------------------
// Inlines.
//-----------------------------------------------------------------------------
template<class T>
inline edict_t* CGlobalEntityList<T>::GetEdict( CBaseHandle hEnt ) const
{
	T *pUnk = (BaseClass::LookupEntity( hEnt ));
	if ( pUnk )
		return pUnk->GetNetworkable()->GetEdict();
	else
		return NULL;
}

template<class T>
inline CBaseNetworkable* CGlobalEntityList<T>::GetBaseNetworkable( CBaseHandle hEnt ) const
{
	T *pUnk = (BaseClass::LookupEntity( hEnt ));
	if ( pUnk )
		return pUnk->GetNetworkable()->GetBaseNetworkable();
	else
		return NULL;
}

template<class T>
inline IServerNetworkable* CGlobalEntityList<T>::GetServerNetworkable( CBaseHandle hEnt ) const
{
	T *pUnk = (BaseClass::LookupEntity( hEnt ));
	if ( pUnk )
		return pUnk->GetNetworkable();
	else
		return NULL;
}

template<class T>
inline CBaseEntity* CGlobalEntityList<T>::GetBaseEntity( CBaseHandle hEnt ) const
{
	T *pUnk = (BaseClass::LookupEntity( hEnt ));
	if ( pUnk )
		return pUnk->GetBaseEntity();
	else
		return NULL;
}

template<class T>
CGlobalEntityList<T>::CGlobalEntityList()
{
	m_iHighestEnt = m_iNumEnts = m_iNumEdicts = 0;
	m_bClearingEntities = false;
}

// mark an entity as deleted
template<class T>
void CGlobalEntityList<T>::AddToDeleteList(IServerNetworkable* ent)
{
	if (ent && ent->GetEntityHandle()->GetRefEHandle() != INVALID_EHANDLE_INDEX)
	{
		g_DeleteList.AddToTail(ent);
	}
}

extern bool g_bDisableEhandleAccess;
// call this before and after each frame to delete all of the marked entities.
template<class T>
void CGlobalEntityList<T>::CleanupDeleteList(void)
{
	VPROF("CGlobalEntityList::CleanupDeleteList");
	g_fInCleanupDelete = true;
	// clean up the vphysics delete list as well
	PhysOnCleanupDeleteList();

	g_bDisableEhandleAccess = true;
	for (int i = 0; i < g_DeleteList.Count(); i++)
	{
		g_DeleteList[i]->Release();
	}
	g_bDisableEhandleAccess = false;
	g_DeleteList.RemoveAll();

	g_fInCleanupDelete = false;
}

template<class T>
int CGlobalEntityList<T>::ResetDeleteList(void)
{
	int result = g_DeleteList.Count();
	g_DeleteList.RemoveAll();
	return result;
}


// add a class that gets notified of entity events
template<class T>
void CGlobalEntityList<T>::AddListenerEntity(IEntityListener* pListener)
{
	if (m_entityListeners.Find(pListener) >= 0)
	{
		AssertMsg(0, "Can't add listeners multiple times\n");
		return;
	}
	m_entityListeners.AddToTail(pListener);
}

template<class T>
void CGlobalEntityList<T>::RemoveListenerEntity(IEntityListener* pListener)
{
	m_entityListeners.FindAndRemove(pListener);
}

template<class T>
void CGlobalEntityList<T>::Clear(void)
{
	m_bClearingEntities = true;

	// Add all remaining entities in the game to the delete list and call appropriate UpdateOnRemove
	CBaseHandle hCur = BaseClass::FirstHandle();
	while (hCur != BaseClass::InvalidHandle())
	{
		IServerNetworkable* ent = GetServerNetworkable(hCur);
		if (ent)
		{
			MDLCACHE_CRITICAL_SECTION();
			// Force UpdateOnRemove to be called
			UTIL_Remove(ent);
		}
		hCur = BaseClass::NextHandle(hCur);
	}

	CleanupDeleteList();
	// free the memory
	g_DeleteList.Purge();

	CBaseEntity::m_nDebugPlayer = -1;
	CBaseEntity::m_bInDebugSelect = false;
	m_iHighestEnt = 0;
	m_iNumEnts = 0;

	m_bClearingEntities = false;
}

template<class T>
int CGlobalEntityList<T>::NumberOfEntities(void)
{
	return m_iNumEnts;
}

template<class T>
int CGlobalEntityList<T>::NumberOfEdicts(void)
{
	return m_iNumEdicts;
}

template<class T>
CBaseEntity* CGlobalEntityList<T>::NextEnt(CBaseEntity* pCurrentEnt)
{
	if (!pCurrentEnt)
	{
		const CEntInfo<T>* pInfo = BaseClass::FirstEntInfo();
		if (!pInfo)
			return NULL;

		return (CBaseEntity*)pInfo->m_pEntity;
	}

	// Run through the list until we get a CBaseEntity.
	const CEntInfo<T>* pList = BaseClass::GetEntInfoPtr(pCurrentEnt->GetRefEHandle());
	if (pList)
		pList = BaseClass::NextEntInfo(pList);

	while (pList)
	{
#if 0
		if (pList->m_pEntity)
		{
			T* pUnk = (const_cast<T*>(pList->m_pEntity));
			CBaseEntity* pRet = pUnk->GetBaseEntity();
			if (pRet)
				return pRet;
		}
#else
		return (CBaseEntity*)pList->m_pEntity;
#endif
		pList = pList->m_pNext;
	}

	return NULL;

}

extern CAimTargetManager g_AimManager;

template<class T>
void CGlobalEntityList<T>::ReportEntityFlagsChanged(CBaseEntity* pEntity, unsigned int flagsOld, unsigned int flagsNow)
{
	if (pEntity->IsMarkedForDeletion())
		return;
	// UNDONE: Move this into IEntityListener instead?
	unsigned int flagsChanged = flagsOld ^ flagsNow;
	if (flagsChanged & FL_AIMTARGET)
	{
		unsigned int flagsAdded = flagsNow & flagsChanged;
		unsigned int flagsRemoved = flagsOld & flagsChanged;

		if (flagsAdded & FL_AIMTARGET)
		{
			g_AimManager.AddEntity(pEntity);
		}
		if (flagsRemoved & FL_AIMTARGET)
		{
			g_AimManager.RemoveEntity(pEntity);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Used to confirm a pointer is a pointer to an entity, useful for
//			asserts.
//-----------------------------------------------------------------------------
template<class T>
bool CGlobalEntityList<T>::IsEntityPtr(void* pTest)
{
	if (pTest)
	{
		const CEntInfo<T>* pInfo = BaseClass::FirstEntInfo();
		for (; pInfo; pInfo = pInfo->m_pNext)
		{
			if (pTest == (void*)pInfo->m_pEntity)
				return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Iterates the entities with a given classname.
// Input  : pStartEntity - Last entity found, NULL to start a new iteration.
//			szName - Classname to search for.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByClassname(CBaseEntity* pStartEntity, const char* szName)
{
	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* pEntity = (CBaseEntity*)pInfo->m_pEntity;
		if (!pEntity)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (pEntity->ClassMatches(szName))
			return pEntity;
	}

	return NULL;
}

CBaseEntity* FindPickerEntity(CBasePlayer* pPlayer);

//-----------------------------------------------------------------------------
// Purpose: Finds an entity given a procedural name.
// Input  : szName - The procedural name to search for, should start with '!'.
//			pSearchingEntity - 
//			pActivator - The activator entity if this was called from an input
//				or Use handler.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityProcedural(const char* szName, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	//
	// Check for the name escape character.
	//
	if (szName[0] == '!')
	{
		const char* pName = szName + 1;

		//
		// It is a procedural name, look for the ones we understand.
		//
		if (FStrEq(pName, "player"))
		{
			return (CBaseEntity*)UTIL_PlayerByIndex(1);
		}
		else if (FStrEq(pName, "pvsplayer"))
		{
			if (pSearchingEntity)
			{
				return CBaseEntity::Instance(UTIL_FindClientInPVS(pSearchingEntity->edict()));
			}
			else if (pActivator)
			{
				// FIXME: error condition?
				return CBaseEntity::Instance(UTIL_FindClientInPVS(pActivator->edict()));
			}
			else
			{
				// FIXME: error condition?
				return (CBaseEntity*)UTIL_PlayerByIndex(1);
			}

		}
		else if (FStrEq(pName, "activator"))
		{
			return pActivator;
		}
		else if (FStrEq(pName, "caller"))
		{
			return pCaller;
		}
		else if (FStrEq(pName, "picker"))
		{
			return FindPickerEntity(UTIL_PlayerByIndex(1));
		}
		else if (FStrEq(pName, "self"))
		{
			return pSearchingEntity;
		}
		else
		{
			Warning("Invalid entity search name %s\n", szName);
			Assert(0);
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Iterates the entities with a given name.
// Input  : pStartEntity - Last entity found, NULL to start a new iteration.
//			szName - Name to search for.
//			pActivator - Activator entity if this was called from an input
//				handler or Use handler.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByName(CBaseEntity* pStartEntity, const char* szName, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller, IEntityFindFilter* pFilter)
{
	if (!szName || szName[0] == 0)
		return NULL;

	if (szName[0] == '!')
	{
		//
		// Avoid an infinite loop, only find one match per procedural search!
		//
		if (pStartEntity == NULL)
			return FindEntityProcedural(szName, pSearchingEntity, pActivator, pCaller);

		return NULL;
	}

	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (!ent->m_iName)
			continue;

		if (ent->NameMatches(szName))
		{
			if (pFilter && !pFilter->ShouldFindEntity(ent))
				continue;

			return ent;
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pStartEntity - 
//			szModelName - 
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByModel(CBaseEntity* pStartEntity, const char* szModelName)
{
	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (!ent->edict() || !ent->GetModelName())
			continue;

		if (FStrEq(STRING(ent->GetModelName()), szModelName))
			return ent;
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Iterates the entities with a given target.
// Input  : pStartEntity - 
//			szName - 
//-----------------------------------------------------------------------------
// FIXME: obsolete, remove
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByTarget(CBaseEntity* pStartEntity, const char* szName)
{
	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (!ent->m_target)
			continue;

		if (FStrEq(STRING(ent->m_target), szName))
			return ent;
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Used to iterate all the entities within a sphere.
// Input  : pStartEntity - 
//			vecCenter - 
//			flRadius - 
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityInSphere(CBaseEntity* pStartEntity, const Vector& vecCenter, float flRadius)
{
	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (!ent->edict())
			continue;

		Vector vecRelativeCenter;
		ent->CollisionProp()->WorldToCollisionSpace(vecCenter, &vecRelativeCenter);
		if (!IsBoxIntersectingSphere(ent->CollisionProp()->OBBMins(), ent->CollisionProp()->OBBMaxs(), vecRelativeCenter, flRadius))
			continue;

		return ent;
	}

	// nothing found
	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds the nearest entity by name within a radius
// Input  : szName - Entity name to search for.
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByNameNearest(const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	CBaseEntity* pEntity = NULL;

	//
	// Check for matching class names within the search radius.
	//
	float flMaxDist2 = flRadius * flRadius;
	if (flMaxDist2 == 0)
	{
		flMaxDist2 = MAX_TRACE_LENGTH * MAX_TRACE_LENGTH;
	}

	CBaseEntity* pSearch = NULL;
	while ((pSearch = gEntList.FindEntityByName(pSearch, szName, pSearchingEntity, pActivator, pCaller)) != NULL)
	{
		if (!pSearch->edict())
			continue;

		float flDist2 = (pSearch->GetAbsOrigin() - vecSrc).LengthSqr();

		if (flMaxDist2 > flDist2)
		{
			pEntity = pSearch;
			flMaxDist2 = flDist2;
		}
	}

	return pEntity;
}



//-----------------------------------------------------------------------------
// Purpose: Finds the first entity by name within a radius
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity name to search for.
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByNameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	//
	// Check for matching class names within the search radius.
	//
	CBaseEntity* pEntity = pStartEntity;
	float flMaxDist2 = flRadius * flRadius;
	if (flMaxDist2 == 0)
	{
		return gEntList.FindEntityByName(pEntity, szName, pSearchingEntity, pActivator, pCaller);
	}

	while ((pEntity = gEntList.FindEntityByName(pEntity, szName, pSearchingEntity, pActivator, pCaller)) != NULL)
	{
		if (!pEntity->edict())
			continue;

		float flDist2 = (pEntity->GetAbsOrigin() - vecSrc).LengthSqr();

		if (flMaxDist2 > flDist2)
		{
			return pEntity;
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds the nearest entity by class name withing given search radius.
// Input  : szName - Entity name to search for. Treated as a target name first,
//				then as an entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByClassnameNearest(const char* szName, const Vector& vecSrc, float flRadius)
{
	CBaseEntity* pEntity = NULL;

	//
	// Check for matching class names within the search radius.
	//
	float flMaxDist2 = flRadius * flRadius;
	if (flMaxDist2 == 0)
	{
		flMaxDist2 = MAX_TRACE_LENGTH * MAX_TRACE_LENGTH;
	}

	CBaseEntity* pSearch = NULL;
	while ((pSearch = gEntList.FindEntityByClassname(pSearch, szName)) != NULL)
	{
		if (!pSearch->edict())
			continue;

		float flDist2 = (pSearch->GetAbsOrigin() - vecSrc).LengthSqr();

		if (flMaxDist2 > flDist2)
		{
			pEntity = pSearch;
			flMaxDist2 = flDist2;
		}
	}

	return pEntity;
}



//-----------------------------------------------------------------------------
// Purpose: Finds the first entity within radius distance by class name.
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByClassnameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius)
{
	//
	// Check for matching class names within the search radius.
	//
	CBaseEntity* pEntity = pStartEntity;
	float flMaxDist2 = flRadius * flRadius;
	if (flMaxDist2 == 0)
	{
		return gEntList.FindEntityByClassname(pEntity, szName);
	}

	while ((pEntity = gEntList.FindEntityByClassname(pEntity, szName)) != NULL)
	{
		if (!pEntity->edict())
			continue;

		float flDist2 = (pEntity->GetAbsOrigin() - vecSrc).LengthSqr();

		if (flMaxDist2 > flDist2)
		{
			return pEntity;
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds the first entity within an extent by class name.
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity class name, ie "info_target".
//			vecMins - Search mins.
//			vecMaxs - Search maxs.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByClassnameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecMins, const Vector& vecMaxs)
{
	//
	// Check for matching class names within the search radius.
	//
	CBaseEntity* pEntity = pStartEntity;

	while ((pEntity = gEntList.FindEntityByClassname(pEntity, szName)) != NULL)
	{
		if (!pEntity->edict() && !pEntity->IsEFlagSet(EFL_SERVER_ONLY))
			continue;

		// check if the aabb intersects the search aabb.
		Vector entMins, entMaxs;
		pEntity->CollisionProp()->WorldSpaceAABB(&entMins, &entMaxs);
		if (IsBoxIntersectingBox(vecMins, vecMaxs, entMins, entMaxs))
		{
			return pEntity;
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds an entity by target name or class name.
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity name to search for. Treated as a target name first,
//				then as an entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityGeneric(CBaseEntity* pStartEntity, const char* szName, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	CBaseEntity* pEntity = NULL;

	pEntity = gEntList.FindEntityByName(pStartEntity, szName, pSearchingEntity, pActivator, pCaller);
	if (!pEntity)
	{
		pEntity = gEntList.FindEntityByClassname(pStartEntity, szName);
	}

	return pEntity;
}


//-----------------------------------------------------------------------------
// Purpose: Finds the first entity by target name or class name within a radius
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity name to search for. Treated as a target name first,
//				then as an entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityGenericWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	CBaseEntity* pEntity = NULL;

	pEntity = gEntList.FindEntityByNameWithin(pStartEntity, szName, vecSrc, flRadius, pSearchingEntity, pActivator, pCaller);
	if (!pEntity)
	{
		pEntity = gEntList.FindEntityByClassnameWithin(pStartEntity, szName, vecSrc, flRadius);
	}

	return pEntity;
}

//-----------------------------------------------------------------------------
// Purpose: Finds the nearest entity by target name or class name within a radius.
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity name to search for. Treated as a target name first,
//				then as an entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityGenericNearest(const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	CBaseEntity* pEntity = NULL;

	pEntity = gEntList.FindEntityByNameNearest(szName, vecSrc, flRadius, pSearchingEntity, pActivator, pCaller);
	if (!pEntity)
	{
		pEntity = gEntList.FindEntityByClassnameNearest(szName, vecSrc, flRadius);
	}

	return pEntity;
}


//-----------------------------------------------------------------------------
// Purpose: Find the nearest entity along the facing direction from the given origin
//			within the angular threshold (ignores worldspawn) with the
//			given classname.
// Input  : origin - 
//			facing - 
//			threshold - 
//			classname - 
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityClassNearestFacing(const Vector& origin, const Vector& facing, float threshold, char* classname)
{
	float bestDot = threshold;
	CBaseEntity* best_ent = NULL;

	const CEntInfo<T>* pInfo = BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		// FIXME: why is this skipping pointsize entities?
		if (ent->IsPointSized())
			continue;

		// Make vector to entity
		Vector	to_ent = (ent->GetAbsOrigin() - origin);

		VectorNormalize(to_ent);
		float dot = DotProduct(facing, to_ent);
		if (dot > bestDot)
		{
			if (FClassnameIs(ent, classname))
			{
				// Ignore if worldspawn
				if (!FClassnameIs(ent, "worldspawn") && !FClassnameIs(ent, "soundent"))
				{
					bestDot = dot;
					best_ent = ent;
				}
			}
		}
	}
	return best_ent;
}


//-----------------------------------------------------------------------------
// Purpose: Find the nearest entity along the facing direction from the given origin
//			within the angular threshold (ignores worldspawn)
// Input  : origin - 
//			facing - 
//			threshold - 
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityNearestFacing(const Vector& origin, const Vector& facing, float threshold)
{
	float bestDot = threshold;
	CBaseEntity* best_ent = NULL;

	const CEntInfo<T>* pInfo = BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		// Ignore logical entities
		if (!ent->edict())
			continue;

		// Make vector to entity
		Vector	to_ent = ent->WorldSpaceCenter() - origin;
		VectorNormalize(to_ent);

		float dot = DotProduct(facing, to_ent);
		if (dot <= bestDot)
			continue;

		// Ignore if worldspawn
		if (!FStrEq(STRING(ent->m_iClassname), "worldspawn") && !FStrEq(STRING(ent->m_iClassname), "soundent"))
		{
			bestDot = dot;
			best_ent = ent;
		}
	}
	return best_ent;
}

template<class T>
void CGlobalEntityList<T>::OnAddEntity(T* pEnt, CBaseHandle handle)
{
	int i = handle.GetEntryIndex();

	// record current list details
	m_iNumEnts++;
	if (i > m_iHighestEnt)
		m_iHighestEnt = i;

	// If it's a CBaseEntity, notify the listeners.
	CBaseEntity* pBaseEnt = (pEnt)->GetBaseEntity();
	if (pBaseEnt->edict())
		m_iNumEdicts++;

	// NOTE: Must be a CBaseEntity on server
	Assert(pBaseEnt);
	//DevMsg(2,"Created %s\n", pBaseEnt->GetClassname() );
	for (i = m_entityListeners.Count() - 1; i >= 0; i--)
	{
		m_entityListeners[i]->OnEntityCreated(pBaseEnt);
	}
}

template<class T>
void CGlobalEntityList<T>::OnRemoveEntity(T* pEnt, CBaseHandle handle)
{
#ifdef DEBUG
	if (!g_fInCleanupDelete)
	{
		int i;
		for (i = 0; i < g_DeleteList.Count(); i++)
		{
			if (g_DeleteList[i]->GetEntityHandle() == pEnt)
			{
				g_DeleteList.FastRemove(i);
				Msg("ERROR: Entity being destroyed but previously threaded on g_DeleteList\n");
				break;
			}
		}
	}
#endif

	CBaseEntity* pBaseEnt = (pEnt)->GetBaseEntity();
	if (pBaseEnt->edict())
		m_iNumEdicts--;

	m_iNumEnts--;
}

template<class T>
void CGlobalEntityList<T>::NotifyCreateEntity(CBaseEntity* pEnt)
{
	if (!pEnt)
		return;

	//DevMsg(2,"Deleted %s\n", pBaseEnt->GetClassname() );
	for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
	{
		m_entityListeners[i]->OnEntityCreated(pEnt);
	}
}

template<class T>
void CGlobalEntityList<T>::NotifySpawn(CBaseEntity* pEnt)
{
	if (!pEnt)
		return;

	//DevMsg(2,"Deleted %s\n", pBaseEnt->GetClassname() );
	for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
	{
		m_entityListeners[i]->OnEntitySpawned(pEnt);
	}
}

// NOTE: This doesn't happen in OnRemoveEntity() specifically because 
// listeners may want to reference the object as it's being deleted
// OnRemoveEntity isn't called until the destructor and all data is invalid.
template<class T>
void CGlobalEntityList<T>::NotifyRemoveEntity(CBaseHandle hEnt)
{
	CBaseEntity* pBaseEnt = GetBaseEntity(hEnt);
	if (!pBaseEnt)
		return;

	//DevMsg(2,"Deleted %s\n", pBaseEnt->GetClassname() );
	for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
	{
		m_entityListeners[i]->OnEntityDeleted(pBaseEnt);
	}
}


//-----------------------------------------------------------------------------
// Common finds
#if 0

template <class ENT_TYPE>
inline bool FindEntityByName( const char *pszName, ENT_TYPE **ppResult)
{
	CBaseEntity *pBaseEntity = gEntList.FindEntityByName( NULL, pszName );
	
	if ( pBaseEntity )
		*ppResult = dynamic_cast<ENT_TYPE *>( pBaseEntity );
	else
		*ppResult = NULL;

	return ( *ppResult != NULL );
}

template <>
inline bool FindEntityByName<CBaseEntity>( const char *pszName, CBaseEntity **ppResult)
{
	*ppResult = gEntList.FindEntityByName( NULL, pszName );
	return ( *ppResult != NULL );
}

template <>
inline bool FindEntityByName<CAI_BaseNPC>( const char *pszName, CAI_BaseNPC **ppResult)
{
	CBaseEntity *pBaseEntity = gEntList.FindEntityByName( NULL, pszName );
	
	if ( pBaseEntity )
		*ppResult = pBaseEntity->MyNPCPointer();
	else
		*ppResult = NULL;

	return ( *ppResult != NULL );
}
#endif
//-----------------------------------------------------------------------------
// Purpose: Simple object for storing a list of objects
//-----------------------------------------------------------------------------
struct entitem_t
{
	EHANDLE hEnt;
	struct entitem_t *pNext;

	// uses pool memory
	static void* operator new( size_t stAllocateBlock );
	static void *operator new( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine );
	static void operator delete( void *pMem );
	static void operator delete( void *pMem, int nBlockUse, const char *pFileName, int nLine ) { operator delete( pMem ); }
};

class CEntityList
{
public:
	CEntityList();
	~CEntityList();

	int m_iNumItems;
	entitem_t *m_pItemList;	// null terminated singly-linked list

	void AddEntity( CBaseEntity * );
	void DeleteEntity( CBaseEntity * );
};



struct notify_teleport_params_t
{
	Vector prevOrigin;
	QAngle prevAngles;
	bool physicsRotate;
};

struct notify_destroy_params_t
{
};

struct notify_system_event_params_t
{
	union
	{
		const notify_teleport_params_t *pTeleport;
		const notify_destroy_params_t *pDestroy;
	};
	notify_system_event_params_t( const notify_teleport_params_t *pInTeleport ) { pTeleport = pInTeleport; }
	notify_system_event_params_t( const notify_destroy_params_t *pInDestroy ) { pDestroy = pInDestroy; }
};


abstract_class INotify
{
public:
	// Add notification for an entity
	virtual void AddEntity( CBaseEntity *pNotify, CBaseEntity *pWatched ) = 0;

	// Remove notification for an entity
	virtual void RemoveEntity( CBaseEntity *pNotify, CBaseEntity *pWatched ) = 0;

	// Call the named input in each entity who is watching pEvent's status
	virtual void ReportNamedEvent( CBaseEntity *pEntity, const char *pEventName ) = 0;

	// System events don't make sense as inputs, so are handled through a generic notify function
	virtual void ReportSystemEvent( CBaseEntity *pEntity, notify_system_event_t eventType, const notify_system_event_params_t &params ) = 0;

	inline void ReportDestroyEvent( CBaseEntity *pEntity )
	{
		notify_destroy_params_t destroy;
		ReportSystemEvent( pEntity, NOTIFY_EVENT_DESTROY, notify_system_event_params_t(&destroy) );
	}
	
	inline void ReportTeleportEvent( CBaseEntity *pEntity, const Vector &prevOrigin, const QAngle &prevAngles, bool physicsRotate )
	{
		notify_teleport_params_t teleport;
		teleport.prevOrigin = prevOrigin;
		teleport.prevAngles = prevAngles;
		teleport.physicsRotate = physicsRotate;
		ReportSystemEvent( pEntity, NOTIFY_EVENT_TELEPORT, notify_system_event_params_t(&teleport) );
	}
	
	// Remove this entity from the notify list
	virtual void ClearEntity( CBaseEntity *pNotify ) = 0;
};



// singleton
extern INotify *g_pNotify;

void EntityTouch_Add( CBaseEntity *pEntity );
int AimTarget_ListCount();
int AimTarget_ListCopy( CBaseEntity *pList[], int listMax );
void AimTarget_ForceRepopulateList();

void SimThink_EntityChanged( CBaseEntity *pEntity );
int SimThink_ListCount();
int SimThink_ListCopy( CBaseEntity *pList[], int listMax );

#endif // ENTITYLIST_H
