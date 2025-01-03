//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================
#include "cbase.h"
#include "const.h"
#include "toolframework/itoolentity.h"
#include "toolframework/itoolsystem.h"
#include "KeyValues.h"
#include "icliententity.h"
#include "iserverentity.h"
#include "sceneentity.h"
#include "particles/particles.h"


//-----------------------------------------------------------------------------
// Interface from engine to tools for manipulating entities
//-----------------------------------------------------------------------------
class CServerTools : public IServerTools
{
public:
	// Inherited from IServerTools
	virtual IServerEntity *GetIServerEntity( IClientEntity *pClientEntity );
	virtual bool GetPlayerPosition( Vector &org, QAngle &ang, IClientEntity *pClientPlayer = NULL );
	virtual bool SnapPlayerToPosition( const Vector &org, const QAngle &ang, IClientEntity *pClientPlayer = NULL );
	virtual int GetPlayerFOV( IClientEntity *pClientPlayer = NULL );
	virtual bool SetPlayerFOV( int fov, IClientEntity *pClientPlayer = NULL );
	virtual bool IsInNoClipMode( IClientEntity *pClientPlayer = NULL );
	virtual IServerEntity *FirstEntity( void );
	virtual IServerEntity *NextEntity( IServerEntity *pEntity );
	virtual IServerEntity *FindEntityByHammerID( int iHammerID );
	virtual bool GetKeyValue( IServerEntity *pEntity, const char *szField, char *szValue, int iMaxLen );
	virtual bool SetKeyValue( IServerEntity *pEntity, const char *szField, const char *szValue );
	virtual bool SetKeyValue( IServerEntity *pEntity, const char *szField, float flValue );
	virtual bool SetKeyValue( IServerEntity *pEntity, const char *szField, const Vector &vecValue );
	virtual IServerEntity *CreateEntityByName( const char *szClassName );
	virtual void DispatchSpawn( IServerEntity *pEntity );
	virtual void ReloadParticleDefintions( const char *pFileName, const void *pBufData, int nLen );
	virtual void AddOriginToPVS( const Vector &org );
	virtual void MoveEngineViewTo( const Vector &vPos, const QAngle &vAngles );
	virtual bool DestroyEntityByHammerId( int iHammerID );
	virtual IServerEntity *GetBaseEntityByEntIndex( int iEntIndex );
	virtual void RemoveEntity( IServerEntity *pEntity );
	virtual void RemoveEntityImmediate( IServerEntity *pEntity );
	//virtual IEntityFactoryDictionary *GetEntityFactoryDictionary( void );
	virtual void SetMoveType( IServerEntity *pEntity, int val );
	virtual void SetMoveType( IServerEntity *pEntity, int val, int moveCollide );
	virtual void ResetSequence( CBaseAnimating *pEntity, int nSequence );
	virtual void ResetSequenceInfo( CBaseAnimating *pEntity );
	virtual void ClearMultiDamage( void );
	virtual void ApplyMultiDamage( void );
	virtual void AddMultiDamage( const CTakeDamageInfo &pTakeDamageInfo, IServerEntity *pEntity );
	virtual void RadiusDamage( const CTakeDamageInfo &info, const Vector &vecSrc, float flRadius, int iClassIgnore, IServerEntity *pEntityIgnore );

	virtual ITempEntsSystem *GetTempEntsSystem( void );
	virtual CBaseTempEntity *GetTempEntList( void );
	virtual IServerEntityList *GetEntityList( void );
	virtual bool IsEntityPtr( void *pTest );
	virtual IServerEntity *FindEntityByClassname( IServerEntity *pStartEntity, const char *szName );
	virtual IServerEntity *FindEntityByName( IServerEntity *pStartEntity, const char *szName, IServerEntity *pSearchingEntity = NULL, IServerEntity *pActivator = NULL, IServerEntity *pCaller = NULL, IEntityFindFilter *pFilter = NULL );
	virtual IServerEntity *FindEntityInSphere( IServerEntity *pStartEntity, const Vector &vecCenter, float flRadius );
	virtual IServerEntity *FindEntityByTarget( IServerEntity *pStartEntity, const char *szName );
	virtual IServerEntity *FindEntityByModel( IServerEntity *pStartEntity, const char *szModelName );
	virtual IServerEntity *FindEntityByNameNearest( const char *szName, const Vector &vecSrc, float flRadius, IServerEntity *pSearchingEntity = NULL, IServerEntity *pActivator = NULL, IServerEntity *pCaller = NULL );
	virtual IServerEntity *FindEntityByNameWithin( IServerEntity *pStartEntity, const char *szName, const Vector &vecSrc, float flRadius, IServerEntity *pSearchingEntity = NULL, IServerEntity *pActivator = NULL, IServerEntity *pCaller = NULL );
	virtual IServerEntity *FindEntityByClassnameNearest( const char *szName, const Vector &vecSrc, float flRadius );
	virtual IServerEntity *FindEntityByClassnameWithin( IServerEntity *pStartEntity, const char *szName, const Vector &vecSrc, float flRadius );
	virtual IServerEntity *FindEntityByClassnameWithin( IServerEntity *pStartEntity, const char *szName, const Vector &vecMins, const Vector &vecMaxs );
	virtual IServerEntity *FindEntityGeneric( IServerEntity *pStartEntity, const char *szName, IServerEntity *pSearchingEntity = NULL, IServerEntity *pActivator = NULL, IServerEntity *pCaller = NULL );
	virtual IServerEntity *FindEntityGenericWithin( IServerEntity *pStartEntity, const char *szName, const Vector &vecSrc, float flRadius, IServerEntity *pSearchingEntity = NULL, IServerEntity *pActivator = NULL, IServerEntity *pCaller = NULL );
	virtual IServerEntity *FindEntityGenericNearest( const char *szName, const Vector &vecSrc, float flRadius, IServerEntity *pSearchingEntity = NULL, IServerEntity *pActivator = NULL, IServerEntity *pCaller = NULL );
	virtual IServerEntity *FindEntityNearestFacing( const Vector &origin, const Vector &facing, float threshold );
	virtual IServerEntity *FindEntityClassNearestFacing( const Vector &origin, const Vector &facing, float threshold, char *classname );
	virtual IServerEntity *FindEntityProcedural( const char *szName, IServerEntity *pSearchingEntity = NULL, IServerEntity *pActivator = NULL, IServerEntity *pCaller = NULL );
};


//-----------------------------------------------------------------------------
// Singleton
//-----------------------------------------------------------------------------
static CServerTools g_ServerTools;

// VSERVERTOOLS_INTERFACE_VERSION_1 is compatible with the latest since we're only adding things to the end, so expose that as well.
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CServerTools, IServerTools001, VSERVERTOOLS_INTERFACE_VERSION_1, g_ServerTools );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CServerTools, IServerTools002, VSERVERTOOLS_INTERFACE_VERSION_2, g_ServerTools );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CServerTools, IServerTools, VSERVERTOOLS_INTERFACE_VERSION, g_ServerTools );

// When bumping the version to this interface, check that our assumption is still valid and expose the older version in the same way
COMPILE_TIME_ASSERT( VSERVERTOOLS_INTERFACE_VERSION_INT == 3 );


IServerEntity *CServerTools::GetIServerEntity( IClientEntity *pClientEntity )
{
	if ( pClientEntity == NULL )
		return NULL;

	CBaseHandle ehandle = pClientEntity->GetRefEHandle();
	if ( ehandle.GetEntryIndex() >= MAX_EDICTS )
		return NULL; // the first MAX_EDICTS entities are networked, the rest are client or server only

#if 0
	// this fails, since the server entities have extra bits in their serial numbers,
	// since 20 bits are reserved for serial numbers, except for networked entities, which are restricted to 10

	// Brian believes that everything should just restrict itself to 10 to make things simpler,
	// so if/when he changes NUM_SERIAL_NUM_BITS to 10, we can switch back to this simpler code

	IServerNetworkable *pNet = EntityList()->GetServerNetworkable( ehandle );
	if ( pNet == NULL )
		return NULL;

	IServerEntity *pServerEnt = pNet->GetBaseEntity();
	return pServerEnt;
#else
	IHandleEntity *pEnt = EntityList()->LookupEntityByNetworkIndex( ehandle.GetEntryIndex() );
	if ( pEnt == NULL )
		return NULL;

	CBaseHandle h = EntityList()->GetNetworkableHandle( ehandle.GetEntryIndex() );
	const int mask = ( 1 << NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS ) - 1;
	if ( !h.IsValid() || ( ( h.GetSerialNumber() & mask ) != ( ehandle.GetSerialNumber() & mask ) ) )
		return NULL;

	return (IServerEntity*)pEnt;
#endif
}

bool CServerTools::GetPlayerPosition( Vector &org, QAngle &ang, IClientEntity *pClientPlayer )
{
	IServerEntity *pServerPlayer = GetIServerEntity( pClientPlayer );
	CBasePlayer *pPlayer = pServerPlayer ? ( CBasePlayer* )pServerPlayer : ToBasePlayer(EntityList()->GetLocalPlayer());
	if ( pPlayer == NULL )
		return false;

	org = pPlayer->EyePosition();
	ang = pPlayer->EyeAngles();
	return true;
}

bool CServerTools::SnapPlayerToPosition( const Vector &org, const QAngle &ang, IClientEntity *pClientPlayer )
{
	IServerEntity *pServerPlayer = GetIServerEntity( pClientPlayer );
	CBasePlayer *pPlayer = pServerPlayer ? ( CBasePlayer* )pServerPlayer : ToBasePlayer(EntityList()->GetLocalPlayer());
	if ( pPlayer == NULL )
		return false;

	pPlayer->GetEngineObject()->SetAbsOrigin( org - pPlayer->GetViewOffset() );
	pPlayer->SnapEyeAngles( ang );

	// Disengage from hierarchy
	pPlayer->GetEngineObject()->SetParent( NULL );

	return true;
}

int CServerTools::GetPlayerFOV( IClientEntity *pClientPlayer )
{
	IServerEntity *pServerPlayer = GetIServerEntity( pClientPlayer );
	CBasePlayer *pPlayer = pServerPlayer ? ( CBasePlayer* )pServerPlayer : ToBasePlayer(EntityList()->GetLocalPlayer());
	if ( pPlayer == NULL )
		return 0;

	return pPlayer->GetFOV();
}

bool CServerTools::SetPlayerFOV( int fov, IClientEntity *pClientPlayer )
{
	IServerEntity *pServerPlayer = GetIServerEntity( pClientPlayer );
	CBasePlayer *pPlayer = pServerPlayer ? ( CBasePlayer* )pServerPlayer : ToBasePlayer(EntityList()->GetLocalPlayer());
	if ( pPlayer == NULL )
		return false;

	pPlayer->SetDefaultFOV( fov );
	IServerEntity *pFOVOwner = pPlayer->GetFOVOwner();
	return pPlayer->SetFOV( pFOVOwner ? (CBaseEntity*)pFOVOwner : pPlayer, fov );
}

bool CServerTools::IsInNoClipMode( IClientEntity *pClientPlayer )
{
	IServerEntity *pServerPlayer = GetIServerEntity( pClientPlayer );
	CBasePlayer *pPlayer = pServerPlayer ? ( CBasePlayer* )pServerPlayer : ToBasePlayer(EntityList()->GetLocalPlayer());
	if ( pPlayer == NULL )
		return true;

	return pPlayer->GetEngineObject()->GetMoveType() == MOVETYPE_NOCLIP;
}

IServerEntity *CServerTools::FirstEntity( void )
{
	return EntityList()->FirstEnt();
}

IServerEntity *CServerTools::NextEntity( IServerEntity *pEntity )
{
	IServerEntity *pEnt;

	if ( pEntity == NULL )
	{
		pEnt = EntityList()->FirstEnt();
	}
	else
	{
		pEnt = EntityList()->NextEnt( (IServerEntity *)pEntity );
	}
	return pEnt;
}

IServerEntity *CServerTools::FindEntityByHammerID( int iHammerID )
{
	IServerEntity *pEntity = EntityList()->FirstEnt();

	while (pEntity)
	{
		if (((CBaseEntity*)pEntity)->m_iHammerID == iHammerID)
			return pEntity;
		pEntity = EntityList()->NextEnt( pEntity );
	}
	return NULL;
}

bool CServerTools::GetKeyValue( IServerEntity *pEntity, const char *szField, char *szValue, int iMaxLen )
{
	return pEntity->GetKeyValue( szField, szValue, iMaxLen );
}

bool CServerTools::SetKeyValue( IServerEntity *pEntity, const char *szField, const char *szValue )
{
	return pEntity->KeyValue( szField, szValue );
}

bool CServerTools::SetKeyValue( IServerEntity *pEntity, const char *szField, float flValue )
{
	return pEntity->KeyValue( szField, flValue );
}

bool CServerTools::SetKeyValue( IServerEntity *pEntity, const char *szField, const Vector &vecValue )
{
	return pEntity->KeyValue( szField, vecValue );
}


//-----------------------------------------------------------------------------
// entity spawning
//-----------------------------------------------------------------------------
IServerEntity *CServerTools::CreateEntityByName( const char *szClassName )
{
	return EntityList()->CreateEntityByName( szClassName );
}

void CServerTools::DispatchSpawn( IServerEntity *pEntity )
{
	::DispatchSpawn( pEntity );
}


//-----------------------------------------------------------------------------
// Reload particle definitions
//-----------------------------------------------------------------------------
void CServerTools::ReloadParticleDefintions( const char *pFileName, const void *pBufData, int nLen )
{
	// FIXME: Use file name to determine if we care about this data
	CUtlBuffer buf( pBufData, nLen, CUtlBuffer::READ_ONLY );
	g_pParticleSystemMgr->ReadParticleConfigFile( buf, true );
}

void CServerTools::AddOriginToPVS( const Vector &org )
{
	engine->AddOriginToPVS( org );
}

void CServerTools::MoveEngineViewTo( const Vector &vPos, const QAngle &vAngles )
{
	CBasePlayer *pPlayer = UTIL_GetListenServerHost();
	if ( !pPlayer )
		return;

	extern void EnableNoClip( CBasePlayer *pPlayer );
	EnableNoClip( pPlayer );

	Vector zOffset = pPlayer->EyePosition() - pPlayer->GetEngineObject()->GetAbsOrigin();

	pPlayer->GetEngineObject()->SetAbsOrigin( vPos - zOffset );
	pPlayer->SnapEyeAngles( vAngles );
}

bool CServerTools::DestroyEntityByHammerId( int iHammerID )
{
	IServerEntity *pEntity = FindEntityByHammerID( iHammerID );
	if ( !pEntity )
		return false;

	EntityList()->DestroyEntity( pEntity );
	return true;
}

void CServerTools::RemoveEntity( IServerEntity *pEntity )
{
	EntityList()->DestroyEntity( pEntity );
}

void CServerTools::RemoveEntityImmediate( IServerEntity *pEntity )
{
	EntityList()->DestroyEntityImmediate( pEntity );
}

IServerEntity *CServerTools::GetBaseEntityByEntIndex( int iEntIndex )
{
	return EntityList()->GetBaseEntity(iEntIndex);
}

//IEntityFactoryDictionary *CServerTools::GetEntityFactoryDictionary( void )
//{
//	return ::EntityFactoryDictionary();
//}


void CServerTools::SetMoveType( IServerEntity *pEntity, int val )
{
	pEntity->GetEngineObject()->SetMoveType( (MoveType_t)val );
}

void CServerTools::SetMoveType( IServerEntity *pEntity, int val, int moveCollide )
{
	pEntity->GetEngineObject()->SetMoveType( (MoveType_t)val, (MoveCollide_t)moveCollide );
}

void CServerTools::ResetSequence( CBaseAnimating *pEntity, int nSequence )
{
	pEntity->GetEngineObject()->ResetSequence( nSequence );
}

void CServerTools::ResetSequenceInfo( CBaseAnimating *pEntity )
{
	pEntity->GetEngineObject()->ResetSequenceInfo();
}


void CServerTools::ClearMultiDamage( void )
{
	::ClearMultiDamage();
}

void CServerTools::ApplyMultiDamage( void )
{
	::ApplyMultiDamage();
}

void CServerTools::AddMultiDamage( const CTakeDamageInfo &pTakeDamageInfo, IServerEntity *pEntity )
{
	::AddMultiDamage( pTakeDamageInfo, pEntity );
}

void CServerTools::RadiusDamage( const CTakeDamageInfo &info, const Vector &vecSrc, float flRadius, int iClassIgnore, IServerEntity *pEntityIgnore )
{
	::RadiusDamage( info, vecSrc, flRadius, iClassIgnore, (CBaseEntity*)pEntityIgnore );
}


ITempEntsSystem *CServerTools::GetTempEntsSystem( void )
{
	return (ITempEntsSystem *)te;
}

CBaseTempEntity *CServerTools::GetTempEntList( void )
{
	return CBaseTempEntity::GetList();
}

IServerEntityList *CServerTools::GetEntityList( void )
{
	return EntityList();
}

bool CServerTools::IsEntityPtr( void *pTest )
{
	return EntityList()->IsEntityPtr( pTest );
}

IServerEntity *CServerTools::FindEntityByClassname( IServerEntity *pStartEntity, const char *szName )
{
	return EntityList()->FindEntityByClassname( pStartEntity, szName );
}

IServerEntity *CServerTools::FindEntityByName( IServerEntity *pStartEntity, const char *szName, IServerEntity *pSearchingEntity, IServerEntity *pActivator, IServerEntity *pCaller, IEntityFindFilter *pFilter )
{
	return EntityList()->FindEntityByName( pStartEntity, szName, pSearchingEntity, pActivator, pCaller, pFilter );
}

IServerEntity *CServerTools::FindEntityInSphere( IServerEntity *pStartEntity, const Vector &vecCenter, float flRadius )
{
	return EntityList()->FindEntityInSphere( pStartEntity, vecCenter, flRadius );
}

IServerEntity *CServerTools::FindEntityByTarget( IServerEntity *pStartEntity, const char *szName )
{
	return EntityList()->FindEntityByTarget( pStartEntity, szName );
}

IServerEntity *CServerTools::FindEntityByModel( IServerEntity *pStartEntity, const char *szModelName )
{
	return EntityList()->FindEntityByModel( pStartEntity, szModelName );
}

IServerEntity *CServerTools::FindEntityByNameNearest( const char *szName, const Vector &vecSrc, float flRadius, IServerEntity *pSearchingEntity, IServerEntity *pActivator, IServerEntity *pCaller )
{
	return EntityList()->FindEntityByNameNearest( szName, vecSrc, flRadius, pSearchingEntity, pActivator, pCaller );
}

IServerEntity *CServerTools::FindEntityByNameWithin( IServerEntity *pStartEntity, const char *szName, const Vector &vecSrc, float flRadius, IServerEntity *pSearchingEntity, IServerEntity *pActivator, IServerEntity *pCaller )
{
	return EntityList()->FindEntityByNameWithin( pStartEntity, szName, vecSrc, flRadius, pSearchingEntity, pActivator, pCaller );
}

IServerEntity *CServerTools::FindEntityByClassnameNearest( const char *szName, const Vector &vecSrc, float flRadius )
{
	return EntityList()->FindEntityByClassnameNearest( szName, vecSrc, flRadius );
}

IServerEntity *CServerTools::FindEntityByClassnameWithin( IServerEntity *pStartEntity, const char *szName, const Vector &vecSrc, float flRadius )
{
	return EntityList()->FindEntityByClassnameWithin( pStartEntity, szName, vecSrc, flRadius );
}

IServerEntity *CServerTools::FindEntityByClassnameWithin( IServerEntity *pStartEntity, const char *szName, const Vector &vecMins, const Vector &vecMaxs )
{
	return EntityList()->FindEntityByClassnameWithin( pStartEntity, szName, vecMins, vecMaxs );
}

IServerEntity *CServerTools::FindEntityGeneric( IServerEntity *pStartEntity, const char *szName, IServerEntity *pSearchingEntity, IServerEntity *pActivator, IServerEntity *pCaller )
{
	return EntityList()->FindEntityGeneric( pStartEntity, szName, pSearchingEntity, pActivator, pCaller );
}

IServerEntity *CServerTools::FindEntityGenericWithin( IServerEntity *pStartEntity, const char *szName, const Vector &vecSrc, float flRadius, IServerEntity *pSearchingEntity, IServerEntity *pActivator, IServerEntity *pCaller )
{
	return EntityList()->FindEntityGenericWithin( pStartEntity, szName, vecSrc, flRadius, pSearchingEntity, pActivator, pCaller );
}

IServerEntity *CServerTools::FindEntityGenericNearest( const char *szName, const Vector &vecSrc, float flRadius, IServerEntity *pSearchingEntity, IServerEntity *pActivator, IServerEntity *pCaller )
{
	return EntityList()->FindEntityGenericNearest( szName, vecSrc, flRadius, pSearchingEntity, pActivator, pCaller );
}

IServerEntity *CServerTools::FindEntityNearestFacing( const Vector &origin, const Vector &facing, float threshold )
{
	return EntityList()->FindEntityNearestFacing( origin, facing, threshold );
}

IServerEntity *CServerTools::FindEntityClassNearestFacing( const Vector &origin, const Vector &facing, float threshold, char *classname )
{
	return EntityList()->FindEntityClassNearestFacing( origin, facing, threshold, classname );
}

IServerEntity *CServerTools::FindEntityProcedural( const char *szName, IServerEntity *pSearchingEntity, IServerEntity *pActivator, IServerEntity *pCaller )
{
	return EntityList()->FindEntityProcedural( szName, pSearchingEntity, pActivator, pCaller );
}


// Interface from engine to tools for manipulating entities
class CServerChoreoTools : public IServerChoreoTools
{
public:
	// Iterates through ALL entities (separate list for client vs. server)
	virtual EntitySearchResult	NextChoreoEntity( EntitySearchResult currentEnt )
	{
		IServerEntity *ent = reinterpret_cast<IServerEntity* >( currentEnt );
		ent = EntityList()->FindEntityByClassname( ent, "logic_choreographed_scene" );
		return reinterpret_cast< EntitySearchResult >( ent );
	}

	virtual const char			*GetSceneFile( EntitySearchResult sr )
	{
		IServerEntity *ent = reinterpret_cast< IServerEntity* >( sr );
		if ( !sr )
			return "";

		if ( Q_stricmp( ent->GetClassname(), "logic_choreographed_scene" ) )
			return "";

		return GetSceneFilename( ent );
	}

	// For interactive editing
	virtual int	GetEntIndex( EntitySearchResult sr )
	{
		IServerEntity *ent = reinterpret_cast< IServerEntity* >( sr );
		if ( !ent )
			return -1;

		return ent->entindex();
	}

	virtual void ReloadSceneFromDisk( int entindex )
	{
		IServerEntity *ent = EntityList()->GetBaseEntity( entindex );
		if ( !ent )
			return;

		::ReloadSceneFromDisk( ent );
	}
};


static CServerChoreoTools g_ServerChoreoTools;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CServerChoreoTools, IServerChoreoTools, VSERVERCHOREOTOOLS_INTERFACE_VERSION, g_ServerChoreoTools );


//------------------------------------------------------------------------------
// Applies keyvalues to the entity by hammer ID.
//------------------------------------------------------------------------------
void CC_Ent_Keyvalue( const CCommand &args )
{
	// Must have an odd number of arguments.
	if ( ( args.ArgC() < 4 ) || ( args.ArgC() & 1 ) )
	{
		Msg( "Format: ent_keyvalue <entity id> \"key1\" \"value1\" \"key2\" \"value2\" ... \"keyN\" \"valueN\"\n" );
		return;
	}

	CBasePlayer *pPlayer = ToBasePlayer( UTIL_GetCommandClient() );
	IServerEntity *pEnt;
	if ( FStrEq( args[1], "" ) || FStrEq( args[1], "!picker" ) )
	{
		if (!pPlayer)
			return;

		pEnt = EntityList()->FindPickerEntity( pPlayer );

		if ( !pEnt )
		{
			ClientPrint( pPlayer, HUD_PRINTCONSOLE, "No entity in front of player.\n" );
			return;
		}
	}
	else if ( FStrEq( args[1], "!self" ) || FStrEq( args[1], "!caller" ) || FStrEq( args[1], "!activator" ) )
	{
		if (!pPlayer)
			return;

		pEnt = pPlayer;
	}
	else
	{
		int nID = atoi( args[1] );

		pEnt = g_ServerTools.FindEntityByHammerID( nID );
		if ( !pEnt )
		{
			Msg( "Entity ID %d not found.\n", nID );
			return;
		}
	}

	int nArg = 2;
	while ( nArg < args.ArgC() )
	{
		const char *pszKey = args[ nArg ];
		const char *pszValue = args[ nArg + 1 ];
		nArg += 2;

		g_ServerTools.SetKeyValue( pEnt, pszKey, pszValue );
	}
} 

static ConCommand ent_keyvalue("ent_keyvalue", CC_Ent_Keyvalue, "Applies the comma delimited key=value pairs to the entity with the given Hammer ID.\n\tFormat: ent_keyvalue <entity id> <key1> <value1> <key2> <value2> ... <keyN> <valueN>\n", FCVAR_CHEAT);
