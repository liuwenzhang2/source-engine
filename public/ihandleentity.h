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


class CBaseHandle;
class IEntityFactory;

// An IHandleEntity-derived class can go into an entity list and use ehandles.
class IHandleEntity
{
public:
	virtual ~IHandleEntity() {}
	virtual void SetRefEHandle( const CBaseHandle &handle ) = 0;
	virtual const CBaseHandle& GetRefEHandle() const = 0;
	virtual IEntityFactory* GetEntityFactory() { return NULL; }
	virtual bool Init(int entnum, int iSerialNum) { return true; }
};

abstract_class IEntityFactory
{
public:
	virtual IHandleEntity * Create(int iForceEdictIndex,int iSerialNum) = 0;//const char* pClassName, 
	virtual void Destroy(IHandleEntity* pEntity) = 0;
	virtual const char* GetMapClassName() = 0;
	virtual const char* GetDllClassName() = 0;
	virtual size_t GetEntitySize() = 0;
};

#endif // IHANDLEENTITY_H
