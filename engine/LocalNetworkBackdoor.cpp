//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//
#include "LocalNetworkBackdoor.h"
#include "server_class.h"
#include "client_class.h"
#include "server.h"
#include "eiface.h"
#include "cdll_engine_int.h"
#include "dt_localtransfer.h"
#include "icliententity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CLocalNetworkBackdoor *g_pLocalNetworkBackdoor = NULL;

#ifndef SWDS
// This is called 
void CLocalNetworkBackdoor::InitFastCopy()
{
	if ( !cl.m_NetChannel->IsLoopback() )
		return;


	const CStandardSendProxies *pSendProxies = NULL;

	// If the game server is greater than v4, then it is using the new proxy format.
	if ( g_iServerGameDLLVersion >= 5 ) // check server version
	{
		pSendProxies = serverGameDLL->GetStandardSendProxies();
	}
	else
	{
		// If the game server is older than v4, it is using the old proxy; we set the new proxy members to the 
		// engine's copy.
		static CStandardSendProxies compatSendProxy = *serverGameDLL->GetStandardSendProxies();

		compatSendProxy.m_DataTableToDataTable = g_StandardSendProxies.m_DataTableToDataTable;
		compatSendProxy.m_SendLocalDataTable = g_StandardSendProxies.m_SendLocalDataTable;
		compatSendProxy.m_ppNonModifiedPointerProxies = g_StandardSendProxies.m_ppNonModifiedPointerProxies;

		pSendProxies = &compatSendProxy;
	} 

	const CStandardRecvProxies *pRecvProxies = g_ClientDLL->GetStandardRecvProxies();

	int nFastCopyProps = 0;
	int nSlowCopyProps = 0;

	for ( int iClass=0; iClass < cl.m_nServerClasses; iClass++ )
	{
		ClientClass *pClientClass = cl.GetClientClass(iClass);
		if ( !pClientClass ) 
			Error( "InitFastCopy - missing client class %d (Should be equivelent of server class: %s)", iClass, cl.m_pServerClasses[iClass].m_ClassName );

		ServerClass *pServerClass = SV_FindServerClass( pClientClass->GetName() );
		if ( !pServerClass )
			Error( "InitFastCopy - missing server class %s", pClientClass->GetName() );

		LocalTransfer_InitFastCopy(
			pServerClass->m_pTable,
			pSendProxies,
			pClientClass->m_pRecvTable,
			pRecvProxies,
			nSlowCopyProps,
			nFastCopyProps
			);
	}

	int percentFast = (nFastCopyProps * 100 ) / (nSlowCopyProps + nFastCopyProps + 1);
	if ( percentFast <= 55 )
	{
		// This may not be a real problem, but at the time this code was added, 67% of the
		// properties were able to be copied without proxies. If percentFast goes to 0 or some
		// really low number suddenly, then something probably got screwed up.
		Assert( false );
		Warning( "InitFastCopy: only %d%% fast props. Bug?\n", percentFast );
	}
} 
#endif

void CLocalNetworkBackdoor::StartEntityStateUpdate()
{
	m_EntsAlive.ClearAll();
	m_nEntsCreated = 0;
	m_nEntsChanged = 0;

	// signal client that we start updating entities
	ClientDLL_FrameStageNotify( FRAME_NET_UPDATE_START );
}

void CLocalNetworkBackdoor::EndEntityStateUpdate()
{
	ClientDLL_FrameStageNotify( FRAME_NET_UPDATE_POSTDATAUPDATE_START );

	// Handle entities created.
	int i;
	for ( i=0; i < m_nEntsCreated; i++ )
	{
		MDLCACHE_CRITICAL_SECTION_( g_pMDLCache );

		int iEdict = m_EntsCreatedIndices[i];
		CCachedEntState *pCached = &m_CachedEntState[iEdict];
		IEngineObjectClient *pEngineObjectClient = pCached->m_pEngineObjectClient;

		pEngineObjectClient->PostDataUpdate( DATA_UPDATE_CREATED );
		pEngineObjectClient->GetClientNetworkable()->NotifyShouldTransmit(SHOULDTRANSMIT_START);
		pCached->m_bDormant = false;
	}

	// Handle entities changed.
	for ( i=0; i < m_nEntsChanged; i++ )
	{
		MDLCACHE_CRITICAL_SECTION_( g_pMDLCache );

		int iEdict = m_EntsChangedIndices[i];
		m_CachedEntState[iEdict].m_pEngineObjectClient->PostDataUpdate( DATA_UPDATE_DATATABLE_CHANGED );
	}

	ClientDLL_FrameStageNotify( FRAME_NET_UPDATE_POSTDATAUPDATE_END );

	// Handle entities removed (= SV_WriteDeletions() in normal mode)
	int nDWords = m_PrevEntsAlive.GetNumDWords();

	// Handle entities removed.
	for ( i=0; i < nDWords; i++ )
	{
		unsigned long prevEntsAlive = m_PrevEntsAlive.GetDWord( i );
		unsigned long entsAlive = m_EntsAlive.GetDWord( i );
		unsigned long toDelete = (prevEntsAlive ^ entsAlive) & prevEntsAlive;

		if ( toDelete )
		{
			for ( int iBit=0; iBit < 32; iBit++ )
			{
				if ( toDelete & (1 << iBit) )
				{
					int iEdict = (i<<5) + iBit;
					if ( iEdict >= 0 && iEdict < MAX_EDICTS )
					{
						if ( m_CachedEntState[iEdict].m_pEngineObjectClient)
						{
							entitylist->DestroyEntity(m_CachedEntState[iEdict].m_pEngineObjectClient->GetClientEntity());// ->Release();
							m_CachedEntState[iEdict].m_pEngineObjectClient = NULL;
						}
						else
						{
							AssertOnce( !"EndEntityStateUpdate:  Would have crashed with NULL m_pClientEntity\n" );
						}
					}
					else
					{
						AssertOnce( !"EndEntityStateUpdate:  Would have crashed with entity out of range\n" );
					}
				}
			}
		}
	}

	// Remember the previous state of which entities were around.
	m_PrevEntsAlive = m_EntsAlive;

	// end of all entity update activity
	ClientDLL_FrameStageNotify( FRAME_NET_UPDATE_END );

	/*
	#ifdef _DEBUG
	for ( i=0; i <= highest_index; i++ )
	{
	if ( !( m_EntsAlive[i>>5] & (1 << (i & 31)) ) )
	Assert( !m_CachedEntState[i].m_pNetworkable );

	if ( ( m_EntsAlive[i>>5] & (1 << (i & 31)) ) &&
	( m_EntsCreated[i>>5] & (1 << (i & 31)) ) )
	{
	Assert( FindInList( m_EntsCreatedIndices, m_nEntsCreated, i ) );
	}

	if ( (m_EntsAlive[i>>5] & (1 << (i & 31))) && 
	!(m_EntsCreated[i>>5] & (1 << (i & 31))) &&
	(m_EntsChanged[i>>5] & (1 << (i & 31)))
	)
	{
	Assert( FindInList( m_EntsChangedIndices, m_nEntsChanged, i ) );
	}
	}
	#endif
	*/
}

void CLocalNetworkBackdoor::EntityDormant( int iEnt, int iSerialNum )
{
	CCachedEntState *pCached = &m_CachedEntState[iEnt];

	IClientEntity *pClientEntity = pCached->m_pEngineObjectClient? pCached->m_pEngineObjectClient->GetClientEntity():NULL;
	Assert(pClientEntity == entitylist->GetClientEntity( iEnt ) );
	if (pClientEntity)
	{
		Assert( pCached->m_iSerialNumber == pClientEntity->GetRefEHandle().GetSerialNumber() );
		if ( pCached->m_iSerialNumber == iSerialNum )
		{
			m_EntsAlive.Set( iEnt );

			// Tell the game code that this guy is now dormant.
			Assert( pCached->m_bDormant == pClientEntity->GetClientNetworkable()->IsDormant());
			if ( !pCached->m_bDormant )
			{
				pClientEntity->GetClientNetworkable()->NotifyShouldTransmit(SHOULDTRANSMIT_END);
				pCached->m_bDormant = true;
			}
		}
		else
		{
			entitylist->DestroyEntity(pClientEntity);// ->Release();
			pCached->m_pEngineObjectClient = NULL;
			m_PrevEntsAlive.Clear( iEnt ); 
		}
	}
}


void CLocalNetworkBackdoor::AddToPendingDormantEntityList( unsigned short iEdict )
{
	//edict_t *e = &sv.edicts[iEdict];
	if ( !( serverEntitylist->GetServerNetworkable(iEdict)->GetTransmitState() & FL_EDICT_PENDING_DORMANT_CHECK))
	{
		serverEntitylist->GetServerNetworkable(iEdict)->GetTransmitState() |= FL_EDICT_PENDING_DORMANT_CHECK;
		m_PendingDormantEntities.AddToTail( iEdict );
	}
}		

void CLocalNetworkBackdoor::ProcessDormantEntities()
{
	FOR_EACH_LL( m_PendingDormantEntities, i )
	{
		int iEdict = m_PendingDormantEntities[i];
		//edict_t *e = &sv.edicts[iEdict];

		// Make sure the entity still exists and stil has the dontsend flag set.
		if (!serverEntitylist->GetServerNetworkable(iEdict)|| !(serverEntitylist->GetServerNetworkable(iEdict)->GetTransmitState() & FL_EDICT_DONTSEND) )//e->IsFree() || 
		{
			if (serverEntitylist->GetServerNetworkable(iEdict)) {
				serverEntitylist->GetServerNetworkable(iEdict)->GetTransmitState() &= ~FL_EDICT_PENDING_DORMANT_CHECK;
			}
			continue;
		}

		EntityDormant( iEdict, serverEntitylist->GetNetworkSerialNumber(iEdict) );
		serverEntitylist->GetServerNetworkable(iEdict)->GetTransmitState() &= ~FL_EDICT_PENDING_DORMANT_CHECK;
	}
	m_PendingDormantEntities.Purge();
}

void CLocalNetworkBackdoor::EntState( 
					 IServerEntity* pServerEntity,
					 //int iSerialNum, 
					 //int iClass, 
					 //const SendTable *pSendTable, 
					 //const void *pSourceEnt,
					 bool bChanged,
					 bool bShouldTransmit )
{
	int iEnt = pServerEntity->entindex();
	int iSerialNum = serverEntitylist->GetNetworkSerialNumber(iEnt);
	ServerClass* pServerClass = pServerEntity->GetServerClass();
	CCachedEntState *pCached = &m_CachedEntState[iEnt];

	// Remember that this ent is alive.
	m_EntsAlive.Set(iEnt);

	ClientClass *pClientClass = cl.GetClientClass(pServerClass->m_ClassID);
	if ( !pClientClass )
		Error( "CLocalNetworkBackdoor::EntState - missing client class %d", pServerClass->m_ClassID);

	IClientEntity* pClientEntity = pCached->m_pEngineObjectClient ? pCached->m_pEngineObjectClient->GetClientEntity() : NULL;
	Assert(pClientEntity == entitylist->GetClientEntity( iEnt ) );

	if ( !bShouldTransmit )
	{
		if (pClientEntity)
		{
			Assert( pCached->m_iSerialNumber == pClientEntity->GetRefEHandle().GetSerialNumber() );
			if ( pCached->m_iSerialNumber == iSerialNum )
			{
				// Tell the game code that this guy is now dormant.
				Assert( pCached->m_bDormant == pClientEntity->GetClientNetworkable()->IsDormant());
				if ( !pCached->m_bDormant )
				{
					pClientEntity->GetClientNetworkable()->NotifyShouldTransmit(SHOULDTRANSMIT_END);
					pCached->m_bDormant = true;
				}
			}
			else
			{
				entitylist->DestroyEntity(pClientEntity);// ->Release();
				pClientEntity = NULL;
				pCached->m_pEngineObjectClient = NULL;
				// Since we set this above, need to clear it now to avoid assertion in EndEntityStateUpdate()
				m_EntsAlive.Clear(iEnt);
				m_PrevEntsAlive.Clear( iEnt ); 
			}
		}
		else
		{
			m_EntsAlive.Clear( iEnt );
		}
		return;
	}
	// Do we have an entity here already?
	bool bExistedAndWasDormant = false;
	if (pClientEntity)
	{
		// If the serial numbers are different, make it recreate the ent.
		Assert( pCached->m_iSerialNumber == pClientEntity->GetRefEHandle().GetSerialNumber() );
		if ( iSerialNum == pCached->m_iSerialNumber )
		{
			bExistedAndWasDormant = pCached->m_bDormant;
		}
		else
		{
			entitylist->DestroyEntity(pClientEntity);// ->Release();
			pClientEntity = NULL;
			m_PrevEntsAlive.Clear(iEnt);
		}
	}

	// Create the entity?
	bool bCreated = false;
	DataUpdateType_t updateType;
	if (pClientEntity)
	{
		updateType = DATA_UPDATE_DATATABLE_CHANGED;
	}
	else		
	{
		updateType = DATA_UPDATE_CREATED;
		pClientEntity = entitylist->CreateEntityByName(pClientClass->m_pClassName, iEnt, iSerialNum );
		bCreated = true;
		m_EntsCreatedIndices[m_nEntsCreated++] = iEnt;

		pCached->m_iSerialNumber = iSerialNum;
		//pCached->m_pDataPointer = pNet->GetClientNetworkable()->GetDataTableBasePtr();
		pCached->m_pEngineObjectClient = entitylist->GetEngineObject(iEnt);
		// Tracker 73192:  ywb 8/1/07:  We used to get an assertion that the pCached->m_bDormant was not equal to pNet->IsDormant() in ProcessDormantEntities.
		// This appears to be the case if when we get here, the entity is set for Transmit still, but is a dormant entity on the server.
		// Seems safe to go ahead an fill in the cache with the correct data.  Probably was just an oversight.
		pCached->m_bDormant = pClientEntity->GetClientNetworkable()->IsDormant();
	}

	if ( bChanged || bCreated || bExistedAndWasDormant )
	{
		IEngineObjectClient* pEngineObjectClient = entitylist->GetEngineObject(iEnt);
		pEngineObjectClient->PreDataUpdate(updateType);

		Assert( pCached->m_pClientEntity == pClientEntity->GetClientNetworkable()->GetDataTableBasePtr());

		LocalTransfer_TransferEntity(
			serverEntitylist->GetEngineObject(iEnt)->GetNetworkable(),
			//pSendTable, 
			//pSourceEnt, 
			//pClientClass->m_pRecvTable, 
			pEngineObjectClient->GetClientNetworkable(),
			bCreated,
			bExistedAndWasDormant,
			iEnt);
		LocalTransfer_TransferEntity( 
			pServerEntity->GetNetworkable(),
			//pSendTable, 
			//pSourceEnt, 
			//pClientClass->m_pRecvTable, 
			pClientEntity->GetClientNetworkable(),
			bCreated, 
			bExistedAndWasDormant,
			iEnt );

		if ( bExistedAndWasDormant )
		{
			// Set this so we use DATA_UPDATE_CREATED logic
			m_EntsCreatedIndices[m_nEntsCreated++] = iEnt;
		}
		else
		{
			if ( !bCreated )
			{
				m_EntsChangedIndices[m_nEntsChanged++] = iEnt;
			}
		}
	}
}
	

void CLocalNetworkBackdoor::ClearState()
{
	// Clear the cache for all the entities.
	for ( int i=0; i < MAX_EDICTS; i++ )
	{
		CCachedEntState &ces = m_CachedEntState[i];

		ces.m_pEngineObjectClient = NULL;
		ces.m_iSerialNumber = -1;
		ces.m_bDormant = false;
		//ces.m_pDataPointer = NULL;
	}

	m_PrevEntsAlive.ClearAll();
}

void CLocalNetworkBackdoor::StartBackdoorMode() 
{ 
	ClearState();

	for ( int i=0; i < MAX_EDICTS; i++ )
	{
		IClientEntity *pClientEntity = entitylist->GetClientEntity( i );

		CCachedEntState &ces = m_CachedEntState[i];

		if (pClientEntity)
		{
			ces.m_pEngineObjectClient = entitylist->GetEngineObject(i);
			ces.m_iSerialNumber = pClientEntity->GetRefEHandle().GetSerialNumber();
			ces.m_bDormant = pClientEntity->IsDormant();
			//ces.m_pDataPointer = pNet->GetDataTableBasePtr();
			m_PrevEntsAlive.Set( i );
		}
	}
}

void CLocalNetworkBackdoor::StopBackdoorMode()
{
	ClearState();
}

