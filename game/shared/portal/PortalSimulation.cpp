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

extern ConVar sv_portal_trace_vs_world;
extern ConVar sv_portal_trace_vs_displacements;
extern ConVar sv_portal_trace_vs_holywall;
extern ConVar sv_portal_trace_vs_staticprops;
extern ConVar sv_use_transformed_collideables;
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
	m_bInCrossLinkedFunction(false)
	//,m_pCallbacks(&s_DummyPortalSimulatorCallback)
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


void CPortalSimulator::UpdateOnRemove(void)
{
	//go assert crazy here
	DetachFromLinked();
	ClearEverything();

	if (pCollisionEntity.Get()) {
		pCollisionEntity->GetEnginePortal()->ClearHoleShapeCollideable();
	}

#ifndef CLIENT_DLL
	if (pCollisionEntity.Get())
	{
		BeforeCollisionEntityDestroy();
		pCollisionEntity->m_pOwningSimulator = NULL;
		UTIL_Remove(pCollisionEntity);
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



void CPortalSimulator::MoveTo( const Vector &ptCenter, const QAngle &angles )
{
#ifdef GAME_DLL
	if( (pCollisionEntity->GetEngineObject()->GetAbsOrigin() == ptCenter) && (pCollisionEntity->GetEngineObject()->GetAbsAngles() == angles)) //not actually moving at all
		return;
#endif // GAME_DLL

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::MoveTo() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	BeforeMove();
	//update geometric data
	pCollisionEntity->GetEnginePortal()->MoveTo(ptCenter, angles);

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

	pCollisionEntity->GetEnginePortal()->CreateHoleShapeCollideable();

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
		pCollisionEntity->GetEnginePortal()->UpdateLinkMatrix(m_pLinkedPortal->pCollisionEntity->GetEnginePortal());
	}
	else
	{
		pCollisionEntity->GetEnginePortal()->UpdateLinkMatrix(NULL);
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

	return pCollisionEntity->GetEnginePortal()->EntityIsInPortalHole(pEntity->GetEngineObject());
}

bool CPortalSimulator::EntityHitBoxExtentIsInPortalHole( CBaseAnimating *pBaseAnimating ) const
{
	if( m_bLocalDataIsReady == false )
		return false;

	return pCollisionEntity->GetEnginePortal()->EntityHitBoxExtentIsInPortalHole(pBaseAnimating->GetEngineObject());
}

bool CPortalSimulator::RayIsInPortalHole(const Ray_t& ray) const
{
	return pCollisionEntity->GetEnginePortal()->RayIsInPortalHole(ray);
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
		if (pCollisionEntity->GetEnginePortal()->TraceWorldBrushes(ray, pTrace))
		{
			bCopyBackBrushTraceData = true;
		}

		if (bTraceHolyWall)
		{
			if (pCollisionEntity->GetEnginePortal()->TraceWallTube(ray, &TempTrace))
			{
				if ((TempTrace.startsolid == false) && (TempTrace.fraction < pTrace->fraction)) //never allow something to be stuck in the tube, it's more of a last-resort guide than a real collideable
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}

			if (pCollisionEntity->GetEnginePortal()->TraceWallBrushes(ray, &TempTrace))
			{
				if ((TempTrace.fraction < pTrace->fraction))
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}

			//if( portalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pCollideable && sv_portal_trace_vs_world.GetBool() )
			if (bTraceTransformedGeometry && pCollisionEntity->GetEnginePortal()->TraceTransformedWorldBrushes(pLinkedPortalSimulator->pCollisionEntity->GetEnginePortal(), ray, &TempTrace))
			{
				if ((TempTrace.fraction < pTrace->fraction))
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}
		}

		if (bCopyBackBrushTraceData)
		{
			pTrace->surface = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().surface;
			pTrace->contents = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().contents;
			pTrace->m_pEnt = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().pEntity;

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
				int iLocalStaticCount = pCollisionEntity->GetEnginePortal()->GetStaticPropsCount();
				if (iLocalStaticCount != 0 && pCollisionEntity->GetEnginePortal()->StaticPropsCollisionExists())
				{
					int iIndex = 0;
					Vector vTransform = vec3_origin;
					QAngle qTransform = vec3_angle;

					do
					{
						const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = pCollisionEntity->GetEnginePortal()->GetStaticProps(iIndex);
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

						++iIndex;
					} while (iIndex != iLocalStaticCount);
				}
			}

			if (bTraceHolyWall)
			{
				//remote clipped static props transformed into our wall space
				if (bTraceTransformedGeometry && (pTraceFilter->GetTraceType() != TRACE_WORLD_ONLY) && sv_portal_trace_vs_staticprops.GetBool())
				{
					int iLocalStaticCount = pLinkedPortalSimulator->pCollisionEntity->GetEnginePortal()->GetStaticPropsCount();
					if (iLocalStaticCount != 0)
					{
						int iIndex = 0;
						Vector vTransform = pCollisionEntity->GetEnginePortal()->GetTransformedOrigin();
						QAngle qTransform = pCollisionEntity->GetEnginePortal()->GetTransformedAngles();

						do
						{
							const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = pLinkedPortalSimulator->pCollisionEntity->GetEnginePortal()->GetStaticProps(iIndex);
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

							++iIndex;
						} while (iIndex != iLocalStaticCount);
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
		pTrace->contents = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().contents;
		pTrace->surface = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().surface;
		pTrace->m_pEnt = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().pEntity;
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
		if (pCollisionEntity->GetEnginePortal()->TraceWorldBrushes(entRay, &tempTrace))
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles, 
			//							pPortalSimulator->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, vec3_origin, vec3_angle, &tempTrace );
			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				*pTrace = tempTrace;
			}
		}

		//if( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pCollideable &&
		if (pLinkedPortalSimulator && pCollisionEntity->GetEnginePortal()->TraceTransformedWorldBrushes(pLinkedPortalSimulator->pCollisionEntity->GetEnginePortal(), entRay, &tempTrace))
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pLinkedPortalSimulator->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, &tempTrace );

			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				*pTrace = tempTrace;
			}
		}

		if (pCollisionEntity->GetEnginePortal()->TraceWallBrushes(entRay, &tempTrace))
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable, vec3_origin, vec3_angle, &tempTrace );

			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				if (tempTrace.fraction == 0.0f)
					tempTrace.startsolid = true;

				if (tempTrace.fractionleftsolid == 1.0f)
					tempTrace.allsolid = true;

				*pTrace = tempTrace;
			}
		}

		if (pCollisionEntity->GetEnginePortal()->TraceWallTube(entRay, &tempTrace))
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable, vec3_origin, vec3_angle, &tempTrace );

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
			pTrace->surface = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().surface;
			pTrace->contents = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().contents;
			pTrace->m_pEnt = pCollisionEntity->GetEnginePortal()->GetSurfaceProperties().pEntity;
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
				int iLocalStaticCount = pCollisionEntity->GetEnginePortal()->GetStaticPropsCount();
				if (iLocalStaticCount != 0 && pCollisionEntity->GetEnginePortal()->StaticPropsCollisionExists())
				{
					int iIndex = 0;
					Vector vTransform = vec3_origin;
					QAngle qTransform = vec3_angle;

					do
					{
						const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = pCollisionEntity->GetEnginePortal()->GetStaticProps(iIndex);
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

						++iIndex;
					} while (iIndex != iLocalStaticCount);
				}
			}

			if (pLinkedPortalSimulator && pCollisionEntity->GetEnginePortal()->EntityIsInPortalHole(pEntity->GetEngineObject()))
			{

#ifndef CLIENT_DLL
				if (sv_use_transformed_collideables.GetBool()) //if this never gets turned off, it should be removed before release
				{
					//moving entities near the remote portal
					CBaseEntity* pEnts[1024];
					int iEntCount = pLinkedPortalSimulator->GetMoveableOwnedEntities(pEnts, 1024);

					CTransformedCollideable transformedCollideable;
					transformedCollideable.m_matTransform = pLinkedPortalSimulator->pCollisionEntity->GetEnginePortal()->MatrixThisToLinked();
					transformedCollideable.m_matInvTransform = pLinkedPortalSimulator->pCollisionEntity->GetEnginePortal()->MatrixLinkedToThis();
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

	if(GetPhysicsEnvironment() != NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::CreateMinimumPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	pCollisionEntity->GetEnginePortal()->CreatePhysicsEnvironment();

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

	pCollisionEntity->GetEnginePortal()->CreateLocalPhysics();
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

	pCollisionEntity->GetEnginePortal()->CreateLinkedPhysics(m_pLinkedPortal->pCollisionEntity->GetEnginePortal());
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
	if(GetPhysicsEnvironment() == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearMinimumPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	pCollisionEntity->GetEnginePortal()->ClearPhysicsEnvironment();

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

	BeforeLocalPhysicsClear();

	if (pCollisionEntity.Get()) {
		pCollisionEntity->GetEnginePortal()->ClearLocalPhysics();
	}

	GetPhysicsEnvironment()->CleanupDeleteList();
	GetPhysicsEnvironment()->SetQuickDelete( false );

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

	if(GetPhysicsEnvironment() == NULL )
		return;

	CREATEDEBUGTIMER( functionTimer );

	STARTDEBUGTIMER( functionTimer );
	DEBUGTIMERONLY( DevMsg( 2, "[PSDT:%d] %sCPortalSimulator::ClearLinkedPhysics() START\n", GetPortalSimulatorGUID(), TABSPACING ); );
	INCREMENTTABSPACING();

	GetPhysicsEnvironment()->CleanupDeleteList();
	GetPhysicsEnvironment()->SetQuickDelete( true ); //if we don't do this, things crash the next time we cleanup the delete list while checking mindists

	BeforeLinkedPhysicsClear();

	if (pCollisionEntity.Get()) {
		pCollisionEntity->GetEnginePortal()->ClearLinkedPhysics();
	}

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
	
	pCollisionEntity->GetEnginePortal()->CreateLocalCollision();

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
		pCollisionEntity->GetEnginePortal()->ClearLocalCollision();
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

	pCollisionEntity->GetEnginePortal()->CreatePolyhedrons();

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
		pCollisionEntity->GetEnginePortal()->ClearPolyhedrons();
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

//void CPortalSimulator::SetPortalSimulatorCallbacks( CPortalSimulatorEventCallbacks *pCallbacks )
//{
//	if( pCallbacks )
//		m_pCallbacks = pCallbacks;
//	else
//		m_pCallbacks = &s_DummyPortalSimulatorCallback; //always keep the pointer valid
//}




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
		if( s_PortalSimulators[i]->pCollisionEntity->GetEnginePortal()->CreatedPhysicsObject( pObject, pOut_SourceType ) )
			return s_PortalSimulators[i];
	}
	
	return NULL;
}

bool CPortalSimulator::CreatedPhysicsObject( const IPhysicsObject *pObject, PS_PhysicsObjectSourceType_t *pOut_SourceType ) const
{
	return pCollisionEntity->GetEnginePortal()->CreatedPhysicsObject(pObject, pOut_SourceType);
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
{
	m_pOwningSimulator = NULL;
}

CPSCollisionEntity::~CPSCollisionEntity( void )
{
	if( m_pOwningSimulator )
	{
		Error("entindex() not valid in destruct");
		//m_pOwningSimulator->m_InternalData.Simulation.Dynamic.m_EntFlags[entindex()] &= ~PSEF_OWNS_PHYSICS;
		//m_pOwningSimulator->MarkAsReleased( this );
		//m_pOwningSimulator->m_InternalData.Simulation.pCollisionEntity = NULL;
		//m_pOwningSimulator = NULL;
	}
	//s_PortalSimulatorCollisionEntities[entindex()] = false;
}


void CPSCollisionEntity::UpdateOnRemove( void )
{
	GetEngineObject()->VPhysicsSetObject( NULL );
	GetEnginePortal()->ClearHoleShapeCollideable();
	GetEnginePortal()->ClearLinkedPhysics();
	GetEnginePortal()->ClearLocalPhysics();
	GetEnginePortal()->ClearLocalCollision();
	GetEnginePortal()->ClearPolyhedrons();
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

IPhysicsObject * CPSCollisionEntity::VPhysicsGetObject( void ) const
{
	if(GetEnginePortal()->GetWorldBrushesPhysicsObject() != NULL)
		return GetEnginePortal()->GetWorldBrushesPhysicsObject();
	else if(GetEnginePortal()->GetWallBrushesPhysicsObject() != NULL )
		return GetEnginePortal()->GetWallBrushesPhysicsObject();
	else if(GetEnginePortal()->GetWallTubePhysicsObject() != NULL )
		return GetEnginePortal()->GetWallTubePhysicsObject();
	else if(GetEnginePortal()->GetRemoteWallBrushesPhysicsObject() != NULL )
		return GetEnginePortal()->GetRemoteWallBrushesPhysicsObject();
	else
		return NULL;
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


//const Vector& CPortalSimulator::GetOrigin() const { return pCollisionEntity->GetOrigin(); }
//const QAngle& CPortalSimulator::GetAngles() const { return pCollisionEntity->GetAngles(); }
const VMatrix& CPortalSimulator::MatrixThisToLinked() const { return pCollisionEntity->GetEnginePortal()->MatrixThisToLinked(); }
const VMatrix& CPortalSimulator::MatrixLinkedToThis() const { return pCollisionEntity->GetEnginePortal()->MatrixLinkedToThis(); }
const cplane_t& CPortalSimulator::GetPortalPlane() const { return pCollisionEntity->GetEnginePortal()->GetPortalPlane(); }
//const PS_InternalData_t& CPortalSimulator::GetDataAccess() const { return pCollisionEntity->GetEnginePortal()->GetDataAccess(); }
const Vector& CPortalSimulator::GetVectorForward() const { return pCollisionEntity->GetEnginePortal()->GetVectorForward(); }
const Vector& CPortalSimulator::GetVectorUp() const { return pCollisionEntity->GetEnginePortal()->GetVectorUp(); }
const Vector& CPortalSimulator::GetVectorRight() const { return pCollisionEntity->GetEnginePortal()->GetVectorRight(); }
const PS_SD_Static_SurfaceProperties_t& CPortalSimulator::GetSurfaceProperties() const { return pCollisionEntity->GetEnginePortal()->GetSurfaceProperties(); }
IPhysicsEnvironment* CPortalSimulator::GetPhysicsEnvironment() { return pCollisionEntity->GetEnginePortal()->GetPhysicsEnvironment(); }

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














