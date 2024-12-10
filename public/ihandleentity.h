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


class CBaseHandle;
class IEntityFactory;
class IEntityList;
class datamap_t;
struct string_t;

class IEngineObject {
public:
	virtual datamap_t* GetDataDescMap(void) = 0;
	virtual const CBaseHandle& GetRefEHandle() const = 0;
	virtual int entindex() const = 0;
	virtual int GetModelIndex(void) const = 0;
	virtual string_t GetModelName(void) const = 0;
	virtual bool IsMarkedForDeletion(void) = 0;
	virtual void CollisionRulesChanged() = 0;
};

// An IHandleEntity-derived class can go into an entity list and use ehandles.
class SINGLE_INHERITANCE IHandleEntity
{
public:
	virtual ~IHandleEntity() {}
	virtual datamap_t* GetDataDescMap(void) = 0;
	virtual void SetRefEHandle( const CBaseHandle &handle ) = 0;
	virtual const CBaseHandle& GetRefEHandle() const = 0;
	virtual IEntityFactory* GetEntityFactory() { return NULL; }
	virtual IEntityList* GetEntityList() { return NULL; }
	virtual IEngineObject* GetEngineObject() { return NULL; }
	virtual const IEngineObject* GetEngineObject() const { return NULL; }
	virtual void PostConstructor(const char* szClassname, int iForceEdictIndex) {}
	virtual bool Init(int entnum, int iSerialNum) { return true; }
	virtual void AfterInit() {};
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

#endif // IHANDLEENTITY_H
