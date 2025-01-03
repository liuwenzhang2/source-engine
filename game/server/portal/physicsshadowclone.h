//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Clones a physics object by use of shadows
//
// $NoKeywords: $
//=============================================================================//

#ifndef PHYSICSSHADOWCLONE_H
#define PHYSICSSHADOWCLONE_H

#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"
#include "baseanimating.h"
#include "engine/IEngineTrace.h"

class CPhysicsShadowClone;
class IPhysicsEnvironment;





class CPhysicsShadowClone : public CBaseAnimating
{
	DECLARE_CLASS( CPhysicsShadowClone, CBaseAnimating );

private:

public:
	CPhysicsShadowClone( void );
	virtual ~CPhysicsShadowClone( void );
	
	static int GetEngineObjectTypeStatic() { return ENGINEOBJECT_SHADOWCLONE; }


	//do the thing with the stuff, you know, the one that goes WooooWooooWooooWooooWoooo
	virtual void	Spawn( void );

	//crush, kill, DESTROY!!!!!
	//void			Free( void );



	//virtual bool CreateVPhysics( void );
	virtual int		ObjectCaps( void );
	virtual void	UpdateOnRemove( void );



	//routing to the source entity for cloning goodness
	virtual	bool	ShouldCollide( int collisionGroup, int contentsMask ) const;

	//avoid blocking traces that are supposed to hit our source entity
	virtual bool	TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );




	virtual void	VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	//damage relays to source entity if anything ever hits the clone
	virtual bool	PassesDamageFilter( const CTakeDamageInfo &info );
	virtual bool	CanBeHitByMeleeAttack( IServerEntity *pAttacker );
	virtual int		OnTakeDamage( const CTakeDamageInfo &info );
	virtual int		TakeHealth( float flHealth, int bitsDamageType );
	virtual void	Event_Killed( const CTakeDamageInfo &info );

	//static bool IsShadowClone( const CBaseEntity *pEntity );
	//static CPhysicsShadowCloneLL *GetClonesOfEntity( const CBaseEntity *pEntity );

};



class CTraceFilterTranslateClones : public CTraceFilter //give it another filter, and it'll translate shadow clones into their source entity for tests
{
	ITraceFilter *m_pActualFilter; //the filter that tests should be forwarded to after translating clones

public:
	CTraceFilterTranslateClones( ITraceFilter *pOtherFilter ) : m_pActualFilter(pOtherFilter) {};
	virtual bool ShouldHitEntity( IHandleEntity *pEntity, int contentsMask );
	virtual TraceType_t	GetTraceType() const;
};

#endif //#ifndef PHYSICSSHADOWCLONE_H
