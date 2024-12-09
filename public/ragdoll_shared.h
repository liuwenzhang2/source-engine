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

struct vcollide_t;
//struct IStudioHdr;
class IStudioHdr;

#include "mathlib/vector.h"
#include "bone_accessor.h"
#include "vphysics_interface.h"

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
