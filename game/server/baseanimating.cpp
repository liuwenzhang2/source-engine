//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Base class for all animating characters and objects.
//
//=============================================================================//

#include "cbase.h"
#include "baseanimating.h"
#include "animation.h"
#include "activitylist.h"
#include "studio.h"
#include "bone_setup.h"
#include "mathlib/mathlib.h"
#include "model_types.h"
#include "datacache/imdlcache.h"
#include "physics.h"
#include "ndebugoverlay.h"
#include "tier1/strtools.h"
#include "npcevent.h"
#include "isaverestore.h"
#include "KeyValues.h"
#include "tier0/vprof.h"
#include "EntityFlame.h"
#include "EntityDissolve.h"
#include "ai_basenpc.h"
#include "physics_prop_ragdoll.h"
#include "datacache/idatacache.h"
#include "smoke_trail.h"
#include "props.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar ai_sequence_debug;

class CIKSaveRestoreOps : public CClassPtrSaveRestoreOps
{
	// save data type interface
	void Save( const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave )
	{
		Assert( fieldInfo.pTypeDesc->fieldSize == 1 );
		CIKContext **pIK = (CIKContext **)fieldInfo.pField;
		bool bHasIK = (*pIK) != 0;
		pSave->WriteBool( &bHasIK );
	}

	void Restore( const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore )
	{
		Assert( fieldInfo.pTypeDesc->fieldSize == 1 );
		CIKContext **pIK = (CIKContext **)fieldInfo.pField;

		bool bHasIK;
		pRestore->ReadBool( &bHasIK );
		*pIK = (bHasIK) ? new CIKContext : NULL;
	}
};



static CIKSaveRestoreOps s_IKSaveRestoreOp;


BEGIN_DATADESC( CBaseAnimating )

	//DEFINE_FIELD( m_flGroundSpeed, FIELD_FLOAT ),
	//DEFINE_FIELD( m_flLastEventCheck, FIELD_TIME ),
	//DEFINE_FIELD( m_bSequenceFinished, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_bSequenceLoops, FIELD_BOOLEAN ),

//	DEFINE_FIELD( m_nForceBone, FIELD_INTEGER ),
//	DEFINE_FIELD( m_vecForce, FIELD_VECTOR ),

	//DEFINE_INPUT( m_nSkin, FIELD_INTEGER, "skin" ),
	//DEFINE_KEYFIELD( m_nBody, FIELD_INTEGER, "body" ),
	//DEFINE_INPUT( m_nBody, FIELD_INTEGER, "SetBodyGroup" ),
	//DEFINE_KEYFIELD( m_nHitboxSet, FIELD_INTEGER, "hitboxset" ),
	//DEFINE_KEYFIELD( m_nSequence, FIELD_INTEGER, "sequence" ),
	//DEFINE_ARRAY( m_flPoseParameter, FIELD_FLOAT, NUM_POSEPAREMETERS ),
	//DEFINE_ARRAY( m_flEncodedController,	FIELD_FLOAT, CBaseAnimating::NUM_BONECTRLS ),
	//DEFINE_KEYFIELD( m_flPlaybackRate, FIELD_FLOAT, "playbackrate" ),
	//DEFINE_KEYFIELD( m_flCycle, FIELD_FLOAT, "cycle" ),
//	DEFINE_FIELD( m_flIKGroundContactTime, FIELD_TIME ),
//	DEFINE_FIELD( m_flIKGroundMinHeight, FIELD_FLOAT ),
//	DEFINE_FIELD( m_flIKGroundMaxHeight, FIELD_FLOAT ),
//	DEFINE_FIELD( m_flEstIkFloor, FIELD_FLOAT ),
//	DEFINE_FIELD( m_flEstIkOffset, FIELD_FLOAT ),
//	DEFINE_FIELD( m_pStudioHdr, IStudioHdr ),
//	DEFINE_FIELD( m_StudioHdrInitLock, CThreadFastMutex ),
//	DEFINE_FIELD( m_BoneSetupMutex, CThreadFastMutex ),
	DEFINE_CUSTOM_FIELD( m_pIk, &s_IKSaveRestoreOp ),
	DEFINE_FIELD( m_iIKCounter, FIELD_INTEGER ),
	//DEFINE_FIELD( m_bClientSideAnimation, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_bClientSideFrameReset, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_nNewSequenceParity, FIELD_INTEGER ),
	//DEFINE_FIELD( m_nResetEventsParity, FIELD_INTEGER ),
	//DEFINE_FIELD( m_nMuzzleFlashParity, FIELD_CHARACTER ),



	//DEFINE_FIELD( m_flModelScale, FIELD_FLOAT ),

 // DEFINE_FIELD( m_boneCacheHandle, memhandle_t ),


	DEFINE_INPUTFUNC( FIELD_VOID, "BecomeRagdoll", InputBecomeRagdoll ),

	//DEFINE_OUTPUT( m_OnIgnite, "OnIgnite" ),



	//DEFINE_KEYFIELD( m_flModelScale, FIELD_FLOAT, "modelscale" ),
	DEFINE_INPUTFUNC( FIELD_VECTOR, "SetModelScale", InputSetModelScale ),

	DEFINE_FIELD( m_fBoneCacheFlags, FIELD_SHORT ),

END_DATADESC()


// SendTable stuff.
IMPLEMENT_SERVERCLASS_ST(CBaseAnimating, DT_BaseAnimating)

END_SEND_TABLE()


CBaseAnimating::CBaseAnimating()
{


	//m_bResetSequenceInfoOnLoad = false;
	m_pIk = NULL;
	m_iIKCounter = 0;

	//InitStepHeightAdjust();

	// initialize anim clock
	m_flPrevAnimTime = gpGlobals->curtime;

	m_boneCacheHandle = 0;
	m_fBoneCacheFlags = 0;
}

void CBaseAnimating::PostConstructor(const char* szClassname, int iForceEdictIndex) {
	BaseClass::PostConstructor(szClassname, iForceEdictIndex);
	GetEngineObject()->SetAnimTime(gpGlobals->curtime);
}

void CBaseAnimating::UpdateOnRemove(void) {
	BaseClass::UpdateOnRemove();
}


CBaseAnimating::~CBaseAnimating()
{
	Studio_DestroyBoneCache( m_boneCacheHandle );
	delete m_pIk;
}

void CBaseAnimating::Precache()
{
#if !defined( TF_DLL )
	// Anything derived from this class can potentially burn - true, but do we want it to!
	PrecacheParticleSystem( "burning_character" );
#endif

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Activate!
//-----------------------------------------------------------------------------
void CBaseAnimating::Activate()
{
	BaseClass::Activate();

	// Scaled physics objects (re)create their physics here
	if ( GetEngineObject()->GetModelScale() != 1.0f && VPhysicsGetObject() )
	{	
		// sanity check to make sure 'm_flModelScale' is in sync with the 
		Assert( m_flModelScale > 0.0f );

		UTIL_CreateScaledPhysObject( this, GetEngineObject()->GetModelScale() );
	}
}


//-----------------------------------------------------------------------------
// Force our lighting origin to be trasmitted
//-----------------------------------------------------------------------------
void CBaseAnimating::SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways )
{
	// Are we already marked for transmission?
	if ( pInfo->m_pTransmitEdict->Get( entindex() ) )
		return;

	BaseClass::SetTransmit( pInfo, bAlways );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CBaseAnimating::Restore( IRestore &restore )
{
	int result = BaseClass::Restore( restore );

	return result;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseAnimating::OnRestore()
{
	BaseClass::OnRestore();

	if (GetEngineObject()->GetSequence() != -1 && GetEngineObject()->GetModelPtr() && !IsValidSequence(GetEngineObject()->GetSequence() ) )
		GetEngineObject()->SetSequence(0);

	PopulatePoseParameters();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseAnimating::Spawn()
{
	BaseClass::Spawn();
	InitStepHeightAdjust();
}



#define MAX_ANIMTIME_INTERVAL 0.2f

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CBaseAnimating::GetAnimTimeInterval( void ) const
{
	float flInterval;
	if (GetEngineObject()->GetAnimTime() < gpGlobals->curtime)
	{
		// estimate what it'll be this frame
		flInterval = clamp( gpGlobals->curtime - GetEngineObject()->GetAnimTime(), 0.f, MAX_ANIMTIME_INTERVAL);
	}
	else
	{
		// report actual
		flInterval = clamp(GetEngineObject()->GetAnimTime() - m_flPrevAnimTime, 0.f, MAX_ANIMTIME_INTERVAL);
	}
	return flInterval;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseAnimating::StudioFrameAdvanceInternal( IStudioHdr *pStudioHdr, float flCycleDelta )
{
	float flNewCycle = GetEngineObject()->GetCycle() + flCycleDelta;
	if (flNewCycle < 0.0 || flNewCycle >= 1.0) 
	{
		if (GetEngineObject()->SequenceLoops())
		{
			flNewCycle -= (int)(flNewCycle);
		}
		else
		{
			flNewCycle = (flNewCycle < 0.0f) ? 0.0f : 1.0f;
		}
		GetEngineObject()->SetSequenceFinished(true);	// just in case it wasn't caught in GetEvents
	}
	else if (flNewCycle > GetLastVisibleCycle( pStudioHdr, GetEngineObject()->GetSequence() ))
	{
		GetEngineObject()->SetSequenceFinished(true);
	}

	GetEngineObject()->SetCycle( flNewCycle );

	/*
	if (!IsPlayer())
		Msg("%s %6.3f : %6.3f %6.3f (%.3f) %.3f\n", 
			GetClassname(), gpGlobals->curtime, 
			m_flAnimTime.Get(), m_flPrevAnimTime, flInterval, GetCycle() );
	*/
 
	GetEngineObject()->SetGroundSpeed(GetEngineObject()->GetSequenceGroundSpeed( pStudioHdr, GetEngineObject()->GetSequence() ) * GetEngineObject()->GetModelScale());

	// Msg("%s : %s : %5.1f\n", GetClassname(), GetSequenceName( GetSequence() ), GetCycle() );
	GetEngineObject()->InvalidatePhysicsRecursive( ANIMATION_CHANGED );

	InvalidateBoneCacheIfOlderThan( 0 );
}

void CBaseAnimating::InvalidateBoneCacheIfOlderThan( float deltaTime )
{
	CBoneCache *pcache = Studio_GetBoneCache( m_boneCacheHandle );
	if ( !pcache || !pcache->IsValid( gpGlobals->curtime, deltaTime ) )
	{
		InvalidateBoneCache();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseAnimating::StudioFrameAdvanceManual( float flInterval )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if ( !pStudioHdr )
		return;

	GetEngineObject()->UpdateModelScale();
	GetEngineObject()->SetAnimTime(gpGlobals->curtime);
	m_flPrevAnimTime = GetEngineObject()->GetAnimTime() - flInterval;
	float flCycleRate = GetEngineObject()->GetSequenceCycleRate( pStudioHdr, GetEngineObject()->GetSequence() ) * GetEngineObject()->GetPlaybackRate();
	StudioFrameAdvanceInternal(GetEngineObject()->GetModelPtr(), flInterval * flCycleRate );
}


//=========================================================
// StudioFrameAdvance - advance the animation frame up some interval (default 0.1) into the future
//=========================================================
void CBaseAnimating::StudioFrameAdvance()
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();

	if ( !pStudioHdr || !pStudioHdr->SequencesAvailable() )
	{
		return;
	}

	GetEngineObject()->UpdateModelScale();

	if ( !m_flPrevAnimTime )
	{
		m_flPrevAnimTime = GetEngineObject()->GetAnimTime();
	}

	// Time since last animation
	float flInterval = gpGlobals->curtime - GetEngineObject()->GetAnimTime();
	flInterval = clamp( flInterval, 0.f, MAX_ANIMTIME_INTERVAL );

	//Msg( "%i %s interval %f\n", entindex(), GetClassname(), flInterval );
	if (flInterval <= 0.001f)
	{
		// Msg("%s : %s : %5.3f (skip)\n", GetClassname(), GetSequenceName( GetSequence() ), GetCycle() );
		return;
	}

	// Latch prev
	m_flPrevAnimTime = GetEngineObject()->GetAnimTime();
	// Set current
	GetEngineObject()->SetAnimTime(gpGlobals->curtime);

	// Drive cycle
	float flCycleRate = GetEngineObject()->GetSequenceCycleRate( pStudioHdr, GetEngineObject()->GetSequence() ) * GetEngineObject()->GetPlaybackRate();

	StudioFrameAdvanceInternal( pStudioHdr, flInterval * flCycleRate );

	if (ai_sequence_debug.GetBool() == true && m_debugOverlays & OVERLAY_NPC_SELECTED_BIT)
	{
		Msg("%5.2f : %s : %s : %5.3f\n", gpGlobals->curtime, GetClassname(), GetSequenceName(GetEngineObject()->GetSequence() ), GetEngineObject()->GetCycle() );
	}
}


//-----------------------------------------------------------------------------
// Purpose: SetModelScale input handler
//-----------------------------------------------------------------------------
void CBaseAnimating::InputSetModelScale( inputdata_t &inputdata )
{
	Vector vecScale;
	inputdata.value.Vector3D( vecScale );

	GetEngineObject()->SetModelScale( vecScale.x, vecScale.y );
}


//=========================================================
// SelectWeightedSequence
//=========================================================
int CBaseAnimating::SelectWeightedSequence ( Activity activity )
{
	Assert( activity != ACT_INVALID );
	Assert( GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->SelectWeightedSequence( activity, GetEngineObject()->GetSequence(), SharedRandomSelect);
}


int CBaseAnimating::SelectWeightedSequence ( Activity activity, int curSequence )
{
	Assert( activity != ACT_INVALID );
	Assert( GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->SelectWeightedSequence( activity, curSequence, SharedRandomSelect);
}

//=========================================================
// ResetActivityIndexes
//=========================================================
void CBaseAnimating::ResetActivityIndexes ( void )
{
	Assert( GetModelPtr() );
	GetEngineObject()->GetModelPtr()->ResetActivityIndexes();
}

//=========================================================
// ResetEventIndexes
//=========================================================
void CBaseAnimating::ResetEventIndexes ( void )
{
	Assert( GetModelPtr() );
	GetEngineObject()->GetModelPtr()->ResetEventIndexes();
}

//=========================================================
// LookupHeaviestSequence
//
// Get sequence with highest 'weight' for this activity
//
//=========================================================
int CBaseAnimating::SelectHeaviestSequence ( Activity activity )
{
	Assert( GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->SelectHeaviestSequence( activity );
}


//-----------------------------------------------------------------------------
// Purpose: Looks up an activity by name.
// Input  : label - Name of the activity, ie "ACT_IDLE".
// Output : Returns the activity ID or ACT_INVALID.
//-----------------------------------------------------------------------------
int CBaseAnimating::LookupActivity( const char *label )
{
	Assert( GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->LookupActivity( label );
}

//=========================================================
//=========================================================
int CBaseAnimating::LookupSequence( const char *label )
{
	Assert( GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->LookupSequence( label, SharedRandomSelect);
}



//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
KeyValues *CBaseAnimating::GetSequenceKeyValues( int iSequence )
{
	const char *szText = GetEngineObject()->GetModelPtr()->Studio_GetKeyValueText( iSequence );

	if (szText)
	{
		KeyValues *seqKeyValues = new KeyValues("");
		if ( seqKeyValues->LoadFromBuffer( modelinfo->GetModelName( GetEngineObject()->GetModel() ), szText ) )
		{
			return seqKeyValues;
		}
		seqKeyValues->deleteThis();
	}
	return NULL;
}









//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : char
//-----------------------------------------------------------------------------
const char *CBaseAnimating::GetSequenceName( int iSequence )
{
	if( iSequence == -1 )
	{
		return "Not Found!";
	}

	if ( !GetEngineObject()->GetModelPtr() )
		return "No model!";

	return GetEngineObject()->GetModelPtr()->GetSequenceName( iSequence );
}
//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : char
//-----------------------------------------------------------------------------
const char *CBaseAnimating::GetSequenceActivityName( int iSequence )
{
	if( iSequence == -1 )
	{
		return "Not Found!";
	}

	if ( !GetEngineObject()->GetModelPtr() )
		return "No model!";

	return GetEngineObject()->GetModelPtr()->GetSequenceActivityName( iSequence );
}

//-----------------------------------------------------------------------------
// Purpose: Make this a client-side simulated entity
// Input  : force - vector of force to be exerted in the physics simulation
//			forceBone - bone to exert force upon
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseAnimating::BecomeRagdollOnClient( const Vector &force )
{
	// If this character has a ragdoll animation, turn it over to the physics system
	if ( CanBecomeRagdoll() ) 
	{
		VPhysicsDestroyObject();
		GetEngineObject()->AddSolidFlags( FSOLID_NOT_SOLID );
		m_nRenderFX = kRenderFxRagdoll;
		
		// Have to do this dance because m_vecForce is a network vector
		// and can't be sent to ClampRagdollForce as a Vector *
		Vector vecClampedForce;
		ClampRagdollForce( force, &vecClampedForce );
		GetEngineObject()->SetVecForce(vecClampedForce);

		GetEngineObject()->SetParent( NULL );

		GetEngineObject()->AddFlag( FL_TRANSRAGDOLL );

		GetEngineObject()->SetMoveType( MOVETYPE_NONE );
		//UTIL_SetSize( this, vec3_origin, vec3_origin );
		SetThink( NULL );
	
		GetEngineObject()->SetNextThink( gpGlobals->curtime + 2.0f );
		//If we're here, then we can vanish safely
		SetThink( &CBaseEntity::SUB_Remove );

		// Remove our flame entity if it's attached to us
		CEntityFlame *pFireChild = dynamic_cast<CEntityFlame *>( GetEffectEntity() );
		if ( pFireChild )
		{
			pFireChild->SetThink( &CBaseEntity::SUB_Remove );
			pFireChild->GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
		}

		return true;
	}
	return false;
}

bool CBaseAnimating::IsRagdoll()
{
	return ( m_nRenderFX == kRenderFxRagdoll ) ? true : false;
}

bool CBaseAnimating::CanBecomeRagdoll( void ) 
{
	MDLCACHE_CRITICAL_SECTION();
	int ragdollSequence = SelectWeightedSequence( ACT_DIERAGDOLL );

	//Can't cause we don't have a ragdoll sequence.
	if ( ragdollSequence == ACTIVITY_NOT_AVAILABLE )
		 return false;
	
	if (GetEngineObject()->GetFlags() & FL_TRANSRAGDOLL )
		 return false;

	return true;
}



//=========================================================
//=========================================================
bool CBaseAnimating::IsValidSequence( int iSequence )
{
	Assert( GetModelPtr() );
	IStudioHdr* pstudiohdr = GetEngineObject()->GetModelPtr( );
	if (iSequence < 0 || iSequence >= pstudiohdr->GetNumSeq())
	{
		return false;
	}
	return true;
}








float CBaseAnimating::GetLastVisibleCycle( IStudioHdr *pStudioHdr, int iSequence )
{
	if ( !pStudioHdr )
	{
		DevWarning( 2, "CBaseAnimating::LastVisibleCycle( %d ) NULL pstudiohdr on %s!\n", iSequence, GetClassname() );
		return 1.0;
	}

	if (!(pStudioHdr->GetSequenceFlags( iSequence ) & STUDIO_LOOPING))
	{
		return 1.0f - (pStudioHdr->pSeqdesc( iSequence ).fadeouttime) * GetEngineObject()->GetSequenceCycleRate( iSequence ) * GetEngineObject()->GetPlaybackRate();
	}
	else
	{
		return 1.0;
	}
}




float CBaseAnimating::GetIdealSpeed( ) const
{
	return GetEngineObject()->GetGroundSpeed();
}

float CBaseAnimating::GetIdealAccel( ) const
{
	// return ideal max velocity change over 1 second.
	// tuned for run-walk range of humans
	return GetIdealSpeed() + 50;
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if the given sequence has the anim event, false if not.
// Input  : nSequence - sequence number to check
//			nEvent - anim event number to look for
//-----------------------------------------------------------------------------
bool CBaseAnimating::HasAnimEvent( int nSequence, int nEvent )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr();
	if ( !pstudiohdr )
	{
		return false;
	}

  	animevent_t event;

	int index = 0;
	while ( ( index = pstudiohdr->GetAnimationEvent( nSequence, &event, 0.0f, 1.0f, index ,gpGlobals->curtime) ) != 0 )
	{
		if ( event.event == nEvent )
		{
			return true;
		}
	}

	return false;
}


//=========================================================
// DispatchAnimEvents
//=========================================================
void CBaseAnimating::DispatchAnimEvents ( CBaseAnimating *eventHandler )
{
	// don't fire events if the framerate is 0
	if (GetEngineObject()->GetPlaybackRate() == 0.0)
		return;

	animevent_t	event;

	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr( );

	if ( !pstudiohdr )
	{
		Assert(!"CBaseAnimating::DispatchAnimEvents: model missing");
		return;
	}

	if ( !pstudiohdr->SequencesAvailable() )
	{
		return;
	}

	// skip this altogether if there are no events
	if (pstudiohdr->pSeqdesc(GetEngineObject()->GetSequence() ).numevents == 0)
	{
		return;
	}

	// look from when it last checked to some short time in the future	
	float flCycleRate = GetEngineObject()->GetSequenceCycleRate(GetEngineObject()->GetSequence() ) * GetEngineObject()->GetPlaybackRate();
	float flStart = GetEngineObject()->GetLastEventCheck();
	float flEnd = GetEngineObject()->GetCycle();

	if (!GetEngineObject()->SequenceLoops() && GetEngineObject()->IsSequenceFinished())
	{
		flEnd = 1.01f;
	}
	GetEngineObject()->SetLastEventCheck(flEnd);

	/*
	if (m_debugOverlays & OVERLAY_NPC_SELECTED_BIT)
	{
		Msg( "%s:%s : checking %.2f %.2f (%d)\n", STRING(GetModelName()), pstudiohdr->pSeqdesc( GetSequence() ).pszLabel(), flStart, flEnd, m_bSequenceFinished );
	}
	*/

	// FIXME: does not handle negative framerates!
	int index = 0;
	while ( (index = pstudiohdr->GetAnimationEvent(GetEngineObject()->GetSequence(), &event, flStart, flEnd, index ,gpGlobals->curtime) ) != 0 )
	{
		event.pSource = this;
		// calc when this event should happen
		if (flCycleRate > 0.0)
		{
			float flCycle = event.cycle;
			if (flCycle > GetEngineObject()->GetCycle())
			{
				flCycle = flCycle - 1.0;
			}
			event.eventtime = GetEngineObject()->GetAnimTime() + (flCycle - GetEngineObject()->GetCycle()) / flCycleRate + GetAnimTimeInterval();
		}

		/*
		if (m_debugOverlays & OVERLAY_NPC_SELECTED_BIT)
		{
			Msg( "dispatch %i (%i) cycle %f event cycle %f cyclerate %f\n", 
				(int)(index - 1), 
				(int)event.event, 
				(float)GetCycle(), 
				(float)event.cycle, 
				(float)flCycleRate );
		}
		*/
		eventHandler->HandleAnimEvent( &event );

		// FAILSAFE:
		// If HandleAnimEvent has somehow reset my internal pointer
		// to IStudioHdr to something other than it was when we entered 
		// this function, we will crash on the next call to GetAnimationEvent
		// because pstudiohdr no longer points at something valid. 
		// So, catch this case, complain vigorously, and bail out of
		// the loop.
		IStudioHdr *pNowStudioHdr = GetEngineObject()->GetModelPtr();
		if ( pNowStudioHdr != pstudiohdr )
		{
			AssertMsg2(false, "%s has changed its model while processing AnimEvents on sequence %d. Aborting dispatch.\n", GetDebugName(), GetEngineObject()->GetSequence() );
			Warning( "%s has changed its model while processing AnimEvents on sequence %d. Aborting dispatch.\n", GetDebugName(), GetEngineObject()->GetSequence() );
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseAnimating::HandleAnimEvent( animevent_t *pEvent )
{
	if ((pEvent->type & AE_TYPE_NEWEVENTSYSTEM) && (pEvent->type & AE_TYPE_SERVER))
	{
		if ( pEvent->event == AE_SV_PLAYSOUND )
		{
			const char* soundname = pEvent->options;
			CPASAttenuationFilter filter(this, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
			return;
		}
		else if ( pEvent->event == AE_RAGDOLL )
		{
			// Convert to ragdoll immediately
			BecomeRagdollOnClient( vec3_origin );
			return;
		}
#ifdef HL2_EPISODIC
		else if ( pEvent->event == AE_SV_DUSTTRAIL )
		{
			char szAttachment[128];
			float flDuration;
			float flSize;
			if (sscanf( pEvent->options, "%s %f %f", szAttachment, &flDuration, &flSize ) == 3)
			{
				CHandle<DustTrail>	hDustTrail;

				hDustTrail = DustTrail::CreateDustTrail();

				if( hDustTrail )
				{
					hDustTrail->m_SpawnRate = 4;    // Particles per second
					hDustTrail->m_ParticleLifetime = 1.5;   // Lifetime of each particle, In seconds
					hDustTrail->m_Color.Init(0.5f, 0.46f, 0.44f);
					hDustTrail->m_StartSize = flSize;
					hDustTrail->m_EndSize = hDustTrail->m_StartSize * 8;
					hDustTrail->m_SpawnRadius = 3;    // Each particle randomly offset from the center up to this many units
					hDustTrail->m_MinSpeed = 4;    // u/sec
					hDustTrail->m_MaxSpeed = 10;    // u/sec
					hDustTrail->m_Opacity = 0.5f;  
					hDustTrail->SetLifetime(flDuration);  // Lifetime of the spawner, in seconds
					hDustTrail->m_StopEmitTime = gpGlobals->curtime + flDuration;
					hDustTrail->GetEngineObject()->SetParent( this->GetEngineObject(), LookupAttachment( szAttachment ) );
					hDustTrail->GetEngineObject()->SetLocalOrigin( vec3_origin );
				}
			}
			else
			{
				DevWarning( 1, "%s unable to parse AE_SV_DUSTTRAIL event \"%s\"\n", STRING(GetEngineObject()->GetModelName() ), pEvent->options );
			}

			return;
		}
#endif
	}

	// Failed to find a handler
	const char *pName = mdlcache->EventList_NameForIndex( pEvent->event );
	if ( pName)
	{
		DevWarning( 1, "Unhandled animation event %s for %s\n", pName, GetClassname() );
	}
	else
	{
		DevWarning( 1, "Unhandled animation event %d for %s\n", pEvent->event, GetClassname() );
	}
}

// SetPoseParamater()





//=========================================================
//=========================================================
bool CBaseAnimating::HasPoseParameter( int iSequence, const char *szName )
{
	int iParameter = GetEngineObject()->LookupPoseParameter( szName );
	if (iParameter == -1)
	{
		return false;
	}

	return HasPoseParameter( iSequence, iParameter );
}

//=========================================================
//=========================================================
bool CBaseAnimating::HasPoseParameter( int iSequence, int iParameter )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr( );

	if ( !pstudiohdr )
	{
		return false;
	}

	if ( !pstudiohdr->SequencesAvailable() )
	{
		return false;
	}

	if (iSequence < 0 || iSequence >= pstudiohdr->GetNumSeq())
	{
		return false;
	}

	mstudioseqdesc_t &seqdesc = pstudiohdr->pSeqdesc( iSequence );
	if (pstudiohdr->GetSharedPoseParameter( iSequence, seqdesc.paramindex[0] ) == iParameter || 
		pstudiohdr->GetSharedPoseParameter( iSequence, seqdesc.paramindex[1] ) == iParameter)
	{
		return true;
	}
	return false;
}


//=========================================================
// Each class that wants to use pose parameters should populate
// static variables in this entry point, rather than calling
// GetPoseParameter(const char*) every time you want to adjust
// an animation.
//
// Make sure to call BaseClass::PopulatePoseParameters() at 
// the *bottom* of your function.
//=========================================================
void	CBaseAnimating::PopulatePoseParameters( void )
{

}

//=========================================================
// Purpose: from input of 75% to 200% of maximum range, rescale smoothly from 75% to 100%
//=========================================================
float CBaseAnimating::EdgeLimitPoseParameter( int iParameter, float flValue, float flBase )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr( );
	if ( !pstudiohdr )
	{
		return flValue;
	}

	if (iParameter < 0 || iParameter >= pstudiohdr->GetNumPoseParameters())
	{
		return flValue;
	}

	const mstudioposeparamdesc_t &Pose = pstudiohdr->pPoseParameter( iParameter );

	if (Pose.loop || Pose.start == Pose.end)
	{
		return flValue;
	}

	return RangeCompressor( flValue, Pose.start, Pose.end, flBase );
}


//-----------------------------------------------------------------------------
// Purpose: Returns index number of a given named bone
// Input  : name of a bone
// Output :	Bone index number or -1 if bone not found
//-----------------------------------------------------------------------------
int CBaseAnimating::LookupBone( const char *szName )
{
	const IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	Assert( pStudioHdr );
	if ( !pStudioHdr )
		return -1;
	return pStudioHdr->Studio_BoneIndexByName( szName );
}


//=========================================================
//=========================================================
void CBaseAnimating::GetBonePosition ( int iBone, Vector &origin, QAngle &angles )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetBonePosition: model missing");
		return;
	}

	if (iBone < 0 || iBone >= pStudioHdr->numbones())
	{
		Assert(!"CBaseAnimating::GetBonePosition: invalid bone index");
		return;
	}

	matrix3x4_t bonetoworld;
	GetBoneTransform( iBone, bonetoworld );
	
	MatrixAngles( bonetoworld, angles, origin );
}



//=========================================================
//=========================================================

void CBaseAnimating::GetBoneTransform( int iBone, matrix3x4_t &pBoneToWorld )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );

	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetBoneTransform: model missing");
		return;
	}

	if (iBone < 0 || iBone >= pStudioHdr->numbones())
	{
		Assert(!"CBaseAnimating::GetBoneTransform: invalid bone index");
		return;
	}

	CBoneCache *pcache = GetBoneCache( );

	matrix3x4_t *pmatrix = pcache->GetCachedBone( iBone );

	if ( !pmatrix )
	{
		MatrixCopy(GetEngineObject()->EntityToWorldTransform(), pBoneToWorld );
		return;
	}

	Assert( pmatrix );
	
	// FIXME
	MatrixCopy( *pmatrix, pBoneToWorld );
}

class CTraceFilterSkipNPCs : public CTraceFilterSimple
{
public:
	CTraceFilterSkipNPCs( const IHandleEntity *passentity, int collisionGroup )
		: CTraceFilterSimple( passentity, collisionGroup )
	{
	}

	virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
	{
		if ( CTraceFilterSimple::ShouldHitEntity(pServerEntity, contentsMask) )
		{
			CBaseEntity *pEntity = EntityFromEntityHandle( pServerEntity );
			if ( pEntity->IsNPC() )
				return false;

			return true;
		}
		return false;
	}
};



//-----------------------------------------------------------------------------
// Purpose: Find IK collisions with world
// Input  : 
// Output :	fills out m_pIk targets, calcs floor offset for rendering
//-----------------------------------------------------------------------------

void CBaseAnimating::CalculateIKLocks( float currentTime )
{
	if ( m_pIk )
	{
		Ray_t ray;
		CTraceFilterSkipNPCs traceFilter( this, GetEngineObject()->GetCollisionGroup() );
		Vector up;
		GetVectors( NULL, NULL, &up );
		// FIXME: check number of slots?
		for (int i = 0; i < m_pIk->m_target.Count(); i++)
		{
			trace_t trace;
			CIKTarget *pTarget = &m_pIk->m_target[i];

			if (!pTarget->IsActive())
				continue;

			switch( pTarget->type )
			{
			case IK_GROUND:
				{
					Vector estGround;
					estGround = (pTarget->est.pos - GetEngineObject()->GetAbsOrigin());
					estGround = estGround - (estGround * up) * up;
					estGround = GetEngineObject()->GetAbsOrigin() + estGround + pTarget->est.floor * up;

					Vector p1, p2;
					VectorMA( estGround, pTarget->est.height, up, p1 );
					VectorMA( estGround, -pTarget->est.height, up, p2 );

					float r = MAX(pTarget->est.radius,1);

					// don't IK to other characters
					ray.Init( p1, p2, Vector(-r,-r,0), Vector(r,r,1) );
					enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );

					/*
					debugoverlay->AddBoxOverlay( p1, Vector(-r,-r,0), Vector(r,r,1), QAngle( 0, 0, 0 ), 255, 0, 0, 0, 1.0f );
					debugoverlay->AddBoxOverlay( trace.endpos, Vector(-r,-r,0), Vector(r,r,1), QAngle( 0, 0, 0 ), 255, 0, 0, 0, 1.0f );
					debugoverlay->AddLineOverlay( p1, trace.endpos, 255, 0, 0, 0, 1.0f );
					*/

					if (trace.startsolid)
					{
						ray.Init( pTarget->trace.hip, pTarget->est.pos, Vector(-r,-r,0), Vector(r,r,1) );

						enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );

						p1 = trace.endpos;
						VectorMA( p1, - pTarget->est.height, up, p2 );
						ray.Init( p1, p2, Vector(-r,-r,0), Vector(r,r,1) );

						enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );
					}

					if (!trace.startsolid)
					{
						if (trace.DidHitWorld())
						{
							pTarget->SetPosWithNormalOffset( trace.endpos, trace.plane.normal );
							pTarget->SetNormal( trace.plane.normal );
						}
						else
						{
							pTarget->SetPos( trace.endpos );
							pTarget->SetAngles(GetEngineObject()->GetAbsAngles() );
						}

					}
				}
				break;
			case IK_ATTACHMENT:
				{
					// anything on the server?
				}
				break;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Clear out animation states that are invalidated with Teleport
//-----------------------------------------------------------------------------

void CBaseAnimating::Teleport( const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity )
{
	BaseClass::Teleport( newPosition, newAngles, newVelocity );
	if (m_pIk)
	{
		m_pIk->ClearTargets( );
	}
	InitStepHeightAdjust();
}


//-----------------------------------------------------------------------------
// Purpose: build matrices first from the parent, then from the passed in arrays if the bone doesn't exist on the parent
//-----------------------------------------------------------------------------

void CBaseAnimating::BuildMatricesWithBoneMerge( 
	const IStudioHdr *pStudioHdr,
	const QAngle& angles, 
	const Vector& origin, 
	const Vector pos[MAXSTUDIOBONES],
	const Quaternion q[MAXSTUDIOBONES],
	matrix3x4_t bonetoworld[MAXSTUDIOBONES],
	CBaseAnimating *pParent,
	CBoneCache *pParentCache
	)
{
	IStudioHdr *fhdr = pParent->GetEngineObject()->GetModelPtr();
	mstudiobone_t *pbones = pStudioHdr->pBone( 0 );

	matrix3x4_t rotationmatrix; // model to world transformation
	AngleMatrix( angles, origin, rotationmatrix);

	for ( int i=0; i < pStudioHdr->numbones(); i++ )
	{
		// Now find the bone in the parent entity.
		bool merged = false;
		int parentBoneIndex = fhdr->Studio_BoneIndexByName( pbones[i].pszName() );
		if ( parentBoneIndex >= 0 )
		{
			matrix3x4_t *pMat = pParentCache->GetCachedBone( parentBoneIndex );
			if ( pMat )
			{
				MatrixCopy( *pMat, bonetoworld[ i ] );
				merged = true;
			}
		}

		if ( !merged )
		{
			// If we get down here, then the bone wasn't merged.
			matrix3x4_t bonematrix;
			QuaternionMatrix( q[i], pos[i], bonematrix );

			if (pbones[i].parent == -1) 
			{
				ConcatTransforms (rotationmatrix, bonematrix, bonetoworld[i]);
			} 
			else 
			{
				ConcatTransforms (bonetoworld[pbones[i].parent], bonematrix, bonetoworld[i]);
			}
		}
	}
}

ConVar sv_pvsskipanimation( "sv_pvsskipanimation", "1", FCVAR_ARCHIVE, "Skips SetupBones when npc's are outside the PVS" );
ConVar ai_setupbones_debug( "ai_setupbones_debug", "0", 0, "Shows that bones that are setup every think" );




inline bool CBaseAnimating::CanSkipAnimation( void )
{
	if ( !sv_pvsskipanimation.GetBool() )
		return false;
		
	CAI_BaseNPC *pNPC = MyNPCPointer();
	if ( pNPC && !pNPC->HasCondition( COND_IN_PVS ) && ( m_fBoneCacheFlags & (BCF_NO_ANIMATION_SKIP | BCF_IS_IN_SPAWN) ) == false )
	{
		// If we have a player as a child, then we better setup our bones. If we don't,
		// the PVS will be screwy. 
		return !GetEngineObject()->DoesHavePlayerChild();
	}
	else
	{	
		return false;
	}
}


void CBaseAnimating::SetupBones( matrix3x4_t *pBoneToWorld, int boneMask )
{
	AUTO_LOCK( m_BoneSetupMutex );
	
	VPROF_BUDGET( "CBaseAnimating::SetupBones", VPROF_BUDGETGROUP_SERVER_ANIM );
	
	MDLCACHE_CRITICAL_SECTION();

	Assert( GetModelPtr() );

	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );

	if(!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetSkeleton() without a model");
		return;
	}

	Assert( !GetEngineObject()->IsEFlagSet( EFL_SETTING_UP_BONES ) );

	GetEngineObject()->AddEFlags( EFL_SETTING_UP_BONES );

	Vector pos[MAXSTUDIOBONES];
	Quaternion q[MAXSTUDIOBONES];

	// adjust hit boxes based on IK driven offset
	Vector adjOrigin = GetEngineObject()->GetAbsOrigin() + Vector( 0, 0, m_flEstIkOffset );

	if ( CanSkipAnimation() )
	{
		IBoneSetup boneSetup( pStudioHdr, boneMask, GetEngineObject()->GetPoseParameterArray() );
		boneSetup.InitPose( pos, q );
		// Msg( "%.03f : %s:%s not in pvs\n", gpGlobals->curtime, GetClassname(), GetEntityName().ToCStr() );
	}
	else 
	{
		if ( m_pIk )
		{
			// FIXME: pass this into Studio_BuildMatrices to skip transforms
			CBoneBitList boneComputed;
			m_iIKCounter++;
			m_pIk->Init( pStudioHdr, GetEngineObject()->GetAbsAngles(), adjOrigin, gpGlobals->curtime, m_iIKCounter, boneMask );
			GetSkeleton( pStudioHdr, pos, q, boneMask );

			m_pIk->UpdateTargets( pos, q, pBoneToWorld, boneComputed );
			CalculateIKLocks( gpGlobals->curtime );
			m_pIk->SolveDependencies( pos, q, pBoneToWorld, boneComputed );
		}
		else
		{
			// Msg( "%.03f : %s:%s\n", gpGlobals->curtime, GetClassname(), GetEntityName().ToCStr() );
			GetSkeleton( pStudioHdr, pos, q, boneMask );
		}
	}
	
	CBaseAnimating* pParent = GetEngineObject()->GetMoveParent() ? dynamic_cast<CBaseAnimating*>(GetEngineObject()->GetMoveParent()->GetOuter()) : NULL;
	if ( pParent )
	{
		// We're doing bone merging, so do special stuff here.
		CBoneCache *pParentCache = pParent->GetBoneCache();
		if ( pParentCache )
		{
			BuildMatricesWithBoneMerge( 
				pStudioHdr, 
				GetEngineObject()->GetAbsAngles(),
				adjOrigin, 
				pos, 
				q, 
				pBoneToWorld, 
				pParent,
				pParentCache );
			
			GetEngineObject()->RemoveEFlags( EFL_SETTING_UP_BONES );
			if (ai_setupbones_debug.GetBool())
			{
				DrawRawSkeleton( pBoneToWorld, boneMask, true, 0.11 );
			}
			return;
		}
	}

	pStudioHdr->Studio_BuildMatrices(
		GetEngineObject()->GetAbsAngles(),
		adjOrigin, 
		pos, 
		q, 
		-1,
		GetEngineObject()->GetModelScale(), // Scaling
		pBoneToWorld,
		boneMask );

	if (ai_setupbones_debug.GetBool())
	{
		// Msg("%s:%s:%s (%x)\n", GetClassname(), GetDebugName(), STRING(GetModelName()), boneMask );
		DrawRawSkeleton( pBoneToWorld, boneMask, true, 0.11 );
	}
	GetEngineObject()->RemoveEFlags( EFL_SETTING_UP_BONES );
}

//=========================================================
//=========================================================
int CBaseAnimating::GetNumBones ( void )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if(pStudioHdr)
	{
		return pStudioHdr->numbones();
	}
	else
	{
		Assert(!"CBaseAnimating::GetNumBones: model missing");
		return 0;
	}
}


//=========================================================
//=========================================================

//-----------------------------------------------------------------------------
// Purpose: Returns index number of a given named attachment
// Input  : name of attachment
// Output :	attachment index number or -1 if attachment not found
//-----------------------------------------------------------------------------
int CBaseAnimating::LookupAttachment( const char *szName )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::LookupAttachment: model missing");
		return 0;
	}

	// The +1 is to make attachment indices be 1-based (namely 0 == invalid or unused attachment)
	return pStudioHdr->Studio_FindAttachment( szName ) + 1;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the world location and world angles of an attachment
// Input  : attachment name
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachment( const char *szName, Vector &absOrigin, QAngle &absAngles )
{																
	return GetAttachment( LookupAttachment( szName ), absOrigin, absAngles );
}

//-----------------------------------------------------------------------------
// Purpose: Returns the world location and world angles of an attachment
// Input  : attachment index
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachment ( int iAttachment, Vector &absOrigin, QAngle &absAngles )
{
	matrix3x4_t attachmentToWorld;

	bool bRet = GetAttachment( iAttachment, attachmentToWorld );
	MatrixAngles( attachmentToWorld, absAngles, absOrigin );
	return bRet;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the world location and world angles of an attachment
// Input  : attachment index
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachment( int iAttachment, matrix3x4_t &attachmentToWorld )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr)
	{
		MatrixCopy(GetEngineObject()->EntityToWorldTransform(), attachmentToWorld);
		AssertOnce(!"CBaseAnimating::GetAttachment: model missing");
		return false;
	}

	if (iAttachment < 1 || iAttachment > pStudioHdr->GetNumAttachments())
	{
		MatrixCopy(GetEngineObject()->EntityToWorldTransform(), attachmentToWorld);
//		Assert(!"CBaseAnimating::GetAttachment: invalid attachment index");
		return false;
	}

	const mstudioattachment_t &pattachment = pStudioHdr->pAttachment( iAttachment-1 );
	int iBone = pStudioHdr->GetAttachmentBone( iAttachment-1 );

	matrix3x4_t bonetoworld;
	GetBoneTransform( iBone, bonetoworld );
	if ( (pattachment.flags & ATTACHMENT_FLAG_WORLD_ALIGN) == 0 )
	{
		ConcatTransforms( bonetoworld, pattachment.local, attachmentToWorld ); 
	}
	else
	{
		Vector vecLocalBonePos, vecWorldBonePos;
		MatrixGetColumn( pattachment.local, 3, vecLocalBonePos );
		VectorTransform( vecLocalBonePos, bonetoworld, vecWorldBonePos );

		SetIdentityMatrix( attachmentToWorld );
		MatrixSetColumn( vecWorldBonePos, 3, attachmentToWorld );
	}

	return true;
}

// gets the bone for an attachment
int CBaseAnimating::GetAttachmentBone( int iAttachment )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr || iAttachment < 1 || iAttachment > pStudioHdr->GetNumAttachments() )
	{
		AssertOnce(pStudioHdr && "CBaseAnimating::GetAttachment: model missing");
		return 0;
	}

	return pStudioHdr->GetAttachmentBone( iAttachment-1 );
}


//-----------------------------------------------------------------------------
// Purpose: Returns the world location of an attachment
// Input  : attachment index
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachment( const char *szName, Vector &absOrigin, Vector *forward, Vector *right, Vector *up )
{																
	return GetAttachment( LookupAttachment( szName ), absOrigin, forward, right, up );
}

bool CBaseAnimating::GetAttachment( int iAttachment, Vector &absOrigin, Vector *forward, Vector *right, Vector *up )
{
	matrix3x4_t attachmentToWorld;

	bool bRet = GetAttachment( iAttachment, attachmentToWorld );
	MatrixPosition( attachmentToWorld, absOrigin );
	if (forward)
	{
		MatrixGetColumn( attachmentToWorld, 0, forward );
	}
	if (right)
	{
		MatrixGetColumn( attachmentToWorld, 1, right );
	}
	if (up)
	{
		MatrixGetColumn( attachmentToWorld, 2, up );
	}
	return bRet;
}


//-----------------------------------------------------------------------------
// Returns the attachment in local space
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachmentLocal( const char *szName, Vector &origin, QAngle &angles )
{
	return GetAttachmentLocal( LookupAttachment( szName ), origin, angles );
}

bool CBaseAnimating::GetAttachmentLocal( int iAttachment, Vector &origin, QAngle &angles )
{
	matrix3x4_t attachmentToEntity;

	bool bRet = GetAttachmentLocal( iAttachment, attachmentToEntity );
	MatrixAngles( attachmentToEntity, angles, origin );
	return bRet;
}

bool CBaseAnimating::GetAttachmentLocal( int iAttachment, matrix3x4_t &attachmentToLocal )
{
	matrix3x4_t attachmentToWorld;
	bool bRet = GetAttachment(iAttachment, attachmentToWorld);
	matrix3x4_t worldToEntity;
	MatrixInvert(GetEngineObject()->EntityToWorldTransform(), worldToEntity );
	ConcatTransforms( worldToEntity, attachmentToWorld, attachmentToLocal ); 
	return bRet;
}


//=========================================================
//=========================================================
void CBaseAnimating::GetEyeballs( Vector &origin, QAngle &angles )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetAttachment: model missing");
		return;
	}

	for (int iBodypart = 0; iBodypart < pStudioHdr->numbodyparts(); iBodypart++)
	{
		mstudiobodyparts_t *pBodypart = pStudioHdr->pBodypart( iBodypart );
		for (int iModel = 0; iModel < pBodypart->nummodels; iModel++)
		{
			mstudiomodel_t *pModel = pBodypart->pModel( iModel );
			for (int iEyeball = 0; iEyeball < pModel->numeyeballs; iEyeball++)
			{
				mstudioeyeball_t *pEyeball = pModel->pEyeball( iEyeball );
				matrix3x4_t bonetoworld;
				GetBoneTransform( pEyeball->bone, bonetoworld );
				VectorTransform( pEyeball->org, bonetoworld,  origin );
				MatrixAngles( bonetoworld, angles ); // ???
			}
		}
	}
}


//=========================================================
//=========================================================
int CBaseAnimating::FindTransitionSequence( int iCurrentSequence, int iGoalSequence, int *piDir )
{
	Assert( GetModelPtr() );

	if (piDir == NULL)
	{
		int iDir = 1;
		int sequence = GetEngineObject()->GetModelPtr()->FindTransitionSequence( iCurrentSequence, iGoalSequence, &iDir );
		if (iDir != 1)
			return -1;
		else
			return sequence;
	}

	return GetEngineObject()->GetModelPtr()->FindTransitionSequence( iCurrentSequence, iGoalSequence, piDir );
}


bool CBaseAnimating::GotoSequence( int iCurrentSequence, float flCurrentCycle, float flCurrentRate, int iGoalSequence, int &nNextSequence, float &flNextCycle, int &iNextDir )
{
	return GetEngineObject()->GetModelPtr()->GotoSequence( iCurrentSequence, flCurrentCycle, flCurrentRate, iGoalSequence, nNextSequence, flNextCycle, iNextDir );
}


int CBaseAnimating::GetEntryNode( int iSequence )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr();
	if (! pstudiohdr)
		return 0;

	return pstudiohdr->EntryNode( iSequence );
}


int CBaseAnimating::GetExitNode( int iSequence )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr();
	if (! pstudiohdr)
		return 0;
	
	return pstudiohdr->ExitNode( iSequence );
}


//=========================================================
//=========================================================

void CBaseAnimating::SetBodygroup( int iGroup, int iValue )
{
	// SetBodygroup is not supported on pending dynamic models. Wait for it to load!
	// XXX TODO we could buffer up the group and value if we really needed to. -henryg
	Assert( GetModelPtr() );
	int newBody = GetEngineObject()->GetBody();
	GetEngineObject()->GetModelPtr()->SetBodygroup( newBody, iGroup, iValue );
	GetEngineObject()->SetBody(newBody);
}

int CBaseAnimating::GetBodygroup( int iGroup )
{
	//Assert( IsDynamicModelLoading() || GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->GetBodygroup(GetEngineObject()->GetBody(), iGroup);//IsDynamicModelLoading() ? 0 : 
}

const char *CBaseAnimating::GetBodygroupName( int iGroup )
{
	//Assert( IsDynamicModelLoading() || GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->GetBodygroupName( iGroup );//IsDynamicModelLoading() ? "" : 
}

int CBaseAnimating::FindBodygroupByName( const char *name )
{
	//Assert( IsDynamicModelLoading() || GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->FindBodygroupByName( name );//IsDynamicModelLoading() ? -1 : 
}

int CBaseAnimating::GetBodygroupCount( int iGroup )
{
	//Assert( IsDynamicModelLoading() || GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->GetBodygroupCount( iGroup );//IsDynamicModelLoading() ? 0 : 
}

int CBaseAnimating::GetNumBodyGroups( void )
{
	//Assert( IsDynamicModelLoading() || GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->GetNumBodyGroups( );//IsDynamicModelLoading() ? 0 : 
}

int CBaseAnimating::ExtractBbox( int sequence, Vector& mins, Vector& maxs )
{
	//Assert( IsDynamicModelLoading() || GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->ExtractBbox( sequence, mins, maxs );//IsDynamicModelLoading() ? 0 : 
}

//=========================================================
//=========================================================

void CBaseAnimating::SetSequenceBox( void )
{
	Vector mins, maxs;

	// Get sequence bbox
	if ( ExtractBbox(GetEngineObject()->GetSequence(), mins, maxs ) )
	{
		// expand box for rotation
		// find min / max for rotations
		float yaw = GetEngineObject()->GetLocalAngles().y * (M_PI / 180.0);
		
		Vector xvector, yvector;
		xvector.x = cos(yaw);
		xvector.y = sin(yaw);
		yvector.x = -sin(yaw);
		yvector.y = cos(yaw);
		Vector bounds[2];

		bounds[0] = mins;
		bounds[1] = maxs;
		
		Vector rmin( 9999, 9999, 9999 );
		Vector rmax( -9999, -9999, -9999 );
		Vector base, transformed;

		for (int i = 0; i <= 1; i++ )
		{
			base.x = bounds[i].x;
			for ( int j = 0; j <= 1; j++ )
			{
				base.y = bounds[j].y;
				for ( int k = 0; k <= 1; k++ )
				{
					base.z = bounds[k].z;
					
				// transform the point
					transformed.x = xvector.x*base.x + yvector.x*base.y;
					transformed.y = xvector.y*base.x + yvector.y*base.y;
					transformed.z = base.z;
					
					for ( int l = 0; l < 3; l++ )
					{
						if (transformed[l] < rmin[l])
							rmin[l] = transformed[l];
						if (transformed[l] > rmax[l])
							rmax[l] = transformed[l];
					}
				}
			}
		}
		rmin.z = 0;
		rmax.z = rmin.z + 1;
		UTIL_SetSize( this, rmin, rmax );
	}
}

//=========================================================
//=========================================================
int CBaseAnimating::RegisterPrivateActivity( const char *pszActivityName )
{
	return mdlcache->ActivityList_RegisterPrivateActivity( pszActivityName );
}

//-----------------------------------------------------------------------------
// Purpose: Notifies the console that this entity could not retrieve an
//			animation sequence for the specified activity. This probably means
//			there's a typo in the model QC file, or the sequence is missing
//			entirely.
//			
//
// Input  : iActivity - The activity that failed to resolve to a sequence.
//
//
// NOTE   :	IMPORTANT - Something needs to be done so that private activities
//			(which are allowed to collide in the activity list) remember each
//			entity that registered an activity there, and the activity name
//			each character registered.
//-----------------------------------------------------------------------------
void CBaseAnimating::ReportMissingActivity( int iActivity )
{
	Msg( "%s has no sequence for act:%s\n", GetClassname(), mdlcache->ActivityList_NameForIndex(iActivity) );
}


LocalFlexController_t CBaseAnimating::GetNumFlexControllers( void )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr( );
	if (! pstudiohdr)
		return LocalFlexController_t(0);

	return pstudiohdr->numflexcontrollers();
}


const char *CBaseAnimating::GetFlexDescFacs( int iFlexDesc )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	mstudioflexdesc_t *pflexdesc = pstudiohdr->pFlexdesc( iFlexDesc );

	return pflexdesc->pszFACS( );
}

const char *CBaseAnimating::GetFlexControllerName( LocalFlexController_t iFlexController )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	mstudioflexcontroller_t *pflexcontroller = pstudiohdr->pFlexcontroller( iFlexController );

	return pflexcontroller->pszName( );
}

const char *CBaseAnimating::GetFlexControllerType( LocalFlexController_t iFlexController )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	mstudioflexcontroller_t *pflexcontroller = pstudiohdr->pFlexcontroller( iFlexController );

	return pflexcontroller->pszType( );
}

//-----------------------------------------------------------------------------
// Purpose: Converts the ground speed of the animating entity into a true velocity
// Output : Vector - velocity of the character at its current m_flGroundSpeed
//-----------------------------------------------------------------------------
Vector CBaseAnimating::GetGroundSpeedVelocity( void )
{
	IStudioHdr *pstudiohdr = GetEngineObject()->GetModelPtr();
	if (!pstudiohdr)
		return vec3_origin;

	QAngle  vecAngles;
	Vector	vecVelocity;

	vecAngles.y = GetEngineObject()->GetSequenceMoveYaw(GetEngineObject()->GetSequence() );
	vecAngles.x = 0;
	vecAngles.z = 0;

	vecAngles.y += GetEngineObject()->GetLocalAngles().y;

	AngleVectors( vecAngles, &vecVelocity );

	vecVelocity = vecVelocity * GetEngineObject()->GetGroundSpeed();

	return vecVelocity;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *szModelName - 
//-----------------------------------------------------------------------------
void CBaseAnimating::SetModel( const char *szModelName )
{
	MDLCACHE_CRITICAL_SECTION();

	if ( szModelName[0] )
	{
		int modelIndex = modelinfo->GetModelIndex( szModelName );
		const model_t *model = modelinfo->GetModel( modelIndex );
		if ( model && ( modelinfo->GetModelType( model ) != mod_studio ) )
		{
			Msg( "Setting CBaseAnimating to non-studio model %s  (type:%i)\n",	szModelName, modelinfo->GetModelType( model ) );
		}
	}

	if ( m_boneCacheHandle )
	{
		Studio_DestroyBoneCache( m_boneCacheHandle );
		m_boneCacheHandle = 0;
	}

	UTIL_SetModel( this, szModelName );

	InitBoneControllers( );
	GetEngineObject()->SetSequence( 0 );
	
	PopulatePoseParameters();
}



//-----------------------------------------------------------------------------
// Purpose: return the index to the shared bone cache
// Output :
//-----------------------------------------------------------------------------
CBoneCache *CBaseAnimating::GetBoneCache( void )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	Assert(pStudioHdr);

	CBoneCache *pcache = Studio_GetBoneCache( m_boneCacheHandle );
	int boneMask = BONE_USED_BY_HITBOX | BONE_USED_BY_ATTACHMENT;

	// TF queries these bones to position weapons when players are killed
#if defined( TF_DLL )
	boneMask |= BONE_USED_BY_BONE_MERGE;
#endif
	if ( pcache )
	{
		if ( pcache->IsValid( gpGlobals->curtime ) && (pcache->m_boneMask & boneMask) == boneMask && pcache->m_timeValid <= gpGlobals->curtime)
		{
			// Msg("%s:%s:%s (%x:%x:%8.4f) cache\n", GetClassname(), GetDebugName(), STRING(GetModelName()), boneMask, pcache->m_boneMask, pcache->m_timeValid );
			// in memory and still valid, use it!
			return pcache;
		}
		// in memory, but missing some of the bone masks
		if ( (pcache->m_boneMask & boneMask) != boneMask )
		{
			Studio_DestroyBoneCache( m_boneCacheHandle );
			m_boneCacheHandle = 0;
			pcache = NULL;
		}
	}

	matrix3x4_t bonetoworld[MAXSTUDIOBONES];
	SetupBones( bonetoworld, boneMask );

	if ( pcache )
	{
		// still in memory but out of date, refresh the bones.
		pcache->UpdateBones( bonetoworld, pStudioHdr->numbones(), gpGlobals->curtime );
	}
	else
	{
		bonecacheparams_t params;
		params.pStudioHdr = pStudioHdr;
		params.pBoneToWorld = bonetoworld;
		params.curtime = gpGlobals->curtime;
		params.boneMask = boneMask;

		m_boneCacheHandle = Studio_CreateBoneCache( params );
		pcache = Studio_GetBoneCache( m_boneCacheHandle );
	}
	Assert(pcache);
	return pcache;
}


void CBaseAnimating::InvalidateBoneCache( void )
{
	Studio_InvalidateBoneCache( m_boneCacheHandle );
}

bool CBaseAnimating::TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	// Return a special case for scaled physics objects
	if (GetEngineObject()->GetModelScale() != 1.0f )
	{
		IPhysicsObject *pPhysObject = VPhysicsGetObject();
		Vector vecPosition;
		QAngle vecAngles;
		pPhysObject->GetPosition( &vecPosition, &vecAngles );
		const CPhysCollide *pScaledCollide = pPhysObject->GetCollide();
		physcollision->TraceBox( ray, pScaledCollide, vecPosition, vecAngles, &tr );
		
		return tr.DidHit();
	}

	if (GetEngineObject()->IsSolidFlagSet( FSOLID_CUSTOMRAYTEST ))
	{
		if (!TestHitboxes( ray, fContentsMask, tr ))
			return true;

		return tr.DidHit();
	}

	// We shouldn't get here.
	Assert(0);
	return false;
}

bool CBaseAnimating::TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetBonePosition: model missing");
		return false;
	}

	mstudiohitboxset_t *set = pStudioHdr->pHitboxSet(GetEngineObject()->GetHitboxSet() );
	if ( !set || !set->numhitboxes )
		return false;

	CBoneCache *pcache = GetBoneCache( );

	matrix3x4_t *hitboxbones[MAXSTUDIOBONES];
	pcache->ReadCachedBonePointers( hitboxbones, pStudioHdr->numbones() );

	if ( TraceToStudio( physprops, ray, pStudioHdr, set, hitboxbones, fContentsMask, GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetModelScale(), tr ) )
	{
		mstudiobbox_t *pbox = set->pHitbox( tr.hitbox );
		mstudiobone_t *pBone = pStudioHdr->pBone(pbox->bone);
		tr.surface.name = "**studio**";
		tr.surface.flags = SURF_HITBOX;
		tr.surface.surfaceProps = physprops->GetSurfaceIndex( pBone->pszSurfaceProp() );
	}
	return true;
}

void CBaseAnimating::InitBoneControllers ( void ) // FIXME: rename
{
	int i;

	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr)
		return;

	int nBoneControllerCount = pStudioHdr->numbonecontrollers();	
	if ( nBoneControllerCount > NUM_BONECTRLS )
	{
		nBoneControllerCount = NUM_BONECTRLS;

#ifdef _DEBUG
		Warning( "Model %s has too many bone controllers! (Max %d allowed)\n", pStudioHdr->pszName(), NUM_BONECTRLS );
#endif
	}

	for (i = 0; i < nBoneControllerCount; i++)
	{
		GetEngineObject()->SetBoneController( i, 0.0 );
	}

	Assert( pStudioHdr->SequencesAvailable() );

	if ( pStudioHdr->SequencesAvailable() )
	{
		for (i = 0; i < pStudioHdr->GetNumPoseParameters(); i++)
		{
			GetEngineObject()->SetPoseParameter( i, 0.0 );
		}
	}
}



//------------------------------------------------------------------------------
// Purpose : Returns velcocity of the NPC from it's animation.  
//			 If physically simulated gets velocity from physics object
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CBaseAnimating::GetVelocity(Vector *vVelocity, AngularImpulse *vAngVelocity) 
{
	if (GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		BaseClass::GetVelocity(vVelocity,vAngVelocity);
	}
	else if ( !(GetEngineObject()->GetFlags() & FL_ONGROUND) )
	{
		BaseClass::GetVelocity(vVelocity,vAngVelocity);
	}
	else
	{
		if (vVelocity != NULL)
		{
			Vector	vRawVel;

			GetEngineObject()->GetSequenceLinearMotion(GetEngineObject()->GetSequence(), &vRawVel );

			// Build a rotation matrix from NPC orientation
			matrix3x4_t fRotateMatrix;
			AngleMatrix(GetEngineObject()->GetLocalAngles(), fRotateMatrix);
			VectorRotate( vRawVel, fRotateMatrix, *vVelocity);
		}
		if (vAngVelocity != NULL)
		{
			QAngle tmp = GetLocalAngularVelocity();
			QAngleToAngularImpulse( tmp, *vAngVelocity );
		}
	}
}


//=========================================================
//=========================================================

void CBaseAnimating::GetSkeleton( IStudioHdr *pStudioHdr, Vector pos[], Quaternion q[], int boneMask )
{
	if(!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetSkeleton() without a model");
		return;
	}

	IBoneSetup boneSetup( pStudioHdr, boneMask, GetEngineObject()->GetPoseParameterArray() );
	boneSetup.InitPose( pos, q );

	boneSetup.AccumulatePose( pos, q, GetEngineObject()->GetSequence(), GetEngineObject()->GetCycle(), 1.0, gpGlobals->curtime, m_pIk );

	if ( m_pIk )
	{
		CIKContext auto_ik;
		auto_ik.Init( pStudioHdr, GetEngineObject()->GetAbsAngles(), GetEngineObject()->GetAbsOrigin(), gpGlobals->curtime, 0, boneMask );
		boneSetup.CalcAutoplaySequences( pos, q, gpGlobals->curtime, &auto_ik );
	}
	else
	{
		boneSetup.CalcAutoplaySequences( pos, q, gpGlobals->curtime, NULL );
	}
	boneSetup.CalcBoneAdj( pos, q, GetEngineObject()->GetEncodedControllerArray() );
}

int CBaseAnimating::DrawDebugTextOverlays(void) 
{
	int text_offset = BaseClass::DrawDebugTextOverlays();

	if (m_debugOverlays & OVERLAY_TEXT_BIT) 
	{
		// ----------------
		// Print Look time
		// ----------------
		char tempstr[1024];
		Q_snprintf(tempstr, sizeof(tempstr), "Sequence: (%3d) %s", GetEngineObject()->GetSequence(), GetSequenceName(GetEngineObject()->GetSequence() ) );
		EntityText(text_offset,tempstr,0);
		text_offset++;
		const char *pActname = GetSequenceActivityName(GetEngineObject()->GetSequence());
		if ( pActname && strlen(pActname) )
		{
			Q_snprintf(tempstr, sizeof(tempstr), "Activity %s", pActname );
			EntityText(text_offset,tempstr,0);
			text_offset++;
		}

		Q_snprintf(tempstr, sizeof(tempstr), "Cycle: %.5f (%.5f)", GetEngineObject()->GetCycle(), GetEngineObject()->GetAnimTime());
		EntityText(text_offset,tempstr,0);
		text_offset++;
	}

	// Visualize attachment points
	if ( m_debugOverlays & OVERLAY_ATTACHMENTS_BIT )
	{	
		IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();

		if ( pStudioHdr )
		{
			Vector	vecPos, vecForward, vecRight, vecUp;
			char tempstr[256];

			// Iterate all the stored attachments
			for ( int i = 1; i <= pStudioHdr->GetNumAttachments(); i++ )
			{
				GetAttachment( i, vecPos, &vecForward, &vecRight, &vecUp );

				// Red - forward, green - right, blue - up
				NDebugOverlay::Line( vecPos, vecPos + ( vecForward * 4.0f ), 255, 0, 0, true, 0.05f );
				NDebugOverlay::Line( vecPos, vecPos + ( vecRight * 4.0f ), 0, 255, 0, true, 0.05f );
				NDebugOverlay::Line( vecPos, vecPos + ( vecUp * 4.0f ), 0, 0, 255, true, 0.05f );
				
				Q_snprintf( tempstr, sizeof(tempstr), " < %s (%d)", pStudioHdr->pAttachment(i-1).pszName(), i );
				NDebugOverlay::Text( vecPos, tempstr, true, 0.05f );
			}
		}
	}

	return text_offset;
}



//-----------------------------------------------------------------------------
// Purpose: Returns the origin at which to play an inputted dispatcheffect 
//-----------------------------------------------------------------------------
void CBaseAnimating::GetInputDispatchEffectPosition( const char *sInputString, Vector &pOrigin, QAngle &pAngles )
{
	// See if there's a specified attachment point
	int iAttachment;
	if (GetEngineObject()->GetModelPtr() && sscanf( sInputString, "%d", &iAttachment ) )
	{
		if ( !GetAttachment( iAttachment, pOrigin, pAngles ) )
		{
			Msg( "ERROR: Mapmaker tried to spawn DispatchEffect %s, but %s has no attachment %d\n", 
				sInputString, STRING(GetEngineObject()->GetModelName()), iAttachment );
		}
		return;
	}

	BaseClass::GetInputDispatchEffectPosition( sInputString, pOrigin, pAngles );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : setnum - 
//-----------------------------------------------------------------------------
void CBaseAnimating::SetHitboxSet( int setnum )
{
#ifdef _DEBUG
	IStudioHdr *pStudioHdr = GetModelPtr();
	if ( !pStudioHdr )
		return;

	if (setnum > pStudioHdr->numhitboxsets())
	{
		// Warn if an bogus hitbox set is being used....
		static bool s_bWarned = false;
		if (!s_bWarned)
		{
			Warning("Using bogus hitbox set in entity %s!\n", GetClassname() );
			s_bWarned = true;
		}
		setnum = 0;
	}
#endif

	GetEngineObject()->SetHitboxSet( setnum);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *setname - 
//-----------------------------------------------------------------------------
void CBaseAnimating::SetHitboxSetByName( const char *setname )
{
	Assert( GetModelPtr() );
	GetEngineObject()->SetHitboxSet(GetEngineObject()->GetModelPtr()->FindHitboxSetByName( setname ));
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CBaseAnimating::GetHitboxSet( void )
{
	return GetEngineObject()->GetHitboxSet();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : char const
//-----------------------------------------------------------------------------
const char *CBaseAnimating::GetHitboxSetName( void )
{
	Assert( GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->GetHitboxSetName(GetEngineObject()->GetHitboxSet() );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CBaseAnimating::GetHitboxSetCount( void )
{
	Assert( GetModelPtr() );
	return GetEngineObject()->GetModelPtr()->GetHitboxSetCount();
}

static Vector	hullcolor[8] = 
{
	Vector( 1.0, 1.0, 1.0 ),
	Vector( 1.0, 0.5, 0.5 ),
	Vector( 0.5, 1.0, 0.5 ),
	Vector( 1.0, 1.0, 0.5 ),
	Vector( 0.5, 0.5, 1.0 ),
	Vector( 1.0, 0.5, 1.0 ),
	Vector( 0.5, 1.0, 1.0 ),
	Vector( 1.0, 1.0, 1.0 )
};

//-----------------------------------------------------------------------------
// Purpose: Send the current hitboxes for this model to the client ( to compare with
//  r_drawentities 3 client side boxes ).
// WARNING:  This uses a ton of bandwidth, only use on a listen server
//-----------------------------------------------------------------------------
void CBaseAnimating::DrawServerHitboxes( float duration /*= 0.0f*/, bool monocolor /*= false*/  )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if ( !pStudioHdr )
		return;

	mstudiohitboxset_t *set =pStudioHdr->pHitboxSet(GetEngineObject()->GetHitboxSet() );
	if ( !set )
		return;

	Vector position;
	QAngle angles;

	int r = 0;
	int g = 0;
	int b = 255;

	for ( int i = 0; i < set->numhitboxes; i++ )
	{
		mstudiobbox_t *pbox = set->pHitbox( i );

		GetBonePosition( pbox->bone, position, angles );

		if ( !monocolor )
		{
			int j = (pbox->group % 8);
			
			r = ( int ) ( 255.0f * hullcolor[j][0] );
			g = ( int ) ( 255.0f * hullcolor[j][1] );
			b = ( int ) ( 255.0f * hullcolor[j][2] );
		}

		NDebugOverlay::BoxAngles( position, pbox->bbmin * GetEngineObject()->GetModelScale(), pbox->bbmax * GetEngineObject()->GetModelScale(), angles, r, g, b, 0 ,duration );
	}
}


void CBaseAnimating::DrawRawSkeleton( matrix3x4_t boneToWorld[], int boneMask, bool noDepthTest, float duration, bool monocolor )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if ( !pStudioHdr )
		return;

	int i;
	int r = 255;
	int g = 255;
	int b = monocolor ? 255 : 0;
	

	for (i = 0; i < pStudioHdr->numbones(); i++)
	{
		if (pStudioHdr->pBone( i )->flags & boneMask)
		{
			Vector p1;
			MatrixPosition( boneToWorld[i], p1 );
			if ( pStudioHdr->pBone( i )->parent != -1 )
			{
				Vector p2;
				MatrixPosition( boneToWorld[pStudioHdr->pBone( i )->parent], p2 );
                NDebugOverlay::Line( p1, p2, r, g, b, noDepthTest, duration );
			}
		}
	}
}


int CBaseAnimating::GetHitboxBone( int hitboxIndex )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if ( pStudioHdr )
	{
		mstudiohitboxset_t *set =pStudioHdr->pHitboxSet(GetEngineObject()->GetHitboxSet() );
		if ( set && hitboxIndex < set->numhitboxes )
		{
			return set->pHitbox( hitboxIndex )->bone;
		}
	}
	return 0;
}


//-----------------------------------------------------------------------------
// Computes a box that surrounds all hitboxes
//-----------------------------------------------------------------------------
bool CBaseAnimating::ComputeHitboxSurroundingBox( Vector *pVecWorldMins, Vector *pVecWorldMaxs )
{
	// Note that this currently should not be called during Relink because of IK.
	// The code below recomputes bones so as to get at the hitboxes,
	// which causes IK to trigger, which causes raycasts against the other entities to occur,
	// which is illegal to do while in the Relink phase.

	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if (!pStudioHdr)
		return false;

	mstudiohitboxset_t *set = pStudioHdr->pHitboxSet(GetEngineObject()->GetHitboxSet() );
	if ( !set || !set->numhitboxes )
		return false;

	CBoneCache *pCache = GetBoneCache();

	// Compute a box in world space that surrounds this entity
	pVecWorldMins->Init( FLT_MAX, FLT_MAX, FLT_MAX );
	pVecWorldMaxs->Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );

	Vector vecBoxAbsMins, vecBoxAbsMaxs;
	for ( int i = 0; i < set->numhitboxes; i++ )
	{
		mstudiobbox_t *pbox = set->pHitbox(i);
		matrix3x4_t *pMatrix = pCache->GetCachedBone(pbox->bone);

		if ( pMatrix )
		{
			TransformAABB( *pMatrix, pbox->bbmin * GetEngineObject()->GetModelScale(), pbox->bbmax * GetEngineObject()->GetModelScale(), vecBoxAbsMins, vecBoxAbsMaxs );
			VectorMin( *pVecWorldMins, vecBoxAbsMins, *pVecWorldMins );
			VectorMax( *pVecWorldMaxs, vecBoxAbsMaxs, *pVecWorldMaxs );
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Computes a box that surrounds all hitboxes, in entity space
//-----------------------------------------------------------------------------
bool CBaseAnimating::ComputeEntitySpaceHitboxSurroundingBox( Vector *pVecWorldMins, Vector *pVecWorldMaxs )
{
	// Note that this currently should not be called during position recomputation because of IK.
	// The code below recomputes bones so as to get at the hitboxes,
	// which causes IK to trigger, which causes raycasts against the other entities to occur,
	// which is illegal to do while in the computeabsposition phase.

	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if (!pStudioHdr)
		return false;

	mstudiohitboxset_t *set = pStudioHdr->pHitboxSet(GetEngineObject()->GetHitboxSet() );
	if ( !set || !set->numhitboxes )
		return false;

	CBoneCache *pCache = GetBoneCache();
	matrix3x4_t *hitboxbones[MAXSTUDIOBONES];
	pCache->ReadCachedBonePointers( hitboxbones, pStudioHdr->numbones() );

	// Compute a box in world space that surrounds this entity
	pVecWorldMins->Init( FLT_MAX, FLT_MAX, FLT_MAX );
	pVecWorldMaxs->Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );

	matrix3x4_t worldToEntity, boneToEntity;
	MatrixInvert(GetEngineObject()->EntityToWorldTransform(), worldToEntity );

	Vector vecBoxAbsMins, vecBoxAbsMaxs;
	for ( int i = 0; i < set->numhitboxes; i++ )
	{
		mstudiobbox_t *pbox = set->pHitbox(i);

		ConcatTransforms( worldToEntity, *hitboxbones[pbox->bone], boneToEntity );
		TransformAABB( boneToEntity, pbox->bbmin, pbox->bbmax, vecBoxAbsMins, vecBoxAbsMaxs );
		VectorMin( *pVecWorldMins, vecBoxAbsMins, *pVecWorldMins );
		VectorMax( *pVecWorldMaxs, vecBoxAbsMaxs, *pVecWorldMaxs );
	}
	return true;
}


int CBaseAnimating::GetPhysicsBone( int boneIndex )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if ( pStudioHdr )
	{
		if ( boneIndex >= 0 && boneIndex < pStudioHdr->numbones() )
			return pStudioHdr->pBone( boneIndex )->physicsbone;
	}
	return 0;
}

bool CBaseAnimating::LookupHitbox( const char *szName, int& outSet, int& outBox )
{
	IStudioHdr* pHdr = GetEngineObject()->GetModelPtr();

	outSet = -1;
	outBox = -1;

	if( !pHdr )
		return false;

	for( int set=0; set < pHdr->numhitboxsets(); set++ )
	{
		for( int i = 0; i < pHdr->iHitboxCount(set); i++ )
		{
			mstudiobbox_t* pBox = pHdr->pHitbox( i, set );
			
			if( !pBox )
				continue;
			
			const char* szBoxName = pBox->pszHitboxName();
			if( Q_stricmp( szBoxName, szName ) == 0 )
			{
				outSet = set;
				outBox = i;
				return true;
			}
		}
	}

	return false;
}

void CBaseAnimating::CopyAnimationDataFrom( CBaseAnimating *pSource )
{
	this->GetEngineObject()->SetModelName( pSource->GetEngineObject()->GetModelName() );
	this->GetEngineObject()->SetModelIndex( pSource->GetEngineObject()->GetModelIndex() );
	this->GetEngineObject()->SetCycle( pSource->GetEngineObject()->GetCycle() );
	this->GetEngineObject()->SetEffects( pSource->GetEngineObject()->GetEffects() );
	this->IncrementInterpolationFrame();
	this->GetEngineObject()->SetSequence( pSource->GetEngineObject()->GetSequence() );
	this->GetEngineObject()->SetAnimTime(pSource->GetEngineObject()->GetAnimTime());
	this->GetEngineObject()->SetBody( pSource->GetEngineObject()->GetBody());
	this->GetEngineObject()->SetSkin( pSource->GetEngineObject()->GetSkin());
	this->GetEngineObject()->GetModelPtr();// LockStudioHdr();
}

int CBaseAnimating::GetHitboxesFrontside( int *boxList, int boxMax, const Vector &normal, float dist )
{
	int count = 0;
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if ( pStudioHdr )
	{
		mstudiohitboxset_t *set = pStudioHdr->pHitboxSet(GetEngineObject()->GetHitboxSet() );
		if ( set )
		{
			matrix3x4_t matrix;
			for ( int b = 0; b < set->numhitboxes; b++ )
			{
				mstudiobbox_t *pbox = set->pHitbox( b );

				GetBoneTransform( pbox->bone, matrix );
				Vector center = (pbox->bbmax + pbox->bbmin) * 0.5;
				Vector centerWs;
				VectorTransform( center, matrix, centerWs );
				if ( DotProduct( centerWs, normal ) >= dist )
				{
					if ( count < boxMax )
					{
						boxList[count] = b;
						count++;
					}
				}
			}
		}
	}

	return count;
}

void CBaseAnimating::EnableServerIK()
{
	if (!m_pIk)
	{
		m_pIk = new CIKContext;
		m_iIKCounter = 0;
	}
}

void CBaseAnimating::DisableServerIK()
{
	delete m_pIk;
	m_pIk = NULL;
}

Activity CBaseAnimating::GetSequenceActivity( int iSequence )
{
	if( iSequence == -1 )
	{
		return ACT_INVALID;
	}

	if ( !GetEngineObject()->GetModelPtr() )
		return ACT_INVALID;

	return (Activity)GetEngineObject()->GetModelPtr()->GetSequenceActivity( iSequence );
}

//void CBaseAnimating::ModifyOrAppendCriteria( AI_CriteriaSet& set )
//{
//	BaseClass::ModifyOrAppendCriteria( set );
//
//	// TODO
//	// Append any animation state parameters here
//}

void CBaseAnimating::OnResetSequence(int nSequence) {
	if (ai_sequence_debug.GetBool() == true && (m_debugOverlays & OVERLAY_NPC_SELECTED_BIT))
	{
		DevMsg("ResetSequence : %s: %s -> %s\n", GetClassname(), GetSequenceName(GetEngineObject()->GetSequence()), GetSequenceName(nSequence));
	}
}

void CBaseAnimating::InputBecomeRagdoll(inputdata_t& inputdata)
{
	BecomeRagdollOnClient(vec3_origin);
}


//-----------------------------------------------------------------------------
// Purpose: Async prefetches all anim data used by a particular sequence.  Returns true if all of the required data is memory resident
// Input  : iSequence - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseAnimating::PrefetchSequence( int iSequence )
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if ( !pStudioHdr )
		return true;

	return pStudioHdr->Studio_PrefetchSequence( iSequence );
}



//-----------------------------------------------------------------------------
// Purpose: model-change notification. Fires on dynamic load completion as well
//-----------------------------------------------------------------------------
IStudioHdr *CBaseAnimating::OnNewModel()
{
	(void) BaseClass::OnNewModel();

	// TODO: if dynamic, validate m_Sequence and apply queued body group settings?
	//if ( IsDynamicModelLoading() )
	//{
	//	// Called while dynamic model still loading -> new model, clear deferred state
	//	m_bResetSequenceInfoOnLoad = false;
	//	return NULL;
	//}

	IStudioHdr *hdr = GetEngineObject()->GetModelPtr();

	//if ( m_bResetSequenceInfoOnLoad )
	//{
	//	m_bResetSequenceInfoOnLoad = false;
	//	ResetSequenceInfo();
	//}

	return hdr;
}
