//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include <ctype.h>
#include <KeyValues.h>
#include "engine/IEngineSound.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "igamesystem.h"
#include "soundchars.h"
#include "filesystem.h"
#include "tier0/vprof.h"
#include "checksum_crc.h"
#include "tier0/icommandline.h"
#include "tier3/tier3.h"

#if defined( TF_CLIENT_DLL ) || defined( TF_DLL )
#include "tf_shareddefs.h"
#include "tf_classdata.h"
#endif

// NVNT haptic utils
#include "haptics/haptic_utils.h"

#ifndef CLIENT_DLL
//#include "envmicrophone.h"
#include "sceneentity.h"
#include "gameinterface.h"
#else
#include <vgui_controls/Controls.h>
#include <vgui/IVGui.h>
#include "hud_closecaption.h"
#define CRecipientFilter C_RecipientFilter
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar sv_soundemitter_trace( "sv_soundemitter_trace", "0", FCVAR_REPLICATED, "Show all EmitSound calls including their symbolic name and the actual wave file they resolved to\n" );
#ifdef STAGING_ONLY
static ConVar sv_snd_filter( "sv_snd_filter", "", FCVAR_REPLICATED, "Filters out all sounds not containing the specified string before being emitted\n" );
#endif // STAGING_ONLY

//extern ISoundEmitterSystemBase *soundemitterbase;
//static ConVar *g_pClosecaption = NULL;

//#ifdef _XBOX
//int LookupStringFromCloseCaptionToken( char const *token );
//const wchar_t *GetStringForIndex( int index );
//#endif




//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &src - 
//-----------------------------------------------------------------------------
//EmitSound_t::EmitSound_t( const CSoundParameters &src )
//{
//	m_nChannel = src.channel;
//	m_pSoundName = src.soundname;
//	m_flVolume = src.volume;
//	m_SoundLevel = src.soundlevel;
//	m_nFlags = 0;
//	m_nPitch = src.pitch;
//	m_nSpecialDSP = 0;
//	m_pOrigin = 0;
//	m_flSoundTime = ( src.delay_msec == 0 ) ? 0.0f : gpGlobals->curtime + ( (float)src.delay_msec / 1000.0f );
//	m_pflSoundDuration = 0;
//	m_bEmitCloseCaption = true;
//	m_bWarnOnMissingCloseCaption = false;
//	m_bWarnOnDirectWaveReference = false;
//	m_nSpeakerEntity = -1;
//}

//void Hack_FixEscapeChars( char *str )
//{
//	int len = Q_strlen( str ) + 1;
//	char *i = str;
//	char *o = (char *)_alloca( len );
//	char *osave = o;
//	while ( *i )
//	{
//		if ( *i == '\\' )
//		{
//			switch (  *( i + 1 ) )
//			{
//			case 'n':
//				*o = '\n';
//				++i;
//				break;
//			default:
//				*o = *i;
//				break;
//			}
//		}
//		else
//		{
//			*o = *i;
//		}
//
//		++i;
//		++o;
//	}
//	*o = 0;
//	Q_strncpy( str, osave, len );
//}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CSoundEmitterSystem : public CBaseGameSystem, public ISoundEmitterSystem
{
public:
	virtual char const *Name() { return "CSoundEmitterSystem"; }

#if !defined( CLIENT_DLL )
	bool			m_bLogPrecache;
	FileHandle_t	m_hPrecacheLogFile;
	CUtlSymbolTable m_PrecachedScriptSounds;
public:
	CSoundEmitterSystem( char const *pszName ) :
		m_bLogPrecache( false ),
		m_hPrecacheLogFile( FILESYSTEM_INVALID_HANDLE )
	{
	}

	void LogPrecache( char const *soundname )
	{
		if ( !m_bLogPrecache )
			return;

		// Make sure we only show the message once
		if ( UTL_INVAL_SYMBOL != m_PrecachedScriptSounds.Find( soundname ) )
			return;

		if (m_hPrecacheLogFile == FILESYSTEM_INVALID_HANDLE)
		{
			StartLog();
		}

		m_PrecachedScriptSounds.AddString( soundname );

		if (m_hPrecacheLogFile != FILESYSTEM_INVALID_HANDLE)
		{
			filesystem->Write("\"", 1, m_hPrecacheLogFile);
			filesystem->Write(soundname, Q_strlen(soundname), m_hPrecacheLogFile);
			filesystem->Write("\"\n", 2, m_hPrecacheLogFile);
		}
		else
		{
			Warning( "Disabling precache logging due to file i/o problem!!!\n" );
			m_bLogPrecache = false;
		}
	}

	void StartLog()
	{
		m_PrecachedScriptSounds.RemoveAll();

		if ( !m_bLogPrecache )
			return;

		if ( FILESYSTEM_INVALID_HANDLE != m_hPrecacheLogFile )
		{
			return;
		}

		filesystem->CreateDirHierarchy("reslists", "DEFAULT_WRITE_PATH");

		// open the new level reslist
		char path[_MAX_PATH];
		Q_snprintf(path, sizeof(path), "reslists\\%s.snd", gpGlobals->mapname.ToCStr() );
		m_hPrecacheLogFile = filesystem->Open(path, "wt", "GAME");
	}

	void FinishLog()
	{
		if ( FILESYSTEM_INVALID_HANDLE != m_hPrecacheLogFile )
		{
			filesystem->Close( m_hPrecacheLogFile );
			m_hPrecacheLogFile = FILESYSTEM_INVALID_HANDLE;
		}

		m_PrecachedScriptSounds.RemoveAll();
	}
#else
	CSoundEmitterSystem( char const *name )
	{
	}

#endif

	// IServerSystem stuff
	virtual bool Init()
	{
		Assert( g_pSoundEmitterSystemBase );
#if !defined( CLIENT_DLL )
		m_bLogPrecache = CommandLine()->CheckParm( "-makereslists" ) ? true : false;
#endif
		//g_pClosecaption = cvar->FindVar("closecaption");
		//Assert(g_pClosecaption);
		return g_pSoundEmitterSystemBase->ModInit();
	}

	virtual void Shutdown()
	{
		Assert( g_pSoundEmitterSystemBase );
#if !defined( CLIENT_DLL )
		FinishLog();
#endif
		g_pSoundEmitterSystemBase->ModShutdown();
	}

	void ReloadSoundEntriesInList( IFileList *pFilesToReload )
	{
		g_pSoundEmitterSystemBase->ReloadSoundEntriesInList( pFilesToReload );
	}

	virtual void TraceEmitSound( char const *fmt, ... )
	{
		if ( !sv_soundemitter_trace.GetBool() )
			return;

		va_list	argptr;
		char string[256];
		va_start (argptr, fmt);
		Q_vsnprintf( string, sizeof( string ), fmt, argptr );
		va_end (argptr);

		// Spew to console
		Msg( "%s %s", CBaseEntity::IsServer() ? "(sv)" : "(cl)", string );
	}

	// Precache all wave files referenced in wave or rndwave keys
	virtual void LevelInitPreEntity()
	{
		char mapname[ 256 ];
#if !defined( CLIENT_DLL )
		StartLog();
		Q_snprintf( mapname, sizeof( mapname ), "maps/%s", STRING( gpGlobals->mapname ) );
#else
		Q_strncpy( mapname, engine->GetLevelName(), sizeof( mapname ) );
#endif

		Q_FixSlashes( mapname );
		Q_strlower( mapname );

		// Load in any map specific overrides
		char scriptfile[ 512 ];
#if defined( TF_CLIENT_DLL ) || defined( TF_DLL )
		if( V_stristr( mapname, "mvm" ) )
		{
			V_strncpy( scriptfile, "scripts/mvm_level_sounds.txt", sizeof( scriptfile ) );
			if ( filesystem->FileExists( "scripts/mvm_level_sounds.txt", "GAME" ) )
			{
				g_pSoundEmitterSystemBase->AddSoundOverrides( "scripts/mvm_level_sounds.txt" );
			}
			if ( filesystem->FileExists( "scripts/mvm_level_sound_tweaks.txt", "GAME" ) )
			{
				g_pSoundEmitterSystemBase->AddSoundOverrides( "scripts/mvm_level_sound_tweaks.txt" );
 			}
			if ( filesystem->FileExists( "scripts/game_sounds_vo_mvm.txt", "GAME" ) )
			{
				g_pSoundEmitterSystemBase->AddSoundOverrides( "scripts/game_sounds_vo_mvm.txt", true );
			}
			if ( filesystem->FileExists( "scripts/game_sounds_vo_mvm_mighty.txt", "GAME" ) )
			{
				g_pSoundEmitterSystemBase->AddSoundOverrides( "scripts/game_sounds_vo_mvm_mighty.txt", true );
			}
			g_pTFPlayerClassDataMgr->AddAdditionalPlayerDeathSounds();
		}
		else
		{
			Q_StripExtension( mapname, scriptfile, sizeof( scriptfile ) );
			Q_strncat( scriptfile, "_level_sounds.txt", sizeof( scriptfile ), COPY_ALL_CHARACTERS );
			if ( filesystem->FileExists( scriptfile, "GAME" ) )
			{
				g_pSoundEmitterSystemBase->AddSoundOverrides( scriptfile );
			}
		}
#else
		Q_StripExtension( mapname, scriptfile, sizeof( scriptfile ) );
		Q_strncat( scriptfile, "_level_sounds.txt", sizeof( scriptfile ), COPY_ALL_CHARACTERS );

		if ( filesystem->FileExists( scriptfile, "GAME" ) )
		{
			g_pSoundEmitterSystemBase->AddSoundOverrides( scriptfile );
		}
#endif

#if !defined( CLIENT_DLL )
		for ( int i=g_pSoundEmitterSystemBase->First(); i != g_pSoundEmitterSystemBase->InvalidIndex(); i=g_pSoundEmitterSystemBase->Next( i ) )
		{
			CSoundParametersInternal *pParams = g_pSoundEmitterSystemBase->InternalGetParametersForSound( i );
			if ( pParams->ShouldPreload() )
			{
				InternalPrecacheWaves( i );
			}
		}
#endif
	}

	virtual void LevelInitPostEntity()
	{
	}

	virtual void LevelShutdownPostEntity()
	{
		g_pSoundEmitterSystemBase->ClearSoundOverrides();

#if !defined( CLIENT_DLL )
		FinishLog();
#endif
	}
		
	void InternalPrecacheWaves( int soundIndex )
	{
		CSoundParametersInternal *internal = g_pSoundEmitterSystemBase->InternalGetParametersForSound( soundIndex );
		if ( !internal )
			return;

		int waveCount = internal->NumSoundNames();
		if ( !waveCount )
		{
			DevMsg( "CSoundEmitterSystem:  sounds.txt entry '%s' has no waves listed under 'wave' or 'rndwave' key!!!\n",
				g_pSoundEmitterSystemBase->GetSoundName( soundIndex ) );
		}
		else
		{
			g_bPermitDirectSoundPrecache = true;

			for( int wave = 0; wave < waveCount; wave++ )
			{
				PrecacheSound( g_pSoundEmitterSystemBase->GetWaveName( internal->GetSoundNames()[ wave ].symbol ) );
			}

			g_bPermitDirectSoundPrecache = false;
		}
	}

	void InternalPrefetchWaves( int soundIndex )
	{
		CSoundParametersInternal *internal = g_pSoundEmitterSystemBase->InternalGetParametersForSound( soundIndex );
		if ( !internal )
			return;

		int waveCount = internal->NumSoundNames();
		if ( !waveCount )
		{
			DevMsg( "CSoundEmitterSystem:  sounds.txt entry '%s' has no waves listed under 'wave' or 'rndwave' key!!!\n",
				g_pSoundEmitterSystemBase->GetSoundName( soundIndex ) );
		}
		else
		{
			for( int wave = 0; wave < waveCount; wave++ )
			{
				PrefetchSound( g_pSoundEmitterSystemBase->GetWaveName( internal->GetSoundNames()[ wave ].symbol ) );
			}
		}
	}

	HSOUNDSCRIPTHANDLE InternalPrecacheScriptSound( const char *soundname )
	{
		int soundIndex = g_pSoundEmitterSystemBase->GetSoundIndex( soundname );
		if ( !g_pSoundEmitterSystemBase->IsValidIndex( soundIndex ) )
		{
			if ( Q_stristr( soundname, ".wav" ) || Q_strstr( soundname, ".mp3" ) )
			{
				g_bPermitDirectSoundPrecache = true;
	
				PrecacheSound( soundname );
	
				g_bPermitDirectSoundPrecache = false;
				return SOUNDEMITTER_INVALID_HANDLE;
			}

#if !defined( CLIENT_DLL )
			if ( soundname[ 0 ] )
			{
				static CUtlSymbolTable s_PrecacheScriptSoundFailures;

				// Make sure we only show the message once
				if ( UTL_INVAL_SYMBOL == s_PrecacheScriptSoundFailures.Find( soundname ) )
				{
					DevMsg( "PrecacheScriptSound '%s' failed, no such sound script entry\n", soundname );
					s_PrecacheScriptSoundFailures.AddString( soundname );
				}
			}
#endif
			return (HSOUNDSCRIPTHANDLE)soundIndex;
		}
#if !defined( CLIENT_DLL )
		LogPrecache( soundname );
#endif

		InternalPrecacheWaves( soundIndex );
		return (HSOUNDSCRIPTHANDLE)soundIndex;
	}

	void InternalPrefetchScriptSound( const char *soundname )
	{
		int soundIndex = g_pSoundEmitterSystemBase->GetSoundIndex( soundname );
		if ( !g_pSoundEmitterSystemBase->IsValidIndex( soundIndex ) )
		{
			if ( Q_stristr( soundname, ".wav" ) || Q_strstr( soundname, ".mp3" ) )
			{
				PrefetchSound( soundname );
			}
			return;
		}

		InternalPrefetchWaves( soundIndex );
	}
public:

	void InternalEmitSoundByHandle( IRecipientFilter& filter, int entindex, const EmitSound_t & ep, HSOUNDSCRIPTHANDLE& handle )
	{
		// Pull data from parameters
		CSoundParameters params;

		// Try to deduce the actor's gender
		gender_t gender = GENDER_NONE;
		CBaseEntity* ent = NULL;
#ifdef GAME_DLL
		ent = gEntList.GetBaseEntity(entindex);
#endif // GAME_DLL
#ifdef CLIENT_DLL
		ent = CBaseEntity::Instance(entindex);
#endif // CLIENT_DLL
		if ( ent )
		{
			char const *actorModel = STRING( ent->GetEngineObject()->GetModelName() );
			gender = g_pSoundEmitterSystemBase->GetActorGender( actorModel );
		}

		if ( !g_pSoundEmitterSystemBase->GetParametersForSoundEx( ep.m_pSoundName, handle, params, gender, true ) )
		{
			return;
		}

		if ( !params.soundname[0] )
			return;

#ifdef STAGING_ONLY
		if ( sv_snd_filter.GetString()[ 0 ] && !V_stristr( params.soundname, sv_snd_filter.GetString() ))
		{
			return;
		}
#endif // STAGING_ONLY

		if ( !Q_strncasecmp( params.soundname, "vo", 2 ) &&
			!( params.channel == CHAN_STREAM ||
			   params.channel == CHAN_VOICE  ||
			   params.channel == CHAN_VOICE2 ) )
		{
			DevMsg( "EmitSound:  Voice wave file %s doesn't specify CHAN_VOICE, CHAN_VOICE2 or CHAN_STREAM for sound %s\n",
				params.soundname, ep.m_pSoundName );
		}

		// handle SND_CHANGEPITCH/SND_CHANGEVOL and other sound flags.etc.
		if( ep.m_nFlags & SND_CHANGE_PITCH )
		{
			params.pitch = ep.m_nPitch;
		}


		if( ep.m_nFlags & SND_CHANGE_VOL )
		{
			params.volume = ep.m_flVolume;
		}

#if !defined( CLIENT_DLL )
		extern CServerGameDLL g_ServerGameDLL;
		bool bSwallowed = g_ServerGameDLL.OnEmitSound(
			entindex, 
			params.soundname, 
			params.soundlevel, 
			params.volume, 
			ep.m_nFlags, 
			params.pitch, 
			ep.m_pOrigin, 
			ep.m_flSoundTime,
			ep.m_UtlVecSoundOrigin );
		if ( bSwallowed )
			return;
#endif

#if defined( _DEBUG ) && !defined( CLIENT_DLL )
		if ( !enginesound->IsSoundPrecached( params.soundname ) )
		{
			Msg( "Sound %s:%s was not precached\n", ep.m_pSoundName, params.soundname );
		}
#endif

		float st = ep.m_flSoundTime;
		if ( !st && 
			params.delay_msec != 0 )
		{
			st = gpGlobals->curtime + (float)params.delay_msec / 1000.f;
		}

		enginesound->EmitSound( 
			filter, 
			entindex, 
			params.channel, 
			params.soundname,
			params.volume,
			(soundlevel_t)params.soundlevel,
			ep.m_nFlags,
			params.pitch,
			ep.m_nSpecialDSP,
			ep.m_pOrigin,
			NULL,
			&ep.m_UtlVecSoundOrigin,
			true,
			st,
			ep.m_nSpeakerEntity );
		if ( ep.m_pflSoundDuration )
		{
			*ep.m_pflSoundDuration = enginesound->GetSoundDuration( params.soundname );
		}

		TraceEmitSound( "EmitSound:  '%s' emitted as '%s' (ent %i)\n",
			ep.m_pSoundName, params.soundname, entindex );


		// Don't caption modulations to the sound
		if ( !( ep.m_nFlags & ( SND_CHANGE_PITCH | SND_CHANGE_VOL ) ) )
		{
#ifdef GAME_DLL
			extern CServerGameDLL g_ServerGameDLL;
			g_ServerGameDLL.InternalEmitCloseCaption(filter, entindex, params, ep);
#endif // GAME_DLL
#ifdef CLIENT_DLL
			clientdll->InternalEmitCloseCaption(filter, entindex, params, ep);
#endif // CLIENT_DLL

		}
#if defined( WIN32 ) && !defined( _X360 )
		// NVNT notify the haptics system of this sound
		HapticProcessSound(ep.m_pSoundName, entindex);
#endif
	}

	static void WaveTrace(char const* wavname, char const* funcname)
	{
		if (IsX360() && !IsDebug())
		{
			return;
		}

		static CUtlSymbolTable s_WaveTrace;

		// Make sure we only show the message once
		if (UTL_INVAL_SYMBOL == s_WaveTrace.Find(wavname))
		{
			DevMsg("%s directly referenced wave %s (should use game_sounds.txt system instead)\n",
				funcname, wavname);
			s_WaveTrace.AddString(wavname);
		}
	}

	void InternalEmitSound( IRecipientFilter& filter, int entindex, const EmitSound_t & ep )
	{
		VPROF( "CSoundEmitterSystem::EmitSound (calls engine)" );

#ifdef STAGING_ONLY
		if ( sv_snd_filter.GetString()[ 0 ] && !V_stristr( ep.m_pSoundName, sv_snd_filter.GetString() ))
		{
			return;
		}
#endif // STAGING_ONLY

		if ( ep.m_pSoundName && 
			( Q_stristr( ep.m_pSoundName, ".wav" ) || 
			  Q_stristr( ep.m_pSoundName, ".mp3" ) || 
			  ep.m_pSoundName[0] == '!' ) )
		{
#if !defined( CLIENT_DLL )
			extern CServerGameDLL g_ServerGameDLL;
			bool bSwallowed = g_ServerGameDLL.OnEmitSound(
				entindex, 
				ep.m_pSoundName, 
				ep.m_SoundLevel, 
				ep.m_flVolume, 
				ep.m_nFlags, 
				ep.m_nPitch, 
				ep.m_pOrigin, 
				ep.m_flSoundTime,
				ep.m_UtlVecSoundOrigin );
			if ( bSwallowed )
				return;
#endif

			if ( ep.m_bWarnOnDirectWaveReference && 
				Q_stristr( ep.m_pSoundName, ".wav" ) )
			{
				WaveTrace( ep.m_pSoundName, "Emitsound" );
			}

#if defined( _DEBUG ) && !defined( CLIENT_DLL )
			if ( !enginesound->IsSoundPrecached( ep.m_pSoundName ) )
			{
				Msg( "Sound %s was not precached\n", ep.m_pSoundName );
			}
#endif
			enginesound->EmitSound( 
				filter, 
				entindex, 
				ep.m_nChannel, 
				ep.m_pSoundName, 
				ep.m_flVolume, 
				ep.m_SoundLevel, 
				ep.m_nFlags, 
				ep.m_nPitch, 
				ep.m_nSpecialDSP,
				ep.m_pOrigin,
				NULL, 
				&ep.m_UtlVecSoundOrigin,
				true, 
				ep.m_flSoundTime,
				ep.m_nSpeakerEntity );
			if ( ep.m_pflSoundDuration )
			{
				*ep.m_pflSoundDuration = enginesound->GetSoundDuration( ep.m_pSoundName );
			}

			TraceEmitSound( "EmitSound:  Raw wave emitted '%s' (ent %i)\n",
				ep.m_pSoundName, entindex );
			return;
		}

		if ( ep.m_hSoundScriptHandle == SOUNDEMITTER_INVALID_HANDLE )
		{
			ep.m_hSoundScriptHandle = (HSOUNDSCRIPTHANDLE)g_pSoundEmitterSystemBase->GetSoundIndex( ep.m_pSoundName );
		}

		if ( ep.m_hSoundScriptHandle == -1 )
			return;

		InternalEmitSoundByHandle( filter, entindex, ep, ep.m_hSoundScriptHandle );
	}

//	void InternalEmitCloseCaption( IRecipientFilter& filter, int entindex, bool fromplayer, char const *token, CUtlVector< Vector >& originlist, float duration, bool warnifmissing /*= false*/ )
//	{
//		// No close captions in multiplayer...
//		if ( gpGlobals->maxClients > 1 || (gpGlobals->maxClients==1 && !g_pClosecaption->GetBool()))
//		{
//			return;
//		}
//
//		// A negative duration means fill it in from the wav file if possible
//		if ( duration < 0.0f )
//		{
//			char const *wav = g_pSoundEmitterSystemBase->GetWavFileForSound( token, GENDER_NONE );
//			if ( wav )
//			{
//				duration = enginesound->GetSoundDuration( wav );
//			}
//			else
//			{
//				duration = 2.0f;
//			}
//		}
//
//		char lowercase[ 256 ];
//		Q_strncpy( lowercase, token, sizeof( lowercase ) );
//		Q_strlower( lowercase );
//		if ( Q_strstr( lowercase, "\\" ) )
//		{
//			Hack_FixEscapeChars( lowercase );
//		}
//
//		// NOTE:  We must make a copy or else if the filter is owned by a SoundPatch, we'll end up destructively removing
//		//  all players from it!!!!
//		CRecipientFilter filterCopy;
//		filterCopy.CopyFrom( (CRecipientFilter &)filter );
//
//		// Remove any players who don't want close captions
//		CBaseEntity::RemoveRecipientsIfNotCloseCaptioning( (CRecipientFilter &)filterCopy );
//
//#if !defined( CLIENT_DLL )
//		{
//			// Defined in sceneentity.cpp
//			bool AttenuateCaption( const char *token, const Vector& listener, CUtlVector< Vector >& soundorigins );
//
//			if ( filterCopy.GetRecipientCount() > 0 )
//			{
//				int c = filterCopy.GetRecipientCount();
//				for ( int i = c - 1 ; i >= 0; --i )
//				{
//					CBasePlayer *player = UTIL_PlayerByIndex( filterCopy.GetRecipientIndex( i ) );
//					if ( !player )
//						continue;
//
//					Vector playerOrigin = player->GetAbsOrigin();
//
//					if ( AttenuateCaption( lowercase, playerOrigin, originlist ) )
//					{
//						filterCopy.RemoveRecipient( player );
//					}
//				}
//			}
//		}
//#endif
//		// Anyone left?
//		if ( filterCopy.GetRecipientCount() > 0 )
//		{
//
//#if !defined( CLIENT_DLL )
//
//			byte byteflags = 0;
//			if ( warnifmissing )
//			{
//				byteflags |= CLOSE_CAPTION_WARNIFMISSING;
//			}
//			if ( fromplayer )
//			{
//				byteflags |= CLOSE_CAPTION_FROMPLAYER;
//			}
//
//			CBaseEntity *pActor = gEntList.GetBaseEntity( entindex );
//			if ( pActor )
//			{
//				char const *pszActorModel = STRING( pActor->GetModelName() );
//				gender_t gender = g_pSoundEmitterSystemBase->GetActorGender( pszActorModel );
//
//				if ( gender == GENDER_MALE )
//				{
//					byteflags |= CLOSE_CAPTION_GENDER_MALE;
//				}
//				else if ( gender == GENDER_FEMALE )
//				{ 
//					byteflags |= CLOSE_CAPTION_GENDER_FEMALE;
//				}
//			}
//
//			// Send caption and duration hint down to client
//			UserMessageBegin( filterCopy, "CloseCaption" );
//				WRITE_STRING( lowercase );
//				WRITE_SHORT( MIN( 255, (int)( duration * 10.0f ) ) ),
//				WRITE_BYTE( byteflags ),
//			MessageEnd();
//#else
//			// Direct dispatch
//			CHudCloseCaption *cchud = GET_HUDELEMENT( CHudCloseCaption );
//			if ( cchud )
//			{
//				cchud->ProcessCaption( lowercase, duration, fromplayer );
//			}
//#endif
//		}
//	}

//	void InternalEmitCloseCaption( IRecipientFilter& filter, int entindex, const CSoundParameters & params, const EmitSound_t & ep )
//	{
//		// No close captions in multiplayer...
//		if ( gpGlobals->maxClients > 1 || (gpGlobals->maxClients==1 && !g_pClosecaption->GetBool()))
//		{
//			return;
//		}
//
//		if ( !ep.m_bEmitCloseCaption )
//		{
//			return;
//		}
//
//		// NOTE:  We must make a copy or else if the filter is owned by a SoundPatch, we'll end up destructively removing
//		//  all players from it!!!!
//		CRecipientFilter filterCopy;
//		filterCopy.CopyFrom( (CRecipientFilter &)filter );
//
//		// Remove any players who don't want close captions
//		CBaseEntity::RemoveRecipientsIfNotCloseCaptioning( (CRecipientFilter &)filterCopy );
//
//		// Anyone left?
//		if ( filterCopy.GetRecipientCount() <= 0 )
//		{
//			return;
//		}
//
//		float duration = 0.0f;
//		if ( ep.m_pflSoundDuration )
//		{
//			duration = *ep.m_pflSoundDuration;
//		}
//		else
//		{
//			duration = enginesound->GetSoundDuration( params.soundname );
//		}
//
//		bool fromplayer = false;
//		CBaseEntity* ent = NULL;
//#ifdef GAME_DLL
//		ent = gEntList.GetBaseEntity(entindex);
//#endif // GAME_DLL
//#ifdef CLIENT_DLL
//		ent = CBaseEntity::Instance(entindex);
//#endif // CLIENT_DLL
//		if ( ent )
//		{
//			while ( ent )
//			{
//				if ( ent->IsPlayer() )
//				{
//					fromplayer = true;
//					break;
//				}
//
//				ent = ent->GetOwnerEntity();
//			}
//		}
//		InternalEmitCloseCaption( filter, entindex, fromplayer, ep.m_pSoundName, ep.m_UtlVecSoundOrigin, duration, ep.m_bWarnOnMissingCloseCaption );
//	}

	void InternalEmitAmbientSound( int entindex, const Vector& origin, const char *soundname, float flVolume, int iFlags, int iPitch, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
	{
		// Pull data from parameters
		CSoundParameters params;

		if ( !g_pSoundEmitterSystemBase->GetParametersForSound( soundname, params, GENDER_NONE ) )
		{
			return;
		}

#ifdef STAGING_ONLY
		if ( sv_snd_filter.GetString()[ 0 ] && !V_stristr( params.soundname, sv_snd_filter.GetString() ))
		{
			return;
		}
#endif // STAGING_ONLY

		if( iFlags & SND_CHANGE_PITCH )
		{
			params.pitch = iPitch;
		}

		if( iFlags & SND_CHANGE_VOL )
		{
			params.volume = flVolume;
		}

//#if defined( CLIENT_DLL )
//		enginesound->EmitAmbientSound( params.soundname, params.volume, params.pitch, iFlags, soundtime );
//#else
		enginesound->EmitAmbientSound(entindex, origin, params.soundname, params.volume, params.soundlevel, iFlags, params.pitch, soundtime );
//#endif

		bool needsCC = !( iFlags & ( SND_STOP | SND_CHANGE_VOL | SND_CHANGE_PITCH ) );

		float soundduration = 0.0f;
		
		if ( duration || needsCC )
		{
			soundduration = enginesound->GetSoundDuration( params.soundname );
			if ( duration )
			{
				*duration = soundduration;
			}
		}

		TraceEmitSound( "EmitAmbientSound:  '%s' emitted as '%s' (ent %i)\n",
			soundname, params.soundname, entindex );

		// We only want to trigger the CC on the start of the sound, not on any changes or halting of the sound
		if ( needsCC )
		{
			CRecipientFilter filter;
			filter.AddAllPlayers();
			filter.MakeReliable();

			CUtlVector< Vector > dummy;
#ifdef GAME_DLL
			extern CServerGameDLL g_ServerGameDLL;
			g_ServerGameDLL.InternalEmitCloseCaption(filter, entindex, false, soundname, dummy, soundduration, false);
#endif // GAME_DLL
#ifdef CLIENT_DLL
			clientdll->InternalEmitCloseCaption(filter, entindex, false, soundname, dummy, soundduration, false);
#endif // CLIENT_DLL

		}

	}

	void InternalStopSoundByHandle( int entindex, const char *soundname, HSOUNDSCRIPTHANDLE& handle )
	{
		if ( handle == SOUNDEMITTER_INVALID_HANDLE )
		{
			handle = (HSOUNDSCRIPTHANDLE)g_pSoundEmitterSystemBase->GetSoundIndex( soundname );
		}

		if ( handle == SOUNDEMITTER_INVALID_HANDLE )
			return;

		CSoundParametersInternal *params;

		params = g_pSoundEmitterSystemBase->InternalGetParametersForSound( (int)handle );
		if ( !params )
		{
			return;
		}

		// HACK:  we have to stop all sounds if there are > 1 in the rndwave section...
		int c = params->NumSoundNames();
		for ( int i = 0; i < c; ++i )
		{
			char const *wavename = g_pSoundEmitterSystemBase->GetWaveName( params->GetSoundNames()[ i ].symbol );
			Assert( wavename );

			enginesound->StopSound( 
				entindex, 
				params->GetChannel(), 
				wavename );

			TraceEmitSound( "StopSound:  '%s' stopped as '%s' (ent %i)\n",
				soundname, wavename, entindex );
		}
	}

	void InternalStopSound( int entindex, const char *soundname )
	{
		HSOUNDSCRIPTHANDLE handle = (HSOUNDSCRIPTHANDLE)g_pSoundEmitterSystemBase->GetSoundIndex( soundname );
		if ( handle == SOUNDEMITTER_INVALID_HANDLE )
		{
			return;
		}

		InternalStopSoundByHandle( entindex, soundname, handle );
	}


	void InternalStopSound( int iEntIndex, int iChannel, const char *pSample )
	{
		if ( pSample && ( Q_stristr( pSample, ".wav" ) || Q_stristr( pSample, ".mp3" ) || pSample[0] == '!' ) )
		{
			enginesound->StopSound( iEntIndex, iChannel, pSample );

			TraceEmitSound( "StopSound:  Raw wave stopped '%s' (ent %i)\n",
				pSample, iEntIndex );
		}
		else
		{
			// Look it up in sounds.txt and ignore other parameters
			InternalStopSound( iEntIndex, pSample );
		}
	}

	void EmitAmbientSound( int entindex, const Vector &origin, const char *pSample, float volume, soundlevel_t soundlevel, int flags, int pitch, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
	{
#ifdef STAGING_ONLY
		if ( sv_snd_filter.GetString()[ 0 ] && !V_stristr( pSample, sv_snd_filter.GetString() ))
		{
			return;
		}
#endif // STAGING_ONLY

#if !defined( CLIENT_DLL )
		CUtlVector< Vector > dummyorigins;

		// Loop through all registered microphones and tell them the sound was just played
		// NOTE: This means that pitch shifts/sound changes on the original ambient will not be reflected in the re-broadcasted sound
		extern CServerGameDLL g_ServerGameDLL;
		bool bSwallowed = g_ServerGameDLL.OnEmitSound(
							entindex, 
							pSample, 
							soundlevel, 
							volume, 
							flags, 
							pitch, 
							&origin, 
							soundtime,
							dummyorigins );
		if ( bSwallowed )
			return;
#endif

		if ( pSample && ( Q_stristr( pSample, ".wav" ) || Q_stristr( pSample, ".mp3" )) )
		{
//#if defined( CLIENT_DLL )
//			enginesound->EmitAmbientSound( pSample, volume, pitch, flags, soundtime );
//#else
			enginesound->EmitAmbientSound( entindex, origin, pSample, volume, soundlevel, flags, pitch, soundtime );
//#endif

			if ( duration )
			{
				*duration = enginesound->GetSoundDuration( pSample );
			}

			TraceEmitSound( "EmitAmbientSound:  Raw wave emitted '%s' (ent %i)\n",
				pSample, entindex );
		}
		else
		{
			InternalEmitAmbientSound( entindex, origin, pSample, volume, flags, pitch, soundtime, duration );
		}
	}

	//-----------------------------------------------------------------------------
// Purpose:  Non-static override for doing the general case of CPASAttenuationFilter( this ), and EmitSound( filter, entindex(), etc. );
// Input  : *soundname - 
//-----------------------------------------------------------------------------
	//void EmitSound(CBaseEntity* pEntity, const char* soundname, float soundtime /*= 0.0f*/, float* duration /*=NULL*/)//CBaseEntity::
	//{
	//	//VPROF( "CBaseEntity::EmitSound" );
	//	VPROF_BUDGET("CBaseEntity::EmitSound", _T("CBaseEntity::EmitSound"));

	//	CPASAttenuationFilter filter(pEntity, soundname);

	//	EmitSound_t params;
	//	params.m_pSoundName = soundname;
	//	params.m_flSoundTime = soundtime;
	//	params.m_pflSoundDuration = duration;
	//	params.m_bWarnOnDirectWaveReference = true;

	//	EmitSound(filter, pEntity->entindex(), params);
	//}

	//-----------------------------------------------------------------------------
	// Purpose:  Non-static override for doing the general case of CPASAttenuationFilter( this ), and EmitSound( filter, entindex(), etc. );
	// Input  : *soundname - 
	//-----------------------------------------------------------------------------
	//void EmitSound(CBaseEntity* pEntity, const char* soundname, HSOUNDSCRIPTHANDLE& handle, float soundtime /*= 0.0f*/, float* duration /*=NULL*/)//CBaseEntity::
	//{
	//	VPROF_BUDGET("CBaseEntity::EmitSound", _T("CBaseEntity::EmitSound"));

	//	// VPROF( "CBaseEntity::EmitSound" );
	//	CPASAttenuationFilter filter(pEntity, soundname, handle);

	//	EmitSound_t params;
	//	params.m_pSoundName = soundname;
	//	params.m_flSoundTime = soundtime;
	//	params.m_pflSoundDuration = duration;
	//	params.m_bWarnOnDirectWaveReference = true;

	//	EmitSound(filter, pEntity->entindex(), params, handle);
	//}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : filter - 
	//			iEntIndex - 
	//			*soundname - 
	//			*pOrigin - 
	//-----------------------------------------------------------------------------
	void EmitSound(IRecipientFilter& filter, int iEntIndex, const char* soundname, const Vector* pOrigin /*= NULL*/, float soundtime /*= 0.0f*/, float* duration /*=NULL*/)//CBaseEntity::
	{
		if (!soundname)
			return;

		VPROF_BUDGET("CBaseEntity::EmitSound", _T("CBaseEntity::EmitSound"));

		// VPROF( "CBaseEntity::EmitSound" );
		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = soundtime;
		params.m_pOrigin = pOrigin;
		params.m_pflSoundDuration = duration;
		params.m_bWarnOnDirectWaveReference = true;

		EmitSound(filter, iEntIndex, params, params.m_hSoundScriptHandle);
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : filter - 
	//			iEntIndex - 
	//			*soundname - 
	//			*pOrigin - 
	//-----------------------------------------------------------------------------
	void EmitSound(IRecipientFilter& filter, int iEntIndex, const char* soundname, HSOUNDSCRIPTHANDLE& handle, const Vector* pOrigin /*= NULL*/, float soundtime /*= 0.0f*/, float* duration /*=NULL*/)//CBaseEntity::
	{
		VPROF_BUDGET("CBaseEntity::EmitSound", _T("CBaseEntity::EmitSound"));

		//VPROF( "CBaseEntity::EmitSound" );
		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = soundtime;
		params.m_pOrigin = pOrigin;
		params.m_pflSoundDuration = duration;
		params.m_bWarnOnDirectWaveReference = true;

		EmitSound(filter, iEntIndex, params, handle);
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : filter - 
	//			iEntIndex - 
	//			params - 
	//-----------------------------------------------------------------------------
	void EmitSound(IRecipientFilter& filter, int iEntIndex, const EmitSound_t& params)//CBaseEntity::
	{
		VPROF_BUDGET("CBaseEntity::EmitSound", _T("CBaseEntity::EmitSound"));

#ifdef GAME_DLL
		//CBaseEntity* pEntity = UTIL_EntityByIndex(iEntIndex);
#else
		//C_BaseEntity* pEntity = EntityList()->GetEnt(iEntIndex);
		clientdll->ModifyEmitSoundParams(const_cast<EmitSound_t&>(params));
#endif
		//if (pEntity)
		//{
		//	pEntity->ModifyEmitSoundParams(const_cast<EmitSound_t&>(params));
		//}

		// VPROF( "CBaseEntity::EmitSound" );
		// Call into the sound emitter system...
		InternalEmitSound(filter, iEntIndex, params);
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : filter - 
	//			iEntIndex - 
	//			params - 
	//-----------------------------------------------------------------------------
	void EmitSound(IRecipientFilter& filter, int iEntIndex, const EmitSound_t& params, HSOUNDSCRIPTHANDLE& handle)//CBaseEntity::
	{
		VPROF_BUDGET("CBaseEntity::EmitSound", _T("CBaseEntity::EmitSound"));

#ifdef GAME_DLL
		//CBaseEntity* pEntity = UTIL_EntityByIndex(iEntIndex);
#else
		//C_BaseEntity* pEntity = EntityList()->GetEnt(iEntIndex);
		clientdll->ModifyEmitSoundParams(const_cast<EmitSound_t&>(params));
#endif
		//if (pEntity)
		//{
		//	pEntity->ModifyEmitSoundParams(const_cast<EmitSound_t&>(params));
		//}

		// VPROF( "CBaseEntity::EmitSound" );
		// Call into the sound emitter system...
		InternalEmitSoundByHandle(filter, iEntIndex, params, handle);
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : *soundname - 
	//-----------------------------------------------------------------------------
	void StopSound(CBaseEntity* pEntity, const char* soundname)//CBaseEntity::
	{
#if defined( CLIENT_DLL )
		if (pEntity->entindex() == -1)
		{
			// If we're a clientside entity, we need to use the soundsourceindex instead of the entindex
			StopSound(pEntity->GetSoundSourceIndex(), soundname);
			return;
		}
#endif

		StopSound(pEntity->entindex(), soundname);
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : *soundname - 
	//-----------------------------------------------------------------------------
	void StopSound(CBaseEntity* pEntity, const char* soundname, HSOUNDSCRIPTHANDLE& handle)//CBaseEntity::
	{
#if defined( CLIENT_DLL )
		if (pEntity->entindex() == -1)
		{
			// If we're a clientside entity, we need to use the soundsourceindex instead of the entindex
			StopSound(pEntity->GetSoundSourceIndex(), soundname);
			return;
		}
#endif

		InternalStopSoundByHandle(pEntity->entindex(), soundname, handle);
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : iEntIndex - 
	//			*soundname - 
	//-----------------------------------------------------------------------------
	void StopSound(int iEntIndex, const char* soundname)//CBaseEntity::
	{
		InternalStopSound(iEntIndex, soundname);
	}

	void StopSound(int iEntIndex, int iChannel, const char* pSample)//CBaseEntity::
	{
		InternalStopSound(iEntIndex, iChannel, pSample);
	}

	soundlevel_t LookupSoundLevel(const char* soundname)//CBaseEntity::
	{
		return g_pSoundEmitterSystemBase->LookupSoundLevel(soundname);
	}


	soundlevel_t LookupSoundLevel(const char* soundname, HSOUNDSCRIPTHANDLE& handle)//CBaseEntity::
	{
		return g_pSoundEmitterSystemBase->LookupSoundLevelByHandle(soundname, handle);
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : *entity - 
	//			origin - 
	//			flags - 
	//			*soundname - 
	//-----------------------------------------------------------------------------
	//void EmitAmbientSound(int entindex, const Vector& origin, const char* soundname, float volume, soundlevel_t soundlevel, int flags, int pitch, float soundtime /*= 0.0f*/, float* duration /*=NULL*/)//CBaseEntity::
	//{
	//	InternalEmitAmbientSound(entindex, origin, soundname, volume, soundlevel, flags, pitch, soundtime, duration);
	//}
	
	void GenderExpandString(CBaseEntity* pEntity, char const* in, char* out, int maxlen)//CBaseEntity::
	{
		g_pSoundEmitterSystemBase->GenderExpandString(STRING(pEntity->GetEngineObject()->GetModelName()), in, out, maxlen);
	}

	bool GetParametersForSound(const char* soundname, CSoundParameters& params, const char* actormodel)//CBaseEntity::
	{
		gender_t gender = g_pSoundEmitterSystemBase->GetActorGender(actormodel);

		return g_pSoundEmitterSystemBase->GetParametersForSound(soundname, params, gender);
	}

	bool GetParametersForSound(const char* soundname, HSOUNDSCRIPTHANDLE& handle, CSoundParameters& params, const char* actormodel)//CBaseEntity::
	{
		gender_t gender = g_pSoundEmitterSystemBase->GetActorGender(actormodel);

		return g_pSoundEmitterSystemBase->GetParametersForSoundEx(soundname, handle, params, gender);
	}

	HSOUNDSCRIPTHANDLE PrecacheScriptSound(const char* soundname)//CBaseEntity::
	{
#if !defined( CLIENT_DLL )
		return InternalPrecacheScriptSound(soundname);
#else
		return g_pSoundEmitterSystemBase->GetSoundIndex(soundname);
#endif
	}

	void PrefetchScriptSound(const char* soundname)//CBaseEntity::
	{
		InternalPrefetchScriptSound(soundname);
	}

	static const char* UTIL_TranslateSoundName(const char* soundname, const char* actormodel)
	{
		Assert(soundname);

		if (Q_stristr(soundname, ".wav") || Q_stristr(soundname, ".mp3"))
		{
			if (Q_stristr(soundname, ".wav"))
			{
				WaveTrace(soundname, "UTIL_TranslateSoundName");
			}
			return soundname;
		}

		return g_pSoundEmitterSystemBase->GetWavFileForSound(soundname, actormodel);
	}
	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : *soundname - 
	// Output : float
	//-----------------------------------------------------------------------------
	float GetSoundDuration(const char* soundname, char const* actormodel)//CBaseEntity::
	{
		return enginesound->GetSoundDuration(PSkipSoundChars(UTIL_TranslateSoundName(soundname, actormodel)));
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : filter - 
	//			*token - 
	//			duration - 
	//			warnifmissing - 
	//-----------------------------------------------------------------------------
//	void EmitCloseCaption(IRecipientFilter& filter, int entindex, char const* token, CUtlVector< Vector >& soundorigin, float duration, bool warnifmissing /*= false*/)// CBaseEntity::
//	{
//		bool fromplayer = false;
//		CBaseEntity* ent = NULL;
//#ifdef GAME_DLL
//		ent = gEntList.GetBaseEntity(entindex);
//#endif // GAME_DLL
//#ifdef CLIENT_DLL
//		ent = CBaseEntity::Instance(entindex);
//#endif // CLIENT_DLL
//		while (ent)
//		{
//			if (ent->IsPlayer())
//			{
//				fromplayer = true;
//				break;
//			}
//			ent = ent->GetOwnerEntity();
//		}
//
//		InternalEmitCloseCaption(filter, entindex, fromplayer, token, soundorigin, duration, warnifmissing);
//	}

	
	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : *name - 
	//			preload - 
	// Output : Returns true on success, false on failure.
	//-----------------------------------------------------------------------------
	bool PrecacheSound(const char* name)//CBaseEntity::
	{
		if (IsPC() && !g_bPermitDirectSoundPrecache)
		{
			Warning("Direct precache of %s\n", name);
		}

#ifdef GAME_DLL
		// If this is out of order, warn
		if (!engine->IsPrecacheAllowed())//CBaseEntity::
		{
			if (!enginesound->IsSoundPrecached(name))
			{
				Assert(!"CBaseEntity::PrecacheSound:  too late");

				Warning("Late precache of %s\n", name);
			}
		}
#endif // GAME_DLL

		bool bret = enginesound->PrecacheSound(name, true);
		return bret;
	}

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : *name - 
	//-----------------------------------------------------------------------------
	void PrefetchSound(const char* name)//CBaseEntity::
	{
		enginesound->PrefetchSound(name);
	}
	
private:
	bool g_bPermitDirectSoundPrecache = false;
	
};

static CSoundEmitterSystem g_SoundEmitterSystem( "CSoundEmitterSystem" );
ISoundEmitterSystem* g_pSoundEmitterSystem = &g_SoundEmitterSystem;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CSoundEmitterSystem, ISoundEmitterSystem,
	SOUNDEMITTERSYSTEM_INTERFACE_VERSION, g_SoundEmitterSystem);

IGameSystem *SoundEmitterSystem()
{
	return &g_SoundEmitterSystem;
}

// HACK HACK:  Do we need to pull the entire SENTENCEG_* wrapper over to the client .dll?
//#if defined( CLIENT_DLL )
//int SENTENCEG_Lookup(const char *sample)
//{
//	return engine->SentenceIndexFromName( sample + 1 );
//}
//#endif


