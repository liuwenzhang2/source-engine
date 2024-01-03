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



struct Ray_t;
class ServerClass;
class ICollideable;
class IServerNetworkable;
class Vector;
class QAngle;

// This class is how the engine talks to entities in the game DLL.
// CBaseEntity implements this interface.
class IServerEntity	: public IServerUnknown
{
public:
	virtual					~IServerEntity() {}
	// Delete yourself.
	virtual void			Release(void) = 0;
// Previously in pev
	virtual int				GetModelIndex( void ) const = 0;
 	virtual string_t		GetModelName( void ) const = 0;

	virtual void			SetModelIndex( int index ) = 0;
};


//-----------------------------------------------------------------------------
// Purpose: Exposes IClientEntity's to engine
//-----------------------------------------------------------------------------
abstract_class IServerEntityList
{
public:
	//virtual edict_t* GetEdict(CBaseHandle hEnt) const = 0;
	virtual int NumberOfEdicts(void) = 0;
	virtual int IndexOfHighestEdict(void) = 0;

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
};

extern IServerEntityList* serverEntitylist;

#define VSERVERENTITYLIST_INTERFACE_VERSION	"VServerEntityList003"

#endif // ISERVERENTITY_H
