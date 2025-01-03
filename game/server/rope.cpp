//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "rope.h"
#include "rope_shared.h"
#include "sendproxy.h"
#include "rope_helpers.h"
#include "te_effect_dispatch.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------
// Rope Spawn Flags
//--------------------------------------------
#define SF_ROPE_RESIZE			1		// Automatically resize the rope

// -------------------------------------------------------------------------------- //
// Fun With Tables.
// -------------------------------------------------------------------------------- //

LINK_ENTITY_TO_CLASS( move_rope, CRopeKeyframe );
LINK_ENTITY_TO_CLASS( keyframe_rope, CRopeKeyframe );

//extern void SendProxy_MoveParentToInt(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID);

IMPLEMENT_SERVERCLASS_ST_NOBASE( CRopeKeyframe, DT_RopeKeyframe )


	//SendPropVector(SENDINFO_ORIGIN(m_vecOrigin), -1,  SPROP_COORD, 0.0f, HIGH_DEFAULT, SendProxy_Origin),
	//SendPropEHandle(SENDINFO_MOVEPARENT(moveparent), 0, SendProxy_MoveParentToInt),

	//SendPropInt		(SENDINFO(m_iParentAttachment), NUM_PARENTATTACHMENT_BITS, SPROP_UNSIGNED),
END_SEND_TABLE()


BEGIN_DATADESC( CRopeKeyframe )


	DEFINE_KEYFIELD( m_iNextLinkName,	FIELD_STRING,	"NextKey" ),
	DEFINE_FIELD( m_bCreatedFromMapFile, FIELD_BOOLEAN ),
	// Inputs
	DEFINE_INPUTFUNC( FIELD_FLOAT,	"SetScrollSpeed",	InputSetScrollSpeed ),
	DEFINE_INPUTFUNC( FIELD_VECTOR,	"SetForce",			InputSetForce ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"Break",			InputBreak ),

END_DATADESC()



// -------------------------------------------------------------------------------- //
// CRopeKeyframe implementation.
// -------------------------------------------------------------------------------- //

CRopeKeyframe::CRopeKeyframe()
{
	m_takedamage = DAMAGE_YES;
	m_bCreatedFromMapFile = true;
}

void CRopeKeyframe::PostConstructor(const char* szClassname, int iForceEdictIndex) {
	GetEngineObject()->AddEFlags(EFL_FORCE_CHECK_TRANSMIT);
	BaseClass::PostConstructor(szClassname, iForceEdictIndex);
}

CRopeKeyframe::~CRopeKeyframe()
{

}

void CRopeKeyframe::UpdateOnRemove()
{
	BaseClass::UpdateOnRemove();
	// Release transmit state ownership.
	GetEngineRope()->SetStartPoint(NULL, 0);
	GetEngineRope()->SetEndPoint(NULL, 0);
	//GetEngineObject()->SetParent( NULL, 0 );
	GetEngineObject()->SetParent(NULL, 0);
}






void CRopeKeyframe::BeforeParentChanged( IServerEntity *pNewParent, int iNewAttachment )
{
	BaseClass::BeforeParentChanged(pNewParent, iNewAttachment);
	IEngineObjectServer *pCurParent = GetEngineObject()->GetMoveParent();
	if ( pCurParent )
	{
		pCurParent->GetOuter()->DecrementTransmitStateOwnedCounter();
		pCurParent->GetOuter()->DispatchUpdateTransmitState();
	}

	// Make sure our move parent always transmits or we get asserts on the client.
	if ( pNewParent	)
	{
		pNewParent->IncrementTransmitStateOwnedCounter();
		pNewParent->SetTransmitState( FL_EDICT_ALWAYS );
	}

	//BaseClass::SetParent( pNewParent, iAttachment );
}

void CRopeKeyframe::EnablePlayerWeaponAttach( bool bAttach )
{
	int newFlags = GetEngineRope()->GetRopeFlags();
	if ( bAttach )
		newFlags |= ROPE_PLAYER_WPN_ATTACH;
	else
		newFlags &= ~ROPE_PLAYER_WPN_ATTACH;

	if ( newFlags != GetEngineRope()->GetRopeFlags() )
	{
		GetEngineRope()->SetRopeFlags(newFlags);
	}
}


CRopeKeyframe* CRopeKeyframe::Create(
	CBaseEntity *pStartEnt,
	CBaseEntity *pEndEnt,
	int iStartAttachment,
	int iEndAttachment,
	int ropeWidth,
	const char *pMaterialName,
	int numSegments
	)
{
	CRopeKeyframe *pRet = (CRopeKeyframe*)EntityList()->CreateEntityByName( "keyframe_rope" );
	if( !pRet )
		return NULL;

	pRet->GetEngineRope()->SetStartPoint( pStartEnt, iStartAttachment );
	pRet->GetEngineRope()->SetEndPoint( pEndEnt, iEndAttachment );
	pRet->m_bCreatedFromMapFile = false;
	pRet->GetEngineRope()->SetRopeFlags(pRet->GetEngineRope()->GetRopeFlags() & ~ROPE_INITIAL_HANG);

	pRet->GetEngineRope()->Init();

	pRet->GetEngineRope()->SetMaterial( pMaterialName );
	pRet->GetEngineRope()->SetWidth(ropeWidth);
	pRet->GetEngineRope()->SetSegments(clamp( numSegments, 2, ROPE_MAX_SEGMENTS ));

	return pRet;
}


CRopeKeyframe* CRopeKeyframe::CreateWithSecondPointDetached(
	CBaseEntity *pStartEnt,
	int iStartAttachment,
	int ropeLength,
	int ropeWidth,
	const char *pMaterialName,
	int numSegments,
	bool bInitialHang
	)
{
	CRopeKeyframe *pRet = (CRopeKeyframe*)EntityList()->CreateEntityByName( "keyframe_rope" );
	if( !pRet )
		return NULL;

	pRet->GetEngineRope()->SetStartPoint( pStartEnt, iStartAttachment );
	pRet->GetEngineRope()->SetEndPoint( NULL, 0 );
	pRet->m_bCreatedFromMapFile = false;
	pRet->GetEngineRope()->SetLockedPoints( ROPE_LOCK_START_POINT ); // Only attach the first point.

	if( !bInitialHang )
	{
		pRet->GetEngineRope()->SetRopeFlags(pRet->GetEngineRope()->GetRopeFlags() & ~ROPE_INITIAL_HANG);
	}

	pRet->GetEngineRope()->Init();

	pRet->GetEngineRope()->SetMaterial( pMaterialName );
	pRet->GetEngineRope()->SetRopeLength(ropeLength);
	pRet->GetEngineRope()->SetWidth(ropeWidth);
	pRet->GetEngineRope()->SetSegments(clamp( numSegments, 2, ROPE_MAX_SEGMENTS ));

	return pRet;
}




void CRopeKeyframe::ShakeRopes( const Vector &vCenter, float flRadius, float flMagnitude )
{
	CEffectData shakeData;
	shakeData.m_vOrigin = vCenter;
	shakeData.m_flRadius = flRadius;
	shakeData.m_flMagnitude = flMagnitude;
	DispatchEffect( "ShakeRopes", shakeData );
}


void CRopeKeyframe::Activate()
{
	BaseClass::Activate();
	
	if( !m_bCreatedFromMapFile )
		return;

	// Legacy support..
	if (GetEngineRope()->GetRopeMaterialModelIndex() == -1)
		GetEngineRope()->SetRopeMaterialModelIndex(engine->PrecacheModel( "cable/cable.vmt" ));

	// Find the next entity in our chain.
	IServerEntity *pEnt = EntityList()->FindEntityByName( NULL, m_iNextLinkName );
	if( pEnt && pEnt->entindex()!=-1 )
	{
		GetEngineRope()->SetEndPoint( pEnt );

		if(GetEngineObject()->GetSpawnFlags() & SF_ROPE_RESIZE)
			GetEngineRope()->SetRopeFlags(GetEngineRope()->GetRopeFlags() | ROPE_RESIZE);
	}
	else
	{
		// If we're from the map file, and we don't have a target ent, and 
		// "Start Dangling" wasn't set, then this rope keyframe doesn't have
		// any rope coming out of it.
		if (GetEngineRope()->GetLockedPoints() & (int)ROPE_LOCK_END_POINT)
		{
			GetEngineRope()->SetRopeFlags(GetEngineRope()->GetRopeFlags() & ~ROPE_SIMULATE);
		}
	}

	// By default, our start point is our own entity.
	GetEngineRope()->SetStartPoint( this );

	// If we don't do this here, then when we save/load, we won't "own" the transmit 
	// state of our parent, so the client might get our entity without our parent entity.
	GetEngineObject()->SetParent(GetEngineObject()->GetMoveParent(), GetEngineObject()->GetParentAttachment());

	GetEngineRope()->EndpointsChanged();

	GetEngineRope()->Init();
}

//-----------------------------------------------------------------------------
// Purpose: This should remove the rope next time it reaches a resting state.
//			Right now only the client knows when it reaches a resting state, so
//			for now it just removes itself after a short time.
//-----------------------------------------------------------------------------
void CRopeKeyframe::DieAtNextRest( void )
{
	SetThink( &CBaseEntity::SUB_Remove );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 1.0f );
}


void CRopeKeyframe::SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways )
{
	if ( !pInfo->m_pTransmitEdict->Get( entindex() ) )
	{	
		BaseClass::SetTransmit( pInfo, bAlways );
	
		// Make sure our target ents are sent too.
		CBaseEntity *pEnt = (CBaseEntity*)GetEngineRope()->GetStartPoint();
		if ( pEnt )
			pEnt->SetTransmit( pInfo, bAlways );

		pEnt = (CBaseEntity*)GetEngineRope()->GetEndPoint();
		if ( pEnt )
			pEnt->SetTransmit( pInfo, bAlways );
	}
}


//------------------------------------------------------------------------------
// Purpose : Propagate force to each link in the rope.  Check for loops
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CRopeKeyframe::PropagateForce(IServerEntity *pActivator, IServerEntity *pCaller, CBaseEntity *pFirstLink, float x, float y, float z)
{
	EntityMessageBegin( this, true );
		WRITE_FLOAT( x );
		WRITE_FLOAT( y );
		WRITE_FLOAT( z );
	MessageEnd();

	// UNDONE: Doesn't deal with intermediate loops
	// Propagate to next segment
	CRopeKeyframe *pNextLink = dynamic_cast<CRopeKeyframe*>((CBaseEntity *)GetEngineRope()->GetEndPoint());
	if (pNextLink && pNextLink != pFirstLink)
	{
		pNextLink->PropagateForce(pActivator, pCaller, pFirstLink, x, y, z);
	}
}

//------------------------------------------------------------------------------
// Purpose: Set an instaneous force on the rope.
// Input  : Force vector.
//------------------------------------------------------------------------------
void CRopeKeyframe::InputSetForce( inputdata_t &inputdata )
{
	Vector vecForce;
	inputdata.value.Vector3D(vecForce);
	PropagateForce(inputdata.pActivator, inputdata.pCaller, this, vecForce.x, vecForce.y, vecForce.z );
}

//-----------------------------------------------------------------------------
// Purpose: Breaks the rope if able
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CRopeKeyframe::InputBreak( inputdata_t &inputdata )
{
	//Route through the damage code
	Break();
}

//-----------------------------------------------------------------------------
// Purpose: Breaks the rope
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CRopeKeyframe::Break( void )
{
	GetEngineRope()->DetachPoint( 0 );

	// Find whoever references us and detach us from them.
	// UNDONE: PERFORMANCE: This is very slow!!!
	CRopeKeyframe *pTest = NULL;
	pTest = NextEntByClass( pTest );
	while ( pTest )
	{
		if( stricmp( STRING(pTest->m_iNextLinkName), STRING(GetEntityName()) ) == 0 )
		{
			pTest->GetEngineRope()->DetachPoint( 1 );
		}
	
		pTest = NextEntByClass( pTest );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CRopeKeyframe::NotifyPositionChanged( CBaseEntity *pEntity )
{
	GetEngineRope()->NotifyPositionChanged();
}

//-----------------------------------------------------------------------------
// Purpose: Take damage will break the rope
//-----------------------------------------------------------------------------
int CRopeKeyframe::OnTakeDamage( const CTakeDamageInfo &info )
{
	// Only allow this if it's been marked
	if( !(GetEngineRope()->GetRopeFlags() & ROPE_BREAKABLE))
		return false;

	Break();
	return 0;
}


void CRopeKeyframe::Precache()
{
	GetEngineRope()->SetRopeMaterialModelIndex(engine->PrecacheModel( GetEngineRope()->GetMaterialName()));
	BaseClass::Precache();
}

bool CRopeKeyframe::KeyValue( const char *szKeyName, const char *szValue )
{
	if( stricmp( szKeyName, "Breakable" ) == 0 )
	{
		if( atoi( szValue ) == 1 )
			GetEngineRope()->SetRopeFlags(GetEngineRope()->GetRopeFlags() | ROPE_BREAKABLE);
	}
	else if( stricmp( szKeyName, "Collide" ) == 0 )
	{
		if( atoi( szValue ) == 1 )
			GetEngineRope()->SetRopeFlags(GetEngineRope()->GetRopeFlags() | ROPE_COLLIDE);
	}
	else if( stricmp( szKeyName, "Barbed" ) == 0 )
	{
		if( atoi( szValue ) == 1 )
			GetEngineRope()->SetRopeFlags(GetEngineRope()->GetRopeFlags() | ROPE_BARBED);
	}
	else if( stricmp( szKeyName, "Dangling" ) == 0 )
	{
		if( atoi( szValue ) == 1 )
			GetEngineRope()->SetLockedPoints(GetEngineRope()->GetLockedPoints() & ~ROPE_LOCK_END_POINT); // detach our dest point
		
		return true;
	}
	else if( stricmp( szKeyName, "Type" ) == 0 )
	{
		int iType = atoi( szValue );
		if( iType == 0 )
			GetEngineRope()->SetSegments(ROPE_MAX_SEGMENTS);
		else if( iType == 1 )
			GetEngineRope()->SetSegments(ROPE_TYPE1_NUMSEGMENTS);
		else
			GetEngineRope()->SetSegments(ROPE_TYPE2_NUMSEGMENTS);
	}
	else if ( stricmp( szKeyName, "RopeShader" ) == 0 )
	{
		// Legacy support for the RopeShader parameter.
		int iShader = atoi( szValue );
		if ( iShader == 0 )
		{
			GetEngineRope()->SetRopeMaterialModelIndex(engine->PrecacheModel( "cable/cable.vmt" ));
		}
		else if ( iShader == 1 )
		{
			GetEngineRope()->SetRopeMaterialModelIndex(engine->PrecacheModel( "cable/rope.vmt" ));
		}
		else
		{
			GetEngineRope()->SetRopeMaterialModelIndex(engine->PrecacheModel( "cable/chain.vmt" ));
		}
	}
	else if ( stricmp( szKeyName, "RopeMaterial" ) == 0 )
	{
		// Make sure we have a vmt extension.
		if ( Q_stristr( szValue, ".vmt" ) )
		{
			GetEngineRope()->SetMaterial( szValue );
		}
		else
		{
			char str[512];
			Q_snprintf( str, sizeof( str ), "%s.vmt", szValue );
			GetEngineRope()->SetMaterial( str );
		}
	}
	else if ( stricmp( szKeyName, "NoWind" ) == 0 )
	{
		if ( atoi( szValue ) == 1 )
		{
			GetEngineRope()->SetRopeFlags(GetEngineRope()->GetRopeFlags() | ROPE_NO_WIND);
		}
	}
	
	return BaseClass::KeyValue( szKeyName, szValue );
}


//-----------------------------------------------------------------------------
// Purpose: Input handler that sets the scroll speed.
//-----------------------------------------------------------------------------
void CRopeKeyframe::InputSetScrollSpeed( inputdata_t &inputdata )
{
	GetEngineRope()->SetScrollSpeed(inputdata.value.Float());
}


int CRopeKeyframe::UpdateTransmitState()
{
	// Certain entities like sprites and ropes are strewn throughout the level and they rarely change.
	// For these entities, it's more efficient to transmit them once and then always leave them on
	// the client. Otherwise, the server will have to send big bursts of data with the entity states
	// as they come in and out of the PVS.
	return SetTransmitState( FL_EDICT_ALWAYS );
}



		
