//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A moving vehicle that is used as a battering ram
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "fourwheelvehiclephysics.h"
#include "engine/IEngineSound.h"
#include "soundenvelope.h"
#include "in_buttons.h"
#include "player.h"
#include "IEffects.h"
#include "physics_saverestore.h"
#include "vehicle_base.h"
#include "isaverestore.h"
#include "movevars_shared.h"
#include "te_effect_dispatch.h"
#include "particle_parse.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"












//BEGIN_DATADESC_NO_BASE( CFourWheelVehiclePhysics )

// These two are reset every time 
//	DEFINE_FIELD( m_pOuter, FIELD_EHANDLE ),
//											m_pOuterServerVehicle;

	// Quiet down classcheck
	// DEFINE_FIELD( m_controls, vehicle_controlparams_t ),

	// Controls
	//DEFINE_FIELD( m_controls.throttle, FIELD_FLOAT ),
	//DEFINE_FIELD( m_controls.steering, FIELD_FLOAT ),
	//DEFINE_FIELD( m_controls.brake, FIELD_FLOAT ),
	//DEFINE_FIELD( m_controls.boost, FIELD_FLOAT ),
	//DEFINE_FIELD( m_controls.handbrake, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_controls.handbrakeLeft, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_controls.handbrakeRight, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_controls.brakepedal, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_controls.bHasBrakePedal, FIELD_BOOLEAN ),

	// This has to be handled by the containing class owing to 'owner' issues
//	DEFINE_PHYSPTR( m_pVehicle ),

	//DEFINE_FIELD( m_nSpeed, FIELD_INTEGER ),
	//DEFINE_FIELD( m_nLastSpeed, FIELD_INTEGER ),
	//DEFINE_FIELD( m_nRPM, FIELD_INTEGER ),
	//DEFINE_FIELD( m_fLastBoost, FIELD_FLOAT ),
	//DEFINE_FIELD( m_nBoostTimeLeft, FIELD_INTEGER ),
	//DEFINE_FIELD( m_nHasBoost, FIELD_INTEGER ),

	//DEFINE_FIELD( m_maxThrottle, FIELD_FLOAT ),
	//DEFINE_FIELD( m_flMaxRevThrottle, FIELD_FLOAT ),
	//DEFINE_FIELD( m_flMaxSpeed, FIELD_FLOAT ),
	//DEFINE_FIELD( m_actionSpeed, FIELD_FLOAT ),

	// This has to be handled by the containing class owing to 'owner' issues
//	DEFINE_PHYSPTR_ARRAY( m_pWheels ),

	//DEFINE_FIELD( m_wheelCount, FIELD_INTEGER ),

	//DEFINE_ARRAY( m_wheelPosition, FIELD_VECTOR, 4 ),
	//DEFINE_ARRAY( m_wheelRotation, FIELD_VECTOR, 4 ),
	//DEFINE_ARRAY( m_wheelBaseHeight, FIELD_FLOAT, 4 ),
	//DEFINE_ARRAY( m_wheelTotalHeight, FIELD_FLOAT, 4 ),
	//DEFINE_ARRAY( m_poseParameters, FIELD_INTEGER, 12 ),
	//DEFINE_FIELD( m_actionValue, FIELD_FLOAT ),
	//DEFINE_KEYFIELD( m_actionScale, FIELD_FLOAT, "actionScale" ),
	//DEFINE_FIELD( m_debugRadius, FIELD_FLOAT ),
	//DEFINE_FIELD( m_throttleRate, FIELD_FLOAT ),
	//DEFINE_FIELD( m_throttleStartTime, FIELD_FLOAT ),
	//DEFINE_FIELD( m_throttleActiveTime, FIELD_FLOAT ),
	//DEFINE_FIELD( m_turboTimer, FIELD_FLOAT ),

	//DEFINE_FIELD( m_flVehicleVolume, FIELD_FLOAT ),
	//DEFINE_FIELD( m_bIsOn, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_bLastThrottle, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_bLastBoost, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_bLastSkid, FIELD_BOOLEAN ),
//END_DATADESC()



