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

//class C_Beam;
//class C_BaseViewModel;
//class C_BaseEntity;


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
	}

	void Init(C_BaseEntity* pOuter) {
		m_pOuter = pOuter;
	}

	C_BaseEntity* GetOuter() {
		return m_pOuter;
	}

	void					ParseMapData(IEntityMapData* mapData);
	bool KeyValue(const char* szKeyName, const char* szValue);
	// NOTE: Setting the abs velocity in either space will cause a recomputation
	// in the other space, so setting the abs velocity will also set the local vel
	void				SetAbsVelocity(const Vector& vecVelocity);
	Vector& GetAbsVelocity();
	const Vector& GetAbsVelocity() const;

	// Sets abs angles, but also sets local angles to be appropriate
	void				SetAbsOrigin(const Vector& origin);
	Vector& GetAbsOrigin(void);
	const Vector& GetAbsOrigin(void) const;

	void				SetAbsAngles(const QAngle& angles);
	QAngle& GetAbsAngles(void);
	const QAngle& GetAbsAngles(void) const;

	void				SetLocalOrigin(const Vector& origin);
	void				SetLocalOriginDim(int iDim, vec_t flValue);
	const Vector& GetLocalOrigin(void) const;
	vec_t				GetLocalOriginDim(int iDim) const;		// You can use the X_INDEX, Y_INDEX, and Z_INDEX defines here.

	void				SetLocalAngles(const QAngle& angles);
	void				SetLocalAnglesDim(int iDim, vec_t flValue);
	const QAngle& GetLocalAngles(void) const;
	vec_t				GetLocalAnglesDim(int iDim) const;		// You can use the X_INDEX, Y_INDEX, and Z_INDEX defines here.

	void				SetLocalVelocity(const Vector& vecVelocity);
	Vector& GetLocalVelocity();
	const Vector& GetLocalVelocity() const;

	const Vector& GetPrevLocalOrigin() const;
	const QAngle& GetPrevLocalAngles() const;

	ITypedInterpolatedVar< QAngle >& GetRotationInterpolator();
	ITypedInterpolatedVar< Vector >& GetOriginInterpolator();

	// Determine approximate velocity based on updates from server
	void					EstimateAbsVelocity(Vector& vel);
	// Computes absolute position based on hierarchy
	void CalcAbsolutePosition();
	void CalcAbsoluteVelocity();

	// Unlinks from hierarchy
	// Set the movement parent. Your local origin and angles will become relative to this parent.
	// If iAttachment is a valid attachment on the parent, then your local origin and angles 
	// are relative to the attachment on this entity.
	void SetParent(IEngineObjectClient* pParentEntity, int iParentAttachment = 0);
	void UnlinkChild(IEngineObjectClient* pParent, IEngineObjectClient* pChild);
	void LinkChild(IEngineObjectClient* pParent, IEngineObjectClient* pChild);
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
	void							EntityToWorldSpace(const Vector& in, Vector* pOut) const;
	void							WorldToEntitySpace(const Vector& in, Vector* pOut) const;

	void							GetVectors(Vector* forward, Vector* right, Vector* up) const;

	// This function gets your parent's transform. If you're parented to an attachment,
// this calculates the attachment's transform and gives you that.
//
// You must pass in tempMatrix for scratch space - it may need to fill that in and return it instead of 
// pointing you right at a variable in your parent.
	matrix3x4_t& GetParentToWorldTransform(matrix3x4_t& tempMatrix);

	// Computes the abs position of a point specified in local space
	void				ComputeAbsPosition(const Vector& vecLocalPosition, Vector* pAbsPosition);

	// Computes the abs position of a direction specified in local space
	void				ComputeAbsDirection(const Vector& vecLocalDirection, Vector* pAbsDirection);

public:

	void AddVar(void* data, IInterpolatedVar* watcher, int type, bool bSetup = false);
	void RemoveVar(void* data, bool bAssert = true);
	VarMapping_t* GetVarMapping();

	// Set appropriate flags and store off data when these fields are about to change
	void							OnLatchInterpolatedVariables(int flags);
	// For predictable entities, stores last networked value
	void							OnStoreLastNetworkedValue();

	void							Interp_SetupMappings(VarMapping_t* map);

	// Returns 1 if there are no more changes (ie: we could call RemoveFromInterpolationList).
	int								Interp_Interpolate(VarMapping_t* map, float currentTime);

	void							Interp_RestoreToLastNetworked(VarMapping_t* map);
	void							Interp_UpdateInterpolationAmounts(VarMapping_t* map);
	void							Interp_Reset(VarMapping_t* map);
	void							Interp_HierarchyUpdateInterpolationAmounts();



	// Returns INTERPOLATE_STOP or INTERPOLATE_CONTINUE.
	// bNoMoreChanges is set to 1 if you can call RemoveFromInterpolationList on the entity.
	int BaseInterpolatePart1(float& currentTime, Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int& bNoMoreChanges);
	void BaseInterpolatePart2(Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int nChangeFlags);

	void							AllocateIntermediateData(void);
	void							DestroyIntermediateData(void);
	void							ShiftIntermediateDataForward(int slots_to_remove, int previous_last_slot);

	void* GetPredictedFrame(int framenumber);
	void* GetOuterPredictedFrame(int framenumber);
	void* GetOriginalNetworkDataObject(void);
	void* GetOuterOriginalNetworkDataObject(void);
	bool							IsIntermediateDataAllocated(void) const;

	void							PreEntityPacketReceived(int commands_acknowledged);
	void							PostEntityPacketReceived(void);
	bool							PostNetworkDataReceived(int commands_acknowledged);

	int								SaveData(const char* context, int slot, int type);
	int								RestoreData(const char* context, int slot, int type);

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
	unsigned char			GetParentAttachment() const;
	unsigned char GetParentAttachment() {
		return m_iParentAttachment;
	}
	void SetParentAttachment(unsigned char iParentAttachment) {
		m_iParentAttachment = iParentAttachment;
	}

#if !defined( NO_ENTITY_PREDICTION )
	// For storing prediction results and pristine network state
	byte* m_pIntermediateData[MULTIPLAYER_BACKUP];
	byte* m_pOriginalData = NULL;
	byte* m_pOuterIntermediateData[MULTIPLAYER_BACKUP];
	byte* m_pOuterOriginalData = NULL;
	int								m_nIntermediateDataCount = 0;

	//bool							m_bIsPlayerSimulated;
#endif

	void					Clear(void) {
		m_pOriginalData = NULL;
		m_pOuterOriginalData = NULL;
		for (int i = 0; i < MULTIPLAYER_BACKUP; i++) {
			m_pIntermediateData[i] = NULL;
			m_pOuterIntermediateData[i] = NULL;
		}
	}

	VarMapping_t	m_VarMap;
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

	unsigned int testNetwork;

	// Last values to come over the wire. Used for interpolation.
	Vector							m_vecNetworkOrigin = Vector(0, 0, 0);
	QAngle							m_angNetworkAngles = QAngle(0, 0, 0);
	// The moveparent received from networking data
	CHandle<C_BaseEntity>			m_hNetworkMoveParent = NULL;
	unsigned char					m_iParentAttachment; // 0 if we're relative to the parent's absorigin and absangles.

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
//friend class C_AllBaseEntityIterator;
typedef CBaseEntityList<T> BaseClass;
public:
	// Constructor, destructor
								CClientEntityList( void );
	virtual 					~CClientEntityList( void );

	void						Release();		// clears everything and releases entities


// Implement IClientEntityList
public:

	virtual C_BaseEntity*		CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1);
	virtual void				DestroyEntity(IHandleEntity* pEntity);

	IEngineObjectClient* GetEngineObject(int entnum);
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
	C_BaseEntity* FirstBaseEntity() const;
	C_BaseEntity* NextBaseEntity( C_BaseEntity *pEnt ) const;

	

	// Get the list of all PVS notifiers.
	CUtlLinkedList<CPVSNotifyInfo,unsigned short>& GetPVSNotifiers();

	CUtlVector<IClientEntityListener *>	m_entityListeners;

	// add a class that gets notified of entity events
	void AddListenerEntity( IClientEntityListener *pListener );
	void RemoveListenerEntity( IClientEntityListener *pListener );

	void NotifyCreateEntity( C_BaseEntity *pEnt );
	void NotifyRemoveEntity( C_BaseEntity *pEnt );

private:

	// Cached info for networked entities.
	//struct EntityCacheInfo_t
	//{
	//	// Cached off because GetClientNetworkable is called a *lot*
	//	IClientNetworkable *m_pNetworkable;
	//	unsigned short m_BaseEntitiesIndex;	// Index into m_BaseEntities (or m_BaseEntities.InvalidIndex() if none).
	//};

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

private:

	void AddPVSNotifier( IClientUnknown *pUnknown );
	void RemovePVSNotifier( IClientUnknown *pUnknown );
	
	// These entities want to know when they enter and leave the PVS (server entities
	// already can get the equivalent notification with NotifyShouldTransmit, but client
	// entities have to get it this way).
	CUtlLinkedList<CPVSNotifyInfo,unsigned short> m_PVSNotifyInfos;
	CUtlMap<IClientUnknown*,unsigned short,unsigned short> m_PVSNotifierMap;	// Maps IClientUnknowns to indices into m_PVSNotifyInfos.
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
#ifdef _DEBUG
	m_EngineObjectArray[entnum]->SetAbsOrigin(vec3_origin);
	m_EngineObjectArray[entnum]->SetAbsAngles(vec3_angle);
	m_EngineObjectArray[entnum]->GetAbsOrigin().Init();
	//	m_vecAbsAngVelocity.Init();
	m_EngineObjectArray[entnum]->GetLocalVelocity().Init();
	m_EngineObjectArray[entnum]->GetAbsVelocity().Init();
#endif
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

