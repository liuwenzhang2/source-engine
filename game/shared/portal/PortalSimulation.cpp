//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=====================================================================================//


#include "cbase.h"
#include "PortalSimulation.h"
#include "vphysics_interface.h"
#include "physics.h"
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

CCallQueue *GetPortalCallQueue();

extern IPhysicsConstraintEvent *g_pConstraintEvents;

static ConVar sv_portal_collision_sim_bounds_x( "sv_portal_collision_sim_bounds_x", "200", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!" );
static ConVar sv_portal_collision_sim_bounds_y( "sv_portal_collision_sim_bounds_y", "200", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!" );
static ConVar sv_portal_collision_sim_bounds_z( "sv_portal_collision_sim_bounds_z", "252", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!" );
ConVar sv_portal_trace_vs_world("sv_portal_trace_vs_world", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment world geometry");
ConVar sv_portal_trace_vs_displacements("sv_portal_trace_vs_displacements", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment displacement geometry");
ConVar sv_portal_trace_vs_holywall("sv_portal_trace_vs_holywall", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment carved wall");
ConVar sv_portal_trace_vs_staticprops("sv_portal_trace_vs_staticprops", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment static prop geometry");
ConVar sv_use_transformed_collideables("sv_use_transformed_collideables", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Disables traces against remote portal moving entities using transforms to bring them into local space.");
class CTransformedCollideable : public ICollideable //wraps an existing collideable, but transforms everything that pertains to world space by another transform
{
public:
	VMatrix m_matTransform; //the transformation we apply to the wrapped collideable
	VMatrix m_matInvTransform; //cached inverse of m_matTransform

	ICollideable* m_pWrappedCollideable; //the collideable we're transforming without it knowing

	struct CTC_ReferenceVars_t
	{
		Vector m_vCollisionOrigin;
		QAngle m_qCollisionAngles;
		matrix3x4_t m_matCollisionToWorldTransform;
		matrix3x4_t m_matRootParentToWorldTransform;
	};

	mutable CTC_ReferenceVars_t m_ReferencedVars; //when returning a const reference, it needs to point to something, so here we go

	//abstract functions which require no transforms, just pass them along to the wrapped collideable
	virtual IHandleEntity* GetEntityHandle() { return m_pWrappedCollideable->GetEntityHandle(); }
	virtual const Vector& OBBMinsPreScaled() const { return m_pWrappedCollideable->OBBMinsPreScaled(); }
	virtual const Vector& OBBMaxsPreScaled() const { return m_pWrappedCollideable->OBBMaxsPreScaled(); }
	virtual const Vector& OBBMins() const { return m_pWrappedCollideable->OBBMins(); }
	virtual const Vector& OBBMaxs() const { return m_pWrappedCollideable->OBBMaxs(); }
	virtual int				GetCollisionModelIndex() { return m_pWrappedCollideable->GetCollisionModelIndex(); }
	virtual const model_t* GetCollisionModel() { return m_pWrappedCollideable->GetCollisionModel(); }
	virtual SolidType_t		GetSolid() const { return m_pWrappedCollideable->GetSolid(); }
	virtual int				GetSolidFlags() const { return m_pWrappedCollideable->GetSolidFlags(); }
	//virtual IClientUnknown*	GetIClientUnknown() { return m_pWrappedCollideable->GetIClientUnknown(); }
	virtual int				GetCollisionGroup() const { return m_pWrappedCollideable->GetCollisionGroup(); }
	virtual bool			ShouldTouchTrigger(int triggerSolidFlags) const { return m_pWrappedCollideable->ShouldTouchTrigger(triggerSolidFlags); }

	//slightly trickier functions
	virtual void			WorldSpaceTriggerBounds(Vector* pVecWorldMins, Vector* pVecWorldMaxs) const;
	virtual bool			TestCollision(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr);
	virtual bool			TestHitboxes(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr);
	virtual const Vector& GetCollisionOrigin() const;
	virtual const QAngle& GetCollisionAngles() const;
	virtual const matrix3x4_t& CollisionToWorldTransform() const;
	virtual void			WorldSpaceSurroundingBounds(Vector* pVecMins, Vector* pVecMaxs);
	virtual const matrix3x4_t* GetRootParentToWorldTransform() const;
};

void CTransformedCollideable::WorldSpaceTriggerBounds(Vector* pVecWorldMins, Vector* pVecWorldMaxs) const
{
	m_pWrappedCollideable->WorldSpaceTriggerBounds(pVecWorldMins, pVecWorldMaxs);

	if (pVecWorldMins)
		*pVecWorldMins = m_matTransform * (*pVecWorldMins);

	if (pVecWorldMaxs)
		*pVecWorldMaxs = m_matTransform * (*pVecWorldMaxs);
}

bool CTransformedCollideable::TestCollision(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr)
{
	//TODO: Transform the ray by inverse matTransform and transform the trace results by matTransform? AABB Errors arise by transforming the ray.
	return m_pWrappedCollideable->TestCollision(ray, fContentsMask, tr);
}

bool CTransformedCollideable::TestHitboxes(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr)
{
	//TODO: Transform the ray by inverse matTransform and transform the trace results by matTransform? AABB Errors arise by transforming the ray.
	return m_pWrappedCollideable->TestHitboxes(ray, fContentsMask, tr);
}

const Vector& CTransformedCollideable::GetCollisionOrigin() const
{
	m_ReferencedVars.m_vCollisionOrigin = m_matTransform * m_pWrappedCollideable->GetCollisionOrigin();
	return m_ReferencedVars.m_vCollisionOrigin;
}

const QAngle& CTransformedCollideable::GetCollisionAngles() const
{
	m_ReferencedVars.m_qCollisionAngles = TransformAnglesToWorldSpace(m_pWrappedCollideable->GetCollisionAngles(), m_matTransform.As3x4());
	return m_ReferencedVars.m_qCollisionAngles;
}

const matrix3x4_t& CTransformedCollideable::CollisionToWorldTransform() const
{
	//1-2 order correct?
	ConcatTransforms(m_matTransform.As3x4(), m_pWrappedCollideable->CollisionToWorldTransform(), m_ReferencedVars.m_matCollisionToWorldTransform);
	return m_ReferencedVars.m_matCollisionToWorldTransform;
}

void CTransformedCollideable::WorldSpaceSurroundingBounds(Vector* pVecMins, Vector* pVecMaxs)
{
	if ((pVecMins == NULL) && (pVecMaxs == NULL))
		return;

	Vector vMins, vMaxs;
	m_pWrappedCollideable->WorldSpaceSurroundingBounds(&vMins, &vMaxs);

	TransformAABB(m_matTransform.As3x4(), vMins, vMaxs, vMins, vMaxs);

	if (pVecMins)
		*pVecMins = vMins;
	if (pVecMaxs)
		*pVecMaxs = vMaxs;
}

const matrix3x4_t* CTransformedCollideable::GetRootParentToWorldTransform() const
{
	const matrix3x4_t* pWrappedVersion = m_pWrappedCollideable->GetRootParentToWorldTransform();
	if (pWrappedVersion == NULL)
		return NULL;

	ConcatTransforms(m_matTransform.As3x4(), *pWrappedVersion, m_ReferencedVars.m_matRootParentToWorldTransform);
	return &m_ReferencedVars.m_matRootParentToWorldTransform;
}

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

#define PORTAL_WALL_FARDIST 200.0f
#define PORTAL_WALL_TUBE_DEPTH 1.0f
#define PORTAL_WALL_TUBE_OFFSET 0.01f
#define PORTAL_WALL_MIN_THICKNESS 0.1f
#define PORTAL_POLYHEDRON_CUT_EPSILON (1.0f/1099511627776.0f) //    1 / (1<<40)
#define PORTAL_WORLD_WALL_HALF_SEPARATION_AMOUNT 0.1f //separating the world collision from wall collision by a small amount gets rid of extremely thin erroneous collision at the separating plane

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
static ConVar sv_dump_portalsimulator_collision( "sv_dump_portalsimulator_collision", "0", FCVAR_REPLICATED | FCVAR_CHEAT ); //whether to actually dump out the data now that the possibility exists
static void PortalSimulatorDumps_DumpCollideToGlView( CPhysCollide *pCollide, const Vector &origin, const QAngle &angles, float fColorScale, const char *pFilename );
static void PortalSimulatorDumps_DumpBoxToGlView( const Vector &vMins, const Vector &vMaxs, float fRed, float fGreen, float fBlue, const char *pszFileName );
#endif

#ifdef DEBUG_PORTAL_SIMULATION_CREATION_TIMES
#define STARTDEBUGTIMER(x) { x.Start(); }
#define STOPDEBUGTIMER(x) { x.End(); }
#define DEBUGTIMERONLY(x) x
#define CREATEDEBUGTIMER(x) CFastTimer x;
static const char *s_szTabSpacing[] = { "", "\t", "\t\t", "\t\t\t", "\t\t\t\t", "\t\t\t\t\t", "\t\t\t\t\t\t", "\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t\t\t" };
static int s_iTabSpacingIndex = 0;
static int s_iPortalSimulatorGUID = 0; //used in standalone function that have no idea what a portal simulator is
#define INCREMENTTABSPACING() ++s_iTabSpacingIndex;
#define DECREMENTTABSPACING() --s_iTabSpacingIndex;
#define TABSPACING (s_szTabSpacing[s_iTabSpacingIndex])
#else
#define STARTDEBUGTIMER(x)
#define STOPDEBUGTIMER(x)
#define DEBUGTIMERONLY(x)
#define CREATEDEBUGTIMER(x)
#define INCREMENTTABSPACING()
#define DECREMENTTABSPACING()
#define TABSPACING
#endif

#define PORTAL_HOLE_HALF_HEIGHT (PORTAL_HALF_HEIGHT + 0.1f)
#define PORTAL_HOLE_HALF_WIDTH (PORTAL_HALF_WIDTH + 0.1f)


static void ConvertBrushListToClippedPolyhedronList( const int *pBrushes, int iBrushCount, const float *pOutwardFacingClipPlanes, int iClipPlaneCount, float fClipEpsilon, CUtlVector<CPolyhedron *> *pPolyhedronList );
static void ClipPolyhedrons( CPolyhedron * const *pExistingPolyhedrons, int iPolyhedronCount, const float *pOutwardFacingClipPlanes, int iClipPlaneCount, float fClipEpsilon, CUtlVector<CPolyhedron *> *pPolyhedronList );
static inline CPolyhedron *TransformAndClipSinglePolyhedron( CPolyhedron *pExistingPolyhedron, const VMatrix &Transform, const float *pOutwardFacingClipPlanes, int iClipPlaneCount, float fCutEpsilon, bool bUseTempMemory );
static int GetEntityPhysicsObjects( IPhysicsEnvironment *pEnvironment, CBaseEntity *pEntity, IPhysicsObject **pRetList, int iRetListArraySize );
static CPhysCollide *ConvertPolyhedronsToCollideable( CPolyhedron **pPolyhedrons, int iPolyhedronCount );

static CUtlVector<CPortalSimulator *> s_PortalSimulators;
CUtlVector<CPortalSimulator *> const &g_PortalSimulators = s_PortalSimulators;
static CPortalSimulatorEventCallbacks s_DummyPortalSimulatorCallback;

const char *PS_SD_Static_World_StaticProps_ClippedProp_t::szTraceSurfaceName = "**studio**";
const int PS_SD_Static_World_StaticProps_ClippedProp_t::iTraceSurfaceFlags = 0;
CBaseEntity *PS_SD_Static_World_StaticProps_ClippedProp_t::pTraceEntity = NULL;

IMPLEMENT_NETWORKCLASS_ALIASED(PSCollisionEntity, DT_PSCollisionEntity)

BEGIN_NETWORK_TABLE(CPSCollisionEntity, DT_PSCollisionEntity)
#if !defined( CLIENT_DLL )
	SendPropEHandle(SENDINFO(m_pOwningSimulator)),
#else
	RecvPropEHandle(RECVINFO(m_pOwningSimulator)),
#endif
END_NETWORK_TABLE()

LINK_ENTITY_TO_CLASS(portalsimulator_collisionentity, CPSCollisionEntity);

static bool s_PortalSimulatorCollisionEntities[MAX_EDICTS] = { false };

//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
IMPLEMENT_NETWORKCLASS_ALIASED(PortalSimulator, DT_PortalSimulator);

BEGIN_NETWORK_TABLE(CPortalSimulator, DT_PortalSimulator)
#if !defined( CLIENT_DLL )
	SendPropEHandle(SENDINFO(pCollisionEntity)),
	SendPropEHandle(SENDINFO(m_hLinkedPortal)),
	SendPropBool(SENDINFO(m_bActivated)),
	SendPropBool(SENDINFO(m_bIsPortal2)),
#else
	RecvPropEHandle(RECVINFO(pCollisionEntity)),
	RecvPropEHandle(RECVINFO(m_hLinkedPortal)),
	RecvPropBool(RECVINFO(m_bActivated)),
	RecvPropBool(RECVINFO(m_bIsPortal2)),
#endif
END_NETWORK_TABLE()

CPortalSimulator::CPortalSimulator( void )
: m_bLocalDataIsReady(false),
	m_bGenerateCollision(true),
	m_bSimulateVPhysics(true),
	m_bSharedCollisionConfiguration(false),
	m_pLinkedPortal(NULL),
	m_bInCrossLinkedFunction(false),
	m_pCallbacks(&s_DummyPortalSimulatorCallback)
{
	s_PortalSimulators.AddToTail( this );

#ifdef CLIENT_DLL
	m_bGenerateCollision = (GameRules() && GameRules()->IsMultiplayer());
#endif
	m_bActivated = false;
	m_bIsPortal2 = false;
	m_CreationChecklist.bPolyhedronsGenerated = false;
	m_CreationChecklist.bLocalCollisionGenerated = false;
	m_CreationChecklist.bLinkedCollisionGenerated = false;
	m_CreationChecklist.bLocalPhysicsGenerated = false;
	m_CreationChecklist.bLinkedPhysicsGenerated = false;

#ifdef PORTAL_SIMULATORS_EMBED_GUID
	static int s_iPortalSimulatorGUIDAllocator = 0;
	m_iPortalSimulatorGUID = s_iPortalSimulatorGUIDAllocator++;
#endif

#ifndef CLIENT_DLL
	PS_SD_Static_World_StaticProps_ClippedProp_t::pTraceEntity = GetWorldEntity(); //will overinitialize, but it's cheap
#else
	PS_SD_Static_World_StaticProps_ClippedProp_t::pTraceEntity = GetClientWorldEntity();
#endif
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
	pCollisionEntity = (CPSCollisionEntity*)gEntList.CreateEntityByName("portalsimulator_collisionentity");
	Assert(pCollisionEntity != NULL);
	pCollisionEntity->m_pOwningSimulator = this;
	AfterCollisionEntityCreated();
	DispatchSpawn(pCollisionEntity);
}
#endif // GAME_DLL


CPortalSimulator::~CPortalSimulator( void )
{
	//go assert crazy here
	DetachFromLinked();
	ClearEverything();

	for( int i = s_PortalSimulators.Count(); --i >= 0; )
	{
		if( s_PortalSimulators[i] == this )
		{
			s_PortalSimulators.FastRemove( i );
			break;
		}
	}

	if (pCollisionEntity.Get()) {
		pCollisionEntity->ClearHoleShapeCollideable();
	}

#ifndef CLIENT_DLL
	if( pCollisionEntity.Get() )
	{
		BeforeCollisionEntityDestroy();
		pCollisionEntity->m_pOwningSimulator = NULL;
		UTIL_Remove( pCollisionEntity );
		pCollisionEntity = NULL;
	}
#endif
}



void CPortalSimulator::MoveTo( const Vector &ptCenter, const QAngle &angles )
{
	if( (pCollisionEntity->m_InternalData.Placement.ptCenter == ptCenter) && (pCollisionEntity->m_InternalData.Placement.qAngles == angles) ) //not actually moving at all
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::MoveTo() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	BeforeMove();
	//update geometric data
	pCollisionEntity->MoveTo(ptCenter, angles);

	//Clear();
#ifndef CLIENT_DLL
	ClearLinkedPhysics();
	ClearLocalPhysics();
#endif
	ClearLinkedCollision();
	ClearLocalCollision();
	ClearPolyhedrons();

	m_bLocalDataIsReady = true;
	UpdateLinkMatrix();

	pCollisionEntity->CreateHoleShapeCollideable();

	CreatePolyhedrons();	
	CreateAllCollision();
#ifndef CLIENT_DLL
	CreateAllPhysics();
#endif

	AfterMove();

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
	if( m_pLinkedPortal && m_pLinkedPortal->m_bLocalDataIsReady )
	{
		pCollisionEntity->UpdateLinkMatrix(m_pLinkedPortal->pCollisionEntity);
	}
	else
	{
		pCollisionEntity->UpdateLinkMatrix(NULL);
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
	if( m_bLocalDataIsReady == false )
		return false;

	return pCollisionEntity->EntityIsInPortalHole(pEntity);
}

bool CPortalSimulator::EntityHitBoxExtentIsInPortalHole( CBaseAnimating *pBaseAnimating ) const
{
	if( m_bLocalDataIsReady == false )
		return false;

	return pCollisionEntity->EntityHitBoxExtentIsInPortalHole(pBaseAnimating);
}

bool CPortalSimulator::RayIsInPortalHole(const Ray_t& ray) const
{
	return pCollisionEntity->RayIsInPortalHole(ray);
}

void CPortalSimulator::TraceRay(const Ray_t& ray, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace, bool bTraceHolyWall) const//traces against a specific portal's environment, does no *real* tracing
{
#ifdef CLIENT_DLL
	Assert((GameRules() == NULL) || GameRules()->IsMultiplayer());
#endif
	Assert(IsReadyToSimulate()); //a trace shouldn't make it down this far if the portal is incapable of changing the results of the trace

	CTraceFilterHitAll traceFilterHitAll;
	if (!pTraceFilter)
	{
		pTraceFilter = &traceFilterHitAll;
	}

	pTrace->fraction = 2.0f;
	pTrace->startsolid = true;
	pTrace->allsolid = true;

	trace_t TempTrace;
	int counter;

	CPortalSimulator* pLinkedPortalSimulator = GetLinkedPortalSimulator();

	//bool bTraceDisplacements = sv_portal_trace_vs_displacements.GetBool();
	bool bTraceStaticProps = sv_portal_trace_vs_staticprops.GetBool();
	if (sv_portal_trace_vs_holywall.GetBool() == false)
		bTraceHolyWall = false;

	bool bTraceTransformedGeometry = ((pLinkedPortalSimulator != NULL) && bTraceHolyWall && RayIsInPortalHole(ray));

	bool bCopyBackBrushTraceData = false;



	// Traces vs world
	if (pTraceFilter->GetTraceType() != TRACE_ENTITIES_ONLY)
	{
		//trace_t RealTrace;
		//enginetrace->TraceRay( ray, fMask, pTraceFilter, &RealTrace );
		if (pCollisionEntity->m_DataAccess.Simulation.Static.World.Brushes.pCollideable && sv_portal_trace_vs_world.GetBool())
		{
			physcollision->TraceBox(ray, pCollisionEntity->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, vec3_origin, vec3_angle, pTrace);
			bCopyBackBrushTraceData = true;
		}

		if (bTraceHolyWall)
		{
			if (pCollisionEntity->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable)
			{
				physcollision->TraceBox(ray, pCollisionEntity->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable, vec3_origin, vec3_angle, &TempTrace);

				if ((TempTrace.startsolid == false) && (TempTrace.fraction < pTrace->fraction)) //never allow something to be stuck in the tube, it's more of a last-resort guide than a real collideable
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}

			if (pCollisionEntity->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable)
			{
				physcollision->TraceBox(ray, pCollisionEntity->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable, vec3_origin, vec3_angle, &TempTrace);
				if ((TempTrace.fraction < pTrace->fraction))
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}

			//if( portalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pCollideable && sv_portal_trace_vs_world.GetBool() )
			if (bTraceTransformedGeometry && pLinkedPortalSimulator->pCollisionEntity->m_DataAccess.Simulation.Static.World.Brushes.pCollideable)
			{
				physcollision->TraceBox(ray, pLinkedPortalSimulator->pCollisionEntity->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, pCollisionEntity->m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, pCollisionEntity->m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, &TempTrace);
				if ((TempTrace.fraction < pTrace->fraction))
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}
		}

		if (bCopyBackBrushTraceData)
		{
			pTrace->surface = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.surface;
			pTrace->contents = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.contents;
			pTrace->m_pEnt = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.pEntity;

			bCopyBackBrushTraceData = false;
		}
	}

	// Traces vs entities
	if (pTraceFilter->GetTraceType() != TRACE_WORLD_ONLY)
	{
		bool bFilterStaticProps = (pTraceFilter->GetTraceType() == TRACE_EVERYTHING_FILTER_PROPS);

		//solid entities
		CPortalCollideableEnumerator enumerator(this);
		partition->EnumerateElementsAlongRay(PARTITION_ENGINE_SOLID_EDICTS | PARTITION_ENGINE_STATIC_PROPS, ray, false, &enumerator);
		for (counter = 0; counter != enumerator.m_iHandleCount; ++counter)
		{
			if (staticpropmgr->IsStaticProp(enumerator.m_pHandles[counter]))
			{
				//if( bFilterStaticProps && !pTraceFilter->ShouldHitEntity( enumerator.m_pHandles[counter], fMask ) )
				continue; //static props are handled separately, with clipped versions
			}
			else if (!pTraceFilter->ShouldHitEntity(enumerator.m_pHandles[counter], fMask))
			{
				continue;
			}

			enginetrace->ClipRayToEntity(ray, fMask, enumerator.m_pHandles[counter], &TempTrace);
			if ((TempTrace.fraction < pTrace->fraction))
				*pTrace = TempTrace;
		}




		if (bTraceStaticProps)
		{
			//local clipped static props
			{
				int iLocalStaticCount = pCollisionEntity->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Count();
				if (iLocalStaticCount != 0 && pCollisionEntity->m_DataAccess.Simulation.Static.World.StaticProps.bCollisionExists)
				{
					const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = pCollisionEntity->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Base();
					const PS_SD_Static_World_StaticProps_ClippedProp_t* pStop = pCurrentProp + iLocalStaticCount;
					Vector vTransform = vec3_origin;
					QAngle qTransform = vec3_angle;

					do
					{
						if ((!bFilterStaticProps) || pTraceFilter->ShouldHitEntity(pCurrentProp->pSourceProp, fMask))
						{
							physcollision->TraceBox(ray, pCurrentProp->pCollide, vTransform, qTransform, &TempTrace);
							if ((TempTrace.fraction < pTrace->fraction))
							{
								*pTrace = TempTrace;
								pTrace->surface.flags = pCurrentProp->iTraceSurfaceFlags;
								pTrace->surface.surfaceProps = pCurrentProp->iTraceSurfaceProps;
								pTrace->surface.name = pCurrentProp->szTraceSurfaceName;
								pTrace->contents = pCurrentProp->iTraceContents;
								pTrace->m_pEnt = pCurrentProp->pTraceEntity;
							}
						}

						++pCurrentProp;
					} while (pCurrentProp != pStop);
				}
			}

			if (bTraceHolyWall)
			{
				//remote clipped static props transformed into our wall space
				if (bTraceTransformedGeometry && (pTraceFilter->GetTraceType() != TRACE_WORLD_ONLY) && sv_portal_trace_vs_staticprops.GetBool())
				{
					int iLocalStaticCount = pLinkedPortalSimulator->pCollisionEntity->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Count();
					if (iLocalStaticCount != 0)
					{
						const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = pLinkedPortalSimulator->pCollisionEntity->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Base();
						const PS_SD_Static_World_StaticProps_ClippedProp_t* pStop = pCurrentProp + iLocalStaticCount;
						Vector vTransform = pCollisionEntity->m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform;
						QAngle qTransform = pCollisionEntity->m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform;

						do
						{
							if ((!bFilterStaticProps) || pTraceFilter->ShouldHitEntity(pCurrentProp->pSourceProp, fMask))
							{
								physcollision->TraceBox(ray, pCurrentProp->pCollide, vTransform, qTransform, &TempTrace);
								if ((TempTrace.fraction < pTrace->fraction))
								{
									*pTrace = TempTrace;
									pTrace->surface.flags = pCurrentProp->iTraceSurfaceFlags;
									pTrace->surface.surfaceProps = pCurrentProp->iTraceSurfaceProps;
									pTrace->surface.name = pCurrentProp->szTraceSurfaceName;
									pTrace->contents = pCurrentProp->iTraceContents;
									pTrace->m_pEnt = pCurrentProp->pTraceEntity;
								}
							}

							++pCurrentProp;
						} while (pCurrentProp != pStop);
					}
				}
			}
		}
	}

	if (pTrace->fraction > 1.0f) //this should only happen if there was absolutely nothing to trace against
	{
		//AssertMsg( 0, "Nothing to trace against" );
		memset(pTrace, 0, sizeof(trace_t));
		pTrace->fraction = 1.0f;
		pTrace->startpos = ray.m_Start - ray.m_StartOffset;
		pTrace->endpos = pTrace->startpos + ray.m_Delta;
	}
	else if (pTrace->fraction < 0)
	{
		// For all brush traces, use the 'portal backbrush' surface surface contents
		// BUGBUG: Doing this is a great solution because brushes near a portal
		// will have their contents and surface properties homogenized to the brush the portal ray hit.
		pTrace->contents = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.contents;
		pTrace->surface = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.surface;
		pTrace->m_pEnt = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.pEntity;
	}
}

void CPortalSimulator::TraceEntity(CBaseEntity* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter* pFilter, trace_t* pTrace) const
{

	CPortalSimulator* pLinkedPortalSimulator = this->GetLinkedPortalSimulator();
	ICollideable* pCollision = enginetrace->GetCollideable(pEntity);

	Ray_t entRay;
	entRay.Init(vecAbsStart, vecAbsEnd, pCollision->OBBMins(), pCollision->OBBMaxs());

#if 0 // this trace for brush ents made sense at one time, but it's 'overcolliding' during portal transitions (bugzilla#25)
	if (realTrace.m_pEnt && (realTrace.m_pEnt->GetEngineObject()->GetMoveType() != MOVETYPE_NONE)) //started by hitting something moving which wouldn't be detected in the following traces
	{
		float fFirstPortalFraction = 2.0f;
		CProp_Portal* pFirstPortal = UTIL_Portal_FirstAlongRay(entRay, fFirstPortalFraction);

		if (!pFirstPortal)
			*pTrace = realTrace;
		else
		{
			Vector vFirstPortalForward;
			pFirstPortal->GetVectors(&vFirstPortalForward, NULL, NULL);
			if (vFirstPortalForward.Dot(realTrace.endpos - pFirstPortal->GetAbsOrigin()) > 0.0f)
				*pTrace = realTrace;
		}
	}
#endif

	// We require both environments to be active in order to trace against them
	Assert(pCollision);
	if (!pCollision)
	{
		return;
	}

	// World, displacements and holy wall are stored in separate collideables
	// Traces against each and keep the closest intersection (if any)
	trace_t tempTrace;

	// Hit the world
	if (pFilter->GetTraceType() != TRACE_ENTITIES_ONLY)
	{
		if (pCollisionEntity->m_DataAccess.Simulation.Static.World.Brushes.pCollideable &&
			sv_portal_trace_vs_world.GetBool())
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles, 
			//							pPortalSimulator->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, vec3_origin, vec3_angle, &tempTrace );

			physcollision->TraceBox(entRay, MASK_ALL, NULL, pCollisionEntity->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, vec3_origin, vec3_angle, &tempTrace);

			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				*pTrace = tempTrace;
			}
		}

		//if( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pCollideable &&
		if (pLinkedPortalSimulator &&
			pLinkedPortalSimulator->pCollisionEntity->m_DataAccess.Simulation.Static.World.Brushes.pCollideable &&
			sv_portal_trace_vs_world.GetBool() &&
			sv_portal_trace_vs_holywall.GetBool())
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pLinkedPortalSimulator->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, &tempTrace );

			physcollision->TraceBox(entRay, MASK_ALL, NULL, pLinkedPortalSimulator->pCollisionEntity->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, pCollisionEntity->m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, pCollisionEntity->m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, &tempTrace);

			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				*pTrace = tempTrace;
			}
		}

		if (pCollisionEntity->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable &&
			sv_portal_trace_vs_holywall.GetBool())
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable, vec3_origin, vec3_angle, &tempTrace );

			physcollision->TraceBox(entRay, MASK_ALL, NULL, pCollisionEntity->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable, vec3_origin, vec3_angle, &tempTrace);

			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				if (tempTrace.fraction == 0.0f)
					tempTrace.startsolid = true;

				if (tempTrace.fractionleftsolid == 1.0f)
					tempTrace.allsolid = true;

				*pTrace = tempTrace;
			}
		}

		if (pCollisionEntity->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable &&
			sv_portal_trace_vs_holywall.GetBool())
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable, vec3_origin, vec3_angle, &tempTrace );

			physcollision->TraceBox(entRay, MASK_ALL, NULL, pCollisionEntity->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable, vec3_origin, vec3_angle, &tempTrace);

			if ((tempTrace.startsolid == false) && (tempTrace.fraction < pTrace->fraction)) //never allow something to be stuck in the tube, it's more of a last-resort guide than a real collideable
			{
				*pTrace = tempTrace;
			}
		}

		// For all brush traces, use the 'portal backbrush' surface surface contents
		// BUGBUG: Doing this is a great solution because brushes near a portal
		// will have their contents and surface properties homogenized to the brush the portal ray hit.
		if (pTrace->startsolid || (pTrace->fraction < 1.0f))
		{
			pTrace->surface = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.surface;
			pTrace->contents = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.contents;
			pTrace->m_pEnt = pCollisionEntity->m_DataAccess.Simulation.Static.SurfaceProperties.pEntity;
		}
	}

	// Trace vs entities
	if (pFilter->GetTraceType() != TRACE_WORLD_ONLY)
	{
		if (sv_portal_trace_vs_staticprops.GetBool() && (pFilter->GetTraceType() != TRACE_ENTITIES_ONLY))
		{
			bool bFilterStaticProps = (pFilter->GetTraceType() == TRACE_EVERYTHING_FILTER_PROPS);

			//local clipped static props
			{
				int iLocalStaticCount = pCollisionEntity->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Count();
				if (iLocalStaticCount != 0 && pCollisionEntity->m_DataAccess.Simulation.Static.World.StaticProps.bCollisionExists)
				{
					const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = pCollisionEntity->m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Base();
					const PS_SD_Static_World_StaticProps_ClippedProp_t* pStop = pCurrentProp + iLocalStaticCount;
					Vector vTransform = vec3_origin;
					QAngle qTransform = vec3_angle;

					do
					{
						if ((!bFilterStaticProps) || pFilter->ShouldHitEntity(pCurrentProp->pSourceProp, mask))
						{
							//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
							//							pCurrentProp->pCollide, vTransform, qTransform, &tempTrace );

							physcollision->TraceBox(entRay, MASK_ALL, NULL, pCurrentProp->pCollide, vTransform, qTransform, &tempTrace);

							if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
							{
								*pTrace = tempTrace;
								pTrace->surface.flags = pCurrentProp->iTraceSurfaceFlags;
								pTrace->surface.surfaceProps = pCurrentProp->iTraceSurfaceProps;
								pTrace->surface.name = pCurrentProp->szTraceSurfaceName;
								pTrace->contents = pCurrentProp->iTraceContents;
								pTrace->m_pEnt = pCurrentProp->pTraceEntity;
							}
						}

						++pCurrentProp;
					} while (pCurrentProp != pStop);
				}
			}

			if (pLinkedPortalSimulator && pCollisionEntity->EntityIsInPortalHole(pEntity))
			{

#ifndef CLIENT_DLL
				if (sv_use_transformed_collideables.GetBool()) //if this never gets turned off, it should be removed before release
				{
					//moving entities near the remote portal
					CBaseEntity* pEnts[1024];
					int iEntCount = pLinkedPortalSimulator->GetMoveableOwnedEntities(pEnts, 1024);

					CTransformedCollideable transformedCollideable;
					transformedCollideable.m_matTransform = pLinkedPortalSimulator->pCollisionEntity->m_DataAccess.Placement.matThisToLinked;
					transformedCollideable.m_matInvTransform = pLinkedPortalSimulator->pCollisionEntity->m_DataAccess.Placement.matLinkedToThis;
					for (int i = 0; i != iEntCount; ++i)
					{
						CBaseEntity* pRemoteEntity = pEnts[i];
						if (pRemoteEntity->GetEngineObject()->GetSolid() == SOLID_NONE)
							continue;

						transformedCollideable.m_pWrappedCollideable = pRemoteEntity->GetCollideable();
						Assert(transformedCollideable.m_pWrappedCollideable != NULL);

						//enginetrace->ClipRayToCollideable( entRay, mask, &transformedCollideable, pTrace );

						enginetrace->ClipRayToCollideable(entRay, mask, &transformedCollideable, &tempTrace);
						if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
						{
							*pTrace = tempTrace;
						}
					}
				}
#endif //#ifndef CLIENT_DLL
			}
		}
	}

	if (pTrace->fraction == 1.0f)
	{
		memset(pTrace, 0, sizeof(trace_t));
		pTrace->fraction = 1.0f;
		pTrace->startpos = vecAbsStart;
		pTrace->endpos = vecAbsEnd;
	}
	//#endif
	
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

	if( m_bLocalDataIsReady && m_pLinkedPortal->m_bLocalDataIsReady )
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

	if( pPhysicsEnvironment != NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateMinimumPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	pPhysicsEnvironment = physenv_main;

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateMinimumPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::CreateLocalPhysics( void )
{
	if( IsSimulatingVPhysics() == false )
		return;

	AssertMsg( m_bLocalDataIsReady, "Portal simulator attempting to create local physics before being placed." );

	if( m_CreationChecklist.bLocalPhysicsGenerated )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLocalPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	CreateMinimumPhysics();

	pCollisionEntity->CreateLocalPhysics();
	//if( pCollisionEntity )
	pCollisionEntity->GetEngineObject()->CollisionRulesChanged();

	AfterLocalPhysicsCreated();
	
	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLocalPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bLocalPhysicsGenerated = true;
}



void CPortalSimulator::CreateLinkedPhysics( void )
{
	if( IsSimulatingVPhysics() == false )
		return;

	AssertMsg( m_bLocalDataIsReady, "Portal simulator attempting to create linked physics before being placed itself." );

	if( (m_pLinkedPortal == NULL) || (m_pLinkedPortal->m_bLocalDataIsReady == false) )
		return;

	if( m_CreationChecklist.bLinkedPhysicsGenerated )
		return;

	CREATEDEBUGTIMER( functionTimer );
	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLinkedPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	CreateMinimumPhysics();

	pCollisionEntity->CreateLinkedPhysics(m_pLinkedPortal->pCollisionEntity);
	//if( pCollisionEntity )
	pCollisionEntity->GetEngineObject()->CollisionRulesChanged();

	AfterLinkedPhysicsCreated();

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
	if( pPhysicsEnvironment == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearMinimumPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	pPhysicsEnvironment = NULL;

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearMinimumPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}



void CPortalSimulator::ClearLocalPhysics( void )
{
	if( m_CreationChecklist.bLocalPhysicsGenerated == false )
		return;

	if( pPhysicsEnvironment == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLocalPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	pPhysicsEnvironment->CleanupDeleteList();
	pPhysicsEnvironment->SetQuickDelete( true ); //if we don't do this, things crash the next time we cleanup the delete list while checking mindists

	BeforeLocalPhysicsClear();

	if (pCollisionEntity.Get()) {
		pCollisionEntity->ClearLocalPhysics();
	}

	pPhysicsEnvironment->CleanupDeleteList();
	pPhysicsEnvironment->SetQuickDelete( false );

	if (pCollisionEntity.Get()) {
		pCollisionEntity->GetEngineObject()->CollisionRulesChanged();
	}

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLocalPhysics() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	m_CreationChecklist.bLocalPhysicsGenerated = false;
}



void CPortalSimulator::ClearLinkedPhysics( void )
{
	if( m_CreationChecklist.bLinkedPhysicsGenerated == false )
		return;

	if( pPhysicsEnvironment == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLinkedPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	pPhysicsEnvironment->CleanupDeleteList();
	pPhysicsEnvironment->SetQuickDelete( true ); //if we don't do this, things crash the next time we cleanup the delete list while checking mindists

	BeforeLinkedPhysicsClear();

	if (pCollisionEntity.Get()) {
		pCollisionEntity->ClearLinkedPhysics();
	}

	if( m_pLinkedPortal && (m_pLinkedPortal->m_bInCrossLinkedFunction == false) )
	{
		Assert( m_bInCrossLinkedFunction == false ); //I'm pretty sure switching to a stack would have negative repercussions
		m_bInCrossLinkedFunction = true;
		m_pLinkedPortal->ClearLinkedPhysics();
		m_bInCrossLinkedFunction = false;
	}

	//Assert( (ShadowClones.FromLinkedPortal.Count() == 0) && 
	//	((m_pLinkedPortal == NULL) || (m_pLinkedPortal->ShadowClones.FromLinkedPortal.Count() == 0)) );

	pPhysicsEnvironment->CleanupDeleteList();
	pPhysicsEnvironment->SetQuickDelete( false );

	if (pCollisionEntity.Get()) {
		pCollisionEntity->GetEngineObject()->CollisionRulesChanged();
	}

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
	AssertMsg( m_bLocalDataIsReady, "Portal simulator attempting to create local collision before being placed." );

	if( m_CreationChecklist.bLocalCollisionGenerated )
		return;

	if( IsCollisionGenerationEnabled() == false )
		return;

	DEBUGTIMERONLY( s_iPortalSimulatorGUID = GetPortalSimulatorGUID() );

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateLocalCollision() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	pCollisionEntity->CreateLocalCollision();

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
	
	if (pCollisionEntity.Get()) {
		pCollisionEntity->ClearLocalCollision();
	}

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

	pCollisionEntity->CreatePolyhedrons();

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
	
	if (pCollisionEntity.Get()) {
		pCollisionEntity->ClearPolyhedrons();
	}

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

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::DetachFromLinked() FINISH: %fms\n", GetPortalSimulatorGUID(), TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );
}

void CPortalSimulator::SetPortalSimulatorCallbacks( CPortalSimulatorEventCallbacks *pCallbacks )
{
	if( pCallbacks )
		m_pCallbacks = pCallbacks;
	else
		m_pCallbacks = &s_DummyPortalSimulatorCallback; //always keep the pointer valid
}




void CPortalSimulator::SetVPhysicsSimulationEnabled( bool bEnabled )
{
	AssertMsg( (m_pLinkedPortal == NULL) || (m_pLinkedPortal->m_bSimulateVPhysics == m_bSimulateVPhysics), "Linked portals are in disagreement as to whether they would simulate VPhysics." );

	if( bEnabled == m_bSimulateVPhysics )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::SetVPhysicsSimulationEnabled() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();
	
	m_bSimulateVPhysics = bEnabled;
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
CPortalSimulator *CPortalSimulator::GetSimulatorThatCreatedPhysicsObject( const IPhysicsObject *pObject, PS_PhysicsObjectSourceType_t *pOut_SourceType )
{
	for( int i = s_PortalSimulators.Count(); --i >= 0; )
	{
		if( s_PortalSimulators[i]->pCollisionEntity->CreatedPhysicsObject( pObject, pOut_SourceType ) )
			return s_PortalSimulators[i];
	}
	
	return NULL;
}

bool CPortalSimulator::CreatedPhysicsObject( const IPhysicsObject *pObject, PS_PhysicsObjectSourceType_t *pOut_SourceType ) const
{
	return pCollisionEntity->CreatedPhysicsObject(pObject, pOut_SourceType);
}

bool CPSCollisionEntity::CreatedPhysicsObject( const IPhysicsObject *pObject, PS_PhysicsObjectSourceType_t *pOut_SourceType ) const
{
	if( (pObject == m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject) || (pObject == m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject) )
	{
		if( pOut_SourceType )
			*pOut_SourceType = PSPOST_LOCAL_BRUSHES;

		return true;
	}

	if( pObject == m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject )
	{
		if( pOut_SourceType )
			*pOut_SourceType = PSPOST_REMOTE_BRUSHES;

		return true;
	}

	for( int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
	{
		if( m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pPhysicsObject == pObject )
		{
			if( pOut_SourceType )
				*pOut_SourceType = PSPOST_LOCAL_STATICPROPS;
			return true;
		}
	}

	for( int i = m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count(); --i >= 0; )
	{
		if( m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects[i] == pObject )
		{
			if( pOut_SourceType )
				*pOut_SourceType = PSPOST_REMOTE_STATICPROPS;

			return true;
		}
	}

	if( pObject == m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject )
	{
		if( pOut_SourceType )
			*pOut_SourceType = PSPOST_HOLYWALL_TUBE;

		return true;
	}	

	return false;
}
#endif //#ifndef CLIENT_DLL








static void ConvertBrushListToClippedPolyhedronList( const int *pBrushes, int iBrushCount, const float *pOutwardFacingClipPlanes, int iClipPlaneCount, float fClipEpsilon, CUtlVector<CPolyhedron *> *pPolyhedronList )
{
	if( pPolyhedronList == NULL )
		return;

	if( (pBrushes == NULL) || (iBrushCount == 0) )
		return;

	for( int i = 0; i != iBrushCount; ++i )
	{
		CPolyhedron *pPolyhedron = ClipPolyhedron( g_StaticCollisionPolyhedronCache.GetBrushPolyhedron( pBrushes[i] ), pOutwardFacingClipPlanes, iClipPlaneCount, fClipEpsilon );
		if( pPolyhedron )
			pPolyhedronList->AddToTail( pPolyhedron );
	}
}

static void ClipPolyhedrons( CPolyhedron * const *pExistingPolyhedrons, int iPolyhedronCount, const float *pOutwardFacingClipPlanes, int iClipPlaneCount, float fClipEpsilon, CUtlVector<CPolyhedron *> *pPolyhedronList )
{
	if( pPolyhedronList == NULL )
		return;

	if( (pExistingPolyhedrons == NULL) || (iPolyhedronCount == 0) )
		return;

	for( int i = 0; i != iPolyhedronCount; ++i )
	{
		CPolyhedron *pPolyhedron = ClipPolyhedron( pExistingPolyhedrons[i], pOutwardFacingClipPlanes, iClipPlaneCount, fClipEpsilon );
		if( pPolyhedron )
			pPolyhedronList->AddToTail( pPolyhedron );
	}
}

static CPhysCollide *ConvertPolyhedronsToCollideable( CPolyhedron **pPolyhedrons, int iPolyhedronCount )
{
	if( (pPolyhedrons == NULL) || (iPolyhedronCount == 0 ) )
		return NULL;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sConvertPolyhedronsToCollideable() START\n", s_iPortalSimulatorGUID, TABSPACING ); );
	INCREMENTTABSPACING();

	CPhysConvex **pConvexes = (CPhysConvex **)stackalloc( iPolyhedronCount * sizeof( CPhysConvex * ) );
	int iConvexCount = 0;

	CREATEDEBUGTIMER( convexTimer );
	STARTDEBUGTIMER( convexTimer );
	for( int i = 0; i != iPolyhedronCount; ++i )
	{
		pConvexes[iConvexCount] = physcollision->ConvexFromConvexPolyhedron( *pPolyhedrons[i] );

		Assert( pConvexes[iConvexCount] != NULL );
		
		if( pConvexes[iConvexCount] )
			++iConvexCount;		
	}
	STOPDEBUGTIMER( convexTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sConvex Generation:%fms\n", s_iPortalSimulatorGUID, TABSPACING, convexTimer.GetDuration().GetMillisecondsF() ); );


	CPhysCollide *pReturn;
	if( iConvexCount != 0 )
	{
		CREATEDEBUGTIMER( collideTimer );
		STARTDEBUGTIMER( collideTimer );
		pReturn = physcollision->ConvertConvexToCollide( pConvexes, iConvexCount );
		STOPDEBUGTIMER( collideTimer );
		DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCollideable Generation:%fms\n", s_iPortalSimulatorGUID, TABSPACING, collideTimer.GetDuration().GetMillisecondsF() ); );
	}
	else
	{
		pReturn = NULL;
	}

	STOPDEBUGTIMER( functionTimer );
	DECREMENTTABSPACING();
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sConvertPolyhedronsToCollideable() FINISH: %fms\n", s_iPortalSimulatorGUID, TABSPACING, functionTimer.GetDuration().GetMillisecondsF() ); );

	return pReturn;
}


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
	class CPS_AutoGameSys_EntityListener : public CAutoGameSystem//, public IEntityListener<CBaseEntity>
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
//		gEntList.AddListenerEntity( this );
//		return true;
//	}
//
//	//virtual void OnEntityCreated( CBaseEntity *pEntity ) {}
//	virtual void OnEntitySpawned( CBaseEntity *pEntity )
//	{
//
//	}
//	virtual void OnEntityDeleted( CBaseEntity *pEntity )
//	{
//		CPortalSimulator *pSimulator = CPortalSimulator::GetSimulatorThatOwnsEntity( pEntity );
//		if( pSimulator )
//		{
//			pSimulator->ReleasePhysicsOwnership( pEntity, false );
//			pSimulator->ReleaseOwnershipOfEntity( pEntity );
//		}
//		Assert( CPortalSimulator::GetSimulatorThatOwnsEntity( pEntity ) == NULL );
//	}
//#endif //#ifndef CLIENT_DLL
};
static CPS_AutoGameSys_EntityListener s_CPS_AGS_EL_Singleton;

CPSCollisionEntity::CPSCollisionEntity( void )
: m_DataAccess(m_InternalData)
{
	m_pOwningSimulator = NULL;
}

CPSCollisionEntity::~CPSCollisionEntity( void )
{
	if( m_pOwningSimulator )
	{
		Error("entindex() not valid in destruct");
		//m_pOwningSimulator->m_InternalData.Simulation.Dynamic.EntFlags[entindex()] &= ~PSEF_OWNS_PHYSICS;
		//m_pOwningSimulator->MarkAsReleased( this );
		//m_pOwningSimulator->m_InternalData.Simulation.pCollisionEntity = NULL;
		//m_pOwningSimulator = NULL;
	}
	//s_PortalSimulatorCollisionEntities[entindex()] = false;
}


void CPSCollisionEntity::UpdateOnRemove( void )
{
	GetEngineObject()->VPhysicsSetObject( NULL );
	ClearHoleShapeCollideable();
	ClearLinkedPhysics();
	ClearLocalPhysics();
	ClearLocalCollision();
	ClearPolyhedrons();
#ifdef GAME_DLL
	if (m_pOwningSimulator) {
		m_pOwningSimulator->BeforeCollisionEntityDestroy();
		m_pOwningSimulator->m_bActivated = false;
		m_pOwningSimulator->pCollisionEntity = NULL;
		m_pOwningSimulator = NULL;
	}
	s_PortalSimulatorCollisionEntities[entindex()] = false;
#endif // GAME_DLL
	BaseClass::UpdateOnRemove();
}

void CPSCollisionEntity::Spawn( void )
{
	BaseClass::Spawn();
	GetEngineObject()->SetSolid( SOLID_CUSTOM );
	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_NONE );
	s_PortalSimulatorCollisionEntities[entindex()] = true;
	GetEngineObject()->VPhysicsSetObject( NULL );
	GetEngineObject()->AddFlag( FL_WORLDBRUSH );
	GetEngineObject()->AddEffects( EF_NODRAW | EF_NOSHADOW | EF_NORECEIVESHADOW );
#ifdef GAME_DLL
	IncrementInterpolationFrame();
#endif // GAME_DLL
}

void CPSCollisionEntity::Activate( void )
{
	BaseClass::Activate();
	GetEngineObject()->CollisionRulesChanged();
}

int CPSCollisionEntity::ObjectCaps( void )
{
	return ((BaseClass::ObjectCaps() | FCAP_DONT_SAVE) & ~(FCAP_FORCE_TRANSITION | FCAP_ACROSS_TRANSITION | FCAP_MUST_SPAWN | FCAP_SAVE_NON_NETWORKABLE));
}

bool CPSCollisionEntity::ShouldCollide( int collisionGroup, int contentsMask ) const
{
#ifdef GAME_DLL
	return GetWorldEntity()->ShouldCollide(collisionGroup, contentsMask);
#endif // GAME_DLL
#ifdef CLIENT_DLL
	return GetClientWorldEntity()->ShouldCollide(collisionGroup, contentsMask);
#endif // CLIENT_DLL
}

IPhysicsObject * CPSCollisionEntity::VPhysicsGetObject( void )
{
	if(m_DataAccess.Simulation.Static.World.Brushes.pPhysicsObject != NULL )
		return m_DataAccess.Simulation.Static.World.Brushes.pPhysicsObject;
	else if(m_DataAccess.Simulation.Static.Wall.Local.Brushes.pPhysicsObject != NULL )
		return m_DataAccess.Simulation.Static.Wall.Local.Brushes.pPhysicsObject;
	else if(m_DataAccess.Simulation.Static.Wall.Local.Tube.pPhysicsObject != NULL )
		return m_DataAccess.Simulation.Static.Wall.Local.Brushes.pPhysicsObject;
	else if(m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject != NULL )
		return m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject;
	else
		return NULL;
}

int CPSCollisionEntity::VPhysicsGetObjectList( IPhysicsObject **pList, int listMax )
{
	if( (pList == NULL) || (listMax == 0) )
		return 0;

	int iRetVal = 0;

	if(m_DataAccess.Simulation.Static.World.Brushes.pPhysicsObject != NULL )
	{
		pList[iRetVal] = m_DataAccess.Simulation.Static.World.Brushes.pPhysicsObject;
		++iRetVal;
		if( iRetVal == listMax )
			return iRetVal;
	}

	if(m_DataAccess.Simulation.Static.Wall.Local.Brushes.pPhysicsObject != NULL )
	{
		pList[iRetVal] = m_DataAccess.Simulation.Static.Wall.Local.Brushes.pPhysicsObject;
		++iRetVal;
		if( iRetVal == listMax )
			return iRetVal;
	}

	if(m_DataAccess.Simulation.Static.Wall.Local.Tube.pPhysicsObject != NULL )
	{
		pList[iRetVal] = m_DataAccess.Simulation.Static.Wall.Local.Tube.pPhysicsObject;
		++iRetVal;
		if( iRetVal == listMax )
			return iRetVal;
	}

	if(m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject != NULL )
	{
		pList[iRetVal] = m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject;
		++iRetVal;
		if( iRetVal == listMax )
			return iRetVal;
	}

	return iRetVal;
}

void CPSCollisionEntity::MoveTo(const Vector& ptCenter, const QAngle& angles)
{
	{
		m_InternalData.Placement.ptCenter = ptCenter;
		m_InternalData.Placement.qAngles = angles;
		AngleVectors(angles, &m_InternalData.Placement.vForward, &m_InternalData.Placement.vRight, &m_InternalData.Placement.vUp);
		m_InternalData.Placement.PortalPlane.normal = m_InternalData.Placement.vForward;
		m_InternalData.Placement.PortalPlane.dist = m_InternalData.Placement.PortalPlane.normal.Dot(m_InternalData.Placement.ptCenter);
		m_InternalData.Placement.PortalPlane.signbits = SignbitsForPlane(&m_InternalData.Placement.PortalPlane);
		//m_InternalData.Placement.PortalPlane.Init(m_InternalData.Placement.vForward, m_InternalData.Placement.vForward.Dot(m_InternalData.Placement.ptCenter));
		Vector vAbsNormal;
		vAbsNormal.x = fabs(m_InternalData.Placement.PortalPlane.normal.x);
		vAbsNormal.y = fabs(m_InternalData.Placement.PortalPlane.normal.y);
		vAbsNormal.z = fabs(m_InternalData.Placement.PortalPlane.normal.z);

		if (vAbsNormal.x > vAbsNormal.y)
		{
			if (vAbsNormal.x > vAbsNormal.z)
			{
				if (vAbsNormal.x > 0.999f)
					m_InternalData.Placement.PortalPlane.type = PLANE_X;
				else
					m_InternalData.Placement.PortalPlane.type = PLANE_ANYX;
			}
			else
			{
				if (vAbsNormal.z > 0.999f)
					m_InternalData.Placement.PortalPlane.type = PLANE_Z;
				else
					m_InternalData.Placement.PortalPlane.type = PLANE_ANYZ;
			}
		}
		else
		{
			if (vAbsNormal.y > vAbsNormal.z)
			{
				if (vAbsNormal.y > 0.999f)
					m_InternalData.Placement.PortalPlane.type = PLANE_Y;
				else
					m_InternalData.Placement.PortalPlane.type = PLANE_ANYY;
			}
			else
			{
				if (vAbsNormal.z > 0.999f)
					m_InternalData.Placement.PortalPlane.type = PLANE_Z;
				else
					m_InternalData.Placement.PortalPlane.type = PLANE_ANYZ;
			}
		}
	}
}

void CPSCollisionEntity::UpdateLinkMatrix(CPSCollisionEntity* pRemoteCollisionEntity) 
{
	if (pRemoteCollisionEntity) {
		Vector vLocalLeft = -m_InternalData.Placement.vRight;
		VMatrix matLocalToWorld(m_InternalData.Placement.vForward, vLocalLeft, m_InternalData.Placement.vUp);
		matLocalToWorld.SetTranslation(m_InternalData.Placement.ptCenter);

		VMatrix matLocalToWorldInverse;
		MatrixInverseTR(matLocalToWorld, matLocalToWorldInverse);

		//180 degree rotation about up
		VMatrix matRotation;
		matRotation.Identity();
		matRotation.m[0][0] = -1.0f;
		matRotation.m[1][1] = -1.0f;

		Vector vRemoteLeft = -pRemoteCollisionEntity->m_InternalData.Placement.vRight;
		VMatrix matRemoteToWorld(pRemoteCollisionEntity->m_InternalData.Placement.vForward, vRemoteLeft, pRemoteCollisionEntity->m_InternalData.Placement.vUp);
		matRemoteToWorld.SetTranslation(pRemoteCollisionEntity->m_InternalData.Placement.ptCenter);

		//final
		m_InternalData.Placement.matThisToLinked = matRemoteToWorld * matRotation * matLocalToWorldInverse;
	}
	else {
		m_InternalData.Placement.matThisToLinked.Identity();
	}

	m_InternalData.Placement.matThisToLinked.InverseTR(m_InternalData.Placement.matLinkedToThis);

	MatrixAngles(m_InternalData.Placement.matThisToLinked.As3x4(), m_InternalData.Placement.ptaap_ThisToLinked.qAngleTransform, m_InternalData.Placement.ptaap_ThisToLinked.ptOriginTransform);
	MatrixAngles(m_InternalData.Placement.matLinkedToThis.As3x4(), m_InternalData.Placement.ptaap_LinkedToThis.qAngleTransform, m_InternalData.Placement.ptaap_LinkedToThis.ptOriginTransform);

}

bool CPSCollisionEntity::EntityIsInPortalHole(CBaseEntity* pEntity) const //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
{
	Assert(m_InternalData.Placement.pHoleShapeCollideable != NULL);

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
	const char* szDumpFileName = "ps_entholecheck.txt";
	if (sv_debug_dumpportalhole_nextcheck.GetBool())
	{
		filesystem->RemoveFile(szDumpFileName);

		DumpActiveCollision(this, szDumpFileName);
		PortalSimulatorDumps_DumpCollideToGlView(m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, 1.0f, szDumpFileName);
	}
#endif

	trace_t Trace;

	switch (pEntity->GetEngineObject()->GetSolid())
	{
	case SOLID_VPHYSICS:
	{
		ICollideable* pCollideable = pEntity->GetCollideable();
		vcollide_t* pVCollide = modelinfo->GetVCollide(pCollideable->GetCollisionModel());

		//Assert( pVCollide != NULL ); //brush models?
		if (pVCollide != NULL)
		{
			Vector ptEntityPosition = pCollideable->GetCollisionOrigin();
			QAngle qEntityAngles = pCollideable->GetCollisionAngles();

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
			if (sv_debug_dumpportalhole_nextcheck.GetBool())
			{
				for (int i = 0; i != pVCollide->solidCount; ++i)
					PortalSimulatorDumps_DumpCollideToGlView(m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, 0.4f, szDumpFileName);

				sv_debug_dumpportalhole_nextcheck.SetValue(false);
			}
#endif

			for (int i = 0; i != pVCollide->solidCount; ++i)
			{
				physcollision->TraceCollide(ptEntityPosition, ptEntityPosition, pVCollide->solids[i], qEntityAngles, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

				if (Trace.startsolid)
					return true;
			}
		}
		else
		{
			//energy balls lack a vcollide
			Vector vMins, vMaxs, ptCenter;
			pCollideable->WorldSpaceSurroundingBounds(&vMins, &vMaxs);
			ptCenter = (vMins + vMaxs) * 0.5f;
			vMins -= ptCenter;
			vMaxs -= ptCenter;
			physcollision->TraceBox(ptCenter, ptCenter, vMins, vMaxs, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

			return Trace.startsolid;
		}
		break;
	}

	case SOLID_BBOX:
	{
		physcollision->TraceBox(pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsOrigin(),
			pEntity->GetEngineObject()->OBBMins(), pEntity->GetEngineObject()->OBBMaxs(),
			m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
		if (sv_debug_dumpportalhole_nextcheck.GetBool())
		{
			Vector vMins = pEntity->GetEngineObject()->GetAbsOrigin() + pEntity->GetEngineObject()->OBBMins();
			Vector vMaxs = pEntity->GetEngineObject()->GetAbsOrigin() + pEntity->GetEngineObject()->OBBMaxs();
			PortalSimulatorDumps_DumpBoxToGlView(vMins, vMaxs, 1.0f, 1.0f, 1.0f, szDumpFileName);

			sv_debug_dumpportalhole_nextcheck.SetValue(false);
		}
#endif

		if (Trace.startsolid)
			return true;

		break;
	}
	case SOLID_NONE:
#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
		if (sv_debug_dumpportalhole_nextcheck.GetBool())
			sv_debug_dumpportalhole_nextcheck.SetValue(false);
#endif

		return false;

	default:
		Assert(false); //make a handler
	};

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
	if (sv_debug_dumpportalhole_nextcheck.GetBool())
		sv_debug_dumpportalhole_nextcheck.SetValue(false);
#endif

	return false;
}
bool CPSCollisionEntity::EntityHitBoxExtentIsInPortalHole(CBaseAnimating* pBaseAnimating) const //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
{
	bool bFirstVert = true;
	Vector vMinExtent;
	Vector vMaxExtent;

	IStudioHdr* pStudioHdr = pBaseAnimating->GetEngineObject()->GetModelPtr();
	if (!pStudioHdr)
		return false;

	mstudiohitboxset_t* set = pStudioHdr->pHitboxSet(pBaseAnimating->GetEngineObject()->GetHitboxSet());
	if (!set)
		return false;

	Vector position;
	QAngle angles;

	for (int i = 0; i < set->numhitboxes; i++)
	{
		mstudiobbox_t* pbox = set->pHitbox(i);

		pBaseAnimating->GetBonePosition(pbox->bone, position, angles);

		// Build a rotation matrix from orientation
		matrix3x4_t fRotateMatrix;
		AngleMatrix(angles, fRotateMatrix);

		//Vector pVerts[8];
		Vector vecPos;
		for (int i = 0; i < 8; ++i)
		{
			vecPos[0] = (i & 0x1) ? pbox->bbmax[0] : pbox->bbmin[0];
			vecPos[1] = (i & 0x2) ? pbox->bbmax[1] : pbox->bbmin[1];
			vecPos[2] = (i & 0x4) ? pbox->bbmax[2] : pbox->bbmin[2];

			Vector vRotVec;

			VectorRotate(vecPos, fRotateMatrix, vRotVec);
			vRotVec += position;

			if (bFirstVert)
			{
				vMinExtent = vRotVec;
				vMaxExtent = vRotVec;
				bFirstVert = false;
			}
			else
			{
				vMinExtent = vMinExtent.Min(vRotVec);
				vMaxExtent = vMaxExtent.Max(vRotVec);
			}
			}
	}

	Vector ptCenter = (vMinExtent + vMaxExtent) * 0.5f;
	vMinExtent -= ptCenter;
	vMaxExtent -= ptCenter;

	trace_t Trace;
	physcollision->TraceBox(ptCenter, ptCenter, vMinExtent, vMaxExtent, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

	if (Trace.startsolid)
		return true;

	return false;
}

bool CPSCollisionEntity::RayIsInPortalHole(const Ray_t& ray) const //traces a ray against the same detector for EntityIsInPortalHole(), bias is towards false positives
{
	trace_t Trace;
	physcollision->TraceBox(ray, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);
	return Trace.DidHit();
}

const Vector& CPSCollisionEntity::GetOrigin() const
{
	return m_DataAccess.Placement.ptCenter;
}

const QAngle& CPSCollisionEntity::GetAngles() const
{
	return m_DataAccess.Placement.qAngles;
}

const VMatrix& CPSCollisionEntity::MatrixThisToLinked() const
{
	return m_InternalData.Placement.matThisToLinked;
}
const VMatrix& CPSCollisionEntity::MatrixLinkedToThis() const
{
	return m_InternalData.Placement.matLinkedToThis;
}

const cplane_t& CPSCollisionEntity::GetPortalPlane() const
{
	return m_DataAccess.Placement.PortalPlane;
}

const PS_InternalData_t& CPSCollisionEntity::GetDataAccess() const
{
	return m_DataAccess;
}

const Vector& CPSCollisionEntity::GetVectorForward() const
{
	return m_DataAccess.Placement.vForward;
}
const Vector& CPSCollisionEntity::GetVectorUp() const
{
	return m_DataAccess.Placement.vUp;
}
const Vector& CPSCollisionEntity::GetVectorRight() const
{
	return m_DataAccess.Placement.vRight;
}

const PS_SD_Static_SurfaceProperties_t& CPSCollisionEntity::GetSurfaceProperties() const 
{
	return m_DataAccess.Simulation.Static.SurfaceProperties;
}

void CPSCollisionEntity::CreatePolyhedrons(void)
{
	//forward reverse conventions signify whether the normal is the same direction as m_InternalData.Placement.PortalPlane.m_Normal
//World and wall conventions signify whether it's been shifted in front of the portal plane or behind it

	float fWorldClipPlane_Forward[4] = { m_InternalData.Placement.PortalPlane.normal.x,
											m_InternalData.Placement.PortalPlane.normal.y,
											m_InternalData.Placement.PortalPlane.normal.z,
											m_InternalData.Placement.PortalPlane.dist + PORTAL_WORLD_WALL_HALF_SEPARATION_AMOUNT };

	float fWorldClipPlane_Reverse[4] = { -fWorldClipPlane_Forward[0],
											-fWorldClipPlane_Forward[1],
											-fWorldClipPlane_Forward[2],
											-fWorldClipPlane_Forward[3] };

	float fWallClipPlane_Forward[4] = { m_InternalData.Placement.PortalPlane.normal.x,
											m_InternalData.Placement.PortalPlane.normal.y,
											m_InternalData.Placement.PortalPlane.normal.z,
											m_InternalData.Placement.PortalPlane.dist }; // - PORTAL_WORLD_WALL_HALF_SEPARATION_AMOUNT

	//float fWallClipPlane_Reverse[4] = {		-fWallClipPlane_Forward[0],
	//										-fWallClipPlane_Forward[1],
	//										-fWallClipPlane_Forward[2],
	//										-fWallClipPlane_Forward[3] };


	//World
	{
		Vector vOBBForward = m_InternalData.Placement.vForward;
		Vector vOBBRight = m_InternalData.Placement.vRight;
		Vector vOBBUp = m_InternalData.Placement.vUp;


		//scale the extents to usable sizes
		float flScaleX = sv_portal_collision_sim_bounds_x.GetFloat();
		if (flScaleX < 200.0f)
			flScaleX = 200.0f;
		float flScaleY = sv_portal_collision_sim_bounds_y.GetFloat();
		if (flScaleY < 200.0f)
			flScaleY = 200.0f;
		float flScaleZ = sv_portal_collision_sim_bounds_z.GetFloat();
		if (flScaleZ < 252.0f)
			flScaleZ = 252.0f;

		vOBBForward *= flScaleX;
		vOBBRight *= flScaleY;
		vOBBUp *= flScaleZ;	// default size for scale z (252) is player (height + portal half height) * 2. Any smaller than this will allow for players to 
		// reach unsimulated geometry before an end touch with teh portal.

		Vector ptOBBOrigin = m_InternalData.Placement.ptCenter;
		ptOBBOrigin -= vOBBRight / 2.0f;
		ptOBBOrigin -= vOBBUp / 2.0f;

		Vector vAABBMins, vAABBMaxs;
		vAABBMins = vAABBMaxs = ptOBBOrigin;

		for (int i = 1; i != 8; ++i)
		{
			Vector ptTest = ptOBBOrigin;
			if (i & (1 << 0)) ptTest += vOBBForward;
			if (i & (1 << 1)) ptTest += vOBBRight;
			if (i & (1 << 2)) ptTest += vOBBUp;

			if (ptTest.x < vAABBMins.x) vAABBMins.x = ptTest.x;
			if (ptTest.y < vAABBMins.y) vAABBMins.y = ptTest.y;
			if (ptTest.z < vAABBMins.z) vAABBMins.z = ptTest.z;
			if (ptTest.x > vAABBMaxs.x) vAABBMaxs.x = ptTest.x;
			if (ptTest.y > vAABBMaxs.y) vAABBMaxs.y = ptTest.y;
			if (ptTest.z > vAABBMaxs.z) vAABBMaxs.z = ptTest.z;
		}

		//Brushes
		{
			Assert(m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count() == 0);

			CUtlVector<int> WorldBrushes;
			enginetrace->GetBrushesInAABB(vAABBMins, vAABBMaxs, &WorldBrushes, MASK_SOLID_BRUSHONLY | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP);

			//create locally clipped polyhedrons for the world
			{
				int* pBrushList = WorldBrushes.Base();
				int iBrushCount = WorldBrushes.Count();
				ConvertBrushListToClippedPolyhedronList(pBrushList, iBrushCount, fWorldClipPlane_Reverse, 1, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.World.Brushes.Polyhedrons);
			}
		}

		//static props
		{
			Assert(m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count() == 0);

			CUtlVector<ICollideable*> StaticProps;
			staticpropmgr->GetAllStaticPropsInAABB(vAABBMins, vAABBMaxs, &StaticProps);

			for (int i = StaticProps.Count(); --i >= 0; )
			{
				ICollideable* pProp = StaticProps[i];

				CPolyhedron* PolyhedronArray[1024];
				int iPolyhedronCount = g_StaticCollisionPolyhedronCache.GetStaticPropPolyhedrons(pProp, PolyhedronArray, 1024);

				StaticPropPolyhedronGroups_t indices;
				indices.iStartIndex = m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count();

				for (int j = 0; j != iPolyhedronCount; ++j)
				{
					CPolyhedron* pPropPolyhedronPiece = PolyhedronArray[j];
					if (pPropPolyhedronPiece)
					{
						CPolyhedron* pClippedPropPolyhedron = ClipPolyhedron(pPropPolyhedronPiece, fWorldClipPlane_Reverse, 1, 0.01f, false);
						if (pClippedPropPolyhedron)
							m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.AddToTail(pClippedPropPolyhedron);
					}
				}

				indices.iNumPolyhedrons = m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count() - indices.iStartIndex;
				if (indices.iNumPolyhedrons != 0)
				{
					int index = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.AddToTail();
					PS_SD_Static_World_StaticProps_ClippedProp_t& NewEntry = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[index];

					NewEntry.PolyhedronGroup = indices;
					NewEntry.pCollide = NULL;
#ifndef CLIENT_DLL
					NewEntry.pPhysicsObject = NULL;
#endif
					NewEntry.pSourceProp = pProp->GetEntityHandle();

					const model_t* pModel = pProp->GetCollisionModel();
					bool bIsStudioModel = pModel && (modelinfo->GetModelType(pModel) == mod_studio);
					AssertOnce(bIsStudioModel);
					if (bIsStudioModel)
					{
						IStudioHdr* pStudioHdr = modelinfo->GetStudiomodel(pModel);
						Assert(pStudioHdr != NULL);
						NewEntry.iTraceContents = pStudioHdr->contents();
						NewEntry.iTraceSurfaceProps = physprops->GetSurfaceIndex(pStudioHdr->pszSurfaceProp());
					}
					else
					{
						NewEntry.iTraceContents = m_InternalData.Simulation.Static.SurfaceProperties.contents;
						NewEntry.iTraceSurfaceProps = m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps;
					}
				}
			}
		}
	}



	//(Holy) Wall
	{
		Assert(m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count() == 0);
		Assert(m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count() == 0);

		Vector vBackward = -m_InternalData.Placement.vForward;
		Vector vLeft = -m_InternalData.Placement.vRight;
		Vector vDown = -m_InternalData.Placement.vUp;

		Vector vOBBForward = -m_InternalData.Placement.vForward;
		Vector vOBBRight = -m_InternalData.Placement.vRight;
		Vector vOBBUp = m_InternalData.Placement.vUp;

		//scale the extents to usable sizes
		vOBBForward *= PORTAL_WALL_FARDIST / 2.0f;
		vOBBRight *= PORTAL_WALL_FARDIST * 2.0f;
		vOBBUp *= PORTAL_WALL_FARDIST * 2.0f;

		Vector ptOBBOrigin = m_InternalData.Placement.ptCenter;
		ptOBBOrigin -= vOBBRight / 2.0f;
		ptOBBOrigin -= vOBBUp / 2.0f;

		Vector vAABBMins, vAABBMaxs;
		vAABBMins = vAABBMaxs = ptOBBOrigin;

		for (int i = 1; i != 8; ++i)
		{
			Vector ptTest = ptOBBOrigin;
			if (i & (1 << 0)) ptTest += vOBBForward;
			if (i & (1 << 1)) ptTest += vOBBRight;
			if (i & (1 << 2)) ptTest += vOBBUp;

			if (ptTest.x < vAABBMins.x) vAABBMins.x = ptTest.x;
			if (ptTest.y < vAABBMins.y) vAABBMins.y = ptTest.y;
			if (ptTest.z < vAABBMins.z) vAABBMins.z = ptTest.z;
			if (ptTest.x > vAABBMaxs.x) vAABBMaxs.x = ptTest.x;
			if (ptTest.y > vAABBMaxs.y) vAABBMaxs.y = ptTest.y;
			if (ptTest.z > vAABBMaxs.z) vAABBMaxs.z = ptTest.z;
		}


		float fPlanes[6 * 4];

		//first and second planes are always forward and backward planes
		fPlanes[(0 * 4) + 0] = fWallClipPlane_Forward[0];
		fPlanes[(0 * 4) + 1] = fWallClipPlane_Forward[1];
		fPlanes[(0 * 4) + 2] = fWallClipPlane_Forward[2];
		fPlanes[(0 * 4) + 3] = fWallClipPlane_Forward[3] - PORTAL_WALL_TUBE_OFFSET;

		fPlanes[(1 * 4) + 0] = vBackward.x;
		fPlanes[(1 * 4) + 1] = vBackward.y;
		fPlanes[(1 * 4) + 2] = vBackward.z;
		float fTubeDepthDist = vBackward.Dot(m_InternalData.Placement.ptCenter + (vBackward * (PORTAL_WALL_TUBE_DEPTH + PORTAL_WALL_TUBE_OFFSET)));
		fPlanes[(1 * 4) + 3] = fTubeDepthDist;


		//the remaining planes will always have the same ordering of normals, with different distances plugged in for each convex we're creating
		//normal order is up, down, left, right

		fPlanes[(2 * 4) + 0] = m_InternalData.Placement.vUp.x;
		fPlanes[(2 * 4) + 1] = m_InternalData.Placement.vUp.y;
		fPlanes[(2 * 4) + 2] = m_InternalData.Placement.vUp.z;
		fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + (m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT));

		fPlanes[(3 * 4) + 0] = vDown.x;
		fPlanes[(3 * 4) + 1] = vDown.y;
		fPlanes[(3 * 4) + 2] = vDown.z;
		fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter + (vDown * PORTAL_HOLE_HALF_HEIGHT));

		fPlanes[(4 * 4) + 0] = vLeft.x;
		fPlanes[(4 * 4) + 1] = vLeft.y;
		fPlanes[(4 * 4) + 2] = vLeft.z;
		fPlanes[(4 * 4) + 3] = vLeft.Dot(m_InternalData.Placement.ptCenter + (vLeft * PORTAL_HOLE_HALF_WIDTH));

		fPlanes[(5 * 4) + 0] = m_InternalData.Placement.vRight.x;
		fPlanes[(5 * 4) + 1] = m_InternalData.Placement.vRight.y;
		fPlanes[(5 * 4) + 2] = m_InternalData.Placement.vRight.z;
		fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter + (m_InternalData.Placement.vRight * PORTAL_HOLE_HALF_WIDTH));

		float* fSidePlanesOnly = &fPlanes[(2 * 4)];

		//these 2 get re-used a bit
		float fFarRightPlaneDistance = m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vRight * (PORTAL_WALL_FARDIST * 10.0f));
		float fFarLeftPlaneDistance = vLeft.Dot(m_InternalData.Placement.ptCenter + vLeft * (PORTAL_WALL_FARDIST * 10.0f));


		CUtlVector<int> WallBrushes;
		CUtlVector<CPolyhedron*> WallBrushPolyhedrons_ClippedToWall;
		CPolyhedron** pWallClippedPolyhedrons = NULL;
		int iWallClippedPolyhedronCount = 0;
		if (m_pOwningSimulator->IsSimulatingVPhysics()) //if not simulating vphysics, we skip making the entire wall, and just create the minimal tube instead
		{
			enginetrace->GetBrushesInAABB(vAABBMins, vAABBMaxs, &WallBrushes, MASK_SOLID_BRUSHONLY);

			if (WallBrushes.Count() != 0)
				ConvertBrushListToClippedPolyhedronList(WallBrushes.Base(), WallBrushes.Count(), fPlanes, 1, PORTAL_POLYHEDRON_CUT_EPSILON, &WallBrushPolyhedrons_ClippedToWall);

			if (WallBrushPolyhedrons_ClippedToWall.Count() != 0)
			{
				for (int i = WallBrushPolyhedrons_ClippedToWall.Count(); --i >= 0; )
				{
					CPolyhedron* pPolyhedron = ClipPolyhedron(WallBrushPolyhedrons_ClippedToWall[i], fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, true);
					if (pPolyhedron)
					{
						//a chunk of this brush passes through the hole, not eligible to be removed from cutting
						pPolyhedron->Release();
					}
					else
					{
						//no part of this brush interacts with the hole, no point in cutting the brush any later
						m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.AddToTail(WallBrushPolyhedrons_ClippedToWall[i]);
						WallBrushPolyhedrons_ClippedToWall.FastRemove(i);
					}
				}

				if (WallBrushPolyhedrons_ClippedToWall.Count() != 0) //might have become 0 while removing uncut brushes
				{
					pWallClippedPolyhedrons = WallBrushPolyhedrons_ClippedToWall.Base();
					iWallClippedPolyhedronCount = WallBrushPolyhedrons_ClippedToWall.Count();
				}
			}
		}


		//upper wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT);
			fPlanes[(4 * 4) + 3] = vLeft.Dot(m_InternalData.Placement.ptCenter + vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vUp * (PORTAL_WALL_FARDIST * 10.0f));
			fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;



			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//lower wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + (vDown * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter + vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(m_InternalData.Placement.ptCenter + vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + (vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter + (vDown * (PORTAL_WALL_FARDIST * 10.0f)));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//left wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + (m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter + (vDown * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(m_InternalData.Placement.ptCenter + (vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter + (vLeft * PORTAL_HOLE_HALF_WIDTH));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter - (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter + (vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS)));

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//right wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter + (vDown * (PORTAL_HOLE_HALF_HEIGHT)));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vRight * PORTAL_HOLE_HALF_WIDTH);
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(m_InternalData.Placement.ptCenter + (vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		for (int i = WallBrushPolyhedrons_ClippedToWall.Count(); --i >= 0; )
			WallBrushPolyhedrons_ClippedToWall[i]->Release();

		WallBrushPolyhedrons_ClippedToWall.RemoveAll();
		}
}

void CPSCollisionEntity::ClearPolyhedrons(void)
{
	if (m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count() != 0)
	{
		for (int i = m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count(); --i >= 0; )
			m_InternalData.Simulation.Static.World.Brushes.Polyhedrons[i]->Release();

		m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.RemoveAll();
	}

	if (m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count() != 0)
	{
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count(); --i >= 0; )
			m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons[i]->Release();

		m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.RemoveAll();
	}
#ifdef _DEBUG
	for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
	{
#ifndef CLIENT_DLL
		Assert(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pPhysicsObject == NULL);
#endif
		Assert(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pCollide == NULL);
	}
#endif
	m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.RemoveAll();

	if (m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count() != 0)
	{
		for (int i = m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count(); --i >= 0; )
			m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons[i]->Release();

		m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.RemoveAll();
	}

	if (m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count() != 0)
	{
		for (int i = m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count(); --i >= 0; )
			m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons[i]->Release();

		m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.RemoveAll();
	}
}

void CPSCollisionEntity::CreateLocalCollision(void)
{
	CREATEDEBUGTIMER(worldBrushTimer);
	STARTDEBUGTIMER(worldBrushTimer);
	Assert(m_InternalData.Simulation.Static.World.Brushes.pCollideable == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count() != 0)
		m_InternalData.Simulation.Static.World.Brushes.pCollideable = ConvertPolyhedronsToCollideable(m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Base(), m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count());
	STOPDEBUGTIMER(worldBrushTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sWorld Brushes=%fms\n", GetPortalSimulatorGUID(), TABSPACING, worldBrushTimer.GetDuration().GetMillisecondsF()); );

	CREATEDEBUGTIMER(worldPropTimer);
	STARTDEBUGTIMER(worldPropTimer);
#ifdef _DEBUG
	for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
	{
		Assert(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pCollide == NULL);
	}
#endif
	Assert(m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists == false); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0)
	{
		Assert(m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count() != 0);
		CPolyhedron** pPolyhedronsBase = m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Base();
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i];

			Assert(Representation.pCollide == NULL);
			Representation.pCollide = ConvertPolyhedronsToCollideable(&pPolyhedronsBase[Representation.PolyhedronGroup.iStartIndex], Representation.PolyhedronGroup.iNumPolyhedrons);
			Assert(Representation.pCollide != NULL);
		}
	}
	m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists = true;
	STOPDEBUGTIMER(worldPropTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sWorld Props=%fms\n", GetPortalSimulatorGUID(), TABSPACING, worldPropTimer.GetDuration().GetMillisecondsF()); );

	if (m_pOwningSimulator->IsSimulatingVPhysics())
	{
		//only need the tube when simulating player movement

		//TODO: replace the complete wall with the wall shell
		CREATEDEBUGTIMER(wallBrushTimer);
		STARTDEBUGTIMER(wallBrushTimer);
		Assert(m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count() != 0)
			m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable = ConvertPolyhedronsToCollideable(m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Base(), m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count());
		STOPDEBUGTIMER(wallBrushTimer);
		DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sWall Brushes=%fms\n", GetPortalSimulatorGUID(), TABSPACING, wallBrushTimer.GetDuration().GetMillisecondsF()); );
	}

	CREATEDEBUGTIMER(wallTubeTimer);
	STARTDEBUGTIMER(wallTubeTimer);
	Assert(m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count() != 0)
		m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable = ConvertPolyhedronsToCollideable(m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Base(), m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count());
	STOPDEBUGTIMER(wallTubeTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sWall Tube=%fms\n", GetPortalSimulatorGUID(), TABSPACING, wallTubeTimer.GetDuration().GetMillisecondsF()); );

	//grab surface properties to use for the portal environment
	{
		CTraceFilterWorldAndPropsOnly filter;
		trace_t Trace;
		UTIL_TraceLine(m_InternalData.Placement.ptCenter + m_InternalData.Placement.vForward, m_InternalData.Placement.ptCenter - (m_InternalData.Placement.vForward * 500.0f), MASK_SOLID_BRUSHONLY, &filter, &Trace);

		if (Trace.fraction != 1.0f)
		{
			m_InternalData.Simulation.Static.SurfaceProperties.contents = Trace.contents;
			m_InternalData.Simulation.Static.SurfaceProperties.surface = Trace.surface;
			m_InternalData.Simulation.Static.SurfaceProperties.pEntity = (CBaseEntity*)Trace.m_pEnt;
		}
		else
		{
			m_InternalData.Simulation.Static.SurfaceProperties.contents = CONTENTS_SOLID;
			m_InternalData.Simulation.Static.SurfaceProperties.surface.name = "**empty**";
			m_InternalData.Simulation.Static.SurfaceProperties.surface.flags = 0;
			m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps = 0;
#ifndef CLIENT_DLL
			m_InternalData.Simulation.Static.SurfaceProperties.pEntity = GetWorldEntity();
#else
			m_InternalData.Simulation.Static.SurfaceProperties.pEntity = GetClientWorldEntity();
#endif
		}

#ifndef CLIENT_DLL
		//if( pCollisionEntity )
		m_InternalData.Simulation.Static.SurfaceProperties.pEntity = this;
#endif		
	}
}

void CPSCollisionEntity::ClearLocalCollision(void)
{
	if (m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable)
	{
		physcollision->DestroyCollide(m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable);
		m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable = NULL;
	}

	if (m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable)
	{
		physcollision->DestroyCollide(m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable);
		m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable = NULL;
	}

	if (m_InternalData.Simulation.Static.World.Brushes.pCollideable)
	{
		physcollision->DestroyCollide(m_InternalData.Simulation.Static.World.Brushes.pCollideable);
		m_InternalData.Simulation.Static.World.Brushes.pCollideable = NULL;
	}

	if (m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists &&
		(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0) )
	{
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i];
			if (Representation.pCollide)
			{
				physcollision->DestroyCollide(Representation.pCollide);
				Representation.pCollide = NULL;
			}
		}
	}
	m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists = false;
}

void CPSCollisionEntity::CreateLocalPhysics(void)
{
	//int iDefaultSurfaceIndex = physprops->GetSurfaceIndex( "default" );
	objectparams_t params = g_PhysDefaultObjectParams;

	// Any non-moving object can point to world safely-- Make sure we dont use 'params' for something other than that beyond this point.
	//if( m_InternalData.Simulation.pCollisionEntity )
	params.pGameData = this;
	//else
	//	GetWorldEntity();

	//World
	{
		Assert(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.World.Brushes.pCollideable != NULL)
		{
			m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject = m_pOwningSimulator->pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.World.Brushes.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				GetEngineObject()->VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

			m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
		}

		//Assert( m_InternalData.Simulation.Static.World.StaticProps.PhysicsObjects.Count() == 0 ); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
#ifdef _DEBUG
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			Assert(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		}
#endif

		if (m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0)
		{
			Assert(m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists);
			for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
			{
				PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i];
				Assert(Representation.pCollide != NULL);
				Assert(Representation.pPhysicsObject == NULL);

				Representation.pPhysicsObject = m_pOwningSimulator->pPhysicsEnvironment->CreatePolyObjectStatic(Representation.pCollide, Representation.iTraceSurfaceProps, vec3_origin, vec3_angle, &params);
				Assert(Representation.pPhysicsObject != NULL);
				Representation.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
			}
		}
		m_InternalData.Simulation.Static.World.StaticProps.bPhysicsExists = true;
	}

	//Wall
	{
		Assert(m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable != NULL)
		{
			m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject = m_pOwningSimulator->pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				GetEngineObject()->VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

			m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
		}

		Assert(m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable != NULL)
		{
			m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject = m_pOwningSimulator->pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				GetEngineObject()->VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

			m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
		}
	}
}

void CPSCollisionEntity::CreateLinkedPhysics(CPSCollisionEntity* pRemoteCollisionEntity)
{
	//int iDefaultSurfaceIndex = physprops->GetSurfaceIndex( "default" );
	objectparams_t params = g_PhysDefaultObjectParams;

	//if( pCollisionEntity )
	params.pGameData = this;
	//else
	//	params.pGameData = GetWorldEntity();

	//everything in our linked collision should be based on the linked portal's world collision
	PS_SD_Static_World_t& RemoteSimulationStaticWorld = pRemoteCollisionEntity->m_InternalData.Simulation.Static.World;

	Assert(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (RemoteSimulationStaticWorld.Brushes.pCollideable != NULL)
	{
		m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject = m_pOwningSimulator->pPhysicsEnvironment->CreatePolyObjectStatic(RemoteSimulationStaticWorld.Brushes.pCollideable, pRemoteCollisionEntity->m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, m_InternalData.Placement.ptaap_LinkedToThis.ptOriginTransform, m_InternalData.Placement.ptaap_LinkedToThis.qAngleTransform, &params);
		m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
	}


	Assert(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count() == 0); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations.Count() != 0)
	{
		for (int i = RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations[i];
			IPhysicsObject* pPhysObject = m_pOwningSimulator->pPhysicsEnvironment->CreatePolyObjectStatic(Representation.pCollide, Representation.iTraceSurfaceProps, m_InternalData.Placement.ptaap_LinkedToThis.ptOriginTransform, m_InternalData.Placement.ptaap_LinkedToThis.qAngleTransform, &params);
			if (pPhysObject)
			{
				m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.AddToTail(pPhysObject);
				pPhysObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
			}
		}
	}
}

void CPSCollisionEntity::ClearLocalPhysics(void)
{
	if (m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject)
	{
		m_pOwningSimulator->pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);
		m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject = NULL;
	}

	if (m_InternalData.Simulation.Static.World.StaticProps.bPhysicsExists &&
		(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0))
	{
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i];
			if (Representation.pPhysicsObject)
			{
				m_pOwningSimulator->pPhysicsEnvironment->DestroyObject(Representation.pPhysicsObject);
				Representation.pPhysicsObject = NULL;
			}
		}
	}
	m_InternalData.Simulation.Static.World.StaticProps.bPhysicsExists = false;

	if (m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject)
	{
		m_pOwningSimulator->pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject);
		m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject = NULL;
	}

	if (m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject)
	{
		m_pOwningSimulator->pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject);
		m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject = NULL;
	}
}

void CPSCollisionEntity::ClearLinkedPhysics(void)
{
	//static collideables
	{
		if (m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject)
		{
			m_pOwningSimulator->pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject);
			m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject = NULL;
		}

		if (m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count())
		{
			for (int i = m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count(); --i >= 0; )
				m_pOwningSimulator->pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects[i]);

			m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.RemoveAll();
		}
	}
}

void CPSCollisionEntity::CreateHoleShapeCollideable()
{
	//update hole shape - used to detect if an entity is within the portal hole bounds
	{
		if (m_InternalData.Placement.pHoleShapeCollideable)
			physcollision->DestroyCollide(m_InternalData.Placement.pHoleShapeCollideable);

		float fHolePlanes[6 * 4];

		//first and second planes are always forward and backward planes
		fHolePlanes[(0 * 4) + 0] = m_InternalData.Placement.PortalPlane.normal.x;
		fHolePlanes[(0 * 4) + 1] = m_InternalData.Placement.PortalPlane.normal.y;
		fHolePlanes[(0 * 4) + 2] = m_InternalData.Placement.PortalPlane.normal.z;
		fHolePlanes[(0 * 4) + 3] = m_InternalData.Placement.PortalPlane.dist - 0.5f;

		fHolePlanes[(1 * 4) + 0] = -m_InternalData.Placement.PortalPlane.normal.x;
		fHolePlanes[(1 * 4) + 1] = -m_InternalData.Placement.PortalPlane.normal.y;
		fHolePlanes[(1 * 4) + 2] = -m_InternalData.Placement.PortalPlane.normal.z;
		fHolePlanes[(1 * 4) + 3] = (-m_InternalData.Placement.PortalPlane.dist) + 500.0f;


		//the remaining planes will always have the same ordering of normals, with different distances plugged in for each convex we're creating
		//normal order is up, down, left, right

		fHolePlanes[(2 * 4) + 0] = m_InternalData.Placement.vUp.x;
		fHolePlanes[(2 * 4) + 1] = m_InternalData.Placement.vUp.y;
		fHolePlanes[(2 * 4) + 2] = m_InternalData.Placement.vUp.z;
		fHolePlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter + (m_InternalData.Placement.vUp * (PORTAL_HALF_HEIGHT * 0.98f)));

		fHolePlanes[(3 * 4) + 0] = -m_InternalData.Placement.vUp.x;
		fHolePlanes[(3 * 4) + 1] = -m_InternalData.Placement.vUp.y;
		fHolePlanes[(3 * 4) + 2] = -m_InternalData.Placement.vUp.z;
		fHolePlanes[(3 * 4) + 3] = -m_InternalData.Placement.vUp.Dot(m_InternalData.Placement.ptCenter - (m_InternalData.Placement.vUp * (PORTAL_HALF_HEIGHT * 0.98f)));

		fHolePlanes[(4 * 4) + 0] = -m_InternalData.Placement.vRight.x;
		fHolePlanes[(4 * 4) + 1] = -m_InternalData.Placement.vRight.y;
		fHolePlanes[(4 * 4) + 2] = -m_InternalData.Placement.vRight.z;
		fHolePlanes[(4 * 4) + 3] = -m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter - (m_InternalData.Placement.vRight * (PORTAL_HALF_WIDTH * 0.98f)));

		fHolePlanes[(5 * 4) + 0] = m_InternalData.Placement.vRight.x;
		fHolePlanes[(5 * 4) + 1] = m_InternalData.Placement.vRight.y;
		fHolePlanes[(5 * 4) + 2] = m_InternalData.Placement.vRight.z;
		fHolePlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(m_InternalData.Placement.ptCenter + (m_InternalData.Placement.vRight * (PORTAL_HALF_WIDTH * 0.98f)));

		CPolyhedron* pPolyhedron = GeneratePolyhedronFromPlanes(fHolePlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON, true);
		Assert(pPolyhedron != NULL);
		CPhysConvex* pConvex = physcollision->ConvexFromConvexPolyhedron(*pPolyhedron);
		pPolyhedron->Release();
		Assert(pConvex != NULL);
		m_InternalData.Placement.pHoleShapeCollideable = physcollision->ConvertConvexToCollide(&pConvex, 1);
	}
}

void CPSCollisionEntity::ClearHoleShapeCollideable()
{
	if (m_InternalData.Placement.pHoleShapeCollideable) {
		physcollision->DestroyCollide(m_InternalData.Placement.pHoleShapeCollideable);
		m_InternalData.Placement.pHoleShapeCollideable = NULL;
	}
}

#ifdef GAME_DLL
bool CPortalSimulator::IsPortalSimulatorCollisionEntity( const CBaseEntity *pEntity )
{
	if (!((CBaseEntity*)pEntity)->IsNetworkable() || pEntity->entindex() == -1) {
		return false;
	}
	return s_PortalSimulatorCollisionEntities[pEntity->entindex()];
}
#endif // GAME_DLL


const Vector& CPortalSimulator::GetOrigin() const { return pCollisionEntity->GetOrigin(); }
const QAngle& CPortalSimulator::GetAngles() const { return pCollisionEntity->GetAngles(); }
const VMatrix& CPortalSimulator::MatrixThisToLinked() const { return pCollisionEntity->MatrixThisToLinked(); }
const VMatrix& CPortalSimulator::MatrixLinkedToThis() const { return pCollisionEntity->MatrixLinkedToThis(); }
const cplane_t& CPortalSimulator::GetPortalPlane() const { return pCollisionEntity->GetPortalPlane(); }
const PS_InternalData_t& CPortalSimulator::GetDataAccess() const { return pCollisionEntity->GetDataAccess(); }
const Vector& CPortalSimulator::GetVectorForward() const { return pCollisionEntity->GetVectorForward(); }
const Vector& CPortalSimulator::GetVectorUp() const { return pCollisionEntity->GetVectorUp(); }
const Vector& CPortalSimulator::GetVectorRight() const { return pCollisionEntity->GetVectorRight(); }
const PS_SD_Static_SurfaceProperties_t& CPortalSimulator::GetSurfaceProperties() const { return pCollisionEntity->GetSurfaceProperties(); }


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
	int vertCount = physcollision->CreateDebugMesh( pCollide, &outVerts );
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
	physcollision->DestroyDebugMesh( vertCount, outVerts );
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














