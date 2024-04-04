//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//


#include "server_pch.h"
#include <eiface.h>
#include <dt_send.h>
#include <utllinkedlist.h>
#include "tier0/etwprof.h"
#include "dt_send_eng.h"
#include "dt.h"
#include "net_synctags.h"
#include "dt_instrumentation_server.h"
#include "LocalNetworkBackdoor.h"
#include "ents_shared.h"
#include "hltvserver.h"
#include "replayserver.h"
#include "tier0/vcrmode.h"
#include "framesnapshot.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//extern ConVar g_CV_DTWatchEnt;

//-----------------------------------------------------------------------------
// Delta timing stuff.
//-----------------------------------------------------------------------------

//static ConVar		sv_deltatime( "sv_deltatime", "0", 0, "Enable profiling of CalcDelta calls" );
//static ConVar		sv_deltaprint( "sv_deltaprint", "0", 0, "Print accumulated CalcDelta profiling data (only if sv_deltatime is on)" );



//class CChangeTrack
//{
//public:
//	char		*m_pName;
//	int			m_nChanged;
//	int			m_nUnchanged;
//	
//	CCycleCount	m_Count;
//	CCycleCount	m_EncodeCount;
//};


//static CUtlLinkedList<CChangeTrack*, int> g_Tracks;






//-----------------------------------------------------------------------------
// Delta timing helpers.
//-----------------------------------------------------------------------------

//CChangeTrack* GetChangeTrack( const char *pName )
//{
//	FOR_EACH_LL( g_Tracks, i )
//	{
//		CChangeTrack *pCur = g_Tracks[i];
//
//		if ( stricmp( pCur->m_pName, pName ) == 0 )
//			return pCur;
//	}
//
//	CChangeTrack *pCur = new CChangeTrack;
//	int len = strlen(pName)+1;
//	pCur->m_pName = new char[len];
//	Q_strncpy( pCur->m_pName, pName, len );
//	pCur->m_nChanged = pCur->m_nUnchanged = 0;
//	
//	g_Tracks.AddToTail( pCur );
//	
//	return pCur;
//}


//void PrintChangeTracks()
//{
//	ConMsg( "\n\n" );
//	ConMsg( "------------------------------------------------------------------------\n" );
//	ConMsg( "CalcDelta MS / %% time / Encode MS / # Changed / # Unchanged / Class Name\n" );
//	ConMsg( "------------------------------------------------------------------------\n" );
//
//	CCycleCount total, encodeTotal;
//	FOR_EACH_LL( g_Tracks, i )
//	{
//		CChangeTrack *pCur = g_Tracks[i];
//		CCycleCount::Add( pCur->m_Count, total, total );
//		CCycleCount::Add( pCur->m_EncodeCount, encodeTotal, encodeTotal );
//	}
//
//	FOR_EACH_LL( g_Tracks, j )
//	{
//		CChangeTrack *pCur = g_Tracks[j];
//	
//		ConMsg( "%6.2fms       %5.2f    %6.2fms    %4d        %4d          %s\n", 
//			pCur->m_Count.GetMillisecondsF(),
//			pCur->m_Count.GetMillisecondsF() * 100.0f / total.GetMillisecondsF(),
//			pCur->m_EncodeCount.GetMillisecondsF(),
//			pCur->m_nChanged, 
//			pCur->m_nUnchanged, 
//			pCur->m_pName
//			);
//	}
//
//	ConMsg( "\n\n" );
//	ConMsg( "Total CalcDelta MS: %.2f\n\n", total.GetMillisecondsF() );
//	ConMsg( "Total Encode    MS: %.2f\n\n", encodeTotal.GetMillisecondsF() );
//}











// Calculates the delta between the two states and writes the delta and the new properties
// into u.m_pBuf. Returns false if the states are the same.
//
// Also uses the IFrameChangeList in pTo to come up with a smaller set of properties to delta against.
// It deltas against any properties that have changed since iFromFrame.
// If iFromFrame is -1, then it deltas all properties.
//static int SV_CalcDeltaAndWriteProps( 
//	CEntityWriteInfo &u, 
//	
//	const void *pFromData,
//	int nFromBits, 
//
//	PackedEntity *pTo
//	)
//{
//	// Calculate the delta props.
//	int deltaProps[MAX_DATATABLE_PROPS];
//	void *pToData = pTo->GetData();
//	int nToBits = pTo->GetNumBits();
//	SendTable *pToTable = pTo->m_pServerClass->m_pTable;
//
//	// TODO if our baseline is compressed, uncompress first
//	Assert( !pTo->IsCompressed() );
//
//	int nDeltaProps = SendTable_CalcDelta(
//		pToTable, 
//		
//		pFromData,
//		nFromBits,
//		
//		pToData,
//		nToBits,
//		
//		deltaProps,
//		ARRAYSIZE( deltaProps ),
//		
//		pTo->m_nEntityIndex	);
//
//	
//
//	// Cull out props given what the proxies say.
//	int culledProps[MAX_DATATABLE_PROPS];
//	
//	int nCulledProps = 0;
//	if ( nDeltaProps )
//	{
//		nCulledProps = SendTable_CullPropsFromProxies(
//			pToTable,
//			deltaProps, 
//			nDeltaProps,
//			u.m_nClientEntity-1,			
//			NULL,
//			-1,
//
//			pTo->GetRecipients(),
//			pTo->GetNumRecipients(),
//
//			culledProps,
//			ARRAYSIZE( culledProps ) );
//	}
//
//	
//	// Write the properties.
//	SendTable_WritePropList( 
//		pToTable,
//		
//		pToData,				// object data
//		pTo->GetNumBits(),
//
//		u.m_pBuf,				// output buffer
//
//		pTo->m_nEntityIndex,
//		culledProps,
//		nCulledProps );
//
//	return nCulledProps;
//}










//static inline ServerClass* GetEntServerClass(edict_t *pEdict)
//{
//	return pEdict->GetNetworkable()->GetServerClass();
//}



























