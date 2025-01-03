//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Bullseyes act as targets for other NPC's to attack and to trigger
//			events 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#include	"cbase.h"
#include	"ai_default.h"
#include	"ai_task.h"
#include	"ai_schedule.h"
#include	"ai_node.h"
#include	"ai_hull.h"
#include	"ai_hint.h"
#include	"ai_route.h"
#include	"soundent.h"
#include	"game.h"
#include	"npcevent.h"
#include	"activitylist.h"
#include	"animation.h"
#include	"basecombatweapon.h"
#include	"IEffects.h"
#include	"vstdlib/random.h"
#include	"engine/IEngineSound.h"
#include	"ammodef.h"
#include	"hl1_ai_basenpc.h"

#define		AFLOCK_MAX_RECRUIT_RADIUS	1024
#define		AFLOCK_FLY_SPEED			125
#define		AFLOCK_TURN_RATE			75
#define		AFLOCK_ACCELERATE			10
#define		AFLOCK_CHECK_DIST			192
#define		AFLOCK_TOO_CLOSE			100
#define		AFLOCK_TOO_FAR				256

//=========================================================
//=========================================================
class CNPC_FlockingFlyerFlock : public CHL1BaseNPC
{
	DECLARE_CLASS( CNPC_FlockingFlyerFlock, CHL1BaseNPC );
public:

	void Spawn( void );
	void Precache( void );
	bool KeyValue( const char *szKeyName, const char *szValue );
	void SpawnFlock( void );

	// Sounds are shared by the flock
	static  void PrecacheFlockSounds( void );
	
	DECLARE_DATADESC();

	int		m_cFlockSize;
	float	m_flFlockRadius;
};

BEGIN_DATADESC( CNPC_FlockingFlyerFlock )
	DEFINE_FIELD( m_cFlockSize, FIELD_INTEGER ),
	DEFINE_FIELD( m_flFlockRadius, FIELD_FLOAT ),
END_DATADESC()

class CNPC_FlockingFlyer : public CHL1BaseNPC
{
	DECLARE_CLASS( CNPC_FlockingFlyer, CHL1BaseNPC );
public:
	void Spawn( void );
	void Precache( void );
	void SpawnCommonCode( void );
	void IdleThink( void );
	void BoidAdvanceFrame( void );
	void Start( void );
	bool FPathBlocked( void );
	void FlockLeaderThink( void );
	void SpreadFlock( void );
	void SpreadFlock2( void );
	void MakeSound( void );
	void FlockFollowerThink( void );
	void Event_Killed( const CTakeDamageInfo &info );
	void FallHack( void );
	//void Poop ( void ); Adrian - wtf?!

	

	int IsLeader( void ) { return m_pSquadLeader == this; }
	int	InSquad( void ) { return m_pSquadLeader != NULL; }
	int	SquadCount( void );
	void SquadRemove( CNPC_FlockingFlyer *pRemove );
	void SquadUnlink( void );
	void SquadAdd( CNPC_FlockingFlyer *pAdd );
	void SquadDisband( void );

	CNPC_FlockingFlyer *m_pSquadLeader;
	CNPC_FlockingFlyer *m_pSquadNext;
	bool	m_fTurning;// is this boid turning?
	bool	m_fCourseAdjust;// followers set this flag TRUE to override flocking while they avoid something
	bool	m_fPathBlocked;// TRUE if there is an obstacle ahead
	Vector	m_vecReferencePoint;// last place we saw leader
	Vector	m_vecAdjustedVelocity;// adjusted velocity (used when fCourseAdjust is TRUE)
	float	m_flGoalSpeed;
	float	m_flLastBlockedTime;
	float	m_flFakeBlockedTime;
	float	m_flAlertTime;
	float	m_flFlockNextSoundTime;
	float   m_flTempVar;

	DECLARE_DATADESC();
};

BEGIN_DATADESC( CNPC_FlockingFlyer )
	DEFINE_FIELD( m_pSquadLeader, FIELD_CLASSPTR ),
	DEFINE_FIELD( m_pSquadNext, FIELD_CLASSPTR ),
	DEFINE_FIELD( m_fTurning, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fCourseAdjust, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fPathBlocked, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecReferencePoint, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecAdjustedVelocity, FIELD_VECTOR ),
	DEFINE_FIELD( m_flGoalSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( m_flLastBlockedTime, FIELD_TIME ),
	DEFINE_FIELD( m_flFakeBlockedTime, FIELD_TIME ),
	DEFINE_FIELD( m_flAlertTime, FIELD_TIME ),
	DEFINE_THINKFUNC( IdleThink ),
	DEFINE_THINKFUNC( Start ),
	DEFINE_THINKFUNC( FlockLeaderThink ),
	DEFINE_THINKFUNC( FlockFollowerThink ),
	DEFINE_THINKFUNC( FallHack ),

	DEFINE_FIELD( m_flFlockNextSoundTime, FIELD_TIME ),
	DEFINE_FIELD( m_flTempVar, FIELD_FLOAT ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( monster_flyer, CNPC_FlockingFlyer );
LINK_ENTITY_TO_CLASS( monster_flyer_flock, CNPC_FlockingFlyerFlock );

bool CNPC_FlockingFlyerFlock::KeyValue(	const char *szKeyName, const char *szValue )
{
	if ( FStrEq( szKeyName, "iFlockSize" ) )
	{
		m_cFlockSize = atoi( szValue );
		return true;
	}
	else if ( FStrEq( szKeyName, "flFlockRadius" ) )
	{
		m_flFlockRadius = atof( szValue );
		return true;
	}
	else
		BaseClass::KeyValue( szKeyName, szValue );

	return false;
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyerFlock::Spawn( void )
{
	Precache( );

	SetRenderColor( 255, 255, 255, 255 );
	SpawnFlock();


	SetThink( &CBaseEntity::SUB_Remove );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyerFlock::Precache( void )
{
	//PRECACHE_MODEL("models/aflock.mdl");		
	engine->PrecacheModel("models/boid.mdl");

	PrecacheFlockSounds();
}

void CNPC_FlockingFlyerFlock::SpawnFlock( void )
{
	float R = m_flFlockRadius;
	int iCount;
	Vector vecSpot;
	CNPC_FlockingFlyer *pBoid, *pLeader;

	pLeader = pBoid = NULL;

	for ( iCount = 0 ; iCount < m_cFlockSize ; iCount++ )
	{
		pBoid = (CNPC_FlockingFlyer*)EntityList()->CreateEntityByName( "monster_flyer" );
		
		if ( !pLeader ) 
		{
			// make this guy the leader.
			pLeader = pBoid;
			
			pLeader->m_pSquadLeader = pLeader;
			pLeader->m_pSquadNext = NULL;
		}

		vecSpot.x = random->RandomFloat( -R, R );
		vecSpot.y = random->RandomFloat( -R, R );
		vecSpot.z = random->RandomFloat( 0, 16 );
		vecSpot = GetEngineObject()->GetAbsOrigin() + vecSpot;

		UTIL_SetOrigin( pBoid, vecSpot);
		pBoid->GetEngineObject()->SetMoveType( MOVETYPE_FLY );
		pBoid->SpawnCommonCode();
		pBoid->GetEngineObject()->SetGroundEntity( NULL );
		pBoid->GetEngineObject()->SetAbsVelocity( Vector ( 0, 0, 0 ) );
		pBoid->GetEngineObject()->SetAbsAngles(GetEngineObject()->GetAbsAngles() );
		
		pBoid->GetEngineObject()->SetCycle( 0 );
		pBoid->SetThink( &CNPC_FlockingFlyer::IdleThink );
		pBoid->GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.2 );

		if ( pBoid != pLeader ) 
		{
			pLeader->SquadAdd( pBoid );
		}
	}
}


void CNPC_FlockingFlyerFlock::PrecacheFlockSounds( void )
{
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyer::Spawn( )
{
	Precache( );
	SpawnCommonCode();
	
	GetEngineObject()->SetCycle( 0 );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
	SetThink( &CNPC_FlockingFlyer::IdleThink );
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyer::SpawnCommonCode( )
{
	m_lifeState		= LIFE_ALIVE;
	SetClassname( "monster_flyer" );
	GetEngineObject()->SetSolid( SOLID_BBOX );
	GetEngineObject()->AddSolidFlags( FSOLID_NOT_STANDABLE );
	GetEngineObject()->SetMoveType( MOVETYPE_FLY );
	m_takedamage	= DAMAGE_NO;
	m_iHealth		= 1;

	m_fPathBlocked	= FALSE;// obstacles will be detected
	m_flFieldOfView	= 0.2;
	m_flTempVar		= 0;

	//SET_MODEL(ENT(pev), "models/aflock.mdl");
	SetModel( "models/boid.mdl" );

//	GetEngineObject()->SetSize(Vector(0,0,0), Vector(0,0,0));
	GetEngineObject()->SetSize(Vector(-5,-5,0), Vector(5,5,2));
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyer::Precache( )
{
	//PRECACHE_MODEL("models/aflock.mdl");
	engine->PrecacheModel("models/boid.mdl");
	CNPC_FlockingFlyerFlock::PrecacheFlockSounds();

	g_pSoundEmitterSystem->PrecacheScriptSound( "FlockingFlyer.Alert" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "FlockingFlyer.Idle" );
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyer::IdleThink( void )
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.2 );

	// see if there's a client in the same pvs as the monster
	IServerEntity* pEnt = EntityList()->FindClientInPVS(this);
	if (pEnt&& pEnt->entindex()>0)
	{
		SetThink( &CNPC_FlockingFlyer::Start );
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
	}
}

//=========================================================
//
// SquadUnlink(), Unlink the squad pointers.
//
//=========================================================
void CNPC_FlockingFlyer::SquadUnlink( void )
{
	m_pSquadLeader = NULL;
	m_pSquadNext	= NULL;
}

//=========================================================
//
// SquadAdd(), add pAdd to my squad
//
//=========================================================
void CNPC_FlockingFlyer::SquadAdd( CNPC_FlockingFlyer *pAdd )
{
	ASSERT( pAdd!=NULL );
	ASSERT( !pAdd->InSquad() );
	ASSERT( this->IsLeader() );

	pAdd->m_pSquadNext = m_pSquadNext;
	m_pSquadNext = pAdd;
	pAdd->m_pSquadLeader = this;
}
//=========================================================
//
// SquadRemove(), remove pRemove from my squad.
// If I am pRemove, promote m_pSquadNext to leader
//
//=========================================================
void CNPC_FlockingFlyer::SquadRemove( CNPC_FlockingFlyer *pRemove )
{
	ASSERT( pRemove!=NULL );
	ASSERT( this->IsLeader() );
	ASSERT( pRemove->m_pSquadLeader == this );

	if ( SquadCount() > 2 )
	{
		// Removing the leader, promote m_pSquadNext to leader
		if ( pRemove == this )
		{
			CNPC_FlockingFlyer *pLeader = m_pSquadNext;
			
			// copy the enemy LKP to the new leader

		//	if ( GetEnemy() )
		//		pLeader->m_vecEnemyLKP = m_vecEnemyLKP;

			if ( pLeader )
			{
				CNPC_FlockingFlyer *pList = pLeader;

				while ( pList )
				{
					pList->m_pSquadLeader = pLeader;
					pList = pList->m_pSquadNext;
				}

			}
			SquadUnlink();
		}
		else	// removing a node
		{
			CNPC_FlockingFlyer *pList = this;

			// Find the node before pRemove
			while ( pList->m_pSquadNext != pRemove )
			{
				// assert to test valid list construction
				ASSERT( pList->m_pSquadNext != NULL );
				pList = pList->m_pSquadNext;
			}
			// List validity
			ASSERT( pList->m_pSquadNext == pRemove );

			// Relink without pRemove
			pList->m_pSquadNext = pRemove->m_pSquadNext;

			// Unlink pRemove
			pRemove->SquadUnlink();
		}
	}
	else
		SquadDisband();
}
//=========================================================
//
// SquadCount(), return the number of members of this squad
// callable from leaders & followers
//
//=========================================================
int CNPC_FlockingFlyer::SquadCount( void )
{
	CNPC_FlockingFlyer *pList = m_pSquadLeader;
	int squadCount = 0;
	while ( pList )
	{
		squadCount++;
		pList = pList->m_pSquadNext;
	}

	return squadCount;
}

//=========================================================
//
// SquadDisband(), Unlink all squad members
//
//=========================================================
void CNPC_FlockingFlyer::SquadDisband( void )
{
	CNPC_FlockingFlyer *pList = m_pSquadLeader;
	CNPC_FlockingFlyer *pNext;

	while ( pList )
	{
		pNext = pList->m_pSquadNext;
		pList->SquadUnlink();
		pList = pNext;
	}
}

//=========================================================
// Start - player enters the pvs, so get things going.
//=========================================================
void CNPC_FlockingFlyer::Start( void )
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

	if ( IsLeader() )
	{
		SetThink( &CNPC_FlockingFlyer::FlockLeaderThink );
	}
	else
	{
		SetThink( &CNPC_FlockingFlyer::FlockFollowerThink );
	}

	SetActivity ( ACT_FLY );
	GetEngineObject()->ResetSequenceInfo( );
	BoidAdvanceFrame( );

	m_flSpeed = AFLOCK_FLY_SPEED;// no delay!
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyer::BoidAdvanceFrame ( void )
{
	float flapspeed = ( m_flSpeed - m_flTempVar ) / AFLOCK_ACCELERATE;
	m_flTempVar = m_flTempVar * .8 + m_flSpeed * .2;

	if (flapspeed < 0) flapspeed = -flapspeed;
	if (flapspeed < 0.25) flapspeed = 0.25;
	if (flapspeed > 1.9) flapspeed = 1.9;

	GetEngineObject()->SetPlaybackRate(flapspeed);

	QAngle angVel = GetLocalAngularVelocity();

	// lean
	angVel.x = -GetEngineObject()->GetAbsAngles().x + flapspeed * 5;

	// bank
	angVel.z = -GetEngineObject()->GetAbsAngles().z + angVel.y;

	SetLocalAngularVelocity( angVel );

	// pev->framerate		= flapspeed;
	StudioFrameAdvance();
}

//=========================================================
// Leader boids use this think every tenth
//=========================================================
void CNPC_FlockingFlyer::FlockLeaderThink( void )
{
	trace_t			tr;
	Vector			vecDist;// used for general measurements
	Vector			vecDir;// used for general measurements
	float			flLeftSide;
	float			flRightSide;
	Vector			vForward, vRight, vUp;
	

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
	
	AngleVectors (GetEngineObject()->GetAbsAngles(), &vForward, &vRight, &vUp );

	// is the way ahead clear?
	if ( !FPathBlocked () )
	{
		// if the boid is turning, stop the trend.
		if ( m_fTurning )
		{
			m_fTurning = FALSE;

			QAngle angVel = GetLocalAngularVelocity();
			angVel.y = 0;
			SetLocalAngularVelocity( angVel );
		}

		m_fPathBlocked = FALSE;

		if ( m_flSpeed <= AFLOCK_FLY_SPEED )
			 m_flSpeed += 5;

		GetEngineObject()->SetAbsVelocity( vForward * m_flSpeed );

		BoidAdvanceFrame( );

		return;
	}
	
	// IF we get this far in the function, the leader's path is blocked!
	m_fPathBlocked = TRUE;

	if ( !m_fTurning)// something in the way and boid is not already turning to avoid
	{
		// measure clearance on left and right to pick the best dir to turn
		UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() + vRight * AFLOCK_CHECK_DIST, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
		vecDist = (tr.endpos - GetEngineObject()->GetAbsOrigin());
		flRightSide = vecDist.Length();

		UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() - vRight * AFLOCK_CHECK_DIST, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
		vecDist = (tr.endpos - GetEngineObject()->GetAbsOrigin());
		flLeftSide = vecDist.Length();

		// turn right if more clearance on right side
		if ( flRightSide > flLeftSide )
		{
			QAngle angVel = GetLocalAngularVelocity();
			angVel.y = -AFLOCK_TURN_RATE;
			SetLocalAngularVelocity( angVel );

			m_fTurning = TRUE;
		}
		// default to left turn :)
		else if ( flLeftSide > flRightSide )
		{
			QAngle angVel = GetLocalAngularVelocity();
			angVel.y = AFLOCK_TURN_RATE;
			SetLocalAngularVelocity( angVel );

			m_fTurning = TRUE;
		}
		else
		{
			// equidistant. Pick randomly between left and right.
			m_fTurning = TRUE;

			QAngle angVel = GetLocalAngularVelocity();

			if ( random->RandomInt( 0, 1 ) == 0 )
			{
				angVel.y = AFLOCK_TURN_RATE;
			}
			else
			{
				angVel.y = -AFLOCK_TURN_RATE;
			}

			SetLocalAngularVelocity( angVel );
		}
	}

	SpreadFlock( );

	GetEngineObject()->SetAbsVelocity( vForward * m_flSpeed );
	
	// check and make sure we aren't about to plow into the ground, don't let it happen
	UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() - vUp * 16, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
	if (tr.fraction != 1.0 && GetEngineObject()->GetAbsVelocity().z < 0 )
	{
		Vector vecVel = GetEngineObject()->GetAbsVelocity();
		vecVel.z = 0;
		GetEngineObject()->SetAbsVelocity( vecVel );
	}

	// maybe it did, though.
	if (GetEngineObject()->GetFlags() & FL_ONGROUND )
	{
		UTIL_SetOrigin( this, GetEngineObject()->GetAbsOrigin() + Vector ( 0 , 0 , 1 ) );
		Vector vecVel = GetEngineObject()->GetAbsVelocity();
		vecVel.z = 0;
		GetEngineObject()->SetAbsVelocity( vecVel );
	}

	if ( m_flFlockNextSoundTime < gpGlobals->curtime )
	{
//		MakeSound();
		m_flFlockNextSoundTime = gpGlobals->curtime + random->RandomFloat( 1, 3 );
	}

	BoidAdvanceFrame( );
	
	return;
}

//=========================================================
// FBoidPathBlocked - returns TRUE if there is an obstacle ahead
//=========================================================
bool CNPC_FlockingFlyer::FPathBlocked( void )
{
	trace_t			tr;
	Vector			vecDist;// used for general measurements
	Vector			vecDir;// used for general measurements
	bool			fBlocked;
	Vector			vForward, vRight, vUp;

	if ( m_flFakeBlockedTime > gpGlobals->curtime )
	{
		m_flLastBlockedTime = gpGlobals->curtime;
		return TRUE;
	}

	// use VELOCITY, not angles, not all boids point the direction they are flying
	//vecDir = UTIL_VecToAngles( pevBoid->velocity );
	AngleVectors (GetEngineObject()->GetAbsAngles(), &vForward, &vRight, &vUp );

	fBlocked = FALSE;// assume the way ahead is clear

	// check for obstacle ahead
	UTIL_TraceLine(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin() + vForward * AFLOCK_CHECK_DIST, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
	
	if (tr.fraction != 1.0)
	{
		m_flLastBlockedTime = gpGlobals->curtime;
		fBlocked = TRUE;
	}

	// extra wide checks
	UTIL_TraceLine(GetEngineObject()->GetAbsOrigin() + vRight * 12, GetEngineObject()->GetAbsOrigin() + vRight * 12 + vForward * AFLOCK_CHECK_DIST, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
	
	if (tr.fraction != 1.0)
	{
		m_flLastBlockedTime = gpGlobals->curtime;
		fBlocked = TRUE;
	}

	UTIL_TraceLine(GetEngineObject()->GetAbsOrigin() - vRight * 12, GetEngineObject()->GetAbsOrigin() - vRight * 12 + vForward * AFLOCK_CHECK_DIST, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
	
	if (tr.fraction != 1.0)
	{
		m_flLastBlockedTime = gpGlobals->curtime;
		fBlocked = TRUE;
	}

	if ( !fBlocked && gpGlobals->curtime - m_flLastBlockedTime > 6 )
	{
		// not blocked, and it's been a few seconds since we've actually been blocked.
		m_flFakeBlockedTime = gpGlobals->curtime + random->RandomInt(1, 3); 
	}

	return	fBlocked;
}

//=========================================================
// Searches for boids that are too close and pushes them away
//=========================================================
void CNPC_FlockingFlyer::SpreadFlock( )
{
	Vector		vecDir;
	float		flSpeed;// holds vector magnitude while we fiddle with the direction
	
	CNPC_FlockingFlyer *pList = m_pSquadLeader;
	while ( pList )
	{
		if ( pList != this && (GetEngineObject()->GetAbsOrigin() - pList->GetEngineObject()->GetAbsOrigin() ).Length() <= AFLOCK_TOO_CLOSE )
		{
			// push the other away
			vecDir = ( pList->GetEngineObject()->GetAbsOrigin() - GetEngineObject()->GetAbsOrigin() );
			VectorNormalize( vecDir );

			// store the magnitude of the other boid's velocity, and normalize it so we
			// can average in a course that points away from the leader.
			flSpeed = pList->GetEngineObject()->GetAbsVelocity().Length();

			Vector vecVel = pList->GetEngineObject()->GetAbsVelocity();
			VectorNormalize( vecVel );
			pList->GetEngineObject()->SetAbsVelocity( ( vecVel + vecDir ) * 0.5 * flSpeed );
		}

		pList = pList->m_pSquadNext;
	}
}

//=========================================================
// Alters the caller's course if he's too close to others 
//
// This function should **ONLY** be called when Caller's velocity is normalized!!
//=========================================================
void CNPC_FlockingFlyer::SpreadFlock2 ( )
{
	Vector		vecDir;
	
	CNPC_FlockingFlyer *pList = m_pSquadLeader;

	while ( pList )
	{
		if ( pList != this && (GetEngineObject()->GetAbsOrigin() - pList->GetEngineObject()->GetAbsOrigin() ).Length() <= AFLOCK_TOO_CLOSE )
		{
			vecDir = (GetEngineObject()->GetAbsOrigin() - pList->GetEngineObject()->GetAbsOrigin() );
			VectorNormalize( vecDir );

			GetEngineObject()->SetAbsVelocity( (GetEngineObject()->GetAbsVelocity() + vecDir ) );
		}

		pList = pList->m_pSquadNext;
	}
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyer::MakeSound( void )
{
	if ( m_flAlertTime > gpGlobals->curtime )
	{
		CPASAttenuationFilter filter1( this );

		// make agitated sounds
		g_pSoundEmitterSystem->EmitSound( filter1, entindex(), "FlockingFlyer.Alert" );
		return;
	}

	// make normal sound
	CPASAttenuationFilter filter2( this );

	g_pSoundEmitterSystem->EmitSound( filter2, entindex(), "FlockingFlyer.Idle" );
}

//=========================================================
// follower boids execute this code when flocking
//=========================================================
void CNPC_FlockingFlyer::FlockFollowerThink( void )	
{
	Vector			vecDist;
	Vector			vecDir;
	Vector			vecDirToLeader;
	float			flDistToLeader;

	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

	if ( IsLeader() || !InSquad() )
	{
		// the leader has been killed and this flyer suddenly finds himself the leader. 
		SetThink ( &CNPC_FlockingFlyer::FlockLeaderThink );
		return;
	}

	vecDirToLeader = ( m_pSquadLeader->GetEngineObject()->GetAbsOrigin() - GetEngineObject()->GetAbsOrigin() );
	flDistToLeader = vecDirToLeader.Length();
	
	// match heading with leader
	GetEngineObject()->SetAbsAngles( m_pSquadLeader->GetEngineObject()->GetAbsAngles() );

	//
	// We can see the leader, so try to catch up to it
	//
	if ( FInViewCone ( m_pSquadLeader ) )
	{
		// if we're too far away, speed up
		if ( flDistToLeader > AFLOCK_TOO_FAR )
		{
			m_flGoalSpeed = m_pSquadLeader->GetEngineObject()->GetAbsVelocity().Length() * 1.5;
		}

		// if we're too close, slow down
		else if ( flDistToLeader < AFLOCK_TOO_CLOSE )
		{
			m_flGoalSpeed = m_pSquadLeader->GetEngineObject()->GetAbsVelocity().Length() * 0.5;
		}
	}
	else
	{
		// wait up! the leader isn't out in front, so we slow down to let him pass
		m_flGoalSpeed = m_pSquadLeader->GetEngineObject()->GetAbsVelocity().Length() * 0.5;
	}

	SpreadFlock2();

	Vector vecVel = GetEngineObject()->GetAbsVelocity();
	m_flSpeed = vecVel.Length();
	VectorNormalize( vecVel );

	// if we are too far from leader, average a vector towards it into our current velocity
	if ( flDistToLeader > AFLOCK_TOO_FAR )
	{
		VectorNormalize( vecDirToLeader );
		vecVel = (vecVel + vecDirToLeader) * 0.5; 	
	}

	// clamp speeds and handle acceleration
	if ( m_flGoalSpeed > AFLOCK_FLY_SPEED * 2 )
	{
		m_flGoalSpeed  = AFLOCK_FLY_SPEED * 2;
	}

	if ( m_flSpeed < m_flGoalSpeed )
	{
		m_flSpeed += AFLOCK_ACCELERATE;
	}
	else if ( m_flSpeed > m_flGoalSpeed )
	{
		m_flSpeed -= AFLOCK_ACCELERATE;
	}

	GetEngineObject()->SetAbsVelocity( vecVel * m_flSpeed );

	BoidAdvanceFrame( );
}

//=========================================================
//=========================================================
void CNPC_FlockingFlyer::Event_Killed( const CTakeDamageInfo &info )
{
	CNPC_FlockingFlyer *pSquad;
	
	pSquad = (CNPC_FlockingFlyer *)m_pSquadLeader;

	while ( pSquad )
	{
		pSquad->m_flAlertTime = gpGlobals->curtime + 15;
		pSquad = (CNPC_FlockingFlyer *)pSquad->m_pSquadNext;
	}

	if ( m_pSquadLeader )
	{
		m_pSquadLeader->SquadRemove( this );
	}

	m_lifeState = LIFE_DEAD;

	GetEngineObject()->SetPlaybackRate(0);
	GetEngineObject()->IncrementInterpolationFrame();

	GetEngineObject()->SetSize( Vector(0,0,0), Vector(0,0,0) );
	GetEngineObject()->SetMoveType( MOVETYPE_FLYGRAVITY );

	SetThink ( &CNPC_FlockingFlyer::FallHack );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
}

void CNPC_FlockingFlyer::FallHack( void )
{
	if (GetEngineObject()->GetFlags() & FL_ONGROUND )
	{
		IServerEntity *groundentity = EntityList()->GetBaseEntity(GetEngineObject()->GetGroundEntity()->entindex());

		if ( !FClassnameIs ( groundentity, "worldspawn" ) )
		{
			GetEngineObject()->SetGroundEntity( NULL );
			GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
		}
		else
		{
			GetEngineObject()->SetAbsVelocity( Vector( 0, 0, 0 ) );
			SetThink( NULL );
		}
	}
}
