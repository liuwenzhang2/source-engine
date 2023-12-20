//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: implementation of player info manager
//
//=============================================================================//
#ifndef PLAYERINFOMANAGER_H
#define PLAYERINFOMANAGER_H
#ifdef _WIN32
#pragma once
#endif


#include "game/server/iplayerinfo.h"

//-----------------------------------------------------------------------------
// Purpose: interface for plugins to get player info
//-----------------------------------------------------------------------------
class CPlayerInfoManager: public IPlayerInfoManager
{
public:
	virtual IPlayerInfo *GetPlayerInfo( int pEdict );
	virtual CGlobalVars *GetGlobalVars();
};

class CPluginBotManager: public IBotManager
{
public:
	virtual IBotController *GetBotController( int pEdict );
	virtual int CreateBot( const char *botname );
};

#endif