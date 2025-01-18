//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "c_baseanimating.h"
#include "c_sprite.h"
#include "model_types.h"
#include "bone_setup.h"
#include "ivrenderview.h"
#include "r_efx.h"
#include "dlight.h"
#include "beamdraw.h"
#include "cl_animevent.h"
#include "engine/IEngineSound.h"
#include "c_te_legacytempents.h"
#include "activitylist.h"
#include "animation.h"
#include "tier0/vprof.h"
#include "clienteffectprecachesystem.h"
#include "IEffects.h"
#include "engine/ivmodelinfo.h"
#include "engine/ivdebugoverlay.h"
#include "c_te_effect_dispatch.h"
#include <KeyValues.h>
#include "c_rope.h"
#include "isaverestore.h"
#include "datacache/imdlcache.h"
#include "eventlist.h"
#include "physics_saverestore.h"
#include "vphysics/constraints.h"
//#include "ragdoll_shared.h"
#include "iviewrender.h"
#include "c_ai_basenpc.h"
#include "saverestoretypes.h"
#include "input.h"
#include "soundinfo.h"
#include "tools/bonelist.h"
#include "toolframework/itoolframework.h"
#include "datacache/idatacache.h"
#include "gamestringpool.h"
#include "jigglebones.h"
#include "toolframework_client.h"
#include "vstdlib/jobthread.h"
#include "bonetoworldarray.h"
#include "posedebugger.h"
#include "tier0/icommandline.h"
#include "prediction.h"
#include "replay/replay_ragdoll.h"
#include "studio_stats.h"
#include "tier1/callqueue.h"
#include "ragdoll.h"

#ifdef TF_CLIENT_DLL
#include "c_tf_player.h"
#include "c_baseobject.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// If an NPC is moving faster than this, he should play the running footstep sound
const float RUN_SPEED_ESTIMATE_SQR = 150.0f * 150.0f;

// Removed macro used by shared code stuff
#if defined( CBaseAnimating )
#undef CBaseAnimating
#endif


#ifdef DEBUG
static ConVar dbganimmodel( "dbganimmodel", "" );
#endif


//-----------------------------------------------------------------------------
// Base Animating
//-----------------------------------------------------------------------------

IMPLEMENT_CLIENTCLASS_DT(C_BaseAnimating, DT_BaseAnimating, CBaseAnimating)

END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_BaseAnimating )

	//DEFINE_PRED_FIELD( m_nSkin, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	//DEFINE_PRED_FIELD( m_nBody, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
//	DEFINE_PRED_FIELD( m_nHitboxSet, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
//	DEFINE_PRED_FIELD( m_flModelScale, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	//DEFINE_PRED_FIELD( m_nSequence, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK ),
	//DEFINE_PRED_FIELD( m_flPlaybackRate, FIELD_FLOAT, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK ),
	//DEFINE_PRED_FIELD( m_flCycle, FIELD_FLOAT, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK ),
//	DEFINE_PRED_ARRAY( m_flPoseParameter, FIELD_FLOAT, MAXSTUDIOPOSEPARAM, FTYPEDESC_INSENDTABLE ),
	//DEFINE_PRED_ARRAY_TOL( m_flEncodedController, FIELD_FLOAT, MAXSTUDIOBONECTRLS, FTYPEDESC_INSENDTABLE, 0.02f ),

	//DEFINE_FIELD( m_nPrevSequence, FIELD_INTEGER ),
	//DEFINE_FIELD( m_flPrevEventCycle, FIELD_FLOAT ),
	//DEFINE_FIELD( m_flEventCycle, FIELD_FLOAT ),
	//DEFINE_FIELD( m_nEventSequence, FIELD_INTEGER ),

	//DEFINE_PRED_FIELD( m_nNewSequenceParity, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK ),
	//DEFINE_PRED_FIELD( m_nResetEventsParity, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_NOERRORCHECK ),
	// DEFINE_PRED_FIELD( m_nPrevResetEventsParity, FIELD_INTEGER, 0 ),

	//DEFINE_PRED_FIELD( m_nMuzzleFlashParity, FIELD_CHARACTER, FTYPEDESC_INSENDTABLE ),
	//DEFINE_FIELD( m_nOldMuzzleFlashParity, FIELD_CHARACTER ),

	//DEFINE_FIELD( m_nPrevNewSequenceParity, FIELD_INTEGER ),
	//DEFINE_FIELD( m_nPrevResetEventsParity, FIELD_INTEGER ),

	// DEFINE_PRED_FIELD( m_vecForce, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),
	// DEFINE_PRED_FIELD( m_nForceBone, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	// DEFINE_PRED_FIELD( m_bClientSideAnimation, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	// DEFINE_PRED_FIELD( m_bClientSideFrameReset, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	
	// DEFINE_FIELD( m_pRagdollInfo, RagdollInfo_t ),
	// DEFINE_FIELD( m_CachedBones, CUtlVector < CBoneCacheEntry > ),
	// DEFINE_FIELD( m_pActualAttachmentAngles, FIELD_VECTOR ),
	// DEFINE_FIELD( m_pActualAttachmentOrigin, FIELD_VECTOR ),

	// DEFINE_FIELD( m_animationQueue, CUtlVector < C_AnimationLayer > ),
	// DEFINE_FIELD( m_pIk, CIKContext ),
	// DEFINE_FIELD( m_bLastClientSideFrameReset, FIELD_BOOLEAN ),
	// DEFINE_FIELD( hdr, studiohdr_t ),
	// DEFINE_FIELD( m_pRagdoll, IRagdoll ),
	// DEFINE_FIELD( m_bStoreRagdollInfo, FIELD_BOOLEAN ),

	// DEFINE_FIELD( C_BaseFlex, m_iEyeAttachment, FIELD_INTEGER ),

END_PREDICTION_DATA()



//class C_BaseAnimatingGameSystem : public CAutoGameSystem
//{
//	void LevelShutdownPostEntity()
//	{
//
//	}
//} g_BaseAnimatingGameSystem;


//-----------------------------------------------------------------------------
// Purpose: convert axis rotations to a quaternion
//-----------------------------------------------------------------------------
C_BaseAnimating::C_BaseAnimating()
{
	m_nEventSequence = -1;
	m_nPrevResetEventsParity = -1;
	m_iEyeAttachment = 0;
#ifdef _XBOX
	m_iAccumulatedBoneMask = 0;
#endif
	//m_boneIndexAttached = -1;
	//m_pAttachedTo = NULL;
	//m_bDynamicModelAllowed = false;
	//m_bDynamicModelPending = false;
	//m_bResetSequenceInfoOnLoad = false;

}

bool C_BaseAnimating::Init(int entnum, int iSerialNum) {
	bool ret = BaseClass::Init(entnum, iSerialNum);
	return ret;
}

void C_BaseAnimating::UpdateOnRemove(void)
{
	C_BaseEntity* pChild = GetEffectEntity();

	if (pChild && pChild->GetEngineObject()->IsMarkedForDeletion() == false)
	{
		EntityList()->DestroyEntity(pChild);// ->Release();
	}

	if (GetThinkHandle() != INVALID_THINK_HANDLE)
	{
		ClientThinkList()->RemoveThinkable(GetClientHandle());
	}
	//EntityList()->RemoveEntity( this );

	partition->Remove(PARTITION_CLIENT_SOLID_EDICTS | PARTITION_CLIENT_RESPONSIVE_EDICTS | PARTITION_CLIENT_NON_STATIC_EDICTS, GetEngineObject()->GetPartitionHandle());
	GetEngineObject()->RemoveFromLeafSystem();

	BaseClass::UpdateOnRemove();
}
//-----------------------------------------------------------------------------
// Purpose: cleanup
//-----------------------------------------------------------------------------
C_BaseAnimating::~C_BaseAnimating()
{
	TermRopes();
	//Assert(!m_pRagdoll);

	// Kill off anything bone attached to us.
	//DestroyBoneAttachments();

	// If we are bone attached to something, remove us from the list.
	//if ( m_pAttachedTo )
	//{
	//	m_pAttachedTo->RemoveBoneAttachment( this );
	//	m_pAttachedTo = NULL;
	//}
}

bool C_BaseAnimating::UsesPowerOfTwoFrameBufferTexture( void )
{
	return modelinfo->IsUsingFBTexture(GetEngineObject()->GetModel(), GetEngineObject()->GetSkin(), GetEngineObject()->GetBody(), GetClientRenderable() );
}


//-----------------------------------------------------------------------------
// Should this object cast render-to-texture shadows?
//-----------------------------------------------------------------------------
ShadowType_t C_BaseAnimating::ShadowCastType()
{
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if ( !pStudioHdr || !pStudioHdr->SequencesAvailable() )
		return SHADOWS_NONE;

	if (GetEngineObject()->IsEffectActive(EF_NODRAW | EF_NOSHADOW) )
		return SHADOWS_NONE;

	if (pStudioHdr->GetNumSeq() == 0)
		return SHADOWS_RENDER_TO_TEXTURE;
		  
	if ( !GetEngineObject()->IsRagdoll() )
	{
		// If we have pose parameters, always update
		if ( pStudioHdr->GetNumPoseParameters() > 0 )
			return SHADOWS_RENDER_TO_TEXTURE_DYNAMIC;
		
		// If we have bone controllers, always update
		if ( pStudioHdr->numbonecontrollers() > 0 )
			return SHADOWS_RENDER_TO_TEXTURE_DYNAMIC;

		// If we use IK, always update
		if ( pStudioHdr->numikchains() > 0 )
			return SHADOWS_RENDER_TO_TEXTURE_DYNAMIC;
	}

	// FIXME: Do something to check to see how many frames the current animation has
	// If we do this, we have to be able to handle the case of changing ShadowCastTypes
	// at the moment, they are assumed to be constant.
	return SHADOWS_RENDER_TO_TEXTURE;
}

//-----------------------------------------------------------------------------
// Purpose: convert axis rotations to a quaternion
//-----------------------------------------------------------------------------

void C_BaseAnimating::SetPredictable( bool state )
{
	BaseClass::SetPredictable( state );

	GetEngineObject()->UpdateRelevantInterpolatedVars();
}


//void C_BaseAnimating::OnModelLoadComplete( const model_t* pModel )
//{
//	Assert( m_bDynamicModelPending && pModel == GetModel() );
//	if ( m_bDynamicModelPending && pModel == GetModel() )
//	{
//		m_bDynamicModelPending = false;
//		OnNewModel();
//		UpdateVisibility();
//	}
//}

void C_BaseAnimating::ValidateModelIndex()
{
	BaseClass::ValidateModelIndex();
	//Assert( m_nModelIndex == 0 || m_AutoRefModelIndex.Get() );
}

IStudioHdr *C_BaseAnimating::OnNewModel()
{

	

	//if ( m_bDynamicModelPending )
	//{
	//	modelinfo->UnregisterModelLoadCallback( -1, this );
	//	m_bDynamicModelPending = false;
	//}

	//m_AutoRefModelIndex.Clear();

	if ( !GetEngineObject()->GetModel() || modelinfo->GetModelType(GetEngineObject()->GetModel() ) != mod_studio )
		return NULL;

	// Reference (and thus start loading) dynamic model
	//int nNewIndex = m_nModelIndex;
	//if ( modelinfo->GetModel( nNewIndex ) != GetModel() )
	//{
	//	// XXX what's authoritative? the model pointer or the model index? what a mess.
	//	nNewIndex = modelinfo->GetModelIndex( modelinfo->GetModelName( GetModel() ) );
	//	Assert( nNewIndex < 0 || modelinfo->GetModel( nNewIndex ) == GetModel() );
	//	if ( nNewIndex < 0 )
	//		nNewIndex = m_nModelIndex;
	//}

	//m_AutoRefModelIndex = nNewIndex;
	//if ( IsDynamicModelIndex( nNewIndex ) && modelinfo->IsDynamicModelLoading( nNewIndex ) )
	//{
	//	m_bDynamicModelPending = true;
	//	modelinfo->RegisterModelLoadCallback( nNewIndex, this );
	//}

	//if ( IsDynamicModelLoading() )
	//{
	//	// Called while dynamic model still loading -> new model, clear deferred state
	//	m_bResetSequenceInfoOnLoad = false;
	//	return NULL;
	//}

	IStudioHdr *hdr = GetEngineObject()->GetModelPtr();
	if (hdr == NULL)
		return NULL;

	InitModelEffects();

	// lookup generic eye attachment, if exists
	m_iEyeAttachment = GetEngineObject()->LookupAttachment( "eyes" );

	// If we didn't have a model before, then we might need to go in the interpolation list now.
	if ( ShouldInterpolate() )
		GetEngineObject()->AddToInterpolationList();

	// objects with attachment points need to be queryable even if they're not solid
	if ( hdr->GetNumAttachments() != 0 )
	{
		GetEngineObject()->AddEFlags( EFL_USE_PARTITION_WHEN_NOT_SOLID );
	}


	// Most entities clear out their sequences when they change models on the server, but 
	// not all entities network down their m_nSequence (like multiplayer game player entities), 
	// so we may need to clear it out here. Force a SetSequence call no matter what, though.
	int forceSequence = ShouldResetSequenceOnNewModel() ? 0 : GetEngineObject()->GetSequence();

	if (GetEngineObject()->GetSequence() >= hdr->GetNumSeq() )
	{
		forceSequence = 0;
	}

	GetEngineObject()->SetSequence(-1);
	GetEngineObject()->SetSequence( forceSequence );

	//if ( m_bResetSequenceInfoOnLoad )
	//{
	//	m_bResetSequenceInfoOnLoad = false;
	//	ResetSequenceInfo();
	//}

	GetEngineObject()->UpdateRelevantInterpolatedVars();
		
	return hdr;
}

//=============================================================================
// HPE_END
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: Setup to initialize our model effects once the model's loaded
//-----------------------------------------------------------------------------
void C_BaseAnimating::InitModelEffects( void )
{
	m_bInitModelEffects = true;
	TermRopes();
}

//-----------------------------------------------------------------------------
// Purpose: Load the model's keyvalues section and create effects listed inside it
//-----------------------------------------------------------------------------
void C_BaseAnimating::DelayedInitModelEffects( void )
{
	m_bInitModelEffects = false;

	// Parse the keyvalues and see if they want to make ropes on this model.
	KeyValues * modelKeyValues = new KeyValues("");
	if ( modelKeyValues->LoadFromBuffer( modelinfo->GetModelName(GetEngineObject()->GetModel() ), modelinfo->GetModelKeyValueText(GetEngineObject()->GetModel() ) ) )
	{
		// Do we have a cables section?
		KeyValues *pkvAllCables = modelKeyValues->FindKey("Cables");
		if ( pkvAllCables )
		{
			// Start grabbing the sounds and slotting them in
			for ( KeyValues *pSingleCable = pkvAllCables->GetFirstSubKey(); pSingleCable; pSingleCable = pSingleCable->GetNextKey() )
			{
				C_RopeKeyframe *pRope = C_RopeKeyframe::CreateFromKeyValues( this, pSingleCable );
				m_Ropes.AddToTail( pRope );
			}
		}

 		if ( !m_bNoModelParticles )
		{
			// Do we have a particles section?
			KeyValues *pkvAllParticleEffects = modelKeyValues->FindKey("Particles");
			if ( pkvAllParticleEffects )
			{
				// Start grabbing the sounds and slotting them in
				for ( KeyValues *pSingleEffect = pkvAllParticleEffects->GetFirstSubKey(); pSingleEffect; pSingleEffect = pSingleEffect->GetNextKey() )
				{
					const char *pszParticleEffect = pSingleEffect->GetString( "name", "" );
					const char *pszAttachment = pSingleEffect->GetString( "attachment_point", "" );
					const char *pszAttachType = pSingleEffect->GetString( "attachment_type", "" );

					// Convert attach type
					int iAttachType = GetAttachTypeFromString( pszAttachType );
					if ( iAttachType == -1 )
					{
						Warning("Invalid attach type specified for particle effect in model '%s' keyvalues section. Trying to spawn effect '%s' with attach type of '%s'\n", GetEngineObject()->GetModelName(), pszParticleEffect, pszAttachType );
						return;
					}

					// Convert attachment point
					int iAttachment = atoi(pszAttachment);
					// See if we can find any attachment points matching the name
					if ( pszAttachment[0] != '0' && iAttachment == 0 )
					{
						iAttachment = GetEngineObject()->LookupAttachment( pszAttachment );
						if ( iAttachment <= 0 )
						{
							Warning("Failed to find attachment point specified for particle effect in model '%s' keyvalues section. Trying to spawn effect '%s' on attachment named '%s'\n", GetEngineObject()->GetModelName(), pszParticleEffect, pszAttachment );
							return;
						}
					}
					#ifdef TF_CLIENT_DLL
					// Halloween Hack for Sentry Rockets
					if ( !V_strcmp( "sentry_rocket", pszParticleEffect ) )
					{
						// Halloween Spell Effect Check
						int iHalloweenSpell = 0;
						// if the owner is a Sentry, Check its owner
						CBaseObject *pSentry = dynamic_cast<CBaseObject*>( GetOwnerEntity() );
						if ( pSentry )
						{
							CALL_ATTRIB_HOOK_INT_ON_OTHER( pSentry->GetOwner(), iHalloweenSpell, halloween_pumpkin_explosions );
						}
						else
						{
							CALL_ATTRIB_HOOK_INT_ON_OTHER( GetOwnerEntity(), iHalloweenSpell, halloween_pumpkin_explosions );
						}

						if ( iHalloweenSpell > 0 )
						{
							pszParticleEffect = "halloween_rockettrail";
						}
					}
					#endif
					// Spawn the particle effect
					ParticleProp()->Create( pszParticleEffect, (ParticleAttachment_t)iAttachType, iAttachment );
				}
			}
		}
	}

	modelKeyValues->deleteThis();
}


void C_BaseAnimating::TermRopes()
{
	FOR_EACH_LL(m_Ropes, i)
		EntityList()->DestroyEntity(m_Ropes[i]);// ->Release();

	m_Ropes.Purge();
}


//-----------------------------------------------------------------------------
// Purpose: Special effects
// Input  : transform - 
//-----------------------------------------------------------------------------
void C_BaseAnimating::ApplyBoneMatrixTransform( matrix3x4_t& transform )
{
	switch( GetEngineObject()->GetRenderFX() )
	{
	case kRenderFxDistort:
	case kRenderFxHologram:
		if ( RandomInt(0,49) == 0 )
		{
			int axis = RandomInt(0,1);
			if ( axis == 1 ) // Choose between x & z
				axis = 2;
			VectorScale( transform[axis], RandomFloat(1,1.484), transform[axis] );
		}
		else if ( RandomInt(0,49) == 0 )
		{
			float offset;
			int axis = RandomInt(0,1);
			if ( axis == 1 ) // Choose between x & z
				axis = 2;
			offset = RandomFloat(-10,10);
			transform[RandomInt(0,2)][3] += offset;
		}
		break;
	case kRenderFxExplode:
		{
			float scale;
			
			scale = 1.0 + (gpGlobals->curtime - GetEngineObject()->GetAnimTime()) * 10.0;
			if ( scale > 2 )	// Don't blow up more than 200%
				scale = 2;
			transform[0][1] *= scale;
			transform[1][1] *= scale;
			transform[2][1] *= scale;
		}
		break;
	default:
		break;
		
	}

	if (GetEngineObject()->IsModelScaled() )
	{
		// The bone transform is in worldspace, so to scale this, we need to translate it back
		float scale = GetEngineObject()->GetModelScale();

		Vector pos;
		MatrixGetColumn( transform, 3, pos );
		pos -= GetRenderOrigin();
		pos *= scale;
		pos += GetRenderOrigin();
		MatrixSetColumn( pos, 3, transform );

		VectorScale( transform[0], scale, transform[0] );
		VectorScale( transform[1], scale, transform[1] );
		VectorScale( transform[2], scale, transform[2] );
	}
}

//-----------------------------------------------------------------------------
// Should we collide?
//-----------------------------------------------------------------------------

CollideType_t C_BaseAnimating::GetCollideType( void )
{
	if (GetEngineObject()->IsRagdoll() )
		return ENTITY_SHOULD_RESPOND;

	return BaseClass::GetCollideType();
}



//void C_BaseAnimating::AccumulateLayers( IBoneSetup &boneSetup, Vector pos[], Quaternion q[], float currentTime )
//{
	// Nothing here
//}

//void C_BaseAnimating::ChildLayerBlend( Vector pos[], Quaternion q[], float currentTime, int boneMask )
//{
//	return;
//
//	Vector		childPos[MAXSTUDIOBONES];
//	Quaternion	childQ[MAXSTUDIOBONES];
//	float		childPoseparam[MAXSTUDIOPOSEPARAM];
//
//	// go through all children
//	for ( IEngineObjectClient *pChild = GetEngineObject()->FirstMoveChild(); pChild; pChild = pChild->NextMovePeer() )
//	{
//		C_BaseAnimating *pChildAnimating = pChild->GetOuter()->GetBaseAnimating();
//
//		if ( pChildAnimating )
//		{
//			IStudioHdr *pChildHdr = pChildAnimating->GetModelPtr();
//
//			// FIXME: needs a new type of EF_BONEMERGE (EF_CHILDMERGE?)
//			if ( pChildHdr && pChild->IsEffectActive( EF_BONEMERGE ) && pChildHdr->SequencesAvailable() && pChildAnimating->m_pBoneMergeCache )
//			{
//				// FIXME: these should Inherit from the parent
//				GetPoseParameters( pChildHdr, childPoseparam );
//
//				IBoneSetup childBoneSetup( pChildHdr, boneMask, childPoseparam );
//				childBoneSetup.InitPose( childPos, childQ );
//
//				// set up the child into the parent's current pose
//				//pChildAnimating->m_pBoneMergeCache->CopyParentToChild( pos, q, childPos, childQ, boneMask );
//
//				// FIXME: needs some kind of sequence
//				// merge over whatever bones the childs sequence modifies
//				childBoneSetup.AccumulatePose( childPos, childQ, 0, GetCycle(), 1.0, currentTime, NULL );
//
//				// copy the result back into the parents bones
//				//pChildAnimating->m_pBoneMergeCache->CopyChildToParent( childPos, childQ, pos, q, boneMask );
//
//				// probably needs an IK merge system of some sort =(
//			}
//		}
//	}
//}

//-----------------------------------------------------------------------------
// Purpose: Get attachment point by index (position only)
// Input  : number - which point
//-----------------------------------------------------------------------------
//bool C_BaseAnimating::GetAttachment( int number, Vector &origin )
//{
//	// Note: this could be more efficient, but we want the matrix3x4_t version of GetAttachment to be the origin of
//	// attachment generation, so a derived class that wants to fudge attachments only 
//	// has to reimplement that version. This also makes it work like the server in that regard.
//	matrix3x4_t attachmentToWorld;
//	if ( !GetAttachment( number, attachmentToWorld ) )
//	{
//		// Set this to the model origin/angles so that we don't have stack fungus in origin and angles.
//		origin = GetEngineObject()->GetAbsOrigin();
//		return false;
//	}
//
//	MatrixPosition( attachmentToWorld, origin );
//	return true;
//}


//bool C_BaseAnimating::GetAttachment( const char *szName, Vector &absOrigin )
//{
//	return GetAttachment( LookupAttachment( szName ), absOrigin );
//}

//-----------------------------------------------------------------------------
// Returns the attachment in local space
//-----------------------------------------------------------------------------
bool C_BaseAnimating::GetAttachmentLocal( int iAttachment, matrix3x4_t &attachmentToLocal )
{
	matrix3x4_t attachmentToWorld;
	if (!GetEngineObject()->GetAttachment(iAttachment, attachmentToWorld))
		return false;

	matrix3x4_t worldToEntity;
	MatrixInvert(GetEngineObject()->EntityToWorldTransform(), worldToEntity );
	ConcatTransforms( worldToEntity, attachmentToWorld, attachmentToLocal ); 
	return true;
}

bool C_BaseAnimating::GetAttachmentLocal( int iAttachment, Vector &origin, QAngle &angles )
{
	matrix3x4_t attachmentToEntity;

	if ( GetAttachmentLocal( iAttachment, attachmentToEntity ) )
	{
		origin.Init( attachmentToEntity[0][3], attachmentToEntity[1][3], attachmentToEntity[2][3] );
		MatrixAngles( attachmentToEntity, angles );
		return true;
	}
	return false;
}

bool C_BaseAnimating::GetAttachmentLocal( int iAttachment, Vector &origin )
{
	matrix3x4_t attachmentToEntity;

	if ( GetAttachmentLocal( iAttachment, attachmentToEntity ) )
	{
		MatrixPosition( attachmentToEntity, origin );
		return true;
	}
	return false;
}



//-----------------------------------------------------------------------------
// Purpose: Move sound location to center of body
//-----------------------------------------------------------------------------
bool C_BaseAnimating::GetSoundSpatialization( SpatializationInfo_t& info )
{
	{
		C_BaseAnimating::AutoAllowBoneAccess boneaccess( true, false );
		if ( !BaseClass::GetSoundSpatialization( info ) )
			return false;
	}

	// move sound origin to center if npc has IK
	if ( info.pOrigin && IsNPC() && GetEngineObject()->GetIk())
	{
		*info.pOrigin = GetEngineObject()->GetAbsOrigin();

		Vector mins, maxs, center;

		modelinfo->GetModelBounds(GetEngineObject()->GetModel(), mins, maxs );
		VectorAdd( mins, maxs, center );
		VectorScale( center, 0.5f, center );

		(*info.pOrigin) += center;
	}
	return true;
}


bool C_BaseAnimating::IsViewModel() const
{
	return false;
}

bool C_BaseAnimating::IsMenuModel() const
{
	return false;
}


class CTraceFilterSkipNPCsAndPlayers : public CTraceFilterSimple
{
public:
	CTraceFilterSkipNPCsAndPlayers( const IHandleEntity *passentity, int collisionGroup )
		: CTraceFilterSimple( passentity, collisionGroup )
	{
	}

	virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
	{
		if ( CTraceFilterSimple::ShouldHitEntity(pServerEntity, contentsMask) )
		{
			C_BaseEntity *pEntity = EntityFromEntityHandle( pServerEntity );
			if ( !pEntity )
				return true;

			if ( pEntity->IsNPC() || pEntity->IsPlayer() )
				return false;

			return true;
		}
		return false;
	}
};


/*
void drawLine(const Vector& origin, const Vector& dest, int r, int g, int b, bool noDepthTest, float duration) 
{
	debugoverlay->AddLineOverlay( origin, dest, r, g, b, noDepthTest, duration );
}
*/

//-----------------------------------------------------------------------------
// Purpose: Find the ground or external attachment points needed by IK rules
//-----------------------------------------------------------------------------

void C_BaseAnimating::CalculateIKLocks( float currentTime )
{
	if (!GetEngineObject()->GetIk())
		return;

	int targetCount = GetEngineObject()->GetIk()->m_target.Count();
	if ( targetCount == 0 )
		return;

	// In TF, we might be attaching a player's view to a walking model that's using IK. If we are, it can
	// get in here during the view setup code, and it's not normally supposed to be able to access the spatial
	// partition that early in the rendering loop. So we allow access right here for that special case.
	SpatialPartitionListMask_t curSuppressed = partition->GetSuppressedLists();
	partition->SuppressLists( PARTITION_ALL_CLIENT_EDICTS, false );
	EntityList()->PushEnableAbsRecomputations( false );

	Ray_t ray;
	CTraceFilterSkipNPCsAndPlayers traceFilter( this, GetEngineObject()->GetCollisionGroup() );

	// FIXME: trace based on gravity or trace based on angles?
	Vector up;
	AngleVectors( GetRenderAngles(), NULL, NULL, &up );

	// FIXME: check number of slots?
	float minHeight = FLT_MAX;
	float maxHeight = -FLT_MAX;

	for (int i = 0; i < targetCount; i++)
	{
		trace_t trace;
		CIKTarget *pTarget = &GetEngineObject()->GetIk()->m_target[i];

		if (!pTarget->IsActive())
			continue;

		switch( pTarget->type)
		{
		case IK_GROUND:
			{
				Vector estGround;
				Vector p1, p2;

				// adjust ground to original ground position
				estGround = (pTarget->est.pos - GetRenderOrigin());
				estGround = estGround - (estGround * up) * up;
				estGround = GetEngineObject()->GetAbsOrigin() + estGround + pTarget->est.floor * up;

				VectorMA( estGround, pTarget->est.height, up, p1 );
				VectorMA( estGround, -pTarget->est.height, up, p2 );

				float r = MAX( pTarget->est.radius, 1);

				// don't IK to other characters
				ray.Init( p1, p2, Vector(-r,-r,0), Vector(r,r,r*2) );
				enginetrace->TraceRay( ray, PhysicsSolidMaskForEntity(), &traceFilter, &trace );

				if ( trace.m_pEnt != NULL && ((C_BaseEntity*)trace.m_pEnt)->GetEngineObject()->GetMoveType() == MOVETYPE_PUSH )
				{
					pTarget->SetOwner(((C_BaseEntity*)trace.m_pEnt)->entindex(), ((C_BaseEntity*)trace.m_pEnt)->GetEngineObject()->GetAbsOrigin(), ((C_BaseEntity*)trace.m_pEnt)->GetEngineObject()->GetAbsAngles() );
				}
				else
				{
					pTarget->ClearOwner( );
				}

				if (trace.startsolid)
				{
					// trace from back towards hip
					Vector tmp = estGround - pTarget->trace.closest;
					tmp.NormalizeInPlace();
					ray.Init( estGround - tmp * pTarget->est.height, estGround, Vector(-r,-r,0), Vector(r,r,1) );

					// debugoverlay->AddLineOverlay( ray.m_Start, ray.m_Start + ray.m_Delta, 255, 0, 0, 0, 0 );

					enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );

					if (!trace.startsolid)
					{
						p1 = trace.endpos;
						VectorMA( p1, - pTarget->est.height, up, p2 );
						ray.Init( p1, p2, Vector(-r,-r,0), Vector(r,r,1) );

						enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );
					}

					// debugoverlay->AddLineOverlay( ray.m_Start, ray.m_Start + ray.m_Delta, 0, 255, 0, 0, 0 );
				}


				if (!trace.startsolid)
				{
					if (trace.DidHitWorld())
					{
						// clamp normal to 33 degrees
						const float limit = 0.832;
						float dot = DotProduct(trace.plane.normal, up);
						if (dot < limit)
						{
							Assert( dot >= 0 );
							// subtract out up component
							Vector diff = trace.plane.normal - up * dot;
							// scale remainder such that it and the up vector are a unit vector
							float d = sqrt( (1 - limit * limit) / DotProduct( diff, diff ) );
							trace.plane.normal = up * limit + d * diff;
						}
						// FIXME: this is wrong with respect to contact position and actual ankle offset
						pTarget->SetPosWithNormalOffset( trace.endpos, trace.plane.normal );
						pTarget->SetNormal( trace.plane.normal );
						pTarget->SetOnWorld( true );

						// only do this on forward tracking or commited IK ground rules
						if (pTarget->est.release < 0.1)
						{
							// keep track of ground height
							float offset = DotProduct( pTarget->est.pos, up );
							if (minHeight > offset )
								minHeight = offset;

							if (maxHeight < offset )
								maxHeight = offset;
						}
						// FIXME: if we don't drop legs, running down hills looks horrible
						/*
						if (DotProduct( pTarget->est.pos, up ) < DotProduct( estGround, up ))
						{
							pTarget->est.pos = estGround;
						}
						*/
					}
					else if (trace.DidHitNonWorldEntity())
					{
						pTarget->SetPos( trace.endpos );
						pTarget->SetAngles( GetRenderAngles() );

						// only do this on forward tracking or commited IK ground rules
						if (pTarget->est.release < 0.1)
						{
							float offset = DotProduct( pTarget->est.pos, up );
							if (minHeight > offset )
								minHeight = offset;

							if (maxHeight < offset )
								maxHeight = offset;
						}
						// FIXME: if we don't drop legs, running down hills looks horrible
						/*
						if (DotProduct( pTarget->est.pos, up ) < DotProduct( estGround, up ))
						{
							pTarget->est.pos = estGround;
						}
						*/
					}
					else
					{
						pTarget->IKFailed( );
					}
				}
				else
				{
					if (!trace.DidHitWorld())
					{
						pTarget->IKFailed( );
					}
					else
					{
						pTarget->SetPos( trace.endpos );
						pTarget->SetAngles( GetRenderAngles() );
						pTarget->SetOnWorld( true );
					}
				}

				/*
				debugoverlay->AddTextOverlay( p1, i, 0, "%d %.1f %.1f %.1f ", i, 
					pTarget->latched.deltaPos.x, pTarget->latched.deltaPos.y, pTarget->latched.deltaPos.z );
				debugoverlay->AddBoxOverlay( pTarget->est.pos, Vector( -r, -r, -1 ), Vector( r, r, 1), QAngle( 0, 0, 0 ), 255, 0, 0, 0, 0 );
				*/
				// debugoverlay->AddBoxOverlay( pTarget->latched.pos, Vector( -2, -2, 2 ), Vector( 2, 2, 6), QAngle( 0, 0, 0 ), 0, 255, 0, 0, 0 );
			}
			break;

		case IK_ATTACHMENT:
			{
				C_BaseEntity *pEntity = NULL;
				float flDist = pTarget->est.radius;

				// FIXME: make entity finding sticky!
				// FIXME: what should the radius check be?
				for ( CEntitySphereQuery sphere( pTarget->est.pos, 64 ); ( pEntity = sphere.GetCurrentEntity() ) != NULL; sphere.NextEntity() )
				{
					//C_BaseAnimating *pAnim = pEntity->GetBaseAnimating( );
					if (!pEntity->GetEngineObject()->GetModelPtr())
						continue;

					int iAttachment = pEntity->GetEngineObject()->LookupAttachment( pTarget->offset.pAttachmentName );
					if (iAttachment <= 0)
						continue;

					Vector origin;
					QAngle angles;
					pEntity->GetEngineObject()->GetAttachment( iAttachment, origin, angles );

					// debugoverlay->AddBoxOverlay( origin, Vector( -1, -1, -1 ), Vector( 1, 1, 1 ), QAngle( 0, 0, 0 ), 255, 0, 0, 0, 0 );

					float d = (pTarget->est.pos - origin).Length();

					if ( d >= flDist)
						continue;

					flDist = d;
					pTarget->SetPos( origin );
					pTarget->SetAngles( angles );
					// debugoverlay->AddBoxOverlay( pTarget->est.pos, Vector( -pTarget->est.radius, -pTarget->est.radius, -pTarget->est.radius ), Vector( pTarget->est.radius, pTarget->est.radius, pTarget->est.radius), QAngle( 0, 0, 0 ), 0, 255, 0, 0, 0 );
				}

				if (flDist >= pTarget->est.radius)
				{
					// debugoverlay->AddBoxOverlay( pTarget->est.pos, Vector( -pTarget->est.radius, -pTarget->est.radius, -pTarget->est.radius ), Vector( pTarget->est.radius, pTarget->est.radius, pTarget->est.radius), QAngle( 0, 0, 0 ), 0, 0, 255, 0, 0 );
					// no solution, disable ik rule
					pTarget->IKFailed( );
				}
			}
			break;
		}
	}

#if defined( HL2_CLIENT_DLL )
	if (minHeight < FLT_MAX)
	{
		input->AddIKGroundContactInfo( entindex(), minHeight, maxHeight );
	}
#endif

	EntityList()->PopEnableAbsRecomputations();
	partition->SuppressLists( curSuppressed, true );
}

C_BaseAnimating::AutoAllowBoneAccess::AutoAllowBoneAccess( bool bAllowForNormalModels, bool bAllowForViewModels )
{
	EntityList()->PushAllowBoneAccess(bAllowForNormalModels, bAllowForViewModels, (char const*)1);
}

C_BaseAnimating::AutoAllowBoneAccess::~AutoAllowBoneAccess( )
{
	EntityList()->PopBoneAccess( ( char const * ) 1 );
}

bool C_BaseAnimating::ShouldDraw()
{
	return BaseClass::ShouldDraw();//!IsDynamicModelLoading() && 
}

ConVar r_drawothermodels( "r_drawothermodels", "1", FCVAR_CHEAT, "0=Off, 1=Normal, 2=Wireframe" );

//-----------------------------------------------------------------------------
// Purpose: Draws the object
// Input  : flags - 
//-----------------------------------------------------------------------------
int C_BaseAnimating::DrawModel( int flags )
{
	VPROF_BUDGET( "C_BaseAnimating::DrawModel", VPROF_BUDGETGROUP_MODEL_RENDERING );
	if ( !GetEngineObject()->IsReadyToDraw() )
		return 0;

	int drawn = 0;

#ifdef TF_CLIENT_DLL
	ValidateModelIndex();
#endif

	if ( r_drawothermodels.GetInt() )
	{
		MDLCACHE_CRITICAL_SECTION();

		int extraFlags = 0;
		if ( r_drawothermodels.GetInt() == 2 )
		{
			extraFlags |= STUDIO_WIREFRAME;
		}

		if ( flags & STUDIO_SHADOWDEPTHTEXTURE )
		{
			extraFlags |= STUDIO_SHADOWDEPTHTEXTURE;
		}

		if ( flags & STUDIO_SSAODEPTHTEXTURE )
		{
			extraFlags |= STUDIO_SSAODEPTHTEXTURE;
		}

		if ( ( flags & ( STUDIO_SSAODEPTHTEXTURE | STUDIO_SHADOWDEPTHTEXTURE ) ) == 0 &&
			g_pStudioStatsEntity != NULL && g_pStudioStatsEntity == GetClientRenderable() )
		{
			extraFlags |= STUDIO_GENERATE_STATS;
		}

		// Necessary for lighting blending
		GetEngineObject()->CreateModelInstance();

		if ( !GetEngineObject()->IsFollowingEntity() )
		{
			drawn = InternalDrawModel( flags|extraFlags );
		}
		else
		{
			// this doesn't draw unless master entity is visible and it's a studio model!!!
			C_BaseAnimating *follow = (C_BaseAnimating*)GetEngineObject()->FindFollowedEntity()->GetOuter();
			if ( follow )
			{
				// recompute master entity bone structure
				int baseDrawn = follow->DrawModel( 0 );

				// draw entity
				// FIXME: Currently only draws if aiment is drawn.  
				// BUGBUG: Fixup bbox and do a separate cull for follow object
				if ( baseDrawn )
				{
					drawn = InternalDrawModel( STUDIO_RENDER|extraFlags );
				}
			}
		}
	}

	// If we're visualizing our bboxes, draw them
	DrawBBoxVisualizations();

	return drawn;
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
void C_BaseAnimating::DoInternalDrawModel( ClientModelRenderInfo_t *pInfo, DrawModelState_t *pState, matrix3x4_t *pBoneToWorldArray )
{
	if ( pState)
	{
		modelrender->DrawModelExecute( *pState, *pInfo, pBoneToWorldArray );
	}

	if ( vcollide_wireframe.GetBool() )
	{
		if (GetEngineObject()->IsRagdoll() )
		{
			GetEngineObject()->DrawWireframe();
		}
		else if (GetEngineObject()->IsSolid() && GetEngineObject()->GetSolid() == SOLID_VPHYSICS )
		{
			vcollide_t *pCollide = modelinfo->GetVCollide(GetEngineObject()->GetModelIndex() );
			if ( pCollide && pCollide->solidCount == 1 )
			{
				static color32 debugColor = {0,255,255,0};
				matrix3x4_t matrix;
				AngleMatrix(GetEngineObject()->GetAbsAngles(), GetEngineObject()->GetAbsOrigin(), matrix );
				engine->DebugDrawPhysCollide( pCollide->solids[0], NULL, matrix, debugColor );
				if (GetEngineObject()->VPhysicsGetObject() )
				{
					static color32 debugColorPhys = {255,0,0,0};
					matrix3x4_t matrix;
					GetEngineObject()->VPhysicsGetObject()->GetPositionMatrix( &matrix );
					engine->DebugDrawPhysCollide( pCollide->solids[0], NULL, matrix, debugColorPhys );
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Draws the object
// Input  : flags - 
//-----------------------------------------------------------------------------
int C_BaseAnimating::InternalDrawModel( int flags )
{
	VPROF( "C_BaseAnimating::InternalDrawModel" );

	if ( !GetEngineObject()->GetModel() )
		return 0;

	// This should never happen, but if the server class hierarchy has bmodel entities derived from CBaseAnimating or does a
	//  SetModel with the wrong type of model, this could occur.
	if ( modelinfo->GetModelType(GetEngineObject()->GetModel() ) != mod_studio )
	{
		return BaseClass::DrawModel( flags );
	}

	// Make sure hdr is valid for drawing
	if ( !GetEngineObject()->GetModelPtr() )
		return 0;

	//UpdateBoneAttachments( );

	if (GetEngineObject()->IsEffectActive( EF_ITEM_BLINK ) )
	{
		flags |= STUDIO_ITEM_BLINK;
	}

	ClientModelRenderInfo_t info;
	ClientModelRenderInfo_t *pInfo;

	pInfo = &info;

	pInfo->flags = flags;
	pInfo->pRenderable = this->GetEngineObject();
	pInfo->instance = GetEngineObject()->GetModelInstance();
	pInfo->entity_index = entindex();
	pInfo->pModel = GetEngineObject()->GetModel();
	pInfo->origin = GetRenderOrigin();
	pInfo->angles = GetRenderAngles();
	pInfo->skin = GetEngineObject()->GetSkin();
	pInfo->body = GetEngineObject()->GetBody();
	pInfo->hitboxset = GetEngineObject()->GetHitboxSet();

	if ( !OnInternalDrawModel( pInfo ) )
	{
		return 0;
	}

	Assert( !pInfo->pModelToWorld);
	if ( !pInfo->pModelToWorld )
	{
		pInfo->pModelToWorld = &pInfo->modelToWorld;

		// Turns the origin + angles into a matrix
		AngleMatrix( pInfo->angles, pInfo->origin, pInfo->modelToWorld );
	}

	DrawModelState_t state;
	matrix3x4_t *pBoneToWorld = NULL;
	bool bMarkAsDrawn = modelrender->DrawModelSetup( *pInfo, &state, NULL, &pBoneToWorld );
	
	// Scale the base transform if we don't have a bone hierarchy
	if (GetEngineObject()->IsModelScaled() )
	{
		IStudioHdr *pHdr = GetEngineObject()->GetModelPtr();
		if ( pHdr && pBoneToWorld && pHdr->numbones() == 1 )
		{
			// Scale the bone to world at this point
			const float flScale = GetEngineObject()->GetModelScale();
			VectorScale( (*pBoneToWorld)[0], flScale, (*pBoneToWorld)[0] );
			VectorScale( (*pBoneToWorld)[1], flScale, (*pBoneToWorld)[1] );
			VectorScale( (*pBoneToWorld)[2], flScale, (*pBoneToWorld)[2] );
		}
	}

	DoInternalDrawModel( pInfo, ( bMarkAsDrawn && ( pInfo->flags & STUDIO_RENDER ) ) ? &state : NULL, pBoneToWorld );

	OnPostInternalDrawModel( pInfo );

	return bMarkAsDrawn;
}

extern ConVar muzzleflash_light;

void C_BaseAnimating::ProcessMuzzleFlashEvent()
{
	// If we have an attachment, then stick a light on it.
	if ( muzzleflash_light.GetBool() )
	{
		//FIXME: We should really use a named attachment for this
		if (GetEngineObject()->GetAttachmentCount() > 0 )
		{
			Vector vAttachment;
			QAngle dummyAngles;
			GetEngineObject()->GetAttachment( 1, vAttachment, dummyAngles );

			// Make an elight
			dlight_t *el = effects->CL_AllocElight( LIGHT_INDEX_MUZZLEFLASH + entindex());
			el->origin = vAttachment;
			el->radius = random->RandomInt( 32, 64 ); 
			el->decay = el->radius / 0.05f;
			el->die = gpGlobals->curtime + 0.05f;
			el->color.r = 255;
			el->color.g = 192;
			el->color.b = 64;
			el->color.exponent = 5;
		}
	}
}

//-----------------------------------------------------------------------------
// Internal routine to process animation events for studiomodels
//-----------------------------------------------------------------------------
void C_BaseAnimating::DoAnimationEvents( IStudioHdr *pStudioHdr )
{
	if ( !pStudioHdr )
		return;

#ifdef DEBUG
	bool watch = dbganimmodel.GetString()[0] && V_stristr( pStudioHdr->pszName(), dbganimmodel.GetString() );
#else
	bool watch = false; // Q_strstr( hdr->name, "rifle" ) ? true : false;
#endif

	//Adrian: eh? This should never happen.
	if (GetEngineObject()->GetSequence() == -1 )
		 return;

	// build root animation
	float flEventCycle = GetEngineObject()->GetCycle();

	// If we're invisible, don't draw the muzzle flash
	bool bIsInvisible = !IsVisible() && !IsViewModel() && !IsMenuModel();

	if ( bIsInvisible && !clienttools->IsInRecordingMode() )
		return;

	// add in muzzleflash effect
	if (GetEngineObject()->ShouldMuzzleFlash() )
	{
		GetEngineObject()->DisableMuzzleFlash();
		
		ProcessMuzzleFlashEvent();
	}

	// If we're invisible, don't process animation events.
	if ( bIsInvisible )
		return;

	// If we don't have any sequences, don't do anything
	int nStudioNumSeq = pStudioHdr->GetNumSeq();
	if ( nStudioNumSeq < 1 )
	{
		Warning( "%s[%d]: no sequences?\n", GetDebugName(), entindex() );
		Assert( nStudioNumSeq >= 1 );
		return;
	}

	int nSeqNum = GetEngineObject()->GetSequence();
	if ( nSeqNum >= nStudioNumSeq )
	{
		// This can happen e.g. while reloading Heavy's shotgun, switch to the minigun.
		Warning( "%s[%d]: Playing sequence %d but there's only %d in total?\n", GetDebugName(), entindex(), nSeqNum, nStudioNumSeq );
		return;
	}

	mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc( nSeqNum );

	if (seqdesc.numevents == 0)
		return;

	// Forces anim event indices to get set and returns pEvent(0);
	mstudioevent_t *pevent = mdlcache->GetEventIndexForSequence( seqdesc );

	if ( watch )
	{
		Msg( "%i cycle %f\n", gpGlobals->tickcount, GetEngineObject()->GetCycle() );
	}

	bool resetEvents = GetEngineObject()->GetResetEventsParity() != m_nPrevResetEventsParity;
	m_nPrevResetEventsParity = GetEngineObject()->GetResetEventsParity();

	if (m_nEventSequence != GetEngineObject()->GetSequence() || resetEvents )
	{
		if ( watch )
		{
			Msg( "new seq: %i - old seq: %i - reset: %s - m_flCycle %f - Model Name: %s - (time %.3f)\n",
				GetEngineObject()->GetSequence(), m_nEventSequence,
				resetEvents ? "true" : "false",
				GetEngineObject()->GetCycle(), pStudioHdr->pszName(),
				gpGlobals->curtime);
		}

		m_nEventSequence = GetEngineObject()->GetSequence();
		flEventCycle = 0.0f;
		m_flPrevEventCycle = -0.01; // back up to get 0'th frame animations
	}

	// stalled?
	if (flEventCycle == m_flPrevEventCycle)
		return;

	if ( watch )
	{
		 Msg( "%i (seq %d cycle %.3f ) evcycle %.3f prevevcycle %.3f (time %.3f)\n",
			 gpGlobals->tickcount, 
			 GetEngineObject()->GetSequence(),
			 GetEngineObject()->GetCycle(),
			 flEventCycle,
			 m_flPrevEventCycle,
			 gpGlobals->curtime );
	}

	// check for looping
	BOOL bLooped = false;
	if (flEventCycle <= m_flPrevEventCycle)
	{
		if (m_flPrevEventCycle - flEventCycle > 0.5)
		{
			bLooped = true;
		}
		else
		{
			// things have backed up, which is bad since it'll probably result in a hitch in the animation playback
			// but, don't play events again for the same time slice
			return;
		}
	}

	// This makes sure events that occur at the end of a sequence occur are
	// sent before events that occur at the beginning of a sequence.
	if (bLooped)
	{
		for (int i = 0; i < (int)seqdesc.numevents; i++)
		{
			// ignore all non-client-side events

			if ( pevent[i].type & AE_TYPE_NEWEVENTSYSTEM )
			{
				if ( !( pevent[i].type & AE_TYPE_CLIENT ) )
					 continue;
			}
			else if ( pevent[i].event < 5000 ) //Adrian - Support the old event system
				continue;
		
			if ( pevent[i].cycle <= m_flPrevEventCycle )
				continue;
			
			if ( watch )
			{
				Msg( "%i FE %i Looped cycle %f, prev %f ev %f (time %.3f)\n",
					gpGlobals->tickcount,
					pevent[i].event,
					pevent[i].cycle,
					m_flPrevEventCycle,
					flEventCycle,
					gpGlobals->curtime );
			}
				
				
			FireEvent(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), pevent[ i ].event, pevent[ i ].pszOptions() );
		}

		// Necessary to get the next loop working
		m_flPrevEventCycle = -0.01;
	}

	for (int i = 0; i < (int)seqdesc.numevents; i++)
	{
		if ( pevent[i].type & AE_TYPE_NEWEVENTSYSTEM )
		{
			if ( !( pevent[i].type & AE_TYPE_CLIENT ) )
				 continue;
		}
		else if ( pevent[i].event < 5000 ) //Adrian - Support the old event system
			continue;

		if ( (pevent[i].cycle > m_flPrevEventCycle && pevent[i].cycle <= flEventCycle) )
		{
			if ( watch )
			{
				Msg( "%i (seq: %d) FE %i Normal cycle %f, prev %f ev %f (time %.3f)\n",
					gpGlobals->tickcount,
					GetEngineObject()->GetSequence(),
					pevent[i].event,
					pevent[i].cycle,
					m_flPrevEventCycle,
					flEventCycle,
					gpGlobals->curtime );
			}

			FireEvent(GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsAngles(), pevent[ i ].event, pevent[ i ].pszOptions() );
		}
	}

	m_flPrevEventCycle = flEventCycle;
}

//-----------------------------------------------------------------------------
// Purpose: Parses a muzzle effect event and sends it out for drawing
// Input  : *options - event parameters in text format
//			isFirstPerson - whether this is coming from an NPC or the player
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_BaseAnimating::DispatchMuzzleEffect( const char *options, bool isFirstPerson )
{
	const char	*p = options;
	char		token[128];
	int			weaponType = 0;

	// Get the first parameter
	p = nexttoken( token, p, ' ' );

	// Find the weapon type
	if ( token ) 
	{
		//TODO: Parse the type from a list instead
		if ( Q_stricmp( token, "COMBINE" ) == 0 )
		{
			weaponType = MUZZLEFLASH_COMBINE;
		}
		else if ( Q_stricmp( token, "SMG1" ) == 0 )
		{
			weaponType = MUZZLEFLASH_SMG1;
		}
		else if ( Q_stricmp( token, "PISTOL" ) == 0 )
		{
			weaponType = MUZZLEFLASH_PISTOL;
		}
		else if ( Q_stricmp( token, "SHOTGUN" ) == 0 )
		{
			weaponType = MUZZLEFLASH_SHOTGUN;
		}
		else if ( Q_stricmp( token, "357" ) == 0 )
		{
			weaponType = MUZZLEFLASH_357;
		}
		else if ( Q_stricmp( token, "RPG" ) == 0 )
		{
			weaponType = MUZZLEFLASH_RPG;
		}
		else
		{
			//NOTENOTE: This means you specified an invalid muzzleflash type, check your spelling?
			Assert( 0 );
		}
	}
	else
	{
		//NOTENOTE: This means that there wasn't a proper parameter passed into the animevent
		Assert( 0 );
		return false;
	}

	// Get the second parameter
	p = nexttoken( token, p, ' ' );

	int	attachmentIndex = -1;

	// Find the attachment name
	if ( token ) 
	{
		attachmentIndex = GetEngineObject()->LookupAttachment( token );

		// Found an invalid attachment
		if ( attachmentIndex <= 0 )
		{
			//NOTENOTE: This means that the attachment you're trying to use is invalid
			Assert( 0 );
			return false;
		}
	}
	else
	{
		//NOTENOTE: This means that there wasn't a proper parameter passed into the animevent
		Assert( 0 );
		return false;
	}

	// Send it out
	tempents->MuzzleFlash( weaponType, this, attachmentIndex, isFirstPerson );

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void MaterialFootstepSound( C_BaseAnimating *pEnt, bool bLeftFoot, float flVolume )
{
	trace_t tr;
	Vector traceStart;
	QAngle angles;

	int attachment;

	//!!!PERF - These string lookups here aren't the swiftest, but
	// this doesn't get called very frequently unless a lot of NPCs
	// are using this code.
	if( bLeftFoot )
	{
		attachment = pEnt->GetEngineObject()->LookupAttachment( "LeftFoot" );
	}
	else
	{
		attachment = pEnt->GetEngineObject()->LookupAttachment( "RightFoot" );
	}

	if( attachment == -1 )
	{
		// Exit if this NPC doesn't have the proper attachments.
		return;
	}

	pEnt->GetEngineObject()->GetAttachment( attachment, traceStart, angles );

	UTIL_TraceLine( traceStart, traceStart - Vector( 0, 0, 48.0f), MASK_SHOT_HULL, pEnt, COLLISION_GROUP_NONE, &tr );
	if( tr.fraction < 1.0 && tr.m_pEnt )
	{
		surfacedata_t *psurf = EntityList()->PhysGetProps()->GetSurfaceData( tr.surface.surfaceProps );
		if( psurf )
		{
			EmitSound_t params;
			if( bLeftFoot )
			{
				params.m_pSoundName = EntityList()->PhysGetProps()->GetString(psurf->sounds.stepleft);
			}
			else
			{
				params.m_pSoundName = EntityList()->PhysGetProps()->GetString(psurf->sounds.stepright);
			}

			CPASAttenuationFilter filter( pEnt, params.m_pSoundName );

			params.m_bWarnOnDirectWaveReference = true;
			params.m_flVolume = flVolume;

			g_pSoundEmitterSystem->EmitSound( filter, pEnt->entindex(), params );//pEnt->
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *origin - 
//			*angles - 
//			event - 
//			*options - 
//			numAttachments - 
//			attachments[] - 
//-----------------------------------------------------------------------------
void C_BaseAnimating::FireEvent( const Vector& origin, const QAngle& angles, int event, const char *options )
{
	Vector attachOrigin;
	QAngle attachAngles; 

	switch( event )
	{
	case AE_CL_CREATE_PARTICLE_EFFECT:
		{
			int iAttachment = -1;
			int iAttachType = PATTACH_ABSORIGIN_FOLLOW;
			char token[256];
			char szParticleEffect[256];

			// Get the particle effect name
			const char *p = options;
			p = nexttoken(token, p, ' ');
			if ( token ) 
			{
				const char* mtoken = ModifyEventParticles( token );
				if ( !mtoken || mtoken[0] == '\0' )
					return;
				Q_strncpy( szParticleEffect, mtoken, sizeof(szParticleEffect) );
			}

			// Get the attachment type
			p = nexttoken(token, p, ' ');
			if ( token ) 
			{
				iAttachType = GetAttachTypeFromString( token );
				if ( iAttachType == -1 )
				{
					Warning("Invalid attach type specified for particle effect anim event. Trying to spawn effect '%s' with attach type of '%s'\n", szParticleEffect, token );
					return;
				}
			}

			// Get the attachment point index
			p = nexttoken(token, p, ' ');
			if ( token )
			{
				iAttachment = atoi(token);

				// See if we can find any attachment points matching the name
				if ( token[0] != '0' && iAttachment == 0 )
				{
					iAttachment = GetEngineObject()->LookupAttachment( token );
					if ( iAttachment <= 0 )
					{
						Warning( "Failed to find attachment point specified for particle effect anim event. Trying to spawn effect '%s' on attachment named '%s'\n", szParticleEffect, token );
						return;
					}
				}
			}

			// Spawn the particle effect
			ParticleProp()->Create( szParticleEffect, (ParticleAttachment_t)iAttachType, iAttachment );
		}
		break;

	case AE_CL_PLAYSOUND:
		{
			CLocalPlayerFilter filter;

			if (GetEngineObject()->GetAttachmentCount() > 0)
			{
				GetEngineObject()->GetAttachment( 1, attachOrigin, attachAngles );
				g_pSoundEmitterSystem->EmitSound( filter, GetSoundSourceIndex(), options, &attachOrigin );
			}
			else
			{
				g_pSoundEmitterSystem->EmitSound( filter, GetSoundSourceIndex(), options, &GetEngineObject()->GetAbsOrigin() );
			} 
		}
		break;
	case AE_CL_STOPSOUND:
		{
		g_pSoundEmitterSystem->StopSound( GetSoundSourceIndex(), options );
		}
		break;

	case CL_EVENT_FOOTSTEP_LEFT:
		{
#ifndef HL2MP
			char pSoundName[256];
			if ( !options || !options[0] )
			{
				options = "NPC_CombineS";
			}

			Vector vel;
			GetEngineObject()->EstimateAbsVelocity( vel );

			// If he's moving fast enough, play the run sound
			if ( vel.Length2DSqr() > RUN_SPEED_ESTIMATE_SQR )
			{
				Q_snprintf( pSoundName, 256, "%s.RunFootstepLeft", options );
			}
			else
			{
				Q_snprintf( pSoundName, 256, "%s.FootstepLeft", options );
			}

			const char* soundname = pSoundName;
			CPASAttenuationFilter filter(this, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
#endif
		}
		break;

	case CL_EVENT_FOOTSTEP_RIGHT:
		{
#ifndef HL2MP
			char pSoundName[256];
			if ( !options || !options[0] )
			{
				options = "NPC_CombineS";
			}

			Vector vel;
			GetEngineObject()->EstimateAbsVelocity( vel );
			// If he's moving fast enough, play the run sound
			if ( vel.Length2DSqr() > RUN_SPEED_ESTIMATE_SQR )
			{
				Q_snprintf( pSoundName, 256, "%s.RunFootstepRight", options );
			}
			else
			{
				Q_snprintf( pSoundName, 256, "%s.FootstepRight", options );
			}
			const char* soundname = pSoundName;
			CPASAttenuationFilter filter(this, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
#endif
		}
		break;

	case CL_EVENT_MFOOTSTEP_LEFT:
		{
			MaterialFootstepSound( this, true, VOL_NORM * 0.5f );
		}
		break;

	case CL_EVENT_MFOOTSTEP_RIGHT:
		{
			MaterialFootstepSound( this, false, VOL_NORM * 0.5f );
		}
		break;

	case CL_EVENT_MFOOTSTEP_LEFT_LOUD:
		{
			MaterialFootstepSound( this, true, VOL_NORM );
		}
		break;

	case CL_EVENT_MFOOTSTEP_RIGHT_LOUD:
		{
			MaterialFootstepSound( this, false, VOL_NORM );
		}
		break;

	// Eject brass
	case CL_EVENT_EJECTBRASS1:
		if (GetEngineObject()->GetAttachmentCount() > 0 )
		{
			if (g_pViewRender->MainViewOrigin().DistToSqr(GetEngineObject()->GetAbsOrigin() ) < (256 * 256) )
			{
				Vector attachOrigin;
				QAngle attachAngles; 
				
				if(GetEngineObject()->GetAttachment( 2, attachOrigin, attachAngles ) )
				{
					tempents->EjectBrass( attachOrigin, attachAngles, GetEngineObject()->GetAbsAngles(), atoi( options ) );
				}
			}
		}
		break;

	case AE_MUZZLEFLASH:
		{
			// Send out the effect for a player
			DispatchMuzzleEffect( options, true );
			break;
		}

	case AE_NPC_MUZZLEFLASH:
		{
			// Send out the effect for an NPC
			DispatchMuzzleEffect( options, false );
			break;
		}

	// OBSOLETE EVENTS. REPLACED BY NEWER SYSTEMS.
	// See below in FireObsoleteEvent() for comments on what to use instead.
	case AE_CLIENT_EFFECT_ATTACH:
	case CL_EVENT_DISPATCHEFFECT0:
	case CL_EVENT_DISPATCHEFFECT1:
	case CL_EVENT_DISPATCHEFFECT2:
	case CL_EVENT_DISPATCHEFFECT3:
	case CL_EVENT_DISPATCHEFFECT4:
	case CL_EVENT_DISPATCHEFFECT5:
	case CL_EVENT_DISPATCHEFFECT6:
	case CL_EVENT_DISPATCHEFFECT7:
	case CL_EVENT_DISPATCHEFFECT8:
	case CL_EVENT_DISPATCHEFFECT9:
	case CL_EVENT_MUZZLEFLASH0:
	case CL_EVENT_MUZZLEFLASH1:
	case CL_EVENT_MUZZLEFLASH2:
	case CL_EVENT_MUZZLEFLASH3:
	case CL_EVENT_NPC_MUZZLEFLASH0:
	case CL_EVENT_NPC_MUZZLEFLASH1:
	case CL_EVENT_NPC_MUZZLEFLASH2:
	case CL_EVENT_NPC_MUZZLEFLASH3:
	case CL_EVENT_SPARK0:
	case CL_EVENT_SOUND:
		FireObsoleteEvent( origin, angles, event, options );
		break;

	case AE_CL_ENABLE_BODYGROUP:
		{
			int index = GetEngineObject()->FindBodygroupByName( options );
			if ( index >= 0 )
			{
				GetEngineObject()->SetBodygroup( index, 1 );
			}
		}
		break;

	case AE_CL_DISABLE_BODYGROUP:
		{
			int index = GetEngineObject()->FindBodygroupByName( options );
			if ( index >= 0 )
			{
				GetEngineObject()->SetBodygroup( index, 0 );
			}
		}
		break;

	case AE_CL_BODYGROUP_SET_VALUE:
		{
			char szBodygroupName[256];
			int value = 0;

			char token[256];

			const char *p = options;

			// Bodygroup Name
			p = nexttoken(token, p, ' ');
			if ( token ) 
			{
				Q_strncpy( szBodygroupName, token, sizeof(szBodygroupName) );
			}

			// Get the desired value
			p = nexttoken(token, p, ' ');
			if ( token ) 
			{
				value = atoi( token );
			}

			int index = GetEngineObject()->FindBodygroupByName( szBodygroupName );
			if ( index >= 0 )
			{
				GetEngineObject()->SetBodygroup( index, value );
			}
		}
		break;

	default:
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: These events are all obsolete events, left here to support old games.
//			Their systems have all been replaced with better ones.
//-----------------------------------------------------------------------------
void C_BaseAnimating::FireObsoleteEvent( const Vector& origin, const QAngle& angles, int event, const char *options )
{
	Vector attachOrigin;
	QAngle attachAngles; 

	switch( event )
	{
	// Obsolete. Use the AE_CL_CREATE_PARTICLE_EFFECT event instead, which uses the artist driven particle system & editor.
	case AE_CLIENT_EFFECT_ATTACH:
		{
			int iAttachment = -1;
			int iParam = 0;
			char token[128];
			char effectFunc[128];

			const char *p = options;

			p = nexttoken(token, p, ' ');

			if( token ) 
			{
				Q_strncpy( effectFunc, token, sizeof(effectFunc) );
			}

			p = nexttoken(token, p, ' ');

			if( token )
			{
				iAttachment = atoi(token);
			}

			p = nexttoken(token, p, ' ');

			if( token )
			{
				iParam = atoi(token);
			}

			if ( iAttachment != -1 && GetEngineObject()->GetAttachmentCount() >= iAttachment )
			{
				GetEngineObject()->GetAttachment( iAttachment, attachOrigin, attachAngles );

				// Fill out the generic data
				CEffectData data;
				data.m_vOrigin = attachOrigin;
				data.m_vAngles = attachAngles;
				AngleVectors( attachAngles, &data.m_vNormal );
				data.m_hEntity = this;
				data.m_nAttachmentIndex = iAttachment + 1;
				data.m_fFlags = iParam;

				DispatchEffect( effectFunc, data );
			}
		}
		break;

	// Obsolete. Use the AE_CL_CREATE_PARTICLE_EFFECT event instead, which uses the artist driven particle system & editor.
	case CL_EVENT_DISPATCHEFFECT0:
	case CL_EVENT_DISPATCHEFFECT1:
	case CL_EVENT_DISPATCHEFFECT2:
	case CL_EVENT_DISPATCHEFFECT3:
	case CL_EVENT_DISPATCHEFFECT4:
	case CL_EVENT_DISPATCHEFFECT5:
	case CL_EVENT_DISPATCHEFFECT6:
	case CL_EVENT_DISPATCHEFFECT7:
	case CL_EVENT_DISPATCHEFFECT8:
	case CL_EVENT_DISPATCHEFFECT9:
		{
			int iAttachment = -1;

			// First person muzzle flashes
			switch (event) 
			{
			case CL_EVENT_DISPATCHEFFECT0:
				iAttachment = 0;
				break;

			case CL_EVENT_DISPATCHEFFECT1:
				iAttachment = 1;
				break;

			case CL_EVENT_DISPATCHEFFECT2:
				iAttachment = 2;
				break;

			case CL_EVENT_DISPATCHEFFECT3:
				iAttachment = 3;
				break;

			case CL_EVENT_DISPATCHEFFECT4:
				iAttachment = 4;
				break;

			case CL_EVENT_DISPATCHEFFECT5:
				iAttachment = 5;
				break;

			case CL_EVENT_DISPATCHEFFECT6:
				iAttachment = 6;
				break;

			case CL_EVENT_DISPATCHEFFECT7:
				iAttachment = 7;
				break;

			case CL_EVENT_DISPATCHEFFECT8:
				iAttachment = 8;
				break;

			case CL_EVENT_DISPATCHEFFECT9:
				iAttachment = 9;
				break;
			}

			if ( iAttachment != -1 && GetEngineObject()->GetAttachmentCount() > iAttachment )
			{
				GetEngineObject()->GetAttachment( iAttachment+1, attachOrigin, attachAngles );

				// Fill out the generic data
				CEffectData data;
				data.m_vOrigin = attachOrigin;
				data.m_vAngles = attachAngles;
				AngleVectors( attachAngles, &data.m_vNormal );
				data.m_hEntity = this;
				data.m_nAttachmentIndex = iAttachment + 1;

				DispatchEffect( options, data );
			}
		}
		break;

	// Obsolete. Use the AE_MUZZLEFLASH / AE_NPC_MUZZLEFLASH events instead.
	case CL_EVENT_MUZZLEFLASH0:
	case CL_EVENT_MUZZLEFLASH1:
	case CL_EVENT_MUZZLEFLASH2:
	case CL_EVENT_MUZZLEFLASH3:
	case CL_EVENT_NPC_MUZZLEFLASH0:
	case CL_EVENT_NPC_MUZZLEFLASH1:
	case CL_EVENT_NPC_MUZZLEFLASH2:
	case CL_EVENT_NPC_MUZZLEFLASH3:
		{
			int iAttachment = -1;
			bool bFirstPerson = true;

			// First person muzzle flashes
			switch (event) 
			{
			case CL_EVENT_MUZZLEFLASH0:
				iAttachment = 0;
				break;

			case CL_EVENT_MUZZLEFLASH1:
				iAttachment = 1;
				break;

			case CL_EVENT_MUZZLEFLASH2:
				iAttachment = 2;
				break;

			case CL_EVENT_MUZZLEFLASH3:
				iAttachment = 3;
				break;

				// Third person muzzle flashes
			case CL_EVENT_NPC_MUZZLEFLASH0:
				iAttachment = 0;
				bFirstPerson = false;
				break;

			case CL_EVENT_NPC_MUZZLEFLASH1:
				iAttachment = 1;
				bFirstPerson = false;
				break;

			case CL_EVENT_NPC_MUZZLEFLASH2:
				iAttachment = 2;
				bFirstPerson = false;
				break;

			case CL_EVENT_NPC_MUZZLEFLASH3:
				iAttachment = 3;
				bFirstPerson = false;
				break;
			}

			if ( iAttachment != -1 && GetEngineObject()->GetAttachmentCount() > iAttachment )
			{
				GetEngineObject()->GetAttachment( iAttachment+1, attachOrigin, attachAngles );
				int entId = render->GetViewEntity();
				C_BaseEntity* hEntity = (C_BaseEntity*)EntityList()->GetBaseEntity( entId );
				tempents->MuzzleFlash( attachOrigin, attachAngles, atoi( options ), hEntity, bFirstPerson );
			}
		}
		break;

	// Obsolete: Use the AE_CL_CREATE_PARTICLE_EFFECT event instead, which uses the artist driven particle system & editor.
	case CL_EVENT_SPARK0:
		{
			Vector vecForward;
			GetEngineObject()->GetAttachment( 1, attachOrigin, attachAngles );
			AngleVectors( attachAngles, &vecForward );
			g_pEffects->Sparks( attachOrigin, atoi( options ), 1, &vecForward );
		}
		break;

	// Obsolete: Use the AE_CL_PLAYSOUND event instead, which doesn't rely on a magic number in the .qc
	case CL_EVENT_SOUND:
		{
			CLocalPlayerFilter filter;

			if (GetEngineObject()->GetAttachmentCount() > 0)
			{
				GetEngineObject()->GetAttachment( 1, attachOrigin, attachAngles );
				g_pSoundEmitterSystem->EmitSound( filter, GetSoundSourceIndex(), options, &attachOrigin );
			}
			else
			{
				g_pSoundEmitterSystem->EmitSound( filter, GetSoundSourceIndex(), options );
			}
		}
		break;

	default:
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
//bool C_BaseAnimating::IsSelfAnimating()
//{
//	if (GetEngineObject()->IsUsingClientSideAnimation())
//		return true;
//
//	// Yes, we use animtime.
//	int iMoveType = GetEngineObject()->GetMoveType();
//	if ( iMoveType != MOVETYPE_STEP && 
//		  iMoveType != MOVETYPE_NONE && 
//		  iMoveType != MOVETYPE_WALK &&
//		  iMoveType != MOVETYPE_FLY &&
//		  iMoveType != MOVETYPE_FLYGRAVITY )
//	{
//		return true;
//	}
//
//	return false;
//}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------

bool C_BaseAnimating::Interpolate( float flCurrentTime )
{
	// ragdolls don't need interpolation
	if (GetEngineObject()->RagdollBoneCount())
		return true;

	VPROF( "C_BaseAnimating::Interpolate" );

	Vector oldOrigin;
	QAngle oldAngles;
	Vector oldVel;
	float flOldCycle = GetEngineObject()->GetCycle();
	int nChangeFlags = 0;



	int bNoMoreChanges;
	int retVal = GetEngineObject()->BaseInterpolatePart1( flCurrentTime, oldOrigin, oldAngles, oldVel, bNoMoreChanges );
	if ( retVal == INTERPOLATE_STOP )
	{
		if ( bNoMoreChanges )
			GetEngineObject()->RemoveFromInterpolationList();
		return true;
	}


	// Did cycle change?
	if(GetEngineObject()->GetCycle() != flOldCycle )
		nChangeFlags |= ANIMATION_CHANGED;

	if ( bNoMoreChanges )
		GetEngineObject()->RemoveFromInterpolationList();
	
	GetEngineObject()->BaseInterpolatePart2( oldOrigin, oldAngles, oldVel, nChangeFlags );
	return true;
}

//-----------------------------------------------------------------------------
// Lets us check our sequence number after a network update
//-----------------------------------------------------------------------------
void C_BaseAnimating::OnPostRestoreData()// const char *context, int slot, int type 
{
	//int retVal = BaseClass::RestoreData( context, slot, type );
	BaseClass::OnPostRestoreData();
	IStudioHdr *pHdr = GetEngineObject()->GetModelPtr();
	if( pHdr && GetEngineObject()->GetSequence() >= pHdr->GetNumSeq())
	{
		// Don't let a network update give us an invalid sequence
		GetEngineObject()->SetSequence(0);
	}
	//return retVal;
}


//-----------------------------------------------------------------------------
// implements these so ragdolls can handle frustum culling & leaf visibility
//-----------------------------------------------------------------------------

void C_BaseAnimating::GetRenderBounds( Vector& theMins, Vector& theMaxs )
{
	if (GetEngineObject()->IsRagdoll() )
	{
		GetEngineObject()->GetRagdollBounds( theMins, theMaxs );
	}
	else if (GetEngineObject()->GetModel() )
	{
		IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
		if ( !pStudioHdr|| !pStudioHdr->SequencesAvailable() || GetEngineObject()->GetSequence() == -1 )
		{
			theMins = vec3_origin;
			theMaxs = vec3_origin;
			return;
		} 
		if (!VectorCompare( vec3_origin, pStudioHdr->view_bbmin() ) || !VectorCompare( vec3_origin, pStudioHdr->view_bbmax() ))
		{
			// clipping bounding box
			VectorCopy ( pStudioHdr->view_bbmin(), theMins);
			VectorCopy ( pStudioHdr->view_bbmax(), theMaxs);
		}
		else
		{
			// movement bounding box
			VectorCopy ( pStudioHdr->hull_min(), theMins);
			VectorCopy ( pStudioHdr->hull_max(), theMaxs);
		}

		mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc(GetEngineObject()->GetSequence() );
		VectorMin( seqdesc.bbmin, theMins, theMins );
		VectorMax( seqdesc.bbmax, theMaxs, theMaxs );
	}
	else
	{
		theMins = vec3_origin;
		theMaxs = vec3_origin;
	}

	// Scale this up depending on if our model is currently scaling
	const float flScale = GetEngineObject()->GetModelScale();
	theMaxs *= flScale;
	theMins *= flScale;
}


//-----------------------------------------------------------------------------
// implements these so ragdolls can handle frustum culling & leaf visibility
//-----------------------------------------------------------------------------
const Vector& C_BaseAnimating::GetRenderOrigin( void )
{
	if (GetEngineObject()->IsRagdoll() )
	{
		return GetEngineObject()->GetRagdollOrigin();
	}
	else
	{
		return BaseClass::GetRenderOrigin();	
	}
}

const QAngle& C_BaseAnimating::GetRenderAngles( void )
{
	if (GetEngineObject()->IsRagdoll() )
	{
		return vec3_angle;
			
	}
	else
	{
		return BaseClass::GetRenderAngles();	
	}
}




//-----------------------------------------------------------------------------
// Purpose: My physics object has been updated, react or extract data
//-----------------------------------------------------------------------------
void C_BaseAnimating::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	// FIXME: Should make sure the physics objects being passed in
	// is the ragdoll physics object, but I think it's pretty safe not to check
	//if (GetEngineObject()->IsRagdoll())
	//{	 
	//	GetEngineObject()->VPhysicsUpdate( pPhysics );
	//	
	//	GetEngineObject()->RagdollMoved();

	//	return;
	//}

	BaseClass::VPhysicsUpdate( pPhysics );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : updateType - 
//-----------------------------------------------------------------------------
void C_BaseAnimating::PreDataUpdate( DataUpdateType_t updateType )
{
	VPROF( "C_BaseAnimating::PreDataUpdate" );

	BaseClass::PreDataUpdate( updateType );
}

void C_BaseAnimating::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	BaseClass::NotifyShouldTransmit( state );

	if ( state == SHOULDTRANSMIT_START )
	{
		// If he's been firing a bunch, then he comes back into the PVS, his muzzle flash
		// will show up even if he isn't firing now.
		GetEngineObject()->DisableMuzzleFlash();

		m_nPrevResetEventsParity = GetEngineObject()->GetResetEventsParity();
		m_nEventSequence = GetEngineObject()->GetSequence();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : updateType - 
//-----------------------------------------------------------------------------
void C_BaseAnimating::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate( updateType );


}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bnewentity - 
//-----------------------------------------------------------------------------
void C_BaseAnimating::OnPreDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnPreDataChanged( updateType );

}

void C_BaseAnimating::ForceSetupBonesAtTime( matrix3x4_t *pBonesOut, float flTime )
{
	// blow the cached prev bones
	GetEngineObject()->InvalidateBoneCache();

	// reset root position to flTime
	Interpolate( flTime );

	// Setup bone state at the given time
	GetEngineObject()->SetupBones( pBonesOut, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, flTime );
}

void C_BaseAnimating::GetRagdollInitBoneArrays( matrix3x4_t *pDeltaBones0, matrix3x4_t *pDeltaBones1, matrix3x4_t *pCurrentBones, float boneDt )
{
	ForceSetupBonesAtTime( pDeltaBones0, gpGlobals->curtime - boneDt );
	ForceSetupBonesAtTime( pDeltaBones1, gpGlobals->curtime );
	float ragdollCreateTime = EntityList()->PhysGetSyncCreateTime();
	if ( ragdollCreateTime != gpGlobals->curtime )
	{
		// The next simulation frame begins before the end of this frame
		// so initialize the ragdoll at that time so that it will reach the current
		// position at curtime.  Otherwise the ragdoll will simulate forward from curtime
		// and pop into the future a bit at this point of transition
		ForceSetupBonesAtTime( pCurrentBones, ragdollCreateTime );
	}
	else
	{
		memcpy( pCurrentBones, GetEngineObject()->GetBoneArray(), sizeof(matrix3x4_t) * GetEngineObject()->GetBoneCount());
	}
}

bool NPC_IsImportantNPC(C_BaseEntity* pAnimating)
{
	C_AI_BaseNPC* pBaseNPC = dynamic_cast <C_AI_BaseNPC*> (pAnimating);

	if (pBaseNPC == NULL)
		return false;

	return pBaseNPC->ImportantRagdoll();
}

C_BaseEntity *C_BaseAnimating::BecomeRagdollOnClient()
{
	GetEngineObject()->MoveToLastReceivedPosition( true );
	GetEngineObject()->GetAbsOrigin();

	C_ClientRagdoll *pRagdoll = CreateRagdollCopy();
	if (!pRagdoll)
	{
		return NULL;
	}

	TermRopes();
	const model_t* model = GetEngineObject()->GetModel();
	const char* pModelName = modelinfo->GetModelName(model);

	if (pRagdoll->InitializeAsClientEntity(pModelName, RENDER_GROUP_OPAQUE_ENTITY) == false)
	{
		EntityList()->DestroyEntity(pRagdoll);// ->Release();
		return NULL;
	}

	// move my current model instance to the ragdoll's so decals are preserved.
	GetEngineObject()->SnatchModelInstance(pRagdoll->GetEngineObject());

	// We need to take these from the entity
	pRagdoll->GetEngineObject()->SetAbsOrigin(GetEngineObject()->GetAbsOrigin());
	pRagdoll->GetEngineObject()->SetAbsAngles(GetEngineObject()->GetAbsAngles());

	pRagdoll->IgniteRagdoll(this);
	pRagdoll->TransferDissolveFrom(this);
	pRagdoll->InitModelEffects();

	if (GetEngineObject()->IsEffectActive(EF_NOSHADOW))
	{
		pRagdoll->GetEngineObject()->AddEffects(EF_NOSHADOW);
	}
	pRagdoll->GetEngineObject()->SetRenderFX(kRenderFxRagdoll);
	pRagdoll->SetRenderMode(GetRenderMode());
	pRagdoll->SetRenderColor(GetRenderColor().r, GetRenderColor().g, GetRenderColor().b, GetRenderColor().a);

	pRagdoll->GetEngineObject()->SetBody(GetEngineObject()->GetBody());
	pRagdoll->GetEngineObject()->SetSkin(GetEngineObject()->GetSkin());
	pRagdoll->GetEngineObject()->SetVecForce(GetEngineObject()->GetVecForce());
	pRagdoll->GetEngineObject()->SetForceBone(GetEngineObject()->GetForceBone());
	pRagdoll->SetNextClientThink(CLIENT_THINK_ALWAYS);

	pRagdoll->GetEngineObject()->SetModelName(AllocPooledString(pModelName));
	pRagdoll->GetEngineObject()->SetModelScale(GetEngineObject()->GetModelScale());
	matrix3x4_t boneDelta0[MAXSTUDIOBONES];
	matrix3x4_t boneDelta1[MAXSTUDIOBONES];
	matrix3x4_t currentBones[MAXSTUDIOBONES];
	const float boneDt = 0.1f;
	GetRagdollInitBoneArrays( boneDelta0, boneDelta1, currentBones, boneDt );
	pRagdoll->GetEngineObject()->InitAsClientRagdoll( boneDelta0, boneDelta1, currentBones, boneDt );
	NoteRagdollCreationTick(this);
	if (AddRagdollToFadeQueue() == true)
	{
		pRagdoll->m_bImportant = NPC_IsImportantNPC(this);
		EntityList()->MoveToTopOfLRU(pRagdoll, pRagdoll->m_bImportant);
		pRagdoll->m_bFadeOut = true;
	}

	GetEngineObject()->AddEffects(EF_NODRAW);
	GetEngineObject()->SetBuiltRagdoll(true);
	return pRagdoll;
}

C_ClientRagdoll* C_BaseAnimating::CreateRagdollCopy()
{
	//Adrian: We now create a separate entity that becomes this entity's ragdoll.
	//That way the server side version of this entity can go away. 
	//Plus we can hook save/restore code to these ragdolls so they don't fall on restore anymore.
	C_ClientRagdoll* pRagdoll = (C_ClientRagdoll*)EntityList()->CreateEntityByName("C_ClientRagdoll");//false
	return pRagdoll;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bnewentity - 
//-----------------------------------------------------------------------------
void C_BaseAnimating::OnDataChanged( DataUpdateType_t updateType )
{


	BaseClass::OnDataChanged( updateType );

	
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseAnimating::AddEntity( void )
{
	// Server says don't interpolate this frame, so set previous info to new info.
	if (GetEngineObject()->IsNoInterpolationFrame() )
	{
		GetEngineObject()->ResetLatched();
	}

	BaseClass::AddEntity();
}

unsigned int C_BaseAnimating::ComputeClientSideAnimationFlags()
{
	return FCLIENTANIM_SEQUENCE_CYCLE;
}

void C_BaseAnimating::UpdateClientSideAnimation()
{
	// Update client side animation
	if (GetEngineObject()->IsUsingClientSideAnimation())
	{
		//Assert( m_ClientSideAnimationListHandle != INVALID_CLIENTSIDEANIMATION_LIST_HANDLE );
		if (GetEngineObject()->GetSequence() != -1 )
		{
			// latch old values
			GetEngineObject()->OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
			// move frame forward
			FrameAdvance( 0.0f ); // 0 means to use the time we last advanced instead of a constant
		}
	}
	else
	{
		//Assert( m_ClientSideAnimationListHandle == INVALID_CLIENTSIDEANIMATION_LIST_HANDLE );
	}
}

void C_BaseAnimating::Simulate()
{
	if ( m_bInitModelEffects )
	{
		DelayedInitModelEffects();
	}

	if ( gpGlobals->frametime != 0.0f  )
	{
		DoAnimationEvents(GetEngineObject()->GetModelPtr() );
	}
	BaseClass::Simulate();
	if (GetEngineObject()->IsNoInterpolationFrame() )
	{
		GetEngineObject()->ResetLatched();
	}
}

bool C_BaseAnimating::TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	if ( ray.m_IsRay && GetEngineObject()->IsSolidFlagSet( FSOLID_CUSTOMRAYTEST ))
	{
		if (!TestHitboxes( ray, fContentsMask, tr ))
			return true;

		return tr.DidHit();
	}

	if ( !ray.m_IsRay && GetEngineObject()->IsSolidFlagSet( FSOLID_CUSTOMBOXTEST ))
	{
		if (!TestHitboxes( ray, fContentsMask, tr ))
			return true;

		return true;
	}

	// We shouldn't get here.
	Assert(0);
	return false;
}

// UNDONE: This almost works.  The client entities have no control over their solid box
// Also they have no ability to expose FSOLID_ flags to the engine to force the accurate
// collision tests.
// Add those and the client hitboxes will be robust
bool C_BaseAnimating::TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	VPROF( "C_BaseAnimating::TestHitboxes" );

	MDLCACHE_CRITICAL_SECTION();

	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	if (!pStudioHdr)
		return false;

	mstudiohitboxset_t *set = pStudioHdr->pHitboxSet(GetEngineObject()->GetHitboxSet() );
	if ( !set || !set->numhitboxes )
		return false;

	// Use vcollide for box traces.
	if ( !ray.m_IsRay )
		return false;

	// This *has* to be true for the existing code to function correctly.
	Assert( ray.m_StartOffset == vec3_origin );

	const matrix3x4_t *hitboxbones[MAXSTUDIOBONES];
	GetEngineObject()->GetHitboxBoneTransforms(hitboxbones);

	if ( TraceToStudio(EntityList()->PhysGetProps(), ray, pStudioHdr, set, hitboxbones, fContentsMask, GetRenderOrigin(), GetEngineObject()->GetModelScale(), tr ) )
	{
		mstudiobbox_t *pbox = set->pHitbox( tr.hitbox );
		mstudiobone_t *pBone = pStudioHdr->pBone(pbox->bone);
		tr.surface.name = "**studio**";
		tr.surface.flags = SURF_HITBOX;
		tr.surface.surfaceProps = EntityList()->PhysGetProps()->GetSurfaceIndex( pBone->pszSurfaceProp() );
		if (GetEngineObject()->IsRagdoll() )
		{
			IPhysicsObject *pReplace = GetEngineObject()->GetElement( tr.physicsbone );
			if ( pReplace )
			{
				GetEngineObject()->VPhysicsSetObject( NULL );
				GetEngineObject()->VPhysicsSetObject( pReplace );
			}
		}
	}

	return true;
}

float C_BaseAnimating::GetAnimTimeInterval( void ) const
{
#define MAX_ANIMTIME_INTERVAL 0.2f

	float flInterval = MIN( gpGlobals->curtime - GetEngineObject()->GetAnimTime(), MAX_ANIMTIME_INTERVAL);
	return flInterval;
}

//=========================================================
// StudioFrameAdvance - advance the animation frame up some interval (default 0.1) into the future
//=========================================================
void C_BaseAnimating::StudioFrameAdvance()
{
	if (GetEngineObject()->IsUsingClientSideAnimation())
		return;

	IStudioHdr *hdr = GetEngineObject()->GetModelPtr();
	if ( !hdr )
		return;

#ifdef DEBUG
	bool watch = dbganimmodel.GetString()[0] && V_stristr( hdr->pszName(), dbganimmodel.GetString() );
#else
	bool watch = false; // Q_strstr( hdr->name, "rifle" ) ? true : false;
#endif

	//if (!anim.prevanimtime)
	//{
		//anim.prevanimtime = m_flAnimTime = gpGlobals->curtime;
	//}

	// How long since last animtime
	float flInterval = GetAnimTimeInterval();

	if (flInterval <= 0.001)
	{
		// Msg("%s : %s : %5.3f (skip)\n", STRING(pev->classname), GetSequenceName( GetSequence() ), GetCycle() );
		return;
	}

	GetEngineObject()->UpdateModelScale();

	//anim.prevanimtime = m_flAnimTime;
	float cycleAdvance = flInterval * GetEngineObject()->GetSequenceCycleRate( hdr, GetEngineObject()->GetSequence() ) * GetEngineObject()->GetPlaybackRate();
	float flNewCycle = GetEngineObject()->GetCycle() + cycleAdvance;
	GetEngineObject()->SetAnimTime(gpGlobals->curtime);

	if ( watch )
	{
		Msg("%s %6.3f : %6.3f (%.3f)\n", GetClassname(), gpGlobals->curtime, GetEngineObject()->GetAnimTime(), flInterval);
	}

	if ( flNewCycle < 0.0f || flNewCycle >= 1.0f ) 
	{
		if (GetEngineObject()->IsSequenceLooping( hdr, GetEngineObject()->GetSequence() ) )
		{
			 flNewCycle -= (int)(flNewCycle);
		}
		else
		{
		 	 flNewCycle = (flNewCycle < 0.0f) ? 0.0f : 1.0f;
		}
		
		GetEngineObject()->SetSequenceFinished(true);	// just in case it wasn't caught in GetEvents
	}

	GetEngineObject()->SetCycle( flNewCycle );

	GetEngineObject()->SetGroundSpeed(GetEngineObject()->GetSequenceGroundSpeed( hdr, GetEngineObject()->GetSequence() ) * GetEngineObject()->GetModelScale());

#if 0
	// I didn't have a test case for this, but it seems like the right thing to do.  Check multi-player!

	// Msg("%s : %s : %5.1f\n", GetClassname(), GetSequenceName( GetSequence() ), GetCycle() );
	GetEngineObject()->InvalidatePhysicsRecursive( ANIMATION_CHANGED );
#endif

	if ( watch )
	{
		Msg("%s : %s : %5.1f\n", GetClassname(), GetEngineObject()->GetSequenceName(GetEngineObject()->GetSequence() ), GetEngineObject()->GetCycle() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flInterval - 
// Output : float
//-----------------------------------------------------------------------------
float C_BaseAnimating::FrameAdvance( float flInterval )
{
	IStudioHdr *hdr = GetEngineObject()->GetModelPtr();
	if ( !hdr )
		return 0.0f;

#ifdef DEBUG
	bool bWatch = dbganimmodel.GetString()[0] && V_stristr( hdr->pszName(), dbganimmodel.GetString() );
#else
	bool bWatch = false; // Q_strstr( hdr->name, "medkit_large" ) ? true : false;
#endif

	float curtime = gpGlobals->curtime;

	if (flInterval == 0.0f)
	{
		flInterval = ( curtime - GetEngineObject()->GetAnimTime() );
		if (flInterval <= 0.001f)
		{
			return 0.0f;
		}
	}

	if ( !GetEngineObject()->GetAnimTime() )
	{
		flInterval = 0.0f;
	}

	float cyclerate = GetEngineObject()->GetSequenceCycleRate( hdr, GetEngineObject()->GetSequence() );
	float addcycle = flInterval * cyclerate * GetEngineObject()->GetPlaybackRate();

	if( GetServerIntendedCycle() != -1.0f )
	{
		// The server would like us to ease in a correction so that we will animate the same on the client and server.
		// So we will actually advance the average of what we would have done and what the server wants.
		float serverCycle = GetServerIntendedCycle();
		float serverAdvance = serverCycle - GetEngineObject()->GetCycle();
		bool adjustOkay = serverAdvance > 0.0f;// only want to go forward. backing up looks really jarring, even when slight
		if( serverAdvance < -0.8f )
		{
			// Oh wait, it was just a wraparound from .9 to .1.
			serverAdvance += 1;
			adjustOkay = true;
		}

		if( adjustOkay )
		{
			float originalAdvance = addcycle;
			addcycle = (serverAdvance + addcycle) / 2;

			const float MAX_CYCLE_ADJUSTMENT = 0.1f;
			addcycle = MIN( MAX_CYCLE_ADJUSTMENT, addcycle );// Don't do too big of a jump; it's too jarring as well.

			DevMsg( 2, "(%d): Cycle latch used to correct %.2f in to %.2f instead of %.2f.\n",
				entindex(), GetEngineObject()->GetCycle(), GetEngineObject()->GetCycle() + addcycle, GetEngineObject()->GetCycle() + originalAdvance );
		}

		SetServerIntendedCycle(-1.0f); // Only use a correction once, it isn't valid any time but right now.
	}

	float flNewCycle = GetEngineObject()->GetCycle() + addcycle;
	GetEngineObject()->SetAnimTime(curtime);

	if ( bWatch )
	{
		Msg("%i CLIENT Time: %6.3f : (Interval %f) : cycle %f rate %f add %f\n", 
			gpGlobals->tickcount, gpGlobals->curtime, flInterval, flNewCycle, cyclerate, addcycle );
	}

	if ( (flNewCycle < 0.0f) || (flNewCycle >= 1.0f) ) 
	{
		if (GetEngineObject()->IsSequenceLooping( hdr, GetEngineObject()->GetSequence() ) )
		{
			flNewCycle -= (int)(flNewCycle);
		}
		else
		{
			flNewCycle = (flNewCycle < 0.0f) ? 0.0f : 1.0f;
		}
		GetEngineObject()->SetSequenceFinished(true);
	}

	GetEngineObject()->SetCycle( flNewCycle );

	return flInterval;
}

void C_BaseAnimating::Clear( void )
{

	//m_bResetSequenceInfoOnLoad = false;
	//m_bDynamicModelPending = false;
	//m_AutoRefModelIndex.Clear();
	BaseClass::Clear();	
}

void C_BaseAnimating::GetAimEntOrigin( IClientEntity *pAttachedTo, Vector *pAbsOrigin, QAngle *pAbsAngles )
{
	IEngineObjectClient *pMoveParent;
	if (GetEngineObject()->IsEffectActive( EF_BONEMERGE ) && GetEngineObject()->IsEffectActive( EF_BONEMERGE_FASTCULL ) && (pMoveParent = GetEngineObject()->GetMoveParent()) != NULL )
	{
		// Doing this saves a lot of CPU.
		*pAbsOrigin = pMoveParent->GetOuter()->WorldSpaceCenter();
		*pAbsAngles = pMoveParent->GetOuter()->GetRenderAngles();
	}
	else
	{
		if (!GetEngineObject()->GetAimEntOrigin(pAbsOrigin, pAbsAngles))
			BaseClass::GetAimEntOrigin( pAttachedTo, pAbsOrigin, pAbsAngles );
	}
}

CBoneList *C_BaseAnimating::RecordBones( IStudioHdr *hdr, matrix3x4_t *pBoneState )
{
	if ( !ToolsEnabled() )
		return NULL;
		
	VPROF_BUDGET( "C_BaseAnimating::RecordBones", VPROF_BUDGETGROUP_TOOLS );

	// Possible optimization: Instead of inverting everything while recording, record the pos/q stuff into a structure instead?
	Assert( hdr );

	// Setup our transform based on render angles and origin.
	matrix3x4_t parentTransform;
	AngleMatrix( GetRenderAngles(), GetRenderOrigin(), parentTransform );

	CBoneList *boneList = CBoneList::Alloc();
	Assert( boneList );

	boneList->m_nBones = hdr->numbones();

	for ( int i = 0;  i < hdr->numbones(); i++ )
	{
		matrix3x4_t inverted;
		matrix3x4_t output;

		mstudiobone_t *bone = hdr->pBone( i );

		// Only update bones referenced during setup
		if ( !(bone->flags & BONE_USED_BY_ANYTHING ) )
		{
			boneList->m_quatRot[ i ].Init( 0.0f, 0.0f, 0.0f, 1.0f ); // Init by default sets all 0's, which is invalid
			boneList->m_vecPos[ i ].Init();
			continue;
		}

		if ( bone->parent == -1 )
		{
			// Decompose into parent space
			MatrixInvert( parentTransform, inverted );
		}
		else
		{
			MatrixInvert( pBoneState[ bone->parent ], inverted );
		}

		ConcatTransforms( inverted, pBoneState[ i ], output );

		MatrixAngles( output, 
			boneList->m_quatRot[ i ],
			boneList->m_vecPos[ i ] );
	}

	return boneList;
}

void C_BaseAnimating::GetToolRecordingState( KeyValues *msg )
{
	if ( !ToolsEnabled() )
		return;

	VPROF_BUDGET( "C_BaseAnimating::GetToolRecordingState", VPROF_BUDGETGROUP_TOOLS );

	// Force the animation to drive bones
	IStudioHdr *hdr = GetEngineObject()->GetModelPtr();
	matrix3x4_t *pBones = (matrix3x4_t*)_alloca( ( hdr ? hdr->numbones() : 1 ) * sizeof(matrix3x4_t) );
	if ( hdr )
	{
		GetEngineObject()->SetupBones( pBones, hdr->numbones(), BONE_USED_BY_ANYTHING, gpGlobals->curtime );
	}
	else
	{
		GetEngineObject()->SetupBones( NULL, -1, BONE_USED_BY_ANYTHING, gpGlobals->curtime );
	}

	BaseClass::GetToolRecordingState( msg );

	static BaseAnimatingRecordingState_t state;
	state.m_nSkin = GetEngineObject()->GetSkin();
	state.m_nBody = GetEngineObject()->GetBody();
	state.m_nSequence = GetEngineObject()->GetSequence();
	state.m_pBoneList = NULL;
	msg->SetPtr( "baseanimating", &state );
	msg->SetInt( "viewmodel", IsViewModel() ? 1 : 0 );

	if ( hdr )
	{
		state.m_pBoneList = RecordBones( hdr, pBones );
	}
}

void C_BaseAnimating::CleanupToolRecordingState( KeyValues *msg )
{
	if ( !ToolsEnabled() )
		return;
		    
	BaseAnimatingRecordingState_t *pState = (BaseAnimatingRecordingState_t*)msg->GetPtr( "baseanimating" );
	if ( pState && pState->m_pBoneList )
	{
		pState->m_pBoneList->Release();
	}

	BaseClass::CleanupToolRecordingState( msg );
}



//-----------------------------------------------------------------------------
// Purpose: See if we should force reset our sequence on a new model
//-----------------------------------------------------------------------------
bool C_BaseAnimating::ShouldResetSequenceOnNewModel( void )
{
	return (GetEngineObject()->GetReceivedSequence() == false);
}

