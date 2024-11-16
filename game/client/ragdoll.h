//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#ifndef RAGDOLL_H
#define RAGDOLL_H

#ifdef _WIN32
#pragma once
#endif

#include "ragdoll_shared.h"
#include "c_baseanimating.h"

#define RAGDOLL_VISUALIZE	0

class C_BaseEntity;
class IStudioHdr;
struct mstudiobone_t;
class Vector;
class IPhysicsObject;
class CBoneAccessor;

//abstract_class IRagdoll
//{
//public:
//	virtual ~IRagdoll() {}
//
//
//};

//class CRagdoll : public IRagdoll
//{
//public:
//	CRagdoll();
//	~CRagdoll( void );
//
//	
//
//
//
//
//
//
//public:
//	
//};


//CRagdoll *CreateRagdoll( 
//	C_BaseEntity *ent, 
//	IStudioHdr *pstudiohdr, 
//	const Vector &forceVector, 
//	int forceBone, 
//	const matrix3x4_t *pDeltaBones0, 
//	const matrix3x4_t *pDeltaBones1, 
//	const matrix3x4_t *pCurrentBonePosition, 
//	float boneDt,
//	bool bFixedConstraints=false );


// save this ragdoll's creation as the current tick
void NoteRagdollCreationTick( C_BaseEntity *pRagdoll );
// returns true if the ragdoll was created on this tick
bool WasRagdollCreatedOnCurrentTick( C_BaseEntity *pRagdoll );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class C_ServerRagdoll : public C_BaseAnimating
{
public:
	DECLARE_CLASS(C_ServerRagdoll, C_BaseAnimating);
	DECLARE_CLIENTCLASS();
	DECLARE_INTERPOLATION();

	C_ServerRagdoll(void);

	bool Init(int entnum, int iSerialNum);

	virtual void PostDataUpdate(DataUpdateType_t updateType);

	virtual int InternalDrawModel(int flags);
	virtual IStudioHdr* OnNewModel(void);
	virtual unsigned char GetClientSideFade();
	virtual void	SetupWeights(const matrix3x4_t* pBoneToWorld, int nFlexWeightCount, float* pFlexWeights, float* pFlexDelayedWeights);

	void GetRenderBounds(Vector& theMins, Vector& theMaxs);
	virtual void AddEntity(void);
	virtual void AccumulateLayers(IBoneSetup& boneSetup, Vector pos[], Quaternion q[], float currentTime);
	virtual void BuildTransformations(IStudioHdr* pStudioHdr, Vector* pos, Quaternion q[], const matrix3x4_t& cameraTransform, int boneMask, CBoneBitList& boneComputed);
	IPhysicsObject* GetElement(int elementNum);
	virtual void UpdateOnRemove();
	virtual float LastBoneChangedTime();







private:
	C_ServerRagdoll(const C_ServerRagdoll& src);

	typedef CHandle<C_BaseAnimating> CBaseAnimatingHandle;
	//CNetworkVar( CBaseAnimatingHandle, m_hUnragdoll );
	CNetworkVar(float, m_flBlendWeight);
	float m_flBlendWeightCurrent;
	CNetworkVar(int, m_nOverlaySequence);
};

#endif // RAGDOLL_H