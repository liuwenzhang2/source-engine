//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ISERVERUNKNOWN_H
#define ISERVERUNKNOWN_H

#ifdef _WIN32
#pragma once
#endif


#include "ihandleentity.h"

class ICollideable;
class IServerNetworkable;
class ServerClass;


// This is the server's version of IUnknown. We may want to use a QueryInterface-like
// mechanism if this gets big.
class IServerUnknown : public IHandleEntity
{
public:
	// Gets the interface to the collideable + networkable representation of the entity
	virtual void SetRefEHandle(const CBaseHandle& handle) { m_RefEHandle = handle; }
	virtual const CBaseHandle& GetRefEHandle() const { return m_RefEHandle; }
	virtual ICollideable*		GetCollideable() = 0;
	virtual IServerNetworkable*	GetNetworkable() = 0;
	//virtual int					entindex() const = 0;
	virtual int				entindex() const {
		CBaseHandle Handle = this->GetRefEHandle();
		if (Handle == INVALID_ENTITY_HANDLE) {
			return -1;
		}
		else {
			return Handle.GetEntryIndex();
		}
	};
	virtual const char*			GetClassName() const = 0;
	virtual ServerClass*		GetServerClass() = 0;
	virtual void				UpdateOnRemove(void) {};
private:
	CBaseHandle m_RefEHandle;
};


#endif // ISERVERUNKNOWN_H
