//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ICLIENTENTITY_H
#define ICLIENTENTITY_H
#ifdef _WIN32
#pragma once
#endif


#include "iclientrenderable.h"
#include "iclientnetworkable.h"
#include "iclientthinkable.h"
#include "client_class.h"
#include "isaverestore.h"
#include "vcollide_parse.h"

struct Ray_t;
class CGameTrace;
typedef CGameTrace trace_t;
class CMouthInfo;
class IClientEntityInternal;
struct SpatializationInfo_t;
struct string_t;
class IPhysicsObject;
struct ragdoll_t;
class CBoneAccessor;
struct PS_SD_Static_World_StaticProps_ClippedProp_t;
struct PS_InternalData_t;
struct PS_SD_Static_SurfaceProperties_t;

class VarMapEntry_t
{
public:
	unsigned short		type;
	unsigned short		m_bNeedsToInterpolate;	// Set to false when this var doesn't
	// need Interpolate() called on it anymore.
	//void* data;
	IInterpolatedVar* watcher;
};

struct VarMapping_t
{
	VarMapping_t()
	{
		m_nInterpolatedEntries = 0;
	}

	CUtlVector< VarMapEntry_t >	m_Entries;
	int							m_nInterpolatedEntries;
	float						m_lastInterpolationTime;
};

struct clienttouchlink_t
{
	C_BaseEntity* entityTouched = NULL;
	int			touchStamp = 0;
	clienttouchlink_t* nextLink = NULL;
	clienttouchlink_t* prevLink = NULL;
	int					flags = 0;
};

//-----------------------------------------------------------------------------
// Purpose: Used for tracking many to one ground entity chains ( many ents can share a single ground entity )
//-----------------------------------------------------------------------------
struct clientgroundlink_t
{
	CBaseHandle			entity;
	clientgroundlink_t* nextLink;
	clientgroundlink_t* prevLink;
};

typedef void (C_BaseEntity::* CBASEPTR)(void);

//-----------------------------------------------------------------------------
// Purpose: think contexts
//-----------------------------------------------------------------------------
struct clientthinkfunc_t
{
	CBASEPTR	m_pfnThink;
	string_t	m_iszContext;
	int			m_nNextThinkTick;
	int			m_nLastThinkTick;
};

class IClientEntity;

class IEngineObjectClient : public IEngineObject {
public:

	virtual datamap_t* GetPredDescMap(void) = 0;
	virtual IClientEntity* GetClientEntity() = 0;
	virtual C_BaseEntity* GetOuter() = 0;
	virtual int entindex() const = 0;
	virtual void ParseMapData(IEntityMapData* mapData) = 0;
	virtual int Save(ISave& save) = 0;
	virtual int Restore(IRestore& restore) = 0;
	// NOTE: Setting the abs velocity in either space will cause a recomputation
// in the other space, so setting the abs velocity will also set the local vel
	virtual void SetAbsVelocity(const Vector& vecVelocity) = 0;
	//virtual const Vector& GetAbsVelocity() = 0;
	virtual const Vector& GetAbsVelocity() const = 0;

	// Sets abs angles, but also sets local angles to be appropriate
	virtual void SetAbsOrigin(const Vector& origin) = 0;
	//virtual const Vector& GetAbsOrigin(void) = 0;
	virtual const Vector& GetAbsOrigin(void) const = 0;

	virtual void SetAbsAngles(const QAngle& angles) = 0;
	//virtual const QAngle& GetAbsAngles(void) = 0;
	virtual const QAngle& GetAbsAngles(void) const = 0;

	virtual void SetLocalOrigin(const Vector& origin) = 0;
	virtual void SetLocalOriginDim(int iDim, vec_t flValue) = 0;
	virtual const Vector& GetLocalOrigin(void) const = 0;
	virtual const vec_t GetLocalOriginDim(int iDim) const = 0;		// You can use the X_INDEX, Y_INDEX, and Z_INDEX defines here.

	virtual void SetLocalAngles(const QAngle& angles) = 0;
	virtual void SetLocalAnglesDim(int iDim, vec_t flValue) = 0;
	virtual const QAngle& GetLocalAngles(void) const = 0;
	virtual const vec_t GetLocalAnglesDim(int iDim) const = 0;		// You can use the X_INDEX, Y_INDEX, and Z_INDEX defines here.

	virtual void SetLocalVelocity(const Vector& vecVelocity) = 0;
	//virtual const Vector& GetLocalVelocity() = 0;
	virtual const Vector& GetLocalVelocity() const = 0;

	virtual const Vector& GetPrevLocalOrigin() const = 0;
	virtual const QAngle& GetPrevLocalAngles() const = 0;

	virtual ITypedInterpolatedVar< QAngle >& GetRotationInterpolator() = 0;
	virtual ITypedInterpolatedVar< Vector >& GetOriginInterpolator() = 0;

	// Determine approximate velocity based on updates from server
	virtual void EstimateAbsVelocity(Vector& vel) = 0;
	// Computes absolute position based on hierarchy
	virtual void CalcAbsolutePosition() = 0;
	virtual void CalcAbsoluteVelocity() = 0;

	// Unlinks from hierarchy
	// Set the movement parent. Your local origin and angles will become relative to this parent.
	// If iAttachment is a valid attachment on the parent, then your local origin and angles 
	// are relative to the attachment on this entity.
	virtual void SetParent(IEngineObjectClient* pParentEntity, int iParentAttachment = 0) = 0;
	virtual void UnlinkChild(IEngineObjectClient* pChild) = 0;
	virtual void LinkChild(IEngineObjectClient* pChild) = 0;
	virtual void HierarchySetParent(IEngineObjectClient* pNewParent) = 0;
	virtual void UnlinkFromHierarchy() = 0;

	// Methods relating to traversing hierarchy
	virtual IEngineObjectClient* GetMoveParent(void) const = 0;
	//virtual void SetMoveParent(IEngineObjectClient* pMoveParent) = 0;
	virtual IEngineObjectClient* GetRootMoveParent() = 0;
	virtual IEngineObjectClient* FirstMoveChild(void) const = 0;
	//virtual void SetFirstMoveChild(IEngineObjectClient* pMoveChild) = 0;
	virtual IEngineObjectClient* NextMovePeer(void) const = 0;
	//virtual void SetNextMovePeer(IEngineObjectClient* pMovePeer) = 0;
	virtual IEngineObjectClient* MovePrevPeer(void) const = 0;
	//virtual void SetMovePrevPeer(IEngineObjectClient* pMovePrevPeer) = 0;

	virtual void ResetRgflCoordinateFrame() = 0;
	// Returns the entity-to-world transform
	//virtual matrix3x4_t& EntityToWorldTransform() = 0;
	virtual const matrix3x4_t& EntityToWorldTransform() const = 0;

	// Some helper methods that transform a point from entity space to world space + back
	virtual void EntityToWorldSpace(const Vector& in, Vector* pOut) const = 0;
	virtual void WorldToEntitySpace(const Vector& in, Vector* pOut) const = 0;

	virtual void GetVectors(Vector* forward, Vector* right, Vector* up) const = 0;

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

	virtual void AddVar(IInterpolatedVar* watcher, bool bSetup = false) = 0;
	virtual void RemoveVar(IInterpolatedVar* watcher, bool bAssert = true) = 0;
	virtual VarMapping_t* GetVarMapping() = 0;

	// Set appropriate flags and store off data when these fields are about to change
	virtual void OnLatchInterpolatedVariables(int flags) = 0;
	// For predictable entities, stores last networked value
	virtual void OnStoreLastNetworkedValue() = 0;

	virtual void Interp_SetupMappings() = 0;

	// Returns 1 if there are no more changes (ie: we could call RemoveFromInterpolationList).
	virtual int	Interp_Interpolate(float currentTime) = 0;

	virtual void Interp_RestoreToLastNetworked() = 0;
	virtual void Interp_UpdateInterpolationAmounts() = 0;
	virtual void Interp_Reset() = 0;
	virtual void Interp_HierarchyUpdateInterpolationAmounts() = 0;



	// Returns INTERPOLATE_STOP or INTERPOLATE_CONTINUE.
	// bNoMoreChanges is set to 1 if you can call RemoveFromInterpolationList on the entity.
	virtual int BaseInterpolatePart1(float& currentTime, Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int& bNoMoreChanges) = 0;
	virtual void BaseInterpolatePart2(Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int nChangeFlags) = 0;

	virtual void AllocateIntermediateData(void) = 0;
	virtual void DestroyIntermediateData(void) = 0;
	virtual void ShiftIntermediateDataForward(int slots_to_remove, int previous_last_slot) = 0;

	virtual void* GetPredictedFrame(int framenumber) = 0;
	virtual void* GetOuterPredictedFrame(int framenumber) = 0;
	virtual void* GetOriginalNetworkDataObject(void) = 0;
	virtual void* GetOuterOriginalNetworkDataObject(void) = 0;
	virtual bool IsIntermediateDataAllocated(void) const = 0;

	virtual void PreEntityPacketReceived(int commands_acknowledged) = 0;
	virtual void PostEntityPacketReceived(void) = 0;
	virtual bool PostNetworkDataReceived(int commands_acknowledged) = 0;

	enum
	{
		SLOT_ORIGINALDATA = -1,
	};

	virtual int	SaveData(const char* context, int slot, int type) = 0;
	virtual int	RestoreData(const char* context, int slot, int type) = 0;
	virtual void Clear(void) = 0;

	virtual void SetClassname(const char* className) = 0;
	virtual const string_t& GetClassname() const = 0;
	virtual IClientNetworkable* GetClientNetworkable() = 0;

	virtual const Vector& GetNetworkOrigin() const = 0;
	virtual const QAngle& GetNetworkAngles() const = 0;
	virtual IEngineObjectClient* GetNetworkMoveParent() = 0;
	virtual void SetNetworkOrigin(const Vector& org) = 0;
	virtual void SetNetworkAngles(const QAngle& ang) = 0;
	virtual void SetNetworkMoveParent(IEngineObjectClient* pMoveParent) = 0;
	virtual unsigned char GetParentAttachment() const = 0;
	virtual void AddFlag(int flags) = 0;
	virtual void RemoveFlag(int flagsToRemove) = 0;
	virtual void ToggleFlag(int flagToToggle) = 0;
	virtual int GetFlags(void) const = 0;
	virtual void ClearFlags() = 0;
	virtual int	GetEFlags() const = 0;
	virtual void SetEFlags(int iEFlags) = 0;
	virtual void AddEFlags(int nEFlagMask) = 0;
	virtual void RemoveEFlags(int nEFlagMask) = 0;
	virtual bool IsEFlagSet(int nEFlagMask) const = 0;
	virtual bool IsMarkedForDeletion(void) = 0;
	virtual int GetSpawnFlags(void) const = 0;
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
	virtual void PhysicsImpact(IEngineObjectClient* other, trace_t& trace) = 0;
	virtual void PhysicsMarkEntitiesAsTouching(IEngineObjectClient* other, trace_t& trace) = 0;
	virtual void PhysicsMarkEntitiesAsTouchingEventDriven(IEngineObjectClient* other, trace_t& trace) = 0;
	virtual clienttouchlink_t* PhysicsMarkEntityAsTouched(IEngineObjectClient* other) = 0;
	virtual void PhysicsTouch(IEngineObjectClient* pentOther) = 0;
	virtual void PhysicsStartTouch(IEngineObjectClient* pentOther) = 0;
	virtual bool IsCurrentlyTouching(void) const = 0;

	// Physics helper
	virtual void PhysicsCheckForEntityUntouch(void) = 0;
	virtual void PhysicsNotifyOtherOfUntouch(IEngineObjectClient* ent) = 0;
	virtual void PhysicsRemoveTouchedList() = 0;
	virtual void PhysicsRemoveToucher(clienttouchlink_t* link) = 0;

	virtual clientgroundlink_t* AddEntityToGroundList(IEngineObjectClient* other) = 0;
	virtual void PhysicsStartGroundContact(IEngineObjectClient* pentOther) = 0;
	virtual void PhysicsNotifyOtherOfGroundRemoval(IEngineObjectClient* ent) = 0;
	virtual void PhysicsRemoveGround(clientgroundlink_t* link) = 0;
	virtual void PhysicsRemoveGroundList() = 0;

	virtual void SetGroundEntity(IEngineObjectClient* ground) = 0;
	virtual IEngineObjectClient* GetGroundEntity(void) = 0;
	virtual IEngineObjectClient* GetGroundEntity(void) const = 0;
	virtual void SetGroundChangeTime(float flTime) = 0;
	virtual float GetGroundChangeTime(void) = 0;

	virtual void SetModelName(string_t name) = 0;
	virtual string_t GetModelName(void) const = 0;
	virtual int GetModelIndex(void) const = 0;
	virtual void SetModelIndex(int index) = 0;

	//virtual ICollideable* CollisionProp() = 0;
	//virtual const ICollideable* CollisionProp() const = 0;
	virtual ICollideable* GetCollideable() = 0;
	virtual void SetCollisionBounds(const Vector& mins, const Vector& maxs) = 0;
	virtual SolidType_t GetSolid() const = 0;
	virtual bool IsSolid() const = 0;
	virtual void SetSolid(SolidType_t val) = 0;
	virtual void AddSolidFlags(int flags) = 0;
	virtual void RemoveSolidFlags(int flags) = 0;
	//virtual void ClearSolidFlags(void) = 0;
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
	virtual int GetCollisionGroup() const = 0;
	virtual void SetCollisionGroup(int collisionGroup) = 0;
	virtual void CollisionRulesChanged() = 0;
	virtual bool IsEffectActive(int nEffectMask) const = 0;
	virtual void AddEffects(int nEffects) = 0;
	virtual void RemoveEffects(int nEffects) = 0;
	virtual int GetEffects(void) const = 0;
	virtual void ClearEffects(void) = 0;
	virtual void SetEffects(int nEffects) = 0;
	virtual void SetGravity(float flGravity) = 0;
	virtual float GetGravity(void) const = 0;
	// Sets physics parameters
	virtual void SetFriction(float flFriction) = 0;
	virtual float GetElasticity(void) const = 0;

	virtual CBASEPTR GetPfnThink() = 0;
	virtual void SetPfnThink(CBASEPTR pfnThink) = 0;
	virtual int GetIndexForThinkContext(const char* pszContext) = 0;
	virtual int RegisterThinkContext(const char* szContext) = 0;
	virtual CBASEPTR ThinkSet(CBASEPTR func, float flNextThinkTime = 0, const char* szContext = NULL) = 0;
	virtual void SetNextThink(float nextThinkTime, const char* szContext = NULL) = 0;
	virtual float GetNextThink(const char* szContext = NULL) = 0;
	virtual int GetNextThinkTick(const char* szContext = NULL) = 0;
	virtual float GetLastThink(const char* szContext = NULL) = 0;
	virtual int GetLastThinkTick(const char* szContext = NULL) = 0;
	virtual void SetLastThinkTick(int iThinkTick) = 0;
	virtual bool WillThink() = 0;
	virtual int GetFirstThinkTick() = 0;	// get first tick thinking on any context
	virtual bool PhysicsRunThink(thinkmethods_t thinkMethod = THINK_FIRE_ALL_FUNCTIONS) = 0;
	virtual bool PhysicsRunSpecificThink(int nContextIndex, CBASEPTR thinkFunc) = 0;

	virtual MoveType_t GetMoveType(void) const = 0;
	virtual MoveCollide_t GetMoveCollide(void) const = 0;
	virtual void SetMoveType(MoveType_t val, MoveCollide_t moveCollide = MOVECOLLIDE_DEFAULT) = 0;	// Set to one of the MOVETYPE_ defines.
	virtual void SetMoveCollide(MoveCollide_t val) = 0;	// Set to one of the MOVECOLLIDE_ defines.

	virtual bool IsSimulatedEveryTick() const = 0;
	virtual bool IsAnimatedEveryTick() const = 0;
	virtual void SetSimulatedEveryTick(bool sim) = 0;
	virtual void SetAnimatedEveryTick(bool anim) = 0;

	virtual void FollowEntity(IEngineObjectClient* pBaseEntity, bool bBoneMerge = true) = 0;
	virtual void StopFollowingEntity() = 0;	// will also change to MOVETYPE_NONE
	virtual bool IsFollowingEntity() = 0;
	virtual IEngineObjectClient* GetFollowedEntity() = 0;
	virtual void PreDataUpdate(DataUpdateType_t updateType) = 0;
	virtual void PostDataUpdate(DataUpdateType_t updateType) = 0;
	// This is called once per frame before any data is read in from the server.
	virtual void OnPreDataChanged(DataUpdateType_t type) = 0;
	virtual void OnDataChanged(DataUpdateType_t type) = 0;
	virtual float GetSpawnTime() const = 0;
	virtual float GetAnimTime() const = 0;
	virtual void SetAnimTime(float at) = 0;
	virtual float GetSimulationTime() const = 0;
	virtual void SetSimulationTime(float st) = 0;
	virtual float GetOldSimulationTime() const = 0;
	virtual const Vector& GetOldOrigin() = 0;
	virtual float ProxyRandomValue() const = 0;
	virtual int& DataChangeEventRef() = 0;
	virtual void MarkMessageReceived() = 0;
	// Gets the last message time
	virtual float GetLastChangeTime(int flags) = 0;
	virtual void UseClientSideAnimation() = 0;
	virtual bool IsUsingClientSideAnimation() = 0;
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
	virtual bool IsModelScaleFractional() const = 0;
	virtual bool IsModelScaled() const = 0;
	virtual void UpdateModelScale(void) = 0;
	virtual const model_t* GetModel(void) const = 0;
	virtual IStudioHdr* GetModelPtr() const = 0;
	virtual void InvalidateMdlCache() = 0;
	virtual float GetCycle() const = 0;
	virtual void SetCycle(float flCycle) = 0;
	virtual float GetPlaybackRate() = 0;
	virtual void SetPlaybackRate(float rate) = 0;
	virtual void SetReceivedSequence(void) = 0;
	virtual bool GetReceivedSequence() = 0;
	virtual int GetSequence() = 0;
	virtual void SetSequence(int nSequence) = 0;
	virtual void ResetSequence(int nSequence) = 0;
	virtual void ResetSequenceInfo() = 0;
	virtual bool IsSequenceFinished(void) = 0;
	virtual void SetSequenceFinished(bool bFinished) = 0;
	virtual bool IsSequenceLooping(IStudioHdr* pStudioHdr, int iSequence) = 0;
	virtual bool IsSequenceLooping(int iSequence) = 0;
	virtual int GetNewSequenceParity() = 0;
	virtual int GetPrevNewSequenceParity() = 0;
	virtual void SetPrevNewSequenceParity(int nPrevNewSequenceParity) = 0;
	virtual int GetResetEventsParity() = 0;
	virtual float GetSequenceCycleRate(IStudioHdr* pStudioHdr, int iSequence) = 0;
	virtual float SequenceDuration(int iSequence) = 0;
	virtual float SequenceDuration(void) = 0;
	virtual void GetSequenceLinearMotion(int iSequence, Vector* pVec) = 0;
	virtual float GetSequenceGroundSpeed(IStudioHdr* pStudioHdr, int iSequence) = 0;
	virtual float GetSequenceGroundSpeed(int iSequence) = 0;
	virtual float GetGroundSpeed() const = 0;
	virtual void SetGroundSpeed(float flGroundSpeed) = 0;
	virtual void GetBoneControllers(float controllers[MAXSTUDIOBONECTRLS]) = 0;
	virtual float SetBoneController(int iController, float flValue) = 0;
	virtual int LookupPoseParameter(IStudioHdr* pStudioHdr, const char* szName) = 0;
	virtual int LookupPoseParameter(const char* szName) = 0;
	virtual bool GetPoseParameterRange(int iPoseParameter, float& minValue, float& maxValue) = 0;
	virtual void GetPoseParameters(IStudioHdr* pStudioHdr, float poseParameter[MAXSTUDIOPOSEPARAM]) = 0;
	virtual float GetPoseParameter(int iPoseParameter) = 0;
	virtual float SetPoseParameter(IStudioHdr* pStudioHdr, const char* szName, float flValue) = 0;
	virtual float SetPoseParameter(IStudioHdr* pStudioHdr, int iParameter, float flValue) = 0;
	virtual float SetPoseParameter(const char* szName, float flValue) = 0;
	virtual float SetPoseParameter(int iParameter, float flValue) = 0;
	virtual CMouthInfo* GetMouth(void) = 0;
	virtual CMouthInfo& MouthInfo() = 0;
	virtual void ControlMouth(IStudioHdr* pStudioHdr) = 0;
	virtual bool ShouldMuzzleFlash() const = 0;
	virtual void DisableMuzzleFlash() = 0;
	virtual void DoMuzzleFlash() = 0;
	virtual void SetModelPointer(const model_t* pModel) = 0;
	virtual void UpdateRelevantInterpolatedVars() = 0;
	virtual void VPhysicsDestroyObject(void) = 0;
	virtual IPhysicsObject* VPhysicsGetObject(void) const = 0;
	virtual int VPhysicsGetObjectList(IPhysicsObject** pList, int listMax) = 0;
	virtual void VPhysicsSetObject(IPhysicsObject* pPhysics) = 0;
	virtual const Vector& WorldAlignMins() const = 0;
	virtual const Vector& WorldAlignMaxs() const = 0;
	virtual const Vector& WorldAlignSize() const = 0;
	virtual IPhysicsObject* VPhysicsInitStatic(void) = 0;
	virtual IPhysicsObject* VPhysicsInitNormal(SolidType_t solidType, int nSolidFlags, bool createAsleep, solid_t* pSolid = NULL) = 0;
	virtual IPhysicsObject* VPhysicsInitShadow(bool allowPhysicsMovement, bool allowPhysicsRotation, solid_t* pSolid = NULL) = 0;

	virtual void RagdollBone(C_BaseEntity* ent, mstudiobone_t* pbones, int boneCount, bool* boneSimulated, CBoneAccessor& pBoneToWorld) = 0;
	virtual const Vector& GetRagdollOrigin() = 0;
	virtual void GetRagdollBounds(Vector& mins, Vector& maxs) = 0;
	virtual int RagdollBoneCount() const = 0;
	virtual IPhysicsObject* GetElement(int elementNum) = 0;
	virtual void DrawWireframe(void) = 0;
	virtual void RagdollMoved(void) = 0;
	virtual void VPhysicsUpdate(IPhysicsObject* pObject) = 0;
	virtual bool TransformVectorToWorld(int boneIndex, const Vector* vTemp, Vector* vOut) = 0;
	virtual ragdoll_t* GetRagdoll(void) = 0;
	virtual void BuildRagdollBounds(C_BaseEntity* ent) = 0;
	virtual void ResetRagdollSleepAfterTime(void) = 0;
	virtual float GetLastVPhysicsUpdateTime() const = 0;
	//virtual void UnragdollBlend(IStudioHdr* hdr, Vector pos[], Quaternion q[], float currentTime) = 0;
	virtual void IgniteRagdoll(C_BaseEntity* pSource) = 0;
	virtual void TransferDissolveFrom(C_BaseEntity* pSource) = 0;
	virtual C_BaseEntity* CreateRagdollCopy() = 0;
	virtual bool InitAsClientRagdoll(const matrix3x4_t* pDeltaBones0, const matrix3x4_t* pDeltaBones1, const matrix3x4_t* pCurrentBonePosition, float boneDt, bool bFixedConstraints = false) = 0;
	virtual bool IsRagdoll() const = 0;
	virtual bool IsAboutToRagdoll() const = 0;
	virtual void Simulate() = 0;
	//virtual void CreateUnragdollInfo(C_BaseEntity* pRagdoll) = 0;
	virtual IPhysicsConstraintGroup* GetConstraintGroup() = 0;
	virtual int SelectWeightedSequence(int activity) = 0;
	virtual float GetLastBoneChangeTime() = 0;
	virtual int GetElementCount() = 0;
	virtual int GetBoneIndex(int index) = 0;
	virtual const Vector& GetRagPos(int index) = 0;
	virtual const QAngle& GetRagAngles(int index) = 0;
	virtual unsigned char GetRenderFX() const = 0;
	virtual void SetRenderFX(unsigned char nRenderFX) = 0;
};

class IEnginePortalClient {
public:
	virtual void				MoveTo(const Vector& ptCenter, const QAngle& angles) = 0;
	virtual void				UpdateLinkMatrix(IEnginePortalClient* pRemoteCollisionEntity) = 0;
	virtual bool				EntityIsInPortalHole(IEngineObjectClient* pEntity) const = 0; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	virtual bool				EntityHitBoxExtentIsInPortalHole(IEngineObjectClient* pBaseAnimating) const = 0; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	virtual bool				RayIsInPortalHole(const Ray_t& ray) const = 0; //traces a ray against the same detector for EntityIsInPortalHole(), bias is towards false positives
	virtual bool				TraceWorldBrushes(const Ray_t& ray, trace_t* pTrace) const = 0;
	virtual bool				TraceWallTube(const Ray_t& ray, trace_t* pTrace) const = 0;
	virtual bool				TraceWallBrushes(const Ray_t& ray, trace_t* pTrace) const = 0;
	virtual bool				TraceTransformedWorldBrushes(IEnginePortalClient* pRemoteCollisionEntity, const Ray_t& ray, trace_t* pTrace) const = 0;
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
	virtual void				CreateLinkedPhysics(IEnginePortalClient* pRemoteCollisionEntity) = 0;
	virtual void				ClearLocalPhysics(void) = 0;
	virtual void				ClearLinkedPhysics(void) = 0;
	virtual bool				CreatedPhysicsObject(const IPhysicsObject* pObject, PS_PhysicsObjectSourceType_t* pOut_SourceType = NULL) const = 0; //true if the physics object was generated by this portal simulator
	virtual void				CreateHoleShapeCollideable() = 0;
	virtual void				ClearHoleShapeCollideable() = 0;
};

class IEngineVehicleClient {
public:

};

//-----------------------------------------------------------------------------
// Purpose: All client entities must implement this interface.
//-----------------------------------------------------------------------------
abstract_class IClientEntity : public IClientUnknown, public IClientRenderable, public IClientNetworkable, public IClientThinkable
{
public:
	// Delete yourself.
	virtual void			Release( void ) = 0;
	
	// Network origin + angles
	//virtual const Vector&	GetAbsOrigin( void ) const = 0;
	//virtual const QAngle&	GetAbsAngles( void ) const = 0;

	//virtual CMouthInfo		*GetMouth( void ) = 0;

	// Retrieve sound spatialization info for the specified sound on this entity
	// Return false to indicate sound is not audible
	virtual bool			GetSoundSpatialization( SpatializationInfo_t& info ) = 0;
	virtual int				entindex() const { return IClientUnknown::entindex(); }
	virtual RecvTable* GetRecvTable() {
		return GetClientClass()->m_pRecvTable;
	}
	virtual ClientClass* GetClientClass() = 0;
	virtual void* GetDataTableBasePtr() { return this; }
	// This just picks one of the routes to IClientUnknown.
	virtual IClientUnknown* GetIClientUnknown() { return this; }
};

//-----------------------------------------------------------------------------
// Purpose: Exposes IClientEntity's to engine
//-----------------------------------------------------------------------------
abstract_class IClientEntityList : public IEntityList, public ISaveRestoreBlockHandler
{
public:

	virtual const char* GetBlockName() = 0;

	virtual void			PreSave(CSaveRestoreData* pSaveData) = 0;
	virtual void			Save(ISave* pSave) = 0;
	virtual void			WriteSaveHeaders(ISave* pSave) = 0;
	virtual void			PostSave() = 0;

	virtual void			PreRestore() = 0;
	virtual void			ReadRestoreHeaders(IRestore* pRestore) = 0;
	virtual void			Restore(IRestore* pRestore, bool createPlayers) = 0;
	virtual void			PostRestore() = 0;

	virtual IClientEntity* CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1) = 0;
	virtual void				DestroyEntity(IHandleEntity* pEntity) = 0;

	// Get IClientNetworkable interface for specified entity
	virtual IEngineObjectClient* GetEngineObject(int entnum) = 0;
	virtual IClientNetworkable* GetClientNetworkable(int entnum) = 0;
	virtual IClientNetworkable* GetClientNetworkableFromHandle(CBaseHandle hEnt) = 0;
	virtual IClientUnknown* GetClientUnknownFromHandle(CBaseHandle hEnt) = 0;

	// NOTE: This function is only a convenience wrapper.
	// It returns GetClientNetworkable( entnum )->GetIClientEntity().
	virtual IClientEntity* GetClientEntity(int entnum) = 0;
	virtual IClientEntity* GetClientEntityFromHandle(CBaseHandle hEnt) = 0;

	// Returns number of entities currently in use
	virtual int					NumberOfEntities(bool bIncludeNonNetworkable) = 0;

	// Returns highest index actually used
	virtual int					GetHighestEntityIndex(void) = 0;

	// Sizes entity list to specified size
	virtual void				SetMaxEntities(int maxents) = 0;
	virtual int					GetMaxEntities() = 0;
};

extern IClientEntityList* entitylist;

#define VCLIENTENTITYLIST_INTERFACE_VERSION	"VClientEntityList003"

#endif // ICLIENTENTITY_H
