//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
/*

===== portal_client.cpp ========================================================

  Portal client/server game specific stuff

*/

#include "cbase.h"
#include "portal_player.h"
#include "portal_gamerules.h"
#include "gamerules.h"
#include "teamplay_gamerules.h"
#include "entitylist.h"
#include "physics.h"
#include "game.h"
#include "player_resource.h"
#include "engine/IEngineSound.h"

#include "tier0/vprof.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void Host_Say( int pEdict, bool teamonly );

extern CBaseEntity*	FindPickerEntityClass( CBasePlayer *pPlayer, char *classname );
extern bool			g_fGameOver;

/*
===========
ClientPutInServer

called each time a player is spawned into the game
============
*/
void ClientPutInServer( int pEdict, const char *playername )
{
	// Allocate a CBasePlayer for pev, and call spawn
	CPortal_Player *pPlayer = CPortal_Player::CreatePlayer( "player", pEdict );
	pPlayer->PlayerData()->netname = AllocPooledString( playername );
}


void ClientActive( int pEdict, bool bLoadGame )
{
	CPortal_Player *pPlayer = dynamic_cast< CPortal_Player* >( gEntList.GetBaseEntity( pEdict ) );
	Assert( pPlayer );

	pPlayer->InitialSpawn();

	if ( !bLoadGame )
	{
		pPlayer->Spawn();
	}
}


/*
===============
const char *GetGameDescription()

Returns the descriptive name of this .dll.  E.g., Half-Life, or Team Fortress 2
===============
*/
const char *GetGameDescription()
{
	if ( g_pGameRules ) // this function may be called before the world has spawned, and the game rules initialized
		return g_pGameRules->GetGameDescription();
	else
		return "Half-Life 2";
}

//-----------------------------------------------------------------------------
// Purpose: Given a player and optional name returns the entity of that 
//			classname that the player is nearest facing
//			
// Input  :
// Output :
//-----------------------------------------------------------------------------
CBaseEntity* FindEntity( int pEdict, char *classname)
{
	// If no name was given set bits based on the picked
	if (FStrEq(classname,"")) 
	{
		return (FindPickerEntityClass( static_cast<CBasePlayer*>(gEntList.GetBaseEntity(pEdict)), classname ));
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Precache game-specific models & sounds
//-----------------------------------------------------------------------------
void ClientGamePrecache( void )
{
	engine->PrecacheModel("models/player.mdl");
	engine->PrecacheModel( "models/gibs/agibs.mdl" );
	engine->PrecacheModel("models/weapons/v_hands.mdl");

	g_pSoundEmitterSystem->PrecacheScriptSound( "HUDQuickInfo.LowAmmo" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "HUDQuickInfo.LowHealth" );

	g_pSoundEmitterSystem->PrecacheScriptSound( "Missile.ShotDown" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "Bullets.DefaultNearmiss" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "Bullets.GunshipNearmiss" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "Bullets.StriderNearmiss" );
	
	g_pSoundEmitterSystem->PrecacheScriptSound( "Geiger.BeepHigh" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "Geiger.BeepLow" );

	engine->PrecacheModel( "models/portals/portal1.mdl" );
	engine->PrecacheModel( "models/portals/portal2.mdl" );
}


// called by ClientKill and DeadThink
void respawn( CBaseEntity *pEdict, bool fCopyCorpse )
{
	if (gpGlobals->coop || gpGlobals->deathmatch)
	{
		if ( fCopyCorpse )
		{
			// make a copy of the dead body for appearances sake
			((CPortal_Player *)pEdict)->CreateCorpse();
		}

		// respawn player
		pEdict->Spawn();
	}
	else
	{       // restart the entire server
		engine->ServerCommand("reload\n");
	}
}

void GameStartFrame( void )
{
	VPROF("GameStartFrame()");
	if ( g_fGameOver )
		return;

	gpGlobals->teamplay = (teamplay.GetInt() != 0);
}

//=========================================================
// instantiate the proper game rules object
//=========================================================
void InstallGameRules()
{
	if ( !gpGlobals->deathmatch )
	{
		CreateGameRulesObject( "CPortalGameRules" );
		return;
	}
	else
	{
		if ( teamplay.GetInt() > 0 )
		{
			// teamplay
			CreateGameRulesObject( "CTeamplayRules" );
		}
		else
		{
			// vanilla deathmatch
			CreateGameRulesObject( "CMultiplayRules" );
		}
	}
}

