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

//struct IStudioHdr;

#include <stdio.h>
#include <stdlib.h>

#include <tier0/platform.h>
#include <tier0/dbg.h>

#include <tier1/strtools.h>
#include <vstdlib/random.h>
#include <utlvector.h>
#include <const.h>
#include <icvar.h>

#include "string_t.h"

// These two have to be included very early
//#include <predictableid.h>
//#include <predictable_entity.h>
#include "ehandle.h"
#include "recvproxy.h"
#include "engine/IEngineTrace.h"
#include "entitylist_base.h"
// This is a precompiled header.  Include a bunch of common stuff.
// This is kind of ugly in that it adds a bunch of dependency where it isn't needed.
// But on balance, the compile time is much lower (even incrementally) once the precompiled
// headers contain these headers.
#include "precache_register.h"
//#include "c_basecombatweapon.h"
//#include "c_basecombatcharacter.h"
//#include "shared_classnames.h"
#include "baseentity_shared.h"
#include "gamerules.h"
#include "c_baseplayer.h"
//#include "cliententitylist.h"
#include "itempents.h"
#include "vphysics_interface.h"
//#include "physics.h"
#include "c_recipientfilter.h"
//#include "cdll_client_int.h"
#include "worldsize.h"
#include "engine/ivmodelinfo.h"
#include "gamemovement.h"
#include "portal_util_shared.h"
#include <util_shared.h>
#include "cdll_util.h"

extern IClientGameRules* g_pGameRules;
inline IClientGameRules* GameRules() {
	return g_pGameRules;
}

abstract_class C_BaseEntityClassList
{
public:
	C_BaseEntityClassList();
	~C_BaseEntityClassList();
	virtual void LevelShutdown() = 0;

	C_BaseEntityClassList* m_pNextClassList;
};

template< class T >
class C_EntityClassList : public C_BaseEntityClassList
{
public:
	virtual void LevelShutdown() { m_pClassList = NULL; }

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

// Maximum size of entity list
#define INVALID_CLIENTENTITY_HANDLE CBaseHandle( INVALID_EHANDLE_INDEX )

#endif // CBASE_H
