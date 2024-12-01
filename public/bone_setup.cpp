//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

#include "tier0/dbg.h"
#include "mathlib/mathlib.h"
#include "bone_setup.h"
#include <string.h>

#include "collisionutils.h"
#include "vstdlib/random.h"
#include "tier0/vprof.h"
#include "bone_accessor.h"
#include "mathlib/ssequaternion.h"
#include "bitvec.h"
#include "datamanager.h"
#include "convar.h"
#include "vphysics_interface.h"
#include "mathlib/compressed_vector.h"

#ifdef CLIENT_DLL
	#include "posedebugger.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CBoneSetup
{
public:
	CBoneSetup( const IStudioHdr *pStudioHdr, int boneMask, const float poseParameter[], IPoseDebugger *pPoseDebugger = NULL );
	void InitPose( Vector pos[], Quaternion q[] );
	void AccumulatePose( Vector pos[], Quaternion q[], int sequence, float cycle, float flWeight, float flTime, CIKContext *pIKContext );
	void CalcAutoplaySequences(	Vector pos[], Quaternion q[], float flRealTime, CIKContext *pIKContext );
private:
	void AddSequenceLayers( Vector pos[], Quaternion q[], mstudioseqdesc_t &seqdesc, int sequence, float cycle, float flWeight, float flTime, CIKContext *pIKContext );
	void AddLocalLayers( Vector pos[], Quaternion q[], mstudioseqdesc_t &seqdesc, int sequence, float cycle, float flWeight, float flTime, CIKContext *pIKContext );
public:
	const IStudioHdr *m_pStudioHdr;
	int m_boneMask;
	const float *m_flPoseParameter;
	IPoseDebugger *m_pPoseDebugger;
};


CBoneSetupMemoryPool<matrix3x4_t> g_MatrixPool;

// -----------------------------------------------------------------
CBoneCache *CBoneCache::CreateResource( const bonecacheparams_t &params )
{
	short studioToCachedIndex[MAXSTUDIOBONES];
	short cachedToStudioIndex[MAXSTUDIOBONES];
	int cachedBoneCount = 0;
	for ( int i = 0; i < params.pStudioHdr->numbones(); i++ )
	{
		// skip bones that aren't part of the boneMask (and aren't the root bone)
		if (i != 0 && !(params.pStudioHdr->boneFlags(i) & params.boneMask))
		{
			studioToCachedIndex[i] = -1;
			continue;
		}
		studioToCachedIndex[i] = cachedBoneCount;
		cachedToStudioIndex[cachedBoneCount] = i;
		cachedBoneCount++;
	}
	int tableSizeStudio = sizeof(short) * params.pStudioHdr->numbones();
	int tableSizeCached = sizeof(short) * cachedBoneCount;
	int matrixSize = sizeof(matrix3x4_t) * cachedBoneCount;
	int size = ( sizeof(CBoneCache) + tableSizeStudio + tableSizeCached + matrixSize + 3 ) & ~3;
	
	CBoneCache *pMem = (CBoneCache *)malloc( size );
	Construct( pMem );
	pMem->Init( params, size, studioToCachedIndex, cachedToStudioIndex, cachedBoneCount );
	return pMem;
}

unsigned int CBoneCache::EstimatedSize( const bonecacheparams_t &params )
{
	// conservative estimate - max size
	return ( params.pStudioHdr->numbones() * (sizeof(short) + sizeof(short) + sizeof(matrix3x4_t)) + 3 ) & ~3;
}

void CBoneCache::DestroyResource()
{
	free( this );
}


CBoneCache::CBoneCache()
{
	m_size = 0;
	m_cachedBoneCount = 0;
}

void CBoneCache::Init( const bonecacheparams_t &params, unsigned int size, short *pStudioToCached, short *pCachedToStudio, int cachedBoneCount ) 
{
	m_cachedBoneCount = cachedBoneCount;
	m_size = size;
	m_timeValid = params.curtime;
	m_boneMask = params.boneMask;

	int studioTableSize = params.pStudioHdr->numbones() * sizeof(short);
	m_cachedToStudioOffset = studioTableSize;
	memcpy( StudioToCached(), pStudioToCached, studioTableSize );

	int cachedTableSize = cachedBoneCount * sizeof(short);
	memcpy( CachedToStudio(), pCachedToStudio, cachedTableSize );

	m_matrixOffset = ( m_cachedToStudioOffset + cachedTableSize + 3 ) & ~3;
	
	UpdateBones( params.pBoneToWorld, params.pStudioHdr->numbones(), params.curtime );
}

void CBoneCache::UpdateBones( const matrix3x4_t *pBoneToWorld, int numbones, float curtime )
{
	matrix3x4_t *pBones = BoneArray();
	const short *pCachedToStudio = CachedToStudio();

	for ( int i = 0; i < m_cachedBoneCount; i++ )
	{
		int index = pCachedToStudio[i];
		MatrixCopy( pBoneToWorld[index], pBones[i] );
	}
	m_timeValid = curtime;
}

matrix3x4_t *CBoneCache::GetCachedBone( int studioIndex )
{
	int cachedIndex = StudioToCached()[studioIndex];
	if ( cachedIndex >= 0 )
	{
		return BoneArray() + cachedIndex;
	}
	return NULL;
}

void CBoneCache::ReadCachedBones( matrix3x4_t *pBoneToWorld )
{
	matrix3x4_t *pBones = BoneArray();
	const short *pCachedToStudio = CachedToStudio();
	for ( int i = 0; i < m_cachedBoneCount; i++ )
	{
		MatrixCopy( pBones[i], pBoneToWorld[pCachedToStudio[i]] );
	}
}

void CBoneCache::ReadCachedBonePointers(const matrix3x4_t **bones, int numbones )
{
	memset( bones, 0, sizeof(matrix3x4_t *) * numbones );
	matrix3x4_t *pBones = BoneArray();
	const short *pCachedToStudio = CachedToStudio();
	for ( int i = 0; i < m_cachedBoneCount; i++ )
	{
		bones[pCachedToStudio[i]] = pBones + i;
	}
}

bool CBoneCache::IsValid( float curtime, float dt )
{
	if ( curtime - m_timeValid <= dt )
		return true;
	return false;
}


// private functions
matrix3x4_t *CBoneCache::BoneArray()
{
	return (matrix3x4_t *)( (char *)(this+1) + m_matrixOffset );
}

short *CBoneCache::StudioToCached()
{
	return (short *)( (char *)(this+1) );
}

short *CBoneCache::CachedToStudio()
{
	return (short *)( (char *)(this+1) + m_cachedToStudioOffset );
}

// Construct a singleton
static CDataManager<CBoneCache, bonecacheparams_t, CBoneCache *, CThreadFastMutex> g_StudioBoneCache( 128 * 1024L );

CBoneCache *Studio_GetBoneCache( memhandle_t cacheHandle )
{
	AUTO_LOCK( g_StudioBoneCache.AccessMutex() );
	return g_StudioBoneCache.GetResource_NoLock( cacheHandle );
}

memhandle_t Studio_CreateBoneCache( bonecacheparams_t &params )
{
	AUTO_LOCK( g_StudioBoneCache.AccessMutex() );
	return g_StudioBoneCache.CreateResource( params );
}

void Studio_DestroyBoneCache( memhandle_t cacheHandle )
{
	AUTO_LOCK( g_StudioBoneCache.AccessMutex() );
	g_StudioBoneCache.DestroyResource( cacheHandle );
}

void Studio_InvalidateBoneCache( memhandle_t cacheHandle )
{
	AUTO_LOCK( g_StudioBoneCache.AccessMutex() );
	CBoneCache *pCache = g_StudioBoneCache.GetResource_NoLock( cacheHandle );
	if ( pCache )
	{
		pCache->m_timeValid = -1.0f;
	}
}


//-----------------------------------------------------------------------------
// Purpose: calculate a pose for a single sequence
//			adds autolayers, runs local ik rukes
//-----------------------------------------------------------------------------
void CBoneSetup::AddSequenceLayers(
   Vector pos[], 
   Quaternion q[], 
   mstudioseqdesc_t &seqdesc,
   int sequence, 
   float cycle,
   float flWeight,
   float flTime,
   CIKContext *pIKContext
   )
{
	for (int i = 0; i < seqdesc.numautolayers; i++)
	{
		mstudioautolayer_t *pLayer = seqdesc.pAutolayer( i );

		if (pLayer->flags & STUDIO_AL_LOCAL)
			continue;

		float layerCycle = cycle;
		float layerWeight = flWeight;

		if (pLayer->start != pLayer->end)
		{
			float s = 1.0;
			float index;

			if (!(pLayer->flags & STUDIO_AL_POSE))
			{
				index = cycle;
			}
			else
			{
				int iSequence = m_pStudioHdr->iRelativeSeq( sequence, pLayer->iSequence );
				int iPose = m_pStudioHdr->GetSharedPoseParameter( iSequence, pLayer->iPose );
				if (iPose != -1)
				{
					const mstudioposeparamdesc_t &Pose = ((IStudioHdr *)m_pStudioHdr)->pPoseParameter( iPose );
					index = m_flPoseParameter[ iPose ] * (Pose.end - Pose.start) + Pose.start;
				}
				else
				{
					index = 0;
				}
			}

			if (index < pLayer->start)
				continue;
			if (index >= pLayer->end)
				continue;

			if (index < pLayer->peak && pLayer->start != pLayer->peak)
			{
				s = (index - pLayer->start) / (pLayer->peak - pLayer->start);
			}
			else if (index > pLayer->tail && pLayer->end != pLayer->tail)
			{
				s = (pLayer->end - index) / (pLayer->end - pLayer->tail);
			}

			if (pLayer->flags & STUDIO_AL_SPLINE)
			{
				s = SimpleSpline( s );
			}

			if ((pLayer->flags & STUDIO_AL_XFADE) && (index > pLayer->tail))
			{
				layerWeight = ( s * flWeight ) / ( 1 - flWeight + s * flWeight );
			}
			else if (pLayer->flags & STUDIO_AL_NOBLEND)
			{
				layerWeight = s;
			}
			else
			{
				layerWeight = flWeight * s;
			}

			if (!(pLayer->flags & STUDIO_AL_POSE))
			{
				layerCycle = (cycle - pLayer->start) / (pLayer->end - pLayer->start);
			}
		}

		int iSequence = m_pStudioHdr->iRelativeSeq( sequence, pLayer->iSequence );
		AccumulatePose( pos, q, iSequence, layerCycle, layerWeight, flTime, pIKContext );
	}
}


//-----------------------------------------------------------------------------
// Purpose: calculate a pose for a single sequence
//			adds autolayers, runs local ik rukes
//-----------------------------------------------------------------------------
void CBoneSetup::AddLocalLayers(
	Vector pos[], 
	Quaternion q[], 
	mstudioseqdesc_t &seqdesc,
	int sequence, 
	float cycle,
	float flWeight,
	float flTime,
	CIKContext *pIKContext
	)
{
	if (!(seqdesc.flags & STUDIO_LOCAL))
	{
		return;
	}

	for (int i = 0; i < seqdesc.numautolayers; i++)
	{
		mstudioautolayer_t *pLayer = seqdesc.pAutolayer( i );

		if (!(pLayer->flags & STUDIO_AL_LOCAL))
			continue;

		float layerCycle = cycle;
		float layerWeight = flWeight;

		if (pLayer->start != pLayer->end)
		{
			float s = 1.0;

			if (cycle < pLayer->start)
				continue;
			if (cycle >= pLayer->end)
				continue;

			if (cycle < pLayer->peak && pLayer->start != pLayer->peak)
			{
				s = (cycle - pLayer->start) / (pLayer->peak - pLayer->start);
			}
			else if (cycle > pLayer->tail && pLayer->end != pLayer->tail)
			{
				s = (pLayer->end - cycle) / (pLayer->end - pLayer->tail);
			}

			if (pLayer->flags & STUDIO_AL_SPLINE)
			{
				s = SimpleSpline( s );
			}

			if ((pLayer->flags & STUDIO_AL_XFADE) && (cycle > pLayer->tail))
			{
				layerWeight = ( s * flWeight ) / ( 1 - flWeight + s * flWeight );
			}
			else if (pLayer->flags & STUDIO_AL_NOBLEND)
			{
				layerWeight = s;
			}
			else
			{
				layerWeight = flWeight * s;
			}

			layerCycle = (cycle - pLayer->start) / (pLayer->end - pLayer->start);
		}

		int iSequence = m_pStudioHdr->iRelativeSeq( sequence, pLayer->iSequence );
		AccumulatePose( pos, q, iSequence, layerCycle, layerWeight, flTime, pIKContext );
	}
}

//-----------------------------------------------------------------------------
// Purpose: my sleezy attempt at an interface only class
//-----------------------------------------------------------------------------

IBoneSetup::IBoneSetup( const IStudioHdr *pStudioHdr, int boneMask, const float poseParameter[], IPoseDebugger *pPoseDebugger )
{
	m_pBoneSetup = new CBoneSetup( pStudioHdr, boneMask, poseParameter, pPoseDebugger );
}

IBoneSetup::~IBoneSetup( void )
{
	if ( m_pBoneSetup )
	{
		delete m_pBoneSetup;
	}
}

void IBoneSetup::InitPose( Vector pos[], Quaternion q[] )
{
	m_pBoneSetup->m_pStudioHdr->InitPose( pos, q, m_pBoneSetup->m_boneMask );
}

void IBoneSetup::AccumulatePose( Vector pos[], Quaternion q[], int sequence, float cycle, float flWeight, float flTime, CIKContext *pIKContext )
{
	m_pBoneSetup->AccumulatePose( pos, q, sequence, cycle, flWeight, flTime, pIKContext );
}

void IBoneSetup::CalcAutoplaySequences(	Vector pos[], Quaternion q[], float flRealTime, CIKContext *pIKContext )
{
	m_pBoneSetup->CalcAutoplaySequences( pos, q, flRealTime, pIKContext );
}


// takes a "controllers[]" array normalized to 0..1 and adds in the adjustments to pos[], and q[].
void IBoneSetup::CalcBoneAdj( Vector pos[], Quaternion q[], const float controllers[] )
{
	m_pBoneSetup->m_pStudioHdr->CalcBoneAdj( pos, q, controllers, m_pBoneSetup->m_boneMask );
}

IStudioHdr *IBoneSetup::GetStudioHdr()
{
	return (IStudioHdr *)m_pBoneSetup->m_pStudioHdr;
}

CBoneSetup::CBoneSetup( const IStudioHdr *pStudioHdr, int boneMask, const float poseParameter[], IPoseDebugger *pPoseDebugger )
{
	m_pStudioHdr = pStudioHdr;
	m_boneMask = boneMask;
	m_flPoseParameter = poseParameter;
	m_pPoseDebugger = pPoseDebugger;
}

#if 0
//-----------------------------------------------------------------------------
// Purpose: calculate a pose for a single sequence
//			adds autolayers, runs local ik rukes
//-----------------------------------------------------------------------------
void CalcPose(
	const IStudioHdr *pStudioHdr,
	CIKContext *pIKContext,
	Vector pos[], 
	Quaternion q[], 
	int sequence, 
	float cycle,
	const float poseParameter[],
	int boneMask,
	float flWeight,
	float flTime
	)
{
	mstudioseqdesc_t	&seqdesc = ((IStudioHdr *)pStudioHdr)->pSeqdesc( sequence );

	Assert( flWeight >= 0.0f && flWeight <= 1.0f );
	// This shouldn't be necessary, but the Assert should help us catch whoever is screwing this up
	flWeight = clamp( flWeight, 0.0f, 1.0f );

	// add any IK locks to prevent numautolayers from moving extremities 
	CIKContext seq_ik;
	if (seqdesc.numiklocks)
	{
		seq_ik.Init( pStudioHdr, vec3_angle, vec3_origin, 0.0, 0, boneMask ); // local space relative so absolute position doesn't mater
		seq_ik.AddSequenceLocks( seqdesc, pos, q );
	}

	CalcPoseSingle( pStudioHdr, pos, q, seqdesc, sequence, cycle, poseParameter, boneMask, flTime );

	if ( pIKContext )
	{
		pIKContext->AddDependencies( seqdesc, sequence, cycle, poseParameter, flWeight );
	}
	
	AddSequenceLayers( pStudioHdr, pIKContext, pos, q, seqdesc, sequence, cycle, poseParameter, boneMask, flWeight, flTime );

	if (seqdesc.numiklocks)
	{
		seq_ik.SolveSequenceLocks( seqdesc, pos, q );
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: accumulate a pose for a single sequence on top of existing animation
//			adds autolayers, runs local ik rukes
//-----------------------------------------------------------------------------
void CBoneSetup::AccumulatePose(
	Vector pos[], 
	Quaternion q[], 
	int sequence, 
	float cycle,
	float flWeight,
	float flTime,
	CIKContext *pIKContext
	)
{
	Vector		pos2[MAXSTUDIOBONES];
	QuaternionAligned	q2[MAXSTUDIOBONES];

	Assert( flWeight >= 0.0f && flWeight <= 1.0f );
	// This shouldn't be necessary, but the Assert should help us catch whoever is screwing this up
	flWeight = clamp( flWeight, 0.0f, 1.0f );

	if ( sequence < 0 )
		return;

#ifdef CLIENT_DLL
	// Trigger pose debugger
	if (m_pPoseDebugger)
	{
		m_pPoseDebugger->AccumulatePose( m_pStudioHdr, pIKContext, pos, q, sequence, cycle, m_flPoseParameter, m_boneMask, flWeight, flTime );
	}
#endif

	mstudioseqdesc_t	&seqdesc = ((IStudioHdr *)m_pStudioHdr)->pSeqdesc( sequence );

	// add any IK locks to prevent extremities from moving
	CIKContext seq_ik;
	if (seqdesc.numiklocks)
	{
		seq_ik.Init( m_pStudioHdr, vec3_angle, vec3_origin, 0.0, 0, m_boneMask );  // local space relative so absolute position doesn't mater
		seq_ik.AddSequenceLocks( seqdesc, pos, q );
	}

	if (seqdesc.flags & STUDIO_LOCAL)
	{
		m_pStudioHdr->InitPose( pos2, q2, m_boneMask );
	}

	if (m_pStudioHdr->CalcPoseSingle( pos2, q2, seqdesc, sequence, cycle, m_flPoseParameter, m_boneMask, flTime ))
	{
		// this weight is wrong, the IK rules won't composite at the correct intensity
		AddLocalLayers( pos2, q2, seqdesc, sequence, cycle, 1.0, flTime, pIKContext );
		m_pStudioHdr->SlerpBones( q, pos, seqdesc, sequence, q2, pos2, flWeight, m_boneMask );
	}


	if ( pIKContext )
	{
		pIKContext->AddDependencies( seqdesc, sequence, cycle, m_flPoseParameter, flWeight );
	}

	AddSequenceLayers( pos, q, seqdesc, sequence, cycle, flWeight, flTime, pIKContext );

	if (seqdesc.numiklocks)
	{
		seq_ik.SolveSequenceLocks( seqdesc, pos, q );
	}
}


void CalcBoneDerivatives( Vector &velocity, AngularImpulse &angVel, const matrix3x4_t &prev, const matrix3x4_t &current, float dt )
{
	float scale = 1.0;
	if ( dt > 0 )
	{
		scale = 1.0 / dt;
	}
	
	Vector endPosition, startPosition, deltaAxis;
	QAngle endAngles, startAngles;
	float deltaAngle;

	MatrixAngles( prev, startAngles, startPosition );
	MatrixAngles( current, endAngles, endPosition );

	velocity.x = (endPosition.x - startPosition.x) * scale;
	velocity.y = (endPosition.y - startPosition.y) * scale;
	velocity.z = (endPosition.z - startPosition.z) * scale;
	RotationDeltaAxisAngle( startAngles, endAngles, deltaAxis, deltaAngle );
	VectorScale( deltaAxis, (deltaAngle * scale), angVel );
}

void CalcBoneVelocityFromDerivative( const QAngle &vecAngles, Vector &velocity, AngularImpulse &angVel, const matrix3x4_t &current )
{
	Vector vecLocalVelocity;
	AngularImpulse LocalAngVel;
	Quaternion q;
	float angle;
	MatrixAngles( current, q, vecLocalVelocity );
	QuaternionAxisAngle( q, LocalAngVel, angle );
	LocalAngVel *= angle;

	matrix3x4_t matAngles;
	AngleMatrix( vecAngles, matAngles );
	VectorTransform( vecLocalVelocity, matAngles, velocity );
	VectorTransform( LocalAngVel, matAngles, angVel );
}


class CIKSolver
{
public:
//-------- SOLVE TWO LINK INVERSE KINEMATICS -------------
// Author: Ken Perlin
//
// Given a two link joint from [0,0,0] to end effector position P,
// let link lengths be a and b, and let norm |P| = c.  Clearly a+b <= c.
//
// Problem: find a "knee" position Q such that |Q| = a and |P-Q| = b.
//
// In the case of a point on the x axis R = [c,0,0], there is a
// closed form solution S = [d,e,0], where |S| = a and |R-S| = b:
//
//    d2+e2 = a2                  -- because |S| = a
//    (c-d)2+e2 = b2              -- because |R-S| = b
//
//    c2-2cd+d2+e2 = b2           -- combine the two equations
//    c2-2cd = b2 - a2
//    c-2d = (b2-a2)/c
//    d - c/2 = (a2-b2)/c / 2
//
//    d = (c + (a2-b2/c) / 2      -- to solve for d and e.
//    e = sqrt(a2-d2)

   static float findD(float a, float b, float c) {
      return (c + (a*a-b*b)/c) / 2;
   }
   static float findE(float a, float d) { return sqrt(a*a-d*d); } 

// This leads to a solution to the more general problem:
//
//   (1) R = Mfwd(P)         -- rotate P onto the x axis
//   (2) Solve for S
//   (3) Q = Minv(S)         -- rotate back again

   float Mfwd[3][3];
   float Minv[3][3];

   bool solve(float A, float B, float const P[], float const D[], float Q[]) {
      float R[3];
      defineM(P,D);
      rot(Minv,P,R);
	  float r = length(R);
      float d = findD(A,B,r);
      float e = findE(A,d);
      float S[3] = {d,e,0};
      rot(Mfwd,S,Q);
      return d > (r - B) && d < A;
   }

// If "knee" position Q needs to be as close as possible to some point D,
// then choose M such that M(D) is in the y>0 half of the z=0 plane.
//
// Given that constraint, define the forward and inverse of M as follows:

   void defineM(float const P[], float const D[]) {
      float *X = Minv[0], *Y = Minv[1], *Z = Minv[2];

// Minv defines a coordinate system whose x axis contains P, so X = unit(P).
	  int i;
      for (i = 0 ; i < 3 ; i++)
         X[i] = P[i];
      normalize(X);

// Its y axis is perpendicular to P, so Y = unit( E - X(E·X) ).

      float dDOTx = dot(D,X);
      for (i = 0 ; i < 3 ; i++)
         Y[i] = D[i] - dDOTx * X[i];
      normalize(Y);

// Its z axis is perpendicular to both X and Y, so Z = X×Y.

      cross(X,Y,Z);

// Mfwd = (Minv)T, since transposing inverts a rotation matrix.

      for (i = 0 ; i < 3 ; i++) {
         Mfwd[i][0] = Minv[0][i];
         Mfwd[i][1] = Minv[1][i];
         Mfwd[i][2] = Minv[2][i];
      }
   }

//------------ GENERAL VECTOR MATH SUPPORT -----------

   static float dot(float const a[], float const b[]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

   static float length(float const v[]) { return sqrt( dot(v,v) ); }

   static void normalize(float v[]) {
      float norm = length(v);
      for (int i = 0 ; i < 3 ; i++)
         v[i] /= norm;
   }

   static void cross(float const a[], float const b[], float c[]) {
      c[0] = a[1] * b[2] - a[2] * b[1];
      c[1] = a[2] * b[0] - a[0] * b[2];
      c[2] = a[0] * b[1] - a[1] * b[0];
   }

   static void rot(float const M[3][3], float const src[], float dst[]) {
      for (int i = 0 ; i < 3 ; i++)
         dst[i] = dot(M[i],src);
   }
};



//-----------------------------------------------------------------------------
// Purpose: visual debugging code
//-----------------------------------------------------------------------------
#if 1
inline void debugLine(const Vector& origin, const Vector& dest, int r, int g, int b, bool noDepthTest, float duration) { };
#else
extern void drawLine( const Vector &p1, const Vector &p2, int r = 0, int g = 0, int b = 1, bool noDepthTest = true, float duration = 0.1 );
void debugLine(const Vector& origin, const Vector& dest, int r, int g, int b, bool noDepthTest, float duration)
{
	drawLine( origin, dest, r, g, b, noDepthTest, duration );
}
#endif


//-----------------------------------------------------------------------------
// Purpose: for a 2 bone chain, find the IK solution and reset the matrices
//-----------------------------------------------------------------------------
bool Studio_SolveIK( mstudioikchain_t *pikchain, Vector &targetFoot, matrix3x4_t *pBoneToWorld )
{
	if (pikchain->pLink(0)->kneeDir.LengthSqr() > 0.0)
	{
		Vector targetKneeDir, targetKneePos;
		// FIXME: knee length should be as long as the legs
		Vector tmp = pikchain->pLink( 0 )->kneeDir;
		VectorRotate( tmp, pBoneToWorld[ pikchain->pLink( 0 )->bone ], targetKneeDir );
		MatrixPosition( pBoneToWorld[ pikchain->pLink( 1 )->bone ], targetKneePos );
		return Studio_SolveIK( pikchain->pLink( 0 )->bone, pikchain->pLink( 1 )->bone, pikchain->pLink( 2 )->bone, targetFoot, targetKneePos, targetKneeDir, pBoneToWorld );
	}
	else
	{
		return Studio_SolveIK( pikchain->pLink( 0 )->bone, pikchain->pLink( 1 )->bone, pikchain->pLink( 2 )->bone, targetFoot, pBoneToWorld );
	}
}


#define KNEEMAX_EPSILON 0.9998 // (0.9998 is about 1 degree)

//-----------------------------------------------------------------------------
// Purpose: Solve Knee position for a known hip and foot location, but no specific knee direction preference
//-----------------------------------------------------------------------------

bool Studio_SolveIK( int iThigh, int iKnee, int iFoot, Vector &targetFoot, matrix3x4_t *pBoneToWorld )
{
	Vector worldFoot, worldKnee, worldThigh;

	MatrixPosition( pBoneToWorld[ iThigh ], worldThigh );
	MatrixPosition( pBoneToWorld[ iKnee ], worldKnee );
	MatrixPosition( pBoneToWorld[ iFoot ], worldFoot );

	//debugLine( worldThigh, worldKnee, 0, 0, 255, true, 0 );
	//debugLine( worldKnee, worldFoot, 0, 0, 255, true, 0 );

	Vector ikFoot, ikKnee;

	ikFoot = targetFoot - worldThigh;
	ikKnee = worldKnee - worldThigh;

	float l1 = (worldKnee-worldThigh).Length();
	float l2 = (worldFoot-worldKnee).Length();
	float l3 = (worldFoot-worldThigh).Length();

	// leg too straight to figure out knee?
	if (l3 > (l1 + l2) * KNEEMAX_EPSILON)
	{
		return false;
	}

	Vector ikHalf = (worldFoot-worldThigh) * (l1 / l3);

	// FIXME: what to do when the knee completely straight?
	Vector ikKneeDir = ikKnee - ikHalf;
	VectorNormalize( ikKneeDir );

	return Studio_SolveIK( iThigh, iKnee, iFoot, targetFoot, worldKnee, ikKneeDir, pBoneToWorld );
}

//-----------------------------------------------------------------------------
// Purpose: Realign the matrix so that its X axis points along the desired axis.
//-----------------------------------------------------------------------------
void Studio_AlignIKMatrix( matrix3x4_t &mMat, const Vector &vAlignTo )
{
	Vector tmp1, tmp2, tmp3;

	// Column 0 (X) becomes the vector.
	tmp1 = vAlignTo;
	VectorNormalize( tmp1 );
	MatrixSetColumn( tmp1, 0, mMat );

	// Column 1 (Y) is the cross of the vector and column 2 (Z).
	MatrixGetColumn( mMat, 2, tmp3 );
	tmp2 = tmp3.Cross( tmp1 );
	VectorNormalize( tmp2 );
	// FIXME: check for X being too near to Z
	MatrixSetColumn( tmp2, 1, mMat );

	// Column 2 (Z) is the cross of columns 0 (X) and 1 (Y).
	tmp3 = tmp1.Cross( tmp2 );
	MatrixSetColumn( tmp3, 2, mMat );
}


//-----------------------------------------------------------------------------
// Purpose: Solve Knee position for a known hip and foot location, and a known knee direction
//-----------------------------------------------------------------------------

bool Studio_SolveIK( int iThigh, int iKnee, int iFoot, Vector &targetFoot, Vector &targetKneePos, Vector &targetKneeDir, matrix3x4_t *pBoneToWorld )
{
	Vector worldFoot, worldKnee, worldThigh;

	MatrixPosition( pBoneToWorld[ iThigh ], worldThigh );
	MatrixPosition( pBoneToWorld[ iKnee ], worldKnee );
	MatrixPosition( pBoneToWorld[ iFoot ], worldFoot );

	//debugLine( worldThigh, worldKnee, 0, 0, 255, true, 0 );
	//debugLine( worldThigh, worldThigh + targetKneeDir, 0, 0, 255, true, 0 );
	// debugLine( worldKnee, targetKnee, 0, 0, 255, true, 0 );

	Vector ikFoot, ikTargetKnee, ikKnee;

	ikFoot = targetFoot - worldThigh;
	ikKnee = targetKneePos - worldThigh;

	float l1 = (worldKnee-worldThigh).Length();
	float l2 = (worldFoot-worldKnee).Length();

	// exaggerate knee targets for legs that are nearly straight
	// FIXME: should be configurable, and the ikKnee should be from the original animation, not modifed
	float d = (targetFoot-worldThigh).Length() - min( l1, l2 );
	d = max( l1 + l2, d );
	// FIXME: too short knee directions cause trouble
	d = d * 100;

	ikTargetKnee = ikKnee + targetKneeDir * d;

	// debugLine( worldKnee, worldThigh + ikTargetKnee, 0, 0, 255, true, 0 );

	int color[3] = { 0, 255, 0 };

	// too far away? (0.9998 is about 1 degree)
	if (ikFoot.Length() > (l1 + l2) * KNEEMAX_EPSILON)
	{
		VectorNormalize( ikFoot );
		VectorScale( ikFoot, (l1 + l2) * KNEEMAX_EPSILON, ikFoot );
		color[0] = 255; color[1] = 0; color[2] = 0;
	}

	// too close?
	// limit distance to about an 80 degree knee bend
	float minDist = max( fabs(l1 - l2) * 1.15, min( l1, l2 ) * 0.15 );
	if (ikFoot.Length() < minDist)
	{
		// too close to get an accurate vector, just use original vector
		ikFoot = (worldFoot - worldThigh);
		VectorNormalize( ikFoot );
		VectorScale( ikFoot, minDist, ikFoot );
	}

	CIKSolver ik;
	if (ik.solve( l1, l2, ikFoot.Base(), ikTargetKnee.Base(), ikKnee.Base() ))
	{
		matrix3x4_t& mWorldThigh = pBoneToWorld[ iThigh ];
		matrix3x4_t& mWorldKnee = pBoneToWorld[ iKnee ];
		matrix3x4_t& mWorldFoot = pBoneToWorld[ iFoot ];

		//debugLine( worldThigh, ikKnee + worldThigh, 255, 0, 0, true, 0 );
		//debugLine( ikKnee + worldThigh, ikFoot + worldThigh, 255, 0, 0, true,0 );

		// debugLine( worldThigh, ikKnee + worldThigh, color[0], color[1], color[2], true, 0 );
		// debugLine( ikKnee + worldThigh, ikFoot + worldThigh, color[0], color[1], color[2], true,0 );


		// build transformation matrix for thigh
		Studio_AlignIKMatrix( mWorldThigh, ikKnee );
		Studio_AlignIKMatrix( mWorldKnee, ikFoot - ikKnee );


		mWorldKnee[0][3] = ikKnee.x + worldThigh.x;
		mWorldKnee[1][3] = ikKnee.y + worldThigh.y;
		mWorldKnee[2][3] = ikKnee.z + worldThigh.z;

		mWorldFoot[0][3] = ikFoot.x + worldThigh.x;
		mWorldFoot[1][3] = ikFoot.y + worldThigh.y;
		mWorldFoot[2][3] = ikFoot.z + worldThigh.z;

		return true;
	}
	else
	{
		/*
		debugLine( worldThigh, worldThigh + ikKnee, 255, 0, 0, true, 0 );
		debugLine( worldThigh + ikKnee, worldThigh + ikFoot, 255, 0, 0, true, 0 );
		debugLine( worldThigh + ikFoot, worldThigh, 255, 0, 0, true, 0 );
		debugLine( worldThigh + ikKnee, worldThigh + ikTargetKnee, 255, 0, 0, true, 0 );
		*/
		return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


CIKContext::CIKContext()
{
	m_target.EnsureCapacity( 12 ); // FIXME: this sucks, shouldn't it be grown?
	m_iFramecounter = -1;
	m_pStudioHdr = NULL;
	m_flTime = -1.0f;
	m_target.SetSize( 0 );
}


void CIKContext::Init( const IStudioHdr *pStudioHdr, const QAngle &angles, const Vector &pos, float flTime, int iFramecounter, int boneMask )
{
	m_pStudioHdr = pStudioHdr;
	m_ikChainRule.RemoveAll(); // m_numikrules = 0;
	if (pStudioHdr->numikchains())
	{
		m_ikChainRule.SetSize( pStudioHdr->numikchains() );

		// FIXME: Brutal hackery to prevent a crash
		if (m_target.Count() == 0)
		{
			m_target.SetSize(12);
			memset( m_target.Base(), 0, sizeof(m_target[0])*m_target.Count() );
			ClearTargets();
		}

	}
	else
	{
		m_target.SetSize( 0 );
	}
	AngleMatrix( angles, pos, m_rootxform );
	m_iFramecounter = iFramecounter;
	m_flTime = flTime;
	m_boneMask = boneMask;
}

void CIKContext::AddDependencies( mstudioseqdesc_t &seqdesc, int iSequence, float flCycle, const float poseParameters[], float flWeight )
{
	int i;

	if ( m_pStudioHdr->numikchains() == 0)
		return;

	if (seqdesc.numikrules == 0)
		return;

	ikcontextikrule_t ikrule;

	Assert( flWeight >= 0.0f && flWeight <= 1.0f );
	// This shouldn't be necessary, but the Assert should help us catch whoever is screwing this up
	flWeight = clamp( flWeight, 0.0f, 1.0f );

	// unify this
	if (seqdesc.flags & STUDIO_REALTIME)
	{
		float cps = m_pStudioHdr->Studio_CPS( seqdesc, iSequence, poseParameters );
		flCycle = m_flTime * cps;
		flCycle = flCycle - (int)flCycle;
	}
	else if (flCycle < 0 || flCycle >= 1)
	{
		if (seqdesc.flags & STUDIO_LOOPING)
		{
			flCycle = flCycle - (int)flCycle;
			if (flCycle < 0) flCycle += 1;
		}
		else
		{
			flCycle = max( 0.f, min( flCycle, 0.9999f ) );
		}
	}

	mstudioanimdesc_t *panim[4];
	float	weight[4];

	m_pStudioHdr->Studio_SeqAnims( seqdesc, iSequence, poseParameters, panim, weight );

	// FIXME: add proper number of rules!!!
	for (i = 0; i < seqdesc.numikrules; i++)
	{
		if ( !m_pStudioHdr->Studio_IKSequenceError( seqdesc, iSequence, flCycle, i, poseParameters, panim, weight, ikrule ) )
			continue;

		// don't add rule if the bone isn't going to be calculated
		int bone = m_pStudioHdr->pIKChain( ikrule.chain )->pLink( 2 )->bone;
		if ( !(m_pStudioHdr->boneFlags( bone ) & m_boneMask))
			continue;

		// or if its relative bone isn't going to be calculated
		if ( ikrule.bone >= 0 && !(m_pStudioHdr->boneFlags( ikrule.bone ) & m_boneMask))
			continue;

		// FIXME: Brutal hackery to prevent a crash
		if (m_target.Count() == 0)
		{
			m_target.SetSize(12);
			memset( m_target.Base(), 0, sizeof(m_target[0])*m_target.Count() );
			ClearTargets();
		}

		ikrule.flRuleWeight = flWeight;

		if (ikrule.flRuleWeight * ikrule.flWeight > 0.999)
		{
			if ( ikrule.type != IK_UNLATCH)
			{
				// clear out chain if rule is 100%
				m_ikChainRule.Element( ikrule.chain ).RemoveAll( );
				if ( ikrule.type == IK_RELEASE)
				{
					continue;
				}
			}
		}

 		int nIndex = m_ikChainRule.Element( ikrule.chain ).AddToTail( );
  		m_ikChainRule.Element( ikrule.chain ).Element( nIndex ) = ikrule;
	}
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CIKContext::AddAutoplayLocks( Vector pos[], Quaternion q[] )
{
	// skip all array access if no autoplay locks.
	if (m_pStudioHdr->GetNumIKAutoplayLocks() == 0)
	{
		return;
	}

	matrix3x4_t *boneToWorld = g_MatrixPool.Alloc();
	CBoneBitList boneComputed;

	int ikOffset = m_ikLock.AddMultipleToTail( m_pStudioHdr->GetNumIKAutoplayLocks() );
	memset( &m_ikLock[ikOffset], 0, sizeof(ikcontextikrule_t)*m_pStudioHdr->GetNumIKAutoplayLocks() );

	for (int i = 0; i < m_pStudioHdr->GetNumIKAutoplayLocks(); i++)
	{
		const mstudioiklock_t &lock = ((IStudioHdr *)m_pStudioHdr)->pIKAutoplayLock( i );
		mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( lock.chain );
		int bone = pchain->pLink( 2 )->bone;

		// don't bother with iklock if the bone isn't going to be calculated
		if ( !(m_pStudioHdr->boneFlags( bone ) & m_boneMask))
			continue;

		// eval current ik'd bone
		BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );

		ikcontextikrule_t &ikrule = m_ikLock[ i + ikOffset ];

		ikrule.chain = lock.chain;
		ikrule.slot = i;
		ikrule.type = IK_WORLD;

		MatrixAngles( boneToWorld[bone], ikrule.q, ikrule.pos );

		// save off current knee direction
		if (pchain->pLink(0)->kneeDir.LengthSqr() > 0.0)
		{
			Vector tmp = pchain->pLink( 0 )->kneeDir;
			VectorRotate( pchain->pLink( 0 )->kneeDir, boneToWorld[ pchain->pLink( 0 )->bone ], ikrule.kneeDir );
			MatrixPosition( boneToWorld[ pchain->pLink( 1 )->bone ], ikrule.kneePos ); 
		}
		else
		{
			ikrule.kneeDir.Init( );
		}
	}
	g_MatrixPool.Free( boneToWorld );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CIKContext::AddSequenceLocks( mstudioseqdesc_t &seqdesc, Vector pos[], Quaternion q[] )
{
	if ( m_pStudioHdr->numikchains() == 0)
	{
		return;
	}

	if ( seqdesc.numiklocks == 0 )
	{
		return;
	}

	matrix3x4_t *boneToWorld = g_MatrixPool.Alloc();
	CBoneBitList boneComputed;

	int ikOffset = m_ikLock.AddMultipleToTail( seqdesc.numiklocks );
	memset( &m_ikLock[ikOffset], 0, sizeof(ikcontextikrule_t) * seqdesc.numiklocks );

	for (int i = 0; i < seqdesc.numiklocks; i++)
	{
		mstudioiklock_t *plock = seqdesc.pIKLock( i );
		mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( plock->chain );
		int bone = pchain->pLink( 2 )->bone;

		// don't bother with iklock if the bone isn't going to be calculated
		if ( !(m_pStudioHdr->boneFlags( bone ) & m_boneMask))
			continue;

		// eval current ik'd bone
		BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );

		ikcontextikrule_t &ikrule = m_ikLock[i+ikOffset];
		ikrule.chain = i;
		ikrule.slot = i;
		ikrule.type = IK_WORLD;

		MatrixAngles( boneToWorld[bone], ikrule.q, ikrule.pos );

		// save off current knee direction
		if (pchain->pLink(0)->kneeDir.LengthSqr() > 0.0)
		{
			VectorRotate( pchain->pLink( 0 )->kneeDir, boneToWorld[ pchain->pLink( 0 )->bone ], ikrule.kneeDir );
		}
		else
		{
			ikrule.kneeDir.Init( );
		}
	}
	g_MatrixPool.Free( boneToWorld );
}

//-----------------------------------------------------------------------------
// Purpose: build boneToWorld transforms for a specific bone
//-----------------------------------------------------------------------------
void CIKContext::BuildBoneChain(
	const Vector pos[], 
	const Quaternion q[], 
	int	iBone,
	matrix3x4_t *pBoneToWorld,
	CBoneBitList &boneComputed )
{
	Assert( m_pStudioHdr->boneFlags( iBone ) & m_boneMask );
	m_pStudioHdr->BuildBoneChain( m_rootxform, pos, q, iBone, pBoneToWorld, boneComputed );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void CIKTarget::SetOwner( int entindex, const Vector &pos, const QAngle &angles )
{
	latched.owner = entindex;
	latched.absOrigin = pos;
	latched.absAngles = angles;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void CIKTarget::ClearOwner( void )
{
	latched.owner = -1;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CIKTarget::GetOwner( void )
{
	return latched.owner;
}

//-----------------------------------------------------------------------------
// Purpose: update the latched IK values that are in a moving frame of reference
//-----------------------------------------------------------------------------

void CIKTarget::UpdateOwner( int entindex, const Vector &pos, const QAngle &angles )
{
	if (pos == latched.absOrigin && angles == latched.absAngles)
		return;

	matrix3x4_t in, out;
	AngleMatrix( angles, pos, in );
	AngleIMatrix( latched.absAngles, latched.absOrigin, out );

	matrix3x4_t tmp1, tmp2;
	QuaternionMatrix( latched.q, latched.pos, tmp1 );
	ConcatTransforms( out, tmp1, tmp2 );
	ConcatTransforms( in, tmp2, tmp1 );
	MatrixAngles( tmp1, latched.q, latched.pos );
}


//-----------------------------------------------------------------------------
// Purpose: sets the ground position of an ik target
//-----------------------------------------------------------------------------

void CIKTarget::SetPos( const Vector &pos )
{
	est.pos = pos;
}

//-----------------------------------------------------------------------------
// Purpose: sets the ground "identity" orientation of an ik target
//-----------------------------------------------------------------------------

void CIKTarget::SetAngles( const QAngle &angles )
{
	AngleQuaternion( angles, est.q );
}

//-----------------------------------------------------------------------------
// Purpose: sets the ground "identity" orientation of an ik target
//-----------------------------------------------------------------------------

void CIKTarget::SetQuaternion( const Quaternion &q )
{
	est.q = q;
}

//-----------------------------------------------------------------------------
// Purpose: calculates a ground "identity" orientation based on the surface
//			normal of the ground and the desired ground identity orientation
//-----------------------------------------------------------------------------

void CIKTarget::SetNormal( const Vector &normal )
{
	// recalculate foot angle based on slope of surface
	matrix3x4_t m1;
	Vector forward, right;
	QuaternionMatrix( est.q, m1 );

	MatrixGetColumn( m1, 1, right );
	forward = CrossProduct( right, normal );
	right = CrossProduct( normal, forward );
	MatrixSetColumn( forward, 0, m1 );
	MatrixSetColumn( right, 1, m1 );
	MatrixSetColumn( normal, 2, m1 );
	QAngle a1;
	Vector p1;
	MatrixAngles( m1, est.q, p1 );
}


//-----------------------------------------------------------------------------
// Purpose: estimates the ground impact at the center location assuming a the edge of 
//			an Z axis aligned disc collided with it the surface.
//-----------------------------------------------------------------------------

void CIKTarget::SetPosWithNormalOffset( const Vector &pos, const Vector &normal )
{
	// assume it's a disc edge intersecting with the floor, so try to estimate the z location of the center
	est.pos = pos;
	if (normal.z > 0.9999)
	{
		return;
	}
	// clamp at 45 degrees
	else if (normal.z > 0.707)
	{
		// tan == sin / cos
		float tan = sqrt( 1 - normal.z * normal.z ) / normal.z;
		est.pos.z = est.pos.z - est.radius * tan;
	}
	else
	{
		est.pos.z = est.pos.z - est.radius;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CIKTarget::SetOnWorld( bool bOnWorld )
{
	est.onWorld = bOnWorld;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

bool CIKTarget::IsActive()
{ 
	return (est.flWeight > 0.0f);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CIKTarget::IKFailed( void )
{
	latched.deltaPos.Init();
	latched.deltaQ.Init();
	latched.pos = ideal.pos;
	latched.q = ideal.q;
	est.latched = 0.0;
	est.flWeight = 0.0;
	est.onWorld = false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CIKTarget::MoveReferenceFrame( Vector &deltaPos, QAngle &deltaAngles )
{
	est.pos -= deltaPos;
	latched.pos -= deltaPos;
	offset.pos -= deltaPos;
	ideal.pos -= deltaPos;
}



//-----------------------------------------------------------------------------
// Purpose: Invalidate any IK locks.
//-----------------------------------------------------------------------------

void CIKContext::ClearTargets( void )
{
	int i;
	for (i = 0; i < m_target.Count(); i++)
	{
		m_target[i].latched.iFramecounter = -9999;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Run through the rules that survived and turn a specific bones boneToWorld 
//			transform into a pos and q in parents bonespace
//-----------------------------------------------------------------------------

void CIKContext::UpdateTargets( Vector pos[], Quaternion q[], matrix3x4_t boneToWorld[], CBoneBitList &boneComputed )
{
	int i, j;

	for (i = 0; i < m_target.Count(); i++)
	{
		m_target[i].est.flWeight = 0.0f;
		m_target[i].est.latched = 1.0f;
		m_target[i].est.release = 1.0f;
		m_target[i].est.height = 0.0f;
		m_target[i].est.floor = 0.0f;
		m_target[i].est.radius = 0.0f;
		m_target[i].offset.pos.Init();
		m_target[i].offset.q.Init();
	}

	AutoIKRelease( );

	for (j = 0; j < m_ikChainRule.Count(); j++)
	{
		for (i = 0; i < m_ikChainRule.Element( j ).Count(); i++)
		{
			ikcontextikrule_t *pRule = &m_ikChainRule.Element( j ).Element( i );

			// ikchainresult_t *pChainRule = &chainRule[ m_ikRule[i].chain ];

			switch( pRule->type )
			{
			case IK_ATTACHMENT:
			case IK_GROUND:
			// case IK_SELF:
				{
					matrix3x4_t footTarget;
					CIKTarget *pTarget = &m_target[pRule->slot];
					pTarget->chain = pRule->chain;
					pTarget->type = pRule->type;

					if (pRule->type == IK_ATTACHMENT)
					{
						pTarget->offset.pAttachmentName = pRule->szLabel;
					}
					else
					{
						pTarget->offset.pAttachmentName = NULL;
					}

					if (pRule->flRuleWeight == 1.0f || pTarget->est.flWeight == 0.0f)
					{
						pTarget->offset.q = pRule->q;
						pTarget->offset.pos = pRule->pos;
						pTarget->est.height = pRule->height;
						pTarget->est.floor = pRule->floor;
						pTarget->est.radius = pRule->radius;
						pTarget->est.latched = pRule->latched * pRule->flRuleWeight;
						pTarget->est.release = pRule->release;
						pTarget->est.flWeight = pRule->flWeight * pRule->flRuleWeight;
					}
					else
					{
						QuaternionSlerp( pTarget->offset.q, pRule->q, pRule->flRuleWeight, pTarget->offset.q );
						pTarget->offset.pos = Lerp( pRule->flRuleWeight, pTarget->offset.pos, pRule->pos );
						pTarget->est.height = Lerp( pRule->flRuleWeight, pTarget->est.height, pRule->height );
						pTarget->est.floor = Lerp( pRule->flRuleWeight, pTarget->est.floor, pRule->floor );
						pTarget->est.radius = Lerp( pRule->flRuleWeight, pTarget->est.radius, pRule->radius );
						//pTarget->est.latched = Lerp( pRule->flRuleWeight, pTarget->est.latched, pRule->latched );
						pTarget->est.latched = min( pTarget->est.latched, pRule->latched );
						pTarget->est.release = Lerp( pRule->flRuleWeight, pTarget->est.release, pRule->release );
						pTarget->est.flWeight = Lerp( pRule->flRuleWeight, pTarget->est.flWeight, pRule->flWeight );
					}

					if ( pRule->type == IK_GROUND )
					{
						pTarget->latched.deltaPos.z = 0;
						pTarget->est.pos.z = pTarget->est.floor + m_rootxform[2][3];
					}
				}
			break;
			case IK_UNLATCH:
				{
					CIKTarget *pTarget = &m_target[pRule->slot];
					if (pRule->latched > 0.0)
						pTarget->est.latched = 0.0;
					else
						pTarget->est.latched = min( pTarget->est.latched, 1.0f - pRule->flWeight );
				}
				break;
			case IK_RELEASE:
				{
					CIKTarget *pTarget = &m_target[pRule->slot];
					if (pRule->latched > 0.0)
						pTarget->est.latched = 0.0;
					else
						pTarget->est.latched = min( pTarget->est.latched, 1.0f - pRule->flWeight );

					pTarget->est.flWeight = (pTarget->est.flWeight) * (1 - pRule->flWeight * pRule->flRuleWeight);
				}
				break;
			}
		}
	}

	for (i = 0; i < m_target.Count(); i++)
	{
		CIKTarget *pTarget = &m_target[i];
		if (pTarget->est.flWeight > 0.0)
		{
			mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( pTarget->chain );
			// ikchainresult_t *pChainRule = &chainRule[ i ];
			int bone = pchain->pLink( 2 )->bone;

			// eval current ik'd bone
			BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );

			// xform IK target error into world space
			matrix3x4_t local;
			matrix3x4_t worldFootpad;
			QuaternionMatrix( pTarget->offset.q, pTarget->offset.pos, local );
			MatrixInvert( local, local );
			ConcatTransforms( boneToWorld[bone], local, worldFootpad );

			if (pTarget->est.latched == 1.0)
			{
				pTarget->latched.bNeedsLatch = true;
			}
			else
			{
				pTarget->latched.bNeedsLatch = false;
			}

			// disable latched position if it looks invalid
			if (m_iFramecounter < 0 || pTarget->latched.iFramecounter < m_iFramecounter - 1 || pTarget->latched.iFramecounter > m_iFramecounter)
			{
				pTarget->latched.bHasLatch = false;
				pTarget->latched.influence = 0.0;
			}
			pTarget->latched.iFramecounter = m_iFramecounter;

			// find ideal contact position
			MatrixAngles( worldFootpad, pTarget->ideal.q, pTarget->ideal.pos );
			pTarget->est.q = pTarget->ideal.q;
			pTarget->est.pos = pTarget->ideal.pos;

			float latched = pTarget->est.latched;

			if (pTarget->latched.bHasLatch)
			{
				if (pTarget->est.latched == 1.0)
				{
					// keep track of latch position error from ideal contact position
					pTarget->latched.deltaPos = pTarget->latched.pos - pTarget->est.pos;
					QuaternionSM( -1, pTarget->est.q, pTarget->latched.q, pTarget->latched.deltaQ );
					pTarget->est.q = pTarget->latched.q;
					pTarget->est.pos = pTarget->latched.pos;
				}
				else if (pTarget->est.latched > 0.0)
				{
					// ramp out latch differences during decay phase of rule
					if (latched > 0 && latched < pTarget->latched.influence)
					{
						// latching has decreased
						float dt = pTarget->latched.influence - latched;
						if (pTarget->latched.influence > 0.0)
							dt = dt / pTarget->latched.influence;

						VectorScale( pTarget->latched.deltaPos, (1-dt), pTarget->latched.deltaPos );
						QuaternionScale( pTarget->latched.deltaQ, (1-dt), pTarget->latched.deltaQ );
					}

					// move ideal contact position by latched error factor
					pTarget->est.pos = pTarget->est.pos + pTarget->latched.deltaPos;
					QuaternionMA( pTarget->est.q, 1, pTarget->latched.deltaQ, pTarget->est.q );
					pTarget->latched.q = pTarget->est.q;
					pTarget->latched.pos = pTarget->est.pos;
				}
				else
				{
					pTarget->latched.bHasLatch = false;
					pTarget->latched.q = pTarget->est.q;
					pTarget->latched.pos = pTarget->est.pos;
					pTarget->latched.deltaPos.Init();
					pTarget->latched.deltaQ.Init();
				}
				pTarget->latched.influence = latched;
			}

			// check for illegal requests
			Vector p1, p2, p3;
			MatrixPosition( boneToWorld[pchain->pLink( 0 )->bone], p1 ); // hip
			MatrixPosition( boneToWorld[pchain->pLink( 1 )->bone], p2 ); // knee
			MatrixPosition( boneToWorld[pchain->pLink( 2 )->bone], p3 ); // foot

			float d1 = (p2 - p1).Length();
			float d2 = (p3 - p2).Length();

			if (pTarget->latched.bHasLatch)
			{
				//float d3 = (p3 - p1).Length();
				float d4 = (p3 + pTarget->latched.deltaPos - p1).Length();

				// unstick feet when distance is too great
				if ((d4 < fabs( d1 - d2 ) || d4 * 0.95 > d1 + d2) && pTarget->est.latched > 0.2)
				{
					pTarget->error.flTime = m_flTime;
				}

				// unstick feet when angle is too great
				if (pTarget->est.latched > 0.2)
				{
					float d = fabs( pTarget->latched.deltaQ.w ) * 2.0f - 1.0f; // QuaternionDotProduct( pTarget->latched.q, pTarget->est.q );

					// FIXME: cos(45), make property of chain
					if (d < 0.707)
					{
						pTarget->error.flTime = m_flTime;
					}
				}
			}

			Vector dt = pTarget->est.pos - p1;
			pTarget->trace.hipToFoot = VectorNormalize( dt );
			pTarget->trace.hipToKnee = d1;
			pTarget->trace.kneeToFoot = d2;
			pTarget->trace.hip = p1;
			pTarget->trace.knee = p2;
			pTarget->trace.closest = p1 + dt * (fabs( d1 - d2 ) * 1.01);
			pTarget->trace.farthest = p1 + dt * (d1 + d2) * 0.99;
			pTarget->trace.lowest = p1 + Vector( 0, 0, -1 ) * (d1 + d2) * 0.99;
			// pTarget->trace.endpos = pTarget->est.pos;
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: insert release rules if the ik rules were in error
//-----------------------------------------------------------------------------

void CIKContext::AutoIKRelease( void )
{
	int i;

	for (i = 0; i < m_target.Count(); i++)
	{
		CIKTarget *pTarget = &m_target[i];

		float dt = m_flTime - pTarget->error.flTime;
		if (pTarget->error.bInError || dt < 0.5)
		{
			if (!pTarget->error.bInError)
			{
				pTarget->error.ramp = 0.0; 
				pTarget->error.flErrorTime = pTarget->error.flTime;
				pTarget->error.bInError = true;
			}

			float ft = m_flTime - pTarget->error.flErrorTime;
			if (dt < 0.25)
			{
				pTarget->error.ramp = min( pTarget->error.ramp + ft * 4.0, 1.0 );
			}
			else
			{
				pTarget->error.ramp = max( pTarget->error.ramp - ft * 4.0, 0.0 );
			}
			if (pTarget->error.ramp > 0.0)
			{
				ikcontextikrule_t ikrule;

				ikrule.chain = pTarget->chain;
				ikrule.bone = 0;
				ikrule.type = IK_RELEASE;
				ikrule.slot = i;
				ikrule.flWeight = SimpleSpline( pTarget->error.ramp );
				ikrule.flRuleWeight = 1.0;
				ikrule.latched = dt < 0.25 ? 0.0 : ikrule.flWeight;

				// don't bother with AutoIKRelease if the bone isn't going to be calculated
				// this code is crashing for some unknown reason.
				if ( pTarget->chain >= 0 && pTarget->chain < m_pStudioHdr->numikchains())
				{
					mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( pTarget->chain );
					if (pchain != NULL)
					{
						int bone = pchain->pLink( 2 )->bone;
						if (bone >= 0 && bone < m_pStudioHdr->numbones())
						{
							mstudiobone_t *pBone = m_pStudioHdr->pBone( bone );
							if (pBone != NULL)
							{
								if ( !(m_pStudioHdr->boneFlags( bone ) & m_boneMask))
								{
									pTarget->error.bInError = false;
									continue;
								}
								/*
								char buf[256];
								sprintf( buf, "dt %.4f ft %.4f weight %.4f latched %.4f\n", dt, ft, ikrule.flWeight, ikrule.latched );
								OutputDebugString( buf );
								*/

								int nIndex = m_ikChainRule.Element( ikrule.chain ).AddToTail( );
  								m_ikChainRule.Element( ikrule.chain ).Element( nIndex ) = ikrule;
							}
							else
							{
								DevWarning( 1, "AutoIKRelease (%s) got a NULL pBone %d\n", m_pStudioHdr->pszName(), bone );
							}
						}
						else
						{
							DevWarning( 1, "AutoIKRelease (%s) got an out of range bone %d (%d)\n", m_pStudioHdr->pszName(), bone, m_pStudioHdr->numbones() );
						}
					}
					else
					{
						DevWarning( 1, "AutoIKRelease (%s) got a NULL pchain %d\n", m_pStudioHdr->pszName(), pTarget->chain );
					}
				}
				else
				{
					DevWarning( 1, "AutoIKRelease (%s) got an out of range chain %d (%d)\n", m_pStudioHdr->pszName(), pTarget->chain, m_pStudioHdr->numikchains());
				}
			}
			else
			{
				pTarget->error.bInError = false;
			}
			pTarget->error.flErrorTime = m_flTime;
		}
	}
}



void CIKContext::SolveDependencies( Vector pos[], Quaternion q[], matrix3x4_t boneToWorld[], CBoneBitList &boneComputed	)
{
//	ASSERT_NO_REENTRY();
	
	matrix3x4_t worldTarget;
	int i, j;

	ikchainresult_t chainResult[32]; // allocate!!!

	// init chain rules
	for (i = 0; i < m_pStudioHdr->numikchains(); i++)
	{
		mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( i );
		ikchainresult_t *pChainResult = &chainResult[ i ];
		int bone = pchain->pLink( 2 )->bone;

		pChainResult->target = -1;
		pChainResult->flWeight = 0.0;

		// don't bother with chain if the bone isn't going to be calculated
		if ( !(m_pStudioHdr->boneFlags( bone ) & m_boneMask))
			continue;

		// eval current ik'd bone
		BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );

		MatrixAngles( boneToWorld[bone], pChainResult->q, pChainResult->pos );
	}

	for (j = 0; j < m_ikChainRule.Count(); j++)
	{
		for (i = 0; i < m_ikChainRule.Element( j ).Count(); i++)
		{
			ikcontextikrule_t *pRule = &m_ikChainRule.Element( j ).Element( i );
			ikchainresult_t *pChainResult = &chainResult[ pRule->chain ];
			pChainResult->target = -1;


			switch( pRule->type )
			{
			case IK_SELF:
				{
					// xform IK target error into world space
					matrix3x4_t local;
					QuaternionMatrix( pRule->q, pRule->pos, local );
					// eval target bone space
					if (pRule->bone != -1)
					{
						BuildBoneChain( pos, q, pRule->bone, boneToWorld, boneComputed );
						ConcatTransforms( boneToWorld[pRule->bone], local, worldTarget );
					}
					else
					{
						ConcatTransforms( m_rootxform, local, worldTarget );
					}
			
					float flWeight = pRule->flWeight * pRule->flRuleWeight;
					pChainResult->flWeight = pChainResult->flWeight * (1 - flWeight) + flWeight;

					Vector p2;
					Quaternion q2;
					
					// target p and q
					MatrixAngles( worldTarget, q2, p2 );

					// debugLine( pChainResult->pos, p2, 0, 0, 255, true, 0.1 );

					// blend in position and angles
					pChainResult->pos = pChainResult->pos * (1.0 - flWeight) + p2 * flWeight;
					QuaternionSlerp( pChainResult->q, q2, flWeight, pChainResult->q );
				}
				break;
			case IK_WORLD:
				Assert( 0 );
				break;

			case IK_ATTACHMENT:
				break;

			case IK_GROUND:
				break;

			case IK_RELEASE:
				{
					// move target back towards original location
					float flWeight = pRule->flWeight * pRule->flRuleWeight;
					mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( pRule->chain );
					int bone = pchain->pLink( 2 )->bone;

					Vector p2;
					Quaternion q2;
					
					BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );
					MatrixAngles( boneToWorld[bone], q2, p2 );

					// blend in position and angles
					pChainResult->pos = pChainResult->pos * (1.0 - flWeight) + p2 * flWeight;
					QuaternionSlerp( pChainResult->q, q2, flWeight, pChainResult->q );
				}
				break;
			case IK_UNLATCH:
				{
					/*
					pChainResult->flWeight = pChainResult->flWeight * (1 - pRule->flWeight) + pRule->flWeight;

					pChainResult->pos = pChainResult->pos * (1.0 - pRule->flWeight ) + pChainResult->local.pos * pRule->flWeight;
					QuaternionSlerp( pChainResult->q, pChainResult->local.q, pRule->flWeight, pChainResult->q );
					*/
				}
				break;
			}
		}
	}

	for (i = 0; i < m_target.Count(); i++)
	{
		CIKTarget *pTarget = &m_target[i];

		if (m_target[i].est.flWeight > 0.0)
		{
			matrix3x4_t worldFootpad;
			matrix3x4_t local;
			//mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( m_target[i].chain );
			ikchainresult_t *pChainResult = &chainResult[ pTarget->chain ];

			AngleMatrix(pTarget->offset.q, pTarget->offset.pos, local );

			AngleMatrix( pTarget->est.q, pTarget->est.pos, worldFootpad );

			ConcatTransforms( worldFootpad, local, worldTarget );

			Vector p2;
			Quaternion q2;
			// target p and q
			MatrixAngles( worldTarget, q2, p2 );
			// MatrixAngles( worldTarget, pChainResult->q, pChainResult->pos );

			// blend in position and angles
			pChainResult->flWeight = pTarget->est.flWeight;
			pChainResult->pos = pChainResult->pos * (1.0 - pChainResult->flWeight ) + p2 * pChainResult->flWeight;
			QuaternionSlerp( pChainResult->q, q2, pChainResult->flWeight, pChainResult->q );
		}

		if (pTarget->latched.bNeedsLatch)
		{
			// keep track of latch position
			pTarget->latched.bHasLatch = true;
			pTarget->latched.q = pTarget->est.q;
			pTarget->latched.pos = pTarget->est.pos;
		}
	}

	for (i = 0; i < m_pStudioHdr->numikchains(); i++)
	{
		ikchainresult_t *pChainResult = &chainResult[ i ];
		mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( i );

		if (pChainResult->flWeight > 0.0)
		{
			Vector tmp;
			MatrixPosition( boneToWorld[pchain->pLink( 2 )->bone], tmp );
			// debugLine( pChainResult->pos, tmp, 255, 255, 255, true, 0.1 );

			// do exact IK solution
			// FIXME: once per link!
			if (Studio_SolveIK(pchain, pChainResult->pos, boneToWorld ))
			{
				Vector p3;
				MatrixGetColumn( boneToWorld[pchain->pLink( 2 )->bone], 3, p3 );
				QuaternionMatrix( pChainResult->q, p3, boneToWorld[pchain->pLink( 2 )->bone] );

				// rebuild chain
				// FIXME: is this needed if everyone past this uses the boneToWorld array?
				m_pStudioHdr->SolveBone( pchain->pLink( 2 )->bone, boneToWorld, pos, q );
				m_pStudioHdr->SolveBone( pchain->pLink( 1 )->bone, boneToWorld, pos, q );
				m_pStudioHdr->SolveBone( pchain->pLink( 0 )->bone, boneToWorld, pos, q );
			}
			else
			{
				// FIXME: need to invalidate the targets that forced this...
				if (pChainResult->target != -1)
				{
					CIKTarget *pTarget = &m_target[pChainResult->target];
					VectorScale( pTarget->latched.deltaPos, 0.8, pTarget->latched.deltaPos );
					QuaternionScale( pTarget->latched.deltaQ, 0.8, pTarget->latched.deltaQ );
				}
			}
		}
	}

#if 0
		Vector p1, p2, p3;
		Quaternion q1, q2, q3;

		// current p and q
		MatrixAngles( boneToWorld[bone], q1, p1 );

		
		// target p and q
		MatrixAngles( worldTarget, q2, p2 );

		// blend in position and angles
		p3 = p1 * (1.0 - m_ikRule[i].flWeight ) + p2 * m_ikRule[i].flWeight;

		// do exact IK solution
		// FIXME: once per link!
		Studio_SolveIK(pchain, p3, boneToWorld );

		// force angle (bad?)
		QuaternionSlerp( q1, q2, m_ikRule[i].flWeight, q3 );
		MatrixGetColumn( boneToWorld[bone], 3, p3 );
		QuaternionMatrix( q3, p3, boneToWorld[bone] );

		// rebuild chain
		SolveBone( m_pStudioHdr, pchain->pLink( 2 )->bone, boneToWorld, pos, q );
		SolveBone( m_pStudioHdr, pchain->pLink( 1 )->bone, boneToWorld, pos, q );
		SolveBone( m_pStudioHdr, pchain->pLink( 0 )->bone, boneToWorld, pos, q );
#endif
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CIKContext::SolveAutoplayLocks(
	Vector pos[], 
	Quaternion q[]
	)
{
	matrix3x4_t *boneToWorld = g_MatrixPool.Alloc();
	CBoneBitList boneComputed;
	int i;

	for (i = 0; i < m_ikLock.Count(); i++)
	{
		const mstudioiklock_t &lock = ((IStudioHdr *)m_pStudioHdr)->pIKAutoplayLock( i );
		SolveLock( &lock, i, pos, q, boneToWorld, boneComputed );
	}
	g_MatrixPool.Free( boneToWorld );
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CIKContext::SolveSequenceLocks(
	mstudioseqdesc_t &seqdesc,
	Vector pos[], 
	Quaternion q[]
	)
{
	matrix3x4_t *boneToWorld = g_MatrixPool.Alloc();
	CBoneBitList boneComputed;
	int i;

	for (i = 0; i < m_ikLock.Count(); i++)
	{
		mstudioiklock_t *plock = seqdesc.pIKLock( i );
		SolveLock( plock, i, pos, q, boneToWorld, boneComputed );
	}
	g_MatrixPool.Free( boneToWorld );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CIKContext::AddAllLocks( Vector pos[], Quaternion q[] )
{
	// skip all array access if no autoplay locks.
	if (m_pStudioHdr->GetNumIKChains() == 0)
	{
		return;
	}

	matrix3x4_t *boneToWorld = g_MatrixPool.Alloc();
	CBoneBitList boneComputed;

	int ikOffset = m_ikLock.AddMultipleToTail( m_pStudioHdr->GetNumIKChains() );
	memset( &m_ikLock[ikOffset], 0, sizeof(ikcontextikrule_t)*m_pStudioHdr->GetNumIKChains() );

	for (int i = 0; i < m_pStudioHdr->GetNumIKChains(); i++)
	{
		mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( i );
		int bone = pchain->pLink( 2 )->bone;

		// don't bother with iklock if the bone isn't going to be calculated
		if ( !(m_pStudioHdr->boneFlags( bone ) & m_boneMask))
			continue;

		// eval current ik'd bone
		BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );

		ikcontextikrule_t &ikrule = m_ikLock[ i + ikOffset ];

		ikrule.chain = i;
		ikrule.slot = i;
		ikrule.type = IK_WORLD;

		MatrixAngles( boneToWorld[bone], ikrule.q, ikrule.pos );

		// save off current knee direction
		if (pchain->pLink(0)->kneeDir.LengthSqr() > 0.0)
		{
			Vector tmp = pchain->pLink( 0 )->kneeDir;
			VectorRotate( pchain->pLink( 0 )->kneeDir, boneToWorld[ pchain->pLink( 0 )->bone ], ikrule.kneeDir );
			MatrixPosition( boneToWorld[ pchain->pLink( 1 )->bone ], ikrule.kneePos ); 
		}
		else
		{
			ikrule.kneeDir.Init( );
		}
	}
	g_MatrixPool.Free( boneToWorld );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


void CIKContext::SolveAllLocks(
	Vector pos[], 
	Quaternion q[]
	)
{
	matrix3x4_t *boneToWorld = g_MatrixPool.Alloc();
	CBoneBitList boneComputed;
	int i;

	mstudioiklock_t lock;

	for (i = 0; i < m_ikLock.Count(); i++)
	{
		lock.chain = i;
		lock.flPosWeight = 1.0;
		lock.flLocalQWeight = 0.0;
		lock.flags = 0;

		SolveLock( &lock, i, pos, q, boneToWorld, boneComputed );
	}
	g_MatrixPool.Free( boneToWorld );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


void CIKContext::SolveLock(
	const mstudioiklock_t *plock,
	int i,
	Vector pos[], 
	Quaternion q[],
	matrix3x4_t boneToWorld[], 
	CBoneBitList &boneComputed
	)
{
	mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( plock->chain );
	int bone = pchain->pLink( 2 )->bone;

	// don't bother with iklock if the bone isn't going to be calculated
	if ( !(m_pStudioHdr->boneFlags( bone ) & m_boneMask))
		return;

	// eval current ik'd bone
	BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );

	Vector p1, p2, p3;
	Quaternion q2, q3;

	// current p and q
	MatrixPosition( boneToWorld[bone], p1 );

	// blend in position
	p3 = p1 * (1.0 - plock->flPosWeight ) + m_ikLock[i].pos * plock->flPosWeight;

	// do exact IK solution
	if (m_ikLock[i].kneeDir.LengthSqr() > 0)
	{
		Studio_SolveIK(pchain->pLink( 0 )->bone, pchain->pLink( 1 )->bone, pchain->pLink( 2 )->bone, p3, m_ikLock[i].kneePos, m_ikLock[i].kneeDir, boneToWorld );
	}
	else
	{
		Studio_SolveIK(pchain, p3, boneToWorld );
	}

	// slam orientation
	MatrixPosition( boneToWorld[bone], p3 );
	QuaternionMatrix( m_ikLock[i].q, p3, boneToWorld[bone] );

	// rebuild chain
	q2 = q[ bone ];
	m_pStudioHdr->SolveBone( pchain->pLink( 2 )->bone, boneToWorld, pos, q );
	QuaternionSlerp( q[bone], q2, plock->flLocalQWeight, q[bone] );

	m_pStudioHdr->SolveBone( pchain->pLink( 1 )->bone, boneToWorld, pos, q );
	m_pStudioHdr->SolveBone( pchain->pLink( 0 )->bone, boneToWorld, pos, q );
}


//-----------------------------------------------------------------------------
// Purpose: run all animations that automatically play and are driven off of poseParameters
//-----------------------------------------------------------------------------
void CBoneSetup::CalcAutoplaySequences(
   Vector pos[], 
   Quaternion q[], 
   float flRealTime,
   CIKContext *pIKContext
   )
{
	//	ASSERT_NO_REENTRY();

	int			i;
	if ( pIKContext )
	{
		pIKContext->AddAutoplayLocks( pos, q );
	}

	unsigned short *pList = NULL;
	int count = m_pStudioHdr->GetAutoplayList( &pList );
	for (i = 0; i < count; i++)
	{
		int sequenceIndex = pList[i];
		mstudioseqdesc_t &seqdesc = ((IStudioHdr *)m_pStudioHdr)->pSeqdesc( sequenceIndex );
		if (seqdesc.flags & STUDIO_AUTOPLAY)
		{
			float cycle = 0;
			float cps = m_pStudioHdr->Studio_CPS( seqdesc, sequenceIndex, m_flPoseParameter );
			cycle = flRealTime * cps;
			cycle = cycle - (int)cycle;

			AccumulatePose( pos, q, sequenceIndex, cycle, 1.0, flRealTime, pIKContext );
		}
	}

	if ( pIKContext )
	{
		pIKContext->SolveAutoplayLocks( pos, q );
	}
}


/*
 * This is for DoAimAtBone below, was just for testing, not needed in general
 * but to turn it back on, uncomment this and the section in DoAimAtBone() below
 *

static ConVar aim_constraint( "aim_constraint", "1", FCVAR_REPLICATED, "Toggle <aimconstraint> Helper Bones" );

*/


#pragma warning (disable : 4701)


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static int ClipRayToHitbox( const Ray_t &ray,const mstudiobbox_t *pbox, const matrix3x4_t& matrix, trace_t &tr )
{
	const float flProjEpsilon = 0.01f;
	// scale by current t so hits shorten the ray and increase the likelihood of early outs
	Vector delta2;
	VectorScale( ray.m_Delta, (0.5f * tr.fraction), delta2 );

	// OPTIMIZE: Store this in the box instead of computing it here
	// compute center in local space
	Vector boxextents;
	boxextents.x = (pbox->bbmin.x + pbox->bbmax.x) * 0.5; 
	boxextents.y = (pbox->bbmin.y + pbox->bbmax.y) * 0.5; 
	boxextents.z = (pbox->bbmin.z + pbox->bbmax.z) * 0.5; 
	Vector boxCenter;
	// transform to world space
	VectorTransform( boxextents, matrix, boxCenter );
	// calc extents from local center
	boxextents.x = pbox->bbmax.x - boxextents.x;
	boxextents.y = pbox->bbmax.y - boxextents.y;
	boxextents.z = pbox->bbmax.z - boxextents.z;
	// OPTIMIZE: This is optimized for world space.  If the transform is fast enough, it may make more
	// sense to just xform and call UTIL_ClipToBox() instead.  MEASURE THIS.

	// save the extents of the ray along 
	Vector extent, uextent;
	Vector segmentCenter;
	segmentCenter.x = ray.m_Start.x + delta2.x - boxCenter.x;
	segmentCenter.y = ray.m_Start.y + delta2.y - boxCenter.y;
	segmentCenter.z = ray.m_Start.z + delta2.z - boxCenter.z;

	extent.Init();

	// check box axes for separation
	for ( int j = 0; j < 3; j++ )
	{
		extent[j] = delta2.x * matrix[0][j] + delta2.y * matrix[1][j] +	delta2.z * matrix[2][j];
		uextent[j] = fabsf(extent[j]);
		float coord = segmentCenter.x * matrix[0][j] + segmentCenter.y * matrix[1][j] +	segmentCenter.z * matrix[2][j];
		coord = fabsf(coord);

		if ( coord > (boxextents[j] + uextent[j]) )
			return -1;
	}

	// now check cross axes for separation
	float tmp, tmpfix, cextent;
	Vector cross;
	CrossProduct( delta2, segmentCenter, cross );
	cextent = cross.x * matrix[0][0] + cross.y * matrix[1][0] + cross.z * matrix[2][0];
	cextent = fabsf(cextent);
	tmp = boxextents[1]*uextent[2] + boxextents[2]*uextent[1];
	tmpfix = MAX(tmp, flProjEpsilon);
	if ( cextent > tmpfix )
		return -1;
	
//	if ( cextent > tmp && cextent <= tmpfix )
//		DevWarning( "ClipRayToHitbox trace precision error case\n" );

	cextent = cross.x * matrix[0][1] + cross.y * matrix[1][1] + cross.z * matrix[2][1];
	cextent = fabsf(cextent);
	tmp = boxextents[0]*uextent[2] + boxextents[2]*uextent[0];
	tmpfix = MAX(tmp, flProjEpsilon);
	if ( cextent > tmpfix )
		return -1;

//	if ( cextent > tmp && cextent <= tmpfix )
//		DevWarning( "ClipRayToHitbox trace precision error case\n" );

	cextent = cross.x * matrix[0][2] + cross.y * matrix[1][2] + cross.z * matrix[2][2];
	cextent = fabsf(cextent);
	tmp = boxextents[0]*uextent[1] + boxextents[1]*uextent[0];
	tmpfix = MAX(tmp, flProjEpsilon);
	if ( cextent > tmpfix )
		return -1;

//	if ( cextent > tmp && cextent <= tmpfix )
//		DevWarning( "ClipRayToHitbox trace precision error case\n" );

	// !!! We hit this box !!! compute intersection point and return
	Vector start;

	// Compute ray start in bone space
	VectorITransform( ray.m_Start, matrix, start );
	// extent is delta2 in bone space, recompute delta in bone space
	VectorScale( extent, 2, extent );

	// delta was prescaled by the current t, so no need to see if this intersection
	// is closer
	trace_t boxTrace;
	if ( !IntersectRayWithBox( start, extent, pbox->bbmin, pbox->bbmax, 0.0f, &boxTrace ) )
		return -1;

	Assert( IsFinite(boxTrace.fraction) );
	tr.fraction *= boxTrace.fraction;
	tr.startsolid = boxTrace.startsolid;
	int hitside = boxTrace.plane.type;
	if ( boxTrace.plane.normal[hitside] >= 0 )
	{
		hitside += 3;
	}
	return hitside;
}

#pragma warning (default : 4701)


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool SweepBoxToStudio( IPhysicsSurfaceProps *pProps, const Ray_t& ray, IStudioHdr *pStudioHdr, mstudiohitboxset_t *set, 
				   const matrix3x4_t **hitboxbones, int fContentsMask, trace_t &tr )
{
	tr.fraction = 1.0;
	tr.startsolid = false;

	// OPTIMIZE: Partition these?
	Ray_t clippedRay = ray;
	int hitbox = -1;
	for ( int i = 0; i < set->numhitboxes; i++ )
	{
		mstudiobbox_t *pbox = set->pHitbox(i);

		// Filter based on contents mask
		int fBoneContents = pStudioHdr->pBone( pbox->bone )->contents;
		if ( ( fBoneContents & fContentsMask ) == 0 )
			continue;
		
		//FIXME: Won't work with scaling!
		trace_t obbTrace;
		if ( IntersectRayWithOBB( clippedRay, *hitboxbones[pbox->bone], pbox->bbmin, pbox->bbmax, 0.0f, &obbTrace ) )
		{
			tr.startpos = obbTrace.startpos;
			tr.endpos = obbTrace.endpos;
			tr.plane = obbTrace.plane;
			tr.startsolid = obbTrace.startsolid;
			tr.allsolid = obbTrace.allsolid;

			// This logic here is to shorten the ray each time to get more early outs
			tr.fraction *= obbTrace.fraction;
			clippedRay.m_Delta *= obbTrace.fraction;
			hitbox = i;
			if (tr.startsolid)
				break;
		}
	}

	if ( hitbox >= 0 )
	{
		tr.hitgroup = set->pHitbox(hitbox)->group;
		tr.hitbox = hitbox;
		const mstudiobone_t *pBone = pStudioHdr->pBone( set->pHitbox(hitbox)->bone );
		tr.contents = pBone->contents | CONTENTS_HITBOX;
		tr.physicsbone = pBone->physicsbone;
		tr.surface.name = "**studio**";
		tr.surface.flags = SURF_HITBOX;
		tr.surface.surfaceProps = pProps->GetSurfaceIndex( pBone->pszSurfaceProp() );

		Assert( tr.physicsbone >= 0 );
		return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool TraceToStudio( IPhysicsSurfaceProps *pProps, const Ray_t& ray, IStudioHdr *pStudioHdr, mstudiohitboxset_t *set, 
				   const matrix3x4_t **hitboxbones, int fContentsMask, const Vector &vecOrigin, float flScale, trace_t &tr )
{
	if ( !ray.m_IsRay )
	{
		return SweepBoxToStudio( pProps, ray, pStudioHdr, set, hitboxbones, fContentsMask, tr );
	}

	tr.fraction = 1.0;
	tr.startsolid = false;

	// no hit yet
	int hitbox = -1;
	int hitside = -1;

	// OPTIMIZE: Partition these?
	for ( int i = 0; i < set->numhitboxes; i++ )
	{
		mstudiobbox_t *pbox = set->pHitbox(i);

		// Filter based on contents mask
		int fBoneContents = pStudioHdr->pBone( pbox->bone )->contents;
		if ( ( fBoneContents & fContentsMask ) == 0 )
			continue;
		
		// columns are axes of the bones in world space, translation is in world space
		const matrix3x4_t& matrix = *hitboxbones[pbox->bone];
		
		// Because we're sending in a matrix with scale data, and because the matrix inversion in the hitbox
		// code does not handle that case, we pre-scale the bones and ray down here and do our collision checks
		// in unscaled space.  We can then rescale the results afterwards.

		int side = -1;
		if ( flScale < 1.0f-FLT_EPSILON || flScale > 1.0f+FLT_EPSILON )
		{
			matrix3x4_t matScaled;
			MatrixCopy( matrix, matScaled );
			
			float invScale = 1.0f / flScale;

			Vector vecBoneOrigin;
			MatrixGetColumn( matScaled, 3, vecBoneOrigin );
			
			// Pre-scale the origin down
			Vector vecNewOrigin = vecBoneOrigin - vecOrigin;
			vecNewOrigin *= invScale;
			vecNewOrigin += vecOrigin;
			MatrixSetColumn( vecNewOrigin, 3, matScaled );

			// Scale it uniformly
			VectorScale( matScaled[0], invScale, matScaled[0] );
			VectorScale( matScaled[1], invScale, matScaled[1] );
			VectorScale( matScaled[2], invScale, matScaled[2] );
			
			// Pre-scale our ray as well
			Vector vecRayStart = ray.m_Start - vecOrigin;
			vecRayStart *= invScale;
			vecRayStart += vecOrigin;
			
			Vector vecRayDelta = ray.m_Delta * invScale;

			Ray_t newRay;
			newRay.Init( vecRayStart, vecRayStart + vecRayDelta );  
			
			side = ClipRayToHitbox( newRay, pbox, matScaled, tr );
		}
		else
		{
			side = ClipRayToHitbox( ray, pbox, matrix, tr );
		}

		if ( side >= 0 )
		{
			hitbox = i;
			hitside = side;
		}
	}

	if ( hitbox >= 0 )
	{
		mstudiobbox_t *pbox = set->pHitbox(hitbox);
		VectorMA( ray.m_Start, tr.fraction, ray.m_Delta, tr.endpos );
		tr.hitgroup = set->pHitbox(hitbox)->group;
		tr.hitbox = hitbox;
		const mstudiobone_t *pBone = pStudioHdr->pBone( pbox->bone );
		tr.contents = pBone->contents | CONTENTS_HITBOX;
		tr.physicsbone = pBone->physicsbone;
		tr.surface.name = "**studio**";
		tr.surface.flags = SURF_HITBOX;
		tr.surface.surfaceProps = pProps->GetSurfaceIndex( pBone->pszSurfaceProp() );

		Assert( tr.physicsbone >= 0 );
		const matrix3x4_t& matrix = *hitboxbones[pbox->bone];
		if ( hitside >= 3 )
		{
			hitside -= 3;
			tr.plane.normal[0] = matrix[0][hitside];
			tr.plane.normal[1] = matrix[1][hitside];
			tr.plane.normal[2] = matrix[2][hitside];
			//tr.plane.dist = DotProduct( tr.plane.normal, Vector(matrix[0][3], matrix[1][3], matrix[2][3] ) ) + pbox->bbmax[hitside];
		}
		else
		{
			tr.plane.normal[0] = -matrix[0][hitside];
			tr.plane.normal[1] = -matrix[1][hitside];
			tr.plane.normal[2] = -matrix[2][hitside];
			//tr.plane.dist = DotProduct( tr.plane.normal, Vector(matrix[0][3], matrix[1][3], matrix[2][3] ) ) - pbox->bbmin[hitside];
		}
		// simpler plane constant equation
		tr.plane.dist = DotProduct( tr.endpos, tr.plane.normal );
		tr.plane.type = 3;
		return true;
	}
	return false;
}

