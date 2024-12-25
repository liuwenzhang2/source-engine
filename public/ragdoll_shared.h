//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef RAGDOLL_SHARED_H
#define RAGDOLL_SHARED_H
#ifdef _WIN32
#pragma once
#endif

class IPhysicsObject;
class IPhysicsConstraint;
class IPhysicsConstraintGroup;
class IPhysicsCollision;
class IPhysicsEnvironment;
class IPhysicsSurfaceProps;
struct matrix3x4_t;
struct ragdoll_t;
struct vcollide_t;
//struct IStudioHdr;
class IStudioHdr;

#include "tier0/vprof.h"
#include "tier0/vcrmode.h"
#include "tier1/mempool.h"
#include "tier1/memstack.h"
#ifdef _WIN32
#include "typeinfo"
// BUGBUG: typeinfo stomps some of the warning settings (in yvals.h)
#pragma warning(disable:4244)
#elif POSIX
#include <typeinfo>
#else
#error "need typeinfo defined"
#endif
#include "utlvector.h"
#include "utlmultilist.h"
#include "UtlSortVector.h"
#include "filesystem.h"
#include "vstdlib/jobthread.h"
#include "coordsize.h"
#include "mathlib/polyhedron.h"
#include "mathlib/vector.h"
#include "server_class.h"
#include "client_class.h"
#include "collisionutils.h"
#include "vphysics_interface.h"
#include "vphysics/performance.h"
#include "vphysics/object_hash.h"
#include "vphysics/player_controller.h"
#include "vphysics/constraints.h"
#include "vphysics/collision_set.h"
#include "datacache/idatacache.h"
#include "datacache/imdlcache.h"
#include "bone_setup.h"
#include "bone_accessor.h"
#include "jigglebones.h"
#include "engine/ivdebugoverlay.h"
#include "posedebugger.h"
#include "usercmd.h"
#include "saverestoretypes.h"
#include "game/server/iservervehicle.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "model_types.h"
#include "rope_shared.h"
#include "rope_physics.h"
#include "vcollide_parse.h"
#include "bspfile.h"
#include "inetchannelinfo.h"
#include "entitylist_base.h"

#include "debugoverlay_shared.h"
#include "decals.h"
#include "IEffects.h"
#include "animation.h"
#include "in_buttons.h"
#include "sharedInterface.h"
#include "rope_helpers.h"
#include "igamesystem.h"
#include "soundenvelope.h"
#include "saverestore_utlvector.h"
#include "collisionproperty.h"
#include "physics_saverestore.h"
#include "gamestringpool.h"
#include "mapentities_shared.h"
#include "util_shared.h"
#include "portal_util_shared.h"
#include "physics_shared.h"
#include "movevars_shared.h"
#include "hl2_gamerules.h"
#include "env_wind_shared.h"
#include "predictioncopy.h"
#include "baseentity_shared.h"
#include "vphysics_sound.h"


struct ragdollparams_t
{
	void		*pGameData;
	vcollide_t	*pCollide;
	IStudioHdr	*pStudioHdr;
	int			modelIndex;
	Vector		forcePosition;
	Vector		forceVector;
	int			forceBoneIndex;
	const matrix3x4_t *pCurrentBones;
	float		jointFrictionScale;
	bool		allowStretch;
	bool		fixedConstraints;
};

bool RagdollCreate( ragdoll_t &ragdoll, const ragdollparams_t &params, IPhysicsEnvironment *pPhysEnv );

void RagdollActivate( ragdoll_t &ragdoll, vcollide_t *pCollide, int modelIndex, bool bForceWake = true );
void RagdollSetupCollisions( ragdoll_t &ragdoll, vcollide_t *pCollide, int modelIndex );
void RagdollDestroy( ragdoll_t &ragdoll );

// Gets the bone matrix for a ragdoll object
// NOTE: This is different than the object's position because it is
// forced to be rigidly attached in parent space
bool RagdollGetBoneMatrix( const ragdoll_t &ragdoll, CBoneAccessor &pBoneToWorld, int objectIndex );

// Parse the ragdoll and obtain the mapping from each physics element index to a bone index
// returns num phys elements
int RagdollExtractBoneIndices( int *boneIndexOut, IStudioHdr *pStudioHdr, vcollide_t *pCollide );

// computes an exact bbox of the ragdoll's physics objects
void RagdollComputeExactBbox( const ragdoll_t &ragdoll, const Vector &origin, Vector &outMins, Vector &outMaxs );
bool RagdollIsAsleep( const ragdoll_t &ragdoll );
void RagdollSetupAnimatedFriction( IPhysicsEnvironment *pPhysEnv, ragdoll_t *ragdoll, int iModelIndex );

void RagdollApplyAnimationAsVelocity( ragdoll_t &ragdoll, const matrix3x4_t *pBoneToWorld );
void RagdollApplyAnimationAsVelocity( ragdoll_t &ragdoll, const matrix3x4_t *pPrevBones, const matrix3x4_t *pCurrentBones, float dt );


#endif // RAGDOLL_SHARED_H
