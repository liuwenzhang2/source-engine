//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#if !defined( FRAMESNAPSHOT_H )
#define FRAMESNAPSHOT_H
#ifdef _WIN32
#pragma once
#endif

#include <mempool.h>
#include <utllinkedlist.h>
#include "smartptr.h"
#include "clientframe.h"
#include <iservernetworkable.h>
#include <bitbuf.h>
#include "ents_shared.h"
#include "dt_send_eng.h"
#include "dt.h"
#include "server_class.h"

class PackedEntity;
class HLTVEntityData;
class ReplayEntityData;
//class ServerClass;
class CEventInfo;
class CBaseClient;
class CGameClient;
//typedef intptr_t PackedEntityHandle_t;



// HLTV needs some more data per entity 
class CHLTVEntityData
{
public:
	vec_t			origin[3];	// entity position
	unsigned int	m_nNodeCluster;  // if (1<<31) is set it's a node, otherwise a cluster
};

// Replay needs some more data per entity 
class CReplayEntityData
{
public:
	vec_t			origin[3];	// entity position
	unsigned int	m_nNodeCluster;  // if (1<<31) is set it's a node, otherwise a cluster
};



class CFrameSnapshotManager;

//-----------------------------------------------------------------------------
// Purpose: For all entities, stores whether the entity existed and what frame the
//  snapshot is for.  Also tracks whether the snapshot is still referenced.  When no
//  longer referenced, it's freed
//-----------------------------------------------------------------------------
class CFrameSnapshot
{
	DECLARE_FIXEDSIZE_ALLOCATOR(CFrameSnapshot);

public:

	CFrameSnapshot(CFrameSnapshotManager* pFrameSnapshotManager);
	~CFrameSnapshot();

	// Reference-counting.
	void					AddReference();
	void					ReleaseReference();

	CFrameSnapshot* NextSnapshot() const;


public:

	CFrameSnapshotManager* const m_pFrameSnapshotManager;
	CInterlockedInt			m_ListIndex;	// Index info CFrameSnapshotManager::m_FrameSnapshots.

	// Associated frame. 
	int						m_nTickCount; // = sv.tickcount

	// State information
	//CFrameSnapshotEntry		*m_pEntities;	
	int						m_nNumEntities; // = sv.num_edicts

	// This list holds the entities that are in use and that also aren't entities for inactive clients.
	unsigned short* m_pValidEntities;
	int						m_nValidEntities;

	// Additional HLTV info
	CHLTVEntityData* m_pHLTVEntityData; // is NULL if not in HLTV mode or array of m_pValidEntities entries
	CReplayEntityData* m_pReplayEntityData; // is NULL if not in replay mode or array of m_pValidEntities entries

	CEventInfo** m_pTempEntities; // temp entities
	int						m_nTempEntities;

	CUtlVector<int>			m_iExplicitDeleteSlots;

private:

	// Snapshots auto-delete themselves when their refcount goes to zero.
	CInterlockedInt			m_nReferences;
};

class CFrameSnapshotManager;

class CClientSnapshotInfo : public CClientFrameManager {
public:
	CClientSnapshotInfo(CFrameSnapshotManager* pFrameSnapshotManager)
	:m_pFrameSnapshotManager(pFrameSnapshotManager)
	{
		m_pLastSnapshot = NULL;
		m_nBaselineUpdateTick = -1;
		m_nBaselineUsed = 0;
		m_BaselinesSent.ClearAll();
		m_pCurrentFrame = NULL;
		memset(&m_PackInfo, 0, sizeof(m_PackInfo));
		//memset(&m_PrevPackInfo, 0, sizeof(m_PrevPackInfo));
		//m_PrevTransmitEdict.ClearAll();
		//m_PrevPackInfo.m_pTransmitEdict = &m_PrevTransmitEdict;
	}

	virtual ~CClientSnapshotInfo()
	{

	}

	void	SetupPackInfo(CFrameSnapshot* pSnapshot, CGameClient* clients);
	//void	SetupPrevPackInfo();
	CClientFrame* GetDeltaFrame(int nTick);
	CClientFrame* GetSendFrame();

	CFrameSnapshotManager* const m_pFrameSnapshotManager;
	CGameClient* m_pClient = NULL;
	CSmartPtr<CFrameSnapshot, CRefCountAccessorLongName> m_pLastSnapshot;	// last send snapshot

	//CFrameSnapshot	*m_pBaseline;			// current entity baselines as a snapshot
	int						m_nBaselineUpdateTick = -1;	// last tick we send client a update baseline signal or -1
	int						m_nBaselineUsed = 0;		// 0/1 toggling flag, singaling client what baseline to use
	CBitVec<MAX_EDICTS>		m_BaselinesSent;	// baselines sent with last update

	CClientFrame* m_pCurrentFrame = NULL;	// last added frame
	CCheckTransmitInfo		m_PackInfo;

	//CCheckTransmitInfo		m_PrevPackInfo;		// Used to speed up CheckTransmit.
	//CBitVec<MAX_EDICTS>		m_PrevTransmitEdict;
};

// These are the main variables used by the SV_CreatePacketEntities function.
// The function is split up into multiple smaller ones and they pass this structure around.
class CEntityWriteInfo : public CEntityInfo
{
public:
	bf_write* m_pBuf;
	//CBaseClient* m_pClient;

	//PackedEntity* m_pOldPack;
	//PackedEntity* m_pNewPack;

	// For each entity handled in the to packet, mark that's it has already been deleted if that's the case
	CBitVec<MAX_EDICTS>	m_DeletionFlags;

	CFrameSnapshot* m_pFromSnapshot; // = m_pFrom->GetSnapshot();
	CFrameSnapshot* m_pToSnapshot; // = m_pTo->GetSnapshot();

	//CFrameSnapshot	*m_pBaseline; // the clients baseline
	//CFrameSnapshotEntry* m_pBaselineEntities;
	//int					m_nNumBaselineEntities;
	CBitVec<MAX_EDICTS>* from_baseline;

	//CBaseServer* m_pServer;	// the server who writes this entity

	int				m_nFullProps;	// number of properties send as full update (Enter PVS)
	bool			m_bCullProps;	// filter props by clients in recipient lists

	/* Some profiling data
	int				m_nTotalGap;
	int				m_nTotalGapCount; */

	int checkProps[MAX_DATATABLE_PROPS];
	int nCheckProps = 0;
};

//-----------------------------------------------------------------------------
// Purpose: snapshot manager class
//-----------------------------------------------------------------------------

class CFrameSnapshotManager
{
	friend class CFrameSnapshot;

public:
	CFrameSnapshotManager(void);
	virtual ~CFrameSnapshotManager(void);

	// IFrameSnapshot implementation.
public:

	// Called when a level change happens
	virtual void			LevelChanged();

	// Called once per frame after simulation to store off all entities.
	// Note: the returned snapshot has a recount of 1 so you MUST call ReleaseReference on it.
	CFrameSnapshot* CreateEmptySnapshot(int ticknumber, int maxEntities);

	CFrameSnapshot* TakeTickSnapshot(int clientCount, CGameClient** clients, int ticknumber);

	CFrameSnapshot* NextSnapshot(const CFrameSnapshot* pSnapshot);

	//CThreadFastMutex	&GetMutex();

	// List of entities to explicitly delete
	void			AddExplicitDelete(int iSlot);

	void	OnClientConnected(CBaseClient* pClient);

	void	OnClientInactivate(CBaseClient* pClient);

	void	FreeClientBaselines(CBaseClient* pClient);

	void	AllocClientBaselines(CBaseClient* pClient);

	virtual void	WriteDeltaEntities(CBaseClient* pClient, CClientFrame* to, CClientFrame* from, bf_write& pBuf);

	virtual void	WriteTempEntities(CBaseClient* pClient, CFrameSnapshot* to, CFrameSnapshot* from, bf_write& pBuf, int nMaxEnts);

	CClientSnapshotInfo* GetClientSnapshotInfo(CBaseClient* pClient);

	void	UpdateAcknowledgedFramecount(CBaseClient* pClient, int tick);

	bool	ProcessBaselineAck(CBaseClient* pClient, int nBaselineTick, int	nBaselineNr);

	CFrameSnapshot* GetClientLastSnapshot(CBaseClient* pClient);

	void SetClientLastSnapshot(CBaseClient* pClient, CFrameSnapshot* pSnapshot);

private:
	void	DeleteFrameSnapshot(CFrameSnapshot* pSnapshot);
	void	CheckClientSnapshotArray(int maxIndex);

	void	PackEntities_NetworkBackDoor(int clientCount, CGameClient** clients, CFrameSnapshot* snapshot);
	void	PackEntities_Normal(int clientCount, CGameClient** clients, CFrameSnapshot* snapshot);

	void	DetermineUpdateType(CEntityWriteInfo& u);
	void	WriteEntityUpdate(CBaseClient* pClient, CEntityWriteInfo& u);
	void	WriteDeltaHeader(CEntityWriteInfo& u, int entnum, int flags);
	void	UpdateHeaderDelta(CEntityWriteInfo& u, int entnum);
	//void	WriteLeavePVS(CEntityWriteInfo& u);
	bool	NeedsExplicitCreate(CEntityWriteInfo& u);
	bool	NeedsExplicitDestroy(int entnum, CFrameSnapshot* from, CFrameSnapshot* to);
	//void	WriteDeltaEnt(CEntityWriteInfo& u);
	//void	WritePreserveEnt(CEntityWriteInfo& u);
	int		WriteDeletions(CEntityWriteInfo& u);

	CUtlLinkedList<CFrameSnapshot*, unsigned short>		m_FrameSnapshots;

	CThreadFastMutex		m_WriteMutex;
	CUtlVector<int>			m_iExplicitDeleteSlots;

	CUtlVector<CClientSnapshotInfo*> m_ClientSnapshotInfo;


};

extern CFrameSnapshotManager* framesnapshotmanager;


#endif // FRAMESNAPSHOT_H
