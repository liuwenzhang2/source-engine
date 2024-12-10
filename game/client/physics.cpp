//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "vcollide_parse.h"
#include "filesystem.h"
#include "engine/IStaticPropMgr.h"
#include "engine/IEngineSound.h"
#include "vphysics_sound.h"
#include "movevars_shared.h"
#include "engine/ivmodelinfo.h"
#include "fx.h"
#include "tier0/vprof.h"
#include "c_world.h"
#include "vphysics/object_hash.h"
#include "vphysics/collision_set.h"
#include "soundenvelope.h"
#include "fx_water.h"
#include "positionwatcher.h"
#include "vphysics/constraints.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// file system interface
extern IFileSystem *filesystem;

ConVar	cl_phys_timescale( "cl_phys_timescale", "1.0", FCVAR_CHEAT, "Sets the scale of time for client-side physics (ragdolls)" );


//FIXME: Replicated from server end, consolidate?


extern IVEngineClient *engine;

ConVar cl_ragdoll_collide( "cl_ragdoll_collide", "0" );

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
	CallbackContext callback(this);

	C_BaseEntity *pEntity0 = static_cast<C_BaseEntity *>(pGameData0);
	C_BaseEntity *pEntity1 = static_cast<C_BaseEntity *>(pGameData1);

	if ( !pEntity0 || !pEntity1 )
		return 1;

	unsigned short gameFlags0 = pObj0->GetGameFlags();
	unsigned short gameFlags1 = pObj1->GetGameFlags();

	if ( pEntity0 == pEntity1 )
	{
		// allow all-or-nothing per-entity disable
		if ( (gameFlags0 | gameFlags1) & FVPHYSICS_NO_SELF_COLLISIONS )
			return 0;

		IPhysicsCollisionSet *pSet = physics->FindCollisionSet( pEntity0->GetEngineObject()->GetModelIndex() );
		if ( pSet )
			return pSet->ShouldCollide( pObj0->GetGameIndex(), pObj1->GetGameIndex() );

		return 1;
	}
	// Obey collision group rules
	Assert(GameRules());
	if ( GameRules() )
	{
		if (!GameRules()->ShouldCollide( pEntity0->GetEngineObject()->GetCollisionGroup(), pEntity1->GetEngineObject()->GetCollisionGroup() ))
			return 0;
	}

	if ( (pObj0->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL) && (pObj1->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL) )
	{
		if ( !cl_ragdoll_collide.GetBool() )
			return 0;
	}

	// check contents
	if ( !(pObj0->GetContents() & pEntity1->PhysicsSolidMaskForEntity()) || !(pObj1->GetContents() & pEntity0->PhysicsSolidMaskForEntity()) )
		return 0;

	if ( g_EntityCollisionHash->IsObjectPairInHash( pGameData0, pGameData1 ) )
		return 0;

	if ( g_EntityCollisionHash->IsObjectPairInHash( pObj0, pObj1 ) )
		return 0;

#if 0
	int solid0 = pEntity0->GetEngineObject()->GetSolid();
	int solid1 = pEntity1->GetEngineObject()->GetSolid();
	int nSolidFlags0 = pEntity0->GetEngineObject()->GetSolidFlags();
	int nSolidFlags1 = pEntity1->GetEngineObject()->GetSolidFlags();
#endif

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
	return 1;
}

int CCollisionEvent::ShouldSolvePenetration( IPhysicsObject *pObj0, IPhysicsObject *pObj1, void *pGameData0, void *pGameData1, float dt )
{
	CallbackContext callback(this);
	// solve it yourself here and return 0, or have the default implementation do it
	if ( pGameData0 == pGameData1 )
	{
		if ( pObj0->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL )
		{
			// this is a ragdoll, self penetrating
			C_BaseEntity *pEnt = reinterpret_cast<C_BaseEntity *>(pGameData0);
			C_BaseAnimating *pAnim = pEnt->GetBaseAnimating();

			if ( pAnim && pAnim->GetEngineObject()->RagdollBoneCount() )
			{
				IPhysicsConstraintGroup *pGroup = pAnim->GetEngineObject()->GetConstraintGroup();
				if ( pGroup )
				{
					pGroup->SolvePenetration( pObj0, pObj1 );
					return false;
				}
			}
		}
	}

	return true;
}

void CCollisionEvent::ObjectSound( int index, vcollisionevent_t *pEvent )
{
	IPhysicsObject *pObject = pEvent->pObjects[index];
	if ( !pObject || pObject->IsStatic() )
		return;

	float speed = pEvent->collisionSpeed * pEvent->collisionSpeed;
	int surfaceProps = pEvent->surfaceProps[index];

	void *pGameData = pObject->GetGameData();
		
	if ( pGameData )
	{
		float volume = speed * (1.0f/(320.0f*320.0f));	// max volume at 320 in/s
		
		if ( volume > 1.0f )
			volume = 1.0f;

		if ( surfaceProps >= 0 )
		{
			ClientEntityList().AddImpactSound(pGameData, pObject, surfaceProps, pEvent->surfaceProps[!index], volume, speed);
		}
	}
}

void CCollisionEvent::PostCollision( vcollisionevent_t *pEvent )
{
	CallbackContext callback(this);
	if ( pEvent->deltaCollisionTime > 0.1f && pEvent->collisionSpeed > 70 )
	{
		ObjectSound( 0, pEvent );
		ObjectSound( 1, pEvent );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollisionEvent::FrameUpdate( void )
{
	UpdateFrictionSounds();
	UpdateTouchEvents();
	UpdateFluidEvents();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CCollisionEvent::UpdateTouchEvents( void )
{
	// Turn on buffering in case new touch events occur during processing
	bool bOldTouchEvents = m_bBufferTouchEvents;
	m_bBufferTouchEvents = true;
	for ( int i = 0; i < m_touchEvents.Count(); i++ )
	{
		const touchevent_t &event = m_touchEvents[i];
		if ( event.touchType == TOUCH_START )
		{
			DispatchStartTouch((C_BaseEntity*)event.pEntity0, (C_BaseEntity*)event.pEntity1, event.endPoint, event.normal );
		}
		else
		{
			// TOUCH_END
			DispatchEndTouch((C_BaseEntity*)event.pEntity0, (C_BaseEntity*)event.pEntity1 );
		}
	}

	m_touchEvents.RemoveAll();
	m_bBufferTouchEvents = bOldTouchEvents;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity0 - 
//			*pEntity1 - 
//			touchType - 
//-----------------------------------------------------------------------------
void CCollisionEvent::AddTouchEvent( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1, int touchType, const Vector &point, const Vector &normal )
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

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject1 - 
//			*pObject2 - 
//			*pTouchData - 
//-----------------------------------------------------------------------------
void CCollisionEvent::StartTouch( IPhysicsObject *pObject1, IPhysicsObject *pObject2, IPhysicsCollisionData *pTouchData )
{
	CallbackContext callback(this);
	C_BaseEntity *pEntity1 = static_cast<C_BaseEntity *>(pObject1->GetGameData());
	C_BaseEntity *pEntity2 = static_cast<C_BaseEntity *>(pObject2->GetGameData());

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

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity0 - 
//			*pEntity1 - 
//-----------------------------------------------------------------------------
void CCollisionEvent::DispatchStartTouch( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1, const Vector &point, const Vector &normal )
{
	trace_t trace;
	memset( &trace, 0, sizeof(trace) );
	trace.endpos = point;
	trace.plane.dist = DotProduct( point, normal );
	trace.plane.normal = normal;

	// NOTE: This sets up the touch list for both entities, no call to pEntity1 is needed
	pEntity0->GetEngineObject()->PhysicsMarkEntitiesAsTouchingEventDriven( pEntity1->GetEngineObject(), trace );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject1 - 
//			*pObject2 - 
//			*pTouchData - 
//-----------------------------------------------------------------------------
void CCollisionEvent::EndTouch( IPhysicsObject *pObject1, IPhysicsObject *pObject2, IPhysicsCollisionData *pTouchData )
{
	CallbackContext callback(this);
	C_BaseEntity *pEntity1 = static_cast<C_BaseEntity *>(pObject1->GetGameData());
	C_BaseEntity *pEntity2 = static_cast<C_BaseEntity *>(pObject2->GetGameData());

	if ( !pEntity1 || !pEntity2 )
		return;

	if ( !m_bBufferTouchEvents )
	{
		DispatchEndTouch( pEntity1, pEntity2 );
	}
	else
	{
		AddTouchEvent( pEntity1, pEntity2, TOUCH_END, vec3_origin, vec3_origin );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity0 - 
//			*pEntity1 - 
//-----------------------------------------------------------------------------
void CCollisionEvent::DispatchEndTouch( C_BaseEntity *pEntity0, C_BaseEntity *pEntity1 )
{
	// frees the event-driven touchlinks
	pEntity1->GetEngineObject()->PhysicsNotifyOtherOfUntouch( pEntity0->GetEngineObject());
	pEntity0->GetEngineObject()->PhysicsNotifyOtherOfUntouch( pEntity1->GetEngineObject());
}

void CCollisionEvent::Friction( IPhysicsObject *pObject, float energy, int surfaceProps, int surfacePropsHit, IPhysicsCollisionData *pData )
{
	CallbackContext callback(this);
	if ( energy < 0.05f || surfaceProps < 0 )
		return;

	//Get our friction information
	Vector vecPos, vecVel;
	pData->GetContactPoint( vecPos );
	pObject->GetVelocityAtPoint( vecPos, &vecVel );

	CBaseEntity *pEntity = reinterpret_cast<CBaseEntity *>(pObject->GetGameData());
		
	if ( pEntity  )
	{
		friction_t *pFriction = FindFriction( pEntity );

		if ( (gpGlobals->maxClients > 1) && pFriction && pFriction->pObject) 
		{
			// in MP mode play sound and effects once every 500 msecs,
			// no ongoing updates, takes too much bandwidth
			if ( (pFriction->flLastEffectTime + 0.5f) > gpGlobals->curtime)
			{
				pFriction->flLastUpdateTime = gpGlobals->curtime;
				return; 			
			}
		}

		PhysFrictionSound( pEntity, pObject, energy, surfaceProps, surfacePropsHit );
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

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &matrix - 
//			&normal - 
// Output : static int
//-----------------------------------------------------------------------------
static int BestAxisMatchingNormal(const matrix3x4_t &matrix, const Vector &normal )
{
	float bestDot = -1;
	int best = 0;
	for ( int i = 0; i < 3; i++ )
	{
		Vector tmp;
		MatrixGetColumn( matrix, i, tmp );
		float dot = fabs(DotProduct( tmp, normal ));
		if ( dot > bestDot )
		{
			bestDot = dot;
			best = i;
		}
	}

	return best;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pFluid - 
//			*pObject - 
//			*pEntity - 
//-----------------------------------------------------------------------------
void PhysicsSplash( IPhysicsFluidController *pFluid, IPhysicsObject *pObject, CBaseEntity *pEntity )
{
	//FIXME: For now just allow ragdolls for E3 - jdw
	if ( ( pObject->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL ) == false )
		return;

	Vector velocity;
	pObject->GetVelocity( &velocity, NULL );
	
	float impactSpeed = velocity.Length();

	if ( impactSpeed < 25.0f )
		return;

	Vector normal;
	float dist;
	pFluid->GetSurfacePlane( &normal, &dist );

	const matrix3x4_t &matrix = pEntity->GetEngineObject()->EntityToWorldTransform();
	
	// Find the local axis that best matches the water surface normal
	int bestAxis = BestAxisMatchingNormal( matrix, normal );

	Vector tangent, binormal;
	MatrixGetColumn( matrix, (bestAxis+1)%3, tangent );
	binormal = CrossProduct( normal, tangent );
	VectorNormalize( binormal );
	tangent = CrossProduct( binormal, normal );
	VectorNormalize( tangent );

	// Now we have a basis tangent to the surface that matches the object's local orientation as well as possible
	// compute an OBB using this basis
	
	// Get object extents in basis
	Vector tanPts[2], binPts[2];
	tanPts[0] = physcollision->CollideGetExtent( pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), -tangent );
	tanPts[1] = physcollision->CollideGetExtent( pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), tangent );
	binPts[0] = physcollision->CollideGetExtent( pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), -binormal );
	binPts[1] = physcollision->CollideGetExtent( pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), binormal );

	// now compute the centered bbox
	float mins[2], maxs[2], center[2], extents[2];
	mins[0] = DotProduct( tanPts[0], tangent );
	maxs[0] = DotProduct( tanPts[1], tangent );

	mins[1] = DotProduct( binPts[0], binormal );
	maxs[1] = DotProduct( binPts[1], binormal );

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

	int contents = enginetrace->GetPointContents( centerPoint-Vector(0,0,2) );

	bool bInSlime = ( contents & CONTENTS_SLIME ) ? true : false;

	Vector	color = vec3_origin;
	float	luminosity = 1.0f;
	
	if ( !bInSlime )
	{
		// Get our lighting information
		FX_GetSplashLighting( centerPoint + ( normal * 8.0f ), &color, &luminosity );
	}

	if ( impactSpeed > 150 )
	{
		if ( bInSlime )
		{
			FX_GunshotSlimeSplash( centerPoint, normal, random->RandomFloat( 8, 10 ) );
		}
		else
		{
			FX_GunshotSplash( centerPoint, normal, random->RandomFloat( 8, 10 ) );
		}
	}
	else if ( !bInSlime )
	{
		FX_WaterRipple( centerPoint, 1.5f, &color, 1.5f, luminosity );
	}
	
	int		splashes = 4;
	Vector	point;

	for ( int i = 0; i < splashes; i++ )
	{
		point = RandomVector( -32.0f, 32.0f );
		point[2] = 0.0f;

		point += corner[i];

		if ( impactSpeed > 150 )
		{
			if ( bInSlime )
			{
				FX_GunshotSlimeSplash( centerPoint, normal, random->RandomFloat( 4, 6 ) );
			}
			else
			{
				FX_GunshotSplash( centerPoint, normal, random->RandomFloat( 4, 6 ) );
			}
		}
		else if ( !bInSlime )
		{
			FX_WaterRipple( point, random->RandomFloat( 0.25f, 0.5f ), &color, luminosity, random->RandomFloat( 0.5f, 1.0f ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
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

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
// Output : float
//-----------------------------------------------------------------------------
float CCollisionEvent::DeltaTimeSinceLastFluid( CBaseEntity *pEntity )
{
	for ( int i = m_fluidEvents.Count()-1; i >= 0; --i )
	{
		if ( m_fluidEvents[i].hEntity.Get() == pEntity )
		{
			return gpGlobals->curtime - m_fluidEvents[i].impactTime;
		}
	}

	int index = m_fluidEvents.AddToTail();
	m_fluidEvents[index].hEntity = pEntity;
	m_fluidEvents[index].impactTime = gpGlobals->curtime;
	return FLUID_TIME_MAX;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject - 
//			*pFluid - 
//-----------------------------------------------------------------------------
void CCollisionEvent::FluidStartTouch( IPhysicsObject *pObject, IPhysicsFluidController *pFluid )
{
	CallbackContext callback(this);
	if ( ( pObject == NULL ) || ( pFluid == NULL ) )
		return;

	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	
	if ( pEntity )
	{
		float timeSinceLastCollision = DeltaTimeSinceLastFluid( pEntity );
		
		if ( timeSinceLastCollision < 0.5f )
			return;

		PhysicsSplash( pFluid, pObject, pEntity );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject - 
//			*pFluid - 
//-----------------------------------------------------------------------------
void CCollisionEvent::FluidEndTouch( IPhysicsObject *pObject, IPhysicsFluidController *pFluid )
{
	CallbackContext callback(this);
	//FIXME: Do nothing for now
}

IPhysicsObject *GetWorldPhysObject ( void )
{
	return g_PhysWorldObject;
}




