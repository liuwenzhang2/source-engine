//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "entitylist_base.h"
#include "ihandleentity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const char* PS_SD_Static_World_StaticProps_ClippedProp_t::szTraceSurfaceName = "**studio**";
const int PS_SD_Static_World_StaticProps_ClippedProp_t::iTraceSurfaceFlags = 0;
IHandleEntity* PS_SD_Static_World_StaticProps_ClippedProp_t::pTraceEntity = NULL;

IEntityFactory* g_pEntityFactoryHead;

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CEntityFactoryDictionary::CEntityFactoryDictionary() : m_Factories(true, 0, 128)
{
}


//-----------------------------------------------------------------------------
// Finds a new factory
//-----------------------------------------------------------------------------
IEntityFactory* CEntityFactoryDictionary::FindFactory(const char* pClassName)
{
	unsigned short nIndex = m_Factories.Find(pClassName);
	if (nIndex == m_Factories.InvalidIndex())
		return NULL;
	return m_Factories[nIndex];
}


//-----------------------------------------------------------------------------
// Install a new factory
//-----------------------------------------------------------------------------
void CEntityFactoryDictionary::InstallFactory(IEntityFactory* pFactory)
{
	//Assert(FindFactory(pClassName) == NULL);
	if (pFactory->RequiredEdictIndex() != -1) {
		int aaa = 0;
	}
	if (!pFactory->IsNetworkable()) {
		int aaa = 0;
	}
	if (pFactory->GetMapClassName() && pFactory->GetMapClassName()[0]) {
		IEntityFactory* pExistFactory = FindFactory(pFactory->GetMapClassName());
		if (pExistFactory) {
			if (!V_strcmp(pExistFactory->GetDllClassName(), pFactory->GetDllClassName())) {
				Error("InstallFactory %s already exist\n", pFactory->GetMapClassName());
			}
			else {
				Msg("InstallFactory %s already exist\n", pFactory->GetMapClassName());
				m_Factories.Remove(pExistFactory->GetMapClassName());
				m_Factories.Remove(pExistFactory->GetDllClassName());
				m_Factories.Insert(pFactory->GetMapClassName(), pFactory);
			}
		}
		else {
			m_Factories.Insert(pFactory->GetMapClassName(), pFactory);
		}
	}
	if (pFactory->GetDllClassName() && pFactory->GetDllClassName()[0]) {
		IEntityFactory* pExistFactory = FindFactory(pFactory->GetDllClassName());
		if (pExistFactory) {
			if(pExistFactory->GetMapClassName() && pExistFactory->GetMapClassName()[0]){
				if (pFactory->GetMapClassName() && pFactory->GetMapClassName()[0]) {
					Msg("InstallFactory %s already exist\n", pFactory->GetDllClassName());
				}
				else {
					Msg("InstallFactory %s already exist\n", pFactory->GetDllClassName());
				}
			}
			else {
				if (pFactory->GetMapClassName() && pFactory->GetMapClassName()[0]) {
					m_Factories.Remove(pExistFactory->GetDllClassName());
					m_Factories.Insert(pFactory->GetDllClassName(), pFactory);
				}
				else {
					Error("InstallFactory %s already exist\n", pFactory->GetDllClassName());
				}
			}
		}
		else {
			m_Factories.Insert(pFactory->GetDllClassName(), pFactory);
		}
	}
}

void CEntityFactoryDictionary::UninstallFactory(IEntityFactory* pFactory)
{
	if (pFactory->GetMapClassName() && pFactory->GetMapClassName()[0]) {
		m_Factories.Remove(pFactory->GetMapClassName());
	}
	if (pFactory->GetDllClassName() && pFactory->GetDllClassName()[0]) {
		m_Factories.Remove(pFactory->GetDllClassName());
	}
}

//-----------------------------------------------------------------------------
// Instantiate something using a factory
//-----------------------------------------------------------------------------
IHandleEntity* CEntityFactoryDictionary::Create(IEntityList* pEntityList, const char* pClassName, int iForceEdictIndex, int iSerialNum, IEntityCallBack* pCallBack)
{
	IEntityFactory* pFactory = FindFactory(pClassName);
	if (!pFactory)
	{
		Warning("Attempted to create unknown entity type %s!\n", pClassName);
		return NULL;
	}
#if defined(TRACK_ENTITY_MEMORY) && defined(USE_MEM_DEBUG)
	MEM_ALLOC_CREDIT_(m_Factories.GetElementName(m_Factories.Find(pClassName)));
#endif
	return pFactory->Create(pEntityList, iForceEdictIndex, iSerialNum, pCallBack);
}

const char* CEntityFactoryDictionary::GetMapClassName(const char* pClassName) {
	IEntityFactory* pFactory = FindFactory(pClassName);
	if (!pFactory)
	{
		Warning("Attempted to create unknown entity type %s!\n", pClassName);
		return NULL;
	}
	return pFactory->GetMapClassName();
}

const char* CEntityFactoryDictionary::GetDllClassName(const char* pClassName) {
	IEntityFactory* pFactory = FindFactory(pClassName);
	if (!pFactory)
	{
		Warning("Attempted to create unknown entity type %s!\n", pClassName);
		return NULL;
	}
	return pFactory->GetDllClassName();
}

size_t		CEntityFactoryDictionary::GetEntitySize(const char* pClassName) {
	IEntityFactory* pFactory = FindFactory(pClassName);
	if (!pFactory)
	{
		Warning("Attempted to create unknown entity type %s!\n", pClassName);
		return -1;
	}
	return pFactory->GetEntitySize();
}

int CEntityFactoryDictionary::RequiredEdictIndex(const char* pClassName) {
	IEntityFactory* pFactory = FindFactory(pClassName);
	if (!pFactory)
	{
		Warning("Attempted to create unknown entity type %s!\n", pClassName);
		return -1;
	}
	return pFactory->RequiredEdictIndex();
}

bool CEntityFactoryDictionary::IsNetworkable(const char* pClassName) {
	IEntityFactory* pFactory = FindFactory(pClassName);
	if (!pFactory)
	{
		Warning("Attempted to create unknown entity type %s!\n", pClassName);
		return -1;
	}
	return pFactory->IsNetworkable();
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
const char* CEntityFactoryDictionary::GetCannonicalName(const char* pClassName)
{
	return m_Factories.GetElementName(m_Factories.Find(pClassName));
}

//-----------------------------------------------------------------------------
// Destroy a networkable
//-----------------------------------------------------------------------------
void CEntityFactoryDictionary::Destroy(IHandleEntity* pEntity)
{
	IEntityFactory* pFactory = pEntity->GetEntityFactory();
	if (!pFactory)
	{
		Error("Attempted to destroy unknown entity \n");
		return;
	}

	pFactory->Destroy(pEntity);
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
void CEntityFactoryDictionary::ReportEntitySizes()
{
	for (int i = m_Factories.First(); i != m_Factories.InvalidIndex(); i = m_Factories.Next(i))
	{
		Msg(" %s: %d", m_Factories.GetElementName(i), m_Factories[i]->GetEntitySize());
	}
}

void CEntityFactoryDictionary::DumpEntityFactories()
{
	for (int i = m_Factories.First(); i != m_Factories.InvalidIndex(); i = m_Factories.Next(i))
	{
		Msg("%s\n", m_Factories.GetElementName(i));
	}
}