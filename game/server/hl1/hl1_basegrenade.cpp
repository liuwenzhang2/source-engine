//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//


#include "cbase.h"
#include "decals.h"
#include "basecombatcharacter.h"
#include "shake.h"
#include "engine/IEngineSound.h"
#include "soundent.h"
//#include "entitylist.h"
#include "hl1_basegrenade.h"


extern short	g_sModelIndexFireball;		// (in combatweapon.cpp) holds the index for the fireball 
extern short	g_sModelIndexWExplosion;	// (in combatweapon.cpp) holds the index for the underwater explosion

unsigned int CHL1BaseGrenade::PhysicsSolidMaskForEntity( void ) const
{
	return BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_HITBOX;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1BaseGrenade::Precache()
{
	BaseClass::Precache();

	g_pSoundEmitterSystem->PrecacheScriptSound( "BaseGrenade.Explode" );
}


void CHL1BaseGrenade::Explode( trace_t *pTrace, int bitsDamageType )
{
	float		flRndSound;// sound randomizer

	GetEngineObject()->SetModelName( NULL_STRING );//invisible
	GetEngineObject()->AddSolidFlags( FSOLID_NOT_SOLID );

	m_takedamage = DAMAGE_NO;

	// Pull out of the wall a bit
	if ( pTrace->fraction != 1.0 )
	{
		GetEngineObject()->SetLocalOrigin( pTrace->endpos + (pTrace->plane.normal * 0.6) );
	}

	Vector vecAbsOrigin = GetEngineObject()->GetAbsOrigin();
	int contents = UTIL_PointContents ( vecAbsOrigin );

	if ( pTrace->fraction != 1.0 )
	{
		Vector vecNormal = pTrace->plane.normal;
		const surfacedata_t *pdata = physprops->GetSurfaceData( pTrace->surface.surfaceProps );	
		CPASFilter filter( vecAbsOrigin );
		te->Explosion( filter, 0.0, 
			&vecAbsOrigin,
			!( contents & MASK_WATER ) ? g_sModelIndexFireball : g_sModelIndexWExplosion,
			m_DmgRadius * .03, 
			25,
			TE_EXPLFLAG_NONE,
			m_DmgRadius,
			m_flDamage,
			&vecNormal,
			(char) pdata->game.material );
	}
	else
	{
		CPASFilter filter( vecAbsOrigin );
		te->Explosion( filter, 0.0,
			&vecAbsOrigin, 
			!( contents & MASK_WATER ) ? g_sModelIndexFireball : g_sModelIndexWExplosion,
			m_DmgRadius * .03, 
			25,
			TE_EXPLFLAG_NONE,
			m_DmgRadius,
			m_flDamage );
	}

	CSoundEnt::InsertSound ( SOUND_COMBAT, GetEngineObject()->GetAbsOrigin(), BASEGRENADE_EXPLOSION_VOLUME, 3.0 );

	// Use the owner's position as the reported position
	Vector vecReported = GetThrower() ? GetThrower()->GetEngineObject()->GetAbsOrigin() : vec3_origin;
	
	CTakeDamageInfo info( this, GetThrower(), GetBlastForce(), GetEngineObject()->GetAbsOrigin(), m_flDamage, bitsDamageType, 0, &vecReported );

	RadiusDamage( info, GetEngineObject()->GetAbsOrigin(), m_DmgRadius, CLASS_NONE, NULL );

	UTIL_DecalTrace( pTrace, "Scorch" );

	flRndSound = random->RandomFloat( 0 , 1 );

	const char* soundname = "BaseGrenade.Explode";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);

	SetTouch( NULL );
	
	GetEngineObject()->AddEffects( EF_NODRAW );
	GetEngineObject()->SetAbsVelocity( vec3_origin );

	SetThink( &CBaseGrenade::Smoke );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.3);

	if ( GetWaterLevel() == 0 )
	{
		int sparkCount = random->RandomInt( 0,3 );
		QAngle angles;
		VectorAngles( pTrace->plane.normal, angles );

		for ( int i = 0; i < sparkCount; i++ )
			Create( "spark_shower", GetEngineObject()->GetAbsOrigin(), angles, NULL );
	}
}
