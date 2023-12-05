//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $NoKeywords: $
//===========================================================================//

//-----------------------------------------------------------------------------
// Purpose: a global list of all the entities in the game.  All iteration through
//			entities is done through this object.
//-----------------------------------------------------------------------------
#include "cbase.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------

// Create interface
static CClientEntityList<C_BaseEntity> s_EntityList;
CBaseEntityList<C_BaseEntity> *g_pEntityList = &s_EntityList;

// Expose list to engine
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CClientEntityList, IClientEntityList, VCLIENTENTITYLIST_INTERFACE_VERSION, s_EntityList );

// Store local pointer to interface for rest of client .dll only 
//  (CClientEntityList instead of IClientEntityList )
CClientEntityList<C_BaseEntity> *cl_entitylist = &s_EntityList;

// -------------------------------------------------------------------------------------------------- //
// Game-code CBaseHandle implementation.
// -------------------------------------------------------------------------------------------------- //

IHandleEntity* CBaseHandle::Get() const
{
#ifdef CLIENT_DLL
	//extern CBaseEntityList<IHandleEntity>* g_pEntityList;
	return g_pEntityList->LookupEntity(*this);
#endif // CLIENT_DLL
#ifdef GAME_DLL
	//extern CBaseEntityList<IHandleEntity>* g_pEntityList;
	return g_pEntityList->LookupEntity(*this);
#endif // GAME_DLL
}

bool PVSNotifierMap_LessFunc( IClientUnknown* const &a, IClientUnknown* const &b )
{
	return a < b;
}

// -------------------------------------------------------------------------------------------------- //
// C_AllBaseEntityIterator
// -------------------------------------------------------------------------------------------------- //
//C_AllBaseEntityIterator::C_AllBaseEntityIterator()
//{
//	Restart();
//}
//
//
//void C_AllBaseEntityIterator::Restart()
//{
//	m_CurBaseEntity = ClientEntityList().m_BaseEntities.Head();
//}
//
//	
//C_BaseEntity* C_AllBaseEntityIterator::Next()
//{
//	if ( m_CurBaseEntity == ClientEntityList().m_BaseEntities.InvalidIndex() )
//		return NULL;
//
//	C_BaseEntity *pRet = ClientEntityList().m_BaseEntities[m_CurBaseEntity];
//	m_CurBaseEntity = ClientEntityList().m_BaseEntities.Next( m_CurBaseEntity );
//	return pRet;
//}


// -------------------------------------------------------------------------------------------------- //
// C_BaseEntityIterator
// -------------------------------------------------------------------------------------------------- //
C_BaseEntityIterator::C_BaseEntityIterator()
{
	Restart();
}

void C_BaseEntityIterator::Restart()
{
	m_CurBaseEntity = ClientEntityList().m_BaseEntities.Head();
}

C_BaseEntity* C_BaseEntityIterator::Next()
{
	// Skip dormant entities
	while ( m_CurBaseEntity != ClientEntityList().m_BaseEntities.InvalidIndex() )
	{
		C_BaseEntity *pRet = ClientEntityList().m_BaseEntities[m_CurBaseEntity];
		m_CurBaseEntity = ClientEntityList().m_BaseEntities.Next( m_CurBaseEntity );

		if (!pRet->IsDormant())
			return pRet;
	}

	return NULL;
}