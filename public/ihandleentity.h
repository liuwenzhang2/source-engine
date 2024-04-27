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

// An IHandleEntity-derived class can go into an entity list and use ehandles.
class IHandleEntity
{
public:
	virtual ~IHandleEntity() {}
	virtual void SetRefEHandle( const CBaseHandle &handle ) = 0;
	virtual const CBaseHandle& GetRefEHandle() const = 0;
	virtual IEntityFactory* GetEntityFactory() { return NULL; }
	virtual IEntityList* GetEntityList() { return NULL; }
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
	virtual IHandleEntity * Create(IEntityList* pEntityList, int iForceEdictIndex,int iSerialNum ,IEntityCallBack* pCallBack) = 0;//const char* pClassName, 
	virtual void Destroy(IHandleEntity* pEntity) = 0;
	virtual const char* GetMapClassName() = 0;
	virtual const char* GetDllClassName() = 0;
	virtual size_t GetEntitySize() = 0;
	virtual int RequiredEdictIndex() = 0;
	virtual bool IsNetworkable() = 0;
};

// This is the glue that hooks .MAP entity class names to our CPP classes
abstract_class IEntityFactoryDictionary
{
public:
	virtual void InstallFactory(IEntityFactory * pFactory) = 0;
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
};

IEntityFactoryDictionary* EntityFactoryDictionary();

abstract_class IEntityList
{
public:
	virtual IHandleEntity * CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1) = 0;
	virtual void DestroyEntity(IHandleEntity* pEntity) = 0;
};

#endif // IHANDLEENTITY_H
