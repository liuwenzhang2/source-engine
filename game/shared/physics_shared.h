//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef PHYSICS_SHARED_H
#define PHYSICS_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#include "ehandle.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "vphysics_interface.h"

class IPhysics;
class IPhysicsEnvironment;
class IPhysicsSurfaceProps;
class IPhysicsCollision;
class IPhysicsObject;
class IPhysicsObjectPairHash;
class CSoundPatch;
class IGameSystem;
struct objectparams_t;

//extern IPhysicsObject		*g_PhysWorldObject;
//extern IPhysics				*physics;
//extern IPhysicsCollision	*physcollision;
//extern IPhysicsEnvironment	*physenv;
//#ifdef PORTAL
//extern IPhysicsEnvironment	*physenv_main;
//#endif
//extern IPhysicsSurfaceProps *physprops;
//extern IPhysicsObjectPairHash *g_EntityCollisionHash;

//extern const objectparams_t g_PhysDefaultObjectParams;

// Compute enough energy of a reference mass travelling at speed
// makes numbers more intuitive


//void PhysFrictionSound( IHandleEntity *pEntity, IPhysicsObject *pObject, float energy, int surfaceProps, int surfacePropsHit );
void PhysFrictionEffect( Vector &vecPos, Vector vecVel, float energy, int surfaceProps, int surfacePropsHit );

// Create a vphysics object based on a model
IPhysicsObject *PhysModelCreate( IHandleEntity *pEntity, int modelIndex, const Vector &origin, const QAngle &angles, solid_t *pSolid = NULL );

IPhysicsObject *PhysModelCreateBox( IHandleEntity *pEntity, const Vector &mins, const Vector &maxs, const Vector &origin, bool isStatic );
IPhysicsObject *PhysModelCreateOBB( IHandleEntity *pEntity, const Vector &mins, const Vector &maxs, const Vector &origin, const QAngle &angle, bool isStatic );

// Create a vphysics object based on a BSP model (unmoveable)
IPhysicsObject *PhysModelCreateUnmoveable( IHandleEntity *pEntity, int modelIndex, const Vector &origin, const QAngle &angles );

// Create a vphysics object based on an existing collision model
IPhysicsObject *PhysModelCreateCustom( IHandleEntity *pEntity, const CPhysCollide *pModel, const Vector &origin, const QAngle &angles, const char *pName, bool isStatic, solid_t *pSolid = NULL );

// Create a bbox collision model (these may be shared among entities, they are auto-deleted at end of level. do not manage)
CPhysCollide *PhysCreateBbox( const Vector &mins, const Vector &maxs );

// Create a vphysics sphere object
IPhysicsObject *PhysSphereCreate( IHandleEntity *pEntity, float radius, const Vector &origin, solid_t &solid );

// Destroy a physics object created using PhysModelCreate...()
void PhysDestroyObject( IPhysicsObject *pObject, IHandleEntity *pEntity = NULL );



// create the world physics objects
IPhysicsObject *PhysCreateWorld_Shared( IHandleEntity *pWorld, vcollide_t *pWorldCollide, const objectparams_t &defaultParams );

// parse the parameters for a single solid from the model's collision data
bool PhysModelParseSolid( solid_t &solid, IHandleEntity *pEntity, int modelIndex );
// parse the parameters for a solid matching a particular index
bool PhysModelParseSolidByIndex( solid_t &solid, IHandleEntity *pEntity, int modelIndex, int solidIndex );

void PhysParseSurfaceData( class IPhysicsSurfaceProps *pProps, class IFileSystem *pFileSystem );

// fill out this solid_t with the AABB defaults (high inertia/no rotation)
void PhysGetDefaultAABBSolid( solid_t &solid );

// Compute an output velocity based on sliding along the current contact points 
// in the closest direction toward inputVelocity.
void PhysComputeSlideDirection( IPhysicsObject *pPhysics, const Vector &inputVelocity, const AngularImpulse &inputAngularVelocity, 
							   Vector *pOutputVelocity, Vector *pOutputAngularVelocity, float minMass );

void PhysForceClearVelocity( IPhysicsObject *pPhys );
bool PhysHasContactWithOtherInDirection( IPhysicsObject *pPhysics, const Vector &dir );

//-----------------------------------------------------------------------------
// Singleton access
//-----------------------------------------------------------------------------
extern IVPhysicsKeyHandler* g_pSolidSetup;


#endif // PHYSICS_SHARED_H
