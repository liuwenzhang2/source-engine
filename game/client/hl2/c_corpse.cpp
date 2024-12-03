//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements C_Corpse
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_corpse.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT(C_Corpse, DT_Corpse, CCorpse)
	RecvPropInt(RECVINFO(m_nReferencePlayer))
END_RECV_TABLE()




C_Corpse::C_Corpse()
{
	m_nReferencePlayer = 0;
}


int C_Corpse::DrawModel( int flags )
{
	int drawn = 0;
	if ( m_nReferencePlayer <= 0 || 
		 m_nReferencePlayer > gpGlobals->maxClients )
	{
		return drawn;
	};

	// Make sure m_pstudiohdr is valid for drawing
	if ( !GetEngineObject()->GetModelPtr() )
	{
		return drawn;
	}

	if ( !m_bReadyToDraw )
		return 0;

	// get copy of player
	C_BasePlayer *player = dynamic_cast< C_BasePlayer *>( cl_entitylist->GetEnt( m_nReferencePlayer ) );
	if ( player )
	{
		Vector zero;
		zero.Init();

		drawn = modelrender->DrawModel( 
			flags, 
			this->GetEngineObject(),
			MODEL_INSTANCE_INVALID,
			m_nReferencePlayer, 
			GetEngineObject()->GetModel(),
			GetEngineObject()->GetAbsOrigin(),
			GetEngineObject()->GetAbsAngles(),
			GetEngineObject()->GetSkin(),
			GetEngineObject()->GetBody(),
			GetEngineObject()->GetHitboxSet() );
	}

	return drawn;
}

