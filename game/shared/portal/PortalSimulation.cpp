//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=====================================================================================//


#include "cbase.h"
#include "PortalSimulation.h"
//#include "physics.h"
#include "portal_shareddefs.h"
#include "StaticCollisionPolyhedronCache.h"
#include "model_types.h"
#include "filesystem.h"
#include "collisionutils.h"
#include "tier1/callqueue.h"
#include "portal_collideable_enumerator.h"

#ifndef CLIENT_DLL

#include "world.h"
#include "portal_player.h" //TODO: Move any portal mod specific code to callback functions or something
#include "physicsshadowclone.h"
#include "portal/weapon_physcannon.h"
#include "player_pickup.h"
#include "isaverestore.h"
#include "hierarchy.h"
#include "env_debughistory.h"

#else

#include "c_world.h"

#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar sv_portal_trace_vs_world;
extern ConVar sv_portal_trace_vs_displacements;
extern ConVar sv_portal_trace_vs_holywall;
extern ConVar sv_portal_trace_vs_staticprops;
extern ConVar sv_use_transformed_collideables;


//#define DEBUG_PORTAL_SIMULATION_CREATION_TIMES //define to output creation timings to developer 2
//#define DEBUG_PORTAL_COLLISION_ENVIRONMENTS //define this to allow for glview collision dumps of portal simulators

#if defined( DEBUG_PORTAL_COLLISION_ENVIRONMENTS ) || defined( DEBUG_PORTAL_SIMULATION_CREATION_TIMES )
#	if !defined( PORTAL_SIMULATORS_EMBED_GUID )
#		pragma message( __FILE__ "(" __LINE__AS_STRING ") : error custom: Portal simulators require a GUID to debug, enable the GUID in PortalSimulation.h ." )
#	endif
#endif

#if defined( DEBUG_PORTAL_COLLISION_ENVIRONMENTS )
void DumpActiveCollision( const CPortalSimulator *pPortalSimulator, const char *szFileName ); //appends to the existing file if it exists
#endif



#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
static ConVar sv_dump_portalsimulator_collision( "sv_dump_portalsimulator_collision", "0", FCVAR_REPLICATED | FCVAR_CHEAT ); //whether to actually dump out the data now that the possibility exists
static void PortalSimulatorDumps_DumpCollideToGlView( CPhysCollide *pCollide, const Vector &origin, const QAngle &angles, float fColorScale, const char *pFilename );
static void PortalSimulatorDumps_DumpBoxToGlView( const Vector &vMins, const Vector &vMaxs, float fRed, float fGreen, float fBlue, const char *pszFileName );
#endif




static inline CPolyhedron *TransformAndClipSinglePolyhedron( CPolyhedron *pExistingPolyhedron, const VMatrix &Transform, const float *pOutwardFacingClipPlanes, int iClipPlaneCount, float fCutEpsilon, bool bUseTempMemory );
static int GetEntityPhysicsObjects( IPhysicsEnvironment *pEnvironment, CBaseEntity *pEntity, IPhysicsObject **pRetList, int iRetListArraySize );

static CUtlVector<CPortalSimulator *> s_PortalSimulators;
CUtlVector<CPortalSimulator *> const &g_PortalSimulators = s_PortalSimulators;
//static CPortalSimulatorEventCallbacks s_DummyPortalSimulatorCallback;



IMPLEMENT_NETWORKCLASS_ALIASED(PSCollisionEntity, DT_PSCollisionEntity)

BEGIN_NETWORK_TABLE(CPSCollisionEntity, DT_PSCollisionEntity)
#if !defined( CLIENT_DLL )
	SendPropEHandle(SENDINFO(m_pOwningSimulator)),
#else
	RecvPropEHandle(RECVINFO(m_pOwningSimulator)),
#endif
END_NETWORK_TABLE()

LINK_ENTITY_TO_CLASS(portalsimulator_collisionentity, CPSCollisionEntity);

//static bool s_PortalSimulatorCollisionEntities[MAX_EDICTS] = { false };

//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
IMPLEMENT_NETWORKCLASS_ALIASED(PortalSimulator, DT_PortalSimulator);

BEGIN_NETWORK_TABLE(CPortalSimulator, DT_PortalSimulator)
#if !defined( CLIENT_DLL )
	SendPropEHandle(SENDINFO(pCollisionEntity)),
	SendPropEHandle(SENDINFO(m_hLinkedPortal)),
#else
	RecvPropEHandle(RECVINFO(pCollisionEntity)),
	RecvPropEHandle(RECVINFO(m_hLinkedPortal)),
#endif
END_NETWORK_TABLE()

CPortalSimulator::CPortalSimulator( void )
:	m_bGenerateCollision(true),
	m_bSharedCollisionConfiguration(false),
	m_pLinkedPortal(NULL),
	m_bInCrossLinkedFunction(false)
	//,m_pCallbacks(&s_DummyPortalSimulatorCallback)
{
	s_PortalSimulators.AddToTail( this );

#ifdef CLIENT_DLL
	m_bGenerateCollision = (GameRules() && GameRules()->IsMultiplayer());
#endif
	m_CreationChecklist.bPolyhedronsGenerated = false;
	m_CreationChecklist.bLocalCollisionGenerated = false;
	m_CreationChecklist.bLinkedCollisionGenerated = false;
	m_CreationChecklist.bLocalPhysicsGenerated = false;
	m_CreationChecklist.bLinkedPhysicsGenerated = false;



//#ifndef CLIENT_DLL
//	PS_SD_Static_World_StaticProps_ClippedProp_t::pTraceEntity = GetWorldEntity(); //will overinitialize, but it's cheap
//#else
//	PS_SD_Static_World_StaticProps_ClippedProp_t::pTraceEntity = GetClientWorldEntity();
//#endif
}

#ifdef CLIENT_DLL
bool CPortalSimulator::Init(int entnum, int iSerialNum) {
	bool ret = BaseClass::Init(entnum, iSerialNum);
	return ret;
}

void CPortalSimulator::GetToolRecordingState(KeyValues* msg) {
	BaseClass::GetToolRecordingState(msg);
	//CPortalRenderable::GetToolRecordingState(m_bActivated, msg);
}
#endif // CLIENT_DLL


#ifdef GAME_DLL
void CPortalSimulator::PostConstructor(const char* szClassname, int iForceEdictIndex) {
	BaseClass::PostConstructor(szClassname, iForceEdictIndex);
	GetEnginePortal()->AfterCollisionEntityCreated();
	pCollisionEntity = (CPSCollisionEntity*)EntityList()->CreateEntityByName("portalsimulator_collisionentity");
	Assert(pCollisionEntity != NULL);
	pCollisionEntity->m_pOwningSimulator = this;
	EntityList()->DispatchSpawn(pCollisionEntity);
}
#endif // GAME_DLL

void CPortalSimulator::Spawn(void)
{
	BaseClass::Spawn();
	GetEngineObject()->SetSolid(SOLID_CUSTOM);
	GetEngineObject()->SetMoveType(MOVETYPE_NONE);
	GetEngineObject()->SetCollisionGroup(COLLISION_GROUP_NONE);
	//s_PortalSimulatorCollisionEntities[entindex()] = true;
	//GetEngineObject()->VPhysicsSetObject(NULL);
	GetEngineObject()->AddFlag(FL_WORLDBRUSH);
	GetEngineObject()->AddEffects(EF_NOSHADOW | EF_NORECEIVESHADOW);//EF_NODRAW | 
#ifdef GAME_DLL
	GetEngineObject()->IncrementInterpolationFrame();
#endif // GAME_DLL
	GetEngineObject()->CollisionRulesChanged();
}

void CPortalSimulator::UpdateOnRemove(void)
{
	//go assert crazy here
	DetachFromLinked();
	ClearEverything();

	GetEnginePortal()->ClearHoleShapeCollideable();
#ifndef CLIENT_DLL
	GetEnginePortal()->BeforeCollisionEntityDestroy();
	GetEnginePortal()->SetActivated(false);
	if (pCollisionEntity.Get())
	{
		pCollisionEntity->m_pOwningSimulator = NULL;
		EntityList()->DestroyEntity(pCollisionEntity);
		pCollisionEntity = NULL;
	}
#endif
	BaseClass::UpdateOnRemove();
}

CPortalSimulator::~CPortalSimulator( void )
{
	for( int i = s_PortalSimulators.Count(); --i >= 0; )
	{
		if( s_PortalSimulators[i] == this )
		{
			s_PortalSimulators.FastRemove( i );
			break;
		}
	}
}

bool CPortalSimulator::ShouldCollide(int collisionGroup, int contentsMask) const
{
#ifdef GAME_DLL
	return GetWorldEntity()->ShouldCollide(collisionGroup, contentsMask);
#endif // GAME_DLL
#ifdef CLIENT_DLL
	return GetClientWorldEntity()->ShouldCollide(collisionGroup, contentsMask);
#endif // CLIENT_DLL
}

void CPortalSimulator::MoveTo( const Vector &ptCenter, const QAngle &angles )
{
#ifdef GAME_DLL
	if( (pCollisionEntity->GetEngineObject()->GetAbsOrigin() == ptCenter) && (pCollisionEntity->GetEngineObject()->GetAbsAngles() == angles) && GetEngineObject()->VPhysicsGetObject()) //not actually moving at all
		return;
#endif // GAME_DLL

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::MoveTo() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	
	pCollisionEntity->GetEngineObject()->SetAbsOrigin(ptCenter);
	pCollisionEntity->GetEngineObject()->SetAbsAngles(angles);
	
	GetEnginePortal()->BeforeMove();
	//update geometric data
	GetEnginePortal()->MoveTo(ptCenter, angles);

	//Clear();
#ifndef CLIENT_DLL
	ClearLinkedPhysics();
	ClearLocalPhysics();
#endif
	ClearLinkedCollision();
	ClearLocalCollision();
	ClearPolyhedrons();

	GetEnginePortal()->SetLocalDataIsReady(true);
	UpdateLinkMatrix();

	GetEnginePortal()->CreateHoleShapeCollideable();

	CreatePolyhedrons();	
	CreateAllCollision();
#ifndef CLIENT_DLL
	CreateAllPhysics();
#endif

	GetEnginePortal()->AfterMove();

#if defined( DEBUG_PORTAL_COLLISION_ENVIRONMENTS ) && !defined( CLIENT_DLL )
	if(   sv_dump_portalsimulator_collision.GetBool() )
	{
		const char *szFileName = "pscd.txt";
		filesystem->RemoveFile( szFileName );
		DumpActiveCollision( this, szFileName );
		if( m_pLinkedPortal )
		{
			szFileName = "pscd_linked.txt";
			filesystem->RemoveFile( szFileName );
			DumpActiveCollision( m_pLinkedPortal, szFileName );
		}
	}
#endif


	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::MoveTo() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::UpdateLinkMatrix( void )
{
	if( m_pLinkedPortal && m_pLinkedPortal->GetEnginePortal()->IsLocalDataIsReady() )
	{
		GetEnginePortal()->UpdateLinkMatrix(m_pLinkedPortal->GetEnginePortal());
	}
	else
	{
		GetEnginePortal()->UpdateLinkMatrix(NULL);
	}
	
	if( m_pLinkedPortal && (m_pLinkedPortal->m_bInCrossLinkedFunction == false) )
	{
		Assert( m_bInCrossLinkedFunction == false ); //I'm pretty sure switching to a stack would have negative repercussions
		m_bInCrossLinkedFunction = true;
		m_pLinkedPortal->UpdateLinkMatrix();
		m_bInCrossLinkedFunction = false;
	}
}

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
static ConVar sv_debug_dumpportalhole_nextcheck( "sv_debug_dumpportalhole_nextcheck", "0", FCVAR_CHEAT | FCVAR_REPLICATED );
#endif

bool CPortalSimulator::EntityIsInPortalHole( CBaseEntity *pEntity ) const
{
	if( GetEnginePortal()->IsLocalDataIsReady() == false)
		return false;

	return GetEnginePortal()->EntityIsInPortalHole(pEntity->GetEngineObject());
}

bool CPortalSimulator::EntityHitBoxExtentIsInPortalHole( CBaseAnimating *pBaseAnimating ) const
{
	if( GetEnginePortal()->IsLocalDataIsReady() == false)
		return false;

	return GetEnginePortal()->EntityHitBoxExtentIsInPortalHole(pBaseAnimating->GetEngineObject());
}

bool CPortalSimulator::RayIsInPortalHole(const Ray_t& ray) const
{
	return GetEnginePortal()->RayIsInPortalHole(ray);
}

void CPortalSimulator::ClearEverything( void )
{
	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::Clear() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();	

	OnClearEverything();
#ifndef CLIENT_DLL
	ClearAllPhysics();
#endif
	ClearAllCollision();
	ClearPolyhedrons();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::Clear() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::AttachTo( CPortalSimulator *pLinkedPortalSimulator )
{
	Assert( pLinkedPortalSimulator );

	if( pLinkedPortalSimulator == m_pLinkedPortal )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::AttachTo() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	DetachFromLinked();

	m_pLinkedPortal = pLinkedPortalSimulator;
	pLinkedPortalSimulator->m_pLinkedPortal = this;
	GetEnginePortal()->AttachTo(pLinkedPortalSimulator->GetEnginePortal());

	if( GetEnginePortal()->IsLocalDataIsReady() && m_pLinkedPortal->GetEnginePortal()->IsLocalDataIsReady())
	{
		UpdateLinkMatrix();
		CreateLinkedCollision();
#ifndef CLIENT_DLL
		CreateLinkedPhysics();
#endif
	}

#if defined( DEBUG_PORTAL_COLLISION_ENVIRONMENTS ) && !defined( CLIENT_DLL )
	if( sv_dump_portalsimulator_collision.GetBool() )
	{
		const char *szFileName = "pscd.txt";
		filesystem->RemoveFile( szFileName );
		DumpActiveCollision( this, szFileName );
		if( m_pLinkedPortal )
		{
			szFileName = "pscd_linked.txt";
			filesystem->RemoveFile( szFileName );
			DumpActiveCollision( m_pLinkedPortal, szFileName );
		}
	}
#endif

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::AttachTo() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}


#ifndef CLIENT_DLL

/*void CPortalSimulator::TeleportEntityToLinkedPortal( CBaseEntity *pEntity )
{
	//TODO: migrate teleportation code from CProp_Portal::Touch to here


}*/

void CPortalSimulator::CreateAllPhysics( void )
{
	if( IsSimulatingVPhysics() == false )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateAllPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	CreateMinimumPhysics();
	CreateLocalPhysics();
	CreateLinkedPhysics();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateAllPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::CreateMinimumPhysics( void )
{
	if( IsSimulatingVPhysics() == false )
		return;

	if(GetPhysicsEnvironment() != NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateMinimumPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	GetEnginePortal()->CreatePhysicsEnvironment();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateMinimumPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::CreateLocalPhysics( void )
{
	if( IsSimulatingVPhysics() == false )
		return;

	AssertMsg( GetEnginePortal()->IsLocalDataIsReady(), "Portal simulator attempting to create local physics before being placed.");

	if( m_CreationChecklist.bLocalPhysicsGenerated )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLocalPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	CreateMinimumPhysics();

	GetEnginePortal()->CreateLocalPhysics();

	GetEngineObject()->CollisionRulesChanged();

	GetEnginePortal()->AfterLocalPhysicsCreated();
	
	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLocalPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bLocalPhysicsGenerated = true;
}



void CPortalSimulator::CreateLinkedPhysics( void )
{
	if( IsSimulatingVPhysics() == false )
		return;

	AssertMsg( GetEnginePortal()->IsLocalDataIsReady(), "Portal simulator attempting to create linked physics before being placed itself.");

	if( (m_pLinkedPortal == NULL) || (m_pLinkedPortal->GetEnginePortal()->IsLocalDataIsReady() == false))
		return;

	if( m_CreationChecklist.bLinkedPhysicsGenerated )
		return;

	CREATEDEBUGTIMER( functionTimer );
	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLinkedPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	CreateMinimumPhysics();

	GetEnginePortal()->CreateLinkedPhysics(m_pLinkedPortal->GetEnginePortal());

	GetEngineObject()->CollisionRulesChanged();

	GetEnginePortal()->AfterLinkedPhysicsCreated();

	if( m_pLinkedPortal && (m_pLinkedPortal->m_bInCrossLinkedFunction == false) )
	{
		Assert( m_bInCrossLinkedFunction == false ); //I'm pretty sure switching to a stack would have negative repercussions
		m_bInCrossLinkedFunction = true;
		m_pLinkedPortal->CreateLinkedPhysics();
		m_bInCrossLinkedFunction = false;
	}

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLinkedPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bLinkedPhysicsGenerated = true;
}



void CPortalSimulator::ClearAllPhysics( void )
{
	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearAllPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	ClearLinkedPhysics();
	ClearLocalPhysics();
	ClearMinimumPhysics();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearAllPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::ClearMinimumPhysics( void )
{
	if(GetPhysicsEnvironment() == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearMinimumPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	GetEnginePortal()->ClearPhysicsEnvironment();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearMinimumPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::ClearLocalPhysics( void )
{
	if( m_CreationChecklist.bLocalPhysicsGenerated == false )
		return;

	if(GetPhysicsEnvironment() == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLocalPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	GetPhysicsEnvironment()->CleanupDeleteList();
	GetPhysicsEnvironment()->SetQuickDelete( true ); //if we don't do this, things crash the next time we cleanup the delete list while checking mindists

	GetEnginePortal()->BeforeLocalPhysicsClear();

	GetEnginePortal()->ClearLocalPhysics();

	GetPhysicsEnvironment()->CleanupDeleteList();
	GetPhysicsEnvironment()->SetQuickDelete( false );

	GetEngineObject()->CollisionRulesChanged();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLocalPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bLocalPhysicsGenerated = false;
}



void CPortalSimulator::ClearLinkedPhysics( void )
{
	if( m_CreationChecklist.bLinkedPhysicsGenerated == false )
		return;

	if(GetPhysicsEnvironment() == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLinkedPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	GetPhysicsEnvironment()->CleanupDeleteList();
	GetPhysicsEnvironment()->SetQuickDelete( true ); //if we don't do this, things crash the next time we cleanup the delete list while checking mindists

	GetEnginePortal()->BeforeLinkedPhysicsClear();

	GetEnginePortal()->ClearLinkedPhysics();

	if( m_pLinkedPortal && (m_pLinkedPortal->m_bInCrossLinkedFunction == false) )
	{
		Assert( m_bInCrossLinkedFunction == false ); //I'm pretty sure switching to a stack would have negative repercussions
		m_bInCrossLinkedFunction = true;
		m_pLinkedPortal->ClearLinkedPhysics();
		m_bInCrossLinkedFunction = false;
	}

	//Assert( (m_ShadowClones.FromLinkedPortal.Count() == 0) && 
	//	((m_pLinkedPortal == NULL) || (m_pLinkedPortal->m_ShadowClones.FromLinkedPortal.Count() == 0)) );

	GetPhysicsEnvironment()->CleanupDeleteList();
	GetPhysicsEnvironment()->SetQuickDelete( false );

	GetEngineObject()->CollisionRulesChanged();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLinkedPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bLinkedPhysicsGenerated = false;
}
#endif //#ifndef CLIENT_DLL


void CPortalSimulator::CreateAllCollision( void )
{
	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateAllCollision() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	CreateLocalCollision();
	CreateLinkedCollision();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateAllCollision() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::CreateLocalCollision( void )
{
	AssertMsg( GetEnginePortal()->IsLocalDataIsReady(), "Portal simulator attempting to create local collision before being placed.");

	if( m_CreationChecklist.bLocalCollisionGenerated )
		return;

	if( IsCollisionGenerationEnabled() == false )
		return;

	DEBUGTIMERONLY( s_iPortalSimulatorGUID = GetPortalSimulatorGUID() );

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLocalCollision() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	GetEnginePortal()->CreateLocalCollision();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLocalCollision() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bLocalCollisionGenerated = true;
}



void CPortalSimulator::CreateLinkedCollision( void )
{
	if( m_CreationChecklist.bLinkedCollisionGenerated )
		return;

	if( IsCollisionGenerationEnabled() == false )
		return;

	//nothing to do for now, the current set of collision is just transformed from the linked simulator when needed. It's really cheap to transform in traces and physics generation.
	
	m_CreationChecklist.bLinkedCollisionGenerated = true;
}



void CPortalSimulator::ClearAllCollision( void )
{
	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearAllCollision() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	ClearLinkedCollision();
	ClearLocalCollision();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearAllCollision() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::ClearLinkedCollision( void )
{
	if( m_CreationChecklist.bLinkedCollisionGenerated == false )
		return;

	//nothing to do for now, the current set of collision is just transformed from the linked simulator when needed. It's really cheap to transform in traces and physics generation.
	
	m_CreationChecklist.bLinkedCollisionGenerated = false;
}



void CPortalSimulator::ClearLocalCollision( void )
{
	if( m_CreationChecklist.bLocalCollisionGenerated == false )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLocalCollision() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	GetEnginePortal()->ClearLocalCollision();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLocalCollision() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bLocalCollisionGenerated = false;
}



void CPortalSimulator::CreatePolyhedrons( void )
{
	if( m_CreationChecklist.bPolyhedronsGenerated )
		return;

	if( IsCollisionGenerationEnabled() == false )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreatePolyhedrons() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	GetEnginePortal()->CreatePolyhedrons();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreatePolyhedrons() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bPolyhedronsGenerated = true;
}



void CPortalSimulator::ClearPolyhedrons( void )
{
	if( m_CreationChecklist.bPolyhedronsGenerated == false )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearPolyhedrons() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	GetEnginePortal()->ClearPolyhedrons();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearPolyhedrons() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bPolyhedronsGenerated = false;
}



void CPortalSimulator::DetachFromLinked( void )
{
	if( m_pLinkedPortal == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::DetachFromLinked() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	BeforeDetachFromLinked();
	//IMPORTANT: Physics objects must be destroyed before their associated collision data or a fairly cryptic crash will ensue
#ifndef CLIENT_DLL
	ClearLinkedPhysics();
#endif
	ClearLinkedCollision();

	if( m_pLinkedPortal->m_bInCrossLinkedFunction == false )
	{
		Assert( m_bInCrossLinkedFunction == false ); //I'm pretty sure switching to a stack would have negative repercussions
		m_bInCrossLinkedFunction = true;
		m_pLinkedPortal->DetachFromLinked();
		m_bInCrossLinkedFunction = false;
	}

	m_pLinkedPortal = NULL;
	GetEnginePortal()->DetachFromLinked();

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::DetachFromLinked() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}

//void CPortalSimulator::SetPortalSimulatorCallbacks( CPortalSimulatorEventCallbacks *pCallbacks )
//{
//	if( pCallbacks )
//		m_pCallbacks = pCallbacks;
//	else
//		m_pCallbacks = &s_DummyPortalSimulatorCallback; //always keep the pointer valid
//}




void CPortalSimulator::SetVPhysicsSimulationEnabled( bool bEnabled )
{
	AssertMsg( (m_pLinkedPortal == NULL) || (m_pLinkedPortal->GetEnginePortal()->IsSimulatingVPhysics() == GetEnginePortal()->IsSimulatingVPhysics()), "Linked portals are in disagreement as to whether they would simulate VPhysics.");

	if( bEnabled == GetEnginePortal()->IsSimulatingVPhysics() )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::SetVPhysicsSimulationEnabled() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	GetEnginePortal()->SetVPhysicsSimulationEnabled(bEnabled);
	if( bEnabled )
	{
		//we took some local collision shortcuts when generating while physics simulation is off, regenerate
		ClearLocalCollision();
		ClearPolyhedrons();
		CreatePolyhedrons();
		CreateLocalCollision();
#ifndef CLIENT_DLL
		CreateAllPhysics();
#endif
	}
#ifndef CLIENT_DLL
	else
	{
		ClearAllPhysics();
	}
#endif

	if( m_pLinkedPortal && (m_pLinkedPortal->m_bInCrossLinkedFunction == false) )
	{
		Assert( m_bInCrossLinkedFunction == false ); //I'm pretty sure switching to a stack would have negative repercussions
		m_bInCrossLinkedFunction = true;
		m_pLinkedPortal->SetVPhysicsSimulationEnabled( bEnabled );
		m_bInCrossLinkedFunction = false;
	}

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::SetVPhysicsSimulationEnabled() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}

#ifndef CLIENT_DLL

bool CPortalSimulator::CreatedPhysicsObject( const IPhysicsObject *pObject, PS_PhysicsObjectSourceType_t *pOut_SourceType ) const
{
	return GetEnginePortal()->CreatedPhysicsObject(pObject, pOut_SourceType);
}


#endif //#ifndef CLIENT_DLL















static inline CPolyhedron *TransformAndClipSinglePolyhedron( CPolyhedron *pExistingPolyhedron, const VMatrix &Transform, const float *pOutwardFacingClipPlanes, int iClipPlaneCount, float fCutEpsilon, bool bUseTempMemory )
{
	Vector *pTempPointArray = (Vector *)stackalloc( sizeof( Vector ) * pExistingPolyhedron->iVertexCount );
	Polyhedron_IndexedPolygon_t *pTempPolygonArray = (Polyhedron_IndexedPolygon_t *)stackalloc( sizeof( Polyhedron_IndexedPolygon_t ) * pExistingPolyhedron->iPolygonCount );

	Polyhedron_IndexedPolygon_t *pOriginalPolygons = pExistingPolyhedron->pPolygons;
	pExistingPolyhedron->pPolygons = pTempPolygonArray;

	Vector *pOriginalPoints = pExistingPolyhedron->pVertices;
	pExistingPolyhedron->pVertices = pTempPointArray;

	for( int j = 0; j != pExistingPolyhedron->iPolygonCount; ++j )
	{
		pTempPolygonArray[j].iFirstIndex = pOriginalPolygons[j].iFirstIndex;
		pTempPolygonArray[j].iIndexCount = pOriginalPolygons[j].iIndexCount;
		pTempPolygonArray[j].polyNormal = Transform.ApplyRotation( pOriginalPolygons[j].polyNormal );
	}

	for( int j = 0; j != pExistingPolyhedron->iVertexCount; ++j )
	{
		pTempPointArray[j] = Transform * pOriginalPoints[j];
	}

	CPolyhedron *pNewPolyhedron = ClipPolyhedron( pExistingPolyhedron, pOutwardFacingClipPlanes, iClipPlaneCount, fCutEpsilon, bUseTempMemory ); //copy the polyhedron

	//restore the original polyhedron to its former self
	pExistingPolyhedron->pVertices = pOriginalPoints;
	pExistingPolyhedron->pPolygons = pOriginalPolygons;

	return pNewPolyhedron;
}

static int GetEntityPhysicsObjects( IPhysicsEnvironment *pEnvironment, CBaseEntity *pEntity, IPhysicsObject **pRetList, int iRetListArraySize )
{	
	int iCount, iRetCount = 0;
	const IPhysicsObject **pList = pEnvironment->GetObjectList( &iCount );

	if( iCount > iRetListArraySize )
		iCount = iRetListArraySize;

	for ( int i = 0; i < iCount; ++i )
	{
		CBaseEntity *pEnvEntity = reinterpret_cast<CBaseEntity *>(pList[i]->GetGameData());
		if ( pEntity == pEnvEntity )
		{
			pRetList[iRetCount] = (IPhysicsObject *)(pList[i]);
			++iRetCount;
		}
	}

	return iRetCount;
}

#ifndef CLIENT_DLL
	class CPS_AutoGameSys_EntityListener : public CAutoGameSystem//, public IEntityListener<IServerEntity>
#else
	class CPS_AutoGameSys_EntityListener : public CAutoGameSystem
#endif
{
public:
	virtual void LevelInitPreEntity( void )
	{
		for( int i = s_PortalSimulators.Count(); --i >= 0; )
			s_PortalSimulators[i]->ClearEverything();
	}

	virtual void LevelShutdownPreEntity( void )
	{
		for( int i = s_PortalSimulators.Count(); --i >= 0; )
			s_PortalSimulators[i]->ClearEverything();
	}

//#ifndef CLIENT_DLL
//	virtual bool Init( void )
//	{
//		EntityList()->AddListenerEntity( this );
//		return true;
//	}
//
//	//virtual void OnEntityCreated( IServerEntity *pEntity ) {}
//	virtual void OnEntitySpawned( IServerEntity *pEntity )
//	{
//
//	}
//	virtual void OnEntityDeleted( IServerEntity *pEntity )
//	{
//		CPortalSimulator *pSimulator = CPortalSimulator::GetPortalThatOwnsEntity( pEntity );
//		if( pSimulator )
//		{
//			pSimulator->ReleasePhysicsOwnership( pEntity, false );
//			pSimulator->ReleaseOwnershipOfEntity( pEntity );
//		}
//		Assert( CPortalSimulator::GetPortalThatOwnsEntity( pEntity ) == NULL );
//	}
//#endif //#ifndef CLIENT_DLL
};
static CPS_AutoGameSys_EntityListener s_CPS_AGS_EL_Singleton;

CPSCollisionEntity::CPSCollisionEntity( void )
{
	m_pOwningSimulator = NULL;
	//create the collision shape.... TODO: consider having one shared collideable between all portals
	float fPlanes[6 * 4];
	fPlanes[(0 * 4) + 0] = 1.0f;
	fPlanes[(0 * 4) + 1] = 0.0f;
	fPlanes[(0 * 4) + 2] = 0.0f;
	fPlanes[(0 * 4) + 3] = vPortalLocalMaxs.x;

	fPlanes[(1 * 4) + 0] = -1.0f;
	fPlanes[(1 * 4) + 1] = 0.0f;
	fPlanes[(1 * 4) + 2] = 0.0f;
	fPlanes[(1 * 4) + 3] = -vPortalLocalMins.x;

	fPlanes[(2 * 4) + 0] = 0.0f;
	fPlanes[(2 * 4) + 1] = 1.0f;
	fPlanes[(2 * 4) + 2] = 0.0f;
	fPlanes[(2 * 4) + 3] = vPortalLocalMaxs.y;

	fPlanes[(3 * 4) + 0] = 0.0f;
	fPlanes[(3 * 4) + 1] = -1.0f;
	fPlanes[(3 * 4) + 2] = 0.0f;
	fPlanes[(3 * 4) + 3] = -vPortalLocalMins.y;

	fPlanes[(4 * 4) + 0] = 0.0f;
	fPlanes[(4 * 4) + 1] = 0.0f;
	fPlanes[(4 * 4) + 2] = 1.0f;
	fPlanes[(4 * 4) + 3] = vPortalLocalMaxs.z;

	fPlanes[(5 * 4) + 0] = 0.0f;
	fPlanes[(5 * 4) + 1] = 0.0f;
	fPlanes[(5 * 4) + 2] = -1.0f;
	fPlanes[(5 * 4) + 3] = -vPortalLocalMins.z;

	CPolyhedron* pPolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, 0.00001f, true);
	Assert(pPolyhedron != NULL);
	CPhysConvex* pConvex = EntityList()->PhysGetCollision()->ConvexFromConvexPolyhedron(*pPolyhedron);
	pPolyhedron->Release();
	Assert(pConvex != NULL);
	m_pCollisionShape = EntityList()->PhysGetCollision()->ConvertConvexToCollide(&pConvex, 1);
}

CPSCollisionEntity::~CPSCollisionEntity( void )
{
#ifdef GAME_DLL
	if( m_pOwningSimulator )
	{
		Error("m_pOwningSimulator must be NULL");
		//m_pOwningSimulator->m_InternalData.Simulation.Dynamic.m_EntFlags[entindex()] &= ~PSEF_OWNS_PHYSICS;
		//m_pOwningSimulator->MarkAsReleased( this );
		//m_pOwningSimulator->m_InternalData.Simulation.pCollisionEntity = NULL;
		//m_pOwningSimulator = NULL;
	}
#endif // GAME_DLL
	//s_PortalSimulatorCollisionEntities[entindex()] = false;
}


void CPSCollisionEntity::UpdateOnRemove( void )
{
#ifdef GAME_DLL
	if (m_pOwningSimulator) {
		EntityList()->DestroyEntity(m_pOwningSimulator);
		m_pOwningSimulator = NULL;
	}
	//s_PortalSimulatorCollisionEntities[entindex()] = false;
#endif // GAME_DLL
	BaseClass::UpdateOnRemove();
}

void CPSCollisionEntity::Spawn( void )
{
	BaseClass::Spawn();
	GetEngineObject()->AddEffects(EF_NODRAW | EF_NORECEIVESHADOW | EF_NOSHADOW);
	GetEngineObject()->SetMoveType(MOVETYPE_NONE);
	GetEngineObject()->SetCollisionGroup(COLLISION_GROUP_PLAYER);
	GetEngineObject()->SetSize(vPortalLocalMins, vPortalLocalMaxs);
	GetEngineObject()->SetSolid(SOLID_OBB);
	GetEngineObject()->SetSolidFlags(FSOLID_TRIGGER | FSOLID_NOT_SOLID | FSOLID_CUSTOMBOXTEST | FSOLID_CUSTOMRAYTEST);
}

void CPSCollisionEntity::Activate( void )
{
	BaseClass::Activate();
	GetEngineObject()->SetSize(vPortalLocalMins, vPortalLocalMaxs);
	GetEngineObject()->SetSolid(SOLID_OBB);
	GetEngineObject()->SetSolidFlags(FSOLID_TRIGGER | FSOLID_NOT_SOLID | FSOLID_CUSTOMBOXTEST | FSOLID_CUSTOMRAYTEST);
}

int CPSCollisionEntity::ObjectCaps( void )
{
	return ((BaseClass::ObjectCaps() | FCAP_DONT_SAVE) & ~(FCAP_FORCE_TRANSITION | FCAP_ACROSS_TRANSITION | FCAP_MUST_SPAWN | FCAP_SAVE_NON_NETWORKABLE));
}

bool CPSCollisionEntity::TestCollision(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr)
{
	EntityList()->PhysGetCollision()->TraceBox(ray, MASK_ALL, NULL, m_pCollisionShape, GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), &tr);
	return tr.DidHit();
}

#ifdef GAME_DLL
void CPSCollisionEntity::StartTouch(IServerEntity* pOther)
{
	BaseClass::StartTouch(pOther);
	// Since prop_portal is a trigger it doesn't send back start touch, so I'm forcing it
	pOther->StartTouch(this);
	if (m_pOwningSimulator) {
		m_pOwningSimulator->StartTouch(pOther);
	}
}

void CPSCollisionEntity::Touch(IServerEntity* pOther)
{
	BaseClass::Touch(pOther);
	pOther->Touch(this);
	if (m_pOwningSimulator) {
		m_pOwningSimulator->Touch(pOther);
	}
}

void CPSCollisionEntity::EndTouch(IServerEntity* pOther)
{
	BaseClass::EndTouch(pOther);
	// Since prop_portal is a trigger it doesn't send back end touch, so I'm forcing it
	pOther->EndTouch(this);
	if (m_pOwningSimulator) {
		m_pOwningSimulator->EndTouch(pOther);
	}
}
#endif // GAME_DLL

//const Vector& CPortalSimulator::GetOrigin() const { return GetEnginePortal()->GetOrigin(); }
//const QAngle& CPortalSimulator::GetAngles() const { return GetEnginePortal()->GetAngles(); }
const VMatrix& CPortalSimulator::MatrixThisToLinked() const { return GetEnginePortal()->MatrixThisToLinked(); }
const VMatrix& CPortalSimulator::MatrixLinkedToThis() const { return GetEnginePortal()->MatrixLinkedToThis(); }
const cplane_t& CPortalSimulator::GetPortalPlane() const { return GetEnginePortal()->GetPortalPlane(); }
//const PS_InternalData_t& CPortalSimulator::GetDataAccess() const { return GetEnginePortal()->GetDataAccess(); }
const Vector& CPortalSimulator::GetVectorForward() const { return GetEnginePortal()->GetVectorForward(); }
const Vector& CPortalSimulator::GetVectorUp() const { return GetEnginePortal()->GetVectorUp(); }
const Vector& CPortalSimulator::GetVectorRight() const { return GetEnginePortal()->GetVectorRight(); }
const PS_SD_Static_SurfaceProperties_t& CPortalSimulator::GetSurfaceProperties() const { return GetEnginePortal()->GetSurfaceProperties(); }
IPhysicsEnvironment* CPortalSimulator::GetPhysicsEnvironment() { return GetEnginePortal()->GetPhysicsEnvironment(); }

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS

#include "filesystem.h"

static void PortalSimulatorDumps_DumpCollideToGlView( CPhysCollide *pCollide, const Vector &origin, const QAngle &angles, float fColorScale, const char *pFilename );
static void PortalSimulatorDumps_DumpPlanesToGlView( float *pPlanes, int iPlaneCount, const char *pszFileName );
static void PortalSimulatorDumps_DumpBoxToGlView( const Vector &vMins, const Vector &vMaxs, float fRed, float fGreen, float fBlue, const char *pszFileName );
static void PortalSimulatorDumps_DumpOBBoxToGlView( const Vector &ptOrigin, const Vector &vExtent1, const Vector &vExtent2, const Vector &vExtent3, float fRed, float fGreen, float fBlue, const char *pszFileName );

void DumpActiveCollision( const CPortalSimulator *pPortalSimulator, const char *szFileName )
{
	CREATEDEBUGTIMER( collisionDumpTimer );
	STARTDEBUGTIMER( collisionDumpTimer );
	
	//color coding scheme, static prop collision is brighter than brush collision. Remote world stuff transformed to the local wall is darker than completely local stuff
#define PSDAC_INTENSITY_LOCALBRUSH 0.25f
#define PSDAC_INTENSITY_LOCALPROP 1.0f
#define PSDAC_INTENSITY_REMOTEBRUSH 0.125f
#define PSDAC_INTENSITY_REMOTEPROP 0.5f

	if( pPortalSimulator->m_DataAccess.Simulation.Static.World.Brushes.pCollideable )
		PortalSimulatorDumps_DumpCollideToGlView( pPortalSimulator->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, vec3_origin, vec3_angle, PSDAC_INTENSITY_LOCALBRUSH, szFileName );
	
	if( pPortalSimulator->m_DataAccess.Simulation.Static.World.StaticProps.bCollisionExists )
	{
		for( int i = pPortalSimulator->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			Assert( pPortalSimulator->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pCollide );
			PortalSimulatorDumps_DumpCollideToGlView( pPortalSimulator->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pCollide, vec3_origin, vec3_angle, PSDAC_INTENSITY_LOCALPROP, szFileName );	
		}
	}

	if( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable )
		PortalSimulatorDumps_DumpCollideToGlView( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable, vec3_origin, vec3_angle, PSDAC_INTENSITY_LOCALBRUSH, szFileName );

	if( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable )
		PortalSimulatorDumps_DumpCollideToGlView( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable, vec3_origin, vec3_angle, PSDAC_INTENSITY_LOCALBRUSH, szFileName );

	//if( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pCollideable )
	//	PortalSimulatorDumps_DumpCollideToGlView( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pCollideable, vec3_origin, vec3_angle, PSDAC_INTENSITY_REMOTEBRUSH, szFileName );
	CPortalSimulator *pLinkedPortal = pPortalSimulator->GetLinkedPortalSimulator();
	if( pLinkedPortal )
	{
		if( pLinkedPortal->m_DataAccess.Simulation.Static.World.Brushes.pCollideable )
			PortalSimulatorDumps_DumpCollideToGlView( pLinkedPortal->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, PSDAC_INTENSITY_REMOTEBRUSH, szFileName );

		//for( int i = pPortalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.Collideables.Count(); --i >= 0; )
		//	PortalSimulatorDumps_DumpCollideToGlView( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.Collideables[i], vec3_origin, vec3_angle, PSDAC_INTENSITY_REMOTEPROP, szFileName );	
		if( pLinkedPortal->m_DataAccess.Simulation.Static.World.StaticProps.bCollisionExists )
		{
			for( int i = pLinkedPortal->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
			{
				Assert( pLinkedPortal->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pCollide );
				PortalSimulatorDumps_DumpCollideToGlView( pLinkedPortal->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pCollide, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, PSDAC_INTENSITY_REMOTEPROP, szFileName );	
			}
		}
	}

	STOPDEBUGTIMER( collisionDumpTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::DumpActiveCollision() Spent %fms generating a collision dump\n", pPortalSimulator->GetPortalSimulatorGUID(), TABSPACING, collisionDumpTimer.GetDuration().GetMillisecondsF() ); );
}

static void PortalSimulatorDumps_DumpCollideToGlView( CPhysCollide *pCollide, const Vector &origin, const QAngle &angles, float fColorScale, const char *pFilename )
{
	if ( !pCollide )
		return;

	printf("Writing %s...\n", pFilename );
	Vector *outVerts;
	int vertCount = EntityList()->PhysGetCollision()->CreateDebugMesh( pCollide, &outVerts );
	FileHandle_t fp = filesystem->Open( pFilename, "ab" );
	int triCount = vertCount / 3;
	int vert = 0;
	VMatrix tmp = SetupMatrixOrgAngles( origin, angles );
	int i;
	for ( i = 0; i < vertCount; i++ )
	{
		outVerts[i] = tmp.VMul4x3( outVerts[i] );
	}

	for ( i = 0; i < triCount; i++ )
	{
		filesystem->FPrintf( fp, "3\n" );
		filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f 0 0\n", outVerts[vert].x, outVerts[vert].y, outVerts[vert].z, fColorScale );
		vert++;
		filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f 0 %.2f 0\n", outVerts[vert].x, outVerts[vert].y, outVerts[vert].z, fColorScale );
		vert++;
		filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f 0 0 %.2f\n", outVerts[vert].x, outVerts[vert].y, outVerts[vert].z, fColorScale );
		vert++;
	}
	filesystem->Close( fp );
	EntityList()->PhysGetCollision()->DestroyDebugMesh( vertCount, outVerts );
}

static void PortalSimulatorDumps_DumpPlanesToGlView( float *pPlanes, int iPlaneCount, const char *pszFileName )
{
	FileHandle_t fp = filesystem->Open( pszFileName, "wb" );

	for( int i = 0; i < iPlaneCount; ++i )
	{
		Vector vPlaneVerts[4];

		float fRed, fGreen, fBlue;
		fRed = rand()/32768.0f;
		fGreen = rand()/32768.0f;
		fBlue = rand()/32768.0f;

		PolyFromPlane( vPlaneVerts, *(Vector *)(pPlanes + (i*4)), pPlanes[(i*4) + 3], 1000.0f );

		filesystem->FPrintf( fp, "4\n" );

		filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vPlaneVerts[3].x, vPlaneVerts[3].y, vPlaneVerts[3].z, fRed, fGreen, fBlue );
		filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vPlaneVerts[2].x, vPlaneVerts[2].y, vPlaneVerts[2].z, fRed, fGreen, fBlue );
		filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vPlaneVerts[1].x, vPlaneVerts[1].y, vPlaneVerts[1].z, fRed, fGreen, fBlue );
		filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vPlaneVerts[0].x, vPlaneVerts[0].y, vPlaneVerts[0].z, fRed, fGreen, fBlue );
	}

	filesystem->Close( fp );
}


static void PortalSimulatorDumps_DumpBoxToGlView( const Vector &vMins, const Vector &vMaxs, float fRed, float fGreen, float fBlue, const char *pszFileName )
{
	FileHandle_t fp = filesystem->Open( pszFileName, "ab" );

	//x min side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMins.z, fRed, fGreen, fBlue );

	//x max side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMins.z, fRed, fGreen, fBlue );


	//y min side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMins.z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMins.z, fRed, fGreen, fBlue );



	//y max side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );



	//z min side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMins.z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMins.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMins.z, fRed, fGreen, fBlue );



	//z max side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMaxs.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMaxs.y, vMaxs.z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", vMins.x, vMins.y, vMaxs.z, fRed, fGreen, fBlue );

	filesystem->Close( fp );
}

static void PortalSimulatorDumps_DumpOBBoxToGlView( const Vector &ptOrigin, const Vector &vExtent1, const Vector &vExtent2, const Vector &vExtent3, float fRed, float fGreen, float fBlue, const char *pszFileName )
{
	FileHandle_t fp = filesystem->Open( pszFileName, "ab" );

	Vector ptExtents[8];
	int counter;
	for( counter = 0; counter != 8; ++counter )
	{
		ptExtents[counter] = ptOrigin;
		if( counter & (1<<0) ) ptExtents[counter] += vExtent1;
		if( counter & (1<<1) ) ptExtents[counter] += vExtent2;
		if( counter & (1<<2) ) ptExtents[counter] += vExtent3;
	}

	//x min side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[0].x, ptExtents[0].y, ptExtents[0].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[4].x, ptExtents[4].y, ptExtents[4].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[6].x, ptExtents[6].y, ptExtents[6].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[2].x, ptExtents[2].y, ptExtents[2].z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[2].x, ptExtents[2].y, ptExtents[2].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[6].x, ptExtents[6].y, ptExtents[6].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[4].x, ptExtents[4].y, ptExtents[4].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[0].x, ptExtents[0].y, ptExtents[0].z, fRed, fGreen, fBlue );

	//x max side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[1].x, ptExtents[1].y, ptExtents[1].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[5].x, ptExtents[5].y, ptExtents[5].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[7].x, ptExtents[7].y, ptExtents[7].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[3].x, ptExtents[3].y, ptExtents[3].z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[3].x, ptExtents[3].y, ptExtents[3].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[7].x, ptExtents[7].y, ptExtents[7].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[5].x, ptExtents[5].y, ptExtents[5].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[1].x, ptExtents[1].y, ptExtents[1].z, fRed, fGreen, fBlue );


	//y min side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[0].x, ptExtents[0].y, ptExtents[0].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[4].x, ptExtents[4].y, ptExtents[4].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[5].x, ptExtents[5].y, ptExtents[5].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[1].x, ptExtents[1].y, ptExtents[1].z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[1].x, ptExtents[1].y, ptExtents[1].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[5].x, ptExtents[5].y, ptExtents[5].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[4].x, ptExtents[4].y, ptExtents[4].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[0].x, ptExtents[0].y, ptExtents[0].z, fRed, fGreen, fBlue );



	//y max side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[2].x, ptExtents[2].y, ptExtents[2].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[6].x, ptExtents[6].y, ptExtents[6].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[7].x, ptExtents[7].y, ptExtents[7].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[3].x, ptExtents[3].y, ptExtents[3].z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[3].x, ptExtents[3].y, ptExtents[3].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[7].x, ptExtents[7].y, ptExtents[7].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[6].x, ptExtents[6].y, ptExtents[6].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[2].x, ptExtents[2].y, ptExtents[2].z, fRed, fGreen, fBlue );



	//z min side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[0].x, ptExtents[0].y, ptExtents[0].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[2].x, ptExtents[2].y, ptExtents[2].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[3].x, ptExtents[3].y, ptExtents[3].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[1].x, ptExtents[1].y, ptExtents[1].z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[1].x, ptExtents[1].y, ptExtents[1].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[3].x, ptExtents[3].y, ptExtents[3].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[2].x, ptExtents[2].y, ptExtents[2].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[0].x, ptExtents[0].y, ptExtents[0].z, fRed, fGreen, fBlue );



	//z max side
	filesystem->FPrintf( fp, "4\n" );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[4].x, ptExtents[4].y, ptExtents[4].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[6].x, ptExtents[6].y, ptExtents[6].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[7].x, ptExtents[7].y, ptExtents[7].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[5].x, ptExtents[5].y, ptExtents[5].z, fRed, fGreen, fBlue );

	filesystem->FPrintf( fp, "4\n" );	
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[5].x, ptExtents[5].y, ptExtents[5].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[7].x, ptExtents[7].y, ptExtents[7].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[6].x, ptExtents[6].y, ptExtents[6].z, fRed, fGreen, fBlue );
	filesystem->FPrintf( fp, "%6.3f %6.3f %6.3f %.2f %.2f %.2f\n", ptExtents[4].x, ptExtents[4].y, ptExtents[4].z, fRed, fGreen, fBlue );

	filesystem->Close( fp );
}



#endif














