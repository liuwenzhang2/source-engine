//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "hl1_grenade_spit.h"
#include "soundent.h"
#include "decals.h"

#include "smoke_trail.h"
#include "hl2_shareddefs.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"

ConVar sk_bullsquid_dmg_spit ( "sk_bullsquid_dmg_spit", "0" );

BEGIN_DATADESC( CGrenadeSpit )

	// Function pointers
	DEFINE_THINKFUNC( SpitThink ),
	DEFINE_TOUCHFUNC( GrenadeSpitTouch ),

	//DEFINE_FIELD( m_nSquidSpitSprite, FIELD_INTEGER ),

	DEFINE_FIELD( m_fSpitDeathTime, FIELD_TIME ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( grenade_spit, CGrenadeSpit );

void CGrenadeSpit::Spawn( void )
{
	Precache( );
	GetEngineObject()->SetSolid( SOLID_BBOX );
	GetEngineObject()->SetMoveType( MOVETYPE_FLYGRAVITY );

	// FIXME, if these is a sprite, then we need a base class derived from CSprite rather than
	// CBaseAnimating.  pev->scale becomes m_flSpriteScale in that case.
	SetModel( "models/spitball_large.mdl" );
	UTIL_SetSize(this, Vector(-3, -3, -3), Vector(3, 3, 3));

	m_nRenderMode		= kRenderTransAdd;
	SetRenderColor( 255, 255, 255, 255 );
	GetEngineObject()->SetRenderFX(kRenderFxNone);

	SetThink( &CGrenadeSpit::SpitThink );
	SetUse( &CBaseGrenade::DetonateUse ); 
	SetTouch( &CGrenadeSpit::GrenadeSpitTouch );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

	m_flDamage		= sk_bullsquid_dmg_spit.GetFloat();
	m_DmgRadius		= 60.0f;
	m_takedamage	= DAMAGE_YES;
	m_iHealth		= 1;

	GetEngineObject()->SetGravity( SPIT_GRAVITY );
	GetEngineObject()->SetFriction( 0.8 );
	GetEngineObject()->SetSequence( 1 );

	GetEngineObject()->SetCollisionGroup( HL2COLLISION_GROUP_SPIT );
}


void CGrenadeSpit::SetSpitSize(int nSize)
{
	switch (nSize)
	{
		case SPIT_LARGE:
		{
			SetModel( "models/spitball_large.mdl" );
			break;
		}
		case SPIT_MEDIUM:
		{
			SetModel( "models/spitball_medium.mdl" );
			break;
		}
		case SPIT_SMALL:
		{
			SetModel( "models/spitball_small.mdl" );
			break;
		}
	}
}

void CGrenadeSpit::Event_Killed( const CTakeDamageInfo &info )
{
	Detonate( );
}

void CGrenadeSpit::GrenadeSpitTouch( CBaseEntity *pOther )
{
	if (m_fSpitDeathTime != 0)
	{
		return;
	}
	if ( pOther->GetEngineObject()->GetCollisionGroup() == HL2COLLISION_GROUP_SPIT)
	{
		return;
	}
	if ( !pOther->m_takedamage )
	{

		// make a splat on the wall
		trace_t tr;
		UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() + GetEngineObject()->GetAbsVelocity() * 10, MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );
		UTIL_DecalTrace(&tr, "BeerSplash" );

		// make some flecks
		CPVSFilter filter( tr.endpos );
		te->SpriteSpray( filter, 0.0,
			&tr.endpos, &tr.plane.normal, m_nSquidSpitSprite, random->RandomInt( 90, 160 ), 50, random->RandomInt ( 5, 15 ) );
	}
	else
	{
		RadiusDamage ( CTakeDamageInfo( this, GetThrower(), m_flDamage, DMG_BLAST ), GetEngineObject()->GetAbsOrigin(), m_DmgRadius, CLASS_NONE, NULL );
	}

	Detonate();
}

void CGrenadeSpit::SpitThink( void )
{
	if (m_fSpitDeathTime != 0 &&
		m_fSpitDeathTime < gpGlobals->curtime)
	{
		UTIL_Remove( this );
	}
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
}

void CGrenadeSpit::Detonate(void)
{
	m_takedamage	= DAMAGE_NO;	

	int		iPitch;

	// splat sound
	iPitch = random->RandomFloat( 90, 110 );

	{
		const char* soundname = "GrenadeSpit.Acid";
		CPASAttenuationFilter filter(this, soundname);

		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = 0.0f;
		params.m_pflSoundDuration = NULL;
		params.m_bWarnOnDirectWaveReference = true;
		g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
	}
	{
		const char* soundname = "GrenadeSpit.Hit";
		CPASAttenuationFilter filter(this, soundname);

		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = 0.0f;
		params.m_pflSoundDuration = NULL;
		params.m_bWarnOnDirectWaveReference = true;
		g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
	}

	UTIL_Remove( this );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGrenadeSpit::Precache( void )
{
	m_nSquidSpitSprite = engine->PrecacheModel("sprites/bigspit.vmt");// client side spittle.

	engine->PrecacheModel("models/spitball_large.mdl");
	engine->PrecacheModel("models/spitball_medium.mdl");
	engine->PrecacheModel("models/spitball_small.mdl");

	g_pSoundEmitterSystem->PrecacheScriptSound( "GrenadeSpit.Acid" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "GrenadeSpit.Hit" );

}


CGrenadeSpit::CGrenadeSpit(void)
{
}
