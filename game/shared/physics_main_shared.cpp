//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "engine/IEngineSound.h"
#include "mempool.h"
#include "movevars_shared.h"
#include "utlrbtree.h"
#include "tier0/vprof.h"
#include "entitydatainstantiator.h"
#include "positionwatcher.h"
#include "movetype_push.h"
#include "vphysicsupdateai.h"
#include "igamesystem.h"
#include "utlmultilist.h"
#include "tier1/callqueue.h"

#ifdef PORTAL
	#include "portal_util_shared.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


struct watcher_t
{
	EHANDLE				hWatcher;
	IWatcherCallback	*pWatcherCallback;
};

static CUtlMultiList<watcher_t, unsigned short>	g_WatcherList;

// Prints warnings if any entity think functions take longer than this many milliseconds
#ifdef _DEBUG
#define DEF_THINK_LIMIT "20"
#else
#define DEF_THINK_LIMIT "10"
#endif

ConVar think_limit( "think_limit", DEF_THINK_LIMIT, FCVAR_REPLICATED, "Maximum think time in milliseconds, warning is printed if this is exceeded." );



void CWatcherList::Init()
{
	m_list = g_WatcherList.CreateList();
}

CWatcherList::~CWatcherList()
{
	g_WatcherList.DestroyList( m_list );
}

int CWatcherList::GetCallbackObjects( IWatcherCallback **pList, int listMax )
{
	int index = 0;
	unsigned short next = g_WatcherList.InvalidIndex();
	for ( unsigned short node = g_WatcherList.Head( m_list ); node != g_WatcherList.InvalidIndex(); node = next )
	{
		next = g_WatcherList.Next( node );
		watcher_t *pNode = &g_WatcherList.Element(node);
		if ( pNode->hWatcher.Get() )
		{
			pList[index] = pNode->pWatcherCallback;
			index++;
			if ( index >= listMax )
			{
				Assert(0);
				return index;
			}
		}
		else
		{
			g_WatcherList.Remove( m_list, node );
		}
	}
	return index;
}

unsigned short CWatcherList::Find( CBaseEntity *pEntity )
{
	unsigned short next = g_WatcherList.InvalidIndex();
	for ( unsigned short node = g_WatcherList.Head( m_list ); node != g_WatcherList.InvalidIndex(); node = next )
	{
		next = g_WatcherList.Next( node );
		watcher_t *pNode = &g_WatcherList.Element(node);
		if ( pNode->hWatcher.Get() == pEntity )
		{
			return node;
		}
	}
	return g_WatcherList.InvalidIndex();
}

void CWatcherList::RemoveWatcher( CBaseEntity *pEntity )
{
	unsigned short node = Find( pEntity );
	if ( node != g_WatcherList.InvalidIndex() )
	{
		g_WatcherList.Remove( m_list, node );
	}
}


void CWatcherList::AddToList( CBaseEntity *pWatcher )
{
	unsigned short node = Find( pWatcher );
	if ( node == g_WatcherList.InvalidIndex() )
	{
		watcher_t watcher;
		watcher.hWatcher = pWatcher;
			// save this separately so we can use the EHANDLE to test for deletion
		watcher.pWatcherCallback = dynamic_cast<IWatcherCallback *> (pWatcher);

		if ( watcher.pWatcherCallback )
		{
			g_WatcherList.AddToTail( m_list, watcher );
		}
	}
}

void CBaseEntity::AddWatcherToEntity(CBaseEntity* pWatcher, int watcherType)
{
	CWatcherList* pList = (CWatcherList*)GetEngineObject()->GetDataObject(watcherType);
	if (!pList)
	{
		pList = (CWatcherList*)GetEngineObject()->CreateDataObject(watcherType);
		pList->Init();
	}

	pList->AddToList(pWatcher);
}

void CBaseEntity::RemoveWatcherFromEntity(CBaseEntity* pWatcher, int watcherType)
{
	CWatcherList* pList = (CWatcherList*)GetEngineObject()->GetDataObject(watcherType);
	if (pList)
	{
		pList->RemoveWatcher(pWatcher);
	}
}

void CBaseEntity::NotifyPositionChanged()
{
	CWatcherList* pList = (CWatcherList*)GetEngineObject()->GetDataObject(POSITIONWATCHER);
	IWatcherCallback* pCallbacks[1024]; // HACKHACK: Assumes this list is big enough
	int count = pList->GetCallbackObjects(pCallbacks, ARRAYSIZE(pCallbacks));
	for (int i = 0; i < count; i++)
	{
		IPositionWatcher* pWatcher = assert_cast<IPositionWatcher*>(pCallbacks[i]);
		if (pWatcher)
		{
			pWatcher->NotifyPositionChanged(this);
		}
	}
}

void CBaseEntity::NotifyVPhysicsStateChanged(IPhysicsObject* pPhysics, bool bAwake)
{
	CWatcherList* pList = (CWatcherList*)GetEngineObject()->GetDataObject(VPHYSICSWATCHER);
	IWatcherCallback* pCallbacks[1024];	// HACKHACK: Assumes this list is big enough!
	int count = pList->GetCallbackObjects(pCallbacks, ARRAYSIZE(pCallbacks));
	for (int i = 0; i < count; i++)
	{
		IVPhysicsWatcher* pWatcher = assert_cast<IVPhysicsWatcher*>(pCallbacks[i]);
		if (pWatcher)
		{
			pWatcher->NotifyVPhysicsStateChanged(pPhysics, this, bAwake);
		}
	}
}

//void WatchPositionChanges( CBaseEntity *pWatcher, CBaseEntity *pMovingEntity )
//{
//	pMovingEntity->AddWatcherToEntity( pWatcher, POSITIONWATCHER );
//}

//void RemovePositionWatcher( CBaseEntity *pWatcher, CBaseEntity *pMovingEntity )
//{
//	pMovingEntity->RemoveWatcherFromEntity( pWatcher, POSITIONWATCHER );
//}

//void ReportPositionChanged( CBaseEntity *pMovedEntity )
//{
//	if (pMovedEntity)
//	{
//		pMovedEntity->NotifyPositionChanged();
//	}
//}

//void WatchVPhysicsStateChanges( CBaseEntity *pWatcher, CBaseEntity *pPhysicsEntity )
//{
//	pPhysicsEntity->AddWatcherToEntity( pWatcher, VPHYSICSWATCHER );
//}

//void RemoveVPhysicsStateWatcher( CBaseEntity *pWatcher, CBaseEntity *pPhysicsEntity )
//{
//	pPhysicsEntity->RemoveWatcherFromEntity( pWatcher, VPHYSICSWATCHER );
//}

//void ReportVPhysicsStateChanged( IPhysicsObject *pPhysics, CBaseEntity *pEntity, bool bAwake )
//{
//	if (pEntity)
//	{
//		pEntity->NotifyVPhysicsStateChanged( pPhysics, bAwake );
//	}
//}



//-----------------------------------------------------------------------------
// For debugging
//-----------------------------------------------------------------------------

#ifdef GAME_DLL

void SpewLinks()
{
	int nCount = 0;
	for ( CBaseEntity *pClass = gEntList.FirstEnt(); pClass != NULL; pClass = gEntList.NextEnt(pClass) )
	{
		if ( pClass /*&& !pClass->IsDormant()*/ )
		{
			servertouchlink_t *root = (servertouchlink_t* )pClass->GetEngineObject()->GetDataObject( TOUCHLINK );
			if ( root )
			{

				// check if the edict is already in the list
				for ( servertouchlink_t *link = root->nextLink; link != root; link = link->nextLink )
				{
					++nCount;
					Msg("[%d] (%d) Link %d (%s) -> %d (%s)\n", nCount, pClass->IsDormant(),
						pClass->entindex(), pClass->GetClassname(),
						gEntList.GetServerEntityFromHandle(link->entityTouched)->entindex(), ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetClassname() );
				}
			}
		}
	}
}

#endif

//-----------------------------------------------------------------------------
// Returns the actual gravity
//-----------------------------------------------------------------------------
static inline float GetActualGravity( CBaseEntity *pEnt )
{
	float ent_gravity = pEnt->GetGravity();
	if ( ent_gravity == 0.0f )
	{
		ent_gravity = 1.0f;
	}

	return ent_gravity * GetCurrentGravity();
}

#ifdef STAGING_ONLY
#ifndef CLIENT_DLL
ConVar sv_groundlink_debug( "sv_groundlink_debug", "0", FCVAR_NONE, "Enable logging of alloc/free operations for debugging." );
#endif
#endif // STAGING_ONLY



//-----------------------------------------------------------------------------
// Purpose: Returns the mask of what is solid for the given entity
// Output : unsigned int
//-----------------------------------------------------------------------------
unsigned int CBaseEntity::PhysicsSolidMaskForEntity( void ) const
{
	return MASK_SOLID;
}


//-----------------------------------------------------------------------------
// Computes the water level + type
//-----------------------------------------------------------------------------
void CBaseEntity::UpdateWaterState()
{
	// FIXME: This computation is nonsensical for rigid child attachments
	// Should we just grab the type + level of the parent?
	// Probably for rigid children anyways...

	// Compute the point to check for water state
	Vector	point;
	GetEngineObject()->NormalizedToWorldSpace( Vector( 0.5f, 0.5f, 0.0f ), &point );

	SetWaterLevel( 0 );
	SetWaterType( CONTENTS_EMPTY );
	int cont = UTIL_PointContents (point);

	if (( cont & MASK_WATER ) == 0)
		return;

	SetWaterType( cont );
	SetWaterLevel( 1 );

	// point sized entities are always fully submerged
	if ( IsPointSized() )
	{
		SetWaterLevel( 3 );
	}
	else
	{
		// Check the exact center of the box
		point[2] = WorldSpaceCenter().z;

		int midcont = UTIL_PointContents (point);
		if ( midcont & MASK_WATER )
		{
			// Now check where the eyes are...
			SetWaterLevel( 2 );
			point[2] = EyePosition().z;

			int eyecont = UTIL_PointContents (point);
			if ( eyecont & MASK_WATER )
			{
				SetWaterLevel( 3 );
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Check if entity is in the water and applies any current to velocity
// and sets appropriate water flags
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseEntity::PhysicsCheckWater( void )
{
	if (GetEngineObject()->GetMoveParent())
		return GetWaterLevel() > 1;

	int cont = GetWaterType();

	// If we're not in water + don't have a current, we're done
	if ( ( cont & (MASK_WATER | MASK_CURRENT) ) != (MASK_WATER | MASK_CURRENT) )
		return GetWaterLevel() > 1;

	// Compute current direction
	Vector v( 0, 0, 0 );
	if ( cont & CONTENTS_CURRENT_0 )
	{
		v[0] += 1;
	}
	if ( cont & CONTENTS_CURRENT_90 )
	{
		v[1] += 1;
	}
	if ( cont & CONTENTS_CURRENT_180 )
	{
		v[0] -= 1;
	}
	if ( cont & CONTENTS_CURRENT_270 )
	{
		v[1] -= 1;
	}
	if ( cont & CONTENTS_CURRENT_UP )
	{
		v[2] += 1;
	}
	if ( cont & CONTENTS_CURRENT_DOWN )
	{
		v[2] -= 1;
	}

	// The deeper we are, the stronger the current.
	Vector newBaseVelocity;
	VectorMA (GetBaseVelocity(), 50.0*GetWaterLevel(), v, newBaseVelocity);
	SetBaseVelocity( newBaseVelocity );
	
	return GetWaterLevel() > 1;
}


//-----------------------------------------------------------------------------
// Purpose: Bounds velocity
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsCheckVelocity( void )
{
	Vector origin = GetEngineObject()->GetAbsOrigin();
	Vector vecAbsVelocity = GetEngineObject()->GetAbsVelocity();

	bool bReset = false;
	for ( int i=0 ; i<3 ; i++ )
	{
		if ( IS_NAN(vecAbsVelocity[i]) )
		{
			Msg( "Got a NaN velocity on %s\n", GetClassname() );
			vecAbsVelocity[i] = 0;
			bReset = true;
		}
		if ( IS_NAN(origin[i]) )
		{
			Msg( "Got a NaN origin on %s\n", GetClassname() );
			origin[i] = 0;
			bReset = true;
		}

		if ( vecAbsVelocity[i] > sv_maxvelocity.GetFloat() ) 
		{
#ifdef _DEBUG
			DevWarning( 2, "Got a velocity too high on %s\n", GetClassname() );
#endif
			vecAbsVelocity[i] = sv_maxvelocity.GetFloat();
			bReset = true;
		}
		else if ( vecAbsVelocity[i] < -sv_maxvelocity.GetFloat() )
		{
#ifdef _DEBUG
			DevWarning( 2, "Got a velocity too low on %s\n", GetClassname() );
#endif
			vecAbsVelocity[i] = -sv_maxvelocity.GetFloat();
			bReset = true;
		}
	}

	if (bReset)
	{
		GetEngineObject()->SetAbsOrigin( origin );
		GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Applies gravity to falling objects
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsAddGravityMove( Vector &move )
{
	Vector vecAbsVelocity = GetEngineObject()->GetAbsVelocity();

	move.x = (vecAbsVelocity.x + GetBaseVelocity().x ) * gpGlobals->frametime;
	move.y = (vecAbsVelocity.y + GetBaseVelocity().y ) * gpGlobals->frametime;

	if (GetEngineObject()->GetFlags() & FL_ONGROUND )
	{
		move.z = GetBaseVelocity().z * gpGlobals->frametime;
		return;
	}

	// linear acceleration due to gravity
	float newZVelocity = vecAbsVelocity.z - GetActualGravity( this ) * gpGlobals->frametime;

	move.z = ((vecAbsVelocity.z + newZVelocity) / 2.0 + GetBaseVelocity().z ) * gpGlobals->frametime;

	Vector vecBaseVelocity = GetBaseVelocity();
	vecBaseVelocity.z = 0.0f;
	SetBaseVelocity( vecBaseVelocity );
	
	vecAbsVelocity.z = newZVelocity;
	GetEngineObject()->SetAbsVelocity( vecAbsVelocity );

	// Bound velocity
	PhysicsCheckVelocity();
}


#define	STOP_EPSILON	0.1
//-----------------------------------------------------------------------------
// Purpose: Slide off of the impacting object.  Returns the blocked flags (1 = floor, 2 = step / wall)
// Input  : in - 
//			normal - 
//			out - 
//			overbounce - 
// Output : int
//-----------------------------------------------------------------------------
int CBaseEntity::PhysicsClipVelocity( const Vector& in, const Vector& normal, Vector& out, float overbounce )
{
	float	backoff;
	float	change;
	float angle;
	int		i, blocked;
	
	blocked = 0;

	angle = normal[ 2 ];

	if ( angle > 0 )
	{
		blocked |= 1;		// floor
	}
	if ( !angle )
	{
		blocked |= 2;		// step
	}
	
	backoff = DotProduct (in, normal) * overbounce;

	for ( i=0 ; i<3 ; i++ )
	{
		change = normal[i]*backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
		{
			out[i] = 0;
		}
	}
	
	return blocked;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseEntity::ResolveFlyCollisionBounce( trace_t &trace, Vector &vecVelocity, float flMinTotalElasticity )
{
#ifdef HL1_DLL
	flMinTotalElasticity = 0.3f;
#endif//HL1_DLL

	// Get the impact surface's elasticity.
	float flSurfaceElasticity;
	physprops->GetPhysicsProperties( trace.surface.surfaceProps, NULL, NULL, NULL, &flSurfaceElasticity );
	
	float flTotalElasticity = GetElasticity() * flSurfaceElasticity;
	if ( flMinTotalElasticity > 0.9f )
	{
		flMinTotalElasticity = 0.9f;
	}
	flTotalElasticity = clamp( flTotalElasticity, flMinTotalElasticity, 0.9f );

	// NOTE: A backoff of 2.0f is a reflection
	Vector vecAbsVelocity;
	PhysicsClipVelocity(GetEngineObject()->GetAbsVelocity(), trace.plane.normal, vecAbsVelocity, 2.0f );
	vecAbsVelocity *= flTotalElasticity;

	// Get the total velocity (player + conveyors, etc.)
	VectorAdd( vecAbsVelocity, GetBaseVelocity(), vecVelocity );
	float flSpeedSqr = DotProduct( vecVelocity, vecVelocity );

	// Stop if on ground.
	if ( trace.plane.normal.z > 0.7f )			// Floor
	{
		// Verify that we have an entity.
		CBaseEntity *pEntity = (CBaseEntity*)trace.m_pEnt;
		Assert( pEntity );

		// Are we on the ground?
		if ( vecVelocity.z < ( GetActualGravity( this ) * gpGlobals->frametime ) )
		{
			vecAbsVelocity.z = 0.0f;

			// Recompute speedsqr based on the new absvel
			VectorAdd( vecAbsVelocity, GetBaseVelocity(), vecVelocity );
			flSpeedSqr = DotProduct( vecVelocity, vecVelocity );
		}

		GetEngineObject()->SetAbsVelocity( vecAbsVelocity );

		if ( flSpeedSqr < ( 30 * 30 ) )
		{
			if ( pEntity->IsStandable() )
			{
				GetEngineObject()->SetGroundEntity( pEntity->GetEngineObject() );
			}

			// Reset velocities.
			GetEngineObject()->SetAbsVelocity( vec3_origin );
			SetLocalAngularVelocity( vec3_angle );
		}
		else
		{
			Vector vecDelta = GetBaseVelocity() - vecAbsVelocity;	
			Vector vecBaseDir = GetBaseVelocity();
			VectorNormalize( vecBaseDir );
			float flScale = vecDelta.Dot( vecBaseDir );

			VectorScale( vecAbsVelocity, ( 1.0f - trace.fraction ) * gpGlobals->frametime, vecVelocity ); 
			VectorMA( vecVelocity, ( 1.0f - trace.fraction ) * gpGlobals->frametime, GetBaseVelocity() * flScale, vecVelocity );
			PhysicsPushEntity( vecVelocity, &trace );
		}
	}
	else
	{
		// If we get *too* slow, we'll stick without ever coming to rest because
		// we'll get pushed down by gravity faster than we can escape from the wall.
		if ( flSpeedSqr < ( 30 * 30 ) )
		{
			// Reset velocities.
			GetEngineObject()->SetAbsVelocity( vec3_origin );
			SetLocalAngularVelocity( vec3_angle );
		}
		else
		{
			GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseEntity::ResolveFlyCollisionSlide( trace_t &trace, Vector &vecVelocity )
{
	// Get the impact surface's friction.
	float flSurfaceFriction;
	physprops->GetPhysicsProperties( trace.surface.surfaceProps, NULL, NULL, &flSurfaceFriction, NULL );

	// A backoff of 1.0 is a slide.
	float flBackOff = 1.0f;	
	Vector vecAbsVelocity;
	PhysicsClipVelocity(GetEngineObject()->GetAbsVelocity(), trace.plane.normal, vecAbsVelocity, flBackOff );

	if ( trace.plane.normal.z <= 0.7 )			// Floor
	{
		GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
		return;
	}

	// Stop if on ground.
	// Get the total velocity (player + conveyors, etc.)
	VectorAdd( vecAbsVelocity, GetBaseVelocity(), vecVelocity );
	float flSpeedSqr = DotProduct( vecVelocity, vecVelocity );

	// Verify that we have an entity.
	CBaseEntity *pEntity = (CBaseEntity*)trace.m_pEnt;
	Assert( pEntity );

	// Are we on the ground?
	if ( vecVelocity.z < ( GetActualGravity( this ) * gpGlobals->frametime ) )
	{
		vecAbsVelocity.z = 0.0f;

		// Recompute speedsqr based on the new absvel
		VectorAdd( vecAbsVelocity, GetBaseVelocity(), vecVelocity );
		flSpeedSqr = DotProduct( vecVelocity, vecVelocity );
	}
	GetEngineObject()->SetAbsVelocity( vecAbsVelocity );

	if ( flSpeedSqr < ( 30 * 30 ) )
	{
		if ( pEntity->IsStandable() )
		{
			GetEngineObject()->SetGroundEntity( pEntity->GetEngineObject());
		}

		// Reset velocities.
		GetEngineObject()->SetAbsVelocity( vec3_origin );
		SetLocalAngularVelocity( vec3_angle );
	}
	else
	{
		vecAbsVelocity += GetBaseVelocity();
		vecAbsVelocity *= ( 1.0f - trace.fraction ) * gpGlobals->frametime * flSurfaceFriction;
		PhysicsPushEntity( vecAbsVelocity, &trace );
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseEntity::ResolveFlyCollisionCustom( trace_t &trace, Vector &vecVelocity )
{
	// Stop if on ground.
	if ( trace.plane.normal.z > 0.7 )			// Floor
	{
		// Get the total velocity (player + conveyors, etc.)
		VectorAdd(GetEngineObject()->GetAbsVelocity(), GetBaseVelocity(), vecVelocity );

		// Verify that we have an entity.
		CBaseEntity *pEntity = (CBaseEntity*)trace.m_pEnt;
		Assert( pEntity );

		// Are we on the ground?
		if ( vecVelocity.z < ( GetActualGravity( this ) * gpGlobals->frametime ) )
		{
			Vector vecAbsVelocity = GetEngineObject()->GetAbsVelocity();
			vecAbsVelocity.z = 0.0f;
			GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
		}

		if ( pEntity->IsStandable() )
		{
			GetEngineObject()->SetGroundEntity( pEntity->GetEngineObject() );
		}
	}
}

//-----------------------------------------------------------------------------
// Performs the collision resolution for fliers.
//-----------------------------------------------------------------------------
void CBaseEntity::PerformFlyCollisionResolution( trace_t &trace, Vector &move )
{
	switch( GetMoveCollide() )
	{
	case MOVECOLLIDE_FLY_CUSTOM:
		{
			ResolveFlyCollisionCustom( trace, move );
			break;
		}

	case MOVECOLLIDE_FLY_BOUNCE:
		{
			ResolveFlyCollisionBounce( trace, move );
			break;
		}

	case MOVECOLLIDE_FLY_SLIDE:
	case MOVECOLLIDE_DEFAULT:
	// NOTE: The default fly collision state is the same as a slide (for backward capatability).
		{
			ResolveFlyCollisionSlide( trace, move );
			break;
		}

	default:
		{
			// Invalid MOVECOLLIDE_<type>
			Assert( 0 );
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Checks if an object has passed into or out of water and sets water info, alters velocity, plays splash sounds, etc.
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsCheckWaterTransition( void )
{
	int oldcont = GetWaterType();
	UpdateWaterState();
	int cont = GetWaterType();

	// We can exit right out if we're a child... don't bother with this...
	if (GetEngineObject()->GetMoveParent())
		return;

	if ( cont & MASK_WATER )
	{
		if (oldcont == CONTENTS_EMPTY)
		{
#ifndef CLIENT_DLL
			Splash();
#endif // !CLIENT_DLL

			// just crossed into water
			const char* soundname = "BaseEntity.EnterWater";
			CPASAttenuationFilter filter(this, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);

			if ( !GetEngineObject()->IsEFlagSet( EFL_NO_WATER_VELOCITY_CHANGE ) )
			{
				Vector vecAbsVelocity = GetEngineObject()->GetAbsVelocity();
				vecAbsVelocity[2] *= 0.5;
				GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
			}
		}
	}
	else
	{
		if ( oldcont != CONTENTS_EMPTY )
		{	
			// just crossed out of water
			const char* soundname = "BaseEntity.ExitWater";
			CPASAttenuationFilter filter(this, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
		}		
	}
}

//-----------------------------------------------------------------------------
// Computes new angles based on the angular velocity
//-----------------------------------------------------------------------------
void CBaseEntity::SimulateAngles( float flFrameTime )
{
	// move angles
	QAngle angles;
	VectorMA (GetEngineObject()->GetLocalAngles(), flFrameTime, GetLocalAngularVelocity(), angles );
	GetEngineObject()->SetLocalAngles( angles );
}


//-----------------------------------------------------------------------------
// Purpose: Toss, bounce, and fly movement.  When onground, do nothing.
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsToss( void )
{
	trace_t	trace;
	Vector	move;

	PhysicsCheckWater();

	// regular thinking
	if ( !PhysicsRunThink() )
		return;

	// Moving upward, off the ground, or  resting on a client/monster, remove FL_ONGROUND
	if (GetEngineObject()->GetAbsVelocity()[2] > 0 || !GetEngineObject()->GetGroundEntity() || !GetEngineObject()->GetGroundEntity()->GetOuter()->IsStandable())
	{
		GetEngineObject()->SetGroundEntity( NULL );
	}

	// Check to see if entity is on the ground at rest
	if (GetEngineObject()->GetFlags() & FL_ONGROUND )
	{
		if ( VectorCompare(GetEngineObject()->GetAbsVelocity(), vec3_origin ) )
		{
			// Clear rotation if not moving (even if on a conveyor)
			SetLocalAngularVelocity( vec3_angle );
			if ( VectorCompare( GetBaseVelocity(), vec3_origin ) )
				return;
		}
	}

	PhysicsCheckVelocity();

	// add gravity
	if ( GetMoveType() == MOVETYPE_FLYGRAVITY && !(GetEngineObject()->GetFlags() & FL_FLY) )
	{
		PhysicsAddGravityMove( move );
	}
	else
	{
		// Base velocity is not properly accounted for since this entity will move again after the bounce without
		// taking it into account
		Vector vecAbsVelocity = GetEngineObject()->GetAbsVelocity();
		vecAbsVelocity += GetBaseVelocity();
		VectorScale(vecAbsVelocity, gpGlobals->frametime, move);
		PhysicsCheckVelocity( );
	}

	// move angles
	SimulateAngles( gpGlobals->frametime );

	// move origin
	PhysicsPushEntity( move, &trace );

#if !defined( CLIENT_DLL )
	if ( VPhysicsGetObject() )
	{
		VPhysicsGetObject()->UpdateShadow(GetEngineObject()->GetAbsOrigin(), vec3_angle, true, gpGlobals->frametime );
	}
#endif

	PhysicsCheckVelocity();

	if (trace.allsolid )
	{	
		// entity is trapped in another solid
		// UNDONE: does this entity needs to be removed?
		GetEngineObject()->SetAbsVelocity(vec3_origin);
		SetLocalAngularVelocity(vec3_angle);
		return;
	}
	
#if !defined( CLIENT_DLL )
	if (GetEngineObject()->IsMarkedForDeletion())//engine->IsEdictFree(entindex())
		return;
#endif

	if (trace.fraction != 1.0f)
	{
		PerformFlyCollisionResolution( trace, move );
	}
	
	// check for in water
	PhysicsCheckWaterTransition();
}


//-----------------------------------------------------------------------------
// Simulation in local space of rigid children
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsRigidChild( void )
{
	VPROF("CBaseEntity::PhysicsRigidChild");
	// NOTE: rigidly attached children do simulation in local space
	// Collision impulses will be handled either not at all, or by
	// forwarding the information to the highest move parent

	Vector vecPrevOrigin = GetEngineObject()->GetAbsOrigin();

	// regular thinking
	if ( !PhysicsRunThink() )
		return;

	VPROF_SCOPE_BEGIN("CBaseEntity::PhysicsRigidChild-2");

#if !defined( CLIENT_DLL )
	// Cause touch functions to be called
	GetEngineObject()->PhysicsTouchTriggers( &vecPrevOrigin );

	// We have to do this regardless owing to hierarchy
	if ( VPhysicsGetObject() )
	{
		int solidType = GetEngineObject()->GetSolid();
		bool bAxisAligned = ( solidType == SOLID_BBOX || solidType == SOLID_NONE ) ? true : false;
		VPhysicsGetObject()->UpdateShadow(GetEngineObject()->GetAbsOrigin(), bAxisAligned ? vec3_angle : GetEngineObject()->GetAbsAngles(), true, gpGlobals->frametime );
	}
#endif

	VPROF_SCOPE_END();
}


//-----------------------------------------------------------------------------
// Computes the base velocity
//-----------------------------------------------------------------------------
void CBaseEntity::UpdateBaseVelocity( void )
{
#if !defined( CLIENT_DLL )
	if (GetEngineObject()->GetFlags() & FL_ONGROUND )
	{
		CBaseEntity* groundentity = GetEngineObject()->GetGroundEntity() ? GetEngineObject()->GetGroundEntity()->GetOuter() : NULL;
		if ( groundentity )
		{
			// On conveyor belt that's moving?
			if ( groundentity->GetEngineObject()->GetFlags() & FL_CONVEYOR )
			{
				Vector vecNewBaseVelocity;
				groundentity->GetGroundVelocityToApply( vecNewBaseVelocity );
				if (GetEngineObject()->GetFlags() & FL_BASEVELOCITY )
				{
					vecNewBaseVelocity += GetBaseVelocity();
				}
				GetEngineObject()->AddFlag( FL_BASEVELOCITY );
				SetBaseVelocity( vecNewBaseVelocity );
			}
		}
	}
#endif
}


//-----------------------------------------------------------------------------
// Purpose: Runs a frame of physics for a specific edict (and all it's children)
// Input  : *ent - the thinking edict
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsSimulate( void )
{
	VPROF( "CBaseEntity::PhysicsSimulate" );
	// NOTE:  Players override PhysicsSimulate and drive through their CUserCmds at that point instead of
	//  processng through this function call!!!  They shouldn't chain to here ever.
	// Make sure not to simulate this guy twice per frame
	if (m_nSimulationTick == gpGlobals->tickcount)
		return;

	m_nSimulationTick = gpGlobals->tickcount;

	Assert( !IsPlayer() );

	// If we've got a moveparent, we must simulate that first.
	CBaseEntity *pMoveParent = GetEngineObject()->GetMoveParent()?GetEngineObject()->GetMoveParent()->GetOuter():NULL;

	if ( (GetMoveType() == MOVETYPE_NONE && !pMoveParent) || (GetMoveType() == MOVETYPE_VPHYSICS ) )
	{
		PhysicsNone();
		return;
	}

	// If ground entity goes away, make sure FL_ONGROUND is valid
	if ( !GetEngineObject()->GetGroundEntity() )
	{
		GetEngineObject()->RemoveFlag( FL_ONGROUND );
	}

	if (pMoveParent)
	{
		VPROF( "CBaseEntity::PhysicsSimulate-MoveParent" );
		pMoveParent->PhysicsSimulate();
	}
	else
	{
		VPROF( "CBaseEntity::PhysicsSimulate-BaseVelocity" );

		UpdateBaseVelocity();

		if ( ((GetEngineObject()->GetFlags() & FL_BASEVELOCITY) == 0) && (GetBaseVelocity() != vec3_origin) )
		{
			// Apply momentum (add in half of the previous frame of velocity first)
			// BUGBUG: This will break with PhysicsStep() because of the timestep difference
			Vector vecAbsVelocity;
			VectorMA(GetEngineObject()->GetAbsVelocity(), 1.0 + (gpGlobals->frametime*0.5), GetBaseVelocity(), vecAbsVelocity );
			GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
			SetBaseVelocity( vec3_origin );
		}
		GetEngineObject()->RemoveFlag( FL_BASEVELOCITY );
	}

	switch( GetMoveType() )
	{
	case MOVETYPE_PUSH:
		{
			VPROF( "CBaseEntity::PhysicsSimulate-MOVETYPE_PUSH" );
			PhysicsPusher();
		}
		break;


	case MOVETYPE_VPHYSICS:
		{
		}
		break;

	case MOVETYPE_NONE:
		{
			VPROF( "CBaseEntity::PhysicsSimulate-MOVETYPE_NONE" );
			Assert(pMoveParent);
			PhysicsRigidChild();
		}
		break;

	case MOVETYPE_NOCLIP:
		{
			VPROF( "CBaseEntity::PhysicsSimulate-MOVETYPE_NOCLIP" );
			PhysicsNoclip();
		}
		break;

	case MOVETYPE_STEP:
		{
			VPROF( "CBaseEntity::PhysicsSimulate-MOVETYPE_STEP" );
			PhysicsStep();
		}
		break;

	case MOVETYPE_FLY:
	case MOVETYPE_FLYGRAVITY:
		{
			VPROF( "CBaseEntity::PhysicsSimulate-MOVETYPE_FLY" );
			PhysicsToss();
		}
		break;

	case MOVETYPE_CUSTOM:
		{
			VPROF( "CBaseEntity::PhysicsSimulate-MOVETYPE_CUSTOM" );
			PhysicsCustom();
		}
		break;

	default:
		Warning( "PhysicsSimulate: %s bad movetype %d", GetClassname(), GetMoveType() );
		Assert(0);
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Runs thinking code if time.  There is some play in the exact time the think
//  function will be called, because it is called before any movement is done
//  in a frame.  Not used for pushmove objects, because they must be exact.
//  Returns false if the entity removed itself.
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseEntity::PhysicsRunThink( thinkmethods_t thinkMethod )
{
	if (GetEngineObject()->IsEFlagSet( EFL_NO_THINK_FUNCTION ) )
		return true;
	
	bool bAlive = true;

	// Don't fire the base if we're avoiding it
	if ( thinkMethod != THINK_FIRE_ALL_BUT_BASE )
	{
		bAlive = PhysicsRunSpecificThink( -1, &CBaseEntity::Think );
		if ( !bAlive )
			return false;
	}

	// Are we just firing the base think?
	if ( thinkMethod == THINK_FIRE_BASE_ONLY )
		return bAlive;

	// Fire the rest of 'em
	for ( int i = 0; i < m_aThinkFunctions.Count(); i++ )
	{
#ifdef _DEBUG
		// Set the context
		m_iCurrentThinkContext = i;
#endif

		bAlive = PhysicsRunSpecificThink( i, m_aThinkFunctions[i].m_pfnThink );

#ifdef _DEBUG
		// Clear our context
		m_iCurrentThinkContext = NO_THINK_CONTEXT;
#endif

		if ( !bAlive )
			return false;
	}
	
	return bAlive;
}

//-----------------------------------------------------------------------------
// Purpose: For testing if all thinks are occuring at the same time
//-----------------------------------------------------------------------------
struct ThinkSync
{
	float					thinktime;
	int						thinktick;
	CUtlVector< EHANDLE >	entities;

	ThinkSync()
	{
		thinktime = 0;
	}

	ThinkSync( const ThinkSync& src )
	{
		thinktime = src.thinktime;
		thinktick = src.thinktick;
		int c = src.entities.Count();
		for ( int i = 0; i < c; i++ )
		{
			entities.AddToTail( src.entities[ i ] );
		}
	}
};

#if !defined( CLIENT_DLL )
static ConVar sv_thinktimecheck( "sv_thinktimecheck", "0", 0, "Check for thinktimes all on same timestamp." );
#endif

//-----------------------------------------------------------------------------
// Purpose: For testing if all thinks are occuring at the same time
//-----------------------------------------------------------------------------
class CThinkSyncTester
{
public:
	CThinkSyncTester() :
	  m_Thinkers( 0, 0, ThinkLessFunc )
	{
		  m_nLastFrameCount = -1;
		  m_bShouldCheck = false;
	}

	void EntityThinking( int framecount, CBaseEntity *ent, float thinktime, int thinktick )
	{
#if !defined( CLIENT_DLL )
		if ( m_nLastFrameCount != framecount )
		{
			if ( m_bShouldCheck )
			{
				// Report
				Report();
				m_Thinkers.RemoveAll();
				m_nLastFrameCount = framecount;
			}

			m_bShouldCheck = sv_thinktimecheck.GetBool();
		}

		if ( !m_bShouldCheck )
			return;

		ThinkSync *p = FindOrAddItem( ent, thinktime );
		if ( !p )
		{
			Assert( 0 );
		}

		p->thinktime = thinktime;
		p->thinktick = thinktick;
		EHANDLE h;
		h = ent;
		p->entities.AddToTail( h );
#endif
	}

private:

	static bool ThinkLessFunc( const ThinkSync& item1, const ThinkSync& item2 )
	{
		return item1.thinktime < item2.thinktime;
	}

	ThinkSync	*FindOrAddItem( CBaseEntity *ent, float thinktime )
	{
		ThinkSync item;
		item.thinktime = thinktime;

		int idx = m_Thinkers.Find( item );
		if ( idx == m_Thinkers.InvalidIndex() )
		{
			idx = m_Thinkers.Insert( item );
		}
		
		return &m_Thinkers[ idx ];
	}

	void Report()
	{
		if ( m_Thinkers.Count() == 0 )
			return;

		Msg( "-----------------\nThink report frame %i\n", gpGlobals->tickcount );

		for ( int i = m_Thinkers.FirstInorder(); 
			i != m_Thinkers.InvalidIndex(); 
			i = m_Thinkers.NextInorder( i ) )
		{
			ThinkSync *p = &m_Thinkers[ i ];
			Assert( p );
			if ( !p )
				continue;

			int ecount = p->entities.Count();
			if ( !ecount )
			{
				continue;
			}

			Msg( "thinktime %f, %i entities\n", p->thinktime, ecount );
			for ( int j =0; j < ecount; j++ )
			{
				EHANDLE h = p->entities[ j ];
				int lastthinktick = 0;
				int nextthinktick = 0;
				CBaseEntity *e = h.Get();
				if ( e )
				{
					lastthinktick = e->m_nLastThinkTick;
					nextthinktick = e->m_nNextThinkTick;
				}

				Msg( "  %p : %30s (last %5i/next %5i)\n", h.Get(), h.Get() ? h->GetClassname() : "NULL",
					lastthinktick, nextthinktick );
			}
		}
	}

	CUtlRBTree< ThinkSync >	m_Thinkers;
	int			m_nLastFrameCount;
	bool		m_bShouldCheck;
};

static CThinkSyncTester g_ThinkChecker;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseEntity::PhysicsRunSpecificThink( int nContextIndex, BASEPTR thinkFunc )
{
	int thinktick = GetNextThinkTick( nContextIndex );

	if ( thinktick <= 0 || thinktick > gpGlobals->tickcount )
		return true;
	
	float thinktime = thinktick * TICK_INTERVAL;

	// Don't let things stay in the past.
	//  it is possible to start that way
	//  by a trigger with a local time.
	if ( thinktime < gpGlobals->curtime )
	{
		thinktime = gpGlobals->curtime;	
	}
	
	// Only do this on the game server
#if !defined( CLIENT_DLL )
	g_ThinkChecker.EntityThinking( gpGlobals->tickcount, this, thinktime, m_nNextThinkTick );
#endif

	SetNextThink( nContextIndex, TICK_NEVER_THINK );

	PhysicsDispatchThink( thinkFunc );

	SetLastThink( nContextIndex, gpGlobals->curtime );

	// Return whether entity is still valid
	return ( !GetEngineObject()->IsMarkedForDeletion() );
}

void CBaseEntity::StartGroundContact( CBaseEntity *ground )
{
	GetEngineObject()->AddFlag( FL_ONGROUND );
//	Msg( "+++ %s starting contact with ground %s\n", GetClassname(), ground->GetClassname() );
}

void CBaseEntity::EndGroundContact( CBaseEntity *ground )
{
	GetEngineObject()->RemoveFlag( FL_ONGROUND );
//	Msg( "--- %s ending contact with ground %s\n", GetClassname(), ground->GetClassname() );
}


void CBaseEntity::SetGroundChangeTime( float flTime )
{
	m_flGroundChangeTime = flTime;
}

float CBaseEntity::GetGroundChangeTime( void )
{
	return m_flGroundChangeTime;
}



// Remove this as ground entity for all object resting on this object
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseEntity::WakeRestingObjects()
{
	// Unset this as ground entity for everything resting on this object
	//  This calls endgroundcontact for everything on the list
	GetEngineObject()->PhysicsRemoveGroundList();
}


