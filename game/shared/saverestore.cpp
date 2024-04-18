//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Helper classes and functions for the save/restore system.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include <limits.h>
#include "isaverestore.h"
#include "saverestore.h"
#include <stdarg.h>
#include "shake.h"
#include "decals.h"
#include "gamerules.h"
#include "bspfile.h"
#include "mathlib/mathlib.h"
#include "engine/IEngineSound.h"
#include "saverestoretypes.h"
#include "saverestore_utlvector.h"
#include "model_types.h"
#include "igamesystem.h"
#include "interval.h"
#include "vphysics/object_hash.h"
#include "datacache/imdlcache.h"
#include "tier0/vprof.h"

#if !defined( CLIENT_DLL )

#include "globalstate.h"
#include "entitylist.h"
#include "gameinterface.h"
#else

#include "gamestringpool.h"
#include "cdll_client_int.h"
#endif

// HACKHACK: Builds a global list of entities that were restored from all levels
#if !defined( CLIENT_DLL )
void AddRestoredEntity( CBaseEntity *pEntity );
#else
void AddRestoredEntity( C_BaseEntity *pEntity );
#endif


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define MAX_ENTITYARRAY 1024
#define ZERO_TIME ((FLT_MAX*-0.5))
// A bit arbitrary, but unlikely to collide with any saved games...
#define TICK_NEVER_THINK_ENCODE	( INT_MAX - 3 )

ASSERT_INVARIANT( sizeof(EHandlePlaceholder_t) == sizeof(EHANDLE) );

//-----------------------------------------------------------------------------

static int gSizes[FIELD_TYPECOUNT] = 
{
	FIELD_SIZE( FIELD_VOID ),
	FIELD_SIZE( FIELD_FLOAT ),
	FIELD_SIZE( FIELD_STRING ),
	FIELD_SIZE( FIELD_VECTOR ),
	FIELD_SIZE( FIELD_QUATERNION ),
	FIELD_SIZE( FIELD_INTEGER ),
	FIELD_SIZE( FIELD_BOOLEAN ),
	FIELD_SIZE( FIELD_SHORT ),
	FIELD_SIZE( FIELD_CHARACTER ),
	FIELD_SIZE( FIELD_COLOR32 ),
	FIELD_SIZE( FIELD_EMBEDDED ),
	FIELD_SIZE( FIELD_CUSTOM ),
	
	FIELD_SIZE( FIELD_CLASSPTR ),
	FIELD_SIZE( FIELD_EHANDLE ),
	FIELD_SIZE( FIELD_EDICT ),

	FIELD_SIZE( FIELD_POSITION_VECTOR ),
	FIELD_SIZE( FIELD_TIME ),
	FIELD_SIZE( FIELD_TICK ),
	FIELD_SIZE( FIELD_MODELNAME ),
	FIELD_SIZE( FIELD_SOUNDNAME ),

	FIELD_SIZE( FIELD_INPUT ),
	FIELD_SIZE( FIELD_FUNCTION ),
	FIELD_SIZE( FIELD_VMATRIX ),
	FIELD_SIZE( FIELD_VMATRIX_WORLDSPACE ),
	FIELD_SIZE( FIELD_MATRIX3X4_WORLDSPACE ),
	FIELD_SIZE( FIELD_INTERVAL ),
	FIELD_SIZE( FIELD_MODELINDEX ),
	FIELD_SIZE( FIELD_MATERIALINDEX ),

	FIELD_SIZE( FIELD_VECTOR2D ),
	FIELD_SIZE( FIELD_INTEGER64 ),
	FIELD_SIZE( FIELD_POINTER ),
};


// helpers to offset worldspace matrices
static void VMatrixOffset( VMatrix &dest, const VMatrix &matrixIn, const Vector &offset )
{
	dest = matrixIn;
	dest.PostTranslate( offset );
}

static void Matrix3x4Offset( matrix3x4_t& dest, const matrix3x4_t& matrixIn, const Vector &offset )
{
	MatrixCopy( matrixIn, dest );
	Vector out;
	MatrixGetColumn( matrixIn, 3, out );
	out += offset;
	MatrixSetColumn( out, 3, dest );
}

// This does the necessary casting / extract to grab a pointer to a member function as a void *
// UNDONE: Cast to BASEPTR or something else here?
//#define EXTRACT_INPUTFUNC_FUNCTIONPTR(x)		(*(inputfunc_t **)(&(x)))

//-----------------------------------------------------------------------------
// Purpose: Search this datamap for the name of this member function
//			This is used to save/restore function pointers (convert pointer to text)
// Input  : *function - pointer to member function
// Output : const char * - function name
//-----------------------------------------------------------------------------
const char *UTIL_FunctionToName( datamap_t *pMap, inputfunc_t function )
{
	while ( pMap )
	{
		for ( int i = 0; i < pMap->dataNumFields; i++ )
		{
			if ( pMap->dataDesc[i].flags & FTYPEDESC_FUNCTIONTABLE )
			{
#ifdef WIN32
				Assert( sizeof(pMap->dataDesc[i].inputFunc) == sizeof(void *) );
#elif defined(POSIX)
				Assert( sizeof(pMap->dataDesc[i].inputFunc) == 8 );
#else
#error
#endif
				inputfunc_t pTest = pMap->dataDesc[i].inputFunc;

				if ( pTest == function )
					return pMap->dataDesc[i].fieldName;
			}
		}
		pMap = pMap->baseMap;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Search the datamap for a function named pName
//			This is used to save/restore function pointers (convert text back to pointer)
// Input  : *pName - name of the member function
//-----------------------------------------------------------------------------
inputfunc_t UTIL_FunctionFromName( datamap_t *pMap, const char *pName )
{
	while ( pMap )
	{
		for ( int i = 0; i < pMap->dataNumFields; i++ )
		{
#ifdef WIN32
			Assert( sizeof(pMap->dataDesc[i].inputFunc) == sizeof(void *) );
#elif defined(POSIX)
			Assert( sizeof(pMap->dataDesc[i].inputFunc) == 8 );
#else
#error
#endif

			if ( pMap->dataDesc[i].flags & FTYPEDESC_FUNCTIONTABLE )
			{
				if ( FStrEq( pName, pMap->dataDesc[i].fieldName ) )
				{
					return pMap->dataDesc[i].inputFunc;
				}
			}
		}
		pMap = pMap->baseMap;
	}

	Msg( "Failed to find function %s\n", pName );

	return NULL;
}

//-----------------------------------------------------------------------------
//
// CSave
//
//-----------------------------------------------------------------------------

CSave::CSave( CSaveRestoreData *pdata )
 :	m_pData(pdata),
	m_pGameInfo( pdata ),
	m_bAsync( pdata->bAsync )
{
	m_BlockStartStack.EnsureCapacity( 32 );

	// Logging.
	m_hLogFile = NULL;
}

#ifdef GAME_DLL
CSaveServer::CSaveServer(CSaveRestoreData* pdata) 
:CSave(pdata)
{

}
#endif // GAME_DLL

#ifdef CLIENT_DLL
CSaveClient::CSaveClient(CSaveRestoreData* pdata)
:CSave(pdata)
{

}
#endif // CLIENT_DLL


//-------------------------------------

inline int CSave::DataEmpty( const char *pdata, int size )
{
	static int void_data = 0;
	if ( size != 4 )
	{
		const char *pLimit = pdata + size;
		while ( pdata < pLimit )
		{
			if ( *pdata++ )
				return 0;
		}
		return 1;
	}

	return memcmp(pdata, &void_data, sizeof(int)) == 0;
}

//-----------------------------------------------------------------------------
// Purpose: Start logging save data.
//-----------------------------------------------------------------------------
void CSave::StartLogging( const char *pszLogName )
{
	m_hLogFile = filesystem->Open( pszLogName, "w" );
}

//-----------------------------------------------------------------------------
// Purpose: Stop logging save data.
//-----------------------------------------------------------------------------
void CSave::EndLogging( void )
{
	if ( m_hLogFile )
	{
		filesystem->Close( m_hLogFile );
	}
	m_hLogFile = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Check to see if we are logging data.
//-----------------------------------------------------------------------------
bool CSave::IsLogging( void )
{
	return ( m_hLogFile != NULL );
}

//-----------------------------------------------------------------------------
// Purpose: Log data.
//-----------------------------------------------------------------------------
void CSave::Log( const char *pName, fieldtype_t fieldType, void *value, int count )
{
	// Check to see if we are logging.
	if ( !IsLogging() )
		return;

	static char szBuf[1024];
	static char szTempBuf[256];

	// Save the name.
	Q_snprintf( szBuf, sizeof( szBuf ), "%s ", pName );

	for ( int iCount = 0; iCount < count; ++iCount )
	{
		switch ( fieldType )
		{
		case FIELD_SHORT:
			{
				short *pValue = ( short* )( value );
				short nValue = pValue[iCount];
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "%d", nValue );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
				break;
			}
		case FIELD_FLOAT:
			{
				float *pValue = ( float* )( value );
				float flValue = pValue[iCount];
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "%f", flValue );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
				break;
			}
		case FIELD_BOOLEAN:
			{
				bool *pValue = ( bool* )( value );
				bool bValue = pValue[iCount];
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "%d", ( int )( bValue ) );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
				break;
			}
		case FIELD_INTEGER:
			{
				int *pValue = ( int* )( value );
				int nValue = pValue[iCount];
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "%d", nValue );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
				break;
			}
		case FIELD_STRING:
			{
				string_t *pValue = ( string_t* )( value );
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "%s", ( char* )STRING( *pValue ) );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
				break;					
			}
		case FIELD_VECTOR:
			{
				Vector *pValue = ( Vector* )( value );
				Vector vecValue = pValue[iCount];
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "(%f %f %f)", vecValue.x, vecValue.y, vecValue.z );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
				break;
			}
		case FIELD_QUATERNION:
			{
				Quaternion *pValue = ( Quaternion* )( value );
				Quaternion q = pValue[iCount];
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "(%f %f %f %f)", q[0], q[1], q[2], q[3] );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
				break;
			}
		case FIELD_CHARACTER:
			{
				char *pValue = ( char* )( value );
				char chValue = pValue[iCount];
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "%c", chValue );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
			}
		case FIELD_COLOR32:
			{
				byte *pValue = ( byte* )( value );
				byte *pColor = &pValue[iCount*4];
				Q_snprintf( szTempBuf, sizeof( szTempBuf ), "(%d %d %d %d)", ( int )pColor[0], ( int )pColor[1], ( int )pColor[2], ( int )pColor[3] );
				Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
			}
		case FIELD_EMBEDDED:
		case FIELD_CUSTOM:
		default:
			{
				break;
			}
		}

		// Add space data.
		if ( ( iCount + 1 ) != count )
		{
			Q_strncpy( szTempBuf, " ", sizeof( szTempBuf ) );
			Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
		}
		else
		{
			Q_strncpy( szTempBuf, "\n", sizeof( szTempBuf ) );
			Q_strncat( szBuf, szTempBuf, sizeof( szTempBuf ), COPY_ALL_CHARACTERS );
		}
	}

	int nLength = strlen( szBuf ) + 1;
	filesystem->Write( szBuf, nLength, m_hLogFile );
}

//-------------------------------------

bool CSave::IsAsync()
{
	return m_bAsync;
}

//-------------------------------------

int CSave::GetWritePos() const
{ 
	return m_pData->GetCurPos(); 
}

//-------------------------------------

void CSave::SetWritePos(int pos)
{
	m_pData->Seek(pos); 
}

//-------------------------------------

void CSave::WriteShort( const short *value, int count )
{
	BufferData( (const char *)value, sizeof(short) * count );
}

//-------------------------------------

void CSave::WriteInt( const int *value, int count )
{
	BufferData( (const char *)value, sizeof(int) * count );
}

//-------------------------------------

void CSave::WriteBool( const bool *value, int count )
{
	COMPILE_TIME_ASSERT( sizeof(bool) == sizeof(char) );
	BufferData( (const char *)value, sizeof(bool) * count );
}

//-------------------------------------

void CSave::WriteFloat( const float *value, int count )
{
	BufferData( (const char *)value, sizeof(float) * count );
}

//-------------------------------------

void CSave::WriteData( const char *pdata , int size )
{
	BufferData( pdata, size );
}

//-------------------------------------

void CSave::WriteString( const char *pstring )
{
	BufferData( pstring, strlen(pstring) + 1 );
}

//-------------------------------------

void CSave::WriteString( const string_t *stringId, int count )
{
	for ( int i = 0; i < count; i++ )
	{
		const char *pString = STRING(stringId[i]);
		BufferData( pString, strlen(pString)+1 );
	}
}

//-------------------------------------

void CSave::WriteVector( const Vector &value )
{
	BufferData( (const char *)&value, sizeof(Vector) );
}

//-------------------------------------

void CSave::WriteVector( const Vector *value, int count )
{
	BufferData( (const char *)value, sizeof(Vector) * count );
}

void CSave::WriteQuaternion( const Quaternion &value )
{
	BufferData( (const char *)&value, sizeof(Quaternion) );
}

//-------------------------------------

void CSave::WriteQuaternion( const Quaternion *value, int count )
{
	BufferData( (const char *)value, sizeof(Quaternion) * count );
}


//-------------------------------------

void CSave::WriteData( const char *pname, int size, const char *pdata )
{
	BufferField( pname, size, pdata );
}

//-------------------------------------

void CSave::WriteShort( const char *pname, const short *data, int count )
{
	BufferField( pname, sizeof(short) * count, (const char *)data );
}

//-------------------------------------

void CSave::WriteInt( const char *pname, const int *data, int count )
{
	BufferField( pname, sizeof(int) * count, (const char *)data );
}

//-------------------------------------

void CSave::WriteBool( const char *pname, const bool *data, int count )
{
	COMPILE_TIME_ASSERT( sizeof(bool) == sizeof(char) );
	BufferField( pname, sizeof(bool) * count, (const char *)data );
}

//-------------------------------------

void CSave::WriteFloat( const char *pname, const float *data, int count )
{
	BufferField( pname, sizeof(float) * count, (const char *)data );
}

//-------------------------------------

void CSave::WriteString( const char *pname, const char *pdata )
{
	BufferField( pname, strlen(pdata) + 1, pdata );
}

//-------------------------------------

void CSave::WriteString( const char *pname, const string_t *stringId, int count )
{
	int i, size;

	size = 0;
	for ( i = 0; i < count; i++ )
		size += strlen( STRING( stringId[i] ) ) + 1;

	WriteHeader( pname, size );
	WriteString( stringId, count );
}

//-------------------------------------

void CSave::WriteVector( const char *pname, const Vector &value )
{
	WriteVector( pname, &value, 1 );
}

//-------------------------------------

void CSave::WriteVector( const char *pname, const Vector *value, int count )
{
	WriteHeader( pname, sizeof(Vector) * count );
	BufferData( (const char *)value, sizeof(Vector) * count );
}

void CSave::WriteQuaternion( const char *pname, const Quaternion &value )
{
	WriteQuaternion( pname, &value, 1 );
}

//-------------------------------------

void CSave::WriteQuaternion( const char *pname, const Quaternion *value, int count )
{
	WriteHeader( pname, sizeof(Quaternion) * count );
	BufferData( (const char *)value, sizeof(Quaternion) * count );
}


//-------------------------------------

void CSave::WriteVMatrix( const VMatrix *value, int count )
{
	BufferData( (const char *)value, sizeof(VMatrix) * count );
}

//-------------------------------------

void CSave::WriteVMatrix( const char *pname, const VMatrix *value, int count )
{
	WriteHeader( pname, sizeof(VMatrix) * count );
	BufferData( (const char *)value, sizeof(VMatrix) * count );
}

//-------------------------------------

void CSave::WriteVMatrixWorldspace( const VMatrix *value, int count )
{
	for ( int i = 0; i < count; i++ )
	{
		VMatrix tmp;
		VMatrixOffset( tmp, value[i], -m_pGameInfo->GetLandmark() );
		BufferData( (const char *)&tmp, sizeof(VMatrix) );
	}
}

//-------------------------------------

void CSave::WriteVMatrixWorldspace( const char *pname, const VMatrix *value, int count )
{
	WriteHeader( pname, sizeof(VMatrix) * count );
	WriteVMatrixWorldspace( value, count );
}

void CSave::WriteMatrix3x4Worldspace( const matrix3x4_t *value, int count )
{
	Vector offset = -m_pGameInfo->GetLandmark();
	for ( int i = 0; i < count; i++ )
	{
		matrix3x4_t tmp;
		Matrix3x4Offset( tmp, value[i], offset );
		BufferData( (const char *)value, sizeof(matrix3x4_t) );
	}
}

//-------------------------------------

void CSave::WriteMatrix3x4Worldspace( const char *pname, const matrix3x4_t *value, int count )
{
	WriteHeader( pname, sizeof(matrix3x4_t) * count );
	WriteMatrix3x4Worldspace( value, count );
}

void CSave::WriteInterval( const char *pname, const interval_t *value, int count )
{
	WriteHeader( pname, sizeof( interval_t ) * count );
	WriteInterval( value, count );
}

void CSave::WriteInterval( const interval_t *value, int count )
{
	BufferData( (const char *)value, count * sizeof( interval_t ) );
}

//-------------------------------------

bool CSave::ShouldSaveField( const void *pData, typedescription_t *pField )
{
	if ( !(pField->flags & FTYPEDESC_SAVE) || pField->fieldType == FIELD_VOID )
		return false;

	switch ( pField->fieldType )
	{
	case FIELD_EMBEDDED:
		{
			if ( pField->flags & FTYPEDESC_PTR )
			{
				AssertMsg( pField->fieldSize == 1, "Arrays of embedded pointer types presently unsupported by save/restore" );
				if ( pField->fieldSize != 1 )
					return false;
			}

			AssertMsg( pField->td != NULL, "Embedded type appears to have not had type description implemented" );
			if ( pField->td == NULL )
				return false;

			if ( (pField->flags & FTYPEDESC_PTR) && !*((void **)pData) )
				return false;

			// @TODO: need real logic for handling embedded types with base classes
			if ( pField->td->baseMap )
			{
				return true;
			}

			int nFieldCount = pField->fieldSize;
			char *pTestData = (char *)( ( !(pField->flags & FTYPEDESC_PTR) ) ? pData : *((void **)pData) );
			while ( --nFieldCount >= 0 )
			{
				typedescription_t *pTestField = pField->td->dataDesc;
				typedescription_t *pLimit	  = pField->td->dataDesc + pField->td->dataNumFields;
			
				for ( ; pTestField < pLimit; ++pTestField )
				{
					if ( ShouldSaveField( pTestData + pTestField->fieldOffset[ TD_OFFSET_NORMAL ], pTestField ) )
						return true;
				}

				pTestData += pField->fieldSizeInBytes;
			}
			return false;
		}

	case FIELD_CUSTOM:
		{
			// ask the data if it's empty
			SaveRestoreFieldInfo_t fieldInfo =
			{
				const_cast<void *>(pData),
				((char *)pData) - pField->fieldOffset[ TD_OFFSET_NORMAL ],
				pField
			};
			if ( pField->pSaveRestoreOps->IsEmpty( fieldInfo ) )
				return false;
		}
		return true;

	case FIELD_EHANDLE:
		{
			if ( (pField->fieldSizeInBytes != pField->fieldSize * gSizes[pField->fieldType]) )
			{
				Warning("WARNING! Field %s is using the wrong FIELD_ type!\nFix this or you'll see a crash.\n", pField->fieldName );
				Assert( 0 );
			}

			int *pEHandle = (int *)pData;
			for ( int i = 0; i < pField->fieldSize; ++i, ++pEHandle )
			{
				if ( (*pEHandle) != INVALID_EHANDLE_INDEX )
					return true;
			}
		}
		return false;

	default:
		{
			if ( (pField->fieldSizeInBytes != pField->fieldSize * gSizes[pField->fieldType]) )
			{
				Warning("WARNING! Field %s is using the wrong FIELD_ type!\nFix this or you'll see a crash.\n", pField->fieldName );
				Assert( 0 );
			}

			// old byte-by-byte null check
			if ( DataEmpty( (const char *)pData, pField->fieldSize * gSizes[pField->fieldType] ) )
				return false;
		}
		return true;
	}
}

//-------------------------------------
// Purpose:	Writes all the fields that are client neutral. In the event of 
//			a librarization of save/restore, these would reside in the library
//

bool CSave::WriteBasicField( const char *pname, void *pData, datamap_t *pRootMap, typedescription_t *pField )
{
	switch( pField->fieldType )
	{
		case FIELD_FLOAT:
			WriteFloat( pField->fieldName, (float *)pData, pField->fieldSize );
			break;

		case FIELD_STRING:
			WriteString( pField->fieldName, (string_t *)pData, pField->fieldSize );
			break;

		case FIELD_VECTOR:
			WriteVector( pField->fieldName, (Vector *)pData, pField->fieldSize );
			break;

		case FIELD_QUATERNION:
			WriteQuaternion( pField->fieldName, (Quaternion *)pData, pField->fieldSize );
			break;

		case FIELD_INTEGER:
			WriteInt( pField->fieldName, (int *)pData, pField->fieldSize );
			break;

		case FIELD_BOOLEAN:
			WriteBool( pField->fieldName, (bool *)pData, pField->fieldSize );
			break;

		case FIELD_SHORT:
			WriteData( pField->fieldName, 2 * pField->fieldSize, ((char *)pData) );
			break;

		case FIELD_CHARACTER:
			WriteData( pField->fieldName, pField->fieldSize, ((char *)pData) );
			break;

		case FIELD_COLOR32:
			WriteData( pField->fieldName, 4*pField->fieldSize, (char *)pData );	
			break;

		case FIELD_EMBEDDED:
		{
			AssertMsg( ( (pField->flags & FTYPEDESC_PTR) == 0 ) || (pField->fieldSize == 1), "Arrays of embedded pointer types presently unsupported by save/restore" );
			Assert( !(pField->flags & FTYPEDESC_PTR) || *((void **)pData) );
			int nFieldCount = pField->fieldSize;
			char *pFieldData = (char *)( ( !(pField->flags & FTYPEDESC_PTR) ) ? pData : *((void **)pData) );

			StartBlock( pField->fieldName );

			while ( --nFieldCount >= 0 )
			{
				WriteAll( pFieldData, pField->td );
				pFieldData += pField->fieldSizeInBytes;
			}

			EndBlock();
			break;
		}

		case FIELD_CUSTOM:
		{
			// Note it is up to the custom type implementor to handle arrays
			StartBlock( pField->fieldName );

			SaveRestoreFieldInfo_t fieldInfo =
			{
				pData,
				((char *)pData) - pField->fieldOffset[ TD_OFFSET_NORMAL ],
				pField
			};
			pField->pSaveRestoreOps->Save( fieldInfo, this );
			
			EndBlock();
			break;
		}

		default:
			Warning( "Bad field type\n" );
			Assert(0);
			return false;
	}

	return true;
}

//-------------------------------------

bool CSave::WriteField( const char *pname, void *pData, datamap_t *pRootMap, typedescription_t *pField )
{
#ifdef _DEBUG
	Log( pname, (fieldtype_t)pField->fieldType, pData, pField->fieldSize );
#endif

	if ( pField->fieldType <= FIELD_CUSTOM )
	{
		return WriteBasicField( pname, pData, pRootMap, pField );
	}
	return WriteGameField( pname, pData, pRootMap, pField );
}

//-------------------------------------

int CSave::WriteFields( const char *pname, const void *pBaseData, datamap_t *pRootMap, typedescription_t *pFields, int fieldCount )
{
	typedescription_t *pTest;
	int iHeaderPos = m_pData->GetCurPos();
	int count = -1;
	WriteInt( pname, &count, 1 );

	count = 0;

#ifdef _X360
	__dcbt( 0, pBaseData );
	__dcbt( 128, pBaseData );
	__dcbt( 256, pBaseData );
	__dcbt( 512, pBaseData );
	void *pDest = m_pData->AccessCurPos();	
	__dcbt( 0, pDest );
	__dcbt( 128, pDest );
	__dcbt( 256, pDest );
	__dcbt( 512, pDest );
#endif

	for ( int i = 0; i < fieldCount; i++ )
	{
		pTest = &pFields[ i ];
		void *pOutputData = ( (char *)pBaseData + pTest->fieldOffset[ TD_OFFSET_NORMAL ] );
			
		if ( !ShouldSaveField( pOutputData, pTest ) )
			continue;

		if ( !WriteField( pname, pOutputData, pRootMap, pTest ) )
			break;
		count++;
	}

	int iCurPos = m_pData->GetCurPos();
	int iRewind = iCurPos - iHeaderPos;
	m_pData->Rewind( iRewind );
	WriteInt( pname, &count, 1 );
	iCurPos = m_pData->GetCurPos();
	m_pData->MoveCurPos( iRewind - ( iCurPos - iHeaderPos ) );

	return 1;
}

//-------------------------------------
// Purpose: Recursively saves all the classes in an object, in reverse order (top down)
// Output : int 0 on failure, 1 on success

int CSave::DoWriteAll( const void *pLeafObject, datamap_t *pLeafMap, datamap_t *pCurMap )
{
	// save base classes first
	if ( pCurMap->baseMap )
	{
		int status = DoWriteAll( pLeafObject, pLeafMap, pCurMap->baseMap );
		if ( !status )
			return status;
	}

	return WriteFields( pCurMap->dataClassName, pLeafObject, pLeafMap, pCurMap->dataDesc, pCurMap->dataNumFields );
}
	
//-------------------------------------

void CSave::StartBlock( const char *pszBlockName )
{
	WriteHeader( pszBlockName, 0 ); // placeholder
	m_BlockStartStack.AddToTail( GetWritePos() );
}

//-------------------------------------

void CSave::StartBlock()
{
	StartBlock( "" );
}

//-------------------------------------

void CSave::EndBlock()
{
	int endPos = GetWritePos();
	int startPos = m_BlockStartStack[ m_BlockStartStack.Count() - 1 ];
	short sizeBlock = endPos - startPos;
	
	m_BlockStartStack.Remove( m_BlockStartStack.Count() - 1 );
	
	// Move to the the location where the size of the block was written & rewrite the size
	SetWritePos( startPos - sizeof(SaveRestoreRecordHeader_t) );
	BufferData( (const char *)&sizeBlock, sizeof(short) );
	
	SetWritePos( endPos );
}
	
//-------------------------------------

void CSave::BufferString( char *pdata, int len )
{
	char c = 0;

	BufferData( pdata, len );		// Write the string
	BufferData( &c, 1 );			// Write a null terminator
}

//-------------------------------------

void CSave::BufferField( const char *pname, int size, const char *pdata )
{
	WriteHeader( pname, size );
	BufferData( pdata, size );
}

//-------------------------------------

void CSave::WriteHeader( const char *pname, int size )
{
	short shortSize = size;
	short hashvalue = m_pData->FindCreateSymbol( pname );
	if ( size > SHRT_MAX || size < 0 )
	{
		Warning( "CSave::WriteHeader() size parameter exceeds 'short'!\n" );
		Assert(0);
	}

	BufferData( (const char *)&shortSize, sizeof(short) );
	BufferData( (const char *)&hashvalue, sizeof(short) );
}

//-------------------------------------

void CSave::BufferData( const char *pdata, int size )
{
	if ( !m_pData )
		return;

	if ( !m_pData->Write( pdata, size ) )
	{
		Warning( "Save/Restore overflow!\n" );
		Assert(0);
	}
}

//---------------------------------------------------------
//
// Game centric save methods.
//
//int	CSave::EntityIndex( const edict_t *pentLookup )
//{
//#if !defined( CLIENT_DLL )
//	if ( pentLookup == NULL )
//		return -1;
//	return EntityIndex( CBaseEntity::Instance(pentLookup) );
//#else
//	Assert( !"CSave::EntityIndex( edict_t * ) not valid on client!" );
//	return -1;
//#endif
//}


//-------------------------------------

int	CSave::EntityIndex( const IHandleEntity *pEntity )
{
	return m_pGameInfo->GetEntityIndex( pEntity );
}

//-------------------------------------

int	CSave::EntityFlagsSet( int entityIndex, int flags )
{
	if ( !m_pGameInfo || entityIndex < 0 )
		return 0;
	if ( entityIndex > m_pGameInfo->NumEntities() )
		return 0;

	m_pGameInfo->GetEntityInfo( entityIndex )->flags |= flags;

	return m_pGameInfo->GetEntityInfo( entityIndex )->flags;
}

//-------------------------------------

void CSave::WriteTime( const char *pname, const float *data, int count )
{
	int i;
	float tmp;

	WriteHeader( pname, sizeof(float) * count );
	for ( i = 0; i < count; i++ )
	{
		// Always encode time as a delta from the current time so it can be re-based if loaded in a new level
		// Times of 0 are never written to the file, so they will be restored as 0, not a relative time
		Assert( data[i] != ZERO_TIME );

		if ( data[i] == 0.0 )
		{
			tmp = ZERO_TIME;
		}
		else if ( data[i] == INVALID_TIME || data[i] == FLT_MAX )
		{
			tmp = data[i];
		}
		else
		{			
			tmp = data[i] - m_pGameInfo->GetBaseTime();
			if ( fabsf( tmp ) < 0.001 ) // never allow a time to become zero due to rebasing
				tmp = 0.001;
		}

		WriteData( (const char *)&tmp, sizeof(float) );
	}
}

//-------------------------------------

void CSave::WriteTime( const float *data, int count )
{
	int i;
	float tmp;

	for ( i = 0; i < count; i++ )
	{
		// Always encode time as a delta from the current time so it can be re-based if loaded in a new level
		// Times of 0 are never written to the file, so they will be restored as 0, not a relative time
		if ( data[i] == 0.0 )
		{
			tmp = ZERO_TIME;
		}
		else if ( data[i] == INVALID_TIME || data[i] == FLT_MAX )
		{
			tmp = data[i];
		}
		else
		{			
			tmp = data[i] - m_pGameInfo->GetBaseTime();
			if ( fabsf( tmp ) < 0.001 ) // never allow a time to become zero due to rebasing
				tmp = 0.001;
		}

		WriteData( (const char *)&tmp, sizeof(float) );
	}
}

void CSave::WriteTick( const char *pname, const int *data, int count )
{
	WriteHeader( pname, sizeof(int) * count );
	WriteTick( data, count );
}

//-------------------------------------

void CSave::WriteTick( const int *data, int count )
{
	int i;
	int tmp;

	int baseTick = TIME_TO_TICKS( m_pGameInfo->GetBaseTime() );

	for ( i = 0; i < count; i++ )
	{
		// Always encode time as a delta from the current time so it can be re-based if loaded in a new level
		// Times of 0 are never written to the file, so they will be restored as 0, not a relative time
		tmp = data[ i ];
		if ( data[ i ] == TICK_NEVER_THINK )
		{
			tmp = TICK_NEVER_THINK_ENCODE;
		}
		else
		{
			// Rebase it...
			tmp -= baseTick;
		}
		WriteData( (const char *)&tmp, sizeof(int) );
	}
}
//-------------------------------------

void CSave::WritePositionVector( const char *pname, const Vector &value )
{
	Vector tmp = value;

	if ( tmp != vec3_invalid )
		tmp -= m_pGameInfo->GetLandmark();

	WriteVector( pname, tmp );
}

//-------------------------------------

void CSave::WritePositionVector( const Vector &value )
{
	Vector tmp = value;

	if ( tmp != vec3_invalid )
		tmp -= m_pGameInfo->GetLandmark();

	WriteVector( tmp );
}

//-------------------------------------

void CSave::WritePositionVector( const char *pname, const Vector *value, int count )
{
	WriteHeader( pname, sizeof(Vector) * count );
	WritePositionVector( value, count );
}

//-------------------------------------

void CSave::WritePositionVector( const Vector *value, int count )
{
	for ( int i = 0; i < count; i++ )
	{
		Vector tmp = value[i];

		if ( tmp != vec3_invalid )
			tmp -= m_pGameInfo->GetLandmark();

		WriteData( (const char *)&tmp.x, sizeof(Vector) );
	}
}

//-------------------------------------

void CSave::WriteFunction( datamap_t *pRootMap, const char *pname, inputfunc_t **data, int count )
{
	AssertMsg( count == 1, "Arrays of functions not presently supported" );
	const char *functionName = UTIL_FunctionToName( pRootMap, *(inputfunc_t*)data );
	if ( !functionName )
	{
		Warning( "Invalid function pointer in entity!\n" );
		Assert(0);
		functionName = "BADFUNCTIONPOINTER";
	}

	BufferField( pname, strlen(functionName) + 1, functionName );
}

//-------------------------------------

void CSave::WriteEntityPtr( const char *pname, IHandleEntity **ppEntity, int count )
{
	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
	int entityArray[MAX_ENTITYARRAY];
	for ( int i = 0; i < count && i < MAX_ENTITYARRAY; i++ )
	{
		entityArray[i] = EntityIndex( ppEntity[i] );
	}
	WriteInt( pname, entityArray, count );
}

//-------------------------------------

void CSave::WriteEntityPtr( IHandleEntity **ppEntity, int count )
{
	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
	int entityArray[MAX_ENTITYARRAY];
	for ( int i = 0; i < count && i < MAX_ENTITYARRAY; i++ )
	{
		entityArray[i] = EntityIndex( ppEntity[i] );
	}
	WriteInt( entityArray, count );
}

//-------------------------------------

//void CSave::WriteEdictPtr( const char *pname, edict_t **ppEdict, int count )
//{
//	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
//	int entityArray[MAX_ENTITYARRAY];
//	for ( int i = 0; i < count && i < MAX_ENTITYARRAY; i++ )
//	{
//		entityArray[i] = EntityIndex( ppEdict[i] );
//	}
//	WriteInt( pname, entityArray, count );
//}

//-------------------------------------

//void CSave::WriteEdictPtr( edict_t **ppEdict, int count )
//{
//	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
//	int entityArray[MAX_ENTITYARRAY];
//	for ( int i = 0; i < count && i < MAX_ENTITYARRAY; i++ )
//	{
//		entityArray[i] = EntityIndex( ppEdict[i] );
//	}
//	WriteInt( entityArray, count );
//}

//-------------------------------------

#ifdef GAME_DLL
void CSaveServer::WriteEHandle( const char *pname, const CBaseHandle *pEHandle, int count )
{
	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
	int entityArray[MAX_ENTITYARRAY];
	for ( int i = 0; i < count && i < MAX_ENTITYARRAY; i++ )
	{
		IHandleEntity* pHandleEntity = gEntList.GetServerEntityFromHandle(const_cast<CBaseHandle*>(pEHandle)[i]);
		entityArray[i] = EntityIndex(pHandleEntity);
	}
	WriteInt( pname, entityArray, count );
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
void CSaveClient::WriteEHandle(const char* pname, const CBaseHandle* pEHandle, int count)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];
	for (int i = 0; i < count && i < MAX_ENTITYARRAY; i++)
	{
		IHandleEntity* pHandleEntity = cl_entitylist->GetClientEntityFromHandle(const_cast<CBaseHandle*>(pEHandle)[i]);
		entityArray[i] = EntityIndex(pHandleEntity);
	}
	WriteInt(pname, entityArray, count);
}
#endif // CLIENT_DLL

//-------------------------------------
#ifdef GAME_DLL
void CSaveServer::WriteEHandle( const CBaseHandle *pEHandle, int count )
{
	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
	int entityArray[MAX_ENTITYARRAY];
	for ( int i = 0; i < count && i < MAX_ENTITYARRAY; i++ )
	{
		IHandleEntity* pHandleEntity = gEntList.GetServerEntityFromHandle(const_cast<CBaseHandle*>(pEHandle)[i]);
		entityArray[i] = EntityIndex(pHandleEntity);
	}
	WriteInt( entityArray, count );
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
void CSaveClient::WriteEHandle(const CBaseHandle* pEHandle, int count)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];
	for (int i = 0; i < count && i < MAX_ENTITYARRAY; i++)
	{
		IHandleEntity* pHandleEntity = cl_entitylist->GetClientEntityFromHandle(const_cast<CBaseHandle*>(pEHandle)[i]);
		entityArray[i] = EntityIndex(pHandleEntity);
	}
	WriteInt(entityArray, count);
}
#endif // CLIENT_DLL

#ifdef GAME_DLL
const model_t* CSaveServer::GetModel(int modelindex) {
	return modelinfo->GetModel(modelindex);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
const model_t* CSaveClient::GetModel(int modelindex) {
	return modelinfo->GetModel(modelindex);
}
#endif // CLIENT_DLL

#ifdef GAME_DLL
const char* CSaveServer::GetModelName(const model_t* model) const {
	return modelinfo->GetModelName(model);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
const char* CSaveClient::GetModelName(const model_t* model) const {
	return modelinfo->GetModelName(model);
}
#endif // CLIENT_DLL

#ifdef GAME_DLL
const char* CSaveServer::GetMaterialNameFromIndex(int nMateralIndex) {
	return g_ServerGameDLL.GetMaterialNameFromIndex(nMateralIndex);
}
#endif

#ifdef CLIENT_DLL
const char* CSaveClient::GetMaterialNameFromIndex(int nMateralIndex) {
	return clientdll->GetMaterialNameFromIndex(nMateralIndex);
}
#endif // CLIENT_DLL

#ifdef GAME_DLL
string_t CSaveServer::AllocPooledString(const char* pszValue) {
	return g_ServerGameDLL.AllocPooledString(pszValue);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
string_t CSaveClient::AllocPooledString(const char* pszValue) {
	return clientdll->AllocPooledString(pszValue);
}
#endif // CLIENT_DLL

//-------------------------------------
// Purpose:	Writes all the fields that are not client neutral. In the event of 
//			a librarization of save/restore, these would not reside in the library

bool CSave::WriteGameField( const char *pname, void *pData, datamap_t *pRootMap, typedescription_t *pField )
{
	switch( pField->fieldType )
	{
		case FIELD_CLASSPTR:
			WriteEntityPtr( pField->fieldName, (IHandleEntity **)pData, pField->fieldSize );
			break;

		case FIELD_EDICT:
			//WriteEdictPtr( pField->fieldName, (edict_t **)pData, pField->fieldSize );
			Error("WriteEdictPtr has been removed!");
			break;

		case FIELD_EHANDLE:
			WriteEHandle( pField->fieldName, (CBaseHandle*)pData, pField->fieldSize );
			break;

		case FIELD_POSITION_VECTOR:
			WritePositionVector( pField->fieldName, (Vector *)pData, pField->fieldSize );
			break;

		case FIELD_TIME:
			WriteTime( pField->fieldName, (float *)pData, pField->fieldSize );
			break;

		case FIELD_TICK:
			WriteTick( pField->fieldName, (int *)pData, pField->fieldSize );
			break;

		case FIELD_MODELINDEX:
			{
				int nModelIndex = *(int*)pData;
				string_t strModelName = NULL_STRING;
				const model_t *pModel = GetModel( nModelIndex );
				if ( pModel )
				{
					strModelName = AllocPooledString( GetModelName( pModel ) );
				}
				WriteString( pField->fieldName, (string_t *)&strModelName, pField->fieldSize );
			}
			break;

		case FIELD_MATERIALINDEX:
			{
				int nMateralIndex = *(int*)pData;
				string_t strMaterialName = NULL_STRING;
				const char* pMaterialName = GetMaterialNameFromIndex(nMateralIndex);
				if ( pMaterialName )
				{
					strMaterialName = MAKE_STRING( pMaterialName );
				}
				WriteString( pField->fieldName, (string_t *)&strMaterialName, pField->fieldSize );
			}
			break;

		case FIELD_MODELNAME:
		case FIELD_SOUNDNAME:
			WriteString( pField->fieldName, (string_t *)pData, pField->fieldSize );
			break;

		// For now, just write the address out, we're not going to change memory while doing this yet!
		case FIELD_FUNCTION:
			WriteFunction( pRootMap, pField->fieldName, (inputfunc_t **)(char *)pData, pField->fieldSize );
			break;

		case FIELD_VMATRIX:
			WriteVMatrix( pField->fieldName, (VMatrix *)pData, pField->fieldSize );
			break;
		case FIELD_VMATRIX_WORLDSPACE:
			WriteVMatrixWorldspace( pField->fieldName, (VMatrix *)pData, pField->fieldSize );
			break;

		case FIELD_MATRIX3X4_WORLDSPACE:
			WriteMatrix3x4Worldspace( pField->fieldName, (const matrix3x4_t *)pData, pField->fieldSize );
			break;

		case FIELD_INTERVAL:
			WriteInterval( pField->fieldName, (interval_t *)pData, pField->fieldSize );
			break;

		case FIELD_POINTER:
			WriteData( pField->fieldName, sizeof(void*)*pField->fieldSize, (char *)pData );
			break;

		default:
			Warning( "Bad field type\n" );
			Assert(0);
			return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
//
// CRestore
//
//-----------------------------------------------------------------------------

CRestore::CRestore( CSaveRestoreData *pdata )
 :	m_pData( pdata ),
	m_pGameInfo( pdata ),
	m_global( 0 ),
	m_precache( true )
{
	m_BlockEndStack.EnsureCapacity( 32 );
}

#ifdef GAME_DLL
CRestoreServer::CRestoreServer(CSaveRestoreData* pdata)
:CRestore(pdata)
{

}
#endif // GAME_DLL

#ifdef CLIENT_DLL
CRestoreClient::CRestoreClient(CSaveRestoreData* pdata)
:CRestore(pdata)
{

}
#endif // CLIENT_DLL

//-------------------------------------

int CRestore::GetReadPos() const
{ 
	return m_pData->GetCurPos(); 
}

//-------------------------------------

void CRestore::SetReadPos( int pos ) 
{ 
	m_pData->Seek(pos); 
}

//-------------------------------------

const char *CRestore::StringFromHeaderSymbol( int symbol )
{
	const char *pszResult = m_pData->StringFromSymbol( symbol );
	return ( pszResult ) ? pszResult : "";
}

//-------------------------------------
// Purpose:	Reads all the fields that are client neutral. In the event of 
//			a librarization of save/restore, these would reside in the library

void CRestore::ReadBasicField( const SaveRestoreRecordHeader_t &header, void *pDest, datamap_t *pRootMap, typedescription_t *pField )
{
	switch( pField->fieldType )
	{
		case FIELD_FLOAT:
		{
			ReadFloat( (float *)pDest, pField->fieldSize, header.size );
			break;
		}
		case FIELD_STRING:
		{
			ReadString( (string_t *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_VECTOR:
		{
			ReadVector( (Vector *)pDest, pField->fieldSize, header.size );
			break;
		}
		
		case FIELD_QUATERNION:
		{
			ReadQuaternion( (Quaternion *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_INTEGER:
		{
			ReadInt( (int *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_BOOLEAN:
		{
			ReadBool( (bool *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_SHORT:
		{
			ReadShort( (short *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_CHARACTER:
		{
			ReadData( (char *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_COLOR32:
		{
			COMPILE_TIME_ASSERT( sizeof(color32) == sizeof(int) );
			ReadInt( (int *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_EMBEDDED:
		{
			AssertMsg( (( pField->flags & FTYPEDESC_PTR ) == 0) || (pField->fieldSize == 1), "Arrays of embedded pointer types presently unsupported by save/restore" );
#ifdef DBGFLAG_ASSERT
			int startPos = GetReadPos();
#endif
			if ( !(pField->flags & FTYPEDESC_PTR) || *((void **)pDest) )
			{
				int nFieldCount = pField->fieldSize;
				char *pFieldData = (char *)( ( !(pField->flags & FTYPEDESC_PTR) ) ? pDest : *((void **)pDest) );
				while ( --nFieldCount >= 0 )
				{
					// No corresponding "block" (see write) as it was used as the header of the field
					ReadAll( pFieldData, pField->td );
					pFieldData += pField->fieldSizeInBytes;
				}
				Assert( GetReadPos() - startPos == header.size );
			}
			else
			{
				SetReadPos( GetReadPos() + header.size );
				Warning( "Attempted to restore FIELD_EMBEDDEDBYREF %s but there is no destination memory\n", pField->fieldName );
			}
			break;
			
		}
		case FIELD_CUSTOM:
		{
			// No corresponding "block" (see write) as it was used as the header of the field
			int posNextField = GetReadPos() + header.size;

			SaveRestoreFieldInfo_t fieldInfo =
			{
				pDest,
				((char *)pDest) - pField->fieldOffset[ TD_OFFSET_NORMAL ],
				pField
			};
			
			pField->pSaveRestoreOps->Restore( fieldInfo, this );
			
			Assert( posNextField >= GetReadPos() );
			SetReadPos( posNextField );
			break;
		}
		
		default:
			Warning( "Bad field type\n" );
			Assert(0);
	}
}

//-------------------------------------

void CRestore::ReadField( const SaveRestoreRecordHeader_t &header, void *pDest, datamap_t *pRootMap, typedescription_t *pField )
{
	if ( pField->fieldType <= FIELD_CUSTOM )
		ReadBasicField( header, pDest, pRootMap, pField );
	else
		ReadGameField( header, pDest, pRootMap, pField );
}

//-------------------------------------

bool CRestore::ShouldReadField( typedescription_t *pField )
{
	if ( (pField->flags & FTYPEDESC_SAVE) == 0 )
		return false;

	if ( m_global && (pField->flags & FTYPEDESC_GLOBAL) )
		return false;

	return true;
}

//-------------------------------------

typedescription_t *CRestore::FindField( const char *pszFieldName, typedescription_t *pFields, int fieldCount, int *pCookie )
{
	int &fieldNumber = *pCookie;
	if ( pszFieldName )
	{
		typedescription_t *pTest;
		
		for ( int i = 0; i < fieldCount; i++ )
		{
			pTest = &pFields[fieldNumber];
			
			++fieldNumber;
			if ( fieldNumber == fieldCount )
				fieldNumber = 0;
			
			if ( stricmp( pTest->fieldName, pszFieldName ) == 0 )
				return pTest;
		}
	}

	fieldNumber = 0;
	return NULL;
}

//-------------------------------------

bool CRestore::ShouldEmptyField( typedescription_t *pField )
{
	// don't clear out fields that don't get saved, or that are handled specially
	if ( !( pField->flags & FTYPEDESC_SAVE ) )
		return false;

	// Don't clear global fields
	if ( m_global && (pField->flags & FTYPEDESC_GLOBAL) )
		return false;

	return true;
}

//-------------------------------------

void CRestore::EmptyFields( void *pBaseData, typedescription_t *pFields, int fieldCount )
{
	int i;
	for ( i = 0; i < fieldCount; i++ )
	{
		typedescription_t *pField = &pFields[i];
		if ( !ShouldEmptyField( pField ) )
			continue;

		void *pFieldData = (char *)pBaseData + pField->fieldOffset[ TD_OFFSET_NORMAL ];
		switch( pField->fieldType )
		{
		case FIELD_CUSTOM:
			{
				SaveRestoreFieldInfo_t fieldInfo =
				{
					pFieldData,
					pBaseData,
					pField
				};
				pField->pSaveRestoreOps->MakeEmpty( fieldInfo );
			}
			break;

		case FIELD_EMBEDDED:
			{
				if ( (pField->flags & FTYPEDESC_PTR) && !*((void **)pFieldData) )
					break;

				int nFieldCount = pField->fieldSize;
				char *pFieldMemory = (char *)( ( !(pField->flags & FTYPEDESC_PTR) ) ? pFieldData : *((void **)pFieldData) );
				while ( --nFieldCount >= 0 )
				{
					EmptyFields( pFieldMemory, pField->td->dataDesc, pField->td->dataNumFields );
					pFieldMemory += pField->fieldSizeInBytes;
				}
			}
			break;

		default:
			// NOTE: If you hit this assertion, you've got a bug where you're using 
			// the wrong field type for your field
			if ( pField->fieldSizeInBytes != pField->fieldSize * gSizes[pField->fieldType] )
			{
				Warning("WARNING! Field %s is using the wrong FIELD_ type!\nFix this or you'll see a crash.\n", pField->fieldName );
				Assert( 0 );
			}
			memset( pFieldData, (pField->fieldType != FIELD_EHANDLE) ? 0 : 0xFF, pField->fieldSize * gSizes[pField->fieldType] );
			break;
		}
	}
}

//-------------------------------------

void CRestore::StartBlock( SaveRestoreRecordHeader_t *pHeader )
{
	ReadHeader( pHeader );
	m_BlockEndStack.AddToTail( GetReadPos() + pHeader->size );	
}

//-------------------------------------

void CRestore::StartBlock( char szBlockName[] )
{
	SaveRestoreRecordHeader_t header;
	StartBlock( &header );
	Q_strncpy( szBlockName, StringFromHeaderSymbol( header.symbol ), SIZE_BLOCK_NAME_BUF );
}

//-------------------------------------

void CRestore::StartBlock()
{
	char szBlockName[SIZE_BLOCK_NAME_BUF];
	StartBlock( szBlockName );
}

//-------------------------------------

void CRestore::EndBlock()
{
	int endPos = m_BlockEndStack[ m_BlockEndStack.Count() - 1 ];
	m_BlockEndStack.Remove( m_BlockEndStack.Count() - 1 );
	SetReadPos( endPos );
}
	
//-------------------------------------

int CRestore::ReadFields( const char *pname, void *pBaseData, datamap_t *pRootMap, typedescription_t *pFields, int fieldCount )
{
	static int lastName = -1;
	Verify( ReadShort() == sizeof(int) );			// First entry should be an int
	int symName = m_pData->FindCreateSymbol(pname);

	// Check the struct name
	int curSym = ReadShort();
	if ( curSym != symName )			// Field Set marker
	{
		const char *pLastName = m_pData->StringFromSymbol( lastName );
		const char *pCurName = m_pData->StringFromSymbol( curSym );
		Msg( "Expected %s found %s ( raw '%s' )! (prev: %s)\n", pname, pCurName, BufferPointer(), pLastName );
		Msg( "Field type name may have changed or inheritance graph changed, save file is suspect\n" );
		m_pData->Rewind( 2*sizeof(short) );
		return 0;
	}
	lastName = symName;

	// Clear out base data
	EmptyFields( pBaseData, pFields, fieldCount );
	
	// Skip over the struct name
	int i;
	int nFieldsSaved = ReadInt();						// Read field count
	int searchCookie = 0;								// Make searches faster, most data is read/written in the same order
	SaveRestoreRecordHeader_t header;

	for ( i = 0; i < nFieldsSaved; i++ )
	{
		ReadHeader( &header );

		typedescription_t *pField = FindField( m_pData->StringFromSymbol( header.symbol ), pFields, fieldCount, &searchCookie);
		if ( pField && ShouldReadField( pField ) )
		{
			ReadField( header, ((char *)pBaseData + pField->fieldOffset[ TD_OFFSET_NORMAL ]), pRootMap, pField );
		}
		else
		{
			BufferSkipBytes( header.size );			// Advance to next field
		}
	}
	
	return 1;
}

//-------------------------------------

void CRestore::ReadHeader( SaveRestoreRecordHeader_t *pheader )
{
	if ( pheader != NULL )
	{
		Assert( pheader!=NULL );
		pheader->size = ReadShort();				// Read field size
		pheader->symbol = ReadShort();				// Read field name token
	}
	else
	{
		BufferSkipBytes( sizeof(short) * 2 );
	}
}

//-------------------------------------

short CRestore::ReadShort( void )
{
	short tmp = 0;

	BufferReadBytes( (char *)&tmp, sizeof(short) );

	return tmp;
}

//-------------------------------------

int	CRestore::ReadInt( void )
{
	int tmp = 0;

	BufferReadBytes( (char *)&tmp, sizeof(int) );

	return tmp;
}

//-------------------------------------
// Purpose: Recursively restores all the classes in an object, in reverse order (top down)
// Output : int 0 on failure, 1 on success

int CRestore::DoReadAll( void *pLeafObject, datamap_t *pLeafMap, datamap_t *pCurMap )
{
	// restore base classes first
	if ( pCurMap->baseMap )
	{
		int status = DoReadAll( pLeafObject, pLeafMap, pCurMap->baseMap );
		if ( !status )
			return status;
	}

	return ReadFields( pCurMap->dataClassName, pLeafObject, pLeafMap, pCurMap->dataDesc, pCurMap->dataNumFields );
}

//-------------------------------------

char *CRestore::BufferPointer( void )
{
	if ( !m_pData )
		return NULL;

	return m_pData->AccessCurPos();
}

//-------------------------------------

void CRestore::BufferReadBytes( char *pOutput, int size )
{
	Assert( m_pData !=NULL );

	if ( !m_pData || m_pData->BytesAvailable() == 0 )
		return;

	if ( !m_pData->Read( pOutput, size ) )
	{
		Warning( "Restore underflow!\n" );
		Assert(0);
	}
}

//-------------------------------------

void CRestore::BufferSkipBytes( int bytes )
{
	BufferReadBytes( NULL, bytes );
}

//-------------------------------------

int CRestore::ReadShort( short *pValue, int nElems, int nBytesAvailable )
{
	return ReadSimple( pValue, nElems, nBytesAvailable );
}

//-------------------------------------

int CRestore::ReadInt( int *pValue, int nElems, int nBytesAvailable )
{
	return ReadSimple( pValue, nElems, nBytesAvailable );
}

//-------------------------------------

int CRestore::ReadBool( bool *pValue, int nElems, int nBytesAvailable )
{
	COMPILE_TIME_ASSERT( sizeof(bool) == sizeof(char) );
	return ReadSimple( pValue, nElems, nBytesAvailable );
}

//-------------------------------------

int CRestore::ReadFloat( float *pValue, int nElems, int nBytesAvailable )
{
	return ReadSimple( pValue, nElems, nBytesAvailable );
}

//-------------------------------------

int CRestore::ReadData( char *pData, int size, int nBytesAvailable )
{
	return ReadSimple( pData, size, nBytesAvailable );
}

//-------------------------------------

void CRestore::ReadString( char *pDest, int nSizeDest, int nBytesAvailable )
{
	const char *pString = BufferPointer();
	if ( !nBytesAvailable )
		nBytesAvailable = strlen( pString ) + 1;
	BufferSkipBytes( nBytesAvailable );

	Q_strncpy(pDest, pString, nSizeDest );
}
	
//-------------------------------------

int CRestore::ReadString( string_t *pValue, int nElems, int nBytesAvailable )
{
	AssertMsg( nBytesAvailable > 0, "CRestore::ReadString() implementation does not currently support unspecified bytes available");
	
	int i;
	char *pString = BufferPointer();
	char *pLimit = pString + nBytesAvailable;
	for ( i = 0; i < nElems && pString < pLimit; i++ )
	{
		if ( *((char *)pString) == 0 )
			pValue[i] = NULL_STRING;
		else
			pValue[i] = AllocPooledString( (char *)pString );
		
		while (*pString)
			pString++;
		pString++;
	}

	BufferSkipBytes( nBytesAvailable );
	
	return i;
}

//-------------------------------------

int CRestore::ReadVector( Vector *pValue)
{
	BufferReadBytes( (char *)pValue, sizeof(Vector) );
	return 1;
}

//-------------------------------------

int CRestore::ReadVector( Vector *pValue, int nElems, int nBytesAvailable )
{
	return ReadSimple( pValue, nElems, nBytesAvailable );
}

int CRestore::ReadQuaternion( Quaternion *pValue)
{
	BufferReadBytes( (char *)pValue, sizeof(Quaternion) );
	return 1;
}

//-------------------------------------

int CRestore::ReadQuaternion( Quaternion *pValue, int nElems, int nBytesAvailable )
{
	return ReadSimple( pValue, nElems, nBytesAvailable );
}

//-------------------------------------
int CRestore::ReadVMatrix( VMatrix *pValue, int nElems, int nBytesAvailable )
{
	return ReadSimple( pValue, nElems, nBytesAvailable );
}


int CRestore::ReadVMatrixWorldspace( VMatrix *pValue, int nElems, int nBytesAvailable )
{
	Vector basePosition = m_pGameInfo->GetLandmark();
	VMatrix tmp;

	for ( int i = 0; i < nElems; i++ )
	{
		BufferReadBytes( (char *)&tmp, sizeof(float)*16 );

		VMatrixOffset( pValue[i], tmp, basePosition );
	}
	return nElems;
}


int CRestore::ReadMatrix3x4Worldspace( matrix3x4_t *pValue, int nElems, int nBytesAvailable )
{
	Vector basePosition = m_pGameInfo->GetLandmark();
	matrix3x4_t tmp;

	for ( int i = 0; i < nElems; i++ )
	{
		BufferReadBytes( (char *)&tmp, sizeof(matrix3x4_t) );

		Matrix3x4Offset( pValue[i], tmp, basePosition );
	}
	return nElems;
}

int CRestore::ReadInterval( interval_t *interval, int count, int nBytesAvailable )
{
	return ReadSimple( interval, count, nBytesAvailable );
}

//---------------------------------------------------------
//
// Game centric restore methods
//

#ifdef GAME_DLL
IHandleEntity* CRestoreServer::EntityFromIndex(int entityIndex)
{
	if (!m_pGameInfo || entityIndex < 0)
		return NULL;

	int i;
	entitytable_t* pTable;

	for (i = 0; i < m_pGameInfo->NumEntities(); i++)
	{
		pTable = m_pGameInfo->GetEntityInfo(i);
		if (pTable->id == entityIndex)
			return gEntList.GetServerEntityFromHandle(pTable->hEnt);
	}
	return NULL;
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
IHandleEntity* CRestoreClient::EntityFromIndex(int entityIndex)
{
	if (!m_pGameInfo || entityIndex < 0)
		return NULL;

	int i;
	entitytable_t* pTable;

	for (i = 0; i < m_pGameInfo->NumEntities(); i++)
	{
		pTable = m_pGameInfo->GetEntityInfo(i);
		if (pTable->id == entityIndex)
			return ClientEntityList().GetClientEntityFromHandle(pTable->hEnt);
	}
	return NULL;
}
#endif // CLIENT_DLL

//-------------------------------------

int CRestore::ReadEntityPtr( IHandleEntity **ppEntity, int count, int nBytesAvailable )
{
	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
	int entityArray[MAX_ENTITYARRAY];
	
	int nRead = ReadInt( entityArray, count, nBytesAvailable );
	
	for ( int i = 0; i < nRead; i++ ) // nRead is never greater than count
	{
		ppEntity[i] = EntityFromIndex( entityArray[i] );
	}
	
	if ( nRead < count)
	{
		memset( &ppEntity[nRead], 0, ( count - nRead ) * sizeof(ppEntity[0]) );
	}
	
	return nRead;
}

//-------------------------------------
//int CRestore::ReadEdictPtr( edict_t **ppEdict, int count, int nBytesAvailable )
//{
//#if !defined( CLIENT_DLL )
//	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
//	int entityArray[MAX_ENTITYARRAY];
//	CBaseEntity	*pEntity;
//	
//	int nRead = ReadInt( entityArray, count, nBytesAvailable );
//	
//	for ( int i = 0; i < nRead; i++ ) // nRead is never greater than count
//	{
//		pEntity = EntityFromIndex( entityArray[i] );
//		ppEdict[i] = (pEntity) ? pEntity->edict() : NULL;
//	}
//	
//	if ( nRead < count)
//	{
//		memset( &ppEdict[nRead], 0, ( count - nRead ) * sizeof(ppEdict[0]) );
//	}
//	
//	return nRead;
//#else
//	return 0;
//#endif
//}


//-------------------------------------

int CRestore::ReadEHandle( CBaseHandle *pEHandle, int count, int nBytesAvailable )
{
	AssertMsg( count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore" );
	int entityArray[MAX_ENTITYARRAY];
	
	int nRead = ReadInt( entityArray, count, nBytesAvailable );
	
	for ( int i = 0; i < nRead; i++ ) // nRead is never greater than count
	{
		pEHandle[i] = EntityFromIndex( entityArray[i] );
	}
	
	if ( nRead < count)
	{
		memset( &pEHandle[nRead], 0xFF, ( count - nRead ) * sizeof(pEHandle[0]) );
	}
	
	return nRead;
}
	
#ifdef GAME_DLL
int	CRestoreServer::GetModelIndex(const char* name) {
	return modelinfo->GetModelIndex(name);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
int	CRestoreClient::GetModelIndex(const char* name) {
	return modelinfo->GetModelIndex(name);
}
#endif // CLIENT_DLL

#ifdef GAME_DLL
void CRestoreServer::PrecacheModel(const char* pModelName) {
	engine->PrecacheModel(pModelName);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
void CRestoreClient::PrecacheModel(const char* pModelName) {

}
#endif // CLIENT_DLL

#ifdef GAME_DLL
int CRestoreServer::GetMaterialIndex(const char* pMaterialName) {
	return g_ServerGameDLL.GetMaterialIndex(pMaterialName);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
int CRestoreClient::GetMaterialIndex(const char* pMaterialName) {
	return clientdll->GetMaterialIndex(pMaterialName);
}
#endif // CLIENT_DLL

#ifdef GAME_DLL
void CRestoreServer::PrecacheMaterial(const char* pMaterialName) {
	g_ServerGameDLL.PrecacheMaterial(pMaterialName);
}
#endif

#ifdef CLIENT_DLL
void CRestoreClient::PrecacheMaterial(const char* pMaterialName) {

}
#endif // CLIENT_DLL

#ifdef GAME_DLL
void CRestoreServer::PrecacheScriptSound(const char* pSoundName) {
	g_pSoundEmitterSystem->PrecacheScriptSound(pSoundName);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
void CRestoreClient::PrecacheScriptSound(const char* pMaterialName) {

}
#endif // CLIENT_DLL

#ifdef GAME_DLL
void CRestoreServer::RenameMapName(string_t* pStringDest) {
	char buf[MAX_PATH];
	Q_strncpy(buf, "maps/", sizeof(buf));
	Q_strncat(buf, gpGlobals->mapname.ToCStr(), sizeof(buf));
	Q_strncat(buf, ".bsp", sizeof(buf));
	*pStringDest = AllocPooledString(buf);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
void CRestoreClient::RenameMapName(string_t* pStringDest) {

}
#endif // CLIENT_DLL

#ifdef GAME_DLL
string_t CRestoreServer::AllocPooledString(const char* pszValue) {
	return g_ServerGameDLL.AllocPooledString(pszValue);
}
#endif // GAME_DLL

#ifdef CLIENT_DLL
string_t CRestoreClient::AllocPooledString(const char* pszValue) {
	return clientdll->AllocPooledString(pszValue);
}
#endif // CLIENT_DLL


//-------------------------------------
// Purpose:	Reads all the fields that are not client neutral. In the event of 
//			a librarization of save/restore, these would NOT reside in the library

void CRestore::ReadGameField( const SaveRestoreRecordHeader_t &header, void *pDest, datamap_t *pRootMap, typedescription_t *pField )
{
	switch( pField->fieldType )
	{
		case FIELD_POSITION_VECTOR:
		{
			ReadPositionVector( (Vector *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_TIME:
		{
			ReadTime( (float *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_TICK:
		{
			ReadTick( (int *)pDest, pField->fieldSize, header.size );
			break;
		}
		
		case FIELD_FUNCTION:
		{
			ReadFunction( pRootMap, (inputfunc_t **)pDest, pField->fieldSize, header.size );
			break;
		}
		
		case FIELD_MODELINDEX:
		{
			int *pModelIndex = (int*)pDest;
			string_t *pModelName = (string_t *)stackalloc( pField->fieldSize * sizeof(string_t) );
			int nRead = ReadString( pModelName, pField->fieldSize, header.size );

			for ( int i = 0; i < nRead; i++ )
			{
				if ( pModelName[i] == NULL_STRING )
				{
					pModelIndex[i] = -1;
					continue;
				}

				pModelIndex[i] = GetModelIndex( STRING( pModelName[i] ) );

				if ( m_precache )
				{
					PrecacheModel( STRING( pModelName[i] ) );
				}
			}
			break;
		}

		case FIELD_MATERIALINDEX:
		{
			int *pMaterialIndex = (int*)pDest;
			string_t *pMaterialName = (string_t *)stackalloc( pField->fieldSize * sizeof(string_t) );
			int nRead = ReadString( pMaterialName, pField->fieldSize, header.size );

			for ( int i = 0; i < nRead; i++ )
			{
				if ( pMaterialName[i] == NULL_STRING )
				{
					pMaterialIndex[i] = 0;
					continue;
				}

				pMaterialIndex[i] = GetMaterialIndex(STRING(pMaterialName[i]));

				if ( m_precache )
				{
					PrecacheMaterial( STRING( pMaterialName[i] ) );
				}
			}
			break;
		}
		
		case FIELD_MODELNAME:
		case FIELD_SOUNDNAME:
		{
			string_t *pStringDest = (string_t *)pDest;
			int nRead = ReadString( pStringDest, pField->fieldSize, header.size );
			if ( m_precache )
			{
				// HACKHACK: Rewrite the .bsp models to match the map name in case the bugreporter renamed it
				if ( pField->fieldType == FIELD_MODELNAME && Q_stristr( STRING( *pStringDest ), ".bsp") )
				{
					RenameMapName(pStringDest);
				}
				for ( int i = 0; i < nRead; i++ )
				{
					if ( pStringDest[i] != NULL_STRING )
					{
						if ( pField->fieldType == FIELD_MODELNAME )
						{
							PrecacheModel( STRING( pStringDest[i] ) );
						}
						else if ( pField->fieldType == FIELD_SOUNDNAME )
						{
							PrecacheScriptSound( STRING( pStringDest[i] ) );
						}
					}
				}
			}
			break;
		}
		
		case FIELD_CLASSPTR:
			ReadEntityPtr( (IHandleEntity **)pDest, pField->fieldSize, header.size );
			break;
			
		case FIELD_EDICT:
			//ReadEdictPtr( (edict_t **)pDest, pField->fieldSize, header.size );
			Error("ReadEdictPtr has been removed!");
			break;
		case FIELD_EHANDLE:
			ReadEHandle( (CBaseHandle*)pDest, pField->fieldSize, header.size );
			break;

		case FIELD_VMATRIX:
		{
			ReadVMatrix( (VMatrix *)pDest, pField->fieldSize, header.size );
			break;
		}

		case FIELD_VMATRIX_WORLDSPACE:
			ReadVMatrixWorldspace( (VMatrix *)pDest, pField->fieldSize, header.size );
			break;

		case FIELD_MATRIX3X4_WORLDSPACE:
			ReadMatrix3x4Worldspace( (matrix3x4_t *)pDest, pField->fieldSize, header.size );
			break;

		case FIELD_INTERVAL:
			ReadInterval( (interval_t *)pDest, pField->fieldSize, header.size );
			break;

		case FIELD_POINTER:
			ReadData( (char *)pDest, sizeof(void*)*pField->fieldSize, header.size );
			break;

		default:
			Warning( "Bad field type\n" );
			Assert(0);
	}
}

//-------------------------------------

int CRestore::ReadTime( float *pValue, int count, int nBytesAvailable )
{
	float baseTime = m_pGameInfo->GetBaseTime();
	int nRead = ReadFloat( pValue, count, nBytesAvailable );
	
	for ( int i = nRead - 1; i >= 0; i-- )
	{
		if ( pValue[i] == ZERO_TIME )
			pValue[i] = 0.0;
		else if ( pValue[i] != INVALID_TIME && pValue[i] != FLT_MAX )
			pValue[i] += baseTime;
	}
	
	return nRead;
}

int CRestore::ReadTick( int *pValue, int count, int nBytesAvailable )
{
	// HACK HACK:  Adding 0.1f here makes sure that all tick times read
	//  from .sav file which are near the basetime will end up just ahead of
	//  the base time, because we are restoring we'll have a slow frame of the
	//  max frametime of 0.1 seconds and that could otherwise cause all of our
	//  think times to get synchronized to each other... sigh.  ywb...
	int baseTick = TIME_TO_TICKS( m_pGameInfo->GetBaseTime() + 0.1f );
	int nRead = ReadInt( pValue, count, nBytesAvailable );
	
	for ( int i = nRead - 1; i >= 0; i-- )
	{
		if ( pValue[ i ] != TICK_NEVER_THINK_ENCODE )
		{
			// Rebase it
			pValue[i] += baseTick;
		}
		else
		{
			// Slam to -1 value
			pValue[ i ] = TICK_NEVER_THINK;
		}
	}
	
	return nRead;
}

//-------------------------------------

int CRestore::ReadPositionVector( Vector *pValue )
{
	return ReadPositionVector( pValue, 1, sizeof(Vector) );
}

//-------------------------------------

int CRestore::ReadPositionVector( Vector *pValue, int count, int nBytesAvailable )
{
	Vector basePosition = m_pGameInfo->GetLandmark();
	int nRead = ReadVector( pValue, count, nBytesAvailable );
	
	for ( int i = nRead - 1; i >= 0; i-- )
	{
		if ( pValue[i] != vec3_invalid )
			pValue[i] += basePosition;
	}
	
	return nRead;
}

//-------------------------------------

int CRestore::ReadFunction( datamap_t *pMap, inputfunc_t **pValue, int count, int nBytesAvailable )
{
	AssertMsg( nBytesAvailable > 0, "CRestore::ReadFunction() implementation does not currently support unspecified bytes available");
	
	char *pszFunctionName = BufferPointer();
	BufferSkipBytes( nBytesAvailable );
	
	AssertMsg( count == 1, "Arrays of functions not presently supported" );
	
	if ( *pszFunctionName == 0 )
		*pValue = NULL;
	else
	{
		inputfunc_t func = UTIL_FunctionFromName( pMap, pszFunctionName );
#ifdef GNUC
		Q_memcpy( (void*)pValue, &func, sizeof(void*)*2 );
#else
		Q_memcpy( (void*)pValue, &func, sizeof(void*) );
#endif
	}
	return 0;
}
	
//-----------------------------------------------------------------------------
//
// Entity data saving routines
//
//-----------------------------------------------------------------------------

BEGIN_SIMPLE_DATADESC(entitytable_t)
	DEFINE_FIELD( id, FIELD_INTEGER ),
	DEFINE_FIELD( edictindex, FIELD_INTEGER ),
	DEFINE_FIELD( saveentityindex, FIELD_INTEGER ),
//	DEFINE_FIELD( restoreentityindex, FIELD_INTEGER ),
	//				hEnt		(not saved, this is the fixup)
	DEFINE_FIELD( location, FIELD_INTEGER ),
	DEFINE_FIELD( size, FIELD_INTEGER ),
	DEFINE_FIELD( flags, FIELD_INTEGER ),
	DEFINE_FIELD( classname, FIELD_STRING ),
	DEFINE_FIELD( globalname, FIELD_STRING ),
	DEFINE_FIELD( landmarkModelSpace, FIELD_VECTOR ),
	DEFINE_FIELD( modelname, FIELD_STRING ),
END_DATADESC()


//-----------------------------------------------------------------------------
// Utilities entities can use when saving
//-----------------------------------------------------------------------------
class CEntitySaveUtils : public IEntitySaveUtils
{
public:
	// Call these in pre-save + post save
	void PreSave();
	void PostSave();

	// Methods of IEntitySaveUtils
	virtual void AddLevelTransitionSaveDependency( CBaseEntity *pEntity1, CBaseEntity *pEntity2 );
	virtual int GetEntityDependencyCount( CBaseEntity *pEntity );
	virtual int GetEntityDependencies( CBaseEntity *pEntity, int nCount, CBaseEntity **ppEntList );

private:
 	IPhysicsObjectPairHash *m_pLevelAdjacencyDependencyHash;
};


//-----------------------------------------------------------------------------
// Call these in pre-save + post save
//-----------------------------------------------------------------------------
void CEntitySaveUtils::PreSave()
{
	Assert( !m_pLevelAdjacencyDependencyHash );
	MEM_ALLOC_CREDIT();
	m_pLevelAdjacencyDependencyHash = physics->CreateObjectPairHash();
}

void CEntitySaveUtils::PostSave()
{
 	physics->DestroyObjectPairHash( m_pLevelAdjacencyDependencyHash );
	m_pLevelAdjacencyDependencyHash = NULL;
}


//-----------------------------------------------------------------------------
// Gets the # of dependencies for a particular entity
//-----------------------------------------------------------------------------
int CEntitySaveUtils::GetEntityDependencyCount( CBaseEntity *pEntity )
{
	return m_pLevelAdjacencyDependencyHash->GetPairCountForObject( pEntity );
}


//-----------------------------------------------------------------------------
// Gets all dependencies for a particular entity
//-----------------------------------------------------------------------------
int CEntitySaveUtils::GetEntityDependencies( CBaseEntity *pEntity, int nCount, CBaseEntity **ppEntList )
{
	return m_pLevelAdjacencyDependencyHash->GetPairListForObject( pEntity, nCount, (void**)ppEntList );
}


//-----------------------------------------------------------------------------
// Methods of IEntitySaveUtils
//-----------------------------------------------------------------------------
void CEntitySaveUtils::AddLevelTransitionSaveDependency( CBaseEntity *pEntity1, CBaseEntity *pEntity2 )
{
	if ( pEntity1 != pEntity2 )
	{
		m_pLevelAdjacencyDependencyHash->AddObjectPair( pEntity1, pEntity2 );
	}
}


//-----------------------------------------------------------------------------
// Block handler for save/restore of entities
//-----------------------------------------------------------------------------
class CEntitySaveRestoreBlockHandler : public ISaveRestoreBlockHandler
{
public:
	const char *GetBlockName();
	void PreSave( CSaveRestoreData *pSaveData );
	void Save( ISave *pSave );
	void WriteSaveHeaders( ISave *pSave );
	virtual void PostSave();
	virtual void PreRestore();
	void ReadRestoreHeaders( IRestore *pRestore );

	void Restore( IRestore *pRestore, bool createPlayers );
	virtual void PostRestore();

	inline IEntitySaveUtils * GetEntitySaveUtils() { return &m_EntitySaveUtils; }

private:
	friend int CreateEntityTransitionList( CSaveRestoreData *pSaveData, int levelMask );
	bool SaveInitEntities( CSaveRestoreData *pSaveData );	
	bool DoRestoreEntity( CBaseEntity *pEntity, IRestore *pRestore );
	Vector ModelSpaceLandmark( int modelIndex );
	int RestoreEntity( CBaseEntity *pEntity, IRestore *pRestore, entitytable_t *pEntInfo );

#if !defined( CLIENT_DLL )	
	// Find the matching global entity.  Spit out an error if the designer made entities of
	// different classes with the same global name
	CBaseEntity *FindGlobalEntity( string_t classname, string_t globalname );

	int RestoreGlobalEntity( CBaseEntity *pEntity, CSaveRestoreData *pSaveData, entitytable_t *pEntInfo );
#endif

private:
	CEntitySaveUtils	m_EntitySaveUtils;
};


//-----------------------------------------------------------------------------

CEntitySaveRestoreBlockHandler g_EntitySaveRestoreBlockHandler;

//-------------------------------------

ISaveRestoreBlockHandler *GetEntitySaveRestoreBlockHandler()
{
	return &g_EntitySaveRestoreBlockHandler;
}

IEntitySaveUtils *GetEntitySaveUtils()
{
	return g_EntitySaveRestoreBlockHandler.GetEntitySaveUtils();
}


//-----------------------------------------------------------------------------
// Implementation of the block handler for save/restore of entities
//-----------------------------------------------------------------------------
const char *CEntitySaveRestoreBlockHandler::GetBlockName()
{
	return "Entities";
}

//---------------------------------

void CEntitySaveRestoreBlockHandler::PreSave( CSaveRestoreData *pSaveData )
{
	MDLCACHE_CRITICAL_SECTION();
	IGameSystem::OnSaveAllSystems();

	m_EntitySaveUtils.PreSave();

	// Allow the entities to do some work
	CBaseEntity *pEnt = NULL;
#if !defined( CLIENT_DLL )
	while ( (pEnt = gEntList.NextEnt( pEnt )) != NULL )
	{
		pEnt->OnSave( &m_EntitySaveUtils );
	}
#else
	// Do this because it'll force entities to figure out their origins, and that requires
	// SetupBones in the case of aiments.
	{
		C_BaseAnimating::AutoAllowBoneAccess boneaccess( true, true );

		int last = ClientEntityList().GetHighestEntityIndex();
		ClientEntityHandle_t iter = ClientEntityList().FirstHandle();

		for ( int e = 0; e <= last; e++ )
		{
			pEnt = ClientEntityList().GetBaseEntity( e );

			if(  !pEnt )
				continue;

			pEnt->OnSave();
		}

		while ( iter != ClientEntityList().InvalidHandle() )
		{
			pEnt = ClientEntityList().GetBaseEntityFromHandle( iter );

			if ( pEnt && pEnt->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE ) 
			{
				pEnt->OnSave();
			}

			iter = ClientEntityList().NextHandle( iter );
		}
	}
#endif
	SaveInitEntities( pSaveData );
}

//---------------------------------

CBaseEntity* EntityFromHandle(CBaseHandle& handle) {
#ifdef GAME_DLL
	return (CBaseEntity*)gEntList.GetServerEntityFromHandle(handle);
#endif // GAME_DLL
#ifdef CLIENT_DLL
	return (CBaseEntity*)ClientEntityList().GetClientEntityFromHandle(handle);
#endif // CLIENT_DLL
}


void CEntitySaveRestoreBlockHandler::Save( ISave *pSave )
{
	CGameSaveRestoreInfo *pSaveData = pSave->GetGameSaveRestoreInfo();
	
	// write entity list that was previously built by SaveInitEntities()
	for ( int i = 0; i < pSaveData->NumEntities(); i++ )
	{
		entitytable_t *pEntInfo = pSaveData->GetEntityInfo( i );
		pEntInfo->location = pSave->GetWritePos();
		pEntInfo->size = 0;

		CBaseEntity *pEnt = EntityFromHandle(pEntInfo->hEnt);
		if ( pEnt && !( pEnt->ObjectCaps() & FCAP_DONT_SAVE ) )
		{
			MDLCACHE_CRITICAL_SECTION();
#if !defined( CLIENT_DLL )
			AssertMsg( pEnt->entindex()==-1 || ( pEnt->m_iClassname != NULL_STRING && 
										   (STRING(pEnt->m_iClassname)[0] != 0) && 
										   FStrEq( STRING(pEnt->m_iClassname), pEnt->GetClassname()) ), 
					   "Saving entity with invalid classname" );
#endif

			pSaveData->SetCurrentEntityContext( pEnt );
			pEnt->Save( *pSave );
			pSaveData->SetCurrentEntityContext( NULL );

			pEntInfo->size = pSave->GetWritePos() - pEntInfo->location;	// Size of entity block is data size written to block

			pEntInfo->classname = pEnt->m_iClassname;	// Remember entity class for respawn

#if !defined( CLIENT_DLL )
			pEntInfo->globalname = pEnt->m_iGlobalname; // remember global name
			pEntInfo->landmarkModelSpace = ModelSpaceLandmark( pEnt->GetModelIndex() );
			int nEntIndex = pEnt->IsNetworkable() ? pEnt->entindex() : -1;
			bool bIsPlayer = ( ( nEntIndex >= 1 ) && ( nEntIndex <= gpGlobals->maxClients ) ) ? true : false;
			if ( bIsPlayer )
			{
				pEntInfo->flags |= FENTTABLE_PLAYER;
			}
#endif
		}
	}
}

//---------------------------------

void CEntitySaveRestoreBlockHandler::WriteSaveHeaders( ISave *pSave )
{
	CGameSaveRestoreInfo *pSaveData = pSave->GetGameSaveRestoreInfo();

	int nEntities = pSaveData->NumEntities();
	pSave->WriteInt( &nEntities );
	
	for ( int i = 0; i < pSaveData->NumEntities(); i++ )
		pSave->WriteFields( "ETABLE", pSaveData->GetEntityInfo( i ), NULL, entitytable_t::m_DataMap.dataDesc, entitytable_t::m_DataMap.dataNumFields );
}
	
//---------------------------------

void CEntitySaveRestoreBlockHandler::PostSave()
{
	m_EntitySaveUtils.PostSave();
}

//---------------------------------

void CEntitySaveRestoreBlockHandler::PreRestore()
{
}

//---------------------------------

void CEntitySaveRestoreBlockHandler::ReadRestoreHeaders( IRestore *pRestore )
{
	CGameSaveRestoreInfo *pSaveData = pRestore->GetGameSaveRestoreInfo();

	int nEntities;
	pRestore->ReadInt( &nEntities );

	entitytable_t *pEntityTable = ( entitytable_t *)engine->SaveAllocMemory( (sizeof(entitytable_t) * nEntities), sizeof(char) );
	if ( !pEntityTable )
	{
		return;
	}

	pSaveData->InitEntityTable( pEntityTable, nEntities );
	
	for (int i = 0; i < pSaveData->NumEntities(); i++) {
		if (i == 165) {
			int aaa = 0;
		}
		entitytable_t* pEntityTable = pSaveData->GetEntityInfo(i);
		pRestore->ReadFields("ETABLE", pEntityTable, NULL, entitytable_t::m_DataMap.dataDesc, entitytable_t::m_DataMap.dataNumFields);
		pEntityTable = pSaveData->GetEntityInfo(i);
	}
}

//---------------------------------

#if !defined( CLIENT_DLL )

void CEntitySaveRestoreBlockHandler::Restore( IRestore *pRestore, bool createPlayers )
{
	entitytable_t *pEntInfo;
	CBaseEntity *pent;

	CGameSaveRestoreInfo *pSaveData = pRestore->GetGameSaveRestoreInfo();
	
	bool restoredWorld = false;

	// Create entity list
	int i;
	for ( i = 0; i < pSaveData->NumEntities(); i++ )
	{
		pEntInfo = pSaveData->GetEntityInfo( i );

		if ( pEntInfo->classname != NULL_STRING && pEntInfo->size && !(pEntInfo->flags & FENTTABLE_REMOVED) )
		{
			if ( pEntInfo->edictindex == 0 )	// worldspawn
			{
				Assert( i == 0 );
				pent = gEntList.CreateEntityByName( STRING(pEntInfo->classname) );
				pRestore->SetReadPos( pEntInfo->location );
				if ( RestoreEntity( pent, pRestore, pEntInfo ) < 0 )
				{
					pEntInfo->hEnt = NULL;
					pEntInfo->restoreentityindex = -1;
					UTIL_RemoveImmediate( pent );	
				}
				else
				{
					// force the entity to be relinked
					AddRestoredEntity( pent );
				}
			}
			else if ( (pEntInfo->edictindex > 0) && (pEntInfo->edictindex <= gpGlobals->maxClients) )
			{
				if ( !(pEntInfo->flags & FENTTABLE_PLAYER) )
				{
					Warning( "ENTITY IS NOT A PLAYER: %d\n" , i );
					Assert(0);
				}

				if ( createPlayers )//ed && 
				{
					// create the player
					pent = CBasePlayer::CreatePlayer( STRING(pEntInfo->classname), pEntInfo->edictindex);
				}
				else
					pent = NULL;
			}
			else
			{
				pent = gEntList.CreateEntityByName( STRING(pEntInfo->classname) );
			}
			pEntInfo->hEnt = pent;
			pEntInfo->restoreentityindex = pent && pent->IsNetworkable() ? pent->entindex() : - 1;
			if ( pent && pEntInfo->restoreentityindex == 0 )
			{
				if ( !FClassnameIs( pent, "worldspawn" ) )
				{
					pEntInfo->restoreentityindex = -1;
				}
			}

			if ( pEntInfo->restoreentityindex == 0 )
			{
				Assert( !restoredWorld );
				restoredWorld = true;
			}
		}
		else
		{
			pEntInfo->hEnt = NULL;
			pEntInfo->restoreentityindex = -1;
		}
	}

	// Now spawn entities
	for ( i = 0; i < pSaveData->NumEntities(); i++ )
	{
		pEntInfo = pSaveData->GetEntityInfo( i );
		if ( pEntInfo->edictindex != 0 )
		{
			pent = EntityFromHandle(pEntInfo->hEnt);
			pRestore->SetReadPos( pEntInfo->location );
			if ( pent )
			{
				if ( RestoreEntity( pent, pRestore, pEntInfo ) < 0 )
				{
					pEntInfo->hEnt = NULL;
					pEntInfo->restoreentityindex = -1;
					UTIL_RemoveImmediate( pent );
				}
				else
				{
					AddRestoredEntity( pent );
				}
			}
		}
	}
}

#else // CLIENT DLL VERSION

void CEntitySaveRestoreBlockHandler::Restore( IRestore *pRestore, bool createPlayers )
{
	entitytable_t *pEntInfo;
	CBaseEntity *pent;

	CGameSaveRestoreInfo *pSaveData = pRestore->GetGameSaveRestoreInfo();
	
	// Create entity list
	int i;
	bool restoredWorld = false;

	for ( i = 0; i < pSaveData->NumEntities(); i++ )
	{
		pEntInfo = pSaveData->GetEntityInfo( i );
		pent = ClientEntityList().GetBaseEntity( pEntInfo->restoreentityindex );
		pEntInfo->hEnt = pent;
	}

	// Blast saved data into entities
	for ( i = 0; i < pSaveData->NumEntities(); i++ )
	{
		pEntInfo = pSaveData->GetEntityInfo( i );

		bool bRestoredCorrectly = false;
		// FIXME, need to translate save spot to real index here using lookup table transmitted from server
		//Assert( !"Need translation still" );
		if ( pEntInfo->restoreentityindex >= 0 )
		{
			if ( pEntInfo->restoreentityindex == 0 )
			{
				Assert( !restoredWorld );
				restoredWorld = true;
			}

			pent = ClientEntityList().GetBaseEntity( pEntInfo->restoreentityindex );
			pRestore->SetReadPos( pEntInfo->location );
			if ( pent )
			{
				if ( RestoreEntity( pent, pRestore, pEntInfo ) >= 0 )
				{
					// Call the OnRestore method
					AddRestoredEntity( pent );
					bRestoredCorrectly = true;
				}
			}
		}
		// BUGBUG: JAY: Disable ragdolls across transitions until PVS/solid check & client entity patch file are implemented
		else if ( !pSaveData->levelInfo.fUseLandmark )
		{
			if ( pEntInfo->classname != NULL_STRING )
			{
				pent = cl_entitylist->CreateEntityByName( STRING(pEntInfo->classname) );
				pent->InitializeAsClientEntity( NULL, RENDER_GROUP_OPAQUE_ENTITY );
				
				pRestore->SetReadPos( pEntInfo->location );

				if ( pent )
				{
					if ( RestoreEntity( pent, pRestore, pEntInfo ) >= 0 )
					{
						pEntInfo->hEnt = pent;
						AddRestoredEntity( pent );
						bRestoredCorrectly = true;
					}
				}
			}
		}

		if ( !bRestoredCorrectly )
		{
			pEntInfo->hEnt = NULL;
			pEntInfo->restoreentityindex = -1;
		}
	}

	// Note, server does this after local player connects fully
	IGameSystem::OnRestoreAllSystems();

	// Tell hud elements to modify behavior based on game restoration, if applicable
	gHUD.OnRestore();
}
#endif

void CEntitySaveRestoreBlockHandler::PostRestore()
{
}

void SaveEntityOnTable( CBaseEntity *pEntity, CSaveRestoreData *pSaveData, int &iSlot )
{
	entitytable_t *pEntInfo = pSaveData->GetEntityInfo( iSlot );
	pEntInfo->id = iSlot;
#if !defined( CLIENT_DLL )
	pEntInfo->edictindex = pEntity->RequiredEdictIndex();
#else
	pEntInfo->edictindex = -1;
#endif
	pEntInfo->modelname = pEntity->GetModelName();
	pEntInfo->restoreentityindex = -1;
	pEntInfo->saveentityindex = pEntity && pEntity->IsNetworkable() ? pEntity->entindex() : -1;
	pEntInfo->hEnt = pEntity->GetRefEHandle();
	pEntInfo->flags = 0;
	pEntInfo->location = 0;
	pEntInfo->size = 0;
	pEntInfo->classname = NULL_STRING;

	iSlot++;
}


//---------------------------------

bool CEntitySaveRestoreBlockHandler::SaveInitEntities( CSaveRestoreData *pSaveData )
{
	int number_of_entities;

#if !defined( CLIENT_DLL )
	number_of_entities = gEntList.NumberOfEntities();
#else
	number_of_entities = ClientEntityList().NumberOfEntities( true );
#endif
	entitytable_t *pEntityTable = ( entitytable_t *)engine->SaveAllocMemory( (sizeof(entitytable_t) * number_of_entities), sizeof(char) );
	if ( !pEntityTable )
		return false;

	pSaveData->InitEntityTable( pEntityTable, number_of_entities );

	// build the table of entities
	// this is used to turn pointers into savable indices
	// build up ID numbers for each entity, for use in pointer conversions
	// if an entity requires a certain edict number upon restore, save that as well
	CBaseEntity *pEnt = NULL;
	int i = 0;

#if !defined( CLIENT_DLL )
	while ( (pEnt = gEntList.NextEnt( pEnt )) != NULL )
	{
#else
	int last = ClientEntityList().GetHighestEntityIndex();

	for ( int e = 0; e <= last; e++ )
	{
		pEnt = ClientEntityList().GetBaseEntity( e );
		if(  !pEnt )
			continue;
#endif
		SaveEntityOnTable( pEnt, pSaveData, i );
	}

#if defined( CLIENT_DLL )
	ClientEntityHandle_t iter = ClientEntityList().FirstHandle();

	while ( iter != ClientEntityList().InvalidHandle() )
	{
		pEnt = ClientEntityList().GetBaseEntityFromHandle( iter );

		if ( pEnt && pEnt->ObjectCaps() & FCAP_SAVE_NON_NETWORKABLE  ) 
		{
			SaveEntityOnTable( pEnt, pSaveData, i );
		}

		iter = ClientEntityList().NextHandle( iter );
	}
#endif

	//pSaveData->BuildEntityHash();

	Assert( i == pSaveData->NumEntities() );
	return ( i == pSaveData->NumEntities() );
}

//---------------------------------

#if !defined( CLIENT_DLL )

// Find the matching global entity.  Spit out an error if the designer made entities of
// different classes with the same global name
CBaseEntity *CEntitySaveRestoreBlockHandler::FindGlobalEntity( string_t classname, string_t globalname )
{
	CBaseEntity *pReturn = NULL;

	while ( (pReturn = gEntList.NextEnt( pReturn )) != NULL )
	{
		if ( FStrEq( STRING(pReturn->m_iGlobalname), STRING(globalname)) )
			break;
	}
		
	if ( pReturn )
	{
		if ( !FClassnameIs( pReturn, STRING(classname) ) )
		{
			Warning( "Global entity found %s, wrong class %s [expects class %s]\n", STRING(globalname), STRING(pReturn->m_iClassname), STRING(classname) );
			pReturn = NULL;
		}
	}

	return pReturn;
}

#endif	// !defined( CLIENT_DLL )

//---------------------------------

bool CEntitySaveRestoreBlockHandler::DoRestoreEntity( CBaseEntity *pEntity, IRestore *pRestore )
{
	MDLCACHE_CRITICAL_SECTION();

	EHANDLE hEntity;
	
	hEntity = pEntity;

	pRestore->GetGameSaveRestoreInfo()->SetCurrentEntityContext( pEntity );
	pEntity->Restore( *pRestore );
	pRestore->GetGameSaveRestoreInfo()->SetCurrentEntityContext( NULL );

#if !defined( CLIENT_DLL )
	if ( pEntity->ObjectCaps() & FCAP_MUST_SPAWN )
	{
		pEntity->Spawn();
	}
	else
	{
		pEntity->Precache( );
	}
#endif

	// Above calls may have resulted in self destruction
	return ( hEntity != NULL );
}

//---------------------------------
// Get a reference position in model space to compute
// changes in model space for global brush entities (designer models them in different coords!)
Vector CEntitySaveRestoreBlockHandler::ModelSpaceLandmark( int modelIndex )
{
	const model_t *pModel = modelinfo->GetModel( modelIndex );
	if ( modelinfo->GetModelType( pModel ) != mod_brush )
		return vec3_origin;

	Vector mins, maxs;
	modelinfo->GetModelBounds( pModel, mins, maxs );
	return mins;
}


int CEntitySaveRestoreBlockHandler::RestoreEntity( CBaseEntity *pEntity, IRestore *pRestore, entitytable_t *pEntInfo )
{
	if ( !DoRestoreEntity( pEntity, pRestore ) )
		return 0;

#if !defined( CLIENT_DLL )		
	if ( pEntity->m_iGlobalname != NULL_STRING ) 
	{
		int globalIndex = GlobalEntity_GetIndex( pEntity->m_iGlobalname );
		if ( globalIndex >= 0 )
		{
			// Already dead? delete
			if ( GlobalEntity_GetState( globalIndex ) == GLOBAL_DEAD )
				return -1;
			else if ( !FStrEq( STRING(gpGlobals->mapname), GlobalEntity_GetMap(globalIndex) ) )
			{
				pEntity->MakeDormant();	// Hasn't been moved to this level yet, wait but stay alive
			}
			// In this level & not dead, continue on as normal
		}
		else
		{
			Warning( "Global Entity %s (%s) not in table!!!\n", STRING(pEntity->m_iGlobalname), STRING(pEntity->m_iClassname) );
			// Spawned entities default to 'On'
			GlobalEntity_Add( pEntity->m_iGlobalname, gpGlobals->mapname, GLOBAL_ON );
		}
	}
#endif

	return 0;
}

//---------------------------------

#if !defined( CLIENT_DLL )
	
int CEntitySaveRestoreBlockHandler::RestoreGlobalEntity( CBaseEntity *pEntity, CSaveRestoreData *pSaveData, entitytable_t *pEntInfo )
{
	Vector oldOffset;
	EHANDLE hEntitySafeHandle;
	hEntitySafeHandle = pEntity;

	oldOffset.Init();
	CRestoreServer restoreHelper( pSaveData );
	
	string_t globalName = pEntInfo->globalname, className = pEntInfo->classname;

	// -------------------

	int globalIndex = GlobalEntity_GetIndex( globalName );
	
	// Don't overlay any instance of the global that isn't the latest
	// pSaveData->szCurrentMapName is the level this entity is coming from
	// pGlobal->levelName is the last level the global entity was active in.
	// If they aren't the same, then this global update is out of date.
	if ( !FStrEq( pSaveData->levelInfo.szCurrentMapName, GlobalEntity_GetMap(globalIndex) ) )
	{
		return 0;
	}

	// Compute the new global offset
	CBaseEntity *pNewEntity = FindGlobalEntity( className, globalName );
	if ( pNewEntity )
	{
//				Msg( "Overlay %s with %s\n", pNewEntity->GetClassname(), STRING(tmpEnt->classname) );
		// Tell the restore code we're overlaying a global entity from another level
		restoreHelper.SetGlobalMode( 1 );	// Don't overwrite global fields

		pSaveData->modelSpaceOffset = pEntInfo->landmarkModelSpace - ModelSpaceLandmark( pNewEntity->GetModelIndex() );

		UTIL_Remove( pEntity );
		pEntity = pNewEntity;// we're going to restore this data OVER the old entity
		pEntInfo->hEnt = pEntity;
		// HACKHACK: Do we need system-wide support for removing non-global spawn allocated resources?
		pEntity->VPhysicsDestroyObject();
		Assert( pEntInfo->edictindex == -1 );
		// Update the global table to say that the global definition of this entity should come from this level
		GlobalEntity_SetMap( globalIndex, gpGlobals->mapname );
	}
	else
	{
		// This entity will be freed automatically by the engine->  If we don't do a restore on a matching entity (below)
		// or call EntityUpdate() to move it to this level, we haven't changed global state at all.
		DevMsg( "Warning: No match for global entity %s found in destination level\n", STRING(globalName) );
		return 0;
	}
	
	if ( !DoRestoreEntity( pEntity, &restoreHelper ) )
	{
		pEntity = NULL;
	}

	// Is this an overriding global entity (coming over the transition)
	pSaveData->modelSpaceOffset.Init();
	if ( pEntity )
		return 1;
	return 0;
}

#endif	// !defined( CLIENT_DLL )



//-----------------------------------------------------------------------------







//=============================================================================
#if !defined( CLIENT_DLL )

//------------------------------------------------------------------------------
// Creates all entities that lie in the transition list
//------------------------------------------------------------------------------
void CreateEntitiesInTransitionList( CSaveRestoreData *pSaveData, int levelMask )
{
	CBaseEntity *pent;
	int i;
	for ( i = 0; i < pSaveData->NumEntities(); i++ )
	{
		entitytable_t *pEntInfo = pSaveData->GetEntityInfo( i );
		pEntInfo->hEnt = NULL;

		if ( pEntInfo->size == 0 || pEntInfo->edictindex == 0 )
			continue;

		if ( pEntInfo->classname == NULL_STRING )
		{
			Warning( "Entity with data saved, but with no classname\n" );
			Assert(0);
			continue;
		}

		bool active = (pEntInfo->flags & levelMask) ? 1 : 0;

		// spawn players
		pent = NULL;
		if ( (pEntInfo->edictindex > 0) && (pEntInfo->edictindex <= gpGlobals->maxClients) )	
		{
			if ( active  )//&& ed && !ed->IsFree()
			{
				if ( !(pEntInfo->flags & FENTTABLE_PLAYER) )
				{
					Warning( "ENTITY IS NOT A PLAYER: %d\n" , i );
					Assert(0);
				}

				pent = CBasePlayer::CreatePlayer( STRING(pEntInfo->classname), pEntInfo->edictindex);
			}
		}
		else if ( active )
		{
			pent = gEntList.CreateEntityByName( STRING(pEntInfo->classname) );
		}

		pEntInfo->hEnt = pent;
	}
}


//-----------------------------------------------------------------------------
int CreateEntityTransitionList( CSaveRestoreData *pSaveData, int levelMask )
{
	CBaseEntity *pent;
	entitytable_t *pEntInfo;

	// Create entity list
	CreateEntitiesInTransitionList( pSaveData, levelMask );
	
	// Now spawn entities
	CUtlVector<int> checkList;

	int i;
	int movedCount = 0;
	for ( i = 0; i < pSaveData->NumEntities(); i++ )
	{
		pEntInfo = pSaveData->GetEntityInfo( i );
		pent = EntityFromHandle(pEntInfo->hEnt);
//		pSaveData->currentIndex = i;
		pSaveData->Seek( pEntInfo->location );
		
		// clear this out - it must be set on a per-entity basis
		pSaveData->modelSpaceOffset.Init();

		if ( pent && (pEntInfo->flags & levelMask) )		// Screen out the player if he's not to be spawned
		{
			if ( pEntInfo->flags & FENTTABLE_GLOBAL )
			{
				DevMsg( 2, "Merging changes for global: %s\n", STRING(pEntInfo->classname) );
			
				// -------------------------------------------------------------------------
				// Pass the "global" flag to the DLL to indicate this entity should only override
				// a matching entity, not be spawned
				if ( g_EntitySaveRestoreBlockHandler.RestoreGlobalEntity( pent, pSaveData, pEntInfo ) > 0 )
				{
					movedCount++;
					pEntInfo->restoreentityindex = EntityFromHandle(pEntInfo->hEnt)->entindex();
					AddRestoredEntity(EntityFromHandle(pEntInfo->hEnt) );
				}
				else
				{
					UTIL_RemoveImmediate(EntityFromHandle(pEntInfo->hEnt) );
				}
				// -------------------------------------------------------------------------
			}
			else 
			{
				DevMsg( 2, "Transferring %s (%d)\n", STRING(pEntInfo->classname), pent->entindex());
				CRestoreServer restoreHelper( pSaveData );
				if ( g_EntitySaveRestoreBlockHandler.RestoreEntity( pent, &restoreHelper, pEntInfo ) < 0 )
				{
					UTIL_RemoveImmediate( pent );
				}
				else
				{
					// needs to be checked.  Do this in a separate pass so that pointers & hierarchy can be traversed
					checkList.AddToTail(i);
				}
			}

			// Remove any entities that were removed using UTIL_Remove() as a result of the above calls to UTIL_RemoveImmediate()
			gEntList.CleanupDeleteList();
		}
	}

	for ( i = checkList.Count()-1; i >= 0; --i )
	{
		pEntInfo = pSaveData->GetEntityInfo( checkList[i] );
		pent = EntityFromHandle(pEntInfo->hEnt);

		// NOTE: pent can be NULL because UTIL_RemoveImmediate (called below) removes all in hierarchy
		if ( !pent )
			continue;

		MDLCACHE_CRITICAL_SECTION();

		if ( !(pEntInfo->flags & FENTTABLE_PLAYER) && UTIL_EntityInSolid( pent ) )
		{
			// this can happen during normal processing - PVS is just a guess, some map areas won't exist in the new map
			DevMsg( 2, "Suppressing %s\n", STRING(pEntInfo->classname) );
			UTIL_RemoveImmediate( pent );
			// Remove any entities that were removed using UTIL_Remove() as a result of the above calls to UTIL_RemoveImmediate()
			gEntList.CleanupDeleteList();
		}
		else
		{
			movedCount++;
			pEntInfo->flags = FENTTABLE_REMOVED;
			pEntInfo->restoreentityindex = pent->entindex();
			AddRestoredEntity( pent );
		}
	}

	return movedCount;
}
#endif
