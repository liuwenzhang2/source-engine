//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//


#include "cbase.h"
#include "hl1_basecombatweapon_shared.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"


BEGIN_DATADESC( CBaseHL1CombatWeapon )
	DEFINE_THINKFUNC( FallThink ),
END_DATADESC();


void CBaseHL1CombatWeapon::Precache()
{
	BaseClass::Precache();

	g_pSoundEmitterSystem->PrecacheScriptSound( "BaseCombatWeapon.WeaponDrop" );
}

bool CBaseHL1CombatWeapon::CreateVPhysics( void )
{
	GetEngineObject()->VPhysicsInitNormal( SOLID_BBOX, GetEngineObject()->GetSolidFlags() | FSOLID_TRIGGER, false );
	IPhysicsObject *pPhysObj = VPhysicsGetObject();
        if ( pPhysObj )
	{
		pPhysObj->SetMass( 30 );
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseHL1CombatWeapon::FallInit( void )
{
	SetModel( GetWorldModel() );

	if( !CreateVPhysics() )
	{
		GetEngineObject()->SetSolid( SOLID_BBOX );
		GetEngineObject()->SetMoveType( MOVETYPE_FLYGRAVITY );
		GetEngineObject()->SetSolid( SOLID_BBOX );
		GetEngineObject()->AddSolidFlags( FSOLID_TRIGGER );
	}

	SetPickupTouch();

	SetThink( &CBaseHL1CombatWeapon::FallThink );

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

	// HACKHACK - On ground isn't always set, so look for ground underneath
	trace_t tr;
	UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() - Vector(0,0,256), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

	GetEngineObject()->SetAbsOrigin( tr.endpos );

	if ( tr.fraction < 1.0 )
	{
		GetEngineObject()->SetGroundEntity((CBaseEntity*)tr.m_pEnt ? ((CBaseEntity*)tr.m_pEnt)->GetEngineObject() : NULL);
	}

	SetViewOffset( Vector(0,0,8) );
}


//-----------------------------------------------------------------------------
// Purpose: Items that have just spawned run this think to catch them when 
//			they hit the ground. Once we're sure that the object is grounded, 
//			we change its solid type to trigger and set it in a large box that 
//			helps the player get it.
//-----------------------------------------------------------------------------
void CBaseHL1CombatWeapon::FallThink ( void )
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

	bool shouldMaterialize = false;
	IPhysicsObject *pPhysics = VPhysicsGetObject();
	if ( pPhysics )
	{
		shouldMaterialize = pPhysics->IsAsleep();
	}
	else
	{
		shouldMaterialize = (GetEngineObject()->GetFlags() & FL_ONGROUND) ? true : false;
		if( shouldMaterialize )
			SetSize( Vector( -24, -24, 0 ), Vector( 24, 24, 16 ) );
	}

	if ( shouldMaterialize )
	{
		// clatter if we have an owner (i.e., dropped by someone)
		// don't clatter if the gun is waiting to respawn (if it's waiting, it is invisible!)
		if ( GetOwnerEntity() )
		{
			const char* soundname = "BaseCombatWeapon.WeaponDrop";
			CPASAttenuationFilter filter(this, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
		}

		// lie flat
		Materialize();

	}
}

