//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef ICLIENTUNKNOWN_H
#define ICLIENTUNKNOWN_H
#ifdef _WIN32
#pragma once
#endif


#include "tier0/platform.h"
#include "ihandleentity.h"
#include "basehandle.h"

class IClientNetworkable;
class C_BaseEntity;
class IClientRenderable;
class ICollideable;
class IClientEntity;
class IClientThinkable;
class ClientClass;


// This is the client's version of IUnknown. We may want to use a QueryInterface-like
// mechanism if this gets big.
abstract_class IClientUnknown : public IHandleEntity
{
public:
	virtual datamap_t* GetDataDescMap(void) { return NULL; }
	virtual void SetRefEHandle(const CBaseHandle& handle) { m_RefEHandle = handle; }
	virtual const CBaseHandle& GetRefEHandle() const { return m_RefEHandle; }
	virtual int				entindex() const {
		CBaseHandle Handle = this->GetRefEHandle();
		if (Handle == INVALID_ENTITY_HANDLE) {
			return -1;
		}
		else {
			return Handle.GetEntryIndex();
		}
	};
	virtual ICollideable*		GetCollideable() = 0;
	virtual IClientNetworkable*	GetClientNetworkable() = 0;
	virtual IClientRenderable*	GetClientRenderable() = 0;
	virtual IClientEntity*		GetIClientEntity() = 0;
	virtual C_BaseEntity*		GetBaseEntity() = 0;
	virtual IClientThinkable*	GetClientThinkable() = 0;
	//virtual ClientClass*		GetClientClass() = 0;
	virtual void				UpdateOnRemove(void) {};
private:
	CBaseHandle m_RefEHandle;
};


#endif // ICLIENTUNKNOWN_H
