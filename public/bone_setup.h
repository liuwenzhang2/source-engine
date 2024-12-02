//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BONE_SETUP_H
#define BONE_SETUP_H
#ifdef _WIN32
#pragma once
#endif


#include "studio.h"
#include "cmodel.h"
#include "bitvec.h"


class CBoneToWorld;
class CIKContext;
class IPoseDebugger;


// This provides access to networked arrays, so if this code actually changes a value, 
// the entity is marked as changed.
abstract_class IParameterAccess
{
public:
	virtual float GetParameter( int iParam ) = 0;
	virtual void SetParameter( int iParam, float flValue ) = 0;
};


class CBoneSetup;
class IBoneSetup
{
public:
	IBoneSetup( const IStudioHdr *pStudioHdr, int boneMask, const float poseParameter[], IPoseDebugger *pPoseDebugger = NULL );
	~IBoneSetup( void );
	void InitPose( Vector pos[], Quaternion[] );
	void AccumulatePose( Vector pos[], Quaternion q[], int sequence, float cycle, float flWeight, float flTime, CIKContext *pIKContext );
	void CalcAutoplaySequences(	Vector pos[], Quaternion q[], float flRealTime, CIKContext *pIKContext );
	void CalcBoneAdj( Vector pos[], Quaternion q[], const float controllers[] );
	IStudioHdr *GetStudioHdr();
private:
	CBoneSetup *m_pBoneSetup;
};

// Given two samples of a bone separated in time by dt, 
// compute the velocity and angular velocity of that bone
void CalcBoneDerivatives( Vector &velocity, AngularImpulse &angVel, const matrix3x4_t &prev, const matrix3x4_t &current, float dt );
// Give a derivative of a bone, compute the velocity & angular velocity of that bone
void CalcBoneVelocityFromDerivative( const QAngle &vecAngles, Vector &velocity, AngularImpulse &angVel, const matrix3x4_t &current );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

// ik info
class CIKTarget
{
public:
	void SetOwner( int entindex, const Vector &pos, const QAngle &angles );
	void ClearOwner( void );
	int GetOwner( void );
	void UpdateOwner( int entindex, const Vector &pos, const QAngle &angles );
	void SetPos( const Vector &pos );
	void SetAngles( const QAngle &angles );
	void SetQuaternion( const Quaternion &q );
	void SetNormal( const Vector &normal );
	void SetPosWithNormalOffset( const Vector &pos, const Vector &normal );
	void SetOnWorld( bool bOnWorld = true );

	bool IsActive( void );
	void IKFailed( void );
	int chain;
	int type;
	void MoveReferenceFrame( Vector &deltaPos, QAngle &deltaAngles );
	// accumulated offset from ideal footplant location
public:
	struct x2 {
		char		*pAttachmentName;
		Vector		pos;
		Quaternion	q;
	} offset;
private:
	struct x3 {
		Vector		pos;
		Quaternion	q;
	} ideal;
public:
	struct x4 {
		float		latched;
		float		release;
		float		height;
		float		floor;
		float		radius;
		float		flTime;
		float		flWeight;
		Vector		pos;
		Quaternion	q;
		bool		onWorld;
	} est; // estimate contact position
	struct x5 {
		float		hipToFoot;	// distance from hip
		float		hipToKnee;	// distance from hip to knee
		float		kneeToFoot;	// distance from knee to foot
		Vector		hip;		// location of hip
		Vector		closest;	// closest valid location from hip to foot that the foot can move to
		Vector		knee;		// pre-ik location of knee
		Vector		farthest;	// farthest valid location from hip to foot that the foot can move to
		Vector		lowest;		// lowest position directly below hip that the foot can drop to
	} trace;
private:
	// internally latched footset, position
	struct x1 {
		// matrix3x4_t		worldTarget;
		bool		bNeedsLatch;
		bool		bHasLatch;
		float		influence;
		int			iFramecounter;
		int			owner;
		Vector		absOrigin;
		QAngle		absAngles;
		Vector		pos;
		Quaternion	q;
		Vector		deltaPos;	// acculated error
		Quaternion	deltaQ;
		Vector		debouncePos;
		Quaternion	debounceQ;
	} latched;
	struct x6 {
		float		flTime; // time last error was detected
		float		flErrorTime;
		float		ramp;
		bool		bInError;
	} error;

	friend class CIKContext;
};


struct ikchainresult_t
{
	// accumulated offset from ideal footplant location
	int			target;
	Vector		pos;
	Quaternion	q;
	float		flWeight;
};


void Studio_AlignIKMatrix( matrix3x4_t &mMat, const Vector &vAlignTo );

bool Studio_SolveIK( int iThigh, int iKnee, int iFoot, Vector &targetFoot, matrix3x4_t* pBoneToWorld );

bool Studio_SolveIK( int iThigh, int iKnee, int iFoot, Vector &targetFoot, Vector &targetKneePos, Vector &targetKneeDir, matrix3x4_t* pBoneToWorld );



class CIKContext 
{
public:
	CIKContext( );
	void Init( const IStudioHdr *pStudioHdr, const QAngle &angles, const Vector &pos, float flTime, int iFramecounter, int boneMask );
	void AddDependencies(  mstudioseqdesc_t &seqdesc, int iSequence, float flCycle, const float poseParameters[], float flWeight = 1.0f );

	void ClearTargets( void );
	void UpdateTargets( Vector pos[], Quaternion q[], matrix3x4_t boneToWorld[], CBoneBitList &boneComputed );
	void AutoIKRelease( void );
	void SolveDependencies( Vector pos[], Quaternion q[], matrix3x4_t boneToWorld[], CBoneBitList &boneComputed );

	void AddAutoplayLocks( Vector pos[], Quaternion q[] );
	void SolveAutoplayLocks( Vector pos[], Quaternion q[] );

	void AddSequenceLocks( mstudioseqdesc_t &SeqDesc, Vector pos[], Quaternion q[] );
	void SolveSequenceLocks( mstudioseqdesc_t &SeqDesc, Vector pos[], 	Quaternion q[] );
	
	void AddAllLocks( Vector pos[], Quaternion q[] );
	void SolveAllLocks( Vector pos[], Quaternion q[] );

	void SolveLock( const mstudioiklock_t *plock, int i, Vector pos[], Quaternion q[], matrix3x4_t boneToWorld[], CBoneBitList &boneComputed );

	CUtlVectorFixed< CIKTarget, 12 >	m_target;

private:

	IStudioHdr const *m_pStudioHdr;

	bool Estimate( int iSequence, float flCycle, int iTarget, const float poseParameter[], float flWeight = 1.0f ); 
	void BuildBoneChain( const Vector pos[], const Quaternion q[], int iBone, matrix3x4_t *pBoneToWorld, CBoneBitList &boneComputed );

	// virtual IK rules, filtered and combined from each sequence
	CUtlVector< CUtlVector< ikcontextikrule_t > > m_ikChainRule;
	CUtlVector< ikcontextikrule_t > m_ikLock;
	matrix3x4_t m_rootxform;

	int m_iFramecounter;
	float m_flTime;
	int m_boneMask;
};


//FORWARD_DECLARE_HANDLE( memhandle_t );
//struct bonecacheparams_t
//{
//	IStudioHdr		*pStudioHdr;
//	matrix3x4_t		*pBoneToWorld;
//	float			curtime;
//	int				boneMask;
//};
//
//class CBoneCache
//{
//public:
//
//	// you must implement these static functions for the ResourceManager
//	// -----------------------------------------------------------
//	static CBoneCache *CreateResource( const bonecacheparams_t &params );
//	static unsigned int EstimatedSize( const bonecacheparams_t &params );
//	// -----------------------------------------------------------
//	// member functions that must be present for the ResourceManager
//	void			DestroyResource();
//	CBoneCache		*GetData() { return this; }
//	unsigned int	Size() { return m_size; }
//	// -----------------------------------------------------------
//
//					CBoneCache();
//
//	// was constructor, but placement new is messy wrt memdebug - so cast & init instead
//	void			Init( const bonecacheparams_t &params, unsigned int size, short *pStudioToCached, short *pCachedToStudio, int cachedBoneCount );
//	
//	void			UpdateBones( const matrix3x4_t *pBoneToWorld, int numbones, float curtime );
//	matrix3x4_t		*GetCachedBone( int studioIndex );
//	void			ReadCachedBones( matrix3x4_t *pBoneToWorld );
//	void			ReadCachedBonePointers(const matrix3x4_t **bones, int numbones );
//
//	bool			IsValid( float curtime, float dt = 0.1f );
//
//public:
//	float			m_timeValid;
//	int				m_boneMask;
//
//private:
//	matrix3x4_t		*BoneArray();
//	short			*StudioToCached();
//	short			*CachedToStudio();
//
//	unsigned int	m_size;
//	unsigned short	m_cachedBoneCount;
//	unsigned short	m_matrixOffset;
//	unsigned short	m_cachedToStudioOffset;
//	unsigned short	m_boneOutOffset;
//};
//
//CBoneCache *Studio_GetBoneCache( memhandle_t cacheHandle );
//memhandle_t Studio_CreateBoneCache( bonecacheparams_t &params );
//void Studio_DestroyBoneCache( memhandle_t cacheHandle );
//void Studio_InvalidateBoneCache( memhandle_t cacheHandle );
//
//// Given a ray, trace for an intersection with this studiomodel.  Get the array of bones from StudioSetupHitboxBones
bool TraceToStudio( class IPhysicsSurfaceProps *pProps, const Ray_t& ray, IStudioHdr *pStudioHdr, mstudiohitboxset_t *set, const matrix3x4_t **hitboxbones, int fContentsMask, const Vector &vecOrigin, float flScale, trace_t &trace );

#endif // BONE_SETUP_H
