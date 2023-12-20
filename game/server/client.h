//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#ifndef CLIENT_H
#define CLIENT_H

#ifdef _WIN32
#pragma once
#endif


class CCommand;
class CUserCmd;
class CBasePlayer;
class CBaseEntity;


void ClientActive( int pEdict, bool bLoadGame );
void ClientPutInServer( int pEdict, const char *playername );
void ClientCommand( CBasePlayer *pSender, const CCommand &args );
void ClientPrecache( void );
// Game specific precaches
void ClientGamePrecache( void );
const char *GetGameDescription( void );
void Host_Say( CBaseEntity *pEdict, bool teamonly );



#endif		// CLIENT_H
