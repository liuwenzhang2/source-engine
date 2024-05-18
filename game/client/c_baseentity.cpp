//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "c_baseentity.h"
#include "prediction.h"
#include "model_types.h"
#include "iviewrender_beams.h"
#include "dlight.h"
#include "iviewrender.h"
#include "view.h"
#include "iefx.h"
#include "c_team.h"
#include "clientmode.h"
#include "usercmd.h"
#include "engine/IEngineSound.h"
#include "engine/IEngineTrace.h"
#include "engine/ivmodelinfo.h"
#include "tier0/vprof.h"
#include "fx_line.h"
#include "interface.h"
#include "materialsystem/imaterialsystem.h"
#include "soundinfo.h"
#include "mathlib/vmatrix.h"
#include "isaverestore.h"
#include "interval.h"
#include "engine/ivdebugoverlay.h"
#include "c_ai_basenpc.h"
#include "apparent_velocity_helper.h"
#include "c_baseanimatingoverlay.h"
#include "tier1/KeyValues.h"
#include "hltvcamera.h"
#include "datacache/imdlcache.h"
#include "toolframework/itoolframework.h"
#include "toolframework_client.h"
#include "decals.h"
#include "cdll_bounded_cvars.h"
#include "inetchannelinfo.h"
#include "proto_version.h"
#include "predictioncopy.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#ifdef INTERPOLATEDVAR_PARANOID_MEASUREMENT
	int g_nInterpolatedVarsChanged = 0;
	bool g_bRestoreInterpolatedVarValues = false;
#endif


static bool g_bWasSkipping = (bool)-1;
static bool g_bWasThreaded =(bool)-1;
static int  g_nThreadModeTicks = 0;


static ConVar  cl_extrapolate( "cl_extrapolate", "1", FCVAR_CHEAT, "Enable/disable extrapolation if interpolation history runs out." );
static ConVar  cl_interp_npcs( "cl_interp_npcs", "0.0", FCVAR_USERINFO, "Interpolate NPC positions starting this many seconds in past (or cl_interp, if greater)" );  
ConVar  r_drawmodeldecals( "r_drawmodeldecals", "1" );
extern ConVar	cl_showerror;
int C_BaseEntity::m_nPredictionRandomSeed = -1;
C_BasePlayer *C_BaseEntity::m_pPredictionPlayer = NULL;
bool C_BaseEntity::s_bAbsQueriesValid = true;
bool C_BaseEntity::s_bAbsRecomputationEnabled = true;
bool C_BaseEntity::s_bInterpolate = true;


static ConVar  r_drawrenderboxes( "r_drawrenderboxes", "0", FCVAR_CHEAT );  

static bool g_bAbsRecomputationStack[8];
static unsigned short g_iAbsRecomputationStackPos = 0;

// All the entities that want Interpolate() called on them.
static CUtlLinkedList<C_BaseEntity*, unsigned short> g_InterpolationList;
static CUtlLinkedList<C_BaseEntity*, unsigned short> g_TeleportList;



abstract_class IRecordingList
{
public:
	virtual ~IRecordingList() {};
	virtual void	AddToList( ClientEntityHandle_t add ) = 0;
	virtual void	RemoveFromList( ClientEntityHandle_t remove ) = 0;

	virtual int		Count() = 0;
	virtual IClientRenderable *Get( int index ) = 0;
};

class CRecordingList : public IRecordingList
{
public:
	virtual void	AddToList( ClientEntityHandle_t add );
	virtual void	RemoveFromList( ClientEntityHandle_t remove );

	virtual int		Count();
	IClientRenderable *Get( int index );
private:
	CUtlVector< ClientEntityHandle_t > m_Recording;
};

static CRecordingList g_RecordingList;
IRecordingList *recordinglist = &g_RecordingList;

//-----------------------------------------------------------------------------
// Purpose: Add entity to list
// Input  : add - 
// Output : int
//-----------------------------------------------------------------------------
void CRecordingList::AddToList( ClientEntityHandle_t add )
{
	// This is a hack to remap slot to index
	if ( m_Recording.Find( add ) != m_Recording.InvalidIndex() )
	{
		return;
	}

	// Add to general list
	m_Recording.AddToTail( add );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : remove - 
//-----------------------------------------------------------------------------
void CRecordingList::RemoveFromList( ClientEntityHandle_t remove )
{
	m_Recording.FindAndRemove( remove );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : slot - 
// Output : IClientRenderable
//-----------------------------------------------------------------------------
IClientRenderable *CRecordingList::Get( int index )
{
	return cl_entitylist->GetClientRenderableFromHandle( m_Recording[ index ] );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CRecordingList::Count()
{
	return m_Recording.Count();
}

// Should these be somewhere else?
#define PITCH 0

//-----------------------------------------------------------------------------
// Purpose: Decodes animtime and notes when it changes
// Input  : *pStruct - ( C_BaseEntity * ) used to flag animtime is changine
//			*pVarData - 
//			*pIn - 
//			objectID - 
//-----------------------------------------------------------------------------
void RecvProxy_AnimTime( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	C_BaseEntity *pEntity = ( C_BaseEntity * )pStruct;
	Assert( pOut == &pEntity->m_flAnimTime );

	int t;
	int tickbase;
	int addt;

	// Unpack the data.
	addt	= pData->m_Value.m_Int;

	// Note, this needs to be encoded relative to packet timestamp, not raw client clock
	tickbase = gpGlobals->GetNetworkBase( gpGlobals->tickcount, pEntity->entindex() );

	t = tickbase;
											//  and then go back to floating point time.
	t += addt;				// Add in an additional up to 256 100ths from the server

	// center m_flAnimTime around current time.
	while (t < gpGlobals->tickcount - 127)
		t += 256;
	while (t > gpGlobals->tickcount + 127)
		t -= 256;
	
	pEntity->m_flAnimTime = ( t * TICK_INTERVAL );
}

void RecvProxy_SimulationTime( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	C_BaseEntity *pEntity = ( C_BaseEntity * )pStruct;
	Assert( pOut == &pEntity->m_flSimulationTime );

	int t;
	int tickbase;
	int addt;

	// Unpack the data.
	addt	= pData->m_Value.m_Int;

	// Note, this needs to be encoded relative to packet timestamp, not raw client clock
	tickbase = gpGlobals->GetNetworkBase( gpGlobals->tickcount, pEntity->entindex() );

	t = tickbase;
											//  and then go back to floating point time.
	t += addt;				// Add in an additional up to 256 100ths from the server

	// center m_flSimulationTime around current time.
	while (t < gpGlobals->tickcount - 127)
		t += 256;
	while (t > gpGlobals->tickcount + 127)
		t -= 256;
	
	pEntity->m_flSimulationTime = ( t * TICK_INTERVAL );
}

void RecvProxy_ToolRecording( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	if ( !ToolsEnabled() )
		return;

	CBaseEntity *pEnt = (CBaseEntity *)pStruct;
	pEnt->SetToolRecording( pData->m_Value.m_Int != 0 );
}

// Expose it to the engine.
IMPLEMENT_CLIENTCLASS(C_BaseEntity, DT_BaseEntity, CBaseEntity);

static void RecvProxy_MoveType( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	((C_BaseEntity*)pStruct)->SetMoveType( (MoveType_t)(pData->m_Value.m_Int) );
}

static void RecvProxy_MoveCollide( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	((C_BaseEntity*)pStruct)->SetMoveCollide( (MoveCollide_t)(pData->m_Value.m_Int) );
}

static void RecvProxy_Solid( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	((C_BaseEntity*)pStruct)->SetSolid( (SolidType_t)pData->m_Value.m_Int );
}

static void RecvProxy_SolidFlags( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	((C_BaseEntity*)pStruct)->SetSolidFlags( pData->m_Value.m_Int );
}

void RecvProxy_EffectFlags( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	((C_BaseEntity*)pStruct)->SetEffects( pData->m_Value.m_Int );
}


BEGIN_RECV_TABLE_NOBASE( C_BaseEntity, DT_AnimTimeMustBeFirst )
	RecvPropInt( RECVINFO(m_flAnimTime), 0, RecvProxy_AnimTime ),
END_RECV_TABLE()


//#ifndef NO_ENTITY_PREDICTION
//BEGIN_RECV_TABLE_NOBASE( C_BaseEntity, DT_PredictableId )
//	RecvPropPredictableId( RECVINFO( m_PredictableID ) ),
//	RecvPropInt( RECVINFO( m_bIsPlayerSimulated ) ),
//END_RECV_TABLE()
//#endif


BEGIN_RECV_TABLE_NOBASE(C_BaseEntity, DT_BaseEntity)
	RecvPropDataTable( "AnimTimeMustBeFirst", 0, 0, &REFERENCE_RECV_TABLE(DT_AnimTimeMustBeFirst) ),
	RecvPropInt( RECVINFO(m_flSimulationTime), 0, RecvProxy_SimulationTime ),
	RecvPropInt( RECVINFO( m_ubInterpolationFrame ) ),

#ifdef DEMO_BACKWARDCOMPATABILITY
	RecvPropInt( RECVINFO(m_nModelIndex), 0, RecvProxy_IntToModelIndex16_BackCompatible ),
#else
	RecvPropInt( RECVINFO(m_nModelIndex) ),
#endif

	RecvPropInt(RECVINFO(m_fEffects), 0, RecvProxy_EffectFlags ),
	RecvPropInt(RECVINFO(m_nRenderMode)),
	RecvPropInt(RECVINFO(m_nRenderFX)),
	RecvPropInt(RECVINFO(m_clrRender)),
	RecvPropInt(RECVINFO(m_iTeamNum)),
	RecvPropInt(RECVINFO(m_CollisionGroup)),
	RecvPropFloat(RECVINFO(m_flElasticity)),
	RecvPropFloat(RECVINFO(m_flShadowCastDistance)),
	RecvPropEHandle( RECVINFO(m_hOwnerEntity) ),
	RecvPropEHandle( RECVINFO(m_hEffectEntity) ),

	RecvPropInt( "movetype", 0, SIZEOF_IGNORE, 0, RecvProxy_MoveType ),
	RecvPropInt( "movecollide", 0, SIZEOF_IGNORE, 0, RecvProxy_MoveCollide ),
	RecvPropDataTable( RECVINFO_DT( m_Collision ), 0, &REFERENCE_RECV_TABLE(DT_CollisionProperty) ),
	
	RecvPropInt( RECVINFO ( m_iTextureFrameIndex ) ),
//#if !defined( NO_ENTITY_PREDICTION )
//	RecvPropDataTable( "predictable_id", 0, 0, &REFERENCE_RECV_TABLE( DT_PredictableId ) ),
//#endif

	RecvPropInt		( RECVINFO( m_bSimulatedEveryTick ), 0, RecvProxy_InterpolationAmountChanged ),
	RecvPropInt		( RECVINFO( m_bAnimatedEveryTick ), 0, RecvProxy_InterpolationAmountChanged ),
	RecvPropBool	( RECVINFO( m_bAlternateSorting ) ),

#ifdef TF_CLIENT_DLL
	RecvPropArray3( RECVINFO_ARRAY(m_nModelIndexOverrides),	RecvPropInt( RECVINFO(m_nModelIndexOverrides[0]) ) ),
#endif

END_RECV_TABLE()


BEGIN_PREDICTION_DATA_NO_BASE( C_BaseEntity )

	// These have a special proxy to handle send/receive
	DEFINE_PRED_TYPEDESCRIPTION( m_Collision, CCollisionProperty ),

	DEFINE_PRED_FIELD( m_MoveType, FIELD_CHARACTER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_MoveCollide, FIELD_CHARACTER, FTYPEDESC_INSENDTABLE ),

	//DEFINE_FIELD( m_vecAbsVelocity, FIELD_VECTOR ),
	//DEFINE_PRED_FIELD_TOL( m_vecVelocity, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.5f ),
//	DEFINE_PRED_FIELD( m_fEffects, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_nRenderMode, FIELD_CHARACTER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_nRenderFX, FIELD_CHARACTER, FTYPEDESC_INSENDTABLE ),
//	DEFINE_PRED_FIELD( m_flAnimTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
//	DEFINE_PRED_FIELD( m_flSimulationTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fFlags, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD_TOL( m_vecViewOffset, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.25f ),
	DEFINE_PRED_FIELD( m_nModelIndex, FIELD_SHORT, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD( m_flFriction, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_iTeamNum, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_hOwnerEntity, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),

//	DEFINE_FIELD( m_nSimulationTick, FIELD_INTEGER ),

//	DEFINE_PRED_FIELD( m_pMoveParent, FIELD_EHANDLE ),
//	DEFINE_PRED_FIELD( m_pMoveChild, FIELD_EHANDLE ),
//	DEFINE_PRED_FIELD( m_pMovePeer, FIELD_EHANDLE ),
//	DEFINE_PRED_FIELD( m_pMovePrevPeer, FIELD_EHANDLE ),


	//DEFINE_FIELD( m_vecAbsOrigin, FIELD_VECTOR ),
	//DEFINE_FIELD( m_angAbsRotation, FIELD_VECTOR ),
	//DEFINE_FIELD( m_vecOrigin, FIELD_VECTOR ),
	//DEFINE_FIELD( m_angRotation, FIELD_VECTOR ),

//	DEFINE_FIELD( m_hGroundEntity, FIELD_EHANDLE ),
	DEFINE_FIELD( m_nWaterLevel, FIELD_CHARACTER ),
	DEFINE_FIELD( m_nWaterType, FIELD_CHARACTER ),
	DEFINE_FIELD( m_vecAngVelocity, FIELD_VECTOR ),
//	DEFINE_FIELD( m_vecAbsAngVelocity, FIELD_VECTOR ),


//	DEFINE_FIELD( model, FIELD_INTEGER ), // writing pointer literally
//	DEFINE_FIELD( index, FIELD_INTEGER ),
//	DEFINE_FIELD( m_ClientHandle, FIELD_SHORT ),
//	DEFINE_FIELD( m_Partition, FIELD_SHORT ),
//	DEFINE_FIELD( m_hRender, FIELD_SHORT ),
	DEFINE_FIELD( m_bDormant, FIELD_BOOLEAN ),
//	DEFINE_FIELD( current_position, FIELD_INTEGER ),
//	DEFINE_FIELD( m_flLastMessageTime, FIELD_FLOAT ),
	DEFINE_FIELD( m_vecBaseVelocity, FIELD_VECTOR ),
	DEFINE_FIELD( m_flGravity, FIELD_FLOAT ),
//	DEFINE_FIELD( m_ModelInstance, FIELD_SHORT ),
	DEFINE_FIELD( m_flProxyRandomValue, FIELD_FLOAT ),

//	DEFINE_FIELD( m_PredictableID, FIELD_INTEGER ),
//	DEFINE_FIELD( m_pPredictionContext, FIELD_POINTER ),
	// Stuff specific to rendering and therefore not to be copied back and forth
	// DEFINE_PRED_FIELD( m_clrRender, color32, FTYPEDESC_INSENDTABLE  ),
	// DEFINE_FIELD( m_bReadyToDraw, FIELD_BOOLEAN ),
	// DEFINE_FIELD( anim, CLatchedAnim ),
	// DEFINE_FIELD( mouth, CMouthInfo ),
	// DEFINE_FIELD( GetAbsOrigin(), FIELD_VECTOR ),
	// DEFINE_FIELD( GetAbsAngles(), FIELD_VECTOR ),
	// DEFINE_FIELD( m_nNumAttachments, FIELD_SHORT ),
	// DEFINE_FIELD( m_pAttachmentAngles, FIELD_VECTOR ),
	// DEFINE_FIELD( m_pAttachmentOrigin, FIELD_VECTOR ),
	// DEFINE_FIELD( m_listentry, CSerialEntity ),
	// DEFINE_FIELD( m_ShadowHandle, ClientShadowHandle_t ),
	// DEFINE_FIELD( m_hThink, ClientThinkHandle_t ),
	// Definitely private and not copied around
	// DEFINE_FIELD( m_bPredictable, FIELD_BOOLEAN ),
	// DEFINE_FIELD( m_CollisionGroup, FIELD_INTEGER ),
	// DEFINE_FIELD( m_DataChangeEventRef, FIELD_INTEGER ),
#if !defined( CLIENT_DLL )
	// DEFINE_FIELD( m_bPredictionEligible, FIELD_BOOLEAN ),
#endif
END_PREDICTION_DATA()

//-----------------------------------------------------------------------------
// Helper functions.
//-----------------------------------------------------------------------------

void SpewInterpolatedVar( CInterpolatedVar< Vector > *pVar )
{
	Msg( "--------------------------------------------------\n" );
	int i = pVar->GetHead();
	CApparentVelocity<Vector> apparent;
	float prevtime = 0.0f;
	while ( 1 )
	{
		float changetime;
		Vector *pVal = pVar->GetHistoryValue( i, changetime );
		if ( !pVal )
			break;

		float vel = apparent.AddSample( changetime, *pVal );
		Msg( "%6.6f: (%.2f %.2f %.2f), vel: %.2f [dt %.1f]\n", changetime, VectorExpand( *pVal ), vel, prevtime == 0.0f ? 0.0f : 1000.0f * ( changetime - prevtime ) );
		i = pVar->GetNext( i );
		prevtime = changetime;
	}
	Msg( "--------------------------------------------------\n" );
}

void SpewInterpolatedVar( CInterpolatedVar< Vector > *pVar, float flNow, float flInterpAmount, bool bSpewAllEntries = true )
{
	float target = flNow - flInterpAmount;

	Msg( "--------------------------------------------------\n" );
	int i = pVar->GetHead();
	CApparentVelocity<Vector> apparent;
	float newtime = 999999.0f;
	Vector newVec( 0, 0, 0 );
	bool bSpew = true;

	while ( 1 )
	{
		float changetime;
		Vector *pVal = pVar->GetHistoryValue( i, changetime );
		if ( !pVal )
			break;

		if ( bSpew && target >= changetime )
		{
			Vector o;
			pVar->DebugInterpolate( &o, flNow );
			bool bInterp = newtime != 999999.0f;
			float frac = 0.0f;
			char desc[ 32 ];

			if ( bInterp )
			{
				frac = ( target - changetime ) / ( newtime - changetime );
				Q_snprintf( desc, sizeof( desc ), "interpolated [%.2f]", frac );
			}
			else
			{
				bSpew = true;
				int savei = i;
				i = pVar->GetNext( i );
				float oldtertime = 0.0f;
				pVar->GetHistoryValue( i, oldtertime );

				if ( changetime != oldtertime )
				{
					frac = ( target - changetime ) / ( changetime - oldtertime );
				}

				Q_snprintf( desc, sizeof( desc ), "extrapolated [%.2f]", frac );
				i = savei;
			}

			if ( bSpew )
			{
				Msg( "  > %6.6f: (%.2f %.2f %.2f) %s for %.1f msec\n", 
					target, 
					VectorExpand( o ), 
					desc,
					1000.0f * ( target - changetime ) );
				bSpew = false;
			}
		}

		float vel = apparent.AddSample( changetime, *pVal );
		if ( bSpewAllEntries )
		{
			Msg( "    %6.6f: (%.2f %.2f %.2f), vel: %.2f [dt %.1f]\n", changetime, VectorExpand( *pVal ), vel, newtime == 999999.0f ? 0.0f : 1000.0f * ( newtime - changetime ) );
		}
		i = pVar->GetNext( i );
		newtime = changetime;
		newVec = *pVal;
	}
	Msg( "--------------------------------------------------\n" );
}
void SpewInterpolatedVar( CInterpolatedVar< float > *pVar )
{
	Msg( "--------------------------------------------------\n" );
	int i = pVar->GetHead();
	CApparentVelocity<float> apparent;
	while ( 1 )
	{
		float changetime;
		float *pVal = pVar->GetHistoryValue( i, changetime );
		if ( !pVal )
			break;

		float vel = apparent.AddSample( changetime, *pVal );
		Msg( "%6.6f: (%.2f), vel: %.2f\n", changetime, *pVal, vel );
		i = pVar->GetNext( i );
	}
	Msg( "--------------------------------------------------\n" );
}

template<class T>
void GetInterpolatedVarTimeRange( CInterpolatedVar<T> *pVar, float &flMin, float &flMax )
{
	flMin = 1e23;
	flMax = -1e23;

	int i = pVar->GetHead();
	CApparentVelocity<Vector> apparent;
	while ( 1 )
	{
		float changetime;
		if ( !pVar->GetHistoryValue( i, changetime ) )
			return;

		flMin = MIN( flMin, changetime );
		flMax = MAX( flMax, changetime );
		i = pVar->GetNext( i );
	}
}


//-----------------------------------------------------------------------------
// Global methods related to when abs data is correct
//-----------------------------------------------------------------------------
void C_BaseEntity::SetAbsQueriesValid( bool bValid )
{
	// @MULTICORE: Always allow in worker threads, assume higher level code is handling correctly
	if ( !ThreadInMainThread() )
		return;

	if ( !bValid )
	{
		s_bAbsQueriesValid = false;
	}
	else
	{
		s_bAbsQueriesValid = true;
	}
}

bool C_BaseEntity::IsAbsQueriesValid( void )
{
	if ( !ThreadInMainThread() )
		return true;
	return s_bAbsQueriesValid;
}

void C_BaseEntity::PushEnableAbsRecomputations( bool bEnable )
{
	if ( !ThreadInMainThread() )
		return;
	if ( g_iAbsRecomputationStackPos < ARRAYSIZE( g_bAbsRecomputationStack ) )
	{
		g_bAbsRecomputationStack[g_iAbsRecomputationStackPos] = s_bAbsRecomputationEnabled;
		++g_iAbsRecomputationStackPos;
		s_bAbsRecomputationEnabled = bEnable;
	}
	else
	{
		Assert( false );
	}
}

void C_BaseEntity::PopEnableAbsRecomputations()
{
	if ( !ThreadInMainThread() )
		return;
	if ( g_iAbsRecomputationStackPos > 0 )
	{
		--g_iAbsRecomputationStackPos;
		s_bAbsRecomputationEnabled = g_bAbsRecomputationStack[g_iAbsRecomputationStackPos];
	}
	else
	{
		Assert( false );
	}
}

void C_BaseEntity::EnableAbsRecomputations( bool bEnable )
{
	if ( !ThreadInMainThread() )
		return;
	// This should only be called at the frame level. Use PushEnableAbsRecomputations
	// if you're blocking out a section of code.
	Assert( g_iAbsRecomputationStackPos == 0 );

	s_bAbsRecomputationEnabled = bEnable;
}

bool C_BaseEntity::IsAbsRecomputationsEnabled()
{
	if ( !ThreadInMainThread() )
		return true;
	return s_bAbsRecomputationEnabled;
}

int	C_BaseEntity::GetTextureFrameIndex( void )
{
	return m_iTextureFrameIndex;
}

void C_BaseEntity::SetTextureFrameIndex( int iIndex )
{
	m_iTextureFrameIndex = iIndex;
}



//-----------------------------------------------------------------------------
// Functions.
//-----------------------------------------------------------------------------
C_BaseEntity::C_BaseEntity()
{
	m_DataChangeEventRef = -1;
	m_EntClientFlags = 0;
	m_bEnableRenderingClipPlane = false;

	m_nRenderFXBlend = 255;

	//SetPredictionEligible( false );
	m_bPredictable = false;

	m_bSimulatedEveryTick = false;
	m_bAnimatedEveryTick = false;
	m_pPhysicsObject = NULL;
	//GetEngineObject()->Init(this);
#ifdef _DEBUG
	m_vecViewOffset.Init();
	m_vecBaseVelocity.Init();

	m_iCurrentThinkContext = NO_THINK_CONTEXT;

#endif

	m_nSimulationTick = -1;

	// Assume drawing everything
	m_bReadyToDraw = true;
	m_flProxyRandomValue = 0.0f;

	m_fBBoxVisFlags = 0;
//#if !defined( NO_ENTITY_PREDICTION )
//	m_pPredictionContext = NULL;
//#endif
	//NOTE: not virtual! we are in the constructor!
	C_BaseEntity::Clear();

	SetModelName( NULL_STRING );

	m_InterpolationListEntry = 0xFFFF;
	m_TeleportListEntry = 0xFFFF;

#ifndef NO_TOOLFRAMEWORK
	m_bEnabledInToolView = true;
	m_bToolRecording = false;
	m_ToolHandle = 0;
	m_nLastRecordedFrame = -1;
	m_bRecordInTools = true;
#endif

#ifdef TF_CLIENT_DLL
	m_bValidatedOwner = false;
	m_bDeemedInvalid = false;
	m_bWasDeemedInvalid = false;
#endif

	ParticleProp()->Init( this );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : 
// Output : 
//-----------------------------------------------------------------------------
C_BaseEntity::~C_BaseEntity()
{
	
}

IEngineObjectClient* C_BaseEntity::GetEngineObject() {
	return ClientEntityList().GetEngineObject(entindex());
}
const IEngineObjectClient* C_BaseEntity::GetEngineObject() const {
	return ClientEntityList().GetEngineObject(entindex());
}

void C_BaseEntity::Clear( void )
{
	m_bDormant = true;
	m_nCreationTick = -1;
	//m_RefEHandle.Term();
	m_ModelInstance = MODEL_INSTANCE_INVALID;
	m_ShadowHandle = CLIENTSHADOW_INVALID_HANDLE;
	m_hRender = INVALID_CLIENT_RENDER_HANDLE;
	m_hThink = INVALID_THINK_HANDLE;
	m_AimEntsListHandle = INVALID_AIMENTS_LIST_HANDLE;

	//index = -1;
	m_Collision.Init( this );
	model = NULL;
	if (entindex() >= 0) {
		GetEngineObject()->SetLocalOrigin(vec3_origin);
		GetEngineObject()->SetLocalAngles(vec3_angle);
		GetEngineObject()->Clear();
	}
	ClearFlags();
	m_vecViewOffset.Init();
	m_vecBaseVelocity.Init();
	m_nModelIndex = 0;
	m_flAnimTime = 0;
	m_flSimulationTime = 0;
	SetSolid( SOLID_NONE );
	SetSolidFlags( 0 );
	SetMoveCollide( MOVECOLLIDE_DEFAULT );
	SetMoveType( MOVETYPE_NONE );

	ClearEffects();
	m_nRenderMode = 0;
	m_nOldRenderMode = 0;
	SetRenderColor( 255, 255, 255, 255 );
	m_nRenderFX = 0;
	m_flFriction = 0.0f;       
	m_flGravity = 0.0f;
	m_ShadowDirUseOtherEntity = NULL;

	m_nLastThinkTick = gpGlobals->tickcount;

#if defined(SIXENSE)
	m_vecEyeOffset.Init();
	m_EyeAngleOffset.Init();
#endif

	// Remove prediction context if it exists
//#if !defined( NO_ENTITY_PREDICTION )
//	delete m_pPredictionContext;
//	m_pPredictionContext = NULL;
//#endif
	// Do not enable this on all entities. It forces bone setup for entities that
	// don't need it.
	//AddEFlags( EFL_USE_PARTITION_WHEN_NOT_SOLID );

	UpdateVisibility();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::Spawn( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::Activate()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::SpawnClientEntity( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::Precache( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Attach to entity
// Input  : *pEnt - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_BaseEntity::Init( int entnum, int iSerialNum )
{
	Assert( entnum >= 0 && entnum < NUM_ENT_ENTRIES );

	//index = entnum;

	if (entnum >= 0) {
		//cl_entitylist->AddNetworkableEntity(this, entnum, iSerialNum);//GetIClientUnknown()
	}

	CollisionProp()->CreatePartitionHandle();

	m_nCreationTick = gpGlobals->tickcount;

	return true;
}

void C_BaseEntity::AfterInit() {
	GetEngineObject()->Interp_SetupMappings(GetEngineObject()->GetVarMapping());
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool C_BaseEntity::InitializeAsClientEntity( const char *pszModelName, RenderGroup_t renderGroup )
{
	int nModelIndex;

	if ( pszModelName != NULL )
	{
		nModelIndex = modelinfo->GetModelIndex( pszModelName );
		
		if ( nModelIndex == -1 )
		{
			// Model could not be found
			Assert( !"Model could not be found, index is -1" );
			return false;
		}
	}
	else
	{
		nModelIndex = -1;
	}

	GetEngineObject()->Interp_SetupMappings(GetEngineObject()->GetVarMapping() );

	return InitializeAsClientEntityByIndex( nModelIndex, renderGroup );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool C_BaseEntity::InitializeAsClientEntityByIndex( int iIndex, RenderGroup_t renderGroup )
{
	//index = -1;

	// Setup model data.
	SetModelByIndex( iIndex );

	// Add the client entity to the master entity list.
	//cl_entitylist->AddNonNetworkableEntity( this );//GetIClientUnknown()
	Assert( GetClientHandle() != ClientEntityList().InvalidHandle() );

	// Add the client entity to the renderable "leaf system." (Renderable)
	AddToLeafSystem( renderGroup );

	// Add the client entity to the spatial partition. (Collidable)
	CollisionProp()->CreatePartitionHandle();

	SpawnClientEntity();

	return true;
}


void C_BaseEntity::Term()
{
#if !defined( NO_ENTITY_PREDICTION )
	// Remove from the predictables list
	if ( GetPredictable() /*|| IsClientCreated()*/)
	{
		predictables->RemoveFromPredictablesList( GetClientHandle() );
	}

	// If it's play simulated, remove from simulation list if the player still exists...
	//if ( IsPlayerSimulated() && C_BasePlayer::GetLocalPlayer() )
	//{
	//	C_BasePlayer::GetLocalPlayer()->RemoveFromPlayerSimulationList( this );
	//}
#endif

	if ( GetClientHandle() != INVALID_CLIENTENTITY_HANDLE )
	{
		if ( GetThinkHandle() != INVALID_THINK_HANDLE )
		{
			ClientThinkList()->RemoveThinkable( GetClientHandle() );
		}

		// Remove from the client entity list.
		//ClientEntityList().RemoveEntity( this );

		//m_RefEHandle = INVALID_CLIENTENTITY_HANDLE;
	}
	
	// Are we in the partition?
	CollisionProp()->DestroyPartitionHandle();

	// If Client side only entity index will be -1
	if ( entindex() != -1 )
	{
		beams->KillDeadBeams( this );
	}

	// Clean up the model instance
	DestroyModelInstance();

	// Clean up drawing
	RemoveFromLeafSystem();

	RemoveFromAimEntsList();
}


//void C_BaseEntity::SetRefEHandle( const CBaseHandle &handle )
//{
//	m_RefEHandle = handle;
//}


//const CBaseHandle& C_BaseEntity::GetRefEHandle() const
//{
//	return m_RefEHandle;
//}

//-----------------------------------------------------------------------------
// Purpose: Free beams and destroy object
//-----------------------------------------------------------------------------
void C_BaseEntity::Release()
{
	{
		C_BaseAnimating::AutoAllowBoneAccess boneaccess( true, true );
		GetEngineObject()->UnlinkFromHierarchy();
	}

	// Note that this must be called from here, not the destructor, because otherwise the
	//  vtable is hosed and the derived classes function is not going to get called!!!
	if (GetEngineObject()->IsIntermediateDataAllocated() )
	{
		GetEngineObject()->DestroyIntermediateData();
	}

	UpdateOnRemove();

	//delete this;
}


//-----------------------------------------------------------------------------
// Only meant to be called from subclasses.
// Returns true if instance valid, false otherwise
//-----------------------------------------------------------------------------
void C_BaseEntity::CreateModelInstance()
{
	if ( m_ModelInstance == MODEL_INSTANCE_INVALID )
	{
		m_ModelInstance = modelrender->CreateInstance( this );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::DestroyModelInstance()
{
	if (m_ModelInstance != MODEL_INSTANCE_INVALID)
	{
		modelrender->DestroyInstance( m_ModelInstance );
		m_ModelInstance = MODEL_INSTANCE_INVALID;
	}
}

void C_BaseEntity::SetRemovalFlag( bool bRemove ) 
{ 
	if (bRemove) 
		GetEngineObject()->AddEFlags(EFL_KILLME);
	else 
		GetEngineObject()->RemoveEFlags(EFL_KILLME);
}


//-----------------------------------------------------------------------------
// VPhysics objects..
//-----------------------------------------------------------------------------
int C_BaseEntity::VPhysicsGetObjectList( IPhysicsObject **pList, int listMax )
{
	IPhysicsObject *pPhys = VPhysicsGetObject();
	if ( pPhys )
	{
		// multi-object entities must implement this function
		Assert( !(pPhys->GetGameFlags() & FVPHYSICS_MULTIOBJECT_ENTITY) );
		if ( listMax > 0 )
		{
			pList[0] = pPhys;
			return 1;
		}
	}
	return 0;
}

bool C_BaseEntity::VPhysicsIsFlesh( void )
{
	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
	for ( int i = 0; i < count; i++ )
	{
		int material = pList[i]->GetMaterialIndex();
		const surfacedata_t *pSurfaceData = physprops->GetSurfaceData( material );
		// Is flesh ?, don't allow pickup
		if ( pSurfaceData->game.material == CHAR_TEX_ANTLION || pSurfaceData->game.material == CHAR_TEX_FLESH || pSurfaceData->game.material == CHAR_TEX_BLOODYFLESH || pSurfaceData->game.material == CHAR_TEX_ALIENFLESH )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Returns the health fraction
//-----------------------------------------------------------------------------
float C_BaseEntity::HealthFraction() const
{
	if (GetMaxHealth() == 0)
		return 1.0f;

	float flFraction = (float)GetHealth() / (float)GetMaxHealth();
	flFraction = clamp( flFraction, 0.0f, 1.0f );
	return flFraction;
}




void C_BaseEntity::UpdateVisibility()
{
#ifdef TF_CLIENT_DLL
	// TF prevents drawing of any entity attached to players that aren't items in the inventory of the player.
	// This is to prevent servers creating fake cosmetic items and attaching them to players.
	if ( !engine->IsPlayingDemo() )
	{
		static bool bIsStaging = ( engine->GetAppID() == 810 );
		if ( !m_bValidatedOwner )
		{
			bool bRetry = false;

			// Check it the first time we call update visibility (Source TV doesn't bother doing validation)
			m_bDeemedInvalid = engine->IsHLTV() ? false : !ValidateEntityAttachedToPlayer( bRetry );
			m_bValidatedOwner = !bRetry;
		}

		if ( m_bDeemedInvalid )
		{
			if ( bIsStaging )
			{
				if ( !m_bWasDeemedInvalid )
				{
					m_PreviousRenderMode = GetRenderMode();
					m_PreviousRenderColor = GetRenderColor();
					m_bWasDeemedInvalid = true;
				}

				SetRenderMode( kRenderTransColor );
				SetRenderColor( 255, 0, 0, 200 );

			}
			else
			{
				RemoveFromLeafSystem();
				return;
			}
		}
		else if ( m_bWasDeemedInvalid )
		{
			if ( bIsStaging )
			{
				// We need to fix up the rendering.
				SetRenderMode( m_PreviousRenderMode );
				SetRenderColor( m_PreviousRenderColor.r, m_PreviousRenderColor.g, m_PreviousRenderColor.b, m_PreviousRenderColor.a );
			}

			m_bWasDeemedInvalid = false;
		}
	}
#endif

	if ( ShouldDraw() && !IsDormant() && ( !ToolsEnabled() || IsEnabledInToolView() ) )
	{
		// add/update leafsystem
		AddToLeafSystem();
	}
	else
	{
		// remove from leaf system
		RemoveFromLeafSystem();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns whether object should render.
//-----------------------------------------------------------------------------
bool C_BaseEntity::ShouldDraw()
{
// Only test this in tf2
#if defined( INVASION_CLIENT_DLL )
	// Let the client mode (like commander mode) reject drawing entities.
	if (g_pClientMode && !g_pClientMode->ShouldDrawEntity(this) )
		return false;
#endif

	// Some rendermodes prevent rendering
	if ( m_nRenderMode == kRenderNone )
		return false;

	return (model != 0) && !IsEffectActive(EF_NODRAW) && (entindex() != 0);
}

bool C_BaseEntity::TestCollision( const Ray_t& ray, unsigned int mask, trace_t& trace )
{
	return false;
}

bool C_BaseEntity::TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	return false;
}

//-----------------------------------------------------------------------------
// Used when the collision prop is told to ask game code for the world-space surrounding box
//-----------------------------------------------------------------------------
void C_BaseEntity::ComputeWorldSpaceSurroundingBox( Vector *pVecWorldMins, Vector *pVecWorldMaxs )
{
	// This should only be called if you're using USE_GAME_CODE on the server
	// and you forgot to implement the client-side version of this method.
	Assert(0);
}


//-----------------------------------------------------------------------------
// Purpose: Derived classes will have to write their own message cracking routines!!!
// Input  : length - 
//			*data - 
//-----------------------------------------------------------------------------
void C_BaseEntity::ReceiveMessage( int classID, bf_read &msg )
{
	// BaseEntity doesn't have a base class we could relay this message to
	Assert( classID == GetClientClass()->m_ClassID );
	
	int messageType = msg.ReadByte();
	switch( messageType )
	{
		case BASEENTITY_MSG_REMOVE_DECALS:	RemoveAllDecals();
											break;
	}
}


//void* C_BaseEntity::GetDataTableBasePtr()
//{
//	return this;
//}


//-----------------------------------------------------------------------------
// Should this object cast shadows?
//-----------------------------------------------------------------------------
ShadowType_t C_BaseEntity::ShadowCastType()
{
	if (IsEffectActive(EF_NODRAW | EF_NOSHADOW))
		return SHADOWS_NONE;

	int modelType = modelinfo->GetModelType( model );
	return (modelType == mod_studio) ? SHADOWS_RENDER_TO_TEXTURE : SHADOWS_NONE;
}


//-----------------------------------------------------------------------------
// Per-entity shadow cast distance + direction
//-----------------------------------------------------------------------------
bool C_BaseEntity::GetShadowCastDistance( float *pDistance, ShadowType_t shadowType ) const			
{ 
	if ( m_flShadowCastDistance != 0.0f )
	{
		*pDistance = m_flShadowCastDistance; 
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_BaseEntity *C_BaseEntity::GetShadowUseOtherEntity( void ) const
{
	return m_ShadowDirUseOtherEntity;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::SetShadowUseOtherEntity( C_BaseEntity *pEntity )
{
	m_ShadowDirUseOtherEntity = pEntity;
}



//-----------------------------------------------------------------------------
// Purpose: Return a per-entity shadow cast direction
//-----------------------------------------------------------------------------
bool C_BaseEntity::GetShadowCastDirection( Vector *pDirection, ShadowType_t shadowType ) const			
{ 
	if ( m_ShadowDirUseOtherEntity )
		return m_ShadowDirUseOtherEntity->GetShadowCastDirection( pDirection, shadowType );

	return false;
}


//-----------------------------------------------------------------------------
// Should this object receive shadows?
//-----------------------------------------------------------------------------
bool C_BaseEntity::ShouldReceiveProjectedTextures( int flags )
{
	Assert( flags & SHADOW_FLAGS_PROJECTED_TEXTURE_TYPE_MASK );

	if ( IsEffectActive( EF_NODRAW ) )
		 return false;

	if( flags & SHADOW_FLAGS_FLASHLIGHT )
	{
		if ( GetRenderMode() > kRenderNormal && GetRenderColor().a == 0 )
			 return false;

		return true;
	}

	Assert( flags & SHADOW_FLAGS_SHADOW );

	if ( IsEffectActive( EF_NORECEIVESHADOW ) )
		 return false;

	if (modelinfo->GetModelType( model ) == mod_studio)
		return false;

	return true;
}


//-----------------------------------------------------------------------------
// Shadow-related methods
//-----------------------------------------------------------------------------
bool C_BaseEntity::IsShadowDirty( )
{
	return GetEngineObject()->IsEFlagSet( EFL_DIRTY_SHADOWUPDATE );
}

void C_BaseEntity::MarkShadowDirty( bool bDirty )
{
	if ( bDirty )
	{
		GetEngineObject()->AddEFlags( EFL_DIRTY_SHADOWUPDATE );
	}
	else
	{
		GetEngineObject()->RemoveEFlags( EFL_DIRTY_SHADOWUPDATE );
	}
}

IClientRenderable *C_BaseEntity::GetShadowParent()
{
	IEngineObjectClient *pParent = GetEngineObject()->GetMoveParent();
	return pParent ? pParent->GetOuter()->GetClientRenderable() : NULL;
}

IClientRenderable *C_BaseEntity::FirstShadowChild()
{
	IEngineObjectClient *pChild = GetEngineObject()->FirstMoveChild();
	return pChild ? pChild->GetOuter()->GetClientRenderable() : NULL;
}

IClientRenderable *C_BaseEntity::NextShadowPeer()
{
	IEngineObjectClient *pPeer = GetEngineObject()->NextMovePeer();
	return pPeer ? pPeer->GetOuter()->GetClientRenderable() : NULL;
}

	
//-----------------------------------------------------------------------------
// Purpose: Returns index into entities list for this entity
// Output : Index
//-----------------------------------------------------------------------------
//int	C_BaseEntity::entindex( void ) const
//{
//	return index;
//}

int C_BaseEntity::GetSoundSourceIndex() const
{
#ifdef _DEBUG
	if ( entindex() != -1 )
	{
		Assert(entindex() == GetRefEHandle().GetEntryIndex() );
	}
#endif
	return GetRefEHandle().GetEntryIndex();
}

//-----------------------------------------------------------------------------
// Get render origin and angles
//-----------------------------------------------------------------------------
const Vector& C_BaseEntity::GetRenderOrigin( void )
{
	return GetEngineObject()->GetAbsOrigin();
}

const QAngle& C_BaseEntity::GetRenderAngles( void )
{
	return GetEngineObject()->GetAbsAngles();
}

const matrix3x4_t &C_BaseEntity::RenderableToWorldTransform()
{
	return GetEngineObject()->EntityToWorldTransform();
}

IPVSNotify* C_BaseEntity::GetPVSNotifyInterface()
{
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : theMins - 
//			theMaxs - 
//-----------------------------------------------------------------------------
void C_BaseEntity::GetRenderBounds( Vector& theMins, Vector& theMaxs )
{
	int nModelType = modelinfo->GetModelType( model );
	if (nModelType == mod_studio || nModelType == mod_brush)
	{
		modelinfo->GetModelRenderBounds( GetModel(), theMins, theMaxs );
	}
	else
	{
		// By default, we'll just snack on the collision bounds, transform
		// them into entity-space, and call it a day.
		if ( GetRenderAngles() == CollisionProp()->GetCollisionAngles() )
		{
			theMins = CollisionProp()->OBBMins();
			theMaxs = CollisionProp()->OBBMaxs();
		}
		else
		{
			Assert( CollisionProp()->GetCollisionAngles() == vec3_angle );
			if ( IsPointSized() )
			{
				//theMins = CollisionProp()->GetCollisionOrigin();
				//theMaxs	= theMins;
				theMins = theMaxs = vec3_origin;
			}
			else
			{
				// NOTE: This shouldn't happen! Or at least, I haven't run
				// into a valid case where it should yet.
//				Assert(0);
				IRotateAABB(GetEngineObject()->EntityToWorldTransform(), CollisionProp()->OBBMins(), CollisionProp()->OBBMaxs(), theMins, theMaxs );
			}
		}
	}
}

void C_BaseEntity::GetRenderBoundsWorldspace( Vector& mins, Vector& maxs )
{
	DefaultRenderBoundsWorldspace( this, mins, maxs );
}


void C_BaseEntity::GetShadowRenderBounds( Vector &mins, Vector &maxs, ShadowType_t shadowType )
{
	m_EntClientFlags |= ENTCLIENTFLAG_GETTINGSHADOWRENDERBOUNDS;
	GetRenderBounds( mins, maxs );
	m_EntClientFlags &= ~ENTCLIENTFLAG_GETTINGSHADOWRENDERBOUNDS;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : index - 
//-----------------------------------------------------------------------------
void C_BaseEntity::SetModelIndex( int index )
{
	m_nModelIndex = index;
	const model_t *pModel = modelinfo->GetModel( m_nModelIndex );
	SetModelPointer( pModel );
}

void C_BaseEntity::SetModelPointer( const model_t *pModel )
{
	if ( pModel != model )
	{
		DestroyModelInstance();
		model = pModel;
		OnNewModel();

		UpdateVisibility();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : val - 
//			moveCollide - 
//-----------------------------------------------------------------------------
void C_BaseEntity::SetMoveType( MoveType_t val, MoveCollide_t moveCollide /*= MOVECOLLIDE_DEFAULT*/ )
{
	// Make sure the move type + move collide are compatible...
#ifdef _DEBUG
	if ((val != MOVETYPE_FLY) && (val != MOVETYPE_FLYGRAVITY))
	{
		Assert( moveCollide == MOVECOLLIDE_DEFAULT );
	}
#endif

 	m_MoveType = val;
	SetMoveCollide( moveCollide );
}

void C_BaseEntity::SetMoveCollide( MoveCollide_t val )
{
	m_MoveCollide = val;
}


//-----------------------------------------------------------------------------
// Purpose: Get rendermode
// Output : int - the render mode
//-----------------------------------------------------------------------------
bool C_BaseEntity::IsTransparent( void )
{
	bool modelIsTransparent = modelinfo->IsTranslucent(model);
	return modelIsTransparent || (m_nRenderMode != kRenderNormal);
}

bool C_BaseEntity::IsTwoPass( void )
{
	return modelinfo->IsTranslucentTwoPass( GetModel() );
}

bool C_BaseEntity::UsesPowerOfTwoFrameBufferTexture()
{
	return false;
}

bool C_BaseEntity::UsesFullFrameBufferTexture()
{
	return false;
}

bool C_BaseEntity::IgnoresZBuffer( void ) const
{
	return m_nRenderMode == kRenderGlow || m_nRenderMode == kRenderWorldGlow;
}

//-----------------------------------------------------------------------------
// Purpose: Get pointer to CMouthInfo data
// Output : CMouthInfo
//-----------------------------------------------------------------------------
CMouthInfo *C_BaseEntity::GetMouth( void )
{
	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Retrieve sound spatialization info for the specified sound on this entity
// Input  : info - 
// Output : Return false to indicate sound is not audible
//-----------------------------------------------------------------------------
bool C_BaseEntity::GetSoundSpatialization( SpatializationInfo_t& info )
{
	// World is always audible
	if ( entindex() == 0 )
	{
		return true;
	}

	// Out of PVS
	if ( IsDormant() )
	{
		return false;
	}

	// pModel might be NULL, but modelinfo can handle that
	const model_t *pModel = GetModel();
	
	if ( info.pflRadius )
	{
		*info.pflRadius = modelinfo->GetModelRadius( pModel );
	}
	
	if ( info.pOrigin )
	{
		*info.pOrigin = GetEngineObject()->GetAbsOrigin();

		// move origin to middle of brush
		if ( modelinfo->GetModelType( pModel ) == mod_brush )
		{
			Vector mins, maxs, center;

			modelinfo->GetModelBounds( pModel, mins, maxs );
			VectorAdd( mins, maxs, center );
			VectorScale( center, 0.5f, center );

			(*info.pOrigin) += center;
		}
	}

	if ( info.pAngles )
	{
		VectorCopy(GetEngineObject()->GetAbsAngles(), *info.pAngles );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Get attachment point by index
// Input  : number - which point
// Output : float * - the attachment point
//-----------------------------------------------------------------------------
bool C_BaseEntity::GetAttachment( int number, Vector &origin, QAngle &angles )
{
	origin = GetEngineObject()->GetAbsOrigin();
	angles = GetEngineObject()->GetAbsAngles();
	return true;
}

bool C_BaseEntity::GetAttachment( int number, Vector &origin )
{
	origin = GetEngineObject()->GetAbsOrigin();
	return true;
}

bool C_BaseEntity::GetAttachment( int number, matrix3x4_t &matrix )
{
	MatrixCopy(GetEngineObject()->EntityToWorldTransform(), matrix );
	return true;
}

bool C_BaseEntity::GetAttachmentVelocity( int number, Vector &originVel, Quaternion &angleVel )
{
	originVel = GetEngineObject()->GetAbsVelocity();
	angleVel.Init();
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Get this entity's rendering clip plane if one is defined
// Output : float * - The clip plane to use, or NULL if no clip plane is defined
//-----------------------------------------------------------------------------
float *C_BaseEntity::GetRenderClipPlane( void )
{
	if( m_bEnableRenderingClipPlane )
		return m_fRenderingClipPlane;
	else
		return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int C_BaseEntity::DrawBrushModel( bool bDrawingTranslucency, int nFlags, bool bTwoPass )
{
	VPROF_BUDGET( "C_BaseEntity::DrawBrushModel", VPROF_BUDGETGROUP_BRUSHMODEL_RENDERING );
	// Identity brushes are drawn in view->DrawWorld as an optimization
	Assert ( modelinfo->GetModelType( model ) == mod_brush );

	ERenderDepthMode DepthMode = DEPTH_MODE_NORMAL;
	if ( ( nFlags & STUDIO_SSAODEPTHTEXTURE ) != 0 )
	{
		DepthMode = DEPTH_MODE_SSA0;
	}
	else if ( ( nFlags & STUDIO_SHADOWDEPTHTEXTURE ) != 0 )
	{
		DepthMode = DEPTH_MODE_SHADOW;
	}

	if ( DepthMode != DEPTH_MODE_NORMAL )
	{
		render->DrawBrushModelShadowDepth( this, (model_t *)model, GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), DepthMode );
	}
	else
	{
		DrawBrushModelMode_t mode = DBM_DRAW_ALL;
		if ( bTwoPass )
		{
			mode = bDrawingTranslucency ? DBM_DRAW_TRANSLUCENT_ONLY : DBM_DRAW_OPAQUE_ONLY;
		}
		render->DrawBrushModelEx( this, (model_t *)model, GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), mode );
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: Draws the object
// Input  : flags - 
//-----------------------------------------------------------------------------
int C_BaseEntity::DrawModel( int flags )
{
	if ( !m_bReadyToDraw )
		return 0;

	int drawn = 0;
	if ( !model )
	{
		return drawn;
	}

	int modelType = modelinfo->GetModelType( model );
	switch ( modelType )
	{
	case mod_brush:
		drawn = DrawBrushModel( flags & STUDIO_TRANSPARENCY ? true : false, flags, ( flags & STUDIO_TWOPASS ) ? true : false );
		break;
	case mod_studio:
		// All studio models must be derived from C_BaseAnimating.  Issue warning.
		Warning( "ERROR:  Can't draw studio model %s because %s is not derived from C_BaseAnimating\n",
			modelinfo->GetModelName( model ), GetClientClass()->m_pNetworkName ? GetClientClass()->m_pNetworkName : "unknown" );
		break;
	case mod_sprite:
		//drawn = DrawSprite();
		Warning( "ERROR:  Sprite model's not supported any more except in legacy temp ents\n" );
		break;
	default:
		break;
	}

	// If we're visualizing our bboxes, draw them
	DrawBBoxVisualizations();

	return drawn;
}

//-----------------------------------------------------------------------------
// Purpose: Setup the bones for drawing
//-----------------------------------------------------------------------------
bool C_BaseEntity::SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Setup vertex weights for drawing
//-----------------------------------------------------------------------------
void C_BaseEntity::SetupWeights( const matrix3x4_t *pBoneToWorld, int nFlexWeightCount, float *pFlexWeights, float *pFlexDelayedWeights )
{
}


//-----------------------------------------------------------------------------
// Purpose: Process any local client-side animation events
//-----------------------------------------------------------------------------
void C_BaseEntity::DoAnimationEvents( )
{
}


void C_BaseEntity::UpdatePartitionListEntry()
{
	// Don't add the world entity
	CollideType_t shouldCollide = GetCollideType();

	// Choose the list based on what kind of collisions we want
	int list = PARTITION_CLIENT_NON_STATIC_EDICTS;
	if (shouldCollide == ENTITY_SHOULD_COLLIDE)
		list |= PARTITION_CLIENT_SOLID_EDICTS;
	else if (shouldCollide == ENTITY_SHOULD_RESPOND)
		list |= PARTITION_CLIENT_RESPONSIVE_EDICTS;

	// add the entity to the KD tree so we will collide against it
	partition->RemoveAndInsert( PARTITION_CLIENT_SOLID_EDICTS | PARTITION_CLIENT_RESPONSIVE_EDICTS | PARTITION_CLIENT_NON_STATIC_EDICTS, list, CollisionProp()->GetPartitionHandle() );
}


void C_BaseEntity::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	// Init should have been called before we get in here.
	Assert( CollisionProp()->GetPartitionHandle() != PARTITION_INVALID_HANDLE );
	if ( entindex() < 0 )
		return;
	
	switch( state )
	{
	case SHOULDTRANSMIT_START:
		{
			// We've just been sent by the server. Become active.
			SetDormant( false );
			
			UpdatePartitionListEntry();

//#if !defined( NO_ENTITY_PREDICTION )
//			// Note that predictables get a chance to hook up to their server counterparts here
//			if ( m_PredictableID.IsActive() )
//			{
//				// Find corresponding client side predicted entity and remove it from predictables
//				m_PredictableID.SetAcknowledged( true );
//
//				C_BaseEntity *otherEntity = FindPreviouslyCreatedEntity( m_PredictableID );
//				if ( otherEntity )
//				{
//					Assert( otherEntity->IsClientCreated() );
//					Assert( otherEntity->m_PredictableID.IsActive() );
//					Assert( ClientEntityList().IsHandleValid( otherEntity->GetClientHandle() ) );
//
//					otherEntity->m_PredictableID.SetAcknowledged( true );
//
//					if ( OnPredictedEntityRemove( false, otherEntity ) )
//					{
//						// Mark it for delete after receive all network data
//						DestroyEntity(otherEntity);// ->Release();
//					}
//				}
//			}
//#endif
		}
		break;

	case SHOULDTRANSMIT_END:
		{
			// Clear out links if we're out of the picture...
			GetEngineObject()->UnlinkFromHierarchy();

			// We're no longer being sent by the server. Become dormant.
			SetDormant( true );
			
			// remove the entity from the KD tree so we won't collide against it
			partition->Remove( PARTITION_CLIENT_SOLID_EDICTS | PARTITION_CLIENT_RESPONSIVE_EDICTS | PARTITION_CLIENT_NON_STATIC_EDICTS, CollisionProp()->GetPartitionHandle() );
		
		}
		break;

	default:
		Assert( 0 );
		break;
	}
}

//-----------------------------------------------------------------------------
// Call this in PostDataUpdate if you don't chain it down!
//-----------------------------------------------------------------------------
void C_BaseEntity::MarkMessageReceived()
{
	m_flLastMessageTime = engine->GetLastTimeStamp();
}


//-----------------------------------------------------------------------------
// Purpose: Entity is about to be decoded from the network stream
// Input  : bnewentity - is this a new entity this update?
//-----------------------------------------------------------------------------
void C_BaseEntity::PreDataUpdate( DataUpdateType_t updateType )
{
	VPROF( "C_BaseEntity::PreDataUpdate" );

	// Register for an OnDataChanged call and call OnPreDataChanged().
	if ( AddDataChangeEvent( this, updateType, &m_DataChangeEventRef ) )
	{
		OnPreDataChanged( updateType );
	}


	// Need to spawn on client before receiving original network data 
	// in case it overrides any values set up in spawn ( e.g., m_iState )
	bool bnewentity = (updateType == DATA_UPDATE_CREATED);

	if ( !bnewentity )
	{
		GetEngineObject()->Interp_RestoreToLastNetworked(GetEngineObject()->GetVarMapping() );
	}

	if ( bnewentity /*&& !IsClientCreated()*/)
	{
		m_flSpawnTime = engine->GetLastTimeStamp();
		MDLCACHE_CRITICAL_SECTION();
		Spawn();
	}

#if 0 // Yahn suggesting commenting this out as a fix to demo recording not working
	// If the entity moves itself every FRAME on the server but doesn't update animtime,
	// then use the current server time as the time for interpolation.
	if ( IsSelfAnimating() )
	{
		m_flAnimTime = engine->GetLastTimeStamp();
	}
#endif

	m_vecOldOrigin = GetEngineObject()->GetNetworkOrigin();
	m_vecOldAngRotation = GetEngineObject()->GetNetworkAngles();

	m_flOldAnimTime = m_flAnimTime;
	m_flOldSimulationTime = m_flSimulationTime;

	m_nOldRenderMode = m_nRenderMode;

	if ( m_hRender != INVALID_CLIENT_RENDER_HANDLE )
	{
		ClientLeafSystem()->EnableAlternateSorting( m_hRender, m_bAlternateSorting );
	}

	m_ubOldInterpolationFrame = m_ubInterpolationFrame;
}

const Vector& C_BaseEntity::GetOldOrigin()
{
	return m_vecOldOrigin;
}




CUtlVector< C_BaseEntity * >	g_AimEntsList;


//-----------------------------------------------------------------------------
// Moves all aiments
//-----------------------------------------------------------------------------
void C_BaseEntity::MarkAimEntsDirty()
{
	// FIXME: With the dirty bits hooked into cycle + sequence, it's unclear
	// that this is even necessary any more (provided aiments are always accessing
	// joints or attachments of the move parent).
	//
	// NOTE: This is a tricky algorithm. This list does not actually contain
	// all aim-ents in its list. It actually contains all hierarchical children,
	// of which aim-ents are a part. We can tell if something is an aiment if it has
	// the EF_BONEMERGE effect flag set.
	// 
	// We will first iterate over all aiments and clear their DIRTY_ABSTRANSFORM flag, 
	// which is necessary to cause them to recompute their aim-ent origin 
	// the next time CalcAbsPosition is called. Because CalcAbsPosition calls MoveToAimEnt
	// and MoveToAimEnt calls SetAbsOrigin/SetAbsAngles, that is how CalcAbsPosition
	// will cause the aim-ent's (and all its children's) dirty state to be correctly updated.
	//
	// Then we will iterate over the loop a second time and call CalcAbsPosition on them,
	int i;
	int c = g_AimEntsList.Count();
	for ( i = 0; i < c; ++i )
	{
		C_BaseEntity *pEnt = g_AimEntsList[ i ];
		Assert( pEnt && pEnt->GetEngineObject()->GetMoveParent() );
		if ( pEnt->IsEffectActive(EF_BONEMERGE | EF_PARENT_ANIMATES) )
		{
			pEnt->GetEngineObject()->AddEFlags( EFL_DIRTY_ABSTRANSFORM );
		}
	}
}


void C_BaseEntity::CalcAimEntPositions()
{
	VPROF("CalcAimEntPositions");
	int i;
	int c = g_AimEntsList.Count();
	for ( i = 0; i < c; ++i )
	{
		C_BaseEntity *pEnt = g_AimEntsList[ i ];
		Assert( pEnt );
		Assert( pEnt->GetEngineObject()->GetMoveParent() );
		if ( pEnt->IsEffectActive(EF_BONEMERGE) )
		{
			pEnt->GetEngineObject()->CalcAbsolutePosition( );
		}
	}
}


void C_BaseEntity::AddToAimEntsList()
{
	// Already in list
	if ( m_AimEntsListHandle != INVALID_AIMENTS_LIST_HANDLE )
		return;

	m_AimEntsListHandle = g_AimEntsList.AddToTail( this );
}

void C_BaseEntity::RemoveFromAimEntsList()
{
	// Not in list yet
	if ( INVALID_AIMENTS_LIST_HANDLE == m_AimEntsListHandle )
	{
		return;
	}

	unsigned int c = g_AimEntsList.Count();

	Assert( m_AimEntsListHandle < c );

	unsigned int last = c - 1;

	if ( last == m_AimEntsListHandle )
	{
		// Just wipe the final entry
		g_AimEntsList.FastRemove( last );
	}
	else
	{
		C_BaseEntity *lastEntity = g_AimEntsList[ last ];
		// Remove the last entry
		g_AimEntsList.FastRemove( last );

		// And update it's handle to point to this slot.
		lastEntity->m_AimEntsListHandle = m_AimEntsListHandle;
		g_AimEntsList[ m_AimEntsListHandle ] = lastEntity;
	}

	// Invalidate our handle no matter what.
	m_AimEntsListHandle = INVALID_AIMENTS_LIST_HANDLE;
}

//-----------------------------------------------------------------------------
// Update move-parent if needed. For SourceTV.
//-----------------------------------------------------------------------------
void C_BaseEntity::HierarchyUpdateMoveParent()
{
	if (GetEngineObject()->GetNetworkMoveParent() && GetEngineObject()->GetNetworkMoveParent() == GetEngineObject()->GetMoveParent())
		return;

	GetEngineObject()->HierarchySetParent(GetEngineObject()->GetNetworkMoveParent());
}





//-----------------------------------------------------------------------------
// Purpose: Make sure that the correct model is referenced for this entity
//-----------------------------------------------------------------------------
void C_BaseEntity::ValidateModelIndex( void )
{
#ifdef TF_CLIENT_DLL
	if ( m_nModelIndexOverrides[VISION_MODE_NONE] > 0 ) 
	{
		if ( IsLocalPlayerUsingVisionFilterFlags( TF_VISION_FILTER_HALLOWEEN ) )
		{
			if ( m_nModelIndexOverrides[VISION_MODE_HALLOWEEN] > 0 )
			{
				SetModelByIndex( m_nModelIndexOverrides[VISION_MODE_HALLOWEEN] );
				return;
			}
		}
		
		if ( IsLocalPlayerUsingVisionFilterFlags( TF_VISION_FILTER_PYRO ) )
		{
			if ( m_nModelIndexOverrides[VISION_MODE_PYRO] > 0 )
			{
				SetModelByIndex( m_nModelIndexOverrides[VISION_MODE_PYRO] );
				return;
			}
		}

		if ( IsLocalPlayerUsingVisionFilterFlags( TF_VISION_FILTER_ROME ) )
		{
			if ( m_nModelIndexOverrides[VISION_MODE_ROME] > 0 )
			{
				SetModelByIndex( m_nModelIndexOverrides[VISION_MODE_ROME] );
				return;
			}
		}

		SetModelByIndex( m_nModelIndexOverrides[VISION_MODE_NONE] );		

		return;
	}
#endif

	SetModelByIndex( m_nModelIndex );
}

//-----------------------------------------------------------------------------
// Purpose: Entity data has been parsed and unpacked.  Now do any necessary decoding, munging
// Input  : bnewentity - was this entity new in this update packet?
//-----------------------------------------------------------------------------
void C_BaseEntity::PostDataUpdate( DataUpdateType_t updateType )
{
	MDLCACHE_CRITICAL_SECTION();

	PREDICTION_TRACKVALUECHANGESCOPE_ENTITY( this, "postdataupdate" );

	// NOTE: This *has* to happen first. Otherwise, Origin + angles may be wrong 
	if ( m_nRenderFX == kRenderFxRagdoll && updateType == DATA_UPDATE_CREATED )
	{
		MoveToLastReceivedPosition( true );
	}
	else
	{
		MoveToLastReceivedPosition( false );
	}

	// If it's the world, force solid flags
	if (entindex() == 0 )
	{
		m_nModelIndex = 1;
		SetSolid( SOLID_BSP );

		// FIXME: Should these be assertions?
		GetEngineObject()->SetAbsOrigin( vec3_origin );
		GetEngineObject()->SetAbsAngles( vec3_angle );
	}

	if ( m_nOldRenderMode != m_nRenderMode )
	{
		SetRenderMode( (RenderMode_t)m_nRenderMode, true );
	}

	bool animTimeChanged = ( m_flAnimTime != m_flOldAnimTime ) ? true : false;
	bool originChanged = ( m_vecOldOrigin != GetEngineObject()->GetLocalOrigin() ) ? true : false;
	bool anglesChanged = ( m_vecOldAngRotation != GetEngineObject()->GetLocalAngles() ) ? true : false;
	bool simTimeChanged = ( m_flSimulationTime != m_flOldSimulationTime ) ? true : false;

	// Detect simulation changes 
	bool simulationChanged = originChanged || anglesChanged || simTimeChanged;

	bool bPredictable = GetPredictable();

	// For non-predicted and non-client only ents, we need to latch network values into the interpolation histories
	if ( !bPredictable /*&& !IsClientCreated()*/)
	{
		if ( animTimeChanged )
		{
			GetEngineObject()->OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
		}

		if ( simulationChanged )
		{
			GetEngineObject()->OnLatchInterpolatedVariables( LATCH_SIMULATION_VAR );
		}
	}
	// For predictables, we also need to store off the last networked value
	else if ( bPredictable )
	{
		// Just store off last networked value for use in prediction
		GetEngineObject()->OnStoreLastNetworkedValue();
	}

	// Deal with hierarchy. Have to do it here (instead of in a proxy)
	// because this is the only point at which all entities are loaded
	// If this condition isn't met, then a child was sent without its parent
	//Assert( m_hNetworkMoveParent.Get() || !m_hNetworkMoveParent.IsValid() );
	GetEngineObject()->HierarchySetParent(GetEngineObject()->GetNetworkMoveParent());

	MarkMessageReceived();

	// Make sure that the correct model is referenced for this entity
	ValidateModelIndex();

	// If this entity was new, then latch in various values no matter what.
	if ( updateType == DATA_UPDATE_CREATED )
	{
		// Construct a random value for this instance
		m_flProxyRandomValue = random->RandomFloat( 0, 1 );

		ResetLatched();

		m_nCreationTick = gpGlobals->tickcount;
	}

	CheckInitPredictable( "PostDataUpdate" );

	// It's possible that a new entity will need to be forceably added to the 
	//   player simulation list.  If so, do this here
//#if !defined( NO_ENTITY_PREDICTION )
//	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
//	if ( IsPlayerSimulated() &&
//		( NULL != local ) && 
//		( local == m_hOwnerEntity ) )
//	{
//		// Make sure player is driving simulation (field is only ever sent to local player)
//		SetPlayerSimulated( local );
//	}
//#endif

	UpdatePartitionListEntry();
	
	// Add the entity to the nointerp list.
//	if ( !IsClientCreated() )
//	{
		if ( Teleported() || IsNoInterpolationFrame() )
			AddToTeleportList();
//	}

	// if we changed parents, recalculate visibility
	if ( m_hOldMoveParent != GetEngineObject()->GetNetworkMoveParent())
	{
		UpdateVisibility();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *context - 
//-----------------------------------------------------------------------------
void C_BaseEntity::CheckInitPredictable( const char *context )
{
#if !defined( NO_ENTITY_PREDICTION )
	// Prediction is disabled
	if ( !cl_predict->GetInt() )
		return;

	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();

	if ( !player )
		return;

	//if ( !GetPredictionEligible() )
	//{
	//	if ( m_PredictableID.IsActive() &&
	//		( player->index - 1 ) == m_PredictableID.GetPlayer() )
	//	{
	//		// If it comes through with an ID, it should be eligible
	//		SetPredictionEligible( true );
	//	}
	//	else
	//	{
	//		return;
	//	}
	//}

	//if ( IsClientCreated() )
	//	return;

	if ( !ShouldPredict() )
		return;

	if (GetEngineObject()->IsIntermediateDataAllocated() )
		return;

	// Msg( "Predicting init %s at %s\n", GetClassname(), context );

	InitPredictable();
#endif
}

bool C_BaseEntity::IsSelfAnimating()
{
	return true;
}


//-----------------------------------------------------------------------------
// Sets the model... 
//-----------------------------------------------------------------------------
void C_BaseEntity::SetModelByIndex( int nModelIndex )
{
	SetModelIndex( nModelIndex );
}


//-----------------------------------------------------------------------------
// Set model... (NOTE: Should only be used by client-only entities
//-----------------------------------------------------------------------------
bool C_BaseEntity::SetModel( const char *pModelName )
{
	if ( pModelName )
	{
		int nModelIndex = modelinfo->GetModelIndex( pModelName );
		SetModelByIndex( nModelIndex );
		return ( nModelIndex != -1 );
	}
	else
	{
		SetModelByIndex( -1 );
		return false;
	}
}




//-----------------------------------------------------------------------------
// Purpose: Default interpolation for entities
// Output : true means entity should be drawn, false means probably not
//-----------------------------------------------------------------------------
bool C_BaseEntity::Interpolate( float currentTime )
{
	VPROF( "C_BaseEntity::Interpolate" );

	Vector oldOrigin;
	QAngle oldAngles;
	Vector oldVel;

	int bNoMoreChanges;
	int retVal = GetEngineObject()->BaseInterpolatePart1( currentTime, oldOrigin, oldAngles, oldVel, bNoMoreChanges );

	// If all the Interpolate() calls returned that their values aren't going to
	// change anymore, then get us out of the interpolation list.
	if ( bNoMoreChanges )
		RemoveFromInterpolationList();

	if ( retVal == INTERPOLATE_STOP )
		return true;

	int nChangeFlags = 0;
	GetEngineObject()->BaseInterpolatePart2( oldOrigin, oldAngles, oldVel, nChangeFlags );

	return true;
}

CStudioHdr *C_BaseEntity::OnNewModel()
{
#ifdef TF_CLIENT_DLL
	m_bValidatedOwner = false;
#endif

	return NULL;
}

void C_BaseEntity::OnNewParticleEffect( const char *pszParticleName, CNewParticleEffect *pNewParticleEffect )
{
	return;
}

// Above this velocity and we'll assume a warp/teleport
#define MAX_INTERPOLATE_VELOCITY 4000.0f
#define MAX_INTERPOLATE_VELOCITY_PLAYER 1250.0f

//-----------------------------------------------------------------------------
// Purpose: Determine whether entity was teleported ( so we can disable interpolation )
// Input  : *ent - 
// Output : bool
//-----------------------------------------------------------------------------
bool C_BaseEntity::Teleported( void )
{
	// Disable interpolation when hierarchy changes
	if (m_hOldMoveParent != GetEngineObject()->GetNetworkMoveParent() || m_iOldParentAttachment != GetEngineObject()->GetParentAttachment())
	{
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Is this a submodel of the world ( model name starts with * )?
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_BaseEntity::IsSubModel( void )
{
	if ( model &&
		modelinfo->GetModelType( model ) == mod_brush &&
		modelinfo->GetModelName( model )[0] == '*' )
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Create entity lighting effects
//-----------------------------------------------------------------------------
void C_BaseEntity::CreateLightEffects( void )
{
	dlight_t *dl;

	// Is this for player flashlights only, if so move to linkplayers?
	if (entindex() == render->GetViewEntity() )
		return;

	if (IsEffectActive(EF_BRIGHTLIGHT))
	{
		dl = effects->CL_AllocDlight (entindex());
		dl->origin = GetEngineObject()->GetAbsOrigin();
		dl->origin[2] += 16;
		dl->color.r = dl->color.g = dl->color.b = 250;
		dl->radius = random->RandomFloat(400,431);
		dl->die = gpGlobals->curtime + 0.001;
	}
	if (IsEffectActive(EF_DIMLIGHT))
	{			
		dl = effects->CL_AllocDlight (entindex());
		dl->origin = GetEngineObject()->GetAbsOrigin();
		dl->color.r = dl->color.g = dl->color.b = 100;
		dl->radius = random->RandomFloat(200,231);
		dl->die = gpGlobals->curtime + 0.001;
	}
}

void C_BaseEntity::MoveToLastReceivedPosition( bool force )
{
	if ( force || ( m_nRenderFX != kRenderFxRagdoll ) )
	{
		GetEngineObject()->SetLocalOrigin( GetEngineObject()->GetNetworkOrigin() );
		GetEngineObject()->SetLocalAngles(GetEngineObject()->GetNetworkAngles() );
	}
}

bool C_BaseEntity::ShouldInterpolate()
{
	if ( render->GetViewEntity() == entindex())
		return true;

	if (entindex() == 0 || !GetModel() )
		return false;

	// always interpolate if visible
	if ( IsVisible() )
		return true;

	// if any movement child needs interpolation, we have to interpolate too
	IEngineObjectClient *pChild = GetEngineObject()->FirstMoveChild();
	while( pChild )
	{
		if ( pChild->GetOuter()->ShouldInterpolate() )	
			return true;

		pChild = pChild->NextMovePeer();
	}

	// don't interpolate
	return false;
}


void C_BaseEntity::ProcessTeleportList()
{
	int iNext;
	for ( int iCur=g_TeleportList.Head(); iCur != g_TeleportList.InvalidIndex(); iCur=iNext )
	{
		iNext = g_TeleportList.Next( iCur );
		C_BaseEntity *pCur = g_TeleportList[iCur];

		bool teleport = pCur->Teleported();
		bool ef_nointerp = pCur->IsNoInterpolationFrame();
	
		if ( teleport || ef_nointerp )
		{
			// Undo the teleport flag..
			pCur->m_hOldMoveParent = pCur->GetEngineObject()->GetNetworkMoveParent();
			pCur->m_iOldParentAttachment = pCur->GetEngineObject()->GetParentAttachment();
			// Zero out all but last update.
			pCur->MoveToLastReceivedPosition( true );
			pCur->ResetLatched();
		}
		else
		{
			// Get it out of the list as soon as we can.
			pCur->RemoveFromTeleportList();
		}
	}
}


void C_BaseEntity::CheckInterpolatedVarParanoidMeasurement()
{
	// What we're doing here is to check all the entities that were not in the interpolation
	// list and make sure that there's no entity that should be in the list that isn't.
	
#ifdef INTERPOLATEDVAR_PARANOID_MEASUREMENT
	int iHighest = ClientEntityList().GetHighestEntityIndex();
	for ( int i=0; i <= iHighest; i++ )
	{
		C_BaseEntity *pEnt = ClientEntityList().GetBaseEntity( i );
		if ( !pEnt || pEnt->m_InterpolationListEntry != 0xFFFF || !pEnt->ShouldInterpolate() )
			continue;
		
		// Player angles always generates this error when the console is up.
		if ( pEnt->entindex() == 1 && engine->Con_IsVisible() )
			continue;
			
		// View models tend to screw up this test unnecesarily because they modify origin,
		// angles, and 
		if ( dynamic_cast<C_BaseViewModel*>( pEnt ) )
			continue;

		g_bRestoreInterpolatedVarValues = true;
		g_nInterpolatedVarsChanged = 0;
		pEnt->Interpolate( gpGlobals->curtime );
		g_bRestoreInterpolatedVarValues = false;
		
		if ( g_nInterpolatedVarsChanged > 0 )
		{
			static int iWarningCount = 0;
			Warning( "(%d): An entity (%d) should have been in g_InterpolationList.\n", iWarningCount++, pEnt->entindex() );
			break;
		}
	}
#endif
}


void C_BaseEntity::ProcessInterpolatedList()
{
	CheckInterpolatedVarParanoidMeasurement();

	// Interpolate the minimal set of entities that need it.
	int iNext;
	for ( int iCur=g_InterpolationList.Head(); iCur != g_InterpolationList.InvalidIndex(); iCur=iNext )
	{
		iNext = g_InterpolationList.Next( iCur );
		C_BaseEntity *pCur = g_InterpolationList[iCur];
		
		pCur->m_bReadyToDraw = pCur->Interpolate( gpGlobals->curtime );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Add entity to visibile entities list
//-----------------------------------------------------------------------------
void C_BaseEntity::AddEntity( void )
{
	// Don't ever add the world, it's drawn separately
	if (entindex() == 0 )
		return;

	// Create flashlight effects, etc.
	CreateLightEffects();
}


//-----------------------------------------------------------------------------
// Returns the aiment render origin + angles
//-----------------------------------------------------------------------------
void C_BaseEntity::GetAimEntOrigin( IClientEntity *pAttachedTo, Vector *pOrigin, QAngle *pAngles )
{
	// Should be overridden for things that attach to attchment points

	// Slam origin to the origin of the entity we are attached to...
	*pOrigin = ((C_BaseEntity*)pAttachedTo)->GetEngineObject()->GetAbsOrigin();
	*pAngles = ((C_BaseEntity*)pAttachedTo)->GetEngineObject()->GetAbsAngles();
}


void C_BaseEntity::StopFollowingEntity( )
{
	Assert( IsFollowingEntity() );

	GetEngineObject()->SetParent( NULL );
	RemoveEffects( EF_BONEMERGE );
	RemoveSolidFlags( FSOLID_NOT_SOLID );
	SetMoveType( MOVETYPE_NONE );
}

bool C_BaseEntity::IsFollowingEntity()
{
	return IsEffectActive(EF_BONEMERGE) && (GetMoveType() == MOVETYPE_NONE) && GetEngineObject()->GetMoveParent();
}

C_BaseEntity *CBaseEntity::GetFollowedEntity()
{
	if (!IsFollowingEntity())
		return NULL;
	return GetEngineObject()->GetMoveParent()?GetEngineObject()->GetMoveParent()->GetOuter():NULL;
}


//-----------------------------------------------------------------------------
// Default implementation for GetTextureAnimationStartTime
//-----------------------------------------------------------------------------
float C_BaseEntity::GetTextureAnimationStartTime()
{
	return m_flSpawnTime;
}


//-----------------------------------------------------------------------------
// Default implementation, indicates that a texture animation has wrapped
//-----------------------------------------------------------------------------
void C_BaseEntity::TextureAnimationWrapped()
{
}


void C_BaseEntity::ClientThink()
{
}

void C_BaseEntity::Simulate()
{
	AddEntity();	// Legacy support. Once-per-frame stuff should go in Simulate().
}

// Defined in engine
static ConVar cl_interpolate( "cl_interpolate", "1.0f", FCVAR_USERINFO | FCVAR_DEVELOPMENTONLY );

// (static function)
void C_BaseEntity::InterpolateServerEntities()
{
	VPROF_BUDGET( "C_BaseEntity::InterpolateServerEntities", VPROF_BUDGETGROUP_INTERPOLATION );

	s_bInterpolate = cl_interpolate.GetBool();

	// Don't interpolate during timedemo playback
	if ( engine->IsPlayingTimeDemo() || engine->IsPaused() )
	{										 
		s_bInterpolate = false;
	}

	if ( !engine->IsPlayingDemo() )
	{
		// Don't interpolate, either, if we are timing out
		INetChannelInfo *nci = engine->GetNetChannelInfo();
		if ( nci && nci->GetTimeSinceLastReceived() > 0.5f )
		{
			s_bInterpolate = false;
		}
	}

	if ( IsSimulatingOnAlternateTicks() != g_bWasSkipping || IsEngineThreaded() != g_bWasThreaded )
	{
		g_bWasSkipping = IsSimulatingOnAlternateTicks();
		g_bWasThreaded = IsEngineThreaded();

		C_BaseEntityIterator iterator;
		C_BaseEntity *pEnt;
		while ( (pEnt = iterator.Next()) != NULL )
		{
			pEnt->GetEngineObject()->Interp_UpdateInterpolationAmounts( pEnt->GetEngineObject()->GetVarMapping() );
		}
	}

	// Enable extrapolation?
	CInterpolationContext context;
	context.SetLastTimeStamp( engine->GetLastTimeStamp() );
	if ( cl_extrapolate.GetBool() && !engine->IsPaused() )
	{
		context.EnableExtrapolation( true );
	}

	// Smoothly interpolate position for server entities.
	ProcessTeleportList();
	ProcessInterpolatedList();
}


// (static function)
//void C_BaseEntity::AddVisibleEntities()
//{
//#if !defined( NO_ENTITY_PREDICTION )
//	VPROF_BUDGET( "C_BaseEntity::AddVisibleEntities", VPROF_BUDGETGROUP_WORLD_RENDERING );
//
//	// Let non-dormant client created predictables get added, too
//	int c = predictables->GetPredictableCount();
//	for ( int i = 0 ; i < c ; i++ )
//	{
//		C_BaseEntity *pEnt = predictables->GetPredictable( i );
//		if ( !pEnt )
//			continue;
//
//		if ( !pEnt->IsClientCreated() )
//			continue;
//
//		// Only draw until it's ack'd since that means a real entity has arrived
//		if ( pEnt->m_PredictableID.GetAcknowledged() )
//			continue;
//
//		// Don't draw if dormant
//		if ( pEnt->IsDormantPredictable() )
//			continue;
//
//		pEnt->UpdateVisibility();	
//	}
//#endif
//}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : type - 
//-----------------------------------------------------------------------------
void C_BaseEntity::OnPreDataChanged( DataUpdateType_t type )
{
	m_hOldMoveParent = GetEngineObject()->GetNetworkMoveParent();
	m_iOldParentAttachment = GetEngineObject()->GetParentAttachment();
}

void C_BaseEntity::OnDataChanged( DataUpdateType_t type )
{
	// See if it needs to allocate prediction stuff
	CheckInitPredictable( "OnDataChanged" );

	// Set up shadows; do it here so that objects can change shadowcasting state
	CreateShadow();

	if ( type == DATA_UPDATE_CREATED )
	{
		UpdateVisibility();
	}
}

ClientThinkHandle_t C_BaseEntity::GetThinkHandle()
{
	return m_hThink;
}


void C_BaseEntity::SetThinkHandle( ClientThinkHandle_t hThink )
{
	m_hThink = hThink;
}


//-----------------------------------------------------------------------------
// Purpose: This routine modulates renderamt according to m_nRenderFX's value
//  This is a client side effect and will not be in-sync on machines across a
//  network game.
// Input  : origin - 
//			alpha - 
// Output : int
//-----------------------------------------------------------------------------
void C_BaseEntity::ComputeFxBlend( void )
{
	// Don't recompute if we've already computed this frame
	if ( m_nFXComputeFrame == gpGlobals->framecount )
		return;

	MDLCACHE_CRITICAL_SECTION();
	int blend=0;
	float offset;

	offset = ((int)entindex()) * 363.0;// Use ent index to de-sync these fx

	switch( m_nRenderFX ) 
	{
	case kRenderFxPulseSlowWide:
		blend = m_clrRender->a + 0x40 * sin( gpGlobals->curtime * 2 + offset );	
		break;
		
	case kRenderFxPulseFastWide:
		blend = m_clrRender->a + 0x40 * sin( gpGlobals->curtime * 8 + offset );
		break;
	
	case kRenderFxPulseFastWider:
		blend = ( 0xff * fabs(sin( gpGlobals->curtime * 12 + offset ) ) );
		break;

	case kRenderFxPulseSlow:
		blend = m_clrRender->a + 0x10 * sin( gpGlobals->curtime * 2 + offset );
		break;
		
	case kRenderFxPulseFast:
		blend = m_clrRender->a + 0x10 * sin( gpGlobals->curtime * 8 + offset );
		break;
		
	// JAY: HACK for now -- not time based
	case kRenderFxFadeSlow:			
		if ( m_clrRender->a > 0 ) 
		{
			SetRenderColorA( m_clrRender->a - 1 );
		}
		else
		{
			SetRenderColorA( 0 );
		}
		blend = m_clrRender->a;
		break;
		
	case kRenderFxFadeFast:
		if ( m_clrRender->a > 3 ) 
		{
			SetRenderColorA( m_clrRender->a - 4 );
		}
		else
		{
			SetRenderColorA( 0 );
		}
		blend = m_clrRender->a;
		break;
		
	case kRenderFxSolidSlow:
		if ( m_clrRender->a < 255 )
		{
			SetRenderColorA( m_clrRender->a + 1 );
		}
		else
		{
			SetRenderColorA( 255 );
		}
		blend = m_clrRender->a;
		break;
		
	case kRenderFxSolidFast:
		if ( m_clrRender->a < 252 )
		{
			SetRenderColorA( m_clrRender->a + 4 );
		}
		else
		{
			SetRenderColorA( 255 );
		}
		blend = m_clrRender->a;
		break;
		
	case kRenderFxStrobeSlow:
		blend = 20 * sin( gpGlobals->curtime * 4 + offset );
		if ( blend < 0 )
		{
			blend = 0;
		}
		else
		{
			blend = m_clrRender->a;
		}
		break;
		
	case kRenderFxStrobeFast:
		blend = 20 * sin( gpGlobals->curtime * 16 + offset );
		if ( blend < 0 )
		{
			blend = 0;
		}
		else
		{
			blend = m_clrRender->a;
		}
		break;
		
	case kRenderFxStrobeFaster:
		blend = 20 * sin( gpGlobals->curtime * 36 + offset );
		if ( blend < 0 )
		{
			blend = 0;
		}
		else
		{
			blend = m_clrRender->a;
		}
		break;
		
	case kRenderFxFlickerSlow:
		blend = 20 * (sin( gpGlobals->curtime * 2 ) + sin( gpGlobals->curtime * 17 + offset ));
		if ( blend < 0 )
		{
			blend = 0;
		}
		else
		{
			blend = m_clrRender->a;
		}
		break;
		
	case kRenderFxFlickerFast:
		blend = 20 * (sin( gpGlobals->curtime * 16 ) + sin( gpGlobals->curtime * 23 + offset ));
		if ( blend < 0 )
		{
			blend = 0;
		}
		else
		{
			blend = m_clrRender->a;
		}
		break;
		
	case kRenderFxHologram:
	case kRenderFxDistort:
		{
			Vector	tmp;
			float	dist;
			
			VectorCopy(GetEngineObject()->GetAbsOrigin(), tmp );
			VectorSubtract( tmp, CurrentViewOrigin(), tmp );
			dist = DotProduct( tmp, CurrentViewForward() );
			
			// Turn off distance fade
			if ( m_nRenderFX == kRenderFxDistort )
			{
				dist = 1;
			}
			if ( dist <= 0 )
			{
				blend = 0;
			}
			else 
			{
				SetRenderColorA( 180 );
				if ( dist <= 100 )
					blend = m_clrRender->a;
				else
					blend = (int) ((1.0 - (dist - 100) * (1.0 / 400.0)) * m_clrRender->a);
				blend += random->RandomInt(-32,31);
			}
		}
		break;
	
	case kRenderFxNone:
	case kRenderFxClampMinScale:
	default:
		if (m_nRenderMode == kRenderNormal)
			blend = 255;
		else
			blend = m_clrRender->a;
		break;	
		
	}

	blend = clamp( blend, 0, 255 );

	// Look for client-side fades
	unsigned char nFadeAlpha = GetClientSideFade();
	if ( nFadeAlpha != 255 )
	{
		float flBlend = blend / 255.0f;
		float flFade = nFadeAlpha / 255.0f;
		blend = (int)( flBlend * flFade * 255.0f + 0.5f );
		blend = clamp( blend, 0, 255 );
	}

	m_nRenderFXBlend = blend;
	m_nFXComputeFrame = gpGlobals->framecount;

	// Update the render group
	if ( GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
	{
		ClientLeafSystem()->SetRenderGroup( GetRenderHandle(), GetRenderGroup() );
	}

	// Tell our shadow
	if ( m_ShadowHandle != CLIENTSHADOW_INVALID_HANDLE )
	{
		g_pClientShadowMgr->SetFalloffBias( m_ShadowHandle, (255 - m_nRenderFXBlend) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int C_BaseEntity::GetFxBlend( void )
{
	Assert( m_nFXComputeFrame == gpGlobals->framecount );
	return m_nRenderFXBlend;
}

//-----------------------------------------------------------------------------
// Determine the color modulation amount
//-----------------------------------------------------------------------------

void C_BaseEntity::GetColorModulation( float* color )
{
	color[0] = m_clrRender->r / 255.0f;
	color[1] = m_clrRender->g / 255.0f;
	color[2] = m_clrRender->b / 255.0f;
}


//-----------------------------------------------------------------------------
// Returns true if we should add this to the collision list
//-----------------------------------------------------------------------------
CollideType_t C_BaseEntity::GetCollideType( void )
{
	if ( !m_nModelIndex || !model )
		return ENTITY_SHOULD_NOT_COLLIDE;

	if ( !IsSolid( ) )
		return ENTITY_SHOULD_NOT_COLLIDE;

	// If the model is a bsp or studio (i.e. it can collide with the player
	if ( ( modelinfo->GetModelType( model ) != mod_brush ) && ( modelinfo->GetModelType( model ) != mod_studio ) )
		return ENTITY_SHOULD_NOT_COLLIDE;

	// Don't get stuck on point sized entities ( world doesn't count )
	if ( m_nModelIndex != 1 )
	{
		if ( IsPointSized() )
			return ENTITY_SHOULD_NOT_COLLIDE;
	}

	return ENTITY_SHOULD_COLLIDE;
}


//-----------------------------------------------------------------------------
// Is this a brush model?
//-----------------------------------------------------------------------------
bool C_BaseEntity::IsBrushModel() const
{
	int modelType = modelinfo->GetModelType( model );
	return (modelType == mod_brush);
}


//-----------------------------------------------------------------------------
// This method works when we've got a studio model
//-----------------------------------------------------------------------------
void C_BaseEntity::AddStudioDecal( const Ray_t& ray, int hitbox, int decalIndex, 
								  bool doTrace, trace_t& tr, int maxLODToDecal )
{
	if (doTrace)
	{
		enginetrace->ClipRayToEntity( ray, MASK_SHOT, this, &tr );

		// Trace the ray against the entity
		if (tr.fraction == 1.0f)
			return;

		// Set the trace index appropriately...
		tr.m_pEnt = this;
	}

	// Exit out after doing the trace so any other effects that want to happen can happen.
	if ( !r_drawmodeldecals.GetBool() )
		return;

	// Found the point, now lets apply the decals
	CreateModelInstance();

	// FIXME: Pass in decal up?
	Vector up(0, 0, 1);

	if (doTrace && (GetSolid() == SOLID_VPHYSICS) && !tr.startsolid && !tr.allsolid)
	{
		// Choose a more accurate normal direction
		// Also, since we have more accurate info, we can avoid pokethru
		Vector temp;
		VectorSubtract( tr.endpos, tr.plane.normal, temp );
		Ray_t betterRay;
		betterRay.Init( tr.endpos, temp );
		modelrender->AddDecal( m_ModelInstance, betterRay, up, decalIndex, GetStudioBody(), true, maxLODToDecal );
	}
	else
	{
		modelrender->AddDecal( m_ModelInstance, ray, up, decalIndex, GetStudioBody(), false, maxLODToDecal );
	}
}

//-----------------------------------------------------------------------------
void C_BaseEntity::AddColoredStudioDecal( const Ray_t& ray, int hitbox, int decalIndex, 
	bool doTrace, trace_t& tr, Color cColor, int maxLODToDecal )
{
	if (doTrace)
	{
		enginetrace->ClipRayToEntity( ray, MASK_SHOT, this, &tr );

		// Trace the ray against the entity
		if (tr.fraction == 1.0f)
			return;

		// Set the trace index appropriately...
		tr.m_pEnt = this;
	}

	// Exit out after doing the trace so any other effects that want to happen can happen.
	if ( !r_drawmodeldecals.GetBool() )
		return;

	// Found the point, now lets apply the decals
	CreateModelInstance();

	// FIXME: Pass in decal up?
	Vector up(0, 0, 1);

	if (doTrace && (GetSolid() == SOLID_VPHYSICS) && !tr.startsolid && !tr.allsolid)
	{
		// Choose a more accurate normal direction
		// Also, since we have more accurate info, we can avoid pokethru
		Vector temp;
		VectorSubtract( tr.endpos, tr.plane.normal, temp );
		Ray_t betterRay;
		betterRay.Init( tr.endpos, temp );
		modelrender->AddColoredDecal( m_ModelInstance, betterRay, up, decalIndex, GetStudioBody(), cColor, true, maxLODToDecal );
	}
	else
	{
		modelrender->AddColoredDecal( m_ModelInstance, ray, up, decalIndex, GetStudioBody(), cColor, false, maxLODToDecal );
	}
}


//-----------------------------------------------------------------------------
// This method works when we've got a brush model
//-----------------------------------------------------------------------------
void C_BaseEntity::AddBrushModelDecal( const Ray_t& ray, const Vector& decalCenter, 
									  int decalIndex, bool doTrace, trace_t& tr )
{
	if ( doTrace )
	{
		enginetrace->ClipRayToEntity( ray, MASK_SHOT, this, &tr );
		if ( tr.fraction == 1.0f )
			return;
	}

	effects->DecalShoot( decalIndex, entindex(),
		model, GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), decalCenter, 0, 0 );
}


//-----------------------------------------------------------------------------
// A method to apply a decal to an entity
//-----------------------------------------------------------------------------
void C_BaseEntity::AddDecal( const Vector& rayStart, const Vector& rayEnd,
		const Vector& decalCenter, int hitbox, int decalIndex, bool doTrace, trace_t& tr, int maxLODToDecal )
{
	Ray_t ray;
	ray.Init( rayStart, rayEnd );

	// FIXME: Better bloat?
	// Bloat a little bit so we get the intersection
	ray.m_Delta *= 1.1f;

	int modelType = modelinfo->GetModelType( model );
	switch ( modelType )
	{
	case mod_studio:
		AddStudioDecal( ray, hitbox, decalIndex, doTrace, tr, maxLODToDecal );
		break;

	case mod_brush:
		AddBrushModelDecal( ray, decalCenter, decalIndex, doTrace, tr );
		break;

	default:
		// By default, no collision
		tr.fraction = 1.0f;
		break;
	}
}

//-----------------------------------------------------------------------------
void C_BaseEntity::AddColoredDecal( const Vector& rayStart, const Vector& rayEnd,
	const Vector& decalCenter, int hitbox, int decalIndex, bool doTrace, trace_t& tr, Color cColor, int maxLODToDecal )
{
	Ray_t ray;
	ray.Init( rayStart, rayEnd );
	
	// FIXME: Better bloat?
	// Bloat a little bit so we get the intersection
	ray.m_Delta *= 1.1f;

	int modelType = modelinfo->GetModelType( model );
	if ( doTrace )
	{
		enginetrace->ClipRayToEntity( ray, MASK_SHOT, this, &tr );
		switch ( modelType )
		{
		case mod_studio:
			tr.m_pEnt = this;
			break;
		case mod_brush:
			if ( tr.fraction == 1.0f )
				return;		// Explicitly end
		default:
			// By default, no collision
			tr.fraction = 1.0f;
			break;
		}
	}

	switch ( modelType )
	{
	case mod_studio:
		AddColoredStudioDecal( ray, hitbox, decalIndex, doTrace, tr, cColor, maxLODToDecal );
		break;

	case mod_brush:
		{
			color32 cColor32 = { (uint8)cColor.r(), (uint8)cColor.g(), (uint8)cColor.b(), (uint8)cColor.a() };
			effects->DecalColorShoot( decalIndex, entindex(), model, GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), decalCenter, 0, 0, cColor32 );
		}
		break;

	default:
		// By default, no collision
		tr.fraction = 1.0f;
		break;
	}
}

//-----------------------------------------------------------------------------
// A method to remove all decals from an entity
//-----------------------------------------------------------------------------
void C_BaseEntity::RemoveAllDecals( void )
{
	// For now, we only handle removing decals from studiomodels
	if ( modelinfo->GetModelType( model ) == mod_studio )
	{
		CreateModelInstance();
		modelrender->RemoveAllDecals( m_ModelInstance );
	}
}

bool C_BaseEntity::SnatchModelInstance( C_BaseEntity *pToEntity )
{
	if ( !modelrender->ChangeInstance(  GetModelInstance(), pToEntity ) )
		return false;  // engine could move modle handle

	// remove old handle from toentity if any
	if ( pToEntity->GetModelInstance() != MODEL_INSTANCE_INVALID )
		 pToEntity->DestroyModelInstance();

	// move the handle to other entity
	pToEntity->SetModelInstance(  GetModelInstance() );

	// delete own reference
	SetModelInstance( MODEL_INSTANCE_INVALID );

	return true;
}

#include "tier0/memdbgoff.h"

//-----------------------------------------------------------------------------
// C_BaseEntity new/delete
// All fields in the object are all initialized to 0.
//-----------------------------------------------------------------------------
void *C_BaseEntity::operator new( size_t stAllocateBlock )
{
	Assert( stAllocateBlock != 0 );	
	MEM_ALLOC_CREDIT();
	void *pMem = MemAlloc_Alloc( stAllocateBlock );
	memset( pMem, 0, stAllocateBlock );
	return pMem;												
}

void *C_BaseEntity::operator new[]( size_t stAllocateBlock )
{
	Assert( stAllocateBlock != 0 );				
	MEM_ALLOC_CREDIT();
	void *pMem = MemAlloc_Alloc( stAllocateBlock );
	memset( pMem, 0, stAllocateBlock );
	return pMem;												
}

void *C_BaseEntity::operator new( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine )
{
	Assert( stAllocateBlock != 0 );	
	void *pMem = MemAlloc_Alloc( stAllocateBlock, pFileName, nLine );
	memset( pMem, 0, stAllocateBlock );
	return pMem;												
}

void *C_BaseEntity::operator new[]( size_t stAllocateBlock, int nBlockUse, const char *pFileName, int nLine )
{
	Assert( stAllocateBlock != 0 );				
	void *pMem = MemAlloc_Alloc( stAllocateBlock, pFileName, nLine );
	memset( pMem, 0, stAllocateBlock );
	return pMem;												
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pMem - 
//-----------------------------------------------------------------------------
void C_BaseEntity::operator delete( void *pMem )
{
	// get the engine to free the memory
	MemAlloc_Free( pMem );
}

#include "tier0/memdbgon.h"

//========================================================================================
// TEAM HANDLING
//========================================================================================
C_Team *C_BaseEntity::GetTeam( void )
{
	return GetGlobalTeam( m_iTeamNum );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int C_BaseEntity::GetTeamNumber( void ) const
{
	return m_iTeamNum;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int	C_BaseEntity::GetRenderTeamNumber( void )
{
	return GetTeamNumber();
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if these entities are both in at least one team together
//-----------------------------------------------------------------------------
bool C_BaseEntity::InSameTeam( C_BaseEntity *pEntity )
{
	if ( !pEntity )
		return false;

	return ( pEntity->GetTeam() == GetTeam() );
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if the entity's on the same team as the local player
//-----------------------------------------------------------------------------
bool C_BaseEntity::InLocalTeam( void )
{
	return ( GetTeam() == GetLocalTeam() );
}


void C_BaseEntity::SetNextClientThink( float nextThinkTime )
{
	Assert( GetClientHandle() != INVALID_CLIENTENTITY_HANDLE );
	ClientThinkList()->SetNextClientThink( GetClientHandle(), nextThinkTime );
}

void C_BaseEntity::AddToLeafSystem()
{
	AddToLeafSystem( GetRenderGroup() );
}

void C_BaseEntity::AddToLeafSystem( RenderGroup_t group )
{
	if( m_hRender == INVALID_CLIENT_RENDER_HANDLE )
	{
		// create new renderer handle
		ClientLeafSystem()->AddRenderable( this, group );
		ClientLeafSystem()->EnableAlternateSorting( m_hRender, m_bAlternateSorting );
	}
	else
	{
		// handle already exists, just update group & origin
		ClientLeafSystem()->SetRenderGroup( m_hRender, group );
		ClientLeafSystem()->RenderableChanged( m_hRender );
	}
}


//-----------------------------------------------------------------------------
// Creates the shadow (if it doesn't already exist) based on shadow cast type
//-----------------------------------------------------------------------------
void C_BaseEntity::CreateShadow()
{
	ShadowType_t shadowType = ShadowCastType();
	if (shadowType == SHADOWS_NONE)
	{
		DestroyShadow();
	}
	else
	{
		if (m_ShadowHandle == CLIENTSHADOW_INVALID_HANDLE)
		{
			int flags = SHADOW_FLAGS_SHADOW;
			if (shadowType != SHADOWS_SIMPLE)
				flags |= SHADOW_FLAGS_USE_RENDER_TO_TEXTURE;
			if (shadowType == SHADOWS_RENDER_TO_TEXTURE_DYNAMIC)
				flags |= SHADOW_FLAGS_ANIMATING_SOURCE;
			m_ShadowHandle = g_pClientShadowMgr->CreateShadow(GetClientHandle(), flags);
		}
	}
}

//-----------------------------------------------------------------------------
// Removes the shadow
//-----------------------------------------------------------------------------
void C_BaseEntity::DestroyShadow()
{
	// NOTE: This will actually cause the shadow type to be recomputed
	// if the entity doesn't immediately go away
	if (m_ShadowHandle != CLIENTSHADOW_INVALID_HANDLE)
	{
		g_pClientShadowMgr->DestroyShadow(m_ShadowHandle);
		m_ShadowHandle = CLIENTSHADOW_INVALID_HANDLE;
	}
}


//-----------------------------------------------------------------------------
// Removes the entity from the leaf system
//-----------------------------------------------------------------------------
void C_BaseEntity::RemoveFromLeafSystem()
{
	// Detach from the leaf lists.
	if( m_hRender != INVALID_CLIENT_RENDER_HANDLE )
	{
		ClientLeafSystem()->RemoveRenderable( m_hRender );
		m_hRender = INVALID_CLIENT_RENDER_HANDLE;
	}
	DestroyShadow();
}


//-----------------------------------------------------------------------------
// Purpose: Flags this entity as being inside or outside of this client's PVS
//			on the server.
//			NOTE: this is meaningless for client-side only entities.
// Input  : inside_pvs - 
//-----------------------------------------------------------------------------
void C_BaseEntity::SetDormant( bool bDormant )
{
	Assert( IsServerEntity() );
	m_bDormant = bDormant;

	// Kill drawing if we became dormant.
	UpdateVisibility();

	ParticleProp()->OwnerSetDormantTo( bDormant );
}

//-----------------------------------------------------------------------------
// Purpose: Returns whether this entity is dormant. Client/server entities become
//			dormant when they leave the PVS on the server. Client side entities
//			can decide for themselves whether to become dormant.
//-----------------------------------------------------------------------------
bool C_BaseEntity::IsDormant( void )
{
	if ( IsServerEntity() )
	{
		return m_bDormant;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Tells the entity that it's about to be destroyed due to the client receiving
// an uncompressed update that's caused it to destroy all entities & recreate them.
//-----------------------------------------------------------------------------
void C_BaseEntity::SetDestroyedOnRecreateEntities( void )
{
	// Robin: We need to destroy all our particle systems immediately, because 
	// we're about to be recreated, and their owner EHANDLEs will match up to 
	// the new entity, but it won't know anything about them.
	ParticleProp()->StopEmissionAndDestroyImmediately();
}



/*
void C_BaseEntity::SetAbsAngularVelocity( const QAngle &vecAbsAngVelocity )
{
	// The abs velocity won't be dirty since we're setting it here
	InvalidatePhysicsRecursive( EFL_DIRTY_ABSANGVELOCITY );
	m_iEFlags &= ~EFL_DIRTY_ABSANGVELOCITY;

	m_vecAbsAngVelocity = vecAbsAngVelocity;

	C_BaseEntity *pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		m_vecAngVelocity = vecAbsAngVelocity;
		return;
	}

	// First subtract out the parent's abs velocity to get a relative
	// angular velocity measured in world space
	QAngle relAngVelocity;
	relAngVelocity = vecAbsAngVelocity - pMoveParent->GetAbsAngularVelocity();

	matrix3x4_t entityToWorld;
	AngleMatrix( relAngVelocity, entityToWorld );

	// Moveparent case: transform the abs angular vel into local space
	matrix3x4_t worldToParent, localMatrix;
	MatrixInvert( pMoveParent->EntityToWorldTransform(), worldToParent );
	ConcatTransforms( worldToParent, entityToWorld, localMatrix );
	MatrixAngles( localMatrix, m_vecAngVelocity );
}
*/




void C_BaseEntity::SetLocalAngularVelocity( const QAngle &vecAngVelocity )
{
	if (m_vecAngVelocity != vecAngVelocity)
	{
//		InvalidatePhysicsRecursive( ANG_VELOCITY_CHANGED );
		m_vecAngVelocity = vecAngVelocity;
	}
}




//-----------------------------------------------------------------------------
// Sets the local position from a transform
//-----------------------------------------------------------------------------
void C_BaseEntity::SetLocalTransform( const matrix3x4_t &localTransform )
{
	Vector vecLocalOrigin;
	QAngle vecLocalAngles;
	MatrixGetColumn( localTransform, 3, vecLocalOrigin );
	MatrixAngles( localTransform, vecLocalAngles );
	GetEngineObject()->SetLocalOrigin( vecLocalOrigin );
	GetEngineObject()->SetLocalAngles( vecLocalAngles );
}


//-----------------------------------------------------------------------------
// FIXME: REMOVE!!!
//-----------------------------------------------------------------------------
void C_BaseEntity::MoveToAimEnt( )
{
	Vector vecAimEntOrigin;
	QAngle vecAimEntAngles;
	GetAimEntOrigin((GetEngineObject()->GetMoveParent()?GetEngineObject()->GetMoveParent()->GetOuter():NULL), &vecAimEntOrigin, &vecAimEntAngles);
	GetEngineObject()->SetAbsOrigin( vecAimEntOrigin );
	GetEngineObject()->SetAbsAngles( vecAimEntAngles );
}


void C_BaseEntity::BoneMergeFastCullBloat( Vector &localMins, Vector &localMaxs, const Vector &thisEntityMins, const Vector &thisEntityMaxs ) const
{
	// By default, we bloat the bbox for fastcull ents by the maximum length it could hang out of the parent bbox,
	// it one corner were touching the edge of the parent's box, and the whole diagonal stretched out.
	float flExpand = (thisEntityMaxs - thisEntityMins).Length();

	localMins.x -= flExpand;
	localMins.y -= flExpand;
	localMins.z -= flExpand;

	localMaxs.x += flExpand;
	localMaxs.y += flExpand;
	localMaxs.z += flExpand;
}




/*
void C_BaseEntity::CalcAbsoluteAngularVelocity()
{
	if ((m_iEFlags & EFL_DIRTY_ABSANGVELOCITY ) == 0)
		return;

	m_iEFlags &= ~EFL_DIRTY_ABSANGVELOCITY;

	CBaseEntity *pMoveParent = GetMoveParent();
	if ( !pMoveParent )
	{
		m_vecAbsAngVelocity = m_vecAngVelocity;
		return;
	}

	matrix3x4_t angVelToParent, angVelToWorld;
	AngleMatrix( m_vecAngVelocity, angVelToParent );
	ConcatTransforms( pMoveParent->EntityToWorldTransform(), angVelToParent, angVelToWorld );
	MatrixAngles( angVelToWorld, m_vecAbsAngVelocity );

	// Now add in the parent abs angular velocity
	m_vecAbsAngVelocity += pMoveParent->GetAbsAngularVelocity();
}
*/






//-----------------------------------------------------------------------------
// Mark shadow as dirty 
//-----------------------------------------------------------------------------
void C_BaseEntity::MarkRenderHandleDirty( )
{
	// Invalidate render leaf too
	ClientRenderHandle_t handle = GetRenderHandle();
	if ( handle != INVALID_CLIENT_RENDER_HANDLE )
	{
		ClientLeafSystem()->RenderableChanged( handle );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::ShutdownPredictable( void )
{
#if !defined( NO_ENTITY_PREDICTION )
	Assert( GetPredictable() );

	predictables->RemoveFromPredictablesList( GetClientHandle() );
	GetEngineObject()->DestroyIntermediateData();
	SetPredictable( false );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Turn entity into something the predicts locally
//-----------------------------------------------------------------------------
void C_BaseEntity::InitPredictable( void )
{
#if !defined( NO_ENTITY_PREDICTION )
	Assert( !GetPredictable() );

	// Mark as predictable
	SetPredictable( true );
	// Allocate buffers into which we copy data
	GetEngineObject()->AllocateIntermediateData();
	// Add to list of predictables
	predictables->AddToPredictableList( GetClientHandle() );
	// Copy everything from "this" into the original_state_data
	//  object.  Don't care about client local stuff, so pull from slot 0 which

	//  should be empty anyway...
	GetEngineObject()->PostNetworkDataReceived( 0 );

	// Copy original data into all prediction slots, so we don't get an error saying we "mispredicted" any
	//  values which are still at their initial values
	for ( int i = 0; i < MULTIPLAYER_BACKUP; i++ )
	{
		GetEngineObject()->SaveData( "InitPredictable", i, PC_EVERYTHING );
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : state - 
//-----------------------------------------------------------------------------
void C_BaseEntity::SetPredictable( bool state )
{
	m_bPredictable = state;

	// update interpolation times
	GetEngineObject()->Interp_UpdateInterpolationAmounts(GetEngineObject()->GetVarMapping() );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_BaseEntity::GetPredictable( void ) const
{
	return m_bPredictable;
}



// Stuff implemented for weapon prediction code
void C_BaseEntity::SetSize( const Vector &vecMin, const Vector &vecMax )
{
	SetCollisionBounds( vecMin, vecMax );
}

//-----------------------------------------------------------------------------
// Purpose: Just look up index
// Input  : *name - 
// Output : int
//-----------------------------------------------------------------------------
int C_BaseEntity::PrecacheModel( const char *name )
{
	return modelinfo->GetModelIndex( name );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *obj - 
//-----------------------------------------------------------------------------
void C_BaseEntity::Remove( )
{
	// Nothing for now, if it's a predicted entity, could flag as "delete" or dormant
	if ( GetPredictable() /*|| IsClientCreated()*/)
	{
		// Make it solid
		AddSolidFlags( FSOLID_NOT_SOLID );
		SetMoveType( MOVETYPE_NONE );

		GetEngineObject()->AddEFlags( EFL_KILLME );	// Make sure to ignore further calls into here or UTIL_Remove.
	}

	Release();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
//bool C_BaseEntity::GetPredictionEligible( void ) const
//{
//#if !defined( NO_ENTITY_PREDICTION )
//	return m_bPredictionEligible;
//#else
//	return false;
//#endif
//}


C_BaseEntity* C_BaseEntity::Instance( CBaseHandle hEnt )
{
	return ClientEntityList().GetBaseEntityFromHandle( hEnt );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iEnt - 
// Output : C_BaseEntity
//-----------------------------------------------------------------------------
C_BaseEntity *C_BaseEntity::Instance( int iEnt )
{
	return ClientEntityList().GetBaseEntity( iEnt );
}

#ifdef WIN32
#pragma warning( push )
#include <typeinfo>
#pragma warning( pop )
#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Output : char const
//-----------------------------------------------------------------------------
const char *C_BaseEntity::GetClassname( void )
{
	static char outstr[ 256 ];
	outstr[ 0 ] = 0;
	bool gotname = false;
#ifndef NO_ENTITY_PREDICTION
	if ( GetPredDescMap() )
	{
		const char *mapname =  GetEntityMapClassName( GetPredDescMap()->dataClassName );
		if ( mapname && mapname[ 0 ] ) 
		{
			Q_strncpy( outstr, mapname, sizeof( outstr ) );
			gotname = true;
		}
	}
#endif

	if ( !gotname )
	{
		Q_strncpy( outstr, typeid( *this ).name(), sizeof( outstr ) );
	}

	return outstr;
}

const char *C_BaseEntity::GetDebugName( void )
{
	return GetClassname();
}

//-----------------------------------------------------------------------------
// Purpose: Creates an entity by string name, but does not spawn it
// Input  : *className - 
// Output : C_BaseEntity
//-----------------------------------------------------------------------------
//C_BaseEntity *CreateEntityByName( const char *className )
//{
//	C_BaseEntity *ent = GetClassMap().CreateEntity( className );
//	if ( ent )
//	{
//		return ent;
//	}
//
//	Warning( "Can't find factory for entity: %s\n", className );
//	return NULL;
//}

#ifdef _DEBUG
CON_COMMAND( cl_sizeof, "Determines the size of the specified client class." )
{
	if ( args.ArgC() != 2 )
	{
		Msg( "cl_sizeof <gameclassname>\n" );
		return;
	}

	int size = GetEntitySize( args[ 1 ] );

	Msg( "%s is %i bytes\n", args[ 1 ], size );
}
#endif

CON_COMMAND_F( dlight_debug, "Creates a dlight in front of the player", FCVAR_CHEAT )
{
	dlight_t *el = effects->CL_AllocDlight( 1 );
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player )
		return;
	Vector start = player->EyePosition();
	Vector forward;
	player->EyeVectors( &forward );
	Vector end = start + forward * MAX_TRACE_LENGTH;
	trace_t tr;
	UTIL_TraceLine( start, end, MASK_SHOT_HULL & (~CONTENTS_GRATE), player, COLLISION_GROUP_NONE, &tr );
	el->origin = tr.endpos - forward * 12.0f;
	el->radius = 200; 
	el->decay = el->radius / 5.0f;
	el->die = gpGlobals->curtime + 5.0f;
	el->color.r = 255;
	el->color.g = 192;
	el->color.b = 64;
	el->color.exponent = 5;

}
//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
//bool C_BaseEntity::IsClientCreated( void ) const
//{
//#ifndef NO_ENTITY_PREDICTION
//	if ( m_pPredictionContext != NULL )
//	{
//		// For now can't be both
//		Assert( !GetPredictable() );
//		return true;
//	}
//#endif
//	return false;
//}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *classname - 
//			*module - 
//			line - 
// Output : C_BaseEntity
//-----------------------------------------------------------------------------
//C_BaseEntity *C_BaseEntity::CreatePredictedEntityByName( const char *classname, const char *module, int line, bool persist /*= false */ )
//{
//#if !defined( NO_ENTITY_PREDICTION )
//	C_BasePlayer *player = C_BaseEntity::GetPredictionPlayer();
//
//	Assert( player );
//	Assert( player->m_pCurrentCommand );
//	Assert( prediction->InPrediction() );
//
//	C_BaseEntity *ent = NULL;
//
//	// What's my birthday (should match server)
//	int command_number	= player->m_pCurrentCommand->command_number;
//	// Who's my daddy?
//	int player_index	= player->entindex() - 1;
//
//	// Create id/context
//	CPredictableId testId;
//	testId.Init( player_index, command_number, classname, module, line );
//
//	// If repredicting, should be able to find the entity in the previously created list
//	if ( !prediction->IsFirstTimePredicted() )
//	{
//		// Only find previous instance if entity was created with persist set
//		if ( persist )
//		{
//			ent = FindPreviouslyCreatedEntity( testId );
//			if ( ent )
//			{
//				return ent;
//			}
//		}
//
//		return NULL;
//	}
//
//	// Try to create it
//	ent = cl_entitylist->CreateEntityByName( classname );
//	if ( !ent )
//	{
//		return NULL;
//	}
//
//	// It's predictable
//	ent->SetPredictionEligible( true );
//
//	// Set up "shared" id number
//	ent->m_PredictableID.SetRaw( testId.GetRaw() );
//
//	// Get a context (mostly for debugging purposes)
//	PredictionContext *context			= new PredictionContext;
//	context->m_bActive					= true;
//	context->m_nCreationCommandNumber	= command_number;
//	context->m_nCreationLineNumber		= line;
//	context->m_pszCreationModule		= module;
//
//	// Attach to entity
//	ent->m_pPredictionContext = context;
//
//	// Add to client entity list
//	//ClientEntityList().AddNonNetworkableEntity( ent );
//
//	//  and predictables
//	g_Predictables.AddToPredictableList( ent->GetClientHandle() );
//
//	// Duhhhh..., but might as well be safe
//	Assert( !ent->GetPredictable() );
//	Assert( ent->IsClientCreated() );
//
//	// Add the client entity to the spatial partition. (Collidable)
//	ent->CollisionProp()->CreatePartitionHandle();
//
//	// CLIENT ONLY FOR NOW!!!
//	ent->index = -1;
//
//	if ( AddDataChangeEvent( ent, DATA_UPDATE_CREATED, &ent->m_DataChangeEventRef ) )
//	{
//		ent->OnPreDataChanged( DATA_UPDATE_CREATED );
//	}
//
//	ent->Interp_UpdateInterpolationAmounts( ent->GetVarMapping() );
//	
//	return ent;
//#else
//	return NULL;
//#endif
//}

//-----------------------------------------------------------------------------
// Purpose: Called each packet that the entity is created on and finally gets called after the next packet
//  that doesn't have a create message for the "parent" entity so that the predicted version
//  can be removed.  Return true to delete entity right away.
//-----------------------------------------------------------------------------
//bool C_BaseEntity::OnPredictedEntityRemove( bool isbeingremoved, C_BaseEntity *predicted )
//{
//#if !defined( NO_ENTITY_PREDICTION )
//	// Nothing right now, but in theory you could look at the error in origins and set
//	//  up something to smooth out the error
//	PredictionContext *ctx = predicted->m_pPredictionContext;
//	Assert( ctx );
//	if ( ctx )
//	{
//		// Create backlink to actual entity
//		ctx->m_hServerEntity = this;
//
//		/*
//		Msg( "OnPredictedEntity%s:  %s created %s(%i) instance(%i)\n",
//			isbeingremoved ? "Remove" : "Acknowledge",
//			predicted->GetClassname(),
//			ctx->m_pszCreationModule,
//			ctx->m_nCreationLineNumber,
//			predicted->m_PredictableID.GetInstanceNumber() );
//		*/
//	}
//
//	// If it comes through with an ID, it should be eligible
//	SetPredictionEligible( true );
//
//	// Start predicting simulation forward from here
//	CheckInitPredictable( "OnPredictedEntityRemove" );
//
//	// Always mark it dormant since we are the "real" entity now
//	predicted->SetDormantPredictable( true );
//
//	InvalidatePhysicsRecursive( POSITION_CHANGED | ANGLES_CHANGED | VELOCITY_CHANGED );
//
//	// By default, signal that it should be deleted right away
//	// If a derived class implements this method, it might chain to here but return
//	// false if it wants to keep the dormant predictable around until the chain of
//	//  DATA_UPDATE_CREATED messages passes
//#endif
//	return true;
//}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pOwner - 
//-----------------------------------------------------------------------------
void C_BaseEntity::SetOwnerEntity( C_BaseEntity *pOwner )
{
	m_hOwnerEntity = pOwner;
}

//-----------------------------------------------------------------------------
// Purpose: Put the entity in the specified team
//-----------------------------------------------------------------------------
void C_BaseEntity::ChangeTeam( int iTeamNum )
{
	m_iTeamNum = iTeamNum;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : name - 
//-----------------------------------------------------------------------------
void C_BaseEntity::SetModelName( string_t name )
{
	m_ModelName = name;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : string_t
//-----------------------------------------------------------------------------
string_t C_BaseEntity::GetModelName( void ) const
{
	return m_ModelName;
}

//-----------------------------------------------------------------------------
// Purpose: Nothing yet, could eventually supercede Term()
//-----------------------------------------------------------------------------
void C_BaseEntity::UpdateOnRemove( void )
{
	VPhysicsDestroyObject(); 

	Assert( !GetEngineObject()->GetMoveParent() );
	GetEngineObject()->UnlinkFromHierarchy();
	GetEngineObject()->SetGroundEntity( NULL );

	Term();
	ClearDataChangedEvent(m_DataChangeEventRef);
//#if !defined( NO_ENTITY_PREDICTION )
//	delete m_pPredictionContext;
//#endif
	RemoveFromInterpolationList();
	RemoveFromTeleportList();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : canpredict - 
//-----------------------------------------------------------------------------
//void C_BaseEntity::SetPredictionEligible( bool canpredict )
//{
//#if !defined( NO_ENTITY_PREDICTION )
//	m_bPredictionEligible = canpredict;
//#endif
//}


//-----------------------------------------------------------------------------
// Purpose: Returns a value that scales all damage done by this entity.
//-----------------------------------------------------------------------------
float C_BaseEntity::GetAttackDamageScale( void )
{
	float flScale = 1;
// Not hooked up to prediction yet
#if 0
	FOR_EACH_LL( m_DamageModifiers, i )
	{
		if ( !m_DamageModifiers[i]->IsDamageDoneToMe() )
		{
			flScale *= m_DamageModifiers[i]->GetModifier();
		}
	}
#endif
	return flScale;
}

//#if !defined( NO_ENTITY_PREDICTION )
////-----------------------------------------------------------------------------
//// Purpose: 
//// Output : Returns true on success, false on failure.
////-----------------------------------------------------------------------------
//bool C_BaseEntity::IsDormantPredictable( void ) const
//{
//	return m_bDormantPredictable;
//}
//#endif
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : dormant - 
//-----------------------------------------------------------------------------
//void C_BaseEntity::SetDormantPredictable( bool dormant )
//{
//#if !defined( NO_ENTITY_PREDICTION )
//	Assert( IsClientCreated() );
//
//	m_bDormantPredictable = true;
//	m_nIncomingPacketEntityBecameDormant = prediction->GetIncomingPacketNumber();
//
//// Do we need to do the following kinds of things?
//#if 0
//	// Remove from collisions
//	SetSolid( SOLID_NOT );
//	// Don't render
//	AddEffects( EF_NODRAW );
//#endif
//#endif
//}

//-----------------------------------------------------------------------------
// Purpose: Used to determine when a dorman client predictable can be safely deleted
//  Note that it can be deleted earlier than this by OnPredictedEntityRemove returning true
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
//bool C_BaseEntity::BecameDormantThisPacket( void ) const
//{
//#if !defined( NO_ENTITY_PREDICTION )
//	Assert( IsDormantPredictable() );
//
//	if ( m_nIncomingPacketEntityBecameDormant != prediction->GetIncomingPacketNumber() )
//		return false;
//
//	return true;
//#else
//	return false;
//#endif
//}





// Convenient way to delay removing oneself
void C_BaseEntity::SUB_Remove( void )
{
	if (m_iHealth > 0)
	{
		// this situation can screw up NPCs who can't tell their entity pointers are invalid.
		m_iHealth = 0;
		DevWarning( 2, "SUB_Remove called on entity with health > 0\n");
	}

	Remove( );
}

CBaseEntity *FindEntityInFrontOfLocalPlayer()
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		// Get the entity under my crosshair
		trace_t tr;
		Vector forward;
		pPlayer->EyeVectors( &forward );
		UTIL_TraceLine( pPlayer->EyePosition(), pPlayer->EyePosition() + forward * MAX_COORD_RANGE,	MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction != 1.0 && tr.DidHitNonWorldEntity() )
		{
			return (C_BaseEntity*)tr.m_pEnt;
		}
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Debug command to wipe the decals off an entity
//-----------------------------------------------------------------------------
static void RemoveDecals_f( void )
{
	CBaseEntity *pHit = FindEntityInFrontOfLocalPlayer();
	if ( pHit )
	{
		pHit->RemoveAllDecals();
	}
}

static ConCommand cl_removedecals( "cl_removedecals", RemoveDecals_f, "Remove the decals from the entity under the crosshair.", FCVAR_CHEAT );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::ToggleBBoxVisualization( int fVisFlags )
{
	if ( m_fBBoxVisFlags & fVisFlags )
	{
		m_fBBoxVisFlags &= ~fVisFlags;
	}
	else
	{
		m_fBBoxVisFlags |= fVisFlags;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static void ToggleBBoxVisualization( int fVisFlags, const CCommand &args )
{
	CBaseEntity *pHit;

	int iEntity = -1;
	if ( args.ArgC() >= 2 )
	{
		iEntity = atoi( args[ 1 ] );
	}

	if ( iEntity == -1 )
	{
		pHit = FindEntityInFrontOfLocalPlayer();
	}
	else
	{
		pHit = cl_entitylist->GetBaseEntity( iEntity );
	}

	if ( pHit )
	{
		pHit->ToggleBBoxVisualization( fVisFlags );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Command to toggle visualizations of bboxes on the client
//-----------------------------------------------------------------------------
CON_COMMAND_F( cl_ent_bbox, "Displays the client's bounding box for the entity under the crosshair.", FCVAR_CHEAT )
{
	ToggleBBoxVisualization( CBaseEntity::VISUALIZE_COLLISION_BOUNDS, args );
}


//-----------------------------------------------------------------------------
// Purpose: Command to toggle visualizations of bboxes on the client
//-----------------------------------------------------------------------------
CON_COMMAND_F( cl_ent_absbox, "Displays the client's absbox for the entity under the crosshair.", FCVAR_CHEAT )
{
	ToggleBBoxVisualization( CBaseEntity::VISUALIZE_SURROUNDING_BOUNDS, args );
}


//-----------------------------------------------------------------------------
// Purpose: Command to toggle visualizations of bboxes on the client
//-----------------------------------------------------------------------------
CON_COMMAND_F( cl_ent_rbox, "Displays the client's render box for the entity under the crosshair.", FCVAR_CHEAT )
{
	ToggleBBoxVisualization( CBaseEntity::VISUALIZE_RENDER_BOUNDS, args );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseEntity::DrawBBoxVisualizations( void )
{
	if ( m_fBBoxVisFlags & VISUALIZE_COLLISION_BOUNDS )
	{
		debugoverlay->AddBoxOverlay( CollisionProp()->GetCollisionOrigin(), CollisionProp()->OBBMins(),
			CollisionProp()->OBBMaxs(), CollisionProp()->GetCollisionAngles(), 190, 190, 0, 0, 0.01 );
	}

	if ( m_fBBoxVisFlags & VISUALIZE_SURROUNDING_BOUNDS )
	{
		Vector vecSurroundMins, vecSurroundMaxs;
		CollisionProp()->WorldSpaceSurroundingBounds( &vecSurroundMins, &vecSurroundMaxs );
		debugoverlay->AddBoxOverlay( vec3_origin, vecSurroundMins,
			vecSurroundMaxs, vec3_angle, 0, 255, 255, 0, 0.01 );
	}

	if ( m_fBBoxVisFlags & VISUALIZE_RENDER_BOUNDS || r_drawrenderboxes.GetInt() )
	{
		Vector vecRenderMins, vecRenderMaxs;
		GetRenderBounds( vecRenderMins, vecRenderMaxs );
		debugoverlay->AddBoxOverlay( GetRenderOrigin(), vecRenderMins, vecRenderMaxs,
			GetRenderAngles(), 255, 0, 255, 0, 0.01 );
	}
}


//-----------------------------------------------------------------------------
// Sets the render mode
//-----------------------------------------------------------------------------
void C_BaseEntity::SetRenderMode( RenderMode_t nRenderMode, bool bForceUpdate )
{
	m_nRenderMode = nRenderMode;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : RenderGroup_t
//-----------------------------------------------------------------------------
RenderGroup_t C_BaseEntity::GetRenderGroup()
{
	// Don't sort things that don't need rendering
	if ( m_nRenderMode == kRenderNone )
		return RENDER_GROUP_OPAQUE_ENTITY;

	// When an entity has a material proxy, we have to recompute
	// translucency here because the proxy may have changed it.
	if (modelinfo->ModelHasMaterialProxy( GetModel() ))
	{
		modelinfo->RecomputeTranslucency( const_cast<model_t*>(GetModel()), GetSkin(), GetBody(), GetClientRenderable() );
	}

	// NOTE: Bypassing the GetFXBlend protection logic because we want this to
	// be able to be called from AddToLeafSystem.
	int nTempComputeFrame = m_nFXComputeFrame;
	m_nFXComputeFrame = gpGlobals->framecount;

	int nFXBlend = GetFxBlend();

	m_nFXComputeFrame = nTempComputeFrame;

	// Don't need to sort invisible stuff
	if ( nFXBlend == 0 )
		return RENDER_GROUP_OPAQUE_ENTITY;

		// Figure out its RenderGroup.
	int modelType = modelinfo->GetModelType( model );
	RenderGroup_t renderGroup = (modelType == mod_brush) ? RENDER_GROUP_OPAQUE_BRUSH : RENDER_GROUP_OPAQUE_ENTITY;
	if ( ( nFXBlend != 255 ) || IsTransparent() )
	{
		if ( m_nRenderMode != kRenderEnvironmental )
		{
			renderGroup = RENDER_GROUP_TRANSLUCENT_ENTITY;
		}
		else
		{
			renderGroup = RENDER_GROUP_OTHER;
		}
	}

	if ( ( renderGroup == RENDER_GROUP_TRANSLUCENT_ENTITY ) &&
		 ( modelinfo->IsTranslucentTwoPass( model ) ) )
	{
		renderGroup = RENDER_GROUP_TWOPASS;
	}

	return renderGroup;
}

void C_BaseEntity::OnPostRestoreData()
{
	if (GetEngineObject()->GetMoveParent() )
	{
		AddToAimEntsList();
	}

	// If our model index has changed, then make sure it's reflected in our model pointer.
	// (Mostly superseded by new modelindex delta check in RestoreData, but I'm leaving it
	// because it might be band-aiding any other missed calls to SetModelByIndex --henryg)
	if ( GetModel() != modelinfo->GetModel( GetModelIndex() ) )
	{
		MDLCACHE_CRITICAL_SECTION();
		SetModelByIndex( GetModelIndex() );
	}
}




void C_BaseEntity::ResetLatched()
{
//	if ( IsClientCreated() )
//		return;

	GetEngineObject()->Interp_Reset(GetEngineObject()->GetVarMapping() );
}

//-----------------------------------------------------------------------------
// Purpose: Fixme, this needs a better solution
// Input  : flags - 
// Output : float
//-----------------------------------------------------------------------------

static float AdjustInterpolationAmount( C_BaseEntity *pEntity, float baseInterpolation )
{
	if ( cl_interp_npcs.GetFloat() > 0 )
	{
		const float minNPCInterpolationTime = cl_interp_npcs.GetFloat();
		const float minNPCInterpolation = TICK_INTERVAL * ( TIME_TO_TICKS( minNPCInterpolationTime ) + 1 );

		if ( minNPCInterpolation > baseInterpolation )
		{
			while ( pEntity )
			{
				if ( pEntity->IsNPC() )
					return minNPCInterpolation;

				pEntity = pEntity->GetEngineObject()->GetMoveParent()?pEntity->GetEngineObject()->GetMoveParent()->GetOuter():NULL;
			}
		}
	}

	return baseInterpolation;
}

//-------------------------------------
float C_BaseEntity::GetInterpolationAmount( int flags )
{
	// If single player server is "skipping ticks" everything needs to interpolate for a bit longer
	int serverTickMultiple = 1;
	if ( IsSimulatingOnAlternateTicks() )
	{
		serverTickMultiple = 2;
	}

	if ( GetPredictable() /*|| IsClientCreated()*/)
	{
		return TICK_INTERVAL * serverTickMultiple;
	}

	// Always fully interpolate during multi-player or during demo playback, if the recorded
	// demo was recorded locally.
	const bool bPlayingDemo = engine->IsPlayingDemo();
	const bool bPlayingMultiplayer = !bPlayingDemo && ( gpGlobals->maxClients > 1 );
	const bool bPlayingNonLocallyRecordedDemo = bPlayingDemo && !engine->IsPlayingDemoALocallyRecordedDemo();
	if ( bPlayingMultiplayer || bPlayingNonLocallyRecordedDemo )
	{
		return AdjustInterpolationAmount( this, TICKS_TO_TIME( TIME_TO_TICKS( GetClientInterpAmount() ) + serverTickMultiple ) );
	}

	int expandedServerTickMultiple = serverTickMultiple;
	if ( IsEngineThreaded() )
	{
		expandedServerTickMultiple += g_nThreadModeTicks;
	}

	if ( IsAnimatedEveryTick() && IsSimulatedEveryTick() )
	{
		return TICK_INTERVAL * expandedServerTickMultiple;
	}

	if ( ( flags & LATCH_ANIMATION_VAR ) && IsAnimatedEveryTick() )
	{
		return TICK_INTERVAL * expandedServerTickMultiple;
	}
	if ( ( flags & LATCH_SIMULATION_VAR ) && IsSimulatedEveryTick() )
	{
		return TICK_INTERVAL * expandedServerTickMultiple;
	}

	return AdjustInterpolationAmount( this, TICKS_TO_TIME( TIME_TO_TICKS( GetClientInterpAmount() ) + serverTickMultiple ) );
}


float C_BaseEntity::GetLastChangeTime( int flags )
{
	if ( GetPredictable() /*|| IsClientCreated()*/)
	{
		return gpGlobals->curtime;
	}
	
	// make sure not both flags are set, we can't resolve that
	Assert( !( (flags & LATCH_ANIMATION_VAR) && (flags & LATCH_SIMULATION_VAR) ) );
	
	if ( flags & LATCH_ANIMATION_VAR )
	{
		return GetAnimTime();
	}

	if ( flags & LATCH_SIMULATION_VAR )
	{
		float st = GetSimulationTime();
		if ( st == 0.0f )
		{
			return gpGlobals->curtime;
		}
		return st;
	}

	Assert( 0 );

	return gpGlobals->curtime;
}



//-----------------------------------------------------------------------------
// Simply here for game shared 
//-----------------------------------------------------------------------------
bool C_BaseEntity::IsFloating()
{
	// NOTE: This is only here because it's called by game shared.
	// The server uses it to lower falling impact damage
	return false;
}


BEGIN_DATADESC_NO_BASE( C_BaseEntity )
	DEFINE_FIELD( m_ModelName, FIELD_STRING ),
	//DEFINE_CUSTOM_FIELD_INVALID( m_vecAbsOrigin, engineObjectFuncs),
	//DEFINE_CUSTOM_FIELD_INVALID( m_angAbsRotation, engineObjectFuncs),
	DEFINE_FIELD( m_fFlags, FIELD_INTEGER ),
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_BaseEntity::ShouldSavePhysics()
{
	return false;
}

//-----------------------------------------------------------------------------
// handler to do stuff before you are saved
//-----------------------------------------------------------------------------
void C_BaseEntity::OnSave()
{

}


//-----------------------------------------------------------------------------
// handler to do stuff after you are restored
//-----------------------------------------------------------------------------
void C_BaseEntity::OnRestore()
{	
	UpdatePartitionListEntry();
	CollisionProp()->UpdatePartition();

	UpdateVisibility();
}

//-----------------------------------------------------------------------------
// Purpose: Saves the current object out to disk, by iterating through the objects
//			data description hierarchy
// Input  : &save - save buffer which the class data is written to
// Output : int	- 0 if the save failed, 1 on success
//-----------------------------------------------------------------------------
int C_BaseEntity::Save( ISave &save )
{
	// loop through the data description list, saving each data desc block
	int status = save.WriteEntity(this);

	return status;
}

//-----------------------------------------------------------------------------
// Purpose: Recursively saves all the classes in an object, in reverse order (top down)
// Output : int 0 on failure, 1 on success
//-----------------------------------------------------------------------------
//int C_BaseEntity::SaveDataDescBlock( ISave &save, datamap_t *dmap )
//{
//	int nResult = save.WriteAll( this, dmap );
//	return nResult;
//}

//-----------------------------------------------------------------------------
// Purpose: Restores the current object from disk, by iterating through the objects
//			data description hierarchy
// Input  : &restore - restore buffer which the class data is read from
// Output : int	- 0 if the restore failed, 1 on success
//-----------------------------------------------------------------------------
int C_BaseEntity::Restore( IRestore &restore )
{
	// loops through the data description list, restoring each data desc block in order
	int status = restore.ReadEntity(this);

	// NOTE: Do *not* use GetAbsOrigin() here because it will
	// try to recompute m_rgflCoordinateFrame!
	//MatrixSetColumn(GetEngineObject()->m_vecAbsOrigin, 3, GetEngineObject()->m_rgflCoordinateFrame);
	GetEngineObject()->ResetRgflCoordinateFrame();

	// Restablish ground entity
	if (GetEngineObject()->GetGroundEntity() != NULL )
	{
		GetEngineObject()->GetGroundEntity()->AddEntityToGroundList(this->GetEngineObject());
	}

	return status;
}

//-----------------------------------------------------------------------------
// Purpose: Recursively restores all the classes in an object, in reverse order (top down)
// Output : int 0 on failure, 1 on success
//-----------------------------------------------------------------------------
//int C_BaseEntity::RestoreDataDescBlock( IRestore &restore, datamap_t *dmap )
//{
//	return restore.ReadAll( this, dmap );
//}

//-----------------------------------------------------------------------------
// capabilities
//-----------------------------------------------------------------------------
int C_BaseEntity::ObjectCaps( void ) 
{
	return 0; 
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : C_AI_BaseNPC
//-----------------------------------------------------------------------------
C_AI_BaseNPC *C_BaseEntity::MyNPCPointer( void )
{
	if ( IsNPC() ) 
	{
		return assert_cast<C_AI_BaseNPC *>(this);
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : recording - 
// Output : inline void
//-----------------------------------------------------------------------------
void C_BaseEntity::EnableInToolView( bool bEnable )
{
#ifndef NO_TOOLFRAMEWORK
	m_bEnabledInToolView = bEnable;
	UpdateVisibility();
#endif
}

void C_BaseEntity::SetToolRecording( bool recording )
{
#ifndef NO_TOOLFRAMEWORK
	m_bToolRecording = recording;
	if ( m_bToolRecording )
	{
		recordinglist->AddToList( GetClientHandle() );
	}
	else
	{
        recordinglist->RemoveFromList( GetClientHandle() );
	}
#endif
}

bool C_BaseEntity::HasRecordedThisFrame() const
{
#ifndef NO_TOOLFRAMEWORK
	Assert( m_nLastRecordedFrame <= gpGlobals->framecount );
	return m_nLastRecordedFrame == gpGlobals->framecount;
#else
	return false;
#endif
}

void C_BaseEntity::GetToolRecordingState( KeyValues *msg )
{
	Assert( ToolsEnabled() );
	if ( !ToolsEnabled() )
		return;

	VPROF_BUDGET( "C_BaseEntity::GetToolRecordingState", VPROF_BUDGETGROUP_TOOLS );

	C_BaseEntity *pOwner = m_hOwnerEntity;

	static BaseEntityRecordingState_t state;
	state.m_flTime = gpGlobals->curtime;
	state.m_pModelName = modelinfo->GetModelName( GetModel() );
	state.m_nOwner = pOwner ? pOwner->entindex() : -1;
	state.m_nEffects = m_fEffects;
	state.m_bVisible = ShouldDraw() && !IsDormant();
	state.m_bRecordFinalVisibleSample = false;
	state.m_vecRenderOrigin = GetRenderOrigin();
	state.m_vecRenderAngles = GetRenderAngles();

	// use EF_NOINTERP if the owner or a hierarchical parent has NO_INTERP
	if ( pOwner && pOwner->IsNoInterpolationFrame() )
	{
		state.m_nEffects |= EF_NOINTERP;
	}
	IEngineObjectClient *pParent = GetEngineObject()->GetMoveParent();
	while ( pParent )
	{
		if ( pParent->GetOuter()->IsNoInterpolationFrame() )
		{
			state.m_nEffects |= EF_NOINTERP;
			break;
		}

		pParent = pParent->GetMoveParent();
	}

	msg->SetPtr( "baseentity", &state );
}

void C_BaseEntity::CleanupToolRecordingState( KeyValues *msg )
{
}

void C_BaseEntity::RecordToolMessage()
{
	Assert( IsToolRecording() );
	if ( !IsToolRecording() )
		return;

	if ( HasRecordedThisFrame() )
		return;

	KeyValues *msg = new KeyValues( "entity_state" );

	// Post a message back to all IToolSystems
	GetToolRecordingState( msg );
	Assert( (int)GetToolHandle() != 0 );
	ToolFramework_PostToolMessage( GetToolHandle(), msg );
	CleanupToolRecordingState( msg );

	msg->deleteThis();

	m_nLastRecordedFrame = gpGlobals->framecount;
}

// (static function)
void C_BaseEntity::ToolRecordEntities()
{
	VPROF_BUDGET( "C_BaseEntity::ToolRecordEnties", VPROF_BUDGETGROUP_TOOLS );

	if ( !ToolsEnabled() || !clienttools->IsInRecordingMode() )
		return;

	// Let non-dormant client created predictables get added, too
	int c = recordinglist->Count();
	for ( int i = 0 ; i < c ; i++ )
	{
		IClientRenderable *pRenderable = recordinglist->Get( i );
		if ( !pRenderable )
			continue;

		pRenderable->RecordToolMessage();
	}
}


void C_BaseEntity::AddToInterpolationList()
{
	if ( m_InterpolationListEntry == 0xFFFF )
		m_InterpolationListEntry = g_InterpolationList.AddToTail( this );
}


void C_BaseEntity::RemoveFromInterpolationList()
{
	if ( m_InterpolationListEntry != 0xFFFF )
	{
		g_InterpolationList.Remove( m_InterpolationListEntry );
		m_InterpolationListEntry = 0xFFFF;
	}
}

				
void C_BaseEntity::AddToTeleportList()
{
	if ( m_TeleportListEntry == 0xFFFF )
		m_TeleportListEntry = g_TeleportList.AddToTail( this );
}


void C_BaseEntity::RemoveFromTeleportList()
{
	if ( m_TeleportListEntry != 0xFFFF )
	{
		g_TeleportList.Remove( m_TeleportListEntry );
		m_TeleportListEntry = 0xFFFF;
	}
}

#ifdef TF_CLIENT_DLL
bool C_BaseEntity::ValidateEntityAttachedToPlayer( bool &bShouldRetry )
{
	bShouldRetry = false;
	C_BaseEntity *pParent = GetRootMoveParent();
	if ( pParent == this )
		return true;

	// Some wearables parent to the view model
	C_BasePlayer *pPlayer = ToBasePlayer( pParent );
	if ( pPlayer && pPlayer->GetViewModel() == this )
	{
		return true;
	}

	// always allow the briefcase model
	const char *pszModel = modelinfo->GetModelName( GetModel() );
	if ( pszModel && pszModel[0] )
	{
		if ( FStrEq( pszModel, "models/flag/briefcase.mdl" ) )
			return true;

		if ( FStrEq( pszModel, "models/props_doomsday/australium_container.mdl" ) )
			return true;

		// Temp for MVM testing
		if ( FStrEq( pszModel, "models/buildables/sapper_placement_sentry1.mdl" ) )
			return true;

		if ( FStrEq( pszModel, "models/props_td/atom_bomb.mdl" ) )
			return true;

		if ( FStrEq( pszModel, "models/props_lakeside_event/bomb_temp_hat.mdl" ) )
			return true;
	}

	// Any entity that's not an item parented to a player is invalid.
	// This prevents them creating some other entity to pretend to be a cosmetic item.
	return !pParent->IsPlayer();
}
#endif // TF_CLIENT_DLL




void C_BaseEntity::CheckCLInterpChanged()
{
	float flCurValue_Interp = GetClientInterpAmount();
	static float flLastValue_Interp = flCurValue_Interp;

	float flCurValue_InterpNPCs = cl_interp_npcs.GetFloat();
	static float flLastValue_InterpNPCs = flCurValue_InterpNPCs;
	
	if ( flLastValue_Interp != flCurValue_Interp || 
		 flLastValue_InterpNPCs != flCurValue_InterpNPCs  )
	{
		flLastValue_Interp = flCurValue_Interp;
		flLastValue_InterpNPCs = flCurValue_InterpNPCs;
	
		// Tell all the existing entities to update their interpolation amounts to account for the change.
		C_BaseEntityIterator iterator;
		C_BaseEntity *pEnt;
		while ( (pEnt = iterator.Next()) != NULL )
		{
			pEnt->GetEngineObject()->Interp_UpdateInterpolationAmounts( pEnt->GetEngineObject()->GetVarMapping() );
		}
	}
}

void C_BaseEntity::DontRecordInTools()
{
#ifndef NO_TOOLFRAMEWORK
	m_bRecordInTools = false;
#endif
}

int C_BaseEntity::GetCreationTick() const
{
	return m_nCreationTick;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *ent - 
//-----------------------------------------------------------------------------
bool C_BaseEntity::HasNPCsOnIt(void)
{
	clientgroundlink_t* link;
	clientgroundlink_t* root = (clientgroundlink_t*)GetEngineObject()->GetDataObject(GROUNDLINK);
	if (root)
	{
		for (link = root->nextLink; link != root; link = link->nextLink)
		{
			if (ClientEntityList().GetClientEntityFromHandle(link->entity) && ((C_BaseEntity*)ClientEntityList().GetClientEntityFromHandle(link->entity))->MyNPCPointer())
				return true;
		}
	}

	return false;
}

//------------------------------------------------------------------------------
void CC_CL_Find_Ent( const CCommand& args )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Format: cl_find_ent <substring>\n" );
		return;
	}

	int iCount = 0;
	const char *pszSubString = args[1];
	Msg("Searching for client entities with classname containing substring: '%s'\n", pszSubString );

	C_BaseEntity *ent = NULL;
	while ( (ent = ClientEntityList().NextBaseEntity(ent)) != NULL )
	{
		const char *pszClassname = ent->GetClassname();

		bool bMatches = false;
		if ( pszClassname && pszClassname[0] )
		{
			if ( Q_stristr( pszClassname, pszSubString ) )
			{
				bMatches = true;
			}
		}

		if ( bMatches )
		{
			iCount++;
			Msg("   '%s' (entindex %d) %s \n", pszClassname ? pszClassname : "[NO NAME]", ent->entindex(), ent->IsDormant() ? "(DORMANT)" : "" );
		}
	}

	Msg("Found %d matches.\n", iCount);
}
static ConCommand cl_find_ent("cl_find_ent", CC_CL_Find_Ent, "Find and list all client entities with classnames that contain the specified substring.\nFormat: cl_find_ent <substring>\n", FCVAR_CHEAT);

//------------------------------------------------------------------------------
void CC_CL_Find_Ent_Index( const CCommand& args )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Format: cl_find_ent_index <index>\n" );
		return;
	}

	int iIndex = atoi(args[1]);
	C_BaseEntity *ent = ClientEntityList().GetBaseEntity( iIndex );
	if ( ent )
	{
		const char *pszClassname = ent->GetClassname();
		Msg("   '%s' (entindex %d) %s \n", pszClassname ? pszClassname : "[NO NAME]", iIndex, ent->IsDormant() ? "(DORMANT)" : "" );
	}
	else
	{
		Msg("Found no entity at %d.\n", iIndex);
	}
}
static ConCommand cl_find_ent_index("cl_find_ent_index", CC_CL_Find_Ent_Index, "Display data for clientside entity matching specified index.\nFormat: cl_find_ent_index <index>\n", FCVAR_CHEAT);
