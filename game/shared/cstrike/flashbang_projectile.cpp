//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "flashbang_projectile.h"
#include "shake.h"
#include "engine/IEngineSound.h"
#include "cs_player.h"
#include "dlight.h"
#include "KeyValues.h"
#include "weapon_csbase.h"
#include "collisionutils.h"
#include "particle_smokegrenade.h"
#include "smoke_fog_overlay_shared.h"

#define GRENADE_MODEL "models/Weapons/w_eq_flashbang_thrown.mdl"


LINK_ENTITY_TO_CLASS( flashbang_projectile, CFlashbangProjectile );
PRECACHE_WEAPON_REGISTER( flashbang_projectile );

float PercentageOfFlashForPlayer(CBaseEntity *player, Vector flashPos, CBaseEntity *pevInflictor)
{
	float retval = 0.0f;

	trace_t tr;

	Vector pos = player->EyePosition();
	Vector vecRight, vecUp, vecForward;
	AngleVectors( player->EyeAngles(), &vecForward );

	QAngle tempAngle;
	VectorAngles(player->EyePosition() - flashPos, tempAngle);
	AngleVectors(tempAngle, NULL, &vecRight, &vecUp);

	vecRight.NormalizeInPlace();
	vecUp.NormalizeInPlace();

	UTIL_TraceLine( flashPos, pos,
		(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_DEBRIS|CONTENTS_MONSTER),
		pevInflictor, COLLISION_GROUP_NONE, &tr );

	if ((tr.fraction == 1.0) || (tr.m_pEnt == player))
	{
		retval = 1.0;
	}
	else
	{
		return 0.0;
	}

	CBaseEntity *pSGren;

	for( pSGren = gEntList.FindEntityByClassname( NULL, "env_particlesmokegrenade" );
		pSGren;
		pSGren = gEntList.FindEntityByClassname( pSGren, "env_particlesmokegrenade" ) )
	{
		ParticleSmokeGrenade *pPSG =( ParticleSmokeGrenade* ) pSGren;

		if ( gpGlobals->curtime > pPSG->m_flSpawnTime + pPSG->m_FadeStartTime )		// ignore the smoke grenade if it's fading.
			continue;

		float flHit1, flHit2;

		float flInnerRadius = SMOKEGRENADE_PARTICLERADIUS;
//		float flOutterRadius = flInnerRadius + ( 0.5 * SMOKEPARTICLE_SIZE );

		Vector vPos = pSGren->GetEngineObject()->GetAbsOrigin();

		/*debugoverlay->AddBoxOverlay( pSGren->GetAbsOrigin(), Vector( flInnerRadius, flInnerRadius, flInnerRadius ),
			Vector( -flInnerRadius, -flInnerRadius, -flInnerRadius ), QAngle( 0, 0, 0 ), 0, 255, 0, 30, 10 );
		debugoverlay->AddBoxOverlay( pSGren->GetAbsOrigin(), Vector( flOutterRadius, flOutterRadius, flOutterRadius ),
			Vector( -flOutterRadius, -flOutterRadius, -flOutterRadius ), QAngle( 0, 0, 0 ), 255, 0, 0, 30, 10 ); */

		if ( IntersectInfiniteRayWithSphere( pos, vecForward, vPos, flInnerRadius, &flHit1, &flHit2 ) )
		{
			retval *= 0.8;
		}
/*		else if ( IntersectInfiniteRayWithSphere( pos, vecForward, vPos, flOutterRadius, &flHit1, &flHit2 ) )
		{
			retval *= 0.9;
		}
*/
	}

	return retval;

}

// --------------------------------------------------------------------------------------------------- //
//
// RadiusDamage - this entity is exploding, or otherwise needs to inflict damage upon entities within a certain range.
// 
// only damage ents that can clearly be seen by the explosion!
// --------------------------------------------------------------------------------------------------- //

void RadiusFlash( 
	Vector vecSrc, 
	CBaseEntity *pevInflictor, 
	CBaseEntity *pevAttacker, 
	float flDamage, 
	int iClassIgnore, 
	int bitsDamageType )
{
	vecSrc.z += 1;// in case grenade is lying on the ground

	if ( !pevAttacker )
		pevAttacker = pevInflictor;
	
	trace_t		tr;
	float		flAdjustedDamage;
	variant_t	var;
	Vector		vecEyePos;
	float		fadeTime, fadeHold;
	Vector		vForward;
	Vector		vecLOS;
	float		flDot;
	
	CBaseEntity		*pEntity = NULL;
	static float	flRadius = 1500;
	float			falloff = flDamage / flRadius;

	bool bInWater = (UTIL_PointContents( vecSrc ) == CONTENTS_WATER);

	// iterate on all entities in the vicinity.
	while ((pEntity = gEntList.FindEntityInSphere( pEntity, vecSrc, flRadius )) != NULL)
	{	
		bool bPlayer = pEntity->IsPlayer();
		bool bHostage = ( Q_stricmp( pEntity->GetClassname(), "hostage_entity" ) == 0 );
		
		if( !bPlayer && !bHostage )
			continue;

		vecEyePos = pEntity->EyePosition();

		// blasts don't travel into or out of water
		if ( bInWater && pEntity->GetWaterLevel() == 0)
			continue;
		if (!bInWater && pEntity->GetWaterLevel() == 3)
			continue;

		float percentageOfFlash = PercentageOfFlashForPlayer(pEntity, vecSrc, pevInflictor);

		if ( percentageOfFlash > 0.0 )
		{
			// decrease damage for an ent that's farther from the grenade
			flAdjustedDamage = flDamage - ( vecSrc - pEntity->EyePosition() ).Length() * falloff;
		
			if ( flAdjustedDamage > 0 )
			{
				// See if we were facing the flash
				AngleVectors( pEntity->EyeAngles(), &vForward );

				vecLOS = ( vecSrc - vecEyePos );

				float flDistance = vecLOS.Length();

				// Normalize both vectors so the dotproduct is in the range -1.0 <= x <= 1.0 
				vecLOS.NormalizeInPlace();
					
				flDot = DotProduct (vecLOS, vForward);

				float startingAlpha = 255;
	
				// if target is facing the bomb, the effect lasts longer
				if( flDot >= 0.5 )
				{
					// looking at the flashbang
					fadeTime = flAdjustedDamage * 2.5f;
					fadeHold = flAdjustedDamage * 1.25f;
				}
				else if( flDot >= -0.5 )
				{
					// looking to the side
					fadeTime = flAdjustedDamage * 1.75f;
					fadeHold = flAdjustedDamage * 0.8f;
				}
				else
				{
					// facing away
					fadeTime = flAdjustedDamage * 1.0f;
					fadeHold = flAdjustedDamage * 0.75f;
					startingAlpha = 200;
				}

				fadeTime *= percentageOfFlash;
				fadeHold *= percentageOfFlash;

				if ( bPlayer )
				{
    				// blind players and bots
					CCSPlayer *player = static_cast< CCSPlayer * >( pEntity );

                    //=============================================================================
                    // HPE_BEGIN:
                    // [tj] Store who was responsible for the most recent flashbang blinding.
                    //=============================================================================
                     
                    CCSPlayer *attacker = ToCSPlayer (pevAttacker);
                    if (attacker && player)
                    {
                        player->SetLastFlashbangAttacker(attacker);
                    }
                     
                    //=============================================================================
                    // HPE_END
                    //=============================================================================
                    

                                         
					player->Blind( fadeHold, fadeTime, startingAlpha );

					// deafen players and bots
					player->Deafen( flDistance );
				}
				else if ( bHostage )
				{
					variant_t val;					
					val.SetFloat( fadeTime );
					pEntity->AcceptInput( "flashbang", pevInflictor, pevAttacker, val, 0 );
				}
			}	
		}
	}

	CPVSFilter filter(vecSrc);
	te->DynamicLight( filter, 0.0, &vecSrc, 255, 255, 255, 2, 400, 0.1, 768 );
}

// --------------------------------------------------------------------------------------------------- //
// CFlashbangProjectile implementation.
// --------------------------------------------------------------------------------------------------- //

CFlashbangProjectile* CFlashbangProjectile::Create( 
	const Vector &position, 
	const QAngle &angles, 
	const Vector &velocity, 
	const AngularImpulse &angVelocity, 
	CBaseCombatCharacter *pOwner )
{
	CFlashbangProjectile *pGrenade = (CFlashbangProjectile*)CBaseEntity::Create( "flashbang_projectile", position, angles, pOwner );
	
	// Set the timer for 1 second less than requested. We're going to issue a SOUND_DANGER
	// one second before detonation.
	pGrenade->GetEngineObject()->SetAbsVelocity( velocity );
	pGrenade->SetupInitialTransmittedGrenadeVelocity( velocity );
	pGrenade->SetThrower( pOwner );
	pGrenade->m_flDamage = 100;
	pGrenade->ChangeTeam( pOwner->GetTeamNumber() );

	pGrenade->SetTouch( &CBaseGrenade::BounceTouch );

	pGrenade->SetThink( &CBaseCSGrenadeProjectile::DangerSoundThink );
	pGrenade->GetEngineObject()->SetNextThink( gpGlobals->curtime );

	pGrenade->SetDetonateTimerLength( 1.5 );

	pGrenade->ApplyLocalAngularVelocityImpulse( angVelocity );

	pGrenade->GetEngineObject()->SetGravity( BaseClass::GetGrenadeGravity() );
	pGrenade->GetEngineObject()->SetFriction( BaseClass::GetGrenadeFriction() );
	pGrenade->GetEngineObject()->SetElasticity( BaseClass::GetGrenadeElasticity() );

	pGrenade->m_pWeaponInfo = GetWeaponInfo( WEAPON_FLASHBANG );


	return pGrenade;
}

void CFlashbangProjectile::Spawn()
{
	SetModel( GRENADE_MODEL );
	BaseClass::Spawn();
}

void CFlashbangProjectile::Precache()
{
	engine->PrecacheModel( GRENADE_MODEL );

	g_pSoundEmitterSystem->PrecacheScriptSound( "Flashbang.Explode" );
	g_pSoundEmitterSystem->PrecacheScriptSound( "Flashbang.Bounce" );

	BaseClass::Precache();
}

void CFlashbangProjectile::Detonate()
{
	RadiusFlash (GetEngineObject()->GetAbsOrigin(), this, GetThrower(), 4, CLASS_NONE, DMG_BLAST );
	const char* soundname = "Flashbang.Explode";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);

	// tell the bots a flashbang grenade has exploded
	CCSPlayer *player = ToCSPlayer(GetThrower());
	if ( player )
	{
		IGameEvent * event = gameeventmanager->CreateEvent( "flashbang_detonate" );
		if ( event )
		{
			event->SetInt( "userid", player->GetUserID() );
			event->SetFloat( "x", GetEngineObject()->GetAbsOrigin().x );
			event->SetFloat( "y", GetEngineObject()->GetAbsOrigin().y );
			event->SetFloat( "z", GetEngineObject()->GetAbsOrigin().z );
			gameeventmanager->FireEvent( event );
		}
	}

	gEntList.DestroyEntity( this );
}

//TODO: Let physics handle the sound!
void CFlashbangProjectile::BounceSound( void )
{
	const char* soundname = "Flashbang.Bounce";
	CPASAttenuationFilter filter(this, soundname);

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = 0.0f;
	params.m_pflSoundDuration = NULL;
	params.m_bWarnOnDirectWaveReference = true;
	g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
}
