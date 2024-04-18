//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
#if !defined( HOST_SAVERESTORE_H )
#define HOST_SAVERESTORE_H
#ifdef _WIN32
#pragma once
#endif

#include "isaverestore.h"
#include "saverestoretypes.h"

class CSaveRestoreData;
struct model_t;

//-----------------------------------------------------------------------------
//
// CSave
//
//-----------------------------------------------------------------------------

class CSave : public ISave
{
public:
	CSave(CSaveRestoreData* pdata);

	//---------------------------------
	// Logging
	void			StartLogging(const char* pszLogName);
	void			EndLogging(void);

	//---------------------------------
	bool			IsAsync();

	//---------------------------------

	int				GetWritePos() const;
	void			SetWritePos(int pos);

	//---------------------------------
	// Datamap based writing
	//

	int				WriteAll(const void* pLeafObject, datamap_t* pLeafMap) { return DoWriteAll(pLeafObject, pLeafMap, pLeafMap); }

	int				WriteFields(const char* pname, const void* pBaseData, datamap_t* pMap, typedescription_t* pFields, int fieldCount);

	//---------------------------------
	// Block support
	//

	virtual void	StartBlock(const char* pszBlockName);
	virtual void	StartBlock();
	virtual void	EndBlock();

	//---------------------------------
	// Primitive types
	//

	void			WriteShort(const short* value, int count = 1);
	void			WriteInt(const int* value, int count = 1);		                           // Save an int
	void			WriteBool(const bool* value, int count = 1);		                           // Save a bool
	void			WriteFloat(const float* value, int count = 1);	                           // Save a float
	void			WriteData(const char* pdata, int size);		                               // Save a binary data block
	void			WriteString(const char* pstring);			                                   // Save a null-terminated string
	void			WriteString(const string_t* stringId, int count = 1);	                       // Save a null-terminated string (engine string)
	void			WriteVector(const Vector& value);				                               // Save a vector
	void			WriteVector(const Vector* value, int count = 1);	                           // Save a vector array
	void			WriteQuaternion(const Quaternion& value);				                        // Save a Quaternion
	void			WriteQuaternion(const Quaternion* value, int count = 1);	                    // Save a Quaternion array
	void			WriteVMatrix(const VMatrix* value, int count = 1);							// Save a vmatrix array

	// Note: All of the following will write out both a header and the data. On restore,
	// this needs to be cracked
	void			WriteShort(const char* pname, const short* value, int count = 1);
	void			WriteInt(const char* pname, const int* value, int count = 1);		           // Save an int
	void			WriteBool(const char* pname, const bool* value, int count = 1);		       // Save a bool
	void			WriteFloat(const char* pname, const float* value, int count = 1);	           // Save a float
	void			WriteData(const char* pname, int size, const char* pdata);		           // Save a binary data block
	void			WriteString(const char* pname, const char* pstring);			               // Save a null-terminated string
	void			WriteString(const char* pname, const string_t* stringId, int count = 1);	   // Save a null-terminated string (engine string)
	void			WriteVector(const char* pname, const Vector& value);				           // Save a vector
	void			WriteVector(const char* pname, const Vector* value, int count = 1);	       // Save a vector array
	void			WriteQuaternion(const char* pname, const Quaternion& value);				   // Save a Quaternion
	void			WriteQuaternion(const char* pname, const Quaternion* value, int count = 1);  // Save a Quaternion array
	void			WriteVMatrix(const char* pname, const VMatrix* value, int count = 1);
	//---------------------------------
	// Game types
	//

	void			WriteTime(const char* pname, const float* value, int count = 1);	           // Save a float (timevalue)
	void			WriteTick(const char* pname, const int* value, int count = 1);	           // Save a int (timevalue)
	void			WritePositionVector(const char* pname, const Vector& value);		           // Offset for landmark if necessary
	void			WritePositionVector(const char* pname, const Vector* value, int count = 1);  // array of pos vectors
	void			WriteFunction(datamap_t* pMap, const char* pname, inputfunc_t** value, int count = 1); // Save a function pointer

	void			WriteEntityPtr(const char* pname, IHandleEntity** ppEntity, int count = 1);
	//void			WriteEdictPtr( const char *pname, edict_t **ppEdict, int count = 1 );
	virtual void	WriteEHandle(const char* pname, const CBaseHandle* pEHandle, int count = 1) = 0;

	virtual void	WriteTime(const float* value, int count = 1);	// Save a float (timevalue)
	virtual void	WriteTick(const int* value, int count = 1);	// Save a int (timevalue)
	virtual void	WritePositionVector(const Vector& value);		// Offset for landmark if necessary
	virtual void	WritePositionVector(const Vector* value, int count = 1);	// array of pos vectors

	virtual void	WriteEntityPtr(IHandleEntity** ppEntity, int count = 1);
	//virtual void	WriteEdictPtr( edict_t **ppEdict, int count = 1 );
	virtual void	WriteEHandle(const CBaseHandle* pEHandle, int count = 1) = 0;
	void			WriteVMatrixWorldspace(const char* pname, const VMatrix* value, int count = 1);	       // Save a vmatrix array
	void			WriteVMatrixWorldspace(const VMatrix* value, int count = 1);	       // Save a vmatrix array
	void			WriteMatrix3x4Worldspace(const matrix3x4_t* value, int count);
	void			WriteMatrix3x4Worldspace(const char* pname, const matrix3x4_t* value, int count);

	void			WriteInterval(const interval_t* value, int count = 1);						// Save an interval
	void			WriteInterval(const char* pname, const interval_t* value, int count = 1);

	//---------------------------------

	int				EntityIndex(const IHandleEntity* pEntity);
	int				EntityFlagsSet(int entityIndex, int flags);

	CGameSaveRestoreInfo* GetGameSaveRestoreInfo() { return m_pGameInfo; }

protected:
	virtual const model_t* GetModel(int modelindex) = 0;
	virtual const char* GetModelName(const model_t* model) const = 0;
	virtual const char* GetMaterialNameFromIndex(int nMateralIndex) = 0;
	virtual string_t AllocPooledString(const char* pszValue) = 0;
private:

	//---------------------------------
	bool			IsLogging(void);
	void			Log(const char* pName, fieldtype_t fieldType, void* value, int count);

	//---------------------------------

	void			BufferField(const char* pname, int size, const char* pdata);
	void			BufferData(const char* pdata, int size);
	void			WriteHeader(const char* pname, int size);

	int				DoWriteAll(const void* pLeafObject, datamap_t* pLeafMap, datamap_t* pCurMap);
	bool 			WriteField(const char* pname, void* pData, datamap_t* pRootMap, typedescription_t* pField);

	bool 			WriteBasicField(const char* pname, void* pData, datamap_t* pRootMap, typedescription_t* pField);

	int				DataEmpty(const char* pdata, int size);
	void			BufferString(char* pdata, int len);

	int				CountFieldsToSave(const void* pBaseData, typedescription_t* pFields, int fieldCount);
	bool			ShouldSaveField(const void* pData, typedescription_t* pField);

	//---------------------------------
	// Game info methods
	//

	bool			WriteGameField(const char* pname, void* pData, datamap_t* pRootMap, typedescription_t* pField);
	//int				EntityIndex( const edict_t *pentLookup );

	//---------------------------------

	CUtlVector<int> m_BlockStartStack;

	// Stream data
	CSaveRestoreSegment* m_pData;

	// Game data
	CGameSaveRestoreInfo* m_pGameInfo;

	FileHandle_t		m_hLogFile;
	bool				m_bAsync;
};

class CSaveServer : public CSave {
public:
	CSaveServer(CSaveRestoreData* pdata);
	virtual void	WriteEHandle(const char* pname, const CBaseHandle* pEHandle, int count = 1);
	virtual void	WriteEHandle(const CBaseHandle* pEHandle, int count = 1);
	virtual const model_t* GetModel(int modelindex);
	virtual const char* GetModelName(const model_t* model) const;
	virtual const char* GetMaterialNameFromIndex(int nMateralIndex);
	virtual string_t AllocPooledString(const char* pszValue);
};

class CSaveClient : public CSave {
public:
	CSaveClient(CSaveRestoreData* pdata);
	virtual void	WriteEHandle(const char* pname, const CBaseHandle* pEHandle, int count = 1);
	virtual void	WriteEHandle(const CBaseHandle* pEHandle, int count = 1);
	virtual const model_t* GetModel(int modelindex);
	virtual const char* GetModelName(const model_t* model) const;
	virtual const char* GetMaterialNameFromIndex(int nMateralIndex);
	virtual string_t AllocPooledString(const char* pszValue);
};

//-----------------------------------------------------------------------------
//
// CRestore
//
//-----------------------------------------------------------------------------

class CRestore : public IRestore
{
public:
	CRestore(CSaveRestoreData* pdata);

	int				GetReadPos() const;
	void			SetReadPos(int pos);

	//---------------------------------
	// Datamap based reading
	//

	int				ReadAll(void* pLeafObject, datamap_t* pLeafMap) { return DoReadAll(pLeafObject, pLeafMap, pLeafMap); }

	int				ReadFields(const char* pname, void* pBaseData, datamap_t* pMap, typedescription_t* pFields, int fieldCount);
	void 			EmptyFields(void* pBaseData, typedescription_t* pFields, int fieldCount);

	//---------------------------------
	// Block support
	//

	virtual void	StartBlock(SaveRestoreRecordHeader_t* pHeader);
	virtual void	StartBlock(char szBlockName[SIZE_BLOCK_NAME_BUF]);
	virtual void	StartBlock();
	virtual void	EndBlock();

	//---------------------------------
	// Field header cracking
	//

	void			ReadHeader(SaveRestoreRecordHeader_t* pheader);
	int				SkipHeader() { SaveRestoreRecordHeader_t header; ReadHeader(&header); return header.size; }
	const char* StringFromHeaderSymbol(int symbol);

	//---------------------------------
	// Primitive types
	//

	short			ReadShort(void);
	int				ReadShort(short* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadInt(int* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadInt(void);
	int 			ReadBool(bool* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadFloat(float* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadData(char* pData, int size, int nBytesAvailable);
	void			ReadString(char* pDest, int nSizeDest, int nBytesAvailable);			// A null-terminated string
	int				ReadString(string_t* pString, int count = 1, int nBytesAvailable = 0);
	int				ReadVector(Vector* pValue);
	int				ReadVector(Vector* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadQuaternion(Quaternion* pValue);
	int				ReadQuaternion(Quaternion* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadVMatrix(VMatrix* pValue, int count = 1, int nBytesAvailable = 0);

	//---------------------------------
	// Game types
	//

	int				ReadTime(float* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadTick(int* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadPositionVector(Vector* pValue);
	int				ReadPositionVector(Vector* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadFunction(datamap_t* pMap, inputfunc_t** pValue, int count = 1, int nBytesAvailable = 0);

	int 			ReadEntityPtr(IHandleEntity** ppEntity, int count = 1, int nBytesAvailable = 0);
	//int				ReadEdictPtr( edict_t **ppEdict, int count = 1, int nBytesAvailable = 0 );
	int				ReadEHandle(CBaseHandle* pEHandle, int count = 1, int nBytesAvailable = 0);
	int				ReadVMatrixWorldspace(VMatrix* pValue, int count = 1, int nBytesAvailable = 0);
	int				ReadMatrix3x4Worldspace(matrix3x4_t* pValue, int nElems = 1, int nBytesAvailable = 0);
	int				ReadInterval(interval_t* interval, int count = 1, int nBytesAvailable = 0);

	//---------------------------------

	void			SetGlobalMode(int global) { m_global = global; }
	void			PrecacheMode(bool mode) { m_precache = mode; }
	bool			GetPrecacheMode(void) { return m_precache; }

	CGameSaveRestoreInfo* GetGameSaveRestoreInfo() { return m_pGameInfo; }

protected:
	virtual int	GetModelIndex(const char* name) = 0;
	virtual void PrecacheModel(const char* pModelName) = 0;
	virtual int GetMaterialIndex(const char* pMaterialName) = 0;
	virtual void PrecacheMaterial(const char* pMaterialName) = 0;
	virtual void PrecacheScriptSound(const char* pMaterialName) = 0;
	virtual void RenameMapName(string_t* pStringDest) = 0;
	virtual string_t AllocPooledString(const char* pszValue) = 0;
	//---------------------------------
	// Game info methods
	//
	virtual IHandleEntity* EntityFromIndex(int entityIndex) = 0;
protected:
	//---------------------------------
	// Read primitives
	//

	char* BufferPointer(void);
	void			BufferSkipBytes(int bytes);

	int				DoReadAll(void* pLeafObject, datamap_t* pLeafMap, datamap_t* pCurMap);

	typedescription_t* FindField(const char* pszFieldName, typedescription_t* pFields, int fieldCount, int* pIterator);
	void			ReadField(const SaveRestoreRecordHeader_t& header, void* pDest, datamap_t* pRootMap, typedescription_t* pField);

	void 			ReadBasicField(const SaveRestoreRecordHeader_t& header, void* pDest, datamap_t* pRootMap, typedescription_t* pField);

	void			BufferReadBytes(char* pOutput, int size);

	template <typename T>
	int ReadSimple(T* pValue, int nElems, int nBytesAvailable) // must be inline in class to keep MSVS happy
	{
		int desired = nElems * sizeof(T);
		int actual;

		if (nBytesAvailable == 0)
			actual = desired;
		else
		{
			Assert(nBytesAvailable % sizeof(T) == 0);
			actual = MIN(desired, nBytesAvailable);
		}

		BufferReadBytes((char*)pValue, actual);

		if (actual < nBytesAvailable)
			BufferSkipBytes(nBytesAvailable - actual);

		return (actual / sizeof(T));
	}

	bool			ShouldReadField(typedescription_t* pField);
	bool 			ShouldEmptyField(typedescription_t* pField);

	
	void 			ReadGameField(const SaveRestoreRecordHeader_t& header, void* pDest, datamap_t* pRootMap, typedescription_t* pField);

	//---------------------------------

	CUtlVector<int> m_BlockEndStack;

	// Stream data
	CSaveRestoreSegment* m_pData;

	// Game data
	CGameSaveRestoreInfo* m_pGameInfo;
	int						m_global;		// Restoring a global entity?
	bool					m_precache;
};

class CRestoreServer : public CRestore {
public:
	CRestoreServer(CSaveRestoreData* pdata);
	virtual int	GetModelIndex(const char* name);
	virtual void PrecacheModel(const char* pModelName);
	virtual int GetMaterialIndex(const char* pMaterialName);
	virtual void PrecacheMaterial(const char* pMaterialName);
	virtual void PrecacheScriptSound(const char* pMaterialName);
	virtual void RenameMapName(string_t* pStringDest);
	virtual string_t AllocPooledString(const char* pszValue);
	virtual IHandleEntity* EntityFromIndex(int entityIndex);
};

class CRestoreClient : public CRestore {
public:
	CRestoreClient(CSaveRestoreData* pdata);
	virtual int	GetModelIndex(const char* name);
	virtual void PrecacheModel(const char* pModelName);
	virtual int GetMaterialIndex(const char* pMaterialName);
	virtual void PrecacheMaterial(const char* pMaterialName);
	virtual void PrecacheScriptSound(const char* pMaterialName);
	virtual void RenameMapName(string_t* pStringDest);
	virtual string_t AllocPooledString(const char* pszValue);
	virtual IHandleEntity* EntityFromIndex(int entityIndex);
};

template <int FIELD_TYPE>
class CTypedescDeducer
{
public:
	template <class UTLCLASS>
	static datamap_t* Deduce(UTLCLASS* p)
	{
		return NULL;
	}

};

template<>
class CTypedescDeducer<FIELD_EMBEDDED>
{
public:
	template <class UTLCLASS>
	static datamap_t* Deduce(UTLCLASS* p)
	{
		return &UTLCLASS::ElemType_t::m_DataMap;
	}

};

#define UTLCLASS_SAVERESTORE_VALIDATE_TYPE( type ) \
	COMPILE_TIME_ASSERT( \
		type == FIELD_FLOAT ||\
		type == FIELD_STRING ||\
		type == FIELD_CLASSPTR ||\
		type == FIELD_EHANDLE ||\
		type == FIELD_EDICT ||\
		type == FIELD_VECTOR ||\
		type == FIELD_QUATERNION ||\
		type == FIELD_POSITION_VECTOR ||\
		type == FIELD_INTEGER ||\
		type == FIELD_BOOLEAN ||\
		type == FIELD_SHORT ||\
		type == FIELD_CHARACTER ||\
		type == FIELD_TIME ||\
		type == FIELD_TICK ||\
		type == FIELD_MODELNAME ||\
		type == FIELD_SOUNDNAME ||\
		type == FIELD_COLOR32 ||\
		type == FIELD_EMBEDDED ||\
		type == FIELD_MODELINDEX ||\
		type == FIELD_MATERIALINDEX\
	)

template <class UTLVECTOR, int FIELD_TYPE>
class CUtlVectorDataOps : public CDefSaveRestoreOps
{
public:
	CUtlVectorDataOps()
	{
		UTLCLASS_SAVERESTORE_VALIDATE_TYPE(FIELD_TYPE);
	}

	virtual void Save(const SaveRestoreFieldInfo_t& fieldInfo, ISave* pSave)
	{
		datamap_t* pArrayTypeDatamap = CTypedescDeducer<FIELD_TYPE>::Deduce((UTLVECTOR*)NULL);
		typedescription_t dataDesc =
		{
			(fieldtype_t)FIELD_TYPE,
			"elems",
			{ 0, 0 },
			1,
			FTYPEDESC_SAVE,
			NULL,
			NULL,
			NULL,
			pArrayTypeDatamap,
			-1,
		};

		datamap_t dataMap =
		{
			&dataDesc,
			1,
			"uv",
			NULL,
			false,
			false,
			0,
#ifdef _DEBUG
			true
#endif
		};

		UTLVECTOR* pUtlVector = (UTLVECTOR*)fieldInfo.pField;
		int nElems = pUtlVector->Count();

		pSave->WriteInt(&nElems, 1);
		if (pArrayTypeDatamap == NULL)
		{
			if (nElems)
			{
				dataDesc.fieldSize = nElems;
				dataDesc.fieldSizeInBytes = nElems * CDatamapFieldSizeDeducer<FIELD_TYPE>::FieldSize();
				pSave->WriteFields("elems", &((*pUtlVector)[0]), &dataMap, &dataDesc, 1);
			}
		}
		else
		{
			// @Note (toml 11-21-02): Save load does not support arrays of user defined types (embedded)
			dataDesc.fieldSizeInBytes = CDatamapFieldSizeDeducer<FIELD_TYPE>::FieldSize();
			for (int i = 0; i < nElems; i++)
				pSave->WriteAll(&((*pUtlVector)[i]), &dataMap);
		}
	}

	virtual void Restore(const SaveRestoreFieldInfo_t& fieldInfo, IRestore* pRestore)
	{
		datamap_t* pArrayTypeDatamap = CTypedescDeducer<FIELD_TYPE>::Deduce((UTLVECTOR*)NULL);
		typedescription_t dataDesc =
		{
			(fieldtype_t)FIELD_TYPE,
			"elems",
			{ 0, 0 },
			1,
			FTYPEDESC_SAVE,
			NULL,
			NULL,
			NULL,
			pArrayTypeDatamap,
			-1,
		};

		datamap_t dataMap =
		{
			&dataDesc,
			1,
			"uv",
			NULL,
			false,
			false,
			0,
#ifdef _DEBUG
			true
#endif
		};

		UTLVECTOR* pUtlVector = (UTLVECTOR*)fieldInfo.pField;

		int nElems = pRestore->ReadInt();

		pUtlVector->SetCount(nElems);
		if (pArrayTypeDatamap == NULL)
		{
			if (nElems)
			{
				dataDesc.fieldSize = nElems;
				dataDesc.fieldSizeInBytes = nElems * CDatamapFieldSizeDeducer<FIELD_TYPE>::FieldSize();
				pRestore->ReadFields("elems", &((*pUtlVector)[0]), &dataMap, &dataDesc, 1);
			}
		}
		else
		{
			// @Note (toml 11-21-02): Save load does not support arrays of user defined types (embedded)
			dataDesc.fieldSizeInBytes = CDatamapFieldSizeDeducer<FIELD_TYPE>::FieldSize();
			for (int i = 0; i < nElems; i++)
				pRestore->ReadAll(&((*pUtlVector)[i]), &dataMap);
		}
	}

	virtual void MakeEmpty(const SaveRestoreFieldInfo_t& fieldInfo)
	{
		UTLVECTOR* pUtlVector = (UTLVECTOR*)fieldInfo.pField;
		pUtlVector->SetCount(0);
	}

	virtual bool IsEmpty(const SaveRestoreFieldInfo_t& fieldInfo)
	{
		UTLVECTOR* pUtlVector = (UTLVECTOR*)fieldInfo.pField;
		return (pUtlVector->Count() == 0);
	}

};

//-------------------------------------

template <int FIELD_TYPE>
class CUtlVectorDataopsInstantiator
{
public:
	template <class UTLVECTOR>
	static ISaveRestoreOps* GetDataOps(UTLVECTOR*)
	{
		static CUtlVectorDataOps<UTLVECTOR, FIELD_TYPE> ops;
		return &ops;
	}
};

//-------------------------------------

#define SaveUtlVector( pSave, pUtlVector, fieldtype) \
	CUtlVectorDataopsInstantiator<fieldtype>::GetDataOps( pUtlVector )->Save( pUtlVector, pSave );

#define RestoreUtlVector( pRestore, pUtlVector, fieldtype) \
	CUtlVectorDataopsInstantiator<fieldtype>::GetDataOps( pUtlVector )->Restore( pUtlVector, pRestore );

abstract_class ISaveRestore
{
public:
	virtual void					Init( void ) = 0;
	virtual void					Shutdown( void ) = 0;
	virtual void					OnFrameRendered() = 0;
	virtual bool					SaveFileExists( const char *pName ) = 0;
	virtual bool					LoadGame( const char *pName ) = 0;
	virtual char					*GetSaveDir(void) = 0;
	virtual void					ClearSaveDir( void ) = 0;
	virtual void					RequestClearSaveDir( void ) = 0;
	virtual int						LoadGameState( char const *level, bool createPlayers ) = 0;
	virtual void					LoadAdjacentEnts( const char *pOldLevel, const char *pLandmarkName ) = 0;
	virtual const char				*FindRecentSave( char *pNameBuf, int nameBufLen ) = 0;
	virtual void					ForgetRecentSave() = 0;
	virtual int						SaveGameSlot( const char *pSaveName, const char *pSaveComment, bool onlyThisLevel = false, bool bSetMostRecent = true, const char *pszDestMap = NULL, const char *pszLandmark = NULL ) = 0;
	virtual bool					SaveGameState( bool bTransition, CSaveRestoreData ** = NULL, bool bOpenContainer = true, bool bIsAutosaveOrDangerous = false ) = 0;
	virtual int						IsValidSave( void ) = 0;
	virtual void					Finish( CSaveRestoreData *save ) = 0;

	virtual void					RestoreClientState( char const *fileName, bool adjacent ) = 0;
	virtual void					RestoreAdjacenClientState( char const *map ) = 0;
	virtual int						SaveReadNameAndComment( FileHandle_t f, OUT_Z_CAP(nameSize) char *name, int nameSize, OUT_Z_CAP(commentSize) char *comment, int commentSize ) = 0;
	virtual int						GetMostRecentElapsedMinutes( void ) = 0;
	virtual int						GetMostRecentElapsedSeconds( void ) = 0;
	virtual int						GetMostRecentElapsedTimeSet( void ) = 0;
	virtual void					SetMostRecentElapsedMinutes( const int min ) = 0;
	virtual void					SetMostRecentElapsedSeconds( const int sec ) = 0;

	virtual void					UpdateSaveGameScreenshots() = 0;

	virtual void					OnFinishedClientRestore() = 0;

	virtual void					AutoSaveDangerousIsSafe() = 0;

	virtual char const				*GetMostRecentlyLoadedFileName() = 0;
	virtual char const				*GetSaveFileName() = 0;

	virtual bool					IsXSave( void ) = 0;
	virtual void					SetIsXSave( bool bState ) = 0;

	virtual void					FinishAsyncSave() = 0;
	virtual bool					StorageDeviceValid() = 0;
	virtual void					SetMostRecentSaveGame( const char *lpszFilename ) = 0;

	virtual bool					IsSaveInProgress() = 0;
};

void *SaveAllocMemory( size_t num, size_t size, bool bClear = false );
void SaveFreeMemory( void *pSaveMem );

extern ISaveRestore *saverestore;

#endif // HOST_SAVERESTORE_H
