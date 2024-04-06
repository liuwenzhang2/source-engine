//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include <proto_version.h>
#include <netmessages.h>
#include "hltvclientstate.h"
#include "hltvserver.h"
#include "quakedef.h"
#include "cl_main.h"
#include "host.h"
#include "dt_recv_eng.h"
#include "dt_common_eng.h"
#include "framesnapshot.h"
#include "clientframe.h"
#include "ents_shared.h"
#include "server.h"
#include "eiface.h"
#include "server_class.h"
#include "cdll_engine_int.h"
#include "sv_main.h"
#include "changeframelist.h"
#include "GameEventManager.h"
#include "dt_recv_decoder.h"
#include "utllinkedlist.h"
#include "cl_demo.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// copy message data from in to out buffer
#define CopyDataInToOut(msg)									\
	int	 size = PAD_NUMBER( Bits2Bytes(msg->m_nLength), 4);		\
	byte *buffer = (byte*) stackalloc( size );					\
	msg->m_DataIn.ReadBits( buffer, msg->m_nLength );			\
	msg->m_DataOut.StartWriting( buffer, size, msg->m_nLength );\
	
static void HLTV_Callback_InstanceBaseline( void *object, INetworkStringTable *stringTable, int stringNumber, char const *newString, void const *newData )
{
	// relink server classes to instance baselines
	CHLTVServer *pHLTV = (CHLTVServer*)object;
	pHLTV->LinkInstanceBaselines();
}

extern CUtlLinkedList< CRecvDecoder *, unsigned short > g_RecvDecoders;

extern	ConVar tv_autorecord;
static	ConVar tv_autoretry( "tv_autoretry", "1", 0, "Relay proxies retry connection after network timeout" );
static	ConVar tv_timeout( "tv_timeout", "30", 0, "SourceTV connection timeout in seconds." );
		ConVar tv_snapshotrate("tv_snapshotrate", "16", 0, "Snapshots broadcasted per second" );


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////



CHLTVClientState::CHLTVClientState()
{
	m_pNewClientFrame = NULL;
	m_pCurrentClientFrame = NULL;
	m_bSaveMemory = false;
	m_HLTVDeltaEntitiesDecoder.Init(this);
}

CHLTVClientState::~CHLTVClientState()
{

}


//-----------------------------------------------------------------------------
// Purpose: A svc_signonnum has been received, perform a client side setup
// Output : void CL_SignonReply
//-----------------------------------------------------------------------------
bool CHLTVClientState::SetSignonState ( int state, int count )
{
	//	ConDMsg ("CL_SignonReply: %i\n", cl.signon);

	if ( !CBaseClientState::SetSignonState( state, count ) )
		return false;
	
	Assert ( m_nSignonState == state );

	switch ( m_nSignonState )
	{
		case SIGNONSTATE_CHALLENGE	:	break;
		case SIGNONSTATE_CONNECTED	:	{
											// allow longer timeout
											m_NetChannel->SetTimeout( SIGNON_TIME_OUT );

											m_NetChannel->Clear();
											// set user settings (rate etc)
											NET_SetConVar convars;
											Host_BuildConVarUpdateMessage( &convars, FCVAR_USERINFO, false );

											// let server know that we are a proxy server:
											NET_SetConVar::cvar_t acvar;
											V_strcpy_safe( acvar.name, "tv_relay" );
											V_strcpy_safe( acvar.value, "1" );
											convars.m_ConVars.AddToTail( acvar );

											m_NetChannel->SendNetMsg( convars );
										}
										break;

		case SIGNONSTATE_NEW		:	SendClientInfo();
										break;

		case SIGNONSTATE_PRESPAWN	:	break;
		
		case SIGNONSTATE_SPAWN		:	m_pHLTV->SignonComplete();
										break;

		case SIGNONSTATE_FULL		:	m_NetChannel->SetTimeout( tv_timeout.GetFloat() );
										// start new recording if autorecord is enabled
										if ( tv_autorecord.GetBool() )
										{
											hltv->m_DemoRecorder.StartAutoRecording();
											m_NetChannel->SetDemoRecorder( &hltv->m_DemoRecorder );
										}
										break;

		case SIGNONSTATE_CHANGELEVEL:	m_pHLTV->Changelevel();
										m_NetChannel->SetTimeout( SIGNON_TIME_OUT );  // allow 5 minutes timeout
										break;
	}

	if ( m_nSignonState >= SIGNONSTATE_CONNECTED )
	{
		// tell server that we entered now that state
		NET_SignonState signonState(  m_nSignonState, count );
		m_NetChannel->SendNetMsg( signonState );
	}

	return true;
}

void CHLTVClientState::SendClientInfo( void )
{
	CLC_ClientInfo info;
	
	info.m_nSendTableCRC = SendTable_GetCRC();
	info.m_nServerCount = m_nServerCount;
	info.m_bIsHLTV = true;
#if defined( REPLAY_ENABLED )
	info.m_bIsReplay = false;
#endif
	info.m_nFriendsID = 0;
	info.m_FriendsName[0] = 0;

	// CheckOwnCustomFiles(); // load & verfiy custom player files

	for ( int i=0; i< MAX_CUSTOM_FILES; i++ )
		info.m_nCustomFiles[i] = 0;
	
	m_NetChannel->SendNetMsg( info );
}


void CHLTVClientState::SendPacket()
{
	if ( !IsConnected() )
		return;

	if ( ( net_time < m_flNextCmdTime ) || !m_NetChannel->CanPacket() )  
		return;
	
	if ( IsActive() )
	{
		NET_Tick tick( m_nDeltaTick, host_frametime_unbounded, host_frametime_stddeviation );
		m_NetChannel->SendNetMsg( tick );
	}

	m_NetChannel->SendDatagram( NULL );

	if ( IsActive() )
	{
		// use full update rate when active
		float commandInterval = (2.0f/3.0f) / tv_snapshotrate.GetInt();
		float maxDelta = min ( host_state.interval_per_tick, commandInterval );
		float delta = clamp( (float)(net_time - m_flNextCmdTime), 0.0f, maxDelta );
		m_flNextCmdTime = net_time + commandInterval - delta;
	}
	else
	{
		// during signon process send only 5 packets/second
		m_flNextCmdTime = net_time + ( 1.0f / 5.0f );
	}
}

bool CHLTVClientState::ProcessStringCmd(NET_StringCmd *msg)
{
	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessSetConVar(NET_SetConVar *msg)
{
	if ( !CBaseClientState::ProcessSetConVar( msg ) )
		return false;

	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

void CHLTVClientState::Clear()
{
	CBaseClientState::Clear();

	m_pNewClientFrame = NULL;
	m_pCurrentClientFrame = NULL;
}

bool CHLTVClientState::ProcessServerInfo(SVC_ServerInfo *msg )
{
	// Reset client state
	Clear();

	// is server a HLTV proxy or demo file ?
	if ( !m_pHLTV->IsPlayingBack() )
	{
		if ( !msg->m_bIsHLTV )
		{
			ConMsg ( "Server (%s) is not a SourceTV proxy.\n", m_NetChannel->GetAddress() );
			Disconnect( "Server is not a SourceTV proxy", true );
			return false; 
		}	
	}

	// tell HLTV relay to clear everything
	m_pHLTV->StartRelay();

	// Process the message
	if ( !CBaseClientState::ProcessServerInfo( msg ) )
	{
		Disconnect( "CBaseClientState::ProcessServerInfo failed", true );
		return false;
	}

	m_StringTableContainer = m_pHLTV->m_StringTables;

	Assert( m_StringTableContainer->GetNumTables() == 0); // must be empty

#ifndef SHARED_NET_STRING_TABLES
	// relay uses normal string tables without a change history
	m_StringTableContainer->EnableRollback( false );
#endif

	// copy setting from HLTV client to HLTV server 
	m_pHLTV->m_nGameServerMaxClients = m_nMaxClients;
	m_pHLTV->serverclasses		= m_nServerClasses;
	m_pHLTV->serverclassbits	= m_nServerClassBits;
	m_pHLTV->m_nPlayerSlot		= m_nPlayerSlot;
	
	// copy other settings to HLTV server
	V_memcpy( m_pHLTV->worldmapMD5.bits, msg->m_nMapMD5.bits, MD5_DIGEST_LENGTH );
	m_pHLTV->m_flTickInterval	= msg->m_fTickInterval;

	host_state.interval_per_tick = msg->m_fTickInterval;
		
	Q_strncpy( m_pHLTV->m_szMapname, msg->m_szMapName, sizeof(m_pHLTV->m_szMapname) );
	Q_strncpy( m_pHLTV->m_szSkyname, msg->m_szSkyName, sizeof(m_pHLTV->m_szSkyname) );

	return true;
}

bool CHLTVClientState::ProcessClassInfo( SVC_ClassInfo *msg )
{
	if ( !msg->m_bCreateOnClient )
	{
		ConMsg("HLTV SendTable CRC differs from server.\n");
		Disconnect( "HLTV SendTable CRC differs from server.", true );
		return false;
	}

#ifdef _HLTVTEST
	RecvTable_Term( false );
#endif

	// Create all of the send tables locally
	DataTable_CreateClientTablesFromServerTables();

	// Now create all of the server classes locally, too
	DataTable_CreateClientClassInfosFromServerClasses( this );

	LinkClasses();	// link server and client classes

#ifdef DEDICATED
	bool bAllowMismatches = false;
#else
	bool bAllowMismatches = ( demoplayer && demoplayer->IsPlayingBack() );
#endif // DEDICATED

	if ( !RecvTable_CreateDecoders( serverGameDLL->GetStandardSendProxies(), bAllowMismatches ) ) // create receive table decoders
	{
		Host_EndGame( true, "CL_ParseClassInfo_EndClasses: CreateDecoders failed.\n" );
		return false;
	}

	return true;
}

void CHLTVClientState::PacketEnd( void )
{
	// did we get a snapshot with this packet ?
	if ( m_pNewClientFrame )
	{
		// if so, add a new frame to HLTV
		m_pCurrentClientFrame = m_pHLTV->AddNewFrame( m_pNewClientFrame );
		delete m_pNewClientFrame; // release own refernces
		m_pNewClientFrame = NULL;
	}
}

bool CHLTVClientState::HookClientStringTable( char const *tableName )
{
	INetworkStringTable *table = GetStringTable( tableName );
	if ( !table )
		return false;

	// Hook instance baseline table
	if ( !Q_strcasecmp( tableName, INSTANCE_BASELINE_TABLENAME ) )
	{
		table->SetStringChangedCallback( m_pHLTV,  HLTV_Callback_InstanceBaseline );
		return true;
	}

	return false;
}

void CHLTVClientState::InstallStringTableCallback( char const *tableName )
{
	INetworkStringTable *table = GetStringTable( tableName );

	if ( !table )
		return;

	// Hook instance baseline table
	if ( !Q_strcasecmp( tableName, INSTANCE_BASELINE_TABLENAME ) )
	{
		table->SetStringChangedCallback( m_pHLTV,  HLTV_Callback_InstanceBaseline );
		return;
	}
}

bool CHLTVClientState::ProcessSetView( SVC_SetView *msg )
{
	if ( !CBaseClientState::ProcessSetView( msg ) )
		return false;

	return m_pHLTV->SendNetMsg( *msg );
}

bool CHLTVClientState::ProcessVoiceInit( SVC_VoiceInit *msg )
{
	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessVoiceData( SVC_VoiceData *msg )
{
	int	 size = PAD_NUMBER( Bits2Bytes(msg->m_nLength), 4);
	byte *buffer = (byte*) stackalloc( size );
	msg->m_DataIn.ReadBits( buffer, msg->m_nLength );
	msg->m_DataOut = buffer;

	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessSounds(SVC_Sounds *msg)
{
	CopyDataInToOut( msg );

	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessPrefetch( SVC_Prefetch *msg )
{
	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessFixAngle( SVC_FixAngle *msg )
{
	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessCrosshairAngle( SVC_CrosshairAngle *msg )
{
	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessBSPDecal( SVC_BSPDecal *msg )
{
	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessGameEvent( SVC_GameEvent *msg )
{
	bf_read tmpBuf = msg->m_DataIn;

	IGameEvent *event = g_GameEventManager.UnserializeEvent( &tmpBuf );

	if ( event )
	{
		const char *pszName = event->GetName();

		bool bDontForward = false;

		if ( Q_strcmp( pszName, "hltv_status" ) == 0 )
		{
			m_pHLTV->m_nGlobalSlots = event->GetInt("slots");;
			m_pHLTV->m_nGlobalProxies = event->GetInt("proxies");
			m_pHLTV->m_nGlobalClients = event->GetInt("clients");
			m_pHLTV->m_RootServer.SetFromString( event->GetString("master") );
			bDontForward = true;
		}
		else if ( Q_strcmp( pszName, "hltv_title" ) == 0 )
		{
			// ignore title messages
			bDontForward = true;
		}

		// free event resources
		g_GameEventManager.FreeEvent( event );

		if ( bDontForward )
			return true;
	}

	// forward event
	CopyDataInToOut( msg );

	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessGameEventList( SVC_GameEventList *msg )
{
	// copy message before processing
	SVC_GameEventList tmpMsg = *msg;
	CBaseClientState::ProcessGameEventList( &tmpMsg );

	CopyDataInToOut( msg );

	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessUserMessage( SVC_UserMessage *msg )
{
	CopyDataInToOut( msg );

	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessEntityMessage( SVC_EntityMessage *msg )
{
	CopyDataInToOut( msg );

	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessMenu( SVC_Menu *msg )
{
	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}

bool CHLTVClientState::ProcessPacketEntities( SVC_PacketEntities *entmsg )
{
	if (!GetDeltaEntitiesDecoder()->ProcessPacketEntities(entmsg))
		return false;
	return CBaseClientState::ProcessPacketEntities( entmsg );
}

bool CHLTVClientState::ProcessTempEntities( SVC_TempEntities *msg )
{
	CopyDataInToOut( msg );

	return m_pHLTV->SendNetMsg( *msg ); // relay to server
}



int CHLTVClientState::GetConnectionRetryNumber() const
{
	if ( tv_autoretry.GetBool() )
	{
		// in autoretry mode try extra long
		return CL_CONNECTION_RETRIES * 4;
	}
	else
	{
		return CL_CONNECTION_RETRIES;
	}
}

void CHLTVClientState::ConnectionCrashed(const char *reason)
{
	CBaseClientState::ConnectionCrashed( reason );

	if ( tv_autoretry.GetBool() && m_szRetryAddress[0] )
	{
		Cbuf_AddText( va( "tv_relay %s\n", m_szRetryAddress ) );
	}
}

void CHLTVClientState::ConnectionClosing( const char *reason )
{
	CBaseClientState::ConnectionClosing( reason );

	if ( tv_autoretry.GetBool() && m_szRetryAddress[0] )
	{
		Cbuf_AddText( va( "tv_relay %s\n", m_szRetryAddress ) );
	}
}

void CHLTVClientState::RunFrame()
{
	CBaseClientState::RunFrame();

	if ( m_NetChannel && m_NetChannel->IsTimedOut() && IsConnected() )
	{
		ConMsg ("\nSourceTV connection timed out.\n");
		Disconnect( "nSourceTV connection timed out", true );
		return;
	}

	UpdateStats();
}

void CHLTVClientState::UpdateStats()
{
	if ( m_nSignonState < SIGNONSTATE_FULL )
	{
		m_fNextSendUpdateTime = 0.0f;
		return;
	}

	if ( m_fNextSendUpdateTime > net_time )
		return;

	m_fNextSendUpdateTime = net_time + 8.0f;

	int proxies, slots, clients;

	m_pHLTV->GetRelayStats( proxies, slots, clients );

	proxies += 1; // add self to number of proxies
	slots += m_pHLTV->GetMaxClients();	// add own slots
	clients += m_pHLTV->GetNumClients(); // add own clients

	NET_SetConVar conVars;
	NET_SetConVar::cvar_t acvar;

	Q_strncpy( acvar.name, "hltv_proxies", sizeof(acvar.name) );
	Q_snprintf( acvar.value, sizeof(acvar.value), "%d", proxies  );
	conVars.m_ConVars.AddToTail( acvar );

	Q_strncpy( acvar.name, "hltv_clients", sizeof(acvar.name) );
	Q_snprintf( acvar.value, sizeof(acvar.value), "%d", clients );
	conVars.m_ConVars.AddToTail( acvar );

	Q_strncpy( acvar.name, "hltv_slots", sizeof(acvar.name) );
	Q_snprintf( acvar.value, sizeof(acvar.value), "%d", slots );
	conVars.m_ConVars.AddToTail( acvar );

	Q_strncpy( acvar.name, "hltv_addr", sizeof(acvar.name) );
	Q_snprintf( acvar.value, sizeof(acvar.value), "%s:%u", net_local_adr.ToString(true), m_pHLTV->GetUDPPort()  );
	conVars.m_ConVars.AddToTail( acvar );

	m_NetChannel->SendNetMsg( conVars );
}
	
