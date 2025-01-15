//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "mathlib/vmatrix.h"
//#include "ragdoll_shared.h"
#include "ragdoll.h"
#include "bone_setup.h"
#include "materialsystem/imesh.h"
#include "engine/ivmodelinfo.h"
#include "iviewrender.h"
#include "tier0/vprof.h"
#include "physics_saverestore.h"
#include "vphysics/constraints.h"
#include "engine/ivdebugoverlay.h"
#include "c_entitydissolve.h"
#include "c_fire_smoke.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _DEBUG
extern ConVar r_FadeProps;
#endif

//CRagdoll::CRagdoll()
//{
//
//}
//
//CRagdoll::~CRagdoll(void)
//{
//
//}

//CRagdoll* CreateRagdoll(
//	C_BaseEntity* ent,
//	IStudioHdr* pstudiohdr,
//	const Vector& forceVector,
//	int forceBone,
//	const matrix3x4_t* pDeltaBones0,
//	const matrix3x4_t* pDeltaBones1,
//	const matrix3x4_t* pCurrentBonePosition,
//	float dt,
//	bool bFixedConstraints)
//{
//	CRagdoll* pRagdoll = new CRagdoll;
//	pRagdoll->Init(ent, pstudiohdr, forceVector, forceBone, pDeltaBones0, pDeltaBones1, pCurrentBonePosition, dt, bFixedConstraints);
//
//	if (!pRagdoll->IsValid())
//	{
//		Msg("Bad ragdoll for %s\n", pstudiohdr->pszName());
//		delete pRagdoll;
//		pRagdoll = NULL;
//	}
//	return pRagdoll;
//}




EXTERN_RECV_TABLE(DT_Ragdoll);
IMPLEMENT_CLIENTCLASS_DT(C_ServerRagdoll, DT_Ragdoll, CRagdollProp)

	//RecvPropEHandle(RECVINFO(m_hUnragdoll)),
	RecvPropFloat(RECVINFO(m_flBlendWeight)),
END_RECV_TABLE()


C_ServerRagdoll::C_ServerRagdoll( void )

{
	m_flBlendWeight = 0.0f;
	m_flFadeScale = 1;
}

bool C_ServerRagdoll::Init(int entnum, int iSerialNum) {
	bool ret = BaseClass::Init(entnum, iSerialNum);

	return ret;
}

void C_ServerRagdoll::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate( updateType );
}

int C_ServerRagdoll::InternalDrawModel( int flags )
{
	int ret = BaseClass::InternalDrawModel( flags );
	if ( vcollide_wireframe.GetBool() )
	{
		vcollide_t *pCollide = modelinfo->GetVCollide(GetEngineObject()->GetModelIndex() );
		IMaterial *pWireframe = materials->FindMaterial("shadertest/wireframevertexcolor", TEXTURE_GROUP_OTHER);

		matrix3x4_t matrix;
		for ( int i = 0; i < GetEngineObject()->GetElementCount(); i++ )
		{
			static color32 debugColor = {0,255,255,0};

			AngleMatrix(GetEngineObject()->GetRagAngles(i), GetEngineObject()->GetRagPos(i), matrix );
			engine->DebugDrawPhysCollide( pCollide->solids[i], pWireframe, matrix, debugColor );
		}
	}
	return ret;
}


IStudioHdr *C_ServerRagdoll::OnNewModel( void )
{
	IStudioHdr *hdr = BaseClass::OnNewModel();



	return hdr;
}

//-----------------------------------------------------------------------------
// Purpose: clear out any face/eye values stored in the material system
//-----------------------------------------------------------------------------
void C_ServerRagdoll::SetupWeights( const matrix3x4_t *pBoneToWorld, int nFlexWeightCount, float *pFlexWeights, float *pFlexDelayedWeights )
{
	BaseClass::SetupWeights( pBoneToWorld, nFlexWeightCount, pFlexWeights, pFlexDelayedWeights );

	IStudioHdr *hdr = GetEngineObject()->GetModelPtr();
	if ( !hdr )
		return;

	int nFlexDescCount = hdr->numflexdesc();
	if ( nFlexDescCount )
	{
		Assert( !pFlexDelayedWeights );
		memset( pFlexWeights, 0, nFlexWeightCount * sizeof(float) );
	}

	if ( m_iEyeAttachment > 0 )
	{
		matrix3x4_t attToWorld;
		if (GetEngineObject()->GetAttachment( m_iEyeAttachment, attToWorld ))
		{
			Vector local, tmp;
			local.Init( 1000.0f, 0.0f, 0.0f );
			VectorTransform( local, attToWorld, tmp );
			modelrender->SetViewTarget(GetEngineObject()->GetModelPtr(), GetEngineObject()->GetBody(), tmp );
		}
	}
}


void C_ServerRagdoll::GetRenderBounds( Vector& theMins, Vector& theMaxs )
{
	if( !GetEngineObject()->IsBoundsDefinedInEntitySpace() )
	{
		IRotateAABB(GetEngineObject()->EntityToWorldTransform(), GetEngineObject()->OBBMins(), GetEngineObject()->OBBMaxs(), theMins, theMaxs );
	}
	else
	{
		theMins = GetEngineObject()->OBBMins();
		theMaxs = GetEngineObject()->OBBMaxs();
	}
}

void C_ServerRagdoll::AddEntity( void )
{
	BaseClass::AddEntity();

	// Move blend weight toward target over 0.2 seconds
	GetEngineObject()->SetBlendWeightCurrent(Approach( m_flBlendWeight, GetEngineObject()->GetBlendWeightCurrent(), gpGlobals->frametime * 5.0f));
}

void C_ServerRagdoll::AccumulateLayers( IBoneSetup &boneSetup, Vector pos[], Quaternion q[], float currentTime )
{
	BaseClass::AccumulateLayers( boneSetup, pos, q, currentTime );

	if (GetEngineObject()->GetOverlaySequence() >= 0 && GetEngineObject()->GetOverlaySequence() < boneSetup.GetStudioHdr()->GetNumSeq())
	{
		boneSetup.AccumulatePose( pos, q, GetEngineObject()->GetOverlaySequence(), GetEngineObject()->GetCycle(), GetEngineObject()->GetBlendWeightCurrent(), currentTime, GetEngineObject()->GetIk());
	}
}

IPhysicsObject *C_ServerRagdoll::GetElement( int elementNum ) 
{ 
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : 	virtual void
//-----------------------------------------------------------------------------
void C_ServerRagdoll::UpdateOnRemove()
{
	//C_BaseAnimating *anim = m_hUnragdoll.Get();
	//if ( NULL != anim && 
	//	anim->GetModel() && 
	//	( anim->GetModel() == GetModel() ) )
	//{
	//	// Need to tell C_BaseAnimating to blend out of the ragdoll data that we received last
	//	C_BaseAnimating::AutoAllowBoneAccess boneaccess( true, false );
	//	anim->GetEngineObject()->CreateUnragdollInfo(this);
	//}

	// Do last to mimic destrictor order
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Fade out
//-----------------------------------------------------------------------------
unsigned char C_ServerRagdoll::GetClientSideFade()
{
	return UTIL_ComputeEntityFade( this, m_fadeMinDist, m_fadeMaxDist, m_flFadeScale );
}

static int GetHighestBit( int flags )
{
	for ( int i = 31; i >= 0; --i )
	{
		if ( flags & (1<<i) )
			return (1<<i);
	}

	return 0;
}

#define ATTACH_INTERP_TIME	0.2
class C_ServerRagdollAttached : public C_ServerRagdoll
{
	DECLARE_CLASS( C_ServerRagdollAttached, C_ServerRagdoll );
public:
	C_ServerRagdollAttached( void ) 
	{
		m_bHasParent = false;
		m_vecOffset.Init();
	}
	DECLARE_CLIENTCLASS();
	bool SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime )
	{
		if (GetEngineObject()->GetMoveParent() )
		{
			// HACKHACK: Force the attached bone to be set up
			int index = GetEngineObject()->GetBoneIndex(m_ragdollAttachedObjectIndex);
			int boneFlags = GetEngineObject()->GetModelPtr()->boneFlags(index);
			if ( !(boneFlags & boneMask) )
			{
				// BUGBUG: The attached bone is required and this call is going to skip it, so force it
				// HACKHACK: Assume the highest bit numbered bone flag is the minimum bone set
				boneMask |= GetHighestBit( boneFlags );
			}
		}
		return GetEngineObject()->SetupBones( pBoneToWorldOut, nMaxBones, boneMask, currentTime );
	}

	virtual void BeforeBuildTransformations( IStudioHdr *hdr, Vector *pos, Quaternion q[], const matrix3x4_t& cameraTransform, int boneMask, CBoneBitList &boneComputed )
	{
		VPROF_BUDGET( "C_ServerRagdollAttached::SetupBones", VPROF_BUDGETGROUP_CLIENT_ANIMATION );

		if ( !hdr )
			return;

		float frac = RemapVal( gpGlobals->curtime, m_parentTime, m_parentTime+ATTACH_INTERP_TIME, 0, 1 );
		frac = clamp( frac, 0.f, 1.f );
		// interpolate offset over some time
		offset = m_vecOffset * (1-frac);

		C_BaseEntity* parent = GetEngineObject()->GetMoveParent() ? (C_BaseEntity*)GetEngineObject()->GetMoveParent()->GetOuter() : NULL;
		worldOrigin.Init();


		if ( parent->GetEngineObject()->GetModelPtr() )
		{
			Assert( parent != this );
			parent->GetEngineObject()->SetupBones( NULL, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime );

			const matrix3x4_t& boneToWorld = parent->GetEngineObject()->GetBone( m_boneIndexAttached );
			VectorTransform( m_attachmentPointBoneSpace, boneToWorld, worldOrigin );
		}
		//BaseClass::BuildTransformations( hdr, pos, q, cameraTransform, boneMask, boneComputed );


	}

	virtual void AfterBuildTransformations(IStudioHdr* hdr, Vector* pos, Quaternion q[], const matrix3x4_t& cameraTransform, int boneMask, CBoneBitList& boneComputed)
	{
		VPROF_BUDGET("C_ServerRagdollAttached::SetupBones", VPROF_BUDGETGROUP_CLIENT_ANIMATION);

		if (!hdr)
			return;

		
		//BaseClass::BuildTransformations(hdr, pos, q, cameraTransform, boneMask, boneComputed);
		C_BaseEntity* parent = GetEngineObject()->GetMoveParent() ? (C_BaseEntity*)GetEngineObject()->GetMoveParent()->GetOuter() : NULL;
		if (parent->GetEngineObject()->GetModelPtr())
		{
			int index = GetEngineObject()->GetBoneIndex(m_ragdollAttachedObjectIndex);
			const matrix3x4_t& matrix = GetEngineObject()->GetBone(index);
			Vector ragOrigin;
			VectorTransform(m_attachmentPointRagdollSpace, matrix, ragOrigin);
			offset = worldOrigin - ragOrigin;
			// fixes culling
			GetEngineObject()->SetAbsOrigin(worldOrigin);
			m_vecOffset = offset;
		}

		for (int i = 0; i < hdr->numbones(); i++)
		{
			if (!(hdr->boneFlags(i) & boneMask))
				continue;

			Vector pos;
			matrix3x4_t& matrix = GetEngineObject()->GetBoneForWrite(i);
			MatrixGetColumn(matrix, 3, pos);
			pos += offset;
			MatrixSetColumn(pos, 3, matrix);
		}
	}

	void OnDataChanged( DataUpdateType_t updateType );
	virtual float LastBoneChangedTime() { return FLT_MAX; }

	Vector		m_attachmentPointBoneSpace;
	Vector		m_vecOffset;
	Vector		m_attachmentPointRagdollSpace;
	int			m_ragdollAttachedObjectIndex;
	int			m_boneIndexAttached;
	float		m_parentTime;
	bool		m_bHasParent;
	Vector offset;
	Vector worldOrigin;

private:
	C_ServerRagdollAttached( const C_ServerRagdollAttached & );
};

EXTERN_RECV_TABLE(DT_Ragdoll_Attached);
IMPLEMENT_CLIENTCLASS_DT(C_ServerRagdollAttached, DT_Ragdoll_Attached, CRagdollPropAttached)
	RecvPropInt( RECVINFO( m_boneIndexAttached ) ),
	RecvPropInt( RECVINFO( m_ragdollAttachedObjectIndex ) ),
	RecvPropVector(RECVINFO(m_attachmentPointBoneSpace) ),
	RecvPropVector(RECVINFO(m_attachmentPointRagdollSpace) ),
END_RECV_TABLE()

void C_ServerRagdollAttached::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	bool bParentNow = GetEngineObject()->GetMoveParent() ? true : false;
	if ( m_bHasParent != bParentNow )
	{
		if ( m_bHasParent )
		{
			m_parentTime = gpGlobals->curtime;
		}
		m_bHasParent = bParentNow;
	}
}


C_EntityFlame* FireEffect(C_BaseAnimating* pTarget, C_BaseEntity* pServerFire, float* flScaleEnd, float* flTimeStart, float* flTimeEnd)
{
	C_EntityFlame* pFire = (C_EntityFlame*)EntityList()->CreateEntityByName("C_EntityFlame");

	if (pFire->InitializeAsClientEntity(NULL, RENDER_GROUP_TRANSLUCENT_ENTITY) == false)
	{
		EntityList()->DestroyEntity(pFire);// ->Release();
		return NULL;
	}

	if (pFire != NULL)
	{
		pFire->GetEngineObject()->RemoveFromLeafSystem();

		pTarget->GetEngineObject()->AddFlag(FL_ONFIRE);
		pFire->GetEngineObject()->SetParent(pTarget->GetEngineObject());
		pFire->m_hEntAttached = (C_BaseEntity*)pTarget;

		pFire->OnDataChanged(DATA_UPDATE_CREATED);
		pFire->GetEngineObject()->SetAbsOrigin(pTarget->GetEngineObject()->GetAbsOrigin());

#ifdef HL2_EPISODIC
		if (pServerFire)
		{
			if (pServerFire->GetEngineObject()->IsEffectActive(EF_DIMLIGHT))
			{
				pFire->GetEngineObject()->AddEffects(EF_DIMLIGHT);
			}
			if (pServerFire->GetEngineObject()->IsEffectActive(EF_BRIGHTLIGHT))
			{
				pFire->GetEngineObject()->AddEffects(EF_BRIGHTLIGHT);
			}
		}
#endif

		//Play a sound
		CPASAttenuationFilter filter(pTarget);
		g_pSoundEmitterSystem->EmitSound(filter, pTarget->GetSoundSourceIndex(), "General.BurningFlesh");//pTarget->

		pFire->SetNextClientThink(gpGlobals->curtime + 7.0f);
	}

	return pFire;
}

#define DEFAULT_FADE_START 2.0f
#define DEFAULT_MODEL_FADE_START 1.9f
#define DEFAULT_MODEL_FADE_LENGTH 0.1f
#define DEFAULT_FADEIN_LENGTH 1.0f

C_EntityDissolve* DissolveEffect(C_BaseEntity* pTarget, float flTime)
{
	C_EntityDissolve* pDissolve = (C_EntityDissolve*)EntityList()->CreateEntityByName("C_EntityDissolve");

	if (pDissolve->InitializeAsClientEntity("sprites/blueglow1.vmt", RENDER_GROUP_TRANSLUCENT_ENTITY) == false)
	{
		EntityList()->DestroyEntity(pDissolve);// ->Release();
		return NULL;
	}

	if (pDissolve != NULL)
	{
		pTarget->GetEngineObject()->AddFlag(FL_DISSOLVING);
		pDissolve->GetEngineObject()->SetParent(pTarget->GetEngineObject());
		pDissolve->OnDataChanged(DATA_UPDATE_CREATED);
		pDissolve->GetEngineObject()->SetAbsOrigin(pTarget->GetEngineObject()->GetAbsOrigin());

		pDissolve->m_flStartTime = flTime;
		pDissolve->m_flFadeOutStart = DEFAULT_FADE_START;
		pDissolve->m_flFadeOutModelStart = DEFAULT_MODEL_FADE_START;
		pDissolve->m_flFadeOutModelLength = DEFAULT_MODEL_FADE_LENGTH;
		pDissolve->m_flFadeInLength = DEFAULT_FADEIN_LENGTH;

		pDissolve->m_nDissolveType = 0;
		pDissolve->m_flNextSparkTime = 0.0f;
		pDissolve->m_flFadeOutLength = 0.0f;
		pDissolve->m_flFadeInStart = 0.0f;

		// Let this entity know it needs to delete itself when it's done
		pDissolve->SetServerLinkState(false);
		pTarget->SetEffectEntity(pDissolve);
	}

	return pDissolve;

}

LINK_ENTITY_TO_CLASS(client_ragdoll, C_ClientRagdoll);

BEGIN_DATADESC(C_ClientRagdoll)
DEFINE_FIELD(m_bFadeOut, FIELD_BOOLEAN),
DEFINE_FIELD(m_bImportant, FIELD_BOOLEAN),
DEFINE_FIELD(m_iCurrentFriction, FIELD_INTEGER),
DEFINE_FIELD(m_iMinFriction, FIELD_INTEGER),
DEFINE_FIELD(m_iMaxFriction, FIELD_INTEGER),
DEFINE_FIELD(m_flFrictionModTime, FIELD_FLOAT),
DEFINE_FIELD(m_flFrictionTime, FIELD_TIME),
DEFINE_FIELD(m_iFrictionAnimState, FIELD_INTEGER),
DEFINE_FIELD(m_bReleaseRagdoll, FIELD_BOOLEAN),
//DEFINE_FIELD( m_nBody, FIELD_INTEGER ),
//DEFINE_FIELD( m_nSkin, FIELD_INTEGER ),
//DEFINE_FIELD( m_nRenderFX, FIELD_CHARACTER ),
DEFINE_FIELD(m_nRenderMode, FIELD_CHARACTER),
DEFINE_FIELD(m_clrRender, FIELD_COLOR32),
DEFINE_FIELD(m_flEffectTime, FIELD_TIME),
DEFINE_FIELD(m_bFadingOut, FIELD_BOOLEAN),

DEFINE_AUTO_ARRAY(m_flScaleEnd, FIELD_FLOAT),
DEFINE_AUTO_ARRAY(m_flScaleTimeStart, FIELD_FLOAT),
DEFINE_AUTO_ARRAY(m_flScaleTimeEnd, FIELD_FLOAT),
//DEFINE_EMBEDDEDBYREF( m_pRagdoll ),

END_DATADESC()

C_ClientRagdoll::C_ClientRagdoll()//bool bRestoring 
{
	m_iCurrentFriction = 0;
	m_iFrictionAnimState = RAGDOLL_FRICTION_NONE;
	m_bReleaseRagdoll = false;
	m_bFadeOut = false;
	m_bFadingOut = false;
	m_bImportant = false;
	m_bNoModelParticles = false;

	//if ( bRestoring == true )
	//{
	//	m_pRagdoll = new CRagdoll;
	//}
}

bool C_ClientRagdoll::Init(int entnum, int iSerialNum) {
	GetEngineObject()->SetClassname("client_ragdoll");
	return true;
}

void C_ClientRagdoll::OnSave(void)
{
}

void C_ClientRagdoll::OnRestore(void)
{
	ragdoll_t* pRagdollT = GetEngineObject()->GetRagdoll();
	if (pRagdollT == NULL || pRagdollT->list[0].pObject == NULL)
	{
		m_bReleaseRagdoll = true;
		//m_pRagdoll = NULL;
		Error("Attempted to restore a ragdoll without physobjects!");
		Assert(!"Attempted to restore a ragdoll without physobjects!");
		return;
	}

	if (GetEngineObject()->GetFlags() & FL_DISSOLVING)
	{
		DissolveEffect(this, m_flEffectTime);
	}
	else if (GetEngineObject()->GetFlags() & FL_ONFIRE)
	{
		C_EntityFlame* pFireChild = dynamic_cast<C_EntityFlame*>(GetEffectEntity());
		C_EntityFlame* pNewFireChild = FireEffect(this, pFireChild, m_flScaleEnd, m_flScaleTimeStart, m_flScaleTimeEnd);

		//Set the new fire child as the new effect entity.
		SetEffectEntity(pNewFireChild);
	}

	SetNextClientThink(CLIENT_THINK_ALWAYS);
	if (m_bFadeOut == true)
	{
		EntityList()->MoveToTopOfLRU(this, m_bImportant);
	}
	NoteRagdollCreationTick(this);
	BaseClass::OnRestore();
}

void C_ClientRagdoll::ImpactTrace(trace_t* pTrace, int iDamageType, const char* pCustomImpactName)
{
	VPROF("C_ClientRagdoll::ImpactTrace");

	IPhysicsObject* pPhysicsObject = GetEngineObject()->VPhysicsGetObject();

	if (!pPhysicsObject)
		return;

	Vector dir = pTrace->endpos - pTrace->startpos;

	if (iDamageType == DMG_BLAST)
	{
		dir *= 500;  // adjust impact strenght

		// apply force at object mass center
		pPhysicsObject->ApplyForceCenter(dir);
	}
	else
	{
		Vector hitpos;

		VectorMA(pTrace->startpos, pTrace->fraction, dir, hitpos);
		VectorNormalize(dir);

		dir *= 4000;  // adjust impact strenght

		// apply force where we hit it
		pPhysicsObject->ApplyForceOffset(dir, hitpos);
	}

	GetEngineObject()->ResetRagdollSleepAfterTime();
}

ConVar g_debug_ragdoll_visualize("g_debug_ragdoll_visualize", "0", FCVAR_CHEAT);

void C_ClientRagdoll::HandleAnimatedFriction(void)
{
	if (m_iFrictionAnimState == RAGDOLL_FRICTION_OFF)
		return;

	ragdoll_t* pRagdollT = NULL;
	int iBoneCount = 0;

	if (GetEngineObject()->RagdollBoneCount())
	{
		pRagdollT = GetEngineObject()->GetRagdoll();
		iBoneCount = GetEngineObject()->RagdollBoneCount();

	}

	if (pRagdollT == NULL)
		return;

	switch (m_iFrictionAnimState)
	{
	case RAGDOLL_FRICTION_NONE:
	{
		m_iMinFriction = pRagdollT->animfriction.iMinAnimatedFriction;
		m_iMaxFriction = pRagdollT->animfriction.iMaxAnimatedFriction;

		if (m_iMinFriction != 0 || m_iMaxFriction != 0)
		{
			m_iFrictionAnimState = RAGDOLL_FRICTION_IN;

			m_flFrictionModTime = pRagdollT->animfriction.flFrictionTimeIn;
			m_flFrictionTime = gpGlobals->curtime + m_flFrictionModTime;

			m_iCurrentFriction = m_iMinFriction;
		}
		else
		{
			m_iFrictionAnimState = RAGDOLL_FRICTION_OFF;
		}

		break;
	}

	case RAGDOLL_FRICTION_IN:
	{
		float flDeltaTime = (m_flFrictionTime - gpGlobals->curtime);

		m_iCurrentFriction = RemapValClamped(flDeltaTime, m_flFrictionModTime, 0, m_iMinFriction, m_iMaxFriction);

		if (flDeltaTime <= 0.0f)
		{
			m_flFrictionModTime = pRagdollT->animfriction.flFrictionTimeHold;
			m_flFrictionTime = gpGlobals->curtime + m_flFrictionModTime;
			m_iFrictionAnimState = RAGDOLL_FRICTION_HOLD;
		}
		break;
	}

	case RAGDOLL_FRICTION_HOLD:
	{
		if (m_flFrictionTime < gpGlobals->curtime)
		{
			m_flFrictionModTime = pRagdollT->animfriction.flFrictionTimeOut;
			m_flFrictionTime = gpGlobals->curtime + m_flFrictionModTime;
			m_iFrictionAnimState = RAGDOLL_FRICTION_OUT;
		}

		break;
	}

	case RAGDOLL_FRICTION_OUT:
	{
		float flDeltaTime = (m_flFrictionTime - gpGlobals->curtime);

		m_iCurrentFriction = RemapValClamped(flDeltaTime, 0, m_flFrictionModTime, m_iMinFriction, m_iMaxFriction);

		if (flDeltaTime <= 0.0f)
		{
			m_iFrictionAnimState = RAGDOLL_FRICTION_OFF;
		}

		break;
	}
	}

	for (int i = 0; i < iBoneCount; i++)
	{
		if (pRagdollT->list[i].pConstraint)
			pRagdollT->list[i].pConstraint->SetAngularMotor(0, m_iCurrentFriction);
	}

	IPhysicsObject* pPhysicsObject = GetEngineObject()->VPhysicsGetObject();

	if (pPhysicsObject)
	{
		pPhysicsObject->Wake();
	}
}

ConVar g_ragdoll_fadespeed("g_ragdoll_fadespeed", "600");
ConVar g_ragdoll_lvfadespeed("g_ragdoll_lvfadespeed", "100");

void C_ClientRagdoll::OnPVSStatusChanged(bool bInPVS)
{
	if (bInPVS)
	{
		GetEngineObject()->CreateShadow();
	}
	else
	{
		GetEngineObject()->DestroyShadow();
	}
}

void C_ClientRagdoll::FadeOut(void)
{
	if (m_bFadingOut == false)
	{
		return;
	}

	int iAlpha = GetRenderColor().a;
	int iFadeSpeed = (g_RagdollLVManager.IsLowViolence()) ? g_ragdoll_lvfadespeed.GetInt() : g_ragdoll_fadespeed.GetInt();

	iAlpha = MAX(iAlpha - (iFadeSpeed * gpGlobals->frametime), 0);

	SetRenderMode(kRenderTransAlpha);
	SetRenderColorA(iAlpha);

	if (iAlpha == 0)
	{
		m_bReleaseRagdoll = true;
	}
}

void C_ClientRagdoll::SUB_Remove(void)
{
	m_bFadingOut = true;
	SetNextClientThink(CLIENT_THINK_ALWAYS);
}

void C_ClientRagdoll::IgniteRagdoll(C_BaseEntity* pSource)
{
	C_BaseEntity* pChild = pSource->GetEffectEntity();

	if (pChild)
	{
		C_EntityFlame* pFireChild = dynamic_cast<C_EntityFlame*>(pChild);
		C_ClientRagdoll* pRagdoll = dynamic_cast<C_ClientRagdoll*> (this);

		if (pFireChild)
		{
			pRagdoll->SetEffectEntity(FireEffect(pRagdoll, pFireChild, NULL, NULL, NULL));
		}
	}
}

void C_ClientRagdoll::TransferDissolveFrom(C_BaseEntity* pSource)
{
	C_BaseEntity* pChild = pSource->GetEffectEntity();

	if (pChild)
	{
		C_EntityDissolve* pDissolveChild = dynamic_cast<C_EntityDissolve*>(pChild);

		if (pDissolveChild)
		{
			C_ClientRagdoll* pRagdoll = dynamic_cast<C_ClientRagdoll*> (this);

			if (pRagdoll)
			{
				pRagdoll->m_flEffectTime = pDissolveChild->m_flStartTime;

				C_EntityDissolve* pDissolve = DissolveEffect(pRagdoll, pRagdoll->m_flEffectTime);

				if (pDissolve)
				{
					pDissolve->SetRenderMode(pDissolveChild->GetRenderMode());
					pDissolve->GetEngineObject()->SetRenderFX(pDissolveChild->GetEngineObject()->GetRenderFX());
					pDissolve->SetRenderColor(255, 255, 255, 255);
					pDissolveChild->SetRenderColorA(0);

					pDissolve->m_vDissolverOrigin = pDissolveChild->m_vDissolverOrigin;
					pDissolve->m_nDissolveType = pDissolveChild->m_nDissolveType;

					if (pDissolve->m_nDissolveType == ENTITY_DISSOLVE_CORE)
					{
						pDissolve->m_nMagnitude = pDissolveChild->m_nMagnitude;
						pDissolve->m_flFadeOutStart = CORE_DISSOLVE_FADE_START;
						pDissolve->m_flFadeOutModelStart = CORE_DISSOLVE_MODEL_FADE_START;
						pDissolve->m_flFadeOutModelLength = CORE_DISSOLVE_MODEL_FADE_LENGTH;
						pDissolve->m_flFadeInLength = CORE_DISSOLVE_FADEIN_LENGTH;
					}
				}
			}
		}
	}
}

void C_ClientRagdoll::ClientThink(void)
{
	if (m_bReleaseRagdoll == true)
	{
		//DestroyBoneAttachments();
		EntityList()->DestroyEntity(this);// Release();
		return;
	}

	if (g_debug_ragdoll_visualize.GetBool())
	{
		Vector vMins, vMaxs;

		Vector origin = GetEngineObject()->GetRagdollOrigin();
		GetEngineObject()->GetRagdollBounds(vMins, vMaxs);

		debugoverlay->AddBoxOverlay(origin, vMins, vMaxs, QAngle(0, 0, 0), 0, 255, 0, 16, 0);
	}

	HandleAnimatedFriction();

	FadeOut();
}


//-----------------------------------------------------------------------------
// Purpose: clear out any face/eye values stored in the material system
//-----------------------------------------------------------------------------
void C_ClientRagdoll::SetupWeights(const matrix3x4_t* pBoneToWorld, int nFlexWeightCount, float* pFlexWeights, float* pFlexDelayedWeights)
{
	BaseClass::SetupWeights(pBoneToWorld, nFlexWeightCount, pFlexWeights, pFlexDelayedWeights);

	IStudioHdr* hdr = GetEngineObject()->GetModelPtr();
	if (!hdr)
		return;

	int nFlexDescCount = hdr->numflexdesc();
	if (nFlexDescCount)
	{
		Assert(!pFlexDelayedWeights);
		memset(pFlexWeights, 0, nFlexWeightCount * sizeof(float));
	}

	if (m_iEyeAttachment > 0)
	{
		matrix3x4_t attToWorld;
		if (GetEngineObject()->GetAttachment(m_iEyeAttachment, attToWorld))
		{
			Vector local, tmp;
			local.Init(1000.0f, 0.0f, 0.0f);
			VectorTransform(local, attToWorld, tmp);
			modelrender->SetViewTarget(GetEngineObject()->GetModelPtr(), GetEngineObject()->GetBody(), tmp);
		}
	}
}

struct ragdoll_remember_t
{
	C_BaseEntity	*ragdoll;
	int				tickCount;
};

struct ragdoll_memory_list_t
{
	CUtlVector<ragdoll_remember_t>	list;

	int tickCount;

	void Update()
	{
		if ( tickCount > gpGlobals->tickcount )
		{
			list.RemoveAll();
			return;
		}

		for ( int i = list.Count()-1; i >= 0; --i )
		{
			if ( list[i].tickCount != gpGlobals->tickcount )
			{
				list.FastRemove(i);
			}
		}
	}

	bool IsInList( C_BaseEntity *pRagdoll )
	{
		for ( int i = list.Count()-1; i >= 0; --i )
		{
			if ( list[i].ragdoll == pRagdoll )
				return true;
		}

		return false;
	}
	void AddToList( C_BaseEntity *pRagdoll )
	{
		Update();
		int index = list.AddToTail();
		list[index].ragdoll = pRagdoll;
		list[index].tickCount = gpGlobals->tickcount;
	}
};

static ragdoll_memory_list_t gRagdolls;

void NoteRagdollCreationTick( C_BaseEntity *pRagdoll )
{
	gRagdolls.AddToList( pRagdoll );
}

// returns true if the ragdoll was created on this tick
bool WasRagdollCreatedOnCurrentTick( C_BaseEntity *pRagdoll )
{
	gRagdolls.Update();
	return gRagdolls.IsInList( pRagdoll );
}

