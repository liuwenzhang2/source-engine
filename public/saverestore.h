//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Helper classes and functions for the save/restore system. These
// 			classes are internally structured to distinguish simple from
//			complex types.
//
// $NoKeywords: $
//=============================================================================//

#ifndef SAVERESTORE_H
#define SAVERESTORE_H

#include "isaverestore.h"
#include "utlvector.h"
#include "filesystem.h"

#ifdef _WIN32
#pragma once
#endif

//-------------------------------------

class CSaveRestoreData;
class CSaveRestoreSegment;
class CGameSaveRestoreInfo;
class typedescription_t;
//struct edict_t;
class datamap_t;
class CBaseEntity;
struct interval_t;
struct model_t;


#endif // SAVERESTORE_H
