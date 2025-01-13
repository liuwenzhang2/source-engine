//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//
#ifndef PORTAL_PLAYER_H
#define PORTAL_PLAYER_H
#pragma once

#include "portal_playeranimstate.h"
#include "c_basehlplayer.h"
#include "portal_player_shared.h"
#include "c_prop_portal.h"
#include "weapon_portalbase.h"
#include "c_func_liquidportal.h"
#include "colorcorrectionmgr.h"

//=============================================================================
// >> Portal_Player
//=============================================================================
class C_Portal_Player : public C_BaseHLPlayer
{
public:
	DECLARE_CLASS( C_Portal_Player, C_BaseHLPlayer );

	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_INTERPOLATION();


	C_Portal_Player();
	~C_Portal_Player( void );

	bool Init(int entnum, int iSerialNum);

	void ClientThink( void );
	void FixTeleportationRoll( void );

	static inline C_Portal_Player* GetLocalPortalPlayer()
	{
		return (C_Portal_Player*)EntityList()->GetLocalPlayer();
	}

	static inline C_Portal_Player* GetLocalPlayer()
	{
		return (C_Portal_Player*)EntityList()->GetLocalPlayer();
	}

	virtual const QAngle& GetRenderAngles();

	virtual void UpdateClientSideAnimation();
	void DoAnimationEvent( PlayerAnimEvent_t event, int nData );

	virtual int DrawModel( int flags );
	virtual void AddEntity( void );

	QAngle GetAnimEyeAngles( void ) { return m_angEyeAngles; }
	Vector GetAttackSpread( CBaseCombatWeapon *pWeapon, CBaseEntity *pTarget = NULL );

	// Used by prediction, sets the view angles for the player
	virtual void SetLocalViewAngles( const QAngle &viewAngles );
	virtual void SetViewAngles( const QAngle &ang );

	// Should this object cast shadows?
	virtual ShadowType_t	ShadowCastType( void );
	//virtual C_BaseEntity* BecomeRagdollOnClient();
	virtual bool			ShouldDraw( void );
	virtual const QAngle&	EyeAngles();
	virtual void			OnPreDataChanged( DataUpdateType_t type );
	virtual void			OnDataChanged( DataUpdateType_t type );
	bool					DetectAndHandlePortalTeleportation( void ); //detects if the player has portalled and fixes views
	virtual float			GetFOV( void );
	virtual IStudioHdr*		OnNewModel( void );
	virtual void			TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr );
	virtual void			ItemPreFrame( void );
	virtual void			ItemPostFrame( void );
	virtual float			GetMinFOV()	const { return 5.0f; }
	virtual Vector			GetAutoaimVector( float flDelta );
	virtual bool			ShouldReceiveProjectedTextures( int flags );
	virtual void			PostDataUpdate( DataUpdateType_t updateType );
	virtual void			GetStepSoundVelocities( float *velwalk, float *velrun );
	virtual void			PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force );
	virtual void			PreThink( void );
	virtual void			DoImpactEffect( trace_t &tr, int nDamageType );

	virtual Vector			EyePosition();
	Vector					EyeFootPosition( const QAngle &qEyeAngles );//interpolates between eyes and feet based on view angle roll
	inline Vector			EyeFootPosition( void ) { return EyeFootPosition( EyeAngles() ); }; 

	virtual void	CalcView( Vector &eyeOrigin, QAngle &eyeAngles, float &zNear, float &zFar, float &fov );
	void			CalcPortalView( Vector &eyeOrigin, QAngle &eyeAngles );
	virtual void	CalcViewModelView( const Vector& eyeOrigin, const QAngle& eyeAngles);

	CBaseEntity*	FindUseEntity( void );
	CBaseEntity*	FindUseEntityThroughPortal( void );

	inline bool		IsCloseToPortal( void ) //it's usually a good idea to turn on draw hacks when this is true
	{
		return ((PortalEyeInterpolation.m_bEyePositionIsInterpolating) || (GetPortalEnvironment() != NULL));
	} 

	bool	CanSprint( void );
	void	StartSprinting( void );
	void	StopSprinting( void );
	void	HandleSpeedChanges( void );
	void	UpdateLookAt( void );
	void	Initialize( void );
	int		GetIDTarget() const;
	void	UpdateIDTarget( void );



	Activity TranslateActivity( Activity baseAct, bool *pRequired = NULL );
	CWeaponPortalBase* GetActivePortalWeapon() const;

	bool IsSuppressingCrosshair( void ) { return m_bSuppressingCrosshair; }
	C_Prop_Portal* GetPortalEnvironment();
private:

	C_Portal_Player( const C_Portal_Player & );

	void UpdatePortalEyeInterpolation( void );
	
	CPortalPlayerAnimState *m_PlayerAnimState;

	QAngle	m_angEyeAngles;
	CInterpolatedVar< QAngle >	m_iv_angEyeAngles;

	virtual const IEngineObjectClient* GetRepresentativeRagdoll() const;
	EHANDLE	m_hRagdoll;

	int	m_headYawPoseParam;
	int	m_headPitchPoseParam;
	float m_headYawMin;
	float m_headYawMax;
	float m_headPitchMin;
	float m_headPitchMax;
	bool m_bSuppressingCrosshair;

	bool m_isInit;
	Vector m_vLookAtTarget;

	float m_flLastBodyYaw;
	float m_flCurrentHeadYaw;
	float m_flCurrentHeadPitch;
	float m_flStartLookTime;

	int	  m_iIDEntIndex;

	CountdownTimer m_blinkTimer;

	int	  m_iSpawnInterpCounter;
	int	  m_iSpawnInterpCounterCache;

	int	  m_iPlayerSoundType;


	int	m_iForceNoDrawInPortalSurface; //only valid for one frame, used to temp disable drawing of the player model in a surface because of freaky artifacts

	struct PortalEyeInterpolation_t
	{
		bool	m_bEyePositionIsInterpolating; //flagged when the eye position would have popped between two distinct positions and we're smoothing it over
		Vector	m_vEyePosition_Interpolated; //we'll be giving the interpolation a certain amount of instant movement per frame based on how much an uninterpolated eye would have moved
		Vector	m_vEyePosition_Uninterpolated; //can't have smooth movement without tracking where we just were
		//bool	m_bNeedToUpdateEyePosition;
		//int		m_iFrameLastUpdated;

		int		m_iTickLastUpdated;
		float	m_fTickInterpolationAmountLastUpdated;
		bool	m_bDisableFreeMovement; //used for one frame usually when error in free movement is likely to be high
		bool	m_bUpdatePosition_FreeMove;

		PortalEyeInterpolation_t( void ) : m_iTickLastUpdated(0), m_fTickInterpolationAmountLastUpdated(0.0f), m_bDisableFreeMovement(false), m_bUpdatePosition_FreeMove(false) { };
	} PortalEyeInterpolation;

	struct PreDataChanged_Backup_t
	{
		CHandle<C_Prop_Portal>	m_hPortalEnvironment;
		CHandle<C_Func_LiquidPortal>	m_hSurroundingLiquidPortal;
		//Vector					m_ptPlayerPosition;
		QAngle					m_qEyeAngles;
	} PreDataChanged_Backup;

	Vector	m_ptEyePosition_LastCalcView;
	QAngle	m_qEyeAngles_LastCalcView; //we've got some VERY persistent single frame errors while teleporting, this will be updated every frame in CalcView() and will serve as a central source for fixed angles
	C_Prop_Portal *m_pPortalEnvironment_LastCalcView;

	ClientCCHandle_t	m_CCDeathHandle;	// handle to death cc effect
	float				m_flDeathCCWeight;	// for fading in cc effect	

public:
	bool	m_bPitchReorientation;
	float	m_fReorientationRate;
	bool	m_bEyePositionIsTransformedByPortal; //when the eye and body positions are not on the same side of a portal

	CHandle<C_Func_LiquidPortal>	m_hSurroundingLiquidPortal; //a liquid portal whose volume the player is standing in
};

inline C_Portal_Player *ToPortalPlayer( IClientEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsPlayer() )
		return NULL;

	return dynamic_cast<C_Portal_Player*>( pEntity );
}

inline C_Portal_Player *GetPortalPlayer( void )
{
	return static_cast<C_Portal_Player*>( (C_BasePlayer*)EntityList()->GetLocalPlayer() );
}

#endif //Portal_PLAYER_H
