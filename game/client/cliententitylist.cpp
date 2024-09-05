//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $NoKeywords: $
//===========================================================================//

//-----------------------------------------------------------------------------
// Purpose: a global list of all the entities in the game.  All iteration through
//			entities is done through this object.
//-----------------------------------------------------------------------------
#include "cbase.h"
#include "tier0/vprof.h"
#include "cdll_bounded_cvars.h"
#include "cliententitylist.h"
#include "mapentities_shared.h"
#include "coordsize.h"
#include "predictioncopy.h"
#include "tier1/mempool.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
void cc_cl_interp_all_changed(IConVar* pConVar, const char* pOldString, float flOldValue)
{
	ConVarRef var(pConVar);
	if (var.GetInt())
	{
		C_BaseEntityIterator iterator;
		C_BaseEntity* pEnt;
		while ((pEnt = iterator.Next()) != NULL)
		{
			if (pEnt->ShouldInterpolate())
			{
				pEnt->AddToInterpolationList();
			}
		}
	}
}

static ConVar  cl_interp_all("cl_interp_all", "0", 0, "Disable interpolation list optimizations.", 0, 0, 0, 0, cc_cl_interp_all_changed);
extern ConVar	cl_showerror;
extern ConVar think_limit;

// Create interface
static CClientEntityList<C_BaseEntity> s_EntityList;
CBaseEntityList<C_BaseEntity> *g_pEntityList = &s_EntityList;

// Expose list to engine
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CClientEntityList, IClientEntityList, VCLIENTENTITYLIST_INTERFACE_VERSION, s_EntityList );

// Store local pointer to interface for rest of client .dll only 
//  (CClientEntityList instead of IClientEntityList )
CClientEntityList<C_BaseEntity> *cl_entitylist = &s_EntityList;

//static clienttouchlink_t *g_pNextLink = NULL;
int linksallocated = 0;
int groundlinksallocated = 0;
// memory pool for storing links between entities
static CUtlMemoryPool g_EdictTouchLinks(sizeof(clienttouchlink_t), MAX_EDICTS, CUtlMemoryPool::GROW_NONE, "g_EdictTouchLinks");
static CUtlMemoryPool g_EntityGroundLinks(sizeof(clientgroundlink_t), MAX_EDICTS, CUtlMemoryPool::GROW_NONE, "g_EntityGroundLinks");
#ifndef CLIENT_DLL
ConVar debug_touchlinks("debug_touchlinks", "0", 0, "Spew touch link activity");
#define DebugTouchlinks() debug_touchlinks.GetBool()
#else
#define DebugTouchlinks() false
#endif

template<>
bool CClientEntityList<C_BaseEntity>::sm_bDisableTouchFuncs = false;	// Disables PhysicsTouch and PhysicsStartTouch function calls
bool C_EngineObjectInternal::s_bAbsQueriesValid = true;
bool C_EngineObjectInternal::s_bAbsRecomputationEnabled = true;
static bool g_bAbsRecomputationStack[8];
static unsigned short g_iAbsRecomputationStackPos = 0;

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
// Output : inline clienttouchlink_t
//-----------------------------------------------------------------------------
inline clienttouchlink_t* AllocTouchLink(void)
{
	clienttouchlink_t* link = (clienttouchlink_t*)g_EdictTouchLinks.Alloc(sizeof(clienttouchlink_t));
	if (link)
	{
		++linksallocated;
	}
	else
	{
		DevWarning("AllocTouchLink: failed to allocate clienttouchlink_t.\n");
	}

	return link;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *link - 
// Output : inline void
//-----------------------------------------------------------------------------
inline void FreeTouchLink(clienttouchlink_t* link)
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
inline clientgroundlink_t* AllocGroundLink(void)
{
	clientgroundlink_t* link = (clientgroundlink_t*)g_EntityGroundLinks.Alloc(sizeof(clientgroundlink_t));
	if (link)
	{
		++groundlinksallocated;
	}
	else
	{
		DevMsg("AllocGroundLink: failed to allocate clientgroundlink_t.!!!  groundlinksallocated=%d g_EntityGroundLinks.Count()=%d\n", groundlinksallocated, g_EntityGroundLinks.Count());
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
inline void FreeGroundLink(clientgroundlink_t* link)
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

// -------------------------------------------------------------------------------------------------- //
// Game-code CBaseHandle implementation.
// -------------------------------------------------------------------------------------------------- //
int addVarCount = 0;
const float coordTolerance = 2.0f / (float)(1 << COORD_FRACTIONAL_BITS);

BEGIN_PREDICTION_DATA_NO_BASE(C_EngineObjectInternal)
	DEFINE_PRED_TYPEDESCRIPTION(m_Collision, CCollisionProperty),
	DEFINE_FIELD(m_vecAbsVelocity, FIELD_VECTOR),
	DEFINE_PRED_FIELD_TOL(m_vecVelocity, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.5f),
	DEFINE_FIELD(m_vecAbsOrigin, FIELD_VECTOR),
	DEFINE_FIELD(m_angAbsRotation, FIELD_VECTOR),
	DEFINE_FIELD(m_vecOrigin, FIELD_VECTOR),
	DEFINE_FIELD(m_angRotation, FIELD_VECTOR),
	DEFINE_PRED_FIELD_TOL(m_vecNetworkOrigin, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, coordTolerance),
	DEFINE_PRED_FIELD(m_angNetworkAngles, FIELD_VECTOR, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK),
	DEFINE_PRED_FIELD(m_hNetworkMoveParent, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_hGroundEntity, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_nModelIndex, FIELD_SHORT, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX),
	DEFINE_PRED_FIELD(m_fFlags, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_fEffects, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_OVERRIDE),
	DEFINE_FIELD(m_flGravity, FIELD_FLOAT),
	DEFINE_PRED_FIELD(m_flFriction, FIELD_FLOAT, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_nNextThinkTick, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_MoveType, FIELD_CHARACTER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_MoveCollide, FIELD_CHARACTER, FTYPEDESC_INSENDTABLE),
	DEFINE_FIELD(m_flProxyRandomValue, FIELD_FLOAT),
	//DEFINE_PRED_FIELD(m_flAnimTime, FIELD_FLOAT, 0),
	DEFINE_PRED_FIELD(m_nSkin, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_FIELD(m_nBody, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
	DEFINE_PRED_ARRAY_TOL(m_flEncodedController, FIELD_FLOAT, MAXSTUDIOBONECTRLS, FTYPEDESC_INSENDTABLE, 0.02f),
	DEFINE_PRED_FIELD(m_nSequence, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK),
	DEFINE_PRED_FIELD(m_flPlaybackRate, FIELD_FLOAT, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK),
	DEFINE_PRED_FIELD(m_flCycle, FIELD_FLOAT, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK),
	DEFINE_PRED_FIELD(m_nNewSequenceParity, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK),
	DEFINE_PRED_FIELD(m_nResetEventsParity, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK),
	DEFINE_PRED_FIELD(m_nMuzzleFlashParity, FIELD_CHARACTER, FTYPEDESC_INSENDTABLE),
END_PREDICTION_DATA()

BEGIN_DATADESC_NO_BASE(C_EngineObjectInternal)
	DEFINE_FIELD(m_vecAbsOrigin, FIELD_POSITION_VECTOR),
	DEFINE_FIELD(m_angAbsRotation, FIELD_VECTOR),
	DEFINE_ARRAY(m_rgflCoordinateFrame, FIELD_FLOAT, 12 ), // NOTE: MUST BE IN LOCAL SPACE, NOT POSITION_VECTOR!!! (see CBaseEntity::Restore)
	DEFINE_FIELD(m_fFlags, FIELD_INTEGER),
	DEFINE_FIELD(m_iEFlags, FIELD_INTEGER),
	DEFINE_FIELD(m_ModelName, FIELD_STRING),
	DEFINE_FIELD(m_nBody, FIELD_INTEGER),
	DEFINE_FIELD(m_nSkin, FIELD_INTEGER),
END_DATADESC()

//-----------------------------------------------------------------------------
// Moveparent receive proxies
//-----------------------------------------------------------------------------
void RecvProxy_IntToMoveParent(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	CHandle<C_BaseEntity>* pHandle = (CHandle<C_BaseEntity>*)pOut;
	RecvProxy_IntToEHandle(pData, pStruct, (CBaseHandle*)pHandle);
	C_EngineObjectInternal* pEntity = (C_EngineObjectInternal*)pStruct;
	C_BaseEntity* pMoveParent = pHandle->Get();
	if (pMoveParent&& pMoveParent->entindex()==1) {
		int aaa = 0;
	}
	if (pMoveParent) {
		if (pEntity->GetNetworkMoveParent()) {
			int aaa = 0;
		}
		else {
			Error("cannot happen");
		}
	}
	else {
		if (pEntity->GetNetworkMoveParent()) {
			Error("cannot happen");
		}
		else {
			int aaa = 0;
		}
	}
}

void RecvProxy_LocalVelocity( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	C_EngineObjectInternal *pEnt = (C_EngineObjectInternal*)pStruct;

	Vector vecVelocity;
	
	vecVelocity.x = pData->m_Value.m_Vector[0];
	vecVelocity.y = pData->m_Value.m_Vector[1];
	vecVelocity.z = pData->m_Value.m_Vector[2];

	// SetLocalVelocity checks to see if the value has changed
	pEnt->SetLocalVelocity( vecVelocity );
}

void RecvProxy_EffectFlags(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	((C_EngineObjectInternal*)pStruct)->SetEffects(pData->m_Value.m_Int);
}

static void RecvProxy_MoveType(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	((C_EngineObjectInternal*)pStruct)->SetMoveType((MoveType_t)(pData->m_Value.m_Int));
}

static void RecvProxy_MoveCollide(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	((C_EngineObjectInternal*)pStruct)->SetMoveCollide((MoveCollide_t)(pData->m_Value.m_Int));
}

void RecvProxy_InterpolationAmountChanged(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	// m_bSimulatedEveryTick & m_bAnimatedEveryTick are boolean
	if (*((bool*)pOut) != (pData->m_Value.m_Int != 0))
	{
		// Have the regular proxy store the data.
		RecvProxy_Int32ToInt8(pData, pStruct, pOut);

		C_EngineObjectInternal* pEntity = (C_EngineObjectInternal*)pStruct;
		pEntity->Interp_UpdateInterpolationAmounts();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Decodes animtime and notes when it changes
// Input  : *pStruct - ( C_BaseEntity * ) used to flag animtime is changine
//			*pVarData - 
//			*pIn - 
//			objectID - 
//-----------------------------------------------------------------------------
void RecvProxy_AnimTime(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	C_EngineObjectInternal* pEntity = (C_EngineObjectInternal*)pStruct;
	//Assert(pOut == &pEntity->m_flAnimTime);

	int t;
	int tickbase;
	int addt;

	// Unpack the data.
	addt = pData->m_Value.m_Int;

	// Note, this needs to be encoded relative to packet timestamp, not raw client clock
	tickbase = gpGlobals->GetNetworkBase(gpGlobals->tickcount, pEntity->entindex());

	t = tickbase;
	//  and then go back to floating point time.
	t += addt;				// Add in an additional up to 256 100ths from the server

	// center m_flAnimTime around current time.
	while (t < gpGlobals->tickcount - 127)
		t += 256;
	while (t > gpGlobals->tickcount + 127)
		t -= 256;

	pEntity->SetAnimTime(t * TICK_INTERVAL);
}

void RecvProxy_SimulationTime(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	C_EngineObjectInternal* pEntity = (C_EngineObjectInternal*)pStruct;
	//Assert(pOut == &pEntity->m_flSimulationTime);

	int t;
	int tickbase;
	int addt;

	// Unpack the data.
	addt = pData->m_Value.m_Int;

	// Note, this needs to be encoded relative to packet timestamp, not raw client clock
	tickbase = gpGlobals->GetNetworkBase(gpGlobals->tickcount, pEntity->entindex());

	t = tickbase;
	//  and then go back to floating point time.
	t += addt;				// Add in an additional up to 256 100ths from the server

	// center m_flSimulationTime around current time.
	while (t < gpGlobals->tickcount - 127)
		t += 256;
	while (t > gpGlobals->tickcount + 127)
		t -= 256;

	pEntity->SetSimulationTime(t * TICK_INTERVAL);
}

BEGIN_RECV_TABLE_NOBASE(C_EngineObjectInternal, DT_AnimTimeMustBeFirst)
	RecvPropInt(RECVINFO(m_flAnimTime), 0, RecvProxy_AnimTime),
END_RECV_TABLE()

BEGIN_RECV_TABLE_NOBASE(C_EngineObjectInternal, DT_SimulationTimeMustBeFirst)
	RecvPropInt(RECVINFO(m_flSimulationTime), 0, RecvProxy_SimulationTime),
END_RECV_TABLE()

void RecvProxy_Sequence(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	C_EngineObjectInternal* pEntity = (C_EngineObjectInternal*)pStruct;
	if (pEntity->GetOuter()->IsViewModel()) {
		if (pData->m_Value.m_Int != pEntity->GetSequence())
		{
			MDLCACHE_CRITICAL_SECTION();

			pEntity->SetSequence(pData->m_Value.m_Int);
			pEntity->SetAnimTime(gpGlobals->curtime);
			pEntity->SetCycle(0);
		}
	}else{
		// Have the regular proxy store the data.
		RecvProxy_Int32ToInt32(pData, pStruct, pOut);

		pEntity->SetReceivedSequence();

		// render bounds may have changed
		pEntity->GetOuter()->UpdateVisibility();
	}
}

BEGIN_RECV_TABLE_NOBASE(C_EngineObjectInternal, DT_ServerAnimationData)
	RecvPropFloat(RECVINFO(m_flCycle)),
	RecvPropArray3(RECVINFO_ARRAY(m_flPoseParameter), RecvPropFloat(RECVINFO(m_flPoseParameter[0]))),
	RecvPropFloat(RECVINFO(m_flPlaybackRate)),
	RecvPropInt(RECVINFO(m_nSequence), 0, RecvProxy_Sequence),
	RecvPropInt(RECVINFO(m_nNewSequenceParity)),
	RecvPropInt(RECVINFO(m_nResetEventsParity)),
	RecvPropInt(RECVINFO(m_nMuzzleFlashParity)),
END_RECV_TABLE()

BEGIN_RECV_TABLE_NOBASE(C_EngineObjectInternal, DT_EngineObject)
	RecvPropDataTable("AnimTimeMustBeFirst", 0, 0, &REFERENCE_RECV_TABLE(DT_AnimTimeMustBeFirst)),
	RecvPropDataTable("SimulationTimeMustBeFirst", 0, 0, &REFERENCE_RECV_TABLE(DT_SimulationTimeMustBeFirst)),
	RecvPropInt(RECVINFO(testNetwork)),
	RecvPropVector(RECVINFO_NAME(m_vecNetworkOrigin, m_vecOrigin)),
#if PREDICTION_ERROR_CHECK_LEVEL > 1 
	RecvPropVector(RECVINFO_NAME(m_angNetworkAngles, m_angRotation)),
#else
	RecvPropQAngles(RECVINFO_NAME(m_angNetworkAngles, m_angRotation)),
#endif
	RecvPropVector(RECVINFO(m_vecVelocity), 0, RecvProxy_LocalVelocity),
	RecvPropInt(RECVINFO_NAME(m_hNetworkMoveParent, moveparent), 0, RecvProxy_IntToMoveParent),
	RecvPropInt(RECVINFO(m_iParentAttachment)),
	RecvPropEHandle(RECVINFO(m_hGroundEntity)),
#ifdef DEMO_BACKWARDCOMPATABILITY
	RecvPropInt(RECVINFO(m_nModelIndex), 0, RecvProxy_IntToModelIndex16_BackCompatible),
#else
	RecvPropInt(RECVINFO(m_nModelIndex)),
#endif
	RecvPropInt(RECVINFO(m_spawnflags)),
	RecvPropDataTable(RECVINFO_DT(m_Collision), 0, &REFERENCE_RECV_TABLE(DT_CollisionProperty)),
	RecvPropInt(RECVINFO(m_CollisionGroup)),
	RecvPropInt(RECVINFO(m_fFlags)),
	RecvPropInt(RECVINFO(m_fEffects), 0, RecvProxy_EffectFlags),
	RecvPropFloat(RECVINFO(m_flFriction)),
	RecvPropFloat(RECVINFO(m_flElasticity)),
	RecvPropInt(RECVINFO(m_nNextThinkTick)),
	RecvPropInt("movetype", 0, SIZEOF_IGNORE, 0, RecvProxy_MoveType),
	RecvPropInt("movecollide", 0, SIZEOF_IGNORE, 0, RecvProxy_MoveCollide),
	RecvPropInt(RECVINFO(m_bSimulatedEveryTick), 0, RecvProxy_InterpolationAmountChanged),
	RecvPropInt(RECVINFO(m_bAnimatedEveryTick), 0, RecvProxy_InterpolationAmountChanged),
	RecvPropInt(RECVINFO(m_bClientSideAnimation)),
	RecvPropInt(RECVINFO(m_nForceBone)),
	RecvPropVector(RECVINFO(m_vecForce)),
	RecvPropInt(RECVINFO(m_nSkin)),
	RecvPropInt(RECVINFO(m_nBody)),
	RecvPropInt(RECVINFO(m_nHitboxSet)),

	RecvPropFloat(RECVINFO(m_flModelScale)),
	RecvPropFloat(RECVINFO_NAME(m_flModelScale, m_flModelWidthScale)), // for demo compatibility only
	RecvPropArray3(RECVINFO_ARRAY(m_flEncodedController), RecvPropFloat(RECVINFO(m_flEncodedController[0]))),
	RecvPropInt(RECVINFO(m_bClientSideFrameReset)),
	RecvPropDataTable("serveranimdata", 0, 0, &REFERENCE_RECV_TABLE(DT_ServerAnimationData)),

END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_NO_FACTORY(C_EngineObjectInternal, DT_EngineObject, CEngineObjectInternal);

int	C_EngineObjectInternal::entindex() const {
	return m_pOuter->entindex();
}

RecvTable* C_EngineObjectInternal::GetRecvTable() {
	return &DT_EngineObject::g_RecvTable;
}

//-----------------------------------------------------------------------------
// Global methods related to when abs data is correct
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetAbsQueriesValid(bool bValid)
{
	// @MULTICORE: Always allow in worker threads, assume higher level code is handling correctly
	if (!ThreadInMainThread())
		return;

	if (!bValid)
	{
		s_bAbsQueriesValid = false;
	}
	else
	{
		s_bAbsQueriesValid = true;
	}
}

bool C_EngineObjectInternal::IsAbsQueriesValid(void)
{
	if (!ThreadInMainThread())
		return true;
	return s_bAbsQueriesValid;
}

void C_EngineObjectInternal::EnableAbsRecomputations(bool bEnable)
{
	if (!ThreadInMainThread())
		return;
	// This should only be called at the frame level. Use PushEnableAbsRecomputations
	// if you're blocking out a section of code.
	Assert(g_iAbsRecomputationStackPos == 0);

	s_bAbsRecomputationEnabled = bEnable;
}

bool C_EngineObjectInternal::IsAbsRecomputationsEnabled()
{
	if (!ThreadInMainThread())
		return true;
	return s_bAbsRecomputationEnabled;
}

void C_EngineObjectInternal::PushEnableAbsRecomputations(bool bEnable)
{
	if (!ThreadInMainThread())
		return;
	if (g_iAbsRecomputationStackPos < ARRAYSIZE(g_bAbsRecomputationStack))
	{
		g_bAbsRecomputationStack[g_iAbsRecomputationStackPos] = s_bAbsRecomputationEnabled;
		++g_iAbsRecomputationStackPos;
		s_bAbsRecomputationEnabled = bEnable;
	}
	else
	{
		Assert(false);
	}
}

void C_EngineObjectInternal::PopEnableAbsRecomputations()
{
	if (!ThreadInMainThread())
		return;
	if (g_iAbsRecomputationStackPos > 0)
	{
		--g_iAbsRecomputationStackPos;
		s_bAbsRecomputationEnabled = g_bAbsRecomputationStack[g_iAbsRecomputationStackPos];
	}
	else
	{
		Assert(false);
	}
}

#include "tier0/memdbgoff.h"

//-----------------------------------------------------------------------------
// C_BaseEntity new/delete
// All fields in the object are all initialized to 0.
//-----------------------------------------------------------------------------
void* C_EngineObjectInternal::operator new(size_t stAllocateBlock)
{
	Assert(stAllocateBlock != 0);
	MEM_ALLOC_CREDIT();
	void* pMem = MemAlloc_Alloc(stAllocateBlock);
	memset(pMem, 0, stAllocateBlock);
	return pMem;
}

void* C_EngineObjectInternal::operator new[](size_t stAllocateBlock)
{
	Assert(stAllocateBlock != 0);
	MEM_ALLOC_CREDIT();
	void* pMem = MemAlloc_Alloc(stAllocateBlock);
	memset(pMem, 0, stAllocateBlock);
	return pMem;
}

void* C_EngineObjectInternal::operator new(size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine)
{
	Assert(stAllocateBlock != 0);
	void* pMem = MemAlloc_Alloc(stAllocateBlock, pFileName, nLine);
	memset(pMem, 0, stAllocateBlock);
	return pMem;
}

void* C_EngineObjectInternal::operator new[](size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine)
{
	Assert(stAllocateBlock != 0);
	void* pMem = MemAlloc_Alloc(stAllocateBlock, pFileName, nLine);
	memset(pMem, 0, stAllocateBlock);
	return pMem;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pMem - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::operator delete(void* pMem)
{
	// get the engine to free the memory
	MemAlloc_Free(pMem);
}

#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : org - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetNetworkOrigin(const Vector& org)
{
	m_vecNetworkOrigin = org;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : ang - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetNetworkAngles(const QAngle& ang)
{
	m_angNetworkAngles = ang;
}

void C_EngineObjectInternal::SetNetworkMoveParent(IEngineObjectClient* pMoveParent) {
	m_hNetworkMoveParent = pMoveParent? pMoveParent->GetOuter():NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Handles keys and outputs from the BSP.
// Input  : mapData - Text block of keys and values from the BSP.
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::ParseMapData(IEntityMapData* mapData)
{
	char keyName[MAPKEY_MAXLENGTH];
	char value[MAPKEY_MAXLENGTH];

//#ifdef _DEBUG
//#ifdef GAME_DLL
//	ValidateDataDescription();
//#endif // GAME_DLL
//#endif // _DEBUG

	// loop through all keys in the data block and pass the info back into the object
	if (mapData->GetFirstKey(keyName, value))
	{
		do
		{
			if (!KeyValue(keyName, value)) {
				m_pOuter->KeyValue(keyName, value);
			}
		} while (mapData->GetNextKey(keyName, value));
	}
}

//-----------------------------------------------------------------------------
// Parse data from a map file
//-----------------------------------------------------------------------------
bool C_EngineObjectInternal::KeyValue(const char* szKeyName, const char* szValue)
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

	// key hasn't been handled
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Saves the current object out to disk, by iterating through the objects
//			data description hierarchy
// Input  : &save - save buffer which the class data is written to
// Output : int	- 0 if the save failed, 1 on success
//-----------------------------------------------------------------------------
int C_EngineObjectInternal::Save(ISave& save)
{
	// loop through the data description list, saving each data desc block
	int status = save.WriteEntity(this->m_pOuter);

	return status;
}

//-----------------------------------------------------------------------------
// Purpose: Restores the current object from disk, by iterating through the objects
//			data description hierarchy
// Input  : &restore - restore buffer which the class data is read from
// Output : int	- 0 if the restore failed, 1 on success
//-----------------------------------------------------------------------------
int C_EngineObjectInternal::Restore(IRestore& restore)
{
	// loops through the data description list, restoring each data desc block in order
	int status = restore.ReadEntity(this->m_pOuter);

	// NOTE: Do *not* use GetAbsOrigin() here because it will
	// try to recompute m_rgflCoordinateFrame!
	//MatrixSetColumn(GetEngineObject()->m_vecAbsOrigin, 3, GetEngineObject()->m_rgflCoordinateFrame);
	ResetRgflCoordinateFrame();

	// Restablish ground entity
	if (GetGroundEntity() != NULL)
	{
		GetGroundEntity()->AddEntityToGroundList(this);
	}

	return status;
}

//-----------------------------------------------------------------------------
// handler to do stuff before you are saved
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::OnSave()
{
	// Here, we must force recomputation of all abs data so it gets saved correctly
	// We can't leave the dirty bits set because the loader can't cope with it.
	CalcAbsolutePosition();
	CalcAbsoluteVelocity();
	m_pOuter->OnSave();
}

//-----------------------------------------------------------------------------
// handler to do stuff after you are restored
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::OnRestore()
{
	InvalidatePhysicsRecursive(POSITION_CHANGED | ANGLES_CHANGED | VELOCITY_CHANGED);

	m_pOuter->OnRestore();
}

//-----------------------------------------------------------------------------
// Purpose: Entity is about to be decoded from the network stream
// Input  : bnewentity - is this a new entity this update?
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::PreDataUpdate(DataUpdateType_t updateType)
{
	VPROF("C_BaseEntity::PreDataUpdate");

	// Register for an OnDataChanged call and call OnPreDataChanged().
	if (AddDataChangeEvent(this, updateType, &m_DataChangeEventRef))
	{
		OnPreDataChanged(updateType);
	}


	// Need to spawn on client before receiving original network data 
	// in case it overrides any values set up in spawn ( e.g., m_iState )
	bool bnewentity = (updateType == DATA_UPDATE_CREATED);

	if (!bnewentity)
	{
		this->Interp_RestoreToLastNetworked();
	}

	if (bnewentity /*&& !IsClientCreated()*/)
	{
		m_flSpawnTime = engine->GetLastTimeStamp();
		MDLCACHE_CRITICAL_SECTION();
		m_pOuter->Spawn();
	}

	m_vecOldOrigin = GetNetworkOrigin();
	m_vecOldAngRotation = GetNetworkAngles();

	m_flOldAnimTime = m_flAnimTime;
	m_flOldSimulationTime = m_flSimulationTime;

	m_flOldCycle = GetCycle();
	m_nOldSequence = GetSequence();
	m_flOldModelScale = GetModelScale();

	int i;
	for (i = 0; i < MAXSTUDIOBONECTRLS; i++)
	{
		m_flOldEncodedController[i] = m_flEncodedController[i];
	}

	for (i = 0; i < MAXSTUDIOPOSEPARAM; i++)
	{
		m_flOldPoseParameters[i] = m_flPoseParameter[i];
	}

	m_pOuter->PreDataUpdate(updateType);
}

void C_EngineObjectInternal::OnPreDataChanged(DataUpdateType_t type)
{
	m_bLastClientSideFrameReset = m_bClientSideFrameReset;
	m_pOuter->OnPreDataChanged(type);
}

//-----------------------------------------------------------------------------
// Call this in PostDataUpdate if you don't chain it down!
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::MarkMessageReceived()
{
	m_flLastMessageTime = engine->GetLastTimeStamp();
}


//-----------------------------------------------------------------------------
// Purpose: Entity data has been parsed and unpacked.  Now do any necessary decoding, munging
// Input  : bnewentity - was this entity new in this update packet?
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::PostDataUpdate(DataUpdateType_t updateType)
{
	MDLCACHE_CRITICAL_SECTION();

	PREDICTION_TRACKVALUECHANGESCOPE_ENTITY(this->m_pOuter, "postdataupdate");

	// NOTE: This *has* to happen first. Otherwise, Origin + angles may be wrong 
	if (m_pOuter->m_nRenderFX == kRenderFxRagdoll && updateType == DATA_UPDATE_CREATED)
	{
		m_pOuter->MoveToLastReceivedPosition(true);
	}
	else
	{
		m_pOuter->MoveToLastReceivedPosition(false);
	}


	// If it's the world, force solid flags
	if (entindex() == 0)
	{
		SetModelIndex(1);
		SetSolid(SOLID_BSP);

		// FIXME: Should these be assertions?
		SetAbsOrigin(vec3_origin);
		SetAbsAngles(vec3_angle);
	}

	bool animTimeChanged = (m_flAnimTime != m_flOldAnimTime) ? true : false;
	bool originChanged = (m_vecOldOrigin != GetLocalOrigin()) ? true : false;
	bool anglesChanged = (m_vecOldAngRotation != GetLocalAngles()) ? true : false;
	bool simTimeChanged = (m_flSimulationTime != m_flOldSimulationTime) ? true : false;

	// Detect simulation changes 
	bool simulationChanged = originChanged || anglesChanged || simTimeChanged;

	bool bPredictable = m_pOuter->GetPredictable();

	// For non-predicted and non-client only ents, we need to latch network values into the interpolation histories
	if (!bPredictable /*&& !IsClientCreated()*/)
	{
		if (animTimeChanged)
		{
			this->OnLatchInterpolatedVariables(LATCH_ANIMATION_VAR);
		}

		if (simulationChanged)
		{
			this->OnLatchInterpolatedVariables(LATCH_SIMULATION_VAR);
		}
	}
	// For predictables, we also need to store off the last networked value
	else if (bPredictable)
	{
		// Just store off last networked value for use in prediction
		this->OnStoreLastNetworkedValue();
	}

	// Deal with hierarchy. Have to do it here (instead of in a proxy)
	// because this is the only point at which all entities are loaded
	// If this condition isn't met, then a child was sent without its parent
	//Assert( m_hNetworkMoveParent.Get() || !m_hNetworkMoveParent.IsValid() );
	HierarchySetParent(GetNetworkMoveParent());

	MarkMessageReceived();

	// Make sure that the correct model is referenced for this entity
	m_pOuter->ValidateModelIndex();

	// If this entity was new, then latch in various values no matter what.
	if (updateType == DATA_UPDATE_CREATED)
	{
		// Construct a random value for this instance
		m_flProxyRandomValue = random->RandomFloat(0, 1);

		m_pOuter->ResetLatched();

		m_nCreationTick = gpGlobals->tickcount;
	}

	m_pOuter->CheckInitPredictable("PostDataUpdate");

	if (IsUsingClientSideAnimation())
	{
		SetCycle(m_flOldCycle);
		m_pOuter->AddToClientSideAnimationList();
	}
	else
	{
		if (m_pOuter->IsViewModel()) {
			SetCycle(m_flOldCycle);
		}
		m_pOuter->RemoveFromClientSideAnimationList();
	}

	bool bBoneControllersChanged = false;

	int i;
	for (i = 0; i < MAXSTUDIOBONECTRLS && !bBoneControllersChanged; i++)
	{
		if (m_flOldEncodedController[i] != m_flEncodedController[i])
		{
			bBoneControllersChanged = true;
		}
	}

	bool bPoseParametersChanged = false;

	for (i = 0; i < MAXSTUDIOPOSEPARAM && !bPoseParametersChanged; i++)
	{
		if (m_flOldPoseParameters[i] != m_flPoseParameter[i])
		{
			bPoseParametersChanged = true;
		}
	}

	// Cycle change? Then re-render
	bool bAnimationChanged = m_flOldCycle != GetCycle() || bBoneControllersChanged || bPoseParametersChanged;
	bool bSequenceChanged = m_nOldSequence != GetSequence();
	bool bScaleChanged = (m_flOldModelScale != GetModelScale());
	if (bAnimationChanged || bSequenceChanged || bScaleChanged)
	{
		InvalidatePhysicsRecursive(ANIMATION_CHANGED);
	}

	if (bAnimationChanged || bSequenceChanged)
	{
		if (IsUsingClientSideAnimation())
		{
			m_pOuter->ClientSideAnimationChanged();
		}
	}

	// reset prev cycle if new sequence
	if (m_nNewSequenceParity != m_nPrevNewSequenceParity)
	{
		// It's important not to call Reset() on a static prop, because if we call
		// Reset(), then the entity will stay in the interpolated entities list
		// forever, wasting CPU.
		MDLCACHE_CRITICAL_SECTION();
		IStudioHdr* hdr = GetModelPtr();
		if (hdr && !(hdr->flags() & STUDIOHDR_FLAGS_STATIC_PROP))
		{
			m_iv_flCycle.Reset();
		}
	}

	m_pOuter->PostDataUpdate(updateType);
}

void C_EngineObjectInternal::OnDataChanged(DataUpdateType_t type)
{
	// Only need to think if animating client side
	if (IsUsingClientSideAnimation())
	{
		// Check to see if we should reset our frame
		if (m_bClientSideFrameReset != m_bLastClientSideFrameReset)
		{
			ResetClientsideFrame();
		}
	}
	// See if it needs to allocate prediction stuff
	m_pOuter->CheckInitPredictable("OnDataChanged");
	m_pOuter->OnDataChanged(type);
}

const Vector& C_EngineObjectInternal::GetOldOrigin()
{
	return m_vecOldOrigin;
}

int C_EngineObjectInternal::GetCreationTick() const
{
	return m_nCreationTick;
}

float C_EngineObjectInternal::GetLastChangeTime(int flags)
{
	if (m_pOuter->GetPredictable() /*|| IsClientCreated()*/)
	{
		return gpGlobals->curtime;
	}

	// make sure not both flags are set, we can't resolve that
	Assert(!((flags & LATCH_ANIMATION_VAR) && (flags & LATCH_SIMULATION_VAR)));

	if (flags & LATCH_ANIMATION_VAR)
	{
		return GetAnimTime();
	}

	if (flags & LATCH_SIMULATION_VAR)
	{
		float st = GetSimulationTime();
		if (st == 0.0f)
		{
			return gpGlobals->curtime;
		}
		return st;
	}

	Assert(0);

	return gpGlobals->curtime;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *map - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::Interp_SetupMappings()
{
	int c = m_VarMap.m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &m_VarMap.m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;
		//void* data = e->data;
		int type = e->type;

		//watcher->Setup(data, type);
		watcher->SetInterpolationAmount(m_pOuter->GetInterpolationAmount(watcher->GetType()));
	}
}

void C_EngineObjectInternal::Interp_RestoreToLastNetworked()
{
	VPROF("C_BaseEntity::Interp_RestoreToLastNetworked");

	PREDICTION_TRACKVALUECHANGESCOPE_ENTITY(this->m_pOuter, "restoretolastnetworked");

	Vector oldOrigin = GetLocalOrigin();
	QAngle oldAngles = GetLocalAngles();
	Vector oldVel = GetLocalVelocity();

	int c = m_VarMap.m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &m_VarMap.m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;
		watcher->RestoreToLastNetworked();
	}

	BaseInterpolatePart2(oldOrigin, oldAngles, oldVel, 0);
}

void C_EngineObjectInternal::Interp_UpdateInterpolationAmounts()
{
	int c = m_VarMap.m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &m_VarMap.m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;
		watcher->SetInterpolationAmount(m_pOuter->GetInterpolationAmount(watcher->GetType()));
	}
}

void C_EngineObjectInternal::Interp_HierarchyUpdateInterpolationAmounts()
{
	Interp_UpdateInterpolationAmounts();

	for (C_EngineObjectInternal* pChild = FirstMoveChild(); pChild; pChild = pChild->NextMovePeer())
	{
		pChild->Interp_HierarchyUpdateInterpolationAmounts();
	}
}

inline int C_EngineObjectInternal::Interp_Interpolate(float currentTime)
{
	int bNoMoreChanges = 1;
	if (currentTime < m_VarMap.m_lastInterpolationTime)
	{
		for (int i = 0; i < m_VarMap.m_nInterpolatedEntries; i++)
		{
			VarMapEntry_t* e = &m_VarMap.m_Entries[i];

			e->m_bNeedsToInterpolate = true;
		}
	}
	m_VarMap.m_lastInterpolationTime = currentTime;

	for (int i = 0; i < m_VarMap.m_nInterpolatedEntries; i++)
	{
		VarMapEntry_t* e = &m_VarMap.m_Entries[i];

		if (!e->m_bNeedsToInterpolate)
			continue;

		IInterpolatedVar* watcher = e->watcher;
		Assert(!(watcher->GetType() & EXCLUDE_AUTO_INTERPOLATE));


		if (watcher->Interpolate(currentTime))
			e->m_bNeedsToInterpolate = false;
		else
			bNoMoreChanges = 0;
	}

	return bNoMoreChanges;
}

//-----------------------------------------------------------------------------
// Purpose: Retrieves the coordinate frame for this entity.
// Input  : forward - Receives the entity's forward vector.
//			right - Receives the entity's right vector.
//			up - Receives the entity's up vector.
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::GetVectors(Vector* pForward, Vector* pRight, Vector* pUp) const
{
	// This call is necessary to cause m_rgflCoordinateFrame to be recomputed
	const matrix3x4_t& entityToWorld = EntityToWorldTransform();

	if (pForward != NULL)
	{
		MatrixGetColumn(entityToWorld, 0, *pForward);
	}

	if (pRight != NULL)
	{
		MatrixGetColumn(entityToWorld, 1, *pRight);
		*pRight *= -1.0f;
	}

	if (pUp != NULL)
	{
		MatrixGetColumn(entityToWorld, 2, *pUp);
	}
}

ITypedInterpolatedVar< QAngle >& C_EngineObjectInternal::GetRotationInterpolator()
{
	return m_iv_angRotation;
}

ITypedInterpolatedVar< Vector >& C_EngineObjectInternal::GetOriginInterpolator()
{
	return m_iv_vecOrigin;
}

//-----------------------------------------------------------------------------
// Purpose: Last received origin
// Output : const float
//-----------------------------------------------------------------------------
const Vector& C_EngineObjectInternal::GetAbsOrigin(void)
{
	//Assert( s_bAbsQueriesValid );
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsolutePosition();
	return m_vecAbsOrigin;
}

//-----------------------------------------------------------------------------
// Purpose: Last received origin
// Output : const float
//-----------------------------------------------------------------------------
const Vector& C_EngineObjectInternal::GetAbsOrigin(void) const
{
	//Assert( s_bAbsQueriesValid );
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsolutePosition();
	return m_vecAbsOrigin;
}

//-----------------------------------------------------------------------------
// Purpose: Last received angles
// Output : const
//-----------------------------------------------------------------------------
const QAngle& C_EngineObjectInternal::GetAbsAngles(void)
{
	//Assert( s_bAbsQueriesValid );
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsolutePosition();
	return m_angAbsRotation;
}

//-----------------------------------------------------------------------------
// Purpose: Last received angles
// Output : const
//-----------------------------------------------------------------------------
const QAngle& C_EngineObjectInternal::GetAbsAngles(void) const
{
	//Assert( s_bAbsQueriesValid );
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsolutePosition();
	return m_angAbsRotation;
}


void C_EngineObjectInternal::UnlinkChild(IEngineObjectClient* pChild)
{
	Assert(pChild);
	Assert(this != pChild);
	Assert(pChild->GetMoveParent() == this);

	// Unlink from parent
	// NOTE: pParent *may well be NULL*! This occurs
	// when a child has unlinked from a parent, and the child
	// remains in the PVS but the parent has not
	if (this && (this->FirstMoveChild() == pChild))
	{
		Assert(!(pChild->MovePrevPeer()));
		this->SetFirstMoveChild(pChild->NextMovePeer());
	}

	// Unlink from siblings...
	if (pChild->MovePrevPeer())
	{
		((C_EngineObjectInternal*)pChild->MovePrevPeer())->SetNextMovePeer(pChild->NextMovePeer());
	}
	if (pChild->NextMovePeer())
	{
		((C_EngineObjectInternal*)pChild->NextMovePeer())->SetMovePrevPeer(pChild->MovePrevPeer());
	}

	((C_EngineObjectInternal*)pChild)->SetNextMovePeer( NULL);
	((C_EngineObjectInternal*)pChild)->SetMovePrevPeer( NULL);
	((C_EngineObjectInternal*)pChild)->SetMoveParent( NULL);
	pChild->GetOuter()->RemoveFromAimEntsList();

	Interp_HierarchyUpdateInterpolationAmounts();
}

void C_EngineObjectInternal::LinkChild(IEngineObjectClient* pChild)
{
	Assert(!pChild->NextMovePeer());
	Assert(!pChild->MovePrevPeer());
	Assert(!pChild->GetMoveParent());
	Assert(this != pChild);

#ifdef _DEBUG
	// Make sure the child isn't already in this list
	IEngineObjectClient* pExistingChild;
	for (pExistingChild = this->FirstMoveChild(); pExistingChild; pExistingChild = pExistingChild->NextMovePeer())
	{
		Assert(pChild != pExistingChild);
	}
#endif

	((C_EngineObjectInternal*)pChild)->SetMovePrevPeer( NULL);
	((C_EngineObjectInternal*)pChild)->SetNextMovePeer( this->FirstMoveChild());
	if (pChild->NextMovePeer())
	{
		((C_EngineObjectInternal*)pChild->NextMovePeer())->SetMovePrevPeer( pChild);
	}
	this->SetFirstMoveChild( pChild);
	((C_EngineObjectInternal*)pChild)->SetMoveParent( this);
	pChild->GetOuter()->AddToAimEntsList();

	Interp_HierarchyUpdateInterpolationAmounts();
}


//-----------------------------------------------------------------------------
// Connects us up to hierarchy
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::HierarchySetParent(IEngineObjectClient* pNewParent)
{
	// NOTE: When this is called, we expect to have a valid
	// local origin, etc. that we received from network daa
	//EHANDLE newParentHandle;
	//newParentHandle.Set( pNewParent );
	if (pNewParent == m_pMoveParent)
		return;

	if (m_pMoveParent)
	{
		m_pMoveParent->UnlinkChild(this);
	}
	if (pNewParent)
	{
		pNewParent->LinkChild(this);
	}

	InvalidatePhysicsRecursive(POSITION_CHANGED | ANGLES_CHANGED | VELOCITY_CHANGED);

#ifdef TF_CLIENT_DLL
	m_bValidatedOwner = false;
#endif
}


//-----------------------------------------------------------------------------
// Unlinks from hierarchy
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetParent(IEngineObjectClient* pParentEntity, int iParentAttachment)
{
	// NOTE: This version is meant to be called *outside* of PostDataUpdate
	// as it assumes the moveparent has a valid handle
	//EHANDLE newParentHandle;
	//newParentHandle.Set( pParentEntity );
	if (pParentEntity == m_pMoveParent)
		return;

	// NOTE: Have to do this before the unlink to ensure local coords are valid
	Vector vecAbsOrigin = GetAbsOrigin();
	QAngle angAbsRotation = GetAbsAngles();
	Vector vecAbsVelocity = GetAbsVelocity();

	// First deal with unlinking
	if (m_pMoveParent)
	{
		m_pMoveParent->UnlinkChild(this);
	}

	if (pParentEntity)
	{
		pParentEntity->LinkChild(this);
	}

	if (!m_pOuter->IsServerEntity())
	{
		SetNetworkMoveParent( pParentEntity);
	}

	m_iParentAttachment = iParentAttachment;

	m_vecAbsOrigin.Init(FLT_MAX, FLT_MAX, FLT_MAX);
	m_angAbsRotation.Init(FLT_MAX, FLT_MAX, FLT_MAX);
	m_vecAbsVelocity.Init(FLT_MAX, FLT_MAX, FLT_MAX);

	SetAbsOrigin(vecAbsOrigin);
	SetAbsAngles(angAbsRotation);
	SetAbsVelocity(vecAbsVelocity);

}


//-----------------------------------------------------------------------------
// Unlinks from hierarchy
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::UnlinkFromHierarchy()
{
	// Clear out links if we're out of the picture...
	if (m_pMoveParent)
	{
		m_pMoveParent->UnlinkChild(this);
	}

	//Adrian: This was causing problems with the local network backdoor with entities coming in and out of the PVS at certain times.
	//This would work fine if a full entity update was coming (caused by certain factors like too many entities entering the pvs at once).
	//but otherwise it would not detect the change on the client (since the server and client shouldn't be out of sync) and the var would not be updated like it should.
	//m_iParentAttachment = 0;

	// unlink also all move children
	C_EngineObjectInternal* pChild = FirstMoveChild();
	while (pChild)
	{
		if (pChild->m_pMoveParent != this)
		{
			Warning("C_BaseEntity::UnlinkFromHierarchy(): Entity has a child with the wrong parent!\n");
			Assert(0);
			UnlinkChild(pChild);
			pChild->UnlinkFromHierarchy();
		}
		else
			pChild->UnlinkFromHierarchy();
		pChild = FirstMoveChild();
	}
}


void C_EngineObjectInternal::OnStoreLastNetworkedValue()
{
	bool bRestore = false;
	Vector savePos;
	QAngle saveAng;

	// Kind of a hack, but we want to latch the actual networked value for origin/angles, not what's sitting in m_vecOrigin in the
	//  ragdoll case where we don't copy it over in MoveToLastNetworkOrigin
	if (m_pOuter->m_nRenderFX == kRenderFxRagdoll && m_pOuter->GetPredictable())
	{
		bRestore = true;
		savePos = GetLocalOrigin();
		saveAng = GetLocalAngles();

		m_pOuter->MoveToLastReceivedPosition(true);
	}

	int c = m_VarMap.m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &m_VarMap.m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;

		int type = watcher->GetType();

		if (type & EXCLUDE_AUTO_LATCH)
			continue;

		watcher->NoteLastNetworkedValue();
	}

	if (bRestore)
	{
		SetLocalOrigin(savePos);
		SetLocalAngles(saveAng);
	}
}

//-----------------------------------------------------------------------------
// Purpose: The animtime is about to be changed in a network update, store off various fields so that
//  we can use them to do blended sequence transitions, etc.
// Input  : *pState - the (mostly) previous state data
//-----------------------------------------------------------------------------

void C_EngineObjectInternal::OnLatchInterpolatedVariables(int flags)
{
	float changetime = GetLastChangeTime(flags);

	bool bUpdateLastNetworkedValue = !(flags & INTERPOLATE_OMIT_UPDATE_LAST_NETWORKED) ? true : false;

	PREDICTION_TRACKVALUECHANGESCOPE_ENTITY(this->m_pOuter, bUpdateLastNetworkedValue ? "latch+net" : "latch");

	int c = m_VarMap.m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &m_VarMap.m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;

		int type = watcher->GetType();

		if (!(type & flags))
			continue;

		if (type & EXCLUDE_AUTO_LATCH)
			continue;

		if (watcher->NoteChanged(changetime, bUpdateLastNetworkedValue))
			e->m_bNeedsToInterpolate = true;
	}

	if (m_pOuter->ShouldInterpolate())
	{
		m_pOuter->AddToInterpolationList();
	}
}

int C_EngineObjectInternal::BaseInterpolatePart1(float& currentTime, Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int& bNoMoreChanges)
{
	// Don't mess with the world!!!
	bNoMoreChanges = 1;


	// These get moved to the parent position automatically
	if (IsFollowingEntity() || !m_pOuter->IsInterpolationEnabled())
	{
		// Assume current origin ( no interpolation )
		m_pOuter->MoveToLastReceivedPosition();
		return INTERPOLATE_STOP;
	}

	if (GetModelPtr()&&!IsUsingClientSideAnimation())
		m_iv_flCycle.SetLooping(IsSequenceLooping(GetSequence()));

	if (m_pOuter->GetPredictable() /*|| IsClientCreated()*/)
	{
		C_BasePlayer* localplayer = C_BasePlayer::GetLocalPlayer();
		if (localplayer && currentTime == gpGlobals->curtime)
		{
			currentTime = localplayer->GetFinalPredictedTime();
			currentTime -= TICK_INTERVAL;
			currentTime += (gpGlobals->interpolation_amount * TICK_INTERVAL);
		}
	}

	oldOrigin = m_vecOrigin;
	oldAngles = m_angRotation;
	oldVel = m_vecVelocity;

	bNoMoreChanges = Interp_Interpolate(currentTime);
	if (cl_interp_all.GetInt() || (m_pOuter->m_EntClientFlags & ENTCLIENTFLAG_ALWAYS_INTERPOLATE))
		bNoMoreChanges = 0;

	return INTERPOLATE_CONTINUE;
}

#if 0
static ConVar cl_watchplayer("cl_watchplayer", "-1", 0);
#endif

void C_EngineObjectInternal::BaseInterpolatePart2(Vector& oldOrigin, QAngle& oldAngles, Vector& oldVel, int nChangeFlags)
{
	if (m_vecOrigin != oldOrigin)
	{
		nChangeFlags |= POSITION_CHANGED;
	}

	if (m_angRotation != oldAngles)
	{
		nChangeFlags |= ANGLES_CHANGED;
	}

	if (m_vecVelocity != oldVel)
	{
		nChangeFlags |= VELOCITY_CHANGED;
	}

	if (nChangeFlags != 0)
	{
		InvalidatePhysicsRecursive(nChangeFlags);
	}

#if 0
	if (index == 1)
	{
		SpewInterpolatedVar(&m_iv_vecOrigin, gpGlobals->curtime, GetInterpolationAmount(LATCH_SIMULATION_VAR), true);
	}
#endif
}

//-----------------------------------------------------------------------------
// These methods recompute local versions as well as set abs versions
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetAbsOrigin(const Vector& absOrigin)
{
	// This is necessary to get the other fields of m_rgflCoordinateFrame ok
	CalcAbsolutePosition();

	if (m_vecAbsOrigin == absOrigin)
		return;

	// All children are invalid, but we are not
	InvalidatePhysicsRecursive(POSITION_CHANGED);
	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	m_vecAbsOrigin = absOrigin;
	MatrixSetColumn(absOrigin, 3, m_rgflCoordinateFrame);

	C_EngineObjectInternal* pMoveParent = GetMoveParent();

	if (!pMoveParent)
	{
		m_vecOrigin = absOrigin;
		return;
	}

	// Moveparent case: transform the abs position into local space
	VectorITransform(absOrigin, pMoveParent->EntityToWorldTransform(), (Vector&)m_vecOrigin);
}

void C_EngineObjectInternal::SetAbsAngles(const QAngle& absAngles)
{
	// This is necessary to get the other fields of m_rgflCoordinateFrame ok
	CalcAbsolutePosition();

	// FIXME: The normalize caused problems in server code like momentary_rot_button that isn't
	//        handling things like +/-180 degrees properly. This should be revisited.
	//QAngle angleNormalize( AngleNormalize( absAngles.x ), AngleNormalize( absAngles.y ), AngleNormalize( absAngles.z ) );

	if (m_angAbsRotation == absAngles)
		return;

	InvalidatePhysicsRecursive(ANGLES_CHANGED);
	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	m_angAbsRotation = absAngles;
	AngleMatrix(absAngles, m_rgflCoordinateFrame);
	MatrixSetColumn(m_vecAbsOrigin, 3, m_rgflCoordinateFrame);

	C_EngineObjectInternal* pMoveParent = GetMoveParent();

	if (!pMoveParent)
	{
		m_angRotation = absAngles;
		return;
	}

	// Moveparent case: we're aligned with the move parent
	if (m_angAbsRotation == pMoveParent->GetAbsAngles())
	{
		m_angRotation.Init();
	}
	else
	{
		// Moveparent case: transform the abs transform into local space
		matrix3x4_t worldToParent, localMatrix;
		MatrixInvert(pMoveParent->EntityToWorldTransform(), worldToParent);
		ConcatTransforms(worldToParent, m_rgflCoordinateFrame, localMatrix);
		MatrixAngles(localMatrix, (QAngle&)m_angRotation);
	}
}

void C_EngineObjectInternal::SetAbsVelocity(const Vector& vecAbsVelocity)
{
	if (m_vecAbsVelocity == vecAbsVelocity)
		return;

	// The abs velocity won't be dirty since we're setting it here
	InvalidatePhysicsRecursive(VELOCITY_CHANGED);
	m_iEFlags &= ~EFL_DIRTY_ABSVELOCITY;

	m_vecAbsVelocity = vecAbsVelocity;

	C_EngineObjectInternal* pMoveParent = GetMoveParent();

	if (!pMoveParent)
	{
		m_vecVelocity = vecAbsVelocity;
		return;
	}

	// First subtract out the parent's abs velocity to get a relative
	// velocity measured in world space
	Vector relVelocity;
	VectorSubtract(vecAbsVelocity, pMoveParent->GetAbsVelocity(), relVelocity);

	// Transform velocity into parent space
	VectorIRotate(relVelocity, pMoveParent->EntityToWorldTransform(), m_vecVelocity);
}

// Prevent these for now until hierarchy is properly networked
const Vector& C_EngineObjectInternal::GetLocalOrigin(void) const
{
	return m_vecOrigin;
}

const vec_t C_EngineObjectInternal::GetLocalOriginDim(int iDim) const
{
	return m_vecOrigin[iDim];
}

// Prevent these for now until hierarchy is properly networked
void C_EngineObjectInternal::SetLocalOrigin(const Vector& origin)
{
	if (m_vecOrigin != origin)
	{
		InvalidatePhysicsRecursive(POSITION_CHANGED);
		m_vecOrigin = origin;
	}
}

void C_EngineObjectInternal::SetLocalOriginDim(int iDim, vec_t flValue)
{
	if (m_vecOrigin[iDim] != flValue)
	{
		InvalidatePhysicsRecursive(POSITION_CHANGED);
		m_vecOrigin[iDim] = flValue;
	}
}


// Prevent these for now until hierarchy is properly networked
const QAngle& C_EngineObjectInternal::GetLocalAngles(void) const
{
	return m_angRotation;
}

const vec_t C_EngineObjectInternal::GetLocalAnglesDim(int iDim) const
{
	return m_angRotation[iDim];
}

// Prevent these for now until hierarchy is properly networked
void C_EngineObjectInternal::SetLocalAngles(const QAngle& angles)
{
	// NOTE: The angle normalize is a little expensive, but we can save
	// a bunch of time in interpolation if we don't have to invalidate everything
	// and sometimes it's off by a normalization amount

	// FIXME: The normalize caused problems in server code like momentary_rot_button that isn't
	//        handling things like +/-180 degrees properly. This should be revisited.
	//QAngle angleNormalize( AngleNormalize( angles.x ), AngleNormalize( angles.y ), AngleNormalize( angles.z ) );

	if (m_angRotation != angles)
	{
		// This will cause the velocities of all children to need recomputation
		InvalidatePhysicsRecursive(ANGLES_CHANGED);
		m_angRotation = angles;
	}
}

void C_EngineObjectInternal::SetLocalAnglesDim(int iDim, vec_t flValue)
{
	flValue = AngleNormalize(flValue);
	if (m_angRotation[iDim] != flValue)
	{
		// This will cause the velocities of all children to need recomputation
		InvalidatePhysicsRecursive(ANGLES_CHANGED);
		m_angRotation[iDim] = flValue;
	}
}

const Vector& C_EngineObjectInternal::GetAbsVelocity()
{
	Assert(C_EngineObjectInternal::s_bAbsQueriesValid);
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	return m_vecAbsVelocity;
}

const Vector& C_EngineObjectInternal::GetAbsVelocity() const
{
	Assert(C_EngineObjectInternal::s_bAbsQueriesValid);
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	return m_vecAbsVelocity;
}

//-----------------------------------------------------------------------------
// Velocity
//-----------------------------------------------------------------------------
//Vector& C_EngineObjectInternal::GetLocalVelocity()
//{
//	return m_vecVelocity;
//}

//-----------------------------------------------------------------------------
// Velocity
//-----------------------------------------------------------------------------
const Vector& C_EngineObjectInternal::GetLocalVelocity() const
{
	return m_vecVelocity;
}

void C_EngineObjectInternal::SetLocalVelocity(const Vector& vecVelocity)
{
	if (m_vecVelocity != vecVelocity)
	{
		InvalidatePhysicsRecursive(VELOCITY_CHANGED);
		m_vecVelocity = vecVelocity;
	}
}

void C_EngineObjectInternal::ResetRgflCoordinateFrame() {
	MatrixSetColumn(m_vecAbsOrigin, 3, m_rgflCoordinateFrame);
}

//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
matrix3x4_t& C_EngineObjectInternal::EntityToWorldTransform()
{
	Assert(C_EngineObjectInternal::s_bAbsQueriesValid);
	CalcAbsolutePosition();
	return m_rgflCoordinateFrame;
}

const matrix3x4_t& C_EngineObjectInternal::EntityToWorldTransform() const
{
	Assert(C_EngineObjectInternal::s_bAbsQueriesValid);
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsolutePosition();
	return m_rgflCoordinateFrame;
}


const matrix3x4_t& C_EngineObjectInternal::GetParentToWorldTransform(matrix3x4_t& tempMatrix)
{
	C_EngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		Assert(false);
		SetIdentityMatrix(tempMatrix);
		return tempMatrix;
	}

	if (GetParentAttachment() != 0)
	{
		Vector vOrigin;
		QAngle vAngles;
		if (pMoveParent->m_pOuter->GetAttachment(GetParentAttachment(), vOrigin, vAngles))
		{
			AngleMatrix(vAngles, vOrigin, tempMatrix);
			return tempMatrix;
		}
	}

	// If we fall through to here, then just use the move parent's abs origin and angles.
	return pMoveParent->EntityToWorldTransform();
}


//-----------------------------------------------------------------------------
// Purpose: Calculates the absolute position of an edict in the world
//			assumes the parent's absolute origin has already been calculated
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::CalcAbsolutePosition()
{
	// There are periods of time where we're gonna have to live with the
	// fact that we're in an indeterminant state and abs queries (which
	// shouldn't be happening at all; I have assertions for those), will
	// just have to accept stale data.
	if (!s_bAbsRecomputationEnabled)
		return;

	// FIXME: Recompute absbox!!!
	if ((m_iEFlags & EFL_DIRTY_ABSTRANSFORM) == 0)
	{
		// quick check to make sure we really don't need an update
		// Assert( m_pMoveParent || m_vecAbsOrigin == GetLocalOrigin() );
		return;
	}

	AUTO_LOCK(m_CalcAbsolutePositionMutex);

	if ((m_iEFlags & EFL_DIRTY_ABSTRANSFORM) == 0) // need second check in event another thread grabbed mutex and did the calculation
	{
		return;
	}

	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	if (!m_pMoveParent)
	{
		// Construct the entity-to-world matrix
		// Start with making an entity-to-parent matrix
		AngleMatrix(GetLocalAngles(), GetLocalOrigin(), m_rgflCoordinateFrame);
		m_vecAbsOrigin = GetLocalOrigin();
		m_angAbsRotation = GetLocalAngles();
		NormalizeAngles(m_angAbsRotation);
		return;
	}

	if (IsEffectActive(EF_BONEMERGE))
	{
		m_pOuter->MoveToAimEnt();
		return;
	}

	// Construct the entity-to-world matrix
	// Start with making an entity-to-parent matrix
	matrix3x4_t matEntityToParent;
	AngleMatrix(GetLocalAngles(), matEntityToParent);
	MatrixSetColumn(GetLocalOrigin(), 3, matEntityToParent);

	// concatenate with our parent's transform
	matrix3x4_t scratchMatrix;
	ConcatTransforms(GetParentToWorldTransform(scratchMatrix), matEntityToParent, m_rgflCoordinateFrame);

	// pull our absolute position out of the matrix
	MatrixGetColumn(m_rgflCoordinateFrame, 3, m_vecAbsOrigin);

	// if we have any angles, we have to extract our absolute angles from our matrix
	if (m_angRotation == vec3_angle && GetParentAttachment() == 0)
	{
		// just copy our parent's absolute angles
		VectorCopy(m_pMoveParent->GetAbsAngles(), m_angAbsRotation);
	}
	else
	{
		MatrixAngles(m_rgflCoordinateFrame, m_angAbsRotation);
	}

	// This is necessary because it's possible that our moveparent's CalculateIKLocks will trigger its move children 
	// (ie: this entity) to call GetAbsOrigin(), and they'll use the moveparent's OLD bone transforms to get their attachments
	// since the moveparent is right in the middle of setting up new transforms. 
	//
	// So here, we keep our absorigin invalidated. It means we're returning an origin that is a frame old to CalculateIKLocks,
	// but we'll still render with the right origin.
	if (GetParentAttachment() != 0 && (m_pMoveParent->GetEFlags() & EFL_SETTING_UP_BONES))
	{
		m_iEFlags |= EFL_DIRTY_ABSTRANSFORM;
	}
}

void C_EngineObjectInternal::CalcAbsoluteVelocity()
{
	if ((m_iEFlags & EFL_DIRTY_ABSVELOCITY) == 0)
		return;

	AUTO_LOCK(m_CalcAbsoluteVelocityMutex);

	if ((m_iEFlags & EFL_DIRTY_ABSVELOCITY) == 0) // need second check in event another thread grabbed mutex and did the calculation
	{
		return;
	}

	m_iEFlags &= ~EFL_DIRTY_ABSVELOCITY;

	C_EngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		m_vecAbsVelocity = m_vecVelocity;
		return;
	}

	VectorRotate(m_vecVelocity, pMoveParent->EntityToWorldTransform(), m_vecAbsVelocity);


	// Add in the attachments velocity if it exists
	if (GetParentAttachment() != 0)
	{
		Vector vOriginVel;
		Quaternion vAngleVel;
		if (pMoveParent->GetOuter()->GetAttachmentVelocity(GetParentAttachment(), vOriginVel, vAngleVel))
		{
			m_vecAbsVelocity += vOriginVel;
			return;
		}
	}

	// Now add in the parent abs velocity
	m_vecAbsVelocity += pMoveParent->GetAbsVelocity();
}

//-----------------------------------------------------------------------------
// Computes the abs position of a point specified in local space
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::ComputeAbsPosition(const Vector& vecLocalPosition, Vector* pAbsPosition)
{
	C_EngineObjectInternal* pMoveParent = GetMoveParent();
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
void C_EngineObjectInternal::ComputeAbsDirection(const Vector& vecLocalDirection, Vector* pAbsDirection)
{
	C_EngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		*pAbsDirection = vecLocalDirection;
	}
	else
	{
		VectorRotate(vecLocalDirection, pMoveParent->EntityToWorldTransform(), *pAbsDirection);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Transfer data for intermediate frame to current entity
// Input  : copyintermediate - 
//			last_predicted - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::PreEntityPacketReceived(int commands_acknowledged)
{
#if !defined( NO_ENTITY_PREDICTION )
	// Don't need to copy intermediate data if server did ack any new commands
	bool copyintermediate = (commands_acknowledged > 0) ? true : false;

	Assert(m_pOuter->GetPredictable());
	Assert(cl_predict->GetInt());

	// First copy in any intermediate predicted data for non-networked fields
	if (copyintermediate)
	{
		RestoreData("PreEntityPacketReceived", commands_acknowledged - 1, PC_NON_NETWORKED_ONLY);
		RestoreData("PreEntityPacketReceived", SLOT_ORIGINALDATA, PC_NETWORKED_ONLY);
	}
	else
	{
		RestoreData("PreEntityPacketReceived(no commands ack)", SLOT_ORIGINALDATA, PC_EVERYTHING);
	}

	// At this point the entity has original network data restored as of the last time the 
	// networking was updated, and it has any intermediate predicted values properly copied over
	// Unpacked and OnDataChanged will fill in any changed, networked fields.

	// That networked data will be copied forward into the starting slot for the next prediction round
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Called every time PreEntityPacket received is called
//  copy any networked data into original_state
// Input  : errorcheck - 
//			last_predicted - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::PostEntityPacketReceived(void)
{
#if !defined( NO_ENTITY_PREDICTION )
	Assert(m_pOuter->GetPredictable());
	Assert(cl_predict->GetInt());

	// Always mark as changed
	AddDataChangeEvent(this, DATA_UPDATE_DATATABLE_CHANGED, &m_DataChangeEventRef);

	// Save networked fields into "original data" store
	SaveData("PostEntityPacketReceived", SLOT_ORIGINALDATA, PC_NETWORKED_ONLY);
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Called once per frame after all updating is done
// Input  : errorcheck - 
//			last_predicted - 
//-----------------------------------------------------------------------------
bool C_EngineObjectInternal::PostNetworkDataReceived(int commands_acknowledged)
{
	bool haderrors = false;
#if !defined( NO_ENTITY_PREDICTION )
	Assert(m_pOuter->GetPredictable());

	bool errorcheck = (commands_acknowledged > 0) ? true : false;

	// Store network data into post networking pristine state slot (slot 64) 
	SaveData("PostNetworkDataReceived", SLOT_ORIGINALDATA, PC_EVERYTHING);

	// Show any networked fields that are different
	bool showthis = cl_showerror.GetInt() >= 2;

	if (cl_showerror.GetInt() < 0)
	{
		if (m_pOuter->entindex() == -cl_showerror.GetInt())
		{
			showthis = true;
		}
		else
		{
			showthis = false;
		}
	}

	if (errorcheck)
	{
		{
			void* predicted_state_data = GetPredictedFrame(commands_acknowledged - 1);
			Assert(predicted_state_data);
			const void* original_state_data = GetOriginalNetworkDataObject();
			Assert(original_state_data);

			bool counterrors = true;
			bool reporterrors = showthis;
			bool copydata = false;

			CPredictionCopy errorCheckHelper(PC_NETWORKED_ONLY,
				predicted_state_data, TD_OFFSET_PACKED,
				original_state_data, TD_OFFSET_PACKED,
				counterrors, reporterrors, copydata);
			// Suppress debugging output
			int ecount = errorCheckHelper.TransferData("", -1, this->GetPredDescMap());
			if (ecount > 0)
			{
				haderrors = true;
				//	Msg( "%i errors %i on entity %i %s\n", gpGlobals->tickcount, ecount, index, IsClientCreated() ? "true" : "false" );
			}
		}
		{
			void* outer_predicted_state_data = GetOuterPredictedFrame(commands_acknowledged - 1);
			Assert(outer_predicted_state_data);
			const void* outer_original_state_data = GetOuterOriginalNetworkDataObject();
			Assert(outer_original_state_data);

			bool counterrors = true;
			bool reporterrors = showthis;
			bool copydata = false;

			CPredictionCopy outerErrorCheckHelper(PC_NETWORKED_ONLY,
				outer_predicted_state_data, TD_OFFSET_PACKED,
				outer_original_state_data, TD_OFFSET_PACKED,
				counterrors, reporterrors, copydata);
			// Suppress debugging output
			int outerEcount = outerErrorCheckHelper.TransferData("", -1, m_pOuter->GetPredDescMap());
			if (outerEcount > 0)
			{
				haderrors = true;
				//	Msg( "%i errors %i on entity %i %s\n", gpGlobals->tickcount, ecount, index, IsClientCreated() ? "true" : "false" );
			}
		}
	}
#endif
	return haderrors;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_EngineObjectInternal::IsIntermediateDataAllocated(void) const
{
#if !defined( NO_ENTITY_PREDICTION )
	return m_pOriginalData != NULL ? true : false;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::AllocateIntermediateData(void)
{
#if !defined( NO_ENTITY_PREDICTION )
	if (m_pOriginalData)
		return;
	size_t allocsize = this->GetPredDescMap()->GetIntermediateDataSize();
	Assert(allocsize > 0);
	m_pOriginalData = new unsigned char[allocsize];
	Q_memset(m_pOriginalData, 0, allocsize);

	size_t outerallocsize = m_pOuter->GetPredDescMap()->GetIntermediateDataSize();
	Assert(outerallocsize > 0);
	m_pOuterOriginalData = new unsigned char[outerallocsize];
	Q_memset(m_pOuterOriginalData, 0, outerallocsize);
	for (int i = 0; i < MULTIPLAYER_BACKUP; i++)
	{
		m_pIntermediateData[i] = new unsigned char[allocsize];
		Q_memset(m_pIntermediateData[i], 0, allocsize);
		m_pOuterIntermediateData[i] = new unsigned char[outerallocsize];
		Q_memset(m_pOuterIntermediateData[i], 0, outerallocsize);
	}

	m_nIntermediateDataCount = 0;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::DestroyIntermediateData(void)
{
#if !defined( NO_ENTITY_PREDICTION )
	if (!m_pOriginalData)
		return;
	for (int i = 0; i < MULTIPLAYER_BACKUP; i++)
	{
		delete[] m_pIntermediateData[i];
		m_pIntermediateData[i] = NULL;
		delete[] m_pOuterIntermediateData[i];
		m_pOuterIntermediateData[i] = NULL;
	}
	delete[] m_pOriginalData;
	m_pOriginalData = NULL;
	delete[] m_pOuterOriginalData;
	m_pOuterOriginalData = NULL;

	m_nIntermediateDataCount = 0;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : slots_to_remove - 
//			number_of_commands_run - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::ShiftIntermediateDataForward(int slots_to_remove, int number_of_commands_run)
{
#if !defined( NO_ENTITY_PREDICTION )
	Assert(m_pIntermediateData);
	if (!m_pIntermediateData)
		return;

	Assert(number_of_commands_run >= slots_to_remove);

	// Just moving pointers, yeah
	CUtlVector< unsigned char* > saved;
	CUtlVector< unsigned char* > outerSaved;

	// Remember first slots
	int i = 0;
	for (; i < slots_to_remove; i++)
	{
		saved.AddToTail(m_pIntermediateData[i]);
		outerSaved.AddToTail(m_pOuterIntermediateData[i]);
	}

	// Move rest of slots forward up to last slot
	for (; i < number_of_commands_run; i++)
	{
		m_pIntermediateData[i - slots_to_remove] = m_pIntermediateData[i];
		m_pOuterIntermediateData[i - slots_to_remove] = m_pOuterIntermediateData[i];
	}

	// Put remembered slots onto end
	for (i = 0; i < slots_to_remove; i++)
	{
		int slot = number_of_commands_run - slots_to_remove + i;

		m_pIntermediateData[slot] = saved[i];
		m_pOuterIntermediateData[slot] = outerSaved[i];
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : framenumber - 
//-----------------------------------------------------------------------------
void* C_EngineObjectInternal::GetPredictedFrame(int framenumber)
{
#if !defined( NO_ENTITY_PREDICTION )
	Assert(framenumber >= 0);

	if (!m_pOriginalData)
	{
		Assert(0);
		return NULL;
	}
	return (void*)m_pIntermediateData[framenumber % MULTIPLAYER_BACKUP];
#else
	return NULL;
#endif
}

void* C_EngineObjectInternal::GetOuterPredictedFrame(int framenumber)
{
#if !defined( NO_ENTITY_PREDICTION )
	Assert(framenumber >= 0);

	if (!m_pOuterOriginalData)
	{
		Assert(0);
		return NULL;
	}
	return (void*)m_pOuterIntermediateData[framenumber % MULTIPLAYER_BACKUP];
#else
	return NULL;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void* C_EngineObjectInternal::GetOriginalNetworkDataObject(void)
{
#if !defined( NO_ENTITY_PREDICTION )
	if (!m_pOriginalData)
	{
		Assert(0);
		return NULL;
	}
	return (void*)m_pOriginalData;
#else
	return NULL;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void* C_EngineObjectInternal::GetOuterOriginalNetworkDataObject(void)
{
#if !defined( NO_ENTITY_PREDICTION )
	if (!m_pOuterOriginalData)
	{
		Assert(0);
		return NULL;
	}
	return (void*)m_pOuterOriginalData;
#else
	return NULL;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Copy from this entity into one of the save slots (original or intermediate)
// Input  : slot - 
//			type - 
//			false - 
//			false - 
//			true - 
//			false - 
//			NULL - 
// Output : int
//-----------------------------------------------------------------------------
int C_EngineObjectInternal::SaveData(const char* context, int slot, int type)
{
#if !defined( NO_ENTITY_PREDICTION )
	VPROF("C_BaseEntity::SaveData");

	void* dest = (slot == SLOT_ORIGINALDATA) ? GetOriginalNetworkDataObject() : GetPredictedFrame(slot);
	Assert(dest);
	void* outerDest = (slot == SLOT_ORIGINALDATA) ? GetOuterOriginalNetworkDataObject() : GetOuterPredictedFrame(slot);
	Assert(outerDest);

	char sz[64];
	sz[0] = 0;
	// don't build debug strings per entity per frame, unless we are watching the entity
	static ConVarRef pwatchent("pwatchent");
	if (pwatchent.GetInt() == m_pOuter->entindex())
	{
		if (slot == SLOT_ORIGINALDATA)
		{
			Q_snprintf(sz, sizeof(sz), "%s SaveData(original)", context);
		}
		else
		{
			Q_snprintf(sz, sizeof(sz), "%s SaveData(slot %02i)", context, slot);
		}
	}

	if (slot != SLOT_ORIGINALDATA)
	{
		// Remember high water mark so that we can detect below if we are reading from a slot not yet predicted into...
		m_nIntermediateDataCount = slot;
	}

	CPredictionCopy copyHelper(type, dest, TD_OFFSET_PACKED, this, TD_OFFSET_NORMAL);
	int error_count = copyHelper.TransferData(sz, m_pOuter->entindex(), this->GetPredDescMap());
	CPredictionCopy outerCopyHelper(type, outerDest, TD_OFFSET_PACKED, m_pOuter, TD_OFFSET_NORMAL);
	int outerError_count = outerCopyHelper.TransferData(sz, m_pOuter->entindex(), m_pOuter->GetPredDescMap());
	return error_count + outerError_count;
#else
	return 0;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Restore data from specified slot into current entity
// Input  : slot - 
//			type - 
//			false - 
//			false - 
//			true - 
//			false - 
//			NULL - 
// Output : int
//-----------------------------------------------------------------------------
int C_EngineObjectInternal::RestoreData(const char* context, int slot, int type)
{
	IStudioHdr* pHdr = GetModelPtr();
#if !defined( NO_ENTITY_PREDICTION )
	VPROF("C_BaseEntity::RestoreData");

	const void* src = (slot == SLOT_ORIGINALDATA) ? GetOriginalNetworkDataObject() : GetPredictedFrame(slot);
	Assert(src);
	const void* outerSrc = (slot == SLOT_ORIGINALDATA) ? GetOuterOriginalNetworkDataObject() : GetOuterPredictedFrame(slot);
	Assert(outerSrc);

	// This assert will fire if the server ack'd a CUserCmd which we hadn't predicted yet...
	// In that case, we'd be comparing "old" data from this "unused" slot with the networked data and reporting all kinds of prediction errors possibly.
	Assert(slot == SLOT_ORIGINALDATA || slot <= m_nIntermediateDataCount);

	char sz[64];
	sz[0] = 0;
	// don't build debug strings per entity per frame, unless we are watching the entity
	static ConVarRef pwatchent("pwatchent");
	if (pwatchent.GetInt() == m_pOuter->entindex())
	{
		if (slot == SLOT_ORIGINALDATA)
		{
			Q_snprintf(sz, sizeof(sz), "%s RestoreData(original)", context);
		}
		else
		{
			Q_snprintf(sz, sizeof(sz), "%s RestoreData(slot %02i)", context, slot);
		}
	}

	// some flags shouldn't be predicted - as we find them, add them to the savedEFlagsMask
	const int savedEFlagsMask = EFL_DIRTY_SHADOWUPDATE;
	int savedEFlags = GetEFlags() & savedEFlagsMask;

	// model index needs to be set manually for dynamic model refcounting purposes
	int oldModelIndex = m_nModelIndex;

	CPredictionCopy copyHelper(type, this, TD_OFFSET_NORMAL, src, TD_OFFSET_PACKED);
	int error_count = copyHelper.TransferData(sz, m_pOuter->entindex(), this->GetPredDescMap());
	CPredictionCopy outerCopyHelper(type, m_pOuter, TD_OFFSET_NORMAL, outerSrc, TD_OFFSET_PACKED);
	int outerError_count = outerCopyHelper.TransferData(sz, m_pOuter->entindex(), m_pOuter->GetPredDescMap());

	// set non-predicting flags back to their prior state
	RemoveEFlags(savedEFlagsMask);
	AddEFlags(savedEFlags);

	// restore original model index and change via SetModelIndex
	int newModelIndex = m_nModelIndex;
	m_nModelIndex = oldModelIndex;
	int overrideModelIndex = m_pOuter->CalcOverrideModelIndex();
	if (overrideModelIndex != -1)
		newModelIndex = overrideModelIndex;
	if (oldModelIndex != newModelIndex)
	{
		MDLCACHE_CRITICAL_SECTION(); // ???
		SetModelIndex(newModelIndex);
	}

	// HACK Force recomputation of origin
	InvalidatePhysicsRecursive(POSITION_CHANGED | ANGLES_CHANGED | VELOCITY_CHANGED);
	m_pOuter->OnPostRestoreData();

	return error_count + outerError_count;
#else
	return 0;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Determine approximate velocity based on updates from server
// Input  : vel - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::EstimateAbsVelocity(Vector& vel)
{
	if (this->m_pOuter == C_BasePlayer::GetLocalPlayer())
	{
		// This is interpolated and networked
		vel = GetAbsVelocity();
		return;
	}

	CInterpolationContext context;
	context.EnableExtrapolation(true);
	m_iv_vecOrigin.GetDerivative_SmoothVelocity(&vel, gpGlobals->curtime);
}

void C_EngineObjectInternal::Interp_Reset()
{
	PREDICTION_TRACKVALUECHANGESCOPE_ENTITY(this->m_pOuter, "reset");
	int c = m_VarMap.m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &m_VarMap.m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;

		watcher->Reset();
	}
}

const Vector& C_EngineObjectInternal::GetPrevLocalOrigin() const
{
	return m_iv_vecOrigin.GetPrev();
}

const QAngle& C_EngineObjectInternal::GetPrevLocalAngles() const
{
	return m_iv_angRotation.GetPrev();
}

void C_EngineObjectInternal::AddVar(IInterpolatedVar* watcher, bool bSetup)
{
	// Only add it if it hasn't been added yet.
	bool bAddIt = true;
	for (int i = 0; i < m_VarMap.m_Entries.Count(); i++)
	{
		if (m_VarMap.m_Entries[i].watcher == watcher)
		{
			if ((m_VarMap.m_Entries[i].type & EXCLUDE_AUTO_INTERPOLATE) != (watcher->GetType() & EXCLUDE_AUTO_INTERPOLATE))
			{
				// Its interpolation mode changed, so get rid of it and re-add it.
				RemoveVar(m_VarMap.m_Entries[i].watcher, true);
			}
			else
			{
				// They're adding something that's already there. No need to re-add it.
				bAddIt = false;
			}

			break;
		}
	}

	if (bAddIt)
	{
		// watchers must have a debug name set
		Assert(watcher->GetDebugName() != NULL);

		VarMapEntry_t map;
		//map.data = data;
		map.watcher = watcher;
		map.type = watcher->GetType();
		map.m_bNeedsToInterpolate = true;
		if (map.type & EXCLUDE_AUTO_INTERPOLATE)
		{
			m_VarMap.m_Entries.AddToTail(map);
		}
		else
		{
			m_VarMap.m_Entries.AddToHead(map);
			++m_VarMap.m_nInterpolatedEntries;
		}
	}

	if (bSetup)
	{
		//watcher->Setup(data, type);
		watcher->SetInterpolationAmount(m_pOuter->GetInterpolationAmount(watcher->GetType()));
	}
}


void C_EngineObjectInternal::RemoveVar(IInterpolatedVar* watcher, bool bAssert)
{
	for (int i = 0; i < m_VarMap.m_Entries.Count(); i++)
	{
		if (m_VarMap.m_Entries[i].watcher == watcher)
		{
			if (!(m_VarMap.m_Entries[i].type & EXCLUDE_AUTO_INTERPOLATE))
				--m_VarMap.m_nInterpolatedEntries;

			m_VarMap.m_Entries.Remove(i);
			return;
		}
	}
	if (bAssert)
	{
		Assert(!"RemoveVar");
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : check - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetCheckUntouch(bool check)
{
	// Invalidate touchstamp
	if (check)
	{
		touchStamp++;
		AddEFlags(EFL_CHECK_UNTOUCH);
	}
	else
	{
		RemoveEFlags(EFL_CHECK_UNTOUCH);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_EngineObjectInternal::GetCheckUntouch() const
{
	return IsEFlagSet(EFL_CHECK_UNTOUCH);
}

bool C_EngineObjectInternal::HasDataObjectType(int type) const
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	return (m_fDataObjectTypes & (1 << type)) ? true : false;
}

void C_EngineObjectInternal::AddDataObjectType(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	m_fDataObjectTypes |= (1 << type);
}

void C_EngineObjectInternal::RemoveDataObjectType(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	m_fDataObjectTypes &= ~(1 << type);
}

void* C_EngineObjectInternal::GetDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	if (!HasDataObjectType(type))
		return NULL;
	return ClientEntityList().GetDataObject(type, m_pOuter);
}

void* C_EngineObjectInternal::CreateDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	AddDataObjectType(type);
	return ClientEntityList().CreateDataObject(type, m_pOuter);
}

void C_EngineObjectInternal::DestroyDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	if (!HasDataObjectType(type))
		return;
	ClientEntityList().DestroyDataObject(type, m_pOuter);
	RemoveDataObjectType(type);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::DestroyAllDataObjects(void)
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
void C_EngineObjectInternal::InvalidatePhysicsRecursive(int nChangeFlags)
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
//		GetEngineObject()->MarkPVSInformationDirty();
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

	for (C_EngineObjectInternal* pChild = FirstMoveChild(); pChild; pChild = pChild->NextMovePeer())
	{
		// If this is due to the parent animating, only invalidate children that are parented to an attachment
		// Entities that are following also access attachments points on parents and must be invalidated.
		if (bOnlyDueToAttachment)
		{
			if ((pChild->GetParentAttachment() == 0) && !pChild->IsFollowingEntity())
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

const trace_t& C_EngineObjectInternal::GetTouchTrace(void)
{
	return g_TouchTrace;
}

//-----------------------------------------------------------------------------
// Purpose: Two entities have touched, so run their touch functions
// Input  : *other - 
//			*ptrace - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::PhysicsImpact(IEngineObjectClient* other, trace_t& trace)
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

//-----------------------------------------------------------------------------
// Purpose: Marks the fact that two edicts are in contact
// Input  : *other - other entity
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::PhysicsMarkEntitiesAsTouching(IEngineObjectClient* other, trace_t& trace)
{
	g_TouchTrace = trace;
	PhysicsMarkEntityAsTouched(other);
	other->PhysicsMarkEntityAsTouched(this);
}

void C_EngineObjectInternal::PhysicsMarkEntitiesAsTouchingEventDriven(IEngineObjectClient* other, trace_t& trace)
{
	g_TouchTrace = trace;
	g_TouchTrace.m_pEnt = other->GetOuter();

	clienttouchlink_t* link;
	link = this->PhysicsMarkEntityAsTouched(other);
	if (link)
	{
		// mark these links as event driven so they aren't untouched the next frame
		// when the physics doesn't refresh them
		link->touchStamp = TOUCHSTAMP_EVENT_DRIVEN;
	}
	g_TouchTrace.m_pEnt = this->m_pOuter;
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
clienttouchlink_t* C_EngineObjectInternal::PhysicsMarkEntityAsTouched(IEngineObjectClient* other)
{
	clienttouchlink_t* link;

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
	clienttouchlink_t* root = (clienttouchlink_t*)GetDataObject(TOUCHLINK);
	if (root)
	{
		for (link = root->nextLink; link != root; link = link->nextLink)
		{
			if (link->entityTouched == other->GetOuter())
			{
				// update stamp
				link->touchStamp = GetTouchStamp();

				if (!CClientEntityList<C_BaseEntity>::sm_bDisableTouchFuncs)
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
		root = (clienttouchlink_t*)CreateDataObject(TOUCHLINK);
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
		if (!CClientEntityList<C_BaseEntity>::sm_bDisableTouchFuncs)
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
void C_EngineObjectInternal::PhysicsTouch(IEngineObjectClient* pentOther)
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
void C_EngineObjectInternal::PhysicsStartTouch(IEngineObjectClient* pentOther)
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
bool C_EngineObjectInternal::IsCurrentlyTouching(void) const
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
void C_EngineObjectInternal::PhysicsCheckForEntityUntouch(void)
{
	//Assert( g_pNextLink == NULL );

	clienttouchlink_t* link, * nextLink;

	clienttouchlink_t* root = (clienttouchlink_t*)this->GetDataObject(TOUCHLINK);
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
				PhysicsTouch(link->entityTouched->GetEngineObject());
			}
			else
			{
				// check to see if the touch stamp is up to date
				if (link->touchStamp != this->GetTouchStamp())
				{
					// stamp is out of data, so entities are no longer touching
					// remove self from other entities touch list
					link->entityTouched->GetEngineObject()->PhysicsNotifyOtherOfUntouch(this);

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
void C_EngineObjectInternal::PhysicsNotifyOtherOfUntouch(IEngineObjectClient* ent)
{
	// loop through ed's touch list, looking for the notifier
	// remove and call untouch if found
	clienttouchlink_t* root = (clienttouchlink_t*)this->GetDataObject(TOUCHLINK);
	if (root)
	{
		clienttouchlink_t* link = root->nextLink;
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
void C_EngineObjectInternal::PhysicsRemoveTouchedList()
{
#ifdef PORTAL
	CPortalTouchScope scope;
#endif

	clienttouchlink_t* link, * nextLink;

	clienttouchlink_t* root = (clienttouchlink_t*)this->GetDataObject(TOUCHLINK);
	if (root)
	{
		link = root->nextLink;
		bool saveCleanup = g_bCleanupDatObject;
		g_bCleanupDatObject = false;
		while (link && link != root)
		{
			nextLink = link->nextLink;

			// notify the other entity that this ent has gone away
			link->entityTouched->GetEngineObject()->PhysicsNotifyOtherOfUntouch(this);

			// kill it
			if (DebugTouchlinks())
				Msg("remove 0x%p: %s-%s (%d-%d) [%d in play, %d max]\n", link, this->m_pOuter->GetDebugName(), link->entityTouched->GetDebugName(), this->entindex(), link->entityTouched->entindex(), linksallocated, g_EdictTouchLinks.PeakCount());
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
void C_EngineObjectInternal::PhysicsRemoveToucher(clienttouchlink_t* link)
{
	// Every start Touch gets a corresponding end touch
	if ((link->flags & FTOUCHLINK_START_TOUCH) &&
		link->entityTouched != NULL)
	{
		this->m_pOuter->EndTouch(link->entityTouched);
	}

	link->nextLink->prevLink = link->prevLink;
	link->prevLink->nextLink = link->nextLink;

	if (DebugTouchlinks())
		Msg("remove 0x%p: %s-%s (%d-%d) [%d in play, %d max]\n", link, link->entityTouched->GetDebugName(), this->m_pOuter->GetDebugName(), link->entityTouched->entindex(), this->entindex(), linksallocated, g_EdictTouchLinks.PeakCount());
	FreeTouchLink(link);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *other - 
// Output : groundlink_t
//-----------------------------------------------------------------------------
clientgroundlink_t* C_EngineObjectInternal::AddEntityToGroundList(IEngineObjectClient* other)
{
	clientgroundlink_t* link;

	if (this == other)
		return NULL;

	// check if the edict is already in the list
	clientgroundlink_t* root = (clientgroundlink_t*)GetDataObject(GROUNDLINK);
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
		root = (clientgroundlink_t*)CreateDataObject(GROUNDLINK);
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
void C_EngineObjectInternal::PhysicsStartGroundContact(IEngineObjectClient* pentOther)
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
void C_EngineObjectInternal::PhysicsNotifyOtherOfGroundRemoval(IEngineObjectClient* ent)
{
	// loop through ed's touch list, looking for the notifier
	// remove and call untouch if found
	clientgroundlink_t* root = (clientgroundlink_t*)this->GetDataObject(GROUNDLINK);
	if (root)
	{
		clientgroundlink_t* link = root->nextLink;
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
void C_EngineObjectInternal::PhysicsRemoveGround(clientgroundlink_t* link)
{
	// Every start Touch gets a corresponding end touch
	if (link->entity != INVALID_EHANDLE_INDEX)
	{
		C_BaseEntity* linkEntity = (C_BaseEntity*)ClientEntityList().GetClientEntityFromHandle(link->entity);
		C_BaseEntity* otherEntity = this->m_pOuter;
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
void C_EngineObjectInternal::PhysicsRemoveGroundList()
{
	clientgroundlink_t* link, * nextLink;

	clientgroundlink_t* root = (clientgroundlink_t*)this->GetDataObject(GROUNDLINK);
	if (root)
	{
		link = root->nextLink;
		while (link && link != root)
		{
			nextLink = link->nextLink;

			// notify the other entity that this ent has gone away
			C_BaseEntity* pEntity = ((C_BaseEntity*)ClientEntityList().GetClientEntityFromHandle(link->entity));
			if (pEntity) {
				pEntity->GetEngineObject()->PhysicsNotifyOtherOfGroundRemoval(this);
			}

			// kill it
			FreeGroundLink(link);

			link = nextLink;
		}

		this->DestroyDataObject(GROUNDLINK);
	}
}

void C_EngineObjectInternal::SetGroundEntity(IEngineObjectClient* ground)
{
	if ((m_hGroundEntity.Get() ? m_hGroundEntity.Get()->GetEngineObject() : NULL) == ground)
		return;

	C_BaseEntity* oldGround = m_hGroundEntity.Get();
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

C_EngineObjectInternal* C_EngineObjectInternal::GetGroundEntity(void)
{
	return m_hGroundEntity.Get() ? (C_EngineObjectInternal*)m_hGroundEntity.Get()->GetEngineObject() : NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : name - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetModelName(string_t name)
{
	m_ModelName = name;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : string_t
//-----------------------------------------------------------------------------
string_t C_EngineObjectInternal::GetModelName(void) const
{
	return m_ModelName;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : index - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetModelIndex(int index)
{
	InvalidateMdlCache();
	m_nModelIndex = index;
	const model_t* pModel = modelinfo->GetModel(m_nModelIndex);
	SetModelPointer(pModel);
}

void C_EngineObjectInternal::SetCollisionGroup(int collisionGroup)
{
	if ((int)m_CollisionGroup != collisionGroup)
	{
		m_CollisionGroup = collisionGroup;
		CollisionRulesChanged();
	}
}

void C_EngineObjectInternal::CollisionRulesChanged()
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
#define CHANGE_FLAGS(flags,newFlags) { unsigned int old = flags; flags = (newFlags); gEntList.ReportEntityFlagsChanged( this, old, flags ); }
#else
#define CHANGE_FLAGS(flags,newFlags) (flags = (newFlags))
#endif

void C_EngineObjectInternal::AddFlag(int flags)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags | flags);
}

void C_EngineObjectInternal::RemoveFlag(int flagsToRemove)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags & ~flagsToRemove);
}

void C_EngineObjectInternal::ClearFlags(void)
{
	CHANGE_FLAGS(m_fFlags, 0);
}

void C_EngineObjectInternal::ToggleFlag(int flagToToggle)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags ^ flagToToggle);
}

void C_EngineObjectInternal::SetEffects(int nEffects)
{
	if (nEffects != m_fEffects)
	{
		m_fEffects = nEffects;
		m_pOuter->UpdateVisibility();
	}
}

void C_EngineObjectInternal::AddEffects(int nEffects)
{
	m_pOuter->OnAddEffects(nEffects);
	m_fEffects |= nEffects;
	if (nEffects & EF_NODRAW)
	{
		m_pOuter->UpdateVisibility();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int	C_EngineObjectInternal::GetIndexForThinkContext(const char* pszContext)
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
int C_EngineObjectInternal::RegisterThinkContext(const char* szContext)
{
	int iIndex = GetIndexForThinkContext(szContext);
	if (iIndex != NO_THINK_CONTEXT)
		return iIndex;

	typedef clientthinkfunc_t thinkfunc_t;

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
CBASEPTR C_EngineObjectInternal::ThinkSet(CBASEPTR func, float thinkTime, const char* szContext)
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
void C_EngineObjectInternal::SetNextThink(float thinkTime, const char* szContext)
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
float C_EngineObjectInternal::GetNextThink(const char* szContext)
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

int	C_EngineObjectInternal::GetNextThinkTick(const char* szContext /*= NULL*/)
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
float C_EngineObjectInternal::GetLastThink(const char* szContext)
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

int C_EngineObjectInternal::GetLastThinkTick(const char* szContext /*= NULL*/)
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

void C_EngineObjectInternal::SetLastThinkTick(int iThinkTick)
{
	m_nLastThinkTick = iThinkTick;
}

bool C_EngineObjectInternal::WillThink()
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
int C_EngineObjectInternal::GetFirstThinkTick()
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
void C_EngineObjectInternal::SetNextThink(int nContextIndex, float thinkTime)
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

void C_EngineObjectInternal::SetLastThink(int nContextIndex, float thinkTime)
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

float C_EngineObjectInternal::GetNextThink(int nContextIndex) const
{
	if (nContextIndex < 0)
		return m_nNextThinkTick * TICK_INTERVAL;

	return m_aThinkFunctions[nContextIndex].m_nNextThinkTick * TICK_INTERVAL;
}

int	C_EngineObjectInternal::GetNextThinkTick(int nContextIndex) const
{
	if (nContextIndex < 0)
		return m_nNextThinkTick;

	return m_aThinkFunctions[nContextIndex].m_nNextThinkTick;
}

// NOTE: pass in the isThinking hint so we have to search the think functions less
void C_EngineObjectInternal::CheckHasThinkFunction(bool isThinking)
{
	if (IsEFlagSet(EFL_NO_THINK_FUNCTION) && isThinking)
	{
		RemoveEFlags(EFL_NO_THINK_FUNCTION);
	}
	else if (!isThinking && !IsEFlagSet(EFL_NO_THINK_FUNCTION) && !WillThink())
	{
		AddEFlags(EFL_NO_THINK_FUNCTION);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Runs thinking code if time.  There is some play in the exact time the think
//  function will be called, because it is called before any movement is done
//  in a frame.  Not used for pushmove objects, because they must be exact.
//  Returns false if the entity removed itself.
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_EngineObjectInternal::PhysicsRunThink(thinkmethods_t thinkMethod)
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
// Purpose: 
//-----------------------------------------------------------------------------
bool C_EngineObjectInternal::PhysicsRunSpecificThink(int nContextIndex, CBASEPTR thinkFunc)
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
	g_ThinkChecker.EntityThinking(gpGlobals->tickcount, this, thinktime, m_nNextThinkTick);
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
void C_EngineObjectInternal::PhysicsDispatchThink(CBASEPTR thinkFunc)
{
	float thinkLimit = think_limit.GetFloat();
	float startTime = 0.0;

	/*
	// This doesn't apply on the client, really
	if ( IsDormant() )
	{
		Warning( "Dormant entity %s is thinking!!\n", GetClassname() );
		Assert(0);
	}
	*/

	if (thinkLimit)
	{
		startTime = engine->Time();
	}

	if (thinkFunc)
	{
		(m_pOuter->*thinkFunc)();
	}

	if (thinkLimit)
	{
		// calculate running time of the AI in milliseconds
		float time = (engine->Time() - startTime) * 1000.0f;
		if (time > thinkLimit)
		{
#if 0
			// If its an NPC print out the shedule/task that took so long
			CAI_BaseNPC* pNPC = MyNPCPointer();
			if (pNPC && pNPC->GetCurSchedule())
			{
				pNPC->ReportOverThinkLimit(time);
			}
			else
#endif
			{
#ifdef WIN32
				Msg("CLIENT:  %s(%s) thinking for %.02f ms!!!\n", GetClassname(), typeid(m_pOuter).raw_name(), time);
#else
				Msg("CLIENT:  %s(%s) thinking for %.02f ms!!!\n", GetClassname(), typeid(m_pOuter).name(), time);
#endif
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : val - 
//			moveCollide - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetMoveType(MoveType_t val, MoveCollide_t moveCollide /*= MOVECOLLIDE_DEFAULT*/)
{
	// Make sure the move type + move collide are compatible...
#ifdef _DEBUG
	if ((val != MOVETYPE_FLY) && (val != MOVETYPE_FLYGRAVITY))
	{
		Assert(moveCollide == MOVECOLLIDE_DEFAULT);
	}
#endif

	m_MoveType = val;
	SetMoveCollide(moveCollide);
}

void C_EngineObjectInternal::SetMoveCollide(MoveCollide_t val)
{
	m_MoveCollide = val;
}

bool C_EngineObjectInternal::WillSimulateGamePhysics()
{
	// players always simulate game physics
	if (!m_pOuter->IsPlayer())
	{
		MoveType_t movetype = GetMoveType();

		if (movetype == MOVETYPE_NONE || movetype == MOVETYPE_VPHYSICS)
			return false;

	}

	return true;
}

void C_EngineObjectInternal::CheckHasGamePhysicsSimulation()
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
}

//-----------------------------------------------------------------------------
// These methods encapsulate MOVETYPE_FOLLOW, which became obsolete
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::FollowEntity(IEngineObjectClient* pBaseEntity, bool bBoneMerge)
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

void C_EngineObjectInternal::StopFollowingEntity()
{
	Assert(IsFollowingEntity());

	SetParent(NULL);
	RemoveEffects(EF_BONEMERGE);
	RemoveSolidFlags(FSOLID_NOT_SOLID);
	SetMoveType(MOVETYPE_NONE);
}

bool C_EngineObjectInternal::IsFollowingEntity()
{
	return IsEffectActive(EF_BONEMERGE) && (GetMoveType() == MOVETYPE_NONE) && GetMoveParent();
}

IEngineObjectClient* C_EngineObjectInternal::GetFollowedEntity()
{
	if (!IsFollowingEntity())
		return NULL;
	return GetMoveParent();
}

//-----------------------------------------------------------------------------
// Purpose: sets client side animation
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::UseClientSideAnimation()
{
	m_bClientSideAnimation = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : scale - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetModelScale(float scale, float change_duration /*= 0.0f*/)
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

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::UpdateModelScale()
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

void C_EngineObjectInternal::LockStudioHdr()
{
	Assert(m_hStudioHdr == MDLHANDLE_INVALID && m_pStudioHdr == NULL);

	AUTO_LOCK(m_StudioHdrInitLock);

	if (m_hStudioHdr != MDLHANDLE_INVALID || m_pStudioHdr != NULL)
	{
		Assert(m_pStudioHdr ? m_pStudioHdr == mdlcache->GetIStudioHdr(m_hStudioHdr) : m_hStudioHdr == MDLHANDLE_INVALID);
		return;
	}

	const model_t* mdl = GetModel();
	if (!mdl)
		return;

	m_hStudioHdr = modelinfo->GetCacheHandle(mdl);
	if (m_hStudioHdr == MDLHANDLE_INVALID)
		return;

	IStudioHdr* pStudioHdr = mdlcache->LockStudioHdr(m_hStudioHdr);
	if (!pStudioHdr)
	{
		m_hStudioHdr = MDLHANDLE_INVALID;
		return;
	}

	//IStudioHdr *pNewWrapper = mdlcache->GetIStudioHdr(pStudioHdr);
	//pNewWrapper->Init( pStudioHdr, mdlcache );
	Assert(pStudioHdr->IsValid());

	//if ( pStudioHdr->GetVirtualModel() )
	//{
	//	MDLHandle_t hVirtualModel = VoidPtrToMDLHandle( pStudioHdr->VirtualModel() );
	//	mdlcache->LockStudioHdr( hVirtualModel );
	//}

	m_pStudioHdr = pStudioHdr;// pNewWrapper; // must be last to ensure virtual model correctly set up
	Assert(pStudioHdr->GetNumPoseParameters() <= ARRAYSIZE(m_flPoseParameter));

	m_iv_flPoseParameter.SetMaxCount(pStudioHdr->GetNumPoseParameters());

	int i;
	for (i = 0; i < pStudioHdr->GetNumPoseParameters(); i++)
	{
		const mstudioposeparamdesc_t& Pose = pStudioHdr->pPoseParameter(i);
		m_iv_flPoseParameter.SetLooping(Pose.loop != 0.0f, i);
		// Note:  We can't do this since if we get a DATA_UPDATE_CREATED (i.e., new entity) with both a new model and some valid pose parameters this will slam the 
		//  pose parameters to zero and if the model goes dormant the pose parameter field will never be set to the true value.  We shouldn't have to zero these out
		//  as they are under the control of the server and should be properly set
		if (!m_pOuter->IsServerEntity())
		{
			SetPoseParameter(pStudioHdr, i, 0.0);
		}
	}

	int boneControllerCount = MIN(pStudioHdr->numbonecontrollers(), ARRAYSIZE(m_flEncodedController));

	m_iv_flEncodedController.SetMaxCount(boneControllerCount);

	for (i = 0; i < boneControllerCount; i++)
	{
		bool loop = (pStudioHdr->pBonecontroller(i)->type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR)) != 0;
		m_iv_flEncodedController.SetLooping(loop, i);
		SetBoneController(i, 0.0);
	}
}

void C_EngineObjectInternal::UnlockStudioHdr()
{
	if (m_hStudioHdr != MDLHANDLE_INVALID)
	{
		//studiohdr_t *pStudioHdr = mdlcache->GetStudioHdr( m_hStudioHdr );
		//Assert( m_pStudioHdr && m_pStudioHdr->GetRenderHdr() == pStudioHdr );

#if 0
		// XXX need to figure out where to flush the queue on map change to not crash
		if (ICallQueue* pCallQueue = materials->GetRenderContext()->GetCallQueue())
		{
			// Parallel rendering: don't unlock model data until end of rendering
			if (pStudioHdr->GetVirtualModel())
			{
				MDLHandle_t hVirtualModel = VoidPtrToMDLHandle(m_pStudioHdr->GetRenderHdr()->VirtualModel());
				pCallQueue->QueueCall(mdlcache, &IMDLCache::UnlockStudioHdr, hVirtualModel);
			}
			pCallQueue->QueueCall(mdlcache, &IMDLCache::UnlockStudioHdr, m_hStudioHdr);
		}
		else
#endif
		{
			// Immediate-mode rendering, can unlock immediately
			//if ( pStudioHdr->GetVirtualModel() )
			//{
			//	MDLHandle_t hVirtualModel = VoidPtrToMDLHandle( m_pStudioHdr->GetRenderHdr()->VirtualModel() );
			//	mdlcache->UnlockStudioHdr( hVirtualModel );
			//}
			mdlcache->UnlockStudioHdr(m_hStudioHdr);
		}
		m_hStudioHdr = MDLHANDLE_INVALID;
		m_pStudioHdr = NULL;
	}
}

void C_EngineObjectInternal::SetModelPointer(const model_t* pModel)
{
	if (m_pModel != pModel)
	{
		m_pOuter->DestroyModelInstance();
		m_pModel = pModel;
		m_pOuter->OnNewModel();

		m_pOuter->UpdateVisibility();
	}
}

//-----------------------------------------------------------------------------
// Sets the cycle, marks the entity as being dirty
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetCycle(float flCycle)
{
	if (m_flCycle != flCycle)
	{
		m_flCycle = flCycle;
		InvalidatePhysicsRecursive(ANIMATION_CHANGED);
	}
}

//-----------------------------------------------------------------------------
// Sets the sequence, marks the entity as being dirty
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetSequence(int nSequence)
{
	if (m_nSequence != nSequence)
	{
		/*
		IStudioHdr *hdr = GetModelPtr();
		// Assert( hdr );
		if ( hdr )
		{
			Assert( nSequence >= 0 && nSequence < hdr->GetNumSeq() );
		}
		*/

		m_nSequence = nSequence;
		InvalidatePhysicsRecursive(ANIMATION_CHANGED);
		if (IsUsingClientSideAnimation())
		{
			m_pOuter->ClientSideAnimationChanged();
		}
	}
}

// Stubs for weapon prediction
void C_EngineObjectInternal::ResetSequenceInfo(void)
{
	if (GetSequence() == -1)
	{
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
	mdlcache->SetEventIndexForSequence(pStudioHdr->pSeqdesc(GetSequence()));
}

void C_EngineObjectInternal::DisableMuzzleFlash()
{
	m_nOldMuzzleFlashParity = m_nMuzzleFlashParity;
}


void C_EngineObjectInternal::DoMuzzleFlash()
{
	m_nMuzzleFlashParity = (m_nMuzzleFlashParity + 1) & ((1 << EF_MUZZLEFLASH_BITS) - 1);
}

// FIXME: redundant?
void C_EngineObjectInternal::GetBoneControllers(float controllers[MAXSTUDIOBONECTRLS])
{
	// interpolate two 0..1 encoded controllers to a single 0..1 controller
	int i;
	for (i = 0; i < MAXSTUDIOBONECTRLS; i++)
	{
		controllers[i] = m_flEncodedController[i];
	}
}

//=========================================================
//=========================================================
float C_EngineObjectInternal::SetBoneController(int iController, float flValue)
{
	Assert(GetModelPtr());

	IStudioHdr* pmodel = GetModelPtr();

	Assert(iController >= 0 && iController < NUM_BONECTRLS);

	float controller = m_flEncodedController[iController];
	float retVal = pmodel->Studio_SetController(iController, flValue, controller);
	m_flEncodedController[iController] = controller;
	return retVal;
}

float C_EngineObjectInternal::GetPoseParameter(int iPoseParameter)
{
	IStudioHdr* pStudioHdr = GetModelPtr();

	if (pStudioHdr == NULL)
		return 0.0f;

	if (pStudioHdr->GetNumPoseParameters() < iPoseParameter)
		return 0.0f;

	if (iPoseParameter < 0)
		return 0.0f;

	return m_flPoseParameter[iPoseParameter];
}

// FIXME: redundant?
void C_EngineObjectInternal::GetPoseParameters(IStudioHdr* pStudioHdr, float poseParameter[MAXSTUDIOPOSEPARAM])
{
	if (!pStudioHdr)
		return;

	// interpolate pose parameters
	int i;
	for (i = 0; i < pStudioHdr->GetNumPoseParameters(); i++)
	{
		poseParameter[i] = m_flPoseParameter[i];
	}


#if 0 // _DEBUG
	if (/* Q_stristr( pStudioHdr->pszName(), r_sequence_debug.GetString()) != NULL || */ r_sequence_debug.GetInt() == entindex())
	{
		DevMsgRT("%s\n", pStudioHdr->pszName());
		DevMsgRT("%6.2f : ", gpGlobals->curtime);
		for (i = 0; i < pStudioHdr->GetNumPoseParameters(); i++)
		{
			const mstudioposeparamdesc_t& Pose = pStudioHdr->pPoseParameter(i);

			DevMsgRT("%s %6.2f ", Pose.pszName(), poseParameter[i] * Pose.end + (1 - poseParameter[i]) * Pose.start);
		}
		DevMsgRT("\n");
	}
#endif
}

CMouthInfo* C_EngineObjectInternal::GetMouth(void)
{
	return &m_mouth;
}

//-----------------------------------------------------------------------------
// Purpose: Do HL1 style lipsynch
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::ControlMouth(IStudioHdr* pstudiohdr)
{
	if (!MouthInfo().NeedsEnvelope())
		return;

	if (!pstudiohdr)
		return;

	int index = LookupPoseParameter(pstudiohdr, LIPSYNC_POSEPARAM_NAME);

	if (index != -1)
	{
		float value = GetMouth()->mouthopen / 64.0;

		float raw = value;

		if (value > 1.0)
			value = 1.0;

		float start, end;
		GetPoseParameterRange(index, start, end);

		value = (1.0 - value) * start + value * end;

		//Adrian - Set the pose parameter value. 
		//It has to be called "mouth".
		SetPoseParameter(pstudiohdr, index, value);
		// Reset interpolation here since the client is controlling this rather than the server...
		m_iv_flPoseParameter.SetHistoryValuesForItem(index, raw);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Check sequence framerate
// Input  : iSequence - 
// Output : float
//-----------------------------------------------------------------------------
float C_EngineObjectInternal::GetSequenceCycleRate(IStudioHdr* pStudioHdr, int iSequence)
{
	if (!pStudioHdr)
		return 0.0f;

	return pStudioHdr->Studio_CPS(pStudioHdr->pSeqdesc(iSequence), iSequence, m_flPoseParameter);
}

float C_EngineObjectInternal::SequenceDuration(IStudioHdr* pStudioHdr, int iSequence)
{
	if (!pStudioHdr)
	{
		return 0.1f;
	}

	if (iSequence >= pStudioHdr->GetNumSeq() || iSequence < 0)
	{
		DevWarning(2, "C_BaseAnimating::SequenceDuration( %d ) out of range\n", iSequence);
		return 0.1;
	}

	return pStudioHdr->Studio_Duration(iSequence, m_flPoseParameter);

}

//=========================================================
//=========================================================
int C_EngineObjectInternal::LookupPoseParameter(IStudioHdr* pstudiohdr, const char* szName)
{
	if (!pstudiohdr)
		return 0;

	for (int i = 0; i < pstudiohdr->GetNumPoseParameters(); i++)
	{
		if (stricmp(pstudiohdr->pPoseParameter(i).pszName(), szName) == 0)
		{
			return i;
		}
	}

	// AssertMsg( 0, UTIL_VarArgs( "poseparameter %s couldn't be mapped!!!\n", szName ) );
	return -1; // Error
}

//=========================================================
//=========================================================
float C_EngineObjectInternal::SetPoseParameter(IStudioHdr* pStudioHdr, const char* szName, float flValue)
{
	return SetPoseParameter(pStudioHdr, LookupPoseParameter(pStudioHdr, szName), flValue);
}

float C_EngineObjectInternal::SetPoseParameter(IStudioHdr* pStudioHdr, int iParameter, float flValue)
{
	if (!pStudioHdr)
	{
		Assert(!"C_BaseAnimating::SetPoseParameter: model missing");
		return flValue;
	}

	if (iParameter >= 0)
	{
		float flNewValue;
		flValue = pStudioHdr->Studio_SetPoseParameter(iParameter, flValue, flNewValue);
		m_flPoseParameter[iParameter] = flNewValue;
	}

	return flValue;
}

void C_EngineObjectInternal::GetSequenceLinearMotion(int iSequence, Vector* pVec)
{
	GetModelPtr()->GetSequenceLinearMotion(iSequence, m_flPoseParameter, pVec);
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : float
//-----------------------------------------------------------------------------
float C_EngineObjectInternal::GetSequenceMoveDist(IStudioHdr* pStudioHdr, int iSequence)
{
	Vector				vecReturn;

	pStudioHdr->GetSequenceLinearMotion(iSequence, m_flPoseParameter, &vecReturn);

	return vecReturn.Length();
}

float C_EngineObjectInternal::GetSequenceGroundSpeed(IStudioHdr* pStudioHdr, int iSequence)
{
	float t = SequenceDuration(pStudioHdr, iSequence);

	if (t > 0)
	{
		return GetSequenceMoveDist(pStudioHdr, iSequence) / t;
	}
	else
	{
		return 0;
	}
}

//=========================================================
//=========================================================

bool C_EngineObjectInternal::IsSequenceLooping(IStudioHdr* pStudioHdr, int iSequence)
{
	return (pStudioHdr->GetSequenceFlags(iSequence) & STUDIO_LOOPING) != 0;
}

bool C_EngineObjectInternal::GetPoseParameterRange(int index, float& minValue, float& maxValue)
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
// Purpose: Note that we've been transmitted a sequence
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::SetReceivedSequence(void)
{
	m_bReceivedSequence = true;
}

void C_EngineObjectInternal::UpdateRelevantInterpolatedVars()
{
	MDLCACHE_CRITICAL_SECTION();
	// Remove any interpolated vars that need to be removed.
	if (!m_pOuter->GetPredictable() /*&& !IsClientCreated()*/ && GetModelPtr() && GetModelPtr()->SequencesAvailable())
	{
		AddBaseAnimatingInterpolatedVars();
	}
	else
	{
		RemoveBaseAnimatingInterpolatedVars();
	}
}


void C_EngineObjectInternal::AddBaseAnimatingInterpolatedVars()
{
	AddVar(&m_iv_flEncodedController, true);//LATCH_ANIMATION_VAR, 
	AddVar(&m_iv_flPoseParameter, true);// LATCH_ANIMATION_VAR, 
	int flags = LATCH_ANIMATION_VAR;
	if (IsUsingClientSideAnimation())
		flags |= EXCLUDE_AUTO_INTERPOLATE;
	m_iv_flCycle.GetType() = flags;

	AddVar(&m_iv_flCycle, true);//flags, 
}

void C_EngineObjectInternal::RemoveBaseAnimatingInterpolatedVars()
{
	RemoveVar(&m_iv_flEncodedController, false);
	RemoveVar(&m_iv_flPoseParameter, false);

#ifdef HL2MP
	// HACK:  Don't want to remove interpolation for predictables in hl2dm, though
	// The animation state stuff sets the pose parameters -- so they should interp
	//  but m_flCycle is not touched, so it's only set during prediction (which occurs on tick boundaries)
	//  and so needs to continue to be interpolated for smooth rendering of the lower body of the local player in third person, etc.
	if (!m_pOuter->GetPredictable())
#endif
	{
		RemoveVar(&m_iv_flCycle, false);
	}
}

bool PVSNotifierMap_LessFunc( IClientUnknown* const &a, IClientUnknown* const &b )
{
	return a < b;
}

// -------------------------------------------------------------------------------------------------- //
// C_AllBaseEntityIterator
// -------------------------------------------------------------------------------------------------- //
//C_AllBaseEntityIterator::C_AllBaseEntityIterator()
//{
//	Restart();
//}
//
//
//void C_AllBaseEntityIterator::Restart()
//{
//	m_CurBaseEntity = ClientEntityList().m_BaseEntities.Head();
//}
//
//	
//C_BaseEntity* C_AllBaseEntityIterator::Next()
//{
//	if ( m_CurBaseEntity == ClientEntityList().m_BaseEntities.InvalidIndex() )
//		return NULL;
//
//	C_BaseEntity *pRet = ClientEntityList().m_BaseEntities[m_CurBaseEntity];
//	m_CurBaseEntity = ClientEntityList().m_BaseEntities.Next( m_CurBaseEntity );
//	return pRet;
//}


// -------------------------------------------------------------------------------------------------- //
// C_BaseEntityIterator
// -------------------------------------------------------------------------------------------------- //
C_BaseEntityIterator::C_BaseEntityIterator()
{
	Restart();
}

void C_BaseEntityIterator::Restart()
{
	start = false;
	m_CurBaseEntity.Term();
}

C_BaseEntity* C_BaseEntityIterator::Next()
{
	while (!start || m_CurBaseEntity.IsValid()) {
		if (!start) {
			start = true;
			m_CurBaseEntity = ClientEntityList().FirstHandle();
		}
		else {
			m_CurBaseEntity = ClientEntityList().NextHandle(m_CurBaseEntity);
		}
		if (!m_CurBaseEntity.IsValid()) {
			break;
		}
		C_BaseEntity* pRet = ClientEntityList().GetBaseEntityFromHandle(m_CurBaseEntity);
		if (!pRet->IsDormant())
			return pRet;
	}

	return NULL;
}