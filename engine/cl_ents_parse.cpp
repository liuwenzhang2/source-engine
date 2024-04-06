//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Parsing of entity network packets.
//
// $NoKeywords: $
//=============================================================================//


#include "client_pch.h"
#include "con_nprint.h"
#include "iprediction.h"
#include "cl_entityreport.h"
#include "dt_recv_eng.h"
#include "net_synctags.h"
#include "ispatialpartitioninternal.h"
#include "LocalNetworkBackdoor.h"
#include "basehandle.h"
#include "dt_localtransfer.h"
#include "iprediction.h"
#include "netmessages.h"
#include "ents_shared.h"
#include "cl_ents_parse.h"
#include "icliententity.h"
#include "hltvclientstate.h"
#include "changeframelist.h"
#include "hltvserver.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar cl_flushentitypacket("cl_flushentitypacket", "0", FCVAR_CHEAT, "For debugging. Force the engine to flush an entity packet.");

// Prints important entity creation/deletion events to console
#if defined( _DEBUG )
static ConVar cl_deltatrace( "cl_deltatrace", "0", 0, "For debugging, print entity creation/deletion info to console." );
#define TRACE_DELTA( text ) if ( cl_deltatrace.GetInt() ) { ConMsg( "%s", text ); };
#else
#define TRACE_DELTA( funcs )
#endif



//-----------------------------------------------------------------------------
// Debug networking stuff.
//-----------------------------------------------------------------------------


// #define DEBUG_NETWORKING 1

#if defined( DEBUG_NETWORKING )
void SpewToFile( char const* pFmt, ... );
static ConVar cl_packettrace( "cl_packettrace", "1", 0, "For debugging, massive spew to file." );
#define TRACE_PACKET( text ) if ( cl_packettrace.GetInt() ) { SpewToFile text ; };
#else
#define TRACE_PACKET( text )
#endif


#if defined( DEBUG_NETWORKING )

//-----------------------------------------------------------------------------
// Opens the recording file
//-----------------------------------------------------------------------------

static FileHandle_t OpenRecordingFile()
{
	FileHandle_t fp = 0;
	static bool s_CantOpenFile = false;
	static bool s_NeverOpened = true;
	if (!s_CantOpenFile)
	{
		fp = g_pFileSystem->Open( "cltrace.txt", s_NeverOpened ? "wt" : "at" );
		if (!fp)
		{
			s_CantOpenFile = true;			
		}
		s_NeverOpened = false;
	}
	return fp;
}

//-----------------------------------------------------------------------------
// Records an argument for a command, flushes when the command is done
//-----------------------------------------------------------------------------

void SpewToFile( char const* pFmt, ... )
{
	static CUtlVector<unsigned char> s_RecordingBuffer;

	char temp[2048];
	va_list args;

	va_start( args, pFmt );
	int len = Q_vsnprintf( temp, sizeof( temp ), pFmt, args );
	va_end( args );
	assert( len < 2048 );

	int idx = s_RecordingBuffer.AddMultipleToTail( len );
	memcpy( &s_RecordingBuffer[idx], temp, len );
	if ( 1 ) //s_RecordingBuffer.Size() > 8192)
	{
		FileHandle_t fp = OpenRecordingFile();
		g_pFileSystem->Write( s_RecordingBuffer.Base(), s_RecordingBuffer.Size(), fp );
		g_pFileSystem->Close( fp );

		s_RecordingBuffer.RemoveAll();
	}
}

#endif // DEBUG_NETWORKING

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------

PackedEntity* PackedEntityDecoder::GetEntityBaseline(int iBaseline, int nEntityIndex)
{
	Assert((iBaseline == 0) || (iBaseline == 1));
	return m_pEntityBaselines[iBaseline][nEntityIndex];
}

void PackedEntityDecoder::FreeEntityBaselines()
{
	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < MAX_EDICTS; j++)
			if (m_pEntityBaselines[i][j])
			{
				delete m_pEntityBaselines[i][j];
				m_pEntityBaselines[i][j] = NULL;
			}
	}
}

void PackedEntityDecoder::SetEntityBaseline(int iBaseline, ClientClass* pClientClass, int index, char* packedData, int length)
{
	VPROF("CBaseClientState::SetEntityBaseline");

	Assert(index >= 0 && index < MAX_EDICTS);
	Assert(pClientClass);
	Assert((iBaseline == 0) || (iBaseline == 1));

	PackedEntity* entitybl = m_pEntityBaselines[iBaseline][index];

	if (!entitybl)
	{
		entitybl = m_pEntityBaselines[iBaseline][index] = new PackedEntity();
	}

	entitybl->m_pClientClass = pClientClass;
	entitybl->m_nEntityIndex = index;
	entitybl->m_pServerClass = NULL;

	// Copy out the data we just decoded.
	entitybl->AllocAndCopyPadded(packedData, length);
}

void PackedEntityDecoder::CopyEntityBaseline(int iFrom, int iTo)
{
	Assert(iFrom != iTo);


	for (int i = 0; i < MAX_EDICTS; i++)
	{
		PackedEntity* blfrom = m_pEntityBaselines[iFrom][i];
		PackedEntity* blto = m_pEntityBaselines[iTo][i];

		if (!blfrom)
		{
			// make sure blto doesn't exists
			if (blto)
			{
				// ups, we already had this entity but our ack got lost
				// we have to remove it again to stay in sync
				delete m_pEntityBaselines[iTo][i];
				m_pEntityBaselines[iTo][i] = NULL;
			}
			continue;
		}

		if (!blto)
		{
			// create new to baseline if none existed before
			blto = m_pEntityBaselines[iTo][i] = new PackedEntity();
			blto->m_pClientClass = NULL;
			blto->m_pServerClass = NULL;
			blto->m_ReferenceCount = 0;
		}

		Assert(blfrom->m_nEntityIndex == i);
		Assert(!blfrom->IsCompressed());

		blto->m_nEntityIndex = blfrom->m_nEntityIndex;
		blto->m_pClientClass = blfrom->m_pClientClass;
		blto->m_pServerClass = blfrom->m_pServerClass;
		blto->AllocAndCopyPadded(blfrom->GetData(), blfrom->GetNumBytes());
	}
}

void PackedEntityDecoder::ReadEnterPvsFromBuffer(CEntityReadInfo& u, IClientEntity* pClientEntity) {
	
	ClientClass* pClientClass = pClientEntity->GetClientClass();
	// Get either the static or instance baseline.
	const void* pFromData;
	int nFromBits;

	PackedEntity* baseline = u.m_bAsDelta ? GetEntityBaseline(u.m_nBaseline, u.m_nNewEntity) : NULL;
	if (baseline && baseline->m_pClientClass == pClientClass)
	{
		Assert(!baseline->IsCompressed());
		pFromData = baseline->GetData();
		nFromBits = baseline->GetNumBits();
	}
	else
	{
		// Every entity must have a static or an instance baseline when we get here.
		ErrorIfNot(
			cl.GetClassBaseline(pClientClass->m_ClassID, &pFromData, &nFromBits),
			("CL_CopyNewEntity: GetClassBaseline(%d) failed.", pClientClass->m_ClassID)
		);

		nFromBits *= 8; // convert to bits
	}

	// Delta from baseline and merge to new baseline
	bf_read fromBuf("CL_CopyNewEntity->fromBuf", pFromData, Bits2Bytes(nFromBits), nFromBits);

	RecvTable* pRecvTable = pClientClass->m_pRecvTable;

	if (!pRecvTable)
		Host_Error("CL_ParseDelta: invalid recv table for ent %d.\n", u.m_nNewEntity);

	if (u.m_bUpdateBaselines)
	{
		// store this baseline in u.m_pUpdateBaselines
		ALIGN4 char packedData[MAX_PACKEDENTITY_DATA] ALIGN4_POST;
		bf_write writeBuf("CL_CopyNewEntity->newBuf", packedData, sizeof(packedData));

		RecvTable_MergeDeltas(pRecvTable, &fromBuf, u.m_pBuf, &writeBuf, -1, NULL, true);

		// set the other baseline
		SetEntityBaseline((u.m_nBaseline == 0) ? 1 : 0, pClientClass, u.m_nNewEntity, packedData, writeBuf.GetNumBytesWritten());

		fromBuf.StartReading(packedData, writeBuf.GetNumBytesWritten());

		RecvTable_Decode(pRecvTable, pClientEntity->GetDataTableBasePtr(), &fromBuf, u.m_nNewEntity, false);

	}
	else
	{
		// write data from baseline into entity
		RecvTable_Decode(pRecvTable, pClientEntity->GetDataTableBasePtr(), &fromBuf, u.m_nNewEntity, false);

		// Now parse in the contents of the network stream.
		RecvTable_Decode(pRecvTable, pClientEntity->GetDataTableBasePtr(), u.m_pBuf, u.m_nNewEntity, true);
	}
}

void PackedEntityDecoder::ReadDeltaEntFromBuffer(CEntityReadInfo& u, IClientEntity* pClientEntity) {
	ClientClass* pClientClass = pClientEntity->GetClientClass();
	RecvTable* pRecvTable = pClientClass->m_pRecvTable;

	if (!pRecvTable)
	{
		Host_Error("CL_ParseDelta: invalid recv table for ent %d.\n", u.m_nNewEntity);
		return;
	}

	RecvTable_Decode(pRecvTable, pClientEntity->GetDataTableBasePtr(), u.m_pBuf, u.m_nNewEntity);
}

void	SpewBitStream( unsigned char* pMem, int bit, int lastbit )
{
	int val = 0;
	char buf[1024];
	char* pTemp = buf;
	int bitcount = 0;
	int charIdx = 1;
	while( bit < lastbit )
	{
		int byte = bit >> 3;
		int bytebit = bit & 0x7;

		val |= ((pMem[byte] & bytebit) != 0) << bitcount;

		++bit;
		++bitcount;

		if (bitcount == 4)
		{
			if ((val >= 0) && (val <= 9))
				pTemp[charIdx] = '0' + val;
			else
				pTemp[charIdx] = 'A' + val - 0xA;
			if (charIdx == 1)
				charIdx = 0;
			else
			{
				charIdx = 1;
				pTemp += 2;
			}
			bitcount = 0;
			val = 0;
		}
	}
	if ((bitcount != 0) || (charIdx != 0))
	{
		if (bitcount > 0)
		{
			if ((val >= 0) && (val <= 9))
				pTemp[charIdx] = '0' + val;
			else
				pTemp[charIdx] = 'A' + val - 0xA;
		}
		if (charIdx == 1)
		{
			pTemp[0] = '0';
		}
		pTemp += 2;
	}
	pTemp[0] = '\0';

	TRACE_PACKET(( "    CL Bitstream %s\n", buf ));
}







//-----------------------------------------------------------------------------
// Purpose: Get the receive table for the specified entity
// Input  : *pEnt - 
// Output : RecvTable*
//-----------------------------------------------------------------------------
//static inline RecvTable* GetEntRecvTable( int entnum )
//{
//	IClientNetworkable *pNet = entitylist->GetClientNetworkable( entnum );
//	if ( pNet )
//		return pNet->GetClientClass()->m_pRecvTable;
//	else
//		return NULL;
//}

//-----------------------------------------------------------------------------
// Purpose: Returns true if the entity index corresponds to a player slot 
// Input  : index - 
// Output : bool
//-----------------------------------------------------------------------------
static inline bool CL_IsPlayerIndex( int index )
{
	return ( index >= 1 && index <= cl.m_nMaxClients );
}


//-----------------------------------------------------------------------------
// Purpose: Bad data was received, just flushes incoming delta data.
//-----------------------------------------------------------------------------
void CL_FlushEntityPacket( CClientFrame *packet, char const *errorString, ... )
{
	con_nprint_t np;
	char str[2048];
	va_list marker;

	// Spit out an error.
	va_start(marker, errorString);
	Q_vsnprintf(str, sizeof(str), errorString, marker);
	va_end(marker);
	
	ConMsg("%s", str);

	np.fixed_width_font = false;
	np.time_to_live = 1.0;
	np.index = 0;
	np.color[ 0 ] = 1.0;
	np.color[ 1 ] = 0.2;
	np.color[ 2 ] = 0.0;
	Con_NXPrintf( &np, "WARNING:  CL_FlushEntityPacket, %s", str );

	// Free packet memory.
	delete packet;
}

//-----------------------------------------------------------------------------
// Purpose: Has the client DLL allocate its data for the object.
// Input  : iEnt - 
//			iClass - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
IClientEntity* DeltaEntitiesDecoder::CreateDLLEntity(int iEnt, int iClass, int iSerialNum)
{
#if defined( _DEBUG )
	IClientNetworkable* pOldNetworkable = entitylist->GetClientNetworkable(iEnt);
	Assert(!pOldNetworkable);
#endif

	ClientClass* pClientClass;
	if ((pClientClass = cl.m_pServerClasses[iClass].m_pClientClass) != NULL)
	{
		TRACE_DELTA(va("Trace %i (%s): create\n", iEnt, pClientClass->m_pNetworkName));
#ifndef _XBOX
		CL_RecordAddEntity(iEnt);
#endif		

		if (!cl.IsActive())
		{
			COM_TimestampedLog("cl:  create '%s'", pClientClass->m_pNetworkName);
		}

		// Create the entity.
		return entitylist->CreateEntityByName(pClientClass->m_pClassName, iEnt, iSerialNum);
	}

	Assert(false);
	return NULL;
}
// ----------------------------------------------------------------------------- //
// Regular handles for ReadPacketEntities.
// ----------------------------------------------------------------------------- //

void DeltaEntitiesDecoder::AddPostDataUpdateCall(CEntityReadInfo& u, int iEnt, DataUpdateType_t updateType)
{
	ErrorIfNot(u.m_nPostDataUpdateCalls < MAX_EDICTS,
		("CL_AddPostDataUpdateCall: overflowed u.m_PostDataUpdateCalls"));

	u.m_PostDataUpdateCalls[u.m_nPostDataUpdateCalls].m_iEnt = iEnt;
	u.m_PostDataUpdateCalls[u.m_nPostDataUpdateCalls].m_UpdateType = updateType;
	++u.m_nPostDataUpdateCalls;
}

void DeltaEntitiesDecoder::CopyNewEntity(
	CEntityReadInfo &u,
	int iClass,
	int iSerialNum
	)
{
	if ( u.m_nNewEntity < 0 || u.m_nNewEntity >= MAX_EDICTS )
	{
		Host_Error ("CL_CopyNewEntity: u.m_nNewEntity < 0 || m_nNewEntity >= MAX_EDICTS");
		return;
	}

	// If it's new, make sure we have a slot for it.
	IClientEntity *pClientEntity = entitylist->GetClientEntity( u.m_nNewEntity );

	if( iClass >= cl.m_nServerClasses )
	{
		Host_Error("CL_CopyNewEntity: invalid class index (%d).\n", iClass);
		return;
	}

	// Delete the entity.
	ClientClass *pClass = cl.m_pServerClasses[iClass].m_pClientClass;
	bool bNew = false;
	if (pClientEntity)
	{
		// if serial number is different, destory old entity
		if (pClientEntity->GetRefEHandle().GetSerialNumber() != iSerialNum )
		{
			DeleteDLLEntity( u.m_nNewEntity, "CopyNewEntity" );
			pClientEntity = NULL; // force a recreate
		}
	}

	if ( !pClientEntity)
	{	
		// Ok, it doesn't exist yet, therefore this is not an "entered PVS" message.
		pClientEntity = CreateDLLEntity( u.m_nNewEntity, iClass, iSerialNum );
		if( !pClientEntity)
		{
			const char *pNetworkName = cl.m_pServerClasses[iClass].m_pClientClass ? cl.m_pServerClasses[iClass].m_pClientClass->m_pNetworkName : "";
			Host_Error( "CL_ParsePacketEntities:  Error creating entity %s(%i)\n", pNetworkName, u.m_nNewEntity );
			return;
		}

		bNew = true;
	}

	int start_bit = u.m_pBuf->GetNumBitsRead();

	DataUpdateType_t updateType = bNew ? DATA_UPDATE_CREATED : DATA_UPDATE_DATATABLE_CHANGED;
	pClientEntity->PreDataUpdate( updateType );

	m_PackedEntityDecoder.ReadEnterPvsFromBuffer(u, pClientEntity);

	AddPostDataUpdateCall( u, u.m_nNewEntity, updateType );

	// If ent doesn't think it's in PVS, signal that it is
	Assert( u.m_pTo->last_entity <= u.m_nNewEntity );
	u.m_pTo->last_entity = u.m_nNewEntity;
	Assert( !u.m_pTo->transmit_entity.Get(u.m_nNewEntity) );
	u.m_pTo->transmit_entity.Set( u.m_nNewEntity );

	//
	// Net stats..
	//
	int bit_count = u.m_pBuf->GetNumBitsRead() - start_bit;
#ifndef _XBOX
	if ( cl_entityreport.GetBool() )
		CL_RecordEntityBits( u.m_nNewEntity, bit_count );
#endif
	if ( CL_IsPlayerIndex( u.m_nNewEntity ) )
	{
		if ( u.m_nNewEntity == cl.m_nPlayerSlot + 1 )
		{
			u.m_nLocalPlayerBits += bit_count;
		}
		else
		{
			u.m_nOtherPlayerBits += bit_count;
		}
	}
}




static float g_flLastPerfRequest = 0.0f;

static ConVar cl_debug_player_perf( "cl_debug_player_perf", "0", 0 );

//-----------------------------------------------------------------------------
// Purpose: When a delta command is received from the server
//  We need to grab the entity # out of it any the bit settings, too.
//  Returns -1 if there are no more entities.
// Input  : &bRemove - 
//			&bIsNew - 
// Output : int
//-----------------------------------------------------------------------------
void DeltaEntitiesDecoder::ParseDeltaHeader(CEntityReadInfo& u)
{
	u.m_UpdateFlags = FHDR_ZERO;

#ifdef DEBUG_NETWORKING
	int startbit = u.m_pBuf->GetNumBitsRead();
#endif
	SyncTag_Read(u.m_pBuf, "Hdr");

	u.m_nNewEntity = u.m_nHeaderBase + 1 + u.m_pBuf->ReadUBitVar();


	u.m_nHeaderBase = u.m_nNewEntity;

	// leave pvs flag
	if (u.m_pBuf->ReadOneBit() == 0)
	{
		// enter pvs flag
		if (u.m_pBuf->ReadOneBit() != 0)
		{
			u.m_UpdateFlags |= FHDR_ENTERPVS;
		}
	}
	else
	{
		u.m_UpdateFlags |= FHDR_LEAVEPVS;

		// Force delete flag
		if (u.m_pBuf->ReadOneBit() != 0)
		{
			u.m_UpdateFlags |= FHDR_DELETE;
		}
	}
	// Output the bitstream...
#ifdef DEBUG_NETWORKING
	int lastbit = u.m_pBuf->GetNumBitsRead();
	{
		void	SpewBitStream(unsigned char* pMem, int bit, int lastbit);
		SpewBitStream((byte*)u.m_pBuf->m_pData, startbit, lastbit);
	}
#endif
}

// Returns false if you should stop reading entities.
bool DeltaEntitiesDecoder::DetermineUpdateType(CEntityReadInfo& u)
{
	if (!u.m_bIsEntity || (u.m_nNewEntity > u.m_nOldEntity))
	{
		// If we're at the last entity, preserve whatever entities followed it in the old packet.
		// If newnum > oldnum, then the server skipped sending entities that it wants to leave the state alone for.
		if (!u.m_pFrom || (u.m_nOldEntity > u.m_pFrom->last_entity))
		{
			Assert(!u.m_bIsEntity);
			u.m_UpdateType = Finished;
			return false;
		}

		// Preserve entities until we reach newnum (ie: the server didn't send certain entities because
		// they haven't changed).
		u.m_UpdateType = PreserveEnt;
	}
	else
	{
		if (u.m_UpdateFlags & FHDR_ENTERPVS)
		{
			u.m_UpdateType = EnterPVS;
		}
		else if (u.m_UpdateFlags & FHDR_LEAVEPVS)
		{
			u.m_UpdateType = LeavePVS;
		}
		else
		{
			u.m_UpdateType = DeltaEnt;
		}
	}

	return true;
}

void DeltaEntitiesDecoder::ReadDeletions(CEntityReadInfo& u)
{
	VPROF("ReadDeletions");
	while (u.m_pBuf->ReadOneBit() != 0)
	{
		int idx = u.m_pBuf->ReadUBitLong(MAX_EDICT_BITS);

		Assert(!u.m_pTo->transmit_entity.Get(idx));

		DeleteDLLEntity(idx, "ReadDeletions");
	}
}

void DeltaEntitiesDecoder::ReadEnterPVS(CEntityReadInfo& u)
{
	VPROF("ReadEnterPVS");

	TRACE_PACKET(("  CL Enter PVS (%d)\n", u.m_nNewEntity));

	int iClass = u.m_pBuf->ReadUBitLong(cl.m_nServerClassBits);

	int iSerialNum = u.m_pBuf->ReadUBitLong(NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS);

	CopyNewEntity(u, iClass, iSerialNum);

}

//-----------------------------------------------------------------------------
// Purpose: Frees the client DLL's binding to the object.
// Input  : iEnt - 
//-----------------------------------------------------------------------------
void DeltaEntitiesDecoder::DeleteDLLEntity(int iEnt, const char* reason, bool bOnRecreatingAllEntities)
{
	IClientEntity* pClientEntity = entitylist->GetClientEntity(iEnt);

	if (pClientEntity)
	{
		ClientClass* pClientClass = pClientEntity->GetClientClass();
		TRACE_DELTA(va("Trace %i (%s): delete (%s)\n", iEnt, pClientClass ? pClientClass->m_pNetworkName : "unknown", reason));
#ifndef _XBOX
		CL_RecordDeleteEntity(iEnt, pClientClass);
#endif
		if (bOnRecreatingAllEntities)
		{
			pClientEntity->SetDestroyedOnRecreateEntities();
		}

		entitylist->DestroyEntity(pClientEntity);// ->Release();
	}
}

void DeltaEntitiesDecoder::ReadLeavePVS(CEntityReadInfo& u)
{
	VPROF("ReadLeavePVS");
	// Sanity check.
	if (!u.m_bAsDelta)
	{
		Assert(0); // cl.validsequence = 0;
		ConMsg("WARNING: LeavePVS on full update");
		u.m_UpdateType = Failed;	// break out
		return;
	}

	Assert(!u.m_pTo->transmit_entity.Get(u.m_nOldEntity));

	if (u.m_UpdateFlags & FHDR_DELETE)
	{
		DeleteDLLEntity(u.m_nOldEntity, "ReadLeavePVS");
	}

}

void DeltaEntitiesDecoder::CopyExistingEntity(CEntityReadInfo& u)
{
	int start_bit = u.m_pBuf->GetNumBitsRead();

	IClientEntity* pClientEntity = entitylist->GetClientEntity(u.m_nNewEntity);
	if (!pClientEntity)
	{
		Host_Error("CL_CopyExistingEntity: missing client entity %d.\n", u.m_nNewEntity);
		return;
	}

	Assert(u.m_pFrom->transmit_entity.Get(u.m_nNewEntity));

	// Read raw data from the network stream
	pClientEntity->PreDataUpdate(DATA_UPDATE_DATATABLE_CHANGED);

	m_PackedEntityDecoder.ReadDeltaEntFromBuffer(u, pClientEntity);

	AddPostDataUpdateCall(u, u.m_nNewEntity, DATA_UPDATE_DATATABLE_CHANGED);

	u.m_pTo->last_entity = u.m_nNewEntity;
	Assert(!u.m_pTo->transmit_entity.Get(u.m_nNewEntity));
	u.m_pTo->transmit_entity.Set(u.m_nNewEntity);

	int bit_count = u.m_pBuf->GetNumBitsRead() - start_bit;
#ifndef _XBOX
	if (cl_entityreport.GetBool())
		CL_RecordEntityBits(u.m_nNewEntity, bit_count);
#endif
	if (CL_IsPlayerIndex(u.m_nNewEntity))
	{
		if (u.m_nNewEntity == cl.m_nPlayerSlot + 1)
		{
			u.m_nLocalPlayerBits += bit_count;
		}
		else
		{
			u.m_nOtherPlayerBits += bit_count;
		}
	}
}

void DeltaEntitiesDecoder::ReadDeltaEnt(CEntityReadInfo& u)
{
	VPROF("ReadDeltaEnt");
	CopyExistingEntity(u);

}

void DeltaEntitiesDecoder::PreserveExistingEntity(int nOldEntity)
{
	IClientNetworkable* pEnt = entitylist->GetClientNetworkable(nOldEntity);
	if (!pEnt)
	{
		// If you hit this, this is because there's a networked client entity that got released
		// by some method other than a server update.  This can happen if client code calls
		// release on a networked entity.

#if defined( STAGING_ONLY )
		// Try to use the cl_removeentity_backtrace_capture code in cliententitylist.cpp...
		Msg("%s: missing client entity %d.\n", __FUNCTION__, nOldEntity);
		Cbuf_AddText(CFmtStr("cl_removeentity_backtrace_dump %d\n", nOldEntity));
		Cbuf_Execute();
#endif // STAGING_ONLY

		Host_Error("CL_PreserveExistingEntity: missing client entity %d.\n", nOldEntity);
		return;
	}

	//	pEnt->OnDataUnchangedInPVS();
}

void DeltaEntitiesDecoder::ReadPreserveEnt(CEntityReadInfo& u)
{
	VPROF("ReadPreserveEnt");
	if (!u.m_bAsDelta)  // Should never happen on a full update.
	{
		Assert(0); // cl.validsequence = 0;
		ConMsg("WARNING: PreserveEnt on full update");
		u.m_UpdateType = Failed;	// break out
		return;
	}

	Assert(u.m_pFrom->transmit_entity.Get(u.m_nOldEntity));

	// copy one of the old entities over to the new packet unchanged

	// XXX(JohnS): This was historically checking for NewEntity overflow, though this path does not care (and new entity
	//             may be -1).  The old entity bounds check here seems like what was intended, but since nNewEntity
	//             should not be overflowed either, I've left that check in case it was guarding against a case I am
	//             overlooking.
	if (u.m_nOldEntity >= MAX_EDICTS || u.m_nOldEntity < 0 || u.m_nNewEntity >= MAX_EDICTS)
	{
		Host_Error("CL_ReadPreserveEnt: Entity out of bounds. Old: %i, New: %i",
			u.m_nOldEntity, u.m_nNewEntity);
	}

	u.m_pTo->last_entity = u.m_nOldEntity;
	u.m_pTo->transmit_entity.Set(u.m_nOldEntity);

	// Zero overhead
	if (cl_entityreport.GetBool())
		CL_RecordEntityBits(u.m_nOldEntity, 0);

	PreserveExistingEntity(u.m_nOldEntity);

}

void DeltaEntitiesDecoder::ReadPacketEntities(CEntityReadInfo& u)
{
	VPROF("ReadPacketEntities");

	// Loop until there are no more entities to read

	u.NextOldEntity();

	while (u.m_UpdateType < Finished)
	{
		u.m_nHeaderCount--;

		u.m_bIsEntity = (u.m_nHeaderCount >= 0) ? true : false;

		if (u.m_bIsEntity)
		{
			ParseDeltaHeader(u);
		}

		u.m_UpdateType = PreserveEnt;

		while (u.m_UpdateType == PreserveEnt)
		{
			// Figure out what kind of an update this is.
			if (DetermineUpdateType(u))
			{
				switch (u.m_UpdateType)
				{
				case EnterPVS:		
					ReadEnterPVS(u);

					if (u.m_nNewEntity == u.m_nOldEntity) // that was a recreate
						u.NextOldEntity();
					break;
				case LeavePVS:		
					ReadLeavePVS(u);

					u.NextOldEntity();
					break;
				case DeltaEnt:		
					ReadDeltaEnt(u);

					u.NextOldEntity();
					break;
				case PreserveEnt:	
					ReadPreserveEnt(u);

					u.NextOldEntity();
					break;
				default:			DevMsg(1, "ReadPacketEntities: unknown updatetype %i\n", u.m_UpdateType);
					break;
				}
			}
		}
	}

	// Now process explicit deletes 
	if (u.m_bAsDelta && u.m_UpdateType == Finished)
	{
		ReadDeletions(u);
	}

	// Something didn't parse...
	if (u.m_pBuf->IsOverflowed())
	{
		Host_Error("CL_ParsePacketEntities:  buffer read overflow\n");
	}

	// If we get an uncompressed packet, then the server is waiting for us to ack the validsequence
	// that we got the uncompressed packet on. So we stop reading packets here and force ourselves to
	// send the clc_move on the next frame.

	if (!u.m_bAsDelta)
	{
		cl.m_flNextCmdTime = 0.0; // answer ASAP to confirm full update tick
	}
}

void DeltaEntitiesDecoder::FreeEntityBaselines() {
	m_PackedEntityDecoder.FreeEntityBaselines();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void DeltaEntitiesDecoder::MarkEntitiesOutOfPVS(CBitVec<MAX_EDICTS>* pvs_flags)
{
	int highest_index = entitylist->GetHighestEntityIndex();
	// Note that we go up to and including the highest_index
	for (int i = 0; i <= highest_index; i++)
	{
		IClientNetworkable* ent = entitylist->GetClientNetworkable(i);
		if (!ent)
			continue;

		// FIXME: We can remove IClientEntity here if we keep track of the
		// last frame's entity_in_pvs
		bool curstate = !ent->IsDormant();
		bool newstate = pvs_flags->Get(i) ? true : false;

		if (!curstate && newstate)
		{
			// Inform the client entity list that the entity entered the PVS
			ent->NotifyShouldTransmit(SHOULDTRANSMIT_START);
		}
		else if (curstate && !newstate)
		{
			// Inform the client entity list that the entity left the PVS
			ent->NotifyShouldTransmit(SHOULDTRANSMIT_END);
#ifndef _XBOX
			CL_RecordLeavePVS(i);
#endif
		}
	}
}

void DeltaEntitiesDecoder::CallPostDataUpdates(CEntityReadInfo& u)
{
	for (int i = 0; i < u.m_nPostDataUpdateCalls; i++)
	{
		MDLCACHE_CRITICAL_SECTION_(g_pMDLCache);
		CPostDataUpdateCall* pCall = &u.m_PostDataUpdateCalls[i];

		IClientNetworkable* pEnt = entitylist->GetClientNetworkable(pCall->m_iEnt);
		ErrorIfNot(pEnt,
			("CL_CallPostDataUpdates: missing ent %d", pCall->m_iEnt));

		pEnt->PostDataUpdate(pCall->m_UpdateType);
	}
}
//-----------------------------------------------------------------------------
// Purpose: An svc_packetentities has just been parsed, deal with the
//  rest of the data stream.  This can be a delta from the baseline or from a previous
//  client frame for this client.
// Input  : delta - 
//			*playerbits - 
// Output : void CL_ParsePacketEntities
//-----------------------------------------------------------------------------
bool DeltaEntitiesDecoder::ProcessPacketEntities ( SVC_PacketEntities *entmsg )
{
	VPROF( "_CL_ParsePacketEntities" );

	// Packed entities for that frame
	// Allocate space for new packet info.
	CClientFrame *newFrame = AllocateFrame();
	newFrame->Init( cl.GetServerTickCount() );
	CClientFrame *oldFrame = NULL;

	// if cl_flushentitypacket is set to N, the next N entity updates will be flushed
	if ( cl_flushentitypacket.GetInt() )
	{	
		// we can't use this, it is too old
		CL_FlushEntityPacket( newFrame, "Forced by cvar\n" );
		cl_flushentitypacket.SetValue( cl_flushentitypacket.GetInt() - 1 );	// Reduce the cvar.
		return false;
	}

	if ( entmsg->m_bIsDelta )
	{
		int nDeltaTicks = cl.GetServerTickCount() - entmsg->m_nDeltaFrom;
		float flDeltaSeconds = TICKS_TO_TIME( nDeltaTicks );

		// If we have cl_debug_player_perf set and we see a huge delta between what we've ack'd to the server and where it's at
		//  ask it for an instantaneous perf snapshot
		if ( cl_debug_player_perf.GetBool() &&
			( flDeltaSeconds > 0.5f ) &&							// delta is pretty out of date
			( ( realtime - g_flLastPerfRequest ) > 5.0f ) )		// haven't requested in a while
		{
			g_flLastPerfRequest = realtime;
			Warning( "Gap in server data, requesting connection perf data\n" );
			cl.SendStringCmd( "playerperf\n" );
		}

		if ( cl.GetServerTickCount() == entmsg->m_nDeltaFrom )
		{
			Host_Error( "Update self-referencing, connection dropped.\n" );
			return false;
		}

		// Otherwise, mark where we are valid to and point to the packet entities we'll be updating from.
		oldFrame = GetClientFrame( entmsg->m_nDeltaFrom );

		if ( !oldFrame )
		{
			CL_FlushEntityPacket( newFrame, "Update delta not found.\n" );
			return false;
		}
	}
	else
	{
		if ( cl_debug_player_perf.GetBool() )
		{
			Warning( "Received uncompressed server update\n" );
		}

		// Clear out the client's entity states..
		for ( int i=0; i <= entitylist->GetHighestEntityIndex(); i++ )
		{
			if (i == 0) {
				continue;
			}
			DeleteDLLEntity( i, "ProcessPacketEntities", true );
		}
	}

	// signal client DLL that we have started updating entities
	ClientDLL_FrameStageNotify( FRAME_NET_UPDATE_START );

	g_nPropsDecoded = 0;

	Assert( entmsg->m_nBaseline >= 0 && entmsg->m_nBaseline < 2 );

	if ( entmsg->m_bUpdateBaseline )
	{
		// server requested to use this snapshot as baseline update
		int nUpdateBaseline = (entmsg->m_nBaseline == 0) ? 1 : 0;
		m_PackedEntityDecoder.CopyEntityBaseline( entmsg->m_nBaseline, nUpdateBaseline );

		// send new baseline acknowledgement(as reliable)
		cl.m_NetChannel->SendNetMsg( CLC_BaselineAck( cl.GetServerTickCount(), entmsg->m_nBaseline ), true );
		
	}

	CEntityReadInfo u;
	u.m_pBuf = &entmsg->m_DataIn;
	u.m_pFrom = oldFrame;
	u.m_pTo = newFrame;
	u.m_bAsDelta = entmsg->m_bIsDelta;
	u.m_nHeaderCount = entmsg->m_nUpdatedEntries;
	u.m_nBaseline = entmsg->m_nBaseline;
	u.m_bUpdateBaselines = entmsg->m_bUpdateBaseline;
	
	// update the entities
	ReadPacketEntities( u );

	ClientDLL_FrameStageNotify( FRAME_NET_UPDATE_POSTDATAUPDATE_START );

	// call PostDataUpdate() for each entity
	CallPostDataUpdates( u );

	ClientDLL_FrameStageNotify( FRAME_NET_UPDATE_POSTDATAUPDATE_END );

	// call NotifyShouldTransmit() for entities that entered or left the PVS
	MarkEntitiesOutOfPVS( &newFrame->transmit_entity );

	// adjust net channel stats

	cl.m_NetChannel->UpdateMessageStats( INetChannelInfo::LOCALPLAYER, u.m_nLocalPlayerBits );
	cl.m_NetChannel->UpdateMessageStats( INetChannelInfo::OTHERPLAYERS, u.m_nOtherPlayerBits );
	cl.m_NetChannel->UpdateMessageStats( INetChannelInfo::ENTITIES, -(u.m_nLocalPlayerBits+u.m_nOtherPlayerBits) );

 	DeleteClientFrames( entmsg->m_nDeltaFrom );

	// If the client has more than 64 frames, the host will start to eat too much memory.
	// TODO: We should enforce this somehow.
	if ( MAX_CLIENT_FRAMES < AddClientFrame( newFrame ) )
	{
		DevMsg( 1, "CL_ProcessPacketEntities: frame window too big (>%i)\n", MAX_CLIENT_FRAMES );	
	}

	// all update activities are finished
	ClientDLL_FrameStageNotify( FRAME_NET_UPDATE_END );

	return true;
}


PackedEntity* CHLTVDeltaEntitiesDecoder::GetEntityBaseline(int iBaseline, int nEntityIndex)
{
	Assert((iBaseline == 0) || (iBaseline == 1));
	return m_pEntityBaselines[iBaseline][nEntityIndex];
}

void CHLTVDeltaEntitiesDecoder::FreeEntityBaselines()
{
	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < MAX_EDICTS; j++)
			if (m_pEntityBaselines[i][j])
			{
				delete m_pEntityBaselines[i][j];
				m_pEntityBaselines[i][j] = NULL;
			}
	}
}

void CHLTVDeltaEntitiesDecoder::SetEntityBaseline(int iBaseline, ClientClass* pClientClass, int index, char* packedData, int length)
{
	VPROF("CBaseClientState::SetEntityBaseline");

	Assert(index >= 0 && index < MAX_EDICTS);
	Assert(pClientClass);
	Assert((iBaseline == 0) || (iBaseline == 1));

	PackedEntity* entitybl = m_pEntityBaselines[iBaseline][index];

	if (!entitybl)
	{
		entitybl = m_pEntityBaselines[iBaseline][index] = new PackedEntity();
	}

	entitybl->m_pClientClass = pClientClass;
	entitybl->m_nEntityIndex = index;
	entitybl->m_pServerClass = NULL;

	// Copy out the data we just decoded.
	entitybl->AllocAndCopyPadded(packedData, length);
}

void CHLTVDeltaEntitiesDecoder::CopyEntityBaseline(int iFrom, int iTo)
{
	Assert(iFrom != iTo);


	for (int i = 0; i < MAX_EDICTS; i++)
	{
		PackedEntity* blfrom = m_pEntityBaselines[iFrom][i];
		PackedEntity* blto = m_pEntityBaselines[iTo][i];

		if (!blfrom)
		{
			// make sure blto doesn't exists
			if (blto)
			{
				// ups, we already had this entity but our ack got lost
				// we have to remove it again to stay in sync
				delete m_pEntityBaselines[iTo][i];
				m_pEntityBaselines[iTo][i] = NULL;
			}
			continue;
		}

		if (!blto)
		{
			// create new to baseline if none existed before
			blto = m_pEntityBaselines[iTo][i] = new PackedEntity();
			blto->m_pClientClass = NULL;
			blto->m_pServerClass = NULL;
			blto->m_ReferenceCount = 0;
		}

		Assert(blfrom->m_nEntityIndex == i);
		Assert(!blfrom->IsCompressed());

		blto->m_nEntityIndex = blfrom->m_nEntityIndex;
		blto->m_pClientClass = blfrom->m_pClientClass;
		blto->m_pServerClass = blfrom->m_pServerClass;
		blto->AllocAndCopyPadded(blfrom->GetData(), blfrom->GetNumBytes());
	}
}

void CHLTVDeltaEntitiesDecoder::CopyNewEntity(
	CEntityReadInfo& u,
	int iClass,
	int iSerialNum
)
{
	ServerClass* pServerClass = SV_FindServerClass(iClass);
	Assert(pServerClass);

	ClientClass* pClientClass = m_pHLTVClientState->GetClientClass(iClass);
	Assert(pClientClass);

	const int ent = u.m_nNewEntity;

	// copy class & serial
	CFrameSnapshot* pSnapshot = u.m_pTo->GetSnapshot();
	g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, ent)->m_nSerialNumber = iSerialNum;
	g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, ent)->m_pClass = pServerClass;

	// Get either the static or instance baseline.
	const void* pFromData = NULL;
	int nFromBits = 0;
	int nFromTick = 0;	// MOTODO get tick when baseline last changed

	PackedEntity* baseline = u.m_bAsDelta ? GetEntityBaseline(u.m_nBaseline, ent) : NULL;

	if (baseline && baseline->m_pClientClass == pClientClass)
	{
		Assert(!baseline->IsCompressed());
		pFromData = baseline->GetData();
		nFromBits = baseline->GetNumBits();
	}
	else
	{
		// Every entity must have a static or an instance baseline when we get here.
		ErrorIfNot(
			m_pHLTVClientState->GetClassBaseline(iClass, &pFromData, &nFromBits),
			("HLTV_CopyNewEntity: GetDynamicBaseline(%d) failed.", iClass)
		);
		nFromBits *= 8; // convert to bits
	}

	// create new ChangeFrameList containing all properties set as changed
	int nFlatProps = SendTable_GetNumFlatProps(pServerClass->m_pTable);
	IChangeFrameList* pChangeFrame = NULL;

	if (!m_pHLTVClientState->m_bSaveMemory)
	{
		pChangeFrame = AllocChangeFrameList(nFlatProps, nFromTick);
	}

	// Now make a PackedEntity and store the new packed data in there.
	PackedEntity* pPackedEntity = g_pPackedEntityManager->CreatePackedEntity(pSnapshot, ent, pServerClass, iSerialNum);
	pPackedEntity->SetChangeFrameList(pChangeFrame);
	pPackedEntity->SetServerAndClientClass(pServerClass, pClientClass);

	// Make space for the baseline data.
	ALIGN4 char packedData[MAX_PACKEDENTITY_DATA] ALIGN4_POST;
	bf_read fromBuf("HLTV_ReadEnterPVS1", pFromData, Bits2Bytes(nFromBits), nFromBits);
	bf_write writeBuf("HLTV_ReadEnterPVS2", packedData, sizeof(packedData));

	int changedProps[MAX_DATATABLE_PROPS];

	// decode basline, is compressed against zero values 
	int nChangedProps = RecvTable_MergeDeltas(pClientClass->m_pRecvTable, &fromBuf,
		u.m_pBuf, &writeBuf, -1, changedProps);

	// update change tick in ChangeFrameList
	if (pChangeFrame)
	{
		pChangeFrame->SetChangeTick(changedProps, nChangedProps, pSnapshot->m_nTickCount);
	}

	if (u.m_bUpdateBaselines)
	{
		SetEntityBaseline((u.m_nBaseline == 0) ? 1 : 0, pClientClass, u.m_nNewEntity, packedData, writeBuf.GetNumBytesWritten());
	}

	pPackedEntity->AllocAndCopyPadded(packedData, writeBuf.GetNumBytesWritten());

	// If ent doesn't think it's in PVS, signal that it is
	Assert(u.m_pTo->last_entity <= ent);
	u.m_pTo->last_entity = ent;
	u.m_pTo->transmit_entity.Set(ent);
}

void CHLTVDeltaEntitiesDecoder::ReadEnterPVS(CEntityReadInfo& u)
{
	int iClass = u.m_pBuf->ReadUBitLong(m_pHLTVClientState->m_nServerClassBits);

	int iSerialNum = u.m_pBuf->ReadUBitLong(NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS);

	CopyNewEntity(u, iClass, iSerialNum);

}

void CHLTVDeltaEntitiesDecoder::ReadLeavePVS(CEntityReadInfo& u)
{
	// do nothing, this entity was removed
	Assert(!u.m_pTo->transmit_entity.Get(u.m_nOldEntity));

	if (u.m_UpdateFlags & FHDR_DELETE)
	{
		CFrameSnapshot* pSnapshot = u.m_pTo->GetSnapshot();
		CFrameSnapshotEntry* pEntry = g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, u.m_nOldEntity);

		// clear entity references
		pEntry->m_nSerialNumber = -1;
		pEntry->m_pClass = NULL;
		Assert(pEntry->m_pPackedData == INVALID_PACKED_ENTITY_HANDLE);
	}

}

void CHLTVDeltaEntitiesDecoder::ReadDeltaEnt(CEntityReadInfo& u)
{
	const int ent = u.m_nNewEntity;
	CFrameSnapshot* pFromSnapshot = u.m_pFrom->GetSnapshot();

	CFrameSnapshot* pSnapshot = u.m_pTo->GetSnapshot();

	Assert(ent < pFromSnapshot->m_nNumEntities);
	*g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, ent) = *g_pPackedEntityManager->GetSnapshotEntry(pFromSnapshot, ent);

	CFrameSnapshotEntry* pSnapshotEntry = g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, ent);
	PackedEntity* pToPackedEntity = g_pPackedEntityManager->CreatePackedEntity(pSnapshot, ent, pSnapshotEntry->m_pClass, pSnapshotEntry->m_nSerialNumber);

	// WARNING! get pFromPackedEntity after new pPackedEntity has been created, otherwise pointer may be wrong
	PackedEntity* pFromPackedEntity = g_pPackedEntityManager->GetPackedEntity(pFromSnapshot, ent);

	pToPackedEntity->SetServerAndClientClass(pFromPackedEntity->m_pServerClass, pFromPackedEntity->m_pClientClass);

	// create a copy of the pFromSnapshot ChangeFrameList
	IChangeFrameList* pChangeFrame = NULL;

	if (!m_pHLTVClientState->m_bSaveMemory)
	{
		pChangeFrame = pFromPackedEntity->GetChangeFrameList()->Copy();
		pToPackedEntity->SetChangeFrameList(pChangeFrame);
	}

	// Make space for the baseline data.
	ALIGN4 char packedData[MAX_PACKEDENTITY_DATA] ALIGN4_POST;
	const void* pFromData;
	int nFromBits;

	if (pFromPackedEntity->IsCompressed())
	{
		pFromData = g_pPackedEntityManager->UncompressPackedEntity(pFromPackedEntity, nFromBits);
	}
	else
	{
		pFromData = pFromPackedEntity->GetData();
		nFromBits = pFromPackedEntity->GetNumBits();
	}

	bf_read fromBuf("HLTV_ReadEnterPVS1", pFromData, Bits2Bytes(nFromBits), nFromBits);
	bf_write writeBuf("HLTV_ReadEnterPVS2", packedData, sizeof(packedData));

	int changedProps[MAX_DATATABLE_PROPS];

	// decode baseline, is compressed against zero values 
	int nChangedProps = RecvTable_MergeDeltas(pToPackedEntity->m_pClientClass->m_pRecvTable,
		&fromBuf, u.m_pBuf, &writeBuf, -1, changedProps, false);

	// update change tick in ChangeFrameList
	if (pChangeFrame)
	{
		pChangeFrame->SetChangeTick(changedProps, nChangedProps, pSnapshot->m_nTickCount);
	}

	if (m_pHLTVClientState->m_bSaveMemory)
	{
		int bits = writeBuf.GetNumBitsWritten();

		const char* compressedData = g_pPackedEntityManager->CompressPackedEntity(
			pToPackedEntity->m_pServerClass,
			(char*)writeBuf.GetData(),
			bits);

		// store as compressed data and don't use mem pools
		pToPackedEntity->AllocAndCopyPadded(compressedData, Bits2Bytes(bits));
		pToPackedEntity->SetCompressed();
	}
	else
	{
		// store as normal
		pToPackedEntity->AllocAndCopyPadded(packedData, writeBuf.GetNumBytesWritten());
	}

	u.m_pTo->last_entity = u.m_nNewEntity;
	u.m_pTo->transmit_entity.Set(u.m_nNewEntity);

}

void CHLTVDeltaEntitiesDecoder::CopyExistingEntity(CEntityReadInfo& u)
{
	if (!u.m_bAsDelta)  // Should never happen on a full update.
	{
		Assert(0); // cl.validsequence = 0;
		ConMsg("WARNING: CopyExitingEnt on full update.\n");
		u.m_UpdateType = Failed;	// break out
		return;
	}

	CFrameSnapshot* pFromSnapshot = u.m_pFrom->GetSnapshot();	// get from snapshot

	const int ent = u.m_nOldEntity;

	CFrameSnapshot* pSnapshot = u.m_pTo->GetSnapshot(); // get to snapshot

	// copy ent handle, serial numbers & class info
	Assert(ent < pFromSnapshot->m_nNumEntities);
	*g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, ent) = *g_pPackedEntityManager->GetSnapshotEntry(pFromSnapshot, ent);

	Assert(g_pPackedEntityManager->GetPackedEntity(pSnapshot, ent) != INVALID_PACKED_ENTITY_HANDLE);

	// increase PackedEntity reference counter
	PackedEntity* pEntity = g_pPackedEntityManager->GetPackedEntity(pSnapshot, ent);
	Assert(pEntity);
	pEntity->m_ReferenceCount++;


	Assert(u.m_pTo->last_entity <= ent);

	// mark flags as received
	u.m_pTo->last_entity = ent;
	u.m_pTo->transmit_entity.Set(ent);
}

void CHLTVDeltaEntitiesDecoder::ReadPreserveEnt(CEntityReadInfo& u)
{
	// copy one of the old entities over to the new packet unchanged

	// XXX(JohnS): This was historically checking for NewEntity overflow, though this path does not care (and new entity
	//             may be -1).  The old entity bounds check here seems like what was intended, but since nNewEntity
	//             should not be overflowed either, I've left that check in case it was guarding against a case I am
	//             overlooking.
	if (u.m_nOldEntity >= MAX_EDICTS || u.m_nOldEntity < 0 || u.m_nNewEntity >= MAX_EDICTS)
	{
		Host_Error("CL_ReadPreserveEnt: Entity out of bounds. Old: %i, New: %i",
			u.m_nOldEntity, u.m_nNewEntity);
	}

	CopyExistingEntity(u);

}

void CHLTVDeltaEntitiesDecoder::ReadDeletions(CEntityReadInfo& u)
{
	while (u.m_pBuf->ReadOneBit() != 0)
	{
		int idx = u.m_pBuf->ReadUBitLong(MAX_EDICT_BITS);

		Assert(!u.m_pTo->transmit_entity.Get(idx));

		CFrameSnapshot* pSnapshot = u.m_pTo->GetSnapshot();
		CFrameSnapshotEntry* pEntry = g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, idx);

		// clear entity references
		pEntry->m_nSerialNumber = -1;
		pEntry->m_pClass = NULL;
		Assert(pEntry->m_pPackedData == INVALID_PACKED_ENTITY_HANDLE);
	}
}

bool CHLTVDeltaEntitiesDecoder::ProcessPacketEntities(SVC_PacketEntities* entmsg) {
	CClientFrame* oldFrame = NULL;

#ifdef _HLTVTEST
	if (g_RecvDecoders.Count() == 0)
		return false;
#endif

	if (entmsg->m_bIsDelta)
	{
		if (m_pHLTVClientState->GetServerTickCount() == entmsg->m_nDeltaFrom)
		{
			Host_Error("Update self-referencing, connection dropped.\n");
			return false;
		}

		// Otherwise, mark where we are valid to and point to the packet entities we'll be updating from.
		oldFrame = m_pHLTVClientState->m_pHLTV->GetClientFrame(entmsg->m_nDeltaFrom);
	}

	// create new empty snapshot
	CFrameSnapshot* pSnapshot = framesnapshotmanager->CreateEmptySnapshot(m_pHLTVClientState->GetServerTickCount(), entmsg->m_nMaxEntries);

	Assert(m_pHLTVClientState->m_pNewClientFrame == NULL);

	m_pHLTVClientState->m_pNewClientFrame = new CClientFrame(pSnapshot);

	Assert(entmsg->m_nBaseline >= 0 && entmsg->m_nBaseline < 2);

	if (entmsg->m_bUpdateBaseline)
	{
		// server requested to use this snapshot as baseline update
		int nUpdateBaseline = (entmsg->m_nBaseline == 0) ? 1 : 0;
		CopyEntityBaseline(entmsg->m_nBaseline, nUpdateBaseline);

		// send new baseline acknowledgement(as reliable)
		CLC_BaselineAck baseline(m_pHLTVClientState->GetServerTickCount(), entmsg->m_nBaseline);
		m_pHLTVClientState->m_NetChannel->SendNetMsg(baseline, true);
	}

	// copy classes and serial numbers from current frame
	if (m_pHLTVClientState->m_pCurrentClientFrame)
	{
		CFrameSnapshot* pLastSnapshot = m_pHLTVClientState->m_pCurrentClientFrame->GetSnapshot();
		CFrameSnapshotEntry* pEntry = g_pPackedEntityManager->GetSnapshotEntry(pSnapshot, 0);
		CFrameSnapshotEntry* pLastEntry = g_pPackedEntityManager->GetSnapshotEntry(pLastSnapshot, 0);

		Assert(pLastSnapshot->m_nNumEntities <= pSnapshot->m_nNumEntities);

		for (int i = 0; i < pLastSnapshot->m_nNumEntities; i++)
		{
			pEntry->m_nSerialNumber = pLastEntry->m_nSerialNumber;
			pEntry->m_pClass = pLastEntry->m_pClass;

			pEntry++;
			pLastEntry++;
		}
	}

	CEntityReadInfo u;
	u.m_pBuf = &entmsg->m_DataIn;
	u.m_pFrom = oldFrame;
	u.m_pTo = m_pHLTVClientState->m_pNewClientFrame;
	u.m_bAsDelta = entmsg->m_bIsDelta;
	u.m_nHeaderCount = entmsg->m_nUpdatedEntries;
	u.m_nBaseline = entmsg->m_nBaseline;
	u.m_bUpdateBaselines = entmsg->m_bUpdateBaseline;

	ReadPacketEntities(u);

	// adjust reference count to be 1
	pSnapshot->ReleaseReference();
}

/*
==================
CL_PreprocessEntities

Server information pertaining to this client only
==================
*/
namespace CDebugOverlay
{
	extern void PurgeServerOverlays( void );
}

void CL_PreprocessEntities( void )
{
	// Zero latency!!! (single player or listen server?)
	bool bIsUsingMultiplayerNetworking = NET_IsMultiplayer();
	bool bLastOutgoingCommandEqualsLastAcknowledgedCommand = cl.lastoutgoingcommand == cl.command_ack;

	// We always want to re-run prediction when using the multiplayer networking, or if we're the listen server and we get a packet
	//  before any frames have run
	if ( bIsUsingMultiplayerNetworking ||
		bLastOutgoingCommandEqualsLastAcknowledgedCommand )
	{
		//Msg( "%i/%i CL_ParseClientdata:  no latency server ack %i\n", 
		//	host_framecount, cl.tickcount,
		//	command_ack );
		CL_RunPrediction( PREDICTION_SIMULATION_RESULTS_ARRIVING_ON_SEND_FRAME );
	}

	// Copy some results from prediction back into right spot
	// Anything not sent over the network from server to client must be specified here.
	//if ( cl.last_command_ack  )
	{
		int number_of_commands_executed = ( cl.command_ack - cl.last_command_ack );

#if 0
		COM_Log( "cl.log", "Receiving frame acknowledging %i commands\n",
			number_of_commands_executed );

		COM_Log( "cl.log", "  last command number executed %i\n",
			cl.command_ack );

		COM_Log( "cl.log", "  previous last command number executed %i\n",
			cl.last_command_ack );

		COM_Log( "cl.log", "  current world frame %i\n",
			cl.m_nCurrentSequence );
#endif

		// Copy last set of changes right into current frame.
		g_pClientSidePrediction->PreEntityPacketReceived( number_of_commands_executed, cl.m_nCurrentSequence );
	}

	CDebugOverlay::PurgeServerOverlays();
}





