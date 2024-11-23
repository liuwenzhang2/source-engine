//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_ROPE_H
#define C_ROPE_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseentity.h"
#include "rope_physics.h"
#include "materialsystem/imaterial.h"
#include "rope_shared.h"
#include "bitvec.h"


class KeyValues;
class C_BaseAnimating;
struct RopeSegData_t;



//=============================================================================
class C_RopeKeyframe : public C_BaseEntity
{
public:

	DECLARE_CLASS( C_RopeKeyframe, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	static int GetEngineObjectTypeStatic() { return ENGINEOBJECT_ROPE; }

public:

					C_RopeKeyframe();
					~C_RopeKeyframe();

	// This can be used for client-only ropes.
	static C_RopeKeyframe* Create(
		C_BaseEntity *pStartEnt,
		C_BaseEntity *pEndEnt,
		int iStartAttachment=0,
		int iEndAttachment=0,
		float ropeWidth = 2,
		const char *pMaterialName = "cable/cable",		// Note: whoever creates the rope must
														// use PrecacheModel for whatever material
														// it specifies here.
		int numSegments = 5,
		int ropeFlags = ROPE_SIMULATE
		);

	// Create a client-only rope and initialize it with the parameters from the KeyValues.
	static C_RopeKeyframe* CreateFromKeyValues( C_BaseAnimating *pEnt, KeyValues *pValues );

	// Find ropes (with both endpoints connected) that intersect this AABB. This is just an approximation.
	static int GetRopesIntersectingAABB( C_RopeKeyframe **pRopes, int nMaxRopes, const Vector &vAbsMin, const Vector &vAbsMax );

	// Attach to things (you can also just lock the endpoints down yourself if you hook the physics).
// C_BaseEntity overrides.
public:

	virtual void	ClientThink();
	virtual int		DrawModel( int flags );
	virtual bool	ShouldDraw();
	virtual const Vector& WorldSpaceCenter() const;

	
	//virtual bool	GetAttachment( int number, Vector &origin );
	virtual bool	GetAttachmentVelocity( int number, Vector &originVel, Quaternion &angleVel );

private:

	void			ReceiveMessage( int classID, bf_read &msg );

private:
	

	// present to start simulating and rendering.

	friend class CRopeManager;
};


// Profiling info.
//void Rope_ShowRSpeeds();



#endif // C_ROPE_H
