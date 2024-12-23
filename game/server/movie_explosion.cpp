//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "movie_explosion.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define MOVIEEXPLOSION_ENTITYNAME	"env_movieexplosion"


IMPLEMENT_SERVERCLASS_ST(MovieExplosion, DT_MovieExplosion)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS(env_movieexplosion, MovieExplosion);


MovieExplosion* MovieExplosion::CreateMovieExplosion(const Vector &pos)
{
	CBaseEntity *pEnt = (CBaseEntity*)EntityList()->CreateEntityByName(MOVIEEXPLOSION_ENTITYNAME);
	if(pEnt)
	{
		MovieExplosion *pEffect = dynamic_cast<MovieExplosion*>(pEnt);
		if(pEffect && pEffect->entindex()!=-1)
		{
			pEffect->GetEngineObject()->SetLocalOrigin( pos );
			pEffect->Activate();
			return pEffect;
		}
		else
		{
			EntityList()->DestroyEntity(pEnt);
		}
	}

	return NULL;
}


