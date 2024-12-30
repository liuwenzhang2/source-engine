//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef GAMERULES_H
#define GAMERULES_H
#ifdef _WIN32
#pragma once
#endif

#include "platform.h"

// Debug history should be disabled in release builds
//#define DISABLE_DEBUG_HISTORY	

//#include "items.h"
class CBaseCombatWeapon;
class CBaseCombatCharacter;
class CBasePlayer;
class CItem;
class CAmmoDef;
class CTacticalMissionManager;
class CViewVectors;

// Autoaiming modes
enum
{
	AUTOAIM_NONE = 0,		// No autoaim at all.
	AUTOAIM_ON,				// Autoaim is on.
	AUTOAIM_ON_CONSOLE,		// Autoaim is on, including enhanced features for Console gaming (more assistance, etc)
};

// weapon respawning return codes
enum
{	
	GR_NONE = 0,
	
	GR_WEAPON_RESPAWN_YES,
	GR_WEAPON_RESPAWN_NO,
	
	GR_AMMO_RESPAWN_YES,
	GR_AMMO_RESPAWN_NO,
	
	GR_ITEM_RESPAWN_YES,
	GR_ITEM_RESPAWN_NO,

	GR_PLR_DROP_GUN_ALL,
	GR_PLR_DROP_GUN_ACTIVE,
	GR_PLR_DROP_GUN_NO,

	GR_PLR_DROP_AMMO_ALL,
	GR_PLR_DROP_AMMO_ACTIVE,
	GR_PLR_DROP_AMMO_NO,
};

// Player relationship return codes
enum
{
	GR_NOTTEAMMATE = 0,
	GR_TEAMMATE,
	GR_ENEMY,
	GR_ALLY,
	GR_NEUTRAL,
};

abstract_class IGameRules
{
public:
	// Level init, shutdown
	virtual void	LevelInitPreEntity() = 0;
	virtual void	LevelInitPostEntity() = 0;
	// The level is shutdown in two parts
	virtual void	LevelShutdownPreEntity() = 0;
	virtual void	LevelShutdownPostEntity() = 0;
	// Damage Queries - these need to be implemented by the various subclasses (single-player, multi-player, etc).
	// The queries represent queries against damage types and properties.
	virtual bool	Damage_IsTimeBased( int iDmgType ) = 0;			// Damage types that are time-based.
	virtual bool	Damage_ShouldGibCorpse( int iDmgType ) = 0;		// Damage types that gib the corpse.
	virtual bool	Damage_ShowOnHUD( int iDmgType ) = 0;			// Damage types that have client HUD art.
	virtual bool	Damage_NoPhysicsForce( int iDmgType ) = 0;		// Damage types that don't have to supply a physics force & position.
	virtual bool	Damage_ShouldNotBleed( int iDmgType ) = 0;		// Damage types that don't make the player bleed.
	//Temp: These will go away once DamageTypes become enums.
	virtual int		Damage_GetTimeBased( void ) = 0;				// Actual bit-fields.
	virtual int		Damage_GetShouldGibCorpse( void ) = 0;
	virtual int		Damage_GetShowOnHud( void ) = 0;					
	virtual int		Damage_GetNoPhysicsForce( void )= 0;
	virtual int		Damage_GetShouldNotBleed( void ) = 0;

// Ammo Definitions
	//CAmmoDef* GetAmmoDef();

	virtual bool SwitchToNextBestWeapon( CBaseCombatCharacter *pPlayer, CBaseCombatWeapon *pCurrentWeapon ) = 0; // Switch to the next best weapon
	virtual CBaseCombatWeapon *GetNextBestWeapon( CBaseCombatCharacter *pPlayer, CBaseCombatWeapon *pCurrentWeapon ) = 0; // I can't use this weapon anymore, get me the next best one.
	virtual bool ShouldCollide( int collisionGroup0, int collisionGroup1 ) = 0;
	virtual int DefaultFOV(void) = 0;
	// Get the view vectors for this mod.
	virtual const CViewVectors* GetViewVectors() const = 0;
// Damage rules for ammo types
	virtual float GetAmmoDamage( CBaseEntity *pAttacker, CBaseEntity *pVictim, int nAmmoType ) = 0;
	virtual float GetDamageMultiplier(void) = 0;
// Functions to verify the single/multiplayer status of a game
	virtual bool IsMultiplayer( void ) = 0;// is this a multiplayer game? (either coop or deathmatch)
	virtual const unsigned char* GetEncryptionKey() = 0;
	virtual bool InRoundRestart(void) = 0;
	//Allow thirdperson camera.
	virtual bool AllowThirdPersonCamera(void) = 0;
	virtual void ClientCommandKeyValues(int pEntity, KeyValues* pKeyValues) = 0;
	// IsConnectedUserInfoChangeAllowed allows the clients to change
	// cvars with the FCVAR_NOT_CONNECTED rule if it returns true
	virtual bool IsConnectedUserInfoChangeAllowed(CBasePlayer* pPlayer) = 0;
	virtual const char* GetGameTypeName(void) = 0;
	virtual int GetGameType(void) = 0;
	virtual bool ShouldDrawHeadLabels() = 0;
	virtual void ClientSpawned(int  pPlayer) = 0;
	virtual void OnFileReceived(const char* fileName, unsigned int transferID) = 0;
	virtual bool IsHolidayActive( /*EHoliday*/ int eHoliday) const = 0;

};

class IServerGameRules : public IGameRules
{
public:
	virtual void GetTaggedConVarList(KeyValues* pCvarTagList) = 0;
	// NVNT see if the client of the player entered is using a haptic device.
	virtual void CheckHaptics(CBasePlayer* pPlayer) = 0;
	// Called when game rules are destroyed by CWorld
	virtual void LevelShutdown(void) = 0;
	virtual void Precache(void) = 0;
	virtual void RefreshSkillData( bool forceUpdate ) = 0;// fill skill data struct with proper values
	virtual void FrameUpdatePreEntityThink() = 0;
	// Called each frame. This just forwards the call to Think().
	virtual void FrameUpdatePostEntityThink() = 0;
	virtual void Think( void ) = 0;// GR_Think - runs every server frame, should handle any timer tasks, periodic events, etc.
	virtual bool IsAllowedToSpawn( CBaseEntity *pEntity ) = 0;  // Can this item spawn (eg NPCs don't spawn in deathmatch).
	// Called at the end of GameFrame (i.e. after all game logic has run this frame)
	virtual void EndGameFrame( void ) = 0;
	virtual bool IsSkillLevel(int iLevel) = 0;
	virtual int	GetSkillLevel() = 0;
	virtual void OnSkillLevelChanged(int iNewLevel) = 0;
	virtual void SetSkillLevel(int iLevel) = 0;
	virtual bool FAllowFlashlight( void ) = 0;// Are players allowed to switch on their flashlight?
	virtual bool FShouldSwitchWeapon( CBasePlayer *pPlayer, CBaseCombatWeapon *pWeapon ) = 0;// should the player switch to this weapon?
// Functions to verify the single/multiplayer status of a game
	virtual bool IsDeathmatch( void ) = 0;//is this a deathmatch game?
	virtual bool IsTeamplay(void) = 0;// is this deathmatch game being played with team rules?
	virtual bool IsCoOp( void ) = 0;// is this a coop game?
	virtual const char* GetGameDescription(void) = 0;  // this is the game name that gets seen in the server browser
// Client connection/disconnection
	virtual bool ClientConnected( int pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen ) = 0;// a client just connected to the server (player hasn't spawned yet)
	virtual void InitHUD( CBasePlayer *pl ) = 0;		// the client dll is ready for updating
	virtual void ClientDisconnected( int pClient ) = 0;// a client just disconnected from the server
// Client damage rules
	virtual float FlPlayerFallDamage( CBasePlayer *pPlayer ) = 0;// this client just hit the ground after a fall. How much damage?
	virtual bool  FPlayerCanTakeDamage(CBasePlayer* pPlayer, CBaseEntity* pAttacker, const CTakeDamageInfo& info) = 0;;// can this player take damage from this attacker?
	virtual bool ShouldAutoAim(CBasePlayer* pPlayer, CBaseEntity* target) = 0;
	virtual float GetAutoAimScale(CBasePlayer* pPlayer) = 0;
	virtual int	GetAutoAimMode() = 0;
	virtual bool ShouldUseRobustRadiusDamage(CBaseEntity* pEntity) = 0;
	virtual void  RadiusDamage( const CTakeDamageInfo &info, const Vector &vecSrc, float flRadius, int iClassIgnore, CBaseEntity *pEntityIgnore ) = 0;
	// Let the game rules specify if fall death should fade screen to black
	virtual bool  FlPlayerFallDeathDoesScreenFade(CBasePlayer* pl) = 0;
	virtual bool AllowDamage( CBaseEntity *pVictim, const CTakeDamageInfo &info ) = 0;
// Client spawn/respawn control
	virtual void PlayerSpawn( CBasePlayer *pPlayer ) = 0;// called by CBasePlayer::Spawn just before releasing player into the game
	virtual void PlayerThink( CBasePlayer *pPlayer ) = 0; // called by CBasePlayer::PreThink every frame, before physics are run and after keys are accepted
	virtual bool FPlayerCanRespawn( CBasePlayer *pPlayer ) = 0;// is this player allowed to respawn now?
	virtual float FlPlayerSpawnTime( CBasePlayer *pPlayer ) = 0;// When in the future will this player be able to spawn?
	virtual CBaseEntity *GetPlayerSpawnSpot( CBasePlayer *pPlayer ) = 0;// Place this player on their spawnspot and face them the proper direction.
	virtual bool IsSpawnPointValid( CBaseEntity *pSpot, CBasePlayer *pPlayer ) = 0;
	virtual bool AllowAutoTargetCrosshair(void) = 0;
	virtual bool ClientCommand( CBaseEntity *pEdict, const CCommand &args ) = 0;  // handles the user commands;  returns TRUE if command handled properly
	virtual void ClientSettingsChanged( CBasePlayer *pPlayer ) = 0;		 // the player has changed cvars
// Client kills/scoring
	virtual int IPointsForKill( CBasePlayer *pAttacker, CBasePlayer *pKilled ) = 0;// how many points do I award whoever kills this player?
	virtual void PlayerKilled( CBasePlayer *pVictim, const CTakeDamageInfo &info ) = 0;// Called each time a player dies
	virtual void DeathNotice( CBasePlayer *pVictim, const CTakeDamageInfo &info )=  0;// Call this from within a GameRules class to report an obituary.
	virtual const char* GetDamageCustomString(const CTakeDamageInfo& info) = 0;
// Weapon Damage
	// Determines how much damage Player's attacks inflict, based on skill level.
	virtual float AdjustPlayerDamageInflicted(float damage) = 0;
	virtual void  AdjustPlayerDamageTaken(CTakeDamageInfo* pInfo) = 0; // Base class does nothing.
// Weapon retrieval
	virtual bool CanHavePlayerItem( CBasePlayer *pPlayer, CBaseCombatWeapon *pWeapon ) = 0;// The player is touching an CBaseCombatWeapon, do I give it to him?
// Weapon spawn/respawn control
	virtual int WeaponShouldRespawn( CBaseCombatWeapon *pWeapon ) = 0;// should this weapon respawn?
	virtual float FlWeaponRespawnTime( CBaseCombatWeapon *pWeapon ) = 0;// when may this weapon respawn?
	virtual float FlWeaponTryRespawn( CBaseCombatWeapon *pWeapon ) = 0; // can i respawn now,  and if not, when should i try again?
	virtual Vector VecWeaponRespawnSpot( CBaseCombatWeapon *pWeapon ) = 0;// where in the world should this weapon respawn?
// Item retrieval
	virtual bool CanHaveItem( CBasePlayer *pPlayer, CItem *pItem ) = 0;// is this player allowed to take this item?
	virtual void PlayerGotItem( CBasePlayer *pPlayer, CItem *pItem ) = 0;// call each time a player picks up an item (battery, healthkit)
// Item spawn/respawn control
	virtual int ItemShouldRespawn( CItem *pItem ) = 0;// Should this item respawn?
	virtual float FlItemRespawnTime( CItem *pItem ) = 0;// when may this item respawn?
	virtual Vector VecItemRespawnSpot( CItem *pItem ) = 0;// where in the world should this item respawn?
	virtual QAngle VecItemRespawnAngles( CItem *pItem ) = 0;// what angles should this item use when respawing?
// Ammo retrieval
	virtual bool CanHaveAmmo( CBaseCombatCharacter *pPlayer, int iAmmoIndex ) = 0; // can this player take more of this ammo?
	virtual bool CanHaveAmmo( CBaseCombatCharacter *pPlayer, const char *szName ) = 0;
	virtual void PlayerGotAmmo( CBaseCombatCharacter *pPlayer, char *szName, int iCount ) = 0;// called each time a player picks up some ammo in the world
	virtual float GetAmmoQuantityScale(int iAmmoIndex) = 0;
// AI Definitions
	virtual void InitDefaultAIRelationships(void) = 0;
	virtual const char* AIClassText(int classType) = 0;
// Healthcharger respawn control
	virtual float FlHealthChargerRechargeTime( void ) = 0;// how long until a depleted HealthCharger recharges itself?
	virtual float FlHEVChargerRechargeTime(void) = 0;// how long until a depleted HealthCharger recharges itself?
// What happens to a dead player's weapons
	virtual int DeadPlayerWeapons( CBasePlayer *pPlayer ) = 0;// what do I do with a player's weapons when he's killed?
// What happens to a dead player's ammo	
	virtual int DeadPlayerAmmo( CBasePlayer *pPlayer ) = 0;// Do I drop ammo when the player dies? How much?
// Teamplay stuff
	virtual const char *GetTeamID( CBaseEntity *pEntity ) = 0;// what team is this entity on?
	virtual int PlayerRelationship( CBaseEntity *pPlayer, CBaseEntity *pTarget ) = 0;// What is the player's relationship with this entity?
	virtual bool PlayerCanHearChat( CBasePlayer *pListener, CBasePlayer *pSpeaker ) = 0;
	virtual void CheckChatText(CBasePlayer* pPlayer, char* pText) = 0;
	virtual int GetTeamIndex(const char* pTeamName) = 0;
	virtual const char* GetIndexedTeamName(int teamIndex) = 0;
	virtual bool IsValidTeam(const char* pTeamName) = 0;
	virtual void ChangePlayerTeam(CBasePlayer* pPlayer, const char* pTeamName, bool bKill, bool bGib) = 0;
	virtual const char* SetDefaultPlayerTeam(CBasePlayer* pPlayer) = 0;
	virtual void UpdateClientData(CBasePlayer* pPlayer) = 0;
// Sounds
	virtual bool PlayTextureSounds(void) = 0;
	virtual bool PlayFootstepSounds(CBasePlayer* pl) = 0;
// NPCs
	virtual bool FAllowNPCs( void ) = 0;//are NPCs allowed
	// Immediately end a multiplayer game
	virtual void EndMultiplayerGame(void) = 0;
	// trace line rules
	virtual float WeaponTraceEntity( CBaseEntity *pEntity, const Vector &vecStart, const Vector &vecEnd, unsigned int mask, trace_t *ptr ) = 0;
	// Setup g_pPlayerResource (some mods use a different entity type here).
	virtual void CreateStandardEntities() = 0;
	// Team name, etc shown in chat and dedicated server console
	virtual const char *GetChatPrefix( bool bTeamOnly, CBasePlayer *pPlayer ) = 0;
	// Location name shown in chat
	virtual const char* GetChatLocation(bool bTeamOnly, CBasePlayer* pPlayer) = 0;
	// VGUI format string for chat, if desired
	virtual const char* GetChatFormat(bool bTeamOnly, CBasePlayer* pPlayer) = 0;
	// Whether props that are on fire should get a DLIGHT.
	virtual bool ShouldBurningPropsEmitLight() = 0;
	virtual bool CanEntityBeUsePushed(CBaseEntity* pEnt) = 0;
	//virtual void CreateCustomNetworkStringTables(void) = 0;
	// Game Achievements (server version)
	virtual void MarkAchievement ( IRecipientFilter& filter, char const *pchAchievementName ) = 0;
	virtual void ResetMapCycleTimeStamp(void) = 0;
	virtual void OnNavMeshLoad(void) = 0;
	// game-specific factories
	virtual CTacticalMissionManager *TacticalMissionManagerFactory( void ) = 0;
	virtual void ProcessVerboseLogOutput(void) = 0;
	virtual bool	MegaPhyscannonActive(void) = 0;
};

class IClientGameRules : public IGameRules
{
public:
	virtual bool IsBonusChallengeTimeBased(void) = 0;
	virtual bool AllowMapParticleEffect(const char* pszParticleEffect) = 0;
	virtual bool AllowWeatherParticles(void) = 0;
	virtual bool AllowMapVisionFilterShaders(void) = 0;
	virtual const char* TranslateEffectForVisionFilter(const char* pchEffectType, const char* pchEffectName) = 0;
	virtual bool IsLocalPlayer(int nEntIndex) = 0;
	virtual void ModifySentChat(char* pBuf, int iBufSize) = 0;
	virtual bool ShouldWarnOfAbandonOnQuit() = 0;
};

#endif // GAMERULES_H
