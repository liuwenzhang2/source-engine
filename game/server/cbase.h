//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef CBASE_H
#define CBASE_H
#ifdef _WIN32
#pragma once
#endif

#ifdef _WIN32
// Silence certain warnings
#pragma warning(disable : 4244)		// int or float down-conversion
#pragma warning(disable : 4305)		// int or float data truncation
#pragma warning(disable : 4201)		// nameless struct/union
#pragma warning(disable : 4511)     // copy constructor could not be generated
#pragma warning(disable : 4675)     // resolved overload was found by argument dependent lookup
#endif

#ifdef _DEBUG
#define DEBUG 1
#endif
class CBaseEntity;

// Misc C-runtime library headers
#include <math.h>

#include <stdio.h>

// tier 0
#include "tier0/dbg.h"
#include "tier0/platform.h"
#include "basetypes.h"

// tier 1
#include "tier1/strtools.h"
#include "utlvector.h"
#include "mathlib/vmatrix.h"

// tier 2
#include "string_t.h"

// tier 3
#include "vphysics_interface.h"

// Shared engine/DLL constants
#include "const.h"
#include "edict.h"

// Shared header describing protocol between engine and DLLs
#include "eiface.h"
#include "iserverentity.h"

#include "dt_send.h"

// Shared header between the client DLL and the game DLLs
#include "shareddefs.h"
#include "ehandle.h"
typedef CHandle<CBaseEntity> EHANDLE;
// app
#if defined(_X360)
#define DISABLE_DEBUG_HISTORY 1
#endif


#include "datamap.h"
//#include "predictable_entity.h"
//#include "predictableid.h"
#include "sendproxy.h"
#include "variant_t.h"
#include "takedamageinfo.h"
#include "utllinkedlist.h"
#include "touchlink.h"
#include "groundlink.h"
#include "base_transmit_proxy.h"
#include "soundflags.h"
#include "networkvar.h"
#include "igameevents.h"
#include "entitylist_base.h"
#ifdef _XBOX
//#define FUNCTANK_AUTOUSE  We haven't made the decision to use this yet (sjb)
#else
#undef FUNCTANK_AUTOUSE
#endif//_XBOX
#include "engine/IEngineTrace.h"
#include "datacache/imdlcache.h"
// This is a precompiled header.  Include a bunch of common stuff.
// This is kind of ugly in that it adds a bunch of dependency where it isn't needed.
// But on balance, the compile time is much lower (even incrementally) once the precompiled
// headers contain these headers.
#include "precache_register.h"
//#include "enginecallback.h"
//#include "baseanimating.h"
//#include "basecombatweapon.h"
//#include "basecombatcharacter.h"
#include "baseentity_shared.h"
#include "gamerules.h"
#include "player.h"
#include "basetempentity.h"
#include "te.h"
//#include "physics.h"
#include "ndebugoverlay.h"
#include "recipientfilter.h"
#include "gamemovement.h"
#include "portal_util_shared.h"
#include "util_shared.h"
#include "util.h"

extern IServerGameRules* g_pGameRules;
inline IServerGameRules* GameRules() {
	return g_pGameRules;
}

abstract_class CBaseEntityClassList
{
public:
	CBaseEntityClassList();
	~CBaseEntityClassList();
	virtual void LevelShutdownPostEntity() = 0;

	CBaseEntityClassList* m_pNextClassList;
};

template< class T >
class CEntityClassList : public CBaseEntityClassList
{
public:
	virtual void LevelShutdownPostEntity() { m_pClassList = NULL; }

	void Insert(T* pEntity)
	{
		pEntity->m_pNext = m_pClassList;
		m_pClassList = pEntity;
	}

	void Remove(T* pEntity)
	{
		T** pPrev = &m_pClassList;
		T* pCur = *pPrev;
		while (pCur)
		{
			if (pCur == pEntity)
			{
				*pPrev = pCur->m_pNext;
				return;
			}
			pPrev = &pCur->m_pNext;
			pCur = *pPrev;
		}
	}

	static T* m_pClassList;
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
		const notify_teleport_params_t* pTeleport;
		const notify_destroy_params_t* pDestroy;
	};
	notify_system_event_params_t(const notify_teleport_params_t* pInTeleport) { pTeleport = pInTeleport; }
	notify_system_event_params_t(const notify_destroy_params_t* pInDestroy) { pDestroy = pInDestroy; }
};

abstract_class INotify
{
public:
	// Add notification for an entity
	virtual void AddEntity(CBaseEntity * pNotify, CBaseEntity * pWatched) = 0;

	// Remove notification for an entity
	virtual void RemoveEntity(CBaseEntity* pNotify, CBaseEntity* pWatched) = 0;

	// Call the named input in each entity who is watching pEvent's status
	virtual void ReportNamedEvent(CBaseEntity* pEntity, const char* pEventName) = 0;

	// System events don't make sense as inputs, so are handled through a generic notify function
	virtual void ReportSystemEvent(CBaseEntity* pEntity, notify_system_event_t eventType, const notify_system_event_params_t& params) = 0;

	inline void ReportDestroyEvent(CBaseEntity* pEntity)
	{
		notify_destroy_params_t destroy;
		ReportSystemEvent(pEntity, NOTIFY_EVENT_DESTROY, notify_system_event_params_t(&destroy));
	}

	inline void ReportTeleportEvent(CBaseEntity* pEntity, const Vector& prevOrigin, const QAngle& prevAngles, bool physicsRotate)
	{
		notify_teleport_params_t teleport;
		teleport.prevOrigin = prevOrigin;
		teleport.prevAngles = prevAngles;
		teleport.physicsRotate = physicsRotate;
		ReportSystemEvent(pEntity, NOTIFY_EVENT_TELEPORT, notify_system_event_params_t(&teleport));
	}

	// Remove this entity from the notify list
	virtual void ClearEntity(CBaseEntity* pNotify) = 0;
};

// singleton
extern INotify* g_pNotify;

#endif // CBASE_H
