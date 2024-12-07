//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef ENTITYLIST_H
#define ENTITYLIST_H

#ifdef _WIN32
#pragma once
#endif

#include "entitylist_base.h"
#include "baseentity.h"
#include "server_class.h"
#include "collisionproperty.h"
#include "collisionutils.h"
#include "datacache/imdlcache.h"
#include "tier0/vprof.h"
#include "vphysics/object_hash.h"
#include "saverestoretypes.h"
#include "gameinterface.h"
#include "vphysics/player_controller.h"
#include "ragdoll_shared.h"
#include "game/server/iservervehicle.h"
#include "bone_setup.h"
#include "usercmd.h"
#include "gamestringpool.h"
#include "util.h"
#include "debugoverlay_shared.h"

//class CBaseEntity;
// We can only ever move 512 entities across a transition
#define MAX_ENTITY 512
#define MAX_ENTITY_BYTE_COUNT	(NUM_ENT_ENTRIES >> 3)
#define DEBUG_TRANSITIONS_VERBOSE	2

//-----------------------------------------------------------------------------
// Spawnflags
//-----------------------------------------------------------------------------
#define	SF_RAGDOLLPROP_DEBRIS		0x0004
#define SF_RAGDOLLPROP_USE_LRU_RETIREMENT	0x1000
#define	SF_RAGDOLLPROP_ALLOW_DISSOLVE		0x2000	// Allow this prop to be dissolved
#define	SF_RAGDOLLPROP_MOTIONDISABLED		0x4000
#define	SF_RAGDOLLPROP_ALLOW_STRETCH		0x8000
#define	SF_RAGDOLLPROP_STARTASLEEP			0x10000

enum
{
	NUM_POSEPAREMETERS = 24,
	NUM_BONECTRLS = 4
};

extern IVEngineServer* engine;
//extern CUtlVector<IServerNetworkable*> g_DeleteList;
extern void PhysOnCleanupDeleteList();
extern CServerGameDLL g_ServerGameDLL;
extern bool TestEntityTriggerIntersection_Accurate(IEngineObjectServer* pTrigger, IEngineObjectServer* pEntity);
extern ISaveRestoreBlockHandler* GetPhysSaveRestoreBlockHandler();
extern ISaveRestoreBlockHandler* GetAISaveRestoreBlockHandler();

abstract_class CBaseEntityClassList
{
public:
	CBaseEntityClassList();
	~CBaseEntityClassList();
	virtual void LevelShutdownPostEntity() = 0;

	CBaseEntityClassList *m_pNextClassList;
};

class CAimTargetManager : public IEntityListener<CBaseEntity>
{
public:
	// Called by CEntityListSystem
	void LevelInitPreEntity();
	void LevelShutdownPostEntity();
	void Clear();
	void ForceRepopulateList();
	bool ShouldAddEntity(CBaseEntity* pEntity);
	// IEntityListener
	virtual void OnEntityCreated(CBaseEntity* pEntity);
	virtual void OnEntityDeleted(CBaseEntity* pEntity);
	void AddEntity(CBaseEntity* pEntity);
	void RemoveEntity(CBaseEntity* pEntity);
	int ListCount();
	int ListCopy(CBaseEntity* pList[], int listMax);

private:
	CUtlVector<CBaseEntity*>	m_targetList;
};

template< class T >
class CEntityClassList : public CBaseEntityClassList
{
public:
	virtual void LevelShutdownPostEntity()  { m_pClassList = NULL; }

	void Insert( T *pEntity )
	{
		pEntity->m_pNext = m_pClassList;
		m_pClassList = pEntity;
	}

	void Remove( T *pEntity )
	{
		T **pPrev = &m_pClassList;
		T *pCur = *pPrev;
		while ( pCur )
		{
			if ( pCur == pEntity )
			{
				*pPrev = pCur->m_pNext;
				return;
			}
			pPrev = &pCur->m_pNext;
			pCur = *pPrev;
		}
	}

	static T *m_pClassList;
};

// Derive a class from this if you want to filter entity list searches
abstract_class IEntityFindFilter
{
public:
	virtual bool ShouldFindEntity( CBaseEntity *pEntity ) = 0;
	virtual CBaseEntity *GetFilterResult( void ) = 0;
};


class CEngineObjectInternal;

class CEngineObjectNetworkProperty : public CServerNetworkProperty {
public:
	void Init(CEngineObjectInternal* pEntity);

	int			entindex() const;
	SendTable* GetSendTable();
	ServerClass* GetServerClass();
	void* GetDataTableBasePtr();

private:
	CEngineObjectInternal* m_pOuter = NULL;;
};

class CEngineObjectInternal : public IEngineObjectServer {
public:
	DECLARE_CLASS_NOBASE(CEngineObjectInternal);
	//DECLARE_EMBEDDED_NETWORKVAR();
	// data description
	DECLARE_DATADESC();

	DECLARE_SERVERCLASS();

	void* operator new(size_t stAllocateBlock);
	void* operator new(size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine);
	void operator delete(void* pMem);
	void operator delete(void* pMem, int nBlockUse, const char* pFileName, int nLine) { operator delete(pMem); }

	CEngineObjectInternal() {
		SetIdentityMatrix(m_rgflCoordinateFrame);
		m_Network.Init(this);
		m_Collision.Init(this);
		testNetwork = 9999;
		m_vecOrigin = Vector(0, 0, 0);
		m_angRotation = QAngle(0, 0, 0);
		m_vecVelocity = Vector(0, 0, 0);
		m_hMoveParent = NULL;
		m_iClassname = NULL_STRING;
		m_iGlobalname = NULL_STRING;
		m_iParent = NULL_STRING;
		m_iName = NULL_STRING;
		m_iParentAttachment = 0;
		m_fFlags = 0;
		m_iEFlags = 0;
		// NOTE: THIS MUST APPEAR BEFORE ANY SetMoveType() or SetNextThink() calls
		AddEFlags(EFL_NO_THINK_FUNCTION | EFL_NO_GAME_PHYSICS_SIMULATION | EFL_USE_PARTITION_WHEN_NOT_SOLID);
#ifndef _XBOX
		AddEFlags(EFL_USE_PARTITION_WHEN_NOT_SOLID);
#endif
		touchStamp = 0;
		SetCheckUntouch(false);
		m_fDataObjectTypes = 0;
		m_ModelName = NULL_STRING;
		m_nModelIndex = 0;
		SetSolid(SOLID_NONE);
		ClearSolidFlags();
		m_CollisionGroup = COLLISION_GROUP_NONE;
		m_flElasticity = 1.0f;
		SetFriction(1.0f);
		m_nLastThinkTick = gpGlobals->tickcount;
		SetMoveType(MOVETYPE_NONE);
		m_rgflCoordinateFrame[0][0] = 1.0f;
		m_rgflCoordinateFrame[1][1] = 1.0f;
		m_rgflCoordinateFrame[2][2] = 1.0f;
		m_bClientSideAnimation = false;
		m_vecForce.GetForModify().Init();
		m_nForceBone = 0;
		m_nSkin = 0;
		m_nBody = 0;
		m_nHitboxSet = 0;
		m_flModelScale = 1.0f;
		m_pStudioHdr = NULL;
		m_nNewSequenceParity = 0;
		m_nResetEventsParity = 0;
		m_flSpeedScale = 1.0f;
		m_pPhysicsObject = NULL;
		m_ragdoll.listCount = 0;
		m_allAsleep = false;
		m_lastUpdateTickCount = -1;
		m_anglesOverrideString = NULL_STRING;
		m_pIk = NULL;
		m_iIKCounter = 0;
		m_fBoneCacheFlags = 0;
		m_bAlternateSorting = false;
	}

	virtual ~CEngineObjectInternal()
	{
		engine->CleanUpEntityClusterList(&m_PVSInfo);
		UnlockStudioHdr();
		ClearRagdoll();
		VPhysicsDestroyObject();
		delete m_pIk;
	}

	void Init(CBaseEntity* pOuter) {
		m_pOuter = pOuter;
		m_PVSInfo.m_nClusterCount = 0;
		m_bPVSInfoDirty = true;
#ifdef _DEBUG
		m_vecVelocity.Init();
		m_vecAbsVelocity.Init();
		m_iCurrentThinkContext = NO_THINK_CONTEXT;
#endif
		SetCollisionBounds(vec3_origin, vec3_origin);
	}

	IServerEntity* GetServerEntity() {
		return m_pOuter;
	}

	CBaseEntity* GetOuter() {
		return m_pOuter;
	}

	int	entindex() const {
		return m_Network.entindex();
	}

	// Verifies that the data description is valid in debug builds.
#ifdef _DEBUG
	void ValidateDataDescription(void);
#endif // _DEBUG
	void ParseMapData(IEntityMapData* mapData);
	bool KeyValue(const char* szKeyName, const char* szValue);
	int	Save(ISave& save);
	int	Restore(IRestore& restore);
	// handler to reset stuff before you are restored
	// NOTE: Always chain to base class when implementing this!
	void OnSave(IEntitySaveUtils* pSaveUtils);
	void OnRestore();

	void SetAbsVelocity(const Vector& vecVelocity);
	const Vector& GetAbsVelocity();
	const Vector& GetAbsVelocity() const;
	// NOTE: Setting the abs origin or angles will cause the local origin + angles to be set also
	void SetAbsOrigin(const Vector& origin);
	const Vector& GetAbsOrigin(void);
	const Vector& GetAbsOrigin(void) const;

	void SetAbsAngles(const QAngle& angles);
	const QAngle& GetAbsAngles(void);
	const QAngle& GetAbsAngles(void) const;

	// Origin and angles in local space ( relative to parent )
	// NOTE: Setting the local origin or angles will cause the abs origin + angles to be set also
	void SetLocalOrigin(const Vector& origin);
	const Vector& GetLocalOrigin(void) const;

	void SetLocalAngles(const QAngle& angles);
	const QAngle& GetLocalAngles(void) const;

	void SetLocalVelocity(const Vector& vecVelocity);
	const Vector& GetLocalVelocity() const;

	void CalcAbsolutePosition();
	void CalcAbsoluteVelocity();

	CEngineObjectInternal* GetMoveParent(void);
	void SetMoveParent(IEngineObjectServer* hMoveParent);
	CEngineObjectInternal* GetRootMoveParent();
	CEngineObjectInternal* FirstMoveChild(void);
	void SetFirstMoveChild(IEngineObjectServer* hMoveChild);
	CEngineObjectInternal* NextMovePeer(void);
	void SetNextMovePeer(IEngineObjectServer* hMovePeer);

	void ResetRgflCoordinateFrame();
	// Returns the entity-to-world transform
	matrix3x4_t& EntityToWorldTransform();
	const matrix3x4_t& EntityToWorldTransform() const;

	// Some helper methods that transform a point from entity space to world space + back
	void EntityToWorldSpace(const Vector& in, Vector* pOut) const;
	void WorldToEntitySpace(const Vector& in, Vector* pOut) const;

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

	void GetVectors(Vector* forward, Vector* right, Vector* up) const;

	// Set the movement parent. Your local origin and angles will become relative to this parent.
	// If iAttachment is a valid attachment on the parent, then your local origin and angles 
	// are relative to the attachment on this entity. If iAttachment == -1, it'll preserve the
	// current m_iParentAttachment.
	void SetParent(IEngineObjectServer* pNewParent, int iAttachment = -1);
	// FIXME: Make hierarchy a member of CBaseEntity
	// or a contained private class...
	void UnlinkChild(IEngineObjectServer* pChild);
	void LinkChild(IEngineObjectServer* pChild);
	//virtual void ClearParent(IEngineObjectServer* pEntity);
	void UnlinkAllChildren();
	void UnlinkFromParent();
	void TransferChildren(IEngineObjectServer* pNewParent);

	int AreaNum() const;
	PVSInfo_t* GetPVSInfo();

	// This version does a PVS check which also checks for connected areas
	bool IsInPVS(const CCheckTransmitInfo* pInfo);

	// This version doesn't do the area check
	bool IsInPVS(const CBaseEntity* pRecipient, const void* pvs, int pvssize);

	// Recomputes PVS information
	void RecomputePVSInformation();
	// Marks the PVS information dirty
	void MarkPVSInformationDirty();

	void SetClassname(const char* className)
	{
		m_iClassname = AllocPooledString(className);
	}
	const char* GetClassName() const
	{
		return STRING(m_iClassname);
	}
	const string_t& GetClassname() const
	{
		return m_iClassname;
	}
	void SetGlobalname(const char* iGlobalname) 
	{
		m_iGlobalname = AllocPooledString(iGlobalname);
	}
	const string_t& GetGlobalname() const
	{
		return m_iGlobalname;
	}
	void SetParentName(const char* parentName)
	{
		m_iParent = AllocPooledString(parentName);
	}
	string_t& GetParentName() 
	{
		return m_iParent;
	}
	void SetName(const char* newName)
	{
		m_iName = AllocPooledString(newName);
	}
	string_t& GetEntityName()
	{
		return m_iName;
	}

	bool NameMatches(const char* pszNameOrWildcard);
	bool ClassMatches(const char* pszClassOrWildcard);
	bool NameMatches(string_t nameStr);
	bool ClassMatches(string_t nameStr);

	CEngineObjectNetworkProperty* NetworkProp();
	const CEngineObjectNetworkProperty* NetworkProp() const;
	IServerNetworkable* GetNetworkable();

	int	 GetParentAttachment();
	void ClearParentAttachment();

	void AddFlag(int flags);
	void RemoveFlag(int flagsToRemove);
	void ToggleFlag(int flagToToggle);
	int GetFlags(void) const;
	void ClearFlags(void);
	int GetEFlags() const;
	void SetEFlags(int iEFlags);
	void AddEFlags(int nEFlagMask);
	void RemoveEFlags(int nEFlagMask);
	bool IsEFlagSet(int nEFlagMask) const;
	// Marks for deletion
	void MarkForDeletion();
	// checks to see if the entity is marked for deletion
	bool IsMarkedForDeletion(void);
	bool IsMarkedForDeletion() const;
	int GetSpawnFlags(void) const;
	void SetSpawnFlags(int nFlags);
	void AddSpawnFlags(int nFlags);
	void RemoveSpawnFlags(int nFlags);
	void ClearSpawnFlags(void);
	bool HasSpawnFlags(int nFlags) const;

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

	virtual void OnPositionChanged();
	virtual void OnAnglesChanged();
	virtual void OnAnimationChanged();
	// Invalidates the abs state of all children
	void InvalidatePhysicsRecursive(int nChangeFlags);

	// HACKHACK:Get the trace_t from the last physics touch call (replaces the even-hackier global trace vars)
	const trace_t& GetTouchTrace(void);
	// FIXME: Should be private, but I can't make em private just yet
	void PhysicsImpact(IEngineObjectServer* other, trace_t& trace);
	void PhysicsTouchTriggers(const Vector* pPrevAbsOrigin = NULL);
	void PhysicsMarkEntitiesAsTouching(IEngineObjectServer* other, trace_t& trace);
	void PhysicsMarkEntitiesAsTouchingEventDriven(IEngineObjectServer* other, trace_t& trace);
	servertouchlink_t* PhysicsMarkEntityAsTouched(IEngineObjectServer* other);
	void PhysicsTouch(IEngineObjectServer* pentOther);
	void PhysicsStartTouch(IEngineObjectServer* pentOther);
	bool IsCurrentlyTouching(void) const;

	// Physics helper
	void PhysicsCheckForEntityUntouch(void);
	void PhysicsNotifyOtherOfUntouch(IEngineObjectServer* ent);
	void PhysicsRemoveTouchedList();
	void PhysicsRemoveToucher(servertouchlink_t* link);

	servergroundlink_t* AddEntityToGroundList(IEngineObjectServer* other);
	void PhysicsStartGroundContact(IEngineObjectServer* pentOther);
	void PhysicsNotifyOtherOfGroundRemoval(IEngineObjectServer* ent);
	void PhysicsRemoveGround(servergroundlink_t* link);
	void PhysicsRemoveGroundList();

	void SetGroundEntity(IEngineObjectServer* ground);
	CEngineObjectInternal* GetGroundEntity(void);
	CEngineObjectInternal* GetGroundEntity(void) const { return const_cast<CEngineObjectInternal*>(this)->GetGroundEntity(); }
	void SetGroundChangeTime(float flTime);
	float GetGroundChangeTime(void);
	
	string_t GetModelName(void) const;
	void SetModelName(string_t name);
	void SetModelIndex(int index);
	int GetModelIndex(void) const;

	// An inline version the game code can use
	//CCollisionProperty* CollisionProp();
	//const CCollisionProperty* CollisionProp() const;
	ICollideable* GetCollideable();
	// This defines collision bounds in OBB space
	void SetCollisionBounds(const Vector& mins, const Vector& maxs);
	SolidType_t GetSolid() const;
	bool IsSolid() const;
	void SetSolid(SolidType_t val);
	void AddSolidFlags(int flags);
	void RemoveSolidFlags(int flags);
	void ClearSolidFlags(void);
	bool IsSolidFlagSet(int flagMask) const;
	void SetSolidFlags(int flags);
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
	// Do the bounding boxes of these two intersect?
	bool Intersects(IEngineObjectServer* pOther);
	// Collision group accessors
	int GetCollisionGroup() const;
	void SetCollisionGroup(int collisionGroup);
	void CollisionRulesChanged();
	int GetEffects(void) const;
	void AddEffects(int nEffects);
	void RemoveEffects(int nEffects);
	void ClearEffects(void);
	void SetEffects(int nEffects);
	bool IsEffectActive(int nEffects) const;

	float GetGravity(void) const;
	void SetGravity(float gravity);
	float GetFriction(void) const;
	void SetFriction(float flFriction);
	void SetElasticity(float flElasticity);
	float GetElasticity(void) const;

	THINKPTR GetPfnThink();
	void SetPfnThink(THINKPTR pfnThink);
	int GetIndexForThinkContext(const char* pszContext);
	// Think functions with contexts
	int RegisterThinkContext(const char* szContext);
	THINKPTR ThinkSet(THINKPTR func, float flNextThinkTime = 0, const char* szContext = NULL);
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
	bool PhysicsRunSpecificThink(int nContextIndex, THINKPTR thinkFunc);
	void PhysicsDispatchThink(THINKPTR thinkFunc);
	// Move type / move collide
	MoveType_t GetMoveType() const;
	MoveCollide_t GetMoveCollide() const;
	void SetMoveType(MoveType_t val, MoveCollide_t moveCollide = MOVECOLLIDE_DEFAULT);
	void SetMoveCollide(MoveCollide_t val);

	void CheckStepSimulationChanged();
	bool IsSimulatedEveryTick() const;
	void SetSimulatedEveryTick(bool sim);
	bool IsAnimatedEveryTick() const;
	void SetAnimatedEveryTick(bool anim);
	// These set entity flags (EFL_*) to help optimize queries
	void CheckHasGamePhysicsSimulation();
	bool WillSimulateGamePhysics();
	bool UseStepSimulationNetworkOrigin(const Vector** out_v);
	bool UseStepSimulationNetworkAngles(const QAngle** out_a);
	// Compute network origin
	void ComputeStepSimulationNetwork(StepSimulationData* step);
	// Quick way to ask if we have a player entity as a child anywhere in our hierarchy.
	void RecalcHasPlayerChildBit();
	bool DoesHavePlayerChild();

	// These methods encapsulate MOVETYPE_FOLLOW, which became obsolete
	void FollowEntity(IEngineObjectServer* pBaseEntity, bool bBoneMerge = true);
	void StopFollowingEntity();	// will also change to MOVETYPE_NONE
	bool IsFollowingEntity();
	IEngineObjectServer* GetFollowedEntity();

	float GetAnimTime() const;
	void SetAnimTime(float at);

	float GetSimulationTime() const;
	void SetSimulationTime(float st);

	// Call this in your constructor to tell it that you will not use animtime. Then the
// interpolation will be done correctly on the client.
// This defaults to off.
	void	UseClientSideAnimation();

	// Tells whether or not we're using client-side animation. Used for controlling
	// the transmission of animtime.
	bool	IsUsingClientSideAnimation() { return m_bClientSideAnimation; }

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

	void				SetModelScale(float scale, float change_duration = 0.0f);
	float				GetModelScale() const { return m_flModelScale; }
	void				UpdateModelScale();

	const model_t* GetModel(void) const;
	void SetModelPointer(const model_t* pModel);
	IStudioHdr* GetModelPtr(void);
	void InvalidateMdlCache();
	void	ResetClientsideFrame(void);
	// Cycle access
	void SetCycle(float flCycle);
	float GetCycle() const;
	const float* GetPoseParameterArray() { return m_flPoseParameter.Base(); }
	const float* GetEncodedControllerArray() { return m_flEncodedController.Base(); }
	float GetPlaybackRate();
	void SetPlaybackRate(float rate);
	inline int GetSequence() { return m_nSequence; }
	void SetSequence(int nSequence);
	/* inline */ void ResetSequence(int nSequence);
	void ResetSequenceInfo();
	float GetGroundSpeed() const{
		return m_flGroundSpeed;
	}
	void SetGroundSpeed(float flGroundSpeed) {
		m_flGroundSpeed = flGroundSpeed;
	}
	float GetSpeedScale() {
		return m_flSpeedScale;
	}
	void SetSpeedScale(float flSpeedScale) {
		m_flSpeedScale = flSpeedScale;
	}
	bool SequenceLoops(void) { return m_bSequenceLoops; }
	bool IsSequenceFinished(void) { return m_bSequenceFinished; }
	void SetSequenceFinished(bool bFinished) {
		m_bSequenceFinished = bFinished;
	}
	inline float SequenceDuration(void) { return SequenceDuration(m_nSequence); }
	float	SequenceDuration(IStudioHdr* pStudioHdr, int iSequence);
	inline float SequenceDuration(int iSequence) { return SequenceDuration(GetModelPtr(), iSequence); }
	float	GetSequenceCycleRate(IStudioHdr* pStudioHdr, int iSequence);
	inline float	GetSequenceCycleRate(int iSequence) { return GetSequenceCycleRate(GetModelPtr(), iSequence); }
	float GetSequenceMoveDist(IStudioHdr* pStudioHdr, int iSequence);
	inline float GetSequenceMoveDist(int iSequence) { return GetSequenceMoveDist(GetModelPtr(), iSequence); }
	float GetSequenceMoveYaw(int iSequence);
	void  GetSequenceLinearMotion(int iSequence, Vector* pVec);
	bool HasMovement(int iSequence);
	float GetMovementFrame(float flDist);
	bool GetSequenceMovement(int nSequence, float fromCycle, float toCycle, Vector& deltaPosition, QAngle& deltaAngles);
	bool GetIntervalMovement(float flIntervalUsed, bool& bMoveSeqFinished, Vector& newPosition, QAngle& newAngles);
	float GetEntryVelocity(int iSequence);
	float GetExitVelocity(int iSequence);
	float GetInstantaneousVelocity(float flInterval = 0.0);
	virtual float	GetSequenceGroundSpeed(IStudioHdr* pStudioHdr, int iSequence);
	inline float GetSequenceGroundSpeed(int iSequence) { return GetSequenceGroundSpeed(GetModelPtr(), iSequence); }

	float GetLastEventCheck() {
		return m_flLastEventCheck;
	}
	void SetLastEventCheck(float flLastEventCheck) {
		m_flLastEventCheck = flLastEventCheck;
	}
	// Send a muzzle flash event to the client for this entity.
	void DoMuzzleFlash();

	int		LookupPoseParameter(IStudioHdr* pStudioHdr, const char* szName);
	int	    LookupPoseParameter(const char* szName) { return LookupPoseParameter(GetModelPtr(), szName); }
	float	GetPoseParameter(const char* szName);
	float	GetPoseParameter(int iParameter);
	float	SetPoseParameter(IStudioHdr* pStudioHdr, const char* szName, float flValue);
	float	SetPoseParameter(IStudioHdr* pStudioHdr, int iParameter, float flValue);
	float   SetPoseParameter(const char* szName, float flValue) { return SetPoseParameter(GetModelPtr(), szName, flValue); }
	float   SetPoseParameter(int iParameter, float flValue) { return SetPoseParameter(GetModelPtr(), iParameter, flValue); }
	// Return's the controller's angle/position in bone space.
	float					GetBoneController(int iController);
	// Maps the angle/position value you specify into the bone's start/end and sets the specified controller to the value.
	float					SetBoneController(int iController, float flValue);
	bool	GetPoseParameterRange(int index, float& minValue, float& maxValue);

	virtual IPhysicsObject* VPhysicsGetObject(void) const { return m_pPhysicsObject; }
	virtual int		VPhysicsGetObjectList(IPhysicsObject** pList, int listMax);
	// destroy and remove the physics object for this entity
	virtual void	VPhysicsDestroyObject(void);
	void			VPhysicsSetObject(IPhysicsObject* pPhysics);
	void			VPhysicsSwapObject(IPhysicsObject* pSwap);
	// Convenience routines to init the vphysics simulation for this object.
// This creates a static object.  Something that behaves like world geometry - solid, but never moves
	IPhysicsObject* VPhysicsInitStatic(void);

	// This creates a normal vphysics simulated object - physics determines where it goes (gravity, friction, etc)
	// and the entity receives updates from vphysics.  SetAbsOrigin(), etc do not affect the object!
	IPhysicsObject* VPhysicsInitNormal(SolidType_t solidType, int nSolidFlags, bool createAsleep, solid_t* pSolid = NULL);

	// This creates a vphysics object with a shadow controller that follows the AI
	// Move the object to where it should be and call UpdatePhysicsShadowToCurrentPosition()
	IPhysicsObject* VPhysicsInitShadow(bool allowPhysicsMovement, bool allowPhysicsRotation, solid_t* pSolid = NULL);

	// These methods return a *world-aligned* box relative to the absorigin of the entity.
	// This is used for collision purposes and is *not* guaranteed
	// to surround the entire entity's visual representation
	// NOTE: It is illegal to ask for the world-aligned bounds for
	// SOLID_BSP objects
	const Vector& WorldAlignMins() const;
	const Vector& WorldAlignMaxs() const;
	const Vector& WorldAlignSize() const;

	IPhysicsObject* GetGroundVPhysics();
	bool IsRideablePhysics(IPhysicsObject* pPhysics);

	int		SelectWeightedSequence(int activity);
	int		SelectWeightedSequence(int activity, int curSequence);
	int		SelectHeaviestSequence(int activity);

	void							ClearRagdoll();
	virtual void VPhysicsUpdate(IPhysicsObject* pPhysics);
	void InitRagdoll(const Vector& forceVector, int forceBone, const Vector& forcePos, matrix3x4_t* pPrevBones, matrix3x4_t* pBoneToWorld, float dt, int collisionGroup, bool activateRagdoll, bool bWakeRagdoll = true);

	virtual int RagdollBoneCount() const { return m_ragdoll.listCount; }
	virtual IPhysicsObject* GetElement(int elementNum);
	void RecheckCollisionFilter(void);
	void			GetAngleOverrideFromCurrentState(char* pOut, int size);
	virtual void RagdollBone(bool* boneSimulated, CBoneAccessor& pBoneToWorld);
	void UpdateNetworkDataFromVPhysics(int index);
	bool GetAllAsleep() { return m_allAsleep; }
	IPhysicsConstraintGroup* GetConstraintGroup() { return m_ragdoll.pGroup; }
	ragdoll_t* GetRagdoll(void) { return &m_ragdoll; }
	virtual bool IsRagdoll() const;


	unsigned char GetRenderFX() const { return m_nRenderFX; }
	void SetRenderFX(unsigned char nRenderFX) { m_nRenderFX = nRenderFX; }

	void SetOverlaySequence(int nOverlaySequence) { m_nOverlaySequence = nOverlaySequence; }
	const matrix3x4_t& GetBone(int iBone) const;
	matrix3x4_t& GetBoneForWrite(int iBone);
	virtual void SetupBones(matrix3x4_t* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime);
	void DrawRawSkeleton(matrix3x4_t boneToWorld[], int boneMask, bool noDepthTest = true, float duration = 0.0f, bool monocolor = false);
	virtual void GetHitboxBoneTransform(int iBone, matrix3x4_t& pBoneToWorld);
	virtual void GetHitboxBoneTransforms(const matrix3x4_t* hitboxbones[MAXSTUDIOBONES]);
	virtual void GetHitboxBonePosition(int iBone, Vector& origin, QAngle& angles);
	int  LookupBone(const char* szName);
	int	GetPhysicsBone(int boneIndex);

	void GetBoneCache(void);
	void InvalidateBoneCache();
	void InvalidateBoneCacheIfOlderThan(float deltaTime);
	int		GetBoneCacheFlags(void) { return m_fBoneCacheFlags; }
	inline void	SetBoneCacheFlags(unsigned short fFlag) { m_fBoneCacheFlags |= fFlag; }
	inline void	ClearBoneCacheFlags(unsigned short fFlag) { m_fBoneCacheFlags &= ~fFlag; }
	// also calculate IK on server? (always done on client)
	void EnableServerIK();
	void DisableServerIK();
	CIKContext* GetIk() { return m_pIk; }
	void SetIKGroundContactInfo(float minHeight, float maxHeight);
	void InitStepHeightAdjust(void);
	void UpdateStepOrigin(void);
	float GetEstIkOffset() const { return m_flEstIkOffset; }

	int GetAttachmentBone( int iAttachment );
	int LookupAttachment(const char* szName);
	virtual bool GetAttachment(int iAttachment, matrix3x4_t& attachmentToWorld);
	bool GetAttachment(int iAttachment, Vector& absOrigin, QAngle& absAngles);
	bool GetAttachment(const char* szName, Vector& absOrigin, QAngle& absAngles)
	{
		return GetAttachment(LookupAttachment(szName), absOrigin, absAngles);
	}
	void SetAlternateSorting(bool bAlternateSorting) { m_bAlternateSorting = bAlternateSorting; }
	void IncrementInterpolationFrame(); // Call this to cause a discontinuity (teleport)

public:
	// Networking related methods
	void NetworkStateChanged();
	void NetworkStateChanged(void* pVar);
	void NetworkStateChanged(unsigned short varOffset);
	void SimulationChanged();
private:
	bool NameMatchesComplex(const char* pszNameOrWildcard);
	bool ClassMatchesComplex(const char* pszClassOrWildcard);
	void LockStudioHdr();
	void UnlockStudioHdr();
	// called by all vphysics inits
	bool			VPhysicsInitSetup();
	void CalcRagdollSize(void);
	void RagdollSolveSeparation(ragdoll_t& ragdoll, IHandleEntity* pEntity);

private:

	friend class CBaseEntity;
	friend class CCollisionProperty;

	CNetworkVector(m_vecOrigin);
	CNetworkQAngle(m_angRotation);
	CNetworkVector(m_vecVelocity);
	Vector			m_vecAbsOrigin = Vector(0, 0, 0);
	QAngle			m_angAbsRotation = QAngle(0, 0, 0);
	// Global velocity
	Vector			m_vecAbsVelocity = Vector(0, 0, 0);
	CBaseEntity*	m_pOuter = NULL;

	// Our immediate parent in the movement hierarchy.
	// FIXME: clarify m_pParent vs. m_pMoveParent
	CNetworkHandle(CBaseEntity, m_hMoveParent);
	// cached child list
	CBaseHandle m_hMoveChild = NULL;
	// generated from m_pMoveParent
	CBaseHandle m_hMovePeer = NULL;
	// local coordinate frame of entity
	matrix3x4_t m_rgflCoordinateFrame;

	PVSInfo_t m_PVSInfo;
	bool m_bPVSInfoDirty = false;

	// members
	string_t m_iClassname;  // identifier for entity creation and save/restore
	string_t m_iGlobalname; // identifier for carrying entity across level transitions
	string_t m_iParent;	// the name of the entities parent; linked into m_pParent during Activate()
	string_t m_iName;	// name used to identify this entity

	CEngineObjectNetworkProperty m_Network;

	CNetworkVar(unsigned int, testNetwork);
	CNetworkVar(unsigned char, m_iParentAttachment); // 0 if we're relative to the parent's absorigin and absangles.

	// was pev->flags
	CNetworkVar(int, m_fFlags);
	int		m_iEFlags;	// entity flags EFL_*
	// FIXME: Make this private! Still too many references to do so...
	CNetworkVar(int, m_spawnflags);
	// used so we know when things are no longer touching
	int		touchStamp;
	int		m_fDataObjectTypes;

	CNetworkHandle(CBaseEntity, m_hGroundEntity);
	float			m_flGroundChangeTime; // Time that the ground entity changed

	string_t		m_ModelName;
	CNetworkVar(short, m_nModelIndex);

	CNetworkVarEmbedded(CCollisionProperty, m_Collision);
	CNetworkVar(int, m_CollisionGroup);		// used to cull collision tests
	// was pev->effects
	CNetworkVar(int, m_fEffects);

	// was pev->gravity;
	float			m_flGravity;  // rename to m_flGravityScale;
	// was pev->friction
	CNetworkVar(float, m_flFriction);
	CNetworkVar(float, m_flElasticity);
	// Think function handling
	THINKPTR						m_pfnThink;
	// was pev->nextthink
	CNetworkVar(int, m_nNextThinkTick);
	int							m_nLastThinkTick;
	CUtlVector< thinkfunc_t >	m_aThinkFunctions;
#ifdef _DEBUG
	int							m_iCurrentThinkContext;
#endif
	CNetworkVar(unsigned char, m_MoveType);		// One of the MOVETYPE_ defines.
	CNetworkVar(unsigned char, m_MoveCollide);

	CNetworkVar(bool, m_bSimulatedEveryTick);
	CNetworkVar(bool, m_bAnimatedEveryTick);

	CNetworkVar(float, m_flAnimTime);  // this is the point in time that the client will interpolate to position,angle,frame,etc.
	CNetworkVar(float, m_flSimulationTime);

	// Client-side animation (useful for looping animation objects)
	CNetworkVar(bool, m_bClientSideAnimation);
	CNetworkVar(int, m_nForceBone);
	CNetworkVector(m_vecForce);
	CNetworkVar(int, m_nSkin);
	CNetworkVar(int, m_nBody);
	CNetworkVar(int, m_nHitboxSet);

	// For making things thin during barnacle swallowing, e.g.
	CNetworkVar(float, m_flModelScale);
	CNetworkArray(float, m_flEncodedController, NUM_BONECTRLS);		// bone controller setting (0..1)
	CNetworkVar(bool, m_bClientSideFrameReset);
	CNetworkVar(float, m_flCycle);
	CNetworkArray(float, m_flPoseParameter, NUM_POSEPAREMETERS);	// must be private so manual mode works!
	// was pev->framerate
	CNetworkVar(float, m_flPlaybackRate);

	// was pev->frame
	CNetworkVar(int, m_nSequence);

	CNetworkVar(int, m_nNewSequenceParity);
	CNetworkVar(int, m_nResetEventsParity);
	// Incremented each time the entity is told to do a muzzle flash.
// The client picks up the change and draws the flash.
	CNetworkVar(unsigned char, m_nMuzzleFlashParity);
	// animation needs
	float				m_flGroundSpeed;	// computed linear movement rate for current sequence
	float				m_flSpeedScale;

	bool				m_bSequenceLoops;	// true if the sequence loops
	bool				m_bSequenceFinished;// flag set when StudioAdvanceFrame moves across a frame boundry
	float				m_flLastEventCheck;	// cycle index of when events were last checked

	const model_t* m_pModel;
	IStudioHdr* m_pStudioHdr;
	CThreadFastMutex	m_StudioHdrInitLock;

	IPhysicsObject* m_pPhysicsObject;	// pointer to the entity's physics object (vphysics.dll)
	ragdoll_t	m_ragdoll;
	CNetworkArray(Vector, m_ragPos, RAGDOLL_MAX_ELEMENTS);
	CNetworkArray(QAngle, m_ragAngles, RAGDOLL_MAX_ELEMENTS);
	unsigned int		m_lastUpdateTickCount;
	bool				m_allAsleep;
	Vector				m_ragdollMins[RAGDOLL_MAX_ELEMENTS];
	Vector				m_ragdollMaxs[RAGDOLL_MAX_ELEMENTS];
	string_t			m_anglesOverrideString;

	// was pev->renderfx
	CNetworkVar(unsigned char, m_nRenderFX);
	CNetworkVar(int, m_nOverlaySequence);

	CThreadFastMutex	m_BoneSetupMutex;

	float				m_flIKGroundContactTime;
	float				m_flIKGroundMinHeight;
	float				m_flIKGroundMaxHeight;

	float				m_flEstIkFloor; // debounced
	float				m_flEstIkOffset;
	CIKContext*			m_pIk;
	int					m_iIKCounter;
	CUtlVector< matrix3x4_t >		m_CachedBoneData;
	CBoneAccessor		m_BoneAccessor;
	float				m_flLastBoneSetupTime;
	unsigned short	m_fBoneCacheFlags;		// Used for bone cache state on model

	CNetworkVar(bool, m_bAlternateSorting);
	CNetworkVar(int, m_ubInterpolationFrame);

};

inline PVSInfo_t* CEngineObjectInternal::GetPVSInfo()
{
	return &m_PVSInfo;
}

inline int CEngineObjectInternal::AreaNum() const
{
	const_cast<CEngineObjectInternal*>(this)->RecomputePVSInformation();
	return m_PVSInfo.m_nAreaNum;
}

//-----------------------------------------------------------------------------
// Marks the PVS information dirty
//-----------------------------------------------------------------------------
inline void CEngineObjectInternal::MarkPVSInformationDirty()
{
	//if (m_entindex != -1)
	//{
	//GetTransmitState() |= FL_EDICT_DIRTY_PVS_INFORMATION;
	//}
	m_bPVSInfoDirty = true;
}

inline CEngineObjectNetworkProperty* CEngineObjectInternal::NetworkProp()
{
	return &m_Network;
}

inline const CEngineObjectNetworkProperty* CEngineObjectInternal::NetworkProp() const
{
	return &m_Network;
}

inline IServerNetworkable* CEngineObjectInternal::GetNetworkable()
{
	return &m_Network;
}

//-----------------------------------------------------------------------------
// Methods relating to networking
//-----------------------------------------------------------------------------
inline void	CEngineObjectInternal::NetworkStateChanged()
{
	NetworkProp()->NetworkStateChanged();
}


inline void	CEngineObjectInternal::NetworkStateChanged(void* pVar)
{
	// Make sure it's a semi-reasonable pointer.
	Assert((char*)pVar > (char*)this);
	Assert((char*)pVar - (char*)this < 32768);

	// Good, they passed an offset so we can track this variable's change
	// and avoid sending the whole entity.
	NetworkProp()->NetworkStateChanged((char*)pVar - (char*)this);
}

inline void	CEngineObjectInternal::NetworkStateChanged(unsigned short varOffset)
{
	// Make sure it's a semi-reasonable pointer.
	//Assert((char*)pVar > (char*)this);
	//Assert((char*)pVar - (char*)this < 32768);

	// Good, they passed an offset so we can track this variable's change
	// and avoid sending the whole entity.
	NetworkProp()->NetworkStateChanged(varOffset);
}

inline int CEngineObjectInternal::GetParentAttachment()
{
	return m_iParentAttachment;
}

inline void	CEngineObjectInternal::ClearParentAttachment() {
	m_iParentAttachment = 0;
}

inline int	CEngineObjectInternal::GetFlags(void) const
{
	return m_fFlags;
}

//-----------------------------------------------------------------------------
// EFlags
//-----------------------------------------------------------------------------
inline int CEngineObjectInternal::GetEFlags() const
{
	return m_iEFlags;
}

inline void CEngineObjectInternal::SetEFlags(int iEFlags)
{
	m_iEFlags = iEFlags;

	if (iEFlags & (EFL_FORCE_CHECK_TRANSMIT | EFL_IN_SKYBOX))
	{
		m_pOuter->DispatchUpdateTransmitState();
	}
}

inline void CEngineObjectInternal::AddEFlags(int nEFlagMask)
{
	m_iEFlags |= nEFlagMask;

	if (nEFlagMask & (EFL_FORCE_CHECK_TRANSMIT | EFL_IN_SKYBOX))
	{
		m_pOuter->DispatchUpdateTransmitState();
	}
}

inline void CEngineObjectInternal::RemoveEFlags(int nEFlagMask)
{
	m_iEFlags &= ~nEFlagMask;

	if (nEFlagMask & (EFL_FORCE_CHECK_TRANSMIT | EFL_IN_SKYBOX))
		m_pOuter->DispatchUpdateTransmitState();
}

inline bool CEngineObjectInternal::IsEFlagSet(int nEFlagMask) const
{
	return (m_iEFlags & nEFlagMask) != 0;
}

//-----------------------------------------------------------------------------
// checks to see if the entity is marked for deletion
//-----------------------------------------------------------------------------
inline bool CEngineObjectInternal::IsMarkedForDeletion(void)
{
	return (GetEFlags() & EFL_KILLME);
}

//-----------------------------------------------------------------------------
// Marks for deletion
//-----------------------------------------------------------------------------
inline void CEngineObjectInternal::MarkForDeletion()
{
	AddEFlags(EFL_KILLME);
}

inline bool CEngineObjectInternal::IsMarkedForDeletion() const
{
	return (GetEFlags() & EFL_KILLME) != 0;
}

inline int CEngineObjectInternal::GetSpawnFlags(void) const
{
	return m_spawnflags;
}

inline void CEngineObjectInternal::SetSpawnFlags(int nFlags)
{
	m_spawnflags = nFlags;
}

inline void CEngineObjectInternal::AddSpawnFlags(int nFlags)
{
	m_spawnflags |= nFlags;
}
inline void CEngineObjectInternal::RemoveSpawnFlags(int nFlags)
{
	m_spawnflags &= ~nFlags;
}

inline void CEngineObjectInternal::ClearSpawnFlags(void)
{
	m_spawnflags = 0;
}

inline bool CEngineObjectInternal::HasSpawnFlags(int nFlags) const
{
	return (m_spawnflags & nFlags) != 0;
}

inline bool CEngineObjectInternal::GetCheckUntouch() const
{
	return IsEFlagSet(EFL_CHECK_UNTOUCH);
}

inline int	CEngineObjectInternal::GetTouchStamp()
{
	return touchStamp;
}

inline void CEngineObjectInternal::ClearTouchStamp()
{
	touchStamp = 0;
}

//-----------------------------------------------------------------------------
// Model related methods
//-----------------------------------------------------------------------------
inline void CEngineObjectInternal::SetModelName(string_t name)
{
	m_ModelName = name;
	m_pOuter->DispatchUpdateTransmitState();
}

inline string_t CEngineObjectInternal::GetModelName(void) const
{
	return m_ModelName;
}

inline int CEngineObjectInternal::GetModelIndex(void) const
{
	return m_nModelIndex;
}

//-----------------------------------------------------------------------------
// An inline version the game code can use
//-----------------------------------------------------------------------------
//inline CCollisionProperty* CEngineObjectInternal::CollisionProp()
//{
//	return &m_Collision;
//}

//inline const CCollisionProperty* CEngineObjectInternal::CollisionProp() const
//{
//	return &m_Collision;
//}

inline ICollideable* CEngineObjectInternal::GetCollideable()
{
	return &m_Collision;
}

inline void CEngineObjectInternal::ClearSolidFlags(void)
{
	m_Collision.ClearSolidFlags();
}

inline void CEngineObjectInternal::RemoveSolidFlags(int flags)
{
	m_Collision.RemoveSolidFlags(flags);
}

inline void CEngineObjectInternal::AddSolidFlags(int flags)
{
	m_Collision.AddSolidFlags(flags);
}

inline int CEngineObjectInternal::GetSolidFlags(void) const
{
	return m_Collision.GetSolidFlags();
}

inline bool CEngineObjectInternal::IsSolidFlagSet(int flagMask) const
{
	return m_Collision.IsSolidFlagSet(flagMask);
}

inline bool CEngineObjectInternal::IsSolid() const
{
	return m_Collision.IsSolid();
}

inline void CEngineObjectInternal::SetSolid(SolidType_t val)
{
	m_Collision.SetSolid(val);
}

inline void CEngineObjectInternal::SetSolidFlags(int flags)
{
	m_Collision.SetSolidFlags(flags);
}

inline SolidType_t CEngineObjectInternal::GetSolid() const
{
	return m_Collision.GetSolid();
}

//-----------------------------------------------------------------------------
// Sets the collision bounds + the size
//-----------------------------------------------------------------------------
inline void CEngineObjectInternal::SetCollisionBounds(const Vector& mins, const Vector& maxs)
{
	m_Collision.SetCollisionBounds(mins, maxs);
}

inline const Vector& CEngineObjectInternal::GetCollisionOrigin() const
{
	return m_Collision.GetCollisionOrigin();
}

inline const QAngle& CEngineObjectInternal::GetCollisionAngles() const
{
	return m_Collision.GetCollisionAngles();
}

inline const Vector& CEngineObjectInternal::OBBMinsPreScaled() const
{
	return m_Collision.OBBMinsPreScaled();
}

inline const Vector& CEngineObjectInternal::OBBMaxsPreScaled() const
{
	return m_Collision.OBBMaxsPreScaled();
}

inline const Vector& CEngineObjectInternal::OBBMins() const
{
	return m_Collision.OBBMins();
}

inline const Vector& CEngineObjectInternal::OBBMaxs() const
{
	return m_Collision.OBBMaxs();
}

inline const Vector& CEngineObjectInternal::OBBSize() const 
{
	return m_Collision.OBBSize();
}

inline const Vector& CEngineObjectInternal::OBBCenter() const 
{
	return m_Collision.OBBCenter();
}

inline const Vector& CEngineObjectInternal::WorldSpaceCenter() const
{
	return m_Collision.WorldSpaceCenter();
}

inline void CEngineObjectInternal::WorldSpaceAABB(Vector* pWorldMins, Vector* pWorldMaxs) const
{
	m_Collision.WorldSpaceAABB(pWorldMins, pWorldMaxs);
}

inline void CEngineObjectInternal::WorldSpaceSurroundingBounds(Vector* pVecMins, Vector* pVecMaxs)
{
	m_Collision.WorldSpaceSurroundingBounds(pVecMins, pVecMaxs);
}

inline void CEngineObjectInternal::WorldSpaceTriggerBounds(Vector* pVecWorldMins, Vector* pVecWorldMaxs) const
{
	m_Collision.WorldSpaceTriggerBounds(pVecWorldMins, pVecWorldMaxs);
}

inline const Vector& CEngineObjectInternal::NormalizedToWorldSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.NormalizedToWorldSpace(in, pResult);
}

inline const Vector& CEngineObjectInternal::WorldToNormalizedSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.WorldToNormalizedSpace(in, pResult);
}

inline const Vector& CEngineObjectInternal::WorldToCollisionSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.WorldToCollisionSpace(in, pResult);
}

inline const Vector& CEngineObjectInternal::CollisionToWorldSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.CollisionToWorldSpace(in, pResult);
}

inline const Vector& CEngineObjectInternal::WorldDirectionToCollisionSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.WorldDirectionToCollisionSpace(in, pResult);
}

inline const Vector& CEngineObjectInternal::NormalizedToCollisionSpace(const Vector& in, Vector* pResult) const
{
	return m_Collision.NormalizedToCollisionSpace(in, pResult);
}

inline const matrix3x4_t& CEngineObjectInternal::CollisionToWorldTransform() const
{
	return m_Collision.CollisionToWorldTransform();
}

inline float CEngineObjectInternal::BoundingRadius() const
{
	return m_Collision.BoundingRadius();
}

inline float CEngineObjectInternal::BoundingRadius2D() const
{
	return m_Collision.BoundingRadius2D();
}

inline bool CEngineObjectInternal::IsPointSized() const
{
	return BoundingRadius() == 0.0f;
}

inline void CEngineObjectInternal::RandomPointInBounds(const Vector& vecNormalizedMins, const Vector& vecNormalizedMaxs, Vector* pPoint) const
{
	m_Collision.RandomPointInBounds(vecNormalizedMins, vecNormalizedMaxs, pPoint);
}

inline bool CEngineObjectInternal::IsPointInBounds(const Vector& vecWorldPt) const
{
	return m_Collision.IsPointInBounds(vecWorldPt);
}

inline void CEngineObjectInternal::UseTriggerBounds(bool bEnable, float flBloat)
{
	m_Collision.UseTriggerBounds(bEnable, flBloat);
}

inline void CEngineObjectInternal::RefreshScaledCollisionBounds(void)
{
	m_Collision.RefreshScaledCollisionBounds();
}

inline void CEngineObjectInternal::MarkPartitionHandleDirty()
{
	m_Collision.MarkPartitionHandleDirty();
}

inline bool CEngineObjectInternal::DoesRotationInvalidateSurroundingBox() const
{
	return m_Collision.DoesRotationInvalidateSurroundingBox();
}

inline void CEngineObjectInternal::MarkSurroundingBoundsDirty()
{
	m_Collision.MarkSurroundingBoundsDirty();
}

inline void CEngineObjectInternal::CalcNearestPoint(const Vector& vecWorldPt, Vector* pVecNearestWorldPt) const
{
	m_Collision.CalcNearestPoint(vecWorldPt, pVecNearestWorldPt);
}

inline void CEngineObjectInternal::SetSurroundingBoundsType(SurroundingBoundsType_t type, const Vector* pMins, const Vector* pMaxs)
{
	m_Collision.SetSurroundingBoundsType(type, pMins, pMaxs);
}

inline void CEngineObjectInternal::CreatePartitionHandle()
{
	m_Collision.CreatePartitionHandle();
}

inline void CEngineObjectInternal::DestroyPartitionHandle()
{
	m_Collision.DestroyPartitionHandle();
}

inline unsigned short CEngineObjectInternal::GetPartitionHandle() const
{
	return m_Collision.GetPartitionHandle();
}

inline float CEngineObjectInternal::CalcDistanceFromPoint(const Vector& vecWorldPt) const
{
	return m_Collision.CalcDistanceFromPoint(vecWorldPt);
}

inline bool CEngineObjectInternal::DoesVPhysicsInvalidateSurroundingBox() const
{
	return m_Collision.DoesVPhysicsInvalidateSurroundingBox();
}

inline void CEngineObjectInternal::UpdatePartition()
{
	m_Collision.UpdatePartition();
}

inline bool CEngineObjectInternal::IsBoundsDefinedInEntitySpace() const
{
	return m_Collision.IsBoundsDefinedInEntitySpace();
}

//-----------------------------------------------------------------------------
// Collision group accessors
//-----------------------------------------------------------------------------
inline int CEngineObjectInternal::GetCollisionGroup() const
{
	return m_CollisionGroup;
}

inline int CEngineObjectInternal::GetEffects(void) const
{
	return m_fEffects;
}

inline void CEngineObjectInternal::RemoveEffects(int nEffects)
{
	m_pOuter->OnRemoveEffects(nEffects);
#if !defined( CLIENT_DLL )
#ifdef HL2_EPISODIC
	if (nEffects & (EF_BRIGHTLIGHT | EF_DIMLIGHT))
	{
		// Hack for now, to avoid player emitting radius with his flashlight
		if (!m_pOuter->IsPlayer())
		{
			RemoveEntityFromDarknessCheck(this->m_pOuter);
		}
	}
#endif // HL2_EPISODIC
#endif // !CLIENT_DLL

	m_fEffects &= ~nEffects;
	if (nEffects & EF_NODRAW)
	{
#ifndef CLIENT_DLL
		MarkPVSInformationDirty();//NetworkProp()->
		m_pOuter->DispatchUpdateTransmitState();
#else
		UpdateVisibility();
#endif
	}
}

inline void CEngineObjectInternal::ClearEffects(void)
{
#if !defined( CLIENT_DLL )
#ifdef HL2_EPISODIC
	if (m_fEffects & (EF_BRIGHTLIGHT | EF_DIMLIGHT))
	{
		// Hack for now, to avoid player emitting radius with his flashlight
		if (!m_pOuter->IsPlayer())
		{
			RemoveEntityFromDarknessCheck(this->m_pOuter);
		}
	}
#endif // HL2_EPISODIC
#endif // !CLIENT_DLL

	m_fEffects = 0;
#ifndef CLIENT_DLL
	m_pOuter->DispatchUpdateTransmitState();
#else
	UpdateVisibility();
#endif
}

inline bool CEngineObjectInternal::IsEffectActive(int nEffects) const
{
	return (m_fEffects & nEffects) != 0;
}

inline void CEngineObjectInternal::SetGroundChangeTime(float flTime)
{
	m_flGroundChangeTime = flTime;
}

inline float CEngineObjectInternal::GetGroundChangeTime(void)
{
	return m_flGroundChangeTime;
}

inline float CEngineObjectInternal::GetGravity(void) const
{
	return m_flGravity;
}

inline void CEngineObjectInternal::SetGravity(float gravity)
{
	m_flGravity = gravity;
}

inline float CEngineObjectInternal::GetFriction(void) const
{
	return m_flFriction;
}

inline void CEngineObjectInternal::SetFriction(float flFriction)
{
	m_flFriction = flFriction;
}

inline void	CEngineObjectInternal::SetElasticity(float flElasticity)
{
	m_flElasticity = flElasticity;
}

inline float CEngineObjectInternal::GetElasticity(void)	const
{
	return m_flElasticity;
}

inline THINKPTR CEngineObjectInternal::GetPfnThink()
{
	return m_pfnThink;
}
inline void CEngineObjectInternal::SetPfnThink(THINKPTR pfnThink)
{
	m_pfnThink = pfnThink;
}

inline void CEngineObjectInternal::SetMoveCollide(MoveCollide_t val)
{
	m_MoveCollide = val;
}

inline MoveType_t CEngineObjectInternal::GetMoveType() const
{
	return (MoveType_t)(unsigned char)m_MoveType;
}

inline MoveCollide_t CEngineObjectInternal::GetMoveCollide() const
{
	return (MoveCollide_t)(unsigned char)m_MoveCollide;
}

inline bool CEngineObjectInternal::IsSimulatedEveryTick() const
{
	return m_bSimulatedEveryTick;
}

inline void CEngineObjectInternal::SetSimulatedEveryTick(bool sim)
{
	if (m_bSimulatedEveryTick != sim)
	{
		m_bSimulatedEveryTick = sim;
	}
}

inline bool CEngineObjectInternal::IsAnimatedEveryTick() const
{
	return m_bAnimatedEveryTick;
}

inline void CEngineObjectInternal::SetAnimatedEveryTick(bool anim)
{
	if (m_bAnimatedEveryTick != anim)
	{
		m_bAnimatedEveryTick = anim;
	}
}

inline float CEngineObjectInternal::GetAnimTime() const
{
	return m_flAnimTime;
}

inline float CEngineObjectInternal::GetSimulationTime() const
{
	return m_flSimulationTime;
}

inline void CEngineObjectInternal::SetAnimTime(float at)
{
	m_flAnimTime = at;
}

inline void CEngineObjectInternal::SetSimulationTime(float st)
{
	m_flSimulationTime = st;
}

//-----------------------------------------------------------------------------
// Purpose: return a pointer to an updated studiomdl cache cache
//-----------------------------------------------------------------------------
inline IStudioHdr* CEngineObjectInternal::GetModelPtr(void)
{
	//if ( IsDynamicModelLoading() )
	//	return NULL;

#ifdef _DEBUG
	// GetModelPtr() is often called before OnNewModel() so go ahead and set it up first chance.
	static IDataCacheSection* pModelCache = datacache->FindSection("ModelData");
	AssertOnce(pModelCache->IsFrameLocking());
#endif
	if (!m_pStudioHdr && GetModel())
	{
		LockStudioHdr();
	}
	return (m_pStudioHdr && m_pStudioHdr->IsValid()) ? m_pStudioHdr : NULL;
}

inline void CEngineObjectInternal::InvalidateMdlCache()
{
	UnlockStudioHdr();
	if (m_pStudioHdr != NULL)
	{
		m_pStudioHdr = NULL;
	}
}

//-----------------------------------------------------------------------------
// Cycle access
//-----------------------------------------------------------------------------
inline float CEngineObjectInternal::GetCycle() const
{
	return m_flCycle;
}

inline void CEngineObjectInternal::SetCycle(float flCycle)
{
	m_flCycle = flCycle;
}

inline float CEngineObjectInternal::GetPlaybackRate()
{
	return m_flPlaybackRate;
}

inline void CEngineObjectInternal::SetPlaybackRate(float rate)
{
	m_flPlaybackRate = rate;
}

//-----------------------------------------------------------------------------
// Methods relating to bounds
//-----------------------------------------------------------------------------
inline const Vector& CEngineObjectInternal::WorldAlignMins() const
{
	Assert(!IsBoundsDefinedInEntitySpace());
	Assert(GetCollisionAngles() == vec3_angle);
	return OBBMins();
}

inline const Vector& CEngineObjectInternal::WorldAlignMaxs() const
{
	Assert(!IsBoundsDefinedInEntitySpace());
	Assert(GetCollisionAngles() == vec3_angle);
	return OBBMaxs();
}

inline const Vector& CEngineObjectInternal::WorldAlignSize() const
{
	Assert(!IsBoundsDefinedInEntitySpace());
	Assert(GetCollisionAngles() == vec3_angle);
	return OBBSize();
}

inline const matrix3x4_t& CEngineObjectInternal::GetBone(int iBone) const
{
	return m_BoneAccessor.GetBone(iBone);
}

inline matrix3x4_t& CEngineObjectInternal::GetBoneForWrite(int iBone)
{
	return m_BoneAccessor.GetBoneForWrite(iBone);
}

class CEngineWorldInternal : public CEngineObjectInternal, public IEngineWorldServer {
public:

};

class CEnginePlayerInternal : public CEngineObjectInternal, public IEnginePlayerServer {
public:
	virtual void			VPhysicsDestroyObject();
	// Player Physics Shadow
	void					SetupVPhysicsShadow(const Vector& vecAbsOrigin, const Vector& vecAbsVelocity, CPhysCollide* pStandModel, const char* pStandHullName, CPhysCollide* pCrouchModel, const char* pCrouchHullName);
	IPhysicsPlayerController* GetPhysicsController() { return m_pPhysicsController; }
	void UpdateVPhysicsPosition(const Vector& position, const Vector& velocity, float secondsToArrival);
	void					SetVCollisionState(const Vector& vecAbsOrigin, const Vector& vecAbsVelocity, int collisionState);
	int GetVphysicsCollisionState() { return m_vphysicsCollisionState; }

private:
	void UpdatePhysicsShadowToPosition(const Vector& vecAbsOrigin);

private:
	IPhysicsPlayerController* m_pPhysicsController;
	IPhysicsObject* m_pShadowStand;
	IPhysicsObject* m_pShadowCrouch;
	// Player Physics Shadow
	int m_vphysicsCollisionState;
};

class CEnginePortalInternal : public CEngineObjectInternal, public IEnginePortalServer {
public:

	CEnginePortalInternal();
	~CEnginePortalInternal();
	virtual IPhysicsObject* VPhysicsGetObject(void) const;
	virtual int		VPhysicsGetObjectList(IPhysicsObject** pList, int listMax);
	void	VPhysicsDestroyObject(void);
	void				MoveTo(const Vector& ptCenter, const QAngle& angles);
	void				UpdateLinkMatrix(IEnginePortalServer* pRemoteCollisionEntity);
	bool				EntityIsInPortalHole(IEngineObjectServer* pEntity) const; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	bool				EntityHitBoxExtentIsInPortalHole(IEngineObjectServer* pBaseAnimating) const; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	bool				RayIsInPortalHole(const Ray_t& ray) const; //traces a ray against the same detector for EntityIsInPortalHole(), bias is towards false positives
	bool				TraceWorldBrushes(const Ray_t& ray, trace_t* pTrace) const;
	bool				TraceWallTube(const Ray_t& ray, trace_t* pTrace) const;
	bool				TraceWallBrushes(const Ray_t& ray, trace_t* pTrace) const;
	bool				TraceTransformedWorldBrushes(IEnginePortalServer* pRemoteCollisionEntity, const Ray_t& ray, trace_t* pTrace) const;
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
	void				CreateLinkedPhysics(IEnginePortalServer* pRemoteCollisionEntity);
	void				ClearLocalPhysics(void);
	void				ClearLinkedPhysics(void);
	bool				CreatedPhysicsObject(const IPhysicsObject* pObject, PS_PhysicsObjectSourceType_t* pOut_SourceType = NULL) const; //true if the physics object was generated by this portal simulator
	void				CreateHoleShapeCollideable();
	void				ClearHoleShapeCollideable();
private:
	PS_InternalData_t m_InternalData;
	const PS_InternalData_t& m_DataAccess;
	IPhysicsEnvironment* pPhysicsEnvironment = NULL;
};

#ifdef DEBUG_PORTAL_SIMULATION_CREATION_TIMES
#define STARTDEBUGTIMER(x) { x.Start(); }
#define STOPDEBUGTIMER(x) { x.End(); }
#define DEBUGTIMERONLY(x) x
#define CREATEDEBUGTIMER(x) CFastTimer x;
static const char* s_szTabSpacing[] = { "", "\t", "\t\t", "\t\t\t", "\t\t\t\t", "\t\t\t\t\t", "\t\t\t\t\t\t", "\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t\t\t" };
static int s_iTabSpacingIndex = 0;
static int s_iPortalSimulatorGUID = 0; //used in standalone function that have no idea what a portal simulator is
#define INCREMENTTABSPACING() ++s_iTabSpacingIndex;
#define DECREMENTTABSPACING() --s_iTabSpacingIndex;
#define TABSPACING (s_szTabSpacing[s_iTabSpacingIndex])
#else
#define STARTDEBUGTIMER(x)
#define STOPDEBUGTIMER(x)
#define DEBUGTIMERONLY(x)
#define CREATEDEBUGTIMER(x)
#define INCREMENTTABSPACING()
#define DECREMENTTABSPACING()
#define TABSPACING
#endif

struct PhysicsObjectCloneLink_t
{
	IPhysicsObject* pSource;
	IPhysicsShadowController* pShadowController;
	IPhysicsObject* pClone;
};

class CEngineShadowCloneInternal : public CEngineObjectInternal, public IEngineShadowCloneServer {
public:
	CEngineShadowCloneInternal() {
		m_matrixShadowTransform.Identity();
		m_matrixShadowTransform_Inverse.Identity();
		m_bShadowTransformIsIdentity = true;
	}

	~CEngineShadowCloneInternal() {
		m_hClonedEntity = NULL;
	}

	virtual void	VPhysicsDestroyObject(void);
	virtual int		VPhysicsGetObjectList(IPhysicsObject** pList, int listMax);

	//what entity are we cloning?
	void			SetClonedEntity(CBaseEntity* pEntToClone);
	CBaseEntity*	GetClonedEntity(void);
	void			SetCloneTransformationMatrix(const matrix3x4_t& matTransform);
	void			SetOwnerEnvironment(IPhysicsEnvironment* pOwnerPhysEnvironment) { m_pOwnerPhysEnvironment = pOwnerPhysEnvironment; }
	IPhysicsEnvironment* GetOwnerEnvironment(void) const { return m_pOwnerPhysEnvironment; }

	//is this clone occupying the exact same space as the object it's cloning?
	bool		IsUntransformedClone(void) const { return m_bShadowTransformIsIdentity; };
	void		SetInAssumedSyncState(bool bInAssumedSyncState) { m_bInAssumedSyncState = bInAssumedSyncState; }
	bool		IsInAssumedSyncState(void) const { return m_bInAssumedSyncState; }

	void			FullSyncClonedPhysicsObjects(bool bTeleport);
	void			SyncEntity(bool bPullChanges);
	//syncs to the source entity in every way possible, assumed sync does some rudimentary tests to see if the object is in sync, and if so, skips the update
	void			FullSync(bool bAllowAssumedSync = false);
	//syncs just the physics objects, bPullChanges should be true when this clone should match it's source, false when it should force differences onto the source entity
	void			PartialSync(bool bPullChanges);
	//given a physics object that is part of this clone, tells you which physics object in the source
	IPhysicsObject* TranslatePhysicsToClonedEnt(const IPhysicsObject* pPhysics);
	
private:
	EHANDLE			m_hClonedEntity; //the entity we're supposed to be cloning the physics of
	VMatrix			m_matrixShadowTransform; //all cloned coordinates and angles will be run through this matrix before being applied
	VMatrix			m_matrixShadowTransform_Inverse;

	CUtlVector<PhysicsObjectCloneLink_t> m_CloneLinks; //keeps track of which of our physics objects are linked to the source's objects
	bool			m_bShadowTransformIsIdentity; //the shadow transform doesn't update often, so we can cache this
	bool			m_bImmovable; //cloning a track train or door, something that doesn't really work on a force-based level
	bool			m_bInAssumedSyncState;

	IPhysicsEnvironment* m_pOwnerPhysEnvironment; //clones exist because of multi-environment situations
	bool			m_bShouldUpSync;

};


#define DUST_SPEED			5		// speed at which dust starts
#define REAR_AXLE			1		// indexes of axlex
#define	FRONT_AXLE			0
#define MAX_GUAGE_SPEED		100.0	// 100 mph is max speed shown on guage

#define BRAKE_MAX_VALUE				1.0f
#define BRAKE_BACK_FORWARD_SCALAR	2.0f
// in/sec to miles/hour
#define INS2MPH_SCALE	( 3600 * (1/5280.0f) * (1/12.0f) )
#define INS2MPH(x)		( (x) * INS2MPH_SCALE )
#define MPH2INS(x)		( (x) * (1/INS2MPH_SCALE) )

#define ROLL_CURVE_ZERO		5		// roll less than this is clamped to zero
#define ROLL_CURVE_LINEAR	45		// roll greater than this is copied out

#define PITCH_CURVE_ZERO		10	// pitch less than this is clamped to zero
#define PITCH_CURVE_LINEAR		45	// pitch greater than this is copied out

#define STICK_EXTENTS	400.0f

// the tires are considered to be skidding if they have sliding velocity of 10 in/s or more
const float DEFAULT_SKID_THRESHOLD = 10.0f;

float RemapAngleRange(float startInterval, float endInterval, float value);

class CEngineVehicleInternal : public CEngineObjectInternal, public IEngineVehicleServer {
public:
	DECLARE_DATADESC();
	DECLARE_CLASS(CEngineVehicleInternal, CEngineObjectInternal);
	CEngineVehicleInternal();
	~CEngineVehicleInternal();

	// Call Precache + Spawn from the containing entity's Precache + Spawn methods
	void Spawn();
	//void SetOuter(CBaseAnimating* pOuter, CFourWheelServerVehicle* pServerVehicle);

	// Initializes the vehicle physics so we can drive it
	bool Initialize(const char* pScriptName, unsigned int nVehicleType);

	void Teleport(matrix3x4_t& relativeTransform);
	void VPhysicsUpdate(IPhysicsObject* pPhysics);
	bool Think();
	void PlaceWheelDust(int wheelIndex, bool ignoreSpeed = false);

	void DrawDebugGeometryOverlays();
	int DrawDebugTextOverlays(int nOffset);

	// Updates the controls based on user input
	void UpdateDriverControls(CUserCmd* cmd, float flFrameTime);

	// Various steering parameters
	void SetThrottle(float flThrottle);
	void SetMaxThrottle(float flMaxThrottle);
	void SetMaxReverseThrottle(float flMaxThrottle);
	void SetSteering(float flSteering, float flSteeringRate);
	void SetSteeringDegrees(float flDegrees);
	void SetAction(float flAction);
	void TurnOn();
	void TurnOff();
	void ReleaseHandbrake();
	void SetHandbrake(bool bBrake);
	bool IsOn() const { return m_bIsOn; }
	void ResetControls();
	void SetBoost(float flBoost);
	bool UpdateBooster(void);
	void SetHasBrakePedal(bool bHasBrakePedal);

	// Engine
	void SetDisableEngine(bool bDisable);
	bool IsEngineDisabled(void) { return m_pVehicle->IsEngineDisabled(); }

	// Enable/Disable Motion
	void EnableMotion(void);
	void DisableMotion(void);

	// Shared code to compute the vehicle view position
	void GetVehicleViewPosition(const char* pViewAttachment, float flPitchFactor, Vector* pAbsPosition, QAngle* pAbsAngles);

	IPhysicsObject* GetWheel(int iWheel) { return m_pWheels[iWheel]; }

	int	GetSpeed() const;
	int GetMaxSpeed() const;
	int GetRPM() const;
	float GetThrottle() const;
	bool HasBoost() const;
	int BoostTimeLeft() const;
	bool IsBoosting(void);
	float GetHLSpeed() const;
	float GetSteering() const;
	float GetSteeringDegrees() const;
	IPhysicsVehicleController* GetVehicle(void) { return m_pVehicle; }
	float GetWheelBaseHeight(int wheelIndex) { return m_wheelBaseHeight[wheelIndex]; }
	float GetWheelTotalHeight(int wheelIndex) { return m_wheelTotalHeight[wheelIndex]; }

	IPhysicsVehicleController* GetVehicleController() { return m_pVehicle; }
	const vehicleparams_t& GetVehicleParams(void) { return m_pVehicle->GetVehicleParams(); }
	const vehicle_controlparams_t& GetVehicleControls(void) { return m_controls; }
	const vehicle_operatingparams_t& GetVehicleOperatingParams(void) { return m_pVehicle->GetOperatingParams(); }

	int VPhysicsGetObjectList(IPhysicsObject** pList, int listMax);

private:
	IServerVehicle* GetOuterServerVehicle() {
		return dynamic_cast<IServerVehicle*>(m_pOuter);
	}
	// engine sounds
	void CalcWheelData(vehicleparams_t& vehicle);

	void SteeringRest(float carSpeed, const vehicleparams_t& vehicleData);
	void SteeringTurn(float carSpeed, const vehicleparams_t& vehicleData, bool bTurnLeft, bool bBrake, bool bThrottle);
	void SteeringTurnAnalog(float carSpeed, const vehicleparams_t& vehicleData, float sidemove);

	// A couple wrapper methods to perform common operations
	//int		LookupPoseParameter(const char* szName);
	//float	GetPoseParameter(int iParameter);
	//float	SetPoseParameter(int iParameter, float flValue);
	//bool	GetAttachment(const char* szName, Vector& origin, QAngle& angles);

	void InitializePoseParameters();
	bool ParseVehicleScript(const char* pScriptName, solid_t& solid, vehicleparams_t& vehicle);
private:
	// This is the entity that contains this class
	//CHandle<CBaseAnimating>		m_pOuter;
	//CFourWheelServerVehicle* m_pOuterServerVehicle;

	vehicle_controlparams_t		m_controls;
	IPhysicsVehicleController* m_pVehicle;

	// Vehicle state info
	int					m_nSpeed;
	int					m_nLastSpeed;
	int					m_nRPM;
	float				m_fLastBoost;
	int					m_nBoostTimeLeft;
	int					m_nHasBoost;

	float				m_maxThrottle;
	float				m_flMaxRevThrottle;
	float				m_flMaxSpeed;
	float				m_actionSpeed;
	IPhysicsObject*		m_pWheels[4];

	int					m_wheelCount;

	Vector				m_wheelPosition[4];
	QAngle				m_wheelRotation[4];
	float				m_wheelBaseHeight[4];
	float				m_wheelTotalHeight[4];
	int					m_poseParameters[12];
	float				m_actionValue;
	float				m_actionScale;
	float				m_debugRadius;
	float				m_throttleRate;
	float				m_throttleStartTime;
	float				m_throttleActiveTime;
	float				m_turboTimer;

	float				m_flVehicleVolume;		// NPC driven vehicles used louder sounds
	bool				m_bIsOn;
	bool				m_bLastThrottle;
	bool				m_bLastBoost;
	bool				m_bLastSkid;
};


//-----------------------------------------------------------------------------
// Physics state..
//-----------------------------------------------------------------------------
inline int CEngineVehicleInternal::GetSpeed() const
{
	return m_nSpeed;
}

inline int CEngineVehicleInternal::GetMaxSpeed() const
{
	return INS2MPH(m_pVehicle->GetVehicleParams().engine.maxSpeed);
}

inline int CEngineVehicleInternal::GetRPM() const
{
	return m_nRPM;
}

inline float CEngineVehicleInternal::GetThrottle() const
{
	return m_controls.throttle;
}

inline bool CEngineVehicleInternal::HasBoost() const
{
	return m_nHasBoost != 0;
}

inline int CEngineVehicleInternal::BoostTimeLeft() const
{
	return m_nBoostTimeLeft;
}

//inline void CEngineVehicleInternal::SetOuter(CBaseAnimating* pOuter, CFourWheelServerVehicle* pServerVehicle)
//{
//	m_pOuter = pOuter;
//	m_pOuterServerVehicle = pServerVehicle;
//}

class CEngineRopeInternal : public CEngineObjectInternal, public IEngineRopeServer {
public:
	DECLARE_DATADESC();
	DECLARE_CLASS(CEngineRopeInternal, CEngineObjectInternal);
	DECLARE_SERVERCLASS();
	CEngineRopeInternal();
	~CEngineRopeInternal();

	CBaseEntity* GetStartPoint() { return m_hStartPoint; }
	CBaseEntity* GetEndPoint() { return m_hEndPoint.Get(); }
	int				GetEndAttachment() { return m_iStartAttachment; };

	void			SetStartPoint(CBaseEntity* pStartPoint, int attachment = 0);
	void			SetEndPoint(CBaseEntity* pEndPoint, int attachment = 0);

	int GetRopeFlags() { return m_RopeFlags; }
	void SetRopeFlags(int RopeFlags) {
		m_RopeFlags = RopeFlags;
	}
	void SetWidth(float Width) { m_Width = Width; }
	int GetSegments() { return m_nSegments; }
	void SetSegments(int nSegments) { m_nSegments = nSegments; }
	int GetLockedPoints() { return m_fLockedPoints; }
	void SetLockedPoints(int LockedPoints) { m_fLockedPoints = LockedPoints; }
	void SetRopeLength(int RopeLength) { m_RopeLength = RopeLength; }

	bool		SetupHangDistance(float flHangDist);
	void		ActivateStartDirectionConstraints(bool bEnable);
	void		ActivateEndDirectionConstraints(bool bEnable);

	int GetRopeMaterialModelIndex() { return m_iRopeMaterialModelIndex; }
	void SetRopeMaterialModelIndex(int RopeMaterialModelIndex) { m_iRopeMaterialModelIndex = RopeMaterialModelIndex; }
	void			EndpointsChanged();
	// Once-off length recalculation
	void			RecalculateLength(void);
	// These work just like the client-side versions.
	bool			GetEndPointPos2(CBaseEntity* pEnt, int iAttachment, Vector& v);
	bool			GetEndPointPos(int iPt, Vector& v);
	void			UpdateBBox(bool bForceRelink);
	// This is normally called by Activate but if you create the rope at runtime,
		// you must call it after you have setup its variables.
	void			Init();
	void			NotifyPositionChanged();
	// Unless this is called during initialization, the caller should have done
	// PrecacheModel on whatever material they specify in here.
	const char*		GetMaterialName() { return m_strRopeMaterialModel.ToCStr(); }
	void			SetMaterial(const char* pName);
	void			SetScrollSpeed(float flScrollSpeed) { m_flScrollSpeed = flScrollSpeed; }
	void			DetachPoint(int iPoint);
	// By default, ropes don't collide with the world. Call this to enable it.
	void			EnableCollision();
	// Toggle wind.
	void			EnableWind(bool bEnable);
	void			SetConstrainBetweenEndpoints(bool bConstrainBetweenEndpoints) { m_bConstrainBetweenEndpoints = m_bConstrainBetweenEndpoints; }
private:
	void			SetAttachmentPoint(CBaseHandle& hOutEnt, short& iOutAttachment, CBaseEntity* pEnt, int iAttachment);



	CNetworkVar(int, m_RopeFlags);		// Combination of ROPE_ defines in rope_shared.h
	CNetworkVar(int, m_Slack);
	CNetworkVar(float, m_Width);
	CNetworkVar(float, m_TextureScale);
	CNetworkVar(int, m_nSegments);		// Number of segments.
	CNetworkVar(bool, m_bConstrainBetweenEndpoints);
	CNetworkVar(int, m_iRopeMaterialModelIndex);	// Index of sprite model with the rope's material.
	string_t m_strRopeMaterialModel;

	// Number of subdivisions in between segments.
	CNetworkVar(int, m_Subdiv);

	//EHANDLE		m_hNextLink;

	CNetworkVar(int, m_RopeLength);	// Rope length at startup, used to calculate tension.

	CNetworkVar(int, m_fLockedPoints);
	CNetworkVar(float, m_flScrollSpeed);

	CNetworkHandle(CBaseEntity, m_hStartPoint);		// StartPoint/EndPoint are entities
	CNetworkHandle(CBaseEntity, m_hEndPoint);
	CNetworkVar(short, m_iStartAttachment);	// StartAttachment/EndAttachment are attachment points.
	CNetworkVar(short, m_iEndAttachment);
	// Used to detect changes.
	bool		m_bStartPointValid;
	bool		m_bEndPointValid;
};

class CEngineGhostInternal : public CEngineObjectInternal, public IEngineGhostServer {
public:

};

//-----------------------------------------------------------------------------
// An interface passed into the OnSave method of all entities
//-----------------------------------------------------------------------------
abstract_class IEntitySaveUtils
{
public:
	// Adds a level transition save dependency
	virtual void AddLevelTransitionSaveDependency(CBaseEntity * pEntity1, CBaseEntity * pEntity2) = 0;

	// Gets the # of dependencies for a particular entity
	virtual int GetEntityDependencyCount(CBaseEntity* pEntity) = 0;

	// Gets all dependencies for a particular entity
	virtual int GetEntityDependencies(CBaseEntity* pEntity, int nCount, CBaseEntity** ppEntList) = 0;
};

//-----------------------------------------------------------------------------
// Utilities entities can use when saving
//-----------------------------------------------------------------------------
class CEntitySaveUtils : public IEntitySaveUtils
{
public:
	// Call these in pre-save + post save
	void PreSave();
	void PostSave();

	// Methods of IEntitySaveUtils
	virtual void AddLevelTransitionSaveDependency(CBaseEntity* pEntity1, CBaseEntity* pEntity2);
	virtual int GetEntityDependencyCount(CBaseEntity* pEntity);
	virtual int GetEntityDependencies(CBaseEntity* pEntity, int nCount, CBaseEntity** ppEntList);

private:
	IPhysicsObjectPairHash* m_pLevelAdjacencyDependencyHash;
};

extern bool ShouldRemoveThisRagdoll(CBaseEntity* pRagdoll);

//-----------------------------------------------------------------------------
// Purpose: a global list of all the entities in the game.  All iteration through
//			entities is done through this object.
//-----------------------------------------------------------------------------
template<class T>
class CGlobalEntityList : public CBaseEntityList<T>, public IServerEntityList, public IEntityCallBack
{
	friend class CEngineObjectInternal;
	typedef CBaseEntityList<T> BaseClass;
public:
	virtual const char* GetBlockName();

	virtual void PreSave(CSaveRestoreData* pSaveData);
	virtual void Save(ISave* pSave);
	virtual void WriteSaveHeaders(ISave* pSave);
	virtual void PostSave();

	virtual void PreRestore();
	virtual void ReadRestoreHeaders(IRestore* pRestore);
	virtual void Restore(IRestore* pRestore, bool createPlayers);
	virtual void PostRestore();

	virtual int	CreateEntityTransitionList(IRestore* pRestore, int) OVERRIDE;
	virtual void BuildAdjacentMapList(ISave* pSave) OVERRIDE;

	void ReserveSlot(int index);
	int AllocateFreeSlot(bool bNetworkable = true, int index = -1);
	CBaseEntity* CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1);
	void				DestroyEntity(IHandleEntity* pEntity);
	IEngineObjectServer* GetEngineObject(int entnum);
	IServerNetworkable* GetServerNetworkable(CBaseHandle hEnt) const;
	IServerNetworkable* GetServerNetworkable(int entnum) const;
	IServerNetworkable* GetServerNetworkableFromHandle(CBaseHandle hEnt) const;
	IServerUnknown* GetServerUnknownFromHandle(CBaseHandle hEnt) const;
	IServerEntity* GetServerEntity(int entnum) const;
	IServerEntity* GetServerEntityFromHandle(CBaseHandle hEnt) const;
	short		GetNetworkSerialNumber(int iEntity) const;
	//CBaseNetworkable* GetBaseNetworkable( CBaseHandle hEnt ) const;
	CBaseEntity* GetBaseEntity(CBaseHandle hEnt) const;
	CBaseEntity* GetBaseEntity(int entnum) const;
	//edict_t* GetEdict( CBaseHandle hEnt ) const;

	int NumberOfEntities(void);
	int NumberOfEdicts(void);
	int NumberOfReservedEdicts(void);
	int IndexOfHighestEdict(void);

	// mark an entity as deleted
	void AddToDeleteList(T* ent);
	// call this before and after each frame to delete all of the marked entities.
	void CleanupDeleteList(void);
	int ResetDeleteList(void);

	// frees all entities in the game
	void Clear(void);

	// Returns true while in the Clear() call.
	bool	IsClearingEntities() { return m_bClearingEntities; }

	void ReportEntityFlagsChanged(CBaseEntity* pEntity, unsigned int flagsOld, unsigned int flagsNow);

	// iteration functions

	// returns the next entity after pCurrentEnt;  if pCurrentEnt is NULL, return the first entity
	CBaseEntity* NextEnt(CBaseEntity* pCurrentEnt);
	CBaseEntity* FirstEnt() { return NextEnt(NULL); }

	// returns the next entity of the specified class, using RTTI
	template< class U >
	U* NextEntByClass(U* start)
	{
		for (CBaseEntity* x = NextEnt(start); x; x = NextEnt(x))
		{
			start = dynamic_cast<U*>(x);
			if (start)
				return start;
		}
		return NULL;
	}

	// search functions
	bool		 IsEntityPtr(void* pTest);
	CBaseEntity* FindEntityByClassname(CBaseEntity* pStartEntity, const char* szName);
	CBaseEntity* FindEntityByName(CBaseEntity* pStartEntity, const char* szName, CBaseEntity* pSearchingEntity = NULL, CBaseEntity* pActivator = NULL, CBaseEntity* pCaller = NULL, IEntityFindFilter* pFilter = NULL);
	CBaseEntity* FindEntityByName(CBaseEntity* pStartEntity, string_t iszName, CBaseEntity* pSearchingEntity = NULL, CBaseEntity* pActivator = NULL, CBaseEntity* pCaller = NULL, IEntityFindFilter* pFilter = NULL)
	{
		return FindEntityByName(pStartEntity, STRING(iszName), pSearchingEntity, pActivator, pCaller, pFilter);
	}
	CBaseEntity* FindEntityInSphere(CBaseEntity* pStartEntity, const Vector& vecCenter, float flRadius);
	CBaseEntity* FindEntityByTarget(CBaseEntity* pStartEntity, const char* szName);
	CBaseEntity* FindEntityByModel(CBaseEntity* pStartEntity, const char* szModelName);

	CBaseEntity* FindEntityByNameNearest(const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity = NULL, CBaseEntity* pActivator = NULL, CBaseEntity* pCaller = NULL);
	CBaseEntity* FindEntityByNameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity = NULL, CBaseEntity* pActivator = NULL, CBaseEntity* pCaller = NULL);
	CBaseEntity* FindEntityByClassnameNearest(const char* szName, const Vector& vecSrc, float flRadius);
	CBaseEntity* FindEntityByClassnameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius);
	CBaseEntity* FindEntityByClassnameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecMins, const Vector& vecMaxs);

	CBaseEntity* FindEntityGeneric(CBaseEntity* pStartEntity, const char* szName, CBaseEntity* pSearchingEntity = NULL, CBaseEntity* pActivator = NULL, CBaseEntity* pCaller = NULL);
	CBaseEntity* FindEntityGenericWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity = NULL, CBaseEntity* pActivator = NULL, CBaseEntity* pCaller = NULL);
	CBaseEntity* FindEntityGenericNearest(const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity = NULL, CBaseEntity* pActivator = NULL, CBaseEntity* pCaller = NULL);

	CBaseEntity* FindEntityNearestFacing(const Vector& origin, const Vector& facing, float threshold);
	CBaseEntity* FindEntityClassNearestFacing(const Vector& origin, const Vector& facing, float threshold, char* classname);
	CBaseEntity* FindEntityByNetname(CBaseEntity* pStartEntity, const char* szModelName);

	CBaseEntity* FindEntityProcedural(const char* szName, CBaseEntity* pSearchingEntity = NULL, CBaseEntity* pActivator = NULL, CBaseEntity* pCaller = NULL);

	CGlobalEntityList();

	void AddDataAccessor(int type, IEntityDataInstantiator<T>* instantiator);
	void RemoveDataAccessor(int type);
	void* GetDataObject(int type, const T* instance);
	void* CreateDataObject(int type, T* instance);
	void DestroyDataObject(int type, T* instance);
	IEntitySaveUtils* GetEntitySaveUtils() { return &m_EntitySaveUtils; }
	T* FindLandmark(const char* pLandmarkName);
	int InTransitionVolume(T* pEntity, const char* pVolumeName);
	bool IsEntityInTransition(T* pEntity, const char* pLandmarkName);
	void OnChangeLevel(const char* pNewMapName, const char* pNewLandmarkName);

	// Call this when hierarchy is not completely set up (such as during Restore) to throw asserts
// when people call GetAbsAnything. 
	void SetAbsQueriesValid(bool bValid) { m_bAbsQueriesValid = bValid; }
	bool IsAbsQueriesValid() { return m_bAbsQueriesValid; }
	bool IsAccurateTriggerBboxChecks() { return m_bAccurateTriggerBboxChecks; }
	void SetAccurateTriggerBboxChecks(bool bAccurateTriggerBboxChecks) { m_bAccurateTriggerBboxChecks = bAccurateTriggerBboxChecks; }
	bool IsDisableTouchFuncs() { return m_bDisableTouchFuncs; }
	void SetDisableTouchFuncs(bool bDisableTouchFuncs) { m_bDisableTouchFuncs = bDisableTouchFuncs; }
	bool IsDisableEhandleAccess() { return m_bDisableEhandleAccess; }
	void SetDisableEhandleAccess(bool bDisableEhandleAccess) { m_bDisableEhandleAccess = bDisableEhandleAccess; }
	bool IsReceivedChainedUpdateOnRemove() { return m_bReceivedChainedUpdateOnRemove; }
	void SetReceivedChainedUpdateOnRemove(bool bReceivedChainedUpdateOnRemove) { m_bReceivedChainedUpdateOnRemove = bReceivedChainedUpdateOnRemove; }

	int GetPredictionRandomSeed(void);
	void SetPredictionRandomSeed(const CUserCmd* cmd);
	IEngineObject* GetPredictionPlayer(void);
	void SetPredictionPlayer(IEngineObject* player);

	bool IsSimulatingOnAlternateTicks();

	// Move it to the top of the LRU
	void MoveToTopOfLRU(CBaseEntity* pRagdoll, bool bImportant = false);
	void SetMaxRagdollCount(int iMaxCount) { m_iMaxRagdolls = iMaxCount; }
	int CountRagdolls(bool bOnlySimulatingRagdolls) { return bOnlySimulatingRagdolls ? m_iSimulatedRagdollCount : m_iRagdollCount; }
	virtual void UpdateRagdolls(float frametime);
protected:
	virtual void AfterCreated(IHandleEntity* pEntity);
	virtual void BeforeDestroy(IHandleEntity* pEntity);
	virtual void OnAddEntity( T *pEnt, CBaseHandle handle );
	virtual void OnRemoveEntity( T *pEnt, CBaseHandle handle );
	bool SaveInitEntities(CSaveRestoreData* pSaveData);
	void SaveEntityOnTable(T* pEntity, CSaveRestoreData* pSaveData, int& iSlot);

	//friend int CreateEntityTransitionList(CSaveRestoreData* pSaveData, int levelMask);
	void AddRestoredEntity(T* pEntity);
	bool DoRestoreEntity(T* pEntity, IRestore* pRestore);
	int RestoreEntity(T* pEntity, IRestore* pRestore, entitytable_t* pEntInfo);

	// Find the matching global entity.  Spit out an error if the designer made entities of
	// different classes with the same global name
	T* FindGlobalEntity(string_t classname, string_t globalname);

	int RestoreGlobalEntity(T* pEntity, IRestore* pRestore, entitytable_t* pEntInfo);
	void CreateEntitiesInTransitionList(IRestore* pRestore, int levelMask);
	int CreateEntityTransitionListInternal(IRestore* pRestore, int levelMask);

	int AddLandmarkToList(levellist_t* pLevelList, int listCount, const char* pMapName, const char* pLandmarkName, T* pentLandmark);
	// Builds the list of entities to save when moving across a transition
	int BuildLandmarkList(levellist_t* pLevelList, int maxList);

	// Builds the list of entities to bring across a particular transition
	int BuildEntityTransitionList(T* pLandmarkEntity, const char* pLandmarkName, T** ppEntList, int* pEntityFlags, int nMaxList);

	// Adds a single entity to the transition list, if appropriate. Returns the new count
	int AddEntityToTransitionList(T* pEntity, int flags, int nCount, T** ppEntList, int* pEntityFlags);

	// Adds in all entities depended on by entities near the transition
	int AddDependentEntities(int nCount, T** ppEntList, int* pEntityFlags, int nMaxList);

	// Figures out save flags for the entity
	int ComputeEntitySaveFlags(T* pEntity);

private:
	int m_iHighestEnt; // the topmost used array index
	int m_iNumEnts;
	int m_iHighestEdicts;
	int m_iNumEdicts;
	int m_iNumReservedEdicts;

	bool m_bClearingEntities;
	// removes the entity from the global list
// only called from with the CBaseEntity destructor
	bool m_bDisableEhandleAccess = false;
	bool m_bReceivedChainedUpdateOnRemove = false;
	bool m_fInCleanupDelete;
	CUtlVector<T*> m_DeleteList;
	CEngineObjectInternal* m_EngineObjectArray[NUM_ENT_ENTRIES];

	CEntitySaveUtils	m_EntitySaveUtils;
	CUtlVector<CBaseHandle> m_RestoredEntities;

	char st_szNextMap[cchMapNameMost];
	char st_szNextSpot[cchMapNameMost];

	// Used to show debug for only the transition volume we're currently in
	int m_iDebuggingTransition = 0;
	// When this is false, throw an assert in debug when GetAbsAnything is called. Used when hierachy is incomplete/invalid.
	bool m_bAbsQueriesValid = true;
	bool m_bAccurateTriggerBboxChecks = true;	// SOLID_BBOX entities do a fully accurate trigger vs bbox check when this is set // set to false for legacy behavior in ep1
	bool m_bDisableTouchFuncs = false;	// Disables PhysicsTouch and PhysicsStartTouch function calls

	// This is a random seed used by the networking code to allow client - side prediction code
//  randon number generators to spit out the same random numbers on both sides for a particular
//  usercmd input.
	int	m_nPredictionRandomSeed = -1;
	IEngineObject* m_pPredictionPlayer = NULL;

	CUtlLinkedList< EHANDLE > m_LRU;
	CUtlLinkedList< EHANDLE > m_LRUImportantRagdolls;

	int m_iMaxRagdolls;
	int m_iSimulatedRagdollCount;
	int m_iRagdollCount;
};

template<class T>
inline const char* CGlobalEntityList<T>::GetBlockName()
{
	return "Entities";
}

template<class T>
inline void CGlobalEntityList<T>::PreSave(CSaveRestoreData* pSaveData)
{
	m_EntitySaveUtils.PreSave();

	// Allow the entities to do some work
	T* pEnt = NULL;
	while ((pEnt = NextEnt(pEnt)) != NULL)
	{
		m_EngineObjectArray[pEnt->entindex()]->OnSave(&m_EntitySaveUtils);
	}

	SaveInitEntities(pSaveData);
}

template<class T>
void CGlobalEntityList<T>::SaveEntityOnTable(T* pEntity, CSaveRestoreData* pSaveData, int& iSlot)
{
	entitytable_t* pEntInfo = pSaveData->GetEntityInfo(iSlot);
	pEntInfo->id = iSlot;
#if !defined( CLIENT_DLL )
	pEntInfo->edictindex = pEntity->RequiredEdictIndex();
#else
	pEntInfo->edictindex = -1;
#endif
	pEntInfo->modelname = pEntity->GetEngineObject()->GetModelName();
	pEntInfo->restoreentityindex = -1;
	pEntInfo->saveentityindex = pEntity && pEntity->IsNetworkable() ? pEntity->entindex() : -1;
	pEntInfo->hEnt = pEntity->GetRefEHandle();
	pEntInfo->flags = 0;
	pEntInfo->location = 0;
	pEntInfo->size = 0;
	pEntInfo->classname = NULL_STRING;

	iSlot++;
}

template<class T>
bool CGlobalEntityList<T>::SaveInitEntities(CSaveRestoreData* pSaveData)
{
	int number_of_entities;

	number_of_entities = NumberOfEntities();

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

	while ((pEnt = NextEnt(pEnt)) != NULL)
	{
		SaveEntityOnTable(pEnt, pSaveData, i);
	}

	//pSaveData->BuildEntityHash();

	Assert(i == pSaveData->NumEntities());
	return (i == pSaveData->NumEntities());
}

template<class T>
void CGlobalEntityList<T>::Save(ISave* pSave)
{
	CGameSaveRestoreInfo* pSaveData = pSave->GetGameSaveRestoreInfo();

	// write entity list that was previously built by SaveInitEntities()
	for (int i = 0; i < pSaveData->NumEntities(); i++)
	{
		entitytable_t* pEntInfo = pSaveData->GetEntityInfo(i);
		pEntInfo->location = pSave->GetWritePos();
		pEntInfo->size = 0;

		T* pEnt = (T*)GetServerEntityFromHandle(pEntInfo->hEnt);
		if (pEnt && !(pEnt->ObjectCaps() & FCAP_DONT_SAVE))
		{
			MDLCACHE_CRITICAL_SECTION();
#if !defined( CLIENT_DLL )
			AssertMsg(pEnt->entindex() == -1 || (pEnt->GetEngineObject()->GetClassname() != NULL_STRING &&
				(STRING(pEnt->GetEngineObject()->GetClassname())[0] != 0) &&
				FStrEq(STRING(pEnt->GetEngineObject()->GetClassname()), pEnt->GetClassname())),
				"Saving entity with invalid classname");
#endif

			pSaveData->SetCurrentEntityContext(pEnt);
			pEnt->Save(*pSave);
			pSaveData->SetCurrentEntityContext(NULL);

			pEntInfo->size = pSave->GetWritePos() - pEntInfo->location;	// Size of entity block is data size written to block

			pEntInfo->classname = pEnt->GetEngineObject()->GetClassname();	// Remember entity class for respawn

#if !defined( CLIENT_DLL )
			pEntInfo->globalname = pEnt->GetEngineObject()->GetGlobalname(); // remember global name
			pEntInfo->landmarkModelSpace = g_ServerGameDLL.ModelSpaceLandmark(pEnt->GetEngineObject()->GetModelIndex());
			int nEntIndex = pEnt->IsNetworkable() ? pEnt->entindex() : -1;
			bool bIsPlayer = ((nEntIndex >= 1) && (nEntIndex <= gpGlobals->maxClients)) ? true : false;
			if (bIsPlayer)
			{
				pEntInfo->flags |= FENTTABLE_PLAYER;
			}
#endif
		}
	}
}

template<class T>
void CGlobalEntityList<T>::WriteSaveHeaders(ISave* pSave)
{
	CGameSaveRestoreInfo* pSaveData = pSave->GetGameSaveRestoreInfo();

	int nEntities = pSaveData->NumEntities();
	pSave->WriteInt(&nEntities);

	for (int i = 0; i < pSaveData->NumEntities(); i++)
		pSave->WriteEntityInfo(pSaveData->GetEntityInfo(i));
}

template<class T>
void CGlobalEntityList<T>::PostSave()
{
	m_EntitySaveUtils.PostSave();
}

template<class T>
void CGlobalEntityList<T>::PreRestore()
{
	CleanupDeleteList();
	m_RestoredEntities.Purge();
}

template<class T>
void CGlobalEntityList<T>::ReadRestoreHeaders(IRestore* pRestore)
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
void CGlobalEntityList<T>::AddRestoredEntity(T* pEntity)
{
	//Assert(m_InRestore);
	if (!pEntity)
		return;

	m_RestoredEntities.AddToTail(pEntity->GetRefEHandle());
}

template<class T>
void CGlobalEntityList<T>::Restore(IRestore* pRestore, bool createPlayers)
{
	entitytable_t* pEntInfo;
	T* pent;

	CGameSaveRestoreInfo* pSaveData = pRestore->GetGameSaveRestoreInfo();

	bool restoredWorld = false;

	// Create entity list
	int i;
	for (i = 0; i < pSaveData->NumEntities(); i++)
	{
		pEntInfo = pSaveData->GetEntityInfo(i);

		if (pEntInfo->classname != NULL_STRING && pEntInfo->size && !(pEntInfo->flags & FENTTABLE_REMOVED))
		{
			if (pEntInfo->edictindex == 0)	// worldspawn
			{
				Assert(i == 0);
				pent = CreateEntityByName(STRING(pEntInfo->classname));
				pRestore->SetReadPos(pEntInfo->location);
				if (RestoreEntity(pent, pRestore, pEntInfo) < 0)
				{
					pEntInfo->hEnt = NULL;
					pEntInfo->restoreentityindex = -1;
					UTIL_RemoveImmediate(pent);
				}
				else
				{
					// force the entity to be relinked
					AddRestoredEntity(pent);
				}
			}
			else if ((pEntInfo->edictindex > 0) && (pEntInfo->edictindex <= gpGlobals->maxClients))
			{
				if (!(pEntInfo->flags & FENTTABLE_PLAYER))
				{
					Warning("ENTITY IS NOT A PLAYER: %d\n", i);
					Assert(0);
				}

				if (createPlayers)//ed && 
				{
					// create the player
					pent = gEntList.CreateEntityByName(STRING(pEntInfo->classname), pEntInfo->edictindex);
				}
				else
					pent = NULL;
			}
			else
			{
				pent = CreateEntityByName(STRING(pEntInfo->classname));
			}
			pEntInfo->hEnt = pent;
			pEntInfo->restoreentityindex = pent && pent->IsNetworkable() ? pent->entindex() : -1;
			if (pent && pEntInfo->restoreentityindex == 0)
			{
				if (!FClassnameIs(pent, "worldspawn"))
				{
					pEntInfo->restoreentityindex = -1;
				}
			}

			if (pEntInfo->restoreentityindex == 0)
			{
				Assert(!restoredWorld);
				restoredWorld = true;
			}
		}
		else
		{
			pEntInfo->hEnt = NULL;
			pEntInfo->restoreentityindex = -1;
		}
	}

	// Now spawn entities
	for (i = 0; i < pSaveData->NumEntities(); i++)
	{
		pEntInfo = pSaveData->GetEntityInfo(i);
		if (pEntInfo->edictindex != 0)
		{
			pent = (T*)GetServerEntityFromHandle(pEntInfo->hEnt);
			pRestore->SetReadPos(pEntInfo->location);
			if (pent)
			{
				if (RestoreEntity(pent, pRestore, pEntInfo) < 0)
				{
					pEntInfo->hEnt = NULL;
					pEntInfo->restoreentityindex = -1;
					UTIL_RemoveImmediate(pent);
				}
				else
				{
					AddRestoredEntity(pent);
				}
			}
		}
	}
}

// Find the matching global entity.  Spit out an error if the designer made entities of
// different classes with the same global name
template<class T>
T* CGlobalEntityList<T>::FindGlobalEntity(string_t classname, string_t globalname)
{
	T* pReturn = NULL;

	while ((pReturn = NextEnt(pReturn)) != NULL)
	{
		if (FStrEq(STRING(pReturn->GetEngineObject()->GetGlobalname()), STRING(globalname)))
			break;
	}

	if (pReturn)
	{
		if (!FClassnameIs(pReturn, STRING(classname)))
		{
			Warning("Global entity found %s, wrong class %s [expects class %s]\n", STRING(globalname), STRING(pReturn->GetEngineObject()->GetClassname()), STRING(classname));
			pReturn = NULL;
		}
	}

	return pReturn;
}
//---------------------------------

template<class T>
bool CGlobalEntityList<T>::DoRestoreEntity(T* pEntity, IRestore* pRestore)
{
	MDLCACHE_CRITICAL_SECTION();

	EHANDLE hEntity;

	hEntity = pEntity;

	pRestore->GetGameSaveRestoreInfo()->SetCurrentEntityContext(pEntity);
	pEntity->Restore(*pRestore);
	pRestore->GetGameSaveRestoreInfo()->SetCurrentEntityContext(NULL);

#if !defined( CLIENT_DLL )
	if (pEntity->ObjectCaps() & FCAP_MUST_SPAWN)
	{
		pEntity->Spawn();
	}
	else
	{
		pEntity->Precache();
	}
#endif

	// Above calls may have resulted in self destruction
	return (hEntity != NULL);
}

template<class T>
int CGlobalEntityList<T>::RestoreEntity(T* pEntity, IRestore* pRestore, entitytable_t* pEntInfo)
{
	if (!DoRestoreEntity(pEntity, pRestore))
		return 0;

#if !defined( CLIENT_DLL )		
	if (pEntity->GetEngineObject()->GetGlobalname() != NULL_STRING)
	{
		int globalIndex = engine->GlobalEntity_GetIndex(pEntity->GetEngineObject()->GetGlobalname());
		if (globalIndex >= 0)
		{
			// Already dead? delete
			if (engine->GlobalEntity_GetState(globalIndex) == GLOBAL_DEAD)
				return -1;
			else if (!FStrEq(STRING(gpGlobals->mapname), engine->GlobalEntity_GetMap(globalIndex)))
			{
				pEntity->MakeDormant();	// Hasn't been moved to this level yet, wait but stay alive
			}
			// In this level & not dead, continue on as normal
		}
		else
		{
			Warning("Global Entity %s (%s) not in table!!!\n", STRING(pEntity->GetEngineObject()->GetGlobalname()), STRING(pEntity->GetEngineObject()->GetClassname()));
			// Spawned entities default to 'On'
			engine->GlobalEntity_Add(pEntity->GetEngineObject()->GetGlobalname(), gpGlobals->mapname, GLOBAL_ON);
		}
	}
#endif

	return 0;
}

//---------------------------------
template<class T>
int CGlobalEntityList<T>::RestoreGlobalEntity(T* pEntity, IRestore* pRestore, entitytable_t* pEntInfo)
{
	Vector oldOffset;
	EHANDLE hEntitySafeHandle;
	hEntitySafeHandle = pEntity;
	CGameSaveRestoreInfo* pSaveData = pRestore->GetGameSaveRestoreInfo();
	oldOffset.Init();
	//CRestoreServer restoreHelper(pSaveData);

	string_t globalName = pEntInfo->globalname, className = pEntInfo->classname;

	// -------------------

	int globalIndex = engine->GlobalEntity_GetIndex(globalName);

	// Don't overlay any instance of the global that isn't the latest
	// pSaveData->szCurrentMapName is the level this entity is coming from
	// pGlobal->levelName is the last level the global entity was active in.
	// If they aren't the same, then this global update is out of date.
	if (!FStrEq(pSaveData->levelInfo.szCurrentMapName, engine->GlobalEntity_GetMap(globalIndex)))
	{
		return 0;
	}

	// Compute the new global offset
	T* pNewEntity = FindGlobalEntity(className, globalName);
	if (pNewEntity)
	{
		//				Msg( "Overlay %s with %s\n", pNewEntity->GetClassname(), STRING(tmpEnt->classname) );
				// Tell the restore code we're overlaying a global entity from another level
		pRestore->SetGlobalMode(1);	// Don't overwrite global fields

		pSaveData->modelSpaceOffset = pEntInfo->landmarkModelSpace - g_ServerGameDLL.ModelSpaceLandmark(pNewEntity->GetEngineObject()->GetModelIndex());

		UTIL_Remove(pEntity);
		pEntity = pNewEntity;// we're going to restore this data OVER the old entity
		pEntInfo->hEnt = pEntity;
		// HACKHACK: Do we need system-wide support for removing non-global spawn allocated resources?
		pEntity->GetEngineObject()->VPhysicsDestroyObject();
		Assert(pEntInfo->edictindex == -1);
		// Update the global table to say that the global definition of this entity should come from this level
		engine->GlobalEntity_SetMap(globalIndex, gpGlobals->mapname);
	}
	else
	{
		// This entity will be freed automatically by the engine->  If we don't do a restore on a matching entity (below)
		// or call EntityUpdate() to move it to this level, we haven't changed global state at all.
		DevMsg("Warning: No match for global entity %s found in destination level\n", STRING(globalName));
		return 0;
	}

	if (!DoRestoreEntity(pEntity, pRestore))
	{
		pEntity = NULL;
	}
	pRestore->SetGlobalMode(0);
	// Is this an overriding global entity (coming over the transition)
	pSaveData->modelSpaceOffset.Init();
	if (pEntity)
		return 1;
	return 0;
}

template<class T>
void CGlobalEntityList<T>::PostRestore()
{
	// The entire hierarchy is restored, so we can call GetAbsOrigin again.
//CBaseEntity::SetAbsQueriesValid( true );

// Call all entities' OnRestore handlers
	for (int i = m_RestoredEntities.Count() - 1; i >= 0; --i)
	{
		T* pEntity = (T*)GetServerEntityFromHandle(m_RestoredEntities[i]);
		if (pEntity && !pEntity->IsDormant())
		{
			MDLCACHE_CRITICAL_SECTION();
			m_EngineObjectArray[pEntity->entindex()]->OnRestore();
		}
	}

	m_RestoredEntities.Purge();
	CleanupDeleteList();
}

//=============================================================================
//------------------------------------------------------------------------------
// Creates all entities that lie in the transition list
//------------------------------------------------------------------------------
template<class T>
void CGlobalEntityList<T>::CreateEntitiesInTransitionList(IRestore* pRestore, int levelMask)
{
	T* pent;
	int i;
	CGameSaveRestoreInfo* pSaveData = pRestore->GetGameSaveRestoreInfo();
	for (i = 0; i < pSaveData->NumEntities(); i++)
	{
		entitytable_t* pEntInfo = pSaveData->GetEntityInfo(i);
		pEntInfo->hEnt = NULL;

		if (pEntInfo->size == 0 || pEntInfo->edictindex == 0)
			continue;

		if (pEntInfo->classname == NULL_STRING)
		{
			Warning("Entity with data saved, but with no classname\n");
			Assert(0);
			continue;
		}

		bool active = (pEntInfo->flags & levelMask) ? 1 : 0;

		// spawn players
		pent = NULL;
		if ((pEntInfo->edictindex > 0) && (pEntInfo->edictindex <= gpGlobals->maxClients))
		{
			if (active)//&& ed && !ed->IsFree()
			{
				if (!(pEntInfo->flags & FENTTABLE_PLAYER))
				{
					Warning("ENTITY IS NOT A PLAYER: %d\n", i);
					Assert(0);
				}

				pent = gEntList.CreateEntityByName(STRING(pEntInfo->classname), pEntInfo->edictindex);
			}
		}
		else if (active)
		{
			pent = CreateEntityByName(STRING(pEntInfo->classname));
		}

		pEntInfo->hEnt = pent;
	}
}

//-----------------------------------------------------------------------------
template<class T>
int CGlobalEntityList<T>::CreateEntityTransitionListInternal(IRestore* pRestore, int levelMask)
{
	T* pent;
	entitytable_t* pEntInfo;

	// Create entity list
	CreateEntitiesInTransitionList(pRestore, levelMask);
	CGameSaveRestoreInfo* pSaveData = pRestore->GetGameSaveRestoreInfo();
	// Now spawn entities
	CUtlVector<int> checkList;

	int i;
	int movedCount = 0;
	for (i = 0; i < pSaveData->NumEntities(); i++)
	{
		pEntInfo = pSaveData->GetEntityInfo(i);
		pent = (T*)GetServerEntityFromHandle(pEntInfo->hEnt);
		//		pSaveData->currentIndex = i;
		pRestore->SetReadPos(pEntInfo->location);

		// clear this out - it must be set on a per-entity basis
		pSaveData->modelSpaceOffset.Init();

		if (pent && (pEntInfo->flags & levelMask))		// Screen out the player if he's not to be spawned
		{
			if (pEntInfo->flags & FENTTABLE_GLOBAL)
			{
				DevMsg(2, "Merging changes for global: %s\n", STRING(pEntInfo->classname));

				// -------------------------------------------------------------------------
				// Pass the "global" flag to the DLL to indicate this entity should only override
				// a matching entity, not be spawned
				if (RestoreGlobalEntity(pent, pRestore, pEntInfo) > 0)
				{
					movedCount++;
					pEntInfo->restoreentityindex = ((T*)GetServerEntityFromHandle(pEntInfo->hEnt))->entindex();
					AddRestoredEntity((T*)GetServerEntityFromHandle(pEntInfo->hEnt));
				}
				else
				{
					UTIL_RemoveImmediate((T*)GetServerEntityFromHandle(pEntInfo->hEnt));
				}
				// -------------------------------------------------------------------------
			}
			else
			{
				DevMsg(2, "Transferring %s (%d)\n", STRING(pEntInfo->classname), pent->entindex());
				//CRestoreServer restoreHelper(pSaveData);
				if (RestoreEntity(pent, pRestore, pEntInfo) < 0)
				{
					UTIL_RemoveImmediate(pent);
				}
				else
				{
					// needs to be checked.  Do this in a separate pass so that pointers & hierarchy can be traversed
					checkList.AddToTail(i);
				}
			}

			// Remove any entities that were removed using UTIL_Remove() as a result of the above calls to UTIL_RemoveImmediate()
			CleanupDeleteList();
		}
	}

	for (i = checkList.Count() - 1; i >= 0; --i)
	{
		pEntInfo = pSaveData->GetEntityInfo(checkList[i]);
		pent = (T*)GetServerEntityFromHandle(pEntInfo->hEnt);

		// NOTE: pent can be NULL because UTIL_RemoveImmediate (called below) removes all in hierarchy
		if (!pent)
			continue;

		MDLCACHE_CRITICAL_SECTION();

		if (!(pEntInfo->flags & FENTTABLE_PLAYER) && UTIL_EntityInSolid(pent))
		{
			// this can happen during normal processing - PVS is just a guess, some map areas won't exist in the new map
			DevMsg(2, "Suppressing %s\n", STRING(pEntInfo->classname));
			UTIL_RemoveImmediate(pent);
			// Remove any entities that were removed using UTIL_Remove() as a result of the above calls to UTIL_RemoveImmediate()
			CleanupDeleteList();
		}
		else
		{
			movedCount++;
			pEntInfo->flags = FENTTABLE_REMOVED;
			pEntInfo->restoreentityindex = pent->entindex();
			AddRestoredEntity(pent);
		}
	}

	return movedCount;
}

template<class T>
int	CGlobalEntityList<T>::CreateEntityTransitionList(IRestore* pRestore, int a)
{
	//CRestoreServer restoreHelper(s);
	// save off file base
	int base = pRestore->GetReadPos();

	int movedCount = CreateEntityTransitionListInternal(pRestore, a);
	if (movedCount)
	{
		engine->CallBlockHandlerRestore(GetPhysSaveRestoreBlockHandler(), base, pRestore, false);
		engine->CallBlockHandlerRestore(GetAISaveRestoreBlockHandler(), base, pRestore, false);
	}

	GetPhysSaveRestoreBlockHandler()->PostRestore();
	GetAISaveRestoreBlockHandler()->PostRestore();
	this->PostRestore();
	return movedCount;
}

template<class T>
T* CGlobalEntityList<T>::FindLandmark(const char* pLandmarkName)
{
	T* pentLandmark;

	pentLandmark = FindEntityByName(NULL, pLandmarkName);
	while (pentLandmark)
	{
		// Found the landmark
		if (FClassnameIs(pentLandmark, "info_landmark"))
			return pentLandmark;
		else
			pentLandmark = FindEntityByName(pentLandmark, pLandmarkName);
	}
	Warning("Can't find landmark %s\n", pLandmarkName);
	return NULL;
}


// Add a transition to the list, but ignore duplicates 
// (a designer may have placed multiple trigger_changelevels with the same landmark)
template<class T>
int CGlobalEntityList<T>::AddLandmarkToList(levellist_t* pLevelList, int listCount, const char* pMapName, const char* pLandmarkName, T* pentLandmark)
{
	int i;

	if (!pLevelList || !pMapName || !pLandmarkName || !pentLandmark)
		return 0;

	// Ignore changelevels to the level we're ready in. Mapmakers love to do this!
	if (stricmp(pMapName, STRING(gpGlobals->mapname)) == 0)
		return 0;

	for (i = 0; i < listCount; i++)
	{
		if (pLevelList[i].pentLandmark == pentLandmark && stricmp(pLevelList[i].mapName, pMapName) == 0)
			return 0;
	}
	Q_strncpy(pLevelList[listCount].mapName, pMapName, sizeof(pLevelList[listCount].mapName));
	Q_strncpy(pLevelList[listCount].landmarkName, pLandmarkName, sizeof(pLevelList[listCount].landmarkName));
	pLevelList[listCount].pentLandmark = pentLandmark;

	T* ent = (pentLandmark);
	Assert(ent);

	pLevelList[listCount].vecLandmarkOrigin = ent->GetEngineObject()->GetAbsOrigin();

	return 1;
}

enum
{
	TRANSITION_VOLUME_SCREENED_OUT = 0,
	TRANSITION_VOLUME_NOT_FOUND = 1,
	TRANSITION_VOLUME_PASSED = 2,
};


template<class T>
int CGlobalEntityList<T>::InTransitionVolume(T* pEntity, const char* pVolumeName)
{
	T* pVolume;

	if (pEntity->ObjectCaps() & FCAP_FORCE_TRANSITION)
		return TRANSITION_VOLUME_PASSED;

	// If you're following another entity, follow it through the transition (weapons follow the player)
	pEntity = pEntity->GetEngineObject()->GetRootMoveParent()->GetOuter();

	int inVolume = TRANSITION_VOLUME_NOT_FOUND;	// Unless we find a trigger_transition, everything is in the volume

	pVolume = FindEntityByName(NULL, pVolumeName);
	while (pVolume)
	{
		if (pVolume && FClassnameIs(pVolume, "trigger_transition"))
		{
			if (TestEntityTriggerIntersection_Accurate(pVolume->GetEngineObject(), pEntity->GetEngineObject()))	// It touches one, it's in the volume
				return TRANSITION_VOLUME_PASSED;

			inVolume = TRANSITION_VOLUME_SCREENED_OUT;	// Found a trigger_transition, but I don't intersect it -- if I don't find another, don't go!
		}
		pVolume = FindEntityByName(pVolume, pVolumeName);
	}
	return inVolume;
}

//-----------------------------------------------------------------------------
// Purpose: Performs the level change and fires targets.
// Input  : pActivator - 
//-----------------------------------------------------------------------------
template<class T>
bool CGlobalEntityList<T>::IsEntityInTransition(T* pEntity, const char* pLandmarkName)
{
	int transitionState = InTransitionVolume(pEntity, pLandmarkName);
	if (transitionState == TRANSITION_VOLUME_SCREENED_OUT)
	{
		return false;
	}

	// look for a landmark entity		
	T* pLandmark = FindLandmark(pLandmarkName);

	if (!pLandmark)
		return false;

	// Check to make sure it's also in the PVS of landmark
	byte pvs[MAX_MAP_CLUSTERS / 8];
	int clusterIndex = engine->GetClusterForOrigin(pLandmark->GetEngineObject()->GetAbsOrigin());
	engine->GetPVSForCluster(clusterIndex, sizeof(pvs), pvs);
	Vector vecSurroundMins, vecSurroundMaxs;
	pEntity->GetEngineObject()->WorldSpaceSurroundingBounds(&vecSurroundMins, &vecSurroundMaxs);

	return engine->CheckBoxInPVS(vecSurroundMins, vecSurroundMaxs, pvs, sizeof(pvs));
}

//------------------------------------------------------------------------------
// Adds a single entity to the transition list, if appropriate. Returns the new count
//------------------------------------------------------------------------------
template<class T>
int CGlobalEntityList<T>::ComputeEntitySaveFlags(T* pEntity)
{
	if (m_iDebuggingTransition == DEBUG_TRANSITIONS_VERBOSE)
	{
		Msg("Trying %s (%s): ", pEntity->GetClassname(), pEntity->GetDebugName());
	}

	int caps = pEntity->ObjectCaps();
	if (caps & FCAP_DONT_SAVE)
	{
		if (m_iDebuggingTransition == DEBUG_TRANSITIONS_VERBOSE)
		{
			Msg("IGNORED due to being marked \"Don't save\".\n");
		}
		return 0;
	}

	// If this entity can be moved or is global, mark it
	int flags = 0;
	if (caps & FCAP_ACROSS_TRANSITION)
	{
		flags |= FENTTABLE_MOVEABLE;
	}
	if (pEntity->GetEngineObject()->GetGlobalname() != NULL_STRING && !pEntity->IsDormant())
	{
		flags |= FENTTABLE_GLOBAL;
	}

	if (m_iDebuggingTransition == DEBUG_TRANSITIONS_VERBOSE && !flags)
	{
		Msg("IGNORED, no across_transition flag & no globalname\n");
	}

	return flags;
}

//------------------------------------------------------------------------------
// Adds a single entity to the transition list, if appropriate. Returns the new count
//------------------------------------------------------------------------------
template<class T>
int CGlobalEntityList<T>::AddEntityToTransitionList(T* pEntity, int flags, int nCount, T** ppEntList, int* pEntityFlags)
{
	ppEntList[nCount] = pEntity;
	pEntityFlags[nCount] = flags;
	++nCount;

	// If we're debugging, make it visible
	if (m_iDebuggingTransition)
	{
		if (m_iDebuggingTransition == DEBUG_TRANSITIONS_VERBOSE)
		{
			// In verbose mode we've already printed out what the entity is
			Msg("ADDED.\n");
		}
		else
		{
			// In non-verbose mode, we just print this line
			Msg("ADDED %s (%s) to transition.\n", pEntity->GetClassname(), pEntity->GetDebugName());
		}

		pEntity->m_debugOverlays |= (OVERLAY_BBOX_BIT | OVERLAY_NAME_BIT);
	}

	return nCount;
}


//------------------------------------------------------------------------------
// Builds the list of entities to bring across a particular transition
//------------------------------------------------------------------------------
template<class T>
int CGlobalEntityList<T>::BuildEntityTransitionList(T* pLandmarkEntity, const char* pLandmarkName,
	T** ppEntList, int* pEntityFlags, int nMaxList)
{
	int iEntity = 0;

	ConVarRef g_debug_transitions("g_debug_transitions");
	// Only show debug for the transition to the level we're going to
	if (g_debug_transitions.GetInt() && pLandmarkEntity->NameMatches(st_szNextSpot))
	{
		m_iDebuggingTransition = g_debug_transitions.GetInt();

		// Show us where the landmark entity is
		pLandmarkEntity->m_debugOverlays |= (OVERLAY_PIVOT_BIT | OVERLAY_BBOX_BIT | OVERLAY_NAME_BIT);
	}
	else
	{
		m_iDebuggingTransition = 0;
	}

	// Follow the linked list of entities in the PVS of the transition landmark
	T* pEntity = NULL;
	while ((pEntity = UTIL_EntitiesInPVS(pLandmarkEntity, pEntity)) != NULL)
	{
		int flags = ComputeEntitySaveFlags(pEntity);
		if (!flags)
			continue;

		// Check to make sure the entity isn't screened out by a trigger_transition
		if (!InTransitionVolume(pEntity, pLandmarkName))
		{
			if (m_iDebuggingTransition == DEBUG_TRANSITIONS_VERBOSE)
			{
				Msg("IGNORED, outside transition volume.\n");
			}
			continue;
		}

		if (iEntity >= nMaxList)
		{
			Warning("Too many entities across a transition!\n");
			Assert(0);
			return iEntity;
		}

		iEntity = AddEntityToTransitionList(pEntity, flags, iEntity, ppEntList, pEntityFlags);
	}

	return iEntity;
}

//------------------------------------------------------------------------------
// Builds the list of entities to save when moving across a transition
//------------------------------------------------------------------------------
template<class T>
int CGlobalEntityList<T>::BuildLandmarkList(levellist_t* pLevelList, int maxList)
{
	int nCount = 0;

	T* pentChangelevel = FindEntityByClassname(NULL, "trigger_changelevel");
	while (pentChangelevel)
	{
		//CChangeLevel* pTrigger = dynamic_cast<CChangeLevel*>(pentChangelevel);
		if (pentChangelevel->IsChangeLevelTrigger())
		{
			// Find the corresponding landmark
			T* pentLandmark = FindLandmark(pentChangelevel->GetNewLandmarkName());
			if (pentLandmark)
			{
				// Build a list of unique transitions
				if (AddLandmarkToList(pLevelList, nCount, pentChangelevel->GetNewMapName(), pentChangelevel->GetNewLandmarkName(), pentLandmark))
				{
					++nCount;
					if (nCount >= maxList)		// FULL!!
						break;
				}
			}
		}
		pentChangelevel = FindEntityByClassname(pentChangelevel, "trigger_changelevel");
	}

	return nCount;
}

template<class T>
void CGlobalEntityList<T>::OnChangeLevel(const char* pNewMapName, const char* pNewLandmarkName) 
{
	m_iDebuggingTransition = 0;
	st_szNextSpot[0] = 0;	// Init landmark to NULL
	Q_strncpy(st_szNextSpot, pNewLandmarkName, sizeof(st_szNextSpot));
	// This object will get removed in the call to engine->ChangeLevel, copy the params into "safe" memory
	Q_strncpy(st_szNextMap, pNewMapName, sizeof(st_szNextMap));
}

//------------------------------------------------------------------------------
// Tests bits in a bitfield
//------------------------------------------------------------------------------
inline bool IsBitSet(char* pBuf, int nBit)
{
	return (pBuf[nBit >> 3] & (1 << (nBit & 0x7))) != 0;
}

inline void Set(char* pBuf, int nBit)
{
	pBuf[nBit >> 3] |= 1 << (nBit & 0x7);
}

//------------------------------------------------------------------------------
// Adds in all entities depended on by entities near the transition
//------------------------------------------------------------------------------
template<class T>
int CGlobalEntityList<T>::AddDependentEntities(int nCount, T** ppEntList, int* pEntityFlags, int nMaxList)
{
	char pEntitiesSaved[MAX_ENTITY_BYTE_COUNT];
	memset(pEntitiesSaved, 0, MAX_ENTITY_BYTE_COUNT * sizeof(char));

	// Populate the initial bitfield
	int i;
	for (i = 0; i < nCount; ++i)
	{
		// NOTE: Must use GetEntryIndex because we're saving non-networked entities
		int nEntIndex = ppEntList[i]->GetRefEHandle().GetEntryIndex();

		// We shouldn't already have this entity in the list!
		Assert(!IsBitSet(pEntitiesSaved, nEntIndex));

		// Mark the entity as being in the list
		Set(pEntitiesSaved, nEntIndex);
	}

	IEntitySaveUtils* pSaveUtils = GetEntitySaveUtils();

	ConVarRef g_debug_transitions("g_debug_transitions");
	// Iterate over entities whose dependencies we've not yet processed
	// NOTE: nCount will change value during this loop in AddEntityToTransitionList
	for (i = 0; i < nCount; ++i)
	{
		T* pEntity = ppEntList[i];

		// Find dependencies in the hash.
		int nDepCount = pSaveUtils->GetEntityDependencyCount(pEntity);
		if (!nDepCount)
			continue;

		T** ppDependentEntities = (T**)stackalloc(nDepCount * sizeof(T*));
		pSaveUtils->GetEntityDependencies(pEntity, nDepCount, ppDependentEntities);
		for (int j = 0; j < nDepCount; ++j)
		{
			T* pDependent = ppDependentEntities[j];
			if (!pDependent)
				continue;

			// NOTE: Must use GetEntryIndex because we're saving non-networked entities
			int nEntIndex = pDependent->GetRefEHandle().GetEntryIndex();

			// Don't re-add it if it's already in the list
			if (IsBitSet(pEntitiesSaved, nEntIndex))
				continue;

			// Mark the entity as being in the list
			Set(pEntitiesSaved, nEntIndex);

			int flags = ComputeEntitySaveFlags(pEntity);
			if (flags)
			{
				if (nCount >= nMaxList)
				{
					Warning("Too many entities across a transition!\n");
					Assert(0);
					return false;
				}

				if (g_debug_transitions.GetInt())
				{
					Msg("ADDED DEPENDANCY: %s (%s)\n", pEntity->GetClassname(), pEntity->GetDebugName());
				}

				nCount = AddEntityToTransitionList(pEntity, flags, nCount, ppEntList, pEntityFlags);
			}
			else
			{
				Warning("Warning!! Save dependency is linked to an entity that doesn't want to be saved!\n");
			}
		}
	}

	return nCount;
}

//-----------------------------------------------------------------------------
// Purpose: Called during a transition, to build a map adjacency list
//-----------------------------------------------------------------------------
template<class T>
void CGlobalEntityList<T>::BuildAdjacentMapList(ISave* pSave)
{
	// retrieve the pointer to the save data
	CGameSaveRestoreInfo* pSaveData = pSave->GetGameSaveRestoreInfo();
	if (!pSaveData) {
		return;
	}

	// Find all of the possible level changes on this BSP
	pSaveData->levelInfo.connectionCount = BuildLandmarkList(pSaveData->levelInfo.levelList, MAX_LEVEL_CONNECTIONS);

	if (pSaveData->NumEntities() == 0) {
		return;
	}

	//CSaveServer saveHelper(pSaveData);

	// For each level change, find nearby entities and save them
	int	i;
	for (i = 0; i < pSaveData->levelInfo.connectionCount; i++)
	{
		T* pEntList[MAX_ENTITY];
		int			 entityFlags[MAX_ENTITY];

		// First, figure out which entities are near the transition
		T* pLandmarkEntity = (T*)(pSaveData->levelInfo.levelList[i].pentLandmark);
		int iEntityCount = BuildEntityTransitionList(pLandmarkEntity, pSaveData->levelInfo.levelList[i].landmarkName, pEntList, entityFlags, MAX_ENTITY);

		// FIXME: Activate if we have a dependency problem on level transition
		// Next, add in all entities depended on by entities near the transition
//		iEntity = AddDependentEntities( iEntity, pEntList, entityFlags, MAX_ENTITY );

		int j;
		for (j = 0; j < iEntityCount; j++)
		{
			// Mark entity table with 1<<i
			int index = pSave->EntityIndex(pEntList[j]);
			// Flag it with the level number
			pSave->EntityFlagsSet(index, entityFlags[j] | (1 << i));
		}
	}
}

//-----------------------------------------------------------------------------
// Inlines.
//-----------------------------------------------------------------------------
//template<class T>
//inline edict_t* CGlobalEntityList<T>::GetEdict( CBaseHandle hEnt ) const
//{
//	T *pUnk = (BaseClass::LookupEntity( hEnt ));
//	if ( pUnk )
//		return pUnk->GetNetworkable()->GetEdict();
//	else
//		return NULL;
//}

template<class T>
inline void CGlobalEntityList<T>::ReserveSlot(int index) {
	BaseClass::ReserveSlot(index);
}

template<class T>
inline int CGlobalEntityList<T>::AllocateFreeSlot(bool bNetworkable, int index) {
	return BaseClass::AllocateFreeSlot(bNetworkable, index);
}

template<class T>
inline CBaseEntity* CGlobalEntityList<T>::CreateEntityByName(const char* className, int iForceEdictIndex, int iSerialNum) {
	if (EntityFactoryDictionary()->RequiredEdictIndex(className) != -1) {
		iForceEdictIndex = EntityFactoryDictionary()->RequiredEdictIndex(className);
	}
	iForceEdictIndex = BaseClass::AllocateFreeSlot(EntityFactoryDictionary()->IsNetworkable(className), iForceEdictIndex);
	iSerialNum = BaseClass::GetNetworkSerialNumber(iForceEdictIndex);
	if (m_EngineObjectArray[iForceEdictIndex]) {
		Error("slot not free!");
	}
	IEntityFactory* pFactory = EntityFactoryDictionary()->FindFactory(className);
	if (!pFactory)
	{
		Warning("Attempted to create unknown entity type %s!\n", className);
		return NULL;
	}
	switch (pFactory->GetEngineObjectType()) {
	case ENGINEOBJECT_BASE:
		m_EngineObjectArray[iForceEdictIndex] = new CEngineObjectInternal();
		break;
	case ENGINEOBJECT_WORLD:
		m_EngineObjectArray[iForceEdictIndex] = new CEngineWorldInternal();
		break;
	case ENGINEOBJECT_PLAYER:
		m_EngineObjectArray[iForceEdictIndex] = new CEnginePlayerInternal();
		break;
	case ENGINEOBJECT_PORTAL:
		m_EngineObjectArray[iForceEdictIndex] = new CEnginePortalInternal();
		break;
	case ENGINEOBJECT_SHADOWCLONE:
		m_EngineObjectArray[iForceEdictIndex] = new CEngineShadowCloneInternal();
		break;
	case ENGINEOBJECT_VEHICLE:
		m_EngineObjectArray[iForceEdictIndex] = new CEngineVehicleInternal();
		break;
	case ENGINEOBJECT_ROPE:
		m_EngineObjectArray[iForceEdictIndex] = new CEngineRopeInternal();
		break;
	case ENGINEOBJECT_GHOST:
		m_EngineObjectArray[iForceEdictIndex] = new CEngineGhostInternal();
		break;
	default:
		Error("GetEngineObjectType error!\n");
	}
	return (CBaseEntity*)EntityFactoryDictionary()->Create(this, className, iForceEdictIndex, iSerialNum, this);
}

template<class T>
inline void	CGlobalEntityList<T>::DestroyEntity(IHandleEntity* pEntity) {
	EntityFactoryDictionary()->Destroy(pEntity);
}

//template<class T>
//inline CBaseNetworkable* CGlobalEntityList<T>::GetBaseNetworkable( CBaseHandle hEnt ) const
//{
//	T *pUnk = (BaseClass::LookupEntity( hEnt ));
//	if ( pUnk )
//		return pUnk->GetNetworkable()->GetBaseNetworkable();
//	else
//		return NULL;
//}

template<class T>
inline IEngineObjectServer* CGlobalEntityList<T>::GetEngineObject(int entnum) {
	if (entnum < 0 || entnum >= NUM_ENT_ENTRIES) {
		return NULL;
	}
	return m_EngineObjectArray[entnum];
}

template<class T>
inline IServerNetworkable* CGlobalEntityList<T>::GetServerNetworkable( CBaseHandle hEnt ) const
{
	T *pUnk = (BaseClass::LookupEntity( hEnt ));
	if ( pUnk )
		return pUnk->GetNetworkable();
	else
		return NULL;
}

template<class T>
IServerNetworkable* CGlobalEntityList<T>::GetServerNetworkable(int entnum) const {
	T* pUnk = (BaseClass::LookupEntityByNetworkIndex(entnum));
	if (pUnk)
		return pUnk->GetNetworkable();
	else
		return NULL;
}

template<class T>
IServerNetworkable* CGlobalEntityList<T>::GetServerNetworkableFromHandle(CBaseHandle hEnt) const {
	T* pUnk = (BaseClass::LookupEntity(hEnt));
	if (pUnk)
		return pUnk->GetNetworkable();
	else
		return NULL;
}

template<class T>
IServerUnknown* CGlobalEntityList<T>::GetServerUnknownFromHandle(CBaseHandle hEnt) const {
	return BaseClass::LookupEntity(hEnt);
}

template<class T>
IServerEntity* CGlobalEntityList<T>::GetServerEntity(int entnum) const {
	return BaseClass::LookupEntityByNetworkIndex(entnum);
}

template<class T>
IServerEntity* CGlobalEntityList<T>::GetServerEntityFromHandle(CBaseHandle hEnt) const {
	return BaseClass::LookupEntity(hEnt);
}

template<class T>
short		CGlobalEntityList<T>::GetNetworkSerialNumber(int iEntity) const {
	return BaseClass::GetNetworkSerialNumber(iEntity);
}

template<class T>
inline CBaseEntity* CGlobalEntityList<T>::GetBaseEntity( CBaseHandle hEnt ) const
{
	T *pUnk = (BaseClass::LookupEntity( hEnt ));
	if ( pUnk )
		return pUnk->GetBaseEntity();
	else
		return NULL;
}

template<class T>
inline CBaseEntity* CGlobalEntityList<T>::GetBaseEntity(int entnum) const
{
	T* pUnk = (BaseClass::LookupEntityByNetworkIndex(entnum));
	if (pUnk)
		return pUnk->GetBaseEntity();
	else
		return NULL;
}

template<class T>
CGlobalEntityList<T>::CGlobalEntityList()
{
	m_iHighestEnt = m_iNumEnts = m_iHighestEdicts = m_iNumEdicts = m_iNumReservedEdicts = 0;
	m_bClearingEntities = false;
	for (int i = 0; i < NUM_ENT_ENTRIES; i++)
	{
		m_EngineObjectArray[i] = NULL;
	}
	m_iMaxRagdolls = -1;
	m_LRUImportantRagdolls.RemoveAll();
	m_LRU.RemoveAll();
}

// mark an entity as deleted
template<class T>
void CGlobalEntityList<T>::AddToDeleteList(T* ent)
{
	if (ent && ent->GetRefEHandle() != INVALID_EHANDLE_INDEX)
	{
		m_DeleteList.AddToTail(ent);
	}
}

// call this before and after each frame to delete all of the marked entities.
template<class T>
void CGlobalEntityList<T>::CleanupDeleteList(void)
{
	VPROF("CGlobalEntityList::CleanupDeleteList");
	m_fInCleanupDelete = true;
	// clean up the vphysics delete list as well
	PhysOnCleanupDeleteList();
	m_bDisableEhandleAccess = true;
	for (int i = 0; i < m_DeleteList.Count(); i++)
	{
		DestroyEntity(m_DeleteList[i]);// ->Release();
	}
	m_bDisableEhandleAccess = false;
	m_DeleteList.RemoveAll();
	
	m_fInCleanupDelete = false;
}

template<class T>
int CGlobalEntityList<T>::ResetDeleteList(void)
{
	int result = m_DeleteList.Count();
	m_DeleteList.RemoveAll();
	return result;
}


template<class T>
void CGlobalEntityList<T>::Clear(void)
{
	m_bClearingEntities = true;

	// Add all remaining entities in the game to the delete list and call appropriate UpdateOnRemove
	CBaseHandle hCur = BaseClass::FirstHandle();
	while (hCur != BaseClass::InvalidHandle())
	{
		T* ent = GetBaseEntity(hCur);
		if (ent)
		{
			MDLCACHE_CRITICAL_SECTION();
			// Force UpdateOnRemove to be called
			UTIL_Remove(ent);
		}
		hCur = BaseClass::NextHandle(hCur);
	}

	CleanupDeleteList();
	// free the memory
	m_DeleteList.Purge();

	CBaseEntity::m_nDebugPlayer = -1;
	CBaseEntity::m_bInDebugSelect = false;
	m_iHighestEnt = 0;
	m_iNumEnts = 0;
	m_iHighestEdicts = 0;
	m_iNumEdicts = 0;
	m_iNumReservedEdicts = 0;

	m_bClearingEntities = false;
	BaseClass::Clear();
}

template<class T>
int CGlobalEntityList<T>::NumberOfEntities(void)
{
	return m_iNumEnts;
}

template<class T>
int CGlobalEntityList<T>::NumberOfEdicts(void)
{
	return m_iNumEdicts;
}

template<class T>
int CGlobalEntityList<T>::NumberOfReservedEdicts(void) {
	return m_iNumReservedEdicts;
}

template<class T>
int CGlobalEntityList<T>::IndexOfHighestEdict(void) {
	return m_iHighestEdicts;
}

template<class T>
CBaseEntity* CGlobalEntityList<T>::NextEnt(CBaseEntity* pCurrentEnt)
{
	if (!pCurrentEnt)
	{
		const CEntInfo<T>* pInfo = BaseClass::FirstEntInfo();
		if (!pInfo)
			return NULL;

		return (CBaseEntity*)pInfo->m_pEntity;
	}

	// Run through the list until we get a CBaseEntity.
	const CEntInfo<T>* pList = BaseClass::GetEntInfoPtr(pCurrentEnt->GetRefEHandle());
	if (pList)
		pList = BaseClass::NextEntInfo(pList);

	while (pList)
	{
#if 0
		if (pList->m_pEntity)
		{
			T* pUnk = (const_cast<T*>(pList->m_pEntity));
			CBaseEntity* pRet = pUnk->GetBaseEntity();
			if (pRet)
				return pRet;
		}
#else
		return (CBaseEntity*)pList->m_pEntity;
#endif
		pList = pList->m_pNext;
	}

	return NULL;

}

extern CAimTargetManager g_AimManager;

template<class T>
void CGlobalEntityList<T>::ReportEntityFlagsChanged(CBaseEntity* pEntity, unsigned int flagsOld, unsigned int flagsNow)
{
	if (pEntity->GetEngineObject()->IsMarkedForDeletion())
		return;
	// UNDONE: Move this into IEntityListener instead?
	unsigned int flagsChanged = flagsOld ^ flagsNow;
	if (flagsChanged & FL_AIMTARGET)
	{
		unsigned int flagsAdded = flagsNow & flagsChanged;
		unsigned int flagsRemoved = flagsOld & flagsChanged;

		if (flagsAdded & FL_AIMTARGET)
		{
			g_AimManager.AddEntity(pEntity);
		}
		if (flagsRemoved & FL_AIMTARGET)
		{
			g_AimManager.RemoveEntity(pEntity);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Used to confirm a pointer is a pointer to an entity, useful for
//			asserts.
//-----------------------------------------------------------------------------
template<class T>
bool CGlobalEntityList<T>::IsEntityPtr(void* pTest)
{
	if (pTest)
	{
		const CEntInfo<T>* pInfo = BaseClass::FirstEntInfo();
		for (; pInfo; pInfo = pInfo->m_pNext)
		{
			if (pTest == (void*)pInfo->m_pEntity)
				return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Iterates the entities with a given classname.
// Input  : pStartEntity - Last entity found, NULL to start a new iteration.
//			szName - Classname to search for.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByClassname(CBaseEntity* pStartEntity, const char* szName)
{
	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* pEntity = (CBaseEntity*)pInfo->m_pEntity;
		if (!pEntity)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (pEntity->ClassMatches(szName))
			return pEntity;
	}

	return NULL;
}

CBaseEntity* FindPickerEntity(CBasePlayer* pPlayer);

//-----------------------------------------------------------------------------
// Purpose: Finds an entity given a procedural name.
// Input  : szName - The procedural name to search for, should start with '!'.
//			pSearchingEntity - 
//			pActivator - The activator entity if this was called from an input
//				or Use handler.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityProcedural(const char* szName, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	//
	// Check for the name escape character.
	//
	if (szName[0] == '!')
	{
		const char* pName = szName + 1;

		//
		// It is a procedural name, look for the ones we understand.
		//
		if (FStrEq(pName, "player"))
		{
			return (CBaseEntity*)UTIL_PlayerByIndex(1);
		}
		else if (FStrEq(pName, "pvsplayer"))
		{
			if (pSearchingEntity)
			{
				return UTIL_FindClientInPVS(pSearchingEntity);
			}
			else if (pActivator)
			{
				// FIXME: error condition?
				return UTIL_FindClientInPVS(pActivator);
			}
			else
			{
				// FIXME: error condition?
				return (CBaseEntity*)UTIL_PlayerByIndex(1);
			}

		}
		else if (FStrEq(pName, "activator"))
		{
			return pActivator;
		}
		else if (FStrEq(pName, "caller"))
		{
			return pCaller;
		}
		else if (FStrEq(pName, "picker"))
		{
			return FindPickerEntity(UTIL_PlayerByIndex(1));
		}
		else if (FStrEq(pName, "self"))
		{
			return pSearchingEntity;
		}
		else
		{
			Warning("Invalid entity search name %s\n", szName);
			Assert(0);
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Iterates the entities with a given name.
// Input  : pStartEntity - Last entity found, NULL to start a new iteration.
//			szName - Name to search for.
//			pActivator - Activator entity if this was called from an input
//				handler or Use handler.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByName(CBaseEntity* pStartEntity, const char* szName, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller, IEntityFindFilter* pFilter)
{
	if (!szName || szName[0] == 0)
		return NULL;

	if (szName[0] == '!')
	{
		//
		// Avoid an infinite loop, only find one match per procedural search!
		//
		if (pStartEntity == NULL)
			return FindEntityProcedural(szName, pSearchingEntity, pActivator, pCaller);

		return NULL;
	}

	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (!ent->GetEngineObject()->GetEntityName())
			continue;

		if (ent->NameMatches(szName))
		{
			if (pFilter && !pFilter->ShouldFindEntity(ent))
				continue;

			return ent;
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pStartEntity - 
//			szModelName - 
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByModel(CBaseEntity* pStartEntity, const char* szModelName)
{
	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (ent->entindex()==-1 || !ent->GetEngineObject()->GetModelName())
			continue;

		if (FStrEq(STRING(ent->GetEngineObject()->GetModelName()), szModelName))
			return ent;
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Iterates the entities with a given target.
// Input  : pStartEntity - 
//			szName - 
//-----------------------------------------------------------------------------
// FIXME: obsolete, remove
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByTarget(CBaseEntity* pStartEntity, const char* szName)
{
	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (!ent->m_target)
			continue;

		if (FStrEq(STRING(ent->m_target), szName))
			return ent;
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Used to iterate all the entities within a sphere.
// Input  : pStartEntity - 
//			vecCenter - 
//			flRadius - 
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityInSphere(CBaseEntity* pStartEntity, const Vector& vecCenter, float flRadius)
{
	const CEntInfo<T>* pInfo = pStartEntity ? BaseClass::GetEntInfoPtr(pStartEntity->GetRefEHandle())->m_pNext : BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		if (ent->entindex()==-1)
			continue;

		Vector vecRelativeCenter;
		ent->GetEngineObject()->WorldToCollisionSpace(vecCenter, &vecRelativeCenter);
		if (!IsBoxIntersectingSphere(ent->GetEngineObject()->OBBMins(), ent->GetEngineObject()->OBBMaxs(), vecRelativeCenter, flRadius))
			continue;

		return ent;
	}

	// nothing found
	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds the nearest entity by name within a radius
// Input  : szName - Entity name to search for.
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByNameNearest(const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	CBaseEntity* pEntity = NULL;

	//
	// Check for matching class names within the search radius.
	//
	float flMaxDist2 = flRadius * flRadius;
	if (flMaxDist2 == 0)
	{
		flMaxDist2 = MAX_TRACE_LENGTH * MAX_TRACE_LENGTH;
	}

	CBaseEntity* pSearch = NULL;
	while ((pSearch = FindEntityByName(pSearch, szName, pSearchingEntity, pActivator, pCaller)) != NULL)
	{
		if (pSearch->entindex()==-1)
			continue;

		float flDist2 = (pSearch->GetEngineObject()->GetAbsOrigin() - vecSrc).LengthSqr();

		if (flMaxDist2 > flDist2)
		{
			pEntity = pSearch;
			flMaxDist2 = flDist2;
		}
	}

	return pEntity;
}



//-----------------------------------------------------------------------------
// Purpose: Finds the first entity by name within a radius
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity name to search for.
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByNameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	//
	// Check for matching class names within the search radius.
	//
	CBaseEntity* pEntity = pStartEntity;
	float flMaxDist2 = flRadius * flRadius;
	if (flMaxDist2 == 0)
	{
		return FindEntityByName(pEntity, szName, pSearchingEntity, pActivator, pCaller);
	}

	while ((pEntity = FindEntityByName(pEntity, szName, pSearchingEntity, pActivator, pCaller)) != NULL)
	{
		if (pEntity->entindex()==-1)
			continue;

		float flDist2 = (pEntity->GetEngineObject()->GetAbsOrigin() - vecSrc).LengthSqr();

		if (flMaxDist2 > flDist2)
		{
			return pEntity;
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds the nearest entity by class name withing given search radius.
// Input  : szName - Entity name to search for. Treated as a target name first,
//				then as an entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByClassnameNearest(const char* szName, const Vector& vecSrc, float flRadius)
{
	CBaseEntity* pEntity = NULL;

	//
	// Check for matching class names within the search radius.
	//
	float flMaxDist2 = flRadius * flRadius;
	if (flMaxDist2 == 0)
	{
		flMaxDist2 = MAX_TRACE_LENGTH * MAX_TRACE_LENGTH;
	}

	CBaseEntity* pSearch = NULL;
	while ((pSearch = FindEntityByClassname(pSearch, szName)) != NULL)
	{
		if (pSearch->entindex()==-1)
			continue;

		float flDist2 = (pSearch->GetEngineObject()->GetAbsOrigin() - vecSrc).LengthSqr();

		if (flMaxDist2 > flDist2)
		{
			pEntity = pSearch;
			flMaxDist2 = flDist2;
		}
	}

	return pEntity;
}



//-----------------------------------------------------------------------------
// Purpose: Finds the first entity within radius distance by class name.
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByClassnameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius)
{
	//
	// Check for matching class names within the search radius.
	//
	CBaseEntity* pEntity = pStartEntity;
	float flMaxDist2 = flRadius * flRadius;
	if (flMaxDist2 == 0)
	{
		return FindEntityByClassname(pEntity, szName);
	}

	while ((pEntity = FindEntityByClassname(pEntity, szName)) != NULL)
	{
		if (pEntity->entindex()==-1)
			continue;

		float flDist2 = (pEntity->GetEngineObject()->GetAbsOrigin() - vecSrc).LengthSqr();

		if (flMaxDist2 > flDist2)
		{
			return pEntity;
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds the first entity within an extent by class name.
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity class name, ie "info_target".
//			vecMins - Search mins.
//			vecMaxs - Search maxs.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityByClassnameWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecMins, const Vector& vecMaxs)
{
	//
	// Check for matching class names within the search radius.
	//
	CBaseEntity* pEntity = pStartEntity;

	while ((pEntity = FindEntityByClassname(pEntity, szName)) != NULL)
	{
		if (pEntity->IsNetworkable() && pEntity->entindex()==-1)
			continue;

		// check if the aabb intersects the search aabb.
		Vector entMins, entMaxs;
		pEntity->GetEngineObject()->WorldSpaceAABB(&entMins, &entMaxs);
		if (IsBoxIntersectingBox(vecMins, vecMaxs, entMins, entMaxs))
		{
			return pEntity;
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds an entity by target name or class name.
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity name to search for. Treated as a target name first,
//				then as an entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityGeneric(CBaseEntity* pStartEntity, const char* szName, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	CBaseEntity* pEntity = NULL;

	pEntity = FindEntityByName(pStartEntity, szName, pSearchingEntity, pActivator, pCaller);
	if (!pEntity)
	{
		pEntity = FindEntityByClassname(pStartEntity, szName);
	}

	return pEntity;
}


//-----------------------------------------------------------------------------
// Purpose: Finds the first entity by target name or class name within a radius
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity name to search for. Treated as a target name first,
//				then as an entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityGenericWithin(CBaseEntity* pStartEntity, const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	CBaseEntity* pEntity = NULL;

	pEntity = FindEntityByNameWithin(pStartEntity, szName, vecSrc, flRadius, pSearchingEntity, pActivator, pCaller);
	if (!pEntity)
	{
		pEntity = FindEntityByClassnameWithin(pStartEntity, szName, vecSrc, flRadius);
	}

	return pEntity;
}

//-----------------------------------------------------------------------------
// Purpose: Finds the nearest entity by target name or class name within a radius.
// Input  : pStartEntity - The entity to start from when doing the search.
//			szName - Entity name to search for. Treated as a target name first,
//				then as an entity class name, ie "info_target".
//			vecSrc - Center of search radius.
//			flRadius - Search radius for classname search, 0 to search everywhere.
//			pSearchingEntity - The entity that is doing the search.
//			pActivator - The activator entity if this was called from an input
//				or Use handler, NULL otherwise.
// Output : Returns a pointer to the found entity, NULL if none.
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityGenericNearest(const char* szName, const Vector& vecSrc, float flRadius, CBaseEntity* pSearchingEntity, CBaseEntity* pActivator, CBaseEntity* pCaller)
{
	CBaseEntity* pEntity = NULL;

	pEntity = FindEntityByNameNearest(szName, vecSrc, flRadius, pSearchingEntity, pActivator, pCaller);
	if (!pEntity)
	{
		pEntity = FindEntityByClassnameNearest(szName, vecSrc, flRadius);
	}

	return pEntity;
}


//-----------------------------------------------------------------------------
// Purpose: Find the nearest entity along the facing direction from the given origin
//			within the angular threshold (ignores worldspawn) with the
//			given classname.
// Input  : origin - 
//			facing - 
//			threshold - 
//			classname - 
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityClassNearestFacing(const Vector& origin, const Vector& facing, float threshold, char* classname)
{
	float bestDot = threshold;
	CBaseEntity* best_ent = NULL;

	const CEntInfo<T>* pInfo = BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		// FIXME: why is this skipping pointsize entities?
		if (ent->GetEngineObject()->IsPointSized())
			continue;

		// Make vector to entity
		Vector	to_ent = (ent->GetEngineObject()->GetAbsOrigin() - origin);

		VectorNormalize(to_ent);
		float dot = DotProduct(facing, to_ent);
		if (dot > bestDot)
		{
			if (FClassnameIs(ent, classname))
			{
				// Ignore if worldspawn
				if (!FClassnameIs(ent, "worldspawn") && !FClassnameIs(ent, "soundent"))
				{
					bestDot = dot;
					best_ent = ent;
				}
			}
		}
	}
	return best_ent;
}


//-----------------------------------------------------------------------------
// Purpose: Find the nearest entity along the facing direction from the given origin
//			within the angular threshold (ignores worldspawn)
// Input  : origin - 
//			facing - 
//			threshold - 
//-----------------------------------------------------------------------------
template<class T>
CBaseEntity* CGlobalEntityList<T>::FindEntityNearestFacing(const Vector& origin, const Vector& facing, float threshold)
{
	float bestDot = threshold;
	CBaseEntity* best_ent = NULL;

	const CEntInfo<T>* pInfo = BaseClass::FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* ent = (CBaseEntity*)pInfo->m_pEntity;
		if (!ent)
		{
			DevWarning("NULL entity in global entity list!\n");
			continue;
		}

		// Ignore logical entities
		if (!ent->IsNetworkable() || ent->entindex()==-1)
			continue;

		// Make vector to entity
		Vector	to_ent = ent->WorldSpaceCenter() - origin;
		VectorNormalize(to_ent);

		float dot = DotProduct(facing, to_ent);
		if (dot <= bestDot)
			continue;

		// Ignore if worldspawn
		if (!FStrEq(STRING(ent->GetEngineObject()->GetClassname()), "worldspawn") && !FStrEq(STRING(ent->GetEngineObject()->GetClassname()), "soundent"))
		{
			bestDot = dot;
			best_ent = ent;
		}
	}
	return best_ent;
}

template<class T>
void CGlobalEntityList<T>::AfterCreated(IHandleEntity* pEntity) {
	BaseClass::AddEntity((T*)pEntity);
}


template<class T>
void CGlobalEntityList<T>::BeforeDestroy(IHandleEntity* pEntity) {
	BaseClass::RemoveEntity((T*)pEntity);
}

template<class T>
void CGlobalEntityList<T>::OnAddEntity(T* pEnt, CBaseHandle handle)
{
	int i = handle.GetEntryIndex();

	// record current list details
	m_iNumEnts++;
	if (i > m_iHighestEnt)
		m_iHighestEnt = i;

	// If it's a CBaseEntity, notify the listeners.
	CBaseEntity* pBaseEnt = (pEnt)->GetBaseEntity();
	//m_EngineObjectArray[i] = new CEngineObjectInternal();
	m_EngineObjectArray[i]->Init(pBaseEnt);

	if (pBaseEnt->IsNetworkable()) {
		if (pBaseEnt->entindex() != -1)
			m_iNumEdicts++;
		if (BaseClass::IsReservedSlot(pBaseEnt->entindex())) {
			m_iNumReservedEdicts++;
		}
		if (pBaseEnt->entindex() > m_iHighestEdicts) {
			m_iHighestEdicts = pBaseEnt->entindex();
		}
	}

	BaseClass::OnAddEntity(pEnt, handle);
}

template<class T>
void CGlobalEntityList<T>::OnRemoveEntity(T* pEnt, CBaseHandle handle)
{
#ifdef DEBUG
	if (!m_fInCleanupDelete)
	{
		int i;
		for (i = 0; i < m_DeleteList.Count(); i++)
		{
			if (m_DeleteList[i] == pEnt)//->GetEntityHandle()
			{
				m_DeleteList.FastRemove(i);
				Msg("ERROR: Entity being destroyed but previously threaded on m_DeleteList\n");
				break;
			}
		}
	}
#endif
	int entnum = handle.GetEntryIndex();
	m_EngineObjectArray[entnum]->PhysicsRemoveTouchedList();
	m_EngineObjectArray[entnum]->PhysicsRemoveGroundList();
	m_EngineObjectArray[entnum]->DestroyAllDataObjects();
	delete m_EngineObjectArray[entnum];
	m_EngineObjectArray[entnum] = NULL;

	CBaseEntity* pBaseEnt = pEnt->GetBaseEntity();
	if (pBaseEnt->IsNetworkable()) {
		if (pBaseEnt->entindex() != -1)
			m_iNumEdicts--;
		if (BaseClass::IsReservedSlot(pBaseEnt->entindex())) {
			m_iNumReservedEdicts--;
		}
	}

	m_iNumEnts--;

	BaseClass::OnRemoveEntity(pEnt, handle);
}

template<class T>
void CGlobalEntityList<T>::AddDataAccessor(int type, IEntityDataInstantiator<T>* instantiator) {
	BaseClass::AddDataAccessor(type, instantiator);
}

template<class T>
void CGlobalEntityList<T>::RemoveDataAccessor(int type) {
	BaseClass::RemoveDataAccessor(type);
}

template<class T>
void* CGlobalEntityList<T>::GetDataObject(int type, const T* instance) {
	return BaseClass::GetDataObject(type, instance);
}

template<class T>
void* CGlobalEntityList<T>::CreateDataObject(int type, T* instance) {
	return BaseClass::CreateDataObject(type, instance);
}

template<class T>
void CGlobalEntityList<T>::DestroyDataObject(int type, T* instance) {
	BaseClass::DestroyDataObject(type, instance);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : seed - 
//-----------------------------------------------------------------------------
template<class T>
void CGlobalEntityList<T>::SetPredictionRandomSeed(const CUserCmd* cmd)
{
	if (!cmd)
	{
		m_nPredictionRandomSeed = -1;
		return;
	}

	m_nPredictionRandomSeed = (cmd->random_seed);
}

template<class T>
int CGlobalEntityList<T>::GetPredictionRandomSeed(void)
{
	return m_nPredictionRandomSeed;
}

template<class T>
IEngineObject* CGlobalEntityList<T>::GetPredictionPlayer(void)
{
	return m_pPredictionPlayer;
}

template<class T>
void CGlobalEntityList<T>::SetPredictionPlayer(IEngineObject* player)
{
	m_pPredictionPlayer = player;
}

extern ConVar	sv_alternateticks;
//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
template<class T>
bool CGlobalEntityList<T>::IsSimulatingOnAlternateTicks()
{
	if (gpGlobals->maxClients != 1)
	{
		return false;
	}

	return sv_alternateticks.GetBool();
}

extern ConVar g_ragdoll_important_maxcount;
//-----------------------------------------------------------------------------
// Move it to the top of the LRU
//-----------------------------------------------------------------------------
template<class T>
void CGlobalEntityList<T>::MoveToTopOfLRU(CBaseEntity* pRagdoll, bool bImportant)
{
	if (bImportant)
	{
		m_LRUImportantRagdolls.AddToTail(pRagdoll);

		if (m_LRUImportantRagdolls.Count() > g_ragdoll_important_maxcount.GetInt())
		{
			int iIndex = m_LRUImportantRagdolls.Head();

			CBaseEntity* pRagdoll = m_LRUImportantRagdolls[iIndex].Get();

			if (pRagdoll)
			{
#ifdef CLIENT_DLL
				pRagdoll->SUB_Remove();
#else
				pRagdoll->SUB_StartFadeOut(0);
#endif
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
void CGlobalEntityList<T>::UpdateRagdolls(float frametime) // EPISODIC VERSION
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
		CBaseEntity* pRagdoll = m_LRU[i].Get();
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
#ifdef CLIENT_DLL
					m_LRU[i]->SUB_Remove();
#else
					m_LRU[i]->SUB_StartFadeOut(0);
#endif

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
#ifdef CLIENT_DLL
	C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
#else
	CBasePlayer* pPlayer = UTIL_GetLocalPlayer();
#endif

	if (pPlayer && m_LRU.Count() > iMaxRagdollCount) // find the furthest one algorithm
	{
		Vector PlayerOrigin = pPlayer->GetEngineObject()->GetAbsOrigin();
		// const CBasePlayer *pPlayer = UTIL_GetLocalPlayer();

		for (i = m_LRU.Head(); i < m_LRU.InvalidIndex(); i = next)
		{
			CBaseEntity* pRagdoll = m_LRU[i].Get();

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

#ifdef CLIENT_DLL
		m_LRU[furthestOne]->SUB_Remove();
#else
		m_LRU[furthestOne]->SUB_StartFadeOut(0);
#endif

	}
	else // fall back on old-style pick the oldest one algorithm
	{
		for (i = m_LRU.Head(); i < m_LRU.InvalidIndex(); i = next)
		{
			if (m_LRU.Count() <= iMaxRagdollCount)
				break;

			next = m_LRU.Next(i);

			CBaseEntity* pRagdoll = m_LRU[i].Get();

			//Just ignore it until we're done burning/dissolving.
			IPhysicsObject* pObject = pRagdoll->GetEngineObject()->VPhysicsGetObject();
			if (pRagdoll && (pRagdoll->GetEffectEntity() || (pObject && !pObject->IsAsleep())))
				continue;

#ifdef CLIENT_DLL
			m_LRU[i]->SUB_Remove();
#else
			m_LRU[i]->SUB_StartFadeOut(0);
#endif
			m_LRU.Remove(i);
		}
	}
}

#else
template<class T>
void CGlobalEntityList<T>::UpdateRagdolls(float frametime) // Non-episodic version
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
		CBaseEntity* pRagdoll = m_LRU[i].Get();
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
#ifdef CLIENT_DLL
					m_LRU[i]->SUB_Remove();
#else
					m_LRU[i]->SUB_StartFadeOut(0);
#endif

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

		CBaseEntity* pRagdoll = m_LRU[i].Get();

		//Just ignore it until we're done burning/dissolving.
		if (pRagdoll && pRagdoll->GetEffectEntity())
			continue;

#ifdef CLIENT_DLL
		m_LRU[i]->SUB_Remove();
#else
		m_LRU[i]->SUB_StartFadeOut(0);
#endif
		m_LRU.Remove(i);
	}
}

#endif // HL2_EPISODIC

extern CGlobalEntityList<CBaseEntity> gEntList;

inline CGlobalEntityList<CBaseEntity>& ServerEntityList()
{
	return gEntList;
}

template<class T>
inline T* CHandle<T>::Get() const
{
#ifdef CLIENT_DLL
	//extern CBaseEntityList<IHandleEntity>* g_pEntityList;
	return (T*)g_pEntityList->LookupEntity(*this);
#endif // CLIENT_DLL
#ifdef GAME_DLL
	//extern CBaseEntityList<IHandleEntity>* g_pEntityList;
	return (T*)gEntList.LookupEntity(*this);
#endif // GAME_DLL
}

//-----------------------------------------------------------------------------
// Common finds
#if 0

template <class ENT_TYPE>
inline bool FindEntityByName( const char *pszName, ENT_TYPE **ppResult)
{
	CBaseEntity *pBaseEntity = gEntList.FindEntityByName( NULL, pszName );
	
	if ( pBaseEntity )
		*ppResult = dynamic_cast<ENT_TYPE *>( pBaseEntity );
	else
		*ppResult = NULL;

	return ( *ppResult != NULL );
}

template <>
inline bool FindEntityByName<CBaseEntity>( const char *pszName, CBaseEntity **ppResult)
{
	*ppResult = gEntList.FindEntityByName( NULL, pszName );
	return ( *ppResult != NULL );
}

template <>
inline bool FindEntityByName<CAI_BaseNPC>( const char *pszName, CAI_BaseNPC **ppResult)
{
	CBaseEntity *pBaseEntity = gEntList.FindEntityByName( NULL, pszName );
	
	if ( pBaseEntity )
		*ppResult = pBaseEntity->MyNPCPointer();
	else
		*ppResult = NULL;

	return ( *ppResult != NULL );
}
#endif
//-----------------------------------------------------------------------------
// Purpose: Simple object for storing a list of objects
//-----------------------------------------------------------------------------
struct entitem_t
{
	EHANDLE hEnt;
	struct entitem_t *pNext;

	// uses pool memory
	static void* operator new( size_t stAllocateBlock );
	static void *operator new( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine );
	static void operator delete( void *pMem );
	static void operator delete( void *pMem, int nBlockUse, const char *pFileName, int nLine ) { operator delete( pMem ); }
};

class CEntityList
{
public:
	CEntityList();
	~CEntityList();

	int m_iNumItems;
	entitem_t *m_pItemList;	// null terminated singly-linked list

	void AddEntity( CBaseEntity * );
	void DeleteEntity( CBaseEntity * );
};



struct notify_teleport_params_t
{
	Vector prevOrigin;
	QAngle prevAngles;
	bool physicsRotate;
};

struct notify_destroy_params_t
{
};

struct notify_system_event_params_t
{
	union
	{
		const notify_teleport_params_t *pTeleport;
		const notify_destroy_params_t *pDestroy;
	};
	notify_system_event_params_t( const notify_teleport_params_t *pInTeleport ) { pTeleport = pInTeleport; }
	notify_system_event_params_t( const notify_destroy_params_t *pInDestroy ) { pDestroy = pInDestroy; }
};


abstract_class INotify
{
public:
	// Add notification for an entity
	virtual void AddEntity( CBaseEntity *pNotify, CBaseEntity *pWatched ) = 0;

	// Remove notification for an entity
	virtual void RemoveEntity( CBaseEntity *pNotify, CBaseEntity *pWatched ) = 0;

	// Call the named input in each entity who is watching pEvent's status
	virtual void ReportNamedEvent( CBaseEntity *pEntity, const char *pEventName ) = 0;

	// System events don't make sense as inputs, so are handled through a generic notify function
	virtual void ReportSystemEvent( CBaseEntity *pEntity, notify_system_event_t eventType, const notify_system_event_params_t &params ) = 0;

	inline void ReportDestroyEvent( CBaseEntity *pEntity )
	{
		notify_destroy_params_t destroy;
		ReportSystemEvent( pEntity, NOTIFY_EVENT_DESTROY, notify_system_event_params_t(&destroy) );
	}
	
	inline void ReportTeleportEvent( CBaseEntity *pEntity, const Vector &prevOrigin, const QAngle &prevAngles, bool physicsRotate )
	{
		notify_teleport_params_t teleport;
		teleport.prevOrigin = prevOrigin;
		teleport.prevAngles = prevAngles;
		teleport.physicsRotate = physicsRotate;
		ReportSystemEvent( pEntity, NOTIFY_EVENT_TELEPORT, notify_system_event_params_t(&teleport) );
	}
	
	// Remove this entity from the notify list
	virtual void ClearEntity( CBaseEntity *pNotify ) = 0;
};



// singleton
extern INotify *g_pNotify;

int AimTarget_ListCount();
int AimTarget_ListCopy( CBaseEntity *pList[], int listMax );
void AimTarget_ForceRepopulateList();

void SimThink_EntityChanged( CBaseEntity *pEntity );
int SimThink_ListCount();
int SimThink_ListCopy( CBaseEntity *pList[], int listMax );

#endif // ENTITYLIST_H