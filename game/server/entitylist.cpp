//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "entitylist.h"
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

#ifdef HL2_DLL
#include "npc_playercompanion.h"
#endif // HL2_DLL

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void SceneManager_ClientActive( CBasePlayer *player );

CGlobalEntityList<CBaseEntity> gEntList;
CBaseEntityList<CBaseEntity> *g_pEntityList = &gEntList;

// Expose list to engine
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CGlobalEntityList, IServerEntityList, VSERVERENTITYLIST_INTERFACE_VERSION, gEntList);

// Store local pointer to interface for rest of client .dll only 
//  (CClientEntityList instead of IClientEntityList )
CGlobalEntityList<CBaseEntity>* sv_entitylist = &gEntList;

// -------------------------------------------------------------------------------------------------- //
// Game-code CBaseHandle implementation.
// -------------------------------------------------------------------------------------------------- //

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

	Assert(m_pOuter->entindex() != pInfo->m_pClientEnt);

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
void CEngineObjectInternal::SetParent(IEngineObject* pParentEntity, int iAttachment)
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
		iAttachment = m_pOuter->m_iParentAttachment;
	}

	bool bWasNotParented = (GetMoveParent() == NULL);
	CEngineObjectInternal* pOldParent = GetMoveParent();

	this->m_pOuter->BeforeUnlinkParent(pParentEntity ? pParentEntity->GetOuter() : NULL);
	// notify the old parent of the loss
	IEngineObject::UnlinkFromParent(this);

	if (pParentEntity == NULL)
	{
		m_pOuter->m_iParent = NULL_STRING;

		// Transform step data from parent to worldspace
		m_pOuter->TransformStepData_ParentToWorld(pOldParent ? pOldParent->m_pOuter : NULL);
		return;
	}
	// set the new name
	//m_pParent = pParentEntity;
	m_pOuter->m_iParent = pParentEntity->GetOuter()->m_iName;

	m_pOuter->RemoveSolidFlags(FSOLID_ROOT_PARENT_ALIGNED);
	if (const_cast<IEngineObject*>(pParentEntity)->GetRootMoveParent()->GetOuter()->GetSolid() == SOLID_BSP)
	{
		m_pOuter->AddSolidFlags(FSOLID_ROOT_PARENT_ALIGNED);
	}
	else
	{
		if (m_pOuter->GetSolid() == SOLID_BSP)
		{
			// Must be SOLID_VPHYSICS because parent might rotate
			m_pOuter->SetSolid(SOLID_VPHYSICS);
		}
	}

	// set the move parent if we have one

	// add ourselves to the list
	IEngineObject::LinkChild(pParentEntity, this);
	this->m_pOuter->AfterLinkParent(pOldParent ? pOldParent->m_pOuter : NULL);

	m_pOuter->m_iParentAttachment = (char)iAttachment;

	EntityMatrix matrix, childMatrix;
	matrix.InitFromEntity(const_cast<CBaseEntity*>(pParentEntity->GetOuter()), m_pOuter->m_iParentAttachment); // parent->world
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
	UTIL_SetOrigin(this->m_pOuter, localOrigin);

	// Move our step data into the correct space
	if (bWasNotParented)
	{
		// Transform step data from world to parent-space
		m_pOuter->TransformStepData_WorldToParent(this->m_pOuter);
	}
	else
	{
		// Transform step data between parent-spaces
		m_pOuter->TransformStepData_ParentToParent(pOldParent->m_pOuter, this->m_pOuter);
	}

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
	m_pOuter->CollisionRulesChanged();
}

//-----------------------------------------------------------------------------
// Purpose: Calculates the absolute position of an edict in the world
//			assumes the parent's absolute origin has already been calculated
//-----------------------------------------------------------------------------
void CEngineObjectInternal::CalcAbsolutePosition(void)
{
	if (!m_pOuter->IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
		return;

	m_pOuter->RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	// Plop the entity->parent matrix into m_rgflCoordinateFrame
	AngleMatrix(m_angRotation, m_vecOrigin, m_rgflCoordinateFrame);

	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		// no move parent, so just copy existing values
		m_vecAbsOrigin = m_vecOrigin;
		m_angAbsRotation = m_angRotation;
		if (m_pOuter->HasDataObjectType(POSITIONWATCHER))
		{
			ReportPositionChanged(this->m_pOuter);
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
	if ((m_angRotation == vec3_angle) && (m_pOuter->m_iParentAttachment == 0))
	{
		// just copy our parent's absolute angles
		VectorCopy(pMoveParent->GetAbsAngles(), m_angAbsRotation);
	}
	else
	{
		MatrixAngles(m_rgflCoordinateFrame, m_angAbsRotation);
	}
	if (m_pOuter->HasDataObjectType(POSITIONWATCHER))
	{
		ReportPositionChanged(this->m_pOuter);
	}
}

void CEngineObjectInternal::CalcAbsoluteVelocity()
{
	if (!m_pOuter->IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
		return;

	m_pOuter->RemoveEFlags(EFL_DIRTY_ABSVELOCITY);

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

matrix3x4_t& CEngineObjectInternal::GetParentToWorldTransform(matrix3x4_t& tempMatrix)
{
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		Assert(false);
		SetIdentityMatrix(tempMatrix);
		return tempMatrix;
	}

	if (m_pOuter->m_iParentAttachment != 0)
	{
		MDLCACHE_CRITICAL_SECTION();

		CBaseAnimating* pAnimating = pMoveParent->m_pOuter->GetBaseAnimating();
		if (pAnimating && pAnimating->GetAttachment(m_pOuter->m_iParentAttachment, tempMatrix))
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
	m_pOuter->InvalidatePhysicsRecursive(POSITION_CHANGED);
	m_pOuter->RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

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
		matrix3x4_t& parentTransform = GetParentToWorldTransform(tempMat);

		// Moveparent case: transform the abs position into local space
		VectorITransform(absOrigin, parentTransform, vecNewOrigin);
	}

	if (m_vecOrigin != vecNewOrigin)
	{
		m_pOuter->NetworkStateChanged(55554);
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
	m_pOuter->InvalidatePhysicsRecursive(ANGLES_CHANGED);
	m_pOuter->RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

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
		m_pOuter->NetworkStateChanged(55555);
		m_angRotation = angNewRotation;
		m_pOuter->SetSimulationTime(gpGlobals->curtime);
	}
}

void CEngineObjectInternal::SetAbsVelocity(const Vector& vecAbsVelocity)
{
	if (m_vecAbsVelocity == vecAbsVelocity)
		return;
	m_pOuter->NetworkStateChanged(55556);
	//m_pOuter->NetworkStateChanged(55553);
	// The abs velocity won't be dirty since we're setting it here
	// All children are invalid, but we are not
	m_pOuter->InvalidatePhysicsRecursive(VELOCITY_CHANGED);
	m_pOuter->RemoveEFlags(EFL_DIRTY_ABSVELOCITY);

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
		m_pOuter->NetworkStateChanged(55554);
		m_pOuter->InvalidatePhysicsRecursive(POSITION_CHANGED);
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
		m_pOuter->NetworkStateChanged(55555);
		m_pOuter->InvalidatePhysicsRecursive(ANGLES_CHANGED);
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
		m_pOuter->NetworkStateChanged(55556);
		m_pOuter->InvalidatePhysicsRecursive(VELOCITY_CHANGED);
		m_vecVelocity = vecVelocity;
	}
}

const Vector& CEngineObjectInternal::GetLocalVelocity() const
{
	return m_vecVelocity;
}

Vector& CEngineObjectInternal::GetAbsVelocity()
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (m_pOuter->IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	}
	return m_vecAbsVelocity;
}

const Vector& CEngineObjectInternal::GetAbsVelocity() const
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (m_pOuter->IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	}
	return m_vecAbsVelocity;
}

//-----------------------------------------------------------------------------
// Physics state accessor methods
//-----------------------------------------------------------------------------
Vector& CEngineObjectInternal::GetLocalOriginForWrite(void) {
	return m_vecOrigin;
}

const Vector& CEngineObjectInternal::GetLocalOrigin(void) const
{
	return m_vecOrigin;
}

const QAngle& CEngineObjectInternal::GetLocalAngles(void) const
{
	return m_angRotation;
}

Vector& CEngineObjectInternal::GetAbsOrigin(void)
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (m_pOuter->IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_vecAbsOrigin;
}

const Vector& CEngineObjectInternal::GetAbsOrigin(void) const
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (m_pOuter->IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_vecAbsOrigin;
}

QAngle& CEngineObjectInternal::GetAbsAngles(void)
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (m_pOuter->IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_angAbsRotation;
}

const QAngle& CEngineObjectInternal::GetAbsAngles(void) const
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (m_pOuter->IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
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
	return m_hMoveParent.Get() ? (CEngineObjectInternal*)m_hMoveParent.Get()->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetMoveParent(EHANDLE hMoveParent) {
	m_hMoveParent = hMoveParent;
	m_pOuter->NetworkStateChanged();
}

CEngineObjectInternal* CEngineObjectInternal::FirstMoveChild(void)
{
	return m_hMoveChild.Get() ? (CEngineObjectInternal*)m_hMoveChild.Get()->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetFirstMoveChild(EHANDLE hMoveChild) {
	m_hMoveChild = hMoveChild;
}

CEngineObjectInternal* CEngineObjectInternal::NextMovePeer(void)
{
	return m_hMovePeer.Get() ? (CEngineObjectInternal*)m_hMovePeer.Get()->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetNextMovePeer(EHANDLE hMovePeer) {
	m_hMovePeer = hMovePeer;
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

	if (m_pOuter->IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		CalcAbsolutePosition();
	}
	return m_rgflCoordinateFrame;
}

const matrix3x4_t& CEngineObjectInternal::EntityToWorldTransform() const
{
	Assert(CBaseEntity::IsAbsQueriesValid());

	if (m_pOuter->IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
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



// Called by CEntityListSystem
void CAimTargetManager::LevelInitPreEntity()
{ 
	gEntList.AddListenerEntity( this );
	Clear(); 
}
void CAimTargetManager::LevelShutdownPostEntity()
{
	gEntList.RemoveListenerEntity( this );
	Clear();
}

void CAimTargetManager::Clear()
{ 
	m_targetList.Purge(); 
}

void CAimTargetManager::ForceRepopulateList()
{
	Clear();

	CBaseEntity *pEnt = gEntList.FirstEnt();

	while( pEnt )
	{
		if( ShouldAddEntity(pEnt) )
			AddEntity(pEnt);

		pEnt = gEntList.NextEnt( pEnt );
	}
}
	
bool CAimTargetManager::ShouldAddEntity( CBaseEntity *pEntity )
{
	return ((pEntity->GetFlags() & FL_AIMTARGET) != 0);
}

// IEntityListener
void CAimTargetManager::OnEntityCreated( CBaseEntity *pEntity ) {}
void CAimTargetManager::OnEntityDeleted( CBaseEntity *pEntity )
{
	if ( !(pEntity->GetFlags() & FL_AIMTARGET) )
		return;
	RemoveEntity(pEntity);
}
void CAimTargetManager::AddEntity( CBaseEntity *pEntity )
{
	if ( pEntity->IsMarkedForDeletion() )
		return;
	m_targetList.AddToTail( pEntity );
}
void CAimTargetManager::RemoveEntity( CBaseEntity *pEntity )
{
	int index = m_targetList.Find( pEntity );
	if ( m_targetList.IsValidIndex(index) )
	{
		m_targetList.FastRemove( index );
	}
}
int CAimTargetManager::ListCount() { return m_targetList.Count(); }
int CAimTargetManager::ListCopy( CBaseEntity *pList[], int listMax )
{
	int count = MIN(listMax, ListCount() );
	memcpy( pList, m_targetList.Base(), sizeof(CBaseEntity *) * count );
	return count;
}

CAimTargetManager g_AimManager;

int AimTarget_ListCount()
{
	return g_AimManager.ListCount();
}
int AimTarget_ListCopy( CBaseEntity *pList[], int listMax )
{
	return g_AimManager.ListCopy( pList, listMax );
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
		for ( int i = 0; i < ARRAYSIZE(m_entinfoIndex); i++ )
		{
			m_entinfoIndex[i] = 0xFFFF;
		}
	}
	void LevelInitPreEntity()
	{
		gEntList.AddListenerEntity( this );
	}

	void LevelShutdownPostEntity()
	{
		gEntList.RemoveListenerEntity( this );
		Clear();
	}

	void OnEntityCreated( CBaseEntity *pEntity )
	{
		Assert( m_entinfoIndex[pEntity->GetRefEHandle().GetEntryIndex()] == 0xFFFF );
	}
	void OnEntityDeleted( CBaseEntity *pEntity )
	{
		RemoveEntinfoIndex( pEntity->GetRefEHandle().GetEntryIndex() );
	}

	void RemoveEntinfoIndex( int index )
	{
		int listHandle = m_entinfoIndex[index];
		// If this guy is in the active list, remove him
		if ( listHandle != 0xFFFF )
		{
			Assert(m_simThinkList[listHandle].entEntry == index);
			m_simThinkList.FastRemove( listHandle );
			m_entinfoIndex[index] = 0xFFFF;
			
			// fast remove shifted someone, update that someone
			if ( listHandle < m_simThinkList.Count() )
			{
				m_entinfoIndex[m_simThinkList[listHandle].entEntry] = listHandle;
			}
		}
	}
	int ListCount()
	{
		return m_simThinkList.Count();
	}

	int ListCopy( CBaseEntity *pList[], int listMax )
	{
		int count = MIN(listMax, ListCount());
		int out = 0;
		for ( int i = 0; i < count; i++ )
		{
			// only copy out entities that will simulate or think this frame
			if ( m_simThinkList[i].nextThinkTick <= gpGlobals->tickcount )
			{
				Assert(m_simThinkList[i].nextThinkTick>=0);
				int entinfoIndex = m_simThinkList[i].entEntry;
				const CEntInfo<CBaseEntity> *pInfo = gEntList.GetEntInfoPtrByIndex( entinfoIndex );
				pList[out] = (CBaseEntity *)pInfo->m_pEntity;
				Assert(m_simThinkList[i].nextThinkTick==0 || pList[out]->GetFirstThinkTick()==m_simThinkList[i].nextThinkTick);
				Assert( gEntList.IsEntityPtr( pList[out] ) );
				out++;
			}
		}

		return out;
	}

	void EntityChanged( CBaseEntity *pEntity )
	{
		// might change after deletion, don't put back into the list
		if ( pEntity->IsMarkedForDeletion() )
			return;
		
		const CBaseHandle &eh = pEntity->GetRefEHandle();
		if ( !eh.IsValid() )
			return;

		int index = eh.GetEntryIndex();
		if ( pEntity->IsEFlagSet( EFL_NO_THINK_FUNCTION ) && pEntity->IsEFlagSet( EFL_NO_GAME_PHYSICS_SIMULATION ) )
		{
			RemoveEntinfoIndex( index );
		}
		else
		{
			// already in the list? (had think or sim last time, now has both - or had both last time, now just one)
			if ( m_entinfoIndex[index] == 0xFFFF )
			{
				MEM_ALLOC_CREDIT();
				m_entinfoIndex[index] = m_simThinkList.AddToTail();
				m_simThinkList[m_entinfoIndex[index]].entEntry = (unsigned short)index;
				m_simThinkList[m_entinfoIndex[index]].nextThinkTick = 0;
				if ( pEntity->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION) )
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = pEntity->GetFirstThinkTick();
					Assert(m_simThinkList[m_entinfoIndex[index]].nextThinkTick>=0);
				}
			}
			else
			{
				// updating existing entry - if no sim, reset think time
				if ( pEntity->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION) )
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = pEntity->GetFirstThinkTick();
					Assert(m_simThinkList[m_entinfoIndex[index]].nextThinkTick>=0);
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

int SimThink_ListCopy( CBaseEntity *pList[], int listMax )
{
	return g_SimThinkManager.ListCopy( pList, listMax );
}

void SimThink_EntityChanged( CBaseEntity *pEntity )
{
	g_SimThinkManager.EntityChanged( pEntity );
}

static CBaseEntityClassList *s_pClassLists = NULL;
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
	CBaseEntity	*pNotify;
	CBaseEntity	*pWatched;
};
class CNotifyList : public INotify, public IEntityListener<CBaseEntity>
{
public:
	// INotify
	void AddEntity( CBaseEntity *pNotify, CBaseEntity *pWatched );
	void RemoveEntity( CBaseEntity *pNotify, CBaseEntity *pWatched );
	void ReportNamedEvent( CBaseEntity *pEntity, const char *pEventName );
	void ClearEntity( CBaseEntity *pNotify );
	void ReportSystemEvent( CBaseEntity *pEntity, notify_system_event_t eventType, const notify_system_event_params_t &params );

	// IEntityListener
	virtual void OnEntityCreated( CBaseEntity *pEntity );
	virtual void OnEntityDeleted( CBaseEntity *pEntity );

	// Called from CEntityListSystem
	void LevelInitPreEntity();
	void LevelShutdownPreEntity();

private:
	CUtlVector<entitynotify_t>	m_notifyList;
};

void CNotifyList::AddEntity( CBaseEntity *pNotify, CBaseEntity *pWatched )
{
	// OPTIMIZE: Also flag pNotify for faster "RemoveAllNotify" ?
	pWatched->AddEFlags( EFL_NOTIFY );
	int index = m_notifyList.AddToTail();
	entitynotify_t &notify = m_notifyList[index];
	notify.pNotify = pNotify;
	notify.pWatched = pWatched;
}

// Remove noitfication for an entity
void CNotifyList::RemoveEntity( CBaseEntity *pNotify, CBaseEntity *pWatched )
{
	for ( int i = m_notifyList.Count(); --i >= 0; )
	{
		if ( m_notifyList[i].pNotify == pNotify && m_notifyList[i].pWatched == pWatched)
		{
			m_notifyList.FastRemove(i);
		}
	}
}


void CNotifyList::ReportNamedEvent( CBaseEntity *pEntity, const char *pInputName )
{
	variant_t emptyVariant;

	if ( !pEntity->IsEFlagSet(EFL_NOTIFY) )
		return;

	for ( int i = 0; i < m_notifyList.Count(); i++ )
	{
		if ( m_notifyList[i].pWatched == pEntity )
		{
			m_notifyList[i].pNotify->AcceptInput( pInputName, pEntity, pEntity, emptyVariant, 0 );
		}
	}
}

void CNotifyList::LevelInitPreEntity()
{
	gEntList.AddListenerEntity( this );
}

void CNotifyList::LevelShutdownPreEntity( void )
{
	gEntList.RemoveListenerEntity( this );
	m_notifyList.Purge();
}

void CNotifyList::OnEntityCreated( CBaseEntity *pEntity )
{
}

void CNotifyList::OnEntityDeleted( CBaseEntity *pEntity )
{
	ReportDestroyEvent( pEntity );
	ClearEntity( pEntity );
}


// UNDONE: Slow linear search?
void CNotifyList::ClearEntity( CBaseEntity *pNotify )
{
	for ( int i = m_notifyList.Count(); --i >= 0; )
	{
		if ( m_notifyList[i].pNotify == pNotify || m_notifyList[i].pWatched == pNotify)
		{
			m_notifyList.FastRemove(i);
		}
	}
}

void CNotifyList::ReportSystemEvent( CBaseEntity *pEntity, notify_system_event_t eventType, const notify_system_event_params_t &params )
{
	if ( !pEntity->IsEFlagSet(EFL_NOTIFY) )
		return;

	for ( int i = 0; i < m_notifyList.Count(); i++ )
	{
		if ( m_notifyList[i].pWatched == pEntity )
		{
			m_notifyList[i].pNotify->NotifySystemEvent( pEntity, eventType, params );
		}
	}
}

static CNotifyList g_NotifyList;
INotify *g_pNotify = &g_NotifyList;

class CEntityTouchManager : public IEntityListener<CBaseEntity>
{
public:
	// called by CEntityListSystem
	void LevelInitPreEntity() 
	{ 
		gEntList.AddListenerEntity( this );
		Clear(); 
	}
	void LevelShutdownPostEntity() 
	{ 
		gEntList.RemoveListenerEntity( this );
		Clear(); 
	}
	void FrameUpdatePostEntityThink();

	void Clear()
	{
		m_updateList.Purge();
	}
	
	// IEntityListener
	virtual void OnEntityCreated( CBaseEntity *pEntity ) {}
	virtual void OnEntityDeleted( CBaseEntity *pEntity )
	{
		if ( !pEntity->GetCheckUntouch() )
			return;
		int index = m_updateList.Find( pEntity );
		if ( m_updateList.IsValidIndex(index) )
		{
			m_updateList.FastRemove( index );
		}
	}
	void AddEntity( CBaseEntity *pEntity )
	{
		if ( pEntity->IsMarkedForDeletion() )
			return;
		m_updateList.AddToTail( pEntity );
	}

private:
	CUtlVector<CBaseEntity *>	m_updateList;
};

static CEntityTouchManager g_TouchManager;

void EntityTouch_Add( CBaseEntity *pEntity )
{
	g_TouchManager.AddEntity( pEntity );
}


void CEntityTouchManager::FrameUpdatePostEntityThink()
{
	VPROF( "CEntityTouchManager::FrameUpdatePostEntityThink" );
	// Loop through all entities again, checking their untouch if flagged to do so
	
	int count = m_updateList.Count();
	if ( count )
	{
		// copy off the list
		CBaseEntity **ents = (CBaseEntity **)stackalloc( sizeof(CBaseEntity *) * count );
		memcpy( ents, m_updateList.Base(), sizeof(CBaseEntity *) * count );
		// clear it
		m_updateList.RemoveAll();
		
		// now update those ents
		for ( int i = 0; i < count; i++ )
		{
			//Assert( ents[i]->GetCheckUntouch() );
			if ( ents[i]->GetCheckUntouch() )
			{
				ents[i]->PhysicsCheckForEntityUntouch();
			}
		}
		stackfree( ents );
	}
}

class CRespawnEntitiesFilter : public IMapEntityFilter
{
public:
	virtual bool ShouldCreateEntity( const char *pClassname )
	{
		// Create everything but the world
		return Q_stricmp( pClassname, "worldspawn" ) != 0;
	}

	virtual CBaseEntity* CreateNextEntity( const char *pClassname )
	{
		return gEntList.CreateEntityByName( pClassname );
	}
};

// One hook to rule them all...
// Since most of the little list managers in here only need one or two of the game
// system callbacks, this hook is a game system that passes them the appropriate callbacks
class CEntityListSystem : public CAutoGameSystemPerFrame
{
public:
	CEntityListSystem( char const *name ) : CAutoGameSystemPerFrame( name )
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
		CBaseEntityClassList *pClassList = s_pClassLists;
		while ( pClassList )
		{
			pClassList->LevelShutdownPostEntity();
			pClassList = pClassList->m_pNextClassList;
		}
	}

	void FrameUpdatePostEntityThink()
	{
		g_TouchManager.FrameUpdatePostEntityThink();

		if ( m_bRespawnAllEntities )
		{
			m_bRespawnAllEntities = false;

			// Don't change globalstate owing to deletion here
			GlobalEntity_EnableStateUpdates( false );

			// Remove all entities
			int nPlayerIndex = -1;
			CBaseEntity *pEnt = gEntList.FirstEnt();
			while ( pEnt )
			{
				CBaseEntity *pNextEnt = gEntList.NextEnt( pEnt );
				if ( pEnt->IsPlayer() )
				{
					nPlayerIndex = pEnt->entindex();
				}
				if ( !pEnt->IsEFlagSet( EFL_KEEP_ON_RECREATE_ENTITIES ) )
				{
					UTIL_Remove( pEnt );
				}
				pEnt = pNextEnt;
			}
			
			gEntList.CleanupDeleteList();

			GlobalEntity_EnableStateUpdates( true );

			// Allows us to immediately re-use the edict indices we just freed to avoid edict overflow
			//engine->AllowImmediateEdictReuse();

			// Reset node counter used during load
			CNodeEnt::m_nNodeCount = 0;

			CRespawnEntitiesFilter filter;
			MapEntity_ParseAllEntities( engine->GetMapEntitiesString(), &filter, true );

			// Allocate a CBasePlayer for pev, and call spawn
			if ( nPlayerIndex >= 0 )
			{
				CBaseEntity *pEdict = gEntList.GetBaseEntity( nPlayerIndex );
				ClientPutInServer(nPlayerIndex, "unnamed" );
				ClientActive(nPlayerIndex, false );

				CBasePlayer *pPlayer = ( CBasePlayer * )pEdict;
				SceneManager_ClientActive( pPlayer );
			}
		}
	}

	bool m_bRespawnAllEntities;
};

static CEntityListSystem g_EntityListSystem( "CEntityListSystem" );

//-----------------------------------------------------------------------------
// Respawns all entities in the level
//-----------------------------------------------------------------------------
void RespawnEntities()
{
	g_EntityListSystem.m_bRespawnAllEntities = true;
}

static ConCommand restart_entities( "respawn_entities", RespawnEntities, "Respawn all the entities in the map.", FCVAR_CHEAT | FCVAR_SPONLY );

class CSortedEntityList
{
public:
	CSortedEntityList() : m_sortedList(), m_emptyCount(0) {}

	typedef CBaseEntity *ENTITYPTR;
	class CEntityReportLess
	{
	public:
		bool Less( const ENTITYPTR &src1, const ENTITYPTR &src2, void *pCtx )
		{
			if ( stricmp( src1->GetClassname(), src2->GetClassname() ) < 0 )
				return true;
	
			return false;
		}
	};

	void AddEntityToList( CBaseEntity *pEntity )
	{
		if ( !pEntity )
		{
			m_emptyCount++;
		}
		else
		{
			m_sortedList.Insert( pEntity );
		}
	}
	void ReportEntityList()
	{
		const char *pLastClass = "";
		int count = 0;
		int edicts = 0;
		for ( int i = 0; i < m_sortedList.Count(); i++ )
		{
			CBaseEntity *pEntity = m_sortedList[i];
			if ( !pEntity )
				continue;

			if ( pEntity->entindex()!=-1 )
				edicts++;

			const char *pClassname = pEntity->GetClassname();
			if ( !FStrEq( pClassname, pLastClass ) )
			{
				if ( count )
				{
					Msg("Class: %s (%d)\n", pLastClass, count );
				}

				pLastClass = pClassname;
				count = 1;
			}
			else
				count++;
		}
		if ( pLastClass[0] != 0 && count )
		{
			Msg("Class: %s (%d)\n", pLastClass, count );
		}
		if ( m_sortedList.Count() )
		{
			Msg("Total %d entities (%d empty, %d edicts)\n", m_sortedList.Count(), m_emptyCount, edicts );
		}
	}
private:
	CUtlSortVector< CBaseEntity *, CEntityReportLess > m_sortedList;
	int		m_emptyCount;
};



CON_COMMAND(report_entities, "Lists all entities")
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	CSortedEntityList list;
	CBaseEntity *pEntity = gEntList.FirstEnt();
	while ( pEntity )
	{
		list.AddEntityToList( pEntity );
		pEntity = gEntList.NextEnt( pEntity );
	}
	list.ReportEntityList();
}


CON_COMMAND(report_touchlinks, "Lists all touchlinks")
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	CSortedEntityList list;
	CBaseEntity *pEntity = gEntList.FirstEnt();
	const char *pClassname = NULL;
	if ( args.ArgC() > 1 )
	{
		pClassname = args.Arg(1);
	}
	while ( pEntity )
	{
		if ( !pClassname || FClassnameIs(pEntity, pClassname) )
		{
			touchlink_t *root = ( touchlink_t * )pEntity->GetDataObject( TOUCHLINK );
			if ( root )
			{
				touchlink_t *link = root->nextLink;
				while ( link != root )
				{
					list.AddEntityToList( link->entityTouched );
					link = link->nextLink;
				}
			}
		}
		pEntity = gEntList.NextEnt( pEntity );
	}
	list.ReportEntityList();
}

CON_COMMAND(report_simthinklist, "Lists all simulating/thinking entities")
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	CBaseEntity *pTmp[NUM_ENT_ENTRIES];
	int count = SimThink_ListCopy( pTmp, ARRAYSIZE(pTmp) );

	CSortedEntityList list;
	for ( int i = 0; i < count; i++ )
	{
		if ( !pTmp[i] )
			continue;

		list.AddEntityToList( pTmp[i] );
	}
	list.ReportEntityList();
}

