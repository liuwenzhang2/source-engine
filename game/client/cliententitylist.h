//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//
#if !defined( CLIENTENTITYLIST_H )
#define CLIENTENTITYLIST_H
#ifdef _WIN32
#pragma once
#endif

#include "tier0/dbg.h"
#include "icliententitylist.h"
#include "iclientunknown.h"
#include "utllinkedlist.h"
#include "utlvector.h"
#include "icliententityinternal.h"
#include "ispatialpartition.h"
#include "cdll_util.h"
#include "entitylist_base.h"
#include "utlmap.h"
#include "client_class.h"
#include "interpolatedvar.h"
#include "bone_accessor.h"
#include "collisionproperty.h"
#include "baseentity_shared.h"
//#include "c_baseanimating.h"
#include "gamestringpool.h"
#include "saverestoretypes.h"
#include "physics_saverestore.h"
#include "mouthinfo.h"
//#include "ragdoll_shared.h"
#include "rope_physics.h"
#include "rope_shared.h"
#include "bone_setup.h"
#include "jigglebones.h"
#include "tier0/vprof.h"
#include "vstdlib/jobthread.h"
#include "bone_merge_cache.h"
#include "toolframework_client.h"
#include "inetchannelinfo.h"
#include "usercmd.h"
#include "engine/ivdebugoverlay.h"
#include "client_factorylist.h"
#include "movevars_shared.h"
#include "vphysics_sound.h"
#include "engine/IEngineSound.h"
#include "soundenvelope.h"
#include "physics_shared.h"
#include "ragdoll_shared.h"

//class C_Beam;
//class C_BaseViewModel;
//class C_BaseEntity;

extern IVEngineClient* engine;



class CAttachmentData
{
public:
	matrix3x4_t	m_AttachmentToWorld;
	QAngle	m_angRotation;
	Vector	m_vOriginVelocity;
	int		m_nLastFramecount : 31;
	int		m_bAnglesComputed : 1;
};

typedef unsigned int			AimEntsListHandle_t;
#define		INVALID_AIMENTS_LIST_HANDLE		(AimEntsListHandle_t)~0
typedef unsigned int			ClientSideAnimationListHandle_t;
#define		INVALID_CLIENTSIDEANIMATION_LIST_HANDLE	(ClientSideAnimationListHandle_t)~0

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
// derive from this so we can add save/load data to it
struct c_game_shadowcontrol_params_t : public hlshadowcontrol_params_t
{
	DECLARE_SIMPLE_DATADESC();
};

//-----------------------------------------------------------------------------
class C_GrabControllerInternal : public IGrabControllerClient, public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();

public:

	C_GrabControllerInternal(void);
	~C_GrabControllerInternal(void);
	void AttachEntity(C_BaseEntity* pPlayer, C_BaseEntity* pEntity, IPhysicsObject* pPhys, bool bIsMegaPhysCannon, const Vector& vGrabPosition, bool bUseGrabPosition);
	void DetachEntity(bool bClearVelocity);
	void OnRestore();

	bool UpdateObject(C_BaseEntity* pPlayer, float flError);

	void SetTargetPosition(const Vector& target, const QAngle& targetOrientation);
	void GetTargetPosition(Vector* target, QAngle* targetOrientation);
	float ComputeError();
	float GetLoadWeight(void) const { return m_flLoadWeight; }
	void SetAngleAlignment(float alignAngleCosine) { m_angleAlignment = alignAngleCosine; }
	void SetIgnorePitch(bool bIgnore) { m_bIgnoreRelativePitch = bIgnore; }
	QAngle TransformAnglesToPlayerSpace(const QAngle& anglesIn, C_BaseEntity* pPlayer);
	QAngle TransformAnglesFromPlayerSpace(const QAngle& anglesIn, C_BaseEntity* pPlayer);

	C_BaseEntity* GetAttached() { return (C_BaseEntity*)m_attachedEntity; }
	const QAngle& GetAttachedAnglesPlayerSpace() { return m_attachedAnglesPlayerSpace; }
	void SetAttachedAnglesPlayerSpace(const QAngle& attachedAnglesPlayerSpace) { m_attachedAnglesPlayerSpace = attachedAnglesPlayerSpace; }
	const Vector& GetAttachedPositionObjectSpace() { return m_attachedPositionObjectSpace; }
	void SetAttachedPositionObjectSpace(const Vector& attachedPositionObjectSpace) { m_attachedPositionObjectSpace = attachedPositionObjectSpace; }

	IMotionEvent::simresult_e Simulate(IPhysicsMotionController* pController, IPhysicsObject* pObject, float deltaTime, Vector& linear, AngularImpulse& angular);
	float GetSavedMass(IPhysicsObject* pObject);
	void GetSavedParamsForCarriedPhysObject(IPhysicsObject* pObject, float* pSavedMassOut, float* pSavedRotationalDampingOut);
#ifndef CLIENT_DLL
	bool IsObjectAllowedOverhead(C_BaseEntity* pEntity);
#endif
	//set when a held entity is penetrating another through a portal. Needed for special fixes
	void SetPortalPenetratingEntity(C_BaseEntity* pPenetrated);

private:
	// Compute the max speed for an attached object
	void ComputeMaxSpeed(C_BaseEntity* pEntity, IPhysicsObject* pPhysics);

	c_game_shadowcontrol_params_t m_shadow;
	float			m_timeToArrive;
	float			m_errorTime;
	float			m_error;
	float			m_contactAmount;
	float			m_angleAlignment;
	bool			m_bCarriedEntityBlocksLOS;
	bool			m_bIgnoreRelativePitch;

	float			m_flLoadWeight;
	float			m_savedRotDamping[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	float			m_savedMass[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	EHANDLE			m_attachedEntity;
	QAngle			m_vecPreferredCarryAngles;
	bool			m_bHasPreferredCarryAngles;
	float			m_flDistanceOffset;

	QAngle			m_attachedAnglesPlayerSpace;
	Vector			m_attachedPositionObjectSpace;

	IPhysicsMotionController* m_controller;

	// NVNT player controlling this grab controller
	C_BaseEntity*	m_pControllingPlayer;
#ifndef CLIENT_DLL
	bool			m_bAllowObjectOverhead; // Can the player hold this object directly overhead? (Default is NO)
#endif // !CLIENT_DLL
	//set when a held entity is penetrating another through a portal. Needed for special fixes
	EHANDLE			m_PenetratedEntity;
	int				m_frameCount;
};

class C_EngineObjectInternal : public IEngineObjectClient {
	friend class CPortalCollideableEnumerator;
	template<class T> friend class CClientEntityList;
public:
	DECLARE_CLASS_NOBASE(C_EngineObjectInternal);
	DECLARE_PREDICTABLE();
	// data description
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();

	const CBaseHandle& GetRefEHandle() const {
		return m_RefEHandle;
	}

	int entindex() const {
		CBaseHandle Handle = this->GetRefEHandle();
		if (Handle == INVALID_ENTITY_HANDLE) {
			return -1;
		}
		else {
			return Handle.GetEntryIndex();
		}
	};

	RecvTable* GetRecvTable() { return GetClientClass()->m_pRecvTable; }
	//ClientClass* GetClientClass() { return NULL; }
	void* GetDataTableBasePtr() { return this; }
	void NotifyShouldTransmit(ShouldTransmitState_t state) { m_pOuter->NotifyShouldTransmit(state); }
	bool IsDormant(void) { return m_pOuter->IsDormant(); }
	void ReceiveMessage(int classID, bf_read& msg) { m_pOuter->ReceiveMessage(classID, msg); }
	void SetDestroyedOnRecreateEntities(void) { m_pOuter->SetDestroyedOnRecreateEntities(); }
	// This just picks one of the routes to IClientUnknown.
	virtual IClientUnknown* GetIClientUnknown() { return m_pOuter; }
	virtual bool LODTest() { return true; }   // NOTE: UNUSED
	// memory handling, uses calloc so members are zero'd out on instantiation
	void* operator new(size_t stAllocateBlock);
	void* operator new[](size_t stAllocateBlock);
	void* operator new(size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine);
	void* operator new[](size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine);
	void operator delete(void* pMem);
	void operator delete(void* pMem, int nBlockUse, const char* pFileName, int nLine) { operator delete(pMem); }

	C_EngineObjectInternal(IClientEntityList* pClientEntityList, int iForceEdictIndex, int iSerialNum)
		:m_pClientEntityList(pClientEntityList), m_RefEHandle(iForceEdictIndex, iSerialNum),
		m_iv_vecOrigin("C_BaseEntity::m_iv_vecOrigin", &m_vecOrigin, LATCH_SIMULATION_VAR),
		m_iv_angRotation("C_BaseEntity::m_iv_angRotation", &m_angRotation, LATCH_SIMULATION_VAR),
		m_iv_vecVelocity("C_BaseEntity::m_iv_vecVelocity", &m_vecVelocity, LATCH_SIMULATION_VAR),
		m_iv_flCycle("C_BaseAnimating::m_iv_flCycle", &m_flCycle, LATCH_ANIMATION_VAR),
		m_iv_flPoseParameter("C_BaseAnimating::m_iv_flPoseParameter", m_flPoseParameter, LATCH_ANIMATION_VAR),
		m_iv_flEncodedController("C_BaseAnimating::m_iv_flEncodedController", m_flEncodedController, LATCH_ANIMATION_VAR),
		m_iv_ragPos("C_ServerRagdoll::m_iv_ragPos", m_ragPos, LATCH_SIMULATION_VAR),
		m_iv_ragAngles("C_ServerRagdoll::m_iv_ragAngles", m_ragAngles, LATCH_SIMULATION_VAR)
	{
		AddVar(&m_iv_vecOrigin);//&m_vecOrigin, , LATCH_SIMULATION_VAR
		AddVar(&m_iv_angRotation);//&m_angRotation, , LATCH_SIMULATION_VAR
		AddVar(&m_iv_ragPos);//, LATCH_SIMULATION_VAR
		AddVar(&m_iv_ragAngles);//, LATCH_SIMULATION_VAR

#ifdef _DEBUG
		m_vecAbsOrigin = vec3_origin;
		m_angAbsRotation = vec3_angle;
		m_vecNetworkOrigin.Init();
		m_angNetworkAngles.Init();
		m_vecAbsOrigin.Init();
		//	m_vecAbsAngVelocity.Init();
		m_vecVelocity.Init();
		m_vecAbsVelocity.Init();
		m_iCurrentThinkContext = NO_THINK_CONTEXT;
#endif
		// Removing this until we figure out why velocity introduces view hitching.
		// One possible fix is removing the player->ResetLatched() call in CGameMovement::FinishDuck(), 
		// but that re-introduces a third-person hitching bug.  One possible cause is the abrupt change
		// in player size/position that occurs when ducking, and how prediction tries to work through that.
		//
		// AddVar( &m_vecVelocity, &m_iv_vecVelocity, LATCH_SIMULATION_VAR );
		for (int i = 0; i < MULTIPLAYER_BACKUP; i++) {
			m_pIntermediateData[i] = NULL;
			m_pOuterIntermediateData[i] = NULL;
		}
		m_iClassname = NULL_STRING;
		m_iParentAttachment = 0;
		m_iEFlags = 0;
		m_spawnflags = 0;
		touchStamp = 0;
		SetCheckUntouch(false);
		m_fDataObjectTypes = 0;
		SetModelName(NULL_STRING);
		m_nModelIndex = 0;
		m_Collision.Init(this);
		SetSolid(SOLID_NONE);
		SetSolidFlags(0);
		m_flFriction = 0.0f;
		m_flGravity = 0.0f;
		m_bSimulatedEveryTick = false;
		m_bAnimatedEveryTick = false;
		m_rgflCoordinateFrame[0][0] = 1.0f;
		m_rgflCoordinateFrame[1][1] = 1.0f;
		m_rgflCoordinateFrame[2][2] = 1.0f;
		m_flAnimTime = 0;
		m_flSimulationTime = 0;
		m_flProxyRandomValue = 0.0f;
		m_DataChangeEventRef = -1;
		// Assume false.  Derived classes might fill in a receive table entry
// and in that case this would show up as true
		m_bClientSideAnimation = false;
		m_vecForce.Init();
		m_nForceBone = -1;
		m_nSkin = 0;
		m_nBody = 0;
		m_nHitboxSet = 0;
		m_flModelScale = 1.0f;
		m_flOldModelScale = 0.0f;
		int i;
		for (i = 0; i < ARRAYSIZE(m_flEncodedController); i++)
		{
			m_flEncodedController[i] = 0.0f;
		}
		m_pModel = NULL;
		m_pStudioHdr = NULL;
		m_hStudioHdr = MDLHANDLE_INVALID;
		m_flCycle = 0;
		m_flOldCycle = 0;
		m_flPlaybackRate = 1.0f;
		m_nMuzzleFlashParity = 0;
		m_nOldMuzzleFlashParity = 0;
		Q_memset(&m_mouth, 0, sizeof(m_mouth));
		m_nPrevNewSequenceParity = -1;
		m_bReceivedSequence = false;
		m_pPhysicsObject = NULL;
		m_ragdoll.listCount = 0;
		m_vecLastOrigin.Init();
		m_flLastOriginChangeTime = -1.0f;

		m_lastUpdate = -FLT_MAX;
		m_nPrevSequence = -1;
		m_nRestoreSequence = -1;
		m_builtRagdoll = false;
		m_vecPreRagdollMins = vec3_origin;
		m_vecPreRagdollMaxs = vec3_origin;

		//m_bStoreRagdollInfo = false;
		//m_pRagdollInfo = NULL;
		m_flLastBoneChangeTime = -FLT_MAX;
		m_nRenderFX = 0;
		m_flBlendWeightCurrent = 0.0f;
		m_nOverlaySequence = -1;
		//m_pRagdoll		= NULL;
		//m_hitboxBoneCacheHandle = 0;



		//AddBaseAnimatingInterpolatedVars();

		m_iMostRecentModelBoneCounter = 0xFFFFFFFF;
		m_iMostRecentBoneSetupRequest = pClientEntityList->GetPreviousBoneCounter() - 1;
		m_flLastBoneSetupTime = -FLT_MAX;


		m_pJiggleBones = NULL;
		m_pBoneMergeCache = NULL;
		m_pIk = NULL;
		m_EntClientFlags = 0;
		m_ragdollListCount = 0;
		m_ModelInstance = MODEL_INSTANCE_INVALID;
		m_ShadowHandle = CLIENTSHADOW_INVALID_HANDLE;
		m_hRender = INVALID_CLIENT_RENDER_HANDLE;
		m_AimEntsListHandle = INVALID_AIMENTS_LIST_HANDLE;
		m_ClientSideAnimationListHandle = INVALID_CLIENTSIDEANIMATION_LIST_HANDLE;
		m_InterpolationListEntry = 0xFFFF;
		m_TeleportListEntry = 0xFFFF;
		// Assume drawing everything
		m_bReadyToDraw = true;
	}

	virtual ~C_EngineObjectInternal()
	{
		RemoveFromClientSideAnimationList();
		ClearDataChangedEvent(m_DataChangeEventRef);
		// Are we in the partition?
		DestroyPartitionHandle();
		InvalidateMdlCache();
		ClearRagdoll();
		//delete m_pRagdollInfo;
		VPhysicsDestroyObject();
		int i = m_pClientEntityList->GetPreviousBoneSetups().Find(this);
		if (i != -1)
			m_pClientEntityList->GetPreviousBoneSetups().FastRemove(i);
		delete m_pIk;
		delete m_pBoneMergeCache;
		//Studio_DestroyBoneCache(m_hitboxBoneCacheHandle);
		delete m_pJiggleBones;
		m_pOuter = NULL;
	}

	

	void Init(C_BaseEntity* pOuter) {
		m_pOuter = pOuter;
		m_nCreationTick = gpGlobals->tickcount;
		CreatePartitionHandle();
		AddBaseAnimatingInterpolatedVars();
	}

	IClientEntity* GetClientEntity() {
		return m_pOuter;
	}

	C_BaseEntity* GetOuter() {
		return m_pOuter;
	}

	IHandleEntity* GetHandleEntity() const {
		return m_pOuter;
	}

	void ParseMapData(IEntityMapData* mapData);
	bool KeyValue(const char* szKeyName, const char* szValue);
	int Save(ISave& save);
	int Restore(IRestore& restore);
	void OnSave();
	void OnRestore();

	// NOTE: Setting the abs velocity in either space will cause a recomputation
	// in the other space, so setting the abs velocity will also set the local vel
	void SetAbsVelocity(const Vector& vecVelocity);
	const Vector& GetAbsVelocity();
	const Vector& GetAbsVelocity() const;

	// Sets abs angles, but also sets local angles to be appropriate
	void SetAbsOrigin(const Vector& origin);
	const Vector& GetAbsOrigin(void);
	const Vector& GetAbsOrigin(void) const;

	void SetAbsAngles(const QAngle& angles);
	const QAngle& GetAbsAngles(void);
	const QAngle& GetAbsAngles(void) const;

	void SetLocalOrigin(const Vector& origin);
	void SetLocalOriginDim(int iDim, vec_t flValue);
	const Vector& GetLocalOrigin(void) const;
	const vec_t GetLocalOriginDim(int iDim) const;		// You can use the X_INDEX, Y_INDEX, and Z_INDEX defines here.

	void SetLocalAngles(const QAngle& angles);
	void SetLocalAnglesDim(int iDim, vec_t flValue);
	const QAngle& GetLocalAngles(void) const;
	const vec_t GetLocalAnglesDim(int iDim) const;		// You can use the X_INDEX, Y_INDEX, and Z_INDEX defines here.

	void SetLocalVelocity(const Vector& vecVelocity);
	//Vector& GetLocalVelocity();
	const Vector& GetLocalVelocity() const;

	const Vector& GetPrevLocalOrigin() const;
	const QAngle& GetPrevLocalAngles() const;

	ITypedInterpolatedVar< QAngle >& GetRotationInterpolator();
	ITypedInterpolatedVar< Vector >& GetOriginInterpolator();

	// Determine approximate velocity based on updates from server
	void EstimateAbsVelocity(Vector& vel);
	// Computes absolute position based on hierarchy
	void CalcAbsolutePosition();
	void CalcAbsoluteVelocity();

	// Unlinks from hierarchy
	// Set the movement parent. Your local origin and angles will become relative to this parent.
	// If iAttachment is a valid attachment on the parent, then your local origin and angles 
	// are relative to the attachment on this entity.
	void SetParent(IEngineObjectClient* pParentEntity, int iParentAttachment = 0);
	void UnlinkChild(IEngineObjectClient* pChild);
	void LinkChild(IEngineObjectClient* pChild);
	void HierarchySetParent(IEngineObjectClient* pNewParent);
	void UnlinkFromHierarchy();

	// Methods relating to traversing hierarchy
	C_EngineObjectInternal* GetMoveParent(void) const;
	void SetMoveParent(IEngineObjectClient* pMoveParent);
	C_EngineObjectInternal* GetRootMoveParent();
	C_EngineObjectInternal* FirstMoveChild(void) const;
	void SetFirstMoveChild(IEngineObjectClient* pMoveChild);
	C_EngineObjectInternal* NextMovePeer(void) const;
	void SetNextMovePeer(IEngineObjectClient* pMovePeer);
	C_EngineObjectInternal* MovePrevPeer(void) const;
	void SetMovePrevPeer(IEngineObjectClient* pMovePrevPeer);

	void ResetRgflCoordinateFrame();
	// Returns the entity-to-world transform
	matrix3x4_t& EntityToWorldTransform();
	const matrix3x4_t& EntityToWorldTransform() const;

	// Some helper methods that transform a point from entity space to world space + back
	void EntityToWorldSpace(const Vector& in, Vector* pOut) const;
	void WorldToEntitySpace(const Vector& in, Vector* pOut) const;

	void GetVectors(Vector* forward, Vector* right, Vector* up) const;

	// This function gets your parent's transform. If you're parented to an attachment,
// this calculates the attachment's transform and gives you that.
//
// You must pass in tempMatrix for scratch space - it may need to fill that in and return it instead of 
// pointing you right at a variable in your parent.
	const matrix3x4_t& GetParentToWorldTransform(matrix3x4_t& tempMatrix);

	// Computes the abs position of a point specified in local space
	void ComputeAbsPosition(const Vector& vecLocalPosition, Vector* pAbsPosition);

	// Computes the abs position of a direction specified in local space
	void ComputeAbsDirection(const Vector& vecLocalDirection, Vector* pAbsDirection);

public:

	void AddVar(IInterpolatedVar* watcher, bool bSetup = false);
	void RemoveVar(IInterpolatedVar* watcher, bool bAssert = true);
	VarMapping_t* GetVarMapping();

	// Set appropriate flags and store off data when these fields are about to change
	void OnLatchInterpolatedVariables(int flags);
	// For predictable entities, stores last networked value
	void OnStoreLastNetworkedValue();

	void Interp_SetupMappings();

	// Returns 1 if there are no more changes (ie: we could call RemoveFromInterpolationList).
	int Interp_Interpolate(float currentTime);

	void Interp_RestoreToLastNetworked();
	void Interp_UpdateInterpolationAmounts();
	void Interp_Reset();
	void Interp_HierarchyUpdateInterpolationAmounts();



	// Returns INTERPOLATE_STOP or INTERPOLATE_CONTINUE.
	// bNoMoreChanges is set to 1 if you can call RemoveFromInterpolationList on the entity.
	int BaseInterpolatePart1(float& currentTime, Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int& bNoMoreChanges);
	void BaseInterpolatePart2(Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int nChangeFlags);

	void AllocateIntermediateData(void);
	void DestroyIntermediateData(void);
	void ShiftIntermediateDataForward(int slots_to_remove, int previous_last_slot);

	void* GetPredictedFrame(int framenumber);
	void* GetOuterPredictedFrame(int framenumber);
	void* GetOriginalNetworkDataObject(void);
	void* GetOuterOriginalNetworkDataObject(void);
	bool IsIntermediateDataAllocated(void) const;

	void PreEntityPacketReceived(int commands_acknowledged);
	void PostEntityPacketReceived(void);
	bool PostNetworkDataReceived(int commands_acknowledged);

	int SaveData(const char* context, int slot, int type);
	int RestoreData(const char* context, int slot, int type);

	void SetClassname(const char* className)
	{
		m_iClassname = AllocPooledString(className);
	}
	const string_t& GetClassname() const {
		return 	m_iClassname;
	}

	IClientNetworkable* GetClientNetworkable() {
		return this;
	}

	const Vector& GetNetworkOrigin() const;
	const QAngle& GetNetworkAngles() const;
	IEngineObjectClient* GetNetworkMoveParent();

	void SetNetworkOrigin(const Vector& org);
	void SetNetworkAngles(const QAngle& ang);
	void SetNetworkMoveParent(IEngineObjectClient* pMoveParent);

	// Returns the attachment point index on our parent that our transform is relative to.
	// 0 if we're relative to the parent's absorigin and absangles.
	unsigned char GetParentAttachment() const;
	unsigned char GetParentAttachment() {
		return m_iParentAttachment;
	}

	void AddFlag(int flags);
	void RemoveFlag(int flagsToRemove);
	void ToggleFlag(int flagToToggle);
	int GetFlags(void) const;
	void ClearFlags();
	int GetEFlags() const;
	void SetEFlags(int iEFlags);
	void AddEFlags(int nEFlagMask);
	void RemoveEFlags(int nEFlagMask);
	bool IsEFlagSet(int nEFlagMask) const;
	// checks to see if the entity is marked for deletion
	bool IsMarkedForDeletion(void);
	int GetSpawnFlags(void) const;
	void SetCheckUntouch(bool check);
	bool GetCheckUntouch() const;
	int GetTouchStamp();
	void ClearTouchStamp();

	// Externalized data objects ( see sharreddefs.h for DataObjectType_t )
	bool HasDataObjectType(int type) const;
	void AddDataObjectType(int type);
	void RemoveDataObjectType(int type);

	void* GetDataObject(int type);
	void* CreateDataObject(int type);
	void DestroyDataObject(int type);
	void DestroyAllDataObjects(void);

	void Clear(void) {
		m_vecAbsOrigin.Init();
		m_angAbsRotation.Init();
		m_vecVelocity.Init();
		m_vecAbsVelocity.Init();//GetLocalVelocity ???
		m_pOriginalData = NULL;
		m_pOuterOriginalData = NULL;
		for (int i = 0; i < MULTIPLAYER_BACKUP; i++) {
			m_pIntermediateData[i] = NULL;
			m_pOuterIntermediateData[i] = NULL;
		}
		m_nModelIndex = 0;
		ClearFlags();
		ClearEffects();
		m_nLastThinkTick = gpGlobals->tickcount;
		SetMoveCollide(MOVECOLLIDE_DEFAULT);
		SetMoveType(MOVETYPE_NONE);
		m_flAnimTime = 0;
		m_flSimulationTime = 0;
		m_nCreationTick = -1;
		m_nSkin = 0;
		m_nBody = 0;
		m_nHitboxSet = 0;
		m_flModelScale = 1.0f;
		m_flOldModelScale = 0.0f;
		InvalidateMdlCache();
		m_pModel = NULL;
		m_flCycle = 0;
		m_flOldCycle = 0;
		m_flPlaybackRate = 1.0f;
		m_nMuzzleFlashParity = 0;
		m_nOldMuzzleFlashParity = 0;
		Q_memset(&m_mouth, 0, sizeof(m_mouth));
		m_nPrevNewSequenceParity = -1;
		m_bReceivedSequence = false;
		m_elementCount = 0;
		m_nRenderFX = 0;
#ifndef NO_TOOLFRAMEWORK
		m_bEnabledInToolView = true;
		m_bToolRecording = false;
		m_ToolHandle = 0;
		m_nLastRecordedFrame = -1;
		m_bRecordInTools = true;
#endif
		m_ModelInstance = MODEL_INSTANCE_INVALID;
		m_ShadowHandle = CLIENTSHADOW_INVALID_HANDLE;
		m_hRender = INVALID_CLIENT_RENDER_HANDLE;
		m_AimEntsListHandle = INVALID_AIMENTS_LIST_HANDLE;
		m_ClientSideAnimationListHandle = INVALID_CLIENTSIDEANIMATION_LIST_HANDLE;
		m_InterpolationListEntry = 0xFFFF;
		m_TeleportListEntry = 0xFFFF;
		// Assume drawing everything
		m_bReadyToDraw = true;
	}

	virtual void OnPositionChanged();
	virtual void OnAnglesChanged();
	virtual void OnAnimationChanged();
	// Invalidates the abs state of all children
	void InvalidatePhysicsRecursive(int nChangeFlags);
	
	// HACKHACK:Get the trace_t from the last physics touch call (replaces the even-hackier global trace vars)
	const trace_t& GetTouchTrace(void);
	// FIXME: Should be private, but I can't make em private just yet
	void PhysicsImpact(IEngineObjectClient* other, trace_t& trace);
	void PhysicsMarkEntitiesAsTouching(IEngineObjectClient* other, trace_t& trace);
	void PhysicsMarkEntitiesAsTouchingEventDriven(IEngineObjectClient* other, trace_t& trace);
	clienttouchlink_t* PhysicsMarkEntityAsTouched(IEngineObjectClient* other);
	void PhysicsTouch(IEngineObjectClient* pentOther);
	void PhysicsStartTouch(IEngineObjectClient* pentOther);
	bool IsCurrentlyTouching(void) const;

	// Physics helper
	void PhysicsCheckForEntityUntouch(void);
	void PhysicsNotifyOtherOfUntouch(IEngineObjectClient* ent);
	void PhysicsRemoveTouchedList();
	void PhysicsRemoveToucher(clienttouchlink_t* link);

	clientgroundlink_t* AddEntityToGroundList(IEngineObjectClient* other);
	void PhysicsStartGroundContact(IEngineObjectClient* pentOther);
	void PhysicsNotifyOtherOfGroundRemoval(IEngineObjectClient* ent);
	void PhysicsRemoveGround(clientgroundlink_t* link);
	void PhysicsRemoveGroundList();

	void SetGroundEntity(IEngineObjectClient* ground);
	C_EngineObjectInternal* GetGroundEntity(void);
	C_EngineObjectInternal* GetGroundEntity(void) const { return const_cast<C_EngineObjectInternal*>(this)->GetGroundEntity(); }
	void SetGroundChangeTime(float flTime);
	float GetGroundChangeTime(void);

	void SetModelName(string_t name);
	string_t GetModelName(void) const;
	int GetModelIndex(void) const;
	void SetModelIndex(int index);

	// An inline version the game code can use
	//CCollisionProperty* CollisionProp();
	//const CCollisionProperty* CollisionProp() const;
	ICollideable* GetCollideable();
	// This defines collision bounds *in whatever space is currently defined by the solid type*
	//	SOLID_BBOX:		World Align
	//	SOLID_OBB:		Entity space
	//	SOLID_BSP:		Entity space
	//	SOLID_VPHYSICS	Not used
	void SetCollisionBounds(const Vector& mins, const Vector& maxs);
	SolidType_t GetSolid(void) const;
	bool IsSolid() const;
	void SetSolid(SolidType_t val);	// Set to one of the SOLID_ defines.
	void AddSolidFlags(int nFlags);
	void RemoveSolidFlags(int nFlags);
	bool IsSolidFlagSet(int flagMask) const;
	void SetSolidFlags(int nFlags);
	int GetSolidFlags(void) const;
	const Vector& GetCollisionOrigin() const;
	const QAngle& GetCollisionAngles() const;
	const Vector& OBBMinsPreScaled() const;
	const Vector& OBBMaxsPreScaled() const;
	const Vector& OBBMins() const;
	const Vector& OBBMaxs() const;
	const Vector& OBBSize() const;
	const Vector& OBBCenter() const;
	const Vector& WorldSpaceCenter() const;
	void WorldSpaceAABB(Vector* pWorldMins, Vector* pWorldMaxs) const;
	void WorldSpaceSurroundingBounds(Vector* pVecMins, Vector* pVecMaxs);
	void WorldSpaceTriggerBounds(Vector* pVecWorldMins, Vector* pVecWorldMaxs) const;
	const Vector& NormalizedToWorldSpace(const Vector& in, Vector* pResult) const;
	const Vector& WorldToNormalizedSpace(const Vector& in, Vector* pResult) const;
	const Vector& WorldToCollisionSpace(const Vector& in, Vector* pResult) const;
	const Vector& CollisionToWorldSpace(const Vector& in, Vector* pResult) const;
	const Vector& WorldDirectionToCollisionSpace(const Vector& in, Vector* pResult) const;
	const Vector& NormalizedToCollisionSpace(const Vector& in, Vector* pResult) const;
	const matrix3x4_t& CollisionToWorldTransform() const;
	float BoundingRadius() const;
	float BoundingRadius2D() const;
	bool IsPointSized() const;
	void RandomPointInBounds(const Vector& vecNormalizedMins, const Vector& vecNormalizedMaxs, Vector* pPoint) const;
	bool IsPointInBounds(const Vector& vecWorldPt) const;
	void UseTriggerBounds(bool bEnable, float flBloat = 0.0f);
	void RefreshScaledCollisionBounds(void);
	void MarkPartitionHandleDirty();
	bool DoesRotationInvalidateSurroundingBox() const;
	void MarkSurroundingBoundsDirty();
	void CalcNearestPoint(const Vector& vecWorldPt, Vector* pVecNearestWorldPt) const;
	void SetSurroundingBoundsType(SurroundingBoundsType_t type, const Vector* pMins = NULL, const Vector* pMaxs = NULL);
	void CreatePartitionHandle();
	void DestroyPartitionHandle();
	unsigned short	GetPartitionHandle() const;
	float CalcDistanceFromPoint(const Vector& vecWorldPt) const;
	bool DoesVPhysicsInvalidateSurroundingBox() const;
	void UpdatePartition();
	bool IsBoundsDefinedInEntitySpace() const;
	// Collision group accessors
	int GetCollisionGroup() const;
	void SetCollisionGroup(int collisionGroup);
	void CollisionRulesChanged();
	// Effects...
	bool IsEffectActive(int nEffectMask) const;
	void AddEffects(int nEffects);
	void RemoveEffects(int nEffects);
	int GetEffects(void) const;
	void ClearEffects(void);
	void SetEffects(int nEffects);

	void SetGravity(float flGravity);
	float GetGravity(void) const;
	// Sets physics parameters
	void SetFriction(float flFriction);
	float GetElasticity(void) const;

	CTHINKPTR GetPfnThink();
	void SetPfnThink(CTHINKPTR pfnThink);
	// Think contexts
	int GetIndexForThinkContext(const char* pszContext);
	// Think functions with contexts
	int RegisterThinkContext(const char* szContext);
	CTHINKPTR ThinkSet(CTHINKPTR func, float flNextThinkTime = 0, const char* szContext = NULL);
	void SetNextThink(float nextThinkTime, const char* szContext = NULL);
	float GetNextThink(const char* szContext = NULL);
	int GetNextThinkTick(const char* szContext = NULL);
	float GetLastThink(const char* szContext = NULL);
	int GetLastThinkTick(const char* szContext = NULL);
	void SetLastThinkTick(int iThinkTick);
	bool WillThink();
	int GetFirstThinkTick();	// get first tick thinking on any context
	// Sets/Gets the next think based on context index
	void SetNextThink(int nContextIndex, float thinkTime);
	void SetLastThink(int nContextIndex, float thinkTime);
	float GetNextThink(int nContextIndex) const;
	int	GetNextThinkTick(int nContextIndex) const;
	void CheckHasThinkFunction(bool isThinkingHint = false);
	bool PhysicsRunThink(thinkmethods_t thinkMethod = THINK_FIRE_ALL_FUNCTIONS);
	bool PhysicsRunSpecificThink(int nContextIndex, CTHINKPTR thinkFunc);
	void PhysicsDispatchThink(CTHINKPTR thinkFunc);

	MoveType_t GetMoveType(void) const;
	MoveCollide_t GetMoveCollide(void) const;
	// Access movetype and solid.
	void SetMoveType(MoveType_t val, MoveCollide_t moveCollide = MOVECOLLIDE_DEFAULT);	// Set to one of the MOVETYPE_ defines.
	void SetMoveCollide(MoveCollide_t val);	// Set to one of the MOVECOLLIDE_ defines.

	bool IsSimulatedEveryTick() const;
	void SetSimulatedEveryTick(bool sim);
	bool IsAnimatedEveryTick() const;
	void SetAnimatedEveryTick(bool anim);
	// These set entity flags (EFL_*) to help optimize queries
	void CheckHasGamePhysicsSimulation();
	bool WillSimulateGamePhysics();

	// These methods encapsulate MOVETYPE_FOLLOW, which became obsolete
	void FollowEntity(IEngineObjectClient* pBaseEntity, bool bBoneMerge = true);
	void StopFollowingEntity();	// will also change to MOVETYPE_NONE
	bool IsFollowingEntity();
	IEngineObjectClient* GetFollowedEntity();
	IEngineObjectClient* FindFollowedEntity();

	// save out interpolated values
	void PreDataUpdate(DataUpdateType_t updateType);
	void PostDataUpdate(DataUpdateType_t updateType);
	// This is called once per frame before any data is read in from the server.
	void OnPreDataChanged(DataUpdateType_t type);
	// This event is triggered during the simulation phase if an entity's data has changed. It is 
// better to hook this instead of PostDataUpdate() because in PostDataUpdate(), server entity origins
// are incorrect and attachment points can't be used.
	virtual void OnDataChanged(DataUpdateType_t type);

	// Call this in OnDataChanged if you don't chain it down!
	void MarkMessageReceived();

	// Gets the last message time
	float GetLastMessageTime() const { return m_flLastMessageTime; }

	// A random value 0-1 used by proxies to make sure they're not all in sync
	float ProxyRandomValue() const { return m_flProxyRandomValue; }

	// get network origin from previous update
	const Vector& GetOldOrigin();

	// The spawn time of this entity
	float GetSpawnTime() const { return m_flSpawnTime; }

	int GetCreationTick() const;

	float GetAnimTime() const;
	void SetAnimTime(float at);

	float GetSimulationTime() const;
	void SetSimulationTime(float st);
	float GetLastChangeTime(int flags);
	float GetOldSimulationTime() const;
	int& DataChangeEventRef() { return m_DataChangeEventRef; }

	void UseClientSideAnimation();
	bool IsUsingClientSideAnimation() { return m_bClientSideAnimation; }

	Vector GetVecForce() {
		return 	m_vecForce;
	}
	void SetVecForce(Vector vecForce) {
		m_vecForce = vecForce;
	}
	int	GetForceBone() {
		return m_nForceBone;
	}
	void SetForceBone(int nForceBone) {
		m_nForceBone = nForceBone;
	}
	int GetBody() {
		return m_nBody;
	}
	void SetBody(int nBody) {
		m_nBody = nBody;
	}
	int GetSkin() {
		return m_nSkin;
	}
	void SetSkin(int nSkin) {
		m_nSkin = nSkin;
	}
	int GetHitboxSet() {
		return m_nHitboxSet;
	}
	void SetHitboxSet(int nHitboxSet) {
		m_nHitboxSet = nHitboxSet;
	}

	void SetModelScale(float scale, float change_duration = 0.0f);
	float GetModelScale() const { return m_flModelScale; }
	inline bool IsModelScaleFractional() const;  /// very fast way to ask if the model scale is < 1.0f  (faster than if (GetModelScale() < 1.0f) )
	inline bool IsModelScaled() const;
	void UpdateModelScale(void);
	const model_t* GetModel(void) const;
	void SetModelPointer(const model_t* pModel);
	IStudioHdr* GetModelPtr() const;
	void InvalidateMdlCache();
	void SetCycle(float flCycle);
	float GetCycle() const;
	float GetPlaybackRate();
	void SetPlaybackRate(float rate);
	void SetReceivedSequence(void);
	bool GetReceivedSequence() {
		return m_bReceivedSequence;
	}
	int GetSequence();
	virtual void SetSequence(int nSequence);
	inline void ResetSequence(int nSequence);
	void ResetSequenceInfo(void);
	int GetNewSequenceParity() {
		return m_nNewSequenceParity;
	}
	int GetPrevNewSequenceParity() {
		return m_nPrevNewSequenceParity;
	}
	void SetPrevNewSequenceParity(int nPrevNewSequenceParity) {
		m_nPrevNewSequenceParity = nPrevNewSequenceParity;
	}
	int GetResetEventsParity() {
		return m_nResetEventsParity;
	}
	float GetGroundSpeed() const {
		return m_flGroundSpeed;
	}
	void SetGroundSpeed(float flGroundSpeed) {
		m_flGroundSpeed = flGroundSpeed;
	}
	bool SequenceLoops(void) { return m_bSequenceLoops; }
	bool IsSequenceFinished(void);
	void SetSequenceFinished(bool bFinished) {
		m_bSequenceFinished = bFinished;
	}

	void DisableMuzzleFlash();		// Turn off the muzzle flash (ie: signal that we handled the server's event).
	virtual void DoMuzzleFlash();	// Force a muzzle flash event. Note: this only QUEUES an event, so
	// ProcessMuzzleFlashEvent will get called later.
	bool ShouldMuzzleFlash() const;	// Is the muzzle flash event on?
	// Get bone controller values.
	virtual void GetBoneControllers(float controllers[MAXSTUDIOBONECTRLS]);
	virtual float SetBoneController(int iController, float flValue);
	virtual CMouthInfo* GetMouth();
	CMouthInfo& MouthInfo();
	virtual void ControlMouth(IStudioHdr* pStudioHdr);
	bool IsSequenceLooping(IStudioHdr* pStudioHdr, int iSequence);
	bool IsSequenceLooping(int iSequence) { return IsSequenceLooping(GetModelPtr(), iSequence); }
	virtual float GetSequenceCycleRate(IStudioHdr* pStudioHdr, int iSequence);
	void GetSequenceLinearMotion(int iSequence, Vector* pVec);
	float GetSequenceMoveDist(IStudioHdr* pStudioHdr, int iSequence);
	float GetSequenceGroundSpeed(IStudioHdr* pStudioHdr, int iSequence);
	float GetSequenceGroundSpeed(int iSequence) { return GetSequenceGroundSpeed(GetModelPtr(), iSequence); }
	float SequenceDuration(void);
	float SequenceDuration(IStudioHdr* pStudioHdr, int iSequence);
	float SequenceDuration(int iSequence) { return SequenceDuration(GetModelPtr(), iSequence); }
	int LookupPoseParameter(IStudioHdr* pStudioHdr, const char* szName);
	int LookupPoseParameter(const char* szName) { return LookupPoseParameter(GetModelPtr(), szName); }
	float GetPoseParameter(int iPoseParameter);
	virtual void GetPoseParameters(IStudioHdr* pStudioHdr, float poseParameter[MAXSTUDIOPOSEPARAM]);
	float SetPoseParameter(IStudioHdr* pStudioHdr, const char* szName, float flValue);
	float SetPoseParameter(IStudioHdr* pStudioHdr, int iParameter, float flValue);
	float SetPoseParameter(const char* szName, float flValue) { return SetPoseParameter(GetModelPtr(), szName, flValue); }
	float SetPoseParameter(int iParameter, float flValue) { return SetPoseParameter(GetModelPtr(), iParameter, flValue); }
	virtual void ResetClientsideFrame(void) { SetCycle(0); }
	bool GetPoseParameterRange(int iPoseParameter, float& minValue, float& maxValue);
	void UpdateRelevantInterpolatedVars();
	void AddBaseAnimatingInterpolatedVars();
	void RemoveBaseAnimatingInterpolatedVars();
	// destroy and remove the physics object for this entity
	virtual void	VPhysicsDestroyObject(void);
	virtual IPhysicsObject* VPhysicsGetObject(void) const { return m_pPhysicsObject; }
	virtual int VPhysicsGetObjectList(IPhysicsObject** pList, int listMax);
	void			VPhysicsSetObject(IPhysicsObject* pPhysics);

	// Convenience routines to init the vphysics simulation for this object.
// This creates a static object.  Something that behaves like world geometry - solid, but never moves
	IPhysicsObject* VPhysicsInitStatic(void);

	// This creates a normal vphysics simulated object
	IPhysicsObject* VPhysicsInitNormal(SolidType_t solidType, int nSolidFlags, bool createAsleep, solid_t* pSolid = NULL);

	// This creates a vphysics object with a shadow controller that follows the AI
	// Move the object to where it should be and call UpdatePhysicsShadowToCurrentPosition()
	IPhysicsObject* VPhysicsInitShadow(bool allowPhysicsMovement, bool allowPhysicsRotation, solid_t* pSolid = NULL);

	// These methods return a *world-aligned* box relative to the absorigin of the entity.
// This is used for collision purposes and is *not* guaranteed
// to surround the entire entity's visual representation
// NOTE: It is illegal to ask for the world-aligned bounds for
// SOLID_BSP objects
	virtual const Vector& WorldAlignMins() const;
	virtual const Vector& WorldAlignMaxs() const;
	// FIXME: Do we want this?
	const Vector& WorldAlignSize() const;

	void InitRagdoll(
		const Vector& forceVector,
		int forceBone,
		const matrix3x4_t* pDeltaBones0,
		const matrix3x4_t* pDeltaBones1,
		const matrix3x4_t* pCurrentBonePosition,
		float boneDt,
		bool bFixedConstraints = false);

	virtual void RagdollBone(C_BaseEntity* ent, mstudiobone_t* pbones, int boneCount, bool* boneSimulated, CBoneAccessor& pBoneToWorld);
	virtual const Vector& GetRagdollOrigin();
	virtual void GetRagdollBounds(Vector& theMins, Vector& theMaxs);
	void	BuildRagdollBounds();

	virtual IPhysicsObject* GetElement(int elementNum);
	virtual IPhysicsConstraintGroup* GetConstraintGroup() { return m_ragdoll.pGroup; }
	virtual void DrawWireframe();
	virtual void VPhysicsUpdate(IPhysicsObject* pPhysics);
	void RagdollMoved(void);
	virtual int RagdollBoneCount() const { return m_ragdoll.listCount; }
	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Transforms a vector from the given bone's space to world space
	//=============================================================================

	virtual bool TransformVectorToWorld(int iBoneIndex, const Vector* vTemp, Vector* vOut);

	//=============================================================================
	// HPE_END
	//=============================================================================


	//void	SetInitialBonePosition( IStudioHdr *pstudiohdr, const CBoneAccessor &pDesiredBonePosition );

	bool IsValid() { return m_ragdoll.listCount > 0; }

	void ResetRagdollSleepAfterTime(void);
	float GetLastVPhysicsUpdateTime() const { return m_lastUpdate; }

	ragdoll_t* GetRagdoll(void) { return m_ragdoll.listCount ? &m_ragdoll : NULL; }
	// returns true if we're currently being ragdolled
	bool							IsRagdoll() const;
	bool							IsAboutToRagdoll() const;
	bool InitAsClientRagdoll(const matrix3x4_t* pDeltaBones0, const matrix3x4_t* pDeltaBones1, const matrix3x4_t* pCurrentBonePosition, float boneDt, bool bFixedConstraints = false);
	//virtual void SaveRagdollInfo(int numbones, const matrix3x4_t& cameraTransform, CBoneAccessor& pBoneToWorld);
	void							SetBuiltRagdoll(bool builtRagdoll) { m_builtRagdoll = builtRagdoll; }
	void							ClearRagdoll();
	//void							CreateUnragdollInfo(C_BaseEntity* pRagdoll);
	//virtual bool					RetrieveRagdollInfo(Vector* pos, Quaternion* q);
	//void UnragdollBlend(IStudioHdr* hdr, Vector pos[], Quaternion q[], float currentTime);

	// For prediction
	int								SelectWeightedSequence(int activity);
	virtual void					Simulate();

	float GetLastBoneChangeTime() { return m_flLastBoneChangeTime; }
	int	 GetElementCount() { return m_elementCount; }
	int GetBoneIndex(int index) { return m_boneIndex[index]; }
	const Vector& GetRagPos(int index) { return m_ragPos[index]; }
	const QAngle& GetRagAngles(int index) { return m_ragAngles[index]; }
	unsigned char GetRenderFX() const { return m_nRenderFX; }
	void SetRenderFX(unsigned char nRenderFX) { m_nRenderFX = nRenderFX; }

	void					SetToolHandle(HTOOLHANDLE handle);
	HTOOLHANDLE				GetToolHandle() const;

	void					EnableInToolView(bool bEnable);
	bool					IsEnabledInToolView() const;

	void					SetToolRecording(bool recording);
	bool					IsToolRecording() const;
	bool					HasRecordedThisFrame() const;

	// used to exclude entities from being recorded in the SFM tools
	void					DontRecordInTools();
	bool					ShouldRecordInTools() const;

	int LookupBone(const char* szName);
	int GetBoneCount() { return m_CachedBoneData.Count(); }
	// Wrappers for CBoneAccessor.
	const matrix3x4_t& GetBone(int iBone) const;
	const matrix3x4_t* GetBoneArray() const;
	matrix3x4_t& GetBoneForWrite(int iBone);
	int GetReadableBones() { return m_BoneAccessor.GetReadableBones(); }
	void SetReadableBones(int flags) { m_BoneAccessor.SetReadableBones(flags); }
	int GetWritableBones() { return m_BoneAccessor.GetWritableBones(); }
	void SetWritableBones(int flags) { m_BoneAccessor.SetWritableBones(flags); }
	int GetAccumulatedBoneMask() { return m_iAccumulatedBoneMask; }
	CIKContext* GetIk() { return m_pIk; }
	void DestroyIk() {
		delete m_pIk;
		m_pIk = NULL;
	}
	// model specific
	virtual bool SetupBones(matrix3x4_t* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime);
	virtual void BuildTransformations(IStudioHdr* pStudioHdr, Vector* pos, Quaternion q[], const matrix3x4_t& cameraTransform, int boneMask, CBoneBitList& boneComputed);
	// Call this if SetupBones() has already been called this frame but you need to move the
// entity and rerender.
		// Computes a box that surrounds all hitboxes
	bool ComputeHitboxSurroundingBox(Vector* pVecWorldMins, Vector* pVecWorldMaxs);
	bool ComputeEntitySpaceHitboxSurroundingBox(Vector* pVecWorldMins, Vector* pVecWorldMaxs);
	void GetHitboxBoneTransform(int iBone, matrix3x4_t& pBoneToWorld);
	void GetHitboxBoneTransforms(const matrix3x4_t* hitboxbones[MAXSTUDIOBONES]);
	void GetHitboxBonePosition(int iBone, Vector& origin, QAngle& angles);
	void GetBoneCache(IStudioHdr* pStudioHdr);
	bool GetRootBone(matrix3x4_t& rootBone);
	bool GetAimEntOrigin(Vector* pAbsOrigin, QAngle* pAbsAngles);
	void InvalidateBoneCache();

	unsigned short& GetEntClientFlags() { return m_EntClientFlags; }
	void SetLastRecordedFrame(int nLastRecordedFrame) { m_nLastRecordedFrame = nLastRecordedFrame; }
	float GetBlendWeightCurrent() { return m_flBlendWeightCurrent; }
	void SetBlendWeightCurrent(float flBlendWeightCurrent) { m_flBlendWeightCurrent = flBlendWeightCurrent; }
	int	GetOverlaySequence() { return m_nOverlaySequence; }

	int		LookupAttachment(const char* pAttachmentName);
	int		GetAttachmentCount() { return m_Attachments.Count(); }
	bool PutAttachment(int number, const matrix3x4_t& attachmentToWorld);
	bool GetAttachment(int number, Vector& origin, QAngle& angles);
	bool GetAttachment(int number, matrix3x4_t& matrix);
	bool GetAttachmentVelocity(int number, Vector& originVel, Quaternion& angleVel);
	virtual bool					CalcAttachments();
	virtual ClientShadowHandle_t GetShadowHandle() const { return m_ShadowHandle; }
	virtual void SetRenderHandle(ClientRenderHandle_t hRenderHandle) { m_hRender = hRenderHandle; }
	virtual const ClientRenderHandle_t& GetRenderHandle() const;
	// Shadow-related methods
	virtual bool IsShadowDirty();
	virtual void MarkShadowDirty(bool bDirty);
	virtual IClientRenderable* GetShadowParent();
	virtual IClientRenderable* FirstShadowChild();
	virtual IClientRenderable* NextShadowPeer();
	void CreateModelInstance();
	// Gets the model instance + shadow handle
	virtual ModelInstanceHandle_t GetModelInstance() { return m_ModelInstance; }
	void SetModelInstance(ModelInstanceHandle_t hInstance) { m_ModelInstance = hInstance; }
	bool SnatchModelInstance(IEngineObjectClient* pToEntity);
	// Only meant to be called from subclasses
	void DestroyModelInstance();
	virtual const matrix3x4_t& RenderableToWorldTransform();
	virtual IPVSNotify* GetPVSNotifyInterface() { return m_pOuter->GetPVSNotifyInterface(); }
	virtual void RecordToolMessage() { return m_pOuter->RecordToolMessage(); }
	virtual void OnThreadedDrawSetup() { m_pOuter->OnThreadedDrawSetup(); }
	virtual bool ShouldDraw() { return m_pOuter->ShouldDraw(); }
	virtual bool IsTransparent(void) { return m_pOuter->IsTransparent(); }
	virtual bool UsesPowerOfTwoFrameBufferTexture() { return m_pOuter->UsesPowerOfTwoFrameBufferTexture(); }
	virtual bool UsesFullFrameBufferTexture() { return m_pOuter->UsesFullFrameBufferTexture(); }
	// Is this a two-pass renderable?
	virtual bool IsTwoPass(void) { return m_pOuter->IsTwoPass(); }
	virtual bool IgnoresZBuffer(void) const { return m_pOuter->IgnoresZBuffer(); }
	// Determine alpha and blend amount for transparent objects based on render state info
	virtual void ComputeFxBlend() { m_pOuter->ComputeFxBlend(); }
	virtual int	GetFxBlend(void) { return m_pOuter->GetFxBlend(); }
	virtual void GetColorModulation(float* color) { m_pOuter->GetColorModulation(color); }
	virtual bool UsesFlexDelayedWeights() { return m_pOuter->UsesFlexDelayedWeights(); }
	virtual void SetupWeights(const matrix3x4_t* pBoneToWorld, int nFlexWeightCount, float* pFlexWeights, float* pFlexDelayedWeights) {
		m_pOuter->SetupWeights(pBoneToWorld, nFlexWeightCount, pFlexWeights, pFlexDelayedWeights);
	}
	virtual Vector const& GetRenderOrigin(void) { return m_pOuter->GetRenderOrigin(); }
	virtual QAngle const& GetRenderAngles(void) { return m_pOuter->GetRenderAngles(); }
	// Returns the bounds relative to the origin (render bounds)
	virtual void GetRenderBounds(Vector& mins, Vector& maxs) { return m_pOuter->GetRenderBounds(mins, maxs); }
	// returns the bounds as an AABB in worldspace
	virtual void GetRenderBoundsWorldspace(Vector& mins, Vector& maxs) { return m_pOuter->GetRenderBoundsWorldspace(mins, maxs); }
	// These normally call through to GetRenderAngles/GetRenderBounds, but some entities custom implement them.
	virtual void GetShadowRenderBounds(Vector& mins, Vector& maxs, ShadowType_t shadowType) { return m_pOuter->GetShadowRenderBounds(mins, maxs, shadowType); }
	// Rendering clip plane, should be 4 floats, return value of NULL indicates a disabled render clip plane
	virtual float* GetRenderClipPlane(void) { return m_pOuter->GetRenderClipPlane(); }
	virtual int	DrawModel(int flags) { return m_pOuter->DrawModel(flags); }
	virtual ShadowType_t ShadowCastType() { return m_pOuter->ShadowCastType(); }
	// Should this object be able to have shadows cast onto it?
	virtual bool ShouldReceiveProjectedTextures(int flags) { return m_pOuter->ShouldReceiveProjectedTextures(flags); }

	// These methods return true if we want a per-renderable shadow cast direction + distance
	virtual bool GetShadowCastDistance(float* pDist, ShadowType_t shadowType) const { 
		return m_pOuter->GetShadowCastDistance(pDist, shadowType); 
	}
	virtual bool GetShadowCastDirection(Vector* pDirection, ShadowType_t shadowType) const {
		return m_pOuter->GetShadowCastDirection(pDirection, shadowType);
	}
	// Creates the shadow (if it doesn't already exist) based on shadow cast type
	void					CreateShadow();

	// Destroys the shadow; causes its type to be recomputed if the entity doesn't go away immediately.
	void					DestroyShadow();
	// Dirty bits
	void					MarkRenderHandleDirty();

	// Sets up a render handle so the leaf system will draw this entity.
	void					AddToLeafSystem();
	void					AddToLeafSystem(RenderGroup_t group);
	// remove entity form leaf system again
	void					RemoveFromLeafSystem();

	void					AddToAimEntsList();
	void					RemoveFromAimEntsList();

	void					AddToClientSideAnimationList();
	void					RemoveFromClientSideAnimationList();

	// This can be used to force client side animation to be on. Only use if you know what you're doing!
	// Normally, the server entity should set this.
	void					ForceClientSideAnimationOn();

	void AddToInterpolationList();
	void RemoveFromInterpolationList();

	void AddToTeleportList();
	void RemoveFromTeleportList();

	// Did the object move so far that it shouldn't interpolate?
	bool Teleported(void);

	// Should we interpolate this tick?  (Used to be EF_NOINTERP)
	bool IsNoInterpolationFrame();

	// Sets the origin + angles to match the last position received
	void MoveToLastReceivedPosition(bool force = false);

	virtual void ResetLatched();
	bool IsReadyToDraw() { return m_bReadyToDraw; }

	bool PhysModelParseSolid(solid_t& solid);
	bool PhysModelParseSolidByIndex(solid_t& solid, int solidIndex);
	void PhysForceClearVelocity(IPhysicsObject* pPhys);

	C_GrabControllerInternal* GetGrabController() { return &m_grabController; }
private:
	void LockStudioHdr();
	void UnlockStudioHdr();

	// called by all vphysics inits
	bool			VPhysicsInitSetup();
	void			CheckSettleStationaryRagdoll();
	void			PhysForceRagdollToSleep();
	// View models say yes to this.
	bool			IsBoneAccessAllowed() const;
	// This method should return true if the bones have changed + SetupBones needs to be called
	virtual float LastBoneChangedTime();
	void			SetupBones_AttachmentHelper(IStudioHdr* pStudioHdr);
	void			ClientSideAnimationChanged();
	void RagdollSolveSeparation(ragdoll_t& ragdoll, IHandleEntity* pEntity);


protected:

	IClientEntityList* const m_pClientEntityList = NULL;
	const CBaseHandle m_RefEHandle;
	friend class C_BaseEntity;
	CThreadFastMutex m_CalcAbsolutePositionMutex;
	CThreadFastMutex m_CalcAbsoluteVelocityMutex;
	Vector							m_vecOrigin = Vector(0,0,0);
	CInterpolatedVar< Vector >		m_iv_vecOrigin;
	QAngle							m_angRotation = QAngle(0, 0, 0);
	CInterpolatedVar< QAngle >		m_iv_angRotation;
	// Object velocity
	Vector							m_vecVelocity = Vector(0, 0, 0);
	CInterpolatedVar< Vector >		m_iv_vecVelocity;
	Vector							m_vecAbsOrigin = Vector(0, 0, 0);
	// Object orientation
	QAngle							m_angAbsRotation = QAngle(0, 0, 0);
	Vector							m_vecAbsVelocity = Vector(0, 0, 0);
	C_BaseEntity* m_pOuter = NULL;

	// Hierarchy
	C_EngineObjectInternal* m_pMoveParent = NULL;
	IEngineObjectClient*	m_hOldMoveParent = NULL;
	C_EngineObjectInternal* m_pMoveChild = NULL;
	C_EngineObjectInternal* m_pMovePeer = NULL;
	C_EngineObjectInternal* m_pMovePrevPeer = NULL;

	// Specifies the entity-to-world transform
	matrix3x4_t						m_rgflCoordinateFrame;
	string_t						m_iClassname;

#if !defined( NO_ENTITY_PREDICTION )
	// For storing prediction results and pristine network state
	byte* m_pIntermediateData[MULTIPLAYER_BACKUP];
	byte* m_pOriginalData = NULL;
	byte* m_pOuterIntermediateData[MULTIPLAYER_BACKUP];
	byte* m_pOuterOriginalData = NULL;
	int								m_nIntermediateDataCount = 0;

	//bool							m_bIsPlayerSimulated;
#endif
	VarMapping_t	m_VarMap;

	unsigned int testNetwork;

	// Last values to come over the wire. Used for interpolation.
	Vector							m_vecNetworkOrigin = Vector(0, 0, 0);
	QAngle							m_angNetworkAngles = QAngle(0, 0, 0);
	// The moveparent received from networking data
	CHandle<C_BaseEntity>			m_hNetworkMoveParent = NULL;
	unsigned char					m_iParentAttachment; // 0 if we're relative to the parent's absorigin and absangles.
	unsigned char					m_iOldParentAttachment;

	// Behavior flags
	int								m_fFlags;
	int								m_iEFlags;	// entity flags EFL_*
	int								m_spawnflags;
	// used so we know when things are no longer touching
	int								touchStamp;
	int								m_fDataObjectTypes;

	EHANDLE							m_hGroundEntity;
	float							m_flGroundChangeTime;

	string_t						m_ModelName;
	// Object model index
	short							m_nModelIndex;
	CCollisionProperty				m_Collision;
	// used to cull collision tests
	int								m_CollisionGroup;
	// Effects to apply
	int								m_fEffects;

	// Gravity multiplier
	float							m_flGravity;
	// Friction.
	float							m_flFriction;
	// Physics state
	float							m_flElasticity;
	// interface function pointers
	CTHINKPTR						m_pfnThink;
	int								m_nNextThinkTick;
	int								m_nLastThinkTick;
	CUtlVector< clientthinkfunc_t >		m_aThinkFunctions;
	int								m_iCurrentThinkContext;
	// Object movetype
	unsigned char					m_MoveType;
	unsigned char					m_MoveCollide;

	bool							m_bSimulatedEveryTick;
	bool							m_bAnimatedEveryTick;

	// Time animation sequence or frame was last changed
	float							m_flAnimTime;
	float							m_flOldAnimTime;

	float							m_flSimulationTime;
	float							m_flOldSimulationTime;
	// The list that holds OnDataChanged events uses this to make sure we don't get multiple
// OnDataChanged calls in the same frame if the client receives multiple packets.
	int								m_DataChangeEventRef;
	Vector							m_vecOldOrigin;
	QAngle							m_vecOldAngRotation;
	// Timestamp of message arrival
	float							m_flLastMessageTime;
	// A random value used by material proxies for each model instance.
	float							m_flProxyRandomValue;
	int								m_nCreationTick;
	// The spawn time of the entity
	float							m_flSpawnTime;
	// Clientside animation
	bool							m_bClientSideAnimation;

	Vector							m_vecForce;
	int								m_nForceBone;
	// Texture group to use
	int								m_nSkin;

	// Object bodygroup
	int								m_nBody;

	// Hitbox set to use (default 0)
	int								m_nHitboxSet;
	float							m_flModelScale;
	float							m_flOldModelScale;

	float							m_flEncodedController[MAXSTUDIOBONECTRLS];
	CInterpolatedVarArray< float, MAXSTUDIOBONECTRLS >		m_iv_flEncodedController;
	float							m_flOldEncodedController[MAXSTUDIOBONECTRLS];

	// Client-side animation
	bool							m_bClientSideFrameReset;
	bool							m_bLastClientSideFrameReset;

	float							m_flCycle;
	CInterpolatedVar< float >		m_iv_flCycle;
	float							m_flOldCycle;

	// Animation blending factors
	float							m_flPoseParameter[MAXSTUDIOPOSEPARAM];
	CInterpolatedVarArray< float, MAXSTUDIOPOSEPARAM >		m_iv_flPoseParameter;
	float							m_flOldPoseParameters[MAXSTUDIOPOSEPARAM];

	// Animation playback framerate
	float							m_flPlaybackRate;

	bool							m_bReceivedSequence;
	// Current animation sequence
	int								m_nSequence;
	int								m_nOldSequence;

	int								m_nNewSequenceParity;
	int								m_nPrevNewSequenceParity;

	int								m_nResetEventsParity;
	// These are compared against each other to determine if the entity should muzzle flash.
	unsigned char					m_nMuzzleFlashParity;
	unsigned char					m_nOldMuzzleFlashParity;
	float							m_flGroundSpeed;	// computed linear movement rate for current sequence
	bool							m_bSequenceLoops;	// true if the sequence loops
	bool							m_bSequenceFinished;// flag set when StudioAdvanceFrame moves across a frame boundry
	float							m_flLastEventCheck;	// cycle index of when events were last checked
	// Mouth lipsync/envelope following values
	CMouthInfo						m_mouth;

	// Model for rendering
	const model_t* m_pModel;
	mutable IStudioHdr* m_pStudioHdr;
	mutable MDLHandle_t				m_hStudioHdr;
	CThreadFastMutex				m_StudioHdrInitLock;

	// pointer to the entity's physics object (vphysics.dll)
	IPhysicsObject* m_pPhysicsObject;
	ragdoll_t	m_ragdoll;
	Vector		m_mins, m_maxs;
	Vector		m_origin;
	float		m_radius;
	float		m_lastUpdate;
	bool		m_allAsleep;
	Vector		m_vecLastOrigin;
	float		m_flLastOriginChangeTime;

#if RAGDOLL_VISUALIZE
	matrix3x4_t			m_savedBone1[MAXSTUDIOBONES];
	matrix3x4_t			m_savedBone2[MAXSTUDIOBONES];
	matrix3x4_t			m_savedBone3[MAXSTUDIOBONES];
#endif

	bool							m_builtRagdoll;
	Vector							m_vecPreRagdollMins;
	Vector							m_vecPreRagdollMaxs;
	// Decomposed ragdoll info
	//bool							m_bStoreRagdollInfo;
	//RagdollInfo_t*					m_pRagdollInfo;

	int								m_nPrevSequence;
	int								m_nRestoreSequence;

	// Incoming from network
	int			m_RagdollBoneCount;
	Vector		m_ragPos[RAGDOLL_MAX_ELEMENTS];
	QAngle		m_ragAngles[RAGDOLL_MAX_ELEMENTS];
	CInterpolatedVarArray< Vector, RAGDOLL_MAX_ELEMENTS >	m_iv_ragPos;
	CInterpolatedVarArray< QAngle, RAGDOLL_MAX_ELEMENTS >	m_iv_ragAngles;
	int			m_elementCount;
	int			m_ragdollListCount;
	int			m_boneIndex[RAGDOLL_MAX_ELEMENTS];
	int			m_nOverlaySequence;
	float		m_flBlendWeightCurrent;
	float		m_flLastBoneChangeTime;

	// Render information
	unsigned char					m_nRenderFX;
	int								m_iPrevBoneMask;
	// Set by tools if this entity should route "info" to various tools listening to HTOOLENTITIES
#ifndef NO_TOOLFRAMEWORK
	bool							m_bEnabledInToolView;
	bool							m_bToolRecording;
	HTOOLHANDLE						m_ToolHandle;
	int								m_nLastRecordedFrame;
	bool							m_bRecordInTools; // should this entity be recorded in the tools (we exclude some things like models for menus)
#endif

	// Is bone cache valid
	// bone transformation matrix
	unsigned long					m_iMostRecentModelBoneCounter;
	unsigned long					m_iMostRecentBoneSetupRequest;
	int								m_iAccumulatedBoneMask;

	CThreadFastMutex				m_BoneSetupLock;
	CUtlVector< matrix3x4_t >		m_CachedBoneData; // never access this directly. Use m_BoneAccessor.
	//memhandle_t						m_hitboxBoneCacheHandle;
	float							m_flLastBoneSetupTime;
	CBoneAccessor					m_BoneAccessor;
	CIKContext*						m_pIk = NULL;
	// Entity flags that are only for the client (ENTCLIENTFLAG_ defines).
	unsigned short					m_EntClientFlags;
	CBoneMergeCache*				m_pBoneMergeCache = NULL;	// This caches the strcmp lookups that it has to do
	CJiggleBones*					m_pJiggleBones = NULL;

	// Calculated attachment points
	CUtlVector<CAttachmentData>		m_Attachments;

	// Shadow data
	ClientShadowHandle_t			m_ShadowHandle;
	// Used to store the state we were added to the BSP as, so it can
	// reinsert the entity if the state changes.
	ClientRenderHandle_t			m_hRender;	// link into spatial partition
	// Model instance data..
	ModelInstanceHandle_t			m_ModelInstance;
	bool							m_bAlternateSorting;
	AimEntsListHandle_t				m_AimEntsListHandle;
	ClientSideAnimationListHandle_t	m_ClientSideAnimationListHandle;
	unsigned short					m_InterpolationListEntry;	// Entry into g_InterpolationList (or g_InterpolationList.InvalidIndex if not in the list).
	unsigned short					m_TeleportListEntry;

	byte							m_ubInterpolationFrame;
	byte							m_ubOldInterpolationFrame;
	// Interpolation says don't draw yet
	bool							m_bReadyToDraw;

	C_GrabControllerInternal		m_grabController;
};

//-----------------------------------------------------------------------------
// Methods relating to traversing hierarchy
//-----------------------------------------------------------------------------
inline C_EngineObjectInternal* C_EngineObjectInternal::GetMoveParent(void) const
{
	return m_pMoveParent;
}

inline void C_EngineObjectInternal::SetMoveParent(IEngineObjectClient* pMoveParent) {
	m_pMoveParent = (C_EngineObjectInternal*)pMoveParent;
}

inline C_EngineObjectInternal* C_EngineObjectInternal::FirstMoveChild(void) const
{
	return m_pMoveChild;
}

inline void C_EngineObjectInternal::SetFirstMoveChild(IEngineObjectClient* pMoveChild) {
	m_pMoveChild = (C_EngineObjectInternal*)pMoveChild;
}

inline C_EngineObjectInternal* C_EngineObjectInternal::NextMovePeer(void) const
{
	return m_pMovePeer;
}

inline void C_EngineObjectInternal::SetNextMovePeer(IEngineObjectClient* pMovePeer) {
	m_pMovePeer = (C_EngineObjectInternal*)pMovePeer;
}

inline C_EngineObjectInternal* C_EngineObjectInternal::MovePrevPeer(void) const
{
	return m_pMovePrevPeer;
}

inline void C_EngineObjectInternal::SetMovePrevPeer(IEngineObjectClient* pMovePrevPeer) {
	m_pMovePrevPeer = (C_EngineObjectInternal*)pMovePrevPeer;
}

inline C_EngineObjectInternal* C_EngineObjectInternal::GetRootMoveParent()
{
	C_EngineObjectInternal* pEntity = this;
	C_EngineObjectInternal* pParent = this->GetMoveParent();
	while (pParent)
	{
		pEntity = pParent;
		pParent = pEntity->GetMoveParent();
	}

	return pEntity;
}

inline VarMapping_t* C_EngineObjectInternal::GetVarMapping()
{
	return &m_VarMap;
}

//-----------------------------------------------------------------------------
// Some helper methods that transform a point from entity space to world space + back
//-----------------------------------------------------------------------------
inline void C_EngineObjectInternal::EntityToWorldSpace(const Vector& in, Vector* pOut) const
{
	if (GetAbsAngles() == vec3_angle)
	{
		VectorAdd(in, GetAbsOrigin(), *pOut);
	}
	else
	{
		VectorTransform(in, EntityToWorldTransform(), *pOut);
	}
}

inline void C_EngineObjectInternal::WorldToEntitySpace(const Vector& in, Vector* pOut) const
{
	if (GetAbsAngles() == vec3_angle)
	{
		VectorSubtract(in, GetAbsOrigin(), *pOut);
	}
	else
	{
		VectorITransform(in, EntityToWorldTransform(), *pOut);
	}
}

inline const Vector& C_EngineObjectInternal::GetNetworkOrigin() const
{
	return m_vecNetworkOrigin;
}

inline const QAngle& C_EngineObjectInternal::GetNetworkAngles() const
{
	return m_angNetworkAngles;
}

inline IEngineObjectClient* C_EngineObjectInternal::GetNetworkMoveParent() {
	return m_hNetworkMoveParent.Get()? m_hNetworkMoveParent.Get()->GetEngineObject():NULL;
}

inline unsigned char C_EngineObjectInternal::GetParentAttachment() const
{
	return m_iParentAttachment;
}

inline int	C_EngineObjectInternal::GetFlags(void) const
{
	return m_fFlags;
}

//-----------------------------------------------------------------------------
// EFlags.. 
//-----------------------------------------------------------------------------
inline int C_EngineObjectInternal::GetEFlags() const
{
	return m_iEFlags;
}

inline void C_EngineObjectInternal::SetEFlags(int iEFlags)
{
	m_iEFlags = iEFlags;
}

inline void C_EngineObjectInternal::AddEFlags(int nEFlagMask)
{
	m_iEFlags |= nEFlagMask;
}

inline void C_EngineObjectInternal::RemoveEFlags(int nEFlagMask)
{
	m_iEFlags &= ~nEFlagMask;
}

inline bool C_EngineObjectInternal::IsEFlagSet(int nEFlagMask) const
{
	return (m_iEFlags & nEFlagMask) != 0;
}

//-----------------------------------------------------------------------------
// checks to see if the entity is marked for deletion
//-----------------------------------------------------------------------------
inline bool C_EngineObjectInternal::IsMarkedForDeletion(void)
{
	return (GetEFlags() & EFL_KILLME);
}

inline int C_EngineObjectInternal::GetSpawnFlags(void) const
{
	return m_spawnflags;
}

inline int	C_EngineObjectInternal::GetTouchStamp()
{
	return touchStamp;
}

inline void C_EngineObjectInternal::ClearTouchStamp()
{
	touchStamp = 0;
}

inline int C_EngineObjectInternal::GetModelIndex(void) const
{
	return m_nModelIndex;
}

//-----------------------------------------------------------------------------
// An inline version the game code can use
//-----------------------------------------------------------------------------
//inline CCollisionProperty* C_EngineObjectInternal::CollisionProp()
//{
//	return &m_Collision;
//}

//inline const CCollisionProperty* C_EngineObjectInternal::CollisionProp() const
//{
//	return &m_Collision;
//}

inline ICollideable* C_EngineObjectInternal::GetCollideable()
{
	return &m_Collision;
}

//-----------------------------------------------------------------------------
// Methods relating to solid type + flags
//-----------------------------------------------------------------------------
inline void C_EngineObjectInternal::SetSolidFlags(int nFlags)
{
	m_Collision.SetSolidFlags(nFlags);
}

inline bool C_EngineObjectInternal::IsSolidFlagSet(int flagMask) const
{
	return m_Collision.IsSolidFlagSet(flagMask);
}

inline int	C_EngineObjectInternal::GetSolidFlags(void) const
{
	return m_Collision.GetSolidFlags();
}

inline void C_EngineObjectInternal::AddSolidFlags(int nFlags)
{
	m_Collision.AddSolidFlags(nFlags);
}

inline void C_EngineObjectInternal::RemoveSolidFlags(int nFlags)
{
	m_Collision.RemoveSolidFlags(nFlags);
}

inline bool C_EngineObjectInternal::IsSolid() const
{
	return m_Collision.IsSolid();
}

inline void C_EngineObjectInternal::SetSolid(SolidType_t val)
{
	m_Collision.SetSolid(val);
}

inline SolidType_t C_EngineObjectInternal::GetSolid() const
{
	return m_Collision.GetSolid();
}

inline void C_EngineObjectInternal::SetCollisionBounds(const Vector& mins, const Vector& maxs)
{
	m_Collision.SetCollisionBounds(mins, maxs);
}

inline const Vector& C_EngineObjectInternal::GetCollisionOrigin() const
{
	return m_Collision.GetCollisionOrigin();
}

inline const QAngle& C_EngineObjectInternal::GetCollisionAngles() const
{
	return m_Collision.GetCollisionAngles();
}

inline const Vector& C_EngineObjectInternal::OBBMinsPreScaled() const
{
	return m_Collision.OBBMinsPreScaled();
}

inline const Vector& C_EngineObjectInternal::OBBMaxsPreScaled() const
{
	return m_Collision.OBBMaxsPreScaled();
}

inline const Vector& C_EngineObjectInternal::OBBMins() const
{
	return m_Collision.OBBMins();
}

inline const Vector& C_EngineObjectInternal::OBBMaxs() const
{
	return m_Collision.OBBMaxs();
}

inline const Vector& C_EngineObjectInternal::OBBSize() const
{
	return m_Collision.OBBSize();
}

inline const Vector& C_EngineObjectInternal::OBBCenter() const
{
	return m_Collision.OBBCenter();
}

inline const Vector& C_EngineObjectInternal::WorldSpaceCenter() const
{
	return m_Collision.WorldSpaceCenter();
}

inline void C_EngineObjectInternal::WorldSpaceAABB(Vector* pWorldMins, Vector* pWorldMaxs) const
{
	m_Collision.WorldSpaceAABB(pWorldMins, pWorldMaxs);
}

inline void C_EngineObjectInternal::WorldSpaceSurroundingBounds(Vector* pVecMins, Vector* pVecMaxs)
{
	m_Collision.WorldSpaceSurroundingBounds(pVecMins, pVecMaxs);
}

inline void C_EngineObjectInternal::WorldSpaceTriggerBounds(Vector* pVecWorldMins, Vector* pVecWorldMaxs) const
{
	m_Collision.WorldSpaceTriggerBounds(pVecWorldMins, pVecWorldMaxs);
}

inline const Vector& C_EngineObjectInternal::NormalizedToWorldSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.NormalizedToWorldSpace(in, pResult);
}

inline const Vector& C_EngineObjectInternal::WorldToNormalizedSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.WorldToNormalizedSpace(in, pResult);
}

inline const Vector& C_EngineObjectInternal::WorldToCollisionSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.WorldToCollisionSpace(in, pResult);
}

inline const Vector& C_EngineObjectInternal::CollisionToWorldSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.CollisionToWorldSpace(in, pResult);
}

inline const Vector& C_EngineObjectInternal::WorldDirectionToCollisionSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.WorldDirectionToCollisionSpace(in, pResult);
}

inline const Vector& C_EngineObjectInternal::NormalizedToCollisionSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.NormalizedToCollisionSpace(in, pResult);
}

inline const matrix3x4_t& C_EngineObjectInternal::CollisionToWorldTransform() const
{
	return m_Collision.CollisionToWorldTransform();
}

inline float C_EngineObjectInternal::BoundingRadius() const
{
	return m_Collision.BoundingRadius();
}

inline float C_EngineObjectInternal::BoundingRadius2D() const
{
	return m_Collision.BoundingRadius2D();
}

inline bool C_EngineObjectInternal::IsPointSized() const
{
	return BoundingRadius() == 0.0f;
}

inline void C_EngineObjectInternal::RandomPointInBounds(const Vector& vecNormalizedMins, const Vector& vecNormalizedMaxs, Vector* pPoint) const
{
	m_Collision.RandomPointInBounds(vecNormalizedMins, vecNormalizedMaxs, pPoint);
}

inline bool C_EngineObjectInternal::IsPointInBounds(const Vector& vecWorldPt) const
{
	return m_Collision.IsPointInBounds(vecWorldPt);
}

inline void C_EngineObjectInternal::UseTriggerBounds(bool bEnable, float flBloat)
{
	m_Collision.UseTriggerBounds(bEnable, flBloat);
}

inline void C_EngineObjectInternal::RefreshScaledCollisionBounds(void)
{
	m_Collision.RefreshScaledCollisionBounds();
}

inline void C_EngineObjectInternal::MarkPartitionHandleDirty()
{
	m_Collision.MarkPartitionHandleDirty();
}

inline bool C_EngineObjectInternal::DoesRotationInvalidateSurroundingBox() const
{
	return m_Collision.DoesRotationInvalidateSurroundingBox();
}

inline void C_EngineObjectInternal::MarkSurroundingBoundsDirty()
{
	m_Collision.MarkSurroundingBoundsDirty();
}

inline void C_EngineObjectInternal::CalcNearestPoint(const Vector& vecWorldPt, Vector* pVecNearestWorldPt) const
{
	m_Collision.CalcNearestPoint(vecWorldPt, pVecNearestWorldPt);
}

inline void C_EngineObjectInternal::SetSurroundingBoundsType(SurroundingBoundsType_t type, const Vector* pMins, const Vector* pMaxs)
{
	m_Collision.SetSurroundingBoundsType(type, pMins, pMaxs);
}

inline void C_EngineObjectInternal::CreatePartitionHandle()
{
	m_Collision.CreatePartitionHandle();
}

inline void C_EngineObjectInternal::DestroyPartitionHandle()
{
	m_Collision.DestroyPartitionHandle();
}

inline unsigned short C_EngineObjectInternal::GetPartitionHandle() const
{
	return m_Collision.GetPartitionHandle();
}

inline float C_EngineObjectInternal::CalcDistanceFromPoint(const Vector& vecWorldPt) const
{
	return m_Collision.CalcDistanceFromPoint(vecWorldPt);
}

inline bool C_EngineObjectInternal::DoesVPhysicsInvalidateSurroundingBox() const
{
	return m_Collision.DoesVPhysicsInvalidateSurroundingBox();
}

inline void C_EngineObjectInternal::UpdatePartition()
{
	m_Collision.UpdatePartition();
}

inline bool C_EngineObjectInternal::IsBoundsDefinedInEntitySpace() const
{
	return m_Collision.IsBoundsDefinedInEntitySpace();
}

//-----------------------------------------------------------------------------
// Collision group accessors
//-----------------------------------------------------------------------------
inline int C_EngineObjectInternal::GetCollisionGroup() const
{
	return m_CollisionGroup;
}

inline int C_EngineObjectInternal::GetEffects(void) const
{
	return m_fEffects;
}

inline void C_EngineObjectInternal::RemoveEffects(int nEffects)
{
	m_pOuter->OnRemoveEffects(nEffects);
	m_fEffects &= ~nEffects;
	if (nEffects & EF_NODRAW)
	{
		m_pOuter->UpdateVisibility();
	}
}

inline void C_EngineObjectInternal::ClearEffects(void)
{
	m_fEffects = 0;
	m_pOuter->UpdateVisibility();
}

inline bool C_EngineObjectInternal::IsEffectActive(int nEffects) const
{
	return (m_fEffects & nEffects) != 0;
}

inline void C_EngineObjectInternal::SetGroundChangeTime(float flTime)
{
	m_flGroundChangeTime = flTime;
}

inline float C_EngineObjectInternal::GetGroundChangeTime(void)
{
	return m_flGroundChangeTime;
}

inline void C_EngineObjectInternal::SetGravity(float flGravity)
{
	m_flGravity = flGravity;
}

inline float C_EngineObjectInternal::GetGravity(void) const
{
	return m_flGravity;
}

inline void C_EngineObjectInternal::SetFriction(float flFriction)
{
	m_flFriction = flFriction;
}

inline float C_EngineObjectInternal::GetElasticity(void)	const
{
	return m_flElasticity;
}

inline CTHINKPTR C_EngineObjectInternal::GetPfnThink()
{
	return m_pfnThink;
}
inline void C_EngineObjectInternal::SetPfnThink(CTHINKPTR pfnThink)
{
	m_pfnThink = pfnThink;
}

inline MoveType_t C_EngineObjectInternal::GetMoveType() const
{
	return (MoveType_t)(unsigned char)m_MoveType;
}

inline MoveCollide_t C_EngineObjectInternal::GetMoveCollide() const
{
	return (MoveCollide_t)(unsigned char)m_MoveCollide;
}

inline bool C_EngineObjectInternal::IsSimulatedEveryTick() const
{
	return m_bSimulatedEveryTick;
}

inline void C_EngineObjectInternal::SetSimulatedEveryTick(bool sim)
{
	if (m_bSimulatedEveryTick != sim)
	{
		m_bSimulatedEveryTick = sim;
		Interp_UpdateInterpolationAmounts();
	}
}

inline bool C_EngineObjectInternal::IsAnimatedEveryTick() const
{
	return m_bAnimatedEveryTick;
}

inline void C_EngineObjectInternal::SetAnimatedEveryTick(bool anim)
{
	if (m_bAnimatedEveryTick != anim)
	{
		m_bAnimatedEveryTick = anim;
		Interp_UpdateInterpolationAmounts();
	}
}

inline float C_EngineObjectInternal::GetAnimTime() const
{
	return m_flAnimTime;
}

inline float C_EngineObjectInternal::GetSimulationTime() const
{
	return m_flSimulationTime;
}

inline void C_EngineObjectInternal::SetAnimTime(float at)
{
	m_flAnimTime = at;
}

inline void C_EngineObjectInternal::SetSimulationTime(float st)
{
	m_flSimulationTime = st;
}

inline float C_EngineObjectInternal::GetOldSimulationTime() const
{
	return m_flOldSimulationTime;
}

inline bool C_EngineObjectInternal::IsModelScaleFractional() const   /// very fast way to ask if the model scale is < 1.0f
{
	COMPILE_TIME_ASSERT(sizeof(m_flModelScale) == sizeof(int));
	return *((const int*)&m_flModelScale) < 0x3f800000;
}

inline bool C_EngineObjectInternal::IsModelScaled() const
{
	return (m_flModelScale > 1.0f + FLT_EPSILON || m_flModelScale < 1.0f - FLT_EPSILON);
}

inline const model_t* C_EngineObjectInternal::GetModel(void) const
{
	return m_pModel;
}

//-----------------------------------------------------------------------------
// Purpose: return a pointer to an updated studiomdl cache cache
//-----------------------------------------------------------------------------

inline IStudioHdr* C_EngineObjectInternal::GetModelPtr() const
{
	//if ( IsDynamicModelLoading() )
	//	return NULL;

#ifdef _DEBUG
	// GetModelPtr() is often called before OnNewModel() so go ahead and set it up first chance.
//	static IDataCacheSection *pModelCache = datacache->FindSection( "ModelData" );
//	AssertOnce( pModelCache->IsFrameLocking() );
#endif
	if (!m_pStudioHdr)
	{
		const_cast<C_EngineObjectInternal*>(this)->LockStudioHdr();
	}
	Assert(m_pStudioHdr ? m_pStudioHdr == mdlcache->GetIStudioHdr(m_hStudioHdr) : m_hStudioHdr == MDLHANDLE_INVALID);
	return m_pStudioHdr;
}


inline void C_EngineObjectInternal::InvalidateMdlCache()
{
	if (m_pStudioHdr)
	{
		UnlockStudioHdr();
		m_pStudioHdr = NULL;
	}
}

inline float C_EngineObjectInternal::GetCycle() const
{
	return m_flCycle;
}

inline float C_EngineObjectInternal::GetPlaybackRate()
{
	return m_flPlaybackRate;
}

inline void C_EngineObjectInternal::SetPlaybackRate(float rate)
{
	m_flPlaybackRate = rate;
}

//-----------------------------------------------------------------------------
// Sequence access
//-----------------------------------------------------------------------------
inline int C_EngineObjectInternal::GetSequence()
{
	return m_nSequence;
}

//-----------------------------------------------------------------------------
// Purpose: Serves the 90% case of calling SetSequence / ResetSequenceInfo.
//-----------------------------------------------------------------------------
inline void C_EngineObjectInternal::ResetSequence(int nSequence)
{
	SetSequence(nSequence);
	ResetSequenceInfo();
}

inline bool C_EngineObjectInternal::IsSequenceFinished(void)
{
	return m_bSequenceFinished;
}

inline bool C_EngineObjectInternal::ShouldMuzzleFlash() const
{
	return m_nOldMuzzleFlashParity != m_nMuzzleFlashParity;
}

//-----------------------------------------------------------------------------
// Mouth
//-----------------------------------------------------------------------------
inline CMouthInfo& C_EngineObjectInternal::MouthInfo()
{
	return m_mouth;
}

inline float C_EngineObjectInternal::SequenceDuration(void)
{
	return SequenceDuration(GetSequence());
}


//-----------------------------------------------------------------------------
// Methods relating to bounds
//-----------------------------------------------------------------------------
inline const Vector& C_EngineObjectInternal::WorldAlignMins() const
{
	Assert(!IsBoundsDefinedInEntitySpace());
	Assert(GetCollisionAngles() == vec3_angle);
	return OBBMins();
}

inline const Vector& C_EngineObjectInternal::WorldAlignMaxs() const
{
	Assert(!IsBoundsDefinedInEntitySpace());
	Assert(GetCollisionAngles() == vec3_angle);
	return OBBMaxs();
}

inline const Vector& C_EngineObjectInternal::WorldAlignSize() const
{
	Assert(!IsBoundsDefinedInEntitySpace());
	Assert(GetCollisionAngles() == vec3_angle);
	return OBBSize();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : handle - 
// Output : inline void
//-----------------------------------------------------------------------------
inline void C_EngineObjectInternal::SetToolHandle(HTOOLHANDLE handle)
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
inline HTOOLHANDLE C_EngineObjectInternal::GetToolHandle() const
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
inline bool C_EngineObjectInternal::IsEnabledInToolView() const
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
inline bool C_EngineObjectInternal::ShouldRecordInTools() const
{
#ifndef NO_TOOLFRAMEWORK
	return m_bRecordInTools;
#else
	return true;
#endif
}

inline const matrix3x4_t& C_EngineObjectInternal::GetBone(int iBone) const
{
	return m_BoneAccessor.GetBone(iBone);
}

inline const matrix3x4_t* C_EngineObjectInternal::GetBoneArray() const
{
	return m_BoneAccessor.GetBoneArrayForWrite();
}

inline matrix3x4_t& C_EngineObjectInternal::GetBoneForWrite(int iBone)
{
	return m_BoneAccessor.GetBoneForWrite(iBone);
}

inline bool C_EngineObjectInternal::GetAimEntOrigin(Vector* pAbsOrigin, QAngle* pAbsAngles)
{
	if (!m_pBoneMergeCache) {
		return false;
	}
	return m_pBoneMergeCache->GetAimEntOrigin(pAbsOrigin, pAbsAngles);
}

inline const ClientRenderHandle_t& C_EngineObjectInternal::GetRenderHandle() const
{
	return m_hRender;
}

//-----------------------------------------------------------------------------
// Should we be interpolating during this frame? (was EF_NOINTERP)
//-----------------------------------------------------------------------------
inline bool C_EngineObjectInternal::IsNoInterpolationFrame()
{
	return m_ubOldInterpolationFrame != m_ubInterpolationFrame;
}

class C_EngineWorldInternal : public C_EngineObjectInternal {
public:
	DECLARE_CLASS(C_EngineWorldInternal, C_EngineObjectInternal);
	C_EngineWorldInternal(IClientEntityList* pClientEntityList, int iForceEdictIndex, int iSerialNum)
	:C_EngineObjectInternal(pClientEntityList, iForceEdictIndex, iSerialNum)
	{
		
	}
};

class C_EnginePlayerInternal : public C_EngineObjectInternal, public IEnginePlayerClient {
public:
	DECLARE_CLASS(C_EnginePlayerInternal, C_EngineObjectInternal);
	DECLARE_CLIENTCLASS();
	C_EnginePlayerInternal(IClientEntityList* pClientEntityList, int iForceEdictIndex, int iSerialNum);
	~C_EnginePlayerInternal();
	IEnginePortalClient* GetPortalEnvironment() { return m_hPortalEnvironment ? m_hPortalEnvironment->GetEnginePortal() : NULL; }
	IEnginePortalClient* GetHeldObjectPortal(void) { return m_pHeldObjectPortal ? m_pHeldObjectPortal->GetEnginePortal() : NULL; }
	void ToggleHeldObjectOnOppositeSideOfPortal(void) { m_bHeldObjectOnOppositeSideOfPortal = !m_bHeldObjectOnOppositeSideOfPortal; }
	void SetHeldObjectOnOppositeSideOfPortal(bool p_bHeldObjectOnOppositeSideOfPortal) { m_bHeldObjectOnOppositeSideOfPortal = p_bHeldObjectOnOppositeSideOfPortal; }
	bool IsHeldObjectOnOppositeSideOfPortal(void) { return m_bHeldObjectOnOppositeSideOfPortal; }
private:
	EHANDLE	m_hPortalEnvironment; //a portal whose environment the player is currently in, should be invalid most of the time
	EHANDLE m_pHeldObjectPortal;
	bool  m_bHeldObjectOnOppositeSideOfPortal;

};

class C_EnginePortalInternal : public C_EngineObjectInternal, public IEnginePortalClient {
public:
	DECLARE_CLASS(C_EnginePortalInternal, C_EngineObjectInternal);
	DECLARE_CLIENTCLASS();
	C_EnginePortalInternal(IClientEntityList* pClientEntityList, int iForceEdictIndex, int iSerialNum);
	~C_EnginePortalInternal();
	void	VPhysicsDestroyObject(void);
	int					GetPortalSimulatorGUID(void) const { return m_iPortalSimulatorGUID; };
	void				SetVPhysicsSimulationEnabled(bool bEnabled); //enable/disable vphysics simulation. Will automatically update the linked portal to be the same
	bool				IsSimulatingVPhysics(void) const; //this portal is setup to handle any physically simulated object, false means the portal is handling player movement only
	bool				IsLocalDataIsReady() const { return m_bLocalDataIsReady; }
	void				SetLocalDataIsReady(bool bLocalDataIsReady) { m_bLocalDataIsReady = bLocalDataIsReady; }
	bool				IsReadyToSimulate(void) const; //is active and linked to another portal
	bool				IsActivedAndLinked(void) const;
	void				MoveTo(const Vector& ptCenter, const QAngle& angles);
	void				AttachTo(IEnginePortalClient* pLinkedPortal);
	C_EnginePortalInternal* GetLinkedPortal() { return m_hLinkedPortal.Get() ? (C_EnginePortalInternal*)m_hLinkedPortal.Get()->GetEnginePortal() : NULL; }
	const C_EnginePortalInternal* GetLinkedPortal() const { return m_hLinkedPortal.Get() ? (const C_EnginePortalInternal*)m_hLinkedPortal.Get()->GetEnginePortal() : NULL; }
	void				DetachFromLinked(void);
	void				UpdateLinkMatrix(IEnginePortalClient* pRemoteCollisionEntity);
	bool				EntityIsInPortalHole(IEngineObjectClient* pEntity) const; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	bool				EntityHitBoxExtentIsInPortalHole(IEngineObjectClient* pBaseAnimating) const; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	bool				RayIsInPortalHole(const Ray_t& ray) const; //traces a ray against the same detector for EntityIsInPortalHole(), bias is towards false positives
	bool				TraceWorldBrushes(const Ray_t& ray, trace_t* pTrace) const;
	bool				TraceWallTube(const Ray_t& ray, trace_t* pTrace) const;
	bool				TraceWallBrushes(const Ray_t& ray, trace_t* pTrace) const;
	bool				TraceTransformedWorldBrushes(const IEnginePortalClient* pRemoteCollisionEntity, const Ray_t& ray, trace_t* pTrace) const;
	void				TraceRay(const Ray_t& ray, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace, bool bTraceHolyWall = true) const; //traces against a specific portal's environment, does no *real* tracing
	void				TraceEntity(C_BaseEntity* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter* pFilter, trace_t* ptr) const;
	int					GetStaticPropsCount() const;
	const PS_SD_Static_World_StaticProps_ClippedProp_t* GetStaticProps(int index) const;
	bool				StaticPropsCollisionExists() const;
	//const Vector& GetOrigin() const;
	//const QAngle& GetAngles() const;
	const Vector& GetTransformedOrigin() const;
	const QAngle& GetTransformedAngles() const;
	const VMatrix& MatrixThisToLinked() const;
	const VMatrix& MatrixLinkedToThis() const;
	const cplane_t& GetPortalPlane() const;
	const Vector& GetVectorForward() const;
	const Vector& GetVectorUp() const;
	const Vector& GetVectorRight() const;
	const PS_SD_Static_SurfaceProperties_t& GetSurfaceProperties() const;
	IPhysicsObject* GetWorldBrushesPhysicsObject() const;
	IPhysicsObject* GetWallBrushesPhysicsObject() const;
	IPhysicsObject* GetWallTubePhysicsObject() const;
	IPhysicsObject* GetRemoteWallBrushesPhysicsObject() const;
	IPhysicsEnvironment* GetPhysicsEnvironment();
	void				CreatePhysicsEnvironment();
	void				ClearPhysicsEnvironment();
	void				CreatePolyhedrons(void);
	void				ClearPolyhedrons(void);
	void				CreateLocalCollision(void);
	void				ClearLocalCollision(void);
	void				CreateLocalPhysics(void);
	void				CreateLinkedPhysics(IEnginePortalClient* pRemoteCollisionEntity);
	void				ClearLocalPhysics(void);
	void				ClearLinkedPhysics(void);
	bool				CreatedPhysicsObject(const IPhysicsObject* pObject, PS_PhysicsObjectSourceType_t* pOut_SourceType = NULL) const; //true if the physics object was generated by this portal simulator
	void				CreateHoleShapeCollideable();
	void				ClearHoleShapeCollideable();
	void				BeforeMove() {}
	void				AfterMove() {}
	C_EngineObjectInternal* AsEngineObject() { return this; }
	const C_EngineObjectInternal* AsEngineObject() const { return this; }
	bool				IsActivated() const { return m_bActivated; }
	bool				IsPortal2() const { return m_bIsPortal2; }
	void				SetPortal2(bool bPortal2) { m_bIsPortal2 = bPortal2; }
private:
	int					m_iPortalSimulatorGUID;
	//IPhysicsEnvironment* pPhysicsEnvironment = NULL;
	bool				m_bActivated; //a portal can exist and not be active
	bool				m_bIsPortal2; //For teleportation, this doesn't matter, but for drawing and moving, it matters
	EHANDLE				m_hLinkedPortal;
	bool				m_bSimulateVPhysics;
	bool				m_bLocalDataIsReady; //this side of the portal is properly setup, no guarantees as to linkage to another portal
	PS_InternalData_t m_InternalData;
	const PS_InternalData_t& m_DataAccess;
	IPhysicsEnvironment* m_pPhysicsEnvironment = NULL;
};

inline bool C_EnginePortalInternal::IsReadyToSimulate(void) const
{
	return m_bLocalDataIsReady && GetLinkedPortal() && GetLinkedPortal()->m_bLocalDataIsReady;
}

inline bool C_EnginePortalInternal::IsActivedAndLinked(void) const
{
	return (m_bActivated && GetLinkedPortal() != NULL);
}

class C_EngineShadowCloneInternal : public C_EngineObjectInternal {
public:
	DECLARE_CLASS(C_EngineShadowCloneInternal, C_EngineObjectInternal);
	C_EngineShadowCloneInternal(IClientEntityList* pClientEntityList, int iForceEdictIndex, int iSerialNum)
	:C_EngineObjectInternal(pClientEntityList, iForceEdictIndex, iSerialNum)
	{
	
	}
};

class C_EngineVehicleInternal : public C_EngineObjectInternal, public IEngineVehicleClient {
public:
	DECLARE_CLASS(C_EngineVehicleInternal, C_EngineObjectInternal);
	C_EngineVehicleInternal(IClientEntityList* pClientEntityList, int iForceEdictIndex, int iSerialNum)
	:C_EngineObjectInternal(pClientEntityList, iForceEdictIndex, iSerialNum)
	{
	
	}
};

class C_EngineRopeInternal : public C_EngineObjectInternal, public IEngineRopeClient {
public:
	DECLARE_CLASS(C_EngineRopeInternal, C_EngineObjectInternal);
	DECLARE_CLIENTCLASS();
	C_EngineRopeInternal(IClientEntityList* pClientEntityList, int iForceEdictIndex, int iSerialNum);
	~C_EngineRopeInternal();
	virtual void	OnDataChanged(DataUpdateType_t updateType);
	// Use this when rope length and slack change to recompute the spring length.
	void			RecomputeSprings();
	void			UpdateBBox();
	void			CalcLightValues();
	void			ShakeRope(const Vector& vCenter, float flRadius, float flMagnitude);
	bool			AnyPointsMoved();
	bool			InitRopePhysics();
	void			ConstrainNodesBetweenEndpoints(void);
	bool			DetectRestingState(bool& bApplyWind);
	// Specify ROPE_ATTACHMENT_START_POINT or ROPE_ATTACHMENT_END_POINT for the attachment.
	virtual	bool	GetAttachment(int number, Vector& origin, QAngle& angles);
	virtual bool	GetAttachment(int number, matrix3x4_t& matrix);
	// Hook the physics. Pass in your own implementation of CSimplePhysics::IHelper. The
// default implementation is returned so you can call through to it if you want.
	//CSimplePhysics::IHelper* HookPhysics(CSimplePhysics::IHelper* pHook);
	// Get the attachment position of one of the endpoints.
	bool			GetEndPointPos(int iPt, Vector& vPos, QAngle& vAngle);
	bool			CalculateEndPointAttachment(C_BaseEntity* pEnt, int iAttachment, Vector& vPos, QAngle& pAngles);
	void SetRopeFlags(int flags);
	int GetRopeFlags() const;
	int				GetSlack() { return m_Slack; }
	// Set the slack.
	void SetSlack(int slack);
	void SetupHangDistance(float flHangDist);
	// Change which entities the rope is connected to.
	void SetStartEntity(C_BaseEntity* pEnt);
	void SetEndEntity(C_BaseEntity* pEnt);

	C_BaseEntity* GetStartEntity() const;
	C_BaseEntity* GetEndEntity() const;
	// Get the rope material data.
	IMaterial* GetSolidMaterial(void);
	IMaterial* GetBackMaterial(void);
	// Client-only right now. This could be moved to the server if there was a good reason.
	void			SetColorMod(const Vector& vColorMod);
	Vector* GetRopeSubdivVectors(int* nSubdivs);
	float			GetTextureScale() {return m_TextureScale;}
	int				GetTextureHeight() { return m_TextureHeight; }
	float			GetCurScroll() { return m_flCurScroll; }
	float			GetWidth() { return m_Width; }
	void			SetWidth(float fWidth) { m_Width = fWidth; }
	CRopePhysics<ROPE_MAX_SEGMENTS>& GetRopePhysics() { return m_RopePhysics; }
	Vector*			GetLightValues() { return m_LightValues; }
	Vector&			GetColorMod() { return m_vColorMod; }
	int				GetRopeLength() { return m_RopeLength; }
	int&			GetLockedPoints() { return m_fLockedPoints; }
	void			SetStartAttachment(short iStartAttachment) { m_iStartAttachment = iStartAttachment; }
	void			SetEndAttachment(short iEndAttachment) { m_iEndAttachment = iEndAttachment; }
	void			SetSegments(int	nSegments) { m_nSegments = nSegments; }
	int&			GetRopeFlags() { return m_RopeFlags; }
	void			FinishInit(const char* pMaterialName);
	void			SetRopeLength(int RopeLength) { m_RopeLength = RopeLength; }
	void			SetTextureScale(float TextureScale) { m_TextureScale = TextureScale; }
	void			AddToRenderCache();
	void			RopeThink();
	Vector&			GetImpulse() { return m_flImpulse; }
private:
	void			RunRopeSimulation(float flSeconds);
	Vector			ConstrainNode(const Vector& vNormal, const Vector& vNodePosition, const Vector& vMidpiont, float fNormalLength);
	bool			DidEndPointMove(int iPt);
	bool			GetEndPointAttachment(int iPt, Vector& vPos, QAngle& angle);

	class CPhysicsDelegate : public CSimplePhysics::IHelper
	{
	public:
		virtual void	GetNodeForces(CSimplePhysics::CNode* pNodes, int iNode, Vector* pAccel);
		virtual void	ApplyConstraints(CSimplePhysics::CNode* pNodes, int nNodes);

		C_EngineRopeInternal* m_pKeyframe;
	};

	friend class CPhysicsDelegate;

	CRopePhysics<ROPE_MAX_SEGMENTS>	m_RopePhysics;
	CPhysicsDelegate	m_PhysicsDelegate;
	int				m_RopeLength;		// Length of the rope, used for tension.
	int				m_Slack;			// Extra length the rope is given.
	int				m_nSegments;		// Number of segments.
	// Instantaneous force
	Vector			m_flImpulse;
	IMaterial* m_pMaterial;
	IMaterial* m_pBackMaterial;			// Optional translucent background material for the rope to help reduce aliasing.
	int				m_TextureHeight;	// Texture height, for texture scale calculations.
	// Track which links touched something last frame. Used to prevent wind from gusting on them.
	CBitVec<ROPE_MAX_SEGMENTS>		m_LinksTouchingSomething;
	int								m_nLinksTouchingSomething;
	// In network table, can't bit-compress
	bool			m_bConstrainBetweenEndpoints;	// Simulated segment points won't stretch beyond the endpoints
	Vector			m_vCachedEndPointAttachmentPos[2];
	QAngle			m_vCachedEndPointAttachmentAngle[2];
	int								m_iForcePointMoveCounter;
	int								m_fPrevLockedPoints;	// Which points are locked down.
	int				m_fLockedPoints;	// Which points are locked down.
	bool			m_bNewDataThisFrame : 1;			// Set to true in OnDataChanged so that we simulate that frame
	int				m_RopeFlags;			// Combo of ROPE_ flags.
	Vector			m_flPreviousImpulse;
	bool			m_bPhysicsInitted : 1;				// It waits until all required entities are 
	// Used to control resting state.
	bool			m_bPrevEndPointPos[2];
	Vector			m_vPrevEndPointPos[2];
	float			m_flTimeToNextGust;			// When will the next wind gust be?
	Vector			m_LightValues[ROPE_MAX_SEGMENTS]; // light info when the rope is created.
	bool			m_bEndPointAttachmentPositionsDirty : 1;
	bool			m_bEndPointAttachmentAnglesDirty : 1;
	EHANDLE			m_hStartPoint;		// StartPoint/EndPoint are entities
	EHANDLE			m_hEndPoint;
	short			m_iStartAttachment;	// StartAttachment/EndAttachment are attachment points.
	short			m_iEndAttachment;
	bool							m_bApplyWind;
	// Simulated wind gusts.
	float			m_flCurrentGustTimer;
	float			m_flCurrentGustLifetime;	// How long will the current gust last?
	Vector			m_vWindDir;					// What direction does the current gust go in?
	float			m_flCurScroll;		// for scrolling texture.
	float			m_flScrollSpeed;
	int				m_iRopeMaterialModelIndex;	// Index of sprite model with the rope's material.
	unsigned char	m_Subdiv;			// Number of subdivions in between segments.
	float			m_TextureScale;		// pixels per inch
	float				m_Width;
	Vector			m_vColorMod;				// Color modulation on all verts?
};

IRopeManager* RopeManager();
void Rope_ResetCounters();

class C_EngineGhostInternal : public C_EngineObjectInternal, public IEngineGhostClient {
public:
	DECLARE_CLASS(C_EngineGhostInternal, C_EngineObjectInternal);
	C_EngineGhostInternal(IClientEntityList* pClientEntityList, int iForceEdictIndex, int iSerialNum)
	:C_EngineObjectInternal(pClientEntityList, iForceEdictIndex, iSerialNum)
	{

	}

	void SetMatGhostTransform(const VMatrix& matGhostTransform) {
		m_matGhostTransform = matGhostTransform;
	}

	void SetGhostedSource(C_BaseEntity* pGhostedSource) {
		m_pGhostedSource = pGhostedSource;
		m_bSourceIsBaseAnimating = m_pGhostedSource ? m_pGhostedSource->GetEngineObject()->GetModelPtr() != NULL : NULL;
	}

	C_BaseEntity* GetGhostedSource() { return m_pGhostedSource; }
	bool GetSourceIsBaseAnimating() { return m_bSourceIsBaseAnimating; }
	void PerFrameUpdate(void);
	virtual Vector const& GetRenderOrigin(void);
	virtual QAngle const& GetRenderAngles(void);
	virtual bool	SetupBones(matrix3x4_t* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime);
	// Returns the bounds relative to the origin (render bounds)
	virtual void	GetRenderBounds(Vector& mins, Vector& maxs);
	// returns the bounds as an AABB in worldspace
	virtual void	GetRenderBoundsWorldspace(Vector& mins, Vector& maxs);
	virtual void	GetShadowRenderBounds(Vector& mins, Vector& maxs, ShadowType_t shadowType);
	virtual const matrix3x4_t& RenderableToWorldTransform();
	virtual int	LookupAttachment(const char* pAttachmentName);
	virtual	bool GetAttachment(int number, Vector& origin, QAngle& angles);
	virtual bool GetAttachment(int number, matrix3x4_t& matrix);
	virtual bool GetAttachmentVelocity(int number, Vector& originVel, Quaternion& angleVel);
	// Get the model instance of the ghosted model so that decals will properly draw across portals
	virtual ModelInstanceHandle_t GetModelInstance();
private:
	C_BaseEntity* m_pGhostedSource; //the renderable we're transforming and re-rendering
	bool m_bSourceIsBaseAnimating;
	VMatrix m_matGhostTransform;
	struct
	{
		Vector vRenderOrigin;
		QAngle qRenderAngle;
		matrix3x4_t matRenderableToWorldTransform;
	} m_ReferencedReturns; //when returning a reference, it has to actually exist somewhere
};

// Use this to iterate over *all* (even dormant) the C_BaseEntities in the client entity list.
//class C_AllBaseEntityIterator
//{
//public:
//	C_AllBaseEntityIterator();
//
//	void Restart();
//	C_BaseEntity* Next();	// keep calling this until it returns null.
//
//private:
//	unsigned short m_CurBaseEntity;
//};

struct clientanimating_t
{
	C_EngineObjectInternal* pAnimating;
	unsigned int	flags;
	clientanimating_t(C_EngineObjectInternal* _pAnim, unsigned int _flags) : pAnimating(_pAnim), flags(_flags) {}
};

extern bool ShouldRemoveThisRagdoll(C_BaseEntity* pRagdoll);

class CCollisionEvent : public IPhysicsCollisionEvent, public IPhysicsCollisionSolver, public IPhysicsObjectEvent
{
public:
	CCollisionEvent(void) = default;

	void	ObjectSound(int index, vcollisionevent_t* pEvent);
	void	PreCollision(vcollisionevent_t* pEvent) {}
	void	PostCollision(vcollisionevent_t* pEvent);
	void	Friction(IPhysicsObject* pObject, float energy, int surfaceProps, int surfacePropsHit, IPhysicsCollisionData* pData);

	void	BufferTouchEvents(bool enable) { m_bBufferTouchEvents = enable; }

	void	StartTouch(IPhysicsObject* pObject1, IPhysicsObject* pObject2, IPhysicsCollisionData* pTouchData);
	void	EndTouch(IPhysicsObject* pObject1, IPhysicsObject* pObject2, IPhysicsCollisionData* pTouchData);

	void	FluidStartTouch(IPhysicsObject* pObject, IPhysicsFluidController* pFluid);
	void	FluidEndTouch(IPhysicsObject* pObject, IPhysicsFluidController* pFluid);
	void	PostSimulationFrame() {}

	virtual void ObjectEnterTrigger(IPhysicsObject* pTrigger, IPhysicsObject* pObject) {}
	virtual void ObjectLeaveTrigger(IPhysicsObject* pTrigger, IPhysicsObject* pObject) {}

	float	DeltaTimeSinceLastFluid(C_BaseEntity* pEntity);
	void	FrameUpdate(void);

	void	UpdateFluidEvents(void);
	void	UpdateTouchEvents(void);

	// IPhysicsCollisionSolver
	int		ShouldCollide(IPhysicsObject* pObj0, IPhysicsObject* pObj1, void* pGameData0, void* pGameData1);
#if _DEBUG
	int		ShouldCollide_2(IPhysicsObject* pObj0, IPhysicsObject* pObj1, void* pGameData0, void* pGameData1);
#endif
	// debugging collision problem in TF2
	int		ShouldSolvePenetration(IPhysicsObject* pObj0, IPhysicsObject* pObj1, void* pGameData0, void* pGameData1, float dt);
	bool	ShouldFreezeObject(IPhysicsObject* pObject) { return true; }
	int		AdditionalCollisionChecksThisTick(int currentChecksDone) { return 0; }
	bool ShouldFreezeContacts(IPhysicsObject** pObjectList, int objectCount) { return true; }

	// IPhysicsObjectEvent
	virtual void ObjectWake(IPhysicsObject* pObject)
	{
		C_BaseEntity* pEntity = static_cast<C_BaseEntity*>(pObject->GetGameData());
		if (pEntity && pEntity->GetEngineObject()->HasDataObjectType(VPHYSICSWATCHER))
		{
			//ReportVPhysicsStateChanged( pObject, pEntity, true );
			pEntity->NotifyVPhysicsStateChanged(pObject, true);
		}
	}

	virtual void ObjectSleep(IPhysicsObject* pObject)
	{
		C_BaseEntity* pEntity = static_cast<C_BaseEntity*>(pObject->GetGameData());
		if (pEntity && pEntity->GetEngineObject()->HasDataObjectType(VPHYSICSWATCHER))
		{
			//ReportVPhysicsStateChanged( pObject, pEntity, false );
			pEntity->NotifyVPhysicsStateChanged(pObject, false);
		}
	}


	friction_t* FindFriction(C_BaseEntity* pObject);
	void ShutdownFriction(friction_t& friction);
	void UpdateFrictionSounds();
	bool IsInCallback() { return m_inCallback > 0 ? true : false; }

private:
	class CallbackContext
	{
	public:
		CallbackContext(CCollisionEvent* pOuter)
		{
			m_pOuter = pOuter;
			m_pOuter->m_inCallback++;
		}
		~CallbackContext()
		{
			m_pOuter->m_inCallback--;
		}
	private:
		CCollisionEvent* m_pOuter;
	};
	friend class CallbackContext;

	void	AddTouchEvent(C_BaseEntity* pEntity0, C_BaseEntity* pEntity1, int touchType, const Vector& point, const Vector& normal);
	void	DispatchStartTouch(C_BaseEntity* pEntity0, C_BaseEntity* pEntity1, const Vector& point, const Vector& normal);
	void	DispatchEndTouch(C_BaseEntity* pEntity0, C_BaseEntity* pEntity1);

	friction_t					m_current[8];
	CUtlVector<fluidevent_t>	m_fluidEvents;
	CUtlVector<touchevent_t>	m_touchEvents;
	int							m_inCallback;
	bool						m_bBufferTouchEvents;
};

class C_WatcherList : public IWatcherList
{
public:
	//CWatcherList(); NOTE: Dataobj doesn't support constructors - it zeros the memory
	~C_WatcherList();	// frees the positionwatcher_t's to the pool
	void Init();

	void AddToList(IHandleEntity* pWatcher);
	void RemoveWatcher(IHandleEntity* pWatcher);

private:
	int GetCallbackObjects(IWatcherCallback** pList, int listMax);

	unsigned short Find(IHandleEntity* pEntity);
	unsigned short m_list;
};

extern ConVar cl_phys_timescale;

//
// This is the IClientEntityList implemenation. It serves two functions:
//
// 1. It converts server entity indices into IClientNetworkables for the engine.
//
// 2. It provides a place to store IClientUnknowns and gives out CBaseHandle's
//    so they can be indexed and retreived. For example, this is how static props are referenced
//    by the spatial partition manager - it doesn't know what is being inserted, so it's 
//	  given CBaseHandle's, and the handlers for spatial partition callbacks can
//    use the client entity list to look them up and check for supported interfaces.
//
template<class T>// = IHandleEntity
class CClientEntityList : public CBaseEntityList<T>, public IClientEntityList, public IEntityCallBack
{
	friend class C_BaseEntityIterator;
	friend class C_EngineObjectInternal;
	friend class C_EnginePortalInternal;
	friend class C_GrabControllerInternal;
	friend class CPortalTouchScope;
	//friend class C_AllBaseEntityIterator;
	typedef CBaseEntityList<T> BaseClass;
public:
	// Constructor, destructor
								CClientEntityList( void );
	virtual 					~CClientEntityList( void );

	virtual bool Init();
	virtual void Shutdown();

	// Level init, shutdown
	virtual void LevelInitPreEntity();
	virtual void LevelInitPostEntity();

	// The level is shutdown in two parts
	virtual void LevelShutdownPreEntity();

	virtual void LevelShutdownPostEntity();

	virtual void InstallEntityFactory(IEntityFactory* pFactory);
	virtual void UninstallEntityFactory(IEntityFactory* pFactory);
	virtual bool CanCreateEntityClass(const char* pClassName);
	virtual const char* GetMapClassName(const char* pClassName);
	virtual const char* GetDllClassName(const char* pClassName);
	virtual size_t		GetEntitySize(const char* pClassName);
	virtual const char* GetCannonicalName(const char* pClassName);
	virtual void ReportEntitySizes();
	virtual void DumpEntityFactories();

	void						Release();		// clears everything and releases entities

	virtual const char*			GetBlockName();

	virtual void				PreSave(CSaveRestoreData* pSaveData);
	virtual void				Save(ISave* pSave);
	virtual void				WriteSaveHeaders(ISave* pSave);
	virtual void				PostSave();

	virtual void				PreRestore();
	virtual void				ReadRestoreHeaders(IRestore* pRestore);
	virtual void				Restore(IRestore* pRestore, bool createPlayers);
	virtual void				PostRestore();
// Implement IClientEntityList
public:

	virtual C_BaseEntity*		CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1);
	virtual void				DestroyEntity(IHandleEntity* pEntity);

	virtual IEngineObjectClient* GetEngineObject(int entnum);
	virtual IEngineObjectClient* GetEngineObjectFromHandle(CBaseHandle handle);
	virtual IClientNetworkable*	GetClientNetworkable( int entnum );
	virtual IClientEntity*		GetClientEntity( int entnum );

	virtual int					NumberOfEntities( bool bIncludeNonNetworkable = false );

	virtual T*					GetClientUnknownFromHandle( CBaseHandle hEnt );
	virtual IClientNetworkable*	GetClientNetworkableFromHandle( CBaseHandle hEnt );
	virtual IClientEntity*		GetClientEntityFromHandle( CBaseHandle hEnt );

	virtual int					GetHighestEntityIndex( void );

	virtual void				SetMaxEntities( int maxents );
	virtual int					GetMaxEntities( );


// CBaseEntityList overrides.
protected:

	virtual void AfterCreated(IHandleEntity* pEntity);
	virtual void BeforeDestroy(IHandleEntity* pEntity);
	virtual void OnAddEntity( T *pEnt, CBaseHandle handle );
	virtual void OnRemoveEntity( T *pEnt, CBaseHandle handle );
	bool SaveInitEntities(CSaveRestoreData* pSaveData);
	bool DoRestoreEntity(T* pEntity, IRestore* pRestore);
	int RestoreEntity(T* pEntity, IRestore* pRestore, entitytable_t* pEntInfo);
	void SaveEntityOnTable(T* pEntity, CSaveRestoreData* pSaveData, int& iSlot);
// Internal to client DLL.
public:

	// All methods of accessing specialized IClientUnknown's go through here.
	T*			GetListedEntity( int entnum );
	
	// Simple wrappers for convenience..
	C_BaseEntity*			GetBaseEntity( int entnum );
	ICollideable*			GetCollideable( int entnum );

	IClientRenderable*		GetClientRenderableFromHandle( CBaseHandle hEnt );
	C_BaseEntity*			GetBaseEntityFromHandle( CBaseHandle hEnt );
	ICollideable*			GetCollideableFromHandle( CBaseHandle hEnt );
	IClientThinkable*		GetClientThinkableFromHandle( CBaseHandle hEnt );

	CBaseHandle				FirstHandle() const { return BaseClass::FirstHandle(); }
	CBaseHandle				NextHandle(CBaseHandle hEnt) const { return BaseClass::NextHandle(hEnt); }
	CBaseHandle				InvalidHandle() { return BaseClass::InvalidHandle(); }

	// Is a handle valid?
	bool					IsHandleValid( CBaseHandle handle ) const;

	void					RecomputeHighestEntityUsed( void );

	// Use this to iterate over all the C_BaseEntities.
	C_BaseEntity*			FirstBaseEntity() const;
	C_BaseEntity*			NextBaseEntity( C_BaseEntity *pEnt ) const;

	// Get the list of all PVS notifiers.
	CUtlLinkedList<CPVSNotifyInfo,unsigned short>& GetPVSNotifiers();

	// add a class that gets notified of entity events
	void AddListenerEntity( IClientEntityListener *pListener );
	void RemoveListenerEntity( IClientEntityListener *pListener );

	void NotifyCreateEntity( C_BaseEntity *pEnt );
	void NotifyRemoveEntity( C_BaseEntity *pEnt );

	void AddDataAccessor(int type, IEntityDataInstantiator<T>* instantiator);
	void RemoveDataAccessor(int type);
	void* GetDataObject(int type, const T* instance);
	void* CreateDataObject(int type, T* instance);
	void DestroyDataObject(int type, T* instance);
	void PushAllowBoneAccess(bool bAllowForNormalModels, bool bAllowForViewModels, char const* tagPush);
	void PopBoneAccess(char const* tagPop);
	bool GetAllowBoneAccessForNormalModels() { return m_BoneAcessBase.bAllowBoneAccessForNormalModels; }
	bool GetAllowBoneAccessForViewModels() { return m_BoneAcessBase.bAllowBoneAccessForViewModels; }
	void AddToRecordList(CBaseHandle add);
	void RemoveFromRecordList(CBaseHandle remove);

	virtual int		CountRecord();
	IClientRenderable* GetRecord(int index);
	// For entities marked for recording, post bone messages to IToolSystems
	void ToolRecordEntities();
	bool GetInThreadedBoneSetup() { return m_bInThreadedBoneSetup; }
	unsigned long GetModelBoneCounter() { return m_iModelBoneCounter; }
	bool GetDoThreadedBoneSetup() { return m_bDoThreadedBoneSetup; }
	unsigned long GetPreviousBoneCounter() { return m_iPreviousBoneCounter; }
	CUtlVector<IEngineObjectClient*>& GetPreviousBoneSetups() { return m_PreviousBoneSetups; }
	void						SetupBonesOnBaseAnimating(IEngineObjectClient*& pBaseAnimating);
	void						PreThreadedBoneSetup();
	void						PostThreadedBoneSetup();
	void						ThreadedBoneSetup();
	void						InitBoneSetupThreadPool();
	void						ShutdownBoneSetupThreadPool();
	// Invalidate bone caches so all SetupBones() calls force bone transforms to be regenerated.
	void						InvalidateBoneCaches();

	void SetAbsQueriesValid(bool bValid);
	bool IsAbsQueriesValid(void);
	// Enable/disable abs recomputations on a stack.
	void PushEnableAbsRecomputations(bool bEnable);
	void PopEnableAbsRecomputations();
	// This requires the abs recomputation stack to be empty and just sets the global state. 
	// It should only be used at the scope of the frame loop.
	void EnableAbsRecomputations(bool bEnable);
	bool IsAbsRecomputationsEnabled(void);
	bool IsDisableTouchFuncs() { return m_bDisableTouchFuncs; }
	// Moves all aiments into their correct position for the frame
	void MarkAimEntsDirty();
	void CalcAimEntPositions();

	// Update client side animations
	void UpdateClientSideAnimations();
	void UpdateDirtySpatialPartitionEntities() {
		::UpdateDirtySpatialPartitionEntities();
	}

	int GetPredictionRandomSeed(void);
	void SetPredictionRandomSeed(const CUserCmd* cmd);
	IEngineObject* GetPredictionPlayer(void);
	void SetPredictionPlayer(IEngineObject* player);

	// Should we be interpolating?
	bool IsInterpolationEnabled();
	// Figure out the smoothly interpolated origin for all server entities. Happens right before
	// letting all entities simulate.
	void InterpolateServerEntities();
	bool IsSimulatingOnAlternateTicks();

	// Interpolate entity
	void ProcessTeleportList();
	void ProcessInterpolatedList();
	void CheckInterpolatedVarParanoidMeasurement();

	// Move it to the top of the LRU
	void MoveToTopOfLRU(C_BaseEntity* pRagdoll, bool bImportant = false);
	void SetMaxRagdollCount(int iMaxCount) { m_iMaxRagdolls = iMaxCount; }
	int CountRagdolls(bool bOnlySimulatingRagdolls) { return bOnlySimulatingRagdolls ? m_iSimulatedRagdollCount : m_iRagdollCount; }
	// Methods of IGameSystem
	virtual void UpdateRagdolls(float frametime);

	C_BaseEntity* GetLocalPlayer(void);
	void SetLocalPlayer(C_BaseEntity* pBasePlayer);

	IPhysics* Physics() {
		return m_physics;
	}

	IPhysicsEnvironment* PhysGetEnv() {
		return m_pPhysenv;
	}

	IPhysicsSurfaceProps* PhysGetProps() {
		return m_pPhysprops;
	}

	IPhysicsCollision* PhysGetCollision() {
		return m_pPhyscollision;
	}

	IPhysicsObjectPairHash* PhysGetEntityCollisionHash() {
		return m_EntityCollisionHash;
	}

	const objectparams_t& PhysGetDefaultObjectParams() {
		return g_PhysDefaultObjectParams;
	}

	IPhysicsObject* PhysGetWorldObject() {
		return m_PhysWorldObject;
	}

	void PhysicsReset()
	{
		if (!m_pPhysenv)
			return;

		m_pPhysenv->ResetSimulationClock();
	}

	void AddImpactSound(void* pGameData, IPhysicsObject* pObject, int surfaceProps, int surfacePropsHit, float volume, float speed);
	void PhysicsSimulate();

	void PhysFrictionSound(IHandleEntity* pEntity, IPhysicsObject* pObject, float energy, int surfaceProps, int surfacePropsHit)
	{
		if (!pEntity || energy < 75.0f || surfaceProps < 0)
			return;

		// don't make noise for hidden/invisible/sky materials
		surfacedata_t* phit = EntityList()->PhysGetProps()->GetSurfaceData(surfacePropsHit);
		surfacedata_t* psurf = EntityList()->PhysGetProps()->GetSurfaceData(surfaceProps);

		if (phit->game.material == 'X' || psurf->game.material == 'X')
			return;

		// rescale the incoming energy
		energy *= ENERGY_VOLUME_SCALE;

		// volume of scrape is proportional to square of energy (steeper rolloff at low energies)
		float volume = energy * energy;

		unsigned short soundName = psurf->sounds.scrapeRough;
		short* soundHandle = &psurf->soundhandles.scrapeRough;

		if (psurf->sounds.scrapeSmooth && phit->audio.roughnessFactor < psurf->audio.roughThreshold)
		{
			soundName = psurf->sounds.scrapeSmooth;
			soundHandle = &psurf->soundhandles.scrapeRough;
		}

		const char* pSoundName = EntityList()->PhysGetProps()->GetString(soundName);

		PhysFrictionSound(pEntity, pObject, pSoundName, *soundHandle, volume);
	}

	void PhysFrictionSound(IHandleEntity* pEntity, IPhysicsObject* pObject, const char* pSoundName, HSOUNDSCRIPTHANDLE& handle, float flVolume)
	{
		if (!pEntity)
			return;

		// cut out the quiet sounds
		// UNDONE: Separate threshold for starting a sound vs. continuing?
		flVolume = clamp(flVolume, 0.0f, 1.0f);
		if (flVolume > (1.0f / 128.0f))
		{
			friction_t* pFriction = m_Collisions.FindFriction((C_BaseEntity*)pEntity);
			if (!pFriction)
				return;

			CSoundParameters params;
			if (!g_pSoundEmitterSystem->GetParametersForSound(pSoundName, handle, params, NULL))//CBaseEntity::
				return;

			if (!pFriction->pObject)
			{
				// don't create really quiet scrapes
				if (params.volume * flVolume <= 0.1f)
					return;

				pFriction->pObject = pEntity;
				CPASAttenuationFilter filter((C_BaseEntity*)pEntity, params.soundlevel);
				int entindex = pEntity->entindex();

				// clientside created entites doesn't have a valid entindex, let 'world' play the sound for them
				if (entindex < 0)
					entindex = 0;

				pFriction->patch = CSoundEnvelopeController::GetController().SoundCreate(
					filter, entindex, CHAN_BODY, pSoundName, params.soundlevel);
				CSoundEnvelopeController::GetController().Play(pFriction->patch, params.volume * flVolume, params.pitch);
			}
			else
			{
				float pitch = (flVolume * (params.pitchhigh - params.pitchlow)) + params.pitchlow;
				CSoundEnvelopeController::GetController().SoundChangeVolume(pFriction->patch, params.volume * flVolume, 0.1f);
				CSoundEnvelopeController::GetController().SoundChangePitch(pFriction->patch, pitch, 0.1f);
			}

			pFriction->flLastUpdateTime = gpGlobals->curtime;
			pFriction->flLastEffectTime = gpGlobals->curtime;
		}
	}

	void PhysCleanupFrictionSounds(IHandleEntity* pEntity)
	{
		friction_t* pFriction = m_Collisions.FindFriction((C_BaseEntity*)pEntity);
		if (pFriction && pFriction->patch)
		{
			m_Collisions.ShutdownFriction(*pFriction);
		}
	}

	float PhysGetNextSimTime()
	{
		return m_pPhysenv->GetSimulationTime() + gpGlobals->frametime * cl_phys_timescale.GetFloat();
	}

	float PhysGetSyncCreateTime()
	{
		float nextTime = m_pPhysenv->GetNextFrameTime();
		float simTime = PhysGetNextSimTime();
		if (nextTime < simTime)
		{
			// The next simulation frame begins before the end of this frame
			// so create physics objects at that time so that they will reach the current
			// position at curtime.  Otherwise the physics object will simulate forward from curtime
			// and pop into the future a bit at this point of transition
			return gpGlobals->curtime + nextTime - simTime;
		}
		return gpGlobals->curtime;
	}

	int GetPortalCount() { return m_ActivePortals.Count(); }
	C_EnginePortalInternal* GetPortal(int index) { return m_ActivePortals[index]; }
	CCallQueue* GetPostTouchQueue();

	IRopeManager* RopeManager() {
		return ::RopeManager();
	}

	void Rope_ResetCounters() {
		::Rope_ResetCounters();
	}
private:
	void AddPVSNotifier(IClientUnknown* pUnknown);
	void RemovePVSNotifier(IClientUnknown* pUnknown);
	void AddRestoredEntity(T* pEntity);
	bool PhysIsInCallback()
	{
		if ((m_pPhysenv && m_pPhysenv->IsInSimulation()) || m_Collisions.IsInCallback())
			return true;

		return false;
	}
	
private:

	CEntityFactoryDictionary m_EntityFactoryDictionary;

	// Cached info for networked entities.
//struct EntityCacheInfo_t
//{
//	// Cached off because GetClientNetworkable is called a *lot*
//	IClientNetworkable *m_pNetworkable;
//	unsigned short m_BaseEntitiesIndex;	// Index into m_BaseEntities (or m_BaseEntities.InvalidIndex() if none).
//};
	CUtlVector<IClientEntityListener*>	m_entityListeners;

	// Current count
	int					m_iNumServerEnts;
	// Max allowed
	int					m_iMaxServerEnts;

	int					m_iNumClientNonNetworkable;

	// Current last used slot
	int					m_iMaxUsedServerIndex;

	// This holds fast lookups for special edicts.
	//EntityCacheInfo_t	m_EntityCacheInfo[NUM_ENT_ENTRIES];

	// For fast iteration.
	//CUtlLinkedList<C_BaseEntity*, unsigned short> m_BaseEntities;
	C_EngineObjectInternal* m_EngineObjectArray[NUM_ENT_ENTRIES];
	// These entities want to know when they enter and leave the PVS (server entities
	// already can get the equivalent notification with NotifyShouldTransmit, but client
	// entities have to get it this way).
	CUtlLinkedList<CPVSNotifyInfo,unsigned short> m_PVSNotifyInfos;
	CUtlMap<IClientUnknown*,unsigned short,unsigned short> m_PVSNotifierMap;	// Maps IClientUnknowns to indices into m_PVSNotifyInfos.
	CUtlVector<CBaseHandle> m_RestoredEntities;

	// Causes an assert to happen if bones or attachments are used while this is false.
	struct BoneAccess
	{
		BoneAccess()
		{
			bAllowBoneAccessForNormalModels = false;
			bAllowBoneAccessForViewModels = false;
			tag = NULL;
		}

		bool bAllowBoneAccessForNormalModels;
		bool bAllowBoneAccessForViewModels;
		char const* tag;
	};

	CUtlVector< BoneAccess > m_BoneAccessStack;
	BoneAccess m_BoneAcessBase;

	CUtlVector< CBaseHandle > m_Recording;
	bool m_bInThreadedBoneSetup;
	bool m_bDoThreadedBoneSetup;
	//-----------------------------------------------------------------------------
// Incremented each frame in InvalidateModelBones. Models compare this value to what it
// was last time they setup their bones to determine if they need to re-setup their bones.
	unsigned long	m_iModelBoneCounter = 0;
	CUtlVector<IEngineObjectClient*> m_PreviousBoneSetups;
	unsigned long	m_iPreviousBoneCounter = (unsigned)-1;

	bool m_bAbsQueriesValid = true;
	bool m_bAbsRecomputationEnabled = true;
	bool m_bAbsRecomputationStack[8];
	unsigned short m_iAbsRecomputationStackPos = 0;
	bool m_bDisableTouchFuncs = false;	// Disables PhysicsTouch and PhysicsStartTouch function calls
	CUtlVector< C_EngineObjectInternal* >	m_AimEntsList;
	CUtlVector< clientanimating_t >	m_ClientSideAnimationList;

	// This is a random seed used by the networking code to allow client - side prediction code
//  randon number generators to spit out the same random numbers on both sides for a particular
//  usercmd input.
	int								m_nPredictionRandomSeed = -1;
	IEngineObject*					m_pPredictionPlayer = NULL;

	bool							m_bInterpolate = true;
	bool							m_bWasSkipping = (bool)-1;
	bool							m_bWasThreaded = (bool)-1;
	// All the entities that want Interpolate() called on them.
	CUtlLinkedList<C_EngineObjectInternal*, unsigned short> m_InterpolationList;
	CUtlLinkedList<C_EngineObjectInternal*, unsigned short> m_TeleportList;

	CUtlLinkedList< EHANDLE > m_LRU;
	CUtlLinkedList< EHANDLE > m_LRUImportantRagdolls;

	int m_iMaxRagdolls;
	int m_iSimulatedRagdollCount;
	int m_iRagdollCount;

	C_BaseEntity* m_pLocalPlayer = NULL;

	IPhysics* m_physics;
	IPhysicsEnvironment* m_pPhysenv = NULL;
	IPhysicsSurfaceProps* m_pPhysprops = NULL;
	IPhysicsCollision* m_pPhyscollision = NULL;
	IPhysicsObjectPairHash* m_EntityCollisionHash = NULL;
	IPhysicsObject* m_PhysWorldObject = NULL;
	physicssound::soundlist_t m_impactSounds;
	CCollisionEvent m_Collisions;

	CUtlVector<C_EnginePortalInternal*> m_ActivePortals;
	int m_nTouchDepth = 0;
	CCallQueue m_PostTouchQueue;
};

template<class T>
void CClientEntityList<T>::InstallEntityFactory(IEntityFactory* pFactory)
{
	m_EntityFactoryDictionary.InstallFactory(pFactory);
}

template<class T>
void CClientEntityList<T>::UninstallEntityFactory(IEntityFactory* pFactory)
{
	m_EntityFactoryDictionary.UninstallFactory(pFactory);
}

template<class T>
bool CClientEntityList<T>::CanCreateEntityClass(const char* pClassName)
{
	return m_EntityFactoryDictionary.FindFactory(pClassName) != NULL;
}

template<class T>
const char* CClientEntityList<T>::GetMapClassName(const char* pClassName)
{
	return m_EntityFactoryDictionary.GetMapClassName(pClassName);
}

template<class T>
const char* CClientEntityList<T>::GetDllClassName(const char* pClassName)
{
	return m_EntityFactoryDictionary.GetDllClassName(pClassName);
}

template<class T>
size_t		CClientEntityList<T>::GetEntitySize(const char* pClassName)
{
	return m_EntityFactoryDictionary.GetEntitySize(pClassName);
}

template<class T>
const char* CClientEntityList<T>::GetCannonicalName(const char* pClassName)
{
	return m_EntityFactoryDictionary.GetCannonicalName(pClassName);
}

template<class T>
void CClientEntityList<T>::ReportEntitySizes()
{
	m_EntityFactoryDictionary.ReportEntitySizes();
}

template<class T>
void CClientEntityList<T>::DumpEntityFactories()
{
	m_EntityFactoryDictionary.DumpEntityFactories();
}

template<class T>
const char* CClientEntityList<T>::GetBlockName()
{
	return "Entities";
}

template<class T>
void CClientEntityList<T>::PreSave(CSaveRestoreData* pSaveData)
{
	//m_EntitySaveUtils.PreSave();

	// Allow the entities to do some work
	T* pEnt = NULL;

	// Do this because it'll force entities to figure out their origins, and that requires
	// SetupBones in the case of aiments.
	{
		PushAllowBoneAccess(true, true, (char const*)1);

		int last = GetHighestEntityIndex();
		CBaseHandle iter = BaseClass::FirstHandle();

		for (int e = 0; e <= last; e++)
		{
			pEnt = GetBaseEntity(e);

			if (!pEnt)
				continue;

			m_EngineObjectArray[pEnt->entindex()]->OnSave();
		}

		while (iter != BaseClass::InvalidHandle())
		{
			pEnt = GetBaseEntityFromHandle(iter);

			if (pEnt && pEnt->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE)
			{
				m_EngineObjectArray[pEnt->entindex()]->OnSave();
			}

			iter = BaseClass::NextHandle(iter);
		}

		PopBoneAccess((char const*)1);
	}
	SaveInitEntities(pSaveData);
}

template<class T>
void CClientEntityList<T>::SaveEntityOnTable(T* pEntity, CSaveRestoreData* pSaveData, int& iSlot)
{
	entitytable_t* pEntInfo = pSaveData->GetEntityInfo(iSlot);
	pEntInfo->id = iSlot;
	pEntInfo->edictindex = -1;
	pEntInfo->modelname = pEntity->GetEngineObject()->GetModelName();
	pEntInfo->restoreentityindex = -1;
	pEntInfo->saveentityindex = pEntity ? pEntity->entindex() : -1;
	pEntInfo->hEnt = pEntity->GetRefEHandle();
	pEntInfo->flags = 0;
	pEntInfo->location = 0;
	pEntInfo->size = 0;
	pEntInfo->classname = NULL_STRING;

	iSlot++;
}

template<class T>
bool CClientEntityList<T>::SaveInitEntities(CSaveRestoreData* pSaveData)
{
	int number_of_entities;

	number_of_entities = NumberOfEntities(true);

	entitytable_t* pEntityTable = (entitytable_t*)engine->SaveAllocMemory((sizeof(entitytable_t) * number_of_entities), sizeof(char));
	if (!pEntityTable)
		return false;

	pSaveData->InitEntityTable(pEntityTable, number_of_entities);

	// build the table of entities
	// this is used to turn pointers into savable indices
	// build up ID numbers for each entity, for use in pointer conversions
	// if an entity requires a certain edict number upon restore, save that as well
	T* pEnt = NULL;
	int i = 0;

	int last = GetHighestEntityIndex();

	for (int e = 0; e <= last; e++)
	{
		pEnt = GetBaseEntity(e);
		if (!pEnt)
			continue;
		SaveEntityOnTable(pEnt, pSaveData, i);
	}

	CBaseHandle iter = BaseClass::FirstHandle();

	while (iter != BaseClass::InvalidHandle())
	{
		pEnt = GetBaseEntityFromHandle(iter);

		if (pEnt && pEnt->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE)
		{
			SaveEntityOnTable(pEnt, pSaveData, i);
		}

		iter = BaseClass::NextHandle(iter);
	}

	//pSaveData->BuildEntityHash();

	Assert(i == pSaveData->NumEntities());
	return (i == pSaveData->NumEntities());
}

template<class T>
void CClientEntityList<T>::Save(ISave* pSave)
{
	CGameSaveRestoreInfo* pSaveData = pSave->GetGameSaveRestoreInfo();

	// write entity list that was previously built by SaveInitEntities()
	for (int i = 0; i < pSaveData->NumEntities(); i++)
	{
		entitytable_t* pEntInfo = pSaveData->GetEntityInfo(i);
		pEntInfo->location = pSave->GetWritePos();
		pEntInfo->size = 0;

		T* pEnt = (T*)GetClientEntityFromHandle(pEntInfo->hEnt);
		if (pEnt && !(pEnt->ObjectCaps() & FCAP_DONT_SAVE))
		{
			MDLCACHE_CRITICAL_SECTION();

			pSaveData->SetCurrentEntityContext(pEnt);
			pEnt->Save(*pSave);
			pSaveData->SetCurrentEntityContext(NULL);

			pEntInfo->size = pSave->GetWritePos() - pEntInfo->location;	// Size of entity block is data size written to block

			pEntInfo->classname = pEnt->GetEngineObject()->GetClassname();	// Remember entity class for respawn

		}
	}
}

template<class T>
void CClientEntityList<T>::WriteSaveHeaders(ISave* pSave)
{
	CGameSaveRestoreInfo* pSaveData = pSave->GetGameSaveRestoreInfo();

	int nEntities = pSaveData->NumEntities();
	pSave->WriteInt(&nEntities);

	for (int i = 0; i < pSaveData->NumEntities(); i++)
		pSave->WriteEntityInfo(pSaveData->GetEntityInfo(i));
}

template<class T>
void CClientEntityList<T>::PostSave()
{
	//m_EntitySaveUtils.PostSave();
}

template<class T>
bool CClientEntityList<T>::DoRestoreEntity(T* pEntity, IRestore* pRestore)
{
	MDLCACHE_CRITICAL_SECTION();

	EHANDLE hEntity;

	hEntity = pEntity;

	pRestore->GetGameSaveRestoreInfo()->SetCurrentEntityContext(pEntity);
	pEntity->Restore(*pRestore);
	pRestore->GetGameSaveRestoreInfo()->SetCurrentEntityContext(NULL);

	// Above calls may have resulted in self destruction
	return (hEntity != NULL);
}

template<class T>
int CClientEntityList<T>::RestoreEntity(T* pEntity, IRestore* pRestore, entitytable_t* pEntInfo)
{
	if (!DoRestoreEntity(pEntity, pRestore))
		return 0;

	return 0;
}

template<class T>
void CClientEntityList<T>::PreRestore()
{

}

template<class T>
void CClientEntityList<T>::ReadRestoreHeaders(IRestore* pRestore)
{
	CGameSaveRestoreInfo* pSaveData = pRestore->GetGameSaveRestoreInfo();

	int nEntities;
	pRestore->ReadInt(&nEntities);

	entitytable_t* pEntityTable = (entitytable_t*)engine->SaveAllocMemory((sizeof(entitytable_t) * nEntities), sizeof(char));
	if (!pEntityTable)
	{
		return;
	}

	pSaveData->InitEntityTable(pEntityTable, nEntities);

	for (int i = 0; i < pSaveData->NumEntities(); i++) {
		if (i == 165) {
			int aaa = 0;
		}
		entitytable_t* pEntityTable = pSaveData->GetEntityInfo(i);
		pRestore->ReadEntityInfo(pEntityTable);
		pEntityTable = pSaveData->GetEntityInfo(i);
	}
}

template<class T>
void CClientEntityList<T>::AddRestoredEntity(T* pEntity)
{
	if (!pEntity)
		return;

	m_RestoredEntities.AddToTail(pEntity->GetRefEHandle());
}

template<class T>
void CClientEntityList<T>::Restore(IRestore* pRestore, bool createPlayers)
{
	entitytable_t* pEntInfo;
	C_BaseEntity* pent;

	CGameSaveRestoreInfo* pSaveData = pRestore->GetGameSaveRestoreInfo();

	// Create entity list
	int i;
	bool restoredWorld = false;

	for (i = 0; i < pSaveData->NumEntities(); i++)
	{
		pEntInfo = pSaveData->GetEntityInfo(i);
		pent = GetBaseEntity(pEntInfo->restoreentityindex);
		pEntInfo->hEnt = pent;
	}

	// Blast saved data into entities
	for (i = 0; i < pSaveData->NumEntities(); i++)
	{
		pEntInfo = pSaveData->GetEntityInfo(i);

		bool bRestoredCorrectly = false;
		// FIXME, need to translate save spot to real index here using lookup table transmitted from server
		//Assert( !"Need translation still" );
		if (pEntInfo->restoreentityindex >= 0)
		{
			if (pEntInfo->restoreentityindex == 0)
			{
				Assert(!restoredWorld);
				restoredWorld = true;
			}

			pent = GetBaseEntity(pEntInfo->restoreentityindex);
			pRestore->SetReadPos(pEntInfo->location);
			if (pent)
			{
				if (RestoreEntity(pent, pRestore, pEntInfo) >= 0)
				{
					// Call the OnRestore method
					AddRestoredEntity(pent);
					bRestoredCorrectly = true;
				}
			}
		}
		// BUGBUG: JAY: Disable ragdolls across transitions until PVS/solid check & client entity patch file are implemented
		else if (!pSaveData->levelInfo.fUseLandmark)
		{
			if (pEntInfo->classname != NULL_STRING)
			{
				pent = CreateEntityByName(STRING(pEntInfo->classname));
				pent->InitializeAsClientEntity(NULL, RENDER_GROUP_OPAQUE_ENTITY);

				pRestore->SetReadPos(pEntInfo->location);

				if (pent)
				{
					if (RestoreEntity(pent, pRestore, pEntInfo) >= 0)
					{
						pEntInfo->hEnt = pent;
						AddRestoredEntity(pent);
						bRestoredCorrectly = true;
					}
				}
			}
		}

		if (!bRestoredCorrectly)
		{
			pEntInfo->hEnt = NULL;
			pEntInfo->restoreentityindex = -1;
		}
	}

}

template<class T>
void CClientEntityList<T>::PostRestore()
{
	for (int i = 0; i < m_RestoredEntities.Count(); i++)
	{
		T* pEntity = (T*)GetClientEntityFromHandle(m_RestoredEntities[i]);
		if (pEntity)
		{
			MDLCACHE_CRITICAL_SECTION();
			m_EngineObjectArray[pEntity->entindex()]->OnRestore();
		}
	}
	m_RestoredEntities.RemoveAll();
}

template<class T>
inline C_BaseEntity* CClientEntityList<T>::CreateEntityByName(const char* className, int iForceEdictIndex, int iSerialNum) {
	if (iForceEdictIndex == -1) {
		iForceEdictIndex = BaseClass::AllocateFreeSlot(false, iForceEdictIndex);
		iSerialNum = BaseClass::GetNetworkSerialNumber(iForceEdictIndex);
	}
	else {
		iForceEdictIndex = BaseClass::AllocateFreeSlot(true, iForceEdictIndex);
		if (iSerialNum == -1) {
			Error("iSerialNum == -1");
		}
	}
	if (m_EngineObjectArray[iForceEdictIndex]) {
		Error("slot not free!");
	}
	IEntityFactory* pFactory = m_EntityFactoryDictionary.FindFactory(className);
	if (!pFactory)
	{
		Warning("Attempted to create unknown entity type %s!\n", className);
		return NULL;
	}
	switch (pFactory->GetEngineObjectType()) {
	case ENGINEOBJECT_BASE:
		m_EngineObjectArray[iForceEdictIndex] = new C_EngineObjectInternal(this, iForceEdictIndex, iSerialNum);
		break;
	case ENGINEOBJECT_WORLD:
		m_EngineObjectArray[iForceEdictIndex] = new C_EngineWorldInternal(this, iForceEdictIndex, iSerialNum);
		break;
	case ENGINEOBJECT_PLAYER:
		m_EngineObjectArray[iForceEdictIndex] = new C_EnginePlayerInternal(this, iForceEdictIndex, iSerialNum);
		break;
	case ENGINEOBJECT_PORTAL:
		m_EngineObjectArray[iForceEdictIndex] = new C_EnginePortalInternal(this, iForceEdictIndex, iSerialNum);
		break;
	case ENGINEOBJECT_SHADOWCLONE:
		m_EngineObjectArray[iForceEdictIndex] = new C_EngineShadowCloneInternal(this, iForceEdictIndex, iSerialNum);
		break;
	case ENGINEOBJECT_VEHICLE:
		m_EngineObjectArray[iForceEdictIndex] = new C_EngineVehicleInternal(this, iForceEdictIndex, iSerialNum);
		break;
	case ENGINEOBJECT_ROPE:
		m_EngineObjectArray[iForceEdictIndex] = new C_EngineRopeInternal(this, iForceEdictIndex, iSerialNum);
		break;
	case ENGINEOBJECT_GHOST:
		m_EngineObjectArray[iForceEdictIndex] = new C_EngineGhostInternal(this, iForceEdictIndex, iSerialNum);
		break;
	default:
		Error("GetEngineObjectType error!\n");
	}
	return (C_BaseEntity*)m_EntityFactoryDictionary.Create(this, className, iForceEdictIndex, iSerialNum, this);
}

template<class T>
inline void	CClientEntityList<T>::DestroyEntity(IHandleEntity* pEntity) {
	m_EntityFactoryDictionary.Destroy(pEntity);
}

//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
template<class T>
inline bool	CClientEntityList<T>::IsHandleValid( CBaseHandle handle ) const
{
	return BaseClass::LookupEntity(handle) != NULL;
}

template<class T>
inline T* CClientEntityList<T>::GetListedEntity( int entnum )
{
	return BaseClass::LookupEntityByNetworkIndex( entnum );
}

template<class T>
inline T* CClientEntityList<T>::GetClientUnknownFromHandle( CBaseHandle hEnt )
{
	return BaseClass::LookupEntity( hEnt );
}

template<class T>
inline CUtlLinkedList<CPVSNotifyInfo,unsigned short>& CClientEntityList<T>::GetPVSNotifiers()//CClientEntityList<T>::
{
	return m_PVSNotifyInfos;
}

bool PVSNotifierMap_LessFunc(IClientUnknown* const& a, IClientUnknown* const& b);

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
template<class T>
CClientEntityList<T>::CClientEntityList(void) :
	m_PVSNotifierMap(0, 0, PVSNotifierMap_LessFunc)
{
	m_iMaxUsedServerIndex = -1;
	m_iMaxServerEnts = 0;
	for (int i = 0; i < NUM_ENT_ENTRIES; i++)
	{
		m_EngineObjectArray[i] = NULL;
	}
	m_iMaxRagdolls = -1;
	m_LRUImportantRagdolls.RemoveAll();
	m_LRU.RemoveAll();
	Release();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
template<class T>
CClientEntityList<T>::~CClientEntityList(void)
{
	Release();
}

template<class T>
bool CClientEntityList<T>::Init()
{
	factorylist_t factories;

	// Get the list of interface factories to extract the physics DLL's factory
	FactoryList_Retrieve(factories);

	if (!factories.physicsFactory)
		return false;

	if ((m_physics = (IPhysics*)factories.physicsFactory(VPHYSICS_INTERFACE_VERSION, NULL)) == NULL ||
		(m_pPhysprops = (IPhysicsSurfaceProps*)factories.physicsFactory(VPHYSICS_SURFACEPROPS_INTERFACE_VERSION, NULL)) == NULL ||
		(m_pPhyscollision = (IPhysicsCollision*)factories.physicsFactory(VPHYSICS_COLLISION_INTERFACE_VERSION, NULL)) == NULL)
	{
		return false;
	}

	if (IsX360())
	{
		// Reduce timescale to save perf on 360
		cl_phys_timescale.SetValue(0.9f);
	}
	PhysParseSurfaceData(m_pPhysprops, filesystem);

	AddDataAccessor(TOUCHLINK, new CEntityDataInstantiator<C_BaseEntity, clienttouchlink_t >);
	AddDataAccessor(GROUNDLINK, new CEntityDataInstantiator<C_BaseEntity, clientgroundlink_t >);
	AddDataAccessor(STEPSIMULATION, new CEntityDataInstantiator<C_BaseEntity, StepSimulationData >);
	AddDataAccessor(MODELSCALE, new CEntityDataInstantiator<C_BaseEntity, ModelScale >);
	AddDataAccessor(POSITIONWATCHER, new CEntityDataInstantiator<C_BaseEntity, C_WatcherList >);
	//AddDataAccessor(PHYSICSPUSHLIST, new CEntityDataInstantiator<C_BaseEntity, physicspushlist_t >);
	//AddDataAccessor(VPHYSICSUPDATEAI, new CEntityDataInstantiator<C_BaseEntity, vphysicsupdateai_t >);
	AddDataAccessor(VPHYSICSWATCHER, new CEntityDataInstantiator<C_BaseEntity, C_WatcherList >);
	return true;
}

template<class T>
void CClientEntityList<T>::Shutdown()
{
	RemoveDataAccessor(TOUCHLINK);
	RemoveDataAccessor(GROUNDLINK);
	RemoveDataAccessor(STEPSIMULATION);
	RemoveDataAccessor(MODELSCALE);
	RemoveDataAccessor(POSITIONWATCHER);
	RemoveDataAccessor(PHYSICSPUSHLIST);
	RemoveDataAccessor(VPHYSICSUPDATEAI);
	RemoveDataAccessor(VPHYSICSWATCHER);
}

// Level init, shutdown
template<class T>
void CClientEntityList<T>::LevelInitPreEntity()
{
	m_impactSounds.RemoveAll();
	PrecachePhysicsSounds();
}

#define DEFAULT_XBOX_CLIENT_VPHYSICS_TICK	0.025		// 25ms ticks on xbox ragdolls
template<class T>
void CClientEntityList<T>::LevelInitPostEntity()
{
	m_pPhysenv = m_physics->CreateEnvironment();
	assert(m_pPhysenv);
//#ifdef PORTAL
//	physenv_main = physenv;
//#endif
	{
		MEM_ALLOC_CREDIT();
		m_EntityCollisionHash = m_physics->CreateObjectPairHash();
	}

	// TODO: need to get the right factory function here
	//physenv->SetDebugOverlay( appSystemFactory );
	m_pPhysenv->SetGravity(Vector(0, 0, -GetCurrentGravity()));
	// 15 ms per tick
	// NOTE: Always run client physics at this rate - helps keep ragdolls stable
	m_pPhysenv->SetSimulationTimestep(IsXbox() ? DEFAULT_XBOX_CLIENT_VPHYSICS_TICK : DEFAULT_TICK_INTERVAL);
	m_pPhysenv->SetCollisionEventHandler(&m_Collisions);
	m_pPhysenv->SetCollisionSolver(&m_Collisions);

	m_PhysWorldObject = PhysCreateWorld_Shared(GetBaseEntity(0), modelinfo->GetVCollide(1), g_PhysDefaultObjectParams);

	staticpropmgr->CreateVPhysicsRepresentations(m_pPhysenv, g_pSolidSetup, NULL);
}

// The level is shutdown in two parts
template<class T>
void CClientEntityList<T>::LevelShutdownPreEntity()
{
	if (m_pPhysenv)
	{
		// we may have deleted multiple objects including the world by now, so 
		// don't try to wake them up
		m_pPhysenv->SetQuickDelete(true);
	}
}

template<class T>
void CClientEntityList<T>::LevelShutdownPostEntity()
{
	if (m_pPhysenv)
	{
		// environment destroys all objects
		// entities are gone, so this is safe now
		m_physics->DestroyEnvironment(m_pPhysenv);
	}
	m_physics->DestroyObjectPairHash(m_EntityCollisionHash);
	m_EntityCollisionHash = NULL;

	m_physics->DestroyAllCollisionSets();

	m_pPhysenv = NULL;
	m_PhysWorldObject = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Clears all entity lists and releases entities
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::Release(void)
{
	// Free all the entities.
	CBaseHandle iter = BaseClass::FirstHandle();
	while (iter != BaseClass::InvalidHandle())
	{
		// Try to call release on anything we can.
		IClientEntity* pNet = GetClientEntityFromHandle(iter);
		if (pNet)
		{
			DestroyEntity(pNet);// ->Release();
		}
		else
		{
			// Try to call release on anything we can.
			IClientThinkable* pThinkable = GetClientThinkableFromHandle(iter);
			if (pThinkable)
			{
				pThinkable->Release();
			}
		}
		//BaseClass::RemoveEntity(iter);

		iter = BaseClass::FirstHandle();
	}

	m_iNumServerEnts = 0;
	m_iMaxServerEnts = 0;
	m_iNumClientNonNetworkable = 0;
	m_iMaxUsedServerIndex = -1;
	m_iPreviousBoneCounter = (unsigned)-1;
	if (m_PreviousBoneSetups.Count() != 0)
	{
		Msg("%d entities in bone setup array. Should have been cleaned up by now\n", m_PreviousBoneSetups.Count());
		m_PreviousBoneSetups.RemoveAll();
	}
}

template<class T>
inline IEngineObjectClient* CClientEntityList<T>::GetEngineObject(int entnum) {
	if (entnum < 0 || entnum >= NUM_ENT_ENTRIES) {
		return NULL;
	}
	return m_EngineObjectArray[entnum];
}

template<class T>
IEngineObjectClient* CClientEntityList<T>::GetEngineObjectFromHandle(CBaseHandle handle) {
	if (handle.GetEntryIndex() < 0 || handle.GetEntryIndex() >= NUM_ENT_ENTRIES) {
		return NULL;
	}
	const CEntInfo<T>* pInfo = &BaseClass::m_EntPtrArray[handle.GetEntryIndex()];
	if (pInfo->m_SerialNumber == handle.GetSerialNumber())
		return m_EngineObjectArray[handle.GetEntryIndex()];
	else
		return NULL;
}

template<class T>
IClientNetworkable* CClientEntityList<T>::GetClientNetworkable(int entnum)
{
	Assert(entnum >= 0);
	Assert(entnum < MAX_EDICTS);
	T* pEnt = GetListedEntity(entnum);
	return pEnt ? pEnt->GetClientNetworkable() : 0;
}

template<class T>
IClientEntity* CClientEntityList<T>::GetClientEntity(int entnum)
{
	T* pEnt = GetListedEntity(entnum);
	return pEnt ? pEnt->GetIClientEntity() : 0;
}

template<class T>
int CClientEntityList<T>::NumberOfEntities(bool bIncludeNonNetworkable)
{
	if (bIncludeNonNetworkable == true)
		return m_iNumServerEnts + m_iNumClientNonNetworkable;

	return m_iNumServerEnts;
}

template<class T>
void CClientEntityList<T>::SetMaxEntities(int maxents)
{
	m_iMaxServerEnts = maxents;
}

template<class T>
int CClientEntityList<T>::GetMaxEntities(void)
{
	return m_iMaxServerEnts;
}

//-----------------------------------------------------------------------------
// Purpose: Because m_iNumServerEnts != last index
// Output : int
//-----------------------------------------------------------------------------
template<class T>
int CClientEntityList<T>::GetHighestEntityIndex(void)
{
	return m_iMaxUsedServerIndex;
}

template<class T>
void CClientEntityList<T>::RecomputeHighestEntityUsed(void)
{
	m_iMaxUsedServerIndex = -1;

	// Walk backward looking for first valid index
	int i;
	for (i = MAX_EDICTS - 1; i >= 0; i--)
	{
		if (GetListedEntity(i) != NULL)
		{
			m_iMaxUsedServerIndex = i;
			break;
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Add a raw C_BaseEntity to the entity list.
// Input  : index - 
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : index - 
//-----------------------------------------------------------------------------
template<class T>
C_BaseEntity* CClientEntityList<T>::GetBaseEntity(int entnum)
{
	T* pEnt = GetListedEntity(entnum);
	return pEnt ? pEnt->GetBaseEntity() : 0;
}

template<class T>
ICollideable* CClientEntityList<T>::GetCollideable(int entnum)
{
	T* pEnt = GetListedEntity(entnum);
	return pEnt ? pEnt->GetCollideable() : 0;
}

template<class T>
IClientNetworkable* CClientEntityList<T>::GetClientNetworkableFromHandle(CBaseHandle hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetClientNetworkable() : 0;
}

template<class T>
IClientEntity* CClientEntityList<T>::GetClientEntityFromHandle(CBaseHandle hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetIClientEntity() : 0;
}

template<class T>
IClientRenderable* CClientEntityList<T>::GetClientRenderableFromHandle(CBaseHandle hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetClientRenderable() : 0;
}

template<class T>
C_BaseEntity* CClientEntityList<T>::GetBaseEntityFromHandle(CBaseHandle hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetBaseEntity() : 0;
}

template<class T>
ICollideable* CClientEntityList<T>::GetCollideableFromHandle(CBaseHandle hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetCollideable() : 0;
}

template<class T>
IClientThinkable* CClientEntityList<T>::GetClientThinkableFromHandle(CBaseHandle hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetClientThinkable() : 0;
}

template<class T>
void CClientEntityList<T>::AddPVSNotifier(IClientUnknown* pUnknown)
{
	IClientRenderable* pRen = pUnknown->GetClientRenderable();
	if (pRen)
	{
		IPVSNotify* pNotify = pRen->GetPVSNotifyInterface();
		if (pNotify)
		{
			unsigned short index = m_PVSNotifyInfos.AddToTail();
			CPVSNotifyInfo* pInfo = &m_PVSNotifyInfos[index];
			pInfo->m_pNotify = pNotify;
			pInfo->m_pRenderable = pRen;
			pInfo->m_InPVSStatus = 0;
			pInfo->m_PVSNotifiersLink = index;

			m_PVSNotifierMap.Insert(pUnknown, index);
		}
	}
}

template<class T>
void CClientEntityList<T>::RemovePVSNotifier(IClientUnknown* pUnknown)
{
	IClientRenderable* pRenderable = pUnknown->GetClientRenderable();
	if (pRenderable)
	{
		IPVSNotify* pNotify = pRenderable->GetPVSNotifyInterface();
		if (pNotify)
		{
			unsigned short index = m_PVSNotifierMap.Find(pUnknown);
			if (!m_PVSNotifierMap.IsValidIndex(index))
			{
				Warning("PVS notifier not in m_PVSNotifierMap\n");
				Assert(false);
				return;
			}

			unsigned short indexIntoPVSNotifyInfos = m_PVSNotifierMap[index];

			Assert(m_PVSNotifyInfos[indexIntoPVSNotifyInfos].m_pNotify == pNotify);
			Assert(m_PVSNotifyInfos[indexIntoPVSNotifyInfos].m_pRenderable == pRenderable);

			m_PVSNotifyInfos.Remove(indexIntoPVSNotifyInfos);
			m_PVSNotifierMap.RemoveAt(index);
			return;
		}
	}

	// If it didn't report itself as a notifier, let's hope it's not in the notifier list now
	// (which would mean that it reported itself as a notifier earlier, but not now).
#ifdef _DEBUG
	unsigned short index = m_PVSNotifierMap.Find(pUnknown);
	Assert(!m_PVSNotifierMap.IsValidIndex(index));
#endif
}

template<class T>
void CClientEntityList<T>::AddListenerEntity(IClientEntityListener* pListener)
{
	if (m_entityListeners.Find(pListener) >= 0)
	{
		AssertMsg(0, "Can't add listeners multiple times\n");
		return;
	}
	m_entityListeners.AddToTail(pListener);
}

template<class T>
void CClientEntityList<T>::RemoveListenerEntity(IClientEntityListener* pListener)
{
	m_entityListeners.FindAndRemove(pListener);
}

template<class T>
void CClientEntityList<T>::AfterCreated(IHandleEntity* pEntity) {
	BaseClass::AddEntity((T*)pEntity);
}


template<class T>
void CClientEntityList<T>::BeforeDestroy(IHandleEntity* pEntity) {
	BaseClass::RemoveEntity((T*)pEntity);
}

template<class T>
void CClientEntityList<T>::OnAddEntity(T* pEnt, CBaseHandle handle)
{
	int entnum = handle.GetEntryIndex();
	//EntityCacheInfo_t* pCache = &m_EntityCacheInfo[entnum];

	if (entnum < 0 || entnum >= NUM_ENT_ENTRIES) {
		Error("entnum overflow!");
		return;
	}

	if (entnum >= 0 && entnum < MAX_EDICTS)
	{
		// Update our counters.
		m_iNumServerEnts++;
		if (entnum > m_iMaxUsedServerIndex)
		{
			m_iMaxUsedServerIndex = entnum;
		}


		// Cache its networkable pointer.
		Assert(dynamic_cast<IClientUnknown*>(pEnt));
		Assert(((IClientUnknown*)pEnt)->GetClientNetworkable()); // Server entities should all be networkable.
		//pCache->m_pNetworkable = (pEnt)->GetClientNetworkable();//(IClientUnknown*)
	}

	IClientUnknown* pUnknown = pEnt;//(IClientUnknown*)

	// Store it in a special list for fast iteration if it's a C_BaseEntity.
	C_BaseEntity* pBaseEntity = pUnknown->GetBaseEntity();
	//m_EngineObjectArray[entnum] = new C_EngineObjectInternal();
	m_EngineObjectArray[entnum]->Init(pBaseEntity);

	// If this thing wants PVS notifications, hook it up.
	AddPVSNotifier(pUnknown);

//	if (pBaseEntity)
//	{
		//pCache->m_BaseEntitiesIndex = m_BaseEntities.AddToTail(pBaseEntity);

		if (pBaseEntity->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE)
		{
			m_iNumClientNonNetworkable++;
		}

		//DevMsg(2,"Created %s\n", pBaseEnt->GetClassname() );
		for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
		{
			m_entityListeners[i]->OnEntityCreated(pBaseEntity);
		}
	//}
	//else
	//{
	//	pCache->m_BaseEntitiesIndex = m_BaseEntities.InvalidIndex();
	//}

	BaseClass::OnAddEntity(pEnt, handle);
}

template<class T>
void CClientEntityList<T>::OnRemoveEntity(T* pEnt, CBaseHandle handle)
{
	int entnum = handle.GetEntryIndex();
	//EntityCacheInfo_t* pCache = &m_EntityCacheInfo[entnum];

	if (entnum >= 0 && entnum < MAX_EDICTS)
	{
		// This is a networkable ent. Clear out our cache info for it.
		//pCache->m_pNetworkable = NULL;
		m_iNumServerEnts--;

		if (entnum >= m_iMaxUsedServerIndex)
		{
			RecomputeHighestEntityUsed();
		}
	}


	IClientUnknown* pUnknown = pEnt;//(IClientUnknown*)

	// If this is a PVS notifier, remove it.
	RemovePVSNotifier(pUnknown);

	C_BaseEntity* pBaseEntity = pUnknown->GetBaseEntity();

	if (pBaseEntity)
	{
		if (pBaseEntity->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE)
		{
			m_iNumClientNonNetworkable--;
		}

		//DevMsg(2,"Deleted %s\n", pBaseEnt->GetClassname() );
		for (int i = m_entityListeners.Count() - 1; i >= 0; i--)
		{
			m_entityListeners[i]->OnEntityDeleted(pBaseEntity);
		}
	}

	m_EngineObjectArray[entnum]->PhysicsRemoveTouchedList();
	m_EngineObjectArray[entnum]->PhysicsRemoveGroundList();
	m_EngineObjectArray[entnum]->DestroyAllDataObjects();
	delete m_EngineObjectArray[entnum];
	m_EngineObjectArray[entnum] = NULL;
	//if (pCache->m_BaseEntitiesIndex != m_BaseEntities.InvalidIndex())
	//	m_BaseEntities.Remove(pCache->m_BaseEntitiesIndex);

	//pCache->m_BaseEntitiesIndex = m_BaseEntities.InvalidIndex();
	BaseClass::OnRemoveEntity(pEnt, handle);
}


// Use this to iterate over all the C_BaseEntities.
template<class T>
C_BaseEntity* CClientEntityList<T>::FirstBaseEntity() const
{
	const CEntInfo<T>* pList = BaseClass::FirstEntInfo();
	while (pList)
	{
		if (pList->m_pEntity)
		{
			T* pUnk = (pList->m_pEntity);//static_cast<IClientUnknown*>
			C_BaseEntity* pRet = pUnk->GetBaseEntity();
			if (pRet)
				return pRet;
		}
		pList = pList->m_pNext;
	}

	return NULL;

}

template<class T>
C_BaseEntity* CClientEntityList<T>::NextBaseEntity(C_BaseEntity* pEnt) const
{
	if (pEnt == NULL)
		return FirstBaseEntity();

	// Run through the list until we get a C_BaseEntity.
	const CEntInfo<T>* pList = BaseClass::GetEntInfoPtr(pEnt->GetRefEHandle());
	if (pList)
	{
		pList = BaseClass::NextEntInfo(pList);
	}

	while (pList)
	{
		if (pList->m_pEntity)
		{
			T* pUnk = (pList->m_pEntity);//static_cast<IClientUnknown*>
			C_BaseEntity* pRet = pUnk->GetBaseEntity();
			if (pRet)
				return pRet;
		}
		pList = pList->m_pNext;
	}

	return NULL;
}

template<class T>
void CClientEntityList<T>::AddDataAccessor(int type, IEntityDataInstantiator<T>* instantiator) {
	BaseClass::AddDataAccessor(type, instantiator);
}

template<class T>
void CClientEntityList<T>::RemoveDataAccessor(int type) {
	BaseClass::RemoveDataAccessor(type);
}

template<class T>
void* CClientEntityList<T>::GetDataObject(int type, const T* instance) {
	return BaseClass::GetDataObject(type, instance);
}

template<class T>
void* CClientEntityList<T>::CreateDataObject(int type, T* instance) {
	return BaseClass::CreateDataObject(type, instance);
}

template<class T>
void CClientEntityList<T>::DestroyDataObject(int type, T* instance) {
	BaseClass::DestroyDataObject(type, instance);
}

template<class T>
void CClientEntityList<T>::PushAllowBoneAccess(bool bAllowForNormalModels, bool bAllowForViewModels, char const* tagPush)
{
	BoneAccess save = m_BoneAcessBase;
	m_BoneAccessStack.AddToTail(save);

	Assert(m_BoneAccessStack.Count() < 32); // Most likely we are leaking "PushAllowBoneAccess" calls if PopBoneAccess is never called. Consider using AutoAllowBoneAccess.
	m_BoneAcessBase.bAllowBoneAccessForNormalModels = bAllowForNormalModels;
	m_BoneAcessBase.bAllowBoneAccessForViewModels = bAllowForViewModels;
	m_BoneAcessBase.tag = tagPush;
}

template<class T>
void CClientEntityList<T>::PopBoneAccess(char const* tagPop)
{
	// Validate that pop matches the push
	Assert((m_BoneAcessBase.tag == tagPop) || (m_BoneAcessBase.tag && m_BoneAcessBase.tag != (char const*)1 && tagPop && tagPop != (char const*)1 && !strcmp(m_BoneAcessBase.tag, tagPop)));
	int lastIndex = m_BoneAccessStack.Count() - 1;
	if (lastIndex < 0)
	{
		Assert(!"C_BaseAnimating::PopBoneAccess:  Stack is empty!!!");
		return;
	}
	m_BoneAcessBase = m_BoneAccessStack[lastIndex];
	m_BoneAccessStack.Remove(lastIndex);
}

//-----------------------------------------------------------------------------
// Purpose: Add entity to list
// Input  : add - 
// Output : int
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::AddToRecordList(CBaseHandle add)
{
	// This is a hack to remap slot to index
	if (m_Recording.Find(add) != m_Recording.InvalidIndex())
	{
		return;
	}

	// Add to general list
	m_Recording.AddToTail(add);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : remove - 
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::RemoveFromRecordList(CBaseHandle remove)
{
	m_Recording.FindAndRemove(remove);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : slot - 
// Output : IClientRenderable
//-----------------------------------------------------------------------------
template<class T>
IClientRenderable* CClientEntityList<T>::GetRecord(int index)
{
	return GetClientRenderableFromHandle(m_Recording[index]);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
template<class T>
int CClientEntityList<T>::CountRecord()
{
	return m_Recording.Count();
}

template<class T>
void CClientEntityList<T>::ToolRecordEntities()
{
	VPROF_BUDGET("C_BaseEntity::ToolRecordEnties", VPROF_BUDGETGROUP_TOOLS);

	if (!ToolsEnabled() || !clienttools->IsInRecordingMode())
		return;

	// Let non-dormant client created predictables get added, too
	int c = CountRecord();
	for (int i = 0; i < c; i++)
	{
		IClientRenderable* pRenderable = GetRecord(i);
		if (!pRenderable)
			continue;

		pRenderable->RecordToolMessage();
	}
}

template<class T>
void CClientEntityList<T>::InitBoneSetupThreadPool()
{
}

template<class T>
void CClientEntityList<T>::ShutdownBoneSetupThreadPool()
{
}

extern ConVar cl_threaded_bone_setup;

//-----------------------------------------------------------------------------
// Purpose: Do the default sequence blending rules as done in HL1
//-----------------------------------------------------------------------------

template<class T>
void CClientEntityList<T>::SetupBonesOnBaseAnimating(IEngineObjectClient*& pBaseAnimating)
{
	if (!pBaseAnimating->GetMoveParent())
		pBaseAnimating->GetOuter()->GetEngineObject()->SetupBones(NULL, -1, -1, gpGlobals->curtime);
}

template<class T>
void CClientEntityList<T>::PreThreadedBoneSetup()
{
	mdlcache->BeginLock();
}

template<class T>
void CClientEntityList<T>::PostThreadedBoneSetup()
{
	mdlcache->EndLock();
}

template<class T>
void CClientEntityList<T>::ThreadedBoneSetup()
{
	m_bDoThreadedBoneSetup = cl_threaded_bone_setup.GetBool();
	if (m_bDoThreadedBoneSetup)
	{
		int nCount = m_PreviousBoneSetups.Count();
		if (nCount > 1)
		{
			m_bInThreadedBoneSetup = true;

			ParallelProcess("C_BaseAnimating::ThreadedBoneSetup", m_PreviousBoneSetups.Base(), nCount, this, &CClientEntityList<T>::SetupBonesOnBaseAnimating, &CClientEntityList<T>::PreThreadedBoneSetup, &CClientEntityList<T>::PostThreadedBoneSetup);

			m_bInThreadedBoneSetup = false;
		}
	}
	m_iPreviousBoneCounter++;
	m_PreviousBoneSetups.RemoveAll();
}

template<class T>
void CClientEntityList<T>::InvalidateBoneCaches()
{
	m_iModelBoneCounter++;
}


//-----------------------------------------------------------------------------
// Global methods related to when abs data is correct
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::SetAbsQueriesValid(bool bValid)
{
	// @MULTICORE: Always allow in worker threads, assume higher level code is handling correctly
	if (!ThreadInMainThread())
		return;

	if (!bValid)
	{
		m_bAbsQueriesValid = false;
	}
	else
	{
		m_bAbsQueriesValid = true;
	}
}

template<class T>
bool CClientEntityList<T>::IsAbsQueriesValid(void)
{
	if (!ThreadInMainThread())
		return true;
	return m_bAbsQueriesValid;
}

template<class T>
void CClientEntityList<T>::EnableAbsRecomputations(bool bEnable)
{
	if (!ThreadInMainThread())
		return;
	// This should only be called at the frame level. Use PushEnableAbsRecomputations
	// if you're blocking out a section of code.
	Assert(m_iAbsRecomputationStackPos == 0);

	m_bAbsRecomputationEnabled = bEnable;
}

template<class T>
bool CClientEntityList<T>::IsAbsRecomputationsEnabled()
{
	if (!ThreadInMainThread())
		return true;
	return m_bAbsRecomputationEnabled;
}

template<class T>
void CClientEntityList<T>::PushEnableAbsRecomputations(bool bEnable)
{
	if (!ThreadInMainThread())
		return;
	if (m_iAbsRecomputationStackPos < ARRAYSIZE(m_bAbsRecomputationStack))
	{
		m_bAbsRecomputationStack[m_iAbsRecomputationStackPos] = m_bAbsRecomputationEnabled;
		++m_iAbsRecomputationStackPos;
		m_bAbsRecomputationEnabled = bEnable;
	}
	else
	{
		Assert(false);
	}
}

template<class T>
void CClientEntityList<T>::PopEnableAbsRecomputations()
{
	if (!ThreadInMainThread())
		return;
	if (m_iAbsRecomputationStackPos > 0)
	{
		--m_iAbsRecomputationStackPos;
		m_bAbsRecomputationEnabled = m_bAbsRecomputationStack[m_iAbsRecomputationStackPos];
	}
	else
	{
		Assert(false);
	}
}

//-----------------------------------------------------------------------------
// Moves all aiments
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::MarkAimEntsDirty()
{
	// FIXME: With the dirty bits hooked into cycle + sequence, it's unclear
	// that this is even necessary any more (provided aiments are always accessing
	// joints or attachments of the move parent).
	//
	// NOTE: This is a tricky algorithm. This list does not actually contain
	// all aim-ents in its list. It actually contains all hierarchical children,
	// of which aim-ents are a part. We can tell if something is an aiment if it has
	// the EF_BONEMERGE effect flag set.
	// 
	// We will first iterate over all aiments and clear their DIRTY_ABSTRANSFORM flag, 
	// which is necessary to cause them to recompute their aim-ent origin 
	// the next time CalcAbsPosition is called. Because CalcAbsPosition calls MoveToAimEnt
	// and MoveToAimEnt calls SetAbsOrigin/SetAbsAngles, that is how CalcAbsPosition
	// will cause the aim-ent's (and all its children's) dirty state to be correctly updated.
	//
	// Then we will iterate over the loop a second time and call CalcAbsPosition on them,
	int i;
	int c = m_AimEntsList.Count();
	for (i = 0; i < c; ++i)
	{
		C_EngineObjectInternal* pEnt = m_AimEntsList[i];
		Assert(pEnt && pEnt->GetMoveParent());
		if (pEnt->IsEffectActive(EF_BONEMERGE | EF_PARENT_ANIMATES))
		{
			pEnt->AddEFlags(EFL_DIRTY_ABSTRANSFORM);
		}
	}
}

template<class T>
void CClientEntityList<T>::CalcAimEntPositions()
{
	VPROF("CalcAimEntPositions");
	int i;
	int c = m_AimEntsList.Count();
	for (i = 0; i < c; ++i)
	{
		C_EngineObjectInternal* pEnt = m_AimEntsList[i];
		Assert(pEnt);
		Assert(pEnt->GetMoveParent());
		if (pEnt->IsEffectActive(EF_BONEMERGE))
		{
			pEnt->CalcAbsolutePosition();
		}
	}
}

template<class T>
void CClientEntityList<T>::UpdateClientSideAnimations()
{
	VPROF_BUDGET("UpdateClientSideAnimations", VPROF_BUDGETGROUP_CLIENT_ANIMATION);

	int c = m_ClientSideAnimationList.Count();
	for (int i = 0; i < c; ++i)
	{
		clientanimating_t& anim = m_ClientSideAnimationList.Element(i);
		if (!(anim.flags & FCLIENTANIM_SEQUENCE_CYCLE))
			continue;
		Assert(anim.pAnimating);
		if (anim.pAnimating->IsUsingClientSideAnimation())
		{
			Assert(anim.pAnimating->m_ClientSideAnimationListHandle != INVALID_CLIENTSIDEANIMATION_LIST_HANDLE );
		}
		else
		{
			Assert(anim.pAnimating->m_ClientSideAnimationListHandle == INVALID_CLIENTSIDEANIMATION_LIST_HANDLE );
		}
		anim.pAnimating->GetOuter()->UpdateClientSideAnimation();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : seed - 
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::SetPredictionRandomSeed(const CUserCmd* cmd)
{
	if (!cmd)
	{
		m_nPredictionRandomSeed = -1;
		return;
	}

	m_nPredictionRandomSeed = (cmd->random_seed);
}

template<class T>
int CClientEntityList<T>::GetPredictionRandomSeed(void)
{
	return m_nPredictionRandomSeed;
}

template<class T>
IEngineObject* CClientEntityList<T>::GetPredictionPlayer(void)
{
	return m_pPredictionPlayer;
}

template<class T>
void CClientEntityList<T>::SetPredictionPlayer(IEngineObject* player)
{
	m_pPredictionPlayer = player;
}

//-----------------------------------------------------------------------------
// Should we be interpolating?
//-----------------------------------------------------------------------------
template<class T>
bool	CClientEntityList<T>::IsInterpolationEnabled()
{
	return m_bInterpolate;
}

extern ConVar	sv_alternateticks;
//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
template<class T>
bool CClientEntityList<T>::IsSimulatingOnAlternateTicks()
{
	if (gpGlobals->maxClients != 1)
	{
		return false;
	}

	return sv_alternateticks.GetBool();
}

extern ConVar  cl_interpolate;
extern ConVar  cl_extrapolate;

template<class T>
void CClientEntityList<T>::InterpolateServerEntities()
{
	VPROF_BUDGET("C_BaseEntity::InterpolateServerEntities", VPROF_BUDGETGROUP_INTERPOLATION);

	m_bInterpolate = cl_interpolate.GetBool();

	// Don't interpolate during timedemo playback
	if (engine->IsPlayingTimeDemo() || engine->IsPaused())
	{
		m_bInterpolate = false;
	}

	if (!engine->IsPlayingDemo())
	{
		// Don't interpolate, either, if we are timing out
		INetChannelInfo* nci = engine->GetNetChannelInfo();
		if (nci && nci->GetTimeSinceLastReceived() > 0.5f)
		{
			m_bInterpolate = false;
		}
	}

	if (IsSimulatingOnAlternateTicks() != m_bWasSkipping || IsEngineThreaded() != m_bWasThreaded)
	{
		m_bWasSkipping = IsSimulatingOnAlternateTicks();
		m_bWasThreaded = IsEngineThreaded();

		C_BaseEntityIterator iterator;
		C_BaseEntity* pEnt;
		while ((pEnt = iterator.Next()) != NULL)
		{
			pEnt->GetEngineObject()->Interp_UpdateInterpolationAmounts();
		}
	}

	// Enable extrapolation?
	CInterpolationContext context;
	context.SetLastTimeStamp(engine->GetLastTimeStamp());
	if (cl_extrapolate.GetBool() && !engine->IsPaused())
	{
		context.EnableExtrapolation(true);
	}

	// Smoothly interpolate position for server entities.
	ProcessTeleportList();
	ProcessInterpolatedList();
}

template<class T>
void CClientEntityList<T>::ProcessTeleportList()
{
	int iNext;
	for (int iCur = m_TeleportList.Head(); iCur != m_TeleportList.InvalidIndex(); iCur = iNext)
	{
		iNext = m_TeleportList.Next(iCur);
		C_EngineObjectInternal* pCur = m_TeleportList[iCur];

		bool teleport = pCur->Teleported();
		bool ef_nointerp = pCur->IsNoInterpolationFrame();

		if (teleport || ef_nointerp)
		{
			// Undo the teleport flag..
			pCur->m_hOldMoveParent = pCur->GetNetworkMoveParent();
			pCur->m_iOldParentAttachment = pCur->GetParentAttachment();
			// Zero out all but last update.
			pCur->MoveToLastReceivedPosition(true);
			pCur->ResetLatched();
		}
		else
		{
			// Get it out of the list as soon as we can.
			pCur->RemoveFromTeleportList();
		}
	}
}

template<class T>
void CClientEntityList<T>::CheckInterpolatedVarParanoidMeasurement()
{
	// What we're doing here is to check all the entities that were not in the interpolation
	// list and make sure that there's no entity that should be in the list that isn't.

#ifdef INTERPOLATEDVAR_PARANOID_MEASUREMENT
	int iHighest = GetHighestEntityIndex();
	for (int i = 0; i <= iHighest; i++)
	{
		C_BaseEntity* pEnt = GetBaseEntity(i);
		if (!pEnt || pEnt->m_InterpolationListEntry != 0xFFFF || !pEnt->ShouldInterpolate())
			continue;

		// Player angles always generates this error when the console is up.
		if (pEnt->entindex() == 1 && engine->Con_IsVisible())
			continue;

		// View models tend to screw up this test unnecesarily because they modify origin,
		// angles, and 
		if (dynamic_cast<C_BaseViewModel*>(pEnt))
			continue;

		g_bRestoreInterpolatedVarValues = true;
		g_nInterpolatedVarsChanged = 0;
		pEnt->Interpolate(gpGlobals->curtime);
		g_bRestoreInterpolatedVarValues = false;

		if (g_nInterpolatedVarsChanged > 0)
		{
			static int iWarningCount = 0;
			Warning("(%d): An entity (%d) should have been in g_InterpolationList.\n", iWarningCount++, pEnt->entindex());
			break;
		}
	}
#endif
}

template<class T>
void CClientEntityList<T>::ProcessInterpolatedList()
{
	CheckInterpolatedVarParanoidMeasurement();

	// Interpolate the minimal set of entities that need it.
	int iNext;
	for (int iCur = m_InterpolationList.Head(); iCur != m_InterpolationList.InvalidIndex(); iCur = iNext)
	{
		iNext = m_InterpolationList.Next(iCur);
		C_EngineObjectInternal* pCur = m_InterpolationList[iCur];

		pCur->m_bReadyToDraw = pCur->GetOuter()->Interpolate(gpGlobals->curtime);
	}
}

extern ConVar g_ragdoll_important_maxcount;
//-----------------------------------------------------------------------------
// Move it to the top of the LRU
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::MoveToTopOfLRU(C_BaseEntity* pRagdoll, bool bImportant)
{
	if (bImportant)
	{
		m_LRUImportantRagdolls.AddToTail(pRagdoll);

		if (m_LRUImportantRagdolls.Count() > g_ragdoll_important_maxcount.GetInt())
		{
			int iIndex = m_LRUImportantRagdolls.Head();

			C_BaseEntity* pRagdoll = m_LRUImportantRagdolls[iIndex].Get();

			if (pRagdoll)
			{
				pRagdoll->SUB_Remove();
				m_LRUImportantRagdolls.Remove(iIndex);
			}

		}
		return;
	}
	for (int i = m_LRU.Head(); i < m_LRU.InvalidIndex(); i = m_LRU.Next(i))
	{
		if (m_LRU[i].Get() == pRagdoll)
		{
			m_LRU.Remove(i);
			break;
		}
	}

	m_LRU.AddToTail(pRagdoll);
}

extern ConVar g_ragdoll_maxcount;
extern ConVar g_debug_ragdoll_removal;
//-----------------------------------------------------------------------------
// Cull stale ragdolls. There is an ifdef here: one version for episodic, 
// one for everything else.
//-----------------------------------------------------------------------------
#if HL2_EPISODIC
template<class T>
void CClientEntityList<T>::UpdateRagdolls(float frametime) // EPISODIC VERSION
{
	VPROF("CRagdollLRURetirement::Update");
	// Compress out dead items
	int i, next;

	int iMaxRagdollCount = m_iMaxRagdolls;

	if (iMaxRagdollCount == -1)
	{
		iMaxRagdollCount = g_ragdoll_maxcount.GetInt();
	}

	// fade them all for the low violence version
	if (g_RagdollLVManager.IsLowViolence())
	{
		iMaxRagdollCount = 0;
	}
	m_iRagdollCount = 0;
	m_iSimulatedRagdollCount = 0;

	// First, find ragdolls that are good candidates for deletion because they are not
	// visible at all, or are in a culled visibility box
	for (i = m_LRU.Head(); i < m_LRU.InvalidIndex(); i = next)
	{
		next = m_LRU.Next(i);
		C_BaseEntity* pRagdoll = m_LRU[i].Get();
		if (pRagdoll)
		{
			m_iRagdollCount++;
			IPhysicsObject* pObject = pRagdoll->GetEngineObject()->VPhysicsGetObject();
			if (pObject && !pObject->IsAsleep())
			{
				m_iSimulatedRagdollCount++;
			}
			if (m_LRU.Count() > iMaxRagdollCount)
			{
				//Found one, we're done.
				if (ShouldRemoveThisRagdoll(m_LRU[i]) == true)
				{
					m_LRU[i]->SUB_Remove();
					m_LRU.Remove(i);
					return;
				}
			}
		}
		else
		{
			m_LRU.Remove(i);
		}
	}

	//////////////////////////////
	///   EPISODIC ALGORITHM   ///
	//////////////////////////////
	// If we get here, it means we couldn't find a suitable ragdoll to remove,
	// so just remove the furthest one.
	int furthestOne = m_LRU.Head();
	float furthestDistSq = 0;
	C_BaseEntity* pPlayer = GetLocalPlayer();

	if (pPlayer && m_LRU.Count() > iMaxRagdollCount) // find the furthest one algorithm
	{
		Vector PlayerOrigin = pPlayer->GetEngineObject()->GetAbsOrigin();
		// const CBasePlayer *pPlayer = UTIL_GetLocalPlayer();

		for (i = m_LRU.Head(); i < m_LRU.InvalidIndex(); i = next)
		{
			C_BaseEntity* pRagdoll = m_LRU[i].Get();

			next = m_LRU.Next(i);
			IPhysicsObject* pObject = pRagdoll->GetEngineObject()->VPhysicsGetObject();
			if (pRagdoll && (pRagdoll->GetEffectEntity() || (pObject && !pObject->IsAsleep())))
				continue;

			if (pRagdoll)
			{
				// float distToPlayer = (pPlayer->GetAbsOrigin() - pRagdoll->GetAbsOrigin()).LengthSqr();
				float distToPlayer = (PlayerOrigin - pRagdoll->GetEngineObject()->GetAbsOrigin()).LengthSqr();

				if (distToPlayer > furthestDistSq)
				{
					furthestOne = i;
					furthestDistSq = distToPlayer;
				}
			}
			else // delete bad rags first.
			{
				furthestOne = i;
				break;
			}
		}

		m_LRU[furthestOne]->SUB_Remove();

	}
	else // fall back on old-style pick the oldest one algorithm
	{
		for (i = m_LRU.Head(); i < m_LRU.InvalidIndex(); i = next)
		{
			if (m_LRU.Count() <= iMaxRagdollCount)
				break;

			next = m_LRU.Next(i);

			C_BaseEntity* pRagdoll = m_LRU[i].Get();

			//Just ignore it until we're done burning/dissolving.
			IPhysicsObject* pObject = pRagdoll->GetEngineObject()->VPhysicsGetObject();
			if (pRagdoll && (pRagdoll->GetEffectEntity() || (pObject && !pObject->IsAsleep())))
				continue;

			m_LRU[i]->SUB_Remove();
			m_LRU.Remove(i);
		}
	}
}

#else
template<class T>
void CClientEntityList<T>::UpdateRagdolls(float frametime) // Non-episodic version
{
	VPROF("CRagdollLRURetirement::Update");
	// Compress out dead items
	int i, next;

	int iMaxRagdollCount = m_iMaxRagdolls;

	if (iMaxRagdollCount == -1)
	{
		iMaxRagdollCount = g_ragdoll_maxcount.GetInt();
	}

	// fade them all for the low violence version
	if (g_RagdollLVManager.IsLowViolence())
	{
		iMaxRagdollCount = 0;
	}
	m_iRagdollCount = 0;
	m_iSimulatedRagdollCount = 0;

	for (i = m_LRU.Head(); i < m_LRU.InvalidIndex(); i = next)
	{
		next = m_LRU.Next(i);
		C_BaseEntity* pRagdoll = m_LRU[i].Get();
		if (pRagdoll)
		{
			m_iRagdollCount++;
			IPhysicsObject* pObject = pRagdoll->GetEngineObject()->VPhysicsGetObject();
			if (pObject && !pObject->IsAsleep())
			{
				m_iSimulatedRagdollCount++;
			}
			if (m_LRU.Count() > iMaxRagdollCount)
			{
				//Found one, we're done.
				if (ShouldRemoveThisRagdoll(m_LRU[i]) == true)
				{
					m_LRU[i]->SUB_Remove();
					m_LRU.Remove(i);
					return;
				}
			}
		}
		else
		{
			m_LRU.Remove(i);
		}
	}


	//////////////////////////////
	///   ORIGINAL ALGORITHM   ///
	//////////////////////////////
	// not episodic -- this is the original mechanism

	for (i = m_LRU.Head(); i < m_LRU.InvalidIndex(); i = next)
	{
		if (m_LRU.Count() <= iMaxRagdollCount)
			break;

		next = m_LRU.Next(i);

		C_BaseEntity* pRagdoll = m_LRU[i].Get();

		//Just ignore it until we're done burning/dissolving.
		if (pRagdoll && pRagdoll->GetEffectEntity())
			continue;

		m_LRU[i]->SUB_Remove();
		m_LRU.Remove(i);
	}
}

#endif // HL2_EPISODIC

//-----------------------------------------------------------------------------
// Purpose: Gets a pointer to the local player, if it exists yet.
// Output : C_BasePlayer
//-----------------------------------------------------------------------------
template<class T>
C_BaseEntity* CClientEntityList<T>::GetLocalPlayer(void)
{
	return m_pLocalPlayer;
}

template<class T>
void CClientEntityList<T>::SetLocalPlayer(C_BaseEntity* pBasePlayer)
{
	m_pLocalPlayer = pBasePlayer;
}

template<class T>
void CClientEntityList<T>::AddImpactSound(void* pGameData, IPhysicsObject* pObject, int surfaceProps, int surfacePropsHit, float volume, float speed)
{
	physicssound::AddImpactSound(m_impactSounds, pGameData, SOUND_FROM_WORLD, CHAN_STATIC, pObject, surfaceProps, surfacePropsHit, volume, speed);
}

template<class T>
void CClientEntityList<T>::PhysicsSimulate()
{
	VPROF_BUDGET("CPhysicsSystem::PhysicsSimulate", VPROF_BUDGETGROUP_PHYSICS);
	float frametime = gpGlobals->frametime;

	if (m_pPhysenv)
	{
		tmZone(TELEMETRY_LEVEL0, TMZF_NONE, "%s %d", __FUNCTION__, m_pPhysenv->GetActiveObjectCount());

		m_Collisions.BufferTouchEvents(true);
#ifdef _DEBUG
		m_pPhysenv->DebugCheckContacts();
#endif
		m_pPhysenv->Simulate(frametime * cl_phys_timescale.GetFloat());

		int activeCount = m_pPhysenv->GetActiveObjectCount();
		IPhysicsObject** pActiveList = NULL;
		if (activeCount)
		{
			pActiveList = (IPhysicsObject**)stackalloc(sizeof(IPhysicsObject*) * activeCount);
			m_pPhysenv->GetActiveObjects(pActiveList);

			for (int i = 0; i < activeCount; i++)
			{
				C_BaseEntity* pEntity = reinterpret_cast<C_BaseEntity*>(pActiveList[i]->GetGameData());
				if (pEntity)
				{
					if (pEntity->GetEngineObject()->DoesVPhysicsInvalidateSurroundingBox())
					{
						pEntity->GetEngineObject()->MarkSurroundingBoundsDirty();
					}
					pEntity->GetEngineObject()->VPhysicsUpdate(pActiveList[i]);
				}
			}
		}

		m_Collisions.BufferTouchEvents(false);
		m_Collisions.FrameUpdate();
	}
	physicssound::PlayImpactSounds(m_impactSounds);
}

template<class T>
CCallQueue* CClientEntityList<T>::GetPostTouchQueue()
{
	return m_nTouchDepth > 0 ? &m_PostTouchQueue : NULL;
}

#endif // CLIENTENTITYLIST_H

