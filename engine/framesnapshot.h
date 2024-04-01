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


class PackedEntity;
class HLTVEntityData;
class ReplayEntityData;
class ServerClass;
class CEventInfo;

#define INVALID_PACKED_ENTITY_HANDLE (0)
//typedef intptr_t PackedEntityHandle_t;

//-----------------------------------------------------------------------------
// Purpose: Individual entity data, did the entity exist and what was it's serial number
//-----------------------------------------------------------------------------
class CFrameSnapshotEntry
{
public:
	ServerClass*			m_pClass = NULL;
	int						m_nSerialNumber = -1;
	// Keeps track of the fullpack info for this frame for all entities in any pvs:
	PackedEntity*			m_pPackedData = INVALID_PACKED_ENTITY_HANDLE;
};

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





//-----------------------------------------------------------------------------
// Purpose: For all entities, stores whether the entity existed and what frame the
//  snapshot is for.  Also tracks whether the snapshot is still referenced.  When no
//  longer referenced, it's freed
//-----------------------------------------------------------------------------
class CFrameSnapshot
{
	DECLARE_FIXEDSIZE_ALLOCATOR( CFrameSnapshot );

public:

							CFrameSnapshot();
							~CFrameSnapshot();

	// Reference-counting.
	void					AddReference();
	void					ReleaseReference();

	CFrameSnapshot*			NextSnapshot() const;						


public:
	CInterlockedInt			m_ListIndex;	// Index info CFrameSnapshotManager::m_FrameSnapshots.

	// Associated frame. 
	int						m_nTickCount; // = sv.tickcount
	
	// State information
	//CFrameSnapshotEntry		*m_pEntities;	
	int						m_nNumEntities; // = sv.num_edicts

	// This list holds the entities that are in use and that also aren't entities for inactive clients.
	unsigned short			*m_pValidEntities; 
	int						m_nValidEntities;

	// Additional HLTV info
	CHLTVEntityData			*m_pHLTVEntityData; // is NULL if not in HLTV mode or array of m_pValidEntities entries
	CReplayEntityData		*m_pReplayEntityData; // is NULL if not in replay mode or array of m_pValidEntities entries

	CEventInfo				**m_pTempEntities; // temp entities
	int						m_nTempEntities;

	CUtlVector<int>			m_iExplicitDeleteSlots;

private:

	// Snapshots auto-delete themselves when their refcount goes to zero.
	CInterlockedInt			m_nReferences;
};

class ClientSnapshotInfo {
public:

	CSmartPtr<CFrameSnapshot, CRefCountAccessorLongName> m_pLastSnapshot;	// last send snapshot

	//CFrameSnapshot	*m_pBaseline;			// current entity baselines as a snapshot
	int				m_nBaselineUpdateTick;	// last tick we send client a update baseline signal or -1
	CBitVec<MAX_EDICTS>	m_BaselinesSent;	// baselines sent with last update
	int				m_nBaselineUsed;		// 0/1 toggling flag, singaling client what baseline to use
};

//-----------------------------------------------------------------------------
// Purpose: snapshot manager class
//-----------------------------------------------------------------------------

class CFrameSnapshotManager
{
	friend class CFrameSnapshot;

public:
	CFrameSnapshotManager( void );
	virtual ~CFrameSnapshotManager( void );

	// IFrameSnapshot implementation.
public:

	// Called when a level change happens
	virtual void			LevelChanged();

	// Called once per frame after simulation to store off all entities.
	// Note: the returned snapshot has a recount of 1 so you MUST call ReleaseReference on it.
	CFrameSnapshot*	CreateEmptySnapshot( int ticknumber, int maxEntities );
	CFrameSnapshot*	TakeTickSnapshot( int ticknumber );

	CFrameSnapshot*	NextSnapshot( const CFrameSnapshot *pSnapshot );

	//CThreadFastMutex	&GetMutex();

	// List of entities to explicitly delete
	void			AddExplicitDelete( int iSlot );

private:
	void	DeleteFrameSnapshot( CFrameSnapshot* pSnapshot );

	CUtlLinkedList<CFrameSnapshot*, unsigned short>		m_FrameSnapshots;

	CThreadFastMutex		m_WriteMutex;
	CUtlVector<int>			m_iExplicitDeleteSlots;




};

extern CFrameSnapshotManager *framesnapshotmanager;


#endif // FRAMESNAPSHOT_H
