//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ACTIVITYLIST_H
#define ACTIVITYLIST_H
#ifdef _WIN32
#pragma once
#endif

#include <KeyValues.h>

//typedef struct activityentry_s activityentry_t;

class CActivityRemap
{
public:

	CActivityRemap()
	{
		pExtraBlock = NULL;
	}

	void SetExtraKeyValueBlock ( KeyValues *pKVBlock )
	{
		pExtraBlock = pKVBlock;
	}

	KeyValues *GetExtraKeyValueBlock ( void ) { return pExtraBlock; }

	Activity 		activity;
	Activity		mappedActivity;

private:

	KeyValues		*pExtraBlock;
};


class CActivityRemapCache
{
public:

	CActivityRemapCache() = default;

	CActivityRemapCache( const CActivityRemapCache& src )
	{
		int c = src.m_cachedActivityRemaps.Count();
		for ( int i = 0; i < c; i++ )
		{
			m_cachedActivityRemaps.AddToTail( src.m_cachedActivityRemaps[ i ] );
		}
	}

	CActivityRemapCache& operator = ( const CActivityRemapCache& src )
	{
		if ( this == &src )
			return *this;

		int c = src.m_cachedActivityRemaps.Count();
		for ( int i = 0; i < c; i++ )
		{
			m_cachedActivityRemaps.AddToTail( src.m_cachedActivityRemaps[ i ] );
		}

		return *this;
	}

	CUtlVector< CActivityRemap > m_cachedActivityRemaps;
};

#ifdef GAME_DLL
void UTIL_LoadActivityRemapFile( const char *filename, const char *section, CUtlVector <CActivityRemap> &entries );
void UTIL_UnLoadActivityRemapFile();
#endif // GAME_DLL

// This macro guarantees that the names of each activity and the constant used to
// reference it in the code are identical.
#define REGISTER_SHARED_ACTIVITY( _n ) mdlcache->ActivityList_RegisterSharedActivity(#_n, _n);
#ifdef GAME_DLL
#define REGISTER_PRIVATE_ACTIVITY( _n ) _n = (Activity)mdlcache->ActivityList_RegisterPrivateActivity( #_n );
#endif // GAME_DLL

// Implemented in shared code
extern void ActivityList_RegisterSharedActivities( void );

class ISaveRestoreOps;
extern ISaveRestoreOps* ActivityDataOps();

#endif // ACTIVITYLIST_H
