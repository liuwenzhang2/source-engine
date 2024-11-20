//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Instead of cloning all physics objects in a level to get proper
//			near-portal reactions, only clone from a larger area near portals.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "PhysicsCloneArea.h"
#include "prop_portal.h"
#include "portal_shareddefs.h"
#include "collisionutils.h"
#include "env_debughistory.h"

LINK_ENTITY_TO_CLASS( physicsclonearea, CPhysicsCloneArea );


#define PHYSICSCLONEAREASCALE 4.0f

const Vector CPhysicsCloneArea::vLocalMins( 3.0f, 
										   -PORTAL_HALF_WIDTH * PHYSICSCLONEAREASCALE, 
										   -PORTAL_HALF_HEIGHT * PHYSICSCLONEAREASCALE );
const Vector CPhysicsCloneArea::vLocalMaxs( PORTAL_HALF_HEIGHT * PHYSICSCLONEAREASCALE,  //x is the forward which is fairly thin for portals, replacing with halfheight
											PORTAL_HALF_WIDTH * PHYSICSCLONEAREASCALE,
											PORTAL_HALF_HEIGHT * PHYSICSCLONEAREASCALE );

extern ConVar sv_portal_debug_touch;

void CPhysicsCloneArea::StartTouch( CBaseEntity *pOther )
{
	if( !m_bActive )
		return;

	if( sv_portal_debug_touch.GetBool() )
	{
		DevMsg( "PortalCloneArea %i Start Touch: %s : %f\n", ((m_pAttachedPortal->m_bIsPortal2)?(2):(1)), pOther->GetClassname(), gpGlobals->curtime );
	}
#if !defined( DISABLE_DEBUG_HISTORY )
	if ( !GetEngineObject()->IsMarkedForDeletion() )
	{
		ADD_DEBUG_HISTORY( HISTORY_PLAYER_DAMAGE, UTIL_VarArgs( "PortalCloneArea %i Start Touch: %s : %f\n", ((m_pAttachedPortal->m_bIsPortal2)?(2):(1)), pOther->GetClassname(), gpGlobals->curtime  ) );
	}
#endif

	m_pAttachedSimulator->StartCloningEntity( pOther );
}

void CPhysicsCloneArea::Touch( CBaseEntity *pOther )
{
	if( !m_bActive )
		return;

	//TODO: Planar checks to see if it's a better idea to reclone/unclone
	
}

void CPhysicsCloneArea::EndTouch( CBaseEntity *pOther )
{
	if( !m_bActive )
		return;

	if( sv_portal_debug_touch.GetBool() )
	{
		DevMsg( "PortalCloneArea %i End Touch: %s : %f\n", ((m_pAttachedPortal->m_bIsPortal2)?(2):(1)), pOther->GetClassname(), gpGlobals->curtime );
	}
#if !defined( DISABLE_DEBUG_HISTORY )
	if ( !GetEngineObject()->IsMarkedForDeletion() )
	{
		ADD_DEBUG_HISTORY( HISTORY_PLAYER_DAMAGE, UTIL_VarArgs( "PortalCloneArea %i End Touch: %s : %f\n", ((m_pAttachedPortal->m_bIsPortal2)?(2):(1)), pOther->GetClassname(), gpGlobals->curtime ) );
	}
#endif

	m_pAttachedSimulator->StopCloningEntity( pOther );
}

void CPhysicsCloneArea::Spawn( void )
{
	BaseClass::Spawn();

	Assert( m_pAttachedPortal );

	GetEngineObject()->AddEffects( EF_NORECEIVESHADOW | EF_NOSHADOW | EF_NODRAW );

	GetEngineObject()->SetSolid( SOLID_OBB );
	GetEngineObject()->SetSolidFlags( FSOLID_TRIGGER | FSOLID_NOT_SOLID );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_PLAYER );

	SetSize( vLocalMins, vLocalMaxs );
}

void CPhysicsCloneArea::Activate( void )
{
	BaseClass::Activate();
}

int CPhysicsCloneArea::ObjectCaps( void )
{ 
	return BaseClass::ObjectCaps() | FCAP_DONT_SAVE; //don't save this entity in any way, we naively recreate them
}


void CPhysicsCloneArea::UpdatePosition( void )
{
	Assert( m_pAttachedPortal );

	//untouch everything we're touching
	servertouchlink_t *root = ( servertouchlink_t * )GetEngineObject()->GetDataObject( TOUCHLINK );
	if( root )
	{
		//don't want to risk list corruption while untouching
		CUtlVector<CBaseEntity *> TouchingEnts;
		for( servertouchlink_t *link = root->nextLink; link != root; link = link->nextLink )
			TouchingEnts.AddToTail( (CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched) );


		for( int i = TouchingEnts.Count(); --i >= 0; )
		{
			CBaseEntity *pTouch = TouchingEnts[i];

			pTouch->GetEngineObject()->PhysicsNotifyOtherOfUntouch( this->GetEngineObject());
			GetEngineObject()->PhysicsNotifyOtherOfUntouch( pTouch->GetEngineObject());
		}
	}

	GetEngineObject()->SetAbsOrigin( m_pAttachedPortal->GetEngineObject()->GetAbsOrigin() );
	GetEngineObject()->SetAbsAngles( m_pAttachedPortal->GetEngineObject()->GetAbsAngles() );
	m_bActive = m_pAttachedPortal->m_bActivated;

	//NDebugOverlay::EntityBounds( this, 0, 0, 255, 25, 5.0f );

	//RemoveFlag( FL_DONTTOUCH );
	CloneNearbyEntities(); //wake new objects so they can figure out that they touch
}

void CPhysicsCloneArea::CloneNearbyEntities( void )
{
	CBaseEntity*	pList[ 1024 ];

	Vector vForward, vUp, vRight;
	GetVectors( &vForward, &vRight, &vUp );

	Vector ptOrigin = GetEngineObject()->GetAbsOrigin();
	QAngle qAngles = GetEngineObject()->GetAbsAngles();

	Vector ptOBBStart = ptOrigin;
	ptOBBStart += vForward * vLocalMins.x;
	ptOBBStart += vRight * vLocalMins.y;
	ptOBBStart += vUp * vLocalMins.z;
	

	vForward *= vLocalMaxs.x - vLocalMins.x;
	vRight *= vLocalMaxs.y - vLocalMins.y;
	vUp *= vLocalMaxs.z - vLocalMins.z;


	Vector vAABBMins, vAABBMaxs;
	vAABBMins = vAABBMaxs = ptOBBStart;

	for( int i = 1; i != 8; ++i )
	{
		Vector ptTest = ptOBBStart;
		if( i & (1 << 0) ) ptTest += vForward;
		if( i & (1 << 1) ) ptTest += vRight;
		if( i & (1 << 2) ) ptTest += vUp;

		if( ptTest.x < vAABBMins.x ) vAABBMins.x = ptTest.x;
		if( ptTest.y < vAABBMins.y ) vAABBMins.y = ptTest.y;
		if( ptTest.z < vAABBMins.z ) vAABBMins.z = ptTest.z;
		if( ptTest.x > vAABBMaxs.x ) vAABBMaxs.x = ptTest.x;
		if( ptTest.y > vAABBMaxs.y ) vAABBMaxs.y = ptTest.y;
		if( ptTest.z > vAABBMaxs.z ) vAABBMaxs.z = ptTest.z;
	}
	

	/*{
		Vector ptAABBCenter = (vAABBMins + vAABBMaxs) * 0.5f;
		Vector vAABBExtent = (vAABBMaxs - vAABBMins) * 0.5f;
		NDebugOverlay::Box( ptAABBCenter, -vAABBExtent, vAABBExtent, 0, 0, 255, 128, 10.0f );
	}*/
	

	int count = UTIL_EntitiesInBox( pList, 1024, vAABBMins, vAABBMaxs, 0 );
	trace_t tr;
	UTIL_ClearTrace( tr );
	

	//Iterate over all the possible targets
	for ( int i = 0; i < count; i++ )
	{
		CBaseEntity *pEntity = pList[i];

		if ( pEntity  && (pEntity != this) )
		{
			IPhysicsObject *pPhysicsObject = pEntity->GetEngineObject()->VPhysicsGetObject();

			if( pPhysicsObject )
			{
				//double check intersection at the OBB vs OBB level, we don't want to affect large piles of physics objects if we don't have to, it gets slow
				if( IsOBBIntersectingOBB( ptOrigin, qAngles, vLocalMins, vLocalMaxs, 
					pEntity->GetEngineObject()->GetCollisionOrigin(), pEntity->GetEngineObject()->GetCollisionAngles(), 
					pEntity->GetEngineObject()->OBBMins(), pEntity->GetEngineObject()->OBBMaxs() ) )
				{
					tr.endpos = (ptOrigin + pEntity->GetEngineObject()->GetCollisionOrigin()) * 0.5;
					GetEngineObject()->PhysicsMarkEntitiesAsTouching( pEntity->GetEngineObject(), tr );
					//StartTouch( pEntity );
					
					//pEntity->WakeRestingObjects();
					//pPhysicsObject->Wake();
				}
			}
		}
	}
}

void CPhysicsCloneArea::CloneTouchingEntities( void )
{
	if( m_pAttachedPortal && m_pAttachedPortal->m_bActivated )
	{
		servertouchlink_t *root = (servertouchlink_t* )GetEngineObject()->GetDataObject( TOUCHLINK );
		if( root )
		{
			for( servertouchlink_t *link = root->nextLink; link != root; link = link->nextLink )
				m_pAttachedSimulator->StartCloningEntity( (CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched) );
		}
	}
}





CPhysicsCloneArea *CPhysicsCloneArea::CreatePhysicsCloneArea( CProp_Portal *pFollowPortal )
{
	if( !pFollowPortal )
		return NULL;

	CPhysicsCloneArea *pCloneArea = (CPhysicsCloneArea *)gEntList.CreateEntityByName( "physicsclonearea" );

	pCloneArea->m_pAttachedPortal = pFollowPortal;
	pCloneArea->m_pAttachedSimulator = pFollowPortal;//->m_hPortalSimulator

	DispatchSpawn( pCloneArea );

	pCloneArea->UpdatePosition();

	return pCloneArea;
}

