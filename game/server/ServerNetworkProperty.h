//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef SERVERNETWORKPROPERTY_H
#define SERVERNETWORKPROPERTY_H
#ifdef _WIN32
#pragma once
#endif

#include "iservernetworkable.h"
#include "server_class.h"
#include "edict.h"
#include "timedeventmgr.h"

//
// Lightweight base class for networkable data on the server.
//
class CServerNetworkProperty : public IServerNetworkable, public IEventRegisterCallback
{
public:
	DECLARE_CLASS_NOBASE(CServerNetworkProperty);
	DECLARE_DATADESC();

public:
	CServerNetworkProperty();
	virtual	~CServerNetworkProperty();

public:
	// IServerNetworkable implementation.
	virtual IHandleEntity* GetEntityHandle();
	//virtual edict_t			*GetEdict() const;
	virtual CBaseNetworkable* GetBaseNetworkable();
	virtual CBaseEntity*	GetBaseEntity();
	virtual ServerClass*	GetServerClass();
	virtual const char*		GetClassName() const;
	//virtual void			Release();
	virtual int				AreaNum() const;
	virtual PVSInfo_t*		GetPVSInfo();
	virtual int&			GetTransmitState();
	virtual void			ClearTransmitState();
public:
	// Other public methods
	void Init(CBaseEntity* pEntity);
	//void SetEntIndex(int entindex);
	//void AttachEdict(int pRequiredEdict = -1);

	// Methods to get the entindex + edict
	int	entindex() const;
	//edict_t *edict();
	//const edict_t *edict() const;
	bool HasStateChanged() const;
	void ClearStateChanged();
	void StateChanged();
	void StateChanged(unsigned short offset);
	const unsigned short* GetStateChangedOffsets() const;
	const unsigned short	GetNumStateChangedOffsets() const;
	//int						GetStateChangedTickCount() const;
	//void					SetStateChangedTickCount(int nTickCount);
	// Sets the edict pointer (for swapping edicts)
	//void SetEdict( edict_t *pEdict );

	// All these functions call through to CNetStateMgr. 
	// See CNetStateMgr for details about these functions.
	void NetworkStateForceUpdate();
	void NetworkStateChanged();
	void NetworkStateChanged(unsigned short offset);

	// Marks the PVS information dirty
	void MarkPVSInformationDirty();

	// Marks for deletion
	void MarkForDeletion();
	bool IsMarkedForDeletion() const;

	// Sets the network parent
	void SetNetworkParent(EHANDLE hParent);
	CServerNetworkProperty* GetNetworkParent();

	// This is useful for entities that don't change frequently or that the client
	// doesn't need updates on very often. If you use this mode, the server will only try to
	// detect state changes every N seconds, so it will save CPU cycles and bandwidth.
	//
	// Note: N must be less than AUTOUPDATE_MAX_TIME_LENGTH.
	//
	// Set back to zero to disable the feature.
	//
	// This feature works on top of manual mode. 
	// - If you turn it on and manual mode is off, it will autodetect changes every N seconds.
	// - If you turn it on and manual mode is on, then every N seconds it will only say there
	//   is a change if you've called NetworkStateChanged.
	void			SetUpdateInterval(float N);

	// You can use this to override any entity's ShouldTransmit behavior.
	// void SetTransmitProxy( CBaseTransmitProxy *pProxy );

	// This version does a PVS check which also checks for connected areas
	bool IsInPVS(const CCheckTransmitInfo* pInfo);

	// This version doesn't do the area check
	bool IsInPVS(const CBaseEntity* pRecipient, const void* pvs, int pvssize);

	// Called by the timed event manager when it's time to detect a state change.
	virtual void FireEvent();

	// Recomputes PVS information
	void RecomputePVSInformation();

private:
	// Detaches the edict.. should only be called by CBaseNetworkable's destructor.
	//void DetachEdict();
	CBaseEntity* GetOuter();

	// Marks the networkable that it will should transmit
	//void SetTransmit(CCheckTransmitInfo* pInfo);

private:
	CBaseEntity* m_pOuter;
	// CBaseTransmitProxy *m_pTransmitProxy;
	//int		m_entindex = -1;
	//edict_t	*m_pPev;
#ifdef _XBOX
	unsigned short m_fStateFlags = 0;
#else
	int	m_fStateFlags = 0;
#endif	
	// Edicts remember the offsets of properties that change 
	unsigned short m_ChangeOffsets[MAX_CHANGE_OFFSETS];
	unsigned short m_nChangeOffsets;
	//int	m_nChangeOffsetsTickCount = 0;
	PVSInfo_t m_PVSInfo;
	ServerClass* m_pServerClass;

	// NOTE: This state is 'owned' by the entity. It's only copied here
	// also to help improve cache performance in networking code.
	EHANDLE m_hParent;

	// Counters for SetUpdateInterval.
	CEventRegister	m_TimerEvent;
	bool m_bPendingStateChange : 1;

	//	friend class CBaseTransmitProxy;
};


//-----------------------------------------------------------------------------
// inline methods // TODOMO does inline work on virtual functions ?
//-----------------------------------------------------------------------------
inline CBaseNetworkable* CServerNetworkProperty::GetBaseNetworkable()
{
	return NULL;
}

inline CBaseEntity* CServerNetworkProperty::GetBaseEntity()
{
	return m_pOuter;
}

inline CBaseEntity* CServerNetworkProperty::GetOuter()
{
	return m_pOuter;
}

inline PVSInfo_t* CServerNetworkProperty::GetPVSInfo()
{
	return &m_PVSInfo;
}

inline int& CServerNetworkProperty::GetTransmitState() {
	return m_fStateFlags;
}

inline void CServerNetworkProperty::ClearTransmitState() {
	m_fStateFlags &= ~(FL_EDICT_ALWAYS | FL_EDICT_PVSCHECK | FL_EDICT_DONTSEND);
}

//-----------------------------------------------------------------------------
// Marks the PVS information dirty
//-----------------------------------------------------------------------------
inline void CServerNetworkProperty::MarkPVSInformationDirty()
{
	//if (m_entindex != -1)
	//{
		GetTransmitState() |= FL_EDICT_DIRTY_PVS_INFORMATION;
	//}
}


//-----------------------------------------------------------------------------
// Sets/gets the network parent
//-----------------------------------------------------------------------------
inline void CServerNetworkProperty::SetNetworkParent(EHANDLE hParent)
{
	m_hParent = hParent;
}


//-----------------------------------------------------------------------------
// Methods related to the net state mgr
//-----------------------------------------------------------------------------
inline void CServerNetworkProperty::NetworkStateForceUpdate()
{
	//if (m_entindex != -1)
		StateChanged();
}

inline void CServerNetworkProperty::NetworkStateChanged()
{
	// If we're using the timer, then ignore this call.
	if (m_TimerEvent.IsRegistered())
	{
		// If we're waiting for a timer event, then queue the change so it happens
		// when the timer goes off.
		m_bPendingStateChange = true;
	}
	else
	{
		//if (m_entindex != -1)
			StateChanged();
	}
}

inline void CServerNetworkProperty::NetworkStateChanged(unsigned short varOffset)
{
	// If we're using the timer, then ignore this call.
	if (m_TimerEvent.IsRegistered())
	{
		// If we're waiting for a timer event, then queue the change so it happens
		// when the timer goes off.
		m_bPendingStateChange = true;
	}
	else
	{
		//if (m_entindex != -1)
			StateChanged(varOffset);
	}
}


//inline edict_t* CServerNetworkProperty::GetEdict() const
//{
//	// This one's virtual, that's why we have to two other versions
//	return m_pPev;
//}

//inline edict_t *CServerNetworkProperty::edict()
//{
//	if (m_entindex == -1) {
//		return NULL;
//	}
//	return INDEXENT(m_entindex);
//}
//
//inline const edict_t *CServerNetworkProperty::edict() const
//{
//	if (m_entindex == -1) {
//		return NULL;
//	}
//	return INDEXENT(m_entindex);
//}

inline bool	CServerNetworkProperty::HasStateChanged() const
{
	return (m_fStateFlags & FL_EDICT_CHANGED) != 0;
}

inline void	CServerNetworkProperty::ClearStateChanged()
{
	m_fStateFlags &= ~(FL_EDICT_CHANGED | FL_FULL_EDICT_CHANGED);
	m_nChangeOffsets = 0;
	//m_nChangeOffsetsTickCount = 0;
}

inline void	CServerNetworkProperty::StateChanged()
{
	// Note: this should only happen for properties in data tables that used some
	// kind of pointer dereference. If the data is directly offsetable 
	m_fStateFlags |= (FL_EDICT_CHANGED | FL_FULL_EDICT_CHANGED);
	m_nChangeOffsets = 0;
	//m_nChangeOffsetsTickCount = 0;
}

inline void	CServerNetworkProperty::StateChanged(unsigned short offset)
{
	if (m_fStateFlags & FL_FULL_EDICT_CHANGED)
		return;

	m_fStateFlags |= FL_EDICT_CHANGED;

//	if (m_nChangeOffsetsTickCount != engine->GetStateChangedTickCount()) {
//		m_nChangeOffsetsTickCount = engine->GetStateChangedTickCount();
//		m_nChangeOffsets = 0;
//	}
	// Now add this offset to our list of changed variables.		
	for (unsigned short i = 0; i < m_nChangeOffsets; i++)
		if (m_ChangeOffsets[i] == offset)
			return;

	if (m_nChangeOffsets == MAX_CHANGE_OFFSETS)
	{
		// Invalidate our change info.
		m_fStateFlags |= FL_FULL_EDICT_CHANGED; // So we don't get in here again.
		m_nChangeOffsets = 0;
		//m_nChangeOffsetsTickCount = 0;
	}
	else
	{
		m_ChangeOffsets[m_nChangeOffsets++] = offset;
	}
}

inline const unsigned short* CServerNetworkProperty::GetStateChangedOffsets() const
{
	return m_ChangeOffsets;
}

inline const unsigned short	CServerNetworkProperty::GetNumStateChangedOffsets() const
{
	//if (m_nChangeOffsetsTickCount != gpGlobals->tickcount) {
	//	return 0;
	//}
	return m_nChangeOffsets;
}

//inline int CServerNetworkProperty::GetStateChangedTickCount() const {
//	return m_nChangeOffsetsTickCount;
//}

//inline void	CServerNetworkProperty::SetStateChangedTickCount(int nTickCount) {
//	m_nChangeOffsetsTickCount = nTickCount;
//}

//-----------------------------------------------------------------------------
// Sets the edict pointer (for swapping edicts)
//-----------------------------------------------------------------------------
//inline void CServerNetworkProperty::SetEdict( edict_t *pEdict )
//{
//	m_entindex = pEdict->m_EdictIndex;
//}


inline int CServerNetworkProperty::AreaNum() const
{
	const_cast<CServerNetworkProperty*>(this)->RecomputePVSInformation();
	return m_PVSInfo.m_nAreaNum;
}


#endif // SERVERNETWORKPROPERTY_H
