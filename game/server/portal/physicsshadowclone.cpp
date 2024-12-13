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


static int g_iShadowCloneCount = 0;
ConVar sv_use_shadow_clones( "sv_use_shadow_clones", "1", FCVAR_REPLICATED | FCVAR_CHEAT ); //should we create shadow clones?

LINK_ENTITY_TO_CLASS( physicsshadowclone, CPhysicsShadowClone );


//static bool s_IsShadowClone[MAX_EDICTS] = { false };




CPhysicsShadowClone::CPhysicsShadowClone( void )
{
}

CPhysicsShadowClone::~CPhysicsShadowClone( void )
{
	//VPhysicsDestroyObject();
	//GetEngineObject()->VPhysicsSetObject( NULL );
}

void CPhysicsShadowClone::UpdateOnRemove( void )
{
	GetEngineObject()->SetMoveType(MOVETYPE_NONE);
	GetEngineObject()->SetSolid(SOLID_NONE);
	GetEngineObject()->SetSolidFlags(0);
	GetEngineObject()->SetCollisionGroup(COLLISION_GROUP_NONE);
	GetEngineObject()->VPhysicsDestroyObject();
	GetEngineObject()->VPhysicsSetObject( NULL );
	GetEngineShadowClone()->SetClonedEntity(NULL);
	//Assert(s_IsShadowClone[entindex()] == true);
	//s_IsShadowClone[entindex()] = false;
	BaseClass::UpdateOnRemove();
}

void CPhysicsShadowClone::Spawn( void )
{
	GetEngineObject()->AddFlag( FL_DONTTOUCH );
	GetEngineObject()->AddEffects( EF_NODRAW | EF_NOSHADOW | EF_NORECEIVESHADOW );

	GetEngineShadowClone()->FullSync( false );
	GetEngineShadowClone()->SetInAssumedSyncState(false);
	
	BaseClass::Spawn();

	//s_IsShadowClone[entindex()] = true;
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

	AssertMsg(pClonedEntity->GetEngineObject()->IsShadowClone() == false, "Shouldn't attempt to clone clones" );

	if( pClonedEntity->GetEngineObject()->IsMarkedForDeletion() )
		return NULL;

	//if( pClonedEntity->IsPlayer() )
	//	return NULL;

	IPhysicsObject *pPhysics = pClonedEntity->GetEngineObject()->VPhysicsGetObject();

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
	//s_IsShadowClone[pClone->entindex()] = true;
	pClone->GetEngineShadowClone()->SetOwnerEnvironment(pInPhysicsEnvironment);
	pClone->GetEngineShadowClone()->SetClonedEntity(hEntToClone);
	DBG_CODE_NOSCOPE( pClone->m_szDebugMarker = szDebugMarker; );



	if( pTransformationMatrix )
	{
		pClone->GetEngineShadowClone()->SetCloneTransformationMatrix(*pTransformationMatrix);
	}

	DispatchSpawn( pClone );

	return pClone;
}

void CPhysicsShadowClone::ReleaseShadowClone(CPhysicsShadowClone* pShadowClone)
{
	gEntList.DestroyEntity(pShadowClone);

	//Too many shadow clones breaks the game (too many entities)
	--g_iShadowCloneCount;
}




void CPhysicsShadowClone::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	//the baseclass just screenshakes, makes sounds, and outputs dust, we rely on the original entity to do this when applicable
}

//bool CPhysicsShadowClone::IsShadowClone( const CBaseEntity *pEntity )
//{
//	return s_IsShadowClone[pEntity->entindex()];
//}

//CPhysicsShadowCloneLL *CPhysicsShadowClone::GetClonesOfEntity( const CBaseEntity *pEntity )
//{
//	return s_EntityClones[pEntity->entindex()];
//}

bool CTraceFilterTranslateClones::ShouldHitEntity( IHandleEntity *pEntity, int contentsMask )
{
	CBaseEntity *pEnt = EntityFromEntityHandle( pEntity );
	if(pEnt->GetEngineObject()->IsShadowClone() )
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



