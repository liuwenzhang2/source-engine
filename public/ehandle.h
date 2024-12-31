//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef EHANDLE_H
#define EHANDLE_H
#ifdef _WIN32
#pragma once
#endif

//#if defined( _DEBUG ) && defined( GAME_DLL )
//#include "tier0/dbg.h"
//#include "cbase.h"
//#endif


//#include "const.h"
#include "basehandle.h"
#include "networkvar.h"
//#include "entitylist_base.h"

#ifdef GAME_DLL
class CBaseEntity;
typedef CHandle<CBaseEntity> EHANDLE;
#endif // GAME_DLL

#endif // EHANDLE_H
