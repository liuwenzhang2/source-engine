//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef CS_CLIENT_H
#define CS_CLIENT_H
#ifdef _WIN32
#pragma once
#endif
class CBaseEntity;
class CCSPlayer;

void respawn( CBaseEntity *pEdict, bool fCopyCorpse );

void FinishClientPutInServer( CCSPlayer *pPlayer );


#endif // CS_CLIENT_H
