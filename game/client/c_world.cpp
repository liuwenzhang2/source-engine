//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_world.h"
#include "ivmodemanager.h"
#include "activitylist.h"
#include "decals.h"
#include "engine/ivmodelinfo.h"
#include "ivieweffects.h"
#include "shake.h"
#include "eventlist.h"
// NVNT haptic include for notification of world precache
#include "haptics/haptic_utils.h"
#include "ammodef.h"
#include "iachievementmgr.h"
#include "usermessages.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CWorld
#undef CWorld
#endif

ConVar g_Language("g_Language", "0", FCVAR_REPLICATED);
ConVar sk_autoaim_mode("sk_autoaim_mode", "1", FCVAR_ARCHIVE | FCVAR_REPLICATED);
ConVar	old_radius_damage("old_radiusdamage", "0.0", FCVAR_REPLICATED);

static CViewVectors g_DefaultViewVectors(
	Vector(0, 0, 64),			//VEC_VIEW (m_vView)

	Vector(-16, -16, 0),		//VEC_HULL_MIN (m_vHullMin)
	Vector(16, 16, 72),		//VEC_HULL_MAX (m_vHullMax)

	Vector(-16, -16, 0),		//VEC_DUCK_HULL_MIN (m_vDuckHullMin)
	Vector(16, 16, 36),		//VEC_DUCK_HULL_MAX	(m_vDuckHullMax)
	Vector(0, 0, 28),			//VEC_DUCK_VIEW		(m_vDuckView)

	Vector(-10, -10, -10),		//VEC_OBS_HULL_MIN	(m_vObsHullMin)
	Vector(10, 10, 10),		//VEC_OBS_HULL_MAX	(m_vObsHullMax)

	Vector(0, 0, 14)			//VEC_DEAD_VIEWHEIGHT (m_vDeadViewHeight)
);

IClientGameRules* g_pGameRules = NULL;

//static C_World *g_pClientWorld;


//void ClientWorldFactoryInit()
//{
//	g_pClientWorld = new C_World;
//}

//void ClientWorldFactoryShutdown()
//{
//	delete g_pClientWorld;
//	g_pClientWorld = NULL;
//}

//static IClientNetworkable* ClientWorldFactory( int entnum, int serialNum )
//{
//	Assert( g_pClientWorld != NULL );
//
//	g_pClientWorld->Init( entnum, serialNum );
//	return g_pClientWorld;
//}


IMPLEMENT_CLIENTCLASS_NO_FACTORY( C_World, DT_World, CWorld );//, ClientWorldFactory

BEGIN_RECV_TABLE( C_World, DT_World )
	RecvPropFloat(RECVINFO(m_flWaveHeight)),
	RecvPropVector(RECVINFO(m_WorldMins)),
	RecvPropVector(RECVINFO(m_WorldMaxs)),
	RecvPropInt(RECVINFO(m_bStartDark)),
	RecvPropFloat(RECVINFO(m_flMaxOccludeeArea)),
	RecvPropFloat(RECVINFO(m_flMinOccluderArea)),
	RecvPropFloat(RECVINFO(m_flMaxPropScreenSpaceWidth)),
	RecvPropFloat(RECVINFO(m_flMinPropScreenSpaceWidth)),
	RecvPropString(RECVINFO(m_iszDetailSpriteMaterial)),
	RecvPropInt(RECVINFO(m_bColdWorld)),
END_RECV_TABLE()


C_World::C_World( void )
{
	g_pGameRules = this;
}

C_World::~C_World( void )
{
	if (!g_pGameRules) {
		Error("m_pGameRules not inited!\n");
	}
	g_pGameRules->LevelShutdownPostEntity();
	g_pGameRules = NULL;
}

bool C_World::Init( int entnum, int iSerialNum )
{
	m_flWaveHeight = 0.0f;
	if (!mdlcache->ActivityList_Inited()) {
		mdlcache->ActivityList_Init();
		mdlcache->EventList_Init();
		m_bActivityInitedByMe = true;
	}

	return BaseClass::Init( entnum, iSerialNum );
}

void C_World::UpdateOnRemove()
{
	if (m_bActivityInitedByMe) {
		mdlcache->ActivityList_Free();
		mdlcache->EventList_Free();
		m_bActivityInitedByMe = false;
	}
	//Term();
	BaseClass::UpdateOnRemove();
}

void C_World::PreDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PreDataUpdate( updateType );
}

void C_World::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	// Always force reset to normal mode upon receipt of world in new map
	if ( updateType == DATA_UPDATE_CREATED )
	{
		modemanager->SwitchMode( false, true );

		if ( m_bStartDark )
		{
			ScreenFade_t sf;
			memset( &sf, 0, sizeof( sf ) );
			sf.a = 255;
			sf.r = 0;
			sf.g = 0;
			sf.b = 0;
			sf.duration = (float)(1<<SCREENFADE_FRACBITS) * 5.0f;
			sf.holdTime = (float)(1<<SCREENFADE_FRACBITS) * 1.0f;
			sf.fadeFlags = FFADE_IN | FFADE_PURGE;
			vieweffects->Fade( sf );
		}

		OcclusionParams_t params;
		params.m_flMaxOccludeeArea = m_flMaxOccludeeArea;
		params.m_flMinOccluderArea = m_flMinOccluderArea;
		engine->SetOcclusionParameters( params );

		modelinfo->SetLevelScreenFadeRange( m_flMinPropScreenSpaceWidth, m_flMaxPropScreenSpaceWidth );
	}
}

void C_World::RegisterSharedActivities( void )
{
	ActivityList_RegisterSharedActivities();
	EventList_RegisterSharedEvents();
}

// -----------------------------------------
//	Sprite Index info
// -----------------------------------------
short		g_sModelIndexLaser;			// holds the index for the laser beam
const char	*g_pModelNameLaser = "sprites/laserbeam.vmt";
short		g_sModelIndexLaserDot;		// holds the index for the laser beam dot
short		g_sModelIndexFireball;		// holds the index for the fireball
short		g_sModelIndexSmoke;			// holds the index for the smoke cloud
short		g_sModelIndexWExplosion;	// holds the index for the underwater explosion
short		g_sModelIndexBubbles;		// holds the index for the bubbles model
short		g_sModelIndexBloodDrop;		// holds the sprite index for the initial blood
short		g_sModelIndexBloodSpray;	// holds the sprite index for splattered blood

//-----------------------------------------------------------------------------
// Purpose: Precache global weapon sounds
//-----------------------------------------------------------------------------
void W_Precache(void)
{
	PrecacheFileWeaponInfoDatabase( filesystem, g_pGameRules->GetEncryptionKey() );

	g_sModelIndexFireball = modelinfo->GetModelIndex ("sprites/zerogxplode.vmt");// fireball
	g_sModelIndexWExplosion = modelinfo->GetModelIndex ("sprites/WXplo1.vmt");// underwater fireball
	g_sModelIndexSmoke = modelinfo->GetModelIndex ("sprites/steam1.vmt");// smoke
	g_sModelIndexBubbles = modelinfo->GetModelIndex ("sprites/bubble.vmt");//bubbles
	g_sModelIndexBloodSpray = modelinfo->GetModelIndex ("sprites/bloodspray.vmt"); // initial blood
	g_sModelIndexBloodDrop = modelinfo->GetModelIndex ("sprites/blood.vmt"); // splattered blood 
	g_sModelIndexLaser = modelinfo->GetModelIndex( (char *)g_pModelNameLaser );
	g_sModelIndexLaserDot = modelinfo->GetModelIndex("sprites/laserdot.vmt");
}

void C_World::Precache( void )
{
	// UNDONE: Make most of these things server systems or precache_registers
	// =================================================
	//	Activities
	// =================================================
	if (m_bActivityInitedByMe) {
		mdlcache->ActivityList_Clear();
		mdlcache->EventList_Clear();

		RegisterSharedActivities();
	}

	EntityList()->LevelInitPreEntity();
	IGameSystem::LevelInitPreEntityAllSystems();//pMapName

	// Get weapon precaches
	W_Precache();	

	// Call all registered precachers.
	CPrecacheRegister::Precache();
	// NVNT notify system of precache
	if (haptics)
		haptics->WorldPrecache();
}

void C_World::Spawn( void )
{
	Precache();
}



C_World *GetClientWorldEntity()
{
	//Assert( g_pClientWorld != NULL );
	return (C_World*)EntityList()->GetBaseEntity(0);
}

// Level init, shutdown
void C_World::LevelInitPreEntity()
{

}

void C_World::LevelInitPostEntity()
{

}

// The level is shutdown in two parts
void C_World::LevelShutdownPreEntity()
{

}

void C_World::LevelShutdownPostEntity()
{

}

bool C_World::IsBonusChallengeTimeBased(void)
{
	return true;
}

bool C_World::IsLocalPlayer(int nEntIndex)
{
	C_BasePlayer* pLocalPlayer = (C_BasePlayer*)EntityList()->GetLocalPlayer();
	return (pLocalPlayer && pLocalPlayer == EntityList()->GetEnt(nEntIndex));
}

bool C_World::SwitchToNextBestWeapon(CBaseCombatCharacter* pPlayer, CBaseCombatWeapon* pCurrentWeapon)
{
	return false;
}

CBaseCombatWeapon* C_World::GetNextBestWeapon(CBaseCombatCharacter* pPlayer, CBaseCombatWeapon* pCurrentWeapon)
{
	return NULL;
}

bool C_World::ShouldCollide(int collisionGroup0, int collisionGroup1)
{
	if (collisionGroup0 > collisionGroup1)
	{
		// swap so that lowest is always first
		::V_swap(collisionGroup0, collisionGroup1);
	}

#ifndef HL2MP
	if ((collisionGroup0 == COLLISION_GROUP_PLAYER || collisionGroup0 == COLLISION_GROUP_PLAYER_MOVEMENT) &&
		collisionGroup1 == COLLISION_GROUP_PUSHAWAY)
	{
		return false;
	}
#endif

	if (collisionGroup0 == COLLISION_GROUP_DEBRIS && collisionGroup1 == COLLISION_GROUP_PUSHAWAY)
	{
		// let debris and multiplayer objects collide
		return true;
	}

	// --------------------------------------------------------------------------
	// NOTE: All of this code assumes the collision groups have been sorted!!!!
	// NOTE: Don't change their order without rewriting this code !!!
	// --------------------------------------------------------------------------

	// Don't bother if either is in a vehicle...
	if ((collisionGroup0 == COLLISION_GROUP_IN_VEHICLE) || (collisionGroup1 == COLLISION_GROUP_IN_VEHICLE))
		return false;

	if ((collisionGroup1 == COLLISION_GROUP_DOOR_BLOCKER) && (collisionGroup0 != COLLISION_GROUP_NPC))
		return false;

	if ((collisionGroup0 == COLLISION_GROUP_PLAYER) && (collisionGroup1 == COLLISION_GROUP_PASSABLE_DOOR))
		return false;

	if (collisionGroup0 == COLLISION_GROUP_DEBRIS || collisionGroup0 == COLLISION_GROUP_DEBRIS_TRIGGER)
	{
		// put exceptions here, right now this will only collide with COLLISION_GROUP_NONE
		return false;
	}

	// Dissolving guys only collide with COLLISION_GROUP_NONE
	if ((collisionGroup0 == COLLISION_GROUP_DISSOLVING) || (collisionGroup1 == COLLISION_GROUP_DISSOLVING))
	{
		if (collisionGroup0 != COLLISION_GROUP_NONE)
			return false;
	}

	// doesn't collide with other members of this group
	// or debris, but that's handled above
	if (collisionGroup0 == COLLISION_GROUP_INTERACTIVE_DEBRIS && collisionGroup1 == COLLISION_GROUP_INTERACTIVE_DEBRIS)
		return false;

#ifndef HL2MP
	// This change was breaking HL2DM
	// Adrian: TEST! Interactive Debris doesn't collide with the player.
	if (collisionGroup0 == COLLISION_GROUP_INTERACTIVE_DEBRIS && (collisionGroup1 == COLLISION_GROUP_PLAYER_MOVEMENT || collisionGroup1 == COLLISION_GROUP_PLAYER))
		return false;
#endif

	if (collisionGroup0 == COLLISION_GROUP_BREAKABLE_GLASS && collisionGroup1 == COLLISION_GROUP_BREAKABLE_GLASS)
		return false;

	// interactive objects collide with everything except debris & interactive debris
	if (collisionGroup1 == COLLISION_GROUP_INTERACTIVE && collisionGroup0 != COLLISION_GROUP_NONE)
		return false;

	// Projectiles hit everything but debris, weapons, + other projectiles
	if (collisionGroup1 == COLLISION_GROUP_PROJECTILE)
	{
		if (collisionGroup0 == COLLISION_GROUP_DEBRIS ||
			collisionGroup0 == COLLISION_GROUP_WEAPON ||
			collisionGroup0 == COLLISION_GROUP_PROJECTILE)
		{
			return false;
		}
	}

	// Don't let vehicles collide with weapons
	// Don't let players collide with weapons...
	// Don't let NPCs collide with weapons
	// Weapons are triggers, too, so they should still touch because of that
	if (collisionGroup1 == COLLISION_GROUP_WEAPON)
	{
		if (collisionGroup0 == COLLISION_GROUP_VEHICLE ||
			collisionGroup0 == COLLISION_GROUP_PLAYER ||
			collisionGroup0 == COLLISION_GROUP_NPC)
		{
			return false;
		}
	}

	// collision with vehicle clip entity??
	if (collisionGroup0 == COLLISION_GROUP_VEHICLE_CLIP || collisionGroup1 == COLLISION_GROUP_VEHICLE_CLIP)
	{
		// yes then if it's a vehicle, collide, otherwise no collision
		// vehicle sorts lower than vehicle clip, so must be in 0
		if (collisionGroup0 == COLLISION_GROUP_VEHICLE)
			return true;
		// vehicle clip against non-vehicle, no collision
		return false;
	}

	return true;
}


const CViewVectors* C_World::GetViewVectors() const
{
	return &g_DefaultViewVectors;
}


//-----------------------------------------------------------------------------
// Purpose: Returns how much damage the given ammo type should do to the victim
//			when fired by the attacker.
// Input  : pAttacker - Dude what shot the gun.
//			pVictim - Dude what done got shot.
//			nAmmoType - What been shot out.
// Output : How much hurt to put on dude what done got shot (pVictim).
//-----------------------------------------------------------------------------
float C_World::GetAmmoDamage(CBaseEntity* pAttacker, CBaseEntity* pVictim, int nAmmoType)
{
	float flDamage = 0;
	CAmmoDef* pAmmoDef = GetAmmoDef();

	if (pAttacker->IsPlayer())
	{
		flDamage = pAmmoDef->PlrDamage(nAmmoType);
	}
	else
	{
		flDamage = pAmmoDef->NPCDamage(nAmmoType);
	}

	return flDamage;
}