//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "studio.h"
#include "activitylist.h"
#include "engine/IEngineSound.h"
#include "ai_activity.h"
#include "animation.h"
#include "bone_setup.h"
#include "scriptevent.h"
#include "npcevent.h"
#include "eventlist.h"
#include "tier0/vprof.h"

#if !defined( CLIENT_DLL ) && !defined( MAKEXVCD )
#include "util.h"
#include "enginecallback.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#pragma warning( disable : 4244 )
#define iabs(i) (( (i) >= 0 ) ? (i) : -(i) )



//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : *pstudiohdr - 
//			iSequence - 
//
// Output : mstudioseqdesc_t
//-----------------------------------------------------------------------------

//extern int g_nActivityListVersion;
//extern int g_nEventListVersion;



