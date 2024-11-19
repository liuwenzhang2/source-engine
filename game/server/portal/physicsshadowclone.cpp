//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Clones a physics object (usually with a matrix transform applied)
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "physicsshadowclone.h"
#include "portal_util_shared.h"
#include "vphysics/object_hash.h"
#include "trains.h"
#include "props.h"
#include "model_types.h"
#include "portal/weapon_physcannon.h" //grab controllers

#include "PortalSimulation.h"
#include "prop_portal.h"

#define MAX_SHADOW_CLONE_COUNT 200

static int g_iShadowCloneCount = 0;
ConVar sv_use_shadow_clones( "sv_use_shadow_clones", "1", FCVAR_REPLICATED | FCVAR_CHEAT ); //should we create shadow clones?

LINK_ENTITY_TO_CLASS( physicsshadowclone, CPhysicsShadowClone );

static CUtlVector<CPhysicsShadowClone *> s_ActiveShadowClones;
CUtlVector<CPhysicsShadowClone *> const &CPhysicsShadowClone::g_ShadowCloneList = s_ActiveShadowClones;
static bool s_IsShadowClone[MAX_EDICTS] = { false };

static CPhysicsShadowCloneLL *s_EntityClones[MAX_EDICTS] = { NULL };
struct ShadowCloneLLEntryManager
{
	CPhysicsShadowCloneLL m_ShadowCloneLLEntries[MAX_SHADOW_CLONE_COUNT];
	CPhysicsShadowCloneLL *m_pFreeShadowCloneLLEntries[MAX_SHADOW_CLONE_COUNT];
	int m_iUsedEntryIndex;

	ShadowCloneLLEntryManager( void )
	{
		m_iUsedEntryIndex = 0;
		for( int i = 0; i != MAX_SHADOW_CLONE_COUNT; ++i )
		{
			m_pFreeShadowCloneLLEntries[i] = &m_ShadowCloneLLEntries[i];
		}
	}

	inline CPhysicsShadowCloneLL *Alloc( void )
	{
		return m_pFreeShadowCloneLLEntries[m_iUsedEntryIndex++];
	}

	inline void Free( CPhysicsShadowCloneLL *pFree )
	{
		m_pFreeShadowCloneLLEntries[--m_iUsedEntryIndex] = pFree;
	}
};
static ShadowCloneLLEntryManager s_SCLLManager;


CPhysicsShadowClone::CPhysicsShadowClone( void )
{
	s_ActiveShadowClones.AddToTail( this );
}

CPhysicsShadowClone::~CPhysicsShadowClone( void )
{
	//VPhysicsDestroyObject();
	//GetEngineObject()->VPhysicsSetObject( NULL );
	s_ActiveShadowClones.FindAndRemove( this ); //also removed in UpdateOnRemove()
}

void CPhysicsShadowClone::UpdateOnRemove( void )
{
	GetEngineObject()->SetMoveType(MOVETYPE_NONE);
	GetEngineObject()->SetSolid(SOLID_NONE);
	GetEngineObject()->SetSolidFlags(0);
	GetEngineObject()->SetCollisionGroup(COLLISION_GROUP_NONE);
	CBaseEntity *pSource = GetEngineShadowClone()->GetClonedEntity();
	if( pSource )
	{
		CPhysicsShadowCloneLL *pCloneListHead = s_EntityClones[pSource->entindex()];
		Assert( pCloneListHead != NULL );

		CPhysicsShadowCloneLL *pFind = pCloneListHead;
		CPhysicsShadowCloneLL *pLast = pFind;
		while( pFind->pClone != this )
		{
			pLast = pFind;
			Assert( pFind->pNext != NULL );
			pFind = pFind->pNext;
		}

		if( pFind == pCloneListHead )
		{
			s_EntityClones[pSource->entindex()] = pFind->pNext;
		}
		else
		{
			pLast->pNext = pFind->pNext;
		}
		s_SCLLManager.Free( pFind );
	}
#ifdef _DEBUG
	else
	{
		//verify that it didn't weasel into a list somewhere and get left behind
		for( int i = 0; i != MAX_SHADOW_CLONE_COUNT; ++i )
		{
			CPhysicsShadowCloneLL *pCloneSearch = s_EntityClones[i];
			while( pCloneSearch )
			{
				Assert( pCloneSearch->pClone != this );
				pCloneSearch = pCloneSearch->pNext;
			}
		}
	}
#endif
	GetEngineObject()->VPhysicsDestroyObject();
	GetEngineObject()->VPhysicsSetObject( NULL );
	GetEngineShadowClone()->SetClonedEntity(NULL);
	s_ActiveShadowClones.FindAndRemove( this ); //also removed in Destructor
	Assert(s_IsShadowClone[entindex()] == true);
	s_IsShadowClone[entindex()] = false;
	BaseClass::UpdateOnRemove();
}

void CPhysicsShadowClone::Spawn( void )
{
	GetEngineObject()->AddFlag( FL_DONTTOUCH );
	GetEngineObject()->AddEffects( EF_NODRAW | EF_NOSHADOW | EF_NORECEIVESHADOW );

	GetEngineShadowClone()->FullSync( false );
	GetEngineShadowClone()->SetInAssumedSyncState(false);
	
	BaseClass::Spawn();

	s_IsShadowClone[entindex()] = true;
}

bool CPhysicsShadowClone::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	CBaseEntity *pClonedEntity = ((CPhysicsShadowClone*)(this))->GetEngineShadowClone()->GetClonedEntity();

	if( pClonedEntity )
		return pClonedEntity->ShouldCollide( collisionGroup, contentsMask );
	else
		return false;
}

bool CPhysicsShadowClone::TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& trace )
{
	return false;

	/*CBaseEntity *pSourceEntity = m_hClonedEntity.Get();
	if( pSourceEntity == NULL )
		return false;

	enginetrace->ClipRayToEntity( ray, fContentsMask, pSourceEntity, &trace );
	return trace.DidHit();*/
}

int	CPhysicsShadowClone::ObjectCaps( void )
{
	return ((BaseClass::ObjectCaps() | FCAP_DONT_SAVE) & ~(FCAP_FORCE_TRANSITION | FCAP_ACROSS_TRANSITION | FCAP_MUST_SPAWN | FCAP_SAVE_NON_NETWORKABLE));
}

//damage relays to source entity
bool CPhysicsShadowClone::PassesDamageFilter( const CTakeDamageInfo &info )
{
	CBaseEntity *pClonedEntity = GetEngineShadowClone()->GetClonedEntity();

	if( pClonedEntity )
		return pClonedEntity->PassesDamageFilter( info );
	else
		return BaseClass::PassesDamageFilter( info );
}

bool CPhysicsShadowClone::CanBeHitByMeleeAttack( CBaseEntity *pAttacker )
{
	CBaseEntity *pClonedEntity = GetEngineShadowClone()->GetClonedEntity();

	if( pClonedEntity )
		return pClonedEntity->CanBeHitByMeleeAttack( pAttacker );
	else
		return BaseClass::CanBeHitByMeleeAttack( pAttacker );
}

int CPhysicsShadowClone::OnTakeDamage( const CTakeDamageInfo &info )
{
	CBaseEntity *pClonedEntity = GetEngineShadowClone()->GetClonedEntity();

	if( pClonedEntity )
		return pClonedEntity->OnTakeDamage( info );
	else
		return BaseClass::OnTakeDamage( info );
}

int CPhysicsShadowClone::TakeHealth( float flHealth, int bitsDamageType )
{
	CBaseEntity *pClonedEntity = GetEngineShadowClone()->GetClonedEntity();

	if( pClonedEntity )
		return pClonedEntity->TakeHealth( flHealth, bitsDamageType );
	else
		return BaseClass::TakeHealth( flHealth, bitsDamageType );
}

void CPhysicsShadowClone::Event_Killed( const CTakeDamageInfo &info )
{
	CBaseEntity *pClonedEntity = GetEngineShadowClone()->GetClonedEntity();

	if( pClonedEntity )
		pClonedEntity->Event_Killed( info );
	else
		BaseClass::Event_Killed( info );
}

CPhysicsShadowClone *CPhysicsShadowClone::CreateShadowClone( IPhysicsEnvironment *pInPhysicsEnvironment, EHANDLE hEntToClone, const char *szDebugMarker, const matrix3x4_t *pTransformationMatrix /*= NULL*/ )
{
	AssertMsg( szDebugMarker != NULL, "All shadow clones must have a debug marker for where it came from in debug builds." );

	if( !sv_use_shadow_clones.GetBool() )
		return NULL;

	CBaseEntity *pClonedEntity = hEntToClone.Get();
	if( pClonedEntity == NULL )
		return NULL;

	AssertMsg( IsShadowClone( pClonedEntity ) == false, "Shouldn't attempt to clone clones" );

	if( pClonedEntity->GetEngineObject()->IsMarkedForDeletion() )
		return NULL;

	//if( pClonedEntity->IsPlayer() )
	//	return NULL;

	IPhysicsObject *pPhysics = pClonedEntity->VPhysicsGetObject();

	if( pPhysics == NULL )
		return NULL;

	if( pPhysics->IsStatic() )
		return NULL;

	if( pClonedEntity->GetEngineObject()->GetSolid() == SOLID_BSP )
		return NULL;

	if( pClonedEntity->GetEngineObject()->GetSolidFlags() & (FSOLID_NOT_SOLID | FSOLID_TRIGGER) )
		return NULL;

	if( pClonedEntity->GetEngineObject()->GetFlags() & (FL_WORLDBRUSH | FL_STATICPROP) )
		return NULL;

	/*if( FClassnameIs( pClonedEntity, "func_door" ) )
	{
		//only clone func_door's that are in front of the portal
		
		return NULL;
	}*/

	// Too many shadow clones breaks the game (too many entities)
	if( g_iShadowCloneCount >= MAX_SHADOW_CLONE_COUNT )
	{
		AssertMsg( false, "Too many shadow clones, consider upping the limit or reducing the level's physics props" );
		return NULL;
	}
	++g_iShadowCloneCount;

	CPhysicsShadowClone *pClone = (CPhysicsShadowClone*)gEntList.CreateEntityByName("physicsshadowclone");
	s_IsShadowClone[pClone->entindex()] = true;
	pClone->GetEngineShadowClone()->SetOwnerEnvironment(pInPhysicsEnvironment);
	pClone->GetEngineShadowClone()->SetClonedEntity(hEntToClone);
	DBG_CODE_NOSCOPE( pClone->m_szDebugMarker = szDebugMarker; );

	CPhysicsShadowCloneLL *pCloneLLEntry = s_SCLLManager.Alloc();
	pCloneLLEntry->pClone = pClone;
	pCloneLLEntry->pNext = s_EntityClones[pClonedEntity->entindex()];
	s_EntityClones[pClonedEntity->entindex()] = pCloneLLEntry;

	if( pTransformationMatrix )
	{
		pClone->GetEngineShadowClone()->SetCloneTransformationMatrix(*pTransformationMatrix);
	}

	DispatchSpawn( pClone );

	return pClone;
}

void CPhysicsShadowClone::ReleaseShadowClone(CPhysicsShadowClone* pShadowClone)
{
	UTIL_Remove(pShadowClone);

	//Too many shadow clones breaks the game (too many entities)
	--g_iShadowCloneCount;
}


void CPhysicsShadowClone::FullSyncAllClones( void )
{
	for( int i = s_ActiveShadowClones.Count(); --i >= 0; )
	{
		s_ActiveShadowClones[i]->GetEngineShadowClone()->FullSync( true );
	}
}

void CPhysicsShadowClone::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	//the baseclass just screenshakes, makes sounds, and outputs dust, we rely on the original entity to do this when applicable
}

bool CPhysicsShadowClone::IsShadowClone( const CBaseEntity *pEntity )
{
	return s_IsShadowClone[pEntity->entindex()];
}

CPhysicsShadowCloneLL *CPhysicsShadowClone::GetClonesOfEntity( const CBaseEntity *pEntity )
{
	return s_EntityClones[pEntity->entindex()];
}

bool CTraceFilterTranslateClones::ShouldHitEntity( IHandleEntity *pEntity, int contentsMask )
{
	CBaseEntity *pEnt = EntityFromEntityHandle( pEntity );
	if( CPhysicsShadowClone::IsShadowClone( pEnt ) )
	{
		CBaseEntity *pClonedEntity = ((CPhysicsShadowClone *)pEnt)->GetEngineShadowClone()->GetClonedEntity();
		CProp_Portal *pSimulator = CProp_Portal::GetSimulatorThatOwnsEntity( pClonedEntity );
		if( pSimulator->m_EntFlags[pClonedEntity->entindex()] & PSEF_IS_IN_PORTAL_HOLE )
			return m_pActualFilter->ShouldHitEntity( pClonedEntity, contentsMask );
		else
			return false;
	}
	else
	{
		return m_pActualFilter->ShouldHitEntity( pEntity, contentsMask );
	}
}

TraceType_t	CTraceFilterTranslateClones::GetTraceType() const
{
	return m_pActualFilter->GetTraceType();
}



