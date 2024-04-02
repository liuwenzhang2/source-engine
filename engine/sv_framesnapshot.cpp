//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "server_pch.h"
#include <utllinkedlist.h>

#include "hltvserver.h"
#if defined( REPLAY_ENABLED )
#include "replayserver.h"
#endif
#include "framesnapshot.h"
#include "sys_dll.h"
#include "LocalNetworkBackdoor.h"
#include "dt_send_eng.h"
#include "dt.h"
#include "tier0/vcrmode.h"
#include "changeframelist.h"
#include "vstdlib/jobthread.h"
#include "sv_main.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

DEFINE_FIXEDSIZE_ALLOCATOR( CFrameSnapshot, 64, 64 );

extern ConVar sv_maxreplay;

void CClientSnapshotInfo::SetupPackInfo(CFrameSnapshot* pSnapshot, CGameClient* clients)
{
	m_pClient = clients;
	// Compute Vis for each client
	m_PackInfo.m_nPVSSize = (GetCollisionBSPData()->numclusters + 7) / 8;
	m_PackInfo.m_pClientEnt = clients->m_nEntityIndex;
	serverGameClients->ClientSetupVisibility(clients->m_pViewEntity,m_PackInfo.m_pClientEnt,
		m_PackInfo.m_PVS,m_PackInfo.m_nPVSSize);

	// This is the frame we are creating, i.e., the next
	// frame after the last one that the client acknowledged

	m_pCurrentFrame = AllocateFrame();
	m_pCurrentFrame->Init(pSnapshot);

	m_PackInfo.m_pTransmitEdict = &m_pCurrentFrame->transmit_entity;

	// if this client is the HLTV or Replay client, add the nocheck PVS bit array
	// normal clients don't need that extra array
#ifndef _XBOX
#if defined( REPLAY_ENABLED )
	if (IsHLTV() || IsReplay())
#else
	if (clients->IsHLTV())
#endif
	{
		// the hltv client doesn't has a ClientFrame list
		m_pCurrentFrame->transmit_always = new CBitVec<MAX_EDICTS>;
		m_PackInfo.m_pTransmitAlways = m_pCurrentFrame->transmit_always;
	}
	else
#endif
	{
		m_PackInfo.m_pTransmitAlways = NULL;
	}

	// Add frame to ClientFrame list 

	int nMaxFrames = MAX_CLIENT_FRAMES;

	if (sv_maxreplay.GetFloat() > 0)
	{
		// if the server has replay features enabled, allow a way bigger frame buffer
		nMaxFrames = max((float)nMaxFrames, sv_maxreplay.GetFloat() / sv.GetTickInterval());
	}

	if (nMaxFrames < AddClientFrame(m_pCurrentFrame))
	{
		// If the client has more than 64 frames, the server will start to eat too much memory.
		RemoveOldestFrame();
	}

	// Since area to area visibility is determined by each player's PVS, copy
	//  the area network lookups into the ClientPackInfo_t
	m_PackInfo.m_AreasNetworked = 0;
	int areaCount = g_AreasNetworked.Count();
	for (int j = 0; j < areaCount; j++)
	{
		m_PackInfo.m_Areas[m_PackInfo.m_AreasNetworked] = g_AreasNetworked[j];
		m_PackInfo.m_AreasNetworked++;

		// Msg("CGameClient::SetupPackInfo: too much areas (%i)", areaCount );
		Assert(m_PackInfo.m_AreasNetworked < MAX_WORLD_AREAS);
	}

	CM_SetupAreaFloodNums(m_PackInfo.m_AreaFloodNums, &m_PackInfo.m_nMapAreas);
}

void CClientSnapshotInfo::SetupPrevPackInfo()
{
	memcpy(&m_PrevTransmitEdict, m_PackInfo.m_pTransmitEdict, sizeof(m_PrevTransmitEdict));

	// Copy the relevant fields into m_PrevPackInfo.
	m_PrevPackInfo.m_AreasNetworked = m_PackInfo.m_AreasNetworked;
	memcpy(m_PrevPackInfo.m_Areas, m_PackInfo.m_Areas, sizeof(m_PackInfo.m_Areas[0]) * m_PackInfo.m_AreasNetworked);

	m_PrevPackInfo.m_nPVSSize = m_PackInfo.m_nPVSSize;
	memcpy(m_PrevPackInfo.m_PVS, m_PackInfo.m_PVS, m_PackInfo.m_nPVSSize);

	m_PrevPackInfo.m_nMapAreas = m_PackInfo.m_nMapAreas;
	memcpy(m_PrevPackInfo.m_AreaFloodNums, m_PackInfo.m_AreaFloodNums, m_PackInfo.m_nMapAreas * sizeof(m_PackInfo.m_nMapAreas));
}

CClientFrame* CClientSnapshotInfo::GetDeltaFrame(int nTick)
{
#ifndef _XBOX
	Assert(!IsHLTV()); // has no ClientFrames
#if defined( REPLAY_ENABLED )
	Assert(!IsReplay());  // has no ClientFrames
#endif
#endif	

	if (m_pClient->m_bIsInReplayMode)
	{
		int followEntity;

		serverGameClients->GetReplayDelay(m_pClient->m_nEntityIndex, followEntity);

		Assert(followEntity > 0);

		CGameClient* pFollowEntity = sv.Client(followEntity - 1);

		if (pFollowEntity) {

			CClientSnapshotInfo* pFollowerClientSnapshotInfo = framesnapshotmanager->GetClientSnapshotInfo(pFollowEntity);
			return pFollowerClientSnapshotInfo->GetClientFrame(nTick);
		}
	}

	return GetClientFrame(nTick);
}

CClientFrame* CClientSnapshotInfo::GetSendFrame()
{
	CClientFrame* pFrame = m_pCurrentFrame;

	// just return if replay is disabled
	if (sv_maxreplay.GetFloat() <= 0)
		return pFrame;

	int followEntity;

	int delayTicks = serverGameClients->GetReplayDelay(m_PackInfo.m_pClientEnt, followEntity);

	bool isInReplayMode = (delayTicks > 0);

	if (isInReplayMode != m_pClient->m_bIsInReplayMode)
	{
		// force a full update when modes are switched
		m_pClient->m_nDeltaTick = -1;

		m_pClient->m_bIsInReplayMode = isInReplayMode;

		if (isInReplayMode)
		{
			m_pClient->m_nEntityIndex = followEntity;
		}
		else
		{
			m_pClient->m_nEntityIndex = m_pClient->m_nClientSlot + 1;
		}
	}

	Assert((m_pClient->m_nClientSlot + 1 == m_pClient->m_nEntityIndex) || isInReplayMode);

	if (isInReplayMode)
	{
		CGameClient* pFollowPlayer = sv.Client(followEntity - 1);

		if (!pFollowPlayer)
			return NULL;

		CClientSnapshotInfo* pFollowerClientSnapshotInfo = framesnapshotmanager->GetClientSnapshotInfo(pFollowPlayer);
		pFrame = pFollowerClientSnapshotInfo->GetClientFrame(sv.GetTick() - delayTicks, false);

		if (!pFrame)
			return NULL;

		CClientSnapshotInfo* pClientSnapshotInfo = framesnapshotmanager->GetClientSnapshotInfo(m_pClient);
		if (pClientSnapshotInfo->m_pLastSnapshot == pFrame->GetSnapshot())
			return NULL;
	}

	return pFrame;
}

// Expose interface
static CFrameSnapshotManager g_FrameSnapshotManager;
CFrameSnapshotManager *framesnapshotmanager = &g_FrameSnapshotManager;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CFrameSnapshotManager::CFrameSnapshotManager( void ) 
{
	COMPILE_TIME_ASSERT( INVALID_PACKED_ENTITY_HANDLE == 0 );

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CFrameSnapshotManager::~CFrameSnapshotManager( void )
{
	AssertMsg1( m_FrameSnapshots.Count() == 0 || IsInErrorExit(), "Expected m_FrameSnapshots to be empty. It had %i items.", m_FrameSnapshots.Count() );
	m_ClientSnapshotInfo.PurgeAndDeleteElements();
}

//-----------------------------------------------------------------------------
// Called when a level change happens
//-----------------------------------------------------------------------------

void CFrameSnapshotManager::LevelChanged()
{
	// Clear all lists...
	Assert( m_FrameSnapshots.Count() == 0 );

	// Release the most recent snapshot...
	g_pPackedEntityManager->OnLevelChanged();
	COMPILE_TIME_ASSERT( INVALID_PACKED_ENTITY_HANDLE == 0 );
}

CFrameSnapshot*	CFrameSnapshotManager::NextSnapshot( const CFrameSnapshot *pSnapshot )
{
	if ( !pSnapshot || ((unsigned short)pSnapshot->m_ListIndex == m_FrameSnapshots.InvalidIndex()) )
		return NULL;

	int next = m_FrameSnapshots.Next(pSnapshot->m_ListIndex);

	if ( next == m_FrameSnapshots.InvalidIndex() )
		return NULL;

	// return next element in list
	return m_FrameSnapshots[ next ];
}

static int lastSnapIndex = -1;
CFrameSnapshot*	CFrameSnapshotManager::CreateEmptySnapshot( int tickcount, int maxEntities )
{
	CFrameSnapshot *snap = new CFrameSnapshot;
	snap->AddReference();
	snap->m_nTickCount = tickcount;
	snap->m_nNumEntities = maxEntities;
	snap->m_nValidEntities = 0;
	snap->m_pValidEntities = NULL;
	snap->m_pHLTVEntityData = NULL;
	snap->m_pReplayEntityData = NULL;
	//snap->m_pEntities = new CFrameSnapshotEntry[maxEntities];
	snap->m_ListIndex = m_FrameSnapshots.AddToTail( snap );
	if (snap->m_ListIndex < lastSnapIndex) {
		int aaa = 0;
	}
	lastSnapIndex = snap->m_ListIndex;
	if (snap->m_ListIndex < 0 || snap->m_ListIndex >= MAX_CLIENT_FRAMES) {
		Error("snap->m_ListIndex overflow!");
	}
	if (snap->m_ListIndex == 0) {
		int aaa = 0;
	}

	g_pPackedEntityManager->OnCreateSnapshot(snap);

	return snap;
}

void PackEntities_NetworkBackDoor(
	int clientCount,
	CGameClient** clients,
	CFrameSnapshot* snapshot)
{
	Assert(clientCount == 1);

	VPROF_BUDGET("PackEntities_NetworkBackDoor", VPROF_BUDGETGROUP_OTHER_NETWORKING);

	CGameClient* client = clients[0];	// update variables cl, pInfo, frame for current client
	CClientSnapshotInfo* pClientSnapshotInfo = framesnapshotmanager->GetClientSnapshotInfo(client);
	CCheckTransmitInfo* pInfo = &pClientSnapshotInfo->m_PackInfo;

	for (int iValidEdict = 0; iValidEdict < snapshot->m_nValidEntities; iValidEdict++)
	{
		int index = snapshot->m_pValidEntities[iValidEdict];
		//edict_t* edict = &sv.edicts[ index ];
		IServerEntity* pServerEntity = serverEntitylist->GetServerEntity(index);

		// this is a bit of a hack to ensure that we get a "preview" of the
		//  packet timstamp that the server will send so that things that
		//  are encoded relative to packet time will be correct
		Assert(serverEntitylist->GetNetworkSerialNumber(index) != -1);

		bool bShouldTransmit = pInfo->m_pTransmitEdict->Get(index) ? true : false;

		//CServerDTITimer timer( pSendTable, SERVERDTI_ENCODE );
		// If we're using the fast path for a single-player game, just pass the entity
		// directly over to the client.
		Assert(index < snapshot->m_nNumEntities);
		//ServerClass *pSVClass = snapshot->m_pEntities[ index ].m_pClass;
		g_pLocalNetworkBackdoor->EntState(pServerEntity, //serverEntitylist->GetNetworkSerialNumber(index),
			pServerEntity->GetNetworkable()->HasStateChanged(), bShouldTransmit);//pSVClass->m_pTable, pServerEntity, pSVClass->m_ClassID,
		pServerEntity->GetNetworkable()->ClearStateChanged();
	}

	// Tell the client about any entities that are now dormant.
	g_pLocalNetworkBackdoor->ProcessDormantEntities();
	InvalidateSharedEdictChangeInfos();
}

ConVar sv_debugmanualmode("sv_debugmanualmode", "0", 0, "Make sure entities correctly report whether or not their network data has changed.");

// This function makes sure that this entity class has an instance baseline.
// If it doesn't have one yet, it makes a new one.
void SV_EnsureInstanceBaseline(ServerClass* pServerClass, int iEdict, const void* pData, int nBytes)
{
	IServerEntity* pEnt = serverEntitylist->GetServerEntity(iEdict);
	ErrorIfNot(pEnt->GetNetworkable(),
		("SV_EnsureInstanceBaseline: edict %d missing ent", iEdict)
	);

	ServerClass* pClass = pEnt->GetServerClass();

	// See if we already have a baseline for this class.
	if (pClass->m_InstanceBaselineIndex == INVALID_STRING_INDEX)
	{
		AUTO_LOCK(g_svInstanceBaselineMutex);

		// We need this second check in case multiple instances of the same class have grabbed the lock.
		if (pClass->m_InstanceBaselineIndex == INVALID_STRING_INDEX)
		{
			char idString[32];
			Q_snprintf(idString, sizeof(idString), "%d", pClass->m_ClassID);

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
			pClass->m_InstanceBaselineIndex = temp;
			Assert(pClass->m_InstanceBaselineIndex != INVALID_STRING_INDEX);
		}
	}
	// Read-acquire barrier
	ThreadMemoryBarrier();
}

//-----------------------------------------------------------------------------
// Pack the entity....
//-----------------------------------------------------------------------------

static inline void SV_PackEntity(
	int edictIdx,
	IServerEntity* edict,
	ServerClass* pServerClass,
	CFrameSnapshot* pSnapshot)
{
	Assert(edictIdx < pSnapshot->m_nNumEntities);
	tmZoneFiltered(TELEMETRY_LEVEL0, 50, TMZF_NONE, "PackEntities_Normal%s", __FUNCTION__);

	int iSerialNum = g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, edictIdx)->m_nSerialNumber;

	// Check to see if this entity specifies its changes.
	// If so, then try to early out making the fullpack
	bool bUsedPrev = false;

	if (!edict->GetNetworkable()->HasStateChanged())
	{
		// Now this may not work if we didn't previously send a packet;
		// if not, then we gotta compute it
		bUsedPrev = g_pPackedEntityManager->UsePreviouslySentPacket(pSnapshot, edictIdx, iSerialNum);
	}

	if (bUsedPrev && !sv_debugmanualmode.GetInt())
	{
		edict->GetNetworkable()->ClearStateChanged();
		return;
	}

	// First encode the entity's data.
	ALIGN4 char packedData[MAX_PACKEDENTITY_DATA] ALIGN4_POST;
	bf_write writeBuf("SV_PackEntity->writeBuf", packedData, sizeof(packedData));

	SendTable* pSendTable = pServerClass->m_pTable;

	// (avoid constructor overhead).
	unsigned char tempData[sizeof(CSendProxyRecipients) * MAX_DATATABLE_PROXIES];
	CUtlMemory< CSendProxyRecipients > recip((CSendProxyRecipients*)tempData, pSendTable->m_pPrecalc->GetNumDataTableProxies());

	if (!SendTable_Encode(pSendTable, edict, &writeBuf, edictIdx, &recip, false))
	{
		Host_Error("SV_PackEntity: SendTable_Encode returned false (ent %d).\n", edictIdx);
	}

#ifndef NO_VCR
	// VCR mode stuff..
	if (vcr_verbose.GetInt() && writeBuf.GetNumBytesWritten() > 0)
		VCRGenericValueVerify("writebuf", writeBuf.GetBasePointer(), writeBuf.GetNumBytesWritten() - 1);
#endif

	SV_EnsureInstanceBaseline(pServerClass, edictIdx, packedData, writeBuf.GetNumBytesWritten());

	int nFlatProps = SendTable_GetNumFlatProps(pSendTable);
	IChangeFrameList* pChangeFrame = NULL;

	// If this entity was previously in there, then it should have a valid IChangeFrameList 
	// which we can delta against to figure out which properties have changed.
	//
	// If not, then we want to setup a new IChangeFrameList.

	PackedEntity* pPrevFrame = g_pPackedEntityManager->GetPreviouslySentPacket(edictIdx, g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, edictIdx)->m_nSerialNumber);
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
				if (g_pPackedEntityManager->UsePreviouslySentPacket(pSnapshot, edictIdx, iSerialNum))
				{
					edict->GetNetworkable()->ClearStateChanged();
					return;
				}
			}
		}
		else
		{
			if (!edict->GetNetworkable()->HasStateChanged())
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
						edict->GetClassName(),
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
	{
		PackedEntity* pPackedEntity = g_pPackedEntityManager->CreatePackedEntity(pSnapshot, edictIdx);
		pPackedEntity->SetChangeFrameList(pChangeFrame);
		pPackedEntity->SetServerAndClientClass(pServerClass, NULL);
		pPackedEntity->AllocAndCopyPadded(packedData, writeBuf.GetNumBytesWritten());
		pPackedEntity->SetRecipients(recip);
	}

	edict->GetNetworkable()->ClearStateChanged();
}

// in HLTV mode we ALWAYS have to store position and PVS info, even if entity didnt change
void SV_FillHLTVData(CFrameSnapshot* pSnapshot, IServerEntity* edict, int iValidEdict)
{
#if !defined( _XBOX )
	if (pSnapshot->m_pHLTVEntityData && edict)
	{
		CHLTVEntityData* pHLTVData = &pSnapshot->m_pHLTVEntityData[iValidEdict];

		PVSInfo_t* pvsInfo = edict->GetPVSInfo();

		if (pvsInfo->m_nClusterCount == 1)
		{
			// store cluster, if entity spawns only over one cluster
			pHLTVData->m_nNodeCluster = pvsInfo->m_pClusters[0];
		}
		else
		{
			// otherwise save PVS head node for larger entities
			pHLTVData->m_nNodeCluster = pvsInfo->m_nHeadNode | (1 << 31);
		}

		// remember origin
		pHLTVData->origin[0] = pvsInfo->m_vCenter[0];
		pHLTVData->origin[1] = pvsInfo->m_vCenter[1];
		pHLTVData->origin[2] = pvsInfo->m_vCenter[2];
	}
#endif
}

// in Replay mode we ALWAYS have to store position and PVS info, even if entity didnt change
void SV_FillReplayData(CFrameSnapshot* pSnapshot, IServerEntity* edict, int iValidEdict)
{
#if !defined( _XBOX )
	if (pSnapshot->m_pReplayEntityData && edict)
	{
		CReplayEntityData* pReplayData = &pSnapshot->m_pReplayEntityData[iValidEdict];

		PVSInfo_t* pvsInfo = edict->GetPVSInfo();

		if (pvsInfo->m_nClusterCount == 1)
		{
			// store cluster, if entity spawns only over one cluster
			pReplayData->m_nNodeCluster = pvsInfo->m_pClusters[0];
		}
		else
		{
			// otherwise save PVS head node for larger entities
			pReplayData->m_nNodeCluster = pvsInfo->m_nHeadNode | (1 << 31);
		}

		// remember origin
		pReplayData->origin[0] = pvsInfo->m_vCenter[0];
		pReplayData->origin[1] = pvsInfo->m_vCenter[1];
		pReplayData->origin[2] = pvsInfo->m_vCenter[2];
	}
#endif
}

static ConVar sv_parallel_packentities("sv_parallel_packentities", "1");

struct PackWork_t
{
	int				nIdx;
	IServerEntity* pEdict;
	CFrameSnapshot* pSnapshot;

	static void Process(PackWork_t& item)
	{
		SV_PackEntity(item.nIdx, item.pEdict, g_pPackedEntityManager->GetSnapshotEntry(item.pSnapshot, item.nIdx)->m_pClass, item.pSnapshot);//item.pSnapshot->m_pEntities
	}
};

void PackEntities_Normal(
	int clientCount,
	CGameClient** clients,
	CFrameSnapshot* snapshot)
{
	Assert(snapshot->m_nValidEntities >= 0 && snapshot->m_nValidEntities <= MAX_EDICTS);
	tmZoneFiltered(TELEMETRY_LEVEL0, 50, TMZF_NONE, "%s %d", __FUNCTION__, snapshot->m_nValidEntities);

	CUtlVectorFixed< PackWork_t, MAX_EDICTS > workItems;

	// check for all active entities, if they are seen by at least on client, if
	// so, bit pack them 
	for (int iValidEdict = 0; iValidEdict < snapshot->m_nValidEntities; ++iValidEdict)
	{
		int index = snapshot->m_pValidEntities[iValidEdict];

		Assert(index < snapshot->m_nNumEntities);

		//edict_t* edict = &sv.edicts[ index ];
		IServerEntity* pServerEntity = serverEntitylist->GetServerEntity(index);

		// if HLTV is running save PVS info for each entity
		SV_FillHLTVData(snapshot, pServerEntity, iValidEdict);

		// if Replay is running save PVS info for each entity
		SV_FillReplayData(snapshot, pServerEntity, iValidEdict);

		// Check to see if the entity changed this frame...
		//ServerDTI_RegisterNetworkStateChange( pSendTable, ent->m_bStateChanged );

		for (int iClient = 0; iClient < clientCount; ++iClient)
		{
			// entities is seen by at least this client, pack it and exit loop
			CGameClient* client = clients[iClient];	// update variables cl, pInfo, frame for current client
			CClientSnapshotInfo* pClientSnapshotInfo = framesnapshotmanager->GetClientSnapshotInfo(client);
			CClientFrame* frame = pClientSnapshotInfo->m_pCurrentFrame;

			if (frame->transmit_entity.Get(index))
			{
				PackWork_t w;
				w.nIdx = index;
				w.pEdict = pServerEntity;
				w.pSnapshot = snapshot;

				workItems.AddToTail(w);
				break;
			}
		}
	}

	// Process work
	if (sv_parallel_packentities.GetBool())
	{
		ParallelProcess("PackWork_t::Process", workItems.Base(), workItems.Count(), &PackWork_t::Process);
	}
	else
	{
		int c = workItems.Count();
		for (int i = 0; i < c; ++i)
		{
			PackWork_t& w = workItems[i];
			SV_PackEntity(w.nIdx, w.pEdict, g_pPackedEntityManager->GetSnapshotEntry(w.pSnapshot, w.nIdx)->m_pClass, w.pSnapshot);//w.pSnapshot->m_pEntities
		}
	}

	InvalidateSharedEdictChangeInfos();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : framenumber - 
//-----------------------------------------------------------------------------
CFrameSnapshot* CFrameSnapshotManager::TakeTickSnapshot(int clientCount, CGameClient** clients, int tickcount )
{
	unsigned short nValidEntities[MAX_EDICTS];

	CFrameSnapshot *snap = CreateEmptySnapshot( tickcount, serverEntitylist->IndexOfHighestEdict()+1 );
	
	int maxclients = sv.GetClientCount();

	CFrameSnapshotEntry* entry = g_pPackedEntityManager->GetSnapshotEntry(snap, 0) - 1;
	//edict_t *edict= sv.edicts - 1;
	
	// Build the snapshot.
	for ( int i = 0; i <= serverEntitylist->IndexOfHighestEdict(); i++ )
	{
		//edict++;
		entry++;

		IServerUnknown *pUnk = serverEntitylist->GetServerEntity(i);

		if ( !pUnk )
			continue;
																		  
		//if ( edict->IsFree() )
		//	continue;
		
		// We don't want entities from inactive clients in the fullpack,
		if ( i > 0 && i <= maxclients )
		{
			// this edict is a client
			if ( !sv.GetClient(i-1)->IsActive() )
				continue;
		}
		
		// entity exists and is not marked as 'free'
		Assert( serverEntitylist->GetNetworkSerialNumber(i) != -1 );
		Assert( serverEntitylist->GetServerNetworkable(i));
		Assert( serverEntitylist->GetServerEntity(i)->GetServerClass() );

		entry->m_nSerialNumber	= serverEntitylist->GetNetworkSerialNumber(i);
		entry->m_pClass			= serverEntitylist->GetServerEntity(i)->GetServerClass();
		if(!entry->m_pClass){
			int aaa = 0;
		}
		nValidEntities[snap->m_nValidEntities++] = i;
	}

	// create dynamic valid entities array and copy indices
	snap->m_pValidEntities = new unsigned short[snap->m_nValidEntities];
	Q_memcpy( snap->m_pValidEntities, nValidEntities, snap->m_nValidEntities * sizeof(unsigned short) );

	if ( hltv && hltv->IsActive() )
	{
		snap->m_pHLTVEntityData = new CHLTVEntityData[snap->m_nValidEntities];
		Q_memset( snap->m_pHLTVEntityData, 0, snap->m_nValidEntities * sizeof(CHLTVEntityData) );
	}

#if defined( REPLAY_ENABLED )
	if ( replay && replay->IsActive() )
	{
		snap->m_pReplayEntityData = new CReplayEntityData[snap->m_nValidEntities];
		Q_memset( snap->m_pReplayEntityData, 0, snap->m_nValidEntities * sizeof(CReplayEntityData) );
	}
#endif

	snap->m_iExplicitDeleteSlots.CopyArray( m_iExplicitDeleteSlots.Base(), m_iExplicitDeleteSlots.Count() );
	m_iExplicitDeleteSlots.Purge();

	tmZone(TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__);

	MDLCACHE_CRITICAL_SECTION_(g_pMDLCache);
	// Do some setup for each client
	{
		VPROF_BUDGET_FLAGS("SV_ComputeClientPacks", "CheckTransmit", BUDGETFLAG_SERVER);

		for (int iClient = 0; iClient < clientCount; ++iClient)
		{
			CClientSnapshotInfo* pClientSnapshotInfo = GetClientSnapshotInfo(clients[iClient]);
			pClientSnapshotInfo->SetupPackInfo(snap, clients[iClient]);
			CCheckTransmitInfo* pInfo = &pClientSnapshotInfo->m_PackInfo;
			serverGameEnts->CheckTransmit(pInfo, snap->m_pValidEntities, snap->m_nValidEntities);
			pClientSnapshotInfo->SetupPrevPackInfo();
		}
	}

	VPROF_BUDGET_FLAGS("SV_ComputeClientPacks", "ComputeClientPacks", BUDGETFLAG_SERVER);

	if (g_pLocalNetworkBackdoor)
	{
		// This will force network string table updates for local client to go through the backdoor if it's active
#ifdef SHARED_NET_STRING_TABLES
		sv.m_StringTables->TriggerCallbacks(clients[0]->m_nDeltaTick);
#else
		sv.m_StringTables->DirectUpdate(clients[0]->GetMaxAckTickCount());
#endif

		g_pLocalNetworkBackdoor->StartEntityStateUpdate();

#ifndef SWDS
		int saveClientTicks = cl.GetClientTickCount();
		int saveServerTicks = cl.GetServerTickCount();
		bool bSaveSimulation = cl.insimulation;
		float flSaveLastServerTickTime = cl.m_flLastServerTickTime;

		cl.insimulation = true;
		cl.SetClientTickCount(sv.m_nTickCount);
		cl.SetServerTickCount(sv.m_nTickCount);

		cl.m_flLastServerTickTime = sv.m_nTickCount * host_state.interval_per_tick;
		g_ClientGlobalVariables.tickcount = cl.GetClientTickCount();
		g_ClientGlobalVariables.curtime = cl.GetTime();
#endif

		PackEntities_NetworkBackDoor(clientCount, clients, snap);

		g_pLocalNetworkBackdoor->EndEntityStateUpdate();

#ifndef SWDS
		cl.SetClientTickCount(saveClientTicks);
		cl.SetServerTickCount(saveServerTicks);
		cl.insimulation = bSaveSimulation;
		cl.m_flLastServerTickTime = flSaveLastServerTickTime;

		g_ClientGlobalVariables.tickcount = cl.GetClientTickCount();
		g_ClientGlobalVariables.curtime = cl.GetTime();
#endif

		PrintPartialChangeEntsList();
	}
	else
	{
		PackEntities_Normal(clientCount, clients, snap);
	}

	return snap;
}

//-----------------------------------------------------------------------------
// Cleans up packed entity data
//-----------------------------------------------------------------------------

void CFrameSnapshotManager::DeleteFrameSnapshot( CFrameSnapshot* pSnapshot )
{
	if (pSnapshot->m_ListIndex == 0) {
		int aaa = 0;
	}
	
	g_pPackedEntityManager->OnDeleteSnapshot(pSnapshot);

	m_FrameSnapshots.Remove( pSnapshot->m_ListIndex );
	delete pSnapshot;
}



void CFrameSnapshotManager::AddExplicitDelete( int iSlot )
{
	AUTO_LOCK( m_WriteMutex );

	if ( m_iExplicitDeleteSlots.Find(iSlot) == m_iExplicitDeleteSlots.InvalidIndex() )
	{
		m_iExplicitDeleteSlots.AddToTail( iSlot );
	}
}

void	CFrameSnapshotManager::CheckClientSnapshotArray(int maxIndex) {
	while (m_ClientSnapshotInfo.Count() < maxIndex + 1) {
		m_ClientSnapshotInfo.AddToTail(new CClientSnapshotInfo());
	}
}

void CFrameSnapshotManager::FreeClientBaselines(CBaseClient* pClient)
{
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotInfo->m_pLastSnapshot = NULL;
	pClientSnapshotInfo->m_nBaselineUpdateTick = -1;
	pClientSnapshotInfo->m_nBaselineUsed = 0;
	g_pPackedEntityManager->FreeClientBaselines(pClient);
}

void CFrameSnapshotManager::AllocClientBaselines(CBaseClient* pClient) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	g_pPackedEntityManager->AllocClientBaselines(pClient);
}

void CFrameSnapshotManager::OnClientConnected(CBaseClient* pClient) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotInfo->m_pLastSnapshot = NULL;
	pClientSnapshotInfo->m_nBaselineUpdateTick = -1;
	pClientSnapshotInfo->m_nBaselineUsed = 0;
	pClientSnapshotInfo->DeleteClientFrames(-1);
	g_pPackedEntityManager->OnClientConnected(pClient);
}

void CFrameSnapshotManager::OnClientInactivate(CBaseClient* pClient) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotInfo->DeleteClientFrames(-1); // delete all
}

CClientSnapshotInfo* CFrameSnapshotManager::GetClientSnapshotInfo(CBaseClient* pClient) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	return m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
}

void	CFrameSnapshotManager::UpdateAcknowledgedFramecount(CBaseClient* pClient, int tick) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	if ((pClientSnapshotInfo->m_nBaselineUpdateTick > -1) && (tick > pClientSnapshotInfo->m_nBaselineUpdateTick))
	{
		// server sent a baseline update, but it wasn't acknowledged yet so it was probably lost. 
		pClientSnapshotInfo->m_nBaselineUpdateTick = -1;
	}
	// delta tick changed, free all frames smaller than tick
	int removeTick = tick;

	if (sv_maxreplay.GetFloat() > 0)
		removeTick -= (sv_maxreplay.GetFloat() / sv.GetTickInterval()); // keep a replay buffer

	if (removeTick > 0)
	{
		pClientSnapshotInfo->DeleteClientFrames(removeTick);
	}
}

bool	CFrameSnapshotManager::ProcessBaselineAck(CBaseClient* pClient, int nBaselineTick, int	nBaselineNr) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	
	if (nBaselineTick != pClientSnapshotInfo->m_nBaselineUpdateTick)
	{
		// This occurs when there are multiple ack's queued up for processing from a client.
		return true;
	}

	if (nBaselineNr != pClientSnapshotInfo->m_nBaselineUsed)
	{
		DevMsg("CBaseClient::ProcessBaselineAck: wrong baseline nr received (%i)\n", nBaselineTick);
		return true;
	}

	//Assert(pClientSnapshotInfo->m_pBaselineEntities);

	// copy ents send as full updates this frame into baseline stuff
	CClientFrame* frame = pClient->GetDeltaFrame(pClientSnapshotInfo->m_nBaselineUpdateTick);
	if (frame == NULL)
	{
		// Will get here if we have a lot of packet loss and finally receive a stale ack from 
		//  remote client.  Our "window" could be well beyond what it's acking, so just ignore the ack.
		return true;
	}

	CFrameSnapshot* pSnapshot = frame->GetSnapshot();

	if (pSnapshot == NULL)
	{
		// TODO if client lags for a couple of seconds the snapshot is lost
		// fix: don't remove snapshots that are labled a possible basline candidates
		// or: send full update
		DevMsg("CBaseClient::ProcessBaselineAck: invalid frame snapshot (%i)\n", pClientSnapshotInfo->m_nBaselineUpdateTick);
		return false;
	}

	g_pPackedEntityManager->ProcessBaselineAck(pClient, pSnapshot);

	//m_pBaseline->m_nTickCount = m_nBaselineUpdateTick;

	// flip used baseline flag
	pClientSnapshotInfo->m_nBaselineUsed = (pClientSnapshotInfo->m_nBaselineUsed == 1) ? 0 : 1;

	pClientSnapshotInfo->m_nBaselineUpdateTick = -1; // ready to update baselines again

	return true;
}

CFrameSnapshot* CFrameSnapshotManager::GetClientLastSnapshot(CBaseClient* pClient) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	return pClientSnapshotInfo->m_pLastSnapshot.GetObject();
}

void CFrameSnapshotManager::SetClientLastSnapshot(CBaseClient* pClient, CFrameSnapshot* pSnapshot) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotInfo->m_pLastSnapshot = pSnapshot;
}
//CThreadFastMutex &CFrameSnapshotManager::GetMutex()
//{
//	return m_WriteMutex;
//}









// ------------------------------------------------------------------------------------------------ //
// CFrameSnapshot
// ------------------------------------------------------------------------------------------------ //

#if defined( _DEBUG )
	int g_nAllocatedSnapshots = 0;
#endif


CFrameSnapshot::CFrameSnapshot()
{
	m_nTempEntities = 0;
	m_pTempEntities = NULL;
	m_pValidEntities = NULL;
	m_nReferences = 0;
#if defined( _DEBUG )
	++g_nAllocatedSnapshots;
	Assert( g_nAllocatedSnapshots < 80000 ); // this probably would indicate a memory leak.
#endif
}


CFrameSnapshot::~CFrameSnapshot()
{
	delete [] m_pValidEntities;
	//delete [] m_pEntities;

	if ( m_pTempEntities )
	{
		Assert( m_nTempEntities>0 );
		for (int i = 0; i < m_nTempEntities; i++ )
		{
			delete m_pTempEntities[i];
		}

		delete [] m_pTempEntities;
	}

	if ( m_pHLTVEntityData )
	{
		delete [] m_pHLTVEntityData;
	}

	if ( m_pReplayEntityData )
	{
		delete [] m_pReplayEntityData;
	}
	Assert ( m_nReferences == 0 );

#if defined( _DEBUG )
	--g_nAllocatedSnapshots;
	Assert( g_nAllocatedSnapshots >= 0 );
#endif
}


void CFrameSnapshot::AddReference()
{
	Assert( m_nReferences < 0xFFFF );
	++m_nReferences;
}

void CFrameSnapshot::ReleaseReference()
{
	Assert( m_nReferences > 0 );

	--m_nReferences;
	if ( m_nReferences == 0 )
	{
		g_FrameSnapshotManager.DeleteFrameSnapshot( this );
	}
}

CFrameSnapshot* CFrameSnapshot::NextSnapshot() const
{
	return g_FrameSnapshotManager.NextSnapshot( this );
}


