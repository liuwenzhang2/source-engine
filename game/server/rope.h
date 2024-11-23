//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ROPE_H
#define ROPE_H
#ifdef _WIN32
#pragma once
#endif


#include "baseentity.h"

#include "positionwatcher.h"

class CRopeKeyframe : public CBaseEntity, public IPositionWatcher
{
	DECLARE_CLASS( CRopeKeyframe, CBaseEntity );
public:
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

					CRopeKeyframe();
	virtual			~CRopeKeyframe();
	static int GetEngineObjectTypeStatic() { return ENGINEOBJECT_ROPE; }
	void			PostConstructor(const char* szClassname, int iForceEdictIndex);
	void			UpdateOnRemove();
	// Create a rope and attach it to two entities.
	// Attachment points on the entities are optional.
	static CRopeKeyframe* Create(
		CBaseEntity *pStartEnt,
		CBaseEntity *pEndEnt,
		int iStartAttachment=0,
		int iEndAttachment=0,
		int ropeWidth = 2,
		const char *pMaterialName = "cable/cable.vmt",		// Note: whoever creates the rope must
															// use PrecacheModel for whatever material
															// it specifies here.
		int numSegments = 5
		);

	static CRopeKeyframe* CreateWithSecondPointDetached(
		CBaseEntity *pStartEnt,
		int iStartAttachment = 0,	// must be 0 if you don't want to use a specific model attachment.
		int ropeLength = 20,
		int ropeWidth = 2,
		const char *pMaterialName = "cable/cable.vmt",		// Note: whoever creates the rope
															// use PrecacheModel for whatever material
															// it specifies here.
		int numSegments = 5,
		bool bInitialHang = false
		);



	
	// Shakes all ropes near vCenter. The higher flMagnitude is, the larger the shake will be.
	static void ShakeRopes( const Vector &vCenter, float flRadius, float flMagnitude );


// CBaseEntity overrides.
public:
	
	// don't cross transitions
	virtual int		ObjectCaps( void ) { return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
	virtual void	Activate();
	virtual void	Precache();
	virtual int		OnTakeDamage( const CTakeDamageInfo &info );
	virtual bool	KeyValue( const char *szKeyName, const char *szValue );

	void			PropagateForce(CBaseEntity *pActivator, CBaseEntity *pCaller, CBaseEntity *pFirstLink, float x, float y, float z);

	

	// Kill myself when I next come to rest
	void			DieAtNextRest( void );

	virtual int		UpdateTransmitState(void);
	virtual void	SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways );
	virtual void	BeforeParentChanged( CBaseEntity *pNewParent, int iNewAttachment );

// Input functions.
public:

	void InputSetScrollSpeed( inputdata_t &inputdata );
	void InputSetForce( inputdata_t &inputdata );
	void InputBreak( inputdata_t &inputdata );

public:

	bool			Break( void );
	

	

	

	



	// See ROPE_PLAYER_WPN_ATTACH for info.
	void			EnablePlayerWeaponAttach( bool bAttach );


	// IPositionWatcher
	virtual void NotifyPositionChanged( CBaseEntity *pEntity );

private:


	





public:

	
	string_t	m_iNextLinkName;
	

	

	bool		m_bCreatedFromMapFile; // set to false when creating at runtime


private:
	
	

};


#endif // ROPE_H
