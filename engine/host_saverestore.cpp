//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Save game read and write. Any *.hl? files may be stored in memory, so use
//			g_pSaveRestoreFileSystem when accessing them. The .sav file is always stored
//			on disk, so use g_pFileSystem when accessing it.
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
// Save / Restore System

#include <ctype.h>
#ifdef _WIN32
#include "winerror.h"
#endif
#include "client.h"
#include "server.h"
#include "vengineserver_impl.h"
#include "host_cmd.h"
#include "sys.h"
#include "cdll_int.h"
#include "tmessage.h"
#include "screen.h"
#include "decal.h"
#include "zone.h"
#include "sv_main.h"
#include "host.h"
#include "r_local.h"
#include "filesystem.h"
#include "filesystem_engine.h"
#include "host_state.h"
#include "datamap.h"
#include "string_t.h"
#include "PlayerState.h"
#include "saverestoretypes.h"
#include "demo.h"
#include "icliententity.h"
#include "r_efx.h"
#include "icliententitylist.h"
#include "cdll_int.h"
#include "utldict.h"
#include "decal_private.h"
#include "engine/IEngineTrace.h"
#include "enginetrace.h"
#include "baseautocompletefilelist.h"
#include "sound.h"
#include "vgui_baseui_interface.h"
#include "gl_matsysiface.h"
#include "cl_main.h"
//#include "pr_edict.h"
#include "tier0/vprof.h"
#include <vgui/ILocalize.h>
#include "vgui_controls/Controls.h"
#include "tier0/icommandline.h"
#include "testscriptmgr.h"
#include "vengineserver_impl.h"
#include "saverestore_filesystem.h"
#include "tier1/callqueue.h"
#include "vstdlib/jobthread.h"
#include "enginebugreporter.h"
#include "tier1/memstack.h"
#include "vstdlib/jobthread.h"
#include "ModelInfo.h"

#if !defined( _X360 )
#include "xbox/xboxstubs.h"
#else
#include "xbox/xbox_launch.h"
#endif

#include "ixboxsystem.h"
extern IXboxSystem *g_pXboxSystem;

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern IBaseClientDLL *g_ClientDLL;

extern ConVar	deathmatch;
extern ConVar	skill;
extern ConVar	save_in_memory;
extern CGlobalVars g_ServerGlobalVariables;

extern CNetworkStringTableContainer *networkStringTableContainerServer;
extern void SaveGlobalState( CSaveRestoreData *pSaveData );
extern void RestoreGlobalState( CSaveRestoreData *pSaveData );

// Keep the last 1 autosave / quick saves
ConVar save_history_count("save_history_count", "1", 0, "Keep this many old copies in history of autosaves and quicksaves." );
ConVar sv_autosave( "sv_autosave", "1", 0, "Set to 1 to autosave game on level transition. Does not affect autosave triggers." );
ConVar save_async( "save_async", "1" );
ConVar save_disable( "save_disable", "0" );
ConVar save_noxsave( "save_noxsave", "0" );

ConVar save_screenshot( "save_screenshot", "1", 0, "0 = none, 1 = non-autosave, 2 = always" );

ConVar save_spew( "save_spew", "0" );
ConVar g_debug_transitions("g_debug_transitions", "0", FCVAR_NONE, "Set to 1 and restart the map to be warned if the map has no trigger_transition volumes. Set to 2 to see a dump of all entities & associated results during a transition.");


#define SaveMsg if ( !save_spew.GetBool() ) ; else Msg

// HACK HACK:  Some hacking to keep the .sav file backward compatible on the client!!!
#define SECTION_MAGIC_NUMBER	0x54541234
#define SECTION_VERSION_NUMBER	2
#define MAX_ENTITYARRAY 1024
#define ZERO_TIME ((FLT_MAX*-0.5))
#define TICK_NEVER_THINK		(-1)
// A bit arbitrary, but unlikely to collide with any saved games...
#define TICK_NEVER_THINK_ENCODE	( INT_MAX - 3 )

CCallQueue g_AsyncSaveCallQueue;
static bool g_ConsoleInput = false;

static char g_szMapLoadOverride[32];

#define MOD_DIR ( IsX360() ? "DEFAULT_WRITE_PATH" : "MOD" )

//-----------------------------------------------------------------------------

IThreadPool *g_pSaveThread;

static bool g_bSaveInProgress = false;

BEGIN_SIMPLE_DATADESC(entitytable_t)
	DEFINE_FIELD(id, FIELD_INTEGER),
	DEFINE_FIELD(edictindex, FIELD_INTEGER),
	DEFINE_FIELD(saveentityindex, FIELD_INTEGER),
//	DEFINE_FIELD( restoreentityindex, FIELD_INTEGER ),
	//				hEnt		(not saved, this is the fixup)
	DEFINE_FIELD(location, FIELD_INTEGER),
	DEFINE_FIELD(size, FIELD_INTEGER),
	DEFINE_FIELD(flags, FIELD_INTEGER),
	DEFINE_FIELD(classname, FIELD_STRING),
	DEFINE_FIELD(globalname, FIELD_STRING),
	DEFINE_FIELD(landmarkModelSpace, FIELD_VECTOR),
	DEFINE_FIELD(modelname, FIELD_STRING),
END_DATADESC()

static int gSizes[FIELD_TYPECOUNT] =
{
	FIELD_SIZE(FIELD_VOID),
	FIELD_SIZE(FIELD_FLOAT),
	FIELD_SIZE(FIELD_STRING),
	FIELD_SIZE(FIELD_VECTOR),
	FIELD_SIZE(FIELD_QUATERNION),
	FIELD_SIZE(FIELD_INTEGER),
	FIELD_SIZE(FIELD_BOOLEAN),
	FIELD_SIZE(FIELD_SHORT),
	FIELD_SIZE(FIELD_CHARACTER),
	FIELD_SIZE(FIELD_COLOR32),
	FIELD_SIZE(FIELD_EMBEDDED),
	FIELD_SIZE(FIELD_CUSTOM),

	FIELD_SIZE(FIELD_CLASSPTR),
	FIELD_SIZE(FIELD_EHANDLE),
	FIELD_SIZE(FIELD_EDICT),

	FIELD_SIZE(FIELD_POSITION_VECTOR),
	FIELD_SIZE(FIELD_TIME),
	FIELD_SIZE(FIELD_TICK),
	FIELD_SIZE(FIELD_MODELNAME),
	FIELD_SIZE(FIELD_SOUNDNAME),

	FIELD_SIZE(FIELD_INPUT),
	FIELD_SIZE(FIELD_FUNCTION),
	FIELD_SIZE(FIELD_VMATRIX),
	FIELD_SIZE(FIELD_VMATRIX_WORLDSPACE),
	FIELD_SIZE(FIELD_MATRIX3X4_WORLDSPACE),
	FIELD_SIZE(FIELD_INTERVAL),
	FIELD_SIZE(FIELD_MODELINDEX),
	FIELD_SIZE(FIELD_MATERIALINDEX),

	FIELD_SIZE(FIELD_VECTOR2D),
	FIELD_SIZE(FIELD_INTEGER64),
	FIELD_SIZE(FIELD_POINTER),
};

// helpers to offset worldspace matrices
static void VMatrixOffset(VMatrix & dest, const VMatrix & matrixIn, const Vector & offset)
{
	dest = matrixIn;
	dest.PostTranslate(offset);
}

static void Matrix3x4Offset(matrix3x4_t & dest, const matrix3x4_t & matrixIn, const Vector & offset)
{
	MatrixCopy(matrixIn, dest);
	Vector out;
	MatrixGetColumn(matrixIn, 3, out);
	out += offset;
	MatrixSetColumn(out, 3, dest);
}

// This does the necessary casting / extract to grab a pointer to a member function as a void *
// UNDONE: Cast to BASEPTR or something else here?
//#define EXTRACT_INPUTFUNC_FUNCTIONPTR(x)		(*(inputfunc_t **)(&(x)))

//-----------------------------------------------------------------------------
//
// CSave
//
//-----------------------------------------------------------------------------

CSave::CSave(CSaveRestoreData * pdata)
	: m_pData(pdata),
	m_pGameInfo(pdata),
	m_bAsync(pdata->bAsync)
{
	m_BlockStartStack.EnsureCapacity(32);

	// Logging.
	m_hLogFile = NULL;
}

CSaveServer::CSaveServer(CSaveRestoreData * pdata)
	:CSave(pdata)
{

}

CSaveClient::CSaveClient(CSaveRestoreData * pdata)
	:CSave(pdata)
{

}


//-------------------------------------

inline int CSave::DataEmpty(const char* pdata, int size)
{
	static int void_data = 0;
	if (size != 4)
	{
		const char* pLimit = pdata + size;
		while (pdata < pLimit)
		{
			if (*pdata++)
				return 0;
		}
		return 1;
	}

	return memcmp(pdata, &void_data, sizeof(int)) == 0;
}

//-----------------------------------------------------------------------------
// Purpose: Start logging save data.
//-----------------------------------------------------------------------------
void CSave::StartLogging(const char* pszLogName)
{
	m_hLogFile = g_pFileSystem->Open(pszLogName, "w");
}

//-----------------------------------------------------------------------------
// Purpose: Stop logging save data.
//-----------------------------------------------------------------------------
void CSave::EndLogging(void)
{
	if (m_hLogFile)
	{
		g_pFileSystem->Close(m_hLogFile);
	}
	m_hLogFile = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Check to see if we are logging data.
//-----------------------------------------------------------------------------
bool CSave::IsLogging(void)
{
	return (m_hLogFile != NULL);
}

//-----------------------------------------------------------------------------
// Purpose: Log data.
//-----------------------------------------------------------------------------
void CSave::Log(const char* pName, fieldtype_t fieldType, void* value, int count)
{
	// Check to see if we are logging.
	if (!IsLogging())
		return;

	static char szBuf[1024];
	static char szTempBuf[256];

	// Save the name.
	Q_snprintf(szBuf, sizeof(szBuf), "%s ", pName);

	for (int iCount = 0; iCount < count; ++iCount)
	{
		switch (fieldType)
		{
		case FIELD_SHORT:
		{
			short* pValue = (short*)(value);
			short nValue = pValue[iCount];
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "%d", nValue);
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
			break;
		}
		case FIELD_FLOAT:
		{
			float* pValue = (float*)(value);
			float flValue = pValue[iCount];
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "%f", flValue);
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
			break;
		}
		case FIELD_BOOLEAN:
		{
			bool* pValue = (bool*)(value);
			bool bValue = pValue[iCount];
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "%d", (int)(bValue));
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
			break;
		}
		case FIELD_INTEGER:
		{
			int* pValue = (int*)(value);
			int nValue = pValue[iCount];
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "%d", nValue);
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
			break;
		}
		case FIELD_STRING:
		{
			string_t* pValue = (string_t*)(value);
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "%s", (char*)STRING(*pValue));
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
			break;
		}
		case FIELD_VECTOR:
		{
			Vector* pValue = (Vector*)(value);
			Vector vecValue = pValue[iCount];
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "(%f %f %f)", vecValue.x, vecValue.y, vecValue.z);
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
			break;
		}
		case FIELD_QUATERNION:
		{
			Quaternion* pValue = (Quaternion*)(value);
			Quaternion q = pValue[iCount];
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "(%f %f %f %f)", q[0], q[1], q[2], q[3]);
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
			break;
		}
		case FIELD_CHARACTER:
		{
			char* pValue = (char*)(value);
			char chValue = pValue[iCount];
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "%c", chValue);
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
		}
		case FIELD_COLOR32:
		{
			byte* pValue = (byte*)(value);
			byte* pColor = &pValue[iCount * 4];
			Q_snprintf(szTempBuf, sizeof(szTempBuf), "(%d %d %d %d)", (int)pColor[0], (int)pColor[1], (int)pColor[2], (int)pColor[3]);
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
		}
		case FIELD_EMBEDDED:
		case FIELD_CUSTOM:
		default:
		{
			break;
		}
		}

		// Add space data.
		if ((iCount + 1) != count)
		{
			Q_strncpy(szTempBuf, " ", sizeof(szTempBuf));
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
		}
		else
		{
			Q_strncpy(szTempBuf, "\n", sizeof(szTempBuf));
			Q_strncat(szBuf, szTempBuf, sizeof(szTempBuf), COPY_ALL_CHARACTERS);
		}
	}

	int nLength = strlen(szBuf) + 1;
	g_pFileSystem->Write(szBuf, nLength, m_hLogFile);
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

void CSave::WriteShort(const short* value, int count)
{
	BufferData((const char*)value, sizeof(short) * count);
}

//-------------------------------------

void CSave::WriteInt(const int* value, int count)
{
	BufferData((const char*)value, sizeof(int) * count);
}

//-------------------------------------

void CSave::WriteBool(const bool* value, int count)
{
	COMPILE_TIME_ASSERT(sizeof(bool) == sizeof(char));
	BufferData((const char*)value, sizeof(bool) * count);
}

//-------------------------------------

void CSave::WriteFloat(const float* value, int count)
{
	BufferData((const char*)value, sizeof(float) * count);
}

//-------------------------------------

void CSave::WriteData(const char* pdata, int size)
{
	BufferData(pdata, size);
}

//-------------------------------------

void CSave::WriteString(const char* pstring)
{
	BufferData(pstring, strlen(pstring) + 1);
}

//-------------------------------------

void CSave::WriteString(const string_t * stringId, int count)
{
	for (int i = 0; i < count; i++)
	{
		const char* pString = STRING(stringId[i]);
		BufferData(pString, strlen(pString) + 1);
	}
}

//-------------------------------------

void CSave::WriteVector(const Vector & value)
{
	BufferData((const char*)&value, sizeof(Vector));
}

//-------------------------------------

void CSave::WriteVector(const Vector * value, int count)
{
	BufferData((const char*)value, sizeof(Vector) * count);
}

void CSave::WriteQuaternion(const Quaternion & value)
{
	BufferData((const char*)&value, sizeof(Quaternion));
}

//-------------------------------------

void CSave::WriteQuaternion(const Quaternion * value, int count)
{
	BufferData((const char*)value, sizeof(Quaternion) * count);
}


//-------------------------------------

void CSave::WriteData(const char* pname, int size, const char* pdata)
{
	BufferField(pname, size, pdata);
}

//-------------------------------------

void CSave::WriteShort(const char* pname, const short* data, int count)
{
	BufferField(pname, sizeof(short) * count, (const char*)data);
}

//-------------------------------------

void CSave::WriteInt(const char* pname, const int* data, int count)
{
	BufferField(pname, sizeof(int) * count, (const char*)data);
}

//-------------------------------------

void CSave::WriteBool(const char* pname, const bool* data, int count)
{
	COMPILE_TIME_ASSERT(sizeof(bool) == sizeof(char));
	BufferField(pname, sizeof(bool) * count, (const char*)data);
}

//-------------------------------------

void CSave::WriteFloat(const char* pname, const float* data, int count)
{
	BufferField(pname, sizeof(float) * count, (const char*)data);
}

//-------------------------------------

void CSave::WriteString(const char* pname, const char* pdata)
{
	BufferField(pname, strlen(pdata) + 1, pdata);
}

//-------------------------------------

void CSave::WriteString(const char* pname, const string_t * stringId, int count)
{
	int i, size;

	size = 0;
	for (i = 0; i < count; i++)
		size += strlen(STRING(stringId[i])) + 1;

	WriteHeader(pname, size);
	WriteString(stringId, count);
}

//-------------------------------------

void CSave::WriteVector(const char* pname, const Vector & value)
{
	WriteVector(pname, &value, 1);
}

//-------------------------------------

void CSave::WriteVector(const char* pname, const Vector * value, int count)
{
	WriteHeader(pname, sizeof(Vector) * count);
	BufferData((const char*)value, sizeof(Vector) * count);
}

void CSave::WriteQuaternion(const char* pname, const Quaternion & value)
{
	WriteQuaternion(pname, &value, 1);
}

//-------------------------------------

void CSave::WriteQuaternion(const char* pname, const Quaternion * value, int count)
{
	WriteHeader(pname, sizeof(Quaternion) * count);
	BufferData((const char*)value, sizeof(Quaternion) * count);
}


//-------------------------------------

void CSave::WriteVMatrix(const VMatrix * value, int count)
{
	BufferData((const char*)value, sizeof(VMatrix) * count);
}

//-------------------------------------

void CSave::WriteVMatrix(const char* pname, const VMatrix * value, int count)
{
	WriteHeader(pname, sizeof(VMatrix) * count);
	BufferData((const char*)value, sizeof(VMatrix) * count);
}

//-------------------------------------

void CSave::WriteVMatrixWorldspace(const VMatrix * value, int count)
{
	for (int i = 0; i < count; i++)
	{
		VMatrix tmp;
		VMatrixOffset(tmp, value[i], -m_pGameInfo->GetLandmark());
		BufferData((const char*)&tmp, sizeof(VMatrix));
	}
}

//-------------------------------------

void CSave::WriteVMatrixWorldspace(const char* pname, const VMatrix * value, int count)
{
	WriteHeader(pname, sizeof(VMatrix) * count);
	WriteVMatrixWorldspace(value, count);
}

void CSave::WriteMatrix3x4Worldspace(const matrix3x4_t * value, int count)
{
	Vector offset = -m_pGameInfo->GetLandmark();
	for (int i = 0; i < count; i++)
	{
		matrix3x4_t tmp;
		Matrix3x4Offset(tmp, value[i], offset);
		BufferData((const char*)value, sizeof(matrix3x4_t));
	}
}

//-------------------------------------

void CSave::WriteMatrix3x4Worldspace(const char* pname, const matrix3x4_t * value, int count)
{
	WriteHeader(pname, sizeof(matrix3x4_t) * count);
	WriteMatrix3x4Worldspace(value, count);
}

void CSave::WriteInterval(const char* pname, const interval_t * value, int count)
{
	WriteHeader(pname, sizeof(interval_t) * count);
	WriteInterval(value, count);
}

void CSave::WriteInterval(const interval_t * value, int count)
{
	BufferData((const char*)value, count * sizeof(interval_t));
}

//-------------------------------------

bool CSave::ShouldSaveField(const void* pData, typedescription_t * pField)
{
	if (!(pField->flags & FTYPEDESC_SAVE) || pField->fieldType == FIELD_VOID)
		return false;

	switch (pField->fieldType)
	{
	case FIELD_EMBEDDED:
	{
		if (pField->flags & FTYPEDESC_PTR)
		{
			AssertMsg(pField->fieldSize == 1, "Arrays of embedded pointer types presently unsupported by save/restore");
			if (pField->fieldSize != 1)
				return false;
		}

		AssertMsg(pField->td != NULL, "Embedded type appears to have not had type description implemented");
		if (pField->td == NULL)
			return false;

		if ((pField->flags & FTYPEDESC_PTR) && !*((void**)pData))
			return false;

		// @TODO: need real logic for handling embedded types with base classes
		if (pField->td->baseMap)
		{
			return true;
		}

		int nFieldCount = pField->fieldSize;
		char* pTestData = (char*)((!(pField->flags & FTYPEDESC_PTR)) ? pData : *((void**)pData));
		while (--nFieldCount >= 0)
		{
			typedescription_t* pTestField = pField->td->dataDesc;
			typedescription_t* pLimit = pField->td->dataDesc + pField->td->dataNumFields;

			for (; pTestField < pLimit; ++pTestField)
			{
				if (ShouldSaveField(pTestData + pTestField->fieldOffset[TD_OFFSET_NORMAL], pTestField))
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
			const_cast<void*>(pData),
			((char*)pData) - pField->fieldOffset[TD_OFFSET_NORMAL],
			pField
		};
		if (pField->pSaveRestoreOps->IsEmpty(fieldInfo))
			return false;
	}
	return true;

	case FIELD_EHANDLE:
	{
		if ((pField->fieldSizeInBytes != pField->fieldSize * gSizes[pField->fieldType]))
		{
			Warning("WARNING! Field %s is using the wrong FIELD_ type!\nFix this or you'll see a crash.\n", pField->fieldName);
			Assert(0);
		}

		int* pEHandle = (int*)pData;
		for (int i = 0; i < pField->fieldSize; ++i, ++pEHandle)
		{
			if ((*pEHandle) != INVALID_EHANDLE_INDEX)
				return true;
		}
	}
	return false;

	default:
	{
		if ((pField->fieldSizeInBytes != pField->fieldSize * gSizes[pField->fieldType]))
		{
			Warning("WARNING! Field %s is using the wrong FIELD_ type!\nFix this or you'll see a crash.\n", pField->fieldName);
			Assert(0);
		}

		// old byte-by-byte null check
		if (DataEmpty((const char*)pData, pField->fieldSize * gSizes[pField->fieldType]))
			return false;
	}
	return true;
	}
}

//-------------------------------------
// Purpose:	Writes all the fields that are client neutral. In the event of 
//			a librarization of save/restore, these would reside in the library
//

bool CSave::WriteBasicField(const char* pname, void* pData, datamap_t * pRootMap, typedescription_t * pField)
{
	switch (pField->fieldType)
	{
	case FIELD_FLOAT:
		WriteFloat(pField->fieldName, (float*)pData, pField->fieldSize);
		break;

	case FIELD_STRING:
		WriteString(pField->fieldName, (string_t*)pData, pField->fieldSize);
		break;

	case FIELD_VECTOR:
		WriteVector(pField->fieldName, (Vector*)pData, pField->fieldSize);
		break;

	case FIELD_QUATERNION:
		WriteQuaternion(pField->fieldName, (Quaternion*)pData, pField->fieldSize);
		break;

	case FIELD_INTEGER:
		WriteInt(pField->fieldName, (int*)pData, pField->fieldSize);
		break;

	case FIELD_BOOLEAN:
		WriteBool(pField->fieldName, (bool*)pData, pField->fieldSize);
		break;

	case FIELD_SHORT:
		WriteData(pField->fieldName, 2 * pField->fieldSize, ((char*)pData));
		break;

	case FIELD_CHARACTER:
		WriteData(pField->fieldName, pField->fieldSize, ((char*)pData));
		break;

	case FIELD_COLOR32:
		WriteData(pField->fieldName, 4 * pField->fieldSize, (char*)pData);
		break;

	case FIELD_EMBEDDED:
	{
		AssertMsg(((pField->flags & FTYPEDESC_PTR) == 0) || (pField->fieldSize == 1), "Arrays of embedded pointer types presently unsupported by save/restore");
		Assert(!(pField->flags & FTYPEDESC_PTR) || *((void**)pData));
		int nFieldCount = pField->fieldSize;
		char* pFieldData = (char*)((!(pField->flags & FTYPEDESC_PTR)) ? pData : *((void**)pData));

		StartBlock(pField->fieldName);

		while (--nFieldCount >= 0)
		{
			WriteAll(pFieldData, pField->td);
			pFieldData += pField->fieldSizeInBytes;
		}

		EndBlock();
		break;
	}

	case FIELD_CUSTOM:
	{
		// Note it is up to the custom type implementor to handle arrays
		StartBlock(pField->fieldName);

		SaveRestoreFieldInfo_t fieldInfo =
		{
			pData,
			((char*)pData) - pField->fieldOffset[TD_OFFSET_NORMAL],
			pField
		};
		pField->pSaveRestoreOps->Save(fieldInfo, this);

		EndBlock();
		break;
	}

	default:
		Warning("Bad field type\n");
		Assert(0);
		return false;
	}

	return true;
}

//-------------------------------------

bool CSave::WriteField(const char* pname, void* pData, datamap_t * pRootMap, typedescription_t * pField)
{
#ifdef _DEBUG
	Log(pname, (fieldtype_t)pField->fieldType, pData, pField->fieldSize);
#endif

	if (pField->fieldType <= FIELD_CUSTOM)
	{
		return WriteBasicField(pname, pData, pRootMap, pField);
	}
	return WriteGameField(pname, pData, pRootMap, pField);
}

//-------------------------------------
int	CSave::WriteRootFields(const char* pname, IHandleEntity* pHandleEntity, datamap_t* pRootMap, typedescription_t* pFields, int fieldCount) {
	typedescription_t* pTest;
	int iHeaderPos = m_pData->GetCurPos();
	int count = -1;
	WriteInt(pname, &count, 1);

	count = 0;

#ifdef _X360
	__dcbt(0, pBaseData);
	__dcbt(128, pBaseData);
	__dcbt(256, pBaseData);
	__dcbt(512, pBaseData);
	void* pDest = m_pData->AccessCurPos();
	__dcbt(0, pDest);
	__dcbt(128, pDest);
	__dcbt(256, pDest);
	__dcbt(512, pDest);
#endif

	IEngineObject* pEngineObject = GetEngineObject(pHandleEntity->GetRefEHandle().GetEntryIndex());
	datamap_t* pDataMaps[100];
	int nDataMapCount = 0;
	datamap_t* pDataMap = pEngineObject->GetDataDescMap();
	while (pDataMap) {
		pDataMaps[nDataMapCount] = pDataMap;
		if (nDataMapCount++ >= 100) {
			Error("too much level");
		}
		pDataMap = pDataMap->baseMap;
	}
	if (nDataMapCount > 0) {
		for (int i = nDataMapCount - 1; i >= 0; i--) {
			datamap_t* pCurMap = pDataMaps[i];
			for (int i = 0; i < pCurMap->dataNumFields; i++) {
				pTest = &pCurMap->dataDesc[i];
				void* pOutputData = ((char*)pEngineObject + pTest->fieldOffset[TD_OFFSET_NORMAL]);

				if (!ShouldSaveField(pOutputData, pTest))
					continue;

				if (!WriteField(pname, pOutputData, pRootMap, pTest))
					break;
				count++;
			}
		}
	}


	for (int i = 0; i < fieldCount; i++)
	{
		pTest = &pFields[i];
		void* pOutputData = ((char*)pHandleEntity + pTest->fieldOffset[TD_OFFSET_NORMAL]);

		if (!ShouldSaveField(pOutputData, pTest))
			continue;

		if (!WriteField(pname, pOutputData, pRootMap, pTest))
			break;
		count++;
	}

	int iCurPos = m_pData->GetCurPos();
	int iRewind = iCurPos - iHeaderPos;
	m_pData->Rewind(iRewind);
	WriteInt(pname, &count, 1);
	iCurPos = m_pData->GetCurPos();
	m_pData->MoveCurPos(iRewind - (iCurPos - iHeaderPos));

	return 1;
}

int CSave::WriteFields(const char* pname, const void* pBaseData, datamap_t * pRootMap, typedescription_t * pFields, int fieldCount)
{
	typedescription_t* pTest;
	int iHeaderPos = m_pData->GetCurPos();
	int count = -1;
	WriteInt(pname, &count, 1);

	count = 0;

#ifdef _X360
	__dcbt(0, pBaseData);
	__dcbt(128, pBaseData);
	__dcbt(256, pBaseData);
	__dcbt(512, pBaseData);
	void* pDest = m_pData->AccessCurPos();
	__dcbt(0, pDest);
	__dcbt(128, pDest);
	__dcbt(256, pDest);
	__dcbt(512, pDest);
#endif

	for (int i = 0; i < fieldCount; i++)
	{
		pTest = &pFields[i];
		void* pOutputData = ((char*)pBaseData + pTest->fieldOffset[TD_OFFSET_NORMAL]);

		if (!ShouldSaveField(pOutputData, pTest))
			continue;

		if (!WriteField(pname, pOutputData, pRootMap, pTest))
			break;
		count++;
	}

	int iCurPos = m_pData->GetCurPos();
	int iRewind = iCurPos - iHeaderPos;
	m_pData->Rewind(iRewind);
	WriteInt(pname, &count, 1);
	iCurPos = m_pData->GetCurPos();
	m_pData->MoveCurPos(iRewind - (iCurPos - iHeaderPos));

	return 1;
}

int CSave::WriteEntityInfo(entitytable_t* pEntityInfo) {
	return WriteFields("ETABLE", pEntityInfo, NULL, entitytable_t::m_DataMap.dataDesc, entitytable_t::m_DataMap.dataNumFields);
}

int	CSave::WriteEntity(IHandleEntity* pHandleEntity) {

	datamap_t* pDataMaps[100];
	int nDataMapCount = 0;
	datamap_t* pDataMap = pHandleEntity->GetDataDescMap();
	while (pDataMap) {
		pDataMaps[nDataMapCount] = pDataMap;
		if (nDataMapCount++ >= 100) {
			Error("too much level");
		}
		pDataMap = pDataMap->baseMap;
	}
	if (nDataMapCount > 0) {
		for (int i = nDataMapCount - 1; i >= 0; i--) {
			datamap_t* pCurMap = pDataMaps[i];
			if (i == nDataMapCount - 1) {
				WriteRootFields(pCurMap->dataClassName, pHandleEntity, pHandleEntity->GetDataDescMap(), pCurMap->dataDesc, pCurMap->dataNumFields);
			}
			else {
				WriteFields(pCurMap->dataClassName, pHandleEntity, pHandleEntity->GetDataDescMap(), pCurMap->dataDesc, pCurMap->dataNumFields);
			}
		}
	}

	return 1;
}
//-------------------------------------
// Purpose: Recursively saves all the classes in an object, in reverse order (top down)
// Output : int 0 on failure, 1 on success

int CSave::DoWriteAll(const void* pLeafObject, datamap_t * pLeafMap, datamap_t * pCurMap)
{
	// save base classes first
	if (pCurMap->baseMap)
	{
		int status = DoWriteAll(pLeafObject, pLeafMap, pCurMap->baseMap);
		if (!status)
			return status;
	}

	return WriteFields(pCurMap->dataClassName, pLeafObject, pLeafMap, pCurMap->dataDesc, pCurMap->dataNumFields);
}

//-------------------------------------

void CSave::StartBlock(const char* pszBlockName)
{
	WriteHeader(pszBlockName, 0); // placeholder
	m_BlockStartStack.AddToTail(GetWritePos());
}

//-------------------------------------

void CSave::StartBlock()
{
	StartBlock("");
}

//-------------------------------------

void CSave::EndBlock()
{
	int endPos = GetWritePos();
	int startPos = m_BlockStartStack[m_BlockStartStack.Count() - 1];
	short sizeBlock = endPos - startPos;

	m_BlockStartStack.Remove(m_BlockStartStack.Count() - 1);

	// Move to the the location where the size of the block was written & rewrite the size
	SetWritePos(startPos - sizeof(SaveRestoreRecordHeader_t));
	BufferData((const char*)&sizeBlock, sizeof(short));

	SetWritePos(endPos);
}

//-------------------------------------

void CSave::BufferString(char* pdata, int len)
{
	char c = 0;

	BufferData(pdata, len);		// Write the string
	BufferData(&c, 1);			// Write a null terminator
}

//-------------------------------------

void CSave::BufferField(const char* pname, int size, const char* pdata)
{
	WriteHeader(pname, size);
	BufferData(pdata, size);
}

//-------------------------------------

void CSave::WriteHeader(const char* pname, int size)
{
	short shortSize = size;
	short hashvalue = m_pData->FindCreateSymbol(pname);
	if (size > SHRT_MAX || size < 0)
	{
		Warning("CSave::WriteHeader() size parameter exceeds 'short'!\n");
		Assert(0);
	}

	BufferData((const char*)&shortSize, sizeof(short));
	BufferData((const char*)&hashvalue, sizeof(short));
}

//-------------------------------------

void CSave::BufferData(const char* pdata, int size)
{
	if (!m_pData)
		return;

	if (!m_pData->Write(pdata, size))
	{
		Warning("Save/Restore overflow!\n");
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

int	CSave::EntityIndex(const IHandleEntity * pEntity)
{
	return m_pGameInfo->GetEntityIndex(pEntity);
}

//-------------------------------------

int	CSave::EntityFlagsSet(int entityIndex, int flags)
{
	if (!m_pGameInfo || entityIndex < 0)
		return 0;
	if (entityIndex > m_pGameInfo->NumEntities())
		return 0;

	m_pGameInfo->GetEntityInfo(entityIndex)->flags |= flags;

	return m_pGameInfo->GetEntityInfo(entityIndex)->flags;
}

//-------------------------------------

void CSave::WriteTime(const char* pname, const float* data, int count)
{
	int i;
	float tmp;

	WriteHeader(pname, sizeof(float) * count);
	for (i = 0; i < count; i++)
	{
		// Always encode time as a delta from the current time so it can be re-based if loaded in a new level
		// Times of 0 are never written to the file, so they will be restored as 0, not a relative time
		Assert(data[i] != ZERO_TIME);

		if (data[i] == 0.0)
		{
			tmp = ZERO_TIME;
		}
		else if (data[i] == INVALID_TIME || data[i] == FLT_MAX)
		{
			tmp = data[i];
		}
		else
		{
			tmp = data[i] - m_pGameInfo->GetBaseTime();
			if (fabsf(tmp) < 0.001) // never allow a time to become zero due to rebasing
				tmp = 0.001;
		}

		WriteData((const char*)&tmp, sizeof(float));
	}
}

//-------------------------------------

void CSave::WriteTime(const float* data, int count)
{
	int i;
	float tmp;

	for (i = 0; i < count; i++)
	{
		// Always encode time as a delta from the current time so it can be re-based if loaded in a new level
		// Times of 0 are never written to the file, so they will be restored as 0, not a relative time
		if (data[i] == 0.0)
		{
			tmp = ZERO_TIME;
		}
		else if (data[i] == INVALID_TIME || data[i] == FLT_MAX)
		{
			tmp = data[i];
		}
		else
		{
			tmp = data[i] - m_pGameInfo->GetBaseTime();
			if (fabsf(tmp) < 0.001) // never allow a time to become zero due to rebasing
				tmp = 0.001;
		}

		WriteData((const char*)&tmp, sizeof(float));
	}
}

void CSave::WriteTick(const char* pname, const int* data, int count)
{
	WriteHeader(pname, sizeof(int) * count);
	WriteTick(data, count);
}

//-------------------------------------

void CSave::WriteTick(const int* data, int count)
{
	int i;
	int tmp;

	int baseTick = TIME_TO_TICKS(m_pGameInfo->GetBaseTime());

	for (i = 0; i < count; i++)
	{
		// Always encode time as a delta from the current time so it can be re-based if loaded in a new level
		// Times of 0 are never written to the file, so they will be restored as 0, not a relative time
		tmp = data[i];
		if (data[i] == TICK_NEVER_THINK)
		{
			tmp = TICK_NEVER_THINK_ENCODE;
		}
		else
		{
			// Rebase it...
			tmp -= baseTick;
		}
		WriteData((const char*)&tmp, sizeof(int));
	}
}
//-------------------------------------

void CSave::WritePositionVector(const char* pname, const Vector & value)
{
	Vector tmp = value;

	if (tmp != vec3_invalid)
		tmp -= m_pGameInfo->GetLandmark();

	WriteVector(pname, tmp);
}

//-------------------------------------

void CSave::WritePositionVector(const Vector & value)
{
	Vector tmp = value;

	if (tmp != vec3_invalid)
		tmp -= m_pGameInfo->GetLandmark();

	WriteVector(tmp);
}

//-------------------------------------

void CSave::WritePositionVector(const char* pname, const Vector * value, int count)
{
	WriteHeader(pname, sizeof(Vector) * count);
	WritePositionVector(value, count);
}

//-------------------------------------

void CSave::WritePositionVector(const Vector * value, int count)
{
	for (int i = 0; i < count; i++)
	{
		Vector tmp = value[i];

		if (tmp != vec3_invalid)
			tmp -= m_pGameInfo->GetLandmark();

		WriteData((const char*)&tmp.x, sizeof(Vector));
	}
}

//-------------------------------------

void CSave::WriteFunction(datamap_t * pRootMap, const char* pname, inputfunc_t * *data, int count)
{
	AssertMsg(count == 1, "Arrays of functions not presently supported");
	const char* functionName = pRootMap->UTIL_FunctionToName(*(inputfunc_t*)data);
	if (!functionName)
	{
		Warning("Invalid function pointer in entity!\n");
		Assert(0);
		functionName = "BADFUNCTIONPOINTER";
	}

	BufferField(pname, strlen(functionName) + 1, functionName);
}

//-------------------------------------

void CSave::WriteEntityPtr(const char* pname, IHandleEntity * *ppEntity, int count)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];
	for (int i = 0; i < count && i < MAX_ENTITYARRAY; i++)
	{
		entityArray[i] = EntityIndex(ppEntity[i]);
	}
	WriteInt(pname, entityArray, count);
}

//-------------------------------------

void CSave::WriteEntityPtr(IHandleEntity * *ppEntity, int count)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];
	for (int i = 0; i < count && i < MAX_ENTITYARRAY; i++)
	{
		entityArray[i] = EntityIndex(ppEntity[i]);
	}
	WriteInt(entityArray, count);
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

void CSaveServer::WriteEHandle(const char* pname, const CBaseHandle * pEHandle, int count)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];
	for (int i = 0; i < count && i < MAX_ENTITYARRAY; i++)
	{
		IHandleEntity* pHandleEntity = serverEntitylist->GetServerEntityFromHandle(const_cast<CBaseHandle*>(pEHandle)[i]);
		entityArray[i] = EntityIndex(pHandleEntity);
	}
	WriteInt(pname, entityArray, count);
}

void CSaveClient::WriteEHandle(const char* pname, const CBaseHandle * pEHandle, int count)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];
	for (int i = 0; i < count && i < MAX_ENTITYARRAY; i++)
	{
		IHandleEntity* pHandleEntity = entitylist->GetClientEntityFromHandle(const_cast<CBaseHandle*>(pEHandle)[i]);
		entityArray[i] = EntityIndex(pHandleEntity);
	}
	WriteInt(pname, entityArray, count);
}

//-------------------------------------
void CSaveServer::WriteEHandle(const CBaseHandle * pEHandle, int count)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];
	for (int i = 0; i < count && i < MAX_ENTITYARRAY; i++)
	{
		IHandleEntity* pHandleEntity = serverEntitylist->GetServerEntityFromHandle(const_cast<CBaseHandle*>(pEHandle)[i]);
		entityArray[i] = EntityIndex(pHandleEntity);
	}
	WriteInt(entityArray, count);
}

void CSaveClient::WriteEHandle(const CBaseHandle * pEHandle, int count)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];
	for (int i = 0; i < count && i < MAX_ENTITYARRAY; i++)
	{
		IHandleEntity* pHandleEntity = entitylist->GetClientEntityFromHandle(const_cast<CBaseHandle*>(pEHandle)[i]);
		entityArray[i] = EntityIndex(pHandleEntity);
	}
	WriteInt(entityArray, count);
}

const model_t* CSaveServer::GetModel(int modelindex) {
	return modelinfo->GetModel(modelindex);
}

const model_t* CSaveClient::GetModel(int modelindex) {
	return modelinfoclient->GetModel(modelindex);
}

const char* CSaveServer::GetModelName(const model_t * model) const {
	return modelinfo->GetModelName(model);
}

const char* CSaveClient::GetModelName(const model_t * model) const {
	return modelinfoclient->GetModelName(model);
}

const char* CSaveServer::GetMaterialNameFromIndex(int nMateralIndex) {
	return serverGameDLL->GetMaterialNameFromIndex(nMateralIndex);
}

const char* CSaveClient::GetMaterialNameFromIndex(int nMateralIndex) {
	return g_ClientDLL->GetMaterialNameFromIndex(nMateralIndex);
}

string_t CSaveServer::AllocPooledString(const char* pszValue) {
	return serverGameDLL->AllocPooledString(pszValue);
}

string_t CSaveClient::AllocPooledString(const char* pszValue) {
	return g_ClientDLL->AllocPooledString(pszValue);
}

IEngineObject* CSaveServer::GetEngineObject(int entnum) {
	return serverEntitylist->GetEngineObject(entnum);
}

IEngineObject* CSaveClient::GetEngineObject(int entnum) {
	return entitylist->GetEngineObject(entnum);
}

//-------------------------------------
// Purpose:	Writes all the fields that are not client neutral. In the event of 
//			a librarization of save/restore, these would not reside in the library

bool CSave::WriteGameField(const char* pname, void* pData, datamap_t * pRootMap, typedescription_t * pField)
{
	switch (pField->fieldType)
	{
	case FIELD_CLASSPTR:
		WriteEntityPtr(pField->fieldName, (IHandleEntity**)pData, pField->fieldSize);
		break;

	case FIELD_EDICT:
		//WriteEdictPtr( pField->fieldName, (edict_t **)pData, pField->fieldSize );
		Error("WriteEdictPtr has been removed!");
		break;

	case FIELD_EHANDLE:
		WriteEHandle(pField->fieldName, (CBaseHandle*)pData, pField->fieldSize);
		break;

	case FIELD_POSITION_VECTOR:
		WritePositionVector(pField->fieldName, (Vector*)pData, pField->fieldSize);
		break;

	case FIELD_TIME:
		WriteTime(pField->fieldName, (float*)pData, pField->fieldSize);
		break;

	case FIELD_TICK:
		WriteTick(pField->fieldName, (int*)pData, pField->fieldSize);
		break;

	case FIELD_MODELINDEX:
	{
		int nModelIndex = *(int*)pData;
		string_t strModelName = NULL_STRING;
		const model_t* pModel = GetModel(nModelIndex);
		if (pModel)
		{
			strModelName = AllocPooledString(GetModelName(pModel));
		}
		WriteString(pField->fieldName, (string_t*)&strModelName, pField->fieldSize);
	}
	break;

	case FIELD_MATERIALINDEX:
	{
		int nMateralIndex = *(int*)pData;
		string_t strMaterialName = NULL_STRING;
		const char* pMaterialName = GetMaterialNameFromIndex(nMateralIndex);
		if (pMaterialName)
		{
			strMaterialName = MAKE_STRING(pMaterialName);
		}
		WriteString(pField->fieldName, (string_t*)&strMaterialName, pField->fieldSize);
	}
	break;

	case FIELD_MODELNAME:
	case FIELD_SOUNDNAME:
		WriteString(pField->fieldName, (string_t*)pData, pField->fieldSize);
		break;

		// For now, just write the address out, we're not going to change memory while doing this yet!
	case FIELD_FUNCTION:
		WriteFunction(pRootMap, pField->fieldName, (inputfunc_t**)(char*)pData, pField->fieldSize);
		break;

	case FIELD_VMATRIX:
		WriteVMatrix(pField->fieldName, (VMatrix*)pData, pField->fieldSize);
		break;
	case FIELD_VMATRIX_WORLDSPACE:
		WriteVMatrixWorldspace(pField->fieldName, (VMatrix*)pData, pField->fieldSize);
		break;

	case FIELD_MATRIX3X4_WORLDSPACE:
		WriteMatrix3x4Worldspace(pField->fieldName, (const matrix3x4_t*)pData, pField->fieldSize);
		break;

	case FIELD_INTERVAL:
		WriteInterval(pField->fieldName, (interval_t*)pData, pField->fieldSize);
		break;

	case FIELD_POINTER:
		WriteData(pField->fieldName, sizeof(void*) * pField->fieldSize, (char*)pData);
		break;

	default:
		Warning("Bad field type\n");
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

CRestore::CRestore(CSaveRestoreData * pdata)
	: m_pData(pdata),
	m_pGameInfo(pdata),
	m_global(0),
	m_precache(true)
{
	m_BlockEndStack.EnsureCapacity(32);
}

CRestoreServer::CRestoreServer(CSaveRestoreData * pdata)
	:CRestore(pdata)
{

}

CRestoreClient::CRestoreClient(CSaveRestoreData * pdata)
	:CRestore(pdata)
{

}

//-------------------------------------

int CRestore::GetReadPos() const
{
	return m_pData->GetCurPos();
}

//-------------------------------------

void CRestore::SetReadPos(int pos)
{
	m_pData->Seek(pos);
}

//-------------------------------------

const char* CRestore::StringFromHeaderSymbol(int symbol)
{
	const char* pszResult = m_pData->StringFromSymbol(symbol);
	return (pszResult) ? pszResult : "";
}

//-------------------------------------
// Purpose:	Reads all the fields that are client neutral. In the event of 
//			a librarization of save/restore, these would reside in the library

void CRestore::ReadBasicField(const SaveRestoreRecordHeader_t & header, void* pDest, datamap_t * pRootMap, typedescription_t * pField)
{
	switch (pField->fieldType)
	{
	case FIELD_FLOAT:
	{
		ReadFloat((float*)pDest, pField->fieldSize, header.size);
		break;
	}
	case FIELD_STRING:
	{
		ReadString((string_t*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_VECTOR:
	{
		ReadVector((Vector*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_QUATERNION:
	{
		ReadQuaternion((Quaternion*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_INTEGER:
	{
		ReadInt((int*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_BOOLEAN:
	{
		ReadBool((bool*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_SHORT:
	{
		ReadShort((short*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_CHARACTER:
	{
		ReadData((char*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_COLOR32:
	{
		COMPILE_TIME_ASSERT(sizeof(color32) == sizeof(int));
		ReadInt((int*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_EMBEDDED:
	{
		AssertMsg(((pField->flags & FTYPEDESC_PTR) == 0) || (pField->fieldSize == 1), "Arrays of embedded pointer types presently unsupported by save/restore");
#ifdef DBGFLAG_ASSERT
		int startPos = GetReadPos();
#endif
		if (!(pField->flags & FTYPEDESC_PTR) || *((void**)pDest))
		{
			int nFieldCount = pField->fieldSize;
			char* pFieldData = (char*)((!(pField->flags & FTYPEDESC_PTR)) ? pDest : *((void**)pDest));
			while (--nFieldCount >= 0)
			{
				// No corresponding "block" (see write) as it was used as the header of the field
				ReadAll(pFieldData, pField->td);
				pFieldData += pField->fieldSizeInBytes;
			}
			Assert(GetReadPos() - startPos == header.size);
		}
		else
		{
			SetReadPos(GetReadPos() + header.size);
			Warning("Attempted to restore FIELD_EMBEDDEDBYREF %s but there is no destination memory\n", pField->fieldName);
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
			((char*)pDest) - pField->fieldOffset[TD_OFFSET_NORMAL],
			pField
		};

		pField->pSaveRestoreOps->Restore(fieldInfo, this);

		Assert(posNextField >= GetReadPos());
		SetReadPos(posNextField);
		break;
	}

	default:
		Warning("Bad field type\n");
		Assert(0);
	}
}

//-------------------------------------

void CRestore::ReadField(const SaveRestoreRecordHeader_t & header, void* pDest, datamap_t * pRootMap, typedescription_t * pField)
{
	if (pField->fieldType <= FIELD_CUSTOM)
		ReadBasicField(header, pDest, pRootMap, pField);
	else
		ReadGameField(header, pDest, pRootMap, pField);
}

//-------------------------------------

bool CRestore::ShouldReadField(typedescription_t * pField)
{
	if ((pField->flags & FTYPEDESC_SAVE) == 0)
		return false;

	if (m_global && (pField->flags & FTYPEDESC_GLOBAL))
		return false;

	return true;
}

//-------------------------------------

typedescription_t* CRestore::FindField(const char* pszFieldName, typedescription_t * pFields, int fieldCount, int* pCookie)
{
	int& fieldNumber = *pCookie;
	if (fieldNumber >= fieldCount) {
		fieldNumber = 0;
	}
	if (pszFieldName)
	{
		typedescription_t* pTest;

		for (int i = 0; i < fieldCount; i++)
		{
			pTest = &pFields[fieldNumber];

			++fieldNumber;
			if (fieldNumber >= fieldCount)
				fieldNumber = 0;

			if (stricmp(pTest->fieldName, pszFieldName) == 0)
				return pTest;
		}
	}

	fieldNumber = 0;
	return NULL;
}

//-------------------------------------

bool CRestore::ShouldEmptyField(typedescription_t * pField)
{
	// don't clear out fields that don't get saved, or that are handled specially
	if (!(pField->flags & FTYPEDESC_SAVE))
		return false;

	// Don't clear global fields
	if (m_global && (pField->flags & FTYPEDESC_GLOBAL))
		return false;

	return true;
}

//-------------------------------------

void CRestore::EmptyFields(void* pBaseData, typedescription_t * pFields, int fieldCount)
{
	int i;
	for (i = 0; i < fieldCount; i++)
	{
		typedescription_t* pField = &pFields[i];
		if (!ShouldEmptyField(pField))
			continue;

		void* pFieldData = (char*)pBaseData + pField->fieldOffset[TD_OFFSET_NORMAL];
		switch (pField->fieldType)
		{
		case FIELD_CUSTOM:
		{
			SaveRestoreFieldInfo_t fieldInfo =
			{
				pFieldData,
				pBaseData,
				pField
			};
			pField->pSaveRestoreOps->MakeEmpty(fieldInfo);
		}
		break;

		case FIELD_EMBEDDED:
		{
			if ((pField->flags & FTYPEDESC_PTR) && !*((void**)pFieldData))
				break;

			int nFieldCount = pField->fieldSize;
			char* pFieldMemory = (char*)((!(pField->flags & FTYPEDESC_PTR)) ? pFieldData : *((void**)pFieldData));
			while (--nFieldCount >= 0)
			{
				EmptyFields(pFieldMemory, pField->td->dataDesc, pField->td->dataNumFields);
				pFieldMemory += pField->fieldSizeInBytes;
			}
		}
		break;

		default:
			// NOTE: If you hit this assertion, you've got a bug where you're using 
			// the wrong field type for your field
			if (pField->fieldSizeInBytes != pField->fieldSize * gSizes[pField->fieldType])
			{
				Warning("WARNING! Field %s is using the wrong FIELD_ type!\nFix this or you'll see a crash.\n", pField->fieldName);
				Assert(0);
			}
			memset(pFieldData, (pField->fieldType != FIELD_EHANDLE) ? 0 : 0xFF, pField->fieldSize * gSizes[pField->fieldType]);
			break;
		}
	}
}

//-------------------------------------

void CRestore::StartBlock(SaveRestoreRecordHeader_t * pHeader)
{
	ReadHeader(pHeader);
	m_BlockEndStack.AddToTail(GetReadPos() + pHeader->size);
}

//-------------------------------------

void CRestore::StartBlock(char szBlockName[])
{
	SaveRestoreRecordHeader_t header;
	StartBlock(&header);
	Q_strncpy(szBlockName, StringFromHeaderSymbol(header.symbol), SIZE_BLOCK_NAME_BUF);
}

//-------------------------------------

void CRestore::StartBlock()
{
	char szBlockName[SIZE_BLOCK_NAME_BUF];
	StartBlock(szBlockName);
}

//-------------------------------------

void CRestore::EndBlock()
{
	int endPos = m_BlockEndStack[m_BlockEndStack.Count() - 1];
	m_BlockEndStack.Remove(m_BlockEndStack.Count() - 1);
	SetReadPos(endPos);
}

//-------------------------------------

int CRestore::ReadRootFields(const char* pname, IHandleEntity* pHandleEntity, datamap_t* pRootMap, typedescription_t* pFields, int fieldCount)
{
	static int lastName = -1;
	Verify(ReadShort() == sizeof(int));			// First entry should be an int
	int symName = m_pData->FindCreateSymbol(pname);

	// Check the struct name
	int curSym = ReadShort();
	if (curSym != symName)			// Field Set marker
	{
		const char* pLastName = m_pData->StringFromSymbol(lastName);
		const char* pCurName = m_pData->StringFromSymbol(curSym);
		Msg("Expected %s found %s ( raw '%s' )! (prev: %s)\n", pname, pCurName, BufferPointer(), pLastName);
		Msg("Field type name may have changed or inheritance graph changed, save file is suspect\n");
		m_pData->Rewind(2 * sizeof(short));
		return 0;
	}
	lastName = symName;

	IEngineObject* pEngineObject = GetEngineObject(pHandleEntity->GetRefEHandle().GetEntryIndex());
	datamap_t* pDataMaps[100];
	int nDataMapCount = 0;
	datamap_t* pDataMap = pEngineObject->GetDataDescMap();
	while (pDataMap) {
		pDataMaps[nDataMapCount] = pDataMap;
		if (nDataMapCount++ >= 100) {
			Error("too much level");
		}
		pDataMap = pDataMap->baseMap;
	}
	if (nDataMapCount > 0) {
		for (int i = nDataMapCount - 1; i >= 0; i--) {
			datamap_t* pCurMap = pDataMaps[i];
			// Clear out base data
			EmptyFields(pEngineObject, pCurMap->dataDesc, pCurMap->dataNumFields);
		}
	}
	EmptyFields(pHandleEntity, pFields, fieldCount);

	// Skip over the struct name
	int i;
	int nFieldsSaved = ReadInt();						// Read field count
	int searchCookie = 0;								// Make searches faster, most data is read/written in the same order
	SaveRestoreRecordHeader_t header;

	for (i = 0; i < nFieldsSaved; i++)
	{
		ReadHeader(&header);

		const char* pFieldName = m_pData->StringFromSymbol(header.symbol);
		typedescription_t* pField = NULL;
		if (nDataMapCount > 0) {
			for (int i = nDataMapCount - 1; i >= 0; i--) {
				datamap_t* pCurMap = pDataMaps[i];
				// Clear out base data
				pField = FindField(pFieldName, pCurMap->dataDesc, pCurMap->dataNumFields, &searchCookie);
				if (pField) {
					break;
				}
			}
		}
		if (pField) {
			if (ShouldReadField(pField)) {
				ReadField(header, ((char*)pEngineObject + pField->fieldOffset[TD_OFFSET_NORMAL]), pRootMap, pField);
			}
			else {
				BufferSkipBytes(header.size);			// Advance to next field
			}
		}
		else {
			pField = FindField(pFieldName, pFields, fieldCount, &searchCookie);
			if (pField && ShouldReadField(pField))
			{
				ReadField(header, ((char*)pHandleEntity + pField->fieldOffset[TD_OFFSET_NORMAL]), pRootMap, pField);
			}
			else
			{
				BufferSkipBytes(header.size);			// Advance to next field
			}
		}
	}

	return 1;
}

int CRestore::ReadFields(const char* pname, void* pBaseData, datamap_t * pRootMap, typedescription_t * pFields, int fieldCount)
{
	static int lastName = -1;
	Verify(ReadShort() == sizeof(int));			// First entry should be an int
	int symName = m_pData->FindCreateSymbol(pname);

	// Check the struct name
	int curSym = ReadShort();
	if (curSym != symName)			// Field Set marker
	{
		const char* pLastName = m_pData->StringFromSymbol(lastName);
		const char* pCurName = m_pData->StringFromSymbol(curSym);
		Msg("Expected %s found %s ( raw '%s' )! (prev: %s)\n", pname, pCurName, BufferPointer(), pLastName);
		Msg("Field type name may have changed or inheritance graph changed, save file is suspect\n");
		m_pData->Rewind(2 * sizeof(short));
		return 0;
	}
	lastName = symName;

	// Clear out base data
	EmptyFields(pBaseData, pFields, fieldCount);

	// Skip over the struct name
	int i;
	int nFieldsSaved = ReadInt();						// Read field count
	int searchCookie = 0;								// Make searches faster, most data is read/written in the same order
	SaveRestoreRecordHeader_t header;

	for (i = 0; i < nFieldsSaved; i++)
	{
		ReadHeader(&header);

		typedescription_t* pField = FindField(m_pData->StringFromSymbol(header.symbol), pFields, fieldCount, &searchCookie);
		if (pField && ShouldReadField(pField))
		{
			ReadField(header, ((char*)pBaseData + pField->fieldOffset[TD_OFFSET_NORMAL]), pRootMap, pField);
		}
		else
		{
			BufferSkipBytes(header.size);			// Advance to next field
		}
	}

	return 1;
}

//-------------------------------------

void CRestore::ReadHeader(SaveRestoreRecordHeader_t * pheader)
{
	if (pheader != NULL)
	{
		Assert(pheader != NULL);
		pheader->size = ReadShort();				// Read field size
		pheader->symbol = ReadShort();				// Read field name token
	}
	else
	{
		BufferSkipBytes(sizeof(short) * 2);
	}
}

//-------------------------------------

short CRestore::ReadShort(void)
{
	short tmp = 0;

	BufferReadBytes((char*)&tmp, sizeof(short));

	return tmp;
}

//-------------------------------------

int	CRestore::ReadInt(void)
{
	int tmp = 0;

	BufferReadBytes((char*)&tmp, sizeof(int));

	return tmp;
}

int CRestore::ReadEntityInfo(entitytable_t* pEntityInfo)
{
	return ReadFields("ETABLE", pEntityInfo, NULL, entitytable_t::m_DataMap.dataDesc, entitytable_t::m_DataMap.dataNumFields);
}

int CRestore::ReadEntity(IHandleEntity* pHandleEntity) {
	datamap_t* pDataMaps[100];
	int nDataMapCount = 0;
	datamap_t* pDataMap = pHandleEntity->GetDataDescMap();
	while (pDataMap) {
		pDataMaps[nDataMapCount] = pDataMap;
		if (nDataMapCount++ >= 100) {
			Error("too much level");
		}
		pDataMap = pDataMap->baseMap;
	}
	if (nDataMapCount > 0) {
		for (int i = nDataMapCount - 1; i >= 0; i--) {
			datamap_t* pCurMap = pDataMaps[i];
			if (i == nDataMapCount - 1) {
				ReadRootFields(pCurMap->dataClassName, pHandleEntity, pHandleEntity->GetDataDescMap(), pCurMap->dataDesc, pCurMap->dataNumFields);
			}
			else {
				ReadFields(pCurMap->dataClassName, pHandleEntity, pHandleEntity->GetDataDescMap(), pCurMap->dataDesc, pCurMap->dataNumFields);
			}
		}
	}

	return 1;
}
//-------------------------------------
// Purpose: Recursively restores all the classes in an object, in reverse order (top down)
// Output : int 0 on failure, 1 on success

int CRestore::DoReadAll(void* pLeafObject, datamap_t * pLeafMap, datamap_t * pCurMap)
{
	// restore base classes first
	if (pCurMap->baseMap)
	{
		int status = DoReadAll(pLeafObject, pLeafMap, pCurMap->baseMap);
		if (!status)
			return status;
	}

	return ReadFields(pCurMap->dataClassName, pLeafObject, pLeafMap, pCurMap->dataDesc, pCurMap->dataNumFields);
}

//-------------------------------------

char* CRestore::BufferPointer(void)
{
	if (!m_pData)
		return NULL;

	return m_pData->AccessCurPos();
}

//-------------------------------------

void CRestore::BufferReadBytes(char* pOutput, int size)
{
	Assert(m_pData != NULL);

	if (!m_pData || m_pData->BytesAvailable() == 0)
		return;

	if (!m_pData->Read(pOutput, size))
	{
		Warning("Restore underflow!\n");
		Assert(0);
	}
}

//-------------------------------------

void CRestore::BufferSkipBytes(int bytes)
{
	BufferReadBytes(NULL, bytes);
}

//-------------------------------------

int CRestore::ReadShort(short* pValue, int nElems, int nBytesAvailable)
{
	return ReadSimple(pValue, nElems, nBytesAvailable);
}

//-------------------------------------

int CRestore::ReadInt(int* pValue, int nElems, int nBytesAvailable)
{
	return ReadSimple(pValue, nElems, nBytesAvailable);
}

//-------------------------------------

int CRestore::ReadBool(bool* pValue, int nElems, int nBytesAvailable)
{
	COMPILE_TIME_ASSERT(sizeof(bool) == sizeof(char));
	return ReadSimple(pValue, nElems, nBytesAvailable);
}

//-------------------------------------

int CRestore::ReadFloat(float* pValue, int nElems, int nBytesAvailable)
{
	return ReadSimple(pValue, nElems, nBytesAvailable);
}

//-------------------------------------

int CRestore::ReadData(char* pData, int size, int nBytesAvailable)
{
	return ReadSimple(pData, size, nBytesAvailable);
}

//-------------------------------------

void CRestore::ReadString(char* pDest, int nSizeDest, int nBytesAvailable)
{
	const char* pString = BufferPointer();
	if (!nBytesAvailable)
		nBytesAvailable = strlen(pString) + 1;
	BufferSkipBytes(nBytesAvailable);

	Q_strncpy(pDest, pString, nSizeDest);
}

//-------------------------------------

int CRestore::ReadString(string_t * pValue, int nElems, int nBytesAvailable)
{
	AssertMsg(nBytesAvailable > 0, "CRestore::ReadString() implementation does not currently support unspecified bytes available");

	int i;
	char* pString = BufferPointer();
	char* pLimit = pString + nBytesAvailable;
	for (i = 0; i < nElems && pString < pLimit; i++)
	{
		if (*((char*)pString) == 0)
			pValue[i] = NULL_STRING;
		else
			pValue[i] = AllocPooledString((char*)pString);

		while (*pString)
			pString++;
		pString++;
	}

	BufferSkipBytes(nBytesAvailable);

	return i;
}

//-------------------------------------

int CRestore::ReadVector(Vector * pValue)
{
	BufferReadBytes((char*)pValue, sizeof(Vector));
	return 1;
}

//-------------------------------------

int CRestore::ReadVector(Vector * pValue, int nElems, int nBytesAvailable)
{
	return ReadSimple(pValue, nElems, nBytesAvailable);
}

int CRestore::ReadQuaternion(Quaternion * pValue)
{
	BufferReadBytes((char*)pValue, sizeof(Quaternion));
	return 1;
}

//-------------------------------------

int CRestore::ReadQuaternion(Quaternion * pValue, int nElems, int nBytesAvailable)
{
	return ReadSimple(pValue, nElems, nBytesAvailable);
}

//-------------------------------------
int CRestore::ReadVMatrix(VMatrix * pValue, int nElems, int nBytesAvailable)
{
	return ReadSimple(pValue, nElems, nBytesAvailable);
}


int CRestore::ReadVMatrixWorldspace(VMatrix * pValue, int nElems, int nBytesAvailable)
{
	Vector basePosition = m_pGameInfo->GetLandmark();
	VMatrix tmp;

	for (int i = 0; i < nElems; i++)
	{
		BufferReadBytes((char*)&tmp, sizeof(float) * 16);

		VMatrixOffset(pValue[i], tmp, basePosition);
	}
	return nElems;
}


int CRestore::ReadMatrix3x4Worldspace(matrix3x4_t * pValue, int nElems, int nBytesAvailable)
{
	Vector basePosition = m_pGameInfo->GetLandmark();
	matrix3x4_t tmp;

	for (int i = 0; i < nElems; i++)
	{
		BufferReadBytes((char*)&tmp, sizeof(matrix3x4_t));

		Matrix3x4Offset(pValue[i], tmp, basePosition);
	}
	return nElems;
}

int CRestore::ReadInterval(interval_t * interval, int count, int nBytesAvailable)
{
	return ReadSimple(interval, count, nBytesAvailable);
}

//---------------------------------------------------------
//
// Game centric restore methods
//

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
			return serverEntitylist->GetServerEntityFromHandle(pTable->hEnt);
	}
	return NULL;
}

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
			return entitylist->GetClientEntityFromHandle(pTable->hEnt);
	}
	return NULL;
}

IEngineObject* CRestoreServer::GetEngineObject(int entnum) {
	return serverEntitylist->GetEngineObject(entnum);
}

IEngineObject* CRestoreClient::GetEngineObject(int entnum) {
	return entitylist->GetEngineObject(entnum);
}
//-------------------------------------

int CRestore::ReadEntityPtr(IHandleEntity * *ppEntity, int count, int nBytesAvailable)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];

	int nRead = ReadInt(entityArray, count, nBytesAvailable);

	for (int i = 0; i < nRead; i++) // nRead is never greater than count
	{
		ppEntity[i] = EntityFromIndex(entityArray[i]);
	}

	if (nRead < count)
	{
		memset(&ppEntity[nRead], 0, (count - nRead) * sizeof(ppEntity[0]));
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

int CRestore::ReadEHandle(CBaseHandle * pEHandle, int count, int nBytesAvailable)
{
	AssertMsg(count <= MAX_ENTITYARRAY, "Array of entities or ehandles exceeds limit supported by save/restore");
	int entityArray[MAX_ENTITYARRAY];

	int nRead = ReadInt(entityArray, count, nBytesAvailable);

	for (int i = 0; i < nRead; i++) // nRead is never greater than count
	{
		pEHandle[i] = EntityFromIndex(entityArray[i]);
	}

	if (nRead < count)
	{
		memset(&pEHandle[nRead], 0xFF, (count - nRead) * sizeof(pEHandle[0]));
	}

	return nRead;
}

int	CRestoreServer::GetModelIndex(const char* name) {
	return modelinfo->GetModelIndex(name);
}

int	CRestoreClient::GetModelIndex(const char* name) {
	return modelinfoclient->GetModelIndex(name);
}

void CRestoreServer::PrecacheModel(const char* pModelName) {
	g_pVEngineServer->PrecacheModel(pModelName);
}

void CRestoreClient::PrecacheModel(const char* pModelName) {

}

int CRestoreServer::GetMaterialIndex(const char* pMaterialName) {
	return serverGameDLL->GetMaterialIndex(pMaterialName);
}

int CRestoreClient::GetMaterialIndex(const char* pMaterialName) {
	return g_ClientDLL->GetMaterialIndex(pMaterialName);
}

void CRestoreServer::PrecacheMaterial(const char* pMaterialName) {
	serverGameDLL->PrecacheMaterial(pMaterialName);
}

void CRestoreClient::PrecacheMaterial(const char* pMaterialName) {

}

void CRestoreServer::PrecacheScriptSound(const char* pSoundName) {
	g_pSoundEmitterSystem->PrecacheScriptSound(pSoundName);
}

void CRestoreClient::PrecacheScriptSound(const char* pMaterialName) {

}

void CRestoreServer::RenameMapName(string_t * pStringDest) {
	char buf[MAX_PATH];
	Q_strncpy(buf, "maps/", sizeof(buf));
	Q_strncat(buf, STRING( g_ServerGlobalVariables.mapname), sizeof(buf));
	Q_strncat(buf, ".bsp", sizeof(buf));
	*pStringDest = AllocPooledString(buf);
}

void CRestoreClient::RenameMapName(string_t * pStringDest) {

}

string_t CRestoreServer::AllocPooledString(const char* pszValue) {
	return serverGameDLL->AllocPooledString(pszValue);
}

string_t CRestoreClient::AllocPooledString(const char* pszValue) {
	return g_ClientDLL->AllocPooledString(pszValue);
}


//-------------------------------------
// Purpose:	Reads all the fields that are not client neutral. In the event of 
//			a librarization of save/restore, these would NOT reside in the library

void CRestore::ReadGameField(const SaveRestoreRecordHeader_t & header, void* pDest, datamap_t * pRootMap, typedescription_t * pField)
{
	switch (pField->fieldType)
	{
	case FIELD_POSITION_VECTOR:
	{
		ReadPositionVector((Vector*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_TIME:
	{
		ReadTime((float*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_TICK:
	{
		ReadTick((int*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_FUNCTION:
	{
		ReadFunction(pRootMap, (inputfunc_t**)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_MODELINDEX:
	{
		int* pModelIndex = (int*)pDest;
		string_t* pModelName = (string_t*)stackalloc(pField->fieldSize * sizeof(string_t));
		int nRead = ReadString(pModelName, pField->fieldSize, header.size);

		for (int i = 0; i < nRead; i++)
		{
			if (pModelName[i] == NULL_STRING)
			{
				pModelIndex[i] = -1;
				continue;
			}

			pModelIndex[i] = GetModelIndex(STRING(pModelName[i]));

			if (m_precache)
			{
				PrecacheModel(STRING(pModelName[i]));
			}
		}
		break;
	}

	case FIELD_MATERIALINDEX:
	{
		int* pMaterialIndex = (int*)pDest;
		string_t* pMaterialName = (string_t*)stackalloc(pField->fieldSize * sizeof(string_t));
		int nRead = ReadString(pMaterialName, pField->fieldSize, header.size);

		for (int i = 0; i < nRead; i++)
		{
			if (pMaterialName[i] == NULL_STRING)
			{
				pMaterialIndex[i] = 0;
				continue;
			}

			pMaterialIndex[i] = GetMaterialIndex(STRING(pMaterialName[i]));

			if (m_precache)
			{
				PrecacheMaterial(STRING(pMaterialName[i]));
			}
		}
		break;
	}

	case FIELD_MODELNAME:
	case FIELD_SOUNDNAME:
	{
		string_t* pStringDest = (string_t*)pDest;
		int nRead = ReadString(pStringDest, pField->fieldSize, header.size);
		if (m_precache)
		{
			// HACKHACK: Rewrite the .bsp models to match the map name in case the bugreporter renamed it
			if (pField->fieldType == FIELD_MODELNAME && Q_stristr(STRING(*pStringDest), ".bsp"))
			{
				RenameMapName(pStringDest);
			}
			for (int i = 0; i < nRead; i++)
			{
				if (pStringDest[i] != NULL_STRING)
				{
					if (pField->fieldType == FIELD_MODELNAME)
					{
						PrecacheModel(STRING(pStringDest[i]));
					}
					else if (pField->fieldType == FIELD_SOUNDNAME)
					{
						PrecacheScriptSound(STRING(pStringDest[i]));
					}
				}
			}
		}
		break;
	}

	case FIELD_CLASSPTR:
		ReadEntityPtr((IHandleEntity**)pDest, pField->fieldSize, header.size);
		break;

	case FIELD_EDICT:
		//ReadEdictPtr( (edict_t **)pDest, pField->fieldSize, header.size );
		Error("ReadEdictPtr has been removed!");
		break;
	case FIELD_EHANDLE:
		ReadEHandle((CBaseHandle*)pDest, pField->fieldSize, header.size);
		break;

	case FIELD_VMATRIX:
	{
		ReadVMatrix((VMatrix*)pDest, pField->fieldSize, header.size);
		break;
	}

	case FIELD_VMATRIX_WORLDSPACE:
		ReadVMatrixWorldspace((VMatrix*)pDest, pField->fieldSize, header.size);
		break;

	case FIELD_MATRIX3X4_WORLDSPACE:
		ReadMatrix3x4Worldspace((matrix3x4_t*)pDest, pField->fieldSize, header.size);
		break;

	case FIELD_INTERVAL:
		ReadInterval((interval_t*)pDest, pField->fieldSize, header.size);
		break;

	case FIELD_POINTER:
		ReadData((char*)pDest, sizeof(void*) * pField->fieldSize, header.size);
		break;

	default:
		Warning("Bad field type\n");
		Assert(0);
	}
}

//-------------------------------------

int CRestore::ReadTime(float* pValue, int count, int nBytesAvailable)
{
	float baseTime = m_pGameInfo->GetBaseTime();
	int nRead = ReadFloat(pValue, count, nBytesAvailable);

	for (int i = nRead - 1; i >= 0; i--)
	{
		if (pValue[i] == ZERO_TIME)
			pValue[i] = 0.0;
		else if (pValue[i] != INVALID_TIME && pValue[i] != FLT_MAX)
			pValue[i] += baseTime;
	}

	return nRead;
}

int CRestore::ReadTick(int* pValue, int count, int nBytesAvailable)
{
	// HACK HACK:  Adding 0.1f here makes sure that all tick times read
	//  from .sav file which are near the basetime will end up just ahead of
	//  the base time, because we are restoring we'll have a slow frame of the
	//  max frametime of 0.1 seconds and that could otherwise cause all of our
	//  think times to get synchronized to each other... sigh.  ywb...
	int baseTick = TIME_TO_TICKS(m_pGameInfo->GetBaseTime() + 0.1f);
	int nRead = ReadInt(pValue, count, nBytesAvailable);

	for (int i = nRead - 1; i >= 0; i--)
	{
		if (pValue[i] != TICK_NEVER_THINK_ENCODE)
		{
			// Rebase it
			pValue[i] += baseTick;
		}
		else
		{
			// Slam to -1 value
			pValue[i] = TICK_NEVER_THINK;
		}
	}

	return nRead;
}

//-------------------------------------

int CRestore::ReadPositionVector(Vector * pValue)
{
	return ReadPositionVector(pValue, 1, sizeof(Vector));
}

//-------------------------------------

int CRestore::ReadPositionVector(Vector * pValue, int count, int nBytesAvailable)
{
	Vector basePosition = m_pGameInfo->GetLandmark();
	int nRead = ReadVector(pValue, count, nBytesAvailable);

	for (int i = nRead - 1; i >= 0; i--)
	{
		if (pValue[i] != vec3_invalid)
			pValue[i] += basePosition;
	}

	return nRead;
}

//-------------------------------------

int CRestore::ReadFunction(datamap_t * pMap, inputfunc_t * *pValue, int count, int nBytesAvailable)
{
	AssertMsg(nBytesAvailable > 0, "CRestore::ReadFunction() implementation does not currently support unspecified bytes available");

	char* pszFunctionName = BufferPointer();
	BufferSkipBytes(nBytesAvailable);

	AssertMsg(count == 1, "Arrays of functions not presently supported");

	if (*pszFunctionName == 0)
		*pValue = NULL;
	else
	{
		inputfunc_t func = pMap->UTIL_FunctionFromName(pszFunctionName);
#ifdef GNUC
		Q_memcpy((void*)pValue, &func, sizeof(void*) * 2);
#else
		Q_memcpy((void*)pValue, &func, sizeof(void*));
#endif
	}
	return 0;
}

//-----------------------------------------------------------------------------
//
// ISaveRestoreBlockSet
//
// Purpose:	Serves as holder for a group of sibling save sections. Takes
//			care of iterating over them, making sure read points are
//			queued up to the right spot (in case one section due to datadesc
//			changes reads less than expected, or doesn't leave the
//			read pointer at the right point), and ensuring the read pointer
//			is at the end of the entire set when the set read is done.
//-----------------------------------------------------------------------------

struct SaveRestoreBlockHeader_t
{
	char szName[MAX_BLOCK_NAME_LEN + 1];
	int locHeader;
	int locBody;

	DECLARE_SIMPLE_DATADESC();
};


//-------------------------------------

class CSaveRestoreBlockSet : public ISaveRestoreBlockSet
{
public:
	CSaveRestoreBlockSet(const char* pszName)
	{
		Q_strncpy(m_Name, pszName, sizeof(m_Name));
	}

	const char* GetBlockName()
	{
		return m_Name;
	}

	//---------------------------------

	void PreSave(CSaveRestoreData* pData)
	{
		m_BlockHeaders.SetCount(m_Handlers.Count());
		for (int i = 0; i < m_Handlers.Count(); i++)
		{
			Q_strncpy(m_BlockHeaders[i].szName, m_Handlers[i]->GetBlockName(), MAX_BLOCK_NAME_LEN + 1);
			m_Handlers[i]->PreSave(pData);
		}
	}

	void Save(ISave* pSave)
	{
		int base = pSave->GetWritePos();
		for (int i = 0; i < m_Handlers.Count(); i++)
		{
			m_BlockHeaders[i].locBody = pSave->GetWritePos() - base;
			m_Handlers[i]->Save(pSave);
		}
		m_SizeBodies = pSave->GetWritePos() - base;
	}

	void WriteSaveHeaders(ISave* pSave)
	{
		int base = pSave->GetWritePos();

		//
		// Reserve space for a fully populated header
		//
		int dummyInt = -1;
		CUtlVector<SaveRestoreBlockHeader_t> dummyArr;

		dummyArr.SetCount(m_BlockHeaders.Count());
		memset(&dummyArr[0], 0xff, dummyArr.Count() * sizeof(SaveRestoreBlockHeader_t));

		pSave->WriteInt(&dummyInt); // size all headers
		pSave->WriteInt(&dummyInt); // size all bodies
		SaveUtlVector(pSave, &dummyArr, FIELD_EMBEDDED);

		//
		// Write the data
		//
		for (int i = 0; i < m_Handlers.Count(); i++)
		{
			m_BlockHeaders[i].locHeader = pSave->GetWritePos() - base;
			m_Handlers[i]->WriteSaveHeaders(pSave);
		}

		m_SizeHeaders = pSave->GetWritePos() - base;

		//
		// Write the actual header
		//
		int savedPos = pSave->GetWritePos();
		pSave->SetWritePos(base);

		pSave->WriteInt(&m_SizeHeaders);
		pSave->WriteInt(&m_SizeBodies);
		SaveUtlVector(pSave, &m_BlockHeaders, FIELD_EMBEDDED);

		pSave->SetWritePos(savedPos);
	}

	void PostSave()
	{
		for (int i = 0; i < m_Handlers.Count(); i++)
		{
			m_Handlers[i]->PostSave();
		}
		m_BlockHeaders.Purge();
	}

	//---------------------------------

	void PreRestore()
	{
		for (int i = 0; i < m_Handlers.Count(); i++)
		{
			m_Handlers[i]->PreRestore();
		}
	}

	void ReadRestoreHeaders(IRestore* pRestore)
	{
		int base = pRestore->GetReadPos();

		pRestore->ReadInt(&m_SizeHeaders);
		pRestore->ReadInt(&m_SizeBodies);
		RestoreUtlVector(pRestore, &m_BlockHeaders, FIELD_EMBEDDED);

		for (int i = 0; i < m_Handlers.Count(); i++)
		{
			int location = GetBlockHeaderLoc(m_Handlers[i]->GetBlockName());
			if (location != -1)
			{
				pRestore->SetReadPos(base + location);
				m_Handlers[i]->ReadRestoreHeaders(pRestore);
			}
		}

		pRestore->SetReadPos(base + m_SizeHeaders);
	}

	void CallBlockHandlerRestore(ISaveRestoreBlockHandler* pHandler, int baseFilePos, IRestore* pRestore, bool fCreatePlayers)
	{
		int location = GetBlockBodyLoc(pHandler->GetBlockName());
		if (location != -1)
		{
			pRestore->SetReadPos(baseFilePos + location);
			pHandler->Restore(pRestore, fCreatePlayers);
		}
	}

	void Restore(IRestore* pRestore, bool fCreatePlayers)
	{
		int base = pRestore->GetReadPos();

		for (int i = 0; i < m_Handlers.Count(); i++)
		{
			CallBlockHandlerRestore(m_Handlers[i], base, pRestore, fCreatePlayers);
		}
		pRestore->SetReadPos(base + m_SizeBodies);
	}

	void PostRestore()
	{
		for (int i = 0; i < m_Handlers.Count(); i++)
		{
			m_Handlers[i]->PostRestore();
		}
		m_BlockHeaders.Purge();
	}

	//---------------------------------

	void AddBlockHandler(ISaveRestoreBlockHandler* pHandler)
	{
		// Grody, but... while this class is still isolated in saverestore.cpp, this seems like a fine time to assert:
		//AssertMsg(pHandler == &g_EntitySaveRestoreBlockHandler || (m_Handlers.Count() >= 1 && m_Handlers[0] == &g_EntitySaveRestoreBlockHandler), "Expected entity save load to always be first");

		Assert(pHandler != this);
		m_Handlers.AddToTail(pHandler);
	}

	void RemoveBlockHandler(ISaveRestoreBlockHandler* pHandler)
	{
		m_Handlers.FindAndRemove(pHandler);
	}

	//---------------------------------

private:
	int GetBlockBodyLoc(const char* pszName)
	{
		for (int i = 0; i < m_BlockHeaders.Count(); i++)
		{
			if (strcmp(m_BlockHeaders[i].szName, pszName) == 0)
				return m_BlockHeaders[i].locBody;
		}
		return -1;
	}

	int GetBlockHeaderLoc(const char* pszName)
	{
		for (int i = 0; i < m_BlockHeaders.Count(); i++)
		{
			if (strcmp(m_BlockHeaders[i].szName, pszName) == 0)
				return m_BlockHeaders[i].locHeader;
		}
		return -1;
	}

	char 								   m_Name[MAX_BLOCK_NAME_LEN + 1];
	CUtlVector<ISaveRestoreBlockHandler*> m_Handlers;

	int									   m_SizeHeaders;
	int									   m_SizeBodies;
	CUtlVector<SaveRestoreBlockHeader_t>   m_BlockHeaders;
};

//-------------------------------------

BEGIN_SIMPLE_DATADESC(SaveRestoreBlockHeader_t)
	DEFINE_ARRAY(szName, FIELD_CHARACTER, MAX_BLOCK_NAME_LEN + 1),
	DEFINE_FIELD(locHeader, FIELD_INTEGER),
	DEFINE_FIELD(locBody, FIELD_INTEGER),
END_DATADESC()

//-------------------------------------

CSaveRestoreBlockSet g_ServerSaveRestoreBlockSet("Game");
ISaveRestoreBlockSet* g_pServerGameSaveRestoreBlockSet = &g_ServerSaveRestoreBlockSet;

CSaveRestoreBlockSet g_ClientSaveRestoreBlockSet("Game");
ISaveRestoreBlockSet* g_pClientGameSaveRestoreBlockSet = &g_ClientSaveRestoreBlockSet;


//-----------------------------------------------------------------------------
static bool HaveExactMap( const char *pszMapName )
{
	char szCanonName[64] = { 0 };
	V_strncpy( szCanonName, pszMapName, sizeof( szCanonName ) );
	IVEngineServer::eFindMapResult eResult = g_pVEngineServer->FindMap( szCanonName, sizeof( szCanonName ) );

	switch ( eResult )
	{
	case IVEngineServer::eFindMap_Found:
		return true;
	case IVEngineServer::eFindMap_NonCanonical:
	case IVEngineServer::eFindMap_NotFound:
	case IVEngineServer::eFindMap_FuzzyMatch:
	case IVEngineServer::eFindMap_PossiblyAvailable:
		return false;
	}

	AssertMsg( false, "Unhandled engine->FindMap return value\n" );
	return false;
}

void FinishAsyncSave()
{
	LOCAL_THREAD_LOCK();
	SaveMsg( "FinishAsyncSave() (%d/%d)\n", ThreadInMainThread(), ThreadGetCurrentId() );
	if ( g_AsyncSaveCallQueue.Count() )
	{
		g_AsyncSaveCallQueue.CallQueued();
		g_pFileSystem->AsyncFinishAllWrites();
	}
	g_bSaveInProgress = false;
}

void DispatchAsyncSave()
{
	Assert( !g_bSaveInProgress );
	g_bSaveInProgress = true;

	if ( save_async.GetBool() )
	{
		g_pSaveThread->QueueCall( &FinishAsyncSave );
	}
	else
	{
		FinishAsyncSave();
	}
}

//-----------------------------------------------------------------------------

inline void GetServerSaveCommentEx( char *comment, int maxlength, float flMinutes, float flSeconds )
{
	if ( g_iServerGameDLLVersion >= 5 )
	{
		serverGameDLL->GetSaveComment( comment, maxlength, flMinutes, flSeconds );
	}
	else
	{
		Assert( 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Alloc/free memory for save games
// Input  : num - 
//			size - 
//-----------------------------------------------------------------------------
class CSaveMemory : public CMemoryStack
{
public:
	CSaveMemory()
	{
		MEM_ALLOC_CREDIT();
		Init( 32*1024*1024, 64, 2*1024*1024 + 192*1024 );
	}

	int m_nSaveAllocs;
};

CSaveMemory &GetSaveMemory()
{
	static CSaveMemory g_SaveMemory;
	return g_SaveMemory;
}

void *SaveAllocMemory( size_t num, size_t size, bool bClear )
{
	MEM_ALLOC_CREDIT();
	++GetSaveMemory().m_nSaveAllocs;
	size_t nBytes = num * size;
	return GetSaveMemory().Alloc( nBytes, bClear );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSaveMem - 
//-----------------------------------------------------------------------------
void SaveFreeMemory( void *pSaveMem )
{
	--GetSaveMemory().m_nSaveAllocs;
	if ( !GetSaveMemory().m_nSaveAllocs )
	{
		GetSaveMemory().FreeAll( false );
	}
}

//-----------------------------------------------------------------------------
// Reset save memory stack, as some failed save/load paths will leak
//-----------------------------------------------------------------------------
void SaveResetMemory()
{
	GetSaveMemory().m_nSaveAllocs = 0;
	GetSaveMemory().FreeAll( false );
}


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
struct GAME_HEADER
{
	DECLARE_SIMPLE_DATADESC();

	char	mapName[32];
	char	comment[80];
	int		mapCount;		// the number of map state files in the save file.  This is usually number of maps * 3 (.hl1, .hl2, .hl3 files)
	char	originMapName[32];
	char	landmark[256];
};

struct SAVE_HEADER 
{
	DECLARE_SIMPLE_DATADESC();

	int		saveId;
	int		version;
	int		skillLevel;
	int		connectionCount;
	int		lightStyleCount;
	int		mapVersion;
	float	time__USE_VCR_MODE; // This is renamed to include the __USE_VCR_MODE prefix due to a #define on win32 from the VCR mode changes
								// The actual save games have the string "time__USE_VCR_MODE" in them
	char	mapName[32];
	char	skyName[32];
};

struct SAVELIGHTSTYLE 
{
	DECLARE_SIMPLE_DATADESC();

	int		index;
	char	style[64];
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CSaveRestore : public ISaveRestore
{
public:
	CSaveRestore()
	{
		m_bClearSaveDir = false;
		m_szSaveGameScreenshotFile[0] = 0;
		SetMostRecentElapsedMinutes( 0 );
		SetMostRecentElapsedSeconds( 0 );
		m_szMostRecentSaveLoadGame[0] = 0;
		m_szSaveGameName[ 0 ] = 0;
		m_bIsXSave = IsX360();
	}

	void					Init( void );
	void					Shutdown( void );
	void					OnFrameRendered();
	virtual bool			SaveFileExists( const char *pName );
	bool					LoadGame( const char *pName );
	char					*GetSaveDir(void);
	void					ClearSaveDir( void );
	void					DoClearSaveDir( bool bIsXSave );
	void					RequestClearSaveDir( void );
	int						LoadGameState( char const *level, bool createPlayers );
	void					LoadAdjacentEnts( const char *pOldLevel, const char *pLandmarkName );
	const char				*FindRecentSave( char *pNameBuf, int nameBufLen );
	void					ForgetRecentSave( void );
	int						SaveGameSlot( const char *pSaveName, const char *pSaveComment, bool onlyThisLevel, bool bSetMostRecent);//, const char *pszDestMap = NULL, const char *pszLandmark = NULL 
	bool					SaveGameState( bool bTransition, CSaveRestoreData ** = NULL, bool bOpenContainer = true, bool bIsAutosaveOrDangerous = false );
	void					RestoreClientState( char const *fileName, bool adjacent );
	void					RestoreAdjacenClientState( char const *map );
	int						IsValidSave( void );
	void					Finish( CSaveRestoreData *save );
	void					ClearRestoredIndexTranslationTables();
	void					OnFinishedClientRestore();
	void					AutoSaveDangerousIsSafe();
	virtual void			UpdateSaveGameScreenshots();
	virtual char const		*GetMostRecentlyLoadedFileName();
	virtual char const		*GetSaveFileName();

	virtual void			SetIsXSave( bool bIsXSave ) { m_bIsXSave = bIsXSave; }
	virtual bool			IsXSave() { return ( m_bIsXSave && !save_noxsave.GetBool() ); }

	virtual void			FinishAsyncSave() { ::FinishAsyncSave(); }

	void					AddDeferredCommand( char const *pchCommand );
	virtual bool			StorageDeviceValid( void );

	virtual bool			IsSaveInProgress();

private:
	bool					SaveClientState( const char *name );

	void					EntityPatchWrite( CSaveRestoreData *pSaveData, const char *level, bool bAsync = false );
	void					EntityPatchRead( CSaveRestoreData *pSaveData, const char *level );
	void					DirectoryCount( const char *pPath, int *pResult );
	void					DirectoryCopy( const char *pPath, const char *pDestFileName, bool bIsXSave );
	bool					DirectoryExtract( FileHandle_t pFile, int mapCount );
	void					DirectoryClear( const char *pPath );

	void					AgeSaveList( const char *pName, int count, bool bIsXSave );
	void					AgeSaveFile( const char *pName, const char *ext, int count, bool bIsXSave );
	int						SaveReadHeader( FileHandle_t pFile, GAME_HEADER *pHeader, int readGlobalState, bool *pbOldSave );
	CSaveRestoreData		*LoadSaveData( const char *level );
	void					ParseSaveTables( CSaveRestoreData *pSaveData, SAVE_HEADER *pHeader, int updateGlobals );
	int						FileSize( FileHandle_t pFile );

	bool					CalcSaveGameName( const char *pName, char *output, int outputStringLength );

	//CSaveRestoreData *		SaveGameStateInit( void );
	void 					SaveGameStateGlobals( CSaveRestoreData *pSaveData );
	int						SaveReadNameAndComment( FileHandle_t f, OUT_Z_CAP(nameSize) char *name, int nameSize, OUT_Z_CAP(commentSize) char *comment, int commentSize ) OVERRIDE;
	void					BuildRestoredIndexTranslationTable( char const *mapname, CSaveRestoreData *pSaveData, bool verbose );
	char const				*GetSaveGameMapName( char const *level );

	void					SetMostRecentSaveGame( const char *pSaveName );
	int						GetMostRecentElapsedMinutes( void );
	int						GetMostRecentElapsedSeconds( void );
	int						GetMostRecentElapsedTimeSet( void );
	void					SetMostRecentElapsedMinutes( const int min );
	void					SetMostRecentElapsedSeconds( const int sec );

	struct SaveRestoreTranslate
	{
		string_t classname;
		int savedindex;
		int restoredindex;
	};

	struct RestoreLookupTable
	{
		RestoreLookupTable() :
			m_vecLandMarkOffset( 0, 0, 0 )
		{
		}

		void Clear()
		{
			lookup.RemoveAll();
			m_vecLandMarkOffset.Init();
		}

		RestoreLookupTable( const RestoreLookupTable& src )
		{
			int c = src.lookup.Count();
			for ( int i = 0 ; i < c; i++ )
			{
				lookup.AddToTail( src.lookup[ i ] );
			}

			m_vecLandMarkOffset = src.m_vecLandMarkOffset;
		}

		RestoreLookupTable& operator=( const RestoreLookupTable& src )
		{
			if ( this == &src )
				return *this;

			int c = src.lookup.Count();
			for ( int i = 0 ; i < c; i++ )
			{
				lookup.AddToTail( src.lookup[ i ] );
			}

			m_vecLandMarkOffset = src.m_vecLandMarkOffset;

			return *this;
		}

		CUtlVector< SaveRestoreTranslate >	lookup;
		Vector								m_vecLandMarkOffset;
	};

	RestoreLookupTable		*FindOrAddRestoreLookupTable( char const *mapname );
	int						LookupRestoreSpotSaveIndex( RestoreLookupTable *table, int save );
	void					ReapplyDecal( bool adjacent, RestoreLookupTable *table, decallist_t *entry );

	CUtlDict< RestoreLookupTable, int >	m_RestoreLookup;

	bool	m_bClearSaveDir;
	char	m_szSaveGameScreenshotFile[MAX_OSPATH];
	float	m_flClientSaveRestoreTime;

	char	m_szMostRecentSaveLoadGame[MAX_OSPATH];
	char	m_szSaveGameName[MAX_OSPATH];

	int		m_MostRecentElapsedMinutes;
	int		m_MostRecentElapsedSeconds;
	int		m_MostRecentElapsedTimeSet;

	bool	m_bWaitingForSafeDangerousSave;
	bool	m_bIsXSave;

	int		m_nDeferredCommandFrames;
	CUtlVector< CUtlSymbol > m_sDeferredCommands;
};

CSaveRestore g_SaveRestore;
ISaveRestore *saverestore = (ISaveRestore *)&g_SaveRestore;

BEGIN_SIMPLE_DATADESC( GAME_HEADER )

	DEFINE_FIELD( mapCount, FIELD_INTEGER ),
	DEFINE_ARRAY( mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( comment, FIELD_CHARACTER, 80 ),
	DEFINE_ARRAY( originMapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( landmark, FIELD_CHARACTER, 256 ),

END_DATADESC()


// The proper way to extend the file format (add a new data chunk) is to add a field here, and use it to determine
// whether your new data chunk is in the file or not.  If the file was not saved with your new field, the chunk 
// won't be there either.
// Structure members can be added/deleted without any problems, new structures must be reflected in an existing struct
// and not read unless actually in the file.  New structure members will be zeroed out when reading 'old' files.

BEGIN_SIMPLE_DATADESC( SAVE_HEADER )

//	DEFINE_FIELD( saveId, FIELD_INTEGER ),
//	DEFINE_FIELD( version, FIELD_INTEGER ),
	DEFINE_FIELD( skillLevel, FIELD_INTEGER ),
	DEFINE_FIELD( connectionCount, FIELD_INTEGER ),
	DEFINE_FIELD( lightStyleCount, FIELD_INTEGER ),
	DEFINE_FIELD( mapVersion, FIELD_INTEGER ),
	DEFINE_FIELD( time__USE_VCR_MODE, FIELD_TIME ),
	DEFINE_ARRAY( mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( skyName, FIELD_CHARACTER, 32 ),
END_DATADESC()

BEGIN_SIMPLE_DATADESC( levellist_t )
	DEFINE_ARRAY( mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( landmarkName, FIELD_CHARACTER, 32 ),
	DEFINE_FIELD( pentLandmark, FIELD_CLASSPTR),
	DEFINE_FIELD( vecLandmarkOrigin, FIELD_VECTOR ),
END_DATADESC()

BEGIN_SIMPLE_DATADESC( SAVELIGHTSTYLE )
	DEFINE_FIELD( index, FIELD_INTEGER ),
	DEFINE_ARRAY( style, FIELD_CHARACTER, 64 ),
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: 
// Output : char const
//-----------------------------------------------------------------------------
char const *CSaveRestore::GetSaveGameMapName( char const *level )
{
	Assert( level );

	static char mapname[ 256 ];
	Q_FileBase( level, mapname, sizeof( mapname ) );
	return mapname;
}

//-----------------------------------------------------------------------------
// Purpose: returns the most recent save
//-----------------------------------------------------------------------------
const char *CSaveRestore::FindRecentSave( char *pNameBuf, int nameBufLen )
{
	Q_strncpy( pNameBuf, m_szMostRecentSaveLoadGame, nameBufLen );

	if ( !m_szMostRecentSaveLoadGame[0] )
		return NULL;

	return pNameBuf;
}

//-----------------------------------------------------------------------------
// Purpose: Forgets the most recent save game
//			this is so the current level will just restart if the player dies
//-----------------------------------------------------------------------------
void CSaveRestore::ForgetRecentSave()
{
	m_szMostRecentSaveLoadGame[0] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Returns the save game directory for the current player profile
//-----------------------------------------------------------------------------
char *CSaveRestore::GetSaveDir(void)
{
	static char szDirectory[MAX_OSPATH];
	Q_memset(szDirectory, 0, MAX_OSPATH);
	Q_strncpy(szDirectory, "save/", sizeof( szDirectory ) );
	return szDirectory;
}

//-----------------------------------------------------------------------------
// Purpose: keeps the last few save files of the specified file around, renamed
//-----------------------------------------------------------------------------
void CSaveRestore::AgeSaveList( const char *pName, int count, bool bIsXSave )
{
	// age all the previous save files (including screenshots)
	while ( count > 0 )
	{
		AgeSaveFile( pName, IsX360() ? "360.sav" : "sav", count, bIsXSave );
		if ( !IsX360() )
		{
			AgeSaveFile( pName, "tga", count, bIsXSave );
		}
		count--;
	}
}

//-----------------------------------------------------------------------------
// Purpose: ages a single sav file
//-----------------------------------------------------------------------------
void CSaveRestore::AgeSaveFile( const char *pName, const char *ext, int count, bool bIsXSave )
{
	char newName[MAX_OSPATH], oldName[MAX_OSPATH];

	if ( !IsXSave() )
	{
		if ( count == 1 )
		{
			Q_snprintf( oldName, sizeof( oldName ), "//%s/%s%s.%s", MOD_DIR, GetSaveDir(), pName, ext );// quick.sav. DON'T FixSlashes on this, it needs to be //MOD
		}
		else
		{
			Q_snprintf( oldName, sizeof( oldName ), "//%s/%s%s%02d.%s", MOD_DIR, GetSaveDir(), pName, count-1, ext );	// quick04.sav, etc. DON'T FixSlashes on this, it needs to be //MOD
		}

		Q_snprintf( newName, sizeof( newName ), "//%s/%s%s%02d.%s", MOD_DIR, GetSaveDir(), pName, count, ext ); // DON'T FixSlashes on this, it needs to be //MOD
	}
	else
	{
		if ( count == 1 )
		{
			Q_snprintf( oldName, sizeof( oldName ), "%s:\\%s.%s", GetCurrentMod(), pName, ext );
		}
		else
		{
			Q_snprintf( oldName, sizeof( oldName ), "%s:\\%s%02d.%s", GetCurrentMod(), pName, count-1, ext );
		}

		Q_snprintf( newName, sizeof( newName ), "%s:\\%s%02d.%s", GetCurrentMod(), pName, count, ext );
	}

	// Scroll the name list down (rename quick04.sav to quick05.sav)
	if ( g_pFileSystem->FileExists( oldName ) )
	{
		if ( count == save_history_count.GetInt() )
		{
			// there could be an old version, remove it
			if ( g_pFileSystem->FileExists( newName ) )
			{
				g_pFileSystem->RemoveFile( newName );
			}
		}

		g_pFileSystem->RenameFile( oldName, newName );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CSaveRestore::IsValidSave( void )
{
	if (cmd_source != src_command)
		return 0;

	// Don't parse autosave/transition save/restores during playback!
	if ( demoplayer->IsPlayingBack() )
	{
		return 0;
	}

	if ( !sv.IsActive() )
	{
		ConMsg ("Not playing a local game.\n");
		return 0;
	}

	if ( !cl.IsActive() )
	{
		ConMsg ("Can't save if not active.\n");
		return 0;
	}

	if ( sv.IsMultiplayer() )
	{
		ConMsg ("Can't save multiplayer games.\n");
		return 0;
	}

	if ( sv.GetClientCount() > 0 && sv.GetClient(0)->IsActive() )
	{
		Assert( serverGameClients );
		CGameClient *pGameClient = sv.Client( 0 );
		CPlayerState *pl = serverGameClients->GetPlayerState( pGameClient->m_nEntityIndex );
		if ( !pl )
		{
			ConMsg ("Can't savegame without a player!\n");
			return 0;
		}
			
		// we can't save if we're dead... unless we're reporting a bug.
		if ( pl->deadflag != false && !bugreporter->IsVisible() )
		{
			ConMsg ("Can't savegame with a dead player\n");
			return 0;
		}
	}
	
	// Passed all checks, it's ok to save
	return 1;
}

static ConVar save_asyncdelay( "save_asyncdelay", "0", 0, "For testing, adds this many milliseconds of delay to the save operation." );

CSaveRestoreData* SaveInit(int size, bool bServer)
{
	CSaveRestoreData* pSaveData;

#if ( defined( DISABLE_DEBUG_HISTORY ) )//defined( CLIENT_DLL ) || 
	if (size <= 0)
		size = 2 * 1024 * 1024;		// Reserve 2048K for now, UNDONE: Shrink this after compressing strings
#else
	if (bServer) {
		if (size <= 0)
			size = 3 * 1024 * 1024;		// Reserve 3096K for now, UNDONE: Shrink this after compressing strings
	}
	else {
		if (size <= 0)
			size = 2 * 1024 * 1024;		// Reserve 2048K for now, UNDONE: Shrink this after compressing strings
	}
#endif

	int numentities;

	if (bServer) {
		numentities = serverEntitylist->NumberOfEntities();
	}
	else {
		numentities = entitylist->NumberOfEntities(false);//need check default value
	}

	void* pSaveMemory = SaveAllocMemory(sizeof(CSaveRestoreData) + (sizeof(entitytable_t) * numentities) + size, sizeof(char));
	if (!pSaveMemory)
	{
		return NULL;
	}

	pSaveData = MakeSaveRestoreData(pSaveMemory);
	pSaveData->Init((char*)(pSaveData + 1), size);	// skip the save structure

	const int nTokens = 0xfff; // Assume a maximum of 4K-1 symbol table entries(each of some length)
	pSaveMemory = SaveAllocMemory(nTokens, sizeof(char*));
	if (!pSaveMemory)
	{
		SaveFreeMemory(pSaveMemory);
		return NULL;
	}

	pSaveData->InitSymbolTable((char**)pSaveMemory, nTokens);

	//---------------------------------

	if (bServer) {
		pSaveData->levelInfo.time = g_ServerGlobalVariables.curtime;	// Use DLL time
	}
	else {
		pSaveData->levelInfo.time = g_ClientGlobalVariables.curtime;	// Use DLL time
	}
	pSaveData->levelInfo.vecLandmarkOffset = vec3_origin;
	pSaveData->levelInfo.fUseLandmark = false;
	pSaveData->levelInfo.connectionCount = 0;

	//---------------------------------

	if (bServer) {
		//g_ServerGlobalVariables.pSaveData = pSaveData;
	}
	else {
		//g_ClientGlobalVariables.pSaveData = pSaveData;
	}

	return pSaveData;
}

//-----------------------------------------------------------------------------
// Purpose: save a game with the given name/comment
//			note: Added S_ExtraUpdate calls to fix audio pops in autosaves
//-----------------------------------------------------------------------------
int CSaveRestore::SaveGameSlot( const char *pSaveName, const char *pSaveComment, bool onlyThisLevel, bool bSetMostRecent)//, const char *pszDestMap, const char *pszLandmark 
{
	if ( save_disable.GetBool()  )
	{
		return 0;
	}

	if ( save_asyncdelay.GetInt() > 0 )
	{
		Sys_Sleep( clamp( save_asyncdelay.GetInt(), 0, 3000 ) );
	}

	SaveMsg( "Start save... (%d/%d)\n", ThreadInMainThread(), ThreadGetCurrentId() );
	VPROF_BUDGET( "SaveGameSlot", "Save" );
	char			hlPath[256], name[256], *pTokenData;
	int				tag, i, tokenSize;
	CSaveRestoreData	*pSaveData;
	GAME_HEADER		gameHeader;

#if defined( _MEMTEST )
	Cbuf_AddText( "mem_dump\n" );
#endif

	g_pSaveRestoreFileSystem->AsyncFinishAllWrites();

	S_ExtraUpdate();
	FinishAsyncSave();
	SaveResetMemory();
	S_ExtraUpdate();

	g_AsyncSaveCallQueue.DisableQueue( !save_async.GetBool() );

	// Figure out the name for this save game
	CalcSaveGameName( pSaveName, name, sizeof( name ) );
	ConDMsg( "Saving game to %s...\n", name );

	Q_strncpy( m_szSaveGameName, name, sizeof( m_szSaveGameName )) ;

	if ( m_bClearSaveDir )
	{
		m_bClearSaveDir = false;
		g_AsyncSaveCallQueue.QueueCall( this, &CSaveRestore::DoClearSaveDir, IsXSave() );
	}

	if ( !IsXSave() )
	{
		if ( onlyThisLevel )
		{
			Q_snprintf( hlPath, sizeof( hlPath ), "%s%s*.HL?", GetSaveDir(), sv.GetMapName() );
		}
		else
		{
			Q_snprintf( hlPath, sizeof( hlPath ), "%s*.HL?", GetSaveDir() );
		}
	}
	else
	{
		if ( onlyThisLevel )
		{
			Q_snprintf( hlPath, sizeof( hlPath ), "%s:\\%s*.HL?", GetCurrentMod(), sv.GetMapName() );
		}
		else
		{
			Q_snprintf( hlPath, sizeof( hlPath ), "%s:\\*.HL?", GetCurrentMod() );
		}
	}

	// Output to disk
	bool bClearFile = true;
	bool bIsQuick = ( stricmp(pSaveName, "quick") == 0 );
	bool bIsAutosave = ( !bIsQuick && stricmp(pSaveName,"autosave") == 0 );
	bool bIsAutosaveDangerous = ( !bIsAutosave && stricmp(pSaveName,"autosavedangerous") == 0 );
	if ( bIsQuick || bIsAutosave || bIsAutosaveDangerous )
	{
		bClearFile = false;
		SaveMsg( "Queue AgeSaveList\n"); 
		if ( StorageDeviceValid() )
		{
			g_AsyncSaveCallQueue.QueueCall( this, &CSaveRestore::AgeSaveList, CUtlEnvelope<const char *>(pSaveName), save_history_count.GetInt(), IsXSave() );
		}
	}

	S_ExtraUpdate();
	if (!SaveGameState( false, NULL, false, ( bIsAutosave || bIsAutosaveDangerous )  ) )
	{
		m_szSaveGameName[ 0 ] = 0;
		return 0;	
	}
	S_ExtraUpdate();

	//---------------------------------
			
	pSaveData = SaveInit( 0, true);

	if ( !pSaveData )
	{
		m_szSaveGameName[ 0 ] = 0;
		return 0;	
	}

	Q_FixSlashes( hlPath );
	Q_strncpy( gameHeader.comment, pSaveComment, sizeof( gameHeader.comment ) );

	//if ( pszDestMap && pszLandmark && *pszDestMap && *pszLandmark )
	//{
	//	Q_strncpy( gameHeader.mapName, pszDestMap, sizeof( gameHeader.mapName ) );
	//	Q_strncpy( gameHeader.originMapName, sv.GetMapName(), sizeof( gameHeader.originMapName ) );
	//	Q_strncpy( gameHeader.landmark, pszLandmark, sizeof( gameHeader.landmark ) );
	//}
	//else
	{
		Q_strncpy( gameHeader.mapName, sv.GetMapName(), sizeof( gameHeader.mapName ) );
		gameHeader.originMapName[0] = 0;
		gameHeader.landmark[0] = 0;
	}

	gameHeader.mapCount = 0; // No longer used. The map packer will place the map count at the head of the compound files (toml 7/18/2007)
	CSaveServer saveHelper(pSaveData);
	saveHelper.WriteFields( "GameHeader", &gameHeader, NULL, GAME_HEADER::m_DataMap.dataDesc, GAME_HEADER::m_DataMap.dataNumFields );
	SaveGlobalState( pSaveData );

	// Write entity string token table
	pTokenData = pSaveData->AccessCurPos();
	for( i = 0; i < pSaveData->SizeSymbolTable(); i++ )
	{
		const char *pszToken = (pSaveData->StringFromSymbol( i )) ? pSaveData->StringFromSymbol( i ) : "";
		if ( !pSaveData->Write( pszToken, strlen(pszToken) + 1 ) )
		{
			ConMsg( "Token Table Save/Restore overflow!" );
			break;
		}
	}	

	tokenSize = pSaveData->AccessCurPos() - pTokenData;
	pSaveData->Rewind( tokenSize );


	// open the file to validate it exists, and to clear it
	if ( bClearFile && !IsX360() )
	{		
		FileHandle_t pSaveFile = g_pSaveRestoreFileSystem->Open( name, "wb" );
		if (!pSaveFile && g_pFileSystem->FileExists( name, "GAME" ) )
		{
			Msg("Save failed: invalid file name '%s'\n", pSaveName);
			m_szSaveGameName[ 0 ] = 0;
			return 0;
		}
		if ( pSaveFile )
			g_pSaveRestoreFileSystem->Close( pSaveFile );
		S_ExtraUpdate();
	}

	// If this isn't a dangerous auto save use it next
	if ( bSetMostRecent )
	{
		SetMostRecentSaveGame( pSaveName );
	}
	m_bWaitingForSafeDangerousSave = bIsAutosaveDangerous;

	int iHeaderBufferSize = 64 + tokenSize + pSaveData->GetCurPos();
	void *pMem = new char[iHeaderBufferSize];

	CUtlBuffer saveHeader( pMem, iHeaderBufferSize );

	// Write the header -- THIS SHOULD NEVER CHANGE STRUCTURE, USE SAVE_HEADER FOR NEW HEADER INFORMATION
	// THIS IS ONLY HERE TO IDENTIFY THE FILE AND GET IT'S SIZE.
	tag = MAKEID('J','S','A','V');
	saveHeader.Put( &tag, sizeof(int) );
	tag = SAVEGAME_VERSION;
	saveHeader.Put( &tag, sizeof(int) );
	tag = pSaveData->GetCurPos();
	saveHeader.Put( &tag, sizeof(int) ); // Does not include token table

	// Write out the tokens first so we can load them before we load the entities
	tag = pSaveData->SizeSymbolTable();
	saveHeader.Put( &tag, sizeof(int) );
	saveHeader.Put( &tokenSize, sizeof(int) );
	saveHeader.Put( pTokenData, tokenSize );

	saveHeader.Put( pSaveData->GetBuffer(), pSaveData->GetCurPos() );
	
	// Create the save game container before the directory copy 
	g_AsyncSaveCallQueue.QueueCall( g_pSaveRestoreFileSystem, &ISaveRestoreFileSystem::AsyncWrite, CUtlEnvelope<const char *>(name), saveHeader.Base(), saveHeader.TellPut(), true, false, (FSAsyncControl_t *) NULL );
	g_AsyncSaveCallQueue.QueueCall( this, &CSaveRestore::DirectoryCopy, CUtlEnvelope<const char *>(hlPath), CUtlEnvelope<const char *>(name), m_bIsXSave );

	// Finish all writes and close the save game container
	// @TODO: this async finish all writes has to go away, very expensive and will make game hitchy. switch to a wait on the last async op
	g_AsyncSaveCallQueue.QueueCall( g_pFileSystem, &IFileSystem::AsyncFinishAllWrites );
	
	if ( IsXSave() && StorageDeviceValid() )
	{
		// Finish all pending I/O to the storage devices
		g_AsyncSaveCallQueue.QueueCall( g_pXboxSystem, &IXboxSystem::FinishContainerWrites );
	}

	S_ExtraUpdate();
	Finish( pSaveData );
	S_ExtraUpdate();

	// queue up to save a matching screenshot
	if ( !IsX360() && save_screenshot.GetBool() ) // X360TBD: Faster savegame screenshots
	{
		if ( !( bIsAutosave || bIsAutosaveDangerous ) || save_screenshot.GetInt() == 2 )
		{
			Q_snprintf( m_szSaveGameScreenshotFile, sizeof( m_szSaveGameScreenshotFile ), "%s%s%s.tga", GetSaveDir(), pSaveName, GetPlatformExt() );
		}
	}

	S_ExtraUpdate();

	DispatchAsyncSave();

	m_szSaveGameName[ 0 ] = 0;
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: Saves a screenshot for save game if necessary
//-----------------------------------------------------------------------------
void CSaveRestore::UpdateSaveGameScreenshots()
{
	if ( IsPC() && g_LostVideoMemory )
		return;

#ifndef SWDS
	if ( m_szSaveGameScreenshotFile[0] )
	{
		host_framecount++;
		g_ClientGlobalVariables.framecount = host_framecount;
		g_ClientDLL->WriteSaveGameScreenshot( m_szSaveGameScreenshotFile );
		m_szSaveGameScreenshotFile[0] = 0;
	}
#endif
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CSaveRestore::SaveReadHeader( FileHandle_t pFile, GAME_HEADER *pHeader, int readGlobalState, bool *pbOldSave )
{
	int					i, tag, size, tokenCount, tokenSize;
	char				*pReadPos;
	CSaveRestoreData	*pSaveData = NULL;

	if( g_pSaveRestoreFileSystem->Read( &tag, sizeof(int), pFile ) != sizeof(int) )
		return 0;

	if ( tag != MAKEID('J','S','A','V') )
	{
		Warning( "Can't load saved game, incorrect FILEID\n" );
		return 0;
	}
		
	if ( g_pSaveRestoreFileSystem->Read( &tag, sizeof(int), pFile ) != sizeof(int) )
		return 0;

	if ( tag != SAVEGAME_VERSION )				// Enforce version for now
	{
		Warning( "Can't load saved game, incorrect version (got %i expecting %i)\n", tag, SAVEGAME_VERSION );
		return 0;
	}

	if ( g_pSaveRestoreFileSystem->Read( &size, sizeof(int), pFile ) != sizeof(int) )
		return 0;

	if ( g_pSaveRestoreFileSystem->Read( &tokenCount, sizeof(int), pFile ) != sizeof(int) )
		return 0;

	if ( g_pSaveRestoreFileSystem->Read( &tokenSize, sizeof(int), pFile ) != sizeof(int) )
		return 0;

	// At this point we must clean this data up if we fail!
	void *pSaveMemory = SaveAllocMemory( sizeof(CSaveRestoreData) + tokenSize + size, sizeof(char) );
	if ( !pSaveMemory )
	{
		return 0;
	}

	pSaveData = MakeSaveRestoreData( pSaveMemory );

	pSaveData->levelInfo.connectionCount = 0;

	pReadPos = (char *)(pSaveData + 1);

	if ( tokenSize > 0 )
	{
		if ( g_pSaveRestoreFileSystem->Read(pReadPos, tokenSize, pFile ) != tokenSize )
		{
			Finish( pSaveData );
			return 0;
		}

		pSaveMemory = SaveAllocMemory( tokenCount, sizeof(char *), true );
		if ( !pSaveMemory )
		{
			Finish( pSaveData );
			return 0;
		}

		pSaveData->InitSymbolTable( (char**)pSaveMemory, tokenCount );

		// Make sure the token strings pointed to by the pToken hashtable.
		for( i=0; i<tokenCount; i++ )
		{
			if ( *pReadPos)
			{
				Verify( pSaveData->DefineSymbol(pReadPos, i ) );
			}
			while( *pReadPos++ );				// Find next token (after next null)
		}
	}
	else
	{
		pSaveData->InitSymbolTable( NULL, 0 );
	}


	pSaveData->levelInfo.fUseLandmark = false;
	pSaveData->levelInfo.time = 0;

	// pszTokenList now points after token data
	pSaveData->Init(pReadPos, size );
	if ( g_pSaveRestoreFileSystem->Read( pSaveData->GetBuffer(), size, pFile ) != size )
	{
		Finish( pSaveData );
		return 0;
	}
	
	CRestoreServer restoreHelper(pSaveData);
	restoreHelper.ReadFields( "GameHeader", pHeader, NULL, GAME_HEADER::m_DataMap.dataDesc, GAME_HEADER::m_DataMap.dataNumFields );
	if ( g_szMapLoadOverride[0] )
	{
		V_strncpy( pHeader->mapName, g_szMapLoadOverride, sizeof( pHeader->mapName ) );
		g_szMapLoadOverride[0] = 0;
	}

	if ( pHeader->mapCount != 0 && pbOldSave)
		*pbOldSave = true;

	if ( readGlobalState && pHeader->mapCount == 0 ) // Alfred: Only load save games from the OB era engine where mapCount is forced to zero
	{
		RestoreGlobalState( pSaveData );
	}

	Finish( pSaveData );

	if ( pHeader->mapCount == 0 )
	{
		if ( g_pSaveRestoreFileSystem->Read( &pHeader->mapCount, sizeof(pHeader->mapCount), pFile ) != sizeof(pHeader->mapCount) )
			return 0;
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pName - 
//			*output - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CSaveRestore::CalcSaveGameName( const char *pName, char *output, int outputStringLength )
{
	if (!pName || !pName[0])
		return false;

	if ( IsXSave() )
	{
		Q_snprintf( output, outputStringLength, "%s:/%s", GetCurrentMod(), pName );
	}
	else
	{
		Q_snprintf( output, outputStringLength, "%s%s", GetSaveDir(), pName );
	}
	Q_DefaultExtension( output, IsX360() ? ".360.sav" : ".sav", outputStringLength );
	Q_FixSlashes( output );

	return true;
}


//-----------------------------------------------------------------------------
// Does this save file exist?
//-----------------------------------------------------------------------------
bool CSaveRestore::SaveFileExists( const char *pName )
{
	FinishAsyncSave();
	char name[256];
	if ( !CalcSaveGameName( pName, name, sizeof( name ) ) )
		return false;

	bool bExists = false;

	if ( IsXSave() )
	{
		if ( StorageDeviceValid() )
		{
			bExists = g_pFileSystem->FileExists( name );
		}
		else
		{
			bExists = g_pSaveRestoreFileSystem->FileExists( name );
		}
	}
	else
	{
		bExists = g_pFileSystem->FileExists( name );
	}

	return bExists;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pName - 
// Output : int
//-----------------------------------------------------------------------------
bool CL_HL2Demo_MapCheck( const char *name ); // in host_cmd.cpp
bool CL_PortalDemo_MapCheck( const char *name ); // in host_cmd.cpp
bool CSaveRestore::LoadGame( const char *pName )
{
	FileHandle_t	pFile;
	GAME_HEADER		gameHeader;
	char			name[ MAX_PATH ];
	bool			validload = false;

	FinishAsyncSave();
	SaveResetMemory();

	if ( !CalcSaveGameName( pName, name, sizeof( name ) ) )
	{
		DevWarning("Loaded bad game %s\n", pName);
		Assert(0);
		return false;
	}

	// store off the most recent save
	SetMostRecentSaveGame( pName );

	ConMsg( "Loading game from %s...\n", name );

	m_bClearSaveDir = false;
	DoClearSaveDir( IsXSave() );

	bool bLoadedToMemory = false;
	if ( IsX360() )
	{
		bool bValidStorageDevice = StorageDeviceValid();
		if ( bValidStorageDevice )
		{
			// Load the file into memory, whole hog
			bLoadedToMemory = g_pSaveRestoreFileSystem->LoadFileFromDisk( name );
			if ( bLoadedToMemory == false )
				return false;
		}
	}
	
	int iElapsedMinutes = 0;
	int iElapsedSeconds = 0;
	bool bOldSave = false;

	pFile = g_pSaveRestoreFileSystem->Open( name, "rb", MOD_DIR );
	if ( pFile )
	{
		char szDummyName[ MAX_PATH ];
		char szComment[ MAX_PATH ];
		char szElapsedTime[ MAX_PATH ];

		if ( SaveReadNameAndComment( pFile, szDummyName, sizeof(szDummyName), szComment, sizeof(szComment) ) )
		{
			// Elapsed time is the last 6 characters in comment. (mmm:ss)
			int i;
			i = strlen( szComment );
			Q_strncpy( szElapsedTime, "??", sizeof( szElapsedTime ) );
			if (i >= 6)
			{
				Q_strncpy( szElapsedTime, (char *)&szComment[i - 6], 7 );
				szElapsedTime[6] = '\0';

				// parse out
				iElapsedMinutes = atoi( szElapsedTime );
				iElapsedSeconds = atoi( szElapsedTime + 4);
			}
		}
		else
		{
			g_pSaveRestoreFileSystem->Close( pFile );
			if ( bLoadedToMemory )
			{
				g_pSaveRestoreFileSystem->RemoveFile( name );
			}
			return NULL;
		}

		// Reset the file pointer to the start of the file
		g_pSaveRestoreFileSystem->Seek( pFile, 0, FILESYSTEM_SEEK_HEAD );

		if ( SaveReadHeader( pFile, &gameHeader, 1, &bOldSave ) )
		{
			validload = DirectoryExtract( pFile, gameHeader.mapCount );
		}

		if ( !HaveExactMap( gameHeader.mapName ) )
		{
			Msg( "Map '%s' missing or invalid\n", gameHeader.mapName );
			validload = false;
		}

		g_pSaveRestoreFileSystem->Close( pFile );
		
		if ( bLoadedToMemory )
		{
			g_pSaveRestoreFileSystem->RemoveFile( name );
		}
	}
	else
	{
		ConMsg( "File not found or failed to open.\n" );
		return false;
	}

	if ( !validload )
	{
		Msg("Save file %s is not valid\n", name );
		return false;
	}

	// stop demo loop in case this fails
	cl.demonum = -1;		

	deathmatch.SetValue( 0 );
	coop.SetValue( 0 );

	if ( !CL_HL2Demo_MapCheck( gameHeader.mapName ) )
	{
		Warning( "Save file %s is not valid\n", name );
		return false;	
	}
	
	if ( !CL_PortalDemo_MapCheck( gameHeader.mapName ) )
	{
		Warning( "Save file %s is not valid\n", name );
		return false;	
	}

	//bool bIsTransitionSave = ( gameHeader.originMapName[0] != 0 );

	bool retval = Host_NewGame( gameHeader.mapName, true, false, bOldSave );//( bIsTransitionSave ) ? gameHeader.originMapName : NULL, ( bIsTransitionSave ) ? gameHeader.landmark : NULL,

	SetMostRecentElapsedMinutes( iElapsedMinutes );
	SetMostRecentElapsedSeconds( iElapsedSeconds );

	return retval;
}

//-----------------------------------------------------------------------------
// Purpose: Remebers the most recent save game
//-----------------------------------------------------------------------------
void CSaveRestore::SetMostRecentSaveGame( const char *pSaveName )
{
	// Only remember xsaves in the x360 case
	if ( IsX360() && IsXSave() == false )
		return;

	if ( pSaveName )
	{
		Q_strncpy( m_szMostRecentSaveLoadGame, pSaveName, sizeof(m_szMostRecentSaveLoadGame) );
	}
	else
	{
		m_szMostRecentSaveLoadGame[0] = 0;
	}
	if ( !m_szMostRecentSaveLoadGame[0] )
	{
		DevWarning("Cleared most recent save!\n");
		Assert(0);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns the last recored elapsed minutes
//-----------------------------------------------------------------------------
int CSaveRestore::GetMostRecentElapsedMinutes( void )
{
	return m_MostRecentElapsedMinutes;
}

//-----------------------------------------------------------------------------
// Purpose: Returns the last recored elapsed seconds
//-----------------------------------------------------------------------------
int CSaveRestore::GetMostRecentElapsedSeconds( void )
{
	return m_MostRecentElapsedSeconds;
}

int CSaveRestore::GetMostRecentElapsedTimeSet( void )
{
	return m_MostRecentElapsedTimeSet;
}

//-----------------------------------------------------------------------------
// Purpose: Sets the last recored elapsed minutes
//-----------------------------------------------------------------------------
void CSaveRestore::SetMostRecentElapsedMinutes( const int min )
{
	m_MostRecentElapsedMinutes = min;
	m_MostRecentElapsedTimeSet = g_ServerGlobalVariables.curtime;
}

//-----------------------------------------------------------------------------
// Purpose: Sets the last recored elapsed seconds
//-----------------------------------------------------------------------------
void CSaveRestore::SetMostRecentElapsedSeconds( const int sec )
{
	m_MostRecentElapsedSeconds = sec;
	m_MostRecentElapsedTimeSet = g_ServerGlobalVariables.curtime;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : CSaveRestoreData
//-----------------------------------------------------------------------------

struct SaveFileHeaderTag_t
{
	int id;
	int version;
	
	bool operator==(const SaveFileHeaderTag_t &rhs) const { return ( memcmp( this, &rhs, sizeof(SaveFileHeaderTag_t) ) == 0 ); }
	bool operator!=(const SaveFileHeaderTag_t &rhs) const { return ( memcmp( this, &rhs, sizeof(SaveFileHeaderTag_t) ) != 0 ); }
};

#define MAKEID(d,c,b,a)	( ((int)(a) << 24) | ((int)(b) << 16) | ((int)(c) << 8) | ((int)(d)) )

const struct SaveFileHeaderTag_t CURRENT_SAVEFILE_HEADER_TAG = { MAKEID('V','A','L','V'), SAVEGAME_VERSION };

struct SaveFileSectionsInfo_t
{
	int nBytesSymbols;
	int nSymbols;
	int nBytesDataHeaders;
	int nBytesData;
	
	int SumBytes() const
	{
		return ( nBytesSymbols + nBytesDataHeaders + nBytesData );
	}
};

struct SaveFileSections_t
{
	char *pSymbols;
	char *pDataHeaders;
	char *pData;
};

void CSaveRestore::SaveGameStateGlobals( CSaveRestoreData *pSaveData )
{
	SAVE_HEADER header;

	INetworkStringTable * table = sv.GetLightStyleTable();

	Assert( table );
	
	// Write global data
	header.version 			= build_number( );
	header.skillLevel 		= skill.GetInt();	// This is created from an int even though it's a float
	header.connectionCount 	= pSaveData->levelInfo.connectionCount;
	header.time__USE_VCR_MODE	= sv.GetTime();
	ConVarRef skyname( "sv_skyname" );
	if ( skyname.IsValid() )
	{
		Q_strncpy( header.skyName, skyname.GetString(), sizeof( header.skyName ) );
	}
	else
	{
		Q_strncpy( header.skyName, "unknown", sizeof( header.skyName ) );
	}

	Q_strncpy( header.mapName, sv.GetMapName(), sizeof( header.mapName ) );
	header.lightStyleCount 	= 0;
	header.mapVersion = g_ServerGlobalVariables.mapversion;

	int i;
	for ( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		const char * ligthStyle = (const char*) table->GetStringUserData( i, NULL );
		if ( ligthStyle && ligthStyle[0] )
			header.lightStyleCount++;
	}

	pSaveData->levelInfo.time = 0; // prohibits rebase of header.time (why not just save time as a field_float and ditch this hack?)
	{
		CSaveServer saveHelper(pSaveData);
		saveHelper.WriteFields("Save Header", &header, NULL, SAVE_HEADER::m_DataMap.dataDesc, SAVE_HEADER::m_DataMap.dataNumFields);
	}
	pSaveData->levelInfo.time = header.time__USE_VCR_MODE;

	// Write adjacency list
	for (i = 0; i < pSaveData->levelInfo.connectionCount; i++) {
		CSaveServer saveHelper(pSaveData);
		saveHelper.WriteFields( "ADJACENCY", pSaveData->levelInfo.levelList + i, NULL, levellist_t::m_DataMap.dataDesc, levellist_t::m_DataMap.dataNumFields);
	}
	// Write the lightstyles
	SAVELIGHTSTYLE	light;
	for ( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		const char * ligthStyle = (const char*) table->GetStringUserData( i, NULL );

		if ( ligthStyle && ligthStyle[0] )
		{
			light.index = i;
			Q_strncpy( light.style, ligthStyle, sizeof( light.style ) );
			CSaveServer saveHelper(pSaveData);
			saveHelper.WriteFields( "LIGHTSTYLE", &light, NULL, SAVELIGHTSTYLE::m_DataMap.dataDesc, SAVELIGHTSTYLE::m_DataMap.dataNumFields );
		}
	}
}

//CSaveRestoreData *CSaveRestore::SaveGameStateInit( void )
//{
//	CSaveRestoreData *pSaveData = serverGameDLL->SaveInit( 0 );
//	
//	return pSaveData;
//}

bool CSaveRestore::SaveGameState( bool bTransition, CSaveRestoreData **ppReturnSaveData, bool bOpenContainer, bool bIsAutosaveOrDangerous )
{
	MDLCACHE_COARSE_LOCK_(g_pMDLCache);
	SaveMsg( "SaveGameState...\n" );
	int i;
	SaveFileSectionsInfo_t sectionsInfo;
	SaveFileSections_t sections;

	if ( ppReturnSaveData )
	{
		*ppReturnSaveData = NULL;
	}

	if ( bTransition )
	{
		if ( m_bClearSaveDir )
		{
			m_bClearSaveDir = false;
			DoClearSaveDir( IsXSave() );
		}
	}

	S_ExtraUpdate();
	CSaveRestoreData* pSaveData = SaveInit(0, true);
	if ( !pSaveData )
	{
		return false;
	}

	pSaveData->bAsync = bIsAutosaveOrDangerous;

	//---------------------------------
	// Save the data
	sections.pData = pSaveData->AccessCurPos();
	
	//---------------------------------
	// Pre-save

	g_pServerGameSaveRestoreBlockSet->PreSave( pSaveData );
	// Build the adjacent map list (after entity table build by game in presave)
	if ( bTransition )
	{
		CSaveServer saveHelper(pSaveData);
		serverEntitylist->BuildAdjacentMapList(&saveHelper);
	}
	else
	{
		pSaveData->levelInfo.connectionCount = 0;
	}
	S_ExtraUpdate();

	//---------------------------------

	SaveGameStateGlobals( pSaveData );

	S_ExtraUpdate();
	{
		CSaveServer saveHelper(pSaveData);
		g_pServerGameSaveRestoreBlockSet->Save(&saveHelper);
	}
	S_ExtraUpdate();
	
	sectionsInfo.nBytesData = pSaveData->AccessCurPos() - sections.pData;

	
	//---------------------------------
	// Save necessary tables/dictionaries/directories
	sections.pDataHeaders = pSaveData->AccessCurPos();
	
	CSaveServer saveHelper(pSaveData);
	g_pServerGameSaveRestoreBlockSet->WriteSaveHeaders( &saveHelper);
	g_pServerGameSaveRestoreBlockSet->PostSave();
	
	sectionsInfo.nBytesDataHeaders = pSaveData->AccessCurPos() - sections.pDataHeaders;

	//---------------------------------
	// Write the save file symbol table
	sections.pSymbols = pSaveData->AccessCurPos();

	for( i = 0; i < pSaveData->SizeSymbolTable(); i++ )
	{
		const char *pszToken = ( pSaveData->StringFromSymbol( i ) ) ? pSaveData->StringFromSymbol( i ) : "";
		if ( !pSaveData->Write( pszToken, strlen(pszToken) + 1 ) )
		{
			break;
		}
	}	

	sectionsInfo.nBytesSymbols = pSaveData->AccessCurPos() - sections.pSymbols;
	sectionsInfo.nSymbols = pSaveData->SizeSymbolTable();

	//---------------------------------
	// Output to disk
	char name[256];
	int nBytesStateFile = sizeof(CURRENT_SAVEFILE_HEADER_TAG) + 
		sizeof(sectionsInfo) + 
		sectionsInfo.nBytesSymbols + 
		sectionsInfo.nBytesDataHeaders + 
		sectionsInfo.nBytesData;

	void *pBuffer = new byte[nBytesStateFile];
	CUtlBuffer buffer( pBuffer, nBytesStateFile );

	// Write the header -- THIS SHOULD NEVER CHANGE STRUCTURE, USE SAVE_HEADER FOR NEW HEADER INFORMATION
	// THIS IS ONLY HERE TO IDENTIFY THE FILE AND GET IT'S SIZE.

	buffer.Put( &CURRENT_SAVEFILE_HEADER_TAG, sizeof(CURRENT_SAVEFILE_HEADER_TAG) );

	// Write out the tokens and table FIRST so they are loaded in the right order, then write out the rest of the data in the file.
	buffer.Put( &sectionsInfo, sizeof(sectionsInfo) );
	buffer.Put( sections.pSymbols, sectionsInfo.nBytesSymbols );
	buffer.Put( sections.pDataHeaders, sectionsInfo.nBytesDataHeaders );
	buffer.Put( sections.pData, sectionsInfo.nBytesData );

	if ( !IsXSave() )
	{
		Q_snprintf( name, 256, "//%s/%s%s.HL1", MOD_DIR, GetSaveDir(), GetSaveGameMapName( sv.GetMapName() ) ); // DON'T FixSlashes on this, it needs to be //MOD
		SaveMsg( "Queue COM_CreatePath\n" );
		g_AsyncSaveCallQueue.QueueCall( &COM_CreatePath, CUtlEnvelope<const char *>(name) );
	}
	else
	{
		Q_snprintf( name, 256, "%s:/%s.HL1", GetCurrentMod(), GetSaveGameMapName( sv.GetMapName() ) ); // DON'T FixSlashes on this, it needs to be //MOD
	}

	S_ExtraUpdate();

	SaveMsg( "Queue AsyncWrite (%s)\n", name );
	g_AsyncSaveCallQueue.QueueCall( g_pSaveRestoreFileSystem, &ISaveRestoreFileSystem::AsyncWrite, CUtlEnvelope<const char *>(name), pBuffer, nBytesStateFile, true, false, (FSAsyncControl_t *)NULL );
	pBuffer = NULL;
	
	//---------------------------------
	
	EntityPatchWrite( pSaveData, GetSaveGameMapName( sv.GetMapName() ), true );
	if ( !ppReturnSaveData )
	{
		Finish( pSaveData );
	}
	else
	{
		*ppReturnSaveData = pSaveData;
	}

	if ( !IsXSave() )
	{
		Q_snprintf(name, sizeof( name ), "//%s/%s%s.HL2", MOD_DIR, GetSaveDir(), GetSaveGameMapName( sv.GetMapName() ) );// DON'T FixSlashes on this, it needs to be //MOD
	}
	else
	{
		Q_snprintf(name, sizeof( name ), "%s:/%s.HL2", GetCurrentMod(), GetSaveGameMapName( sv.GetMapName() ) );// DON'T FixSlashes on this, it needs to be //MOD
	}
	// Let the client see the server entity to id lookup tables, etc.
	S_ExtraUpdate();
	bool bSuccess = SaveClientState( name );
	S_ExtraUpdate();

	//---------------------------------

	if ( bTransition )
	{
		FinishAsyncSave();
	}
	S_ExtraUpdate();

	return bSuccess;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *save - 
//-----------------------------------------------------------------------------
void CSaveRestore::Finish( CSaveRestoreData *save )
{
	char **pTokens = save->DetachSymbolTable();
	if ( pTokens )
		SaveFreeMemory( pTokens );

	entitytable_t *pEntityTable = save->DetachEntityTable();
	if ( pEntityTable )
		SaveFreeMemory( pEntityTable );

	//save->PurgeEntityHash();
	SaveFreeMemory( save );


	//g_ServerGlobalVariables.pSaveData = NULL;
}

BEGIN_SIMPLE_DATADESC( musicsave_t )

	DEFINE_ARRAY( songname, FIELD_CHARACTER, 128 ),
	DEFINE_FIELD( sampleposition, FIELD_INTEGER ),
	DEFINE_FIELD( master_volume, FIELD_SHORT ),

END_DATADESC()

BEGIN_SIMPLE_DATADESC( decallist_t )

	DEFINE_FIELD( position, FIELD_POSITION_VECTOR ),
	DEFINE_ARRAY( name, FIELD_CHARACTER, 128 ),
	DEFINE_FIELD( entityIndex, FIELD_SHORT ),
	//	DEFINE_FIELD( depth, FIELD_CHARACTER ),
	DEFINE_FIELD( flags, FIELD_CHARACTER ),
	DEFINE_FIELD( impactPlaneNormal, FIELD_VECTOR ),

END_DATADESC()

struct baseclientsectionsold_t
{
	int entitysize;
	int headersize;
	int decalsize;
	int symbolsize;

	int decalcount;
	int symbolcount;

	int SumBytes()
	{
		return entitysize + headersize + decalsize + symbolsize;
	}
};

struct clientsectionsold_t : public baseclientsectionsold_t
{
	char	*symboldata;
	char	*entitydata;
	char	*headerdata;
	char	*decaldata;
};

// FIXME:  Remove the above and replace with this once we update the save format!!
struct baseclientsections_t
{
	int entitysize;
	int headersize;
	int decalsize;
	int musicsize;
	int symbolsize;

	int decalcount;
	int	musiccount;
	int symbolcount;

	int SumBytes()
	{
		return entitysize + headersize + decalsize + symbolsize + musicsize;
	}
};

struct clientsections_t : public baseclientsections_t
{
	char	*symboldata;
	char	*entitydata;
	char	*headerdata;
	char	*decaldata;
	char	*musicdata;
};

int CSaveRestore::LookupRestoreSpotSaveIndex( RestoreLookupTable *table, int save )
{
	int c = table->lookup.Count();
	for ( int i = 0; i < c; i++ )
	{
		SaveRestoreTranslate *slot = &table->lookup[ i ];
		if ( slot->savedindex == save )
			return slot->restoredindex;
	}
	
	return -1;
}

void CSaveRestore::ReapplyDecal( bool adjacent, RestoreLookupTable *table, decallist_t *entry )
{
	int flags = entry->flags;
	if ( adjacent )
	{
		flags |= FDECAL_DONTSAVE;
	}

	// unlock sting tables to allow changes, helps to find unwanted changes (bebug build only)
	bool oldlock = networkStringTableContainerServer->Lock( false );

	if ( adjacent )
	{
		// These entities might not exist over transitions, so we'll use the saved plane and do a traceline instead
		Vector testspot = entry->position;
		VectorMA( testspot, 5.0f, entry->impactPlaneNormal, testspot );

		Vector testend = entry->position;
		VectorMA( testend, -5.0f, entry->impactPlaneNormal, testend );

		CTraceFilterHitAll traceFilter;
		trace_t tr;
		Ray_t ray;
		ray.Init( testspot, testend );
		g_pEngineTraceServer->TraceRay( ray, MASK_OPAQUE, &traceFilter, &tr );

		if ( tr.fraction != 1.0f && !tr.allsolid )
		{
			// Check impact plane normal
			float dot = entry->impactPlaneNormal.Dot( tr.plane.normal );
			if ( dot >= 0.99 )
			{
				// Hack, have to use server traceline stuff to get at an actuall index here
				IHandleEntity *hit = tr.m_pEnt;
				if ( hit != NULL )
				{
					// Looks like a good match for original splat plane, reapply the decal
					int entityToHit = hit->GetRefEHandle().GetEntryIndex();// NUM_FOR_EDICT(hit);
					if ( entityToHit >= 0 )
					{
						IEngineObjectClient *clientEntity = entitylist->GetEngineObject( entityToHit );
						if ( !clientEntity )
							return;
						
						bool found = false;
						int decalIndex = Draw_DecalIndexFromName( entry->name, &found );
						if ( !found )
						{
							// This is a serious HACK because we're grabbing the index that the server hasn't networked down to us and forcing
							//  the decal name directly.  However, the message should eventually arrive and set the decal back to the same
							//  name on the server's index...we can live with that I suppose.
							decalIndex = sv.PrecacheDecal( entry->name, RES_FATALIFMISSING );
							Draw_DecalSetName( decalIndex, entry->name );
						}

						g_pEfx->DecalShoot( 
							decalIndex, 
							entityToHit, 
							clientEntity->GetModel(), 
							clientEntity->GetAbsOrigin(),
							clientEntity->GetAbsAngles(),
							entry->position, 0, flags );
					}
				}
			}
		}

	}
	else
	{
		int entityToHit = entry->entityIndex != 0 ? LookupRestoreSpotSaveIndex( table, entry->entityIndex ) : entry->entityIndex;
		if ( entityToHit >= 0 )
		{
			// NOTE: I re-initialized the origin and angles as the decals pos/angle are saved in local space (ie. relative to
			//       the entity they are attached to.
			Vector vecOrigin( 0.0f, 0.0f, 0.0f );
			QAngle vecAngle( 0.0f, 0.0f, 0.0f );

			const model_t *pModel = NULL;
			IEngineObjectClient *clientEntity = entitylist->GetEngineObject( entityToHit );
			if ( clientEntity )
			{
				pModel = clientEntity->GetModel();
			}
			else
			{
				// This breaks client/server.  However, non-world entities are not in your PVS potentially.
				IEngineObjectServer* pServerEntity = serverEntitylist->GetEngineObject(entityToHit);
				if (pServerEntity)
				{
					pModel = sv.GetModel( pServerEntity->GetModelIndex() );						
				}
			}

			if ( pModel )
			{
				bool found = false;
				int decalIndex = Draw_DecalIndexFromName( entry->name, &found );
				if ( !found )
				{
					// This is a serious HACK because we're grabbing the index that the server hasn't networked down to us and forcing
					//  the decal name directly.  However, the message should eventually arrive and set the decal back to the same
					//  name on the server's index...we can live with that I suppose.
					decalIndex = sv.PrecacheDecal( entry->name, RES_FATALIFMISSING );
					Draw_DecalSetName( decalIndex, entry->name );
				}
				
				g_pEfx->DecalShoot( decalIndex, entityToHit, pModel, vecOrigin, vecAngle, entry->position, 0, flags );
			}
		}
	}

	// unlock sting tables to allow changes, helps to find unwanted changes (bebug build only)
	networkStringTableContainerServer->Lock( oldlock );
}

void CSaveRestore::RestoreClientState( char const *fileName, bool adjacent )
{
	FileHandle_t pFile;

	pFile = g_pSaveRestoreFileSystem->Open( fileName, "rb" );
	if ( !pFile )
	{
		DevMsg( "Failed to open client state file %s\n", fileName );
		return;
	}

	SaveFileHeaderTag_t tag;
	g_pSaveRestoreFileSystem->Read( &tag, sizeof(tag), pFile );
	if ( tag != CURRENT_SAVEFILE_HEADER_TAG )
	{
		g_pSaveRestoreFileSystem->Close( pFile );
		return;
	}

	// Check for magic number
	int savePos = g_pSaveRestoreFileSystem->Tell( pFile );

	int sectionheaderversion = 1;
	int magicnumber = 0;
	baseclientsections_t sections;

	g_pSaveRestoreFileSystem->Read( &magicnumber, sizeof( magicnumber ), pFile );

	if ( magicnumber == SECTION_MAGIC_NUMBER )
	{
		g_pSaveRestoreFileSystem->Read( &sectionheaderversion, sizeof( sectionheaderversion ), pFile );

		if ( sectionheaderversion != SECTION_VERSION_NUMBER )
		{
			g_pSaveRestoreFileSystem->Close( pFile );
			return;
		}
		g_pSaveRestoreFileSystem->Read( &sections, sizeof(baseclientsections_t), pFile );
	}
	else
	{
		// Rewind
		g_pSaveRestoreFileSystem->Seek( pFile, savePos, FILESYSTEM_SEEK_HEAD );
	
		baseclientsectionsold_t oldsections;

		g_pSaveRestoreFileSystem->Read( &oldsections, sizeof(baseclientsectionsold_t), pFile );

		Q_memset( &sections, 0, sizeof( sections ) );
		sections.entitysize = oldsections.entitysize;
		sections.headersize = oldsections.headersize;
		sections.decalsize = oldsections.decalsize;
		sections.symbolsize = oldsections.symbolsize;

		sections.decalcount = oldsections.decalcount;
		sections.symbolcount = oldsections.symbolcount;
	}


	void *pSaveMemory = SaveAllocMemory( sizeof(CSaveRestoreData) + sections.SumBytes(), sizeof(char) );
	if ( !pSaveMemory )
	{
		return;
	}

	CSaveRestoreData *pSaveData = MakeSaveRestoreData( pSaveMemory );
	// Needed?
	Q_strncpy( pSaveData->levelInfo.szCurrentMapName, fileName, sizeof( pSaveData->levelInfo.szCurrentMapName ) );

	g_pSaveRestoreFileSystem->Read( (char *)(pSaveData + 1), sections.SumBytes(), pFile );
	g_pSaveRestoreFileSystem->Close( pFile );

	char *pszTokenList = (char *)(pSaveData + 1);

	if ( sections.symbolsize > 0 )
	{
		pSaveMemory = SaveAllocMemory( sections.symbolcount, sizeof(char *), true );
		if ( !pSaveMemory )
		{
			SaveFreeMemory( pSaveData );
			return;
		}

		pSaveData->InitSymbolTable( (char**)pSaveMemory, sections.symbolcount );

		// Make sure the token strings pointed to by the pToken hashtable.
		for( int i=0; i<sections.symbolcount; i++ )
		{
			if ( *pszTokenList )
			{
				Verify( pSaveData->DefineSymbol( pszTokenList, i ) );
			}
			while( *pszTokenList++ );				// Find next token (after next null)
		}
	}
	else
	{
		pSaveData->InitSymbolTable( NULL, 0 );
	}

	Assert( pszTokenList - (char *)(pSaveData + 1) == sections.symbolsize );

	//---------------------------------
	// Set up the restore basis
	int size = sections.SumBytes() - sections.symbolsize;

	pSaveData->Init( (char *)(pszTokenList), size );	// The point pszTokenList was incremented to the end of the tokens

	CRestoreClient restoreHelper(pSaveData);
	g_pClientGameSaveRestoreBlockSet->PreRestore();
	g_pClientGameSaveRestoreBlockSet->ReadRestoreHeaders(&restoreHelper);
	
	pSaveData->Rebase();

	//HACKHACK
	pSaveData->levelInfo.time = m_flClientSaveRestoreTime;

	char name[256];
	Q_FileBase( fileName, name, sizeof( name ) );
	Q_strlower( name );

	RestoreLookupTable *table = FindOrAddRestoreLookupTable( name );

	pSaveData->levelInfo.fUseLandmark = adjacent;
	if ( adjacent )
	{
		pSaveData->levelInfo.vecLandmarkOffset = table->m_vecLandMarkOffset;
	}

	bool bFixTable = false;

	// Fixup restore indices based on what server re-created for us
	int c = pSaveData->NumEntities();
	for ( int i = 0 ; i < c; i++ )
	{
		entitytable_t *entry = pSaveData->GetEntityInfo( i );
		
		entry->restoreentityindex = LookupRestoreSpotSaveIndex( table, entry->saveentityindex );

		//Adrian: This means we are a client entity with no index to restore and we need our model precached.
		if ( entry->restoreentityindex == -1 && entry->classname != NULL_STRING && entry->modelname != NULL_STRING )
		{
			sv.PrecacheModel( STRING( entry->modelname ), RES_FATALIFMISSING | RES_PRELOAD );
			bFixTable = true;
		}
	}


	//Adrian: Fix up model string tables to make sure they match on both sides.
	if ( bFixTable == true )
	{
		int iCount = cl.m_pModelPrecacheTable->GetNumStrings();

		while ( iCount < sv.GetModelPrecacheTable()->GetNumStrings() )
		{
			string_t szString = MAKE_STRING( sv.GetModelPrecacheTable()->GetString( iCount ) );
			cl.m_pModelPrecacheTable->AddString( true, STRING( szString ) );
			iCount++;
		}
	}

	{
		CRestoreClient restore(pSaveData);
		g_pClientGameSaveRestoreBlockSet->Restore(&restore, false);
		g_pClientGameSaveRestoreBlockSet->PostRestore();
	}

	if ( r_decals.GetInt() )
	{
		for ( int i = 0; i < sections.decalcount; i++ )
		{
			decallist_t entry;
			CRestoreClient restoreHelper(pSaveData);
			restoreHelper.ReadFields( "DECALLIST", &entry, NULL, decallist_t::m_DataMap.dataDesc, decallist_t::m_DataMap.dataNumFields );

			ReapplyDecal( adjacent, table, &entry );
		}
	}

	for ( int i = 0; i < sections.musiccount; i++ )
	{
		musicsave_t song;

		CRestoreClient restoreHelper(pSaveData);
		restoreHelper.ReadFields( "MUSICLIST", &song, NULL, musicsave_t::m_DataMap.dataDesc, musicsave_t::m_DataMap.dataNumFields );

		// Tell sound system to restart the music
		S_RestartSong( &song );
	}

	Finish( pSaveData );
}

void CSaveRestore::RestoreAdjacenClientState( char const *map )
{
	char name[256];
	if ( !IsXSave() )
	{
		Q_snprintf( name, sizeof( name ), "//%s/%s%s.HL2", MOD_DIR, GetSaveDir(), GetSaveGameMapName( map ) );// DON'T FixSlashes on this, it needs to be //MOD
	}
	else
	{
		Q_snprintf( name, sizeof( name ), "%s:/%s.HL2", GetCurrentMod(), GetSaveGameMapName( map ) );// DON'T FixSlashes on this, it needs to be //MOD
	}
	COM_CreatePath( name );

	RestoreClientState( name, true );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *name - 
//-----------------------------------------------------------------------------
bool CSaveRestore::SaveClientState( const char *name )
{
#ifndef SWDS
	decallist_t		*decalList;
	int				i;

	clientsections_t	sections;

	CSaveRestoreData *pSaveData = SaveInit( 0, false );
	if ( !pSaveData )
	{
		return false;
	}
	
	sections.entitydata = pSaveData->AccessCurPos();

	// Now write out the client .dll entities to the save file, too
	g_pClientGameSaveRestoreBlockSet->PreSave( pSaveData );
	{
		CSaveClient saveHelper(pSaveData);
		g_pClientGameSaveRestoreBlockSet->Save(&saveHelper);
	}

	sections.entitysize = pSaveData->AccessCurPos() - sections.entitydata;

	sections.headerdata = pSaveData->AccessCurPos();

	{
		CSaveClient saveHelper(pSaveData);
		g_pClientGameSaveRestoreBlockSet->WriteSaveHeaders(&saveHelper);
		g_pClientGameSaveRestoreBlockSet->PostSave();
	}

	sections.headersize = pSaveData->AccessCurPos() - sections.headerdata;

	sections.decaldata = pSaveData->AccessCurPos();

	decalList = (decallist_t*)malloc( sizeof(decallist_t) * Draw_DecalMax() );
	sections.decalcount = DecalListCreate( decalList );

	for ( i = 0; i < sections.decalcount; i++ )
	{
		decallist_t *entry = &decalList[ i ];
		CSaveClient saveHelper(pSaveData);
		saveHelper.WriteFields( "DECALLIST", entry, NULL, decallist_t::m_DataMap.dataDesc, decallist_t::m_DataMap.dataNumFields );
	}

	sections.decalsize = pSaveData->AccessCurPos() - sections.decaldata;

	sections.musicdata = pSaveData->AccessCurPos();

	CUtlVector< musicsave_t >	music;

	// Ask sound system for current music tracks
	S_GetCurrentlyPlayingMusic( music );

	sections.musiccount = music.Count();

	for ( i = 0; i < sections.musiccount; ++i )
	{
		musicsave_t *song = &music[ i ];
		CSaveClient saveHelper(pSaveData);
		saveHelper.WriteFields( "MUSICLIST", song, NULL, musicsave_t::m_DataMap.dataDesc, musicsave_t::m_DataMap.dataNumFields );
	}

	sections.musicsize = pSaveData->AccessCurPos() - sections.musicdata;

	// Write string token table
	sections.symboldata = pSaveData->AccessCurPos();

	for( i = 0; i < pSaveData->SizeSymbolTable(); i++ )
	{
		const char *pszToken = (pSaveData->StringFromSymbol( i )) ? pSaveData->StringFromSymbol( i ) : "";
		if ( !pSaveData->Write( pszToken, strlen(pszToken) + 1 ) )
		{
			ConMsg( "Token Table Save/Restore overflow!" );
			break;
		}
	}	

	sections.symbolcount = pSaveData->SizeSymbolTable();
	sections.symbolsize = pSaveData->AccessCurPos() - sections.symboldata;

	int magicnumber = SECTION_MAGIC_NUMBER;
	int sectionheaderversion = SECTION_VERSION_NUMBER;

	unsigned nBytes = sizeof(CURRENT_SAVEFILE_HEADER_TAG) +
						sizeof( magicnumber ) +
						sizeof( sectionheaderversion ) + 
						sizeof( baseclientsections_t ) +
						sections.symbolsize + 
						sections.headersize + 
						sections.entitysize + 
						sections.decalsize + 
						sections.musicsize;



	void *pBuffer = new byte[nBytes];
	CUtlBuffer buffer( pBuffer, nBytes );
	buffer.Put( &CURRENT_SAVEFILE_HEADER_TAG, sizeof(CURRENT_SAVEFILE_HEADER_TAG) );
	buffer.Put( &magicnumber, sizeof( magicnumber ) );
	buffer.Put( &sectionheaderversion, sizeof( sectionheaderversion ) );
	buffer.Put( (baseclientsections_t * )&sections, sizeof( baseclientsections_t ) );
	buffer.Put( sections.symboldata, sections.symbolsize );
	buffer.Put( sections.headerdata, sections.headersize );
	buffer.Put( sections.entitydata, sections.entitysize );
	buffer.Put( sections.decaldata, sections.decalsize );
	buffer.Put( sections.musicdata, sections.musicsize );

	SaveMsg( "Queue AsyncWrite (%s)\n", name );
	g_AsyncSaveCallQueue.QueueCall( g_pSaveRestoreFileSystem, &ISaveRestoreFileSystem::AsyncWrite, CUtlEnvelope<const char *>(name), pBuffer, nBytes, true, false, (FSAsyncControl_t *)NULL );

	Finish( pSaveData );

	free( decalList );
	return true;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Parses and confirms save information. Pulled from PC UI
//-----------------------------------------------------------------------------
int CSaveRestore::SaveReadNameAndComment( FileHandle_t f, OUT_Z_CAP(nameSize) char *name, int nameSize, OUT_Z_CAP(commentSize) char *comment, int commentSize )
{
	int i, tag, size, tokenSize, tokenCount;
	char *pSaveData = NULL;
	char *pFieldName = NULL;
	char **pTokenList = NULL;

	name[0] = '\0';
	comment[0] = '\0';

	// Make sure we can at least read in the first five fields
	unsigned int tagsize = sizeof(int) * 5;
	if ( g_pSaveRestoreFileSystem->Size( f ) < tagsize )
		return 0;

	int nRead = g_pSaveRestoreFileSystem->Read( &tag, sizeof(int), f );
	if ( ( nRead != sizeof(int) ) || tag != MAKEID('J','S','A','V') )
		return 0;

	if ( g_pSaveRestoreFileSystem->Read( &tag, sizeof(int), f ) != sizeof(int) )
		return 0;

	if ( g_pSaveRestoreFileSystem->Read( &size, sizeof(int), f ) != sizeof(int) )
		return 0;

	if ( g_pSaveRestoreFileSystem->Read( &tokenCount, sizeof(int), f ) != sizeof(int) )	// These two ints are the token list
		return 0;

	if ( g_pSaveRestoreFileSystem->Read( &tokenSize, sizeof(int), f ) != sizeof(int) )
		return 0;

	size += tokenSize;

	// Sanity Check.
	if ( tokenCount < 0 || tokenCount > 1024 * 1024 * 32  )
	{
		return 0;
	}

	if ( tokenSize < 0 || tokenSize > 1024*1024*10  )
	{
		return 0;
	}


	pSaveData = (char *)new char[size];
	if ( g_pSaveRestoreFileSystem->Read(pSaveData, size, f) != size )
	{
		delete[] pSaveData;
		return 0;
	}

	int nNumberOfFields;

	char *pData;
	short nFieldSize;

	pData = pSaveData;

	// Allocate a table for the strings, and parse the table
	if ( tokenSize > 0 )
	{
		pTokenList = new char *[tokenCount];

		// Make sure the token strings pointed to by the pToken hashtable.
		for( i=0; i<tokenCount; i++ )
		{
			pTokenList[i] = *pData ? pData : NULL;	// Point to each string in the pToken table
			while( *pData++ );				// Find next token (after next null)
		}
	}
	else
		pTokenList = NULL;

	// short, short (size, index of field name)

	Q_memcpy( &nFieldSize, pData, sizeof(short) );
	pData += sizeof(short);
	short index = 0;
	Q_memcpy( &index, pData, sizeof(short) );
	pFieldName = pTokenList[index];

	if ( !pFieldName || Q_stricmp( pFieldName, "GameHeader" ) )
	{
		delete[] pSaveData;
		delete[] pTokenList;
		return 0;
	};

	// int (fieldcount)
	pData += sizeof(short);
	Q_memcpy( &nNumberOfFields, pData, sizeof(int) );
	pData += nFieldSize;

	// Each field is a short (size), short (index of name), binary string of "size" bytes (data)
	for ( i = 0; i < nNumberOfFields; ++i )
	{
		// Data order is:
		// Size
		// szName
		// Actual Data

		Q_memcpy( &nFieldSize, pData, sizeof(short) );
		pData += sizeof(short);

		Q_memcpy( &index, pData, sizeof(short) );
		pFieldName = pTokenList[index];
		pData += sizeof(short);

		if ( !Q_stricmp( pFieldName, "comment" ) )
		{
			int copySize = MAX( commentSize, nFieldSize );
			Q_strncpy( comment, pData, copySize );
		}
		else if ( !Q_stricmp( pFieldName, "mapName" ) )
		{
			int copySize = MAX( commentSize, nFieldSize );
			Q_strncpy( name, pData, copySize );
		};

		// Move to Start of next field.
		pData += nFieldSize;
	}

	// Delete the string table we allocated
	delete[] pTokenList;
	delete[] pSaveData;
	
	if ( strlen( name ) > 0 && strlen( comment ) > 0 )
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *level - 
// Output : CSaveRestoreData
//-----------------------------------------------------------------------------
CSaveRestoreData *CSaveRestore::LoadSaveData( const char *level )
{
	char			name[MAX_OSPATH];
	FileHandle_t	pFile;

	if ( !IsXSave() )
	{
		Q_snprintf( name, sizeof( name ), "//%s/%s%s.HL1", MOD_DIR, GetSaveDir(), level);// DON'T FixSlashes on this, it needs to be //MOD
	}
	else
	{
		Q_snprintf( name, sizeof( name ), "%s:/%s.HL1", GetCurrentMod(), level);// DON'T FixSlashes on this, it needs to be //MOD
	}
	ConMsg ("Loading game from %s...\n", name);

	pFile = g_pSaveRestoreFileSystem->Open( name, "rb" );
	if (!pFile)
	{
		ConMsg ("ERROR: couldn't open.\n");
		return NULL;
	}

	//---------------------------------
	// Read the header
	SaveFileHeaderTag_t tag;
	if ( g_pSaveRestoreFileSystem->Read( &tag, sizeof(tag), pFile ) != sizeof(tag) )
		return NULL;

	// Is this a valid save?
	if ( tag != CURRENT_SAVEFILE_HEADER_TAG )
		return NULL;

	//---------------------------------
	// Read the sections info and the data
	//
	SaveFileSectionsInfo_t sectionsInfo;
	
	if ( g_pSaveRestoreFileSystem->Read( &sectionsInfo, sizeof(sectionsInfo), pFile ) != sizeof(sectionsInfo) )
		return NULL;

	void *pSaveMemory = SaveAllocMemory( sizeof(CSaveRestoreData) + sectionsInfo.SumBytes(), sizeof(char) );
	if ( !pSaveMemory )
	{
		return 0;
	}

	CSaveRestoreData *pSaveData = MakeSaveRestoreData( pSaveMemory );
	Q_strncpy( pSaveData->levelInfo.szCurrentMapName, level, sizeof( pSaveData->levelInfo.szCurrentMapName ) );
	
	if ( g_pSaveRestoreFileSystem->Read( (char *)(pSaveData + 1), sectionsInfo.SumBytes(), pFile ) != sectionsInfo.SumBytes() )
	{
		// Free the memory and give up
		Finish( pSaveData );
		return NULL;
	}

	g_pSaveRestoreFileSystem->Close( pFile );
	
	//---------------------------------
	// Parse the symbol table
	char *pszTokenList = (char *)(pSaveData + 1);// Skip past the CSaveRestoreData structure

	if ( sectionsInfo.nBytesSymbols > 0 )
	{
		pSaveMemory = SaveAllocMemory( sectionsInfo.nSymbols, sizeof(char *), true );
		if ( !pSaveMemory )
		{
			SaveFreeMemory( pSaveData );
			return 0;
		}

		pSaveData->InitSymbolTable( (char**)pSaveMemory, sectionsInfo.nSymbols );

		// Make sure the token strings pointed to by the pToken hashtable.
		for( int i = 0; i<sectionsInfo.nSymbols; i++ )
		{
			if ( *pszTokenList )
			{
				Verify( pSaveData->DefineSymbol( pszTokenList, i ) );
			}
			while( *pszTokenList++ );				// Find next token (after next null)
		}
	}
	else
	{
		pSaveData->InitSymbolTable( NULL, 0 );
	}

	Assert( pszTokenList - (char *)(pSaveData + 1) == sectionsInfo.nBytesSymbols );

	//---------------------------------
	// Set up the restore basis
	int size = sectionsInfo.SumBytes() - sectionsInfo.nBytesSymbols;

	pSaveData->levelInfo.connectionCount = 0;
	pSaveData->Init( (char *)(pszTokenList), size );	// The point pszTokenList was incremented to the end of the tokens
	pSaveData->levelInfo.fUseLandmark = true;
	pSaveData->levelInfo.time = 0;
	VectorCopy( vec3_origin, pSaveData->levelInfo.vecLandmarkOffset );
	//g_ServerGlobalVariables.pSaveData = (CSaveRestoreData*)pSaveData;

	return pSaveData;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSaveData - 
//			*pHeader - 
//			updateGlobals - 
//-----------------------------------------------------------------------------
void CSaveRestore::ParseSaveTables( CSaveRestoreData *pSaveData, SAVE_HEADER *pHeader, int updateGlobals )
{
	int				i;
	SAVELIGHTSTYLE	light;
	INetworkStringTable * table = sv.GetLightStyleTable();
	
	// Re-base the savedata since we re-ordered the entity/table / restore fields
	pSaveData->Rebase();
	// Process SAVE_HEADER
	{
		CRestoreServer restoreHelper(pSaveData);
		restoreHelper.ReadFields("Save Header", pHeader, NULL, SAVE_HEADER::m_DataMap.dataDesc, SAVE_HEADER::m_DataMap.dataNumFields);
	}
	//	header.version = ENGINE_VERSION;

	pSaveData->levelInfo.mapVersion = pHeader->mapVersion;
	pSaveData->levelInfo.connectionCount = pHeader->connectionCount;
	pSaveData->levelInfo.time = pHeader->time__USE_VCR_MODE;
	pSaveData->levelInfo.fUseLandmark = true;
	VectorCopy( vec3_origin, pSaveData->levelInfo.vecLandmarkOffset );

	// Read adjacency list
	for (i = 0; i < pSaveData->levelInfo.connectionCount; i++) {
		CRestoreServer restoreHelper(pSaveData);
		restoreHelper.ReadFields( "ADJACENCY", pSaveData->levelInfo.levelList + i, NULL, levellist_t::m_DataMap.dataDesc, levellist_t::m_DataMap.dataNumFields);
	}

	if ( updateGlobals )
  	{
  		for ( i = 0; i < MAX_LIGHTSTYLES; i++ )
  			table->SetStringUserData( i, 1, "" );
  	}


	for ( i = 0; i < pHeader->lightStyleCount; i++ )
	{
		CRestoreServer restoreHelper(pSaveData);
		restoreHelper.ReadFields( "LIGHTSTYLE", &light, NULL, SAVELIGHTSTYLE::m_DataMap.dataDesc, SAVELIGHTSTYLE::m_DataMap.dataNumFields );
		if ( updateGlobals )
		{
			table->SetStringUserData( light.index, Q_strlen(light.style)+1, light.style );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Write out the list of entities that are no longer in the save file for this level
//  (they've been moved to another level)
// Input  : *pSaveData - 
//			*level - 
//-----------------------------------------------------------------------------
void CSaveRestore::EntityPatchWrite( CSaveRestoreData *pSaveData, const char *level, bool bAsync )
{
	char			name[MAX_OSPATH];
	int				i, size;

	if ( !IsXSave() )
	{
		Q_snprintf( name, sizeof( name ), "//%s/%s%s.HL3", MOD_DIR, GetSaveDir(), level);// DON'T FixSlashes on this, it needs to be //MOD
	}
	else
	{
		Q_snprintf( name, sizeof( name ), "%s:/%s.HL3", GetCurrentMod(), level);// DON'T FixSlashes on this, it needs to be //MOD
	}

	size = 0;
	for ( i = 0; i < pSaveData->NumEntities(); i++ )
	{
		if ( pSaveData->GetEntityInfo(i)->flags & FENTTABLE_REMOVED )
			size++;
	}

	int nBytesEntityPatch = sizeof(int) + size * sizeof(int);
	void *pBuffer = new byte[nBytesEntityPatch];
	CUtlBuffer buffer( pBuffer, nBytesEntityPatch );

	// Patch count
	buffer.Put( &size, sizeof(int) );
	for ( i = 0; i < pSaveData->NumEntities(); i++ )
	{
		if ( pSaveData->GetEntityInfo(i)->flags & FENTTABLE_REMOVED )
			buffer.Put( &i, sizeof(int) );
	}


	if ( !bAsync )
	{
		g_pSaveRestoreFileSystem->AsyncWrite( name, pBuffer, nBytesEntityPatch, true, false );
		g_pSaveRestoreFileSystem->AsyncFinishAllWrites();
	}
	else
	{
		SaveMsg( "Queue AsyncWrite (%s)\n", name );
		g_AsyncSaveCallQueue.QueueCall( g_pSaveRestoreFileSystem, &ISaveRestoreFileSystem::AsyncWrite, CUtlEnvelope<const char *>(name), pBuffer, nBytesEntityPatch, true, false, (FSAsyncControl_t *)NULL );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Read the list of entities that are no longer in the save file for this level (they've been moved to another level)
//   and correct the table
// Input  : *pSaveData - 
//			*level - 
//-----------------------------------------------------------------------------
void CSaveRestore::EntityPatchRead( CSaveRestoreData *pSaveData, const char *level )
{
	char			name[MAX_OSPATH];
	FileHandle_t	pFile;
	int				i, size, entityId;

	if ( !IsXSave() )
	{
		Q_snprintf(name, sizeof( name ), "//%s/%s%s.HL3", MOD_DIR, GetSaveDir(), GetSaveGameMapName( level ) );// DON'T FixSlashes on this, it needs to be //MOD
	}
	else
	{
		Q_snprintf(name, sizeof( name ), "%s:/%s.HL3", GetCurrentMod(), GetSaveGameMapName( level ) );// DON'T FixSlashes on this, it needs to be //MOD
	}

	pFile = g_pSaveRestoreFileSystem->Open( name, "rb" );
	if ( pFile )
	{
		// Patch count
		g_pSaveRestoreFileSystem->Read( &size, sizeof(int), pFile );
		for ( i = 0; i < size; i++ )
		{
			g_pSaveRestoreFileSystem->Read( &entityId, sizeof(int), pFile );
			pSaveData->GetEntityInfo(entityId)->flags = FENTTABLE_REMOVED;
		}
		g_pSaveRestoreFileSystem->Close( pFile );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *level - 
//			createPlayers - 
// Output : int
//-----------------------------------------------------------------------------
int CSaveRestore::LoadGameState( char const *level, bool createPlayers )
{
	VPROF("CSaveRestore::LoadGameState");

	SAVE_HEADER		header;
	CSaveRestoreData *pSaveData;
	pSaveData = LoadSaveData( GetSaveGameMapName( level ) );
	if ( !pSaveData )		// Couldn't load the file
		return 0;

	CRestoreServer restoreHelper(pSaveData);
	g_pServerGameSaveRestoreBlockSet->PreRestore();
	g_pServerGameSaveRestoreBlockSet->ReadRestoreHeaders( &restoreHelper);

	ParseSaveTables( pSaveData, &header, 1 );
	EntityPatchRead( pSaveData, level );
	
	if ( !IsX360() )
	{
		skill.SetValue( header.skillLevel );
	}

	Q_strncpy( sv.m_szMapname, header.mapName, sizeof( sv.m_szMapname ) );
	ConVarRef skyname( "sv_skyname" );
	if ( skyname.IsValid() )
	{
		skyname.SetValue( header.skyName );
	}
	
	// Create entity list
	CRestoreServer restore(pSaveData);
	g_pServerGameSaveRestoreBlockSet->Restore( &restore, createPlayers );
	g_pServerGameSaveRestoreBlockSet->PostRestore();

	BuildRestoredIndexTranslationTable( level, pSaveData, false );

	m_flClientSaveRestoreTime = pSaveData->levelInfo.time;

	Finish( pSaveData );

	sv.m_nTickCount = (int)( header.time__USE_VCR_MODE / host_state.interval_per_tick );
	// SUCCESS!
	return 1;
}

CSaveRestore::RestoreLookupTable *CSaveRestore::FindOrAddRestoreLookupTable( char const *mapname )
{
	int idx = m_RestoreLookup.Find( mapname );
	if ( idx == m_RestoreLookup.InvalidIndex() )
	{
		idx = m_RestoreLookup.Insert( mapname );
	}
	return &m_RestoreLookup[ idx ];
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSaveData - 
// Output : int
//-----------------------------------------------------------------------------
void CSaveRestore::BuildRestoredIndexTranslationTable( char const *mapname, CSaveRestoreData *pSaveData, bool verbose )
{
	char name[ 256 ];
	Q_FileBase( mapname, name, sizeof( name ) );
	Q_strlower( name );

	// Build Translation Lookup
	RestoreLookupTable *table = FindOrAddRestoreLookupTable( name );
	table->Clear();

	int c = pSaveData->NumEntities();
	for ( int i = 0; i < c; i++ )
	{
		entitytable_t *entry = pSaveData->GetEntityInfo( i );
		SaveRestoreTranslate slot;

		slot.classname		= entry->classname;
		slot.savedindex		= entry->saveentityindex;
		slot.restoredindex	= entry->restoreentityindex;

		table->lookup.AddToTail( slot );
	}

	table->m_vecLandMarkOffset = pSaveData->levelInfo.vecLandmarkOffset;
}

void CSaveRestore::ClearRestoredIndexTranslationTables()
{
	m_RestoreLookup.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Find all occurances of the map in the adjacency table
// Input  : *pSaveData - 
//			*pMapName - 
//			index - 
// Output : int
//-----------------------------------------------------------------------------
int EntryInTable( CSaveRestoreData *pSaveData, const char *pMapName, int index )
{
	int i;

	index++;
	for ( i = index; i < pSaveData->levelInfo.connectionCount; i++ )
	{
		if ( !stricmp( pSaveData->levelInfo.levelList[i].mapName, pMapName ) )
			return i;
	}

	return -1;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSaveData - 
//			output - 
//			*pLandmarkName - 
//-----------------------------------------------------------------------------
void LandmarkOrigin( CSaveRestoreData *pSaveData, Vector& output, const char *pLandmarkName )
{
	int i;

	for ( i = 0; i < pSaveData->levelInfo.connectionCount; i++ )
	{
		if ( !stricmp( pSaveData->levelInfo.levelList[i].landmarkName, pLandmarkName ) )
		{
			VectorCopy( pSaveData->levelInfo.levelList[i].vecLandmarkOrigin, output );
			return;
		}
	}

	VectorCopy( vec3_origin, output );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pOldLevel - 
//			*pLandmarkName - 
//-----------------------------------------------------------------------------
void CSaveRestore::LoadAdjacentEnts( const char *pOldLevel, const char *pLandmarkName )
{
	FinishAsyncSave();

	CSaveRestoreData currentLevelData, *pSaveData;
	int				i, test, flags, index, movedCount = 0;
	SAVE_HEADER		header;
	Vector			landmarkOrigin;

	memset( &currentLevelData, 0, sizeof(CSaveRestoreData) );
	//g_ServerGlobalVariables.pSaveData = &currentLevelData;
	// Build the adjacent map list
	CSaveServer saveHelper(&currentLevelData);
	serverEntitylist->BuildAdjacentMapList(&saveHelper);
	bool foundprevious = false;

	for ( i = 0; i < currentLevelData.levelInfo.connectionCount; i++ )
	{
		// make sure the previous level is in the connection list so we can
		// bring over the player.
		if ( !strcmpi( currentLevelData.levelInfo.levelList[i].mapName, pOldLevel ) )
		{
			foundprevious = true;
		}

		for ( test = 0; test < i; test++ )
		{
			// Only do maps once
			if ( !stricmp( currentLevelData.levelInfo.levelList[i].mapName, currentLevelData.levelInfo.levelList[test].mapName ) )
				break;
		}
		// Map was already in the list
		if ( test < i )
			continue;

//		ConMsg("Merging entities from %s ( at %s )\n", currentLevelData.levelInfo.levelList[i].mapName, currentLevelData.levelInfo.levelList[i].landmarkName );
		pSaveData = LoadSaveData( GetSaveGameMapName( currentLevelData.levelInfo.levelList[i].mapName ) );

		if ( pSaveData )
		{
			CRestoreServer restoreHelper(pSaveData);
			g_pServerGameSaveRestoreBlockSet->PreRestore();
			g_pServerGameSaveRestoreBlockSet->ReadRestoreHeaders( &restoreHelper);

			ParseSaveTables( pSaveData, &header, 0 );
			EntityPatchRead( pSaveData, currentLevelData.levelInfo.levelList[i].mapName );
			pSaveData->levelInfo.time = sv.GetTime();// - header.time;
			pSaveData->levelInfo.fUseLandmark = true;
			flags = 0;
			LandmarkOrigin( &currentLevelData, landmarkOrigin, pLandmarkName );
			LandmarkOrigin( pSaveData, pSaveData->levelInfo.vecLandmarkOffset, pLandmarkName );
			VectorSubtract( landmarkOrigin, pSaveData->levelInfo.vecLandmarkOffset, pSaveData->levelInfo.vecLandmarkOffset );
			if ( !stricmp( currentLevelData.levelInfo.levelList[i].mapName, pOldLevel ) )
				flags |= FENTTABLE_PLAYER;

			index = -1;
			while ( 1 )
			{
				index = EntryInTable( pSaveData, sv.GetMapName(), index );
				if ( index < 0 )
					break;
				flags |= 1<<index;
			}
			
			if ( flags )
				movedCount = serverEntitylist->CreateEntityTransitionList(&restoreHelper, flags );

			// If ents were moved, rewrite entity table to save file
			if ( movedCount )
				EntityPatchWrite( pSaveData, GetSaveGameMapName( currentLevelData.levelInfo.levelList[i].mapName ) );

			BuildRestoredIndexTranslationTable( currentLevelData.levelInfo.levelList[i].mapName, pSaveData, true );

			Finish( pSaveData );
		}
	}
	//g_ServerGlobalVariables.pSaveData = NULL;
	if ( !foundprevious )
	{
		// Host_Error( "Level transition ERROR\nCan't find connection to %s from %s\n", pOldLevel, sv.GetMapName() );
		Warning( "Level transition ERROR\nCan't find connection to %s from %s\n", pOldLevel, sv.GetMapName() );
		Cbuf_AddText( "disconnect\n" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pFile - 
// Output : int
//-----------------------------------------------------------------------------
int CSaveRestore::FileSize( FileHandle_t pFile )
{
	if ( !pFile )
		return 0;

	return g_pSaveRestoreFileSystem->Size(pFile);
}

//-----------------------------------------------------------------------------
// Purpose: Copies the contents of the save directory into a single file
//-----------------------------------------------------------------------------
void CSaveRestore::DirectoryCopy( const char *pPath, const char *pDestFileName, bool bIsXSave )
{
	SaveMsg( "Directory copy (%s)\n", pPath );

	g_pSaveRestoreFileSystem->AsyncFinishAllWrites();
	int nMaps = g_pSaveRestoreFileSystem->DirectoryCount( pPath );
	FileHandle_t hFile = g_pSaveRestoreFileSystem->Open( pDestFileName, "ab+" );
	if ( hFile )
	{
		g_pSaveRestoreFileSystem->Write( &nMaps, sizeof(nMaps), hFile );
		g_pSaveRestoreFileSystem->Close( hFile );
		g_pSaveRestoreFileSystem->DirectoryCopy( pPath, pDestFileName, bIsXSave );
	}
	else
	{
		Warning( "Invalid save, failed to open file\n" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Extracts all the files contained within pFile
//-----------------------------------------------------------------------------
bool CSaveRestore::DirectoryExtract( FileHandle_t pFile, int fileCount )
{
	return g_pSaveRestoreFileSystem->DirectoryExtract( pFile, fileCount, IsXSave() );
}

//-----------------------------------------------------------------------------
// Purpose: returns the number of save files in the specified filter
//-----------------------------------------------------------------------------
void CSaveRestore::DirectoryCount( const char *pPath, int *pResult )
{
	LOCAL_THREAD_LOCK();
	if ( *pResult == -1 )
		*pResult = g_pSaveRestoreFileSystem->DirectoryCount( pPath );
	// else already set by worker thread
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPath - 
//-----------------------------------------------------------------------------
void CSaveRestore::DirectoryClear( const char *pPath )
{
	g_pSaveRestoreFileSystem->DirectoryClear( pPath, IsXSave() );
}


//-----------------------------------------------------------------------------
// Purpose: deletes all the partial save files from the save game directory
//-----------------------------------------------------------------------------
void CSaveRestore::ClearSaveDir( void )
{
	m_bClearSaveDir = true;
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
void CSaveRestore::DoClearSaveDir( bool bIsXSave )
{
	// before we clear the save dir, we need to make sure that 
	// any async-written save games have finished writing, 
	// since we still may need these temp files to write the save game

	char szName[MAX_OSPATH];

	if ( !bIsXSave )
	{
		Q_snprintf(szName, sizeof( szName ), "%s", GetSaveDir() );
		Q_FixSlashes( szName );
		// Create save directory if it doesn't exist
		Sys_mkdir( szName );
	}
	else
	{
		Q_snprintf( szName, sizeof( szName ), "%s:\\", GetCurrentMod() );
	}

	Q_strncat( szName, "*.HL?", sizeof( szName ), COPY_ALL_CHARACTERS );
	DirectoryClear( szName );
}

void CSaveRestore::RequestClearSaveDir( void )
{
	m_bClearSaveDir = true;
}

void CSaveRestore::OnFinishedClientRestore()
{
	//g_ClientDLL->DispatchOnRestore();

	ClearRestoredIndexTranslationTables();

	if ( m_bClearSaveDir )
	{
		m_bClearSaveDir = false;
		FinishAsyncSave();
		DoClearSaveDir( IsXSave() );
	}
}

void CSaveRestore::AutoSaveDangerousIsSafe()
{
	if ( save_async.GetBool() && ThreadInMainThread() && g_pSaveThread )
	{
		g_pSaveThread->QueueCall(  this, &CSaveRestore::FinishAsyncSave );

		g_pSaveThread->QueueCall(  this, &CSaveRestore::AutoSaveDangerousIsSafe );

		return;
	}

	if ( !m_bWaitingForSafeDangerousSave )
		return;

	m_bWaitingForSafeDangerousSave = false;

	ConDMsg( "Committing autosavedangerous...\n" );

	char szOldName[MAX_PATH];
	char szNewName[MAX_PATH];

	// Back up the old autosaves
	if ( StorageDeviceValid() )
	{
		AgeSaveList( "autosave", save_history_count.GetInt(), IsXSave() );
	}

	// Rename the screenshot
	if ( !IsX360() )
	{
		Q_snprintf( szOldName, sizeof( szOldName ), "//%s/%sautosavedangerous%s.tga", MOD_DIR, GetSaveDir(), GetPlatformExt() );
		Q_snprintf( szNewName, sizeof( szNewName ), "//%s/%sautosave%s.tga", MOD_DIR, GetSaveDir(), GetPlatformExt() );

		// there could be an old version, remove it
		if ( g_pFileSystem->FileExists( szNewName ) )
		{
			g_pFileSystem->RemoveFile( szNewName );
		}

		if ( g_pFileSystem->FileExists( szOldName ) )
		{
			if ( !g_pFileSystem->RenameFile( szOldName, szNewName ) )
			{
				SetMostRecentSaveGame( "autosavedangerous" );
				return;
			}
		}
	}

	// Rename the dangerous auto save as a normal auto save
	if ( !IsXSave() )
	{
		Q_snprintf( szOldName, sizeof( szOldName ), "//%s/%sautosavedangerous%s.sav", MOD_DIR, GetSaveDir(), GetPlatformExt() );
		Q_snprintf( szNewName, sizeof( szNewName ), "//%s/%sautosave%s.sav", MOD_DIR, GetSaveDir(), GetPlatformExt() );
	}
	else
	{
		Q_snprintf( szOldName, sizeof( szOldName ), "%s:\\autosavedangerous%s.sav", GetCurrentMod(), GetPlatformExt() );
		Q_snprintf( szNewName, sizeof( szNewName ), "%s:\\autosave%s.sav", GetCurrentMod(), GetPlatformExt() );
	}

	// there could be an old version, remove it
	if ( g_pFileSystem->FileExists( szNewName ) )
	{
		g_pFileSystem->RemoveFile( szNewName );
	}

	if ( !g_pFileSystem->RenameFile( szOldName, szNewName ) )
	{
		SetMostRecentSaveGame( "autosavedangerous" );
		return;
	}

	// Use this as the most recent now that it's safe
	SetMostRecentSaveGame( "autosave" );

	// Finish off all writes
	if ( IsXSave() )
	{
		g_pXboxSystem->FinishContainerWrites();
	}
}

static void SaveGame( const CCommand &args )
{
	bool bFinishAsync = false;
	bool bSetMostRecent = true;
	bool bRenameMap = false;
	if ( args.ArgC() > 2 )
	{
		for ( int i = 2; i < args.ArgC(); i++ )
		{
			if ( !Q_stricmp( args[i], "wait" ) )
			{
				bFinishAsync = true;
			}
			else if ( !Q_stricmp(args[i], "notmostrecent"))
			{
				bSetMostRecent = false;
			}
			else if ( !Q_stricmp( args[i], "copymap" ) )
			{
				bRenameMap = true;
			}
		}
	}

	char szMapName[MAX_PATH];
	if ( bRenameMap )
	{
		// HACK: The bug is going to make a copy of this map, so replace the global state to
		// fool the system
		Q_strncpy( szMapName, sv.m_szMapname, sizeof(szMapName) );
		Q_strncpy( sv.m_szMapname, args[1], sizeof(sv.m_szMapname) );
	}

	int iAdditionalSeconds = g_ServerGlobalVariables.curtime - saverestore->GetMostRecentElapsedTimeSet();
	int iAdditionalMinutes = iAdditionalSeconds / 60;
	iAdditionalSeconds -= iAdditionalMinutes * 60;

	char comment[80];
	GetServerSaveCommentEx( 
		comment, 
		sizeof( comment ), 
		saverestore->GetMostRecentElapsedMinutes() + iAdditionalMinutes,
		saverestore->GetMostRecentElapsedSeconds() + iAdditionalSeconds );

	saverestore->SaveGameSlot( args[1], comment, false, bSetMostRecent );

	if ( bFinishAsync )
	{
		FinishAsyncSave();
	}

	if ( bRenameMap )
	{
		// HACK: Put the original name back
		Q_strncpy( sv.m_szMapname, szMapName, sizeof(sv.m_szMapname) );
	}

#if !defined (SWDS)
	CL_HudMessage( IsX360() ? "GAMESAVED_360" : "GAMESAVED" );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : void Host_Savegame_f
//-----------------------------------------------------------------------------
CON_COMMAND_F( save, "Saves current game.", FCVAR_DONTRECORD )
{
	// Can we save at this point?
	if ( !saverestore->IsValidSave() )
		return;

	if ( args.ArgC() < 2 )
	{
		ConDMsg("save <savename> [wait]: save a game\n");
		return;
	}

	if ( strstr(args[1], ".." ) )
	{
		ConDMsg ("Relative pathnames are not allowed.\n");
		return;
	}

	if ( strstr(sv.m_szMapname, "background" ) )
	{
		ConDMsg ("\"background\" is a reserved map name and cannot be saved or loaded.\n");
		return;
	}

	g_SaveRestore.SetIsXSave( false );
	SaveGame( args );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : void Host_Savegame_f
//-----------------------------------------------------------------------------
CON_COMMAND_F( xsave, "Saves current game to a 360 storage device.", FCVAR_DONTRECORD )
{
	// Can we save at this point?
	if ( !saverestore->IsValidSave() )
		return;

	if ( args.ArgC() < 2 )
	{
		ConDMsg("save <savename> [wait]: save a game\n");
		return;
	}

	if ( strstr(args[1], ".." ) )
	{
		ConDMsg ("Relative pathnames are not allowed.\n");
		return;
	}

	if ( strstr(sv.m_szMapname, "background" ) )
	{
		ConDMsg ("\"background\" is a reserved map name and cannot be saved or loaded.\n");
		return;
	}

	g_SaveRestore.SetIsXSave( IsX360() );
	SaveGame( args );
}

//-----------------------------------------------------------------------------
// Purpose: saves the game, but only includes the state for the current level
//			useful for bug reporting.
// Output : 
//-----------------------------------------------------------------------------
CON_COMMAND_F( minisave, "Saves game (for current level only!)", FCVAR_DONTRECORD )
{
	// Can we save at this point?
	if ( !saverestore->IsValidSave() )
		return;

	if (args.ArgC() != 2 || strstr(args[1], ".."))
		return;

	int iAdditionalSeconds = g_ServerGlobalVariables.curtime - saverestore->GetMostRecentElapsedTimeSet();
	int iAdditionalMinutes = iAdditionalSeconds / 60;
	iAdditionalSeconds -= iAdditionalMinutes * 60;

	char comment[80];
	GetServerSaveCommentEx( 
		comment, 
		sizeof( comment ),
		saverestore->GetMostRecentElapsedMinutes() + iAdditionalMinutes,
		saverestore->GetMostRecentElapsedSeconds() + iAdditionalSeconds );
	saverestore->SaveGameSlot( args[1], comment, true, true );
}

static void AutoSave_Silent( bool bDangerous )
{
	// Can we save at this point?
	if ( !saverestore->IsValidSave() )
		return;

	int iAdditionalSeconds = g_ServerGlobalVariables.curtime - saverestore->GetMostRecentElapsedTimeSet();
	int iAdditionalMinutes = iAdditionalSeconds / 60;
	iAdditionalSeconds -= iAdditionalMinutes * 60;

	char comment[80];
	GetServerSaveCommentEx( 
		comment, 
		sizeof( comment ),
		saverestore->GetMostRecentElapsedMinutes() + iAdditionalMinutes,
		saverestore->GetMostRecentElapsedSeconds() + iAdditionalSeconds );

	g_SaveRestore.SetIsXSave( IsX360() );
	if ( !bDangerous )
	{
		saverestore->SaveGameSlot( "autosave", comment, false, true );
	}
	else
	{
		saverestore->SaveGameSlot( "autosavedangerous", comment, false, false );
	}
}

static ConVar save_console( "save_console", "0", 0, "Autosave on the PC behaves like it does on the consoles." );
static ConVar save_huddelayframes( "save_huddelayframes", "1", 0, "Number of frames to defer for drawing the Saving message." );

CON_COMMAND( _autosave, "Autosave" )
{
	AutoSave_Silent( false );
	bool bConsole = save_console.GetBool();
#if defined ( _X360 )
	bConsole = true;
#endif
	if ( bConsole )
	{
#if !defined (SWDS)
		CL_HudMessage( IsX360() ? "GAMESAVED_360" : "GAMESAVED" );
#endif
	}
}

CON_COMMAND( _autosavedangerous, "AutoSaveDangerous" )
{
	// Don't even bother if we've got an invalid save
	if ( saverestore->StorageDeviceValid() == false )
		return;

	AutoSave_Silent( true );
	bool bConsole = save_console.GetBool();
#if defined ( _X360 )
	bConsole = true;
#endif
	if ( bConsole )
	{
#if !defined (SWDS)
		CL_HudMessage( IsX360() ? "GAMESAVED_360" : "GAMESAVED" );
#endif
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : void Host_AutoSave_f
//-----------------------------------------------------------------------------
CON_COMMAND( autosave, "Autosave" )
{
	// Can we save at this point?
	if ( !saverestore->IsValidSave() || !sv_autosave.GetBool() )
		return;

	bool bConsole = save_console.GetBool();
	char const *pchSaving = IsX360() ? "GAMESAVING_360" : "GAMESAVING";
#if defined ( _X360 )
	bConsole = true;
#endif

	if ( bConsole )
	{
#if !defined (SWDS)
		CL_HudMessage( pchSaving );
#endif
		g_SaveRestore.AddDeferredCommand( "_autosave" );
	}
	else
	{
		AutoSave_Silent( false );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : void Host_AutoSaveDangerous_f
//-----------------------------------------------------------------------------
CON_COMMAND( autosavedangerous, "AutoSaveDangerous" )
{
	// Can we save at this point?
	if ( !saverestore->IsValidSave() || !sv_autosave.GetBool() )
		return;

	// Don't even bother if we've got an invalid save
	if ( saverestore->StorageDeviceValid() == false )
		return;

	//Don't print out "SAVED" unless we're running on an Xbox (in which case it prints "CHECKPOINT").
	bool bConsole = save_console.GetBool();
	char const *pchSaving = IsX360() ? "GAMESAVING_360" : "GAMESAVING";
#if defined ( _X360 )
	bConsole = true;
#endif

	if ( bConsole )
	{
#if !defined (SWDS)
		CL_HudMessage( pchSaving );
#endif
		g_SaveRestore.AddDeferredCommand( "_autosavedangerous" );
	}
	else
	{
		AutoSave_Silent( true );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : void Host_AutoSaveSafe_f
//-----------------------------------------------------------------------------
CON_COMMAND( autosavedangerousissafe, "" )
{
	saverestore->AutoSaveDangerousIsSafe();	
}

//-----------------------------------------------------------------------------
// Purpose: Load a save game in response to a console command (load or xload)
//-----------------------------------------------------------------------------
static void LoadSaveGame( const char *savename )
{
	// Make sure the freaking save file exists....
	if ( !saverestore->SaveFileExists( savename ) )
	{
		Warning( "Can't load '%s', file missing!\n", savename );
		return;
	}

	GetTestScriptMgr()->SetWaitCheckPoint( "load_game" );

	// if we're not currently in a game, show progress
	if ( !sv.IsActive() || sv.IsLevelMainMenuBackground() )
	{
		EngineVGui()->EnabledProgressBarForNextLoad();
	}

	// Put up loading plaque
	SCR_BeginLoadingPlaque();

	Host_Disconnect( false );	// stop old game

	HostState_LoadGame( savename, false );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : void Host_Loadgame_f
//-----------------------------------------------------------------------------
void Host_Loadgame_f( const CCommand &args )
{
	if ( cmd_source != src_command )
		return;

	if ( sv.IsMultiplayer() )
	{
		ConMsg ("Can't load in multiplayer games.\n");
		return;
	}

	if (args.ArgC() < 2)
	{
		ConMsg ("load <savename> : load a game\n");
		return;
	}

	g_szMapLoadOverride[0] = 0;

	if ( args.ArgC() > 2)
	{
		V_strncpy( g_szMapLoadOverride, args[2], sizeof( g_szMapLoadOverride ) );
	}

	g_SaveRestore.SetIsXSave( false );
	LoadSaveGame( args[1] );
}

// Always loads saves from DEFAULT_WRITE_PATH, regardless of platform
CON_COMMAND_AUTOCOMPLETEFILE( load, Host_Loadgame_f, "Load a saved game.", "save", sav );

// Loads saves from the 360 storage device
CON_COMMAND( xload, "Load a saved game from a 360 storage device." )
{
	if ( sv.IsMultiplayer() )
	{
		ConMsg ("Can't load in multiplayer games.\n");
		return;
	}
	if (args.ArgC() != 2)
	{
		ConMsg ("xload <savename>\n");
		return;
	}

	g_SaveRestore.SetIsXSave( IsX360() );
	LoadSaveGame( args[1] );
}

CON_COMMAND( save_finish_async, "" )
{
	FinishAsyncSave();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSaveRestore::Init( void )
{
	int minplayers = 1;
	// serverGameClients should have been initialized by the CModAppSystemGroup Create method (so it's before the Host_Init stuff which calls this)
	Assert( serverGameClients );
	if ( serverGameClients )
	{
		int dummy = 1;
		int dummy2 = 1;
		serverGameClients->GetPlayerLimits( minplayers, dummy, dummy2 );
	}

	if ( !serverGameClients || 
		( minplayers == 1 ) )
	{
		GetSaveMemory();

		Assert( !g_pSaveThread );

		ThreadPoolStartParams_t threadPoolStartParams;
		threadPoolStartParams.nThreads = 1;
		if ( !IsX360() )
		{
			threadPoolStartParams.fDistribute = TRS_FALSE;
		}
		else
		{
			threadPoolStartParams.iAffinityTable[0] = XBOX_PROCESSOR_1;
			threadPoolStartParams.bUseAffinityTable = true;
		}

		g_pSaveThread = CreateThreadPool();
		g_pSaveThread->Start( threadPoolStartParams, "SaveJob" );
	}

	m_nDeferredCommandFrames = 0;
	m_szSaveGameScreenshotFile[0] = 0;
	if ( !IsX360() && !CommandLine()->FindParm( "-noclearsave" ) )
	{
		ClearSaveDir();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSaveRestore::Shutdown( void )
{
	FinishAsyncSave();
	if ( g_pSaveThread )
	{
		g_pSaveThread->Stop();
		g_pSaveThread->Release();
		g_pSaveThread = NULL;
	}
	m_szSaveGameScreenshotFile[0] = 0;
}

char const *CSaveRestore::GetMostRecentlyLoadedFileName()
{
	return m_szMostRecentSaveLoadGame;
}

char const *CSaveRestore::GetSaveFileName()
{
	return m_szSaveGameName;
}

void CSaveRestore::AddDeferredCommand( char const *pchCommand )
{
	m_nDeferredCommandFrames = clamp( save_huddelayframes.GetInt(), 0, 10 );
	CUtlSymbol sym;
	sym = pchCommand;
	m_sDeferredCommands.AddToTail( sym );
}

void CSaveRestore::OnFrameRendered()
{
	if ( m_nDeferredCommandFrames > 0 )
	{
		--m_nDeferredCommandFrames;
		if ( m_nDeferredCommandFrames == 0 )
		{
			// Dispatch deferred command
			for ( int i = 0; i < m_sDeferredCommands.Count(); ++i )
			{
				Cbuf_AddText( m_sDeferredCommands[ i ].String() );
			}
			m_sDeferredCommands.Purge();
		}
	}
}

bool CSaveRestore::StorageDeviceValid( void )
{
	// PC is always valid
	if ( !IsX360() )
		return true;

	// Non-XSaves are always valid
	if ( !IsXSave() )
		return true;

#ifdef _X360
	// Otherwise, we must have a real storage device
	int nStorageDeviceID = XBX_GetStorageDeviceId();
	return ( nStorageDeviceID != XBX_INVALID_STORAGE_ID && nStorageDeviceID != XBX_STORAGE_DECLINED );
#endif

	return true;
}

bool CSaveRestore::IsSaveInProgress()
{
	return g_bSaveInProgress;
}

