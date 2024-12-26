//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

//#include "cbase.h"
//#include "portal_shareddefs.h"
//#include "portal_collideable_enumerator.h"
#include "Color.h"
#include "cmodel.h"
#include "mathlib/vmatrix.h"
#include "engine/ICollideable.h"
#include "ihandleentity.h"
#include "collisionutils.h"
#include "entitylist_base.h"
#include "baseentity_shared.h"
#include "gamerules.h"
#include "util_shared.h"
#include "portal_util_shared.h"

bool g_bAllowForcePortalTrace = false;
bool g_bForcePortalTrace = false;
bool g_bBulletPortalTrace = false;

const Vector vPortalLocalMins(0.0f, -PORTAL_HALF_WIDTH, -PORTAL_HALF_HEIGHT);
const Vector vPortalLocalMaxs(64.0f, PORTAL_HALF_WIDTH, PORTAL_HALF_HEIGHT);

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

IEnginePortal* UTIL_Portal_FirstAlongRay(IEntityList* pEntityList, const Ray_t &ray, float &fMustBeCloserThan )
{
	IEnginePortal *pIntersectedPortal = NULL;

	int iPortalCount = pEntityList->GetPortalCount();
	if( iPortalCount != 0 )
	{
		for( int i = 0; i != iPortalCount; ++i )
		{
			IEnginePortal* pTempPortal = pEntityList->GetPortal(i);
			if( pTempPortal->IsActivedAndLinked() )
			{
				float fIntersection = UTIL_IntersectRayWithPortal( ray, pTempPortal );
				if( fIntersection >= 0.0f && fIntersection < fMustBeCloserThan )
				{
					//within range, now check directionality
					if( pTempPortal->GetPortalPlane().normal.Dot(ray.m_Delta) < 0.0f)
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


bool UTIL_Portal_TraceRay_Bullets( const IEnginePortal *pPortal, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
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
	pPortal->AsEngineObject()->GetVectors( &vPortalForward, 0, 0 );

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

void UTIL_Portal_TraceRay_With( const IEnginePortal *pPortal, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
	//check to see if the player is theoretically in a portal environment
	if( !pPortal || !pPortal->IsReadyToSimulate() )//m_hPortalSimulator->
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
		pPortal->AsEngineObject()->GetVectors( &vPortalForward, 0, 0 );
		
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
				if ( UTIL_DidTraceTouchPortals(pPortal->AsEngineObject()->GetEntityList(), ray, PortalLinkedTrace ) )
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
bool UTIL_DidTraceTouchPortals(IEntityList* pEntityList, const Ray_t& ray, const trace_t& trace,const IEnginePortal** pOutLocal,const IEnginePortal** pOutRemote )
{
	int iPortalCount = pEntityList->GetPortalCount();
	if( iPortalCount == 0 )
	{
		if( pOutLocal )
			*pOutLocal = NULL;

		if( pOutRemote )
			*pOutRemote = NULL;

		return false;
	}

	IEnginePortal *pIntersectedPortal = NULL;

	if( ray.m_IsSwept )
	{
		float fMustBeCloserThan = trace.fraction + 0.0001f;

		pIntersectedPortal = UTIL_Portal_FirstAlongRay(pEntityList, ray, fMustBeCloserThan );
	}
	
	if( (pIntersectedPortal == NULL) && !ray.m_IsRay )
	{
		//haven't hit anything yet, try again with box tests

		Vector ptRayEndPoint = trace.endpos - ray.m_StartOffset; // The trace added the start offset to the end position, so remove it for the box test
		IEnginePortal **pBoxIntersectsPortals = (IEnginePortal**)stackalloc( sizeof(IEnginePortal*) * iPortalCount );
		int iBoxIntersectsPortalsCount = 0;

		for( int i = 0; i != iPortalCount; ++i )
		{
			IEnginePortal *pTempPortal = pEntityList->GetPortal(i);
			if( (pTempPortal->IsActivated()) && 
				(pTempPortal->GetLinkedPortal() != NULL) )
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
				float fDistToBeat = (ptRayEndPoint - pIntersectedPortal->AsEngineObject()->GetAbsOrigin()).LengthSqr();

				for( int i = 1; i != iBoxIntersectsPortalsCount; ++i )
				{
					float fDist = (ptRayEndPoint - pBoxIntersectsPortals[i]->AsEngineObject()->GetAbsOrigin()).LengthSqr();
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
			*pOutRemote = pIntersectedPortal->GetLinkedPortal();

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
IEnginePortal* UTIL_Portal_TraceRay(IEntityList* pEntityList, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
	float fMustBeCloserThan = 2.0f;
	IEnginePortal *pIntersectedPortal = UTIL_Portal_FirstAlongRay(pEntityList, ray, fMustBeCloserThan );

	if ( g_bBulletPortalTrace )
	{
		if (UTIL_Portal_TraceRay_Bullets(pIntersectedPortal, ray, fMask, pTraceFilter, pTrace, bTraceHolyWall))
			return pIntersectedPortal;

		// Bullet didn't actually go through portal
		return NULL;

	}
	else
	{
		UTIL_Portal_TraceRay_With(pIntersectedPortal, ray, fMask, pTraceFilter, pTrace, bTraceHolyWall);
		return pIntersectedPortal;
	}
}

IEnginePortal* UTIL_Portal_TraceRay(IEntityList* pEntityList, const Ray_t &ray, unsigned int fMask, const IHandleEntity *ignore, int collisionGroup, trace_t *pTrace, bool bTraceHolyWall )
{
	CTraceFilterSimple traceFilter( ignore, collisionGroup );
	return UTIL_Portal_TraceRay(pEntityList, ray, fMask, &traceFilter, pTrace, bTraceHolyWall );
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
void UTIL_Portal_TraceRay( const IEnginePortal *pPortal, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
	pPortal->TraceRay(ray, fMask, pTraceFilter, pTrace, bTraceHolyWall);//m_hPortalSimulator->
}

void UTIL_Portal_TraceRay( const IEnginePortal *pPortal, const Ray_t &ray, unsigned int fMask, const IHandleEntity *ignore, int collisionGroup, trace_t *pTrace, bool bTraceHolyWall )
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
void UTIL_PortalLinked_TraceRay( const IEnginePortal *pPortal, const Ray_t &ray, unsigned int fMask, ITraceFilter *pTraceFilter, trace_t *pTrace, bool bTraceHolyWall )
{
#ifdef CLIENT_DLL
	Assert( (GameRules() == NULL) || GameRules()->IsMultiplayer() );
#endif
	// Transform the specified ray to the remote portal's space
	Ray_t rayTransformed;
	UTIL_Portal_RayTransform( pPortal->MatrixThisToLinked(), ray, rayTransformed );

	AssertMsg ( ray.m_IsRay, "Ray with extents across portal tracing not implemented!" );

	const IEnginePortal *pLinkedPortal = pPortal->GetLinkedPortal();
	if( (pLinkedPortal == NULL) || (pPortal->RayIsInPortalHole( ray ) == false) )
	{
		memset( pTrace, 0, sizeof(trace_t));
		pTrace->fraction = 1.0f;
		pTrace->fractionleftsolid = 0;

		pTrace->contents = pPortal->GetSurfaceProperties().contents;//m_hPortalSimulator->
		pTrace->surface  = pPortal->GetSurfaceProperties().surface;//m_hPortalSimulator->
		pTrace->m_pEnt	 = pPortal->GetSurfaceProperties().pEntity;//m_hPortalSimulator->
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

void UTIL_PortalLinked_TraceRay( const IEnginePortal *pPortal, const Ray_t &ray, unsigned int fMask, const IHandleEntity *ignore, int collisionGroup, trace_t *pTrace, bool bTraceHolyWall )
{
	CTraceFilterSimple traceFilter( ignore, collisionGroup );
	UTIL_PortalLinked_TraceRay( pPortal, ray, fMask, &traceFilter, pTrace, bTraceHolyWall );
}

//-----------------------------------------------------------------------------
// Purpose: A version of trace entity which detects portals and translates the trace through portals
//-----------------------------------------------------------------------------
void UTIL_Portal_TraceEntity(IEnginePortal* pPortal, IHandleEntity *pEntity, const Vector &vecAbsStart, const Vector &vecAbsEnd,
							 unsigned int mask, ITraceFilter *pFilter, trace_t *pTrace )
{
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
	if(pPortal == NULL )
	{
		// If main is simulating this object, trace as UTIL_TraceEntity would
		*pTrace = realTrace;
	}
	else
	{
		pPortal->TraceEntity(pEntity, vecAbsStart, vecAbsEnd, mask, pFilter, pTrace);
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

void UTIL_Portal_Triangles( const IEnginePortal *pPortal, Vector pvTri1[ 3 ], Vector pvTri2[ 3 ] )
{
	UTIL_Portal_Triangles( pPortal->AsEngineObject()->GetAbsOrigin(), pPortal->AsEngineObject()->GetAbsAngles(), pvTri1, pvTri2 );
}

float UTIL_Portal_DistanceThroughPortal( const IEnginePortal *pPortal, const Vector &vPoint1, const Vector &vPoint2 )
{
	return FastSqrt( UTIL_Portal_DistanceThroughPortalSqr( pPortal, vPoint1, vPoint2 ) );
}

float UTIL_Portal_DistanceThroughPortalSqr( const IEnginePortal *pPortal, const Vector &vPoint1, const Vector &vPoint2 )
{
	if ( !pPortal || !pPortal->IsActivated() )
		return -1.0f;

	const IEnginePortal *pPortalLinked = pPortal->GetLinkedPortal();
	if ( !pPortalLinked || !pPortalLinked->IsActivated() )
		return -1.0f;

	return vPoint1.DistToSqr( pPortal->AsEngineObject()->GetAbsOrigin() ) + pPortalLinked->AsEngineObject()->GetAbsOrigin().DistToSqr( vPoint2 );
}

float UTIL_Portal_ShortestDistance(IEntityList* pEntityList, const Vector &vPoint1, const Vector &vPoint2, IEnginePortal **pShortestDistPortal_Out /*= NULL*/, bool bRequireStraightLine /*= false*/ )
{
	return FastSqrt( UTIL_Portal_ShortestDistanceSqr(pEntityList, vPoint1, vPoint2, pShortestDistPortal_Out, bRequireStraightLine ) );
}

float UTIL_Portal_ShortestDistanceSqr(IEntityList* pEntityList, const Vector &vPoint1, const Vector &vPoint2, IEnginePortal **pShortestDistPortal_Out /*= NULL*/, bool bRequireStraightLine /*= false*/ )
{
	float fMinDist = vPoint1.DistToSqr( vPoint2 );	
	
	int iPortalCount = pEntityList->GetPortalCount();
	if( iPortalCount == 0 )
	{
		if( pShortestDistPortal_Out )
			*pShortestDistPortal_Out = NULL;

		return fMinDist;
	}

	IEnginePortal *pShortestDistPortal = NULL;

	for( int i = 0; i != iPortalCount; ++i )
	{
		IEnginePortal *pTempPortal = pEntityList->GetPortal(i);
		if( pTempPortal->IsActivated() )
		{
			const IEnginePortal *pLinkedPortal = pTempPortal->GetLinkedPortal();
			if( pLinkedPortal != NULL )
			{
				Vector vPoint1Transformed = pTempPortal->MatrixThisToLinked() * vPoint1;

				float fDirectDist = vPoint1Transformed.DistToSqr( vPoint2 );
				if( fDirectDist < fMinDist )
				{
					//worth investigating further
					//find out if it's a straight line through the portal, or if we have to wrap around a corner
					float fPoint1TransformedDist = pLinkedPortal->GetPortalPlane().normal.Dot( vPoint1Transformed ) - pLinkedPortal->GetPortalPlane().dist;
					float fPoint2Dist = pLinkedPortal->GetPortalPlane().normal.Dot( vPoint2 ) - pLinkedPortal->GetPortalPlane().dist;

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
						pLinkedPortal->AsEngineObject()->GetVectors( NULL, &vRight, &vUp );
						
						Vector ptLinkedCenter = pLinkedPortal->AsEngineObject()->GetAbsOrigin();
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
						float fWrapDist = vPoint1.DistToSqr( pTempPortal->AsEngineObject()->GetAbsOrigin() ) + pLinkedPortal->AsEngineObject()->GetAbsOrigin().DistToSqr( vPoint2 );
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

void UTIL_Portal_AABB( const IEnginePortal *pPortal, Vector &vMin, Vector &vMax )
{
	Vector vOrigin = pPortal->AsEngineObject()->GetAbsOrigin();
	QAngle qAngles = pPortal->AsEngineObject()->GetAbsAngles();

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

float UTIL_IntersectRayWithPortal( const Ray_t &ray, const IEnginePortal *pPortal )
{
	if ( !pPortal || !pPortal->IsActivated() )
	{
		return -1.0f;
	}

	Vector vForward;
	pPortal->AsEngineObject()->GetVectors( &vForward, NULL, NULL );

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

bool UTIL_IntersectRayWithPortalOBB( const IEnginePortal *pPortal, const Ray_t &ray, trace_t *pTrace )
{
	return IntersectRayWithOBB( ray, pPortal->AsEngineObject()->GetAbsOrigin(), pPortal->AsEngineObject()->GetAbsAngles(), vPortalLocalMins, vPortalLocalMaxs, 0.0f, pTrace );
}

bool UTIL_IntersectRayWithPortalOBBAsAABB( const IEnginePortal *pPortal, const Ray_t &ray, trace_t *pTrace )
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

bool UTIL_IsBoxIntersectingPortal( const Vector &vecBoxCenter, const Vector &vecBoxExtents, const IEnginePortal *pPortal, float flTolerance )
{
	if( pPortal == NULL )
		return false;

	return UTIL_IsBoxIntersectingPortal( vecBoxCenter, vecBoxExtents, pPortal->AsEngineObject()->GetAbsOrigin(), pPortal->AsEngineObject()->GetAbsAngles(), flTolerance );
}

IEnginePortal *UTIL_IntersectEntityExtentsWithPortal( const IHandleEntity *pEntity )
{
	int iPortalCount = pEntity->GetEntityList()->GetPortalCount();
	if( iPortalCount == 0 )
		return NULL;

	Vector vMin, vMax;
	pEntity->GetEngineObject()->WorldSpaceAABB( &vMin, &vMax );
	Vector ptCenter = ( vMin + vMax ) * 0.5f;
	Vector vExtents = ( vMax - vMin ) * 0.5f;

	for( int i = 0; i != iPortalCount; ++i )
	{
		IEnginePortal *pTempPortal = pEntity->GetEntityList()->GetPortal(i);
		if( pTempPortal->IsActivated() &&
			(pTempPortal->GetLinkedPortal() != NULL) &&
			UTIL_IsBoxIntersectingPortal( ptCenter, vExtents, pTempPortal )	)
		{
			return pTempPortal;
		}
	}

	return NULL;
}

bool UTIL_Portal_EntityIsInPortalHole( const IEnginePortal *pPortal, IHandleEntity *pEntity )
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
	pPortal->AsEngineObject()->GetVectors( &vPortalForward, &vPortalRight, &vPortalUp );
	Vector ptPortalCenter = pPortal->AsEngineObject()->GetAbsOrigin();

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



