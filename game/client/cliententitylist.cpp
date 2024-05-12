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

// Create interface
static CClientEntityList<C_BaseEntity> s_EntityList;
CBaseEntityList<C_BaseEntity> *g_pEntityList = &s_EntityList;

// Expose list to engine
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CClientEntityList, IClientEntityList, VCLIENTENTITYLIST_INTERFACE_VERSION, s_EntityList );

// Store local pointer to interface for rest of client .dll only 
//  (CClientEntityList instead of IClientEntityList )
CClientEntityList<C_BaseEntity> *cl_entitylist = &s_EntityList;

// -------------------------------------------------------------------------------------------------- //
// Game-code CBaseHandle implementation.
// -------------------------------------------------------------------------------------------------- //
int addVarCount = 0;
const float coordTolerance = 2.0f / (float)(1 << COORD_FRACTIONAL_BITS);

BEGIN_PREDICTION_DATA_NO_BASE(C_EngineObjectInternal)
	DEFINE_FIELD(m_vecAbsVelocity, FIELD_VECTOR),
	DEFINE_PRED_FIELD_TOL(m_vecVelocity, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, 0.5f),
	DEFINE_FIELD(m_vecAbsOrigin, FIELD_VECTOR),
	DEFINE_FIELD(m_angAbsRotation, FIELD_VECTOR),
	DEFINE_FIELD(m_vecOrigin, FIELD_VECTOR),
	DEFINE_FIELD(m_angRotation, FIELD_VECTOR),
	DEFINE_PRED_FIELD_TOL(m_vecNetworkOrigin, FIELD_VECTOR, FTYPEDESC_INSENDTABLE, coordTolerance),
	DEFINE_PRED_FIELD(m_angNetworkAngles, FIELD_VECTOR, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK),
	DEFINE_PRED_FIELD(m_hNetworkMoveParent, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE),
END_PREDICTION_DATA()

BEGIN_DATADESC_NO_BASE(C_EngineObjectInternal)
	DEFINE_FIELD(m_vecAbsOrigin, FIELD_POSITION_VECTOR),
	DEFINE_FIELD(m_angAbsRotation, FIELD_VECTOR),
	DEFINE_ARRAY( m_rgflCoordinateFrame, FIELD_FLOAT, 12 ), // NOTE: MUST BE IN LOCAL SPACE, NOT POSITION_VECTOR!!! (see CBaseEntity::Restore)
	DEFINE_FIELD(m_iEFlags, FIELD_INTEGER),
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

BEGIN_RECV_TABLE_NOBASE(C_EngineObjectInternal, DT_EngineObject)
	RecvPropInt(RECVINFO(testNetwork)),
	RecvPropVector(RECVINFO_NAME(m_vecNetworkOrigin, m_vecOrigin)),
#if PREDICTION_ERROR_CHECK_LEVEL > 1 
	RecvPropVector(RECVINFO_NAME(m_angNetworkAngles, m_angRotation)),
#else
	RecvPropQAngles(RECVINFO_NAME(m_angNetworkAngles, m_angRotation)),
#endif
	RecvPropVector(RECVINFO(m_vecVelocity)),//, 0, RecvProxy_LocalVelocity
	RecvPropInt(RECVINFO_NAME(m_hNetworkMoveParent, moveparent), 0, RecvProxy_IntToMoveParent),
	RecvPropInt(RECVINFO(m_iParentAttachment)),
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_NO_FACTORY(C_EngineObjectInternal, DT_EngineObject, CEngineObjectInternal);

int	C_EngineObjectInternal::entindex() const {
	return m_pOuter->entindex();
}

RecvTable* C_EngineObjectInternal::GetRecvTable() {
	return &DT_EngineObject::g_RecvTable;
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
	//	CollisionProp()->SetCollisionBounds(mins, CollisionProp()->OBBMaxs());
	//	return true;
	//}

	//if (FStrEq(szKeyName, "maxs"))
	//{
	//	Vector maxs;
	//	UTIL_StringToVector(maxs.Base(), szValue);
	//	CollisionProp()->SetCollisionBounds(CollisionProp()->OBBMins(), maxs);
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
// Purpose: 
// Input  : *map - 
//-----------------------------------------------------------------------------
void C_EngineObjectInternal::Interp_SetupMappings(VarMapping_t* map)
{
	if (!map)
		return;

	int c = map->m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &map->m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;
		void* data = e->data;
		int type = e->type;

		watcher->Setup(data, type);
		watcher->SetInterpolationAmount(m_pOuter->GetInterpolationAmount(watcher->GetType()));
	}
}

void C_EngineObjectInternal::Interp_RestoreToLastNetworked(VarMapping_t* map)
{
	VPROF("C_BaseEntity::Interp_RestoreToLastNetworked");

	PREDICTION_TRACKVALUECHANGESCOPE_ENTITY(this->m_pOuter, "restoretolastnetworked");

	Vector oldOrigin = GetLocalOrigin();
	QAngle oldAngles = GetLocalAngles();
	Vector oldVel = GetLocalVelocity();

	int c = map->m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &map->m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;
		watcher->RestoreToLastNetworked();
	}

	BaseInterpolatePart2(oldOrigin, oldAngles, oldVel, 0);
}

void C_EngineObjectInternal::Interp_UpdateInterpolationAmounts(VarMapping_t* map)
{
	if (!map)
		return;

	int c = map->m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &map->m_Entries[i];
		IInterpolatedVar* watcher = e->watcher;
		watcher->SetInterpolationAmount(m_pOuter->GetInterpolationAmount(watcher->GetType()));
	}
}

void C_EngineObjectInternal::Interp_HierarchyUpdateInterpolationAmounts()
{
	Interp_UpdateInterpolationAmounts(GetVarMapping());

	for (C_EngineObjectInternal* pChild = FirstMoveChild(); pChild; pChild = pChild->NextMovePeer())
	{
		pChild->Interp_HierarchyUpdateInterpolationAmounts();
	}
}

inline int C_EngineObjectInternal::Interp_Interpolate(VarMapping_t* map, float currentTime)
{
	int bNoMoreChanges = 1;
	if (currentTime < map->m_lastInterpolationTime)
	{
		for (int i = 0; i < map->m_nInterpolatedEntries; i++)
		{
			VarMapEntry_t* e = &map->m_Entries[i];

			e->m_bNeedsToInterpolate = true;
		}
	}
	map->m_lastInterpolationTime = currentTime;

	for (int i = 0; i < map->m_nInterpolatedEntries; i++)
	{
		VarMapEntry_t* e = &map->m_Entries[i];

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
Vector& C_EngineObjectInternal::GetAbsOrigin(void)
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
QAngle& C_EngineObjectInternal::GetAbsAngles(void)
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


void C_EngineObjectInternal::UnlinkChild(IEngineObjectClient* pParent, IEngineObjectClient* pChild)
{
	Assert(pChild);
	Assert(pParent != pChild);
	Assert(pChild->GetMoveParent() == pParent);

	// Unlink from parent
	// NOTE: pParent *may well be NULL*! This occurs
	// when a child has unlinked from a parent, and the child
	// remains in the PVS but the parent has not
	if (pParent && (pParent->FirstMoveChild() == pChild))
	{
		Assert(!(pChild->MovePrevPeer()));
		pParent->SetFirstMoveChild(pChild->NextMovePeer());
	}

	// Unlink from siblings...
	if (pChild->MovePrevPeer())
	{
		pChild->MovePrevPeer()->SetNextMovePeer(pChild->NextMovePeer());
	}
	if (pChild->NextMovePeer())
	{
		pChild->NextMovePeer()->SetMovePrevPeer(pChild->MovePrevPeer());
	}

	pChild->SetNextMovePeer( NULL);
	pChild->SetMovePrevPeer( NULL);
	pChild->SetMoveParent( NULL);
	pChild->GetOuter()->RemoveFromAimEntsList();

	Interp_HierarchyUpdateInterpolationAmounts();
}

void C_EngineObjectInternal::LinkChild(IEngineObjectClient* pParent, IEngineObjectClient* pChild)
{
	Assert(!pChild->NextMovePeer());
	Assert(!pChild->MovePrevPeer());
	Assert(!pChild->GetMoveParent());
	Assert(pParent != pChild);

#ifdef _DEBUG
	// Make sure the child isn't already in this list
	IEngineObjectClient* pExistingChild;
	for (pExistingChild = pParent->FirstMoveChild(); pExistingChild; pExistingChild = pExistingChild->NextMovePeer())
	{
		Assert(pChild != pExistingChild);
	}
#endif

	pChild->SetMovePrevPeer( NULL);
	pChild->SetNextMovePeer( pParent->FirstMoveChild());
	if (pChild->NextMovePeer())
	{
		pChild->NextMovePeer()->SetMovePrevPeer( pChild);
	}
	pParent->SetFirstMoveChild( pChild);
	pChild->SetMoveParent( pParent);
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
		UnlinkChild(m_pMoveParent, this);
	}
	if (pNewParent)
	{
		LinkChild(pNewParent, this);
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
		UnlinkChild(m_pMoveParent, this);
	}

	if (pParentEntity)
	{
		LinkChild(pParentEntity, this);
	}

	if (!m_pOuter->IsServerEntity())
	{
		SetNetworkMoveParent( pParentEntity);
	}

	m_iParentAttachment = iParentAttachment;

	GetAbsOrigin().Init(FLT_MAX, FLT_MAX, FLT_MAX);
	GetAbsAngles().Init(FLT_MAX, FLT_MAX, FLT_MAX);
	GetAbsVelocity().Init(FLT_MAX, FLT_MAX, FLT_MAX);

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
		UnlinkChild(m_pMoveParent, this);
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
			UnlinkChild(this, pChild);
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
	float changetime = m_pOuter->GetLastChangeTime(flags);

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
	if (m_pOuter->IsFollowingEntity() || !m_pOuter->IsInterpolationEnabled())
	{
		// Assume current origin ( no interpolation )
		m_pOuter->MoveToLastReceivedPosition();
		return INTERPOLATE_STOP;
	}


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

	bNoMoreChanges = Interp_Interpolate(GetVarMapping(), currentTime);
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

vec_t C_EngineObjectInternal::GetLocalOriginDim(int iDim) const
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

vec_t C_EngineObjectInternal::GetLocalAnglesDim(int iDim) const
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

Vector& C_EngineObjectInternal::GetAbsVelocity()
{
	Assert(C_BaseEntity::s_bAbsQueriesValid);
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	return m_vecAbsVelocity;
}

const Vector& C_EngineObjectInternal::GetAbsVelocity() const
{
	Assert(C_BaseEntity::s_bAbsQueriesValid);
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	return m_vecAbsVelocity;
}

//-----------------------------------------------------------------------------
// Velocity
//-----------------------------------------------------------------------------
Vector& C_EngineObjectInternal::GetLocalVelocity()
{
	return m_vecVelocity;
}

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
	Assert(C_BaseEntity::s_bAbsQueriesValid);
	CalcAbsolutePosition();
	return m_rgflCoordinateFrame;
}

const matrix3x4_t& C_EngineObjectInternal::EntityToWorldTransform() const
{
	Assert(C_BaseEntity::s_bAbsQueriesValid);
	const_cast<C_EngineObjectInternal*>(this)->CalcAbsolutePosition();
	return m_rgflCoordinateFrame;
}


matrix3x4_t& C_EngineObjectInternal::GetParentToWorldTransform(matrix3x4_t& tempMatrix)
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
	if (!m_pOuter->s_bAbsRecomputationEnabled)
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

	if (m_pOuter->IsEffectActive(EF_BONEMERGE))
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
	AddDataChangeEvent(m_pOuter, DATA_UPDATE_DATATABLE_CHANGED, &m_pOuter->m_DataChangeEventRef);

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
	CStudioHdr* pHdr = ((C_BaseAnimating*)m_pOuter)->GetModelPtr();
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
	int oldModelIndex = m_pOuter->m_nModelIndex;

	CPredictionCopy copyHelper(type, this, TD_OFFSET_NORMAL, src, TD_OFFSET_PACKED);
	int error_count = copyHelper.TransferData(sz, m_pOuter->entindex(), this->GetPredDescMap());
	CPredictionCopy outerCopyHelper(type, m_pOuter, TD_OFFSET_NORMAL, outerSrc, TD_OFFSET_PACKED);
	int outerError_count = outerCopyHelper.TransferData(sz, m_pOuter->entindex(), m_pOuter->GetPredDescMap());

	// set non-predicting flags back to their prior state
	RemoveEFlags(savedEFlagsMask);
	AddEFlags(savedEFlags);

	// restore original model index and change via SetModelIndex
	int newModelIndex = m_pOuter->m_nModelIndex;
	m_pOuter->m_nModelIndex = oldModelIndex;
	int overrideModelIndex = m_pOuter->CalcOverrideModelIndex();
	if (overrideModelIndex != -1)
		newModelIndex = overrideModelIndex;
	if (oldModelIndex != newModelIndex)
	{
		MDLCACHE_CRITICAL_SECTION(); // ???
		m_pOuter->SetModelIndex(newModelIndex);
	}


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

void C_EngineObjectInternal::Interp_Reset(VarMapping_t* map)
{
	PREDICTION_TRACKVALUECHANGESCOPE_ENTITY(this->m_pOuter, "reset");
	int c = map->m_Entries.Count();
	for (int i = 0; i < c; i++)
	{
		VarMapEntry_t* e = &map->m_Entries[i];
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

void C_EngineObjectInternal::AddVar(void* data, IInterpolatedVar* watcher, int type, bool bSetup)
{
	// Only add it if it hasn't been added yet.
	bool bAddIt = true;
	for (int i = 0; i < m_VarMap.m_Entries.Count(); i++)
	{
		if (m_VarMap.m_Entries[i].watcher == watcher)
		{
			if ((type & EXCLUDE_AUTO_INTERPOLATE) != (watcher->GetType() & EXCLUDE_AUTO_INTERPOLATE))
			{
				// Its interpolation mode changed, so get rid of it and re-add it.
				RemoveVar(m_VarMap.m_Entries[i].data, true);
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
		map.data = data;
		map.watcher = watcher;
		map.type = type;
		map.m_bNeedsToInterpolate = true;
		if (type & EXCLUDE_AUTO_INTERPOLATE)
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
		watcher->Setup(data, type);
		watcher->SetInterpolationAmount(m_pOuter->GetInterpolationAmount(watcher->GetType()));
	}
}


void C_EngineObjectInternal::RemoveVar(void* data, bool bAssert)
{
	for (int i = 0; i < m_VarMap.m_Entries.Count(); i++)
	{
		if (m_VarMap.m_Entries[i].data == data)
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
			if ((pChild->GetParentAttachment() == 0) && !pChild->m_pOuter->IsFollowingEntity())
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