//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Provides structures and classes necessary to simulate a portal.
//
// $NoKeywords: $
//=====================================================================================//

#ifndef PORTALSIMULATION_H
#define PORTALSIMULATION_H

#ifdef _WIN32
#pragma once
#endif

//#include "mathlib/polyhedron.h"
#include "const.h"
#include "tier1/utlmap.h"
#include "tier1/utlvector.h"
#ifdef CLIENT_DLL
#include "c_baseanimating.h"
#include "PortalRender.h"
#endif // CLIENT_DLL
#ifdef GAME_DLL
#include "baseentity.h"
#endif // GAME_DLL


#define PORTAL_SIMULATORS_EMBED_GUID //define this to embed a unique integer with each portal simulator for debugging purposes



#ifdef CLIENT_DLL
#define CPSCollisionEntity C_PSCollisionEntity
#define CPortalSimulator C_PortalSimulator
#endif // CLIENT_DLL

class CPortalSimulator;

class CPSCollisionEntity : public CBaseEntity
{
public:
	DECLARE_CLASS(CPSCollisionEntity, CBaseEntity);
	DECLARE_NETWORKCLASS();
	CPSCollisionEntity(void);
	virtual ~CPSCollisionEntity(void);
#ifdef GAME_DLL
	virtual void StartTouch(CBaseEntity* pOther);
	virtual void Touch(CBaseEntity* pOther);
	virtual void EndTouch(CBaseEntity* pOther);
	virtual int UpdateTransmitState(void)	// set transmit filter to transmit always
	{
		return SetTransmitState(FL_EDICT_ALWAYS);
	}
#endif // GAME_DLL
	virtual void	Spawn(void);
	virtual void	Activate(void);
	virtual int		ObjectCaps(void);
	virtual void	UpdateOnRemove(void);
	virtual bool	TestCollision(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr);
private:
	CNetworkHandle(CPortalSimulator, m_pOwningSimulator);
	CPhysCollide* m_pCollisionShape;
};

class CPortalSimulator : public CBaseAnimating
#ifdef CLIENT_DLL
	, public CPortalRenderable
#endif // CLIENT_DLL
{
	DECLARE_CLASS(CPortalSimulator, CBaseEntity);
public:
	DECLARE_NETWORKCLASS();

	CPortalSimulator( void );
	~CPortalSimulator( void );
	//static bool IsNetworkableStatic(void) { return false; }
	//virtual bool IsNetworkable(void) { return CPortalSimulator::IsNetworkableStatic(); }
	static int GetEngineObjectTypeStatic() { return ENGINEOBJECT_PORTAL; }
#ifdef CLIENT_DLL
	virtual bool Init(int entnum, int iSerialNum);
	virtual void GetToolRecordingState(KeyValues* msg);
#endif // CLIENT_DLL

#ifdef GAME_DLL
	virtual void PostConstructor(const char* szClassname, int iForceEdictIndex);
	virtual int UpdateTransmitState(void)	// set transmit filter to transmit always
	{
		return SetTransmitState(FL_EDICT_ALWAYS);
	}
	virtual void	VPhysicsCollision(int index, gamevcollisionevent_t* pEvent) {};
	virtual void	VPhysicsFriction(IPhysicsObject* pObject, float energy, int surfaceProps, int surfacePropsHit) {};
#endif // GAME_DLL
	virtual void		Spawn(void);
	void				UpdateOnRemove(void);
	virtual	bool		ShouldCollide(int collisionGroup, int contentsMask) const;
	void				MoveTo( const Vector &ptCenter, const QAngle &angles );

	virtual void		OnClearEverything() {};
	void				ClearEverything( void );

	void				AttachTo( CPortalSimulator *pLinkedPortalSimulator );
	virtual void		BeforeDetachFromLinked() {};
	void				DetachFromLinked( void ); //detach portals to sever the connection, saves work when planning on moving both portals
	CPortalSimulator	*GetLinkedPortalSimulator( void ) const;
	CPortalSimulator*	GetLinkedPortal() { return m_hLinkedPortal.Get(); }
	const CPortalSimulator* GetLinkedPortal() const{ return m_hLinkedPortal.Get(); }
	//void				SetPortalSimulatorCallbacks( CPortalSimulatorEventCallbacks *pCallbacks );
	
	bool				IsReadyToSimulate( void ) const; //is active and linked to another portal
	bool				IsActivedAndLinked(void) const;
	//const Vector&		GetOrigin() const;
	//const QAngle&		GetAngles() const;
	const VMatrix&		MatrixThisToLinked() const;
	const VMatrix&		MatrixLinkedToThis() const;
	const cplane_t&		GetPortalPlane() const;
	//const PS_InternalData_t& GetDataAccess() const;
	const Vector&		GetVectorForward() const;
	const Vector&		GetVectorUp() const;
	const Vector&		GetVectorRight() const;
	const PS_SD_Static_SurfaceProperties_t& GetSurfaceProperties() const;
	IPhysicsEnvironment* GetPhysicsEnvironment();

	void				SetCollisionGenerationEnabled( bool bEnabled ); //enable/disable collision generation for the hole in the wall, needed for proper vphysics simulation
	bool				IsCollisionGenerationEnabled( void ) const;

	void				SetVPhysicsSimulationEnabled( bool bEnabled ); //enable/disable vphysics simulation. Will automatically update the linked portal to be the same
	bool				IsSimulatingVPhysics( void ) const; //this portal is setup to handle any physically simulated object, false means the portal is handling player movement only
	
	bool				EntityIsInPortalHole( CBaseEntity *pEntity ) const; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	bool				EntityHitBoxExtentIsInPortalHole( CBaseAnimating *pBaseAnimating ) const; //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
	bool				RayIsInPortalHole(const Ray_t& ray) const;
	bool				CreatedPhysicsObject(const IPhysicsObject* pObject, PS_PhysicsObjectSourceType_t* pOut_SourceType = NULL) const; //true if the physics object was generated by this portal simulator
	virtual int				GetMoveableOwnedEntities(CBaseEntity** pEntsOut, int iEntOutLimit) { return 0; } //gets owned entities that aren't either world or static props. Excludes fake portal ents such as physics clones
#ifdef PORTAL_SIMULATORS_EMBED_GUID
	int					GetPortalSimulatorGUID( void ) const { return GetEnginePortal()->GetPortalSimulatorGUID(); };
#endif
	//bool				IsPortal2() const;
	//void				SetIsPortal2(bool bIsPortal2);
protected:
	bool				m_bGenerateCollision;
	bool				m_bSharedCollisionConfiguration; //when portals are in certain configurations, they need to cross-clip and share some collision data and things get nasty. For the love of all that is holy, pray that this is false.
	CPortalSimulator	*m_pLinkedPortal;
	CNetworkHandle(CPortalSimulator, m_hLinkedPortal); //the portal this portal is linked to
	bool				m_bInCrossLinkedFunction; //A flag to mark that we're already in a linked function and that the linked portal shouldn't call our side
	//CPortalSimulatorEventCallbacks *m_pCallbacks; 


	struct
	{
		bool			bPolyhedronsGenerated;
		bool			bLocalCollisionGenerated;
		bool			bLinkedCollisionGenerated;
		bool			bLocalPhysicsGenerated;
		bool			bLinkedPhysicsGenerated;
	} m_CreationChecklist;

	//friend class CPSCollisionEntity;


#ifndef CLIENT_DLL //physics handled purely by server side
	void				CreateAllPhysics( void );
	void				CreateMinimumPhysics( void ); //stuff needed by any part of physics simulations
	void				CreateLocalPhysics( void );
	void				CreateLinkedPhysics( void );

	void				ClearAllPhysics( void );
	void				ClearMinimumPhysics( void );
	void				ClearLocalPhysics( void );
	void				ClearLinkedPhysics( void );

#endif

	void				CreateAllCollision( void );
	void				CreateLocalCollision( void );
	void				CreateLinkedCollision( void );

	void				ClearAllCollision( void );
	void				ClearLinkedCollision( void );
	void				ClearLocalCollision( void );

	void				CreatePolyhedrons( void ); //carves up the world around the portal's position into sets of polyhedrons
	void				ClearPolyhedrons( void );

	void				UpdateLinkMatrix( void );


	CNetworkHandle(CPSCollisionEntity, pCollisionEntity); //the entity we'll be tying physics objects to for collision


public:

	//friend class CPS_AutoGameSys_EntityListener;
};

//-----------------------------------------------------------------------------
// inline state querying methods
//-----------------------------------------------------------------------------
//inline bool	CPortalSimulator::IsPortal2() const
//{
//	return m_bIsPortal2;
//}

//inline void	CPortalSimulator::SetIsPortal2(bool bIsPortal2)
//{
//	m_bIsPortal2 = bIsPortal2;
//}

extern CUtlVector<CPortalSimulator *> const &g_PortalSimulators;

inline bool CPortalSimulator::IsReadyToSimulate( void ) const
{
	return GetEnginePortal()->IsReadyToSimulate();
}

inline bool CPortalSimulator::IsActivedAndLinked(void) const
{
	return GetEnginePortal()->IsActivedAndLinked();
}

inline bool CPortalSimulator::IsSimulatingVPhysics( void ) const
{
	return GetEnginePortal()->IsSimulatingVPhysics();
}

inline void CPortalSimulator::SetCollisionGenerationEnabled(bool bEnabled)
{
	m_bGenerateCollision = bEnabled;
}

inline bool CPortalSimulator::IsCollisionGenerationEnabled( void ) const
{
	return m_bGenerateCollision;
}

inline CPortalSimulator	*CPortalSimulator::GetLinkedPortalSimulator( void ) const
{
	return m_pLinkedPortal;
}




#endif //#ifndef PORTALSIMULATION_H

