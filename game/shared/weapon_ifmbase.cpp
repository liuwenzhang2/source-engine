//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "weapon_ifmbase.h"

#if defined( CLIENT_DLL )

	#include "vgui/ISurface.h"
	#include "vgui_controls/Controls.h"
	#include "hud_crosshair.h"

#endif


//-----------------------------------------------------------------------------
// CWeaponIFMBase tables.
//-----------------------------------------------------------------------------
IMPLEMENT_NETWORKCLASS_ALIASED( WeaponIFMBase, DT_WeaponIFMBase )

BEGIN_NETWORK_TABLE( CWeaponIFMBase, DT_WeaponIFMBase )	
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CWeaponIFMBase ) 
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_ifm_base, CWeaponIFMBase );


#ifdef GAME_DLL

BEGIN_DATADESC( CWeaponIFMBase )

END_DATADESC()

#endif

//-----------------------------------------------------------------------------
// CWeaponIFMBase implementation. 
//-----------------------------------------------------------------------------
CWeaponIFMBase::CWeaponIFMBase()
{
	//SetPredictionEligible( true );
}

#ifdef GAME_DLL
void CWeaponIFMBase::PostConstructor(const char* szClassname, int iForceEdictIndex)
{
	BaseClass::PostConstructor(szClassname, iForceEdictIndex);
	GetEngineObject()->AddSolidFlags(FSOLID_TRIGGER); // Nothing collides with these but it gets touches.
}
#endif // GAME_DLL
#ifdef CLIENT_DLL
bool CWeaponIFMBase::Init(int entnum, int iSerialNum)
{
	bool bRet = BaseClass::Init(entnum, iSerialNum);
	GetEngineObject()->AddSolidFlags(FSOLID_TRIGGER); // Nothing collides with these but it gets touches.
	return bRet;
}
#endif // CLIENT_DLL

bool CWeaponIFMBase::IsPredicted() const
{ 
	return true;
}

#ifdef CLIENT_DLL
	
void CWeaponIFMBase::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );

	if ( GetPredictable() && !ShouldPredict() )
	{
		ShutdownPredictable();
	}
}

bool CWeaponIFMBase::ShouldPredict()
{
	if ( GetOwner() && GetOwner() == (C_BasePlayer*)EntityList()->GetLocalPlayer() )
		return true;

	return BaseClass::ShouldPredict();
}


#else
	
void CWeaponIFMBase::Spawn()
{
	BaseClass::Spawn();

	// Set this here to allow players to shoot dropped weapons
	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_WEAPON );
}

#endif

/*
void CWeaponIFMBase::FallInit( void )
{
#ifndef CLIENT_DLL
	SetModel( GetWorldModel() );
	VPhysicsDestroyObject();

	if ( HasSpawnFlags( SF_NORESPAWN ) == false )
	{
		GetEngineObject()->SetMoveType( MOVETYPE_NONE );
		SetSolid( SOLID_BBOX );
		AddSolidFlags( FSOLID_TRIGGER );

		UTIL_DropToFloor( this, MASK_SOLID );
	}
	else
	{
		if ( !VPhysicsInitNormal( SOLID_BBOX, GetSolidFlags() | FSOLID_TRIGGER, false ) )
		{
			GetEngineObject()->SetMoveType( MOVETYPE_NONE );
			SetSolid( SOLID_BBOX );
			AddSolidFlags( FSOLID_TRIGGER );
		}
	}

	SetPickupTouch();
	
	SetThink( &CBaseCombatWeapon::FallThink );

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

#endif
}
*/
