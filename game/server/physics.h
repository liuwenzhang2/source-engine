//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: This is the abstraction layer for the physics simulation system
// Any calls to the external physics library (ipion) should be made through this
// layer.  Eventually, the physics system will probably become a DLL and made 
// accessible to the client & server side code.
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#ifndef PHYSICS_H
#define PHYSICS_H

#ifdef _WIN32
#pragma once
#endif

//#include "physics_shared.h"

class CBaseEntity;
class IPhysicsMaterial;
class IPhysicsConstraint;
class IPhysicsSpring;
class IPhysicsSurfaceProps;
class CTakeDamageInfo;
class ConVar;
class CEntityList;

//extern IPhysicsMaterial		*g_Material;

struct objectparams_t;

class IPhysicsCollisionSolver;
class IPhysicsCollisionEvent;
class IPhysicsObjectEvent;
//extern IPhysicsCollisionSolver * const g_pCollisionSolver;
//extern IPhysicsCollisionEvent * const g_pCollisionEventHandler;
//extern IPhysicsObjectEvent * const g_pObjectEventHandler;

// HACKHACK: We treat anything >= 500kg as a special "large mass" that does more impact damage
// and has special recovery on crushing/killing other objects
// also causes screen shakes on impact with static/world objects

//extern CEntityList *g_pShadowEntities;
//#ifdef PORTAL
//extern CEntityList *g_pShadowEntities_Main;
//#endif
//void PhysAddShadow( CBaseEntity *pEntity );
//void PhysRemoveShadow( CBaseEntity *pEntity );
//bool PhysHasShadow( CBaseEntity *pEntity );


//void PhysCollisionSound( CBaseEntity *pEntity, IPhysicsObject *pPhysObject, int channel, int surfaceProps, int surfacePropsHit, float deltaTime, float speed );

//void PhysBreakSound( CBaseEntity *pEntity, IPhysicsObject *pPhysObject, Vector vecOrigin );

// plays the impact sound for a particular material
//void PhysicsImpactSound( CBaseEntity *pEntity, IPhysicsObject *pPhysObject, int channel, int surfaceProps, int surfacePropsHit, float volume, float impactSpeed );

//void PhysCallbackDamage( CBaseEntity *pEntity, const CTakeDamageInfo &info );
//void PhysCallbackDamage( CBaseEntity *pEntity, const CTakeDamageInfo &info, gamevcollisionevent_t &event, int hurtIndex );

// Applies force impulses at a later time
//void PhysCallbackImpulse( IPhysicsObject *pPhysicsObject, const Vector &vecCenterForce, const AngularImpulse &vecCenterTorque );

// Sets the velocity at a later time
//void PhysCallbackSetVelocity( IPhysicsObject *pPhysicsObject, const Vector &vecVelocity );

// queue up a delete on this object
//void PhysCallbackRemove(CBaseEntity *pRemove);

//bool PhysGetDamageInflictorVelocityStartOfFrame( IPhysicsObject *pInflictor, Vector &velocity, AngularImpulse &angVelocity );

// force a physics entity to sleep immediately
//void PhysForceEntityToSleep( CBaseEntity *pEntity, IPhysicsObject *pObject );

// teleport an entity to it's position relative to an object it's constrained to
//void PhysTeleportConstrainedEntity( CBaseEntity *pTeleportSource, IPhysicsObject *pObject0, IPhysicsObject *pObject1, const Vector &prevPosition, const QAngle &prevAngles, bool physicsRotate );

//void PhysGetListOfPenetratingEntities( CBaseEntity *pSearch, CUtlVector<CBaseEntity *> &list );
//bool PhysShouldCollide( IPhysicsObject *pObj0, IPhysicsObject *pObj1 );

//bool PhysGetTriggerEvent( triggerevent_t *pEvent, CBaseEntity *pTrigger );
// note: pErrorEntity is used to report errors (object not found, more than one found).  It can be NULL

// this is called to flush all queues when the delete list is cleared
//void PhysOnCleanupDeleteList();



//void PhysSetMassCenterOverride( masscenteroverride_t &override );
// NOTE: this removes the entry from the table as well as retrieving it
//void PhysGetMassCenterOverride( CBaseEntity *pEntity, vcollide_t *pCollide, solid_t &solidOut );


#endif		// PHYSICS_H
