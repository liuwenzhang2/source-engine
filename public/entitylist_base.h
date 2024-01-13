//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef ENTITYLIST_BASE_H
#define ENTITYLIST_BASE_H
#ifdef _WIN32
#pragma once
#endif


#include "const.h"
#include "basehandle.h"
#include "utllinkedlist.h"
#include "ihandleentity.h"


template<class T>
class CEntInfo
{
public:
	T*				m_pEntity;
	int				m_SerialNumber;
	CEntInfo<T>		*m_pPrev;
	CEntInfo<T>		*m_pNext;
	bool			m_bReserved = false;
	void			ClearLinks();
};

template<class T>
class CEntInfoList
{
public:
	CEntInfoList();

	const CEntInfo<T>* Head() const { return m_pHead; }
	const CEntInfo<T>* Tail() const { return m_pTail; }
	CEntInfo<T>* Head() { return m_pHead; }
	CEntInfo<T>* Tail() { return m_pTail; }
	void			AddToHead(CEntInfo<T>* pElement) { LinkAfter(NULL, pElement); }
	void			AddToTail(CEntInfo<T>* pElement) { LinkBefore(NULL, pElement); }

	void LinkBefore(CEntInfo<T>* pBefore, CEntInfo<T>* pElement);
	void LinkAfter(CEntInfo<T>* pBefore, CEntInfo<T>* pElement);
	void Unlink(CEntInfo<T>* pElement);
	bool IsInList(CEntInfo<T>* pElement);

private:
	CEntInfo<T>* m_pHead;
	CEntInfo<T>* m_pTail;
};

enum
{
	SERIAL_MASK = 0x7fff // the max value of a serial number, rolls back to 0 when it hits this limit
};

template<class T>
void CEntInfo<T>::ClearLinks()
{
	m_pPrev = m_pNext = this;
}

template<class T>
CEntInfoList<T>::CEntInfoList()
{
	m_pHead = NULL;
	m_pTail = NULL;
}

// NOTE: Cut from UtlFixedLinkedList<>, UNDONE: Find a way to share this code
template<class T>
void CEntInfoList<T>::LinkBefore(CEntInfo<T>* pBefore, CEntInfo<T>* pElement)
{
	Assert(pElement);

	// Unlink it if it's in the list at the moment
	Unlink(pElement);

	// The element *after* our newly linked one is the one we linked before.
	pElement->m_pNext = pBefore;

	if (pBefore == NULL)
	{
		// In this case, we're linking to the end of the list, so reset the tail
		pElement->m_pPrev = m_pTail;
		m_pTail = pElement;
	}
	else
	{
		// Here, we're not linking to the end. Set the prev pointer to point to
		// the element we're linking.
		Assert(IsInList(pBefore));
		pElement->m_pPrev = pBefore->m_pPrev;
		pBefore->m_pPrev = pElement;
	}

	// Reset the head if we linked to the head of the list
	if (pElement->m_pPrev == NULL)
	{
		m_pHead = pElement;
	}
	else
	{
		pElement->m_pPrev->m_pNext = pElement;
	}
}

template<class T>
void CEntInfoList<T>::LinkAfter(CEntInfo<T>* pAfter, CEntInfo<T>* pElement)
{
	Assert(pElement);

	// Unlink it if it's in the list at the moment
	if (IsInList(pElement))
		Unlink(pElement);

	// The element *before* our newly linked one is the one we linked after
	pElement->m_pPrev = pAfter;
	if (pAfter == NULL)
	{
		// In this case, we're linking to the head of the list, reset the head
		pElement->m_pNext = m_pHead;
		m_pHead = pElement;
	}
	else
	{
		// Here, we're not linking to the end. Set the next pointer to point to
		// the element we're linking.
		Assert(IsInList(pAfter));
		pElement->m_pNext = pAfter->m_pNext;
		pAfter->m_pNext = pElement;
	}

	// Reset the tail if we linked to the tail of the list
	if (pElement->m_pNext == NULL)
	{
		m_pTail = pElement;
	}
	else
	{
		pElement->m_pNext->m_pPrev = pElement;
	}
}

template<class T>
void CEntInfoList<T>::Unlink(CEntInfo<T>* pElement)
{
	if (IsInList(pElement))
	{
		// If we're the first guy, reset the head
		// otherwise, make our previous node's next pointer = our next
		if (pElement->m_pPrev)
		{
			pElement->m_pPrev->m_pNext = pElement->m_pNext;
		}
		else
		{
			m_pHead = pElement->m_pNext;
		}

		// If we're the last guy, reset the tail
		// otherwise, make our next node's prev pointer = our prev
		if (pElement->m_pNext)
		{
			pElement->m_pNext->m_pPrev = pElement->m_pPrev;
		}
		else
		{
			m_pTail = pElement->m_pPrev;
		}

		// This marks this node as not in the list, 
		// but not in the free list either
		pElement->ClearLinks();
	}
}

template<class T>
bool CEntInfoList<T>::IsInList(CEntInfo<T>* pElement)
{
	return pElement->m_pPrev != pElement;
}

// Implement this class and register with gEntList to receive entity create/delete notification
template< class T >
class IEntityListener
{
public:
	virtual void OnEntityCreated(T* pEntity) {};
	virtual void OnEntitySpawned(T* pEntity) {};
	virtual void OnEntityDeleted(T* pEntity) {};
};

template<class T>// = IHandleEntity
class CBaseEntityList
{
public:
	CBaseEntityList();
	~CBaseEntityList();
	
	void ReserveEdict(int index);
	int AllocateFreeEdict(int index = -1);
	// Add and remove entities. iForcedSerialNum should only be used on the client. The server
	// gets to dictate what the networkable serial numbers are on the client so it can send
	// ehandles over and they work.
	CBaseHandle AddNetworkableEntity( T *pEnt, int index, int iForcedSerialNum = -1 );
	CBaseHandle AddNonNetworkableEntity( T *pEnt );
	void RemoveEntity( CBaseHandle handle );

	// Get an ehandle from a networkable entity's index (note: if there is no entity in that slot,
	// then the ehandle will be invalid and produce NULL).
	CBaseHandle GetNetworkableHandle( int iEntity ) const;
	short		GetNetworkSerialNumber(int iEntity) const;
	// ehandles use this in their Get() function to produce a pointer to the entity.
	T* LookupEntity( const CBaseHandle &handle ) const;
	T* LookupEntityByNetworkIndex( int edictIndex ) const;

	// Use these to iterate over all the entities.
	CBaseHandle FirstHandle() const;
	CBaseHandle NextHandle( CBaseHandle hEnt ) const;
	static CBaseHandle InvalidHandle();

	const CEntInfo<T> *FirstEntInfo() const;
	const CEntInfo<T> *NextEntInfo( const CEntInfo<T>* pInfo ) const;
	const CEntInfo<T> *GetEntInfoPtr( const CBaseHandle &hEnt ) const;
	const CEntInfo<T> *GetEntInfoPtrByIndex( int index ) const;

	// add a class that gets notified of entity events
	void AddListenerEntity(IEntityListener<T>* pListener);
	void RemoveListenerEntity(IEntityListener<T>* pListener);
// Overridables.

	// entity is about to be removed, notify the listeners
	void NotifyCreateEntity(T* pEnt);
	void NotifySpawn(T* pEnt);
	void NotifyRemoveEntity(T* pEnt);
protected:

	// These are notifications to the derived class. It can cache info here if it wants.
	virtual void OnAddEntity( T *pEnt, CBaseHandle handle );
	
	// It is safe to delete the entity here. We won't be accessing the pointer after
	// calling OnRemoveEntity.
	virtual void OnRemoveEntity( T *pEnt, CBaseHandle handle );

	virtual void Clear(void);

	virtual bool IsReservedEdicts(int index);
private:
	CBaseHandle AddEntityAtSlot( T *pEnt, int iSlot, int iForcedSerialNum );
	void RemoveEntityAtSlot( int iSlot );
	int GetEntInfoIndex( const CEntInfo<T> *pEntInfo ) const;

	// The first MAX_EDICTS entities are networkable. The rest are client-only or server-only.
	CEntInfo<T> m_EntPtrArray[NUM_ENT_ENTRIES];
	CEntInfoList<T>	m_activeList;
	CEntInfoList<T>	m_freeNetworkableList;
	CEntInfoList<T>	m_ReservedNetworkableList;
	CEntInfoList<T>	m_freeNonNetworkableList;
	CUtlVector<IEntityListener<T>*>	m_entityListeners;
};

template<class T>
inline void CBaseEntityList<T>::ReserveEdict(int index) {
	if (m_activeList.Head()) {
		Error("already actived");
	}
	CEntInfo<T>* pSlot = &m_EntPtrArray[index];
	if (pSlot->m_bReserved) {
		Error("already reserved");
	}
	pSlot->m_bReserved = true;
	m_freeNetworkableList.Unlink(pSlot);
	m_ReservedNetworkableList.AddToTail(pSlot);
}

template<class T>
inline int CBaseEntityList<T>::AllocateFreeEdict(int index) {
	Assert(index >= 0 && index < MAX_EDICTS);
	CEntInfo<T>* pSlot = NULL;
	if (index == -1) {
		pSlot = m_freeNetworkableList.Head();
	}
	else {
		pSlot = &m_EntPtrArray[index];
	}
	if (!pSlot)
	{
		Error("no free edict");
	}
	if (pSlot->m_bReserved) {
		Error("has been reserved");
	}
	m_freeNetworkableList.Unlink(pSlot);
	int iSlot = GetEntInfoIndex(pSlot);
	return iSlot;
};

// ------------------------------------------------------------------------------------ //
// Inlines.
// ------------------------------------------------------------------------------------ //
template<class T>
inline int CBaseEntityList<T>::GetEntInfoIndex( const CEntInfo<T> *pEntInfo ) const
{
	Assert( pEntInfo );
	int index = (int)(pEntInfo - m_EntPtrArray);
	Assert( index >= 0 && index < NUM_ENT_ENTRIES );
	return index;
}

template<class T>
inline CBaseHandle CBaseEntityList<T>::GetNetworkableHandle( int iEntity ) const
{
	Assert( iEntity >= 0 && iEntity < MAX_EDICTS );
	if ( m_EntPtrArray[iEntity].m_pEntity )
		return CBaseHandle( iEntity, m_EntPtrArray[iEntity].m_SerialNumber );
	else
		return CBaseHandle();
}

template<class T>
inline short CBaseEntityList<T>::GetNetworkSerialNumber(int iEntity) const {
	Assert(iEntity >= 0 && iEntity < MAX_EDICTS);
	return m_EntPtrArray[iEntity].m_SerialNumber & (1 << NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS) - 1;
}

template<class T>
inline T* CBaseEntityList<T>::LookupEntity( const CBaseHandle &handle ) const
{
	if ( handle.m_Index == INVALID_EHANDLE_INDEX )
		return NULL;

	const CEntInfo<T> *pInfo = &m_EntPtrArray[ handle.GetEntryIndex() ];
	if ( pInfo->m_SerialNumber == handle.GetSerialNumber() )
		return pInfo->m_pEntity;
	else
		return NULL;
}

template<class T>
inline T* CBaseEntityList<T>::LookupEntityByNetworkIndex( int edictIndex ) const
{
	// (Legacy support).
	if ( edictIndex < 0 )
		return NULL;

	Assert( edictIndex < NUM_ENT_ENTRIES );
	return m_EntPtrArray[edictIndex].m_pEntity;
}

template<class T>
inline CBaseHandle CBaseEntityList<T>::FirstHandle() const
{
	if ( !m_activeList.Head() )
		return INVALID_EHANDLE_INDEX;

	int index = GetEntInfoIndex( m_activeList.Head() );
	return CBaseHandle( index, m_EntPtrArray[index].m_SerialNumber );
}

template<class T>
inline CBaseHandle CBaseEntityList<T>::NextHandle( CBaseHandle hEnt ) const
{
	int iSlot = hEnt.GetEntryIndex();
	CEntInfo<T> *pNext = m_EntPtrArray[iSlot].m_pNext;
	if ( !pNext )
		return INVALID_EHANDLE_INDEX;

	int index = GetEntInfoIndex( pNext );

	return CBaseHandle( index, m_EntPtrArray[index].m_SerialNumber );
}
	
template<class T>
inline CBaseHandle CBaseEntityList<T>::InvalidHandle()
{
	return INVALID_EHANDLE_INDEX;
}

template<class T>
inline const CEntInfo<T> *CBaseEntityList<T>::FirstEntInfo() const
{
	return m_activeList.Head();
}

template<class T>
inline const CEntInfo<T> *CBaseEntityList<T>::NextEntInfo( const CEntInfo<T> *pInfo ) const
{
	return pInfo->m_pNext;
}

template<class T>
inline const CEntInfo<T> *CBaseEntityList<T>::GetEntInfoPtr( const CBaseHandle &hEnt ) const
{
	int iSlot = hEnt.GetEntryIndex();
	return &m_EntPtrArray[iSlot];
}

template<class T>
inline const CEntInfo<T> *CBaseEntityList<T>::GetEntInfoPtrByIndex( int index ) const
{
	return &m_EntPtrArray[index];
}


template<class T>
CBaseEntityList<T>::CBaseEntityList()
{
	// These are not in any list (yet)
	int i;
	for (i = 0; i < NUM_ENT_ENTRIES; i++)
	{
		m_EntPtrArray[i].ClearLinks();
		m_EntPtrArray[i].m_SerialNumber = (rand() & SERIAL_MASK); // generate random starting serial number
		m_EntPtrArray[i].m_pEntity = NULL;
	}

	for (i = 0; i < MAX_EDICTS; i++) {
		CEntInfo<T>* pList = &m_EntPtrArray[i];
		m_freeNetworkableList.AddToTail(pList);
	}
	// make a free list of the non-networkable entities
	// Initially, all the slots are free.
	for (i = MAX_EDICTS + 1; i < NUM_ENT_ENTRIES; i++)
	{
		CEntInfo<T>* pList = &m_EntPtrArray[i];
		m_freeNonNetworkableList.AddToTail(pList);
	}
}

template<class T>
CBaseEntityList<T>::~CBaseEntityList()
{
	Clear();
}

template<class T>
CBaseHandle CBaseEntityList<T>::AddNetworkableEntity(T* pEnt, int index, int iForcedSerialNum)
{
	Assert(index >= 0 && index < MAX_EDICTS);
	CEntInfo<T>* pSlot = &m_EntPtrArray[index];
	if (pSlot->m_bReserved) {
		m_ReservedNetworkableList.Unlink(pSlot);
	}
	else {
		if(m_freeNetworkableList.IsInList(pSlot)) {
			m_freeNetworkableList.Unlink(pSlot);
		}
	}
	return AddEntityAtSlot(pEnt, index, iForcedSerialNum);
}

template<class T>
CBaseHandle CBaseEntityList<T>::AddNonNetworkableEntity(T* pEnt)
{
	// Find a slot for it.
	CEntInfo<T>* pSlot = m_freeNonNetworkableList.Head();
	if (!pSlot)
	{
		Warning("CBaseEntityList::AddNonNetworkableEntity: no free slots!\n");
		AssertMsg(0, ("CBaseEntityList::AddNonNetworkableEntity: no free slots!\n"));
		return CBaseHandle();
	}

	// Move from the free list into the allocated list.
	m_freeNonNetworkableList.Unlink(pSlot);
	int iSlot = GetEntInfoIndex(pSlot);

	return AddEntityAtSlot(pEnt, iSlot, -1);
}

template<class T>
void CBaseEntityList<T>::RemoveEntity(CBaseHandle handle)
{
	RemoveEntityAtSlot(handle.GetEntryIndex());
}

template<class T>
CBaseHandle CBaseEntityList<T>::AddEntityAtSlot(T* pEnt, int iSlot, int iForcedSerialNum)
{
	// Init the CSerialEntity.
	CEntInfo<T>* pSlot = &m_EntPtrArray[iSlot];
	Assert(pSlot->m_pEntity == NULL);
	if (pSlot->m_pEntity) {
		Error("pSlot->m_pEntity must be NULL");
	}
	pSlot->m_pEntity = pEnt;

	// Force the serial number (client-only)?
	if (iForcedSerialNum != -1)
	{
		pSlot->m_SerialNumber = iForcedSerialNum;

#if !defined( CLIENT_DLL )
		// Only the client should force the serial numbers.
		Assert(false);
#endif
	}

	// Update our list of active entities.
	m_activeList.AddToTail(pSlot);
	CBaseHandle retVal(iSlot, pSlot->m_SerialNumber);

	// Tell the entity to store its handle.
	pEnt->SetRefEHandle(retVal);

	// Notify any derived class.
	OnAddEntity(pEnt, retVal);
	return retVal;
}

template<class T>
void CBaseEntityList<T>::RemoveEntityAtSlot(int iSlot)
{
	Assert(iSlot >= 0 && iSlot < NUM_ENT_ENTRIES);

	CEntInfo<T>* pInfo = &m_EntPtrArray[iSlot];

	if (pInfo->m_pEntity)
	{
		pInfo->m_pEntity->SetRefEHandle(INVALID_EHANDLE_INDEX);

		// Notify the derived class that we're about to remove this entity.
		OnRemoveEntity(pInfo->m_pEntity, CBaseHandle(iSlot, pInfo->m_SerialNumber));

		// Increment the serial # so ehandles go invalid.
		pInfo->m_pEntity = NULL;
		pInfo->m_SerialNumber = (pInfo->m_SerialNumber + 1) & SERIAL_MASK;

		m_activeList.Unlink(pInfo);

		// Add the slot back to the free list if it's a non-networkable entity.
		if (iSlot >= MAX_EDICTS)
		{
			m_freeNonNetworkableList.AddToTail(pInfo);
		}
		else {
			if (pInfo->m_bReserved) {
				m_ReservedNetworkableList.AddToTail(pInfo);
			}
			else {
				m_freeNetworkableList.AddToTail(pInfo);
			}
		}
	}
}

// add a class that gets notified of entity events
template<class T>
void CBaseEntityList<T>::AddListenerEntity(IEntityListener<T>* pListener)
{
	if (m_entityListeners.Find(pListener) >= 0)
	{
		AssertMsg(0, "Can't add listeners multiple times\n");
		return;
	}
	m_entityListeners.AddToTail(pListener);
}

template<class T>
void CBaseEntityList<T>::RemoveListenerEntity(IEntityListener<T>* pListener)
{
	m_entityListeners.FindAndRemove(pListener);
}

template<class T>
void CBaseEntityList<T>::NotifyCreateEntity(T* pEnt)
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
void CBaseEntityList<T>::NotifySpawn(T* pEnt)
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
void CBaseEntityList<T>::NotifyRemoveEntity(T* pEnt)
{
	if (!pEnt)
		return;

	//DevMsg(2,"Deleted %s\n", pBaseEnt->GetClassname() );
	for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
	{
		m_entityListeners[i]->OnEntityDeleted(pEnt);
	}
}

template<class T>
void CBaseEntityList<T>::OnAddEntity(T* pEnt, CBaseHandle handle)
{
	// NOTE: Must be a CBaseEntity on server
	Assert(pEnt);
	//DevMsg(2,"Created %s\n", pBaseEnt->GetClassname() );
	for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
	{
		m_entityListeners[i]->OnEntityCreated(pEnt);
	}
}


template<class T>
void CBaseEntityList<T>::OnRemoveEntity(T* pEnt, CBaseHandle handle)
{
}

template<class T>
bool CBaseEntityList<T>::IsReservedEdicts(int index) {
	CEntInfo<T>* pSlot = &m_EntPtrArray[index];
	return pSlot->m_bReserved;
}

template<class T>
void CBaseEntityList<T>::Clear(void) {
	CEntInfo<T>* pList = m_activeList.Head();

	while (pList)
	{
		CEntInfo<T>* pNext = pList->m_pNext;
		RemoveEntityAtSlot(GetEntInfoIndex(pList));
		pList = pNext;
	}

	pList = m_ReservedNetworkableList.Head();

	while (pList)
	{
		CEntInfo<T>* pNext = pList->m_pNext;
		pList->m_bReserved = false;
		m_ReservedNetworkableList.Unlink(pList);
		m_freeNetworkableList.AddToTail(pList);
		pList = pNext;
	}
}

#ifdef CLIENT_DLL
extern CBaseEntityList<C_BaseEntity>* g_pEntityList;
#endif // CLIENT_DLL
#ifdef GAME_DLL
extern CBaseEntityList<CBaseEntity>* g_pEntityList;
#endif // GAME_DLL

class IEntityFactory;

// This is the glue that hooks .MAP entity class names to our CPP classes
abstract_class IEntityFactoryDictionary
{
public:
	virtual void InstallFactory(IEntityFactory * pFactory) = 0;
	virtual IHandleEntity* Create(const char* pClassName , int iForceEdictIndex, int iSerialNum) = 0;
	virtual void Destroy(const char* pClassName, IHandleEntity* pEntity) = 0;
	virtual IEntityFactory* FindFactory(const char* pClassName) = 0;
	virtual const char* GetMapClassName(const char* pClassName) = 0;
	virtual const char* GetDllClassName(const char* pClassName) = 0;
	virtual size_t		GetEntitySize(const char* pClassName) = 0;
	virtual const char* GetCannonicalName(const char* pClassName) = 0;
	virtual void ReportEntitySizes() = 0;
};

IEntityFactoryDictionary* EntityFactoryDictionary();

inline bool CanCreateEntityClass(const char* pszClassname)
{
	return (EntityFactoryDictionary() != NULL && EntityFactoryDictionary()->FindFactory(pszClassname) != NULL);
}

#ifdef CLIENT_DLL
inline C_BaseEntity* CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1)
{
	return (C_BaseEntity*)EntityFactoryDictionary()->Create(className, iForceEdictIndex, iSerialNum);
}
#endif // CLIENT_DLL
#ifdef GAME_DLL
inline CBaseEntity* CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1)
{
	return (CBaseEntity*)EntityFactoryDictionary()->Create(className, iForceEdictIndex, iSerialNum);
}
#endif // GAME_DLL

inline const char* GetEntityMapClassName(const char* className)
{
	return EntityFactoryDictionary()->GetMapClassName(className);
}

inline const char* GetEntityDllClassName(const char* className)
{
	return EntityFactoryDictionary()->GetDllClassName(className);
}

inline size_t GetEntitySize(const char* className)
{
	return EntityFactoryDictionary()->GetEntitySize(className);
}


abstract_class IEntityFactory
{
public:
	virtual IHandleEntity * Create(int iForceEdictIndex,int iSerialNum) = 0;//const char* pClassName, 
	virtual void Destroy(IHandleEntity* pEntity) = 0;
	virtual const char* GetMapClassName() = 0;
	virtual const char* GetDllClassName() = 0;
	virtual size_t GetEntitySize() = 0;
};

#include "tier0/memdbgon.h"

// entity creation
// creates an entity that has not been linked to a classname
template< class T >
T* _CreateEntityTemplate(T* newEnt, const char* className, int iForceEdictIndex, int iSerialNum)
{
	newEnt = new T; // this is the only place 'new' should be used!
#ifdef CLIENT_DLL
	newEnt->Init(iForceEdictIndex, iSerialNum);
#endif
#ifdef GAME_DLL
	newEnt->PostConstructor(className, iForceEdictIndex);
#endif // GAME_DLL
	return newEnt;
}

#define CREATE_UNSAVED_ENTITY( newClass, className ) _CreateEntityTemplate( (newClass*)NULL, className, -1, -1 )

#include "tier0/memdbgoff.h"

template <class T>
class CEntityFactory : public IEntityFactory
{
	class CEntityProxy : public T {


	};

public:
	CEntityFactory(const char* pMapClassName, const char* pDllClassName)
	{
		if (!pDllClassName || !pDllClassName[0]) {
			Error("pDllClassName can not be null");
		}
		m_pMapClassName = pMapClassName;
		m_pDllClassName = pDllClassName;
		EntityFactoryDictionary()->InstallFactory(this);
	}

	IHandleEntity* Create(int iForceEdictIndex, int iSerialNum)
	{
		T* pEnt = _CreateEntityTemplate((CEntityProxy*)NULL, m_pMapClassName, iForceEdictIndex, iSerialNum);
		return pEnt;
	}

	void Destroy(IHandleEntity* pEntity)
	{
		if (pEntity)
		{
			delete ((T*)pEntity);//Release();
		}
	}

	const char* GetMapClassName() {
		return m_pMapClassName;
	}

	const char* GetDllClassName() {
		return m_pDllClassName;
	}

	virtual size_t GetEntitySize()
	{
		return sizeof(T);
	}
private:
	const char* m_pMapClassName;
	const char* m_pDllClassName;
};

#define LINK_ENTITY_TO_CLASS( mapClassName, DLLClassName )\
	static CEntityFactory<DLLClassName> mapClassName( #mapClassName, #DLLClassName );

#endif // ENTITYLIST_BASE_H
