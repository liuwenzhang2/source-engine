//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Dissolve entity to be attached to target entity. Serves two purposes:
//
//			1) An entity that can be placed by a level designer and triggered
//			   to ignite a target entity.
//
//			2) An entity that can be created at runtime to ignite a target entity.
//
//=============================================================================//

#include "cbase.h"
#include "RagdollBoogie.h"
#include "physics_prop_ragdoll.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "IEffects.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Make electriciy every so often
//-----------------------------------------------------------------------------
static const char *s_pZapContext = "ZapContext";


//-----------------------------------------------------------------------------
// Save/load 
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CRagdollBoogie )

	DEFINE_FIELD( m_flStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_flBoogieLength, FIELD_FLOAT ),
	DEFINE_FIELD( m_flMagnitude, FIELD_FLOAT ),

	// Think this should be handled by StartTouch/etc.
//	DEFINE_FIELD( m_nSuppressionCount, FIELD_INTEGER ),

	DEFINE_FUNCTION( BoogieThink ),
	DEFINE_FUNCTION( ZapThink ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( env_ragdoll_boogie, CRagdollBoogie );


//-----------------------------------------------------------------------------
// Purpose: Creates a flame and attaches it to a target entity.
// Input  : pTarget - 
//-----------------------------------------------------------------------------
CRagdollBoogie *CRagdollBoogie::Create( CBaseEntity *pTarget, float flMagnitude, 
	float flStartTime, float flLengthTime, int nSpawnFlags )
{
	CRagdollProp *pRagdoll = dynamic_cast< CRagdollProp* >( pTarget );
	if ( !pRagdoll )
		return NULL;

	CRagdollBoogie *pBoogie = (CRagdollBoogie *)EntityList()->CreateEntityByName( "env_ragdoll_boogie" );
	if ( pBoogie == NULL )
		return NULL;

	pBoogie->GetEngineObject()->AddSpawnFlags( nSpawnFlags );
	pBoogie->AttachToEntity( pTarget );
	pBoogie->SetBoogieTime( flStartTime, flLengthTime );
	pBoogie->SetMagnitude( flMagnitude );
	pBoogie->Spawn();
	return pBoogie;
}


//-----------------------------------------------------------------------------
// Spawn
//-----------------------------------------------------------------------------
void CRagdollBoogie::Spawn()
{
	BaseClass::Spawn();

	SetThink( &CRagdollBoogie::BoogieThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.01f );

	if (GetEngineObject()->HasSpawnFlags( SF_RAGDOLL_BOOGIE_ELECTRICAL ) )
	{
		SetContextThink( &CRagdollBoogie::ZapThink, gpGlobals->curtime + random->RandomFloat( 0.1f, 0.3f ), s_pZapContext ); 
	}
}


//-----------------------------------------------------------------------------
// Zap!
//-----------------------------------------------------------------------------
void CRagdollBoogie::ZapThink()
{
	if ( !GetEngineObject()->GetMoveParent() )
		return;

	CBaseAnimating *pRagdoll = GetEngineObject()->GetMoveParent()->GetOuter()->GetBaseAnimating();
	if ( !pRagdoll )
		return;

	// Make electricity on the client
	IStudioHdr *pStudioHdr = pRagdoll->GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr)
		return;

	mstudiohitboxset_t *set = pStudioHdr->pHitboxSet( pRagdoll->GetHitboxSet() );

	if ( set->numhitboxes == 0 )
		return;

	if ( m_nSuppressionCount == 0 )
	{
		CEffectData	data;
		
		data.m_nEntIndex = GetEngineObject()->GetMoveParent()->GetOuter()->entindex();
		data.m_flMagnitude = 4;
		data.m_flScale = GetEngineObject()->HasSpawnFlags(SF_RAGDOLL_BOOGIE_ELECTRICAL_NARROW_BEAM) ? 1.0f : 2.0f;

		DispatchEffect( "TeslaHitboxes", data );	
	}

#ifdef HL2_EPISODIC
	const char* soundname = "RagdollBoogie.Zap";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
#endif

	SetContextThink( &CRagdollBoogie::ZapThink, gpGlobals->curtime + random->RandomFloat( 0.1f, 0.3f ), s_pZapContext ); 
}


//-----------------------------------------------------------------------------
// Suppression count
//-----------------------------------------------------------------------------
void CRagdollBoogie::IncrementSuppressionCount( CBaseEntity *pTarget )
{
	// Look for other boogies on the ragdoll + kill them
	for ( IEngineObjectServer *pChild = pTarget->GetEngineObject()->FirstMoveChild(); pChild; pChild = pChild->NextMovePeer() )
	{
		CRagdollBoogie *pBoogie = dynamic_cast<CRagdollBoogie*>(pChild->GetOuter());
		if ( !pBoogie )
			continue;

		++pBoogie->m_nSuppressionCount;
	}
}

void CRagdollBoogie::DecrementSuppressionCount( CBaseEntity *pTarget )
{
	// Look for other boogies on the ragdoll + kill them
	IEngineObjectServer *pNext;
	for ( IEngineObjectServer *pChild = pTarget->GetEngineObject()->FirstMoveChild(); pChild; pChild = pNext )
	{
		pNext = pChild->NextMovePeer();
		CRagdollBoogie *pBoogie = dynamic_cast<CRagdollBoogie*>(pChild->GetOuter());
		if ( !pBoogie )
			continue;

		if ( --pBoogie->m_nSuppressionCount <= 0 )
		{
			pBoogie->m_nSuppressionCount = 0;

			float dt = gpGlobals->curtime - pBoogie->m_flStartTime;
			if ( dt >= pBoogie->m_flBoogieLength )
			{
				EntityList()->PhysCallbackRemove( pBoogie );
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Attach to an entity
//-----------------------------------------------------------------------------
void CRagdollBoogie::AttachToEntity( CBaseEntity *pTarget )
{
	m_nSuppressionCount = 0;

	// Look for other boogies on the ragdoll + kill them
	IEngineObjectServer *pNext;
	for ( IEngineObjectServer *pChild = pTarget->GetEngineObject()->FirstMoveChild(); pChild; pChild = pNext )
	{
		pNext = pChild->NextMovePeer();
		CRagdollBoogie *pBoogie = dynamic_cast<CRagdollBoogie*>(pChild->GetOuter());
		if ( !pBoogie )
			continue;

		m_nSuppressionCount = pBoogie->m_nSuppressionCount;
		EntityList()->DestroyEntity( pChild->GetOuter() );
	}

	GetEngineObject()->FollowEntity( pTarget->GetEngineObject());
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : lifetime - 
//-----------------------------------------------------------------------------
void CRagdollBoogie::SetBoogieTime( float flStartTime, float flLengthTime )
{
	m_flStartTime = flStartTime;
	m_flBoogieLength = flLengthTime;
}


//-----------------------------------------------------------------------------
// Purpose: Burn targets around us
//-----------------------------------------------------------------------------
void CRagdollBoogie::SetMagnitude( float flMagnitude )
{
	m_flMagnitude = flMagnitude;
}


//-----------------------------------------------------------------------------
// Purpose: Burn targets around us
//-----------------------------------------------------------------------------
void CRagdollBoogie::BoogieThink( void )
{
	if (!GetEngineObject()->GetMoveParent()) {
		return;
	}
	CRagdollProp *pRagdoll = dynamic_cast< CRagdollProp* >(GetEngineObject()->GetMoveParent()->GetOuter() );
	if ( !pRagdoll )
	{
		EntityList()->DestroyEntity( this );
		return;
	}

	float flMagnitude = m_flMagnitude;
	if ( m_flBoogieLength != 0 )
	{
		float dt = gpGlobals->curtime - m_flStartTime;
		if ( dt >= m_flBoogieLength )
		{
			// Don't remove while suppressed... this helps if we try to start another boogie
			if ( m_nSuppressionCount == 0 )
			{
				EntityList()->DestroyEntity( this );
			}
			SetThink( NULL );
			return;
		}

		if ( dt < 0 )
		{
			GetEngineObject()->SetNextThink( gpGlobals->curtime + random->RandomFloat( 0.1, 0.2f ) );
			return;
		}

		flMagnitude = SimpleSplineRemapVal( dt, 0.0f, m_flBoogieLength, m_flMagnitude, 0.0f ); 
	}

#ifndef _XBOX
	if ( m_nSuppressionCount == 0 )
	{
		ragdoll_t *pRagdollPhys = pRagdoll->GetEngineObject()->GetRagdoll( );
		for ( int j = 0; j < pRagdollPhys->listCount; ++j )
		{
			float flMass = pRagdollPhys->list[j].pObject->GetMass();
			float flForce = m_flMagnitude * flMass;

			Vector vecForce;
			vecForce = RandomVector( -flForce, flForce );
			pRagdollPhys->list[j].pObject->ApplyForceCenter( vecForce ); 
		}
	}
#endif // !_XBOX

	GetEngineObject()->SetNextThink( gpGlobals->curtime + random->RandomFloat( 0.1, 0.2f ) );
}
