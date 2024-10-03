//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A base class for the client-side representation of entities.
//
//			This class encompasses both entities that are created on the server
//			and networked to the client AND entities that are created on the
//			client.
//
// $NoKeywords: $
//===========================================================================//

#ifndef C_BASEENTITY_H
#define C_BASEENTITY_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "icliententityinternal.h"
#include "engine/ivmodelinfo.h"
#include "engine/ivmodelrender.h"
#include "client_class.h"
#include "iclientshadowmgr.h"
#include "ehandle.h"
#include "iclientunknown.h"
#include "client_thinklist.h"
//#if !defined( NO_ENTITY_PREDICTION )
//#include "predictableid.h"
//#endif
#include "soundflags.h"
#include "shareddefs.h"
#include "networkvar.h"
#include "interpolatedvar.h"
#include "collisionproperty.h"
#include "particle_property.h"
#include "toolframework/itoolentity.h"
#include "tier0/threadtools.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
//#include "engine\ivmodelrender.h"

class C_Team;
class IPhysicsObject;
class IClientVehicle;
class CPredictionCopy;
class C_BasePlayer;
//struct IStudioHdr;
class IStudioHdr;
class CDamageModifier;
class IRecipientFilter;
class CUserCmd;
struct solid_t;
class ISave;
class IRestore;
class C_BaseAnimating;
class C_AI_BaseNPC;
struct EmitSound_t;
class C_RecipientFilter;
class CTakeDamageInfo;
class C_BaseCombatCharacter;
class CEntityMapData;
class ConVar;
class CDmgAccumulator;

struct CSoundParameters;

typedef unsigned int			AimEntsListHandle_t;

#define		INVALID_AIMENTS_LIST_HANDLE		(AimEntsListHandle_t)~0

extern void RecvProxy_IntToColor32( const CRecvProxyData *pData, void *pStruct, void *pOut );
//extern void RecvProxy_LocalVelocity( const CRecvProxyData *pData, void *pStruct, void *pOut );
//extern ISaveRestoreOps* engineObjectFuncs;
extern ISoundEmitterSystem* g_pSoundEmitterSystem;
//extern HSOUNDSCRIPTHANDLE PrecacheScriptSound(const char* soundname);
//extern void PrefetchScriptSound(const char* soundname);
//extern bool PrecacheSound(const char* name);
//extern void PrefetchSound(const char* name);
//extern bool IsPrecacheAllowed();
//extern void SetAllowPrecache(bool allow);
//extern bool	GetParametersForSound(const char* soundname, CSoundParameters& params, char const* actormodel);
//extern bool	GetParametersForSound(const char* soundname, HSOUNDSCRIPTHANDLE& handle, CSoundParameters& params, char const* actormodel);
//extern void	EmitSound(CBaseEntity* pEntity, const char* soundname, float soundtime = 0.0f, float* duration = NULL);  // Override for doing the general case of CPASAttenuationFilter filter( this ), and EmitSound( filter, entindex(), etc. );
//extern void EmitSound(IRecipientFilter& filter, int iEntIndex, const EmitSound_t& params);
//extern void EmitSound(IRecipientFilter& filter, int iEntIndex, const EmitSound_t& params, HSOUNDSCRIPTHANDLE& handle);
//extern void EmitSound(IRecipientFilter& filter, int iEntIndex, const char* soundname, const Vector* pOrigin = NULL, float soundtime = 0.0f, float* duration = NULL);
//extern void EmitSound(IRecipientFilter& filter, int iEntIndex, const char* soundname, HSOUNDSCRIPTHANDLE& handle, const Vector* pOrigin = NULL, float soundtime = 0.0f, float* duration = NULL);
//extern void	StopSound(CBaseEntity* pEntity, const char* soundname);
//extern void StopSound(int iEntIndex, const char* soundname);
//extern void StopSound(int iEntIndex, int iChannel, const char* pSample);
//extern void EmitCloseCaption(IRecipientFilter& filter, int entindex, char const* token, CUtlVector< Vector >& soundorigins, float duration, bool warnifmissing = false);


enum CollideType_t
{
	ENTITY_SHOULD_NOT_COLLIDE = 0,
	ENTITY_SHOULD_COLLIDE,
	ENTITY_SHOULD_RESPOND
};



																	

#define DECLARE_INTERPOLATION()


// How many data slots to use when in multiplayer.
#define MULTIPLAYER_BACKUP			90


struct serialentity_t;

typedef CHandle<C_BaseEntity> EHANDLE; // The client's version of EHANDLE.

typedef void (C_BaseEntity::*ENTITYFUNCPTR)(C_BaseEntity *pOther );

// For entity creation on the client
typedef C_BaseEntity* (*DISPATCHFUNCTION)( void );

#include "touchlink.h"
#include "groundlink.h"

//#if !defined( NO_ENTITY_PREDICTION )
////-----------------------------------------------------------------------------
//// Purpose: For fully client side entities we use this information to determine
////  authoritatively if the server has acknowledged creating this entity, etc.
////-----------------------------------------------------------------------------
//struct PredictionContext
//{
//	PredictionContext()
//	{
//		m_bActive					= false;
//		m_nCreationCommandNumber	= -1;
//		m_pszCreationModule			= NULL;
//		m_nCreationLineNumber		= 0;
//		m_hServerEntity				= NULL;
//	}
//
//	// The command_number of the usercmd which created this entity
//	bool						m_bActive;
//	int							m_nCreationCommandNumber;
//	char const					*m_pszCreationModule;
//	int							m_nCreationLineNumber;
//	// The entity to whom we are attached
//	CHandle< C_BaseEntity >		m_hServerEntity;
//};
//#endif



//#define CREATE_PREDICTED_ENTITY( className )	\
//	C_BaseEntity::CreatePredictedEntityByName( className, __FILE__, __LINE__ );



// Entity flags that only exist on the client.
#define ENTCLIENTFLAG_GETTINGSHADOWRENDERBOUNDS	0x0001		// Tells us if we're getting the real ent render bounds or the shadow render bounds.
#define ENTCLIENTFLAG_DONTUSEIK					0x0002		// Don't use IK on this entity even if its model has IK.
#define ENTCLIENTFLAG_ALWAYS_INTERPOLATE		0x0004		// Used by view models.

enum
{
	INTERPOLATE_STOP = 0,
	INTERPOLATE_CONTINUE
};

struct ClientModelRenderInfo_t : public ModelRenderInfo_t
{
	// Added space for lighting origin override. Just allocated space, need to set base pointer
	matrix3x4_t lightingOffset;

	// Added space for model to world matrix. Just allocated space, need to set base pointer
	matrix3x4_t modelToWorld;
};

//-----------------------------------------------------------------------------
// Purpose: Base client side entity object
//-----------------------------------------------------------------------------
class C_BaseEntity : public IClientEntity
{
// Construction
	DECLARE_CLASS_NOBASE( C_BaseEntity );

	friend class CPrediction;
	friend void cc_cl_interp_all_changed( IConVar *pConVar, const char *pOldString, float flOldValue );

public:
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

									C_BaseEntity();
	virtual							~C_BaseEntity();

	virtual bool IsNetworkable(void) { return C_BaseEntity::IsServerEntity(); }
	static int GetEngineObjectTypeStatic() { return ENGINEOBJECT_BASE; }
	//static C_BaseEntity				*CreatePredictedEntityByName( const char *classname, const char *module, int line, bool persist = false );
	
	// FireBullets uses shared code for prediction.
	virtual void					FireBullets( const FireBulletsInfo_t &info );
	virtual void					ModifyFireBulletsDamage( CTakeDamageInfo* dmgInfo ) {}
	virtual bool					ShouldDrawUnderwaterBulletBubbles();
	virtual bool					ShouldDrawWaterImpacts( void ) { return true; }
	virtual bool					HandleShotImpactingWater( const FireBulletsInfo_t &info, 
		const Vector &vecEnd, ITraceFilter *pTraceFilter, Vector *pVecTracerDest );
	virtual ITraceFilter*			GetBeamTraceFilter( void );
	virtual void					DispatchTraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator = NULL );
	virtual void					TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator = NULL );
	virtual void					DoImpactEffect( trace_t &tr, int nDamageType );
	virtual void					MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType );
	virtual int						GetTracerAttachment( void );
	void							ComputeTracerStartPosition( const Vector &vecShotSrc, Vector *pVecTracerStart );
	void							TraceBleed( float flDamage, const Vector &vecDir, trace_t *ptr, int bitsDamageType );
	virtual int						BloodColor();
	virtual const char*				GetTracerType();

	virtual void					Spawn( void );
	virtual void					SpawnClientEntity( void );
	virtual void					Precache( void );
	virtual void					Activate();

	virtual bool					KeyValue( const char *szKeyName, const char *szValue );
	virtual bool					KeyValue( const char *szKeyName, float flValue );
	virtual bool					KeyValue( const char *szKeyName, const Vector &vecValue );
	virtual bool					GetKeyValue( const char *szKeyName, char *szValue, int iMaxLen );

		// Entities block Line-Of-Sight for NPCs by default.
	// Set this to false if you want to change this behavior.
	void							SetBlocksLOS( bool bBlocksLOS );
	bool							BlocksLOS( void );
	void							SetAIWalkable( bool bBlocksLOS );
	bool							IsAIWalkable( void );




	// Called by the CLIENTCLASS macros.
	virtual bool					Init( int entnum, int iSerialNum );

	virtual void					AfterInit();

	// Called in the destructor to shutdown everything.
	void							Term();

	// memory handling, uses calloc so members are zero'd out on instantiation
    void							*operator new( size_t stAllocateBlock );
	void							*operator new[]( size_t stAllocateBlock );
    void							*operator new( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine );
	void							*operator new[]( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine );
	void							operator delete( void *pMem );
	void							operator delete( void *pMem, int nBlockUse, const char *pFileName, int nLine ) { operator delete( pMem ); }


	virtual C_BaseAnimating*		GetBaseAnimating() { return NULL; }

// IClientUnknown overrides.
public:

	//virtual void SetRefEHandle( const CBaseHandle &handle );
	//virtual const CBaseHandle& GetRefEHandle() const;

	void					SetToolHandle( HTOOLHANDLE handle );
	HTOOLHANDLE				GetToolHandle() const;

	void					EnableInToolView( bool bEnable );
	bool					IsEnabledInToolView() const;

	void					SetToolRecording( bool recording );
	bool					IsToolRecording() const;
	bool					HasRecordedThisFrame() const;
	virtual void			RecordToolMessage();

	// used to exclude entities from being recorded in the SFM tools
	void					DontRecordInTools();
	bool					ShouldRecordInTools() const;

	virtual void					Release();
	virtual ICollideable*			GetCollideable()		{ return GetEngineObject()->GetCollideable(); }
	virtual IClientNetworkable*		GetClientNetworkable()	{ return this; }
	virtual IClientRenderable*		GetClientRenderable()	{ return this; }
	virtual IClientEntity*			GetIClientEntity()		{ return this; }
	virtual C_BaseEntity*			GetBaseEntity()			{ return this; }
	virtual IClientThinkable*		GetClientThinkable()	{ return this; }


// Methods of IClientRenderable
public:

	virtual const Vector&			GetRenderOrigin( void );
	virtual const QAngle&			GetRenderAngles( void );
	virtual Vector					GetObserverCamOrigin( void ) { return GetRenderOrigin(); }	// Return the origin for player observers tracking this target
	virtual const matrix3x4_t &		RenderableToWorldTransform();
	virtual bool					IsTwoPass( void );
	virtual bool					UsesPowerOfTwoFrameBufferTexture();
	virtual bool					UsesFullFrameBufferTexture();
	virtual int						DrawModel( int flags );
	virtual bool OnInternalDrawModel(ClientModelRenderInfo_t* pInfo);
	virtual bool OnPostInternalDrawModel(ClientModelRenderInfo_t* pInfo);
	virtual void					ComputeFxBlend( void );
	virtual int						GetFxBlend( void );
	virtual bool					LODTest() { return true; }   // NOTE: UNUSED
	virtual void					GetRenderBounds( Vector& mins, Vector& maxs );
	virtual IPVSNotify*				GetPVSNotifyInterface();
	virtual void					GetRenderBoundsWorldspace( Vector& absMins, Vector& absMaxs );

	virtual void					GetShadowRenderBounds( Vector &mins, Vector &maxs, ShadowType_t shadowType );

	// Determine the color modulation amount
	virtual void					GetColorModulation( float* color );

	virtual void OnThreadedDrawSetup() {}
public:
	virtual bool					TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );
	virtual bool					TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );

	// To mimic server call convention
	C_BaseEntity					*GetOwnerEntity( void ) const;
	void							SetOwnerEntity( C_BaseEntity *pOwner );

	C_BaseEntity					*GetEffectEntity( void ) const;
	void							SetEffectEntity( C_BaseEntity *pEffectEnt );

	// This function returns a value that scales all damage done by this entity.
	// Use CDamageModifier to hook in damage modifiers on a guy.
	virtual float					GetAttackDamageScale( void );

// IClientNetworkable implementation.
public:
	virtual void					NotifyShouldTransmit( ShouldTransmitState_t state );

	// save out interpolated values
	virtual void					PreDataUpdate( DataUpdateType_t updateType );
	virtual void					PostDataUpdate( DataUpdateType_t updateType );

	virtual void					ValidateModelIndex( void );

	// pvs info. NOTE: Do not override these!!
	virtual void					SetDormant( bool bDormant );
	virtual bool					IsDormant( void );

	// Tells the entity that it's about to be destroyed due to the client receiving
	// an uncompressed update that's caused it to destroy all entities & recreate them.
	virtual void					SetDestroyedOnRecreateEntities( void );

	//virtual int						entindex( void ) const;
	
	// This works for client-only entities and returns the GetEntryIndex() of the entity's handle,
	// so the sound system can get an IClientEntity from it.
	int GetSoundSourceIndex() const;	

	// Server to client message received
	virtual void					ReceiveMessage( int classID, bf_read &msg );

	//virtual void*					GetDataTableBasePtr();

// IClientThinkable.
public:
	// Called whenever you registered for a think message (with SetNextClientThink).
	virtual void					ClientThink();

	virtual ClientThinkHandle_t		GetThinkHandle();
	virtual void					SetThinkHandle( ClientThinkHandle_t hThink );





public:

	CParticleProperty		*ParticleProp();
	const CParticleProperty *ParticleProp() const;

	// Simply here for game shared 
	bool					IsFloating();

	virtual bool					ShouldSavePhysics();

// save/restore stuff
	virtual void					OnSave();
	virtual void					OnRestore();
	// capabilities for save/restore
	virtual int						ObjectCaps( void );
	// only overload these if you have special data to serialize
	virtual int						Save(ISave& save) { return GetEngineObject()->Save(save); }
	virtual int						Restore(IRestore& restore) { return GetEngineObject()->Restore(restore); }

private:

	//int SaveDataDescBlock( ISave &save, datamap_t *dmap );
	//int RestoreDataDescBlock( IRestore &restore, datamap_t *dmap );

public:
	// Called after restoring data into prediction slots. This function is used in place of proxies
	// on the variables, so if some variable like m_nModelIndex needs to update other state (like 
	// the model pointer), it is done here.
	virtual void OnPostRestoreData();

public:

	// Called after spawn, and in the case of self-managing objects, after load
	virtual bool	CreateVPhysics();

	// Convenience routines to init the vphysics simulation for this object.
	// This creates a static object.  Something that behaves like world geometry - solid, but never moves
	IPhysicsObject *VPhysicsInitStatic( void );

	// This creates a normal vphysics simulated object
	IPhysicsObject *VPhysicsInitNormal( SolidType_t solidType, int nSolidFlags, bool createAsleep, solid_t *pSolid = NULL );

	// This creates a vphysics object with a shadow controller that follows the AI
	// Move the object to where it should be and call UpdatePhysicsShadowToCurrentPosition()
	IPhysicsObject *VPhysicsInitShadow( bool allowPhysicsMovement, bool allowPhysicsRotation, solid_t *pSolid = NULL );

private:
	// called by all vphysics inits
	bool			VPhysicsInitSetup();
public:

	void			VPhysicsSetObject( IPhysicsObject *pPhysics );
	// destroy and remove the physics object for this entity
	virtual void	VPhysicsDestroyObject( void );

	// Purpose: My physics object has been updated, react or extract data
	virtual void					VPhysicsUpdate( IPhysicsObject *pPhysics );
	inline IPhysicsObject			*VPhysicsGetObject( void ) const { return m_pPhysicsObject; }
	virtual int						VPhysicsGetObjectList( IPhysicsObject **pList, int listMax );
	virtual bool					VPhysicsIsFlesh( void );

// IClientEntity implementation.
public:
	virtual bool					SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime );
	virtual void					SetupWeights( const matrix3x4_t *pBoneToWorld, int nFlexWeightCount, float *pFlexWeights, float *pFlexDelayedWeights );
	virtual bool					UsesFlexDelayedWeights() { return false; }
	//virtual void					DoAnimationEvents( void );

	// Add entity to visible entities list?
	virtual void					AddEntity( void );

	



	void							SetLocalTransform( const matrix3x4_t &localTransform );

	virtual int						CalcOverrideModelIndex() { return -1; }

	// These methods return a *world-aligned* box relative to the absorigin of the entity.
	// This is used for collision purposes and is *not* guaranteed
	// to surround the entire entity's visual representation
	// NOTE: It is illegal to ask for the world-aligned bounds for
	// SOLID_BSP objects
	virtual const Vector&			WorldAlignMins( ) const;
	virtual const Vector&			WorldAlignMaxs( ) const;

	

	// NOTE: These use the collision OBB to compute a reasonable center point for the entity
	virtual const Vector&			WorldSpaceCenter( ) const;

	// FIXME: Do we want this?
	const Vector&					WorldAlignSize( ) const;

	// Returns a radius of a sphere 
	// *centered at the world space center* bounding the collision representation 
	// of the entity. NOTE: The world space center *may* move when the entity rotates.
	//float							BoundingRadius() const;

	// Used when the collision prop is told to ask game code for the world-space surrounding box
	virtual void					ComputeWorldSpaceSurroundingBox( Vector *pVecWorldMins, Vector *pVecWorldMaxs );	



	//virtual class CMouthInfo		*GetMouth( void );

	// Retrieve sound spatialization info for the specified sound on this entity
	// Return false to indicate sound is not audible
	virtual bool					GetSoundSpatialization( SpatializationInfo_t& info );

	// Attachments
	virtual int						LookupAttachment( const char *pAttachmentName ) { return -1; }
	virtual bool					GetAttachment( int number, matrix3x4_t &matrix );
	virtual bool					GetAttachment( int number, Vector &origin );
	virtual	bool					GetAttachment( int number, Vector &origin, QAngle &angles );
	virtual bool					GetAttachmentVelocity( int number, Vector &originVel, Quaternion &angleVel );

	// Team handling
	virtual C_Team					*GetTeam( void );
	virtual int						GetTeamNumber( void ) const;
	virtual void					ChangeTeam( int iTeamNum );			// Assign this entity to a team.
	virtual int						GetRenderTeamNumber( void );
	virtual bool					InSameTeam( C_BaseEntity *pEntity );	// Returns true if the specified entity is on the same team as this one
	virtual bool					InLocalTeam( void );

	// ID Target handling
	virtual bool					IsValidIDTarget( void ) { return false; }
	virtual const char				*GetIDString( void ) { return ""; };

	// See CSoundEmitterSystem
	//virtual void ModifyEmitSoundParams( EmitSound_t &params );

	//static void	EmitSound(C_BaseEntity* pEntity, const char *soundname, float soundtime = 0.0f, float *duration = NULL );  // Override for doing the general case of CPASAttenuationFilter( this ), and EmitSound( filter, entindex(), etc. );
	//static void	EmitSound(C_BaseEntity* pEntity, const char *soundname, HSOUNDSCRIPTHANDLE& handle, float soundtime = 0.0f, float *duration = NULL );  // Override for doing the general case of CPASAttenuationFilter( this ), and EmitSound( filter, entindex(), etc. );
	//static void	StopSound(C_BaseEntity* pEntity, const char *soundname );
	//static void	StopSound(C_BaseEntity* pEntity, const char *soundname, HSOUNDSCRIPTHANDLE& handle );
	//static void	GenderExpandString(C_BaseEntity* pEntity, char const *in, char *out, int maxlen );

	//static float GetSoundDuration( const char *soundname, char const *actormodel );

	//static bool	GetParametersForSound( const char *soundname, CSoundParameters &params, const char *actormodel );
	//static bool	GetParametersForSound( const char *soundname, HSOUNDSCRIPTHANDLE& handle, CSoundParameters &params, const char *actormodel );

	//static void EmitSound( IRecipientFilter& filter, int iEntIndex, const char *soundname, const Vector *pOrigin = NULL, float soundtime = 0.0f, float *duration = NULL );
	//static void EmitSound( IRecipientFilter& filter, int iEntIndex, const char *soundname, HSOUNDSCRIPTHANDLE& handle, const Vector *pOrigin = NULL, float soundtime = 0.0f, float *duration = NULL );
	//static void StopSound( int iEntIndex, const char *soundname );
	//static soundlevel_t LookupSoundLevel( const char *soundname );
	//static soundlevel_t LookupSoundLevel( const char *soundname, HSOUNDSCRIPTHANDLE& handle );

	//static void EmitSound( IRecipientFilter& filter, int iEntIndex, const EmitSound_t & params );
	//static void EmitSound( IRecipientFilter& filter, int iEntIndex, const EmitSound_t & params, HSOUNDSCRIPTHANDLE& handle );

	//static void StopSound( int iEntIndex, int iChannel, const char *pSample );

	//static void EmitAmbientSound( int entindex, const Vector& origin, const char *soundname, int flags = 0, float soundtime = 0.0f, float *duration = NULL );

	// These files need to be listed in scripts/game_sounds_manifest.txt
	//static HSOUNDSCRIPTHANDLE PrecacheScriptSound( const char *soundname );
	//static void PrefetchScriptSound( const char *soundname );

	// For each client who appears to be a valid recipient, checks the client has disabled CC and if so, removes them from 
	//  the recipient list.
	//static void RemoveRecipientsIfNotCloseCaptioning( C_RecipientFilter& filter );
	//static void EmitCloseCaption( IRecipientFilter& filter, int entindex, char const *token, CUtlVector< Vector >& soundorigins, float duration, bool warnifmissing = false );

	// Moves all aiments into their correct position for the frame
	static void	MarkAimEntsDirty();
	static void CalcAimEntPositions();

	//static bool IsPrecacheAllowed();
	//static void SetAllowPrecache( bool allow );

	//static bool m_bAllowPrecache;

	static bool IsSimulatingOnAlternateTicks();

// C_BaseEntity local functions
public:

	void UpdatePartitionListEntry();

	// This can be used to setup the entity as a client-only entity. 
	// Override this to perform per-entity clientside setup
	virtual bool InitializeAsClientEntity( const char *pszModelName, RenderGroup_t renderGroup );

	



	// This function gets called on all client entities once per simulation phase.
	// It dispatches events like OnDataChanged(), and calls the legacy function AddEntity().
	virtual void					Simulate();	


	// This event is triggered during the simulation phase if an entity's data has changed. It is 
	// better to hook this instead of PostDataUpdate() because in PostDataUpdate(), server entity origins
	// are incorrect and attachment points can't be used.
	virtual void					OnDataChanged( DataUpdateType_t type );

	// This is called once per frame before any data is read in from the server.
	virtual void					OnPreDataChanged( DataUpdateType_t type );

	bool IsStandable() const;
	bool IsBSPModel() const;

	
	// If this is a vehicle, returns the vehicle interface
	virtual IClientVehicle*			GetClientVehicle() { return NULL; }

	// Returns the aiment render origin + angles
	virtual void					GetAimEntOrigin( IClientEntity *pAttachedTo, Vector *pAbsOrigin, QAngle *pAbsAngles );

	inline ClientEntityHandle_t		GetClientHandle() const	{ return ClientEntityHandle_t( GetRefEHandle() ); }
	inline bool						IsServerEntity( void );

	virtual RenderGroup_t			GetRenderGroup();

	virtual void					GetToolRecordingState( KeyValues *msg );
	virtual void					CleanupToolRecordingState( KeyValues *msg );

	// The value returned by here determines whether or not (and how) the entity
	// is put into the spatial partition.
	virtual CollideType_t			GetCollideType( void );

	virtual bool					ShouldDraw();
	inline	bool					IsVisible() const { return m_hRender != INVALID_CLIENT_RENDER_HANDLE; }
			void					UpdateVisibility();
	
	// Returns true if the entity changes its position every frame on the server but it doesn't
	// set animtime. In that case, the client returns true here so it copies the server time to
	// animtime in OnDataChanged and the position history is correct for interpolation.
	virtual bool					IsSelfAnimating();



	// Initialize things given a new model.
	virtual IStudioHdr				*OnNewModel();
	virtual void					OnNewParticleEffect( const char *pszParticleName, CNewParticleEffect *pNewParticleEffect );



	virtual void					ResetLatched();
	
	float							GetInterpolationAmount( int flags );

	// Interpolate the position for rendering
	virtual bool					Interpolate( float currentTime );

	// Did the object move so far that it shouldn't interpolate?
	bool							Teleported( void );
	// Is this a submodel of the world ( *1 etc. in name ) ( brush models only )
	virtual bool					IsSubModel( void );
	// Deal with EF_* flags
	virtual void					CreateLightEffects( void );

	void							AddToAimEntsList();
	void							RemoveFromAimEntsList();

	// Reset internal fields
	virtual void					Clear( void );
	// Helper to draw raw brush models
	virtual int						DrawBrushModel( bool bTranslucent, int nFlags, bool bTwoPass );

	// returns the material animation start time
	virtual float					GetTextureAnimationStartTime();
	// Indicates that a texture animation has wrapped
	virtual void					TextureAnimationWrapped();

	// Set the next think time. Pass in CLIENT_THINK_ALWAYS to have Think() called each frame.
	virtual void					SetNextClientThink( float nextThinkTime );

	// anything that has health can override this...
	virtual void					SetHealth(int iHealth) {}
	virtual int						GetHealth() const { return 0; }
	virtual int						GetMaxHealth() const { return 1; }
	virtual bool					IsVisibleToTargetID( void ) { return false; }

	// Returns the health fraction
	float							HealthFraction() const;

	// Should this object cast shadows?
	virtual ShadowType_t			ShadowCastType();

	// Should this object receive shadows?
	virtual bool					ShouldReceiveProjectedTextures( int flags );

	// Shadow-related methods
	virtual bool IsShadowDirty( );
	virtual void MarkShadowDirty( bool bDirty );
	virtual IClientRenderable *GetShadowParent();
	virtual IClientRenderable *FirstShadowChild();
	virtual IClientRenderable *NextShadowPeer();

	// Sets up a render handle so the leaf system will draw this entity.
	void							AddToLeafSystem();
	void							AddToLeafSystem( RenderGroup_t group );
	// remove entity form leaf system again
	void							RemoveFromLeafSystem();	

	// A method to apply a decal to an entity
	virtual void					AddDecal( const Vector& rayStart, const Vector& rayEnd,
										const Vector& decalCenter, int hitbox, int decalIndex, bool doTrace, trace_t& tr, int maxLODToDecal = ADDDECAL_TO_ALL_LODS );

	virtual void					AddColoredDecal( const Vector& rayStart, const Vector& rayEnd,
		const Vector& decalCenter, int hitbox, int decalIndex, bool doTrace, trace_t& tr, Color cColor, int maxLODToDecal = ADDDECAL_TO_ALL_LODS );

	// A method to remove all decals from an entity
	void							RemoveAllDecals( void );

	// Is this a brush model?
	bool							IsBrushModel() const;

	virtual bool					IsViewModel() const { return false; }




	//virtual bool					IsClientCreated( void ) const;

	virtual void					UpdateOnRemove( void );

	virtual void					SUB_Remove( void );

	// Prediction stuff
	/////////////////
	void							CheckInitPredictable( const char *context );



	void							InitPredictable( void );
	void							ShutdownPredictable( void );

	virtual void					SetPredictable( bool state );
	bool							GetPredictable( void ) const;

	//bool							GetPredictionEligible( void ) const;
	//void							SetPredictionEligible( bool canpredict );



	virtual char const *			DamageDecal( int bitsDamageType, int gameMaterial );
	virtual void					DecalTrace( trace_t *pTrace, char const *decalName );
	virtual void					ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName );

	virtual bool					ShouldPredict( void ) { return false; };

	virtual void					Think( void )
	{
		AssertMsg( GetEngineObject()->GetPfnThink() != &C_BaseEntity::Think, "Infinite recursion is infinitely bad." );

		if (GetEngineObject()->GetPfnThink() )
		{
			( this->*GetEngineObject()->GetPfnThink())();
		}
	}

	
	// Toggle the visualization of the entity's abs/bbox
	enum
	{
		VISUALIZE_COLLISION_BOUNDS		= 0x1,
		VISUALIZE_SURROUNDING_BOUNDS	= 0x2,
		VISUALIZE_RENDER_BOUNDS			= 0x4,
	};

	void							ToggleBBoxVisualization( int fVisFlags );
	void							DrawBBoxVisualizations( void );

// Methods implemented on both client and server
public:
	void							SetSize( const Vector &vecMin, const Vector &vecMax ); // UTIL_SetSize( pev, mins, maxs );
	char const						*GetClassname( void );
	char const						*GetDebugName( void );
	static int						PrecacheModel( const char *name ); 
	//static bool						PrecacheSound( const char *name );
	//static void						PrefetchSound( const char *name );
	void							Remove( ); // UTIL_Remove( this );

public:

	



	

//#if !defined( NO_ENTITY_PREDICTION )
//	// The player drives simulation of this entity
//	void					SetPlayerSimulated( C_BasePlayer *pOwner );
//	bool					IsPlayerSimulated( void ) const;
//	CBasePlayer				*GetSimulatingPlayer( void );
//	void					UnsetPlayerSimulated( void );
//#endif

	// Sorry folks, here lies TF2-specific stuff that really has no other place to go
	virtual bool			CanBePoweredUp( void ) { return false; }
	virtual bool			AttemptToPowerup( int iPowerup, float flTime, float flAmount = 0, C_BaseEntity *pAttacker = NULL, CDamageModifier *pDamageModifier = NULL ) { return false; }

	virtual void			StartTouch( C_BaseEntity *pOther );
	virtual void			Touch( C_BaseEntity *pOther ); 
	virtual void			EndTouch( C_BaseEntity *pOther );

	void (C_BaseEntity ::*m_pfnTouch)( C_BaseEntity *pOther );

	void					PhysicsStep( void );

public:

	void					StartGroundContact( CBaseEntity *ground );
	void					EndGroundContact( CBaseEntity *ground );



	// Remove this as ground entity for all object resting on this object
	void					WakeRestingObjects();
	bool					HasNPCsOnIt();

	bool					PhysicsCheckWater( void );
	void					PhysicsCheckVelocity( void );
	void					PhysicsAddHalfGravity( float timestep );
	void					PhysicsAddGravityMove( Vector &move );

	virtual unsigned int	PhysicsSolidMaskForEntity( void ) const;

	void					PhysicsPushEntity( const Vector& push, trace_t *pTrace );
	void					PhysicsCheckWaterTransition( void );



	// Performs the collision resolution for fliers.
	void					PerformFlyCollisionResolution( trace_t &trace, Vector &move );
	void					ResolveFlyCollisionBounce( trace_t &trace, Vector &vecVelocity, float flMinTotalElasticity = 0.0f );
	void					ResolveFlyCollisionSlide( trace_t &trace, Vector &vecVelocity );
	void					ResolveFlyCollisionCustom( trace_t &trace, Vector &vecVelocity );

	// Creates the shadow (if it doesn't already exist) based on shadow cast type
	void					CreateShadow();

	// Destroys the shadow; causes its type to be recomputed if the entity doesn't go away immediately.
	void					DestroyShadow();

public:

	virtual void					PhysicsSimulate( void );
	virtual bool					IsAlive( void );

	bool							IsInWorld( void ) { return true; }

	bool							IsWorld() { return entindex() == 0; }
	/////////////////

	virtual bool					IsPlayer( void ) const { return false; };
	virtual bool					IsBaseCombatCharacter( void ) { return false; };
	virtual C_BaseCombatCharacter	*MyCombatCharacterPointer( void ) { return NULL; }
	virtual bool					IsNPC( void ) { return false; }
	C_AI_BaseNPC					*MyNPCPointer( void ); 
	virtual bool					IsNextBot() { return false; }
	// TF2 specific
	virtual bool					IsBaseObject( void ) const { return false; }
	virtual bool					IsBaseCombatWeapon( void ) const { return false; }
	virtual class C_BaseCombatWeapon		*MyCombatWeaponPointer() { return NULL; }
	virtual bool					IsCombatItem( void ) const { return false; }

	virtual bool					IsBaseTrain( void ) const { return false; }

	// Returns the eye point + angles (used for viewing + shooting)
	virtual Vector			EyePosition( void );
	virtual const QAngle&	EyeAngles( void );		// Direction of eyes
	virtual const QAngle&	LocalEyeAngles( void );	// Direction of eyes in local space (pl.v_angle)
	
	// position of ears
	virtual Vector		EarPosition( void );

	Vector	EyePosition( void ) const;			// position of eyes
	const QAngle &EyeAngles( void ) const;		// Direction of eyes in world space
	const QAngle &LocalEyeAngles( void ) const;	// Direction of eyes
	Vector	EarPosition( void ) const;			// position of ears

	// Called by physics to see if we should avoid a collision test....
	virtual bool		ShouldCollide( int collisionGroup, int contentsMask ) const;
	virtual	void					RefreshCollisionBounds(void);





	// Sets the model from a model index 
	void				SetModelByIndex( int nModelIndex );

	// Set model... (NOTE: Should only be used by client-only entities
	// Returns false if the model name is bogus or otherwise can't be loaded
	bool				SetModel( const char *pModelName );

	//friend class C_EngineObject;

	virtual IEngineObjectClient* GetEngineObject();
	virtual const IEngineObjectClient* GetEngineObject() const;

	void				ApplyLocalVelocityImpulse( const Vector &vecImpulse );
	void				ApplyAbsVelocityImpulse( const Vector &vecImpulse );
	void				ApplyLocalAngularVelocityImpulse( const AngularImpulse &angImpulse );

	// NOTE: Setting the abs velocity in either space will cause a recomputation
	// in the other space, so setting the abs velocity will also set the local vel
	void				SetLocalAngularVelocity( const QAngle &vecAngVelocity );
	const QAngle&		GetLocalAngularVelocity( ) const;

//	void				SetAbsAngularVelocity( const QAngle &vecAngAbsVelocity );
//	const QAngle&		GetAbsAngularVelocity( ) const;

	const Vector&		GetBaseVelocity() const;
	void				SetBaseVelocity( const Vector& v );

	virtual const Vector &GetViewOffset() const;
	virtual void		  SetViewOffset( const Vector& v );

#ifdef SIXENSE
	const Vector&		GetEyeOffset() const;
	void				SetEyeOffset( const Vector& v );

	const QAngle &		GetEyeAngleOffset() const;
	void				SetEyeAngleOffset( const QAngle & qa );
#endif

	void					OnPositionChenged();
	void					OnAnglesChanged();
	void					OnAnimationChanged();
	void					AddWatcherToEntity(CBaseEntity* pWatcher, int watcherType);
	void					RemoveWatcherFromEntity(CBaseEntity* pWatcher, int watcherType);
	void					NotifyPositionChanged();
	void					NotifyVPhysicsStateChanged(IPhysicsObject* pPhysics, bool bAwake);

	ClientRenderHandle_t	GetRenderHandle() const;

	void				SetRemovalFlag( bool bRemove );

	virtual void OnAddEffects(int nEffects) {}
	virtual void OnRemoveEffects(int nEffects) {}

	// For shadows rendering the correct body + sequence...
	virtual int GetBody() { return 0; }
	virtual int GetSkin() { return 0; }

	// Stubs on client
	void	NetworkStateManualMode( bool activate )		{ }
	void	NetworkStateChanged()						{ }
	void	NetworkStateChanged( void *pVar )			{ }
	void	NetworkStateSetUpdateInterval( float N )	{ }
	void	NetworkStateForceUpdate()					{ }



	float	GetCreateTime()										{ return m_flCreateTime; }
	void	SetCreateTime( float flCreateTime )					{ m_flCreateTime = flCreateTime; }


#ifdef _DEBUG
	void FunctionCheck( void *pFunction, const char *name );

	ENTITYFUNCPTR TouchSet( ENTITYFUNCPTR func, char *name ) 
	{ 
		//COMPILE_TIME_ASSERT( sizeof(func) == 4 );
		m_pfnTouch = func; 
		//FunctionCheck( *(reinterpret_cast<void **>(&m_pfnTouch)), name ); 
		return func;
	}
#endif

	const model_t* GetModel() const { return GetEngineObject()->GetModel(); }
	// Gets the model instance + shadow handle
	virtual ModelInstanceHandle_t GetModelInstance() { return m_ModelInstance; }
	void SetModelInstance( ModelInstanceHandle_t hInstance) { m_ModelInstance = hInstance; }
	bool SnatchModelInstance( C_BaseEntity * pToEntity );
	virtual ClientShadowHandle_t GetShadowHandle() const	{ return m_ShadowHandle; }
	virtual ClientRenderHandle_t&	RenderHandle();

	void CreateModelInstance();

	// Sets the origin + angles to match the last position received
	void MoveToLastReceivedPosition( bool force = false );

	// Only meant to be called from subclasses
	void DestroyModelInstance();
protected:
	

	// Interpolate entity
	static void ProcessTeleportList();
	static void ProcessInterpolatedList();
	static void CheckInterpolatedVarParanoidMeasurement();

public:
	// overrideable rules if an entity should interpolate
	virtual bool ShouldInterpolate();
protected:



	// For non-players
	int	PhysicsClipVelocity (const Vector& in, const Vector& normal, Vector& out, float overbounce );

	// Allow entities to perform client-side fades
    //virtual unsigned char GetClientSideFade() { return 255; }

protected:
	// Two part guts of Interpolate(). Shared with C_BaseAnimating.



public:
	// Accessors for above
	static int						GetPredictionRandomSeed( void );
	static void						SetPredictionRandomSeed( const CUserCmd *cmd );
	static C_BasePlayer				*GetPredictionPlayer( void );
	static void						SetPredictionPlayer( C_BasePlayer *player );
	static void						CheckCLInterpChanged();

	static C_BaseEntity				*Instance( int iEnt );
	// Doesn't do much, but helps with trace results
	static C_BaseEntity				*Instance( IClientEntity *ent );
	static C_BaseEntity				*Instance( CBaseHandle hEnt );
	// For debugging shared code
	static bool						IsServer( void );
	static bool						IsClient( void );
	static char const				*GetDLLType( void );





	// Bloat the culling bbox past the parent ent's bbox in local space if EF_BONEMERGE_FASTCULL is set.
	virtual void BoneMergeFastCullBloat( Vector &localMins, Vector &localMaxs, const Vector &thisEntityMins, const Vector &thisEntityMaxs ) const;


	// Accessors for color.
	const color32 GetRenderColor() const;
	void SetRenderColor( byte r, byte g, byte b );
	void SetRenderColor( byte r, byte g, byte b, byte a );
	void SetRenderColorR( byte r );
	void SetRenderColorG( byte g );
	void SetRenderColorB( byte b );
	void SetRenderColorA( byte a );

	virtual bool IsTransparent(void);
	virtual bool IgnoresZBuffer(void) const;
	void SetRenderMode( RenderMode_t nRenderMode, bool bForceUpdate = false );
	RenderMode_t GetRenderMode() const;
	virtual unsigned char	GetClientSideFade(void);
	virtual void SetFadeMinMax(float fademin, float fademax);
	bool IsOnFire() { return ((GetEngineObject()->GetFlags() & FL_ONFIRE) != 0); }
	virtual void ClientSideAnimationChanged() {}
	virtual void AddToClientSideAnimationList() {}
	virtual void RemoveFromClientSideAnimationList() {}
public:	

	// Determine what entity this corresponds to
	//int								index;	

	// Render information
	unsigned char					m_nRenderFX;
	unsigned char					m_nRenderFXBlend;

	// Entity flags that are only for the client (ENTCLIENTFLAG_ defines).
	unsigned short					m_EntClientFlags;

	CNetworkColor32( m_clrRender );

private:
	


public:

	
	float							m_flCreateTime;

	byte							m_ubInterpolationFrame;
	byte							m_ubOldInterpolationFrame;

private:

	unsigned char 					m_nRenderMode;
	unsigned char 					m_nOldRenderMode;

public:
	// Used to store the state we were added to the BSP as, so it can
	// reinsert the entity if the state changes.
	ClientRenderHandle_t			m_hRender;	// link into spatial partition

	// Interpolation says don't draw yet
	bool							m_bReadyToDraw;

	// Should we be interpolating?
	static bool						IsInterpolationEnabled();

	// Should we interpolate this tick?  (Used to be EF_NOINTERP)
	bool							IsNoInterpolationFrame();





#ifdef TF_CLIENT_DLL
	int								m_nModelIndexOverrides[MAX_VISION_MODES];
#endif

	char							m_takedamage;
	char							m_lifeState;

	int								m_iHealth;

	// was pev->speed
	float							m_flSpeed;

	// Team Handling
	int								m_iTeamNum;

//#if !defined( NO_ENTITY_PREDICTION )
//	// Certain entities (projectiles) can be created on the client
//	CPredictableId					m_PredictableID;
//	PredictionContext				*m_pPredictionContext;
//#endif



	// Called after predicted entity has been acknowledged so that no longer needed entity can
	//  be deleted
	// Return true to force deletion right now, regardless of isbeingremoved
	//virtual bool					OnPredictedEntityRemove( bool isbeingremoved, C_BaseEntity *predicted );

	//bool							IsDormantPredictable( void ) const;
	//bool							BecameDormantThisPacket( void ) const;
	//void							SetDormantPredictable( bool dormant );

	int								GetWaterLevel() const;
	void							SetWaterLevel( int nLevel );
	int								GetWaterType() const;
	void							SetWaterType( int nType );


	int								GetTextureFrameIndex( void );
	void							SetTextureFrameIndex( int iIndex );

	virtual bool					GetShadowCastDistance( float *pDist, ShadowType_t shadowType ) const;
	virtual bool					GetShadowCastDirection( Vector *pDirection, ShadowType_t shadowType ) const;
	virtual C_BaseEntity 			*GetShadowUseOtherEntity( void ) const;
	virtual void					SetShadowUseOtherEntity( C_BaseEntity *pEntity );


	virtual bool					AddRagdollToFadeQueue( void ) { return true; }

	// Dirty bits
	void							MarkRenderHandleDirty();

	// used by SourceTV since move-parents may be missing when child spawns.
	void							HierarchyUpdateMoveParent();

	virtual bool					IsDeflectable() { return false; }

protected:
	int								m_nFXComputeFrame;

	// FIXME: Should I move the functions handling these out of C_ClientEntity
	// and into C_BaseEntity? Then we could make these private.
	// Client handle
	//CBaseHandle m_RefEHandle;	// Reference ehandle. Used to generate ehandles off this entity.

private:
	// Set by tools if this entity should route "info" to various tools listening to HTOOLENTITIES
#ifndef NO_TOOLFRAMEWORK
	bool							m_bEnabledInToolView;
	bool							m_bToolRecording;
	HTOOLHANDLE						m_ToolHandle;
	int								m_nLastRecordedFrame;
	bool							m_bRecordInTools; // should this entity be recorded in the tools (we exclude some things like models for menus)
#endif

protected:
	// pointer to the entity's physics object (vphysics.dll)
	IPhysicsObject					*m_pPhysicsObject;	

//#if !defined( NO_ENTITY_PREDICTION )
//	bool							m_bPredictionEligible;
//#endif

	int								m_nSimulationTick;



	// Object eye position
	Vector							m_vecViewOffset;

#if defined(SIXENSE)
	Vector							m_vecEyeOffset;
	QAngle							m_EyeAngleOffset;    
#endif
	// Allow studio models to tell us what their m_nBody value is
	//virtual int						GetStudioBody( void ) { return 0; }

public:
	// This can be used to setup the entity as a client-only entity. It gets an entity handle,
	// a render handle, and is put into the spatial partition.
	bool InitializeAsClientEntityByIndex( int iIndex, RenderGroup_t renderGroup );


private:
	friend void OnRenderStart();

	// Figure out the smoothly interpolated origin for all server entities. Happens right before
	// letting all entities simulate.
	static void InterpolateServerEntities();
	
	// Check which entities want to be drawn and add them to the leaf system.
	//static void	AddVisibleEntities();

	// For entities marked for recording, post bone messages to IToolSystems
	static void ToolRecordEntities();

	// Computes the base velocity
	void UpdateBaseVelocity( void );

	// Physics-related private methods
	void PhysicsPusher( void );
	void PhysicsNone( void );
	void PhysicsNoclip( void );
	void PhysicsParent( void );
	void PhysicsStepRunTimestep( float timestep );
	void PhysicsToss( void );
	void PhysicsCustom( void );

	// Simulation in local space of rigid children
	void PhysicsRigidChild( void );

	

	// Computes new angles based on the angular velocity
	void SimulateAngles( float flFrameTime );

	// Implement this if you use MOVETYPE_CUSTOM
	virtual void PerformCustomPhysics( Vector *pNewPosition, Vector *pNewVelocity, QAngle *pNewAngles, QAngle *pNewAngVelocity );

	// methods related to decal adding
	void AddStudioDecal( const Ray_t& ray, int hitbox, int decalIndex, bool doTrace, trace_t& tr, int maxLODToDecal = ADDDECAL_TO_ALL_LODS );
	void AddColoredStudioDecal( const Ray_t& ray, int hitbox, int decalIndex, bool doTrace, trace_t& tr, Color cColor, int maxLODToDecal );
	void AddBrushModelDecal( const Ray_t& ray, const Vector& decalCenter, int decalIndex, bool doTrace, trace_t& tr );

	

	// Computes the water level + type
	void UpdateWaterState();

	// Checks a sweep without actually performing the move
	void PhysicsCheckSweep( const Vector& vecAbsStart, const Vector &vecAbsDelta, trace_t *pTrace );

public:
	// FIXME: REMOVE!!!
	void MoveToAimEnt( );
private:

	// was pev->avelocity
	QAngle							m_vecAngVelocity;

//	QAngle							m_vecAbsAngVelocity;

//#if !defined( NO_ENTITY_PREDICTION )
//	// It's still in the list for "fixup purposes" and simulation, but don't try to render it any more...
//	bool							m_bDormantPredictable;
//
//	// So we can clean it up
//	int								m_nIncomingPacketEntityBecameDormant;
//#endif


	// Base velocity
	Vector							m_vecBaseVelocity;

	// Model instance data..
	ModelInstanceHandle_t			m_ModelInstance;

	// Shadow data
	ClientShadowHandle_t			m_ShadowHandle;



	ClientThinkHandle_t				m_hThink;


private:


	unsigned char					m_iOldParentAttachment;

	unsigned char					m_nWaterLevel;
	unsigned char					m_nWaterType;
	// For client/server entities, true if the entity goes outside the PVS.
	// Unused for client only entities.
	bool							m_bDormant;
	// Prediction system
	bool							m_bPredictable;

	IEngineObjectClient* m_hOldMoveParent = NULL;


	//IEngineObjectClient* m_EngineObject;

	CNetworkVarEmbedded( CParticleProperty, m_Particles );



	float							m_flShadowCastDistance;
	EHANDLE							m_ShadowDirUseOtherEntity;



    

	EHANDLE							m_hLightingOrigin;
	EHANDLE							m_hLightingOriginRelative;
	float							m_fadeMinDist;
	float							m_fadeMaxDist;
	float							m_flFadeScale;
	CNetworkVar( bool, m_bAlternateSorting );

	//Adrian
	unsigned char					m_iTextureFrameIndex;

	// Bbox visualization
	unsigned char					m_fBBoxVisFlags;



//#if !defined( NO_ENTITY_PREDICTION )
//	// Player who is driving my simulation
//	CHandle< CBasePlayer >			m_hPlayerSimulationOwner;
//#endif

	// The owner!
	EHANDLE							m_hOwnerEntity;
	EHANDLE							m_hEffectEntity;
	
	// This is a random seed used by the networking code to allow client - side prediction code
	//  randon number generators to spit out the same random numbers on both sides for a particular
	//  usercmd input.
	static int						m_nPredictionRandomSeed;
	static C_BasePlayer				*m_pPredictionPlayer;

	static bool						s_bInterpolate;
	

	AimEntsListHandle_t				m_AimEntsListHandle;

	
public:
	float							m_fRenderingClipPlane[4]; //world space clip plane when drawing
	bool							m_bEnableRenderingClipPlane; //true to use the custom clip plane when drawing
	float *							GetRenderClipPlane( void ); // Rendering clip plane, should be 4 floats, return value of NULL indicates a disabled render clip plane

public:

	void AddToInterpolationList();
protected:
	void RemoveFromInterpolationList();
	unsigned short m_InterpolationListEntry;	// Entry into g_InterpolationList (or g_InterpolationList.InvalidIndex if not in the list).
	
	void AddToTeleportList();
	void RemoveFromTeleportList();
	unsigned short m_TeleportListEntry;



#ifdef TF_CLIENT_DLL
	// TF prevents drawing of any entity attached to players that aren't items in the inventory of the player.
	// This is to prevent servers creating fake cosmetic items and attaching them to players.
public:
	virtual bool ValidateEntityAttachedToPlayer( bool &bShouldRetry );
	bool EntityDeemedInvalid( void ) { return (m_bValidatedOwner && m_bDeemedInvalid); }
protected:
	bool m_bValidatedOwner;
	bool m_bDeemedInvalid;
	bool m_bWasDeemedInvalid;
	RenderMode_t m_PreviousRenderMode;
	color32 m_PreviousRenderColor;
#endif
};

EXTERN_RECV_TABLE(DT_BaseEntity);

inline bool FClassnameIs( C_BaseEntity *pEntity, const char *szClassname )
{ 
	Assert( pEntity );
	if ( pEntity == NULL )
		return false;

	return !strcmp( pEntity->GetClassname(), szClassname ) ? true : false; 
}

#define SetThink( a ) GetEngineObject()->ThinkSet( static_cast <void (CBaseEntity::*)(void)> (a), 0, NULL )
#define SetContextThink( a, b, context ) GetEngineObject()->ThinkSet( static_cast <void (CBaseEntity::*)(void)> (a), (b), context )

#ifdef _DEBUG
#define SetTouch( a ) TouchSet( static_cast <void (C_BaseEntity::*)(C_BaseEntity *)> (a), #a )

#else
#define SetTouch( a ) m_pfnTouch = static_cast <void (C_BaseEntity::*)(C_BaseEntity *)> (a)

#endif

//-----------------------------------------------------------------------------
// An inline version the game code can use
//-----------------------------------------------------------------------------
inline CParticleProperty *C_BaseEntity::ParticleProp()
{
	return &m_Particles;
}

inline const CParticleProperty *C_BaseEntity::ParticleProp() const
{
	return &m_Particles;
}

//-----------------------------------------------------------------------------
// Purpose: Returns whether this entity was created on the client.
//-----------------------------------------------------------------------------
inline bool C_BaseEntity::IsServerEntity( void )
{
	return entindex() >= 0 && entindex() < MAX_EDICTS;
}

inline C_BaseEntity	*C_BaseEntity::Instance( IClientEntity *ent )
{
	return ent ? ent->GetBaseEntity() : NULL;
}

// For debugging shared code
inline bool	C_BaseEntity::IsServer( void )
{
	return false;
}

inline bool	C_BaseEntity::IsClient( void )
{
	return true;
}

inline const char *C_BaseEntity::GetDLLType( void )
{
	return "client";
}


//-----------------------------------------------------------------------------
// Methods relating to bounds
//-----------------------------------------------------------------------------
inline const Vector& C_BaseEntity::WorldAlignMins( ) const
{
	Assert( !GetEngineObject()->IsBoundsDefinedInEntitySpace() );
	Assert(GetEngineObject()->GetCollisionAngles() == vec3_angle );
	return GetEngineObject()->OBBMins();
}

inline const Vector& C_BaseEntity::WorldAlignMaxs( ) const
{
	Assert( !GetEngineObject()->IsBoundsDefinedInEntitySpace() );
	Assert(GetEngineObject()->GetCollisionAngles() == vec3_angle );
	return GetEngineObject()->OBBMaxs();
}

inline const Vector& C_BaseEntity::WorldAlignSize( ) const
{
	Assert( !GetEngineObject()->IsBoundsDefinedInEntitySpace() );
	Assert(GetEngineObject()->GetCollisionAngles() == vec3_angle );
	return GetEngineObject()->OBBSize();
}

//inline float C_BaseEntity::BoundingRadius() const
//{
//	return GetEngineObject()->BoundingRadius();
//}

inline const QAngle& C_BaseEntity::GetLocalAngularVelocity( ) const
{
	return m_vecAngVelocity;
}

inline const Vector& C_BaseEntity::GetBaseVelocity() const 
{ 
	return m_vecBaseVelocity; 
}

inline void	C_BaseEntity::SetBaseVelocity( const Vector& v ) 
{ 
	m_vecBaseVelocity = v; 
}


inline int C_BaseEntity::GetWaterLevel() const
{
	return m_nWaterLevel;
}

inline void C_BaseEntity::SetWaterLevel( int nLevel )
{
	m_nWaterLevel = nLevel;
}



inline const color32 C_BaseEntity::GetRenderColor() const
{
	return m_clrRender.Get();
}

inline void C_BaseEntity::SetRenderColor( byte r, byte g, byte b )
{
	color32 clr = { r, g, b, m_clrRender->a };
	m_clrRender = clr;
}

inline void C_BaseEntity::SetRenderColor( byte r, byte g, byte b, byte a )
{
	color32 clr = { r, g, b, a };
	m_clrRender = clr;
}

inline void C_BaseEntity::SetRenderColorR( byte r )
{
	SetRenderColor( r, GetRenderColor().g, GetRenderColor().b );
}

inline void C_BaseEntity::SetRenderColorG( byte g )
{
	SetRenderColor( GetRenderColor().r, g, GetRenderColor().b );
}

inline void C_BaseEntity::SetRenderColorB( byte b )
{
	SetRenderColor( GetRenderColor().r, GetRenderColor().g, b );
}

inline void C_BaseEntity::SetRenderColorA( byte a )
{
	SetRenderColor( GetRenderColor().r, GetRenderColor().g, GetRenderColor().b, a );
}

inline ClientRenderHandle_t C_BaseEntity::GetRenderHandle() const 
{ 
	return m_hRender; 
}

inline ClientRenderHandle_t& C_BaseEntity::RenderHandle()
{
	return m_hRender;
}

#ifdef SIXENSE

inline const Vector& CBaseEntity::GetEyeOffset() const 
{ 
	return m_vecEyeOffset; 
}

inline void CBaseEntity::SetEyeOffset( const Vector& v ) 
{ 
	m_vecEyeOffset = v; 
}

inline const QAngle & CBaseEntity::GetEyeAngleOffset() const 
{ 
	return m_EyeAngleOffset; 
}

inline void CBaseEntity::SetEyeAngleOffset( const QAngle & qa ) 
{ 
	m_EyeAngleOffset = qa; 
}

#endif

//-----------------------------------------------------------------------------
// Methods to cast away const
//-----------------------------------------------------------------------------
inline Vector C_BaseEntity::EyePosition( void ) const
{
	return const_cast<C_BaseEntity*>(this)->EyePosition();
}

inline const QAngle &C_BaseEntity::EyeAngles( void ) const		// Direction of eyes in world space
{
	return const_cast<C_BaseEntity*>(this)->EyeAngles();
}

inline const QAngle &C_BaseEntity::LocalEyeAngles( void ) const	// Direction of eyes
{
	return const_cast<C_BaseEntity*>(this)->LocalEyeAngles();
}

inline Vector	C_BaseEntity::EarPosition( void ) const			// position of ears
{
	return const_cast<C_BaseEntity*>(this)->EarPosition();
}



//-----------------------------------------------------------------------------
// Should we be interpolating?
//-----------------------------------------------------------------------------
inline bool	C_BaseEntity::IsInterpolationEnabled()
{
	return s_bInterpolate;
}

//-----------------------------------------------------------------------------
// Should we be interpolating during this frame? (was EF_NOINTERP)
//-----------------------------------------------------------------------------
inline bool C_BaseEntity::IsNoInterpolationFrame()
{
	return m_ubOldInterpolationFrame != m_ubInterpolationFrame;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : handle - 
// Output : inline void
//-----------------------------------------------------------------------------
inline void C_BaseEntity::SetToolHandle( HTOOLHANDLE handle )
{
#ifndef NO_TOOLFRAMEWORK
	m_ToolHandle = handle;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  :  - 
// Output : inline HTOOLHANDLE
//-----------------------------------------------------------------------------
inline HTOOLHANDLE C_BaseEntity::GetToolHandle() const
{
#ifndef NO_TOOLFRAMEWORK
	return m_ToolHandle;
#else
	return (HTOOLHANDLE)0;
#endif
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
inline bool C_BaseEntity::IsEnabledInToolView() const
{
#ifndef NO_TOOLFRAMEWORK
	return m_bEnabledInToolView;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  :  - 
// Output : inline bool
//-----------------------------------------------------------------------------
inline bool C_BaseEntity::ShouldRecordInTools() const
{
#ifndef NO_TOOLFRAMEWORK
	return m_bRecordInTools;
#else
	return true;
#endif
}

//C_BaseEntity *CreateEntityByName( const char *className );

#endif // C_BASEENTITY_H
