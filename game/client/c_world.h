//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#if !defined( C_WORLD_H )
#define C_WORLD_H
#ifdef _WIN32
#pragma once
#endif

#include "gamerules.h"
#include "c_baseentity.h"

#if defined( CLIENT_DLL )
#define CWorld C_World
#endif

extern ConVar g_Language;
extern ConVar sk_autoaim_mode;

class C_World : public C_BaseEntity, public IClientGameRules
{
public:
	DECLARE_CLASS( C_World, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	C_World( void );
	~C_World( void );
	
	// Override the factory create/delete functions since the world is a singleton.
	virtual bool Init( int entnum, int iSerialNum );
	virtual void UpdateOnRemove();

	virtual void Precache();
	virtual void Spawn();

	// Don't worry about adding the world to the collision list; it's already there
	virtual CollideType_t	GetCollideType( void )	{ return ENTITY_SHOULD_NOT_COLLIDE; }

	virtual void OnDataChanged( DataUpdateType_t updateType );
	virtual void PreDataUpdate( DataUpdateType_t updateType );
	virtual void PostDataUpdate(DataUpdateType_t updateType);

	float GetWaveHeight() const;
	const char *GetDetailSpriteMaterial() const;

	//gamerule
	virtual void LevelInitPreEntity();
	virtual void LevelInitPostEntity();

	// The level is shutdown in two parts
	virtual void LevelShutdownPreEntity();

	virtual void LevelShutdownPostEntity();

	// Damage Queries - these need to be implemented by the various subclasses (single-player, multi-player, etc).
// The queries represent queries against damage types and properties.
	virtual bool	Damage_IsTimeBased(int iDmgType) = 0;			// Damage types that are time-based.
	virtual bool	Damage_ShouldGibCorpse(int iDmgType) = 0;		// Damage types that gib the corpse.
	virtual bool	Damage_ShowOnHUD(int iDmgType) = 0;			// Damage types that have client HUD art.
	virtual bool	Damage_NoPhysicsForce(int iDmgType) = 0;		// Damage types that don't have to supply a physics force & position.
	virtual bool	Damage_ShouldNotBleed(int iDmgType) = 0;		// Damage types that don't make the player bleed.
	//Temp: These will go away once DamageTypes become enums.
	virtual int		Damage_GetTimeBased(void) = 0;				// Actual bit-fields.
	virtual int		Damage_GetShouldGibCorpse(void) = 0;
	virtual int		Damage_GetShowOnHud(void) = 0;
	virtual int		Damage_GetNoPhysicsForce(void) = 0;
	virtual int		Damage_GetShouldNotBleed(void) = 0;

	// Ammo Definitions
		//CAmmoDef* GetAmmoDef();

	virtual bool SwitchToNextBestWeapon(CBaseCombatCharacter* pPlayer, CBaseCombatWeapon* pCurrentWeapon); // Switch to the next best weapon
	virtual CBaseCombatWeapon* GetNextBestWeapon(CBaseCombatCharacter* pPlayer, CBaseCombatWeapon* pCurrentWeapon); // I can't use this weapon anymore, get me the next best one.
	virtual bool ShouldCollide(int collisionGroup0, int collisionGroup1);

	virtual int DefaultFOV(void) { return 90; }

	// Get the view vectors for this mod.
	virtual const CViewVectors* GetViewVectors() const;

	// Damage rules for ammo types
	virtual float GetAmmoDamage(CBaseEntity* pAttacker, CBaseEntity* pVictim, int nAmmoType);
	virtual float GetDamageMultiplier(void) { return 1.0f; }

	// Functions to verify the single/multiplayer status of a game
	virtual bool IsMultiplayer(void) = 0;// is this a multiplayer game? (either coop or deathmatch)

	virtual const unsigned char* GetEncryptionKey() { return NULL; }

	virtual bool InRoundRestart(void) { return false; }

	//Allow thirdperson camera.
	virtual bool AllowThirdPersonCamera(void) { return false; }

	virtual void ClientCommandKeyValues(int pEntity, KeyValues* pKeyValues) {}

	// IsConnectedUserInfoChangeAllowed allows the clients to change
	// cvars with the FCVAR_NOT_CONNECTED rule if it returns true
	virtual bool IsConnectedUserInfoChangeAllowed(CBasePlayer* pPlayer)
	{
		Assert(!IsMultiplayer());
		return true;
	}

	virtual bool IsBonusChallengeTimeBased(void);

	virtual bool AllowMapParticleEffect(const char* pszParticleEffect) { return true; }

	virtual bool AllowWeatherParticles(void) { return true; }

	virtual bool AllowMapVisionFilterShaders(void) { return false; }
	virtual const char* TranslateEffectForVisionFilter(const char* pchEffectType, const char* pchEffectName) { return pchEffectName; }

	virtual bool IsLocalPlayer(int nEntIndex);

	virtual void ModifySentChat(char* pBuf, int iBufSize) { return; }

	virtual bool ShouldWarnOfAbandonOnQuit() { return false; }

	virtual const char* GetGameTypeName(void) { return NULL; }
	virtual int GetGameType(void) { return 0; }

	virtual bool ShouldDrawHeadLabels() { return true; }

	virtual void ClientSpawned(int  pPlayer) { return; }

	virtual void OnFileReceived(const char* fileName, unsigned int transferID) { return; }

	virtual bool IsHolidayActive( /*EHoliday*/ int eHoliday) const { return false; }

public:
	enum
	{
		MAX_DETAIL_SPRITE_MATERIAL_NAME_LENGTH = 256,
	};

	float	m_flWaveHeight;
	Vector	m_WorldMins;
	Vector	m_WorldMaxs;
	bool	m_bStartDark;
	float	m_flMaxOccludeeArea;
	float	m_flMinOccluderArea;
	float	m_flMinPropScreenSpaceWidth;
	float	m_flMaxPropScreenSpaceWidth;
	bool	m_bColdWorld;
	bool	m_bActivityInitedByMe = false;
private:
	void	RegisterSharedActivities( void );
	char	m_iszDetailSpriteMaterial[MAX_DETAIL_SPRITE_MATERIAL_NAME_LENGTH];
};

inline float C_World::GetWaveHeight() const
{
	return m_flWaveHeight;
}

inline const char *C_World::GetDetailSpriteMaterial() const
{
	return m_iszDetailSpriteMaterial;
}

//void ClientWorldFactoryInit();
//void ClientWorldFactoryShutdown();
C_World* GetClientWorldEntity();

#endif // C_WORLD_H