//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "portal_util_shared.h"
#include "prop_portal_shared.h"
#include "portal_shareddefs.h"
#include "portal_collideable_enumerator.h"
#include "beam_shared.h"
#include "collisionutils.h"
#include "util_shared.h"
#ifndef CLIENT_DLL
	#include "util.h"
	#include "ndebugoverlay.h"
	#include "env_debughistory.h"
#else
	#include "c_portal_player.h"
#endif
#include "PortalSimulation.h"

bool g_bAllowForcePortalTrace = false;
bool g_bForcePortalTrace = false;
bool g_bBulletPortalTrace = false;

ConVar sv_use_find_closest_passable_space("sv_use_find_closest_passable_space", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Enables heavy-handed player teleporting stuck fix code.");

Color UTIL_Portal_Color( int iPortal )
{
	switch ( iPortal )
	{
		case 0:
			// GRAVITY BEAM
			return Color( 242, 202, 167, 255 );

		case 1:
			// PORTAL 1
			return Color( 64, 160, 255, 255 );

		case 2:
			// PORTAL 2
			return Color( 255, 160, 32, 255 );
	}

	return Color( 255, 255, 255, 255 );
}

void UTIL_Portal_Trace_Filter( CTraceFilterSimpleClassnameList *traceFilterPortalShot )
{
	traceFilterPortalShot->AddClassnameToIgnore( "prop_physics" );
	traceFilterPortalShot->AddClassnameToIgnore( "func_physbox" );
	traceFilterPortalShot->AddClassnameToIgnore( "npc_portal_turret_floor" );
	traceFilterPortalShot->AddClassnameToIgnore( "prop_energy_ball" );
	traceFilterPortalShot->AddClassnameToIgnore( "npc_security_camera" );
	traceFilterPortalShot->AddClassnameToIgnore( "player" );
	traceFilterPortalShot->AddClassnameToIgnore( "simple_physics_prop" );
	traceFilterPortalShot->AddClassnameToIgnore( "simple_physics_brush" );
	traceFilterPortalShot->AddClassnameToIgnore( "prop_ragdoll" );
	traceFilterPortalShot->AddClassnameToIgnore( "prop_glados_core" );
	traceFilterPortalShot->AddClassnameToIgnore( "updateitem2" );
}


CProp_Portal* UTIL_Portal_FirstAlongRay( const Ray_t &ray, float &fMustBeCloserThan )
{
	CProp_Portal *pIntersectedPortal = NULL;

	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if( iPortalCount != 0 )
	{
		CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();

		for( int i = 0; i != iPortalCount; ++i )
		{
			CProp_Portal *pTempPortal = pPortals[i];
			if( pTempPortal->IsActivedAndLinked() )
			{
				float fIntersection = UTIL_IntersectRayWithPortal( ray, pTempPortal );
				if( fIntersection >= 0.0f && fIntersection < fMustBeCloserThan )
				{
					//within range, now check directionality
					if( pTempPortal->m_plane_Origin.normal.Dot( ray.m_Delta ) < 0.0f )
					{
						//qualifies for consideration, now it just has to compete for closest
						pIntersectedPortal = pTempPortal;
						fMustBeCloserThan = fIntersection;
					}
				}
			}
		}
	}

	return pIntersectedPortal;
}


bool UTIL_Portal_TraceRay_Bullets( const CProp_Portal *pPortal, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
	if( !pPortal || !pPortal->IsActivedAndLinked() )
	{
		//not in a portal environment, use regular traces
		enginetrace->TraceRay( ray, fMask, pTraceFilter, pTrace );
		return false;
	}

	trace_t trReal;

	enginetrace->TraceRay( ray, fMask, pTraceFilter, &trReal );

	Vector vRayNormal = ray.m_Delta;
	VectorNormalize( vRayNormal );

	Vector vPortalForward;
	pPortal->GetEngineObject()->GetVectors( &vPortalForward, 0, 0 );

	// If the ray isn't going into the front of the portal, just use the real trace
	if ( vPortalForward.Dot( vRayNormal ) > 0.0f )
	{
		*pTrace = trReal;
		return false;
	}

	// If the real trace collides before the portal plane, just use the real trace
	float fPortalFraction = UTIL_IntersectRayWithPortal( ray, pPortal );

	if ( fPortalFraction == -1.0f || trReal.fraction + 0.0001f < fPortalFraction )
	{
		// Didn't intersect or the real trace intersected closer
		*pTrace = trReal;
		return false;
	}

	Ray_t rayPostPortal;
	rayPostPortal = ray;
	rayPostPortal.m_Start = ray.m_Start + ray.m_Delta * fPortalFraction;
	rayPostPortal.m_Delta = ray.m_Delta * ( 1.0f - fPortalFraction );

	VMatrix matThisToLinked = pPortal->MatrixThisToLinked();

	Ray_t rayTransformed;
	UTIL_Portal_RayTransform( matThisToLinked, rayPostPortal, rayTransformed );

	// After a bullet traces through a portal it can hit the player that fired it
	CTraceFilterSimple *pSimpleFilter = dynamic_cast<CTraceFilterSimple*>(pTraceFilter);
	const IHandleEntity *pPassEntity = NULL;
	if ( pSimpleFilter )
	{
		pPassEntity = pSimpleFilter->GetPassEntity();
		pSimpleFilter->SetPassEntity( 0 );
	}

	trace_t trPostPortal;
	enginetrace->TraceRay( rayTransformed, fMask, pTraceFilter, &trPostPortal );

	if ( pSimpleFilter )
	{
		pSimpleFilter->SetPassEntity( pPassEntity );
	}

	//trPostPortal.startpos = ray.m_Start;
	UTIL_Portal_PointTransform( matThisToLinked, ray.m_Start, trPostPortal.startpos );
	trPostPortal.fraction = trPostPortal.fraction * ( 1.0f - fPortalFraction ) + fPortalFraction;

	*pTrace = trPostPortal;

	return true;
}

CProp_Portal* UTIL_Portal_TraceRay_Beam( const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, float *pfFraction )
{
	// Do a regular trace
	trace_t tr;
	UTIL_TraceLine( ray.m_Start, ray.m_Start + ray.m_Delta, fMask, pTraceFilter, &tr );
	float fMustBeCloserThan = tr.fraction + 0.0001f;

	CProp_Portal *pIntersectedPortal = UTIL_Portal_FirstAlongRay( ray, fMustBeCloserThan );

	*pfFraction = fMustBeCloserThan; //will be real trace distance if it didn't hit a portal
	return pIntersectedPortal;
}


bool UTIL_Portal_Trace_Beam( const CBeam *pBeam, Vector &vecStart, Vector &vecEnd, Vector &vecIntersectionStart, Vector &vecIntersectionEnd, ITraceFilter *pTraceFilter )
{
	vecStart = pBeam->GetAbsStartPos();
	vecEnd = pBeam->GetAbsEndPos();

	// Trace to see if we've intersected a portal
	float fEndFraction;
	Ray_t rayBeam;

	bool bIsReversed = ( pBeam->GetBeamFlags() & FBEAM_REVERSED ) != 0x0;

	if ( !bIsReversed )
		rayBeam.Init( vecStart, vecEnd );
	else
		rayBeam.Init( vecEnd, vecStart );

	CProp_Portal *pPortal = UTIL_Portal_TraceRay_Beam( rayBeam, MASK_SHOT, pTraceFilter, &fEndFraction );

	// If we intersected a portal we need to modify the start and end points to match the actual trace through portal drawing extents
	if ( !pPortal )
		return false;

	// Modify the start and end points to match the actual trace through portal drawing extents
	vecStart = rayBeam.m_Start;

	Vector vecIntersection = rayBeam.m_Start + rayBeam.m_Delta * fEndFraction;

	int iNumLoops = 0;

	// Loop through the portals (at most 16 times)
	while ( pPortal && iNumLoops < 16 )
	{
		// Get the point that we hit a portal or wall
		vecIntersectionStart = vecIntersection;

		VMatrix matThisToLinked = pPortal->MatrixThisToLinked();

		// Get the transformed positions of the sub beam in the other portal's space
		UTIL_Portal_PointTransform( matThisToLinked, vecIntersectionStart, vecIntersectionEnd );
		UTIL_Portal_PointTransform( matThisToLinked, rayBeam.m_Start + rayBeam.m_Delta, vecEnd );

		CTraceFilterSkipClassname traceFilter( pPortal->m_hLinkedPortal, "prop_energy_ball", COLLISION_GROUP_NONE );

		rayBeam.Init( vecIntersectionEnd, vecEnd );
		pPortal = UTIL_Portal_TraceRay_Beam( rayBeam, MASK_SHOT, &traceFilter, &fEndFraction );
		vecIntersection = rayBeam.m_Start + rayBeam.m_Delta * fEndFraction;

		++iNumLoops;
	}

	vecEnd = vecIntersection;

	return true;
}


void UTIL_Portal_TraceRay_With( const CProp_Portal *pPortal, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
	//check to see if the player is theoretically in a portal environment
	if( !pPortal || !pPortal->m_hPortalSimulator->IsReadyToSimulate() )
	{
		//not in a portal environment, use regular traces
		enginetrace->TraceRay( ray, fMask, pTraceFilter, pTrace );
	}
	else
	{		

		trace_t RealTrace;
		enginetrace->TraceRay( ray, fMask, pTraceFilter, &RealTrace );

		trace_t PortalTrace;
		UTIL_Portal_TraceRay( pPortal, ray, fMask, pTraceFilter, &PortalTrace, bTraceHolyWall );

		if( !g_bForcePortalTrace && !RealTrace.startsolid && PortalTrace.fraction <= RealTrace.fraction )
		{
			*pTrace = RealTrace;
			return;
		}

		if ( g_bAllowForcePortalTrace )
		{
			g_bForcePortalTrace = true;
		}

		*pTrace = PortalTrace;

		// If this ray has a delta, make sure its towards the portal before we try to trace across portals
		Vector vDirection = ray.m_Delta;
		VectorNormalize( vDirection );
		Vector vPortalForward;
		pPortal->GetEngineObject()->GetVectors( &vPortalForward, 0, 0 );
		
		float flDot = -1.0f;
		if ( ray.m_IsSwept )
		{
			flDot = vDirection.Dot( vPortalForward );
		} 

		// TODO: Translate extents of rays properly, tracing extruded box rays across portals causes collision bugs
		//		 Until this is fixed, we'll only test true rays across portals
		if ( flDot < 0.0f && /*PortalTrace.fraction == 1.0f &&*/ ray.m_IsRay)
		{
			// Check if we're hitting stuff on the other side of the portal
			trace_t PortalLinkedTrace;
			UTIL_PortalLinked_TraceRay( pPortal, ray, fMask, pTraceFilter, &PortalLinkedTrace, bTraceHolyWall );

			if ( PortalLinkedTrace.fraction < pTrace->fraction )
			{
				// Only collide with the cross-portal objects if this trace crossed a portal
				if ( UTIL_DidTraceTouchPortals( ray, PortalLinkedTrace ) )
				{
					*pTrace = PortalLinkedTrace;
				}
			}
		}

		if( pTrace->fraction < 1.0f )
		{
			pTrace->contents = RealTrace.contents;
			pTrace->surface = RealTrace.surface;
		}

	}
}


//-----------------------------------------------------------------------------
// Purpose: Tests if a ray touches the surface of any portals
// Input  : ray - the ray to be tested against portal surfaces
//			trace - a filled-in trace corresponding to the parameter ray 
// Output : bool - false if the 'ray' parameter failed to hit any portal surface
//		    pOutLocal - the portal touched (if any)
//			pOutRemote - the portal linked to the portal touched
//-----------------------------------------------------------------------------
bool UTIL_DidTraceTouchPortals( const Ray_t& ray, const trace_t& trace, CProp_Portal** pOutLocal, CProp_Portal** pOutRemote )
{
	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if( iPortalCount == 0 )
	{
		if( pOutLocal )
			*pOutLocal = NULL;

		if( pOutRemote )
			*pOutRemote = NULL;

		return false;
	}

	CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();
	CProp_Portal *pIntersectedPortal = NULL;

	if( ray.m_IsSwept )
	{
		float fMustBeCloserThan = trace.fraction + 0.0001f;

		pIntersectedPortal = UTIL_Portal_FirstAlongRay( ray, fMustBeCloserThan );
	}
	
	if( (pIntersectedPortal == NULL) && !ray.m_IsRay )
	{
		//haven't hit anything yet, try again with box tests

		Vector ptRayEndPoint = trace.endpos - ray.m_StartOffset; // The trace added the start offset to the end position, so remove it for the box test
		CProp_Portal **pBoxIntersectsPortals = (CProp_Portal **)stackalloc( sizeof(CProp_Portal *) * iPortalCount );
		int iBoxIntersectsPortalsCount = 0;

		for( int i = 0; i != iPortalCount; ++i )
		{
			CProp_Portal *pTempPortal = pPortals[i];
			if( (pTempPortal->m_bActivated) && 
				(pTempPortal->m_hLinkedPortal.Get() != NULL) )
			{
				if( UTIL_IsBoxIntersectingPortal( ptRayEndPoint, ray.m_Extents, pTempPortal, 0.00f ) )
				{
					pBoxIntersectsPortals[iBoxIntersectsPortalsCount] = pTempPortal;
					++iBoxIntersectsPortalsCount;
				}
			}
		}

		if( iBoxIntersectsPortalsCount > 0 )
		{
			pIntersectedPortal = pBoxIntersectsPortals[0];
			
			if( iBoxIntersectsPortalsCount > 1 )
			{
				//hit more than one, use the closest
				float fDistToBeat = (ptRayEndPoint - pIntersectedPortal->GetEngineObject()->GetAbsOrigin()).LengthSqr();

				for( int i = 1; i != iBoxIntersectsPortalsCount; ++i )
				{
					float fDist = (ptRayEndPoint - pBoxIntersectsPortals[i]->GetEngineObject()->GetAbsOrigin()).LengthSqr();
					if( fDist < fDistToBeat )
					{
						pIntersectedPortal = pBoxIntersectsPortals[i];
						fDistToBeat = fDist;
					}
				}
			}
		}
	}

	if( pIntersectedPortal == NULL )
	{
		if( pOutLocal )
			*pOutLocal = NULL;

		if( pOutRemote )
			*pOutRemote = NULL;

		return false;
	}
	else
	{
		// Record the touched portals and return
		if( pOutLocal )
			*pOutLocal = pIntersectedPortal;

		if( pOutRemote )
			*pOutRemote = pIntersectedPortal->m_hLinkedPortal.Get();

		return true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Redirects the trace to either a trace that uses portal environments, or if a 
//			global boolean is set, trace with a special bullets trace.
//			NOTE: UTIL_Portal_TraceRay_With will use the default world trace if it gets a NULL portal pointer
// Input  : &ray - the ray to use to trace
//			fMask - collision mask
//			*pTraceFilter - customizable filter on the trace
//			*pTrace - trace struct to fill with output info
//-----------------------------------------------------------------------------
CProp_Portal* UTIL_Portal_TraceRay( const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
	float fMustBeCloserThan = 2.0f;
	CProp_Portal *pIntersectedPortal = UTIL_Portal_FirstAlongRay( ray, fMustBeCloserThan );

	if ( g_bBulletPortalTrace )
	{
		if ( UTIL_Portal_TraceRay_Bullets( pIntersectedPortal, ray, fMask, pTraceFilter, pTrace, bTraceHolyWall ) )
			return pIntersectedPortal;

		// Bullet didn't actually go through portal
		return NULL;

	}
	else
	{
		UTIL_Portal_TraceRay_With( pIntersectedPortal, ray, fMask, pTraceFilter, pTrace, bTraceHolyWall );
		return pIntersectedPortal;
	}
}

CProp_Portal* UTIL_Portal_TraceRay( const Ray_t &ray, unsigned int fMask, const IHandleEntity *ignore, int collisionGroup, trace_t *pTrace, bool bTraceHolyWall )
{
	CTraceFilterSimple traceFilter( ignore, collisionGroup );
	return UTIL_Portal_TraceRay( ray, fMask, &traceFilter, pTrace, bTraceHolyWall );
}


//-----------------------------------------------------------------------------
// Purpose: This version of traceray only traces against the portal environment of the specified portal.
// Input  : *pPortal - the portal whose physics we will trace against
//			&ray - the ray to trace with
//			fMask - collision mask
//			*pTraceFilter - customizable filter to determine what it hits
//			*pTrace - the trace struct to fill in with results
//			bTraceHolyWall - if this trace is to test against the 'holy wall' geometry
//-----------------------------------------------------------------------------
void UTIL_Portal_TraceRay( const CProp_Portal *pPortal, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
	pPortal->m_hPortalSimulator->TraceRay(ray, fMask, pTraceFilter, pTrace, bTraceHolyWall);
}

void UTIL_Portal_TraceRay( const CProp_Portal *pPortal, const Ray_t &ray, unsigned int fMask, const IHandleEntity *ignore, int collisionGroup, trace_t *pTrace, bool bTraceHolyWall )
{
	CTraceFilterSimple traceFilter( ignore, collisionGroup );
	UTIL_Portal_TraceRay( pPortal, ray, fMask, &traceFilter, pTrace, bTraceHolyWall );
}

//-----------------------------------------------------------------------------
// Purpose: Trace a ray 'past' a portal's surface, hitting objects in the linked portal's collision environment
// Input  : *pPortal - The portal being traced 'through'
//			&ray - The ray being traced
//			fMask - trace mask to cull results
//			*pTraceFilter - trace filter to cull results
//			*pTrace - Empty trace to return the result (value will be overwritten)
//-----------------------------------------------------------------------------
void UTIL_PortalLinked_TraceRay( const CProp_Portal *pPortal, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
#ifdef CLIENT_DLL
	Assert( (GameRules() == NULL) || GameRules()->IsMultiplayer() );
#endif
	// Transform the specified ray to the remote portal's space
	Ray_t rayTransformed;
	UTIL_Portal_RayTransform( pPortal->MatrixThisToLinked(), ray, rayTransformed );

	AssertMsg ( ray.m_IsRay, "Ray with extents across portal tracing not implemented!" );

	const CPortalSimulator* portalSimulator = pPortal->m_hPortalSimulator;
	CProp_Portal *pLinkedPortal = (CProp_Portal*)(pPortal->m_hLinkedPortal.Get());
	if( (pLinkedPortal == NULL) || (portalSimulator->RayIsInPortalHole( ray ) == false) )
	{
		memset( pTrace, 0, sizeof(trace_t));
		pTrace->fraction = 1.0f;
		pTrace->fractionleftsolid = 0;

		pTrace->contents = pPortal->m_hPortalSimulator->GetSurfaceProperties().contents;
		pTrace->surface  = pPortal->m_hPortalSimulator->GetSurfaceProperties().surface;
		pTrace->m_pEnt	 = pPortal->m_hPortalSimulator->GetSurfaceProperties().pEntity;
		return;
	}
	UTIL_Portal_TraceRay( pLinkedPortal, rayTransformed, fMask, pTraceFilter, pTrace, bTraceHolyWall );

	// Transform the ray's start, end and plane back into this portal's space, 
	// because we react to the collision as it is displayed, and the image is displayed with this local portal's orientation.
	VMatrix matLinkedToThis = pLinkedPortal->MatrixThisToLinked();
	UTIL_Portal_PointTransform( matLinkedToThis, pTrace->startpos, pTrace->startpos );
	UTIL_Portal_PointTransform( matLinkedToThis, pTrace->endpos, pTrace->endpos );
	UTIL_Portal_PlaneTransform( matLinkedToThis, pTrace->plane, pTrace->plane );
}

void UTIL_PortalLinked_TraceRay( const CProp_Portal *pPortal, const Ray_t &ray, unsigned int fMask, const IHandleEntity *ignore, int collisionGroup, trace_t *pTrace, bool bTraceHolyWall )
{
	CTraceFilterSimple traceFilter( ignore, collisionGroup );
	UTIL_PortalLinked_TraceRay( pPortal, ray, fMask, &traceFilter, pTrace, bTraceHolyWall );
}

//-----------------------------------------------------------------------------
// Purpose: A version of trace entity which detects portals and translates the trace through portals
//-----------------------------------------------------------------------------
void UTIL_Portal_TraceEntity( CBaseEntity *pEntity, const Vector &vecAbsStart, const Vector &vecAbsEnd, 
							 unsigned int mask, ITraceFilter *pFilter, trace_t *pTrace )
{
#ifdef CLIENT_DLL
	Assert( (GameRules() == NULL) || GameRules()->IsMultiplayer() );
	Assert( pEntity->IsPlayer() );

	CPortalSimulator *pPortalSimulator = NULL;
	if( pEntity->IsPlayer() )
	{
		C_Prop_Portal *pPortal = ((C_Portal_Player *)pEntity)->m_hPortalEnvironment.Get();
		if( pPortal )
			pPortalSimulator = pPortal->m_hPortalSimulator;
	}
#else
	CPortalSimulator *pPortalSimulator = CPortalSimulator::GetSimulatorThatOwnsEntity( pEntity );
#endif

	memset( pTrace, 0, sizeof(trace_t));
	pTrace->fraction = 1.0f;
	pTrace->fractionleftsolid = 0;

	ICollideable* pCollision = enginetrace->GetCollideable( pEntity );

	// If main is simulating this object, trace as UTIL_TraceEntity would
	trace_t realTrace;
	QAngle qCollisionAngles = pCollision->GetCollisionAngles();
	enginetrace->SweepCollideable( pCollision, vecAbsStart, vecAbsEnd, qCollisionAngles, mask, pFilter, &realTrace );

	// For the below box test, we need to add the tolerance onto the extents, because the underlying
	// box on plane side test doesn't use the parameter tolerance.
	float flTolerance = 0.1f;
	Vector vEntExtents = pEntity->GetEngineObject()->WorldAlignSize() * 0.5 + Vector ( flTolerance, flTolerance, flTolerance );
	Vector vColCenter = realTrace.endpos + ( pEntity->GetEngineObject()->WorldAlignMaxs() + pEntity->GetEngineObject()->WorldAlignMins() ) * 0.5f;

	// If this entity is not simulated in a portal environment, trace as normal
	if( pPortalSimulator == NULL )
	{
		// If main is simulating this object, trace as UTIL_TraceEntity would
		*pTrace = realTrace;
	}
	else
	{
		pPortalSimulator->TraceEntity(pEntity, vecAbsStart, vecAbsEnd, mask, pFilter, pTrace);
	}
}

void UTIL_Portal_PointTransform( const VMatrix matThisToLinked, const Vector &ptSource, Vector &ptTransformed )
{
	ptTransformed = matThisToLinked * ptSource;
}

void UTIL_Portal_VectorTransform( const VMatrix matThisToLinked, const Vector &vSource, Vector &vTransformed )
{
	vTransformed = matThisToLinked.ApplyRotation( vSource );
}

void UTIL_Portal_AngleTransform( const VMatrix matThisToLinked, const QAngle &qSource, QAngle &qTransformed )
{
	qTransformed = TransformAnglesToWorldSpace( qSource, matThisToLinked.As3x4() );
}

void UTIL_Portal_RayTransform( const VMatrix matThisToLinked, const Ray_t &raySource, Ray_t &rayTransformed )
{
	rayTransformed = raySource;

	UTIL_Portal_PointTransform( matThisToLinked, raySource.m_Start, rayTransformed.m_Start );
	UTIL_Portal_VectorTransform( matThisToLinked, raySource.m_StartOffset, rayTransformed.m_StartOffset );
	UTIL_Portal_VectorTransform( matThisToLinked, raySource.m_Delta, rayTransformed.m_Delta );

	//BUGBUG: Extents are axis aligned, so rotating it won't necessarily give us what we're expecting
	UTIL_Portal_VectorTransform( matThisToLinked, raySource.m_Extents, rayTransformed.m_Extents );
	
	//HACKHACK: Negative extents hang in traces, make each positive because we rotated it above
	if ( rayTransformed.m_Extents.x < 0.0f )
	{
		rayTransformed.m_Extents.x = -rayTransformed.m_Extents.x;
	}
	if ( rayTransformed.m_Extents.y < 0.0f )
	{
		rayTransformed.m_Extents.y = -rayTransformed.m_Extents.y;
	}
	if ( rayTransformed.m_Extents.z < 0.0f )
	{
		rayTransformed.m_Extents.z = -rayTransformed.m_Extents.z;
	}

}

void UTIL_Portal_PlaneTransform( const VMatrix matThisToLinked, const cplane_t &planeSource, cplane_t &planeTransformed )
{
	planeTransformed = planeSource;

	Vector vTrans;
	UTIL_Portal_VectorTransform( matThisToLinked, planeSource.normal, planeTransformed.normal );
	planeTransformed.dist = planeSource.dist * DotProduct( planeTransformed.normal, planeTransformed.normal );
	planeTransformed.dist += DotProduct( planeTransformed.normal, matThisToLinked.GetTranslation( vTrans ) );
}

void UTIL_Portal_PlaneTransform( const VMatrix matThisToLinked, const VPlane &planeSource, VPlane &planeTransformed )
{
	Vector vTranformedNormal;
	float fTransformedDist;

	Vector vTrans;
	UTIL_Portal_VectorTransform( matThisToLinked, planeSource.m_Normal, vTranformedNormal );
	fTransformedDist = planeSource.m_Dist * DotProduct( vTranformedNormal, vTranformedNormal );
	fTransformedDist += DotProduct( vTranformedNormal, matThisToLinked.GetTranslation( vTrans ) );

	planeTransformed.Init( vTranformedNormal, fTransformedDist );
}

void UTIL_Portal_Triangles( const Vector &ptPortalCenter, const QAngle &qPortalAngles, Vector pvTri1[ 3 ], Vector pvTri2[ 3 ] )
{
	// Get points to make triangles
	Vector vRight, vUp;
	AngleVectors( qPortalAngles, NULL, &vRight, &vUp );

	Vector vTopEdge = vUp * PORTAL_HALF_HEIGHT;
	Vector vBottomEdge = -vTopEdge;
	Vector vRightEdge = vRight * PORTAL_HALF_WIDTH;
	Vector vLeftEdge = -vRightEdge;

	Vector vTopLeft = ptPortalCenter + vTopEdge + vLeftEdge;
	Vector vTopRight = ptPortalCenter + vTopEdge + vRightEdge;
	Vector vBottomLeft = ptPortalCenter + vBottomEdge + vLeftEdge;
	Vector vBottomRight = ptPortalCenter + vBottomEdge + vRightEdge;

	// Make triangles
	pvTri1[ 0 ] = vTopRight;
	pvTri1[ 1 ] = vTopLeft;
	pvTri1[ 2 ] = vBottomLeft;

	pvTri2[ 0 ] = vTopRight;
	pvTri2[ 1 ] = vBottomLeft;
	pvTri2[ 2 ] = vBottomRight;
}

void UTIL_Portal_Triangles( const CProp_Portal *pPortal, Vector pvTri1[ 3 ], Vector pvTri2[ 3 ] )
{
	UTIL_Portal_Triangles( pPortal->GetEngineObject()->GetAbsOrigin(), pPortal->GetEngineObject()->GetAbsAngles(), pvTri1, pvTri2 );
}

float UTIL_Portal_DistanceThroughPortal( const CProp_Portal *pPortal, const Vector &vPoint1, const Vector &vPoint2 )
{
	return FastSqrt( UTIL_Portal_DistanceThroughPortalSqr( pPortal, vPoint1, vPoint2 ) );
}

float UTIL_Portal_DistanceThroughPortalSqr( const CProp_Portal *pPortal, const Vector &vPoint1, const Vector &vPoint2 )
{
	if ( !pPortal || !pPortal->m_bActivated )
		return -1.0f;

	CProp_Portal *pPortalLinked = pPortal->m_hLinkedPortal;
	if ( !pPortalLinked || !pPortalLinked->m_bActivated )
		return -1.0f;

	return vPoint1.DistToSqr( pPortal->GetEngineObject()->GetAbsOrigin() ) + pPortalLinked->GetEngineObject()->GetAbsOrigin().DistToSqr( vPoint2 );
}

float UTIL_Portal_ShortestDistance( const Vector &vPoint1, const Vector &vPoint2, CProp_Portal **pShortestDistPortal_Out /*= NULL*/, bool bRequireStraightLine /*= false*/ )
{
	return FastSqrt( UTIL_Portal_ShortestDistanceSqr( vPoint1, vPoint2, pShortestDistPortal_Out, bRequireStraightLine ) );
}

float UTIL_Portal_ShortestDistanceSqr( const Vector &vPoint1, const Vector &vPoint2, CProp_Portal **pShortestDistPortal_Out /*= NULL*/, bool bRequireStraightLine /*= false*/ )
{
	float fMinDist = vPoint1.DistToSqr( vPoint2 );	
	
	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if( iPortalCount == 0 )
	{
		if( pShortestDistPortal_Out )
			*pShortestDistPortal_Out = NULL;

		return fMinDist;
	}
	CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();
	CProp_Portal *pShortestDistPortal = NULL;

	for( int i = 0; i != iPortalCount; ++i )
	{
		CProp_Portal *pTempPortal = pPortals[i];
		if( pTempPortal->m_bActivated )
		{
			CProp_Portal *pLinkedPortal = pTempPortal->m_hLinkedPortal.Get();
			if( pLinkedPortal != NULL )
			{
				Vector vPoint1Transformed = pTempPortal->MatrixThisToLinked() * vPoint1;

				float fDirectDist = vPoint1Transformed.DistToSqr( vPoint2 );
				if( fDirectDist < fMinDist )
				{
					//worth investigating further
					//find out if it's a straight line through the portal, or if we have to wrap around a corner
					float fPoint1TransformedDist = pLinkedPortal->m_plane_Origin.normal.Dot( vPoint1Transformed ) - pLinkedPortal->m_plane_Origin.dist;
					float fPoint2Dist = pLinkedPortal->m_plane_Origin.normal.Dot( vPoint2 ) - pLinkedPortal->m_plane_Origin.dist;

					bool bStraightLine = true;
					if( (fPoint1TransformedDist > 0.0f) || (fPoint2Dist < 0.0f) ) //straight line through portal impossible, part of the line has to backtrack to get to the portal surface
						bStraightLine = false;

					if( bStraightLine ) //if we're not already doing some crazy wrapping, find an intersection point
					{
						float fTotalDist = fPoint2Dist - fPoint1TransformedDist; //fPoint1TransformedDist is known to be negative
						Vector ptPlaneIntersection;

						if( fTotalDist != 0.0f )
						{
							float fInvTotalDist = 1.0f / fTotalDist;
							ptPlaneIntersection = (vPoint1Transformed * (fPoint2Dist * fInvTotalDist)) + (vPoint2 * ((-fPoint1TransformedDist) * fInvTotalDist));
						}
						else
						{
							ptPlaneIntersection = vPoint1Transformed;
						}

						Vector vRight, vUp;
						pLinkedPortal->GetEngineObject()->GetVectors( NULL, &vRight, &vUp );
						
						Vector ptLinkedCenter = pLinkedPortal->GetEngineObject()->GetAbsOrigin();
						Vector vCenterToIntersection = ptPlaneIntersection - ptLinkedCenter;
						float fRight = vRight.Dot( vCenterToIntersection );
						float fUp = vUp.Dot( vCenterToIntersection );

						float fAbsRight = fabs( fRight );
						float fAbsUp = fabs( fUp );
						if( (fAbsRight > PORTAL_HALF_WIDTH) ||
							(fAbsUp > PORTAL_HALF_HEIGHT) )
							bStraightLine = false;

						if( bStraightLine == false )
						{
							if( bRequireStraightLine )
								continue;

							//find the offending extent and shorten both extents to bring it into the portal quad
							float fNormalizer;
							if( fAbsRight > PORTAL_HALF_WIDTH )
							{
								fNormalizer = fAbsRight/PORTAL_HALF_WIDTH;

								if( fAbsUp > PORTAL_HALF_HEIGHT )
								{
									float fUpNormalizer = fAbsUp/PORTAL_HALF_HEIGHT;
									if( fUpNormalizer > fNormalizer )
										fNormalizer = fUpNormalizer;
								}
							}
							else
							{
								fNormalizer = fAbsUp/PORTAL_HALF_HEIGHT;
							}

							vCenterToIntersection *= (1.0f/fNormalizer);
							ptPlaneIntersection = ptLinkedCenter + vCenterToIntersection;

							float fWrapDist = vPoint1Transformed.DistToSqr( ptPlaneIntersection ) + vPoint2.DistToSqr( ptPlaneIntersection );
							if( fWrapDist < fMinDist )
							{
								fMinDist = fWrapDist;
								pShortestDistPortal = pTempPortal;
							}
						}
						else
						{
							//it's a straight shot from point 1 to 2 through the portal
							fMinDist = fDirectDist;
							pShortestDistPortal = pTempPortal;
						}
					}
					else
					{
						if( bRequireStraightLine )
							continue;

						//do some crazy wrapped line intersection algorithm

						//for now, just do the cheap and easy solution
						float fWrapDist = vPoint1.DistToSqr( pTempPortal->GetEngineObject()->GetAbsOrigin() ) + pLinkedPortal->GetEngineObject()->GetAbsOrigin().DistToSqr( vPoint2 );
						if( fWrapDist < fMinDist )
						{
							fMinDist = fWrapDist;
							pShortestDistPortal = pTempPortal;
						}
					}
				}
			}
		}
	}

	return fMinDist;
}

void UTIL_Portal_AABB( const CProp_Portal *pPortal, Vector &vMin, Vector &vMax )
{
	Vector vOrigin = pPortal->GetEngineObject()->GetAbsOrigin();
	QAngle qAngles = pPortal->GetEngineObject()->GetAbsAngles();

	Vector vOBBForward;
	Vector vOBBRight;
	Vector vOBBUp;

	AngleVectors( qAngles, &vOBBForward, &vOBBRight, &vOBBUp );

	//scale the extents to usable sizes
	vOBBForward *= PORTAL_HALF_DEPTH;
	vOBBRight *= PORTAL_HALF_WIDTH;
	vOBBUp *= PORTAL_HALF_HEIGHT;

	vOrigin -= vOBBForward + vOBBRight + vOBBUp;

	vOBBForward *= 2.0f;
	vOBBRight *= 2.0f;
	vOBBUp *= 2.0f;

	vMin = vMax = vOrigin;

	for( int i = 1; i != 8; ++i )
	{
		Vector ptTest = vOrigin;
		if( i & (1 << 0) ) ptTest += vOBBForward;
		if( i & (1 << 1) ) ptTest += vOBBRight;
		if( i & (1 << 2) ) ptTest += vOBBUp;

		if( ptTest.x < vMin.x ) vMin.x = ptTest.x;
		if( ptTest.y < vMin.y ) vMin.y = ptTest.y;
		if( ptTest.z < vMin.z ) vMin.z = ptTest.z;
		if( ptTest.x > vMax.x ) vMax.x = ptTest.x;
		if( ptTest.y > vMax.y ) vMax.y = ptTest.y;
		if( ptTest.z > vMax.z ) vMax.z = ptTest.z;
	}
}

float UTIL_IntersectRayWithPortal( const Ray_t &ray, const CProp_Portal *pPortal )
{
	if ( !pPortal || !pPortal->m_bActivated )
	{
		return -1.0f;
	}

	Vector vForward;
	pPortal->GetEngineObject()->GetVectors( &vForward, NULL, NULL );

	// Discount rays not coming from the front of the portal
	float fDot = DotProduct( vForward, ray.m_Delta );
	if ( fDot > 0.0f  )
	{
		return -1.0f;
	}

	Vector pvTri1[ 3 ], pvTri2[ 3 ];

	UTIL_Portal_Triangles( pPortal, pvTri1, pvTri2 );

	float fT;

	// Test triangle 1
	fT = IntersectRayWithTriangle( ray, pvTri1[ 0 ], pvTri1[ 1 ], pvTri1[ 2 ], false );

	// If there was an intersection return the T
	if ( fT >= 0.0f )
		return fT;

	// Return the result of collision with the other face triangle
	return IntersectRayWithTriangle( ray, pvTri2[ 0 ], pvTri2[ 1 ], pvTri2[ 2 ], false );
}

bool UTIL_IntersectRayWithPortalOBB( const CProp_Portal *pPortal, const Ray_t &ray, trace_t *pTrace )
{
	return IntersectRayWithOBB( ray, pPortal->GetEngineObject()->GetAbsOrigin(), pPortal->GetEngineObject()->GetAbsAngles(), CProp_Portal_Shared::vLocalMins, CProp_Portal_Shared::vLocalMaxs, 0.0f, pTrace );
}

bool UTIL_IntersectRayWithPortalOBBAsAABB( const CProp_Portal *pPortal, const Ray_t &ray, trace_t *pTrace )
{
	Vector vAABBMins, vAABBMaxs;

	UTIL_Portal_AABB( pPortal, vAABBMins, vAABBMaxs );

	return IntersectRayWithBox( ray, vAABBMins, vAABBMaxs, 0.0f, pTrace );
}

bool UTIL_IsBoxIntersectingPortal( const Vector &vecBoxCenter, const Vector &vecBoxExtents, const Vector &ptPortalCenter, const QAngle &qPortalAngles, float flTolerance )
{
	Vector pvTri1[ 3 ], pvTri2[ 3 ];

	UTIL_Portal_Triangles( ptPortalCenter, qPortalAngles, pvTri1, pvTri2 );

	cplane_t plane;

	ComputeTrianglePlane( pvTri1[ 0 ], pvTri1[ 1 ], pvTri1[ 2 ], plane.normal, plane.dist );
	plane.type = PLANE_ANYZ;
	plane.signbits = SignbitsForPlane( &plane );

	if ( IsBoxIntersectingTriangle( vecBoxCenter, vecBoxExtents, pvTri1[ 0 ], pvTri1[ 1 ], pvTri1[ 2 ], plane, flTolerance ) )
	{
		return true;
	}

	ComputeTrianglePlane( pvTri2[ 0 ], pvTri2[ 1 ], pvTri2[ 2 ], plane.normal, plane.dist );
	plane.type = PLANE_ANYZ;
	plane.signbits = SignbitsForPlane( &plane );

	return IsBoxIntersectingTriangle( vecBoxCenter, vecBoxExtents, pvTri2[ 0 ], pvTri2[ 1 ], pvTri2[ 2 ], plane, flTolerance );
}

bool UTIL_IsBoxIntersectingPortal( const Vector &vecBoxCenter, const Vector &vecBoxExtents, const CProp_Portal *pPortal, float flTolerance )
{
	if( pPortal == NULL )
		return false;

	return UTIL_IsBoxIntersectingPortal( vecBoxCenter, vecBoxExtents, pPortal->GetEngineObject()->GetAbsOrigin(), pPortal->GetEngineObject()->GetAbsAngles(), flTolerance );
}

CProp_Portal *UTIL_IntersectEntityExtentsWithPortal( const CBaseEntity *pEntity )
{
	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if( iPortalCount == 0 )
		return NULL;

	Vector vMin, vMax;
	pEntity->GetEngineObject()->WorldSpaceAABB( &vMin, &vMax );
	Vector ptCenter = ( vMin + vMax ) * 0.5f;
	Vector vExtents = ( vMax - vMin ) * 0.5f;

	CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();
	for( int i = 0; i != iPortalCount; ++i )
	{
		CProp_Portal *pTempPortal = pPortals[i];
		if( pTempPortal->m_bActivated && 
			(pTempPortal->m_hLinkedPortal.Get() != NULL) &&
			UTIL_IsBoxIntersectingPortal( ptCenter, vExtents, pTempPortal )	)
		{
			return pPortals[i];
		}
	}

	return NULL;
}

void UTIL_Portal_NDebugOverlay( const Vector &ptPortalCenter, const QAngle &qPortalAngles, int r, int g, int b, int a, bool noDepthTest, float duration )
{
#ifndef CLIENT_DLL
	Vector pvTri1[ 3 ], pvTri2[ 3 ];

	UTIL_Portal_Triangles( ptPortalCenter, qPortalAngles, pvTri1, pvTri2 );

	NDebugOverlay::Triangle( pvTri1[ 0 ], pvTri1[ 1 ], pvTri1[ 2 ], r, g, b, a, noDepthTest, duration );
	NDebugOverlay::Triangle( pvTri2[ 0 ], pvTri2[ 1 ], pvTri2[ 2 ], r, g, b, a, noDepthTest, duration );
#endif //#ifndef CLIENT_DLL
}

void UTIL_Portal_NDebugOverlay( const CProp_Portal *pPortal, int r, int g, int b, int a, bool noDepthTest, float duration )
{
#ifndef CLIENT_DLL
	UTIL_Portal_NDebugOverlay( pPortal->GetEngineObject()->GetAbsOrigin(), pPortal->GetEngineObject()->GetAbsAngles(), r, g, b, a, noDepthTest, duration );
#endif //#ifndef CLIENT_DLL
}


bool FindClosestPassableSpace( CBaseEntity *pEntity, const Vector &vIndecisivePush, unsigned int fMask ) //assumes the object is already in a mostly passable space
{
	if ( sv_use_find_closest_passable_space.GetBool() == false )
		return true;

	// Don't ever do this to entities with a move parent
	if ( pEntity->GetEngineObject()->GetMoveParent() )
		return true;

#ifndef CLIENT_DLL
	ADD_DEBUG_HISTORY( HISTORY_PLAYER_DAMAGE, UTIL_VarArgs( "RUNNING FIND CLOSEST PASSABLE SPACE on %s..\n", pEntity->GetDebugName() ) );
#endif

	Vector ptExtents[8]; //ordering is going to be like 3 bits, where 0 is a min on the related axis, and 1 is a max on the same axis, axis order x y z

	float fExtentsValidation[8]; //some points are more valid than others, and this is our measure


	Vector vEntityMaxs;// = pEntity->WorldAlignMaxs();
	Vector vEntityMins;// = pEntity->WorldAlignMins();
	pEntity->GetEngineObject()->WorldSpaceAABB( &vEntityMins, &vEntityMaxs );

	Vector ptEntityCenter = ((vEntityMins + vEntityMaxs) / 2.0f);
	vEntityMins -= ptEntityCenter;
	vEntityMaxs -= ptEntityCenter;

	Vector ptEntityOriginalCenter = ptEntityCenter;
	
	ptEntityCenter.z += 0.001f; //to satisfy m_IsSwept on first pass

	int iEntityCollisionGroup = pEntity->GetEngineObject()->GetCollisionGroup();

	trace_t traces[2];
	Ray_t entRay;
	//entRay.Init( ptEntityCenter, ptEntityCenter, vEntityMins, vEntityMaxs );
	entRay.m_Extents = vEntityMaxs;
	entRay.m_IsRay = false;
	entRay.m_IsSwept = true;
	entRay.m_StartOffset = vec3_origin;

	Vector vOriginalExtents = vEntityMaxs;	

	Vector vGrowSize = vEntityMaxs / 101.0f;
	vEntityMaxs -= vGrowSize;
	vEntityMins += vGrowSize;
	
	
	Ray_t testRay;
	testRay.m_Extents = vGrowSize;
	testRay.m_IsRay = false;
	testRay.m_IsSwept = true;
	testRay.m_StartOffset = vec3_origin;



	unsigned int iFailCount;
	for( iFailCount = 0; iFailCount != 100; ++iFailCount )
	{
		entRay.m_Start = ptEntityCenter;
		entRay.m_Delta = ptEntityOriginalCenter - ptEntityCenter;

		UTIL_TraceRay( entRay, fMask, pEntity, iEntityCollisionGroup, &traces[0] );
		if( traces[0].startsolid == false )
		{
			Vector vNewPos = traces[0].endpos + (pEntity->GetEngineObject()->GetAbsOrigin() - ptEntityOriginalCenter);
#ifdef CLIENT_DLL
			pEntity->GetEngineObject()->SetAbsOrigin( vNewPos );
#else
			pEntity->Teleport( &vNewPos, NULL, NULL );
#endif
			return true; //current placement worked
		}

		bool bExtentInvalid[8];
		for( int i = 0; i != 8; ++i )
		{
			fExtentsValidation[i] = 0.0f;
			ptExtents[i] = ptEntityCenter;
			ptExtents[i].x += ((i & (1<<0)) ? vEntityMaxs.x : vEntityMins.x);
			ptExtents[i].y += ((i & (1<<1)) ? vEntityMaxs.y : vEntityMins.y);
			ptExtents[i].z += ((i & (1<<2)) ? vEntityMaxs.z : vEntityMins.z);

			bExtentInvalid[i] = enginetrace->PointOutsideWorld( ptExtents[i] );
		}

		unsigned int counter, counter2;
		for( counter = 0; counter != 7; ++counter )
		{
			for( counter2 = counter + 1; counter2 != 8; ++counter2 )
			{

				testRay.m_Delta = ptExtents[counter2] - ptExtents[counter];
				
				if( bExtentInvalid[counter] )
					traces[0].startsolid = true;
				else
				{
					testRay.m_Start = ptExtents[counter];
					UTIL_TraceRay( testRay, fMask, pEntity, iEntityCollisionGroup, &traces[0] );
				}

				if( bExtentInvalid[counter2] )
					traces[1].startsolid = true;
				else
				{
					testRay.m_Start = ptExtents[counter2];
					testRay.m_Delta = -testRay.m_Delta;
					UTIL_TraceRay( testRay, fMask, pEntity, iEntityCollisionGroup, &traces[1] );
				}

				float fDistance = testRay.m_Delta.Length();

				for( int i = 0; i != 2; ++i )
				{
					int iExtent = (i==0)?(counter):(counter2);

					if( traces[i].startsolid )
					{
						fExtentsValidation[iExtent] -= 100.0f;
					}
					else
					{
						fExtentsValidation[iExtent] += traces[i].fraction * fDistance;
					}
				}
			}
		}

		Vector vNewOriginDirection( 0.0f, 0.0f, 0.0f );
		float fTotalValidation = 0.0f;
		for( counter = 0; counter != 8; ++counter )
		{
			if( fExtentsValidation[counter] > 0.0f )
			{
				vNewOriginDirection += (ptExtents[counter] - ptEntityCenter) * fExtentsValidation[counter];
				fTotalValidation += fExtentsValidation[counter];
			}
		}

		if( fTotalValidation != 0.0f )
		{
			ptEntityCenter += (vNewOriginDirection / fTotalValidation);

			//increase sizing
			testRay.m_Extents += vGrowSize;
			vEntityMaxs -= vGrowSize;
			vEntityMins = -vEntityMaxs;
		}
		else
		{
			//no point was valid, apply the indecisive vector
			ptEntityCenter += vIndecisivePush;

			//reset sizing
			testRay.m_Extents = vGrowSize;
			vEntityMaxs = vOriginalExtents;
			vEntityMins = -vEntityMaxs;
		}		
	}

	// X360TBD: Hits in portal devtest
	AssertMsg( IsX360() || iFailCount != 100, "FindClosestPassableSpace() failure." );
	return false;
}

bool UTIL_Portal_EntityIsInPortalHole( const CProp_Portal *pPortal, CBaseEntity *pEntity )
{
	Vector vMins = pEntity->GetEngineObject()->OBBMins();
	Vector vMaxs = pEntity->GetEngineObject()->OBBMaxs();
	Vector vForward, vUp, vRight;
	AngleVectors(pEntity->GetEngineObject()->GetCollisionAngles(), &vForward, &vRight, &vUp );
	Vector ptOrigin = pEntity->GetEngineObject()->GetAbsOrigin();

	Vector ptOBBCenter = pEntity->GetEngineObject()->GetAbsOrigin() + (vMins + vMaxs * 0.5f);
	Vector vExtents = (vMaxs - vMins) * 0.5f;

	vForward *= vExtents.x;
	vRight *= vExtents.y;
	vUp *= vExtents.z;

	Vector vPortalForward, vPortalRight, vPortalUp;
	pPortal->GetEngineObject()->GetVectors( &vPortalForward, &vPortalRight, &vPortalUp );
	Vector ptPortalCenter = pPortal->GetEngineObject()->GetAbsOrigin();

	return OBBHasFullyContainedIntersectionWithQuad( vForward, vRight, vUp, ptOBBCenter, 
		vPortalForward, vPortalForward.Dot( ptPortalCenter ), ptPortalCenter, 
		vPortalRight, PORTAL_HALF_WIDTH + 1.0f, vPortalUp, PORTAL_HALF_HEIGHT + 1.0f );
}


#ifdef CLIENT_DLL
void UTIL_TransformInterpolatedAngle(ITypedInterpolatedVar< QAngle > &qInterped, matrix3x4_t matTransform, bool bSkipNewest )
{
	int iHead = qInterped.GetHead();
	if( !qInterped.IsValidIndex( iHead ) )
		return;

#ifdef DBGFLAG_ASSERT
	float fHeadTime;
	qInterped.GetHistoryValue( iHead, fHeadTime );
#endif

	float fTime;
	QAngle *pCurrent;
	int iCurrent;

	if( bSkipNewest )
		iCurrent = qInterped.GetNext( iHead );
	else
		iCurrent = iHead;

	while( (pCurrent = qInterped.GetHistoryValue( iCurrent, fTime )) != NULL )
	{
		Assert( (fTime <= fHeadTime) || (iCurrent == iHead) ); //asserting that head is always newest

		if( fTime < gpGlobals->curtime )
			*pCurrent = TransformAnglesToWorldSpace( *pCurrent, matTransform );

		iCurrent = qInterped.GetNext( iCurrent );
		if( iCurrent == iHead )
			break;
	}

	qInterped.Interpolate( gpGlobals->curtime );
}

void UTIL_TransformInterpolatedPosition(ITypedInterpolatedVar< Vector > &vInterped, VMatrix matTransform, bool bSkipNewest )
{
	int iHead = vInterped.GetHead();
	if( !vInterped.IsValidIndex( iHead ) )
		return;

#ifdef DBGFLAG_ASSERT
	float fHeadTime;
	vInterped.GetHistoryValue( iHead, fHeadTime );
#endif

	float fTime;
	Vector *pCurrent;
	int iCurrent;

	if( bSkipNewest )
		iCurrent = vInterped.GetNext( iHead );
	else
		iCurrent = iHead;

	while( (pCurrent = vInterped.GetHistoryValue( iCurrent, fTime )) != NULL )
	{
		Assert( (fTime <= fHeadTime) || (iCurrent == iHead) );

		if( fTime < gpGlobals->curtime )
			*pCurrent = matTransform * (*pCurrent);

		iCurrent = vInterped.GetNext( iCurrent );
		if( iCurrent == iHead )
			break;
	}

	vInterped.Interpolate( gpGlobals->curtime );
}
#endif


#ifndef CLIENT_DLL

void CC_Debug_FixMyPosition( void )
{
	CBaseEntity *pPlayer = UTIL_GetCommandClient();

	FindClosestPassableSpace( pPlayer, vec3_origin );
}

static ConCommand debug_fixmyposition("debug_fixmyposition", CC_Debug_FixMyPosition, "Runs FindsClosestPassableSpace() on player.", FCVAR_CHEAT );
#endif
