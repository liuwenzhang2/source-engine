//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//


#include "server_pch.h"
#include "client.h"
//#include "sv_packedentities.h"
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







