//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

#include "env_detail_controller.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CEnvDetailController C_EnvDetailController
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Implementation of the class that controls detail prop fade distances
//-----------------------------------------------------------------------------
class CEnvDetailController : public CBaseEntity
{
public:
	DECLARE_CLASS(CEnvDetailController, CBaseEntity);
	DECLARE_NETWORKCLASS();

	CEnvDetailController();
	virtual ~CEnvDetailController();

#ifndef CLIENT_DLL
	virtual bool KeyValue(const char* szKeyName, const char* szValue);
#else
	virtual void PostDataUpdate(DataUpdateType_t updateType);
#endif // !CLIENT_DLL

	CNetworkVar(float, m_flFadeStartDist);
	CNetworkVar(float, m_flFadeEndDist);

	// ALWAYS transmit to all clients.
	virtual int UpdateTransmitState(void);
};

LINK_ENTITY_TO_CLASS(env_detail_controller,	CEnvDetailController);

IMPLEMENT_NETWORKCLASS_ALIASED( EnvDetailController, DT_DetailController )

BEGIN_NETWORK_TABLE_NOBASE( CEnvDetailController, DT_DetailController )
	#ifdef CLIENT_DLL
		RecvPropFloat( RECVINFO( m_flFadeStartDist ) ),
		RecvPropFloat( RECVINFO( m_flFadeEndDist ) ),
	#else
		SendPropFloat( SENDINFO( m_flFadeStartDist ) ),
		SendPropFloat( SENDINFO( m_flFadeEndDist ) ),
	#endif
END_NETWORK_TABLE()

static CEnvDetailController *s_detailController = NULL;
CEnvDetailController * GetDetailController()
{
	return s_detailController;
}

CEnvDetailController::CEnvDetailController()
{
	s_detailController = this;
}

CEnvDetailController::~CEnvDetailController()
{
	if ( s_detailController == this )
	{
		s_detailController = NULL;
	}
}

//--------------------------------------------------------------------------------------------------------------
int CEnvDetailController::UpdateTransmitState()
{
#ifndef CLIENT_DLL
	// ALWAYS transmit to all clients.
	return SetTransmitState( FL_EDICT_ALWAYS );
#else
	return 0;
#endif
}

#ifndef CLIENT_DLL

	bool CEnvDetailController::KeyValue( const char *szKeyName, const char *szValue )
	{
		if (FStrEq(szKeyName, "fademindist"))
		{
			m_flFadeStartDist = atof(szValue);
		}
		else if (FStrEq(szKeyName, "fademaxdist"))
		{
			m_flFadeEndDist = atof(szValue);
		}

		return true;
	}

#else
void CEnvDetailController::PostDataUpdate(DataUpdateType_t updateType) 
{
	BaseClass::PostDataUpdate(updateType);
	ConVarRef cl_detailfade("cl_detailfade");
	ConVarRef cl_detaildist("cl_detaildist");
	cl_detailfade.SetValue(MIN(cl_detailfade.GetFloat(), GetDetailController()->m_flFadeStartDist));
	cl_detaildist.SetValue(MIN(cl_detaildist.GetFloat(), GetDetailController()->m_flFadeEndDist));
}

#endif // !CLIENT_DLL
