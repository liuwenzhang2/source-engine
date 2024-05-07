//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef CL_ENTS_PARSE_H
#define CL_ENTS_PARSE_H
#ifdef _WIN32
#pragma once
#endif

#include <bitbuf.h>
#include "ents_shared.h"

//class CEntityReadInfo;
class CHLTVClientState;

class PackedEntityDecoder {
public:
	PackedEntityDecoder() {
		Q_memset(m_pEntityBaselines, 0, sizeof(m_pEntityBaselines));
	}
	~PackedEntityDecoder()
	{

	}

	void ReadEnterPvsFromBuffer(CEntityReadInfo& u, IClientNetworkable* pClientNetworkable);

	void ReadDeltaEntFromBuffer(CEntityReadInfo& u, IClientNetworkable* pClientNetworkable);

	void FreeEntityBaselines();

	void CopyEntityBaseline(int iFrom, int iTo);

private:
	PackedEntity* GetEntityBaseline(int iBaseline, int nEntityIndex);
	void SetEntityBaseline(int iBaseline, ClientClass* pClientClass, int index, char* packedData, int length);

	PackedEntity* m_pEntityBaselines[2][MAX_EDICTS];	// storing entity baselines
};

class DeltaEntitiesDecoder : public CClientFrameManager {
public:
	DeltaEntitiesDecoder() {
		
	}

	~DeltaEntitiesDecoder()
	{
		DeleteClientFrames(-1); // clear all
	}

	virtual bool ProcessPacketEntities(SVC_PacketEntities* entmsg);

	virtual void CopyNewEntity(CEntityReadInfo& u, int iClass, int iSerialNum);

	virtual void CopyExistingEntity(CEntityReadInfo& u);

	void PreserveExistingEntity(int nOldEntity);

	void DeleteDLLEntity(int iEnt, const char* reason, bool bOnRecreatingAllEntities = false);

	void ReadPacketEntities(CEntityReadInfo& u);
	virtual void ReadEnterPVS(CEntityReadInfo& u);
	virtual void ReadLeavePVS(CEntityReadInfo& u);
	virtual void ReadDeltaEnt(CEntityReadInfo& u);
	virtual void ReadPreserveEnt(CEntityReadInfo& u);
	virtual void ReadDeletions(CEntityReadInfo& u);
	void FreeEntityBaselines();

private:
	void ParseDeltaHeader(CEntityReadInfo& u);
	bool DetermineUpdateType(CEntityReadInfo& u);
	IClientEntity* CreateDLLEntity(int iEnt, int iClass, int iSerialNum);
	void MarkEntitiesOutOfPVS(CBitVec<MAX_EDICTS>* pvs_flags);
	void AddPostDataUpdateCall(CEntityReadInfo& u, int iEnt, DataUpdateType_t updateType);
	void CallPostDataUpdates(CEntityReadInfo& u);

	PackedEntityDecoder m_EnginePackedEntityDecoder;
	PackedEntityDecoder m_PackedEntityDecoder;
};

class CHLTVDeltaEntitiesDecoder : public DeltaEntitiesDecoder {
public:
	CHLTVDeltaEntitiesDecoder() {
	
	}
	~CHLTVDeltaEntitiesDecoder()
	{

	}

	void Init(CHLTVClientState* pHLTVClientState) {
		m_pHLTVClientState = pHLTVClientState;
	}

	virtual bool ProcessPacketEntities(SVC_PacketEntities* entmsg);

	void ReadEnterPVS(CEntityReadInfo& u);
	void ReadLeavePVS(CEntityReadInfo& u);
	void ReadDeltaEnt(CEntityReadInfo& u);
	void ReadPreserveEnt(CEntityReadInfo& u);
	void ReadDeletions(CEntityReadInfo& u);

	virtual void CopyNewEntity(CEntityReadInfo& u, int iClass, int iSerialNum);

	virtual void CopyExistingEntity(CEntityReadInfo& u);
	void FreeEntityBaselines();
private:
	PackedEntity* GetEntityBaseline(int iBaseline, int nEntityIndex);
	void SetEntityBaseline(int iBaseline, ClientClass* pClientClass, int index, char* packedData, int length);
	void CopyEntityBaseline(int iFrom, int iTo);

	PackedEntity* m_pEntityBaselines[2][MAX_EDICTS];	// storing entity baselines
	CHLTVClientState* m_pHLTVClientState;
};

//void CL_DeleteDLLEntity( int iEnt, const char *reason, bool bOnRecreatingAllEntities = false );
//void CL_CopyExistingEntity( CEntityReadInfo &u );
//void CL_PreserveExistingEntity( int nOldEntity );
void CL_PreprocessEntities( void );
//bool CL_ProcessPacketEntities ( SVC_PacketEntities * entmsg );


#endif // CL_ENTS_PARSE_H
