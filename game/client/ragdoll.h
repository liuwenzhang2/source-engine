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

//#include "ragdoll_shared.h"
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
	IPhysicsObject* GetElement(int elementNum);
	virtual void UpdateOnRemove();

private:
	C_ServerRagdoll(const C_ServerRagdoll& src);

	typedef CHandle<C_BaseAnimating> CBaseAnimatingHandle;
	//CNetworkVar( CBaseAnimatingHandle, m_hUnragdoll );
	CNetworkVar(float, m_flBlendWeight);
};

enum
{
	RAGDOLL_FRICTION_OFF = -2,
	RAGDOLL_FRICTION_NONE,
	RAGDOLL_FRICTION_IN,
	RAGDOLL_FRICTION_HOLD,
	RAGDOLL_FRICTION_OUT,
};

class C_ClientRagdoll : public C_BaseAnimating, public IPVSNotify
{

public:
	C_ClientRagdoll();//bool bRestoring 
	DECLARE_CLASS(C_ClientRagdoll, C_BaseAnimating);
	DECLARE_DATADESC();

	bool Init(int entnum, int iSerialNum);

	void IgniteRagdoll(C_BaseEntity* pSource);
	void TransferDissolveFrom(C_BaseEntity* pSource);
	// inherited from IPVSNotify
	virtual void OnPVSStatusChanged(bool bInPVS);

	virtual void SetupWeights(const matrix3x4_t* pBoneToWorld, int nFlexWeightCount, float* pFlexWeights, float* pFlexDelayedWeights);
	virtual void ImpactTrace(trace_t* pTrace, int iDamageType, const char* pCustomImpactName);
	void ClientThink(void);
	void ReleaseRagdoll(void) { m_bReleaseRagdoll = true; }
	bool ShouldSavePhysics(void) { return true; }
	virtual void	OnSave();
	virtual void	OnRestore();
	virtual int ObjectCaps(void) { return BaseClass::ObjectCaps() | FCAP_SAVE_NON_NETWORKABLE; }
	virtual IPVSNotify* GetPVSNotifyInterface() { return this; }

	void	HandleAnimatedFriction(void);
	virtual void SUB_Remove(void);

	void	FadeOut(void);

	bool m_bFadeOut;
	bool m_bImportant;
	float m_flEffectTime;

private:
	int m_iCurrentFriction;
	int m_iMinFriction;
	int m_iMaxFriction;
	float m_flFrictionModTime;
	float m_flFrictionTime;

	int  m_iFrictionAnimState;
	bool m_bReleaseRagdoll;

	bool m_bFadingOut;

	float m_flScaleEnd[NUM_HITBOX_FIRES];
	float m_flScaleTimeStart[NUM_HITBOX_FIRES];
	float m_flScaleTimeEnd[NUM_HITBOX_FIRES];
};

#endif // RAGDOLL_H