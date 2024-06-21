//========= Copyright Valve Corporation, All rights reserved. ============//
//
// The copyright to the contents herein is the property of Valve, L.L.C.
// The contents may be used and/or copied only with the written permission of
// Valve, L.L.C., or in accordance with the terms and conditions stipulated in
// the agreement/contract under which the contents have been supplied.
//
// model loading and caching
//
//===========================================================================//

#include <memory.h>
#include "tier0/vprof.h"
#include "tier0/icommandline.h"
#include "tier1/utllinkedlist.h"
#include "tier1/utlmap.h"
#include "datacache/imdlcache.h"
#include "istudiorender.h"
#include "filesystem.h"
#include "optimize.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imesh.h"
#include "datacache/idatacache.h"
#include "studio.h"
#include "vcollide.h"
#include "utldict.h"
#include "convar.h"
#include "datacache_common.h"
#include "mempool.h"
#include "vphysics_interface.h"
#include "phyfile.h"
#include "studiobyteswap.h"
#include "tier2/fileutils.h"
#include "filesystem/IQueuedLoader.h"
#include "tier1/lzmaDecoder.h"
#include "functors.h"
#include "const.h"

// XXX remove this later. (henryg)
#if 0 && defined(_DEBUG) && defined(_WIN32) && !defined(_X360)
typedef struct LARGE_INTEGER { unsigned long long QuadPart; } LARGE_INTEGER;
extern "C" void __stdcall OutputDebugStringA( const char *lpOutputString );
extern "C" long __stdcall QueryPerformanceCounter( LARGE_INTEGER *lpPerformanceCount );
extern "C" long __stdcall QueryPerformanceFrequency( LARGE_INTEGER *lpPerformanceCount );
namespace {
	class CDebugMicroTimer
	{
	public:
		CDebugMicroTimer(const char* n) : name(n) { QueryPerformanceCounter(&start); }
		~CDebugMicroTimer() {
			LARGE_INTEGER end;
			char outbuf[128];
			QueryPerformanceCounter(&end);
			if (!freq) QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
			V_snprintf(outbuf, 128, "%s %6d us\n", name, (int)((end.QuadPart - start.QuadPart) * 1000000 / freq));
			OutputDebugStringA(outbuf);
		}
		LARGE_INTEGER start;
		const char* name;
		static long long freq;
	};
	long long CDebugMicroTimer::freq = 0;
}
#define DEBUG_SCOPE_TIMER(name) CDebugMicroTimer dbgLocalTimer(#name)
#else
#define DEBUG_SCOPE_TIMER(name) (void)0
#endif

#ifdef _RETAIL
#define NO_LOG_MDLCACHE 1
#endif

#ifdef NO_LOG_MDLCACHE
#define LogMdlCache() 0
#else
#define LogMdlCache() mod_trace_load.GetBool()
#endif

#define MdlCacheMsg		if ( !LogMdlCache() ) ; else Msg
#define MdlCacheWarning if ( !LogMdlCache() ) ; else Warning

#if defined( _X360 )
#define AsyncMdlCache() 0	// Explicitly OFF for 360 (incompatible)
#else
#define AsyncMdlCache() 0
#endif

#define IDSTUDIOHEADER	(('T'<<24)+('S'<<16)+('D'<<8)+'I')

#define MakeCacheID( handle, type )	( ( (uint)(handle) << 16 ) | (uint)(type) )
#define HandleFromCacheID( id)		( (MDLHandle_t)((id) >> 16) )
#define TypeFromCacheID( id )		( (MDLCacheDataType_t)((id) & 0xffff) )



DEFINE_FIXEDSIZE_ALLOCATOR_MT(CStudioHdr, 128, CUtlMemoryPool::GROW_SLOW );

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CTempAllocHelper
{
public:
	CTempAllocHelper()
	{
		m_pData = NULL;
	}

	~CTempAllocHelper()
	{
		Free();
	}

	void *Get()
	{
		return m_pData;
	}

	void Alloc( int nSize )
	{
		m_pData = malloc( nSize );
	}

	void Free()
	{
		if ( m_pData )
		{
			free( m_pData );
			m_pData = NULL;
		}
	}
private:
	void *m_pData;
};

//-----------------------------------------------------------------------------
// ConVars
//-----------------------------------------------------------------------------
static ConVar r_rootlod( "r_rootlod", "0", FCVAR_ARCHIVE );
static ConVar mod_forcedata( "mod_forcedata", ( AsyncMdlCache() ) ? "0" : "1",	0, "Forces all model file data into cache on model load." );
static ConVar mod_test_not_available( "mod_test_not_available", "0", FCVAR_CHEAT );
static ConVar mod_test_mesh_not_available( "mod_test_mesh_not_available", "0", FCVAR_CHEAT );
static ConVar mod_test_verts_not_available( "mod_test_verts_not_available", "0", FCVAR_CHEAT );
static ConVar mod_load_mesh_async( "mod_load_mesh_async", ( AsyncMdlCache() ) ? "1" : "0" );
static ConVar mod_load_anims_async( "mod_load_anims_async", ( IsX360() || AsyncMdlCache() ) ? "1" : "0" );
static ConVar mod_load_vcollide_async( "mod_load_vcollide_async",  ( AsyncMdlCache() ) ? "1" : "0" );
static ConVar mod_trace_load( "mod_trace_load", "0" );
static ConVar mod_lock_mdls_on_load( "mod_lock_mdls_on_load", ( IsX360() ) ? "1" : "0" );
static ConVar mod_load_fakestall( "mod_load_fakestall", "0", 0, "Forces all ANI file loading to stall for specified ms\n");

//-----------------------------------------------------------------------------
// Purpose: turn a 2x2 blend into a 3 way triangle blend
// Returns: returns the animination indices and barycentric coordinates of a triangle
//			the triangle is a right triangle, and the diagonal is between elements [0] and [2]
//-----------------------------------------------------------------------------

static ConVar anim_3wayblend("anim_3wayblend", "1", FCVAR_REPLICATED, "Toggle the 3-way animation blending code.");

void Calc3WayBlendIndices(int i0, int i1, float s0, float s1, const mstudioseqdesc_t& seqdesc, int* pAnimIndices, float* pWeight)
{
	// Figure out which bi-section direction we are using to make triangles.
	bool bEven = (((i0 + i1) & 0x1) == 0);

	int x1, y1;
	int x2, y2;
	int x3, y3;

	// diagonal is between elements 1 & 3
	// TL to BR
	if (bEven)
	{
		if (s0 > s1)
		{
			// B
			x1 = 0; y1 = 0;
			x2 = 1; y2 = 0;
			x3 = 1; y3 = 1;
			pWeight[0] = (1.0f - s0);
			pWeight[1] = s0 - s1;
		}
		else
		{
			// C
			x1 = 1; y1 = 1;
			x2 = 0; y2 = 1;
			x3 = 0; y3 = 0;
			pWeight[0] = s0;
			pWeight[1] = s1 - s0;
		}
	}
	// BL to TR
	else
	{
		float flTotal = s0 + s1;

		if (flTotal > 1.0f)
		{
			// D
			x1 = 1; y1 = 0;
			x2 = 1; y2 = 1;
			x3 = 0; y3 = 1;
			pWeight[0] = (1.0f - s1);
			pWeight[1] = s0 - 1.0f + s1;
		}
		else
		{
			// A
			x1 = 0; y1 = 1;
			x2 = 0; y2 = 0;
			x3 = 1; y3 = 0;
			pWeight[0] = s1;
			pWeight[1] = 1.0f - s0 - s1;
		}
	}

	pAnimIndices[0] = seqdesc.anim(i0 + x1, i1 + y1);
	pAnimIndices[1] = seqdesc.anim(i0 + x2, i1 + y2);
	pAnimIndices[2] = seqdesc.anim(i0 + x3, i1 + y3);

	/*
	float w0 = ((x2-x3)*(y3-s1) - (x3-s0)*(y2-y3)) / ((x1-x3)*(y2-y3) - (x2-x3)*(y1-y3));
	float w1 = ((x1-x3)*(y3-s1) - (x3-s0)*(y1-y3)) / ((x2-x3)*(y1-y3) - (x1-x3)*(y2-y3));
	Assert( pWeight[0] == w0 && pWeight[1] == w1 );
	*/

	// clamp the diagonal
	if (pWeight[1] < 0.001f)
		pWeight[1] = 0.0f;
	pWeight[2] = 1.0f - pWeight[0] - pWeight[1];

	Assert(pWeight[0] >= 0.0f && pWeight[0] <= 1.0f);
	Assert(pWeight[1] >= 0.0f && pWeight[1] <= 1.0f);
	Assert(pWeight[2] >= 0.0f && pWeight[2] <= 1.0f);
}

//-----------------------------------------------------------------------------
// Utility functions
//-----------------------------------------------------------------------------

#if defined( USE_HARDWARE_CACHE )
unsigned ComputeHardwareDataSize( studiohwdata_t *pData )
{
	unsigned size = 0;
	for ( int i = pData->m_RootLOD; i < pData->m_NumLODs; i++ )
	{
		studioloddata_t *pLOD = &pData->m_pLODs[i];
		for ( int j = 0; j < pData->m_NumStudioMeshes; j++ )
		{
			studiomeshdata_t *pMeshData = &pLOD->m_pMeshData[j];
			for ( int k = 0; k < pMeshData->m_NumGroup; k++ )
			{
				size += pMeshData->m_pMeshGroup[k].m_pMesh->ComputeMemoryUsed();
			}
		}
	}
	return size;
}
#endif

//-----------------------------------------------------------------------------
// Async support
//-----------------------------------------------------------------------------

#define MDLCACHE_NONE ((MDLCacheDataType_t)-1)

struct AsyncInfo_t
{
	AsyncInfo_t() : hControl( NULL ), hModel( MDLHANDLE_INVALID ), type( MDLCACHE_NONE ), iAnimBlock( 0 ) {}

	FSAsyncControl_t	hControl;
	MDLHandle_t			hModel;
	MDLCacheDataType_t	type;
	int					iAnimBlock;
};

const intp NO_ASYNC = CUtlFixedLinkedList< AsyncInfo_t >::InvalidIndex();

//-------------------------------------

CUtlMap<int, intp> g_AsyncInfoMap( DefLessFunc( int ) );
CThreadFastMutex g_AsyncInfoMapMutex;

inline int MakeAsyncInfoKey( MDLHandle_t hModel, MDLCacheDataType_t type, int iAnimBlock )
{
	Assert( type <= 7 && iAnimBlock < 8*1024 );
	return ( ( ( (int)hModel) << 16 ) | ( (int)type << 13 ) | iAnimBlock );
}

inline intp GetAsyncInfoIndex( MDLHandle_t hModel, MDLCacheDataType_t type, int iAnimBlock = 0 )
{
	AUTO_LOCK( g_AsyncInfoMapMutex );
	int key = MakeAsyncInfoKey( hModel, type, iAnimBlock );
	int i = g_AsyncInfoMap.Find( key );
	if ( i == g_AsyncInfoMap.InvalidIndex() )
	{
		return NO_ASYNC;
	}
	return g_AsyncInfoMap[i];
}

inline intp SetAsyncInfoIndex( MDLHandle_t hModel, MDLCacheDataType_t type, int iAnimBlock, intp index )
{
	AUTO_LOCK( g_AsyncInfoMapMutex );
	Assert( index == NO_ASYNC || GetAsyncInfoIndex( hModel, type, iAnimBlock ) == NO_ASYNC );
	int key = MakeAsyncInfoKey( hModel, type, iAnimBlock );
	if ( index == NO_ASYNC )
	{
		g_AsyncInfoMap.Remove( key );
	}
	else
	{
		g_AsyncInfoMap.Insert( key, index );
	}

	return index;
}

inline intp SetAsyncInfoIndex( MDLHandle_t hModel, MDLCacheDataType_t type, intp index )
{
	return SetAsyncInfoIndex( hModel, type, 0, index );
}

//-----------------------------------------------------------------------------
// QUEUED LOADING
// Populates the cache by pushing expected MDL's (and all of their data).
// The Model cache i/o behavior is unchanged during gameplay, ideally the cache
// should yield miss free behaviour.
//-----------------------------------------------------------------------------

struct ModelParts_t
{
	enum BufferType_t
	{
		BUFFER_MDL = 0,
		BUFFER_VTX = 1,
		BUFFER_VVD = 2,
		BUFFER_PHY = 3,
		BUFFER_MAXPARTS,
	};

	ModelParts_t()
	{
		nLoadedParts = 0;
		nExpectedParts = 0;
		hMDL = MDLHANDLE_INVALID;
		hFileCache = 0;
		bHeaderLoaded = false;
		bMaterialsPending = false;
		bTexturesPending = false;
	}

	// thread safe, only one thread will get a positive result
	bool DoFinalProcessing()
	{
		// indicates that all buffers have arrived
		// when all parts are present, returns true ( guaranteed once ), and marked as completed
		return nLoadedParts.AssignIf( nExpectedParts, nExpectedParts | 0x80000000 );
	}

	CUtlBuffer		Buffers[BUFFER_MAXPARTS];
	MDLHandle_t		hMDL;

	// async material loading on PC
	FileCacheHandle_t hFileCache;
	bool			bHeaderLoaded;
	bool			bMaterialsPending;
	bool			bTexturesPending;
	CUtlVector< IMaterial* > Materials;

	// bit flags
	CInterlockedInt	nLoadedParts;
	int				nExpectedParts;

private:
	ModelParts_t(const ModelParts_t&); // no impl
	ModelParts_t& operator=(const ModelParts_t&); // no impl
};

struct CleanupModelParts_t
{
	FileCacheHandle_t hFileCache;
	CUtlVector< IMaterial* > Materials;
};

// a string table to speed up searching for sequences in the current virtual model
struct modellookup_t
{
	CUtlDict<short, short> seqTable;
	CUtlDict<short, short> animTable;
};

static CUtlVector<modellookup_t> g_ModelLookup;
static int g_ModelLookupIndex = -1;

inline bool HasLookupTable()
{
	return g_ModelLookupIndex >= 0 ? true : false;
}

inline CUtlDict<short, short>* GetSeqTable()
{
	return &g_ModelLookup[g_ModelLookupIndex].seqTable;
}

inline CUtlDict<short, short>* GetAnimTable()
{
	return &g_ModelLookup[g_ModelLookupIndex].animTable;
}

class CModelLookupContext
{
public:
	CModelLookupContext(int group, const CStudioHdr* pStudioHdr);
	~CModelLookupContext();

private:
	int		m_lookupIndex;
};

CModelLookupContext::CModelLookupContext(int group, const CStudioHdr* pStudioHdr)
{
	m_lookupIndex = -1;
	if (group == 0 && pStudioHdr->numincludemodels())
	{
		m_lookupIndex = g_ModelLookup.AddToTail();
		g_ModelLookupIndex = g_ModelLookup.Count() - 1;
	}
}

CModelLookupContext::~CModelLookupContext()
{
	if (m_lookupIndex >= 0)
	{
		Assert(m_lookupIndex == (g_ModelLookup.Count() - 1));
		g_ModelLookup.FastRemove(m_lookupIndex);
		g_ModelLookupIndex = g_ModelLookup.Count() - 1;
	}
}

// NOTE: If CStringRegistry allowed storing arbitrary data, we could just use that.
// in this case we have the "isPrivate" member and the replacement rules 
// (activityIndex can be reused by private activities), so a custom table is necessary
struct activitylist_t
{
	int					activityIndex;
	unsigned short		stringKey;
	short				isPrivate;
};

// NOTE: If CStringRegistry allowed storing arbitrary data, we could just use that.
// in this case we have the "isPrivate" member and the replacement rules 
// (eventIndex can be reused by private activities), so a custom table is necessary
struct eventlist_t
{
	int					eventIndex;
	int					iType;
	unsigned short		stringKey;
	short				isPrivate;
};

struct StringTable_t : public CUtlDict<int, unsigned short>
{
};

//-----------------------------------------------------------------------------
// Purpose: Just a convenience/legacy wrapper for CUtlDict<> .
//-----------------------------------------------------------------------------
class CStringRegistry
{
private:
	StringTable_t* m_pStringList;

public:
	// returns a key for a given string
	unsigned short AddString(const char* stringText, int stringID);

	// This is optimized.  It will do 2 O(logN) searches
	// Only one of the searches needs to compare strings, the other compares symbols (ints)
	// returns -1 if the string is not present in the registry.
	int		GetStringID(const char* stringText);

	// This is unoptimized.  It will linearly search (but only compares ints, not strings)
	const char* GetStringText(int stringID);

	// This is O(1).  It will not search.  key MUST be a value that was returned by AddString
	const char* GetStringForKey(unsigned short key);
	// This is O(1).  It will not search.  key MUST be a value that was returned by AddString
	int		GetIDForKey(unsigned short key);

	void	ClearStrings(void);


	// Iterate all the keys.
	unsigned short First() const;
	unsigned short Next(unsigned short key) const;
	unsigned short InvalidIndex() const;

	~CStringRegistry(void);			// Need to free allocated memory
	CStringRegistry(void);
};

//-----------------------------------------------------------------------------
// Purpose: Add null terminated string to the string registry 
// Input  :
// Output :
//-----------------------------------------------------------------------------
unsigned short CStringRegistry::AddString(const char* stringText, int stringID)
{
	return m_pStringList->Insert(stringText, stringID);
}

//-----------------------------------------------------------------------------
// Purpose: Given string text get the string ID
// Input  :	Text of string to find
// Output : Return string id or -1 if no such string exists
//-----------------------------------------------------------------------------
int	CStringRegistry::GetStringID(const char* stringText)
{
	unsigned short index = m_pStringList->Find(stringText);
	if (m_pStringList->IsValidIndex(index))
	{
		return (*m_pStringList)[index];
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: Given a string ID return the string text
// Input  : ID of string to find
// Output : Return string text of NULL of no such ID exists
//-----------------------------------------------------------------------------
char const* CStringRegistry::GetStringText(int stringID)
{
	for (unsigned short index = m_pStringList->First(); index != m_pStringList->InvalidIndex(); index = m_pStringList->Next(index))
	{
		if ((*m_pStringList)[index] == stringID)
		{
			return m_pStringList->GetElementName(index);
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Given a key return the string text
//-----------------------------------------------------------------------------
char const* CStringRegistry::GetStringForKey(unsigned short key)
{
	if (!m_pStringList->IsValidIndex(key))
		return NULL;

	return m_pStringList->GetElementName(key);
}

//-----------------------------------------------------------------------------
// Purpose: Given a key return the string text
//-----------------------------------------------------------------------------
int CStringRegistry::GetIDForKey(unsigned short key)
{
	if (!m_pStringList->IsValidIndex(key))
		return 0;

	return (*m_pStringList)[key];
}

//-----------------------------------------------------------------------------
// Purpose: Clear all strings from the string registry
//-----------------------------------------------------------------------------
void CStringRegistry::ClearStrings(void)
{
	m_pStringList->RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Destructor - delete the list of strings and maps
// Input  :
// Output :
//-----------------------------------------------------------------------------
CStringRegistry::~CStringRegistry(void)
{
	delete m_pStringList;
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
// Input  :
// Output :
//-----------------------------------------------------------------------------
CStringRegistry::CStringRegistry(void)
{
	m_pStringList = new StringTable_t;
}


unsigned short CStringRegistry::First() const
{
	return m_pStringList->First();
}

unsigned short CStringRegistry::Next(unsigned short key) const
{
	return m_pStringList->Next(key);
}

unsigned short CStringRegistry::InvalidIndex() const
{
	return m_pStringList->InvalidIndex();
}

//-----------------------------------------------------------------------------
// Implementation of the simple studio data cache (no caching)
//-----------------------------------------------------------------------------
class CMDLCache : public CTier3AppSystem< IMDLCache >, public CDefaultDataCacheClient//public IStudioDataCache, 
{
	typedef CTier3AppSystem< IMDLCache > BaseClass;
	friend class CStudioHdr;
public:
	CMDLCache();

	// Inherited from IAppSystem
	virtual bool Connect( CreateInterfaceFn factory );
	virtual void Disconnect();
	virtual void *QueryInterface( const char *pInterfaceName );
	virtual InitReturnVal_t Init();
	virtual void Shutdown();

	// Inherited from IStudioDataCache
	bool VerifyHeaders( studiohdr_t *pStudioHdr );

	// Inherited from IMDLCache
	virtual MDLHandle_t FindMDL( const char *pMDLRelativePath );
	virtual int AddRef( MDLHandle_t handle );
	virtual int Release( MDLHandle_t handle );
	virtual int GetRef( MDLHandle_t handle );
	virtual void MarkAsLoaded(MDLHandle_t handle);

	virtual void SetCacheNotify(IMDLCacheNotify* pNotify);
	virtual void Flush(MDLCacheFlush_t nFlushFlags = MDLCACHE_FLUSH_ALL);

	virtual IStudioHdr* GetIStudioHdr(MDLHandle_t handle);
	virtual CStudioHdr* GetStudioHdr(MDLHandle_t handle);
	//virtual IStudioHdr* GetIStudioHdr(studiohdr_t* pStudioHdr);

	virtual studiohwdata_t *GetHardwareData( MDLHandle_t handle );
	virtual bool GetVCollideSize(MDLHandle_t handle, int* pVCollideSize);
	virtual vcollide_t *GetVCollide( MDLHandle_t handle ) { return GetVCollideEx( handle, true); }
	virtual vcollide_t *GetVCollideEx( MDLHandle_t handle, bool synchronousLoad = true );
	virtual unsigned char *GetAnimBlock( MDLHandle_t handle, int nBlock );
	virtual CVirtualModel *GetVirtualModel( MDLHandle_t handle );
	//virtual CVirtualModel *GetVirtualModelFast( const studiohdr_t *pStudioHdr, MDLHandle_t handle );
	virtual int GetAutoplayList( MDLHandle_t handle, unsigned short **pOut );
	virtual void TouchAllData( MDLHandle_t handle );
	virtual void SetUserData( MDLHandle_t handle, void* pData );
	virtual void *GetUserData( MDLHandle_t handle );
	virtual bool IsErrorModel( MDLHandle_t handle );
	virtual vertexFileHeader_t *GetVertexData( MDLHandle_t handle );
	virtual void Flush( MDLHandle_t handle, int nFlushFlags = MDLCACHE_FLUSH_ALL );
	virtual const char *GetModelName( MDLHandle_t handle );
	virtual bool IsDataLoaded(MDLHandle_t handle, MDLCacheDataType_t type);

	virtual IStudioHdr* LockStudioHdr(MDLHandle_t handle);
	virtual void UnlockStudioHdr(MDLHandle_t handle);

	virtual bool PreloadModel(MDLHandle_t handle);
	virtual void ResetErrorModelStatus(MDLHandle_t handle);

	IDataCacheSection *GetCacheSection( MDLCacheDataType_t type )
	{
		switch ( type )
		{
		case MDLCACHE_STUDIOHWDATA:
		case MDLCACHE_VERTEXES:
			// meshes and vertexes are isolated to their own section
			return m_pMeshCacheSection;

		case MDLCACHE_ANIMBLOCK:
			// anim blocks have their own section
			return m_pAnimBlockCacheSection;

		default:
			// everybody else
			return m_pModelCacheSection;
		}
	}

	void *AllocData( MDLCacheDataType_t type, int size );
	void FreeData( MDLCacheDataType_t type, void *pData );
	void CacheData( DataCacheHandle_t *c, void *pData, int size, const char *name, MDLCacheDataType_t type, DataCacheClientID_t id = (DataCacheClientID_t)-1 );
	void *CheckData( DataCacheHandle_t c, MDLCacheDataType_t type );
	void *CheckDataNoTouch( DataCacheHandle_t c, MDLCacheDataType_t type );
	void UncacheData( DataCacheHandle_t c, MDLCacheDataType_t type, bool bLockedOk = false );

	void DisableAsync() { mod_load_mesh_async.SetValue( 0 ); mod_load_anims_async.SetValue( 0 ); }

	virtual void BeginLock();
	virtual void EndLock();
	virtual int *GetFrameUnlockCounterPtrOLD();
	virtual int *GetFrameUnlockCounterPtr( MDLCacheDataType_t type );

	virtual void FinishPendingLoads();

	// Task switch
	void ReleaseMaterialSystemObjects();
	void RestoreMaterialSystemObjects( int nChangeFlags );

	virtual void BeginMapLoad();
	virtual void EndMapLoad();

	virtual void InitPreloadData( bool rebuild );
	virtual void ShutdownPreloadData();

	virtual void MarkFrame();

	// Queued loading
	void ProcessQueuedData( ModelParts_t *pModelParts, bool bHeaderOnly = false );
	static void	QueuedLoaderCallback_MDL( void *pContext, void  *pContext2, const void *pData, int nSize, LoaderError_t loaderError );
	static void	ProcessDynamicLoad( ModelParts_t *pModelParts );
	static void CleanupDynamicLoad( CleanupModelParts_t *pCleanup );

	virtual bool ActivityList_Inited();
	virtual void ActivityList_Init(void);
	virtual void ActivityList_Clear(void);
	virtual void ActivityList_Free(void);
	virtual bool ActivityList_RegisterSharedActivity(const char* pszActivityName, int iActivityIndex);
	//#ifdef GAME_DLL
	virtual int ActivityList_RegisterPrivateActivity(const char* pszActivityName);
	//#endif // GAME_DLL
	virtual int ActivityList_IndexForName(const char* pszActivityName);
	virtual const char* ActivityList_NameForIndex(int iActivityIndex);
	virtual int ActivityList_HighestIndex();
	virtual void EventList_Init(void);
	virtual void EventList_Clear(void);
	virtual void EventList_Free(void);
	virtual bool EventList_RegisterSharedEvent(const char* pszEventName, int iEventIndex, int iType = 0);
//#ifdef GAME_DLL
	virtual int EventList_RegisterPrivateEvent(const char* pszEventName);
//#endif // GAME_DLL
	virtual int EventList_IndexForName(const char* pszEventName);
	virtual const char* EventList_NameForIndex(int iEventIndex);
	virtual int EventList_GetEventType(int eventIndex);
	void SetEventIndexForSequence(mstudioseqdesc_t& seqdesc);
	mstudioevent_t* GetEventIndexForSequence(mstudioseqdesc_t& seqdesc);

private:
	// Inits, shuts downs studiodata_t
	void InitStudioData( MDLHandle_t handle, const char* pModelName);
	void ShutdownStudioData( MDLHandle_t handle );

	// Returns the *actual* name of the model (could be an error model if the requested model didn't load)
	const char *GetActualModelName( MDLHandle_t handle );

	// Constructs a filename based on a model handle
	void MakeFilename( MDLHandle_t handle, const char *pszExtension, char *pszFileName, int nMaxLength );

	// Inform filesystem that we unloaded a particular file
	void NotifyFileUnloaded( MDLHandle_t handle, const char *pszExtension );

	FSAsyncStatus_t LoadData( const char *pszFilename, const char *pszPathID, bool bAsync, FSAsyncControl_t *pControl ) { return LoadData( pszFilename, pszPathID, NULL, 0, 0, bAsync, pControl ); }
	FSAsyncStatus_t LoadData( const char *pszFilename, const char *pszPathID, void *pDest, int nBytes, int nOffset, bool bAsync, FSAsyncControl_t *pControl );

	int ProcessPendingAsync( intp iAsync );
	void ProcessPendingAsyncs( MDLCacheDataType_t type = MDLCACHE_NONE );

	const char *GetVTXExtension();

	virtual bool HandleCacheNotification( const DataCacheNotification_t &notification  );
	virtual bool GetItemName( DataCacheClientID_t clientId, const void *pItem, char *pDest, unsigned nMaxLen  );

	virtual bool GetAsyncLoad( MDLCacheDataType_t type );
	virtual bool SetAsyncLoad( MDLCacheDataType_t type, bool bAsync );

	// Creates the 360 file if it doesn't exist or is out of date
	int UpdateOrCreate( studiohdr_t *pHdr, const char *pFilename, char *pX360Filename, int maxLen, const char *pPathID, bool bForce = false );

	// Attempts to read the platform native file - on 360 it can read and swap Win32 file as a fallback
	bool ReadFileNative( char *pFileName, const char *pPath, CUtlBuffer &buf, int nMaxBytes = 0, MDLCacheDataType_t type = MDLCACHE_NONE );

	void BreakFrameLock( bool bModels = true, bool bMesh = true );
	void RestoreFrameLock();

	activitylist_t* ListFromActivity(int activityIndex);
	activitylist_t* ListFromString(const char* pString);
	activitylist_t* ActivityList_AddActivityEntry(const char* pName, int iActivityIndex, bool isPrivate);
	int ActivityListVersion(){
		return g_nActivityListVersion;
	}

	eventlist_t* EventList_ListFromString(const char* pString);
	eventlist_t* ListFromEvent(int eventIndex);
	eventlist_t* EventList_AddEventEntry(const char* pName, int iEventIndex, bool isPrivate, int iType);
	int EventListVersion() {
		return g_nEventListVersion;
	}
private:
	IDataCacheSection *m_pModelCacheSection;
	IDataCacheSection *m_pMeshCacheSection;
	IDataCacheSection *m_pAnimBlockCacheSection;

	int m_nModelCacheFrameLocks;
	int m_nMeshCacheFrameLocks;

	CUtlDict< CStudioHdr*, MDLHandle_t > m_MDLDict;

	IMDLCacheNotify *m_pCacheNotify;

	CUtlFixedLinkedList< AsyncInfo_t > m_PendingAsyncs;

	CThreadFastMutex m_QueuedLoadingMutex;
	CThreadFastMutex m_AsyncMutex;

	bool m_bLostVideoMemory : 1;
	bool m_bConnected : 1;
	bool m_bInitialized : 1;

	// This stores the actual activity names.  Also, the string ID in the registry is simply an index 
// into the g_ActivityList array.
	CStringRegistry	g_ActivityStrings;

	// this is just here to accelerate adds
	static int g_HighestActivity;

	int g_nActivityListVersion = 1;

	CUtlVector<activitylist_t> g_ActivityList;

	CUtlVector<eventlist_t> g_EventList;

	// This stores the actual event names.  Also, the string ID in the registry is simply an index 
	// into the g_EventList array.
	CStringRegistry	g_EventStrings;

	// this is just here to accelerate adds
	static int g_HighestEvent;

	int g_nEventListVersion = 1;

	bool m_bActivityInited = false;
};

int CMDLCache::g_HighestActivity = 0;
int CMDLCache::g_HighestEvent = 0;

//-----------------------------------------------------------------------------
// Singleton interface
//-----------------------------------------------------------------------------
static CMDLCache g_MDLCache;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CMDLCache, IMDLCache, MDLCACHE_INTERFACE_VERSION, g_MDLCache );
//EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CMDLCache, IStudioDataCache, STUDIO_DATA_CACHE_INTERFACE_VERSION, g_MDLCache );


//-----------------------------------------------------------------------------
// Task switch
//-----------------------------------------------------------------------------
static void ReleaseMaterialSystemObjects( )
{
	g_MDLCache.ReleaseMaterialSystemObjects();
}

static void RestoreMaterialSystemObjects( int nChangeFlags )
{
	g_MDLCache.RestoreMaterialSystemObjects( nChangeFlags );
}



//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CMDLCache::CMDLCache() : BaseClass( false )
{
	m_bLostVideoMemory = false;
	m_bConnected = false;
	m_bInitialized = false;
	m_pCacheNotify = NULL;
	m_pModelCacheSection = NULL;
	m_pMeshCacheSection = NULL;
	m_pAnimBlockCacheSection = NULL;
	m_nModelCacheFrameLocks = 0;
	m_nMeshCacheFrameLocks = 0;
}


//-----------------------------------------------------------------------------
// Connect, disconnect
//-----------------------------------------------------------------------------
bool CMDLCache::Connect( CreateInterfaceFn factory )
{
	// Connect can be called twice, because this inherits from 2 appsystems.
	if ( m_bConnected )
		return true;

	if ( !BaseClass::Connect( factory ) )
		return false;

	if ( !g_pMaterialSystemHardwareConfig || !g_pPhysicsCollision || !g_pStudioRender || !g_pMaterialSystem )
		return false;

	m_bConnected = true;
	if( g_pMaterialSystem )
	{
		g_pMaterialSystem->AddReleaseFunc( ::ReleaseMaterialSystemObjects );
		g_pMaterialSystem->AddRestoreFunc( ::RestoreMaterialSystemObjects );
	}

	return true;
}

void CMDLCache::Disconnect()
{
	if ( g_pMaterialSystem && m_bConnected )
	{
		g_pMaterialSystem->RemoveReleaseFunc( ::ReleaseMaterialSystemObjects );
		g_pMaterialSystem->RemoveRestoreFunc( ::RestoreMaterialSystemObjects );
		m_bConnected = false;
	}

	BaseClass::Disconnect();
}


//-----------------------------------------------------------------------------
// Query Interface
//-----------------------------------------------------------------------------
void *CMDLCache::QueryInterface( const char *pInterfaceName )
{
	//if (!Q_strncmp(	pInterfaceName, STUDIO_DATA_CACHE_INTERFACE_VERSION, Q_strlen(STUDIO_DATA_CACHE_INTERFACE_VERSION) + 1))
	//	return (IStudioDataCache*)this;

	if (!Q_strncmp(	pInterfaceName, MDLCACHE_INTERFACE_VERSION, Q_strlen(MDLCACHE_INTERFACE_VERSION) + 1))
		return (IMDLCache*)this;

	return NULL;
}


//-----------------------------------------------------------------------------
// Init/Shutdown
//-----------------------------------------------------------------------------

#define MODEL_CACHE_MODEL_SECTION_NAME		"ModelData"
#define MODEL_CACHE_MESH_SECTION_NAME		"ModelMesh"
#define MODEL_CACHE_ANIMBLOCK_SECTION_NAME	"AnimBlock"

// #define ENABLE_CACHE_WATCH 1

#if defined( ENABLE_CACHE_WATCH )
static ConVar cache_watch( "cache_watch", "", 0 );

static void CacheLog( const char *fileName, const char *accessType )
{
	if ( Q_stristr( fileName, cache_watch.GetString() ) )
	{
		Msg( "%s access to %s\n", accessType, fileName );
	}
}
#endif

InitReturnVal_t CMDLCache::Init()
{
	// Can be called twice since it inherits from 2 appsystems
	if ( m_bInitialized )
		return INIT_OK;

	MathLib_Init(2.2f, 2.2f, 0.0f, 2.0f);

	InitReturnVal_t nRetVal = BaseClass::Init();
	if ( nRetVal != INIT_OK )
		return nRetVal;

	if ( !m_pModelCacheSection )
	{
		m_pModelCacheSection = g_pDataCache->AddSection( this, MODEL_CACHE_MODEL_SECTION_NAME );
	}

	if ( !m_pMeshCacheSection )
	{
		unsigned int meshLimit = (unsigned)-1;
		DataCacheLimits_t limits( meshLimit, (unsigned)-1, 0, 0 );
		m_pMeshCacheSection = g_pDataCache->AddSection( this, MODEL_CACHE_MESH_SECTION_NAME, limits );
	}

	if ( !m_pAnimBlockCacheSection )
	{
		// 360 tuned to worst case, ep_outland_12a, less than 6 MB is not a viable working set
		unsigned int animBlockLimit = IsX360() ? 6*1024*1024 : (unsigned)-1;
		DataCacheLimits_t limits( animBlockLimit, (unsigned)-1, 0, 0 );
		m_pAnimBlockCacheSection = g_pDataCache->AddSection( this, MODEL_CACHE_ANIMBLOCK_SECTION_NAME, limits );
	}

	if ( IsX360() )
	{
		// By default, source data is assumed to be non-native to the 360.
		StudioByteSwap::ActivateByteSwapping( true );
		StudioByteSwap::SetCollisionInterface( g_pPhysicsCollision );
	}
	m_bLostVideoMemory = false;
	m_bInitialized = true;

#if defined( ENABLE_CACHE_WATCH )
	g_pFullFileSystem->AddLoggingFunc( &CacheLog );
#endif

	return INIT_OK;
}

void CMDLCache::Shutdown()
{
	if ( !m_bInitialized )
		return;
#if defined( ENABLE_CACHE_WATCH )
	g_pFullFileSystem->RemoveLoggingFunc( CacheLog );
#endif
	m_bInitialized = false;

	if ( m_pModelCacheSection || m_pMeshCacheSection )
	{
		// Free all MDLs that haven't been cleaned up
		MDLHandle_t i = m_MDLDict.First();
		while ( i != m_MDLDict.InvalidIndex() )
		{
			ShutdownStudioData( i );
			i = m_MDLDict.Next( i );
		}

		m_MDLDict.Purge();

		if ( m_pModelCacheSection )
		{
			g_pDataCache->RemoveSection( MODEL_CACHE_MODEL_SECTION_NAME );
			m_pModelCacheSection = NULL;
		}
		if ( m_pMeshCacheSection )
		{
			g_pDataCache->RemoveSection( MODEL_CACHE_MESH_SECTION_NAME );
			m_pMeshCacheSection = NULL;
		}
	}

	if ( m_pAnimBlockCacheSection )
	{
		g_pDataCache->RemoveSection( MODEL_CACHE_ANIMBLOCK_SECTION_NAME );
		m_pAnimBlockCacheSection = NULL;
	}

	BaseClass::Shutdown();
}


//-----------------------------------------------------------------------------
// Flushes an MDLHandle_t
//-----------------------------------------------------------------------------
void CMDLCache::Flush( MDLHandle_t handle, int nFlushFlags )
{
	CStudioHdr *pStudioData = GetStudioHdr(handle);
	Assert( pStudioData != NULL );

	bool bIgnoreLock = ( nFlushFlags & MDLCACHE_FLUSH_IGNORELOCK ) != 0;

	// release the hardware portion
	if ( nFlushFlags & MDLCACHE_FLUSH_STUDIOHWDATA )
	{
		if (pStudioData->ClearAsync( MDLCACHE_STUDIOHWDATA, 0, true ) )
		{
			m_pMeshCacheSection->Unlock( pStudioData->m_VertexCache );
		}
		pStudioData->UnloadHardwareData( true, bIgnoreLock );
	}

	// free collision
	if ( nFlushFlags & MDLCACHE_FLUSH_VCOLLIDE )
	{
		pStudioData->DestroyVCollide();
	}

	// Free animations
	if ( nFlushFlags & MDLCACHE_FLUSH_VIRTUALMODEL )
	{
		pStudioData->FreeVirtualModel();
	}

	if ( nFlushFlags & MDLCACHE_FLUSH_ANIMBLOCK )
	{
		pStudioData->FreeAnimBlocks();
	}

	if ( nFlushFlags & MDLCACHE_FLUSH_AUTOPLAY )
	{
		// Free autoplay sequences
		pStudioData->FreeAutoplaySequences();
	}

	if ( nFlushFlags & MDLCACHE_FLUSH_STUDIOHDR )
	{
		MdlCacheMsg( "MDLCache: Free studiohdr %s\n", GetModelName( handle ) );

		if ( pStudioData->m_nFlags & STUDIODATA_FLAGS_LOCKED_MDL )
		{
			GetCacheSection( MDLCACHE_STUDIOHDR )->Unlock( pStudioData->m_MDLCache );
			pStudioData->m_nFlags &= ~STUDIODATA_FLAGS_LOCKED_MDL;
		}
		UncacheData( pStudioData->m_MDLCache, MDLCACHE_STUDIOHDR, bIgnoreLock );
		pStudioData->m_MDLCache = NULL;
	}

	if ( nFlushFlags & MDLCACHE_FLUSH_VERTEXES )
	{
		MdlCacheMsg( "MDLCache: Free VVD %s\n", GetModelName( handle ) );

		pStudioData->ClearAsync( MDLCACHE_VERTEXES, 0, true );

		UncacheData( pStudioData->m_VertexCache, MDLCACHE_VERTEXES, bIgnoreLock );
		pStudioData->m_VertexCache = NULL;
	}

	// Now check whatever files are not loaded, make sure file system knows
	// that we don't have them loaded.
	if ( !IsDataLoaded( handle, MDLCACHE_STUDIOHDR ) )
		NotifyFileUnloaded( handle, ".mdl" );

	if ( !IsDataLoaded( handle, MDLCACHE_STUDIOHWDATA ) )
		NotifyFileUnloaded( handle, GetVTXExtension() );

	if ( !IsDataLoaded( handle, MDLCACHE_VERTEXES ) )
		NotifyFileUnloaded( handle, ".vvd" );

	if ( !IsDataLoaded( handle, MDLCACHE_VCOLLIDE ) )
		NotifyFileUnloaded( handle, ".phy" );
}


//-----------------------------------------------------------------------------
// Inits, shuts downs studiodata_t
//-----------------------------------------------------------------------------
void CMDLCache::InitStudioData( MDLHandle_t handle, const char* pModelName)
{
	Assert( m_MDLDict[handle] == NULL );

	CStudioHdr* pStudioData = new CStudioHdr(this, handle, pModelName);
	m_MDLDict[handle] = pStudioData;
	//memset( pStudioData, 0, sizeof( studiodata_t ) );
}

void CMDLCache::ShutdownStudioData( MDLHandle_t handle )
{
	Flush( handle );

	CStudioHdr *pStudioData = m_MDLDict[handle];
	Assert( pStudioData != NULL );
	delete pStudioData;
	m_MDLDict[handle] = NULL;
}


//-----------------------------------------------------------------------------
// Sets the cache notify
//-----------------------------------------------------------------------------
void CMDLCache::SetCacheNotify( IMDLCacheNotify *pNotify )
{
	m_pCacheNotify = pNotify;
}


//-----------------------------------------------------------------------------
// Returns the name of the model
//-----------------------------------------------------------------------------
const char *CMDLCache::GetModelName( MDLHandle_t handle )
{
	if ( handle == MDLHANDLE_INVALID  )
		return ERROR_MODEL;

	return m_MDLDict[handle]->GetModelName();
}


//-----------------------------------------------------------------------------
// Returns the *actual* name of the model (could be an error model)
//-----------------------------------------------------------------------------
const char *CMDLCache::GetActualModelName( MDLHandle_t handle )
{
	if ( handle == MDLHANDLE_INVALID )
		return ERROR_MODEL;

	return m_MDLDict[handle]->GetActualModelName();
}


//-----------------------------------------------------------------------------
// Constructs a filename based on a model handle
//-----------------------------------------------------------------------------
void CMDLCache::MakeFilename( MDLHandle_t handle, const char *pszExtension, char *pszFileName, int nMaxLength )
{
	Q_strncpy( pszFileName, GetActualModelName( handle ), nMaxLength );
	Q_SetExtension( pszFileName, pszExtension, nMaxLength );
	Q_FixSlashes( pszFileName );
#ifdef POSIX
	Q_strlower( pszFileName );
#endif
}

//-----------------------------------------------------------------------------
void CMDLCache::NotifyFileUnloaded( MDLHandle_t handle, const char *pszExtension )
{
	if ( handle == MDLHANDLE_INVALID )
		return;
	if ( !m_MDLDict.IsValidIndex( handle ) )
		return;

	char szFilename[MAX_PATH];
	V_strcpy_safe( szFilename, m_MDLDict.GetElementName( handle ) );
	V_SetExtension( szFilename, pszExtension, sizeof(szFilename) );
	V_FixSlashes( szFilename );
	g_pFullFileSystem->NotifyFileUnloaded( szFilename, "game" );
}


//-----------------------------------------------------------------------------
// Finds an MDL
//-----------------------------------------------------------------------------
MDLHandle_t CMDLCache::FindMDL( const char *pMDLRelativePath )
{
	// can't trust provided path
	// ensure provided path correctly resolves (Dictionary is case-insensitive)
	char szFixedName[MAX_PATH];
	V_strncpy( szFixedName, pMDLRelativePath, sizeof( szFixedName ) );
	V_RemoveDotSlashes( szFixedName, '/' );

	MDLHandle_t handle = m_MDLDict.Find( szFixedName );
	if ( handle == m_MDLDict.InvalidIndex() )
	{
		handle = m_MDLDict.Insert( szFixedName, NULL );
		InitStudioData( handle, m_MDLDict.GetElementName(handle));
	}

	AddRef( handle );
	return handle;
}

//-----------------------------------------------------------------------------
// Reference counting
//-----------------------------------------------------------------------------
int CMDLCache::AddRef( MDLHandle_t handle )
{
	return ++m_MDLDict[handle]->m_nRefCount;
}

int CMDLCache::Release( MDLHandle_t handle )
{
	// Deal with shutdown order issues (i.e. datamodel shutting down after mdlcache)
	if ( !m_bInitialized )
		return 0;

	// NOTE: It can be null during shutdown because multiple studiomdls
	// could be referencing the same virtual model
	if ( !m_MDLDict[handle] )
		return 0;

	Assert( m_MDLDict[handle]->m_nRefCount > 0 );

	int nRefCount = --m_MDLDict[handle]->m_nRefCount;
	if ( nRefCount <= 0 )
	{
		ShutdownStudioData( handle );
		m_MDLDict.RemoveAt( handle );
	}

	return nRefCount;
}

int CMDLCache::GetRef( MDLHandle_t handle )
{
	if ( !m_bInitialized )
		return 0;

	if ( !m_MDLDict[handle] )
		return 0;

	return m_MDLDict[handle]->m_nRefCount;
}

//-----------------------------------------------------------------------------
// Unserializes the PHY file associated w/ models (the vphysics representation)
//-----------------------------------------------------------------------------
void CStudioHdr::UnserializeVCollide( bool synchronousLoad ) const
{
	VPROF( "CMDLCache::UnserializeVCollide" );

	// FIXME: Should the vcollde be played into cacheable memory?
	//CStudioHdr *pStudioData = m_MDLDict[handle];

	intp iAsync = GetAsyncInfoIndex( m_handle, MDLCACHE_VCOLLIDE );

	if ( iAsync == NO_ASYNC )
	{
		// clear existing data
		m_nFlags &= ~STUDIODATA_FLAGS_VCOLLISION_LOADED;
		memset( &m_VCollisionData, 0, sizeof( m_VCollisionData ) );

#if 0
		// FIXME:  ywb
		// If we don't ask for the virtual model to load, then we can get a hitch later on after startup
		// Should we async load the sub .mdls during startup assuming they'll all be resident by the time the level can actually
		//  start drawing?
		if ( m_pVirtualModel || synchronousLoad )
#endif
		{
			CVirtualModel* pVirtualModel = &m_pVirtualModel;// GetVirtualModel(handle);
			if ( pVirtualModel )
			{
				for ( int i = 1; i < pVirtualModel->m_group.Count(); i++ )
				{
					//MDLHandle_t sharedHandle = VoidPtrToMDLHandle(pVirtualModel->m_group[i].cache);
					const CStudioHdr* pData = pVirtualModel->GroupStudioHdr(i);// m_MDLDict[sharedHandle];
					if ( !(pData->m_nFlags & STUDIODATA_FLAGS_VCOLLISION_LOADED) )
					{
						pData->UnserializeVCollide( synchronousLoad );
					}
					if ( pData->m_VCollisionData.solidCount > 0 )
					{
						m_VCollisionData = pData->m_VCollisionData;
						m_nFlags |= STUDIODATA_FLAGS_VCOLLISION_SHARED;
						return;
					}
				}
			}
		}

		char pFileName[MAX_PATH];
		m_pMDLCache->MakeFilename( m_handle, ".phy", pFileName, sizeof(pFileName) );
		if ( IsX360() )
		{
			char pX360Filename[MAX_PATH];
			m_pMDLCache->UpdateOrCreate( NULL, pFileName, pX360Filename, sizeof( pX360Filename ), "GAME" );
			Q_strncpy( pFileName, pX360Filename, sizeof(pX360Filename) );
		}

		bool bAsyncLoad = mod_load_vcollide_async.GetBool() && !synchronousLoad;

		MdlCacheMsg( "MDLCache: %s load vcollide %s\n", bAsyncLoad ? "Async" : "Sync", GetModelName() );

		AsyncInfo_t info;
		if ( IsDebug() )
		{
			memset( &info, 0xdd, sizeof( AsyncInfo_t ) );
		}
		info.hModel = m_handle;
		info.type = MDLCACHE_VCOLLIDE;
		info.iAnimBlock = 0;
		info.hControl = NULL;
		m_pMDLCache->LoadData( pFileName, "GAME", bAsyncLoad, &info.hControl );
		{
			AUTO_LOCK(m_pMDLCache->m_AsyncMutex );
			iAsync = SetAsyncInfoIndex( m_handle, MDLCACHE_VCOLLIDE, m_pMDLCache->m_PendingAsyncs.AddToTail( info ) );
		}
	}
	else if ( synchronousLoad )
	{
		AsyncInfo_t *pInfo;
		{
			AUTO_LOCK(m_pMDLCache->m_AsyncMutex );
			pInfo = &m_pMDLCache->m_PendingAsyncs[iAsync];
		}
		if ( pInfo->hControl )
		{
			g_pFullFileSystem->AsyncFinish( pInfo->hControl, true );
		}
	}

	m_pMDLCache->ProcessPendingAsync( iAsync );
}


//-----------------------------------------------------------------------------
// Free model's collision data
//-----------------------------------------------------------------------------
void CStudioHdr::DestroyVCollide()
{
	//CStudioHdr *pStudioData = m_MDLDict[handle];

	if ( m_nFlags & STUDIODATA_FLAGS_VCOLLISION_SHARED )
		return;

	if ( m_nFlags & STUDIODATA_FLAGS_VCOLLISION_LOADED )
	{
		m_nFlags &= ~STUDIODATA_FLAGS_VCOLLISION_LOADED;
		if ( m_VCollisionData.solidCount )
		{
			if ( m_pMDLCache->m_pCacheNotify )
			{
				m_pMDLCache->m_pCacheNotify->OnDataUnloaded( MDLCACHE_VCOLLIDE, m_handle );
			}

			MdlCacheMsg("MDLCache: Unload vcollide %s\n", GetModelName() );

			g_pPhysicsCollision->VCollideUnload( &m_VCollisionData );
		}
	}
}


//-----------------------------------------------------------------------------
// Unserializes the PHY file associated w/ models (the vphysics representation)
//-----------------------------------------------------------------------------
vcollide_t *CMDLCache::GetVCollideEx( MDLHandle_t handle, bool synchronousLoad /*= true*/ )
{
	if ( mod_test_not_available.GetBool() )
		return NULL;

	if ( handle == MDLHANDLE_INVALID )
		return NULL;

	CStudioHdr *pStudioData = GetStudioHdr(handle);

	if ( ( pStudioData->m_nFlags & STUDIODATA_FLAGS_VCOLLISION_LOADED ) == 0 )
	{
		pStudioData->UnserializeVCollide( synchronousLoad );
	}

	// We've loaded an empty collision file or no file was found, so return NULL
	if ( !pStudioData->m_VCollisionData.solidCount )
		return NULL;

	return &pStudioData->m_VCollisionData;
}


bool CMDLCache::GetVCollideSize( MDLHandle_t handle, int *pVCollideSize )
{
	*pVCollideSize = 0;

	CStudioHdr *pStudioData = GetStudioHdr(handle);
	if ( ( pStudioData->m_nFlags & STUDIODATA_FLAGS_VCOLLISION_LOADED ) == 0 )
		return false;

	vcollide_t *pCollide = &pStudioData->m_VCollisionData;
	for ( int j = 0; j < pCollide->solidCount; j++ )
	{
		*pVCollideSize += g_pPhysicsCollision->CollideSize( pCollide->solids[j] );
	}
	*pVCollideSize += pCollide->descSize;
	return true;
}

//-----------------------------------------------------------------------------
// Allocates/frees the anim blocks
//-----------------------------------------------------------------------------
void CStudioHdr::AllocateAnimBlocks(int nCount ) const
{
	Assert( m_pAnimBlock == NULL );

	m_nAnimBlockCount = nCount;
	m_pAnimBlock = new DataCacheHandle_t[m_nAnimBlockCount];

	memset( m_pAnimBlock, 0, sizeof(DataCacheHandle_t) * m_nAnimBlockCount );

	m_iFakeAnimBlockStall = new unsigned int [m_nAnimBlockCount];
	memset( m_iFakeAnimBlockStall, 0, sizeof( unsigned int ) * m_nAnimBlockCount );
}

void CStudioHdr::FreeAnimBlocks() const
{
	//CStudioHdr *pStudioData = m_MDLDict[handle];

	if ( m_pAnimBlock )
	{
		for (int i = 0; i < m_nAnimBlockCount; ++i )
		{
			MdlCacheMsg( "MDLCache: Free Anim block: %d\n", i );

			ClearAsync( MDLCACHE_ANIMBLOCK, i, true );
			if ( m_pAnimBlock[i] )
			{
				m_pMDLCache->UncacheData( m_pAnimBlock[i], MDLCACHE_ANIMBLOCK, true );
			}
		}

		delete[] m_pAnimBlock;
		m_pAnimBlock = NULL;

		delete[] m_iFakeAnimBlockStall;
		m_iFakeAnimBlockStall = NULL;
	}

	m_nAnimBlockCount = 0;
}


//-----------------------------------------------------------------------------
// Unserializes an animation block from disk
//-----------------------------------------------------------------------------
unsigned char *CStudioHdr::UnserializeAnimBlock( int nBlock ) const
{
	VPROF( "CMDLCache::UnserializeAnimBlock" );

	if ( IsX360() && g_pQueuedLoader->IsMapLoading() )
	{
		// anim block i/o is not allowed at this stage
		return NULL;
	}

	// Block 0 is never used!!!
	Assert( nBlock > 0 );

	//CStudioHdr *pStudioData = m_MDLDict[handle];

	intp iAsync = GetAsyncInfoIndex( m_handle, MDLCACHE_ANIMBLOCK, nBlock );

	if ( iAsync == NO_ASYNC )
	{
		//studiohdr_t *pStudioHdr = GetStudioHdrInternal( handle );

		// FIXME: For consistency, the block name maybe shouldn't have 'model' in it.
		char const *pModelName = m_pStudioHdr->pszAnimBlockName();
		mstudioanimblock_t *pBlock = m_pStudioHdr->pAnimBlock( nBlock );
		int nSize = pBlock->dataend - pBlock->datastart;
		if ( nSize == 0 )
			return NULL;

		// allocate space in the cache
		m_pAnimBlock[nBlock] = NULL;

		char pFileName[MAX_PATH];
		Q_strncpy( pFileName, pModelName, sizeof(pFileName) );
		Q_FixSlashes( pFileName );
#ifdef POSIX
		Q_strlower( pFileName );
#endif
		if ( IsX360() )
		{
			char pX360Filename[MAX_PATH];
			m_pMDLCache->UpdateOrCreate( m_pStudioHdr, pFileName, pX360Filename, sizeof( pX360Filename ), "GAME" );
			Q_strncpy( pFileName, pX360Filename, sizeof(pX360Filename) );
		}

		MdlCacheMsg( "MDLCache: Begin load Anim Block %s (block %i)\n", GetModelName(), nBlock );

		AsyncInfo_t info;
		if ( IsDebug() )
		{
			memset( &info, 0xdd, sizeof( AsyncInfo_t ) );
		}
		info.hModel = m_handle;
		info.type = MDLCACHE_ANIMBLOCK;
		info.iAnimBlock = nBlock;
		info.hControl = NULL;
		m_pMDLCache->LoadData( pFileName, "GAME", NULL, nSize, pBlock->datastart, mod_load_anims_async.GetBool(), &info.hControl );
		{
			AUTO_LOCK(m_pMDLCache->m_AsyncMutex );
			iAsync = SetAsyncInfoIndex( m_handle, MDLCACHE_ANIMBLOCK, nBlock, m_pMDLCache->m_PendingAsyncs.AddToTail( info ) );
		}
	}

	m_pMDLCache->ProcessPendingAsync( iAsync );

	return ( unsigned char * )m_pMDLCache->CheckData( m_pAnimBlock[nBlock], MDLCACHE_ANIMBLOCK );
}

//-----------------------------------------------------------------------------
// Gets at an animation block associated with an MDL
//-----------------------------------------------------------------------------
unsigned char *CMDLCache::GetAnimBlock( MDLHandle_t handle, int nBlock )
{
	if ( mod_test_not_available.GetBool() )
		return NULL;

	if ( handle == MDLHANDLE_INVALID )
		return NULL;

	// Allocate animation blocks if we don't have them yet
	CStudioHdr *pStudioData = GetStudioHdr(handle);

	// check for request being in range
	if ( nBlock < 0 || nBlock >= pStudioData->m_nAnimBlockCount)
		return NULL;

	// Check the cache to see if the animation is in memory
	unsigned char *pData = ( unsigned char * )CheckData( pStudioData->m_pAnimBlock[nBlock], MDLCACHE_ANIMBLOCK );
	if ( !pData )
	{
	
	}

	if (mod_load_fakestall.GetInt())
	{
		unsigned int t = Plat_MSTime();
		if (pStudioData->m_iFakeAnimBlockStall[nBlock] == 0 || pStudioData->m_iFakeAnimBlockStall[nBlock] > t)
		{
			pStudioData->m_iFakeAnimBlockStall[nBlock] = t;
		}

		if ((int)(t - pStudioData->m_iFakeAnimBlockStall[nBlock]) < mod_load_fakestall.GetInt())
		{
			return NULL;
		}
	}
	return pData;
}


//-----------------------------------------------------------------------------
// Allocates/frees autoplay sequence list
//-----------------------------------------------------------------------------
void CStudioHdr::AllocateAutoplaySequences( int nCount ) const
{
	FreeAutoplaySequences();

	m_nAutoplaySequenceCount = nCount;
	m_pAutoplaySequenceList = new unsigned short[nCount];
}

void CStudioHdr::FreeAutoplaySequences() const
{
	if ( m_pAutoplaySequenceList )
	{
		delete[] m_pAutoplaySequenceList;
		m_pAutoplaySequenceList = NULL;
	}

	m_nAutoplaySequenceCount = 0;
}


//-----------------------------------------------------------------------------
// Gets the autoplay list
//-----------------------------------------------------------------------------
int CMDLCache::GetAutoplayList( MDLHandle_t handle, unsigned short **pAutoplayList )
{
	if ( pAutoplayList )
	{
		*pAutoplayList = NULL;
	}

	if ( handle == MDLHANDLE_INVALID )
		return 0;

	CStudioHdr* pStudioData = GetStudioHdr(handle);

	CVirtualModel *pVirtualModel = GetVirtualModel( handle );
	if ( pVirtualModel )
	{
		if ( pAutoplayList && pVirtualModel->m_autoplaySequences.Count() )
		{
			*pAutoplayList = pVirtualModel->m_autoplaySequences.Base();
		}
		return pVirtualModel->m_autoplaySequences.Count();
	}

	// FIXME: Should we cache autoplay info here on demand instead of in unserializeMDL?
	if ( pAutoplayList )
	{
		*pAutoplayList = pStudioData->m_pAutoplaySequenceList;
	}

	return pStudioData->m_nAutoplaySequenceCount;
}


//-----------------------------------------------------------------------------
// Allocates/frees the virtual model
//-----------------------------------------------------------------------------
void CStudioHdr::AllocateVirtualModel() const
{
	//CStudioHdr *pStudioData = m_MDLDict[handle];
	//Assert( pStudioData->m_pVirtualModel == NULL );
	//pStudioData->m_pVirtualModel;// = new CVirtualModel;

	// FIXME: The old code slammed these; could have leaked memory?
	Assert( m_nAnimBlockCount == 0 );
	Assert( m_pAnimBlock == NULL );
}

void CStudioHdr::FreeVirtualModel() const
{
	//CStudioHdr *pStudioData = m_MDLDict[handle];
	if ( m_pVirtualModel.NumGroup()>0 )//pStudioData && pStudioData->
	{
		int nGroupCount = m_pVirtualModel.m_group.Count();
		Assert( (nGroupCount >= 1) && m_pVirtualModel.m_group[0].cache == MDLHandleToVirtual(m_handle) );

		// NOTE: Start at *1* here because the 0th element contains a reference to *this* handle
		for ( int i = 1; i < nGroupCount; ++i )
		{
			//MDLHandle_t h = VoidPtrToMDLHandle( m_pVirtualModel.m_group[i].cache );
			const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr(i);
			pStudioHdr->FreeVirtualModel();
			m_pMDLCache->Release(pStudioHdr->m_handle);
		}
		m_pVirtualModel.Clear();
	}
}


//-----------------------------------------------------------------------------
// Returns the virtual model
//-----------------------------------------------------------------------------
CVirtualModel *CMDLCache::GetVirtualModel( MDLHandle_t handle )
{
	if ( mod_test_not_available.GetBool() )
		return NULL;

	if ( handle == MDLHANDLE_INVALID )
		return NULL;

	CStudioHdr *pStudioHdr = GetStudioHdr( handle );

	if ( pStudioHdr == NULL )
		return NULL;

	return &pStudioHdr->m_pVirtualModel;
}

//CVirtualModel *CMDLCache::GetVirtualModelFast( const studiohdr_t *pStudioHdr, MDLHandle_t handle )
//{
//
//
//	return &pStudioData->m_pVirtualModel;
//}

//-----------------------------------------------------------------------------
// Purpose: Pulls all submodels/.ani file models into the cache
// to avoid runtime hitches and load animations at load time, set mod_forcedata to be 1
//-----------------------------------------------------------------------------
void CStudioHdr::UnserializeAllVirtualModelsAndAnimBlocks() const
{
	if ( m_handle == MDLHANDLE_INVALID )
		return;

	//CStudioHdr* pStudioData = m_MDLDict[handle];
	// might be re-loading, discard old virtualmodel to force rebuild
	// unfortunately, the virtualmodel does build data into the cacheable studiohdr
	FreeVirtualModel();

	if ( IsX360() && g_pQueuedLoader->IsMapLoading() )
	{
		// queued loading has to do it
		return;
	}

	// don't load the submodel data
	if ( !mod_forcedata.GetBool() )
		return;

	// if not present, will instance and load the submodels
	//m_pMDLCache->GetVirtualModel( m_handle );
	if (m_pStudioHdr->numincludemodels != 0) {
		if (m_pVirtualModel.NumGroup() == 0)
		{
			DevMsg(2, "Loading virtual model for %s\n", m_pStudioHdr->pszName());

			CMDLCacheCriticalSection criticalSection(m_pMDLCache);

			AllocateVirtualModel();

			// Group has to be zero to ensure refcounting is correct
			int nGroup = m_pVirtualModel.m_group.AddToTail();
			Assert(nGroup == 0);
			m_pVirtualModel.m_group[nGroup].cache = MDLHandleToVirtual(m_handle);

			// Add all dependent data
			m_pVirtualModel.AppendModels(0, this);
		}
	}

	if ( IsX360() )
	{
		// 360 does not drive the anims into its small cache section
		return;
	}

	FreeAnimBlocks();
	AllocateAnimBlocks(m_pStudioHdr->numanimblocks);
	// Note that the animblocks start at 1!!!
	//studiohdr_t *pStudioHdr = GetStudioHdrInternal( handle );
	for ( int i = 1 ; i < (int)m_pStudioHdr->numanimblocks; ++i )
	{
		//m_pMDLCache->GetAnimBlock( m_handle, i );
		m_pAnimBlock[i] = NULL;

		// It's not in memory, read it off of disk
		UnserializeAnimBlock(i);
	}

	m_pMDLCache->ProcessPendingAsyncs( MDLCACHE_ANIMBLOCK );
}


//-----------------------------------------------------------------------------
// Loads the static meshes
//-----------------------------------------------------------------------------
bool CStudioHdr::LoadHardwareData()
{
	Assert( m_handle != MDLHANDLE_INVALID );

	// Don't try to load VTX files if we don't have focus...
	if (m_pMDLCache->m_bLostVideoMemory )
		return false;

	//CStudioHdr *pStudioData = m_MDLDict[handle];

	CMDLCacheCriticalSection criticalSection(m_pMDLCache);

	// Load up the model
	//studiohdr_t *pStudioHdr = GetStudioHdrInternal( handle );
	if ( !m_pStudioHdr || !m_pStudioHdr->numbodyparts )
	{
		m_nFlags |= STUDIODATA_FLAGS_NO_STUDIOMESH;
		return true;
	}

	if ( m_nFlags & STUDIODATA_FLAGS_NO_STUDIOMESH )
	{
		return false;
	}

	if ( LogMdlCache() &&
		 GetAsyncInfoIndex( m_handle, MDLCACHE_STUDIOHWDATA ) == NO_ASYNC &&
		 GetAsyncInfoIndex( m_handle, MDLCACHE_VERTEXES ) == NO_ASYNC )
	{
		MdlCacheMsg( "MDLCache: Begin load studiomdl %s\n", GetModelName() );
	}

	// Vertex data is required to call LoadModel(), so make sure that's ready
	if ( !m_pMDLCache->GetVertexData( m_handle ) )
	{
		if ( m_nFlags & STUDIODATA_FLAGS_NO_VERTEX_DATA )
		{
			m_nFlags |= STUDIODATA_FLAGS_NO_STUDIOMESH;
		}
		return false;
	}

	intp iAsync = GetAsyncInfoIndex( m_handle, MDLCACHE_STUDIOHWDATA );

	if ( iAsync == NO_ASYNC )
	{
		m_pMDLCache->m_pMeshCacheSection->Lock( m_VertexCache );

		// load and persist the vtx file
		// use model name for correct path
		char pFileName[MAX_PATH];
		m_pMDLCache->MakeFilename( m_handle, m_pMDLCache->GetVTXExtension(), pFileName, sizeof(pFileName) );
		if ( IsX360() )
		{
			char pX360Filename[MAX_PATH];
			m_pMDLCache->UpdateOrCreate( m_pStudioHdr, pFileName, pX360Filename, sizeof( pX360Filename ), "GAME" );
			Q_strncpy( pFileName, pX360Filename, sizeof(pX360Filename) );
		}

		MdlCacheMsg("MDLCache: Begin load VTX %s\n", GetModelName() );

		AsyncInfo_t info;
		if ( IsDebug() )
		{
			memset( &info, 0xdd, sizeof( AsyncInfo_t ) );
		}
		info.hModel = m_handle;
		info.type = MDLCACHE_STUDIOHWDATA;
		info.iAnimBlock = 0;
		info.hControl = NULL;
		m_pMDLCache->LoadData( pFileName, "GAME", mod_load_mesh_async.GetBool(), &info.hControl );
		{
			AUTO_LOCK(m_pMDLCache->m_AsyncMutex );
			iAsync = SetAsyncInfoIndex( m_handle, MDLCACHE_STUDIOHWDATA, m_pMDLCache->m_PendingAsyncs.AddToTail( info ) );
		}
	}

	if (m_pMDLCache->ProcessPendingAsync( iAsync ) > 0 )
	{
		if ( m_nFlags & STUDIODATA_FLAGS_NO_STUDIOMESH )
		{
			return false;
		}

		return ( m_HardwareData.m_NumStudioMeshes != 0 );
	}

	return false;
}

void CStudioHdr::ConvertFlexData( studiohdr_t *pStudioHdr ) const
{
	float flVertAnimFixedPointScale = pStudioHdr->VertAnimFixedPointScale();

	for ( int i = 0; i < pStudioHdr->numbodyparts; i++ )
	{
		mstudiobodyparts_t *pBody = pStudioHdr->pBodypart( i );
		for ( int j = 0; j < pBody->nummodels; j++ )
		{
			mstudiomodel_t *pModel = pBody->pModel( j );
			for ( int k = 0; k < pModel->nummeshes; k++ )
			{
				mstudiomesh_t *pMesh = pModel->pMesh( k );
				for ( int l = 0; l < pMesh->numflexes; l++ )
				{
					mstudioflex_t *pFlex = pMesh->pFlex( l );
					bool bIsWrinkleAnim = ( pFlex->vertanimtype == STUDIO_VERT_ANIM_WRINKLE );
					for ( int m = 0; m < pFlex->numverts; m++ )
					{
						mstudiovertanim_t *pVAnim = bIsWrinkleAnim ?
							pFlex->pVertanimWrinkle( m ) : pFlex->pVertanim( m );
						pVAnim->ConvertToFixed( flVertAnimFixedPointScale );
					}
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CStudioHdr::BuildHardwareData(OptimizedModel::FileHeader_t *pVtxHdr ) const
{
	if ( pVtxHdr )
	{
		MdlCacheMsg("MDLCache: Alloc VTX %s\n", m_pStudioHdr->pszName() );

		// check header
		if ( pVtxHdr->version != OPTIMIZED_MODEL_FILE_VERSION )
		{
			Warning( "Error Index File for '%s' version %d should be %d\n", m_pStudioHdr->pszName(), pVtxHdr->version, OPTIMIZED_MODEL_FILE_VERSION );
			pVtxHdr = NULL;
		}
		else if ( pVtxHdr->checkSum != m_pStudioHdr->checksum )
		{
			Warning( "Error Index File for '%s' checksum %d should be %d\n", m_pStudioHdr->pszName(), pVtxHdr->checkSum, m_pStudioHdr->checksum );
			pVtxHdr = NULL;
		}
	}

	if ( !pVtxHdr )
	{
		m_nFlags |= STUDIODATA_FLAGS_NO_STUDIOMESH;
		return false;
	}

	CTempAllocHelper pOriginalData;
	if ( IsX360() )
	{
		unsigned char *pInputData = (unsigned char *)pVtxHdr + sizeof( OptimizedModel::FileHeader_t );
		if ( CLZMA::IsCompressed( pInputData ) )
		{
			// vtx arrives compressed, decode and cache the results
			unsigned int nOriginalSize = CLZMA::GetActualSize( pInputData );
			pOriginalData.Alloc( sizeof( OptimizedModel::FileHeader_t ) + nOriginalSize );
			V_memcpy( pOriginalData.Get(), pVtxHdr, sizeof( OptimizedModel::FileHeader_t ) );
			unsigned int nOutputSize = CLZMA::Uncompress( pInputData, sizeof( OptimizedModel::FileHeader_t ) + (unsigned char *)pOriginalData.Get() );
			if ( nOutputSize != nOriginalSize )
			{
				// decoder failure
				return false;
			}

			pVtxHdr = (OptimizedModel::FileHeader_t *)pOriginalData.Get();
		}
	}

	MdlCacheMsg( "MDLCache: Load studiomdl %s\n", m_pStudioHdr->pszName() );

	Assert( m_pMDLCache->GetVertexData( m_handle ) );

	if( m_pStudioHdr->version == 49 )
	{
		for( int i = 0; i < pVtxHdr->numBodyParts; i++)
		{
			OptimizedModel::BodyPartHeader_t *pBodyPartHdr = pVtxHdr->pBodyPart(i);

			for( int j = 0; j < pBodyPartHdr->numModels; j++ )
			{
				OptimizedModel::ModelHeader_t *pModelHdr = pBodyPartHdr->pModel(j);

				for( int k = 0; k < pModelHdr->numLODs; k++)
				{
					OptimizedModel::ModelLODHeader_t *pModelLODHdr = pModelHdr->pLOD(k);

					for( int l = 0; l < pModelLODHdr->numMeshes; l++ )
					{
						OptimizedModel::MeshHeader_t *pMeshHdr = pModelLODHdr->pMesh(l);
						pMeshHdr->flags |= OptimizedModel::MESH_IS_MDL49;

						for( int m = 0; m < pMeshHdr->numStripGroups; m++ )
						{
							OptimizedModel::StripGroupHeader_t *pStripGroupHdr = pMeshHdr->pStripGroup(m);
							pStripGroupHdr->flags |= OptimizedModel::STRIPGROUP_IS_MDL49;
						}
					}
				}
			}
		}
	}

	m_pMDLCache->BeginLock();
	bool bLoaded = g_pStudioRender->LoadModel( (IStudioHdr*)this, pVtxHdr, (studiohwdata_t*)&m_HardwareData);
	m_pMDLCache->EndLock();

	if ( bLoaded )
	{
		m_nFlags |= STUDIODATA_FLAGS_STUDIOMESH_LOADED;
	}
	else
	{
		m_nFlags |= STUDIODATA_FLAGS_NO_STUDIOMESH;
	}

	if (m_pMDLCache->m_pCacheNotify )
	{
		m_pMDLCache->m_pCacheNotify->OnDataLoaded( MDLCACHE_STUDIOHWDATA, m_handle );
	}

#if defined( USE_HARDWARE_CACHE )
	m_pMDLCache->GetCacheSection( MDLCACHE_STUDIOHWDATA )->Add( MakeCacheID( m_handle, MDLCACHE_STUDIOHWDATA ), &m_HardwareData, ComputeHardwareDataSize( &m_HardwareData ), &m_HardwareDataCache );
#endif
	return true;
}


//-----------------------------------------------------------------------------
// Loads the static meshes
//-----------------------------------------------------------------------------
void CStudioHdr::UnloadHardwareData( bool bCacheRemove, bool bLockedOk )
{
	if ( m_handle == MDLHANDLE_INVALID )
		return;

	// Don't load it if it's loaded
	//CStudioHdr *pStudioData = m_MDLDict[handle];
	if ( m_nFlags & STUDIODATA_FLAGS_STUDIOMESH_LOADED )
	{
#if defined( USE_HARDWARE_CACHE )
		if (bCacheRemove )
		{
			if (m_pMDLCache->GetCacheSection( MDLCACHE_STUDIOHWDATA )->BreakLock( m_HardwareDataCache ) && !bLockedOk )
			{
				DevMsg( "Warning: freed a locked resource\n" );
				Assert( 0 );
			}

			m_pMDLCache->GetCacheSection( MDLCACHE_STUDIOHWDATA )->Remove( m_HardwareDataCache );
		}
#endif

		if (m_pMDLCache->m_pCacheNotify )
		{
			m_pMDLCache->m_pCacheNotify->OnDataUnloaded( MDLCACHE_STUDIOHWDATA, m_handle );
		}

		MdlCacheMsg("MDLCache: Unload studiomdl %s\n", GetModelName() );

		g_pStudioRender->UnloadModel( &m_HardwareData );
		memset( &m_HardwareData, 0, sizeof( m_HardwareData ) );
		m_nFlags &= ~STUDIODATA_FLAGS_STUDIOMESH_LOADED;

		m_pMDLCache->NotifyFileUnloaded( m_handle, ".mdl" );

	}
}


//-----------------------------------------------------------------------------
// Returns the hardware data associated with an MDL
//-----------------------------------------------------------------------------
studiohwdata_t *CMDLCache::GetHardwareData( MDLHandle_t handle )
{
	if ( mod_test_not_available.GetBool() )
		return NULL;

	if ( mod_test_mesh_not_available.GetBool() )
		return NULL;

	CStudioHdr *pStudioData = GetStudioHdr(handle);
	m_pMeshCacheSection->LockMutex();
	if ( ( pStudioData->m_nFlags & (STUDIODATA_FLAGS_STUDIOMESH_LOADED | STUDIODATA_FLAGS_NO_STUDIOMESH) ) == 0 )
	{
		m_pMeshCacheSection->UnlockMutex();
		if ( !pStudioData->LoadHardwareData() )
		{
			return NULL;
		}
	}
	else
	{
#if defined( USE_HARDWARE_CACHE )
		CheckData( pStudioData->m_HardwareDataCache, MDLCACHE_STUDIOHWDATA );
#endif
		m_pMeshCacheSection->UnlockMutex();
	}

	// didn't load, don't return an empty pointer
	if ( pStudioData->m_nFlags & STUDIODATA_FLAGS_NO_STUDIOMESH )
		return NULL;

	return &pStudioData->m_HardwareData;
}


//-----------------------------------------------------------------------------
// Task switch
//-----------------------------------------------------------------------------
void CMDLCache::ReleaseMaterialSystemObjects()
{
	Assert( !m_bLostVideoMemory );
	m_bLostVideoMemory = true;

	BreakFrameLock( false );

	// Free all hardware data
	MDLHandle_t i = m_MDLDict.First();
	while ( i != m_MDLDict.InvalidIndex() )
	{
		m_MDLDict[i]->UnloadHardwareData( i );
		i = m_MDLDict.Next( i );
	}

	RestoreFrameLock();
}

void CMDLCache::RestoreMaterialSystemObjects( int nChangeFlags )
{
	Assert( m_bLostVideoMemory );
	m_bLostVideoMemory = false;

	BreakFrameLock( false );

	// Restore all hardware data
	MDLHandle_t i = m_MDLDict.First();
	while ( i != m_MDLDict.InvalidIndex() )
	{
		CStudioHdr *pStudioData = m_MDLDict[i];

		bool bIsMDLInMemory = GetCacheSection( MDLCACHE_STUDIOHDR )->IsPresent( pStudioData->m_MDLCache );

		// If the vertex format changed, we have to free the data because we may be using different .vtx files.
		if ( nChangeFlags & MATERIAL_RESTORE_VERTEX_FORMAT_CHANGED )
		{
			MdlCacheMsg( "MDLCache: Free studiohdr\n" );
			MdlCacheMsg( "MDLCache: Free VVD\n" );
			MdlCacheMsg( "MDLCache: Free VTX\n" );

			// FIXME: Do we have to free m_MDLCache + m_VertexCache?
			// Certainly we have to free m_IndexCache, cause that's a dx-level specific vtx file.
			pStudioData->ClearAsync( MDLCACHE_STUDIOHWDATA, 0, true );

			Flush( i, MDLCACHE_FLUSH_VERTEXES );
		}

		// Only restore the hardware data of those studiohdrs which are currently in memory
		if ( bIsMDLInMemory )
		{
			GetHardwareData( i );
		}

		i = m_MDLDict.Next( i );
	}

	RestoreFrameLock();
}


void CMDLCache::MarkAsLoaded(MDLHandle_t handle)
{
	if ( mod_lock_mdls_on_load.GetBool() )
	{
		g_MDLCache.GetStudioHdr(handle);
		if ( !( m_MDLDict[handle]->m_nFlags & STUDIODATA_FLAGS_LOCKED_MDL ) )
		{
			m_MDLDict[handle]->m_nFlags |= STUDIODATA_FLAGS_LOCKED_MDL;
			GetCacheSection( MDLCACHE_STUDIOHDR )->Lock( m_MDLDict[handle]->m_MDLCache );
		}
	}
}


//-----------------------------------------------------------------------------
// Callback for UpdateOrCreate utility function - swaps any studiomdl file type.
//-----------------------------------------------------------------------------
static bool MdlcacheCreateCallback( const char *pSourceName, const char *pTargetName, const char *pPathID, void *pHdr )
{
	// Missing studio files are permissible and not spewed as errors
	bool retval = false;
	CUtlBuffer sourceBuf;
	bool bOk = g_pFullFileSystem->ReadFile( pSourceName, NULL, sourceBuf );
	if ( bOk )
	{
		CUtlBuffer targetBuf;
		targetBuf.EnsureCapacity( sourceBuf.TellPut() + BYTESWAP_ALIGNMENT_PADDING );

		int bytes = StudioByteSwap::ByteswapStudioFile( pTargetName, targetBuf.Base(), sourceBuf.Base(), sourceBuf.TellPut(), (studiohdr_t*)pHdr );
		if ( bytes )
		{
			// If the file was an .mdl, attempt to swap the .ani as well
			if ( Q_stristr( pSourceName, ".mdl" ) )
			{
				char szANISourceName[ MAX_PATH ];
				Q_StripExtension( pSourceName, szANISourceName, sizeof( szANISourceName ) );
				Q_strncat( szANISourceName, ".ani", sizeof( szANISourceName ), COPY_ALL_CHARACTERS );
				UpdateOrCreate( szANISourceName, NULL, 0, pPathID, MdlcacheCreateCallback, true, targetBuf.Base() );
			}

			targetBuf.SeekPut( CUtlBuffer::SEEK_HEAD, bytes );
			g_pFullFileSystem->WriteFile( pTargetName, pPathID, targetBuf );
			retval = true;
		}
		else
		{
			Warning( "Failed to create %s\n", pTargetName );
		}
	}
	return retval;
}

//-----------------------------------------------------------------------------
// Calls utility function to create .360 version of a file.
//-----------------------------------------------------------------------------
int CMDLCache::UpdateOrCreate( studiohdr_t *pHdr, const char *pSourceName, char *pTargetName, int targetLen, const char *pPathID, bool bForce )
{
	return ::UpdateOrCreate( pSourceName, pTargetName, targetLen, pPathID, MdlcacheCreateCallback, bForce, pHdr );
}

//-----------------------------------------------------------------------------
// Purpose: Attempts to read a file native to the current platform
//-----------------------------------------------------------------------------
bool CMDLCache::ReadFileNative( char *pFileName, const char *pPath, CUtlBuffer &buf, int nMaxBytes, MDLCacheDataType_t type )
{
	bool bOk = false;

	if ( IsX360() )
	{
		// Read the 360 version
		char pX360Filename[ MAX_PATH ];
		UpdateOrCreate( NULL, pFileName, pX360Filename, sizeof( pX360Filename ), pPath );
		bOk = g_pFullFileSystem->ReadFile( pX360Filename, pPath, buf, nMaxBytes );
	}
	else
	{
		// Read the PC version
		bOk = g_pFullFileSystem->ReadFile( pFileName, pPath, buf, nMaxBytes );

		if( bOk && type == MDLCACHE_STUDIOHDR )
		{
			studiohdr_t* pStudioHdr = ( studiohdr_t* ) buf.PeekGet();

			if ( pStudioHdr->studiohdr2index == 0 )
			{
				// We always need this now, so make room for it in the buffer now.
				int bufferContentsEnd = buf.TellMaxPut();
				int maskBits = VALIGNOF( studiohdr2_t ) - 1;
				int offsetStudiohdr2 = ( bufferContentsEnd + maskBits ) & ~maskBits;
				int sizeIncrease = ( offsetStudiohdr2 - bufferContentsEnd )  + sizeof( studiohdr2_t );
				buf.SeekPut( CUtlBuffer::SEEK_CURRENT, sizeIncrease );

				// Re-get the pointer after resizing, because it has probably moved.
				pStudioHdr = ( studiohdr_t* ) buf.Base();
				studiohdr2_t* pStudioHdr2 = ( studiohdr2_t* ) ( ( byte * ) pStudioHdr + offsetStudiohdr2 );
				memset( pStudioHdr2, 0, sizeof( studiohdr2_t ) );
				pStudioHdr2->flMaxEyeDeflection = 0.866f; // Matches studio.h.

				pStudioHdr->studiohdr2index = offsetStudiohdr2;
				// Also make sure the structure knows about the extra bytes 
				// we've added so they get copied around.
				pStudioHdr->length += sizeIncrease;
			}
		}
	}

	return bOk;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
studiohdr_t *CStudioHdr::UnserializeMDL(void *pData, int nDataSize, bool bDataValid ) const
{
	if ( !bDataValid || nDataSize <= 0 || pData == NULL)
	{
		return NULL;
	}

	CTempAllocHelper pOriginalData;
	if ( IsX360() )
	{
		if ( CLZMA::IsCompressed( (unsigned char *)pData ) )
		{
			// mdl arrives compressed, decode and cache the results
			unsigned int nOriginalSize = CLZMA::GetActualSize( (unsigned char *)pData );
			pOriginalData.Alloc( nOriginalSize );
			unsigned int nOutputSize = CLZMA::Uncompress( (unsigned char *)pData, (unsigned char *)pOriginalData.Get() );
			if ( nOutputSize != nOriginalSize )
			{
				// decoder failure
				return NULL;
			}

			pData = pOriginalData.Get();
			nDataSize = nOriginalSize;
		}
	}

	studiohdr_t	*pStudioHdrIn = (studiohdr_t *)pData;

	if ( r_rootlod.GetInt() > 0 )
	{
		// raw data is already setup for lod 0, override otherwise
		Studio_SetRootLOD( pStudioHdrIn, r_rootlod.GetInt() );
	}

	// critical! store a back link to our data
	// this is fetched when re-establishing dependent cached data (vtx/vvd)
	pStudioHdrIn->SetVirtualModel( MDLHandleToVirtual( m_handle ) );

	MdlCacheMsg( "MDLCache: Alloc studiohdr %s\n", GetModelName() );

	// allocate cache space
	MemAlloc_PushAllocDbgInfo( "Models:StudioHdr", 0);
	studiohdr_t *pHdr = (studiohdr_t *)m_pMDLCache->AllocData( MDLCACHE_STUDIOHDR, pStudioHdrIn->length );
	MemAlloc_PopAllocDbgInfo();
	if ( !pHdr )
		return NULL;

	m_pMDLCache->CacheData( &m_MDLCache, pHdr, pStudioHdrIn->length, GetModelName(), MDLCACHE_STUDIOHDR, MakeCacheID( m_handle, MDLCACHE_STUDIOHDR) );

	if ( mod_lock_mdls_on_load.GetBool() )
	{
		m_pMDLCache->GetCacheSection( MDLCACHE_STUDIOHDR )->Lock( m_MDLCache );
		m_nFlags |= STUDIODATA_FLAGS_LOCKED_MDL;
	}

	// FIXME: Is there any way we can compute the size to load *before* loading in
	// and read directly into cache memory? It would be nice to reduce cache overhead here.
	// move the complete, relocatable model to the cache
	memcpy( pHdr, pStudioHdrIn, pStudioHdrIn->length );

	// On first load, convert the flex deltas from fp16 to 16-bit fixed-point
	if ( (pHdr->flags & STUDIOHDR_FLAGS_FLEXES_CONVERTED) == 0 )
	{
		ConvertFlexData( pHdr );

		// Mark as converted so it only happens once
		pHdr->flags |= STUDIOHDR_FLAGS_FLEXES_CONVERTED;
	}

	Init(pHdr);

	if (!Studio_ConvertStudioHdrToNewVersion(pHdr))
	{
		Warning("MDLCache: %s needs to be recompiled\n", pHdr->pszName());
	}

	if (numincludemodels() == 0)
	{
		// perf optimization, calculate once and cache off the autoplay sequences
		int nCount = CountAutoplaySequences();
		if (nCount)
		{
			AllocateAutoplaySequences(nCount);
			CopyAutoplaySequences(m_pAutoplaySequenceList, nCount);
		}
	}

	// Load animations
	UnserializeAllVirtualModelsAndAnimBlocks();

	if (m_pMDLCache->m_pCacheNotify)
	{
		m_pMDLCache->m_pCacheNotify->OnDataLoaded(MDLCACHE_STUDIOHDR, m_handle);
	}

	return pHdr;
}


//-----------------------------------------------------------------------------
// Attempts to load a MDL file, validates that it's ok.
//-----------------------------------------------------------------------------
bool CStudioHdr::ReadMDLFile( const char *pMDLFileName, CUtlBuffer &buf ) const
{
	VPROF( "CMDLCache::ReadMDLFile" );

	char pFileName[ MAX_PATH ];
	Q_strncpy( pFileName, pMDLFileName, sizeof( pFileName ) );
	Q_FixSlashes( pFileName );
#ifdef POSIX
	Q_strlower( pFileName );
#endif

	MdlCacheMsg( "MDLCache: Load studiohdr %s\n", pFileName );

	MEM_ALLOC_CREDIT();

	bool bOk = m_pMDLCache->ReadFileNative( pFileName, "GAME", buf, 0, MDLCACHE_STUDIOHDR );
	if ( !bOk )
	{
		DevWarning( "Failed to load %s!\n", pMDLFileName );
		return false;
	}

	if ( IsX360() )
	{
		if ( CLZMA::IsCompressed( (unsigned char *)buf.PeekGet() ) )
		{
			// mdl arrives compressed, decode and cache the results
			unsigned int nOriginalSize = CLZMA::GetActualSize( (unsigned char *)buf.PeekGet() );
			void *pOriginalData = malloc( nOriginalSize );
			unsigned int nOutputSize = CLZMA::Uncompress( (unsigned char *)buf.PeekGet(), (unsigned char *)pOriginalData );
			if ( nOutputSize != nOriginalSize )
			{
				// decoder failure
				free( pOriginalData );
				return false;
			}

			// replace caller's buffer
			buf.Purge();
			buf.Put( pOriginalData, nOriginalSize );
			free( pOriginalData );
		}
	}

    if ( buf.Size() < sizeof(studiohdr_t) )
    {
        DevWarning( "Empty model %s\n", pMDLFileName );
        return false;
    }

	studiohdr_t *pStudioHdr = (studiohdr_t*)buf.PeekGet();
	if ( !pStudioHdr )
	{
		DevWarning( "Failed to read model %s from buffer!\n", pMDLFileName );
		return false;
	}
	if ( pStudioHdr->id != IDSTUDIOHEADER )
	{
		DevWarning( "Model %s not a .MDL format file!\n", pMDLFileName );
		return false;
	}

	// critical! store a back link to our data
	// this is fetched when re-establishing dependent cached data (vtx/vvd)
	pStudioHdr->SetVirtualModel( MDLHandleToVirtual( m_handle ) );

	// Make sure all dependent files are valid
	if ( !m_pMDLCache->VerifyHeaders( pStudioHdr ) )
	{
		DevWarning( "Model %s has mismatched .vvd + .vtx files!\n", pMDLFileName );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
IStudioHdr *CMDLCache::LockStudioHdr( MDLHandle_t handle )
{
	if ( handle == MDLHANDLE_INVALID )
	{
		return NULL;
	}

	CMDLCacheCriticalSection cacheCriticalSection( this );
	CStudioHdr *pStdioHdr = GetStudioHdr( handle );
	// @TODO (toml 9/12/2006) need this?: AddRef( handle );
	if ( !pStdioHdr )
	{
		return NULL;
	}

	GetCacheSection( MDLCACHE_STUDIOHDR )->Lock( m_MDLDict[handle]->m_MDLCache );
	return GetStudioHdr(handle);
}

void CMDLCache::UnlockStudioHdr( MDLHandle_t handle )
{
	if ( handle == MDLHANDLE_INVALID )
	{
		return;
	}

	CMDLCacheCriticalSection cacheCriticalSection( this );
	CStudioHdr *pStdioHdr = GetStudioHdr( handle );
	if ( pStdioHdr )
	{
		GetCacheSection( MDLCACHE_STUDIOHDR )->Unlock( m_MDLDict[handle]->m_MDLCache );
	}
	// @TODO (toml 9/12/2006) need this?: Release( handle );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

CStudioHdr::CStudioHdr(CMDLCache* pMDLCache, MDLHandle_t handle, const char* pModelName)
:m_pMDLCache(pMDLCache), m_handle(handle), m_pModelName(pModelName)
{
	memset(&m_VCollisionData, 0, sizeof(vcollide_t));
	memset(&m_HardwareData, 0, sizeof(studiohwdata_t));
#if defined( USE_HARDWARE_CACHE )
	memset(&m_HardwareDataCache, 0, sizeof(DataCacheHandle_t));
#endif
	m_pVirtualModel.Init(this);
	// set pointer to bogus value
	//m_nFrameUnlockCounter = 0;
	//m_pFrameUnlockCounter = &m_nFrameUnlockCounter;
	Init(NULL);
}

//CStudioHdr::CStudioHdr(studiohdr_t* pStudioHdr, IMDLCache* mdlcache)
//{
//	// preset pointer to bogus value (it may be overwritten with legitimate data later)
//	//m_nFrameUnlockCounter = 0;
//	//m_pFrameUnlockCounter = &m_nFrameUnlockCounter;
//	Init(pStudioHdr, mdlcache);
//}


// extern IDataCache *g_pDataCache;

void CStudioHdr::Init(studiohdr_t* pStudioHdr) const
{
	m_pStudioHdr = pStudioHdr;

	//(&m_pVirtualModel) = NULL;

	if (m_pStudioHdr == NULL)
	{
		return;
	}

	if (m_pStudioHdr->numincludemodels == 0)
	{
#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
#else
		m_ActivityToSequence.Initialize(this);
#endif
	}
	else
	{
		//ResetVModel(m_pStudioHdr->GetVirtualModel());
#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
#else
		m_ActivityToSequence.Initialize(this);
#endif
	}

	m_boneFlags.EnsureCount(numbones());
	m_boneParent.EnsureCount(numbones());
	for (int i = 0; i < numbones(); i++)
	{
		m_boneFlags[i] = pBone(i)->flags;
		m_boneParent[i] = pBone(i)->parent;
	}
}

void CStudioHdr::Term()
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

bool CStudioHdr::SequencesAvailable() const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return true;
	}

	if (m_pStudioHdr->numincludemodels == 0)
	{
		// repoll (&m_pVirtualModel)
		return true;// (ResetVModel(m_pStudioHdr->GetVirtualModel()) != NULL);
	}
	else
		return true;
}


//const CVirtualModel* CStudioHdr::ResetVModel(const CVirtualModel* pVModel) const
//{
//	if (pVModel != NULL)
//	{
//		if (pVModel != &m_pVirtualModel) {
//			Error("can not happen");
//		}
//		//(&m_pVirtualModel) = (CVirtualModel*)pVModel;
//		Assert(!pVModel->m_Lock.GetOwnerId());
//		m_pStudioHdrCache.SetCount(pVModel->m_group.Count());
//
//		int i;
//		for (i = 0; i < m_pStudioHdrCache.Count(); i++)
//		{
//			m_pStudioHdrCache[i] = NULL;
//		}
//
//		return const_cast<CVirtualModel*>(pVModel);
//	}
//	else
//	{
//		//(&m_pVirtualModel) = NULL;
//		return NULL;
//	}
//}

const CStudioHdr* CVirtualModel::GroupStudioHdr(int i) const
{
	if (!this)
	{
		ExecuteNTimes(5, Warning("Call to NULL CVirtualModel::GroupStudioHdr()\n"));
	}

	if (m_nFrameUnlockCounter != *m_pFrameUnlockCounter)
	{
		m_FrameUnlockCounterMutex.Lock();
		if (*m_pFrameUnlockCounter != m_nFrameUnlockCounter) // i.e., this thread got the mutex
		{
			memset(m_pStudioHdrCache.Base(), 0, m_pStudioHdrCache.Count() * sizeof(CStudioHdr*));
			m_nFrameUnlockCounter = *m_pFrameUnlockCounter;
		}
		m_FrameUnlockCounterMutex.Unlock();
	}

	if (!m_pStudioHdrCache.IsValidIndex(i))
	{
		const char* pszName = (m_pStudioHdr) ? m_pStudioHdr->pszName() : "<<null>>";
		ExecuteNTimes(5, Warning("Invalid index passed to CStudioHdr(%s)::GroupStudioHdr(): %d, but max is %d\n", pszName, i, m_pStudioHdrCache.Count()));
		DebuggerBreakIfDebugging();
		return m_pStudioHdr;// m_pStudioHdr; // return something known to probably exist, certainly things will be messed up, but hopefully not crash before the warning is noticed
	}

	CStudioHdr* pStudioHdr = m_pStudioHdrCache[i];

	if (pStudioHdr == NULL)
	{
		Assert(!m_Lock.GetOwnerId());
		const virtualgroup_t* pGroup = &m_group[i];
		pStudioHdr = g_MDLCache.GetStudioHdr(VoidPtrToMDLHandle(pGroup->cache));
		m_pStudioHdrCache[i] = pStudioHdr;
	}

	Assert(pStudioHdr);
	return pStudioHdr;
}


const IStudioHdr* CStudioHdr::pSeqStudioHdr(int sequence) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return this;
	}

	const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr((&m_pVirtualModel)->m_seq[sequence].group);

	return pStudioHdr;
}


const IStudioHdr* CStudioHdr::pAnimStudioHdr(int animation) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return this;
	}

	const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr((&m_pVirtualModel)->m_anim[animation].group);

	return pStudioHdr;
}

const IStudioHdr* CStudioHdr::RealStudioHdr(studiohdr_t* pStudioHdr) const {
	CStudioHdr* studioHdr = g_MDLCache.GetStudioHdr(VoidPtrToMDLHandle(pStudioHdr->VirtualModel()));
	if (studioHdr->m_pStudioHdr != pStudioHdr) {
		Error("error in RealStudioHdr");
	}
	return studioHdr;
}

mstudioanimdesc_t& CStudioHdr::pAnimdesc(int i)
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return *m_pStudioHdr->pLocalAnimdesc(i);
	}

	const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr((&m_pVirtualModel)->m_anim[i].group);

	return *pStudioHdr->pLocalAnimdesc((&m_pVirtualModel)->m_anim[i].index);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::GetNumSeq(void) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return m_pStudioHdr->numlocalseq;
	}

	return (&m_pVirtualModel)->m_seq.Count();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

mstudioseqdesc_t& CStudioHdr::pSeqdesc(int i) const
{
	Assert((i >= 0 && i < GetNumSeq()) || (i == 1 && GetNumSeq() <= 1));
	if (i < 0 || i >= GetNumSeq())
	{
		if (GetNumSeq() <= 0)
		{
			// Return a zero'd out struct reference if we've got nothing.
			// C_BaseObject::StopAnimGeneratedSounds was crashing due to this function
			//	returning a reference to garbage. It should now see numevents is 0,
			//	and bail.
			static mstudioseqdesc_t s_nil_seq;
			return s_nil_seq;
		}

		// Avoid reading random memory.
		i = 0;
	}

	if (m_pStudioHdr->numincludemodels == 0)
	{
		return *m_pStudioHdr->pLocalSeqdesc(i);
	}

	const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr((&m_pVirtualModel)->m_seq[i].group);

	return *pStudioHdr->pLocalSeqdesc((&m_pVirtualModel)->m_seq[i].index);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::iRelativeAnim(int baseseq, int relanim) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return relanim;
	}

	virtualgroup_t* pGroup = &(&m_pVirtualModel)->m_group[(&m_pVirtualModel)->m_seq[baseseq].group];

	return pGroup->masterAnim[relanim];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::iRelativeSeq(int baseseq, int relseq) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return relseq;
	}

	Assert(m_pStudioHdr->numincludemodels != 0);

	virtualgroup_t* pGroup = &(&m_pVirtualModel)->m_group[(&m_pVirtualModel)->m_seq[baseseq].group];

	return pGroup->masterSeq[relseq];
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int	CStudioHdr::GetNumPoseParameters(void) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		if (m_pStudioHdr)
			return m_pStudioHdr->numlocalposeparameters;
		else
			return 0;
	}

	Assert(m_pStudioHdr->numincludemodels != 0);

	return (&m_pVirtualModel)->m_pose.Count();
}



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

const mstudioposeparamdesc_t& CStudioHdr::pPoseParameter(int i)
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return *m_pStudioHdr->pLocalPoseParameter(i);
	}

	if ((&m_pVirtualModel)->m_pose[i].group == 0)
		return *m_pStudioHdr->pLocalPoseParameter((&m_pVirtualModel)->m_pose[i].index);

	const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr((&m_pVirtualModel)->m_pose[i].group);

	return *pStudioHdr->pLocalPoseParameter((&m_pVirtualModel)->m_pose[i].index);
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::GetSharedPoseParameter(int iSequence, int iLocalPose) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return iLocalPose;
	}

	if (iLocalPose == -1)
		return iLocalPose;

	Assert(m_pStudioHdr->numincludemodels != 0);

	int group = (&m_pVirtualModel)->m_seq[iSequence].group;
	virtualgroup_t* pGroup = (&m_pVirtualModel)->m_group.IsValidIndex(group) ? &(&m_pVirtualModel)->m_group[group] : NULL;

	return pGroup ? pGroup->masterPose[iLocalPose] : iLocalPose;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::EntryNode(int iSequence)
{
	mstudioseqdesc_t& seqdesc = pSeqdesc(iSequence);

	if (m_pStudioHdr->numincludemodels == 0 || seqdesc.localentrynode == 0)
	{
		return seqdesc.localentrynode;
	}

	Assert(m_pStudioHdr->numincludemodels != 0);

	virtualgroup_t* pGroup = &(&m_pVirtualModel)->m_group[(&m_pVirtualModel)->m_seq[iSequence].group];

	return pGroup->masterNode[seqdesc.localentrynode - 1] + 1;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


int CStudioHdr::ExitNode(int iSequence)
{
	mstudioseqdesc_t& seqdesc = pSeqdesc(iSequence);

	if (m_pStudioHdr->numincludemodels == 0 || seqdesc.localexitnode == 0)
	{
		return seqdesc.localexitnode;
	}

	Assert(m_pStudioHdr->numincludemodels != 0);

	virtualgroup_t* pGroup = &(&m_pVirtualModel)->m_group[(&m_pVirtualModel)->m_seq[iSequence].group];

	return pGroup->masterNode[seqdesc.localexitnode - 1] + 1;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int	CStudioHdr::GetNumAttachments(void) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return m_pStudioHdr->numlocalattachments;
	}

	Assert(m_pStudioHdr->numincludemodels != 0);

	return (&m_pVirtualModel)->m_attachment.Count();
}



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

const mstudioattachment_t& CStudioHdr::pAttachment(int i)
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return *m_pStudioHdr->pLocalAttachment(i);
	}

	Assert(m_pStudioHdr->numincludemodels != 0);

	const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr((&m_pVirtualModel)->m_attachment[i].group);

	return *pStudioHdr->pLocalAttachment((&m_pVirtualModel)->m_attachment[i].index);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int	CStudioHdr::GetAttachmentBone(int i)
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return m_pStudioHdr->pLocalAttachment(i)->localbone;
	}

	virtualgroup_t* pGroup = &(&m_pVirtualModel)->m_group[(&m_pVirtualModel)->m_attachment[i].group];
	const mstudioattachment_t& attachment = pAttachment(i);
	int iBone = pGroup->masterBone[attachment.localbone];
	if (iBone == -1)
		return 0;
	return iBone;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void CStudioHdr::SetAttachmentBone(int iAttachment, int iBone)
{
	mstudioattachment_t& attachment = (mstudioattachment_t&)pAttachment(iAttachment);//m_pStudioHdr->

	// remap bone
	if (m_pStudioHdr->numincludemodels != 0)
	{
		virtualgroup_t* pGroup = &(&m_pVirtualModel)->m_group[(&m_pVirtualModel)->m_attachment[iAttachment].group];
		iBone = pGroup->boneMap[iBone];
	}
	attachment.localbone = iBone;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

char* CStudioHdr::pszNodeName(int iNode)
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return m_pStudioHdr->pszLocalNodeName(iNode);
	}

	if ((&m_pVirtualModel)->m_node.Count() <= iNode - 1)
		return "Invalid node";

	const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr((&m_pVirtualModel)->m_node[iNode - 1].group);

	return pStudioHdr->pszLocalNodeName((&m_pVirtualModel)->m_node[iNode - 1].index);
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::GetTransition(int iFrom, int iTo) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return *m_pStudioHdr->pLocalTransition((iFrom - 1) * m_pStudioHdr->numlocalnodes + (iTo - 1));
	}

	return iTo;
	/*
	FIXME: not connected
	CVirtualModel *pVModel = (CVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	return pVModel->m_transition.Element( iFrom ).Element( iTo );
	*/
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int	CStudioHdr::GetActivityListVersion(void)
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return m_pStudioHdr->activitylistversion;
	}

	int version = m_pStudioHdr->activitylistversion;

	int i;
	for (i = 1; i < (&m_pVirtualModel)->m_group.Count(); i++)
	{
		const studiohdr_t* pStudioHdr = m_pVirtualModel.GroupStudioHdr(i)->m_pStudioHdr;
		Assert(pStudioHdr);
		version = min(version, pStudioHdr->activitylistversion);
	}

	return version;
}

void CStudioHdr::SetActivityListVersion(int version) const
{
	m_pStudioHdr->activitylistversion = version;

	if (m_pStudioHdr->numincludemodels == 0)
	{
		return;
	}

	int i;
	for (i = 1; i < (&m_pVirtualModel)->m_group.Count(); i++)
	{
		const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr(i);
		Assert(pStudioHdr);
		pStudioHdr->SetActivityListVersion(version);
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int	CStudioHdr::GetEventListVersion(void)
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return m_pStudioHdr->eventsindexed;
	}

	int version = m_pStudioHdr->eventsindexed;

	int i;
	for (i = 1; i < (&m_pVirtualModel)->m_group.Count(); i++)
	{
		const studiohdr_t* pStudioHdr = m_pVirtualModel.GroupStudioHdr(i)->m_pStudioHdr;
		Assert(pStudioHdr);
		version = min(version, pStudioHdr->eventsindexed);
	}

	return version;
}

void CStudioHdr::SetEventListVersion(int version)
{
	m_pStudioHdr->eventsindexed = version;

	if (m_pStudioHdr->numincludemodels == 0)
	{
		return;
	}

	int i;
	for (i = 1; i < (&m_pVirtualModel)->m_group.Count(); i++)
	{
		const studiohdr_t* pStudioHdr = m_pVirtualModel.GroupStudioHdr(i)->m_pStudioHdr;
		Assert(pStudioHdr);
		pStudioHdr->eventsindexed = version;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


int CStudioHdr::GetNumIKAutoplayLocks(void) const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return m_pStudioHdr->numlocalikautoplaylocks;
	}

	return (&m_pVirtualModel)->m_iklock.Count();
}

const mstudioiklock_t& CStudioHdr::pIKAutoplayLock(int i)
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return *m_pStudioHdr->pLocalIKAutoplayLock(i);
	}

	const CStudioHdr* pStudioHdr = m_pVirtualModel.GroupStudioHdr((&m_pVirtualModel)->m_iklock[i].group);
	Assert(pStudioHdr);
	return *pStudioHdr->pLocalIKAutoplayLock((&m_pVirtualModel)->m_iklock[i].index);
}


int	CStudioHdr::CountAutoplaySequences() const
{
	int count = 0;
	for (int i = 0; i < GetNumSeq(); i++)
	{
		mstudioseqdesc_t& seqdesc = pSeqdesc(i);
		if (seqdesc.flags & STUDIO_AUTOPLAY)
		{
			count++;
		}
	}
	return count;
}

int	CStudioHdr::CopyAutoplaySequences(unsigned short* pOut, int outCount) const
{
	int outIndex = 0;
	for (int i = 0; i < GetNumSeq() && outIndex < outCount; i++)
	{
		mstudioseqdesc_t& seqdesc = pSeqdesc(i);
		if (seqdesc.flags & STUDIO_AUTOPLAY)
		{
			pOut[outIndex] = i;
			outIndex++;
		}
	}
	return outIndex;
}

//-----------------------------------------------------------------------------
// Purpose:	maps local sequence bone to global bone
//-----------------------------------------------------------------------------

int	CStudioHdr::RemapSeqBone(int iSequence, int iLocalBone) const
{
	// remap bone
	if (m_pStudioHdr->numincludemodels != 0)
	{
		const virtualgroup_t* pSeqGroup = (&m_pVirtualModel)->pSeqGroup(iSequence);
		return pSeqGroup->masterBone[iLocalBone];
	}
	return iLocalBone;
}

int	CStudioHdr::RemapAnimBone(int iAnim, int iLocalBone) const
{
	// remap bone
	if (m_pStudioHdr->numincludemodels != 0)
	{
		const virtualgroup_t* pAnimGroup = (&m_pVirtualModel)->pAnimGroup(iAnim);
		return pAnimGroup->masterBone[iLocalBone];
	}
	return iLocalBone;
}

// JasonM hack
//ConVar	flex_maxrule( "flex_maxrule", "0" );


//-----------------------------------------------------------------------------
// Purpose: run the interpreted FAC's expressions, converting flex_controller 
//			values into FAC weights
//-----------------------------------------------------------------------------
void CStudioHdr::RunFlexRules(const float* src, float* dest)
{

	// FIXME: this shouldn't be needed, flex without rules should be stripped in studiomdl
	for (int i = 0; i < numflexdesc(); i++)
	{
		dest[i] = 0;
	}

	for (int i = 0; i < numflexrules(); i++)
	{
		float stack[32] = {};
		int k = 0;
		mstudioflexrule_t* prule = pFlexRule(i);

		mstudioflexop_t* pops = prule->iFlexOp(0);
		/*
				// JasonM hack for flex perf testing...
				int nFlexRulesToRun = 0;								// 0 means run them all
				const char *pszExpression = flex_maxrule.GetString();
				if ( pszExpression )
				{
					nFlexRulesToRun = atoi(pszExpression);				// 0 will be returned if not a numeric string
				}
				// end JasonM hack
		//*/
		// debugoverlay->AddTextOverlay( GetAbsOrigin() + Vector( 0, 0, 64 ), i + 1, 0, "%2d:%d\n", i, prule->flex );

		for (int j = 0; j < prule->numops; j++)
		{
			switch (pops->op)
			{
			case STUDIO_ADD: stack[k - 2] = stack[k - 2] + stack[k - 1]; k--; break;
			case STUDIO_SUB: stack[k - 2] = stack[k - 2] - stack[k - 1]; k--; break;
			case STUDIO_MUL: stack[k - 2] = stack[k - 2] * stack[k - 1]; k--; break;
			case STUDIO_DIV:
				if (stack[k - 1] > 0.0001)
				{
					stack[k - 2] = stack[k - 2] / stack[k - 1];
				}
				else
				{
					stack[k - 2] = 0;
				}
				k--;
				break;
			case STUDIO_NEG: stack[k - 1] = -stack[k - 1]; break;
			case STUDIO_MAX: stack[k - 2] = max(stack[k - 2], stack[k - 1]); k--; break;
			case STUDIO_MIN: stack[k - 2] = min(stack[k - 2], stack[k - 1]); k--; break;
			case STUDIO_CONST: stack[k] = pops->d.value; k++; break;
			case STUDIO_FETCH1:
			{
				int m = pFlexcontroller((LocalFlexController_t)pops->d.index)->localToGlobal;
				stack[k] = src[m];
				k++;
				break;
			}
			case STUDIO_FETCH2:
			{
				stack[k] = dest[pops->d.index]; k++; break;
			}
			case STUDIO_COMBO:
			{
				int m = pops->d.index;
				int km = k - m;
				for (int iStack = km + 1; iStack < k; ++iStack)
				{
					stack[km] *= stack[iStack];
				}
				k = k - m + 1;
			}
			break;
			case STUDIO_DOMINATE:
			{
				int m = pops->d.index;
				int km = k - m;
				float dv = stack[km];
				for (int iStack = km + 1; iStack < k; ++iStack)
				{
					dv *= stack[iStack];
				}
				stack[km - 1] *= 1.0f - dv;
				k -= m;
			}
			break;
			case STUDIO_2WAY_0:
			{
				int m = pFlexcontroller((LocalFlexController_t)pops->d.index)->localToGlobal;
				stack[k] = RemapValClamped(src[m], -1.0f, 0.0f, 1.0f, 0.0f);
				k++;
			}
			break;
			case STUDIO_2WAY_1:
			{
				int m = pFlexcontroller((LocalFlexController_t)pops->d.index)->localToGlobal;
				stack[k] = RemapValClamped(src[m], 0.0f, 1.0f, 0.0f, 1.0f);
				k++;
			}
			break;
			case STUDIO_NWAY:
			{
				LocalFlexController_t valueControllerIndex = static_cast<LocalFlexController_t>((int)stack[k - 1]);
				int m = pFlexcontroller(valueControllerIndex)->localToGlobal;
				float flValue = src[m];
				int v = pFlexcontroller((LocalFlexController_t)pops->d.index)->localToGlobal;

				const Vector4D filterRamp(stack[k - 5], stack[k - 4], stack[k - 3], stack[k - 2]);

				// Apply multicontrol remapping
				if (flValue <= filterRamp.x || flValue >= filterRamp.w)
				{
					flValue = 0.0f;
				}
				else if (flValue < filterRamp.y)
				{
					flValue = RemapValClamped(flValue, filterRamp.x, filterRamp.y, 0.0f, 1.0f);
				}
				else if (flValue > filterRamp.z)
				{
					flValue = RemapValClamped(flValue, filterRamp.z, filterRamp.w, 1.0f, 0.0f);
				}
				else
				{
					flValue = 1.0f;
				}

				stack[k - 5] = flValue * src[v];

				k -= 4;
			}
			break;
			case STUDIO_DME_LOWER_EYELID:
			{
				const mstudioflexcontroller_t* const pCloseLidV = pFlexcontroller((LocalFlexController_t)pops->d.index);
				const float flCloseLidV = RemapValClamped(src[pCloseLidV->localToGlobal], pCloseLidV->min, pCloseLidV->max, 0.0f, 1.0f);

				const mstudioflexcontroller_t* const pCloseLid = pFlexcontroller(static_cast<LocalFlexController_t>((int)stack[k - 1]));
				const float flCloseLid = RemapValClamped(src[pCloseLid->localToGlobal], pCloseLid->min, pCloseLid->max, 0.0f, 1.0f);

				int nBlinkIndex = static_cast<int>(stack[k - 2]);
				float flBlink = 0.0f;
				if (nBlinkIndex >= 0)
				{
					const mstudioflexcontroller_t* const pBlink = pFlexcontroller(static_cast<LocalFlexController_t>((int)stack[k - 2]));
					flBlink = RemapValClamped(src[pBlink->localToGlobal], pBlink->min, pBlink->max, 0.0f, 1.0f);
				}

				int nEyeUpDownIndex = static_cast<int>(stack[k - 3]);
				float flEyeUpDown = 0.0f;
				if (nEyeUpDownIndex >= 0)
				{
					const mstudioflexcontroller_t* const pEyeUpDown = pFlexcontroller(static_cast<LocalFlexController_t>((int)stack[k - 3]));
					flEyeUpDown = RemapValClamped(src[pEyeUpDown->localToGlobal], pEyeUpDown->min, pEyeUpDown->max, -1.0f, 1.0f);
				}

				if (flEyeUpDown > 0.0)
				{
					stack[k - 3] = (1.0f - flEyeUpDown) * (1.0f - flCloseLidV) * flCloseLid;
				}
				else
				{
					stack[k - 3] = (1.0f - flCloseLidV) * flCloseLid;
				}
				k -= 2;
			}
			break;
			case STUDIO_DME_UPPER_EYELID:
			{
				const mstudioflexcontroller_t* const pCloseLidV = pFlexcontroller((LocalFlexController_t)pops->d.index);
				const float flCloseLidV = RemapValClamped(src[pCloseLidV->localToGlobal], pCloseLidV->min, pCloseLidV->max, 0.0f, 1.0f);

				const mstudioflexcontroller_t* const pCloseLid = pFlexcontroller(static_cast<LocalFlexController_t>((int)stack[k - 1]));
				const float flCloseLid = RemapValClamped(src[pCloseLid->localToGlobal], pCloseLid->min, pCloseLid->max, 0.0f, 1.0f);

				int nBlinkIndex = static_cast<int>(stack[k - 2]);
				float flBlink = 0.0f;
				if (nBlinkIndex >= 0)
				{
					const mstudioflexcontroller_t* const pBlink = pFlexcontroller(static_cast<LocalFlexController_t>((int)stack[k - 2]));
					flBlink = RemapValClamped(src[pBlink->localToGlobal], pBlink->min, pBlink->max, 0.0f, 1.0f);
				}

				int nEyeUpDownIndex = static_cast<int>(stack[k - 3]);
				float flEyeUpDown = 0.0f;
				if (nEyeUpDownIndex >= 0)
				{
					const mstudioflexcontroller_t* const pEyeUpDown = pFlexcontroller(static_cast<LocalFlexController_t>((int)stack[k - 3]));
					flEyeUpDown = RemapValClamped(src[pEyeUpDown->localToGlobal], pEyeUpDown->min, pEyeUpDown->max, -1.0f, 1.0f);
				}

				if (flEyeUpDown < 0.0f)
				{
					stack[k - 3] = (1.0f + flEyeUpDown) * flCloseLidV * flCloseLid;
				}
				else
				{
					stack[k - 3] = flCloseLidV * flCloseLid;
				}
				k -= 2;
			}
			break;
			}

			pops++;
		}

		dest[prule->flex] = stack[0];
		/*
				// JasonM hack
				if ( nFlexRulesToRun == 0)					// 0 means run all rules correctly
				{
					dest[prule->flex] = stack[0];
				}
				else // run only up to nFlexRulesToRun correctly...zero out the rest
				{
					if ( j < nFlexRulesToRun )
						dest[prule->flex] = stack[0];
					else
						dest[prule->flex] = 0.0f;
				}

				dest[prule->flex] = 1.0f;
		//*/
		// end JasonM hack

	}
}



//-----------------------------------------------------------------------------
//	CODE PERTAINING TO ACTIVITY->SEQUENCE MAPPING SUBCLASS
//-----------------------------------------------------------------------------
#define iabs(i) (( (i) >= 0 ) ? (i) : -(i) )

CUtlSymbolTable g_ActivityModifiersTable;

void CActivityToSequenceMapping::Initialize(CStudioHdr* __restrict pstudiohdr)
{
	// Algorithm: walk through every sequence in the model, determine to which activity
	// it corresponds, and keep a count of sequences per activity. Once the total count
	// is available, allocate an array large enough to contain them all, update the 
	// starting indices for every activity's section in the array, and go back through,
	// populating the array with its data.

	AssertMsg1(m_pSequenceTuples == NULL, "Tried to double-initialize sequence mapping for %s", pstudiohdr->pszName());
	if (m_pSequenceTuples != NULL)
		return; // don't double initialize.

	SetValidationPair(pstudiohdr);

	if (!pstudiohdr->SequencesAvailable())
		return; // nothing to do.

#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
	m_bIsInitialized = true;
#endif

	// Some studio headers have no activities at all. In those
	// cases we can avoid a lot of this effort.
	bool bFoundOne = false;

	// for each sequence in the header...
	const int NumSeq = pstudiohdr->GetNumSeq();
	for (int i = 0; i < NumSeq; ++i)
	{
		const mstudioseqdesc_t& seqdesc = pstudiohdr->pSeqdesc(i);
//#if defined(SERVER_DLL) || defined(CLIENT_DLL) || defined(GAME_DLL)
		if (!(seqdesc.flags & STUDIO_ACTIVITY))
		{
			// AssertMsg2( false, "Sequence %d on studiohdr %s didn't have its activity initialized!", i, pstudiohdr->pszName() );
			pstudiohdr->SetActivityForSequence(i);
		}
//#endif

		// is there an activity associated with this sequence?
		if (seqdesc.activity >= 0)
		{
			bFoundOne = true;

			// look up if we already have an entry. First we need to make a speculative one --
			HashValueType entry(seqdesc.activity, 0, 1, iabs(seqdesc.actweight));
			UtlHashHandle_t handle = m_ActToSeqHash.Find(entry);
			if (m_ActToSeqHash.IsValidHandle(handle))
			{
				// we already have an entry and must update it by incrementing count
				HashValueType* __restrict toUpdate = &m_ActToSeqHash.Element(handle);
				toUpdate->count += 1;
				toUpdate->totalWeight += iabs(seqdesc.actweight);
				if (!HushAsserts())
				{
					AssertMsg(toUpdate->totalWeight > 0, "toUpdate->totalWeight: %d", toUpdate->totalWeight);
				}
			}
			else
			{
				// we do not have an entry yet; create one.
				m_ActToSeqHash.Insert(entry);
			}
		}
	}

	// if we found nothing, don't bother with any other initialization!
	if (!bFoundOne)
		return;

	// Now, create starting indices for each activity. For an activity n, 
	// the starting index is of course the sum of counts [0..n-1]. 
	int sequenceCount = 0;
	int topActivity = 0; // this will store the highest seen activity number (used later to make an ad hoc map on the stack)
	for (UtlHashHandle_t handle = m_ActToSeqHash.GetFirstHandle();
		m_ActToSeqHash.IsValidHandle(handle);
		handle = m_ActToSeqHash.GetNextHandle(handle))
	{
		HashValueType& element = m_ActToSeqHash[handle];
		element.startingIdx = sequenceCount;
		sequenceCount += element.count;
		topActivity = max(topActivity, element.activityIdx);
	}


	// Allocate the actual array of sequence information. Note the use of restrict;
	// this is an important optimization, but means that you must never refer to this
	// array through m_pSequenceTuples in the scope of this function.
	SequenceTuple* __restrict tupleList = new SequenceTuple[sequenceCount];
	m_pSequenceTuples = tupleList; // save it off -- NEVER USE m_pSequenceTuples in this function!
	m_iSequenceTuplesCount = sequenceCount;



	// Now we're going to actually populate that list with the relevant data. 
	// First, create an array on the stack to store how many sequences we've written
	// so far for each activity. (This is basically a very simple way of doing a map.)
	// This stack may potentially grow very large; so if you have problems with it, 
	// go to a utlmap or similar structure.
	unsigned int allocsize = (topActivity + 1) * sizeof(int);
#define ALIGN_VALUE( val, alignment ) ( ( val + alignment - 1 ) & ~( alignment - 1 ) ) //  need macro for constant expression
	allocsize = ALIGN_VALUE(allocsize, 16);
	int* __restrict seqsPerAct = static_cast<int*>(stackalloc(allocsize));
	memset(seqsPerAct, 0, allocsize);

	// okay, walk through all the sequences again, and write the relevant data into 
	// our little table.
	for (int i = 0; i < NumSeq; ++i)
	{
		const mstudioseqdesc_t& seqdesc = pstudiohdr->pSeqdesc(i);
		if (seqdesc.activity >= 0)
		{
			const HashValueType& element = m_ActToSeqHash[m_ActToSeqHash.Find(HashValueType(seqdesc.activity, 0, 0, 0))];

			// If this assert trips, we've written more sequences per activity than we allocated 
			// (therefore there must have been a miscount in the first for loop above).
			int tupleOffset = seqsPerAct[seqdesc.activity];
			Assert(tupleOffset < element.count);

			if (seqdesc.numactivitymodifiers > 0)
			{
				// add entries for this model's activity modifiers
				(tupleList + element.startingIdx + tupleOffset)->pActivityModifiers = new CUtlSymbol[seqdesc.numactivitymodifiers];
				(tupleList + element.startingIdx + tupleOffset)->iNumActivityModifiers = seqdesc.numactivitymodifiers;

				for (int k = 0; k < seqdesc.numactivitymodifiers; k++)
				{
					(tupleList + element.startingIdx + tupleOffset)->pActivityModifiers[k] = g_ActivityModifiersTable.AddString(seqdesc.pActivityModifier(k)->pszName());
				}
			}
			else
			{
				(tupleList + element.startingIdx + tupleOffset)->pActivityModifiers = NULL;
				(tupleList + element.startingIdx + tupleOffset)->iNumActivityModifiers = 0;
			}

			// You might be tempted to collapse this pointer math into a single pointer --
			// don't! the tuple list is marked __restrict above.
			(tupleList + element.startingIdx + tupleOffset)->seqnum = i; // store sequence number
			(tupleList + element.startingIdx + tupleOffset)->weight = iabs(seqdesc.actweight);

			// We can't have weights of 0
			// Assert( (tupleList + element.startingIdx + tupleOffset)->weight > 0 );
			if ((tupleList + element.startingIdx + tupleOffset)->weight == 0)
			{
				(tupleList + element.startingIdx + tupleOffset)->weight = 1;
			}

			seqsPerAct[seqdesc.activity] += 1;
		}
	}

#ifdef DBGFLAG_ASSERT
	// double check that we wrote exactly the right number of sequences.
	unsigned int chkSequenceCount = 0;
	for (int j = 0; j <= topActivity; ++j)
	{
		chkSequenceCount += seqsPerAct[j];
	}
	Assert(chkSequenceCount == m_iSequenceTuplesCount);
#endif

}

/// Force Initialize() to occur again, even if it has already occured.
void CActivityToSequenceMapping::Reinitialize(CStudioHdr* pstudiohdr)
{
	m_bIsInitialized = false;
	if (m_pSequenceTuples)
	{
		delete m_pSequenceTuples;
		m_pSequenceTuples = NULL;
	}
	m_ActToSeqHash.RemoveAll();

	Initialize(pstudiohdr);
}

// Look up relevant data for an activity's sequences. This isn't terribly efficient, due to the
// load-hit-store on the output parameters, so the most common case -- SelectWeightedSequence --
// is specially implemented.
const CActivityToSequenceMapping::SequenceTuple* CActivityToSequenceMapping::GetSequences(int forActivity, int* __restrict outSequenceCount, int* __restrict outTotalWeight)
{
	// Construct a dummy entry so we can do a hash lookup (the UtlHash does not divorce keys from values)

	HashValueType entry(forActivity, 0, 0, 0);
	UtlHashHandle_t handle = m_ActToSeqHash.Find(entry);

	if (m_ActToSeqHash.IsValidHandle(handle))
	{
		const HashValueType& element = m_ActToSeqHash[handle];
		const SequenceTuple* retval = m_pSequenceTuples + element.startingIdx;
		*outSequenceCount = element.count;
		*outTotalWeight = element.totalWeight;

		return retval;
	}
	else
	{
		// invalid handle; return NULL.
		// this is actually a legit use case, so no need to assert.
		return NULL;
	}
}

int CActivityToSequenceMapping::NumSequencesForActivity(int forActivity)
{
	// If this trips, you've called this function on something that doesn't 
	// have activities.
	//Assert(m_pSequenceTuples != NULL);
	if (m_pSequenceTuples == NULL)
		return 0;

	HashValueType entry(forActivity, 0, 0, 0);
	UtlHashHandle_t handle = m_ActToSeqHash.Find(entry);
	if (m_ActToSeqHash.IsValidHandle(handle))
	{
		return m_ActToSeqHash[handle].count;
	}
	else
	{
		return 0;
	}
}

// double-check that the data I point to hasn't changed
bool CActivityToSequenceMapping::ValidateAgainst(const CStudioHdr* RESTRICT pstudiohdr) RESTRICT
{
	if (m_bIsInitialized)
	{
		return m_expectedPStudioHdr == pstudiohdr->GetRenderHdr() &&
			m_expectedVModel == pstudiohdr->GetVirtualModel();
	}
	else
	{
		return true; // Allow an ordinary initialization to take place without printing a panicky assert.
	}
}

void CActivityToSequenceMapping::SetValidationPair(const CStudioHdr* RESTRICT pstudiohdr) RESTRICT
{
	m_expectedPStudioHdr = pstudiohdr->GetRenderHdr();
	m_expectedVModel = pstudiohdr->GetVirtualModel();
}

// Pick a sequence for the given activity. If the current sequence is appropriate for the 
// current activity, and its stored weight is negative (whatever that means), always select
// it. Otherwise perform a weighted selection -- imagine a large roulette wheel, with each
// sequence having a number of spaces corresponding to its weight.
int CActivityToSequenceMapping::SelectWeightedSequence(CStudioHdr* pstudiohdr, int activity, int curSequence, RandomWeightFunc pRandomWeightFunc)
{
	if (!ValidateAgainst(pstudiohdr))
	{
		AssertMsg1(false, "IStudioHdr %s has changed its vmodel pointer without reinitializing its activity mapping! Now performing emergency reinitialization.", pstudiohdr->pszName());
		ExecuteOnce(DebuggerBreakIfDebugging());
		Reinitialize(pstudiohdr);
	}

	// a null m_pSequenceTuples just means that this studio header has no activities.
	if (!m_pSequenceTuples)
		return ACTIVITY_NOT_AVAILABLE;

	// is the current sequence appropriate?
	if (curSequence >= 0)
	{
		mstudioseqdesc_t& seqdesc = pstudiohdr->pSeqdesc(curSequence);

		if (seqdesc.activity == activity && seqdesc.actweight < 0)
			return curSequence;
	}

	// get the data for the given activity
	HashValueType dummy(activity, 0, 0, 0);
	UtlHashHandle_t handle = m_ActToSeqHash.Find(dummy);
	if (!m_ActToSeqHash.IsValidHandle(handle))
	{
		return ACTIVITY_NOT_AVAILABLE;
	}
	const HashValueType* __restrict actData = &m_ActToSeqHash[handle];

	int weighttotal = actData->totalWeight;
	// generate a random number from 0 to the total weight
	int randomValue;
	if (pRandomWeightFunc) {
		randomValue = (*pRandomWeightFunc)(0, weighttotal - 1);
	}
	else {
		Error("pRandomWeightFunc must not been NULL");
	}

	// chug through the entries in the list (they are sequential therefore cache-coherent)
	// until we run out of random juice
	SequenceTuple* __restrict sequenceInfo = m_pSequenceTuples + actData->startingIdx;

	const SequenceTuple* const stopHere = sequenceInfo + actData->count; // this is a backup 
	// in case the weights are somehow miscalculated -- we don't read or write through
	// it (because it aliases the restricted pointer above); it's only here for 
	// the comparison.

	while (randomValue >= sequenceInfo->weight && sequenceInfo < stopHere)
	{
		randomValue -= sequenceInfo->weight;
		++sequenceInfo;
	}

	return sequenceInfo->seqnum;

}


IStudioHdr* CMDLCache::GetIStudioHdr(MDLHandle_t handle) 
{
	return GetStudioHdr(handle);
}

//IStudioHdr* CMDLCache::GetIStudioHdr(studiohdr_t* pStudioHdr)
//{
//	return new CStudioHdr(pStudioHdr, this);
//}

CStudioHdr* CMDLCache::GetStudioHdr(MDLHandle_t handle) {
	if (handle == MDLHANDLE_INVALID)
		return NULL;

	CStudioHdr* pStudioData = m_MDLDict[handle];

	if (!pStudioData)
		return NULL;

	if (pStudioData->IsValid()) {
		return pStudioData;
	}

	pStudioData->LoadStudioHdr();

	if (!pStudioData->IsValid()) {
		return NULL;
	}

	return pStudioData;
}

//-----------------------------------------------------------------------------
// Loading the data in
//-----------------------------------------------------------------------------
studiohdr_t *CStudioHdr::LoadStudioHdr() const
{
	if ( m_handle == MDLHANDLE_INVALID )
		return NULL;

	// Returning a pointer to data inside the cache when it's unlocked is just a bad idea.
	// It's technically legal, but the pointer can get invalidated if anything else looks at the cache.
	// Don't do that.
	// Assert( m_pModelCacheSection->IsFrameLocking() );
	// Assert( m_pMeshCacheSection->IsFrameLocking() );

	//CStudioHdr *pStudioData = m_MDLDict[handle];

	//if( !pStudioData )
	//	return NULL;

#if _DEBUG
	VPROF_INCREMENT_COUNTER( "GetStudioHdr", 1 );
#endif
	studiohdr_t *pHdr = (studiohdr_t*)m_pMDLCache->CheckData( m_MDLCache, MDLCACHE_STUDIOHDR );
	if ( !pHdr )
	{
		if (m_bInLoad) {
			Error("Recursive call LoadStudioHdr");
			return NULL;
		}
		m_bInLoad = true;
		m_MDLCache = NULL;

		CMDLCacheCriticalSection cacheCriticalSection(m_pMDLCache);

		// load the file
		const char *pModelName = GetActualModelName();
		if ( developer.GetInt() > 1 )
		{
			DevMsg( "Loading %s\n", pModelName );
		}

		// Load file to temporary space
		CUtlBuffer buf;
		if ( !ReadMDLFile( pModelName, buf ) )
		{
			bool bOk = false;
			if ( ( m_nFlags & STUDIODATA_ERROR_MODEL ) == 0 )
			{
				buf.Clear(); // clear buffer for next file read

				m_nFlags |= STUDIODATA_ERROR_MODEL;
				bOk = ReadMDLFile( ERROR_MODEL, buf );
			}

			if ( !bOk )
			{
				if (IsOSX())
				{
					// rbarris wants this to go somewhere like the console.log prior to crashing, which is what the Error call will do next
					printf("\n ##### Model %s not found and %s couldn't be loaded", pModelName, ERROR_MODEL );
					fflush( stdout );
				}
				Error( "Model %s not found and %s couldn't be loaded", pModelName, ERROR_MODEL );
				return NULL;
			}
		}

		// put it in the cache
		if (ProcessDataIntoCache( MDLCACHE_STUDIOHDR, 0, buf.Base(), buf.TellMaxPut(), true ) )
		{
			pHdr = (studiohdr_t*)m_pMDLCache->CheckData( m_MDLCache, MDLCACHE_STUDIOHDR );
		}
	}

	return pHdr;
}


//-----------------------------------------------------------------------------
// Gets/sets user data associated with the MDL
//-----------------------------------------------------------------------------
void CMDLCache::SetUserData( MDLHandle_t handle, void* pData )
{
	if ( handle == MDLHANDLE_INVALID )
		return;

	m_MDLDict[handle]->m_pUserData = pData;
}

void *CMDLCache::GetUserData( MDLHandle_t handle )
{
	if ( handle == MDLHANDLE_INVALID )
		return NULL;
	return m_MDLDict[handle]->m_pUserData;
}


//-----------------------------------------------------------------------------
// Polls information about a particular mdl
//-----------------------------------------------------------------------------
bool CMDLCache::IsErrorModel( MDLHandle_t handle )
{
	if ( handle == MDLHANDLE_INVALID )
		return false;

	return (m_MDLDict[handle]->m_nFlags & STUDIODATA_ERROR_MODEL) != 0;
}


//-----------------------------------------------------------------------------
// Brings all data associated with an MDL into memory
//-----------------------------------------------------------------------------
void CMDLCache::TouchAllData( MDLHandle_t handle )
{
	CStudioHdr* pStudioHdr = m_MDLDict[handle];
	if (!pStudioHdr)
	{
		return;
	}
	studiohdr_t *studioHdr = pStudioHdr->LoadStudioHdr();
	CVirtualModel *pVModel = GetVirtualModel( handle );
	if ( pVModel )
	{
		// skip self, start at children
		// ensure all sub models are cached
		for ( int i=1; i<pVModel->m_group.Count(); ++i )
		{
			//MDLHandle_t childHandle = VoidPtrToMDLHandle( pVModel->m_group[i].cache );
			const CStudioHdr* pGroupStudioHdr = pVModel->GroupStudioHdr(i);
			if (pGroupStudioHdr != NULL)
			{
				// FIXME: Should this be calling TouchAllData on the child?
				pGroupStudioHdr->LoadStudioHdr();
			}
		}
	}

	if ( !IsX360() )
	{
		// cache the anims
		// Note that the animblocks start at 1!!!
		for ( int i=1; i< (int)studioHdr->numanimblocks; ++i )
		{
			//pStudioHdr->GetAnimBlock( i );
			GetAnimBlock(handle, i);
		}
	}

	// cache the vertexes
	if ( pStudioHdr->numbodyparts() )
	{
		pStudioHdr->CacheVertexData();
		GetHardwareData( handle );
	}
}


//-----------------------------------------------------------------------------
// Flushes all data
//-----------------------------------------------------------------------------
void CMDLCache::Flush( MDLCacheFlush_t nFlushFlags )
{
	// Free all MDLs that haven't been cleaned up
	MDLHandle_t i = m_MDLDict.First();
	while ( i != m_MDLDict.InvalidIndex() )
	{
		Flush( i, nFlushFlags );
		i = m_MDLDict.Next( i );
	}
}

//-----------------------------------------------------------------------------
// Cache handlers
//-----------------------------------------------------------------------------
static const char *g_ppszTypes[] =
{
	"studiohdr",	// MDLCACHE_STUDIOHDR
	"studiohwdata", // MDLCACHE_STUDIOHWDATA
	"vcollide",		// MDLCACHE_VCOLLIDE
	"animblock",	// MDLCACHE_ANIMBLOCK
	"virtualmodel", // MDLCACHE_VIRTUALMODEL
	"vertexes",		// MDLCACHE_VERTEXES
};

bool CMDLCache::HandleCacheNotification( const DataCacheNotification_t &notification  )
{
	switch ( notification.type )
	{
	case DC_AGE_DISCARD:
	case DC_FLUSH_DISCARD:
	case DC_REMOVED:
		{
			MdlCacheMsg( "MDLCache: Data cache discard %s %s\n", g_ppszTypes[TypeFromCacheID( notification.clientId )], GetModelName( HandleFromCacheID( notification.clientId ) ) );

			if ( (DataCacheClientID_t)(intp)notification.pItemData == notification.clientId ||
				 TypeFromCacheID(notification.clientId) != MDLCACHE_STUDIOHWDATA )
			{
				Assert( notification.pItemData );
				FreeData( TypeFromCacheID(notification.clientId), (void *)notification.pItemData );
			}
			else
			{
				m_MDLDict[HandleFromCacheID(notification.clientId)]->UnloadHardwareData(  false );
			}
			return true;
		}
	}

	return CDefaultDataCacheClient::HandleCacheNotification( notification );
}

bool CMDLCache::GetItemName( DataCacheClientID_t clientId, const void *pItem, char *pDest, unsigned nMaxLen  )
{
	if ( (DataCacheClientID_t)(uintp)pItem == clientId )
	{
		return false;
	}

	MDLHandle_t handle = HandleFromCacheID( clientId );
	MDLCacheDataType_t type = TypeFromCacheID( clientId );

	Q_snprintf( pDest, nMaxLen, "%s - %s", g_ppszTypes[type], GetModelName( handle ) );

	return false;
}

//-----------------------------------------------------------------------------
// Flushes all data
//-----------------------------------------------------------------------------
void CMDLCache::BeginLock()
{
	m_pModelCacheSection->BeginFrameLocking();
	m_pMeshCacheSection->BeginFrameLocking();
}

//-----------------------------------------------------------------------------
// Flushes all data
//-----------------------------------------------------------------------------
void CMDLCache::EndLock()
{
	m_pModelCacheSection->EndFrameLocking();
	m_pMeshCacheSection->EndFrameLocking();
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CMDLCache::BreakFrameLock( bool bModels, bool bMesh )
{
	if ( bModels )
	{
		if ( m_pModelCacheSection->IsFrameLocking() )
		{
			Assert( !m_nModelCacheFrameLocks );
			m_nModelCacheFrameLocks = 0;
			do
			{
				m_nModelCacheFrameLocks++;
			} while ( m_pModelCacheSection->EndFrameLocking() );
		}

	}

	if ( bMesh )
	{
		if ( m_pMeshCacheSection->IsFrameLocking() )
		{
			Assert( !m_nMeshCacheFrameLocks );
			m_nMeshCacheFrameLocks = 0;
			do
			{
				m_nMeshCacheFrameLocks++;
			} while ( m_pMeshCacheSection->EndFrameLocking() );
		}
	}

}

void CMDLCache::RestoreFrameLock()
{
	while ( m_nModelCacheFrameLocks )
	{
		m_pModelCacheSection->BeginFrameLocking();
		m_nModelCacheFrameLocks--;
	}
	while ( m_nMeshCacheFrameLocks )
	{
		m_pMeshCacheSection->BeginFrameLocking();
		m_nMeshCacheFrameLocks--;
	}
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
int *CMDLCache::GetFrameUnlockCounterPtrOLD()
{
	return GetCacheSection( MDLCACHE_STUDIOHDR )->GetFrameUnlockCounterPtr();
}

int *CMDLCache::GetFrameUnlockCounterPtr( MDLCacheDataType_t type )
{
	return GetCacheSection( type )->GetFrameUnlockCounterPtr();
}

//-----------------------------------------------------------------------------
// Completes all pending async operations
//-----------------------------------------------------------------------------
void CMDLCache::FinishPendingLoads()
{
	if ( !ThreadInMainThread() )
	{
		return;
	}

	AUTO_LOCK( m_AsyncMutex );

	// finish just our known jobs
	intp iAsync = m_PendingAsyncs.Head();
	while ( iAsync != m_PendingAsyncs.InvalidIndex() )
	{
		AsyncInfo_t &info = m_PendingAsyncs[iAsync];
		if ( info.hControl )
		{
			g_pFullFileSystem->AsyncFinish( info.hControl, true );
		}
		iAsync = m_PendingAsyncs.Next( iAsync );
	}

	ProcessPendingAsyncs();
}

//-----------------------------------------------------------------------------
// Notify map load has started
//-----------------------------------------------------------------------------
void CMDLCache::BeginMapLoad()
{
	BreakFrameLock();

	CStudioHdr *pStudioData;

	// Unlock prior map MDLs prior to load
	MDLHandle_t i = m_MDLDict.First();
	while ( i != m_MDLDict.InvalidIndex() )
	{
		pStudioData = m_MDLDict[i];
		if ( pStudioData->m_nFlags & STUDIODATA_FLAGS_LOCKED_MDL )
		{
			GetCacheSection( MDLCACHE_STUDIOHDR )->Unlock( pStudioData->m_MDLCache );
			pStudioData->m_nFlags &= ~STUDIODATA_FLAGS_LOCKED_MDL;
		}
		i = m_MDLDict.Next( i );
	}
}

//-----------------------------------------------------------------------------
// Notify map load is complete
//-----------------------------------------------------------------------------
void CMDLCache::EndMapLoad()
{
	FinishPendingLoads();

	// Remove all stray MDLs not referenced during load
	if ( mod_lock_mdls_on_load.GetBool() )
	{
		CStudioHdr *pStudioData;
		MDLHandle_t i = m_MDLDict.First();
		while ( i != m_MDLDict.InvalidIndex() )
		{
			pStudioData = m_MDLDict[i];
			if ( !(pStudioData->m_nFlags & STUDIODATA_FLAGS_LOCKED_MDL) )
			{
				Flush( i, MDLCACHE_FLUSH_STUDIOHDR );
			}
			i = m_MDLDict.Next( i );
		}
	}

	RestoreFrameLock();
}


//-----------------------------------------------------------------------------
// Is a particular part of the model data loaded?
//-----------------------------------------------------------------------------
bool CMDLCache::IsDataLoaded( MDLHandle_t handle, MDLCacheDataType_t type )
{
	if ( handle == MDLHANDLE_INVALID || !m_MDLDict.IsValidIndex( handle ) )
		return false;

	CStudioHdr *pData = m_MDLDict[ handle ];
	switch( type )
	{
	case MDLCACHE_STUDIOHDR:
		return GetCacheSection( MDLCACHE_STUDIOHDR )->IsPresent( pData->m_MDLCache );

	case MDLCACHE_STUDIOHWDATA:
		return ( pData->m_nFlags & STUDIODATA_FLAGS_STUDIOMESH_LOADED ) != 0;

	case MDLCACHE_VCOLLIDE:
		return ( pData->m_nFlags & STUDIODATA_FLAGS_VCOLLISION_LOADED ) != 0;

	case MDLCACHE_ANIMBLOCK:
		{
			if ( !pData->m_pAnimBlock )
				return false;

			for (int i = 0; i < pData->m_nAnimBlockCount; ++i )
			{
				if ( !pData->m_pAnimBlock[i] )
					return false;

				if ( !GetCacheSection( type )->IsPresent( pData->m_pAnimBlock[i] ) )
					return false;
			}
			return true;
		}

	case MDLCACHE_VIRTUALMODEL:
		return ( pData->m_pVirtualModel.NumGroup() != 0 );

	case MDLCACHE_VERTEXES:
		return m_pMeshCacheSection->IsPresent( pData->m_VertexCache );
	}
	return false;
}


//-----------------------------------------------------------------------------
// Get the correct extension for our dx
//-----------------------------------------------------------------------------
const char *CMDLCache::GetVTXExtension()
{
	if ( IsPC() )
	{
		if ( g_pMaterialSystemHardwareConfig->GetDXSupportLevel() >= 90 )
		{
			return ".dx90.vtx";
		}
		else if ( g_pMaterialSystemHardwareConfig->GetDXSupportLevel() >= 80 )
		{
			return ".dx80.vtx";
		}
		else
		{
			return ".sw.vtx";
		}
	}

	return ".dx90.vtx";
}

//-----------------------------------------------------------------------------
// Minimal presence and header validation, no data loads
// Return true if successful, false otherwise.
//-----------------------------------------------------------------------------
bool CMDLCache::VerifyHeaders( studiohdr_t *pStudioHdr )
{
	VPROF( "CMDLCache::VerifyHeaders" );

	if ( developer.GetInt() < 2 )
	{
		return true;
	}

	// model has no vertex data
	if ( !pStudioHdr->numbodyparts )
	{
		// valid
		return true;
	}

	char pFileName[ MAX_PATH ];
	MDLHandle_t handle = VoidPtrToMDLHandle( pStudioHdr->VirtualModel() );

	MakeFilename( handle, ".vvd", pFileName, sizeof(pFileName) );

	MdlCacheMsg("MDLCache: Load VVD (verify) %s\n", pFileName );

	// vvd header only
	CUtlBuffer vvdHeader( 0, sizeof(vertexFileHeader_t) );
	if ( !ReadFileNative( pFileName, "GAME", vvdHeader, sizeof(vertexFileHeader_t) ) )
	{
		return false;
	}

	vertexFileHeader_t *pVertexHdr = (vertexFileHeader_t*)vvdHeader.PeekGet();

	// check
	if (( pVertexHdr->id != MODEL_VERTEX_FILE_ID ) ||
		( pVertexHdr->version != MODEL_VERTEX_FILE_VERSION ) ||
		( pVertexHdr->checksum != pStudioHdr->checksum ))
	{
		return false;
	}

	// load the VTX file
	// use model name for correct path
	MakeFilename( handle, GetVTXExtension(), pFileName, sizeof(pFileName) );

	MdlCacheMsg("MDLCache: Load VTX (verify) %s\n", pFileName );

	// vtx header only
	CUtlBuffer vtxHeader( 0, sizeof(OptimizedModel::FileHeader_t) );
	if ( !ReadFileNative( pFileName, "GAME", vtxHeader, sizeof(OptimizedModel::FileHeader_t) ) )
	{
		return false;
	}

	// check
	OptimizedModel::FileHeader_t *pVtxHdr = (OptimizedModel::FileHeader_t*)vtxHeader.PeekGet();
	if (( pVtxHdr->version != OPTIMIZED_MODEL_FILE_VERSION ) ||
		( pVtxHdr->checkSum != pStudioHdr->checksum ))
	{
		return false;
	}

	// valid
	return true;
}


//-----------------------------------------------------------------------------
// Cache model's specified dynamic data
//-----------------------------------------------------------------------------
vertexFileHeader_t *CStudioHdr::CacheVertexData()
{
	VPROF( "CMDLCache::CacheVertexData" );

	vertexFileHeader_t	*pVvdHdr;
	//MDLHandle_t			handle;

	//Assert( pStudioHdr );

	//handle = VoidPtrToMDLHandle( pStudioHdr->VirtualModel() );
	Assert( m_handle != MDLHANDLE_INVALID );

	pVvdHdr = (vertexFileHeader_t *)m_pMDLCache->CheckData( m_VertexCache, MDLCACHE_VERTEXES );
	if ( pVvdHdr )
	{
		return pVvdHdr;
	}

	m_VertexCache = NULL;

	return LoadVertexData();
}

//-----------------------------------------------------------------------------
// Start an async transfer
//-----------------------------------------------------------------------------
FSAsyncStatus_t CMDLCache::LoadData( const char *pszFilename, const char *pszPathID, void *pDest, int nBytes, int nOffset, bool bAsync, FSAsyncControl_t *pControl )
{
	if ( !*pControl )
	{
		if ( IsX360() && g_pQueuedLoader->IsMapLoading() )
		{
			DevWarning( "CMDLCache: Non-Optimal loading path for %s\n", pszFilename );
		}

		FileAsyncRequest_t asyncRequest;
		asyncRequest.pszFilename = pszFilename;
		asyncRequest.pszPathID = pszPathID;
		asyncRequest.pData = pDest;
		asyncRequest.nBytes = nBytes;
		asyncRequest.nOffset = nOffset;

		if ( !pDest )
		{
			asyncRequest.flags = FSASYNC_FLAGS_ALLOCNOFREE;
		}

		if ( !bAsync )
		{
			asyncRequest.flags |= FSASYNC_FLAGS_SYNC;
		}

		MEM_ALLOC_CREDIT();
		return g_pFullFileSystem->AsyncRead( asyncRequest, pControl );
	}

	return FSASYNC_ERR_FAILURE;
}

//-----------------------------------------------------------------------------
// Determine the maximum number of 'real' bone influences used by any vertex in a model
// (100% binding to bone zero doesn't count)
//-----------------------------------------------------------------------------
int ComputeMaxRealBoneInfluences( vertexFileHeader_t * vertexFile, int lod )
{
	const mstudiovertex_t * verts = vertexFile->GetVertexData();
	int numVerts = vertexFile->numLODVertexes[ lod ];
	Assert(verts);

	int maxWeights = 0;
	for (int i = 0;i < numVerts;i++)
	{
		if ( verts[i].m_BoneWeights.numbones > 0 )
		{
			int numWeights = 0;
			for (int j = 0;j < MAX_NUM_BONES_PER_VERT;j++)
			{
				if ( verts[i].m_BoneWeights.weight[j] > 0 )
					numWeights = j + 1;
			}
			if ( ( numWeights == 1 ) && ( verts[i].m_BoneWeights.bone[0] == 0 ) )
			{
				// 100% binding to first bone - not really skinned (the first bone is just the model transform)
				numWeights = 0;
			}
			maxWeights = max( numWeights, maxWeights );
		}
	}
	return maxWeights;
}

//-----------------------------------------------------------------------------
// Generate thin vertices (containing just the data needed to do model decals)
//-----------------------------------------------------------------------------
vertexFileHeader_t * CStudioHdr::CreateThinVertexes( vertexFileHeader_t * originalData, int * cacheLength ) const
{
	int rootLod = min( (int)m_pStudioHdr->rootLOD, ( originalData->numLODs - 1 ) );
	int numVerts = originalData->numLODVertexes[ rootLod ] + 1; // Add 1 vert to support prefetch during array access

	int numBoneInfluences = ComputeMaxRealBoneInfluences( originalData, rootLod );
	// Only store (N-1) weights (all N weights sum to 1, so we can re-compute the Nth weight later)
	int numStoredWeights = max( 0, ( numBoneInfluences - 1 ) );

	int vertexSize = 2*sizeof( Vector ) + numBoneInfluences*sizeof( unsigned char ) + numStoredWeights*sizeof( float );
	*cacheLength = sizeof( vertexFileHeader_t ) + sizeof( thinModelVertices_t ) + numVerts*vertexSize;

	// Allocate cache space for the thin data
	MemAlloc_PushAllocDbgInfo( "Models:Vertex data", 0);
	vertexFileHeader_t * pNewVvdHdr = (vertexFileHeader_t *)m_pMDLCache->AllocData( MDLCACHE_VERTEXES, *cacheLength );
	MemAlloc_PopAllocDbgInfo();

	Assert( pNewVvdHdr );
	if ( pNewVvdHdr )
	{
		// Copy the header and set it up to hold thin vertex data
		memcpy( (void *)pNewVvdHdr, (void *)originalData, sizeof( vertexFileHeader_t ) );
		pNewVvdHdr->id					= MODEL_VERTEX_FILE_THIN_ID;
		pNewVvdHdr->numFixups			= 0;
		pNewVvdHdr->fixupTableStart		= 0;
		pNewVvdHdr->tangentDataStart	= 0;
		pNewVvdHdr->vertexDataStart		= sizeof( vertexFileHeader_t );

		// Set up the thin vertex structure
 		thinModelVertices_t	* pNewThinVerts = (thinModelVertices_t	*)( pNewVvdHdr		+ 1 );
		Vector				* pPositions	= (Vector				*)( pNewThinVerts	+ 1 );
		float				* pBoneWeights	= (float				*)( pPositions		+ numVerts );
		// Alloc the (short) normals here to avoid mis-aligning the float data
		unsigned short		* pNormals		= (unsigned short		*)( pBoneWeights	+ numVerts*numStoredWeights );
		// Alloc the (char) indices here to avoid mis-aligning the float/short data
		char				* pBoneIndices	= (char					*)( pNormals		+ numVerts );
		if ( numStoredWeights == 0 )
			pBoneWeights = NULL;
		if ( numBoneInfluences == 0 )
			pBoneIndices = NULL;
		pNewThinVerts->Init( numBoneInfluences, pPositions, pNormals, pBoneWeights, pBoneIndices );

		// Copy over the original data
		const mstudiovertex_t * srcVertexData = originalData->GetVertexData();
		for ( int i = 0; i < numVerts; i++ )
		{
			pNewThinVerts->SetPosition( i, srcVertexData[ i ].m_vecPosition );
			pNewThinVerts->SetNormal(   i, srcVertexData[ i ].m_vecNormal );
			if ( numBoneInfluences > 0 )
			{
				mstudioboneweight_t boneWeights;
				boneWeights.numbones = numBoneInfluences;
				for ( int j = 0; j < numStoredWeights; j++ )
				{
					boneWeights.weight[ j ] = srcVertexData[ i ].m_BoneWeights.weight[ j ];
				}
				for ( int j = 0; j < numBoneInfluences; j++ )
				{
					boneWeights.bone[ j ] = srcVertexData[ i ].m_BoneWeights.bone[ j ];
				}
				pNewThinVerts->SetBoneWeights( i, boneWeights );
			}
		}
	}

	return pNewVvdHdr;
}

//-----------------------------------------------------------------------------
// Process the provided raw data into the cache. Distributes to low level
// unserialization or build methods.
//-----------------------------------------------------------------------------
bool CStudioHdr::ProcessDataIntoCache( MDLCacheDataType_t type, int iAnimBlock, void *pData, int nDataSize, bool bDataValid ) const
{
	//CStudioHdr* pStudioDataCurrent = m_MDLDict[handle];

	//if (!pStudioDataCurrent)
	//{
	//	return false;
	//}

	studiohdr_t *pStudioHdrCurrent = NULL;
	if ( type != MDLCACHE_STUDIOHDR )
	{
		// can only get the studiohdr once the header has been processed successfully into the cache
		// causes a ProcessDataIntoCache() with the studiohdr data
		pStudioHdrCurrent = LoadStudioHdr();
		if ( !pStudioHdrCurrent )
		{
			return false;
		}
	}

	switch ( type )
	{
	case MDLCACHE_STUDIOHDR:
		{
			pStudioHdrCurrent = UnserializeMDL(pData, nDataSize, bDataValid );
			if ( !pStudioHdrCurrent )
			{
				return false;
			}
			break;
		}

	case MDLCACHE_VERTEXES:
		{
			if ( bDataValid )
			{
				BuildAndCacheVertexData( (vertexFileHeader_t *)pData );
			}
			else
			{
				m_nFlags |= STUDIODATA_FLAGS_NO_VERTEX_DATA;
				if ( pStudioHdrCurrent->numbodyparts )
				{
					// expected data not valid
					Warning( "MDLCache: Failed load of .VVD data for %s\n", pStudioHdrCurrent->pszName() );
					return false;
				}
			}
			break;
		}

	case MDLCACHE_STUDIOHWDATA:
		{
			if ( bDataValid )
			{
				BuildHardwareData( (OptimizedModel::FileHeader_t *)pData );
			}
			else
			{
				m_nFlags |= STUDIODATA_FLAGS_NO_STUDIOMESH;
				if ( pStudioHdrCurrent->numbodyparts )
				{
					// expected data not valid
					Warning( "MDLCache: Failed load of .VTX data for %s\n", pStudioHdrCurrent->pszName() );
					return false;
				}
			}

			m_pMDLCache->m_pMeshCacheSection->Unlock( m_VertexCache );
			m_pMDLCache->m_pMeshCacheSection->Age( m_VertexCache );

			// FIXME: thin VVD data on PC too (have to address alt-tab, various DX8/DX7/debug software paths in studiorender, tools, etc)
			static bool bCompressedVVDs = CommandLine()->CheckParm( "-no_compressed_vvds" ) == NULL;
			if ( IsX360() && !( m_nFlags & STUDIODATA_FLAGS_NO_STUDIOMESH ) && bCompressedVVDs )
			{
				// Replace the cached vertex data with a thin version (used for model decals).
				// Flexed meshes require the fat data to remain, for CPU mesh anim.
				if ( pStudioHdrCurrent->numflexdesc == 0 )
				{
					vertexFileHeader_t *originalVertexData = m_pMDLCache->GetVertexData( m_handle );
					Assert( originalVertexData );
					if ( originalVertexData )
					{
						int thinVertexDataSize = 0;
						vertexFileHeader_t *thinVertexData = CreateThinVertexes( originalVertexData, &thinVertexDataSize );
						Assert( thinVertexData && ( thinVertexDataSize > 0 ) );
						if ( thinVertexData && ( thinVertexDataSize > 0 ) )
						{
							// Remove the original cache entry (and free it)
							m_pMDLCache->Flush( m_handle, MDLCACHE_FLUSH_VERTEXES | MDLCACHE_FLUSH_IGNORELOCK );
							// Add the new one
							m_pMDLCache->CacheData( (DataCacheHandle_t*) & m_VertexCache, thinVertexData, thinVertexDataSize, pStudioHdrCurrent->pszName(), MDLCACHE_VERTEXES, MakeCacheID(m_handle, MDLCACHE_VERTEXES));
						}
					}
				}
			}

			break;
		}

	case MDLCACHE_ANIMBLOCK:
		{
			MEM_ALLOC_CREDIT_( __FILE__ ": Anim Blocks" );

			if ( bDataValid )
			{
				MdlCacheMsg( "MDLCache: Finish load anim block %s (block %i)\n", pStudioHdrCurrent->pszName(), iAnimBlock );

				char pCacheName[MAX_PATH];
				Q_snprintf( pCacheName, MAX_PATH, "%s (block %i)", pStudioHdrCurrent->pszName(), iAnimBlock );

				if ( IsX360() )
				{
					if ( CLZMA::IsCompressed( (unsigned char *)pData ) )
					{
						// anim block arrives compressed, decode and cache the results
						unsigned int nOriginalSize = CLZMA::GetActualSize( (unsigned char *)pData );

						// get a "fake" (not really aligned) optimal read buffer, as expected by the free logic
						void *pOriginalData = g_pFullFileSystem->AllocOptimalReadBuffer( FILESYSTEM_INVALID_HANDLE, nOriginalSize, 0 );
						unsigned int nOutputSize = CLZMA::Uncompress( (unsigned char *)pData, (unsigned char *)pOriginalData );
						if ( nOutputSize != nOriginalSize )
						{
							// decoder failure
							g_pFullFileSystem->FreeOptimalReadBuffer( pOriginalData );
							return false;
						}

						// input i/o buffer is now unused
						g_pFullFileSystem->FreeOptimalReadBuffer( pData );

						// datacache will now own the data
						pData = pOriginalData;
						nDataSize = nOriginalSize;
					}
				}

				m_pMDLCache->CacheData( &m_pAnimBlock[iAnimBlock], pData, nDataSize, pCacheName, MDLCACHE_ANIMBLOCK, MakeCacheID( m_handle, MDLCACHE_ANIMBLOCK) );
			}
			else
			{
				MdlCacheMsg( "MDLCache: Failed load anim block %s (block %i)\n", pStudioHdrCurrent->pszName(), iAnimBlock );
				if ( m_pAnimBlock )
				{
					m_pAnimBlock[iAnimBlock] = NULL;
				}
				return false;
			}
			break;
		}

	case MDLCACHE_VCOLLIDE:
		{
			// always marked as loaded, vcollides are not present for every model
			m_nFlags |= STUDIODATA_FLAGS_VCOLLISION_LOADED;

			if ( bDataValid )
			{
				MdlCacheMsg( "MDLCache: Finish load vcollide for %s\n", pStudioHdrCurrent->pszName() );

				CTempAllocHelper pOriginalData;
				if ( IsX360() )
				{
					if ( CLZMA::IsCompressed( (unsigned char *)pData ) )
					{
						// phy arrives compressed, decode and cache the results
						unsigned int nOriginalSize = CLZMA::GetActualSize( (unsigned char *)pData );
						pOriginalData.Alloc( nOriginalSize );
						unsigned int nOutputSize = CLZMA::Uncompress( (unsigned char *)pData, (unsigned char *)pOriginalData.Get() );
						if ( nOutputSize != nOriginalSize )
						{
							// decoder failure
							return NULL;
						}

						pData = pOriginalData.Get();
						nDataSize = nOriginalSize;
					}
				}

				CUtlBuffer buf( pData, nDataSize, CUtlBuffer::READ_ONLY );
				buf.SeekPut( CUtlBuffer::SEEK_HEAD, nDataSize );

				phyheader_t header;
				buf.Get( &header, sizeof( phyheader_t ) );
				if ( ( header.size == sizeof( header ) ) && header.solidCount > 0 )
				{
					int nBufSize = buf.TellMaxPut() - buf.TellGet();
					vcollide_t *pCollide = &m_VCollisionData;
					g_pPhysicsCollision->VCollideLoad( pCollide, header.solidCount, (const char*)buf.PeekGet(), nBufSize );
					if (m_pMDLCache->m_pCacheNotify )
					{
						m_pMDLCache->m_pCacheNotify->OnDataLoaded( MDLCACHE_VCOLLIDE, m_handle );
					}
				}
			}
			else
			{
				MdlCacheWarning( "MDLCache: Failed load of .PHY data for %s\n", pStudioHdrCurrent->pszName() );
				return false;
			}
			break;
		}

	default:
		Assert( 0 );
	}

	// success
	return true;
}

//-----------------------------------------------------------------------------
// Returns:
//	<0: indeterminate at this time
//	=0: pending
//	>0:	completed
//-----------------------------------------------------------------------------
int CMDLCache::ProcessPendingAsync( intp iAsync )
{
	if ( !ThreadInMainThread() )
	{
		return -1;
	}

	ASSERT_NO_REENTRY();

	void *pData = NULL;
	int nBytesRead = 0;

	AsyncInfo_t *pInfo;
	{
		AUTO_LOCK( m_AsyncMutex );
		pInfo = &m_PendingAsyncs[iAsync];
	}
	Assert( pInfo->hControl );

	FSAsyncStatus_t status = g_pFullFileSystem->AsyncGetResult( pInfo->hControl, &pData, &nBytesRead );
	if ( status == FSASYNC_STATUS_PENDING )
	{
		return 0;
	}

	AsyncInfo_t info = *pInfo;
	pInfo = &info;

	CStudioHdr* pStudioHdr = m_MDLDict[pInfo->hModel];
	if (!pStudioHdr) {
		return -1;
	}

	pStudioHdr->ClearAsync( pInfo->type, pInfo->iAnimBlock );

	switch ( pInfo->type )
	{
	case MDLCACHE_VERTEXES:
	case MDLCACHE_STUDIOHWDATA:
	case MDLCACHE_VCOLLIDE:
		{
			pStudioHdr->ProcessDataIntoCache( pInfo->type, 0, pData, nBytesRead, status == FSASYNC_OK );
			g_pFullFileSystem->FreeOptimalReadBuffer( pData );
			break;
		}

	case MDLCACHE_ANIMBLOCK:
		{
			// cache assumes ownership of valid async'd data
			if ( !pStudioHdr->ProcessDataIntoCache( MDLCACHE_ANIMBLOCK, pInfo->iAnimBlock, pData, nBytesRead, status == FSASYNC_OK ) )
			{
				g_pFullFileSystem->FreeOptimalReadBuffer( pData );
			}
			break;
		}

	default:
		Assert( 0 );
	}

	return 1;
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CMDLCache::ProcessPendingAsyncs( MDLCacheDataType_t type )
{
	if ( !ThreadInMainThread() )
	{
		return;
	}

	if ( !m_PendingAsyncs.Count() )
	{
		return;
	}

	static bool bReentering;
	if ( bReentering )
	{
		return;
	}
	bReentering = true;

	AUTO_LOCK( m_AsyncMutex );

	// Process all of the completed loads that were requested before a new one. This ensures two
	// things -- the LRU is in correct order, and it catches precached items lurking
	// in the async queue that have only been requested once (thus aren't being cached
	// and might lurk forever, e.g., wood gibs in the citadel)
	intp current = m_PendingAsyncs.Head();
	while ( current != m_PendingAsyncs.InvalidIndex() )
	{
		intp next = m_PendingAsyncs.Next( current );

		if ( type == MDLCACHE_NONE || m_PendingAsyncs[current].type == type )
		{
			// process, also removes from list
			if ( ProcessPendingAsync( current ) <= 0 )
			{
				// indeterminate or pending
				break;
			}
		}

		current = next;
	}

	bReentering = false;
}

//-----------------------------------------------------------------------------
// Cache model's specified dynamic data
//-----------------------------------------------------------------------------
bool CStudioHdr::ClearAsync( MDLCacheDataType_t type, int iAnimBlock, bool bAbort ) const
{
	intp iAsyncInfo = GetAsyncInfoIndex( m_handle, type, iAnimBlock );
	if ( iAsyncInfo != NO_ASYNC )
	{
		AsyncInfo_t *pInfo;
		{
			AUTO_LOCK( m_pMDLCache->m_AsyncMutex );
			pInfo = &m_pMDLCache->m_PendingAsyncs[iAsyncInfo];
		}
		if ( pInfo->hControl )
		{
			if ( bAbort )
			{
				g_pFullFileSystem->AsyncAbort(  pInfo->hControl );
				void *pData;
				int ignored;
				if ( g_pFullFileSystem->AsyncGetResult(  pInfo->hControl, &pData, &ignored ) == FSASYNC_OK )
				{
					g_pFullFileSystem->FreeOptimalReadBuffer( pData );
				}
			}
			g_pFullFileSystem->AsyncRelease(  pInfo->hControl );
			pInfo->hControl = NULL;
		}

		SetAsyncInfoIndex( m_handle, type, iAnimBlock, NO_ASYNC );
		{
			AUTO_LOCK(m_pMDLCache->m_AsyncMutex );
			m_pMDLCache->m_PendingAsyncs.Remove( iAsyncInfo );
		}

		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CMDLCache::GetAsyncLoad( MDLCacheDataType_t type )
{
	switch ( type )
	{
	case MDLCACHE_STUDIOHDR:
		return false;
	case MDLCACHE_STUDIOHWDATA:
		return mod_load_mesh_async.GetBool();
	case MDLCACHE_VCOLLIDE:
		return mod_load_vcollide_async.GetBool();
	case MDLCACHE_ANIMBLOCK:
		return mod_load_anims_async.GetBool();
	case MDLCACHE_VIRTUALMODEL:
		return false;
	case MDLCACHE_VERTEXES:
		return mod_load_mesh_async.GetBool();
	}
	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CMDLCache::SetAsyncLoad( MDLCacheDataType_t type, bool bAsync )
{
	bool bRetVal = false;
	switch ( type )
	{
	case MDLCACHE_STUDIOHDR:
		break;
	case MDLCACHE_STUDIOHWDATA:
		bRetVal = mod_load_mesh_async.GetBool();
		mod_load_mesh_async.SetValue( bAsync );
		break;
	case MDLCACHE_VCOLLIDE:
		bRetVal = mod_load_vcollide_async.GetBool();
		mod_load_vcollide_async.SetValue( bAsync );
		break;
	case MDLCACHE_ANIMBLOCK:
		bRetVal = mod_load_anims_async.GetBool();
		mod_load_anims_async.SetValue( bAsync );
		break;
	case MDLCACHE_VIRTUALMODEL:
		return false;
		break;
	case MDLCACHE_VERTEXES:
		bRetVal = mod_load_mesh_async.GetBool();
		mod_load_mesh_async.SetValue( bAsync );
		break;
	}
	return bRetVal;
}

//-----------------------------------------------------------------------------
// Cache model's specified dynamic data
//-----------------------------------------------------------------------------
vertexFileHeader_t *CStudioHdr::BuildAndCacheVertexData( vertexFileHeader_t *pRawVvdHdr  ) const
{
	//MDLHandle_t	handle = VoidPtrToMDLHandle( pStudioHdr->VirtualModel() );
	vertexFileHeader_t *pVvdHdr;

	MdlCacheMsg( "MDLCache: Load VVD for %s\n", m_pStudioHdr->pszName() );

	Assert( pRawVvdHdr );

	// check header
	if ( pRawVvdHdr->id != MODEL_VERTEX_FILE_ID )
	{
		Warning( "Error Vertex File for '%s' id %d should be %d\n", m_pStudioHdr->pszName(), pRawVvdHdr->id, MODEL_VERTEX_FILE_ID );
		return NULL;
	}
	if ( pRawVvdHdr->version != MODEL_VERTEX_FILE_VERSION )
	{
		Warning( "Error Vertex File for '%s' version %d should be %d\n", m_pStudioHdr->pszName(), pRawVvdHdr->version, MODEL_VERTEX_FILE_VERSION );
		return NULL;
	}
	if ( pRawVvdHdr->checksum != m_pStudioHdr->checksum )
	{
		Warning( "Error Vertex File for '%s' checksum %d should be %d\n", m_pStudioHdr->pszName(), pRawVvdHdr->checksum, m_pStudioHdr->checksum );
		return NULL;
	}

	Assert( pRawVvdHdr->numLODs );
	if ( !pRawVvdHdr->numLODs )
	{
		return NULL;
	}

	CTempAllocHelper pOriginalData;
	if ( IsX360() )
	{
		unsigned char *pInput = (unsigned char *)pRawVvdHdr + sizeof( vertexFileHeader_t );
		if ( CLZMA::IsCompressed( pInput ) )
		{
			// vvd arrives compressed, decode and cache the results
			unsigned int nOriginalSize = CLZMA::GetActualSize( pInput );
			pOriginalData.Alloc( sizeof( vertexFileHeader_t ) + nOriginalSize );
			V_memcpy( pOriginalData.Get(), pRawVvdHdr, sizeof( vertexFileHeader_t ) );
			unsigned int nOutputSize = CLZMA::Uncompress( pInput, sizeof( vertexFileHeader_t ) + (unsigned char *)pOriginalData.Get() );
			if ( nOutputSize != nOriginalSize )
			{
				// decoder failure
				return NULL;
			}

			pRawVvdHdr = (vertexFileHeader_t *)pOriginalData.Get();
		}
	}

	bool bNeedsTangentS = IsX360() || (g_pMaterialSystemHardwareConfig->GetDXSupportLevel() >= 80);
	int rootLOD = min( (int)m_pStudioHdr->rootLOD, pRawVvdHdr->numLODs - 1 );

	// determine final cache footprint, possibly truncated due to lod
	int cacheLength = Studio_VertexDataSize( pRawVvdHdr, rootLOD, bNeedsTangentS );

	MdlCacheMsg("MDLCache: Alloc VVD %s\n", GetModelName() );

	// allocate cache space
	MemAlloc_PushAllocDbgInfo( "Models:Vertex data", 0);
	pVvdHdr = (vertexFileHeader_t *)m_pMDLCache->AllocData( MDLCACHE_VERTEXES, cacheLength );
	MemAlloc_PopAllocDbgInfo();

	m_pMDLCache->GetCacheSection( MDLCACHE_VERTEXES )->BeginFrameLocking();

	m_pMDLCache->CacheData( (DataCacheHandle_t*)&m_VertexCache, pVvdHdr, cacheLength, m_pStudioHdr->pszName(), MDLCACHE_VERTEXES, MakeCacheID(m_handle, MDLCACHE_VERTEXES));

	// expected 32 byte alignment
	Assert( ((int64)pVvdHdr & 0x1F) == 0 );

	// load minimum vertexes and fixup
	Studio_LoadVertexes( pRawVvdHdr, pVvdHdr, rootLOD, bNeedsTangentS );

	m_pMDLCache->GetCacheSection( MDLCACHE_VERTEXES )->EndFrameLocking();

	return pVvdHdr;
}

//-----------------------------------------------------------------------------
// Load and cache model's specified dynamic data
//-----------------------------------------------------------------------------
vertexFileHeader_t *CStudioHdr::LoadVertexData()
{
	char				pFileName[MAX_PATH];
	//MDLHandle_t			handle;

	Assert( m_pStudioHdr );
	//handle = VoidPtrToMDLHandle( pStudioHdr->VirtualModel() );
	Assert( !m_VertexCache );//m_MDLDict[handle]->

	//CStudioHdr *pStudioData = m_MDLDict[handle];

	if ( m_nFlags & STUDIODATA_FLAGS_NO_VERTEX_DATA )
	{
		return NULL;
	}

	intp iAsync = GetAsyncInfoIndex( m_handle, MDLCACHE_VERTEXES );

	if ( iAsync == NO_ASYNC )
	{
		// load the VVD file
		// use model name for correct path
		m_pMDLCache->MakeFilename( m_handle, ".vvd", pFileName, sizeof(pFileName) );
		if ( IsX360() )
		{
			char pX360Filename[MAX_PATH];
			m_pMDLCache->UpdateOrCreate( m_pStudioHdr, pFileName, pX360Filename, sizeof( pX360Filename ), "GAME" );
			Q_strncpy( pFileName, pX360Filename, sizeof(pX360Filename) );
		}

		MdlCacheMsg( "MDLCache: Begin load VVD %s\n", pFileName );

		AsyncInfo_t info;
		if ( IsDebug() )
		{
			memset( &info, 0xdd, sizeof( AsyncInfo_t ) );
		}
		info.hModel = m_handle;
		info.type = MDLCACHE_VERTEXES;
		info.iAnimBlock = 0;
		info.hControl = NULL;
		m_pMDLCache->LoadData( pFileName, "GAME", mod_load_mesh_async.GetBool(), &info.hControl );
		{
			AUTO_LOCK(m_pMDLCache->m_AsyncMutex );
			iAsync = SetAsyncInfoIndex( m_handle, MDLCACHE_VERTEXES, m_pMDLCache->m_PendingAsyncs.AddToTail( info ) );
		}
	}

	m_pMDLCache->ProcessPendingAsync( iAsync );

	return (vertexFileHeader_t *)m_pMDLCache->CheckData( m_VertexCache, MDLCACHE_VERTEXES );
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
vertexFileHeader_t *CMDLCache::GetVertexData( MDLHandle_t handle )
{
	if ( mod_test_not_available.GetBool() )
		return NULL;

	if ( mod_test_verts_not_available.GetBool() )
		return NULL;

	CStudioHdr* pStudioHdr = GetStudioHdr(handle);
	if (!pStudioHdr) {
		return NULL;
	}

	return pStudioHdr->CacheVertexData();
}


//-----------------------------------------------------------------------------
// Allocates a cacheable item
//-----------------------------------------------------------------------------
void *CMDLCache::AllocData( MDLCacheDataType_t type, int size )
{
	void *pData = _aligned_malloc( size, 32 );

	if ( !pData )
	{
		Error( "CMDLCache:: Out of memory" );
		return NULL;
	}

	return pData;
}


//-----------------------------------------------------------------------------
// Caches an item
//-----------------------------------------------------------------------------
void CMDLCache::CacheData( DataCacheHandle_t *c, void *pData, int size, const char *name, MDLCacheDataType_t type, DataCacheClientID_t id )
{
	if ( !pData )
	{
		return;
	}

	if ( id == (DataCacheClientID_t)-1 )
		id = (DataCacheClientID_t)(intp)pData;

	GetCacheSection( type )->Add(id, pData, size, c );
}

//-----------------------------------------------------------------------------
// returns the cached data, and moves to the head of the LRU list
// if present, otherwise returns NULL
//-----------------------------------------------------------------------------
void *CMDLCache::CheckData( DataCacheHandle_t c, MDLCacheDataType_t type )
{
	return GetCacheSection( type )->Get( c, true );
}

//-----------------------------------------------------------------------------
// returns the cached data, if present, otherwise returns NULL
//-----------------------------------------------------------------------------
void *CMDLCache::CheckDataNoTouch( DataCacheHandle_t c, MDLCacheDataType_t type )
{
	return GetCacheSection( type )->GetNoTouch( c, true );
}

//-----------------------------------------------------------------------------
// Frees a cache item
//-----------------------------------------------------------------------------
void CMDLCache::UncacheData( DataCacheHandle_t c, MDLCacheDataType_t type, bool bLockedOk )
{
	if ( c == DC_INVALID_HANDLE )
		return;

	if ( !GetCacheSection( type )->IsPresent( c ) )
		return;

	if ( GetCacheSection( type )->BreakLock( c )  && !bLockedOk )
	{
		DevMsg( "Warning: freed a locked resource\n" );
		Assert( 0 );
	}

	const void *pItemData;
	GetCacheSection( type )->Remove( c, &pItemData );

	FreeData( type, (void *)pItemData );
}


//-----------------------------------------------------------------------------
// Frees memory for an item
//-----------------------------------------------------------------------------
void CMDLCache::FreeData( MDLCacheDataType_t type, void *pData )
{
	if ( type != MDLCACHE_ANIMBLOCK )
	{
		_aligned_free( (void *)pData );
	}
	else
	{
		g_pFullFileSystem->FreeOptimalReadBuffer( pData );
	}
}


void CMDLCache::InitPreloadData( bool rebuild )
{
}

void CMDLCache::ShutdownPreloadData()
{
}

//-----------------------------------------------------------------------------
// Work function for processing a model delivered by the queued loader.
// ProcessDataIntoCache() is invoked for each MDL datum.
//-----------------------------------------------------------------------------
void CMDLCache::ProcessQueuedData( ModelParts_t *pModelParts, bool bHeaderOnly )
{
	void *pData;
	int nSize;

	// the studiohdr is critical, ensure it's setup as expected
	MDLHandle_t handle = pModelParts->hMDL;
	CStudioHdr* studioHdr = m_MDLDict[handle];
	if ( !pModelParts->bHeaderLoaded && ( pModelParts->nLoadedParts & ( 1 << ModelParts_t::BUFFER_MDL ) ) )
	{
		DEBUG_SCOPE_TIMER(mdl);
		pData = pModelParts->Buffers[ModelParts_t::BUFFER_MDL].Base();
		nSize = pModelParts->Buffers[ModelParts_t::BUFFER_MDL].TellMaxPut();
		studioHdr->ProcessDataIntoCache( MDLCACHE_STUDIOHDR, 0, pData, nSize, nSize != 0 );
		LockStudioHdr( handle );
		g_pFullFileSystem->FreeOptimalReadBuffer( pData );
		pModelParts->bHeaderLoaded = true;
	}

	if ( bHeaderOnly )
	{
		return;
	}

	bool bAbort = false;
	studiohdr_t* pStudioHdr = NULL;
	pStudioHdr = (studiohdr_t *)CheckDataNoTouch( m_MDLDict[handle]->m_MDLCache, MDLCACHE_STUDIOHDR );
	if ( !pStudioHdr )
	{
		// The header is expected to be loaded and locked, everything depends on it!
		// but if the async read fails, we might not have it
		//Assert( 0 );
		DevWarning( "CMDLCache:: Error MDLCACHE_STUDIOHDR not present for '%s'\n", GetModelName( handle ) );

		// cannot unravel any of this model's dependant data, abort any further processing
		bAbort = true;
	}

	if ( pModelParts->nLoadedParts & ( 1 << ModelParts_t::BUFFER_PHY ) )
	{
		DEBUG_SCOPE_TIMER(phy);
		// regardless of error, call job callback so caller can do cleanup of their context
		pData = pModelParts->Buffers[ModelParts_t::BUFFER_PHY].Base();
		nSize = bAbort ? 0 : pModelParts->Buffers[ModelParts_t::BUFFER_PHY].TellMaxPut();
		studioHdr->ProcessDataIntoCache( MDLCACHE_VCOLLIDE, 0, pData, nSize, nSize != 0 );
		g_pFullFileSystem->FreeOptimalReadBuffer( pData );
	}

	// vvd vertexes before vtx
	if ( pModelParts->nLoadedParts & ( 1 << ModelParts_t::BUFFER_VVD ) )
	{
		DEBUG_SCOPE_TIMER(vvd);
		pData = pModelParts->Buffers[ModelParts_t::BUFFER_VVD].Base();
		nSize = bAbort ? 0 : pModelParts->Buffers[ModelParts_t::BUFFER_VVD].TellMaxPut();
		studioHdr->ProcessDataIntoCache( MDLCACHE_VERTEXES, 0, pData, nSize, nSize != 0 );
		g_pFullFileSystem->FreeOptimalReadBuffer( pData );
	}

	// can construct meshes after vvd and vtx vertexes arrive
	if ( pModelParts->nLoadedParts & ( 1 << ModelParts_t::BUFFER_VTX ) )
	{
		DEBUG_SCOPE_TIMER(vtx);
		pData = pModelParts->Buffers[ModelParts_t::BUFFER_VTX].Base();
		nSize = bAbort ?  0 : pModelParts->Buffers[ModelParts_t::BUFFER_VTX].TellMaxPut();

		// ProcessDataIntoCache() will do an unlock, so lock
		//CStudioHdr *pStudioData = m_MDLDict[handle];
		GetCacheSection( MDLCACHE_STUDIOHWDATA )->Lock(studioHdr->m_VertexCache );
		{
			// constructing the static meshes isn't thread safe
			AUTO_LOCK( m_QueuedLoadingMutex );
			studioHdr->ProcessDataIntoCache( MDLCACHE_STUDIOHWDATA, 0, pData, nSize, nSize != 0 );
		}
		g_pFullFileSystem->FreeOptimalReadBuffer( pData );
	}

	UnlockStudioHdr( handle );
	delete pModelParts;
}

//-----------------------------------------------------------------------------
// Journals each of the incoming MDL components until all arrive (or error).
// Not all components exist, but that information is not known at job submission.
//-----------------------------------------------------------------------------
void CMDLCache::QueuedLoaderCallback_MDL( void *pContext, void *pContext2, const void *pData, int nSize, LoaderError_t loaderError )
{
	// validity is denoted by a nonzero buffer
	nSize = ( loaderError == LOADERERROR_NONE ) ? nSize : 0;

	// journal each incoming buffer
	ModelParts_t *pModelParts = (ModelParts_t *)pContext;
	ModelParts_t::BufferType_t bufferType = static_cast< ModelParts_t::BufferType_t >((intp)pContext2);
	pModelParts->Buffers[bufferType].SetExternalBuffer( (void *)pData, nSize, nSize, CUtlBuffer::READ_ONLY );
	pModelParts->nLoadedParts += (1 << bufferType);

	// wait for all components
	if ( pModelParts->DoFinalProcessing() )
	{
		if ( !IsPC() )
		{
			// now have all components, process the raw data into the cache
			g_MDLCache.ProcessQueuedData( pModelParts );
		}
		else
		{
			// PC background load path. pull in material dependencies on the fly.
			Assert( ThreadInMainThread() );

			g_MDLCache.ProcessQueuedData( pModelParts, true );

			// preload all possible paths to VMTs
			{
				DEBUG_SCOPE_TIMER(findvmt);
				MaterialLock_t hMatLock = materials->Lock();
				if ( CStudioHdr * pHdr = g_MDLCache.GetStudioHdr( pModelParts->hMDL ))
				{
					if ( !(pHdr->flags() & STUDIOHDR_FLAGS_OBSOLETE))
					{
						char buf[MAX_PATH];
						V_strcpy( buf, "materials/" );
						int prefixLen = V_strlen( buf );
						
						for ( int t = 0; t < pHdr->numtextures(); ++t )
						{
							// XXX this does not take remaps from vtxdata into account;
							// right now i am not caring about that. we will hitch if any
							// LODs remap to materials that are not in the header. (henryg)
							const char *pTexture = pHdr->pTexture(t)->pszName();
							pTexture += ( pTexture[0] == CORRECT_PATH_SEPARATOR || pTexture[0] == INCORRECT_PATH_SEPARATOR );
							for ( int cd = 0; cd < pHdr->numcdtextures(); ++cd )
							{
								const char *pCdTexture = pHdr->pCdtexture( cd );
								pCdTexture += ( pCdTexture[0] == CORRECT_PATH_SEPARATOR || pCdTexture[0] == INCORRECT_PATH_SEPARATOR );
								V_ComposeFileName( pCdTexture, pTexture, buf + prefixLen, MAX_PATH - prefixLen );
								V_strncat( buf, ".vmt", MAX_PATH, COPY_ALL_CHARACTERS );
								pModelParts->bMaterialsPending = true;
								const char *pbuf = buf;
								g_pFullFileSystem->AddFilesToFileCache( pModelParts->hFileCache, &pbuf, 1, "GAME" );
								if ( materials->IsMaterialLoaded( buf + prefixLen ) )
								{
									// found a loaded one. still cache it in case it unloads,
									// but we can stop adding more potential paths to the cache
									// since this one is known to be valid.
									break;
								}
							}
						}
					}
				}
				materials->Unlock(hMatLock);
			}

			// queue functor which will start polling every frame by re-queuing itself
			g_pQueuedLoader->QueueDynamicLoadFunctor( CreateFunctor( ProcessDynamicLoad, pModelParts ) );
		}
	}
}

void CMDLCache::ProcessDynamicLoad( ModelParts_t *pModelParts )
{
	Assert( IsPC() && ThreadInMainThread() );

	if ( !g_pFullFileSystem->IsFileCacheLoaded( pModelParts->hFileCache ) )
	{
		// poll again next frame...
		g_pQueuedLoader->QueueDynamicLoadFunctor( CreateFunctor( ProcessDynamicLoad, pModelParts ) );
		return;
	}

	if ( pModelParts->bMaterialsPending )
	{
		DEBUG_SCOPE_TIMER(processvmt);
		pModelParts->bMaterialsPending = false;
		pModelParts->bTexturesPending = true;

		MaterialLock_t hMatLock = materials->Lock();
		materials->SetAsyncTextureLoadCache( pModelParts->hFileCache );

		// Load all the materials
		CStudioHdr * pHdr = g_MDLCache.GetStudioHdr( pModelParts->hMDL );
		if ( pHdr && !(pHdr->flags() & STUDIOHDR_FLAGS_OBSOLETE))
		{
			// build strings inside a buffer that already contains a materials/ prefix
			char buf[MAX_PATH];
			V_strcpy( buf, "materials/" );
			int prefixLen = V_strlen( buf );

			// XXX this does not take remaps from vtxdata into account;
			// right now i am not caring about that. we will hitch if any
			// LODs remap to materials that are not in the header. (henryg)
			for ( int t = 0; t < pHdr->numtextures(); ++t )
			{
				const char *pTexture = pHdr->pTexture(t)->pszName();
				pTexture += ( pTexture[0] == CORRECT_PATH_SEPARATOR || pTexture[0] == INCORRECT_PATH_SEPARATOR );
				for ( int cd = 0; cd < pHdr->numcdtextures(); ++cd )
				{
					const char *pCdTexture = pHdr->pCdtexture( cd );
					pCdTexture += ( pCdTexture[0] == CORRECT_PATH_SEPARATOR || pCdTexture[0] == INCORRECT_PATH_SEPARATOR );
					V_ComposeFileName( pCdTexture, pTexture, buf + prefixLen, MAX_PATH - prefixLen );
					IMaterial* pMaterial = materials->FindMaterial( buf + prefixLen, TEXTURE_GROUP_MODEL, false );
					if ( !IsErrorMaterial( pMaterial ) && !pMaterial->IsPrecached() )
					{
						pModelParts->Materials.AddToTail( pMaterial );
						pMaterial->IncrementReferenceCount();
						// Force texture loads while material system is set to capture
						// them and redirect to an error texture... this will populate
						// the file cache with all the requested textures
						pMaterial->RefreshPreservingMaterialVars();
						break;
					}
				}
			}
		}

		materials->SetAsyncTextureLoadCache( NULL );
		materials->Unlock( hMatLock );

		// poll again next frame... dont want to do too much work right now
		g_pQueuedLoader->QueueDynamicLoadFunctor( CreateFunctor( ProcessDynamicLoad, pModelParts ) );
		return;
	}
	
	if ( pModelParts->bTexturesPending )
	{
		DEBUG_SCOPE_TIMER(matrefresh);
		pModelParts->bTexturesPending = false;

		// Perform the real material loads now while raw texture files are cached.
		FOR_EACH_VEC( pModelParts->Materials, i )
		{
			IMaterial* pMaterial = pModelParts->Materials[i];
			if ( !IsErrorMaterial( pMaterial ) && pMaterial->IsPrecached() )
			{
				// Do a full reload to get the correct textures and computed flags
				pMaterial->Refresh();
			}
		}

		// poll again next frame... dont want to do too much work right now
		g_pQueuedLoader->QueueDynamicLoadFunctor( CreateFunctor( ProcessDynamicLoad, pModelParts ) );
		return;
	}

	// done. finish and clean up.
	Assert( !pModelParts->bTexturesPending && !pModelParts->bMaterialsPending );

	// pull out cached items we want to overlap with final processing
	CleanupModelParts_t *pCleanup = new CleanupModelParts_t;
	pCleanup->hFileCache = pModelParts->hFileCache;
	pCleanup->Materials.Swap( pModelParts->Materials );
	g_pQueuedLoader->QueueCleanupDynamicLoadFunctor( CreateFunctor( CleanupDynamicLoad, pCleanup ) );

	{
		DEBUG_SCOPE_TIMER(processall);
		g_MDLCache.ProcessQueuedData( pModelParts ); // pModelParts is deleted here
	}
}

void CMDLCache::CleanupDynamicLoad( CleanupModelParts_t *pCleanup )
{
	Assert( IsPC() && ThreadInMainThread() );

	// remove extra material refs, unload cached files
	FOR_EACH_VEC( pCleanup->Materials, i )
	{
		pCleanup->Materials[i]->DecrementReferenceCount();
	}

	g_pFullFileSystem->DestroyFileCache( pCleanup->hFileCache );

	delete pCleanup;
}

//-----------------------------------------------------------------------------
// Build a queued loader job to get the MDL ant all of its components into the cache.
//-----------------------------------------------------------------------------
bool CMDLCache::PreloadModel( MDLHandle_t handle )
{
	if ( g_pQueuedLoader->IsDynamic() == false )
	{
		if ( !IsX360() )
		{
			return false;
		}

		if ( !g_pQueuedLoader->IsMapLoading() || handle == MDLHANDLE_INVALID )
		{
			return false;
		}
	}

	if ( !g_pQueuedLoader->IsBatching() )
	{
		// batching must be active, following code depends on its behavior
		DevWarning( "CMDLCache:: Late preload of model '%s'\n", GetModelName( handle ) );
		return false;
	}

	// determine existing presence
	// actual necessity is not established here, allowable absent files need their i/o error to occur
	bool bNeedsMDL = !IsDataLoaded( handle, MDLCACHE_STUDIOHDR );
	bool bNeedsVTX = !IsDataLoaded( handle, MDLCACHE_STUDIOHWDATA );
	bool bNeedsVVD = !IsDataLoaded( handle, MDLCACHE_VERTEXES );
	bool bNeedsPHY = !IsDataLoaded( handle, MDLCACHE_VCOLLIDE );
	if ( !bNeedsMDL && !bNeedsVTX && !bNeedsVVD && !bNeedsPHY )
	{
		// nothing to do
		return true;
	}

	char szFilename[MAX_PATH];
	char szNameOnDisk[MAX_PATH];
	V_strncpy( szFilename, GetActualModelName( handle ), sizeof( szFilename ) );
	V_StripExtension( szFilename, szFilename, sizeof( szFilename ) );

	// need to gather all model parts (mdl, vtx, vvd, phy, ani)
	ModelParts_t *pModelParts = new ModelParts_t;
	pModelParts->hMDL = handle;
	pModelParts->hFileCache = g_pFullFileSystem->CreateFileCache();

	// create multiple loader jobs to perform gathering i/o operations
	LoaderJob_t loaderJob;
	loaderJob.m_pPathID = "GAME";
	loaderJob.m_pCallback = QueuedLoaderCallback_MDL;
	loaderJob.m_pContext = (void *)pModelParts;
	loaderJob.m_Priority = LOADERPRIORITY_DURINGPRELOAD;
	loaderJob.m_bPersistTargetData = true;

	if ( bNeedsMDL )
	{
		V_snprintf( szNameOnDisk, sizeof( szNameOnDisk ), "%s%s.mdl", szFilename, GetPlatformExt() );
		loaderJob.m_pFilename = szNameOnDisk;
		loaderJob.m_pContext2 = (void *)ModelParts_t::BUFFER_MDL;
		g_pQueuedLoader->AddJob( &loaderJob );
		pModelParts->nExpectedParts |= 1 << ModelParts_t::BUFFER_MDL;
	}

	if ( bNeedsVTX )
	{
		// vtx extensions are .xxx.vtx, need to re-form as, ???.xxx.yyy.vtx
		char szTempName[MAX_PATH];
		V_snprintf( szNameOnDisk, sizeof( szNameOnDisk ), "%s%s", szFilename, GetVTXExtension() );
		V_StripExtension( szNameOnDisk, szTempName, sizeof( szTempName ) );
		V_snprintf( szNameOnDisk, sizeof( szNameOnDisk ), "%s%s.vtx", szTempName, GetPlatformExt() );
		loaderJob.m_pFilename = szNameOnDisk;
		loaderJob.m_pContext2 = (void *)ModelParts_t::BUFFER_VTX;
		g_pQueuedLoader->AddJob( &loaderJob );
		pModelParts->nExpectedParts |= 1 << ModelParts_t::BUFFER_VTX;
	}

	if ( bNeedsVVD )
	{
		V_snprintf( szNameOnDisk, sizeof( szNameOnDisk ), "%s%s.vvd", szFilename, GetPlatformExt() );
		loaderJob.m_pFilename = szNameOnDisk;
		loaderJob.m_pContext2 = (void *)ModelParts_t::BUFFER_VVD;
		g_pQueuedLoader->AddJob( &loaderJob );
		pModelParts->nExpectedParts |= 1 << ModelParts_t::BUFFER_VVD;
	}

	if ( bNeedsPHY )
	{
		V_snprintf( szNameOnDisk, sizeof( szNameOnDisk ), "%s%s.phy", szFilename, GetPlatformExt() );
		loaderJob.m_pFilename = szNameOnDisk;
		loaderJob.m_pContext2 = (void *)ModelParts_t::BUFFER_PHY;
		g_pQueuedLoader->AddJob( &loaderJob );
		pModelParts->nExpectedParts |= 1 << ModelParts_t::BUFFER_PHY;
	}

	if ( !pModelParts->nExpectedParts )
	{
		g_pFullFileSystem->DestroyFileCache( pModelParts->hFileCache );
		delete pModelParts;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Clear the STUDIODATA_ERROR_MODEL flag.
//-----------------------------------------------------------------------------
void CMDLCache::ResetErrorModelStatus( MDLHandle_t handle )
{
	if ( handle == MDLHANDLE_INVALID )
		return;

	m_MDLDict[handle]->m_nFlags &= ~STUDIODATA_ERROR_MODEL;
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CMDLCache::MarkFrame()
{
	ProcessPendingAsyncs();
}

bool CMDLCache::ActivityList_Inited() {
	return m_bActivityInited;
}

void CMDLCache::ActivityList_Init(void)
{
	g_HighestActivity = 0;
	m_bActivityInited = true;
}

void CMDLCache::ActivityList_Clear(void) 
{
	g_ActivityStrings.ClearStrings();
	g_ActivityList.Purge();

	// So studiohdrs can reindex activity indices
	++g_nActivityListVersion;
}

void CMDLCache::ActivityList_Free(void)
{
	ActivityList_Clear();
	m_bActivityInited = false;
}

// add a new activity to the database
activitylist_t* CMDLCache::ActivityList_AddActivityEntry(const char* pName, int iActivityIndex, bool isPrivate)
{
	MEM_ALLOC_CREDIT();
	int index = g_ActivityList.AddToTail();
	activitylist_t* pList = &g_ActivityList[index];
	pList->activityIndex = iActivityIndex;
	pList->stringKey = g_ActivityStrings.AddString(pName, index);
	pList->isPrivate = isPrivate;

	// UNDONE: This implies that ALL shared activities are added before ANY custom activities
	// UNDONE: Segment these instead?  It's a 32-bit int, how many activities do we need?
	if (iActivityIndex > g_HighestActivity)
	{
		g_HighestActivity = iActivityIndex;
	}

	return pList;
}

// get the database entry from a string
activitylist_t* CMDLCache::ListFromString(const char* pString)
{
	// just use the string registry to do this search/map
	int stringID = g_ActivityStrings.GetStringID(pString);
	if (stringID < 0)
		return NULL;

	return &g_ActivityList[stringID];
}

// Get the database entry for an index
activitylist_t* CMDLCache::ListFromActivity(int activityIndex)
{
	// ugly linear search
	for (int i = 0; i < g_ActivityList.Size(); i++)
	{
		if (g_ActivityList[i].activityIndex == activityIndex)
		{
			return &g_ActivityList[i];
		}
	}

	return NULL;
}

bool CMDLCache::ActivityList_RegisterSharedActivity(const char* pszActivityName, int iActivityIndex)
{
	// UNDONE: Do we want to do these checks when not in developer mode? or maybe DEBUG only?
	// They really only matter when you change the list of code controlled activities.  IDs
	// for content controlled activities never collide because they are generated.

	// technically order isn't dependent, but it's too damn easy to forget to add new ACT_'s to all three lists.
	static int lastActivityIndex = -1;
	Assert((iActivityIndex == lastActivityIndex + 1 || iActivityIndex == 0));//iActivityIndex < LAST_SHARED_ACTIVITY && 
	lastActivityIndex = iActivityIndex;

	// first, check to make sure the slot we're asking for is free. It must be for 
	// a shared activity.
	activitylist_t* pList = ListFromString(pszActivityName);
	if (!pList)
	{
		pList = ListFromActivity(iActivityIndex);
	}

	if (pList)
	{
		if (!V_strcmp(pszActivityName, g_ActivityStrings.GetStringForKey(pList->stringKey)) && iActivityIndex == pList->activityIndex) {
			int aaa = 0;
		}
		else {
			Warning("***\nShared activity collision! %s<->%s\n***\n", pszActivityName, g_ActivityStrings.GetStringForKey(pList->stringKey));
			Assert(0);
		}
		return false;
	}
	// ----------------------------------------------------------------

	ActivityList_AddActivityEntry(pszActivityName, iActivityIndex, false);
	return true;
}

//#ifdef GAME_DLL
int CMDLCache::ActivityList_RegisterPrivateActivity(const char* pszActivityName)
{
	activitylist_t* pList = ListFromString(pszActivityName);
	if (pList)
	{
		// this activity is already in the list. If the activity we collided with is also private, 
		// then the collision is OK. Otherwise, it's a bug.
		if (pList->isPrivate)
		{
			return pList->activityIndex;
		}
		else
		{
			// this private activity collides with a shared activity. That is not allowed.
			Warning("***\nShared<->Private Activity collision!\n***\n");
			Assert(0);
			return -1;// ACT_INVALID;
		}
	}

	pList = ActivityList_AddActivityEntry(pszActivityName, g_HighestActivity + 1, true);
	return pList->activityIndex;
}
//#endif // GAME_DLL

// Get the index for a given activity name
// Done at load time for all models
int CMDLCache::ActivityList_IndexForName(const char* pszActivityName)
{
	// this is a fast O(lgn) search (actually does 2 O(lgn) searches)
	activitylist_t* pList = ListFromString(pszActivityName);

	if (pList)
	{
		return pList->activityIndex;
	}

	return -1;// kActivityLookup_Missing;
}

// Get the name for a given index
// This should only be used in debug code, it does a linear search
// But at least it only compares integers
const char* CMDLCache::ActivityList_NameForIndex(int activityIndex)
{
	activitylist_t* pList = ListFromActivity(activityIndex);
	if (pList)
	{
		return g_ActivityStrings.GetStringForKey(pList->stringKey);
	}
	return NULL;
}

int CMDLCache::ActivityList_HighestIndex()
{
	return g_HighestActivity;
}

void CMDLCache::EventList_Init(void)
{
	g_HighestEvent = 0;
}

void CMDLCache::EventList_Clear(void)
{
	g_EventStrings.ClearStrings();
	g_EventList.Purge();

	// So studiohdrs can reindex event indices
	++g_nEventListVersion;
}

void CMDLCache::EventList_Free(void)
{
	EventList_Clear();
}

// add a new event to the database
eventlist_t* CMDLCache::EventList_AddEventEntry(const char* pName, int iEventIndex, bool isPrivate, int iType)
{
	MEM_ALLOC_CREDIT();
	int index = g_EventList.AddToTail();
	eventlist_t* pList = &g_EventList[index];
	pList->eventIndex = iEventIndex;
	pList->stringKey = g_EventStrings.AddString(pName, index);
	pList->isPrivate = isPrivate;
	pList->iType = iType;

	// UNDONE: This implies that ALL shared activities are added before ANY custom activities
	// UNDONE: Segment these instead?  It's a 32-bit int, how many activities do we need?
	if (iEventIndex > g_HighestEvent)
	{
		g_HighestEvent = iEventIndex;
	}

	return pList;
}

// get the database entry from a string
eventlist_t* CMDLCache::EventList_ListFromString(const char* pString)
{
	// just use the string registry to do this search/map
	int stringID = g_EventStrings.GetStringID(pString);
	if (stringID < 0)
		return NULL;

	return &g_EventList[stringID];
}

// Get the database entry for an index
eventlist_t* CMDLCache::ListFromEvent(int eventIndex)
{
	// ugly linear search
	for (int i = 0; i < g_EventList.Size(); i++)
	{
		if (g_EventList[i].eventIndex == eventIndex)
		{
			return &g_EventList[i];
		}
	}

	return NULL;
}

int CMDLCache::EventList_GetEventType(int eventIndex)
{
	eventlist_t* pEvent = ListFromEvent(eventIndex);

	if (pEvent)
	{
		return pEvent->iType;
	}

	return -1;
}


bool CMDLCache::EventList_RegisterSharedEvent(const char* pszEventName, int iEventIndex, int iType)
{
	// UNDONE: Do we want to do these checks when not in developer mode? or maybe DEBUG only?
	// They really only matter when you change the list of code controlled activities.  IDs
	// for content controlled activities never collide because they are generated.

	// first, check to make sure the slot we're asking for is free. It must be for 
	// a shared event.
	eventlist_t* pList = EventList_ListFromString(pszEventName);
	if (!pList)
	{
		pList = ListFromEvent(iEventIndex);
	}

	//Already in list.
	if (pList)
	{
		return false;
	}
	// ----------------------------------------------------------------

	EventList_AddEventEntry(pszEventName, iEventIndex, false, iType);
	return true;
}

//#ifdef GAME_DLL
int CMDLCache::EventList_RegisterPrivateEvent(const char* pszEventName)
{
	eventlist_t* pList = EventList_ListFromString(pszEventName);
	if (pList)
	{
		// this activity is already in the list. If the activity we collided with is also private, 
		// then the collision is OK. Otherwise, it's a bug.
		if (pList->isPrivate)
		{
			return pList->eventIndex;
		}
		else
		{
			// this private activity collides with a shared activity. That is not allowed.
			Warning("***\nShared<->Private Event collision!\n***\n");
			Assert(0);
			return -1;// AE_INVALID;
		}
	}

	pList = EventList_AddEventEntry(pszEventName, g_HighestEvent + 1, true, 1 << 0);
	return pList->eventIndex;
}
//#endif // GAME_DLL

// Get the index for a given Event name
// Done at load time for all models
int CMDLCache::EventList_IndexForName(const char* pszEventName)
{
	// this is a fast O(lgn) search (actually does 2 O(lgn) searches)
	eventlist_t* pList = EventList_ListFromString(pszEventName);

	if (pList)
	{
		return pList->eventIndex;
	}

	return -1;
}

// Get the name for a given index
// This should only be used in debug code, it does a linear search
// But at least it only compares integers
const char* CMDLCache::EventList_NameForIndex(int eventIndex)
{
	eventlist_t* pList = ListFromEvent(eventIndex);
	if (pList)
	{
		return g_EventStrings.GetStringForKey(pList->stringKey);
	}
	return NULL;
}

void CMDLCache::SetEventIndexForSequence(mstudioseqdesc_t& seqdesc)
{
	if (&seqdesc == NULL)
		return;

	seqdesc.flags |= STUDIO_EVENT;

	if (seqdesc.numevents == 0)
		return;

	for (int index = 0; index < (int)seqdesc.numevents; index++)
	{
		mstudioevent_t* pevent = seqdesc.pEvent(index);

		if (!pevent)
			continue;

		if (pevent->type & AE_TYPE_NEWEVENTSYSTEM)
		{
			const char* pEventName = pevent->pszEventName();

			int iEventIndex = mdlcache->EventList_IndexForName(pEventName);

			if (iEventIndex == -1)
			{
#ifdef CLIENT_DLL
				Error("can not happen!");
#endif // CLIENT_DLL
//#ifdef GAME_DLL
				pevent->event = mdlcache->EventList_RegisterPrivateEvent(pEventName);
//#endif // GAME_DLL
			}
			else
			{
				pevent->event = iEventIndex;
				pevent->type |= mdlcache->EventList_GetEventType(iEventIndex);
			}
		}
	}
}

mstudioevent_t* CMDLCache::GetEventIndexForSequence(mstudioseqdesc_t& seqdesc)
{
	if (!(seqdesc.flags & STUDIO_EVENT))
	{
		SetEventIndexForSequence(seqdesc);
	}

	return seqdesc.pEvent(0);
}

//-----------------------------------------------------------------------------
// Purpose: bind studiohdr_t support functions to the mdlcacher
//-----------------------------------------------------------------------------
//const studiohdr_t *studiohdr_t::FindModel( void **cache, char const *pModelName ) const
//{
//	MDLHandle_t handle = g_MDLCache.FindMDL( pModelName );
//	*cache = MDLHandleToVirtual(handle);
//	return g_MDLCache.GetStudioHdrInternal( handle );
//}

//CVirtualModel *studiohdr_t::GetVirtualModel( void ) const
//{
//	if (numincludemodels == 0)
//		return NULL;
//
//    return g_MDLCache.GetVirtualModelFast( this, VoidPtrToMDLHandle( VirtualModel() ) );
//}

//byte *studiohdr_t::GetAnimBlock( int i ) const
//{
//	return g_MDLCache.GetAnimBlock( VoidPtrToMDLHandle( VirtualModel() ), i );
//}

//int studiohdr_t::GetAutoplayList( unsigned short **pOut ) const
//{
//	return g_MDLCache.GetAutoplayList( VoidPtrToMDLHandle( VirtualModel() ), pOut );
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//mstudioanimdesc_t& studiohdr_t::pAnimdesc(int i) const
//{
//	if (numincludemodels == 0)
//	{
//		return *pLocalAnimdesc(i);
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	virtualgroup_t* pGroup = pVModel->pAnimGroup(i);// &pVModel->m_group[pVModel->m_anim[i].group];
//	const studiohdr_t* pStudioHdr = pVModel->GetGroupStudioHdr(pGroup);
//	Assert(pStudioHdr);
//
//	return *pStudioHdr->pLocalAnimdesc(pVModel->pAnimIndex(i));//m_anim[i].index
//}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//bool studiohdr_t::SequencesAvailable() const
//{
//	if (numincludemodels == 0)
//	{
//		return true;
//	}
//
//	return (GetVirtualModel() != NULL);
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int studiohdr_t::GetNumSeq(void) const
//{
//	if (numincludemodels == 0)
//	{
//		return numlocalseq;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//	return pVModel->NumSeq();// m_seq.Count();
//}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//mstudioseqdesc_t& studiohdr_t::pSeqdesc(int i) const
//{
//	if (numincludemodels == 0)
//	{
//		return *pLocalSeqdesc(i);
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	if (!pVModel)
//	{
//		return *pLocalSeqdesc(i);
//	}
//
//	virtualgroup_t* pGroup = pVModel->pSeqGroup(i);// &pVModel->m_group[pVModel->m_seq[i].group];
//	const studiohdr_t* pStudioHdr = pVModel->GetGroupStudioHdr(pGroup);
//	Assert(pStudioHdr);
//
//	return *pStudioHdr->pLocalSeqdesc(pVModel->pSeqIndex(i));// m_seq[i].index
//}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int studiohdr_t::iRelativeAnim(int baseseq, int relanim) const
//{
//	if (numincludemodels == 0)
//	{
//		return relanim;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	virtualgroup_t* pGroup = pVModel->pSeqGroup(baseseq);// & pVModel->m_group[pVModel->m_seq[baseseq].group];
//
//	return pGroup->masterAnim[relanim];
//}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int studiohdr_t::iRelativeSeq(int baseseq, int relseq) const
//{
//	if (numincludemodels == 0)
//	{
//		return relseq;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	virtualgroup_t* pGroup = pVModel->pSeqGroup(baseseq);// &pVModel->m_group[pVModel->m_seq[baseseq].group];
//
//	return pGroup->masterSeq[relseq];
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int	studiohdr_t::GetNumPoseParameters(void) const
//{
//	if (numincludemodels == 0)
//	{
//		return numlocalposeparameters;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	return pVModel->NumPose();// m_pose.Count();
//}



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//const mstudioposeparamdesc_t& studiohdr_t::pPoseParameter(int i)
//{
//	if (numincludemodels == 0)
//	{
//		return *pLocalPoseParameter(i);
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	if (pVModel->nPoseGroup(i) == 0)// m_pose[i].group
//		return *pLocalPoseParameter(pVModel->pPoseIndex(i));// m_pose[i].index
//
//	virtualgroup_t* pGroup = pVModel->pPoseGroup(i);// &pVModel->m_group[pVModel->m_pose[i].group];
//
//	const studiohdr_t* pStudioHdr = pVModel->GetGroupStudioHdr(pGroup);
//	Assert(pStudioHdr);
//
//	return *pStudioHdr->pLocalPoseParameter(pVModel->pPoseIndex(i));// m_pose[i].index
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int studiohdr_t::GetSharedPoseParameter(int iSequence, int iLocalPose) const
//{
//	if (numincludemodels == 0)
//	{
//		return iLocalPose;
//	}
//
//	if (iLocalPose == -1)
//		return iLocalPose;
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	virtualgroup_t* pGroup = pVModel->pSeqGroup(iSequence);// &pVModel->m_group[pVModel->m_seq[iSequence].group];
//
//	return pGroup->masterPose[iLocalPose];
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int studiohdr_t::EntryNode(int iSequence)
//{
//	mstudioseqdesc_t& seqdesc = pSeqdesc(iSequence);
//
//	if (numincludemodels == 0 || seqdesc.localentrynode == 0)
//	{
//		return seqdesc.localentrynode;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	virtualgroup_t* pGroup = pVModel->pSeqGroup(iSequence);// &pVModel->m_group[pVModel->m_seq[iSequence].group];
//
//	return pGroup->masterNode[seqdesc.localentrynode - 1] + 1;
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//int studiohdr_t::ExitNode(int iSequence)
//{
//	mstudioseqdesc_t& seqdesc = pSeqdesc(iSequence);
//
//	if (numincludemodels == 0 || seqdesc.localexitnode == 0)
//	{
//		return seqdesc.localexitnode;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	virtualgroup_t* pGroup = pVModel->pSeqGroup(iSequence);// &pVModel->m_group[pVModel->m_seq[iSequence].group];
//
//	return pGroup->masterNode[seqdesc.localexitnode - 1] + 1;
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int	studiohdr_t::GetNumAttachments(void) const
//{
//	if (numincludemodels == 0)
//	{
//		return numlocalattachments;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	return pVModel->NumAttachment();// m_attachment.Count();
//}



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//const mstudioattachment_t& studiohdr_t::pAttachment(int i) const
//{
//	if (numincludemodels == 0)
//	{
//		return *pLocalAttachment(i);
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	virtualgroup_t* pGroup = pVModel->pAttachmentGroup(i);// &pVModel->m_group[pVModel->m_attachment[i].group];
//	const studiohdr_t* pStudioHdr = pVModel->GetGroupStudioHdr(pGroup);
//	Assert(pStudioHdr);
//
//	return *pStudioHdr->pLocalAttachment(pVModel->pAttachmentIndex(i));// m_attachment[i].index
//}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int	studiohdr_t::GetAttachmentBone(int i)
//{
//	const mstudioattachment_t& attachment = pAttachment(i);
//
//	// remap bone
//	CVirtualModel* pVModel = GetVirtualModel();
//	if (pVModel)
//	{
//		virtualgroup_t* pGroup = pVModel->pAttachmentGroup(i);// &pVModel->m_group[pVModel->m_attachment[i].group];
//		int iBone = pGroup->masterBone[attachment.localbone];
//		if (iBone == -1)
//			return 0;
//		return iBone;
//	}
//	return attachment.localbone;
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//void studiohdr_t::SetAttachmentBone(int iAttachment, int iBone)
//{
//	mstudioattachment_t& attachment = (mstudioattachment_t&)pAttachment(iAttachment);
//
//	// remap bone
//	CVirtualModel* pVModel = GetVirtualModel();
//	if (pVModel)
//	{
//		virtualgroup_t* pGroup = pVModel->pAttachmentGroup(iAttachment);// &pVModel->m_group[pVModel->m_attachment[iAttachment].group];
//		iBone = pGroup->boneMap[iBone];
//	}
//	attachment.localbone = iBone;
//}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//char* studiohdr_t::pszNodeName(int iNode)
//{
//	if (numincludemodels == 0)
//	{
//		return pszLocalNodeName(iNode);
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	if (pVModel->NumNode() <= iNode - 1)// m_node.Count()
//		return "Invalid node";
//	//pVModel->m_group[ pVModel->m_node[iNode-1].group ]
//	return pVModel->GetGroupStudioHdr(pVModel->pNodeGroup(iNode - 1))->pszLocalNodeName(pVModel->pNodeIndex(iNode - 1));// m_node[iNode - 1].index
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//int studiohdr_t::GetTransition(int iFrom, int iTo) const
//{
//	if (numincludemodels == 0)
//	{
//		return *pLocalTransition((iFrom - 1) * numlocalnodes + (iTo - 1));
//	}
//
//	return iTo;
//	/*
//	FIXME: not connected
//	CVirtualModel *pVModel = GetVirtualModel();
//	Assert( pVModel );
//
//	return pVModel->m_transition.Element( iFrom ).Element( iTo );
//	*/
//}


//int	studiohdr_t::GetActivityListVersion(void)
//{
//	if (numincludemodels == 0)
//	{
//		return activitylistversion;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	int ActVersion = activitylistversion;
//
//	int i;
//	for (i = 1; i < pVModel->NumGroup(); i++)//m_group.Count()
//	{
//		virtualgroup_t* pGroup = pVModel->pGroup(i);// &pVModel->m_group[i];
//		const studiohdr_t* pStudioHdr = pVModel->GetGroupStudioHdr(pGroup);
//
//		Assert(pStudioHdr);
//
//		ActVersion = min(ActVersion, pStudioHdr->activitylistversion);
//	}
//
//	return ActVersion;
//}

//void studiohdr_t::SetActivityListVersion(int ActVersion) const
//{
//	activitylistversion = ActVersion;
//
//	if (numincludemodels == 0)
//	{
//		return;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	int i;
//	for (i = 1; i < pVModel->NumGroup(); i++)//m_group.Count()
//	{
//		virtualgroup_t* pGroup = pVModel->pGroup(i);// &pVModel->m_group[i];
//		const studiohdr_t* pStudioHdr = pVModel->GetGroupStudioHdr(pGroup);
//
//		Assert(pStudioHdr);
//
//		pStudioHdr->SetActivityListVersion(ActVersion);
//	}
//}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//int studiohdr_t::GetNumIKAutoplayLocks(void) const
//{
//	if (numincludemodels == 0)
//	{
//		return numlocalikautoplaylocks;
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	return pVModel->NumIKlock();// m_iklock.Count();
//}

//const mstudioiklock_t& studiohdr_t::pIKAutoplayLock(int i)
//{
//	if (numincludemodels == 0)
//	{
//		return *pLocalIKAutoplayLock(i);
//	}
//
//	CVirtualModel* pVModel = GetVirtualModel();
//	Assert(pVModel);
//
//	virtualgroup_t* pGroup = pVModel->pIKlockGroup(i);// &pVModel->m_group[pVModel->m_iklock[i].group];
//	const studiohdr_t* pStudioHdr = pVModel->GetGroupStudioHdr(pGroup);
//	Assert(pStudioHdr);
//
//	return *pStudioHdr->pLocalIKAutoplayLock(pVModel->pIKlockIndex(i));// m_iklock[i].index
//}

//int	studiohdr_t::CountAutoplaySequences() const
//{
//	int count = 0;
//	for (int i = 0; i < GetNumSeq(); i++)
//	{
//		mstudioseqdesc_t& seqdesc = pSeqdesc(i);
//		if (seqdesc.flags & STUDIO_AUTOPLAY)
//		{
//			count++;
//		}
//	}
//	return count;
//}

//int	studiohdr_t::CopyAutoplaySequences(unsigned short* pOut, int outCount) const
//{
//	int outIndex = 0;
//	for (int i = 0; i < GetNumSeq() && outIndex < outCount; i++)
//	{
//		mstudioseqdesc_t& seqdesc = pSeqdesc(i);
//		if (seqdesc.flags & STUDIO_AUTOPLAY)
//		{
//			pOut[outIndex] = i;
//			outIndex++;
//		}
//	}
//	return outIndex;
//}

//-----------------------------------------------------------------------------
// Purpose:	maps local sequence bone to global bone
//-----------------------------------------------------------------------------

//int	studiohdr_t::RemapSeqBone(int iSequence, int iLocalBone) const
//{
//	// remap bone
//	CVirtualModel* pVModel = GetVirtualModel();
//	if (pVModel)
//	{
//		const virtualgroup_t* pSeqGroup = pVModel->pSeqGroup(iSequence);
//		return pSeqGroup->masterBone[iLocalBone];
//	}
//	return iLocalBone;
//}

//int	studiohdr_t::RemapAnimBone(int iAnim, int iLocalBone) const
//{
//	// remap bone
//	CVirtualModel* pVModel = GetVirtualModel();
//	if (pVModel)
//	{
//		const virtualgroup_t* pAnimGroup = pVModel->pAnimGroup(iAnim);
//		return pAnimGroup->masterBone[iLocalBone];
//	}
//	return iLocalBone;
//}

const studiohdr_t* CVirtualModel::GetGroupStudioHdr(virtualgroup_t* pGroup) const
{
	return g_MDLCache.GetStudioHdr(VoidPtrToMDLHandle(pGroup->cache))->m_pStudioHdr;
}

void CVirtualModel::AppendModels(int group, const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);

	// build a search table if necesary
	CModelLookupContext ctx(group, pStudioHdr);

	AppendSequences(group, pStudioHdr);
	AppendAnimations(group, pStudioHdr);
	AppendBonemap(group, pStudioHdr);
	AppendAttachments(group, pStudioHdr);
	AppendPoseParameters(group, pStudioHdr);
	AppendNodes(group, pStudioHdr);
	AppendIKLocks(group, pStudioHdr);

	struct HandleAndHeader_t
	{
		void* handle;
		const CStudioHdr* pHdr;
	};
	HandleAndHeader_t list[64];

	// determine quantity of valid include models in one pass only
	// temporarily cache results off, otherwise FindModel() causes ref counting problems
	int j;
	int nValidIncludes = 0;
	for (j = 0; j < pStudioHdr->numincludemodels(); j++)
	{
		// find model (increases ref count)
		MDLHandle_t handle = g_MDLCache.FindMDL(pStudioHdr->pModelGroup(j)->pszName());
		void* tmp = MDLHandleToVirtual(handle);
		const CStudioHdr* pTmpHdr = g_MDLCache.GetStudioHdr(handle);
		if (pTmpHdr)
		{
			if (nValidIncludes >= ARRAYSIZE(list))
			{
				// would cause stack overflow
				Assert(0);
				break;
			}

			list[nValidIncludes].handle = tmp;
			list[nValidIncludes].pHdr = pTmpHdr;
			nValidIncludes++;
		}
	}

	if (nValidIncludes)
	{
		m_group.EnsureCapacity(m_group.Count() + nValidIncludes);
		for (j = 0; j < nValidIncludes; j++)
		{
			MEM_ALLOC_CREDIT();
			int group = m_group.AddToTail();
			m_group[group].cache = list[j].handle;
			AppendModels(group, list[j].pHdr);
		}
		m_pStudioHdrCache.SetCount(m_group.Count());

		int i;
		for (i = 0; i < m_pStudioHdrCache.Count(); i++)
		{
			m_pStudioHdrCache[i] = NULL;
		}
	}

	if (g_pMDLCache)
	{
		m_pFrameUnlockCounter = g_pMDLCache->GetFrameUnlockCounterPtr(MDLCACHE_STUDIOHDR);
		m_nFrameUnlockCounter = *m_pFrameUnlockCounter - 1;
	}
	UpdateAutoplaySequences(pStudioHdr);
}

void CVirtualModel::AppendSequences(int group, const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);
	int numCheck = m_seq.Count();

	int j, k;

	MEM_ALLOC_CREDIT();

	CUtlVector< virtualsequence_t > seq;

	seq = m_seq;

	m_group[group].masterSeq.SetCount(pStudioHdr->numlocalseq());

	for (j = 0; j < pStudioHdr->numlocalseq(); j++)
	{
		const mstudioseqdesc_t* seqdesc = pStudioHdr->pLocalSeqdesc(j);
		char* s1 = seqdesc->pszLabel();

		if (HasLookupTable())
		{
			k = numCheck;
			short index = GetSeqTable()->Find(s1);
			if (index != GetSeqTable()->InvalidIndex())
			{
				k = GetSeqTable()->Element(index);
			}
		}
		else
		{
			for (k = 0; k < numCheck; k++)
			{
				const studiohdr_t* hdr = GetGroupStudioHdr(&m_group[seq[k].group]);
				char* s2 = hdr->pLocalSeqdesc(seq[k].index)->pszLabel();
				if (!stricmp(s1, s2))
				{
					break;
				}
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualsequence_t tmp;
			tmp.group = group;
			tmp.index = j;
			tmp.flags = seqdesc->flags;
			tmp.activity = seqdesc->activity;
			k = seq.AddToTail(tmp);
		}
		else if (GetGroupStudioHdr(&m_group[seq[k].group])->pLocalSeqdesc(seq[k].index)->flags & STUDIO_OVERRIDE)
		{
			// the one in memory is a forward declared sequence, override it
			virtualsequence_t tmp;
			tmp.group = group;
			tmp.index = j;
			tmp.flags = seqdesc->flags;
			tmp.activity = seqdesc->activity;
			seq[k] = tmp;
		}
		m_group[group].masterSeq[j] = k;
	}

	if (HasLookupTable())
	{
		for (j = numCheck; j < seq.Count(); j++)
		{
			const studiohdr_t* hdr = GetGroupStudioHdr(&m_group[seq[j].group]);
			const char* s1 = hdr->pLocalSeqdesc(seq[j].index)->pszLabel();
			GetSeqTable()->Insert(s1, j);
		}
	}

	m_seq = seq;
}


void CVirtualModel::UpdateAutoplaySequences(const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);
	int autoplayCount = pStudioHdr->CountAutoplaySequences();
	m_autoplaySequences.SetCount(autoplayCount);
	pStudioHdr->CopyAutoplaySequences(m_autoplaySequences.Base(), autoplayCount);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void CVirtualModel::AppendAnimations(int group, const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);
	int numCheck = m_anim.Count();

	CUtlVector< virtualgeneric_t > anim;
	anim = m_anim;

	MEM_ALLOC_CREDIT();

	int j, k;

	m_group[group].masterAnim.SetCount(pStudioHdr->numlocalanim());

	for (j = 0; j < pStudioHdr->numlocalanim(); j++)
	{
		char* s1 = pStudioHdr->pLocalAnimdesc(j)->pszName();
		if (HasLookupTable())
		{
			k = numCheck;
			short index = GetAnimTable()->Find(s1);
			if (index != GetAnimTable()->InvalidIndex())
			{
				k = GetAnimTable()->Element(index);
			}
		}
		else
		{
			for (k = 0; k < numCheck; k++)
			{
				char* s2 = GetGroupStudioHdr(&m_group[anim[k].group])->pLocalAnimdesc(anim[k].index)->pszName();
				if (stricmp(s1, s2) == 0)
				{
					break;
				}
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = anim.AddToTail(tmp);
		}

		m_group[group].masterAnim[j] = k;
	}

	if (HasLookupTable())
	{
		for (j = numCheck; j < anim.Count(); j++)
		{
			const char* s1 = GetGroupStudioHdr(&m_group[anim[j].group])->pLocalAnimdesc(anim[j].index)->pszName();
			GetAnimTable()->Insert(s1, j);
		}
	}

	m_anim = anim;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void CVirtualModel::AppendBonemap(int group, const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);
	MEM_ALLOC_CREDIT();

	const studiohdr_t* pBaseStudioHdr = GetGroupStudioHdr(&m_group[0]);

	m_group[group].boneMap.SetCount(pBaseStudioHdr->numbones);
	m_group[group].masterBone.SetCount(pStudioHdr->numbones());

	int j, k;

	if (group == 0)
	{
		for (j = 0; j < pStudioHdr->numbones(); j++)
		{
			m_group[group].boneMap[j] = j;
			m_group[group].masterBone[j] = j;
		}
	}
	else
	{
		for (j = 0; j < pBaseStudioHdr->numbones; j++)
		{
			m_group[group].boneMap[j] = -1;
		}
		for (j = 0; j < pStudioHdr->numbones(); j++)
		{
			// NOTE: studiohdr has a bone table - using the table is ~5% faster than this for alyx.mdl on a P4/3.2GHz
			for (k = 0; k < pBaseStudioHdr->numbones; k++)
			{
				if (stricmp(pStudioHdr->pBone(j)->pszName(), pBaseStudioHdr->pBone(k)->pszName()) == 0)
				{
					break;
				}
			}
			if (k < pBaseStudioHdr->numbones)
			{
				m_group[group].masterBone[j] = k;
				m_group[group].boneMap[k] = j;

				// FIXME: these runtime messages don't display in hlmv
				if ((pStudioHdr->pBone(j)->parent == -1) || (pBaseStudioHdr->pBone(k)->parent == -1))
				{
					if ((pStudioHdr->pBone(j)->parent != -1) || (pBaseStudioHdr->pBone(k)->parent != -1))
					{
						Warning("%s/%s : missmatched parent bones on \"%s\"\n", pBaseStudioHdr->pszName(), pStudioHdr->pszName(), pStudioHdr->pBone(j)->pszName());
					}
				}
				else if (m_group[group].masterBone[pStudioHdr->pBone(j)->parent] != m_group[0].masterBone[pBaseStudioHdr->pBone(k)->parent])
				{
					Warning("%s/%s : missmatched parent bones on \"%s\"\n", pBaseStudioHdr->pszName(), pStudioHdr->pszName(), pStudioHdr->pBone(j)->pszName());
				}
			}
			else
			{
				m_group[group].masterBone[j] = -1;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void CVirtualModel::AppendAttachments(int group, const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);
	int numCheck = m_attachment.Count();

	CUtlVector< virtualgeneric_t > attachment;
	attachment = m_attachment;

	MEM_ALLOC_CREDIT();

	int j, k, n;

	m_group[group].masterAttachment.SetCount(pStudioHdr->numlocalattachments());

	for (j = 0; j < pStudioHdr->numlocalattachments(); j++)
	{

		n = m_group[group].masterBone[pStudioHdr->pLocalAttachment(j)->localbone];

		// skip if the attachments bone doesn't exist in the root model
		if (n == -1)
		{
			continue;
		}


		char* s1 = pStudioHdr->pLocalAttachment(j)->pszName();
		for (k = 0; k < numCheck; k++)
		{
			char* s2 = GetGroupStudioHdr(&m_group[attachment[k].group])->pLocalAttachment(attachment[k].index)->pszName();

			if (stricmp(s1, s2) == 0)
			{
				break;
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = attachment.AddToTail(tmp);

			// make sure bone flags are set so attachment calculates
			if ((GetGroupStudioHdr(&m_group[0])->pBone(n)->flags & BONE_USED_BY_ATTACHMENT) == 0)
			{
				while (n != -1)
				{
					GetGroupStudioHdr(&m_group[0])->pBone(n)->flags |= BONE_USED_BY_ATTACHMENT;

					if (GetGroupStudioHdr(&m_group[0])->pLinearBones())
					{
						*GetGroupStudioHdr(&m_group[0])->pLinearBones()->pflags(n) |= BONE_USED_BY_ATTACHMENT;
					}

					n = GetGroupStudioHdr(&m_group[0])->pBone(n)->parent;
				}
				continue;
			}
		}

		m_group[group].masterAttachment[j] = k;
	}

	m_attachment = attachment;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void CVirtualModel::AppendPoseParameters(int group, const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);
	int numCheck = m_pose.Count();

	CUtlVector< virtualgeneric_t > pose;
	pose = m_pose;

	MEM_ALLOC_CREDIT();

	int j, k;

	m_group[group].masterPose.SetCount(pStudioHdr->numlocalposeparameters());

	for (j = 0; j < pStudioHdr->numlocalposeparameters(); j++)
	{
		char* s1 = pStudioHdr->pLocalPoseParameter(j)->pszName();
		for (k = 0; k < numCheck; k++)
		{
			char* s2 = GetGroupStudioHdr(&m_group[pose[k].group])->pLocalPoseParameter(pose[k].index)->pszName();

			if (stricmp(s1, s2) == 0)
			{
				break;
			}
		}
		if (k == numCheck)
		{
			// no duplication
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = pose.AddToTail(tmp);
		}
		else
		{
			// duplicate, reset start and end to fit full dynamic range
			mstudioposeparamdesc_t* pPose1 = pStudioHdr->pLocalPoseParameter(j);
			mstudioposeparamdesc_t* pPose2 = GetGroupStudioHdr(&m_group[pose[k].group])->pLocalPoseParameter(pose[k].index);
			float start = min(pPose2->end, min(pPose1->end, min(pPose2->start, pPose1->start)));
			float end = max(pPose2->end, max(pPose1->end, max(pPose2->start, pPose1->start)));
			pPose2->start = start;
			pPose2->end = end;
		}

		m_group[group].masterPose[j] = k;
	}

	m_pose = pose;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void CVirtualModel::AppendNodes(int group, const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);
	int numCheck = m_node.Count();

	CUtlVector< virtualgeneric_t > node;
	node = m_node;

	MEM_ALLOC_CREDIT();

	int j, k;

	m_group[group].masterNode.SetCount(pStudioHdr->numlocalnodes());

	for (j = 0; j < pStudioHdr->numlocalnodes(); j++)
	{
		char* s1 = pStudioHdr->pszLocalNodeName(j);
		for (k = 0; k < numCheck; k++)
		{
			char* s2 = GetGroupStudioHdr(&m_group[node[k].group])->pszLocalNodeName(node[k].index);

			if (stricmp(s1, s2) == 0)
			{
				break;
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = node.AddToTail(tmp);
		}

		m_group[group].masterNode[j] = k;
	}

	m_node = node;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


void CVirtualModel::AppendIKLocks(int group, const CStudioHdr* pStudioHdr)
{
	AUTO_LOCK(m_Lock);
	int numCheck = m_iklock.Count();

	CUtlVector< virtualgeneric_t > iklock;
	iklock = m_iklock;

	int j, k;

	for (j = 0; j < pStudioHdr->numlocalikautoplaylocks(); j++)
	{
		int chain1 = pStudioHdr->pLocalIKAutoplayLock(j)->chain;
		for (k = 0; k < numCheck; k++)
		{
			int chain2 = GetGroupStudioHdr(&m_group[iklock[k].group])->pLocalIKAutoplayLock(iklock[k].index)->chain;

			if (chain1 == chain2)
			{
				break;
			}
		}
		// no duplication
		if (k == numCheck)
		{
			MEM_ALLOC_CREDIT();

			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = iklock.AddToTail(tmp);
		}
	}

	m_iklock = iklock;

	// copy knee directions for uninitialized knees
	if (group != 0)
	{
		studiohdr_t* pBaseHdr = (studiohdr_t*)GetGroupStudioHdr(&m_group[0]);
		if (pStudioHdr->numikchains() == pBaseHdr->numikchains)
		{
			for (j = 0; j < pStudioHdr->numikchains(); j++)
			{
				if (pBaseHdr->pIKChain(j)->pLink(0)->kneeDir.LengthSqr() == 0.0f)
				{
					if (pStudioHdr->pIKChain(j)->pLink(0)->kneeDir.LengthSqr() > 0.0f)
					{
						pBaseHdr->pIKChain(j)->pLink(0)->kneeDir = pStudioHdr->pIKChain(j)->pLink(0)->kneeDir;
					}
				}
			}
		}
	}
}

int CStudioHdr::ExtractBbox(int sequence, Vector& mins, Vector& maxs)
{
	if (!this)
		return 0;

	if (!this->SequencesAvailable())
		return 0;

	mstudioseqdesc_t& seqdesc = this->pSeqdesc(sequence);

	mins = seqdesc.bbmin;

	maxs = seqdesc.bbmax;

	return 1;
}

void CStudioHdr::BuildAllAnimationEventIndexes()
{
	if (!this)
		return;

	if (this->GetEventListVersion() != mdlcache->EventListVersion())
	{
		for (int i = 0; i < this->GetNumSeq(); i++)
		{
			mdlcache->SetEventIndexForSequence(this->pSeqdesc(i));
		}

		this->SetEventListVersion(mdlcache->EventListVersion());
	}
}

//-----------------------------------------------------------------------------
// Purpose: Ensures that activity / index relationship is recalculated
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CStudioHdr::ResetEventIndexes()
{
	if (!this)
		return;

	this->SetEventListVersion(mdlcache->EventListVersion() - 1);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CStudioHdr::SetActivityForSequence(int i)
{
	int iActivityIndex;
	const char* pszActivityName;
	mstudioseqdesc_t& seqdesc = this->pSeqdesc(i);

	seqdesc.flags |= STUDIO_ACTIVITY;

	pszActivityName = GetSequenceActivityName(i);
	if (pszActivityName[0] != '\0')
	{
		iActivityIndex = mdlcache->ActivityList_IndexForName(pszActivityName);

		if (iActivityIndex == -1)
		{
			// Allow this now.  Animators can create custom activities that are referenced only on the client or by scripts, etc.
			//Warning( "***\nModel %s tried to reference unregistered activity: %s \n***\n", pstudiohdr->name, pszActivityName );
			//Assert(0);
			// HACK: the client and server don't share the private activity list so registering it on the client would hose the server
#ifdef CLIENT_DLL
			seqdesc.flags &= ~STUDIO_ACTIVITY;
#else
			seqdesc.activity = mdlcache->ActivityList_RegisterPrivateActivity(pszActivityName);
#endif
		}
		else
		{
			seqdesc.activity = iActivityIndex;
		}
	}
}

//=========================================================
// IndexModelSequences - set activity and event indexes for all model
// sequences that have them.
//=========================================================

void CStudioHdr::IndexModelSequences()
{
	int i;

	if (!this)
		return;

	if (!this->SequencesAvailable())
		return;

	for (i = 0; i < this->GetNumSeq(); i++)
	{
		SetActivityForSequence(i);
		mdlcache->SetEventIndexForSequence(this->pSeqdesc(i));
	}

	this->SetActivityListVersion(mdlcache->ActivityListVersion());
}

//-----------------------------------------------------------------------------
// Purpose: Ensures that activity / index relationship is recalculated
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CStudioHdr::ResetActivityIndexes()
{
	if (!this)
		return;

	this->SetActivityListVersion(mdlcache->ActivityListVersion() - 1);
}

void CStudioHdr::VerifySequenceIndex()
{
	if (!this)
	{
		return;
	}

	if (this->GetActivityListVersion() != mdlcache->ActivityListVersion())
	{
		// this model's sequences have not yet been indexed by activity
		IndexModelSequences();
	}
}

int CStudioHdr::SelectWeightedSequence(int activity, int curSequence, RandomWeightFunc pRandomWeightFunc)
{
	VPROF("SelectWeightedSequence");

	if (!this)
		return 0;

	if (!this->SequencesAvailable())
		return 0;

	this->VerifySequenceIndex();

#if STUDIO_SEQUENCE_ACTIVITY_LOOKUPS_ARE_SLOW
	int weighttotal = 0;
	int seq = ACTIVITY_NOT_AVAILABLE;
	int weight = 0;
	for (int i = 0; i < this->GetNumSeq(); i++)
	{
		int curActivity = GetSequenceActivity(i, &weight);
		if (curActivity == activity)
		{
			if (curSequence == i && weight < 0)
			{
				seq = i;
				break;
			}
			weighttotal += iabs(weight);

			int randomValue;

			if (IsInPrediction())
				randomValue = SharedRandomInt("SelectWeightedSequence", 0, weighttotal - 1, i);
			else
				randomValue = RandomInt(0, weighttotal - 1);

			if (!weighttotal || randomValue < iabs(weight))
				seq = i;
		}
	}

	return seq;
#else
	return this->SelectWeightedSequenceInternal(activity, curSequence, pRandomWeightFunc);// &SharedRandomSelect);
#endif
}






int CStudioHdr::SelectHeaviestSequence(int activity)
{
	if (!this)
		return 0;

	this->VerifySequenceIndex();

	int maxweight = 0;
	int seq = ACTIVITY_NOT_AVAILABLE;
	int weight = 0;
	for (int i = 0; i < this->GetNumSeq(); i++)
	{
		int curActivity = GetSequenceActivity(i, &weight);
		if (curActivity == activity)
		{
			if (iabs(weight) > maxweight)
			{
				maxweight = iabs(weight);
				seq = i;
			}
		}
	}

	return seq;
}

void CStudioHdr::GetEyePosition(Vector& vecEyePosition)
{
	if (!this)
	{
		Warning("GetEyePosition() Can't get pstudiohdr ptr!\n");
		return;
	}

	vecEyePosition = this->eyeposition();
}


//-----------------------------------------------------------------------------
// Purpose: Looks up an activity by name.
// Input  : label - Name of the activity to look up, ie "ACT_IDLE"
// Output : Activity index or ACT_INVALID if not found.
//-----------------------------------------------------------------------------
int CStudioHdr::LookupActivity(const char* label)
{
	VPROF("LookupActivity");

	if (!this)
	{
		return 0;
	}

	for (int i = 0; i < this->GetNumSeq(); i++)
	{
		mstudioseqdesc_t& seqdesc = this->pSeqdesc(i);
		if (stricmp(seqdesc.pszActivityName(), label) == 0)
		{
			return seqdesc.activity;
		}
	}

	return -1;// ACT_INVALID;
}

#if !defined( MAKEXVCD )
//-----------------------------------------------------------------------------
// Purpose: Looks up a sequence by sequence name first, then by activity name.
// Input  : label - The sequence name or activity name to look up.
// Output : Returns the sequence index of the matching sequence, or ACT_INVALID.
//-----------------------------------------------------------------------------
int CStudioHdr::LookupSequence(const char* label, RandomWeightFunc pRandomWeightFunc)
{
	VPROF("LookupSequence");

	if (!this)
		return 0;

	if (!this->SequencesAvailable())
		return 0;

	//
	// Look up by sequence name.
	//
	for (int i = 0; i < this->GetNumSeq(); i++)
	{
		mstudioseqdesc_t& seqdesc = this->pSeqdesc(i);
		if (stricmp(seqdesc.pszLabel(), label) == 0)
			return i;
	}

	//
	// Not found, look up by activity name.
	//
	int nActivity = this->LookupActivity(label);
	if (nActivity != -1)//ACT_INVALID)
	{
		return SelectWeightedSequence(nActivity, nActivity, pRandomWeightFunc);
	}

	return -1;// ACT_INVALID;
}

void CStudioHdr::GetSequenceLinearMotion(int iSequence, const float poseParameter[], Vector* pVec)
{
	if (!this)
	{
		Msg("Bad pstudiohdr in GetSequenceLinearMotion()!\n");
		return;
	}

	if (!this->SequencesAvailable())
		return;

	if (iSequence < 0 || iSequence >= this->GetNumSeq())
	{
		// Don't spam on bogus model
		if (this->GetNumSeq() > 0)
		{
			static int msgCount = 0;
			while (++msgCount <= 10)
			{
				Msg("Bad sequence (%i out of %i max) in GetSequenceLinearMotion() for model '%s'!\n", iSequence, this->GetNumSeq(), this->pszName());
			}
		}
		pVec->Init();
		return;
	}

	QAngle vecAngles;
	Studio_SeqMovement(iSequence, 0, 1.0, poseParameter, (*pVec), vecAngles);
}
#endif

const char* CStudioHdr::GetSequenceName(int iSequence)
{
	if (!this || iSequence < 0 || iSequence >= this->GetNumSeq())
	{
		if (this)
		{
			Msg("Bad sequence in GetSequenceName() for model '%s'!\n", this->pszName());
		}
		return "Unknown";
	}

	mstudioseqdesc_t& seqdesc = this->pSeqdesc(iSequence);
	return seqdesc.pszLabel();
}

const char* CStudioHdr::GetSequenceActivityName(int iSequence)
{
	if (!this || iSequence < 0 || iSequence >= this->GetNumSeq())
	{
		if (this)
		{
			Msg("Bad sequence in GetSequenceActivityName() for model '%s'!\n", this->pszName());
		}
		return "Unknown";
	}

	mstudioseqdesc_t& seqdesc = this->pSeqdesc(iSequence);
	return seqdesc.pszActivityName();
}

int CStudioHdr::GetSequenceFlags(int sequence)
{
	if (!this ||
		!this->SequencesAvailable() ||
		sequence < 0 ||
		sequence >= this->GetNumSeq())
	{
		return 0;
	}

	mstudioseqdesc_t& seqdesc = this->pSeqdesc(sequence);

	return seqdesc.flags;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pstudiohdr - 
//			sequence - 
//			type - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CStudioHdr::HasAnimationEventOfType(int sequence, int type)
{
	if (!this || sequence >= this->GetNumSeq())
		return false;

	mstudioseqdesc_t& seqdesc = this->pSeqdesc(sequence);
	if (!&seqdesc)
		return false;

	mstudioevent_t* pevent = mdlcache->GetEventIndexForSequence(seqdesc);
	if (!pevent)
		return false;

	if (seqdesc.numevents == 0)
		return false;

	int index;
	for (index = 0; index < (int)seqdesc.numevents; index++)
	{
		if (pevent[index].event == type)
		{
			return true;
		}
	}

	return false;
}

int CStudioHdr::GetAnimationEvent(int sequence, animevent_t* pNPCEvent, float flStart, float flEnd, int index, const float fCurtime)
{
	if (!this || sequence >= this->GetNumSeq() || !pNPCEvent)
		return 0;

	mstudioseqdesc_t& seqdesc = this->pSeqdesc(sequence);
	if (seqdesc.numevents == 0 || index >= (int)seqdesc.numevents)
		return 0;

	// Msg( "flStart %f flEnd %f (%d) %s\n", flStart, flEnd, seqdesc.numevents, seqdesc.label );
	mstudioevent_t* pevent = mdlcache->GetEventIndexForSequence(seqdesc);
	for (; index < (int)seqdesc.numevents; index++)
	{
		// Don't send client-side events to the server AI
		if (pevent[index].type & AE_TYPE_NEWEVENTSYSTEM)
		{
			if (!(pevent[index].type & AE_TYPE_SERVER))
				continue;
		}
		else if (pevent[index].event >= EVENT_CLIENT) //Adrian - Support the old event system
			continue;

		bool bOverlapEvent = false;

		if (pevent[index].cycle >= flStart && pevent[index].cycle < flEnd)
		{
			bOverlapEvent = true;
		}
		// FIXME: doesn't work with animations being played in reverse
		else if ((seqdesc.flags & STUDIO_LOOPING) && flEnd < flStart)
		{
			if (pevent[index].cycle >= flStart || pevent[index].cycle < flEnd)
			{
				bOverlapEvent = true;
			}
		}

		if (bOverlapEvent)
		{
//#ifdef GAME_DLL
			pNPCEvent->pSource = NULL;
//#endif // GAME_DLL
			pNPCEvent->cycle = pevent[index].cycle;
#if !defined( MAKEXVCD )
			pNPCEvent->eventtime = fCurtime;// gpGlobals->curtime;
#else
			pNPCEvent->eventtime = 0.0f;
#endif
			pNPCEvent->event = pevent[index].event;
			pNPCEvent->options = pevent[index].pszOptions();
			pNPCEvent->type = pevent[index].type;
			return index + 1;
		}
	}
	return 0;
}



int CStudioHdr::FindTransitionSequence(int iCurrentSequence, int iGoalSequence, int* piDir)
{
	if (!this)
		return iGoalSequence;

	if (!this->SequencesAvailable())
		return iGoalSequence;

	if ((iCurrentSequence < 0) || (iCurrentSequence >= this->GetNumSeq()))
		return iGoalSequence;

	if ((iGoalSequence < 0) || (iGoalSequence >= this->GetNumSeq()))
	{
		// asking for a bogus sequence.  Punt.
		Assert(0);
		return iGoalSequence;
	}


	// bail if we're going to or from a node 0
	if (this->EntryNode(iCurrentSequence) == 0 || this->EntryNode(iGoalSequence) == 0)
	{
		*piDir = 1;
		return iGoalSequence;
	}

	int	iEndNode;

	// Msg( "from %d to %d: ", pEndNode->iEndNode, pGoalNode->iStartNode );

	// check to see if we should be going forward or backward through the graph
	if (*piDir > 0)
	{
		iEndNode = this->ExitNode(iCurrentSequence);
	}
	else
	{
		iEndNode = this->EntryNode(iCurrentSequence);
	}

	// if both sequences are on the same node, just go there
	if (iEndNode == this->EntryNode(iGoalSequence))
	{
		*piDir = 1;
		return iGoalSequence;
	}

	int iInternNode = this->GetTransition(iEndNode, this->EntryNode(iGoalSequence));

	// if there is no transitionial node, just go to the goal sequence
	if (iInternNode == 0)
		return iGoalSequence;

	int i;

	// look for someone going from the entry node to next node it should hit
	// this may be the goal sequences node or an intermediate node
	for (i = 0; i < this->GetNumSeq(); i++)
	{
		mstudioseqdesc_t& seqdesc = this->pSeqdesc(i);
		if (this->EntryNode(i) == iEndNode && this->ExitNode(i) == iInternNode)
		{
			*piDir = 1;
			return i;
		}
		if (seqdesc.nodeflags)
		{
			if (this->ExitNode(i) == iEndNode && this->EntryNode(i) == iInternNode)
			{
				*piDir = -1;
				return i;
			}
		}
	}

	// this means that two parts of the node graph are not connected.
	DevMsg(2, "error in transition graph: %s to %s\n", this->pszNodeName(iEndNode), this->pszNodeName(this->EntryNode(iGoalSequence)));
	// Go ahead and jump to the goal sequence
	return iGoalSequence;
}






bool CStudioHdr::GotoSequence(int iCurrentSequence, float flCurrentCycle, float flCurrentRate, int iGoalSequence, int& nNextSequence, float& flNextCycle, int& iNextDir)
{
	if (!this)
		return false;

	if (!this->SequencesAvailable())
		return false;

	if ((iCurrentSequence < 0) || (iCurrentSequence >= this->GetNumSeq()))
		return false;

	if ((iGoalSequence < 0) || (iGoalSequence >= this->GetNumSeq()))
	{
		// asking for a bogus sequence.  Punt.
		Assert(0);
		return false;
	}

	// bail if we're going to or from a node 0
	if (this->EntryNode(iCurrentSequence) == 0 || this->EntryNode(iGoalSequence) == 0)
	{
		iNextDir = 1;
		flNextCycle = 0.0;
		nNextSequence = iGoalSequence;
		return true;
	}

	int	iEndNode = this->ExitNode(iCurrentSequence);
	// Msg( "from %d to %d: ", pEndNode->iEndNode, pGoalNode->iStartNode );

	// if we're in a transition sequence
	if (this->EntryNode(iCurrentSequence) != this->ExitNode(iCurrentSequence))
	{
		// are we done with it?
		if (flCurrentRate > 0.0 && flCurrentCycle >= 0.999)
		{
			iEndNode = this->ExitNode(iCurrentSequence);
		}
		else if (flCurrentRate < 0.0 && flCurrentCycle <= 0.001)
		{
			iEndNode = this->EntryNode(iCurrentSequence);
		}
		else
		{
			// nope, exit
			return false;
		}
	}

	// if both sequences are on the same node, just go there
	if (iEndNode == this->EntryNode(iGoalSequence))
	{
		iNextDir = 1;
		flNextCycle = 0.0;
		nNextSequence = iGoalSequence;
		return true;
	}

	int iInternNode = this->GetTransition(iEndNode, this->EntryNode(iGoalSequence));

	// if there is no transitionial node, just go to the goal sequence
	if (iInternNode == 0)
	{
		iNextDir = 1;
		flNextCycle = 0.0;
		nNextSequence = iGoalSequence;
		return true;
	}

	int i;

	// look for someone going from the entry node to next node it should hit
	// this may be the goal sequences node or an intermediate node
	for (i = 0; i < this->GetNumSeq(); i++)
	{
		mstudioseqdesc_t& seqdesc = this->pSeqdesc(i);
		if (this->EntryNode(i) == iEndNode && this->ExitNode(i) == iInternNode)
		{
			iNextDir = 1;
			flNextCycle = 0.0;
			nNextSequence = i;
			return true;
		}
		if (seqdesc.nodeflags)
		{
			if (this->ExitNode(i) == iEndNode && this->EntryNode(i) == iInternNode)
			{
				iNextDir = -1;
				flNextCycle = 0.999;
				nNextSequence = i;
				return true;
			}
		}
	}

	// this means that two parts of the node graph are not connected.
	DevMsg(2, "error in transition graph: %s to %s\n", this->pszNodeName(iEndNode), this->pszNodeName(this->EntryNode(iGoalSequence)));
	return false;
}

void CStudioHdr::SetBodygroup(int& body, int iGroup, int iValue)
{
	if (!this)
		return;

	if (iGroup >= this->numbodyparts())
		return;

	mstudiobodyparts_t* pbodypart = this->pBodypart(iGroup);

	if (iValue >= pbodypart->nummodels)
		return;

	int iCurrent = (body / pbodypart->base) % pbodypart->nummodels;

	body = (body - (iCurrent * pbodypart->base) + (iValue * pbodypart->base));
}


int CStudioHdr::GetBodygroup(int body, int iGroup)
{
	if (!this)
		return 0;

	if (iGroup >= this->numbodyparts())
		return 0;

	mstudiobodyparts_t* pbodypart = this->pBodypart(iGroup);

	if (pbodypart->nummodels <= 1)
		return 0;

	int iCurrent = (body / pbodypart->base) % pbodypart->nummodels;

	return iCurrent;
}

const char* CStudioHdr::GetBodygroupName(int iGroup)
{
	if (!this)
		return "";

	if (iGroup >= this->numbodyparts())
		return "";

	mstudiobodyparts_t* pbodypart = this->pBodypart(iGroup);
	return pbodypart->pszName();
}

int CStudioHdr::FindBodygroupByName(const char* name)
{
	if (!this)
		return -1;

	int group;
	for (group = 0; group < this->numbodyparts(); group++)
	{
		mstudiobodyparts_t* pbodypart = this->pBodypart(group);
		if (!Q_strcasecmp(name, pbodypart->pszName()))
		{
			return group;
		}
	}

	return -1;
}

int CStudioHdr::GetBodygroupCount(int iGroup)
{
	if (!this)
		return 0;

	if (iGroup >= this->numbodyparts())
		return 0;

	mstudiobodyparts_t* pbodypart = this->pBodypart(iGroup);
	return pbodypart->nummodels;
}

int CStudioHdr::GetNumBodyGroups()
{
	if (!this)
		return 0;

	return this->numbodyparts();
}

int CStudioHdr::GetSequenceActivity(int sequence, int* pweight)
{
	if (!this || !this->SequencesAvailable())
	{
		if (pweight)
			*pweight = 0;
		return 0;
	}

	mstudioseqdesc_t& seqdesc = this->pSeqdesc(sequence);

	if (!(seqdesc.flags & STUDIO_ACTIVITY))
	{
		SetActivityForSequence(sequence);
	}
	if (pweight)
		*pweight = seqdesc.actweight;
	return seqdesc.activity;
}


void CStudioHdr::GetAttachmentLocalSpace(int attachIndex, matrix3x4_t& pLocalToWorld)
{
	if (attachIndex >= 0)
	{
		const mstudioattachment_t& pAttachment = this->pAttachment(attachIndex);
		//MatrixCopy(pAttachment.local, pLocalToWorld);
		memcpy(pLocalToWorld.Base(), pAttachment.local.Base(), sizeof(float) * 3 * 4);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pstudiohdr - 
//			*name - 
// Output : int
//-----------------------------------------------------------------------------
int CStudioHdr::FindHitboxSetByName(const char* name)
{
	if (!this)
		return -1;

	for (int i = 0; i < this->numhitboxsets(); i++)
	{
		mstudiohitboxset_t* set = this->pHitboxSet(i);
		if (!set)
			continue;

		if (!stricmp(set->pszName(), name))
			return i;
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pstudiohdr - 
//			setnumber - 
// Output : char const
//-----------------------------------------------------------------------------
const char* CStudioHdr::GetHitboxSetName(int setnumber)
{
	if (!this)
		return "";

	mstudiohitboxset_t* set = this->pHitboxSet(setnumber);
	if (!set)
		return "";

	return set->pszName();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pstudiohdr - 
// Output : int
//-----------------------------------------------------------------------------
int CStudioHdr::GetHitboxSetCount()
{
	if (!this)
		return 0;

	return this->numhitboxsets();
}

//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle between two points in a sequences cycle
// Output:	updated position and angle, relative to CycleFrom being at the origin
//			returns false if sequence is not a movement sequence
//-----------------------------------------------------------------------------

bool CStudioHdr::Studio_SeqMovement(int iSequence, float flCycleFrom, float flCycleTo, const float poseParameter[], Vector& deltaPos, QAngle& deltaAngles)
{
	mstudioanimdesc_t* panim[4];
	float	weight[4];

	mstudioseqdesc_t& seqdesc = ((IStudioHdr*)this)->pSeqdesc(iSequence);

	Studio_SeqAnims(seqdesc, iSequence, poseParameter, panim, weight);

	deltaPos.Init();
	deltaAngles.Init();

	bool found = false;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			Vector localPos;
			QAngle localAngles;

			localPos.Init();
			localAngles.Init();

			if (Studio_AnimMovement(panim[i], flCycleFrom, flCycleTo, localPos, localAngles))
			{
				found = true;
				deltaPos = deltaPos + localPos * weight[i];
				// FIXME: this makes no sense
				deltaAngles = deltaAngles + localAngles * weight[i];
			}
			else if (!(panim[i]->flags & STUDIO_DELTA) && panim[i]->nummovements == 0 && seqdesc.weight(0) > 0.0)
			{
				found = true;
			}
		}
	}
	return found;
}

//-----------------------------------------------------------------------------
// Purpose: returns array of animations and weightings for a sequence based on current pose parameters
//-----------------------------------------------------------------------------

void CStudioHdr::Studio_SeqAnims(mstudioseqdesc_t& seqdesc, int iSequence, const float poseParameter[], mstudioanimdesc_t* panim[4], float* weight) const
{
#if _DEBUG
	VPROF_INCREMENT_COUNTER("SEQ_ANIMS", 1);
#endif
	if (!this || iSequence >= this->GetNumSeq())
	{
		weight[0] = weight[1] = weight[2] = weight[3] = 0.0;
		return;
	}

	int i0 = 0, i1 = 0;
	float s0 = 0, s1 = 0;

	Studio_LocalPoseParameter(poseParameter, seqdesc, iSequence, 0, s0, i0);
	Studio_LocalPoseParameter(poseParameter, seqdesc, iSequence, 1, s1, i1);

	panim[0] = &((IStudioHdr*)this)->pAnimdesc(this->iRelativeAnim(iSequence, seqdesc.anim(i0, i1)));
	weight[0] = (1 - s0) * (1 - s1);

	panim[1] = &((IStudioHdr*)this)->pAnimdesc(this->iRelativeAnim(iSequence, seqdesc.anim(i0 + 1, i1)));
	weight[1] = (s0) * (1 - s1);

	panim[2] = &((IStudioHdr*)this)->pAnimdesc(this->iRelativeAnim(iSequence, seqdesc.anim(i0, i1 + 1)));
	weight[2] = (1 - s0) * (s1);

	panim[3] = &((IStudioHdr*)this)->pAnimdesc(this->iRelativeAnim(iSequence, seqdesc.anim(i0 + 1, i1 + 1)));
	weight[3] = (s0) * (s1);

	Assert(weight[0] >= 0.0f && weight[1] >= 0.0f && weight[2] >= 0.0f && weight[3] >= 0.0f);
}

//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle between two points in an animation cycle
// Output:	updated position and angle, relative to CycleFrom being at the origin
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------

bool CStudioHdr::Studio_AnimMovement(mstudioanimdesc_t* panim, float flCycleFrom, float flCycleTo, Vector& deltaPos, QAngle& deltaAngle)
{
	if (panim->nummovements == 0)
		return false;

	Vector startPos;
	QAngle startA;
	Studio_AnimPosition(panim, flCycleFrom, startPos, startA);

	Vector endPos;
	QAngle endA;
	Studio_AnimPosition(panim, flCycleTo, endPos, endA);

	Vector tmp = endPos - startPos;
	deltaAngle.y = endA.y - startA.y;
	//VectorYawRotate(tmp, -startA.y, deltaPos);
	float sy, cy;

	SinCos(DEG2RAD(-startA.y), &sy, &cy);

	deltaPos.x = tmp.x * cy - tmp.y * sy;
	deltaPos.y = tmp.x * sy + tmp.y * cy;
	deltaPos.z = tmp.z;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: resolve a global pose parameter to the specific setting for this sequence
//-----------------------------------------------------------------------------
void CStudioHdr::Studio_LocalPoseParameter(const float poseParameter[], mstudioseqdesc_t& seqdesc, int iSequence, int iLocalIndex, float& flSetting, int& index) const
{
	if (!this)
	{
		flSetting = 0;
		index = 0;
		return;
	}

	int iPose = this->GetSharedPoseParameter(iSequence, seqdesc.paramindex[iLocalIndex]);

	if (iPose == -1)
	{
		flSetting = 0;
		index = 0;
		return;
	}

	const mstudioposeparamdesc_t& Pose = ((IStudioHdr*)this)->pPoseParameter(iPose);

	float flValue = poseParameter[iPose];

	if (Pose.loop)
	{
		float wrap = (Pose.start + Pose.end) / 2.0 + Pose.loop / 2.0;
		float shift = Pose.loop - wrap;

		flValue = flValue - Pose.loop * floor((flValue + shift) / Pose.loop);
	}

	if (seqdesc.posekeyindex == 0)
	{
		float flLocalStart = ((float)seqdesc.paramstart[iLocalIndex] - Pose.start) / (Pose.end - Pose.start);
		float flLocalEnd = ((float)seqdesc.paramend[iLocalIndex] - Pose.start) / (Pose.end - Pose.start);

		// convert into local range
		flSetting = (flValue - flLocalStart) / (flLocalEnd - flLocalStart);

		// clamp.  This shouldn't ever need to happen if it's looping.
		if (flSetting < 0)
			flSetting = 0;
		if (flSetting > 1)
			flSetting = 1;

		index = 0;
		if (seqdesc.groupsize[iLocalIndex] > 2)
		{
			// estimate index
			index = (int)(flSetting * (seqdesc.groupsize[iLocalIndex] - 1));
			if (index == seqdesc.groupsize[iLocalIndex] - 1) index = seqdesc.groupsize[iLocalIndex] - 2;
			flSetting = flSetting * (seqdesc.groupsize[iLocalIndex] - 1) - index;
		}
	}
	else
	{
		flValue = flValue * (Pose.end - Pose.start) + Pose.start;
		index = 0;

		// FIXME: this needs to be 2D
		// FIXME: this shouldn't be a linear search

		while (1)
		{
			flSetting = (flValue - seqdesc.poseKey(iLocalIndex, index)) / (seqdesc.poseKey(iLocalIndex, index + 1) - seqdesc.poseKey(iLocalIndex, index));
			/*
			if (index > 0 && flSetting < 0.0)
			{
				index--;
				continue;
			}
			else
			*/
			if (index < seqdesc.groupsize[iLocalIndex] - 2 && flSetting > 1.0)
			{
				index++;
				continue;
			}
			break;
		}

		// clamp.
		if (flSetting < 0.0f)
			flSetting = 0.0f;
		if (flSetting > 1.0f)
			flSetting = 1.0f;
	}
}

//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle relative to the start of an animations cycle
// Output:	updated position and angle, relative to the origin
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------

bool CStudioHdr::Studio_AnimPosition(mstudioanimdesc_t* panim, float flCycle, Vector& vecPos, QAngle& vecAngle)
{
	float	prevframe = 0;
	vecPos.Init();
	vecAngle.Init();

	if (panim->nummovements == 0)
		return false;

	int iLoops = 0;
	if (flCycle > 1.0)
	{
		iLoops = (int)flCycle;
	}
	else if (flCycle < 0.0)
	{
		iLoops = (int)flCycle - 1;
	}
	flCycle = flCycle - iLoops;

	float	flFrame = flCycle * (panim->numframes - 1);


	for (int i = 0; i < panim->nummovements; i++)
	{
		mstudiomovement_t pmove;
		// TODO(nillerusr): fix alignment on model loading
		V_memcpy(&pmove, panim->pMovement(i), sizeof(mstudiomovement_t));

		if (pmove.endframe >= flFrame)
		{
			float f = (flFrame - prevframe) / (pmove.endframe - prevframe);
			float d = pmove.v0 * f + 0.5 * (pmove.v1 - pmove.v0) * f * f;

			vecPos = vecPos + d * pmove.vector;
			vecAngle.y = vecAngle.y * (1 - f) + pmove.angle * f;
			if (iLoops != 0)
			{
				mstudiomovement_t* pmoveAnim = panim->pMovement(panim->nummovements - 1);
				vecPos = vecPos + iLoops * pmoveAnim->position;
				vecAngle.y = vecAngle.y + iLoops * pmoveAnim->angle;
			}
			return true;
		}
		else
		{
			prevframe = pmove.endframe;
			vecPos = pmove.position;
			vecAngle.y = pmove.angle;
		}
	}

	return false;
}

CBoneSetupMemoryPool<Quaternion> g_QaternionPool;
CBoneSetupMemoryPool<Vector> g_VectorPool;
CBoneSetupMemoryPool<matrix3x4_t> g_MatrixPool;

//-----------------------------------------------------------------------------
// Purpose: blend together in world space q1,pos1 with q2,pos2.  Return result in q1,pos1.  
//			0 returns q1, pos1.  1 returns q2, pos2
//-----------------------------------------------------------------------------

void CStudioHdr::WorldSpaceSlerp(
	Quaternion q1[MAXSTUDIOBONES],
	Vector pos1[MAXSTUDIOBONES],
	mstudioseqdesc_t& seqdesc,
	int sequence,
	const Quaternion q2[MAXSTUDIOBONES],
	const Vector pos2[MAXSTUDIOBONES],
	float s,
	int boneMask) const
{
	int			i, j;
	float		s1; // weight of parent for q2, pos2
	float		s2; // weight for q2, pos2

	// make fake root transform
	matrix3x4_t rootXform;
	SetIdentityMatrix(rootXform);

	// matrices for q2, pos2
	matrix3x4_t* srcBoneToWorld = g_MatrixPool.Alloc();
	CBoneBitList srcBoneComputed;

	matrix3x4_t* destBoneToWorld = g_MatrixPool.Alloc();
	CBoneBitList destBoneComputed;

	matrix3x4_t* targetBoneToWorld = g_MatrixPool.Alloc();
	CBoneBitList targetBoneComputed;

	IVirtualModel* pVModel = this->GetVirtualModel();
	const virtualgroup_t* pSeqGroup = NULL;
	if (pVModel)
	{
		pSeqGroup = pVModel->pSeqGroup(sequence);
	}

	mstudiobone_t* pbone = this->pBone(0);

	for (i = 0; i < this->numbones(); i++)
	{
		// skip unused bones
		if (!(this->boneFlags(i) & boneMask))
		{
			continue;
		}

		int n = pbone[i].parent;
		s1 = 0.0;
		if (pSeqGroup)
		{
			j = pSeqGroup->boneMap[i];
			if (j >= 0)
			{
				s2 = s * seqdesc.weight(j);	// blend in based on this bones weight
				if (n != -1)
				{
					s1 = s * seqdesc.weight(pSeqGroup->boneMap[n]);
				}
			}
			else
			{
				s2 = 0.0;
			}
		}
		else
		{
			s2 = s * seqdesc.weight(i);	// blend in based on this bones weight
			if (n != -1)
			{
				s1 = s * seqdesc.weight(n);
			}
		}

		if (s1 == 1.0 && s2 == 1.0)
		{
			pos1[i] = pos2[i];
			q1[i] = q2[i];
		}
		else if (s2 > 0.0)
		{
			Quaternion srcQ, destQ;
			Vector srcPos, destPos;
			Quaternion targetQ;
			Vector targetPos;
			Vector tmp;

			this->BuildBoneChain( rootXform, pos1, q1, i, destBoneToWorld, destBoneComputed);
			this->BuildBoneChain( rootXform, pos2, q2, i, srcBoneToWorld, srcBoneComputed);

			MatrixAngles(destBoneToWorld[i], destQ, destPos);
			MatrixAngles(srcBoneToWorld[i], srcQ, srcPos);

			QuaternionSlerp(destQ, srcQ, s2, targetQ);
			AngleMatrix(targetQ, destPos, targetBoneToWorld[i]);

			// back solve
			if (n == -1)
			{
				MatrixAngles(targetBoneToWorld[i], q1[i], tmp);
			}
			else
			{
				matrix3x4_t worldToBone;
				MatrixInvert(targetBoneToWorld[n], worldToBone);

				matrix3x4_t local;
				ConcatTransforms(worldToBone, targetBoneToWorld[i], local);
				MatrixAngles(local, q1[i], tmp);

				// blend bone lengths (local space)
				pos1[i] = Lerp(s2, pos1[i], pos2[i]);
			}
		}
	}
	g_MatrixPool.Free(srcBoneToWorld);
	g_MatrixPool.Free(destBoneToWorld);
	g_MatrixPool.Free(targetBoneToWorld);
}

#if ALLOW_SIMD_QUATERNION_MATH
FORCEINLINE fltx4 QuaternionMASIMD(const fltx4& p, float s, const fltx4& q)
{
	fltx4 p1, q1, result;
	q1 = QuaternionScaleSIMD(q, s);
	p1 = QuaternionMultSIMD(p, q1);
	result = QuaternionNormalizeSIMD(p1);
	return result;
}
#endif

#if ALLOW_SIMD_QUATERNION_MATH
FORCEINLINE fltx4 QuaternionSMSIMD(float s, const fltx4& p, const fltx4& q)
{
	fltx4 p1, q1, result;
	p1 = QuaternionScaleSIMD(p, s);
	q1 = QuaternionMultSIMD(p1, q);
	result = QuaternionNormalizeSIMD(q1);
	return result;
}
#endif

#if ALLOW_SIMD_QUATERNION_MATH
FORCEINLINE fltx4 QuaternionAccumulateSIMD(const fltx4& p, float s, const fltx4& q)
{
	fltx4 q2, s4, result;
	q2 = QuaternionAlignSIMD(p, q);
	s4 = ReplicateX4(s);
	result = MaddSIMD(s4, q2, p);
	return result;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: blend together q1,pos1 with q2,pos2.  Return result in q1,pos1.  
//			0 returns q1, pos1.  1 returns q2, pos2
//-----------------------------------------------------------------------------
void CStudioHdr::SlerpBones(
	Quaternion q1[MAXSTUDIOBONES],
	Vector pos1[MAXSTUDIOBONES],
	mstudioseqdesc_t& seqdesc,  // source of q2 and pos2
	int sequence,
	const QuaternionAligned q2[MAXSTUDIOBONES],
	const Vector pos2[MAXSTUDIOBONES],
	float s,
	int boneMask) const
{
	if (s <= 0.0f)
		return;
	if (s > 1.0f)
	{
		s = 1.0f;
	}

	if (seqdesc.flags & STUDIO_WORLD)
	{
		this->WorldSpaceSlerp( q1, pos1, seqdesc, sequence, q2, pos2, s, boneMask);
		return;
	}

	int			i, j;
	IVirtualModel* pVModel = this->GetVirtualModel();
	const virtualgroup_t* pSeqGroup = NULL;
	if (pVModel)
	{
		pSeqGroup = pVModel->pSeqGroup(sequence);
	}

	// Build weightlist for all bones
	int nBoneCount = this->numbones();
	float* pS2 = (float*)stackalloc(nBoneCount * sizeof(float));
	for (i = 0; i < nBoneCount; i++)
	{
		// skip unused bones
		if (!(this->boneFlags(i) & boneMask))
		{
			pS2[i] = 0.0f;
			continue;
		}

		if (!pSeqGroup)
		{
			pS2[i] = s * seqdesc.weight(i);	// blend in based on this bones weight
			continue;
		}

		j = pSeqGroup->boneMap[i];
		if (j >= 0)
		{
			pS2[i] = s * seqdesc.weight(j);	// blend in based on this bones weight
		}
		else
		{
			pS2[i] = 0.0;
		}
	}

	float s1, s2;
	if (seqdesc.flags & STUDIO_DELTA)
	{
		for (i = 0; i < nBoneCount; i++)
		{
			s2 = pS2[i];
			if (s2 <= 0.0f)
				continue;

			if (seqdesc.flags & STUDIO_POST)
			{
#ifndef _X360
				QuaternionMA(q1[i], s2, q2[i], q1[i]);
#else
				fltx4 q1simd = LoadUnalignedSIMD(q1[i].Base());
				fltx4 q2simd = LoadAlignedSIMD(q2[i]);
				fltx4 result = QuaternionMASIMD(q1simd, s2, q2simd);
				StoreUnalignedSIMD(q1[i].Base(), result);
#endif
				// FIXME: are these correct?
				pos1[i][0] = pos1[i][0] + pos2[i][0] * s2;
				pos1[i][1] = pos1[i][1] + pos2[i][1] * s2;
				pos1[i][2] = pos1[i][2] + pos2[i][2] * s2;
			}
			else
			{
#ifndef _X360
				QuaternionSM(s2, q2[i], q1[i], q1[i]);
#else
				fltx4 q1simd = LoadUnalignedSIMD(q1[i].Base());
				fltx4 q2simd = LoadAlignedSIMD(q2[i]);
				fltx4 result = QuaternionSMSIMD(s2, q2simd, q1simd);
				StoreUnalignedSIMD(q1[i].Base(), result);
#endif

				// FIXME: are these correct?
				pos1[i][0] = pos1[i][0] + pos2[i][0] * s2;
				pos1[i][1] = pos1[i][1] + pos2[i][1] * s2;
				pos1[i][2] = pos1[i][2] + pos2[i][2] * s2;
			}
		}
		return;
	}

	QuaternionAligned q3;
	for (i = 0; i < nBoneCount; i++)
	{
		s2 = pS2[i];
		if (s2 <= 0.0f)
			continue;

		s1 = 1.0 - s2;

#ifdef _X360
		fltx4  q1simd, q2simd, result;
		q1simd = LoadUnalignedSIMD(q1[i].Base());
		q2simd = LoadAlignedSIMD(q2[i]);
#endif
		if (this->boneFlags(i) & BONE_FIXED_ALIGNMENT)
		{
#ifndef _X360
			QuaternionSlerpNoAlign(q2[i], q1[i], s1, q3);
#else
			result = QuaternionSlerpNoAlignSIMD(q2simd, q1simd, s1);
#endif
		}
		else
		{
#ifndef _X360
			QuaternionSlerp(q2[i], q1[i], s1, q3);
#else
			result = QuaternionSlerpSIMD(q2simd, q1simd, s1);
#endif
		}

#ifndef _X360
		q1[i][0] = q3[0];
		q1[i][1] = q3[1];
		q1[i][2] = q3[2];
		q1[i][3] = q3[3];
#else
		StoreUnalignedSIMD(q1[i].Base(), result);
#endif

		pos1[i][0] = pos1[i][0] * s1 + pos2[i][0] * s2;
		pos1[i][1] = pos1[i][1] * s1 + pos2[i][1] * s2;
		pos1[i][2] = pos1[i][2] * s1 + pos2[i][2] * s2;
	}
}

//-----------------------------------------------------------------------------
// Purpose: return a sub frame rotation for a single bone
//-----------------------------------------------------------------------------
void ExtractAnimValue(int frame, mstudioanimvalue_t* panimvalue, float scale, float& v1, float& v2)
{
	if (!panimvalue)
	{
		v1 = v2 = 0;
		return;
	}

	// Avoids a crash reading off the end of the data
	// There is probably a better long-term solution; Ken is going to look into it.
	if ((panimvalue->num.total == 1) && (panimvalue->num.valid == 1))
	{
		v1 = v2 = panimvalue[1].value * scale;
		return;
	}

	int k = frame;

	// find the data list that has the frame
	while (panimvalue->num.total <= k)
	{
		k -= panimvalue->num.total;
		panimvalue += panimvalue->num.valid + 1;
		if (panimvalue->num.total == 0)
		{
			Assert(0); // running off the end of the animation stream is bad
			v1 = v2 = 0;
			return;
		}
	}
	if (panimvalue->num.valid > k)
	{
		// has valid animation data
		v1 = panimvalue[k + 1].value * scale;

		if (panimvalue->num.valid > k + 1)
		{
			// has valid animation blend data
			v2 = panimvalue[k + 2].value * scale;
		}
		else
		{
			if (panimvalue->num.total > k + 1)
			{
				// data repeats, no blend
				v2 = v1;
			}
			else
			{
				// pull blend from first data block in next list
				v2 = panimvalue[panimvalue->num.valid + 2].value * scale;
			}
		}
	}
	else
	{
		// get last valid data block
		v1 = panimvalue[panimvalue->num.valid].value * scale;
		if (panimvalue->num.total > k + 1)
		{
			// data repeats, no blend
			v2 = v1;
		}
		else
		{
			// pull blend from first data block in next list
			v2 = panimvalue[panimvalue->num.valid + 2].value * scale;
		}
	}
}


void ExtractAnimValue(int frame, mstudioanimvalue_t* panimvalue, float scale, float& v1)
{
	if (!panimvalue)
	{
		v1 = 0;
		return;
	}

	int k = frame;

	while (panimvalue->num.total <= k)
	{
		k -= panimvalue->num.total;
		panimvalue += panimvalue->num.valid + 1;
		if (panimvalue->num.total == 0)
		{
			Assert(0); // running off the end of the animation stream is bad
			v1 = 0;
			return;
		}
	}
	if (panimvalue->num.valid > k)
	{
		v1 = panimvalue[k + 1].value * scale;
	}
	else
	{
		// get last valid data block
		v1 = panimvalue[panimvalue->num.valid].value * scale;
	}
}

//-----------------------------------------------------------------------------
// Purpose: return a sub frame rotation for a single bone
//-----------------------------------------------------------------------------
void CalcBoneQuaternion(int frame, float s,
	const Quaternion& baseQuat, const RadianEuler& baseRot, const Vector& baseRotScale,
	int iBaseFlags, const Quaternion& baseAlignment,
	const mstudioanim_t* panim, Quaternion& q)
{
	if (panim->flags & STUDIO_ANIM_RAWROT)
	{
		Quaternion48 tmp;
		V_memcpy(&tmp, panim->pQuat48(), sizeof(Quaternion48));
		q = tmp;
		Assert(q.IsValid());
		return;
	}

	if (panim->flags & STUDIO_ANIM_RAWROT2)
	{
		Quaternion64 tmp;
		V_memcpy(&tmp, panim->pQuat64(), sizeof(Quaternion64));
		q = tmp;
		Assert(q.IsValid());
		return;
	}

	if (!(panim->flags & STUDIO_ANIM_ANIMROT))
	{
		if (panim->flags & STUDIO_ANIM_DELTA)
		{
			q.Init(0.0f, 0.0f, 0.0f, 1.0f);
		}
		else
		{
			q = baseQuat;
		}
		return;
	}

	mstudioanim_valueptr_t* pValuesPtr = panim->pRotV();

	if (s > 0.001f)
	{
		QuaternionAligned	q1, q2;
		RadianEuler			angle1, angle2;

		ExtractAnimValue(frame, pValuesPtr->pAnimvalue(0), baseRotScale.x, angle1.x, angle2.x);
		ExtractAnimValue(frame, pValuesPtr->pAnimvalue(1), baseRotScale.y, angle1.y, angle2.y);
		ExtractAnimValue(frame, pValuesPtr->pAnimvalue(2), baseRotScale.z, angle1.z, angle2.z);

		if (!(panim->flags & STUDIO_ANIM_DELTA))
		{
			angle1.x = angle1.x + baseRot.x;
			angle1.y = angle1.y + baseRot.y;
			angle1.z = angle1.z + baseRot.z;
			angle2.x = angle2.x + baseRot.x;
			angle2.y = angle2.y + baseRot.y;
			angle2.z = angle2.z + baseRot.z;
		}

		Assert(angle1.IsValid() && angle2.IsValid());
		if (angle1.x != angle2.x || angle1.y != angle2.y || angle1.z != angle2.z)
		{
			AngleQuaternion(angle1, q1);
			AngleQuaternion(angle2, q2);

#ifdef _X360
			fltx4 q1simd, q2simd, qsimd;
			q1simd = LoadAlignedSIMD(q1);
			q2simd = LoadAlignedSIMD(q2);
			qsimd = QuaternionBlendSIMD(q1simd, q2simd, s);
			StoreUnalignedSIMD(q.Base(), qsimd);
#else
			QuaternionBlend(q1, q2, s, q);
#endif
		}
		else
		{
			AngleQuaternion(angle1, q);
		}
	}
	else
	{
		RadianEuler			angle;

		ExtractAnimValue(frame, pValuesPtr->pAnimvalue(0), baseRotScale.x, angle.x);
		ExtractAnimValue(frame, pValuesPtr->pAnimvalue(1), baseRotScale.y, angle.y);
		ExtractAnimValue(frame, pValuesPtr->pAnimvalue(2), baseRotScale.z, angle.z);

		if (!(panim->flags & STUDIO_ANIM_DELTA))
		{
			angle.x = angle.x + baseRot.x;
			angle.y = angle.y + baseRot.y;
			angle.z = angle.z + baseRot.z;
		}

		Assert(angle.IsValid());
		AngleQuaternion(angle, q);
	}

	Assert(q.IsValid());

	// align to unified bone
	if (!(panim->flags & STUDIO_ANIM_DELTA) && (iBaseFlags & BONE_FIXED_ALIGNMENT))
	{
		QuaternionAlign(baseAlignment, q, q);
	}
}

inline void CalcBoneQuaternion(int frame, float s,
	const mstudiobone_t* pBone,
	const mstudiolinearbone_t* pLinearBones,
	const mstudioanim_t* panim, Quaternion& q)
{
	if (pLinearBones)
	{
		CalcBoneQuaternion(frame, s, pLinearBones->quat(panim->bone), pLinearBones->rot(panim->bone), pLinearBones->rotscale(panim->bone), pLinearBones->flags(panim->bone), pLinearBones->qalignment(panim->bone), panim, q);
	}
	else
	{
		CalcBoneQuaternion(frame, s, pBone->quat, pBone->rot, pBone->rotscale, pBone->flags, pBone->qAlignment, panim, q);
	}
}

//-----------------------------------------------------------------------------
// Purpose: return a sub frame position for a single bone
//-----------------------------------------------------------------------------
void CalcBonePosition(int frame, float s,
	const Vector& basePos, const Vector& baseBoneScale,
	const mstudioanim_t* panim, Vector& pos)
{
	if (panim->flags & STUDIO_ANIM_RAWPOS)
	{
		pos = *(panim->pPos());
		Assert(pos.IsValid());

		return;
	}
	else if (!(panim->flags & STUDIO_ANIM_ANIMPOS))
	{
		if (panim->flags & STUDIO_ANIM_DELTA)
		{
			pos.Init(0.0f, 0.0f, 0.0f);
		}
		else
		{
			pos = basePos;
		}
		return;
	}

	mstudioanim_valueptr_t* pPosV = panim->pPosV();
	int					j;

	if (s > 0.001f)
	{
		float v1, v2;
		for (j = 0; j < 3; j++)
		{
			ExtractAnimValue(frame, pPosV->pAnimvalue(j), baseBoneScale[j], v1, v2);
			pos[j] = v1 * (1.0 - s) + v2 * s;
		}
	}
	else
	{
		for (j = 0; j < 3; j++)
		{
			ExtractAnimValue(frame, pPosV->pAnimvalue(j), baseBoneScale[j], pos[j]);
		}
	}

	if (!(panim->flags & STUDIO_ANIM_DELTA))
	{
		pos.x = pos.x + basePos.x;
		pos.y = pos.y + basePos.y;
		pos.z = pos.z + basePos.z;
	}

	Assert(pos.IsValid());
}


inline void CalcBonePosition(int frame, float s,
	const mstudiobone_t* pBone,
	const mstudiolinearbone_t* pLinearBones,
	const mstudioanim_t* panim, Vector& pos)
{
	if (pLinearBones)
	{
		CalcBonePosition(frame, s, pLinearBones->pos(panim->bone), pLinearBones->posscale(panim->bone), panim, pos);
	}
	else
	{
		CalcBonePosition(frame, s, pBone->pos, pBone->posscale, panim, pos);
	}
}

void CStudioHdr::SetupSingleBoneMatrix(
	int nSequence,
	int iFrame,
	int iBone,
	matrix3x4_t& mBoneLocal)
{
	mstudioseqdesc_t& seqdesc = this->pSeqdesc(nSequence);
	mstudioanimdesc_t& animdesc = this->pAnimdesc(seqdesc.anim(0, 0));
	int iLocalFrame = iFrame;
	mstudioanim_t* panim = animdesc.pAnim(this, &iLocalFrame);
	float s = 0;
	mstudiobone_t* pbone = this->pBone(iBone);

	Quaternion boneQuat;
	Vector bonePos;

	// search for bone
	while (panim && panim->bone != iBone)
	{
		panim = panim->pNext();
	}

	// look up animation if found, if not, initialize
	if (panim && seqdesc.weight(iBone) > 0)
	{
		CalcBoneQuaternion(iLocalFrame, s, pbone, NULL, panim, boneQuat);
		CalcBonePosition(iLocalFrame, s, pbone, NULL, panim, bonePos);
	}
	else if (animdesc.flags & STUDIO_DELTA)
	{
		boneQuat.Init(0.0f, 0.0f, 0.0f, 1.0f);
		bonePos.Init(0.0f, 0.0f, 0.0f);
	}
	else
	{
		boneQuat = pbone->quat;
		bonePos = pbone->pos;
	}

	QuaternionMatrix(boneQuat, bonePos, mBoneLocal);
}

//-----------------------------------------------------------------------------
// Purpose: build boneToWorld transforms for a specific bone
//-----------------------------------------------------------------------------
void CStudioHdr::BuildBoneChain(
	const matrix3x4_t& rootxform,
	const Vector pos[],
	const Quaternion q[],
	int	iBone,
	matrix3x4_t* pBoneToWorld,
	CBoneBitList& boneComputed) const
{
	if (boneComputed.IsBoneMarked(iBone))
		return;

	matrix3x4_t bonematrix;
	QuaternionMatrix(q[iBone], pos[iBone], bonematrix);

	int parent = this->boneParent(iBone);
	if (parent == -1)
	{
		ConcatTransforms(rootxform, bonematrix, pBoneToWorld[iBone]);
	}
	else
	{
		// evil recursive!!!
		this->BuildBoneChain( rootxform, pos, q, parent, pBoneToWorld, boneComputed);
		ConcatTransforms(pBoneToWorld[parent], bonematrix, pBoneToWorld[iBone]);
	}
	boneComputed.MarkBone(iBone);
}

void CStudioHdr::BuildBoneChain(
	const matrix3x4_t& rootxform,
	const Vector pos[],
	const Quaternion q[],
	int	iBone,
	matrix3x4_t* pBoneToWorld) const
{
	CBoneBitList boneComputed;
	this->BuildBoneChain( rootxform, pos, q, iBone, pBoneToWorld, boneComputed);
	return;
}

//-----------------------------------------------------------------------------
// Purpose: look at single column vector of another bones local transformation 
//			and generate a procedural transformation based on how that column 
//			points down the 6 cardinal axis (all negative weights are clamped to 0).
//-----------------------------------------------------------------------------

void DoAxisInterpBone(
	mstudiobone_t* pbones,
	int	ibone,
	IBoneAccessor* bonetoworld
)
{
	matrix3x4_t			bonematrix;
	Vector				control;

	mstudioaxisinterpbone_t* pProc = (mstudioaxisinterpbone_t*)pbones[ibone].pProcedure();
	const matrix3x4_t& controlBone = bonetoworld->GetBone(pProc->control);
	if (pProc && pbones[pProc->control].parent != -1)
	{
		Vector tmp;
		// pull out the control column
		tmp.x = controlBone[0][pProc->axis];
		tmp.y = controlBone[1][pProc->axis];
		tmp.z = controlBone[2][pProc->axis];

		// invert it back into parent's space.
		VectorIRotate(tmp, bonetoworld->GetBone(pbones[pProc->control].parent), control);
#if 0
		matrix3x4_t	tmpmatrix;
		matrix3x4_t	controlmatrix;
		MatrixInvert(bonetoworld.GetBone(pbones[pProc->control].parent), tmpmatrix);
		ConcatTransforms(tmpmatrix, bonetoworld.GetBone(pProc->control), controlmatrix);

		// pull out the control column
		control.x = controlmatrix[0][pProc->axis];
		control.y = controlmatrix[1][pProc->axis];
		control.z = controlmatrix[2][pProc->axis];
#endif
	}
	else
	{
		// pull out the control column
		control.x = controlBone[0][pProc->axis];
		control.y = controlBone[1][pProc->axis];
		control.z = controlBone[2][pProc->axis];
	}

	Quaternion* q1, * q2, * q3;
	Vector* p1, * p2, * p3;

	// find axial control inputs
	float a1 = control.x;
	float a2 = control.y;
	float a3 = control.z;
	if (a1 >= 0)
	{
		q1 = &pProc->quat[0];
		p1 = &pProc->pos[0];
	}
	else
	{
		a1 = -a1;
		q1 = &pProc->quat[1];
		p1 = &pProc->pos[1];
	}

	if (a2 >= 0)
	{
		q2 = &pProc->quat[2];
		p2 = &pProc->pos[2];
	}
	else
	{
		a2 = -a2;
		q2 = &pProc->quat[3];
		p2 = &pProc->pos[3];
	}

	if (a3 >= 0)
	{
		q3 = &pProc->quat[4];
		p3 = &pProc->pos[4];
	}
	else
	{
		a3 = -a3;
		q3 = &pProc->quat[5];
		p3 = &pProc->pos[5];
	}

	// do a three-way blend
	Vector p;
	Quaternion v, tmp;
	if (a1 + a2 > 0)
	{
		float t = 1.0 / (a1 + a2 + a3);
		// FIXME: do a proper 3-way Quat blend!
		QuaternionSlerp(*q2, *q1, a1 / (a1 + a2), tmp);
		QuaternionSlerp(tmp, *q3, a3 * t, v);
		VectorScale(*p1, a1 * t, p);
		VectorMA(p, a2 * t, *p2, p);
		VectorMA(p, a3 * t, *p3, p);
	}
	else
	{
		QuaternionSlerp(*q3, *q3, 0, v); // ??? no quat copy?
		p = *p3;
	}

	QuaternionMatrix(v, p, bonematrix);

	ConcatTransforms(bonetoworld->GetBone(pbones[ibone].parent), bonematrix, bonetoworld->GetBoneForWrite(ibone));
}

//-----------------------------------------------------------------------------
// Purpose: Generate a procedural transformation based on how that another bones 
//			local transformation matches a set of target orientations.
//-----------------------------------------------------------------------------
void DoQuatInterpBone(
	mstudiobone_t* pbones,
	int	ibone,
	IBoneAccessor* bonetoworld
)
{
	matrix3x4_t			bonematrix;
	Vector				control;

	mstudioquatinterpbone_t* pProc = (mstudioquatinterpbone_t*)pbones[ibone].pProcedure();
	if (pProc && pbones[pProc->control].parent != -1)
	{
		Quaternion	src;
		float		weight[32];
		float		scale = 0.0;
		Quaternion	quat;
		Vector		pos;

		matrix3x4_t	tmpmatrix;
		matrix3x4_t	controlmatrix;
		MatrixInvert(bonetoworld->GetBone(pbones[pProc->control].parent), tmpmatrix);
		ConcatTransforms(tmpmatrix, bonetoworld->GetBone(pProc->control), controlmatrix);

		MatrixAngles(controlmatrix, src, pos); // FIXME: make a version without pos

		int i;
		for (i = 0; i < pProc->numtriggers; i++)
		{
			float dot = fabs(QuaternionDotProduct(pProc->pTrigger(i)->trigger, src));
			// FIXME: a fast acos should be acceptable
			dot = clamp(dot, -1.f, 1.f);
			weight[i] = 1 - (2 * acos(dot) * pProc->pTrigger(i)->inv_tolerance);
			weight[i] = max(0.f, weight[i]);
			scale += weight[i];
		}

		if (scale <= 0.001)  // EPSILON?
		{
			AngleMatrix(pProc->pTrigger(0)->quat, pProc->pTrigger(0)->pos, bonematrix);
			ConcatTransforms(bonetoworld->GetBone(pbones[ibone].parent), bonematrix, bonetoworld->GetBoneForWrite(ibone));
			return;
		}

		scale = 1.0 / scale;

		quat.Init(0, 0, 0, 0);
		pos.Init();

		for (i = 0; i < pProc->numtriggers; i++)
		{
			if (weight[i])
			{
				float s = weight[i] * scale;
				mstudioquatinterpinfo_t* pTrigger = pProc->pTrigger(i);

				QuaternionAlign(pTrigger->quat, quat, quat);

				quat.x = quat.x + s * pTrigger->quat.x;
				quat.y = quat.y + s * pTrigger->quat.y;
				quat.z = quat.z + s * pTrigger->quat.z;
				quat.w = quat.w + s * pTrigger->quat.w;
				pos.x = pos.x + s * pTrigger->pos.x;
				pos.y = pos.y + s * pTrigger->pos.y;
				pos.z = pos.z + s * pTrigger->pos.z;
			}
		}
		Assert(QuaternionNormalize(quat) != 0);
		QuaternionMatrix(quat, pos, bonematrix);
	}

	ConcatTransforms(bonetoworld->GetBone(pbones[ibone].parent), bonematrix, bonetoworld->GetBoneForWrite(ibone));
}

//-----------------------------------------------------------------------------
// Purpose: Generate a procedural transformation so that one bone points at
//			another point on the model
//-----------------------------------------------------------------------------
void DoAimAtBone(
	mstudiobone_t* pBones,
	int	iBone,
	IBoneAccessor* bonetoworld,
	const IStudioHdr* pStudioHdr,
	bool attachment
)
{
	mstudioaimatbone_t* pProc = (mstudioaimatbone_t*)pBones[iBone].pProcedure();

	if (!pProc)
	{
		return;
	}

	/*
	 * Uncomment this if the ConVar above is uncommented
	 *

	if ( !aim_constraint.GetBool() )
	{
		// If the aim constraint is turned off then just copy the parent transform
		// plus the offset value

		matrix3x4_t boneToWorldSpace;
		MatrixCopy ( bonetoworld.GetBone( pProc->parent ), boneToWorldSpace );
		Vector boneWorldPosition;
		VectorTransform( pProc->basepos, boneToWorldSpace, boneWorldPosition );
		MatrixSetColumn( boneWorldPosition, 3, boneToWorldSpace );
		MatrixCopy( boneToWorldSpace, bonetoworld.GetBoneForWrite( iBone ) );

		return;
	}

	*/

	// The world matrix of the bone to change
	matrix3x4_t boneMatrix;

	// Guaranteed to be unit length
	const Vector& userAimVector(pProc->aimvector);

	// Guaranteed to be unit length
	const Vector& userUpVector(pProc->upvector);

	// Get to get position of bone but also for up reference
	matrix3x4_t parentSpace;
	MatrixCopy(bonetoworld->GetBone(pProc->parent), parentSpace);

	// World space position of the bone to aim
	Vector aimWorldPosition;
	VectorTransform(pProc->basepos, parentSpace, aimWorldPosition);

	// The worldspace matrix of the bone to aim at
	matrix3x4_t aimAtSpace;
	if (attachment)
	{
		// This means it's AIMATATTACH
		const mstudioattachment_t& attachment(((IStudioHdr*)pStudioHdr)->pAttachment(pProc->aim));
		ConcatTransforms(
			bonetoworld->GetBone(attachment.localbone),
			attachment.local,
			aimAtSpace);
	}
	else
	{
		MatrixCopy(bonetoworld->GetBone(pProc->aim), aimAtSpace);
	}

	Vector aimAtWorldPosition;
	MatrixGetColumn(aimAtSpace, 3, aimAtWorldPosition);

	// make sure the redundant parent info is correct
	Assert(pProc->parent == pBones[iBone].parent);
	// make sure the redundant position info is correct
	Assert(pProc->basepos.DistToSqr(pBones[iBone].pos) < 0.1);

	// The aim and up data is relative to this bone, not the parent bone
	matrix3x4_t bonematrix, boneLocalToWorld;
	AngleMatrix(pBones[iBone].quat, pProc->basepos, bonematrix);
	ConcatTransforms(bonetoworld->GetBone(pProc->parent), bonematrix, boneLocalToWorld);

	Vector aimVector;
	VectorSubtract(aimAtWorldPosition, aimWorldPosition, aimVector);
	VectorNormalizeFast(aimVector);

	Vector axis;
	CrossProduct(userAimVector, aimVector, axis);
	VectorNormalizeFast(axis);
	Assert(1.0f - fabs(DotProduct(userAimVector, aimVector)) > FLT_EPSILON);
	float angle(acosf(DotProduct(userAimVector, aimVector)));
	Quaternion aimRotation;
	AxisAngleQuaternion(axis, RAD2DEG(angle), aimRotation);

	if ((1.0f - fabs(DotProduct(userUpVector, userAimVector))) > FLT_EPSILON)
	{
		matrix3x4_t aimRotationMatrix;
		QuaternionMatrix(aimRotation, aimRotationMatrix);

		Vector tmpV;

		Vector tmp_pUp;
		VectorRotate(userUpVector, aimRotationMatrix, tmp_pUp);
		VectorScale(aimVector, DotProduct(aimVector, tmp_pUp), tmpV);
		Vector pUp;
		VectorSubtract(tmp_pUp, tmpV, pUp);
		VectorNormalizeFast(pUp);

		Vector tmp_pParentUp;
		VectorRotate(userUpVector, boneLocalToWorld, tmp_pParentUp);
		VectorScale(aimVector, DotProduct(aimVector, tmp_pParentUp), tmpV);
		Vector pParentUp;
		VectorSubtract(tmp_pParentUp, tmpV, pParentUp);
		VectorNormalizeFast(pParentUp);

		Quaternion upRotation;
		//Assert( 1.0f - fabs( DotProduct( pUp, pParentUp ) ) > FLT_EPSILON );
		if (1.0f - fabs(DotProduct(pUp, pParentUp)) > FLT_EPSILON)
		{
			angle = acos(DotProduct(pUp, pParentUp));
			CrossProduct(pUp, pParentUp, axis);
		}
		else
		{
			angle = 0;
			axis = pUp;
		}

		VectorNormalizeFast(axis);
		AxisAngleQuaternion(axis, RAD2DEG(angle), upRotation);

		Quaternion boneRotation;
		QuaternionMult(upRotation, aimRotation, boneRotation);
		QuaternionMatrix(boneRotation, aimWorldPosition, boneMatrix);
	}
	else
	{
		QuaternionMatrix(aimRotation, aimWorldPosition, boneMatrix);
	}

	MatrixCopy(boneMatrix, bonetoworld->GetBoneForWrite(iBone));
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

bool CStudioHdr::CalcProceduralBone(
	int iBone,
	IBoneAccessor* bonetoworld
)
{
	mstudiobone_t* pbones = this->pBone(0);

	if (this->boneFlags(iBone) & BONE_ALWAYS_PROCEDURAL)
	{
		switch (pbones[iBone].proctype)
		{
		case STUDIO_PROC_AXISINTERP:
			DoAxisInterpBone(pbones, iBone, bonetoworld);
			return true;

		case STUDIO_PROC_QUATINTERP:
			DoQuatInterpBone(pbones, iBone, bonetoworld);
			return true;

		case STUDIO_PROC_AIMATBONE:
			DoAimAtBone(pbones, iBone, bonetoworld, this, false);
			return true;

		case STUDIO_PROC_AIMATATTACH:
			DoAimAtBone(pbones, iBone, bonetoworld, this, true);
			return true;

		default:
			return false;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CStudioHdr::Studio_BuildMatrices(
	const QAngle& angles,
	const Vector& origin,
	const Vector pos[],
	const Quaternion q[],
	int iBone,
	float flScale,
	matrix3x4_t bonetoworld[MAXSTUDIOBONES],
	int boneMask
)
{
	int i, j;

	int					chain[MAXSTUDIOBONES] = {};
	int					chainlength = 0;

	if (iBone < -1 || iBone >= this->numbones())
		iBone = 0;

	// build list of what bones to use
	if (iBone == -1)
	{
		// all bones
		chainlength = this->numbones();
		for (i = 0; i < this->numbones(); i++)
		{
			chain[chainlength - i - 1] = i;
		}
	}
	else
	{
		// only the parent bones
		i = iBone;
		while (i != -1)
		{
			chain[chainlength++] = i;
			i = this->boneParent(i);
		}
	}

	matrix3x4_t bonematrix;
	matrix3x4_t rotationmatrix; // model to world transformation
	AngleMatrix(angles, origin, rotationmatrix);

	// Account for a change in scale
	if (flScale < 1.0f - FLT_EPSILON || flScale > 1.0f + FLT_EPSILON)
	{
		Vector vecOffset;
		MatrixGetColumn(rotationmatrix, 3, vecOffset);
		vecOffset -= origin;
		vecOffset *= flScale;
		vecOffset += origin;
		MatrixSetColumn(vecOffset, 3, rotationmatrix);

		// Scale it uniformly
		VectorScale(rotationmatrix[0], flScale, rotationmatrix[0]);
		VectorScale(rotationmatrix[1], flScale, rotationmatrix[1]);
		VectorScale(rotationmatrix[2], flScale, rotationmatrix[2]);
	}

	for (j = chainlength - 1; j >= 0; j--)
	{
		i = chain[j];
		if (this->boneFlags(i) & boneMask)
		{
			QuaternionMatrix(q[i], pos[i], bonematrix);

			if (this->boneParent(i) == -1)
			{
				ConcatTransforms(rotationmatrix, bonematrix, bonetoworld[i]);
			}
			else
			{
				ConcatTransforms(bonetoworld[this->boneParent(i)], bonematrix, bonetoworld[i]);
			}
		}
	}
}

void CStudioHdr::Studio_CalcBoneToBoneTransform( int inputBoneIndex, int outputBoneIndex, matrix3x4_t& matrixOut)
{
	mstudiobone_t* pbone = this->pBone(inputBoneIndex);

	matrix3x4_t inputToPose;
	MatrixInvert(pbone->poseToBone, inputToPose);
	ConcatTransforms(this->pBone(outputBoneIndex)->poseToBone, inputToPose, matrixOut);
}

//-----------------------------------------------------------------------------
// Purpose:  Lookup a bone controller
//-----------------------------------------------------------------------------

static mstudiobonecontroller_t* FindController(const IStudioHdr* pStudioHdr, int iController)
{
	// find first controller that matches the index
	for (int i = 0; i < pStudioHdr->numbonecontrollers(); i++)
	{
		if (pStudioHdr->pBonecontroller(i)->inputfield == iController)
			return pStudioHdr->pBonecontroller(i);
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: converts a ranged bone controller value into a 0..1 encoded value
// Output: 	ctlValue contains 0..1 encoding.
//			returns clamped ranged value
//-----------------------------------------------------------------------------

float CStudioHdr::Studio_SetController( int iController, float flValue, float& ctlValue)
{
	if (!this)
		return flValue;

	mstudiobonecontroller_t* pbonecontroller = FindController(this, iController);
	if (!pbonecontroller)
	{
		ctlValue = 0;
		return flValue;
	}

	// wrap 0..360 if it's a rotational controller
	if (pbonecontroller->type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR))
	{
		// ugly hack, invert value if end < start
		if (pbonecontroller->end < pbonecontroller->start)
			flValue = -flValue;

		// does the controller not wrap?
		if (pbonecontroller->start + 359.0 >= pbonecontroller->end)
		{
			if (flValue > ((pbonecontroller->start + pbonecontroller->end) / 2.0) + 180)
				flValue = flValue - 360;
			if (flValue < ((pbonecontroller->start + pbonecontroller->end) / 2.0) - 180)
				flValue = flValue + 360;
		}
		else
		{
			if (flValue > 360)
				flValue = flValue - (int)(flValue / 360.0) * 360.0;
			else if (flValue < 0)
				flValue = flValue + (int)((flValue / -360.0) + 1) * 360.0;
		}
	}

	ctlValue = (flValue - pbonecontroller->start) / (pbonecontroller->end - pbonecontroller->start);
	if (ctlValue < 0) ctlValue = 0;
	if (ctlValue > 1) ctlValue = 1;

	float flReturnVal = ((1.0 - ctlValue) * pbonecontroller->start + ctlValue * pbonecontroller->end);

	// ugly hack, invert value if a rotational controller and end < start
	if (pbonecontroller->type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR) &&
		pbonecontroller->end < pbonecontroller->start)
	{
		flReturnVal *= -1;
	}

	return flReturnVal;
}

//-----------------------------------------------------------------------------
// Purpose: converts a 0..1 encoded bone controller value into a ranged value
// Output: 	returns ranged value
//-----------------------------------------------------------------------------

float CStudioHdr::Studio_GetController( int iController, float ctlValue)
{
	if (!this)
		return 0.0;

	mstudiobonecontroller_t* pbonecontroller = FindController(this, iController);
	if (!pbonecontroller)
		return 0;

	return ctlValue * (pbonecontroller->end - pbonecontroller->start) + pbonecontroller->start;
}

//-----------------------------------------------------------------------------
// Purpose: Calculates default values for the pose parameters
// Output: 	fills in an array
//-----------------------------------------------------------------------------

void CStudioHdr::Studio_CalcDefaultPoseParameters( float flPoseParameter[MAXSTUDIOPOSEPARAM], int nCount)
{
	int nPoseCount = this->GetNumPoseParameters();
	int nNumParams = MIN(nCount, MAXSTUDIOPOSEPARAM);

	for (int i = 0; i < nNumParams; ++i)
	{
		// Default to middle of the pose parameter range
		flPoseParameter[i] = 0.5f;
		if (i < nPoseCount)
		{
			const mstudioposeparamdesc_t& Pose = ((IStudioHdr*)this)->pPoseParameter(i);

			// Want to try for a zero state.  If one doesn't exist set it to .5 by default.
			if (Pose.start < 0.0f && Pose.end > 0.0f)
			{
				float flPoseDelta = Pose.end - Pose.start;
				flPoseParameter[i] = -Pose.start / flPoseDelta;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: converts a 0..1 encoded pose parameter value into a ranged value
// Output: 	returns ranged value
//-----------------------------------------------------------------------------

float CStudioHdr::Studio_GetPoseParameter( int iParameter, float ctlValue)
{
	if (iParameter < 0 || iParameter >= this->GetNumPoseParameters())
	{
		return 0;
	}

	const mstudioposeparamdesc_t& PoseParam = ((IStudioHdr*)this)->pPoseParameter(iParameter);

	return ctlValue * (PoseParam.end - PoseParam.start) + PoseParam.start;
}

//-----------------------------------------------------------------------------
// Purpose: converts a ranged pose parameter value into a 0..1 encoded value
// Output: 	ctlValue contains 0..1 encoding.
//			returns clamped ranged value
//-----------------------------------------------------------------------------

float CStudioHdr::Studio_SetPoseParameter( int iParameter, float flValue, float& ctlValue)
{
	if (iParameter < 0 || iParameter >= this->GetNumPoseParameters())
	{
		return 0;
	}

	const mstudioposeparamdesc_t& PoseParam = ((IStudioHdr*)this)->pPoseParameter(iParameter);

	Assert(IsFinite(flValue));

	if (PoseParam.loop)
	{
		float wrap = (PoseParam.start + PoseParam.end) / 2.0 + PoseParam.loop / 2.0;
		float shift = PoseParam.loop - wrap;

		flValue = flValue - PoseParam.loop * floor((flValue + shift) / PoseParam.loop);
	}

	ctlValue = (flValue - PoseParam.start) / (PoseParam.end - PoseParam.start);

	if (ctlValue < 0) ctlValue = 0;
	if (ctlValue > 1) ctlValue = 1;

	Assert(IsFinite(ctlValue));

	return ctlValue * (PoseParam.end - PoseParam.start) + PoseParam.start;
}

//-----------------------------------------------------------------------------
// Purpose: returns max frame number for a sequence
//-----------------------------------------------------------------------------

int CStudioHdr::Studio_MaxFrame( int iSequence, const float poseParameter[]) const
{
	mstudioanimdesc_t* panim[4];
	float	weight[4];

	mstudioseqdesc_t& seqdesc = ((IStudioHdr*)this)->pSeqdesc(iSequence);
	this->Studio_SeqAnims(seqdesc, iSequence, poseParameter, panim, weight);

	float maxFrame = 0;
	for (int i = 0; i < 4; i++)
	{
		if (weight[i] > 0)
		{
			maxFrame += panim[i]->numframes * weight[i];
		}
	}

	if (maxFrame > 1)
		maxFrame -= 1;


	// FIXME: why does the weights sometimes not exactly add it 1.0 and this sometimes rounds down?
	return (maxFrame + 0.01);
}

//-----------------------------------------------------------------------------
// Purpose: returns frames per second of a sequence
//-----------------------------------------------------------------------------

float CStudioHdr::Studio_FPS( int iSequence, const float poseParameter[])
{
	mstudioanimdesc_t* panim[4];
	float	weight[4];

	mstudioseqdesc_t& seqdesc = ((IStudioHdr*)this)->pSeqdesc(iSequence);
	this->Studio_SeqAnims(seqdesc, iSequence, poseParameter, panim, weight);

	float t = 0;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i] > 0)
		{
			t += panim[i]->fps * weight[i];
		}
	}
	return t;
}

//-----------------------------------------------------------------------------
// Purpose: returns cycles per second of a sequence (cycles/second)
//-----------------------------------------------------------------------------

float CStudioHdr::Studio_CPS( mstudioseqdesc_t& seqdesc, int iSequence, const float poseParameter[]) const
{
	mstudioanimdesc_t* panim[4];
	float	weight[4];

	this->Studio_SeqAnims(seqdesc, iSequence, poseParameter, panim, weight);

	float t = 0;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i] > 0 && panim[i]->numframes > 1)
		{
			t += (panim[i]->fps / (panim[i]->numframes - 1)) * weight[i];
		}
	}
	return t;
}

//-----------------------------------------------------------------------------
// Purpose: returns length (in seconds) of a sequence (seconds/cycle)
//-----------------------------------------------------------------------------

float CStudioHdr::Studio_Duration( int iSequence, const float poseParameter[])
{
	mstudioseqdesc_t& seqdesc = ((IStudioHdr*)this)->pSeqdesc(iSequence);
	float cps = this->Studio_CPS( seqdesc, iSequence, poseParameter);

	if (cps == 0)
		return 0.0f;

	return 1.0f / cps;
}

//-----------------------------------------------------------------------------
// Purpose: calculate instantaneous velocity in ips at a given point 
//			in the animations cycle
// Output:	velocity vector, relative to identity orientation
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------

bool Studio_AnimVelocity(mstudioanimdesc_t* panim, float flCycle, Vector& vecVelocity)
{
	float	prevframe = 0;

	float	flFrame = flCycle * (panim->numframes - 1);
	flFrame = flFrame - (int)(flFrame / (panim->numframes - 1));

	for (int i = 0; i < panim->nummovements; i++)
	{
		mstudiomovement_t* pmove = panim->pMovement(i);

		if (pmove->endframe >= flFrame)
		{
			float f = (flFrame - prevframe) / (pmove->endframe - prevframe);

			float vel = pmove->v0 * (1 - f) + pmove->v1 * f;
			// scale from per block to per sec velocity
			vel = vel * panim->fps / (pmove->endframe - prevframe);

			vecVelocity = pmove->vector * vel;
			return true;
		}
		else
		{
			prevframe = pmove->endframe;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: calculate instantaneous velocity in ips at a given point in the sequence's cycle
// Output:	velocity vector, relative to identity orientation
//			returns false if sequence is not a movement sequence
//-----------------------------------------------------------------------------

bool CStudioHdr::Studio_SeqVelocity( int iSequence, float flCycle, const float poseParameter[], Vector& vecVelocity)
{
	mstudioanimdesc_t* panim[4];
	float	weight[4];

	mstudioseqdesc_t& seqdesc = ((IStudioHdr*)this)->pSeqdesc(iSequence);
	this->Studio_SeqAnims(seqdesc, iSequence, poseParameter, panim, weight);

	vecVelocity.Init();

	bool found = false;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			Vector vecLocalVelocity;

			if (Studio_AnimVelocity(panim[i], flCycle, vecLocalVelocity))
			{
				vecVelocity = vecVelocity + vecLocalVelocity * weight[i];
				found = true;
			}
		}
	}
	return found;
}

//-----------------------------------------------------------------------------
// Purpose: finds how much of an animation to play to move given linear distance
//-----------------------------------------------------------------------------

float Studio_FindAnimDistance(mstudioanimdesc_t* panim, float flDist)
{
	float	prevframe = 0;

	if (flDist <= 0)
		return 0.0;

	for (int i = 0; i < panim->nummovements; i++)
	{
		mstudiomovement_t* pmove = panim->pMovement(i);

		float flMove = (pmove->v0 + pmove->v1) * 0.5;

		if (flMove >= flDist)
		{
			float root1, root2;

			// d = V0 * t + 1/2 (V1-V0) * t^2
			if (SolveQuadratic(0.5 * (pmove->v1 - pmove->v0), pmove->v0, -flDist, root1, root2))
			{
				float cpf = 1.0 / (panim->numframes - 1);  // cycles per frame

				return (prevframe + root1 * (pmove->endframe - prevframe)) * cpf;
			}
			return 0.0;
		}
		else
		{
			flDist -= flMove;
			prevframe = pmove->endframe;
		}
	}
	return 1.0;
}

//-----------------------------------------------------------------------------
// Purpose: finds how much of an sequence to play to move given linear distance
//-----------------------------------------------------------------------------

float CStudioHdr::Studio_FindSeqDistance( int iSequence, const float poseParameter[], float flDist)
{
	mstudioanimdesc_t* panim[4];
	float	weight[4];

	mstudioseqdesc_t& seqdesc = ((IStudioHdr*)this)->pSeqdesc(iSequence);
	this->Studio_SeqAnims(seqdesc, iSequence, poseParameter, panim, weight);

	float flCycle = 0;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			float flLocalCycle = Studio_FindAnimDistance(panim[i], flDist);
			flCycle = flCycle + flLocalCycle * weight[i];
		}
	}
	return flCycle;
}

//-----------------------------------------------------------------------------
// Purpose: lookup attachment by name
//-----------------------------------------------------------------------------

int CStudioHdr::Studio_FindAttachment( const char* pAttachmentName)
{
	if (this && this->SequencesAvailable())
	{
		// Extract the bone index from the name
		for (int i = 0; i < this->GetNumAttachments(); i++)
		{
			if (!V_stricmp(pAttachmentName, ((IStudioHdr*)this)->pAttachment(i).pszName()))
			{
				return i;
			}
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: lookup attachments by substring. Randomly return one of the matching attachments.
//-----------------------------------------------------------------------------

int CStudioHdr::Studio_FindRandomAttachment( const char* pAttachmentName)
{
	if (this)
	{
		// First move them all matching attachments into a list
		CUtlVector<int> matchingAttachments;

		// Extract the bone index from the name
		for (int i = 0; i < this->GetNumAttachments(); i++)
		{
			if (strstr(((IStudioHdr*)this)->pAttachment(i).pszName(), pAttachmentName))
			{
				matchingAttachments.AddToTail(i);
			}
		}

		// Then randomly return one of the attachments
		if (matchingAttachments.Size() > 0)
			return matchingAttachments[RandomInt(0, matchingAttachments.Size() - 1)];
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: lookup bone by name
//-----------------------------------------------------------------------------

int CStudioHdr::Studio_BoneIndexByName( const char* pName) const
{
	if (this)
	{
		// binary search for the bone matching pName
		int start = 0, end = this->numbones() - 1;
		const byte* pBoneTable = this->GetBoneTableSortedByName();
		mstudiobone_t* pbones = this->pBone(0);
		while (start <= end)
		{
			int mid = (start + end) >> 1;
			int cmp = Q_stricmp(pbones[pBoneTable[mid]].pszName(), pName);

			if (cmp < 0)
			{
				start = mid + 1;
			}
			else if (cmp > 0)
			{
				end = mid - 1;
			}
			else
			{
				return pBoneTable[mid];
			}
		}
	}

	return -1;
}

const char* CStudioHdr::Studio_GetDefaultSurfaceProps()
{
	return this->pszSurfaceProp();
}

float CStudioHdr::Studio_GetMass()
{
	if (this == NULL) return 0.f;

	return this->mass();
}

//-----------------------------------------------------------------------------
// Purpose: return pointer to sequence key value buffer
//-----------------------------------------------------------------------------

const char* CStudioHdr::Studio_GetKeyValueText( int iSequence)
{
	if (this && this->SequencesAvailable())
	{
		if (iSequence >= 0 && iSequence < this->GetNumSeq())
		{
			return ((IStudioHdr*)this)->pSeqdesc(iSequence).KeyValueText();
		}
	}
	return NULL;
}

bool CStudioHdr::Studio_PrefetchSequence( int iSequence)
{
	bool pendingload = false;
	mstudioseqdesc_t& seqdesc = ((IStudioHdr*)this)->pSeqdesc(iSequence);
	int size0 = seqdesc.groupsize[0];
	int size1 = seqdesc.groupsize[1];
	for (int i = 0; i < size0; ++i)
	{
		for (int j = 0; j < size1; ++j)
		{
			mstudioanimdesc_t& animdesc = ((IStudioHdr*)this)->pAnimdesc(seqdesc.anim(i, j));
			int iFrame = 0;
			mstudioanim_t* panim = animdesc.pAnim(this, &iFrame);
			if (!panim)
			{
				pendingload = true;
			}
		}
	}

	// Everything for this sequence is resident?
	return !pendingload;
}

//-----------------------------------------------------------------------------
// Purpose: Drive a flex controller from a component of a bone
//-----------------------------------------------------------------------------
void CStudioHdr::Studio_RunBoneFlexDrivers(float* pflFlexControllerWeights,  const Vector* pvPositions, const matrix3x4_t* pBoneToWorld, const matrix3x4_t& mRootToWorld)
{
	bool bRootToWorldInvComputed = false;
	matrix3x4_t mRootToWorldInv;
	matrix3x4_t mParentInv;
	matrix3x4_t mBoneLocal;

	const int nBoneFlexDriverCount = this->BoneFlexDriverCount();

	for (int i = 0; i < nBoneFlexDriverCount; ++i)
	{
		const mstudioboneflexdriver_t* pBoneFlexDriver = this->BoneFlexDriver(i);
		const mstudiobone_t* pStudioBone = this->pBone(pBoneFlexDriver->m_nBoneIndex);

		const int nControllerCount = pBoneFlexDriver->m_nControlCount;

		if (pStudioBone->flags & BONE_USED_BY_BONE_MERGE)
		{
			// The local space version of the bone is not available if this is a bonemerged bone
			// so do the slow computation of the local version of the bone from boneToWorld

			if (pStudioBone->parent < 0)
			{
				if (!bRootToWorldInvComputed)
				{
					MatrixInvert(mRootToWorld, mRootToWorldInv);
					bRootToWorldInvComputed = true;
				}

				MatrixMultiply(mRootToWorldInv, pBoneToWorld[pBoneFlexDriver->m_nBoneIndex], mBoneLocal);
			}
			else
			{
				MatrixInvert(pBoneToWorld[pStudioBone->parent], mParentInv);
				MatrixMultiply(mParentInv, pBoneToWorld[pBoneFlexDriver->m_nBoneIndex], mBoneLocal);
			}

			for (int j = 0; j < nControllerCount; ++j)
			{
				const mstudioboneflexdrivercontrol_t* pController = pBoneFlexDriver->pBoneFlexDriverControl(j);
				const mstudioflexcontroller_t* pFlexController = this->pFlexcontroller(static_cast<LocalFlexController_t>(pController->m_nFlexControllerIndex));

				if (pFlexController->localToGlobal < 0)
					continue;

				Assert(pController->m_nFlexControllerIndex >= 0 && pController->m_nFlexControllerIndex < this->numflexcontrollers());
				Assert(pController->m_nBoneComponent >= 0 && pController->m_nBoneComponent <= 2);
				pflFlexControllerWeights[pFlexController->localToGlobal] =
					RemapValClamped(mBoneLocal[pController->m_nBoneComponent][3], pController->m_flMin, pController->m_flMax, 0.0f, 1.0f);
			}
		}
		else
		{
			// Use the local space version of the bone directly for non-bonemerged bones

			const Vector& position = pvPositions[pBoneFlexDriver->m_nBoneIndex];

			for (int j = 0; j < nControllerCount; ++j)
			{
				const mstudioboneflexdrivercontrol_t* pController = pBoneFlexDriver->pBoneFlexDriverControl(j);
				const mstudioflexcontroller_t* pFlexController = this->pFlexcontroller(static_cast<LocalFlexController_t>(pController->m_nFlexControllerIndex));

				if (pFlexController->localToGlobal < 0)
					continue;

				Assert(pController->m_nFlexControllerIndex >= 0 && pController->m_nFlexControllerIndex < this->numflexcontrollers());
				Assert(pController->m_nBoneComponent >= 0 && pController->m_nBoneComponent <= 2);
				pflFlexControllerWeights[pFlexController->localToGlobal] =
					RemapValClamped(position[pController->m_nBoneComponent], pController->m_flMin, pController->m_flMax, 0.0f, 1.0f);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: turn a specific bones boneToWorld transform into a pos and q in parents bonespace
//-----------------------------------------------------------------------------
void CStudioHdr::SolveBone(
	int	iBone,
	matrix3x4_t* pBoneToWorld,
	Vector pos[],
	Quaternion q[]
) const
{
	int iParent = this->boneParent(iBone);

	matrix3x4_t worldToBone;
	MatrixInvert(pBoneToWorld[iParent], worldToBone);

	matrix3x4_t local;
	ConcatTransforms(worldToBone, pBoneToWorld[iBone], local);

	MatrixAngles(local, q[iBone], pos[iBone]);
}

//-----------------------------------------------------------------------------
// Purpose: blend together q1,pos1 with q2,pos2.  Return result in q1,pos1.  
//			0 returns q1, pos1.  1 returns q2, pos2
//-----------------------------------------------------------------------------
void CStudioHdr::CalcBoneAdj(
	Vector pos[],
	Quaternion q[],
	const float controllers[],
	int boneMask
) const
{
	int					i, j, k;
	float				value;
	mstudiobonecontroller_t* pbonecontroller;
	Vector p0;
	RadianEuler a0;
	Quaternion q0;

	for (j = 0; j < this->numbonecontrollers(); j++)
	{
		pbonecontroller = this->pBonecontroller(j);
		k = pbonecontroller->bone;

		if (this->boneFlags(k) & boneMask)
		{
			i = pbonecontroller->inputfield;
			value = controllers[i];
			if (value < 0) value = 0;
			if (value > 1.0) value = 1.0;
			value = (1.0 - value) * pbonecontroller->start + value * pbonecontroller->end;

			switch (pbonecontroller->type & STUDIO_TYPES)
			{
			case STUDIO_XR:
				a0.Init(value * (M_PI / 180.0), 0, 0);
				AngleQuaternion(a0, q0);
				QuaternionSM(1.0, q0, q[k], q[k]);
				break;
			case STUDIO_YR:
				a0.Init(0, value * (M_PI / 180.0), 0);
				AngleQuaternion(a0, q0);
				QuaternionSM(1.0, q0, q[k], q[k]);
				break;
			case STUDIO_ZR:
				a0.Init(0, 0, value * (M_PI / 180.0));
				AngleQuaternion(a0, q0);
				QuaternionSM(1.0, q0, q[k], q[k]);
				break;
			case STUDIO_X:
				pos[k].x += value;
				break;
			case STUDIO_Y:
				pos[k].y += value;
				break;
			case STUDIO_Z:
				pos[k].z += value;
				break;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Calc Zeroframe Data
//-----------------------------------------------------------------------------

static void CalcZeroframeData(const IStudioHdr* pStudioHdr, const IStudioHdr* pAnimStudioHdr, const virtualgroup_t* pAnimGroup, const mstudiobone_t* pAnimbone, mstudioanimdesc_t& animdesc, float fFrame, Vector* pos, Quaternion* q, int boneMask, float flWeight)
{
	byte* pData = animdesc.pZeroFrameData();

	if (!pData)
		return;

	int i, j;

	// Msg("zeroframe %s\n", animdesc.pszName() );
	if (animdesc.zeroframecount == 1)
	{
		for (j = 0; j < pAnimStudioHdr->numbones(); j++)
		{
			if (pAnimGroup)
				i = pAnimGroup->masterBone[j];
			else
				i = j;

			if (pAnimbone[j].flags & BONE_HAS_SAVEFRAME_POS)
			{
				if ((i >= 0) && (pStudioHdr->boneFlags(i) & boneMask))
				{
					Vector p = *(Vector48*)pData;
					pos[i] = pos[i] * (1.0f - flWeight) + p * flWeight;
					Assert(pos[i].IsValid());
				}
				pData += sizeof(Vector48);
			}
			if (pAnimbone[j].flags & BONE_HAS_SAVEFRAME_ROT)
			{
				if ((i >= 0) && (pStudioHdr->boneFlags(i) & boneMask))
				{
					Quaternion q0 = *(Quaternion64*)pData;
					QuaternionBlend(q[i], q0, flWeight, q[i]);
					Assert(q[i].IsValid());
				}
				pData += sizeof(Quaternion64);
			}
		}
	}
	else
	{
		float s1;
		int index = fFrame / animdesc.zeroframespan;
		if (index >= animdesc.zeroframecount - 1)
		{
			index = animdesc.zeroframecount - 2;
			s1 = 1.0f;
		}
		else
		{
			s1 = clamp((fFrame - index * animdesc.zeroframespan) / animdesc.zeroframespan, 0.0f, 1.0f);
		}
		int i0 = max(index - 1, 0);
		int i1 = index;
		int i2 = min(index + 1, animdesc.zeroframecount - 1);
		for (j = 0; j < pAnimStudioHdr->numbones(); j++)
		{
			if (pAnimGroup)
				i = pAnimGroup->masterBone[j];
			else
				i = j;

			if (pAnimbone[j].flags & BONE_HAS_SAVEFRAME_POS)
			{
				if ((i >= 0) && (pStudioHdr->boneFlags(i) & boneMask))
				{
					Vector p0 = *(((Vector48*)pData) + i0);
					Vector p1 = *(((Vector48*)pData) + i1);
					Vector p2 = *(((Vector48*)pData) + i2);
					Vector p3;
					Hermite_Spline(p0, p1, p2, s1, p3);
					pos[i] = pos[i] * (1.0f - flWeight) + p3 * flWeight;
					Assert(pos[i].IsValid());
				}
				pData += sizeof(Vector48) * animdesc.zeroframecount;
			}
			if (pAnimbone[j].flags & BONE_HAS_SAVEFRAME_ROT)
			{
				if ((i >= 0) && (pStudioHdr->boneFlags(i) & boneMask))
				{
					Quaternion q0 = *(((Quaternion64*)pData) + i0);
					Quaternion q1 = *(((Quaternion64*)pData) + i1);
					Quaternion q2 = *(((Quaternion64*)pData) + i2);
					if (flWeight == 1.0f)
					{
						Hermite_Spline(q0, q1, q2, s1, q[i]);
					}
					else
					{
						Quaternion q3;
						Hermite_Spline(q0, q1, q2, s1, q3);
						QuaternionBlend(q[i], q3, flWeight, q[i]);
					}
					Assert(q[i].IsValid());
				}
				pData += sizeof(Quaternion64) * animdesc.zeroframecount;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

static void CalcDecompressedAnimation(const mstudiocompressedikerror_t* pCompressed, int iFrame, float fraq, Vector& pos, Quaternion& q)
{
	if (fraq > 0.0001f)
	{
		Vector p1, p2;
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(0), pCompressed->scale[0], p1.x, p2.x);
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(1), pCompressed->scale[1], p1.y, p2.y);
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(2), pCompressed->scale[2], p1.z, p2.z);
		pos = p1 * (1 - fraq) + p2 * fraq;

		Quaternion			q1, q2;
		RadianEuler			angle1, angle2;
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(3), pCompressed->scale[3], angle1.x, angle2.x);
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(4), pCompressed->scale[4], angle1.y, angle2.y);
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(5), pCompressed->scale[5], angle1.z, angle2.z);

		if (angle1.x != angle2.x || angle1.y != angle2.y || angle1.z != angle2.z)
		{
			AngleQuaternion(angle1, q1);
			AngleQuaternion(angle2, q2);
			QuaternionBlend(q1, q2, fraq, q);
		}
		else
		{
			AngleQuaternion(angle1, q);
		}
	}
	else
	{
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(0), pCompressed->scale[0], pos.x);
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(1), pCompressed->scale[1], pos.y);
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(2), pCompressed->scale[2], pos.z);

		RadianEuler			angle;
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(3), pCompressed->scale[3], angle.x);
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(4), pCompressed->scale[4], angle.y);
		ExtractAnimValue(iFrame, pCompressed->pAnimvalue(5), pCompressed->scale[5], angle.z);

		AngleQuaternion(angle, q);
	}
}

//-----------------------------------------------------------------------------
// Purpose: translate animations done in a non-standard parent space
//-----------------------------------------------------------------------------
static void CalcLocalHierarchyAnimation(
	const IStudioHdr* pStudioHdr,
	matrix3x4_t* boneToWorld,
	CBoneBitList& boneComputed,
	Vector* pos,
	Quaternion* q,
	//const mstudioanimdesc_t &animdesc,
	const mstudiobone_t* pbone,
	mstudiolocalhierarchy_t* pHierarchy,
	int iBone,
	int iNewParent,
	float cycle,
	int iFrame,
	float flFraq,
	int boneMask
)
{
#ifdef STAGING_ONLY
	Assert(iNewParent == -1 || (iNewParent >= 0 && iNewParent < MAXSTUDIOBONES));
	Assert(iBone > 0);
	Assert(iBone < MAXSTUDIOBONES);
#endif // STAGING_ONLY

	Vector localPos;
	Quaternion localQ;

	// make fake root transform
	static ALIGN16 matrix3x4_t rootXform ALIGN16_POST(1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1.0f, 0);

	// FIXME: missing check to see if seq has a weight for this bone
	float weight = 1.0f;

	// check to see if there's a ramp on the influence
	if (pHierarchy->tail - pHierarchy->peak < 1.0f)
	{
		float index = cycle;

		if (pHierarchy->end > 1.0f && index < pHierarchy->start)
			index += 1.0f;

		if (index < pHierarchy->start)
			return;
		if (index >= pHierarchy->end)
			return;

		if (index < pHierarchy->peak && pHierarchy->start != pHierarchy->peak)
		{
			weight = (index - pHierarchy->start) / (pHierarchy->peak - pHierarchy->start);
		}
		else if (index > pHierarchy->tail && pHierarchy->end != pHierarchy->tail)
		{
			weight = (pHierarchy->end - index) / (pHierarchy->end - pHierarchy->tail);
		}

		weight = SimpleSpline(weight);
	}

	CalcDecompressedAnimation(pHierarchy->pLocalAnim(), iFrame - pHierarchy->iStart, flFraq, localPos, localQ);

	pStudioHdr->BuildBoneChain( rootXform, pos, q, iBone, boneToWorld, boneComputed);

	matrix3x4_t localXform;
	AngleMatrix(localQ, localPos, localXform);

	if (iNewParent != -1)
	{
		pStudioHdr->BuildBoneChain( rootXform, pos, q, iNewParent, boneToWorld, boneComputed);
		ConcatTransforms(boneToWorld[iNewParent], localXform, boneToWorld[iBone]);
	}
	else
	{
		boneToWorld[iBone] = localXform;
	}

	// back solve
	Vector p1;
	Quaternion q1;
	int n = pbone[iBone].parent;
	if (n == -1)
	{
		if (weight == 1.0f)
		{
			MatrixAngles(boneToWorld[iBone], q[iBone], pos[iBone]);
		}
		else
		{
			MatrixAngles(boneToWorld[iBone], q1, p1);
			QuaternionSlerp(q[iBone], q1, weight, q[iBone]);
			pos[iBone] = Lerp(weight, p1, pos[iBone]);
		}
	}
	else
	{
		matrix3x4_t worldToBone;
		MatrixInvert(boneToWorld[n], worldToBone);

		matrix3x4_t local;
		ConcatTransforms(worldToBone, boneToWorld[iBone], local);
		if (weight == 1.0f)
		{
			MatrixAngles(local, q[iBone], pos[iBone]);
		}
		else
		{
			MatrixAngles(local, q1, p1);
			QuaternionSlerp(q[iBone], q1, weight, q[iBone]);
			pos[iBone] = Lerp(weight, p1, pos[iBone]);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Find and decode a sub-frame of animation, remapping the skeleton bone indexes
//-----------------------------------------------------------------------------
static void CalcVirtualAnimation(IVirtualModel* pVModel, const IStudioHdr* pStudioHdr, Vector* pos, Quaternion* q,
	mstudioseqdesc_t& seqdesc, int sequence, int animation,
	float cycle, int boneMask)
{
	//int	i, k;

	const mstudiobone_t* pbone;
	const virtualgroup_t* pSeqGroup;
	const IStudioHdr* pSeqStudioHdr = NULL;
	const mstudiolinearbone_t* pSeqLinearBones;
	const mstudiobone_t* pSeqbone;
	const mstudioanim_t* panim;
	const IStudioHdr* pAnimStudioHdr = NULL;
	const mstudiolinearbone_t* pAnimLinearBones;
	const mstudiobone_t* pAnimbone;
	const virtualgroup_t* pAnimGroup;

	pSeqGroup = pVModel->pSeqGroup(sequence);
	int baseanimation = pStudioHdr->iRelativeAnim(sequence, animation);
	mstudioanimdesc_t& animdesc = ((IStudioHdr*)pStudioHdr)->pAnimdesc(baseanimation);
	pSeqStudioHdr = pStudioHdr->pSeqStudioHdr(sequence);
	pSeqLinearBones = pSeqStudioHdr->pLinearBones();
	pSeqbone = pSeqStudioHdr->pBone(0);
	pAnimGroup = pVModel->pAnimGroup(baseanimation);
	pAnimStudioHdr = pStudioHdr->pAnimStudioHdr(baseanimation);
	pAnimLinearBones = pAnimStudioHdr->pLinearBones();
	pAnimbone = pAnimStudioHdr->pBone(0);

	int					iFrame;
	float				s;

	float fFrame = cycle * (animdesc.numframes - 1);

	iFrame = (int)fFrame;
	s = (fFrame - iFrame);

	int iLocalFrame = iFrame;
	float flStall;
	panim = animdesc.pAnim(pStudioHdr, &iLocalFrame, flStall);

	float* pweight = seqdesc.pBoneweight(0);
	pbone = pStudioHdr->pBone(0);

	for (int i = 0; i < pStudioHdr->numbones(); i++)
	{
		if (pStudioHdr->boneFlags(i) & boneMask)
		{
			int j = pSeqGroup->boneMap[i];
			if (j >= 0 && pweight[j] > 0.0f)
			{
				if (animdesc.flags & STUDIO_DELTA)
				{
					q[i].Init(0.0f, 0.0f, 0.0f, 1.0f);
					pos[i].Init(0.0f, 0.0f, 0.0f);
				}
				else if (pSeqLinearBones)
				{
					q[i] = pSeqLinearBones->quat(j);
					pos[i] = pSeqLinearBones->pos(j);
				}
				else
				{
					q[i] = pSeqbone[j].quat;
					pos[i] = pSeqbone[j].pos;
				}
#ifdef STUDIO_ENABLE_PERF_COUNTERS
				pStudioHdr->IncPerfUsedBones();
#endif
			}
		}
	}

	// if the animation isn't available, look for the zero frame cache
	if (!panim)
	{
		CalcZeroframeData(((IStudioHdr*)pStudioHdr), pAnimStudioHdr, pAnimGroup, pAnimbone, animdesc, fFrame, pos, q, boneMask, 1.0);
		return;
	}

	// FIXME: change encoding so that bone -1 is never the case
	while (panim && panim->bone < 255)
	{
		int j = pAnimGroup->masterBone[panim->bone];
		if (j >= 0 && (pStudioHdr->boneFlags(j) & boneMask))
		{
			int k = pSeqGroup->boneMap[j];

			if (k >= 0 && pweight[k] > 0.0f)
			{
				CalcBoneQuaternion(iLocalFrame, s, &pAnimbone[panim->bone], pAnimLinearBones, panim, q[j]);
				CalcBonePosition(iLocalFrame, s, &pAnimbone[panim->bone], pAnimLinearBones, panim, pos[j]);
#ifdef STUDIO_ENABLE_PERF_COUNTERS
				pStudioHdr->IncPerfAnimatedBones();
#endif
			}
		}
		panim = panim->pNext();
	}

	// cross fade in previous zeroframe data
	if (flStall > 0.0f)
	{
		CalcZeroframeData(pStudioHdr, pAnimStudioHdr, pAnimGroup, pAnimbone, animdesc, fFrame, pos, q, boneMask, flStall);
	}

	// calculate a local hierarchy override
	if (animdesc.numlocalhierarchy)
	{
		matrix3x4_t* boneToWorld = g_MatrixPool.Alloc();
		CBoneBitList boneComputed;

		int i;
		for (i = 0; i < animdesc.numlocalhierarchy; i++)
		{
			mstudiolocalhierarchy_t* pHierarchy = animdesc.pHierarchy(pStudioHdr, i);

			if (!pHierarchy)
				break;

			int iBone = pAnimGroup->masterBone[pHierarchy->iBone];
			if (iBone >= 0 && (pStudioHdr->boneFlags(iBone) & boneMask))
			{
				if (pHierarchy->iNewParent != -1)
				{
					int iNewParent = pAnimGroup->masterBone[pHierarchy->iNewParent];
					if (iNewParent >= 0 && (pStudioHdr->boneFlags(iNewParent) & boneMask))
					{
						CalcLocalHierarchyAnimation(pStudioHdr, boneToWorld, boneComputed, pos, q, pbone, pHierarchy, iBone, iNewParent, cycle, iFrame, s, boneMask);
					}
				}
				else
				{
					CalcLocalHierarchyAnimation(pStudioHdr, boneToWorld, boneComputed, pos, q, pbone, pHierarchy, iBone, -1, cycle, iFrame, s, boneMask);
				}
			}
		}

		g_MatrixPool.Free(boneToWorld);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Find and decode a sub-frame of animation
//-----------------------------------------------------------------------------

static void CalcAnimation(const IStudioHdr* pStudioHdr, Vector* pos, Quaternion* q,
	mstudioseqdesc_t& seqdesc,
	int sequence, int animation,
	float cycle, int boneMask)
{
#ifdef STUDIO_ENABLE_PERF_COUNTERS
	pStudioHdr->IncPerfAnimationLayers();
#endif

	IVirtualModel* pVModel = pStudioHdr->GetVirtualModel();

	if (pVModel)
	{
		CalcVirtualAnimation(pVModel, pStudioHdr, pos, q, seqdesc, sequence, animation, cycle, boneMask);
		return;
	}

	mstudioanimdesc_t& animdesc = ((IStudioHdr*)pStudioHdr)->pAnimdesc(animation);
	mstudiobone_t* pbone = pStudioHdr->pBone(0);
	const mstudiolinearbone_t* pLinearBones = pStudioHdr->pLinearBones();

	//	int					i;
	int					iFrame;
	float				s;

	float fFrame = cycle * (animdesc.numframes - 1);

	iFrame = (int)fFrame;
	s = (fFrame - iFrame);

	int iLocalFrame = iFrame;
	float flStall;
	mstudioanim_t* panim = animdesc.pAnim(pStudioHdr, &iLocalFrame, flStall);

	float* pweight = seqdesc.pBoneweight(0);

	// if the animation isn't available, look for the zero frame cache
	if (!panim)
	{
		// Msg("zeroframe %s\n", animdesc.pszName() );
		// pre initialize
		for (int i = 0; i < pStudioHdr->numbones(); i++, pbone++, pweight++)
		{
			if (*pweight > 0 && (pStudioHdr->boneFlags(i) & boneMask))
			{
				if (animdesc.flags & STUDIO_DELTA)
				{
					q[i].Init(0.0f, 0.0f, 0.0f, 1.0f);
					pos[i].Init(0.0f, 0.0f, 0.0f);
				}
				else
				{
					q[i] = pbone->quat;
					pos[i] = pbone->pos;
				}
			}
		}

		CalcZeroframeData(pStudioHdr, pStudioHdr, NULL, pStudioHdr->pBone(0), animdesc, fFrame, pos, q, boneMask, 1.0);

		return;
	}

	// BUGBUG: the sequence, the anim, and the model can have all different bone mappings.
	for (int i = 0; i < pStudioHdr->numbones(); i++, pbone++, pweight++)
	{
		if (panim && panim->bone == i)
		{
			if (*pweight > 0 && (pStudioHdr->boneFlags(i) & boneMask))
			{
				CalcBoneQuaternion(iLocalFrame, s, pbone, pLinearBones, panim, q[i]);
				CalcBonePosition(iLocalFrame, s, pbone, pLinearBones, panim, pos[i]);
#ifdef STUDIO_ENABLE_PERF_COUNTERS
				pStudioHdr->IncPerfAnimatedBones();
				pStudioHdr->IncPerfUsedBones();
#endif
			}
			panim = panim->pNext();
		}
		else if (*pweight > 0 && (pStudioHdr->boneFlags(i) & boneMask))
		{
			if (animdesc.flags & STUDIO_DELTA)
			{
				q[i].Init(0.0f, 0.0f, 0.0f, 1.0f);
				pos[i].Init(0.0f, 0.0f, 0.0f);
			}
			else
			{
				q[i] = pbone->quat;
				pos[i] = pbone->pos;
			}
#ifdef STUDIO_ENABLE_PERF_COUNTERS
			pStudioHdr->IncPerfUsedBones();
#endif
		}
	}

	// cross fade in previous zeroframe data
	if (flStall > 0.0f)
	{
		CalcZeroframeData(pStudioHdr, pStudioHdr, NULL, pStudioHdr->pBone(0), animdesc, fFrame, pos, q, boneMask, flStall);
	}

	if (animdesc.numlocalhierarchy)
	{
		matrix3x4_t* boneToWorld = g_MatrixPool.Alloc();
		CBoneBitList boneComputed;

		int i;
		for (i = 0; i < animdesc.numlocalhierarchy; i++)
		{
			mstudiolocalhierarchy_t* pHierarchy = animdesc.pHierarchy(pStudioHdr, i);

			if (!pHierarchy)
				break;

			if (pStudioHdr->boneFlags(pHierarchy->iBone) & boneMask)
			{
				if (pStudioHdr->boneFlags(pHierarchy->iNewParent) & boneMask)
				{
					CalcLocalHierarchyAnimation(pStudioHdr, boneToWorld, boneComputed, pos, q, pbone, pHierarchy, pHierarchy->iBone, pHierarchy->iNewParent, cycle, iFrame, s, boneMask);
				}
			}
		}

		g_MatrixPool.Free(boneToWorld);
	}

}

inline bool PoseIsAllZeros(
	const IStudioHdr* pStudioHdr,
	int sequence,
	mstudioseqdesc_t& seqdesc,
	int i0,
	int i1
)
{
	int baseanim;

	// remove "zero" positional blends
	baseanim = pStudioHdr->iRelativeAnim(sequence, seqdesc.anim(i0, i1));
	mstudioanimdesc_t& anim = ((IStudioHdr*)pStudioHdr)->pAnimdesc(baseanim);
	return (anim.flags & STUDIO_ALLZEROS) != 0;
}

//-----------------------------------------------------------------------------
// Purpose: Inter-animation blend.  Assumes both types are identical.
//			blend together q1,pos1 with q2,pos2.  Return result in q1,pos1.  
//			0 returns q1, pos1.  1 returns q2, pos2
//-----------------------------------------------------------------------------
void BlendBones(
	const IStudioHdr* pStudioHdr,
	Quaternion q1[MAXSTUDIOBONES],
	Vector pos1[MAXSTUDIOBONES],
	mstudioseqdesc_t& seqdesc,
	int sequence,
	const Quaternion q2[MAXSTUDIOBONES],
	const Vector pos2[MAXSTUDIOBONES],
	float s,
	int boneMask)
{
	int			i, j;
	Quaternion		q3;

	IVirtualModel* pVModel = pStudioHdr->GetVirtualModel();
	const virtualgroup_t* pSeqGroup = NULL;
	if (pVModel)
	{
		pSeqGroup = pVModel->pSeqGroup(sequence);
	}

	if (s <= 0)
	{
		Assert(0); // shouldn't have been called
		return;
	}
	else if (s >= 1.0)
	{
		Assert(0); // shouldn't have been called
		for (i = 0; i < pStudioHdr->numbones(); i++)
		{
			// skip unused bones
			if (!(pStudioHdr->boneFlags(i) & boneMask))
			{
				continue;
			}

			if (pSeqGroup)
			{
				j = pSeqGroup->boneMap[i];
			}
			else
			{
				j = i;
			}

			if (j >= 0 && seqdesc.weight(j) > 0.0)
			{
				q1[i] = q2[i];
				pos1[i] = pos2[i];
			}
		}
		return;
	}

	float s2 = s;
	float s1 = 1.0 - s2;

	for (i = 0; i < pStudioHdr->numbones(); i++)
	{
		// skip unused bones
		if (!(pStudioHdr->boneFlags(i) & boneMask))
		{
			continue;
		}

		if (pSeqGroup)
		{
			j = pSeqGroup->boneMap[i];
		}
		else
		{
			j = i;
		}

		if (j >= 0 && seqdesc.weight(j) > 0.0)
		{
			if (pStudioHdr->boneFlags(i) & BONE_FIXED_ALIGNMENT)
			{
				QuaternionBlendNoAlign(q2[i], q1[i], s1, q3);
			}
			else
			{
				QuaternionBlend(q2[i], q1[i], s1, q3);
			}
			q1[i][0] = q3[0];
			q1[i][1] = q3[1];
			q1[i][2] = q3[2];
			q1[i][3] = q3[3];
			pos1[i][0] = pos1[i][0] * s1 + pos2[i][0] * s2;
			pos1[i][1] = pos1[i][1] * s1 + pos2[i][1] * s2;
			pos1[i][2] = pos1[i][2] * s1 + pos2[i][2] * s2;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Scale a set of bones.  Must be of type delta
//-----------------------------------------------------------------------------
void ScaleBones(
	const IStudioHdr* pStudioHdr,
	Quaternion q1[MAXSTUDIOBONES],
	Vector pos1[MAXSTUDIOBONES],
	int sequence,
	float s,
	int boneMask)
{
	int			i, j;
	Quaternion		q3;

	mstudioseqdesc_t& seqdesc = ((IStudioHdr*)pStudioHdr)->pSeqdesc(sequence);

	IVirtualModel* pVModel = pStudioHdr->GetVirtualModel();
	const virtualgroup_t* pSeqGroup = NULL;
	if (pVModel)
	{
		pSeqGroup = pVModel->pSeqGroup(sequence);
	}

	float s2 = s;
	float s1 = 1.0 - s2;

	for (i = 0; i < pStudioHdr->numbones(); i++)
	{
		// skip unused bones
		if (!(pStudioHdr->boneFlags(i) & boneMask))
		{
			continue;
		}

		if (pSeqGroup)
		{
			j = pSeqGroup->boneMap[i];
		}
		else
		{
			j = i;
		}

		if (j >= 0 && seqdesc.weight(j) > 0.0)
		{
			QuaternionIdentityBlend(q1[i], s1, q1[i]);
			VectorScale(pos1[i], s2, pos1[i]);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: calculate a pose for a single sequence
//-----------------------------------------------------------------------------
bool CStudioHdr::CalcPoseSingle(
	Vector pos[],
	Quaternion q[],
	mstudioseqdesc_t& seqdesc,
	int sequence,
	float cycle,
	const float poseParameter[],
	int boneMask,
	float flTime
) const
{
	bool bResult = true;

	Vector* pos2 = g_VectorPool.Alloc();
	Quaternion* q2 = g_QaternionPool.Alloc();
	Vector* pos3 = g_VectorPool.Alloc();
	Quaternion* q3 = g_QaternionPool.Alloc();

	if (sequence >= this->GetNumSeq())
	{
		sequence = 0;
		seqdesc = ((IStudioHdr*)this)->pSeqdesc(sequence);
	}


	int i0 = 0, i1 = 0;
	float s0 = 0, s1 = 0;

	this->Studio_LocalPoseParameter(poseParameter, seqdesc, sequence, 0, s0, i0);
	this->Studio_LocalPoseParameter(poseParameter, seqdesc, sequence, 1, s1, i1);


	if (seqdesc.flags & STUDIO_REALTIME)
	{
		float cps = this->Studio_CPS( seqdesc, sequence, poseParameter);
		cycle = flTime * cps;
		cycle = cycle - (int)cycle;
	}
	else if (seqdesc.flags & STUDIO_CYCLEPOSE)
	{
		int iPose = this->GetSharedPoseParameter(sequence, seqdesc.cycleposeindex);
		if (iPose != -1)
		{
			/*
			const mstudioposeparamdesc_t &Pose = ((IStudioHdr *)pStudioHdr)->pPoseParameter( iPose );
			cycle = poseParameter[ iPose ] * (Pose.end - Pose.start) + Pose.start;
			*/
			cycle = poseParameter[iPose];
		}
		else
		{
			cycle = 0.0f;
		}
	}
	else if (cycle < 0 || cycle >= 1)
	{
		if (seqdesc.flags & STUDIO_LOOPING)
		{
			cycle = cycle - (int)cycle;
			if (cycle < 0) cycle += 1;
		}
		else
		{
			cycle = clamp(cycle, 0.0f, 1.0f);
		}
	}

	if (s0 < 0.001)
	{
		if (s1 < 0.001)
		{
			if (PoseIsAllZeros(this, sequence, seqdesc, i0, i1))
			{
				bResult = false;
			}
			else
			{
				CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0, i1), cycle, boneMask);
			}
		}
		else if (s1 > 0.999)
		{
			CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0, i1 + 1), cycle, boneMask);
		}
		else
		{
			CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0, i1), cycle, boneMask);
			CalcAnimation(this, pos2, q2, seqdesc, sequence, seqdesc.anim(i0, i1 + 1), cycle, boneMask);
			BlendBones(this, q, pos, seqdesc, sequence, q2, pos2, s1, boneMask);
		}
	}
	else if (s0 > 0.999)
	{
		if (s1 < 0.001)
		{
			if (PoseIsAllZeros(this, sequence, seqdesc, i0 + 1, i1))
			{
				bResult = false;
			}
			else
			{
				CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0 + 1, i1), cycle, boneMask);
			}
		}
		else if (s1 > 0.999)
		{
			CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0 + 1, i1 + 1), cycle, boneMask);
		}
		else
		{
			CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0 + 1, i1), cycle, boneMask);
			CalcAnimation(this, pos2, q2, seqdesc, sequence, seqdesc.anim(i0 + 1, i1 + 1), cycle, boneMask);
			BlendBones(this, q, pos, seqdesc, sequence, q2, pos2, s1, boneMask);
		}
	}
	else
	{
		if (s1 < 0.001)
		{
			if (PoseIsAllZeros(this, sequence, seqdesc, i0 + 1, i1))
			{
				CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0, i1), cycle, boneMask);
				ScaleBones(this, q, pos, sequence, 1.0 - s0, boneMask);
			}
			else if (PoseIsAllZeros(this, sequence, seqdesc, i0, i1))
			{
				CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0 + 1, i1), cycle, boneMask);
				ScaleBones(this, q, pos, sequence, s0, boneMask);
			}
			else
			{
				CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0, i1), cycle, boneMask);
				CalcAnimation(this, pos2, q2, seqdesc, sequence, seqdesc.anim(i0 + 1, i1), cycle, boneMask);

				BlendBones(this, q, pos, seqdesc, sequence, q2, pos2, s0, boneMask);
			}
		}
		else if (s1 > 0.999)
		{
			CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0, i1 + 1), cycle, boneMask);
			CalcAnimation(this, pos2, q2, seqdesc, sequence, seqdesc.anim(i0 + 1, i1 + 1), cycle, boneMask);
			BlendBones(this, q, pos, seqdesc, sequence, q2, pos2, s0, boneMask);
		}
		else if (!anim_3wayblend.GetBool())
		{
			CalcAnimation(this, pos, q, seqdesc, sequence, seqdesc.anim(i0, i1), cycle, boneMask);
			CalcAnimation(this, pos2, q2, seqdesc, sequence, seqdesc.anim(i0 + 1, i1), cycle, boneMask);
			BlendBones(this, q, pos, seqdesc, sequence, q2, pos2, s0, boneMask);

			CalcAnimation(this, pos2, q2, seqdesc, sequence, seqdesc.anim(i0, i1 + 1), cycle, boneMask);
			CalcAnimation(this, pos3, q3, seqdesc, sequence, seqdesc.anim(i0 + 1, i1 + 1), cycle, boneMask);
			BlendBones(this, q2, pos2, seqdesc, sequence, q3, pos3, s0, boneMask);

			BlendBones(this, q, pos, seqdesc, sequence, q2, pos2, s1, boneMask);
		}
		else
		{
			int		iAnimIndices[3];
			float	weight[3];

			Calc3WayBlendIndices(i0, i1, s0, s1, seqdesc, iAnimIndices, weight);

			/*
			char buf[256];
			sprintf( buf, "%d %6.2f  %d %6.2f : %6.2f %6.2f %6.2f\n", i0, s0, i1, s1, weight[0], weight[1], weight[2] );
			OutputDebugString( buf );
			*/

			if (weight[1] < 0.001)
			{
				// on diagonal
				CalcAnimation(this, pos, q, seqdesc, sequence, iAnimIndices[0], cycle, boneMask);
				CalcAnimation(this, pos2, q2, seqdesc, sequence, iAnimIndices[2], cycle, boneMask);
				BlendBones(this, q, pos, seqdesc, sequence, q2, pos2, weight[2] / (weight[0] + weight[2]), boneMask);
			}
			else
			{
				CalcAnimation(this, pos, q, seqdesc, sequence, iAnimIndices[0], cycle, boneMask);
				CalcAnimation(this, pos2, q2, seqdesc, sequence, iAnimIndices[1], cycle, boneMask);
				BlendBones(this, q, pos, seqdesc, sequence, q2, pos2, weight[1] / (weight[0] + weight[1]), boneMask);

				CalcAnimation(this, pos3, q3, seqdesc, sequence, iAnimIndices[2], cycle, boneMask);
				BlendBones(this, q, pos, seqdesc, sequence, q3, pos3, weight[2], boneMask);
			}
		}
	}

	g_VectorPool.Free(pos2);
	g_QaternionPool.Free(q2);
	g_VectorPool.Free(pos3);
	g_QaternionPool.Free(q3);

	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: calculate a pose for a single sequence
//-----------------------------------------------------------------------------
void CStudioHdr::InitPose(
	Vector pos[],
	Quaternion q[],
	int boneMask
) const
{
	if (!this->pLinearBones())
	{
		for (int i = 0; i < this->numbones(); i++)
		{
			if (this->boneFlags(i) & boneMask)
			{
				mstudiobone_t* pbone = this->pBone(i);
				pos[i] = pbone->pos;
				q[i] = pbone->quat;
			}
		}
	}
	else
	{
		mstudiolinearbone_t* pLinearBones = this->pLinearBones();
		for (int i = 0; i < this->numbones(); i++)
		{
			if (this->boneFlags(i) & boneMask)
			{
				pos[i] = pLinearBones->pos(i);
				q[i] = pLinearBones->quat(i);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

float Studio_IKRuleWeight(mstudioikrule_t& ikRule, const mstudioanimdesc_t* panim, float flCycle, int& iFrame, float& fraq)
{
	if (ikRule.end > 1.0f && flCycle < ikRule.start)
	{
		flCycle = flCycle + 1.0f;
	}

	float value = 0.0f;
	fraq = (panim->numframes - 1) * (flCycle - ikRule.start) + ikRule.iStart;
	iFrame = (int)fraq;
	fraq = fraq - iFrame;

	if (flCycle < ikRule.start)
	{
		iFrame = ikRule.iStart;
		fraq = 0.0f;
		return 0.0f;
	}
	else if (flCycle < ikRule.peak)
	{
		value = (flCycle - ikRule.start) / (ikRule.peak - ikRule.start);
	}
	else if (flCycle < ikRule.tail)
	{
		return 1.0f;
	}
	else if (flCycle < ikRule.end)
	{
		value = 1.0f - ((flCycle - ikRule.tail) / (ikRule.end - ikRule.tail));
	}
	else
	{
		fraq = (panim->numframes - 1) * (ikRule.end - ikRule.start) + ikRule.iStart;
		iFrame = (int)fraq;
		fraq = fraq - iFrame;
	}
	return SimpleSpline(value);
}


float Studio_IKRuleWeight(ikcontextikrule_t& ikRule, float flCycle)
{
	if (ikRule.end > 1.0f && flCycle < ikRule.start)
	{
		flCycle = flCycle + 1.0f;
	}

	float value = 0.0f;
	if (flCycle < ikRule.start)
	{
		return 0.0f;
	}
	else if (flCycle < ikRule.peak)
	{
		value = (flCycle - ikRule.start) / (ikRule.peak - ikRule.start);
	}
	else if (flCycle < ikRule.tail)
	{
		return 1.0f;
	}
	else if (flCycle < ikRule.end)
	{
		value = 1.0f - ((flCycle - ikRule.tail) / (ikRule.end - ikRule.tail));
	}
	return 3.0f * value * value - 2.0f * value * value * value;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


bool CStudioHdr::Studio_IKAnimationError( mstudioikrule_t* pRule, const mstudioanimdesc_t* panim, float flCycle, Vector& pos, Quaternion& q, float& flWeight) const
{
	float fraq;
	int iFrame;

	flWeight = Studio_IKRuleWeight(*pRule, panim, flCycle, iFrame, fraq);
	Assert(fraq >= 0.0 && fraq < 1.0);
	Assert(flWeight >= 0.0f && flWeight <= 1.0f);

	// This shouldn't be necessary, but the Assert should help us catch whoever is screwing this up
	flWeight = clamp(flWeight, 0.0f, 1.0f);

	if (pRule->type != IK_GROUND && flWeight < 0.0001)
		return false;

	mstudioikerror_t* pError = pRule->pError(iFrame);
	if (pError != NULL)
	{
		if (fraq < 0.001)
		{
			q = pError[0].q;
			pos = pError[0].pos;
		}
		else
		{
			QuaternionBlend(pError[0].q, pError[1].q, fraq, q);
			pos = pError[0].pos * (1.0f - fraq) + pError[1].pos * fraq;
		}
		return true;
	}

	mstudiocompressedikerror_t* pCompressed = pRule->pCompressedError();
	if (pCompressed != NULL)
	{
		CalcDecompressedAnimation(pCompressed, iFrame - pRule->iStart, fraq, pos, q);
		return true;
	}
	// no data, disable IK rule
	Assert(0);
	flWeight = 0.0f;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

float Studio_IKTail(ikcontextikrule_t& ikRule, float flCycle)
{
	if (ikRule.end > 1.0f && flCycle < ikRule.start)
	{
		flCycle = flCycle + 1.0f;
	}

	if (flCycle <= ikRule.tail)
	{
		return 0.0f;
	}
	else if (flCycle < ikRule.end)
	{
		return ((flCycle - ikRule.tail) / (ikRule.end - ikRule.tail));
	}
	return 0.0;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

bool Studio_IKShouldLatch(ikcontextikrule_t& ikRule, float flCycle)
{
	if (ikRule.end > 1.0f && flCycle < ikRule.start)
	{
		flCycle = flCycle + 1.0f;
	}

	if (flCycle < ikRule.peak)
	{
		return false;
	}
	else if (flCycle < ikRule.end)
	{
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: For a specific sequence:rule, find where it starts, stops, and what 
//			the estimated offset from the connection point is.
//			return true if the rule is within bounds.
//-----------------------------------------------------------------------------

bool CStudioHdr::Studio_IKSequenceError( mstudioseqdesc_t& seqdesc, int iSequence, float flCycle, int iRule, const float poseParameter[], mstudioanimdesc_t* panim[4], float weight[4], ikcontextikrule_t& ikRule) const
{
	int i;

	memset(&ikRule, 0, sizeof(ikRule));
	ikRule.start = ikRule.peak = ikRule.tail = ikRule.end = 0;


	mstudioikrule_t* prevRule = NULL;

	// find overall influence
	for (i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			if (iRule >= panim[i]->numikrules || panim[i]->numikrules != panim[0]->numikrules)
			{
				Assert(0);
				return false;
			}

			mstudioikrule_t* pRule = panim[i]->pIKRule(this, iRule);
			if (pRule == NULL)
				return false;

			float dt = 0.0;
			if (prevRule != NULL)
			{
				if (pRule->start - prevRule->start > 0.5)
				{
					dt = -1.0;
				}
				else if (pRule->start - prevRule->start < -0.5)
				{
					dt = 1.0;
				}
			}
			else
			{
				prevRule = pRule;
			}

			ikRule.start += (pRule->start + dt) * weight[i];
			ikRule.peak += (pRule->peak + dt) * weight[i];
			ikRule.tail += (pRule->tail + dt) * weight[i];
			ikRule.end += (pRule->end + dt) * weight[i];
		}
	}
	if (ikRule.start > 1.0)
	{
		ikRule.start -= 1.0;
		ikRule.peak -= 1.0;
		ikRule.tail -= 1.0;
		ikRule.end -= 1.0;
	}
	else if (ikRule.start < 0.0)
	{
		ikRule.start += 1.0;
		ikRule.peak += 1.0;
		ikRule.tail += 1.0;
		ikRule.end += 1.0;
	}

	ikRule.flWeight = Studio_IKRuleWeight(ikRule, flCycle);
	if (ikRule.flWeight <= 0.001f)
	{
		// go ahead and allow IK_GROUND rules a virtual looping section
		if (panim[0]->pIKRule(this, iRule) == NULL)
			return false;
		if ((panim[0]->flags & STUDIO_LOOPING) && panim[0]->pIKRule(this, iRule)->type == IK_GROUND && ikRule.end - ikRule.start > 0.75)
		{
			ikRule.flWeight = 0.001;
			flCycle = ikRule.end - 0.001;
		}
		else
		{
			return false;
		}
	}

	Assert(ikRule.flWeight > 0.0f);

	ikRule.pos.Init();
	ikRule.q.Init();

	// find target error
	float total = 0.0f;
	for (i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			Vector pos1;
			Quaternion q1;
			float w;

			mstudioikrule_t* pRule = panim[i]->pIKRule(this, iRule);
			if (pRule == NULL)
				return false;

			ikRule.chain = pRule->chain;	// FIXME: this is anim local
			ikRule.bone = pRule->bone;		// FIXME: this is anim local
			ikRule.type = pRule->type;
			ikRule.slot = pRule->slot;

			ikRule.height += pRule->height * weight[i];
			ikRule.floor += pRule->floor * weight[i];
			ikRule.radius += pRule->radius * weight[i];
			ikRule.drop += pRule->drop * weight[i];
			ikRule.top += pRule->top * weight[i];

			// keep track of tail condition
			ikRule.release += Studio_IKTail(ikRule, flCycle) * weight[i];

			// only check rules with error values
			switch (ikRule.type)
			{
			case IK_SELF:
			case IK_WORLD:
			case IK_GROUND:
			case IK_ATTACHMENT:
			{
				int bResult = this->Studio_IKAnimationError( pRule, panim[i], flCycle, pos1, q1, w);

				if (bResult)
				{
					ikRule.pos = ikRule.pos + pos1 * weight[i];
					QuaternionAccumulate(ikRule.q, weight[i], q1, ikRule.q);
					total += weight[i];
				}
			}
			break;
			default:
				total += weight[i];
				break;
			}

			ikRule.latched = Studio_IKShouldLatch(ikRule, flCycle) * ikRule.flWeight;

			if (ikRule.type == IK_ATTACHMENT)
			{
				ikRule.szLabel = pRule->pszAttachment();
			}
		}
	}

	if (total <= 0.0001f)
	{
		return false;
	}

	if (total < 0.999f)
	{
		VectorScale(ikRule.pos, 1.0f / total, ikRule.pos);
		QuaternionScale(ikRule.q, 1.0f / total, ikRule.q);
	}

	if (ikRule.type == IK_SELF && ikRule.bone != -1)
	{
		// FIXME: this is anim local, not seq local!
		ikRule.bone = this->RemapSeqBone(iSequence, ikRule.bone);
		if (ikRule.bone == -1)
			return false;
	}

	QuaternionNormalize(ikRule.q);
	return true;
}