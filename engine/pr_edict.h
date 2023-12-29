//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Expose things from pr_edict.cpp.
//
// $NoKeywords: $
//===========================================================================//

#ifndef PR_EDICT_H
#define PR_EDICT_H


#include "edict.h"
struct edict_t;

//class CEdictChangeInfo
//{
//public:
//	// Edicts remember the offsets of properties that change 
//	unsigned short m_ChangeOffsets[MAX_CHANGE_OFFSETS];
//	unsigned short m_nChangeOffsets;
//};
//
//// Shared between engine and game DLL.
//class CSharedEdictChangeInfo
//{
//public:
//	CSharedEdictChangeInfo()
//	{
//		m_iSerialNumber = 1;
//	}
//
//	// Matched against edict_t::m_iChangeInfoSerialNumber to determine if its
//	// change info is valid.
//	unsigned short m_iSerialNumber;
//
//	CEdictChangeInfo m_ChangeInfos[MAX_EDICT_CHANGE_INFOS];
//	unsigned short m_nChangeInfos;	// How many are in use this frame.
//};
//extern CSharedEdictChangeInfo* g_pSharedChangeInfo;
//
//class IChangeInfoAccessor
//{
//public:
//	inline void SetChangeInfo(unsigned short info)
//	{
//		m_iChangeInfo = info;
//	}
//
//	inline void SetChangeInfoSerialNumber(unsigned short sn)
//	{
//		m_iChangeInfoSerialNumber = sn;
//	}
//
//	inline unsigned short	 GetChangeInfo() const
//	{
//		return m_iChangeInfo;
//	}
//
//	inline unsigned short	 GetChangeInfoSerialNumber() const
//	{
//		return m_iChangeInfoSerialNumber;
//	}
//
//private:
//	unsigned short m_iChangeInfo;
//	unsigned short m_iChangeInfoSerialNumber;
//};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
// NOTE: YOU CAN'T CHANGE THE LAYOUT OR SIZE OF CBASEEDICT AND REMAIN COMPATIBLE WITH HL2_VC6!!!!!
class CBaseEdict
{
public:

	// Returns an IServerEntity if FL_FULLEDICT is set or NULL if this 
	// is a lightweight networking entity.
	//IServerEntity* GetIServerEntity();
	//const IServerEntity* GetIServerEntity() const;

	//IServerNetworkable* GetNetworkable();
	//IServerUnknown* GetUnknown();

	// Set when initting an entity. If it's only a networkable, this is false.
	void				SetEdict(bool bFullEdict);

	//int					AreaNum() const;
	//const char*			GetClassName() const;

	bool				IsFree() const;
	void				SetFree();
	void				ClearFree();

	//bool				HasStateChanged() const;
	//void				ClearStateChanged();
	//void				StateChanged();
	//void				StateChanged(unsigned short offset);

	//void				ClearTransmitState();

	//void SetChangeInfo(unsigned short info);
	//void SetChangeInfoSerialNumber(unsigned short sn);
	//unsigned short	 GetChangeInfo() const;
	//unsigned short	 GetChangeInfoSerialNumber() const;

public:

	// NOTE: this is in the edict instead of being accessed by a virtual because the engine needs fast access to it.
	// NOTE: YOU CAN'T CHANGE THE LAYOUT OR SIZE OF CBASEEDICT AND REMAIN COMPATIBLE WITH HL2_VC6!!!!!
//#ifdef _XBOX
//	unsigned short m_fStateFlags;
//#else
//	int	m_fStateFlags;
//#endif	
	//bool m_bIsReady = false;
	bool m_bIsFree = true;

	// NOTE: this is in the edict instead of being accessed by a virtual because the engine needs fast access to it.
	// int m_NetworkSerialNumber;

	// NOTE: m_EdictIndex is an optimization since computing the edict index
	// from a CBaseEdict* pointer otherwise requires divide-by-20. values for
	// m_NetworkSerialNumber all fit within a 16-bit integer range, so we're
	// repurposing the other 16 bits to cache off the index without changing
	// the overall layout or size of this struct. existing mods compiled with
	// a full 32-bit serial number field should still work. henryg 8/17/2011
#if VALVE_LITTLE_ENDIAN
	short m_NetworkSerialNumber;
	short m_EdictIndex;
#else
	short m_EdictIndex;
	short m_NetworkSerialNumber;
#endif

	// NOTE: this is in the edict instead of being accessed by a virtual because the engine needs fast access to it.
	//IServerNetworkable	*m_pNetworkable;

protected:
	//IServerUnknown		*m_pUnk;		


public:

//	IChangeInfoAccessor* GetChangeAccessor(); // The engine implements this and the game .dll implements as
//	const IChangeInfoAccessor* GetChangeAccessor() const; // The engine implements this and the game .dll implements as
	// as callback through to the engine!!!

	// NOTE: YOU CAN'T CHANGE THE LAYOUT OR SIZE OF CBASEEDICT AND REMAIN COMPATIBLE WITH HL2_VC6!!!!!
	// This breaks HL2_VC6!!!!!
	// References a CEdictChangeInfo with a list of modified network props.
	//unsigned short m_iChangeInfo;
	//unsigned short m_iChangeInfoSerialNumber;

	friend void InitializeEntityDLLFields(edict_t* pEdict);
};


//-----------------------------------------------------------------------------
// CBaseEdict inlines.
//-----------------------------------------------------------------------------
//inline IServerEntity* CBaseEdict::GetIServerEntity()
//{
//	if (m_bIsReady)
//		return serverEntitylist->GetServerEntity(m_EdictIndex);
//	else
//		return 0;
//}

inline bool CBaseEdict::IsFree() const
{
	return m_bIsFree;
}



//inline bool	CBaseEdict::HasStateChanged() const
//{
//	return (m_fStateFlags & FL_EDICT_CHANGED) != 0;
//}
//
//inline void	CBaseEdict::ClearStateChanged()
//{
//	m_fStateFlags &= ~(FL_EDICT_CHANGED | FL_FULL_EDICT_CHANGED);
//	SetChangeInfoSerialNumber(0);
//}
//
//inline void	CBaseEdict::StateChanged()
//{
//	// Note: this should only happen for properties in data tables that used some
//	// kind of pointer dereference. If the data is directly offsetable 
//	m_fStateFlags |= (FL_EDICT_CHANGED | FL_FULL_EDICT_CHANGED);
//	SetChangeInfoSerialNumber(0);
//}
//
//inline void	CBaseEdict::StateChanged(unsigned short offset)
//{
//	if (m_fStateFlags & FL_FULL_EDICT_CHANGED)
//		return;
//
//	m_fStateFlags |= FL_EDICT_CHANGED;
//
//	IChangeInfoAccessor* accessor = GetChangeAccessor();
//
//	if (accessor->GetChangeInfoSerialNumber() == g_pSharedChangeInfo->m_iSerialNumber)
//	{
//		// Ok, I still own this one.
//		CEdictChangeInfo* p = &g_pSharedChangeInfo->m_ChangeInfos[accessor->GetChangeInfo()];
//
//		// Now add this offset to our list of changed variables.		
//		for (unsigned short i = 0; i < p->m_nChangeOffsets; i++)
//			if (p->m_ChangeOffsets[i] == offset)
//				return;
//
//		if (p->m_nChangeOffsets == MAX_CHANGE_OFFSETS)
//		{
//			// Invalidate our change info.
//			accessor->SetChangeInfoSerialNumber(0);
//			m_fStateFlags |= FL_FULL_EDICT_CHANGED; // So we don't get in here again.
//		}
//		else
//		{
//			p->m_ChangeOffsets[p->m_nChangeOffsets++] = offset;
//		}
//	}
//	else
//	{
//		if (g_pSharedChangeInfo->m_nChangeInfos == MAX_EDICT_CHANGE_INFOS)
//		{
//			// Shucks.. have to mark the edict as fully changed because we don't have room to remember this change.
//			accessor->SetChangeInfoSerialNumber(0);
//			m_fStateFlags |= FL_FULL_EDICT_CHANGED;
//		}
//		else
//		{
//			// Get a new CEdictChangeInfo and fill it out.
//			accessor->SetChangeInfo(g_pSharedChangeInfo->m_nChangeInfos);
//			g_pSharedChangeInfo->m_nChangeInfos++;
//
//			accessor->SetChangeInfoSerialNumber(g_pSharedChangeInfo->m_iSerialNumber);
//
//			CEdictChangeInfo* p = &g_pSharedChangeInfo->m_ChangeInfos[accessor->GetChangeInfo()];
//			p->m_ChangeOffsets[0] = offset;
//			p->m_nChangeOffsets = 1;
//		}
//	}
//}



inline void CBaseEdict::SetFree()
{
	m_bIsFree = true;
}

// WARNING: Make sure you don't really want to call ED_ClearFreeFlag which will also
//  remove this edict from the g_FreeEdicts bitset.
inline void CBaseEdict::ClearFree()
{
	m_bIsFree = false;
}

//inline void	CBaseEdict::ClearTransmitState()
//{
//	m_fStateFlags &= ~(FL_EDICT_ALWAYS | FL_EDICT_PVSCHECK | FL_EDICT_DONTSEND);
//}

//inline const IServerEntity* CBaseEdict::GetIServerEntity() const
//{
//	if (m_bIsReady)
//		return serverEntitylist->GetServerEntity(m_EdictIndex);
//	else
//		return 0;
//}

//inline IServerUnknown* CBaseEdict::GetUnknown()
//{
//	return serverEntitylist->GetServerEntity(m_EdictIndex);
//}

//inline IServerNetworkable* CBaseEdict::GetNetworkable()
//{
//	return serverEntitylist->GetServerEntity(m_EdictIndex)->GetNetworkable();
//}

inline void CBaseEdict::SetEdict(bool bFullEdict)
{
	//m_pUnk = pUnk;
	//if (bFullEdict)
	//{
	//	m_bIsReady = true;
	//}
	//else
	//{
	//	m_bIsReady = false;
	//}
	m_bIsFree = false;
}

//inline int CBaseEdict::AreaNum() const
//{
//	IServerEntity* serverEntity = serverEntitylist->GetServerEntity(m_EdictIndex);
//	if (!serverEntity) {
//		return 0;
//	}
//
//	return serverEntity->GetNetworkable()->AreaNum();
//}

//inline const char* CBaseEdict::GetClassName() const
//{
//	IServerEntity* serverEntity = serverEntitylist->GetServerEntity(m_EdictIndex);
//	if (!serverEntity) {
//		return "";
//	}
//
//	return serverEntity->GetNetworkable()->GetClassName();
//}

//inline void CBaseEdict::SetChangeInfo(unsigned short info)
//{
//	GetChangeAccessor()->SetChangeInfo(info);
//}
//
//inline void CBaseEdict::SetChangeInfoSerialNumber(unsigned short sn)
//{
//	GetChangeAccessor()->SetChangeInfoSerialNumber(sn);
//}
//
//inline unsigned short CBaseEdict::GetChangeInfo() const
//{
//	return GetChangeAccessor()->GetChangeInfo();
//}
//
//inline unsigned short CBaseEdict::GetChangeInfoSerialNumber() const
//{
//	return GetChangeAccessor()->GetChangeInfoSerialNumber();
//}

//-----------------------------------------------------------------------------
// Purpose: The engine's internal representation of an entity, including some
//  basic collision and position info and a pointer to the class wrapped on top
//  of the structure
//-----------------------------------------------------------------------------
struct edict_t : public CBaseEdict
{
public:
	//ICollideable* GetCollideable();

	// The server timestampe at which the edict was freed (so we can try to use other edicts before reallocating this one)
	float		freetime;
};

//inline ICollideable* edict_t::GetCollideable()
//{
//	IServerEntity* pEnt = GetIServerEntity();
//	if (pEnt)
//		return pEnt->GetCollideable();
//	else
//		return NULL;
//}

void InitializeEntityDLLFields( edict_t *pEdict );

// If iForceEdictIndex is not -1, then it will return the edict with that index. If that edict index
// is already used, it'll return null.
edict_t *ED_Alloc( int iForceEdictIndex = -1 );
void	ED_Free( edict_t *ed );

// Clear the FL_EDICT_FREE flag and the g_FreeEdicts bit.
void	ED_ClearFreeFlag( edict_t *pEdict );

edict_t *EDICT_NUM(int n);
int NUM_FOR_EDICT(const edict_t *e);

void ED_AllowImmediateReuse();
void ED_ClearFreeEdictList();

#endif


