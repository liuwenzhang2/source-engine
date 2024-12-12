//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Interface layer for ipion IVP physics.
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//


#include "cbase.h"
#include "coordsize.h"
//#include "entitylist.h"
#include "vcollide_parse.h"
#include "soundenvelope.h"
#include "game.h"
#include "utlvector.h"
#include "init_factory.h"
#include "igamesystem.h"
#include "hierarchy.h"
#include "IEffects.h"
#include "engine/IEngineSound.h"
#include "world.h"
#include "decals.h"
#include "physics_fx.h"
#include "vphysics_sound.h"
#include "vphysics/vehicles.h"
#include "game/server/vehicle_sounds.h"
#include "movevars_shared.h"
#include "physics_saverestore.h"
#include "tier0/vprof.h"
#include "engine/IStaticPropMgr.h"
#include "physics_prop_ragdoll.h"
#if HL2_EPISODIC
#include "particle_parse.h"
#endif
#include "vphysics/object_hash.h"
#include "vphysics/collision_set.h"
#include "vphysics/friction.h"
#include "fmtstr.h"
#include "physics_npc_solver.h"
#include "physics_collisionevent.h"
#include "vphysics/performance.h"
#include "positionwatcher.h"
#include "tier1/callqueue.h"
#include "vphysics/constraints.h"

#ifdef PORTAL
#include "portal_physics_collisionevent.h"
#include "physicsshadowclone.h"
#include "PortalSimulation.h"
#include "prop_portal.h"
#endif

#include "physics_shared.h"
#include "te_effect_dispatch.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//#ifdef PORTAL
//CEntityList *g_pShadowEntities_Main = NULL;
//#endif







