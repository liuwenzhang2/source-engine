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
#include "tier0/etwprof.h"
#include "net_synctags.h"
#include "dt_instrumentation_server.h"

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
	Assert(!m_pClient->IsHLTV()); // has no ClientFrames
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

			CClientSnapshotInfo* pFollowerClientSnapshotInfo = m_pFrameSnapshotManager->GetClientSnapshotInfo(pFollowEntity);
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

		CClientSnapshotInfo* pFollowerClientSnapshotInfo = m_pFrameSnapshotManager->GetClientSnapshotInfo(pFollowPlayer);
		pFrame = pFollowerClientSnapshotInfo->GetClientFrame(sv.GetTick() - delayTicks, false);

		if (!pFrame)
			return NULL;

		CClientSnapshotInfo* pClientSnapshotInfo = m_pFrameSnapshotManager->GetClientSnapshotInfo(m_pClient);
		if (pClientSnapshotInfo->m_pLastSnapshot == pFrame->GetSnapshot())
			return NULL;
	}

	return pFrame;
}

// Expose interface
static CFrameSnapshotManager s_FrameSnapshotManager;
CFrameSnapshotManager *framesnapshotmanager = &s_FrameSnapshotManager;

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

CFrameSnapshot*	CFrameSnapshotManager::CreateEmptySnapshot( int tickcount, int maxEntities )
{
	CFrameSnapshot *snap = new CFrameSnapshot(this);
	snap->AddReference();
	snap->m_nTickCount = tickcount;
	snap->m_nNumEntities = maxEntities;
	snap->m_nValidEntities = 0;
	snap->m_pValidEntities = NULL;
	snap->m_pHLTVEntityData = NULL;
	snap->m_pReplayEntityData = NULL;
	//snap->m_pEntities = new CFrameSnapshotEntry[maxEntities];
	snap->m_ListIndex = m_FrameSnapshots.AddToTail( snap );
	
	if (snap->m_ListIndex < 0 || snap->m_ListIndex >= MAX_CLIENT_FRAMES) {
		Error("snap->m_ListIndex overflow!");
	}

	g_pPackedEntityManager->OnCreateSnapshot(snap);

	return snap;
}

void CFrameSnapshotManager::PackEntities_NetworkBackDoor(
	int clientCount,
	CGameClient** clients,
	CFrameSnapshot* snapshot)
{
	Assert(clientCount == 1);

	VPROF_BUDGET("PackEntities_NetworkBackDoor", VPROF_BUDGETGROUP_OTHER_NETWORKING);

	CGameClient* client = clients[0];	// update variables cl, pInfo, frame for current client
	CClientSnapshotInfo* pClientSnapshotInfo = GetClientSnapshotInfo(client);
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
	//int				nIdx;
	IServerEntity* pEdict;
	CFrameSnapshot* pSnapshot;

	static void Process(PackWork_t& item)
	{
		g_pPackedEntityManager->DoPackEntity(item.pSnapshot,item.pEdict);//item.pSnapshot->m_pEntities item.nIdx, g_pPackedEntityManager->GetSnapshotEntry(item.pSnapshot, item.nIdx)->m_pClass,
	}
};

void CFrameSnapshotManager::PackEntities_Normal(
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
			CClientSnapshotInfo* pClientSnapshotInfo = GetClientSnapshotInfo(client);
			CClientFrame* frame = pClientSnapshotInfo->m_pCurrentFrame;

			if (frame->transmit_entity.Get(index))
			{
				PackWork_t w;
				//w.nIdx = index;
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
			g_pPackedEntityManager->DoPackEntity(w.pSnapshot, w.pEdict);//w.pSnapshot->m_pEntities g_pPackedEntityManager->GetSnapshotEntry(w.pSnapshot, w.nIdx)->m_pClass, 
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

	//CFrameSnapshotEntry* entry = g_pPackedEntityManager->GetSnapshotEntry(snap, 0) - 1;
	//edict_t *edict= sv.edicts - 1;
	
	// Build the snapshot.
	for ( int i = 0; i <= serverEntitylist->IndexOfHighestEdict(); i++ )
	{
		//edict++;
		//entry++;

		IServerEntity *pServerEntity = serverEntitylist->GetServerEntity(i);

		if ( !pServerEntity)
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
		g_pPackedEntityManager->InitPackedEntity(snap, pServerEntity);

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
		m_ClientSnapshotInfo.AddToTail(new CClientSnapshotInfo(this));
	}
}

void CFrameSnapshotManager::FreeClientBaselines(CBaseClient* pClient)
{
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotInfo->m_pLastSnapshot = NULL;
	pClientSnapshotInfo->m_nBaselineUpdateTick = -1;
	pClientSnapshotInfo->m_nBaselineUsed = 0;
	pClientSnapshotInfo->m_BaselinesSent.ClearAll();
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
	pClientSnapshotInfo->m_BaselinesSent.ClearAll();
	pClientSnapshotInfo->DeleteClientFrames(-1);
	g_pPackedEntityManager->OnClientConnected(pClient);
}

void CFrameSnapshotManager::OnClientInactivate(CBaseClient* pClient) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotInfo->DeleteClientFrames(-1); // delete all
}



//-----------------------------------------------------------------------------
// Purpose: See if the entity needs a "hard" reset ( i.e., and explicit creation tag )
//  This should only occur if the entity slot deleted and re-created an entity faster than
//  the last two updates toa  player.  Should never or almost never occur.  You never know though.
// Input  : type - 
//			entnum - 
//			*from - 
//			*to - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CFrameSnapshotManager::NeedsExplicitCreate(CEntityWriteInfo& u)
{
	// Never on uncompressed packet
	if (!u.m_bAsDelta)
	{
		return false;
	}

	const int index = u.m_nNewEntity;

	if (index >= u.m_pFromSnapshot->m_nNumEntities)
		return true; // entity didn't exist in old frame, so create

	// Server thinks the entity was continues, but the serial # changed, so we might need to destroy and recreate it
	const CFrameSnapshotEntry* pFromEnt = g_pPackedEntityManager->GetSnapshotEntry(u.m_pFromSnapshot, index);//u.m_pFromSnapshot->m_pEntities
	const CFrameSnapshotEntry* pToEnt = g_pPackedEntityManager->GetSnapshotEntry(u.m_pToSnapshot, index);//u.m_pToSnapshot->m_pEntities

	bool bNeedsExplicitCreate = (pFromEnt->m_pClass == NULL) || pFromEnt->m_nSerialNumber != pToEnt->m_nSerialNumber;

#ifdef _DEBUG
	if (!bNeedsExplicitCreate)
	{
		// If it doesn't need explicit create, then the classnames should match.
		// This assert is analagous to the "Server / Client mismatch" one on the client.
		static int nWhines = 0;
		if (pFromEnt->m_pClass->GetName() != pToEnt->m_pClass->GetName())
		{
			if (++nWhines < 4)
			{
				Msg("ERROR in SV_NeedsExplicitCreate: ent %d from/to classname (%s/%s) mismatch.\n", u.m_nNewEntity, pFromEnt->m_pClass->GetName(), pToEnt->m_pClass->GetName());
			}
		}
	}
#endif

	return bNeedsExplicitCreate;
}

//-----------------------------------------------------------------------------
// Purpose: Creates a delta header for the entity
//-----------------------------------------------------------------------------
void CFrameSnapshotManager::UpdateHeaderDelta(CEntityWriteInfo& u, int entnum)
{
	// Profiling info
	//	u.m_nTotalGap += entnum - u.m_nHeaderBase;
	//	u.m_nTotalGapCount++;

	// Keep track of number of headers so we can tell the client
	u.m_nHeaderCount++;
	u.m_nHeaderBase = entnum;
}

//
// Write the delta header. Also update the header delta info if bUpdateHeaderDelta is true.
//
// There are some cases where you want to tenatively write a header, then possibly back it out.
// In these cases:
// - pass in false for bUpdateHeaderDelta
// - store the return value from SV_WriteDeltaHeader
// - call SV_UpdateHeaderDelta ONLY if you want to keep the delta header it wrote
//
void CFrameSnapshotManager::WriteDeltaHeader(CEntityWriteInfo& u, int entnum, int flags)
{
	bf_write* pBuf = u.m_pBuf;

	// int startbit = pBuf->GetNumBitsWritten();

	int offset = entnum - u.m_nHeaderBase - 1;

	Assert(offset >= 0);

	SyncTag_Write(u.m_pBuf, "Hdr");

	pBuf->WriteUBitVar(offset);

	if (flags & FHDR_LEAVEPVS)
	{
		pBuf->WriteOneBit(1); // leave PVS bit
		pBuf->WriteOneBit(flags & FHDR_DELETE);
	}
	else
	{
		pBuf->WriteOneBit(0); // delta or enter PVS
		pBuf->WriteOneBit(flags & FHDR_ENTERPVS);
	}

	UpdateHeaderDelta(u, entnum);
}

// NOTE: to optimize this, it could store the bit offsets of each property in the packed entity.
// It would only have to store the offsets for the entities for each frame, since it only reaches 
// into the current frame's entities here.
//static inline void SV_WritePropsFromPackedEntity(
//	CEntityWriteInfo& u,
//	const int* pCheckProps,
//	const int nCheckProps
//)
//{
//	
//}

void CFrameSnapshotManager::DetermineUpdateType(CBaseClient* pClient, CEntityWriteInfo& u)
{
	// Figure out how we want to update the entity.
	if (u.m_nNewEntity < u.m_nOldEntity)
	{
		// If the entity was not in the old packet (oldnum == 9999), then 
		// delta from the baseline since this is a new entity.
		u.m_UpdateType = EnterPVS;
		return;
	}

	if (u.m_nNewEntity > u.m_nOldEntity)
	{
		// If the entity was in the old list, but is not in the new list 
		// (newnum == 9999), then construct a special remove message.
		u.m_UpdateType = LeavePVS;
		return;
	}

	if (u.m_nNewEntity != u.m_nOldEntity) {
		Error("state error");
	}

	Assert(g_pPackedEntityManager->GetSnapshotEntry(u.m_pToSnapshot, u.m_nNewEntity)->m_pClass);

	bool recreate = NeedsExplicitCreate(u);

	if (recreate)
	{
		u.m_UpdateType = EnterPVS;
		return;
	}

	// These should be the same! If they're not, then it should detect an explicit create message.
	//Assert(u.m_pOldPack->m_pServerClass == u.m_pNewPack->m_pServerClass);

	// We can early out with the delta bits if we are using the same pack handles...
	if (g_pPackedEntityManager->IsSamePackedEntity(u))//u.m_pOldPack == u.m_pNewPack
	{
		//Assert(u.m_pOldPack != NULL);
		u.m_UpdateType = PreserveEnt;
		return;
	}

#ifndef _X360
	int nBits;
#if defined( REPLAY_ENABLED )
	if (!u.m_bCullProps && (hltv || replay))
	{
		unsigned char* pBuffer = hltv ? hltv->m_DeltaCache.FindDeltaBits(u.m_nNewEntity, u.m_pFromSnapshot->m_nTickCount, nBits)
			: replay->m_DeltaCache.FindDeltaBits(u.m_nNewEntity, u.m_pFromSnapshot->m_nTickCount, nBits);
#else
	if (!u.m_bCullProps && hltv)
	{
		unsigned char* pBuffer = hltv->m_DeltaCache.FindDeltaBits(u.m_nNewEntity, u.m_pFromSnapshot->m_nTickCount, nBits);
#endif

		if (pBuffer)
		{
			if (nBits > 0)
			{
				// Write a header.
				WriteDeltaHeader(u, u.m_nNewEntity, FHDR_ZERO);

				// just write the cached bit stream 
				u.m_pBuf->WriteBits(pBuffer, nBits);

				u.m_UpdateType = DeltaEnt;
			}
			else
			{
				u.m_UpdateType = PreserveEnt;
			}

			return; // we used the cache, great
		}
	}
#endif

	int checkProps[MAX_DATATABLE_PROPS];
	int nCheckProps = 0;
	g_pPackedEntityManager->GetChangedProps(u, checkProps, nCheckProps, ARRAYSIZE(checkProps));

	if (nCheckProps > 0)
	{
		// Write a header.
		WriteDeltaHeader(u, u.m_nNewEntity, FHDR_ZERO);
#if defined( DEBUG_NETWORKING )
		int startBit = u.m_pBuf->GetNumBitsWritten();
#endif
		g_pPackedEntityManager->WriteDeltaEnt(pClient, u, checkProps, nCheckProps);
#if defined( DEBUG_NETWORKING )
		int endBit = u.m_pBuf->GetNumBitsWritten();
		TRACE_PACKET(("    Delta Bits (%d) = %d (%d bytes)\n", u.m_nNewEntity, (endBit - startBit), ((endBit - startBit) + 7) / 8));
#endif
		// If the numbers are the same, then the entity was in the old and new packet.
		// Just delta compress the differences.
		u.m_UpdateType = DeltaEnt;
	}
	else
	{
#ifndef _X360
		if (!u.m_bCullProps)
		{
			if (hltv)
			{
				// no bits changed, PreserveEnt
				hltv->m_DeltaCache.AddDeltaBits(u.m_nNewEntity, u.m_pFromSnapshot->m_nTickCount, 0, NULL);
			}

#if defined( REPLAY_ENABLED )
			if (replay)
			{
				// no bits changed, PreserveEnt
				replay->m_DeltaCache.AddDeltaBits(u.m_nNewEntity, u.m_pFromSnapshot->m_nTickCount, 0, NULL);
			}
#endif
		}
#endif
		u.m_UpdateType = PreserveEnt;
	}
}

//static inline void SV_WriteEnterPVS(CEntityWriteInfo& u)
//{
//	
//
//}

//-----------------------------------------------------------------------------
// Purpose: Entity wasn't dealt with in packet, but it has been deleted, we'll flag
//  the entity for destruction
// Input  : type - 
//			entnum - 
//			*from - 
//			*to - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CFrameSnapshotManager::NeedsExplicitDestroy(int entnum, CFrameSnapshot* from, CFrameSnapshot* to)
{
	// Never on uncompressed packet

	if (entnum >= to->m_nNumEntities || g_pPackedEntityManager->GetSnapshotEntry(to, entnum)->m_pClass == NULL) // doesn't exits in new
	{
		if (entnum >= from->m_nNumEntities)
			return false; // didn't exist in old

		// in old, but not in new, destroy.
		if (g_pPackedEntityManager->GetSnapshotEntry(from, entnum)->m_pClass != NULL)
		{
			return true;
		}
	}

	return false;
}

void CFrameSnapshotManager::WriteLeavePVS(CEntityWriteInfo& u)
{
	int headerflags = FHDR_LEAVEPVS;
	bool deleteentity = false;

	if (u.m_bAsDelta)
	{
		deleteentity = NeedsExplicitDestroy(u.m_nOldEntity, u.m_pFromSnapshot, u.m_pToSnapshot);
	}

	if (deleteentity)
	{
		// Mark that we handled deletion of this index
		u.m_DeletionFlags.Set(u.m_nOldEntity);

		headerflags |= FHDR_DELETE;
	}

	TRACE_PACKET(("  SV Leave PVS (%d) %s %s\n", u.m_nOldEntity,
		deleteentity ? "deleted" : "left pvs",
		u.m_pOldPack->m_pServerClass->m_pNetworkName));

	WriteDeltaHeader(u, u.m_nOldEntity, headerflags);

	u.NextOldEntity();
}

void CFrameSnapshotManager::WriteDeltaEnt(CEntityWriteInfo& u)
{
	TRACE_PACKET(("  SV Delta PVS (%d %d) %s\n", u.m_nNewEntity, u.m_nOldEntity, u.m_pOldPack->m_pServerClass->m_pNetworkName));

	// NOTE: it was already written in DetermineUpdateType. By doing it this way, we avoid an expensive
	// (non-byte-aligned) copy of the data.

	u.NextOldEntity();
	u.NextNewEntity();
}

void CFrameSnapshotManager::WritePreserveEnt(CEntityWriteInfo& u)
{
	TRACE_PACKET(("  SV Preserve PVS (%d) %s\n", u.m_nOldEntity, u.m_pOldPack->m_pServerClass->m_pNetworkName));

	// updateType is preserveEnt. The client will detect this because our next entity will have a newnum
	// that is greater than oldnum, in which case the client just keeps the current entity alive.
	u.NextOldEntity();
	u.NextNewEntity();
}

void CFrameSnapshotManager::WriteEntityUpdate(CBaseClient* pClient, CEntityWriteInfo& u)
{
	switch (u.m_UpdateType)
	{
	case EnterPVS:
	{
		TRACE_PACKET(("  SV Enter PVS (%d) %s\n", u.m_nNewEntity, u.m_pNewPack->m_pServerClass->m_pNetworkName));

		WriteDeltaHeader(u, u.m_nNewEntity, FHDR_ENTERPVS);
		//SV_WriteEnterPVS(u);
		g_pPackedEntityManager->WriteEnterPVS(pClient, u);
	}
	break;

	case LeavePVS:
	{
		WriteLeavePVS(u);
	}
	break;

	case DeltaEnt:
	{
		WriteDeltaEnt(u);
	}
	break;

	case PreserveEnt:
	{
		WritePreserveEnt(u);
	}
	break;
	}
}

int CFrameSnapshotManager::WriteDeletions(CEntityWriteInfo& u)
{
	if (!u.m_bAsDelta)
		return 0;

	int nNumDeletions = 0;

	CFrameSnapshot* pFromSnapShot = u.m_pFromSnapshot;
	CFrameSnapshot* pToSnapShot = u.m_pToSnapshot;

	int nLast = MAX(pFromSnapShot->m_nNumEntities, pToSnapShot->m_nNumEntities);
	for (int i = 0; i < nLast; i++)
	{
		// Packet update didn't clear it out expressly
		if (u.m_DeletionFlags.Get(i))
			continue;

		// If the entity is marked to transmit in the u.m_pTo, then it can never be destroyed by the m_iExplicitDeleteSlots
		// Another possible fix would be to clear any slots in the explicit deletes list that were actually occupied when a snapshot was taken
		if (u.m_pTo->transmit_entity.Get(i))
			continue;

		// Looks like it should be gone
		bool bNeedsExplicitDelete = NeedsExplicitDestroy(i, pFromSnapShot, pToSnapShot);
		if (!bNeedsExplicitDelete && u.m_pTo)
		{
			bNeedsExplicitDelete = (pToSnapShot->m_iExplicitDeleteSlots.Find(i) != pToSnapShot->m_iExplicitDeleteSlots.InvalidIndex());
			// We used to do more stuff here as a sanity check, but I don't think it was necessary since the only thing that would unset the bould would be a "recreate" in the same slot which is
			// already implied by the u.m_pTo->transmit_entity.Get(i) check
			if (bNeedsExplicitDelete) {
				int aaa = 0;
			}
		}

		// Check conditions
		if (bNeedsExplicitDelete)
		{
			TRACE_PACKET(("  SV Explicit Destroy (%d)\n", i));

			u.m_pBuf->WriteOneBit(1);
			u.m_pBuf->WriteUBitLong(i, MAX_EDICT_BITS);
			++nNumDeletions;
		}
	}
	// No more entities..
	u.m_pBuf->WriteOneBit(0);

	return nNumDeletions;
}

/*
=============
WritePacketEntities

Computes either a compressed, or uncompressed delta buffer for the client.
Returns the size IN BITS of the message buffer created.
=============
*/

void CFrameSnapshotManager::WriteDeltaEntities(CBaseClient* pClient, CClientFrame* to, CClientFrame* from, bf_write& pBuf)
{
	VPROF_BUDGET("CBaseServer::WriteDeltaEntities", VPROF_BUDGETGROUP_OTHER_NETWORKING);
	// Setup the CEntityWriteInfo structure.
	CEntityWriteInfo u;
	u.m_pBuf = &pBuf;
	u.m_pTo = to;
	u.m_pToSnapshot = to->GetSnapshot();

	//CClientSnapshotEntryInfo* pClientSnapshotEntryInfo = g_pPackedEntityManager->GetClientSnapshotEntryInfo(client);

	//u.m_pBaselineEntities = pClientSnapshotEntryInfo->m_pBaselineEntities;
	//u.m_nNumBaselineEntities = pClientSnapshotEntryInfo->m_nNumBaselineEntities;
	u.m_nFullProps = 0;
	//u.m_pServer = &sv;
	//u.m_pClient = client;
#ifndef _XBOX
	if (sv.IsHLTV() || sv.IsReplay())
	{
		// cull props only on master proxy
		u.m_bCullProps = sv.IsActive();
	}
	else
#endif
	{
		u.m_bCullProps = true;	// always cull props for players
	}

	if (from != NULL)
	{
		u.m_bAsDelta = true;
		u.m_pFrom = from;
		u.m_pFromSnapshot = from->GetSnapshot();
		Assert(u.m_pFromSnapshot);
	}
	else
	{
		u.m_bAsDelta = false;
		u.m_pFrom = NULL;
		u.m_pFromSnapshot = NULL;
	}

	u.m_nHeaderCount = 0;
	//	u.m_nTotalGap = 0;
	//	u.m_nTotalGapCount = 0;

		// set from_baseline pointer if this snapshot may become a baseline update
	if (GetClientSnapshotInfo(pClient)->m_nBaselineUpdateTick == -1)
	{
		GetClientSnapshotInfo(pClient)->m_BaselinesSent.ClearAll();
		u.from_baseline = &GetClientSnapshotInfo(pClient)->m_BaselinesSent;
	}

	// Write the header, TODO use class SVC_PacketEntities

	TRACE_PACKET(("WriteDeltaEntities (%d)\n", u.m_pToSnapshot->m_nNumEntities));

	u.m_pBuf->WriteUBitLong(svc_PacketEntities, NETMSG_TYPE_BITS);

	u.m_pBuf->WriteUBitLong(u.m_pToSnapshot->m_nNumEntities, MAX_EDICT_BITS);

	if (u.m_bAsDelta)
	{
		u.m_pBuf->WriteOneBit(1); // use delta sequence

		u.m_pBuf->WriteLong(u.m_pFrom->tick_count);    // This is the sequence # that we are updating from.
	}
	else
	{
		u.m_pBuf->WriteOneBit(0);	// use baseline
	}

	u.m_pBuf->WriteUBitLong(GetClientSnapshotInfo(pClient)->m_nBaselineUsed, 1);	// tell client what baseline we are using

	// Store off current position 
	bf_write savepos = *u.m_pBuf;

	// Save room for number of headers to parse, too
	u.m_pBuf->WriteUBitLong(0, MAX_EDICT_BITS + DELTASIZE_BITS + 1);

	int startbit = u.m_pBuf->GetNumBitsWritten();

	bool bIsTracing = pClient->IsTracing();
	if (bIsTracing)
	{
		pClient->TraceNetworkData(pBuf, "Delta Entities Overhead");
	}

	// Don't work too hard if we're using the optimized single-player mode.
	if (!g_pLocalNetworkBackdoor)
	{
		// Iterate through the in PVS bitfields until we find an entity 
		// that was either in the old pack or the new pack
		u.NextOldEntity();
		u.NextNewEntity();

		while ((u.m_nOldEntity != ENTITY_SENTINEL) || (u.m_nNewEntity != ENTITY_SENTINEL))
		{
			//u.m_pNewPack = (u.m_nNewEntity != ENTITY_SENTINEL) ? g_pPackedEntityManager->GetPackedEntity(u.m_pToSnapshot, u.m_nNewEntity) : NULL;
			//u.m_pOldPack = (u.m_nOldEntity != ENTITY_SENTINEL) ? g_pPackedEntityManager->GetPackedEntity(u.m_pFromSnapshot, u.m_nOldEntity) : NULL;
			int nEntityStartBit = pBuf.GetNumBitsWritten();
			int oldEntityIndex = u.m_nOldEntity;
			int newEntityIndex = u.m_nNewEntity;

			// Figure out how we want to write this entity.
			DetermineUpdateType(pClient, u);
			WriteEntityUpdate(pClient, u);

			if (!bIsTracing)
				continue;

			PackedEntity* pNewPack = (newEntityIndex != ENTITY_SENTINEL) ? g_pPackedEntityManager->GetPackedEntity(u.m_pToSnapshot, newEntityIndex) : NULL;
			PackedEntity* pOldPack = (oldEntityIndex != ENTITY_SENTINEL) ? g_pPackedEntityManager->GetPackedEntity(u.m_pFromSnapshot, oldEntityIndex) : NULL;
			switch (u.m_UpdateType)
			{
			default:
			case PreserveEnt:
				break;
			case EnterPVS:
			{
				char const* eString = serverEntitylist->GetServerEntity(pNewPack->m_nEntityIndex)->GetClassName();
				pClient->TraceNetworkData(pBuf, "enter [%s]", eString);
				ETWMark1I(eString, pBuf.GetNumBitsWritten() - nEntityStartBit);
			}
			break;
			case LeavePVS:
			{
				// Note, can't use GetNetworkable() since the edict has been freed at this point
				char const* eString = pOldPack->m_pServerClass->m_pNetworkName;
				pClient->TraceNetworkData(pBuf, "leave [%s]", eString);
				ETWMark1I(eString, pBuf.GetNumBitsWritten() - nEntityStartBit);
			}
			break;
			case DeltaEnt:
			{
				char const* eString = serverEntitylist->GetServerEntity(pOldPack->m_nEntityIndex)->GetClassName();
				pClient->TraceNetworkData(pBuf, "delta [%s]", eString);
				ETWMark1I(eString, pBuf.GetNumBitsWritten() - nEntityStartBit);
			}
			break;
			}
		}

		if (u.m_nOldEntity == ENTITY_SENTINEL && u.m_nNewEntity == ENTITY_SENTINEL) {

		}
		else {
			Error("state error");
		}

		// Now write out the express deletions
		int nNumDeletions = WriteDeletions(u);
		if (bIsTracing)
		{
			pClient->TraceNetworkData(pBuf, "Delta: [%d] deletions", nNumDeletions);
		}
	}

	// get number of written bits
	int length = u.m_pBuf->GetNumBitsWritten() - startbit;

	// go back to header and fill in correct length now
	savepos.WriteUBitLong(u.m_nHeaderCount, MAX_EDICT_BITS);
	savepos.WriteUBitLong(length, DELTASIZE_BITS);

	bool bUpdateBaseline = ((GetClientSnapshotInfo(pClient)->m_nBaselineUpdateTick == -1) &&
		(u.m_nFullProps > 0 || !u.m_bAsDelta));

	if (u.m_nFullProps > 0) {
	
	}
	else {
		int aaa = 0;
	}

	if (bUpdateBaseline)// && u.m_pBaselineEntities
	{
		// tell client to use this snapshot as baseline update
		savepos.WriteOneBit(1);
		GetClientSnapshotInfo(pClient)->m_nBaselineUpdateTick = to->tick_count;
	}
	else
	{
		savepos.WriteOneBit(0);
	}

	if (bIsTracing)
	{
		pClient->TraceNetworkData(pBuf, "Delta Finish");
	}
}

//-----------------------------------------------------------------------------
// Purpose: Writes events to the client's network buffer
// Input  : *cl -
//			*pack -
//			*msg -
//-----------------------------------------------------------------------------
static ConVar sv_debugtempentities("sv_debugtempentities", "0", 0, "Show temp entity bandwidth usage.");

static bool CEventInfo_LessFunc(CEventInfo* const& lhs, CEventInfo* const& rhs)
{
	return lhs->classID < rhs->classID;
}

void CFrameSnapshotManager::WriteTempEntities(CBaseClient* pClient, CFrameSnapshot* pCurrentSnapshot, CFrameSnapshot* pLastSnapshot, bf_write& buf, int ev_max)
{
	VPROF_BUDGET("CBaseServer::WriteTempEntities", VPROF_BUDGETGROUP_OTHER_NETWORKING);

	ALIGN4 char data[NET_MAX_PAYLOAD] ALIGN4_POST;
	SVC_TempEntities msg;
	msg.m_DataOut.StartWriting(data, sizeof(data));
	bf_write& buffer = msg.m_DataOut; // shortcut

	CFrameSnapshot* pSnapshot;
	CEventInfo* pLastEvent = NULL;

	bool bDebug = sv_debugtempentities.GetBool();

	// limit max entities to field bit length
	ev_max = min(ev_max, ((1 << CEventInfo::EVENT_INDEX_BITS) - 1));

	if (pLastSnapshot)
	{
		pSnapshot = pLastSnapshot->NextSnapshot();
	}
	else
	{
		pSnapshot = pCurrentSnapshot;
	}

	CUtlRBTree< CEventInfo* >	sorted(0, ev_max, CEventInfo_LessFunc);

	// Build list of events sorted by send table classID (makes the delta work better in cases with a lot of the same message type )
	while (pSnapshot && ((int)sorted.Count() < ev_max))
	{
		for (int i = 0; i < pSnapshot->m_nTempEntities; ++i)
		{
			CEventInfo* event = pSnapshot->m_pTempEntities[i];

			if (pClient->IgnoreTempEntity(event))
				continue; // event is not seen by this player

			sorted.Insert(event);
			// More space still
			if ((int)sorted.Count() >= ev_max)
				break;
		}

		// stop, we reached our current snapshot
		if (pSnapshot == pCurrentSnapshot)
			break;

		// got to next snapshot
		pSnapshot = NextSnapshot(pSnapshot);
	}

	if (sorted.Count() <= 0)
		return;

	for (int i = sorted.FirstInorder();
		i != sorted.InvalidIndex();
		i = sorted.NextInorder(i))
	{
		CEventInfo* event = sorted[i];

		if (event->fire_delay == 0.0f)
		{
			buffer.WriteOneBit(0);
		}
		else
		{
			buffer.WriteOneBit(1);
			buffer.WriteSBitLong(event->fire_delay * 100.0f, 8);
		}

		if (pLastEvent &&
			pLastEvent->classID == event->classID)
		{
			buffer.WriteOneBit(0); // delta against last temp entity

			int startBit = bDebug ? buffer.GetNumBitsWritten() : 0;

			SendTable_WriteAllDeltaProps(event->pSendTable,
				pLastEvent->pData,
				pLastEvent->bits,
				event->pData,
				event->bits,
				-1,
				&buffer);

			if (bDebug)
			{
				int length = buffer.GetNumBitsWritten() - startBit;
				DevMsg("TE %s delta bits: %i\n", event->pSendTable->GetName(), length);
			}
		}
		else
		{
			// full update, just compressed against zeros in MP

			buffer.WriteOneBit(1);

			int startBit = bDebug ? buffer.GetNumBitsWritten() : 0;

			buffer.WriteUBitLong(event->classID, sv.GetClassBits());

			if (sv.IsMultiplayer())
			{
				SendTable_WriteAllDeltaProps(event->pSendTable,
					NULL,	// will write only non-zero elements
					0,
					event->pData,
					event->bits,
					-1,
					&buffer);
			}
			else
			{
				// write event with zero properties
				buffer.WriteBits(event->pData, event->bits);
			}

			if (bDebug)
			{
				int length = buffer.GetNumBitsWritten() - startBit;
				DevMsg("TE %s full bits: %i\n", event->pSendTable->GetName(), length);
			}
		}

		if (sv.IsMultiplayer())
		{
			// in single player, don't used delta compression, lastEvent remains NULL
			pLastEvent = event;
		}
	}

	// set num entries
	msg.m_nNumEntries = sorted.Count();
	msg.WriteToBuffer(buf);
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


CFrameSnapshot::CFrameSnapshot(CFrameSnapshotManager* pFrameSnapshotManager)
:m_pFrameSnapshotManager(pFrameSnapshotManager)
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
		m_pFrameSnapshotManager->DeleteFrameSnapshot( this );
	}
}

CFrameSnapshot* CFrameSnapshot::NextSnapshot() const
{
	return m_pFrameSnapshotManager->NextSnapshot( this );
}


