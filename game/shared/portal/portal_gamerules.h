//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game rules for Portal.
//
//=============================================================================//

#ifdef PORTAL_MP



#include "portal_mp_gamerules.h" //redirect to multiplayer gamerules in multiplayer builds



#else

#ifndef PORTAL_GAMERULES_H
#define PORTAL_GAMERULES_H
#ifdef _WIN32
#pragma once
#endif

#include "gamerules.h"
#include "hl2_gamerules.h"

#ifdef CLIENT_DLL
	#define CPortalGameWorld C_PortalGameWorld
#endif

#if defined ( CLIENT_DLL )
#include "steam/steam_api.h"
#endif

class CPortalGameWorld : public CHalfLife2World
{
public:
	DECLARE_CLASS(CPortalGameWorld, CHalfLife2World);
	
	virtual bool	ShouldCollide( int collisionGroup0, int collisionGroup1 );
	virtual bool	ShouldUseRobustRadiusDamage(CBaseEntity *pEntity);
#ifndef CLIENT_DLL
	virtual bool	ShouldAutoAim( CBasePlayer *pPlayer, CBaseEntity *target );
	virtual float	GetAutoAimScale( CBasePlayer *pPlayer );
#endif

#ifdef CLIENT_DLL
	virtual bool IsBonusChallengeTimeBased( void );
#endif

private:
	// Rules change for the mega physgun
	CNetworkVar( bool, m_bMegaPhysgun );

#ifdef CLIENT_DLL

	DECLARE_CLIENTCLASS(); // This makes datatables able to access our private vars.

#else

	DECLARE_SERVERCLASS(); // This makes datatables able to access our private vars.

	CPortalGameWorld();
	virtual ~CPortalGameWorld() {}

	virtual void			Think( void );

	virtual bool			ClientCommand( CBaseEntity *pEdict, const CCommand &args );
	virtual void			PlayerSpawn( CBasePlayer *pPlayer );

	virtual void			InitDefaultAIRelationships( void );
	virtual const char*		AIClassText(int classType);
	virtual const char *GetGameDescription( void ) { return "Portal"; }

	// Ammo
	virtual void			PlayerThink( CBasePlayer *pPlayer );
	virtual float			GetAmmoDamage( CBaseEntity *pAttacker, CBaseEntity *pVictim, int nAmmoType );

	virtual bool			ShouldBurningPropsEmitLight();

	bool ShouldRemoveRadio( void );
	
public:

	virtual float FlPlayerFallDamage( CBasePlayer *pPlayer );

	bool	MegaPhyscannonActive( void ) { return m_bMegaPhysgun;	}

private:

	int						DefaultFOV( void ) { return 75; }
#endif
};


//-----------------------------------------------------------------------------
// Gets us at the Half-Life 2 game rules
//-----------------------------------------------------------------------------
inline CPortalGameWorld* PortalGameRules()
{
	return (CPortalGameWorld*)EntityList()->GetBaseEntity(0);
}

#endif // PORTAL_GAMERULES_H
#endif
