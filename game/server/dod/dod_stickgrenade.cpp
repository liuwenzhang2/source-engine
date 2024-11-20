//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "dod_stickgrenade.h"
#include "dod_shareddefs.h"

#define GRENADE_MODEL "models/Weapons/w_stick.mdl"

LINK_ENTITY_TO_CLASS( grenade_frag_ger, CDODStickGrenade );
PRECACHE_WEAPON_REGISTER( grenade_frag_ger );

CDODStickGrenade* CDODStickGrenade::Create( 
	const Vector &position, 
	const QAngle &angles, 
	const Vector &velocity, 
	const AngularImpulse &angVelocity, 
	CBaseCombatCharacter *pOwner, 
	float timer,
	DODWeaponID weaponID )
{
	CDODStickGrenade *pGrenade = (CDODStickGrenade*)CBaseEntity::Create( "grenade_frag_ger", position, angles, pOwner );
	
	Assert( pGrenade );

	if( !pGrenade )
		return NULL;

	IPhysicsObject *pPhysicsObject = pGrenade->GetEngineObject()->VPhysicsGetObject();
	if ( pPhysicsObject )
	{
		pPhysicsObject->AddVelocity( &velocity, &angVelocity );
	}

	pGrenade->SetupInitialTransmittedGrenadeVelocity( velocity );

	// Who threw this grenade
	pGrenade->SetThrower( pOwner ); 

	pGrenade->ChangeTeam( pOwner->GetTeamNumber() );

	// How long until we explode
	pGrenade->SetDetonateTimerLength( timer );

	// Save the weapon id for stats purposes
	pGrenade->m_EmitterWeaponID = weaponID;

	return pGrenade;
}

void CDODStickGrenade::Spawn()
{
	SetModel( GRENADE_MODEL );
	BaseClass::Spawn();
}

void CDODStickGrenade::Precache()
{
	engine->PrecacheModel( GRENADE_MODEL );

	g_pSoundEmitterSystem->PrecacheScriptSound( "HEGrenade.Bounce" );

	BaseClass::Precache();
}

void CDODStickGrenade::BounceSound( void )
{
	const char* soundname = "HEGrenade.Bounce";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
}

//Pass the classname of the exploding version of this grenade.
char *CDODStickGrenade::GetExplodingClassname()
{
	return "weapon_frag_ger_live";
}