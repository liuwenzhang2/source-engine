//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ISERVERENTITY_H
#define ISERVERENTITY_H
#ifdef _WIN32
#pragma once
#endif


#include "iserverunknown.h"
#include "string_t.h"
#include "platform.h"
#include "isaverestore.h"
#include "vcollide_parse.h"
#include "studio.h"
#include "tier1/callqueue.h"

struct Ray_t;
class ServerClass;
class ICollideable;
class IServerNetworkable;
struct PVSInfo_t;
class CCheckTransmitInfo;
struct matrix3x4_t;
class CGameTrace;
typedef CGameTrace trace_t;
class IStudioHdr;
struct model_t;
class IPhysicsObject;
class IPhysicsPlayerController;
class CBoneAccessor;
struct ragdoll_t;
struct PS_SD_Static_World_StaticProps_ClippedProp_t;
struct PS_InternalData_t;
struct PS_SD_Static_SurfaceProperties_t;
class CUserCmd;
struct vehicleparams_t;
struct vehicle_controlparams_t;
struct vehicle_operatingparams_t;
class CIKContext;
class CTakeDamageInfo;
class CDmgAccumulator;
struct vehiclesounds_t;
class IServerEntity;
class IServerGameRules;
class IServerVehicle;
class variant_t;
class IEntitySaveUtils;
class CEventAction;
struct TimedOverlay_t;

struct servertouchlink_t
{
	CBaseHandle			entityTouched = NULL;
	int					touchStamp = 0;
	servertouchlink_t* nextLink = NULL;
	servertouchlink_t* prevLink = NULL;
	int					flags = 0;
};

//-----------------------------------------------------------------------------
// Purpose: Used for tracking many to one ground entity chains ( many ents can share a single ground entity )
//-----------------------------------------------------------------------------
struct servergroundlink_t
{
	CBaseHandle			entity;
	servergroundlink_t* nextLink;
	servergroundlink_t* prevLink;
};

typedef enum
{
	USE_OFF = 0,
	USE_ON = 1,
	USE_SET = 2,
	USE_TOGGLE = 3
} USE_TYPE;

typedef void (IHandleEntity::* THINKPTR)(void);
typedef void (IHandleEntity::* TOUCHPTR)(IServerEntity* pOther);
typedef void (IHandleEntity::* USEPTR)(IServerEntity* pActivator, IServerEntity* pCaller, USE_TYPE useType, float value);
#define DEFINE_THINKFUNC( function ) DEFINE_FUNCTION_RAW( function, THINKPTR )
#define DEFINE_TOUCHFUNC( function ) DEFINE_FUNCTION_RAW( function, TOUCHPTR )
#define DEFINE_USEFUNC( function ) DEFINE_FUNCTION_RAW( function, USEPTR )

class IEngineWorldServer;
class IEnginePlayerServer;
class IEnginePortalServer;
class IEngineShadowCloneServer;
class IEngineVehicleServer;
class IEngineRopeServer;
class IEngineGhostServer;

class IGrabControllerServer {
public:
	virtual void AttachEntity(IServerEntity* pPlayer, IServerEntity* pEntity, IPhysicsObject* pPhys, bool bIsMegaPhysCannon, const Vector& vGrabPosition, bool bUseGrabPosition) = 0;
	virtual void DetachEntity(bool bClearVelocity) = 0;
	virtual IServerEntity* GetAttached() = 0;
	virtual const QAngle& GetAttachedAnglesPlayerSpace() = 0;
	virtual void SetAttachedAnglesPlayerSpace(const QAngle& attachedAnglesPlayerSpace) = 0;
	virtual const Vector& GetAttachedPositionObjectSpace() = 0;
	virtual void SetAttachedPositionObjectSpace(const Vector& attachedPositionObjectSpace) = 0;
	virtual void SetIgnorePitch(bool bIgnore) = 0;
	virtual void SetAngleAlignment(float alignAngleCosine) = 0;
	virtual float GetLoadWeight(void) const = 0;
	virtual float ComputeError() = 0;
	virtual bool UpdateObject(IServerEntity* pPlayer, float flError) = 0;
	virtual float GetSavedMass(IPhysicsObject* pObject) = 0;
	virtual void GetSavedParamsForCarriedPhysObject(IPhysicsObject* pObject, float* pSavedMassOut, float* pSavedRotationalDampingOut) = 0;
	virtual void GetTargetPosition(Vector* target, QAngle* targetOrientation) = 0;
	virtual void SetPortalPenetratingEntity(IServerEntity* pPenetrated) = 0;
};

class IEngineObjectServer : public IEngineObject {
public:

	virtual IServerEntity* GetServerEntity() = 0;
	virtual IServerEntity* GetOuter() = 0;
	virtual IHandleEntity* GetHandleEntity() const = 0;

	virtual void ParseMapData(IEntityMapData* mapData) = 0;
	virtual int	Save(ISave& save) = 0;
	virtual int	Restore(IRestore& restore) = 0;

	virtual void SetAbsVelocity(const Vector& vecVelocity) = 0;
	//virtual const Vector& GetAbsVelocity() = 0;
	virtual const Vector& GetAbsVelocity() const = 0;

	// NOTE: Setting the abs origin or angles will cause the local origin + angles to be set also
	virtual void SetAbsOrigin(const Vector& origin) = 0;
	//virtual const Vector& GetAbsOrigin(void) = 0;
	virtual const Vector& GetAbsOrigin(void) const = 0;

	virtual void SetAbsAngles(const QAngle& angles) = 0;
	//virtual const QAngle& GetAbsAngles(void) = 0;
	virtual const QAngle& GetAbsAngles(void) const = 0;

	// Origin and angles in local space ( relative to parent )
	// NOTE: Setting the local origin or angles will cause the abs origin + angles to be set also
	virtual void SetLocalOrigin(const Vector& origin) = 0;
	virtual const Vector& GetLocalOrigin(void) const = 0;

	virtual void SetLocalAngles(const QAngle& angles) = 0;
	virtual const QAngle& GetLocalAngles(void) const = 0;

	virtual void SetLocalVelocity(const Vector& vecVelocity) = 0;
	virtual const Vector& GetLocalVelocity() const = 0;

	virtual void CalcAbsolutePosition() = 0;
	virtual void CalcAbsoluteVelocity() = 0;

	// Set the movement parent. Your local origin and angles will become relative to this parent.
// If iAttachment is a valid attachment on the parent, then your local origin and angles 
// are relative to the attachment on this entity. If iAttachment == -1, it'll preserve the
// current m_iParentAttachment.
	virtual void	SetParent(IEngineObjectServer* pNewParent, int iAttachment = -1) = 0;
	// FIXME: Make hierarchy a member of IServerEntity
	// or a contained private class...
	virtual void UnlinkChild(IEngineObjectServer* pChild) = 0;
	virtual void LinkChild(IEngineObjectServer* pChild) = 0;
	//virtual void ClearParent(IEngineObjectServer* pEntity) = 0;
	virtual void UnlinkAllChildren() = 0;
	virtual void UnlinkFromParent() = 0;
	virtual void TransferChildren(IEngineObjectServer* pNewParent) = 0;
	virtual bool EntityHasMatchingRootParent(IEngineObjectServer* pRootParent) = 0;

	virtual IEngineObjectServer* GetMoveParent(void) const = 0;
	//virtual void SetMoveParent(IEngineObjectServer* hMoveParent) = 0;
	virtual IEngineObjectServer* GetRootMoveParent() = 0;
	virtual IEngineObjectServer* FirstMoveChild(void) const = 0;
	//virtual void SetFirstMoveChild(IEngineObjectServer* hMoveChild) = 0;
	virtual IEngineObjectServer* NextMovePeer(void) const = 0;
	//virtual void SetNextMovePeer(IEngineObjectServer* hMovePeer) = 0;
	virtual int GetAllChildren(CUtlVector<IEngineObjectServer*>& list) = 0;
	virtual bool EntityIsParentOf(IEngineObjectServer* pEntity) = 0;
	virtual int GetAllInHierarchy(CUtlVector<IEngineObjectServer*>& list) = 0;
	virtual void ResetRgflCoordinateFrame() = 0;
	// Returns the entity-to-world transform
	//virtual matrix3x4_t& EntityToWorldTransform() = 0;
	virtual const matrix3x4_t& EntityToWorldTransform() const = 0;

	// Some helper methods that transform a point from entity space to world space + back
	virtual void EntityToWorldSpace(const Vector& in, Vector* pOut) const = 0;
	virtual void WorldToEntitySpace(const Vector& in, Vector* pOut) const = 0;

	// This function gets your parent's transform. If you're parented to an attachment,
	// this calculates the attachment's transform and gives you that.
	//
	// You must pass in tempMatrix for scratch space - it may need to fill that in and return it instead of 
	// pointing you right at a variable in your parent.
	virtual const matrix3x4_t& GetParentToWorldTransform(matrix3x4_t& tempMatrix) = 0;

	// Computes the abs position of a point specified in local space
	virtual void ComputeAbsPosition(const Vector& vecLocalPosition, Vector* pAbsPosition) = 0;

	// Computes the abs position of a direction specified in local space
	virtual void ComputeAbsDirection(const Vector& vecLocalDirection, Vector* pAbsDirection) = 0;

	virtual void	GetVectors(Vector* forward, Vector* right, Vector* up) const = 0;

	virtual int	AreaNum() const = 0;
	virtual PVSInfo_t* GetPVSInfo() = 0;

	// This version does a PVS check which also checks for connected areas
	virtual bool IsInPVS(const CCheckTransmitInfo* pInfo) = 0;

	// This version doesn't do the area check
	virtual bool IsInPVS(const IServerEntity* pRecipient, const void* pvs, int pvssize) = 0;

	// Recomputes PVS information
	virtual void RecomputePVSInformation() = 0;
	// Marks the PVS information dirty
	virtual void MarkPVSInformationDirty() = 0;

	virtual void SetClassname(const char* className) = 0;
	virtual const char* GetClassName() const = 0;
	virtual const string_t& GetClassname() const = 0;
	virtual void SetGlobalname(const char* iGlobalname) = 0;
	virtual const string_t& GetGlobalname() const = 0;
	virtual void SetParentName(const char* parentName) = 0;
	virtual string_t& GetParentName() = 0;
	virtual void SetName(const char* newName) = 0;
	virtual string_t& GetEntityName() = 0;
	virtual bool NameMatches(const char* pszNameOrWildcard) = 0;
	virtual bool ClassMatches(const char* pszClassOrWildcard) = 0;
	virtual bool NameMatches(string_t nameStr) = 0;
	virtual bool ClassMatches(string_t nameStr) = 0;
	virtual IServerNetworkable* GetNetworkable() = 0;
	virtual int GetParentAttachment() = 0;
	virtual void ClearParentAttachment() = 0;
	virtual void AddFlag(int flags) = 0;
	virtual void RemoveFlag(int flagsToRemove) = 0;
	virtual void ToggleFlag(int flagToToggle) = 0;
	virtual int GetFlags(void) const = 0;
	virtual void ClearFlags(void) = 0;
	virtual int GetEFlags() const = 0;
	virtual void SetEFlags(int iEFlags) = 0;
	virtual void AddEFlags(int nEFlagMask) = 0;
	virtual void RemoveEFlags(int nEFlagMask) = 0;
	virtual bool IsEFlagSet(int nEFlagMask) const = 0;
	virtual void MarkForDeletion() = 0;
	virtual bool IsMarkedForDeletion(void) = 0;
	virtual bool IsMarkedForDeletion() const = 0;
	virtual int GetSpawnFlags(void) const = 0;
	virtual void SetSpawnFlags(int nFlags) = 0;
	virtual void AddSpawnFlags(int nFlags) = 0;
	virtual void RemoveSpawnFlags(int nFlags) = 0;
	virtual void ClearSpawnFlags(void) = 0;
	virtual bool HasSpawnFlags(int nFlags) const = 0;
	virtual void SetCheckUntouch(bool check) = 0;
	virtual bool GetCheckUntouch() const = 0;
	virtual int GetTouchStamp() = 0;
	virtual void ClearTouchStamp() = 0;
	virtual bool HasDataObjectType(int type) const = 0;
	virtual void* GetDataObject(int type) = 0;
	virtual void* CreateDataObject(int type) = 0;
	virtual void DestroyDataObject(int type) = 0;
	virtual void DestroyAllDataObjects(void) = 0;
	virtual void InvalidatePhysicsRecursive(int nChangeFlags) = 0;
	// HACKHACK:Get the trace_t from the last physics touch call (replaces the even-hackier global trace vars)
	virtual const trace_t& GetTouchTrace(void) = 0;
	// FIXME: Should be private, but I can't make em private just yet
	virtual void PhysicsImpact(IEngineObjectServer* other, trace_t& trace) = 0;
	virtual void PhysicsTouchTriggers(const Vector* pPrevAbsOrigin = NULL) = 0;
	virtual void PhysicsMarkEntitiesAsTouching(IEngineObjectServer* other, trace_t& trace) = 0;
	virtual void PhysicsMarkEntitiesAsTouchingEventDriven(IEngineObjectServer* other, trace_t& trace) = 0;
	virtual servertouchlink_t* PhysicsMarkEntityAsTouched(IEngineObjectServer* other) = 0;
	virtual void PhysicsTouch(IEngineObjectServer* pentOther) = 0;
	virtual void PhysicsStartTouch(IEngineObjectServer* pentOther) = 0;
	virtual bool IsCurrentlyTouching(void) const = 0;

	// Physics helper
	virtual void PhysicsCheckForEntityUntouch(void) = 0;
	virtual void PhysicsNotifyOtherOfUntouch(IEngineObjectServer* ent) = 0;
	virtual void PhysicsRemoveTouchedList() = 0;
	virtual void PhysicsRemoveToucher(servertouchlink_t* link) = 0;

	virtual servergroundlink_t* AddEntityToGroundList(IEngineObjectServer* other) = 0;
	virtual void PhysicsStartGroundContact(IEngineObjectServer* pentOther) = 0;
	virtual void PhysicsNotifyOtherOfGroundRemoval(IEngineObjectServer* ent) = 0;
	virtual void PhysicsRemoveGround(servergroundlink_t* link) = 0;
	virtual void PhysicsRemoveGroundList() = 0;

	virtual void SetGroundEntity(IEngineObjectServer* ground) = 0;
	virtual IEngineObjectServer* GetGroundEntity(void) = 0;
	virtual IEngineObjectServer* GetGroundEntity(void) const = 0;
	virtual void SetGroundChangeTime(float flTime) = 0;
	virtual float GetGroundChangeTime(void) = 0;

	virtual string_t GetModelName(void) const = 0;
	virtual void SetModelName(string_t name) = 0;
	virtual void SetModelIndex(int index) = 0;
	virtual int GetModelIndex(void) const = 0;

	//virtual ICollideable* CollisionProp() = 0;
	//virtual const ICollideable* CollisionProp() const = 0;
	virtual ICollideable* GetCollideable() = 0;
	// This defines collision bounds in OBB space
	virtual void SetCollisionBounds(const Vector& mins, const Vector& maxs) = 0;
	virtual void SetSize(const Vector& mins, const Vector& maxs) = 0;
	virtual SolidType_t GetSolid() const = 0;
	virtual bool IsSolid() const = 0;
	virtual void SetSolid(SolidType_t val) = 0;
	virtual void AddSolidFlags(int flags) = 0;
	virtual void RemoveSolidFlags(int flags) = 0;
	virtual void ClearSolidFlags(void) = 0;
	virtual bool IsSolidFlagSet(int flagMask) const = 0;
	virtual void SetSolidFlags(int flags) = 0;
	virtual int GetSolidFlags(void) const = 0;
	virtual const Vector& GetCollisionOrigin() const = 0;
	virtual const QAngle& GetCollisionAngles() const = 0;
	virtual const Vector& OBBMinsPreScaled() const = 0;
	virtual const Vector& OBBMaxsPreScaled() const = 0;
	virtual const Vector& OBBMins() const = 0;
	virtual const Vector& OBBMaxs() const = 0;
	virtual const Vector& OBBSize() const = 0;
	virtual const Vector& OBBCenter() const = 0;
	virtual const Vector& WorldSpaceCenter() const = 0;
	virtual void WorldSpaceAABB(Vector* pWorldMins, Vector* pWorldMaxs) const = 0;
	virtual void WorldSpaceSurroundingBounds(Vector* pVecMins, Vector* pVecMaxs) = 0;
	virtual void WorldSpaceTriggerBounds(Vector* pVecWorldMins, Vector* pVecWorldMaxs) const = 0;
	virtual const Vector& NormalizedToWorldSpace(const Vector& in, Vector* pResult) const = 0;
	virtual const Vector& WorldToNormalizedSpace(const Vector& in, Vector* pResult) const = 0;
	virtual const Vector& WorldToCollisionSpace(const Vector& in, Vector* pResult) const = 0;
	virtual const Vector& CollisionToWorldSpace(const Vector& in, Vector* pResult) const = 0;
	virtual const Vector& WorldDirectionToCollisionSpace(const Vector& in, Vector* pResult) const = 0;
	virtual const Vector& NormalizedToCollisionSpace(const Vector& in, Vector* pResult) const = 0;
	virtual const matrix3x4_t& CollisionToWorldTransform() const = 0;
	virtual float BoundingRadius() const = 0;
	virtual float BoundingRadius2D() const = 0;
	virtual bool IsPointSized() const = 0;
	virtual void RandomPointInBounds(const Vector& vecNormalizedMins, const Vector& vecNormalizedMaxs, Vector* pPoint) const = 0;
	virtual bool IsPointInBounds(const Vector& vecWorldPt) const = 0;
	virtual void UseTriggerBounds(bool bEnable, float flBloat = 0.0f) = 0;
	virtual void RefreshScaledCollisionBounds(void) = 0;
	virtual void MarkPartitionHandleDirty() = 0;
	virtual bool DoesRotationInvalidateSurroundingBox() const = 0;
	virtual void MarkSurroundingBoundsDirty() = 0;
	virtual void CalcNearestPoint(const Vector& vecWorldPt, Vector* pVecNearestWorldPt) const = 0;
	virtual void SetSurroundingBoundsType(SurroundingBoundsType_t type, const Vector* pMins = NULL, const Vector* pMaxs = NULL) = 0;
	//virtual void CreatePartitionHandle() = 0;
	//virtual void DestroyPartitionHandle() = 0;
	virtual unsigned short	GetPartitionHandle() const = 0;
	virtual float CalcDistanceFromPoint(const Vector& vecWorldPt) const = 0;
	virtual bool DoesVPhysicsInvalidateSurroundingBox() const = 0;
	virtual void UpdatePartition() = 0;
	virtual bool IsBoundsDefinedInEntitySpace() const = 0;
	virtual bool Intersects(IEngineObjectServer* pOther) = 0;
	virtual int GetCollisionGroup() const = 0;
	virtual void SetCollisionGroup(int collisionGroup) = 0;
	virtual void CollisionRulesChanged() = 0;
	virtual int GetEffects(void) const = 0;
	virtual void AddEffects(int nEffects) = 0;
	virtual void RemoveEffects(int nEffects) = 0;
	virtual void ClearEffects(void) = 0;
	virtual void SetEffects(int nEffects) = 0;
	virtual bool IsEffectActive(int nEffects) const = 0;
	virtual float GetGravity(void) const = 0;
	virtual void SetGravity(float gravity) = 0;
	virtual float GetFriction(void) const = 0;
	virtual void SetFriction(float flFriction) = 0;
	virtual void SetElasticity(float flElasticity) = 0;
	virtual float GetElasticity(void) const = 0;

	virtual THINKPTR GetPfnThink() = 0;
	virtual void SetPfnThink(THINKPTR pfnThink) = 0;
	virtual int GetIndexForThinkContext(const char* pszContext) = 0;
	virtual int RegisterThinkContext(const char* szContext) = 0;
	virtual THINKPTR ThinkSet(THINKPTR func, float flNextThinkTime = 0, const char* szContext = NULL) = 0;
	virtual void SetNextThink(float nextThinkTime, const char* szContext = NULL) = 0;
	virtual float GetNextThink(const char* szContext = NULL) = 0;
	virtual int GetNextThinkTick(const char* szContext = NULL) = 0;
	virtual float GetLastThink(const char* szContext = NULL) = 0;
	virtual int GetLastThinkTick(const char* szContext = NULL) = 0;
	virtual void SetLastThinkTick(int iThinkTick) = 0;
	virtual bool WillThink() = 0;
	virtual int GetFirstThinkTick() = 0;	// get first tick thinking on any context
	virtual bool PhysicsRunThink(thinkmethods_t thinkMethod = THINK_FIRE_ALL_FUNCTIONS) = 0;
	virtual bool PhysicsRunSpecificThink(int nContextIndex, THINKPTR thinkFunc) = 0;
	virtual void CheckHasThinkFunction(bool isThinkingHint = false) = 0;

	virtual MoveType_t GetMoveType() const = 0;
	virtual MoveCollide_t GetMoveCollide() const = 0;
	virtual void SetMoveType(MoveType_t val, MoveCollide_t moveCollide = MOVECOLLIDE_DEFAULT) = 0;
	virtual void SetMoveCollide(MoveCollide_t val) = 0;
	virtual void CheckStepSimulationChanged() = 0;
	virtual void CheckHasGamePhysicsSimulation() = 0;
	virtual bool IsSimulatedEveryTick() const = 0;
	virtual void SetSimulatedEveryTick(bool sim) = 0;
	virtual bool IsAnimatedEveryTick() const = 0;
	virtual void SetAnimatedEveryTick(bool anim) = 0;

	virtual bool DoesHavePlayerChild() = 0;

	virtual void FollowEntity(IEngineObjectServer* pBaseEntity, bool bBoneMerge = true) = 0;
	virtual void StopFollowingEntity() = 0;	// will also change to MOVETYPE_NONE
	virtual bool IsFollowingEntity() = 0;
	virtual IEngineObjectServer* GetFollowedEntity() = 0;
	virtual float GetAnimTime() const = 0;
	virtual void SetAnimTime(float at) = 0;
	virtual float GetSimulationTime() const = 0;
	virtual void SetSimulationTime(float st) = 0;
	virtual void UseClientSideAnimation() = 0;
	virtual bool IsUsingClientSideAnimation() = 0;
	virtual void SimulationChanged() = 0;
	virtual Vector GetVecForce() = 0;
	virtual void SetVecForce(Vector vecForce) = 0;
	virtual int	GetForceBone() = 0;
	virtual void SetForceBone(int nForceBone) = 0;
	virtual int GetBody() = 0;
	virtual void SetBody(int nBody) = 0;
	virtual int GetSkin() = 0;
	virtual void SetSkin(int nSkin) = 0;
	virtual int GetHitboxSet() = 0;
	virtual void SetHitboxSet(int nHitboxSet) = 0;
	virtual void SetModelScale(float scale, float change_duration = 0.0f) = 0;
	virtual float GetModelScale() const = 0;
	virtual void UpdateModelScale() = 0;
	virtual const model_t* GetModel(void) const = 0;
	virtual IStudioHdr* GetModelPtr(void) const = 0;
	virtual void InvalidateMdlCache() = 0;
	virtual float GetCycle() const = 0;
	virtual void SetCycle(float flCycle) = 0;
	virtual float GetPlaybackRate() = 0;
	virtual void SetPlaybackRate(float rate) = 0;
	virtual int GetSequence() = 0;
	virtual void SetSequence(int nSequence) = 0;
	virtual void ResetSequence(int nSequence) = 0;
	virtual void ResetSequenceInfo() = 0;
	virtual bool SequenceLoops(void) = 0;
	virtual bool IsSequenceFinished(void) = 0;
	virtual void SetSequenceFinished(bool bFinished) = 0;
	virtual float GetLastEventCheck() = 0;
	virtual void SetLastEventCheck(float flLastEventCheck) = 0;
	virtual float SequenceDuration(int iSequence) = 0;
	virtual float SequenceDuration(void) = 0;
	virtual float GetSequenceMoveYaw(int iSequence) = 0;
	virtual void GetSequenceLinearMotion(int iSequence, Vector* pVec) = 0;
	virtual float GetSequenceCycleRate(IStudioHdr* pStudioHdr, int iSequence) = 0;
	virtual float GetSequenceCycleRate(int iSequence) = 0;
	virtual float GetSequenceGroundSpeed(IStudioHdr* pStudioHdr, int iSequence) = 0;
	virtual float GetSequenceGroundSpeed(int iSequence) = 0;
	virtual float GetGroundSpeed() const = 0;
	virtual void SetGroundSpeed(float flGroundSpeed) = 0;
	virtual float GetSpeedScale() = 0;
	virtual void SetSpeedScale(float flSpeedScale) = 0;
	virtual float GetEntryVelocity(int iSequence) = 0;
	virtual float GetExitVelocity(int iSequence) = 0;
	virtual float GetInstantaneousVelocity(float flInterval = 0.0) = 0;
	virtual float GetMovementFrame(float flDist) = 0;
	virtual bool HasMovement(int iSequence) = 0;
	virtual bool GetSequenceMovement(int nSequence, float fromCycle, float toCycle, Vector& deltaPosition, QAngle& deltaAngles) = 0;
	virtual bool GetIntervalMovement(float flIntervalUsed, bool& bMoveSeqFinished, Vector& newPosition, QAngle& newAngles) = 0;
	virtual const float* GetEncodedControllerArray() = 0;
	virtual float GetBoneController(int iController) = 0;
	virtual float SetBoneController(int iController, float flValue) = 0;
	virtual int LookupPoseParameter(IStudioHdr* pStudioHdr, const char* szName) = 0;
	virtual int LookupPoseParameter(const char* szName) = 0;
	virtual bool GetPoseParameterRange(int index, float& minValue, float& maxValue) = 0;
	virtual const float* GetPoseParameterArray() = 0;
	virtual float GetPoseParameter(const char* szName) = 0;
	virtual float GetPoseParameter(int iParameter) = 0;
	virtual float SetPoseParameter(IStudioHdr* pStudioHdr, const char* szName, float flValue) = 0;
	virtual float SetPoseParameter(IStudioHdr* pStudioHdr, int iParameter, float flValue) = 0;
	virtual float SetPoseParameter(const char* szName, float flValue) = 0;
	virtual float SetPoseParameter(int iParameter, float flValue) = 0;
	virtual void ResetClientsideFrame(void) = 0;
	virtual void DoMuzzleFlash() = 0;
	virtual void VPhysicsDestroyObject(void) = 0;
	virtual IPhysicsObject* VPhysicsGetObject(void) const = 0;
	virtual int VPhysicsGetObjectList(IPhysicsObject** pList, int listMax) = 0;
	virtual void VPhysicsSetObject(IPhysicsObject* pPhysics) = 0;
	virtual void VPhysicsSwapObject(IPhysicsObject* pSwap) = 0;
	virtual const Vector& WorldAlignMins() const = 0;
	virtual const Vector& WorldAlignMaxs() const = 0;
	virtual const Vector& WorldAlignSize() const = 0;
	virtual IPhysicsObject* VPhysicsInitStatic(void) = 0;
	virtual IPhysicsObject* VPhysicsInitNormal(SolidType_t solidType, int nSolidFlags, bool createAsleep, solid_t* pSolid = NULL) = 0;
	virtual IPhysicsObject* VPhysicsInitShadow(bool allowPhysicsMovement, bool allowPhysicsRotation, solid_t* pSolid = NULL) = 0;
	
	virtual IPhysicsObject* GetGroundVPhysics() = 0;
	virtual bool IsRideablePhysics(IPhysicsObject* pPhysics) = 0;
	virtual int LookupSequence(const char* label) = 0;
	virtual int SelectWeightedSequence(int activity) = 0;
	virtual int SelectWeightedSequence(int activity, int curSequence) = 0;
	virtual int SelectHeaviestSequence(int activity) = 0;
	virtual void InitRagdoll(const Vector& forceVector, int forceBone, const Vector& forcePos, matrix3x4_t* pPrevBones, matrix3x4_t* pBoneToWorld, float dt, int collisionGroup, bool activateRagdoll, bool bWakeRagdoll = true) = 0;
	virtual void RecheckCollisionFilter(void) = 0;
	virtual int RagdollBoneCount() const = 0;
	virtual IPhysicsObject* GetElement(int elementNum) = 0;
	virtual void GetAngleOverrideFromCurrentState(char* pOut, int size) = 0;
	virtual void RagdollBone(bool* boneSimulated, CBoneAccessor& pBoneToWorld) = 0;
	virtual void UpdateNetworkDataFromVPhysics(int index) = 0;
	virtual void VPhysicsUpdate(IPhysicsObject* pPhysics) = 0;
	virtual bool GetAllAsleep() = 0;
	virtual IPhysicsConstraintGroup* GetConstraintGroup() = 0;
	virtual ragdoll_t* GetRagdoll(void) = 0;
	virtual void ClearRagdoll() = 0;
	virtual bool IsRagdoll() const = 0;
	virtual void ActiveRagdoll() = 0;
	virtual void ApplyAnimationAsVelocityToRagdoll(const matrix3x4_t* pPrevBones, const matrix3x4_t* pCurrentBones, float dt) = 0;

	virtual unsigned char GetRenderFX() const = 0;
	virtual void SetRenderFX(unsigned char nRenderFX) = 0;
	virtual void SetOverlaySequence(int nOverlaySequence) = 0;

	virtual void SetupBones(matrix3x4_t* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime) = 0;
	virtual void DrawRawSkeleton(matrix3x4_t boneToWorld[], int boneMask, bool noDepthTest = true, float duration = 0.0f, bool monocolor = false) = 0;
	virtual bool ComputeHitboxSurroundingBox(Vector* pVecWorldMins, Vector* pVecWorldMaxs) = 0;
	virtual bool ComputeEntitySpaceHitboxSurroundingBox(Vector* pVecWorldMins, Vector* pVecWorldMaxs) = 0;
	virtual void GetHitboxBoneTransform(int iBone, matrix3x4_t& pBoneToWorld) = 0;
	virtual void GetHitboxBoneTransforms(const matrix3x4_t* hitboxbones[MAXSTUDIOBONES]) = 0;
	virtual void GetHitboxBonePosition(int iBone, Vector& origin, QAngle& angles) = 0;
	virtual int  LookupBone(const char* szName) = 0;
	virtual int	GetPhysicsBone(int boneIndex) = 0;

	virtual void InvalidateBoneCache() = 0;
	virtual void InvalidateBoneCacheIfOlderThan(float deltaTime) = 0;
	virtual int GetBoneCacheFlags(void) = 0;
	virtual void SetBoneCacheFlags(unsigned short fFlag) = 0;
	virtual void ClearBoneCacheFlags(unsigned short fFlag) = 0;
	// also calculate IK on server? (always done on client)
	virtual void EnableServerIK() = 0;
	virtual void DisableServerIK() = 0;
	virtual CIKContext* GetIk() = 0;
	virtual void SetIKGroundContactInfo(float minHeight, float maxHeight) = 0;
	virtual void InitStepHeightAdjust(void) = 0;
	virtual void UpdateStepOrigin(void) = 0;
	virtual float GetEstIkOffset() const = 0;
	virtual int GetAttachmentBone(int iAttachment) = 0;
	virtual int LookupAttachment(const char* szName) = 0;
	virtual bool GetAttachment(int iAttachment, matrix3x4_t& attachmentToWorld) = 0;
	virtual bool GetAttachment(int iAttachment, Vector& absOrigin, QAngle& absAngles) = 0;
	virtual bool GetAttachment(int iAttachment, Vector& absOrigin, Vector* forward = NULL, Vector* right = NULL, Vector* up = NULL) = 0;
	virtual bool GetAttachment(const char* szName, Vector& absOrigin, QAngle& absAngles) = 0;
	virtual bool GetAttachment(const char* szName, Vector& absOrigin, Vector* forward = NULL, Vector* right = NULL, Vector* up = NULL) = 0;
	virtual void SetAlternateSorting(bool bAlternateSorting) = 0;
	virtual void IncrementInterpolationFrame() = 0;
	virtual bool PhysModelParseSolid(solid_t& solid) = 0;
	virtual bool PhysModelParseSolidByIndex(solid_t& solid, int solidIndex) = 0;
	virtual void PhysForceClearVelocity(IPhysicsObject* pPhys) = 0;
	virtual float PhysGetEntityMass() = 0;
	virtual IEngineObjectServer* GetClonesOfEntity() const = 0;
	virtual IEnginePortalServer* GetPortalThatOwnsEntity() = 0;

	virtual bool IsWorld() = 0;
	virtual IEngineWorldServer* AsEngineWorld() = 0;
	virtual const IEngineWorldServer* AsEngineWorld() const = 0;
	virtual bool IsPlayer() = 0;
	virtual IEnginePlayerServer* AsEnginePlayer() = 0;
	virtual const IEnginePlayerServer* AsEnginePlayer() const = 0;
	virtual bool IsPortal() = 0;
	virtual IEnginePortalServer* AsEnginePortal() = 0;
	virtual const IEnginePortalServer* AsEnginePortal() const = 0;
	virtual bool IsShadowClone() = 0;
	virtual IEngineShadowCloneServer* AsEngineShadowClone() = 0;
	virtual const IEngineShadowCloneServer* AsEngineShadowClone() const = 0;
	virtual bool IsVehicle() = 0;
	virtual IEngineVehicleServer* AsEngineVehicle() = 0;
	virtual const IEngineVehicleServer* AsEngineVehicle() const = 0;
	virtual bool IsRope() = 0;
	virtual IEngineRopeServer* AsEngineRope() = 0;
	virtual const IEngineRopeServer* AsEngineRope() const = 0;
	virtual bool IsGhost() = 0;
	virtual IEngineGhostServer* AsEngineGhost() = 0;
	virtual const IEngineGhostServer* AsEngineGhost() const = 0;
	virtual IGrabControllerServer* GetGrabController() = 0;
};

class IEngineWorldServer : public IEngineWorld {
public:
	// Sweeps a particular entity through the world
	virtual void TraceEntity(IEngineObjectServer* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, trace_t* ptr) = 0;
	virtual void TraceEntity(IEngineObjectServer* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter* pFilter, trace_t* ptr) = 0;
	virtual void TraceEntity(IEngineObjectServer* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, const IHandleEntity* ignore, int collisionGroup, trace_t* ptr) = 0;
	virtual void TraceLineFilterEntity(IEngineObjectServer* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, const int nCollisionGroup, trace_t* ptr) = 0;
};

enum
{
	VPHYS_WALK = 0,
	VPHYS_CROUCH,
	VPHYS_NOCLIP,
};

class IEnginePortalServer;

class IEnginePlayerServer : public IEnginePlayer {
public:
	virtual void SetupVPhysicsShadow(const Vector& vHullMin, const Vector& vHullMax, const Vector& vDuckHullMin, const Vector& vDuckHullMax) = 0;
	virtual IPhysicsPlayerController* GetPhysicsController() = 0;
	virtual void UpdateVPhysicsPosition(const Vector& position, const Vector& velocity, float secondsToArrival) = 0;
	virtual void SetVCollisionState(const Vector& vecAbsOrigin, const Vector& vecAbsVelocity, int collisionState) = 0;
	virtual int GetVphysicsCollisionState() = 0;
	virtual IEnginePortalServer* GetPortalEnvironment() = 0;
	virtual void SetPortalEnvironment(IEnginePortalServer* pEnginePortal) = 0;
	virtual IEnginePortalServer* GetHeldObjectPortal(void) = 0;
	virtual void SetHeldObjectPortal(IEnginePortalServer* pPortal) = 0;
	virtual void ToggleHeldObjectOnOppositeSideOfPortal(void) = 0;
	virtual void SetHeldObjectOnOppositeSideOfPortal(bool p_bHeldObjectOnOppositeSideOfPortal) = 0;
	virtual bool IsHeldObjectOnOppositeSideOfPortal(void) = 0;
	virtual bool IsSilentDropAndPickup() = 0;
	virtual void SetSilentDropAndPickup(bool bSilentDropAndPickup) = 0;
};

class IEnginePortalServer : public IEnginePortal {
public:
	virtual int					GetPortalSimulatorGUID(void) const = 0;
	virtual void				SetVPhysicsSimulationEnabled(bool bEnabled) = 0;
	virtual bool				IsSimulatingVPhysics(void) const = 0;
	virtual bool				IsLocalDataIsReady() const = 0;
	virtual void				SetLocalDataIsReady(bool bLocalDataIsReady) = 0;
	virtual bool				IsReadyToSimulate(void) const = 0;
	virtual bool				IsActivedAndLinked(void) const = 0;
	virtual void				MoveTo(const Vector& ptCenter, const QAngle& angles) = 0;
	virtual void				AttachTo(IEnginePortalServer* pLinkedPortal) = 0;
	virtual IEnginePortalServer* GetLinkedPortal() = 0;
	virtual const IEnginePortalServer* GetLinkedPortal() const = 0;
	virtual void				DetachFromLinked(void) = 0;
	virtual void				UpdateLinkMatrix(IEnginePortalServer* pRemoteCollisionEntity) = 0;
	virtual bool				EntityIsInPortalHole(IEngineObjectServer* pEntity) const = 0; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	virtual bool				EntityHitBoxExtentIsInPortalHole(IEngineObjectServer* pBaseAnimating) const = 0; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	virtual bool				RayIsInPortalHole(const Ray_t& ray) const = 0; //traces a ray against the same detector for EntityIsInPortalHole(), bias is towards false positives
	virtual bool				TraceWorldBrushes(const Ray_t& ray, trace_t* pTrace) const = 0;
	virtual bool				TraceWallTube(const Ray_t& ray, trace_t* pTrace) const = 0;
	virtual bool				TraceWallBrushes(const Ray_t& ray, trace_t* pTrace) const = 0;
	virtual bool				TraceTransformedWorldBrushes(const IEnginePortalServer* pRemoteCollisionEntity, const Ray_t& ray, trace_t* pTrace) const = 0;
	virtual void				TraceRay(const Ray_t& ray, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace, bool bTraceHolyWall = true) const = 0;
	virtual void				TraceEntity(IHandleEntity* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter* pFilter, trace_t* ptr) const = 0;
	virtual int					GetStaticPropsCount() const = 0;
	virtual const PS_SD_Static_World_StaticProps_ClippedProp_t* GetStaticProps(int index) const = 0;
	virtual bool				StaticPropsCollisionExists() const = 0;
	//const Vector& GetOrigin() const;
	//const QAngle& GetAngles() const;
	virtual const Vector& GetTransformedOrigin() const = 0;
	virtual const QAngle& GetTransformedAngles() const = 0;
	virtual const VMatrix& MatrixThisToLinked() const = 0;
	virtual const VMatrix& MatrixLinkedToThis() const = 0;
	virtual const cplane_t& GetPortalPlane() const = 0;
	virtual const Vector& GetVectorForward() const = 0;
	virtual const Vector& GetVectorUp() const = 0;
	virtual const Vector& GetVectorRight() const = 0;
	virtual const PS_SD_Static_SurfaceProperties_t& GetSurfaceProperties() const = 0;
	virtual IPhysicsObject* GetWorldBrushesPhysicsObject() const = 0;
	virtual IPhysicsObject* GetWallBrushesPhysicsObject() const = 0;
	virtual IPhysicsObject* GetWallTubePhysicsObject() const = 0;
	virtual IPhysicsObject* GetRemoteWallBrushesPhysicsObject() const = 0;
	virtual IPhysicsEnvironment* GetPhysicsEnvironment() = 0;
	virtual void				CreatePhysicsEnvironment() = 0;
	virtual void				ClearPhysicsEnvironment() = 0;
	virtual void				CreatePolyhedrons(void) = 0;
	virtual void				ClearPolyhedrons(void) = 0;
	virtual void				CreateLocalCollision(void) = 0;
	virtual void				ClearLocalCollision(void) = 0;
	virtual void				CreateLocalPhysics(void) = 0;
	virtual void				CreateLinkedPhysics(IEnginePortalServer* pRemoteCollisionEntity) = 0;
	virtual void				ClearLocalPhysics(void) = 0;
	virtual void				ClearLinkedPhysics(void) = 0;
	virtual bool				CreatedPhysicsObject(const IPhysicsObject* pObject, PS_PhysicsObjectSourceType_t* pOut_SourceType = NULL) const = 0; //true if the physics object was generated by this portal simulator
	virtual void				CreateHoleShapeCollideable() = 0;
	virtual void				ClearHoleShapeCollideable() = 0;
	virtual bool				OwnsEntity(const IServerEntity* pEntity) const = 0;
	virtual bool				OwnsPhysicsForEntity(const IServerEntity* pEntity) const = 0;
	virtual void				MarkAsOwned(IServerEntity* pEntity) = 0;
	virtual void				MarkAsReleased(IServerEntity* pEntity) = 0;
	//these three really should be made internal and the public interface changed to a "watch this entity" setup
	virtual void				TakeOwnershipOfEntity(IServerEntity* pEntity) = 0; //general ownership, not necessarily physics ownership
	virtual void				ReleaseOwnershipOfEntity(IServerEntity* pEntity, bool bMovingToLinkedSimulator = false) = 0; //if bMovingToLinkedSimulator is true, the code skips some steps that are going to be repeated when the entity is added to the other simulator
	virtual void				ReleaseAllEntityOwnership(void) = 0; //go back to not owning any entities

	virtual void				TakePhysicsOwnership(IServerEntity* pEntity) = 0;
	virtual void				ReleasePhysicsOwnership(IServerEntity* pEntity, bool bContinuePhysicsCloning = true, bool bMovingToLinkedSimulator = false) = 0;

	virtual int					GetMoveableOwnedEntities(IServerEntity** pEntsOut, int iEntOutLimit) const = 0; //gets owned entities that aren't either world or static props. Excludes fake portal ents such as physics clones

	virtual void				BeforeMove() = 0;
	virtual void				AfterMove() = 0;

	virtual void				BeforeLocalPhysicsClear() = 0;
	virtual	void				AfterLocalPhysicsCreated() = 0;
	virtual void				BeforeLinkedPhysicsClear() = 0;
	virtual void				AfterLinkedPhysicsCreated() = 0;

	virtual void				AfterCollisionEntityCreated() = 0;
	virtual void				BeforeCollisionEntityDestroy() = 0;

	virtual void				StartCloningEntity(IServerEntity* pEntity) = 0;
	virtual void				StopCloningEntity(IServerEntity* pEntity) = 0;
	virtual void				ClearLinkedEntities(void) = 0; //gets rid of transformed shadow clones
	virtual IEngineObjectServer* AsEngineObject() = 0;
	virtual const IEngineObjectServer* AsEngineObject() const = 0;
	virtual unsigned int		GetEntFlags(int entindex) = 0;
	virtual void				ClearEntFlags(int entindex) = 0;
	virtual bool				IsActivated() const = 0;
	virtual void				SetActivated(bool bActivated) = 0;
	virtual bool				IsPortal2() const = 0;
	virtual void				SetPortal2(bool bPortal2) = 0;
	virtual void				UpdateCorners(void) = 0;
	virtual const Vector&		GetPortalCorners(int iCorner)  const = 0;
};

class IEngineShadowCloneServer : public IEngineShadowClone {
public:
	virtual void			SetClonedEntity(IServerEntity* pEntToClone) = 0;
	virtual IServerEntity*	GetClonedEntity(void) = 0;
	virtual void			SetCloneTransformationMatrix(const matrix3x4_t& matTransform) = 0;
	virtual void			SetOwnerEnvironment(IPhysicsEnvironment* pOwnerPhysEnvironment) = 0;
	virtual IPhysicsEnvironment* GetOwnerEnvironment(void) const = 0;
	virtual bool			IsUntransformedClone(void) const = 0;
	virtual void			SetInAssumedSyncState(bool bInAssumedSyncState) = 0;
	virtual bool			IsInAssumedSyncState(void) const = 0;
	virtual void			FullSyncClonedPhysicsObjects(bool bTeleport) = 0;
	virtual void			SyncEntity(bool bPullChanges) = 0;
	//syncs to the source entity in every way possible, assumed sync does some rudimentary tests to see if the object is in sync, and if so, skips the update
	virtual void			FullSync(bool bAllowAssumedSync = false) = 0;
	//syncs just the physics objects, bPullChanges should be true when this clone should match it's source, false when it should force differences onto the source entity
	virtual void			PartialSync(bool bPullChanges) = 0;
	//given a physics object that is part of this clone, tells you which physics object in the source
	virtual IPhysicsObject* TranslatePhysicsToClonedEnt(const IPhysicsObject* pPhysics) = 0;
	virtual IEngineShadowCloneServer* GetNext() = 0;
	virtual IEngineObjectServer* AsEngineObject() = 0;
	virtual const IEngineObjectServer* AsEngineObject() const = 0;
};

enum
{
	VEHICLE_ANALOG_BIAS_NONE = 0,
	VEHICLE_ANALOG_BIAS_FORWARD,
	VEHICLE_ANALOG_BIAS_REVERSE,
};

class IEngineVehicleServer : public IEngineVehicle {
public:
	// Call Precache + Spawn from the containing entity's Precache + Spawn methods
	virtual void Spawn() = 0;
	//void SetOuter(CBaseAnimating* pOuter, CFourWheelServerVehicle* pServerVehicle);

	// Initializes the vehicle physics so we can drive it
	virtual bool Initialize(const char* pScriptName, unsigned int nVehicleType) = 0;

	virtual void Teleport(matrix3x4_t& relativeTransform) = 0;
	//virtual bool VPhysicsUpdate(IPhysicsObject* pPhysics) = 0;
	virtual bool Think() = 0;
	virtual void PlaceWheelDust(int wheelIndex, bool ignoreSpeed = false) = 0;

	// Updates the controls based on user input
	virtual void UpdateDriverControls(CUserCmd* cmd, float flFrameTime) = 0;

	// Various steering parameters
	virtual void SetThrottle(float flThrottle) = 0;
	virtual void SetMaxThrottle(float flMaxThrottle) = 0;
	virtual void SetMaxReverseThrottle(float flMaxThrottle) = 0;
	virtual void SetSteering(float flSteering, float flSteeringRate) = 0;
	virtual void SetSteeringDegrees(float flDegrees) = 0;
	virtual void SetAction(float flAction) = 0;
	virtual void TurnOn() = 0;
	virtual void TurnOff() = 0;
	virtual void ReleaseHandbrake() = 0;
	virtual void SetHandbrake(bool bBrake) = 0;
	virtual bool IsOn() const = 0;
	virtual void ResetControls() = 0;
	virtual void SetBoost(float flBoost) = 0;
	virtual bool UpdateBooster(void) = 0;
	virtual void SetHasBrakePedal(bool bHasBrakePedal) = 0;

	// Engine
	virtual void SetDisableEngine(bool bDisable) = 0;
	virtual bool IsEngineDisabled(void) = 0;

	// Enable/Disable Motion
	virtual void EnableMotion(void) = 0;
	virtual void DisableMotion(void) = 0;

	// Shared code to compute the vehicle view position
	virtual void GetVehicleViewPosition(const char* pViewAttachment, float flPitchFactor, Vector* pAbsPosition, QAngle* pAbsAngles) = 0;

	virtual int GetWheelCount() = 0;
	virtual IPhysicsObject* GetWheel(int iWheel) = 0;
	virtual const Vector& GetWheelPosition(int iWheel) = 0;
	virtual const QAngle GetWheelRotation(int iWheel) = 0;

	virtual int	GetSpeed() const = 0;
	virtual int GetMaxSpeed() const = 0;
	virtual int GetRPM() const = 0;
	virtual float GetThrottle() const = 0;
	virtual float GetBrake() const = 0;
	virtual bool HasBoost() const = 0;
	virtual int BoostTimeLeft() const = 0;
	virtual bool IsBoosting(void) = 0;
	virtual float GetHLSpeed() const = 0;
	virtual float GetSteering() const = 0;
	virtual float GetSteeringDegrees() const = 0;
	virtual IPhysicsVehicleController* GetVehicle(void) = 0;
	virtual float GetWheelBaseHeight(int wheelIndex) = 0;
	virtual float GetWheelTotalHeight(int wheelIndex) = 0;

	virtual IPhysicsVehicleController* GetVehicleController() = 0;
	virtual const vehicleparams_t& GetVehicleParams(void) = 0;
	virtual const vehicle_controlparams_t& GetVehicleControls(void) = 0;
	virtual const vehicle_operatingparams_t& GetVehicleOperatingParams(void) = 0;

};

class IEngineRopeServer : public IEngineRope {
public:
	virtual void SetStartPoint(IServerEntity* pStartPoint, int attachment = 0) = 0;
	virtual void SetEndPoint(IServerEntity* pEndPoint, int attachment = 0) = 0;
	virtual IServerEntity* GetStartPoint() = 0;
	virtual IServerEntity* GetEndPoint() = 0;
	virtual int GetRopeFlags() = 0;
	virtual void SetRopeFlags(int RopeFlags) = 0;
	virtual void SetWidth(float Width) = 0;
	virtual int GetSegments() = 0;
	virtual void SetSegments(int nSegments) = 0;
	virtual int GetLockedPoints() = 0;
	virtual void SetLockedPoints(int LockedPoints) = 0;
	virtual void SetRopeLength(int RopeLength) = 0;
	virtual const char* GetMaterialName() = 0;
	virtual void SetMaterial(const char* pName) = 0;
	virtual int GetRopeMaterialModelIndex() = 0;
	virtual void SetRopeMaterialModelIndex(int RopeMaterialModelIndex) = 0;
	virtual void EndpointsChanged() = 0;
	virtual void Init() = 0;
	virtual void NotifyPositionChanged() = 0;
	virtual void SetScrollSpeed(float flScrollSpeed) = 0;
	virtual void DetachPoint(int iPoint) = 0;
	virtual bool SetupHangDistance(float flHangDist) = 0;
	virtual void EnableWind(bool bEnable) = 0;
	virtual void SetConstrainBetweenEndpoints(bool bConstrainBetweenEndpoints) = 0;
};

class IEngineGhostServer : public IEngineGhost {
public:

};

//-----------------------------------------------------------------------------
// Entity events... targetted to a particular entity
// Each event has a well defined structure to use for parameters
//-----------------------------------------------------------------------------
enum EntityEvent_t
{
	ENTITY_EVENT_WATER_TOUCH = 0,		// No data needed
	ENTITY_EVENT_WATER_UNTOUCH,			// No data needed
	ENTITY_EVENT_PARENT_CHANGED,		// No data needed
};

// This class is how the engine talks to entities in the game DLL.
// IServerEntity implements this interface.
class IServerEntity	: public IServerUnknown
{
public:
	virtual ~IServerEntity() {}
	virtual int RequiredEdictIndex(void) = 0;
	virtual bool IsNetworkable(void) = 0;
	virtual void NetworkStateChanged() = 0;
	virtual const char* GetClassname() const = 0;
	virtual bool NameMatches(const char* pszNameOrWildcard) = 0;
	virtual bool NameMatches(string_t nameStr) = 0;
	virtual bool ClassMatches(const char* pszClassOrWildcard) = 0;
	virtual bool ClassMatches(string_t nameStr) = 0;
	virtual string_t GetEntityName() = 0;
	virtual const string_t& GetTarget() const = 0;
	virtual void SetTarget(const string_t& target) = 0;
	virtual bool HasTarget(string_t targetname) = 0;
	virtual const char* GetDebugName(void) = 0;
	virtual int& GetDebugOverlays() = 0;
	virtual TimedOverlay_t* GetTimedOverlay() = 0;
	virtual void AddTimedOverlay(const char* msg, int endTime) = 0;
	virtual void EntityText(int text_offset, const char* text, float flDuration, int r = 255, int g = 255, int b = 255, int a = 255) = 0;
	virtual void DrawOutputOverlay(CEventAction* ev) = 0;
	virtual	void DrawDebugGeometryOverlays(void) = 0;
	virtual int DrawDebugTextOverlays(void) = 0;
	virtual void DrawBBoxOverlay(float flDuration = 0.0f) = 0;
	virtual void SendDebugPivotOverlay(void) = 0;
	virtual void AddContext(const char* nameandvalue) = 0;
	virtual void DumpResponseCriteria(void) = 0;
	//can not call this in constructor!
	virtual IEngineObjectServer* GetEngineObject() = 0;
	virtual const IEngineObjectServer* GetEngineObject() const = 0;
	virtual IEnginePlayerServer* GetEnginePlayer() = 0;
	virtual const IEnginePlayerServer* GetEnginePlayer() const = 0;
	virtual IEngineWorldServer* GetEngineWorld() = 0;
	virtual const IEngineWorldServer* GetEngineWorld() const = 0;
	virtual IEnginePortalServer* GetEnginePortal() = 0;
	virtual const IEnginePortalServer* GetEnginePortal() const = 0;
	virtual IEngineShadowCloneServer* GetEngineShadowClone() = 0;
	virtual const IEngineShadowCloneServer* GetEngineShadowClone() const = 0;
	virtual IEngineVehicleServer* GetEngineVehicle() = 0;
	virtual const IEngineVehicleServer* GetEngineVehicle() const = 0;
	virtual IEngineRopeServer* GetEngineRope() = 0;
	virtual const IEngineRopeServer* GetEngineRope() const = 0;

	virtual void OnSave(IEntitySaveUtils* pSaveUtils) = 0;
	virtual int	Save(ISave& save) = 0;
	virtual void OnRestore() = 0;
	virtual int	Restore(IRestore& restore) = 0;
	virtual bool KeyValue(const char* szKeyName, const char* szValue) = 0;
	virtual bool KeyValue(const char* szKeyName, float flValue) = 0;
	virtual bool KeyValue(const char* szKeyName, const Vector& vecValue) = 0;
	virtual bool GetKeyValue(const char* szKeyName, char* szValue, int iMaxLen) = 0;
	virtual int	ObjectCaps(void) = 0;
	virtual void Spawn(void) = 0;
	virtual void Precache(void) = 0;
	virtual void Activate(void) = 0;
	virtual void PostClientActive(void) = 0;
	virtual void SUB_StartFadeOut(float delay = 10.0f, bool bNotSolid = true) = 0;
	// Delete yourself.
	virtual void Release(void) = 0;

	virtual bool IsTemplate(void) = 0;
	virtual bool IsWorld() const = 0;
	virtual bool IsBSPModel() const = 0;
	virtual	bool IsPlayer(void) const = 0;
	virtual bool IsCombatCharacter() const = 0;
	virtual bool IsNPC(void) const = 0;
	virtual bool IsNetClient(void) const = 0;
	virtual bool IsCombineBall() const = 0;
	virtual bool IsViewModel() const = 0;
	virtual bool IsPhysicsProp() const = 0;
	virtual bool IsGib() const = 0;
	virtual bool IsChangeLevelTrigger() const = 0;
	virtual const char* GetNewMapName() = 0;
	virtual const char* GetNewLandmarkName() = 0;
	virtual bool IsAlive(void) = 0;
	virtual bool IsStandable() const = 0;
	virtual bool IsMoving(void) = 0;
	virtual bool IsFloating() = 0;
	virtual bool IsNavIgnored() const = 0;
	virtual void SetNavIgnore(float duration = FLT_MAX) = 0;
	virtual void ClearNavIgnore() = 0;
	virtual bool HasNPCsOnIt() = 0;
	virtual int GetTextureFrameIndex(void) = 0;
	virtual void SetTextureFrameIndex(int iIndex) = 0;
	virtual void SetRenderMode(RenderMode_t nRenderMode) = 0;
	virtual RenderMode_t GetRenderMode() const = 0;
	virtual void SetRenderColor(color32 color) = 0;
	virtual void SetRenderColor(byte r, byte g, byte b) = 0;
	virtual void SetRenderColor(byte r, byte g, byte b, byte a) = 0;
	virtual void SetRenderColorR(byte r) = 0;
	virtual void SetRenderColorG(byte g) = 0;
	virtual void SetRenderColorB(byte b) = 0;
	virtual void SetRenderColorA(byte a) = 0;
	virtual void SetBlocksLOS(bool bBlocksLOS) = 0;
	virtual bool BlocksLOS(void) = 0;
	virtual int ShouldTransmit(const CCheckTransmitInfo* pInfo) = 0;
	virtual int GetTransmitState(void) = 0;
	virtual int SetTransmitState(int nFlag) = 0;
	virtual void SetTransmit(CCheckTransmitInfo* pInfo, bool bAlways) = 0;
	virtual void IncrementTransmitStateOwnedCounter() = 0;
	virtual void DecrementTransmitStateOwnedCounter() = 0;
	virtual int DispatchUpdateTransmitState() = 0;
	virtual void SetModel(const char* szModelName) = 0;
	virtual IStudioHdr* OnNewModel() = 0;
	virtual void OnResetSequence(int nSequence) = 0;
	virtual void StudioFrameAdvance() = 0;
	virtual bool CanSkipAnimation(void) = 0;
	virtual	void GetSkeleton(IStudioHdr* pStudioHdr, Vector pos[], Quaternion q[], int boneMask) = 0;
	virtual void CalculateIKLocks(float currentTime) = 0;
	virtual void BeforeParentChanged(IServerEntity* pNewParent, int inewAttachment = -1) = 0;
	virtual void AfterParentChanged(IServerEntity* pOldParent, int iOldAttachment = -1) = 0;
	virtual IServerEntity* GetEffectEntity() const = 0;
	virtual void OnAddEffects(int nEffects) = 0;
	virtual void OnRemoveEffects(int nEffects) = 0;
	virtual void OnSetEffects(int nEffects) = 0;
	virtual	bool ShouldCollide(int collisionGroup, int contentsMask) const = 0;
	virtual bool TestCollision(const Ray_t& ray, unsigned int mask, trace_t& trace) = 0;
	virtual	bool TestHitboxes(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr) = 0;
	virtual void AddWatcherToEntity(IServerEntity* pWatcher, int watcherType) = 0;
	virtual void RemoveWatcherFromEntity(IServerEntity* pWatcher, int watcherType) = 0;
	virtual void Teleport(const Vector* newPosition, const QAngle* newAngles, const Vector* newVelocity) = 0;
	virtual int GetPushEnumCount() = 0;
	virtual void PhysicsSimulate(void) = 0;
	virtual void Think(void) = 0;
#ifdef _DEBUG
	virtual void FunctionCheck(void* pFunction, const char* name) = 0;
#endif // _DEBUG
	virtual bool ShouldReportOverThinkLimit() = 0;
	virtual void ReportOverThinkLimit(float time) = 0;
	virtual void OnPositionChanged() = 0;
	virtual void OnAnglesChanged() = 0;
	virtual void OnAnimationChanged() = 0;
	virtual void NotifyPositionChanged() = 0;
	virtual Vector GetSoundEmissionOrigin() const = 0;
	virtual void ComputeWorldSpaceSurroundingBox(Vector* pWorldMins, Vector* pWorldMaxs) = 0;
	virtual	void RefreshCollisionBounds(void) = 0;
	virtual void OnEntityEvent(EntityEvent_t event, void* pEventData) = 0;
	virtual void NotifyVPhysicsStateChanged(IPhysicsObject* pPhysics, bool bAwake) = 0;
	virtual bool ForceVPhysicsCollide(IServerEntity* pEntity) = 0;
	virtual unsigned int PhysicsSolidMaskForEntity(void) const = 0;
	virtual void VPhysicsUpdate(IPhysicsObject* pPhysics) = 0;
	virtual void VPhysicsShadowUpdate(IPhysicsObject* pPhysics) = 0;
	virtual void UpdatePhysicsShadowToCurrentPosition(float deltaTime) = 0;
	virtual int VPhysicsTakeDamage(const CTakeDamageInfo& info) = 0;
	virtual void VPhysicsCollision(int index, gamevcollisionevent_t* pEvent) = 0;
	virtual void VPhysicsShadowCollision(int index, gamevcollisionevent_t* pEvent) = 0;
	virtual void VPhysicsFriction(IPhysicsObject* pObject, float energy, int surfaceProps, int surfacePropsHit) = 0;
	virtual void StartTouch(IServerEntity* pOther) = 0;
	virtual void Touch(IServerEntity* pOther) = 0;
	virtual void EndTouch(IServerEntity* pOther) = 0;
	virtual void Blocked(IServerEntity* pOther) = 0;
	virtual bool IsTriggered(IServerEntity* pActivator) = 0;
	virtual void StartGroundContact(IServerEntity* ground) = 0;
	virtual void EndGroundContact(IServerEntity* ground) = 0;
	virtual void GetVectors(Vector* forward, Vector* right, Vector* up) const = 0;
	virtual void GetVelocity(Vector* vVelocity, AngularImpulse* vAngVelocity = NULL) = 0;
	virtual bool FInViewCone(IServerEntity* pEntity) = 0;
	virtual bool FInViewCone(const Vector& vecSpot) = 0;
	virtual Vector EyePosition(void) = 0;
	virtual const QAngle& EyeAngles(void) = 0;
	virtual const QAngle& LocalEyeAngles(void) = 0;
	virtual Vector EarPosition(void) = 0;
	virtual Vector BodyTarget(const Vector& posSrc, bool bNoisy = true) = 0;
	virtual Vector HeadTarget(const Vector& posSrc) = 0;
	virtual const Vector& GetViewOffset() const = 0;
	virtual void ViewPunch(const QAngle& angleOffset) = 0;
	virtual const Vector& WorldSpaceCenter() const = 0;
	virtual	Vector GetStepOrigin(void) const = 0;
	virtual	QAngle GetStepAngles(void) const = 0;
	virtual const Vector& GetBaseVelocity() const = 0;
	virtual void SetBaseVelocity(const Vector& v) = 0;
	virtual void SetLocalAngularVelocity(const QAngle& vecAngVelocity) = 0;
	virtual const QAngle& GetLocalAngularVelocity() const = 0;
	virtual Vector GetSmoothedVelocity(void) = 0;
	virtual void VelocityPunch(const Vector& vecForce) = 0;
	virtual void ApplyAbsVelocityImpulse(const Vector& vecImpulse) = 0;
	virtual float GetStepHeight() const = 0;
	virtual int IsDormant(void) = 0;
	virtual void MakeDormant(void) = 0;
	virtual float GetMoveDoneTime() const = 0;
	virtual IServerEntity* GetOwnerEntity() const = 0;
	virtual void SetOwnerEntity(IServerEntity* pOwner) = 0;
	virtual IServerEntity* GetActiveWeapon() const = 0;
	virtual IServerEntity* PhysCannonGetHeldEntity() = 0;
	virtual int GetMaxHealth() const = 0;
	virtual int GetHealth() const = 0;
	virtual int TakeHealth(float flHealth, int bitsDamageType) = 0;
	virtual bool CanBeHitByMeleeAttack(IServerEntity* pAttacker) = 0;
	virtual const char& GetTakeDamage() const = 0;
	virtual void SetTakeDamage(int takedamage) = 0;
	virtual float GetAttackDamageScale(IHandleEntity* pVictim) = 0;
	virtual bool PassesDamageFilter(const CTakeDamageInfo& info) = 0;
	virtual void TakeDamage(const CTakeDamageInfo& info) = 0;
	virtual int OnTakeDamage(const CTakeDamageInfo& info) = 0;
	virtual void Event_Killed(const CTakeDamageInfo& info) = 0;
	virtual void Event_KilledOther(IServerEntity* pVictim, const CTakeDamageInfo& info) = 0;
	virtual int GetTeamNumber(void) const = 0;
	virtual const char* TeamID(void) const = 0;
	virtual void AddPoints(int score, bool bAllowNegativeScore) = 0;
	virtual void AddPointsToTeam(int score, bool bAllowNegativeScore) = 0;
	virtual IServerEntity* GetPlayerHeldEntity() = 0;
	virtual IGrabControllerServer* GetGrabController() = 0;
	virtual bool IsObjectAllowedOverhead() = 0;
	virtual bool HasPreferredCarryAnglesForPlayer(IServerEntity* pPlayer) = 0;
	virtual bool Pickup_GetPreferredCarryAngles(IServerEntity* pPlayer, const matrix3x4_t& localToWorld, QAngle& outputAnglesWorldSpace) = 0;
	virtual QAngle PreferredCarryAngles(void) = 0;
	virtual float GetCarryDistanceOffset(void) = 0;
	virtual void PickupObject(IServerEntity* pObject, bool bLimitMassAndSize = true) = 0;
	virtual void ForceDropOfCarriedPhysObjects(IServerEntity* pOnlyIfHoldindThis = NULL) = 0;
	virtual void Use(IServerEntity* pActivator, IServerEntity* pCaller, USE_TYPE useType, float value) = 0;
	virtual bool AcceptInput(const char* szInputName, IServerEntity* pActivator, IServerEntity* pCaller, variant_t Value, int outputID) = 0;
	virtual IServerVehicle* GetServerVehicle() = 0;
	virtual int GetVehicleAnalogControlBias() = 0;
	virtual void SetVehicleAnalogControlBias(int bias) = 0;
	virtual void PhysicsRelinkChildren(float dt) = 0;
	virtual bool PhysicsSplash(const Vector& centerPoint, const Vector& normal, float rawSpeed, float scaledSpeed) = 0;
	virtual void PortalSimulator_TookOwnershipOfEntity(IEnginePortalServer* pEntity) = 0;
	virtual void PortalSimulator_ReleasedOwnershipOfEntity(IEnginePortalServer* pEntity) = 0;
	virtual bool FindClosestPassableSpace(const Vector& vIndecisivePush, unsigned int fMask = MASK_SOLID) = 0;
	virtual void DispatchTraceAttack(const CTakeDamageInfo& info, const Vector& vecDir, trace_t* ptr, CDmgAccumulator* pAccumulator = NULL) = 0;
	virtual IServerEntity* EntityPhysics_CreateSolver(IServerEntity* pPhysicsBlocker, bool disableCollisions, float separationDuration) = 0;
	virtual IServerEntity* NPCPhysics_CreateSolver(IServerEntity* pPhysicsObject, bool disableCollisions, float separationDuration) = 0;
	virtual bool NPC_CheckBrushExclude(IServerEntity* pBrush) = 0;
	virtual int GetWaterLevel() const = 0;
	virtual int GetWaterType() const = 0;
	virtual void UpdateWaterState() = 0;
	static bool IsServer(void) { return true; }
};

// Derive a class from this if you want to filter entity list searches
abstract_class IEntityFindFilter
{
public:
	virtual bool ShouldFindEntity(IServerEntity * pEntity) = 0;
	virtual IServerEntity* GetFilterResult(void) = 0;
};

typedef void (*EntityCallbackFunction) (IServerEntity* pEntity);
typedef short HSOUNDSCRIPTHANDLE;

const int MAX_PUSHED_ENTITIES = 32;
struct physicspushlist_t
{
	float	localMoveTime;
	Vector	localOrigin;
	QAngle	localAngles;
	int		pushedCount;
	CBaseHandle	pushedEnts[MAX_PUSHED_ENTITIES];
	Vector	pushVec[MAX_PUSHED_ENTITIES];
};

// this is used to temporarily allow the vphysics shadow object to update the entity's position
// for entities that typically ignore those updates.
struct vphysicsupdateai_t
{
	float	startUpdateTime;
	float	stopUpdateTime;
	float	savedShadowControllerMaxSpeed;
};

enum
{
	TRANSITION_VOLUME_SCREENED_OUT = 0,
	TRANSITION_VOLUME_NOT_FOUND = 1,
	TRANSITION_VOLUME_PASSED = 2,
};

#define ROLL_CURVE_ZERO		5		// roll less than this is clamped to zero
#define ROLL_CURVE_LINEAR	45		// roll greater than this is copied out

#define PITCH_CURVE_ZERO		10	// pitch less than this is clamped to zero
#define PITCH_CURVE_LINEAR		45	// pitch greater than this is copied out

// remaps an angular variable to a 3 band function:
// 0 <= t < start :		f(t) = 0
// start <= t <= end :	f(t) = end * spline(( t-start) / (end-start) )  // s curve between clamped and linear
// end < t :			f(t) = t
inline float RemapAngleRange(float startInterval, float endInterval, float value)
{
	// Fixup the roll
	value = AngleNormalize(value);
	float absAngle = fabs(value);

	// beneath cutoff?
	if (absAngle < startInterval)
	{
		value = 0;
	}
	// in spline range?
	else if (absAngle <= endInterval)
	{
		float newAngle = SimpleSpline((absAngle - startInterval) / (endInterval - startInterval)) * endInterval;
		// grab the sign from the initial value
		if (value < 0)
		{
			newAngle *= -1;
		}
		value = newAngle;
	}
	// else leave it alone, in linear range

	return value;
}

// the tires are considered to be skidding if they have sliding velocity of 10 in/s or more
const float DEFAULT_SKID_THRESHOLD = 10.0f;

//-----------------------------------------------------------------------------
// Purpose: Exposes IClientEntity's to engine
//-----------------------------------------------------------------------------
abstract_class IServerEntityList : public IEntityList, public ISaveRestoreBlockHandler
{
public:

	virtual bool Init() = 0;
	virtual void Shutdown() = 0;

	// Level init, shutdown
	virtual void LevelInitPreEntity() = 0;
	virtual void LevelInitPostEntity() = 0;

	// The level is shutdown in two parts
	virtual void LevelShutdownPreEntity() = 0;
	virtual void LevelShutdownPostEntity() = 0;

	virtual void FrameUpdatePreEntityThink() = 0;
	virtual void FrameUpdatePostEntityThink() = 0;
	virtual void PreClientUpdate() = 0;

	virtual IPhysics* Physics() = 0;
	virtual IPhysicsEnvironment* PhysGetEnv() = 0;
	virtual IPhysicsSurfaceProps* PhysGetProps() = 0;
	virtual IPhysicsCollision* PhysGetCollision() = 0;
	virtual IPhysicsObjectPairHash* PhysGetEntityCollisionHash() = 0;
	virtual const objectparams_t& PhysGetDefaultObjectParams() = 0;
	virtual IPhysicsObject* PhysGetWorldObject() = 0;
	virtual CCallQueue& PhysGetPostSimulationQueue() = 0;
	virtual bool PhysIsFinalTick() = 0;
	virtual float PhysGetTimeScale() = 0;
	virtual bool PhysIsInCallback() = 0;
	virtual void PhysCallbackRemove(IServerEntity* pRemove) = 0;
	virtual void PhysCallbackImpulse(IPhysicsObject* pPhysicsObject, const Vector& vecCenterForce, const AngularImpulse& vecCenterTorque) = 0;
	virtual void PhysCallbackSetVelocity(IPhysicsObject* pPhysicsObject, const Vector& vecVelocity) = 0;
	virtual void PhysCallbackDamage(IServerEntity* pEntity, const CTakeDamageInfo& info) = 0;
	virtual void PhysCallbackDamage(IServerEntity* pEntity, const CTakeDamageInfo& info, gamevcollisionevent_t& event, int hurtIndex) = 0;
	virtual void PhysicsImpactSound(IServerEntity* pEntity, IPhysicsObject* pPhysObject, int channel, int surfaceProps, int surfacePropsHit, float volume, float impactSpeed) = 0;
	virtual void PhysCollisionSound(IServerEntity* pEntity, IPhysicsObject* pPhysObject, int channel, int surfaceProps, int surfacePropsHit, float deltaTime, float speed) = 0;
	virtual void PhysCleanupFrictionSounds(IHandleEntity* pEntity) = 0;
	virtual void PhysBreakSound(IServerEntity* pEntity, IPhysicsObject* pPhysObject, Vector vecOrigin) = 0;
	virtual void PhysFrictionSound(IHandleEntity* pEntity, IPhysicsObject* pObject, float energy, int surfaceProps, int surfacePropsHit) = 0;
	virtual void PhysFrictionSound(IHandleEntity* pEntity, IPhysicsObject* pObject, const char* pSoundName, HSOUNDSCRIPTHANDLE& handle, float flVolume) = 0;
	virtual void PhysSetMassCenterOverride(masscenteroverride_t & override) = 0;
	virtual void PhysGetMassCenterOverride(IServerEntity* pEntity, vcollide_t* pCollide, solid_t& solidOut) = 0;
	virtual void PhysTeleportConstrainedEntity(IServerEntity* pTeleportSource, IPhysicsObject* pObject0, IPhysicsObject* pObject1, const Vector& prevPosition, const QAngle& prevAngles, bool physicsRotate) = 0;
	virtual void PhysGetListOfPenetratingEntities(IServerEntity* pSearch, CUtlVector<IServerEntity*>& list) = 0;
	virtual bool PhysGetTriggerEvent(triggerevent_t* pEvent, IServerEntity* pTriggerEntity) = 0;
	virtual void IterateActivePhysicsEntities(EntityCallbackFunction func) = 0;
	virtual void OutputVPhysicsBudgetInfo() = 0;
	virtual void OutputVPhysicsDebugInfo(IServerEntity* pEntity) = 0;

	virtual void InstallEntityFactory(IEntityFactory* pFactory) = 0;
	virtual void UninstallEntityFactory(IEntityFactory* pFactory) = 0;
	virtual bool CanCreateEntityClass(const char* pClassName) = 0;
	virtual const char* GetMapClassName(const char* pClassName) = 0;
	virtual const char* GetDllClassName(const char* pClassName) = 0;
	virtual size_t GetEntitySize(const char* pClassName) = 0;
	virtual const char* GetCannonicalName(const char* pClassName) = 0;
	virtual void ReportEntitySizes() = 0;
	virtual void DumpEntityFactories() = 0;

	virtual const char* GetBlockName() = 0;

	virtual void PreSave(CSaveRestoreData* pSaveData) = 0;
	virtual void Save(ISave* pSave) = 0;
	virtual void WriteSaveHeaders(ISave* pSave) = 0;
	virtual void PostSave() = 0;

	virtual void PreRestore() = 0;
	virtual void ReadRestoreHeaders(IRestore* pRestore) = 0;
	virtual void Restore(IRestore* pRestore, bool createPlayers) = 0;
	virtual void PostRestore() = 0;

	virtual IServerEntity* FindLandmark(const char* pLandmarkName) = 0;
	virtual void OnChangeLevel(const char* pNewMapName, const char* pNewLandmarkName) = 0;
	virtual bool IsEntityInTransition(IServerEntity* pEntity, const char* pLandmarkName) = 0;
	virtual int InTransitionVolume(IServerEntity* pEntity, const char* pVolumeName) = 0;
	// Returns the number of entities moved across the transition
	virtual int CreateEntityTransitionList(IRestore* pRestore, int) = 0;
	// Build the list of maps adjacent to the current map
	virtual void BuildAdjacentMapList(ISave* pSave) = 0;

	virtual void AddListenerEntity(IEntityListener<IServerEntity>* pListener) = 0;
	virtual void RemoveListenerEntity(IEntityListener<IServerEntity>* pListener) = 0;

	virtual void ReserveSlot(int index) = 0;
	virtual int AllocateFreeSlot(bool bNetworkable = true, int index = -1) = 0;
	virtual IServerEntity* CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1) = 0;
	virtual void NotifyCreateEntity(IServerEntity* pEnt) = 0;
	virtual void NotifyRemoveEntity(IServerEntity* pEnt) = 0;
	virtual void NotifySpawn(IServerEntity* pEnt) = 0;
	// marks the entity for deletion so it will get removed next frame
	virtual void DestroyEntity(IHandleEntity* pEntity) = 0;
	// deletes an entity, without any delay.  Only use this when sure no pointers rely on this entity.
	virtual void DisableDestroyImmediate() = 0;
	virtual void EnableDestroyImmediate() = 0;
	virtual void DestroyEntityImmediate(IHandleEntity* pEntity) = 0;
	//virtual edict_t* GetEdict(CBaseHandle hEnt) const = 0;
	// Returns number of entities currently in use
	virtual int NumberOfEntities() = 0;
	virtual int NumberOfEdicts(void) = 0;
	virtual int NumberOfReservedEdicts(void) = 0;
	virtual int IndexOfHighestEdict(void) = 0;
	virtual void CleanupDeleteList(void) = 0;
	virtual int ResetDeleteList(void) = 0;
	virtual void Clear(void) = 0;

	virtual CBaseHandle GetNetworkableHandle(int iEntity) const = 0;
	virtual IHandleEntity* LookupEntityByNetworkIndex(int edictIndex) const = 0;
	virtual bool IsEntityPtr(void* pTest) = 0;
	virtual IEngineObjectServer* GetEngineObject(int entnum) = 0;
	virtual IEngineObjectServer* GetEngineObjectFromHandle(CBaseHandle handle) = 0;
	virtual IEngineWorldServer* GetEngineWorld() = 0;
	// Get IServerNetworkable interface for specified entity
	virtual IServerNetworkable* GetServerNetworkable(int entnum) const = 0;
	virtual IServerNetworkable* GetServerNetworkableFromHandle(CBaseHandle hEnt) const = 0;
	virtual IServerUnknown* GetServerUnknownFromHandle(CBaseHandle hEnt) const = 0;

	// returns the next entity after pCurrentEnt;  if pCurrentEnt is NULL, return the first entity
	virtual IServerEntity* NextEnt(IServerEntity* pCurrentEnt) = 0;
	virtual IServerEntity* FirstEnt() { return NextEnt(NULL); }

	// NOTE: This function is only a convenience wrapper.
	// It returns GetServerNetworkable( entnum )->GetIServerEntity().
	virtual IServerEntity* GetServerEntity(int entnum) const = 0;
	virtual IServerEntity* GetServerEntityFromHandle(CBaseHandle hEnt) const = 0;
	virtual short GetNetworkSerialNumber(int entnum) const = 0;
	virtual IServerEntity* GetBaseEntity(int entnum) const = 0;
	virtual IServerEntity* GetBaseEntityFromHandle(CBaseHandle hEnt) const = 0;
	virtual IServerEntity* GetPlayerByIndex(int playerIndex) = 0;
	virtual IServerEntity* GetLocalPlayer() = 0;

	virtual IServerEntity* FindEntityGeneric(IServerEntity* pStartEntity, const char* szName, IServerEntity* pSearchingEntity = NULL, IServerEntity* pActivator = NULL, IServerEntity* pCaller = NULL) = 0;
	virtual IServerEntity* FindEntityGenericWithin(IServerEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, IServerEntity* pSearchingEntity = NULL, IServerEntity* pActivator = NULL, IServerEntity* pCaller = NULL) = 0;
	virtual IServerEntity* FindEntityGenericNearest(const char* szName, const Vector& vecSrc, float flRadius, IServerEntity* pSearchingEntity = NULL, IServerEntity* pActivator = NULL, IServerEntity* pCaller = NULL) = 0;

	virtual IServerEntity* FindEntityByClassname(IServerEntity* pStartEntity, const char* szName) = 0;
	virtual IServerEntity* FindEntityByClassnameNearest(const char* szName, const Vector& vecSrc, float flRadius) = 0;
	virtual IServerEntity* FindEntityByClassnameWithin(IServerEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius) = 0;
	virtual IServerEntity* FindEntityByClassnameWithin(IServerEntity* pStartEntity, const char* szName, const Vector& vecMins, const Vector& vecMaxs) = 0;

	virtual IServerEntity* FindEntityByName(IServerEntity* pStartEntity, const char* szName, IServerEntity* pSearchingEntity = NULL, IServerEntity* pActivator = NULL, IServerEntity* pCaller = NULL, IEntityFindFilter* pFilter = NULL) = 0;
	virtual IServerEntity* FindEntityByName(IServerEntity* pStartEntity, string_t iszName, IServerEntity* pSearchingEntity = NULL, IServerEntity* pActivator = NULL, IServerEntity* pCaller = NULL, IEntityFindFilter* pFilter = NULL) = 0;
	virtual IServerEntity* FindEntityByNameNearest(const char* szName, const Vector& vecSrc, float flRadius, IServerEntity* pSearchingEntity = NULL, IServerEntity* pActivator = NULL, IServerEntity* pCaller = NULL) = 0;
	virtual IServerEntity* FindEntityByNameWithin(IServerEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, IServerEntity* pSearchingEntity = NULL, IServerEntity* pActivator = NULL, IServerEntity* pCaller = NULL) = 0;

	virtual IServerEntity* FindEntityByTarget(IServerEntity* pStartEntity, const char* szName) = 0;
	virtual IServerEntity* FindEntityByModel(IServerEntity* pStartEntity, const char* szModelName) = 0;
	virtual IServerEntity* FindEntityInSphere(IServerEntity* pStartEntity, const Vector& vecCenter, float flRadius) = 0;

	virtual IServerEntity* FindEntityNearestFacing(const Vector& origin, const Vector& facing, float threshold) = 0;
	virtual IServerEntity* FindEntityClassNearestFacing(const Vector& origin, const Vector& facing, float threshold, char* classname) = 0;
	virtual IServerEntity* FindEntityProcedural(const char* szName, IServerEntity* pSearchingEntity = NULL, IServerEntity* pActivator = NULL, IServerEntity* pCaller = NULL) = 0;

	virtual IServerEntity* FindEntityForward(IServerEntity* pMe, bool fHull) = 0;
	virtual IServerEntity* FindPickerEntity(IServerEntity* pPlayer) = 0;

	virtual int AimTarget_ListCount() = 0;
	virtual int AimTarget_ListCopy(IServerEntity* pList[], int listMax) = 0;
	virtual void AimTarget_ForceRepopulateList() = 0;
	virtual void SimThink_EntityChanged(IServerEntity* pEntity) = 0;
	virtual int SimThink_ListCount() = 0;
	virtual int SimThink_ListCopy(IServerEntity* pList[], int listMax) = 0;

	virtual bool IsAccurateTriggerBboxChecks() = 0;
	virtual void SetAccurateTriggerBboxChecks(bool bAccurateTriggerBboxChecks) = 0;
	virtual bool IsDisableTouchFuncs() = 0;
	virtual void SetDisableTouchFuncs(bool bDisableTouchFuncs) = 0;
	virtual bool IsDisableEhandleAccess() = 0;
	virtual void SetDisableEhandleAccess(bool bDisableEhandleAccess) = 0;
	virtual bool IsReceivedChainedUpdateOnRemove() = 0;
	virtual void SetReceivedChainedUpdateOnRemove(bool bReceivedChainedUpdateOnRemove) = 0;
	virtual int GetPredictionRandomSeed(void) = 0;
	virtual void SetPredictionRandomSeed(const CUserCmd* cmd) = 0;
	virtual IEngineObject* GetPredictionPlayer(void) = 0;
	virtual void SetPredictionPlayer(IEngineObject* player) = 0;
	virtual bool IsSimulatingOnAlternateTicks() = 0;
	virtual IServerEntity* GetPlayerHoldingEntity(IServerEntity* pEntity) = 0;
	virtual int GetPortalCount() = 0;
	virtual IEnginePortalServer* GetPortal(int index) = 0;
	virtual CCallQueue* GetPostTouchQueue() = 0;

	virtual void MoveToTopOfLRU(IServerEntity* pRagdoll, bool bImportant = false) = 0;
	virtual void SetMaxRagdollCount(int iMaxCount) = 0;
	virtual int CountRagdolls(bool bOnlySimulatingRagdolls) = 0;

	virtual bool FindOrAddVehicleScript(const char* pScriptName, vehicleparams_t* pVehicle, vehiclesounds_t* pSounds) = 0;
	virtual void FlushVehicleScripts() = 0;

	virtual void SetClientVisibilityPVS(IServerEntity* pClient, const unsigned char* pvs, int pvssize) = 0;
	virtual bool ClientPVSIsExpanded() = 0;

	virtual IServerEntity* FindClientInPVS(IServerEntity* pEdict) = 0;
	virtual IServerEntity* FindClientInVisibilityPVS(IServerEntity* pEdict) = 0;

	// This is a version which finds any clients whose PVS intersects the box
	virtual IServerEntity* FindClientInPVS(const Vector& vecBoxMins, const Vector& vecBoxMaxs) = 0;
	virtual IServerEntity* EntitiesInPVS(IServerEntity* pPVSEntity, IServerEntity* pStartingEntity) = 0;

	virtual IServerGameRules* GetGameRules() = 0;

};

extern IServerEntityList* serverEntitylist;

#define VSERVERENTITYLIST_INTERFACE_VERSION	"VServerEntityList003"

#endif // ISERVERENTITY_H
 