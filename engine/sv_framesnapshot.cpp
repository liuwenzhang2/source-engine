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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

DEFINE_FIXEDSIZE_ALLOCATOR( CFrameSnapshot, 64, 64 );




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
	m_ClientSnapshotInfo.Purge();
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

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : framenumber - 
//-----------------------------------------------------------------------------
CFrameSnapshot* CFrameSnapshotManager::TakeTickSnapshot( int tickcount )
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
		m_ClientSnapshotInfo.AddToTail();
	}
}

void CFrameSnapshotManager::FreeClientBaselines(CBaseClient* pClient)
{
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = &m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
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
	CClientSnapshotInfo* pClientSnapshotInfo = &m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	pClientSnapshotInfo->m_pLastSnapshot = NULL;
	pClientSnapshotInfo->m_nBaselineUpdateTick = -1;
	pClientSnapshotInfo->m_nBaselineUsed = 0;

	g_pPackedEntityManager->OnClientConnected(pClient);
}

CClientSnapshotInfo* CFrameSnapshotManager::GetClientSnapshotInfo(CBaseClient* pClient) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	return &m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
}

void	CFrameSnapshotManager::UpdateAcknowledgedFramecount(CBaseClient* pClient, int tick) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = &m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	if ((pClientSnapshotInfo->m_nBaselineUpdateTick > -1) && (tick > pClientSnapshotInfo->m_nBaselineUpdateTick))
	{
		// server sent a baseline update, but it wasn't acknowledged yet so it was probably lost. 
		pClientSnapshotInfo->m_nBaselineUpdateTick = -1;
	}
}

bool	CFrameSnapshotManager::ProcessBaselineAck(CBaseClient* pClient, int nBaselineTick, int	nBaselineNr) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = &m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	
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

	Assert(m_pBaselineEntities);

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
	CClientSnapshotInfo* pClientSnapshotInfo = &m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
	return pClientSnapshotInfo->m_pLastSnapshot.GetObject();
}

void CFrameSnapshotManager::SetClientLastSnapshot(CBaseClient* pClient, CFrameSnapshot* pSnapshot) {
	CheckClientSnapshotArray(pClient->m_nClientSlot);
	CClientSnapshotInfo* pClientSnapshotInfo = &m_ClientSnapshotInfo.Element(pClient->m_nClientSlot);
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


