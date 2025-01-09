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

		C_BaseAnimating* parent = GetEngineObject()->GetMoveParent() ? ((C_BaseEntity*)GetEngineObject()->GetMoveParent()->GetOuter())->GetBaseAnimating() : NULL;
		worldOrigin.Init();


		if ( parent )
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
		C_BaseAnimating* parent = GetEngineObject()->GetMoveParent() ? ((C_BaseEntity*)GetEngineObject()->GetMoveParent()->GetOuter())->GetBaseAnimating() : NULL;
		if (parent)
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

