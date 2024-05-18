//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Contains the set of functions for manipulating entity hierarchies.
//
// $NoKeywords: $
//=============================================================================//

// UNDONE: Reconcile this with SetParent()

#include "cbase.h"
#include "hierarchy.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

bool EntityIsParentOf( IEngineObjectServer *pParent, IEngineObjectServer *pEntity )
{
	while ( pEntity->GetMoveParent() )
	{
		pEntity = pEntity->GetMoveParent();
		if ( pParent == pEntity )
			return true;
	}

	return false;
}

static void GetAllChildren_r( IEngineObjectServer *pEntity, CUtlVector<IEngineObjectServer *> &list )
{
	for ( ; pEntity != NULL; pEntity = pEntity->NextMovePeer() )
	{
		list.AddToTail( pEntity );
		GetAllChildren_r( pEntity->FirstMoveChild(), list );
	}
}

int GetAllChildren( IEngineObjectServer *pParent, CUtlVector<IEngineObjectServer *> &list )
{
	if ( !pParent )
		return 0;

	GetAllChildren_r( pParent->FirstMoveChild(), list );
	return list.Count();
}

int	GetAllInHierarchy( IEngineObjectServer *pParent, CUtlVector<IEngineObjectServer *> &list )
{
	if (!pParent)
		return 0;
	list.AddToTail( pParent );
	return GetAllChildren( pParent, list ) + 1;
}
