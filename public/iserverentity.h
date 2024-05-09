//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ISERVERENTITY_H
#define ISERVERENTITY_H
#ifdef _WIN32
#pragma once
#endif


#include "iserverunknown.h"
#include "string_t.h"
#include "platform.h"



struct Ray_t;
class ServerClass;
class ICollideable;
class IServerNetworkable;
class Vector;
class QAngle;
struct PVSInfo_t;
class CCheckTransmitInfo;
struct matrix3x4_t;

class IEngineObjectServer : public IEngineObject {
public:

	virtual CBaseEntity* GetOuter() = 0;

	virtual void ParseMapData(IEntityMapData* mapData) = 0;
	virtual void SetAbsVelocity(const Vector& vecVelocity) = 0;
	virtual Vector& GetAbsVelocity() = 0;
	virtual const Vector& GetAbsVelocity() const = 0;
	// NOTE: Setting the abs origin or angles will cause the local origin + angles to be set also
	virtual void SetAbsOrigin(const Vector& origin) = 0;
	virtual Vector& GetAbsOrigin(void) = 0;
	virtual const Vector& GetAbsOrigin(void) const = 0;

	virtual void SetAbsAngles(const QAngle& angles) = 0;
	virtual QAngle& GetAbsAngles(void) = 0;
	virtual const QAngle& GetAbsAngles(void) const = 0;

	// Origin and angles in local space ( relative to parent )
	// NOTE: Setting the local origin or angles will cause the abs origin + angles to be set also
	virtual void SetLocalOrigin(const Vector& origin) = 0;
	virtual const Vector& GetLocalOrigin(void) const = 0;

	virtual void SetLocalAngles(const QAngle& angles) = 0;
	virtual const QAngle& GetLocalAngles(void) const = 0;

	virtual void SetLocalVelocity(const Vector& vecVelocity) = 0;
	virtual const Vector& GetLocalVelocity() const = 0;

	virtual void CalcAbsolutePosition() = 0;
	virtual void CalcAbsoluteVelocity() = 0;

	virtual IEngineObjectServer* GetMoveParent(void) = 0;
	virtual void SetMoveParent(IEngineObjectServer* hMoveParent) = 0;
	virtual IEngineObjectServer* GetRootMoveParent() = 0;
	virtual IEngineObjectServer* FirstMoveChild(void) = 0;
	virtual void SetFirstMoveChild(IEngineObjectServer* hMoveChild) = 0;
	virtual IEngineObjectServer* NextMovePeer(void) = 0;
	virtual void SetNextMovePeer(IEngineObjectServer* hMovePeer) = 0;

	virtual void ResetRgflCoordinateFrame() = 0;
	// Returns the entity-to-world transform
	virtual matrix3x4_t& EntityToWorldTransform() = 0;
	virtual const matrix3x4_t& EntityToWorldTransform() const = 0;

	// Some helper methods that transform a point from entity space to world space + back
	virtual void EntityToWorldSpace(const Vector& in, Vector* pOut) const = 0;
	virtual void WorldToEntitySpace(const Vector& in, Vector* pOut) const = 0;

	// This function gets your parent's transform. If you're parented to an attachment,
	// this calculates the attachment's transform and gives you that.
	//
	// You must pass in tempMatrix for scratch space - it may need to fill that in and return it instead of 
	// pointing you right at a variable in your parent.
	virtual matrix3x4_t& GetParentToWorldTransform(matrix3x4_t& tempMatrix) = 0;

	// Computes the abs position of a point specified in local space
	virtual void ComputeAbsPosition(const Vector& vecLocalPosition, Vector* pAbsPosition) = 0;

	// Computes the abs position of a direction specified in local space
	virtual void ComputeAbsDirection(const Vector& vecLocalDirection, Vector* pAbsDirection) = 0;

	virtual void	GetVectors(Vector* forward, Vector* right, Vector* up) const = 0;

	// Set the movement parent. Your local origin and angles will become relative to this parent.
	// If iAttachment is a valid attachment on the parent, then your local origin and angles 
	// are relative to the attachment on this entity. If iAttachment == -1, it'll preserve the
	// current m_iParentAttachment.
	virtual void	SetParent(IEngineObjectServer* pNewParent, int iAttachment = -1) = 0;
	// FIXME: Make hierarchy a member of CBaseEntity
	// or a contained private class...
	static void UnlinkChild(IEngineObjectServer* pParent, IEngineObjectServer* pChild);
	static void LinkChild(IEngineObjectServer* pParent, IEngineObjectServer* pChild);
	static void ClearParent(IEngineObjectServer* pEntity);
	static void UnlinkAllChildren(IEngineObjectServer* pParent);
	static void UnlinkFromParent(IEngineObjectServer* pRemove);
	static void TransferChildren(IEngineObjectServer* pOldParent, IEngineObjectServer* pNewParent);

	virtual int				AreaNum() const = 0;
	virtual PVSInfo_t* GetPVSInfo() = 0;

	// This version does a PVS check which also checks for connected areas
	virtual bool IsInPVS(const CCheckTransmitInfo* pInfo) = 0;

	// This version doesn't do the area check
	virtual bool IsInPVS(const CBaseEntity* pRecipient, const void* pvs, int pvssize) = 0;

	// Recomputes PVS information
	virtual void RecomputePVSInformation() = 0;
	// Marks the PVS information dirty
	virtual void MarkPVSInformationDirty() = 0;

	virtual void SetClassname(const char* className) = 0;
	virtual const char* GetClassName() const = 0;
	virtual const string_t& GetClassname() const = 0;
	virtual void SetGlobalname(const char* iGlobalname) = 0;
	virtual const string_t& GetGlobalname() const = 0;
	virtual void SetParentName(const char* parentName) = 0;
	virtual string_t& GetParentName() = 0;
	virtual void SetName(const char* newName) = 0;
	virtual string_t& GetEntityName() = 0;
	virtual bool NameMatches(const char* pszNameOrWildcard) = 0;
	virtual bool ClassMatches(const char* pszClassOrWildcard) = 0;
	virtual bool NameMatches(string_t nameStr) = 0;
	virtual bool ClassMatches(string_t nameStr) = 0;
	virtual IServerNetworkable* GetNetworkable() = 0;
	virtual int			GetParentAttachment() = 0;
	virtual void		ClearParentAttachment() = 0;

};

// This class is how the engine talks to entities in the game DLL.
// CBaseEntity implements this interface.
class IServerEntity	: public IServerUnknown
{
public:
	virtual					~IServerEntity() {}
	// Delete yourself.
	virtual void			Release(void) = 0;
// Previously in pev
	virtual int				GetModelIndex( void ) const = 0;
 	virtual string_t		GetModelName( void ) const = 0;

	virtual void			SetModelIndex( int index ) = 0;
	virtual PVSInfo_t*		GetPVSInfo() = 0; // get current visibilty data
	virtual int				AreaNum() const = 0;

};


//-----------------------------------------------------------------------------
// Purpose: Exposes IClientEntity's to engine
//-----------------------------------------------------------------------------
abstract_class IServerEntityList : public IEntityList
{
public:
	virtual void ReserveSlot(int index) = 0;
	virtual int AllocateFreeSlot(bool bNetworkable = true, int index = -1) = 0;
	virtual IServerEntity* CreateEntityByName(const char* className, int iForceEdictIndex = -1, int iSerialNum = -1) = 0;
	virtual void DestroyEntity(IHandleEntity* pEntity) = 0;
	//virtual edict_t* GetEdict(CBaseHandle hEnt) const = 0;
	virtual int NumberOfEdicts(void) = 0;
	virtual int NumberOfReservedEdicts(void) = 0;
	virtual int IndexOfHighestEdict(void) = 0;

	virtual IEngineObjectServer* GetEngineObject(int entnum) = 0;
	// Get IServerNetworkable interface for specified entity
	virtual IServerNetworkable* GetServerNetworkable(int entnum) const = 0;
	virtual IServerNetworkable* GetServerNetworkableFromHandle(CBaseHandle hEnt) const = 0;
	virtual IServerUnknown* GetServerUnknownFromHandle(CBaseHandle hEnt) const = 0;

	// NOTE: This function is only a convenience wrapper.
	// It returns GetServerNetworkable( entnum )->GetIServerEntity().
	virtual IServerEntity* GetServerEntity(int entnum) const = 0;
	virtual IServerEntity* GetServerEntityFromHandle(CBaseHandle hEnt) const = 0;
	virtual short		GetNetworkSerialNumber(int entnum) const = 0;

	// Returns number of entities currently in use
	virtual int					NumberOfEntities() = 0;

	// Returns highest index actually used
	//virtual int					GetHighestEntityIndex(void) = 0;

	// Sizes entity list to specified size
	//virtual void				SetMaxEntities(int maxents) = 0;
	//virtual int					GetMaxEntities() = 0;
};

extern IServerEntityList* serverEntitylist;

#define VSERVERENTITYLIST_INTERFACE_VERSION	"VServerEntityList003"

#endif // ISERVERENTITY_H
