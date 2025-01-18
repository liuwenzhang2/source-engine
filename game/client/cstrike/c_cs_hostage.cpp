//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side C_CHostage class
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_cs_hostage.h"
#include <bitbuf.h>
//#include "ragdoll_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#undef CHostage

//-----------------------------------------------------------------------------

static float HOSTAGE_HEAD_TURN_RATE = 130;


CUtlVector< C_CHostage* > g_Hostages;
CUtlVector< EHANDLE > g_HostageRagdolls;

extern ConVar g_ragdoll_fadespeed;

//-----------------------------------------------------------------------------
const int NumInterestingPoseParameters = 6;
static const char* InterestingPoseParameters[NumInterestingPoseParameters] =
{
	"body_yaw",
	"spine_yaw",
	"neck_trans",
	"head_pitch",
	"head_yaw",
	"head_roll"
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class C_LowViolenceHostageDeathModel : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_LowViolenceHostageDeathModel, C_BaseAnimating );
	
	C_LowViolenceHostageDeathModel();
	~C_LowViolenceHostageDeathModel();

	bool SetupLowViolenceModel( C_CHostage *pHostage );

	// fading out
	void ClientThink( void );

private:

	void Interp_Copy( VarMapping_t *pDest, CBaseEntity *pSourceEntity, VarMapping_t *pSrc );

	float m_flFadeOutStart;
};

static CEntityFactory<C_LowViolenceHostageDeathModel> g_C_LowViolenceHostageDeathModel_Factory("","C_LowViolenceHostageDeathModel");

//-----------------------------------------------------------------------------
C_LowViolenceHostageDeathModel::C_LowViolenceHostageDeathModel()
{
}

//-----------------------------------------------------------------------------
C_LowViolenceHostageDeathModel::~C_LowViolenceHostageDeathModel()
{
}

//-----------------------------------------------------------------------------
void C_LowViolenceHostageDeathModel::Interp_Copy( VarMapping_t *pDest, CBaseEntity *pSourceEntity, VarMapping_t *pSrc )
{
	if ( !pDest || !pSrc )
		return;

	if ( pDest->m_Entries.Count() != pSrc->m_Entries.Count() )
	{
		Assert( false );
		return;
	}

	int c = pDest->m_Entries.Count();
	for ( int i = 0; i < c; i++ )
	{
		pDest->m_Entries[ i ].watcher->Copy( pSrc->m_Entries[i].watcher );
	}
}

//-----------------------------------------------------------------------------
bool C_LowViolenceHostageDeathModel::SetupLowViolenceModel( C_CHostage *pHostage )
{
	const model_t *model = pHostage->GetEngineObject()->GetModel();
	const char *pModelName = modelinfo->GetModelName( model );
	if ( InitializeAsClientEntity( pModelName, RENDER_GROUP_OPAQUE_ENTITY ) == false )
	{
		EntityList()->DestroyEntity(this);// Release();
		return false;
	}

	// Play the low-violence death anim
	if (GetEngineObject()->LookupSequence( "death1" ) == -1 )
	{
		EntityList()->DestroyEntity(this); //Release();
		return false;
	}

	m_flFadeOutStart = gpGlobals->curtime + 5.0f;
	SetNextClientThink( CLIENT_THINK_ALWAYS );

	GetEngineObject()->SetSequence(GetEngineObject()->LookupSequence( "death1" ) );
	GetEngineObject()->ForceClientSideAnimationOn();

	if ( pHostage && !pHostage->IsDormant() )
	{
		GetEngineObject()->SetNetworkOrigin( pHostage->GetEngineObject()->GetAbsOrigin() );
		GetEngineObject()->SetAbsOrigin( pHostage->GetEngineObject()->GetAbsOrigin() );
		GetEngineObject()->SetAbsVelocity( pHostage->GetEngineObject()->GetAbsVelocity() );

		// move my current model instance to the ragdoll's so decals are preserved.
		pHostage->GetEngineObject()->SnatchModelInstance( this->GetEngineObject());

		GetEngineObject()->SetAbsAngles( pHostage->GetRenderAngles() );
		GetEngineObject()->SetNetworkAngles( pHostage->GetRenderAngles() );

		IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();

		// update pose parameters
		float poseParameter[MAXSTUDIOPOSEPARAM];
		GetEngineObject()->GetPoseParameters( pStudioHdr, poseParameter );
		for ( int i=0; i<NumInterestingPoseParameters; ++i )
		{
			int poseParameterIndex = GetEngineObject()->LookupPoseParameter( pStudioHdr, InterestingPoseParameters[i] );
			GetEngineObject()->SetPoseParameter( pStudioHdr, poseParameterIndex, poseParameter[poseParameterIndex] );
		}
	}

	GetEngineObject()->Interp_Reset();
	return true;
}

//-----------------------------------------------------------------------------
void C_LowViolenceHostageDeathModel::ClientThink( void )
{
	if ( m_flFadeOutStart > gpGlobals->curtime )
	{
		 return;
	}

	int iAlpha = GetEngineObject()->GetRenderColor().a;

	iAlpha = MAX( iAlpha - ( g_ragdoll_fadespeed.GetInt() * gpGlobals->frametime ), 0 );

	GetEngineObject()->SetRenderMode( kRenderTransAlpha );
	GetEngineObject()->SetRenderColorA( iAlpha );

	if ( iAlpha == 0 )
	{
		EntityList()->DestroyEntity(this);//Release();
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_CHostage::RecvProxy_Rescued( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	C_CHostage *pHostage= (C_CHostage *) pStruct;
	
	bool isRescued = pData->m_Value.m_Int != 0;

	if ( isRescued && !pHostage->m_isRescued )
	{
		// hostage was rescued
		pHostage->m_flDeadOrRescuedTime = gpGlobals->curtime + 2;
		pHostage->GetEngineObject()->SetRenderMode( kRenderGlow );
		pHostage->SetNextClientThink( gpGlobals->curtime );
	}

	pHostage->m_isRescued = isRescued;
}

//-----------------------------------------------------------------------------
IMPLEMENT_CLIENTCLASS_DT(C_CHostage, DT_CHostage, CHostage)
	
	RecvPropInt( RECVINFO( m_isRescued ), 0, C_CHostage::RecvProxy_Rescued ),
	RecvPropInt( RECVINFO( m_iHealth ) ),
	RecvPropInt( RECVINFO( m_iMaxHealth ) ),
	RecvPropInt( RECVINFO( m_lifeState ) ),
	
	RecvPropEHandle( RECVINFO( m_leader ) ),

END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_CHostage::C_CHostage()
{
	g_Hostages.AddToTail( this );

	m_flDeadOrRescuedTime = 0.0;
	m_flLastBodyYaw = 0;
	m_createdLowViolenceRagdoll = false;
	
	

	m_PlayerAnimState = CreateHostageAnimState( this, this, LEGANIM_8WAY, false );
	
	m_leader = NULL;
	m_blinkTimer.Invalidate();
	m_seq = -1;

	m_flCurrentHeadPitch = 0;
	m_flCurrentHeadYaw = 0;

	m_eyeAttachment = -1;
	m_chestAttachment = -1;
	m_headYawPoseParam = -1;
	m_headPitchPoseParam = -1;
	m_lookAt = Vector( 0, 0, 0 );
	m_isInit = false;
	m_lookAroundTimer.Invalidate();
}

bool C_CHostage::Init(int entnum, int iSerialNum) {
	bool bRet = BaseClass::Init(entnum, iSerialNum);
	// set the model so the PlayerAnimState uses the Hostage activities/sequences
	GetEngineObject()->SetModelName(MAKE_STRING("models/Characters/Hostage_01.mdl"));
	// TODO: Get IK working on the steep slopes CS has, then enable it on characters.
	GetEngineObject()->GetEntClientFlags() |= ENTCLIENTFLAG_DONTUSEIK;
	return bRet;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_CHostage::~C_CHostage()
{
	g_Hostages.FindAndRemove( this );
	m_PlayerAnimState->Release();
}

//-----------------------------------------------------------------------------
void C_CHostage::Spawn( void )
{
	m_leader = NULL;
	m_blinkTimer.Invalidate();
}

//-----------------------------------------------------------------------------
bool C_CHostage::ShouldDraw( void )
{
	if ( m_createdLowViolenceRagdoll )
		return false;

	return BaseClass::ShouldDraw();
}

//-----------------------------------------------------------------------------
C_BaseEntity* C_CHostage::BecomeRagdollOnClient()
{
	if ( g_RagdollLVManager.IsLowViolence() )
	{
		// We can't just play the low-violence anim ourselves, since we're about to be deleted by the server.
		// So, let's create another entity that can play the anim and stick around.
		C_LowViolenceHostageDeathModel *pLowViolenceModel = (C_LowViolenceHostageDeathModel*)EntityList()->CreateEntityByName( "C_LowViolenceHostageDeathModel" );
		m_createdLowViolenceRagdoll = pLowViolenceModel->SetupLowViolenceModel( this );
		if ( m_createdLowViolenceRagdoll )
		{
			UpdateVisibility();
			g_HostageRagdolls.AddToTail( pLowViolenceModel );
			return pLowViolenceModel;
		}
		else
		{
			// if we don't have a low-violence death anim, don't create a ragdoll.
			return NULL;
		}
	}

	C_BaseEntity *pRagdoll = BaseClass::BecomeRagdollOnClient();
	if ( pRagdoll && pRagdoll != this )
	{
		g_HostageRagdolls.AddToTail( pRagdoll );
	}
	return pRagdoll;
}

//-----------------------------------------------------------------------------
/** 
 * Set up attachment and pose param indices.
 * We can't do this in the constructor or Spawn() because the data isn't 
 * there yet.
 */
void C_CHostage::Initialize( )
{
	m_eyeAttachment = GetEngineObject()->LookupAttachment( "eyes" );
	m_chestAttachment = GetEngineObject()->LookupAttachment( "chest" );

	m_headYawPoseParam = GetEngineObject()->LookupPoseParameter( "head_yaw" );
	GetEngineObject()->GetPoseParameterRange( m_headYawPoseParam, m_headYawMin, m_headYawMax );

	m_headPitchPoseParam = GetEngineObject()->LookupPoseParameter( "head_pitch" );
	GetEngineObject()->GetPoseParameterRange( m_headPitchPoseParam, m_headPitchMin, m_headPitchMax );

	m_bodyYawPoseParam = GetEngineObject()->LookupPoseParameter( "body_yaw" );
	GetEngineObject()->GetPoseParameterRange( m_bodyYawPoseParam, m_bodyYawMin, m_bodyYawMax );

	Vector pos;
	QAngle angles;

	if (!GetEngineObject()->GetAttachment( m_eyeAttachment, pos, angles ))
	{
		m_vecViewOffset = Vector( 0, 0, 50.0f );
	}
	else
	{
		m_vecViewOffset = pos - GetEngineObject()->GetAbsOrigin();
	}


	if (!GetEngineObject()->GetAttachment( m_chestAttachment, pos, angles ))
	{
		m_lookAt = Vector( 0, 0, 0 );
	}
	else
	{
		Vector forward;
		AngleVectors( angles, &forward );
		m_lookAt = EyePosition() + 100.0f * forward;
	}
}

//-----------------------------------------------------------------------------
CWeaponCSBase* C_CHostage::CSAnim_GetActiveWeapon()
{
	return NULL;
}

//-----------------------------------------------------------------------------
bool C_CHostage::CSAnim_CanMove()
{
	return true;
}


//-----------------------------------------------------------------------------
/**
 * Orient head and eyes towards m_lookAt.
 */
void C_CHostage::UpdateLookAt( IStudioHdr *pStudioHdr )
{
	if (!m_isInit)
	{
		m_isInit = true;
		Initialize( );
	}

	// head yaw
	if (m_headYawPoseParam < 0 || m_bodyYawPoseParam < 0 || m_headPitchPoseParam < 0)
		return;

	if (GetLeader())
	{
		m_lookAt = GetLeader()->EyePosition();
	}
	
	// orient eyes
	m_viewtarget = m_lookAt;

	// blinking
	if (m_blinkTimer.IsElapsed())
	{
		m_blinktoggle = !m_blinktoggle;
		m_blinkTimer.Start( RandomFloat( 1.5f, 4.0f ) );
	}

	// Figure out where we want to look in world space.
	QAngle desiredAngles;
	Vector to = m_lookAt - EyePosition();
	VectorAngles( to, desiredAngles );

	// Figure out where our body is facing in world space.
	float poseParams[MAXSTUDIOPOSEPARAM];
	GetEngineObject()->GetPoseParameters( pStudioHdr, poseParams );
	QAngle bodyAngles( 0, 0, 0 );
	bodyAngles[YAW] = GetRenderAngles()[YAW] + RemapVal( poseParams[m_bodyYawPoseParam], 0, 1, m_bodyYawMin, m_bodyYawMax );


	float flBodyYawDiff = bodyAngles[YAW] - m_flLastBodyYaw;
	m_flLastBodyYaw = bodyAngles[YAW];
	

	// Set the head's yaw.
	float desired = AngleNormalize( desiredAngles[YAW] - bodyAngles[YAW] );
	desired = clamp( desired, m_headYawMin, m_headYawMax );
	m_flCurrentHeadYaw = ApproachAngle( desired, m_flCurrentHeadYaw, HOSTAGE_HEAD_TURN_RATE * gpGlobals->frametime );

	// Counterrotate the head from the body rotation so it doesn't rotate past its target.
	m_flCurrentHeadYaw = AngleNormalize( m_flCurrentHeadYaw - flBodyYawDiff );
	desired = clamp( desired, m_headYawMin, m_headYawMax );
	
	GetEngineObject()->SetPoseParameter( pStudioHdr, m_headYawPoseParam, m_flCurrentHeadYaw );

	
	// Set the head's yaw.
	desired = AngleNormalize( desiredAngles[PITCH] );
	desired = clamp( desired, m_headPitchMin, m_headPitchMax );
	
	m_flCurrentHeadPitch = ApproachAngle( desired, m_flCurrentHeadPitch, HOSTAGE_HEAD_TURN_RATE * gpGlobals->frametime );
	m_flCurrentHeadPitch = AngleNormalize( m_flCurrentHeadPitch );
	GetEngineObject()->SetPoseParameter( pStudioHdr, m_headPitchPoseParam, m_flCurrentHeadPitch );

	GetEngineObject()->SetPoseParameter( pStudioHdr, "head_roll", 0.0f );
}


//-----------------------------------------------------------------------------
/**
 * Look around at various interesting things
 */
void C_CHostage::LookAround( void )
{
	if (GetLeader() == NULL && m_lookAroundTimer.IsElapsed())
	{
		m_lookAroundTimer.Start( RandomFloat( 3.0f, 15.0f ) );

		Vector forward;
		QAngle angles = GetEngineObject()->GetAbsAngles();
		angles[ YAW ] += RandomFloat( m_headYawMin, m_headYawMax );
		angles[ PITCH ] += RandomFloat( m_headPitchMin, m_headPitchMax );
		AngleVectors( angles, &forward );
		m_lookAt = EyePosition() + 100.0f * forward;
	}
}

//-----------------------------------------------------------------------------
void C_CHostage::UpdateClientSideAnimation()
{
	if (IsDormant())
	{
		return;
	}

	m_PlayerAnimState->Update(GetEngineObject()->GetAbsAngles()[YAW], GetEngineObject()->GetAbsAngles()[PITCH] );

	// initialize pose parameters
	char *setToZero[] =
	{
		"spine_yaw",
		"head_roll"
	};
	IStudioHdr *pStudioHdr = GetEngineObject()->GetModelPtr();
	for ( int i=0; i < ARRAYSIZE( setToZero ); i++ )
	{
		int index = GetEngineObject()->LookupPoseParameter( pStudioHdr, setToZero[i] );
		if ( index >= 0 )
			GetEngineObject()->SetPoseParameter( pStudioHdr, index, 0 );
	}

	// orient head and eyes
	LookAround();
	UpdateLookAt( pStudioHdr );


	BaseClass::UpdateClientSideAnimation();
}

//-----------------------------------------------------------------------------
void C_CHostage::ClientThink()
{
	C_BaseCombatCharacter::ClientThink();

	int speed = 2;
	int a = GetEngineObject()->GetRenderColor().a;

	a = MAX( 0, a - speed );

	GetEngineObject()->SetRenderColorA( a );

	if (GetEngineObject()->GetRenderColor().a > 0 )
	{
		SetNextClientThink( gpGlobals->curtime + 0.001 );
	}
}

//-----------------------------------------------------------------------------
bool C_CHostage::WasRecentlyKilledOrRescued( void )
{
	return ( gpGlobals->curtime < m_flDeadOrRescuedTime );
}

//-----------------------------------------------------------------------------
void C_CHostage::OnPreDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnPreDataChanged( updateType );

	m_OldLifestate = m_lifeState;
}

//-----------------------------------------------------------------------------
void C_CHostage::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	if ( m_OldLifestate != m_lifeState )
	{
		if( m_lifeState == LIFE_DEAD || m_lifeState == LIFE_DYING )
			m_flDeadOrRescuedTime = gpGlobals->curtime + 2;
	}
}

//-----------------------------------------------------------------------------
void C_CHostage::ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName )
{
	static ConVar *violence_hblood = cvar->FindVar( "violence_hblood" );
	if ( violence_hblood && !violence_hblood->GetBool() )
		return;

	BaseClass::ImpactTrace( pTrace, iDamageType, pCustomImpactName );
}

