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
#include "dt_send_eng.h"
#include "dt.h"
#include "tier0/vcrmode.h"
#include "hltvserver.h"
#if defined( REPLAY_ENABLED )
#include "replayserver.h"
#endif

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
	m_ClientSnapshotEntryInfo.Purge();
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

PackedEntity* PackedEntityManager::CreatePackedEntity(CFrameSnapshot* pSnapshot, IServerEntity* pServerEntity)
{
	m_WriteMutex.Lock();
	PackedEntity* packedEntity = m_PackedEntitiesPool.Alloc();
	//PackedEntity * handle =  packedEntity ;
	m_WriteMutex.Unlock();

	int edictIdx = pServerEntity->entindex();
	ServerClass* pServerClass = pServerEntity->GetServerClass();
	int iSerialNum = serverEntitylist->GetNetworkSerialNumber(edictIdx);
	Assert(edictIdx < pSnapshot->m_nNumEntities);

	// Referenced twice: in the mru 
	packedEntity->m_ReferenceCount = 2;
	packedEntity->m_nEntityIndex = edictIdx;

	CFrameSnapshotEntry* pFrameSnapshotEntry = GetSnapshotEntry(pSnapshot, edictIdx);
	pFrameSnapshotEntry->m_nSerialNumber = iSerialNum;
	pFrameSnapshotEntry->m_pClass = pServerClass;
	pFrameSnapshotEntry->m_pPackedData = packedEntity;

	// Add a reference into the global list of last entity packets seen...
	// and remove the reference to the last entity packet we saw
	if (m_pPackedData[edictIdx] != INVALID_PACKED_ENTITY_HANDLE)
	{
		RemoveEntityReference(m_pPackedData[edictIdx]);
	}

	m_pPackedData[edictIdx] = packedEntity;
	m_pSerialNumber[edictIdx] = iSerialNum;

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

	CFrameSnapshotEntry* pFrameSnapshotEntry = GetSnapshotEntry(pSnapshot, entity);
	PackedEntity* pPackedEntity = pFrameSnapshotEntry->m_pPackedData;

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

bool PackedEntityManager::UsePreviouslySentPacket(CFrameSnapshot* pSnapshot, IServerEntity* pServerEntity)
{
	int edictIdx = pServerEntity->entindex();
	ServerClass* pServerClass = pServerEntity->GetServerClass();
	int iSerialNum = serverEntitylist->GetNetworkSerialNumber(edictIdx);

	PackedEntity* pPackedEntity = m_pPackedData[edictIdx];
	if (pPackedEntity != INVALID_PACKED_ENTITY_HANDLE)
	{
		// NOTE: We can't use the previously sent packet if there was a 
		// serial number change....
		if (m_pSerialNumber[edictIdx] == iSerialNum)
		{
			// Check if we need to re-pack entity due to encoding against gpGlobals->tickcount
			if (ShouldForceRepack(pSnapshot, edictIdx, pPackedEntity))
			{
				return false;
			}

			Assert(edictIdx < pSnapshot->m_nNumEntities);

			CFrameSnapshotEntry* pFrameSnapshotEntry = GetSnapshotEntry(pSnapshot, edictIdx);
			pFrameSnapshotEntry->m_nSerialNumber = iSerialNum;
			pFrameSnapshotEntry->m_pClass = pServerClass;
			pFrameSnapshotEntry->m_pPackedData = pPackedEntity;
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

void	PackedEntityManager::OnClientConnected(CBaseClient* pClient) {
	CheckClientSnapshotEntryArray(pClient->m_nClientSlot);
	CClientSnapshotEntryInfo* pClientSnapshotEntryInfo = &m_ClientSnapshotEntryInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotEntryInfo->m_BaselinesSent.ClearAll();
}

void	PackedEntityManager::FreeClientBaselines(CBaseClient* pClient) {
	CheckClientSnapshotEntryArray(pClient->m_nClientSlot);
	CClientSnapshotEntryInfo* pClientSnapshotEntryInfo = &m_ClientSnapshotEntryInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotEntryInfo->m_BaselinesSent.ClearAll();
	if (pClientSnapshotEntryInfo->m_pBaselineEntities)
	{
		//m_pBaseline->ReleaseReference();
		for (int i = 0; i < pClientSnapshotEntryInfo->m_nNumBaselineEntities; ++i)
		{
			if (pClientSnapshotEntryInfo->m_pBaselineEntities[i].m_pPackedData != INVALID_PACKED_ENTITY_HANDLE)
			{
				RemoveEntityReference(pClientSnapshotEntryInfo->m_pBaselineEntities[i].m_pPackedData);
			}
		}
		//m_pBaseline = NULL;
		delete[] pClientSnapshotEntryInfo->m_pBaselineEntities;
		pClientSnapshotEntryInfo->m_pBaselineEntities = null;
		pClientSnapshotEntryInfo->m_nNumBaselineEntities = 0;
	}
}

void	PackedEntityManager::AllocClientBaselines(CBaseClient* pClient) {
	CheckClientSnapshotEntryArray(pClient->m_nClientSlot);
	CClientSnapshotEntryInfo* pClientSnapshotEntryInfo = &m_ClientSnapshotEntryInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotEntryInfo->m_pBaselineEntities = new CFrameSnapshotEntry[MAX_EDICTS];
	pClientSnapshotEntryInfo->m_nNumBaselineEntities = MAX_EDICTS;
}

bool	PackedEntityManager::ProcessBaselineAck(CBaseClient* pClient, CFrameSnapshot* pSnapshot) {
	CheckClientSnapshotEntryArray(pClient->m_nClientSlot);
	CClientSnapshotEntryInfo* pClientSnapshotEntryInfo = &m_ClientSnapshotEntryInfo.Element(pClient->m_nClientSlot);
	
	int index = pClientSnapshotEntryInfo->m_BaselinesSent.FindNextSetBit(0);

	while (index >= 0)
	{
		// get new entity
		PackedEntity* hNewPackedEntity = GetPackedEntity(pSnapshot, index);
		if (hNewPackedEntity == INVALID_PACKED_ENTITY_HANDLE)
		{
			DevMsg("CBaseClient::ProcessBaselineAck: invalid packet handle (%i)\n", index);
			return false;
		}

		PackedEntity* hOldPackedEntity = pClientSnapshotEntryInfo->m_pBaselineEntities[index].m_pPackedData;//m_pBaseline->

		if (hOldPackedEntity != INVALID_PACKED_ENTITY_HANDLE)
		{
			// remove reference before overwriting packed entity
			RemoveEntityReference(hOldPackedEntity);
		}

		// increase reference
		AddEntityReference(hNewPackedEntity);

		// copy entity handle, class & serial number to
		pClientSnapshotEntryInfo->m_pBaselineEntities[index] = *GetSnapshotEntry(pSnapshot, index);//m_pBaseline->

		// go to next entity
		index = pClientSnapshotEntryInfo->m_BaselinesSent.FindNextSetBit(index + 1);
	}
}

CClientSnapshotEntryInfo* PackedEntityManager::GetClientSnapshotEntryInfo(CBaseClient* pClient) {
	CheckClientSnapshotEntryArray(pClient->m_nClientSlot);
	CClientSnapshotEntryInfo* pClientSnapshotEntryInfo = &m_ClientSnapshotEntryInfo.Element(pClient->m_nClientSlot);
	return pClientSnapshotEntryInfo;
}

void PackedEntityManager::CheckClientSnapshotEntryArray(int maxIndex) {
	while (m_ClientSnapshotEntryInfo.Count() < maxIndex + 1) {
		m_ClientSnapshotEntryInfo.AddToTail();
	}
}

ConVar sv_debugmanualmode("sv_debugmanualmode", "0", 0, "Make sure entities correctly report whether or not their network data has changed.");

// This function makes sure that this entity class has an instance baseline.
// If it doesn't have one yet, it makes a new one.
void SV_EnsureInstanceBaseline(ServerClass* pServerClass, const void* pData, int nBytes)//int iEdict, 
{
	//IServerEntity* pEnt = serverEntitylist->GetServerEntity(iEdict);
	//ErrorIfNot(pEnt->GetNetworkable(),
	//	("SV_EnsureInstanceBaseline: edict %d missing ent", iEdict)
	//);

	//ServerClass* pClass = pEnt->GetServerClass();

	// See if we already have a baseline for this class.
	if (pServerClass->m_InstanceBaselineIndex == INVALID_STRING_INDEX)
	{
		AUTO_LOCK(g_svInstanceBaselineMutex);

		// We need this second check in case multiple instances of the same class have grabbed the lock.
		if (pServerClass->m_InstanceBaselineIndex == INVALID_STRING_INDEX)
		{
			char idString[32];
			Q_snprintf(idString, sizeof(idString), "%d", pServerClass->m_ClassID);

			// Ok, make a new instance baseline so they can reference it.
			int temp = sv.GetInstanceBaselineTable()->AddString(
				true,
				idString,	// Note we're sending a string with the ID number, not the class name.
				nBytes,
				pData);

			// Insert a compiler and/or CPU memory barrier to ensure that all side-effects have
			// been published before the index is published. Otherwise the string index may
			// be visible before its initialization has finished. This potential problem is caused
			// by the use of double-checked locking -- the problem is that the code outside of the
			// lock is looking at the variable that is protected by the lock. See this article for details:
			// http://en.wikipedia.org/wiki/Double-checked_locking
			// Write-release barrier
			ThreadMemoryBarrier();
			pServerClass->m_InstanceBaselineIndex = temp;
			Assert(pServerClass->m_InstanceBaselineIndex != INVALID_STRING_INDEX);
		}
	}
	// Read-acquire barrier
	ThreadMemoryBarrier();
}

void PackedEntityManager::InitPackedEntity(CFrameSnapshot* pSnapshot, IServerEntity* pServerEntity) {
	int edictIdx = pServerEntity->entindex();
	ServerClass* pServerClass = pServerEntity->GetServerClass();
	int iSerialNum = serverEntitylist->GetNetworkSerialNumber(edictIdx);

	CFrameSnapshotEntry* pFrameSnapshotEntry = GetSnapshotEntry(pSnapshot, edictIdx);
	pFrameSnapshotEntry->m_nSerialNumber = iSerialNum;
	pFrameSnapshotEntry->m_pClass = pServerClass;
}

//-----------------------------------------------------------------------------
// Pack the entity....
//-----------------------------------------------------------------------------

void PackedEntityManager::DoPackEntity(CFrameSnapshot* pSnapshot, IServerEntity* pServerEntity)
{
	int edictIdx = pServerEntity->entindex();
	Assert(edictIdx < pSnapshot->m_nNumEntities);
	tmZoneFiltered(TELEMETRY_LEVEL0, 50, TMZF_NONE, "PackEntities_Normal%s", __FUNCTION__);
	ServerClass* pServerClass = pServerEntity->GetServerClass();
	SendTable* pSendTable = pServerClass->m_pTable;
	int iSerialNum = serverEntitylist->GetNetworkSerialNumber(edictIdx);

	// Check to see if this entity specifies its changes.
	// If so, then try to early out making the fullpack
	bool bUsedPrev = false;

	if (!pServerEntity->GetNetworkable()->HasStateChanged())
	{
		// Now this may not work if we didn't previously send a packet;
		// if not, then we gotta compute it
		bUsedPrev = UsePreviouslySentPacket(pSnapshot, pServerEntity);
	}

	if (bUsedPrev && !sv_debugmanualmode.GetInt())
	{
		pServerEntity->GetNetworkable()->ClearStateChanged();
		return;
	}

	// First encode the entity's data.
	ALIGN4 char packedData[MAX_PACKEDENTITY_DATA] ALIGN4_POST;
	bf_write writeBuf("SV_PackEntity->writeBuf", packedData, sizeof(packedData));

	// (avoid constructor overhead).
	unsigned char tempData[sizeof(CSendProxyRecipients) * MAX_DATATABLE_PROXIES];
	CUtlMemory< CSendProxyRecipients > recip((CSendProxyRecipients*)tempData, pSendTable->m_pPrecalc->GetNumDataTableProxies());

	if (!SendTable_Encode(pSendTable, pServerEntity, &writeBuf, edictIdx, &recip, false))
	{
		Host_Error("SV_PackEntity: SendTable_Encode returned false (ent %d).\n", edictIdx);
	}

#ifndef NO_VCR
	// VCR mode stuff..
	if (vcr_verbose.GetInt() && writeBuf.GetNumBytesWritten() > 0)
		VCRGenericValueVerify("writebuf", writeBuf.GetBasePointer(), writeBuf.GetNumBytesWritten() - 1);
#endif

	SV_EnsureInstanceBaseline(pServerClass, packedData, writeBuf.GetNumBytesWritten());// edictIdx, 

	int nFlatProps = SendTable_GetNumFlatProps(pSendTable);
	IChangeFrameList* pChangeFrame = NULL;

	// If this entity was previously in there, then it should have a valid IChangeFrameList 
	// which we can delta against to figure out which properties have changed.
	//
	// If not, then we want to setup a new IChangeFrameList.

	PackedEntity* pPrevFrame = GetPreviouslySentPacket(edictIdx, iSerialNum);
	if (pPrevFrame)
	{
		// Calculate a delta.
		Assert(!pPrevFrame->IsCompressed());

		int deltaProps[MAX_DATATABLE_PROPS];

		int nChanges = SendTable_CalcDelta(
			pSendTable,
			pPrevFrame->GetData(), pPrevFrame->GetNumBits(),
			packedData, writeBuf.GetNumBitsWritten(),

			deltaProps,
			ARRAYSIZE(deltaProps),

			edictIdx
		);

#ifndef NO_VCR
		if (vcr_verbose.GetInt())
			VCRGenericValueVerify("nChanges", &nChanges, sizeof(nChanges));
#endif

		// If it's non-manual-mode, but we detect that there are no changes here, then just
		// use the previous pSnapshot if it's available (as though the entity were manual mode).
		// It would be interesting to hook here and see how many non-manual-mode entities 
		// are winding up with no changes.
		if (nChanges == 0)
		{
			if (pPrevFrame->CompareRecipients(recip))
			{
				if (UsePreviouslySentPacket(pSnapshot, pServerEntity))
				{
					pServerEntity->GetNetworkable()->ClearStateChanged();
					return;
				}
			}
		}
		else
		{
			if (!pServerEntity->GetNetworkable()->HasStateChanged())
			{
				for (int iDeltaProp = 0; iDeltaProp < nChanges; iDeltaProp++)
				{
					Assert(pSendTable->m_pPrecalc);
					Assert(deltaProps[iDeltaProp] < pSendTable->m_pPrecalc->GetNumProps());

					const SendProp* pProp = pSendTable->m_pPrecalc->GetProp(deltaProps[iDeltaProp]);
					// If a field changed, but it changed because it encoded against tickcount, 
					//   then it's just like the entity changed the underlying field, not an error, that is.
					if (pProp->GetFlags() & SPROP_ENCODED_AGAINST_TICKCOUNT)
						continue;

					Msg("Entity %d (class '%s') reported ENTITY_CHANGE_NONE but '%s' changed.\n",
						edictIdx,
						pServerEntity->GetClassName(),
						pProp->GetName());

				}
			}
		}

#ifndef _XBOX	
#if defined( REPLAY_ENABLED )
		if ((hltv && hltv->IsActive()) || (replay && replay->IsActive()))
#else
		if (hltv && hltv->IsActive())
#endif
		{
			// in HLTV or Replay mode every PackedEntity keeps it's own ChangeFrameList
			// we just copy the ChangeFrameList from prev frame and update it
			pChangeFrame = pPrevFrame->GetChangeFrameList();
			pChangeFrame = pChangeFrame->Copy(); // allocs and copies ChangeFrameList
		}
		else
#endif
		{
			// Ok, now snag the changeframe from the previous frame and update the 'last frame changed'
			// for the properties in the delta.
			pChangeFrame = pPrevFrame->SnagChangeFrameList();
		}

		ErrorIfNot(pChangeFrame,
			("SV_PackEntity: SnagChangeFrameList returned null"));
		ErrorIfNot(pChangeFrame->GetNumProps() == nFlatProps,
			("SV_PackEntity: SnagChangeFrameList mismatched number of props[%d vs %d]", nFlatProps, pChangeFrame->GetNumProps()));

		pChangeFrame->SetChangeTick(deltaProps, nChanges, pSnapshot->m_nTickCount);
	}
	else
	{
		// Ok, init the change frames for the first time.
		pChangeFrame = AllocChangeFrameList(nFlatProps, pSnapshot->m_nTickCount);
	}

	// Now make a PackedEntity and store the new packed data in there.
	//entry->m_nSerialNumber = serverEntitylist->GetNetworkSerialNumber(i);
	//entry->m_pClass = serverEntitylist->GetServerEntity(i)->GetServerClass();
	{
		PackedEntity* pPackedEntity = CreatePackedEntity(pSnapshot, pServerEntity);
		pPackedEntity->SetChangeFrameList(pChangeFrame);
		pPackedEntity->SetServerAndClientClass(pServerClass, NULL);
		pPackedEntity->AllocAndCopyPadded(packedData, writeBuf.GetNumBytesWritten());
		pPackedEntity->SetRecipients(recip);
	}

	pServerEntity->GetNetworkable()->ClearStateChanged();
}