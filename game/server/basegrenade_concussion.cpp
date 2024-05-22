//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "baseentity.h"
#include "basegrenade_shared.h"
#include "soundent.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CBaseGrenadeConcussion : public CBaseGrenade
{
	DECLARE_DATADESC();

public:
	DECLARE_CLASS( CBaseGrenadeConcussion, CBaseGrenade );

	void Spawn( void );
	void Precache( void );
	void FallThink(void);
	void ExplodeConcussion( CBaseEntity *pOther );

protected:
	static int m_nTrailSprite;
};

int CBaseGrenadeConcussion::m_nTrailSprite = 0;

LINK_ENTITY_TO_CLASS( npc_concussiongrenade, CBaseGrenadeConcussion );

BEGIN_DATADESC( CBaseGrenadeConcussion )

	DEFINE_THINKFUNC( FallThink ),
	DEFINE_ENTITYFUNC( ExplodeConcussion ),

END_DATADESC()


void CBaseGrenadeConcussion::FallThink(void)
{
	if (!IsInWorld())
	{
		Remove( );
		return;
	}
	CSoundEnt::InsertSound ( SOUND_DANGER, GetEngineObject()->GetAbsOrigin() + GetEngineObject()->GetAbsVelocity() * 0.5, GetEngineObject()->GetAbsVelocity().Length( ), 0.2 );

	SetNextThink( gpGlobals->curtime + random->RandomFloat(0.05, 0.1) );

	if (GetWaterLevel() != 0)
	{
		GetEngineObject()->SetAbsVelocity(GetEngineObject()->GetAbsVelocity() * 0.5 );
	}

	Vector 	pos = GetEngineObject()->GetAbsOrigin() + Vector(random->RandomFloat(-4, 4), random->RandomFloat(-4, 4), random->RandomFloat(-4, 4));

	CPVSFilter filter(GetEngineObject()->GetAbsOrigin() );

	te->Sprite( filter, 0.0,
		&pos,
		m_nTrailSprite,
		random->RandomFloat(0.5, 0.8),
		200 );
}


//
// Contact grenade, explode when it touches something
// 
void CBaseGrenadeConcussion::ExplodeConcussion( CBaseEntity *pOther )
{
	trace_t		tr;
	Vector		vecSpot;// trace starts here!

	Vector velDir = GetEngineObject()->GetAbsVelocity();
	VectorNormalize( velDir );
	vecSpot = GetEngineObject()->GetAbsOrigin() - velDir * 32;
	UTIL_TraceLine( vecSpot, vecSpot + velDir * 64, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

	Explode( &tr, DMG_BLAST );
}


void CBaseGrenadeConcussion::Spawn( void )
{
	// point sized, solid, bouncing
	SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE );
	GetEngineObject()->SetSolid( SOLID_BBOX );
	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_PROJECTILE );
	SetModel( "models/weapons/w_grenade.mdl" );	// BUG: wrong model

	UTIL_SetSize(this, vec3_origin, vec3_origin);

	// contact grenades arc lower
	GetEngineObject()->SetGravity( UTIL_ScaleForGravity( 400 ) );	// use a lower gravity for grenades to make them easier to see
	QAngle angles;
	VectorAngles(GetEngineObject()->GetAbsVelocity(), angles );
	GetEngineObject()->SetLocalAngles( angles );

	m_nRenderFX = kRenderFxGlowShell;
	SetRenderColor( 200, 200, 20, 255 );
	
	// make NPCs afaid of it while in the air
	SetThink( &CBaseGrenadeConcussion::FallThink );
	SetNextThink( gpGlobals->curtime );
	
	// Tumble in air
	QAngle vecAngVel( random->RandomFloat ( -100, -500 ), 0, 0 );
	SetLocalAngularVelocity( vecAngVel );
	
	// Explode on contact
	SetTouch( &CBaseGrenadeConcussion::ExplodeConcussion );

	m_flDamage = 80;

	// Allow player to blow this puppy up in the air
	m_takedamage	= DAMAGE_YES;
}


void CBaseGrenadeConcussion::Precache( void )
{
	BaseClass::Precache( );

	engine->PrecacheModel("models/weapons/w_grenade.mdl");
	m_nTrailSprite = engine->PrecacheModel("sprites/twinkle01.vmt");
}
