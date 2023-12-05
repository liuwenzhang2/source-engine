//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//
#if !defined( CLIENTENTITYLIST_H )
#define CLIENTENTITYLIST_H
#ifdef _WIN32
#pragma once
#endif

#include "tier0/dbg.h"
#include "icliententitylist.h"
#include "iclientunknown.h"
#include "utllinkedlist.h"
#include "utlvector.h"
#include "icliententityinternal.h"
#include "ispatialpartition.h"
#include "cdll_util.h"
#include "entitylist_base.h"
#include "utlmap.h"
#include "c_baseentity.h"

//class C_Beam;
//class C_BaseViewModel;
//class C_BaseEntity;


#define INPVS_YES			0x0001		// The entity thinks it's in the PVS.
#define INPVS_THISFRAME		0x0002		// Accumulated as different views are rendered during the frame and used to notify the entity if
										// it is not in the PVS anymore (at the end of the frame).
#define INPVS_NEEDSNOTIFY	0x0004		// The entity thinks it's in the PVS.
							   
class IClientEntityListener;

abstract_class C_BaseEntityClassList
{
public:
	C_BaseEntityClassList();
	~C_BaseEntityClassList();
	virtual void LevelShutdown() = 0;

	C_BaseEntityClassList *m_pNextClassList;
};

template< class T >
class C_EntityClassList : public C_BaseEntityClassList
{
public:
	virtual void LevelShutdown()  { m_pClassList = NULL; }

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


// Maximum size of entity list
#define INVALID_CLIENTENTITY_HANDLE CBaseHandle( INVALID_EHANDLE_INDEX )

class CPVSNotifyInfo
{
public:
	IPVSNotify* m_pNotify;
	IClientRenderable* m_pRenderable;
	unsigned char m_InPVSStatus;				// Combination of the INPVS_ flags.
	unsigned short m_PVSNotifiersLink;			// Into m_PVSNotifyInfos.
};
//
// This is the IClientEntityList implemenation. It serves two functions:
//
// 1. It converts server entity indices into IClientNetworkables for the engine.
//
// 2. It provides a place to store IClientUnknowns and gives out ClientEntityHandle_t's
//    so they can be indexed and retreived. For example, this is how static props are referenced
//    by the spatial partition manager - it doesn't know what is being inserted, so it's 
//	  given ClientEntityHandle_t's, and the handlers for spatial partition callbacks can
//    use the client entity list to look them up and check for supported interfaces.
//
template<class T>// = IHandleEntity
class CClientEntityList : public CBaseEntityList<T>, public IClientEntityList
{
friend class C_BaseEntityIterator;
friend class C_AllBaseEntityIterator;
typedef CBaseEntityList<T> BaseClass;
public:
	// Constructor, destructor
								CClientEntityList( void );
	virtual 					~CClientEntityList( void );

	void						Release();		// clears everything and releases entities


// Implement IClientEntityList
public:

	virtual IClientNetworkable*	GetClientNetworkable( int entnum );
	virtual IClientEntity*		GetClientEntity( int entnum );

	virtual int					NumberOfEntities( bool bIncludeNonNetworkable = false );

	virtual IClientUnknown*		GetClientUnknownFromHandle( ClientEntityHandle_t hEnt );
	virtual IClientNetworkable*	GetClientNetworkableFromHandle( ClientEntityHandle_t hEnt );
	virtual IClientEntity*		GetClientEntityFromHandle( ClientEntityHandle_t hEnt );

	virtual int					GetHighestEntityIndex( void );

	virtual void				SetMaxEntities( int maxents );
	virtual int					GetMaxEntities( );


// CBaseEntityList overrides.
protected:

	virtual void OnAddEntity( T *pEnt, CBaseHandle handle );
	virtual void OnRemoveEntity( T *pEnt, CBaseHandle handle );


// Internal to client DLL.
public:

	// All methods of accessing specialized IClientUnknown's go through here.
	IClientUnknown*			GetListedEntity( int entnum );
	
	// Simple wrappers for convenience..
	C_BaseEntity*			GetBaseEntity( int entnum );
	ICollideable*			GetCollideable( int entnum );

	IClientRenderable*		GetClientRenderableFromHandle( ClientEntityHandle_t hEnt );
	C_BaseEntity*			GetBaseEntityFromHandle( ClientEntityHandle_t hEnt );
	ICollideable*			GetCollideableFromHandle( ClientEntityHandle_t hEnt );
	IClientThinkable*		GetClientThinkableFromHandle( ClientEntityHandle_t hEnt );

	// Convenience methods to convert between entindex + ClientEntityHandle_t
	ClientEntityHandle_t	EntIndexToHandle( int entnum );
	int						HandleToEntIndex( ClientEntityHandle_t handle );

	// Is a handle valid?
	bool					IsHandleValid( ClientEntityHandle_t handle ) const;

	// For backwards compatibility...
	C_BaseEntity*			GetEnt( int entnum ) { return GetBaseEntity( entnum ); }

	void					RecomputeHighestEntityUsed( void );


	// Use this to iterate over all the C_BaseEntities.
	C_BaseEntity* FirstBaseEntity() const;
	C_BaseEntity* NextBaseEntity( C_BaseEntity *pEnt ) const;

	

	// Get the list of all PVS notifiers.
	CUtlLinkedList<CPVSNotifyInfo,unsigned short>& GetPVSNotifiers();

	CUtlVector<IClientEntityListener *>	m_entityListeners;

	// add a class that gets notified of entity events
	void AddListenerEntity( IClientEntityListener *pListener );
	void RemoveListenerEntity( IClientEntityListener *pListener );

	void NotifyCreateEntity( C_BaseEntity *pEnt );
	void NotifyRemoveEntity( C_BaseEntity *pEnt );

private:

	// Cached info for networked entities.
	struct EntityCacheInfo_t
	{
		// Cached off because GetClientNetworkable is called a *lot*
		IClientNetworkable *m_pNetworkable;
		unsigned short m_BaseEntitiesIndex;	// Index into m_BaseEntities (or m_BaseEntities.InvalidIndex() if none).
	};

	// Current count
	int					m_iNumServerEnts;
	// Max allowed
	int					m_iMaxServerEnts;

	int					m_iNumClientNonNetworkable;

	// Current last used slot
	int					m_iMaxUsedServerIndex;

	// This holds fast lookups for special edicts.
	EntityCacheInfo_t	m_EntityCacheInfo[NUM_ENT_ENTRIES];

	// For fast iteration.
	CUtlLinkedList<C_BaseEntity*, unsigned short> m_BaseEntities;


private:

	void AddPVSNotifier( IClientUnknown *pUnknown );
	void RemovePVSNotifier( IClientUnknown *pUnknown );
	
	// These entities want to know when they enter and leave the PVS (server entities
	// already can get the equivalent notification with NotifyShouldTransmit, but client
	// entities have to get it this way).
	CUtlLinkedList<CPVSNotifyInfo,unsigned short> m_PVSNotifyInfos;
	CUtlMap<IClientUnknown*,unsigned short,unsigned short> m_PVSNotifierMap;	// Maps IClientUnknowns to indices into m_PVSNotifyInfos.
};


// Use this to iterate over *all* (even dormant) the C_BaseEntities in the client entity list.
class C_AllBaseEntityIterator
{
public:
	C_AllBaseEntityIterator();

	void Restart();
	C_BaseEntity* Next();	// keep calling this until it returns null.

private:
	unsigned short m_CurBaseEntity;
};

class C_BaseEntityIterator
{
public:
	C_BaseEntityIterator();

	void Restart();
	C_BaseEntity* Next();	// keep calling this until it returns null.

private:
	unsigned short m_CurBaseEntity;
};

//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
template<class T>
inline bool	CClientEntityList<T>::IsHandleValid( ClientEntityHandle_t handle ) const
{
	return handle.Get() != 0;
}

template<class T>
inline IClientUnknown* CClientEntityList<T>::GetListedEntity( int entnum )
{
	return (IClientUnknown*)BaseClass::LookupEntityByNetworkIndex( entnum );
}

template<class T>
inline IClientUnknown* CClientEntityList<T>::GetClientUnknownFromHandle( ClientEntityHandle_t hEnt )
{
	return (IClientUnknown*)BaseClass::LookupEntity( hEnt );
}

template<class T>
inline CUtlLinkedList<CPVSNotifyInfo,unsigned short>& CClientEntityList<T>::GetPVSNotifiers()//CClientEntityList<T>::
{
	return m_PVSNotifyInfos;
}


//-----------------------------------------------------------------------------
// Convenience methods to convert between entindex + ClientEntityHandle_t
//-----------------------------------------------------------------------------
template<class T>
inline ClientEntityHandle_t CClientEntityList<T>::EntIndexToHandle( int entnum )
{
	if ( entnum < -1 )
		return INVALID_EHANDLE_INDEX;
	IClientUnknown *pUnk = GetListedEntity( entnum );
	return pUnk ? pUnk->GetRefEHandle() : INVALID_EHANDLE_INDEX; 
}

bool PVSNotifierMap_LessFunc(IClientUnknown* const& a, IClientUnknown* const& b);

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
template<class T>
CClientEntityList<T>::CClientEntityList(void) :
	m_PVSNotifierMap(0, 0, PVSNotifierMap_LessFunc)
{
	m_iMaxUsedServerIndex = -1;
	m_iMaxServerEnts = 0;
	Release();

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
template<class T>
CClientEntityList<T>::~CClientEntityList(void)
{
	Release();
}

//-----------------------------------------------------------------------------
// Purpose: Clears all entity lists and releases entities
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::Release(void)
{
	// Free all the entities.
	ClientEntityHandle_t iter = BaseClass::FirstHandle();
	while (iter != BaseClass::InvalidHandle())
	{
		// Try to call release on anything we can.
		IClientNetworkable* pNet = GetClientNetworkableFromHandle(iter);
		if (pNet)
		{
			pNet->Release();
		}
		else
		{
			// Try to call release on anything we can.
			IClientThinkable* pThinkable = GetClientThinkableFromHandle(iter);
			if (pThinkable)
			{
				pThinkable->Release();
			}
		}
		BaseClass::RemoveEntity(iter);

		iter = BaseClass::FirstHandle();
	}

	m_iNumServerEnts = 0;
	m_iMaxServerEnts = 0;
	m_iNumClientNonNetworkable = 0;
	m_iMaxUsedServerIndex = -1;
}

template<class T>
IClientNetworkable* CClientEntityList<T>::GetClientNetworkable(int entnum)
{
	Assert(entnum >= 0);
	Assert(entnum < MAX_EDICTS);
	return m_EntityCacheInfo[entnum].m_pNetworkable;
}

template<class T>
IClientEntity* CClientEntityList<T>::GetClientEntity(int entnum)
{
	IClientUnknown* pEnt = GetListedEntity(entnum);
	return pEnt ? pEnt->GetIClientEntity() : 0;
}

template<class T>
int CClientEntityList<T>::NumberOfEntities(bool bIncludeNonNetworkable)
{
	if (bIncludeNonNetworkable == true)
		return m_iNumServerEnts + m_iNumClientNonNetworkable;

	return m_iNumServerEnts;
}

template<class T>
void CClientEntityList<T>::SetMaxEntities(int maxents)
{
	m_iMaxServerEnts = maxents;
}

template<class T>
int CClientEntityList<T>::GetMaxEntities(void)
{
	return m_iMaxServerEnts;
}


//-----------------------------------------------------------------------------
// Convenience methods to convert between entindex + ClientEntityHandle_t
//-----------------------------------------------------------------------------
template<class T>
int CClientEntityList<T>::HandleToEntIndex(ClientEntityHandle_t handle)
{
	if (handle == INVALID_EHANDLE_INDEX)
		return -1;
	C_BaseEntity* pEnt = GetBaseEntityFromHandle(handle);
	return pEnt ? pEnt->entindex() : -1;
}


//-----------------------------------------------------------------------------
// Purpose: Because m_iNumServerEnts != last index
// Output : int
//-----------------------------------------------------------------------------
template<class T>
int CClientEntityList<T>::GetHighestEntityIndex(void)
{
	return m_iMaxUsedServerIndex;
}

template<class T>
void CClientEntityList<T>::RecomputeHighestEntityUsed(void)
{
	m_iMaxUsedServerIndex = -1;

	// Walk backward looking for first valid index
	int i;
	for (i = MAX_EDICTS - 1; i >= 0; i--)
	{
		if (GetListedEntity(i) != NULL)
		{
			m_iMaxUsedServerIndex = i;
			break;
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Add a raw C_BaseEntity to the entity list.
// Input  : index - 
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : index - 
//-----------------------------------------------------------------------------
template<class T>
C_BaseEntity* CClientEntityList<T>::GetBaseEntity(int entnum)
{
	IClientUnknown* pEnt = GetListedEntity(entnum);
	return pEnt ? pEnt->GetBaseEntity() : 0;
}

template<class T>
ICollideable* CClientEntityList<T>::GetCollideable(int entnum)
{
	IClientUnknown* pEnt = GetListedEntity(entnum);
	return pEnt ? pEnt->GetCollideable() : 0;
}

template<class T>
IClientNetworkable* CClientEntityList<T>::GetClientNetworkableFromHandle(ClientEntityHandle_t hEnt)
{
	IClientUnknown* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetClientNetworkable() : 0;
}

template<class T>
IClientEntity* CClientEntityList<T>::GetClientEntityFromHandle(ClientEntityHandle_t hEnt)
{
	IClientUnknown* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetIClientEntity() : 0;
}

template<class T>
IClientRenderable* CClientEntityList<T>::GetClientRenderableFromHandle(ClientEntityHandle_t hEnt)
{
	IClientUnknown* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetClientRenderable() : 0;
}

template<class T>
C_BaseEntity* CClientEntityList<T>::GetBaseEntityFromHandle(ClientEntityHandle_t hEnt)
{
	IClientUnknown* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetBaseEntity() : 0;
}

template<class T>
ICollideable* CClientEntityList<T>::GetCollideableFromHandle(ClientEntityHandle_t hEnt)
{
	IClientUnknown* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetCollideable() : 0;
}

template<class T>
IClientThinkable* CClientEntityList<T>::GetClientThinkableFromHandle(ClientEntityHandle_t hEnt)
{
	IClientUnknown* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetClientThinkable() : 0;
}

template<class T>
void CClientEntityList<T>::AddPVSNotifier(IClientUnknown* pUnknown)
{
	IClientRenderable* pRen = pUnknown->GetClientRenderable();
	if (pRen)
	{
		IPVSNotify* pNotify = pRen->GetPVSNotifyInterface();
		if (pNotify)
		{
			unsigned short index = m_PVSNotifyInfos.AddToTail();
			CPVSNotifyInfo* pInfo = &m_PVSNotifyInfos[index];
			pInfo->m_pNotify = pNotify;
			pInfo->m_pRenderable = pRen;
			pInfo->m_InPVSStatus = 0;
			pInfo->m_PVSNotifiersLink = index;

			m_PVSNotifierMap.Insert(pUnknown, index);
		}
	}
}

template<class T>
void CClientEntityList<T>::RemovePVSNotifier(IClientUnknown* pUnknown)
{
	IClientRenderable* pRenderable = pUnknown->GetClientRenderable();
	if (pRenderable)
	{
		IPVSNotify* pNotify = pRenderable->GetPVSNotifyInterface();
		if (pNotify)
		{
			unsigned short index = m_PVSNotifierMap.Find(pUnknown);
			if (!m_PVSNotifierMap.IsValidIndex(index))
			{
				Warning("PVS notifier not in m_PVSNotifierMap\n");
				Assert(false);
				return;
			}

			unsigned short indexIntoPVSNotifyInfos = m_PVSNotifierMap[index];

			Assert(m_PVSNotifyInfos[indexIntoPVSNotifyInfos].m_pNotify == pNotify);
			Assert(m_PVSNotifyInfos[indexIntoPVSNotifyInfos].m_pRenderable == pRenderable);

			m_PVSNotifyInfos.Remove(indexIntoPVSNotifyInfos);
			m_PVSNotifierMap.RemoveAt(index);
			return;
		}
	}

	// If it didn't report itself as a notifier, let's hope it's not in the notifier list now
	// (which would mean that it reported itself as a notifier earlier, but not now).
#ifdef _DEBUG
	unsigned short index = m_PVSNotifierMap.Find(pUnknown);
	Assert(!m_PVSNotifierMap.IsValidIndex(index));
#endif
}

template<class T>
void CClientEntityList<T>::AddListenerEntity(IClientEntityListener* pListener)
{
	if (m_entityListeners.Find(pListener) >= 0)
	{
		AssertMsg(0, "Can't add listeners multiple times\n");
		return;
	}
	m_entityListeners.AddToTail(pListener);
}

template<class T>
void CClientEntityList<T>::RemoveListenerEntity(IClientEntityListener* pListener)
{
	m_entityListeners.FindAndRemove(pListener);
}

template<class T>
void CClientEntityList<T>::OnAddEntity(T* pEnt, CBaseHandle handle)
{
	int entnum = handle.GetEntryIndex();
	EntityCacheInfo_t* pCache = &m_EntityCacheInfo[entnum];

	if (entnum >= 0 && entnum < MAX_EDICTS)
	{
		// Update our counters.
		m_iNumServerEnts++;
		if (entnum > m_iMaxUsedServerIndex)
		{
			m_iMaxUsedServerIndex = entnum;
		}


		// Cache its networkable pointer.
		Assert(dynamic_cast<IClientUnknown*>(pEnt));
		Assert(((IClientUnknown*)pEnt)->GetClientNetworkable()); // Server entities should all be networkable.
		pCache->m_pNetworkable = ((IClientUnknown*)pEnt)->GetClientNetworkable();
	}

	IClientUnknown* pUnknown = (IClientUnknown*)pEnt;

	// If this thing wants PVS notifications, hook it up.
	AddPVSNotifier(pUnknown);

	// Store it in a special list for fast iteration if it's a C_BaseEntity.
	C_BaseEntity* pBaseEntity = pUnknown->GetBaseEntity();
	if (pBaseEntity)
	{
		pCache->m_BaseEntitiesIndex = m_BaseEntities.AddToTail(pBaseEntity);

		if (pBaseEntity->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE)
		{
			m_iNumClientNonNetworkable++;
		}

		//DevMsg(2,"Created %s\n", pBaseEnt->GetClassname() );
		for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
		{
			m_entityListeners[i]->OnEntityCreated(pBaseEntity);
		}
	}
	else
	{
		pCache->m_BaseEntitiesIndex = m_BaseEntities.InvalidIndex();
	}


}

template<class T>
void CClientEntityList<T>::OnRemoveEntity(T* pEnt, CBaseHandle handle)
{
	int entnum = handle.GetEntryIndex();
	EntityCacheInfo_t* pCache = &m_EntityCacheInfo[entnum];

	if (entnum >= 0 && entnum < MAX_EDICTS)
	{
		// This is a networkable ent. Clear out our cache info for it.
		pCache->m_pNetworkable = NULL;
		m_iNumServerEnts--;

		if (entnum >= m_iMaxUsedServerIndex)
		{
			RecomputeHighestEntityUsed();
		}
	}


	IClientUnknown* pUnknown = (IClientUnknown*)pEnt;

	// If this is a PVS notifier, remove it.
	RemovePVSNotifier(pUnknown);

	C_BaseEntity* pBaseEntity = pUnknown->GetBaseEntity();

	if (pBaseEntity)
	{
		if (pBaseEntity->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE)
		{
			m_iNumClientNonNetworkable--;
		}

		//DevMsg(2,"Deleted %s\n", pBaseEnt->GetClassname() );
		for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
		{
			m_entityListeners[i]->OnEntityDeleted(pBaseEntity);
		}
	}

	if (pCache->m_BaseEntitiesIndex != m_BaseEntities.InvalidIndex())
		m_BaseEntities.Remove(pCache->m_BaseEntitiesIndex);

	pCache->m_BaseEntitiesIndex = m_BaseEntities.InvalidIndex();
}


// Use this to iterate over all the C_BaseEntities.
template<class T>
C_BaseEntity* CClientEntityList<T>::FirstBaseEntity() const
{
	const CEntInfo<T>* pList = BaseClass::FirstEntInfo();
	while (pList)
	{
		if (pList->m_pEntity)
		{
			IClientUnknown* pUnk = static_cast<IClientUnknown*>(pList->m_pEntity);
			C_BaseEntity* pRet = pUnk->GetBaseEntity();
			if (pRet)
				return pRet;
		}
		pList = pList->m_pNext;
	}

	return NULL;

}

template<class T>
C_BaseEntity* CClientEntityList<T>::NextBaseEntity(C_BaseEntity* pEnt) const
{
	if (pEnt == NULL)
		return FirstBaseEntity();

	// Run through the list until we get a C_BaseEntity.
	const CEntInfo<T>* pList = BaseClass::GetEntInfoPtr(pEnt->GetRefEHandle());
	if (pList)
	{
		pList = NextEntInfo(pList);
	}

	while (pList)
	{
		if (pList->m_pEntity)
		{
			IClientUnknown* pUnk = static_cast<IClientUnknown*>(pList->m_pEntity);
			C_BaseEntity* pRet = pUnk->GetBaseEntity();
			if (pRet)
				return pRet;
		}
		pList = pList->m_pNext;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Returns the client entity list
//-----------------------------------------------------------------------------
//template<class T>
extern CClientEntityList<C_BaseEntity> *cl_entitylist;

inline CClientEntityList<C_BaseEntity>& ClientEntityList()
{
	return *cl_entitylist;
}

// Implement this class and register with entlist to receive entity create/delete notification
class IClientEntityListener
{
public:
	virtual void OnEntityCreated( C_BaseEntity *pEntity ) {};
	//virtual void OnEntitySpawned( C_BaseEntity *pEntity ) {};
	virtual void OnEntityDeleted( C_BaseEntity *pEntity ) {};
};


#endif // CLIENTENTITYLIST_H

