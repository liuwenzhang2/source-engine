//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=====================================================================================//
#include "cbase.h"
#include "c_rope.h"
#include "beamdraw.h"
#include "view.h"
#include "env_wind_shared.h"
#include "input.h"
#ifdef TF_CLIENT_DLL
#include "cdll_util.h"
#endif
#include "rope_helpers.h"
#include "engine/ivmodelinfo.h"
#include "tier0/vprof.h"
#include "c_te_effect_dispatch.h"
#include "collisionutils.h"
#include <KeyValues.h>
#include <bitbuf.h>
#include "utllinkedlist.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "tier1/callqueue.h"
#include "tier1/memstack.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


IMPLEMENT_CLIENTCLASS_DT_NOBASE( C_RopeKeyframe, DT_RopeKeyframe, CRopeKeyframe )

END_RECV_TABLE()


static ConVar r_ropetranslucent( "r_ropetranslucent", "1");


// Active ropes.
CUtlLinkedList<C_RopeKeyframe*, int> g_Ropes;

//=============================================================================

// ------------------------------------------------------------------------------------ //
// This handles the rope shake command.
// ------------------------------------------------------------------------------------ //

void ShakeRopesCallback( const CEffectData &data )
{
	Vector vCenter = data.m_vOrigin;
	float flRadius = data.m_flRadius;
	float flMagnitude = data.m_flMagnitude;

	// Now find any nearby ropes and shake them.
	FOR_EACH_LL( g_Ropes, i )
	{
		C_RopeKeyframe *pRope = g_Ropes[i];
	
		pRope->GetEngineRope()->ShakeRope( vCenter, flRadius, flMagnitude );
	}
}

DECLARE_CLIENT_EFFECT( "ShakeRopes", ShakeRopesCallback );


// ------------------------------------------------------------------------------------ //
// C_RopeKeyframe
// ------------------------------------------------------------------------------------ //

C_RopeKeyframe::C_RopeKeyframe()
{
	g_Ropes.AddToTail( this );
}


C_RopeKeyframe::~C_RopeKeyframe()
{
	g_Ropes.FindAndRemove( this );
}


C_RopeKeyframe* C_RopeKeyframe::Create(
	C_BaseEntity *pStartEnt,
	C_BaseEntity *pEndEnt,
	int iStartAttachment,
	int iEndAttachment,
	float ropeWidth,
	const char *pMaterialName,
	int numSegments,
	int ropeFlags
	)
{
	C_RopeKeyframe *pRope = (C_RopeKeyframe*)EntityList()->CreateEntityByName( "C_RopeKeyframe" );

	pRope->InitializeAsClientEntity( NULL, RENDER_GROUP_OPAQUE_ENTITY );
	
	if ( pStartEnt )
	{
		pRope->GetEngineRope()->SetStartEntity(pStartEnt);
		pRope->GetEngineRope()->GetLockedPoints() |= ROPE_LOCK_START_POINT;
	}

	if ( pEndEnt )
	{
		pRope->GetEngineRope()->SetEndEntity(pEndEnt);
		pRope->GetEngineRope()->GetLockedPoints() |= ROPE_LOCK_END_POINT;
	}

	pRope->GetEngineRope()->SetStartAttachment(iStartAttachment);
	pRope->GetEngineRope()->SetEndAttachment(iEndAttachment);
	pRope->GetEngineRope()->SetWidth(ropeWidth);
	pRope->GetEngineRope()->SetSegments(clamp( numSegments, 2, ROPE_MAX_SEGMENTS ));
	pRope->GetEngineRope()->SetRopeFlags(ropeFlags);

	pRope->GetEngineRope()->FinishInit( pMaterialName );
	return pRope;
}


C_RopeKeyframe* C_RopeKeyframe::CreateFromKeyValues( C_BaseAnimating *pEnt, KeyValues *pValues )
{
	C_RopeKeyframe *pRope = C_RopeKeyframe::Create( 
		pEnt,
		pEnt,
		pEnt->GetEngineObject()->LookupAttachment( pValues->GetString( "StartAttachment" ) ),
		pEnt->GetEngineObject()->LookupAttachment( pValues->GetString( "EndAttachment" ) ),
		pValues->GetFloat( "Width", 0.5 ),
		pValues->GetString( "Material" ),
		pValues->GetInt( "NumSegments" ),
		0 );

	if ( pRope )
	{
		if ( pValues->GetInt( "Gravity", 1 ) == 0 )
		{
			pRope->GetEngineRope()->GetRopeFlags() |= ROPE_NO_GRAVITY;
		}

		pRope->GetEngineRope()->SetRopeLength(pValues->GetInt( "Length" ));
		pRope->GetEngineRope()->SetTextureScale(pValues->GetFloat( "TextureScale", pRope->GetEngineRope()->GetTextureScale() ));
		pRope->GetEngineRope()->SetSlack(0);
		pRope->GetEngineRope()->GetRopeFlags() |= ROPE_SIMULATE;
	}

	return pRope;
}


int C_RopeKeyframe::GetRopesIntersectingAABB( C_RopeKeyframe **pRopes, int nMaxRopes, const Vector &vAbsMin, const Vector &vAbsMax )
{
	if ( nMaxRopes == 0 )
		return 0;
	
	int nRopes = 0;
	FOR_EACH_LL( g_Ropes, i )
	{
		C_RopeKeyframe *pRope = g_Ropes[i];
	
		Vector v1, v2;
		QAngle dummy;
		if ( pRope->GetEngineRope()->GetEndPointPos( 0, v1, dummy) && pRope->GetEngineRope()->GetEndPointPos( 1, v2, dummy) )
		{
			if ( IsBoxIntersectingRay( v1, v2-v1, vAbsMin, vAbsMax, 0.1f ) )
			{
				pRopes[nRopes++] = pRope;
				if ( nRopes == nMaxRopes )
					break;
			}
		}
	}

	return nRopes;
}

void C_RopeKeyframe::ClientThink()
{
	GetEngineRope()->RopeThink();
}

int C_RopeKeyframe::DrawModel( int flags )
{
	VPROF_BUDGET( "C_RopeKeyframe::DrawModel", VPROF_BUDGETGROUP_ROPES );
	if( !GetEngineRope()->InitRopePhysics() )
		return 0;

	if ( !GetEngineObject()->IsReadyToDraw() )
		return 0;

	// Resize the rope
	if(GetEngineRope()->GetRopeFlags() & ROPE_RESIZE)
	{
		GetEngineRope()->RecomputeSprings();
	}

	// If our start & end entities have models, but are nodraw, then we don't draw
	if (GetEngineRope()->GetStartEntity() && GetEngineRope()->GetStartEntity()->IsDormant() && GetEngineRope()->GetEndEntity() && GetEngineRope()->GetEndEntity()->IsDormant())
	{
		// Check models because rope endpoints are point entities
		if (GetEngineRope()->GetStartEntity()->GetEngineObject()->GetModelIndex() && GetEngineRope()->GetEndEntity()->GetEngineObject()->GetModelIndex())
			return 0;
	}

	GetEngineRope()->ConstrainNodesBetweenEndpoints();

	GetEngineRope()->AddToRenderCache();
	return 1;
}

bool C_RopeKeyframe::ShouldDraw()
{
	if( !r_ropetranslucent.GetBool() ) 
		return false;

	if( !(GetEngineRope()->GetRopeFlags() & ROPE_SIMULATE))
		return false;

	return true;
}

const Vector& C_RopeKeyframe::WorldSpaceCenter( ) const
{
	return GetEngineObject()->GetAbsOrigin();
}

//bool C_RopeKeyframe::GetAttachment( int number, Vector &origin )
//{
//	int nNodes = m_RopePhysics.NumNodes();
//	if ( (number != ROPE_ATTACHMENT_START_POINT && number != ROPE_ATTACHMENT_END_POINT) || nNodes < 2 )
//		return false;
//
//	// Now setup the orientation based on the last segment.
//	if ( number == ROPE_ATTACHMENT_START_POINT )
//	{
//		origin = m_RopePhysics.GetNode( 0 )->m_vPredicted;
//	}
//	else
//	{
//		origin = m_RopePhysics.GetNode( nNodes-1 )->m_vPredicted;
//	}
//	return true;
//}

bool C_RopeKeyframe::GetAttachmentVelocity( int number, Vector &originVel, Quaternion &angleVel )
{
	Assert(0);
	return false;
}


//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void C_RopeKeyframe::ReceiveMessage( int classID, bf_read &msg )
{
	if ( classID != GetClientClass()->m_ClassID )
	{
		// message is for subclass
		BaseClass::ReceiveMessage( classID, msg );
		return;
	}

	// Read instantaneous fore data
	GetEngineRope()->GetImpulse().x = msg.ReadFloat();
	GetEngineRope()->GetImpulse().y = msg.ReadFloat();
	GetEngineRope()->GetImpulse().z = msg.ReadFloat();
}
