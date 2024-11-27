//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $NoKeywords: $
//=============================================================================//
#ifndef C_BASEANIMATING_H
#define C_BASEANIMATING_H

#ifdef _WIN32
#pragma once
#endif

#include "c_baseentity.h"
#include "studio.h"
#include "utlvector.h"
//#include "ragdoll.h"
#include "mouthinfo.h"
// Shared activities
#include "ai_activity.h"
#include "animationlayer.h"
#include "sequence_Transitioner.h"
#include "bone_accessor.h"
#include "bone_merge_cache.h"
#include "ragdoll_shared.h"
#include "tier0/threadtools.h"
#include "datacache/idatacache.h"

#define LIPSYNC_POSEPARAM_NAME "mouth"
#define NUM_HITBOX_FIRES	10

/*
class C_BaseClientShader
{
	virtual void RenderMaterial( C_BaseEntity *pEntity, int count, const vec4_t *verts, const vec4_t *normals, const vec2_t *texcoords, vec4_t *lightvalues );
};
*/

//class IRagdoll;
class CIKContext;
class CIKState;
class ConVar;
class C_RopeKeyframe;
class CBoneBitList;
class CBoneList;
class KeyValues;
class CJiggleBones;
class IBoneSetup;
FORWARD_DECLARE_HANDLE( memhandle_t );
typedef unsigned short MDLHandle_t;

extern ConVar vcollide_wireframe;




struct RagdollInfo_t
{
	bool		m_bActive;
	float		m_flSaveTime;
	int			m_nNumBones;
	Vector		m_rgBonePos[MAXSTUDIOBONES];
	Quaternion	m_rgBoneQuaternion[MAXSTUDIOBONES];
};


class CAttachmentData
{
public:
	matrix3x4_t	m_AttachmentToWorld;
	QAngle	m_angRotation;
	Vector	m_vOriginVelocity;
	int		m_nLastFramecount : 31;
	int		m_bAnglesComputed : 1;
};


typedef unsigned int			ClientSideAnimationListHandle_t;

#define		INVALID_CLIENTSIDEANIMATION_LIST_HANDLE	(ClientSideAnimationListHandle_t)~0


class C_BaseAnimating : public C_BaseEntity//, private IModelLoadCallback
{
public:
	DECLARE_CLASS( C_BaseAnimating, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_INTERPOLATION();



	C_BaseAnimating();
	~C_BaseAnimating();

	bool Init(int entnum, int iSerialNum);
	void UpdateOnRemove(void);

	virtual C_BaseAnimating*		GetBaseAnimating() { return this; }

	bool UsesPowerOfTwoFrameBufferTexture( void );

	virtual bool	Interpolate( float currentTime );
	virtual void	Simulate();	
	virtual void	Release();	

	float	GetAnimTimeInterval( void ) const;




	LocalFlexController_t GetNumFlexControllers( void );
	const char *GetFlexDescFacs( int iFlexDesc );
	const char *GetFlexControllerName( LocalFlexController_t iFlexController );
	const char *GetFlexControllerType( LocalFlexController_t iFlexController );

	virtual void	GetAimEntOrigin( IClientEntity *pAttachedTo, Vector *pAbsOrigin, QAngle *pAbsAngles );

	// Computes a box that surrounds all hitboxes
	bool ComputeHitboxSurroundingBox( Vector *pVecWorldMins, Vector *pVecWorldMaxs );
	bool ComputeEntitySpaceHitboxSurroundingBox( Vector *pVecWorldMins, Vector *pVecWorldMaxs );

	// Gets the hitbox-to-world transforms, returns false if there was a problem
	bool HitboxToWorldTransforms( matrix3x4_t *pHitboxToWorld[MAXSTUDIOBONES] );

	// base model functionality
	float		  ClampCycle( float cycle, bool isLooping );

	virtual void ApplyBoneMatrixTransform( matrix3x4_t& transform );

	
	virtual void UpdateIKLocks( float currentTime );
	virtual void CalculateIKLocks( float currentTime );
	virtual bool ShouldDraw();
	virtual int DrawModel( int flags );
	virtual int	InternalDrawModel( int flags );

	void		DoInternalDrawModel( ClientModelRenderInfo_t *pInfo, DrawModelState_t *pState, matrix3x4_t *pBoneToWorldArray = NULL );

	//
	
	// override in sub-classes
	virtual void DoAnimationEvents( IStudioHdr *pStudio );
	virtual void FireEvent( const Vector& origin, const QAngle& angles, int event, const char *options );
	virtual void FireObsoleteEvent( const Vector& origin, const QAngle& angles, int event, const char *options );
	virtual const char* ModifyEventParticles( const char* token ) { return token; }

	// Parses and distributes muzzle flash events
	virtual bool DispatchMuzzleEffect( const char *options, bool isFirstPerson );

	// virtual	void AllocateMaterials( void );
	// virtual	void FreeMaterials( void );

	virtual void ValidateModelIndex( void );
	virtual IStudioHdr *OnNewModel( void );

	
	virtual void SetPredictable( bool state );

	// C_BaseClientShader **p_ClientShaders;

	virtual	void StandardBlendingRules( IStudioHdr *pStudioHdr, Vector pos[], Quaternion q[], float currentTime, int boneMask );

	void MaintainSequenceTransitions( IBoneSetup &boneSetup, float flCycle, Vector pos[], Quaternion q[] );
	virtual void AccumulateLayers( IBoneSetup &boneSetup, Vector pos[], Quaternion q[], float currentTime );

	//virtual void ChildLayerBlend( Vector pos[], Quaternion q[], float currentTime, int boneMask );

	// Attachments
	int		LookupAttachment( const char *pAttachmentName );
	int		LookupRandomAttachment( const char *pAttachmentNameSubstring );


	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Finds the bone associated with the given hitbox
	//=============================================================================

	int		GetHitboxBone( int hitboxIndex );

	//=============================================================================
	// HPE_END
	//=============================================================================

	// Bone attachments
	virtual void		AttachEntityToBone( C_BaseAnimating* attachTarget, int boneIndexAttached=-1, Vector bonePosition=Vector(0,0,0), QAngle boneAngles=QAngle(0,0,0) );
	void				AddBoneAttachment( C_BaseAnimating* newBoneAttachment );
	void				RemoveBoneAttachment( C_BaseAnimating* boneAttachment );
	void				RemoveBoneAttachments();
	void				DestroyBoneAttachments();
	void				MoveBoneAttachments( C_BaseAnimating* attachTarget );
	int					GetNumBoneAttachments();
	C_BaseAnimating*	GetBoneAttachment( int i );
	virtual void		NotifyBoneAttached( C_BaseAnimating* attachTarget );
	virtual void		UpdateBoneAttachments( void );

	//bool solveIK(float a, float b, const Vector &Foot, const Vector &Knee1, Vector &Knee2);
	//void DebugIK( mstudioikchain_t *pikchain );

	virtual void					PreDataUpdate( DataUpdateType_t updateType );
	virtual void					PostDataUpdate( DataUpdateType_t updateType );
	//virtual int						RestoreData( const char *context, int slot, int type );
	virtual void					OnPostRestoreData();

	virtual void					NotifyShouldTransmit( ShouldTransmitState_t state );
	virtual void					OnPreDataChanged( DataUpdateType_t updateType );
	virtual void					OnDataChanged( DataUpdateType_t updateType );
	virtual void					AddEntity( void );

	// This can be used to force client side animation to be on. Only use if you know what you're doing!
	// Normally, the server entity should set this.
	void							ForceClientSideAnimationOn();
	
	void							AddToClientSideAnimationList();
	void							RemoveFromClientSideAnimationList();

	virtual bool					IsSelfAnimating();
	virtual void					ResetLatched();

	// implements these so ragdolls can handle frustum culling & leaf visibility
	virtual void					GetRenderBounds( Vector& theMins, Vector& theMaxs );
	virtual const Vector&			GetRenderOrigin( void );
	virtual const QAngle&			GetRenderAngles( void );

	virtual bool					GetSoundSpatialization( SpatializationInfo_t& info );

	// Attachments.
	//bool							GetAttachment( const char *szName, Vector &absOrigin );
	bool							GetAttachment( const char *szName, Vector &absOrigin, QAngle &absAngles );

	// Inherited from C_BaseEntity
	//virtual bool					GetAttachment( int number, Vector &origin );
	virtual bool					GetAttachment( int number, Vector &origin, QAngle &angles );
	virtual bool					GetAttachment( int number, matrix3x4_t &matrix );
	virtual bool					GetAttachmentVelocity( int number, Vector &originVel, Quaternion &angleVel );
	
	// Returns the attachment in local space
	bool							GetAttachmentLocal( int iAttachment, matrix3x4_t &attachmentToLocal );
	bool							GetAttachmentLocal( int iAttachment, Vector &origin, QAngle &angles );
	bool                            GetAttachmentLocal( int iAttachment, Vector &origin );


	// Should this object cast render-to-texture shadows?
	virtual ShadowType_t			ShadowCastType();

	// Should we collide?
	virtual CollideType_t			GetCollideType( void );

	virtual bool					TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );
	virtual bool					TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );


	virtual C_BaseEntity			*BecomeRagdollOnClient();


	virtual void					Clear( void );
	void							ForceSetupBonesAtTime( matrix3x4_t *pBonesOut, float flTime );
	virtual void					GetRagdollInitBoneArrays( matrix3x4_t *pDeltaBones0, matrix3x4_t *pDeltaBones1, matrix3x4_t *pCurrentBones, float boneDt );

	// For shadows rendering the correct body + sequence...
	virtual int GetBody()			{ return GetEngineObject()->GetBody(); }
	virtual int GetSkin()			{ return GetEngineObject()->GetSkin(); }

	void							GetBlendedLinearVelocity( Vector *pVec );
	int								LookupSequence ( const char *label );
	int								LookupActivity( const char *label );
	char const						*GetSequenceName( int iSequence ); 
	char const						*GetSequenceActivityName( int iSequence );
	Activity						GetSequenceActivity( int iSequence );
	KeyValues						*GetSequenceKeyValues( int iSequence );
	virtual void					StudioFrameAdvance(); // advance animation frame to some time in the future

	// Clientside animation
	virtual float					FrameAdvance( float flInterval = 0.0f );
	virtual void					UpdateClientSideAnimation();
	void							ClientSideAnimationChanged();
	virtual unsigned int			ComputeClientSideAnimationFlags();




	void SetBodygroup( int iGroup, int iValue );
	int GetBodygroup( int iGroup );

	const char *GetBodygroupName( int iGroup );
	int FindBodygroupByName( const char *name );
	int GetBodygroupCount( int iGroup );
	int GetNumBodyGroups( void );

	void							SetHitboxSet( int setnum );
	void							SetHitboxSetByName( const char *setname );
	int								GetHitboxSet( void );
	char const						*GetHitboxSetName( void );
	int								GetHitboxSetCount( void );
	void							DrawClientHitboxes( float duration = 0.0f, bool monocolor = false );


	//virtual bool					IsActivityFinished( void ) { return m_bSequenceFinished; }

	// All view model attachments origins are stretched so you can place entities at them and
	// they will match up with where the attachment winds up being drawn on the view model, since
	// the view models are drawn with a different FOV.
	//
	// If you're drawing something inside of a view model's DrawModel() function, then you want the
	// original attachment origin instead of the adjusted one. To get that, call this on the 
	// adjusted attachment origin.
	virtual void					UncorrectViewModelAttachment( Vector &vOrigin ) {}





	// Used for debugging. Will produce asserts if someone tries to setup bones or
	// attachments before it's allowed.
	// Use the "AutoAllowBoneAccess" class to auto push/pop bone access.
	// Use a distinct "tag" when pushing/popping - asserts when push/pop tags do not match.
	struct AutoAllowBoneAccess
	{
		AutoAllowBoneAccess( bool bAllowForNormalModels, bool bAllowForViewModels );
		~AutoAllowBoneAccess( void );
	};

	

	

	// Purpose: My physics object has been updated, react or extract data
	virtual void					VPhysicsUpdate( IPhysicsObject *pPhysics );



	// This is called to do the actual muzzle flash effect.
	virtual void ProcessMuzzleFlashEvent();
	
	// Update client side animations
	static void UpdateClientSideAnimations();

	// Load the model's keyvalues section and create effects listed inside it
	void InitModelEffects( void );

	// Sometimes the server wants to update the client's cycle to get the two to run in sync (for proper hit detection)
	virtual void SetServerIntendedCycle( float intended ) { (void)intended; }
	virtual float GetServerIntendedCycle( void ) { return -1.0f; }



	int								FindTransitionSequence( int iCurrentSequence, int iGoalSequence, int *piDir );


	virtual void					GetToolRecordingState( KeyValues *msg );
	virtual void					CleanupToolRecordingState( KeyValues *msg );

	virtual bool					ShouldResetSequenceOnNewModel( void );

	virtual bool					IsViewModel() const;
	void							TermRopes();

protected:
	// View models scale their attachment positions to account for FOV. To get the unmodified
	// attachment position (like if you're rendering something else during the view model's DrawModel call),
	// use TransformViewModelAttachmentToWorld.
	virtual void					FormatViewModelAttachment( int nAttachment, matrix3x4_t &attachmentToWorld ) {}

	

	// Models used in a ModelPanel say yes to this
	virtual bool					IsMenuModel() const;

	// Allow studio models to tell C_BaseEntity what their m_nBody value is
	//virtual int						GetStudioBody( void ) { return m_nBody; }

	virtual bool					CalcAttachments();

private:

	CBoneList*						RecordBones( IStudioHdr *hdr, matrix3x4_t *pBoneState );

	bool							PutAttachment( int number, const matrix3x4_t &attachmentToWorld );

	void							DelayedInitModelEffects( void );



public:
	//CRagdoll						*m_pRagdoll;



	CSequenceTransitioner			m_SequenceTransitioner;

protected:

	int								m_iEyeAttachment;






	

	ClientSideAnimationListHandle_t	m_ClientSideAnimationListHandle;



	// Bone attachments. Used for attaching one BaseAnimating to another's bones.
	// Client side only.
	CUtlVector<CHandle<C_BaseAnimating> > m_BoneAttachments;
	int								m_boneIndexAttached;
	Vector							m_bonePosition;
	QAngle							m_boneAngles;
	CHandle<C_BaseAnimating>		m_pAttachedTo;

protected:



private:


	






	// Ropes that got spawned when the model was created.
	CUtlLinkedList<C_RopeKeyframe*,unsigned short> m_Ropes;

	// event processing info
	float							m_flPrevEventCycle;
	int								m_nEventSequence;







	int								m_nPrevResetEventsParity;





	// Current cycle location from server
protected:

	bool							m_bNoModelParticles;

private:

														// when merg
	


	// Calculated attachment points
	CUtlVector<CAttachmentData>		m_Attachments;

	void							SetupBones_AttachmentHelper( IStudioHdr *pStudioHdr );





	bool							m_bInitModelEffects;

	// Dynamic models
	//bool							m_bDynamicModelAllowed;
	//bool							m_bDynamicModelPending;
	//bool							m_bResetSequenceInfoOnLoad;
	//CRefCountedModelIndex			m_AutoRefModelIndex;
public:
	//void							EnableDynamicModels() { m_bDynamicModelAllowed = true; }
	//bool							IsDynamicModelLoading() const { return m_bDynamicModelPending; }
//private:
	//virtual void					OnModelLoadComplete( const model_t* pModel );

private:

};

enum 
{
	RAGDOLL_FRICTION_OFF = -2,
	RAGDOLL_FRICTION_NONE,
	RAGDOLL_FRICTION_IN,
	RAGDOLL_FRICTION_HOLD,
	RAGDOLL_FRICTION_OUT,
};

class C_ClientRagdoll : public C_BaseAnimating, public IPVSNotify
{
	
public:
	C_ClientRagdoll( );//bool bRestoring 
	DECLARE_CLASS( C_ClientRagdoll, C_BaseAnimating );
	DECLARE_DATADESC();

	bool Init(int entnum, int iSerialNum);

	// inherited from IPVSNotify
	virtual void OnPVSStatusChanged( bool bInPVS );

	virtual void Release( void );
	virtual void SetupWeights( const matrix3x4_t *pBoneToWorld, int nFlexWeightCount, float *pFlexWeights, float *pFlexDelayedWeights );
	virtual void ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName );
	void ClientThink( void );
	void ReleaseRagdoll( void ) { m_bReleaseRagdoll = true;	}
	bool ShouldSavePhysics( void ) { return true; }
	virtual void	OnSave();
	virtual void	OnRestore();
	virtual int ObjectCaps( void ) { return BaseClass::ObjectCaps() | FCAP_SAVE_NON_NETWORKABLE; }
	virtual IPVSNotify*				GetPVSNotifyInterface() { return this; }

	void	HandleAnimatedFriction( void );
	virtual void SUB_Remove( void );

	void	FadeOut( void );

	bool m_bFadeOut;
	bool m_bImportant;
	float m_flEffectTime;

private:
	int m_iCurrentFriction;
	int m_iMinFriction;
	int m_iMaxFriction;
	float m_flFrictionModTime;
	float m_flFrictionTime;

	int  m_iFrictionAnimState;
	bool m_bReleaseRagdoll;

	bool m_bFadingOut;

	float m_flScaleEnd[NUM_HITBOX_FIRES];
	float m_flScaleTimeStart[NUM_HITBOX_FIRES];
	float m_flScaleTimeEnd[NUM_HITBOX_FIRES];
};






















// FIXME: move these to somewhere that makes sense
void GetColumn( matrix3x4_t& src, int column, Vector &dest );
void SetColumn( Vector &src, int column, matrix3x4_t& dest );

EXTERN_RECV_TABLE(DT_BaseAnimating);


extern void DevMsgRT( PRINTF_FORMAT_STRING char const* pMsg, ... );

#endif // C_BASEANIMATING_H
