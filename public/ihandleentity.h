//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef IHANDLEENTITY_H
#define IHANDLEENTITY_H
#ifdef _WIN32
#pragma once
#endif
#include "platform.h"
#include "const.h"

class IHandleEntity;
class CBaseHandle;
class IEntityFactory;
class IEntityList;
class datamap_t;
struct string_t;
class Vector;
class QAngle;
class VMatrix;
struct Ray_t;
struct PS_SD_Static_SurfaceProperties_t;
class ITraceFilter;
class CGameTrace;
typedef CGameTrace trace_t;
struct cplane_t;
class IPhysicsObject;
class IEngineWorld;
class IEnginePlayer;
class IEnginePortal;
class IEngineShadowClone;
class IEngineVehicle;
class IEngineRope;
class IEngineGhost;

class IEngineObject {
public:
	virtual datamap_t* GetDataDescMap(void) = 0;
	virtual const CBaseHandle& GetRefEHandle() const = 0;
	virtual IEntityList* GetEntityList() const = 0;
	virtual int entindex() const = 0;
	virtual const string_t& GetClassname() const = 0;
	virtual int GetFlags(void) const = 0;
	virtual bool IsEFlagSet(int nEFlagMask) const = 0;
	virtual IEngineObject* GetMoveParent(void) const = 0;
	//virtual void SetMoveParent(IEngineObjectServer* hMoveParent) = 0;
	virtual IEngineObject* GetRootMoveParent() = 0;
	virtual IEngineObject* FirstMoveChild(void) const = 0;
	//virtual void SetFirstMoveChild(IEngineObjectServer* hMoveChild) = 0;
	virtual IEngineObject* NextMovePeer(void) const = 0;
	virtual int GetModelIndex(void) const = 0;
	virtual string_t GetModelName(void) const = 0;
	virtual void AddSolidFlags(int flags) = 0;
	virtual SolidType_t GetSolid() const = 0;
	virtual bool IsSolidFlagSet(int flagMask) const = 0;
	virtual bool IsMarkedForDeletion(void) = 0;
	virtual void CollisionRulesChanged() = 0;
	virtual const Vector& GetAbsOrigin(void) const = 0;
	virtual const QAngle& GetAbsAngles(void) const = 0;
	virtual void GetVectors(Vector* forward, Vector* right, Vector* up) const = 0;
	virtual IHandleEntity* GetHandleEntity() const = 0;
	virtual const Vector& WorldAlignMins() const = 0;
	virtual const Vector& WorldAlignMaxs() const = 0;
	virtual const Vector& WorldAlignSize() const = 0;
	virtual void WorldSpaceAABB(Vector* pWorldMins, Vector* pWorldMaxs) const = 0;
	virtual const Vector& OBBMins() const = 0;
	virtual const Vector& OBBMaxs() const = 0;
	virtual const Vector& OBBSize() const = 0;
	virtual const Vector& GetCollisionOrigin() const = 0;
	virtual const QAngle& GetCollisionAngles() const = 0;
	virtual MoveType_t GetMoveType() const = 0;
	virtual IPhysicsObject* VPhysicsGetObject(void) const = 0;
	virtual bool IsRagdoll() const = 0;

	virtual bool IsWorld() = 0;
	virtual bool IsPlayer() = 0;
	virtual bool IsPortal() = 0;
	virtual bool IsShadowClone() = 0;
	virtual bool IsVehicle() = 0;
	virtual bool IsRope() = 0;
	virtual bool IsGhost() = 0;
};

class IEngineWorld {
public:

};

class IEnginePlayer {
public:

};

class IEnginePortal {
public:
	virtual bool IsActivated() const = 0;
	virtual bool IsPortal2() const = 0;
	virtual bool IsActivedAndLinked(void) const = 0;
	virtual bool IsReadyToSimulate(void) const = 0;
	virtual const IEngineObject* AsEngineObject() const = 0;
	virtual const VMatrix& MatrixThisToLinked() const = 0;
	virtual const cplane_t& GetPortalPlane() const = 0;
	virtual const IEnginePortal* GetLinkedPortal() const = 0;
	virtual bool RayIsInPortalHole(const Ray_t& ray) const = 0;
	virtual void TraceRay(const Ray_t& ray, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace, bool bTraceHolyWall = true) const = 0;
	virtual void TraceEntity(IHandleEntity* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter* pFilter, trace_t* ptr) const = 0;
	virtual const PS_SD_Static_SurfaceProperties_t& GetSurfaceProperties() const = 0;
};

class IEngineShadowClone {
public:

};

class IEngineVehicle {
public:

};

class IEngineRope {
public:

};

class IEngineGhost {
public:

};

// An IHandleEntity-derived class can go into an entity list and use ehandles.
class SINGLE_INHERITANCE IHandleEntity
{
public:
	virtual ~IHandleEntity() {}
	virtual int entindex() const = 0;
	virtual datamap_t* GetDataDescMap(void) = 0;
	virtual void SetRefEHandle( const CBaseHandle &handle ) = 0;
	virtual const CBaseHandle& GetRefEHandle() const = 0;
	virtual IEntityFactory* GetEntityFactory() { return NULL; }
	virtual IEntityList* GetEntityList() const { return NULL; }
	virtual IEngineObject* GetEngineObject() { return NULL; }
	virtual const IEngineObject* GetEngineObject() const { return NULL; }
	virtual void PostConstructor(const char* szClassname, int iForceEdictIndex) {}
	virtual bool Init(int entnum, int iSerialNum) { return true; }
	virtual void AfterInit() {};
	virtual char const* GetClassname(void) { return NULL; }
	virtual bool IsWorld() const { return false; }
	virtual bool IsBSPModel() const { return false; }
	virtual bool IsNPC(void) const { return false; }
	virtual bool IsPlayer(void) const { return false; }
	virtual const char& GetTakeDamage() const { return 0; }
	virtual bool IsStandable() const { return false; }
};

abstract_class IEntityCallBack{
public:
	virtual void AfterCreated(IHandleEntity* pEntity) = 0;
	virtual void BeforeDestroy(IHandleEntity* pEntity) = 0;
};

abstract_class IEntityFactory
{
public:
	virtual int GetEngineObjectType() = 0;
	virtual IHandleEntity * Create(IEntityList* pEntityList, int iForceEdictIndex,int iSerialNum ,IEntityCallBack* pCallBack) = 0;//const char* pClassName, 
	virtual void Destroy(IHandleEntity* pEntity) = 0;
	virtual const char* GetMapClassName() = 0;
	virtual const char* GetDllClassName() = 0;
	virtual size_t GetEntitySize() = 0;
	virtual int RequiredEdictIndex() = 0;
	virtual bool IsNetworkable() = 0;
	IEntityFactory* m_pNext = NULL;
};

// This is the glue that hooks .MAP entity class names to our CPP classes
abstract_class IEntityFactoryDictionary
{
public:
	virtual void InstallFactory(IEntityFactory * pFactory) = 0;
	virtual void UninstallFactory(IEntityFactory* pFactory) = 0;
	virtual IHandleEntity* Create(IEntityList* pEntityList, const char* pClassName , int iForceEdictIndex, int iSerialNum, IEntityCallBack* pCallBack) = 0;
	virtual void Destroy(IHandleEntity* pEntity) = 0;
	virtual IEntityFactory* FindFactory(const char* pClassName) = 0;
	virtual const char* GetMapClassName(const char* pClassName) = 0;
	virtual const char* GetDllClassName(const char* pClassName) = 0;
	virtual size_t		GetEntitySize(const char* pClassName) = 0;
	virtual int RequiredEdictIndex(const char* pClassName) = 0;
	virtual bool IsNetworkable(const char* pClassName) = 0;
	virtual const char* GetCannonicalName(const char* pClassName) = 0;
	virtual void ReportEntitySizes() = 0;
	virtual void DumpEntityFactories() = 0;
};

abstract_class IEntityList
{
public:
	virtual IHandleEntity * CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1) = 0;
	virtual void DestroyEntity(IHandleEntity* pEntity) = 0;
	virtual int GetPortalCount() = 0;
	virtual IEnginePortal* GetPortal(int index) = 0;
};

abstract_class IEntityMapData
{
public:
	virtual bool ExtractValue(const char* keyName, char* Value) = 0;
	// find the nth keyName in the endata and change its value to specified one
	// where n == nKeyInstance
	virtual bool SetValue(const char* keyName, char* NewValue, int nKeyInstance = 0) = 0;
	virtual bool GetFirstKey(char* keyName, char* Value) = 0;
	virtual bool GetNextKey(char* keyName, char* Value) = 0;
	virtual const char* CurrentBufferPosition(void) = 0;
};

abstract_class IInterpolatedVar
{
public:
	virtual		 ~IInterpolatedVar() {}

	//virtual void Setup(void* pValue, int type) = 0;
	virtual void SetInterpolationAmount(float seconds) = 0;

	// Returns true if the new value is different from the prior most recent value.
	virtual void NoteLastNetworkedValue() = 0;
	virtual bool NoteChanged(float changetime, bool bUpdateLastNetworkedValue) = 0;
	virtual void Reset() = 0;

	// Returns 1 if the value will always be the same if currentTime is always increasing.
	virtual int Interpolate(float currentTime) = 0;

	virtual int& GetType() = 0;
	virtual void RestoreToLastNetworked() = 0;
	virtual void Copy(IInterpolatedVar* pSrc) = 0;

	virtual const char* GetDebugName() = 0;
	//virtual void SetDebugName(const char* pName) = 0;

	virtual void SetDebug(bool bDebug) = 0;
};

template< typename Type>
class ITypedInterpolatedVar : public IInterpolatedVar {
public:
	virtual void ClearHistory() = 0;
	virtual void AddToHead(float changeTime, const Type* values, bool bFlushNewer) = 0;
	virtual const Type& GetCurrent(int iArrayIndex = 0) const = 0;
	virtual int		GetHead() = 0;
	virtual bool	IsValidIndex(int i) = 0;
	virtual int		GetNext(int i) = 0;
	virtual Type*	GetHistoryValue(int index, float& changetime, int iArrayIndex = 0) = 0;
	virtual int GetMaxCount() const = 0;

};

// inherit from this interface to be able to call WatchPositionChanges
abstract_class IWatcherCallback
{
public:
	virtual ~IWatcherCallback() {}
};

class IWatcherList {
public:
	virtual void Init() = 0;
	virtual void AddToList(IHandleEntity* pWatcher) = 0;
	virtual void RemoveWatcher(IHandleEntity* pWatcher) = 0;
	virtual int GetCallbackObjects(IWatcherCallback** pList, int listMax) = 0;
};

// Implement this class and register with gEntList to receive entity create/delete notification
template< class T >
class IEntityListener
{
public:
	virtual void PreEntityRemove(T* pEntity) {};
	virtual void OnEntityCreated(T* pEntity) {};
	virtual void OnEntitySpawned(T* pEntity) {};
	virtual void OnEntityDeleted(T* pEntity) {};
	virtual void PostEntityRemove(int entnum) {};
};

#endif // IHANDLEENTITY_H
