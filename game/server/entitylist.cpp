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
END_DATADESC()

void SendProxy_Origin(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* v;
	if (entity->GetOuter()->entindex() == 1) {
		int aaa = 0;
	}
	if (!entity->GetOuter()->UseStepSimulationNetworkOrigin(&v))
	{
		v = &entity->GetLocalOrigin();
	}

	pOut->m_Vector[0] = v->x;
	pOut->m_Vector[1] = v->y;
	pOut->m_Vector[2] = v->z;
}

void SendProxy_Angles(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const QAngle* a;

	if (!entity->GetOuter()->UseStepSimulationNetworkAngles(&a))
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

BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_EngineObject)
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
					// loop through the data description, and try and place the keys in
					if (!*ent_debugkeys.GetString())
					{
						for (datamap_t* dmap = m_pOuter->GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap)
						{
							if (::ParseKeyvalue(m_pOuter, dmap->dataDesc, dmap->dataNumFields, keyName, value)) {
								//return true;
								break;
							}
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
						for (datamap_t* dmap = m_pOuter->GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap)
						{
							if (!printKeyHits && *ent_debugkeys.GetString() && !Q_stricmp(dmap->dataClassName, ent_debugkeys.GetString()))
							{
								// Msg( "-- found class of type %s\n", dmap->dataClassName );
								printKeyHits = true;
								debugName = dmap->dataClassName;
							}

							if (::ParseKeyvalue(m_pOuter, dmap->dataDesc, dmap->dataNumFields, keyName, value))
							{
								if (printKeyHits)
									Msg("(%s) key: %-16s value: %s\n", debugName, keyName, value);

								//return true;
								break;
							}
						}

						if (printKeyHits)
							Msg("!! (%s) key not handled: \"%s\" \"%s\"\n", STRING(m_iClassname), keyName, value);
					}
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
// handler to do stuff before you are saved
//-----------------------------------------------------------------------------
void CEngineObjectInternal::OnSave(IEntitySaveUtils* pUtils)
{
	// Here, we must force recomputation of all abs data so it gets saved correctly
	// We can't leave the dirty bits set because the loader can't cope with it.
	CalcAbsolutePosition();
	CalcAbsoluteVelocity();
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

	m_pOuter->OnRestore();
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
			m_pOuter->VPhysicsDestroyObject();
			m_pOuter->VPhysicsInitShadow(false, false);
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

			this->GetOuter()->RecalcHasPlayerChildBit();
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
	this->GetOuter()->RecalcHasPlayerChildBit();
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
		m_pOuter->SetSimulationTime(gpGlobals->curtime);
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
		m_pOuter->SetSimulationTime(gpGlobals->curtime);
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
		m_pOuter->SetSimulationTime(gpGlobals->curtime);
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
		m_pOuter->SetSimulationTime(gpGlobals->curtime);
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
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	}
	return m_vecAbsVelocity;
}

const Vector& CEngineObjectInternal::GetAbsVelocity() const
{
	Assert(CBaseEntity::IsAbsQueriesValid());

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
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_vecAbsOrigin;
}

const Vector& CEngineObjectInternal::GetAbsOrigin(void) const
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_vecAbsOrigin;
}

const QAngle& CEngineObjectInternal::GetAbsAngles(void)
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_angAbsRotation;
}

const QAngle& CEngineObjectInternal::GetAbsAngles(void) const
{
	Assert(CBaseEntity::IsAbsQueriesValid());

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
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		CalcAbsolutePosition();
	}
	return m_rgflCoordinateFrame;
}

const matrix3x4_t& CEngineObjectInternal::EntityToWorldTransform() const
{
	Assert(CBaseEntity::IsAbsQueriesValid());

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
			if (!m_pOuter->GetModel() && Q_strlen(STRING(m_pOuter->GetEngineObject()->GetModelName())) == 0)
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
			((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetEngineObject()->PhysicsNotifyOtherOfUntouch(this);

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
	if (ground && m_pOuter->IsPlayer() && ground->GetOuter()->GetMoveType() == MOVETYPE_VPHYSICS)
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

	if (index != m_nModelIndex)
	{
		/*if ( m_bDynamicModelPending )
		{
			sg_DynamicLoadHandlers.Remove( this );
		}*/

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
		m_pOuter->SetModelPointer(pModel);
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
		FunctionCheck(*(reinterpret_cast<void**>(&m_pfnThink)), "BaseThinkFunc");
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
	FunctionCheck(*(reinterpret_cast<void**>(&m_aThinkFunctions[iIndex].m_pfnThink)), szContext);
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
			ray.Init(pEntity->GetAbsOrigin(), pEntity->GetAbsOrigin(), pEntity->GetOuter()->WorldAlignMins(), pEntity->GetOuter()->WorldAlignMaxs());
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
				Assert(m_simThinkList[i].nextThinkTick == 0 || pList[out]->GetFirstThinkTick() == m_simThinkList[i].nextThinkTick);
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

