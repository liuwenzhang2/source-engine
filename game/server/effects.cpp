//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements a grab bag of visual effects entities.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "effects.h"
#include "gib.h"
#include "beam_shared.h"
#include "decals.h"
#include "func_break.h"
#include "EntityFlame.h"
#include "basecombatweapon.h"
#include "model_types.h"
#include "player.h"
//#include "physics.h"
#include "baseparticleentity.h"
#include "ndebugoverlay.h"
#include "IEffects.h"
#include "vstdlib/random.h"
#include "env_wind_shared.h"
#include "filesystem.h"
#include "engine/IEngineSound.h"
#include "fire.h"
#include "te_effect_dispatch.h"
#include "Sprite.h"
#include "precipitation_shared.h"
#include "shot_manipulator.h"
#include "gameinterface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define SF_FUNNEL_REVERSE			1 // funnel effect repels particles instead of attracting them.

#define	SF_GIBSHOOTER_REPEATABLE	(1<<0)	// allows a gibshooter to be refired
#define	SF_SHOOTER_FLAMING			(1<<1)	// gib is on fire
#define SF_SHOOTER_STRICT_REMOVE	(1<<2)	// remove this gib even if it is in the player's view

// UNDONE: This should be client-side and not use TempEnts
class CBubbling : public CBaseEntity
{
public:
	DECLARE_CLASS( CBubbling, CBaseEntity );

	virtual  void	Spawn( void );
	virtual void	Precache( void );

	void	FizzThink( void );

	// Input handlers.
	void	InputActivate( inputdata_t &inputdata );
	void	InputDeactivate( inputdata_t &inputdata );
	void	InputToggle( inputdata_t &inputdata );

	void	InputSetCurrent( inputdata_t &inputdata );
	void	InputSetDensity( inputdata_t &inputdata );
	void	InputSetFrequency( inputdata_t &inputdata );

	DECLARE_DATADESC();

private:

	void TurnOn();
	void TurnOff();
	void Toggle();

	int		m_density;
	int		m_frequency;
	int		m_bubbleModel;
	int		m_state;
};

LINK_ENTITY_TO_CLASS( env_bubbles, CBubbling );

BEGIN_DATADESC( CBubbling )

	DEFINE_KEYFIELD( m_flSpeed, FIELD_FLOAT, "current" ),
	DEFINE_KEYFIELD( m_density, FIELD_INTEGER, "density" ),
	DEFINE_KEYFIELD( m_frequency, FIELD_INTEGER, "frequency" ),

	DEFINE_FIELD( m_state, FIELD_INTEGER ),
	// Let spawn restore this!
	//	DEFINE_FIELD( m_bubbleModel, FIELD_INTEGER ),

	// Function Pointers
	DEFINE_FUNCTION( FizzThink ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Activate", InputActivate ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Deactivate", InputDeactivate ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Toggle", InputToggle ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetCurrent", InputSetCurrent ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetDensity", InputSetDensity ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetFrequency", InputSetFrequency ),

END_DATADESC()



#define SF_BUBBLES_STARTOFF		0x0001

void CBubbling::Spawn( void )
{
	Precache( );
	SetModel( STRING(GetEngineObject()->GetModelName() ) );		// Set size

	// Make it invisible to client
	SetRenderColorA( 0 );

	GetEngineObject()->SetSolid( SOLID_NONE );						// Remove model & collisions

	if ( !GetEngineObject()->HasSpawnFlags(SF_BUBBLES_STARTOFF) )
	{
		SetThink( &CBubbling::FizzThink );
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 2.0 );
		m_state = 1;
	}
	else
	{
		m_state = 0;
	}
}

void CBubbling::Precache( void )
{
	m_bubbleModel = engine->PrecacheModel("sprites/bubble.vmt");			// Precache bubble sprite
}


void CBubbling::Toggle()
{
	if (!m_state)
	{
		TurnOn();
	}
	else
	{
		TurnOff();
	}
}


void CBubbling::TurnOn()
{
	m_state = 1;
	SetThink( &CBubbling::FizzThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
}

void CBubbling::TurnOff()
{
	m_state = 0;
	SetThink( NULL );
	GetEngineObject()->SetNextThink( TICK_NEVER_THINK );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBubbling::InputActivate( inputdata_t &inputdata )
{
	TurnOn();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBubbling::InputDeactivate( inputdata_t &inputdata )
{
	TurnOff();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBubbling::InputToggle( inputdata_t &inputdata )
{
	Toggle();
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : &inputdata -
//-----------------------------------------------------------------------------
void CBubbling::InputSetCurrent( inputdata_t &inputdata )
{
	m_flSpeed = (float)inputdata.value.Int();
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : &inputdata -
//-----------------------------------------------------------------------------
void CBubbling::InputSetDensity( inputdata_t &inputdata )
{
	m_density = inputdata.value.Int();
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : &inputdata -
//-----------------------------------------------------------------------------
void CBubbling::InputSetFrequency( inputdata_t &inputdata )
{
	m_frequency = inputdata.value.Int();

	// Reset think time
	if ( m_state )
	{
		if ( m_frequency > 19 )
		{
			GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.5f );
		}
		else
		{
			GetEngineObject()->SetNextThink( gpGlobals->curtime + 2.5 - (0.1 * m_frequency) );
		}
	}
}

void CBubbling::FizzThink( void )
{
	Vector center = WorldSpaceCenter();
	CPASFilter filter( center );
	te->Fizz( filter, 0.0, this, m_bubbleModel, m_density, (int)m_flSpeed );

	if ( m_frequency > 19 )
	{
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.5f );
	}
	else
	{
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 2.5 - (0.1 * m_frequency) );
	}
}


// ENV_TRACER
// Fakes a tracer
class CEnvTracer : public CPointEntity
{
public:
	DECLARE_CLASS( CEnvTracer, CPointEntity );

	void Spawn( void );
	void TracerThink( void );
	void Activate( void );

	DECLARE_DATADESC();

	Vector m_vecEnd;
	float  m_flDelay;
};

LINK_ENTITY_TO_CLASS( env_tracer, CEnvTracer );

BEGIN_DATADESC( CEnvTracer )

	DEFINE_KEYFIELD( m_flDelay, FIELD_FLOAT, "delay" ),

	DEFINE_FIELD( m_vecEnd, FIELD_POSITION_VECTOR ),

	// Function Pointers
	DEFINE_FUNCTION( TracerThink ),

END_DATADESC()



//-----------------------------------------------------------------------------
// Purpose: Called after keyvalues are parsed.
//-----------------------------------------------------------------------------
void CEnvTracer::Spawn( void )
{
	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );

	if (!m_flDelay)
		m_flDelay = 1;
}


//-----------------------------------------------------------------------------
// Purpose: Called after all the entities have been loaded.
//-----------------------------------------------------------------------------
void CEnvTracer::Activate( void )
{
	BaseClass::Activate();

	IServerEntity *pEnd = EntityList()->FindEntityByName( NULL, m_target );
	if (pEnd != NULL)
	{
		m_vecEnd = pEnd->GetEngineObject()->GetLocalOrigin();
		SetThink( &CEnvTracer::TracerThink );
		GetEngineObject()->SetNextThink( gpGlobals->curtime + m_flDelay );
	}
	else
	{
		Msg( "env_tracer: unknown entity \"%s\"\n", STRING(m_target) );
	}
}

// Think
void CEnvTracer::TracerThink( void )
{
	UTIL_Tracer(GetEngineObject()->GetAbsOrigin(), m_vecEnd );

	GetEngineObject()->SetNextThink( gpGlobals->curtime + m_flDelay );
}


//#################################################################################
//  >> CGibShooter
//#################################################################################
enum GibSimulation_t
{
	GIB_SIMULATE_POINT,
	GIB_SIMULATE_PHYSICS,
	GIB_SIMULATE_RAGDOLL,
};

class CGibShooter : public CBaseEntity
{
public:
	DECLARE_CLASS( CGibShooter, CBaseEntity );

	void Spawn( void );
	void Precache( void );
	void Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value );

	virtual CGib *CreateGib( void );

protected:
	// Purpose:
	CBaseEntity *SpawnGib( const Vector &vecShootDir, float flSpeed );

	DECLARE_DATADESC();
private:
	void InitPointGib( CGib *pGib, const Vector &vecShootDir, float flSpeed );
	void ShootThink( void );

protected:
	int		m_iGibs;
	int		m_iGibCapacity;
	int		m_iGibMaterial;
	int		m_iGibModelIndex;
	float	m_flGibVelocity;
	QAngle	m_angGibRotation;
	float	m_flGibAngVelocity;
	float	m_flVariance;
	float	m_flGibLife;
	int		m_nSimulationType;
	int		m_nMaxGibModelFrame;
	float	m_flDelay;

	bool	m_bNoGibShadows;

	bool	m_bIsSprite;
	string_t m_iszLightingOrigin;

	// ----------------
	//	Inputs
	// ----------------
	void InputShoot( inputdata_t &inputdata );
};

BEGIN_DATADESC( CGibShooter )

	DEFINE_KEYFIELD( m_iGibs, FIELD_INTEGER, "m_iGibs" ),
	DEFINE_KEYFIELD( m_flGibVelocity, FIELD_FLOAT, "m_flVelocity" ),
	DEFINE_KEYFIELD( m_flVariance, FIELD_FLOAT, "m_flVariance" ),
	DEFINE_KEYFIELD( m_flGibLife, FIELD_FLOAT, "m_flGibLife" ),
	DEFINE_KEYFIELD( m_nSimulationType, FIELD_INTEGER, "Simulation" ),
	DEFINE_KEYFIELD( m_flDelay, FIELD_FLOAT, "delay" ),
	DEFINE_KEYFIELD( m_angGibRotation, FIELD_VECTOR, "gibangles" ),
	DEFINE_KEYFIELD( m_flGibAngVelocity, FIELD_FLOAT, "gibanglevelocity"),
	DEFINE_FIELD( m_bIsSprite, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_iGibCapacity, FIELD_INTEGER ),
	DEFINE_FIELD( m_iGibMaterial, FIELD_INTEGER ),
	DEFINE_FIELD( m_iGibModelIndex, FIELD_INTEGER ),
	DEFINE_FIELD( m_nMaxGibModelFrame, FIELD_INTEGER ),

	DEFINE_KEYFIELD( m_iszLightingOrigin, FIELD_STRING, "LightingOrigin" ),
	DEFINE_KEYFIELD( m_bNoGibShadows, FIELD_BOOLEAN, "nogibshadows" ),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_VOID,	"Shoot", InputShoot ),

	// Function Pointers
	DEFINE_FUNCTION( ShootThink ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( gibshooter, CGibShooter );


void CGibShooter::Precache ( void )
{
	if ( g_Language.GetInt() == LANGUAGE_GERMAN )
	{
		m_iGibModelIndex = engine->PrecacheModel ("models/germanygibs.mdl");
	}
	else
	{
		m_iGibModelIndex = engine->PrecacheModel ("models/gibs/hgibs.mdl");
	}
}


void CGibShooter::Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value )
{
	SetThink( &CGibShooter::ShootThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime );
}


//-----------------------------------------------------------------------------
// Purpose: Input handler for shooting gibs.
//-----------------------------------------------------------------------------
void CGibShooter::InputShoot( inputdata_t &inputdata )
{
	SetThink( &CGibShooter::ShootThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime );
}


void CGibShooter::Spawn( void )
{
	Precache();

	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->AddEffects( EF_NODRAW );

	if ( m_flDelay < 0 )
	{
		m_flDelay = 0.0;
	}

	if ( m_flGibLife == 0 )
	{
		m_flGibLife = 25;
	}

	m_iGibCapacity = m_iGibs;

	m_nMaxGibModelFrame = modelinfo->GetModelFrameCount( modelinfo->GetModel( m_iGibModelIndex ) );
}

CGib *CGibShooter::CreateGib ( void )
{
	ConVarRef violence_hgibs( "violence_hgibs" );
	if ( violence_hgibs.IsValid() && !violence_hgibs.GetInt() )
		return NULL;

	CGib *pGib = (CGib*)EntityList()->CreateEntityByName( "gib" );
	pGib->Spawn( "models/gibs/hgibs.mdl" );
	pGib->SetBloodColor( BLOOD_COLOR_RED );

	if ( m_nMaxGibModelFrame <= 1 )
	{
		DevWarning( 2, "GibShooter Body is <= 1!\n" );
	}

	pGib->GetEngineObject()->SetBody(random->RandomInt ( 1, m_nMaxGibModelFrame - 1 ));// avoid throwing random amounts of the 0th gib. (skull).

	if ( m_iszLightingOrigin != NULL_STRING )
	{
		// Make the gibs use the lighting origin
		pGib->SetLightingOrigin( m_iszLightingOrigin );
	}

	return pGib;
}


void CGibShooter::InitPointGib( CGib *pGib, const Vector &vecShootDir, float flSpeed )
{
	if ( pGib )
	{
		pGib->GetEngineObject()->SetLocalOrigin(GetEngineObject()->GetAbsOrigin() );
		pGib->GetEngineObject()->SetAbsVelocity( vecShootDir * flSpeed );

		QAngle angVel( random->RandomFloat ( 100, 200 ), random->RandomFloat ( 100, 300 ), 0 );
		pGib->SetLocalAngularVelocity( angVel );

		float thinkTime = ( pGib->GetEngineObject()->GetNextThink() - gpGlobals->curtime );

		pGib->m_lifeTime = (m_flGibLife * random->RandomFloat( 0.95, 1.05 ));	// +/- 5%

		// HL1 gibs always die after a certain time, other games have to opt-in
#ifndef HL1_DLL
		if(GetEngineObject()->HasSpawnFlags( SF_SHOOTER_STRICT_REMOVE ) )
#endif
		{
			pGib->GetEngineObject()->SetNextThink( gpGlobals->curtime + pGib->m_lifeTime );
			pGib->SetThink ( &CGib::DieThink );
		}

		if ( pGib->m_lifeTime < thinkTime )
		{
			pGib->GetEngineObject()->SetNextThink( gpGlobals->curtime + pGib->m_lifeTime );
			pGib->m_lifeTime = 0;
		}

		if ( m_bIsSprite == true )
		{
			pGib->SetSprite( CSprite::SpriteCreate( STRING(GetEngineObject()->GetModelName() ), pGib->GetEngineObject()->GetAbsOrigin(), false ) );

			CSprite *pSprite = (CSprite*)pGib->GetSprite();

			if ( pSprite )
			{
				pSprite->SetAttachment( pGib, 0 );
				pSprite->SetOwnerEntity( pGib );

				pSprite->SetScale( 1 );
				pSprite->SetTransparency( m_nRenderMode, m_clrRender->r, m_clrRender->g, m_clrRender->b, m_clrRender->a, GetEngineObject()->GetRenderFX() );
				pSprite->AnimateForTime( 5, m_flGibLife + 1 ); //This framerate is totally wrong
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBaseEntity *CGibShooter::SpawnGib( const Vector &vecShootDir, float flSpeed )
{
	switch (m_nSimulationType)
	{
		case GIB_SIMULATE_RAGDOLL:
		{
			// UNDONE: Assume a mass of 200 for now
			Vector force = vecShootDir * flSpeed * 200;
			return CreateRagGib( STRING(GetEngineObject()->GetModelName() ), GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), force, m_flGibLife );
		}

		case GIB_SIMULATE_PHYSICS:
		{
			CGib *pGib = CreateGib();

			if ( pGib )
			{
				pGib->GetEngineObject()->SetAbsOrigin(GetEngineObject()->GetAbsOrigin() );
				pGib->GetEngineObject()->SetAbsAngles( m_angGibRotation );

				pGib->m_lifeTime = (m_flGibLife * random->RandomFloat( 0.95, 1.05 ));	// +/- 5%

				pGib->GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
				IPhysicsObject *pPhysicsObject = pGib->GetEngineObject()->VPhysicsInitNormal( SOLID_VPHYSICS, pGib->GetEngineObject()->GetSolidFlags(), false );
				pGib->GetEngineObject()->SetMoveType( MOVETYPE_VPHYSICS );

				if ( pPhysicsObject )
				{
					// Set gib velocity
					Vector vVel		= vecShootDir * flSpeed;
					pPhysicsObject->AddVelocity(&vVel, NULL);

					AngularImpulse torque;
					torque.x = m_flGibAngVelocity * random->RandomFloat( 0.1f, 1.0f );
					torque.y = m_flGibAngVelocity * random->RandomFloat( 0.1f, 1.0f );
					torque.z = 0.0f;
					torque *= pPhysicsObject->GetMass();

					pPhysicsObject->ApplyTorqueCenter( torque );

#ifndef HL1_DLL
					if(GetEngineObject()->HasSpawnFlags( SF_SHOOTER_STRICT_REMOVE ) )
#endif
					{
						pGib->m_bForceRemove = true;
						pGib->GetEngineObject()->SetNextThink( gpGlobals->curtime + pGib->m_lifeTime );
						pGib->SetThink ( &CGib::DieThink );
					}

				}
				else
				{
					InitPointGib( pGib, vecShootDir, flSpeed );
				}
			}
			return pGib;
		}

		case GIB_SIMULATE_POINT:
		{
			CGib *pGib = CreateGib();

			if ( pGib )
			{
				pGib->GetEngineObject()->SetAbsAngles( m_angGibRotation );

				InitPointGib( pGib, vecShootDir, flSpeed );
				return pGib;
			}
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CGibShooter::ShootThink ( void )
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime + m_flDelay );

	Vector vecShootDir, vForward,vRight,vUp;
	AngleVectors(GetEngineObject()->GetAbsAngles(), &vForward, &vRight, &vUp );
	vecShootDir = vForward;
	vecShootDir = vecShootDir + vRight * random->RandomFloat( -1, 1) * m_flVariance;
	vecShootDir = vecShootDir + vForward * random->RandomFloat( -1, 1) * m_flVariance;
	vecShootDir = vecShootDir + vUp * random->RandomFloat( -1, 1) * m_flVariance;

	VectorNormalize( vecShootDir );

	SpawnGib( vecShootDir, m_flGibVelocity );

	if ( --m_iGibs <= 0 )
	{
		if (GetEngineObject()->HasSpawnFlags(SF_GIBSHOOTER_REPEATABLE) )
		{
			m_iGibs = m_iGibCapacity;
			SetThink ( NULL );
			GetEngineObject()->SetNextThink( gpGlobals->curtime );
		}
		else
		{
			SetThink ( &CGibShooter::SUB_Remove );
			GetEngineObject()->SetNextThink( gpGlobals->curtime );
		}
	}
}

class CEnvShooter : public CGibShooter
{
public:
	DECLARE_CLASS( CEnvShooter, CGibShooter );

	CEnvShooter() { m_flGibGravityScale = 1.0f; }
	void		Precache( void );
	bool		KeyValue( const char *szKeyName, const char *szValue );

	CGib		*CreateGib( void );

	DECLARE_DATADESC();

public:

	int m_nSkin;
	float m_flGibScale;
	float m_flGibGravityScale;

#if HL2_EPISODIC
	float m_flMassOverride;	// allow designer to force a mass for gibs in some cases
#endif
};

BEGIN_DATADESC( CEnvShooter )

	DEFINE_KEYFIELD( m_nSkin, FIELD_INTEGER, "skin" ),
	DEFINE_KEYFIELD( m_flGibScale, FIELD_FLOAT ,"scale" ),
	DEFINE_KEYFIELD( m_flGibGravityScale, FIELD_FLOAT, "gibgravityscale" ),

#if HL2_EPISODIC
	DEFINE_KEYFIELD( m_flMassOverride, FIELD_FLOAT, "massoverride" ),
#endif

END_DATADESC()


LINK_ENTITY_TO_CLASS( env_shooter, CEnvShooter );

bool CEnvShooter::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "shootmodel"))
	{
		m_bIsSprite = false;
		GetEngineObject()->SetModelName( AllocPooledString(szValue) );

		//Adrian - not pretty...
		if ( Q_stristr( szValue, ".vmt" ) )
			 m_bIsSprite = true;
	}

	else if (FStrEq(szKeyName, "shootsounds"))
	{
		int iNoise = atoi(szValue);
		switch( iNoise )
		{
		case 0:
			m_iGibMaterial = matGlass;
			break;
		case 1:
			m_iGibMaterial = matWood;
			break;
		case 2:
			m_iGibMaterial = matMetal;
			break;
		case 3:
			m_iGibMaterial = matFlesh;
			break;
		case 4:
			m_iGibMaterial = matRocks;
			break;

		default:
		case -1:
			m_iGibMaterial = matNone;
			break;
		}
	}
	else
	{
		return BaseClass::KeyValue( szKeyName, szValue );
	}

	return true;
}


void CEnvShooter::Precache ( void )
{
	m_iGibModelIndex = engine->PrecacheModel( STRING(GetEngineObject()->GetModelName() ) );
}


CGib *CEnvShooter::CreateGib ( void )
{
	CGib *pGib = (CGib*)EntityList()->CreateEntityByName( "gib" );

	if ( m_bIsSprite == true )
	{
		//HACK HACK
		pGib->Spawn( "" );
	}
	else
	{
		pGib->Spawn( STRING(GetEngineObject()->GetModelName() ) );
	}

	int bodyPart = 0;

	if ( m_nMaxGibModelFrame > 1 )
	{
		bodyPart = random->RandomInt( 0, m_nMaxGibModelFrame-1 );
	}

	pGib->GetEngineObject()->SetBody(bodyPart);
	pGib->SetBloodColor( DONT_BLEED );
	pGib->m_material = m_iGibMaterial;

	pGib->m_nRenderMode = m_nRenderMode;
	pGib->m_clrRender = m_clrRender;
	pGib->GetEngineObject()->SetRenderFX(GetEngineObject()->GetRenderFX());
	pGib->GetEngineObject()->SetSkin(GetEngineObject()->GetSkin());
	pGib->m_lifeTime = gpGlobals->curtime + m_flGibLife;

	pGib->GetEngineObject()->SetGravity( m_flGibGravityScale );

	// Spawn a flaming gib
	if (GetEngineObject()->HasSpawnFlags( SF_SHOOTER_FLAMING ) )
	{
		// Tag an entity flame along with us
		CEntityFlame *pFlame = CEntityFlame::Create( pGib, false );
		if ( pFlame != NULL )
		{
			pFlame->SetLifetime( pGib->m_lifeTime );
			pGib->SetFlame( pFlame );
		}
	}

	if ( m_iszLightingOrigin != NULL_STRING )
	{
		// Make the gibs use the lighting origin
		pGib->SetLightingOrigin( m_iszLightingOrigin );
	}

	if( m_bNoGibShadows )
	{
		pGib->GetEngineObject()->AddEffects( EF_NOSHADOW );
	}

#if HL2_EPISODIC
	// if a mass override is set, apply it to the gib
	if (m_flMassOverride != 0)
	{
		IPhysicsObject *pPhys = pGib->GetEngineObject()->VPhysicsGetObject();
		if (pPhys)
		{
			pPhys->SetMass( m_flMassOverride );
		}
	}
#endif

	return pGib;
}


//-----------------------------------------------------------------------------
// An entity that shoots out junk when hit by a rotor wash
//-----------------------------------------------------------------------------
class CRotorWashShooter : public CEnvShooter, public IRotorWashShooter
{
public:
	DECLARE_CLASS( CRotorWashShooter, CEnvShooter );
	DECLARE_DATADESC();

	virtual void Spawn();

public:
	// Inherited from IRotorWashShooter
	virtual CBaseEntity *DoWashPush( float flTimeSincePushStarted, const Vector &vecForce );

private:
	// Amount of time we need to spend under the rotor before we shoot
	float m_flTimeUnderRotor;
	float m_flTimeUnderRotorVariance;

	// Last time we were hit with a wash...
	float m_flLastWashStartTime;
	float m_flNextGibTime;
};


LINK_ENTITY_TO_CLASS( env_rotorshooter, CRotorWashShooter );


//-----------------------------------------------------------------------------
// Save/load
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CRotorWashShooter )

	DEFINE_KEYFIELD( m_flTimeUnderRotor,	FIELD_FLOAT ,"rotortime" ),
	DEFINE_KEYFIELD( m_flTimeUnderRotorVariance,	FIELD_FLOAT ,"rotortimevariance" ),
	DEFINE_FIELD( m_flLastWashStartTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flNextGibTime, FIELD_TIME ),

END_DATADESC()



//-----------------------------------------------------------------------------
// Gets at the interface if the entity supports it
//-----------------------------------------------------------------------------
IRotorWashShooter *GetRotorWashShooter( CBaseEntity *pEntity )
{
	CRotorWashShooter *pShooter = dynamic_cast<CRotorWashShooter*>(pEntity);
	return pShooter;
}


//-----------------------------------------------------------------------------
// Inherited from IRotorWashShooter
//-----------------------------------------------------------------------------
void CRotorWashShooter::Spawn()
{
	BaseClass::Spawn();
	m_flLastWashStartTime = -1;
}



//-----------------------------------------------------------------------------
// Inherited from IRotorWashShooter
//-----------------------------------------------------------------------------
CBaseEntity *CRotorWashShooter::DoWashPush( float flWashStartTime, const Vector &vecForce )
{
	if ( flWashStartTime == m_flLastWashStartTime )
	{
		if ( m_flNextGibTime > gpGlobals->curtime )
			return NULL;
	}

	m_flLastWashStartTime = flWashStartTime;
	m_flNextGibTime	= gpGlobals->curtime + m_flTimeUnderRotor + random->RandomFloat( -1, 1) * m_flTimeUnderRotorVariance;
	if ( m_flNextGibTime <= gpGlobals->curtime )
	{
		m_flNextGibTime = gpGlobals->curtime + 0.01f;
	}

	// Set the velocity to be what the force would cause it to accelerate to
	// after one tick
	Vector vecShootDir = vecForce;
	VectorNormalize( vecShootDir );

	vecShootDir.x += random->RandomFloat( -1, 1 ) * m_flVariance;
	vecShootDir.y += random->RandomFloat( -1, 1 ) * m_flVariance;
	vecShootDir.z += random->RandomFloat( -1, 1 ) * m_flVariance;

	VectorNormalize( vecShootDir );

	CBaseEntity *pGib = SpawnGib( vecShootDir, m_flGibVelocity /*flLength*/ );

	if ( --m_iGibs <= 0 )
	{
		if (GetEngineObject()->HasSpawnFlags(SF_GIBSHOOTER_REPEATABLE) )
		{
			m_iGibs = m_iGibCapacity;
		}
		else
		{
			SetThink ( &CGibShooter::SUB_Remove );
			GetEngineObject()->SetNextThink( gpGlobals->curtime );
		}
	}

	return pGib;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CTestEffect : public CBaseEntity
{
public:
	DECLARE_CLASS( CTestEffect, CBaseEntity );

	void	Spawn( void );
	void	Precache( void );
	// bool	KeyValue( const char *szKeyName, const char *szValue );
	void Think( void );
	void Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value );

	int		m_iLoop;
	int		m_iBeam;
	CBeam	*m_pBeam[24];
	float	m_flBeamTime[24];
	float	m_flStartTime;
};


LINK_ENTITY_TO_CLASS( test_effect, CTestEffect );

void CTestEffect::Spawn( void )
{
	Precache( );
}

void CTestEffect::Precache( void )
{
	engine->PrecacheModel( "sprites/lgtning.vmt" );
}

void CTestEffect::Think( void )
{
	int i;
	float t = (gpGlobals->curtime - m_flStartTime);

	if (m_iBeam < 24)
	{
		CBeam *pbeam = CBeam::BeamCreate( "sprites/lgtning.vmt", 10 );

		trace_t	tr;

		Vector vecSrc = GetEngineObject()->GetAbsOrigin();
		Vector vecDir = Vector( random->RandomFloat( -1.0, 1.0 ), random->RandomFloat( -1.0, 1.0 ),random->RandomFloat( -1.0, 1.0 ) );
		VectorNormalize( vecDir );
		UTIL_TraceLine( vecSrc, vecSrc + vecDir * 128, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);

		pbeam->PointsInit( vecSrc, tr.endpos );
		// pbeam->SetColor( 80, 100, 255 );
		pbeam->SetColor( 255, 180, 100 );
		pbeam->SetWidth( 10.0 );
		pbeam->SetScrollRate( 12 );

		m_flBeamTime[m_iBeam] = gpGlobals->curtime;
		m_pBeam[m_iBeam] = pbeam;
		m_iBeam++;

#if 0
		Vector vecMid = (vecSrc + tr.endpos) * 0.5;
		CBroadcastRecipientFilter filter;
		TE_DynamicLight( filter, 0.0,
			vecMid, 255, 180, 100, 3, 2.0, 0.0 );
#endif
	}

	if (t < 3.0)
	{
		for (i = 0; i < m_iBeam; i++)
		{
			t = (gpGlobals->curtime - m_flBeamTime[i]) / ( 3 + m_flStartTime - m_flBeamTime[i]);
			m_pBeam[i]->SetBrightness( 255 * t );
			// m_pBeam[i]->SetScrollRate( 20 * t );
		}
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
	}
	else
	{
		for (i = 0; i < m_iBeam; i++)
		{
			EntityList()->DestroyEntity( m_pBeam[i] );
		}
		m_flStartTime = gpGlobals->curtime;
		m_iBeam = 0;
		GetEngineObject()->SetNextThink( TICK_NEVER_THINK );
	}
}


void CTestEffect::Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value )
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
	m_flStartTime = gpGlobals->curtime;
}



// Blood effects
class CBlood : public CPointEntity
{
public:
	DECLARE_CLASS( CBlood, CPointEntity );

	void	Spawn( void );
	bool	KeyValue( const char *szKeyName, const char *szValue );

	inline	int		Color( void ) { return m_Color; }
	inline	float 	BloodAmount( void ) { return m_flAmount; }

	inline	void SetColor( int color ) { m_Color = color; }

	// Input handlers
	void InputEmitBlood( inputdata_t &inputdata );

	Vector	Direction( void );
	Vector	BloodPosition( IServerEntity *pActivator );

	DECLARE_DATADESC();

	Vector m_vecSprayDir;
	float m_flAmount;
	int m_Color;

private:
};

LINK_ENTITY_TO_CLASS( env_blood, CBlood );

BEGIN_DATADESC( CBlood )

	DEFINE_KEYFIELD( m_vecSprayDir, FIELD_VECTOR, "spraydir" ),
	DEFINE_KEYFIELD( m_flAmount, FIELD_FLOAT, "amount" ),
	DEFINE_FIELD( m_Color, FIELD_INTEGER ),

	DEFINE_INPUTFUNC( FIELD_VOID, "EmitBlood", InputEmitBlood ),

END_DATADESC()


#define SF_BLOOD_RANDOM		0x0001
#define SF_BLOOD_STREAM		0x0002
#define SF_BLOOD_PLAYER		0x0004
#define SF_BLOOD_DECAL		0x0008
#define SF_BLOOD_CLOUD		0x0010
#define SF_BLOOD_DROPS		0x0020
#define SF_BLOOD_GORE		0x0040


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBlood::Spawn( void )
{
	// Convert spraydir from angles to a vector
	QAngle angSprayDir = QAngle( m_vecSprayDir.x, m_vecSprayDir.y, m_vecSprayDir.z );
	AngleVectors( angSprayDir, &m_vecSprayDir );

	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	SetColor( BLOOD_COLOR_RED );
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : szKeyName -
//			szValue -
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBlood::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "color"))
	{
		int color = atoi(szValue);
		switch ( color )
		{
			case 1:
			{
				SetColor( BLOOD_COLOR_YELLOW );
				break;
			}
		}
	}
	else
	{
		return BaseClass::KeyValue( szKeyName, szValue );
	}

	return true;
}


Vector CBlood::Direction( void )
{
	if (GetEngineObject()->HasSpawnFlags( SF_BLOOD_RANDOM ) )
		return UTIL_RandomBloodVector();

	return m_vecSprayDir;
}


Vector CBlood::BloodPosition( IServerEntity *pActivator )
{
	if (GetEngineObject()->HasSpawnFlags( SF_BLOOD_PLAYER ) )
	{
		CBasePlayer *player;

		if ( pActivator && pActivator->IsPlayer() )
		{
			player = ToBasePlayer( pActivator );
		}
		else
		{
			player = ToBasePlayer(EntityList()->GetLocalPlayer());
		}

		if ( player )
		{
			return (player->EyePosition()) + Vector( random->RandomFloat(-10,10), random->RandomFloat(-10,10), random->RandomFloat(-10,10) );
		}
	}

	return GetEngineObject()->GetLocalOrigin();
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void UTIL_BloodSpray( const Vector &pos, const Vector &dir, int color, int amount, int flags )
{
	if( color == DONT_BLEED )
		return;

	CEffectData	data;

	data.m_vOrigin = pos;
	data.m_vNormal = dir;
	data.m_flScale = (float)amount;
	data.m_fFlags = flags;
	data.m_nColor = color;

	DispatchEffect( "bloodspray", data );
}

//-----------------------------------------------------------------------------
// Purpose: Input handler for triggering the blood effect.
//-----------------------------------------------------------------------------
void CBlood::InputEmitBlood( inputdata_t &inputdata )
{
	if (GetEngineObject()->HasSpawnFlags( SF_BLOOD_STREAM ) )
	{
		UTIL_BloodStream( BloodPosition( inputdata.pActivator), Direction(), Color(), BloodAmount() );
	}
	else
	{
		UTIL_BloodDrips( BloodPosition( inputdata.pActivator), Direction(), Color(), BloodAmount() );
	}

	if (GetEngineObject()->HasSpawnFlags( SF_BLOOD_DECAL ) )
	{
		Vector forward = Direction();
		Vector start = BloodPosition( inputdata.pActivator );
		trace_t tr;

		UTIL_TraceLine( start, start + forward * BloodAmount() * 2, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction != 1.0 )
		{
			UTIL_BloodDecalTrace( &tr, Color() );
		}
	}

	//
	// New-fangled blood effects.
	//
	if (GetEngineObject()->HasSpawnFlags( SF_BLOOD_CLOUD | SF_BLOOD_DROPS | SF_BLOOD_GORE ) )
	{
		int nFlags = 0;
		if (GetEngineObject()->HasSpawnFlags(SF_BLOOD_CLOUD))
		{
			nFlags |= FX_BLOODSPRAY_CLOUD;
		}

		if (GetEngineObject()->HasSpawnFlags(SF_BLOOD_DROPS))
		{
			nFlags |= FX_BLOODSPRAY_DROPS;
		}

		if (GetEngineObject()->HasSpawnFlags(SF_BLOOD_GORE))
		{
			nFlags |= FX_BLOODSPRAY_GORE;
		}

		UTIL_BloodSpray(GetEngineObject()->GetAbsOrigin(), Direction(), Color(), BloodAmount(), nFlags);
	}
}


//-----------------------------------------------------------------------------
// Purpose: Console command for emitting the blood spray effect from an NPC.
//-----------------------------------------------------------------------------
void CC_BloodSpray( const CCommand &args )
{
	IServerEntity *pEnt = NULL;
	while ( ( pEnt = EntityList()->FindEntityGeneric( pEnt, args[1] ) ) != NULL )
	{
		Vector forward;
		pEnt->GetVectors(&forward, NULL, NULL);
		UTIL_BloodSpray( (forward * 4 ) + ( pEnt->EyePosition() + pEnt->WorldSpaceCenter() ) * 0.5f, forward, BLOOD_COLOR_RED, 4, FX_BLOODSPRAY_ALL );
	}
}

static ConCommand bloodspray( "bloodspray", CC_BloodSpray, "blood", FCVAR_CHEAT );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CEnvFunnel : public CBaseEntity
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CEnvFunnel, CBaseEntity );

	void	Spawn( void );
	void	Precache( void );
	void	Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value );

	int		m_iSprite;	// Don't save, precache
};

LINK_ENTITY_TO_CLASS( env_funnel, CEnvFunnel );

//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CEnvFunnel )

//	DEFINE_FIELD( m_iSprite,	FIELD_INTEGER ),

END_DATADESC()



void CEnvFunnel::Precache ( void )
{
	m_iSprite = engine->PrecacheModel ( "sprites/flare6.vmt" );
}

void CEnvFunnel::Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value )
{
	CBroadcastRecipientFilter filter;
	te->LargeFunnel( filter, 0.0,
		&GetEngineObject()->GetAbsOrigin(), m_iSprite, GetEngineObject()->HasSpawnFlags( SF_FUNNEL_REVERSE ) ? 1 : 0 );

	SetThink( &CEnvFunnel::SUB_Remove );
	GetEngineObject()->SetNextThink( gpGlobals->curtime );
}

void CEnvFunnel::Spawn( void )
{
	Precache();
	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->AddEffects( EF_NODRAW );
}

//=========================================================
// Beverage Dispenser
// overloaded m_iHealth, is now how many cans remain in the machine.
//=========================================================
class CEnvBeverage : public CBaseEntity
{
public:
	DECLARE_CLASS( CEnvBeverage, CBaseEntity );

	void	Spawn( void );
	void	Precache( void );
	void	Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value );
	bool	KeyValue( const char *szKeyName, const char *szValue );

	// Input handlers.
	void	InputActivate( inputdata_t &inputdata );

	DECLARE_DATADESC();

public:
	bool	m_CanInDispenser;
	int		m_nBeverageType;
};

void CEnvBeverage::Precache ( void )
{
	engine->PrecacheModel( "models/can.mdl" );
}

BEGIN_DATADESC( CEnvBeverage )
	DEFINE_FIELD( m_CanInDispenser, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_nBeverageType, FIELD_INTEGER ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Activate", InputActivate ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( env_beverage, CEnvBeverage );


bool CEnvBeverage::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "beveragetype"))
	{
		m_nBeverageType = atoi(szValue);
	}
	else
	{
		return BaseClass::KeyValue( szKeyName, szValue );
	}

	return true;
}

void CEnvBeverage::Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value )
{
	if ( m_CanInDispenser || m_iHealth <= 0 )
	{
		// no more cans while one is waiting in the dispenser, or if I'm out of cans.
		return;
	}

	CBaseAnimating *pCan = (CBaseAnimating *)CBaseEntity::Create( "item_sodacan", GetEngineObject()->GetLocalOrigin(), GetEngineObject()->GetLocalAngles(), this );

	if ( m_nBeverageType == 6 )
	{
		// random
		pCan->GetEngineObject()->SetSkin(random->RandomInt( 0, 5 ));
	}
	else
	{
		pCan->GetEngineObject()->SetSkin(m_nBeverageType);
	}

	m_CanInDispenser = true;
	m_iHealth -= 1;

	//SetThink (SUB_Remove);
	//SetNextThink( gpGlobals->curtime );
}

void CEnvBeverage::InputActivate( inputdata_t &inputdata )
{
	Use( inputdata.pActivator, inputdata.pCaller, USE_ON, 0 );
}

void CEnvBeverage::Spawn( void )
{
	Precache();
	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->AddEffects( EF_NODRAW );
	m_CanInDispenser = false;

	if ( m_iHealth == 0 )
	{
		m_iHealth = 10;
	}
}

//=========================================================
// Soda can
//=========================================================
class CItemSoda : public CBaseAnimating
{
public:
	DECLARE_CLASS( CItemSoda, CBaseAnimating );

	void	Spawn( void );
	void	Precache( void );
	void	CanThink ( void );
	void	CanTouch ( IServerEntity *pOther );

	DECLARE_DATADESC();
};


BEGIN_DATADESC( CItemSoda )

	// Function Pointers
	DEFINE_FUNCTION( CanThink ),
	DEFINE_FUNCTION( CanTouch ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( item_sodacan, CItemSoda );

void CItemSoda::Precache ( void )
{
	engine->PrecacheModel( "models/can.mdl" );

	g_pSoundEmitterSystem->PrecacheScriptSound( "ItemSoda.Bounce" );
}

void CItemSoda::Spawn( void )
{
	Precache();
	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->SetMoveType( MOVETYPE_FLYGRAVITY );

	SetModel ( "models/can.mdl" );
	GetEngineObject()->SetSize ( Vector ( 0, 0, 0 ), Vector ( 0, 0, 0 ) );

	SetThink (&CItemSoda::CanThink);
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.5f );
}

void CItemSoda::CanThink ( void )
{
	const char* soundname = "ItemSoda.Bounce";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);

	GetEngineObject()->SetSolid( SOLID_BBOX );
	GetEngineObject()->AddSolidFlags( FSOLID_TRIGGER );

#ifdef HL1_DLL
	GetEngineObject()->SetSize(Vector(-16, -16, 0), Vector(16, 16, 16));
#else
	GetEngineObject()->SetSize ( Vector ( -8, -8, 0 ), Vector ( 8, 8, 8 ) );
#endif

	SetThink ( NULL );
	SetTouch ( &CItemSoda::CanTouch );
}

void CItemSoda::CanTouch ( IServerEntity *pOther )
{
	if ( !pOther->IsPlayer() )
	{
		return;
	}

	// spoit sound here

	pOther->TakeHealth( 1, DMG_GENERIC );// a bit of health.

	if ( GetOwnerEntity() )
	{
		// tell the machine the can was taken
		CEnvBeverage *bev = (CEnvBeverage *)GetOwnerEntity();
		bev->m_CanInDispenser = false;
	}

	GetEngineObject()->AddSolidFlags( FSOLID_NOT_SOLID );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	GetEngineObject()->AddEffects( EF_NODRAW );
	SetTouch ( NULL );
	SetThink ( &CItemSoda::SUB_Remove );
	GetEngineObject()->SetNextThink( gpGlobals->curtime );
}

#ifndef _XBOX
//=========================================================
// func_precipitation - temporary snow solution for first HL2
// technology demo
//=========================================================

class CPrecipitation : public CBaseEntity
{
public:
	DECLARE_CLASS( CPrecipitation, CBaseEntity );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	CPrecipitation();
	void	Spawn( void );

	CNetworkVar( PrecipitationType_t, m_nPrecipType );
};

LINK_ENTITY_TO_CLASS( func_precipitation, CPrecipitation );

BEGIN_DATADESC( CPrecipitation )
	DEFINE_KEYFIELD( m_nPrecipType, FIELD_INTEGER, "preciptype" ),
END_DATADESC()

// Just send the normal entity crap
IMPLEMENT_SERVERCLASS_ST( CPrecipitation, DT_Precipitation)
	SendPropInt( SENDINFO( m_nPrecipType ), Q_log2( NUM_PRECIPITATION_TYPES ) + 1, SPROP_UNSIGNED )
END_SEND_TABLE()


CPrecipitation::CPrecipitation()
{
	m_nPrecipType = PRECIPITATION_TYPE_RAIN; // default to rain.
}

void CPrecipitation::Spawn( void )
{
	g_ServerGameDLL.PrecacheMaterial( "effects/fleck_ash1" );
	g_ServerGameDLL.PrecacheMaterial( "effects/fleck_ash2" );
	g_ServerGameDLL.PrecacheMaterial( "effects/fleck_ash3" );
	g_ServerGameDLL.PrecacheMaterial( "effects/ember_swirling001" );

	Precache();
	GetEngineObject()->SetSolid( SOLID_NONE );							// Remove model & collisions
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	SetModel( STRING(GetEngineObject()->GetModelName() ) );		// Set size

	// Default to rain.
	if ( m_nPrecipType < 0 || m_nPrecipType > NUM_PRECIPITATION_TYPES )
		m_nPrecipType = PRECIPITATION_TYPE_RAIN;

	m_nRenderMode = kRenderEnvironmental;
}
#endif

//-----------------------------------------------------------------------------
// EnvWind - global wind info
//-----------------------------------------------------------------------------
class CEnvWind : public CBaseEntity
{
public:
	DECLARE_CLASS( CEnvWind, CBaseEntity );

	void	Spawn( void );
	void	Precache( void );
	void	WindThink( void );
	int		UpdateTransmitState( void );

	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

private:
//#ifdef POSIX
	//CEnvWindShared m_EnvWindShared; // FIXME - fails to compile as networked var due to operator= problem
//#else
	CNetworkVarEmbedded( CEnvWindShared, m_EnvWindShared );
//#endif
};

LINK_ENTITY_TO_CLASS( env_wind, CEnvWind );

BEGIN_DATADESC( CEnvWind )

	DEFINE_KEYFIELD( m_EnvWindShared.m_iMinWind, FIELD_INTEGER, "minwind" ),
	DEFINE_KEYFIELD( m_EnvWindShared.m_iMaxWind, FIELD_INTEGER, "maxwind" ),
	DEFINE_KEYFIELD( m_EnvWindShared.m_iMinGust, FIELD_INTEGER, "mingust" ),
	DEFINE_KEYFIELD( m_EnvWindShared.m_iMaxGust, FIELD_INTEGER, "maxgust" ),
	DEFINE_KEYFIELD( m_EnvWindShared.m_flMinGustDelay, FIELD_FLOAT, "mingustdelay" ),
	DEFINE_KEYFIELD( m_EnvWindShared.m_flMaxGustDelay, FIELD_FLOAT, "maxgustdelay" ),
	DEFINE_KEYFIELD( m_EnvWindShared.m_iGustDirChange, FIELD_INTEGER, "gustdirchange" ),
	DEFINE_KEYFIELD( m_EnvWindShared.m_flGustDuration, FIELD_FLOAT, "gustduration" ),
//	DEFINE_KEYFIELD( m_EnvWindShared.m_iszGustSound, FIELD_STRING, "gustsound" ),

// Just here to quiet down classcheck
	// DEFINE_FIELD( m_EnvWindShared, CEnvWindShared ),

	DEFINE_FIELD( m_EnvWindShared.m_iWindDir, FIELD_INTEGER ),
	DEFINE_FIELD( m_EnvWindShared.m_flWindSpeed, FIELD_FLOAT ),

	DEFINE_OUTPUT( m_EnvWindShared.m_OnGustStart, "OnGustStart" ),
	DEFINE_OUTPUT( m_EnvWindShared.m_OnGustEnd,	"OnGustEnd" ),

	// Function Pointers
	DEFINE_FUNCTION( WindThink ),

END_DATADESC()


BEGIN_SEND_TABLE_NOBASE(CEnvWindShared, DT_EnvWindShared)
	// These are parameters that are used to generate the entire motion
	SendPropInt		(SENDINFO(m_iMinWind),		10, SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_iMaxWind),		10, SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_iMinGust),		10, SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_iMaxGust),		10, SPROP_UNSIGNED ),
	SendPropFloat	(SENDINFO(m_flMinGustDelay), 0, SPROP_NOSCALE),		// NOTE: Have to do this, so it's *exactly* the same on client
	SendPropFloat	(SENDINFO(m_flMaxGustDelay), 0, SPROP_NOSCALE),
	SendPropInt		(SENDINFO(m_iGustDirChange), 9, SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_iWindSeed),		32, SPROP_UNSIGNED ),

	// These are related to initial state
	SendPropInt		(SENDINFO(m_iInitialWindDir),9, SPROP_UNSIGNED ),
	SendPropFloat	(SENDINFO(m_flInitialWindSpeed),0, SPROP_NOSCALE ),
	SendPropFloat	(SENDINFO(m_flStartTime),	 0, SPROP_NOSCALE ),

	SendPropFloat	(SENDINFO(m_flGustDuration), 0, SPROP_NOSCALE),
	// Sound related
//	SendPropInt		(SENDINFO(m_iszGustSound),	10, SPROP_UNSIGNED ),
END_SEND_TABLE()

// This table encodes the CBaseEntity data.
IMPLEMENT_SERVERCLASS_ST_NOBASE(CEnvWind, DT_EnvWind)
	SendPropDataTable(SENDINFO_DT(m_EnvWindShared), &REFERENCE_SEND_TABLE(DT_EnvWindShared)),
END_SEND_TABLE()

void CEnvWind::Precache ( void )
{
//	if (m_iszGustSound)
//	{
//		PrecacheScriptSound( STRING( m_iszGustSound ) );
//	}
}

void CEnvWind::Spawn( void )
{
	Precache();
	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->AddEffects( EF_NODRAW );

	m_EnvWindShared.Init( entindex(), 0, gpGlobals->frametime, GetEngineObject()->GetLocalAngles().y, 0 );

	SetThink( &CEnvWind::WindThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime );
}

int CEnvWind::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

void CEnvWind::WindThink( void )
{
	GetEngineObject()->SetNextThink( m_EnvWindShared.WindThink( gpGlobals->curtime ) );
}



//==================================================
// CEmbers
//==================================================

#define	bitsSF_EMBERS_START_ON	0x00000001
#define	bitsSF_EMBERS_TOGGLE	0x00000002

// UNDONE: This is a brush effect-in-volume entity, move client side.
class CEmbers : public CBaseEntity
{
public:
	DECLARE_CLASS( CEmbers, CBaseEntity );

	void	Spawn( void );
	void	Precache( void );

	void	EmberUse( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value );

	CNetworkVar( int, m_nDensity );
	CNetworkVar( int, m_nLifetime );
	CNetworkVar( int, m_nSpeed );

	CNetworkVar( bool, m_bEmit );

	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();
};

LINK_ENTITY_TO_CLASS( env_embers, CEmbers );

//Data description
BEGIN_DATADESC( CEmbers )

	DEFINE_KEYFIELD( m_nDensity,	FIELD_INTEGER, "density" ),
	DEFINE_KEYFIELD( m_nLifetime,	FIELD_INTEGER, "lifetime" ),
	DEFINE_KEYFIELD( m_nSpeed,		FIELD_INTEGER, "speed" ),

	DEFINE_FIELD( m_bEmit,	FIELD_BOOLEAN ),

	//Function pointers
	DEFINE_FUNCTION( EmberUse ),

END_DATADESC()


//Data table
IMPLEMENT_SERVERCLASS_ST( CEmbers, DT_Embers )
	SendPropInt(	SENDINFO( m_nDensity ),		32,	SPROP_UNSIGNED ),
	SendPropInt(	SENDINFO( m_nLifetime ),	32,	SPROP_UNSIGNED ),
	SendPropInt(	SENDINFO( m_nSpeed ),		32,	SPROP_UNSIGNED ),
	SendPropInt(	SENDINFO( m_bEmit ),		2,	SPROP_UNSIGNED ),
END_SEND_TABLE()

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEmbers::Spawn( void )
{
	Precache();
	SetModel( STRING(GetEngineObject()->GetModelName() ) );

	GetEngineObject()->SetSolid( SOLID_NONE );
	SetRenderColorA( 0 );
	m_nRenderMode	= kRenderTransTexture;

	SetUse( &CEmbers::EmberUse );

	//Start off if we're targetted (unless flagged)
	m_bEmit = (GetEngineObject()->HasSpawnFlags( bitsSF_EMBERS_START_ON ) || ( !GetEntityName() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEmbers::Precache( void )
{
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pActivator -
//			*pCaller -
//			useType -
//			value -
//-----------------------------------------------------------------------------
void CEmbers::EmberUse( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value )
{
	//If we're not toggable, only allow one use
	if ( !GetEngineObject()->HasSpawnFlags( bitsSF_EMBERS_TOGGLE ) )
	{
		SetUse( NULL );
	}

	//Handle it
	switch ( useType )
	{
	case USE_OFF:
		m_bEmit = false;
		break;

	case USE_ON:
		m_bEmit = true;
		break;

	case USE_SET:
		m_bEmit = !!(int)value;
		break;

	default:
	case USE_TOGGLE:
		m_bEmit = !m_bEmit;
		break;
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CPhysicsWire : public CBaseEntity
{
public:
	DECLARE_CLASS( CPhysicsWire, CBaseEntity );

	void	Spawn( void );
	void	Precache( void );

	DECLARE_DATADESC();

protected:

	bool SetupPhysics( void );

	int		m_nDensity;
};

LINK_ENTITY_TO_CLASS( env_physwire, CPhysicsWire );

BEGIN_DATADESC( CPhysicsWire )

	DEFINE_KEYFIELD( m_nDensity,	FIELD_INTEGER, "Density" ),
//	DEFINE_KEYFIELD( m_frequency, FIELD_INTEGER, "frequency" ),

//	DEFINE_FIELD( m_flFoo, FIELD_FLOAT ),

	// Function Pointers
//	DEFINE_FUNCTION( WireThink ),

END_DATADESC()


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPhysicsWire::Spawn( void )
{
	BaseClass::Spawn();

	Precache();

//	if ( SetupPhysics() == false )
//		return;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPhysicsWire::Precache( void )
{
	BaseClass::Precache();
}

class CPhysBallSocket;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPhysicsWire::SetupPhysics( void )
{
/*
	CPointEntity	*anchorEnt, *freeEnt;
	CPhysBallSocket *socket;

	char	anchorName[256];
	char	freeName[256];

	int		iAnchorName, iFreeName;

	anchorEnt = (CPointEntity *) CreateEntityByName( "info_target" );

	if ( anchorEnt == NULL )
		return false;

	//Create and connect all segments
	for ( int i = 0; i < m_nDensity; i++ )
	{
		// Create other end of our link
		freeEnt = (CPointEntity *) CreateEntityByName( "info_target" );

		// Create a ballsocket and attach the two
		//socket = (CPhysBallSocket *) CreateEntityByName( "phys_ballsocket" );

		Q_snprintf( anchorName,sizeof(anchorName), "__PWIREANCHOR%d", i );
		Q_snprintf( freeName,sizeof(freeName), "__PWIREFREE%d", i+1 );

		iAnchorName = MAKE_STRING( anchorName );
		iFreeName	= MAKE_STRING( freeName );

		//Fake the names
		//socket->m_nameAttach1 = anchorEnt->m_iGlobalname	= iAnchorName;
		//socket->m_nameAttach2 = freeEnt->m_iGlobalname	= iFreeName

		//socket->Activate();

		//The free ent is now the anchor for the next link
		anchorEnt = freeEnt;
	}
*/

	return true;
}

//
// Muzzle flash
//

class CEnvMuzzleFlash : public CPointEntity
{
	DECLARE_CLASS( CEnvMuzzleFlash, CPointEntity );

public:
	virtual void Spawn();

	// Input handlers
	void	InputFire( inputdata_t &inputdata );

	DECLARE_DATADESC();

	float	m_flScale;
	string_t m_iszParentAttachment;
};

BEGIN_DATADESC( CEnvMuzzleFlash )

	DEFINE_KEYFIELD( m_flScale, FIELD_FLOAT, "scale" ),
	DEFINE_KEYFIELD( m_iszParentAttachment, FIELD_STRING, "parentattachment" ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Fire", InputFire ),

END_DATADESC()


LINK_ENTITY_TO_CLASS( env_muzzleflash, CEnvMuzzleFlash );


//-----------------------------------------------------------------------------
// Spawn!
//-----------------------------------------------------------------------------
void CEnvMuzzleFlash::Spawn()
{
	if ( (m_iszParentAttachment != NULL_STRING) && GetEngineObject()->GetMoveParent() && GetEngineObject()->GetMoveParent()->GetModelPtr())
	{
		IEngineObjectServer *pAnim = GetEngineObject()->GetMoveParent();
		int nParentAttachment = pAnim->LookupAttachment( STRING(m_iszParentAttachment) );
		if ( nParentAttachment > 0 )
		{
			GetEngineObject()->SetParent(GetEngineObject()->GetMoveParent(), nParentAttachment);
			GetEngineObject()->SetLocalOrigin( vec3_origin );
			GetEngineObject()->SetLocalAngles( vec3_angle );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : &inputdata -
//-----------------------------------------------------------------------------
void CEnvMuzzleFlash::InputFire( inputdata_t &inputdata )
{
	g_pEffects->MuzzleFlash(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), m_flScale, MUZZLEFLASH_TYPE_DEFAULT );
}


//=========================================================
// Splash!
//=========================================================
#define SF_ENVSPLASH_FINDWATERSURFACE	0x00000001
#define SF_ENVSPLASH_DIMINISH			0x00000002
class CEnvSplash : public CPointEntity
{
	DECLARE_CLASS( CEnvSplash, CPointEntity );

public:
	// Input handlers
	void	InputSplash( inputdata_t &inputdata );

protected:

	float	m_flScale;

	DECLARE_DATADESC();
};

BEGIN_DATADESC( CEnvSplash )
	DEFINE_KEYFIELD( m_flScale, FIELD_FLOAT, "scale" ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Splash", InputSplash ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( env_splash, CEnvSplash );

//-----------------------------------------------------------------------------
// Purpose:
// Input  : &inputdata -
//-----------------------------------------------------------------------------
#define SPLASH_MAX_DEPTH	120.0f
void CEnvSplash::InputSplash( inputdata_t &inputdata )
{
	CEffectData	data;

	data.m_fFlags = 0;

	float scale = m_flScale;

	if(GetEngineObject()->HasSpawnFlags( SF_ENVSPLASH_FINDWATERSURFACE ) )
	{
		if( UTIL_PointContents(GetEngineObject()->GetAbsOrigin()) & MASK_WATER )
		{
			// No splash if I'm supposed to find the surface of the water, but I'm underwater.
			return;
		}

		// Trace down and find the water's surface. This is designed for making
		// splashes on the surface of water that can change water level.
		trace_t tr;
		UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() - Vector( 0, 0, 4096 ), (MASK_WATER|MASK_SOLID_BRUSHONLY), this, COLLISION_GROUP_NONE, &tr );
		data.m_vOrigin = tr.endpos;

		if ( tr.contents & CONTENTS_SLIME )
		{
			data.m_fFlags |= FX_WATER_IN_SLIME;
		}
	}
	else
	{
		data.m_vOrigin = GetEngineObject()->GetAbsOrigin();
	}

	if(GetEngineObject()->HasSpawnFlags( SF_ENVSPLASH_DIMINISH ) )
	{
		// Get smaller if I'm in deeper water.
		float depth = 0.0f;

		trace_t tr;
		UTIL_TraceLine( data.m_vOrigin, data.m_vOrigin - Vector( 0, 0, 4096 ), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

		depth = fabs( tr.startpos.z - tr.endpos.z );

		float factor = 1.0f - (depth / SPLASH_MAX_DEPTH);

		if( factor < 0.1 )
		{
			// Don't bother making one this small.
			return;
		}

		scale *= factor;
	}

	data.m_vNormal = Vector( 0, 0, 1 );
	data.m_flScale = scale;

	DispatchEffect( "watersplash", data );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class CEnvGunfire : public CPointEntity
{
public:
	DECLARE_CLASS( CEnvGunfire, CPointEntity );

	CEnvGunfire()
	{
		// !!!HACKHACK
		// These fields came along kind of late, so they get
		// initialized in the constructor for now. (sjb)
		m_flBias = 1.0f;
		m_bCollide = false;
	}

	void Precache();
	void Spawn();
	void Activate();
	void StartShooting();
	void StopShooting();
	void ShootThink();
	void UpdateTarget();

	void InputEnable( inputdata_t &inputdata );
	void InputDisable( inputdata_t &inputdata );

	int	m_iMinBurstSize;
	int m_iMaxBurstSize;

	float m_flMinBurstDelay;
	float m_flMaxBurstDelay;

	float m_flRateOfFire;

	string_t	m_iszShootSound;
	string_t	m_iszTracerType;

	bool m_bDisabled;

	int	m_iShotsRemaining;

	int		m_iSpread;
	Vector	m_vecSpread;
	Vector	m_vecTargetPosition;
	float	m_flTargetDist;

	float	m_flBias;
	bool	m_bCollide;

	EHANDLE m_hTarget;


	DECLARE_DATADESC();
};

BEGIN_DATADESC( CEnvGunfire )
	DEFINE_KEYFIELD( m_iMinBurstSize, FIELD_INTEGER, "minburstsize" ),
	DEFINE_KEYFIELD( m_iMaxBurstSize, FIELD_INTEGER, "maxburstsize" ),
	DEFINE_KEYFIELD( m_flMinBurstDelay, FIELD_TIME, "minburstdelay" ),
	DEFINE_KEYFIELD( m_flMaxBurstDelay, FIELD_TIME, "maxburstdelay" ),
	DEFINE_KEYFIELD( m_flRateOfFire, FIELD_FLOAT, "rateoffire" ),
	DEFINE_KEYFIELD( m_iszShootSound, FIELD_STRING, "shootsound" ),
	DEFINE_KEYFIELD( m_iszTracerType, FIELD_STRING, "tracertype" ),
	DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "startdisabled" ),
	DEFINE_KEYFIELD( m_iSpread, FIELD_INTEGER, "spread" ),
	DEFINE_KEYFIELD( m_flBias, FIELD_FLOAT, "bias" ),
	DEFINE_KEYFIELD( m_bCollide, FIELD_BOOLEAN, "collisions" ),

	DEFINE_FIELD( m_iShotsRemaining, FIELD_INTEGER ),
	DEFINE_FIELD( m_vecSpread, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecTargetPosition, FIELD_VECTOR ),
	DEFINE_FIELD( m_flTargetDist, FIELD_FLOAT ),

	DEFINE_FIELD( m_hTarget, FIELD_EHANDLE ),

	DEFINE_THINKFUNC( ShootThink ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
END_DATADESC()
LINK_ENTITY_TO_CLASS( env_gunfire, CEnvGunfire );

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::Precache()
{
	g_pSoundEmitterSystem->PrecacheScriptSound( STRING( m_iszShootSound ) );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::Spawn()
{
	Precache();

	m_iShotsRemaining = 0;
	m_flRateOfFire = 1.0f / m_flRateOfFire;

	switch( m_iSpread )
	{
	case 1:
		m_vecSpread = VECTOR_CONE_1DEGREES;
		break;
	case 5:
		m_vecSpread = VECTOR_CONE_5DEGREES;
		break;
	case 10:
		m_vecSpread = VECTOR_CONE_10DEGREES;
		break;
	case 15:
		m_vecSpread = VECTOR_CONE_15DEGREES;
		break;

	default:
		m_vecSpread = vec3_origin;
		break;
	}

	if( !m_bDisabled )
	{
		StartShooting();
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::Activate( void )
{
	// Find my target
	if (m_target != NULL_STRING)
	{
		m_hTarget = (CBaseEntity*)EntityList()->FindEntityByName( NULL, m_target );
	}

	BaseClass::Activate();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::StartShooting()
{
	m_iShotsRemaining = random->RandomInt( m_iMinBurstSize, m_iMaxBurstSize );

	SetThink( &CEnvGunfire::ShootThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::UpdateTarget()
{
	if( m_hTarget )
	{
		if( m_hTarget->WorldSpaceCenter() != m_vecTargetPosition )
		{
			// Target has moved.
			// Locate my target and cache the position and distance.
			m_vecTargetPosition = m_hTarget->WorldSpaceCenter();
			m_flTargetDist = (GetEngineObject()->GetAbsOrigin() - m_vecTargetPosition).Length();
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::StopShooting()
{
	SetThink( NULL );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::ShootThink()
{
	if( !m_hTarget )
	{
		StopShooting();
	}

	GetEngineObject()->SetNextThink( gpGlobals->curtime + m_flRateOfFire );

	UpdateTarget();

	Vector vecDir = m_vecTargetPosition - GetEngineObject()->GetAbsOrigin();
	VectorNormalize( vecDir );

	CShotManipulator manipulator( vecDir );

	vecDir = manipulator.ApplySpread( m_vecSpread, m_flBias );

	Vector vecEnd;

	if( m_bCollide )
	{
		trace_t tr;

		UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() + vecDir * 8192, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

		if( tr.fraction != 1.0 )
		{
			DoImpactEffect( tr, DMG_BULLET );
		}

		vecEnd = tr.endpos;
	}
	else
	{
		vecEnd = GetEngineObject()->GetAbsOrigin() + vecDir * m_flTargetDist;
	}

	if( m_iszTracerType != NULL_STRING )
	{
		UTIL_Tracer(GetEngineObject()->GetAbsOrigin(), vecEnd, 0, TRACER_DONT_USE_ATTACHMENT, 5000, true, STRING(m_iszTracerType) );
	}
	else
	{
		UTIL_Tracer(GetEngineObject()->GetAbsOrigin(), vecEnd, 0, TRACER_DONT_USE_ATTACHMENT, 5000, true );
	}

	const char* soundname = STRING(m_iszShootSound);
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);

	m_iShotsRemaining--;

	if( m_iShotsRemaining == 0 )
	{
		StartShooting();
		GetEngineObject()->SetNextThink( gpGlobals->curtime + random->RandomFloat( m_flMinBurstDelay, m_flMaxBurstDelay ) );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::InputEnable( inputdata_t &inputdata )
{
	m_bDisabled = false;
	StartShooting();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvGunfire::InputDisable( inputdata_t &inputdata )
{
	m_bDisabled = true;
	SetThink( NULL );
}

//-----------------------------------------------------------------------------
// Quadratic spline beam effect
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CEnvQuadraticBeam )
	DEFINE_FIELD( m_targetPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_controlPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_scrollRate, FIELD_FLOAT ),
	DEFINE_FIELD( m_flWidth, FIELD_FLOAT ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( env_quadraticbeam, CEnvQuadraticBeam );

IMPLEMENT_SERVERCLASS_ST( CEnvQuadraticBeam, DT_QuadraticBeam )
	SendPropVector(SENDINFO(m_targetPosition), -1, SPROP_COORD),
	SendPropVector(SENDINFO(m_controlPosition), -1, SPROP_COORD),
	SendPropFloat(SENDINFO(m_scrollRate), 8, 0, -4, 4),
	SendPropFloat(SENDINFO(m_flWidth), -1, SPROP_NOSCALE),
END_SEND_TABLE()

void CEnvQuadraticBeam::Spawn()
{
	BaseClass::Spawn();
	m_nRenderMode = kRenderTransAdd;
	SetRenderColor( 255, 255, 255 );
}

CEnvQuadraticBeam *CreateQuadraticBeam( const char *pSpriteName, const Vector &start, const Vector &control, const Vector &end, float width, CBaseEntity *pOwner )
{
	CEnvQuadraticBeam *pBeam = (CEnvQuadraticBeam *)CBaseEntity::Create( "env_quadraticbeam", start, vec3_angle, pOwner );
	UTIL_SetModel( pBeam, pSpriteName );
	pBeam->SetSpline( control, end );
	pBeam->SetScrollRate( 0.0 );
	pBeam->SetWidth(width);
	return pBeam;
}

void EffectsPrecache( void *pUser )
{
	g_pSoundEmitterSystem->PrecacheScriptSound( "Underwater.BulletImpact" );

	g_pSoundEmitterSystem->PrecacheScriptSound( "FX_RicochetSound.Ricochet" );

	g_pSoundEmitterSystem->PrecacheScriptSound( "Physics.WaterSplash" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "BaseExplosionEffect.Sound" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "Splash.SplashSound" );

	if ( gpGlobals->maxClients > 1 )
	{
		g_pSoundEmitterSystem->PrecacheScriptSound( "HudChat.Message" );
	}
}

PRECACHE_REGISTER_FN( EffectsPrecache );


class CEnvViewPunch : public CPointEntity
{
public:

	DECLARE_CLASS( CEnvViewPunch, CPointEntity );

	virtual void Spawn();

	// Input handlers
	void InputViewPunch( inputdata_t &inputdata );

private:

	float m_flRadius;
	QAngle m_angViewPunch;

	void DoViewPunch();

	DECLARE_DATADESC();
};

LINK_ENTITY_TO_CLASS( env_viewpunch, CEnvViewPunch );

BEGIN_DATADESC( CEnvViewPunch )

	DEFINE_KEYFIELD( m_angViewPunch, FIELD_VECTOR, "punchangle" ),
	DEFINE_KEYFIELD( m_flRadius, FIELD_FLOAT, "radius" ),

	DEFINE_INPUTFUNC( FIELD_VOID, "ViewPunch", InputViewPunch ),

END_DATADESC()

#define SF_PUNCH_EVERYONE	0x0001		// Don't check radius
#define SF_PUNCH_IN_AIR		0x0002		// Punch players in air


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvViewPunch::Spawn( void )
{
	GetEngineObject()->SetSolid( SOLID_NONE );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );

	if (GetEngineObject()->GetSpawnFlags() & SF_PUNCH_EVERYONE )
	{
		m_flRadius = 0;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvViewPunch::DoViewPunch()
{
	bool bAir = (GetEngineObject()->GetSpawnFlags() & SF_PUNCH_IN_AIR) ? true : false;
	UTIL_ViewPunch(GetEngineObject()->GetAbsOrigin(), m_angViewPunch, m_flRadius, bAir );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEnvViewPunch::InputViewPunch( inputdata_t &inputdata )
{
	DoViewPunch();
}
