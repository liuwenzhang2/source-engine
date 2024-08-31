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


struct Ray_t;
class ServerClass;
class ICollideable;
class IServerNetworkable;
class Vector;
class QAngle;
struct PVSInfo_t;
class CCheckTransmitInfo;
struct matrix3x4_t;
class CGameTrace;
typedef CGameTrace trace_t;

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

typedef void (CBaseEntity::* BASEPTR)(void);

//-----------------------------------------------------------------------------
// Purpose: think contexts
//-----------------------------------------------------------------------------
struct thinkfunc_t
{
	BASEPTR		m_pfnThink;
	string_t	m_iszContext;
	int			m_nNextThinkTick;
	int			m_nLastThinkTick;

	DECLARE_SIMPLE_DATADESC();
};

class IServerEntity;

class IEngineObjectServer : public IEngineObject {
public:

	virtual IServerEntity* GetServerEntity() = 0;
	virtual CBaseEntity* GetOuter() = 0;

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
	// FIXME: Make hierarchy a member of CBaseEntity
	// or a contained private class...
	virtual void UnlinkChild(IEngineObjectServer* pChild) = 0;
	virtual void LinkChild(IEngineObjectServer* pChild) = 0;
	//virtual void ClearParent(IEngineObjectServer* pEntity) = 0;
	virtual void UnlinkAllChildren() = 0;
	virtual void UnlinkFromParent() = 0;
	virtual void TransferChildren(IEngineObjectServer* pNewParent) = 0;

	virtual IEngineObjectServer* GetMoveParent(void) = 0;
	//virtual void SetMoveParent(IEngineObjectServer* hMoveParent) = 0;
	virtual IEngineObjectServer* GetRootMoveParent() = 0;
	virtual IEngineObjectServer* FirstMoveChild(void) = 0;
	//virtual void SetFirstMoveChild(IEngineObjectServer* hMoveChild) = 0;
	virtual IEngineObjectServer* NextMovePeer(void) = 0;
	//virtual void SetNextMovePeer(IEngineObjectServer* hMovePeer) = 0;

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
	virtual bool IsInPVS(const CBaseEntity* pRecipient, const void* pvs, int pvssize) = 0;

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

	virtual BASEPTR GetPfnThink() = 0;
	virtual void SetPfnThink(BASEPTR pfnThink) = 0;
	virtual int GetIndexForThinkContext(const char* pszContext) = 0;
	virtual int RegisterThinkContext(const char* szContext) = 0;
	virtual BASEPTR	ThinkSet(BASEPTR func, float flNextThinkTime = 0, const char* szContext = NULL) = 0;
	virtual void SetNextThink(float nextThinkTime, const char* szContext = NULL) = 0;
	virtual float GetNextThink(const char* szContext = NULL) = 0;
	virtual int GetNextThinkTick(const char* szContext = NULL) = 0;
	virtual float GetLastThink(const char* szContext = NULL) = 0;
	virtual int GetLastThinkTick(const char* szContext = NULL) = 0;
	virtual void SetLastThinkTick(int iThinkTick) = 0;
	virtual bool WillThink() = 0;
	virtual int GetFirstThinkTick() = 0;	// get first tick thinking on any context
	virtual bool PhysicsRunThink(thinkmethods_t thinkMethod = THINK_FIRE_ALL_FUNCTIONS) = 0;
	virtual bool PhysicsRunSpecificThink(int nContextIndex, BASEPTR thinkFunc) = 0;
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

};

// This class is how the engine talks to entities in the game DLL.
// CBaseEntity implements this interface.
class IServerEntity	: public IServerUnknown
{
public:
	virtual					~IServerEntity() {}
	// Delete yourself.
	virtual void			Release(void) = 0;
// Previously in pev
	//virtual int			GetModelIndex( void ) const = 0;
 	//virtual string_t		GetModelName( void ) const = 0;

	//virtual void			SetModelIndex( int index ) = 0;
	//virtual PVSInfo_t*		GetPVSInfo() = 0; // get current visibilty data
	//virtual int				AreaNum() const = 0;

};

//-----------------------------------------------------------------------------
// Purpose: Exposes IClientEntity's to engine
//-----------------------------------------------------------------------------
abstract_class IServerEntityList : public IEntityList, public ISaveRestoreBlockHandler
{
public:
	virtual void ReserveSlot(int index) = 0;
	virtual int AllocateFreeSlot(bool bNetworkable = true, int index = -1) = 0;
	virtual IServerEntity* CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1) = 0;
	virtual void DestroyEntity(IHandleEntity* pEntity) = 0;
	//virtual edict_t* GetEdict(CBaseHandle hEnt) const = 0;
	virtual int NumberOfEdicts(void) = 0;
	virtual int NumberOfReservedEdicts(void) = 0;
	virtual int IndexOfHighestEdict(void) = 0;

	virtual IEngineObjectServer* GetEngineObject(int entnum) = 0;
	// Get IServerNetworkable interface for specified entity
	virtual IServerNetworkable* GetServerNetworkable(int entnum) const = 0;
	virtual IServerNetworkable* GetServerNetworkableFromHandle(CBaseHandle hEnt) const = 0;
	virtual IServerUnknown* GetServerUnknownFromHandle(CBaseHandle hEnt) const = 0;

	// NOTE: This function is only a convenience wrapper.
	// It returns GetServerNetworkable( entnum )->GetIServerEntity().
	virtual IServerEntity* GetServerEntity(int entnum) const = 0;
	virtual IServerEntity* GetServerEntityFromHandle(CBaseHandle hEnt) const = 0;
	virtual short		GetNetworkSerialNumber(int entnum) const = 0;

	// Returns number of entities currently in use
	virtual int					NumberOfEntities() = 0;

	// Returns highest index actually used
	//virtual int					GetHighestEntityIndex(void) = 0;

	// Sizes entity list to specified size
	//virtual void				SetMaxEntities(int maxents) = 0;
	//virtual int					GetMaxEntities() = 0;

	virtual const char* GetBlockName() = 0;

	virtual void PreSave(CSaveRestoreData* pSaveData) = 0;
	virtual void Save(ISave* pSave) = 0;
	virtual void WriteSaveHeaders(ISave* pSave) = 0;
	virtual void PostSave() = 0;

	virtual void PreRestore() = 0;
	virtual void ReadRestoreHeaders(IRestore* pRestore) = 0;
	virtual void Restore(IRestore* pRestore, bool createPlayers) = 0;
	virtual void PostRestore() = 0;

	// Returns the number of entities moved across the transition
	virtual int				CreateEntityTransitionList(CSaveRestoreData*, int) = 0;
	// Build the list of maps adjacent to the current map
	virtual void			BuildAdjacentMapList(void) = 0;
};

extern IServerEntityList* serverEntitylist;

#define VSERVERENTITYLIST_INTERFACE_VERSION	"VServerEntityList003"

#endif // ISERVERENTITY_H
 