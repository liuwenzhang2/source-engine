//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "hegrenade_projectile.h"
#include "soundent.h"
#include "cs_player.h"
#include "KeyValues.h"
#include "weapon_csbase.h"

#define GRENADE_MODEL "models/Weapons/w_eq_fraggrenade_thrown.mdl"


LINK_ENTITY_TO_CLASS( hegrenade_projectile, CHEGrenadeProjectile );
PRECACHE_WEAPON_REGISTER( hegrenade_projectile );

CHEGrenadeProjectile* CHEGrenadeProjectile::Create( 
	const Vector &position, 
	const QAngle &angles, 
	const Vector &velocity, 
	const AngularImpulse &angVelocity, 
	CBaseCombatCharacter *pOwner, 
	float timer )
{
	CHEGrenadeProjectile *pGrenade = (CHEGrenadeProjectile*)CBaseEntity::Create( "hegrenade_projectile", position, angles, pOwner );
	
	// Set the timer for 1 second less than requested. We're going to issue a SOUND_DANGER
	// one second before detonation.

	pGrenade->SetDetonateTimerLength( 1.5 );
	pGrenade->GetEngineObject()->SetAbsVelocity( velocity );
	pGrenade->SetupInitialTransmittedGrenadeVelocity( velocity );
	pGrenade->SetThrower( pOwner ); 

	pGrenade->GetEngineObject()->SetGravity( BaseClass::GetGrenadeGravity() );
	pGrenade->GetEngineObject()->SetFriction( BaseClass::GetGrenadeFriction() );
	pGrenade->GetEngineObject()->SetElasticity( BaseClass::GetGrenadeElasticity() );

	pGrenade->m_flDamage = 100;
	pGrenade->m_DmgRadius = pGrenade->m_flDamage * 3.5f;
	pGrenade->ChangeTeam( pOwner->GetTeamNumber() );
	pGrenade->ApplyLocalAngularVelocityImpulse( angVelocity );	

	// make NPCs afaid of it while in the air
	pGrenade->SetThink( &CHEGrenadeProjectile::DangerSoundThink );
	pGrenade->SetNextThink( gpGlobals->curtime );

	pGrenade->m_pWeaponInfo = GetWeaponInfo( WEAPON_HEGRENADE );

	return pGrenade;
}

void CHEGrenadeProjectile::Spawn()
{
	SetModel( GRENADE_MODEL );
	BaseClass::Spawn();
}

void CHEGrenadeProjectile::Precache()
{
	engine->PrecacheModel( GRENADE_MODEL );

	g_pSoundEmitterSystem->PrecacheScriptSound( "HEGrenade.Bounce" );

	BaseClass::Precache();
}

void CHEGrenadeProjectile::BounceSound( void )
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

void CHEGrenadeProjectile::Detonate()
{
	BaseClass::Detonate();

	// tell the bots an HE grenade has exploded
	CCSPlayer *player = ToCSPlayer(GetThrower());
	if ( player )
	{
		IGameEvent * event = gameeventmanager->CreateEvent( "hegrenade_detonate" );
		if ( event )
		{
			event->SetInt( "userid", player->GetUserID() );
			event->SetFloat( "x", GetEngineObject()->GetAbsOrigin().x );
			event->SetFloat( "y", GetEngineObject()->GetAbsOrigin().y );
			event->SetFloat( "z", GetEngineObject()->GetAbsOrigin().z );
			gameeventmanager->FireEvent( event );
		}
	}
}
