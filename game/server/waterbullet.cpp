//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: An effect for a single bullet passing through a body of water.
//			The slug quickly decelerates, leaving a trail of bubbles behind it.
//
//			TODO: make clientside
//
//=============================================================================//
#include "cbase.h"
#include "waterbullet.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define WATERBULLET_INITIAL_SPEED		1000.0
#define WATERBULLET_STOP_TIME			0.5 // how long it takes a bullet in water to come to a stop!

#define WATERBULLET_DECAY	( WATERBULLET_INITIAL_SPEED / WATERBULLET_STOP_TIME )

BEGIN_DATADESC( CWaterBullet )

	// Function Pointers
	DEFINE_FUNCTION( Touch ),
	DEFINE_FUNCTION( BulletThink ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( waterbullet, CWaterBullet );

IMPLEMENT_SERVERCLASS_ST( CWaterBullet, DT_WaterBullet )
END_SEND_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWaterBullet::Precache()
{
	engine->PrecacheModel( "models/weapons/w_bullet.mdl" );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWaterBullet::Spawn( const Vector &vecOrigin, const Vector &vecDir )
{
	Precache();

	GetEngineObject()->SetSolid( SOLID_BBOX );
	SetModel( "models/weapons/w_bullet.mdl" );
	UTIL_SetSize( this, vec3_origin, vec3_origin );

	GetEngineObject()->SetMoveType( MOVETYPE_FLY );

	GetEngineObject()->SetGravity( 0.0 );

	QAngle angles;
	GetEngineObject()->SetAbsOrigin( vecOrigin );
	
	GetEngineObject()->SetAbsVelocity( vecDir * 1500.0f );
	VectorAngles(GetEngineObject()->GetAbsVelocity(), angles );
	GetEngineObject()->SetAbsAngles( angles );

	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
	SetTouch( &CWaterBullet::Touch );

	SetThink( &CWaterBullet::BulletThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWaterBullet::BulletThink()
{
	//NDebugOverlay::Line( GetAbsOrigin(), GetAbsOrigin() - GetAbsVelocity() * 0.1, 255, 255, 255, false, 1 );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.05 );

/*
	QAngle angles = GetAbsAngles();
	angles.x += random->RandomInt( -6, 6 );
	angles.y += random->RandomInt( -6, 6 );
	SetAbsAngles( angles );
*/

	Vector forward;
	AngleVectors(GetEngineObject()->GetAbsAngles(), &forward );
	GetEngineObject()->SetAbsVelocity( forward * 1500.0f );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWaterBullet::Touch( CBaseEntity *pOther )
{
	Vector	vecDir = GetEngineObject()->GetAbsVelocity();
	float speed = VectorNormalize( vecDir );

	Vector	vecStart = GetEngineObject()->GetAbsOrigin() - ( vecDir * 8 );
	Vector	vecEnd = GetEngineObject()->GetAbsOrigin() + ( vecDir * speed );

	trace_t	tr;
	UTIL_TraceLine( vecStart, vecEnd, MASK_SHOT, NULL, &tr );
	UTIL_ImpactTrace( &tr, DMG_BULLET );

	gEntList.DestroyEntity( this );
}
