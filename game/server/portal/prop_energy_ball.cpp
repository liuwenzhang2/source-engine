//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Defines a combine ball and a combine ball launcher which have certain properties
//			overwritten to make use of them in portal game play.
//
//=====================================================================================//

#include "cbase.h"					// for pch
#include "prop_combine_ball.h"		// for base class
#include "te_effect_dispatch.h"		// for the explosion/impact effects
#include "prop_portal.h"			// Special case code for passing through portals. We need the class definition.
#include "soundenvelope.h"
#include "physicsshadowclone.h"

// resource file names
#define IMPACT_DECAL_NAME	"decals/smscorch1model"

// context think
#define UPDATE_THINK_CONTEXT	"UpdateThinkContext"

class CPropEnergyBall : public CPropCombineBall
{
public:
	DECLARE_CLASS( CPropEnergyBall, CPropCombineBall );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	virtual void Precache();
	virtual void CreateSounds( void );
	virtual void StopLoopingSounds( void );
	virtual void Spawn();
	virtual void Activate( void );

	// Overload for unlimited bounces and predictable movement
	virtual void VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );
	// Overload for less sound, no shake.
	virtual void ExplodeThink( void );
	// Update in a time till death update
	virtual void Think ( void );
	virtual void EndTouch( CBaseEntity *pOther );
	virtual void StartTouch( CBaseEntity *pOther );
	virtual void NotifySystemEvent( CBaseEntity *pNotify, notify_system_event_t eventType, const notify_system_event_params_t &params );

	CHandle<CProp_Portal>		m_hTouchedPortal;	// Pointer to the portal we are touched most recently
	bool						m_bTouchingPortal1;	// Are we touching portal 1
	bool						m_bTouchingPortal2;	// Are we touching portal 2

	// Remember the last known direction of travel, incase our velocity is cleared.
	Vector						m_vLastKnownDirection;

	// After portal teleports, we force the life to be at least this number.
	float						m_fMinLifeAfterPortal;

	CNetworkVar( bool, m_bIsInfiniteLife );
	CNetworkVar( float, m_fTimeTillDeath );

	CSoundPatch		*m_pAmbientSound;

};

LINK_ENTITY_TO_CLASS( prop_energy_ball, CPropEnergyBall );


BEGIN_DATADESC( CPropEnergyBall )

	DEFINE_FIELD( m_hTouchedPortal,			FIELD_EHANDLE ),
	DEFINE_FIELD( m_bTouchingPortal1,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bTouchingPortal2,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vLastKnownDirection,	FIELD_VECTOR ),
	DEFINE_FIELD( m_fMinLifeAfterPortal,	FIELD_FLOAT ),
	DEFINE_FIELD( m_bIsInfiniteLife,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fTimeTillDeath,			FIELD_FLOAT ),

	DEFINE_SOUNDPATCH( m_pAmbientSound ),

	DEFINE_THINKFUNC( Think ),

END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CPropEnergyBall, DT_PropEnergyBall )

	SendPropBool( SENDINFO( m_bIsInfiniteLife ) ),
	SendPropFloat ( SENDINFO( m_fTimeTillDeath ) ),

END_SEND_TABLE()



//-----------------------------------------------------------------------------
// Purpose: Precache
// Input  :  - 
//-----------------------------------------------------------------------------
void CPropEnergyBall::Precache()
{
	BaseClass::Precache();

	g_pSoundEmitterSystem->PrecacheScriptSound( "EnergyBall.Explosion" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "EnergyBall.Launch" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "EnergyBall.Impact" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "EnergyBall.AmbientLoop" );
	UTIL_PrecacheDecal( IMPACT_DECAL_NAME, false );

}


void CPropEnergyBall::CreateSounds()
{
	if (!m_pAmbientSound)
	{
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( this );

		m_pAmbientSound = controller.SoundCreate( filter, entindex(), "EnergyBall.AmbientLoop" );
		controller.Play( m_pAmbientSound, 1.0, 100 );
	}
}


void CPropEnergyBall::StopLoopingSounds()
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	controller.SoundDestroy( m_pAmbientSound );
	m_pAmbientSound = NULL;

	BaseClass::StopLoopingSounds();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  :  - 
//-----------------------------------------------------------------------------
void CPropEnergyBall::Spawn()
{
	Precache();

	BaseClass::Spawn();

	m_bTouchingPortal1 = false;
	m_bTouchingPortal2 = false;
	m_bIsInfiniteLife  = false;
	m_fTimeTillDeath = -1;
	m_fMinLifeAfterPortal = 5;
	// Init last known direction to our initial direction
	GetVelocity( &m_vLastKnownDirection, NULL );
}

void CPropEnergyBall::Activate( void )
{
	BaseClass::Activate();

	CreateSounds();
}

//-----------------------------------------------------------------------------
// Purpose: Keep a constant velocity despite collisions, make impact sounds and effects
//-----------------------------------------------------------------------------
void CPropEnergyBall::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	// Skip combine ball's collision, but do everything below it.

	BaseClass::BaseClass::VPhysicsCollision( index, pEvent );

	Vector preVelocity = pEvent->preVelocity[index];
//	float flSpeed = VectorNormalize( preVelocity );

	// It's ok to change direction, but maintain speed = m_flSpeed.
	Vector vecFinalVelocity = pEvent->postVelocity[index];
	VectorNormalize( vecFinalVelocity );

	if ( m_bTouchingPortal2 || m_bTouchingPortal1 )
	{
		AssertMsg ( m_hTouchedPortal.Get(), "Touching a portal, but recorded an invalid handle." );
	}

	// Used for deciding if we play our impact effects/sounds
	bool bIsEnteringPortalAndLockingAxisForward = false;

	// Fixed bounce axis when in a portal environment
	if ( (m_bTouchingPortal2 || m_bTouchingPortal1) && m_hTouchedPortal.Get() )
	{
		// Force our velocity to be either towards or away from the portal, no bouncing at odd angles allowed
		IEnginePortalServer* pPortal = m_hTouchedPortal.Get()->pCollisionEntity->GetEnginePortal();

		// Only lock to the portal's forward axis if we're in it's world bounds
		// We use a tolerance of four, because the render bounds thickness for a portal is 4, and this function
		// intersects with a plane.
		bool bHitPortal = UTIL_IsBoxIntersectingPortal(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->WorldAlignSize(), pPortal, 4.0f );

		// We definitely hit a portal
		if ( bHitPortal && pPortal && pPortal->IsActivedAndLinked() )
		{
			Vector vecTouchedPortalFace;
			pPortal->AsEngineObject()->GetVectors( &vecTouchedPortalFace, NULL, NULL );
			vecTouchedPortalFace.NormalizeInPlace();
			float fDot = vecTouchedPortalFace.Dot( vecFinalVelocity );

			// closer to 'towards' the portal, force it to go that direction
			if ( fDot < 0 )
			{
				vecFinalVelocity = -vecTouchedPortalFace;

				// Since we're going 'through', don't do surfaceprop based collision effects
				// because the it will look like we didn't hit anything.
				pEvent->surfaceProps[0] = pEvent->surfaceProps[1] = EntityList()->PhysGetProps()->GetSurfaceIndex( "default" );
				bIsEnteringPortalAndLockingAxisForward = true;
			}
			else // Closer to 'away from' the portal. Force the energy ball to go that direction
			{
				vecFinalVelocity = vecTouchedPortalFace;
			}
		}
	}


	// Plant a decal on any solid brushes we hit
	if ( !bIsEnteringPortalAndLockingAxisForward )
	{
		trace_t		tr;
		UTIL_TraceLine (GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() + 60*preVelocity, MASK_SHOT,
			this, COLLISION_GROUP_NONE, &tr);

		// Only place decals and draw effects if we hit something valid
		if ( tr.m_pEnt )
		{

			// Cball impact effect (using same trace as the decal placement above)
			CEffectData data;
			data.m_flRadius = 16;
			data.m_vNormal	= tr.plane.normal;
			data.m_vOrigin	= tr.endpos + tr.plane.normal * 1.0f;


			DispatchEffect( "cball_bounce", data );

			if ( tr.m_pEnt )
			{
				UTIL_DecalTrace( &tr, "EnergyBall.Impact" );
			}
		}

		const char* soundname = "EnergyBall.Impact";
		CPASAttenuationFilter filter(this, soundname);

		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = 0.0f;
		params.m_pflSoundDuration = NULL;
		params.m_bWarnOnDirectWaveReference = true;
		g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
	}
	
	// Record our direction so our fixed direction hacks know we have changed direction immediately
	m_vLastKnownDirection = vecFinalVelocity; 

	// Scale new velocity to our fixed speed
	vecFinalVelocity *= GetSpeed();

	// Try to update the velocity now, however I'm told this rarely works.
	// We will spam updates in our think function to help get us in the direction we want to go.
	gEntList.PhysCallbackSetVelocity( pEvent->pObjects[index], vecFinalVelocity );
}

void CPropEnergyBall::NotifySystemEvent(CBaseEntity *pNotify, notify_system_event_t eventType, const notify_system_event_params_t &params )
{
	// On teleport, we record a pointer to the portal we are arriving at
	if ( eventType == NOTIFY_EVENT_TELEPORT )
	{
		CProp_Portal *pEnteredPortal = dynamic_cast<CProp_Portal*>( pNotify );
		if( pEnteredPortal )
		{
			m_vLastKnownDirection = pEnteredPortal->MatrixThisToLinked().ApplyRotation(m_vLastKnownDirection);
			m_vLastKnownDirection.NormalizeInPlace();

			IPhysicsObject *pPhysObject = GetEngineObject()->VPhysicsGetObject();
			if( pPhysObject )
			{
				Vector vNewVelocity = m_vLastKnownDirection * GetSpeed();
				pPhysObject->SetVelocityInstantaneous( &vNewVelocity, NULL );
			}

			// Record the new portal for the purposes of locking our movement
			m_hTouchedPortal = pEnteredPortal->GetLinkedPortal();
		}
		else
		{
			m_hTouchedPortal = NULL;
		}

		// If an energy ball passes a portal (teleports), add a make sure its life is >= sk_energy_ball_min_life_after_portal
		float fCurTimeTillDeath = GetEngineObject()->GetNextThink( "ExplodeTimerContext" );
		// If we are set to die, then refresh that time if it is below a set threshold
		if ( fCurTimeTillDeath > 0 )
		{
			float fTimeLeft = fCurTimeTillDeath - gpGlobals->curtime;
			float fMinLife = m_fMinLifeAfterPortal;
			float fTimeToDie = (fTimeLeft > fMinLife) ? (fTimeLeft) : (fMinLife);
			SetContextThink( &CPropCombineBall::ExplodeThink, gpGlobals->curtime + fTimeToDie, "ExplodeTimerContext" );
		}
	}

	//BaseClass::NotifySystemEvent( pNotify, eventType, params );
}

//-----------------------------------------------------------------------------
// Purpose: Send down the time till death to the client code to help indicate when the ball will detonate
// Input  :  - 
//-----------------------------------------------------------------------------
void CPropEnergyBall::Think()
{
	// Finite life energy balls send the time till death down to the client for display purposes
	if ( !m_bIsInfiniteLife )
	{
		m_fTimeTillDeath = GetEngineObject()->GetNextThink( "ExplodeTimerContext" ) - gpGlobals->curtime;
		GetEngineObject()->SetNextThink ( gpGlobals->curtime + 0.5f );
	}

	// Force our movement to be at desired speed
	IPhysicsObject* pMyObject = GetEngineObject()->VPhysicsGetObject();
	if ( pMyObject )
	{
		// get our current speed
		Vector vCurVelocity, vNewVelocity;
		pMyObject->GetVelocity( &vCurVelocity, NULL );
		float fCurSpeed = vCurVelocity.Length();

		if ( fCurSpeed < GetSpeed() )
		{
			m_vLastKnownDirection.NormalizeInPlace();
			vNewVelocity = m_vLastKnownDirection * GetSpeed();
			pMyObject->SetVelocityInstantaneous( &vNewVelocity, NULL );
		}
	}

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1 );
}



//-----------------------------------------------------------------------------
// Purpose: Make a sound/effect for the removal of the energy ball, and switch to the cleanup think
// Input  :  - 
//-----------------------------------------------------------------------------
void CPropEnergyBall::ExplodeThink( )
{
	// Tell the respawner to make a new one
	if ( GetSpawner() )
	{
		GetSpawner()->RespawnBallPostExplosion();
	}

	//Destruction effect
	CBroadcastRecipientFilter filter2;
	CEffectData data;
	data.m_vOrigin = GetEngineObject()->GetAbsOrigin();
	DispatchEffect( "ManhackSparks", data );
	const char* soundname = "EnergyBall.Explosion";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);

	// Turn us off and wait because we need our trails to finish up properly
	GetEngineObject()->SetAbsVelocity( vec3_origin );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	GetEngineObject()->AddSolidFlags( FSOLID_NOT_SOLID );

	SetEmitState( false );

	SetContextThink( &CPropCombineBall::SUB_Remove, gpGlobals->curtime + 0.5f, "RemoveContext" );
	StopLoopingSounds();
}

void CPropEnergyBall::StartTouch( CBaseEntity *pOther )
{
	Assert( pOther );

	if(pOther->GetEngineObject()->IsShadowClone() )
	{
		CBaseEntity *pCloned = ((CPhysicsShadowClone *)pOther)->GetEngineShadowClone()->GetClonedEntity();
		if( pCloned )
			pOther = pCloned;
	}

	// Kill the player on hit.
	if ( pOther->IsPlayer() )
	{
		CTakeDamageInfo info( this, GetOwnerEntity(), GetEngineObject()->GetAbsVelocity(), GetEngineObject()->GetAbsOrigin(), 1500.0f, DMG_DISSOLVE );
	 	pOther->OnTakeDamage( info );
		
		// Destruct when we hit the player
		SetContextThink( &CPropCombineBall::ExplodeThink, gpGlobals->curtime, "ExplodeTimerContext" );
	}

	CProp_Portal* pPortal = dynamic_cast<CProp_Portal*>(pOther);
	// If toucher is a prop portal
	if ( pPortal )
	{
		// Record the touched portal for locking collision movements.
		// The forward direction we want to follow is the forward vector of the portal we've touched most recently
		m_hTouchedPortal = pPortal;

		// record that we touched this portal
		if ( pPortal->pCollisionEntity->GetEnginePortal()->IsPortal2() == false)
		{
			m_bTouchingPortal1 = true;
		}
		else //if ( pPortal->m_bIsPortal2 == true )
		{
			m_bTouchingPortal2 = true;
		}
	}

	BaseClass::StartTouch( pOther );
}

void CPropEnergyBall::EndTouch( CBaseEntity *pOther )
{
	CProp_Portal* pPortal = dynamic_cast<CProp_Portal*>(pOther);

	if ( pPortal )
	{
		// We are no longer touching this portal
		if ( pPortal->pCollisionEntity->GetEnginePortal()->IsPortal2() == false)
		{
			m_bTouchingPortal1 = false;
		}
		else //if ( pPortal->m_bIsPortal2 == true )
		{
			m_bTouchingPortal2 = false;
		}
	}

	BaseClass::EndTouch( pOther );
	
}

class CEnergyBallLauncher : public CPointCombineBallLauncher
{
public:
	DECLARE_CLASS( CEnergyBallLauncher, CPointCombineBallLauncher );
	DECLARE_DATADESC();

	virtual void SpawnBall();
	virtual void Precache();
	virtual void Spawn();

private:
	float	m_fBallLifetime;
	float	m_fMinBallLifeAfterPortal;

	COutputEvent		m_OnPostSpawnBall;


};

LINK_ENTITY_TO_CLASS( point_energy_ball_launcher, CEnergyBallLauncher );

BEGIN_DATADESC( CEnergyBallLauncher )
	
	DEFINE_KEYFIELD( m_fBallLifetime, FIELD_FLOAT, "BallLifetime" ),
	DEFINE_KEYFIELD( m_fMinBallLifeAfterPortal, FIELD_FLOAT, "MinLifeAfterPortal" ),

	DEFINE_OUTPUT ( m_OnPostSpawnBall, "OnPostSpawnBall" ),

END_DATADESC()

void CEnergyBallLauncher::Precache()
{
	BaseClass::Precache();

	UTIL_PrecacheDecal( IMPACT_DECAL_NAME, false );
}

void CEnergyBallLauncher::Spawn()
{
	Precache();

	BaseClass::Spawn();
}


void CEnergyBallLauncher::SpawnBall()
{
	CPropEnergyBall *pBall = static_cast<CPropEnergyBall*>(gEntList.CreateEntityByName( "prop_energy_ball" ) );

	if ( pBall == NULL )
		return;

	pBall->SetRadius( m_flBallRadius );
	Vector vecAbsOrigin = GetEngineObject()->GetAbsOrigin();
	Vector zaxis;

	pBall->GetEngineObject()->SetAbsOrigin( vecAbsOrigin );
	pBall->SetSpawner( this );

	pBall->SetSpeed( m_flMaxSpeed );
	float flSpeed = m_flMaxSpeed;

	Vector vDirection;
	QAngle qAngle = GetEngineObject()->GetAbsAngles();
	AngleVectors( qAngle, &vDirection, NULL, NULL );

	vDirection *= flSpeed;
	pBall->GetEngineObject()->SetAbsVelocity( vDirection );

	DispatchSpawn(pBall);
	pBall->Activate();
	pBall->SetState( CPropCombineBall::STATE_LAUNCHED );
	pBall->GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_PROJECTILE );
	pBall->m_fMinLifeAfterPortal = m_fMinBallLifeAfterPortal;

	// Additional setup of the physics object for energy ball uses
	IPhysicsObject *pBallObj = pBall->GetEngineObject()->VPhysicsGetObject();

	if ( pBallObj )
	{
		// Make sure we dont use air drag
		pBallObj->EnableDrag( false );

		// Remove damping
		float speed, rot;
		speed = rot = 0.0f;
		pBallObj->SetDamping( &speed, &rot );

		// HUGE rotational inertia, don't allow the ball to have any spin
        Vector vInertia( 1e30, 1e30, 1e30 );
		pBallObj->SetInertia( vInertia );

		// Low mass to let it bounce off of obstructions for certain puzzles.
		pBallObj->SetMass( 1.0f );
	}

	// Only expire if the lifetme field is positive
	if ( m_fBallLifetime >=0 )
	{
		pBall->StartLifetime( m_fBallLifetime );
		pBall->m_bIsInfiniteLife = false;
	}
	else
	{	
		pBall->m_bIsInfiniteLife = true;
	}

	// Think function, used to update time till death and avoid sleeping
	pBall->GetEngineObject()->SetNextThink ( gpGlobals->curtime + 0.1f );

	const char* soundname = "EnergyBall.Launch";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);

	m_OnPostSpawnBall.FireOutput( this, this );
}










static void fire_energy_ball_f( void )
{
	if( sv_cheats->GetBool() == false ) //heavy handed version since setting the concommand with FCVAR_CHEATS isn't working like I thought
		return;

	CBasePlayer *pPlayer = (CBasePlayer *)UTIL_GetCommandClient();

	Vector ptEyes, vForward;
	ptEyes = pPlayer->EyePosition();
	pPlayer->EyeVectors( &vForward );




	{
		CPropEnergyBall *pBall = static_cast<CPropEnergyBall*>(gEntList.CreateEntityByName( "prop_energy_ball" ) );

		if ( pBall == NULL )
			return;

		pBall->SetRadius( 12.0f );

		pBall->GetEngineObject()->SetAbsOrigin( ptEyes + (vForward * 50.0f) );
		pBall->SetSpawner( NULL );

		pBall->SetSpeed( 400.0f );
				

		pBall->GetEngineObject()->SetAbsVelocity( vForward * 400.0f );

		DispatchSpawn(pBall);
		pBall->Activate();
		pBall->SetState( CPropCombineBall::STATE_LAUNCHED );
		pBall->GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_PROJECTILE );
		pBall->m_fMinLifeAfterPortal = 5.0f;

		// Additional setup of the physics object for energy ball uses
		IPhysicsObject *pBallObj = pBall->GetEngineObject()->VPhysicsGetObject();

		if ( pBallObj )
		{
			// Make sure we dont use air drag
			pBallObj->EnableDrag( false );

			// Remove damping
			float speed, rot;
			speed = rot = 0.0f;
			pBallObj->SetDamping( &speed, &rot );

			// HUGE rotational inertia, don't allow the ball to have any spin
			Vector vInertia( 1e30, 1e30, 1e30 );
			pBallObj->SetInertia( vInertia );

			// Low mass to let it bounce off of obstructions for certain puzzles.
			pBallObj->SetMass( 1.0f );
		}

		pBall->StartLifetime( 10.0f );
		pBall->m_bIsInfiniteLife = false;

		// Think function, used to update time till death and avoid sleeping
		pBall->GetEngineObject()->SetNextThink ( gpGlobals->curtime + 0.1f );

	}


}

ConCommand fire_energy_ball( "fire_energy_ball", fire_energy_ball_f, "Fires a test energy ball out of your face", FCVAR_CHEAT );