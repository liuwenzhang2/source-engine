//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "basetypes.h"
#include "saverestore_utlvector.h"
#include "saverestore_utlsymbol.h"
#include "globalstate.h"
#include "igamesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern IVEngineServer* engine;

CON_COMMAND(dump_globals, "Dump all global entities/states")
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	engine->DumpGlobals();
}

void ShowServerGameTime()
{
	Msg( "Server game time: %f\n", gpGlobals->curtime );
}

CON_COMMAND(server_game_time, "Gives the game time in seconds (server's curtime)")
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	ShowServerGameTime();
}
