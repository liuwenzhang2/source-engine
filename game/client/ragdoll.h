//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#ifndef RAGDOLL_H
#define RAGDOLL_H

#ifdef _WIN32
#pragma once
#endif

#include "ragdoll_shared.h"

#define RAGDOLL_VISUALIZE	0

class C_BaseEntity;
class IStudioHdr;
struct mstudiobone_t;
class Vector;
class IPhysicsObject;
class CBoneAccessor;

//abstract_class IRagdoll
//{
//public:
//	virtual ~IRagdoll() {}
//
//
//};

//class CRagdoll : public IRagdoll
//{
//public:
//	CRagdoll();
//	~CRagdoll( void );
//
//	
//
//
//
//
//
//
//public:
//	
//};


//CRagdoll *CreateRagdoll( 
//	C_BaseEntity *ent, 
//	IStudioHdr *pstudiohdr, 
//	const Vector &forceVector, 
//	int forceBone, 
//	const matrix3x4_t *pDeltaBones0, 
//	const matrix3x4_t *pDeltaBones1, 
//	const matrix3x4_t *pCurrentBonePosition, 
//	float boneDt,
//	bool bFixedConstraints=false );


// save this ragdoll's creation as the current tick
void NoteRagdollCreationTick( C_BaseEntity *pRagdoll );
// returns true if the ragdoll was created on this tick
bool WasRagdollCreatedOnCurrentTick( C_BaseEntity *pRagdoll );

#endif // RAGDOLL_H