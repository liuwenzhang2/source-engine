//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "player_pickup.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// player pickup utility routine
void Pickup_ForcePlayerToDropThisObject( CBaseEntity *pTarget )
{
	if ( pTarget == NULL )
		return;

	IPhysicsObject *pPhysics = pTarget->GetEngineObject()->VPhysicsGetObject();
	
	if ( pPhysics == NULL )
		return;

	if ( pPhysics->GetGameFlags() & FVPHYSICS_PLAYER_HELD )
	{
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		pPlayer->ForceDropOfCarriedPhysObjects( pTarget );
	}
}


void Pickup_OnPhysGunDrop( CBaseEntity *pDroppedObject, CBasePlayer *pPlayer, PhysGunDrop_t Reason )
{
	if (pDroppedObject) {
		pDroppedObject->OnPhysGunDrop(pPlayer, Reason);
	}
}


void Pickup_OnPhysGunPickup( CBaseEntity *pPickedUpObject, CBasePlayer *pPlayer, PhysGunPickup_t reason )
{
	if (pPickedUpObject) {
		pPickedUpObject->OnPhysGunPickup(pPlayer, reason);
	}

	// send phys gun pickup item event, but only in single player
	if ( !g_pGameRules->IsMultiplayer() )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "physgun_pickup" );
		if ( event )
		{
			event->SetInt( "entindex", pPickedUpObject->entindex() );
			gameeventmanager->FireEvent( event );
		}
	}
}

bool Pickup_OnAttemptPhysGunPickup( CBaseEntity *pPickedUpObject, CBasePlayer *pPlayer, PhysGunPickup_t reason )
{
	if (pPickedUpObject) {
		return pPickedUpObject->OnAttemptPhysGunPickup(pPlayer, reason);
	}
	return true;
}

CBaseEntity	*Pickup_OnFailedPhysGunPickup( CBaseEntity *pPickedUpObject, Vector vPhysgunPos )
{
	if (pPickedUpObject) {
		return pPickedUpObject->OnFailedPhysGunPickup(vPhysgunPos);
	}
	return NULL;
}

bool Pickup_GetPreferredCarryAngles( CBaseEntity *pObject, CBasePlayer *pPlayer, const matrix3x4_t &localToWorld, QAngle &outputAnglesWorldSpace )
{
	if (pObject != NULL && pObject->HasPreferredCarryAnglesForPlayer( pPlayer ) )
	{
		outputAnglesWorldSpace = TransformAnglesToWorldSpace(pObject->PreferredCarryAngles(), localToWorld );
		return true;
	}
	return false;
}

bool Pickup_ForcePhysGunOpen( CBaseEntity *pObject, CBasePlayer *pPlayer )
{
	if (pObject) {
		return pObject->ForcePhysgunOpen(pPlayer);
	}
	return false;
}

AngularImpulse Pickup_PhysGunLaunchAngularImpulse( CBaseEntity *pObject, PhysGunForce_t reason )
{
	if (pObject != NULL && pObject->ShouldPuntUseLaunchForces( reason ) )
	{
		return pObject->PhysGunLaunchAngularImpulse();
	}
	return RandomAngularImpulse( -600, 600 );
}

Vector Pickup_DefaultPhysGunLaunchVelocity( const Vector &vecForward, float flMass )
{
#ifdef HL2_DLL
	// Calculate the velocity based on physcannon rules
	float flForceMax = physcannon_maxforce.GetFloat();
	float flForce = flForceMax;

	float mass = flMass;
	if ( mass > 100 )
	{
		mass = MIN( mass, 1000 );
		float flForceMin = physcannon_minforce.GetFloat();
		flForce = SimpleSplineRemapValClamped( mass, 100, 600, flForceMax, flForceMin );
	}

	return ( vecForward * flForce );
#endif

	// Do the simple calculation
	return ( vecForward * flMass );
}

Vector Pickup_PhysGunLaunchVelocity( CBaseEntity *pObject, const Vector &vecForward, PhysGunForce_t reason )
{
	// The object must be valid
	if ( pObject == NULL )
	{
		Assert( 0 );
		return vec3_origin;
	}

	// Shouldn't ever get here with a non-vphysics object.
	IPhysicsObject *pPhysicsObject = pObject->GetEngineObject()->VPhysicsGetObject();
	if ( pPhysicsObject == NULL )
	{
		Assert( 0 );
		return vec3_origin;
	}

	// Call the pickup entity's callback
	if (pObject != NULL && pObject->ShouldPuntUseLaunchForces( reason ) )
		return pObject->PhysGunLaunchVelocity( vecForward, pPhysicsObject->GetMass() );

	// Do our default behavior
	return Pickup_DefaultPhysGunLaunchVelocity(	vecForward, pPhysicsObject->GetMass() );
}

bool Pickup_ShouldPuntUseLaunchForces( CBaseEntity *pObject, PhysGunForce_t reason )
{
	if (pObject) {
		return pObject->ShouldPuntUseLaunchForces(reason);
	}
	return false;
}

