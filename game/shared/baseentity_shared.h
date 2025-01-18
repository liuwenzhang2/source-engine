//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BASEENTITY_SHARED_H
#define BASEENTITY_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/convar.h"

extern ConVar hl2_episodic;

// Simple shared header file for common base entities






#if defined( CLIENT_DLL )
#include "shared_classnames.h"
#include "c_baseentity.h"
#include "c_baseanimating.h"
#else
#include "baseentity.h"

#ifdef HL2_EPISODIC
	#include "info_darknessmode_lightsource.h"
#endif // HL2_EPISODIC

#endif
#include "positionwatcher.h"

//#if !defined( NO_ENTITY_PREDICTION )
//// CBaseEntity inlines
//inline bool CBaseEntity::IsPlayerSimulated( void ) const
//{
//	return m_bIsPlayerSimulated;
//}
//
//inline CBasePlayer *CBaseEntity::GetSimulatingPlayer( void )
//{
//	return m_hPlayerSimulationOwner;
//}
//#endif

inline bool CBaseEntity::IsAlive( void )
{
	return m_lifeState == LIFE_ALIVE; 
}

// Shared EntityMessage between game and client .dlls
#define BASEENTITY_MSG_REMOVE_DECALS	1

extern const float k_flMaxEntityPosCoord;
extern const float k_flMaxEntityEulerAngle;
extern const float k_flMaxEntitySpeed;
extern const float k_flMaxEntitySpinRate;

inline bool IsEntityCoordinateReasonable ( const vec_t c )
{
	float r = k_flMaxEntityPosCoord;
	return c > -r && c < r;
}

inline bool IsEntityPositionReasonable( const Vector &v )
{
	float r = k_flMaxEntityPosCoord;
	return
		v.x > -r && v.x < r &&
		v.y > -r && v.y < r &&
		v.z > -r && v.z < r;
}

// Returns:
//   -1 - velocity is really, REALLY bad and probably should be rejected.
//   0  - velocity was suspicious and clamped.
//   1  - velocity was OK and not modified
extern int CheckEntityVelocity( Vector &v );

inline bool IsEntityQAngleReasonable( const QAngle &q )
{
	float r = k_flMaxEntityEulerAngle;
	return
		q.x > -r && q.x < r &&
		q.y > -r && q.y < r &&
		q.z > -r && q.z < r;
}

// Angular velocity in exponential map form
inline bool IsEntityAngularVelocityReasonable( const Vector &q )
{
	float r = k_flMaxEntitySpinRate;
	return
		q.x > -r && q.x < r &&
		q.y > -r && q.y < r &&
		q.z > -r && q.z < r;
}

// Angular velocity of each Euler angle.
inline bool IsEntityQAngleVelReasonable( const QAngle &q )
{
	float r = k_flMaxEntitySpinRate;
	return
		q.x > -r && q.x < r &&
		q.y > -r && q.y < r &&
		q.z > -r && q.z < r;
}

extern bool CheckEmitReasonablePhysicsSpew();

#endif // BASEENTITY_SHARED_H
