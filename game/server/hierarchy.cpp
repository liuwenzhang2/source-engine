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

//-----------------------------------------------------------------------------
// Purpose: Does the linked list work of removing a child object from the hierarchy.
// Input  : pParent - 
//			pChild - 
//-----------------------------------------------------------------------------
void IEngineObject::UnlinkChild( IEngineObject *pParent, IEngineObject *pChild )
{
	IEngineObject *pList = pParent->FirstMoveChild();
	IEngineObject *pPrev = NULL;
	
	while ( pList )
	{
		IEngineObject *pNext = pList->NextMovePeer();
		if ( pList == pChild )
		{
			// patch up the list
			if (!pPrev) {
				pParent->SetFirstMoveChild(pNext?pNext->GetOuter():NULL);
			}else{
				pPrev->SetNextMovePeer(pNext?pNext->GetOuter():NULL);
			}

			// Clear hierarchy bits for this guy
			pList->SetMoveParent( NULL );
			pList->SetNextMovePeer( NULL );
			//pList->GetOuter()->NetworkProp()->SetNetworkParent( CBaseHandle() );
			pList->GetOuter()->DispatchUpdateTransmitState();
			pList->GetOuter()->OnEntityEvent( ENTITY_EVENT_PARENT_CHANGED, NULL );
			
			pParent->GetOuter()->RecalcHasPlayerChildBit();
			return;
		}
		else
		{
			pPrev = pList;
			pList = pNext;
		}
	}

	// This only happens if the child wasn't found in the parent's child list
	Assert(0);
}

void IEngineObject::LinkChild( IEngineObject *pParent, IEngineObject *pChild )
{
	//EHANDLE hParent;
	//hParent.Set( pParent->GetOuter() );
	pChild->SetNextMovePeer( pParent->GetOuter()->FirstMoveChild());
	pParent->SetFirstMoveChild( pChild->GetOuter() );
	pChild->SetMoveParent(pParent->GetOuter());
	//pChild->GetOuter()->NetworkProp()->SetNetworkParent(pParent->GetOuter());
	pChild->GetOuter()->DispatchUpdateTransmitState();
	pChild->GetOuter()->OnEntityEvent( ENTITY_EVENT_PARENT_CHANGED, NULL );
	pParent->GetOuter()->RecalcHasPlayerChildBit();
}

void IEngineObject::TransferChildren( IEngineObject *pOldParent, IEngineObject *pNewParent )
{
	IEngineObject *pChild = pOldParent->FirstMoveChild();
	while ( pChild )
	{
		// NOTE: Have to do this before the unlink to ensure local coords are valid
		Vector vecAbsOrigin = pChild->GetAbsOrigin();
		QAngle angAbsRotation = pChild->GetAbsAngles();
		Vector vecAbsVelocity = pChild->GetAbsVelocity();
//		QAngle vecAbsAngVelocity = pChild->GetAbsAngularVelocity();
		pChild->GetOuter()->BeforeUnlinkParent(pOldParent->GetOuter());
		UnlinkChild( pOldParent, pChild );
		LinkChild( pNewParent, pChild );
		pChild->GetOuter()->AfterLinkParent(pNewParent->GetOuter());

		// FIXME: This is a hack to guarantee update of the local origin, angles, etc.
		pChild->GetAbsOrigin().Init(FLT_MAX, FLT_MAX, FLT_MAX);
		pChild->GetAbsAngles().Init(FLT_MAX, FLT_MAX, FLT_MAX);
		pChild->GetAbsVelocity().Init(FLT_MAX, FLT_MAX, FLT_MAX);

		pChild->SetAbsOrigin(vecAbsOrigin);
		pChild->SetAbsAngles(angAbsRotation);
		pChild->SetAbsVelocity(vecAbsVelocity);
//		pChild->SetAbsAngularVelocity(vecAbsAngVelocity);

		pChild  = pOldParent->FirstMoveChild();
	}
}

void IEngineObject::UnlinkFromParent( IEngineObject *pRemove )
{
	if ( pRemove->GetMoveParent() )
	{
		// NOTE: Have to do this before the unlink to ensure local coords are valid
		Vector vecAbsOrigin = pRemove->GetAbsOrigin();
		QAngle angAbsRotation = pRemove->GetAbsAngles();
		Vector vecAbsVelocity = pRemove->GetAbsVelocity();
//		QAngle vecAbsAngVelocity = pRemove->GetAbsAngularVelocity();

		UnlinkChild( pRemove->GetMoveParent(), pRemove );

		pRemove->SetLocalOrigin(vecAbsOrigin);
		pRemove->SetLocalAngles(angAbsRotation);
		pRemove->SetLocalVelocity(vecAbsVelocity);
//		pRemove->SetLocalAngularVelocity(vecAbsAngVelocity);
		pRemove->GetOuter()->UpdateWaterState();
	}
}


//-----------------------------------------------------------------------------
// Purpose: Clears the parent of all the children of the given object.
//-----------------------------------------------------------------------------
void IEngineObject::UnlinkAllChildren( IEngineObject *pParent )
{
	IEngineObject *pChild = pParent->FirstMoveChild();
	while ( pChild )
	{
		IEngineObject *pNext = pChild->NextMovePeer();
		UnlinkFromParent( pChild );
		pChild  = pNext;
	}
}

bool EntityIsParentOf( CBaseEntity *pParent, CBaseEntity *pEntity )
{
	while ( pEntity->GetMoveParent() )
	{
		pEntity = pEntity->GetMoveParent();
		if ( pParent == pEntity )
			return true;
	}

	return false;
}

static void GetAllChildren_r( CBaseEntity *pEntity, CUtlVector<CBaseEntity *> &list )
{
	for ( ; pEntity != NULL; pEntity = pEntity->NextMovePeer() )
	{
		list.AddToTail( pEntity );
		GetAllChildren_r( pEntity->FirstMoveChild(), list );
	}
}

int GetAllChildren( CBaseEntity *pParent, CUtlVector<CBaseEntity *> &list )
{
	if ( !pParent )
		return 0;

	GetAllChildren_r( pParent->FirstMoveChild(), list );
	return list.Count();
}

int	GetAllInHierarchy( CBaseEntity *pParent, CUtlVector<CBaseEntity *> &list )
{
	if (!pParent)
		return 0;
	list.AddToTail( pParent );
	return GetAllChildren( pParent, list ) + 1;
}
