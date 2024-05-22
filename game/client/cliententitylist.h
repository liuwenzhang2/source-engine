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
#include "c_baseentity.h"
#include "gamestringpool.h"
#include "saverestoretypes.h"
#include "saverestore.h"

//class C_Beam;
//class C_BaseViewModel;
//class C_BaseEntity;

extern IVEngineClient* engine;

#define INPVS_YES			0x0001		// The entity thinks it's in the PVS.
#define INPVS_THISFRAME		0x0002		// Accumulated as different views are rendered during the frame and used to notify the entity if
										// it is not in the PVS anymore (at the end of the frame).
#define INPVS_NEEDSNOTIFY	0x0004		// The entity thinks it's in the PVS.
							   
// Implement this class and register with entlist to receive entity create/delete notification
class IClientEntityListener
{
public:
	virtual void OnEntityCreated(C_BaseEntity* pEntity) {};
	//virtual void OnEntitySpawned( C_BaseEntity *pEntity ) {};
	virtual void OnEntityDeleted(C_BaseEntity* pEntity) {};
};

abstract_class C_BaseEntityClassList
{
public:
	C_BaseEntityClassList();
	~C_BaseEntityClassList();
	virtual void LevelShutdown() = 0;

	C_BaseEntityClassList *m_pNextClassList;
};

template< class T >
class C_EntityClassList : public C_BaseEntityClassList
{
public:
	virtual void LevelShutdown()  { m_pClassList = NULL; }

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


// Maximum size of entity list
#define INVALID_CLIENTENTITY_HANDLE CBaseHandle( INVALID_EHANDLE_INDEX )

class CPVSNotifyInfo
{
public:
	IPVSNotify* m_pNotify;
	IClientRenderable* m_pRenderable;
	unsigned char m_InPVSStatus;				// Combination of the INPVS_ flags.
	unsigned short m_PVSNotifiersLink;			// Into m_PVSNotifyInfos.
};

class C_EngineObjectInternal : public IEngineObjectClient, public IClientNetworkable {
public:
	DECLARE_CLASS_NOBASE(C_EngineObjectInternal);
	DECLARE_PREDICTABLE();
	// data description
	DECLARE_DATADESC();
	DECLARE_CLIENTCLASS();

	int	entindex() const;
	RecvTable* GetRecvTable();
	//ClientClass* GetClientClass() { return NULL; }
	void* GetDataTableBasePtr() { return this; }
	void NotifyShouldTransmit(ShouldTransmitState_t state) {}
	void OnPreDataChanged(DataUpdateType_t updateType) {}
	void OnDataChanged(DataUpdateType_t updateType) {}
	void PreDataUpdate(DataUpdateType_t updateType) {}
	void PostDataUpdate(DataUpdateType_t updateType) {}
	bool IsDormant(void) { return false; }
	void ReceiveMessage(int classID, bf_read& msg) {}
	void SetDestroyedOnRecreateEntities(void) {}

	// memory handling, uses calloc so members are zero'd out on instantiation
	void* operator new(size_t stAllocateBlock);
	void* operator new[](size_t stAllocateBlock);
	void* operator new(size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine);
	void* operator new[](size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine);
	void operator delete(void* pMem);
	void operator delete(void* pMem, int nBlockUse, const char* pFileName, int nLine) { operator delete(pMem); }

	C_EngineObjectInternal() :
		m_iv_vecOrigin("C_BaseEntity::m_iv_vecOrigin"),
		m_iv_angRotation("C_BaseEntity::m_iv_angRotation"),
		m_iv_vecVelocity("C_BaseEntity::m_iv_vecVelocity")
	{
		AddVar(&m_vecOrigin, &m_iv_vecOrigin, LATCH_SIMULATION_VAR);
		AddVar(&m_angRotation, &m_iv_angRotation, LATCH_SIMULATION_VAR);

#ifdef _DEBUG
		m_vecAbsOrigin = vec3_origin;
		m_angAbsRotation = vec3_angle;
		m_vecNetworkOrigin.Init();
		m_angNetworkAngles.Init();
		m_vecAbsOrigin.Init();
		//	m_vecAbsAngVelocity.Init();
		m_vecVelocity.Init();
		m_vecAbsVelocity.Init();
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
	}

	void Init(C_BaseEntity* pOuter) {
		m_pOuter = pOuter;
	}

	C_BaseEntity* GetOuter() {
		return m_pOuter;
	}

	void ParseMapData(IEntityMapData* mapData);
	bool KeyValue(const char* szKeyName, const char* szValue);
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
	Vector& GetLocalVelocity();
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

	virtual void ResetRgflCoordinateFrame();
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

	void AddVar(void* data, IInterpolatedVar* watcher, int type, bool bSetup = false);
	void RemoveVar(void* data, bool bAssert = true);
	VarMapping_t* GetVarMapping();

	// Set appropriate flags and store off data when these fields are about to change
	void OnLatchInterpolatedVariables(int flags);
	// For predictable entities, stores last networked value
	void OnStoreLastNetworkedValue();

	void Interp_SetupMappings(VarMapping_t* map);

	// Returns 1 if there are no more changes (ie: we could call RemoveFromInterpolationList).
	int Interp_Interpolate(VarMapping_t* map, float currentTime);

	void Interp_RestoreToLastNetworked(VarMapping_t* map);
	void Interp_UpdateInterpolationAmounts(VarMapping_t* map);
	void Interp_Reset(VarMapping_t* map);
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
	}

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


private:

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

class C_BaseEntityIterator
{
public:
	C_BaseEntityIterator();

	void Restart();
	C_BaseEntity* Next();	// keep calling this until it returns null.

private:
	bool start = false;
	CBaseHandle m_CurBaseEntity;
};

//
// This is the IClientEntityList implemenation. It serves two functions:
//
// 1. It converts server entity indices into IClientNetworkables for the engine.
//
// 2. It provides a place to store IClientUnknowns and gives out ClientEntityHandle_t's
//    so they can be indexed and retreived. For example, this is how static props are referenced
//    by the spatial partition manager - it doesn't know what is being inserted, so it's 
//	  given ClientEntityHandle_t's, and the handlers for spatial partition callbacks can
//    use the client entity list to look them up and check for supported interfaces.
//
template<class T>// = IHandleEntity
class CClientEntityList : public CBaseEntityList<T>, public IClientEntityList, public IEntityCallBack
{
	friend class C_BaseEntityIterator;
	friend class C_EngineObjectInternal;
	//friend class C_AllBaseEntityIterator;
	typedef CBaseEntityList<T> BaseClass;
public:
	// Constructor, destructor
								CClientEntityList( void );
	virtual 					~CClientEntityList( void );

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
	virtual IClientNetworkable*	GetClientNetworkable( int entnum );
	virtual IClientEntity*		GetClientEntity( int entnum );

	virtual int					NumberOfEntities( bool bIncludeNonNetworkable = false );

	virtual T*					GetClientUnknownFromHandle( ClientEntityHandle_t hEnt );
	virtual IClientNetworkable*	GetClientNetworkableFromHandle( ClientEntityHandle_t hEnt );
	virtual IClientEntity*		GetClientEntityFromHandle( ClientEntityHandle_t hEnt );

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

	IClientRenderable*		GetClientRenderableFromHandle( ClientEntityHandle_t hEnt );
	C_BaseEntity*			GetBaseEntityFromHandle( ClientEntityHandle_t hEnt );
	ICollideable*			GetCollideableFromHandle( ClientEntityHandle_t hEnt );
	IClientThinkable*		GetClientThinkableFromHandle( ClientEntityHandle_t hEnt );

	// Convenience methods to convert between entindex + ClientEntityHandle_t
	ClientEntityHandle_t	EntIndexToHandle( int entnum );
	int						HandleToEntIndex( ClientEntityHandle_t handle );

	// Is a handle valid?
	bool					IsHandleValid( ClientEntityHandle_t handle ) const;

	// For backwards compatibility...
	C_BaseEntity*			GetEnt( int entnum ) { return GetBaseEntity( entnum ); }

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

private:
	void AddPVSNotifier(IClientUnknown* pUnknown);
	void RemovePVSNotifier(IClientUnknown* pUnknown);
	void AddRestoredEntity(T* pEntity);

public:
	static bool				sm_bDisableTouchFuncs;	// Disables PhysicsTouch and PhysicsStartTouch function calls
private:
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

};

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
		C_BaseAnimating::AutoAllowBoneAccess boneaccess(true, true);

		int last = GetHighestEntityIndex();
		ClientEntityHandle_t iter = BaseClass::FirstHandle();

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
	}
	SaveInitEntities(pSaveData);
}

template<class T>
void CClientEntityList<T>::SaveEntityOnTable(T* pEntity, CSaveRestoreData* pSaveData, int& iSlot)
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

#if defined( CLIENT_DLL )
	ClientEntityHandle_t iter = BaseClass::FirstHandle();

	while (iter != BaseClass::InvalidHandle())
	{
		pEnt = GetBaseEntityFromHandle(iter);

		if (pEnt && pEnt->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE)
		{
			SaveEntityOnTable(pEnt, pSaveData, i);
		}

		iter = BaseClass::NextHandle(iter);
	}
#endif

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
		pSave->WriteFields("ETABLE", pSaveData->GetEntityInfo(i), NULL, entitytable_t::m_DataMap.dataDesc, entitytable_t::m_DataMap.dataNumFields);
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
		pRestore->ReadFields("ETABLE", pEntityTable, NULL, entitytable_t::m_DataMap.dataDesc, entitytable_t::m_DataMap.dataNumFields);
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
	CBaseEntity* pent;

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
	return (C_BaseEntity*)EntityFactoryDictionary()->Create(this, className, iForceEdictIndex, iSerialNum, this);
}

template<class T>
inline void	CClientEntityList<T>::DestroyEntity(IHandleEntity* pEntity) {
	EntityFactoryDictionary()->Destroy(pEntity);
}

//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
template<class T>
inline bool	CClientEntityList<T>::IsHandleValid( ClientEntityHandle_t handle ) const
{
	return BaseClass::LookupEntity(handle) != NULL;
}

template<class T>
inline T* CClientEntityList<T>::GetListedEntity( int entnum )
{
	return BaseClass::LookupEntityByNetworkIndex( entnum );
}

template<class T>
inline T* CClientEntityList<T>::GetClientUnknownFromHandle( ClientEntityHandle_t hEnt )
{
	return BaseClass::LookupEntity( hEnt );
}

template<class T>
inline CUtlLinkedList<CPVSNotifyInfo,unsigned short>& CClientEntityList<T>::GetPVSNotifiers()//CClientEntityList<T>::
{
	return m_PVSNotifyInfos;
}


//-----------------------------------------------------------------------------
// Convenience methods to convert between entindex + ClientEntityHandle_t
//-----------------------------------------------------------------------------
template<class T>
inline ClientEntityHandle_t CClientEntityList<T>::EntIndexToHandle( int entnum )
{
	if ( entnum < -1 )
		return INVALID_EHANDLE_INDEX;
	T *pUnk = GetListedEntity( entnum );
	return pUnk ? pUnk->GetRefEHandle() : INVALID_EHANDLE_INDEX; 
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

//-----------------------------------------------------------------------------
// Purpose: Clears all entity lists and releases entities
//-----------------------------------------------------------------------------
template<class T>
void CClientEntityList<T>::Release(void)
{
	// Free all the entities.
	ClientEntityHandle_t iter = BaseClass::FirstHandle();
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
}

template<class T>
inline IEngineObjectClient* CClientEntityList<T>::GetEngineObject(int entnum) {
	if (entnum < 0 || entnum >= NUM_ENT_ENTRIES) {
		return NULL;
	}
	return m_EngineObjectArray[entnum];
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
// Convenience methods to convert between entindex + ClientEntityHandle_t
//-----------------------------------------------------------------------------
template<class T>
int CClientEntityList<T>::HandleToEntIndex(ClientEntityHandle_t handle)
{
	if (handle == INVALID_EHANDLE_INDEX)
		return -1;
	C_BaseEntity* pEnt = GetBaseEntityFromHandle(handle);
	return pEnt ? pEnt->entindex() : -1;
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
IClientNetworkable* CClientEntityList<T>::GetClientNetworkableFromHandle(ClientEntityHandle_t hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetClientNetworkable() : 0;
}

template<class T>
IClientEntity* CClientEntityList<T>::GetClientEntityFromHandle(ClientEntityHandle_t hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetIClientEntity() : 0;
}

template<class T>
IClientRenderable* CClientEntityList<T>::GetClientRenderableFromHandle(ClientEntityHandle_t hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetClientRenderable() : 0;
}

template<class T>
C_BaseEntity* CClientEntityList<T>::GetBaseEntityFromHandle(ClientEntityHandle_t hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetBaseEntity() : 0;
}

template<class T>
ICollideable* CClientEntityList<T>::GetCollideableFromHandle(ClientEntityHandle_t hEnt)
{
	T* pEnt = GetClientUnknownFromHandle(hEnt);
	return pEnt ? pEnt->GetCollideable() : 0;
}

template<class T>
IClientThinkable* CClientEntityList<T>::GetClientThinkableFromHandle(ClientEntityHandle_t hEnt)
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

	// If this thing wants PVS notifications, hook it up.
	AddPVSNotifier(pUnknown);

	// Store it in a special list for fast iteration if it's a C_BaseEntity.
	C_BaseEntity* pBaseEntity = pUnknown->GetBaseEntity();
	m_EngineObjectArray[entnum] = new C_EngineObjectInternal();
	m_EngineObjectArray[entnum]->Init(pBaseEntity);

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


}

template<class T>
void CClientEntityList<T>::OnRemoveEntity(T* pEnt, CBaseHandle handle)
{
	int entnum = handle.GetEntryIndex();

	m_EngineObjectArray[entnum]->PhysicsRemoveTouchedList();
	m_EngineObjectArray[entnum]->PhysicsRemoveGroundList();
	m_EngineObjectArray[entnum]->DestroyAllDataObjects();
	delete m_EngineObjectArray[entnum];
	m_EngineObjectArray[entnum] = NULL;

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

	//if (pCache->m_BaseEntitiesIndex != m_BaseEntities.InvalidIndex())
	//	m_BaseEntities.Remove(pCache->m_BaseEntitiesIndex);

	//pCache->m_BaseEntitiesIndex = m_BaseEntities.InvalidIndex();
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

//-----------------------------------------------------------------------------
// Returns the client entity list
//-----------------------------------------------------------------------------
//template<class T>
extern CClientEntityList<C_BaseEntity> *cl_entitylist;

inline CClientEntityList<C_BaseEntity>& ClientEntityList()
{
	return *cl_entitylist;
}

template<class T>
inline T* CHandle<T>::Get() const
{
#ifdef CLIENT_DLL
	//extern CBaseEntityList<IHandleEntity>* g_pEntityList;
	return (T*)ClientEntityList().LookupEntity(*this);
#endif // CLIENT_DLL
#ifdef GAME_DLL
	//extern CBaseEntityList<IHandleEntity>* g_pEntityList;
	return (T*)g_pEntityList->LookupEntity(*this);
#endif // GAME_DLL
}


#endif // CLIENTENTITYLIST_H

