//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Tripmine
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "hl1_basecombatweapon_shared.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#include "soundent.h"
#include "game.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "hl1_player.h"
#include "hl1_basegrenade.h"
#include "beam_shared.h"

extern ConVar sk_plr_dmg_tripmine;


//-----------------------------------------------------------------------------
// CWeaponTripMine
//-----------------------------------------------------------------------------


#define TRIPMINE_MODEL "models/w_tripmine.mdl"


class CWeaponTripMine : public CBaseHL1CombatWeapon
{
	DECLARE_CLASS( CWeaponTripMine, CBaseHL1CombatWeapon );
public:

	CWeaponTripMine( void );

	void	Spawn( void );
	void	Precache( void );
	void	Equip( CBaseCombatCharacter *pOwner );
	void	PrimaryAttack( void );
	void	WeaponIdle( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

private:
	int		m_iGroundIndex;
	int		m_iPickedUpIndex;
};

LINK_ENTITY_TO_CLASS( weapon_tripmine, CWeaponTripMine );

PRECACHE_WEAPON_REGISTER( weapon_tripmine );

IMPLEMENT_SERVERCLASS_ST( CWeaponTripMine, DT_WeaponTripMine )
END_SEND_TABLE()

BEGIN_DATADESC( CWeaponTripMine )
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CWeaponTripMine::CWeaponTripMine( void )
{
	m_bReloadsSingly	= false;
	m_bFiresUnderwater	= true;
}

void CWeaponTripMine::Spawn( void )
{
	BaseClass::Spawn();

	m_iWorldModelIndex = m_iGroundIndex;
	SetModel( TRIPMINE_MODEL );

	SetActivity( ACT_TRIPMINE_GROUND );
	GetEngineObject()->ResetSequenceInfo( );
	GetEngineObject()->SetPlaybackRate(0);

	if ( !g_pGameRules->IsDeathmatch() )
	{
		UTIL_SetSize( this, Vector(-16, -16, 0), Vector(16, 16, 28) ); 
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponTripMine::Precache( void )
{
	BaseClass::Precache();

	m_iGroundIndex		= engine->PrecacheModel( TRIPMINE_MODEL );
	m_iPickedUpIndex	= engine->PrecacheModel( GetWorldModel() );

	UTIL_PrecacheOther( "monster_tripmine" );
}

void CWeaponTripMine::Equip( CBaseCombatCharacter *pOwner )
{
	m_iWorldModelIndex = m_iPickedUpIndex;

	BaseClass::Equip( pOwner );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponTripMine::PrimaryAttack( void )
{
	CHL1_Player *pPlayer = ToHL1Player( GetOwner() );
	if ( !pPlayer )
	{
		return;
	}

	if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
		return;

	Vector vecAiming	= pPlayer->GetAutoaimVector( 0 );
	Vector vecSrc		= pPlayer->Weapon_ShootPosition( );

	trace_t tr;

	UTIL_TraceLine( vecSrc, vecSrc + vecAiming * 64, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );

	if ( tr.fraction < 1.0 )
	{
		CBaseEntity *pEntity = (CBaseEntity*)tr.m_pEnt;
		if ( pEntity && !( pEntity->GetEngineObject()->GetFlags() & FL_CONVEYOR ) )
		{
			QAngle angles;
			VectorAngles( tr.plane.normal, angles );

			CBaseEntity::Create( "monster_tripmine", tr.endpos + tr.plane.normal * 2, angles, pPlayer );

			pPlayer->RemoveAmmo( 1, m_iPrimaryAmmoType );

			pPlayer->SetAnimation( PLAYER_ATTACK1 );
			
			if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
			{
				if ( !pPlayer->SwitchToNextBestWeapon( pPlayer->GetActiveWeapon() ) )
					Holster();
			}
			else
			{
				SendWeaponAnim( ACT_VM_DRAW );
				SetWeaponIdleTime( gpGlobals->curtime + random->RandomFloat( 10, 15 ) );
			}

			m_flNextPrimaryAttack = gpGlobals->curtime + 0.5;
			
			SetWeaponIdleTime( gpGlobals->curtime ); // MO curtime correct ?
		}
	}
	else
	{
		SetWeaponIdleTime( m_flTimeWeaponIdle = gpGlobals->curtime + random->RandomFloat( 10, 15 ) );
	}
}

void CWeaponTripMine::WeaponIdle( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( !pPlayer )
	{
		return;
	}

	if ( !HasWeaponIdleTimeElapsed() )
		return;

	int iAnim;

	if ( random->RandomFloat( 0, 1 ) <= 0.75 )
	{
		iAnim = ACT_VM_IDLE;
	}
	else
	{
		iAnim = ACT_VM_FIDGET;
	}

	SendWeaponAnim( iAnim );
}

bool CWeaponTripMine::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
	{
		return false;
	}

	if ( !BaseClass::Holster( pSwitchingTo ) )
	{
		return false;
	}

	if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		SetThink( &CWeaponTripMine::DestroyItem );
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1 );
	}

	pPlayer->SetNextAttack( gpGlobals->curtime + 0.5 );

	return true;
}


//-----------------------------------------------------------------------------
// CTripmineGrenade
//-----------------------------------------------------------------------------

#define TRIPMINE_BEAM_SPRITE "sprites/laserbeam.vmt"

class CTripmineGrenade : public CHL1BaseGrenade
{
	DECLARE_CLASS( CTripmineGrenade, CHL1BaseGrenade );
public:
	CTripmineGrenade();
	void		Spawn( void );
	void		Precache( void );

	int			OnTakeDamage_Alive( const CTakeDamageInfo &info );
	
	void		WarningThink( void );
	void		PowerupThink( void );
	void		BeamBreakThink( void );
	void		DelayDeathThink( void );
	void		Event_Killed( const CTakeDamageInfo &info );

	DECLARE_DATADESC();

private:
	void			MakeBeam( void );
	void			KillBeam( void );

private:
	float					m_flPowerUp;
	Vector					m_vecDir;
	Vector					m_vecEnd;
	float					m_flBeamLength;

	CHandle<CBaseEntity>	m_hRealOwner;
	CHandle<CBeam>			m_hBeam;

	CHandle<CBaseEntity>	m_hStuckOn;
	Vector					m_posStuckOn;
	QAngle					m_angStuckOn;

	int						m_iLaserModel;
};

LINK_ENTITY_TO_CLASS( monster_tripmine, CTripmineGrenade );

BEGIN_DATADESC( CTripmineGrenade )
	DEFINE_FIELD( m_flPowerUp,	FIELD_TIME ),
	DEFINE_FIELD( m_vecDir,		FIELD_VECTOR ),
	DEFINE_FIELD( m_vecEnd,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flBeamLength, FIELD_FLOAT ),
//	DEFINE_FIELD( m_hBeam,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_hRealOwner,	FIELD_EHANDLE ),
	DEFINE_FIELD( m_hStuckOn,	FIELD_EHANDLE ),
	DEFINE_FIELD( m_posStuckOn,	FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_angStuckOn,	FIELD_VECTOR ),
	//DEFINE_FIELD( m_iLaserModel, FIELD_INTEGER ),

	// Function Pointers
	DEFINE_THINKFUNC( WarningThink ),
	DEFINE_THINKFUNC( PowerupThink ),
	DEFINE_THINKFUNC( BeamBreakThink ),
	DEFINE_THINKFUNC( DelayDeathThink ),
END_DATADESC()


CTripmineGrenade::CTripmineGrenade()
{
	m_vecDir.Init();
	m_vecEnd.Init();
}

void CTripmineGrenade::Spawn( void )
{
	Precache( );
	// motor
	GetEngineObject()->SetMoveType( MOVETYPE_FLY );
	GetEngineObject()->SetSolid( SOLID_BBOX );
	GetEngineObject()->AddSolidFlags( FSOLID_NOT_SOLID );
	SetModel( TRIPMINE_MODEL );

	// Don't collide with the player (the beam will still be tripped by one, however)
	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_WEAPON );

	GetEngineObject()->SetCycle( 0 );
	GetEngineObject()->SetSequence(GetEngineObject()->SelectWeightedSequence( ACT_TRIPMINE_WORLD ) );
	GetEngineObject()->ResetSequenceInfo();
	GetEngineObject()->SetPlaybackRate(0);
	
	UTIL_SetSize( this, Vector( -8, -8, -8), Vector(8, 8, 8) );

	m_flDamage	= sk_plr_dmg_tripmine.GetFloat();
	m_DmgRadius	= m_flDamage * 2.5;

	if (GetEngineObject()->GetSpawnFlags() & 1 )//need check
	{
		// power up quickly
		m_flPowerUp = gpGlobals->curtime + 1.0;
	}
	else
	{
		// power up in 2.5 seconds
		m_flPowerUp = gpGlobals->curtime + 2.5;
	}
	
	SetThink( &CTripmineGrenade::PowerupThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.2 );

	m_takedamage = DAMAGE_YES;

	m_iHealth = 1;

	if ( GetOwnerEntity() != NULL )
	{
		// play deploy sound
		{
			const char* soundname = "TripmineGrenade.Deploy";
			CPASAttenuationFilter filter(this, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
		}
		{
			const char* soundname = "TripmineGrenade.Charge";
			CPASAttenuationFilter filter(this, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
		}

		m_hRealOwner = GetOwnerEntity();
	}
	AngleVectors(GetEngineObject()->GetAbsAngles(), &m_vecDir );
	m_vecEnd = GetEngineObject()->GetAbsOrigin() + m_vecDir * MAX_TRACE_LENGTH;
}


void CTripmineGrenade::Precache( void )
{
	engine->PrecacheModel( TRIPMINE_MODEL );
	m_iLaserModel = engine->PrecacheModel( TRIPMINE_BEAM_SPRITE );

	g_pSoundEmitterSystem->PrecacheScriptSound( "TripmineGrenade.Deploy" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "TripmineGrenade.Charge" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "TripmineGrenade.Activate" );

}


void CTripmineGrenade::WarningThink( void  )
{
	// set to power up
	SetThink( &CTripmineGrenade::PowerupThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 1.0f );
}


void CTripmineGrenade::PowerupThink( void  )
{
	if ( m_hStuckOn == NULL )
	{
		trace_t tr;
		CBaseEntity *pOldOwner = GetOwnerEntity();

		// don't explode if the player is standing in front of the laser
		UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() + m_vecDir * 32, MASK_SHOT, NULL, COLLISION_GROUP_NONE, &tr );

		if( tr.m_pEnt && pOldOwner &&
			( tr.m_pEnt == pOldOwner ) && pOldOwner->IsPlayer() )
		{
			m_flPowerUp += 0.1;	//delay the arming
			GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
			return;
		}

		// find out what we've been stuck on		
		SetOwnerEntity( NULL );
		
		UTIL_TraceLine(GetEngineObject()->GetAbsOrigin() + m_vecDir * 8, GetEngineObject()->GetAbsOrigin() - m_vecDir * 32, MASK_SHOT, pOldOwner, COLLISION_GROUP_NONE, &tr );

		if ( tr.startsolid )
		{
			SetOwnerEntity( pOldOwner );
			m_flPowerUp += 0.1;
			GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
			return;
		}
		if ( tr.fraction < 1.0 )
		{
			SetOwnerEntity((CBaseEntity*)tr.m_pEnt );
			m_hStuckOn		= (CBaseEntity*)tr.m_pEnt;
			m_posStuckOn	= m_hStuckOn->GetEngineObject()->GetAbsOrigin();
			m_angStuckOn	= m_hStuckOn->GetEngineObject()->GetAbsAngles();
		}
		else
		{
			// somehow we've been deployed on nothing, or something that was there, but now isn't.
			// remove ourselves

			g_pSoundEmitterSystem->StopSound(this, "TripmineGrenade.Deploy" );
			g_pSoundEmitterSystem->StopSound(this, "TripmineGrenade.Charge" );
			SetThink( &CBaseEntity::SUB_Remove );
			GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
//			ALERT( at_console, "WARNING:Tripmine at %.0f, %.0f, %.0f removed\n", pev->origin.x, pev->origin.y, pev->origin.z );
			KillBeam();
			return;
		}
	}
	else if ( (m_posStuckOn != m_hStuckOn->GetEngineObject()->GetAbsOrigin()) || (m_angStuckOn != m_hStuckOn->GetEngineObject()->GetAbsAngles()) )
	{
		// what we were stuck on has moved, or rotated. Create a tripmine weapon and drop to ground

		g_pSoundEmitterSystem->StopSound(this, "TripmineGrenade.Deploy" );
		g_pSoundEmitterSystem->StopSound(this, "TripmineGrenade.Charge" );
		CBaseEntity *pMine = Create( "weapon_tripmine", GetEngineObject()->GetAbsOrigin() + m_vecDir * 24, GetEngineObject()->GetAbsAngles() );
		pMine->GetEngineObject()->AddSpawnFlags( SF_NORESPAWN );

		SetThink( &CBaseEntity::SUB_Remove );
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
		KillBeam();
		return;
	}

	if ( gpGlobals->curtime > m_flPowerUp )
	{
		MakeBeam( );
		GetEngineObject()->RemoveSolidFlags( FSOLID_NOT_SOLID );

		m_bIsLive = true;

		// play enabled sound
		const char* soundname = "TripmineGrenade.Activate";
		CPASAttenuationFilter filter(this, soundname);

		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = 0.0f;
		params.m_pflSoundDuration = NULL;
		params.m_bWarnOnDirectWaveReference = true;
		g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
	}

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
}


void CTripmineGrenade::KillBeam( void )
{
	if ( m_hBeam )
	{
		EntityList()->DestroyEntity( m_hBeam );
		m_hBeam = NULL;
	}
}


void CTripmineGrenade::MakeBeam( void )
{
	trace_t tr;

	UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), m_vecEnd, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

	m_flBeamLength = tr.fraction;

	// set to follow laser spot
	SetThink( &CTripmineGrenade::BeamBreakThink );

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 1.0f );

	Vector vecTmpEnd = GetEngineObject()->GetAbsOrigin() + m_vecDir * MAX_TRACE_LENGTH * m_flBeamLength;

	m_hBeam = CBeam::BeamCreate( TRIPMINE_BEAM_SPRITE, 1.0 );
	m_hBeam->PointEntInit( vecTmpEnd, this );
	m_hBeam->SetColor( 0, 214, 198 );
	m_hBeam->SetScrollRate( 25.5 );
	m_hBeam->SetBrightness( 64 );
	m_hBeam->GetEngineObject()->AddSpawnFlags( SF_BEAM_TEMPORARY );	// so it won't save and come back to haunt us later..
}


void CTripmineGrenade::BeamBreakThink( void  )
{
	bool bBlowup = false;
	trace_t tr;

	// NOT MASK_SHOT because we want only simple hit boxes
	UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), m_vecEnd, MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );

	// ALERT( at_console, "%f : %f\n", tr.flFraction, m_flBeamLength );

	// respawn detect. 
	if ( !m_hBeam )
	{
		MakeBeam();

		trace_t stuckOnTrace;
		Vector forward;
		GetVectors( &forward, NULL, NULL );

		UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() - forward * 12.0f, MASK_SOLID, this, COLLISION_GROUP_NONE, &stuckOnTrace );

		if ( stuckOnTrace.m_pEnt )
		{
			m_hStuckOn = (CBaseEntity*)stuckOnTrace.m_pEnt;	// reset stuck on ent too
		}
	}

	CBaseEntity *pEntity = (CBaseEntity*)tr.m_pEnt;
	CBaseCombatCharacter *pBCC  = ToBaseCombatCharacter( pEntity );

	if ( pBCC || fabs( m_flBeamLength - tr.fraction ) > 0.001 )
	{
		bBlowup = true;
	}
	else
	{
		if ( m_hStuckOn == NULL )
			bBlowup = true;
		else if ( m_posStuckOn != m_hStuckOn->GetEngineObject()->GetAbsOrigin() )
			bBlowup = true;
		else if ( m_angStuckOn != m_hStuckOn->GetEngineObject()->GetAbsAngles() )
			bBlowup = true;
	}

	if ( bBlowup )
	{
		SetOwnerEntity( m_hRealOwner );
		m_iHealth = 0;
		Event_Killed( CTakeDamageInfo( this, m_hRealOwner, 100, GIB_NORMAL ) );
		return;
	}

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1 );
}
/*
int CTripmineGrenade::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	if (gpGlobals->curtime < m_flPowerUp && info.GetDamage() < m_iHealth)
	{
		// disable
		// Create( "weapon_tripmine", GetLocalOrigin() + m_vecDir * 24, GetAngles() );
		SetThink( &CBaseEntity::SUB_Remove );
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
		KillBeam();
		return 0;
	}
	return BaseClass::OnTakeDamage_Alive( info );
}*/

void CTripmineGrenade::Event_Killed( const CTakeDamageInfo &info )
{
	m_takedamage = DAMAGE_NO;

	if ( info.GetAttacker() && ( info.GetAttacker()->GetEngineObject()->GetFlags() & FL_CLIENT ) )
	{
		// some client has destroyed this mine, he'll get credit for any kills
		SetOwnerEntity( info.GetAttacker() );
	}

	SetThink( &CTripmineGrenade::DelayDeathThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + random->RandomFloat( 0.1, 0.3 ) );

	g_pSoundEmitterSystem->StopSound(this, "TripmineGrenade.Charge" );
}

void CTripmineGrenade::DelayDeathThink( void )
{
	KillBeam();
	trace_t tr;
	UTIL_TraceLine (GetEngineObject()->GetAbsOrigin() + m_vecDir * 8, GetEngineObject()->GetAbsOrigin() - m_vecDir * 64,  MASK_SOLID, this, COLLISION_GROUP_NONE, & tr);

	Explode( &tr, DMG_BLAST );
}
