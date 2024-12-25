//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: The Halflife Cycler NPCs
//
//=============================================================================//

#include "cbase.h"
#include "ai_basenpc.h"
#include "ai_motor.h"
#include "basecombatweapon.h"
#include "animation.h"
#include "vstdlib/random.h"
#include "h_cycler.h"
#include "Sprite.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define FCYCLER_NOTSOLID		0x0001

extern short		g_sModelIndexSmoke; // (in combatweapon.cpp) holds the index for the smoke cloud

BEGIN_DATADESC( CCycler )

	// Fields
	DEFINE_FIELD( m_animate, FIELD_INTEGER ),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_STRING, "SetSequence", InputSetSequence ),

END_DATADESC()

//
// we should get rid of all the other cyclers and replace them with this.
//
class CGenericCycler : public CCycler
{
public:
	DECLARE_CLASS( CGenericCycler, CCycler );

	void Spawn()
	{
		GenericCyclerSpawn( (char *)STRING(GetEngineObject()->GetModelName() ), Vector(-16, -16, 0), Vector(16, 16, 72) );
	}
};
LINK_ENTITY_TO_CLASS( cycler, CGenericCycler );
LINK_ENTITY_TO_CLASS( model_studio, CGenericCycler ); // For now model_studios build as cyclers.


// Cycler member functions

void CCycler::GenericCyclerSpawn(char *szModel, Vector vecMin, Vector vecMax)
{
	if (!szModel || !*szModel)
	{
		Warning( "cycler at %.0f %.0f %0.f missing modelname\n", GetEngineObject()->GetAbsOrigin().x, GetEngineObject()->GetAbsOrigin().y, GetEngineObject()->GetAbsOrigin().z );
		EntityList()->DestroyEntity( this );
		return;
	}

	Precache();

	SetModel( szModel );

	m_bloodColor = DONT_BLEED;

	CCycler::Spawn( );

	GetEngineObject()->SetSize(vecMin, vecMax);
}


void CCycler::Precache()
{
	engine->PrecacheModel( (const char *)STRING(GetEngineObject()->GetModelName() ) );
}


void CCycler::Spawn( )
{
	InitBoneControllers();
	
	GetEngineObject()->SetSolid( SOLID_BBOX );
	if (GetEngineObject()->GetSpawnFlags() & FCYCLER_NOTSOLID)
	{
		GetEngineObject()->AddSolidFlags( FSOLID_NOT_SOLID );
	}
	else
	{
		GetEngineObject()->AddSolidFlags( FSOLID_NOT_STANDABLE );
	}

	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	m_takedamage		= DAMAGE_YES;
	m_iHealth			= 80000;// no cycler should die
	GetMotor()->SetIdealYaw(GetEngineObject()->GetLocalAngles().y );
	GetMotor()->SnapYaw();
	
	GetEngineObject()->SetPlaybackRate(1.0);
	GetEngineObject()->SetGroundSpeed(0);

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 1.0f );

	GetEngineObject()->ResetSequenceInfo( );

	if (GetEngineObject()->GetSequence() != 0 || GetEngineObject()->GetCycle() != 0)
	{
#ifdef TF2_DLL
		m_animate = 1;
#else
		m_animate = 0;
		GetEngineObject()->SetPlaybackRate(0);
#endif
	}
	else
	{
		m_animate = 1;
	}
}




//
// cycler think
//
void CCycler::Think( void )
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

	if (m_animate)
	{
		StudioFrameAdvance ( );
		DispatchAnimEvents( this );
	}
	if (GetEngineObject()->IsSequenceFinished() && !GetEngineObject()->SequenceLoops())
	{
		// ResetSequenceInfo();
		// hack to avoid reloading model every frame
		GetEngineObject()->SetAnimTime(gpGlobals->curtime);
		GetEngineObject()->SetPlaybackRate(1.0);
		GetEngineObject()->SetSequenceFinished(false);
		GetEngineObject()->SetLastEventCheck(0);
		GetEngineObject()->SetCycle(0);
		if (!m_animate)
			GetEngineObject()->SetPlaybackRate(0.0);	// FIX: don't reset framerate
	}
}

//
// CyclerUse - starts a rotation trend
//
void CCycler::Use ( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	m_animate = !m_animate;
	if (m_animate)
		GetEngineObject()->SetPlaybackRate(1.0);
	else
		GetEngineObject()->SetPlaybackRate(0.0);
}

//-----------------------------------------------------------------------------
// Purpose: Changes sequences when hurt.
//-----------------------------------------------------------------------------
int CCycler::OnTakeDamage( const CTakeDamageInfo &info )
{
	if (m_animate)
	{
		int nSequence = GetEngineObject()->GetSequence() + 1;
		if ( !IsValidSequence(nSequence) )
		{
			nSequence = 0;
		}

		GetEngineObject()->ResetSequence( nSequence );
		GetEngineObject()->SetCycle(0);
	}
	else
	{
		GetEngineObject()->SetPlaybackRate(1.0);
		StudioFrameAdvance ();
		GetEngineObject()->SetPlaybackRate(0);
		Msg( "sequence: %d, frame %.0f\n", GetEngineObject()->GetSequence(), GetEngineObject()->GetCycle() );
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Input that sets the sequence of the cycler
//-----------------------------------------------------------------------------
void CCycler::InputSetSequence( inputdata_t &inputdata )
{
	if (m_animate)
	{
		// Legacy support: Try it as a number, and support '0'
		const char *sChar = inputdata.value.String();
		int iSeqNum = atoi( sChar );
		if ( !iSeqNum && sChar[0] != '0' )
		{
			// Treat it as a sequence name
			GetEngineObject()->ResetSequence(GetEngineObject()->LookupSequence( sChar ) );
		}
		else
		{
			GetEngineObject()->ResetSequence( iSeqNum );
		}

		if (GetEngineObject()->GetPlaybackRate() == 0.0)
		{
			GetEngineObject()->ResetSequence( 0 );
		}

		GetEngineObject()->SetCycle(0);
	}
}

// FIXME: this doesn't work anymore, and hasn't for a while now.

class CWeaponCycler : public CBaseCombatWeapon
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWeaponCycler, CBaseCombatWeapon );

	DECLARE_SERVERCLASS();

	void Spawn( void );

	void PrimaryAttack( void );
	void SecondaryAttack( void );
	bool Deploy( void );
	bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	string_t m_iszModel;
	int m_iModel;
};

IMPLEMENT_SERVERCLASS_ST(CWeaponCycler, DT_WeaponCycler)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( cycler_weapon, CWeaponCycler );

//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CWeaponCycler )

	DEFINE_FIELD( m_iszModel,	FIELD_STRING ),
	DEFINE_FIELD( m_iModel,		FIELD_INTEGER ),

END_DATADESC()

void CWeaponCycler::Spawn( )
{
	GetEngineObject()->SetSolid( SOLID_BBOX );
	GetEngineObject()->AddSolidFlags( FSOLID_NOT_STANDABLE );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );

	engine->PrecacheModel( STRING(GetEngineObject()->GetModelName() ) );
	SetModel( STRING(GetEngineObject()->GetModelName() ) );
	m_iszModel = GetEngineObject()->GetModelName();
	m_iModel = GetEngineObject()->GetModelIndex();

	GetEngineObject()->SetSize(Vector(-16, -16, 0), Vector(16, 16, 16));
	SetTouch( &CWeaponCycler::DefaultTouch );
}



bool CWeaponCycler::Deploy( )
{
	CBaseCombatCharacter *pOwner = GetOwner();

	if (pOwner)
	{
		pOwner->m_flNextAttack = gpGlobals->curtime + 1.0;
		SendWeaponAnim( 0 );
		m_iClip1 = 0;
		m_iClip2 = 0;
		return true;
	}
	return false;
}


bool CWeaponCycler::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	CBaseCombatCharacter *pOwner = GetOwner();
	if (pOwner)
	{
		pOwner->m_flNextAttack = gpGlobals->curtime + 0.5;
	}

	return true;
}


void CWeaponCycler::PrimaryAttack()
{
	SendWeaponAnim(GetEngineObject()->GetSequence() );
	m_flNextPrimaryAttack = gpGlobals->curtime + 0.3;
}


void CWeaponCycler::SecondaryAttack( void )
{
	float flFrameRate;

	int nSequence = (GetEngineObject()->GetSequence() + 1) % 8;

	// BUG:  Why do we set this here and then set to zero right after?
	GetEngineObject()->SetModelIndex( m_iModel );
	flFrameRate = 0.0;

	GetEngineObject()->SetModelIndex( 0 );

	if (flFrameRate == 0.0)
	{
		nSequence = 0;
	}

	GetEngineObject()->SetSequence( nSequence );
	SendWeaponAnim( nSequence );

	m_flNextSecondaryAttack = gpGlobals->curtime + 0.3;
}



// Flaming Wreakage
class CWreckage : public CAI_BaseNPC
{
public:
	DECLARE_CLASS( CWreckage, CAI_BaseNPC );

	DECLARE_DATADESC();

	void Spawn( void );
	void Precache( void );
	void Think( void );

	float m_flStartTime;
	float m_flDieTime;
};

BEGIN_DATADESC( CWreckage )

	DEFINE_FIELD( m_flStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_flDieTime, FIELD_TIME ),

END_DATADESC()


LINK_ENTITY_TO_CLASS( cycler_wreckage, CWreckage );

void CWreckage::Spawn( void )
{
	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	m_takedamage		= 0;

	GetEngineObject()->SetCycle( 0 );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

	if (GetEngineObject()->GetModelName() != NULL_STRING)
	{
		engine->PrecacheModel( STRING(GetEngineObject()->GetModelName() ) );
		SetModel( STRING(GetEngineObject()->GetModelName() ) );
	}

	m_flStartTime		= gpGlobals->curtime;
}

void CWreckage::Precache( )
{
	if (GetEngineObject()->GetModelName() != NULL_STRING )
		engine->PrecacheModel( STRING(GetEngineObject()->GetModelName() ) );
}

void CWreckage::Think( void )
{
	StudioFrameAdvance( );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.2 );

	if (m_flDieTime)
	{
		if (m_flDieTime < gpGlobals->curtime)
		{
			EntityList()->DestroyEntity( this );
			return;
		}
		else if (random->RandomFloat( 0, m_flDieTime - m_flStartTime ) > m_flDieTime - gpGlobals->curtime)
		{
			return;
		}
	}
	
	Vector vecSrc;
	GetEngineObject()->RandomPointInBounds( vec3_origin, Vector(1, 1, 1), &vecSrc );
	CPVSFilter filter( vecSrc );
	te->Smoke( filter, 0.0, 
		&vecSrc, g_sModelIndexSmoke,
		random->RandomFloat(0,4.9) + 5.0,
		random->RandomInt(0, 3) + 8 );
}

// BlendingCycler
// Used to demonstrate animation blending
class CBlendingCycler : public CCycler
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CBlendingCycler, CCycler );

	void Spawn( void );
	bool KeyValue( const char *szKeyName, const char *szValue );
	void Think( void );
	virtual int	ObjectCaps( void ) { return (BaseClass::ObjectCaps() | FCAP_DONT_SAVE | FCAP_IMPULSE_USE); }

	int m_iLowerBound;
	int m_iUpperBound;
	int m_iCurrent;
	int m_iBlendspeed;
	string_t m_iszSequence;
};
LINK_ENTITY_TO_CLASS( cycler_blender, CBlendingCycler );
 
//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CBlendingCycler )

	DEFINE_FIELD( m_iLowerBound,	FIELD_INTEGER ),
	DEFINE_FIELD( m_iUpperBound,	FIELD_INTEGER ),
	DEFINE_FIELD( m_iCurrent,		FIELD_INTEGER ),
	DEFINE_FIELD( m_iBlendspeed,	FIELD_INTEGER ),
	DEFINE_FIELD( m_iszSequence,	FIELD_STRING ),

END_DATADESC()

void CBlendingCycler::Spawn( void )
{
	// Remove if it's not blending
	if (m_iLowerBound == 0 && m_iUpperBound == 0)
	{
		EntityList()->DestroyEntity( this );
		return;
	}

	GenericCyclerSpawn( (char *)STRING(GetEngineObject()->GetModelName() ), Vector(-16,-16,-16), Vector(16,16,16));
	if (!m_iBlendspeed)
		m_iBlendspeed = 5;

	// Initialise Sequence
	if (m_iszSequence != NULL_STRING)
	{
		GetEngineObject()->SetSequence(GetEngineObject()->LookupSequence( STRING(m_iszSequence) ) );
	}

	m_iCurrent = m_iLowerBound;
}

bool CBlendingCycler::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "lowboundary"))
	{
		m_iLowerBound = atoi(szValue);
	}
	else if (FStrEq(szKeyName, "highboundary"))
	{
		m_iUpperBound = atoi(szValue);
	}
	else if (FStrEq(szKeyName, "blendspeed"))
	{
		m_iBlendspeed = atoi(szValue);
	}
	else if (FStrEq(szKeyName, "blendsequence"))
	{
		m_iszSequence = AllocPooledString(szValue);
	}
	else
		return BaseClass::KeyValue( szKeyName, szValue );

	return true;
}

// Blending Cycler think
void CBlendingCycler::Think( void )
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

	// Move
	m_iCurrent += m_iBlendspeed;
	if ( (m_iCurrent > m_iUpperBound) || (m_iCurrent < m_iLowerBound) )
		m_iBlendspeed = m_iBlendspeed * -1;

	// Set blend
	GetEngineObject()->SetPoseParameter( 0, m_iCurrent );

	Msg( "Current Blend: %d\n", m_iCurrent );

	if (GetEngineObject()->IsSequenceFinished() && !GetEngineObject()->SequenceLoops())
	{
		// ResetSequenceInfo();
		// hack to avoid reloading model every frame
		GetEngineObject()->SetAnimTime(gpGlobals->curtime);
		GetEngineObject()->SetPlaybackRate(1.0);
		GetEngineObject()->SetSequenceFinished(false);
		GetEngineObject()->SetLastEventCheck(0);
		GetEngineObject()->SetCycle(0);
		if (!m_animate)
		{
			GetEngineObject()->SetPlaybackRate(0.0);	// FIX: don't reset framerate
		}
	}
}


