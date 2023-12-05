//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "entitylist.h"
#include "utlvector.h"
#include "igamesystem.h"
#include "collisionutils.h"
#include "UtlSortVector.h"
#include "tier0/vprof.h"
#include "mapentities.h"
#include "client.h"
#include "ai_initutils.h"
#include "globalstate.h"
#include "datacache/imdlcache.h"

#ifdef HL2_DLL
#include "npc_playercompanion.h"
#endif // HL2_DLL

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void SceneManager_ClientActive( CBasePlayer *player );

CUtlVector<IServerNetworkable*> g_DeleteList;

CGlobalEntityList<CBaseEntity> gEntList;
CBaseEntityList<CBaseEntity> *g_pEntityList = &gEntList;

// -------------------------------------------------------------------------------------------------- //
// Game-code CBaseHandle implementation.
// -------------------------------------------------------------------------------------------------- //

IHandleEntity* CBaseHandle::Get() const
{
#ifdef CLIENT_DLL
	//extern CBaseEntityList<IHandleEntity>* g_pEntityList;
	return g_pEntityList->LookupEntity(*this);
#endif // CLIENT_DLL
#ifdef GAME_DLL
	//extern CBaseEntityList<IHandleEntity>* g_pEntityList;
	return g_pEntityList->LookupEntity(*this);
#endif // GAME_DLL
}

// Called by CEntityListSystem
void CAimTargetManager::LevelInitPreEntity()
{ 
	gEntList.AddListenerEntity( this );
	Clear(); 
}
void CAimTargetManager::LevelShutdownPostEntity()
{
	gEntList.RemoveListenerEntity( this );
	Clear();
}

void CAimTargetManager::Clear()
{ 
	m_targetList.Purge(); 
}

void CAimTargetManager::ForceRepopulateList()
{
	Clear();

	CBaseEntity *pEnt = gEntList.FirstEnt();

	while( pEnt )
	{
		if( ShouldAddEntity(pEnt) )
			AddEntity(pEnt);

		pEnt = gEntList.NextEnt( pEnt );
	}
}
	
bool CAimTargetManager::ShouldAddEntity( CBaseEntity *pEntity )
{
	return ((pEntity->GetFlags() & FL_AIMTARGET) != 0);
}

// IEntityListener
void CAimTargetManager::OnEntityCreated( CBaseEntity *pEntity ) {}
void CAimTargetManager::OnEntityDeleted( CBaseEntity *pEntity )
{
	if ( !(pEntity->GetFlags() & FL_AIMTARGET) )
		return;
	RemoveEntity(pEntity);
}
void CAimTargetManager::AddEntity( CBaseEntity *pEntity )
{
	if ( pEntity->IsMarkedForDeletion() )
		return;
	m_targetList.AddToTail( pEntity );
}
void CAimTargetManager::RemoveEntity( CBaseEntity *pEntity )
{
	int index = m_targetList.Find( pEntity );
	if ( m_targetList.IsValidIndex(index) )
	{
		m_targetList.FastRemove( index );
	}
}
int CAimTargetManager::ListCount() { return m_targetList.Count(); }
int CAimTargetManager::ListCopy( CBaseEntity *pList[], int listMax )
{
	int count = MIN(listMax, ListCount() );
	memcpy( pList, m_targetList.Base(), sizeof(CBaseEntity *) * count );
	return count;
}

CAimTargetManager g_AimManager;

int AimTarget_ListCount()
{
	return g_AimManager.ListCount();
}
int AimTarget_ListCopy( CBaseEntity *pList[], int listMax )
{
	return g_AimManager.ListCopy( pList, listMax );
}
void AimTarget_ForceRepopulateList()
{
	g_AimManager.ForceRepopulateList();
}


// Manages a list of all entities currently doing game simulation or thinking
// NOTE: This is usually a small subset of the global entity list, so it's
// an optimization to maintain this list incrementally rather than polling each
// frame.
struct simthinkentry_t
{
	unsigned short	entEntry;
	unsigned short	unused0;
	int				nextThinkTick;
};
class CSimThinkManager : public IEntityListener
{
public:
	CSimThinkManager()
	{
		Clear();
	}
	void Clear()
	{
		m_simThinkList.Purge();
		for ( int i = 0; i < ARRAYSIZE(m_entinfoIndex); i++ )
		{
			m_entinfoIndex[i] = 0xFFFF;
		}
	}
	void LevelInitPreEntity()
	{
		gEntList.AddListenerEntity( this );
	}

	void LevelShutdownPostEntity()
	{
		gEntList.RemoveListenerEntity( this );
		Clear();
	}

	void OnEntityCreated( CBaseEntity *pEntity )
	{
		Assert( m_entinfoIndex[pEntity->GetRefEHandle().GetEntryIndex()] == 0xFFFF );
	}
	void OnEntityDeleted( CBaseEntity *pEntity )
	{
		RemoveEntinfoIndex( pEntity->GetRefEHandle().GetEntryIndex() );
	}

	void RemoveEntinfoIndex( int index )
	{
		int listHandle = m_entinfoIndex[index];
		// If this guy is in the active list, remove him
		if ( listHandle != 0xFFFF )
		{
			Assert(m_simThinkList[listHandle].entEntry == index);
			m_simThinkList.FastRemove( listHandle );
			m_entinfoIndex[index] = 0xFFFF;
			
			// fast remove shifted someone, update that someone
			if ( listHandle < m_simThinkList.Count() )
			{
				m_entinfoIndex[m_simThinkList[listHandle].entEntry] = listHandle;
			}
		}
	}
	int ListCount()
	{
		return m_simThinkList.Count();
	}

	int ListCopy( CBaseEntity *pList[], int listMax )
	{
		int count = MIN(listMax, ListCount());
		int out = 0;
		for ( int i = 0; i < count; i++ )
		{
			// only copy out entities that will simulate or think this frame
			if ( m_simThinkList[i].nextThinkTick <= gpGlobals->tickcount )
			{
				Assert(m_simThinkList[i].nextThinkTick>=0);
				int entinfoIndex = m_simThinkList[i].entEntry;
				const CEntInfo<CBaseEntity> *pInfo = gEntList.GetEntInfoPtrByIndex( entinfoIndex );
				pList[out] = (CBaseEntity *)pInfo->m_pEntity;
				Assert(m_simThinkList[i].nextThinkTick==0 || pList[out]->GetFirstThinkTick()==m_simThinkList[i].nextThinkTick);
				Assert( gEntList.IsEntityPtr( pList[out] ) );
				out++;
			}
		}

		return out;
	}

	void EntityChanged( CBaseEntity *pEntity )
	{
		// might change after deletion, don't put back into the list
		if ( pEntity->IsMarkedForDeletion() )
			return;
		
		const CBaseHandle &eh = pEntity->GetRefEHandle();
		if ( !eh.IsValid() )
			return;

		int index = eh.GetEntryIndex();
		if ( pEntity->IsEFlagSet( EFL_NO_THINK_FUNCTION ) && pEntity->IsEFlagSet( EFL_NO_GAME_PHYSICS_SIMULATION ) )
		{
			RemoveEntinfoIndex( index );
		}
		else
		{
			// already in the list? (had think or sim last time, now has both - or had both last time, now just one)
			if ( m_entinfoIndex[index] == 0xFFFF )
			{
				MEM_ALLOC_CREDIT();
				m_entinfoIndex[index] = m_simThinkList.AddToTail();
				m_simThinkList[m_entinfoIndex[index]].entEntry = (unsigned short)index;
				m_simThinkList[m_entinfoIndex[index]].nextThinkTick = 0;
				if ( pEntity->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION) )
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = pEntity->GetFirstThinkTick();
					Assert(m_simThinkList[m_entinfoIndex[index]].nextThinkTick>=0);
				}
			}
			else
			{
				// updating existing entry - if no sim, reset think time
				if ( pEntity->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION) )
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = pEntity->GetFirstThinkTick();
					Assert(m_simThinkList[m_entinfoIndex[index]].nextThinkTick>=0);
				}
				else
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = 0;
				}
			}
		}
	}

private:
	unsigned short m_entinfoIndex[NUM_ENT_ENTRIES];
	CUtlVector<simthinkentry_t>	m_simThinkList;
};

CSimThinkManager g_SimThinkManager;

int SimThink_ListCount()
{
	return g_SimThinkManager.ListCount();
}

int SimThink_ListCopy( CBaseEntity *pList[], int listMax )
{
	return g_SimThinkManager.ListCopy( pList, listMax );
}

void SimThink_EntityChanged( CBaseEntity *pEntity )
{
	g_SimThinkManager.EntityChanged( pEntity );
}

static CBaseEntityClassList *s_pClassLists = NULL;
CBaseEntityClassList::CBaseEntityClassList()
{
	m_pNextClassList = s_pClassLists;
	s_pClassLists = this;
}
CBaseEntityClassList::~CBaseEntityClassList()
{
}


// removes the entity from the global list
// only called from with the CBaseEntity destructor
bool g_fInCleanupDelete;


//-----------------------------------------------------------------------------
// NOTIFY LIST
// 
// Allows entities to get events fired when another entity changes
//-----------------------------------------------------------------------------
struct entitynotify_t
{
	CBaseEntity	*pNotify;
	CBaseEntity	*pWatched;
};
class CNotifyList : public INotify, public IEntityListener
{
public:
	// INotify
	void AddEntity( CBaseEntity *pNotify, CBaseEntity *pWatched );
	void RemoveEntity( CBaseEntity *pNotify, CBaseEntity *pWatched );
	void ReportNamedEvent( CBaseEntity *pEntity, const char *pEventName );
	void ClearEntity( CBaseEntity *pNotify );
	void ReportSystemEvent( CBaseEntity *pEntity, notify_system_event_t eventType, const notify_system_event_params_t &params );

	// IEntityListener
	virtual void OnEntityCreated( CBaseEntity *pEntity );
	virtual void OnEntityDeleted( CBaseEntity *pEntity );

	// Called from CEntityListSystem
	void LevelInitPreEntity();
	void LevelShutdownPreEntity();

private:
	CUtlVector<entitynotify_t>	m_notifyList;
};

void CNotifyList::AddEntity( CBaseEntity *pNotify, CBaseEntity *pWatched )
{
	// OPTIMIZE: Also flag pNotify for faster "RemoveAllNotify" ?
	pWatched->AddEFlags( EFL_NOTIFY );
	int index = m_notifyList.AddToTail();
	entitynotify_t &notify = m_notifyList[index];
	notify.pNotify = pNotify;
	notify.pWatched = pWatched;
}

// Remove noitfication for an entity
void CNotifyList::RemoveEntity( CBaseEntity *pNotify, CBaseEntity *pWatched )
{
	for ( int i = m_notifyList.Count(); --i >= 0; )
	{
		if ( m_notifyList[i].pNotify == pNotify && m_notifyList[i].pWatched == pWatched)
		{
			m_notifyList.FastRemove(i);
		}
	}
}


void CNotifyList::ReportNamedEvent( CBaseEntity *pEntity, const char *pInputName )
{
	variant_t emptyVariant;

	if ( !pEntity->IsEFlagSet(EFL_NOTIFY) )
		return;

	for ( int i = 0; i < m_notifyList.Count(); i++ )
	{
		if ( m_notifyList[i].pWatched == pEntity )
		{
			m_notifyList[i].pNotify->AcceptInput( pInputName, pEntity, pEntity, emptyVariant, 0 );
		}
	}
}

void CNotifyList::LevelInitPreEntity()
{
	gEntList.AddListenerEntity( this );
}

void CNotifyList::LevelShutdownPreEntity( void )
{
	gEntList.RemoveListenerEntity( this );
	m_notifyList.Purge();
}

void CNotifyList::OnEntityCreated( CBaseEntity *pEntity )
{
}

void CNotifyList::OnEntityDeleted( CBaseEntity *pEntity )
{
	ReportDestroyEvent( pEntity );
	ClearEntity( pEntity );
}


// UNDONE: Slow linear search?
void CNotifyList::ClearEntity( CBaseEntity *pNotify )
{
	for ( int i = m_notifyList.Count(); --i >= 0; )
	{
		if ( m_notifyList[i].pNotify == pNotify || m_notifyList[i].pWatched == pNotify)
		{
			m_notifyList.FastRemove(i);
		}
	}
}

void CNotifyList::ReportSystemEvent( CBaseEntity *pEntity, notify_system_event_t eventType, const notify_system_event_params_t &params )
{
	if ( !pEntity->IsEFlagSet(EFL_NOTIFY) )
		return;

	for ( int i = 0; i < m_notifyList.Count(); i++ )
	{
		if ( m_notifyList[i].pWatched == pEntity )
		{
			m_notifyList[i].pNotify->NotifySystemEvent( pEntity, eventType, params );
		}
	}
}

static CNotifyList g_NotifyList;
INotify *g_pNotify = &g_NotifyList;

class CEntityTouchManager : public IEntityListener
{
public:
	// called by CEntityListSystem
	void LevelInitPreEntity() 
	{ 
		gEntList.AddListenerEntity( this );
		Clear(); 
	}
	void LevelShutdownPostEntity() 
	{ 
		gEntList.RemoveListenerEntity( this );
		Clear(); 
	}
	void FrameUpdatePostEntityThink();

	void Clear()
	{
		m_updateList.Purge();
	}
	
	// IEntityListener
	virtual void OnEntityCreated( CBaseEntity *pEntity ) {}
	virtual void OnEntityDeleted( CBaseEntity *pEntity )
	{
		if ( !pEntity->GetCheckUntouch() )
			return;
		int index = m_updateList.Find( pEntity );
		if ( m_updateList.IsValidIndex(index) )
		{
			m_updateList.FastRemove( index );
		}
	}
	void AddEntity( CBaseEntity *pEntity )
	{
		if ( pEntity->IsMarkedForDeletion() )
			return;
		m_updateList.AddToTail( pEntity );
	}

private:
	CUtlVector<CBaseEntity *>	m_updateList;
};

static CEntityTouchManager g_TouchManager;

void EntityTouch_Add( CBaseEntity *pEntity )
{
	g_TouchManager.AddEntity( pEntity );
}


void CEntityTouchManager::FrameUpdatePostEntityThink()
{
	VPROF( "CEntityTouchManager::FrameUpdatePostEntityThink" );
	// Loop through all entities again, checking their untouch if flagged to do so
	
	int count = m_updateList.Count();
	if ( count )
	{
		// copy off the list
		CBaseEntity **ents = (CBaseEntity **)stackalloc( sizeof(CBaseEntity *) * count );
		memcpy( ents, m_updateList.Base(), sizeof(CBaseEntity *) * count );
		// clear it
		m_updateList.RemoveAll();
		
		// now update those ents
		for ( int i = 0; i < count; i++ )
		{
			//Assert( ents[i]->GetCheckUntouch() );
			if ( ents[i]->GetCheckUntouch() )
			{
				ents[i]->PhysicsCheckForEntityUntouch();
			}
		}
		stackfree( ents );
	}
}

class CRespawnEntitiesFilter : public IMapEntityFilter
{
public:
	virtual bool ShouldCreateEntity( const char *pClassname )
	{
		// Create everything but the world
		return Q_stricmp( pClassname, "worldspawn" ) != 0;
	}

	virtual CBaseEntity* CreateNextEntity( const char *pClassname )
	{
		return CreateEntityByName( pClassname );
	}
};

// One hook to rule them all...
// Since most of the little list managers in here only need one or two of the game
// system callbacks, this hook is a game system that passes them the appropriate callbacks
class CEntityListSystem : public CAutoGameSystemPerFrame
{
public:
	CEntityListSystem( char const *name ) : CAutoGameSystemPerFrame( name )
	{
		m_bRespawnAllEntities = false;
	}
	void LevelInitPreEntity()
	{
		g_NotifyList.LevelInitPreEntity();
		g_TouchManager.LevelInitPreEntity();
		g_AimManager.LevelInitPreEntity();
		g_SimThinkManager.LevelInitPreEntity();
#ifdef HL2_DLL
		OverrideMoveCache_LevelInitPreEntity();
#endif	// HL2_DLL
	}
	void LevelShutdownPreEntity()
	{
		g_NotifyList.LevelShutdownPreEntity();
	}
	void LevelShutdownPostEntity()
	{
		g_TouchManager.LevelShutdownPostEntity();
		g_AimManager.LevelShutdownPostEntity();
		g_SimThinkManager.LevelShutdownPostEntity();
#ifdef HL2_DLL
		OverrideMoveCache_LevelShutdownPostEntity();
#endif // HL2_DLL
		CBaseEntityClassList *pClassList = s_pClassLists;
		while ( pClassList )
		{
			pClassList->LevelShutdownPostEntity();
			pClassList = pClassList->m_pNextClassList;
		}
	}

	void FrameUpdatePostEntityThink()
	{
		g_TouchManager.FrameUpdatePostEntityThink();

		if ( m_bRespawnAllEntities )
		{
			m_bRespawnAllEntities = false;

			// Don't change globalstate owing to deletion here
			GlobalEntity_EnableStateUpdates( false );

			// Remove all entities
			int nPlayerIndex = -1;
			CBaseEntity *pEnt = gEntList.FirstEnt();
			while ( pEnt )
			{
				CBaseEntity *pNextEnt = gEntList.NextEnt( pEnt );
				if ( pEnt->IsPlayer() )
				{
					nPlayerIndex = pEnt->entindex();
				}
				if ( !pEnt->IsEFlagSet( EFL_KEEP_ON_RECREATE_ENTITIES ) )
				{
					UTIL_Remove( pEnt );
				}
				pEnt = pNextEnt;
			}
			
			gEntList.CleanupDeleteList();

			GlobalEntity_EnableStateUpdates( true );

			// Allows us to immediately re-use the edict indices we just freed to avoid edict overflow
			engine->AllowImmediateEdictReuse();

			// Reset node counter used during load
			CNodeEnt::m_nNodeCount = 0;

			CRespawnEntitiesFilter filter;
			MapEntity_ParseAllEntities( engine->GetMapEntitiesString(), &filter, true );

			// Allocate a CBasePlayer for pev, and call spawn
			if ( nPlayerIndex >= 0 )
			{
				edict_t *pEdict = engine->PEntityOfEntIndex( nPlayerIndex );
				ClientPutInServer( pEdict, "unnamed" );
				ClientActive( pEdict, false );

				CBasePlayer *pPlayer = ( CBasePlayer * )CBaseEntity::Instance( pEdict );
				SceneManager_ClientActive( pPlayer );
			}
		}
	}

	bool m_bRespawnAllEntities;
};

static CEntityListSystem g_EntityListSystem( "CEntityListSystem" );

//-----------------------------------------------------------------------------
// Respawns all entities in the level
//-----------------------------------------------------------------------------
void RespawnEntities()
{
	g_EntityListSystem.m_bRespawnAllEntities = true;
}

static ConCommand restart_entities( "respawn_entities", RespawnEntities, "Respawn all the entities in the map.", FCVAR_CHEAT | FCVAR_SPONLY );

class CSortedEntityList
{
public:
	CSortedEntityList() : m_sortedList(), m_emptyCount(0) {}

	typedef CBaseEntity *ENTITYPTR;
	class CEntityReportLess
	{
	public:
		bool Less( const ENTITYPTR &src1, const ENTITYPTR &src2, void *pCtx )
		{
			if ( stricmp( src1->GetClassname(), src2->GetClassname() ) < 0 )
				return true;
	
			return false;
		}
	};

	void AddEntityToList( CBaseEntity *pEntity )
	{
		if ( !pEntity )
		{
			m_emptyCount++;
		}
		else
		{
			m_sortedList.Insert( pEntity );
		}
	}
	void ReportEntityList()
	{
		const char *pLastClass = "";
		int count = 0;
		int edicts = 0;
		for ( int i = 0; i < m_sortedList.Count(); i++ )
		{
			CBaseEntity *pEntity = m_sortedList[i];
			if ( !pEntity )
				continue;

			if ( pEntity->edict() )
				edicts++;

			const char *pClassname = pEntity->GetClassname();
			if ( !FStrEq( pClassname, pLastClass ) )
			{
				if ( count )
				{
					Msg("Class: %s (%d)\n", pLastClass, count );
				}

				pLastClass = pClassname;
				count = 1;
			}
			else
				count++;
		}
		if ( pLastClass[0] != 0 && count )
		{
			Msg("Class: %s (%d)\n", pLastClass, count );
		}
		if ( m_sortedList.Count() )
		{
			Msg("Total %d entities (%d empty, %d edicts)\n", m_sortedList.Count(), m_emptyCount, edicts );
		}
	}
private:
	CUtlSortVector< CBaseEntity *, CEntityReportLess > m_sortedList;
	int		m_emptyCount;
};



CON_COMMAND(report_entities, "Lists all entities")
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	CSortedEntityList list;
	CBaseEntity *pEntity = gEntList.FirstEnt();
	while ( pEntity )
	{
		list.AddEntityToList( pEntity );
		pEntity = gEntList.NextEnt( pEntity );
	}
	list.ReportEntityList();
}


CON_COMMAND(report_touchlinks, "Lists all touchlinks")
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	CSortedEntityList list;
	CBaseEntity *pEntity = gEntList.FirstEnt();
	const char *pClassname = NULL;
	if ( args.ArgC() > 1 )
	{
		pClassname = args.Arg(1);
	}
	while ( pEntity )
	{
		if ( !pClassname || FClassnameIs(pEntity, pClassname) )
		{
			touchlink_t *root = ( touchlink_t * )pEntity->GetDataObject( TOUCHLINK );
			if ( root )
			{
				touchlink_t *link = root->nextLink;
				while ( link != root )
				{
					list.AddEntityToList( link->entityTouched );
					link = link->nextLink;
				}
			}
		}
		pEntity = gEntList.NextEnt( pEntity );
	}
	list.ReportEntityList();
}

CON_COMMAND(report_simthinklist, "Lists all simulating/thinking entities")
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	CBaseEntity *pTmp[NUM_ENT_ENTRIES];
	int count = SimThink_ListCopy( pTmp, ARRAYSIZE(pTmp) );

	CSortedEntityList list;
	for ( int i = 0; i < count; i++ )
	{
		if ( !pTmp[i] )
			continue;

		list.AddEntityToList( pTmp[i] );
	}
	list.ReportEntityList();
}

