//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

#include "decals.h"
#include "effect_dispatch_data.h"
#include "model_types.h"
#include "gamestringpool.h"
#include "ammodef.h"
#include "takedamageinfo.h"
#include "shot_manipulator.h"
#include "ai_debug_shared.h"
#include "mapentities_shared.h"
#include "debugoverlay_shared.h"
#include "coordsize.h"
#include "vphysics/performance.h"
#include "isaverestore.h"
#include "saverestoretypes.h"

#ifdef CLIENT_DLL
	#include "c_te_effect_dispatch.h"
#else
	#include "te_effect_dispatch.h"
	#include "soundent.h"
	#include "game/server/iservervehicle.h"
	#include "player_pickup.h"
	#include "waterbullet.h"
	#include "func_break.h"

#ifdef HL2MP
	#include "te_hl2mp_shotgun_shot.h"
#endif

	#include "gamestats.h"

#endif

#ifdef HL2_EPISODIC
ConVar hl2_episodic( "hl2_episodic", "1", FCVAR_REPLICATED );
#else
ConVar hl2_episodic( "hl2_episodic", "0", FCVAR_REPLICATED );
#endif//HL2_EPISODIC

#ifdef PORTAL
	#include "prop_portal_shared.h"
#endif

#ifdef TF_DLL
#include "tf_gamerules.h"
#include "tf_weaponbase.h"
#endif // TF_DLL

#include "rumble_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef GAME_DLL
	ConVar ent_debugkeys( "ent_debugkeys", "" );
	extern bool ParseKeyvalue( void *pObject, typedescription_t *pFields, int iNumFields, const char *szKeyName, const char *szValue );
	extern bool ExtractKeyvalue( void *pObject, typedescription_t *pFields, int iNumFields, const char *szKeyName, char *szValue, int iMaxLen );
#endif
	extern ISoundEmitterSystemBase* soundemitterbase;

//class CEngineObjectSaveDataOps : public CDefSaveRestoreOps
//{
//	// saves the entire array of variables
//	virtual void Save(const SaveRestoreFieldInfo_t& fieldInfo, ISave* pSave)
//	{
//		CBaseEntity* pEntity = (CBaseEntity*)pSave->GetGameSaveRestoreInfo()->GetCurrentEntityContext();
//		if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_vecOrigin")) {
//			Vector verOrigin = pEntity->GetLocalOrigin();
//			pSave->WriteVector(&verOrigin, fieldInfo.pTypeDesc->fieldSize);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_angRotation")) {
//			QAngle angel = pEntity->GetLocalAngles();
//			pSave->WriteVector((Vector*)&angel, fieldInfo.pTypeDesc->fieldSize);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_vecVelocity")) {
//			Vector verOrigin = pEntity->GetLocalVelocity();
//			pSave->WriteVector(&verOrigin, fieldInfo.pTypeDesc->fieldSize);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_vecAbsOrigin")) {
//			Vector verOrigin = pEntity->GetAbsOrigin();
//			pSave->WritePositionVector(&verOrigin, fieldInfo.pTypeDesc->fieldSize);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_angAbsRotation")) {
//			QAngle angel = pEntity->GetAbsAngles();
//			pSave->WriteVector((Vector*)&angel, fieldInfo.pTypeDesc->fieldSize);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_vecAbsVelocity")) {
//			Vector verOrigin = pEntity->GetAbsVelocity();
//			pSave->WritePositionVector(&verOrigin, fieldInfo.pTypeDesc->fieldSize);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_hMoveParent")) {
//			CBaseEntity* pMoveParent = pEntity->GetEngineObject()->GetMoveParent()? pEntity->GetEngineObject()->GetMoveParent()->GetOuter():NULL;
//			EHANDLE Handle = pMoveParent ? pMoveParent->GetRefEHandle() : INVALID_EHANDLE_INDEX;
//			pSave->WriteEHandle(&Handle, fieldInfo.pTypeDesc->fieldSize);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_hMoveChild")) {
//			CBaseEntity* pMoveChild = pEntity->GetEngineObject()->FirstMoveChild() ? pEntity->GetEngineObject()->FirstMoveChild()->GetOuter() : NULL;
//			EHANDLE Handle = pMoveChild ? pMoveChild->GetRefEHandle() : INVALID_EHANDLE_INDEX;
//			pSave->WriteEHandle(&Handle, fieldInfo.pTypeDesc->fieldSize);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_hMovePeer")) {
//			CBaseEntity* pMovePeer = pEntity->GetEngineObject()->NextMovePeer() ? pEntity->GetEngineObject()->NextMovePeer()->GetOuter() : NULL;
//			EHANDLE Handle = pMovePeer ? pMovePeer->GetRefEHandle() : INVALID_EHANDLE_INDEX;
//			pSave->WriteEHandle(&Handle, fieldInfo.pTypeDesc->fieldSize);
//		}
//#ifdef GAME_DLL
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_iClassname")) {
//			string_t iClassname = pEntity->GetEngineObject()->GetClassname();
//			pSave->WriteString(&iClassname, fieldInfo.pTypeDesc->fieldSize);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_iGlobalname")) {
//			string_t iGlobalname = pEntity->GetEngineObject()->GetGlobalname();
//			pSave->WriteString(&iGlobalname, fieldInfo.pTypeDesc->fieldSize);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_iParent")) {
//			string_t iParentName = pEntity->GetEngineObject()->GetParentName();
//			pSave->WriteString(&iParentName, fieldInfo.pTypeDesc->fieldSize);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_iName")) {
//			string_t iEntityName = pEntity->GetEngineObject()->GetEntityName();
//			pSave->WriteString(&iEntityName, fieldInfo.pTypeDesc->fieldSize);
//		}
//#endif // GAME_DLL
//
//	}
//
//	// restores a single instance of the variable
//	virtual void Restore(const SaveRestoreFieldInfo_t& fieldInfo, IRestore* pRestore)
//	{
//		CBaseEntity* pEntity = (CBaseEntity*)pRestore->GetGameSaveRestoreInfo()->GetCurrentEntityContext();
//		if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_vecOrigin")) {
//			Vector vecOrigin;
//			pRestore->ReadVector(&vecOrigin, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->SetLocalOrigin(vecOrigin);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_angRotation")) {
//			QAngle angel;
//			pRestore->ReadVector((Vector*)&angel, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->SetLocalAngles(angel);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_vecVelocity")) {
//			Vector vecVelocity;
//			pRestore->ReadVector(&vecVelocity, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->SetLocalVelocity(vecVelocity);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_vecAbsOrigin")) {
//			Vector vecOrigin;
//			pRestore->ReadPositionVector(&vecOrigin, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->SetAbsOrigin(vecOrigin);
//		} else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_angAbsRotation")) {
//			QAngle angel;
//			pRestore->ReadVector((Vector*)&angel, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->SetAbsAngles(angel);
//		} else if(!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_vecAbsVelocity")) {
//			Vector vecVelocity;
//			pRestore->ReadVector(&vecVelocity, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->SetAbsVelocity(vecVelocity);
//		} 
//#ifdef GAME_DLL
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_hMoveParent")) {
//			EHANDLE handle;
//			pRestore->ReadEHandle(&handle, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->GetEngineObject()->SetMoveParent(handle);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_hMoveChild")) {
//			EHANDLE handle;
//			pRestore->ReadEHandle(&handle, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->GetEngineObject()->SetFirstMoveChild(handle);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_hMovePeer")) {
//			EHANDLE handle;
//			pRestore->ReadEHandle(&handle, fieldInfo.pTypeDesc->fieldSize, 0);
//			pEntity->GetEngineObject()->SetNextMovePeer(handle);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_iClassname")) {
//			char iClassname[512];
//			iClassname[0] = 0;
//			pRestore->ReadString(iClassname, sizeof(iClassname), 0);
//			pEntity->GetEngineObject()->SetClassname(iClassname);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_iGlobalname")) {
//			char iGlobalname[512];
//			iGlobalname[0] = 0;
//			pRestore->ReadString(iGlobalname, sizeof(iGlobalname), 0);
//			pEntity->GetEngineObject()->SetGlobalname(iGlobalname);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_iParent")) {
//			char iParentName[512];
//			iParentName[0] = 0;
//			pRestore->ReadString(iParentName, sizeof(iParentName), 0);
//			pEntity->GetEngineObject()->SetParentName(iParentName);
//		}
//		else if (!V_strcmp(fieldInfo.pTypeDesc->fieldName, "m_iName")) {
//			char iEntityName[512];
//			iEntityName[0] = 0;
//			pRestore->ReadString(iEntityName, sizeof(iEntityName), 0);
//			pEntity->GetEngineObject()->SetName(iEntityName);
//		}
//#endif // GAME_DLL
//	}
//
//
//	virtual bool IsEmpty(const SaveRestoreFieldInfo_t& fieldInfo)
//	{
//		return false;
//	}
//
//	virtual void MakeEmpty(const SaveRestoreFieldInfo_t& fieldInfo)
//	{
//		// Don't no how to. This is okay, since objects of this type
//		// are always born clean before restore, and not reused
//	}
//};

//CEngineObjectSaveDataOps g_EngineObjectSaveDataOps;
//ISaveRestoreOps* engineObjectFuncs = &g_EngineObjectSaveDataOps;


//bool CBaseEntity::m_bAllowPrecache = false;

// Set default max values for entities based on the existing constants from elsewhere
const float k_flMaxEntityPosCoord = MAX_COORD_FLOAT;
const float k_flMaxEntityEulerAngle = 360.0 * 1000.0f; // really should be restricted to +/-180, but some code doesn't adhere to this.  let's just trap NANs, etc
// Sometimes the resulting computed speeds are legitimately above the original
// constants; use bumped up versions for the downstream validation logic to
// account for this.
const float k_flMaxEntitySpeed = k_flMaxVelocity * 2.0f;
const float k_flMaxEntitySpinRate = k_flMaxAngularVelocity * 10.0f;

ConVar	ai_shot_bias_min( "ai_shot_bias_min", "-1.0", FCVAR_REPLICATED );
ConVar	ai_shot_bias_max( "ai_shot_bias_max", "1.0", FCVAR_REPLICATED );
ConVar	ai_debug_shoot_positions( "ai_debug_shoot_positions", "0", FCVAR_REPLICATED | FCVAR_CHEAT );

// Utility func to throttle rate at which the "reasonable position" spew goes out
static double s_LastEntityReasonableEmitTime;
bool CheckEmitReasonablePhysicsSpew()
{

	// Reported recently?
	double now = Plat_FloatTime();
	if ( now >= s_LastEntityReasonableEmitTime && now < s_LastEntityReasonableEmitTime + 5.0 )
	{
		// Already reported recently
		return false;
	}

	// Not reported recently.  Report it now
	s_LastEntityReasonableEmitTime = now;
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Spawn some blood particles
//-----------------------------------------------------------------------------
void SpawnBlood(Vector vecSpot, const Vector &vecDir, int bloodColor, float flDamage)
{
	UTIL_BloodDrips( vecSpot, vecDir, bloodColor, (int)flDamage );
}

//#if !defined( NO_ENTITY_PREDICTION )
////-----------------------------------------------------------------------------
//// The player drives simulation of this entity
////-----------------------------------------------------------------------------
//void CBaseEntity::SetPlayerSimulated( CBasePlayer *pOwner )
//{
//	m_bIsPlayerSimulated = true;
//	pOwner->AddToPlayerSimulationList( this );
//	m_hPlayerSimulationOwner = pOwner;
//}
//
//void CBaseEntity::UnsetPlayerSimulated( void )
//{
//	if ( m_hPlayerSimulationOwner != NULL )
//	{
//		m_hPlayerSimulationOwner->RemoveFromPlayerSimulationList( this );
//	}
//	m_hPlayerSimulationOwner = NULL;
//	m_bIsPlayerSimulated = false;
//}
//#endif

// position of eyes
Vector CBaseEntity::EyePosition( void )
{ 
	return GetEngineObject()->GetAbsOrigin() + GetViewOffset();
}

const QAngle &CBaseEntity::EyeAngles( void )
{
	return GetEngineObject()->GetAbsAngles();
}

const QAngle &CBaseEntity::LocalEyeAngles( void )
{
	return GetEngineObject()->GetLocalAngles();
}

// position of ears
Vector CBaseEntity::EarPosition( void )
{ 
	return EyePosition(); 
}

void CBaseEntity::SetViewOffset( const Vector& v ) 
{ 
	m_vecViewOffset = v; 
}

const Vector& CBaseEntity::GetViewOffset() const 
{ 
	return m_vecViewOffset; 
}


//-----------------------------------------------------------------------------
// center point of entity
//-----------------------------------------------------------------------------
const Vector &CBaseEntity::WorldSpaceCenter( ) const 
{
	return GetEngineObject()->WorldSpaceCenter();
}

void CBaseEntity::SetBlocksLOS( bool bBlocksLOS )
{
	if ( bBlocksLOS )
	{
		GetEngineObject()->RemoveEFlags( EFL_DONTBLOCKLOS );
	}
	else
	{
		GetEngineObject()->AddEFlags( EFL_DONTBLOCKLOS );
	}
}

bool CBaseEntity::BlocksLOS( void ) 
{ 
	return !GetEngineObject()->IsEFlagSet(EFL_DONTBLOCKLOS);
}

void CBaseEntity::SetAIWalkable( bool bBlocksLOS )
{
	if ( bBlocksLOS )
	{
		GetEngineObject()->RemoveEFlags( EFL_DONTWALKON );
	}
	else
	{
		GetEngineObject()->AddEFlags( EFL_DONTWALKON );
	}
}

bool CBaseEntity::IsAIWalkable( void ) 
{ 
	return !GetEngineObject()->IsEFlagSet(EFL_DONTWALKON);
}




//-----------------------------------------------------------------------------
// Parse data from a map file
//-----------------------------------------------------------------------------
bool CBaseEntity::KeyValue( const char *szKeyName, const char *szValue ) 
{
	//!! temp hack, until worldcraft is fixed
	// strip the # tokens from (duplicate) key names
	char *s = (char *)strchr( szKeyName, '#' );
	if ( s )
	{
		*s = '\0';
	}

	if ( FStrEq( szKeyName, "rendercolor" ) || FStrEq( szKeyName, "rendercolor32" ))
	{
		color32 tmp;
		UTIL_StringToColor32( &tmp, szValue );
		SetRenderColor( tmp.r, tmp.g, tmp.b );
		// don't copy alpha, legacy support uses renderamt
		return true;
	}
	
	if ( FStrEq( szKeyName, "renderamt" ) )
	{
		SetRenderColorA( atoi( szValue ) );
		return true;
	}

	if ( FStrEq( szKeyName, "disableshadows" ))
	{
		int val = atoi( szValue );
		if (val)
		{
			GetEngineObject()->AddEffects( EF_NOSHADOW );
		}
		return true;
	}

	if ( FStrEq( szKeyName, "mins" ))
	{
		Vector mins;
		UTIL_StringToVector( mins.Base(), szValue );
		GetEngineObject()->SetCollisionBounds( mins, GetEngineObject()->OBBMaxs() );
		return true;
	}

	if ( FStrEq( szKeyName, "maxs" ))
	{
		Vector maxs;
		UTIL_StringToVector( maxs.Base(), szValue );
		GetEngineObject()->SetCollisionBounds(GetEngineObject()->OBBMins(), maxs );
		return true;
	}

	if ( FStrEq( szKeyName, "disablereceiveshadows" ))
	{
		int val = atoi( szValue );
		if (val)
		{
			GetEngineObject()->AddEffects( EF_NORECEIVESHADOW );
		}
		return true;
	}

	if ( FStrEq( szKeyName, "nodamageforces" ))
	{
		int val = atoi( szValue );
		if (val)
		{
			GetEngineObject()->AddEFlags( EFL_NO_DAMAGE_FORCES );
		}
		return true;
	}

	// Fix up single angles
	//if( FStrEq( szKeyName, "angle" ) )
	//{
	//	static char szBuf[64];

	//	float y = atof( szValue );
	//	if (y >= 0)
	//	{
	//		Q_snprintf( szBuf,sizeof(szBuf), "%f %f %f", GetLocalAngles()[0], y, GetLocalAngles()[2] );
	//	}
	//	else if ((int)y == -1)
	//	{
	//		Q_strncpy( szBuf, "-90 0 0", sizeof(szBuf) );
	//	}
	//	else
	//	{
	//		Q_strncpy( szBuf, "90 0 0", sizeof(szBuf) );
	//	}

	//	// Do this so inherited classes looking for 'angles' don't have to bother with 'angle'
	//	return KeyValue( "angles", szBuf );
	//}

	// NOTE: Have to do these separate because they set two values instead of one
	//if( FStrEq( szKeyName, "angles" ) )
	//{
	//	QAngle angles;
	//	UTIL_StringToVector( angles.Base(), szValue );

	//	// If you're hitting this assert, it's probably because you're
	//	// calling SetLocalAngles from within a KeyValues method.. use SetAbsAngles instead!
	//	Assert( (GetEngineObject()->GetMoveParent() == NULL) && !IsEFlagSet( EFL_DIRTY_ABSTRANSFORM ) );
	//	SetAbsAngles( angles );
	//	return true;
	//}

	//if( FStrEq( szKeyName, "origin" ) )
	//{
	//	Vector vecOrigin;
	//	UTIL_StringToVector( vecOrigin.Base(), szValue );

	//	// If you're hitting this assert, it's probably because you're
	//	// calling SetLocalOrigin from within a KeyValues method.. use SetAbsOrigin instead!
	//	Assert( (GetEngineObject()->GetMoveParent() == NULL) && !IsEFlagSet( EFL_DIRTY_ABSTRANSFORM ) );
	//	SetAbsOrigin( vecOrigin );
	//	return true;
	//}

#ifdef GAME_DLL	
	
	//if ( FStrEq( szKeyName, "targetname" ) )
	//{
	//	m_iName = AllocPooledString( szValue );
	//	return true;
	//}

	// loop through the data description, and try and place the keys in
	if (!*ent_debugkeys.GetString())
	{
		for (datamap_t* dmap = this->GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap)
		{
			if (::ParseKeyvalue(this, dmap->dataDesc, dmap->dataNumFields, szKeyName, szValue)) {
				return true;
				//break;
			}
		}
	}
	else
	{
		// debug version - can be used to see what keys have been parsed in
		bool printKeyHits = false;
		const char* debugName = "";

		if (*ent_debugkeys.GetString() && !Q_stricmp(ent_debugkeys.GetString(), GetClassname()))
		{
			// Msg( "-- found entity of type %s\n", STRING(m_iClassname) );
			printKeyHits = true;
			debugName = GetClassname();
		}

		// loop through the data description, and try and place the keys in
		for (datamap_t* dmap = this->GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap)
		{
			if (!printKeyHits && *ent_debugkeys.GetString() && !Q_stricmp(dmap->dataClassName, ent_debugkeys.GetString()))
			{
				// Msg( "-- found class of type %s\n", dmap->dataClassName );
				printKeyHits = true;
				debugName = dmap->dataClassName;
			}

			if (::ParseKeyvalue(this, dmap->dataDesc, dmap->dataNumFields, szKeyName, szValue))
			{
				if (printKeyHits)
					Msg("(%s) key: %-16s value: %s\n", debugName, szKeyName, szValue);

				return true;
				//break;
			}
		}

		if (printKeyHits)
			Msg("!! (%s) key not handled: \"%s\" \"%s\"\n", GetClassname(), szKeyName, szValue);
	}

#endif

	// key hasn't been handled
	return false;
}

bool CBaseEntity::KeyValue( const char *szKeyName, float flValue ) 
{
	char	string[256];

	Q_snprintf(string,sizeof(string), "%f", flValue );

	return KeyValue( szKeyName, string );
}

bool CBaseEntity::KeyValue( const char *szKeyName, const Vector &vecValue ) 
{
	char	string[256];

	Q_snprintf(string,sizeof(string), "%f %f %f", vecValue.x, vecValue.y, vecValue.z );

	return KeyValue( szKeyName, string );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  :
// Output :
//-----------------------------------------------------------------------------

bool CBaseEntity::GetKeyValue( const char *szKeyName, char *szValue, int iMaxLen )
{
	if ( FStrEq( szKeyName, "rendercolor" ) || FStrEq( szKeyName, "rendercolor32" ))
	{
		color32 tmp = GetRenderColor();
		Q_snprintf( szValue, iMaxLen, "%d %d %d %d", tmp.r, tmp.g, tmp.b, tmp.a );
		return true;
	}
	
	if ( FStrEq( szKeyName, "renderamt" ) )
	{
		color32 tmp = GetRenderColor();
		Q_snprintf( szValue, iMaxLen, "%d", tmp.a );
		return true;
	}

	if ( FStrEq( szKeyName, "disableshadows" ))
	{
		Q_snprintf( szValue, iMaxLen, "%d", GetEngineObject()->IsEffectActive( EF_NOSHADOW ) );
		return true;
	}

	if ( FStrEq( szKeyName, "mins" ))
	{
		Assert( 0 );
		return false;
	}

	if ( FStrEq( szKeyName, "maxs" ))
	{
		Assert( 0 );
		return false;
	}

	if ( FStrEq( szKeyName, "disablereceiveshadows" ))
	{
		Q_snprintf( szValue, iMaxLen, "%d", GetEngineObject()->IsEffectActive( EF_NORECEIVESHADOW ) );
		return true;
	}

	if ( FStrEq( szKeyName, "nodamageforces" ))
	{
		Q_snprintf( szValue, iMaxLen, "%d", GetEngineObject()->IsEffectActive( EFL_NO_DAMAGE_FORCES ) );
		return true;
	}

	// Fix up single angles
	if( FStrEq( szKeyName, "angle" ) )
	{
		return false;
	}

	// NOTE: Have to do these separate because they set two values instead of one
	if( FStrEq( szKeyName, "angles" ) )
	{
		QAngle angles = GetEngineObject()->GetAbsAngles();

		Q_snprintf( szValue, iMaxLen, "%f %f %f", angles.x, angles.y, angles.z );
		return true;
	}

	if( FStrEq( szKeyName, "origin" ) )
	{
		Vector vecOrigin = GetEngineObject()->GetAbsOrigin();
		Q_snprintf( szValue, iMaxLen, "%f %f %f", vecOrigin.x, vecOrigin.y, vecOrigin.z );
		return true;
	}

#ifdef GAME_DLL	
	
	if ( FStrEq( szKeyName, "targetname" ) )
	{
		Q_snprintf( szValue, iMaxLen, "%s", STRING( GetEntityName() ) );
		return true;
	}

	if ( FStrEq( szKeyName, "classname" ) )
	{
		Q_snprintf( szValue, iMaxLen, "%s", GetClassname() );
		return true;
	}

	for ( datamap_t *dmap = GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap )
	{
		if ( ::ExtractKeyvalue( this, dmap->dataDesc, dmap->dataNumFields, szKeyName, szValue, iMaxLen ) )
			return true;
	}
#endif

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : collisionGroup - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseEntity::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	if (GetEngineObject()->GetCollisionGroup() == COLLISION_GROUP_DEBRIS)
	{
		if ( ! (contentsMask & CONTENTS_DEBRIS) )
			return false;
	}
	return true;
}

//------------------------------------------------------------------------------
// Purpose : Base implimentation for entity handling decals
//------------------------------------------------------------------------------
void CBaseEntity::DecalTrace( trace_t *pTrace, char const *decalName )
{
	int index = decalsystem->GetDecalIndexForName( decalName );
	if ( index < 0 )
		return;

	Assert( pTrace->m_pEnt );

	CBroadcastRecipientFilter filter;
	te->Decal( filter, 0.0, &pTrace->endpos, &pTrace->startpos,
		pTrace->GetEntityIndex(), pTrace->hitbox, index );
}

//-----------------------------------------------------------------------------
// Purpose: Base handling for impacts against entities
//-----------------------------------------------------------------------------
void CBaseEntity::ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName )
{
	VPROF( "CBaseEntity::ImpactTrace" );
	Assert( pTrace->m_pEnt );

	CBaseEntity *pEntity = (CBaseEntity*)pTrace->m_pEnt;
 
	// Build the impact data
	CEffectData data;
	data.m_vOrigin = pTrace->endpos;
	data.m_vStart = pTrace->startpos;
	data.m_nSurfaceProp = pTrace->surface.surfaceProps;
	data.m_nDamageType = iDamageType;
	data.m_nHitBox = pTrace->hitbox;
#ifdef CLIENT_DLL
	data.m_hEntity = ClientEntityList().GetBaseEntity( pEntity->entindex() );
#else
	data.m_nEntIndex = pEntity->entindex();
#endif

	// Send it on its way
	if ( !pCustomImpactName )
	{
		DispatchEffect( "Impact", data );
	}
	else
	{
		DispatchEffect( pCustomImpactName, data );
	}
}

//-----------------------------------------------------------------------------
// Purpose: returns the damage decal to use, given a damage type
// Input  : bitsDamageType - the damage type
// Output : the index of the damage decal to use
//-----------------------------------------------------------------------------
char const *CBaseEntity::DamageDecal( int bitsDamageType, int gameMaterial )
{
	if ( m_nRenderMode == kRenderTransAlpha )
		return "";

	if ( m_nRenderMode != kRenderNormal && gameMaterial == 'G' )
		return "BulletProof";

	if ( bitsDamageType == DMG_SLASH )
		return "ManhackCut";

	// This will get translated at a lower layer based on game material
	return "Impact.Concrete";
}

int CheckEntityVelocity( Vector &v )
{
	float r = k_flMaxEntitySpeed;
	if (
		v.x > -r && v.x < r &&
		v.y > -r && v.y < r &&
		v.z > -r && v.z < r)
	{
		// The usual case.  It's totally reasonable
		return 1;
	}
	float speed = v.Length();
	if ( speed < k_flMaxEntitySpeed * 100.0f )
	{
		// Sort of suspicious.  Clamp it
		v *= k_flMaxEntitySpeed / speed;
		return 0;
	}

	// A terrible, horrible, no good, very bad velocity.
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: My physics object has been updated, react or extract data
//-----------------------------------------------------------------------------
void CBaseEntity::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	switch(GetEngineObject()->GetMoveType() )
	{
	case MOVETYPE_VPHYSICS:
		{
			if (GetEngineObject()->GetMoveParent() )
			{
				DevWarning("Updating physics on object in hierarchy %s!\n", GetClassname());
				return;
			}
			Vector origin;
			QAngle angles;

			pPhysics->GetPosition( &origin, &angles );

			if ( !IsEntityQAngleReasonable( angles ) )
			{
				if ( CheckEmitReasonablePhysicsSpew() )
				{
					Warning( "Ignoring bogus angles (%f,%f,%f) from vphysics! (entity %s)\n", angles.x, angles.y, angles.z, GetDebugName() );
				}
				angles = vec3_angle;
			}
#ifndef CLIENT_DLL 
			Vector prevOrigin = GetEngineObject()->GetAbsOrigin();
#endif

			if ( IsEntityPositionReasonable( origin ) )
			{
				GetEngineObject()->SetAbsOrigin( origin );
			}
			else
			{
				if ( CheckEmitReasonablePhysicsSpew() )
				{
					Warning( "Ignoring unreasonable position (%f,%f,%f) from vphysics! (entity %s)\n", origin.x, origin.y, origin.z, GetDebugName() );
				}
			}

			for ( int i = 0; i < 3; ++i )
			{
				angles[ i ] = AngleNormalize( angles[ i ] );
			}
			GetEngineObject()->SetAbsAngles( angles );

			// Interactive debris converts back to debris when it comes to rest
			if ( pPhysics->IsAsleep() && GetEngineObject()->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE_DEBRIS )
			{
				GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
			}

#ifndef CLIENT_DLL 
			GetEngineObject()->PhysicsTouchTriggers( &prevOrigin );
			PhysicsRelinkChildren(gpGlobals->frametime);
#endif
		}
	break;

	case MOVETYPE_STEP:
		break;

	case MOVETYPE_PUSH:
#ifndef CLIENT_DLL
		VPhysicsUpdatePusher( pPhysics );
#endif
	break;
	}
}












//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseEntity::CreateVPhysics()
{
	return false;
}

bool CBaseEntity::IsStandable() const
{
	if (GetEngineObject()->GetSolidFlags() & FSOLID_NOT_STANDABLE)
		return false;

	if (GetEngineObject()->GetSolid() == SOLID_BSP || GetEngineObject()->GetSolid() == SOLID_VPHYSICS || GetEngineObject()->GetSolid() == SOLID_BBOX )
		return true;

	return IsBSPModel( ); 
}

bool CBaseEntity::IsBSPModel() const
{
	if (GetEngineObject()->GetSolid() == SOLID_BSP )
		return true;
	
	const model_t *model = modelinfo->GetModel(GetEngineObject()->GetModelIndex() );

	if (GetEngineObject()->GetSolid() == SOLID_VPHYSICS && modelinfo->GetModelType( model ) == mod_brush )
		return true;

	return false;
}


void CBaseEntity::OnPositionChanged() {
	// NOTE: This will also mark shadow projection + client leaf dirty
	GetEngineObject()->MarkPartitionHandleDirty();
}

void CBaseEntity::OnAnglesChanged() {
	if (GetEngineObject()->DoesRotationInvalidateSurroundingBox())
	{
		// NOTE: This will handle the KD-tree, surrounding bounds, PVS
		// render-to-texture shadow, shadow projection, and client leaf dirty
		GetEngineObject()->MarkSurroundingBoundsDirty();
	}
}

void CBaseEntity::OnAnimationChanged() {

}


/*
================
FireBullets

Go to the trouble of combining multiple pellets into a single damage call.
================
*/

#if defined( GAME_DLL )
class CBulletsTraceFilter : public CTraceFilterSimpleList
{
public:
	CBulletsTraceFilter( int collisionGroup ) : CTraceFilterSimpleList( collisionGroup ) {}

	bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		if ( m_PassEntities.Count() )
		{
			CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
			CBaseEntity *pPassEntity = EntityFromEntityHandle( m_PassEntities[0] );
			if ( pEntity && pPassEntity && pEntity->GetOwnerEntity() == pPassEntity && 
				pPassEntity->GetEngineObject()->IsSolidFlagSet(FSOLID_NOT_SOLID) && pPassEntity->GetEngineObject()->IsSolidFlagSet( FSOLID_CUSTOMBOXTEST ) &&
				pPassEntity->GetEngineObject()->IsSolidFlagSet( FSOLID_CUSTOMRAYTEST ) )
			{
				// It's a bone follower of the entity to ignore (toml 8/3/2007)
				return false;
			}
		}
		return CTraceFilterSimpleList::ShouldHitEntity( pHandleEntity, contentsMask );
	}

};
#else
typedef CTraceFilterSimpleList CBulletsTraceFilter;
#endif

void CBaseEntity::FireBullets( const FireBulletsInfo_t &info )
{
	static int	tracerCount;
	trace_t		tr;
	CAmmoDef*	pAmmoDef	= GetAmmoDef();
	int			nDamageType	= pAmmoDef->DamageType(info.m_iAmmoType);
	int			nAmmoFlags	= pAmmoDef->Flags(info.m_iAmmoType);
	
	bool bDoServerEffects = true;

#if defined( HL2MP ) && defined( GAME_DLL )
	bDoServerEffects = false;
#endif

#if defined( GAME_DLL )
	if( IsPlayer() )
	{
		CBasePlayer *pPlayer = dynamic_cast<CBasePlayer*>(this);

		int rumbleEffect = pPlayer->GetActiveWeapon()->GetRumbleEffect();

		if( rumbleEffect != RUMBLE_INVALID )
		{
			if( rumbleEffect == RUMBLE_SHOTGUN_SINGLE )
			{
				if( info.m_iShots == 12 )
				{
					// Upgrade to double barrel rumble effect
					rumbleEffect = RUMBLE_SHOTGUN_DOUBLE;
				}
			}

			pPlayer->RumbleEffect( rumbleEffect, 0, RUMBLE_FLAG_RESTART );
		}
	}
#endif// GAME_DLL

	int iPlayerDamage = info.m_iPlayerDamage;
	if ( iPlayerDamage == 0 )
	{
		if ( nAmmoFlags & AMMO_INTERPRET_PLRDAMAGE_AS_DAMAGE_TO_PLAYER )
		{
			iPlayerDamage = pAmmoDef->PlrDamage( info.m_iAmmoType );
		}
	}

	// the default attacker is ourselves
	CBaseEntity *pAttacker = info.m_pAttacker ? info.m_pAttacker : this;

	// Make sure we don't have a dangling damage target from a recursive call
	if ( g_MultiDamage.GetTarget() != NULL )
	{
		ApplyMultiDamage();
	}
	  
	ClearMultiDamage();
	g_MultiDamage.SetDamageType( nDamageType | DMG_NEVERGIB );

	Vector vecDir;
	Vector vecEnd;
	
	// Skip multiple entities when tracing
	CBulletsTraceFilter traceFilter( COLLISION_GROUP_NONE );
	traceFilter.SetPassEntity( this ); // Standard pass entity for THIS so that it can be easily removed from the list after passing through a portal
	traceFilter.AddEntityToIgnore( info.m_pAdditionalIgnoreEnt );

#if defined( HL2_EPISODIC ) && defined( GAME_DLL )
	// FIXME: We need to emulate this same behavior on the client as well -- jdw
	// Also ignore a vehicle we're a passenger in
	if ( MyCombatCharacterPointer() != NULL && MyCombatCharacterPointer()->IsInAVehicle() )
	{
		traceFilter.AddEntityToIgnore( MyCombatCharacterPointer()->GetVehicleEntity() );
	}
#endif // SERVER_DLL

	bool bUnderwaterBullets = ShouldDrawUnderwaterBulletBubbles();
	bool bStartedInWater = false;
	if ( bUnderwaterBullets )
	{
		bStartedInWater = ( enginetrace->GetPointContents( info.m_vecSrc ) & (CONTENTS_WATER|CONTENTS_SLIME) ) != 0;
	}

	// Prediction is only usable on players
	int iSeed = 0;
	if ( IsPlayer() )
	{
		iSeed = EntityList()->GetPredictionRandomSeed() & 255;
	}

#if defined( HL2MP ) && defined( GAME_DLL )
	int iEffectSeed = iSeed;
#endif
	//-----------------------------------------------------
	// Set up our shot manipulator.
	//-----------------------------------------------------
	CShotManipulator Manipulator( info.m_vecDirShooting );

	bool bDoImpacts = false;
	bool bDoTracers = false;
	
	float flCumulativeDamage = 0.0f;

	for (int iShot = 0; iShot < info.m_iShots; iShot++)
	{
		bool bHitWater = false;
		bool bHitGlass = false;

		// Prediction is only usable on players
		if ( IsPlayer() )
		{
			RandomSeed( iSeed );	// init random system with this seed
		}

		// If we're firing multiple shots, and the first shot has to be bang on target, ignore spread
		if ( iShot == 0 && info.m_iShots > 1 && (info.m_nFlags & FIRE_BULLETS_FIRST_SHOT_ACCURATE) )
		{
			vecDir = Manipulator.GetShotDirection();
		}
		else
		{

			// Don't run the biasing code for the player at the moment.
			vecDir = Manipulator.ApplySpread( info.m_vecSpread );
		}

		vecEnd = info.m_vecSrc + vecDir * info.m_flDistance;

#ifdef PORTAL
		CProp_Portal *pShootThroughPortal = NULL;
		float fPortalFraction = 2.0f;
#endif


		if( IsPlayer() && info.m_iShots > 1 && iShot % 2 )
		{
			// Half of the shotgun pellets are hulls that make it easier to hit targets with the shotgun.
#ifdef PORTAL
			Ray_t rayBullet;
			rayBullet.Init( info.m_vecSrc, vecEnd );
			pShootThroughPortal = UTIL_Portal_FirstAlongRay( rayBullet, fPortalFraction );
			if ( !UTIL_Portal_TraceRay_Bullets( pShootThroughPortal, rayBullet, MASK_SHOT, &traceFilter, &tr ) )
			{
				pShootThroughPortal = NULL;
			}
#else
			AI_TraceHull( info.m_vecSrc, vecEnd, Vector( -3, -3, -3 ), Vector( 3, 3, 3 ), MASK_SHOT, &traceFilter, &tr );
#endif //#ifdef PORTAL
		}
		else
		{
#ifdef PORTAL
			Ray_t rayBullet;
			rayBullet.Init( info.m_vecSrc, vecEnd );
			pShootThroughPortal = UTIL_Portal_FirstAlongRay( rayBullet, fPortalFraction );
			if ( !UTIL_Portal_TraceRay_Bullets( pShootThroughPortal, rayBullet, MASK_SHOT, &traceFilter, &tr ) )
			{
				pShootThroughPortal = NULL;
			}
#elif TF_DLL
			CTraceFilterIgnoreFriendlyCombatItems traceFilterCombatItem( this, COLLISION_GROUP_NONE, GetTeamNumber() );
			if ( TFGameRules() && TFGameRules()->GameModeUsesUpgrades() )
			{
				CTraceFilterChain traceFilterChain( &traceFilter, &traceFilterCombatItem );
				AI_TraceLine(info.m_vecSrc, vecEnd, MASK_SHOT, &traceFilterChain, &tr);
			}
			else
			{
				AI_TraceLine(info.m_vecSrc, vecEnd, MASK_SHOT, &traceFilter, &tr);
			}
#else
			AI_TraceLine(info.m_vecSrc, vecEnd, MASK_SHOT, &traceFilter, &tr);
#endif //#ifdef PORTAL
		}

		// Tracker 70354/63250:  ywb 8/2/07
		// Fixes bug where trace from turret with attachment point outside of Vcollide
		//  starts solid so doesn't hit anything else in the world and the final coord 
		//  is outside of the MAX_COORD_FLOAT range.  This cause trying to send the end pos
		//  of the tracer down to the client with an origin which is out-of-range for networking
		if ( tr.startsolid )
		{
			tr.endpos = tr.startpos;
			tr.fraction = 0.0f;
		}

	// bullet's final direction can be changed by passing through a portal
#ifdef PORTAL
		if ( !tr.startsolid )
		{
			vecDir = tr.endpos - tr.startpos;
			VectorNormalize( vecDir );
		}
#endif

#ifdef GAME_DLL
		if ( ai_debug_shoot_positions.GetBool() )
			NDebugOverlay::Line(info.m_vecSrc, vecEnd, 255, 255, 255, false, .1 );
#endif

		if ( bStartedInWater )
		{
#ifdef GAME_DLL
			Vector vBubbleStart = info.m_vecSrc;
			Vector vBubbleEnd = tr.endpos;

#ifdef PORTAL
			if ( pShootThroughPortal )
			{
				vBubbleEnd = info.m_vecSrc + ( vecEnd - info.m_vecSrc ) * fPortalFraction;
			}
#endif //#ifdef PORTAL

			CreateBubbleTrailTracer( vBubbleStart, vBubbleEnd, vecDir );
			
#ifdef PORTAL
			if ( pShootThroughPortal )
			{
				Vector vTransformedIntersection;
				UTIL_Portal_PointTransform( pShootThroughPortal->MatrixThisToLinked(), vBubbleEnd, vTransformedIntersection );

				CreateBubbleTrailTracer( vTransformedIntersection, tr.endpos, vecDir );
			}
#endif //#ifdef PORTAL

#endif //#ifdef GAME_DLL
			bHitWater = true;
		}

		// Now hit all triggers along the ray that respond to shots...
		// Clip the ray to the first collided solid returned from traceline
		CTakeDamageInfo triggerInfo( pAttacker, pAttacker, info.m_flDamage, nDamageType );
		CalculateBulletDamageForce( &triggerInfo, info.m_iAmmoType, vecDir, tr.endpos );
		triggerInfo.ScaleDamageForce( info.m_flDamageForceScale );
		triggerInfo.SetAmmoType( info.m_iAmmoType );
#ifdef GAME_DLL
		TraceAttackToTriggers( triggerInfo, tr.startpos, tr.endpos, vecDir );
#endif

		// Make sure given a valid bullet type
		if (info.m_iAmmoType == -1)
		{
			DevMsg("ERROR: Undefined ammo type!\n");
			return;
		}

		Vector vecTracerDest = tr.endpos;

		// do damage, paint decals
		if (tr.fraction != 1.0)
		{
#ifdef GAME_DLL
			UpdateShotStatistics( tr );

			// For shots that don't need persistance
			int soundEntChannel = ( info.m_nFlags&FIRE_BULLETS_TEMPORARY_DANGER_SOUND ) ? SOUNDENT_CHANNEL_BULLET_IMPACT : SOUNDENT_CHANNEL_UNSPECIFIED;

			CSoundEnt::InsertSound( SOUND_BULLET_IMPACT, tr.endpos, 200, 0.5, this, soundEntChannel );
#endif

			// See if the bullet ended up underwater + started out of the water
			if ( !bHitWater && ( enginetrace->GetPointContents( tr.endpos ) & (CONTENTS_WATER|CONTENTS_SLIME) ) )
			{
				bHitWater = HandleShotImpactingWater( info, vecEnd, &traceFilter, &vecTracerDest );
			}

			float flActualDamage = info.m_flDamage;

			// If we hit a player, and we have player damage specified, use that instead
			// Adrian: Make sure to use the currect value if we hit a vehicle the player is currently driving.
			if ( iPlayerDamage )
			{
				if (((CBaseEntity*)tr.m_pEnt)->IsPlayer() )
				{
					flActualDamage = iPlayerDamage;
				}
#ifdef GAME_DLL
				else if (((CBaseEntity*)tr.m_pEnt)->GetServerVehicle() )
				{
					if (((CBaseEntity*)tr.m_pEnt)->GetServerVehicle()->GetPassenger() && ((CBaseEntity*)tr.m_pEnt)->GetServerVehicle()->GetPassenger()->IsPlayer() )
					{
						flActualDamage = iPlayerDamage;
					}
				}
#endif
			}

			int nActualDamageType = nDamageType;
			if ( flActualDamage == 0.0 )
			{
				flActualDamage = g_pGameRules->GetAmmoDamage( pAttacker, (CBaseEntity*)tr.m_pEnt, info.m_iAmmoType );
			}
			else
			{
				nActualDamageType = nDamageType | ((flActualDamage > 16) ? DMG_ALWAYSGIB : DMG_NEVERGIB );
			}

			if ( !bHitWater || ((info.m_nFlags & FIRE_BULLETS_DONT_HIT_UNDERWATER) == 0) )
			{
				// Damage specified by function parameter
				CTakeDamageInfo dmgInfo( this, pAttacker, flActualDamage, nActualDamageType );
				ModifyFireBulletsDamage( &dmgInfo );
				CalculateBulletDamageForce( &dmgInfo, info.m_iAmmoType, vecDir, tr.endpos );
				dmgInfo.ScaleDamageForce( info.m_flDamageForceScale );
				dmgInfo.SetAmmoType( info.m_iAmmoType );
				((CBaseEntity*)tr.m_pEnt)->DispatchTraceAttack( dmgInfo, vecDir, &tr );
			
				if ( ToBaseCombatCharacter((CBaseEntity*)tr.m_pEnt ) )
				{
					flCumulativeDamage += dmgInfo.GetDamage();
				}

				if ( bStartedInWater || !bHitWater || (info.m_nFlags & FIRE_BULLETS_ALLOW_WATER_SURFACE_IMPACTS) )
				{
					if ( bDoServerEffects == true )
					{
						DoImpactEffect( tr, nDamageType );
					}
					else
					{
						bDoImpacts = true;
					}
				}
				else
				{
					// We may not impact, but we DO need to affect ragdolls on the client
					CEffectData data;
					data.m_vStart = tr.startpos;
					data.m_vOrigin = tr.endpos;
					data.m_nDamageType = nDamageType;
					
					DispatchEffect( "RagdollImpact", data );
				}
	
#ifdef GAME_DLL
				if ( nAmmoFlags & AMMO_FORCE_DROP_IF_CARRIED )
				{
					// Make sure if the player is holding this, he drops it
					Pickup_ForcePlayerToDropThisObject((CBaseEntity*)tr.m_pEnt );
				}
#endif
			}
		}

		// See if we hit glass
		if ( tr.m_pEnt != NULL )
		{
#ifdef GAME_DLL
			surfacedata_t *psurf = EntityList()->PhysGetProps()->GetSurfaceData( tr.surface.surfaceProps );
			if ( ( psurf != NULL ) && ( psurf->game.material == CHAR_TEX_GLASS ) && (((CBaseEntity*)tr.m_pEnt)->ClassMatches( "func_breakable" ) ) )
			{
				// Query the func_breakable for whether it wants to allow for bullet penetration
				if (((CBaseEntity*)tr.m_pEnt)->GetEngineObject()->HasSpawnFlags( SF_BREAK_NO_BULLET_PENETRATION ) == false )
				{
					bHitGlass = true;
				}
			}
#endif
		}

		if ( ( info.m_iTracerFreq != 0 ) && ( tracerCount++ % info.m_iTracerFreq ) == 0 && ( bHitGlass == false ) )
		{
			if ( bDoServerEffects == true )
			{
				Vector vecTracerSrc = vec3_origin;
				ComputeTracerStartPosition( info.m_vecSrc, &vecTracerSrc );

				trace_t Tracer;
				Tracer = tr;
				Tracer.endpos = vecTracerDest;

#ifdef PORTAL
				if ( pShootThroughPortal )
				{
					Tracer.endpos = info.m_vecSrc + ( vecEnd - info.m_vecSrc ) * fPortalFraction;
				}
#endif //#ifdef PORTAL

				MakeTracer( vecTracerSrc, Tracer, pAmmoDef->TracerType(info.m_iAmmoType) );

#ifdef PORTAL
				if ( pShootThroughPortal )
				{
					Vector vTransformedIntersection;
					UTIL_Portal_PointTransform( pShootThroughPortal->MatrixThisToLinked(), Tracer.endpos, vTransformedIntersection );
					ComputeTracerStartPosition( vTransformedIntersection, &vecTracerSrc );

					Tracer.endpos = vecTracerDest;

					MakeTracer( vecTracerSrc, Tracer, pAmmoDef->TracerType(info.m_iAmmoType) );

					// Shooting through a portal, the damage direction is translated through the passed-through portal
					// so the damage indicator hud animation is correct
					Vector vDmgOriginThroughPortal;
					UTIL_Portal_PointTransform( pShootThroughPortal->MatrixThisToLinked(), info.m_vecSrc, vDmgOriginThroughPortal );
					g_MultiDamage.SetDamagePosition ( vDmgOriginThroughPortal );
				}
				else
				{
					g_MultiDamage.SetDamagePosition ( info.m_vecSrc );
				}
#endif //#ifdef PORTAL
			}
			else
			{
				bDoTracers = true;
			}
		}

		//NOTENOTE: We could expand this to a more general solution for various material penetration types (wood, thin metal, etc)

		// See if we should pass through glass
#ifdef GAME_DLL
		if ( bHitGlass )
		{
			HandleShotImpactingGlass( info, tr, vecDir, &traceFilter );
		}
#endif

		iSeed++;
	}

#if defined( HL2MP ) && defined( GAME_DLL )
	if ( bDoServerEffects == false )
	{
		TE_HL2MPFireBullets( entindex(), tr.startpos, info.m_vecDirShooting, info.m_iAmmoType, iEffectSeed, info.m_iShots, info.m_vecSpread.x, bDoTracers, bDoImpacts );
	}
#endif

#ifdef GAME_DLL
	ApplyMultiDamage();

	if ( IsPlayer() && flCumulativeDamage > 0.0f )
	{
		CBasePlayer *pPlayer = static_cast< CBasePlayer * >( this );
		CTakeDamageInfo dmgInfo( this, pAttacker, flCumulativeDamage, nDamageType );
		gamestats->Event_WeaponHit( pPlayer, info.m_bPrimaryAttack, pPlayer->GetActiveWeapon()->GetClassname(), dmgInfo );
	}
#endif
}


//-----------------------------------------------------------------------------
// Should we draw bubbles underwater?
//-----------------------------------------------------------------------------
bool CBaseEntity::ShouldDrawUnderwaterBulletBubbles()
{
#if defined( HL2_DLL ) && defined( GAME_DLL )
	CBaseEntity *pPlayer = ( gpGlobals->maxClients == 1 ) ? UTIL_GetLocalPlayer() : NULL;
	return pPlayer && (pPlayer->GetWaterLevel() == 3);
#else
	return false;
#endif
}


//-----------------------------------------------------------------------------
// Handle shot entering water
//-----------------------------------------------------------------------------
bool CBaseEntity::HandleShotImpactingWater( const FireBulletsInfo_t &info, 
	const Vector &vecEnd, ITraceFilter *pTraceFilter, Vector *pVecTracerDest )
{
	trace_t	waterTrace;

	// Trace again with water enabled
	AI_TraceLine( info.m_vecSrc, vecEnd, (MASK_SHOT|CONTENTS_WATER|CONTENTS_SLIME), pTraceFilter, &waterTrace );
	
	// See if this is the point we entered
	if ( ( enginetrace->GetPointContents( waterTrace.endpos - Vector(0,0,0.1f) ) & (CONTENTS_WATER|CONTENTS_SLIME) ) == 0 )
		return false;

	if ( ShouldDrawWaterImpacts() )
	{
		int	nMinSplashSize = GetAmmoDef()->MinSplashSize(info.m_iAmmoType);
		int	nMaxSplashSize = GetAmmoDef()->MaxSplashSize(info.m_iAmmoType);

		CEffectData	data;
 		data.m_vOrigin = waterTrace.endpos;
		data.m_vNormal = waterTrace.plane.normal;
		data.m_flScale = random->RandomFloat( nMinSplashSize, nMaxSplashSize );
		if ( waterTrace.contents & CONTENTS_SLIME )
		{
			data.m_fFlags |= FX_WATER_IN_SLIME;
		}
		DispatchEffect( "gunshotsplash", data );
	}

#ifdef GAME_DLL
	if ( ShouldDrawUnderwaterBulletBubbles() )
	{
		CWaterBullet *pWaterBullet = ( CWaterBullet * )gEntList.CreateEntityByName( "waterbullet" );
		if ( pWaterBullet )
		{
			pWaterBullet->Spawn( waterTrace.endpos, info.m_vecDirShooting );
					 
			CEffectData tracerData;
			tracerData.m_vStart = waterTrace.endpos;
			tracerData.m_vOrigin = waterTrace.endpos + info.m_vecDirShooting * 400.0f;
			tracerData.m_fFlags = TRACER_TYPE_WATERBULLET;
			DispatchEffect( "TracerSound", tracerData );
		}
	}
#endif

	*pVecTracerDest = waterTrace.endpos;
	return true;
}


ITraceFilter* CBaseEntity::GetBeamTraceFilter( void )
{
	return NULL;
}

void CBaseEntity::DispatchTraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator )
{
#ifdef GAME_DLL
	// Make sure our damage filter allows the damage.
	if ( !PassesDamageFilter( info ))
	{
		return;
	}
#endif

	TraceAttack( info, vecDir, ptr, pAccumulator );
}

void CBaseEntity::TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator )
{
	Vector vecOrigin = ptr->endpos - vecDir * 4;

	if ( m_takedamage )
	{
#ifdef GAME_DLL
		if ( pAccumulator )
		{
			pAccumulator->AccumulateMultiDamage( info, this );
		}
		else
#endif // GAME_DLL
		{
			AddMultiDamage( info, this );
		}

		int blood = BloodColor();
		
		if ( blood != DONT_BLEED )
		{
			SpawnBlood( vecOrigin, vecDir, blood, info.GetDamage() );// a little surface blood.
			TraceBleed( info.GetDamage(), vecDir, ptr, info.GetDamageType() );
		}
	}
}


//-----------------------------------------------------------------------------
// Allows the shooter to change the impact effect of his bullets
//-----------------------------------------------------------------------------
void CBaseEntity::DoImpactEffect( trace_t &tr, int nDamageType )
{
	// give shooter a chance to do a custom impact.
	UTIL_ImpactTrace( &tr, nDamageType );
} 


//-----------------------------------------------------------------------------
// Computes the tracer start position
//-----------------------------------------------------------------------------
void CBaseEntity::ComputeTracerStartPosition( const Vector &vecShotSrc, Vector *pVecTracerStart )
{
#ifndef HL2MP
	if ( g_pGameRules->IsMultiplayer() )
	{
		// NOTE: we do this because in MakeTracer, we force it to use the attachment position
		// in multiplayer, so the results from this function should never actually get used.
		pVecTracerStart->Init( 999, 999, 999 );
		return;
	}
#endif
	
	if ( IsPlayer() )
	{
		// adjust tracer position for player
		Vector forward, right;
		CBasePlayer *pPlayer = ToBasePlayer( this );
		pPlayer->EyeVectors( &forward, &right, NULL );
		*pVecTracerStart = vecShotSrc + Vector ( 0 , 0 , -4 ) + right * 2 + forward * 16;
	}
	else
	{
		*pVecTracerStart = vecShotSrc;

		CBaseCombatCharacter *pBCC = MyCombatCharacterPointer();
		if ( pBCC != NULL )
		{
			CBaseCombatWeapon *pWeapon = pBCC->GetActiveWeapon();

			if ( pWeapon != NULL )
			{
				Vector vecMuzzle;
				QAngle vecMuzzleAngles;

				if ( pWeapon->GetEngineObject()->GetAttachment( 1, vecMuzzle, vecMuzzleAngles ) )
				{
					*pVecTracerStart = vecMuzzle;
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Virtual function allows entities to handle tracer presentation
//			as they see fit.
//
// Input  : vecTracerSrc - the point at which to start the tracer (not always the
//			same spot as the traceline!
//
//			tr - the entire trace result for the shot.
//
// Output :
//-----------------------------------------------------------------------------
void CBaseEntity::MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType )
{
	const char *pszTracerName = GetTracerType();

	Vector vNewSrc = vecTracerSrc;

	int iAttachment = GetTracerAttachment();

	switch ( iTracerType )
	{
	case TRACER_LINE:
		UTIL_Tracer( vNewSrc, tr.endpos, entindex(), iAttachment, 0.0f, false, pszTracerName );
		break;

	case TRACER_LINE_AND_WHIZ:
		UTIL_Tracer( vNewSrc, tr.endpos, entindex(), iAttachment, 0.0f, true, pszTracerName );
		break;
	}
}

//-----------------------------------------------------------------------------
// Default tracer attachment
//-----------------------------------------------------------------------------
int CBaseEntity::GetTracerAttachment( void )
{
	int iAttachment = TRACER_DONT_USE_ATTACHMENT;

	if ( g_pGameRules->IsMultiplayer() )
	{
		iAttachment = 1;
	}

	return iAttachment;
}


int CBaseEntity::BloodColor()
{
	return DONT_BLEED; 
}


void CBaseEntity::TraceBleed( float flDamage, const Vector &vecDir, trace_t *ptr, int bitsDamageType )
{
	if ((BloodColor() == DONT_BLEED) || (BloodColor() == BLOOD_COLOR_MECH))
	{
		return;
	}

	if (flDamage == 0)
		return;

	if (! (bitsDamageType & (DMG_CRUSH | DMG_BULLET | DMG_SLASH | DMG_BLAST | DMG_CLUB | DMG_AIRBOAT)))
		return;

	// make blood decal on the wall!
	trace_t Bloodtr;
	Vector vecTraceDir;
	float flNoise;
	int cCount;
	int i;

#ifdef GAME_DLL
	if ( !IsAlive() )
	{
		// dealing with a dead npc.
		if ( GetMaxHealth() <= 0 )
		{
			// no blood decal for a npc that has already decalled its limit.
			return;
		}
		else
		{
			m_iMaxHealth -= 1;
		}
	}
#endif

	if (flDamage < 10)
	{
		flNoise = 0.1;
		cCount = 1;
	}
	else if (flDamage < 25)
	{
		flNoise = 0.2;
		cCount = 2;
	}
	else
	{
		flNoise = 0.3;
		cCount = 4;
	}

	float flTraceDist = (bitsDamageType & DMG_AIRBOAT) ? 384 : 172;
	for ( i = 0 ; i < cCount ; i++ )
	{
		vecTraceDir = vecDir * -1;// trace in the opposite direction the shot came from (the direction the shot is going)

		vecTraceDir.x += random->RandomFloat( -flNoise, flNoise );
		vecTraceDir.y += random->RandomFloat( -flNoise, flNoise );
		vecTraceDir.z += random->RandomFloat( -flNoise, flNoise );

		// Don't bleed on grates.
		AI_TraceLine( ptr->endpos, ptr->endpos + vecTraceDir * -flTraceDist, MASK_SOLID_BRUSHONLY & ~CONTENTS_GRATE, this, COLLISION_GROUP_NONE, &Bloodtr);

		if ( Bloodtr.fraction != 1.0 )
		{
			UTIL_BloodDecalTrace( &Bloodtr, BloodColor() );
		}
	}
}


const char* CBaseEntity::GetTracerType()
{
	return NULL;
}



void CBaseEntity::SetEffectEntity( CBaseEntity *pEffectEnt )
{
	if ( m_hEffectEntity.Get() != pEffectEnt )
	{
		m_hEffectEntity = pEffectEnt;
	}
}

void CBaseEntity::ApplyLocalVelocityImpulse( const Vector &inVecImpulse )
{
	// NOTE: Don't have to use GetVelocity here because local values
	// are always guaranteed to be correct, unlike abs values which may 
	// require recomputation
	if ( inVecImpulse != vec3_origin )
	{
		Vector vecImpulse = inVecImpulse;

		// Safety check against receive a huge impulse, which can explode physics
		switch ( CheckEntityVelocity( vecImpulse ) )
		{
			case -1:
				Warning( "Discarding ApplyLocalVelocityImpulse(%f,%f,%f) on %s\n", vecImpulse.x, vecImpulse.y, vecImpulse.z, GetDebugName() );
				Assert( false );
				return;
			case 0:
				if ( CheckEmitReasonablePhysicsSpew() )
				{
					Warning( "Clamping ApplyLocalVelocityImpulse(%f,%f,%f) on %s\n", inVecImpulse.x, inVecImpulse.y, inVecImpulse.z, GetDebugName() );
				}
				break;
		}

		if (GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS )
		{
			Vector worldVel;
			GetEngineObject()->VPhysicsGetObject()->LocalToWorld( &worldVel, vecImpulse );
			GetEngineObject()->VPhysicsGetObject()->AddVelocity( &worldVel, NULL );
		}
		else
		{
			GetEngineObject()->InvalidatePhysicsRecursive( VELOCITY_CHANGED );
#ifdef GAME_DLL
			GetEngineObject()->SetLocalVelocity(GetEngineObject()->GetLocalVelocity() + vecImpulse);
#endif // GAME_DLL
#ifdef CLIENT_DLL
			GetEngineObject()->SetLocalVelocity(GetEngineObject()->GetLocalVelocity() + vecImpulse);
#endif // CLIENT_DLL

		}
	}
}

void CBaseEntity::ApplyAbsVelocityImpulse( const Vector &inVecImpulse )
{
	if ( inVecImpulse != vec3_origin )
	{
		Vector vecImpulse = inVecImpulse;

		// Safety check against receive a huge impulse, which can explode physics
		switch ( CheckEntityVelocity( vecImpulse ) )
		{
			case -1:
				Warning( "Discarding ApplyAbsVelocityImpulse(%f,%f,%f) on %s\n", vecImpulse.x, vecImpulse.y, vecImpulse.z, GetDebugName() );
				Assert( false );
				return;
			case 0:
				if ( CheckEmitReasonablePhysicsSpew() )
				{
					Warning( "Clamping ApplyAbsVelocityImpulse(%f,%f,%f) on %s\n", inVecImpulse.x, inVecImpulse.y, inVecImpulse.z, GetDebugName() );
				}
				break;
		}

		if (GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS )
		{
			GetEngineObject()->VPhysicsGetObject()->AddVelocity( &vecImpulse, NULL );
		}
		else
		{
			// NOTE: Have to use GetAbsVelocity here to ensure it's the correct value
			Vector vecResult;
			VectorAdd(GetEngineObject()->GetAbsVelocity(), vecImpulse, vecResult );
			GetEngineObject()->SetAbsVelocity( vecResult );
		}
	}
}

void CBaseEntity::ApplyLocalAngularVelocityImpulse( const AngularImpulse &angImpulse )
{
	if (angImpulse != vec3_origin )
	{
		// Safety check against receive a huge impulse, which can explode physics
		if ( !IsEntityAngularVelocityReasonable( angImpulse ) )
		{
			Warning( "Bad ApplyLocalAngularVelocityImpulse(%f,%f,%f) on %s\n", angImpulse.x, angImpulse.y, angImpulse.z, GetDebugName() );
			Assert( false );
			return;
		}

		if (GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS )
		{
			GetEngineObject()->VPhysicsGetObject()->AddVelocity( NULL, &angImpulse );
		}
		else
		{
			QAngle vecResult;
			AngularImpulseToQAngle( angImpulse, vecResult );
			VectorAdd( GetLocalAngularVelocity(), vecResult, vecResult );
			SetLocalAngularVelocity( vecResult );
		}
	}
}

int CBaseEntity::GetWaterType() const
{
	int out = 0;
	if ( m_nWaterType & 1 )
		out |= CONTENTS_WATER;
	if ( m_nWaterType & 2 )
		out |= CONTENTS_SLIME;
	return out;
}

void CBaseEntity::SetWaterType( int nType )
{
	m_nWaterType = 0;
	if ( nType & CONTENTS_WATER )
		m_nWaterType |= 1;
	if ( nType & CONTENTS_SLIME )
		m_nWaterType |= 2;
}






#if !defined( CLIENT_DLL )

void ClearModelSoundsCache();

#endif // !CLIENT_DLL

void S_SoundEmitterSystemFlush(void)
{
#if !defined( CLIENT_DLL )
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;
#endif

	// save the current soundscape
	// kill the system
	g_pSoundEmitterSystem->Shutdown();

	// restart the system
	g_pSoundEmitterSystem->Init();

#if !defined( CLIENT_DLL )
	// Redo precache all wave files... (this should work now that we have dynamic string tables)
	g_pSoundEmitterSystem->LevelInitPreEntity();

	// These store raw sound indices for faster precaching, blow them away.
	ClearModelSoundsCache();
#endif

	// TODO:  when we go to a handle system, we'll need to invalidate handles somehow
}

#if defined( CLIENT_DLL )
CON_COMMAND_F(cl_soundemitter_flush, "Flushes the sounds.txt system (client only)", FCVAR_CHEAT)
#else
CON_COMMAND_F(sv_soundemitter_flush, "Flushes the sounds.txt system (server only)", FCVAR_DEVELOPMENTONLY)
#endif
{
	S_SoundEmitterSystemFlush();
}

#if !defined(_RETAIL)

#if !defined( CLIENT_DLL ) 

#if !defined( _XBOX )

CON_COMMAND_F(sv_soundemitter_filecheck, "Report missing wave files for sounds and game_sounds files.", FCVAR_DEVELOPMENTONLY)
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	int missing = soundemitterbase->CheckForMissingWavFiles(true);
	DevMsg("---------------------------\nTotal missing files %i\n", missing);
}

CON_COMMAND_F(sv_findsoundname, "Find sound names which reference the specified wave files.", FCVAR_DEVELOPMENTONLY)
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	if (args.ArgC() != 2)
		return;

	int c = soundemitterbase->GetSoundCount();
	int i;

	char const* search = args[1];
	if (!search)
		return;

	for (i = 0; i < c; i++)
	{
		CSoundParametersInternal* internal = soundemitterbase->InternalGetParametersForSound(i);
		if (!internal)
			continue;

		int waveCount = internal->NumSoundNames();
		if (waveCount > 0)
		{
			for (int wave = 0; wave < waveCount; wave++)
			{
				char const* wavefilename = soundemitterbase->GetWaveName(internal->GetSoundNames()[wave].symbol);

				if (Q_stristr(wavefilename, search))
				{
					char const* soundname = soundemitterbase->GetSoundName(i);
					char const* scriptname = soundemitterbase->GetSourceFileForSound(i);

					Msg("Referenced by '%s:%s' -- %s\n", scriptname, soundname, wavefilename);
				}
			}
		}
	}
}
#endif // !_XBOX

#else
void Playgamesound_f(const CCommand& args)
{
	CBasePlayer* pPlayer = (C_BasePlayer*)ClientEntityList().GetLocalPlayer();
	if (pPlayer)
	{
		if (args.ArgC() > 2)
		{
			Vector position = pPlayer->EyePosition();
			Vector forward;
			pPlayer->GetEngineObject()->GetVectors(&forward, NULL, NULL);
			position += atof(args[2]) * forward;
			CPASAttenuationFilter filter(pPlayer);
			EmitSound_t params;
			params.m_pSoundName = args[1];
			params.m_pOrigin = &position;
			params.m_flVolume = 0.0f;
			params.m_nPitch = 0;
			g_pSoundEmitterSystem->EmitSound(filter, 0, params);
		}
		else
		{
			const char* soundname = args[1];
			CPASAttenuationFilter filter(pPlayer, soundname);

			EmitSound_t params;
			params.m_pSoundName = soundname;
			params.m_flSoundTime = 0.0f;
			params.m_pflSoundDuration = NULL;
			params.m_bWarnOnDirectWaveReference = true;
			g_pSoundEmitterSystem->EmitSound(filter, pPlayer->entindex(), params);
			//g_pSoundEmitterSystem->EmitSound(pPlayer, args[1]);//pPlayer->
		}
	}
	else
	{
		Msg("Can't play until a game is started.\n");
		// UNDONE: Make something like this work?
		//CBroadcastRecipientFilter filter;
		//g_SoundEmitterSystem.EmitSound( filter, 1, args[1], 0.0, 0, 0, &vec3_origin, 0, NULL );
	}
}

static int GamesoundCompletion(const char* partial, char commands[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH])
{
	int current = 0;

	const char* cmdname = "playgamesound";
	char* substring = NULL;
	int substringLen = 0;
	if (Q_strstr(partial, cmdname) && strlen(partial) > strlen(cmdname) + 1)
	{
		substring = (char*)partial + strlen(cmdname) + 1;
		substringLen = strlen(substring);
	}

	for (int i = soundemitterbase->GetSoundCount() - 1; i >= 0 && current < COMMAND_COMPLETION_MAXITEMS; i--)
	{
		const char* pSoundName = soundemitterbase->GetSoundName(i);
		if (pSoundName)
		{
			if (!substring || !Q_strncasecmp(pSoundName, substring, substringLen))
			{
				Q_snprintf(commands[current], sizeof(commands[current]), "%s %s", cmdname, pSoundName);
				current++;
			}
		}
	}

	return current;
}

static ConCommand Command_Playgamesound("playgamesound", Playgamesound_f, "Play a sound from the game sounds txt file", FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE, GamesoundCompletion);
#endif

#endif