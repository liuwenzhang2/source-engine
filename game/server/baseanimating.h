//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef BASEANIMATING_H
#define BASEANIMATING_H
#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"
#include "entityoutput.h"
#include "studio.h"
#include "datacache/idatacache.h"
#include "tier0/threadtools.h"
#include "ai_activity.h"
//#include <entitylist.h>


struct animevent_t;
struct matrix3x4_t;
class CIKContext;
class KeyValues;
class CRagdollProp;
FORWARD_DECLARE_HANDLE( memhandle_t );

#define	BCF_NO_ANIMATION_SKIP	( 1 << 0 )	// Do not allow PVS animation skipping (mostly for attachments being critical to an entity)
#define	BCF_IS_IN_SPAWN			( 1 << 1 )	// Is currently inside of spawn, always evaluate animations

class CBaseAnimating : public CBaseEntity
{
public:
	DECLARE_CLASS( CBaseAnimating, CBaseEntity );

	CBaseAnimating();
	~CBaseAnimating();

	DECLARE_PREDICTABLE();

	

	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();
	virtual void PostConstructor(const char* szClassname, int iForceEdictIndex);
	virtual void UpdateOnRemove(void);
	virtual void SetModel( const char *szModelName );
	virtual void Activate();
	virtual void Spawn();
	virtual void Precache();
	virtual void SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways );

	virtual int	 Restore( IRestore &restore );
	virtual void OnRestore();



	virtual IStudioHdr *OnNewModel();

	virtual CBaseAnimating*	GetBaseAnimating() { return this; }



	float	GetAnimTimeInterval( void ) const;

	// Basic NPC Animation functions
	virtual float	GetIdealSpeed( ) const;
	virtual float	GetIdealAccel( ) const;
	virtual void	StudioFrameAdvance(); // advance animation frame to some time in the future
	void StudioFrameAdvanceManual( float flInterval );
	bool	IsValidSequence( int iSequence );




	// FIXME: push transitions support down into CBaseAnimating?
	//virtual bool IsActivityFinished( void ) { return m_bSequenceFinished; }
	inline bool	 IsSequenceLooping( int iSequence ) { return GetEngineObject()->GetModelPtr()->IsSequenceLooping(iSequence); }


	float	GetLastVisibleCycle( IStudioHdr *pStudioHdr, int iSequence );

	void	ResetActivityIndexes ( void );
	void    ResetEventIndexes ( void );

	int		LookupActivity( const char *label );
	KeyValues *GetSequenceKeyValues( int iSequence );


	const char *GetSequenceName( int iSequence );
	const char *GetSequenceActivityName( int iSequence );
	Activity GetSequenceActivity( int iSequence );

	// This will stop animation until you call ResetSequenceInfo() at some point in the future
	inline void StopAnimation( void ) { GetEngineObject()->SetPlaybackRate(0); }
	virtual CRagdollProp* CreateRagdollProp();
	virtual CBaseEntity* CreateServerRagdoll(int forceBone, const CTakeDamageInfo& info, int collisionGroup, bool bUseLRURetirement = false);
	virtual void ClampRagdollForce( const Vector &vecForceIn, Vector *vecForceOut ) { *vecForceOut = vecForceIn; } // Base class does nothing.
	//virtual bool BecomeRagdollOnClient( const Vector &force );
	virtual bool CanBecomeRagdoll( void ); //Check if this entity will ragdoll when dead.
	virtual void FixupBurningServerRagdoll(CBaseEntity* pRagdoll) {}
	virtual	void GetSkeleton( IStudioHdr *pStudioHdr, Vector pos[], Quaternion q[], int boneMask );

	virtual void CalculateIKLocks( float currentTime );
	virtual void Teleport( const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity );

	bool HasAnimEvent( int nSequence, int nEvent );
	virtual	void DispatchAnimEvents ( CBaseAnimating *eventHandler ); // Handle events that have happend since last time called up until X seconds into the future
	virtual void HandleAnimEvent( animevent_t *pEvent );


	bool	HasPoseParameter( int iSequence, const char *szName );
	bool	HasPoseParameter( int iSequence, int iParameter );
	float	EdgeLimitPoseParameter( int iParameter, float flValue, float flBase = 0.0f );

protected:
	// The modus operandi for pose parameters is that you should not use the const char * version of the functions
	// in general code -- it causes many many string comparisons, which is slower than you think. Better is to 
	// save off your pose parameters in member variables in your derivation of this function:
	virtual void	PopulatePoseParameters( void );


public:



	int  FindTransitionSequence( int iCurrentSequence, int iGoalSequence, int *piDir );
	bool GotoSequence( int iCurrentSequence, float flCurrentCycle, float flCurrentRate,  int iGoalSequence, int &iNextSequence, float &flCycle, int &iDir );
	int  GetEntryNode( int iSequence );
	int  GetExitNode( int iSequence );
	
	void GetEyeballs( Vector &origin, QAngle &angles ); // ?? remove ??


	// These return the attachment in world space
	//bool GetAttachment( const char *szName, Vector &absOrigin, QAngle &absAngles );

	// These return the attachment in the space of the entity
	bool GetAttachmentLocal( const char *szName, Vector &origin, QAngle &angles );
	bool GetAttachmentLocal( int iAttachment, Vector &origin, QAngle &angles );
	bool GetAttachmentLocal( int iAttachment, matrix3x4_t &attachmentToLocal );
	
	

	void SetBodygroup( int iGroup, int iValue );
	int GetBodygroup( int iGroup );

	const char *GetBodygroupName( int iGroup );
	int FindBodygroupByName( const char *name );
	int GetBodygroupCount( int iGroup );
	int GetNumBodyGroups( void );

	void					SetHitboxSet( int setnum );
	void					SetHitboxSetByName( const char *setname );
	int						GetHitboxSet( void );
	char const				*GetHitboxSetName( void );
	int						GetHitboxSetCount( void );
	int						GetHitboxBone( int hitboxIndex );
	bool					LookupHitbox( const char *szName, int& outSet, int& outBox );


	
	// Clone a CBaseAnimating from another (copies model & sequence data)
	void CopyAnimationDataFrom( CBaseAnimating *pSource );

	int ExtractBbox( int sequence, Vector& mins, Vector& maxs );
	void SetSequenceBox( void );
	int RegisterPrivateActivity( const char *pszActivityName );


// Controllers.
	virtual	void			InitBoneControllers ( void );
	

	
	void					GetVelocity(Vector *vVelocity, AngularImpulse *vAngVelocity);

	// these two need to move somewhere else
	LocalFlexController_t GetNumFlexControllers( void );
	const char *GetFlexDescFacs( int iFlexDesc );
	const char *GetFlexControllerName( LocalFlexController_t iFlexController );
	const char *GetFlexControllerType( LocalFlexController_t iFlexController );

	virtual	Vector GetGroundSpeedVelocity( void );



	void ReportMissingActivity( int iActivity );
	virtual bool TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );
	virtual bool TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );
	
	virtual int DrawDebugTextOverlays( void );
	
	// See note in code re: bandwidth usage!!!
	void				DrawServerHitboxes( float duration = 0.0f, bool monocolor = false );
	
	

	// for ragdoll vs. car
	int GetHitboxesFrontside( int *boxList, int boxMax, const Vector &normal, float dist );

	void	GetInputDispatchEffectPosition( const char *sInputString, Vector &pOrigin, QAngle &pAngles );

	//virtual void	ModifyOrAppendCriteria( AI_CriteriaSet& set );



	void InputBecomeRagdoll(inputdata_t& inputdata);

	bool PrefetchSequence( int iSequence );
	bool CanSkipAnimation(void);

private:


	void StudioFrameAdvanceInternal( IStudioHdr *pStudioHdr, float flInterval );

	void InputSetModelScale( inputdata_t &inputdata );

	void OnResetSequence(int nSequence);
public:






public:
	



  	

public:
	//Vector	GetStepOrigin( void ) const;
	//QAngle	GetStepAngles( void ) const;

private:
	//bool				m_bResetSequenceInfoOnLoad; // true if a ResetSequenceInfo was queued up during dynamic load









	

protected:


private:


// FIXME: necessary so that cyclers can hack m_bSequenceFinished
friend class CFlexCycler;
friend class CCycler;
friend class CBlendingCycler;
};

//-----------------------------------------------------------------------------
// Purpose: Serves the 90% case of calling SetSequence / ResetSequenceInfo.
//-----------------------------------------------------------------------------

/*
inline void CBaseAnimating::ResetSequence(int nSequence)
{
	m_nSequence = nSequence;
	ResetSequenceInfo();
}
*/

EXTERN_SEND_TABLE(DT_BaseAnimating);

#endif // BASEANIMATING_H
