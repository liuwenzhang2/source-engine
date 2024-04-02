//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//


#include "server_pch.h"
#include "client.h"
#include "sv_packedentities.h"
#include "bspfile.h"
#include "eiface.h"
#include "dt_send_eng.h"
#include "dt_common_eng.h"
#include "changeframelist.h"
#include "sv_main.h"
#include "hltvserver.h"
#if defined( REPLAY_ENABLED )
#include "replayserver.h"
#endif
#include "dt_instrumentation_server.h"
#include "LocalNetworkBackdoor.h"
#include "tier0/vprof.h"
#include "host.h"
#include "networkstringtableserver.h"
#include "networkstringtable.h"
#include "utlbuffer.h"
#include "dt.h"
#include "con_nprint.h"
#include "smooth_average.h"
#include "vengineserver_impl.h"
#include "tier0/vcrmode.h"
#include "vstdlib/jobthread.h"
#include "enginethreads.h"

#ifdef SWDS
IClientEntityList *entitylist = NULL;
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Returns false and calls Host_Error if the edict's pvPrivateData is NULL.
//static inline bool SV_EnsurePrivateData(edict_t *pEdict)
//{
//	if(pEdict->GetUnknown())
//	{
//		return true;
//	}
//	else
//	{
//		Host_Error("SV_EnsurePrivateData: pEdict->pvPrivateData==NULL (ent %d).\n", pEdict - sv.edicts);
//		return false;
//	}
//}





// Returns the SendTable that should be used with the specified edict.
//SendTable* GetEntSendTable(edict_t *pEdict)
//{
//	if ( pEdict->GetNetworkable() )
//	{
//		ServerClass *pClass = pEdict->GetNetworkable()->GetServerClass();
//		if ( pClass )
//		{
//			return pClass->m_pTable;
//		}
//	}
//
//	return NULL;
//}









//-----------------------------------------------------------------------------
// Writes the compressed packet of entities to all clients
//-----------------------------------------------------------------------------

//void SV_ComputeClientPacks( 
//	int clientCount, 
//	CGameClient **clients,
//	CFrameSnapshot *snapshot )
//{
//	
//
//	
//}



// If the table's ID is -1, writes its info into the buffer and increments curID.
void SV_MaybeWriteSendTable( SendTable *pTable, bf_write &pBuf, bool bNeedDecoder )
{
	// Already sent?
	if ( pTable->GetWriteFlag() )
		return;

	pTable->SetWriteFlag( true );

	SVC_SendTable sndTbl;

	byte	tmpbuf[4096];
	sndTbl.m_DataOut.StartWriting( tmpbuf, sizeof(tmpbuf) );
	
	// write send table properties into message buffer
	SendTable_WriteInfos(pTable, &sndTbl.m_DataOut );

	sndTbl.m_bNeedsDecoder = bNeedDecoder;

	// write message to stream
	sndTbl.WriteToBuffer( pBuf );
}

// Calls SV_MaybeWriteSendTable recursively.
void SV_MaybeWriteSendTable_R( SendTable *pTable, bf_write &pBuf )
{
	SV_MaybeWriteSendTable( pTable, pBuf, false );

	// Make sure we send child send tables..
	for(int i=0; i < pTable->m_nProps; i++)
	{
		SendProp *pProp = &pTable->m_pProps[i];

		if( pProp->m_Type == DPT_DataTable )
			SV_MaybeWriteSendTable_R( pProp->GetDataTable(), pBuf );
	}
}


// Sets up SendTable IDs and sends an svc_sendtable for each table.
void SV_WriteSendTables( ServerClass *pClasses, bf_write &pBuf )
{
	ServerClass *pCur;

	DataTable_ClearWriteFlags( pClasses );

	// First, we send all the leaf classes. These are the ones that will need decoders
	// on the client.
	for ( pCur=pClasses; pCur; pCur=pCur->m_pNext )
	{
		SV_MaybeWriteSendTable( pCur->m_pTable, pBuf, true );
	}

	// Now, we send their base classes. These don't need decoders on the client
	// because we will never send these SendTables by themselves.
	for ( pCur=pClasses; pCur; pCur=pCur->m_pNext )
	{
		SV_MaybeWriteSendTable_R( pCur->m_pTable, pBuf );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : crc - 
//-----------------------------------------------------------------------------
void SV_ComputeClassInfosCRC( CRC32_t* crc )
{
	ServerClass *pClasses = serverGameDLL->GetAllServerClasses();

	for ( ServerClass *pClass=pClasses; pClass; pClass=pClass->m_pNext )
	{
		CRC32_ProcessBuffer( crc, (void *)pClass->m_pNetworkName, Q_strlen( pClass->m_pNetworkName ) );
		CRC32_ProcessBuffer( crc, (void *)pClass->m_pTable->GetName(), Q_strlen(pClass->m_pTable->GetName() ) );
	}
}

void CGameServer::AssignClassIds()
{
	ServerClass *pClasses = serverGameDLL->GetAllServerClasses();

	// Count the number of classes.
	int nClasses = 0;
	for ( ServerClass *pCount=pClasses; pCount; pCount=pCount->m_pNext )
	{
		++nClasses;
	}
	
	// These should be the same! If they're not, then it should detect an explicit create message.
	ErrorIfNot( nClasses <= MAX_SERVER_CLASSES,
		("CGameServer::AssignClassIds: too many server classes (%i, MAX = %i).\n", nClasses, MAX_SERVER_CLASSES );
	);

	serverclasses = nClasses;
	serverclassbits = Q_log2( serverclasses ) + 1;

	bool bSpew = CommandLine()->FindParm( "-netspike" ) != 0;

	int curID = 0;
	for ( ServerClass *pClass=pClasses; pClass; pClass=pClass->m_pNext )
	{
		pClass->m_ClassID = curID++;

		if ( bSpew )
		{
			Msg( "%d == '%s'\n", pClass->m_ClassID, pClass->GetName() );
		}
	}
}

// Assign each class and ID and write an svc_classinfo for each one.
void SV_WriteClassInfos(ServerClass *pClasses, bf_write &pBuf)
{
	// Assert( sv.serverclasses < MAX_SERVER_CLASSES );
	
	SVC_ClassInfo classinfomsg;

	classinfomsg.m_bCreateOnClient = false;
	
	for ( ServerClass *pClass=pClasses; pClass; pClass=pClass->m_pNext )
	{
		SVC_ClassInfo::class_t svclass;

		svclass.classID = pClass->m_ClassID;
		Q_strncpy( svclass.datatablename, pClass->m_pTable->GetName(), sizeof(svclass.datatablename) );
		Q_strncpy( svclass.classname, pClass->m_pNetworkName, sizeof(svclass.classname) );
				
		classinfomsg.m_Classes.AddToTail( svclass );  // add all known classes to message
	}

	classinfomsg.WriteToBuffer( pBuf );
}

// This is implemented for the datatable code so its warnings can include an object's classname.
const char* GetObjectClassName( int objectID )
{
	if ( objectID >= 0 && objectID < serverEntitylist->IndexOfHighestEdict() )
	{
		return serverEntitylist->GetServerNetworkable(objectID)?serverEntitylist->GetServerEntity(objectID)->GetClassName():"";
	}
	else
	{
		return "[unknown]";
	}
}



