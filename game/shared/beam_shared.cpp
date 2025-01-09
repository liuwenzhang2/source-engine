//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements visual effects entities: sprites, beams, bubbles, etc.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "beam_shared.h"
#include "decals.h"
#include "model_types.h"
#include "IEffects.h"
#include "util_shared.h"

#if !defined( CLIENT_DLL )
#include "ndebugoverlay.h"
#include "sendproxy.h"
#else
#include "iviewrender_beams.h"
#include "c_pixel_visibility.h"
#include "iclientmode.h"
#include "viewrender.h"
#include "beamdraw.h"
#ifdef PORTAL
	#include "c_prop_portal.h"
#endif //ifdef PORTAL

#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define BEAM_DEFAULT_HALO_SCALE		10

#if !defined( CLIENT_DLL )
// Lightning target, just alias landmark

class CInfoTarget : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoTarget, CPointEntity );

	void	Spawn( void );
};

//info targets are like point entities except you can force them to spawn on the client
void CInfoTarget::Spawn( void )
{
	BaseClass::Spawn();

	if (GetEngineObject()->HasSpawnFlags(SF_WEAPON_START_CONSTRAINED) )//0x01
	{
		GetEngineObject()->SetEFlags( EFL_FORCE_CHECK_TRANSMIT );
	}
}

LINK_ENTITY_TO_CLASS( info_target, CInfoTarget );
#endif


//-----------------------------------------------------------------------------
// Purpose: Returns true if the given entity is a fixed target for lightning.
//-----------------------------------------------------------------------------
bool IsStaticPointEntity( CBaseEntity *pEnt )
{
	if ( pEnt->GetEngineObject()->GetMoveParent() )
		return false;

	if ( !pEnt->GetEngineObject()->GetModelIndex() )
		return 1;

	if ( FClassnameIs( pEnt, "info_target" ) || FClassnameIs( pEnt, "info_landmark" ) || 
		FClassnameIs( pEnt, "path_corner" ) )
		return true;

	return false;
}

#if defined( CLIENT_DLL )
extern bool ComputeBeamEntPosition( IClientEntity *pEnt, int nAttachment, bool bInterpretAttachmentIndexAsHitboxIndex, Vector& pt );

void RecvProxy_Beam_ScrollSpeed( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	C_Beam *beam;
	float	val;

	// Unpack the data.
	val	= pData->m_Value.m_Float;
	val *= 0.1;

	beam = ( C_Beam * )pStruct;
	Assert( pOut == &beam->m_fSpeed );

	beam->m_fSpeed = val;
}
#else
//#if !defined( NO_ENTITY_PREDICTION )
//static void* SendProxy_SendPredictableId( const SendProp *pProp, const void *pStruct, const void *pVarData, CSendProxyRecipients *pRecipients, int objectID )
//{
//	CBaseEntity *pEntity = (CBaseEntity *)pStruct;
//	if ( !pEntity || !pEntity->m_PredictableID->IsActive() )
//		return NULL;
//
//	if ( !pEntity->GetOwnerEntity() )
//		return NULL;
//
//	CBaseEntity *owner = pEntity->GetOwnerEntity();
//	if ( !owner || !owner->IsPlayer() )
//		return NULL;
//
//	CBasePlayer *pOwner = static_cast< CBasePlayer * >( owner );
//	if ( !pOwner )
//		return NULL;
//
//	int id_player_index = pEntity->m_PredictableID->GetPlayer();
//	int owner_player_index = pOwner->entindex() - 1;
//	// Only send to owner player
//	// FIXME:  Is this ever not the case due to the SetOnly call?
//	if ( id_player_index != owner_player_index )
//		return NULL;
//
//	pRecipients->SetOnly( owner_player_index );
//	return ( void * )pVarData;
//}
//REGISTER_SEND_PROXY_NON_MODIFIED_POINTER( SendProxy_SendPredictableId );
//#endif

//extern void SendProxy_MoveParentToInt(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID);
#endif

LINK_ENTITY_TO_CLASS( beam, CBeam );

// This table encodes the CBeam data.
IMPLEMENT_NETWORKCLASS_ALIASED( Beam, DT_Beam )

//#if !defined( NO_ENTITY_PREDICTION )
//BEGIN_NETWORK_TABLE_NOBASE( CBeam, DT_BeamPredictableId )
//#if !defined( CLIENT_DLL )
//	SendPropPredictableId( SENDINFO( m_PredictableID ) ),
//	SendPropInt( SENDINFO( m_bIsPlayerSimulated ), 1, SPROP_UNSIGNED ),
//#else
//	RecvPropPredictableId( RECVINFO( m_PredictableID ) ),
//	RecvPropInt( RECVINFO( m_bIsPlayerSimulated ) ),
//#endif
//END_NETWORK_TABLE()
//#endif

BEGIN_NETWORK_TABLE_NOBASE( CBeam, DT_Beam )
#if !defined( CLIENT_DLL )
	SendPropInt		(SENDINFO(m_nBeamType),		Q_log2(NUM_BEAM_TYPES)+1,	SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_nBeamFlags),	NUM_BEAM_FLAGS,	SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_nNumBeamEnts ),			5,	SPROP_UNSIGNED ),
	SendPropArray3
	(
		SENDINFO_ARRAY3(m_hAttachEntity), 
		SendPropEHandle( SENDINFO_ARRAY(m_hAttachEntity) )
	),
	SendPropArray3
	(
		SENDINFO_ARRAY3(m_nAttachIndex), 
		SendPropInt( SENDINFO_ARRAY(m_nAttachIndex), ATTACHMENT_INDEX_BITS, SPROP_UNSIGNED)
	),
	SendPropInt		(SENDINFO(m_nHaloIndex),	16, SPROP_UNSIGNED ),
	SendPropFloat	(SENDINFO(m_fHaloScale),	0,	SPROP_NOSCALE ),
	SendPropFloat	(SENDINFO(m_fWidth),		10,	SPROP_ROUNDUP,	0.0f, MAX_BEAM_WIDTH ),
	SendPropFloat	(SENDINFO(m_fEndWidth),		10,	SPROP_ROUNDUP,	0.0f, MAX_BEAM_WIDTH ),
	SendPropFloat	(SENDINFO(m_fFadeLength),	0,	SPROP_NOSCALE ),
	SendPropFloat	(SENDINFO(m_fAmplitude),	8,	SPROP_ROUNDDOWN,	0.0f, MAX_BEAM_NOISEAMPLITUDE ),
	SendPropFloat	(SENDINFO(m_fStartFrame),	8,	SPROP_ROUNDDOWN,	0.0f,   256.0f),
	SendPropFloat	(SENDINFO(m_fSpeed),		8,	SPROP_NOSCALE,	0.0f,	MAX_BEAM_SCROLLSPEED),
	//SendPropInt		(SENDINFO(m_nRenderFX),		8,	SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_nRenderMode),	8,	SPROP_UNSIGNED ),
	SendPropFloat	(SENDINFO(m_flFrameRate),	10, SPROP_ROUNDUP, -25.0f, 25.0f ),
	SendPropFloat	(SENDINFO(m_flHDRColorScale),	0, SPROP_NOSCALE, 0.0f, 100.0f ),
	SendPropFloat	(SENDINFO(m_flFrame),		20, SPROP_ROUNDDOWN | SPROP_CHANGES_OFTEN,	0.0f,   256.0f),
	SendPropInt		(SENDINFO(m_clrRender),		32,	SPROP_UNSIGNED | SPROP_CHANGES_OFTEN ),
	SendPropVector	(SENDINFO(m_vecEndPos),		-1,	SPROP_COORD ),
#ifdef PORTAL
	SendPropBool	(SENDINFO(m_bDrawInMainRender) ),
	SendPropBool	(SENDINFO(m_bDrawInPortalRender) ),
#endif
	//SendPropModelIndex(SENDINFO(m_nModelIndex) ),
	//SendPropVector (SENDINFO_ORIGIN(m_vecOrigin), 19, SPROP_CHANGES_OFTEN,	MIN_COORD_INTEGER, MAX_COORD_INTEGER, SendProxy_Origin),
	//SendPropEHandle(SENDINFO_MOVEPARENT(moveparent), 0, SendProxy_MoveParentToInt),
	SendPropInt		(SENDINFO(m_nMinDXLevel),	8,	SPROP_UNSIGNED ),
//#if !defined( NO_ENTITY_PREDICTION )
//	SendPropDataTable( "beampredictable_id", 0, &REFERENCE_SEND_TABLE( DT_BeamPredictableId ), SendProxy_SendPredictableId ),
//#endif

#else
	RecvPropInt		(RECVINFO(m_nBeamType)),
	RecvPropInt		(RECVINFO(m_nBeamFlags)),
	RecvPropInt		(RECVINFO(m_nNumBeamEnts)),
	RecvPropArray3
	(
		RECVINFO_ARRAY( m_hAttachEntity ),
		RecvPropEHandle (RECVINFO(m_hAttachEntity[0]))
	),
	RecvPropArray3	
	(
		RECVINFO_ARRAY( m_nAttachIndex ),
		RecvPropInt (RECVINFO(m_nAttachIndex[0]))
	),
	RecvPropInt		(RECVINFO(m_nHaloIndex)),
	RecvPropFloat	(RECVINFO(m_fHaloScale)),
	RecvPropFloat	(RECVINFO(m_fWidth)),
	RecvPropFloat	(RECVINFO(m_fEndWidth)),
	RecvPropFloat	(RECVINFO(m_fFadeLength)),
	RecvPropFloat	(RECVINFO(m_fAmplitude)),
	RecvPropFloat	(RECVINFO(m_fStartFrame)),
	RecvPropFloat	(RECVINFO(m_fSpeed), 0, RecvProxy_Beam_ScrollSpeed ),
	RecvPropFloat(RECVINFO(m_flFrameRate)),
	RecvPropFloat(RECVINFO(m_flHDRColorScale)),
	RecvPropInt(RECVINFO(m_clrRender)),
	//RecvPropInt(RECVINFO(m_nRenderFX)),
	RecvPropInt(RECVINFO(m_nRenderMode)),
	RecvPropFloat(RECVINFO(m_flFrame)),
	RecvPropVector(RECVINFO(m_vecEndPos)),
#ifdef PORTAL
	RecvPropBool(RECVINFO(m_bDrawInMainRender) ),
	RecvPropBool(RECVINFO(m_bDrawInPortalRender) ),
#endif
	//RecvPropInt(RECVINFO(m_nModelIndex)),
	RecvPropInt(RECVINFO(m_nMinDXLevel)),

	//RecvPropVector(RECVINFO_NAME(m_vecNetworkOrigin, m_vecOrigin)),
	//RecvPropInt( RECVINFO_NAME(m_hNetworkMoveParent, moveparent), 0, RecvProxy_IntToMoveParent ),
//#if !defined( NO_ENTITY_PREDICTION )
//	RecvPropDataTable( "beampredictable_id", 0, 0, &REFERENCE_RECV_TABLE( DT_BeamPredictableId ) ),
//#endif

#endif
END_NETWORK_TABLE()

#if !defined( CLIENT_DLL )
BEGIN_DATADESC( CBeam )
	DEFINE_FIELD( m_nHaloIndex, FIELD_MODELINDEX ),
	DEFINE_FIELD( m_nBeamType, FIELD_INTEGER ),
	DEFINE_FIELD( m_nBeamFlags, FIELD_INTEGER ),
	DEFINE_FIELD( m_nNumBeamEnts, FIELD_INTEGER ),
	DEFINE_ARRAY( m_hAttachEntity, FIELD_EHANDLE, MAX_BEAM_ENTS ),
	DEFINE_ARRAY( m_nAttachIndex, FIELD_INTEGER, MAX_BEAM_ENTS ),
	DEFINE_FIELD( m_nMinDXLevel, FIELD_INTEGER ),

	DEFINE_FIELD( m_fWidth, FIELD_FLOAT ),
	DEFINE_FIELD( m_fEndWidth, FIELD_FLOAT ),
	DEFINE_FIELD( m_fFadeLength, FIELD_FLOAT ),
	DEFINE_FIELD( m_fHaloScale, FIELD_FLOAT ),
	DEFINE_FIELD( m_fAmplitude, FIELD_FLOAT ),
	DEFINE_FIELD( m_fStartFrame, FIELD_FLOAT ),
	DEFINE_FIELD( m_fSpeed, FIELD_FLOAT ),

	DEFINE_FIELD( m_flFrameRate, FIELD_FLOAT ),
	DEFINE_FIELD( m_flFrame, FIELD_FLOAT ),

	DEFINE_KEYFIELD( m_flHDRColorScale, FIELD_FLOAT, "HDRColorScale" ),

	DEFINE_KEYFIELD( m_flDamage, FIELD_FLOAT, "damage" ),
	DEFINE_FIELD( m_flFireTime, FIELD_TIME ),

	DEFINE_FIELD( m_vecEndPos, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_hEndEntity, FIELD_EHANDLE ),

	DEFINE_KEYFIELD( m_nDissolveType, FIELD_INTEGER, "dissolvetype" ),

#ifdef PORTAL
	DEFINE_FIELD( m_bDrawInMainRender, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bDrawInPortalRender, FIELD_BOOLEAN ),
#endif

	// Inputs
	DEFINE_INPUTFUNC( FIELD_FLOAT, "Width", InputWidth ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "Noise", InputNoise ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "ColorRedValue", InputColorRedValue ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "ColorGreenValue", InputColorGreenValue ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "ColorBlueValue", InputColorBlueValue ),
	DEFINE_INPUT( m_fSpeed, FIELD_FLOAT, "ScrollSpeed" ),

	// don't save this
	//DEFINE_FIELD( m_queryHandleHalo, FIELD_ ),

END_DATADESC()

#else

BEGIN_PREDICTION_DATA( CBeam )

	DEFINE_PRED_FIELD( m_nBeamType, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	
	DEFINE_PRED_FIELD( m_nNumBeamEnts, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_ARRAY( m_hAttachEntity, FIELD_EHANDLE, MAX_BEAM_ENTS, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_ARRAY( m_nAttachIndex, FIELD_INTEGER, MAX_BEAM_ENTS, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_nHaloIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fHaloScale, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fWidth, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fEndWidth, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fFadeLength, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fAmplitude, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fStartFrame, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fSpeed, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	//DEFINE_PRED_FIELD( m_nRenderFX, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_nRenderMode, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flFrameRate, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flFrame, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_clrRender, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_nMinDXLevel, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD_TOL( m_vecEndPos, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.125f ),
#ifdef PORTAL
	DEFINE_PRED_FIELD( m_bDrawInMainRender, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_bDrawInPortalRender, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
#endif
	//DEFINE_PRED_FIELD( m_nModelIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	//DEFINE_PRED_FIELD_TOL( m_vecOrigin, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.125f ),
	
	//DEFINE_PRED_FIELD( m_pMoveParent, SendProxy_MoveParent ),
	//DEFINE_PRED_FIELD( m_flHDRColorScale, SendProxy_HDRColorScale ),

END_PREDICTION_DATA()

#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBeam::CBeam( void )
{
#ifdef _DEBUG
	// necessary since in debug, we initialize vectors to NAN for debugging
	m_vecEndPos.Init();
#endif

	m_nMinDXLevel = 0;
	m_flHDRColorScale = 1.0f; // default value.

#if !defined( CLIENT_DLL )
	m_nDissolveType = -1;
#else
	m_queryHandleHalo = 0;
#endif

#ifdef PORTAL
	m_bDrawInMainRender = true;
	m_bDrawInPortalRender = true;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *szModelName - 
//-----------------------------------------------------------------------------
void CBeam::SetModel( const char *szModelName )
{
	int modelIndex = modelinfo->GetModelIndex( szModelName );
	const model_t *model = modelinfo->GetModel( modelIndex );
	if ( model && modelinfo->GetModelType( model ) != mod_sprite )
	{
		Msg( "Setting CBeam to non-sprite model %s\n", szModelName );
	}
#if !defined( CLIENT_DLL )
	UTIL_SetModel( this, szModelName );
#else
	BaseClass::SetModel( szModelName );
#endif
}


void CBeam::Spawn( void )
{
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	GetEngineObject()->SetSolid( SOLID_NONE );							// Remove model & collisions
	SetRenderMode( kRenderTransTexture );

	// Opt out of all shadow routines
	GetEngineObject()->AddEffects( EF_NOSHADOW | EF_NORECEIVESHADOW );

	Precache( );
}


void CBeam::Precache( void )
{
	if ( GetOwnerEntity() )
	{
		SetStartEntity( GetOwnerEntity() );
	}
	
	if ( m_hEndEntity.Get() )
	{
		SetEndEntity( m_hEndEntity );
	}
}


void CBeam::SetType( int type )		
{ 
	Assert( type < NUM_BEAM_TYPES );
	m_nBeamType = type;
}

void CBeam::SetBeamFlags( int flags )	
{ 
	Assert( flags < (1 << NUM_BEAM_FLAGS) );
	m_nBeamFlags = flags;
}

void CBeam::SetBeamFlag( int flag )		
{ 
	m_nBeamFlags |= flag;
}

int CBeam::GetType( void ) const 
{ 
	return m_nBeamType;
}

int CBeam::GetBeamFlags( void ) const
{ 
	return m_nBeamFlags;
}

void CBeam::SetStartEntity( IHandleEntity *pEntity )
{ 
	Assert( m_nNumBeamEnts >= 2 );
	m_hAttachEntity.Set( 0, pEntity );
	SetOwnerEntity( (CBaseEntity*)pEntity );
	RelinkBeam();
	pEntity->GetEngineObject()->AddEFlags( EFL_FORCE_CHECK_TRANSMIT );
}

void CBeam::SetEndEntity( IHandleEntity *pEntity ) 
{ 
	Assert( m_nNumBeamEnts >= 2 );
	m_hAttachEntity.Set( m_nNumBeamEnts-1, pEntity );
	m_hEndEntity = (CBaseEntity*)pEntity;
	RelinkBeam();
	pEntity->GetEngineObject()->AddEFlags( EFL_FORCE_CHECK_TRANSMIT );
}


//-----------------------------------------------------------------------------
// This will change things so the abs position matches the requested spot
//-----------------------------------------------------------------------------
void CBeam::SetAbsStartPos( const Vector &pos )
{
	if (!GetEngineObject()->GetMoveParent())
	{
		SetStartPos( pos );
		return;
	}

	Vector vecLocalPos;
	matrix3x4_t worldToBeam;
	MatrixInvert(GetEngineObject()->EntityToWorldTransform(), worldToBeam );
	VectorTransform( pos, worldToBeam, vecLocalPos );
	SetStartPos( vecLocalPos );
}

void CBeam::SetAbsEndPos( const Vector &pos )
{
	if (!GetEngineObject()->GetMoveParent())
	{
		SetEndPos( pos );
		return;
	}

	Vector vecLocalPos;
	matrix3x4_t worldToBeam;
	MatrixInvert(GetEngineObject()->EntityToWorldTransform(), worldToBeam );
	VectorTransform( pos, worldToBeam, vecLocalPos );
	SetEndPos( vecLocalPos );
}

#if !defined( CLIENT_DLL )

// These don't take attachments into account
const Vector &CBeam::GetAbsStartPos( void ) const
{
	if ( GetType() == BEAM_ENTS && GetStartEntity() )
	{
		IHandleEntity* ent = EntityList()->GetBaseEntity( GetStartEntity() );
		if ( !ent )
		{
			return GetEngineObject()->GetAbsOrigin();
		}
		return ent->GetEngineObject()->GetAbsOrigin();
	}
	return GetEngineObject()->GetAbsOrigin();
}


const Vector &CBeam::GetAbsEndPos( void ) const
{
	if ( GetType() != BEAM_POINTS && GetType() != BEAM_HOSE && GetEndEntity() ) 
	{
		IHandleEntity *ent = EntityList()->GetBaseEntity( GetEndEntity() );
		if ( ent )
			return ent->GetEngineObject()->GetAbsOrigin();
	}

	if (!const_cast<CBeam*>(this)->GetEngineObject()->GetMoveParent())
		return m_vecEndPos.Get();

	// FIXME: Cache this off?
	static Vector vecAbsPos;
	VectorTransform( m_vecEndPos, GetEngineObject()->EntityToWorldTransform(), vecAbsPos );
	return vecAbsPos;
}

#else

//-----------------------------------------------------------------------------
// Unlike the server, these take attachments into account
//-----------------------------------------------------------------------------
const Vector &C_Beam::GetAbsStartPos( void ) const
{
	static Vector vecStartAbsPosition;
	if ( GetType() != BEAM_POINTS && GetType() != BEAM_HOSE ) 
	{
		if (ComputeBeamEntPosition( (IClientEntity*)m_hAttachEntity[0].Get(), m_nAttachIndex[0], false, vecStartAbsPosition))
			return vecStartAbsPosition;
	}

	return GetEngineObject()->GetAbsOrigin();
}


const Vector &C_Beam::GetAbsEndPos( void ) const
{
	static Vector vecEndAbsPosition;
	if ( GetType() != BEAM_POINTS && GetType() != BEAM_HOSE ) 
	{
		if (ComputeBeamEntPosition( (IClientEntity*)m_hAttachEntity[m_nNumBeamEnts-1].Get(), m_nAttachIndex[m_nNumBeamEnts - 1], false, vecEndAbsPosition))
			return vecEndAbsPosition;
	}

	if (!const_cast<C_Beam*>(this)->GetEngineObject()->GetMoveParent())
		return m_vecEndPos.Get();

	// FIXME: Cache this off?
	VectorTransform( m_vecEndPos, GetEngineObject()->EntityToWorldTransform(), vecEndAbsPosition );
	return vecEndAbsPosition;
}
#endif

CBeam *CBeam::BeamCreate( const char *pSpriteName, float width )
{
	// Create a new entity with CBeam private data
#ifdef GAME_DLL
	CBeam* pBeam = (CBeam*)EntityList()->CreateEntityByName("beam");
	pBeam->BeamInit(pSpriteName, width);
	return pBeam;
#endif // GAME_DLL
#ifdef CLIENT_DLL
	CBeam* pBeam = (CBeam*)EntityList()->CreateEntityByName("beam");
	pBeam->BeamInit(pSpriteName, width);
	return pBeam;
#endif // CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSpriteName - 
//			&origin - 
//			animate - 
// Output : CSprite
//-----------------------------------------------------------------------------
//CBeam *CBeam::BeamCreatePredictable( const char *module, int line, bool persist, const char *pSpriteName, float width, CBasePlayer *pOwner )
//{
//#if !defined( NO_ENTITY_PREDICTION )
//	CBeam *pBeam = ( CBeam * )CBaseEntity::CreatePredictedEntityByName( "beam", module, line, persist );
//	if ( pBeam )
//	{
//		pBeam->BeamInit( pSpriteName, width );
//		pBeam->SetOwnerEntity( pOwner );
//		pBeam->SetPlayerSimulated( pOwner );
//	}
//
//	return pBeam;
//#else
//	return NULL;
//#endif
//}

void CBeam::BeamInit( const char *pSpriteName, float width )
{
	SetColor( 255, 255, 255 );
	SetBrightness( 255 );
	SetNoise( 0 );
	SetFrame( 0 );
	SetScrollRate( 0 );
	GetEngineObject()->SetModelName( MAKE_STRING( pSpriteName ) );
	SetRenderMode( kRenderTransTexture );
	SetTexture(engine->PrecacheModel( pSpriteName ) );
	SetWidth( width );
	SetEndWidth( width );
	SetFadeLength( 0 );			// No fade
	for (int i=0;i<MAX_BEAM_ENTS;i++)
	{
		m_hAttachEntity.Set( i, NULL );
		m_nAttachIndex.Set( i, 0 );
	}
	m_nHaloIndex	= 0;
	m_fHaloScale	= BEAM_DEFAULT_HALO_SCALE;
	m_nBeamType		= 0;
	m_nBeamFlags    = 0;
}


void CBeam::PointsInit( const Vector &start, const Vector &end )
{
	SetType( BEAM_POINTS );
	m_nNumBeamEnts = 2;
	SetStartPos( start );
	SetEndPos( end );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}


void CBeam::HoseInit( const Vector &start, const Vector &direction )
{
	SetType( BEAM_HOSE );
	m_nNumBeamEnts = 2;
	SetStartPos( start );
	SetEndPos( direction );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}


void CBeam::PointEntInit( const Vector &start, CBaseEntity *pEndEntity )
{
	SetType( BEAM_ENTPOINT );
	m_nNumBeamEnts = 2;
	SetStartPos( start );
	SetEndEntity( pEndEntity );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}

void CBeam::EntsInit( CBaseEntity *pStartEntity, CBaseEntity *pEndEntity )
{
	SetType( BEAM_ENTS );
	m_nNumBeamEnts = 2;
	SetStartEntity( pStartEntity );
	SetEndEntity( pEndEntity );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}

void CBeam::LaserInit( CBaseEntity *pStartEntity, CBaseEntity *pEndEntity )
{
	SetType( BEAM_LASER );
	m_nNumBeamEnts = 2;
	SetStartEntity( pStartEntity );
	SetEndEntity( pEndEntity );
	SetStartAttachment( 0 );
	SetEndAttachment( 0 );
	RelinkBeam();
}

void CBeam::SplineInit( int nNumEnts, CBaseEntity** pEntList, int *attachment )
{
	if (nNumEnts < 2)
	{
		Msg("ERROR: Min of 2 ents required for spline beam.\n");
	}
	else if (nNumEnts > MAX_BEAM_ENTS)
	{
		Msg("ERROR: Max of %i ents allowed for spline beam.\n",MAX_BEAM_ENTS);
	}
	SetType( BEAM_SPLINE );

	for (int i=0;i<nNumEnts;i++)
	{
		m_hAttachEntity.Set( i, pEntList[i] ); 
		m_nAttachIndex.Set( i, attachment[i] );
	}
	m_nNumBeamEnts = nNumEnts;
	RelinkBeam();
}


void CBeam::RelinkBeam( void )
{
	// FIXME: Why doesn't this just define the absbox too?
	// It seems that we don't need to recompute the absbox
	// in CBaseEntity::SetObjectCollisionBox, in fact the absbox
	// computed there seems way too big
	Vector startPos = GetAbsStartPos(), endPos = GetAbsEndPos();

	Vector vecAbsExtra1, vecAbsExtra2;
	bool bUseExtraPoints = false;

#ifdef PORTAL
	IHandleEntity *pStartEntity = GetStartEntityPtr();
	
	CTraceFilterSkipClassname traceFilter( pStartEntity, "prop_energy_ball", COLLISION_GROUP_NONE );
	
	ITraceFilter *pEntityBeamTraceFilter = NULL;
	if ( pStartEntity )
		pEntityBeamTraceFilter = pStartEntity->GetBeamTraceFilter();

	CTraceFilterChain traceFilterChain( &traceFilter, pEntityBeamTraceFilter );

	bUseExtraPoints = UTIL_Portal_Trace_Beam( this, startPos, endPos, vecAbsExtra1, vecAbsExtra2, &traceFilterChain );
#endif

	// UNDONE: Should we do this to make the boxes smaller?
	//SetAbsOrigin( startPos );

	Vector vecBeamMin, vecBeamMax;
	VectorMin( startPos, endPos, vecBeamMin );
	VectorMax( startPos, endPos, vecBeamMax );

	if ( bUseExtraPoints )
	{
		VectorMin( vecBeamMin, vecAbsExtra1, vecBeamMin );
		VectorMin( vecBeamMin, vecAbsExtra2, vecBeamMin );
		VectorMax( vecBeamMax, vecAbsExtra1, vecBeamMax );
		VectorMax( vecBeamMax, vecAbsExtra2, vecBeamMax );
	}

	GetEngineObject()->SetCollisionBounds( vecBeamMin - GetEngineObject()->GetAbsOrigin(), vecBeamMax - GetEngineObject()->GetAbsOrigin() );
}


CBaseEntity *CBeam::RandomTargetname( const char *szName )
{
#if !defined( CLIENT_DLL )
	int total = 0;

	IServerEntity *pEntity = NULL;
	IServerEntity *pNewEntity = NULL;
	while ((pNewEntity = EntityList()->FindEntityByName( pNewEntity, szName )) != NULL)
	{
		total++;
		if (random->RandomInt(0,total-1) < 1)
			pEntity = pNewEntity;
	}
	return (CBaseEntity*)pEntity;
#else
	return NULL;
#endif
}


void CBeam::DoSparks( const Vector &start, const Vector &end )
{
#if !defined( CLIENT_DLL )
	if (GetEngineObject()->HasSpawnFlags(SF_BEAM_SPARKSTART|SF_BEAM_SPARKEND) )
	{
		if (GetEngineObject()->HasSpawnFlags( SF_BEAM_SPARKSTART ) )
		{
			g_pEffects->Sparks( start );
		}
		if (GetEngineObject()->HasSpawnFlags( SF_BEAM_SPARKEND ) )
		{
			g_pEffects->Sparks( end );
		}
	}
#endif
}


//-----------------------------------------------------------------------------
// Purpose: Damages anything in the beam.
// Input  : ptr - 
//-----------------------------------------------------------------------------
void CBeam::BeamDamage( trace_t *ptr )
{
	RelinkBeam();
#if !defined( CLIENT_DLL )
	if ( ptr->fraction != 1.0 && ptr->m_pEnt != NULL )
	{
		CBaseEntity *pHit = (CBaseEntity*)ptr->m_pEnt;
		if ( pHit )
		{
			ClearMultiDamage();
			Vector dir = ptr->endpos - GetEngineObject()->GetAbsOrigin();
			VectorNormalize( dir );
			int nDamageType = DMG_ENERGYBEAM;

#ifndef HL1_DLL
			if (m_nDissolveType == 0)
			{
				nDamageType = DMG_DISSOLVE;
			}
			else if ( m_nDissolveType > 0 )
			{
				nDamageType = DMG_DISSOLVE | DMG_SHOCK; 
			}
#endif

			CTakeDamageInfo info( this, this, m_flDamage * (gpGlobals->curtime - m_flFireTime), nDamageType );
			CalculateMeleeDamageForce( &info, dir, ptr->endpos );
			pHit->DispatchTraceAttack( info, dir, ptr );
			ApplyMultiDamage();
			if (GetEngineObject()->HasSpawnFlags( SF_BEAM_DECALS ) )
			{
				if ( pHit->IsBSPModel() )
				{
					UTIL_DecalTrace( ptr, GetDecalName() );
				}
			}
		}
	}
#endif
	m_flFireTime = gpGlobals->curtime;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBeam::TurnOn( void )
{
	GetEngineObject()->AddEffects( EF_NODRAW );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBeam::TurnOff( void )
{
	GetEngineObject()->RemoveEffects( EF_NODRAW );
}

#if !defined( CLIENT_DLL )
//-----------------------------------------------------------------------------
// Purpose: Input handler for the beam width. Sets the end width based on the
//			beam width.
// Input  : Beam width in tenths of world units.
//-----------------------------------------------------------------------------
void CBeam::InputWidth( inputdata_t &inputdata )
{
	SetWidth( inputdata.value.Float() );
	SetEndWidth( inputdata.value.Float() );
}

void CBeam::InputColorRedValue( inputdata_t &inputdata )
{
	int nNewColor = clamp( FastFloatToSmallInt(inputdata.value.Float()), 0, 255 );
	SetColor( nNewColor, m_clrRender->g, m_clrRender->b );
}

void CBeam::InputColorGreenValue( inputdata_t &inputdata )
{
	int nNewColor =clamp( FastFloatToSmallInt(inputdata.value.Float()), 0, 255 );
	SetColor( m_clrRender->r, nNewColor, m_clrRender->b );
}

void CBeam::InputColorBlueValue( inputdata_t &inputdata )
{
	int nNewColor = clamp( FastFloatToSmallInt(inputdata.value.Float()), 0, 255 );
	SetColor( m_clrRender->r, m_clrRender->g, nNewColor );
}

void CBeam::InputNoise( inputdata_t &inputdata )
{
	SetNoise( inputdata.value.Float() );
}

int CBeam::UpdateTransmitState( void )
{
	// we must call ShouldTransmit() if we have a move parent
	if (GetEngineObject()->GetMoveParent() )
		return SetTransmitState( FL_EDICT_FULLCHECK );

	return BaseClass::UpdateTransmitState( );
}

void CBeam::SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways )
{
	// Are we already marked for transmission?
	if ( pInfo->m_pTransmitEdict->Get( entindex() ) )
		return;

	BaseClass::SetTransmit( pInfo, bAlways );
	
	// Force our attached entities to go too...
	for ( int i=0; i < MAX_BEAM_ENTS; ++i )
	{
		if ( m_hAttachEntity[i].Get() )
		{
			((IServerEntity*)m_hAttachEntity[i].Get())->SetTransmit( pInfo, bAlways );
		}
	}
}

int CBeam::ShouldTransmit( const CCheckTransmitInfo *pInfo )
{
	if (GetEngineObject()->IsEffectActive( EF_NODRAW ) )
		return FL_EDICT_DONTSEND;

	// Transmit us with the same rules as our move parent
	if (GetEngineObject()->GetMoveParent() )
	{
		return GetEngineObject()->GetMoveParent()->GetOuter()->ShouldTransmit(pInfo);
	}

	return BaseClass::ShouldTransmit( pInfo );
}

#endif

//-----------------------------------------------------------------------------
// Purpose: Draw any debug text overlays.
// Output : Returns the new text offset from the top.
//-----------------------------------------------------------------------------
int CBeam::DrawDebugTextOverlays(void)
{
#if !defined( CLIENT_DLL )
	int text_offset = BaseClass::DrawDebugTextOverlays();
	if (m_debugOverlays & OVERLAY_TEXT_BIT) 
	{
		// Print state
		char tempstr[512];
		Q_snprintf(tempstr, sizeof(tempstr), "start: (%.2f,%.2f,%.2f)", GetEngineObject()->GetAbsOrigin().x, GetEngineObject()->GetAbsOrigin().y, GetEngineObject()->GetAbsOrigin().z);
		EntityText(text_offset,tempstr,0);
		text_offset++;

		Q_snprintf(tempstr, sizeof(tempstr), "end  : (%.2f,%.2f,%.2f)", m_vecEndPos.GetX(), m_vecEndPos.GetY(), m_vecEndPos.GetZ());
		EntityText(text_offset,tempstr,0);
		text_offset++;
	}
	
	return text_offset;
#else
	return 0;
#endif
}

#if defined( CLIENT_DLL )

// Purpose: 
// Input  : isbeingremoved - 
//			*predicted - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
//bool CBeam::OnPredictedEntityRemove( bool isbeingremoved, C_BaseEntity *predicted )
//{
//	BaseClass::OnPredictedEntityRemove( isbeingremoved, predicted );
//
//	CBeam *beam = dynamic_cast< CBeam * >( predicted );
//	if ( !beam )
//	{
//		// Hrm, we didn't link up to correct type!!!
//		Assert( 0 );
//		// Delete right away since it's fucked up
//		return true;
//	}
//
//	if ( beam->IsEFlagSet( EFL_KILLME ) )
//	{
//		// Don't delete right away
//		AddEFlags( EFL_KILLME );
//		return false;
//	}
//
//	// Go ahead and delete if it's not short-lived
//	return true;
//}

extern bool g_bRenderingScreenshot;
extern ConVar r_drawviewmodel;
#ifdef PORTAL
bool bBeamDrawingThroughPortal = false;
#endif
void CBeam::DrawBeam(C_Beam* pbeam, ITraceFilter* pEntityBeamTraceFilter)
{

	Beam_t beam;

	// Set up the beam.
	int beamType = pbeam->GetType();

	BeamInfo_t beamInfo;
	beamInfo.m_vecStart = pbeam->GetAbsStartPos();
	beamInfo.m_vecEnd = pbeam->GetAbsEndPos();
	beamInfo.m_pStartEnt = beamInfo.m_pEndEnt = NULL;
	beamInfo.m_nModelIndex = pbeam->GetEngineObject()->GetModelIndex();
	beamInfo.m_nHaloIndex = pbeam->m_nHaloIndex;
	beamInfo.m_flHaloScale = pbeam->m_fHaloScale;
	beamInfo.m_flLife = 0;
	beamInfo.m_flWidth = pbeam->GetWidth();
	beamInfo.m_flEndWidth = pbeam->GetEndWidth();
	beamInfo.m_flFadeLength = pbeam->GetFadeLength();
	beamInfo.m_flAmplitude = pbeam->GetNoise();
	beamInfo.m_flBrightness = pbeam->GetFxBlend();
	beamInfo.m_flSpeed = pbeam->GetScrollRate();

#ifdef PORTAL	// Beams need to recursively draw through portals
	// Trace to see if we've intersected a portal
	float fEndFraction;
	Ray_t rayBeam;

	bool bIsReversed = (pbeam->GetBeamFlags() & FBEAM_REVERSED) != 0x0;

	Vector vRayStartPoint, vRayEndPoint;

	vRayStartPoint = beamInfo.m_vecStart;
	vRayEndPoint = beamInfo.m_vecEnd;

	if (beamType == BEAM_ENTPOINT || beamType == BEAM_ENTS || beamType == BEAM_LASER)
	{
		ComputeBeamEntPosition((IClientEntity*)pbeam->m_hAttachEntity[0].Get(), pbeam->m_nAttachIndex[0], false, vRayStartPoint);
		ComputeBeamEntPosition((IClientEntity*)pbeam->m_hAttachEntity[1].Get(), pbeam->m_nAttachIndex[1], false, vRayEndPoint);
	}

	if (!bIsReversed)
		rayBeam.Init(vRayStartPoint, vRayEndPoint);
	else
		rayBeam.Init(vRayEndPoint, vRayStartPoint);

	IHandleEntity* pStartEntity = pbeam->GetStartEntityPtr();

	CTraceFilterSkipClassname traceFilter(pStartEntity, "prop_energy_ball", COLLISION_GROUP_NONE);

	if (!pEntityBeamTraceFilter && pStartEntity)
		pEntityBeamTraceFilter = pStartEntity->GetBeamTraceFilter();

	CTraceFilterChain traceFilterChain(&traceFilter, pEntityBeamTraceFilter);

	IEnginePortalClient* pPortal = (IEnginePortalClient*)UTIL_Portal_TraceRay_Beam(EntityList(), rayBeam, MASK_SHOT, &traceFilterChain, &fEndFraction);

	// Get the point that we hit a portal or wall
	Vector vEndPoint = rayBeam.m_Start + rayBeam.m_Delta * fEndFraction;

	if (pPortal)
	{
		// Prevent infinite recursion by lower the brightness each call
		int iOldBrightness = pbeam->GetBrightness();

		if (iOldBrightness > 16)
		{
			// Remember the old values of the beam before changing it for the next call
			Vector vOldStart = pbeam->GetAbsStartPos();
			Vector vOldEnd = pbeam->GetAbsEndPos();
			//float fOldWidth = pbeam->GetEndWidth();
			IHandleEntity* pOldStartEntity = pbeam->GetStartEntityPtr();
			IHandleEntity* pOldEndEntity = pbeam->GetEndEntityPtr();
			int iOldStartAttachment = pbeam->GetStartAttachment();
			int iOldEndAttachment = pbeam->GetEndAttachment();
			int iOldType = pbeam->GetType();

			// Get the transformed positions of the sub beam in the other portal's space
			Vector vTransformedStart, vTransformedEnd;
			VMatrix matThisToLinked = pPortal->MatrixThisToLinked();
			UTIL_Portal_PointTransform(matThisToLinked, vEndPoint, vTransformedStart);
			UTIL_Portal_PointTransform(matThisToLinked, rayBeam.m_Start + rayBeam.m_Delta, vTransformedEnd);

			// Set up the sub beam for the next call
			pbeam->SetBrightness(iOldBrightness - 16);
			if (bIsReversed)
				pbeam->PointsInit(vTransformedEnd, vTransformedStart);
			else
				pbeam->PointsInit(vTransformedStart, vTransformedEnd);
			if (bIsReversed)
				pbeam->SetEndWidth(pbeam->GetWidth());
			pbeam->SetStartEntity((C_BaseEntity*)pPortal->GetLinkedPortal()->AsEngineObject()->GetOuter());

			// Draw the sub beam
			bBeamDrawingThroughPortal = true;
			DrawBeam(pbeam, pEntityBeamTraceFilter);
			bBeamDrawingThroughPortal = true;

			// Restore the original values
			pbeam->SetBrightness(iOldBrightness);
			pbeam->SetStartPos(vOldStart);
			pbeam->SetEndPos(vOldEnd);
			//if ( bIsReversed )
			//	pbeam->SetEndWidth( fOldWidth );
			if (pOldStartEntity)
				pbeam->SetStartEntity(pOldStartEntity);
			if (pOldEndEntity)
				pbeam->SetEndEntity(pOldEndEntity);
			pbeam->SetStartAttachment(iOldStartAttachment);
			pbeam->SetEndAttachment(iOldEndAttachment);
			pbeam->SetType(iOldType);

			// Doesn't use a hallow or taper the beam because we recursed
			beamInfo.m_nHaloIndex = 0;
			if (!bIsReversed)
				beamInfo.m_flEndWidth = beamInfo.m_flWidth;
		}
	}

	// Clip to the traced end point (portal or wall)
	if (bBeamDrawingThroughPortal)
	{
		if (bIsReversed)
			beamInfo.m_vecStart = vEndPoint;
		else
			beamInfo.m_vecEnd = vEndPoint;
	}

	bBeamDrawingThroughPortal = false;
#endif

	beams->SetupBeam(&beam, beamInfo);

	beamInfo.m_nStartFrame = pbeam->m_fStartFrame;
	beamInfo.m_flFrameRate = pbeam->m_flFrameRate;
	beamInfo.m_flRed = pbeam->m_clrRender->r;
	beamInfo.m_flGreen = pbeam->m_clrRender->g;
	beamInfo.m_flBlue = pbeam->m_clrRender->b;

	beams->SetBeamAttributes(&beam, beamInfo);

	if (pbeam->m_nHaloIndex > 0)
	{
		// HACKHACK: heuristic to estimate proxy size.  Revisit this!
		float size = 1.0f + (pbeam->m_fHaloScale * pbeam->m_fWidth / pbeam->m_fEndWidth);
		size = clamp(size, 1.0f, 8.0f);
		beam.m_queryHandleHalo = &pbeam->m_queryHandleHalo;
		beam.m_haloProxySize = size;
	}
	else
	{
		beam.m_queryHandleHalo = NULL;
	}

	// Handle code from relinking.
	switch (beamType)
	{
	case BEAM_ENTS:
	{
		beam.type = TE_BEAMPOINTS;
		beam.flags = FBEAM_STARTENTITY | FBEAM_ENDENTITY;
		beam.entity[0] = pbeam->m_hAttachEntity[0];
		beam.attachmentIndex[0] = pbeam->m_nAttachIndex[0];
		beam.entity[1] = pbeam->m_hAttachEntity[1];
		beam.attachmentIndex[1] = pbeam->m_nAttachIndex[1];
		beam.numAttachments = pbeam->m_nNumBeamEnts;
		break;
	}
	case BEAM_LASER:
	{
		beam.type = TE_BEAMLASER;
		beam.flags = FBEAM_STARTENTITY | FBEAM_ENDENTITY;
		beam.entity[0] = pbeam->m_hAttachEntity[0];
		beam.attachmentIndex[0] = pbeam->m_nAttachIndex[0];
		beam.entity[1] = pbeam->m_hAttachEntity[1];
		beam.attachmentIndex[1] = pbeam->m_nAttachIndex[1];
		beam.numAttachments = pbeam->m_nNumBeamEnts;
		break;
	}
	case BEAM_SPLINE:
	{
		beam.type = TE_BEAMSPLINE;
		beam.flags = FBEAM_STARTENTITY | FBEAM_ENDENTITY;
		beam.numAttachments = pbeam->m_nNumBeamEnts;
		for (int i = 0; i < beam.numAttachments; i++)
		{
			beam.entity[i] = pbeam->m_hAttachEntity[i];
			beam.attachmentIndex[i] = pbeam->m_nAttachIndex[i];
		}
		break;
	}
	case BEAM_ENTPOINT:
	{
		beam.type = TE_BEAMPOINTS;
		beam.flags = 0;
		beam.entity[0] = pbeam->m_hAttachEntity[0];
		beam.attachmentIndex[0] = pbeam->m_nAttachIndex[0];
		beam.entity[1] = pbeam->m_hAttachEntity[1];
		beam.attachmentIndex[1] = pbeam->m_nAttachIndex[1];
		if (beam.entity[0].Get())
		{
			beam.flags |= FBEAM_STARTENTITY;
		}
		if (beam.entity[1].Get())
		{
			beam.flags |= FBEAM_ENDENTITY;
		}
		beam.numAttachments = pbeam->m_nNumBeamEnts;
		break;
	}
	case BEAM_POINTS:
		// Already set up
		break;
	}

	beam.flags |= pbeam->GetBeamFlags() & (FBEAM_SINENOISE | FBEAM_SOLID | FBEAM_SHADEIN | FBEAM_SHADEOUT | FBEAM_NOTILE);

	if (beam.entity[0])
	{
		// don't draw viewmodel effects in reflections
		if (CurrentViewID() == VIEW_REFLECTION)
		{
			int group = beam.entity[0]->GetRenderGroup();
			if (group == RENDER_GROUP_VIEW_MODEL_TRANSLUCENT || group == RENDER_GROUP_VIEW_MODEL_OPAQUE)
				return;
		}
	}

	beam.m_flHDRColorScale = pbeam->GetHDRColorScale();

	// Draw it
	beams->UpdateBeam(&beam, gpGlobals->frametime);
	beams->DrawBeam(&beam);
}

int CBeam::DrawModel( int flags )
{
	if ( !GetEngineObject()->IsReadyToDraw() )
		return 0;

	if (GetEngineObject()->IsMarkedForDeletion() )
		return 0;

	if ( CurrentViewID() == VIEW_SHADOW_DEPTH_TEXTURE )
		return 0;

#ifdef PORTAL
	if ( ( !g_pPortalRender->IsRenderingPortal() && !m_bDrawInMainRender ) || 
		( g_pPortalRender->IsRenderingPortal() && !m_bDrawInPortalRender ) )
	{
		return 0;
	}
#endif //#ifdef PORTAL

	// Tracker 16432:  If rendering a savegame screenshot don't draw beams 
	//   who have viewmodels as their attached entity
	if ( g_bRenderingScreenshot || !r_drawviewmodel.GetBool() )
	{
		// If the beam is attached
		for (int i=0;i<MAX_BEAM_ENTS;i++)
		{
			C_BaseViewModel *vm = dynamic_cast<C_BaseViewModel *>(m_hAttachEntity[i].Get());
			if ( vm )
			{
				return 0;
			}
		}
	}

	DrawBeam( this );//beams->
	return 0;
}

void CBeam::OnDataChanged( DataUpdateType_t updateType )
{
	GetEngineObject()->MarkMessageReceived();

	// Make sure that the correct model is referenced for this entity
	GetEngineObject()->SetModelPointer( modelinfo->GetModel(GetEngineObject()->GetModelIndex() ) );

	// Convert weapon world models to viewmodels if they're weapons being carried by the local player
	for (int i=0;i<MAX_BEAM_ENTS;i++)
	{
		IHandleEntity *pEnt = m_hAttachEntity[i].Get();
		if ( pEnt )
		{
			C_BaseCombatWeapon *pWpn = dynamic_cast<C_BaseCombatWeapon *>(pEnt);
			if ( pWpn && pWpn->ShouldDrawUsingViewModel() )
			{
				C_BasePlayer *player = ToBasePlayer( pWpn->GetOwner() );

				// Use GetRenderedWeaponModel() instead?
				C_BaseViewModel *pViewModel = player ? player->GetViewModel( 0 ) : NULL;
				if ( pViewModel )
				{
					// Get the viewmodel and use it instead
					m_hAttachEntity.Set( i, pViewModel );
				}
			}
		}
	}

	// Compute the bounds here...
	Vector mins, maxs;
	ComputeBounds( mins, maxs );
	GetEngineObject()->SetCollisionBounds( mins, maxs );
}

bool CBeam::IsTransparent( void )
{
	return true;
}

bool CBeam::ShouldDraw()
{
	if ( m_nMinDXLevel != 0 )
	{
		if ( m_nMinDXLevel > g_pMaterialSystemHardwareConfig->GetDXSupportLevel() )
			return false;
	}
	return BaseClass::ShouldDraw();
}

//-----------------------------------------------------------------------------
// Purpose: Adds to beam entity list
//-----------------------------------------------------------------------------
void CBeam::AddEntity( void )
{
	// If set to invisible, skip. Do this before resetting the entity pointer so it has 
	// valid data to decide whether it's visible.
	if ( !ShouldDraw() )
	{
		return;
	}

	//FIXME: If we're hooked up to an attachment point, then recompute our bounds every frame
	if ( m_hAttachEntity[0].Get() || m_hAttachEntity[1].Get() )
	{
		// Compute the bounds here...
		Vector mins, maxs;
		ComputeBounds( mins, maxs );
		GetEngineObject()->SetCollisionBounds( mins, maxs );
	}

	GetEngineObject()->MoveToLastReceivedPosition();
}

//-----------------------------------------------------------------------------
// Computes the bounding box of a beam local to the origin of the beam
//-----------------------------------------------------------------------------
void CBeam::ComputeBounds( Vector& mins, Vector& maxs )
{
	Vector vecAbsStart = GetAbsStartPos();
	Vector vecAbsEnd = GetAbsEndPos();

	// May need extra points for creating the min/max bounds
	bool bUseExtraPoints = false;
	Vector vecAbsExtra1, vecAbsExtra2;

#ifdef PORTAL
	IHandleEntity *pStartEntity = GetStartEntityPtr();

	CTraceFilterSkipClassname traceFilter( pStartEntity, "prop_energy_ball", COLLISION_GROUP_NONE );

	ITraceFilter *pEntityBeamTraceFilter = NULL;
	if ( pStartEntity )
		pEntityBeamTraceFilter = pStartEntity->GetBeamTraceFilter();

	CTraceFilterChain traceFilterChain( &traceFilter, pEntityBeamTraceFilter );

	bUseExtraPoints = UTIL_Portal_Trace_Beam( this, vecAbsStart, vecAbsEnd, vecAbsExtra1, vecAbsExtra2, &traceFilterChain );
#endif

	switch( GetType() )
	{
	case BEAM_LASER:
	case BEAM_ENTS:
	case BEAM_SPLINE:
	case BEAM_ENTPOINT:
		{
			// Compute the bounds here...
			Vector attachmentPoint( 0, 0, 0 );
			mins.Init( 99999, 99999, 99999 );
			maxs.Init( -99999, -99999, -99999 );
			for (int i = 0; i < m_nNumBeamEnts; ++i )
			{
				IHandleEntity *pTestEnt = m_hAttachEntity[i].Get();
				if ( pTestEnt )
				{
					if ( pTestEnt == this )
					{
						mins = maxs = GetEngineObject()->GetAbsOrigin();
					}
					else
					{
						// We do this so we don't have to calculate attachments (and do expensive bone-setup calculations) on our attachments.
						Vector attMins, attMaxs;
						((IClientEntity*)m_hAttachEntity[i].Get())->GetRenderBoundsWorldspace( attMins, attMaxs );

						mins = mins.Min( attMins );
						mins = mins.Min( attMaxs );
						
						maxs = maxs.Max( attMins );
						maxs = maxs.Max( attMaxs );
					}
					
					//ASSERT_COORD( mins );
					//ASSERT_COORD( maxs );
				}
				else
				{
					if (i == 0)
					{
						VectorCopy( vecAbsStart, attachmentPoint );
					}
					else if (i == 1)
					{
						VectorCopy( vecAbsEnd, attachmentPoint );
					}
					else
					{
						Assert(0);
					}

					mins = mins.Min( attachmentPoint );
					maxs = maxs.Max( attachmentPoint );
				}
			}
		}
		break;

	case BEAM_POINTS:
	default:
		{
			for (int i = 0; i < 3; ++i)
			{
				if (vecAbsStart[i] < vecAbsEnd[i])
				{
					mins[i] = vecAbsStart[i];
					maxs[i] = vecAbsEnd[i];
				}
				else
				{
					mins[i] = vecAbsEnd[i];
					maxs[i] = vecAbsStart[i];
				}
			}
		}
		break;
	}

	if ( bUseExtraPoints )
	{
		mins = mins.Min( vecAbsExtra1 );
		mins = mins.Min( vecAbsExtra2 );
		maxs = maxs.Max( vecAbsExtra1 );
		maxs = maxs.Max( vecAbsExtra2 );
	}

	// bloat the bounding box by the width of the beam
	float rad = 0.5f * MAX( m_fWidth.Get(), m_fEndWidth.Get() );
	Vector vecRad( rad, rad, rad );
	mins -= vecRad;
	maxs += vecRad;

	// Make sure the bounds are measured in *relative coords*
	Vector vecAbsOrigin = GetEngineObject()->GetAbsOrigin();
	mins -= vecAbsOrigin;
	maxs -= vecAbsOrigin;
}
#endif

IEnginePortal* UTIL_Portal_TraceRay_Beam(IEntityList* pEntityList, const Ray_t& ray, unsigned int fMask, ITraceFilter* pTraceFilter, float* pfFraction)
{
	// Do a regular trace
	trace_t tr;
	UTIL_TraceLine(ray.m_Start, ray.m_Start + ray.m_Delta, fMask, pTraceFilter, &tr);
	float fMustBeCloserThan = tr.fraction + 0.0001f;

	IEnginePortal* pIntersectedPortal = UTIL_Portal_FirstAlongRay(pEntityList, ray, fMustBeCloserThan);

	*pfFraction = fMustBeCloserThan; //will be real trace distance if it didn't hit a portal
	return pIntersectedPortal;
}


bool UTIL_Portal_Trace_Beam(const CBeam* pBeam, Vector& vecStart, Vector& vecEnd, Vector& vecIntersectionStart, Vector& vecIntersectionEnd, ITraceFilter* pTraceFilter)
{
	vecStart = pBeam->GetAbsStartPos();
	vecEnd = pBeam->GetAbsEndPos();

	// Trace to see if we've intersected a portal
	float fEndFraction;
	Ray_t rayBeam;

	bool bIsReversed = (pBeam->GetBeamFlags() & FBEAM_REVERSED) != 0x0;

	if (!bIsReversed)
		rayBeam.Init(vecStart, vecEnd);
	else
		rayBeam.Init(vecEnd, vecStart);

	IEnginePortal* pPortal = UTIL_Portal_TraceRay_Beam(pBeam->GetEntityList(), rayBeam, MASK_SHOT, pTraceFilter, &fEndFraction);

	// If we intersected a portal we need to modify the start and end points to match the actual trace through portal drawing extents
	if (!pPortal)
		return false;

	// Modify the start and end points to match the actual trace through portal drawing extents
	vecStart = rayBeam.m_Start;

	Vector vecIntersection = rayBeam.m_Start + rayBeam.m_Delta * fEndFraction;

	int iNumLoops = 0;

	// Loop through the portals (at most 16 times)
	while (pPortal && iNumLoops < 16)
	{
		// Get the point that we hit a portal or wall
		vecIntersectionStart = vecIntersection;

		VMatrix matThisToLinked = pPortal->MatrixThisToLinked();

		// Get the transformed positions of the sub beam in the other portal's space
		UTIL_Portal_PointTransform(matThisToLinked, vecIntersectionStart, vecIntersectionEnd);
		UTIL_Portal_PointTransform(matThisToLinked, rayBeam.m_Start + rayBeam.m_Delta, vecEnd);

		CTraceFilterSkipClassname traceFilter(pPortal->GetLinkedPortal()->AsEngineObject()->GetHandleEntity(), "prop_energy_ball", COLLISION_GROUP_NONE);

		rayBeam.Init(vecIntersectionEnd, vecEnd);
		pPortal = UTIL_Portal_TraceRay_Beam(pBeam->GetEntityList(), rayBeam, MASK_SHOT, &traceFilter, &fEndFraction);
		vecIntersection = rayBeam.m_Start + rayBeam.m_Delta * fEndFraction;

		++iNumLoops;
	}

	vecEnd = vecIntersection;

	return true;
}