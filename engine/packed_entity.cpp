//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#ifdef OSX
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include <string.h>
#include <assert.h>
#include "packed_entity.h"
#include "basetypes.h"
#include "changeframelist.h"
#include "dt_send.h"
#include "dt_send_eng.h"
#include "server_class.h"
#include "framesnapshot.h";
#include "server_pch.h"
#include "sys_dll.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar sv_creationtickcheck("sv_creationtickcheck", "1", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Do extended check for encoding of timestamps against tickcount");
extern	CGlobalVars g_ServerGlobalVariables;
// -------------------------------------------------------------------------------------------------- //
// PackedEntity.
// -------------------------------------------------------------------------------------------------- //

PackedEntity::PackedEntity()
{
	m_pData = NULL;
	m_pChangeFrameList = NULL;
	m_nSnapshotCreationTick = 0;
	m_nShouldCheckCreationTick = 0;
}

PackedEntity::~PackedEntity()
{
	FreeData();

	if ( m_pChangeFrameList )
	{
		m_pChangeFrameList->Release();
		m_pChangeFrameList = NULL;
	}
}


bool PackedEntity::AllocAndCopyPadded( const void *pData, unsigned long size )
{
	FreeData();
	
	unsigned long nBytes = PAD_NUMBER( size, 4 );

	// allocate the memory
	m_pData = malloc( nBytes );

	if ( !m_pData )
	{
		Assert( m_pData );
		return false;
	}
	
	Q_memcpy( m_pData, pData, size );
	SetNumBits( nBytes * 8 );
	
	return true;
}


int PackedEntity::GetPropsChangedAfterTick( int iTick, int *iOutProps, int nMaxOutProps )
{
	if ( m_pChangeFrameList )
	{
		return m_pChangeFrameList->GetPropsChangedAfterTick( iTick, iOutProps, nMaxOutProps );
	}
	else
	{
		// signal that we don't have a changelist
		return -1;
	}
}


const CSendProxyRecipients*	PackedEntity::GetRecipients() const
{
	return m_Recipients.Base();
}


int PackedEntity::GetNumRecipients() const
{
	return m_Recipients.Count();
}


void PackedEntity::SetRecipients( const CUtlMemory<CSendProxyRecipients> &recipients )
{
	m_Recipients.CopyArray( recipients.Base(), recipients.Count() );
}


bool PackedEntity::CompareRecipients( const CUtlMemory<CSendProxyRecipients> &recipients )
{
	if ( recipients.Count() != m_Recipients.Count() )
		return false;
	
	return memcmp( recipients.Base(), m_Recipients.Base(), sizeof( CSendProxyRecipients ) * m_Recipients.Count() ) == 0;
}	

void PackedEntity::SetServerAndClientClass( ServerClass *pServerClass, ClientClass *pClientClass )
{
	m_pServerClass = pServerClass;
	m_pClientClass = pClientClass;
	if ( pServerClass )
	{
		Assert( pServerClass->m_pTable );
		SetShouldCheckCreationTick( pServerClass->m_pTable->HasPropsEncodedAgainstTickCount() );
	}
}

static PackedEntityManager s_PackedEntityManager;
PackedEntityManager* g_pPackedEntityManager = &s_PackedEntityManager;

PackedEntityManager::PackedEntityManager() : m_PackedEntitiesPool(MAX_EDICTS / 16, CUtlMemoryPool::GROW_SLOW)
{
	Q_memset(m_pPackedData, 0x00, MAX_EDICTS * sizeof(PackedEntity*));
}

PackedEntityManager::~PackedEntityManager() {
	// TODO: This assert has been failing. HenryG says it's a valid assert and that we're probably leaking memory.
	AssertMsg1(m_PackedEntitiesPool.Count() == 0 || IsInErrorExit(), "Expected m_PackedEntitiesPool to be empty. It had %i items.", m_PackedEntitiesPool.Count());
}

void PackedEntityManager::OnLevelChanged() {
	m_PackedEntityCache.RemoveAll();
	Q_memset(m_pPackedData, 0x00, MAX_EDICTS * sizeof(PackedEntity*));
}

void PackedEntityManager::OnCreateSnapshot(CFrameSnapshot* pSnapshot) {
	if (m_pEntities.Count() < pSnapshot->m_ListIndex + 1) {
		m_pEntities.EnsureCapacity(pSnapshot->m_ListIndex + 1);
	}
	m_pEntities.Element(pSnapshot->m_ListIndex) = new CFrameSnapshotEntry[pSnapshot->m_nNumEntities];

	CFrameSnapshotEntry* entry = m_pEntities.Element(pSnapshot->m_ListIndex);

	// clear entries
	for (int i = 0; i < pSnapshot->m_nNumEntities; i++)
	{
		entry->m_pClass = NULL;
		entry->m_nSerialNumber = -1;
		entry->m_pPackedData = INVALID_PACKED_ENTITY_HANDLE;
		entry++;
	}
}

void PackedEntityManager::OnDeleteSnapshot(CFrameSnapshot* pSnapshot) {
	// Decrement reference counts of all packed entities
	for (int i = 0; i < pSnapshot->m_nNumEntities; ++i)
	{
		if (m_pEntities.Element(pSnapshot->m_ListIndex)[i].m_pPackedData != NULL)
		{
			RemoveEntityReference(m_pEntities.Element(pSnapshot->m_ListIndex)[i].m_pPackedData);
		}
	}
	delete[] m_pEntities.Element(pSnapshot->m_ListIndex);
	m_pEntities.Remove(pSnapshot->m_ListIndex);
}

void PackedEntityManager::RemoveEntityReference(PackedEntity* pPackedEntity)
{
	Assert(pPackedEntity != INVALID_PACKED_ENTITY_HANDLE);

	//PackedEntity *packedEntity =  handle ;

	if (--pPackedEntity->m_ReferenceCount <= 0)
	{
		AUTO_LOCK(m_WriteMutex);

		m_PackedEntitiesPool.Free(pPackedEntity);

		// if we have a uncompression cache, remove reference too
		FOR_EACH_VEC(m_PackedEntityCache, i)
		{
			UnpackedDataCache_t& pdc = m_PackedEntityCache[i];
			if (pdc.pEntity == pPackedEntity)
			{
				pdc.pEntity = NULL;
				pdc.counter = 0;
				break;
			}
		}
	}
}

void PackedEntityManager::AddEntityReference(PackedEntity* pPackedEntity)
{
	Assert(pPackedEntity != INVALID_PACKED_ENTITY_HANDLE);
	pPackedEntity->m_ReferenceCount++;
}

// ------------------------------------------------------------------------------------------------ //
// purpose: lookup cache if we have an uncompressed version of this packed entity
// ------------------------------------------------------------------------------------------------ //
UnpackedDataCache_t* PackedEntityManager::GetCachedUncompressedEntity(PackedEntity* packedEntity)
{
	if (m_PackedEntityCache.Count() == 0)
	{
		// ops, we have no cache yet, create one and reset counter
		m_nPackedEntityCacheCounter = 0;
		m_PackedEntityCache.SetCount(128);

		FOR_EACH_VEC(m_PackedEntityCache, i)
		{
			m_PackedEntityCache[i].pEntity = NULL;
			m_PackedEntityCache[i].counter = 0;
		}
	}

	m_nPackedEntityCacheCounter++;

	// remember oldest cache entry
	UnpackedDataCache_t* pdcOldest = NULL;
	int oldestValue = m_nPackedEntityCacheCounter;


	FOR_EACH_VEC(m_PackedEntityCache, i)
	{
		UnpackedDataCache_t* pdc = &m_PackedEntityCache[i];

		if (pdc->pEntity == packedEntity)
		{
			// hit, found it, update counter
			pdc->counter = m_nPackedEntityCacheCounter;
			return pdc;
		}

		if (pdc->counter < oldestValue)
		{
			oldestValue = pdc->counter;
			pdcOldest = pdc;
		}
	}

	Assert(pdcOldest);

	// hmm, not in cache, clear & return oldest one
	pdcOldest->counter = m_nPackedEntityCacheCounter;
	pdcOldest->bits = -1;	// important, this is the signal for the caller to fill this structure
	pdcOldest->pEntity = packedEntity;
	return pdcOldest;
}

//-----------------------------------------------------------------------------
// Returns the pack data for a particular entity for a particular snapshot
//-----------------------------------------------------------------------------

PackedEntity* PackedEntityManager::CreatePackedEntity(CFrameSnapshot* pSnapshot, int entity)
{
	m_WriteMutex.Lock();
	PackedEntity* packedEntity = m_PackedEntitiesPool.Alloc();
	//PackedEntity * handle =  packedEntity ;
	m_WriteMutex.Unlock();

	Assert(entity < pSnapshot->m_nNumEntities);

	// Referenced twice: in the mru 
	packedEntity->m_ReferenceCount = 2;
	packedEntity->m_nEntityIndex = entity;
	m_pEntities.Element(pSnapshot->m_ListIndex)[entity].m_pPackedData = packedEntity;

	// Add a reference into the global list of last entity packets seen...
	// and remove the reference to the last entity packet we saw
	if (m_pPackedData[entity] != INVALID_PACKED_ENTITY_HANDLE)
	{
		RemoveEntityReference(m_pPackedData[entity]);
	}

	m_pPackedData[entity] = packedEntity;
	m_pSerialNumber[entity] = m_pEntities.Element(pSnapshot->m_ListIndex)[entity].m_nSerialNumber;

	packedEntity->SetSnapshotCreationTick(pSnapshot->m_nTickCount);

	return packedEntity;
}

CFrameSnapshotEntry* PackedEntityManager::GetSnapshotEntry(CFrameSnapshot* pSnapshot, int entity) {
	if (!pSnapshot)
		return NULL;

	Assert(entity < pSnapshot->m_nNumEntities);

	CFrameSnapshotEntry* pSnapshotEntry = &m_pEntities.Element(pSnapshot->m_ListIndex)[entity];

	return pSnapshotEntry;
}

//-----------------------------------------------------------------------------
// Returns the pack data for a particular entity for a particular snapshot
//-----------------------------------------------------------------------------

PackedEntity* PackedEntityManager::GetPackedEntity(CFrameSnapshot* pSnapshot, int entity)
{
	if (!pSnapshot)
		return NULL;

	Assert(entity < pSnapshot->m_nNumEntities);

	PackedEntity* pPackedEntity = m_pEntities.Element(pSnapshot->m_ListIndex)[entity].m_pPackedData;

	if (pPackedEntity == INVALID_PACKED_ENTITY_HANDLE)
		return NULL;

	//PackedEntity* packedEntity = pPackedEntity;
	Assert(pPackedEntity->m_nEntityIndex == entity);
	return pPackedEntity;
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if the "basis" for encoding m_flAnimTime, m_flSimulationTime has changed 
//  since the time this entity was packed to the time we're trying to re-use the packing.
//-----------------------------------------------------------------------------
bool PackedEntityManager::ShouldForceRepack(CFrameSnapshot* pSnapshot, int entity, PackedEntity* pPackedEntity)
{
	if (sv_creationtickcheck.GetBool())
	{
		//PackedEntity *pe =  handle ;
		Assert(pPackedEntity);
		if (pPackedEntity && pPackedEntity->ShouldCheckCreationTick())
		{
			int nCurrentNetworkBase = g_ServerGlobalVariables.GetNetworkBase(pSnapshot->m_nTickCount, entity);
			int nPackedEntityNetworkBase = g_ServerGlobalVariables.GetNetworkBase(pPackedEntity->GetSnapshotCreationTick(), entity);
			if (nCurrentNetworkBase != nPackedEntityNetworkBase)
			{
				return true;
			}
		}
	}

	return false;
}

bool PackedEntityManager::UsePreviouslySentPacket(CFrameSnapshot* pSnapshot,
	int entity, int entSerialNumber)
{
	PackedEntity* pPackedEntity = m_pPackedData[entity];
	if (pPackedEntity != INVALID_PACKED_ENTITY_HANDLE)
	{
		// NOTE: We can't use the previously sent packet if there was a 
		// serial number change....
		if (m_pSerialNumber[entity] == entSerialNumber)
		{
			// Check if we need to re-pack entity due to encoding against gpGlobals->tickcount
			if (ShouldForceRepack(pSnapshot, entity, pPackedEntity))
			{
				return false;
			}

			Assert(entity < pSnapshot->m_nNumEntities);
			m_pEntities.Element(pSnapshot->m_ListIndex)[entity].m_pPackedData = pPackedEntity;
			pPackedEntity->m_ReferenceCount++;
			return true;
		}
		else
		{
			return false;
		}
	}

	return false;
}


PackedEntity* PackedEntityManager::GetPreviouslySentPacket(int iEntity, int iSerialNumber)
{
	PackedEntity* pPackedEntity = m_pPackedData[iEntity];
	if (pPackedEntity != INVALID_PACKED_ENTITY_HANDLE)
	{
		// NOTE: We can't use the previously sent packet if there was a 
		// serial number change....
		if (m_pSerialNumber[iEntity] == iSerialNumber)
		{
			return  pPackedEntity;
		}
		else
		{
			return NULL;
		}
	}

	return NULL;
}