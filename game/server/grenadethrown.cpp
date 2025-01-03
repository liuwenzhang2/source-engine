//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
/*

===== grenade_base.cpp ========================================================

  Base Handling for all the player's grenades

*/
#include "cbase.h"
#include "grenadethrown.h"
#include "ammodef.h"
#include "vstdlib/random.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Precaches a grenade and ensures clients know of it's "ammo"
void UTIL_PrecacheOtherGrenade( const char *szClassname )
{
	IServerEntity *pEntity = EntityList()->CreateEntityByName( szClassname );
	if ( !pEntity )
	{
		Msg( "NULL Ent in UTIL_PrecacheOtherGrenade\n" );
		return;
	}
	
	CThrownGrenade *pGrenade = dynamic_cast<CThrownGrenade *>( pEntity );

	if (pGrenade)
	{
		pGrenade->Precache( );
	}

	EntityList()->DestroyEntity( pEntity );
}

//-----------------------------------------------------------------------------
// Purpose: Setup basic values for Thrown grens
//-----------------------------------------------------------------------------
void CThrownGrenade::Spawn( void )
{
	// point sized, solid, bouncing
	GetEngineObject()->SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE );
	GetEngineObject()->SetSolid( SOLID_BBOX );
	GetEngineObject()->SetSize(vec3_origin, vec3_origin);

	// Movement
	GetEngineObject()->SetGravity( UTIL_ScaleForGravity( 648 ) );
	GetEngineObject()->SetFriction(0.6);
	QAngle angles;
	VectorAngles(GetEngineObject()->GetAbsVelocity(), angles );
	GetEngineObject()->SetLocalAngles( angles );
	QAngle vecAngVel( random->RandomFloat ( -100, -500 ), 0, 0 );
	SetLocalAngularVelocity( vecAngVel );
	
	SetTouch( &CThrownGrenade::BounceTouch );
}

//-----------------------------------------------------------------------------
// Purpose: Throw the grenade.
// Input  : vecOrigin - Starting position
//			vecVelocity - Starting velocity
//			flExplodeTime - Time at which to detonate
//-----------------------------------------------------------------------------
void CThrownGrenade::Thrown( Vector vecOrigin, Vector vecVelocity, float flExplodeTime )
{
	// Throw
	GetEngineObject()->SetLocalOrigin( vecOrigin );
	GetEngineObject()->SetAbsVelocity( vecVelocity );

	// Explode in 3 seconds
	SetThink( &CThrownGrenade::Detonate );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + flExplodeTime );
}

