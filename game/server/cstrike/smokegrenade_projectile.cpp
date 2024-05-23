//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "smokegrenade_projectile.h"
#include "sendproxy.h"
#include "particle_smokegrenade.h"
#include "cs_player.h"
#include "KeyValues.h"
#include "bot_manager.h"
#include "weapon_csbase.h"

#define GRENADE_MODEL "models/Weapons/w_eq_smokegrenade_thrown.mdl"


LINK_ENTITY_TO_CLASS( smokegrenade_projectile, CSmokeGrenadeProjectile );
PRECACHE_WEAPON_REGISTER( smokegrenade_projectile );

BEGIN_DATADESC( CSmokeGrenadeProjectile )
	DEFINE_THINKFUNC( Think_Detonate ),
	DEFINE_THINKFUNC( Think_Fade ),
	DEFINE_THINKFUNC( Think_Remove )
END_DATADESC()


CSmokeGrenadeProjectile* CSmokeGrenadeProjectile::Create( 
	const Vector &position, 
	const QAngle &angles, 
	const Vector &velocity, 
	const AngularImpulse &angVelocity, 
	CBaseCombatCharacter *pOwner )
{
	CSmokeGrenadeProjectile *pGrenade = (CSmokeGrenadeProjectile*)CBaseEntity::Create( "smokegrenade_projectile", position, angles, pOwner );
	
	// Set the timer for 1 second less than requested. We're going to issue a SOUND_DANGER
	// one second before detonation.
	pGrenade->SetTimer( 1.5 );
	pGrenade->GetEngineObject()->SetAbsVelocity( velocity );
	pGrenade->SetupInitialTransmittedGrenadeVelocity( velocity );
	pGrenade->SetThrower( pOwner );
	pGrenade->GetEngineObject()->SetGravity( 0.55 );
	pGrenade->GetEngineObject()->SetFriction( 0.7 );
	pGrenade->m_flDamage = 100;
	pGrenade->ChangeTeam( pOwner->GetTeamNumber() );
	pGrenade->ApplyLocalAngularVelocityImpulse( angVelocity );	
	pGrenade->SetTouch( &CBaseGrenade::BounceTouch );

	pGrenade->GetEngineObject()->SetGravity( BaseClass::GetGrenadeGravity() );
	pGrenade->GetEngineObject()->SetFriction( BaseClass::GetGrenadeFriction() );
	pGrenade->GetEngineObject()->SetElasticity( BaseClass::GetGrenadeElasticity() );
	pGrenade->m_bDidSmokeEffect = false;

	pGrenade->m_pWeaponInfo = GetWeaponInfo( WEAPON_SMOKEGRENADE );

	return pGrenade;
}


void CSmokeGrenadeProjectile::SetTimer( float timer )
{
	SetThink( &CSmokeGrenadeProjectile::Think_Detonate );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + timer );

	TheBots->SetGrenadeRadius( this, 0.0f );
}

void CSmokeGrenadeProjectile::Think_Detonate()
{
	if (GetEngineObject()->GetAbsVelocity().Length() > 0.1 )
	{
		// Still moving. Don't detonate yet.
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.2 );
		return;
	}

	TheBots->SetGrenadeRadius( this, SmokeGrenadeRadius );

	// Ok, we've stopped rolling or whatever. Now detonate.
	ParticleSmokeGrenade *pGren = (ParticleSmokeGrenade*)CBaseEntity::Create( PARTICLESMOKEGRENADE_ENTITYNAME, GetEngineObject()->GetAbsOrigin(), QAngle(0,0,0), NULL );
	if ( pGren )
	{
		pGren->FillVolume();
		pGren->SetFadeTime( 15, 20 );
		pGren->GetEngineObject()->SetAbsOrigin(GetEngineObject()->GetAbsOrigin() );

		//tell the hostages about the smoke!
		CBaseEntity *pEntity = NULL;
		variant_t var;	//send the location of the smoke?
		var.SetVector3D(GetEngineObject()->GetAbsOrigin() );
		while ( ( pEntity = gEntList.FindEntityByClassname( pEntity, "hostage_entity" ) ) != NULL)
		{
			//send to hostages that have a resonable chance of being in it while its still smoking
			if( (GetEngineObject()->GetAbsOrigin() - pEntity->GetEngineObject()->GetAbsOrigin()).Length() < 1000 )
				pEntity->AcceptInput( "smokegrenade", this, this, var, 0 );
		}

		// tell the bots a smoke grenade has exploded
		CCSPlayer *player = ToCSPlayer(GetThrower());
		if ( player )
		{
			IGameEvent * event = gameeventmanager->CreateEvent( "smokegrenade_detonate" );
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

	m_hSmokeEffect = pGren;
	m_bDidSmokeEffect = true;

	const char* soundname = "BaseSmokeEffect.Sound";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);

	m_nRenderMode = kRenderTransColor;
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 5 );
	SetThink( &CSmokeGrenadeProjectile::Think_Fade );
}


// Fade the projectile out over time before making it disappear
void CSmokeGrenadeProjectile::Think_Fade()
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime );

	color32 c = GetRenderColor();
	c.a -= 1;
	SetRenderColor( c.r, c.b, c.g, c.a );

	if ( !c.a )
	{
		TheBots->RemoveGrenade( this );

		GetEngineObject()->SetModelName( NULL_STRING );//invisible
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 20 );
		SetThink( &CSmokeGrenadeProjectile::Think_Remove );	// Spit out smoke for 10 seconds.
		GetEngineObject()->SetSolid( SOLID_NONE );
	}
}


void CSmokeGrenadeProjectile::Think_Remove()
{
	if ( m_hSmokeEffect.Get() )
		UTIL_Remove( m_hSmokeEffect );

	TheBots->RemoveGrenade( this );

	GetEngineObject()->SetModelName( NULL_STRING );//invisible
	GetEngineObject()->SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
}

//Implement this so we never call the base class,
//but this should never be called either.
void CSmokeGrenadeProjectile::Detonate( void )
{
	Assert(!"Smoke grenade handles its own detonation");
}


void CSmokeGrenadeProjectile::Spawn()
{
	SetModel( GRENADE_MODEL );
	BaseClass::Spawn();
}


void CSmokeGrenadeProjectile::Precache()
{
	engine->PrecacheModel( GRENADE_MODEL );
	g_pSoundEmitterSystem->PrecacheScriptSound( "BaseSmokeEffect.Sound" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "SmokeGrenade.Bounce" );
	BaseClass::Precache();
}

void CSmokeGrenadeProjectile::BounceSound( void )
{
	if ( !m_bDidSmokeEffect )
	{
		const char* soundname = "SmokeGrenade.Bounce";
		CPASAttenuationFilter filter(this, soundname);

		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = 0.0f;
		params.m_pflSoundDuration = NULL;
		params.m_bWarnOnDirectWaveReference = true;
		g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
	}
}
