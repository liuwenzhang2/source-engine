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
#include "framesnapshot.h"
#include "server_pch.h"
#include "sys_dll.h"
#include "dt_send_eng.h"
#include "dt.h"
#include "tier0/vcrmode.h"
#include "hltvserver.h"
#if defined( REPLAY_ENABLED )
#include "replayserver.h"
#endif
#include "dt_instrumentation_server.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar sv_creationtickcheck("sv_creationtickcheck", "1", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Do extended check for encoding of timestamps against tickcount");
#if defined( DEBUG_NETWORKING )
ConVar  sv_packettrace("sv_packettrace", "1", 0, "For debugging, print entity creation/deletion info to console.");
#endif
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
	m_pEntities.SetLessFunc(CFrameSnapshot_LessFunc);
}

PackedEntityManager::~PackedEntityManager() {
	// TODO: This assert has been failing. HenryG says it's a valid assert and that we're probably leaking memory.
	AssertMsg1(m_PackedEntitiesPool.Count() == 0 || IsInErrorExit(), "Expected m_PackedEntitiesPool to be empty. It had %i items.", m_PackedEntitiesPool.Count());
	m_ClientBaselineInfo.Purge();
}

void PackedEntityManager::OnLevelChanged() {
	m_PackedEntityCache.RemoveAll();
	Q_memset(m_pPackedData, 0x00, MAX_EDICTS * sizeof(PackedEntity*));
}

void PackedEntityManager::OnCreateSnapshot(CFrameSnapshot* pSnapshot) {
	if (m_pEntities.Find(pSnapshot) != m_pEntities.InvalidIndex()) {
		Error("m_pEntities.Find(pSnapshot)!");
	}
	CFrameSnapshotEntry* pSnapshotEntry = new CFrameSnapshotEntry[pSnapshot->m_nNumEntities];
	// clear entries
	for (int i = 0; i < pSnapshot->m_nNumEntities; i++)
	{
		pSnapshotEntry[i].m_pClass = NULL;
		pSnapshotEntry[i].m_nSerialNumber = -1;
		pSnapshotEntry[i].m_pPackedData = INVALID_PACKED_ENTITY_HANDLE;
	}
	m_pEntities.Insert(pSnapshot, pSnapshotEntry);
}

void PackedEntityManager::OnDeleteSnapshot(CFrameSnapshot* pSnapshot) {
	if (!pSnapshot)
		return;
	unsigned short index = m_pEntities.Find(pSnapshot);
	if (index == m_pEntities.InvalidIndex()) {
		Error("index == m_pEntities.InvalidIndex()");
	}
	CFrameSnapshotEntry* pSnapshotEntry = m_pEntities.Element(index);
	// Decrement reference counts of all packed entities
	for (int i = 0; i < pSnapshot->m_nNumEntities; ++i)
	{
		if (pSnapshotEntry[i].m_pPackedData != INVALID_PACKED_ENTITY_HANDLE)
		{
			RemoveEntityReference(pSnapshotEntry[i].m_pPackedData);
		}
	}
	delete[] pSnapshotEntry;
	m_pEntities.RemoveAt(index);
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

// compresses a packed entity, returns data & bits
const char* PackedEntityManager::CompressPackedEntity(ServerClass* pServerClass, const char* data, int& bits)
{
	ALIGN4 static char s_packedData[MAX_PACKEDENTITY_DATA] ALIGN4_POST;

	bf_write writeBuf("CompressPackedEntity", s_packedData, sizeof(s_packedData));

	const void* pBaselineData = NULL;
	int nBaselineBits = 0;

	Assert(pServerClass != NULL);

	sv.GetClassBaseline(pServerClass, &pBaselineData, &nBaselineBits);
	nBaselineBits *= 8;

	Assert(pBaselineData != NULL);

	SendTable_WriteAllDeltaProps(
		pServerClass->m_pTable,
		pBaselineData,
		nBaselineBits,
		data,
		bits,
		-1,
		&writeBuf);

	//overwrite in bits with out bits
	bits = writeBuf.GetNumBitsWritten();

	return s_packedData;
}

// uncompresses a 
const char* PackedEntityManager::UncompressPackedEntity(PackedEntity* pPackedEntity, int& bits)
{
	UnpackedDataCache_t* pdc = GetCachedUncompressedEntity(pPackedEntity);

	if (pdc->bits > 0)
	{
		// found valid uncompressed version in cache
		bits = pdc->bits;
		return pdc->data;
	}

	// not in cache, so uncompress it

	const void* pBaseline;
	int nBaselineBytes = 0;

	sv.GetClassBaseline(pPackedEntity->m_pServerClass, &pBaseline, &nBaselineBytes);

	Assert(pBaseline != NULL);

	// store this baseline in u.m_pUpdateBaselines
	bf_read oldBuf("UncompressPackedEntity1", pBaseline, nBaselineBytes);
	bf_read newBuf("UncompressPackedEntity2", pPackedEntity->GetData(), Bits2Bytes(pPackedEntity->GetNumBits()));
	bf_write outBuf("UncompressPackedEntity3", pdc->data, MAX_PACKEDENTITY_DATA);

	Assert(pPackedEntity->m_pClientClass);

	RecvTable_MergeDeltas(
		pPackedEntity->m_pClientClass->m_pRecvTable,
		&oldBuf,
		&newBuf,
		&outBuf);

	bits = pdc->bits = outBuf.GetNumBitsWritten();

	return pdc->data;
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

PackedEntity* PackedEntityManager::CreatePackedEntity(CFrameSnapshot* pSnapshot, int edictIdx, ServerClass* pServerClass, int iSerialNum)
{
	m_WriteMutex.Lock();
	PackedEntity* packedEntity = m_PackedEntitiesPool.Alloc();
	//PackedEntity * handle =  packedEntity ;
	m_WriteMutex.Unlock();

	//int edictIdx = pServerEntity->entindex();
	//ServerClass* pServerClass = pServerEntity->GetServerClass();
	//int iSerialNum = serverEntitylist->GetNetworkSerialNumber(edictIdx);
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
	unsigned short index = m_pEntities.Find(pSnapshot);
	if (index == m_pEntities.InvalidIndex()) {
		Error("index == m_pEntities.InvalidIndex()");
	}
	CFrameSnapshotEntry* pSnapshotEntry = m_pEntities.Element(index);
	return &pSnapshotEntry[entity];
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
	CClientBaselineInfo* pClientBaselineInfo = &m_ClientBaselineInfo.Element(pClient->m_nClientSlot);
}

void	PackedEntityManager::FreeClientBaselines(CBaseClient* pClient) {
	CheckClientSnapshotEntryArray(pClient->m_nClientSlot);
	CClientBaselineInfo* pClientBaselineInfo = &m_ClientBaselineInfo.Element(pClient->m_nClientSlot);
	if (pClientBaselineInfo->m_pPackedEntities)
	{
		//m_pBaseline->ReleaseReference();
		for (int i = 0; i < pClientBaselineInfo->m_nNumPackedEntities; ++i)
		{
			if (pClientBaselineInfo->m_pPackedEntities[i] != INVALID_PACKED_ENTITY_HANDLE)
			{
				RemoveEntityReference(pClientBaselineInfo->m_pPackedEntities[i]);
			}
		}
		//m_pBaseline = NULL;
		delete[] pClientBaselineInfo->m_pPackedEntities;
		pClientBaselineInfo->m_pPackedEntities = null;
		pClientBaselineInfo->m_nNumPackedEntities = 0;
	}
}

void	PackedEntityManager::AllocClientBaselines(CBaseClient* pClient) {
	CheckClientSnapshotEntryArray(pClient->m_nClientSlot);
	CClientBaselineInfo* pClientBaselineInfo = &m_ClientBaselineInfo.Element(pClient->m_nClientSlot);
	pClientBaselineInfo->m_pPackedEntities = new PackedEntity*[MAX_EDICTS];
	for (int i = 0; i < MAX_EDICTS; i++) {
		pClientBaselineInfo->m_pPackedEntities[i] = INVALID_PACKED_ENTITY_HANDLE;
	}
	pClientBaselineInfo->m_nNumPackedEntities = MAX_EDICTS;
}

bool	PackedEntityManager::ProcessBaselineAck(CBaseClient* pClient, CFrameSnapshot* pSnapshot) {
	CheckClientSnapshotEntryArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = framesnapshotmanager->GetClientSnapshotInfo(pClient);
	CClientBaselineInfo* pClientBaselineInfo = &m_ClientBaselineInfo.Element(pClient->m_nClientSlot);
	
	int index = pClientSnapshotInfo->m_BaselinesSent.FindNextSetBit(0);

	while (index >= 0)
	{
		// get new entity
		PackedEntity* hNewPackedEntity = GetPackedEntity(pSnapshot, index);
		if (hNewPackedEntity == INVALID_PACKED_ENTITY_HANDLE)
		{
			DevMsg("CBaseClient::ProcessBaselineAck: invalid packet handle (%i)\n", index);
			return false;
		}

		PackedEntity* hOldPackedEntity = pClientBaselineInfo->m_pPackedEntities[index];//m_pBaseline->

		if (hOldPackedEntity != INVALID_PACKED_ENTITY_HANDLE)
		{
			// remove reference before overwriting packed entity
			RemoveEntityReference(hOldPackedEntity);
		}

		// increase reference
		AddEntityReference(hNewPackedEntity);

		// copy entity handle, class & serial number to
		pClientBaselineInfo->m_pPackedEntities[index] = GetSnapshotEntry(pSnapshot, index)->m_pPackedData;//m_pBaseline->

		// go to next entity
		index = pClientSnapshotInfo->m_BaselinesSent.FindNextSetBit(index + 1);
	}
	return true;
}

bool PackedEntityManager::IsSamePackedEntity(CEntityWriteInfo& u) {
	CFrameSnapshotEntry* oldFrameSnapshotEntry = GetSnapshotEntry(u.m_pFromSnapshot, u.m_nOldEntity);
	CFrameSnapshotEntry* toFrameSnapshotEntry = GetSnapshotEntry(u.m_pToSnapshot, u.m_nNewEntity);
	return oldFrameSnapshotEntry == toFrameSnapshotEntry;
}

void PackedEntityManager::GetChangedProps(CEntityWriteInfo& u) {
	
	PackedEntity* pNewPack = (u.m_nNewEntity != ENTITY_SENTINEL) ? GetPackedEntity(u.m_pToSnapshot, u.m_nNewEntity) : NULL;
	PackedEntity* pOldPack = (u.m_nOldEntity != ENTITY_SENTINEL) ? GetPackedEntity(u.m_pFromSnapshot, u.m_nOldEntity) : NULL;
	
	u.nCheckProps = pNewPack->GetPropsChangedAfterTick(u.m_pFromSnapshot->m_nTickCount, u.checkProps, ARRAYSIZE(u.checkProps));

	if (u.nCheckProps == -1)
	{
		// check failed, we have to recalc delta props based on from & to snapshot
		// that should happen only in HLTV/Replay demo playback mode, this code is really expensive

		const void* pOldData, * pNewData;
		int nOldBits, nNewBits;

		if (pOldPack->IsCompressed())
		{
			pOldData = UncompressPackedEntity(pOldPack, nOldBits);
		}
		else
		{
			pOldData = pOldPack->GetData();
			nOldBits = pOldPack->GetNumBits();
		}

		if (pNewPack->IsCompressed())
		{
			pNewData = UncompressPackedEntity(pNewPack, nNewBits);
		}
		else
		{
			pNewData = pNewPack->GetData();
			nNewBits = pNewPack->GetNumBits();
		}

		u.nCheckProps = SendTable_CalcDelta(
			pOldPack->m_pServerClass->m_pTable,
			pOldData,
			nOldBits,
			pNewData,
			nNewBits,
			u.checkProps,
			ARRAYSIZE(u.checkProps),
			u.m_nNewEntity
		);
	}

#ifndef NO_VCR
	if (vcr_verbose.GetInt())
	{
		VCRGenericValueVerify("checkProps", u.checkProps, sizeof(u.checkProps[0]) * u.nCheckProps);
	}
#endif
}

void PackedEntityManager::WriteDeltaEnt(CBaseClient* pClient, CEntityWriteInfo& u, const int* pCheckProps, const int nCheckProps) {
	
	PackedEntity* pNewPack = (u.m_nNewEntity != ENTITY_SENTINEL) ? GetPackedEntity(u.m_pToSnapshot, u.m_nNewEntity) : NULL;
	PackedEntity* pOldPack = (u.m_nOldEntity != ENTITY_SENTINEL) ? GetPackedEntity(u.m_pFromSnapshot, u.m_nOldEntity) : NULL;
	//PackedEntity* pTo = u.m_pNewPack;
	//PackedEntity* pFrom = u.m_pOldPack;
	SendTable* pSendTable = pNewPack->m_pServerClass->m_pTable;

	CServerDTITimer timer(pSendTable, SERVERDTI_WRITE_DELTA_PROPS);
	if (g_bServerDTIEnabled && !sv.IsHLTV() && !sv.IsReplay())
	{
		ICollideable* pEnt = serverEntitylist->GetServerEntity(pNewPack->m_nEntityIndex)->GetCollideable();
		ICollideable* pClientEnt = serverEntitylist->GetServerEntity(pClient->m_nEntityIndex)->GetCollideable();
		if (pEnt && pClientEnt)
		{
			float flDist = (pEnt->GetCollisionOrigin() - pClientEnt->GetCollisionOrigin()).Length();
			ServerDTI_AddEntityEncodeEvent(pSendTable, flDist);
		}
	}

	const void* pToData;
	int nToBits;

	if (pNewPack->IsCompressed())
	{
		// let server uncompress PackedEntity
		pToData = UncompressPackedEntity(pNewPack, nToBits);
	}
	else
	{
		// get raw data direct
		pToData = pNewPack->GetData();
		nToBits = pNewPack->GetNumBits();
	}

	Assert(pToData != NULL);

	// Cull out the properties that their proxies said not to send to this client.
	int pSendProps[MAX_DATATABLE_PROPS];
	const int* sendProps = pCheckProps;
	int nSendProps = nCheckProps;
	bf_write bufStart;


	// cull properties that are removed by SendProxies for this client.
	// don't do that for HLTV relay proxies
	if (u.m_bCullProps)
	{
		sendProps = pSendProps;

		nSendProps = SendTable_CullPropsFromProxies(
			pSendTable,
			pCheckProps,
			nCheckProps,
			pClient->m_nClientSlot,

			pOldPack->GetRecipients(),
			pOldPack->GetNumRecipients(),

			pNewPack->GetRecipients(),
			pNewPack->GetNumRecipients(),

			pSendProps,
			ARRAYSIZE(pSendProps)
		);
	}
	else
	{
		// this is a HLTV relay proxy
		bufStart = *u.m_pBuf;
	}

	SendTable_WritePropList(
		pSendTable,
		pToData,
		nToBits,
		u.m_pBuf,
		pNewPack->m_nEntityIndex,

		sendProps,
		nSendProps
	);

	if (!u.m_bCullProps && hltv)
	{
		// this is a HLTV relay proxy, cache delta bits
		int nBits = u.m_pBuf->GetNumBitsWritten() - bufStart.GetNumBitsWritten();
		hltv->m_DeltaCache.AddDeltaBits(pNewPack->m_nEntityIndex, u.m_pFromSnapshot->m_nTickCount, nBits, &bufStart);
	}
}

void PackedEntityManager::WriteEnterPVS(CBaseClient* pClient, CEntityWriteInfo& u) {

	Assert(u.m_nNewEntity < u.m_pToSnapshot->m_nNumEntities);

	PackedEntity* pNewPack = (u.m_nNewEntity != ENTITY_SENTINEL) ? GetPackedEntity(u.m_pToSnapshot, u.m_nNewEntity) : NULL;
	PackedEntity* pOldPack = (u.m_nOldEntity != ENTITY_SENTINEL) ? GetPackedEntity(u.m_pFromSnapshot, u.m_nOldEntity) : NULL;

	CFrameSnapshotEntry* entry = GetSnapshotEntry(u.m_pToSnapshot, u.m_nNewEntity);//u.m_pToSnapshot->m_pEntities

	ServerClass* pClass = entry->m_pClass;

	if (!pClass)
	{
		Host_Error("SV_CreatePacketEntities: GetEntServerClass failed for ent %d.\n", u.m_nNewEntity);
		return;
	}

	TRACE_PACKET(("  SV Enter Class %s\n", pClass->m_pNetworkName));

	if (pClass->m_ClassID >= sv.serverclasses)
	{
		ConMsg("pClass->m_ClassID(%i) >= %i\n", pClass->m_ClassID, sv.serverclasses);
		Assert(0);
	}

	u.m_pBuf->WriteUBitLong(pClass->m_ClassID, sv.serverclassbits);

	// Write some of the serial number's bits. 
	u.m_pBuf->WriteUBitLong(entry->m_nSerialNumber, NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS);

	if (u.from_baseline)
	{
		// remember that we sent this entity as full update from entity baseline
		u.from_baseline->Set(u.m_nNewEntity);
	}

	CClientBaselineInfo* pClientBaselineInfo = &m_ClientBaselineInfo.Element(pClient->m_nClientSlot);
	PackedEntity* hBaselinePackedEntity = pClientBaselineInfo->m_pPackedEntities[u.m_nNewEntity];

	// Get the baseline.
	// Since the ent is in the fullpack, then it must have either a static or an instance baseline.
	PackedEntity* pBaseline = u.m_bAsDelta && hBaselinePackedEntity != INVALID_PACKED_ENTITY_HANDLE ? hBaselinePackedEntity : NULL;
	const void* pFromData;
	int nFromBits;

	if (pBaseline && (pBaseline->m_pServerClass == pNewPack->m_pServerClass))
	{
		Assert(!pBaseline->IsCompressed());
		pFromData = pBaseline->GetData();
		nFromBits = pBaseline->GetNumBits();
	}
	else
	{
		// Since the ent is in the fullpack, then it must have either a static or an instance baseline.
		int nFromBytes;
		if (!sv.GetClassBaseline(pClass, &pFromData, &nFromBytes))
		{
			Error("SV_WriteEnterPVS: missing instance baseline for '%s'.", pClass->m_pNetworkName);
		}

		ErrorIfNot(pFromData,
			("SV_WriteEnterPVS: missing pFromData for '%s'.", pClass->m_pNetworkName)
		);

		nFromBits = nFromBytes * 8;	// NOTE: this isn't the EXACT number of bits but that's ok since it's
		// only used to detect if we overran the buffer (and if we do, it's probably
		// by more than 7 bits).
	}

	const void* pToData;
	int nToBits;

	if (pNewPack->IsCompressed())
	{
		pToData = UncompressPackedEntity(pNewPack, nToBits);
	}
	else
	{
		pToData = pNewPack->GetData();
		nToBits = pNewPack->GetNumBits();
	}

	/*if ( server->IsHLTV() || server->IsReplay() )
	{*/
	// send all changed properties when entering PVS (no SendProxy culling since we may use it as baseline
	u.m_nFullProps += SendTable_WriteAllDeltaProps(pClass->m_pTable, pFromData, nFromBits,
		pToData, nToBits, pNewPack->m_nEntityIndex, u.m_pBuf);
	/*}
	else
	{
		// remove all props that are excluded for this client
		u.m_nFullProps += SV_CalcDeltaAndWriteProps( u, pFromData, nFromBits, u.m_pNewPack );
	}*/

}

void PackedEntityManager::CheckClientSnapshotEntryArray(int maxIndex) {
	while (m_ClientBaselineInfo.Count() < maxIndex + 1) {
		m_ClientBaselineInfo.AddToTail();
	}
}

ConVar sv_debugmanualmode("sv_debugmanualmode", "0", 0, "Make sure entities correctly report whether or not their network data has changed.");



void PackedEntityManager::InitPackedEntity(CFrameSnapshot* pSnapshot, int edictIdx, ServerClass* pServerClass, int iSerialNum) {
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

	sv.SetClassBaseline(pServerClass, packedData, writeBuf.GetNumBytesWritten());// edictIdx, 

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
		PackedEntity* pPackedEntity = CreatePackedEntity(pSnapshot, edictIdx, pServerClass, iSerialNum);
		pPackedEntity->SetChangeFrameList(pChangeFrame);
		pPackedEntity->SetServerAndClientClass(pServerClass, NULL);
		pPackedEntity->AllocAndCopyPadded(packedData, writeBuf.GetNumBytesWritten());
		pPackedEntity->SetRecipients(recip);
	}

	pServerEntity->GetNetworkable()->ClearStateChanged();
}