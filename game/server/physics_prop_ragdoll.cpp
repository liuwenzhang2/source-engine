//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "baseanimating.h"
#include "studio.h"
//#include "physics.h"
#include "physics_saverestore.h"
#include "ai_basenpc.h"
#include "vphysics/constraints.h"
#include "datacache/imdlcache.h"
#include "bone_setup.h"
#include "physics_prop_ragdoll.h"
#include "KeyValues.h"
#include "props.h"
#include "RagdollBoogie.h"
#include "AI_Criteria.h"
//#include "ragdoll_shared.h"
#include "hierarchy.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
const char *GetMassEquivalent(float flMass);

#define RAGDOLL_VISUALIZE 0

//-----------------------------------------------------------------------------
// ThinkContext
//-----------------------------------------------------------------------------
const char *s_pFadeOutContext = "RagdollFadeOutContext";
const char *s_pDebrisContext = "DebrisContext";

const float ATTACHED_DAMPING_SCALE = 50.0f;



//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( physics_prop_ragdoll, CRagdollProp );
LINK_ENTITY_TO_CLASS( prop_ragdoll, CRagdollProp );
EXTERN_SEND_TABLE(DT_Ragdoll)

IMPLEMENT_SERVERCLASS_ST(CRagdollProp, DT_Ragdoll)

	//SendPropEHandle(SENDINFO( m_hUnragdoll ) ),
	SendPropFloat(SENDINFO(m_flBlendWeight), 8, SPROP_ROUNDDOWN, 0.0f, 1.0f ),
END_SEND_TABLE()



BEGIN_DATADESC(CRagdollProp)
//					m_ragdoll (custom handling)


	DEFINE_FIELD( m_hDamageEntity, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hKiller, FIELD_EHANDLE ),

	DEFINE_KEYFIELD( m_bStartDisabled, FIELD_BOOLEAN, "StartDisabled" ),

	DEFINE_INPUTFUNC( FIELD_VOID, "StartRagdollBoogie", InputStartRadgollBoogie ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableMotion", InputEnableMotion ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableMotion", InputDisableMotion ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable",		InputTurnOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable",	InputTurnOff ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "FadeAndRemove", InputFadeAndRemove ),

	//DEFINE_FIELD( m_hUnragdoll, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bFirstCollisionAfterLaunch, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_flBlendWeight, FIELD_FLOAT ),
	//DEFINE_FIELD( m_nOverlaySequence, FIELD_INTEGER ),

	// Physics Influence
	DEFINE_FIELD( m_hPhysicsAttacker, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flLastPhysicsInfluenceTime, FIELD_TIME ),
	DEFINE_FIELD( m_flFadeOutStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_flFadeTime,	FIELD_FLOAT),
	DEFINE_FIELD( m_strSourceClassName, FIELD_STRING ),
	DEFINE_FIELD( m_bHasBeenPhysgunned, FIELD_BOOLEAN ),

	// think functions
	DEFINE_THINKFUNC( SetDebrisThink ),
	DEFINE_THINKFUNC( ClearFlagsThink ),
	DEFINE_THINKFUNC( FadeOutThink ),

	DEFINE_FIELD( m_flDefaultFadeScale, FIELD_FLOAT ),

	//DEFINE_RAGDOLL_ELEMENT( 0 ),

END_DATADESC()

//-----------------------------------------------------------------------------
// Disable auto fading under dx7 or when level fades are specified
//-----------------------------------------------------------------------------
void CRagdollProp::DisableAutoFade()
{
	m_flFadeScale = 0;
	m_flDefaultFadeScale = 0;
}

	
void CRagdollProp::Spawn( void )
{
	// Starts out as the default fade scale value
	m_flDefaultFadeScale = m_flFadeScale;

	// NOTE: If this fires, then the assert or the datadesc is wrong!  (see DEFINE_RAGDOLL_ELEMENT above)
	Assert( RAGDOLL_MAX_ELEMENTS == 24 );
	Precache();
	SetModel( STRING(GetEngineObject()->GetModelName() ) );

	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if ( pStudioHdr->flags() & STUDIOHDR_FLAGS_NO_FORCED_FADE )
	{
		DisableAutoFade();
	}
	else
	{
		m_flFadeScale = m_flDefaultFadeScale;
	}

	matrix3x4_t pBoneToWorld[MAXSTUDIOBONES];
	GetEngineObject()->SetupBones( pBoneToWorld, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, gpGlobals->curtime); // FIXME: shouldn't this be a subset of the bones
	// this is useless info after the initial conditions are set
	GetEngineObject()->SetAbsAngles( vec3_angle );
	int collisionGroup = (GetEngineObject()->GetSpawnFlags() & SF_RAGDOLLPROP_DEBRIS) ? COLLISION_GROUP_DEBRIS : COLLISION_GROUP_NONE;
	bool bWake = (GetEngineObject()->GetSpawnFlags() & SF_RAGDOLLPROP_STARTASLEEP) ? false : true;
	GetEngineObject()->InitRagdoll( vec3_origin, 0, vec3_origin, pBoneToWorld, pBoneToWorld, 0, collisionGroup, true, bWake );
	// Make sure it's interactive debris for at most 5 seconds
	if (collisionGroup == COLLISION_GROUP_INTERACTIVE_DEBRIS)
	{
		SetContextThink(&CRagdollProp::SetDebrisThink, gpGlobals->curtime + 5, s_pDebrisContext);
	}
	m_flBlendWeight = 0.0f;
	GetEngineObject()->SetOverlaySequence(- 1);

	// Unless specified, do not allow this to be dissolved
	if (GetEngineObject()->HasSpawnFlags( SF_RAGDOLLPROP_ALLOW_DISSOLVE ) == false )
	{
		GetEngineObject()->AddEFlags( EFL_NO_DISSOLVE );
	}

	if (GetEngineObject()->HasSpawnFlags(SF_RAGDOLLPROP_MOTIONDISABLED) )
	{
		DisableMotion();
	}

	if( m_bStartDisabled )
	{
		GetEngineObject()->AddEffects( EF_NODRAW );
	}
}

void CRagdollProp::SetSourceClassName( const char *pClassname )
{
	m_strSourceClassName = MAKE_STRING( pClassname );
}


void CRagdollProp::OnSave( IEntitySaveUtils *pUtils )
{
	BaseClass::OnSave( pUtils );
}

void CRagdollProp::OnRestore()
{
	BaseClass::OnRestore();
	VPhysicsUpdate(GetEngineObject()->VPhysicsGetObject() );
}



void CRagdollProp::UpdateOnRemove( void )
{

	// Chain to base after doing our own cleanup to mimic
	//  destructor unwind order
	BaseClass::UpdateOnRemove();
	//GetEngineObject()->ClearRagdoll();
}

CRagdollProp::CRagdollProp( void )
{
	m_strSourceClassName = NULL_STRING;
	Assert( (1<<RAGDOLL_INDEX_BITS) >=RAGDOLL_MAX_ELEMENTS );
	m_flFadeScale = 1;
	m_flDefaultFadeScale = 1;
}

CRagdollProp::~CRagdollProp( void )
{
	int aaa = 0;
}

void CRagdollProp::Precache( void )
{
	engine->PrecacheModel( STRING(GetEngineObject()->GetModelName() ) );
	BaseClass::Precache();
}

int CRagdollProp::ObjectCaps()
{
	return BaseClass::ObjectCaps() | FCAP_WCEDIT_POSITION;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CRagdollProp::InitRagdollAnimation()
{
	GetEngineObject()->SetAnimTime(gpGlobals->curtime);
	GetEngineObject()->SetPlaybackRate(0.0);
	GetEngineObject()->SetCycle( 0 );
	
	// put into ACT_DIERAGDOLL if it exists, otherwise use sequence 0
	int nSequence = GetEngineObject()->SelectWeightedSequence( ACT_DIERAGDOLL );
	if ( nSequence < 0 )
	{
		GetEngineObject()->ResetSequence( 0 );
	}
	else
	{
		GetEngineObject()->ResetSequence( nSequence );
	}
}


//-----------------------------------------------------------------------------
// Response system stuff
//-----------------------------------------------------------------------------
IResponseSystem *CRagdollProp::GetResponseSystem()
{
	extern IResponseSystem *g_pResponseSystem;

	// Just use the general NPC response system; we often come from NPCs after all
	return g_pResponseSystem;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CRagdollProp::ModifyOrAppendCriteria( AI_CriteriaSet& set )
{
	BaseClass::ModifyOrAppendCriteria( set );

	if ( m_strSourceClassName != NULL_STRING )
	{
		set.RemoveCriteria( "classname" );
		set.AppendCriteria( "classname", STRING(m_strSourceClassName) );
		set.AppendCriteria( "ragdoll", "1" );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CRagdollProp::OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason )
{
	m_hPhysicsAttacker = pPhysGunUser;
	m_flLastPhysicsInfluenceTime = gpGlobals->curtime;

	// Clear out the classname if we've been physgunned before
	// so that the screams, etc. don't happen. Simulate that the first
	// major punt or throw has been enough to kill him.
	if ( m_bHasBeenPhysgunned )
	{
		m_strSourceClassName = NULL_STRING;
	}
	m_bHasBeenPhysgunned = true;

	if( HasPhysgunInteraction( "onpickup", "boogie" ) )
	{
		if ( reason == PUNTED_BY_CANNON )
		{
			CRagdollBoogie::Create( this, 150, gpGlobals->curtime, 3.0f, SF_RAGDOLL_BOOGIE_ELECTRICAL );
		}
		else
		{
			CRagdollBoogie::Create( this, 150, gpGlobals->curtime, 2.0f, 0.0f );
		}
	}

	if (GetEngineObject()->HasSpawnFlags( SF_RAGDOLLPROP_USE_LRU_RETIREMENT ) )
	{
		EntityList()->MoveToTopOfLRU( this );
	}

	if ( !GetEngineObject()->HasSpawnFlags( SF_PHYSPROP_ENABLE_ON_PHYSCANNON ) )
		return;

	ragdoll_t *pRagdollPhys = GetEngineObject()->GetRagdoll( );
	for ( int j = 0; j < pRagdollPhys->listCount; ++j )
	{
		pRagdollPhys->list[j].pObject->Wake();
		pRagdollPhys->list[j].pObject->EnableMotion( true );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CRagdollProp::OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t Reason )
{
	BaseClass::OnPhysGunDrop( pPhysGunUser, Reason );
	m_hPhysicsAttacker = pPhysGunUser;
	m_flLastPhysicsInfluenceTime = gpGlobals->curtime;

	if( HasPhysgunInteraction( "onpickup", "boogie" ) )
	{
		CRagdollBoogie::Create( this, 150, gpGlobals->curtime, 3.0f, SF_RAGDOLL_BOOGIE_ELECTRICAL );
	}

	if (GetEngineObject()->HasSpawnFlags( SF_RAGDOLLPROP_USE_LRU_RETIREMENT ) )
	{
		EntityList()->MoveToTopOfLRU( this );
	}

	// Make sure it's interactive debris for at most 5 seconds
	if (GetEngineObject()->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE_DEBRIS )
	{
		SetContextThink( &CRagdollProp::SetDebrisThink, gpGlobals->curtime + 5, s_pDebrisContext );
	}

	if ( Reason != LAUNCHED_BY_CANNON )
		return;

	if( HasPhysgunInteraction( "onlaunch", "spin_zaxis" ) )
	{
		Vector vecAverageCenter( 0, 0, 0 );

		// Get the average position, apply forces to produce a spin
		int j;
		ragdoll_t *pRagdollPhys = GetEngineObject()->GetRagdoll( );
		for ( j = 0; j < pRagdollPhys->listCount; ++j )
		{
			Vector vecCenter;
			pRagdollPhys->list[j].pObject->GetPosition( &vecCenter, NULL );
			vecAverageCenter += vecCenter;
		}

		vecAverageCenter /= pRagdollPhys->listCount;

		Vector vecZAxis( 0, 0, 1 );
		for ( j = 0; j < pRagdollPhys->listCount; ++j )
		{
			Vector vecDelta;
			pRagdollPhys->list[j].pObject->GetPosition( &vecDelta, NULL );
			vecDelta -= vecAverageCenter;

			Vector vecDir;
			CrossProduct( vecZAxis, vecDelta, vecDir );
			vecDir *= 100;
			pRagdollPhys->list[j].pObject->AddVelocity( &vecDir, NULL );
		}
	}

	PhysSetGameFlags(GetEngineObject()->VPhysicsGetObject(), FVPHYSICS_WAS_THROWN );
	m_bFirstCollisionAfterLaunch = true;
}


//-----------------------------------------------------------------------------
// Physics attacker
//-----------------------------------------------------------------------------
CBasePlayer *CRagdollProp::HasPhysicsAttacker( float dt )
{
	if (gpGlobals->curtime - dt <= m_flLastPhysicsInfluenceTime)
	{
		return m_hPhysicsAttacker;
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CRagdollProp::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	BaseClass::VPhysicsCollision( index, pEvent );

	CBaseEntity *pHitEntity = (CBaseEntity*)pEvent->pEntities[!index];
	if ( pHitEntity == this )
		return;

	// Don't take physics damage from whoever's holding him with the physcannon.
	if (GetEngineObject()->VPhysicsGetObject() && (GetEngineObject()->VPhysicsGetObject()->GetGameFlags() & FVPHYSICS_PLAYER_HELD) )
	{
		if ( pHitEntity && (pHitEntity == HasPhysicsAttacker( FLT_MAX )) )
			return;
	}

	// Don't bother taking damage from the physics attacker
	if ( pHitEntity && HasPhysicsAttacker( 0.5f ) == pHitEntity )
		return;

	if( m_bFirstCollisionAfterLaunch )
	{
		HandleFirstCollisionInteractions( index, pEvent );
	}
	
	if ( m_takedamage != DAMAGE_NO )
	{
		int damageType = 0;
		float damage = CalculateDefaultPhysicsDamage( index, pEvent, 1.0f, true, damageType );
		if ( damage > 0 )
		{
			// Take extra damage after we're punted by the physcannon
			if ( m_bFirstCollisionAfterLaunch )
			{
				damage *= 10;
			}

			IServerEntity *pHitEntity = pEvent->pEntities[!index];
			if ( !pHitEntity )
			{
				// hit world
				pHitEntity = EntityList()->GetBaseEntity( 0 );
			}
			Vector damagePos;
			pEvent->pInternalData->GetContactPoint( damagePos );
			Vector damageForce = pEvent->postVelocity[index] * pEvent->pObjects[index]->GetMass();
			if ( damageForce == vec3_origin )
			{
				// This can happen if this entity is motion disabled, and can't move.
				// Use the velocity of the entity that hit us instead.
				damageForce = pEvent->postVelocity[!index] * pEvent->pObjects[!index]->GetMass();
			}

			// FIXME: this doesn't pass in who is responsible if some other entity "caused" this collision
			EntityList()->PhysCallbackDamage( this, CTakeDamageInfo( pHitEntity, pHitEntity, damageForce, damagePos, damage, damageType ), *pEvent, index );
		}
	}

	if ( m_bFirstCollisionAfterLaunch )
	{
		// Setup the think function to remove the flags
		SetThink( &CRagdollProp::ClearFlagsThink );
		GetEngineObject()->SetNextThink( gpGlobals->curtime );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CRagdollProp::HasPhysgunInteraction( const char *pszKeyName, const char *pszValue )
{
	KeyValues *modelKeyValues = new KeyValues("");
	if ( modelKeyValues->LoadFromBuffer( modelinfo->GetModelName(GetEngineObject()->GetModel() ), modelinfo->GetModelKeyValueText(GetEngineObject()->GetModel() ) ) )
	{
		KeyValues *pkvPropData = modelKeyValues->FindKey("physgun_interactions");
		if ( pkvPropData )
		{
			char const *pszBase = pkvPropData->GetString( pszKeyName );

			if ( pszBase && pszBase[0] && !stricmp( pszBase, pszValue ) )
			{
				modelKeyValues->deleteThis();
				return true;
			}
		}
	}

	modelKeyValues->deleteThis();
	return false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CRagdollProp::HandleFirstCollisionInteractions( int index, gamevcollisionevent_t *pEvent )
{
	IPhysicsObject *pObj = GetEngineObject()->VPhysicsGetObject();
	if ( !pObj)
		return;

	if( HasPhysgunInteraction( "onfirstimpact", "break" ) )
	{
		// Looks like it's best to break by having the object damage itself. 
		CTakeDamageInfo info;

		info.SetDamage( m_iHealth );
		info.SetAttacker( this );
		info.SetInflictor( this );
		info.SetDamageType( DMG_GENERIC );

		Vector vecPosition;
		Vector vecVelocity;

		GetEngineObject()->VPhysicsGetObject()->GetVelocity( &vecVelocity, NULL );
		GetEngineObject()->VPhysicsGetObject()->GetPosition( &vecPosition, NULL );

		info.SetDamageForce( vecVelocity );
		info.SetDamagePosition( vecPosition );

		TakeDamage( info );
		return;
	}

	if( HasPhysgunInteraction( "onfirstimpact", "paintsplat" ) )
	{
		IPhysicsObject *pObj = GetEngineObject()->VPhysicsGetObject();
 
		Vector vecPos;
		pObj->GetPosition( &vecPos, NULL );
 
		trace_t tr;
		UTIL_TraceLine( vecPos, vecPos + pEvent->preVelocity[0] * 1.5, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

		switch( random->RandomInt( 1, 3 ) )
		{
		case 1:
			UTIL_DecalTrace( &tr, "PaintSplatBlue" );
			break;

		case 2:
			UTIL_DecalTrace( &tr, "PaintSplatGreen" );
			break;

		case 3:
			UTIL_DecalTrace( &tr, "PaintSplatPink" );
			break;
		}
	}

	bool bAlienBloodSplat = HasPhysgunInteraction( "onfirstimpact", "alienbloodsplat" );
	if( bAlienBloodSplat || HasPhysgunInteraction( "onfirstimpact", "bloodsplat" ) )
	{
		IPhysicsObject *pObj = GetEngineObject()->VPhysicsGetObject();
 
		Vector vecPos;
		pObj->GetPosition( &vecPos, NULL );
 
		trace_t tr;
		UTIL_TraceLine( vecPos, vecPos + pEvent->preVelocity[0] * 1.5, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

		UTIL_BloodDecalTrace( &tr, bAlienBloodSplat ? BLOOD_COLOR_GREEN : BLOOD_COLOR_RED );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CRagdollProp::ClearFlagsThink( void )
{
	PhysClearGameFlags(GetEngineObject()->VPhysicsGetObject(), FVPHYSICS_WAS_THROWN );
	m_bFirstCollisionAfterLaunch = false;
	SetThink( NULL );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
AngularImpulse CRagdollProp::PhysGunLaunchAngularImpulse()
{
	if( HasPhysgunInteraction( "onlaunch", "spin_zaxis" ) )
	{
		// Don't add in random angular impulse if this object is supposed to spin in a specific way.
		AngularImpulse ang( 0, 0, 0 );
		return ang;
	}

	return BaseClass::PhysGunLaunchAngularImpulse();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : activity - 
//-----------------------------------------------------------------------------
void CRagdollProp::SetOverlaySequence( Activity activity )
{
	int seq = GetEngineObject()->SelectWeightedSequence( activity );
	if ( seq < 0 )
	{
		GetEngineObject()->SetOverlaySequence(- 1);
	}
	else
	{
		GetEngineObject()->SetOverlaySequence(seq);
	}
}



void CRagdollProp::SetDebrisThink()
{
	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
	GetEngineObject()->RecheckCollisionFilter();
}

void CRagdollProp::SetDamageEntity( CBaseEntity *pEntity )
{
	// Damage passing
	m_hDamageEntity = pEntity;

	// Set our takedamage to match it
	if ( pEntity )
	{
		m_takedamage = pEntity->m_takedamage;
	}
	else
	{
		m_takedamage = DAMAGE_EVENTS_ONLY;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int	CRagdollProp::OnTakeDamage( const CTakeDamageInfo &info )
{
	// If we have a damage entity, we want to pass damage to it. Add the
	// Never Ragdoll flag, on the assumption that if the entity dies, we'll
	// actually be taking the role of its ragdoll.
	if ( m_hDamageEntity.Get() )
	{
		CTakeDamageInfo subInfo = info;
		subInfo.AddDamageType( DMG_REMOVENORAGDOLL );
		return m_hDamageEntity->OnTakeDamage( subInfo );
	}

	return BaseClass::OnTakeDamage( info );
}




void CRagdollProp::TraceAttack( const CTakeDamageInfo &info, const Vector &dir, trace_t *ptr, CDmgAccumulator *pAccumulator )
{
	if ( ptr->physicsbone >= 0 && ptr->physicsbone < GetEngineObject()->RagdollBoneCount() )
	{
		GetEngineObject()->VPhysicsSwapObject(GetEngineObject()->GetElement(ptr->physicsbone));
	}
	BaseClass::TraceAttack( info, dir, ptr, pAccumulator );
}

bool CRagdollProp::TestCollision( const Ray_t &ray, unsigned int mask, trace_t& trace )
{
#if 0
	// PERFORMANCE: Use hitboxes for rays instead of vcollides if this is a performance problem
	if ( ray.m_IsRay )
	{
		return BaseClass::TestCollision( ray, mask, trace );
	}
#endif

	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr( );
	if (!pStudioHdr)
		return false;

	// Just iterate all of the elements and trace the box against each one.
	// NOTE: This is pretty expensive for small/dense characters
	trace_t tr;
	for ( int i = 0; i < GetEngineObject()->RagdollBoneCount(); i++ )
	{
		Vector position;
		QAngle angles;

		if(GetEngineObject()->GetElement(i) )
		{
			GetEngineObject()->GetElement(i)->GetPosition( &position, &angles );
			EntityList()->PhysGetCollision()->TraceBox( ray, GetEngineObject()->GetElement(i)->GetCollide(), position, angles, &tr );

			if ( tr.fraction < trace.fraction )
			{
				tr.physicsbone = i;
				tr.surface.surfaceProps = GetEngineObject()->GetElement(i)->GetMaterialIndex();
				trace = tr;
			}
		}
		else
		{
			DevWarning("Bogus object in Ragdoll Prop's ragdoll list!\n");
		}
	}

	if ( trace.fraction >= 1 )
	{
		return false;
	}

	return true;
}


void CRagdollProp::Teleport( const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity )
{
	// newAngles is a relative transform for the entity
	// But a ragdoll entity has identity orientation by design
	// so we compute a relative transform here based on the previous transform
	matrix3x4_t startMatrixInv;
	MatrixInvert(GetEngineObject()->EntityToWorldTransform(), startMatrixInv );
	matrix3x4_t endMatrix;
	MatrixCopy(GetEngineObject()->EntityToWorldTransform(), endMatrix );
	if ( newAngles )
	{
		AngleMatrix( *newAngles, endMatrix );
	}
	if ( newPosition )
	{
		PositionMatrix( *newPosition, endMatrix );
	}
	// now endMatrix is the refernce matrix for the entity at the target position
	matrix3x4_t xform;
	ConcatTransforms( endMatrix, startMatrixInv, xform );
	// now xform is the relative transform the entity must undergo

	// we need to call the base class and it will teleport our vphysics object, 
	// so set object 0 up and compute the origin/angles for its new position (base implementation has side effects)
	GetEngineObject()->VPhysicsSwapObject(GetEngineObject()->GetElement(0));
	matrix3x4_t obj0source, obj0Target;
	GetEngineObject()->GetElement(0)->GetPositionMatrix( &obj0source );
	ConcatTransforms( xform, obj0source, obj0Target );
	Vector obj0Pos;
	QAngle obj0Angles;
	MatrixAngles( obj0Target, obj0Angles, obj0Pos );
	BaseClass::Teleport( &obj0Pos, &obj0Angles, newVelocity );
	
	for ( int i = 1; i < GetEngineObject()->RagdollBoneCount(); i++ )
	{
		matrix3x4_t matrix, newMatrix;
		GetEngineObject()->GetElement(i)->GetPositionMatrix( &matrix );
		ConcatTransforms( xform, matrix, newMatrix );
		GetEngineObject()->GetElement(i)->SetPositionMatrix( newMatrix, true );
		GetEngineObject()->UpdateNetworkDataFromVPhysics(i );
	}
	// fixup/relink object 0
	GetEngineObject()->UpdateNetworkDataFromVPhysics(0 );
}

void CRagdollProp::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	//GetEngineObject()->VPhysicsUpdate(pPhysics);

	// Don't scream after you've come to rest
	if (GetEngineObject()->GetAllAsleep() )
	{
		m_strSourceClassName = NULL_STRING;
	}
	
	// Interactive debris converts back to debris when it comes to rest
	if (GetEngineObject()->GetAllAsleep() && GetEngineObject()->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE_DEBRIS)
	{
		GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
		GetEngineObject()->RecheckCollisionFilter();
		SetContextThink( NULL, gpGlobals->curtime, s_pDebrisContext );
	}
}

//-----------------------------------------------------------------------------
// Fade out due to the LRU telling it do
//-----------------------------------------------------------------------------
#define FADE_OUT_LENGTH 0.5f

void CRagdollProp::FadeOut( float flDelay, float fadeTime ) 
{
	if ( IsFading() )
		return;

	m_flFadeTime = ( fadeTime == -1 ) ? FADE_OUT_LENGTH : fadeTime;

	m_flFadeOutStartTime = gpGlobals->curtime + flDelay;
	m_flFadeScale = 0;
	SetContextThink( &CRagdollProp::FadeOutThink, gpGlobals->curtime + flDelay + 0.01f, s_pFadeOutContext );
}

bool CRagdollProp::IsFading()
{
	return (GetEngineObject()->GetNextThink( s_pFadeOutContext ) >= gpGlobals->curtime );
}

void CRagdollProp::FadeOutThink(void) 
{
	float dt = gpGlobals->curtime - m_flFadeOutStartTime;
	if ( dt < 0 )
	{
		SetContextThink( &CRagdollProp::FadeOutThink, gpGlobals->curtime + 0.1, s_pFadeOutContext );
	}
	else if ( dt < m_flFadeTime )
	{
		float alpha = 1.0f - dt / m_flFadeTime;
		int nFade = (int)(alpha * 255.0f);
		GetEngineObject()->SetRenderMode(kRenderTransTexture);
		GetEngineObject()->SetRenderColorA( nFade );
		NetworkStateChanged();
		SetContextThink( &CRagdollProp::FadeOutThink, gpGlobals->curtime + TICK_INTERVAL, s_pFadeOutContext );
	}
	else
	{
		// Necessary to cause it to do the appropriate death cleanup
		// Yeah, the player may have nothing to do with it, but
		// passing NULL to TakeDamage causes bad things to happen
		CBasePlayer *pPlayer = ToBasePlayer(EntityList()->GetLocalPlayer());
		CTakeDamageInfo info( pPlayer, pPlayer, 10000.0, DMG_GENERIC );
		TakeDamage( info );
		EntityList()->DestroyEntity( this );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Draw any debug text overlays
// Output : Current text offset from the top
//-----------------------------------------------------------------------------
int CRagdollProp::DrawDebugTextOverlays(void) 
{
	int text_offset = BaseClass::DrawDebugTextOverlays();

	if (m_debugOverlays & OVERLAY_TEXT_BIT) 
	{
		if (GetEngineObject()->RagdollBoneCount())
		{
			float mass = 0;
			for ( int i = 0; i < GetEngineObject()->RagdollBoneCount(); i++ )
			{
				if (GetEngineObject()->GetElement(i))
				{
					mass += GetEngineObject()->GetElement(i)->GetMass();
				}
			}

			char tempstr[512];
			Q_snprintf(tempstr, sizeof(tempstr),"Mass: %.2f kg / %.2f lb (%s)", mass, kg2lbs(mass), GetMassEquivalent(mass) );
			EntityText( text_offset, tempstr, 0);
			text_offset++;
		}
	}

	return text_offset;
}

void CRagdollProp::DrawDebugGeometryOverlays() 
{
	if (m_debugOverlays & OVERLAY_BBOX_BIT) 
	{
		GetEngineObject()->DrawServerHitboxes();
	}
	if (m_debugOverlays & OVERLAY_PIVOT_BIT)
	{
		for ( int i = 0; i < GetEngineObject()->RagdollBoneCount(); i++ )
		{
			if (GetEngineObject()->GetElement(i))
			{
				float mass = GetEngineObject()->GetElement(i)->GetMass();
				Vector pos;
				GetEngineObject()->GetElement(i)->GetPosition( &pos, NULL );
				CFmtStr str("mass %.1f", mass );
				NDebugOverlay::EntityTextAtPosition( pos, 0, str.Access(), 0, 0, 255, 0, 255 );
			}
		}
	} 
	BaseClass::DrawDebugGeometryOverlays();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pOther - 
//-----------------------------------------------------------------------------
//void CRagdollProp::SetUnragdoll( CBaseAnimating *pOther )
//{
//	m_hUnragdoll = pOther;
//}

//===============================================================================================================
// RagdollPropAttached
//===============================================================================================================
class CRagdollPropAttached : public CRagdollProp
{
	DECLARE_CLASS( CRagdollPropAttached, CRagdollProp );
public:

	CRagdollPropAttached()
	{
		m_bShouldDetach = false;
	}

	~CRagdollPropAttached()
	{
		EntityList()->PhysGetEnv()->DestroyConstraint( m_pAttachConstraint );
		m_pAttachConstraint = NULL;
	}

	void InitRagdollAttached( IPhysicsObject *pAttached, const Vector &forceVector, int forceBone, matrix3x4_t *pPrevBones, matrix3x4_t *pBoneToWorld, float dt, int collisionGroup, CBaseAnimating *pFollow, int boneIndexRoot, const Vector &boneLocalOrigin, int parentBoneAttach, const Vector &worldAttachOrigin );
	void DetachOnNextUpdate();
	void VPhysicsUpdate( IPhysicsObject *pPhysics );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

private:
	void Detach();
	CNetworkVar( int, m_boneIndexAttached );
	CNetworkVar( int, m_ragdollAttachedObjectIndex );
	CNetworkVector( m_attachmentPointBoneSpace );
	CNetworkVector( m_attachmentPointRagdollSpace );
	bool		m_bShouldDetach;
	IPhysicsConstraint	*m_pAttachConstraint;
};

LINK_ENTITY_TO_CLASS( prop_ragdoll_attached, CRagdollPropAttached );
EXTERN_SEND_TABLE(DT_Ragdoll_Attached)

IMPLEMENT_SERVERCLASS_ST(CRagdollPropAttached, DT_Ragdoll_Attached)
	SendPropInt( SENDINFO( m_boneIndexAttached ), MAXSTUDIOBONEBITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_ragdollAttachedObjectIndex ), RAGDOLL_INDEX_BITS, SPROP_UNSIGNED ),
	SendPropVector(SENDINFO(m_attachmentPointBoneSpace), -1,  SPROP_COORD ),
	SendPropVector(SENDINFO(m_attachmentPointRagdollSpace), -1,  SPROP_COORD ),
END_SEND_TABLE()

BEGIN_DATADESC(CRagdollPropAttached)
	DEFINE_FIELD( m_boneIndexAttached,	FIELD_INTEGER ),
	DEFINE_FIELD( m_ragdollAttachedObjectIndex, FIELD_INTEGER ),
	DEFINE_FIELD( m_attachmentPointBoneSpace,	FIELD_VECTOR ),
	DEFINE_FIELD( m_attachmentPointRagdollSpace, FIELD_VECTOR ),
	DEFINE_FIELD( m_bShouldDetach, FIELD_BOOLEAN ),
	DEFINE_PHYSPTR( m_pAttachConstraint ),
END_DATADESC()




	
CBaseAnimating *CreateServerRagdollSubmodel( CBaseAnimating *pOwner, const char *pModelName, const Vector &position, const QAngle &angles, int collisionGroup )
{
	CRagdollProp *pRagdoll = (CRagdollProp *)CBaseEntity::CreateNoSpawn( "prop_ragdoll", position, angles, pOwner );
	pRagdoll->GetEngineObject()->SetModelName( AllocPooledString( pModelName ) );
	pRagdoll->SetModel( STRING(pRagdoll->GetEngineObject()->GetModelName()) );
	matrix3x4_t pBoneToWorld[MAXSTUDIOBONES], pBoneToWorldNext[MAXSTUDIOBONES];
	pRagdoll->GetEngineObject()->ResetSequence( 0 );

	// let bone merging do the work of copying everything over for us
	pRagdoll->GetEngineObject()->SetParent( pOwner?pOwner->GetEngineObject():NULL );
	pRagdoll->GetEngineObject()->SetupBones( pBoneToWorld, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, gpGlobals->curtime);
	// HACKHACK: don't want this parent anymore
	pRagdoll->GetEngineObject()->SetParent( NULL );

	memcpy( pBoneToWorldNext, pBoneToWorld, sizeof(pBoneToWorld) );

	pRagdoll->GetEngineObject()->InitRagdoll( vec3_origin, -1, vec3_origin, pBoneToWorld, pBoneToWorldNext, 0.1, collisionGroup, true );
	// Make sure it's interactive debris for at most 5 seconds
	if (collisionGroup == COLLISION_GROUP_INTERACTIVE_DEBRIS)
	{
		pRagdoll->SetContextThink(&CRagdollProp::SetDebrisThink, gpGlobals->curtime + 5, s_pDebrisContext);
	}
	return pRagdoll;
}




void CRagdollPropAttached::DetachOnNextUpdate()
{
	m_bShouldDetach = true;
}

void CRagdollPropAttached::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	if ( m_bShouldDetach )
	{
		Detach();
		m_bShouldDetach = false;
	}
	BaseClass::VPhysicsUpdate( pPhysics );
}

void CRagdollPropAttached::Detach()
{
	GetEngineObject()->SetParent(NULL);
	SetOwnerEntity( NULL );
	GetEngineObject()->SetAbsAngles( vec3_angle );
	GetEngineObject()->SetMoveType( MOVETYPE_VPHYSICS );
	GetEngineObject()->RemoveSolidFlags( FSOLID_NOT_SOLID );
	EntityList()->PhysGetEnv()->DestroyConstraint( m_pAttachConstraint );
	m_pAttachConstraint = NULL;
	const float dampingScale = 1.0f / ATTACHED_DAMPING_SCALE;
	for ( int i = 0; i < GetEngineObject()->RagdollBoneCount(); i++ )
	{
		float damping, rotdamping;
		GetEngineObject()->GetElement(i)->GetDamping(&damping, &rotdamping);
		damping *= dampingScale;
		rotdamping *= dampingScale;
		GetEngineObject()->GetElement(i)->SetDamping( &damping, &damping );
	}

	// Go non-solid
	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
	GetEngineObject()->RecheckCollisionFilter();
}

void CRagdollPropAttached::InitRagdollAttached( 
	IPhysicsObject *pAttached, 
	const Vector &forceVector, 
	int forceBone, 
	matrix3x4_t *pPrevBones, 
	matrix3x4_t *pBoneToWorld, 
	float dt, 
	int collisionGroup, 
	CBaseAnimating *pFollow, 
	int boneIndexRoot, 
	const Vector &boneLocalOrigin, 
	int parentBoneAttach, 
	const Vector &worldAttachOrigin )
{
	int ragdollAttachedIndex = 0;
	if ( parentBoneAttach > 0 )
	{
		IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
		mstudiobone_t *pBone = pStudioHdr->pBone( parentBoneAttach );
		ragdollAttachedIndex = pBone->physicsbone;
	}

	GetEngineObject()->InitRagdoll( forceVector, forceBone, vec3_origin, pPrevBones, pBoneToWorld, dt, collisionGroup, false );
	// Make sure it's interactive debris for at most 5 seconds
	if (collisionGroup == COLLISION_GROUP_INTERACTIVE_DEBRIS)
	{
		SetContextThink(&CRagdollProp::SetDebrisThink, gpGlobals->curtime + 5, s_pDebrisContext);
	}
	IPhysicsObject *pRefObject = GetEngineObject()->GetElement(ragdollAttachedIndex);

	Vector attachmentPointRagdollSpace;
	pRefObject->WorldToLocal( &attachmentPointRagdollSpace, worldAttachOrigin );

	constraint_ragdollparams_t constraint;
	constraint.Defaults();
	matrix3x4_t tmp, worldToAttached, worldToReference, constraintToWorld;

	Vector offsetWS;
	pAttached->LocalToWorld( &offsetWS, boneLocalOrigin );

	QAngle followAng = QAngle(0, pFollow->GetEngineObject()->GetAbsAngles().y, 0 );
	AngleMatrix( followAng, offsetWS, constraintToWorld );

	constraint.axes[0].SetAxisFriction( -2, 2, 20 );
	constraint.axes[1].SetAxisFriction( 0, 0, 0 );
	constraint.axes[2].SetAxisFriction( -15, 15, 20 );

	// Exaggerate the bone's ability to pull the mass of the ragdoll around
	constraint.constraint.bodyMassScale[1] = 50.0f;

	pAttached->GetPositionMatrix( &tmp );
	MatrixInvert( tmp, worldToAttached );

	pRefObject->GetPositionMatrix( &tmp );
	MatrixInvert( tmp, worldToReference );

	ConcatTransforms( worldToReference, constraintToWorld, constraint.constraintToReference );
	ConcatTransforms( worldToAttached, constraintToWorld, constraint.constraintToAttached );

	// for now, just slam this to be the passed in value
	MatrixSetColumn( attachmentPointRagdollSpace, 3, constraint.constraintToReference );

	PhysDisableEntityCollisions( pAttached, GetEngineObject()->GetElement(0));
	m_pAttachConstraint = EntityList()->PhysGetEnv()->CreateRagdollConstraint( pRefObject, pAttached, GetEngineObject()->GetConstraintGroup(), constraint);

	GetEngineObject()->SetParent( pFollow->GetEngineObject() );
	SetOwnerEntity( pFollow );

	GetEngineObject()->ActiveRagdoll();

	// add a bunch of dampening to the ragdoll
	for ( int i = 0; i < GetEngineObject()->RagdollBoneCount(); i++ )
	{
		float damping, rotdamping;
		GetEngineObject()->GetElement(i)->GetDamping(&damping, &rotdamping);
		damping *= ATTACHED_DAMPING_SCALE;
		rotdamping *= ATTACHED_DAMPING_SCALE;
		GetEngineObject()->GetElement(i)->SetDamping( &damping, &rotdamping );
	}

	m_boneIndexAttached = boneIndexRoot;
	m_ragdollAttachedObjectIndex = ragdollAttachedIndex;
	m_attachmentPointBoneSpace = boneLocalOrigin;
	
	Vector vTemp;
	MatrixGetColumn( constraint.constraintToReference, 3, vTemp );
	m_attachmentPointRagdollSpace = vTemp;
}

CRagdollProp *CreateServerRagdollAttached( CBaseAnimating *pAnimating, const Vector &vecForce, int forceBone, int collisionGroup, IPhysicsObject *pAttached, CBaseAnimating *pParentEntity, int boneAttach, const Vector &originAttached, int parentBoneAttach, const Vector &boneOrigin )
{
	// Return immediately if the model doesn't have a vcollide
	if ( modelinfo->GetVCollide( pAnimating->GetEngineObject()->GetModelIndex() ) == NULL )
		return NULL;

	CRagdollPropAttached *pRagdoll = (CRagdollPropAttached *)CBaseEntity::CreateNoSpawn( "prop_ragdoll_attached", pAnimating->GetEngineObject()->GetAbsOrigin(), vec3_angle, NULL );
	pRagdoll->CopyAnimationDataFrom( pAnimating );

	pRagdoll->InitRagdollAnimation();
	matrix3x4_t pBoneToWorld[MAXSTUDIOBONES];
	pAnimating->GetEngineObject()->SetupBones( pBoneToWorld, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, gpGlobals->curtime);
	pRagdoll->InitRagdollAttached( pAttached, vecForce, forceBone, pBoneToWorld, pBoneToWorld, 0.1, collisionGroup, pParentEntity, boneAttach, boneOrigin, parentBoneAttach, originAttached );
	
	return pRagdoll;
}

void DetachAttachedRagdoll( CBaseEntity *pRagdollIn )
{
	CRagdollPropAttached *pRagdoll = dynamic_cast<CRagdollPropAttached *>(pRagdollIn);

	if ( pRagdoll )
	{
		pRagdoll->DetachOnNextUpdate();
	}
}

void DetachAttachedRagdollsForEntity( CBaseEntity *pRagdollParent )
{
	CUtlVector<IEngineObjectServer *> list;
	pRagdollParent->GetEngineObject()->GetAllChildren( list );
	for ( int i = list.Count()-1; i >= 0; --i )
	{
		DetachAttachedRagdoll((CBaseEntity*)list[i]->GetOuter() );
	}
}

bool Ragdoll_IsPropRagdoll( IServerEntity *pEntity )
{
	if ( dynamic_cast<CRagdollProp *>(pEntity) != NULL )
		return true;
	return false;
}





void CRagdollProp::DisableMotion( void )
{
	for ( int iRagdoll = 0; iRagdoll < GetEngineObject()->RagdollBoneCount(); ++iRagdoll )
	{
		IPhysicsObject *pPhysicsObject = GetEngineObject()->GetElement(iRagdoll);
		if ( pPhysicsObject != NULL )
		{
			pPhysicsObject->EnableMotion( false );
		}
	}
}

void CRagdollProp::InputStartRadgollBoogie( inputdata_t &inputdata )
{
	float duration = inputdata.value.Float();

	if( duration <= 0.0f )
	{
		duration = 5.0f;
	}

	CRagdollBoogie::Create( this, 100, gpGlobals->curtime, duration, 0 );
}

//-----------------------------------------------------------------------------
// Purpose: Enable physics motion and collision response (on by default)
//-----------------------------------------------------------------------------
void CRagdollProp::InputEnableMotion( inputdata_t &inputdata )
{
	for ( int iRagdoll = 0; iRagdoll < GetEngineObject()->RagdollBoneCount(); ++iRagdoll )
	{
		IPhysicsObject *pPhysicsObject = GetEngineObject()->GetElement(iRagdoll);
		if ( pPhysicsObject != NULL )
		{
			pPhysicsObject->EnableMotion( true );
			pPhysicsObject->Wake();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Disable any physics motion or collision response
//-----------------------------------------------------------------------------
void CRagdollProp::InputDisableMotion( inputdata_t &inputdata )
{
	DisableMotion();
}

void CRagdollProp::InputTurnOn( inputdata_t &inputdata )
{
	GetEngineObject()->RemoveEffects( EF_NODRAW );
}

void CRagdollProp::InputTurnOff( inputdata_t &inputdata )
{
	GetEngineObject()->AddEffects( EF_NODRAW );
}

void CRagdollProp::InputFadeAndRemove( inputdata_t &inputdata )
{
	float flFadeDuration = inputdata.value.Float();
	
	if( flFadeDuration == 0.0f )
		flFadeDuration = 1.0f;

	FadeOut( 0.0f, flFadeDuration );
}

void Ragdoll_GetAngleOverrideString( char *pOut, int size, IServerEntity *pEntity )
{
	CRagdollProp *pRagdoll = dynamic_cast<CRagdollProp *>(pEntity);
	if ( pRagdoll )
	{
		pRagdoll->GetEngineObject()->GetAngleOverrideFromCurrentState( pOut, size );
	}
}
