//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Interface layer for ipion IVP physics.
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//


#include "cbase.h"
#include "coordsize.h"
//#include "entitylist.h"
#include "vcollide_parse.h"
#include "soundenvelope.h"
#include "game.h"
#include "utlvector.h"
#include "init_factory.h"
#include "igamesystem.h"
#include "hierarchy.h"
#include "IEffects.h"
#include "engine/IEngineSound.h"
#include "world.h"
#include "decals.h"
#include "physics_fx.h"
#include "vphysics_sound.h"
#include "vphysics/vehicles.h"
#include "game/server/vehicle_sounds.h"
#include "movevars_shared.h"
#include "physics_saverestore.h"
#include "tier0/vprof.h"
#include "engine/IStaticPropMgr.h"
#include "physics_prop_ragdoll.h"
#if HL2_EPISODIC
#include "particle_parse.h"
#endif
#include "vphysics/object_hash.h"
#include "vphysics/collision_set.h"
#include "vphysics/friction.h"
#include "fmtstr.h"
#include "physics_npc_solver.h"
#include "physics_collisionevent.h"
#include "vphysics/performance.h"
#include "positionwatcher.h"
#include "tier1/callqueue.h"
#include "vphysics/constraints.h"

#ifdef PORTAL
#include "portal_physics_collisionevent.h"
#include "physicsshadowclone.h"
#include "PortalSimulation.h"
#include "prop_portal.h"
#endif

#include "physics_shared.h"
#include "te_effect_dispatch.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//#ifdef PORTAL
//CEntityList *g_pShadowEntities_Main = NULL;
//#endif



static bool IsDebris( int collisionGroup );

#if _DEBUG
ConVar phys_dontprintint( "phys_dontprintint", "1", FCVAR_NONE, "Don't print inter-penetration warnings." );
#endif



// a little debug wrapper to help fix bugs when entity pointers get trashed
#if 0
struct physcheck_t
{
	IPhysicsObject *pPhys;
	char			string[512];
};

CUtlVector< physcheck_t > physCheck;

void PhysCheckAdd( IPhysicsObject *pPhys, const char *pString )
{
	physcheck_t tmp;
	tmp.pPhys = pPhys;
	Q_strncpy( tmp.string, pString ,sizeof(tmp.string));
	physCheck.AddToTail( tmp );
}

const char *PhysCheck( IPhysicsObject *pPhys )
{
	for ( int i = 0; i < physCheck.Size(); i++ )
	{
		if ( physCheck[i].pPhys == pPhys )
			return physCheck[i].string;
	}

	return "unknown";
}
#endif

// vehicle wheels can only collide with things that can't get stuck in them during game physics
// because they aren't in the game physics world at present
static bool WheelCollidesWith( IPhysicsObject *pObj, CBaseEntity *pEntity )
{
#if defined( INVASION_DLL )
	if ( pEntity->GetEngineObject()->GetCollisionGroup() == TFCOLLISION_GROUP_OBJECT )
		return false;
#endif

	// Cull against interactive debris
	if ( pEntity->GetEngineObject()->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE_DEBRIS )
		return false;

	// Hit physics ents
	if ( pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_PUSH || pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS || pObj->IsStatic() )
		return true;

	return false;
}

CCollisionEvent::CCollisionEvent()
{
	m_inCallback = 0;
	m_bBufferTouchEvents = false;
	m_lastTickFrictionError = 0;
}

int CCollisionEvent::ShouldCollide( IPhysicsObject *pObj0, IPhysicsObject *pObj1, void *pGameData0, void *pGameData1 )
#if _DEBUG
{
	int x0 = ShouldCollide_2(pObj0, pObj1, pGameData0, pGameData1);
	int x1 = ShouldCollide_2(pObj1, pObj0, pGameData1, pGameData0);
	Assert(x0==x1);
	return x0;
}
int CCollisionEvent::ShouldCollide_2( IPhysicsObject *pObj0, IPhysicsObject *pObj1, void *pGameData0, void *pGameData1 )
#endif
{
	CallbackContext check(this);

	CBaseEntity *pEntity0 = static_cast<CBaseEntity *>(pGameData0);
	CBaseEntity *pEntity1 = static_cast<CBaseEntity *>(pGameData1);

	if ( !pEntity0 || !pEntity1 )
		return 1;

	unsigned short gameFlags0 = pObj0->GetGameFlags();
	unsigned short gameFlags1 = pObj1->GetGameFlags();

	if ( pEntity0 == pEntity1 )
	{
		// allow all-or-nothing per-entity disable
		if ( (gameFlags0 | gameFlags1) & FVPHYSICS_NO_SELF_COLLISIONS )
			return 0;

		IPhysicsCollisionSet *pSet = gEntList.Physics()->FindCollisionSet( pEntity0->GetEngineObject()->GetModelIndex() );
		if ( pSet )
			return pSet->ShouldCollide( pObj0->GetGameIndex(), pObj1->GetGameIndex() );

		return 1;
	}

	// objects that are both constrained to the world don't collide with each other
	if ( (gameFlags0 & gameFlags1) & FVPHYSICS_CONSTRAINT_STATIC )
	{
		return 0;
	}

	// Special collision rules for vehicle wheels
	// Their entity collides with stuff using the normal rules, but they
	// have different rules than the vehicle body for various reasons.
	// sort of a hack because we don't have spheres to represent them in the game
	// world for speculative collisions.
	if ( pObj0->GetCallbackFlags() & CALLBACK_IS_VEHICLE_WHEEL )
	{
		if ( !WheelCollidesWith( pObj1, pEntity1 ) )
			return false;
	}
	if ( pObj1->GetCallbackFlags() & CALLBACK_IS_VEHICLE_WHEEL )
	{
		if ( !WheelCollidesWith( pObj0, pEntity0 ) )
			return false;
	}

	if ( pEntity0->ForceVPhysicsCollide( pEntity1 ) || pEntity1->ForceVPhysicsCollide( pEntity0 ) )
		return 1;

	if ( pEntity0->entindex()!=-1 && pEntity1->entindex()!=-1 )
	{
		// don't collide with your owner
		if ( pEntity0->GetOwnerEntity() == pEntity1 || pEntity1->GetOwnerEntity() == pEntity0 )
			return 0;
	}

	if ( pEntity0->GetEngineObject()->GetMoveParent() || pEntity1->GetEngineObject()->GetMoveParent() )
	{
		CBaseEntity *pParent0 = pEntity0->GetEngineObject()->GetRootMoveParent()->GetOuter();
		CBaseEntity *pParent1 = pEntity1->GetEngineObject()->GetRootMoveParent()->GetOuter();
		
		// NOTE: Don't let siblings/parents collide.  If you want this behavior, do it
		// with constraints, not hierarchy!
		if ( pParent0 == pParent1 )
			return 0;

		if (gEntList.PhysGetEntityCollisionHash()->IsObjectPairInHash( pParent0, pParent1 ) )
			return 0;

		IPhysicsObject *p0 = pParent0->GetEngineObject()->VPhysicsGetObject();
		IPhysicsObject *p1 = pParent1->GetEngineObject()->VPhysicsGetObject();
		if ( p0 && p1 )
		{
			if (gEntList.PhysGetEntityCollisionHash()->IsObjectPairInHash( p0, p1 ) )
				return 0;
		}
	}

	int solid0 = pEntity0->GetEngineObject()->GetSolid();
	int solid1 = pEntity1->GetEngineObject()->GetSolid();
	int nSolidFlags0 = pEntity0->GetEngineObject()->GetSolidFlags();
	int nSolidFlags1 = pEntity1->GetEngineObject()->GetSolidFlags();

	int movetype0 = pEntity0->GetEngineObject()->GetMoveType();
	int movetype1 = pEntity1->GetEngineObject()->GetMoveType();

	// entities with non-physical move parents or entities with MOVETYPE_PUSH
	// are considered as "AI movers".  They are unchanged by collision; they exert
	// physics forces on the rest of the system.
	bool aiMove0 = (movetype0==MOVETYPE_PUSH) ? true : false;
	bool aiMove1 = (movetype1==MOVETYPE_PUSH) ? true : false;

	if ( pEntity0->GetEngineObject()->GetMoveParent() )
	{
		// if the object & its parent are both MOVETYPE_VPHYSICS, then this must be a special case
		// like a prop_ragdoll_attached
		if ( !(movetype0 == MOVETYPE_VPHYSICS && pEntity0->GetEngineObject()->GetRootMoveParent()->GetMoveType() == MOVETYPE_VPHYSICS))
		{
			aiMove0 = true;
		}
	}
	if ( pEntity1->GetEngineObject()->GetMoveParent() )
	{
		// if the object & its parent are both MOVETYPE_VPHYSICS, then this must be a special case.
		if ( !(movetype1 == MOVETYPE_VPHYSICS && pEntity1->GetEngineObject()->GetRootMoveParent()->GetMoveType() == MOVETYPE_VPHYSICS))
		{
			aiMove1 = true;
		}
	}

	// AI movers don't collide with the world/static/pinned objects or other AI movers
	if ( (aiMove0 && !pObj1->IsMoveable()) ||
		(aiMove1 && !pObj0->IsMoveable()) ||
		(aiMove0 && aiMove1) )
		return 0;

	// two objects under shadow control should not collide.  The AI will figure it out
	if ( pObj0->GetShadowController() && pObj1->GetShadowController() )
		return 0;

	// BRJ 1/24/03
	// You can remove the assert if it's problematic; I *believe* this condition
	// should be met, but I'm not sure.
	//Assert ( (solid0 != SOLID_NONE) && (solid1 != SOLID_NONE) );
	if ( (solid0 == SOLID_NONE) || (solid1 == SOLID_NONE) )
		return 0;

	// not solid doesn't collide with anything
	if ( (nSolidFlags0|nSolidFlags1) & FSOLID_NOT_SOLID )
	{
		// might be a vphysics trigger, collide with everything but "not solid"
		if ( pObj0->IsTrigger() && !(nSolidFlags1 & FSOLID_NOT_SOLID) )
			return 1;
		if ( pObj1->IsTrigger() && !(nSolidFlags0 & FSOLID_NOT_SOLID) )
			return 1;

		return 0;
	}
	
	if ( (nSolidFlags0 & FSOLID_TRIGGER) && 
		!(solid1 == SOLID_VPHYSICS || solid1 == SOLID_BSP || movetype1 == MOVETYPE_VPHYSICS) )
		return 0;

	if ( (nSolidFlags1 & FSOLID_TRIGGER) && 
		!(solid0 == SOLID_VPHYSICS || solid0 == SOLID_BSP || movetype0 == MOVETYPE_VPHYSICS) )
		return 0;

	if ( !g_pGameRules->ShouldCollide( pEntity0->GetEngineObject()->GetCollisionGroup(), pEntity1->GetEngineObject()->GetCollisionGroup() ) )
		return 0;

	// check contents
	if ( !(pObj0->GetContents() & pEntity1->PhysicsSolidMaskForEntity()) || !(pObj1->GetContents() & pEntity0->PhysicsSolidMaskForEntity()) )
		return 0;

	if (gEntList.PhysGetEntityCollisionHash()->IsObjectPairInHash( pGameData0, pGameData1 ) )
		return 0;

	if (gEntList.PhysGetEntityCollisionHash()->IsObjectPairInHash( pObj0, pObj1 ) )
		return 0;

	return 1;
}

bool FindMaxContact( IPhysicsObject *pObject, float minForce, IPhysicsObject **pOtherObject, Vector *contactPos, Vector *pForce )
{
	float mass = pObject->GetMass();
	float maxForce = minForce;
	*pOtherObject = NULL;
	IPhysicsFrictionSnapshot *pSnapshot = pObject->CreateFrictionSnapshot();
	while ( pSnapshot->IsValid() )
	{
		IPhysicsObject *pOther = pSnapshot->GetObject(1);
		if ( pOther->IsMoveable() && pOther->GetMass() > mass )
		{
			float force = pSnapshot->GetNormalForce();
			if ( force > maxForce )
			{
				*pOtherObject = pOther;
				pSnapshot->GetContactPoint( *contactPos );
				pSnapshot->GetSurfaceNormal( *pForce );
				*pForce *= force;
			}
		}
		pSnapshot->NextFrictionData();
	}
	pObject->DestroyFrictionSnapshot( pSnapshot );
	if ( *pOtherObject )
		return true;

	return false;
}

bool CCollisionEvent::ShouldFreezeObject( IPhysicsObject *pObject )
{
	// for now, don't apply a per-object limit to ai MOVETYPE_PUSH objects
	// NOTE: If this becomes a problem (too many collision checks this tick) we should add a path
	// to inform the logic in VPhysicsUpdatePusher() about the limit being applied so 
	// that it doesn't falsely block the object when it's simply been temporarily frozen
	// for performance reasons
	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	if ( pEntity )
	{
		if (pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_PUSH )
			return false;
		
		// don't limit vehicle collisions either, limit can make breaking through a pile of breakable
		// props very hitchy
		if (pEntity->GetServerVehicle() && !(pObject->GetCallbackFlags() & CALLBACK_IS_VEHICLE_WHEEL))
			return false;
	}

	// if we're freezing a debris object, then it's probably due to some kind of solver issue
	// usually this is a large object resting on the debris object in question which is not
	// very stable.
	// After doing the experiment of constraining the dynamic range of mass while solving friction
	// contacts, I like the results of this tradeoff better.  So damage or remove the debris object
	// wherever possible once we hit this case:
	if ( IsDebris( pEntity->GetEngineObject()->GetCollisionGroup()) && !pEntity->IsNPC() )
	{
		IPhysicsObject *pOtherObject = NULL;
		Vector contactPos;
		Vector force;
		// find the contact with the moveable object applying the most contact force
		if ( FindMaxContact( pObject, pObject->GetMass() * 10, &pOtherObject, &contactPos, &force ) )
		{
			CBaseEntity *pOther = static_cast<CBaseEntity *>(pOtherObject->GetGameData());
			// this object can take damage, crush it
			if ( pEntity->m_takedamage > DAMAGE_EVENTS_ONLY )
			{
				CTakeDamageInfo dmgInfo( pOther, pOther, force, contactPos, force.Length() * 0.1f, DMG_CRUSH );
				gEntList.PhysCallbackDamage( pEntity, dmgInfo );
			}
			else
			{
				// can't be damaged, so do something else:
				if ( pEntity->IsGib() )
				{
					// it's always safe to delete gibs, so kill this one to avoid simulation problems
					gEntList.PhysCallbackRemove( pEntity );
				}
				else
				{
					// not a gib, create a solver:
					// UNDONE: Add a property to override this in gameplay critical scenarios?
					gEntList.PhysGetPostSimulationQueue().QueueCall( EntityPhysics_CreateSolver, pOther, pEntity, true, 1.0f );
				}
			}
		}
	}
	return true;
}

bool CCollisionEvent::ShouldFreezeContacts( IPhysicsObject **pObjectList, int objectCount )
{
	if ( m_lastTickFrictionError > gpGlobals->tickcount || m_lastTickFrictionError < (gpGlobals->tickcount-1) )
	{
		DevWarning("Performance Warning: large friction system (%d objects)!!!\n", objectCount );
#if _DEBUG
		for ( int i = 0; i < objectCount; i++ )
		{
			CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObjectList[i]->GetGameData());
			pEntity->m_debugOverlays |= OVERLAY_ABSBOX_BIT | OVERLAY_PIVOT_BIT;
		}
#endif
	}
	m_lastTickFrictionError = gpGlobals->tickcount;
	return false;
}

// NOTE: these are fully edge triggered events 
// called when an object wakes up (starts simulating)
void CCollisionEvent::ObjectWake( IPhysicsObject *pObject )
{
	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	if ( pEntity && pEntity->GetEngineObject()->HasDataObjectType( VPHYSICSWATCHER ) )
	{
		//ReportVPhysicsStateChanged( pObject, pEntity, true );
		pEntity->NotifyVPhysicsStateChanged(pObject, true);
	}
}
// called when an object goes to sleep (no longer simulating)
void CCollisionEvent::ObjectSleep( IPhysicsObject *pObject )
{
	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	if ( pEntity && pEntity->GetEngineObject()->HasDataObjectType( VPHYSICSWATCHER ) )
	{
		//ReportVPhysicsStateChanged( pObject, pEntity, false );
		pEntity->NotifyVPhysicsStateChanged(pObject, false);
	}
}



static void ReportPenetration( CBaseEntity *pEntity, float duration )
{
	if ( pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		if ( g_pDeveloper->GetInt() > 1 )
		{
			pEntity->m_debugOverlays |= OVERLAY_ABSBOX_BIT;
		}

		pEntity->AddTimedOverlay( UTIL_VarArgs("VPhysics Penetration Error (%s)!", pEntity->GetDebugName()), duration );
	}
}

static bool IsDebris( int collisionGroup )
{
	switch ( collisionGroup )
	{
	case COLLISION_GROUP_DEBRIS:
	case COLLISION_GROUP_INTERACTIVE_DEBRIS:
	case COLLISION_GROUP_DEBRIS_TRIGGER:
		return true;
	default:
		break;
	}
	return false;
}

static void UpdateEntityPenetrationFlag( CBaseEntity *pEntity, bool isPenetrating )
{
	if ( !pEntity )
		return;
	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEntity->GetEngineObject()->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
	for ( int i = 0; i < count; i++ )
	{
		if ( !pList[i]->IsStatic() )
		{
			if ( isPenetrating )
			{
				PhysSetGameFlags( pList[i], FVPHYSICS_PENETRATING );
			}
			else
			{
				PhysClearGameFlags( pList[i], FVPHYSICS_PENETRATING );
			}
		}
	}
}

void CCollisionEvent::GetListOfPenetratingEntities( CBaseEntity *pSearch, CUtlVector<CBaseEntity *> &list )
{
	for ( int i = m_penetrateEvents.Count()-1; i >= 0; --i )
	{
		if ( m_penetrateEvents[i].hEntity0 == pSearch && m_penetrateEvents[i].hEntity1.Get() != NULL )
		{
			list.AddToTail( m_penetrateEvents[i].hEntity1 );
		}
		else if ( m_penetrateEvents[i].hEntity1 == pSearch && m_penetrateEvents[i].hEntity0.Get() != NULL )
		{
			list.AddToTail( m_penetrateEvents[i].hEntity0 );
		}
	}
}

void CCollisionEvent::UpdatePenetrateEvents( void )
{
	for ( int i = m_penetrateEvents.Count()-1; i >= 0; --i )
	{
		CBaseEntity *pEntity0 = m_penetrateEvents[i].hEntity0;
		CBaseEntity *pEntity1 = m_penetrateEvents[i].hEntity1;

		if ( m_penetrateEvents[i].collisionState == COLLSTATE_TRYDISABLE )
		{
			if ( pEntity0 && pEntity1 )
			{
				IPhysicsObject *pObj0 = pEntity0->GetEngineObject()->VPhysicsGetObject();
				if ( pObj0 )
				{
					gEntList.PhysForceEntityToSleep( pEntity0, pObj0 );
				}
				IPhysicsObject *pObj1 = pEntity1->GetEngineObject()->VPhysicsGetObject();
				if ( pObj1 )
				{
					gEntList.PhysForceEntityToSleep( pEntity1, pObj1 );
				}
				m_penetrateEvents[i].collisionState = COLLSTATE_DISABLED;
				continue;
			}
			// missing entity or object, clear event
		}
		else if ( m_penetrateEvents[i].collisionState == COLLSTATE_TRYNPCSOLVER )
		{
			if ( pEntity0 && pEntity1 )
			{
				CAI_BaseNPC *pNPC = pEntity0->MyNPCPointer();
				CBaseEntity *pBlocker = pEntity1;
				if ( !pNPC )
				{
					pNPC = pEntity1->MyNPCPointer();
					Assert(pNPC);
					pBlocker = pEntity0;
				}
				NPCPhysics_CreateSolver( pNPC, pBlocker, true, 1.0f );
			}
			// transferred to solver, clear event
		}
		else if ( m_penetrateEvents[i].collisionState == COLLSTATE_TRYENTITYSOLVER )
		{
			if ( pEntity0 && pEntity1 )
			{
				if ( !IsDebris(pEntity1->GetEngineObject()->GetCollisionGroup()) || pEntity1->GetEngineObject()->GetMoveType() != MOVETYPE_VPHYSICS )
				{
					CBaseEntity *pTmp = pEntity0;
					pEntity0 = pEntity1;
					pEntity1 = pTmp;
				}
				EntityPhysics_CreateSolver( pEntity0, pEntity1, true, 1.0f );
			}
			// transferred to solver, clear event
		}
		else if ( gpGlobals->curtime - m_penetrateEvents[i].timeStamp > 1.0 )
		{
			if ( m_penetrateEvents[i].collisionState == COLLSTATE_DISABLED )
			{
				if ( pEntity0 && pEntity1 )
				{
					IPhysicsObject *pObj0 = pEntity0->GetEngineObject()->VPhysicsGetObject();
					IPhysicsObject *pObj1 = pEntity1->GetEngineObject()->VPhysicsGetObject();
					if ( pObj0 && pObj1 )
					{
						m_penetrateEvents[i].collisionState = COLLSTATE_ENABLED;
						continue;
					}
				}
			}
			// haven't penetrated for 1 second, so remove
		}
		else
		{
			// recent timestamp, don't remove the event yet
			continue;
		}
		// done, clear event
		m_penetrateEvents.FastRemove(i);
		UpdateEntityPenetrationFlag( pEntity0, false );
		UpdateEntityPenetrationFlag( pEntity1, false );
	}
}

penetrateevent_t &CCollisionEvent::FindOrAddPenetrateEvent( CBaseEntity *pEntity0, CBaseEntity *pEntity1 )
{
	int index = -1;
	for ( int i = m_penetrateEvents.Count()-1; i >= 0; --i )
	{
		if ( m_penetrateEvents[i].hEntity0.Get() == pEntity0 && m_penetrateEvents[i].hEntity1.Get() == pEntity1 )
		{
			index = i;
			break;
		}
	}
	if ( index < 0 )
	{
		index = m_penetrateEvents.AddToTail();
		penetrateevent_t &event = m_penetrateEvents[index];
		event.hEntity0 = pEntity0;
		event.hEntity1 = pEntity1;
		event.startTime = gpGlobals->curtime;
		event.collisionState = COLLSTATE_ENABLED;
		UpdateEntityPenetrationFlag( pEntity0, true );
		UpdateEntityPenetrationFlag( pEntity1, true );
	}
	penetrateevent_t &event = m_penetrateEvents[index];
	event.timeStamp = gpGlobals->curtime;
	return event;
}



static ConVar phys_penetration_error_time( "phys_penetration_error_time", "10", 0, "Controls the duration of vphysics penetration error boxes." );

static bool CanResolvePenetrationWithNPC( CBaseEntity *pEntity, IPhysicsObject *pObject )
{
	if ( pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		// hinged objects won't be able to be pushed out anyway, so don't try the npc solver
		if ( !pObject->IsHinged() && !pObject->IsAttachedToConstraint(true) )
		{
			if ( pObject->IsMoveable() || pEntity->GetServerVehicle() )
				return true;
		}
	}
	return false;
}

int CCollisionEvent::ShouldSolvePenetration( IPhysicsObject *pObj0, IPhysicsObject *pObj1, void *pGameData0, void *pGameData1, float dt )
{
	CallbackContext check(this);
	
	// Pointers to the entity for each physics object
	CBaseEntity *pEntity0 = static_cast<CBaseEntity *>(pGameData0);
	CBaseEntity *pEntity1 = static_cast<CBaseEntity *>(pGameData1);

	// this can get called as entities are being constructed on the other side of a game load or level transition
	// Some entities may not be fully constructed, so don't call into their code until the level is running
	if ( gEntList.PhysIsPaused())
		return true;

	// solve it yourself here and return 0, or have the default implementation do it
	if ( pEntity0 > pEntity1 )
	{
		// swap sort
		CBaseEntity *pTmp = pEntity0;
		pEntity0 = pEntity1;
		pEntity1 = pTmp;
		IPhysicsObject *pTmpObj = pObj0;
		pObj0 = pObj1;
		pObj1 = pTmpObj;
	}
	if ( pEntity0 == pEntity1 )
	{
		if ( pObj0->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL )
		{
			DevMsg(2, "Solving ragdoll self penetration! %s (%s) (%d v %d)\n", pObj0->GetName(), pEntity0->GetDebugName(), pObj0->GetGameIndex(), pObj1->GetGameIndex() );
			ragdoll_t *pRagdoll = Ragdoll_GetRagdoll( pEntity0 );
			pRagdoll->pGroup->SolvePenetration( pObj0, pObj1 );
			return false;
		}
	}
	penetrateevent_t &event = FindOrAddPenetrateEvent( pEntity0, pEntity1 );
	float eventTime = gpGlobals->curtime - event.startTime;
	 
	// NPC vs. physics object.  Create a game DLL solver and remove this event
	if ( (pEntity0->MyNPCPointer() && CanResolvePenetrationWithNPC(pEntity1, pObj1)) ||
		(pEntity1->MyNPCPointer() && CanResolvePenetrationWithNPC(pEntity0, pObj0)) )
	{ 
		event.collisionState = COLLSTATE_TRYNPCSOLVER;
	}
  
	if ( (IsDebris( pEntity0->GetEngineObject()->GetCollisionGroup() ) && !pObj1->IsStatic()) || (IsDebris( pEntity1->GetEngineObject()->GetCollisionGroup() ) && !pObj0->IsStatic()) )
	{
		if ( eventTime > 0.5f )
		{
			//Msg("Debris stuck in non-static!\n");
			event.collisionState = COLLSTATE_TRYENTITYSOLVER;
		}
	}
#if _DEBUG
	if ( phys_dontprintint.GetBool() == false )
	{
		const char *pName1 = STRING(pEntity0->GetEngineObject()->GetModelName());
		const char *pName2 = STRING(pEntity1->GetEngineObject()->GetModelName());
		if ( pEntity0 == pEntity1 )
		{
			int index0 = gEntList.PhysGetCollision()->CollideIndex( pObj0->GetCollide() );
			int index1 = gEntList.PhysGetCollision()->CollideIndex( pObj1->GetCollide() );
			DevMsg(1, "***Inter-penetration on %s (%d & %d) (%.0f, %.0f)\n", pName1?pName1:"(null)", index0, index1, gpGlobals->curtime, eventTime );
		}
		else
		{
			DevMsg(1, "***Inter-penetration between %s(%s) AND %s(%s) (%.0f, %.0f)\n", pName1?pName1:"(null)", pEntity0->GetDebugName(), pName2?pName2:"(null)", pEntity1->GetDebugName(), gpGlobals->curtime, eventTime );
		}
	}
#endif

	if ( eventTime > 3 )
	{
		// don't report penetrations on ragdolls with themselves, or outside of developer mode
		if ( g_pDeveloper->GetInt() && pEntity0 != pEntity1 )
		{
			ReportPenetration( pEntity0, phys_penetration_error_time.GetFloat() );
			ReportPenetration( pEntity1, phys_penetration_error_time.GetFloat() );
		}
		event.startTime = gpGlobals->curtime;
		// don't put players or game physics controlled objects to sleep
		if ( !pEntity0->IsPlayer() && !pEntity1->IsPlayer() && !pObj0->GetShadowController() && !pObj1->GetShadowController() )
		{
			// two objects have been stuck for more than 3 seconds, try disabling simulation
			event.collisionState = COLLSTATE_TRYDISABLE;
			return false;
		}
	}


	return true;
}

static int BestAxisMatchingNormal(const matrix3x4_t& matrix, const Vector& normal)
{
	float bestDot = -1;
	int best = 0;
	for (int i = 0; i < 3; i++)
	{
		Vector tmp;
		MatrixGetColumn(matrix, i, tmp);
		float dot = fabs(DotProduct(tmp, normal));
		if (dot > bestDot)
		{
			bestDot = dot;
			best = i;
		}
	}

	return best;
}

void PhysicsSplash(IPhysicsFluidController* pFluid, IPhysicsObject* pObject, CBaseEntity* pEntity)
{
	Vector normal;
	float dist;
	pFluid->GetSurfacePlane(&normal, &dist);

	const matrix3x4_t& matrix = pEntity->GetEngineObject()->EntityToWorldTransform();

	// Find the local axis that best matches the water surface normal
	int bestAxis = BestAxisMatchingNormal(matrix, normal);

	Vector tangent, binormal;
	MatrixGetColumn(matrix, (bestAxis + 1) % 3, tangent);
	binormal = CrossProduct(normal, tangent);
	VectorNormalize(binormal);
	tangent = CrossProduct(binormal, normal);
	VectorNormalize(tangent);

	// Now we have a basis tangent to the surface that matches the object's local orientation as well as possible
	// compute an OBB using this basis

	// Get object extents in basis
	Vector tanPts[2], binPts[2];
	tanPts[0] = EntityList()->PhysGetCollision()->CollideGetExtent(pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), -tangent);
	tanPts[1] = EntityList()->PhysGetCollision()->CollideGetExtent(pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), tangent);
	binPts[0] = EntityList()->PhysGetCollision()->CollideGetExtent(pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), -binormal);
	binPts[1] = EntityList()->PhysGetCollision()->CollideGetExtent(pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), binormal);

	// now compute the centered bbox
	float mins[2], maxs[2], center[2], extents[2];
	mins[0] = DotProduct(tanPts[0], tangent);
	maxs[0] = DotProduct(tanPts[1], tangent);

	mins[1] = DotProduct(binPts[0], binormal);
	maxs[1] = DotProduct(binPts[1], binormal);

	center[0] = 0.5 * (mins[0] + maxs[0]);
	center[1] = 0.5 * (mins[1] + maxs[1]);

	extents[0] = maxs[0] - center[0];
	extents[1] = maxs[1] - center[1];

	Vector centerPoint = center[0] * tangent + center[1] * binormal + dist * normal;

	Vector axes[2];
	axes[0] = (maxs[0] - center[0]) * tangent;
	axes[1] = (maxs[1] - center[1]) * binormal;

	// visualize OBB hit
	/*
	Vector corner1 = centerPoint - axes[0] - axes[1];
	Vector corner2 = centerPoint + axes[0] - axes[1];
	Vector corner3 = centerPoint + axes[0] + axes[1];
	Vector corner4 = centerPoint - axes[0] + axes[1];
	NDebugOverlay::Line( corner1, corner2, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner2, corner3, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner3, corner4, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner4, corner1, 0, 0, 255, false, 10 );
	*/

	Vector	corner[4];

	corner[0] = centerPoint - axes[0] - axes[1];
	corner[1] = centerPoint + axes[0] - axes[1];
	corner[2] = centerPoint + axes[0] + axes[1];
	corner[3] = centerPoint - axes[0] + axes[1];

	CEffectData	data;

	if (pObject->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL)
	{
		/*
		data.m_vOrigin = centerPoint;
		data.m_vNormal = normal;
		VectorAngles( normal, data.m_vAngles );
		data.m_flScale = random->RandomFloat( 8, 10 );

		DispatchEffect( "watersplash", data );

		int		splashes = 4;
		Vector	point;

		for ( int i = 0; i < splashes; i++ )
		{
			point = RandomVector( -32.0f, 32.0f );
			point[2] = 0.0f;

			point += corner[i];

			data.m_vOrigin = point;
			data.m_vNormal = normal;
			VectorAngles( normal, data.m_vAngles );
			data.m_flScale = random->RandomFloat( 4, 6 );

			DispatchEffect( "watersplash", data );
		}
		*/

		//FIXME: This code will not work correctly given how the ragdoll/fluid collision is acting currently
		return;
	}

	Vector vel;
	pObject->GetVelocity(&vel, NULL);
	float rawSpeed = -DotProduct(normal, vel);

	// proportional to cross-sectional area times velocity squared (fluid pressure)
	float speed = rawSpeed * rawSpeed * extents[0] * extents[1] * (1.0f / 2500000.0f) * pObject->GetMass() * (0.01f);

	speed = clamp(speed, 0.f, 50.f);

	bool bRippleOnly = false;

	// allow the entity to perform a custom splash effect
	if (pEntity->PhysicsSplash(centerPoint, normal, rawSpeed, speed))
		return;

	//Deny really weak hits
	//FIXME: We still need to ripple the surface in this case
	if (speed <= 0.35f)
	{
		if (speed <= 0.1f)
			return;

		bRippleOnly = true;
	}

	float size = RemapVal(speed, 0.35, 50, 8, 18);

	//Find the surface area
	float	radius = extents[0] * extents[1];
	//int	splashes = clamp ( radius / 128.0f, 1, 2 );	//One splash for every three square feet of area

	//Msg( "Speed: %.2f, Size: %.2f\n, Radius: %.2f, Splashes: %d", speed, size, radius, splashes );

	Vector point;

	data.m_fFlags = 0;
	data.m_vOrigin = centerPoint;
	data.m_vNormal = normal;
	VectorAngles(normal, data.m_vAngles);
	data.m_flScale = size + random->RandomFloat(0, 2);
	if (pEntity->GetWaterType() & CONTENTS_SLIME)
	{
		data.m_fFlags |= FX_WATER_IN_SLIME;
	}

	if (bRippleOnly)
	{
		DispatchEffect("waterripple", data);
	}
	else
	{
		DispatchEffect("watersplash", data);
	}

	if (radius > 500.0f)
	{
		int splashes = random->RandomInt(1, 4);

		for (int i = 0; i < splashes; i++)
		{
			point = RandomVector(-4.0f, 4.0f);
			point[2] = 0.0f;

			point += corner[i];

			data.m_fFlags = 0;
			data.m_vOrigin = point;
			data.m_vNormal = normal;
			VectorAngles(normal, data.m_vAngles);
			data.m_flScale = size + random->RandomFloat(-3, 1);
			if (pEntity->GetWaterType() & CONTENTS_SLIME)
			{
				data.m_fFlags |= FX_WATER_IN_SLIME;
			}

			if (bRippleOnly)
			{
				DispatchEffect("waterripple", data);
			}
			else
			{
				DispatchEffect("watersplash", data);
			}
		}
	}

	/*
	for ( i = 0; i < splashes; i++ )
	{
		point = RandomVector( -8.0f, 8.0f );
		point[2] = 0.0f;

		point += centerPoint + axes[0] * random->RandomFloat( -1, 1 ) + axes[1] * random->RandomFloat( -1, 1 );

		data.m_vOrigin = point;
		data.m_vNormal = normal;
		VectorAngles( normal, data.m_vAngles );
		data.m_flScale = size + random->RandomFloat( -2, 4 );

		DispatchEffect( "watersplash", data );
	}
	*/
}

void CCollisionEvent::FluidStartTouch( IPhysicsObject *pObject, IPhysicsFluidController *pFluid ) 
{
	CallbackContext check(this);
	if ( ( pObject == NULL ) || ( pFluid == NULL ) )
		return;

	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	if ( !pEntity )
		return;

	pEntity->GetEngineObject()->AddEFlags( EFL_TOUCHING_FLUID );
	pEntity->OnEntityEvent( ENTITY_EVENT_WATER_TOUCH, (void*)(intp)pFluid->GetContents() );

	float timeSinceLastCollision = DeltaTimeSinceLastFluid( pEntity );
	if ( timeSinceLastCollision < 0.5f )
		return;

	// UNDONE: Use this for splash logic instead?
	// UNDONE: Use angular term too - push splashes in rotAxs cross normal direction?
	Vector normal;
	float dist;
	pFluid->GetSurfacePlane( &normal, &dist );
	Vector vel;
	AngularImpulse angVel;
	pObject->GetVelocity( &vel, &angVel );
	Vector unitVel = vel;
	VectorNormalize( unitVel );
	
	// normal points out of the surface, we want the direction that points in
	float dragScale = pFluid->GetDensity() * gEntList.PhysGetEnv()->GetSimulationTimestep();
	normal = -normal;
	float linearScale = 0.5f * DotProduct( unitVel, normal ) * pObject->CalculateLinearDrag( normal ) * dragScale;
	linearScale = clamp( linearScale, 0.0f, 1.0f );
	vel *= -linearScale;

	// UNDONE: Figure out how much of the surface area has crossed the water surface and scale angScale by that
	// For now assume 25%
	Vector rotAxis = angVel;
	VectorNormalize(rotAxis);
	float angScale = 0.25f * pObject->CalculateAngularDrag( angVel ) * dragScale;
	angScale = clamp( angScale, 0.0f, 1.0f );
	angVel *= -angScale;
	
	// compute the splash before we modify the velocity
	PhysicsSplash( pFluid, pObject, pEntity );

	// now damp out some motion toward the surface
	pObject->AddVelocity( &vel, &angVel );
}

void CCollisionEvent::FluidEndTouch( IPhysicsObject *pObject, IPhysicsFluidController *pFluid ) 
{
	CallbackContext check(this);
	if ( ( pObject == NULL ) || ( pFluid == NULL ) )
		return;

	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	if ( !pEntity )
		return;

	float timeSinceLastCollision = DeltaTimeSinceLastFluid( pEntity );
	if ( timeSinceLastCollision >= 0.5f )
	{
		PhysicsSplash( pFluid, pObject, pEntity );
	}

	pEntity->GetEngineObject()->RemoveEFlags( EFL_TOUCHING_FLUID );
	pEntity->OnEntityEvent( ENTITY_EVENT_WATER_UNTOUCH, (void*)(intp)pFluid->GetContents() );
}

//-----------------------------------------------------------------------------
// CollisionEvent system 
//-----------------------------------------------------------------------------
// NOTE: PreCollision/PostCollision ALWAYS come in matched pairs!!!
void CCollisionEvent::PreCollision( vcollisionevent_t *pEvent )
{
	CallbackContext check(this);
	m_gameEvent.Init( pEvent );

	// gather the pre-collision data that the game needs to track
	for ( int i = 0; i < 2; i++ )
	{
		IPhysicsObject *pObject = pEvent->pObjects[i];
		if ( pObject )
		{
			if ( pObject->GetGameFlags() & FVPHYSICS_PLAYER_HELD )
			{
				CBaseEntity *pOtherEntity = reinterpret_cast<CBaseEntity *>(pEvent->pObjects[!i]->GetGameData());
				if ( pOtherEntity && !pOtherEntity->IsPlayer() )
				{
					Vector velocity;
					AngularImpulse angVel;
					// HACKHACK: If we totally clear this out, then Havok will think the objects
					// are penetrating and generate forces to separate them
					// so make it fairly small and have a tiny collision instead.
					pObject->GetVelocity( &velocity, &angVel );
					float len = VectorNormalize(velocity);
					len = MAX( len, 10 );
					velocity *= len;
					len = VectorNormalize(angVel);
					len = MAX( len, 1 );
					angVel *= len;
					pObject->SetVelocity( &velocity, &angVel );
				}
			}
			pObject->GetVelocity( &m_gameEvent.preVelocity[i], &m_gameEvent.preAngularVelocity[i] );
		}
	}
}

void CCollisionEvent::PostCollision( vcollisionevent_t *pEvent )
{
	CallbackContext check(this);
	bool isShadow[2] = {false,false};
	int i;

	for ( i = 0; i < 2; i++ )
	{
		IPhysicsObject *pObject = pEvent->pObjects[i];
		if ( pObject )
		{
			CBaseEntity *pEntity = reinterpret_cast<CBaseEntity *>(pObject->GetGameData());
			if ( !pEntity )
				return;

			// UNDONE: This is here to trap crashes due to NULLing out the game data on delete
			m_gameEvent.pEntities[i] = pEntity;
			unsigned int flags = pObject->GetCallbackFlags();
			pObject->GetVelocity( &m_gameEvent.postVelocity[i], NULL );
			if ( flags & CALLBACK_SHADOW_COLLISION )
			{
				isShadow[i] = true;
			}

			// Shouldn't get impacts with triggers
			Assert( !pObject->IsTrigger() );
		}
	}

	// copy off the post-collision variable data
	m_gameEvent.collisionSpeed = pEvent->collisionSpeed;
	m_gameEvent.pInternalData = pEvent->pInternalData;

	// special case for hitting self, only make one non-shadow call
	if ( m_gameEvent.pEntities[0] == m_gameEvent.pEntities[1] )
	{
		if ( pEvent->isCollision && m_gameEvent.pEntities[0] )
		{
			m_gameEvent.pEntities[0]->VPhysicsCollision( 0, &m_gameEvent );
		}
		return;
	}

	if ( isShadow[0] && isShadow[1] )
	{
		pEvent->isCollision = false;
	}

	for ( i = 0; i < 2; i++ )
	{
		if ( pEvent->isCollision )
		{
			m_gameEvent.pEntities[i]->VPhysicsCollision( i, &m_gameEvent );
		}
		if ( pEvent->isShadowCollision && isShadow[i] )
		{
			m_gameEvent.pEntities[i]->VPhysicsShadowCollision( i, &m_gameEvent );
		}
	}
}



void CCollisionEvent::Friction( IPhysicsObject *pObject, float energy, int surfaceProps, int surfacePropsHit, IPhysicsCollisionData *pData )
{
	CallbackContext check(this);
	//Get our friction information
	Vector vecPos, vecVel;
	pData->GetContactPoint( vecPos );
	pObject->GetVelocityAtPoint( vecPos, &vecVel );

	CBaseEntity *pEntity = reinterpret_cast<CBaseEntity *>(pObject->GetGameData());
		
	if ( pEntity  )
	{
		friction_t *pFriction = FindFriction( pEntity );

		if ( pFriction && pFriction->pObject) 
		{
			// in MP mode play sound and effects once every 500 msecs,
			// no ongoing updates, takes too much bandwidth
			if ( (pFriction->flLastEffectTime + 0.5f) > gpGlobals->curtime)
			{
				pFriction->flLastUpdateTime = gpGlobals->curtime;
				return; 			
			}
		}

		pEntity->VPhysicsFriction( pObject, energy, surfaceProps, surfacePropsHit );
	}

	PhysFrictionEffect( vecPos, vecVel, energy, surfaceProps, surfacePropsHit );
}


friction_t *CCollisionEvent::FindFriction( CBaseEntity *pObject )
{
	friction_t *pFree = NULL;

	for ( int i = 0; i < ARRAYSIZE(m_current); i++ )
	{
		if ( !m_current[i].pObject && !pFree )
			pFree = &m_current[i];

		if ( m_current[i].pObject == pObject )
			return &m_current[i];
	}

	return pFree;
}

void CCollisionEvent::ShutdownFriction( friction_t &friction )
{
//	Msg( "Scrape Stop %s \n", STRING(friction.pObject->m_iClassname) );
	CSoundEnvelopeController::GetController().SoundDestroy( friction.patch );
	friction.patch = NULL;
	friction.pObject = NULL;
}

void CCollisionEvent::UpdateRemoveObjects()
{
	Assert(!gEntList.PhysIsInCallback());
	for ( int i = 0 ; i < m_removeObjects.Count(); i++ )
	{
		gEntList.DestroyEntity(m_removeObjects[i]);
	}
	m_removeObjects.RemoveAll();
}

void CCollisionEvent::PostSimulationFrame()
{
	UpdateDamageEvents();
	gEntList.PhysGetPostSimulationQueue().CallQueued();
	UpdateRemoveObjects();
}

void CCollisionEvent::FlushQueuedOperations()
{
	int loopCount = 0;
	while ( loopCount < 20 )
	{
		int count = m_triggerEvents.Count() + m_touchEvents.Count() + m_damageEvents.Count() + m_removeObjects.Count() + gEntList.PhysGetPostSimulationQueue().Count();
		if ( !count )
			break;
		// testing, if this assert fires it proves we've fixed the crash
		// after that the assert + warning can safely be removed
		Assert(0);
		Warning("Physics queue not empty, error!\n");
		loopCount++;
		UpdateTouchEvents();
		UpdateDamageEvents();
		gEntList.PhysGetPostSimulationQueue().CallQueued();
		UpdateRemoveObjects();
	}
}

void CCollisionEvent::FrameUpdate( void )
{
	UpdateFrictionSounds();
	UpdateTouchEvents();
	UpdatePenetrateEvents();
	UpdateFluidEvents();
	UpdateDamageEvents(); // if there was no PSI in physics, we'll still need to do some of these because collisions are solved in between PSIs
	gEntList.PhysGetPostSimulationQueue().CallQueued();
	UpdateRemoveObjects();

	// There are some queued operations that must complete each frame, iterate until these are done
	FlushQueuedOperations();
}



void CCollisionEvent::UpdateFluidEvents( void )
{
	for ( int i = m_fluidEvents.Count()-1; i >= 0; --i )
	{
		if ( (gpGlobals->curtime - m_fluidEvents[i].impactTime) > FLUID_TIME_MAX )
		{
			m_fluidEvents.FastRemove(i);
		}
	}
}


float CCollisionEvent::DeltaTimeSinceLastFluid( CBaseEntity *pEntity )
{
	for ( int i = m_fluidEvents.Count()-1; i >= 0; --i )
	{
		if (gEntList.GetBaseEntity(m_fluidEvents[i].hEntity) == pEntity )
		{
			return gpGlobals->curtime - m_fluidEvents[i].impactTime;
		}
	}

	int index = m_fluidEvents.AddToTail();
	m_fluidEvents[index].hEntity = pEntity;
	m_fluidEvents[index].impactTime = gpGlobals->curtime;
	return FLUID_TIME_MAX;
}

void CCollisionEvent::UpdateFrictionSounds( void )
{
	for ( int i = 0; i < ARRAYSIZE(m_current); i++ )
	{
		if ( m_current[i].patch )
		{
			if ( m_current[i].flLastUpdateTime < (gpGlobals->curtime-0.1f) )
			{
				// friction wasn't updated the last 100msec, assume fiction finished
				ShutdownFriction( m_current[i] );
			}
		}
	}
}


void CCollisionEvent::DispatchStartTouch( CBaseEntity *pEntity0, CBaseEntity *pEntity1, const Vector &point, const Vector &normal )
{
	trace_t trace;
	memset( &trace, 0, sizeof(trace) );
	trace.endpos = point;
	trace.plane.dist = DotProduct( point, normal );
	trace.plane.normal = normal;

	// NOTE: This sets up the touch list for both entities, no call to pEntity1 is needed
	pEntity0->GetEngineObject()->PhysicsMarkEntitiesAsTouchingEventDriven( pEntity1->GetEngineObject(), trace );
}

void CCollisionEvent::DispatchEndTouch( CBaseEntity *pEntity0, CBaseEntity *pEntity1 )
{
	// frees the event-driven touchlinks
	pEntity1->GetEngineObject()->PhysicsNotifyOtherOfUntouch( pEntity0->GetEngineObject());
	pEntity0->GetEngineObject()->PhysicsNotifyOtherOfUntouch( pEntity1->GetEngineObject());
}

void CCollisionEvent::UpdateTouchEvents( void )
{
	int i;
	// Turn on buffering in case new touch events occur during processing
	bool bOldTouchEvents = m_bBufferTouchEvents;
	m_bBufferTouchEvents = true;
	for ( i = 0; i < m_touchEvents.Count(); i++ )
	{
		const touchevent_t &event = m_touchEvents[i];
		if ( event.touchType == TOUCH_START )
		{
			DispatchStartTouch( (CBaseEntity*)event.pEntity0, (CBaseEntity*)event.pEntity1, event.endPoint, event.normal );
		}
		else
		{
			// TOUCH_END
			DispatchEndTouch((CBaseEntity*)event.pEntity0, (CBaseEntity*)event.pEntity1 );
		}
	}
	m_touchEvents.RemoveAll();

	for ( i = 0; i < m_triggerEvents.Count(); i++ )
	{
		m_currentTriggerEvent = m_triggerEvents[i];
		if ( m_currentTriggerEvent.bStart )
		{
			m_currentTriggerEvent.pTriggerEntity->StartTouch( m_currentTriggerEvent.pEntity );
		}
		else
		{
			m_currentTriggerEvent.pTriggerEntity->EndTouch( m_currentTriggerEvent.pEntity );
		}
	}
	m_triggerEvents.RemoveAll();
	m_currentTriggerEvent.Clear();
	m_bBufferTouchEvents = bOldTouchEvents;
}

void CCollisionEvent::UpdateDamageEvents( void )
{
	for ( int i = 0; i < m_damageEvents.Count(); i++ )
	{
		damageevent_t &event = m_damageEvents[i];

		// Track changes in the entity's life state
		int iEntBits = event.pEntity->IsAlive() ? 0x0001 : 0;
		iEntBits |= event.pEntity->GetEngineObject()->IsMarkedForDeletion() ? 0x0002 : 0;
		iEntBits |= (event.pEntity->GetEngineObject()->GetSolidFlags() & FSOLID_NOT_SOLID) ? 0x0004 : 0;
#if 0
		// Go ahead and compute the current static stress when hit by a large object (with a force high enough to do damage).  
		// That way you die from the impact rather than the stress of the object resting on you whenever possible. 
		// This makes the damage effects cleaner.
		if ( event.pInflictorPhysics && event.pInflictorPhysics->GetMass() > VPHYSICS_LARGE_OBJECT_MASS )
		{
			CBaseCombatCharacter *pCombat = event.pEntity->MyCombatCharacterPointer();
			if ( pCombat )
			{
				vphysics_objectstress_t stressOut;
				event.info.AddDamage( pCombat->CalculatePhysicsStressDamage( &stressOut, pCombat->GetEngineObject()->VPhysicsGetObject() ) );
			}
		}
#endif

		event.pEntity->TakeDamage( event.info );
		int iEntBits2 = event.pEntity->IsAlive() ? 0x0001 : 0;
		iEntBits2 |= event.pEntity->GetEngineObject()->IsMarkedForDeletion() ? 0x0002 : 0;
		iEntBits2 |= (event.pEntity->GetEngineObject()->GetSolidFlags() & FSOLID_NOT_SOLID) ? 0x0004 : 0;

		if ( event.bRestoreVelocity && iEntBits != iEntBits2 )
		{
			// UNDONE: Use ratio of masses to blend in a little of the collision response?
			// UNDONE: Damage for future events is already computed - it would be nice to
			//			go back and recompute it now that the values have
			//			been adjusted
			RestoreDamageInflictorState( event.pInflictorPhysics );
		}
	}
	m_damageEvents.RemoveAll();
	m_damageInflictors.RemoveAll();
}

void CCollisionEvent::RestoreDamageInflictorState( int inflictorStateIndex, float velocityBlend )
{
	inflictorstate_t &state = m_damageInflictors[inflictorStateIndex];
	if ( state.restored )
		return;

	// so we only restore this guy once
	state.restored = true;

	if ( velocityBlend > 0 )
	{
		Vector velocity;
		AngularImpulse angVel;
		state.pInflictorPhysics->GetVelocity( &velocity, &angVel );
		state.savedVelocity = state.savedVelocity*velocityBlend + velocity*(1-velocityBlend);
		state.savedAngularVelocity = state.savedAngularVelocity*velocityBlend + angVel*(1-velocityBlend);
		state.pInflictorPhysics->SetVelocity( &state.savedVelocity, &state.savedAngularVelocity );
	}

	if ( state.nextIndex >= 0 )
	{
		RestoreDamageInflictorState( state.nextIndex, velocityBlend );
	}
}

void CCollisionEvent::RestoreDamageInflictorState( IPhysicsObject *pInflictor )
{
	if ( !pInflictor )
		return;

	int index = FindDamageInflictor( pInflictor );
	if ( index >= 0 )
	{
		inflictorstate_t &state = m_damageInflictors[index];
		if ( !state.restored )
		{
			float velocityBlend = 1.0;
			float inflictorMass = state.pInflictorPhysics->GetMass();
			if ( inflictorMass < VPHYSICS_LARGE_OBJECT_MASS && !(state.pInflictorPhysics->GetGameFlags() & FVPHYSICS_DMG_SLICE) )
			{
				float otherMass = state.otherMassMax > 0 ? state.otherMassMax : 1;
				float massRatio = inflictorMass / otherMass;
				massRatio = clamp( massRatio, 0.1f, 10.0f );
				if ( massRatio < 1 )
				{
					velocityBlend = RemapVal( massRatio, 0.1, 1, 0, 0.5 );
				}
				else
				{
					velocityBlend = RemapVal( massRatio, 1.0, 10, 0.5, 1 );
				}
			}
			RestoreDamageInflictorState( index, velocityBlend );
		}
	}
}

bool CCollisionEvent::GetInflictorVelocity( IPhysicsObject *pInflictor, Vector &velocity, AngularImpulse &angVelocity )
{
	int index = FindDamageInflictor( pInflictor );
	if ( index >= 0 )
	{
		inflictorstate_t &state = m_damageInflictors[index];
		velocity = state.savedVelocity;
		angVelocity = state.savedAngularVelocity;
		return true;
	}

	return false;
}



void CCollisionEvent::AddTouchEvent( CBaseEntity *pEntity0, CBaseEntity *pEntity1, int touchType, const Vector &point, const Vector &normal )
{
	if ( !pEntity0 || !pEntity1 )
		return;

	int index = m_touchEvents.AddToTail();
	touchevent_t &event = m_touchEvents[index];
	event.pEntity0 = pEntity0;
	event.pEntity1 = pEntity1;
	event.touchType = touchType;
	event.endPoint = point;
	event.normal = normal;
}

void CCollisionEvent::AddDamageEvent( CBaseEntity *pEntity, const CTakeDamageInfo &info, IPhysicsObject *pInflictorPhysics, bool bRestoreVelocity, const Vector &savedVel, const AngularImpulse &savedAngVel )
{
	if ( pEntity->GetEngineObject()->IsMarkedForDeletion() )
		return;

	int iTimeBasedDamage = g_pGameRules->Damage_GetTimeBased();
	if ( !( info.GetDamageType() & (DMG_BURN | DMG_DROWN | iTimeBasedDamage | DMG_PREVENT_PHYSICS_FORCE) ) )
	{
		Assert( info.GetDamageForce() != vec3_origin && info.GetDamagePosition() != vec3_origin );
	}

	int index = m_damageEvents.AddToTail();
	damageevent_t &event = m_damageEvents[index];
	event.pEntity = pEntity;
	event.info = info;
	event.pInflictorPhysics = pInflictorPhysics;
	event.bRestoreVelocity = bRestoreVelocity;
	if ( !pInflictorPhysics || !pInflictorPhysics->IsMoveable() )
	{
		event.bRestoreVelocity = false;
	}

	if ( event.bRestoreVelocity )
	{
		float otherMass = pEntity->GetEngineObject()->VPhysicsGetObject()->GetMass();
		int inflictorIndex = FindDamageInflictor(pInflictorPhysics);
		if ( inflictorIndex >= 0 )
		{
			// if this is a bigger mass, save that info
			inflictorstate_t &state = m_damageInflictors[inflictorIndex];
			if ( otherMass > state.otherMassMax )
			{
				state.otherMassMax = otherMass;
			}

		}
		else
		{
			AddDamageInflictor( pInflictorPhysics, otherMass, savedVel, savedAngVel, true );
		}
	}

}

//-----------------------------------------------------------------------------
// Impulse events
//-----------------------------------------------------------------------------
void PostSimulation_ImpulseEvent( IPhysicsObject *pObject, const Vector &centerForce, const AngularImpulse &centerTorque )
{
	pObject->ApplyForceCenter( centerForce );
	pObject->ApplyTorqueCenter( centerTorque );
}

void PostSimulation_SetVelocityEvent( IPhysicsObject *pPhysicsObject, const Vector &vecVelocity )
{
	pPhysicsObject->SetVelocity( &vecVelocity, NULL );
}

void CCollisionEvent::AddRemoveObject(CBaseEntity *pRemove)
{
	if ( pRemove && m_removeObjects.Find(pRemove) == -1 )
	{
		m_removeObjects.AddToTail(pRemove);
	}
}
int CCollisionEvent::FindDamageInflictor( IPhysicsObject *pInflictorPhysics )
{
	// UNDONE: Linear search?  Probably ok with a low count here
	for ( int i = m_damageInflictors.Count()-1; i >= 0; --i )
	{
		const inflictorstate_t &state = m_damageInflictors[i];
		if ( state.pInflictorPhysics == pInflictorPhysics )
			return i;
	}

	return -1;
}


int CCollisionEvent::AddDamageInflictor( IPhysicsObject *pInflictorPhysics, float otherMass, const Vector &savedVel, const AngularImpulse &savedAngVel, bool addList )
{
	// NOTE: Save off the state of the object before collision
	// restore if the impact is a kill
	// UNDONE: Should we absorb some energy here?
	// NOTE: we can't save a delta because there could be subsequent post-fatal collisions

	int addIndex = m_damageInflictors.AddToTail();
	{
		inflictorstate_t &state = m_damageInflictors[addIndex];
		state.pInflictorPhysics = pInflictorPhysics;
		state.savedVelocity = savedVel;
		state.savedAngularVelocity = savedAngVel;
		state.otherMassMax = otherMass;
		state.restored = false;
		state.nextIndex = -1;
	}

	if ( addList )
	{
		CBaseEntity *pEntity = static_cast<CBaseEntity *>(pInflictorPhysics->GetGameData());
		if ( pEntity )
		{
			IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int physCount = pEntity->GetEngineObject()->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
			if ( physCount > 1 )
			{
				int currentIndex = addIndex;
				for ( int i = 0; i < physCount; i++ )
				{
					if ( pList[i] != pInflictorPhysics )
					{
						Vector vel;
						AngularImpulse angVel;
						pList[i]->GetVelocity( &vel, &angVel );
						int next = AddDamageInflictor( pList[i], otherMass, vel, angVel, false );
						m_damageInflictors[currentIndex].nextIndex = next;
						currentIndex = next;
					}
				}
			}
		}
	}
	return addIndex;
}


void CCollisionEvent::LevelShutdown( void )
{
	for ( int i = 0; i < ARRAYSIZE(m_current); i++ )
	{
		if ( m_current[i].patch )
		{
			ShutdownFriction( m_current[i] );
		}
	}
}


void CCollisionEvent::StartTouch( IPhysicsObject *pObject1, IPhysicsObject *pObject2, IPhysicsCollisionData *pTouchData )
{
	CallbackContext check(this);
	CBaseEntity *pEntity1 = static_cast<CBaseEntity *>(pObject1->GetGameData());
	CBaseEntity *pEntity2 = static_cast<CBaseEntity *>(pObject2->GetGameData());

	if ( !pEntity1 || !pEntity2 )
		return;

	Vector endPoint, normal;
	pTouchData->GetContactPoint( endPoint );
	pTouchData->GetSurfaceNormal( normal );
	if ( !m_bBufferTouchEvents )
	{
		DispatchStartTouch( pEntity1, pEntity2, endPoint, normal );
	}
	else
	{
		AddTouchEvent( pEntity1, pEntity2, TOUCH_START, endPoint, normal );
	}
}

static int CountPhysicsObjectEntityContacts( IPhysicsObject *pObject, CBaseEntity *pEntity )
{
	IPhysicsFrictionSnapshot *pSnapshot = pObject->CreateFrictionSnapshot();
	int count = 0;
	while ( pSnapshot->IsValid() )
	{
		IPhysicsObject *pOther = pSnapshot->GetObject(1);
		CBaseEntity *pOtherEntity = static_cast<CBaseEntity *>(pOther->GetGameData());
		if ( pOtherEntity == pEntity )
			count++;
		pSnapshot->NextFrictionData();
	}
	pObject->DestroyFrictionSnapshot( pSnapshot );
	return count;
}

void CCollisionEvent::EndTouch( IPhysicsObject *pObject1, IPhysicsObject *pObject2, IPhysicsCollisionData *pTouchData )
{
	CallbackContext check(this);
	CBaseEntity *pEntity1 = static_cast<CBaseEntity *>(pObject1->GetGameData());
	CBaseEntity *pEntity2 = static_cast<CBaseEntity *>(pObject2->GetGameData());

	if ( !pEntity1 || !pEntity2 )
		return;

	// contact point deleted, but entities are still touching?
	IPhysicsObject *list[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEntity1->GetEngineObject()->VPhysicsGetObjectList( list, ARRAYSIZE(list) );

	int contactCount = 0;
	for ( int i = 0; i < count; i++ )
	{
		contactCount += CountPhysicsObjectEntityContacts( list[i], pEntity2 );
		
		// still touching
		if ( contactCount > 1 )
			return;
	}

	// should have exactly one contact point (the one getting deleted here)
	//Assert( contactCount == 1 );

	Vector endPoint, normal;
	pTouchData->GetContactPoint( endPoint );
	pTouchData->GetSurfaceNormal( normal );

	if ( !m_bBufferTouchEvents )
	{
		DispatchEndTouch( pEntity1, pEntity2 );
	}
	else
	{
		AddTouchEvent( pEntity1, pEntity2, TOUCH_END, vec3_origin, vec3_origin );
	}
}

// UNDONE: This is functional, but minimally.
void CCollisionEvent::ObjectEnterTrigger( IPhysicsObject *pTrigger, IPhysicsObject *pObject )
{
	CBaseEntity *pTriggerEntity = static_cast<CBaseEntity *>(pTrigger->GetGameData());
	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	if ( pTriggerEntity && pEntity )
	{
		// UNDONE: Don't buffer these until we can solve generating touches at object creation time
		if ( 0 && m_bBufferTouchEvents )
		{
			int index = m_triggerEvents.AddToTail();
			m_triggerEvents[index].Init( pTriggerEntity, pTrigger, pEntity, pObject, true );
		}
		else
		{
			CallbackContext check(this);
			m_currentTriggerEvent.Init( pTriggerEntity, pTrigger, pEntity, pObject, true ); 
			pTriggerEntity->StartTouch( pEntity );
			m_currentTriggerEvent.Clear();
		}
	}
}

void CCollisionEvent::ObjectLeaveTrigger( IPhysicsObject *pTrigger, IPhysicsObject *pObject )
{
	CBaseEntity *pTriggerEntity = static_cast<CBaseEntity *>(pTrigger->GetGameData());
	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	if ( pTriggerEntity && pEntity )
	{
		// UNDONE: Don't buffer these until we can solve generating touches at object creation time
		if ( 0 && m_bBufferTouchEvents )
		{
			int index = m_triggerEvents.AddToTail();
			m_triggerEvents[index].Init( pTriggerEntity, pTrigger, pEntity, pObject, false );
		}
		else
		{
			CallbackContext check(this);
			m_currentTriggerEvent.Init( pTriggerEntity, pTrigger, pEntity, pObject, false ); 
			pTriggerEntity->EndTouch( pEntity );
			m_currentTriggerEvent.Clear();
		}
	}
}

bool CCollisionEvent::GetTriggerEvent( triggerevent_t *pEvent, CBaseEntity *pTriggerEntity )
{
	if ( pEvent && pTriggerEntity == m_currentTriggerEvent.pTriggerEntity )
	{
		*pEvent = m_currentTriggerEvent;
		return true;
	}

	return false;
}



