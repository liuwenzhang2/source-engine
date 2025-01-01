//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Utility code.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include <stdarg.h>
#include "shake.h"
#include "decals.h"
#include "player.h"
#include "gamerules.h"
#include "bspfile.h"
#include "mathlib/mathlib.h"
#include "IEffects.h"
#include "vstdlib/random.h"
#include "soundflags.h"
#include "ispatialpartition.h"
#include "igamesystem.h"
#include "saverestoretypes.h"
#include "checksum_crc.h"
#include "hierarchy.h"
#include "game/server/iservervehicle.h"
#include "te_effect_dispatch.h"
#include "utldict.h"
#include "collisionutils.h"
#include "movevars_shared.h"
#include "inetchannelinfo.h"
#include "tier0/vprof.h"
#include "ndebugoverlay.h"
#include "engine/ivdebugoverlay.h"
#include "datacache/imdlcache.h"
#include "util.h"
#include "cdll_int.h"
#include "filesystem.h"

#ifdef PORTAL
#include "PortalSimulation.h"
#include "prop_portal.h"
//#include "Portal_PhysicsEnvironmentMgr.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern short		g_sModelIndexSmoke;			// (in combatweapon.cpp) holds the index for the smoke cloud
extern short		g_sModelIndexBloodDrop;		// (in combatweapon.cpp) holds the sprite index for the initial blood
extern short		g_sModelIndexBloodSpray;	// (in combatweapon.cpp) holds the sprite index for splattered blood

#ifdef	DEBUG
void DBG_AssertFunction( bool fExpr, const char *szExpr, const char *szFile, int szLine, const char *szMessage )
{
	if (fExpr)
		return;
	char szOut[512];
	if (szMessage != NULL)
		Q_snprintf(szOut,sizeof(szOut), "ASSERT FAILED:\n %s \n(%s@%d)\n%s", szExpr, szFile, szLine, szMessage);
	else
		Q_snprintf(szOut,sizeof(szOut), "ASSERT FAILED:\n %s \n(%s@%d)\n", szExpr, szFile, szLine);
	Warning( "%s", szOut );
}
#endif	// DEBUG




void DumpEntityFactories_f()
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	EntityList()->DumpEntityFactories();
}

static ConCommand dumpentityfactories( "dumpentityfactories", DumpEntityFactories_f, "Lists all entity factory names.", FCVAR_GAMEDLL );


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
CON_COMMAND( dump_entity_sizes, "Print sizeof(entclass)" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	EntityList()->ReportEntitySizes();
}

//-----------------------------------------------------------------------------
// class CFlaggedEntitiesEnum
//-----------------------------------------------------------------------------

CFlaggedEntitiesEnum::CFlaggedEntitiesEnum( CBaseEntity **pList, int listMax, int flagMask )
{
	m_pList = pList;
	m_listMax = listMax;
	m_flagMask = flagMask;
	m_count = 0;
}

bool CFlaggedEntitiesEnum::AddToList( CBaseEntity *pEntity )
{
	if ( m_count >= m_listMax )
	{
		AssertMsgOnce( 0, "reached enumerated list limit.  Increase limit, decrease radius, or make it so entity flags will work for you" );
		return false;
	}
	m_pList[m_count] = pEntity;
	m_count++;
	return true;
}

IterationRetval_t CFlaggedEntitiesEnum::EnumElement( IHandleEntity *pHandleEntity )
{
	CBaseEntity *pEntity = EntityList()->GetBaseEntityFromHandle( pHandleEntity->GetRefEHandle() );
	if ( pEntity )
	{
		if ( m_flagMask && !(pEntity->GetEngineObject()->GetFlags() & m_flagMask) )	// Does it meet the criteria?
			return ITERATION_CONTINUE;

		if ( !AddToList( pEntity ) )
			return ITERATION_STOP;
	}

	return ITERATION_CONTINUE;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int UTIL_PrecacheDecal( const char *name, bool preload )
{
	// If this is out of order, make sure to warn.
	if ( !engine->IsPrecacheAllowed() )//CBaseEntity::
	{
		if ( !engine->IsDecalPrecached( name ) )
		{
			Assert( !"UTIL_PrecacheDecal:  too late" );

			Warning( "Late precache of %s\n", name );
		}
	}

	return engine->PrecacheDecal( name, preload );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float UTIL_GetSimulationInterval()
{
	if (EntityList()->IsSimulatingOnAlternateTicks() )
		return ( TICK_INTERVAL * 2.0 );
	return TICK_INTERVAL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int UTIL_EntitiesInBox( const Vector &mins, const Vector &maxs, CFlaggedEntitiesEnum *pEnum )
{
	partition->EnumerateElementsInBox( PARTITION_ENGINE_NON_STATIC_EDICTS, mins, maxs, false, pEnum );
	return pEnum->GetCount();
}

int UTIL_EntitiesAlongRay( const Ray_t &ray, CFlaggedEntitiesEnum *pEnum )
{
	partition->EnumerateElementsAlongRay( PARTITION_ENGINE_NON_STATIC_EDICTS, ray, false, pEnum );
	return pEnum->GetCount();
}

int UTIL_EntitiesInSphere( const Vector &center, float radius, CFlaggedEntitiesEnum *pEnum )
{
	partition->EnumerateElementsInSphere( PARTITION_ENGINE_NON_STATIC_EDICTS, center, radius, false, pEnum );
	return pEnum->GetCount();
}

CEntitySphereQuery::CEntitySphereQuery( const Vector &center, float radius, int flagMask )
{
	m_listIndex = 0;
	m_listCount = UTIL_EntitiesInSphere( m_pList, ARRAYSIZE(m_pList), center, radius, flagMask );
}

CBaseEntity *CEntitySphereQuery::GetCurrentEntity()
{
	if ( m_listIndex < m_listCount )
		return m_pList[m_listIndex];
	return NULL;
}


//-----------------------------------------------------------------------------
// Simple trace filter
//-----------------------------------------------------------------------------
class CTracePassFilter : public CTraceFilter
{
public:
	CTracePassFilter( IHandleEntity *pPassEnt ) : m_pPassEnt( pPassEnt ) {}

	bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		if ( !StandardFilterRules( pHandleEntity, contentsMask ) )
			return false;

		if (!PassServerEntityFilter( pHandleEntity, m_pPassEnt ))
			return false;

		return true;
	}

private:
	IHandleEntity *m_pPassEnt;
};


//-----------------------------------------------------------------------------
// Drops an entity onto the floor
//-----------------------------------------------------------------------------
int UTIL_DropToFloor( CBaseEntity *pEntity, unsigned int mask, CBaseEntity *pIgnore )
{
	// Assume no ground
	pEntity->GetEngineObject()->SetGroundEntity( NULL );

	Assert( pEntity );

	trace_t	trace;

#if !defined(HL2MP) && !defined(HL1_DLL)
	// HACK: is this really the only sure way to detect crossing a terrain boundry?
	EntityList()->GetEngineWorld()->TraceEntity( pEntity->GetEngineObject(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsOrigin(), mask, pIgnore, pEntity->GetEngineObject()->GetCollisionGroup(), &trace);
	if (trace.fraction == 0.0)
		return -1;
#endif // HL2MP

	EntityList()->GetEngineWorld()->TraceEntity( pEntity->GetEngineObject(), pEntity->GetEngineObject()->GetAbsOrigin() + Vector(0, 0, 1), pEntity->GetEngineObject()->GetAbsOrigin() - Vector(0, 0, 256), mask, pIgnore, pEntity->GetEngineObject()->GetCollisionGroup(), &trace);

#ifdef HL1_DLL
	if( fabs(pEntity->GetEngineObject()->GetAbsOrigin().z - trace.endpos.z) <= 2.f )
		return -1;
#endif

	if (trace.allsolid)
		return -1;

	if (trace.fraction == 1)
		return 0;

	pEntity->GetEngineObject()->SetAbsOrigin( trace.endpos );
	pEntity->GetEngineObject()->SetGroundEntity((CBaseEntity*)trace.m_pEnt ? ((CBaseEntity*)trace.m_pEnt)->GetEngineObject() : NULL);

	return 1;
}

//-----------------------------------------------------------------------------
// Returns false if any part of the bottom of the entity is off an edge that
// is not a staircase.
//-----------------------------------------------------------------------------
bool UTIL_CheckBottom( CBaseEntity *pEntity, ITraceFilter *pTraceFilter, float flStepSize )
{
	Vector	mins, maxs, start, stop;
	trace_t	trace;
	int		x, y;
	float	mid, bottom;

	Assert( pEntity );

	CTracePassFilter traceFilter(pEntity);
	if ( !pTraceFilter )
	{
		pTraceFilter = &traceFilter;
	}

	unsigned int mask = pEntity->PhysicsSolidMaskForEntity();

	VectorAdd (pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->WorldAlignMins(), mins);
	VectorAdd (pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->WorldAlignMaxs(), maxs);

	// if all of the points under the corners are solid world, don't bother
	// with the tougher checks
	// the corners must be within 16 of the midpoint
	start[2] = mins[2] - 1;
	for	(x=0 ; x<=1 ; x++)
	{
		for	(y=0 ; y<=1 ; y++)
		{
			start[0] = x ? maxs[0] : mins[0];
			start[1] = y ? maxs[1] : mins[1];
			if (enginetrace->GetPointContents(start) != CONTENTS_SOLID)
				goto realcheck;
		}
	}
	return true;		// we got out easy

realcheck:
	// check it for real...
	start[2] = mins[2] + flStepSize; // seems to help going up/down slopes.
	
	// the midpoint must be within 16 of the bottom
	start[0] = stop[0] = (mins[0] + maxs[0])*0.5;
	start[1] = stop[1] = (mins[1] + maxs[1])*0.5;
	stop[2] = start[2] - 2*flStepSize;
	
	UTIL_TraceLine( start, stop, mask, pTraceFilter, &trace );

	if (trace.fraction == 1.0)
		return false;
	mid = bottom = trace.endpos[2];

	// the corners must be within 16 of the midpoint	
	for	(x=0 ; x<=1 ; x++)
	{
		for	(y=0 ; y<=1 ; y++)
		{
			start[0] = stop[0] = x ? maxs[0] : mins[0];
			start[1] = stop[1] = y ? maxs[1] : mins[1];
			
			UTIL_TraceLine( start, stop, mask, pTraceFilter, &trace );
			
			if (trace.fraction != 1.0 && trace.endpos[2] > bottom)
				bottom = trace.endpos[2];
			if (trace.fraction == 1.0 || mid - trace.endpos[2] > flStepSize)
				return false;
		}
	}
	return true;
}

CBasePlayer* UTIL_PlayerByName( const char *name )
{
	if ( !name || !name[0] )
		return NULL;

	for (int i = 1; i<=gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = ToBasePlayer(EntityList()->GetPlayerByIndex( i ));
		
		if ( !pPlayer )
			continue;

		if ( !pPlayer->IsConnected() )
			continue;

		if ( Q_stricmp( pPlayer->GetPlayerName(), name ) == 0 )
		{
			return pPlayer;
		}
	}
	
	return NULL;
}

CBasePlayer* UTIL_PlayerByUserId( int userID )
{
	for (int i = 1; i<=gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = ToBasePlayer(EntityList()->GetPlayerByIndex( i ));
		
		if ( !pPlayer )
			continue;

		if ( !pPlayer->IsConnected() )
			continue;

		if ( engine->GetPlayerUserId(pPlayer->entindex()) == userID )
		{
			return pPlayer;
		}
	}
	
	return NULL;
}

//
// Get the local player on a listen server - this is for multiplayer use only
// 
CBasePlayer *UTIL_GetListenServerHost( void )
{
	// no "local player" if this is a dedicated server or a single player game
	if (engine->IsDedicatedServer())
	{
		Assert( !"UTIL_GetListenServerHost" );
		Warning( "UTIL_GetListenServerHost() called from a dedicated server or single-player game.\n" );
		return NULL;
	}

	return ToBasePlayer(EntityList()->GetPlayerByIndex( 1 ));
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if the command was issued by the listenserver host, or by the dedicated server, via rcon or the server console.
 * This is valid during ConCommand execution.
 */
bool UTIL_IsCommandIssuedByServerAdmin( void )
{
	int issuingPlayerIndex = UTIL_GetCommandClientIndex();

	if ( engine->IsDedicatedServer() && issuingPlayerIndex > 0 )
		return false;

#if defined( REPLAY_ENABLED )
	// entity 1 is replay?
	player_info_t pi;
	bool bPlayerIsReplay = engine->GetPlayerInfo( 1, &pi ) && pi.isreplay;
#else
	bool bPlayerIsReplay = false;
#endif

	if ( bPlayerIsReplay )
	{
		if ( issuingPlayerIndex > 2 )
			return false;
	}
	else if ( issuingPlayerIndex > 1 )
	{
		return false;
	}

	return true;
}


//int ENTINDEX( CBaseEntity *pEnt )
//{
//	// This works just like ENTINDEX for edicts.
//	if ( pEnt )
//		return pEnt->entindex();
//	else
//		return 0;
//}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : playerIndex - 
//			ping - 
//			packetloss - 
//-----------------------------------------------------------------------------
void UTIL_GetPlayerConnectionInfo( int playerIndex, int& ping, int &packetloss )
{
	CBasePlayer *player = ToBasePlayer(EntityList()->GetPlayerByIndex( playerIndex ));

	INetChannelInfo *nci = engine->GetPlayerNetInfo(playerIndex);

	if ( nci && player && !player->IsBot() )
	{
		float latency = nci->GetAvgLatency( FLOW_OUTGOING ); // in seconds
		
		// that should be the correct latency, we assume that cmdrate is higher 
		// then updaterate, what is the case for default settings
		const char * szCmdRate = engine->GetClientConVarValue( playerIndex, "cl_cmdrate" );
		
		int nCmdRate = MAX( 1, Q_atoi( szCmdRate ) );
		latency -= (0.5f/nCmdRate) + TICKS_TO_TIME( 1.0f ); // correct latency

		// in GoldSrc we had a different, not fixed tickrate. so we have to adjust
		// Source pings by half a tick to match the old GoldSrc pings.
		latency -= TICKS_TO_TIME( 0.5f );

		ping = latency * 1000.0f; // as msecs
		ping = clamp( ping, 5, 1000 ); // set bounds, dont show pings under 5 msecs
		
		packetloss = 100.0f * nci->GetAvgLoss( FLOW_INCOMING ); // loss in percentage
		packetloss = clamp( packetloss, 0, 100 );
	}
	else
	{
		ping = 0;
		packetloss = 0;
	}
}

static unsigned short FixedUnsigned16( float value, float scale )
{
	int output;

	output = value * scale;
	if ( output < 0 )
		output = 0;
	if ( output > 0xFFFF )
		output = 0xFFFF;

	return (unsigned short)output;
}


//-----------------------------------------------------------------------------
// Compute shake amplitude
//-----------------------------------------------------------------------------
inline float ComputeShakeAmplitude( const Vector &center, const Vector &shakePt, float amplitude, float radius ) 
{
	if ( radius <= 0 )
		return amplitude;

	float localAmplitude = -1;
	Vector delta = center - shakePt;
	float distance = delta.Length();

	if ( distance <= radius )
	{
		// Make the amplitude fall off over distance
		float flPerc = 1.0 - (distance / radius);
		localAmplitude = amplitude * flPerc;
	}

	return localAmplitude;
}


//-----------------------------------------------------------------------------
// Transmits the actual shake event
//-----------------------------------------------------------------------------
inline void TransmitShakeEvent( CBasePlayer *pPlayer, float localAmplitude, float frequency, float duration, ShakeCommand_t eCommand )
{
	if (( localAmplitude > 0 ) || ( eCommand == SHAKE_STOP ))
	{
		if ( eCommand == SHAKE_STOP )
			localAmplitude = 0;

		CSingleUserRecipientFilter user( pPlayer );
		user.MakeReliable();
		UserMessageBegin( user, "Shake" );
			WRITE_BYTE( eCommand );				// shake command (SHAKE_START, STOP, FREQUENCY, AMPLITUDE)
			WRITE_FLOAT( localAmplitude );			// shake magnitude/amplitude
			WRITE_FLOAT( frequency );				// shake noise frequency
			WRITE_FLOAT( duration );				// shake lasts this long
		MessageEnd();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Shake the screen of all clients within radius.
//			radius == 0, shake all clients
// UNDONE: Fix falloff model (disabled)?
// UNDONE: Affect user controls?
// Input  : center - Center of screen shake, radius is measured from here.
//			amplitude - Amplitude of shake
//			frequency - 
//			duration - duration of shake in seconds.
//			radius - Radius of effect, 0 shakes all clients.
//			command - One of the following values:
//				SHAKE_START - starts the screen shake for all players within the radius
//				SHAKE_STOP - stops the screen shake for all players within the radius
//				SHAKE_AMPLITUDE - modifies the amplitude of the screen shake
//									for all players within the radius
//				SHAKE_FREQUENCY - modifies the frequency of the screen shake
//									for all players within the radius
//			bAirShake - if this is false, then it will only shake players standing on the ground.
//-----------------------------------------------------------------------------
const float MAX_SHAKE_AMPLITUDE = 16.0f;
void UTIL_ScreenShake( const Vector &center, float amplitude, float frequency, float duration, float radius, ShakeCommand_t eCommand, bool bAirShake )
{
	int			i;
	float		localAmplitude;

	if ( amplitude > MAX_SHAKE_AMPLITUDE )
	{
		amplitude = MAX_SHAKE_AMPLITUDE;
	}
	for ( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pPlayer = EntityList()->GetPlayerByIndex( i );

		//
		// Only start shakes for players that are on the ground unless doing an air shake.
		//
		if ( !pPlayer || (!bAirShake && (eCommand == SHAKE_START) && !(pPlayer->GetEngineObject()->GetFlags() & FL_ONGROUND)) )
		{
			continue;
		}

		localAmplitude = ComputeShakeAmplitude( center, pPlayer->WorldSpaceCenter(), amplitude, radius );

		// This happens if the player is outside the radius, in which case we should ignore 
		// all commands
		if (localAmplitude < 0)
			continue;

		TransmitShakeEvent( (CBasePlayer *)pPlayer, localAmplitude, frequency, duration, eCommand );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Shake an object and all players on or near it
//-----------------------------------------------------------------------------
void UTIL_ScreenShakeObject( CBaseEntity *pEnt, const Vector &center, float amplitude, float frequency, float duration, float radius, ShakeCommand_t eCommand, bool bAirShake )
{
	int			i;
	float		localAmplitude;

	CBaseEntity *pHighestParent = pEnt->GetEngineObject()->GetRootMoveParent()->GetOuter();
	for ( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pPlayer = EntityList()->GetPlayerByIndex( i );
		if (!pPlayer)
			continue;

		// Shake the object, or anything hierarchically attached to it at maximum amplitude
		localAmplitude = 0;
		if (pHighestParent == pPlayer->GetEngineObject()->GetRootMoveParent()->GetOuter())
		{
			localAmplitude = amplitude;
		}
		else if ((pPlayer->GetEngineObject()->GetFlags() & FL_ONGROUND) && (pPlayer->GetEngineObject()->GetGroundEntity()->GetRootMoveParent()->GetOuter() == pHighestParent))
		{
			// If the player is standing on the object, use maximum amplitude
			localAmplitude = amplitude;
		}
		else
		{
			// Only shake players that are on the ground.
			if ( !bAirShake && !(pPlayer->GetEngineObject()->GetFlags() & FL_ONGROUND) )
			{
				continue;
			}

			if ( radius > 0 )
			{
				localAmplitude = ComputeShakeAmplitude( center, pPlayer->WorldSpaceCenter(), amplitude, radius );
			}
			else
			{
				// If using a 0 radius, apply to everyone with no falloff
				localAmplitude = amplitude;
			}

			// This happens if the player is outside the radius, 
			// in which case we should ignore all commands
			if (localAmplitude < 0)
				continue;
		}

		TransmitShakeEvent( (CBasePlayer *)pPlayer, localAmplitude, frequency, duration, eCommand );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Punches the view of all clients within radius.
//			If radius is 0, punches all clients.
// Input  : center - Center of punch, radius is measured from here.
//			radius - Radius of effect, 0 punches all clients.
//			bInAir - if this is false, then it will only punch players standing on the ground.
//-----------------------------------------------------------------------------
void UTIL_ViewPunch( const Vector &center, QAngle angPunch, float radius, bool bInAir )
{
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pPlayer = EntityList()->GetPlayerByIndex( i );

		//
		// Only apply the punch to players that are on the ground unless doing an air punch.
		//
		if ( !pPlayer || (!bInAir && !(pPlayer->GetEngineObject()->GetFlags() & FL_ONGROUND)) )
		{
			continue;
		}

		QAngle angTemp = angPunch;

		if ( radius > 0 )
		{
			Vector delta = center - pPlayer->GetEngineObject()->GetAbsOrigin();
			float distance = delta.Length();

			if ( distance <= radius )
			{
				// Make the punch amplitude fall off over distance.
				float flPerc = 1.0 - (distance / radius);
				angTemp *= flPerc;
			}
			else
			{
				continue;
			}
		}
		
		pPlayer->ViewPunch( angTemp );
	}
}


void UTIL_ScreenFadeBuild( ScreenFade_t &fade, const color32 &color, float fadeTime, float fadeHold, int flags )
{
	fade.duration = FixedUnsigned16( fadeTime, 1<<SCREENFADE_FRACBITS );		// 7.9 fixed
	fade.holdTime = FixedUnsigned16( fadeHold, 1<<SCREENFADE_FRACBITS );		// 7.9 fixed
	fade.r = color.r;
	fade.g = color.g;
	fade.b = color.b;
	fade.a = color.a;
	fade.fadeFlags = flags;
}


void UTIL_ScreenFadeWrite( const ScreenFade_t &fade, CBaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsNetClient() )
		return;

	CSingleUserRecipientFilter user( (CBasePlayer *)pEntity );
	user.MakeReliable();

	UserMessageBegin( user, "Fade" );		// use the magic #1 for "one client"
		WRITE_SHORT( fade.duration );		// fade lasts this long
		WRITE_SHORT( fade.holdTime );		// fade lasts this long
		WRITE_SHORT( fade.fadeFlags );		// fade type (in / out)
		WRITE_BYTE( fade.r );				// fade red
		WRITE_BYTE( fade.g );				// fade green
		WRITE_BYTE( fade.b );				// fade blue
		WRITE_BYTE( fade.a );				// fade blue
	MessageEnd();
}


void UTIL_ScreenFadeAll( const color32 &color, float fadeTime, float fadeHold, int flags )
{
	int			i;
	ScreenFade_t	fade;


	UTIL_ScreenFadeBuild( fade, color, fadeTime, fadeHold, flags );

	for ( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pPlayer = EntityList()->GetPlayerByIndex( i );
	
		UTIL_ScreenFadeWrite( fade, pPlayer );
	}
}


void UTIL_ScreenFade( CBaseEntity *pEntity, const color32 &color, float fadeTime, float fadeHold, int flags )
{
	ScreenFade_t	fade;

	UTIL_ScreenFadeBuild( fade, color, fadeTime, fadeHold, flags );
	UTIL_ScreenFadeWrite( fade, pEntity );
}


void UTIL_HudMessage( CBasePlayer *pToPlayer, const hudtextparms_t &textparms, const char *pMessage )
{
	CRecipientFilter filter;
	
	if( pToPlayer )
	{
		filter.AddRecipient( pToPlayer );
	}
	else
	{
		filter.AddAllPlayers();
	}

	filter.MakeReliable();

	UserMessageBegin( filter, "HudMsg" );
		WRITE_BYTE ( textparms.channel & 0xFF );
		WRITE_FLOAT( textparms.x );
		WRITE_FLOAT( textparms.y );
		WRITE_BYTE ( textparms.r1 );
		WRITE_BYTE ( textparms.g1 );
		WRITE_BYTE ( textparms.b1 );
		WRITE_BYTE ( textparms.a1 );
		WRITE_BYTE ( textparms.r2 );
		WRITE_BYTE ( textparms.g2 );
		WRITE_BYTE ( textparms.b2 );
		WRITE_BYTE ( textparms.a2 );
		WRITE_BYTE ( textparms.effect );
		WRITE_FLOAT( textparms.fadeinTime );
		WRITE_FLOAT( textparms.fadeoutTime );
		WRITE_FLOAT( textparms.holdTime );
		WRITE_FLOAT( textparms.fxTime );
		WRITE_STRING( pMessage );
	MessageEnd();
}

void UTIL_HudMessageAll( const hudtextparms_t &textparms, const char *pMessage )
{
	UTIL_HudMessage( NULL, textparms, pMessage );
}

void UTIL_HudHintText( CBaseEntity *pEntity, const char *pMessage )
{
	if ( !pEntity )
		return;

	CSingleUserRecipientFilter user( (CBasePlayer *)pEntity );
	user.MakeReliable();
	UserMessageBegin( user, "KeyHintText" );
		WRITE_BYTE( 1 );	// one string
		WRITE_STRING( pMessage );
	MessageEnd();
}

void UTIL_ClientPrintFilter( IRecipientFilter& filter, int msg_dest, const char *msg_name, const char *param1, const char *param2, const char *param3, const char *param4 )
{
	UserMessageBegin( filter, "TextMsg" );
		WRITE_BYTE( msg_dest );
		WRITE_STRING( msg_name );

		if ( param1 )
			WRITE_STRING( param1 );
		else
			WRITE_STRING( "" );

		if ( param2 )
			WRITE_STRING( param2 );
		else
			WRITE_STRING( "" );

		if ( param3 )
			WRITE_STRING( param3 );
		else
			WRITE_STRING( "" );

		if ( param4 )
			WRITE_STRING( param4 );
		else
			WRITE_STRING( "" );

	MessageEnd();
}
					 
void UTIL_ClientPrintAll( int msg_dest, const char *msg_name, const char *param1, const char *param2, const char *param3, const char *param4 )
{
	CReliableBroadcastRecipientFilter filter;

	UTIL_ClientPrintFilter( filter, msg_dest, msg_name, param1, param2, param3, param4 );
}

void ClientPrint( CBasePlayer *player, int msg_dest, const char *msg_name, const char *param1, const char *param2, const char *param3, const char *param4 )
{
	if ( !player )
		return;

	CSingleUserRecipientFilter user( player );
	user.MakeReliable();

	UTIL_ClientPrintFilter( user, msg_dest, msg_name, param1, param2, param3, param4 );
}

void UTIL_SayTextFilter( IRecipientFilter& filter, const char *pText, CBasePlayer *pPlayer, bool bChat )
{
	UserMessageBegin( filter, "SayText" );
		if ( pPlayer ) 
		{
			WRITE_BYTE( pPlayer->entindex() );
		}
		else
		{
			WRITE_BYTE( 0 ); // world, dedicated server says
		}
		WRITE_STRING( pText );
		WRITE_BYTE( bChat );
	MessageEnd();
}

void UTIL_SayText2Filter( IRecipientFilter& filter, CBasePlayer *pEntity, bool bChat, const char *msg_name, const char *param1, const char *param2, const char *param3, const char *param4 )
{
	UserMessageBegin( filter, "SayText2" );
		if ( pEntity )
		{
			WRITE_BYTE( pEntity->entindex() );
		}
		else
		{
			WRITE_BYTE( 0 ); // world, dedicated server says
		}

		WRITE_BYTE( bChat );

		WRITE_STRING( msg_name );

		if ( param1 )
			WRITE_STRING( param1 );
		else
			WRITE_STRING( "" );

		if ( param2 )
			WRITE_STRING( param2 );
		else
			WRITE_STRING( "" );

		if ( param3 )
			WRITE_STRING( param3 );
		else
			WRITE_STRING( "" );

		if ( param4 )
			WRITE_STRING( param4 );
		else
			WRITE_STRING( "" );

	MessageEnd();
}

void UTIL_SayText( const char *pText, CBasePlayer *pToPlayer )
{
	if ( !pToPlayer->IsNetClient() )
		return;

	CSingleUserRecipientFilter user( pToPlayer );
	user.MakeReliable();

	UTIL_SayTextFilter( user, pText, pToPlayer, false );
}

void UTIL_SayTextAll( const char *pText, CBasePlayer *pPlayer, bool bChat )
{
	CReliableBroadcastRecipientFilter filter;
	UTIL_SayTextFilter( filter, pText, pPlayer, bChat );
}

void UTIL_ShowMessage( const char *pString, CBasePlayer *pPlayer )
{
	CRecipientFilter filter;

	if ( pPlayer )
	{
		filter.AddRecipient( pPlayer );
	}
	else
	{
		filter.AddAllPlayers();
	}
	
	filter.MakeReliable();

	UserMessageBegin( filter, "HudText" );
		WRITE_STRING( pString );
	MessageEnd();
}


void UTIL_ShowMessageAll( const char *pString )
{
	UTIL_ShowMessage( pString, NULL );
}

// So we always return a valid surface
static csurface_t	g_NullSurface = { "**empty**", 0 };

void UTIL_SetTrace(trace_t& trace, const Ray_t &ray, CBaseEntity *ent, float fraction, 
				   int hitgroup, unsigned int contents, const Vector& normal, float intercept )
{
	trace.startsolid = (fraction == 0.0f);
	trace.fraction = fraction;
	VectorCopy( ray.m_Start, trace.startpos );
	VectorMA( ray.m_Start, fraction, ray.m_Delta, trace.endpos );
	VectorCopy( normal, trace.plane.normal );
	trace.plane.dist = intercept;
	trace.m_pEnt = ent;
	trace.hitgroup = hitgroup;
	trace.surface =	g_NullSurface;
	trace.contents = contents;
}

void UTIL_ClearTrace( trace_t &trace )
{
	memset( &trace, 0, sizeof(trace));
	trace.fraction = 1.f;
	trace.fractionleftsolid = 0;
	trace.surface = g_NullSurface;
}
	
//-----------------------------------------------------------------------------
// Sets the model to be associated with an entity
//-----------------------------------------------------------------------------
void UTIL_SetModel( CBaseEntity *pEntity, const char *pModelName )
{
	// check to see if model was properly precached
	int i = modelinfo->GetModelIndex( pModelName );
	if ( i == -1 )	
	{
		Error("%i/%s - %s:  UTIL_SetModel:  not precached: %s\n", pEntity->entindex(),
			STRING( pEntity->GetEntityName() ),
			pEntity->GetClassname(), pModelName);
	}

	CBaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pAnimating )
	{
		pAnimating->GetEngineObject()->SetForceBone(0);
	}

	pEntity->GetEngineObject()->SetModelName( AllocPooledString( pModelName ) );
	pEntity->GetEngineObject()->SetModelIndex( i ) ;
	pEntity->GetEngineObject()->SetSize(vec3_origin, vec3_origin);
	pEntity->SetCollisionBoundsFromModel();
}

	
void UTIL_SetOrigin( CBaseEntity *entity, const Vector &vecOrigin, bool bFireTriggers )
{
	entity->GetEngineObject()->SetLocalOrigin( vecOrigin );
	if ( bFireTriggers )
	{
		entity->GetEngineObject()->PhysicsTouchTriggers();
	}
}


void UTIL_ParticleEffect( const Vector &vecOrigin, const Vector &vecDirection, ULONG ulColor, ULONG ulCount )
{
	Msg( "UTIL_ParticleEffect:  Disabled\n" );
}

void UTIL_Smoke( const Vector &origin, const float scale, const float framerate )
{
	g_pEffects->Smoke( origin, g_sModelIndexSmoke, scale, framerate );
}

// snaps a vector to the nearest axis vector (if within epsilon)
void UTIL_SnapDirectionToAxis( Vector &direction, float epsilon )
{
	float proj = 1 - epsilon;
	for ( int i = 0; i < 3; i ++ )
	{
		if ( fabs(direction[i]) > proj )
		{
			// snap to axis unit vector
			if ( direction[i] < 0 )
				direction[i] = -1.0f;
			else
				direction[i] = 1.0f;
			direction[(i+1)%3] = 0;
			direction[(i+2)%3] = 0;
			return;
		}
	}
}

bool UTIL_IsMasterTriggered(string_t sMaster, CBaseEntity *pActivator)
{
	if (sMaster != NULL_STRING)
	{
		CBaseEntity *pMaster = EntityList()->FindEntityByName( NULL, sMaster, NULL, pActivator );
	
		if ( pMaster && (pMaster->ObjectCaps() & FCAP_MASTER) )
		{
			return pMaster->IsTriggered( pActivator );
		}

		Warning( "Master was null or not a master!\n");
	}

	// if this isn't a master entity, just say yes.
	return true;
}

void UTIL_BloodStream( const Vector &origin, const Vector &direction, int color, int amount )
{
	if ( !UTIL_ShouldShowBlood( color ) )
		return;

	if ( g_Language.GetInt() == LANGUAGE_GERMAN && color == BLOOD_COLOR_RED )
		color = 0;

	CPVSFilter filter( origin );
	te->BloodStream( filter, 0.0, &origin, &direction, 247, 63, 14, 255, MIN( amount, 255 ) );
}				


Vector UTIL_RandomBloodVector( void )
{
	Vector direction;

	direction.x = random->RandomFloat ( -1, 1 );
	direction.y = random->RandomFloat ( -1, 1 );
	direction.z = random->RandomFloat ( 0, 1 );

	return direction;
}


//------------------------------------------------------------------------------
// Purpose : Creates both an decal and any associated impact effects (such
//			 as flecks) for the given iDamageType and the trace's end position
// Input   :
// Output  :
//------------------------------------------------------------------------------
void UTIL_ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName )
{
	CBaseEntity *pEntity = (CBaseEntity*)pTrace->m_pEnt;

	// Is the entity valid, is the surface sky?
	if ( !pEntity || !UTIL_IsValidEntity( pEntity ) || (pTrace->surface.flags & SURF_SKY) )
		return;

	if ( pTrace->fraction == 1.0 )
		return;

	pEntity->ImpactTrace( pTrace, iDamageType, pCustomImpactName );
}

/*
==============
UTIL_PlayerDecalTrace

A player is trying to apply his custom decal for the spray can.
Tell connected clients to display it, or use the default spray can decal
if the custom can't be loaded.
==============
*/
void UTIL_PlayerDecalTrace( trace_t *pTrace, int playernum )
{
	if (pTrace->fraction == 1.0)
		return;

	CBroadcastRecipientFilter filter;

	te->PlayerDecal( filter, 0.0,
		&pTrace->endpos, playernum, ((CBaseEntity*)pTrace->m_pEnt)->entindex() );
}

bool UTIL_TeamsMatch( const char *pTeamName1, const char *pTeamName2 )
{
	// Everyone matches unless it's teamplay
	if ( !g_pGameRules->IsTeamplay() )
		return true;

	// Both on a team?
	if ( *pTeamName1 != 0 && *pTeamName2 != 0 )
	{
		if ( !stricmp( pTeamName1, pTeamName2 ) )	// Same Team?
			return true;
	}

	return false;
}


void UTIL_AxisStringToPointPoint( Vector &start, Vector &end, const char *pString )
{
	char tmpstr[256];

	Q_strncpy( tmpstr, pString, sizeof(tmpstr) );
	char *pVec = strtok( tmpstr, "," );
	int i = 0;
	while ( pVec != NULL && *pVec )
	{
		if ( i == 0 )
		{
			UTIL_StringToVector( start.Base(), pVec );
			i++;
		}
		else
		{
			UTIL_StringToVector( end.Base(), pVec );
		}
		pVec = strtok( NULL, "," );
	}
}

void UTIL_AxisStringToPointDir( Vector &start, Vector &dir, const char *pString )
{
	Vector end;
	UTIL_AxisStringToPointPoint( start, end, pString );
	dir = end - start;
	VectorNormalize(dir);
}

void UTIL_AxisStringToUnitDir( Vector &dir, const char *pString )
{
	Vector start;
	UTIL_AxisStringToPointDir( start, dir, pString );
}

/*
==================================================
UTIL_ClipPunchAngleOffset
==================================================
*/

void UTIL_ClipPunchAngleOffset( QAngle &in, const QAngle &punch, const QAngle &clip )
{
	QAngle	final = in + punch;

	//Clip each component
	for ( int i = 0; i < 3; i++ )
	{
		if ( final[i] > clip[i] )
		{
			final[i] = clip[i];
		}
		else if ( final[i] < -clip[i] )
		{
			final[i] = -clip[i];
		}

		//Return the result
		in[i] = final[i] - punch[i];
	}
}

float UTIL_WaterLevel( const Vector &position, float minz, float maxz )
{
	Vector midUp = position;
	midUp.z = minz;

	if ( !(UTIL_PointContents(midUp) & MASK_WATER) )
		return minz;

	midUp.z = maxz;
	if ( UTIL_PointContents(midUp) & MASK_WATER )
		return maxz;

	float diff = maxz - minz;
	while (diff > 1.0)
	{
		midUp.z = minz + diff/2.0;
		if ( UTIL_PointContents(midUp) & MASK_WATER )
		{
			minz = midUp.z;
		}
		else
		{
			maxz = midUp.z;
		}
		diff = maxz - minz;
	}

	return midUp.z;
}


//-----------------------------------------------------------------------------
// Like UTIL_WaterLevel, but *way* less expensive.
// I didn't replace UTIL_WaterLevel everywhere to avoid breaking anything.
//-----------------------------------------------------------------------------
class CWaterTraceFilter : public CTraceFilter
{
public:
	bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		CBaseEntity *pCollide = EntityFromEntityHandle( pHandleEntity );

		// Static prop case...
		if ( !pCollide )
			return false;

		// Only impact water stuff...
		if ( pCollide->GetEngineObject()->GetSolidFlags() & FSOLID_VOLUME_CONTENTS )
			return true;

		return false;
	}
};

float UTIL_FindWaterSurface( const Vector &position, float minz, float maxz )
{
	Vector vecStart, vecEnd;
	vecStart.Init( position.x, position.y, maxz );
	vecEnd.Init( position.x, position.y, minz );

	Ray_t ray;
	trace_t tr;
	CWaterTraceFilter waterTraceFilter;
	ray.Init( vecStart, vecEnd );
	enginetrace->TraceRay( ray, MASK_WATER, &waterTraceFilter, &tr );

	return tr.endpos.z;
}


extern short	g_sModelIndexBubbles;// holds the index for the bubbles model

void UTIL_Bubbles( const Vector& mins, const Vector& maxs, int count )
{
	Vector mid =  (mins + maxs) * 0.5;

	float flHeight = UTIL_WaterLevel( mid,  mid.z, mid.z + 1024 );
	flHeight = flHeight - mins.z;

	CPASFilter filter( mid );

	te->Bubbles( filter, 0.0,
		&mins, &maxs, flHeight, g_sModelIndexBubbles, count, 8.0 );
}

void UTIL_BubbleTrail( const Vector& from, const Vector& to, int count )
{
	// Find water surface will return from.z if the from point is above water
	float flStartHeight = UTIL_FindWaterSurface( from, from.z, from.z + 256 );
	flStartHeight = flStartHeight - from.z;

	float flEndHeight = UTIL_FindWaterSurface( to, to.z, to.z + 256 );
	flEndHeight = flEndHeight - to.z;

	if ( ( flStartHeight == 0 ) && ( flEndHeight == 0 ) )
		return;

	float flWaterZ = flStartHeight + from.z;

	const Vector *pFrom = &from;
	const Vector *pTo = &to;
	Vector vecWaterPoint;
	if ( ( flStartHeight == 0 ) || ( flEndHeight == 0 ) )
	{
		if ( flStartHeight == 0 )
		{
			flWaterZ = flEndHeight + to.z;
		}

		float t = IntersectRayWithAAPlane( from, to, 2, 1.0f, flWaterZ );
		Assert( (t >= -1e-3f) && ( t <= 1.0f ) );
		VectorLerp( from, to, t, vecWaterPoint );
		if ( flStartHeight == 0 )
		{
			pFrom = &vecWaterPoint;

			// Reduce the count by the actual length
			count = (int)( count * ( 1.0f - t ) );
		}
		else
		{
			pTo = &vecWaterPoint;

			// Reduce the count by the actual length
			count = (int)( count * t );
		}
	}

	CBroadcastRecipientFilter filter;
	te->BubbleTrail( filter, 0.0, pFrom, pTo, flWaterZ, g_sModelIndexBubbles, count, 8.0 );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : Start - 
//			End - 
//			ModelIndex - 
//			FrameStart - 
//			FrameRate - 
//			Life - 
//			Width - 
//			Noise - 
//			Red - 
//			Green - 
//			Brightness - 
//			Speed - 
//-----------------------------------------------------------------------------
void UTIL_Beam( Vector &Start, Vector &End, int nModelIndex, int nHaloIndex, unsigned char FrameStart, unsigned char FrameRate,
				float Life, unsigned char Width, unsigned char EndWidth, unsigned char FadeLength, unsigned char Noise, unsigned char Red, unsigned char Green,
				unsigned char Blue, unsigned char Brightness, unsigned char Speed)
{
	CBroadcastRecipientFilter filter;

	te->BeamPoints( filter, 0.0,
		&Start, 
		&End, 
		nModelIndex, 
		nHaloIndex, 
		FrameStart,
		FrameRate, 
		Life,
		Width,
		EndWidth,
		FadeLength,
		Noise,
		Red,
		Green,
		Blue,
		Brightness,
		Speed );
}

bool UTIL_IsValidEntity( CBaseEntity *pEnt )
{
	//edict_t *pEdict = pEnt->edict();
	if ( pEnt->entindex()==-1  )//|| pEdict->IsFree()
		return false;
	return true;
}


#define PRECACHE_OTHER_ONCE
// UNDONE: Do we need this to avoid doing too much of this?  Measure startup times and see
#if defined( PRECACHE_OTHER_ONCE )

#include "utlsymbol.h"
class CPrecacheOtherList : public CAutoGameSystem
{
public:
	CPrecacheOtherList( char const *name ) : CAutoGameSystem( name )
	{
	}
	virtual void LevelInitPreEntity();
	virtual void LevelShutdownPostEntity();

	bool AddOrMarkPrecached( const char *pClassname );

private:
	CUtlSymbolTable		m_list;
};

void CPrecacheOtherList::LevelInitPreEntity()
{
	m_list.RemoveAll();
}

void CPrecacheOtherList::LevelShutdownPostEntity()
{
	m_list.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: mark or add
// Input  : *pEntity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CPrecacheOtherList::AddOrMarkPrecached( const char *pClassname )
{
	CUtlSymbol sym = m_list.Find( pClassname );
	if ( sym.IsValid() )
		return false;

	m_list.AddString( pClassname );
	return true;
}

CPrecacheOtherList g_PrecacheOtherList( "CPrecacheOtherList" );
#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *szClassname - 
//			*modelName - 
//-----------------------------------------------------------------------------
void UTIL_PrecacheOther( const char *szClassname, const char *modelName )
{
#if defined( PRECACHE_OTHER_ONCE )
	// already done this one?, if not, mark as done
	if ( !g_PrecacheOtherList.AddOrMarkPrecached( szClassname ) )
		return;
#endif

	CBaseEntity	*pEntity = (CBaseEntity*)EntityList()->CreateEntityByName( szClassname );
	if ( !pEntity )
	{
		Warning( "NULL Ent in UTIL_PrecacheOther\n" );
		return;
	}

	// If we have a specified model, set it before calling precache
	if ( modelName && modelName[0] )
	{
		pEntity->GetEngineObject()->SetModelName( AllocPooledString( modelName ) );
	}
	
	if (pEntity)
		pEntity->Precache( );

	EntityList()->DestroyEntityImmediate( pEntity );
}

//=========================================================
// UTIL_LogPrintf - Prints a logged message to console.
// Preceded by LOG: ( timestamp ) < message >
//=========================================================
void UTIL_LogPrintf( const char *fmt, ... )
{
	va_list		argptr;
	char		tempString[1024];
	
	va_start ( argptr, fmt );
	Q_vsnprintf( tempString, sizeof(tempString), fmt, argptr );
	va_end   ( argptr );

	// Print to server console
	engine->LogPrint( tempString );
}

//=========================================================
// UTIL_DotPoints - returns the dot product of a line from
// src to check and vecdir.
//=========================================================
float UTIL_DotPoints ( const Vector &vecSrc, const Vector &vecCheck, const Vector &vecDir )
{
	Vector2D	vec2LOS;

	vec2LOS = ( vecCheck - vecSrc ).AsVector2D();
	Vector2DNormalize( vec2LOS );

	return DotProduct2D(vec2LOS, vecDir.AsVector2D());
}


//=========================================================
// UTIL_StripToken - for redundant keynames
//=========================================================
void UTIL_StripToken( const char *pKey, char *pDest )
{
	int i = 0;

	while ( pKey[i] && pKey[i] != '#' )
	{
		pDest[i] = pKey[i];
		i++;
	}
	pDest[i] = 0;
}


// computes gravity scale for an absolute gravity.  Pass the result into CBaseEntity::SetGravity()
float UTIL_ScaleForGravity( float desiredGravity )
{
	float worldGravity = GetCurrentGravity();
	return worldGravity > 0 ? desiredGravity / worldGravity : 0;
}


//-----------------------------------------------------------------------------
// Purpose: Implemented for mathlib.c error handling
// Input  : *error - 
//-----------------------------------------------------------------------------
extern "C" void Sys_Error( char *error, ... )
{
	va_list	argptr;
	char	string[1024];
	
	va_start( argptr, error );
	Q_vsnprintf( string, sizeof(string), error, argptr );
	va_end( argptr );

	Warning( "%s", string );
	Assert(0);
}


//-----------------------------------------------------------------------------
// Purpose: Spawns an entity into the game, initializing it with the map ent data block
// Input  : *pEntity - the newly created entity
//			*mapData - pointer a block of entity map data
// Output : -1 if the entity was not successfully created; 0 on success
//-----------------------------------------------------------------------------
int DispatchSpawn( CBaseEntity *pEntity )
{
	if ( pEntity )
	{
		MDLCACHE_CRITICAL_SECTION();

		// keep a smart pointer that will now if the object gets deleted
		EHANDLE pEntSafe;
		pEntSafe = pEntity;

		// Initialize these or entities who don't link to the world won't have anything in here
		// is this necessary?
		//pEntity->SetAbsMins( pEntity->GetOrigin() - Vector(1,1,1) );
		//pEntity->SetAbsMaxs( pEntity->GetOrigin() + Vector(1,1,1) );

#if defined(TRACK_ENTITY_MEMORY) && defined(USE_MEM_DEBUG)
		const char *pszClassname = EntityList()->GetCannonicalName(pEntity->GetClassname());
		if ( pszClassname )
		{
			MemAlloc_PushAllocDbgInfo( pszClassname, __LINE__ );
		}
#endif
		bool bAsyncAnims = mdlcache->SetAsyncLoad( MDLCACHE_ANIMBLOCK, false );
		CBaseAnimating *pAnimating = pEntity->GetBaseAnimating();
		if (!pAnimating)
		{
			pEntity->Spawn();
		}
		else
		{
			// Don't allow the PVS check to skip animation setup during spawning
			pAnimating->GetEngineObject()->SetBoneCacheFlags( BCF_IS_IN_SPAWN );
			pEntity->Spawn();
			if ( pEntSafe != NULL )
				pAnimating->GetEngineObject()->ClearBoneCacheFlags( BCF_IS_IN_SPAWN );
		}
		mdlcache->SetAsyncLoad( MDLCACHE_ANIMBLOCK, bAsyncAnims );

#if defined(TRACK_ENTITY_MEMORY) && defined(USE_MEM_DEBUG)
		if ( pszClassname )
		{
			MemAlloc_PopAllocDbgInfo();
		}
#endif
		// Try to get the pointer again, in case the spawn function deleted the entity.
		// UNDONE: Spawn() should really return a code to ask that the entity be deleted, but
		// that would touch too much code for me to do that right now.

		if ( pEntSafe == NULL || pEntity->GetEngineObject()->IsMarkedForDeletion() )
			return -1;

		if ( pEntity->GetEngineObject()->GetGlobalname() != NULL_STRING )
		{
			// Handle global stuff here
			int globalIndex = engine->GlobalEntity_GetIndex( pEntity->GetEngineObject()->GetGlobalname() );
			if ( globalIndex >= 0 )
			{
				// Already dead? delete
				if (engine->GlobalEntity_GetState(globalIndex) == GLOBAL_DEAD )
				{
					pEntity->Release();
					return -1;
				}
				else if ( !FStrEq(STRING(gpGlobals->mapname), engine->GlobalEntity_GetMap(globalIndex)) )
				{
					pEntity->MakeDormant();	// Hasn't been moved to this level yet, wait but stay alive
				}
				// In this level & not dead, continue on as normal
			}
			else
			{
				// Spawned entities default to 'On'
				engine->GlobalEntity_Add( pEntity->GetEngineObject()->GetGlobalname(), gpGlobals->mapname, GLOBAL_ON);
//					Msg( "Added global entity %s (%s)\n", pEntity->GetClassname(), STRING(pEntity->m_iGlobalname) );
			}
		}

		EntityList()->NotifySpawn( pEntity );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Purpose: Initialize the matrix from an entity
// Input  : *pEntity - 
//-----------------------------------------------------------------------------
void EntityMatrix::InitFromEntity( CBaseEntity *pEntity, int iAttachment )
{
	if ( !pEntity )
	{
		Identity();
		return;
	}

	// Get an attachment's matrix?
	if ( iAttachment != 0 )
	{
		CBaseAnimating *pAnimating = pEntity->GetBaseAnimating();
		if ( pAnimating && pAnimating->GetEngineObject()->GetModelPtr() )
		{
			Vector vOrigin;
			QAngle vAngles;
			if ( pAnimating->GetEngineObject()->GetAttachment( iAttachment, vOrigin, vAngles ) )
			{
				((VMatrix *)this)->SetupMatrixOrgAngles( vOrigin, vAngles );
				return;
			}
		}
	}

	((VMatrix *)this)->SetupMatrixOrgAngles( pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles() );
}


void EntityMatrix::InitFromEntityLocal( CBaseEntity *entity )
{
	if ( !entity || entity->entindex()==-1 )
	{
		Identity();
		return;
	}
	((VMatrix *)this)->SetupMatrixOrgAngles( entity->GetEngineObject()->GetLocalOrigin(), entity->GetEngineObject()->GetLocalAngles() );
}

//==================================================
// Purpose: 
// Input: 
// Output: 
//==================================================

void UTIL_ValidateSoundName( string_t &name, const char *defaultStr )
{
	if ( ( !name || 
		   strlen( (char*) STRING( name ) ) < 1 ) || 
		   !Q_stricmp( (char *)STRING(name), "0" ) )
	{
		name = AllocPooledString( defaultStr );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Slightly modified strtok. Does not modify the input string. Does
//			not skip over more than one separator at a time. This allows parsing
//			strings where tokens between separators may or may not be present:
//
//			Door01,,,0 would be parsed as "Door01"  ""  ""  "0"
//			Door01,Open,,0 would be parsed as "Door01"  "Open"  ""  "0"
//
// Input  : token - Returns with a token, or zero length if the token was missing.
//			str - String to parse.
//			sep - Character to use as separator. UNDONE: allow multiple separator chars
// Output : Returns a pointer to the next token to be parsed.
//-----------------------------------------------------------------------------
const char *nexttoken(char *token, const char *str, char sep)
{
	if ((str == NULL) || (*str == '\0'))
	{
		*token = '\0';
		return(NULL);
	}

	//
	// Copy everything up to the first separator into the return buffer.
	// Do not include separators in the return buffer.
	//
	while ((*str != sep) && (*str != '\0'))
	{
		*token++ = *str++;
	}
	*token = '\0';

	//
	// Advance the pointer unless we hit the end of the input string.
	//
	if (*str == '\0')
	{
		return(str);
	}

	return(++str);
}

//-----------------------------------------------------------------------------
// Purpose: Get the predicted postion of an entity of a certain number of seconds
//			Use this function with caution, it has great potential for annoying the player, especially
//			if used for target firing predition
// Input  : *pTarget - target entity to predict
//			timeDelta - amount of time to predict ahead (in seconds)
//			&vecPredictedPosition - output
//-----------------------------------------------------------------------------
void UTIL_PredictedPosition( CBaseEntity *pTarget, float flTimeDelta, Vector *vecPredictedPosition )
{
	if ( ( pTarget == NULL ) || ( vecPredictedPosition == NULL ) )
		return;

	Vector	vecPredictedVel;

	//FIXME: Should we look at groundspeed or velocity for non-clients??

	//Get the proper velocity to predict with
	CBasePlayer	*pPlayer = ToBasePlayer( pTarget );

	//Player works differently than other entities
	if ( pPlayer != NULL )
	{
		if ( pPlayer->IsInAVehicle() )
		{
			//Calculate the predicted position in this vehicle
			vecPredictedVel = pPlayer->GetVehicleEntity()->GetSmoothedVelocity();
		}
		else
		{
			//Get the player's stored velocity
			vecPredictedVel = pPlayer->GetSmoothedVelocity();
		}
	}
	else
	{
		// See if we're a combat character in a vehicle
		CBaseCombatCharacter *pCCTarget = pTarget->MyCombatCharacterPointer();
		if ( pCCTarget != NULL && pCCTarget->IsInAVehicle() )
		{
			//Calculate the predicted position in this vehicle
			vecPredictedVel = pCCTarget->GetVehicleEntity()->GetSmoothedVelocity();
		}
		else
		{
			// See if we're an animating entity
			CBaseAnimating *pAnimating = dynamic_cast<CBaseAnimating *>(pTarget);
			if ( pAnimating != NULL )
			{
				vecPredictedVel = pAnimating->GetGroundSpeedVelocity();
			}
			else
			{
				// Otherwise we're a vanilla entity
				vecPredictedVel = pTarget->GetSmoothedVelocity();				
			}
		}
	}

	//Get the result
	(*vecPredictedPosition) = pTarget->GetEngineObject()->GetAbsOrigin() + ( vecPredictedVel * flTimeDelta );
}

//-----------------------------------------------------------------------------
// Purpose: Points the destination entity at the target entity
// Input  : *pDest - entity to be pointed at the target
//			*pTarget - target to point at
//-----------------------------------------------------------------------------
bool UTIL_PointAtEntity( CBaseEntity *pDest, CBaseEntity *pTarget )
{
	if ( ( pDest == NULL ) || ( pTarget == NULL ) )
	{
		return false;
	}

	Vector dir = (pTarget->GetEngineObject()->GetAbsOrigin() - pDest->GetEngineObject()->GetAbsOrigin());

	VectorNormalize( dir );

	//Store off as angles
	QAngle angles;
	VectorAngles( dir, angles );
	pDest->GetEngineObject()->SetLocalAngles( angles );
	pDest->GetEngineObject()->SetAbsAngles( angles );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Points the destination entity at the target entity by name
// Input  : *pDest - entity to be pointed at the target
//			strTarget - name of entity to target (will only choose the first!)
//-----------------------------------------------------------------------------
void UTIL_PointAtNamedEntity( CBaseEntity *pDest, string_t strTarget )
{
	//Attempt to find the entity
	if ( !UTIL_PointAtEntity( pDest, EntityList()->FindEntityByName( NULL, strTarget ) ) )
	{
		DevMsg( 1, "%s (%s) was unable to point at an entity named: %s\n", pDest->GetClassname(), pDest->GetDebugName(), STRING( strTarget ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Copy the pose parameter values from one entity to the other
// Input  : *pSourceEntity - entity to copy from
//			*pDestEntity - entity to copy to
//-----------------------------------------------------------------------------
bool UTIL_TransferPoseParameters( CBaseEntity *pSourceEntity, CBaseEntity *pDestEntity )
{
	CBaseAnimating *pSourceBaseAnimating = dynamic_cast<CBaseAnimating*>( pSourceEntity );
	CBaseAnimating *pDestBaseAnimating = dynamic_cast<CBaseAnimating*>( pDestEntity );

	if ( !pSourceBaseAnimating || !pDestBaseAnimating )
		return false;

	for ( int iPose = 0; iPose < MAXSTUDIOPOSEPARAM; ++iPose )
	{
		pDestBaseAnimating->GetEngineObject()->SetPoseParameter( iPose, pSourceBaseAnimating->GetEngineObject()->GetPoseParameter( iPose ) );
	}
	
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Make a muzzle flash appear
// Input  : &origin - position of the muzzle flash
//			&angles - angles of the fire direction
//			scale - scale of the muzzle flash
//			type - type of muzzle flash
//-----------------------------------------------------------------------------
void UTIL_MuzzleFlash( const Vector &origin, const QAngle &angles, int scale, int type )
{
	CPASFilter filter( origin );

	te->MuzzleFlash( filter, 0.0f, origin, angles, scale, type );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : vStartPos - start of the line
//			vEndPos - end of the line
//			vPoint - point to find nearest point to on specified line
//			clampEnds - clamps returned points to being on the line segment specified
// Output : Vector - nearest point on the specified line
//-----------------------------------------------------------------------------
Vector UTIL_PointOnLineNearestPoint(const Vector& vStartPos, const Vector& vEndPos, const Vector& vPoint, bool clampEnds )
{
	Vector	vEndToStart		= (vEndPos - vStartPos);
	Vector	vOrgToStart		= (vPoint  - vStartPos);
	float	fNumerator		= DotProduct(vEndToStart,vOrgToStart);
	float	fDenominator	= vEndToStart.Length() * vOrgToStart.Length();
	float	fIntersectDist	= vOrgToStart.Length()*(fNumerator/fDenominator);
	float	flLineLength	= VectorNormalize( vEndToStart ); 
	
	if ( clampEnds )
	{
		fIntersectDist = clamp( fIntersectDist, 0.0f, flLineLength );
	}
	
	Vector	vIntersectPos	= vStartPos + vEndToStart * fIntersectDist;

	return vIntersectPos;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
AngularImpulse WorldToLocalRotation( const VMatrix &localToWorld, const Vector &worldAxis, float rotation )
{
	// fix axes of rotation to match axes of vector
	Vector rot = worldAxis * rotation;
	// since the matrix maps local to world, do a transpose rotation to get world to local
	AngularImpulse ang = localToWorld.VMul3x3Transpose( rot );

	return ang;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *filename - 
//			*pLength - 
// Output : byte
//-----------------------------------------------------------------------------
byte *UTIL_LoadFileForMe( const char *filename, int *pLength )
{
	void *buffer = NULL;

	int length = filesystem->ReadFileEx( filename, "GAME", &buffer, true, true );

	if ( pLength )
	{
		*pLength = length;
	}

	return (byte *)buffer;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *buffer - 
//-----------------------------------------------------------------------------
void UTIL_FreeFile( byte *buffer )
{
	filesystem->FreeOptimalReadBuffer( buffer );
}

//-----------------------------------------------------------------------------
// Purpose: Determines whether an entity is within a certain angular tolerance to viewer
// Input  : *pEntity - entity which is the "viewer"
//			vecPosition - position to test against
//			flTolerance - tolerance (as dot-product)
//			*pflDot - if not NULL, holds the 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool UTIL_IsFacingWithinTolerance( CBaseEntity *pViewer, const Vector &vecPosition, float flDotTolerance, float *pflDot /*= NULL*/ )
{
	if ( pflDot )
	{
		*pflDot = 0.0f;
	}

	// Required elements
	if ( pViewer == NULL )
		return false;

	Vector forward;
	pViewer->GetVectors( &forward, NULL, NULL );

	Vector dir = vecPosition - pViewer->GetEngineObject()->GetAbsOrigin();
	VectorNormalize( dir );

	// Larger dot product corresponds to a smaller angle
	float flDot = dir.Dot( forward );

	// Return the result
	if ( pflDot )
	{
		*pflDot = flDot;
	}

	// Within the goal tolerance
	if ( flDot >= flDotTolerance )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Determines whether an entity is within a certain angular tolerance to viewer
// Input  : *pEntity - entity which is the "viewer"
//			*pTarget - entity to test against
//			flTolerance - tolerance (as dot-product)
//			*pflDot - if not NULL, holds the 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool UTIL_IsFacingWithinTolerance( CBaseEntity *pViewer, CBaseEntity *pTarget, float flDotTolerance, float *pflDot /*= NULL*/ )
{
	if ( pViewer == NULL || pTarget == NULL )
		return false;

	return UTIL_IsFacingWithinTolerance( pViewer, pTarget->GetEngineObject()->GetAbsOrigin(), flDotTolerance, pflDot );
}

//-----------------------------------------------------------------------------
// Purpose: Fills in color for debug purposes based on a relationship
// Input  : nRelationship - relationship to test
//			*pR, *pG, *pB - colors to fill
//-----------------------------------------------------------------------------
void UTIL_GetDebugColorForRelationship( int nRelationship, int &r, int &g, int &b )
{
	switch ( nRelationship )
	{
	case D_LI:
		r = 0;
		g = 255;
		b = 0;
		break;
	case D_NU:
		r = 0;
		g = 0;
		b = 255;
		break;
	case D_HT:
		r = 255;
		g = 0;
		b = 0;
		break;
	case D_FR:
		r = 255;
		g = 255;
		b = 0;
		break;
	default:
		r = 255;
		g = 255;
		b = 255;
		break;
	}
}

void LoadAndSpawnEntities_ParseEntKVBlockHelper( CBaseEntity *pNode, KeyValues *pkvNode )
{
	KeyValues *pkvNodeData = pkvNode->GetFirstSubKey();
	while ( pkvNodeData )
	{
		// Handle the connections block
		if ( !Q_strcmp(pkvNodeData->GetName(), "connections") )
		{
			LoadAndSpawnEntities_ParseEntKVBlockHelper( pNode, pkvNodeData );
		}
		else
		{
			pNode->KeyValue( pkvNodeData->GetName(), pkvNodeData->GetString() );
		}

		pkvNodeData = pkvNodeData->GetNextKey();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Loads and parses a file and spawns entities defined in it.
//-----------------------------------------------------------------------------
bool UTIL_LoadAndSpawnEntitiesFromScript( CUtlVector <CBaseEntity*> &entities, const char *pScriptFile, const char *pBlock, bool bActivate )
{
	KeyValues *pkvFile = new KeyValues( pBlock );

	if ( pkvFile->LoadFromFile( filesystem, pScriptFile, "MOD" ) )
	{	
		// Load each block, and spawn the entities
		KeyValues *pkvNode = pkvFile->GetFirstSubKey();
		while ( pkvNode )
		{
			// Get name
			const char *pNodeName = pkvNode->GetName();

			if ( stricmp( pNodeName, "entity" ) )
			{
				pkvNode = pkvNode->GetNextKey();
				continue;
			}

			KeyValues *pClassname = pkvNode->FindKey( "classname" );

			if ( pClassname )
			{
				// Use the classname instead
				pNodeName = pClassname->GetString();
			}

			// Spawn the entity
			CBaseEntity *pNode = (CBaseEntity*)EntityList()->CreateEntityByName( pNodeName );

			if ( pNode )
			{
				LoadAndSpawnEntities_ParseEntKVBlockHelper( pNode, pkvNode );
				DispatchSpawn( pNode );
				entities.AddToTail( pNode );
			}
			else
			{
				Warning( "UTIL_LoadAndSpawnEntitiesFromScript: Failed to spawn entity, type: '%s'\n", pNodeName );
			}

			// Move to next entity
			pkvNode = pkvNode->GetNextKey();
		}

		if ( bActivate == true )
		{
			bool bAsyncAnims = mdlcache->SetAsyncLoad( MDLCACHE_ANIMBLOCK, false );
			// Then activate all the entities
			for ( int i = 0; i < entities.Count(); i++ )
			{
				entities[i]->Activate();
			}
			mdlcache->SetAsyncLoad( MDLCACHE_ANIMBLOCK, bAsyncAnims );
		}
	}
	else
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Convert a vector an angle from worldspace to the entity's parent's local space
// Input  : *pEntity - Entity whose parent we're concerned with
//-----------------------------------------------------------------------------
void UTIL_ParentToWorldSpace( CBaseEntity *pEntity, Vector &vecPosition, QAngle &vecAngles )
{
	if ( pEntity == NULL )
		return;

	// Construct the entity-to-world matrix
	// Start with making an entity-to-parent matrix
	matrix3x4_t matEntityToParent;
	AngleMatrix( vecAngles, matEntityToParent );
	MatrixSetColumn( vecPosition, 3, matEntityToParent );

	// concatenate with our parent's transform
	matrix3x4_t matScratch, matResult;
	matrix3x4_t matParentToWorld;
	
	if ( pEntity->GetEngineObject()->GetMoveParent() != NULL )
	{
		matParentToWorld = pEntity->GetEngineObject()->GetParentToWorldTransform( matScratch );
	}
	else
	{
		matParentToWorld = pEntity->GetEngineObject()->EntityToWorldTransform();
	}

	ConcatTransforms( matParentToWorld, matEntityToParent, matResult );

	// pull our absolute position out of the matrix
	MatrixGetColumn( matResult, 3, vecPosition );
	MatrixAngles( matResult, vecAngles );
}

//-----------------------------------------------------------------------------
// Purpose: Convert a vector and quaternion from worldspace to the entity's parent's local space
// Input  : *pEntity - Entity whose parent we're concerned with
//-----------------------------------------------------------------------------
void UTIL_ParentToWorldSpace( CBaseEntity *pEntity, Vector &vecPosition, Quaternion &quat )
{
	if ( pEntity == NULL )
		return;

	QAngle vecAngles;
	QuaternionAngles( quat, vecAngles );
	UTIL_ParentToWorldSpace( pEntity, vecPosition, vecAngles );
	AngleQuaternion( vecAngles, quat );
}

//-----------------------------------------------------------------------------
// Purpose: Convert a vector an angle from worldspace to the entity's parent's local space
// Input  : *pEntity - Entity whose parent we're concerned with
//-----------------------------------------------------------------------------
void UTIL_WorldToParentSpace( CBaseEntity *pEntity, Vector &vecPosition, QAngle &vecAngles )
{
	if ( pEntity == NULL )
		return;

	// Construct the entity-to-world matrix
	// Start with making an entity-to-parent matrix
	matrix3x4_t matEntityToParent;
	AngleMatrix( vecAngles, matEntityToParent );
	MatrixSetColumn( vecPosition, 3, matEntityToParent );

	// concatenate with our parent's transform
	matrix3x4_t matScratch, matResult;
	matrix3x4_t matWorldToParent;
	
	if ( pEntity->GetEngineObject()->GetMoveParent() != NULL )
	{
		matScratch = pEntity->GetEngineObject()->GetParentToWorldTransform( matScratch );
	}
	else
	{
		matScratch = pEntity->GetEngineObject()->EntityToWorldTransform();
	}

	MatrixInvert( matScratch, matWorldToParent );
	ConcatTransforms( matWorldToParent, matEntityToParent, matResult );

	// pull our absolute position out of the matrix
	MatrixGetColumn( matResult, 3, vecPosition );
	MatrixAngles( matResult, vecAngles );
}

//-----------------------------------------------------------------------------
// Purpose: Convert a vector and quaternion from worldspace to the entity's parent's local space
// Input  : *pEntity - Entity whose parent we're concerned with
//-----------------------------------------------------------------------------
void UTIL_WorldToParentSpace( CBaseEntity *pEntity, Vector &vecPosition, Quaternion &quat )
{
	if ( pEntity == NULL )
		return;

	QAngle vecAngles;
	QuaternionAngles( quat, vecAngles );
	UTIL_WorldToParentSpace( pEntity, vecPosition, vecAngles );
	AngleQuaternion( vecAngles, quat );
}

//-----------------------------------------------------------------------------
// Purpose: Given a vector, clamps the scalar axes to MAX_COORD_FLOAT ranges from worldsize.h
// Input  : *pVecPos - 
//-----------------------------------------------------------------------------
void UTIL_BoundToWorldSize( Vector *pVecPos )
{
	Assert( pVecPos );
	for ( int i = 0; i < 3; ++i )
	{
		(*pVecPos)[ i ] = clamp( (*pVecPos)[ i ], MIN_COORD_FLOAT, MAX_COORD_FLOAT );
	}
}

// This does the necessary casting / extract to grab a pointer to a member function as a void *
// UNDONE: Cast to BASEPTR or something else here?
//#define EXTRACT_INPUTFUNC_FUNCTIONPTR(x)		(*(inputfunc_t **)(&(x)))

class CSkipKeys : public IVPhysicsKeyHandler
{
public:
	virtual void ParseKeyValue(void* pData, const char* pKey, const char* pValue) {}
	virtual void SetDefaults(void* pData) {}
};

void PhysSolidOverride(solid_t& solid, string_t overrideScript)
{
	if (overrideScript != NULL_STRING)
	{
		// parser destroys this data
		bool collisions = solid.params.enableCollisions;

		char pTmpString[4096];

		// write a header for a solid_t
		Q_strncpy(pTmpString, "solid { ", sizeof(pTmpString));

		// suck out the comma delimited tokens and turn them into quoted key/values
		char szToken[256];
		const char* pStr = nexttoken(szToken, STRING(overrideScript), ',');
		while (szToken[0] != 0)
		{
			Q_strncat(pTmpString, "\"", sizeof(pTmpString), COPY_ALL_CHARACTERS);
			Q_strncat(pTmpString, szToken, sizeof(pTmpString), COPY_ALL_CHARACTERS);
			Q_strncat(pTmpString, "\" ", sizeof(pTmpString), COPY_ALL_CHARACTERS);
			pStr = nexttoken(szToken, pStr, ',');
		}
		// terminate the script
		Q_strncat(pTmpString, "}", sizeof(pTmpString), COPY_ALL_CHARACTERS);

		// parse that sucker
		IVPhysicsKeyParser* pParse = EntityList()->PhysGetCollision()->VPhysicsKeyParserCreate(pTmpString);
		CSkipKeys tmp;
		pParse->ParseSolid(&solid, &tmp);
		EntityList()->PhysGetCollision()->VPhysicsKeyParserDestroy(pParse);

		// parser destroys this data
		solid.params.enableCollisions = collisions;
	}
}

void PhysEnableFloating(IPhysicsObject* pObject, bool bEnable)
{
	if (pObject != NULL)
	{
		unsigned short flags = pObject->GetCallbackFlags();
		if (bEnable)
		{
			flags |= CALLBACK_DO_FLUID_SIMULATION;
		}
		else
		{
			flags &= ~CALLBACK_DO_FLUID_SIMULATION;
		}
		pObject->SetCallbackFlags(flags);
	}
}

ConVar collision_shake_amp("collision_shake_amp", "0.2");
ConVar collision_shake_freq("collision_shake_freq", "0.5");
ConVar collision_shake_time("collision_shake_time", "0.5");

void PhysCollisionScreenShake(gamevcollisionevent_t* pEvent, int index)
{
	int otherIndex = !index;
	float mass = pEvent->pObjects[index]->GetMass();
	if (mass >= VPHYSICS_LARGE_OBJECT_MASS && pEvent->pObjects[otherIndex]->IsStatic() &&
		!(pEvent->pObjects[index]->GetGameFlags() & FVPHYSICS_PENETRATING))
	{
		mass = clamp(mass, VPHYSICS_LARGE_OBJECT_MASS, 2000.f);
		if (pEvent->collisionSpeed > 30 && pEvent->deltaCollisionTime > 0.25f)
		{
			Vector vecPos;
			pEvent->pInternalData->GetContactPoint(vecPos);
			float impulse = pEvent->collisionSpeed * mass;
			float amplitude = impulse * (collision_shake_amp.GetFloat() / (30.0f * VPHYSICS_LARGE_OBJECT_MASS));
			UTIL_ScreenShake(vecPos, amplitude, collision_shake_freq.GetFloat(), collision_shake_time.GetFloat(), amplitude * 60, SHAKE_START);
		}
	}
}

#if HL2_EPISODIC
// Uses DispatchParticleEffect because, so far as I know, that is the new means of kicking
// off flinders for this kind of collision. Should this be in g_pEffects instead? 
void PhysCollisionWarpEffect(gamevcollisionevent_t* pEvent, surfacedata_t* phit)
{
	Vector vecPos;
	QAngle vecAngles;

	pEvent->pInternalData->GetContactPoint(vecPos);
	{
		Vector vecNormal;
		pEvent->pInternalData->GetSurfaceNormal(vecNormal);
		VectorAngles(vecNormal, vecAngles);
	}

	DispatchParticleEffect("warp_shield_impact", vecPos, vecAngles);
}
#endif

void PhysCollisionDust(gamevcollisionevent_t* pEvent, surfacedata_t* phit)
{

	switch (phit->game.material)
	{
	case CHAR_TEX_SAND:
	case CHAR_TEX_DIRT:

		if (pEvent->collisionSpeed < 200.0f)
			return;

		break;

	case CHAR_TEX_CONCRETE:

		if (pEvent->collisionSpeed < 340.0f)
			return;

		break;

#if HL2_EPISODIC 
		// this is probably redundant because BaseEntity::VHandleCollision should have already dispatched us elsewhere
	case CHAR_TEX_WARPSHIELD:
		PhysCollisionWarpEffect(pEvent, phit);
		return;

		break;
#endif

	default:
		return;
	}

	//Kick up dust
	Vector	vecPos, vecVel;

	pEvent->pInternalData->GetContactPoint(vecPos);

	vecVel.Random(-1.0f, 1.0f);
	vecVel.z = random->RandomFloat(0.3f, 1.0f);
	VectorNormalize(vecVel);
	g_pEffects->Dust(vecPos, vecVel, 8.0f, pEvent->collisionSpeed);
}

void PhysSetEntityGameFlags(CBaseEntity* pEntity, unsigned short flags)
{
	IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEntity->GetEngineObject()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
	for (int i = 0; i < count; i++)
	{
		PhysSetGameFlags(pList[i], flags);
	}
}

void DebugDrawContactPoints(IPhysicsObject* pPhysics)
{
	IPhysicsFrictionSnapshot* pSnapshot = pPhysics->CreateFrictionSnapshot();

	while (pSnapshot->IsValid())
	{
		Vector pt, normal;
		pSnapshot->GetContactPoint(pt);
		pSnapshot->GetSurfaceNormal(normal);
		NDebugOverlay::Box(pt, -Vector(1, 1, 1), Vector(1, 1, 1), 0, 255, 0, 32, 0);
		NDebugOverlay::Line(pt, pt - normal * 20, 0, 255, 0, false, 0);
		IPhysicsObject* pOther = pSnapshot->GetObject(1);
		CBaseEntity* pEntity0 = static_cast<CBaseEntity*>(pOther->GetGameData());
		CFmtStr str("%s (%s): %s [%0.2f]", pEntity0->GetClassname(), STRING(pEntity0->GetEngineObject()->GetModelName()), pEntity0->GetDebugName(), pSnapshot->GetFrictionCoefficient());
		NDebugOverlay::Text(pt, str.Access(), false, 0);
		pSnapshot->NextFrictionData();
	}
	pSnapshot->DeleteAllMarkedContacts(true);
	pPhysics->DestroyFrictionSnapshot(pSnapshot);
}

IPhysicsObject* FindPhysicsObjectByName(const char* pName, CBaseEntity* pErrorEntity)
{
	if (!pName || !strlen(pName))
		return NULL;

	CBaseEntity* pEntity = NULL;
	IPhysicsObject* pBestObject = NULL;
	while (1)
	{
		pEntity = EntityList()->FindEntityByName(pEntity, pName);
		if (!pEntity)
			break;
		if (pEntity->GetEngineObject()->VPhysicsGetObject())
		{
			if (pBestObject)
			{
				const char* pErrorName = pErrorEntity ? pErrorEntity->GetClassname() : "Unknown";
				Vector origin = pErrorEntity ? pErrorEntity->GetEngineObject()->GetAbsOrigin() : vec3_origin;
				DevWarning("entity %s at %s has physics attachment to more than one entity with the name %s!!!\n", pErrorName, VecToString(origin), pName);
				while ((pEntity = EntityList()->FindEntityByName(pEntity, pName)) != NULL)
				{
					DevWarning("Found %s\n", pEntity->GetClassname());
				}
				break;

			}
			pBestObject = pEntity->GetEngineObject()->VPhysicsGetObject();
		}
	}
	return pBestObject;
}

static void CallbackHighlight(CBaseEntity* pEntity)
{
	pEntity->m_debugOverlays |= OVERLAY_ABSBOX_BIT | OVERLAY_PIVOT_BIT;
}

CON_COMMAND(physics_highlight_active, "Turns on the absbox for all active physics objects")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	EntityList()->IterateActivePhysicsEntities(CallbackHighlight);
}

static void CallbackReport(CBaseEntity* pEntity)
{
	const char* pName = STRING(pEntity->GetEntityName());
	if (!Q_strlen(pName))
	{
		pName = STRING(pEntity->GetEngineObject()->GetModelName());
	}
	Msg("%s - %s\n", pEntity->GetClassname(), pName);
}

CON_COMMAND(physics_report_active, "Lists all active physics objects")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	EntityList()->IterateActivePhysicsEntities(CallbackReport);
}

CON_COMMAND_F(surfaceprop, "Reports the surface properties at the cursor", FCVAR_CHEAT)
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	CBasePlayer* pPlayer = UTIL_GetCommandClient();

	trace_t tr;
	Vector forward;
	pPlayer->EyeVectors(&forward);
	UTIL_TraceLine(pPlayer->EyePosition(), pPlayer->EyePosition() + forward * MAX_COORD_RANGE,
		MASK_SHOT_HULL | CONTENTS_GRATE | CONTENTS_DEBRIS, pPlayer, COLLISION_GROUP_NONE, &tr);

	if (tr.DidHit())
	{
		const model_t* pModel = modelinfo->GetModel(tr.m_pEnt->GetEngineObject()->GetModelIndex());
		const char* pModelName = STRING(tr.m_pEnt->GetEngineObject()->GetModelName());
		if (tr.DidHitWorld() && tr.hitbox > 0)
		{
			ICollideable* pCollide = staticpropmgr->GetStaticPropByIndex(tr.hitbox - 1);
			pModel = pCollide->GetCollisionModel();
			pModelName = modelinfo->GetModelName(pModel);
		}
		CFmtStr modelStuff;
		if (pModel)
		{
			modelStuff.sprintf("%s.%s ", modelinfo->IsTranslucent(pModel) ? "Translucent" : "Opaque",
				modelinfo->IsTranslucentTwoPass(pModel) ? "  Two-pass." : "");
		}

		// Calculate distance to surface that was hit
		Vector vecVelocity = tr.startpos - tr.endpos;
		int length = vecVelocity.Length();

		Msg("Hit surface \"%s\" (entity %s, model \"%s\" %s), texture \"%s\"\n", EntityList()->PhysGetProps()->GetPropName(tr.surface.surfaceProps), tr.m_pEnt->GetClassname(), pModelName, modelStuff.Access(), tr.surface.name);
		Msg("Distance to surface: %d\n", length);
	}
}

class CConstraintFloodEntry
{
public:
	CConstraintFloodEntry() : isMarked(false), isConstraint(false) {}

	CUtlVector<CBaseEntity*> linkList;
	bool isMarked;
	bool isConstraint;
};

class CConstraintFloodList
{
public:
	CConstraintFloodList()
	{
		SetDefLessFunc(m_list);
		m_list.EnsureCapacity(64);
		m_entryList.EnsureCapacity(64);
	}

	bool IsWorldEntity(CBaseEntity* pEnt)
	{
		if (pEnt->entindex() != -1)
			return pEnt->IsWorld();
		return false;
	}

	void AddLink(CBaseEntity* pEntity, CBaseEntity* pLink, bool bIsConstraint)
	{
		if (!pEntity || !pLink || IsWorldEntity(pEntity) || IsWorldEntity(pLink))
			return;
		int listIndex = m_list.Find(pEntity);
		if (listIndex == m_list.InvalidIndex())
		{
			int entryIndex = m_entryList.AddToTail();
			m_entryList[entryIndex].isConstraint = bIsConstraint;
			listIndex = m_list.Insert(pEntity, entryIndex);
		}
		int entryIndex = m_list.Element(listIndex);
		CConstraintFloodEntry& entry = m_entryList.Element(entryIndex);
		Assert(entry.isConstraint == bIsConstraint);
		if (entry.linkList.Find(pLink) < 0)
		{
			entry.linkList.AddToTail(pLink);
		}
	}

	void BuildGraphFromEntity(CBaseEntity* pEntity, CUtlVector<CBaseEntity*>& constraintList)
	{
		int listIndex = m_list.Find(pEntity);
		if (listIndex != m_list.InvalidIndex())
		{
			int entryIndex = m_list.Element(listIndex);
			CConstraintFloodEntry& entry = m_entryList.Element(entryIndex);
			if (!entry.isMarked)
			{
				if (entry.isConstraint)
				{
					Assert(constraintList.Find(pEntity) < 0);
					constraintList.AddToTail(pEntity);
				}
				entry.isMarked = true;
				for (int i = 0; i < entry.linkList.Count(); i++)
				{
					// now recursively traverse the graph from here
					BuildGraphFromEntity(entry.linkList[i], constraintList);
				}
			}
		}
	}
	CUtlMap<CBaseEntity*, int>	m_list;
	CUtlVector<CConstraintFloodEntry> m_entryList;
};

void PhysicsCommand(const CCommand& args, void (*func)(CBaseEntity* pEntity))
{
	if (args.ArgC() < 2)
	{
		CBasePlayer* pPlayer = UTIL_GetCommandClient();

		trace_t tr;
		Vector forward;
		pPlayer->EyeVectors(&forward);
		UTIL_TraceLine(pPlayer->EyePosition(), pPlayer->EyePosition() + forward * MAX_COORD_RANGE,
			MASK_SHOT_HULL | CONTENTS_GRATE | CONTENTS_DEBRIS, pPlayer, COLLISION_GROUP_NONE, &tr);

		if (tr.DidHit())
		{
			func((CBaseEntity*)tr.m_pEnt);
		}
	}
	else
	{
		CBaseEntity* pEnt = NULL;
		while ((pEnt = EntityList()->FindEntityGeneric(pEnt, args[1])) != NULL)
		{
			func(pEnt);
		}
	}
}

// traverses the graph of attachments (currently supports springs & constraints) starting at an entity
// Then turns on debug info for each link in the graph (springs/constraints are links)
static void DebugConstraints(CBaseEntity* pEntity)
{
	extern bool GetSpringAttachments(CBaseEntity * pEntity, CBaseEntity * pAttach[2], IPhysicsObject * pAttachVPhysics[2]);
	extern bool GetConstraintAttachments(CBaseEntity * pEntity, CBaseEntity * pAttach[2], IPhysicsObject * pAttachVPhysics[2]);
	extern void DebugConstraint(CBaseEntity * pEntity);

	if (!pEntity)
		return;

	CBaseEntity* pAttach[2];
	IPhysicsObject* pAttachVPhysics[2];
	CConstraintFloodList list;

	for (CBaseEntity* pList = EntityList()->FirstEnt(); pList != NULL; pList = EntityList()->NextEnt(pList))
	{
		if (GetConstraintAttachments(pList, pAttach, pAttachVPhysics) || GetSpringAttachments(pList, pAttach, pAttachVPhysics))
		{
			list.AddLink(pList, pAttach[0], true);
			list.AddLink(pList, pAttach[1], true);
			list.AddLink(pAttach[0], pList, false);
			list.AddLink(pAttach[1], pList, false);
		}
	}

	CUtlVector<CBaseEntity*> constraints;
	list.BuildGraphFromEntity(pEntity, constraints);
	for (int i = 0; i < constraints.Count(); i++)
	{
		if (!GetConstraintAttachments(constraints[i], pAttach, pAttachVPhysics))
		{
			GetSpringAttachments(constraints[i], pAttach, pAttachVPhysics);
		}
		const char* pName0 = "world";
		const char* pName1 = "world";
		const char* pModel0 = "";
		const char* pModel1 = "";
		int index0 = 0;
		int index1 = 0;
		if (pAttach[0])
		{
			pName0 = pAttach[0]->GetClassname();
			pModel0 = STRING(pAttach[0]->GetEngineObject()->GetModelName());
			index0 = pAttachVPhysics[0]->GetGameIndex();
		}
		if (pAttach[1])
		{
			pName1 = pAttach[1]->GetClassname();
			pModel1 = STRING(pAttach[1]->GetEngineObject()->GetModelName());
			index1 = pAttachVPhysics[1]->GetGameIndex();
		}
		Msg("**********************\n%s connects %s(%s:%d) to %s(%s:%d)\n", constraints[i]->GetClassname(), pName0, pModel0, index0, pName1, pModel1, index1);
		DebugConstraint(constraints[i]);
		constraints[i]->m_debugOverlays |= OVERLAY_BBOX_BIT | OVERLAY_TEXT_BIT;
	}
}

CON_COMMAND(physics_constraints, "Highlights constraint system graph for an entity")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	PhysicsCommand(args, DebugConstraints);
}

static void OutputVPhysicsDebugInfo(CBaseEntity* pEntity)
{
	EntityList()->OutputVPhysicsDebugInfo(pEntity);
}

CON_COMMAND(physics_debug_entity, "Dumps debug info for an entity")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	PhysicsCommand(args, OutputVPhysicsDebugInfo);
}

static void MarkVPhysicsDebug(CBaseEntity* pEntity)
{
	if (pEntity)
	{
		IPhysicsObject* pPhysics = pEntity->GetEngineObject()->VPhysicsGetObject();
		if (pPhysics)
		{
			unsigned short callbacks = pPhysics->GetCallbackFlags();
			callbacks ^= CALLBACK_MARKED_FOR_TEST;
			pPhysics->SetCallbackFlags(callbacks);
		}
	}
}

CON_COMMAND(physics_select, "Dumps debug info for an entity")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	PhysicsCommand(args, MarkVPhysicsDebug);
}

CON_COMMAND(physics_budget, "Times the cost of each active object")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	EntityList()->OutputVPhysicsBudgetInfo();
}

//-----------------------------------------------------------------------------

void CC_AirDensity(const CCommand& args)
{
	if (!EntityList()->PhysGetEnv())
		return;

	if (args.ArgC() < 2)
	{
		Msg("air_density <value>\nCurrent air density is %.2f\n", EntityList()->PhysGetEnv()->GetAirDensity());
	}
	else
	{
		float density = atof(args[1]);
		EntityList()->PhysGetEnv()->SetAirDensity(density);
	}
}
static ConCommand air_density("air_density", CC_AirDensity, "Changes the density of air for drag computations.", FCVAR_CHEAT);

#if 0

#include "filesystem.h"
//-----------------------------------------------------------------------------
// Purpose: This will append a collide to a glview file.  Then you can view the 
//			collisionmodels with glview.
// Input  : *pCollide - collision model
//			&origin - position of the instance of this model
//			&angles - orientation of instance
//			*pFilename - output text file
//-----------------------------------------------------------------------------
// examples:
// world:
//	DumpCollideToGlView( pWorldCollide->solids[0], vec3_origin, vec3_origin, "jaycollide.txt" );
// static_prop:
//	DumpCollideToGlView( info.m_pCollide->solids[0], info.m_Origin, info.m_Angles, "jaycollide.txt" );
//
//-----------------------------------------------------------------------------
void DumpCollideToGlView(CPhysCollide* pCollide, const Vector& origin, const QAngle& angles, const char* pFilename)
{
	if (!pCollide)
		return;

	printf("Writing %s...\n", pFilename);
	Vector* outVerts;
	int vertCount = EntityList()->PhysGetCollision()->CreateDebugMesh(pCollide, &outVerts);
	FileHandle_t fp = filesystem->Open(pFilename, "ab");
	int triCount = vertCount / 3;
	int vert = 0;
	VMatrix tmp = SetupMatrixOrgAngles(origin, angles);
	int i;
	for (i = 0; i < vertCount; i++)
	{
		outVerts[i] = tmp.VMul4x3(outVerts[i]);
	}
	for (i = 0; i < triCount; i++)
	{
		filesystem->FPrintf(fp, "3\n");
		filesystem->FPrintf(fp, "%6.3f %6.3f %6.3f 1 0 0\n", outVerts[vert].x, outVerts[vert].y, outVerts[vert].z);
		vert++;
		filesystem->FPrintf(fp, "%6.3f %6.3f %6.3f 0 1 0\n", outVerts[vert].x, outVerts[vert].y, outVerts[vert].z);
		vert++;
		filesystem->FPrintf(fp, "%6.3f %6.3f %6.3f 0 0 1\n", outVerts[vert].x, outVerts[vert].y, outVerts[vert].z);
		vert++;
	}
	filesystem->Close(fp);
	EntityList()->PhysGetCollision()->DestroyDebugMesh(vertCount, outVerts);
}
#endif

//=============================================================================
//
// Tests!
//

#define NUM_KDTREE_TESTS		2500
#define NUM_KDTREE_ENTITY_SIZE	256

void CC_KDTreeTest( const CCommand &args )
{
	Msg( "Testing kd-tree entity queries." );

	// Get the testing spot.
//	CBaseEntity *pSpot = EntityList()->FindEntityByClassname( NULL, "info_player_start" );
//	Vector vecStart = pSpot->GetAbsOrigin();

	CBasePlayer *pPlayer = static_cast<CBasePlayer*>(EntityList()->GetLocalPlayer() );
	Vector vecStart = pPlayer->GetEngineObject()->GetAbsOrigin();

	static Vector *vecTargets = NULL;
	static bool bFirst = true;

	// Generate the targets - rays (1K long).
	if ( bFirst )
	{
		vecTargets = new Vector [NUM_KDTREE_TESTS];
		double flRadius = 0;
		double flTheta = 0;
		double flPhi = 0;
		for ( int i = 0; i < NUM_KDTREE_TESTS; ++i )
		{
			flRadius += NUM_KDTREE_TESTS * 123.123;
			flRadius =  fmod( flRadius, 128.0 );
			flRadius =  fabs( flRadius );

			flTheta  += NUM_KDTREE_TESTS * 76.76;
			flTheta  =  fmod( flTheta, (double) DEG2RAD( 360 ) );
			flTheta  =  fabs( flTheta );

			flPhi    += NUM_KDTREE_TESTS * 1997.99;
			flPhi    =  fmod( flPhi, (double) DEG2RAD( 180 ) );
			flPhi    =  fabs( flPhi );

			float st, ct, sp, cp;
			SinCos( flTheta, &st, &ct );
			SinCos( flPhi, &sp, &cp );

			vecTargets[i].x = flRadius * ct * sp;
			vecTargets[i].y = flRadius * st * sp;
			vecTargets[i].z = flRadius * cp;
			
			// Make the trace 1024 units long.
			Vector vecDir = vecTargets[i] - vecStart;
			VectorNormalize( vecDir );
			vecTargets[i] = vecStart + vecDir * 1024;
		}

		bFirst = false;
	}

	int nTestType = 0;
	if ( args.ArgC() >= 2 )
	{
		nTestType = atoi( args[ 1 ] );
	}

	vtune( true );

#ifdef VPROF_ENABLED
	g_VProfCurrentProfile.Resume();
	g_VProfCurrentProfile.Start();
	g_VProfCurrentProfile.Reset();
	g_VProfCurrentProfile.MarkFrame();
#endif

	switch ( nTestType )
	{
	case 0:
		{
			VPROF( "TraceTotal" );			

			trace_t trace;
			for ( int iTest = 0; iTest < NUM_KDTREE_TESTS; ++iTest )
			{
				UTIL_TraceLine( vecStart, vecTargets[iTest], MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &trace );
			}
			break;
		}
	case 1:
		{
			VPROF( "TraceTotal" );

			trace_t trace;
			for ( int iTest = 0; iTest < NUM_KDTREE_TESTS; ++iTest )
			{
				UTIL_TraceHull( vecStart, vecTargets[iTest], VEC_HULL_MIN_SCALED( pPlayer ), VEC_HULL_MAX_SCALED( pPlayer ), MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &trace );
			}
			break;
		}
	case 2:
		{
			Vector vecMins[NUM_KDTREE_TESTS];
			Vector vecMaxs[NUM_KDTREE_TESTS];
			int iTest;
			for ( iTest = 0; iTest < NUM_KDTREE_TESTS; ++iTest )
			{
				vecMins[iTest] = vecStart;
				vecMaxs[iTest] = vecStart;
				for ( int iAxis = 0; iAxis < 3; ++iAxis )
				{
					if ( vecTargets[iTest].x < vecMins[iTest].x ) { vecMins[iTest].x = vecTargets[iTest].x; }
					if ( vecTargets[iTest].y < vecMins[iTest].y ) { vecMins[iTest].y = vecTargets[iTest].y; }
					if ( vecTargets[iTest].z < vecMins[iTest].z ) { vecMins[iTest].z = vecTargets[iTest].z; }

					if ( vecTargets[iTest].x > vecMaxs[iTest].x ) { vecMaxs[iTest].x = vecTargets[iTest].x; }
					if ( vecTargets[iTest].y > vecMaxs[iTest].y ) { vecMaxs[iTest].y = vecTargets[iTest].y; }
					if ( vecTargets[iTest].z > vecMaxs[iTest].z ) { vecMaxs[iTest].z = vecTargets[iTest].z; }
				}
			}


			VPROF( "TraceTotal" );

			int nCount = 0;

			Vector vecDelta;
			trace_t trace;
			CBaseEntity *pList[1024];
			for ( iTest = 0; iTest < NUM_KDTREE_TESTS; ++iTest )
			{
				nCount += UTIL_EntitiesInBox( pList, 1024, vecMins[iTest], vecMaxs[iTest], 0 );
			}

			Msg( "Count = %d\n", nCount );
			break;
		}
	case 3:
		{
			Vector vecDelta;
			float flRadius[NUM_KDTREE_TESTS];
			int iTest;
			for ( iTest = 0; iTest < NUM_KDTREE_TESTS; ++iTest )
			{
				VectorSubtract( vecTargets[iTest], vecStart, vecDelta );
				flRadius[iTest] = vecDelta.Length() * 0.5f;
			}

			VPROF( "TraceTotal" );

			int nCount = 0;

			trace_t trace;
			CBaseEntity *pList[1024];
			for ( iTest = 0; iTest < NUM_KDTREE_TESTS; ++iTest )
			{
				nCount += UTIL_EntitiesInSphere( pList, 1024, vecStart, flRadius[iTest], 0 );
			}

			Msg( "Count = %d\n", nCount );
			break;
		}
	default:
		{
			break;
		}
	}

#ifdef VPROF_ENABLED
	g_VProfCurrentProfile.MarkFrame();
	g_VProfCurrentProfile.Pause();
	g_VProfCurrentProfile.OutputReport( VPRT_FULL );
#endif
	
	vtune( false );
}

static ConCommand kdtree_test( "kdtree_test", CC_KDTreeTest, "Tests spatial partition for entities queries.", FCVAR_CHEAT );

void CC_VoxelTreeView( void )
{
	Msg( "VoxelTreeView\n" );
	partition->RenderAllObjectsInTree( 10.0f );
}

static ConCommand voxeltree_view( "voxeltree_view", CC_VoxelTreeView, "View entities in the voxel-tree.", FCVAR_CHEAT );

void CC_VoxelTreePlayerView( void )
{
	Msg( "VoxelTreePlayerView\n" );

	CBasePlayer *pPlayer = static_cast<CBasePlayer*>(EntityList()->GetLocalPlayer() );
	Vector vecStart = pPlayer->GetEngineObject()->GetAbsOrigin();
	partition->RenderObjectsInPlayerLeafs( vecStart - VEC_HULL_MIN_SCALED( pPlayer ), vecStart + VEC_HULL_MAX_SCALED( pPlayer ), 3.0f  );
}

static ConCommand voxeltree_playerview( "voxeltree_playerview", CC_VoxelTreePlayerView, "View entities in the voxel-tree at the player position.", FCVAR_CHEAT );

void CC_VoxelTreeBox( const CCommand &args )
{
	Vector vecMin, vecMax;
	if ( args.ArgC() >= 6 )
	{
		vecMin.x = atof( args[ 1 ] );
		vecMin.y = atof( args[ 2 ] );
		vecMin.z = atof( args[ 3 ] );

		vecMax.x = atof( args[ 4 ] );
		vecMax.y = atof( args[ 5 ] );
		vecMax.z = atof( args[ 6 ] );
	}
	else
	{
		return;
	}

	float flTime = 10.0f;

	Vector vecPoints[8];
	vecPoints[0].Init( vecMin.x, vecMin.y, vecMin.z );
	vecPoints[1].Init( vecMin.x, vecMax.y, vecMin.z );
	vecPoints[2].Init( vecMax.x, vecMax.y, vecMin.z );
	vecPoints[3].Init( vecMax.x, vecMin.y, vecMin.z );
	vecPoints[4].Init( vecMin.x, vecMin.y, vecMax.z );
	vecPoints[5].Init( vecMin.x, vecMax.y, vecMax.z );
	vecPoints[6].Init( vecMax.x, vecMax.y, vecMax.z );
	vecPoints[7].Init( vecMax.x, vecMin.y, vecMax.z );
	
	debugoverlay->AddLineOverlay( vecPoints[0], vecPoints[1], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[1], vecPoints[2], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[2], vecPoints[3], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[3], vecPoints[0], 255, 0, 0, true, flTime );
	
	debugoverlay->AddLineOverlay( vecPoints[4], vecPoints[5], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[5], vecPoints[6], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[6], vecPoints[7], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[7], vecPoints[4], 255, 0, 0, true, flTime );
	
	debugoverlay->AddLineOverlay( vecPoints[0], vecPoints[4], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[3], vecPoints[7], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[1], vecPoints[5], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[2], vecPoints[6], 255, 0, 0, true, flTime );

	Msg( "VoxelTreeBox - (%f %f %f) to (%f %f %f)\n", vecMin.x, vecMin.y, vecMin.z, vecMax.x, vecMax.y, vecMax.z );
	partition->RenderObjectsInBox( vecMin, vecMax, flTime );
}

static ConCommand voxeltree_box( "voxeltree_box", CC_VoxelTreeBox, "View entities in the voxel-tree inside box <Vector(min), Vector(max)>.", FCVAR_CHEAT );

void CC_VoxelTreeSphere( const CCommand &args )
{
	Vector vecCenter;
	float flRadius;
	if ( args.ArgC() >= 4 )
	{
		vecCenter.x = atof( args[ 1 ] );
		vecCenter.y = atof( args[ 2 ] );
		vecCenter.z = atof( args[ 3 ] );

		flRadius = atof( args[ 3 ] );
	}
	else
	{
		return;
	}

	float flTime = 3.0f;

	Vector vecMin, vecMax;
	vecMin.Init( vecCenter.x - flRadius, vecCenter.y - flRadius, vecCenter.z - flRadius );
	vecMax.Init( vecCenter.x + flRadius, vecCenter.y + flRadius, vecCenter.z + flRadius );

	Vector vecPoints[8];
	vecPoints[0].Init( vecMin.x, vecMin.y, vecMin.z );
	vecPoints[1].Init( vecMin.x, vecMax.y, vecMin.z );
	vecPoints[2].Init( vecMax.x, vecMax.y, vecMin.z );
	vecPoints[3].Init( vecMax.x, vecMin.y, vecMin.z );
	vecPoints[4].Init( vecMin.x, vecMin.y, vecMax.z );
	vecPoints[5].Init( vecMin.x, vecMax.y, vecMax.z );
	vecPoints[6].Init( vecMax.x, vecMax.y, vecMax.z );
	vecPoints[7].Init( vecMax.x, vecMin.y, vecMax.z );
	
	debugoverlay->AddLineOverlay( vecPoints[0], vecPoints[1], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[1], vecPoints[2], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[2], vecPoints[3], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[3], vecPoints[0], 255, 0, 0, true, flTime );
	
	debugoverlay->AddLineOverlay( vecPoints[4], vecPoints[5], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[5], vecPoints[6], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[6], vecPoints[7], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[7], vecPoints[4], 255, 0, 0, true, flTime );
	
	debugoverlay->AddLineOverlay( vecPoints[0], vecPoints[4], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[3], vecPoints[7], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[1], vecPoints[5], 255, 0, 0, true, flTime );
	debugoverlay->AddLineOverlay( vecPoints[2], vecPoints[6], 255, 0, 0, true, flTime );

	Msg( "VoxelTreeSphere - (%f %f %f), %f\n", vecCenter.x, vecCenter.y, vecCenter.z, flRadius );
	partition->RenderObjectsInSphere( vecCenter, flRadius, flTime );
}

static ConCommand voxeltree_sphere( "voxeltree_sphere", CC_VoxelTreeSphere, "View entities in the voxel-tree inside sphere <Vector(center), float(radius)>.", FCVAR_CHEAT );



#define NUM_COLLISION_TESTS 2500
void CC_CollisionTest( const CCommand &args )
{
	if ( !EntityList()->PhysGetEnv())
		return;

	Msg( "Testing collision system\n" );
	partition->ReportStats( "" );
	int i;
	CBaseEntity *pSpot = EntityList()->FindEntityByClassname( NULL, "info_player_start");
	Vector start = pSpot->GetEngineObject()->GetAbsOrigin();
	static Vector *targets = NULL;
	static bool first = true;
	static float test[2] = {1,1};
	if ( first )
	{
		targets = new Vector[NUM_COLLISION_TESTS];
		float radius = 0;
		float theta = 0;
		float phi = 0;
		for ( i = 0; i < NUM_COLLISION_TESTS; i++ )
		{
			radius += NUM_COLLISION_TESTS * 123.123;
			radius = fabs(fmod(radius, 128));
			theta += NUM_COLLISION_TESTS * 76.76;
			theta = fabs(fmod(theta, DEG2RAD(360)));
			phi += NUM_COLLISION_TESTS * 1997.99;
			phi = fabs(fmod(phi, DEG2RAD(180)));
			
			float st, ct, sp, cp;
			SinCos( theta, &st, &ct );
			SinCos( phi, &sp, &cp );

			targets[i].x = radius * ct * sp;
			targets[i].y = radius * st * sp;
			targets[i].z = radius * cp;
			
			// make the trace 1024 units long
			Vector dir = targets[i] - start;
			VectorNormalize(dir);
			targets[i] = start + dir * 1024;
		}
		first = false;
	}

	//Vector results[NUM_COLLISION_TESTS];

	int testType = 0;
	if ( args.ArgC() >= 2 )
	{
		testType = atoi(args[1]);
	}
	float duration = 0;
	Vector size[2];
	size[0].Init(0,0,0);
	size[1].Init(16,16,16);
	unsigned int dots = 0;
	int nMask = MASK_ALL & ~(CONTENTS_MONSTER | CONTENTS_HITBOX );
	for ( int j = 0; j < 2; j++ )
	{
		float startTime = engine->Time();
		if ( testType == 1 )
		{
			trace_t tr;
			for ( i = 0; i < NUM_COLLISION_TESTS; i++ )
			{
				UTIL_TraceHull( start, targets[i], -size[1], size[1], nMask, NULL, COLLISION_GROUP_NONE, &tr );
			}
		}
		else
		{
			testType = 0;
			trace_t tr;

			for ( i = 0; i < NUM_COLLISION_TESTS; i++ )
			{
				if ( i == 0 )
				{
					partition->RenderLeafsForRayTraceStart( 10.0f );
				}

				UTIL_TraceLine( start, targets[i], nMask, NULL, COLLISION_GROUP_NONE, &tr );

				if ( i == 0 )
				{
					partition->RenderLeafsForRayTraceEnd( );
				}
			}
		}

		duration += engine->Time() - startTime;
	}
	test[testType] = duration;
	Msg("%d collisions in %.2f ms (%u dots)\n", NUM_COLLISION_TESTS, duration*1000, dots );
	partition->ReportStats( "" );
#if 1
	int red = 255, green = 0, blue = 0;
	for ( i = 0; i < 1 /*NUM_COLLISION_TESTS*/; i++ )
	{
		NDebugOverlay::Line( start, targets[i], red, green, blue, false, 2 );
	}
#endif
}
static ConCommand collision_test("collision_test", CC_CollisionTest, "Tests collision system", FCVAR_CHEAT );

#ifndef CLIENT_DLL

void CC_Debug_FixMyPosition(void)
{
	CBaseEntity* pPlayer = UTIL_GetCommandClient();

	pPlayer->FindClosestPassableSpace(vec3_origin);
}

static ConCommand debug_fixmyposition("debug_fixmyposition", CC_Debug_FixMyPosition, "Runs FindsClosestPassableSpace() on player.", FCVAR_CHEAT);
#endif


