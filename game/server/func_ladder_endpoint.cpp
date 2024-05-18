//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//
#include "cbase.h"
#include "func_ladder.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: A transient entity used to construct a true CFuncLadder
// FIXME:  THIS ENTITY IS OBSOLETE NOW, SHOULD BE REMOVED FROM HERE AND .FGD AT SOME POINT!!!
//-----------------------------------------------------------------------------
class CFuncLadderEndPoint : public CBaseEntity
{
public:
	DECLARE_CLASS( CFuncLadderEndPoint, CBaseEntity );

	virtual void Activate();

private:
	bool		Validate();
};

LINK_ENTITY_TO_CLASS( func_ladderendpoint, CFuncLadderEndPoint );

void CFuncLadderEndPoint::Activate()
{
	BaseClass::Activate();

	if ( IsMarkedForDeletion() )
		return;

	Validate();
}

bool CFuncLadderEndPoint::Validate()
{
	// Find the the other end
	Vector startPos = GetEngineObject()->GetAbsOrigin();
	
	CFuncLadderEndPoint *other = dynamic_cast< CFuncLadderEndPoint * >( GetNextTarget() );
	if ( !other )
	{
		DevMsg( 1, "func_ladderendpoint(%s) without matching target\n", GetEntityName().ToCStr() );
		return false;
	}

	Vector endPos = other->GetEngineObject()->GetAbsOrigin();

	CFuncLadder *ladder = ( CFuncLadder * )gEntList.CreateEntityByName( "func_useableladder" );
	if ( ladder )
	{
		ladder->SetEndPoints( startPos, endPos );
		ladder->GetEngineObject()->SetAbsOrigin(GetEngineObject()->GetAbsOrigin() );
		ladder->GetEngineObject()->SetParent(GetEngineObject()->GetMoveParent());
		ladder->SetName(STRING( GetEntityName()) );
		ladder->Spawn();
	}

	// Delete both endpoints
	UTIL_Remove( other );
	UTIL_Remove( this );

	return true;
}
