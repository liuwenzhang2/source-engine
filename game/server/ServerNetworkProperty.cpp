//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "ServerNetworkProperty.h"
#include "tier0/dbg.h"
#include "gameinterface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern CTimedEventMgr g_NetworkPropertyEventMgr;


//-----------------------------------------------------------------------------
// Save/load
//-----------------------------------------------------------------------------
BEGIN_DATADESC_NO_BASE( CServerNetworkProperty )
//	DEFINE_FIELD( m_pOuter, FIELD_CLASSPTR ),
//	DEFINE_FIELD( m_pPev, FIELD_CLASSPTR ),
//	DEFINE_FIELD( m_PVSInfo, PVSInfo_t ),
//	DEFINE_FIELD( m_pServerClass, FIELD_CLASSPTR ),
//	DEFINE_GLOBAL_FIELD( m_hParent, FIELD_EHANDLE ),
//	DEFINE_FIELD( m_TimerEvent, CEventRegister ),
//	DEFINE_FIELD( m_bPendingStateChange, FIELD_BOOLEAN ),
END_DATADESC()


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
CServerNetworkProperty::CServerNetworkProperty()
{
	Init( NULL );
}


CServerNetworkProperty::~CServerNetworkProperty()
{
	/* Free our transmit proxy.
	if ( m_pTransmitProxy )
	{
		m_pTransmitProxy->Release();
	}*/

	//engine->CleanUpEntityClusterList( &m_PVSInfo );

	// remove the attached edict if it exists
	//DetachEdict();
}


//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------
void CServerNetworkProperty::Init( CBaseEntity *pEntity )
{
	//m_pPev = NULL;
	m_pOuter = pEntity;
	m_pServerClass = NULL;
//	m_pTransmitProxy = NULL;
	m_bPendingStateChange = false;
	m_TimerEvent.Init( &g_NetworkPropertyEventMgr, this );
	ClearStateChanged();
}

//void CServerNetworkProperty::SetEntIndex(int entindex) {
//	m_entindex = entindex;
//}
//-----------------------------------------------------------------------------
// Connects, disconnects edicts
//-----------------------------------------------------------------------------
//void CServerNetworkProperty::AttachEdict( int pRequiredEdict )
//{
//	Assert (pRequiredEdict == -1);
//
//	// see if there is an edict allocated for it, otherwise get one from the engine
//	if ( pRequiredEdict==-1 )
//	{
//		//pRequiredEdict = engine->CreateEdict();
//	}
//
//	m_entindex = pRequiredEdict;
//	//engine->SetEdict(m_entindex, true );
//}

//void CServerNetworkProperty::DetachEdict()
//{
//	if ( m_entindex!=-1 )
//	{
//		//engine->SetEdict(m_entindex, false );
//		//engine->RemoveEdict(m_entindex);
//		m_entindex = -1;
//	}
//}


//-----------------------------------------------------------------------------
// Methods to get the entindex + edict
//-----------------------------------------------------------------------------
inline int CServerNetworkProperty::entindex() const
{
//	if (m_pOuter->IsEFlagSet(EFL_SERVER_ONLY)) {
//		Error("can not call entindex on server only entity!");
//	}
	CBaseHandle Handle = m_pOuter->GetRefEHandle();
	if (Handle == INVALID_ENTITY_HANDLE) {
		return -1;
	}
	else {
		return Handle.GetEntryIndex();
	}
}

//-----------------------------------------------------------------------------
// Entity handles
//-----------------------------------------------------------------------------
IHandleEntity *CServerNetworkProperty::GetEntityHandle( )
{
	return m_pOuter;
}

//void CServerNetworkProperty::Release()
//{
//	delete m_pOuter;
//}


//-----------------------------------------------------------------------------
// Returns the network parent
//-----------------------------------------------------------------------------
//CServerNetworkProperty* CServerNetworkProperty::GetNetworkParent()
//{
//	CBaseEntity *pParent = m_hParent.Get();
//	return pParent ? pParent->NetworkProp() : NULL;
//}








//-----------------------------------------------------------------------------
// Serverclass
//-----------------------------------------------------------------------------
ServerClass* CServerNetworkProperty::GetServerClass()
{
	if ( !m_pServerClass )
		m_pServerClass = m_pOuter->GetServerClass();
	return m_pServerClass;
}

const char* CServerNetworkProperty::GetClassName() const
{
	return STRING(m_pOuter->m_iClassname);
}


//-----------------------------------------------------------------------------
// Transmit proxies
/*-----------------------------------------------------------------------------
void CServerNetworkProperty::SetTransmitProxy( CBaseTransmitProxy *pProxy )
{
	if ( m_pTransmitProxy )
	{
		m_pTransmitProxy->Release();
	}

	m_pTransmitProxy = pProxy;
	
	if ( m_pTransmitProxy )
	{
		m_pTransmitProxy->AddRef();
	}
}*/




void CServerNetworkProperty::SetUpdateInterval( float val )
{
	if ( val == 0 )
		m_TimerEvent.StopUpdates();
	else
		m_TimerEvent.SetUpdateInterval( val );
}


void CServerNetworkProperty::FireEvent()
{
	// Our timer went off. If our state has changed in the background, then 
	// trigger a state change in the edict.
	if ( m_bPendingStateChange )
	{
		//if (m_entindex != -1)
			StateChanged();
		m_bPendingStateChange = false;
	}
}



