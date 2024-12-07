//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Helper classes and functions for the save/restore system.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include <limits.h>
#include "isaverestore.h"
#include "saverestore.h"
#include <stdarg.h>
#include "shake.h"
#include "decals.h"
#include "gamerules.h"
#include "bspfile.h"
#include "mathlib/mathlib.h"
#include "engine/IEngineSound.h"
#include "saverestoretypes.h"
#include "saverestore_utlvector.h"
#include "model_types.h"
#include "igamesystem.h"
#include "interval.h"
#include "vphysics/object_hash.h"
#include "datacache/imdlcache.h"
#include "tier0/vprof.h"

#if !defined( CLIENT_DLL )

//#include "entitylist.h"
#include "gameinterface.h"
#else

#include "gamestringpool.h"
#include "cdll_client_int.h"
#endif

// HACKHACK: Builds a global list of entities that were restored from all levels
//#if !defined( CLIENT_DLL )
//void AddRestoredEntity( CBaseEntity *pEntity );
//#else
//void AddRestoredEntity( C_BaseEntity *pEntity );
//#endif


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"







