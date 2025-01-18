//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Base class for simple projectiles
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "cbaseanimatingprojectile.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( baseanimating_projectile, CBaseAnimatingProjectile );

//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CBaseAnimatingProjectile )

	DEFINE_FIELD( m_iDmg,		FIELD_INTEGER ),
	DEFINE_FIELD( m_iDmgType,	FIELD_INTEGER ),

END_DATADESC()

//---------------------------------------------------------
//---------------------------------------------------------
void CBaseAnimatingProjectile::Spawn(	char *pszModel,
										const Vector &vecOrigin,
										const Vector &vecVelocity,
										CBaseEntity *pOwner,
										MoveType_t	iMovetype,
										MoveCollide_t nMoveCollide,
										int	iDamage,
										int iDamageType )
{
	Precache();

	GetEngineObject()->SetSolid( SOLID_BBOX );
	SetModel( pszModel );

	GetEngineObject()->SetSize( vec3_origin, vec3_origin );

	m_iDmg = iDamage;
	m_iDmgType = iDamageType;

	GetEngineObject()->SetMoveType( iMovetype, nMoveCollide );

	UTIL_SetOrigin( this, vecOrigin );
	GetEngineObject()->SetAbsVelocity( vecVelocity );

	GetEngineObject()->SetOwnerEntity( pOwner );

	QAngle qAngles;
	VectorAngles( vecVelocity, qAngles );
	GetEngineObject()->SetAbsAngles( qAngles );
}


//---------------------------------------------------------
//---------------------------------------------------------
void CBaseAnimatingProjectile::Touch( IServerEntity *pOther )
{
	IServerEntity *pOwner;

	pOwner = GetEngineObject()->GetOwnerEntity();

	if( !pOwner )
	{
		pOwner = this;
	}

	trace_t	tr;
	tr = GetEngineObject()->GetTouchTrace( );

	CTakeDamageInfo info( this, pOwner, m_iDmg, m_iDmgType );
	GuessDamageForce( &info, (tr.endpos - tr.startpos), tr.endpos );
	pOther->TakeDamage( info );
	
	EntityList()->DestroyEntity( this );
}
