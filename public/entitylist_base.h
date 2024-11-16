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

#ifdef CLIENT_DLL
class C_BaseEntity;
#endif // CLIENT_DLL
#ifdef GAME_DLL
class CBaseEntity;
#endif // GAME_DLL


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

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
template <class T>
abstract_class IEntityDataInstantiator
{
public:
	virtual ~IEntityDataInstantiator() {};

	virtual void* GetDataObject(const T* instance) = 0;
	virtual void* CreateDataObject(const T* instance) = 0;
	virtual void DestroyDataObject(const T* instance) = 0;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
template <class T, class V>
class CEntityDataInstantiator : public IEntityDataInstantiator<T>
{
public:
	CEntityDataInstantiator() :
		m_HashTable(64, 0, 0, CompareFunc, KeyFunc)
	{
	}

	virtual void* GetDataObject(const T* instance)
	{
		UtlHashHandle_t handle;
		HashEntry entry;
		entry.key = instance;
		handle = m_HashTable.Find(entry);

		if (handle != m_HashTable.InvalidHandle())
		{
			return (void*)m_HashTable[handle].data;
		}

		return NULL;
	}

	virtual void* CreateDataObject(const T* instance)
	{
		UtlHashHandle_t handle;
		HashEntry entry;
		entry.key = instance;
		handle = m_HashTable.Find(entry);

		// Create it if not already present
		if (handle == m_HashTable.InvalidHandle())
		{
			handle = m_HashTable.Insert(entry);
			Assert(handle != m_HashTable.InvalidHandle());
			m_HashTable[handle].data = AllocDataObject(instance);

			// FIXME: We'll have to remove this if any objects we instance have vtables!!!
			Q_memset(m_HashTable[handle].data, 0, sizeof(V));
		}

		return (void*)m_HashTable[handle].data;
	}

	virtual void DestroyDataObject(const T* instance)
	{
		UtlHashHandle_t handle;
		HashEntry entry;
		entry.key = instance;
		handle = m_HashTable.Find(entry);

		if (handle != m_HashTable.InvalidHandle())
		{
			FreeDataObject(instance, m_HashTable[handle].data);
			m_HashTable.Remove(handle);
		}
	}

protected:

	virtual V* AllocDataObject(const T* instance) {
		return new V;
	}

	virtual void FreeDataObject(const T* instance, V* pDataObject) {
		delete pDataObject;
	}

private:

	struct HashEntry
	{
		HashEntry()
		{
			key = NULL;
			data = NULL;
		}

		const T* key;
		V* data;
	};

	static bool CompareFunc(const HashEntry& src1, const HashEntry& src2)
	{
		return (src1.key == src2.key);
	}


	static unsigned int KeyFunc(const HashEntry& src)
	{
		// Shift right to get rid of alignment bits and border the struct on a 16 byte boundary
		return (unsigned int)(uintp)src.key;
	}

	CUtlHash< HashEntry >	m_HashTable;
};

//-----------------------------------------------------------------------------
// Purpose: System for hanging objects off of CBaseEntity, etc.
//  Externalized data objects ( see sharreddefs.h for enum )
//-----------------------------------------------------------------------------
template<class T>
class CDataObjectAccessSystem
{
public:

	enum
	{
		MAX_ACCESSORS = 32,
	};

	CDataObjectAccessSystem()
	{
		// Cast to int to make it clear that we know we are comparing different enum types.
		COMPILE_TIME_ASSERT((int)NUM_DATAOBJECT_TYPES <= (int)MAX_ACCESSORS);

		Q_memset(m_Accessors, 0, sizeof(m_Accessors));
	}

	~CDataObjectAccessSystem()
	{
		for (int i = 0; i < MAX_ACCESSORS; i++)
		{
			delete m_Accessors[i];
			m_Accessors[i] = 0;
		}
	}

	//virtual bool Init()
	//{
	//	AddDataAccessor(TOUCHLINK, new CEntityDataInstantiator< touchlink_t >);
	//	AddDataAccessor(GROUNDLINK, new CEntityDataInstantiator< groundlink_t >);
	//	AddDataAccessor(STEPSIMULATION, new CEntityDataInstantiator< StepSimulationData >);
	//	AddDataAccessor(MODELSCALE, new CEntityDataInstantiator< ModelScale >);
	//	AddDataAccessor(POSITIONWATCHER, new CEntityDataInstantiator< CWatcherList >);
	//	AddDataAccessor(PHYSICSPUSHLIST, new CEntityDataInstantiator< physicspushlist_t >);
	//	AddDataAccessor(VPHYSICSUPDATEAI, new CEntityDataInstantiator< vphysicsupdateai_t >);
	//	AddDataAccessor(VPHYSICSWATCHER, new CEntityDataInstantiator< CWatcherList >);

	//	return true;
	//}

	void* GetDataObject(int type, const T* instance)
	{
		if (!IsValidType(type))
		{
			Assert(!"Bogus type");
			return NULL;
		}
		return m_Accessors[type]->GetDataObject(instance);
	}

	void* CreateDataObject(int type, T* instance)
	{
		if (!IsValidType(type))
		{
			Assert(!"Bogus type");
			return NULL;
		}

		return m_Accessors[type]->CreateDataObject(instance);
	}

	void DestroyDataObject(int type, T* instance)
	{
		if (!IsValidType(type))
		{
			Assert(!"Bogus type");
			return;
		}

		m_Accessors[type]->DestroyDataObject(instance);
	}

	void AddDataAccessor(int type, IEntityDataInstantiator<T>* instantiator)
	{
		if (type < 0 || type >= MAX_ACCESSORS)
		{
			Assert(!"AddDataAccessor with out of range type!!!\n");
			return;
		}

		Assert(instantiator);

		if (m_Accessors[type] != NULL)
		{
			Assert(!"AddDataAccessor, duplicate adds!!!\n");
			return;
		}

		m_Accessors[type] = instantiator;
	}

	void RemoveDataAccessor(int type) {
		if (type < 0 || type >= MAX_ACCESSORS)
		{
			Assert(!"AddDataAccessor with out of range type!!!\n");
			return;
		}

		Assert(m_Accessors[type]);

		if (m_Accessors[type] == NULL)
		{
			Assert(!"AddDataAccessor, duplicate remove!!!\n");
			return;
		}

		delete m_Accessors[type];
		m_Accessors[type] = NULL;
	}

private:

	bool IsValidType(int type) const
	{
		if (type < 0 || type >= MAX_ACCESSORS)
			return false;

		if (m_Accessors[type] == NULL)
			return false;
		return true;
	}



	IEntityDataInstantiator<T>* m_Accessors[MAX_ACCESSORS];
};

template<class T>// = IHandleEntity
class CBaseEntityList
{
public:
	CBaseEntityList();
	~CBaseEntityList();
	
	
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
	void ReserveSlot(int index);
	bool IsReservedSlot(int index);
	int AllocateFreeSlot(bool bNetworkable = true, int index = -1);
	// Add and remove entities. iForcedSerialNum should only be used on the client. The server
	// gets to dictate what the networkable serial numbers are on the client so it can send
	// ehandles over and they work.
	//CBaseHandle AddNetworkableEntity( T *pEnt, int index, int iForcedSerialNum = -1 );
	//CBaseHandle AddNonNetworkableEntity( T *pEnt );
	void AddEntity(T* pEnt);
	void RemoveEntity(T* pEnt);

	// These are notifications to the derived class. It can cache info here if it wants.
	virtual void OnAddEntity( T *pEnt, CBaseHandle handle );
	
	// It is safe to delete the entity here. We won't be accessing the pointer after
	// calling OnRemoveEntity.
	virtual void OnRemoveEntity( T *pEnt, CBaseHandle handle );

	virtual void Clear(void);

	void AddDataAccessor(int type, IEntityDataInstantiator<T>* instantiator);
	void RemoveDataAccessor(int type);
	void* GetDataObject(int type, const T* instance);
	void* CreateDataObject(int type, T* instance);
	void DestroyDataObject(int type, T* instance);

private:
	//CBaseHandle AddEntityAtSlot( T *pEnt, int iSlot, int iForcedSerialNum );
	//void RemoveEntityAtSlot( int iSlot );
	int GetEntInfoIndex( const CEntInfo<T> *pEntInfo ) const;

	// The first MAX_EDICTS entities are networkable. The rest are client-only or server-only.
	CEntInfo<T> m_EntPtrArray[NUM_ENT_ENTRIES];
	CEntInfoList<T>	m_activeList;
	CEntInfoList<T>	m_freeNetworkableList;
	CEntInfoList<T>	m_ReservedNetworkableList;
	CEntInfoList<T>	m_freeNonNetworkableList;
	CUtlVector<IEntityListener<T>*>	m_entityListeners;
	CDataObjectAccessSystem<T> m_DataObjectAccessSystem;
};

template<class T>
inline void CBaseEntityList<T>::ReserveSlot(int index) {
	Assert(index >= 0 && index < MAX_EDICTS);
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
bool CBaseEntityList<T>::IsReservedSlot(int index) {
	CEntInfo<T>* pSlot = &m_EntPtrArray[index];
	return pSlot->m_bReserved;
}

template<class T>
inline int CBaseEntityList<T>::AllocateFreeSlot(bool bNetworkable, int index) {
	if (index != -1) {
		if (bNetworkable) {
			if (index >= 0 && index < MAX_EDICTS) {

			}
			else {
				Error("error index\n");
			}
		}
		else {
			if (index >= MAX_EDICTS + 1 && index < NUM_ENT_ENTRIES) {

			}
			else {
				Error("error index\n");
			}
		}
	}
	CEntInfo<T>* pSlot = NULL;
	if (index == -1) {
		if (bNetworkable) {
			pSlot = m_freeNetworkableList.Head();
		}
		else {
			pSlot = m_freeNonNetworkableList.Head();
		}
	}
	else {
		pSlot = &m_EntPtrArray[index];
	}
	if (!pSlot)
	{
		Error("no free slot");
	}
	if (pSlot->m_pEntity) {
		Error("pSlot->m_pEntity must be NULL");
	}
	if (pSlot->m_bReserved) {
		m_ReservedNetworkableList.Unlink(pSlot);
	}
	else {
		if (bNetworkable) {
			m_freeNetworkableList.Unlink(pSlot);
		}
		else {
			m_freeNonNetworkableList.Unlink(pSlot);
		}
	}
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

//template<class T>
//CBaseHandle CBaseEntityList<T>::AddNetworkableEntity(T* pEnt, int index, int iForcedSerialNum)
//{
//	Assert(index >= 0 && index < MAX_EDICTS);
//	if (pEnt->GetEntityFactory() == NULL) {
//		Error("EntityFactory can not be NULL!");
//	}
//	CEntInfo<T>* pSlot = &m_EntPtrArray[index];
//	if (pSlot->m_bReserved) {
//		m_ReservedNetworkableList.Unlink(pSlot);
//	}
//	else {
//		if(m_freeNetworkableList.IsInList(pSlot)) {
//			m_freeNetworkableList.Unlink(pSlot);
//		}
//	}
//	return AddEntityAtSlot(pEnt, index, iForcedSerialNum);
//}

//template<class T>
//CBaseHandle CBaseEntityList<T>::AddNonNetworkableEntity(T* pEnt)
//{
//	if (pEnt->GetEntityFactory() == NULL) {
//		Error("EntityFactory can not be NULL!");
//	}
//	// Find a slot for it.
//	CEntInfo<T>* pSlot = m_freeNonNetworkableList.Head();
//	if (!pSlot)
//	{
//		Warning("CBaseEntityList::AddNonNetworkableEntity: no free slots!\n");
//		AssertMsg(0, ("CBaseEntityList::AddNonNetworkableEntity: no free slots!\n"));
//		return CBaseHandle();
//	}
//
//	// Move from the free list into the allocated list.
//	m_freeNonNetworkableList.Unlink(pSlot);
//	int iSlot = GetEntInfoIndex(pSlot);
//
//	return AddEntityAtSlot(pEnt, iSlot, -1);
//}

template<class T>
void CBaseEntityList<T>::AddEntity(T* pEnt)
{
	int iSlot = ((IHandleEntity*)pEnt)->GetRefEHandle().GetEntryIndex();
	int iSerialNum = ((IHandleEntity*)pEnt)->GetRefEHandle().GetSerialNumber();
	// Init the CSerialEntity.
	CEntInfo<T>* pSlot = &m_EntPtrArray[iSlot];
	Assert(pSlot->m_pEntity == NULL);
	if (pSlot->m_pEntity) {
		Error("pSlot->m_pEntity must be NULL");
	}
	pSlot->m_pEntity = pEnt;

	// Force the serial number (client-only)?
	if (iSerialNum != pSlot->m_SerialNumber)
	{
		pSlot->m_SerialNumber = iSerialNum;
	}

	// Update our list of active entities.
	m_activeList.AddToTail(pSlot);
	CBaseHandle retVal(iSlot, pSlot->m_SerialNumber);

	// Tell the entity to store its handle.
	//pEnt->SetRefEHandle(retVal);

	// Notify any derived class.
	OnAddEntity(pEnt, retVal);
}

template<class T>
void CBaseEntityList<T>::RemoveEntity(T* pEnt)
{
	int iSlot = ((IHandleEntity*)pEnt)->GetRefEHandle().GetEntryIndex();
	Assert(iSlot >= 0 && iSlot < NUM_ENT_ENTRIES);

	CEntInfo<T>* pInfo = &m_EntPtrArray[iSlot];

	if (pEnt != pInfo->m_pEntity) {
		Error("pSlot->m_pEntity must not be NULL");
	}
	
	//pInfo->m_pEntity->SetRefEHandle(INVALID_EHANDLE_INDEX);

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

//template<class T>
//CBaseHandle CBaseEntityList<T>::AddEntityAtSlot(T* pEnt, int iSlot, int iForcedSerialNum)
//{
//	// Init the CSerialEntity.
//	CEntInfo<T>* pSlot = &m_EntPtrArray[iSlot];
//	Assert(pSlot->m_pEntity == NULL);
//	if (pSlot->m_pEntity) {
//		Error("pSlot->m_pEntity must be NULL");
//	}
//	pSlot->m_pEntity = pEnt;
//
//	// Force the serial number (client-only)?
//	if (iForcedSerialNum != -1)
//	{
//		pSlot->m_SerialNumber = iForcedSerialNum;
//
//#if !defined( CLIENT_DLL )
//		// Only the client should force the serial numbers.
//		Assert(false);
//#endif
//	}
//
//	// Update our list of active entities.
//	m_activeList.AddToTail(pSlot);
//	CBaseHandle retVal(iSlot, pSlot->m_SerialNumber);
//
//	// Tell the entity to store its handle.
//	pEnt->SetRefEHandle(retVal);
//
//	// Notify any derived class.
//	OnAddEntity(pEnt, retVal);
//	return retVal;
//}

//template<class T>
//void CBaseEntityList<T>::RemoveEntityAtSlot(int iSlot)
//{
//	Assert(iSlot >= 0 && iSlot < NUM_ENT_ENTRIES);
//
//	CEntInfo<T>* pInfo = &m_EntPtrArray[iSlot];
//
//	if (pInfo->m_pEntity)
//	{
//		pInfo->m_pEntity->SetRefEHandle(INVALID_EHANDLE_INDEX);
//
//		// Notify the derived class that we're about to remove this entity.
//		OnRemoveEntity(pInfo->m_pEntity, CBaseHandle(iSlot, pInfo->m_SerialNumber));
//
//		// Increment the serial # so ehandles go invalid.
//		pInfo->m_pEntity = NULL;
//		pInfo->m_SerialNumber = (pInfo->m_SerialNumber + 1) & SERIAL_MASK;
//
//		m_activeList.Unlink(pInfo);
//
//		// Add the slot back to the free list if it's a non-networkable entity.
//		if (iSlot >= MAX_EDICTS)
//		{
//			m_freeNonNetworkableList.AddToTail(pInfo);
//		}
//		else {
//			if (pInfo->m_bReserved) {
//				m_ReservedNetworkableList.AddToTail(pInfo);
//			}
//			else {
//				m_freeNetworkableList.AddToTail(pInfo);
//			}
//		}
//	}
//}

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
void CBaseEntityList<T>::Clear(void) {
	CEntInfo<T>* pList = m_activeList.Head();
	if (pList) {
		Error("entity must been cleared by sub class");
	}
	//while (pList)
	//{
	//	CEntInfo<T>* pNext = pList->m_pNext;
	//	RemoveEntityAtSlot(GetEntInfoIndex(pList));
	//	pList = pNext;
	//}

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

template<class T>
void CBaseEntityList<T>::AddDataAccessor(int type, IEntityDataInstantiator<T>* instantiator) {
	m_DataObjectAccessSystem.AddDataAccessor(type, instantiator);
}

template<class T>
void CBaseEntityList<T>::RemoveDataAccessor(int type) {
	m_DataObjectAccessSystem.RemoveDataAccessor(type);
}

template<class T>
void* CBaseEntityList<T>::GetDataObject(int type, const T* instance) {
	return m_DataObjectAccessSystem.GetDataObject(type, instance);
}

template<class T>
void* CBaseEntityList<T>::CreateDataObject(int type, T* instance) {
	return m_DataObjectAccessSystem.CreateDataObject(type, instance);
}

template<class T>
void CBaseEntityList<T>::DestroyDataObject(int type, T* instance) {
	m_DataObjectAccessSystem.DestroyDataObject(type, instance);
}

#ifdef CLIENT_DLL
extern CBaseEntityList<C_BaseEntity>* g_pEntityList;
#endif // CLIENT_DLL
#ifdef GAME_DLL
extern CBaseEntityList<CBaseEntity>* g_pEntityList;
#endif // GAME_DLL

inline bool CanCreateEntityClass(const char* pszClassname)
{
	return (EntityFactoryDictionary() != NULL && EntityFactoryDictionary()->FindFactory(pszClassname) != NULL);
}

//#ifdef CLIENT_DLL
//inline C_BaseEntity* CreateEntityByName2(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1)
//{
//	return (C_BaseEntity*)EntityFactoryDictionary()->Create(className, iForceEdictIndex, iSerialNum);
//}
//#endif // CLIENT_DLL
//#ifdef GAME_DLL
//inline CBaseEntity* CreateEntityByName2(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1)
//{
//	return (CBaseEntity*)EntityFactoryDictionary()->Create(className, iForceEdictIndex, iSerialNum);
//}
//#endif // GAME_DLL

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

inline void DestroyEntity(IHandleEntity* pEntity) {
	EntityFactoryDictionary()->Destroy(pEntity);
}


#include "tier0/memdbgon.h"

// entity creation
// creates an entity that has not been linked to a classname
//template< class T >
//T* _CreateEntityTemplate(T* newEnt, const char* className, int iForceEdictIndex, int iSerialNum)
//{
//	newEnt = new T; // this is the only place 'new' should be used!
//#ifdef CLIENT_DLL
//	newEnt->Init(iForceEdictIndex, iSerialNum);
//#endif
//#ifdef GAME_DLL
//	newEnt->PostConstructor(className, iForceEdictIndex);
//#endif // GAME_DLL
//	return newEnt;
//}

//#define CREATE_UNSAVED_ENTITY( newClass, className ) _CreateEntityTemplate( (newClass*)NULL, className, -1, -1 )

#include "tier0/memdbgoff.h"

template <class T>
class CEntityFactory : public IEntityFactory
{
	class CEntityProxy : public T {

		CEntityProxy(CEntityFactory<T>* pEntityFactory, IEntityList* pEntityList, int iForceEdictIndex, int iSerialNum, IEntityCallBack* pCallBack)
		:m_pEntityFactory(pEntityFactory), m_pEntityList(pEntityList), m_RefEHandle(iForceEdictIndex, iSerialNum), m_pCallBack(pCallBack)
		{

		}
		~CEntityProxy() {
			if (!m_pEntityFactory->IsInDestruction(this)) {
				Error("Must be destroy by IEntityFactory!");
			}
		}
		IEntityFactory* GetEntityFactory() { 
			return m_pEntityFactory; 
		}

		IEntityList* GetEntityList() { 
			return m_pEntityList;
		}

		const CBaseHandle& GetRefEHandle() const {
			return m_RefEHandle;
		}

		void UpdateOnRemove() {
			if (bUpdateOnRemoved) {
				Error("recursive UpdateOnRemove hit");
			}
			T::UpdateOnRemove();
			bUpdateOnRemoved = true;
		}

	private:
		CEntityFactory<T>* const m_pEntityFactory;
		bool m_bInDestruction = false;
		IEntityList* const m_pEntityList;
		const CBaseHandle m_RefEHandle;
		IEntityCallBack* const m_pCallBack;
		bool bUpdateOnRemoved = false;
		template <class U>
		friend class CEntityFactory;
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

	IHandleEntity* Create(IEntityList* pEntityList, int iForceEdictIndex, int iSerialNum, IEntityCallBack* pCallBack)
	{
		CEntityProxy* newEnt = new CEntityProxy(this, pEntityList, iForceEdictIndex, iSerialNum, pCallBack); // this is the only place 'new' should be used!
		//CBaseHandle refHandle(iForceEdictIndex, iSerialNum);
		//newEnt->SetRefEHandle(refHandle);
		if (newEnt->GetEntityList()) {
			int aaa = 0;
		}
		newEnt->m_pCallBack->AfterCreated(newEnt);
#ifdef CLIENT_DLL
		((IHandleEntity*)newEnt)->Init(iForceEdictIndex, iSerialNum);
		((IHandleEntity*)newEnt)->AfterInit();
#endif
#ifdef GAME_DLL
		((CBaseEntity*)newEnt)->PostConstructor(m_pMapClassName, iForceEdictIndex);
#endif // GAME_DLL
		return newEnt;
	}

	void Destroy(IHandleEntity* pEntity)
	{
		CEntityProxy* pEntityProxy = (CEntityProxy*)pEntity;
		if (!pEntityProxy)
		{
			Error("not created by IEntityFactory!");
		}
#ifdef CLIENT_DLL
		((C_BaseEntity*)pEntityProxy)->Remove();
#endif // CLIENT_DLL
		if (!pEntityProxy->bUpdateOnRemoved) {
			pEntityProxy->UpdateOnRemove();
		}
		pEntityProxy->m_pCallBack->BeforeDestroy(pEntity);
		pEntityProxy->m_bInDestruction = true;
		delete pEntityProxy;
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

	virtual int RequiredEdictIndex() {
#ifdef GAME_DLL
		return T::RequiredEdictIndexStatic();
#else
		return -1;
#endif // GAME_DLL
	}

	virtual bool IsNetworkable() {
#ifdef GAME_DLL
		return T::IsNetworkableStatic();
#else
		return true;
#endif // GAME_DLL
	}

	virtual int GetEngineObjectType() {
		return T::GetEngineObjectTypeStatic();
	}

private:

	bool IsInDestruction(CEntityProxy* entityProxy) const{
		return entityProxy->m_bInDestruction;
	}
	const char* m_pMapClassName;
	const char* m_pDllClassName;

	friend class CEntityProxy;
};

#define LINK_ENTITY_TO_CLASS( mapClassName, DLLClassName )\
	static CEntityFactory<DLLClassName> mapClassName( #mapClassName, #DLLClassName );

struct StaticPropPolyhedronGroups_t //each static prop is made up of a group of polyhedrons, these help us pull those groups from an array
{
	int iStartIndex;
	int iNumPolyhedrons;
};

struct PortalTransformAsAngledPosition_t //a matrix transformation from this portal to the linked portal, stored as vector and angle transforms
{
	Vector ptOriginTransform;
	QAngle qAngleTransform;
};

inline bool LessFunc_Integer(const int& a, const int& b) { return a < b; };


//class CPortalSimulatorEventCallbacks //sends out notifications of events to game specific code
//{
//public:
//	virtual void PortalSimulator_TookOwnershipOfEntity( CBaseEntity *pEntity ) { };
//	virtual void PortalSimulator_ReleasedOwnershipOfEntity( CBaseEntity *pEntity ) { };
//
//	virtual void PortalSimulator_TookPhysicsOwnershipOfEntity( CBaseEntity *pEntity ) { };
//	virtual void PortalSimulator_ReleasedPhysicsOwnershipOfEntity( CBaseEntity *pEntity ) { };
//};

//====================================================================================
// To any coder trying to understand the following nested structures....
//
// You may be wondering... why? wtf?
//
// The answer. The previous incarnation of server side portal simulation suffered
// terribly from evolving variables with increasingly cryptic names with no clear
// definition of what part of the system the variable was involved with.
//
// It's my hope that a nested structure with clear boundaries will eliminate that 
// horrible, awful, nasty, frustrating confusion. (It was really really bad). This
// system has the added benefit of pseudo-forcing a naming structure.
//
// Lastly, if it all roots in one struct, we can const reference it out to allow 
// easy reads without writes
//
// It's broken out like this to solve a few problems....
// 1. It cleans up intellisense when you don't actually define a structure
//		within a structure.
// 2. Shorter typenames when you want to have a pointer/reference deep within
//		the nested structure.
// 3. Needed at least one level removed from CPortalSimulator so
//		pointers/references could be made while the primary instance of the
//		data was private/protected.
//
// It may be slightly difficult to understand in it's broken out structure, but
// intellisense brings all the data together in a very cohesive manner for
// working with.
//====================================================================================

struct PS_PlacementData_t //stuff useful for geometric operations
{
	//Vector ptCenter;
	//QAngle qAngles;
	Vector vForward;
	Vector vUp;
	Vector vRight;
	cplane_t PortalPlane;
	VMatrix matThisToLinked;
	VMatrix matLinkedToThis;
	PortalTransformAsAngledPosition_t ptaap_ThisToLinked;
	PortalTransformAsAngledPosition_t ptaap_LinkedToThis;
	CPhysCollide* pHoleShapeCollideable; //used to test if a collideable is in the hole, should NOT be collided against in general
	PS_PlacementData_t(void)
	{
		memset(this, 0, sizeof(PS_PlacementData_t));
		matThisToLinked.Identity();
		matLinkedToThis.Identity();
	}
};

struct PS_SD_Static_World_Brushes_t
{
	CUtlVector<CPolyhedron*> Polyhedrons; //the building blocks of more complex collision
	CPhysCollide* pCollideable;
	//#ifndef CLIENT_DLL
	IPhysicsObject* pPhysicsObject;
	PS_SD_Static_World_Brushes_t() : pCollideable(NULL), pPhysicsObject(NULL) {};
	//#else
	//	PS_SD_Static_World_Brushes_t() : pCollideable(NULL) {};
	//#endif

};


struct PS_SD_Static_World_StaticProps_ClippedProp_t
{
	StaticPropPolyhedronGroups_t	PolyhedronGroup;
	CPhysCollide* pCollide;
	//#ifndef CLIENT_DLL
	IPhysicsObject* pPhysicsObject;
	//#endif
	IHandleEntity* pSourceProp;

	int								iTraceContents;
	short							iTraceSurfaceProps;
	static CBaseEntity* pTraceEntity;
	static const char* szTraceSurfaceName; //same for all static props, here just for easy reference
	static const int				iTraceSurfaceFlags; //same for all static props, here just for easy reference
};

struct PS_SD_Static_World_StaticProps_t
{
	CUtlVector<CPolyhedron*> Polyhedrons; //the building blocks of more complex collision
	CUtlVector<PS_SD_Static_World_StaticProps_ClippedProp_t> ClippedRepresentations;
	bool bCollisionExists; //the shortcut to know if collideables exist for each prop
	bool bPhysicsExists; //the shortcut to know if physics obects exist for each prop
	PS_SD_Static_World_StaticProps_t(void) : bCollisionExists(false), bPhysicsExists(false) { };
};

struct PS_SD_Static_World_t //stuff in front of the portal
{
	PS_SD_Static_World_Brushes_t Brushes;
	PS_SD_Static_World_StaticProps_t StaticProps;
};

struct PS_SD_Static_Wall_Local_Tube_t //a minimal tube, an object must fit inside this to be eligible for portaling
{
	CUtlVector<CPolyhedron*> Polyhedrons; //the building blocks of more complex collision
	CPhysCollide* pCollideable;

	//#ifndef CLIENT_DLL
	IPhysicsObject* pPhysicsObject;
	PS_SD_Static_Wall_Local_Tube_t() : pCollideable(NULL), pPhysicsObject(NULL) {};
	//#else
	//	PS_SD_Static_Wall_Local_Tube_t() : pCollideable(NULL) {};
	//#endif
};

struct PS_SD_Static_Wall_Local_Brushes_t
{
	CUtlVector<CPolyhedron*> Polyhedrons; //the building blocks of more complex collision
	CPhysCollide* pCollideable;

	//#ifndef CLIENT_DLL
	IPhysicsObject* pPhysicsObject;
	PS_SD_Static_Wall_Local_Brushes_t() : pCollideable(NULL), pPhysicsObject(NULL) {};
	//#else
	//	PS_SD_Static_Wall_Local_Brushes_t() : pCollideable(NULL) {};
	//#endif
};

struct PS_SD_Static_Wall_Local_t //things in the wall that are completely independant of having a linked portal
{
	PS_SD_Static_Wall_Local_Tube_t Tube;
	PS_SD_Static_Wall_Local_Brushes_t Brushes;
};

struct PS_SD_Static_Wall_RemoteTransformedToLocal_Brushes_t
{
	IPhysicsObject* pPhysicsObject;
	PS_SD_Static_Wall_RemoteTransformedToLocal_Brushes_t() : pPhysicsObject(NULL) {};
};

struct PS_SD_Static_Wall_RemoteTransformedToLocal_StaticProps_t
{
	CUtlVector<IPhysicsObject*> PhysicsObjects;
};

struct PS_SD_Static_Wall_RemoteTransformedToLocal_t //things taken from the linked portal's "World" collision and transformed into local space
{
	PS_SD_Static_Wall_RemoteTransformedToLocal_Brushes_t Brushes;
	PS_SD_Static_Wall_RemoteTransformedToLocal_StaticProps_t StaticProps;
};

struct PS_SD_Static_Wall_t //stuff behind the portal
{
	PS_SD_Static_Wall_Local_t Local;
	//#ifndef CLIENT_DLL
	PS_SD_Static_Wall_RemoteTransformedToLocal_t RemoteTransformedToLocal;
	//#endif
};

struct PS_SD_Static_SurfaceProperties_t //surface properties to pretend every collideable here is using
{
	int contents;
	csurface_t surface;
	CBaseEntity* pEntity;
};

struct PS_SD_Static_t //stuff that doesn't move around
{
	PS_SD_Static_World_t World;
	PS_SD_Static_Wall_t Wall;
	PS_SD_Static_SurfaceProperties_t SurfaceProperties;
};



//struct PS_SD_Dynamic_t //stuff that moves around
//{
//	PS_SD_Dynamic_t()
//	{
//	}
//};

struct PS_SimulationData_t //compartmentalized data for coherent management
{
	PS_SD_Static_t Static;

	//#ifndef CLIENT_DLL
		//PS_SD_Dynamic_t Dynamic;


		//PS_SimulationData_t() : pPhysicsEnvironment(NULL) {};// , pCollisionEntity(NULL) {};
	//#endif
};

struct PS_InternalData_t
{
	PS_PlacementData_t Placement;
	PS_SimulationData_t Simulation;
};

#endif // ENTITYLIST_BASE_H
