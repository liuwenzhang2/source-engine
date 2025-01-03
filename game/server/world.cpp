//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Precaches and defs for entities and other data that must always be available.
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "ammodef.h"
#include "soundent.h"
#include "client.h"
#include "decals.h"
#include "EnvMessage.h"
#include "player.h"
#include "player_resource.h"
#include "gamerules.h"
#include "teamplay_gamerules.h"
//#include "physics.h"
#include "isaverestore.h"
#include "activitylist.h"
#include "eventlist.h"
#include "eventqueue.h"
#include "ai_network.h"
#include "ai_schedule.h"
#include "ai_networkmanager.h"
#include "ai_utils.h"
#include "tactical_mission.h"
#include "basetempentity.h"
#include "world.h"
#include "mempool.h"
#include "igamesystem.h"
#include "iachievementmgr.h"
#include "engine/IEngineSound.h"
#include "globals.h"
#include "engine/IStaticPropMgr.h"
#include "particle_parse.h"
#include "gamestats.h"
#include "voice_gamemgr.h"
#ifdef CSTRIKE_DLL
#include "cs_gamerules.h"
#endif // CSTRIKE_DLL
#ifdef DOD_DLL
#include "dod_gamerules.h"
#endif // DOD_DLL
#if defined(HL1_DLL) && !defined(HL1MP_DLL)
#include "hl1_gamerules.h"
#endif // HL1_DLL
#ifdef HL1MP_DLL
#include "hl1mp_gamerules.h"
#endif // HL1MP_DLL
#if defined(HL2_DLL) && !defined(HL2MP) && !defined(PORTAL)
#include "hl2_gamerules.h"
#endif
#ifdef HL2MP
#include "hl2mp_gamerules.h"
#endif
#ifdef PORTAL
#include "portal_gamerules.h"
#endif // PORTAL


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar g_Language("g_Language", "0", FCVAR_REPLICATED);
ConVar sk_autoaim_mode("sk_autoaim_mode", "1", FCVAR_ARCHIVE | FCVAR_REPLICATED);
ConVar log_verbose_enable("log_verbose_enable", "0", FCVAR_GAMEDLL, "Set to 1 to enable verbose server log on the server.");
ConVar log_verbose_interval("log_verbose_interval", "3.0", FCVAR_GAMEDLL, "Determines the interval (in seconds) for the verbose server log.");
ConVar	old_radius_damage("old_radiusdamage", "0.0", FCVAR_REPLICATED);

static CViewVectors g_DefaultViewVectors(
	Vector(0, 0, 64),			//VEC_VIEW (m_vView)

	Vector(-16, -16, 0),		//VEC_HULL_MIN (m_vHullMin)
	Vector(16, 16, 72),		//VEC_HULL_MAX (m_vHullMax)

	Vector(-16, -16, 0),		//VEC_DUCK_HULL_MIN (m_vDuckHullMin)
	Vector(16, 16, 36),		//VEC_DUCK_HULL_MAX	(m_vDuckHullMax)
	Vector(0, 0, 28),			//VEC_DUCK_VIEW		(m_vDuckView)

	Vector(-10, -10, -10),		//VEC_OBS_HULL_MIN	(m_vObsHullMin)
	Vector(10, 10, 10),		//VEC_OBS_HULL_MAX	(m_vObsHullMax)

	Vector(0, 0, 14)			//VEC_DEAD_VIEWHEIGHT (m_vDeadViewHeight)
);

extern IServerEntity				*g_pLastSpawn;
void InitBodyQue(void);
extern void W_Precache(void);
//extern void ActivityList_Free( void );
IServerGameRules* g_pGameRules = NULL;

#define SF_DECAL_NOTINDEATHMATCH		2048

class CDecal : public CPointEntity
{
public:
	DECLARE_CLASS( CDecal, CPointEntity );

	void	Spawn( void );
	bool	KeyValue( const char *szKeyName, const char *szValue );

	// Need to apply static decals here to get them into the signon buffer for the server appropriately
	virtual void Activate();

	void	TriggerDecal( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value );

	// Input handlers.
	void	InputActivate( inputdata_t &inputdata );

	DECLARE_DATADESC();

public:
	int		m_nTexture;
	bool	m_bLowPriority;

private:

	void	StaticDecal( void );
};

BEGIN_DATADESC( CDecal )

	DEFINE_FIELD( m_nTexture, FIELD_INTEGER ),
	DEFINE_KEYFIELD( m_bLowPriority, FIELD_BOOLEAN, "LowPriority" ), // Don't mark as FDECAL_PERMANENT so not save/restored and will be reused on the client preferentially

	// Function pointers
	DEFINE_FUNCTION( StaticDecal ),
	DEFINE_FUNCTION( TriggerDecal ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Activate", InputActivate ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( infodecal, CDecal );

// UNDONE:  These won't get sent to joining players in multi-player
void CDecal::Spawn( void )
{
	if ( m_nTexture < 0 || 
		(gpGlobals->deathmatch && GetEngineObject()->HasSpawnFlags( SF_DECAL_NOTINDEATHMATCH )) )
	{
		EntityList()->DestroyEntity( this );
		return;
	} 
}

void CDecal::Activate()
{
	BaseClass::Activate();

	if ( !GetEntityName() )
	{
		StaticDecal();
	}
	else
	{
		// if there IS a targetname, the decal sprays itself on when it is triggered.
		SetThink ( &CDecal::SUB_DoNothing );
		SetUse(&CDecal::TriggerDecal);
	}
}

void CDecal::TriggerDecal ( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value )
{
	// this is set up as a USE function for info_decals that have targetnames, so that the
	// decal doesn't get applied until it is fired. (usually by a scripted sequence)
	trace_t		trace;
	int			entityIndex;

	UTIL_TraceLine(GetEngineObject()->GetAbsOrigin() - Vector(5,5,5), GetEngineObject()->GetAbsOrigin() + Vector(5,5,5), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &trace );

	entityIndex = trace.m_pEnt ? trace.m_pEnt->entindex() : 0;

	CBroadcastRecipientFilter filter;

	te->BSPDecal( filter, 0.0, 
		&GetEngineObject()->GetAbsOrigin(), entityIndex, m_nTexture );

	SetThink( &CDecal::SUB_Remove );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
}


void CDecal::InputActivate( inputdata_t &inputdata )
{
	TriggerDecal( inputdata.pActivator, inputdata.pCaller, USE_ON, 0 );
}


void CDecal::StaticDecal( void )
{
	class CTraceFilterValidForDecal : public CTraceFilterSimple
	{
	public:
		CTraceFilterValidForDecal(const IHandleEntity *passentity, int collisionGroup )
		 :	CTraceFilterSimple( passentity, collisionGroup )
		{
		}

		virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
		{
			static const char *ppszIgnoredClasses[] = 
			{
				"weapon_*",
				"item_*",
				"prop_ragdoll",
				"prop_dynamic",
				"prop_static",
				"prop_physics",
				"npc_bullseye",  // Tracker 15335
			};

			CBaseEntity *pEntity = EntityFromEntityHandle( pServerEntity );

			// Tracker 15335:  Never impact decals against entities which are not rendering, either.
			if ( pEntity->GetEngineObject()->IsEffectActive( EF_NODRAW ) )
				return false;

			for ( int i = 0; i < ARRAYSIZE(ppszIgnoredClasses); i++ )
			{
				if ( pEntity->ClassMatches( ppszIgnoredClasses[i] ) )
					return false;
			}


			return CTraceFilterSimple::ShouldHitEntity( pServerEntity, contentsMask );
		}
	};

	trace_t trace;
	CTraceFilterValidForDecal traceFilter( this, COLLISION_GROUP_NONE );
	int entityIndex, modelIndex = 0;

	Vector position = GetEngineObject()->GetAbsOrigin();
	UTIL_TraceLine( position - Vector(5,5,5), position + Vector(5,5,5),  MASK_SOLID, &traceFilter, &trace );

	bool canDraw = true;

	entityIndex = trace.m_pEnt ? trace.m_pEnt->entindex() : 0;
	if ( entityIndex )
	{
		CBaseEntity *ent = (CBaseEntity*)trace.m_pEnt;
		if ( ent )
		{
			modelIndex = ent->GetEngineObject()->GetModelIndex();
			VectorITransform(GetEngineObject()->GetAbsOrigin(), ent->GetEngineObject()->EntityToWorldTransform(), position );

			canDraw = ( modelIndex != 0 );
			if ( !canDraw )
			{
				Warning( "Suppressed StaticDecal which would have hit entity %i (class:%s, name:%s) with modelindex = 0\n",
					ent->entindex(),
					ent->GetClassname(),
					STRING( ent->GetEntityName() ) );
			}
		}
	}

	if ( canDraw )
	{
		engine->StaticDecal( position, m_nTexture, entityIndex, modelIndex, m_bLowPriority );
	}

	SUB_Remove();
}


bool CDecal::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "texture"))
	{
		// FIXME:  should decals all be preloaded?
		m_nTexture = UTIL_PrecacheDecal( szValue, true );
		
		// Found
		if (m_nTexture >= 0 )
			return true;
		Warning( "Can't find decal %s\n", szValue );
	}
	else
	{
		return BaseClass::KeyValue( szKeyName, szValue );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Projects a decal against a prop
//-----------------------------------------------------------------------------
class CProjectedDecal : public CPointEntity
{
public:
	DECLARE_CLASS( CProjectedDecal, CPointEntity );

	void	Spawn( void );
	bool	KeyValue( const char *szKeyName, const char *szValue );

	// Need to apply static decals here to get them into the signon buffer for the server appropriately
	virtual void Activate();

	void	TriggerDecal( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value );

	// Input handlers.
	void	InputActivate( inputdata_t &inputdata );

	DECLARE_DATADESC();

public:
	int		m_nTexture;
	float	m_flDistance;

private:
	void	ProjectDecal( CRecipientFilter& filter );

	void	StaticDecal( void );
};

BEGIN_DATADESC( CProjectedDecal )

	DEFINE_FIELD( m_nTexture, FIELD_INTEGER ),

	DEFINE_KEYFIELD( m_flDistance, FIELD_FLOAT, "Distance" ),

	// Function pointers
	DEFINE_FUNCTION( StaticDecal ),
	DEFINE_FUNCTION( TriggerDecal ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Activate", InputActivate ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( info_projecteddecal, CProjectedDecal );

// UNDONE:  These won't get sent to joining players in multi-player
void CProjectedDecal::Spawn( void )
{
	if ( m_nTexture < 0 || 
		(gpGlobals->deathmatch && GetEngineObject()->HasSpawnFlags( SF_DECAL_NOTINDEATHMATCH )) )
	{
		EntityList()->DestroyEntity( this );
		return;
	} 
}

void CProjectedDecal::Activate()
{
	BaseClass::Activate();

	if ( !GetEntityName() )
	{
		StaticDecal();
	}
	else
	{
		// if there IS a targetname, the decal sprays itself on when it is triggered.
		SetThink ( &CProjectedDecal::SUB_DoNothing );
		SetUse(&CProjectedDecal::TriggerDecal);
	}
}

void CProjectedDecal::InputActivate( inputdata_t &inputdata )
{
	TriggerDecal( inputdata.pActivator, inputdata.pCaller, USE_ON, 0 );
}

void CProjectedDecal::ProjectDecal( CRecipientFilter& filter )
{
	te->ProjectDecal( filter, 0.0, 
		&GetEngineObject()->GetAbsOrigin(), &GetEngineObject()->GetAbsAngles(), m_flDistance, m_nTexture );
}

void CProjectedDecal::TriggerDecal ( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value )
{
	CBroadcastRecipientFilter filter;

	ProjectDecal( filter );

	SetThink( &CProjectedDecal::SUB_Remove );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
}

void CProjectedDecal::StaticDecal( void )
{
	CBroadcastRecipientFilter initFilter;
	initFilter.MakeInitMessage();

	ProjectDecal( initFilter );

	SUB_Remove();
}


bool CProjectedDecal::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "texture"))
	{
		// FIXME:  should decals all be preloaded?
		m_nTexture = UTIL_PrecacheDecal( szValue, true );
		
		// Found
		if (m_nTexture >= 0 )
			return true;
		Warning( "Can't find decal %s\n", szValue );
	}
	else
	{
		return BaseClass::KeyValue( szKeyName, szValue );
	}

	return true;
}

//=======================
// CWorld
//
// This spawns first when each level begins.
//=======================
#ifdef CSTRIKE_DLL
LINK_ENTITY_TO_CLASS( worldspawn, CCSGameWorld );
#endif
#ifdef DOD_DLL
LINK_ENTITY_TO_CLASS(worldspawn, CDODGameWorld);
#endif // DOD_DLL
#if defined(HL1_DLL) && !defined(HL1MP_DLL)
LINK_ENTITY_TO_CLASS(worldspawn, CHalfLife1World);
#endif // HL1_DLL
#ifdef HL1MP_DLL
LINK_ENTITY_TO_CLASS(worldspawn, CHL1MPWorld);
#endif // HL1MP_DLL
#if defined(HL2_DLL) && !defined(HL2MP) && !defined(PORTAL)
LINK_ENTITY_TO_CLASS(worldspawn, CHalfLife2World);
#endif
#ifdef HL2MP
LINK_ENTITY_TO_CLASS(worldspawn, CHL2MPWorld);
#endif
#ifdef PORTAL
LINK_ENTITY_TO_CLASS(worldspawn, CPortalGameWorld);
#endif // PORTAL


BEGIN_DATADESC( CWorld )

	DEFINE_FIELD( m_flWaveHeight, FIELD_FLOAT ),

	// keyvalues are parsed from map, but not saved/loaded
	DEFINE_KEYFIELD( m_iszChapterTitle, FIELD_STRING, "chaptertitle" ),
	DEFINE_KEYFIELD( m_bStartDark,		FIELD_BOOLEAN, "startdark" ),
	DEFINE_KEYFIELD( m_bDisplayTitle,	FIELD_BOOLEAN, "gametitle" ),
	DEFINE_FIELD( m_WorldMins, FIELD_VECTOR ),
	DEFINE_FIELD( m_WorldMaxs, FIELD_VECTOR ),
#ifdef _X360
	DEFINE_KEYFIELD( m_flMaxOccludeeArea, FIELD_FLOAT, "maxoccludeearea_x360" ),
	DEFINE_KEYFIELD( m_flMinOccluderArea, FIELD_FLOAT, "minoccluderarea_x360" ),
#else
	DEFINE_KEYFIELD( m_flMaxOccludeeArea, FIELD_FLOAT, "maxoccludeearea" ),
	DEFINE_KEYFIELD( m_flMinOccluderArea, FIELD_FLOAT, "minoccluderarea" ),
#endif
	DEFINE_KEYFIELD( m_flMaxPropScreenSpaceWidth, FIELD_FLOAT, "maxpropscreenwidth" ),
	DEFINE_KEYFIELD( m_flMinPropScreenSpaceWidth, FIELD_FLOAT, "minpropscreenwidth" ),
	DEFINE_KEYFIELD( m_iszDetailSpriteMaterial, FIELD_STRING, "detailmaterial" ),
	DEFINE_KEYFIELD( m_bColdWorld,		FIELD_BOOLEAN, "coldworld" ),

END_DATADESC()


// SendTable stuff.
IMPLEMENT_SERVERCLASS_ST(CWorld, DT_WORLD)
	SendPropFloat	(SENDINFO(m_flWaveHeight), 8, SPROP_ROUNDUP,	0.0f,	8.0f),
	SendPropVector	(SENDINFO(m_WorldMins),	-1,	SPROP_COORD),
	SendPropVector	(SENDINFO(m_WorldMaxs),	-1,	SPROP_COORD),
	SendPropInt		(SENDINFO(m_bStartDark), 1, SPROP_UNSIGNED ),
	SendPropFloat	(SENDINFO(m_flMaxOccludeeArea), 0, SPROP_NOSCALE ),
	SendPropFloat	(SENDINFO(m_flMinOccluderArea), 0, SPROP_NOSCALE ),
	SendPropFloat	(SENDINFO(m_flMaxPropScreenSpaceWidth), 0, SPROP_NOSCALE ),
	SendPropFloat	(SENDINFO(m_flMinPropScreenSpaceWidth), 0, SPROP_NOSCALE ),
	SendPropStringT (SENDINFO(m_iszDetailSpriteMaterial) ),
	SendPropInt		(SENDINFO(m_bColdWorld), 1, SPROP_UNSIGNED ),
END_SEND_TABLE()

//
// Just to ignore the "wad" field.
//
bool CWorld::KeyValue( const char *szKeyName, const char *szValue )
{
	if ( FStrEq(szKeyName, "skyname") )
	{
		// Sent over net now.
		ConVarRef skyname( "sv_skyname" );
		skyname.SetValue( szValue );
	}
	else if ( FStrEq(szKeyName, "newunit") )
	{
		// Single player only.  Clear save directory if set
		if ( atoi(szValue) )
		{
			extern void Game_SetOneWayTransition();
			Game_SetOneWayTransition();
		}
	}
	else if ( FStrEq(szKeyName, "world_mins") )
	{
		Vector vec;
		sscanf(	szValue, "%f %f %f", &vec.x, &vec.y, &vec.z );
		m_WorldMins = vec;
	}
	else if ( FStrEq(szKeyName, "world_maxs") )
	{
		Vector vec;
		sscanf(	szValue, "%f %f %f", &vec.x, &vec.y, &vec.z ); 
		m_WorldMaxs = vec;
	}
	else
		return BaseClass::KeyValue( szKeyName, szValue );

	return true;
}


extern bool		g_fGameOver;
static CWorld *g_WorldEntity = NULL;

CWorld* GetWorldEntity()
{
	return g_WorldEntity;
}

// In tf_gamerules.cpp or hl_gamerules.cpp.
extern IVoiceGameMgrHelper* g_pVoiceGameMgrHelper;
extern bool	g_fGameOver;

CWorld::CWorld( )
{
	//NetworkProp()->AttachEdict( RequiredEdictIndex() );
	mdlcache->ActivityList_Init();
	mdlcache->EventList_Init();
	m_bColdWorld = false;
	GetVoiceGameMgr()->Init(g_pVoiceGameMgrHelper, gpGlobals->maxClients);
	ClearMultiDamage();

	m_flNextVerboseLogOutput = 0.0f;
}

void CWorld::PostConstructor(const char* szClassname, int iForceEdictIndex)
{
	GetEngineObject()->AddEFlags(EFL_KEEP_ON_RECREATE_ENTITIES);
	BaseClass::PostConstructor(szClassname, iForceEdictIndex);
	GetEngineObject()->SetSolid(SOLID_BSP);
	GetEngineObject()->SetMoveType(MOVETYPE_NONE);
	g_pGameRules = this;
}

CWorld::~CWorld( )
{
	mdlcache->EventList_Free();
	UTIL_UnLoadActivityRemapFile();
	mdlcache->ActivityList_Free();
	if ( g_pGameRules )
	{
		g_pGameRules->LevelShutdown();
		if (!g_pGameRules) {
			Error("m_pGameRules not inited!\n");
		}
		g_pGameRules->LevelShutdownPostEntity();
	}
	g_WorldEntity = NULL;
	g_pGameRules = NULL;
}


//------------------------------------------------------------------------------
// Purpose : Add a decal to the world
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CWorld::DecalTrace( trace_t *pTrace, char const *decalName)
{
	int index = decalsystem->GetDecalIndexForName( decalName );
	if ( index < 0 )
		return;

	CBroadcastRecipientFilter filter;
	if ( pTrace->hitbox != 0 )
	{
		te->Decal( filter, 0.0f, &pTrace->endpos, &pTrace->startpos, 0, pTrace->hitbox, index );
	}
	else
	{
		te->WorldDecal( filter, 0.0, &pTrace->endpos, index );
	}
}

void CWorld::RegisterSharedActivities( void )
{
	ActivityList_RegisterSharedActivities();
}

void CWorld::RegisterSharedEvents( void )
{
	EventList_RegisterSharedEvents();
}

int CWorld::UpdateTransmitState()
{
	// ALWAYS transmit to all clients.
	return SetTransmitState(FL_EDICT_ALWAYS);
}

void CWorld::Spawn( void )
{
	GetEngineObject()->SetLocalOrigin( vec3_origin );
	GetEngineObject()->SetLocalAngles( vec3_angle );
	// NOTE:  SHOULD NEVER BE ANYTHING OTHER THAN 1!!!
	GetEngineObject()->SetModelIndex( 1 );
	// world model
	GetEngineObject()->SetModelName( AllocPooledString( modelinfo->GetModelName(GetEngineObject()->GetModel() ) ) );
	GetEngineObject()->AddFlag( FL_WORLDBRUSH );

	g_EventQueue.Init();
	Precache( );
	engine->GlobalEntity_Add( "is_console", STRING(gpGlobals->mapname), ( IsConsole() ) ? GLOBAL_ON : GLOBAL_OFF );
	engine->GlobalEntity_Add( "is_pc", STRING(gpGlobals->mapname), ( !IsConsole() ) ? GLOBAL_ON : GLOBAL_OFF );
}

static const char *g_DefaultLightstyles[] =
{
	// 0 normal
	"m",
	// 1 FLICKER (first variety)
	"mmnmmommommnonmmonqnmmo",
	// 2 SLOW STRONG PULSE
	"abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",
	// 3 CANDLE (first variety)
	"mmmmmaaaaammmmmaaaaaabcdefgabcdefg",
	// 4 FAST STROBE
	"mamamamamama",
	// 5 GENTLE PULSE 1
	"jklmnopqrstuvwxyzyxwvutsrqponmlkj",
	// 6 FLICKER (second variety)
	"nmonqnmomnmomomno",
	// 7 CANDLE (second variety)
	"mmmaaaabcdefgmmmmaaaammmaamm",
	// 8 CANDLE (third variety)
	"mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",
	// 9 SLOW STROBE (fourth variety)
	"aaaaaaaazzzzzzzz",
	// 10 FLUORESCENT FLICKER
	"mmamammmmammamamaaamammma",
	// 11 SLOW PULSE NOT FADE TO BLACK
	"abcdefghijklmnopqrrqponmlkjihgfedcba",
	// 12 UNDERWATER LIGHT MUTATION
	// this light only distorts the lightmap - no contribution
	// is made to the brightness of affected surfaces
	"mmnnmmnnnmmnn",
};


const char *GetDefaultLightstyleString( int styleIndex )
{
	if ( styleIndex < ARRAYSIZE(g_DefaultLightstyles) )
	{
		return g_DefaultLightstyles[styleIndex];
	}
	return "m";
}

void InstallGameRules();
void CWorld::Precache( void )
{
	g_WorldEntity = this;
	g_fGameOver = false;
	g_pLastSpawn = NULL;

	ConVarRef stepsize( "sv_stepsize" );
	stepsize.SetValue( 18 );

	ConVarRef roomtype( "room_type" );
	roomtype.SetValue( 0 );

	InstallGameRules();
	//Assert( g_pGameRules );
	//g_pGameRules->Init();

	CSoundEnt::InitSoundEnt();

	// Only allow precaching between LevelInitPreEntity and PostEntity
	engine->SetAllowPrecache( true );//CBaseEntity::

	EntityList()->LevelInitPreEntity();
	IGameSystem::LevelInitPreEntityAllSystems();// STRING(GetEngineObject()->GetModelName() ) 

	// Create the player resource
	g_pGameRules->CreateStandardEntities();

	// UNDONE: Make most of these things server systems or precache_registers
	// =================================================
	//	Activities
	// =================================================
	UTIL_UnLoadActivityRemapFile();
	mdlcache->ActivityList_Clear();
	RegisterSharedActivities();

	mdlcache->EventList_Clear();
	RegisterSharedEvents();

	InitBodyQue();
// init sentence group playback stuff from sentences.txt.
// ok to call this multiple times, calls after first are ignored.

	SENTENCEG_Init();

	// Precache standard particle systems
	PrecacheStandardParticleSystems( );

// the area based ambient sounds MUST be the first precache_sounds

// player precaches     
	W_Precache ();									// get weapon precaches
	ClientPrecache();
	//g_pGameRules->Precache();
	// precache all temp ent stuff
	CBaseTempEntity::PrecacheTempEnts();

	g_Language.SetValue( LANGUAGE_ENGLISH );	// TODO use VGUI to get current language

	if ( g_Language.GetInt() == LANGUAGE_GERMAN )
	{
		engine->PrecacheModel( "models/germangibs.mdl" );
	}
	else
	{
		engine->PrecacheModel( "models/gibs/hgibs.mdl" );
	}

	g_pSoundEmitterSystem->PrecacheScriptSound( "BaseEntity.EnterWater" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "BaseEntity.ExitWater" );

//
// Setup light animation tables. 'a' is total darkness, 'z' is maxbright.
//
	for ( int i = 0; i < ARRAYSIZE(g_DefaultLightstyles); i++ )
	{
		engine->LightStyle( i, GetDefaultLightstyleString(i) );
	}

	// styles 32-62 are assigned by the light program for switchable lights

	// 63 testing
	engine->LightStyle(63, "a");

	// =================================================
	//	Load and Init AI Networks
	// =================================================
	CAI_NetworkManager::InitializeAINetworks();
	// =================================================
	//	Load and Init AI Schedules
	// =================================================
	g_AI_SchedulesManager.LoadAllSchedules();
	// =================================================
	//	Initialize NPC Relationships
	// =================================================
	g_pGameRules->InitDefaultAIRelationships();
	CBaseCombatCharacter::InitInteractionSystem();

	// Call all registered precachers.
	CPrecacheRegister::Precache();	

	if ( m_iszChapterTitle != NULL_STRING )
	{
		DevMsg( 2, "Chapter title: %s\n", STRING(m_iszChapterTitle) );
		CMessage *pMessage = (CMessage *)CBaseEntity::Create( "env_message", vec3_origin, vec3_angle, NULL );
		if ( pMessage )
		{
			pMessage->SetMessage( m_iszChapterTitle );
			m_iszChapterTitle = NULL_STRING;

			// send the message entity a play message command, delayed by 1 second
			pMessage->GetEngineObject()->AddSpawnFlags( SF_MESSAGE_ONCE );
			pMessage->SetThink( &CMessage::SUB_CallUseToggle );
			pMessage->GetEngineObject()->SetNextThink( gpGlobals->curtime + 1.0f );
		}
	}

	g_iszFuncBrushClassname = AllocPooledString("func_brush");
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float GetRealTime()
{
	return engine->Time();
}


bool CWorld::GetDisplayTitle() const
{
	return m_bDisplayTitle;
}

bool CWorld::GetStartDark() const
{
	return m_bStartDark;
}

void CWorld::SetDisplayTitle( bool display )
{
	m_bDisplayTitle = display;
}

void CWorld::SetStartDark( bool startdark )
{
	m_bStartDark = startdark;
}

bool CWorld::IsColdWorld( void )
{
	return m_bColdWorld;
}

//-----------------------------------------------------------------------------
// Purpose: Return true if the specified player can carry any more of the ammo type
//-----------------------------------------------------------------------------
bool CWorld::CanHaveAmmo(CBaseCombatCharacter* pPlayer, int iAmmoIndex)
{
	if (iAmmoIndex > -1)
	{
		// Get the max carrying capacity for this ammo
		int iMaxCarry = GetAmmoDef()->MaxCarry(iAmmoIndex);

		// Does the player have room for more of this type of ammo?
		if (pPlayer->GetAmmoCount(iAmmoIndex) < iMaxCarry)
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Return true if the specified player can carry any more of the ammo type
//-----------------------------------------------------------------------------
bool CWorld::CanHaveAmmo(CBaseCombatCharacter* pPlayer, const char* szName)
{
	return CanHaveAmmo(pPlayer, GetAmmoDef()->Index(szName));
}

//=========================================================
//=========================================================
IServerEntity* CWorld::GetPlayerSpawnSpot(CBasePlayer* pPlayer)
{
	IServerEntity* pSpawnSpot = pPlayer->EntSelectSpawnPoint();
	Assert(pSpawnSpot);

	pPlayer->GetEngineObject()->SetLocalOrigin(pSpawnSpot->GetEngineObject()->GetAbsOrigin() + Vector(0, 0, 1));
	pPlayer->GetEngineObject()->SetAbsVelocity(vec3_origin);
	pPlayer->GetEngineObject()->SetLocalAngles(pSpawnSpot->GetEngineObject()->GetLocalAngles());
	pPlayer->m_Local.m_vecPunchAngle = vec3_angle;
	pPlayer->m_Local.m_vecPunchAngleVel = vec3_angle;
	pPlayer->SnapEyeAngles(pSpawnSpot->GetEngineObject()->GetLocalAngles());

	return pSpawnSpot;
}

// checks if the spot is clear of players
bool CWorld::IsSpawnPointValid(CBaseEntity* pSpot, CBasePlayer* pPlayer)
{
	CBaseEntity* ent = NULL;

	if (!pSpot->IsTriggered(pPlayer))
	{
		return false;
	}

	for (CEntitySphereQuery sphere(pSpot->GetEngineObject()->GetAbsOrigin(), 128); (ent = sphere.GetCurrentEntity()) != NULL; sphere.NextEntity())
	{
		// if ent is a client, don't spawn on 'em
		if (ent->IsPlayer() && ent != pPlayer)
			return false;
	}

	return true;
}

//=========================================================
//=========================================================
bool CWorld::CanHavePlayerItem(CBasePlayer* pPlayer, CBaseCombatWeapon* pWeapon)
{
	/*
		if ( pWeapon->m_pszAmmo1 )
		{
			if ( !CanHaveAmmo( pPlayer, pWeapon->m_iPrimaryAmmoType ) )
			{
				// we can't carry anymore ammo for this gun. We can only
				// have the gun if we aren't already carrying one of this type
				if ( pPlayer->Weapon_OwnsThisType( pWeapon ) )
				{
					return FALSE;
				}
			}
		}
		else
		{
			// weapon doesn't use ammo, don't take another if you already have it.
			if ( pPlayer->Weapon_OwnsThisType( pWeapon ) )
			{
				return FALSE;
			}
		}
	*/
	// note: will fall through to here if GetItemInfo doesn't fill the struct!
	return TRUE;
}

//=========================================================
// load the SkillData struct with the proper values based on the skill level.
//=========================================================
void CWorld::RefreshSkillData(bool forceUpdate)
{
#ifndef CLIENT_DLL
	if (!forceUpdate)
	{
		if (engine->GlobalEntity_IsInTable("skill.cfg"))
			return;
	}
	engine->GlobalEntity_Add("skill.cfg", STRING(gpGlobals->mapname), GLOBAL_ON);

#if !defined( TF_DLL ) && !defined( DOD_DLL )
	char	szExec[256];
#endif 

	ConVarRef skill("skill");

	SetSkillLevel(skill.IsValid() ? skill.GetInt() : 1);

#ifdef HL2_DLL
	// HL2 current only uses one skill config file that represents MEDIUM skill level and
	// synthesizes EASY and HARD. (sjb)
	Q_snprintf(szExec, sizeof(szExec), "exec skill_manifest.cfg\n");

	engine->ServerCommand(szExec);
	engine->ServerExecute();
#else

#if !defined( TF_DLL ) && !defined( DOD_DLL )
	Q_snprintf(szExec, sizeof(szExec), "exec skill%d.cfg\n", GetSkillLevel());

	engine->ServerCommand(szExec);
	engine->ServerExecute();
#endif // TF_DLL && DOD_DLL

#endif // HL2_DLL
#endif // CLIENT_DLL
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool IsExplosionTraceBlocked(trace_t* ptr)
{
	if (ptr->DidHitWorld())
		return true;

	if (ptr->m_pEnt == NULL)
		return false;

	if (ptr->m_pEnt->GetEngineObject()->GetMoveType() == MOVETYPE_PUSH)
	{
		// All doors are push, but not all things that push are doors. This 
		// narrows the search before we start to do classname compares.
		if (FClassnameIs((CBaseEntity*)ptr->m_pEnt, "prop_door_rotating") ||
			FClassnameIs((CBaseEntity*)ptr->m_pEnt, "func_door") ||
			FClassnameIs((CBaseEntity*)ptr->m_pEnt, "func_door_rotating"))
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Default implementation of radius damage
//-----------------------------------------------------------------------------
#define ROBUST_RADIUS_PROBE_DIST 16.0f // If a solid surface blocks the explosion, this is how far to creep along the surface looking for another way to the target
void CWorld::RadiusDamage(const CTakeDamageInfo& info, const Vector& vecSrcIn, float flRadius, int iClassIgnore, CBaseEntity* pEntityIgnore)
{
	const int MASK_RADIUS_DAMAGE = MASK_SHOT & (~CONTENTS_HITBOX);
	CBaseEntity* pEntity = NULL;
	trace_t		tr;
	float		flAdjustedDamage, falloff;
	Vector		vecSpot;

	Vector vecSrc = vecSrcIn;

	if (flRadius)
		falloff = info.GetDamage() / flRadius;
	else
		falloff = 1.0;

	int bInWater = (UTIL_PointContents(vecSrc) & MASK_WATER) ? true : false;

#ifdef HL2_DLL
	if (bInWater)
	{
		// Only muffle the explosion if deeper than 2 feet in water.
		if (!(UTIL_PointContents(vecSrc + Vector(0, 0, 24)) & MASK_WATER))
		{
			bInWater = false;
		}
	}
#endif // HL2_DLL

	vecSrc.z += 1;// in case grenade is lying on the ground

	float flHalfRadiusSqr = Square(flRadius / 2.0f);

	// iterate on all entities in the vicinity.
	for (CEntitySphereQuery sphere(vecSrc, flRadius); (pEntity = sphere.GetCurrentEntity()) != NULL; sphere.NextEntity())
	{
		// This value is used to scale damage when the explosion is blocked by some other object.
		float flBlockedDamagePercent = 0.0f;

		if (pEntity == pEntityIgnore)
			continue;

		if (pEntity->m_takedamage == DAMAGE_NO)
			continue;

		// UNDONE: this should check a damage mask, not an ignore
		if (iClassIgnore != CLASS_NONE && pEntity->Classify() == iClassIgnore)
		{// houndeyes don't hurt other houndeyes with their attack
			continue;
		}

		// blast's don't tavel into or out of water
		if (bInWater && pEntity->GetWaterLevel() == 0)
			continue;

		if (!bInWater && pEntity->GetWaterLevel() == 3)
			continue;

		// Check that the explosion can 'see' this entity.
		vecSpot = pEntity->BodyTarget(vecSrc, false);
		UTIL_TraceLine(vecSrc, vecSpot, MASK_RADIUS_DAMAGE, info.GetInflictor(), COLLISION_GROUP_NONE, &tr);

		if (old_radius_damage.GetBool())
		{
			if (tr.fraction != 1.0 && tr.m_pEnt != pEntity)
				continue;
		}
		else
		{
			if (tr.fraction != 1.0)
			{
				if (IsExplosionTraceBlocked(&tr))
				{
					if (ShouldUseRobustRadiusDamage(pEntity))
					{
						if (vecSpot.DistToSqr(vecSrc) > flHalfRadiusSqr)
						{
							// Only use robust model on a target within one-half of the explosion's radius.
							continue;
						}

						Vector vecToTarget = vecSpot - tr.endpos;
						VectorNormalize(vecToTarget);

						// We're going to deflect the blast along the surface that 
						// interrupted a trace from explosion to this target.
						Vector vecUp, vecDeflect;
						CrossProduct(vecToTarget, tr.plane.normal, vecUp);
						CrossProduct(tr.plane.normal, vecUp, vecDeflect);
						VectorNormalize(vecDeflect);

						// Trace along the surface that intercepted the blast...
						UTIL_TraceLine(tr.endpos, tr.endpos + vecDeflect * ROBUST_RADIUS_PROBE_DIST, MASK_RADIUS_DAMAGE, info.GetInflictor(), COLLISION_GROUP_NONE, &tr);
						//NDebugOverlay::Line( tr.startpos, tr.endpos, 255, 255, 0, false, 10 );

						// ...to see if there's a nearby edge that the explosion would 'spill over' if the blast were fully simulated.
						UTIL_TraceLine(tr.endpos, vecSpot, MASK_RADIUS_DAMAGE, info.GetInflictor(), COLLISION_GROUP_NONE, &tr);
						//NDebugOverlay::Line( tr.startpos, tr.endpos, 255, 0, 0, false, 10 );

						if (tr.fraction != 1.0 && tr.DidHitWorld())
						{
							// Still can't reach the target.
							continue;
						}
						// else fall through
					}
					else
					{
						continue;
					}
				}

				// UNDONE: Probably shouldn't let children block parents either?  Or maybe those guys should set their owner if they want this behavior?
				// HL2 - Dissolve damage is not reduced by interposing non-world objects
				if (tr.m_pEnt && tr.m_pEnt != pEntity && tr.m_pEnt->GetOwnerEntity() != pEntity)
				{
					// Some entity was hit by the trace, meaning the explosion does not have clear
					// line of sight to the entity that it's trying to hurt. If the world is also
					// blocking, we do no damage.
					CBaseEntity* pBlockingEntity = (CBaseEntity*)tr.m_pEnt;
					//Msg( "%s may be blocked by %s...", pEntity->GetClassname(), pBlockingEntity->GetClassname() );

					UTIL_TraceLine(vecSrc, vecSpot, CONTENTS_SOLID, info.GetInflictor(), COLLISION_GROUP_NONE, &tr);

					if (tr.fraction != 1.0)
					{
						continue;
					}

					// Now, if the interposing object is physics, block some explosion force based on its mass.
					if (pBlockingEntity->GetEngineObject()->VPhysicsGetObject())
					{
						const float MASS_ABSORB_ALL_DAMAGE = 350.0f;
						float flMass = pBlockingEntity->GetEngineObject()->VPhysicsGetObject()->GetMass();
						float scale = flMass / MASS_ABSORB_ALL_DAMAGE;

						// Absorbed all the damage.
						if (scale >= 1.0f)
						{
							continue;
						}

						ASSERT(scale > 0.0f);
						flBlockedDamagePercent = scale;
						//Msg("  Object (%s) weighing %fkg blocked %f percent of explosion damage\n", pBlockingEntity->GetClassname(), flMass, scale * 100.0f);
					}
					else
					{
						// Some object that's not the world and not physics. Generically block 25% damage
						flBlockedDamagePercent = 0.25f;
					}
				}
			}
		}
		// decrease damage for an ent that's farther from the bomb.
		flAdjustedDamage = (vecSrc - tr.endpos).Length() * falloff;
		flAdjustedDamage = info.GetDamage() - flAdjustedDamage;

		if (flAdjustedDamage <= 0)
		{
			continue;
		}

		// the explosion can 'see' this entity, so hurt them!
		if (tr.startsolid)
		{
			// if we're stuck inside them, fixup the position and distance
			tr.endpos = vecSrc;
			tr.fraction = 0.0;
		}

		CTakeDamageInfo adjustedInfo = info;
		//Msg("%s: Blocked damage: %f percent (in:%f  out:%f)\n", pEntity->GetClassname(), flBlockedDamagePercent * 100, flAdjustedDamage, flAdjustedDamage - (flAdjustedDamage * flBlockedDamagePercent) );
		adjustedInfo.SetDamage(flAdjustedDamage - (flAdjustedDamage * flBlockedDamagePercent));

		// Now make a consideration for skill level!
		if (info.GetAttacker() && info.GetAttacker()->IsPlayer() && pEntity->IsNPC())
		{
			// An explosion set off by the player is harming an NPC. Adjust damage accordingly.
			adjustedInfo.AdjustPlayerDamageInflictedForSkillLevel();
		}

		Vector dir = vecSpot - vecSrc;
		VectorNormalize(dir);

		// If we don't have a damage force, manufacture one
		if (adjustedInfo.GetDamagePosition() == vec3_origin || adjustedInfo.GetDamageForce() == vec3_origin)
		{
			if (!(adjustedInfo.GetDamageType() & DMG_PREVENT_PHYSICS_FORCE))
			{
				CalculateExplosiveDamageForce(&adjustedInfo, dir, vecSrc);
			}
		}
		else
		{
			// Assume the force passed in is the maximum force. Decay it based on falloff.
			float flForce = adjustedInfo.GetDamageForce().Length() * falloff;
			adjustedInfo.SetDamageForce(dir * flForce);
			adjustedInfo.SetDamagePosition(vecSrc);
		}

		if (tr.fraction != 1.0 && pEntity == tr.m_pEnt)
		{
			ClearMultiDamage();
			pEntity->DispatchTraceAttack(adjustedInfo, dir, &tr);
			ApplyMultiDamage();
		}
		else
		{
			pEntity->TakeDamage(adjustedInfo);
		}

		// Now hit all triggers along the way that respond to damage... 
		pEntity->TraceAttackToTriggers(adjustedInfo, vecSrc, tr.endpos, dir);

#if defined( GAME_DLL )
		if (info.GetAttacker() && info.GetAttacker()->IsPlayer() && ToBaseCombatCharacter((IServerEntity*)tr.m_pEnt))
		{

			// This is a total hack!!!
			bool bIsPrimary = true;
			CBasePlayer* player = ToBasePlayer((IServerEntity*)info.GetAttacker());
			CBaseCombatWeapon* pWeapon = player->GetActiveWeapon();
			if (pWeapon && FClassnameIs(pWeapon, "weapon_smg1"))
			{
				bIsPrimary = false;
			}

			gamestats->Event_WeaponHit(player, bIsPrimary, (pWeapon != NULL) ? player->GetActiveWeapon()->GetClassname() : "NULL", info);
		}
#endif
	}
}


bool CWorld::ClientCommand(CBaseEntity* pEdict, const CCommand& args)
{
	if (pEdict->IsPlayer())
	{
		if (GetVoiceGameMgr()->ClientCommand(static_cast<CBasePlayer*>(pEdict), args))
			return true;
	}

	return false;
}

// Level init, shutdown
void CWorld::LevelInitPreEntity()
{
	
}

void CWorld::LevelInitPostEntity()
{
	
}

// The level is shutdown in two parts
void CWorld::LevelShutdownPreEntity()
{
	
}

void CWorld::LevelShutdownPostEntity()
{
	
}

void CWorld::FrameUpdatePreEntityThink()
{
	
}

void CWorld::FrameUpdatePostEntityThink()
{
	VPROF("CGameRules::FrameUpdatePostEntityThink");
	Think();
}

// Hook into the convar from the engine
ConVar skill("skill", "1");

void CWorld::Think()
{
	GetVoiceGameMgr()->Update(gpGlobals->frametime);
	SetSkillLevel(skill.GetInt());

	if (log_verbose_enable.GetBool())
	{
		if (m_flNextVerboseLogOutput < gpGlobals->curtime)
		{
			ProcessVerboseLogOutput();
			m_flNextVerboseLogOutput = gpGlobals->curtime + log_verbose_interval.GetFloat();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called at the end of GameFrame (i.e. after all game logic has run this frame)
//-----------------------------------------------------------------------------
void CWorld::EndGameFrame(void)
{
	// If you hit this assert, it means something called AddMultiDamage() and didn't ApplyMultiDamage().
	// The g_MultiDamage.m_hAttacker & g_MultiDamage.m_hInflictor should give help you figure out the culprit.
	Assert(g_MultiDamage.IsClear());
	if (!g_MultiDamage.IsClear())
	{
		Warning("Unapplied multidamage left in the system:\nTarget: %s\nInflictor: %s\nAttacker: %s\nDamage: %.2f\n",
			g_MultiDamage.GetTarget()->GetDebugName(),
			g_MultiDamage.GetInflictor()->GetDebugName(),
			g_MultiDamage.GetAttacker()->GetDebugName(),
			g_MultiDamage.GetDamage());
		ApplyMultiDamage();
	}
}

//-----------------------------------------------------------------------------
// trace line rules
//-----------------------------------------------------------------------------
float CWorld::WeaponTraceEntity(CBaseEntity* pEntity, const Vector& vecStart, const Vector& vecEnd,
	unsigned int mask, trace_t* ptr)
{
	EntityList()->GetEngineWorld()->TraceEntity(pEntity->GetEngineObject(), vecStart, vecEnd, mask, ptr);
	return 1.0f;
}


void CWorld::CreateStandardEntities()
{
	g_pPlayerResource = (CPlayerResource*)CBaseEntity::Create("player_manager", vec3_origin, vec3_angle);
	g_pPlayerResource->GetEngineObject()->AddEFlags(EFL_KEEP_ON_RECREATE_ENTITIES);
}

//-----------------------------------------------------------------------------
// Purpose: Inform client(s) they can mark the indicated achievement as completed (SERVER VERSION)
// Input  : filter - which client(s) to send this to
//			iAchievementID - The enumeration value of the achievement to mark (see TODO:Kerry, what file will have the mod's achievement enum?) 
//-----------------------------------------------------------------------------
void CWorld::MarkAchievement(IRecipientFilter& filter, char const* pchAchievementName)
{
	gamestats->Event_IncrementCountedStatistic(vec3_origin, pchAchievementName, 1.0f);

	IAchievementMgr* pAchievementMgr = engine->GetAchievementMgr();
	if (!pAchievementMgr)
		return;
	pAchievementMgr->OnMapEvent(pchAchievementName);
}

bool CWorld::SwitchToNextBestWeapon(CBaseCombatCharacter* pPlayer, CBaseCombatWeapon* pCurrentWeapon)
{
	return false;
}

CBaseCombatWeapon* CWorld::GetNextBestWeapon(CBaseCombatCharacter* pPlayer, CBaseCombatWeapon* pCurrentWeapon)
{
	return NULL;
}

bool CWorld::ShouldCollide(int collisionGroup0, int collisionGroup1)
{
	if (collisionGroup0 > collisionGroup1)
	{
		// swap so that lowest is always first
		::V_swap(collisionGroup0, collisionGroup1);
	}

#ifndef HL2MP
	if ((collisionGroup0 == COLLISION_GROUP_PLAYER || collisionGroup0 == COLLISION_GROUP_PLAYER_MOVEMENT) &&
		collisionGroup1 == COLLISION_GROUP_PUSHAWAY)
	{
		return false;
	}
#endif

	if (collisionGroup0 == COLLISION_GROUP_DEBRIS && collisionGroup1 == COLLISION_GROUP_PUSHAWAY)
	{
		// let debris and multiplayer objects collide
		return true;
	}

	// --------------------------------------------------------------------------
	// NOTE: All of this code assumes the collision groups have been sorted!!!!
	// NOTE: Don't change their order without rewriting this code !!!
	// --------------------------------------------------------------------------

	// Don't bother if either is in a vehicle...
	if ((collisionGroup0 == COLLISION_GROUP_IN_VEHICLE) || (collisionGroup1 == COLLISION_GROUP_IN_VEHICLE))
		return false;

	if ((collisionGroup1 == COLLISION_GROUP_DOOR_BLOCKER) && (collisionGroup0 != COLLISION_GROUP_NPC))
		return false;

	if ((collisionGroup0 == COLLISION_GROUP_PLAYER) && (collisionGroup1 == COLLISION_GROUP_PASSABLE_DOOR))
		return false;

	if (collisionGroup0 == COLLISION_GROUP_DEBRIS || collisionGroup0 == COLLISION_GROUP_DEBRIS_TRIGGER)
	{
		// put exceptions here, right now this will only collide with COLLISION_GROUP_NONE
		return false;
	}

	// Dissolving guys only collide with COLLISION_GROUP_NONE
	if ((collisionGroup0 == COLLISION_GROUP_DISSOLVING) || (collisionGroup1 == COLLISION_GROUP_DISSOLVING))
	{
		if (collisionGroup0 != COLLISION_GROUP_NONE)
			return false;
	}

	// doesn't collide with other members of this group
	// or debris, but that's handled above
	if (collisionGroup0 == COLLISION_GROUP_INTERACTIVE_DEBRIS && collisionGroup1 == COLLISION_GROUP_INTERACTIVE_DEBRIS)
		return false;

#ifndef HL2MP
	// This change was breaking HL2DM
	// Adrian: TEST! Interactive Debris doesn't collide with the player.
	if (collisionGroup0 == COLLISION_GROUP_INTERACTIVE_DEBRIS && (collisionGroup1 == COLLISION_GROUP_PLAYER_MOVEMENT || collisionGroup1 == COLLISION_GROUP_PLAYER))
		return false;
#endif

	if (collisionGroup0 == COLLISION_GROUP_BREAKABLE_GLASS && collisionGroup1 == COLLISION_GROUP_BREAKABLE_GLASS)
		return false;

	// interactive objects collide with everything except debris & interactive debris
	if (collisionGroup1 == COLLISION_GROUP_INTERACTIVE && collisionGroup0 != COLLISION_GROUP_NONE)
		return false;

	// Projectiles hit everything but debris, weapons, + other projectiles
	if (collisionGroup1 == COLLISION_GROUP_PROJECTILE)
	{
		if (collisionGroup0 == COLLISION_GROUP_DEBRIS ||
			collisionGroup0 == COLLISION_GROUP_WEAPON ||
			collisionGroup0 == COLLISION_GROUP_PROJECTILE)
		{
			return false;
		}
	}

	// Don't let vehicles collide with weapons
	// Don't let players collide with weapons...
	// Don't let NPCs collide with weapons
	// Weapons are triggers, too, so they should still touch because of that
	if (collisionGroup1 == COLLISION_GROUP_WEAPON)
	{
		if (collisionGroup0 == COLLISION_GROUP_VEHICLE ||
			collisionGroup0 == COLLISION_GROUP_PLAYER ||
			collisionGroup0 == COLLISION_GROUP_NPC)
		{
			return false;
		}
	}

	// collision with vehicle clip entity??
	if (collisionGroup0 == COLLISION_GROUP_VEHICLE_CLIP || collisionGroup1 == COLLISION_GROUP_VEHICLE_CLIP)
	{
		// yes then if it's a vehicle, collide, otherwise no collision
		// vehicle sorts lower than vehicle clip, so must be in 0
		if (collisionGroup0 == COLLISION_GROUP_VEHICLE)
			return true;
		// vehicle clip against non-vehicle, no collision
		return false;
	}

	return true;
}


const CViewVectors* CWorld::GetViewVectors() const
{
	return &g_DefaultViewVectors;
}


//-----------------------------------------------------------------------------
// Purpose: Returns how much damage the given ammo type should do to the victim
//			when fired by the attacker.
// Input  : pAttacker - Dude what shot the gun.
//			pVictim - Dude what done got shot.
//			nAmmoType - What been shot out.
// Output : How much hurt to put on dude what done got shot (pVictim).
//-----------------------------------------------------------------------------
float CWorld::GetAmmoDamage(CBaseEntity* pAttacker, CBaseEntity* pVictim, int nAmmoType)
{
	float flDamage = 0;
	CAmmoDef* pAmmoDef = GetAmmoDef();

	if (pAttacker->IsPlayer())
	{
		flDamage = pAmmoDef->PlrDamage(nAmmoType);
	}
	else
	{
		flDamage = pAmmoDef->NPCDamage(nAmmoType);
	}

	return flDamage;
}

const char* CWorld::GetChatPrefix(bool bTeamOnly, CBasePlayer* pPlayer)
{
	if (pPlayer && pPlayer->IsAlive() == false)
	{
		if (bTeamOnly)
			return "*DEAD*(TEAM)";
		else
			return "*DEAD*";
	}

	return "";
}

void CWorld::CheckHaptics(CBasePlayer* pPlayer)
{
	// NVNT see if the client of pPlayer is using a haptic device.
	const char* pszHH = engine->GetClientConVarValue(pPlayer->entindex(), "hap_HasDevice");
	if (pszHH)
	{
		int iHH = atoi(pszHH);
		pPlayer->SetHaptics(iHH != 0);
	}
}

void CWorld::ClientSettingsChanged(CBasePlayer* pPlayer)
{
	const char* pszName = engine->GetClientConVarValue(pPlayer->entindex(), "name");

	const char* pszOldName = pPlayer->GetPlayerName();

	// msg everyone if someone changes their name,  and it isn't the first time (changing no name to current name)
	// Note, not using FStrEq so that this is case sensitive
	if (pszOldName[0] != 0 && Q_strcmp(pszOldName, pszName))
	{
		char text[256];
		Q_snprintf(text, sizeof(text), "%s changed name to %s\n", pszOldName, pszName);

		UTIL_ClientPrintAll(HUD_PRINTTALK, text);

		IGameEvent* event = gameeventmanager->CreateEvent("player_changename");
		if (event)
		{
			event->SetInt("userid", pPlayer->GetUserID());
			event->SetString("oldname", pszOldName);
			event->SetString("newname", pszName);
			gameeventmanager->FireEvent(event);
		}

		pPlayer->SetPlayerName(pszName);
	}

	const char* pszFov = engine->GetClientConVarValue(pPlayer->entindex(), "fov_desired");
	if (pszFov)
	{
		int iFov = atoi(pszFov);
		iFov = clamp(iFov, 75, 110);
		pPlayer->SetDefaultFOV(iFov);
	}

	// NVNT see if this user is still or has began using a haptic device
	const char* pszHH = engine->GetClientConVarValue(pPlayer->entindex(), "hap_HasDevice");
	if (pszHH)
	{
		int iHH = atoi(pszHH);
		pPlayer->SetHaptics(iHH != 0);
	}
}

CTacticalMissionManager* CWorld::TacticalMissionManagerFactory(void)
{
	return new CTacticalMissionManager;
}