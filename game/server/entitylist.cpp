//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "entitylist.h"
#ifdef _WIN32
#include "typeinfo"
// BUGBUG: typeinfo stomps some of the warning settings (in yvals.h)
#pragma warning(disable:4244)
#elif POSIX
#include <typeinfo>
#else
#error "need typeinfo defined"
#endif
#include "utlvector.h"
#include "igamesystem.h"
#include "collisionutils.h"
#include "UtlSortVector.h"
#include "tier0/vprof.h"
#include "mapentities.h"
#include "client.h"
#include "ai_initutils.h"
#include "globalstate.h"
#include "datacache/imdlcache.h"
#include "positionwatcher.h"
#include "mapentities_shared.h"
#include "tier1/mempool.h"
#include "tier1/callqueue.h"
#include "saverestore_utlvector.h"
#include "tier0/vcrmode.h"
#include "coordsize.h"
#include "physics_saverestore.h"
#include "animation.h"
#include "vphysics/constraints.h"
#include "mathlib/polyhedron.h"
#include "model_types.h"
#include "portal/weapon_physcannon.h" //grab controllers
#include "te_effect_dispatch.h"
#include "movevars_shared.h"
#include "vehicle_base.h"
#include "in_buttons.h"

#ifdef HL2_DLL
#include "npc_playercompanion.h"
#endif // HL2_DLL

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar ent_debugkeys;
extern ConVar think_limit;

//extern bool ParseKeyvalue(void* pObject, typedescription_t* pFields, int iNumFields, const char* szKeyName, const char* szValue);
extern bool ExtractKeyvalue(void* pObject, typedescription_t* pFields, int iNumFields, const char* szKeyName, char* szValue, int iMaxLen);
void SceneManager_ClientActive(CBasePlayer* player);

CGlobalEntityList<CBaseEntity> gEntList;
CBaseEntityList<CBaseEntity>* g_pEntityList = &gEntList;

// Expose list to engine
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CGlobalEntityList, IServerEntityList, VSERVERENTITYLIST_INTERFACE_VERSION, gEntList);

// Store local pointer to interface for rest of client .dll only 
//  (CClientEntityList instead of IClientEntityList )
CGlobalEntityList<CBaseEntity>* sv_entitylist = &gEntList;

//static servertouchlink_t *g_pNextLink = NULL;
int linksallocated = 0;
int groundlinksallocated = 0;
// memory pool for storing links between entities
static CUtlMemoryPool g_EdictTouchLinks(sizeof(servertouchlink_t), MAX_EDICTS, CUtlMemoryPool::GROW_NONE, "g_EdictTouchLinks");
static CUtlMemoryPool g_EntityGroundLinks(sizeof(servergroundlink_t), MAX_EDICTS, CUtlMemoryPool::GROW_NONE, "g_EntityGroundLinks");
#ifndef CLIENT_DLL
ConVar debug_touchlinks("debug_touchlinks", "0", 0, "Spew touch link activity");
#define DebugTouchlinks() debug_touchlinks.GetBool()
#else
#define DebugTouchlinks() false
#endif
#if !defined( CLIENT_DLL )
static ConVar sv_thinktimecheck("sv_thinktimecheck", "0", 0, "Check for thinktimes all on same timestamp.");
#endif
template<>
bool CGlobalEntityList<CBaseEntity>::sm_bDisableTouchFuncs = false;	// Disables PhysicsTouch and PhysicsStartTouch function calls
template<>
bool CGlobalEntityList<CBaseEntity>::sm_bAccurateTriggerBboxChecks = true;	// set to false for legacy behavior in ep1
bool g_bTestMoveTypeStepSimulation = true;
ConVar sv_teststepsimulation("sv_teststepsimulation", "1", 0);
ConVar step_spline("step_spline", "0");

// When this is false, throw an assert in debug when GetAbsAnything is called. Used when hierachy is incomplete/invalid.
bool CEngineObjectInternal::s_bAbsQueriesValid = true;

static ConVar sv_portal_collision_sim_bounds_x("sv_portal_collision_sim_bounds_x", "200", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!");
static ConVar sv_portal_collision_sim_bounds_y("sv_portal_collision_sim_bounds_y", "200", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!");
static ConVar sv_portal_collision_sim_bounds_z("sv_portal_collision_sim_bounds_z", "252", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!");
ConVar sv_portal_trace_vs_world("sv_portal_trace_vs_world", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment world geometry");
ConVar sv_portal_trace_vs_displacements("sv_portal_trace_vs_displacements", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment displacement geometry");
ConVar sv_portal_trace_vs_holywall("sv_portal_trace_vs_holywall", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment carved wall");
ConVar sv_portal_trace_vs_staticprops("sv_portal_trace_vs_staticprops", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment static prop geometry");
ConVar sv_use_transformed_collideables("sv_use_transformed_collideables", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Disables traces against remote portal moving entities using transforms to bring them into local space.");
ConVar sv_debug_physicsshadowclones("sv_debug_physicsshadowclones", "0", FCVAR_REPLICATED);
ConVar r_vehicleBrakeRate("r_vehicleBrakeRate", "1.5", FCVAR_CHEAT);
ConVar xbox_throttlebias("xbox_throttlebias", "100", FCVAR_ARCHIVE);
ConVar xbox_throttlespoof("xbox_throttlespoof", "200", FCVAR_ARCHIVE);
ConVar xbox_autothrottle("xbox_autothrottle", "1", FCVAR_ARCHIVE);
ConVar xbox_steering_deadzone("xbox_steering_deadzone", "0.0");

//-----------------------------------------------------------------------------
// Portal-specific hack designed to eliminate re-entrancy in touch functions
//-----------------------------------------------------------------------------
class CPortalTouchScope
{
public:
	CPortalTouchScope();
	~CPortalTouchScope();

public:
	static int m_nDepth;
	static CCallQueue m_CallQueue;
};

int CPortalTouchScope::m_nDepth = 0;
CCallQueue CPortalTouchScope::m_CallQueue;

CCallQueue* GetPortalCallQueue()
{
	return (CPortalTouchScope::m_nDepth > 0) ? &CPortalTouchScope::m_CallQueue : NULL;
}

CPortalTouchScope::CPortalTouchScope()
{
	++m_nDepth;
}

CPortalTouchScope::~CPortalTouchScope()
{
	Assert(m_nDepth >= 1);
	if (--m_nDepth == 0)
	{
		m_CallQueue.CallQueued();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : inline servertouchlink_t
//-----------------------------------------------------------------------------
inline servertouchlink_t* AllocTouchLink(void)
{
	servertouchlink_t* link = (servertouchlink_t*)g_EdictTouchLinks.Alloc(sizeof(servertouchlink_t));
	if (link)
	{
		++linksallocated;
	}
	else
	{
		DevWarning("AllocTouchLink: failed to allocate servertouchlink_t.\n");
	}

	return link;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *link - 
// Output : inline void
//-----------------------------------------------------------------------------
inline void FreeTouchLink(servertouchlink_t* link)
{
	if (link)
	{
		//if ( link == g_pNextLink )
		//{
		//	g_pNextLink = link->nextLink;
		//}
		--linksallocated;
		link->prevLink = link->nextLink = NULL;
	}

	// Necessary to catch crashes
	g_EdictTouchLinks.Free(link);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : inline groundlink_t
//-----------------------------------------------------------------------------
inline servergroundlink_t* AllocGroundLink(void)
{
	servergroundlink_t* link = (servergroundlink_t*)g_EntityGroundLinks.Alloc(sizeof(servergroundlink_t));
	if (link)
	{
		++groundlinksallocated;
	}
	else
	{
		DevMsg("AllocGroundLink: failed to allocate groundlink_t.!!!  groundlinksallocated=%d g_EntityGroundLinks.Count()=%d\n", groundlinksallocated, g_EntityGroundLinks.Count());
	}

#ifdef STAGING_ONLY
#ifndef CLIENT_DLL
	if (sv_groundlink_debug.GetBool())
	{
		UTIL_LogPrintf("Groundlink Alloc: %p at %d\n", link, groundlinksallocated);
	}
#endif
#endif // STAGING_ONLY

	return link;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *link - 
// Output : inline void
//-----------------------------------------------------------------------------
inline void FreeGroundLink(servergroundlink_t* link)
{
#ifdef STAGING_ONLY
#ifndef CLIENT_DLL
	if (sv_groundlink_debug.GetBool())
	{
		UTIL_LogPrintf("Groundlink Free: %p at %d\n", link, groundlinksallocated);
	}
#endif
#endif // STAGING_ONLY

	if (link)
	{
		--groundlinksallocated;
	}

	g_EntityGroundLinks.Free(link);
}

class CThinkContextsSaveDataOps : public CDefSaveRestoreOps
{
	virtual void Save(const SaveRestoreFieldInfo_t& fieldInfo, ISave* pSave)
	{
		AssertMsg(fieldInfo.pTypeDesc->fieldSize == 1, "CThinkContextsSaveDataOps does not support arrays");

		// Write out the vector
		CUtlVector< thinkfunc_t >* pUtlVector = (CUtlVector< thinkfunc_t > *)fieldInfo.pField;
		SaveUtlVector(pSave, pUtlVector, FIELD_EMBEDDED);

		// Get our owner
		CEngineObjectInternal* pOwner = (CEngineObjectInternal*)fieldInfo.pOwner;

		pSave->StartBlock();
		// Now write out all the functions
		for (int i = 0; i < pUtlVector->Size(); i++)
		{
			thinkfunc_t* thinkfun = &pUtlVector->Element(i);
#ifdef WIN32
			void** ppV = (void**)&((*pUtlVector)[i].m_pfnThink);
#else
			BASEPTR* ppV = &((*pUtlVector)[i].m_pfnThink);
#endif
			bool bHasFunc = (*ppV != NULL);
			pSave->WriteBool(&bHasFunc, 1);
			if (bHasFunc)
			{
				pSave->WriteFunction(pOwner->GetOuter()->GetDataDescMap(), "m_pfnThink", (inputfunc_t**)ppV, 1);
			}
		}
		pSave->EndBlock();
	}

	virtual void Restore(const SaveRestoreFieldInfo_t& fieldInfo, IRestore* pRestore)
	{
		AssertMsg(fieldInfo.pTypeDesc->fieldSize == 1, "CThinkContextsSaveDataOps does not support arrays");

		// Read in the vector
		CUtlVector< thinkfunc_t >* pUtlVector = (CUtlVector< thinkfunc_t > *)fieldInfo.pField;
		RestoreUtlVector(pRestore, pUtlVector, FIELD_EMBEDDED);

		// Get our owner
		CEngineObjectInternal* pOwner = (CEngineObjectInternal*)fieldInfo.pOwner;

		pRestore->StartBlock();
		// Now read in all the functions
		for (int i = 0; i < pUtlVector->Size(); i++)
		{
			thinkfunc_t* thinkfun = &pUtlVector->Element(i);
			bool bHasFunc;
			pRestore->ReadBool(&bHasFunc, 1);
#ifdef WIN32
			void** ppV = (void**)&((*pUtlVector)[i].m_pfnThink);
#else
			BASEPTR* ppV = &((*pUtlVector)[i].m_pfnThink);
			Q_memset((void*)ppV, 0x0, sizeof(inputfunc_t));
#endif
			if (bHasFunc)
			{
				SaveRestoreRecordHeader_t header;
				pRestore->ReadHeader(&header);
				pRestore->ReadFunction(pOwner->GetOuter()->GetDataDescMap(), (inputfunc_t**)ppV, 1, header.size);
			}
			else
			{
				*ppV = NULL;
			}
		}
		pRestore->EndBlock();
	}

	virtual bool IsEmpty(const SaveRestoreFieldInfo_t& fieldInfo)
	{
		CUtlVector< thinkfunc_t >* pUtlVector = (CUtlVector< thinkfunc_t > *)fieldInfo.pField;
		return (pUtlVector->Count() == 0);
	}

	virtual void MakeEmpty(const SaveRestoreFieldInfo_t& fieldInfo)
	{
		BASEPTR pFunc = *((BASEPTR*)fieldInfo.pField);
		pFunc = NULL;
	}
};

CThinkContextsSaveDataOps g_ThinkContextsSaveDataOps;
ISaveRestoreOps* thinkcontextFuncs = &g_ThinkContextsSaveDataOps;

BEGIN_SIMPLE_DATADESC(thinkfunc_t)
	DEFINE_FIELD(m_iszContext, FIELD_STRING),
	// DEFINE_FIELD( m_pfnThink,		FIELD_FUNCTION ),		// Manually written
	DEFINE_FIELD(m_nNextThinkTick, FIELD_TICK),
	DEFINE_FIELD(m_nLastThinkTick, FIELD_TICK),
END_DATADESC()

#define DEFINE_RAGDOLL_ELEMENT( i ) \
	DEFINE_FIELD( m_ragdoll.list[i].originParentSpace, FIELD_VECTOR ), \
	DEFINE_PHYSPTR( m_ragdoll.list[i].pObject ), \
	DEFINE_PHYSPTR( m_ragdoll.list[i].pConstraint ), \
	DEFINE_FIELD( m_ragdoll.list[i].parentIndex, FIELD_INTEGER )

BEGIN_DATADESC_NO_BASE(CEngineObjectInternal)
	DEFINE_FIELD(m_vecOrigin, FIELD_VECTOR),			// NOTE: MUST BE IN LOCAL SPACE, NOT POSITION_VECTOR!!! (see CBaseEntity::Restore)
	DEFINE_FIELD(m_angRotation, FIELD_VECTOR),
	DEFINE_KEYFIELD(m_vecVelocity, FIELD_VECTOR, "velocity"),
	DEFINE_FIELD(m_vecAbsOrigin, FIELD_POSITION_VECTOR),
	DEFINE_FIELD(m_angAbsRotation, FIELD_VECTOR),
	DEFINE_FIELD(m_vecAbsVelocity, FIELD_VECTOR),
	DEFINE_ARRAY( m_rgflCoordinateFrame, FIELD_FLOAT, 12 ), // NOTE: MUST BE IN LOCAL SPACE, NOT POSITION_VECTOR!!! (see CBaseEntity::Restore)
	DEFINE_GLOBAL_FIELD(m_hMoveParent, FIELD_EHANDLE),
	DEFINE_GLOBAL_FIELD(m_hMoveChild, FIELD_EHANDLE),
	DEFINE_GLOBAL_FIELD(m_hMovePeer, FIELD_EHANDLE),
	DEFINE_KEYFIELD(m_iClassname, FIELD_STRING, "classname"),
	DEFINE_GLOBAL_KEYFIELD(m_iGlobalname, FIELD_STRING, "globalname"),
	DEFINE_KEYFIELD(m_iParent, FIELD_STRING, "parentname"),
	DEFINE_FIELD(m_iName, FIELD_STRING),
	DEFINE_FIELD(m_iParentAttachment, FIELD_CHARACTER),
	DEFINE_FIELD(m_fFlags, FIELD_INTEGER),
	DEFINE_FIELD(m_iEFlags, FIELD_INTEGER),
	DEFINE_FIELD(touchStamp, FIELD_INTEGER),
	DEFINE_FIELD(m_hGroundEntity, FIELD_EHANDLE),
	DEFINE_FIELD(m_flGroundChangeTime, FIELD_TIME),
	DEFINE_GLOBAL_KEYFIELD(m_ModelName, FIELD_MODELNAME, "model"),
	DEFINE_GLOBAL_KEYFIELD(m_nModelIndex, FIELD_SHORT, "modelindex"),
	DEFINE_KEYFIELD(m_spawnflags, FIELD_INTEGER, "spawnflags"),
	DEFINE_EMBEDDED(m_Collision),
	DEFINE_FIELD(m_CollisionGroup, FIELD_INTEGER),
	DEFINE_KEYFIELD(m_fEffects, FIELD_INTEGER, "effects"),
	DEFINE_KEYFIELD(m_flGravity, FIELD_FLOAT, "gravity"),
	DEFINE_KEYFIELD(m_flFriction, FIELD_FLOAT, "friction"),
	DEFINE_FIELD(m_flElasticity, FIELD_FLOAT),
	DEFINE_FIELD(m_pfnThink, FIELD_FUNCTION),
	DEFINE_KEYFIELD(m_nNextThinkTick, FIELD_TICK, "nextthink"),
	DEFINE_FIELD(m_nLastThinkTick, FIELD_TICK),
	DEFINE_CUSTOM_FIELD(m_aThinkFunctions, thinkcontextFuncs),
	DEFINE_FIELD(m_MoveType, FIELD_CHARACTER),
	DEFINE_FIELD(m_MoveCollide, FIELD_CHARACTER),
	DEFINE_FIELD(m_bSimulatedEveryTick, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bAnimatedEveryTick, FIELD_BOOLEAN),
	DEFINE_FIELD(m_flAnimTime, FIELD_TIME),
	DEFINE_FIELD(m_flSimulationTime, FIELD_TIME),
	DEFINE_FIELD(m_bClientSideAnimation, FIELD_BOOLEAN),
	DEFINE_INPUT(m_nSkin, FIELD_INTEGER, "skin"),
	DEFINE_KEYFIELD(m_nBody, FIELD_INTEGER, "body"),
	DEFINE_INPUT(m_nBody, FIELD_INTEGER, "SetBodyGroup"),
	DEFINE_KEYFIELD(m_nHitboxSet, FIELD_INTEGER, "hitboxset"),
	DEFINE_FIELD(m_flModelScale, FIELD_FLOAT),
	DEFINE_KEYFIELD(m_flModelScale, FIELD_FLOAT, "modelscale"),
	DEFINE_ARRAY(m_flEncodedController, FIELD_FLOAT, NUM_BONECTRLS),
	DEFINE_FIELD(m_bClientSideFrameReset, FIELD_BOOLEAN),
	DEFINE_KEYFIELD(m_nSequence, FIELD_INTEGER, "sequence"),
	DEFINE_ARRAY(m_flPoseParameter, FIELD_FLOAT, NUM_POSEPAREMETERS),
	DEFINE_KEYFIELD(m_flPlaybackRate, FIELD_FLOAT, "playbackrate"),
	DEFINE_KEYFIELD(m_flCycle, FIELD_FLOAT, "cycle"),
	DEFINE_FIELD(m_nNewSequenceParity, FIELD_INTEGER),
	DEFINE_FIELD(m_nResetEventsParity, FIELD_INTEGER),
	DEFINE_FIELD(m_nMuzzleFlashParity, FIELD_CHARACTER),
	DEFINE_FIELD(m_flGroundSpeed, FIELD_FLOAT),
	DEFINE_FIELD(m_flLastEventCheck, FIELD_TIME),
	DEFINE_FIELD(m_bSequenceFinished, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bSequenceLoops, FIELD_BOOLEAN),
	DEFINE_FIELD(m_flSpeedScale, FIELD_FLOAT),
	DEFINE_PHYSPTR(m_pPhysicsObject),
	DEFINE_AUTO_ARRAY(m_ragdoll.boneIndex, FIELD_INTEGER),
	DEFINE_AUTO_ARRAY(m_ragPos, FIELD_POSITION_VECTOR),
	DEFINE_AUTO_ARRAY(m_ragAngles, FIELD_VECTOR),
	DEFINE_FIELD(m_ragdoll.listCount, FIELD_INTEGER),
	DEFINE_FIELD(m_ragdoll.allowStretch, FIELD_BOOLEAN),
	DEFINE_PHYSPTR(m_ragdoll.pGroup),
	DEFINE_FIELD(m_lastUpdateTickCount, FIELD_INTEGER),
	DEFINE_FIELD(m_allAsleep, FIELD_BOOLEAN),
	DEFINE_AUTO_ARRAY(m_ragdollMins, FIELD_VECTOR),
	DEFINE_AUTO_ARRAY(m_ragdollMaxs, FIELD_VECTOR),
	DEFINE_KEYFIELD(m_anglesOverrideString, FIELD_STRING, "angleOverride"),
	DEFINE_RAGDOLL_ELEMENT(1),
	DEFINE_RAGDOLL_ELEMENT(2),
	DEFINE_RAGDOLL_ELEMENT(3),
	DEFINE_RAGDOLL_ELEMENT(4),
	DEFINE_RAGDOLL_ELEMENT(5),
	DEFINE_RAGDOLL_ELEMENT(6),
	DEFINE_RAGDOLL_ELEMENT(7),
	DEFINE_RAGDOLL_ELEMENT(8),
	DEFINE_RAGDOLL_ELEMENT(9),
	DEFINE_RAGDOLL_ELEMENT(10),
	DEFINE_RAGDOLL_ELEMENT(11),
	DEFINE_RAGDOLL_ELEMENT(12),
	DEFINE_RAGDOLL_ELEMENT(13),
	DEFINE_RAGDOLL_ELEMENT(14),
	DEFINE_RAGDOLL_ELEMENT(15),
	DEFINE_RAGDOLL_ELEMENT(16),
	DEFINE_RAGDOLL_ELEMENT(17),
	DEFINE_RAGDOLL_ELEMENT(18),
	DEFINE_RAGDOLL_ELEMENT(19),
	DEFINE_RAGDOLL_ELEMENT(20),
	DEFINE_RAGDOLL_ELEMENT(21),
	DEFINE_RAGDOLL_ELEMENT(22),
	DEFINE_RAGDOLL_ELEMENT(23),
	DEFINE_KEYFIELD(m_nRenderFX, FIELD_CHARACTER, "renderfx"),

END_DATADESC()

void SendProxy_Origin(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* v;
	if (entity->entindex() == 1) {
		int aaa = 0;
	}
	if (!entity->UseStepSimulationNetworkOrigin(&v))
	{
		v = &entity->GetLocalOrigin();
	}

	pOut->m_Vector[0] = v->x;
	pOut->m_Vector[1] = v->y;
	pOut->m_Vector[2] = v->z;
}

//--------------------------------------------------------------------------------------------------------
// Used when breaking up origin, note we still have to deal with StepSimulation
//--------------------------------------------------------------------------------------------------------
void SendProxy_OriginXY(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* v;

	if (!entity->UseStepSimulationNetworkOrigin(&v))
	{
		v = &entity->GetLocalOrigin();
	}

	pOut->m_Vector[0] = v->x;
	pOut->m_Vector[1] = v->y;
}

//--------------------------------------------------------------------------------------------------------
// Used when breaking up origin, note we still have to deal with StepSimulation
//--------------------------------------------------------------------------------------------------------
void SendProxy_OriginZ(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* v;

	if (!entity->UseStepSimulationNetworkOrigin(&v))
	{
		v = &entity->GetLocalOrigin();
	}

	pOut->m_Float = v->z;
}

void SendProxy_Angles(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const QAngle* a;

	if (!entity->UseStepSimulationNetworkAngles(&a))
	{
		a = &entity->GetLocalAngles();
	}

	pOut->m_Vector[0] = anglemod(a->x);
	pOut->m_Vector[1] = anglemod(a->y);
	pOut->m_Vector[2] = anglemod(a->z);
}

void SendProxy_LocalVelocity(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* a = &entity->GetLocalVelocity();;

	pOut->m_Vector[0] = a->x;
	pOut->m_Vector[1] = a->y;
	pOut->m_Vector[2] = a->z;
}

void SendProxy_MoveParentToInt(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	CBaseEntity* pMoveParent = entity->GetMoveParent() ? entity->GetMoveParent()->GetOuter() : NULL;
	if (pMoveParent&& pMoveParent->entindex()==1) {
		int aaa = 0;
	}
	CBaseHandle pHandle = pMoveParent ? pMoveParent->GetRefEHandle() : NULL;;
	SendProxy_EHandleToInt(pProp, pStruct, &pHandle, pOut, iElement, objectID);
}

void SendProxy_CropFlagsToPlayerFlagBitsLength(const SendProp* pProp, const void* pStruct, const void* pVarData, DVariant* pOut, int iElement, int objectID)
{
	int mask = (1 << PLAYER_FLAG_BITS) - 1;
	int data = *(int*)pVarData;

	pOut->m_Int = (data & mask);
}

// This table encodes edict data.
void SendProxy_AnimTime(const SendProp* pProp, const void* pStruct, const void* pVarData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

#if defined( _DEBUG )
	Assert(!pEntity->IsUsingClientSideAnimation());
#endif

	int ticknumber = TIME_TO_TICKS(pEntity->m_flAnimTime);
	// Tickbase is current tick rounded down to closes 100 ticks
	int tickbase = gpGlobals->GetNetworkBase(gpGlobals->tickcount, pEntity->entindex());
	int addt = 0;
	// If it's within the last tick interval through the current one, then we can encode it
	if (ticknumber >= (tickbase - 100))
	{
		addt = (ticknumber - tickbase) & 0xFF;
	}

	pOut->m_Int = addt;
}

// This table encodes edict data.
void SendProxy_SimulationTime(const SendProp* pProp, const void* pStruct, const void* pVarData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

	int ticknumber = TIME_TO_TICKS(pEntity->m_flSimulationTime);
	// tickbase is current tick rounded down to closest 100 ticks
	int tickbase = gpGlobals->GetNetworkBase(gpGlobals->tickcount, pEntity->entindex());
	int addt = 0;
	if (ticknumber >= tickbase)
	{
		addt = (ticknumber - tickbase) & 0xff;
	}

	pOut->m_Int = addt;
}

void* SendProxy_ClientSideAnimation(const SendProp* pProp, const void* pStruct, const void* pVarData, CSendProxyRecipients* pRecipients, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

	if (!pEntity->IsUsingClientSideAnimation() && !pEntity->GetOuter()->IsViewModel())
		return (void*)pVarData;
	else
		return NULL;	// Don't send animtime unless the client needs it.
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER(SendProxy_ClientSideAnimation);


void* SendProxy_ClientSideSimulation(const SendProp* pProp, const void* pStruct, const void* pVarData, CSendProxyRecipients* pRecipients, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

	if (!pEntity->GetOuter()->IsViewModel())
		return (void*)pVarData;
	else
		return NULL;	// Don't send animtime unless the client needs it.
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER(SendProxy_ClientSideSimulation);

BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_AnimTimeMustBeFirst)
// NOTE:  Animtime must be sent before origin and angles ( from pev ) because it has a 
//  proxy on the client that stores off the old values before writing in the new values and
//  if it is sent after the new values, then it will only have the new origin and studio model, etc.
//  interpolation will be busted
	SendPropInt(SENDINFO(m_flAnimTime), 8, SPROP_UNSIGNED | SPROP_CHANGES_OFTEN | SPROP_ENCODED_AGAINST_TICKCOUNT, SendProxy_AnimTime),
END_SEND_TABLE()

BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_SimulationTimeMustBeFirst)
	SendPropInt(SENDINFO(m_flSimulationTime), SIMULATION_TIME_WINDOW_BITS, SPROP_UNSIGNED | SPROP_CHANGES_OFTEN | SPROP_ENCODED_AGAINST_TICKCOUNT, SendProxy_SimulationTime),
END_SEND_TABLE()

// Sendtable for fields we don't want to send to clientside animating entities
BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_ServerAnimationData)
	// ANIMATION_CYCLE_BITS is defined in shareddefs.h
	SendPropFloat(SENDINFO(m_flCycle), ANIMATION_CYCLE_BITS, SPROP_CHANGES_OFTEN | SPROP_ROUNDDOWN, 0.0f, 1.0f),
	SendPropArray3(SENDINFO_ARRAY3(m_flPoseParameter), SendPropFloat(SENDINFO_ARRAY(m_flPoseParameter), ANIMATION_POSEPARAMETER_BITS, 0, 0.0f, 1.0f)),
	SendPropFloat(SENDINFO(m_flPlaybackRate), ANIMATION_PLAYBACKRATE_BITS, SPROP_ROUNDUP, -4.0, 12.0f), // NOTE: if this isn't a power of 2 than "1.0" can't be encoded correctly
	SendPropInt(SENDINFO(m_nSequence), ANIMATION_SEQUENCE_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nNewSequenceParity), EF_PARITY_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nResetEventsParity), EF_PARITY_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nMuzzleFlashParity), EF_MUZZLEFLASH_BITS, SPROP_UNSIGNED),
END_SEND_TABLE()

void* SendProxy_ClientSideAnimationE(const SendProp* pProp, const void* pStruct, const void* pVarData, CSendProxyRecipients* pRecipients, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

	if (!pEntity->IsUsingClientSideAnimation())
		return (void*)pVarData;
	else
		return NULL;	// Don't send animtime unless the client needs it.
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER(SendProxy_ClientSideAnimationE);

BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_EngineObject)
	SendPropDataTable("AnimTimeMustBeFirst", 0, &REFERENCE_SEND_TABLE(DT_AnimTimeMustBeFirst), SendProxy_ClientSideAnimation),
	SendPropDataTable("SimulationTimeMustBeFirst", 0, &REFERENCE_SEND_TABLE(DT_SimulationTimeMustBeFirst), SendProxy_ClientSideSimulation),
	SendPropInt(SENDINFO(testNetwork), 32, SPROP_UNSIGNED),
#if PREDICTION_ERROR_CHECK_LEVEL > 1 
	SendPropVector(SENDINFO(m_vecOrigin), -1, SPROP_NOSCALE | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_Origin),
#else
	SendPropVector(SENDINFO(m_vecOrigin), -1, SPROP_COORD | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_Origin),
#endif
#if PREDICTION_ERROR_CHECK_LEVEL > 1 
	SendPropVector(SENDINFO(m_angRotation), -1, SPROP_NOSCALE | SPROP_CHANGES_OFTEN, 0, HIGH_DEFAULT, SendProxy_Angles),
#else
	SendPropQAngles(SENDINFO(m_angRotation), 13, SPROP_CHANGES_OFTEN, SendProxy_Angles),
#endif
	SendPropVector(SENDINFO(m_vecVelocity), 0, SPROP_NOSCALE, 0.0f, HIGH_DEFAULT, SendProxy_LocalVelocity),
	SendPropEHandle(SENDINFO_NAME(m_hMoveParent, moveparent), 0, SendProxy_MoveParentToInt),
	SendPropInt(SENDINFO(m_iParentAttachment), NUM_PARENTATTACHMENT_BITS, SPROP_UNSIGNED),
	SendPropEHandle(SENDINFO(m_hGroundEntity), SPROP_CHANGES_OFTEN),
	SendPropModelIndex(SENDINFO(m_nModelIndex)),
	SendPropDataTable(SENDINFO_DT(m_Collision), &REFERENCE_SEND_TABLE(DT_CollisionProperty)),
	SendPropInt(SENDINFO(m_CollisionGroup), 5, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_fFlags), PLAYER_FLAG_BITS, SPROP_UNSIGNED | SPROP_CHANGES_OFTEN, SendProxy_CropFlagsToPlayerFlagBitsLength),
	SendPropInt(SENDINFO(m_fEffects), EF_MAX_BITS, SPROP_UNSIGNED),
	SendPropFloat(SENDINFO(m_flFriction), 8, SPROP_ROUNDDOWN, 0.0f, 4.0f),
	SendPropFloat(SENDINFO(m_flElasticity), 0, SPROP_COORD),
	SendPropInt(SENDINFO(m_nNextThinkTick)),
	SendPropInt(SENDINFO_NAME(m_MoveType, movetype), MOVETYPE_MAX_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO_NAME(m_MoveCollide, movecollide), MOVECOLLIDE_MAX_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_bSimulatedEveryTick), 1, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_bAnimatedEveryTick), 1, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_bClientSideAnimation), 1, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nForceBone), 8, 0),
	SendPropVector(SENDINFO(m_vecForce), -1, SPROP_NOSCALE),
	SendPropInt(SENDINFO(m_nSkin), ANIMATION_SKIN_BITS),
	SendPropInt(SENDINFO(m_nBody), ANIMATION_BODY_BITS),

	SendPropInt(SENDINFO(m_nHitboxSet), ANIMATION_HITBOXSET_BITS, SPROP_UNSIGNED),

	SendPropFloat(SENDINFO(m_flModelScale)),
	SendPropArray3(SENDINFO_ARRAY3(m_flEncodedController), SendPropFloat(SENDINFO_ARRAY(m_flEncodedController), 11, SPROP_ROUNDDOWN, 0.0f, 1.0f)),
	SendPropInt(SENDINFO(m_bClientSideFrameReset), 1, SPROP_UNSIGNED),
	SendPropDataTable("serveranimdata", 0, &REFERENCE_SEND_TABLE(DT_ServerAnimationData), SendProxy_ClientSideAnimationE),
	SendPropArray(SendPropQAngles(SENDINFO_ARRAY(m_ragAngles), 13, 0), m_ragAngles),
	SendPropArray(SendPropVector(SENDINFO_ARRAY(m_ragPos), -1, SPROP_COORD), m_ragPos),
	SendPropInt(SENDINFO(m_nRenderFX), 8, SPROP_UNSIGNED),

END_SEND_TABLE()

IMPLEMENT_SERVERCLASS(CEngineObjectInternal, DT_EngineObject)

void CEngineObjectNetworkProperty::Init(CEngineObjectInternal* pEntity) {
	CServerNetworkProperty::Init();
	m_pOuter = pEntity;
}

int CEngineObjectNetworkProperty::entindex() const {
	return m_pOuter->GetOuter()->entindex();
}

SendTable* CEngineObjectNetworkProperty::GetSendTable() {
	return &DT_EngineObject::g_SendTable;
}

ServerClass* CEngineObjectNetworkProperty::GetServerClass() {
	return m_pOuter->GetServerClass();
}

void* CEngineObjectNetworkProperty::GetDataTableBasePtr() {
	return m_pOuter;
}

#include "tier0/memdbgoff.h"
//-----------------------------------------------------------------------------
// CBaseEntity new/delete
// allocates and frees memory for itself from the engine->
// All fields in the object are all initialized to 0.
//-----------------------------------------------------------------------------
void* CEngineObjectInternal::operator new(size_t stAllocateBlock)
{
	// call into engine to get memory
	Assert(stAllocateBlock != 0);
	return engine->PvAllocEntPrivateData(stAllocateBlock);
};

void* CEngineObjectInternal::operator new(size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine)
{
	// call into engine to get memory
	Assert(stAllocateBlock != 0);
	return engine->PvAllocEntPrivateData(stAllocateBlock);
}

void CEngineObjectInternal::operator delete(void* pMem)
{
	// get the engine to free the memory
	engine->FreeEntPrivateData(pMem);
}

#include "tier0/memdbgon.h"

FORCEINLINE bool NamesMatch(const char* pszQuery, string_t nameToMatch)
{
	if (nameToMatch == NULL_STRING)
		return (!pszQuery || *pszQuery == 0 || *pszQuery == '*');

	const char* pszNameToMatch = STRING(nameToMatch);

	// If the pointers are identical, we're identical
	if (pszNameToMatch == pszQuery)
		return true;

	while (*pszNameToMatch && *pszQuery)
	{
		unsigned char cName = *pszNameToMatch;
		unsigned char cQuery = *pszQuery;
		// simple ascii case conversion
		if (cName == cQuery)
			;
		else if (cName - 'A' <= (unsigned char)'Z' - 'A' && cName - 'A' + 'a' == cQuery)
			;
		else if (cName - 'a' <= (unsigned char)'z' - 'a' && cName - 'a' + 'A' == cQuery)
			;
		else
			break;
		++pszNameToMatch;
		++pszQuery;
	}

	if (*pszQuery == 0 && *pszNameToMatch == 0)
		return true;

	// @TODO (toml 03-18-03): Perhaps support real wildcards. Right now, only thing supported is trailing *
	if (*pszQuery == '*')
		return true;

	return false;
}

bool CEngineObjectInternal::NameMatchesComplex(const char* pszNameOrWildcard)
{
	if (!Q_stricmp("!player", pszNameOrWildcard))
		return m_pOuter->IsPlayer();

	return NamesMatch(pszNameOrWildcard, m_iName);
}

bool CEngineObjectInternal::ClassMatchesComplex(const char* pszClassOrWildcard)
{
	return NamesMatch(pszClassOrWildcard, m_iClassname);
}

inline bool CEngineObjectInternal::NameMatches(const char* pszNameOrWildcard)
{
	if (IDENT_STRINGS(m_iName, pszNameOrWildcard))
		return true;
	return NameMatchesComplex(pszNameOrWildcard);
}

inline bool CEngineObjectInternal::NameMatches(string_t nameStr)
{
	if (IDENT_STRINGS(m_iName, nameStr))
		return true;
	return NameMatchesComplex(STRING(nameStr));
}

inline bool CEngineObjectInternal::ClassMatches(const char* pszClassOrWildcard)
{
	if (IDENT_STRINGS(m_iClassname, pszClassOrWildcard))
		return true;
	return ClassMatchesComplex(pszClassOrWildcard);
}

inline bool CEngineObjectInternal::ClassMatches(string_t nameStr)
{
	if (IDENT_STRINGS(m_iClassname, nameStr))
		return true;
	return ClassMatchesComplex(STRING(nameStr));
}

//-----------------------------------------------------------------------------
// Purpose: Verifies that this entity's data description is valid in debug builds.
//-----------------------------------------------------------------------------
#ifdef _DEBUG
typedef CUtlVector< const char* >	KeyValueNameList_t;

static void AddDataMapFieldNamesToList(KeyValueNameList_t& list, datamap_t* pDataMap)
{
	while (pDataMap != NULL)
	{
		for (int i = 0; i < pDataMap->dataNumFields; i++)
		{
			typedescription_t* pField = &pDataMap->dataDesc[i];

			if (pField->fieldType == FIELD_EMBEDDED)
			{
				AddDataMapFieldNamesToList(list, pField->td);
				continue;
			}

			if (pField->flags & FTYPEDESC_KEY)
			{
				list.AddToTail(pField->externalName);
			}
		}

		pDataMap = pDataMap->baseMap;
	}
}

void CEngineObjectInternal::ValidateDataDescription(void)
{
	// Multiple key fields that have the same name are not allowed - it creates an
	// ambiguity when trying to parse keyvalues and outputs.
	datamap_t* pDataMap = GetDataDescMap();
	if ((pDataMap == NULL) || pDataMap->bValidityChecked)
		return;

	pDataMap->bValidityChecked = true;

	// Let's generate a list of all keyvalue strings in the entire hierarchy...
	KeyValueNameList_t	names(128);
	AddDataMapFieldNamesToList(names, pDataMap);

	for (int i = names.Count(); --i > 0; )
	{
		for (int j = i - 1; --j >= 0; )
		{
			if (!Q_stricmp(names[i], names[j]))
			{
				DevMsg("%s has multiple data description entries for \"%s\"\n", STRING(m_iClassname), names[i]);
				break;
			}
		}
	}
}
#endif // _DEBUG

//-----------------------------------------------------------------------------
// Purpose: iterates through a typedescript data block, so it can insert key/value data into the block
// Input  : *pObject - pointer to the struct or class the data is to be insterted into
//			*pFields - description of the data
//			iNumFields - number of fields contained in pFields
//			char *szKeyName - name of the variable to look for
//			char *szValue - value to set the variable to
// Output : Returns true if the variable is found and set, false if the key is not found.
//-----------------------------------------------------------------------------
bool ParseKeyvalue(void* pObject, typedescription_t* pFields, int iNumFields, const char* szKeyName, const char* szValue)
{
	int i;
	typedescription_t* pField;

	for (i = 0; i < iNumFields; i++)
	{
		pField = &pFields[i];

		int fieldOffset = pField->fieldOffset[TD_OFFSET_NORMAL];

		// Check the nested classes, but only if they aren't in array form.
		if ((pField->fieldType == FIELD_EMBEDDED) && (pField->fieldSize == 1))
		{
			for (datamap_t* dmap = pField->td; dmap != NULL; dmap = dmap->baseMap)
			{
				void* pEmbeddedObject = (void*)((char*)pObject + fieldOffset);
				if (ParseKeyvalue(pEmbeddedObject, dmap->dataDesc, dmap->dataNumFields, szKeyName, szValue))
					return true;
			}
		}

		if ((pField->flags & FTYPEDESC_KEY) && !stricmp(pField->externalName, szKeyName))
		{
			switch (pField->fieldType)
			{
			case FIELD_MODELNAME:
			case FIELD_SOUNDNAME:
			case FIELD_STRING:
				(*(string_t*)((char*)pObject + fieldOffset)) = AllocPooledString(szValue);
				return true;

			case FIELD_TIME:
			case FIELD_FLOAT:
				(*(float*)((char*)pObject + fieldOffset)) = atof(szValue);
				return true;

			case FIELD_BOOLEAN:
				(*(bool*)((char*)pObject + fieldOffset)) = (bool)(atoi(szValue) != 0);
				return true;

			case FIELD_CHARACTER:
				(*(char*)((char*)pObject + fieldOffset)) = (char)atoi(szValue);
				return true;

			case FIELD_SHORT:
				(*(short*)((char*)pObject + fieldOffset)) = (short)atoi(szValue);
				return true;

			case FIELD_INTEGER:
			case FIELD_TICK:
				(*(int*)((char*)pObject + fieldOffset)) = atoi(szValue);
				return true;

			case FIELD_POSITION_VECTOR:
			case FIELD_VECTOR:
				UTIL_StringToVector((float*)((char*)pObject + fieldOffset), szValue);
				return true;

			case FIELD_VMATRIX:
			case FIELD_VMATRIX_WORLDSPACE:
				UTIL_StringToFloatArray((float*)((char*)pObject + fieldOffset), 16, szValue);
				return true;

			case FIELD_MATRIX3X4_WORLDSPACE:
				UTIL_StringToFloatArray((float*)((char*)pObject + fieldOffset), 12, szValue);
				return true;

			case FIELD_COLOR32:
				UTIL_StringToColor32((color32*)((char*)pObject + fieldOffset), szValue);
				return true;

			case FIELD_CUSTOM:
			{
				SaveRestoreFieldInfo_t fieldInfo =
				{
					(char*)pObject + fieldOffset,
					pObject,
					pField
				};
				pField->pSaveRestoreOps->Parse(fieldInfo, szValue);
				return true;
			}

			default:
			case FIELD_INTERVAL: // Fixme, could write this if needed
			case FIELD_CLASSPTR:
			case FIELD_MODELINDEX:
			case FIELD_MATERIALINDEX:
			case FIELD_EDICT:
				Warning("Bad field in entity!!\n");
				Assert(0);
				break;
			}
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Handles keys and outputs from the BSP.
// Input  : mapData - Text block of keys and values from the BSP.
//-----------------------------------------------------------------------------
void CEngineObjectInternal::ParseMapData(IEntityMapData* mapData)
{
	char keyName[MAPKEY_MAXLENGTH];
	char value[MAPKEY_MAXLENGTH];

#ifdef _DEBUG
	ValidateDataDescription();
#endif // _DEBUG

	// loop through all keys in the data block and pass the info back into the object
	if (mapData->GetFirstKey(keyName, value))
	{
		do
		{
			if (!KeyValue(keyName, value)) {
				if (!m_pOuter->KeyValue(keyName, value)) {
					Msg("Entity %s has unparsed key: %s!\n", GetClassname(), keyName);
				}
			}
		} while (mapData->GetNextKey(keyName, value));
	}
}

//-----------------------------------------------------------------------------
// Parse data from a map file
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::KeyValue(const char* szKeyName, const char* szValue)
{
	//!! temp hack, until worldcraft is fixed
	// strip the # tokens from (duplicate) key names
	char* s = (char*)strchr(szKeyName, '#');
	if (s)
	{
		*s = '\0';
	}

	//if (FStrEq(szKeyName, "rendercolor") || FStrEq(szKeyName, "rendercolor32"))
	//{
	//	color32 tmp;
	//	UTIL_StringToColor32(&tmp, szValue);
	//	SetRenderColor(tmp.r, tmp.g, tmp.b);
	//	// don't copy alpha, legacy support uses renderamt
	//	return true;
	//}

	//if (FStrEq(szKeyName, "renderamt"))
	//{
	//	SetRenderColorA(atoi(szValue));
	//	return true;
	//}

	//if (FStrEq(szKeyName, "disableshadows"))
	//{
	//	int val = atoi(szValue);
	//	if (val)
	//	{
	//		AddEffects(EF_NOSHADOW);
	//	}
	//	return true;
	//}

	//if (FStrEq(szKeyName, "mins"))
	//{
	//	Vector mins;
	//	UTIL_StringToVector(mins.Base(), szValue);
	//	m_Collision.SetCollisionBounds(mins, OBBMaxs());
	//	return true;
	//}

	//if (FStrEq(szKeyName, "maxs"))
	//{
	//	Vector maxs;
	//	UTIL_StringToVector(maxs.Base(), szValue);
	//	m_Collision.SetCollisionBounds(OBBMins(), maxs);
	//	return true;
	//}

	//if (FStrEq(szKeyName, "disablereceiveshadows"))
	//{
	//	int val = atoi(szValue);
	//	if (val)
	//	{
	//		AddEffects(EF_NORECEIVESHADOW);
	//	}
	//	return true;
	//}

	//if (FStrEq(szKeyName, "nodamageforces"))
	//{
	//	int val = atoi(szValue);
	//	if (val)
	//	{
	//		AddEFlags(EFL_NO_DAMAGE_FORCES);
	//	}
	//	return true;
	//}

	// Fix up single angles
	if (FStrEq(szKeyName, "angle"))
	{
		static char szBuf[64];

		float y = atof(szValue);
		if (y >= 0)
		{
			Q_snprintf(szBuf, sizeof(szBuf), "%f %f %f", GetLocalAngles()[0], y, GetLocalAngles()[2]);
		}
		else if ((int)y == -1)
		{
			Q_strncpy(szBuf, "-90 0 0", sizeof(szBuf));
		}
		else
		{
			Q_strncpy(szBuf, "90 0 0", sizeof(szBuf));
		}

		// Do this so inherited classes looking for 'angles' don't have to bother with 'angle'
		return KeyValue("angles", szBuf);
	}

	// NOTE: Have to do these separate because they set two values instead of one
	if (FStrEq(szKeyName, "angles"))
	{
		QAngle angles;
		UTIL_StringToVector(angles.Base(), szValue);

		// If you're hitting this assert, it's probably because you're
		// calling SetLocalAngles from within a KeyValues method.. use SetAbsAngles instead!
		Assert((GetMoveParent() == NULL) && !IsEFlagSet(EFL_DIRTY_ABSTRANSFORM));
		SetAbsAngles(angles);
		return true;
	}

	if (FStrEq(szKeyName, "origin"))
	{
		Vector vecOrigin;
		UTIL_StringToVector(vecOrigin.Base(), szValue);

		// If you're hitting this assert, it's probably because you're
		// calling SetLocalOrigin from within a KeyValues method.. use SetAbsOrigin instead!
		Assert((GetMoveParent() == NULL) && !IsEFlagSet(EFL_DIRTY_ABSTRANSFORM));
		SetAbsOrigin(vecOrigin);
		return true;
	}

	if (FStrEq(szKeyName, "targetname"))
	{
		m_iName = AllocPooledString(szValue);
		return true;
	}

	// loop through the data description, and try and place the keys in
	if (!*ent_debugkeys.GetString())
	{
		for (datamap_t* dmap = GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap)
		{
			if (::ParseKeyvalue(this, dmap->dataDesc, dmap->dataNumFields, szKeyName, szValue))
				return true;
		}
	}
	else
	{
		// debug version - can be used to see what keys have been parsed in
		bool printKeyHits = false;
		const char* debugName = "";

		if (*ent_debugkeys.GetString() && !Q_stricmp(ent_debugkeys.GetString(), STRING(m_iClassname)))
		{
			// Msg( "-- found entity of type %s\n", STRING(m_iClassname) );
			printKeyHits = true;
			debugName = STRING(m_iClassname);
		}

		// loop through the data description, and try and place the keys in
		for (datamap_t* dmap = GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap)
		{
			if (!printKeyHits && *ent_debugkeys.GetString() && !Q_stricmp(dmap->dataClassName, ent_debugkeys.GetString()))
			{
				// Msg( "-- found class of type %s\n", dmap->dataClassName );
				printKeyHits = true;
				debugName = dmap->dataClassName;
			}

			if (::ParseKeyvalue(this, dmap->dataDesc, dmap->dataNumFields, szKeyName, szValue))
			{
				if (printKeyHits)
					Msg("(%s) key: %-16s value: %s\n", debugName, szKeyName, szValue);

				return true;
			}
		}

		if (printKeyHits)
			Msg("!! (%s) key not handled: \"%s\" \"%s\"\n", STRING(m_iClassname), szKeyName, szValue);
	}

	// key hasn't been handled
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Saves the current object out to disk, by iterating through the objects
//			data description hierarchy
// Input  : &save - save buffer which the class data is written to
// Output : int	- 0 if the save failed, 1 on success
//-----------------------------------------------------------------------------
int CEngineObjectInternal::Save(ISave& save)
{
	// loop through the data description list, saving each data desc block
	int status = save.WriteEntity(this->m_pOuter);

	return status;
}

//-----------------------------------------------------------------------------
// Purpose: Recursively saves all the classes in an object, in reverse order (top down)
// Output : int 0 on failure, 1 on success
//-----------------------------------------------------------------------------
//int CBaseEntity::SaveDataDescBlock( ISave &save, datamap_t *dmap )
//{
//	return save.WriteAll( this, dmap );
//}

//-----------------------------------------------------------------------------
// Purpose: Restores the current object from disk, by iterating through the objects
//			data description hierarchy
// Input  : &restore - restore buffer which the class data is read from
// Output : int	- 0 if the restore failed, 1 on success
//-----------------------------------------------------------------------------
int CEngineObjectInternal::Restore(IRestore& restore)
{
	// This is essential to getting the spatial partition info correct
	DestroyPartitionHandle();

	// loops through the data description list, restoring each data desc block in order
	int status = restore.ReadEntity(this->m_pOuter);;

	// ---------------------------------------------------------------
	// HACKHACK: We don't know the space of these vectors until now
	// if they are worldspace, fix them up.
	// ---------------------------------------------------------------
	{
		CGameSaveRestoreInfo* pGameInfo = restore.GetGameSaveRestoreInfo();
		Vector parentSpaceOffset = pGameInfo->modelSpaceOffset;
		if (!GetMoveParent())
		{
			// parent is the world, so parent space is worldspace
			// so update with the worldspace leveltransition transform
			parentSpaceOffset += pGameInfo->GetLandmark();
		}

		// NOTE: Do *not* use GetAbsOrigin() here because it will
		// try to recompute m_rgflCoordinateFrame!
		//MatrixSetColumn(GetEngineObject()->m_vecAbsOrigin, 3, GetEngineObject()->m_rgflCoordinateFrame );
		ResetRgflCoordinateFrame();

		m_vecOrigin += parentSpaceOffset;
	}

	// Gotta do this after the coordframe is set up as it depends on it.

	// By definition, the surrounding bounds are dirty
	// Also, twiddling with the flags here ensures it gets added to the KD tree dirty list
	// (We don't want to use the saved version of this flag)
	RemoveEFlags(EFL_DIRTY_SPATIAL_PARTITION);
	MarkSurroundingBoundsDirty();

	m_pModel = modelinfo->GetModel(GetModelIndex());
	if (m_pOuter->IsNetworkable() && entindex() != -1 && GetModelIndex() != 0 && GetModelName() != NULL_STRING && restore.GetPrecacheMode())
	{
		engine->PrecacheModel(STRING(GetModelName()));

		//Adrian: We should only need to do this after we precache. No point in setting the model again.
		SetModelIndex(modelinfo->GetModelIndex(STRING(GetModelName())));
	}

	// Restablish ground entity
	if (GetGroundEntity() != NULL)
	{
		GetGroundEntity()->AddEntityToGroundList(this);
	}
	if (GetModelScale() <= 0.0f)
		SetModelScale(1.0f);
	LockStudioHdr();
	return status;
}


//-----------------------------------------------------------------------------
// handler to do stuff before you are saved
//-----------------------------------------------------------------------------
void CEngineObjectInternal::OnSave(IEntitySaveUtils* pUtils)
{
	// Here, we must force recomputation of all abs data so it gets saved correctly
	// We can't leave the dirty bits set because the loader can't cope with it.
	CalcAbsolutePosition();
	CalcAbsoluteVelocity();
	if (m_ragdoll.listCount) {
		// Don't save ragdoll element 0, base class saves the pointer in 
		// m_pPhysicsObject
		Assert(m_ragdoll.list[0].parentIndex == -1);
		Assert(m_ragdoll.list[0].pConstraint == NULL);
		Assert(m_ragdoll.list[0].originParentSpace == vec3_origin);
		Assert(m_ragdoll.list[0].pObject != NULL);
		VPhysicsSetObject(NULL);	// squelch a warning message
		VPhysicsSetObject(m_ragdoll.list[0].pObject);	// make sure object zero is saved by CBaseEntity
	}
	m_pOuter->OnSave(pUtils);
}

//-----------------------------------------------------------------------------
// handler to do stuff after you are restored
//-----------------------------------------------------------------------------
void CEngineObjectInternal::OnRestore()
{
	SimThink_EntityChanged(this->m_pOuter);

	// touchlinks get recomputed
	if (IsEFlagSet(EFL_CHECK_UNTOUCH))
	{
		RemoveEFlags(EFL_CHECK_UNTOUCH);
		SetCheckUntouch(true);
	}

	if (GetMoveParent())
	{
		CEngineObjectInternal* pChild = GetMoveParent()->FirstMoveChild();
		while (pChild)
		{
			if (pChild == this)
				break;
			pChild = pChild->NextMovePeer();
		}
		if (pChild != this)
		{
#if _DEBUG
			// generally this means you've got something marked FCAP_DONT_SAVE
			// in a hierarchy.  That's probably ok given this fixup, but the hierarhcy
			// linked list is just saved/loaded in-place
			Warning("Fixing up parent on %s\n", GetClassname());
#endif
			// We only need to be back in the parent's list because we're already in the right place and with the right data
			GetMoveParent()->LinkChild(this);
			this->m_pOuter->AfterParentChanged(NULL);
		}
	}

	// We're not save/loading the PVS dirty state. Assume everything is dirty after a restore
	MarkPVSInformationDirty();
	NetworkStateChanged();
	if (m_ragdoll.listCount) {
		// rebuild element 0 since it isn't saved
		// NOTE: This breaks the rules - the pointer needs to get fixed in Restore()
		m_ragdoll.list[0].pObject = VPhysicsGetObject();
		m_ragdoll.list[0].parentIndex = -1;
		m_ragdoll.list[0].originParentSpace.Init();
		// JAY: Reset collision relationships
		RagdollSetupCollisions(m_ragdoll, modelinfo->GetVCollide(GetModelIndex()), GetModelIndex());
	}
	m_pOuter->OnRestore();
	m_pOuter->NetworkStateChanged();
}

//-----------------------------------------------------------------------------
// PVS rules
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::IsInPVS(const CBaseEntity* pRecipient, const void* pvs, int pvssize)
{
	RecomputePVSInformation();

	// ignore if not touching a PV leaf
	// negative leaf count is a node number
	// If no pvs, add any entity

	Assert(pvs && (GetOuter() != pRecipient));

	unsigned char* pPVS = (unsigned char*)pvs;

	if (m_PVSInfo.m_nClusterCount < 0)   // too many clusters, use headnode
	{
		return (engine->CheckHeadnodeVisible(m_PVSInfo.m_nHeadNode, pPVS, pvssize) != 0);
	}

	for (int i = m_PVSInfo.m_nClusterCount; --i >= 0; )
	{
		if (pPVS[m_PVSInfo.m_pClusters[i] >> 3] & (1 << (m_PVSInfo.m_pClusters[i] & 7)))
			return true;
	}

	return false;		// not visible
}


//-----------------------------------------------------------------------------
// PVS: this function is called a lot, so it avoids function calls
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::IsInPVS(const CCheckTransmitInfo* pInfo)
{
	// PVS data must be up to date
	//Assert( !m_pPev || ( ( m_pPev->m_fStateFlags & FL_EDICT_DIRTY_PVS_INFORMATION ) == 0 ) );

	int i;

	// Early out if the areas are connected
	if (!m_PVSInfo.m_nAreaNum2)
	{
		for (i = 0; i < pInfo->m_AreasNetworked; i++)
		{
			int clientArea = pInfo->m_Areas[i];
			if (clientArea == m_PVSInfo.m_nAreaNum || engine->CheckAreasConnected(clientArea, m_PVSInfo.m_nAreaNum))
				break;
		}
	}
	else
	{
		// doors can legally straddle two areas, so
		// we may need to check another one
		for (i = 0; i < pInfo->m_AreasNetworked; i++)
		{
			int clientArea = pInfo->m_Areas[i];
			if (clientArea == m_PVSInfo.m_nAreaNum || clientArea == m_PVSInfo.m_nAreaNum2)
				break;

			if (engine->CheckAreasConnected(clientArea, m_PVSInfo.m_nAreaNum))
				break;

			if (engine->CheckAreasConnected(clientArea, m_PVSInfo.m_nAreaNum2))
				break;
		}
	}

	if (i == pInfo->m_AreasNetworked)
	{
		// areas not connected
		return false;
	}

	// ignore if not touching a PV leaf
	// negative leaf count is a node number
	// If no pvs, add any entity

	Assert(entindex() != pInfo->m_pClientEnt);

	unsigned char* pPVS = (unsigned char*)pInfo->m_PVS;

	if (m_PVSInfo.m_nClusterCount < 0)   // too many clusters, use headnode
	{
		return (engine->CheckHeadnodeVisible(m_PVSInfo.m_nHeadNode, pPVS, pInfo->m_nPVSSize) != 0);
	}

	for (i = m_PVSInfo.m_nClusterCount; --i >= 0; )
	{
		int nCluster = m_PVSInfo.m_pClusters[i];
		if (((int)(pPVS[nCluster >> 3])) & BitVec_BitInByte(nCluster))
			return true;
	}

	return false;		// not visible

}

//-----------------------------------------------------------------------------
// PVS information
//-----------------------------------------------------------------------------
void CEngineObjectInternal::RecomputePVSInformation()
{
	if (m_bPVSInfoDirty/*((GetTransmitState() & FL_EDICT_DIRTY_PVS_INFORMATION) != 0)*/)// m_entindex!=-1 && 
	{
		//GetTransmitState() &= ~FL_EDICT_DIRTY_PVS_INFORMATION;
		m_bPVSInfoDirty = false;
		engine->BuildEntityClusterList(m_pOuter, &m_PVSInfo);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sets the movement parent of this entity. This entity will be moved
//			to a local coordinate calculated from its current absolute offset
//			from the parent entity and will then follow the parent entity.
// Input  : pParentEntity - This entity's new parent in the movement hierarchy.
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetParent(IEngineObjectServer* pParentEntity, int iAttachment)
{
	if (pParentEntity == this)
	{
		// should never set parent to 'this' - makes no sense
		Assert(0);
		pParentEntity = NULL;
	}
	// If they didn't specify an attachment, use our current
	if (iAttachment == -1)
	{
		iAttachment = m_iParentAttachment;
	}

	//bool bWasNotParented = (GetMoveParent() == NULL);
	CEngineObjectInternal* pOldParent = GetMoveParent();
	unsigned char iOldParentAttachment = m_iParentAttachment;

	this->m_pOuter->BeforeParentChanged(pParentEntity ? pParentEntity->GetOuter() : NULL, iAttachment);
	// notify the old parent of the loss
	this->UnlinkFromParent();
	m_iParent = NULL_STRING;
	m_iParentAttachment = 0;

	if (pParentEntity == NULL)
	{
		this->m_pOuter->AfterParentChanged(pOldParent ? pOldParent->m_pOuter : NULL, iOldParentAttachment);
		return;
	}

	RemoveSolidFlags(FSOLID_ROOT_PARENT_ALIGNED);
	if (const_cast<IEngineObjectServer*>(pParentEntity)->GetRootMoveParent()->GetSolid() == SOLID_BSP)
	{
		AddSolidFlags(FSOLID_ROOT_PARENT_ALIGNED);
	}
	else
	{
		if (GetSolid() == SOLID_BSP)
		{
			// Must be SOLID_VPHYSICS because parent might rotate
			SetSolid(SOLID_VPHYSICS);
		}
	}

	// set the move parent if we have one

	// add ourselves to the list
	pParentEntity->LinkChild(this);
	// set the new name
//m_pParent = pParentEntity;
	m_iParent = pParentEntity->GetEntityName();
	m_iParentAttachment = (char)iAttachment;
	this->m_pOuter->AfterParentChanged(pOldParent ? pOldParent->m_pOuter : NULL, iOldParentAttachment);

	EntityMatrix matrix, childMatrix;
	matrix.InitFromEntity(pParentEntity->GetOuter(), m_iParentAttachment); // parent->world
	childMatrix.InitFromEntityLocal(this->m_pOuter); // child->world
	Vector localOrigin = matrix.WorldToLocal(this->GetLocalOrigin());

	// I have the axes of local space in world space. (childMatrix)
	// I want to compute those world space axes in the parent's local space
	// and set that transform (as angles) on the child's object so the net
	// result is that the child is now in parent space, but still oriented the same way
	VMatrix tmp = matrix.Transpose(); // world->parent
	tmp.MatrixMul(childMatrix, matrix); // child->parent
	QAngle angles;
	MatrixToAngles(matrix, angles);
	this->SetLocalAngles(angles);
	//UTIL_SetOrigin(this->m_pOuter, localOrigin);
	this->SetLocalOrigin(localOrigin);

	if (m_pOuter->VPhysicsGetObject())
	{
		if (m_pOuter->VPhysicsGetObject()->IsStatic())
		{
			if (m_pOuter->VPhysicsGetObject()->IsAttachedToConstraint(false))
			{
				Warning("SetParent on static object, all constraints attached to %s (%s)will now be broken!\n", m_pOuter->GetDebugName(), m_pOuter->GetClassname());
			}
			VPhysicsDestroyObject();
			VPhysicsInitShadow(false, false);
		}
	}
	CollisionRulesChanged();
}


//-----------------------------------------------------------------------------
// Purpose: Does the linked list work of removing a child object from the hierarchy.
// Input  : pParent - 
//			pChild - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::UnlinkChild(IEngineObjectServer* pChild)
{
	CEngineObjectInternal* pList = this->FirstMoveChild();
	CEngineObjectInternal* pPrev = NULL;

	while (pList)
	{
		CEngineObjectInternal* pNext = pList->NextMovePeer();
		if (pList == pChild)
		{
			// patch up the list
			if (!pPrev) {
				this->SetFirstMoveChild(pNext);
			}
			else {
				pPrev->SetNextMovePeer(pNext);
			}

			// Clear hierarchy bits for this guy
			((CEngineObjectInternal*)pChild)->SetMoveParent(NULL);
			((CEngineObjectInternal*)pChild)->SetNextMovePeer(NULL);
			//pList->GetOuter()->NetworkProp()->SetNetworkParent( CBaseHandle() );
			pChild->GetOuter()->DispatchUpdateTransmitState();
			pChild->GetOuter()->OnEntityEvent(ENTITY_EVENT_PARENT_CHANGED, NULL);

			this->RecalcHasPlayerChildBit();
			return;
		}
		else
		{
			pPrev = pList;
			pList = pNext;
		}
	}

	// This only happens if the child wasn't found in the parent's child list
	Assert(0);
}

void CEngineObjectInternal::LinkChild(IEngineObjectServer* pChild)
{
	//EHANDLE hParent;
	//hParent.Set( pParent->GetOuter() );
	((CEngineObjectInternal*)pChild)->SetNextMovePeer(this->FirstMoveChild());
	this->SetFirstMoveChild(pChild);
	((CEngineObjectInternal*)pChild)->SetMoveParent(this);
	//pChild->GetOuter()->NetworkProp()->SetNetworkParent(pParent->GetOuter());
	pChild->GetOuter()->DispatchUpdateTransmitState();
	pChild->GetOuter()->OnEntityEvent(ENTITY_EVENT_PARENT_CHANGED, NULL);
	this->RecalcHasPlayerChildBit();
}

void CEngineObjectInternal::TransferChildren(IEngineObjectServer* pNewParent)
{
	CEngineObjectInternal* pChild = this->FirstMoveChild();
	while (pChild)
	{
		// NOTE: Have to do this before the unlink to ensure local coords are valid
		Vector vecAbsOrigin = pChild->GetAbsOrigin();
		QAngle angAbsRotation = pChild->GetAbsAngles();
		Vector vecAbsVelocity = pChild->GetAbsVelocity();
		//		QAngle vecAbsAngVelocity = pChild->GetAbsAngularVelocity();
		pChild->GetOuter()->BeforeParentChanged(pNewParent->GetOuter());
		UnlinkChild(pChild);
		pNewParent->LinkChild(pChild);
		pChild->GetOuter()->AfterParentChanged(this->GetOuter());

		// FIXME: This is a hack to guarantee update of the local origin, angles, etc.
		pChild->m_vecAbsOrigin.Init(FLT_MAX, FLT_MAX, FLT_MAX);
		pChild->m_angAbsRotation.Init(FLT_MAX, FLT_MAX, FLT_MAX);
		pChild->m_vecAbsVelocity.Init(FLT_MAX, FLT_MAX, FLT_MAX);

		pChild->SetAbsOrigin(vecAbsOrigin);
		pChild->SetAbsAngles(angAbsRotation);
		pChild->SetAbsVelocity(vecAbsVelocity);
		//		pChild->SetAbsAngularVelocity(vecAbsAngVelocity);

		pChild = this->FirstMoveChild();
	}
}

void CEngineObjectInternal::UnlinkFromParent()
{
	if (this->GetMoveParent())
	{
		// NOTE: Have to do this before the unlink to ensure local coords are valid
		Vector vecAbsOrigin = this->GetAbsOrigin();
		QAngle angAbsRotation = this->GetAbsAngles();
		Vector vecAbsVelocity = this->GetAbsVelocity();
		//		QAngle vecAbsAngVelocity = pRemove->GetAbsAngularVelocity();

		this->GetMoveParent()->UnlinkChild(this);

		this->SetLocalOrigin(vecAbsOrigin);
		this->SetLocalAngles(angAbsRotation);
		this->SetLocalVelocity(vecAbsVelocity);
		//		pRemove->SetLocalAngularVelocity(vecAbsAngVelocity);
		this->GetOuter()->UpdateWaterState();
	}
}


//-----------------------------------------------------------------------------
// Purpose: Clears the parent of all the children of the given object.
//-----------------------------------------------------------------------------
void CEngineObjectInternal::UnlinkAllChildren()
{
	CEngineObjectInternal* pChild = this->FirstMoveChild();
	while (pChild)
	{
		CEngineObjectInternal* pNext = pChild->NextMovePeer();
		pChild->UnlinkFromParent();
		pChild = pNext;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Calculates the absolute position of an edict in the world
//			assumes the parent's absolute origin has already been calculated
//-----------------------------------------------------------------------------
void CEngineObjectInternal::CalcAbsolutePosition(void)
{
	if (!IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
		return;

	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	// Plop the entity->parent matrix into m_rgflCoordinateFrame
	AngleMatrix(m_angRotation, m_vecOrigin, m_rgflCoordinateFrame);

	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		// no move parent, so just copy existing values
		m_vecAbsOrigin = m_vecOrigin;
		m_angAbsRotation = m_angRotation;
		if (HasDataObjectType(POSITIONWATCHER))
		{
			//ReportPositionChanged(this->m_pOuter);
			this->m_pOuter->NotifyPositionChanged();
		}
		return;
	}

	// concatenate with our parent's transform
	matrix3x4_t tmpMatrix, scratchSpace;
	ConcatTransforms(GetParentToWorldTransform(scratchSpace), m_rgflCoordinateFrame, tmpMatrix);
	MatrixCopy(tmpMatrix, m_rgflCoordinateFrame);

	// pull our absolute position out of the matrix
	MatrixGetColumn(m_rgflCoordinateFrame, 3, m_vecAbsOrigin);

	// if we have any angles, we have to extract our absolute angles from our matrix
	if ((m_angRotation == vec3_angle) && (m_iParentAttachment == 0))
	{
		// just copy our parent's absolute angles
		VectorCopy(pMoveParent->GetAbsAngles(), m_angAbsRotation);
	}
	else
	{
		MatrixAngles(m_rgflCoordinateFrame, m_angAbsRotation);
	}
	if (HasDataObjectType(POSITIONWATCHER))
	{
		//ReportPositionChanged(this->m_pOuter);
		this->m_pOuter->NotifyPositionChanged();
	}
}

void CEngineObjectInternal::CalcAbsoluteVelocity()
{
	if (!IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
		return;

	RemoveEFlags(EFL_DIRTY_ABSVELOCITY);

	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		m_vecAbsVelocity = m_vecVelocity;
		return;
	}

	// This transforms the local velocity into world space
	VectorRotate(m_vecVelocity, pMoveParent->EntityToWorldTransform(), m_vecAbsVelocity);

	// Now add in the parent abs velocity
	m_vecAbsVelocity += pMoveParent->GetAbsVelocity();
}

void CEngineObjectInternal::ComputeAbsPosition(const Vector& vecLocalPosition, Vector* pAbsPosition)
{
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		*pAbsPosition = vecLocalPosition;
	}
	else
	{
		VectorTransform(vecLocalPosition, pMoveParent->EntityToWorldTransform(), *pAbsPosition);
	}
}


//-----------------------------------------------------------------------------
// Computes the abs position of a point specified in local space
//-----------------------------------------------------------------------------
void CEngineObjectInternal::ComputeAbsDirection(const Vector& vecLocalDirection, Vector* pAbsDirection)
{
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		*pAbsDirection = vecLocalDirection;
	}
	else
	{
		VectorRotate(vecLocalDirection, pMoveParent->EntityToWorldTransform(), *pAbsDirection);
	}
}

void CEngineObjectInternal::GetVectors(Vector* forward, Vector* right, Vector* up) const {
	m_pOuter->GetVectors(forward, right, up);
}

const matrix3x4_t& CEngineObjectInternal::GetParentToWorldTransform(matrix3x4_t& tempMatrix)
{
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		Assert(false);
		SetIdentityMatrix(tempMatrix);
		return tempMatrix;
	}

	if (m_iParentAttachment != 0)
	{
		MDLCACHE_CRITICAL_SECTION();

		CBaseAnimating* pAnimating = pMoveParent->m_pOuter->GetBaseAnimating();
		if (pAnimating && pAnimating->GetAttachment(m_iParentAttachment, tempMatrix))
		{
			return tempMatrix;
		}
	}

	// If we fall through to here, then just use the move parent's abs origin and angles.
	return pMoveParent->EntityToWorldTransform();
}


//-----------------------------------------------------------------------------
// These methods recompute local versions as well as set abs versions
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetAbsOrigin(const Vector& absOrigin)
{
	AssertMsg(absOrigin.IsValid(), "Invalid origin set");

	// This is necessary to get the other fields of m_rgflCoordinateFrame ok
	CalcAbsolutePosition();

	if (m_vecAbsOrigin == absOrigin)
		return;
	//m_pOuter->NetworkStateChanged(55551);

	// All children are invalid, but we are not
	InvalidatePhysicsRecursive(POSITION_CHANGED);
	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	m_vecAbsOrigin = absOrigin;

	MatrixSetColumn(absOrigin, 3, m_rgflCoordinateFrame);

	Vector vecNewOrigin;
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		vecNewOrigin = absOrigin;
	}
	else
	{
		matrix3x4_t tempMat;
		const matrix3x4_t& parentTransform = GetParentToWorldTransform(tempMat);

		// Moveparent case: transform the abs position into local space
		VectorITransform(absOrigin, parentTransform, vecNewOrigin);
	}

	if (m_vecOrigin != vecNewOrigin)
	{
		//m_pOuter->NetworkStateChanged(55554);
		m_vecOrigin = vecNewOrigin;
		SetSimulationTime(gpGlobals->curtime);
	}
}

void CEngineObjectInternal::SetAbsAngles(const QAngle& absAngles)
{
	// This is necessary to get the other fields of m_rgflCoordinateFrame ok
	CalcAbsolutePosition();

	// FIXME: The normalize caused problems in server code like momentary_rot_button that isn't
	//        handling things like +/-180 degrees properly. This should be revisited.
	//QAngle angleNormalize( AngleNormalize( absAngles.x ), AngleNormalize( absAngles.y ), AngleNormalize( absAngles.z ) );

	if (m_angAbsRotation == absAngles)
		return;
	//m_pOuter->NetworkStateChanged(55552);

	// All children are invalid, but we are not
	InvalidatePhysicsRecursive(ANGLES_CHANGED);
	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	m_angAbsRotation = absAngles;
	AngleMatrix(absAngles, m_rgflCoordinateFrame);
	MatrixSetColumn(m_vecAbsOrigin, 3, m_rgflCoordinateFrame);

	QAngle angNewRotation;
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		angNewRotation = absAngles;
	}
	else
	{
		if (m_angAbsRotation == pMoveParent->GetAbsAngles())
		{
			angNewRotation.Init();
		}
		else
		{
			// Moveparent case: transform the abs transform into local space
			matrix3x4_t worldToParent, localMatrix;
			MatrixInvert(pMoveParent->EntityToWorldTransform(), worldToParent);
			ConcatTransforms(worldToParent, m_rgflCoordinateFrame, localMatrix);
			MatrixAngles(localMatrix, angNewRotation);
		}
	}

	if (m_angRotation != angNewRotation)
	{
		//m_pOuter->NetworkStateChanged(55555);
		m_angRotation = angNewRotation;
		SetSimulationTime(gpGlobals->curtime);
	}
}

void CEngineObjectInternal::SetAbsVelocity(const Vector& vecAbsVelocity)
{
	if (m_vecAbsVelocity == vecAbsVelocity)
		return;
	//m_pOuter->NetworkStateChanged(55556);
	//m_pOuter->NetworkStateChanged(55553);
	// The abs velocity won't be dirty since we're setting it here
	// All children are invalid, but we are not
	InvalidatePhysicsRecursive(VELOCITY_CHANGED);
	RemoveEFlags(EFL_DIRTY_ABSVELOCITY);

	m_vecAbsVelocity = vecAbsVelocity;

	// NOTE: Do *not* do a network state change in this case.
	// m_vecVelocity is only networked for the player, which is not manual mode
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		m_vecVelocity = vecAbsVelocity;
		return;
	}

	// First subtract out the parent's abs velocity to get a relative
	// velocity measured in world space
	Vector relVelocity;
	VectorSubtract(vecAbsVelocity, pMoveParent->GetAbsVelocity(), relVelocity);

	// Transform relative velocity into parent space
	Vector vNew;
	VectorIRotate(relVelocity, pMoveParent->EntityToWorldTransform(), vNew);
	m_vecVelocity = vNew;
}

//-----------------------------------------------------------------------------
// Methods that modify local physics state, and let us know to compute abs state later
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetLocalOrigin(const Vector& origin)
{
	// Safety check against NaN's or really huge numbers
	if (!IsEntityPositionReasonable(origin))
	{
		if (CheckEmitReasonablePhysicsSpew())
		{
			Warning("Bad SetLocalOrigin(%f,%f,%f) on %s\n", origin.x, origin.y, origin.z, m_pOuter->GetDebugName());
		}
		Assert(false);
		return;
	}

	//	if ( !origin.IsValid() )
	//	{
	//		AssertMsg( 0, "Bad origin set" );
	//		return;
	//	}

	if (m_vecOrigin != origin)
	{
		// Sanity check to make sure the origin is valid.
#ifdef _DEBUG
		float largeVal = 1024 * 128;
		Assert(origin.x >= -largeVal && origin.x <= largeVal);
		Assert(origin.y >= -largeVal && origin.y <= largeVal);
		Assert(origin.z >= -largeVal && origin.z <= largeVal);
#endif
		//m_pOuter->NetworkStateChanged(55554);
		InvalidatePhysicsRecursive(POSITION_CHANGED);
		m_vecOrigin = origin;
		SetSimulationTime(gpGlobals->curtime);
	}
}

void CEngineObjectInternal::SetLocalAngles(const QAngle& angles)
{
	// NOTE: The angle normalize is a little expensive, but we can save
	// a bunch of time in interpolation if we don't have to invalidate everything
	// and sometimes it's off by a normalization amount

	// FIXME: The normalize caused problems in server code like momentary_rot_button that isn't
	//        handling things like +/-180 degrees properly. This should be revisited.
	//QAngle angleNormalize( AngleNormalize( angles.x ), AngleNormalize( angles.y ), AngleNormalize( angles.z ) );

	// Safety check against NaN's or really huge numbers
	if (!IsEntityQAngleReasonable(angles))
	{
		if (CheckEmitReasonablePhysicsSpew())
		{
			Warning("Bad SetLocalAngles(%f,%f,%f) on %s\n", angles.x, angles.y, angles.z, m_pOuter->GetDebugName());
		}
		Assert(false);
		return;
	}

	if (m_angRotation != angles)
	{
		//m_pOuter->NetworkStateChanged(55555);
		InvalidatePhysicsRecursive(ANGLES_CHANGED);
		m_angRotation = angles;
		SetSimulationTime(gpGlobals->curtime);
	}
}

void CEngineObjectInternal::SetLocalVelocity(const Vector& inVecVelocity)
{
	Vector vecVelocity = inVecVelocity;

	// Safety check against receive a huge impulse, which can explode physics
	switch (CheckEntityVelocity(vecVelocity))
	{
	case -1:
		Warning("Discarding SetLocalVelocity(%f,%f,%f) on %s\n", vecVelocity.x, vecVelocity.y, vecVelocity.z, m_pOuter->GetDebugName());
		Assert(false);
		return;
	case 0:
		if (CheckEmitReasonablePhysicsSpew())
		{
			Warning("Clamping SetLocalVelocity(%f,%f,%f) on %s\n", inVecVelocity.x, inVecVelocity.y, inVecVelocity.z, m_pOuter->GetDebugName());
		}
		break;
	}

	if (m_vecVelocity != vecVelocity)
	{
		//m_pOuter->NetworkStateChanged(55556);
		InvalidatePhysicsRecursive(VELOCITY_CHANGED);
		m_vecVelocity = vecVelocity;
	}
}

const Vector& CEngineObjectInternal::GetLocalVelocity() const
{
	return m_vecVelocity;
}

const Vector& CEngineObjectInternal::GetAbsVelocity()
{
	Assert(CEngineObjectInternal::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	}
	return m_vecAbsVelocity;
}

const Vector& CEngineObjectInternal::GetAbsVelocity() const
{
	Assert(CEngineObjectInternal::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	}
	return m_vecAbsVelocity;
}

//-----------------------------------------------------------------------------
// Physics state accessor methods
//-----------------------------------------------------------------------------

const Vector& CEngineObjectInternal::GetLocalOrigin(void) const
{
	return m_vecOrigin;
}

const QAngle& CEngineObjectInternal::GetLocalAngles(void) const
{
	return m_angRotation;
}

const Vector& CEngineObjectInternal::GetAbsOrigin(void)
{
	Assert(CEngineObjectInternal::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_vecAbsOrigin;
}

const Vector& CEngineObjectInternal::GetAbsOrigin(void) const
{
	Assert(CEngineObjectInternal::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_vecAbsOrigin;
}

const QAngle& CEngineObjectInternal::GetAbsAngles(void)
{
	Assert(CEngineObjectInternal::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_angAbsRotation;
}

const QAngle& CEngineObjectInternal::GetAbsAngles(void) const
{
	Assert(CEngineObjectInternal::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_angAbsRotation;
}

//-----------------------------------------------------------------------------
// Methods relating to traversing hierarchy
//-----------------------------------------------------------------------------
CEngineObjectInternal* CEngineObjectInternal::GetMoveParent(void)
{
	return m_hMoveParent.Get() ? (CEngineObjectInternal*)(m_hMoveParent.Get()->GetEngineObject()) : NULL;
}

void CEngineObjectInternal::SetMoveParent(IEngineObjectServer* hMoveParent) {
	m_hMoveParent = hMoveParent? hMoveParent->GetOuter():NULL;
	//this->NetworkStateChanged();
}

CEngineObjectInternal* CEngineObjectInternal::FirstMoveChild(void)
{
	return gEntList.GetBaseEntity(m_hMoveChild) ? (CEngineObjectInternal*)gEntList.GetBaseEntity(m_hMoveChild)->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetFirstMoveChild(IEngineObjectServer* hMoveChild) {
	m_hMoveChild = hMoveChild? hMoveChild->GetOuter():NULL;
}

CEngineObjectInternal* CEngineObjectInternal::NextMovePeer(void)
{
	return gEntList.GetBaseEntity(m_hMovePeer) ? (CEngineObjectInternal*)gEntList.GetBaseEntity(m_hMovePeer)->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetNextMovePeer(IEngineObjectServer* hMovePeer) {
	m_hMovePeer = hMovePeer? hMovePeer->GetOuter():NULL;
}

CEngineObjectInternal* CEngineObjectInternal::GetRootMoveParent()
{
	CEngineObjectInternal* pEntity = this;
	CEngineObjectInternal* pParent = this->GetMoveParent();
	while (pParent)
	{
		pEntity = pParent;
		pParent = pEntity->GetMoveParent();
	}

	return pEntity;
}

void CEngineObjectInternal::ResetRgflCoordinateFrame() {
	MatrixSetColumn(this->m_vecAbsOrigin, 3, this->m_rgflCoordinateFrame);
}

//-----------------------------------------------------------------------------
// Returns the entity-to-world transform
//-----------------------------------------------------------------------------
matrix3x4_t& CEngineObjectInternal::EntityToWorldTransform()
{
	Assert(CEngineObjectInternal::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		CalcAbsolutePosition();
	}
	return m_rgflCoordinateFrame;
}

const matrix3x4_t& CEngineObjectInternal::EntityToWorldTransform() const
{
	Assert(CEngineObjectInternal::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_rgflCoordinateFrame;
}

//-----------------------------------------------------------------------------
// Some helper methods that transform a point from entity space to world space + back
//-----------------------------------------------------------------------------
void CEngineObjectInternal::EntityToWorldSpace(const Vector& in, Vector* pOut) const
{
	if (const_cast<CEngineObjectInternal*>(this)->GetAbsAngles() == vec3_angle)
	{
		VectorAdd(in, const_cast<CEngineObjectInternal*>(this)->GetAbsOrigin(), *pOut);
	}
	else
	{
		VectorTransform(in, EntityToWorldTransform(), *pOut);
	}
}

void CEngineObjectInternal::WorldToEntitySpace(const Vector& in, Vector* pOut) const
{
	if (const_cast<CEngineObjectInternal*>(this)->GetAbsAngles() == vec3_angle)
	{
		VectorSubtract(in, const_cast<CEngineObjectInternal*>(this)->GetAbsOrigin(), *pOut);
	}
	else
	{
		VectorITransform(in, EntityToWorldTransform(), *pOut);
	}
}

void EntityTouch_Add(CBaseEntity* pEntity);

void CEngineObjectInternal::SetCheckUntouch(bool check)
{
	// Invalidate touchstamp
	if (check)
	{
		touchStamp++;
		if (!IsEFlagSet(EFL_CHECK_UNTOUCH))
		{
			AddEFlags(EFL_CHECK_UNTOUCH);
			EntityTouch_Add(this->GetOuter());
		}
	}
	else
	{
		RemoveEFlags(EFL_CHECK_UNTOUCH);
	}
}

bool CEngineObjectInternal::HasDataObjectType(int type) const
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	return (m_fDataObjectTypes & (1 << type)) ? true : false;
}

void CEngineObjectInternal::AddDataObjectType(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	m_fDataObjectTypes |= (1 << type);
}

void CEngineObjectInternal::RemoveDataObjectType(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	m_fDataObjectTypes &= ~(1 << type);
}

void* CEngineObjectInternal::GetDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	if (!HasDataObjectType(type))
		return NULL;
	return gEntList.GetDataObject(type, m_pOuter);
}

void* CEngineObjectInternal::CreateDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	AddDataObjectType(type);
	return gEntList.CreateDataObject(type, m_pOuter);
}

void CEngineObjectInternal::DestroyDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	if (!HasDataObjectType(type))
		return;
	gEntList.DestroyDataObject(type, m_pOuter);
	RemoveDataObjectType(type);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::DestroyAllDataObjects(void)
{
	int i;
	for (i = 0; i < NUM_DATAOBJECT_TYPES; i++)
	{
		if (HasDataObjectType(i))
		{
			DestroyDataObject(i);
		}
	}
}

//-----------------------------------------------------------------------------
// Invalidates the abs state of all children
//-----------------------------------------------------------------------------
void CEngineObjectInternal::InvalidatePhysicsRecursive(int nChangeFlags)
{
	// Main entry point for dirty flag setting for the 90% case
	// 1) If the origin changes, then we have to update abstransform, Shadow projection, PVS, KD-tree, 
	//    client-leaf system.
	// 2) If the angles change, then we have to update abstransform, Shadow projection,
	//    shadow render-to-texture, client-leaf system, and surrounding bounds. 
	//	  Children have to additionally update absvelocity, KD-tree, and PVS.
	//	  If the surrounding bounds actually update, when we also need to update the KD-tree and the PVS.
	// 3) If it's due to attachment, then all children who are attached to an attachment point
	//    are assumed to have dirty origin + angles.

	// Other stuff:
	// 1) Marking the surrounding bounds dirty will automatically mark KD tree + PVS dirty.

	int nDirtyFlags = 0;

	if (nChangeFlags & VELOCITY_CHANGED)
	{
		nDirtyFlags |= EFL_DIRTY_ABSVELOCITY;
	}

	if (nChangeFlags & POSITION_CHANGED)
	{
		nDirtyFlags |= EFL_DIRTY_ABSTRANSFORM;

//#ifndef CLIENT_DLL
		MarkPVSInformationDirty();
//#endif
		m_pOuter->OnPositionChenged();
	}

	// NOTE: This has to be done after velocity + position are changed
	// because we change the nChangeFlags for the child entities
	if (nChangeFlags & ANGLES_CHANGED)
	{
		nDirtyFlags |= EFL_DIRTY_ABSTRANSFORM;
		m_pOuter->OnAnglesChanged();

		// This is going to be used for all children: children
		// have position + velocity changed
		nChangeFlags |= POSITION_CHANGED | VELOCITY_CHANGED;
	}

	AddEFlags(nDirtyFlags);

	// Set flags for children
	bool bOnlyDueToAttachment = false;
	if (nChangeFlags & ANIMATION_CHANGED)
	{
		m_pOuter->OnAnimationChanged();

		// Only set this flag if the only thing that changed us was the animation.
		// If position or something else changed us, then we must tell all children.
		if (!(nChangeFlags & (POSITION_CHANGED | VELOCITY_CHANGED | ANGLES_CHANGED)))
		{
			bOnlyDueToAttachment = true;
		}

		nChangeFlags = POSITION_CHANGED | ANGLES_CHANGED | VELOCITY_CHANGED;
	}

	for (CEngineObjectInternal* pChild = FirstMoveChild(); pChild; pChild = pChild->NextMovePeer())
	{
		// If this is due to the parent animating, only invalidate children that are parented to an attachment
		// Entities that are following also access attachments points on parents and must be invalidated.
		if (bOnlyDueToAttachment)
		{
			if (pChild->GetParentAttachment() == 0)
				continue;
		}
		pChild->InvalidatePhysicsRecursive(nChangeFlags);
	}

	//
	// This code should really be in here, or the bone cache should not be in world space.
	// Since the bone transforms are in world space, if we move or rotate the entity, its
	// bones should be marked invalid.
	//
	// As it is, we're near ship, and don't have time to setup a good A/B test of how much
	// overhead this fix would add. We've also only got one known case where the lack of
	// this fix is screwing us, and I just fixed it, so I'm leaving this commented out for now.
	//
	// Hopefully, we'll put the bone cache in entity space and remove the need for this fix.
	//
	//#ifdef CLIENT_DLL
	//	if ( nChangeFlags & (POSITION_CHANGED | ANGLES_CHANGED | ANIMATION_CHANGED) )
	//	{
	//		C_BaseAnimating *pAnim = GetBaseAnimating();
	//		if ( pAnim )
	//			pAnim->InvalidateBoneCache();		
	//	}
	//#endif
}

static trace_t g_TouchTrace;
static bool g_bCleanupDatObject = true;

const trace_t& CEngineObjectInternal::GetTouchTrace(void)
{
	return g_TouchTrace;
}

//-----------------------------------------------------------------------------
// Purpose: Two entities have touched, so run their touch functions
// Input  : *other - 
//			*ptrace - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsImpact(IEngineObjectServer* other, trace_t& trace)
{
	if (!other)
	{
		return;
	}

	// If either of the entities is flagged to be deleted, 
	//  don't call the touch functions
	if ((GetFlags() | other->GetFlags()) & FL_KILLME)
	{
		return;
	}

	PhysicsMarkEntitiesAsTouching(other, trace);
}

void CEngineObjectInternal::PhysicsTouchTriggers(const Vector* pPrevAbsOrigin)
{
	if (m_pOuter->IsNetworkable() && entindex() != -1 && !m_pOuter->IsWorld())
	{
		bool isTriggerCheckSolids = IsSolidFlagSet(FSOLID_TRIGGER);
		bool isSolidCheckTriggers = IsSolid() && !isTriggerCheckSolids;		// NOTE: Moving triggers (items, ammo etc) are not 
		// checked against other triggers to reduce the number of touchlinks created
		if (!(isSolidCheckTriggers || isTriggerCheckSolids))
			return;

		if (GetSolid() == SOLID_BSP)
		{
			if (!GetModel() && Q_strlen(STRING(GetModelName())) == 0)
			{
				Warning("Inserted %s with no model\n", GetClassname());
				return;
			}
		}

		SetCheckUntouch(true);
		if (isSolidCheckTriggers)
		{
			engine->SolidMoved(this->m_pOuter, &m_Collision, pPrevAbsOrigin, CGlobalEntityList<CBaseEntity>::sm_bAccurateTriggerBboxChecks);
		}
		if (isTriggerCheckSolids)
		{
			engine->TriggerMoved(this->m_pOuter, CGlobalEntityList<CBaseEntity>::sm_bAccurateTriggerBboxChecks);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Marks the fact that two edicts are in contact
// Input  : *other - other entity
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsMarkEntitiesAsTouching(IEngineObjectServer* other, trace_t& trace)
{
	g_TouchTrace = trace;
	PhysicsMarkEntityAsTouched(other);
	other->PhysicsMarkEntityAsTouched(this);
}


void CEngineObjectInternal::PhysicsMarkEntitiesAsTouchingEventDriven(IEngineObjectServer* other, trace_t& trace)
{
	g_TouchTrace = trace;
	g_TouchTrace.m_pEnt = other->GetOuter();

	servertouchlink_t* link;
	link = this->PhysicsMarkEntityAsTouched(other);
	if (link)
	{
		// mark these links as event driven so they aren't untouched the next frame
		// when the physics doesn't refresh them
		link->touchStamp = TOUCHSTAMP_EVENT_DRIVEN;
	}
	g_TouchTrace.m_pEnt = this->GetOuter();
	link = other->PhysicsMarkEntityAsTouched(this);
	if (link)
	{
		link->touchStamp = TOUCHSTAMP_EVENT_DRIVEN;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Marks in an entity that it is touching another entity, and calls
//			it's Touch() function if it is a new touch.
//			Stamps the touch link with the new time so that when we check for
//			untouch we know things haven't changed.
// Input  : *other - entity that it is in contact with
//-----------------------------------------------------------------------------
servertouchlink_t* CEngineObjectInternal::PhysicsMarkEntityAsTouched(IEngineObjectServer* other)
{
	servertouchlink_t* link;

	if (this == other)
		return NULL;

	// Entities in hierarchy should not interact
	if ((this->GetMoveParent() == other) || (this == other->GetMoveParent()))
		return NULL;

	// check if either entity doesn't generate touch functions
	if ((GetFlags() | other->GetFlags()) & FL_DONTTOUCH)
		return NULL;

	// Pure triggers should not touch each other
	if (IsSolidFlagSet(FSOLID_TRIGGER) && other->IsSolidFlagSet(FSOLID_TRIGGER))
	{
		if (!IsSolid() && !other->IsSolid())
			return NULL;
	}

	// Don't do touching if marked for deletion
	if (other->IsMarkedForDeletion())
	{
		return NULL;
	}

	if (IsMarkedForDeletion())
	{
		return NULL;
	}

#ifdef PORTAL
	CPortalTouchScope scope;
#endif

	// check if the edict is already in the list
	servertouchlink_t* root = (servertouchlink_t*)GetDataObject(TOUCHLINK);
	if (root)
	{
		for (link = root->nextLink; link != root; link = link->nextLink)
		{
			if (link->entityTouched == other->GetOuter())
			{
				// update stamp
				link->touchStamp = GetTouchStamp();

				if (!CGlobalEntityList<CBaseEntity>::sm_bDisableTouchFuncs)
				{
					PhysicsTouch(other);
				}

				// no more to do
				return link;
			}
		}
	}
	else
	{
		// Allocate the root object
		root = (servertouchlink_t*)CreateDataObject(TOUCHLINK);
		root->nextLink = root->prevLink = root;
	}

	// entity is not in list, so it's a new touch
	// add it to the touched list and then call the touch function

	// build new link
	link = AllocTouchLink();
	if (DebugTouchlinks())
		Msg("add 0x%p: %s-%s (%d-%d) [%d in play, %d max]\n", link, m_pOuter->GetDebugName(), other->GetOuter()->GetDebugName(), entindex(), other->GetOuter()->entindex(), linksallocated, g_EdictTouchLinks.PeakCount());
	if (!link)
		return NULL;

	link->touchStamp = GetTouchStamp();
	link->entityTouched = other->GetOuter();
	link->flags = 0;
	// add it to the list
	link->nextLink = root->nextLink;
	link->prevLink = root;
	link->prevLink->nextLink = link;
	link->nextLink->prevLink = link;

	// non-solid entities don't get touched
	bool bShouldTouch = (IsSolid() && !IsSolidFlagSet(FSOLID_VOLUME_CONTENTS)) || IsSolidFlagSet(FSOLID_TRIGGER);
	if (bShouldTouch && !other->IsSolidFlagSet(FSOLID_TRIGGER))
	{
		link->flags |= FTOUCHLINK_START_TOUCH;
		if (!CGlobalEntityList<CBaseEntity>::sm_bDisableTouchFuncs)
		{
			PhysicsStartTouch(other);
		}
	}

	return link;
}


//-----------------------------------------------------------------------------
// Purpose: Called every frame that two entities are touching
// Input  : *pentOther - the entity who it has touched
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsTouch(IEngineObjectServer* pentOther)
{
	if (pentOther)
	{
		if (!(IsMarkedForDeletion() || pentOther->IsMarkedForDeletion()))
		{
			m_pOuter->Touch(pentOther->GetOuter());
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called whenever two entities come in contact
// Input  : *pentOther - the entity who it has touched
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsStartTouch(IEngineObjectServer* pentOther)
{
	if (pentOther)
	{
		if (!(IsMarkedForDeletion() || pentOther->IsMarkedForDeletion()))
		{
			m_pOuter->StartTouch(pentOther->GetOuter());
			m_pOuter->Touch(pentOther->GetOuter());
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::IsCurrentlyTouching(void) const
{
	if (HasDataObjectType(TOUCHLINK))
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Checks to see if any entities that have been touching this one
//			have stopped touching it, and notify the entity if so.
//			Called at the end of a frame, after all the entities have run
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsCheckForEntityUntouch(void)
{
	//Assert( g_pNextLink == NULL );

	servertouchlink_t* link, * nextLink;

	servertouchlink_t* root = (servertouchlink_t*)this->GetDataObject(TOUCHLINK);
	if (root)
	{
#ifdef PORTAL
		CPortalTouchScope scope;
#endif
		bool saveCleanup = g_bCleanupDatObject;
		g_bCleanupDatObject = false;

		link = root->nextLink;
		while (link && link != root)
		{
			nextLink = link->nextLink;

			// these touchlinks are not polled.  The ents are touching due to an outside
			// system that will add/delete them as necessary (vphysics in this case)
			if (link->touchStamp == TOUCHSTAMP_EVENT_DRIVEN)
			{
				// refresh the touch call
				PhysicsTouch(((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetEngineObject());
			}
			else
			{
				// check to see if the touch stamp is up to date
				if (link->touchStamp != this->GetTouchStamp())
				{
					// stamp is out of data, so entities are no longer touching
					// remove self from other entities touch list
					((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetEngineObject()->PhysicsNotifyOtherOfUntouch(this);

					// remove other entity from this list
					this->PhysicsRemoveToucher(link);
				}
			}

			link = nextLink;
		}

		g_bCleanupDatObject = saveCleanup;

		// Nothing left in list, destroy root
		if (root->nextLink == root &&
			root->prevLink == root)
		{
			DestroyDataObject(TOUCHLINK);
		}
	}

	//g_pNextLink = NULL;

	SetCheckUntouch(false);
}

//-----------------------------------------------------------------------------
// Purpose: notifies an entity than another touching entity has moved out of contact.
// Input  : *other - the entity to be acted upon
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsNotifyOtherOfUntouch(IEngineObjectServer* ent)
{
	// loop through ed's touch list, looking for the notifier
	// remove and call untouch if found
	servertouchlink_t* root = (servertouchlink_t*)this->GetDataObject(TOUCHLINK);
	if (root)
	{
		servertouchlink_t* link = root->nextLink;
		while (link && link != root)
		{
			if (link->entityTouched == ent->GetOuter())
			{
				this->PhysicsRemoveToucher(link);

				// Check for complete removal
				if (g_bCleanupDatObject &&
					root->nextLink == root &&
					root->prevLink == root)
				{
					this->DestroyDataObject(TOUCHLINK);
				}
				return;
			}

			link = link->nextLink;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Clears all touches from the list
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsRemoveTouchedList()
{
#ifdef PORTAL
	CPortalTouchScope scope;
#endif

	servertouchlink_t* link, * nextLink;

	servertouchlink_t* root = (servertouchlink_t*)this->GetDataObject(TOUCHLINK);
	if (root)
	{
		link = root->nextLink;
		bool saveCleanup = g_bCleanupDatObject;
		g_bCleanupDatObject = false;
		while (link && link != root)
		{
			nextLink = link->nextLink;

			// notify the other entity that this ent has gone away
			if (gEntList.GetServerEntityFromHandle(link->entityTouched)) {
				((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetEngineObject()->PhysicsNotifyOtherOfUntouch(this);
			}

			// kill it
			if (DebugTouchlinks())
				Msg("remove 0x%p: %s-%s (%d-%d) [%d in play, %d max]\n", link, this->m_pOuter->GetDebugName(), ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetDebugName(), this->entindex(), ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->entindex(), linksallocated, g_EdictTouchLinks.PeakCount());
			FreeTouchLink(link);
			link = nextLink;
		}

		g_bCleanupDatObject = saveCleanup;
		this->DestroyDataObject(TOUCHLINK);
	}

	this->ClearTouchStamp();
}

//-----------------------------------------------------------------------------
// Purpose: removes a toucher from the list
// Input  : *link - the link to remove
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsRemoveToucher(servertouchlink_t* link)
{
	// Every start Touch gets a corresponding end touch
	if ((link->flags & FTOUCHLINK_START_TOUCH) &&
		link->entityTouched != INVALID_EHANDLE_INDEX)
	{
		CBaseEntity* pEntity = (CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched);
		this->m_pOuter->EndTouch(pEntity);
	}

	link->nextLink->prevLink = link->prevLink;
	link->prevLink->nextLink = link->nextLink;

	if (DebugTouchlinks())
		Msg("remove 0x%p: %s-%s (%d-%d) [%d in play, %d max]\n", link, ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetDebugName(), this->m_pOuter->GetDebugName(), ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->entindex(), this->entindex(), linksallocated, g_EdictTouchLinks.PeakCount());
	FreeTouchLink(link);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *other - 
// Output : groundlink_t
//-----------------------------------------------------------------------------
servergroundlink_t* CEngineObjectInternal::AddEntityToGroundList(IEngineObjectServer* other)
{
	servergroundlink_t* link;

	if (this == other)
		return NULL;

	// check if the edict is already in the list
	servergroundlink_t* root = (servergroundlink_t*)GetDataObject(GROUNDLINK);
	if (root)
	{
		for (link = root->nextLink; link != root; link = link->nextLink)
		{
			if (link->entity == other->GetOuter())
			{
				// no more to do
				return link;
			}
		}
	}
	else
	{
		root = (servergroundlink_t*)CreateDataObject(GROUNDLINK);
		root->prevLink = root->nextLink = root;
	}

	// entity is not in list, so it's a new touch
	// add it to the touched list and then call the touch function

	// build new link
	link = AllocGroundLink();
	if (!link)
		return NULL;

	link->entity = other->GetOuter();
	// add it to the list
	link->nextLink = root->nextLink;
	link->prevLink = root;
	link->prevLink->nextLink = link;
	link->nextLink->prevLink = link;

	PhysicsStartGroundContact(other);

	return link;
}

//-----------------------------------------------------------------------------
// Purpose: Called whenever two entities come in contact
// Input  : *pentOther - the entity who it has touched
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsStartGroundContact(IEngineObjectServer* pentOther)
{
	if (!pentOther)
		return;

	if (!(IsMarkedForDeletion() || pentOther->IsMarkedForDeletion()))
	{
		pentOther->GetOuter()->StartGroundContact(this->m_pOuter);
	}
}

//-----------------------------------------------------------------------------
// Purpose: notifies an entity than another touching entity has moved out of contact.
// Input  : *other - the entity to be acted upon
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsNotifyOtherOfGroundRemoval(IEngineObjectServer* ent)
{
	// loop through ed's touch list, looking for the notifier
	// remove and call untouch if found
	servergroundlink_t* root = (servergroundlink_t*)this->GetDataObject(GROUNDLINK);
	if (root)
	{
		servergroundlink_t* link = root->nextLink;
		while (link != root)
		{
			if (link->entity == ent->GetOuter())
			{
				PhysicsRemoveGround(link);

				if (root->nextLink == root &&
					root->prevLink == root)
				{
					this->DestroyDataObject(GROUNDLINK);
				}
				return;
			}

			link = link->nextLink;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: removes a toucher from the list
// Input  : *link - the link to remove
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsRemoveGround(servergroundlink_t* link)
{
	// Every start Touch gets a corresponding end touch
	if (link->entity != INVALID_EHANDLE_INDEX)
	{
		CBaseEntity* linkEntity = (CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entity);
		CBaseEntity* otherEntity = this->m_pOuter;
		if (linkEntity && otherEntity)
		{
			linkEntity->EndGroundContact(otherEntity);
		}
	}

	link->nextLink->prevLink = link->prevLink;
	link->prevLink->nextLink = link->nextLink;
	FreeGroundLink(link);
}

//-----------------------------------------------------------------------------
// Purpose: static method to remove ground list for an entity
// Input  : *ent - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsRemoveGroundList()
{
	servergroundlink_t* link, * nextLink;

	servergroundlink_t* root = (servergroundlink_t*)this->GetDataObject(GROUNDLINK);
	if (root)
	{
		link = root->nextLink;
		while (link && link != root)
		{
			nextLink = link->nextLink;

			// notify the other entity that this ent has gone away
			((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entity))->GetEngineObject()->PhysicsNotifyOtherOfGroundRemoval(this);

			// kill it
			FreeGroundLink(link);

			link = nextLink;
		}

		this->DestroyDataObject(GROUNDLINK);
	}
}

void CEngineObjectInternal::SetGroundEntity(IEngineObjectServer* ground)
{
	if ((m_hGroundEntity.Get() ? m_hGroundEntity.Get()->GetEngineObject() : NULL) == ground)
		return;

	// this can happen in-between updates to the held object controller (physcannon, +USE)
	// so trap it here and release held objects when they become player ground
	if (ground && m_pOuter->IsPlayer() && ground->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		CBasePlayer* pPlayer = static_cast<CBasePlayer*>(this->m_pOuter);
		IPhysicsObject* pPhysGround = ground->GetOuter()->VPhysicsGetObject();
		if (pPhysGround && pPlayer)
		{
			if (pPhysGround->GetGameFlags() & FVPHYSICS_PLAYER_HELD)
			{
				pPlayer->ForceDropOfCarriedPhysObjects(ground->GetOuter());
			}
		}
	}

	CBaseEntity* oldGround = m_hGroundEntity;
	m_hGroundEntity = ground ? ground->GetOuter() : NULL;

	// Just starting to touch
	if (!oldGround && ground)
	{
		ground->AddEntityToGroundList(this);
	}
	// Just stopping touching
	else if (oldGround && !ground)
	{
		oldGround->GetEngineObject()->PhysicsNotifyOtherOfGroundRemoval(this);
	}
	// Changing out to new ground entity
	else
	{
		oldGround->GetEngineObject()->PhysicsNotifyOtherOfGroundRemoval(this);
		ground->AddEntityToGroundList(this);
	}

	// HACK/PARANOID:  This is redundant with the code above, but in case we get out of sync groundlist entries ever, 
	//  this will force the appropriate flags
	if (ground)
	{
		AddFlag(FL_ONGROUND);
	}
	else
	{
		RemoveFlag(FL_ONGROUND);
	}
}

CEngineObjectInternal* CEngineObjectInternal::GetGroundEntity(void)
{
	return m_hGroundEntity.Get() ? (CEngineObjectInternal*)m_hGroundEntity.Get()->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetModelIndex(int index)
{
	//if ( IsDynamicModelIndex( index ) && !(GetBaseAnimating() && m_bDynamicModelAllowed) )
	//{
	//	AssertMsg( false, "dynamic model support not enabled on server entity" );
	//	index = -1;
	//}
		// delete exiting studio model container
	if (index != m_nModelIndex)
	{
		/*if ( m_bDynamicModelPending )
		{
			sg_DynamicLoadHandlers.Remove( this );
		}*/
		UnlockStudioHdr();
		m_pStudioHdr = NULL;
		//modelinfo->ReleaseDynamicModel( m_nModelIndex );
		//modelinfo->AddRefDynamicModel( index );
		m_nModelIndex = index;

		//m_bDynamicModelSetBounds = false;

		//if ( IsDynamicModelIndex( index ) )
		//{
		//	m_bDynamicModelPending = true;
		//	sg_DynamicLoadHandlers[ sg_DynamicLoadHandlers.Insert( this ) ].Register( index );
		//}
		//else
		//{
		//	m_bDynamicModelPending = false;
		//m_pOuter->OnNewModel();
		const model_t* pModel = modelinfo->GetModel(m_nModelIndex);
		SetModelPointer(pModel);
		//}
	}
	m_pOuter->DispatchUpdateTransmitState();
}

bool CEngineObjectInternal::Intersects(IEngineObjectServer* pOther)
{
	//if (entindex() == -1 || pOther->entindex() == -1)
	//	return false;
	return IsOBBIntersectingOBB(
		this->GetCollisionOrigin(), this->GetCollisionAngles(), this->OBBMins(), this->OBBMaxs(),
		pOther->GetCollisionOrigin(), pOther->GetCollisionAngles(), pOther->OBBMins(), pOther->OBBMaxs());
}

void CEngineObjectInternal::SetCollisionGroup(int collisionGroup)
{
	if ((int)m_CollisionGroup != collisionGroup)
	{
		m_CollisionGroup = collisionGroup;
		CollisionRulesChanged();
	}
}


void CEngineObjectInternal::CollisionRulesChanged()
{
	// ivp maintains state based on recent return values from the collision filter, so anything
	// that can change the state that a collision filter will return (like m_Solid) needs to call RecheckCollisionFilter.
	if (m_pOuter->VPhysicsGetObject())
	{
		extern bool PhysIsInCallback();
		if (PhysIsInCallback())
		{
			Warning("Changing collision rules within a callback is likely to cause crashes!\n");
			Assert(0);
		}
		IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = m_pOuter->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
		for (int i = 0; i < count; i++)
		{
			if (pList[i] != NULL) //this really shouldn't happen, but it does >_<
				pList[i]->RecheckCollisionFilter();
		}
	}
}

#if !defined( CLIENT_DLL )
#define CHANGE_FLAGS(flags,newFlags) { unsigned int old = flags; flags = (newFlags); gEntList.ReportEntityFlagsChanged( this->m_pOuter, old, flags ); }
#else
#define CHANGE_FLAGS(flags,newFlags) (flags = (newFlags))
#endif

void CEngineObjectInternal::AddFlag(int flags)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags | flags);
}

void CEngineObjectInternal::RemoveFlag(int flagsToRemove)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags & ~flagsToRemove);
}

void CEngineObjectInternal::ClearFlags(void)
{
	CHANGE_FLAGS(m_fFlags, 0);
}

void CEngineObjectInternal::ToggleFlag(int flagToToggle)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags ^ flagToToggle);
}

void CEngineObjectInternal::SetEffects(int nEffects)
{
	if (nEffects != m_fEffects)
	{
#ifdef HL2_EPISODIC
		// Hack for now, to avoid player emitting radius with his flashlight
		if (!m_pOuter->IsPlayer())
		{
			if ((nEffects & (EF_BRIGHTLIGHT | EF_DIMLIGHT)) && !(m_fEffects & (EF_BRIGHTLIGHT | EF_DIMLIGHT)))
			{
				AddEntityToDarknessCheck(this->m_pOuter);
			}
			else if (!(nEffects & (EF_BRIGHTLIGHT | EF_DIMLIGHT)) && (m_fEffects & (EF_BRIGHTLIGHT | EF_DIMLIGHT)))
			{
				RemoveEntityFromDarknessCheck(this->m_pOuter);
			}
		}
#endif // HL2_EPISODIC
		m_fEffects = nEffects;
		m_pOuter->DispatchUpdateTransmitState();
	}
}

void CEngineObjectInternal::AddEffects(int nEffects)
{
	m_pOuter->OnAddEffects(nEffects);
#ifdef HL2_EPISODIC
	if ((nEffects & (EF_BRIGHTLIGHT | EF_DIMLIGHT)) && !(m_fEffects & (EF_BRIGHTLIGHT | EF_DIMLIGHT)))
	{
		// Hack for now, to avoid player emitting radius with his flashlight
		if (!m_pOuter->IsPlayer())
		{
			AddEntityToDarknessCheck(this->m_pOuter);
		}
	}
#endif // HL2_EPISODIC
	m_fEffects |= nEffects;
	if (nEffects & EF_NODRAW)
	{
		m_pOuter->DispatchUpdateTransmitState();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int	CEngineObjectInternal::GetIndexForThinkContext(const char* pszContext)
{
	for (int i = 0; i < m_aThinkFunctions.Size(); i++)
	{
		if (!Q_strncmp(STRING(m_aThinkFunctions[i].m_iszContext), pszContext, MAX_CONTEXT_LENGTH))
			return i;
	}

	return NO_THINK_CONTEXT;
}

//-----------------------------------------------------------------------------
// Purpose: Get a fresh think context for this entity
//-----------------------------------------------------------------------------
int CEngineObjectInternal::RegisterThinkContext(const char* szContext)
{
	int iIndex = GetIndexForThinkContext(szContext);
	if (iIndex != NO_THINK_CONTEXT)
		return iIndex;

	// Make a new think func
	thinkfunc_t sNewFunc;
	Q_memset(&sNewFunc, 0, sizeof(sNewFunc));
	sNewFunc.m_pfnThink = NULL;
	sNewFunc.m_nNextThinkTick = 0;
	sNewFunc.m_iszContext = AllocPooledString(szContext);

	// Insert it into our list
	return m_aThinkFunctions.AddToTail(sNewFunc);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
BASEPTR	CEngineObjectInternal::ThinkSet(BASEPTR func, float thinkTime, const char* szContext)
{
#if !defined( CLIENT_DLL )
#ifdef _DEBUG
#ifdef PLATFORM_64BITS
#ifdef GNUC
	COMPILE_TIME_ASSERT(sizeof(func) == 16);
#else
	COMPILE_TIME_ASSERT(sizeof(func) == 8);
#endif
#else
#ifdef GNUC
	COMPILE_TIME_ASSERT(sizeof(func) == 8);
#else
	COMPILE_TIME_ASSERT(sizeof(func) == 4);
#endif
#endif
#endif
#endif

	// Old system?
	if (!szContext)
	{
		m_pfnThink = func;
#if !defined( CLIENT_DLL )
#ifdef _DEBUG
		m_pOuter->FunctionCheck(*(reinterpret_cast<void**>(&m_pfnThink)), "BaseThinkFunc");
#endif
#endif
		return m_pfnThink;
	}

	// Find the think function in our list, and if we couldn't find it, register it
	int iIndex = GetIndexForThinkContext(szContext);
	if (iIndex == NO_THINK_CONTEXT)
	{
		iIndex = RegisterThinkContext(szContext);
	}

	m_aThinkFunctions[iIndex].m_pfnThink = func;
#if !defined( CLIENT_DLL )
#ifdef _DEBUG
	m_pOuter->FunctionCheck(*(reinterpret_cast<void**>(&m_aThinkFunctions[iIndex].m_pfnThink)), szContext);
#endif
#endif

	if (thinkTime != 0)
	{
		int thinkTick = (thinkTime == TICK_NEVER_THINK) ? TICK_NEVER_THINK : TIME_TO_TICKS(thinkTime);
		m_aThinkFunctions[iIndex].m_nNextThinkTick = thinkTick;
		CheckHasThinkFunction(thinkTick == TICK_NEVER_THINK ? false : true);
	}
	return func;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetNextThink(float thinkTime, const char* szContext)
{
	int thinkTick = (thinkTime == TICK_NEVER_THINK) ? TICK_NEVER_THINK : TIME_TO_TICKS(thinkTime);

	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Setting base think function within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif

		// Old system
		m_nNextThinkTick = thinkTick;
		CheckHasThinkFunction(thinkTick == TICK_NEVER_THINK ? false : true);
		return;
	}
	else
	{
		// Find the think function in our list, and if we couldn't find it, register it
		iIndex = GetIndexForThinkContext(szContext);
		if (iIndex == NO_THINK_CONTEXT)
		{
			iIndex = RegisterThinkContext(szContext);
		}
	}

	// Old system
	m_aThinkFunctions[iIndex].m_nNextThinkTick = thinkTick;
	CheckHasThinkFunction(thinkTick == TICK_NEVER_THINK ? false : true);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetNextThink(const char* szContext)
{
	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Getting base nextthink time within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif

		if (m_nNextThinkTick == TICK_NEVER_THINK)
			return TICK_NEVER_THINK;

		// Old system
		return TICK_INTERVAL * (m_nNextThinkTick);
	}
	else
	{
		// Find the think function in our list
		iIndex = GetIndexForThinkContext(szContext);
	}

	if (iIndex == m_aThinkFunctions.InvalidIndex())
		return TICK_NEVER_THINK;

	if (m_aThinkFunctions[iIndex].m_nNextThinkTick == TICK_NEVER_THINK)
	{
		return TICK_NEVER_THINK;
	}
	return TICK_INTERVAL * (m_aThinkFunctions[iIndex].m_nNextThinkTick);
}

int	CEngineObjectInternal::GetNextThinkTick(const char* szContext /*= NULL*/)
{
	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Getting base nextthink time within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif

		if (m_nNextThinkTick == TICK_NEVER_THINK)
			return TICK_NEVER_THINK;

		// Old system
		return m_nNextThinkTick;
	}
	else
	{
		// Find the think function in our list
		iIndex = GetIndexForThinkContext(szContext);

		// Looking up an invalid think context!
		Assert(iIndex != -1);
	}

	if ((iIndex == -1) || (m_aThinkFunctions[iIndex].m_nNextThinkTick == TICK_NEVER_THINK))
	{
		return TICK_NEVER_THINK;
	}

	return m_aThinkFunctions[iIndex].m_nNextThinkTick;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetLastThink(const char* szContext)
{
	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Getting base lastthink time within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif
		// Old system
		return m_nLastThinkTick * TICK_INTERVAL;
	}
	else
	{
		// Find the think function in our list
		iIndex = GetIndexForThinkContext(szContext);
	}

	return m_aThinkFunctions[iIndex].m_nLastThinkTick * TICK_INTERVAL;
}

int CEngineObjectInternal::GetLastThinkTick(const char* szContext /*= NULL*/)
{
	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Getting base lastthink time within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif
		// Old system
		return m_nLastThinkTick;
	}
	else
	{
		// Find the think function in our list
		iIndex = GetIndexForThinkContext(szContext);
	}

	return m_aThinkFunctions[iIndex].m_nLastThinkTick;
}

void CEngineObjectInternal::SetLastThinkTick(int iThinkTick)
{
	m_nLastThinkTick = iThinkTick;
}

bool CEngineObjectInternal::WillThink()
{
	if (m_nNextThinkTick > 0)
		return true;

	for (int i = 0; i < m_aThinkFunctions.Count(); i++)
	{
		if (m_aThinkFunctions[i].m_nNextThinkTick > 0)
			return true;
	}

	return false;
}

// returns the first tick the entity will run any think function
// returns TICK_NEVER_THINK if no think functions are scheduled
int CEngineObjectInternal::GetFirstThinkTick()
{
	int minTick = TICK_NEVER_THINK;
	if (m_nNextThinkTick > 0)
	{
		minTick = m_nNextThinkTick;
	}

	for (int i = 0; i < m_aThinkFunctions.Count(); i++)
	{
		int next = m_aThinkFunctions[i].m_nNextThinkTick;
		if (next > 0)
		{
			if (next < minTick || minTick == TICK_NEVER_THINK)
			{
				minTick = next;
			}
		}
	}
	return minTick;
}

//-----------------------------------------------------------------------------
// Sets/Gets the next think based on context index
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetNextThink(int nContextIndex, float thinkTime)
{
	int thinkTick = (thinkTime == TICK_NEVER_THINK) ? TICK_NEVER_THINK : TIME_TO_TICKS(thinkTime);

	if (nContextIndex < 0)
	{
		SetNextThink(thinkTime);
	}
	else
	{
		m_aThinkFunctions[nContextIndex].m_nNextThinkTick = thinkTick;
	}
	CheckHasThinkFunction(thinkTick == TICK_NEVER_THINK ? false : true);
}

void CEngineObjectInternal::SetLastThink(int nContextIndex, float thinkTime)
{
	int thinkTick = (thinkTime == TICK_NEVER_THINK) ? TICK_NEVER_THINK : TIME_TO_TICKS(thinkTime);

	if (nContextIndex < 0)
	{
		m_nLastThinkTick = thinkTick;
	}
	else
	{
		m_aThinkFunctions[nContextIndex].m_nLastThinkTick = thinkTick;
	}
}

float CEngineObjectInternal::GetNextThink(int nContextIndex) const
{
	if (nContextIndex < 0)
		return m_nNextThinkTick * TICK_INTERVAL;

	return m_aThinkFunctions[nContextIndex].m_nNextThinkTick * TICK_INTERVAL;
}

int	CEngineObjectInternal::GetNextThinkTick(int nContextIndex) const
{
	if (nContextIndex < 0)
		return m_nNextThinkTick;

	return m_aThinkFunctions[nContextIndex].m_nNextThinkTick;
}

// NOTE: pass in the isThinking hint so we have to search the think functions less
void CEngineObjectInternal::CheckHasThinkFunction(bool isThinking)
{
	if (IsEFlagSet(EFL_NO_THINK_FUNCTION) && isThinking)
	{
		RemoveEFlags(EFL_NO_THINK_FUNCTION);
	}
	else if (!isThinking && !IsEFlagSet(EFL_NO_THINK_FUNCTION) && !WillThink())
	{
		AddEFlags(EFL_NO_THINK_FUNCTION);
	}
#if !defined( CLIENT_DLL )
	SimThink_EntityChanged(this->m_pOuter);
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Runs thinking code if time.  There is some play in the exact time the think
//  function will be called, because it is called before any movement is done
//  in a frame.  Not used for pushmove objects, because they must be exact.
//  Returns false if the entity removed itself.
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::PhysicsRunThink(thinkmethods_t thinkMethod)
{
	if (IsEFlagSet(EFL_NO_THINK_FUNCTION))
		return true;

	bool bAlive = true;

	// Don't fire the base if we're avoiding it
	if (thinkMethod != THINK_FIRE_ALL_BUT_BASE)
	{
		bAlive = PhysicsRunSpecificThink(-1, &CBaseEntity::Think);
		if (!bAlive)
			return false;
	}

	// Are we just firing the base think?
	if (thinkMethod == THINK_FIRE_BASE_ONLY)
		return bAlive;

	// Fire the rest of 'em
	for (int i = 0; i < m_aThinkFunctions.Count(); i++)
	{
#ifdef _DEBUG
		// Set the context
		m_iCurrentThinkContext = i;
#endif

		bAlive = PhysicsRunSpecificThink(i, m_aThinkFunctions[i].m_pfnThink);

#ifdef _DEBUG
		// Clear our context
		m_iCurrentThinkContext = NO_THINK_CONTEXT;
#endif

		if (!bAlive)
			return false;
	}

	return bAlive;
}

//-----------------------------------------------------------------------------
// Purpose: For testing if all thinks are occuring at the same time
//-----------------------------------------------------------------------------
struct ThinkSync
{
	float					thinktime;
	int						thinktick;
	CUtlVector< EHANDLE >	entities;

	ThinkSync()
	{
		thinktime = 0;
	}

	ThinkSync(const ThinkSync& src)
	{
		thinktime = src.thinktime;
		thinktick = src.thinktick;
		int c = src.entities.Count();
		for (int i = 0; i < c; i++)
		{
			entities.AddToTail(src.entities[i]);
		}
	}
};

//-----------------------------------------------------------------------------
// Purpose: For testing if all thinks are occuring at the same time
//-----------------------------------------------------------------------------
class CThinkSyncTester
{
public:
	CThinkSyncTester() :
		m_Thinkers(0, 0, ThinkLessFunc)
	{
		m_nLastFrameCount = -1;
		m_bShouldCheck = false;
	}

	void EntityThinking(int framecount, CBaseEntity* ent, float thinktime, int thinktick)
	{
#if !defined( CLIENT_DLL )
		if (m_nLastFrameCount != framecount)
		{
			if (m_bShouldCheck)
			{
				// Report
				Report();
				m_Thinkers.RemoveAll();
				m_nLastFrameCount = framecount;
			}

			m_bShouldCheck = sv_thinktimecheck.GetBool();
		}

		if (!m_bShouldCheck)
			return;

		ThinkSync* p = FindOrAddItem(ent, thinktime);
		if (!p)
		{
			Assert(0);
		}

		p->thinktime = thinktime;
		p->thinktick = thinktick;
		EHANDLE h;
		h = ent;
		p->entities.AddToTail(h);
#endif
	}

private:

	static bool ThinkLessFunc(const ThinkSync& item1, const ThinkSync& item2)
	{
		return item1.thinktime < item2.thinktime;
	}

	ThinkSync* FindOrAddItem(CBaseEntity* ent, float thinktime)
	{
		ThinkSync item;
		item.thinktime = thinktime;

		int idx = m_Thinkers.Find(item);
		if (idx == m_Thinkers.InvalidIndex())
		{
			idx = m_Thinkers.Insert(item);
		}

		return &m_Thinkers[idx];
	}

	void Report()
	{
		if (m_Thinkers.Count() == 0)
			return;

		Msg("-----------------\nThink report frame %i\n", gpGlobals->tickcount);

		for (int i = m_Thinkers.FirstInorder();
			i != m_Thinkers.InvalidIndex();
			i = m_Thinkers.NextInorder(i))
		{
			ThinkSync* p = &m_Thinkers[i];
			Assert(p);
			if (!p)
				continue;

			int ecount = p->entities.Count();
			if (!ecount)
			{
				continue;
			}

			Msg("thinktime %f, %i entities\n", p->thinktime, ecount);
			for (int j = 0; j < ecount; j++)
			{
				EHANDLE h = p->entities[j];
				int lastthinktick = 0;
				int nextthinktick = 0;
				CBaseEntity* e = h.Get();
				if (e)
				{
					lastthinktick = e->GetEngineObject()->GetLastThinkTick();
					nextthinktick = e->GetEngineObject()->GetNextThinkTick();
				}

				Msg("  %p : %30s (last %5i/next %5i)\n", h.Get(), h.Get() ? h->GetClassname() : "NULL",
					lastthinktick, nextthinktick);
			}
		}
	}

	CUtlRBTree< ThinkSync >	m_Thinkers;
	int			m_nLastFrameCount;
	bool		m_bShouldCheck;
};

static CThinkSyncTester g_ThinkChecker;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::PhysicsRunSpecificThink(int nContextIndex, BASEPTR thinkFunc)
{
	int thinktick = GetNextThinkTick(nContextIndex);

	if (thinktick <= 0 || thinktick > gpGlobals->tickcount)
		return true;

	float thinktime = thinktick * TICK_INTERVAL;

	// Don't let things stay in the past.
	//  it is possible to start that way
	//  by a trigger with a local time.
	if (thinktime < gpGlobals->curtime)
	{
		thinktime = gpGlobals->curtime;
	}

	// Only do this on the game server
#if !defined( CLIENT_DLL )
	g_ThinkChecker.EntityThinking(gpGlobals->tickcount, this->m_pOuter, thinktime, m_nNextThinkTick);
#endif

	SetNextThink(nContextIndex, TICK_NEVER_THINK);

	PhysicsDispatchThink(thinkFunc);

	SetLastThink(nContextIndex, gpGlobals->curtime);

	// Return whether entity is still valid
	return (!IsMarkedForDeletion());
}

//-----------------------------------------------------------------------------
// Purpose: Called when it's time for a physically moved objects (plats, doors, etc)
//			to run it's game code.
//			All other entity thinking is done during worldspawn's think
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsDispatchThink(BASEPTR thinkFunc)
{
	VPROF_ENTER_SCOPE((!vprof_scope_entity_thinks.GetBool()) ?
		"CBaseEntity::PhysicsDispatchThink" :
		EntityFactoryDictionary()->GetCannonicalName(GetClassname()));

	float thinkLimit = think_limit.GetFloat();

	// The thinkLimit stuff makes a LOT of calls to Sys_FloatTime, which winds up calling into
	// VCR mode so much that the framerate becomes unusable.
	if (VCRGetMode() != VCR_Disabled)
		thinkLimit = 0;

	float startTime = 0.0;

	if (m_pOuter->IsDormant())
	{
		Warning("Dormant entity %s (%s) is thinking!!\n", GetClassname(), m_pOuter->GetDebugName());
		Assert(0);
	}

	if (thinkLimit)
	{
		startTime = engine->Time();
	}

	if (thinkFunc)
	{
		MDLCACHE_CRITICAL_SECTION();
		(m_pOuter->*thinkFunc)();
	}

	if (thinkLimit)
	{
		// calculate running time of the AI in milliseconds
		float time = (engine->Time() - startTime) * 1000.0f;
		if (time > thinkLimit)
		{
#if defined( _XBOX ) && !defined( _RETAIL )
			if (vprof_think_limit.GetBool())
			{
				extern bool g_VProfSignalSpike;
				g_VProfSignalSpike = true;
			}
#endif
			// If its an NPC print out the shedule/task that took so long
			CAI_BaseNPC* pNPC = m_pOuter->MyNPCPointer();
			if (pNPC && pNPC->GetCurSchedule())
			{
				pNPC->ReportOverThinkLimit(time);
			}
			else
			{
#ifdef _WIN32
				Msg("%s(%s) thinking for %.02f ms!!!\n", GetClassname(), typeid(m_pOuter).raw_name(), time);
#elif POSIX
				Msg("%s(%s) thinking for %.02f ms!!!\n", GetClassname(), typeid(m_pOuter).name(), time);
#else
#error "typeinfo"
#endif
			}
		}
	}

	VPROF_EXIT_SCOPE();
}

void CEngineObjectInternal::SetMoveType(MoveType_t val, MoveCollide_t moveCollide)
{
#ifdef _DEBUG
	// Make sure the move type + move collide are compatible...
	if ((val != MOVETYPE_FLY) && (val != MOVETYPE_FLYGRAVITY))
	{
		Assert(moveCollide == MOVECOLLIDE_DEFAULT);
	}

	if (m_MoveType == MOVETYPE_VPHYSICS && val != m_MoveType)
	{
		if (m_pOuter->VPhysicsGetObject() && val != MOVETYPE_NONE)
		{
			// What am I supposed to do with the physics object if
			// you're changing away from MOVETYPE_VPHYSICS without making the object 
			// shadow?  This isn't likely to work, assert.
			// You probably meant to call VPhysicsInitShadow() instead of VPhysicsInitNormal()!
			Assert(m_pOuter->VPhysicsGetObject()->GetShadowController());
		}
	}
#endif

	if (m_MoveType == val)
	{
		m_MoveCollide = moveCollide;
		return;
	}

	// This is needed to the removal of MOVETYPE_FOLLOW:
	// We can't transition from follow to a different movetype directly
	// or the leaf code will break.
	Assert(!IsEffectActive(EF_BONEMERGE));
	m_MoveType = val;
	m_MoveCollide = moveCollide;

	CollisionRulesChanged();

	switch (m_MoveType)
	{
	case MOVETYPE_WALK:
	{
		SetSimulatedEveryTick(true);
		SetAnimatedEveryTick(true);
	}
	break;
	case MOVETYPE_STEP:
	{
		// This will probably go away once I remove the cvar that controls the test code
		SetSimulatedEveryTick(g_bTestMoveTypeStepSimulation ? true : false);
		SetAnimatedEveryTick(false);
	}
	break;
	case MOVETYPE_FLY:
	case MOVETYPE_FLYGRAVITY:
	{
		// Initialize our water state, because these movetypes care about transitions in/out of water
		m_pOuter->UpdateWaterState();
	}
	break;
	default:
	{
		SetSimulatedEveryTick(true);
		SetAnimatedEveryTick(false);
	}
	}

	// This will probably go away or be handled in a better way once I remove the cvar that controls the test code
	CheckStepSimulationChanged();
	CheckHasGamePhysicsSimulation();
}

//-----------------------------------------------------------------------------
// Purpose: Until we remove the above cvar, we need to have the entities able
//  to dynamically deal with changing their simulation stuff here.
//-----------------------------------------------------------------------------
void CEngineObjectInternal::CheckStepSimulationChanged()
{
	if (g_bTestMoveTypeStepSimulation != IsSimulatedEveryTick())
	{
		SetSimulatedEveryTick(g_bTestMoveTypeStepSimulation);
	}

	bool hadobject = HasDataObjectType(STEPSIMULATION);

	if (g_bTestMoveTypeStepSimulation)
	{
		if (!hadobject)
		{
			CreateDataObject(STEPSIMULATION);
		}
	}
	else
	{
		if (hadobject)
		{
			DestroyDataObject(STEPSIMULATION);
		}
	}
}

bool CEngineObjectInternal::WillSimulateGamePhysics()
{
	// players always simulate game physics
	if (!m_pOuter->IsPlayer())
	{
		MoveType_t movetype = GetMoveType();

		if (movetype == MOVETYPE_NONE || movetype == MOVETYPE_VPHYSICS)
			return false;

#if !defined( CLIENT_DLL )
		// MOVETYPE_PUSH not supported on the client
		if (movetype == MOVETYPE_PUSH && m_pOuter->GetMoveDoneTime() <= 0)
			return false;
#endif
	}

	return true;
}

void CEngineObjectInternal::CheckHasGamePhysicsSimulation()
{
	bool isSimulating = WillSimulateGamePhysics();
	if (isSimulating != IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION))
		return;
	if (isSimulating)
	{
		RemoveEFlags(EFL_NO_GAME_PHYSICS_SIMULATION);
	}
	else
	{
		AddEFlags(EFL_NO_GAME_PHYSICS_SIMULATION);
	}
#if !defined( CLIENT_DLL )
	SimThink_EntityChanged(this->m_pOuter);
#endif
}

//-----------------------------------------------------------------------------
bool CEngineObjectInternal::UseStepSimulationNetworkOrigin(const Vector** out_v)
{
	Assert(out_v);


	if (g_bTestMoveTypeStepSimulation &&
		GetMoveType() == MOVETYPE_STEP &&
		HasDataObjectType(STEPSIMULATION))
	{
		StepSimulationData* step = (StepSimulationData*)GetDataObject(STEPSIMULATION);
		ComputeStepSimulationNetwork(step);
		*out_v = &step->m_vecNetworkOrigin;

		return step->m_bOriginActive;
	}

	return false;
}

//-----------------------------------------------------------------------------
bool CEngineObjectInternal::UseStepSimulationNetworkAngles(const QAngle** out_a)
{
	Assert(out_a);

	if (g_bTestMoveTypeStepSimulation &&
		GetMoveType() == MOVETYPE_STEP &&
		HasDataObjectType(STEPSIMULATION))
	{
		StepSimulationData* step = (StepSimulationData*)GetDataObject(STEPSIMULATION);
		ComputeStepSimulationNetwork(step);
		*out_a = &step->m_angNetworkAngles;
		return step->m_bAnglesActive;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Run one tick's worth of faked simulation
// Input  : *step - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::ComputeStepSimulationNetwork(StepSimulationData* step)
{
	if (!step)
	{
		Assert(!"ComputeStepSimulationNetworkOriginAndAngles with NULL step\n");
		return;
	}

	// Don't run again if we've already calculated this tick
	if (step->m_nLastProcessTickCount == gpGlobals->tickcount)
	{
		return;
	}

	step->m_nLastProcessTickCount = gpGlobals->tickcount;

	// Origin
	// It's inactive
	if (step->m_bOriginActive)
	{
		// First see if any external code moved the entity
		if (m_pOuter->GetStepOrigin() != step->m_Next.vecOrigin)
		{
			step->m_bOriginActive = false;
		}
		else
		{
			// Compute interpolated info based on tick interval
			float frac = 1.0f;
			int tickdelta = step->m_Next.nTickCount - step->m_Previous.nTickCount;
			if (tickdelta > 0)
			{
				frac = (float)(gpGlobals->tickcount - step->m_Previous.nTickCount) / (float)tickdelta;
				frac = clamp(frac, 0.0f, 1.0f);
			}

			if (step->m_Previous2.nTickCount == 0 || step->m_Previous2.nTickCount >= step->m_Previous.nTickCount)
			{
				Vector delta = step->m_Next.vecOrigin - step->m_Previous.vecOrigin;
				VectorMA(step->m_Previous.vecOrigin, frac, delta, step->m_vecNetworkOrigin);
			}
			else if (!step_spline.GetBool())
			{
				StepSimulationStep* pOlder = &step->m_Previous;
				StepSimulationStep* pNewer = &step->m_Next;

				if (step->m_Discontinuity.nTickCount > step->m_Previous.nTickCount)
				{
					if (gpGlobals->tickcount > step->m_Discontinuity.nTickCount)
					{
						pOlder = &step->m_Discontinuity;
					}
					else
					{
						pNewer = &step->m_Discontinuity;
					}

					tickdelta = pNewer->nTickCount - pOlder->nTickCount;
					if (tickdelta > 0)
					{
						frac = (float)(gpGlobals->tickcount - pOlder->nTickCount) / (float)tickdelta;
						frac = clamp(frac, 0.0f, 1.0f);
					}
				}

				Vector delta = pNewer->vecOrigin - pOlder->vecOrigin;
				VectorMA(pOlder->vecOrigin, frac, delta, step->m_vecNetworkOrigin);
			}
			else
			{
				Hermite_Spline(step->m_Previous2.vecOrigin, step->m_Previous.vecOrigin, step->m_Next.vecOrigin, frac, step->m_vecNetworkOrigin);
			}
		}
	}

	// Angles
	if (step->m_bAnglesActive)
	{
		// See if external code changed the orientation of the entity
		if (m_pOuter->GetStepAngles() != step->m_angNextRotation)
		{
			step->m_bAnglesActive = false;
		}
		else
		{
			// Compute interpolated info based on tick interval
			float frac = 1.0f;
			int tickdelta = step->m_Next.nTickCount - step->m_Previous.nTickCount;
			if (tickdelta > 0)
			{
				frac = (float)(gpGlobals->tickcount - step->m_Previous.nTickCount) / (float)tickdelta;
				frac = clamp(frac, 0.0f, 1.0f);
			}

			if (step->m_Previous2.nTickCount == 0 || step->m_Previous2.nTickCount >= step->m_Previous.nTickCount)
			{
				// Pure blend between start/end orientations
				Quaternion outangles;
				QuaternionBlend(step->m_Previous.qRotation, step->m_Next.qRotation, frac, outangles);
				QuaternionAngles(outangles, step->m_angNetworkAngles);
			}
			else if (!step_spline.GetBool())
			{
				StepSimulationStep* pOlder = &step->m_Previous;
				StepSimulationStep* pNewer = &step->m_Next;

				if (step->m_Discontinuity.nTickCount > step->m_Previous.nTickCount)
				{
					if (gpGlobals->tickcount > step->m_Discontinuity.nTickCount)
					{
						pOlder = &step->m_Discontinuity;
					}
					else
					{
						pNewer = &step->m_Discontinuity;
					}

					tickdelta = pNewer->nTickCount - pOlder->nTickCount;
					if (tickdelta > 0)
					{
						frac = (float)(gpGlobals->tickcount - pOlder->nTickCount) / (float)tickdelta;
						frac = clamp(frac, 0.0f, 1.0f);
					}
				}

				// Pure blend between start/end orientations
				Quaternion outangles;
				QuaternionBlend(pOlder->qRotation, pNewer->qRotation, frac, outangles);
				QuaternionAngles(outangles, step->m_angNetworkAngles);
			}
			else
			{
				// FIXME: enable spline interpolation when turning is debounced.
				Quaternion outangles;
				Hermite_Spline(step->m_Previous2.qRotation, step->m_Previous.qRotation, step->m_Next.qRotation, frac, outangles);
				QuaternionAngles(outangles, step->m_angNetworkAngles);
			}
		}
	}

}

inline bool AnyPlayersInHierarchy_R(IEngineObjectServer* pEnt)
{
	if (pEnt->GetOuter()->IsPlayer())
		return true;

	for (IEngineObjectServer* pCur = pEnt->FirstMoveChild(); pCur; pCur = pCur->NextMovePeer())
	{
		if (AnyPlayersInHierarchy_R(pCur))
			return true;
	}

	return false;
}

void CEngineObjectInternal::RecalcHasPlayerChildBit()
{
	if (AnyPlayersInHierarchy_R(this))
		AddEFlags(EFL_HAS_PLAYER_CHILD);
	else
		RemoveEFlags(EFL_HAS_PLAYER_CHILD);
}

bool CEngineObjectInternal::DoesHavePlayerChild()
{
	return IsEFlagSet(EFL_HAS_PLAYER_CHILD);
}

//-----------------------------------------------------------------------------
// These methods encapsulate MOVETYPE_FOLLOW, which became obsolete
//-----------------------------------------------------------------------------
void CEngineObjectInternal::FollowEntity(IEngineObjectServer* pBaseEntity, bool bBoneMerge)
{
	if (pBaseEntity)
	{
		SetParent(pBaseEntity);
		SetMoveType(MOVETYPE_NONE);

		if (bBoneMerge)
			AddEffects(EF_BONEMERGE);

		AddSolidFlags(FSOLID_NOT_SOLID);
		SetLocalOrigin(vec3_origin);
		SetLocalAngles(vec3_angle);
	}
	else
	{
		StopFollowingEntity();
	}
}

void CEngineObjectInternal::StopFollowingEntity()
{
	if (!IsFollowingEntity())
	{
		//		Assert( GetEngineObject()->IsEffectActive( EF_BONEMERGE ) == 0 );
		return;
	}

	SetParent(NULL);
	RemoveEffects(EF_BONEMERGE);
	RemoveSolidFlags(FSOLID_NOT_SOLID);
	SetMoveType(MOVETYPE_NONE);
	CollisionRulesChanged();
}

bool CEngineObjectInternal::IsFollowingEntity()
{
	return IsEffectActive(EF_BONEMERGE) && (GetMoveType() == MOVETYPE_NONE) && GetMoveParent();
}

IEngineObjectServer* CEngineObjectInternal::GetFollowedEntity()
{
	if (!IsFollowingEntity())
		return NULL;
	return GetMoveParent();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEngineObjectInternal::UseClientSideAnimation()
{
	m_bClientSideAnimation = true;
}

void CEngineObjectInternal::SimulationChanged() {
	NetworkStateChanged(&m_vecOrigin);
	NetworkStateChanged(&m_angRotation);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : scale - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetModelScale(float scale, float change_duration /*= 0.0f*/)
{
	if (change_duration > 0.0f)
	{
		ModelScale* mvs = (ModelScale*)CreateDataObject(MODELSCALE);
		mvs->m_flModelScaleStart = m_flModelScale;
		mvs->m_flModelScaleGoal = scale;
		mvs->m_flModelScaleStartTime = gpGlobals->curtime;
		mvs->m_flModelScaleFinishTime = mvs->m_flModelScaleStartTime + change_duration;
	}
	else
	{
		m_flModelScale = scale;
		m_pOuter->RefreshCollisionBounds();

		if (HasDataObjectType(MODELSCALE))
		{
			DestroyDataObject(MODELSCALE);
		}
	}
}

void CEngineObjectInternal::UpdateModelScale()
{
	ModelScale* mvs = (ModelScale*)GetDataObject(MODELSCALE);
	if (!mvs)
	{
		return;
	}

	float dt = mvs->m_flModelScaleFinishTime - mvs->m_flModelScaleStartTime;
	Assert(dt > 0.0f);

	float frac = (gpGlobals->curtime - mvs->m_flModelScaleStartTime) / dt;
	frac = clamp(frac, 0.0f, 1.0f);

	if (gpGlobals->curtime >= mvs->m_flModelScaleFinishTime)
	{
		m_flModelScale = mvs->m_flModelScaleGoal;
		DestroyDataObject(MODELSCALE);
	}
	else
	{
		m_flModelScale = Lerp(frac, mvs->m_flModelScaleStart, mvs->m_flModelScaleGoal);
	}

	m_pOuter->RefreshCollisionBounds();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  :
//-----------------------------------------------------------------------------
void CEngineObjectInternal::LockStudioHdr()
{
	AUTO_LOCK(m_StudioHdrInitLock);
	const model_t* mdl = GetModel();
	if (mdl)
	{
		MDLHandle_t hStudioHdr = modelinfo->GetCacheHandle(mdl);
		if (hStudioHdr != MDLHANDLE_INVALID)
		{
			IStudioHdr* pStudioHdr = mdlcache->LockStudioHdr(hStudioHdr);
			IStudioHdr* pStudioHdrContainer = NULL;
			if (!m_pStudioHdr)
			{
				if (pStudioHdr)
				{
					pStudioHdrContainer = pStudioHdr;// mdlcache->GetIStudioHdr(pStudioHdr);
					//pStudioHdrContainer->Init( pStudioHdr, mdlcache );
				}
			}
			else
			{
				pStudioHdrContainer = m_pStudioHdr;
			}

			Assert((pStudioHdr == NULL && pStudioHdrContainer == NULL) || pStudioHdrContainer->GetRenderHdr() == pStudioHdr->GetRenderHdr());

			//if ( pStudioHdrContainer && pStudioHdrContainer->GetVirtualModel() )
			//{
			//	MDLHandle_t hVirtualModel = VoidPtrToMDLHandle( pStudioHdrContainer->GetRenderHdr()->VirtualModel() );
			//	mdlcache->LockStudioHdr( hVirtualModel );
			//}
			m_pStudioHdr = pStudioHdrContainer; // must be last to ensure virtual model correctly set up
		}
	}
}

void CEngineObjectInternal::UnlockStudioHdr()
{
	if (m_pStudioHdr)
	{
		const model_t* mdl = GetModel();
		if (mdl)
		{
			mdlcache->UnlockStudioHdr(modelinfo->GetCacheHandle(mdl));
			//if ( m_pStudioHdr->GetVirtualModel() )
			//{
			//	MDLHandle_t hVirtualModel = VoidPtrToMDLHandle( m_pStudioHdr->GetRenderHdr()->VirtualModel() );
			//	mdlcache->UnlockStudioHdr( hVirtualModel );
			//}
		}
	}
}

void CEngineObjectInternal::SetModelPointer(const model_t* pModel)
{
	if (m_pModel != pModel)
	{
		m_pModel = pModel;
		m_pOuter->OnNewModel();
	}
}

const model_t* CEngineObjectInternal::GetModel(void) const
{
	return m_pModel;
}

//-----------------------------------------------------------------------------
// Purpose: Force a clientside-animating entity to reset it's frame
//-----------------------------------------------------------------------------
void CEngineObjectInternal::ResetClientsideFrame(void)
{
	// TODO: Once we can chain MSG_ENTITY messages, use one of them
	m_bClientSideFrameReset = !(bool)m_bClientSideFrameReset;
}

//=========================================================
//=========================================================
void CEngineObjectInternal::SetSequence(int nSequence)
{
	Assert(nSequence == 0 /* || IsDynamicModelLoading()*/ || (GetModelPtr() && (nSequence < GetModelPtr()->GetNumSeq()) && (GetModelPtr()->GetNumSeq() < (1 << ANIMATION_SEQUENCE_BITS))));
	m_nSequence = nSequence;
}

void CEngineObjectInternal::ResetSequence(int nSequence)
{
	m_pOuter->OnResetSequence(nSequence);

	if (!SequenceLoops())
	{
		SetCycle(0);
	}

	// Tracker 17868:  If the sequence number didn't actually change, but you call resetsequence info, it changes
	//  the newsequenceparity bit which causes the client to call m_flCycle.Reset() which causes a very slight 
	//  discontinuity in looping animations as they reset around to cycle 0.0.  This was causing the parentattached
	//  helmet on barney to hitch every time barney's idle cycled back around to its start.
	bool changed = nSequence != GetSequence() ? true : false;

	SetSequence(nSequence);
	if (changed || !SequenceLoops())
	{
		ResetSequenceInfo();
	}
}

//=========================================================
//=========================================================
void CEngineObjectInternal::ResetSequenceInfo()
{
	if (GetSequence() == -1)
	{
		// This shouldn't happen.  Setting m_nSequence blindly is a horrible coding practice.
		SetSequence(0);
	}

	//if ( IsDynamicModelLoading() )
	//{
	//	m_bResetSequenceInfoOnLoad = true;
	//	return;
	//}

	IStudioHdr* pStudioHdr = GetModelPtr();
	m_flGroundSpeed = GetSequenceGroundSpeed(pStudioHdr, GetSequence()) * GetModelScale();
	m_bSequenceLoops = ((pStudioHdr->GetSequenceFlags(GetSequence()) & STUDIO_LOOPING) != 0);
	// m_flAnimTime = gpGlobals->time;
	m_flPlaybackRate = 1.0;
	m_bSequenceFinished = false;
	m_flLastEventCheck = 0;

	m_nNewSequenceParity = (m_nNewSequenceParity + 1) & EF_PARITY_MASK;
	m_nResetEventsParity = (m_nResetEventsParity + 1) & EF_PARITY_MASK;

	// FIXME: why is this called here?  Nothing should have changed to make this nessesary
	if (pStudioHdr)
	{
		mdlcache->SetEventIndexForSequence(pStudioHdr->pSeqdesc(GetSequence()));
	}
}

void CEngineObjectInternal::DoMuzzleFlash()
{
	m_nMuzzleFlashParity = (m_nMuzzleFlashParity + 1) & ((1 << EF_MUZZLEFLASH_BITS) - 1);
}

//=========================================================
//=========================================================
int CEngineObjectInternal::LookupPoseParameter(IStudioHdr* pStudioHdr, const char* szName)
{
	if (!pStudioHdr)
		return 0;

	if (!pStudioHdr->SequencesAvailable())
	{
		return 0;
	}

	for (int i = 0; i < pStudioHdr->GetNumPoseParameters(); i++)
	{
		if (Q_stricmp(pStudioHdr->pPoseParameter(i).pszName(), szName) == 0)
		{
			return i;
		}
	}

	// AssertMsg( 0, UTIL_VarArgs( "poseparameter %s couldn't be mapped!!!\n", szName ) );
	return -1; // Error
}

//=========================================================
//=========================================================
float CEngineObjectInternal::GetPoseParameter(const char* szName)
{
	return GetPoseParameter(LookupPoseParameter(szName));
}

float CEngineObjectInternal::GetPoseParameter(int iParameter)
{
	IStudioHdr* pstudiohdr = GetModelPtr();

	if (!pstudiohdr)
	{
		Assert(!"CBaseAnimating::GetPoseParameter: model missing");
		return 0.0;
	}

	if (!pstudiohdr->SequencesAvailable())
	{
		return 0;
	}

	if (iParameter >= 0)
	{
		return pstudiohdr->Studio_GetPoseParameter(iParameter, m_flPoseParameter[iParameter]);
	}

	return 0.0;
}

//=========================================================
//=========================================================
float CEngineObjectInternal::SetPoseParameter(IStudioHdr* pStudioHdr, const char* szName, float flValue)
{
	int poseParam = LookupPoseParameter(pStudioHdr, szName);
	AssertMsg2(poseParam >= 0, "SetPoseParameter called with invalid argument %s by %s", szName, m_pOuter->GetDebugName());
	return SetPoseParameter(pStudioHdr, poseParam, flValue);
}

float CEngineObjectInternal::SetPoseParameter(IStudioHdr* pStudioHdr, int iParameter, float flValue)
{
	if (!pStudioHdr)
	{
		return flValue;
	}

	if (iParameter >= 0)
	{
		float flNewValue;
		flValue = pStudioHdr->Studio_SetPoseParameter(iParameter, flValue, flNewValue);
		m_flPoseParameter.Set(iParameter, flNewValue);
	}

	return flValue;
}

//=========================================================
//=========================================================
float CEngineObjectInternal::SetBoneController(int iController, float flValue)
{
	Assert(GetModelPtr());

	IStudioHdr* pmodel = (IStudioHdr*)GetModelPtr();

	Assert(iController >= 0 && iController < NUM_BONECTRLS);

	float newValue;
	float retVal = pmodel->Studio_SetController(iController, flValue, newValue);

	float& val = m_flEncodedController.GetForModify(iController);
	val = newValue;
	return retVal;
}

//=========================================================
//=========================================================
float CEngineObjectInternal::GetBoneController(int iController)
{
	Assert(GetModelPtr());

	IStudioHdr* pmodel = (IStudioHdr*)GetModelPtr();

	return pmodel->Studio_GetController(iController, m_flEncodedController[iController]);
}

//=========================================================
//=========================================================
float CEngineObjectInternal::SequenceDuration(IStudioHdr* pStudioHdr, int iSequence)
{
	if (!pStudioHdr)
	{
		DevWarning(2, "CBaseAnimating::SequenceDuration( %d ) NULL pstudiohdr on %s!\n", iSequence, GetClassname());
		return 0.1;
	}
	if (!pStudioHdr->SequencesAvailable())
	{
		return 0.1;
	}
	if (iSequence >= pStudioHdr->GetNumSeq() || iSequence < 0)
	{
		DevWarning(2, "CBaseAnimating::SequenceDuration( %d ) out of range\n", iSequence);
		return 0.1;
	}

	return pStudioHdr->Studio_Duration(iSequence, GetPoseParameterArray());
}

float CEngineObjectInternal::GetSequenceCycleRate(IStudioHdr* pStudioHdr, int iSequence)
{
	float t = SequenceDuration(pStudioHdr, iSequence);

	if (t > 0.0f)
	{
		return 1.0f / t;
	}
	else
	{
		return 1.0f / 0.1f;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : float
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetSequenceMoveDist(IStudioHdr* pStudioHdr, int iSequence)
{
	Vector				vecReturn;

	pStudioHdr->GetSequenceLinearMotion(iSequence, GetPoseParameterArray(), &vecReturn);

	return vecReturn.Length();
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : float - 
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetSequenceMoveYaw(int iSequence)
{
	Vector				vecReturn;

	Assert(GetModelPtr());
	GetModelPtr()->GetSequenceLinearMotion(iSequence, GetPoseParameterArray(), &vecReturn);

	if (vecReturn.Length() > 0)
	{
		return UTIL_VecToYaw(vecReturn);
	}

	return NOMOTION;
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//			*pVec - 
//
//-----------------------------------------------------------------------------
void CEngineObjectInternal::GetSequenceLinearMotion(int iSequence, Vector* pVec)
{
	Assert(GetModelPtr());
	GetModelPtr()->GetSequenceLinearMotion(iSequence, GetPoseParameterArray(), pVec);
}

//-----------------------------------------------------------------------------
// Purpose: does a specific sequence have movement?
// Output :
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::HasMovement(int iSequence)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return false;

	// FIXME: this needs to check to see if there are keys, and the object is walking
	Vector deltaPos;
	QAngle deltaAngles;
	if (pstudiohdr->Studio_SeqMovement(iSequence, 0.0f, 1.0f, GetPoseParameterArray(), deltaPos, deltaAngles))
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: find frame where they animation has moved a given distance.
// Output :
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetMovementFrame(float flDist)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return 0;

	float t = pstudiohdr->Studio_FindSeqDistance(GetSequence(), GetPoseParameterArray(), flDist);

	return t;
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::GetSequenceMovement(int nSequence, float fromCycle, float toCycle, Vector& deltaPosition, QAngle& deltaAngles)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return false;

	return pstudiohdr->Studio_SeqMovement(nSequence, fromCycle, toCycle, GetPoseParameterArray(), deltaPosition, deltaAngles);
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::GetIntervalMovement(float flIntervalUsed, bool& bMoveSeqFinished, Vector& newPosition, QAngle& newAngles)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr || !pstudiohdr->SequencesAvailable())
		return false;

	float flComputedCycleRate = GetSequenceCycleRate(GetSequence());

	float flNextCycle = GetCycle() + flIntervalUsed * flComputedCycleRate * GetPlaybackRate();

	if ((!SequenceLoops()) && flNextCycle > 1.0)
	{
		flIntervalUsed = GetCycle() / (flComputedCycleRate * GetPlaybackRate());
		flNextCycle = 1.0;
		bMoveSeqFinished = true;
	}
	else
	{
		bMoveSeqFinished = false;
	}

	Vector deltaPos;
	QAngle deltaAngles;

	if (pstudiohdr->Studio_SeqMovement(GetSequence(), GetCycle(), flNextCycle, GetPoseParameterArray(), deltaPos, deltaAngles))
	{
		VectorYawRotate(deltaPos, GetLocalAngles().y, deltaPos);
		newPosition = GetLocalOrigin() + deltaPos;
		newAngles.Init();
		newAngles.y = GetLocalAngles().y + deltaAngles.y;
		return true;
	}
	else
	{
		newPosition = GetLocalOrigin();
		newAngles = GetLocalAngles();
		return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetEntryVelocity(int iSequence)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return 0;

	Vector vecVelocity;
	pstudiohdr->Studio_SeqVelocity(iSequence, 0.0, GetPoseParameterArray(), vecVelocity);

	return vecVelocity.Length();
}

float CEngineObjectInternal::GetExitVelocity(int iSequence)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return 0;

	Vector vecVelocity;
	pstudiohdr->Studio_SeqVelocity(iSequence, 1.0, GetPoseParameterArray(), vecVelocity);

	return vecVelocity.Length();
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetInstantaneousVelocity(float flInterval)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return 0;

	// FIXME: someone needs to check for last frame, etc.
	float flNextCycle = GetCycle() + flInterval * GetSequenceCycleRate(GetSequence()) * GetPlaybackRate();

	Vector vecVelocity;
	pstudiohdr->Studio_SeqVelocity(GetSequence(), flNextCycle, GetPoseParameterArray(), vecVelocity);
	vecVelocity *= GetPlaybackRate();

	return vecVelocity.Length();
}

float CEngineObjectInternal::GetSequenceGroundSpeed(IStudioHdr* pStudioHdr, int iSequence)
{
	float t = SequenceDuration(pStudioHdr, iSequence);

	if (t > 0)
	{
		return (GetSequenceMoveDist(pStudioHdr, iSequence) / t) * m_flSpeedScale;
	}
	else
	{
		return 0;
	}
}

bool CEngineObjectInternal::GetPoseParameterRange(int index, float& minValue, float& maxValue)
{
	IStudioHdr* pStudioHdr = GetModelPtr();

	if (pStudioHdr)
	{
		if (index >= 0 && index < pStudioHdr->GetNumPoseParameters())
		{
			const mstudioposeparamdesc_t& pose = pStudioHdr->pPoseParameter(index);
			minValue = pose.start;
			maxValue = pose.end;
			return true;
		}
	}
	minValue = 0.0f;
	maxValue = 1.0f;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::VPhysicsDestroyObject(void)
{
	if (m_pPhysicsObject && !m_ragdoll.listCount)
	{
#ifndef CLIENT_DLL
		PhysRemoveShadow(this->m_pOuter);
#endif
		PhysDestroyObject(m_pPhysicsObject, this->m_pOuter);
		m_pPhysicsObject = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysics - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::VPhysicsSetObject(IPhysicsObject* pPhysics)
{
	if (m_pPhysicsObject && pPhysics)
	{
		Warning("Overwriting physics object for %s\n", GetClassname());
	}
	m_pPhysicsObject = pPhysics;
	if (pPhysics && !m_pPhysicsObject)
	{
		CollisionRulesChanged();
	}
}

void CEngineObjectInternal::VPhysicsSwapObject(IPhysicsObject* pSwap)
{
	if (!pSwap)
	{
		PhysRemoveShadow(this->m_pOuter);
	}

	if (!m_pPhysicsObject)
	{
		Warning("Bad vphysics swap for %s\n", STRING(GetClassname()));
	}
	m_pPhysicsObject = pSwap;
}

//-----------------------------------------------------------------------------
// Purpose: Init this object's physics as a static
//-----------------------------------------------------------------------------
IPhysicsObject* CEngineObjectInternal::VPhysicsInitStatic(void)
{
	if (!VPhysicsInitSetup())
		return NULL;

#ifndef CLIENT_DLL
	// If this entity has a move parent, it needs to be shadow, not static
	if (GetMoveParent())
	{
		// must be SOLID_VPHYSICS if in hierarchy to solve collisions correctly
		if (GetSolid() == SOLID_BSP && GetRootMoveParent()->GetSolid() != SOLID_BSP)
		{
			SetSolid(SOLID_VPHYSICS);
		}

		return VPhysicsInitShadow(false, false);
	}
#endif

	// No physics
	if (GetSolid() == SOLID_NONE)
		return NULL;

	// create a static physics objct
	IPhysicsObject* pPhysicsObject = NULL;
	if (GetSolid() == SOLID_BBOX)
	{
		pPhysicsObject = PhysModelCreateBox(this->m_pOuter, WorldAlignMins(), WorldAlignMaxs(), GetAbsOrigin(), true);
	}
	else
	{
		pPhysicsObject = PhysModelCreateUnmoveable(this->m_pOuter, GetModelIndex(), GetAbsOrigin(), GetAbsAngles());
	}
	VPhysicsSetObject(pPhysicsObject);
	return pPhysicsObject;
}

//-----------------------------------------------------------------------------
// Purpose: This creates a normal vphysics simulated object
//			physics alone determines where it goes (gravity, friction, etc)
//			and the entity receives updates from vphysics.  SetAbsOrigin(), etc do not affect the object!
//-----------------------------------------------------------------------------
IPhysicsObject* CEngineObjectInternal::VPhysicsInitNormal(SolidType_t solidType, int nSolidFlags, bool createAsleep, solid_t* pSolid)
{
	if (!VPhysicsInitSetup())
		return NULL;

	// NOTE: This has to occur before PhysModelCreate because that call will
	// call back into ShouldCollide(), which uses solidtype for rules.
	SetSolid(solidType);
	SetSolidFlags(nSolidFlags);

	// No physics
	if (solidType == SOLID_NONE)
	{
		return NULL;
	}

	// create a normal physics object
	IPhysicsObject* pPhysicsObject = PhysModelCreate(this->m_pOuter, GetModelIndex(), GetAbsOrigin(), GetAbsAngles(), pSolid);
	if (pPhysicsObject)
	{
		VPhysicsSetObject(pPhysicsObject);
		SetMoveType(MOVETYPE_VPHYSICS);

		if (!createAsleep)
		{
			pPhysicsObject->Wake();
		}
	}

	return pPhysicsObject;
}

// This creates a vphysics object with a shadow controller that follows the AI
IPhysicsObject* CEngineObjectInternal::VPhysicsInitShadow(bool allowPhysicsMovement, bool allowPhysicsRotation, solid_t* pSolid)
{
	if (!VPhysicsInitSetup())
		return NULL;

	// No physics
	if (GetSolid() == SOLID_NONE)
		return NULL;

	const Vector& origin = GetAbsOrigin();
	QAngle angles = GetAbsAngles();
	IPhysicsObject* pPhysicsObject = NULL;

	if (GetSolid() == SOLID_BBOX)
	{
		// adjust these so the game tracing epsilons match the physics minimum separation distance
		// this will shrink the vphysics version of the model by the difference in epsilons
		float radius = 0.25f - DIST_EPSILON;
		Vector mins = WorldAlignMins() + Vector(radius, radius, radius);
		Vector maxs = WorldAlignMaxs() - Vector(radius, radius, radius);
		pPhysicsObject = PhysModelCreateBox(this->m_pOuter, mins, maxs, origin, false);
		angles = vec3_angle;
	}
	else if (GetSolid() == SOLID_OBB)
	{
		pPhysicsObject = PhysModelCreateOBB(this->m_pOuter, OBBMins(), OBBMaxs(), origin, angles, false);
	}
	else
	{
		pPhysicsObject = PhysModelCreate(this->m_pOuter, GetModelIndex(), origin, angles, pSolid);
	}
	if (!pPhysicsObject)
		return NULL;

	VPhysicsSetObject(pPhysicsObject);
	// UNDONE: Tune these speeds!!!
	pPhysicsObject->SetShadow(1e4, 1e4, allowPhysicsMovement, allowPhysicsRotation);
	pPhysicsObject->UpdateShadow(origin, angles, false, 0);
	return pPhysicsObject;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::VPhysicsInitSetup()
{
#ifndef CLIENT_DLL
	// don't support logical ents
	if (entindex() == -1 || IsMarkedForDeletion())
		return false;
#endif

	// If this entity already has a physics object, then it should have been deleted prior to making this call.
	Assert(!m_pPhysicsObject);
	VPhysicsDestroyObject();

	// make sure absorigin / absangles are correct
	return true;
}

IPhysicsObject* CEngineObjectInternal::GetGroundVPhysics()
{
	CBaseEntity* pGroundEntity = GetGroundEntity() ? GetGroundEntity()->GetOuter() : NULL;
	;	if (pGroundEntity && pGroundEntity->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		IPhysicsObject* pPhysGround = pGroundEntity->VPhysicsGetObject();
		if (pPhysGround && pPhysGround->IsMoveable())
			return pPhysGround;
	}
	return NULL;
}

// UNDONE: Look and see if the ground entity is in hierarchy with a MOVETYPE_VPHYSICS?
// Behavior in that case is not as good currently when the parent is rideable
bool CEngineObjectInternal::IsRideablePhysics(IPhysicsObject* pPhysics)
{
	if (pPhysics)
	{
		if (pPhysics->GetMass() > (VPhysicsGetObject()->GetMass() * 2))
			return true;
	}

	return false;
}

//=========================================================
// SelectWeightedSequence
//=========================================================
int CEngineObjectInternal::SelectWeightedSequence(int activity)
{
	Assert(activity != ACT_INVALID);
	Assert(GetModelPtr());
	return GetModelPtr()->SelectWeightedSequence(activity, GetSequence(), SharedRandomSelect);
}


int CEngineObjectInternal::SelectWeightedSequence(int activity, int curSequence)
{
	Assert(activity != ACT_INVALID);
	Assert(GetModelPtr());
	return GetModelPtr()->SelectWeightedSequence(activity, curSequence, SharedRandomSelect);
}

//=========================================================
// LookupHeaviestSequence
//
// Get sequence with highest 'weight' for this activity
//
//=========================================================
int CEngineObjectInternal::SelectHeaviestSequence(int activity)
{
	Assert(GetModelPtr());
	return GetModelPtr()->SelectHeaviestSequence(activity);
}

void CEngineObjectInternal::ClearRagdoll() {
	if (m_ragdoll.listCount) {
		for (int i = 0; i < m_ragdoll.listCount; i++)
		{
			if (m_ragdoll.list[i].pObject)
			{
				g_pPhysSaveRestoreManager->ForgetModel(m_ragdoll.list[i].pObject);
				m_ragdoll.list[i].pObject->EnableCollisions(false);
			}
		}

		// Set to null so that the destructor's call to DestroyObject won't destroy
		//  m_pObjects[ 0 ] twice since that's the physics object for the prop
		VPhysicsSetObject(NULL);

		RagdollDestroy(m_ragdoll);
	}
}

bool CEngineObjectInternal::VPhysicsUpdate(IPhysicsObject* pPhysics)
{
	if (m_lastUpdateTickCount == (unsigned int)gpGlobals->tickcount)
		return false;

	m_lastUpdateTickCount = gpGlobals->tickcount;
	//NetworkStateChanged();

	matrix3x4_t boneToWorld[MAXSTUDIOBONES];
	QAngle angles;
	Vector surroundingMins, surroundingMaxs;

	int i;
	for (i = 0; i < m_ragdoll.listCount; i++)
	{
		CBoneAccessor boneaccessor(boneToWorld);
		if (RagdollGetBoneMatrix(m_ragdoll, boneaccessor, i))
		{
			Vector vNewPos;
			MatrixAngles(boneToWorld[m_ragdoll.boneIndex[i]], angles, vNewPos);
			m_ragPos.Set(i, vNewPos);
			m_ragAngles.Set(i, angles);
		}
		else
		{
			m_ragPos.GetForModify(i).Init();
			m_ragAngles.GetForModify(i).Init();
		}
	}

	// BUGBUG: Use the ragdollmins/maxs to do this instead of the collides
	m_allAsleep = RagdollIsAsleep(m_ragdoll);

	if (m_allAsleep)
	{

	}
	else
	{
		if (m_ragdoll.pGroup->IsInErrorState())
		{
			RagdollSolveSeparation(m_ragdoll, this->m_pOuter);
		}
	}

	Vector vecFullMins, vecFullMaxs;
	vecFullMins = m_ragPos[0];
	vecFullMaxs = m_ragPos[0];
	for (i = 0; i < m_ragdoll.listCount; i++)
	{
		Vector mins, maxs;
		matrix3x4_t update;
		if (!m_ragdoll.list[i].pObject)
		{
			m_ragdollMins[i].Init();
			m_ragdollMaxs[i].Init();
			continue;
		}
		m_ragdoll.list[i].pObject->GetPositionMatrix(&update);
		TransformAABB(update, m_ragdollMins[i], m_ragdollMaxs[i], mins, maxs);
		for (int j = 0; j < 3; j++)
		{
			if (mins[j] < vecFullMins[j])
			{
				vecFullMins[j] = mins[j];
			}
			if (maxs[j] > vecFullMaxs[j])
			{
				vecFullMaxs[j] = maxs[j];
			}
		}
	}

	SetAbsOrigin(m_ragPos[0]);
	SetAbsAngles(vec3_angle);
	const Vector& vecOrigin = GetCollisionOrigin();
	AddSolidFlags(FSOLID_FORCE_WORLD_ALIGNED);
	SetSurroundingBoundsType(USE_COLLISION_BOUNDS_NEVER_VPHYSICS);
	SetCollisionBounds(vecFullMins - vecOrigin, vecFullMaxs - vecOrigin);
	MarkSurroundingBoundsDirty();

	PhysicsTouchTriggers();

	return true;
}

void CEngineObjectInternal::InitRagdoll(const Vector& forceVector, int forceBone, const Vector& forcePos, matrix3x4_t* pPrevBones, matrix3x4_t* pBoneToWorld, float dt, int collisionGroup, bool activateRagdoll, bool bWakeRagdoll)
{
	if (m_ragdoll.listCount) {
		return;
	}
	SetCollisionGroup(collisionGroup);
	SetMoveType(MOVETYPE_VPHYSICS);
	SetSolid(SOLID_VPHYSICS);
	AddSolidFlags(FSOLID_CUSTOMRAYTEST | FSOLID_CUSTOMBOXTEST);
	m_pOuter->m_takedamage = DAMAGE_EVENTS_ONLY;

	ragdollparams_t params;
	params.pGameData = static_cast<void*>(static_cast<CBaseEntity*>(this->m_pOuter));
	params.modelIndex = GetModelIndex();
	params.pCollide = modelinfo->GetVCollide(params.modelIndex);
	params.pStudioHdr = GetModelPtr();
	params.forceVector = forceVector;
	params.forceBoneIndex = forceBone;
	params.forcePosition = forcePos;
	params.pCurrentBones = pBoneToWorld;
	params.jointFrictionScale = 1.0;
	params.allowStretch = HasSpawnFlags(SF_RAGDOLLPROP_ALLOW_STRETCH);
	params.fixedConstraints = false;
	RagdollCreate(m_ragdoll, params, physenv);
	RagdollApplyAnimationAsVelocity(m_ragdoll, pPrevBones, pBoneToWorld, dt);
	if (m_anglesOverrideString != NULL_STRING && Q_strlen(m_anglesOverrideString.ToCStr()) > 0)
	{
		char szToken[2048];
		const char* pStr = nexttoken(szToken, STRING(m_anglesOverrideString), ',');
		// anglesOverride is index,angles,index,angles (e.g. "1, 22.5 123.0 0.0, 2, 0 0 0, 3, 0 0 180.0")
		while (szToken[0] != 0)
		{
			int objectIndex = atoi(szToken);
			// sanity check to make sure this token is an integer
			Assert(atof(szToken) == ((float)objectIndex));
			pStr = nexttoken(szToken, pStr, ',');
			Assert(szToken[0]);
			if (objectIndex >= m_ragdoll.listCount)
			{
				Warning("Bad ragdoll pose in entity %s, model (%s) at %s, model changed?\n", m_pOuter->GetDebugName(), GetModelName().ToCStr(), VecToString(GetAbsOrigin()));
			}
			else if (szToken[0] != 0)
			{
				QAngle angles;
				Assert(objectIndex >= 0 && objectIndex < RAGDOLL_MAX_ELEMENTS);
				UTIL_StringToVector(angles.Base(), szToken);
				int boneIndex = m_ragdoll.boneIndex[objectIndex];
				AngleMatrix(angles, pBoneToWorld[boneIndex]);
				const ragdollelement_t& element = m_ragdoll.list[objectIndex];
				Vector out;
				if (element.parentIndex >= 0)
				{
					int parentBoneIndex = m_ragdoll.boneIndex[element.parentIndex];
					VectorTransform(element.originParentSpace, pBoneToWorld[parentBoneIndex], out);
				}
				else
				{
					out = GetAbsOrigin();
				}
				MatrixSetColumn(out, 3, pBoneToWorld[boneIndex]);
				element.pObject->SetPositionMatrix(pBoneToWorld[boneIndex], true);
			}
			pStr = nexttoken(szToken, pStr, ',');
		}
	}

	if (activateRagdoll)
	{
		MEM_ALLOC_CREDIT();
		RagdollActivate(m_ragdoll, params.pCollide, GetModelIndex(), bWakeRagdoll);
	}

	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		UpdateNetworkDataFromVPhysics(i);
		g_pPhysSaveRestoreManager->AssociateModel(m_ragdoll.list[i].pObject, GetModelIndex());
		physcollision->CollideGetAABB(&m_ragdollMins[i], &m_ragdollMaxs[i], m_ragdoll.list[i].pObject->GetCollide(), vec3_origin, vec3_angle);
	}
	VPhysicsSetObject(m_ragdoll.list[0].pObject);

	CalcRagdollSize();
}

void CEngineObjectInternal::CalcRagdollSize(void)
{
	SetSurroundingBoundsType(USE_HITBOXES);
	RemoveSolidFlags(FSOLID_FORCE_WORLD_ALIGNED);
}

void CEngineObjectInternal::UpdateNetworkDataFromVPhysics(int index)
{
	Assert(index < m_ragdoll.listCount);

	QAngle angles;
	Vector vPos;
	m_ragdoll.list[index].pObject->GetPosition(&vPos, &angles);
	m_ragPos.Set(index, vPos);
	m_ragAngles.Set(index, angles);

	// move/relink if root moved
	if (index == 0)
	{
		SetAbsOrigin(m_ragPos[0]);
		PhysicsTouchTriggers();
	}
}

IPhysicsObject* CEngineObjectInternal::GetElement(int elementNum)
{
	return m_ragdoll.list[elementNum].pObject;
}

//-----------------------------------------------------------------------------
// Purpose: Force all the ragdoll's bone's physics objects to recheck their collision filters
//-----------------------------------------------------------------------------
void CEngineObjectInternal::RecheckCollisionFilter(void)
{
	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		m_ragdoll.list[i].pObject->RecheckCollisionFilter();
	}
}

void CEngineObjectInternal::GetAngleOverrideFromCurrentState(char* pOut, int size)
{
	pOut[0] = 0;
	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		if (i != 0)
		{
			Q_strncat(pOut, ",", size, COPY_ALL_CHARACTERS);

		}
		CFmtStr str("%d,%.2f %.2f %.2f", i, m_ragAngles[i].x, m_ragAngles[i].y, m_ragAngles[i].z);
		Q_strncat(pOut, str, size, COPY_ALL_CHARACTERS);
	}
}

void CEngineObjectInternal::RagdollBone(bool* boneSimulated, CBoneAccessor& pBoneToWorld)
{
	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		// during restore this may be NULL
		if (!GetElement(i))
			continue;

		if (RagdollGetBoneMatrix(m_ragdoll, pBoneToWorld, i))
		{
			boneSimulated[m_ragdoll.boneIndex[i]] = true;
		}
	}
}

void CEnginePlayerInternal::VPhysicsDestroyObject()
{
	// Since CBasePlayer aliases its pointer to the physics object, tell CBaseEntity to 
	// clear out its physics object pointer so we don't wind up deleting one of
	// the aliased objects twice.
	VPhysicsSetObject(NULL);

	PhysRemoveShadow(this->m_pOuter);

	if (m_pPhysicsController)
	{
		physenv->DestroyPlayerController(m_pPhysicsController);
		m_pPhysicsController = NULL;
	}

	if (m_pShadowStand)
	{
		m_pShadowStand->EnableCollisions(false);
		PhysDestroyObject(m_pShadowStand);
		m_pShadowStand = NULL;
	}
	if (m_pShadowCrouch)
	{
		m_pShadowCrouch->EnableCollisions(false);
		PhysDestroyObject(m_pShadowCrouch);
		m_pShadowCrouch = NULL;
	}

	CEngineObjectInternal::VPhysicsDestroyObject();
}

void CEnginePlayerInternal::SetupVPhysicsShadow(const Vector& vecAbsOrigin, const Vector& vecAbsVelocity, CPhysCollide* pStandModel, const char* pStandHullName, CPhysCollide* pCrouchModel, const char* pCrouchHullName)
{
	solid_t solid;
	Q_strncpy(solid.surfaceprop, "player", sizeof(solid.surfaceprop));
	solid.params = g_PhysDefaultObjectParams;
	solid.params.mass = 85.0f;
	solid.params.inertia = 1e24f;
	solid.params.enableCollisions = false;
	//disable drag
	solid.params.dragCoefficient = 0;
	// create standing hull
	m_pShadowStand = PhysModelCreateCustom(this->m_pOuter, pStandModel, GetLocalOrigin(), GetLocalAngles(), pStandHullName, false, &solid);
	m_pShadowStand->SetCallbackFlags(CALLBACK_GLOBAL_COLLISION | CALLBACK_SHADOW_COLLISION);

	// create crouchig hull
	m_pShadowCrouch = PhysModelCreateCustom(this->m_pOuter, pCrouchModel, GetLocalOrigin(), GetLocalAngles(), pCrouchHullName, false, &solid);
	m_pShadowCrouch->SetCallbackFlags(CALLBACK_GLOBAL_COLLISION | CALLBACK_SHADOW_COLLISION);

	// default to stand
	VPhysicsSetObject(m_pShadowStand);

	// tell physics lists I'm a shadow controller object
	PhysAddShadow(this->m_pOuter);
	m_pPhysicsController = physenv->CreatePlayerController(m_pShadowStand);
	m_pPhysicsController->SetPushMassLimit(350.0f);
	m_pPhysicsController->SetPushSpeedLimit(50.0f);

	// Give the controller a valid position so it doesn't do anything rash.
	UpdatePhysicsShadowToPosition(vecAbsOrigin);

	// init state
	if (GetFlags() & FL_DUCKING)
	{
		SetVCollisionState(vecAbsOrigin, vecAbsVelocity, VPHYS_CROUCH);
	}
	else
	{
		SetVCollisionState(vecAbsOrigin, vecAbsVelocity, VPHYS_WALK);
	}
}

void CEnginePlayerInternal::UpdatePhysicsShadowToPosition(const Vector& vecAbsOrigin)
{
	UpdateVPhysicsPosition(vecAbsOrigin, vec3_origin, gpGlobals->frametime);
}

void CEnginePlayerInternal::UpdateVPhysicsPosition(const Vector& position, const Vector& velocity, float secondsToArrival)
{
	bool onground = (GetFlags() & FL_ONGROUND) ? true : false;
	IPhysicsObject* pPhysGround = GetGroundVPhysics();

	// if the object is much heavier than the player, treat it as a local coordinate system
	// the player controller will solve movement differently in this case.
	if (!IsRideablePhysics(pPhysGround))
	{
		pPhysGround = NULL;
	}

	m_pPhysicsController->Update(position, velocity, secondsToArrival, onground, pPhysGround);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEnginePlayerInternal::SetVCollisionState(const Vector& vecAbsOrigin, const Vector& vecAbsVelocity, int collisionState)
{
	m_vphysicsCollisionState = collisionState;
	switch (collisionState)
	{
	case VPHYS_WALK:
		m_pShadowStand->SetPosition(vecAbsOrigin, vec3_angle, true);
		m_pShadowStand->SetVelocity(&vecAbsVelocity, NULL);
		m_pShadowCrouch->EnableCollisions(false);
		m_pPhysicsController->SetObject(m_pShadowStand);
		VPhysicsSwapObject(m_pShadowStand);
		m_pShadowStand->EnableCollisions(true);
		break;

	case VPHYS_CROUCH:
		m_pShadowCrouch->SetPosition(vecAbsOrigin, vec3_angle, true);
		m_pShadowCrouch->SetVelocity(&vecAbsVelocity, NULL);
		m_pShadowStand->EnableCollisions(false);
		m_pPhysicsController->SetObject(m_pShadowCrouch);
		VPhysicsSwapObject(m_pShadowCrouch);
		m_pShadowCrouch->EnableCollisions(true);
		break;

	case VPHYS_NOCLIP:
		m_pShadowCrouch->EnableCollisions(false);
		m_pShadowStand->EnableCollisions(false);
		break;
	}
}

CEnginePortalInternal::CEnginePortalInternal() 
: m_DataAccess(m_InternalData)
{

}

CEnginePortalInternal::~CEnginePortalInternal(){
	
}

void CEnginePortalInternal::VPhysicsDestroyObject(void)
{
	VPhysicsSetObject(NULL);
}

void CEnginePortalInternal::MoveTo(const Vector& ptCenter, const QAngle& angles)
{
	{
		SetAbsOrigin(ptCenter);
		SetAbsAngles(angles);
		//m_InternalData.Placement.ptCenter = ptCenter;
		//m_InternalData.Placement.qAngles = angles;
		AngleVectors(angles, &m_InternalData.Placement.vForward, &m_InternalData.Placement.vRight, &m_InternalData.Placement.vUp);
		m_InternalData.Placement.PortalPlane.normal = m_InternalData.Placement.vForward;
		m_InternalData.Placement.PortalPlane.dist = m_InternalData.Placement.PortalPlane.normal.Dot(GetAbsOrigin());
		m_InternalData.Placement.PortalPlane.signbits = SignbitsForPlane(&m_InternalData.Placement.PortalPlane);
		//m_InternalData.Placement.PortalPlane.Init(m_InternalData.Placement.vForward, m_InternalData.Placement.vForward.Dot(GetEngineObject()->GetAbsOrigin()));
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

void CEnginePortalInternal::UpdateLinkMatrix(IEnginePortalServer* pRemoteCollisionEntity)
{
	if (pRemoteCollisionEntity) {
		CEnginePortalInternal* pRemotePortalInternal = dynamic_cast<CEnginePortalInternal*>(pRemoteCollisionEntity);
		Vector vLocalLeft = -m_InternalData.Placement.vRight;
		VMatrix matLocalToWorld(m_InternalData.Placement.vForward, vLocalLeft, m_InternalData.Placement.vUp);
		matLocalToWorld.SetTranslation(GetAbsOrigin());

		VMatrix matLocalToWorldInverse;
		MatrixInverseTR(matLocalToWorld, matLocalToWorldInverse);

		//180 degree rotation about up
		VMatrix matRotation;
		matRotation.Identity();
		matRotation.m[0][0] = -1.0f;
		matRotation.m[1][1] = -1.0f;

		Vector vRemoteLeft = -pRemotePortalInternal->m_InternalData.Placement.vRight;
		VMatrix matRemoteToWorld(pRemotePortalInternal->m_InternalData.Placement.vForward, vRemoteLeft, pRemotePortalInternal->m_InternalData.Placement.vUp);
		matRemoteToWorld.SetTranslation(pRemotePortalInternal->GetAbsOrigin());

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

bool CEnginePortalInternal::EntityIsInPortalHole(IEngineObjectServer* pEntity) const //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
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

	switch (pEntity->GetSolid())
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
		physcollision->TraceBox(pEntity->GetAbsOrigin(), pEntity->GetAbsOrigin(),
			pEntity->OBBMins(), pEntity->OBBMaxs(),
			m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
		if (sv_debug_dumpportalhole_nextcheck.GetBool())
		{
			Vector vMins = pEntity->GetAbsOrigin() + pEntity->OBBMins();
			Vector vMaxs = pEntity->GetAbsOrigin() + pEntity->OBBMaxs();
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
bool CEnginePortalInternal::EntityHitBoxExtentIsInPortalHole(IEngineObjectServer* pBaseAnimating) const //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
{
	bool bFirstVert = true;
	Vector vMinExtent;
	Vector vMaxExtent;

	IStudioHdr* pStudioHdr = pBaseAnimating->GetModelPtr();
	if (!pStudioHdr)
		return false;

	mstudiohitboxset_t* set = pStudioHdr->pHitboxSet(pBaseAnimating->GetHitboxSet());
	if (!set)
		return false;

	Vector position;
	QAngle angles;

	for (int i = 0; i < set->numhitboxes; i++)
	{
		mstudiobbox_t* pbox = set->pHitbox(i);

		pBaseAnimating->GetOuter()->GetBonePosition(pbox->bone, position, angles);

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

bool CEnginePortalInternal::RayIsInPortalHole(const Ray_t& ray) const //traces a ray against the same detector for EntityIsInPortalHole(), bias is towards false positives
{
	trace_t Trace;
	physcollision->TraceBox(ray, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);
	return Trace.DidHit();
}

bool CEnginePortalInternal::TraceWorldBrushes(const Ray_t& ray, trace_t* pTrace) const
{
	if (m_DataAccess.Simulation.Static.World.Brushes.pCollideable && sv_portal_trace_vs_world.GetBool())
	{
		physcollision->TraceBox(ray, m_DataAccess.Simulation.Static.World.Brushes.pCollideable, vec3_origin, vec3_angle, pTrace);
		return true;
	}
	return false;
}

bool CEnginePortalInternal::TraceWallTube(const Ray_t& ray, trace_t* pTrace) const
{
	if (m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable && sv_portal_trace_vs_holywall.GetBool())
	{
		physcollision->TraceBox(ray, m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable, vec3_origin, vec3_angle, pTrace);
		return true;
	}
	return false;
}

bool CEnginePortalInternal::TraceWallBrushes(const Ray_t& ray, trace_t* pTrace) const
{
	if (m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable && sv_portal_trace_vs_holywall.GetBool())
	{
		physcollision->TraceBox(ray, m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable, vec3_origin, vec3_angle, pTrace);
		return true;
	}
	return false;
}

bool CEnginePortalInternal::TraceTransformedWorldBrushes(IEnginePortalServer* pRemoteCollisionEntity, const Ray_t& ray, trace_t* pTrace) const
{
	CEnginePortalInternal* pRemotePortalInternal = dynamic_cast<CEnginePortalInternal*>(pRemoteCollisionEntity);
	if (pRemotePortalInternal->m_DataAccess.Simulation.Static.World.Brushes.pCollideable && sv_portal_trace_vs_world.GetBool())
	{
		physcollision->TraceBox(ray, pRemotePortalInternal->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, pTrace);
		return true;
	}
	return false;
}

int CEnginePortalInternal::GetStaticPropsCount() const
{
	return m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Count();
}

const PS_SD_Static_World_StaticProps_ClippedProp_t* CEnginePortalInternal::GetStaticProps(int index) const
{
	return &m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations[index];
}

bool CEnginePortalInternal::StaticPropsCollisionExists() const
{
	return m_DataAccess.Simulation.Static.World.StaticProps.bCollisionExists;
}

//const Vector& CPSCollisionEntity::GetOrigin() const
//{
//	return m_DataAccess.Placement.ptCenter;
//}

//const QAngle& CPSCollisionEntity::GetAngles() const
//{
//	return m_DataAccess.Placement.qAngles;
//}

const Vector& CEnginePortalInternal::GetTransformedOrigin() const
{
	return m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform;
}

const QAngle& CEnginePortalInternal::GetTransformedAngles() const
{
	return m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform;
}

const VMatrix& CEnginePortalInternal::MatrixThisToLinked() const
{
	return m_InternalData.Placement.matThisToLinked;
}
const VMatrix& CEnginePortalInternal::MatrixLinkedToThis() const
{
	return m_InternalData.Placement.matLinkedToThis;
}

const cplane_t& CEnginePortalInternal::GetPortalPlane() const
{
	return m_DataAccess.Placement.PortalPlane;
}

const Vector& CEnginePortalInternal::GetVectorForward() const
{
	return m_DataAccess.Placement.vForward;
}
const Vector& CEnginePortalInternal::GetVectorUp() const
{
	return m_DataAccess.Placement.vUp;
}
const Vector& CEnginePortalInternal::GetVectorRight() const
{
	return m_DataAccess.Placement.vRight;
}

const PS_SD_Static_SurfaceProperties_t& CEnginePortalInternal::GetSurfaceProperties() const
{
	return m_DataAccess.Simulation.Static.SurfaceProperties;
}

IPhysicsObject* CEnginePortalInternal::GetWorldBrushesPhysicsObject() const
{
	return m_DataAccess.Simulation.Static.World.Brushes.pPhysicsObject;
}

IPhysicsObject* CEnginePortalInternal::GetWallBrushesPhysicsObject() const
{
	return m_DataAccess.Simulation.Static.Wall.Local.Brushes.pPhysicsObject;
}

IPhysicsObject* CEnginePortalInternal::GetWallTubePhysicsObject() const
{
	return m_DataAccess.Simulation.Static.Wall.Local.Tube.pPhysicsObject;
}

IPhysicsObject* CEnginePortalInternal::GetRemoteWallBrushesPhysicsObject() const
{
	return m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject;
}

IPhysicsEnvironment* CEnginePortalInternal::GetPhysicsEnvironment()
{
	return pPhysicsEnvironment;
}

void CEnginePortalInternal::CreatePhysicsEnvironment()
{
	pPhysicsEnvironment = physenv;
#ifdef PORTAL
	pPhysicsEnvironment = physenv_main;
#endif
}

void CEnginePortalInternal::ClearPhysicsEnvironment()
{
	pPhysicsEnvironment = NULL;
}

class CStaticCollisionPolyhedronCache : public CAutoGameSystem
{
public:
	CStaticCollisionPolyhedronCache(void);
	~CStaticCollisionPolyhedronCache(void);

	void LevelInitPreEntity(void);
	void Shutdown(void);

	const CPolyhedron* GetBrushPolyhedron(int iBrushNumber);
	int GetStaticPropPolyhedrons(ICollideable* pStaticProp, CPolyhedron** pOutputPolyhedronArray, int iOutputArraySize);

private:
	// See comments in LevelInitPreEntity for why these members are commented out
//	CUtlString	m_CachedMap;

	CUtlVector<CPolyhedron*> m_BrushPolyhedrons;

	struct StaticPropPolyhedronCacheInfo_t
	{
		int iStartIndex;
		int iNumPolyhedrons;
		int iStaticPropIndex; //helps us remap ICollideable pointers when the map is restarted
	};

	CUtlVector<CPolyhedron*> m_StaticPropPolyhedrons;
	CUtlMap<ICollideable*, StaticPropPolyhedronCacheInfo_t> m_CollideableIndicesMap;


	void Clear(void);
	void Update(void);
};

class CPolyhedron_LumpedMemory : public CPolyhedron //we'll be allocating one big chunk of memory for all our polyhedrons. No individual will own any memory.
{
public:
	virtual void Release(void) { };
	static CPolyhedron_LumpedMemory* AllocateAt(void* pMemory, int iVertices, int iLines, int iIndices, int iPolygons)
	{
#include "tier0/memdbgoff.h" //the following placement new doesn't compile with memory debugging
		CPolyhedron_LumpedMemory* pAllocated = new (pMemory) CPolyhedron_LumpedMemory;
#include "tier0/memdbgon.h"

		pAllocated->iVertexCount = iVertices;
		pAllocated->iLineCount = iLines;
		pAllocated->iIndexCount = iIndices;
		pAllocated->iPolygonCount = iPolygons;
		pAllocated->pVertices = (Vector*)(pAllocated + 1); //start vertex memory at the end of the class
		pAllocated->pLines = (Polyhedron_IndexedLine_t*)(pAllocated->pVertices + iVertices);
		pAllocated->pIndices = (Polyhedron_IndexedLineReference_t*)(pAllocated->pLines + iLines);
		pAllocated->pPolygons = (Polyhedron_IndexedPolygon_t*)(pAllocated->pIndices + iIndices);

		return pAllocated;
	}
};

static uint8* s_BrushPolyhedronMemory = NULL;
static uint8* s_StaticPropPolyhedronMemory = NULL;


typedef ICollideable* ICollideablePtr; //needed for key comparison function syntax
static bool CollideablePtr_KeyCompareFunc(const ICollideablePtr& a, const ICollideablePtr& b)
{
	return a < b;
};

CStaticCollisionPolyhedronCache::CStaticCollisionPolyhedronCache(void)
	: m_CollideableIndicesMap(CollideablePtr_KeyCompareFunc)
{

}

CStaticCollisionPolyhedronCache::~CStaticCollisionPolyhedronCache(void)
{
	Clear();
}

void CStaticCollisionPolyhedronCache::LevelInitPreEntity(void)
{

	// FIXME: Fast updates would be nice but this method doesn't work with the recent changes to standard containers.
	// For now we're going with the quick fix of always doing a full update. -Jeep

//	if( Q_stricmp( m_CachedMap, MapName() ) != 0 )
//	{
//		// New map or the last load was a transition, fully update the cache
//		m_CachedMap.Set( MapName() );

	Update();
	//	}
	//	else
	//	{
	//		// No need for a full update, but we need to remap static prop ICollideable's in the old system to the new system
	//		for( int i = m_CollideableIndicesMap.Count(); --i >= 0; )
	//		{
	//#ifdef _DEBUG
	//			StaticPropPolyhedronCacheInfo_t cacheInfo = m_CollideableIndicesMap.Element(i);
	//#endif
	//			m_CollideableIndicesMap.Reinsert( staticpropmgr->GetStaticPropByIndex( m_CollideableIndicesMap.Element(i).iStaticPropIndex ), i );
	//
	//			Assert( (m_CollideableIndicesMap.Element(i).iStartIndex == cacheInfo.iStartIndex) &&
	//					(m_CollideableIndicesMap.Element(i).iNumPolyhedrons == cacheInfo.iNumPolyhedrons) &&
	//					(m_CollideableIndicesMap.Element(i).iStaticPropIndex == cacheInfo.iStaticPropIndex) ); //I'm assuming this doesn't cause a reindex of the unordered list, if it does then this needs to be rewritten
	//		}
	//	}
}

void CStaticCollisionPolyhedronCache::Shutdown(void)
{
	Clear();
}


void CStaticCollisionPolyhedronCache::Clear(void)
{
	//The uses one big lump of memory to store polyhedrons. No need to Release() the polyhedrons.

	//Brushes
	{
		m_BrushPolyhedrons.RemoveAll();
		if (s_BrushPolyhedronMemory != NULL)
		{
			delete[]s_BrushPolyhedronMemory;
			s_BrushPolyhedronMemory = NULL;
		}
	}

	//Static props
	{
		m_CollideableIndicesMap.RemoveAll();
		m_StaticPropPolyhedrons.RemoveAll();
		if (s_StaticPropPolyhedronMemory != NULL)
		{
			delete[]s_StaticPropPolyhedronMemory;
			s_StaticPropPolyhedronMemory = NULL;
		}
	}
}

void CStaticCollisionPolyhedronCache::Update(void)
{
	Clear();

	//There's no efficient way to know exactly how much memory we'll need to cache off all these polyhedrons.
	//So we're going to allocated temporary workspaces as we need them and consolidate into one allocation at the end.
	const size_t workSpaceSize = 1024 * 1024; //1MB. Fairly arbitrary size for a workspace. Brushes usually use 1-3MB in the end. Static props usually use about half as much as brushes.

	uint8* workSpaceAllocations[256];
	size_t usedSpaceInWorkspace[256];
	unsigned int workSpacesAllocated = 0;
	uint8* pCurrentWorkSpace = new uint8[workSpaceSize];
	size_t roomLeftInWorkSpace = workSpaceSize;
	workSpaceAllocations[workSpacesAllocated] = pCurrentWorkSpace;
	usedSpaceInWorkspace[workSpacesAllocated] = 0;
	++workSpacesAllocated;


	//brushes
	{
		int iBrush = 0;
		CUtlVector<Vector4D> Planes;

		float fStackPlanes[4 * 400]; //400 is a crapload of planes in my opinion

		while (enginetrace->GetBrushInfo(iBrush, &Planes, NULL))
		{
			int iPlaneCount = Planes.Count();
			AssertMsg(iPlaneCount != 0, "A brush with no planes???????");

			const Vector4D* pReturnedPlanes = Planes.Base();

			CPolyhedron* pTempPolyhedron;

			if (iPlaneCount > 400)
			{
				// o_O, we'll have to get more memory to transform this brush
				float* pNonstackPlanes = new float[4 * iPlaneCount];

				for (int i = 0; i != iPlaneCount; ++i)
				{
					pNonstackPlanes[(i * 4) + 0] = pReturnedPlanes[i].x;
					pNonstackPlanes[(i * 4) + 1] = pReturnedPlanes[i].y;
					pNonstackPlanes[(i * 4) + 2] = pReturnedPlanes[i].z;
					pNonstackPlanes[(i * 4) + 3] = pReturnedPlanes[i].w;
				}

				pTempPolyhedron = GeneratePolyhedronFromPlanes(pNonstackPlanes, iPlaneCount, 0.01f, true);

				delete[]pNonstackPlanes;
			}
			else
			{
				for (int i = 0; i != iPlaneCount; ++i)
				{
					fStackPlanes[(i * 4) + 0] = pReturnedPlanes[i].x;
					fStackPlanes[(i * 4) + 1] = pReturnedPlanes[i].y;
					fStackPlanes[(i * 4) + 2] = pReturnedPlanes[i].z;
					fStackPlanes[(i * 4) + 3] = pReturnedPlanes[i].w;
				}

				pTempPolyhedron = GeneratePolyhedronFromPlanes(fStackPlanes, iPlaneCount, 0.01f, true);
			}

			if (pTempPolyhedron)
			{
				size_t memRequired = (sizeof(CPolyhedron_LumpedMemory)) +
					(sizeof(Vector) * pTempPolyhedron->iVertexCount) +
					(sizeof(Polyhedron_IndexedLine_t) * pTempPolyhedron->iLineCount) +
					(sizeof(Polyhedron_IndexedLineReference_t) * pTempPolyhedron->iIndexCount) +
					(sizeof(Polyhedron_IndexedPolygon_t) * pTempPolyhedron->iPolygonCount);

				Assert(memRequired < workSpaceSize);

				if (roomLeftInWorkSpace < memRequired)
				{
					usedSpaceInWorkspace[workSpacesAllocated - 1] = workSpaceSize - roomLeftInWorkSpace;

					pCurrentWorkSpace = new uint8[workSpaceSize];
					roomLeftInWorkSpace = workSpaceSize;
					workSpaceAllocations[workSpacesAllocated] = pCurrentWorkSpace;
					usedSpaceInWorkspace[workSpacesAllocated] = 0;
					++workSpacesAllocated;
				}

				CPolyhedron* pWorkSpacePolyhedron = CPolyhedron_LumpedMemory::AllocateAt(pCurrentWorkSpace,
					pTempPolyhedron->iVertexCount,
					pTempPolyhedron->iLineCount,
					pTempPolyhedron->iIndexCount,
					pTempPolyhedron->iPolygonCount);

				pCurrentWorkSpace += memRequired;
				roomLeftInWorkSpace -= memRequired;

				memcpy(pWorkSpacePolyhedron->pVertices, pTempPolyhedron->pVertices, pTempPolyhedron->iVertexCount * sizeof(Vector));
				memcpy(pWorkSpacePolyhedron->pLines, pTempPolyhedron->pLines, pTempPolyhedron->iLineCount * sizeof(Polyhedron_IndexedLine_t));
				memcpy(pWorkSpacePolyhedron->pIndices, pTempPolyhedron->pIndices, pTempPolyhedron->iIndexCount * sizeof(Polyhedron_IndexedLineReference_t));
				memcpy(pWorkSpacePolyhedron->pPolygons, pTempPolyhedron->pPolygons, pTempPolyhedron->iPolygonCount * sizeof(Polyhedron_IndexedPolygon_t));

				m_BrushPolyhedrons.AddToTail(pWorkSpacePolyhedron);

				pTempPolyhedron->Release();
			}
			else
			{
				m_BrushPolyhedrons.AddToTail(NULL);
			}

			++iBrush;
		}

		usedSpaceInWorkspace[workSpacesAllocated - 1] = workSpaceSize - roomLeftInWorkSpace;

		if (usedSpaceInWorkspace[0] != 0) //At least a little bit of memory was used.
		{
			//consolidate workspaces into a single memory chunk
			size_t totalMemoryNeeded = 0;
			for (unsigned int i = 0; i != workSpacesAllocated; ++i)
			{
				totalMemoryNeeded += usedSpaceInWorkspace[i];
			}

			uint8* pFinalDest = new uint8[totalMemoryNeeded];
			s_BrushPolyhedronMemory = pFinalDest;

			DevMsg(2, "CStaticCollisionPolyhedronCache: Used %.2f KB to cache %d brush polyhedrons.\n", ((float)totalMemoryNeeded) / 1024.0f, m_BrushPolyhedrons.Count());

			int iCount = m_BrushPolyhedrons.Count();
			for (int i = 0; i != iCount; ++i)
			{
				CPolyhedron_LumpedMemory* pSource = (CPolyhedron_LumpedMemory*)m_BrushPolyhedrons[i];

				if (pSource == NULL)
					continue;

				size_t memRequired = (sizeof(CPolyhedron_LumpedMemory)) +
					(sizeof(Vector) * pSource->iVertexCount) +
					(sizeof(Polyhedron_IndexedLine_t) * pSource->iLineCount) +
					(sizeof(Polyhedron_IndexedLineReference_t) * pSource->iIndexCount) +
					(sizeof(Polyhedron_IndexedPolygon_t) * pSource->iPolygonCount);

				CPolyhedron_LumpedMemory* pDest = (CPolyhedron_LumpedMemory*)pFinalDest;
				m_BrushPolyhedrons[i] = pDest;
				pFinalDest += memRequired;

				intp memoryOffset = ((uint8*)pDest) - ((uint8*)pSource);

				memcpy(pDest, pSource, memRequired);
				//move all the pointers to their new location.
				pDest->pVertices = (Vector*)(((uint8*)(pDest->pVertices)) + memoryOffset);
				pDest->pLines = (Polyhedron_IndexedLine_t*)(((uint8*)(pDest->pLines)) + memoryOffset);
				pDest->pIndices = (Polyhedron_IndexedLineReference_t*)(((uint8*)(pDest->pIndices)) + memoryOffset);
				pDest->pPolygons = (Polyhedron_IndexedPolygon_t*)(((uint8*)(pDest->pPolygons)) + memoryOffset);
			}
		}
	}

	unsigned int iBrushWorkSpaces = workSpacesAllocated;
	workSpacesAllocated = 1;
	pCurrentWorkSpace = workSpaceAllocations[0];
	usedSpaceInWorkspace[0] = 0;
	roomLeftInWorkSpace = workSpaceSize;

	//static props
	{
		CUtlVector<ICollideable*> StaticPropCollideables;
		staticpropmgr->GetAllStaticProps(&StaticPropCollideables);

		if (StaticPropCollideables.Count() != 0)
		{
			ICollideable** pCollideables = StaticPropCollideables.Base();
			ICollideable** pStop = pCollideables + StaticPropCollideables.Count();

			int iStaticPropIndex = 0;
			do
			{
				ICollideable* pProp = *pCollideables;
				vcollide_t* pCollide = modelinfo->GetVCollide(pProp->GetCollisionModel());
				StaticPropPolyhedronCacheInfo_t cacheInfo;
				cacheInfo.iStartIndex = m_StaticPropPolyhedrons.Count();

				if (pCollide != NULL)
				{
					VMatrix matToWorldPosition = pProp->CollisionToWorldTransform();

					for (int i = 0; i != pCollide->solidCount; ++i)
					{
						CPhysConvex* ConvexesArray[1024];
						int iConvexes = physcollision->GetConvexesUsedInCollideable(pCollide->solids[i], ConvexesArray, 1024);

						for (int j = 0; j != iConvexes; ++j)
						{
							CPolyhedron* pTempPolyhedron = physcollision->PolyhedronFromConvex(ConvexesArray[j], true);
							if (pTempPolyhedron)
							{
								for (int iPointCounter = 0; iPointCounter != pTempPolyhedron->iVertexCount; ++iPointCounter)
									pTempPolyhedron->pVertices[iPointCounter] = matToWorldPosition * pTempPolyhedron->pVertices[iPointCounter];

								for (int iPolyCounter = 0; iPolyCounter != pTempPolyhedron->iPolygonCount; ++iPolyCounter)
									pTempPolyhedron->pPolygons[iPolyCounter].polyNormal = matToWorldPosition.ApplyRotation(pTempPolyhedron->pPolygons[iPolyCounter].polyNormal);


								size_t memRequired = (sizeof(CPolyhedron_LumpedMemory)) +
									(sizeof(Vector) * pTempPolyhedron->iVertexCount) +
									(sizeof(Polyhedron_IndexedLine_t) * pTempPolyhedron->iLineCount) +
									(sizeof(Polyhedron_IndexedLineReference_t) * pTempPolyhedron->iIndexCount) +
									(sizeof(Polyhedron_IndexedPolygon_t) * pTempPolyhedron->iPolygonCount);

								Assert(memRequired < workSpaceSize);

								if (roomLeftInWorkSpace < memRequired)
								{
									usedSpaceInWorkspace[workSpacesAllocated - 1] = workSpaceSize - roomLeftInWorkSpace;

									if (workSpacesAllocated < iBrushWorkSpaces)
									{
										//re-use a workspace already allocated during brush polyhedron conversion
										pCurrentWorkSpace = workSpaceAllocations[workSpacesAllocated];
										usedSpaceInWorkspace[workSpacesAllocated] = 0;
									}
									else
									{
										//allocate a new workspace
										pCurrentWorkSpace = new uint8[workSpaceSize];
										workSpaceAllocations[workSpacesAllocated] = pCurrentWorkSpace;
										usedSpaceInWorkspace[workSpacesAllocated] = 0;
									}

									roomLeftInWorkSpace = workSpaceSize;
									++workSpacesAllocated;
								}

								CPolyhedron* pWorkSpacePolyhedron = CPolyhedron_LumpedMemory::AllocateAt(pCurrentWorkSpace,
									pTempPolyhedron->iVertexCount,
									pTempPolyhedron->iLineCount,
									pTempPolyhedron->iIndexCount,
									pTempPolyhedron->iPolygonCount);

								pCurrentWorkSpace += memRequired;
								roomLeftInWorkSpace -= memRequired;

								memcpy(pWorkSpacePolyhedron->pVertices, pTempPolyhedron->pVertices, pTempPolyhedron->iVertexCount * sizeof(Vector));
								memcpy(pWorkSpacePolyhedron->pLines, pTempPolyhedron->pLines, pTempPolyhedron->iLineCount * sizeof(Polyhedron_IndexedLine_t));
								memcpy(pWorkSpacePolyhedron->pIndices, pTempPolyhedron->pIndices, pTempPolyhedron->iIndexCount * sizeof(Polyhedron_IndexedLineReference_t));
								memcpy(pWorkSpacePolyhedron->pPolygons, pTempPolyhedron->pPolygons, pTempPolyhedron->iPolygonCount * sizeof(Polyhedron_IndexedPolygon_t));

								m_StaticPropPolyhedrons.AddToTail(pWorkSpacePolyhedron);

#ifdef _DEBUG
								CPhysConvex* pConvex = physcollision->ConvexFromConvexPolyhedron(*pTempPolyhedron);
								AssertMsg(pConvex != NULL, "Conversion from Convex to Polyhedron was unreversable");
								if (pConvex)
								{
									physcollision->ConvexFree(pConvex);
								}
#endif

								pTempPolyhedron->Release();
							}
						}
					}

					cacheInfo.iNumPolyhedrons = m_StaticPropPolyhedrons.Count() - cacheInfo.iStartIndex;
					cacheInfo.iStaticPropIndex = iStaticPropIndex;
					Assert(staticpropmgr->GetStaticPropByIndex(iStaticPropIndex) == pProp);

					m_CollideableIndicesMap.InsertOrReplace(pProp, cacheInfo);
				}

				++iStaticPropIndex;
				++pCollideables;
			} while (pCollideables != pStop);


			usedSpaceInWorkspace[workSpacesAllocated - 1] = workSpaceSize - roomLeftInWorkSpace;

			if (usedSpaceInWorkspace[0] != 0) //At least a little bit of memory was used.
			{
				//consolidate workspaces into a single memory chunk
				size_t totalMemoryNeeded = 0;
				for (unsigned int i = 0; i != workSpacesAllocated; ++i)
				{
					totalMemoryNeeded += usedSpaceInWorkspace[i];
				}

				uint8* pFinalDest = new uint8[totalMemoryNeeded];
				s_StaticPropPolyhedronMemory = pFinalDest;

				DevMsg(2, "CStaticCollisionPolyhedronCache: Used %.2f KB to cache %d static prop polyhedrons.\n", ((float)totalMemoryNeeded) / 1024.0f, m_StaticPropPolyhedrons.Count());

				int iCount = m_StaticPropPolyhedrons.Count();
				for (int i = 0; i != iCount; ++i)
				{
					CPolyhedron_LumpedMemory* pSource = (CPolyhedron_LumpedMemory*)m_StaticPropPolyhedrons[i];

					size_t memRequired = (sizeof(CPolyhedron_LumpedMemory)) +
						(sizeof(Vector) * pSource->iVertexCount) +
						(sizeof(Polyhedron_IndexedLine_t) * pSource->iLineCount) +
						(sizeof(Polyhedron_IndexedLineReference_t) * pSource->iIndexCount) +
						(sizeof(Polyhedron_IndexedPolygon_t) * pSource->iPolygonCount);

					CPolyhedron_LumpedMemory* pDest = (CPolyhedron_LumpedMemory*)pFinalDest;
					m_StaticPropPolyhedrons[i] = pDest;
					pFinalDest += memRequired;

					intp memoryOffset = ((uint8*)pDest) - ((uint8*)pSource);

					memcpy(pDest, pSource, memRequired);
					//move all the pointers to their new location.
					pDest->pVertices = (Vector*)(((uint8*)(pDest->pVertices)) + memoryOffset);
					pDest->pLines = (Polyhedron_IndexedLine_t*)(((uint8*)(pDest->pLines)) + memoryOffset);
					pDest->pIndices = (Polyhedron_IndexedLineReference_t*)(((uint8*)(pDest->pIndices)) + memoryOffset);
					pDest->pPolygons = (Polyhedron_IndexedPolygon_t*)(((uint8*)(pDest->pPolygons)) + memoryOffset);
				}
			}
		}
	}

	if (iBrushWorkSpaces > workSpacesAllocated)
		workSpacesAllocated = iBrushWorkSpaces;

	for (unsigned int i = 0; i != workSpacesAllocated; ++i)
	{
		delete[]workSpaceAllocations[i];
	}
}



const CPolyhedron* CStaticCollisionPolyhedronCache::GetBrushPolyhedron(int iBrushNumber)
{
	Assert(iBrushNumber < m_BrushPolyhedrons.Count());

	if ((iBrushNumber < 0) || (iBrushNumber >= m_BrushPolyhedrons.Count()))
		return NULL;

	return m_BrushPolyhedrons[iBrushNumber];
}

int CStaticCollisionPolyhedronCache::GetStaticPropPolyhedrons(ICollideable* pStaticProp, CPolyhedron** pOutputPolyhedronArray, int iOutputArraySize)
{
	unsigned short iPropIndex = m_CollideableIndicesMap.Find(pStaticProp);
	if (!m_CollideableIndicesMap.IsValidIndex(iPropIndex)) //static prop never made it into the cache for some reason (specifically no collision data when this workaround was written)
		return 0;

	StaticPropPolyhedronCacheInfo_t cacheInfo = m_CollideableIndicesMap.Element(iPropIndex);

	if (cacheInfo.iNumPolyhedrons < iOutputArraySize)
		iOutputArraySize = cacheInfo.iNumPolyhedrons;

	for (int i = cacheInfo.iStartIndex, iWriteIndex = 0; iWriteIndex != iOutputArraySize; ++i, ++iWriteIndex)
	{
		pOutputPolyhedronArray[iWriteIndex] = m_StaticPropPolyhedrons[i];
	}

	return iOutputArraySize;
}

CStaticCollisionPolyhedronCache g_StaticCollisionPolyhedronCache;



static void ConvertBrushListToClippedPolyhedronList(const int* pBrushes, int iBrushCount, const float* pOutwardFacingClipPlanes, int iClipPlaneCount, float fClipEpsilon, CUtlVector<CPolyhedron*>* pPolyhedronList)
{
	if (pPolyhedronList == NULL)
		return;

	if ((pBrushes == NULL) || (iBrushCount == 0))
		return;

	for (int i = 0; i != iBrushCount; ++i)
	{
		CPolyhedron* pPolyhedron = ClipPolyhedron(g_StaticCollisionPolyhedronCache.GetBrushPolyhedron(pBrushes[i]), pOutwardFacingClipPlanes, iClipPlaneCount, fClipEpsilon);
		if (pPolyhedron)
			pPolyhedronList->AddToTail(pPolyhedron);
	}
}

static void ClipPolyhedrons(CPolyhedron* const* pExistingPolyhedrons, int iPolyhedronCount, const float* pOutwardFacingClipPlanes, int iClipPlaneCount, float fClipEpsilon, CUtlVector<CPolyhedron*>* pPolyhedronList)
{
	if (pPolyhedronList == NULL)
		return;

	if ((pExistingPolyhedrons == NULL) || (iPolyhedronCount == 0))
		return;

	for (int i = 0; i != iPolyhedronCount; ++i)
	{
		CPolyhedron* pPolyhedron = ClipPolyhedron(pExistingPolyhedrons[i], pOutwardFacingClipPlanes, iClipPlaneCount, fClipEpsilon);
		if (pPolyhedron)
			pPolyhedronList->AddToTail(pPolyhedron);
	}
}

void CEnginePortalInternal::CreatePolyhedrons(void)
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

		Vector ptOBBOrigin = GetAbsOrigin();
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

		Vector ptOBBOrigin = GetAbsOrigin();
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
		float fTubeDepthDist = vBackward.Dot(GetAbsOrigin() + (vBackward * (PORTAL_WALL_TUBE_DEPTH + PORTAL_WALL_TUBE_OFFSET)));
		fPlanes[(1 * 4) + 3] = fTubeDepthDist;


		//the remaining planes will always have the same ordering of normals, with different distances plugged in for each convex we're creating
		//normal order is up, down, left, right

		fPlanes[(2 * 4) + 0] = m_InternalData.Placement.vUp.x;
		fPlanes[(2 * 4) + 1] = m_InternalData.Placement.vUp.y;
		fPlanes[(2 * 4) + 2] = m_InternalData.Placement.vUp.z;
		fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT));

		fPlanes[(3 * 4) + 0] = vDown.x;
		fPlanes[(3 * 4) + 1] = vDown.y;
		fPlanes[(3 * 4) + 2] = vDown.z;
		fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * PORTAL_HOLE_HALF_HEIGHT));

		fPlanes[(4 * 4) + 0] = vLeft.x;
		fPlanes[(4 * 4) + 1] = vLeft.y;
		fPlanes[(4 * 4) + 2] = vLeft.z;
		fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + (vLeft * PORTAL_HOLE_HALF_WIDTH));

		fPlanes[(5 * 4) + 0] = m_InternalData.Placement.vRight.x;
		fPlanes[(5 * 4) + 1] = m_InternalData.Placement.vRight.y;
		fPlanes[(5 * 4) + 2] = m_InternalData.Placement.vRight.z;
		fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + (m_InternalData.Placement.vRight * PORTAL_HOLE_HALF_WIDTH));

		float* fSidePlanesOnly = &fPlanes[(2 * 4)];

		//these 2 get re-used a bit
		float fFarRightPlaneDistance = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_WALL_FARDIST * 10.0f));
		float fFarLeftPlaneDistance = vLeft.Dot(GetAbsOrigin() + vLeft * (PORTAL_WALL_FARDIST * 10.0f));


		CUtlVector<int> WallBrushes;
		CUtlVector<CPolyhedron*> WallBrushPolyhedrons_ClippedToWall;
		CPolyhedron** pWallClippedPolyhedrons = NULL;
		int iWallClippedPolyhedronCount = 0;
		//if (m_pOwningSimulator->IsSimulatingVPhysics()) //if not simulating vphysics, we skip making the entire wall, and just create the minimal tube instead
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
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT);
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + m_InternalData.Placement.vUp * (PORTAL_WALL_FARDIST * 10.0f));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;



			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//lower wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (vDown * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * (PORTAL_WALL_FARDIST * 10.0f)));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//left wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + (vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + (vLeft * PORTAL_HOLE_HALF_WIDTH));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() - (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + (vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS)));

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//right wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * (PORTAL_HOLE_HALF_HEIGHT)));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * PORTAL_HOLE_HALF_WIDTH);
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		for (int i = WallBrushPolyhedrons_ClippedToWall.Count(); --i >= 0; )
			WallBrushPolyhedrons_ClippedToWall[i]->Release();

		WallBrushPolyhedrons_ClippedToWall.RemoveAll();
	}
}

void CEnginePortalInternal::ClearPolyhedrons(void)
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

static CPhysCollide* ConvertPolyhedronsToCollideable(CPolyhedron** pPolyhedrons, int iPolyhedronCount)
{
	if ((pPolyhedrons == NULL) || (iPolyhedronCount == 0))
		return NULL;

	CREATEDEBUGTIMER(functionTimer);

	STARTDEBUGTIMER(functionTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sConvertPolyhedronsToCollideable() START\n", s_iPortalSimulatorGUID, TABSPACING); );
	INCREMENTTABSPACING();

	CPhysConvex** pConvexes = (CPhysConvex**)stackalloc(iPolyhedronCount * sizeof(CPhysConvex*));
	int iConvexCount = 0;

	CREATEDEBUGTIMER(convexTimer);
	STARTDEBUGTIMER(convexTimer);
	for (int i = 0; i != iPolyhedronCount; ++i)
	{
		pConvexes[iConvexCount] = physcollision->ConvexFromConvexPolyhedron(*pPolyhedrons[i]);

		Assert(pConvexes[iConvexCount] != NULL);

		if (pConvexes[iConvexCount])
			++iConvexCount;
	}
	STOPDEBUGTIMER(convexTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sConvex Generation:%fms\n", s_iPortalSimulatorGUID, TABSPACING, convexTimer.GetDuration().GetMillisecondsF()); );


	CPhysCollide* pReturn;
	if (iConvexCount != 0)
	{
		CREATEDEBUGTIMER(collideTimer);
		STARTDEBUGTIMER(collideTimer);
		pReturn = physcollision->ConvertConvexToCollide(pConvexes, iConvexCount);
		STOPDEBUGTIMER(collideTimer);
		DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sCollideable Generation:%fms\n", s_iPortalSimulatorGUID, TABSPACING, collideTimer.GetDuration().GetMillisecondsF()); );
	}
	else
	{
		pReturn = NULL;
	}

	STOPDEBUGTIMER(functionTimer);
	DECREMENTTABSPACING();
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sConvertPolyhedronsToCollideable() FINISH: %fms\n", s_iPortalSimulatorGUID, TABSPACING, functionTimer.GetDuration().GetMillisecondsF()); );

	return pReturn;
}

void CEnginePortalInternal::CreateLocalCollision(void)
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

	//if (m_pOwningSimulator->IsSimulatingVPhysics())
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
		UTIL_TraceLine(GetAbsOrigin() + m_InternalData.Placement.vForward, GetAbsOrigin() - (m_InternalData.Placement.vForward * 500.0f), MASK_SOLID_BRUSHONLY, &filter, &Trace);

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
			m_InternalData.Simulation.Static.SurfaceProperties.pEntity = gEntList.GetBaseEntity(0);
#else
			m_InternalData.Simulation.Static.SurfaceProperties.pEntity = ClientEntityList().GetBaseEntity(0);
#endif
		}

#ifndef CLIENT_DLL
		//if( pCollisionEntity )
		m_InternalData.Simulation.Static.SurfaceProperties.pEntity = this->m_pOuter;
#endif		
	}
}

void CEnginePortalInternal::ClearLocalCollision(void)
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
		(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0))
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

void CEnginePortalInternal::CreateLocalPhysics(void)
{
	//int iDefaultSurfaceIndex = physprops->GetSurfaceIndex( "default" );
	objectparams_t params = g_PhysDefaultObjectParams;

	// Any non-moving object can point to world safely-- Make sure we dont use 'params' for something other than that beyond this point.
	//if( m_InternalData.Simulation.pCollisionEntity )
	params.pGameData = this->m_pOuter;
	//else
	//	GetWorldEntity();

	//World
	{
		Assert(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.World.Brushes.pCollideable != NULL)
		{
			m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject = pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.World.Brushes.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

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

				Representation.pPhysicsObject = pPhysicsEnvironment->CreatePolyObjectStatic(Representation.pCollide, Representation.iTraceSurfaceProps, vec3_origin, vec3_angle, &params);
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
			m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject = pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

			m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
		}

		Assert(m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable != NULL)
		{
			m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject = pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

			m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
		}
	}
}

void CEnginePortalInternal::CreateLinkedPhysics(IEnginePortalServer* pRemoteCollisionEntity)
{
	CEnginePortalInternal* pRemotePortalInternal = dynamic_cast<CEnginePortalInternal*>(pRemoteCollisionEntity);
	//int iDefaultSurfaceIndex = physprops->GetSurfaceIndex( "default" );
	objectparams_t params = g_PhysDefaultObjectParams;

	//if( pCollisionEntity )
	params.pGameData = this->m_pOuter;
	//else
	//	params.pGameData = GetWorldEntity();

	//everything in our linked collision should be based on the linked portal's world collision
	PS_SD_Static_World_t& RemoteSimulationStaticWorld = pRemotePortalInternal->m_InternalData.Simulation.Static.World;

	Assert(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (RemoteSimulationStaticWorld.Brushes.pCollideable != NULL)
	{
		m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject = pPhysicsEnvironment->CreatePolyObjectStatic(RemoteSimulationStaticWorld.Brushes.pCollideable, pRemotePortalInternal->m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, m_InternalData.Placement.ptaap_LinkedToThis.ptOriginTransform, m_InternalData.Placement.ptaap_LinkedToThis.qAngleTransform, &params);
		m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
	}


	Assert(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count() == 0); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations.Count() != 0)
	{
		for (int i = RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations[i];
			IPhysicsObject* pPhysObject = pPhysicsEnvironment->CreatePolyObjectStatic(Representation.pCollide, Representation.iTraceSurfaceProps, m_InternalData.Placement.ptaap_LinkedToThis.ptOriginTransform, m_InternalData.Placement.ptaap_LinkedToThis.qAngleTransform, &params);
			if (pPhysObject)
			{
				m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.AddToTail(pPhysObject);
				pPhysObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
			}
		}
	}
}

void CEnginePortalInternal::ClearLocalPhysics(void)
{
	if (m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject)
	{
		pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);
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
				pPhysicsEnvironment->DestroyObject(Representation.pPhysicsObject);
				Representation.pPhysicsObject = NULL;
			}
		}
	}
	m_InternalData.Simulation.Static.World.StaticProps.bPhysicsExists = false;

	if (m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject)
	{
		pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject);
		m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject = NULL;
	}

	if (m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject)
	{
		pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject);
		m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject = NULL;
	}
	VPhysicsSetObject(NULL);
}

void CEnginePortalInternal::ClearLinkedPhysics(void)
{
	//static collideables
	{
		if (m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject)
		{
			pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject);
			m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject = NULL;
		}

		if (m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count())
		{
			for (int i = m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count(); --i >= 0; )
				pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects[i]);

			m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.RemoveAll();
		}
	}
}

void CEnginePortalInternal::CreateHoleShapeCollideable()
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
		fHolePlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * (PORTAL_HALF_HEIGHT * 0.98f)));

		fHolePlanes[(3 * 4) + 0] = -m_InternalData.Placement.vUp.x;
		fHolePlanes[(3 * 4) + 1] = -m_InternalData.Placement.vUp.y;
		fHolePlanes[(3 * 4) + 2] = -m_InternalData.Placement.vUp.z;
		fHolePlanes[(3 * 4) + 3] = -m_InternalData.Placement.vUp.Dot(GetAbsOrigin() - (m_InternalData.Placement.vUp * (PORTAL_HALF_HEIGHT * 0.98f)));

		fHolePlanes[(4 * 4) + 0] = -m_InternalData.Placement.vRight.x;
		fHolePlanes[(4 * 4) + 1] = -m_InternalData.Placement.vRight.y;
		fHolePlanes[(4 * 4) + 2] = -m_InternalData.Placement.vRight.z;
		fHolePlanes[(4 * 4) + 3] = -m_InternalData.Placement.vRight.Dot(GetAbsOrigin() - (m_InternalData.Placement.vRight * (PORTAL_HALF_WIDTH * 0.98f)));

		fHolePlanes[(5 * 4) + 0] = m_InternalData.Placement.vRight.x;
		fHolePlanes[(5 * 4) + 1] = m_InternalData.Placement.vRight.y;
		fHolePlanes[(5 * 4) + 2] = m_InternalData.Placement.vRight.z;
		fHolePlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + (m_InternalData.Placement.vRight * (PORTAL_HALF_WIDTH * 0.98f)));

		CPolyhedron* pPolyhedron = GeneratePolyhedronFromPlanes(fHolePlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON, true);
		Assert(pPolyhedron != NULL);
		CPhysConvex* pConvex = physcollision->ConvexFromConvexPolyhedron(*pPolyhedron);
		pPolyhedron->Release();
		Assert(pConvex != NULL);
		m_InternalData.Placement.pHoleShapeCollideable = physcollision->ConvertConvexToCollide(&pConvex, 1);
	}
}

void CEnginePortalInternal::ClearHoleShapeCollideable()
{
	if (m_InternalData.Placement.pHoleShapeCollideable) {
		physcollision->DestroyCollide(m_InternalData.Placement.pHoleShapeCollideable);
		m_InternalData.Placement.pHoleShapeCollideable = NULL;
	}
}

bool CEnginePortalInternal::CreatedPhysicsObject(const IPhysicsObject* pObject, PS_PhysicsObjectSourceType_t* pOut_SourceType) const
{
	if ((pObject == m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject) || (pObject == m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject))
	{
		if (pOut_SourceType)
			*pOut_SourceType = PSPOST_LOCAL_BRUSHES;

		return true;
	}

	if (pObject == m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject)
	{
		if (pOut_SourceType)
			*pOut_SourceType = PSPOST_REMOTE_BRUSHES;

		return true;
	}

	for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
	{
		if (m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pPhysicsObject == pObject)
		{
			if (pOut_SourceType)
				*pOut_SourceType = PSPOST_LOCAL_STATICPROPS;
			return true;
		}
	}

	for (int i = m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count(); --i >= 0; )
	{
		if (m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects[i] == pObject)
		{
			if (pOut_SourceType)
				*pOut_SourceType = PSPOST_REMOTE_STATICPROPS;

			return true;
		}
	}

	if (pObject == m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject)
	{
		if (pOut_SourceType)
			*pOut_SourceType = PSPOST_HOLYWALL_TUBE;

		return true;
	}

	return false;
}

void CEngineShadowCloneInternal::SetClonedEntity(CBaseEntity* pEntToClone)
{
	VPhysicsDestroyObject();

	m_hClonedEntity = pEntToClone;

	//FullSyncClonedPhysicsObjects();
}

CBaseEntity* CEngineShadowCloneInternal::GetClonedEntity(void)
{
	return m_hClonedEntity.Get();
}

void CEngineShadowCloneInternal::VPhysicsDestroyObject(void)
{
	VPhysicsSetObject(NULL);

	for (int i = m_CloneLinks.Count(); --i >= 0; )
	{
		Assert(m_CloneLinks[i].pClone != NULL);
		m_pOwnerPhysEnvironment->DestroyObject(m_CloneLinks[i].pClone);
	}
	m_CloneLinks.RemoveAll();

	//BaseClass::VPhysicsDestroyObject();
}

int CEngineShadowCloneInternal::VPhysicsGetObjectList(IPhysicsObject** pList, int listMax)
{
	int iCountStop = m_CloneLinks.Count();
	if (iCountStop > listMax)
		iCountStop = listMax;

	for (int i = 0; i != iCountStop; ++i, ++pList)
		*pList = m_CloneLinks[i].pClone;

	return iCountStop;
}

static void DrawDebugOverlayForShadowClone(CEngineShadowCloneInternal* pClone)
{
	unsigned char iColorIntensity = (pClone->IsInAssumedSyncState()) ? (127) : (255);

	int iRed = (pClone->IsUntransformedClone()) ? (0) : (iColorIntensity);
	int iGreen = iColorIntensity;
	int iBlue = iColorIntensity;

	NDebugOverlay::EntityBounds(pClone->m_pOuter, iRed, iGreen, iBlue, (iColorIntensity >> 2), 0.05f);
}

void CEngineShadowCloneInternal::FullSync(bool bAllowAssumedSync)
{
	Assert(IsMarkedForDeletion() == false);

	CBaseEntity* pClonedEntity = m_hClonedEntity.Get();

	if (pClonedEntity == NULL)
	{
		AssertMsg(VPhysicsGetObject() != NULL, "Been linkless for more than this update, something should have killed this clone.");
		SetMoveType(MOVETYPE_NONE);
		SetSolid(SOLID_NONE);
		SetSolidFlags(0);
		SetCollisionGroup(COLLISION_GROUP_NONE);
		VPhysicsDestroyObject();
		return;
	}

	SetGroundEntity(NULL);

	bool bIsSynced = bAllowAssumedSync;
	bool bBigChanges = true; //assume there are, and be proven wrong

	if (bAllowAssumedSync)
	{
		IPhysicsObject* pSourceObjects[1024];
		int iObjectCount = pClonedEntity->VPhysicsGetObjectList(pSourceObjects, 1024);

		//scan for really big differences that would definitely require a full sync
		bBigChanges = (iObjectCount != m_CloneLinks.Count());
		if (!bBigChanges)
		{
			for (int i = 0; i != iObjectCount; ++i)
			{
				IPhysicsObject* pSourcePhysics = pSourceObjects[i];
				IPhysicsObject* pClonedPhysics = m_CloneLinks[i].pClone;

				if ((pSourcePhysics != m_CloneLinks[i].pSource) ||
					(pSourcePhysics->IsCollisionEnabled() != pClonedPhysics->IsCollisionEnabled()))
				{
					bBigChanges = true;
					bIsSynced = false;
					break;
				}

				Vector ptSourcePosition, ptClonePosition;
				pSourcePhysics->GetPosition(&ptSourcePosition, NULL);
				if (!m_bShadowTransformIsIdentity)
					ptSourcePosition = m_matrixShadowTransform * ptSourcePosition;

				pClonedPhysics->GetPosition(&ptClonePosition, NULL);

				if ((ptClonePosition - ptSourcePosition).LengthSqr() > 2500.0f)
				{
					bBigChanges = true;
					bIsSynced = false;
					break;
				}

				//Vector vSourceVelocity, vCloneVelocity;


				if (!pSourcePhysics->IsAsleep()) //only allow full syncrosity if the source entity is entirely asleep
					bIsSynced = false;

				if (m_bInAssumedSyncState && !pClonedPhysics->IsAsleep())
					bIsSynced = false;
			}
		}
		else
		{
			bIsSynced = false;
		}

		bIsSynced = false;

		if (bIsSynced)
		{
			//good enough to skip a full update
			if (!m_bInAssumedSyncState)
			{
				//do one last sync
				PartialSync(true);

				//if we don't do this, objects just fall out of the world (it happens, I swear)

				for (int i = m_CloneLinks.Count(); --i >= 0; )
				{
					if ((m_CloneLinks[i].pSource->GetShadowController() == NULL) && m_CloneLinks[i].pClone->IsMotionEnabled())
					{
						//m_CloneLinks[i].pClone->SetVelocityInstantaneous( &vec3_origin, &vec3_origin );
						//m_CloneLinks[i].pClone->SetVelocity( &vec3_origin, &vec3_origin );
						m_CloneLinks[i].pClone->EnableGravity(false);
						m_CloneLinks[i].pClone->EnableMotion(false);
						m_CloneLinks[i].pClone->Sleep();
					}
				}

				m_bInAssumedSyncState = true;
			}

			if (sv_debug_physicsshadowclones.GetBool())
				DrawDebugOverlayForShadowClone(this);

			return;
		}
	}

	m_bInAssumedSyncState = false;





	//past this point, we're committed to a broad update

	if (bBigChanges)
	{
		MoveType_t sourceMoveType = pClonedEntity->GetEngineObject()->GetMoveType();


		IPhysicsObject* pPhysObject = pClonedEntity->VPhysicsGetObject();
		if ((sourceMoveType == MOVETYPE_CUSTOM) ||
			(sourceMoveType == MOVETYPE_STEP) ||
			(sourceMoveType == MOVETYPE_WALK) ||
			(pPhysObject &&
				(
					(pPhysObject->GetGameFlags() & FVPHYSICS_PLAYER_HELD) ||
					(pPhysObject->GetShadowController() != NULL)
					)
				)
			)
		{
			//#ifdef _DEBUG
			SetMoveType(MOVETYPE_NONE); //to kill an assert
			//#endif
						//PUSH should be used sparingly, you can't stand on a MOVETYPE_PUSH object :/
			SetMoveType(MOVETYPE_VPHYSICS, pClonedEntity->GetEngineObject()->GetMoveCollide()); //either an unclonable movetype, or a shadow/held object
		}
		/*else if(sourceMoveType == MOVETYPE_STEP)
		{
			//GetEngineObject()->SetMoveType( MOVETYPE_NONE ); //to kill an assert
			GetEngineObject()->SetMoveType( MOVETYPE_VPHYSICS, pClonedEntity->GetMoveCollide() );
		}*/
		else
		{
			//if( m_bShadowTransformIsIdentity )
			SetMoveType(sourceMoveType, pClonedEntity->GetEngineObject()->GetMoveCollide());
			//else
			//{
			//	GetEngineObject()->SetMoveType( MOVETYPE_NONE ); //to kill an assert
			//	GetEngineObject()->SetMoveType( MOVETYPE_PUSH, pClonedEntity->GetMoveCollide() );
			//}
		}

		SolidType_t sourceSolidType = pClonedEntity->GetEngineObject()->GetSolid();
		if (sourceSolidType == SOLID_BBOX)
			SetSolid(SOLID_VPHYSICS);
		else
			SetSolid(sourceSolidType);
		//SetSolid( SOLID_VPHYSICS );

		SetElasticity(pClonedEntity->GetEngineObject()->GetElasticity());
		SetFriction(pClonedEntity->GetEngineObject()->GetFriction());



		int iSolidFlags = pClonedEntity->GetEngineObject()->GetSolidFlags() | FSOLID_CUSTOMRAYTEST;
		if (m_bShadowTransformIsIdentity)
			iSolidFlags |= FSOLID_CUSTOMBOXTEST; //need this at least for the player or they get stuck in themselves
		else
			iSolidFlags &= ~FSOLID_FORCE_WORLD_ALIGNED;
		/*if( pClonedEntity->IsPlayer() )
		{
			iSolidFlags |= FSOLID_CUSTOMRAYTEST | FSOLID_CUSTOMBOXTEST;
		}*/

		SetSolidFlags(iSolidFlags);



		SetEffects(pClonedEntity->GetEngineObject()->GetEffects() | (EF_NODRAW | EF_NOSHADOW | EF_NORECEIVESHADOW));

		SetCollisionGroup(pClonedEntity->GetEngineObject()->GetCollisionGroup());

		SetModelIndex(pClonedEntity->GetEngineObject()->GetModelIndex());
		SetModelName(pClonedEntity->GetEngineObject()->GetModelName());

		if (modelinfo->GetModelType(pClonedEntity->GetEngineObject()->GetModel()) == mod_studio)
			m_pOuter->SetModel(STRING(pClonedEntity->GetEngineObject()->GetModelName()));

		m_pOuter->SetSize(pClonedEntity->GetEngineObject()->OBBMins(), pClonedEntity->GetEngineObject()->OBBMaxs());
	}

	FullSyncClonedPhysicsObjects(bBigChanges);
	SyncEntity(true);

	if (bBigChanges)
		CollisionRulesChanged();

	if (sv_debug_physicsshadowclones.GetBool())
		DrawDebugOverlayForShadowClone(this);
}

void CEngineShadowCloneInternal::SyncEntity(bool bPullChanges)
{
	m_bShouldUpSync = false;

	CBaseEntity* pSource, * pDest;
	VMatrix* pTransform;
	if (bPullChanges)
	{
		pSource = m_hClonedEntity.Get();
		pDest = this->m_pOuter;
		pTransform = &m_matrixShadowTransform;

		if (pSource == NULL)
			return;
	}
	else
	{
		pSource = this->m_pOuter;
		pDest = m_hClonedEntity.Get();
		pTransform = &m_matrixShadowTransform_Inverse;

		if (pDest == NULL)
			return;
	}


	Vector ptOrigin, vVelocity;
	QAngle qAngles;

	ptOrigin = pSource->GetEngineObject()->GetAbsOrigin();
	qAngles = pSource->GetEngineObject()->GetAbsAngles();
	vVelocity = pSource->GetEngineObject()->GetAbsVelocity();

	if (!m_bShadowTransformIsIdentity)
	{
		ptOrigin = (*pTransform) * ptOrigin;
		qAngles = TransformAnglesToWorldSpace(qAngles, pTransform->As3x4());
		vVelocity = pTransform->ApplyRotation(vVelocity);
	}
	//else
	//{
	//	pDest->SetGroundEntity( pSource->GetGroundEntity() );
	//}

	if ((ptOrigin != pDest->GetEngineObject()->GetAbsOrigin()) || (qAngles != pDest->GetEngineObject()->GetAbsAngles()))
	{
		pDest->Teleport(&ptOrigin, &qAngles, NULL);
	}

	if (vVelocity != pDest->GetEngineObject()->GetAbsVelocity())
	{
		//pDest->IncrementInterpolationFrame();
		pDest->GetEngineObject()->SetAbsVelocity(vec3_origin); //the two step process helps, I don't know why, but it does
		pDest->ApplyAbsVelocityImpulse(vVelocity);
	}
}

static void FullSyncPhysicsObject(IPhysicsObject* pSource, IPhysicsObject* pDest, const VMatrix* pTransform, bool bTeleport)
{
	CGrabController* pGrabController = NULL;

	if (!pSource->IsAsleep())
		pDest->Wake();

	float fSavedMass = 0.0f, fSavedRotationalDamping; //setting mass to 0.0f purely to kill a warning that I can't seem to kill with pragmas
#ifdef PORTAL
	if (pSource->GetGameFlags() & FVPHYSICS_PLAYER_HELD)
	{
		//CBasePlayer *pPlayer = UTIL_PlayerByIndex( 1 );
		//Assert( pPlayer );

		CBaseEntity* pLookingForEntity = (CBaseEntity*)pSource->GetGameData();

		CBasePlayer* pHoldingPlayer = GetPlayerHoldingEntity(pLookingForEntity);
		if (pHoldingPlayer)
		{
			pGrabController = GetGrabControllerForPlayer(pHoldingPlayer);

			if (!pGrabController)
				pGrabController = GetGrabControllerForPhysCannon(pHoldingPlayer->GetActiveWeapon());
		}

		AssertMsg(pGrabController, "Physics object is held, but we can't find the holding controller.");
		GetSavedParamsForCarriedPhysObject(pGrabController, pSource, &fSavedMass, &fSavedRotationalDamping);
	}
#endif // PORTAL

	//Boiler plate
	{
		pDest->SetGameIndex(pSource->GetGameIndex()); //what's it do?
		pDest->SetCallbackFlags(pSource->GetCallbackFlags()); //wise?
		pDest->SetGameFlags(pSource->GetGameFlags() | FVPHYSICS_NO_SELF_COLLISIONS | FVPHYSICS_IS_SHADOWCLONE);
		pDest->SetMaterialIndex(pSource->GetMaterialIndex());
		pDest->SetContents(pSource->GetContents());

		pDest->EnableCollisions(pSource->IsCollisionEnabled());
		pDest->EnableGravity(pSource->IsGravityEnabled());
		pDest->EnableDrag(pSource->IsDragEnabled());
		pDest->EnableMotion(pSource->IsMotionEnabled());
	}

	//Damping
	{
		float fSpeedDamp, fRotDamp;
		if (pGrabController)
		{
			pSource->GetDamping(&fSpeedDamp, NULL);
			pDest->SetDamping(&fSpeedDamp, &fSavedRotationalDamping);
		}
		else
		{
			pSource->GetDamping(&fSpeedDamp, &fRotDamp);
			pDest->SetDamping(&fSpeedDamp, &fRotDamp);
		}
	}

	//stuff that we really care about
	{
		if (pGrabController)
			pDest->SetMass(fSavedMass);
		else
			pDest->SetMass(pSource->GetMass());

		Vector ptOrigin, vVelocity, vAngularVelocity, vInertia;
		QAngle qAngles;

		pSource->GetPosition(&ptOrigin, &qAngles);
		pSource->GetVelocity(&vVelocity, &vAngularVelocity);
		vInertia = pSource->GetInertia();

		if (pTransform)
		{
#if 0
			pDest->SetPositionMatrix(pTransform->As3x4(), true); //works like we think?
#else		
			ptOrigin = (*pTransform) * ptOrigin;
			qAngles = TransformAnglesToWorldSpace(qAngles, pTransform->As3x4());
			vVelocity = pTransform->ApplyRotation(vVelocity);
			vAngularVelocity = pTransform->ApplyRotation(vAngularVelocity);
#endif
		}

		//avoid oversetting variables (I think that even setting them to the same value they already are disrupts the delicate physics balance)
		if (vInertia != pDest->GetInertia())
			pDest->SetInertia(vInertia);

		Vector ptDestOrigin, vDestVelocity, vDestAngularVelocity;
		QAngle qDestAngles;
		pDest->GetPosition(&ptDestOrigin, &qDestAngles);

		if ((ptOrigin != ptDestOrigin) || (qAngles != qDestAngles))
			pDest->SetPosition(ptOrigin, qAngles, bTeleport);

		//pDest->SetVelocityInstantaneous( &vec3_origin, &vec3_origin );
		//pDest->Sleep();

		pDest->GetVelocity(&vDestVelocity, &vDestAngularVelocity);

		if ((vVelocity != vDestVelocity) || (vAngularVelocity != vDestAngularVelocity))
			pDest->SetVelocityInstantaneous(&vVelocity, &vAngularVelocity);

		IPhysicsShadowController* pSourceController = pSource->GetShadowController();
		if (pSourceController == NULL)
		{
			if (pDest->GetShadowController() != NULL)
			{
				//we don't need a shadow controller anymore
				pDest->RemoveShadowController();
			}
		}
		else
		{
			IPhysicsShadowController* pDestController = pDest->GetShadowController();
			if (pDestController == NULL)
			{
				//we need a shadow controller
				float fMaxSpeed, fMaxAngularSpeed;
				pSourceController->GetMaxSpeed(&fMaxSpeed, &fMaxAngularSpeed);

				pDest->SetShadow(fMaxSpeed, fMaxAngularSpeed, pSourceController->AllowsTranslation(), pSourceController->AllowsRotation());
				pDestController = pDest->GetShadowController();
				pDestController->SetTeleportDistance(pSourceController->GetTeleportDistance());
				pDestController->SetPhysicallyControlled(pSourceController->IsPhysicallyControlled());
			}

			//sync shadow controllers
			float fTimeOffset;
			Vector ptTargetPosition;
			QAngle qTargetAngles;
			fTimeOffset = pSourceController->GetTargetPosition(&ptTargetPosition, &qTargetAngles);

			if (pTransform)
			{
				ptTargetPosition = (*pTransform) * ptTargetPosition;
				qTargetAngles = TransformAnglesToWorldSpace(qTargetAngles, pTransform->As3x4());
			}

			pDestController->Update(ptTargetPosition, qTargetAngles, fTimeOffset);
		}


	}

	//pDest->RecheckContactPoints();
}

static void PartialSyncPhysicsObject(IPhysicsObject* pSource, IPhysicsObject* pDest, const VMatrix* pTransform)
{
	Vector ptOrigin, vVelocity, vAngularVelocity, vInertia;
	QAngle qAngles;

	pSource->GetPosition(&ptOrigin, &qAngles);
	pSource->GetVelocity(&vVelocity, &vAngularVelocity);
	vInertia = pSource->GetInertia();

	if (pTransform)
	{
#if 0
		//pDest->SetPositionMatrix( matTransform.As3x4(), true ); //works like we think?
#else	
		ptOrigin = (*pTransform) * ptOrigin;
		qAngles = TransformAnglesToWorldSpace(qAngles, pTransform->As3x4());
		vVelocity = pTransform->ApplyRotation(vVelocity);
		vAngularVelocity = pTransform->ApplyRotation(vAngularVelocity);
#endif
	}

	//avoid oversetting variables (I think that even setting them to the same value they already are disrupts the delicate physics balance)
	if (vInertia != pDest->GetInertia())
		pDest->SetInertia(vInertia);

	Vector ptDestOrigin, vDestVelocity, vDestAngularVelocity;
	QAngle qDestAngles;
	pDest->GetPosition(&ptDestOrigin, &qDestAngles);
	pDest->GetVelocity(&vDestVelocity, &vDestAngularVelocity);


	if ((ptOrigin != ptDestOrigin) || (qAngles != qDestAngles))
		pDest->SetPosition(ptOrigin, qAngles, false);

	if ((vVelocity != vDestVelocity) || (vAngularVelocity != vDestAngularVelocity))
		pDest->SetVelocity(&vVelocity, &vAngularVelocity);

	pDest->EnableCollisions(pSource->IsCollisionEnabled());
}


void CEngineShadowCloneInternal::FullSyncClonedPhysicsObjects(bool bTeleport)
{
	CBaseEntity* pClonedEntity = m_hClonedEntity.Get();
	if (pClonedEntity == NULL)
	{
		VPhysicsDestroyObject();
		return;
	}

	VMatrix* pTransform;
	if (m_bShadowTransformIsIdentity)
		pTransform = NULL;
	else
		pTransform = &m_matrixShadowTransform;

	IPhysicsObject* (pSourceObjects[1024]);
	int iObjectCount = pClonedEntity->VPhysicsGetObjectList(pSourceObjects, 1024);

	//easy out if nothing has changed
	if (iObjectCount == m_CloneLinks.Count())
	{
		int i;
		for (i = 0; i != iObjectCount; ++i)
		{
			if (pSourceObjects[i] == NULL)
				break;

			if (pSourceObjects[i] != m_CloneLinks[i].pSource)
				break;
		}

		if (i == iObjectCount) //no changes
		{
			for (i = 0; i != iObjectCount; ++i)
				FullSyncPhysicsObject(m_CloneLinks[i].pSource, m_CloneLinks[i].pClone, pTransform, bTeleport);

			return;
		}
	}



	//copy the existing list of clone links to a temp array, we're going to be starting from scratch and copying links as we need them
	PhysicsObjectCloneLink_t* pExistingLinks = NULL;
	int iExistingLinkCount = m_CloneLinks.Count();
	if (iExistingLinkCount != 0)
	{
		pExistingLinks = (PhysicsObjectCloneLink_t*)stackalloc(sizeof(PhysicsObjectCloneLink_t) * m_CloneLinks.Count());
		memcpy(pExistingLinks, m_CloneLinks.Base(), sizeof(PhysicsObjectCloneLink_t) * m_CloneLinks.Count());
	}
	m_CloneLinks.RemoveAll();

	//now, go over the object list we just got from the source entity, and either copy or create links as necessary
	int i;
	for (i = 0; i != iObjectCount; ++i)
	{
		IPhysicsObject* pSource = pSourceObjects[i];

		if (pSource == NULL) //this really shouldn't happen, but it does >_<
			continue;

		PhysicsObjectCloneLink_t cloneLink;

		int j;
		for (j = 0; j != iExistingLinkCount; ++j)
		{
			if (pExistingLinks[j].pSource == pSource)
				break;
		}

		if (j != iExistingLinkCount)
		{
			//copyable link found
			cloneLink = pExistingLinks[j];
			memset(&pExistingLinks[j], 0, sizeof(PhysicsObjectCloneLink_t)); //zero out this slot so we don't destroy it in cleanup
		}
		else
		{
			//no link found to copy, create a new one
			cloneLink.pSource = pSource;

			//apparently some collision code gets called on creation before we've set extra game flags, so we're going to cheat a bit and temporarily set our extra flags on the source
			unsigned int iOldGameFlags = pSource->GetGameFlags();
			pSource->SetGameFlags(iOldGameFlags | FVPHYSICS_IS_SHADOWCLONE);

			unsigned int size = physenv->GetObjectSerializeSize(pSource);
			byte* pBuffer = (byte*)stackalloc(size);
			memset(pBuffer, 0, size);

			physenv->SerializeObjectToBuffer(pSource, pBuffer, size); //this should work across physics environments because the serializer doesn't write anything about itself to the template
			pSource->SetGameFlags(iOldGameFlags);
			cloneLink.pClone = m_pOwnerPhysEnvironment->UnserializeObjectFromBuffer(this->m_pOuter, pBuffer, size, false); //unserializer has to be in the target environment
			assert(cloneLink.pClone); //there should be absolutely no case where we can't clone a valid existing physics object

			stackfree(pBuffer);
		}

		FullSyncPhysicsObject(cloneLink.pSource, cloneLink.pClone, pTransform, bTeleport);

		//cloneLink.pClone->Wake();

		m_CloneLinks.AddToTail(cloneLink);
	}


	//now go over the existing links, if any of them haven't been nullified, they need to be deleted
	for (i = 0; i != iExistingLinkCount; ++i)
	{
		if (pExistingLinks[i].pClone)
			m_pOwnerPhysEnvironment->DestroyObject(pExistingLinks[i].pClone); //also destroys shadow controller
	}


	VPhysicsSetObject(NULL);

	IPhysicsObject* pSource = m_hClonedEntity->VPhysicsGetObject();

	for (i = m_CloneLinks.Count(); --i >= 0; )
	{
		if (m_CloneLinks[i].pSource == pSource)
		{
			//m_CloneLinks[i].pClone->Wake();
			VPhysicsSetObject(m_CloneLinks[i].pClone);
			break;
		}
	}

	if ((i < 0) && (m_CloneLinks.Count() != 0))
	{
		VPhysicsSetObject(m_CloneLinks[0].pClone);
	}

	stackfree(pExistingLinks);

	//CollisionRulesChanged();
}



void CEngineShadowCloneInternal::PartialSync(bool bPullChanges)
{
	VMatrix* pTransform;

	if (bPullChanges)
	{
		if (m_bShadowTransformIsIdentity)
			pTransform = NULL;
		else
			pTransform = &m_matrixShadowTransform;

		for (int i = m_CloneLinks.Count(); --i >= 0; )
			PartialSyncPhysicsObject(m_CloneLinks[i].pSource, m_CloneLinks[i].pClone, pTransform);
	}
	else
	{
		if (m_bShadowTransformIsIdentity)
			pTransform = NULL;
		else
			pTransform = &m_matrixShadowTransform_Inverse;

		for (int i = m_CloneLinks.Count(); --i >= 0; )
			PartialSyncPhysicsObject(m_CloneLinks[i].pClone, m_CloneLinks[i].pSource, pTransform);
	}

	SyncEntity(bPullChanges);
}

void CEngineShadowCloneInternal::SetCloneTransformationMatrix(const matrix3x4_t& sourceMatrix)
{
	m_matrixShadowTransform = sourceMatrix;
	m_bShadowTransformIsIdentity = m_matrixShadowTransform.IsIdentity();

	if (!m_bShadowTransformIsIdentity)
	{
		if (m_matrixShadowTransform.InverseGeneral(m_matrixShadowTransform_Inverse) == false)
		{
			m_matrixShadowTransform.InverseTR(m_matrixShadowTransform_Inverse); //probably not the right matrix, but we're out of options
		}
	}

	FullSync();
	//PartialSync( true );
}

IPhysicsObject* CEngineShadowCloneInternal::TranslatePhysicsToClonedEnt(const IPhysicsObject* pPhysics)
{
	if (m_hClonedEntity.Get() != NULL)
	{
		for (int i = m_CloneLinks.Count(); --i >= 0; )
		{
			if (m_CloneLinks[i].pClone == pPhysics)
				return m_CloneLinks[i].pSource;
		}
	}

	return NULL;
}

// remaps an angular variable to a 3 band function:
// 0 <= t < start :		f(t) = 0
// start <= t <= end :	f(t) = end * spline(( t-start) / (end-start) )  // s curve between clamped and linear
// end < t :			f(t) = t
float RemapAngleRange(float startInterval, float endInterval, float value)
{
	// Fixup the roll
	value = AngleNormalize(value);
	float absAngle = fabs(value);

	// beneath cutoff?
	if (absAngle < startInterval)
	{
		value = 0;
	}
	// in spline range?
	else if (absAngle <= endInterval)
	{
		float newAngle = SimpleSpline((absAngle - startInterval) / (endInterval - startInterval)) * endInterval;
		// grab the sign from the initial value
		if (value < 0)
		{
			newAngle *= -1;
		}
		value = newAngle;
	}
	// else leave it alone, in linear range

	return value;
}

BEGIN_DATADESC(CEngineVehicleInternal)

// These two are reset every time 
//	DEFINE_FIELD( m_pOuter, FIELD_EHANDLE ),
//											m_pOuterServerVehicle;

	// Quiet down classcheck
	// DEFINE_FIELD( m_controls, vehicle_controlparams_t ),

	// Controls
	DEFINE_FIELD(m_controls.throttle, FIELD_FLOAT),
	DEFINE_FIELD(m_controls.steering, FIELD_FLOAT),
	DEFINE_FIELD(m_controls.brake, FIELD_FLOAT),
	DEFINE_FIELD(m_controls.boost, FIELD_FLOAT),
	DEFINE_FIELD(m_controls.handbrake, FIELD_BOOLEAN),
	DEFINE_FIELD(m_controls.handbrakeLeft, FIELD_BOOLEAN),
	DEFINE_FIELD(m_controls.handbrakeRight, FIELD_BOOLEAN),
	DEFINE_FIELD(m_controls.brakepedal, FIELD_BOOLEAN),
	DEFINE_FIELD(m_controls.bHasBrakePedal, FIELD_BOOLEAN),

	// This has to be handled by the containing class owing to 'owner' issues
//	DEFINE_PHYSPTR( m_pVehicle ),
	DEFINE_PHYSPTR(m_pVehicle),

	DEFINE_FIELD(m_nSpeed, FIELD_INTEGER),
	DEFINE_FIELD(m_nLastSpeed, FIELD_INTEGER),
	DEFINE_FIELD(m_nRPM, FIELD_INTEGER),
	DEFINE_FIELD(m_fLastBoost, FIELD_FLOAT),
	DEFINE_FIELD(m_nBoostTimeLeft, FIELD_INTEGER),
	DEFINE_FIELD(m_nHasBoost, FIELD_INTEGER),

	DEFINE_FIELD(m_maxThrottle, FIELD_FLOAT),
	DEFINE_FIELD(m_flMaxRevThrottle, FIELD_FLOAT),
	DEFINE_FIELD(m_flMaxSpeed, FIELD_FLOAT),
	DEFINE_FIELD(m_actionSpeed, FIELD_FLOAT),

	// This has to be handled by the containing class owing to 'owner' issues
	//	DEFINE_PHYSPTR_ARRAY( m_pWheels ),
	DEFINE_PHYSPTR_ARRAY(m_pWheels),

	DEFINE_FIELD(m_wheelCount, FIELD_INTEGER),

	DEFINE_ARRAY(m_wheelPosition, FIELD_VECTOR, 4),
	DEFINE_ARRAY(m_wheelRotation, FIELD_VECTOR, 4),
	DEFINE_ARRAY(m_wheelBaseHeight, FIELD_FLOAT, 4),
	DEFINE_ARRAY(m_wheelTotalHeight, FIELD_FLOAT, 4),
	DEFINE_ARRAY(m_poseParameters, FIELD_INTEGER, 12),
	DEFINE_FIELD(m_actionValue, FIELD_FLOAT),
	DEFINE_KEYFIELD(m_actionScale, FIELD_FLOAT, "actionScale"),
	DEFINE_FIELD(m_debugRadius, FIELD_FLOAT),
	DEFINE_FIELD(m_throttleRate, FIELD_FLOAT),
	DEFINE_FIELD(m_throttleStartTime, FIELD_FLOAT),
	DEFINE_FIELD(m_throttleActiveTime, FIELD_FLOAT),
	DEFINE_FIELD(m_turboTimer, FIELD_FLOAT),

	DEFINE_FIELD(m_flVehicleVolume, FIELD_FLOAT),
	DEFINE_FIELD(m_bIsOn, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bLastThrottle, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bLastBoost, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bLastSkid, FIELD_BOOLEAN),
END_DATADESC()

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CEngineVehicleInternal::CEngineVehicleInternal()
{
	m_flVehicleVolume = 0.5;
	//m_pOuter = NULL;
	//m_pOuterServerVehicle = NULL;
	m_flMaxSpeed = 30;
}

//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
CEngineVehicleInternal::~CEngineVehicleInternal()
{
	physenv->DestroyVehicleController(m_pVehicle);
}

//-----------------------------------------------------------------------------
// A couple wrapper methods to perform common operations
//-----------------------------------------------------------------------------
//inline int CEngineVehicleInternal::LookupPoseParameter(const char* szName)
//{
//	return m_pOuter->GetEngineObject()->LookupPoseParameter(szName);
//}
//
//inline float CEngineVehicleInternal::GetPoseParameter(int iParameter)
//{
//	return m_pOuter->GetEngineObject()->GetPoseParameter(iParameter);
//}
//
//inline float CEngineVehicleInternal::SetPoseParameter(int iParameter, float flValue)
//{
//	Assert(IsFinite(flValue));
//	return m_pOuter->GetEngineObject()->SetPoseParameter(iParameter, flValue);
//}

inline bool CEngineVehicleInternal::GetAttachment(const char* szName, Vector& origin, QAngle& angles)
{
	return ((CBaseAnimating*)m_pOuter)->GetAttachment(szName, origin, angles);
}

//-----------------------------------------------------------------------------
// Methods related to spawn
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::InitializePoseParameters()
{
	m_poseParameters[VEH_FL_WHEEL_HEIGHT] = LookupPoseParameter("vehicle_wheel_fl_height");
	m_poseParameters[VEH_FR_WHEEL_HEIGHT] = LookupPoseParameter("vehicle_wheel_fr_height");
	m_poseParameters[VEH_RL_WHEEL_HEIGHT] = LookupPoseParameter("vehicle_wheel_rl_height");
	m_poseParameters[VEH_RR_WHEEL_HEIGHT] = LookupPoseParameter("vehicle_wheel_rr_height");
	m_poseParameters[VEH_FL_WHEEL_SPIN] = LookupPoseParameter("vehicle_wheel_fl_spin");
	m_poseParameters[VEH_FR_WHEEL_SPIN] = LookupPoseParameter("vehicle_wheel_fr_spin");
	m_poseParameters[VEH_RL_WHEEL_SPIN] = LookupPoseParameter("vehicle_wheel_rl_spin");
	m_poseParameters[VEH_RR_WHEEL_SPIN] = LookupPoseParameter("vehicle_wheel_rr_spin");
	m_poseParameters[VEH_STEER] = LookupPoseParameter("vehicle_steer");
	m_poseParameters[VEH_ACTION] = LookupPoseParameter("vehicle_action");
	m_poseParameters[VEH_SPEEDO] = LookupPoseParameter("vehicle_guage");


	// move the wheels to a neutral position
	SetPoseParameter(m_poseParameters[VEH_SPEEDO], 0);
	SetPoseParameter(m_poseParameters[VEH_STEER], 0);
	SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_FR_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RR_WHEEL_HEIGHT], 0);
	((CBaseAnimating*)m_pOuter)->InvalidateBoneCache();
}

//-----------------------------------------------------------------------------
// Purpose: Parses the vehicle's script
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::ParseVehicleScript(const char* pScriptName, solid_t& solid, vehicleparams_t& vehicle)
{
	// Physics keeps a cache of these to share among spawns of vehicles or flush for debugging
	PhysFindOrAddVehicleScript(pScriptName, &vehicle, NULL);

	m_debugRadius = vehicle.axles[0].wheels.radius;
	CalcWheelData(vehicle);

	PhysModelParseSolid(solid, m_pOuter, m_pOuter->GetEngineObject()->GetModelIndex());

	// Allow the script to shift the center of mass
	if (vehicle.body.massCenterOverride != vec3_origin)
	{
		solid.massCenterOverride = vehicle.body.massCenterOverride;
		solid.params.massCenterOverride = &solid.massCenterOverride;
	}

	// allow script to change the mass of the vehicle body
	if (vehicle.body.massOverride > 0)
	{
		solid.params.mass = vehicle.body.massOverride;
	}

	return true;
}

void CEngineVehicleInternal::CalcWheelData(vehicleparams_t& vehicle)
{
	const char* pWheelAttachments[4] = { "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr" };
	Vector left, right;
	QAngle dummy;
	SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_FR_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RR_WHEEL_HEIGHT], 0);
	((CBaseAnimating*)m_pOuter)->InvalidateBoneCache();
	if (GetAttachment("wheel_fl", left, dummy) && GetAttachment("wheel_fr", right, dummy))
	{
		VectorITransform(left, m_pOuter->GetEngineObject()->EntityToWorldTransform(), left);
		VectorITransform(right, m_pOuter->GetEngineObject()->EntityToWorldTransform(), right);
		Vector center = (left + right) * 0.5;
		vehicle.axles[0].offset = center;
		vehicle.axles[0].wheelOffset = right - center;
		// Cache the base height of the wheels in body space
		m_wheelBaseHeight[0] = left.z;
		m_wheelBaseHeight[1] = right.z;
	}

	if (GetAttachment("wheel_rl", left, dummy) && GetAttachment("wheel_rr", right, dummy))
	{
		VectorITransform(left, m_pOuter->GetEngineObject()->EntityToWorldTransform(), left);
		VectorITransform(right, m_pOuter->GetEngineObject()->EntityToWorldTransform(), right);
		Vector center = (left + right) * 0.5;
		vehicle.axles[1].offset = center;
		vehicle.axles[1].wheelOffset = right - center;
		// Cache the base height of the wheels in body space
		m_wheelBaseHeight[2] = left.z;
		m_wheelBaseHeight[3] = right.z;
	}
	SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT], 1);
	SetPoseParameter(m_poseParameters[VEH_FR_WHEEL_HEIGHT], 1);
	SetPoseParameter(m_poseParameters[VEH_RL_WHEEL_HEIGHT], 1);
	SetPoseParameter(m_poseParameters[VEH_RR_WHEEL_HEIGHT], 1);
	((CBaseAnimating*)m_pOuter)->InvalidateBoneCache();
	if (GetAttachment("wheel_fl", left, dummy) && GetAttachment("wheel_fr", right, dummy))
	{
		VectorITransform(left, m_pOuter->GetEngineObject()->EntityToWorldTransform(), left);
		VectorITransform(right, m_pOuter->GetEngineObject()->EntityToWorldTransform(), right);
		// Cache the height range of the wheels in body space
		m_wheelTotalHeight[0] = m_wheelBaseHeight[0] - left.z;
		m_wheelTotalHeight[1] = m_wheelBaseHeight[1] - right.z;
		vehicle.axles[0].wheels.springAdditionalLength = m_wheelTotalHeight[0];
	}

	if (GetAttachment("wheel_rl", left, dummy) && GetAttachment("wheel_rr", right, dummy))
	{
		VectorITransform(left, m_pOuter->GetEngineObject()->EntityToWorldTransform(), left);
		VectorITransform(right, m_pOuter->GetEngineObject()->EntityToWorldTransform(), right);
		// Cache the height range of the wheels in body space
		m_wheelTotalHeight[2] = m_wheelBaseHeight[0] - left.z;
		m_wheelTotalHeight[3] = m_wheelBaseHeight[1] - right.z;
		vehicle.axles[1].wheels.springAdditionalLength = m_wheelTotalHeight[2];
	}
	for (int i = 0; i < 4; i++)
	{
		if (m_wheelTotalHeight[i] == 0.0f)
		{
			DevWarning("Vehicle %s has invalid wheel attachment for %s - no movement\n", STRING(m_pOuter->GetEngineObject()->GetModelName()), pWheelAttachments[i]);
			m_wheelTotalHeight[i] = 1.0f;
		}
	}

	SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_FR_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RR_WHEEL_HEIGHT], 0);
	((CBaseAnimating*)m_pOuter)->InvalidateBoneCache();

	// Get raytrace offsets if they exist.
	if (GetAttachment("raytrace_fl", left, dummy) && GetAttachment("raytrace_fr", right, dummy))
	{
		VectorITransform(left, m_pOuter->GetEngineObject()->EntityToWorldTransform(), left);
		VectorITransform(right, m_pOuter->GetEngineObject()->EntityToWorldTransform(), right);
		Vector center = (left + right) * 0.5;
		vehicle.axles[0].raytraceCenterOffset = center;
		vehicle.axles[0].raytraceOffset = right - center;
	}

	if (GetAttachment("raytrace_rl", left, dummy) && GetAttachment("raytrace_rr", right, dummy))
	{
		VectorITransform(left, m_pOuter->GetEngineObject()->EntityToWorldTransform(), left);
		VectorITransform(right, m_pOuter->GetEngineObject()->EntityToWorldTransform(), right);
		Vector center = (left + right) * 0.5;
		vehicle.axles[1].raytraceCenterOffset = center;
		vehicle.axles[1].raytraceOffset = right - center;
	}
}


//-----------------------------------------------------------------------------
// Spawns the vehicle
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::Spawn()
{
	Assert(m_pOuter);

	m_actionValue = 0;
	m_actionSpeed = 0;

	m_bIsOn = false;
	m_controls.handbrake = false;
	m_controls.handbrakeLeft = false;
	m_controls.handbrakeRight = false;
	m_controls.bHasBrakePedal = true;
	m_controls.bAnalogSteering = false;

	SetMaxThrottle(1.0);
	SetMaxReverseThrottle(-1.0f);

	InitializePoseParameters();
}


//-----------------------------------------------------------------------------
// Purpose: Initializes the vehicle physics
//			Called by our outer vehicle in it's Spawn()
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::Initialize(const char* pVehicleScript, unsigned int nVehicleType)
{
	// Ok, turn on the simulation now
	// FIXME: Disabling collisions here is necessary because we seem to be
	// getting a one-frame collision between the old + new collision models
	if (m_pOuter->VPhysicsGetObject())
	{
		m_pOuter->VPhysicsGetObject()->EnableCollisions(false);
	}
	m_pOuter->GetEngineObject()->VPhysicsDestroyObject();

	// Create the vphysics model + teleport it into position
	solid_t solid;
	vehicleparams_t vehicle;
	if (!ParseVehicleScript(pVehicleScript, solid, vehicle))
	{
		UTIL_Remove(m_pOuter);
		return false;
	}

	// NOTE: this needs to be greater than your max framerate (so zero is still instant)
	m_throttleRate = 10000.0;
	if (vehicle.engine.throttleTime > 0)
	{
		m_throttleRate = 1.0 / vehicle.engine.throttleTime;
	}

	m_flMaxSpeed = vehicle.engine.maxSpeed;

	IPhysicsObject* pBody = m_pOuter->GetEngineObject()->VPhysicsInitNormal(SOLID_VPHYSICS, 0, false, &solid);
	PhysSetGameFlags(pBody, FVPHYSICS_NO_SELF_COLLISIONS | FVPHYSICS_MULTIOBJECT_ENTITY);
	m_pVehicle = physenv->CreateVehicleController(pBody, vehicle, nVehicleType, physgametrace);
	m_wheelCount = m_pVehicle->GetWheelCount();
	for (int i = 0; i < m_wheelCount; i++)
	{
		m_pWheels[i] = m_pVehicle->GetWheel(i);
	}
	return true;
}


//-----------------------------------------------------------------------------
// Various steering parameters
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetThrottle(float flThrottle)
{
	m_controls.throttle = flThrottle;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetMaxThrottle(float flMaxThrottle)
{
	m_maxThrottle = flMaxThrottle;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetMaxReverseThrottle(float flMaxThrottle)
{
	m_flMaxRevThrottle = flMaxThrottle;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetSteering(float flSteering, float flSteeringRate)
{
	if (!flSteeringRate)
	{
		m_controls.steering = flSteering;
	}
	else
	{
		m_controls.steering = Approach(flSteering, m_controls.steering, flSteeringRate);
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetSteeringDegrees(float flDegrees)
{
	vehicleparams_t& vehicleParams = m_pVehicle->GetVehicleParamsForChange();
	vehicleParams.steering.degreesSlow = flDegrees;
	vehicleParams.steering.degreesFast = flDegrees;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetAction(float flAction)
{
	m_actionSpeed = flAction;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::TurnOn()
{
	if (IsEngineDisabled())
		return;

	if (!m_bIsOn)
	{
		GetOuterServerVehicle()->SoundStart();
		m_bIsOn = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::TurnOff()
{
	ResetControls();

	if (m_bIsOn)
	{
		GetOuterServerVehicle()->SoundShutdown();
		m_bIsOn = false;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetBoost(float flBoost)
{
	if (!IsEngineDisabled())
	{
		m_controls.boost = flBoost;
	}
}

//------------------------------------------------------
// UpdateBooster - Calls UpdateBooster() in the vphysics
// code to allow the timer to be updated
//
// Returns: false if timer has expired (can use again and
//			can stop think
//			true if timer still running
//------------------------------------------------------
bool CEngineVehicleInternal::UpdateBooster(void)
{
	float retval = m_pVehicle->UpdateBooster(gpGlobals->frametime);
	return (retval > 0);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetHasBrakePedal(bool bHasBrakePedal)
{
	m_controls.bHasBrakePedal = bHasBrakePedal;
}

//-----------------------------------------------------------------------------
// Teleport
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::Teleport(matrix3x4_t& relativeTransform)
{
	// We basically just have to make sure the wheels are in the right place
	// after teleportation occurs

	for (int i = 0; i < m_wheelCount; i++)
	{
		matrix3x4_t matrix, newMatrix;
		m_pWheels[i]->GetPositionMatrix(&matrix);
		ConcatTransforms(relativeTransform, matrix, newMatrix);
		m_pWheels[i]->SetPositionMatrix(newMatrix, true);
	}

	// Wake the vehicle back up after a teleport
	//if (GetOuterServerVehicle() && GetOuterServerVehicle()->GetFourWheelVehicle())
	//{
		IPhysicsObject* pObj = m_pOuter->VPhysicsGetObject();
		if (pObj)
		{
			pObj->Wake();
		}
	//}
}

#if 1
// For the #if 0 debug code below!
#define HL2IVP_FACTOR	METERS_PER_INCH
#define IVP2HL(x)		(float)(x * (1.0f/HL2IVP_FACTOR))
#define HL2IVP(x)		(double)(x * HL2IVP_FACTOR)		
#endif

//-----------------------------------------------------------------------------
// Debugging methods
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::DrawDebugGeometryOverlays()
{
	for (int iWheel = 0; iWheel < m_wheelCount; iWheel++)
	{
		IPhysicsObject* pWheel = m_pVehicle->GetWheel(iWheel);
		float radius = pWheel->GetSphereRadius();

		Vector vecPos;
		QAngle vecRot;
		pWheel->GetPosition(&vecPos, &vecRot);
		// draw the physics object position/orientation
		NDebugOverlay::Sphere(vecPos, vecRot, radius, 0, 255, 0, 0, false, 0);
		// draw the animation position/orientation
		NDebugOverlay::Sphere(m_wheelPosition[iWheel], m_wheelRotation[iWheel], radius, 255, 255, 0, 0, false, 0);
	}

	// Render vehicle data.
	IPhysicsObject* pBody = m_pOuter->VPhysicsGetObject();
	if (pBody)
	{
		const vehicleparams_t vehicleParams = m_pVehicle->GetVehicleParams();

		// Draw a red cube as the "center" of the vehicle.
		Vector vecBodyPosition;
		QAngle angBodyDirection;
		pBody->GetPosition(&vecBodyPosition, &angBodyDirection);
		NDebugOverlay::BoxAngles(vecBodyPosition, Vector(-5, -5, -5), Vector(5, 5, 5), angBodyDirection, 255, 0, 0, 0, 0);

		matrix3x4_t matrix;
		AngleMatrix(angBodyDirection, vecBodyPosition, matrix);

		// Draw green cubes at axle centers.
		Vector vecAxlePositions[2], vecAxlePositionsHL[2];
		vecAxlePositions[0] = vehicleParams.axles[0].offset;
		vecAxlePositions[1] = vehicleParams.axles[1].offset;

		VectorTransform(vecAxlePositions[0], matrix, vecAxlePositionsHL[0]);
		VectorTransform(vecAxlePositions[1], matrix, vecAxlePositionsHL[1]);

		NDebugOverlay::BoxAngles(vecAxlePositionsHL[0], Vector(-3, -3, -3), Vector(3, 3, 3), angBodyDirection, 0, 255, 0, 0, 0);
		NDebugOverlay::BoxAngles(vecAxlePositionsHL[1], Vector(-3, -3, -3), Vector(3, 3, 3), angBodyDirection, 0, 255, 0, 0, 0);

		// Draw wheel raycasts in yellow
		vehicle_debugcarsystem_t debugCarSystem;
		m_pVehicle->GetCarSystemDebugData(debugCarSystem);
		for (int iWheel = 0; iWheel < 4; ++iWheel)
		{
			Vector vecStart, vecEnd, vecImpact;

			// Hack for now.
			float tmpY = IVP2HL(debugCarSystem.vecWheelRaycasts[iWheel][0].z);
			vecStart.z = -IVP2HL(debugCarSystem.vecWheelRaycasts[iWheel][0].y);
			vecStart.y = tmpY;
			vecStart.x = IVP2HL(debugCarSystem.vecWheelRaycasts[iWheel][0].x);

			tmpY = IVP2HL(debugCarSystem.vecWheelRaycasts[iWheel][1].z);
			vecEnd.z = -IVP2HL(debugCarSystem.vecWheelRaycasts[iWheel][1].y);
			vecEnd.y = tmpY;
			vecEnd.x = IVP2HL(debugCarSystem.vecWheelRaycasts[iWheel][1].x);

			tmpY = IVP2HL(debugCarSystem.vecWheelRaycastImpacts[iWheel].z);
			vecImpact.z = -IVP2HL(debugCarSystem.vecWheelRaycastImpacts[iWheel].y);
			vecImpact.y = tmpY;
			vecImpact.x = IVP2HL(debugCarSystem.vecWheelRaycastImpacts[iWheel].x);

			NDebugOverlay::BoxAngles(vecStart, Vector(-1, -1, -1), Vector(1, 1, 1), angBodyDirection, 0, 255, 0, 0, 0);
			NDebugOverlay::Line(vecStart, vecEnd, 255, 255, 0, true, 0);
			NDebugOverlay::BoxAngles(vecEnd, Vector(-1, -1, -1), Vector(1, 1, 1), angBodyDirection, 255, 0, 0, 0, 0);

			NDebugOverlay::BoxAngles(vecImpact, Vector(-0.5f, -0.5f, -0.5f), Vector(0.5f, 0.5f, 0.5f), angBodyDirection, 0, 0, 255, 0, 0);
			DebugDrawContactPoints(m_pVehicle->GetWheel(iWheel));
		}
	}
}

int CEngineVehicleInternal::DrawDebugTextOverlays(int nOffset)
{
	const vehicle_operatingparams_t& params = m_pVehicle->GetOperatingParams();
	char tempstr[512];
	Q_snprintf(tempstr, sizeof(tempstr), "Speed %.1f  T/S/B (%.0f/%.0f/%.1f)", params.speed, m_controls.throttle, m_controls.steering, m_controls.brake);
	m_pOuter->EntityText(nOffset, tempstr, 0);
	nOffset++;
	Msg("%s", tempstr);

	Q_snprintf(tempstr, sizeof(tempstr), "Gear: %d, RPM %4d", params.gear, (int)params.engineRPM);
	m_pOuter->EntityText(nOffset, tempstr, 0);
	nOffset++;
	Msg(" %s\n", tempstr);

	return nOffset;
}

//----------------------------------------------------
// Place dust at vector passed in
//----------------------------------------------------
void CEngineVehicleInternal::PlaceWheelDust(int wheelIndex, bool ignoreSpeed)
{
	// New vehicles handle this deeper into the base class
	if (hl2_episodic.GetBool())
		return;

	// Old dust
	Vector	vecPos, vecVel;
	m_pVehicle->GetWheelContactPoint(wheelIndex, &vecPos, NULL);

	vecVel.Random(-1.0f, 1.0f);
	vecVel.z = random->RandomFloat(0.3f, 1.0f);

	VectorNormalize(vecVel);

	// Higher speeds make larger dust clouds
	float flSize;
	if (ignoreSpeed)
	{
		flSize = 1.0f;
	}
	else
	{
		flSize = RemapValClamped(m_nSpeed, DUST_SPEED, m_flMaxSpeed, 0.0f, 1.0f);
	}

	if (flSize)
	{
		CEffectData	data;

		data.m_vOrigin = vecPos;
		data.m_vNormal = vecVel;
		data.m_flScale = flSize;

		DispatchEffect("WheelDust", data);
	}
}

//-----------------------------------------------------------------------------
// Frame-based updating 
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::Think()
{
	if (!m_pVehicle)
		return false;

	// Update sound + physics state
	const vehicle_operatingparams_t& carState = m_pVehicle->GetOperatingParams();
	const vehicleparams_t& vehicleData = m_pVehicle->GetVehicleParams();

	// Set save data.
	float carSpeed = fabs(INS2MPH(carState.speed));
	m_nLastSpeed = m_nSpeed;
	m_nSpeed = (int)carSpeed;
	m_nRPM = (int)carState.engineRPM;
	m_nHasBoost = vehicleData.engine.boostDelay;	// if we have any boost delay, vehicle has boost ability

	m_pVehicle->Update(gpGlobals->frametime, m_controls);

	// boost sounds
	if (IsBoosting() && !m_bLastBoost)
	{
		m_bLastBoost = true;
		m_turboTimer = gpGlobals->curtime + 2.75f;		// min duration for turbo sound
	}
	else if (!IsBoosting() && m_bLastBoost)
	{
		if (gpGlobals->curtime >= m_turboTimer)
		{
			m_bLastBoost = false;
		}
	}

	m_fLastBoost = carState.boostDelay;
	m_nBoostTimeLeft = carState.boostTimeLeft;

	// UNDONE: Use skid info from the physics system?
	// Only check wheels if we're not being carried by a dropship
	if (m_pOuter->VPhysicsGetObject() && !m_pOuter->VPhysicsGetObject()->GetShadowController())
	{
		const float skidFactor = 0.15f;
		const float minSpeed = DEFAULT_SKID_THRESHOLD / skidFactor;
		// we have to slide at least 15% of our speed at higher speeds to make the skid sound (otherwise it can be too frequent)
		float skidThreshold = m_bLastSkid ? DEFAULT_SKID_THRESHOLD : (carState.speed * 0.15f);
		if (skidThreshold < DEFAULT_SKID_THRESHOLD)
		{
			// otherwise, ramp in the skid threshold to avoid the sound at really low speeds unless really skidding
			skidThreshold = RemapValClamped(fabs(carState.speed), 0, minSpeed, DEFAULT_SKID_THRESHOLD * 8, DEFAULT_SKID_THRESHOLD);
		}
		// check for skidding, if we're skidding, need to play the sound
		if (carState.skidSpeed > skidThreshold && m_bIsOn)
		{
			if (!m_bLastSkid)	// only play sound once
			{
				m_bLastSkid = true;
				CPASAttenuationFilter filter(m_pOuter);
				GetOuterServerVehicle()->PlaySound(VS_SKID_FRICTION_NORMAL);
			}

			// kick up dust from the wheels while skidding
			for (int i = 0; i < 4; i++)
			{
				PlaceWheelDust(i, true);
			}
		}
		else if (m_bLastSkid == true)
		{
			m_bLastSkid = false;
			GetOuterServerVehicle()->StopSound(VS_SKID_FRICTION_NORMAL);
		}

		// toss dust up from the wheels of the vehicle if we're moving fast enough
		if (m_nSpeed >= DUST_SPEED && vehicleData.steering.dustCloud && m_bIsOn)
		{
			for (int i = 0; i < 4; i++)
			{
				PlaceWheelDust(i);
			}
		}
	}

	// Make the steering wheel match the input, with a little dampening.
#define STEER_DAMPING	0.8
	float flSteer = GetPoseParameter(m_poseParameters[VEH_STEER]);
	float flPhysicsSteer = carState.steeringAngle / vehicleData.steering.degreesSlow;
	SetPoseParameter(m_poseParameters[VEH_STEER], (STEER_DAMPING * flSteer) + ((1 - STEER_DAMPING) * flPhysicsSteer));

	m_actionValue += m_actionSpeed * m_actionScale * gpGlobals->frametime;
	SetPoseParameter(m_poseParameters[VEH_ACTION], m_actionValue);

	// setup speedometer
	if (m_bIsOn == true)
	{
		float displaySpeed = m_nSpeed / MAX_GUAGE_SPEED;
		SetPoseParameter(m_poseParameters[VEH_SPEEDO], displaySpeed);
	}

	return m_bIsOn;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::VPhysicsUpdate(IPhysicsObject* pPhysics)
{
	// must be a wheel
	if (pPhysics == m_pOuter->VPhysicsGetObject())
		return true;

	// This is here so we can make the pose parameters of the wheels
	// reflect their current physics state
	for (int i = 0; i < m_wheelCount; i++)
	{
		if (pPhysics == m_pWheels[i])
		{
			Vector tmp;
			pPhysics->GetPosition(&m_wheelPosition[i], &m_wheelRotation[i]);

			// transform the wheel into body space
			VectorITransform(m_wheelPosition[i], m_pOuter->GetEngineObject()->EntityToWorldTransform(), tmp);
			SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT + i], (m_wheelBaseHeight[i] - tmp.z) / m_wheelTotalHeight[i]);
			SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_SPIN + i], -m_wheelRotation[i].z);
			return false;
		}
	}

	return false;
}


//-----------------------------------------------------------------------------
// Shared code to compute the vehicle view position
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::GetVehicleViewPosition(const char* pViewAttachment, float flPitchFactor, Vector* pAbsOrigin, QAngle* pAbsAngles)
{
	matrix3x4_t vehicleEyePosToWorld;
	Vector vehicleEyeOrigin;
	QAngle vehicleEyeAngles;
	GetAttachment(pViewAttachment, vehicleEyeOrigin, vehicleEyeAngles);
	AngleMatrix(vehicleEyeAngles, vehicleEyePosToWorld);

#ifdef HL2_DLL
	// View dampening.
	if (r_VehicleViewDampen.GetInt())
	{
		GetOuterServerVehicle()->DampenEyePosition(vehicleEyeOrigin, vehicleEyeAngles);
	}
#endif

	// Compute the relative rotation between the unperterbed eye attachment + the eye angles
	matrix3x4_t cameraToWorld;
	AngleMatrix(*pAbsAngles, cameraToWorld);

	matrix3x4_t worldToEyePos;
	MatrixInvert(vehicleEyePosToWorld, worldToEyePos);

	matrix3x4_t vehicleCameraToEyePos;
	ConcatTransforms(worldToEyePos, cameraToWorld, vehicleCameraToEyePos);

	// Now perterb the attachment point
	vehicleEyeAngles.x = RemapAngleRange(PITCH_CURVE_ZERO * flPitchFactor, PITCH_CURVE_LINEAR, vehicleEyeAngles.x);
	vehicleEyeAngles.z = RemapAngleRange(ROLL_CURVE_ZERO * flPitchFactor, ROLL_CURVE_LINEAR, vehicleEyeAngles.z);
	AngleMatrix(vehicleEyeAngles, vehicleEyeOrigin, vehicleEyePosToWorld);

	// Now treat the relative eye angles as being relative to this new, perterbed view position...
	matrix3x4_t newCameraToWorld;
	ConcatTransforms(vehicleEyePosToWorld, vehicleCameraToEyePos, newCameraToWorld);

	// output new view abs angles
	MatrixAngles(newCameraToWorld, *pAbsAngles);

	// UNDONE: *pOrigin would already be correct in single player if the HandleView() on the server ran after vphysics
	MatrixGetColumn(newCameraToWorld, 3, *pAbsOrigin);
}


//-----------------------------------------------------------------------------
// Control initialization
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::ResetControls()
{
	m_controls.handbrake = true;
	m_controls.handbrakeLeft = false;
	m_controls.handbrakeRight = false;
	m_controls.boost = 0;
	m_controls.brake = 0.0f;
	m_controls.throttle = 0;
	m_controls.steering = 0;
}

void CEngineVehicleInternal::ReleaseHandbrake()
{
	m_controls.handbrake = false;
}

void CEngineVehicleInternal::SetHandbrake(bool bBrake)
{
	m_controls.handbrake = bBrake;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::EnableMotion(void)
{
	for (int iWheel = 0; iWheel < m_wheelCount; ++iWheel)
	{
		m_pWheels[iWheel]->EnableMotion(true);
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::DisableMotion(void)
{
	Vector vecZero(0.0f, 0.0f, 0.0f);
	AngularImpulse angNone(0.0f, 0.0f, 0.0f);

	for (int iWheel = 0; iWheel < m_wheelCount; ++iWheel)
	{
		m_pWheels[iWheel]->SetVelocity(&vecZero, &angNone);
		m_pWheels[iWheel]->EnableMotion(false);
	}
}

float CEngineVehicleInternal::GetHLSpeed() const
{
	const vehicle_operatingparams_t& carState = m_pVehicle->GetOperatingParams();
	return carState.speed;
}

float CEngineVehicleInternal::GetSteering() const
{
	return m_controls.steering;
}

float CEngineVehicleInternal::GetSteeringDegrees() const
{
	const vehicleparams_t vehicleParams = m_pVehicle->GetVehicleParams();
	return vehicleParams.steering.degreesSlow;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SteeringRest(float carSpeed, const vehicleparams_t& vehicleData)
{
	float flSteeringRate = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
		vehicleData.steering.steeringRestRateSlow, vehicleData.steering.steeringRestRateFast);
	m_controls.steering = Approach(0, m_controls.steering, flSteeringRate * gpGlobals->frametime);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SteeringTurn(float carSpeed, const vehicleparams_t& vehicleData, bool bTurnLeft, bool bBrake, bool bThrottle)
{
	float flTargetSteering = bTurnLeft ? -1.0f : 1.0f;
	// steering speeds are stored in MPH
	float flSteeringRestRate = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
		vehicleData.steering.steeringRestRateSlow, vehicleData.steering.steeringRestRateFast);

	float carSpeedIns = MPH2INS(carSpeed);
	// engine speeds are stored in in/s
	if (carSpeedIns > vehicleData.engine.maxSpeed)
	{
		flSteeringRestRate = RemapValClamped(carSpeedIns, vehicleData.engine.maxSpeed, vehicleData.engine.boostMaxSpeed, vehicleData.steering.steeringRestRateFast, vehicleData.steering.steeringRestRateFast * 0.5f);
	}

	const vehicle_operatingparams_t& carState = m_pVehicle->GetOperatingParams();
	bool bIsBoosting = carState.isTorqueBoosting;

	// if you're recovering from a boost and still going faster than max, use the boost steering values
	bool bIsBoostRecover = (carState.boostTimeLeft == 100 || carState.boostTimeLeft == 0) ? false : true;
	float boostMinSpeed = vehicleData.engine.maxSpeed * vehicleData.engine.autobrakeSpeedGain;
	if (!bIsBoosting && bIsBoostRecover && carSpeedIns > boostMinSpeed)
	{
		bIsBoosting = true;
	}

	if (bIsBoosting)
	{
		flSteeringRestRate *= vehicleData.steering.boostSteeringRestRateFactor;
	}
	else if (bThrottle)
	{
		flSteeringRestRate *= vehicleData.steering.throttleSteeringRestRateFactor;
	}

	float flSteeringRate = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
		vehicleData.steering.steeringRateSlow, vehicleData.steering.steeringRateFast);

	if (fabs(flSteeringRate) < flSteeringRestRate)
	{
		if (Sign(flTargetSteering) != Sign(m_controls.steering))
		{
			flSteeringRate = flSteeringRestRate;
		}
	}
	if (bIsBoosting)
	{
		flSteeringRate *= vehicleData.steering.boostSteeringRateFactor;
	}
	else if (bBrake)
	{
		flSteeringRate *= vehicleData.steering.brakeSteeringRateFactor;
	}
	flSteeringRate *= gpGlobals->frametime;
	m_controls.steering = Approach(flTargetSteering, m_controls.steering, flSteeringRate);
	m_controls.bAnalogSteering = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SteeringTurnAnalog(float carSpeed, const vehicleparams_t& vehicleData, float sidemove)
{

	// OLD Code
#if 0
	float flSteeringRate = STEERING_BASE_RATE;

	float factor = clamp(fabs(sidemove) / STICK_EXTENTS, 0.0f, 1.0f);

	factor *= 30;
	flSteeringRate *= log(factor);
	flSteeringRate *= gpGlobals->frametime;

	SetSteering(sidemove < 0.0f ? -1 : 1, flSteeringRate);
#else
	// This is tested with gamepads with analog sticks.  It gives full analog control allowing the player to hold shallow turns.
	float steering = (sidemove / STICK_EXTENTS);

	float flSign = (steering > 0) ? 1.0f : -1.0f;
	float flSteerAdj = RemapValClamped(fabs(steering), xbox_steering_deadzone.GetFloat(), 1.0f, 0.0f, 1.0f);

	float flSteeringRate = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
		vehicleData.steering.steeringRateSlow, vehicleData.steering.steeringRateFast);
	flSteeringRate *= vehicleData.steering.throttleSteeringRestRateFactor;

	m_controls.bAnalogSteering = true;
	SetSteering(flSign * flSteerAdj, flSteeringRate * gpGlobals->frametime);
#endif
}

//-----------------------------------------------------------------------------
// Methods related to actually driving the vehicle
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::UpdateDriverControls(CUserCmd* cmd, float flFrameTime)
{
	const float SPEED_THROTTLE_AS_BRAKE = 2.0f;
	int nButtons = cmd->buttons;

	// Get vehicle data.
	const vehicle_operatingparams_t& carState = m_pVehicle->GetOperatingParams();
	const vehicleparams_t& vehicleData = m_pVehicle->GetVehicleParams();

	// Get current speed in miles/hour.
	float flCarSign = 0.0f;
	if (carState.speed >= SPEED_THROTTLE_AS_BRAKE)
	{
		flCarSign = 1.0f;
	}
	else if (carState.speed <= -SPEED_THROTTLE_AS_BRAKE)
	{
		flCarSign = -1.0f;
	}
	float carSpeed = fabs(INS2MPH(carState.speed));

	// If going forward and turning hard, keep the throttle applied.
	if (xbox_autothrottle.GetBool() && cmd->forwardmove > 0.0f)
	{
		if (carSpeed > GetMaxSpeed() * 0.75)
		{
			if (fabs(cmd->sidemove) > cmd->forwardmove)
			{
				cmd->forwardmove = STICK_EXTENTS;
			}
		}
	}

	//Msg("F: %4.1f \tS: %4.1f!\tSTEER: %3.1f\n", cmd->forwardmove, cmd->sidemove, carState.steeringAngle);
	// If changing direction, use default "return to zero" speed to more quickly transition.
	if ((nButtons & IN_MOVELEFT) || (nButtons & IN_MOVERIGHT))
	{
		bool bTurnLeft = ((nButtons & IN_MOVELEFT) != 0);
		bool bBrake = ((nButtons & IN_BACK) != 0);
		bool bThrottleDown = ((nButtons & IN_FORWARD) != 0) && !bBrake;
		SteeringTurn(carSpeed, vehicleData, bTurnLeft, bBrake, bThrottleDown);
	}
	else if (cmd->sidemove != 0.0f)
	{
		SteeringTurnAnalog(carSpeed, vehicleData, cmd->sidemove);
	}
	else
	{
		SteeringRest(carSpeed, vehicleData);
	}

	// Set vehicle control inputs.
	m_controls.boost = 0;
	m_controls.handbrake = false;
	m_controls.handbrakeLeft = false;
	m_controls.handbrakeRight = false;
	m_controls.brakepedal = false;
	bool bThrottle;

	//-------------------------------------------------------------------------
	// Analog throttle biasing - This code gives the player a bit of control stick
	// 'slop' in the opposite direction that they are driving. If a player is 
	// driving forward and makes a hard turn in which the stick actually goes
	// below neutral (toward reverse), this code continues to propel the car 
	// forward unless the player makes a significant motion towards reverse.
	// (The inverse is true when driving in reverse and the stick is moved slightly forward)
	//-------------------------------------------------------------------------
	IDrivableVehicle* pDrivableVehicle = dynamic_cast<IDrivableVehicle*>(GetOuterServerVehicle());
	CBaseEntity* pDriver = pDrivableVehicle ? pDrivableVehicle->GetDriver() : NULL;
	CBasePlayer* pPlayerDriver;
	float flBiasThreshold = xbox_throttlebias.GetFloat();

	if (pDriver && pDriver->IsPlayer())
	{
		pPlayerDriver = dynamic_cast<CBasePlayer*>(pDriver);

		if (cmd->forwardmove == 0.0f && (fabs(cmd->sidemove) < 200.0f))
		{
			// If the stick goes neutral, clear out the bias. When the bias is neutral, it will begin biasing
			// in whichever direction the user next presses the analog stick.
			pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_NONE);
		}
		else if (cmd->forwardmove > 0.0f)
		{
			if (pPlayerDriver->GetVehicleAnalogControlBias() == VEHICLE_ANALOG_BIAS_REVERSE)
			{
				// Player is pushing forward, but the controller is currently biased for reverse driving.
				// Must pass a threshold to be accepted as forward input. Otherwise we just spoof a reduced reverse input 
				// to keep the car moving in the direction the player probably expects.
				if (cmd->forwardmove < flBiasThreshold)
				{
					cmd->forwardmove = -xbox_throttlespoof.GetFloat();
				}
				else
				{
					// Passed the threshold. Allow the direction change to occur.
					pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_FORWARD);
				}
			}
			else if (pPlayerDriver->GetVehicleAnalogControlBias() == VEHICLE_ANALOG_BIAS_NONE)
			{
				pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_FORWARD);
			}
		}
		else if (cmd->forwardmove < 0.0f)
		{
			if (pPlayerDriver->GetVehicleAnalogControlBias() == VEHICLE_ANALOG_BIAS_FORWARD)
			{
				// Inverse of above logic
				if (cmd->forwardmove > -flBiasThreshold)
				{
					cmd->forwardmove = xbox_throttlespoof.GetFloat();
				}
				else
				{
					pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_REVERSE);
				}
			}
			else if (pPlayerDriver->GetVehicleAnalogControlBias() == VEHICLE_ANALOG_BIAS_NONE)
			{
				pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_REVERSE);
			}
		}
	}

	//=========================
	// analog control
	//=========================
	if (cmd->forwardmove > 0.0f)
	{
		float flAnalogThrottle = cmd->forwardmove / STICK_EXTENTS;

		flAnalogThrottle = clamp(flAnalogThrottle, 0.25f, 1.0f);

		bThrottle = true;
		if (m_controls.throttle < 0)
		{
			m_controls.throttle = 0;
		}

		float flMaxThrottle = MAX(0.1, m_maxThrottle);
		if (m_controls.steering != 0)
		{
			float flThrottleReduce = 0;

			// ramp this in, don't just start at the slow speed reduction (helps accelerate from a stop)
			if (carSpeed < vehicleData.steering.speedSlow)
			{
				flThrottleReduce = RemapValClamped(carSpeed, 0, vehicleData.steering.speedSlow,
					0, vehicleData.steering.turnThrottleReduceSlow);
			}
			else
			{
				flThrottleReduce = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
					vehicleData.steering.turnThrottleReduceSlow, vehicleData.steering.turnThrottleReduceFast);
			}

			float limit = 1.0f - (flThrottleReduce * fabs(m_controls.steering));
			if (limit < 0)
				limit = 0;
			flMaxThrottle = MIN(flMaxThrottle, limit);
		}

		m_controls.throttle = Approach(flMaxThrottle * flAnalogThrottle, m_controls.throttle, flFrameTime * m_throttleRate);

		// Apply the brake.
		if ((flCarSign < 0.0f) && m_controls.bHasBrakePedal)
		{
			m_controls.brake = Approach(BRAKE_MAX_VALUE, m_controls.brake, flFrameTime * r_vehicleBrakeRate.GetFloat() * BRAKE_BACK_FORWARD_SCALAR);
			m_controls.brakepedal = true;
			m_controls.throttle = 0.0f;
			bThrottle = false;
		}
		else
		{
			m_controls.brake = 0.0f;
		}
	}
	else if (cmd->forwardmove < 0.0f)
	{
		float flAnalogBrake = fabs(cmd->forwardmove / STICK_EXTENTS);

		flAnalogBrake = clamp(flAnalogBrake, 0.25f, 1.0f);

		bThrottle = true;
		if (m_controls.throttle > 0)
		{
			m_controls.throttle = 0;
		}

		float flMaxThrottle = MIN(-0.1, m_flMaxRevThrottle);
		m_controls.throttle = Approach(flMaxThrottle * flAnalogBrake, m_controls.throttle, flFrameTime * m_throttleRate);

		// Apply the brake.
		if ((flCarSign > 0.0f) && m_controls.bHasBrakePedal)
		{
			m_controls.brake = Approach(BRAKE_MAX_VALUE, m_controls.brake, flFrameTime * r_vehicleBrakeRate.GetFloat());
			m_controls.brakepedal = true;
			m_controls.throttle = 0.0f;
			bThrottle = false;
		}
		else
		{
			m_controls.brake = 0.0f;
		}
	}
	// digital control
	else if (nButtons & IN_FORWARD)
	{
		bThrottle = true;
		if (m_controls.throttle < 0)
		{
			m_controls.throttle = 0;
		}

		float flMaxThrottle = MAX(0.1, m_maxThrottle);

		if (m_controls.steering != 0)
		{
			float flThrottleReduce = 0;

			// ramp this in, don't just start at the slow speed reduction (helps accelerate from a stop)
			if (carSpeed < vehicleData.steering.speedSlow)
			{
				flThrottleReduce = RemapValClamped(carSpeed, 0, vehicleData.steering.speedSlow,
					0, vehicleData.steering.turnThrottleReduceSlow);
			}
			else
			{
				flThrottleReduce = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
					vehicleData.steering.turnThrottleReduceSlow, vehicleData.steering.turnThrottleReduceFast);
			}

			float limit = 1.0f - (flThrottleReduce * fabs(m_controls.steering));
			if (limit < 0)
				limit = 0;
			flMaxThrottle = MIN(flMaxThrottle, limit);
		}

		m_controls.throttle = Approach(flMaxThrottle, m_controls.throttle, flFrameTime * m_throttleRate);

		// Apply the brake.
		if ((flCarSign < 0.0f) && m_controls.bHasBrakePedal)
		{
			m_controls.brake = Approach(BRAKE_MAX_VALUE, m_controls.brake, flFrameTime * r_vehicleBrakeRate.GetFloat() * BRAKE_BACK_FORWARD_SCALAR);
			m_controls.brakepedal = true;
			m_controls.throttle = 0.0f;
			bThrottle = false;
		}
		else
		{
			m_controls.brake = 0.0f;
		}
	}
	else if (nButtons & IN_BACK)
	{
		bThrottle = true;
		if (m_controls.throttle > 0)
		{
			m_controls.throttle = 0;
		}

		float flMaxThrottle = MIN(-0.1, m_flMaxRevThrottle);
		m_controls.throttle = Approach(flMaxThrottle, m_controls.throttle, flFrameTime * m_throttleRate);

		// Apply the brake.
		if ((flCarSign > 0.0f) && m_controls.bHasBrakePedal)
		{
			m_controls.brake = Approach(BRAKE_MAX_VALUE, m_controls.brake, flFrameTime * r_vehicleBrakeRate.GetFloat());
			m_controls.brakepedal = true;
			m_controls.throttle = 0.0f;
			bThrottle = false;
		}
		else
		{
			m_controls.brake = 0.0f;
		}
	}
	else
	{
		bThrottle = false;
		m_controls.throttle = 0;
		m_controls.brake = 0.0f;
	}

	if ((nButtons & IN_SPEED) && !IsEngineDisabled() && bThrottle)
	{
		m_controls.boost = 1.0f;
	}

	// Using has brakepedal for handbrake as well.
	if ((nButtons & IN_JUMP) && m_controls.bHasBrakePedal)
	{
		m_controls.handbrake = true;

		if (cmd->sidemove < -100)
		{
			m_controls.handbrakeLeft = true;
		}
		else if (cmd->sidemove > 100)
		{
			m_controls.handbrakeRight = true;
		}

		// Prevent playing of the engine revup when we're braking
		bThrottle = false;
	}

	if (IsEngineDisabled())
	{
		m_controls.throttle = 0.0f;
		m_controls.handbrake = true;
		bThrottle = false;
	}

	// throttle sounds
	// If we dropped a bunch of speed, restart the throttle
	if (bThrottle && (m_nLastSpeed > m_nSpeed && (m_nLastSpeed - m_nSpeed > 10)))
	{
		m_bLastThrottle = false;
	}

	// throttle down now but not before??? (or we're braking)
	if (!m_controls.handbrake && !m_controls.brakepedal && bThrottle && !m_bLastThrottle)
	{
		m_throttleStartTime = gpGlobals->curtime;		// need to track how long throttle is down
		m_bLastThrottle = true;
	}
	// throttle up now but not before??
	else if (!bThrottle && m_bLastThrottle && IsEngineDisabled() == false)
	{
		m_throttleActiveTime = gpGlobals->curtime - m_throttleStartTime;
		m_bLastThrottle = false;
	}

	float flSpeedPercentage = clamp(m_nSpeed / m_flMaxSpeed, 0.f, 1.f);
	vbs_sound_update_t params;
	params.Defaults();
	params.bReverse = (m_controls.throttle < 0);
	params.bThrottleDown = bThrottle;
	params.bTurbo = IsBoosting();
	params.bVehicleInWater = GetOuterServerVehicle()->IsVehicleBodyInWater();
	params.flCurrentSpeedFraction = flSpeedPercentage;
	params.flFrameTime = flFrameTime;
	params.flWorldSpaceSpeed = carState.speed;
	GetOuterServerVehicle()->SoundUpdate(params);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::IsBoosting(void)
{
	const vehicleparams_t* pVehicleParams = &m_pVehicle->GetVehicleParams();
	const vehicle_operatingparams_t* pVehicleOperating = &m_pVehicle->GetOperatingParams();
	if (pVehicleParams && pVehicleOperating)
	{
		if ((pVehicleOperating->boostDelay - pVehicleParams->engine.boostDelay) > 0.0f)
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetDisableEngine(bool bDisable)
{
	// Set the engine state.
	m_pVehicle->SetEngineDisabled(bDisable);
}

static int AddPhysToList(IPhysicsObject** pList, int listMax, int count, IPhysicsObject* pPhys)
{
	if (pPhys)
	{
		if (count < listMax)
		{
			pList[count] = pPhys;
			count++;
		}
	}
	return count;
}

int CEngineVehicleInternal::VPhysicsGetObjectList(IPhysicsObject** pList, int listMax)
{
	int count = 0;
	// add the body
	count = AddPhysToList(pList, listMax, count, m_pOuter->VPhysicsGetObject());
	for (int i = 0; i < 4; i++)
	{
		count = AddPhysToList(pList, listMax, count, m_pWheels[i]);
	}
	return count;
}


struct collidelist_t
{
	const CPhysCollide* pCollide;
	Vector			origin;
	QAngle			angles;
};

// NOTE: This routine is relatively slow.  If you need to use it for per-frame work, consider that fact.
// UNDONE: Expand this to the full matrix of solid types on each side and move into enginetrace
bool TestEntityTriggerIntersection_Accurate(IEngineObjectServer* pTrigger, IEngineObjectServer* pEntity)
{
	Assert(pTrigger->GetSolid() == SOLID_BSP);

	if (pTrigger->Intersects(pEntity))	// It touches one, it's in the volume
	{
		switch (pEntity->GetSolid())
		{
		case SOLID_BBOX:
		{
			ICollideable* pCollide = pTrigger->GetCollideable();
			Ray_t ray;
			trace_t tr;
			ray.Init(pEntity->GetAbsOrigin(), pEntity->GetAbsOrigin(), pEntity->WorldAlignMins(), pEntity->WorldAlignMaxs());
			enginetrace->ClipRayToCollideable(ray, MASK_ALL, pCollide, &tr);

			if (tr.startsolid)
				return true;
		}
		break;
		case SOLID_BSP:
		case SOLID_VPHYSICS:
		{
			CPhysCollide* pTriggerCollide = modelinfo->GetVCollide(pTrigger->GetModelIndex())->solids[0];
			Assert(pTriggerCollide);

			CUtlVector<collidelist_t> collideList;
			IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int physicsCount = pEntity->GetOuter()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
			if (physicsCount)
			{
				for (int i = 0; i < physicsCount; i++)
				{
					const CPhysCollide* pCollide = pList[i]->GetCollide();
					if (pCollide)
					{
						collidelist_t element;
						element.pCollide = pCollide;
						pList[i]->GetPosition(&element.origin, &element.angles);
						collideList.AddToTail(element);
					}
				}
			}
			else
			{
				vcollide_t* pVCollide = modelinfo->GetVCollide(pEntity->GetModelIndex());
				if (pVCollide && pVCollide->solidCount)
				{
					collidelist_t element;
					element.pCollide = pVCollide->solids[0];
					element.origin = pEntity->GetAbsOrigin();
					element.angles = pEntity->GetAbsAngles();
					collideList.AddToTail(element);
				}
			}
			for (int i = collideList.Count() - 1; i >= 0; --i)
			{
				const collidelist_t& element = collideList[i];
				trace_t tr;
				physcollision->TraceCollide(element.origin, element.origin, element.pCollide, element.angles, pTriggerCollide, pTrigger->GetAbsOrigin(), pTrigger->GetAbsAngles(), &tr);
				if (tr.startsolid)
					return true;
			}
		}
		break;

		default:
			return true;
		}
	}
	return false;
}

// Called by CEntityListSystem
void CAimTargetManager::LevelInitPreEntity()
{
	gEntList.AddListenerEntity(this);
	Clear();
}
void CAimTargetManager::LevelShutdownPostEntity()
{
	gEntList.RemoveListenerEntity(this);
	Clear();
}

void CAimTargetManager::Clear()
{
	m_targetList.Purge();
}

void CAimTargetManager::ForceRepopulateList()
{
	Clear();

	CBaseEntity* pEnt = gEntList.FirstEnt();

	while (pEnt)
	{
		if (ShouldAddEntity(pEnt))
			AddEntity(pEnt);

		pEnt = gEntList.NextEnt(pEnt);
	}
}

bool CAimTargetManager::ShouldAddEntity(CBaseEntity* pEntity)
{
	return ((pEntity->GetEngineObject()->GetFlags() & FL_AIMTARGET) != 0);
}

// IEntityListener
void CAimTargetManager::OnEntityCreated(CBaseEntity* pEntity) {}
void CAimTargetManager::OnEntityDeleted(CBaseEntity* pEntity)
{
	if (!(pEntity->GetEngineObject()->GetFlags() & FL_AIMTARGET))
		return;
	RemoveEntity(pEntity);
}
void CAimTargetManager::AddEntity(CBaseEntity* pEntity)
{
	if (pEntity->GetEngineObject()->IsMarkedForDeletion())
		return;
	m_targetList.AddToTail(pEntity);
}
void CAimTargetManager::RemoveEntity(CBaseEntity* pEntity)
{
	int index = m_targetList.Find(pEntity);
	if (m_targetList.IsValidIndex(index))
	{
		m_targetList.FastRemove(index);
	}
}
int CAimTargetManager::ListCount() { return m_targetList.Count(); }
int CAimTargetManager::ListCopy(CBaseEntity* pList[], int listMax)
{
	int count = MIN(listMax, ListCount());
	memcpy(pList, m_targetList.Base(), sizeof(CBaseEntity*) * count);
	return count;
}

CAimTargetManager g_AimManager;

int AimTarget_ListCount()
{
	return g_AimManager.ListCount();
}
int AimTarget_ListCopy(CBaseEntity* pList[], int listMax)
{
	return g_AimManager.ListCopy(pList, listMax);
}
void AimTarget_ForceRepopulateList()
{
	g_AimManager.ForceRepopulateList();
}


// Manages a list of all entities currently doing game simulation or thinking
// NOTE: This is usually a small subset of the global entity list, so it's
// an optimization to maintain this list incrementally rather than polling each
// frame.
struct simthinkentry_t
{
	unsigned short	entEntry;
	unsigned short	unused0;
	int				nextThinkTick;
};
class CSimThinkManager : public IEntityListener<CBaseEntity>
{
public:
	CSimThinkManager()
	{
		Clear();
	}
	void Clear()
	{
		m_simThinkList.Purge();
		for (int i = 0; i < ARRAYSIZE(m_entinfoIndex); i++)
		{
			m_entinfoIndex[i] = 0xFFFF;
		}
	}
	void LevelInitPreEntity()
	{
		gEntList.AddListenerEntity(this);
	}

	void LevelShutdownPostEntity()
	{
		gEntList.RemoveListenerEntity(this);
		Clear();
	}

	void OnEntityCreated(CBaseEntity* pEntity)
	{
		Assert(m_entinfoIndex[pEntity->GetRefEHandle().GetEntryIndex()] == 0xFFFF);
	}
	void OnEntityDeleted(CBaseEntity* pEntity)
	{
		RemoveEntinfoIndex(pEntity->GetRefEHandle().GetEntryIndex());
	}

	void RemoveEntinfoIndex(int index)
	{
		int listHandle = m_entinfoIndex[index];
		// If this guy is in the active list, remove him
		if (listHandle != 0xFFFF)
		{
			Assert(m_simThinkList[listHandle].entEntry == index);
			m_simThinkList.FastRemove(listHandle);
			m_entinfoIndex[index] = 0xFFFF;

			// fast remove shifted someone, update that someone
			if (listHandle < m_simThinkList.Count())
			{
				m_entinfoIndex[m_simThinkList[listHandle].entEntry] = listHandle;
			}
		}
	}
	int ListCount()
	{
		return m_simThinkList.Count();
	}

	int ListCopy(CBaseEntity* pList[], int listMax)
	{
		int count = MIN(listMax, ListCount());
		int out = 0;
		for (int i = 0; i < count; i++)
		{
			// only copy out entities that will simulate or think this frame
			if (m_simThinkList[i].nextThinkTick <= gpGlobals->tickcount)
			{
				Assert(m_simThinkList[i].nextThinkTick >= 0);
				int entinfoIndex = m_simThinkList[i].entEntry;
				const CEntInfo<CBaseEntity>* pInfo = gEntList.GetEntInfoPtrByIndex(entinfoIndex);
				pList[out] = (CBaseEntity*)pInfo->m_pEntity;
				Assert(m_simThinkList[i].nextThinkTick == 0 || pList[out]->GetEngineObject()->GetFirstThinkTick() == m_simThinkList[i].nextThinkTick);
				Assert(gEntList.IsEntityPtr(pList[out]));
				out++;
			}
		}

		return out;
	}

	void EntityChanged(CBaseEntity* pEntity)
	{
		// might change after deletion, don't put back into the list
		if (pEntity->GetEngineObject()->IsMarkedForDeletion())
			return;

		const CBaseHandle& eh = pEntity->GetRefEHandle();
		if (!eh.IsValid())
			return;

		int index = eh.GetEntryIndex();
		if (pEntity->GetEngineObject()->IsEFlagSet(EFL_NO_THINK_FUNCTION) && pEntity->GetEngineObject()->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION))
		{
			RemoveEntinfoIndex(index);
		}
		else
		{
			// already in the list? (had think or sim last time, now has both - or had both last time, now just one)
			if (m_entinfoIndex[index] == 0xFFFF)
			{
				MEM_ALLOC_CREDIT();
				m_entinfoIndex[index] = m_simThinkList.AddToTail();
				m_simThinkList[m_entinfoIndex[index]].entEntry = (unsigned short)index;
				m_simThinkList[m_entinfoIndex[index]].nextThinkTick = 0;
				if (pEntity->GetEngineObject()->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION))
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = pEntity->GetEngineObject()->GetFirstThinkTick();
					Assert(m_simThinkList[m_entinfoIndex[index]].nextThinkTick >= 0);
				}
			}
			else
			{
				// updating existing entry - if no sim, reset think time
				if (pEntity->GetEngineObject()->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION))
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = pEntity->GetEngineObject()->GetFirstThinkTick();
					Assert(m_simThinkList[m_entinfoIndex[index]].nextThinkTick >= 0);
				}
				else
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = 0;
				}
			}
		}
	}

private:
	unsigned short m_entinfoIndex[NUM_ENT_ENTRIES];
	CUtlVector<simthinkentry_t>	m_simThinkList;
};

CSimThinkManager g_SimThinkManager;

int SimThink_ListCount()
{
	return g_SimThinkManager.ListCount();
}

int SimThink_ListCopy(CBaseEntity* pList[], int listMax)
{
	return g_SimThinkManager.ListCopy(pList, listMax);
}

void SimThink_EntityChanged(CBaseEntity* pEntity)
{
	g_SimThinkManager.EntityChanged(pEntity);
}

static CBaseEntityClassList* s_pClassLists = NULL;
CBaseEntityClassList::CBaseEntityClassList()
{
	m_pNextClassList = s_pClassLists;
	s_pClassLists = this;
}
CBaseEntityClassList::~CBaseEntityClassList()
{
}


// removes the entity from the global list
// only called from with the CBaseEntity destructor
bool g_fInCleanupDelete;


//-----------------------------------------------------------------------------
// NOTIFY LIST
// 
// Allows entities to get events fired when another entity changes
//-----------------------------------------------------------------------------
struct entitynotify_t
{
	CBaseEntity* pNotify;
	CBaseEntity* pWatched;
};
class CNotifyList : public INotify, public IEntityListener<CBaseEntity>
{
public:
	// INotify
	void AddEntity(CBaseEntity* pNotify, CBaseEntity* pWatched);
	void RemoveEntity(CBaseEntity* pNotify, CBaseEntity* pWatched);
	void ReportNamedEvent(CBaseEntity* pEntity, const char* pEventName);
	void ClearEntity(CBaseEntity* pNotify);
	void ReportSystemEvent(CBaseEntity* pEntity, notify_system_event_t eventType, const notify_system_event_params_t& params);

	// IEntityListener
	virtual void OnEntityCreated(CBaseEntity* pEntity);
	virtual void OnEntityDeleted(CBaseEntity* pEntity);

	// Called from CEntityListSystem
	void LevelInitPreEntity();
	void LevelShutdownPreEntity();

private:
	CUtlVector<entitynotify_t>	m_notifyList;
};

void CNotifyList::AddEntity(CBaseEntity* pNotify, CBaseEntity* pWatched)
{
	// OPTIMIZE: Also flag pNotify for faster "RemoveAllNotify" ?
	pWatched->GetEngineObject()->AddEFlags(EFL_NOTIFY);
	int index = m_notifyList.AddToTail();
	entitynotify_t& notify = m_notifyList[index];
	notify.pNotify = pNotify;
	notify.pWatched = pWatched;
}

// Remove noitfication for an entity
void CNotifyList::RemoveEntity(CBaseEntity* pNotify, CBaseEntity* pWatched)
{
	for (int i = m_notifyList.Count(); --i >= 0; )
	{
		if (m_notifyList[i].pNotify == pNotify && m_notifyList[i].pWatched == pWatched)
		{
			m_notifyList.FastRemove(i);
		}
	}
}


void CNotifyList::ReportNamedEvent(CBaseEntity* pEntity, const char* pInputName)
{
	variant_t emptyVariant;

	if (!pEntity->GetEngineObject()->IsEFlagSet(EFL_NOTIFY))
		return;

	for (int i = 0; i < m_notifyList.Count(); i++)
	{
		if (m_notifyList[i].pWatched == pEntity)
		{
			m_notifyList[i].pNotify->AcceptInput(pInputName, pEntity, pEntity, emptyVariant, 0);
		}
	}
}

void CNotifyList::LevelInitPreEntity()
{
	gEntList.AddListenerEntity(this);
}

void CNotifyList::LevelShutdownPreEntity(void)
{
	gEntList.RemoveListenerEntity(this);
	m_notifyList.Purge();
}

void CNotifyList::OnEntityCreated(CBaseEntity* pEntity)
{
}

void CNotifyList::OnEntityDeleted(CBaseEntity* pEntity)
{
	ReportDestroyEvent(pEntity);
	ClearEntity(pEntity);
}


// UNDONE: Slow linear search?
void CNotifyList::ClearEntity(CBaseEntity* pNotify)
{
	for (int i = m_notifyList.Count(); --i >= 0; )
	{
		if (m_notifyList[i].pNotify == pNotify || m_notifyList[i].pWatched == pNotify)
		{
			m_notifyList.FastRemove(i);
		}
	}
}

void CNotifyList::ReportSystemEvent(CBaseEntity* pEntity, notify_system_event_t eventType, const notify_system_event_params_t& params)
{
	if (!pEntity->GetEngineObject()->IsEFlagSet(EFL_NOTIFY))
		return;

	for (int i = 0; i < m_notifyList.Count(); i++)
	{
		if (m_notifyList[i].pWatched == pEntity)
		{
			m_notifyList[i].pNotify->NotifySystemEvent(pEntity, eventType, params);
		}
	}
}

static CNotifyList g_NotifyList;
INotify* g_pNotify = &g_NotifyList;

class CEntityTouchManager : public IEntityListener<CBaseEntity>
{
public:
	// called by CEntityListSystem
	void LevelInitPreEntity()
	{
		gEntList.AddListenerEntity(this);
		Clear();
	}
	void LevelShutdownPostEntity()
	{
		gEntList.RemoveListenerEntity(this);
		Clear();
	}
	void FrameUpdatePostEntityThink();

	void Clear()
	{
		m_updateList.Purge();
	}

	// IEntityListener
	virtual void OnEntityCreated(CBaseEntity* pEntity) {}
	virtual void OnEntityDeleted(CBaseEntity* pEntity)
	{
		if (!pEntity->GetEngineObject()->GetCheckUntouch())
			return;
		int index = m_updateList.Find(pEntity);
		if (m_updateList.IsValidIndex(index))
		{
			m_updateList.FastRemove(index);
		}
	}
	void AddEntity(CBaseEntity* pEntity)
	{
		if (pEntity->GetEngineObject()->IsMarkedForDeletion())
			return;
		m_updateList.AddToTail(pEntity);
	}

private:
	CUtlVector<CBaseEntity*>	m_updateList;
};

static CEntityTouchManager g_TouchManager;

void EntityTouch_Add(CBaseEntity* pEntity)
{
	g_TouchManager.AddEntity(pEntity);
}


void CEntityTouchManager::FrameUpdatePostEntityThink()
{
	VPROF("CEntityTouchManager::FrameUpdatePostEntityThink");
	// Loop through all entities again, checking their untouch if flagged to do so

	int count = m_updateList.Count();
	if (count)
	{
		// copy off the list
		CBaseEntity** ents = (CBaseEntity**)stackalloc(sizeof(CBaseEntity*) * count);
		memcpy(ents, m_updateList.Base(), sizeof(CBaseEntity*) * count);
		// clear it
		m_updateList.RemoveAll();

		// now update those ents
		for (int i = 0; i < count; i++)
		{
			//Assert( ents[i]->GetCheckUntouch() );
			if (ents[i]->GetEngineObject()->GetCheckUntouch())
			{
				ents[i]->GetEngineObject()->PhysicsCheckForEntityUntouch();
			}
		}
		stackfree(ents);
	}
}

class CRespawnEntitiesFilter : public IMapEntityFilter
{
public:
	virtual bool ShouldCreateEntity(const char* pClassname)
	{
		// Create everything but the world
		return Q_stricmp(pClassname, "worldspawn") != 0;
	}

	virtual CBaseEntity* CreateNextEntity(const char* pClassname)
	{
		return gEntList.CreateEntityByName(pClassname);
	}
};

// One hook to rule them all...
// Since most of the little list managers in here only need one or two of the game
// system callbacks, this hook is a game system that passes them the appropriate callbacks
class CEntityListSystem : public CAutoGameSystemPerFrame
{
public:
	CEntityListSystem(char const* name) : CAutoGameSystemPerFrame(name)
	{
		m_bRespawnAllEntities = false;
	}
	void LevelInitPreEntity()
	{
		g_NotifyList.LevelInitPreEntity();
		g_TouchManager.LevelInitPreEntity();
		g_AimManager.LevelInitPreEntity();
		g_SimThinkManager.LevelInitPreEntity();
#ifdef HL2_DLL
		OverrideMoveCache_LevelInitPreEntity();
#endif	// HL2_DLL
	}
	void LevelShutdownPreEntity()
	{
		g_NotifyList.LevelShutdownPreEntity();
	}
	void LevelShutdownPostEntity()
	{
		g_TouchManager.LevelShutdownPostEntity();
		g_AimManager.LevelShutdownPostEntity();
		g_SimThinkManager.LevelShutdownPostEntity();
#ifdef HL2_DLL
		OverrideMoveCache_LevelShutdownPostEntity();
#endif // HL2_DLL
		CBaseEntityClassList* pClassList = s_pClassLists;
		while (pClassList)
		{
			pClassList->LevelShutdownPostEntity();
			pClassList = pClassList->m_pNextClassList;
		}
	}

	void FrameUpdatePostEntityThink()
	{
		g_TouchManager.FrameUpdatePostEntityThink();

		if (m_bRespawnAllEntities)
		{
			m_bRespawnAllEntities = false;

			// Don't change globalstate owing to deletion here
			engine->GlobalEntity_EnableStateUpdates(false);

			// Remove all entities
			int nPlayerIndex = -1;
			CBaseEntity* pEnt = gEntList.FirstEnt();
			while (pEnt)
			{
				CBaseEntity* pNextEnt = gEntList.NextEnt(pEnt);
				if (pEnt->IsPlayer())
				{
					nPlayerIndex = pEnt->entindex();
				}
				if (!pEnt->GetEngineObject()->IsEFlagSet(EFL_KEEP_ON_RECREATE_ENTITIES))
				{
					UTIL_Remove(pEnt);
				}
				pEnt = pNextEnt;
			}

			gEntList.CleanupDeleteList();

			engine->GlobalEntity_EnableStateUpdates(true);

			// Allows us to immediately re-use the edict indices we just freed to avoid edict overflow
			//engine->AllowImmediateEdictReuse();

			// Reset node counter used during load
			CNodeEnt::m_nNodeCount = 0;

			CRespawnEntitiesFilter filter;
			MapEntity_ParseAllEntities(engine->GetMapEntitiesString(), &filter, true);

			// Allocate a CBasePlayer for pev, and call spawn
			if (nPlayerIndex >= 0)
			{
				CBaseEntity* pEdict = gEntList.GetBaseEntity(nPlayerIndex);
				ClientPutInServer(nPlayerIndex, "unnamed");
				ClientActive(nPlayerIndex, false);

				CBasePlayer* pPlayer = (CBasePlayer*)pEdict;
				SceneManager_ClientActive(pPlayer);
			}
		}
	}

	bool m_bRespawnAllEntities;
};

static CEntityListSystem g_EntityListSystem("CEntityListSystem");

//-----------------------------------------------------------------------------
// Respawns all entities in the level
//-----------------------------------------------------------------------------
void RespawnEntities()
{
	g_EntityListSystem.m_bRespawnAllEntities = true;
}

static ConCommand restart_entities("respawn_entities", RespawnEntities, "Respawn all the entities in the map.", FCVAR_CHEAT | FCVAR_SPONLY);

class CSortedEntityList
{
public:
	CSortedEntityList() : m_sortedList(), m_emptyCount(0) {}

	typedef CBaseEntity* ENTITYPTR;
	class CEntityReportLess
	{
	public:
		bool Less(const ENTITYPTR& src1, const ENTITYPTR& src2, void* pCtx)
		{
			if (stricmp(src1->GetClassname(), src2->GetClassname()) < 0)
				return true;

			return false;
		}
	};

	void AddEntityToList(CBaseEntity* pEntity)
	{
		if (!pEntity)
		{
			m_emptyCount++;
		}
		else
		{
			m_sortedList.Insert(pEntity);
		}
	}
	void ReportEntityList()
	{
		const char* pLastClass = "";
		int count = 0;
		int edicts = 0;
		for (int i = 0; i < m_sortedList.Count(); i++)
		{
			CBaseEntity* pEntity = m_sortedList[i];
			if (!pEntity)
				continue;

			if (pEntity->entindex() != -1)
				edicts++;

			const char* pClassname = pEntity->GetClassname();
			if (!FStrEq(pClassname, pLastClass))
			{
				if (count)
				{
					Msg("Class: %s (%d)\n", pLastClass, count);
				}

				pLastClass = pClassname;
				count = 1;
			}
			else
				count++;
		}
		if (pLastClass[0] != 0 && count)
		{
			Msg("Class: %s (%d)\n", pLastClass, count);
		}
		if (m_sortedList.Count())
		{
			Msg("Total %d entities (%d empty, %d edicts)\n", m_sortedList.Count(), m_emptyCount, edicts);
		}
	}
private:
	CUtlSortVector< CBaseEntity*, CEntityReportLess > m_sortedList;
	int		m_emptyCount;
};



CON_COMMAND(report_entities, "Lists all entities")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	CSortedEntityList list;
	CBaseEntity* pEntity = gEntList.FirstEnt();
	while (pEntity)
	{
		list.AddEntityToList(pEntity);
		pEntity = gEntList.NextEnt(pEntity);
	}
	list.ReportEntityList();
}


CON_COMMAND(report_touchlinks, "Lists all touchlinks")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	CSortedEntityList list;
	CBaseEntity* pEntity = gEntList.FirstEnt();
	const char* pClassname = NULL;
	if (args.ArgC() > 1)
	{
		pClassname = args.Arg(1);
	}
	while (pEntity)
	{
		if (!pClassname || FClassnameIs(pEntity, pClassname))
		{
			servertouchlink_t* root = (servertouchlink_t*)pEntity->GetEngineObject()->GetDataObject(TOUCHLINK);
			if (root)
			{
				servertouchlink_t* link = root->nextLink;
				while (link && link != root)
				{
					list.AddEntityToList((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched));
					link = link->nextLink;
				}
			}
		}
		pEntity = gEntList.NextEnt(pEntity);
	}
	list.ReportEntityList();
}

CON_COMMAND(report_simthinklist, "Lists all simulating/thinking entities")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	CBaseEntity* pTmp[NUM_ENT_ENTRIES];
	int count = SimThink_ListCopy(pTmp, ARRAYSIZE(pTmp));

	CSortedEntityList list;
	for (int i = 0; i < count; i++)
	{
		if (!pTmp[i])
			continue;

		list.AddEntityToList(pTmp[i]);
	}
	list.ReportEntityList();
}

