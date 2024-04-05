//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
#if !defined( PACKED_ENTITY_H )
#define PACKED_ENTITY_H
#ifdef _WIN32
#pragma once
#endif

#include <const.h>
#include <basetypes.h>
#include <mempool.h>
#include <utlvector.h>
#include <tier0/dbg.h>
#include "utlmap.h"
#include "common.h"
#include "bitvec.h"
#include <iservernetworkable.h>

// Matched with the memdbgoff at end of header
#include "memdbgon.h"

// This is extra spew to the files cltrace.txt + svtrace.txt
// #define DEBUG_NETWORKING 1

#if defined( DEBUG_NETWORKING )
#include "convar.h"
void SpewToFile( PRINTF_FORMAT_STRING char const* pFmt, ... );
extern ConVar  sv_packettrace;
#define TRACE_PACKET( text ) if ( sv_packettrace.GetInt() ) { SpewToFile text ; };
#else
#define TRACE_PACKET( text )
#endif

enum
{
	ENTITY_SENTINEL = 9999	// larger number than any real entity number
};

#define	FLAG_IS_COMPRESSED	(1<<31)


class CSendProxyRecipients;
class SendTable;
class RecvTable;
class ServerClass;
class ClientClass;
class IChangeFrameList;
//class CFrameSnapshotEntry;
class CFrameSnapshot;
class CBaseClient;
class CEntityWriteInfo;
// Replaces entity_state_t.
// This is what we send to clients.

class PackedEntity
{
public:

				PackedEntity();
				~PackedEntity();
	
	void		SetNumBits( int nBits );
	int			GetNumBits() const;
	int			GetNumBytes() const;
	void		SetCompressed();
	bool		IsCompressed() const;

	// Access the data in the entity.
	void*		GetData();
	void		FreeData();

	// Copy the data into the PackedEntity's data and make sure the # bytes allocated is
	// an integer multiple of 4.
	bool		AllocAndCopyPadded( const void *pData, unsigned long size );

	// These are like Get/Set, except SnagChangeFrameList clears out the
	// PackedEntity's pointer since the usage model in sv_main is to keep
	// the same CChangeFrameList in the most recent PackedEntity for the
	// lifetime of an edict.
	//
	// When the PackedEntity is deleted, it deletes its current CChangeFrameList if it exists.
	void				SetChangeFrameList( IChangeFrameList *pList );
	IChangeFrameList*	GetChangeFrameList();
	IChangeFrameList*	SnagChangeFrameList();

	// If this PackedEntity has a ChangeFrameList, then this calls through. If not, it returns all props
	int					GetPropsChangedAfterTick( int iTick, int *iOutProps, int nMaxOutProps );
	
	// Access the recipients array.
	const CSendProxyRecipients*	GetRecipients() const;
	int							GetNumRecipients() const;

	void				SetRecipients( const CUtlMemory<CSendProxyRecipients> &recipients );
	bool				CompareRecipients( const CUtlMemory<CSendProxyRecipients> &recipients );

	void				SetSnapshotCreationTick( int nTick );
	int					GetSnapshotCreationTick() const;

	void				SetShouldCheckCreationTick( bool bState );
	bool				ShouldCheckCreationTick() const;

	void				SetServerAndClientClass( ServerClass *pServerClass, ClientClass *pClientClass );

public:
	
	ServerClass *m_pServerClass;	// Valid on the server
	ClientClass	*m_pClientClass;	// Valid on the client
		
	int			m_nEntityIndex;		// Entity index.
	int			m_ReferenceCount;	// reference count;

private:

	CUtlVector<CSendProxyRecipients>	m_Recipients;

	void				*m_pData;				// Packed data.
	int					m_nBits;				// Number of bits used to encode.
	IChangeFrameList	*m_pChangeFrameList;	// Only the most current 

	// This is the tick this PackedEntity was created on
	unsigned int		m_nSnapshotCreationTick : 31;
	unsigned int		m_nShouldCheckCreationTick : 1;
};


inline void PackedEntity::SetNumBits( int nBits )
{
	Assert( !( nBits & 31 ) );
	m_nBits = nBits;
}

inline void PackedEntity::SetCompressed()
{
	m_nBits |= FLAG_IS_COMPRESSED;
}

inline bool PackedEntity::IsCompressed() const
{
	return (m_nBits & FLAG_IS_COMPRESSED) != 0;
}

inline int PackedEntity::GetNumBits() const
{
	Assert( !( m_nBits & 31 ) ); 
	return m_nBits & ~(FLAG_IS_COMPRESSED); 
}

inline int PackedEntity::GetNumBytes() const
{
	return Bits2Bytes( m_nBits ); 
}

inline void* PackedEntity::GetData()
{
	return m_pData;
}

inline void PackedEntity::FreeData()
{
	if ( m_pData )
	{
		free(m_pData);
		m_pData = NULL;
	}
}

inline void PackedEntity::SetChangeFrameList( IChangeFrameList *pList )
{
	Assert( !m_pChangeFrameList );
	m_pChangeFrameList = pList;
}

inline IChangeFrameList* PackedEntity::GetChangeFrameList()
{
	return m_pChangeFrameList;
}

inline IChangeFrameList* PackedEntity::SnagChangeFrameList()
{
	IChangeFrameList *pRet = m_pChangeFrameList;
	m_pChangeFrameList = NULL;
	return pRet;
}

inline void PackedEntity::SetSnapshotCreationTick( int nTick )
{
	m_nSnapshotCreationTick = (unsigned int)nTick;
}

inline int PackedEntity::GetSnapshotCreationTick() const
{
	return (int)m_nSnapshotCreationTick;
}

inline void PackedEntity::SetShouldCheckCreationTick( bool bState )
{
	m_nShouldCheckCreationTick = bState ? 1 : 0;
}

inline bool PackedEntity::ShouldCheckCreationTick() const
{
	return m_nShouldCheckCreationTick == 1 ? true : false;
}


#define INVALID_PACKED_ENTITY_HANDLE (0)

//-----------------------------------------------------------------------------
// Purpose: Individual entity data, did the entity exist and what was it's serial number
//-----------------------------------------------------------------------------
class CFrameSnapshotEntry
{
public:
	ServerClass*			m_pClass = NULL;
	int						m_nSerialNumber = -1;
	// Keeps track of the fullpack info for this frame for all entities in any pvs:
	PackedEntity*			m_pPackedData = INVALID_PACKED_ENTITY_HANDLE;
};



typedef struct
{
	PackedEntity* pEntity;	// original packed entity
	int				counter;	// increaseing counter to find LRU entries
	int				bits;		// uncompressed data length in bits
	char			data[MAX_PACKEDENTITY_DATA]; // uncompressed data cache
} UnpackedDataCache_t;

class PackedEntityManager {
public:

	PackedEntityManager();
	~PackedEntityManager();

	void OnLevelChanged();

	void OnCreateSnapshot(CFrameSnapshot* pSnapshot);

	void OnDeleteSnapshot(CFrameSnapshot* pSnapshot);

	// if we are copying a Packed Entity, we have to increase the reference counter 
	void AddEntityReference(PackedEntity* pPackedEntity);

	// if we are removeing a Packed Entity, we have to decrease the reference counter
	void RemoveEntityReference(PackedEntity* pPackedEntity);

	const char* CompressPackedEntity(ServerClass* pServerClass, const char* data, int& bits);

	const char* UncompressPackedEntity(PackedEntity* pPackedEntity, int& size);

	// Return the entity sitting in iEntity's slot if iSerialNumber matches its number.
	UnpackedDataCache_t* GetCachedUncompressedEntity(PackedEntity* pPackedEntity);
	// Keeps track of the fullpack info for this frame for all entities in any pvs:
	//PackedEntityHandle_t	m_pPackedData;

		// Creates pack data for a particular entity for a particular snapshot
	PackedEntity* CreatePackedEntity(CFrameSnapshot* pSnapshot, IServerEntity* pServerEntity);

	CFrameSnapshotEntry* GetSnapshotEntry(CFrameSnapshot* pSnapshot, int entity);

	void	InitPackedEntity(CFrameSnapshot* pSnapshot, IServerEntity* pServerEntity);

	void	DoPackEntity(CFrameSnapshot* pSnapshot, IServerEntity* pServerEntity);

	// Returns the pack data for a particular entity for a particular snapshot
	PackedEntity* GetPackedEntity(CFrameSnapshot* pSnapshot, int entity);

	// Uses a previously sent packet
	bool			UsePreviouslySentPacket(CFrameSnapshot* pSnapshot, IServerEntity* pServerEntity);

	bool			ShouldForceRepack(CFrameSnapshot* pSnapshot, int entity, PackedEntity* handle);

	PackedEntity* GetPreviouslySentPacket(int iEntity, int iSerialNumber);

	void	OnClientConnected(CBaseClient* pClient);

	void	FreeClientBaselines(CBaseClient* pClient);

	void	AllocClientBaselines(CBaseClient* pClient);

	bool	ProcessBaselineAck(CBaseClient* pClient, CFrameSnapshot* pSnapshot);

	bool	IsSamePackedEntity(CEntityWriteInfo& u);

	void	GetChangedProps(CEntityWriteInfo& u);

	void	WriteDeltaEnt(CBaseClient* pClient, CEntityWriteInfo& u, const int* pCheckProps, const int nCheckProps);

	void	WriteEnterPVS(CBaseClient* pClient, CEntityWriteInfo& u);

private:
	void CheckClientSnapshotEntryArray(int maxIndex);
	static bool CFrameSnapshot_LessFunc(CFrameSnapshot* const& a, CFrameSnapshot* const& b)
	{
		return a < b;
	}

	// State information
	CUtlMap<CFrameSnapshot* ,CFrameSnapshotEntry*> m_pEntities;

	CThreadFastMutex		m_WriteMutex;

	CClassMemoryPool< PackedEntity > m_PackedEntitiesPool;

	int								m_nPackedEntityCacheCounter = 0;  // increase with every cache access
	CUtlVector<UnpackedDataCache_t>	m_PackedEntityCache;	// cache for uncompressed packed entities

	// The most recently sent packets for each entity
	PackedEntity*			m_pPackedData[MAX_EDICTS];
	int						m_pSerialNumber[MAX_EDICTS];

	class CClientBaselineInfo {
	public:
		CClientBaselineInfo() {
			m_pPackedEntities = NULL;
			m_nNumPackedEntities = 0;
		}
		// State information
		PackedEntity** m_pPackedEntities = NULL;
		int				m_nNumPackedEntities = 0;
	};
	CUtlVector<CClientBaselineInfo> m_ClientBaselineInfo;
};

extern PackedEntityManager* g_pPackedEntityManager;

#include "memdbgoff.h"

#endif // PACKED_ENTITY_H

