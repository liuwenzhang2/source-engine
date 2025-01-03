//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef TAKEDAMAGEINFO_H
#define TAKEDAMAGEINFO_H
#ifdef _WIN32
#pragma once
#endif


#include "networkvar.h" // todo: change this when DECLARE_CLASS is moved into a better location.

// Used to initialize m_flBaseDamage to something that we know pretty much for sure
// hasn't been modified by a user. 
#define BASEDAMAGE_NOT_SPECIFIED	FLT_MAX

class IHandleEntity;


class CTakeDamageInfo
{
public:
	DECLARE_CLASS_NOBASE( CTakeDamageInfo );

					CTakeDamageInfo();
					CTakeDamageInfo( IHandleEntity *pInflictor, IHandleEntity *pAttacker, float flDamage, int bitsDamageType, int iKillType = 0 );
					CTakeDamageInfo( IHandleEntity *pInflictor, IHandleEntity *pAttacker, IHandleEntity *pWeapon, float flDamage, int bitsDamageType, int iKillType = 0 );
					CTakeDamageInfo( IHandleEntity *pInflictor, IHandleEntity *pAttacker, const Vector &damageForce, const Vector &damagePosition, float flDamage, int bitsDamageType, int iKillType = 0, Vector *reportedPosition = NULL );
					CTakeDamageInfo( IHandleEntity *pInflictor, IHandleEntity *pAttacker, IHandleEntity *pWeapon, const Vector &damageForce, const Vector &damagePosition, float flDamage, int bitsDamageType, int iKillType = 0, Vector *reportedPosition = NULL );
	

	// Inflictor is the weapon or rocket (or player) that is dealing the damage.
	IHandleEntity*	GetInflictor() const;
	void			SetInflictor( IHandleEntity *pInflictor );

	// Weapon is the weapon that did the attack.
	// For hitscan weapons, it'll be the same as the inflictor. For projectile weapons, the projectile 
	// is the inflictor, and this contains the weapon that created the projectile.
	IHandleEntity*	GetWeapon() const;
	void			SetWeapon( IHandleEntity *pWeapon );

	// Attacker is the character who originated the attack (like a player or an AI).
	IHandleEntity*	GetAttacker() const;
	void			SetAttacker( IHandleEntity *pAttacker );

	float			GetDamage() const;
	void			SetDamage( float flDamage );
	float			GetMaxDamage() const;
	void			SetMaxDamage( float flMaxDamage );
	void			ScaleDamage( float flScaleAmount );
	void			AddDamage( float flAddAmount );
	void			SubtractDamage( float flSubtractAmount );
	float			GetDamageBonus() const;
	void			SetDamageBonus( float flBonus );

	float			GetBaseDamage() const;
	bool			BaseDamageIsValid() const;

	Vector			GetDamageForce() const;
	void			SetDamageForce( const Vector &damageForce );
	void			ScaleDamageForce( float flScaleAmount );

	Vector			GetDamagePosition() const;
	void			SetDamagePosition( const Vector &damagePosition );

	Vector			GetReportedPosition() const;
	void			SetReportedPosition( const Vector &reportedPosition );

	int				GetDamageType() const;
	void			SetDamageType( int bitsDamageType );
	void			AddDamageType( int bitsDamageType );
	int				GetDamageCustom( void ) const;
	void			SetDamageCustom( int iDamageCustom );
	int				GetDamageStats( void ) const;
	void			SetDamageStats( int iDamageStats );
	void			SetForceFriendlyFire( bool bValue ) { m_bForceFriendlyFire = bValue; }
	bool			IsForceFriendlyFire( void ) const { return m_bForceFriendlyFire; }

	int				GetAmmoType() const;
	void			SetAmmoType( int iAmmoType );
	const char *	GetAmmoName() const;

	int				GetPlayerPenetrationCount() const { return m_iPlayerPenetrationCount; }
	void			SetPlayerPenetrationCount( int iPlayerPenetrationCount ) { m_iPlayerPenetrationCount = iPlayerPenetrationCount; }
	
	int				GetDamagedOtherPlayers() const     { return m_iDamagedOtherPlayers; }
	void			SetDamagedOtherPlayers( int iVal ) { m_iDamagedOtherPlayers = iVal; }

	void			Set( IHandleEntity *pInflictor, IHandleEntity *pAttacker, float flDamage, int bitsDamageType, int iKillType = 0 );
	void			Set( IHandleEntity *pInflictor, IHandleEntity *pAttacker, IHandleEntity *pWeapon, float flDamage, int bitsDamageType, int iKillType = 0 );
	void			Set( IHandleEntity *pInflictor, IHandleEntity *pAttacker, const Vector &damageForce, const Vector &damagePosition, float flDamage, int bitsDamageType, int iKillType = 0, Vector *reportedPosition = NULL );
	void			Set( IHandleEntity *pInflictor, IHandleEntity *pAttacker, IHandleEntity *pWeapon, const Vector &damageForce, const Vector &damagePosition, float flDamage, int bitsDamageType, int iKillType = 0, Vector *reportedPosition = NULL );

	void			AdjustPlayerDamageInflictedForSkillLevel();
	void			AdjustPlayerDamageTakenForSkillLevel();

	// Given a damage type (composed of the #defines above), fill out a string with the appropriate text.
	// For designer debug output.
	static void		DebugGetDamageTypeString(unsigned int DamageType, char *outbuf, int outbuflength );


//private:
	void			CopyDamageToBaseDamage();

protected:
	void			Init( IHandleEntity *pInflictor, IHandleEntity *pAttacker, IHandleEntity *pWeapon, const Vector &damageForce, const Vector &damagePosition, const Vector &reportedPosition, float flDamage, int bitsDamageType, int iKillType );

	Vector			m_vecDamageForce;
	Vector			m_vecDamagePosition;
	Vector			m_vecReportedPosition;	// Position players are told damage is coming from
	CHandle<IHandleEntity>	m_hInflictor;
	CHandle<IHandleEntity>	m_hAttacker;
	CHandle<IHandleEntity>	m_hWeapon;
	float			m_flDamage;
	float			m_flMaxDamage;
	float			m_flBaseDamage;			// The damage amount before skill leve adjustments are made. Used to get uniform damage forces.
	int				m_bitsDamageType;
	int				m_iDamageCustom;
	int				m_iDamageStats;
	int				m_iAmmoType;			// AmmoType of the weapon used to cause this damage, if any
	int				m_iDamagedOtherPlayers;
	int				m_iPlayerPenetrationCount;
	float			m_flDamageBonus;		// Anything that increases damage (crit) - store the delta
	bool			m_bForceFriendlyFire;	// Ideally this would be a dmg type, but we can't add more

	DECLARE_SIMPLE_DATADESC();
};

//-----------------------------------------------------------------------------
// Purpose: Multi damage. Used to collect multiple damages in the same frame (i.e. shotgun pellets)
//-----------------------------------------------------------------------------
class CMultiDamage : public CTakeDamageInfo
{
	DECLARE_CLASS( CMultiDamage, CTakeDamageInfo );
public:
	CMultiDamage();

	bool			IsClear( void ) { return (m_hTarget == NULL); }
	IHandleEntity		*GetTarget() const;
	void			SetTarget( IHandleEntity *pTarget );

	void			Init( IHandleEntity *pTarget, IHandleEntity *pInflictor, IHandleEntity *pAttacker, IHandleEntity *pWeapon, const Vector &damageForce, const Vector &damagePosition, const Vector &reportedPosition, float flDamage, int bitsDamageType, int iKillType );

protected:
	CHandle<IHandleEntity> m_hTarget;

	DECLARE_SIMPLE_DATADESC();
};

extern CMultiDamage g_MultiDamage;

// Multidamage accessors
void ClearMultiDamage( void );
void ApplyMultiDamage( void );
void AddMultiDamage( const CTakeDamageInfo &info, IHandleEntity *pEntity );

//-----------------------------------------------------------------------------
// Purpose: Utility functions for physics damage force calculation 
//-----------------------------------------------------------------------------
float ImpulseScale( float flTargetMass, float flDesiredSpeed );
void CalculateExplosiveDamageForce( CTakeDamageInfo *info, const Vector &vecDir, const Vector &vecForceOrigin, float flScale = 1.0 );
void CalculateBulletDamageForce( CTakeDamageInfo *info, int iBulletType, const Vector &vecBulletDir, const Vector &vecForceOrigin, float flScale = 1.0 );
void CalculateMeleeDamageForce( CTakeDamageInfo *info, const Vector &vecMeleeDir, const Vector &vecForceOrigin, float flScale = 1.0 );
void GuessDamageForce( CTakeDamageInfo *info, const Vector &vecForceDir, const Vector &vecForceOrigin, float flScale = 1.0 );


// -------------------------------------------------------------------------------------------------- //
// Inlines.
// -------------------------------------------------------------------------------------------------- //

inline IHandleEntity* CTakeDamageInfo::GetInflictor() const
{
	return m_hInflictor;
}


inline void CTakeDamageInfo::SetInflictor( IHandleEntity *pInflictor )
{
	m_hInflictor = pInflictor;
}


inline IHandleEntity* CTakeDamageInfo::GetAttacker() const
{
	return m_hAttacker;
}


inline void CTakeDamageInfo::SetAttacker( IHandleEntity *pAttacker )
{
	m_hAttacker = pAttacker;
}

inline IHandleEntity* CTakeDamageInfo::GetWeapon() const
{
	return m_hWeapon;
}


inline void CTakeDamageInfo::SetWeapon( IHandleEntity *pWeapon )
{
	m_hWeapon = pWeapon;
}


inline float CTakeDamageInfo::GetDamage() const
{
	return m_flDamage;
}

inline void CTakeDamageInfo::SetDamage( float flDamage )
{
	m_flDamage = flDamage;
}

inline float CTakeDamageInfo::GetMaxDamage() const
{
	return m_flMaxDamage;
}

inline void CTakeDamageInfo::SetMaxDamage( float flMaxDamage )
{
	m_flMaxDamage = flMaxDamage;
}

inline void CTakeDamageInfo::ScaleDamage( float flScaleAmount )
{
	m_flDamage *= flScaleAmount;
}

inline void CTakeDamageInfo::AddDamage( float flAddAmount )
{
	m_flDamage += flAddAmount;
}

inline void CTakeDamageInfo::SubtractDamage( float flSubtractAmount )
{
	m_flDamage -= flSubtractAmount;
}

inline float CTakeDamageInfo::GetDamageBonus() const
{
	return m_flDamageBonus;
}

inline void CTakeDamageInfo::SetDamageBonus( float flBonus )
{
	m_flDamageBonus = flBonus;
}

inline float CTakeDamageInfo::GetBaseDamage() const
{
	if( BaseDamageIsValid() )
		return m_flBaseDamage;

	// No one ever specified a base damage, so just return damage.
	return m_flDamage;
}

inline bool CTakeDamageInfo::BaseDamageIsValid() const
{
	return (m_flBaseDamage != BASEDAMAGE_NOT_SPECIFIED);
}

inline Vector CTakeDamageInfo::GetDamageForce() const
{
	return m_vecDamageForce;
}

inline void CTakeDamageInfo::SetDamageForce( const Vector &damageForce )
{
	m_vecDamageForce = damageForce;
}

inline void	CTakeDamageInfo::ScaleDamageForce( float flScaleAmount )
{
	m_vecDamageForce *= flScaleAmount;
}

inline Vector CTakeDamageInfo::GetDamagePosition() const
{
	return m_vecDamagePosition;
}


inline void CTakeDamageInfo::SetDamagePosition( const Vector &damagePosition )
{
	m_vecDamagePosition = damagePosition;
}

inline Vector CTakeDamageInfo::GetReportedPosition() const
{
	return m_vecReportedPosition;
}


inline void CTakeDamageInfo::SetReportedPosition( const Vector &reportedPosition )
{
	m_vecReportedPosition = reportedPosition;
}


inline void CTakeDamageInfo::SetDamageType( int bitsDamageType )
{
	m_bitsDamageType = bitsDamageType;
}

inline int CTakeDamageInfo::GetDamageType() const
{
	return m_bitsDamageType;
}

inline void	CTakeDamageInfo::AddDamageType( int bitsDamageType )
{
	m_bitsDamageType |= bitsDamageType;
}

inline int CTakeDamageInfo::GetDamageCustom() const
{
	return m_iDamageCustom;
}

inline void CTakeDamageInfo::SetDamageCustom( int iDamageCustom )
{
	m_iDamageCustom = iDamageCustom;
}

inline int CTakeDamageInfo::GetDamageStats() const
{
	return m_iDamageCustom;
}

inline void CTakeDamageInfo::SetDamageStats( int iDamageCustom )
{
	m_iDamageCustom = iDamageCustom;
}

inline int CTakeDamageInfo::GetAmmoType() const
{
	return m_iAmmoType;
}

inline void CTakeDamageInfo::SetAmmoType( int iAmmoType )
{
	m_iAmmoType = iAmmoType;
}

inline void CTakeDamageInfo::CopyDamageToBaseDamage()
{ 
	m_flBaseDamage = m_flDamage;
}


// -------------------------------------------------------------------------------------------------- //
// Inlines.
// -------------------------------------------------------------------------------------------------- //
inline IHandleEntity *CMultiDamage::GetTarget() const
{
	return m_hTarget;
}

inline void CMultiDamage::SetTarget( IHandleEntity *pTarget )
{
	m_hTarget = pTarget;
}


#endif // TAKEDAMAGEINFO_H
