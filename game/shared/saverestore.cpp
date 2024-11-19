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

#include "globalstate.h"
#include "entitylist.h"
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


BEGIN_SIMPLE_DATADESC(entitytable_t)
	DEFINE_FIELD(id, FIELD_INTEGER),
	DEFINE_FIELD(edictindex, FIELD_INTEGER),
	DEFINE_FIELD(saveentityindex, FIELD_INTEGER),
//	DEFINE_FIELD( restoreentityindex, FIELD_INTEGER ),
	//				hEnt		(not saved, this is the fixup)
	DEFINE_FIELD(location, FIELD_INTEGER),
	DEFINE_FIELD(size, FIELD_INTEGER),
	DEFINE_FIELD(flags, FIELD_INTEGER),
	DEFINE_FIELD(classname, FIELD_STRING),
	DEFINE_FIELD(globalname, FIELD_STRING),
	DEFINE_FIELD(landmarkModelSpace, FIELD_VECTOR),
	DEFINE_FIELD(modelname, FIELD_STRING),
END_DATADESC()





