//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "C_PortalGhostRenderable.h"
#include "PortalRender.h"
#include "c_portal_player.h"
#include "model_types.h"

static CEntityFactory<C_PortalGhostRenderable> g_C_PortalGhostRenderable_Factory("","C_PortalGhostRenderable");

C_PortalGhostRenderable::C_PortalGhostRenderable( )
{
}

C_PortalGhostRenderable::~C_PortalGhostRenderable( void )
{
	//EntityList()->RemoveEntity( GetIClientUnknown()->GetRefEHandle() );
}

void C_PortalGhostRenderable::Init(C_Prop_Portal* pOwningPortal, C_BaseEntity* pGhostSource, RenderGroup_t sourceRenderGroup, const VMatrix& matGhostTransform, float* pSharedRenderClipPlane, bool bLocalPlayer) {
	GetEngineGhost()->SetGhostedSource(pGhostSource);
	GetEngineGhost()->SetMatGhostTransform(matGhostTransform),
	m_pSharedRenderClipPlane = pSharedRenderClipPlane,
	m_bLocalPlayer = bLocalPlayer,
	m_pOwningPortal = pOwningPortal;

	//EntityList()->AddNonNetworkableEntity(this);//GetIClientUnknown()
	g_pClientLeafSystem->AddRenderable(this->GetEngineObject(), sourceRenderGroup);
}

void C_PortalGhostRenderable::UpdateOnRemove(void)
{
	g_pClientLeafSystem->RemoveRenderable(GetEngineObject()->GetRenderHandle());
	GetEngineGhost()->SetGhostedSource(NULL);
	GetEngineObject()->DestroyModelInstance();
	BaseClass::UpdateOnRemove();
}

void C_PortalGhostRenderable::PerFrameUpdate( void )
{
	GetEngineGhost()->PerFrameUpdate();

	GetEngineObject()->RemoveFromInterpolationList();

	g_pClientLeafSystem->RenderableChanged(GetEngineObject()->GetRenderHandle() );
}

Vector const& C_PortalGhostRenderable::GetRenderOrigin( void )
{
	return GetEngineGhost()->GetRenderOrigin();
}

QAngle const& C_PortalGhostRenderable::GetRenderAngles( void )
{
	return GetEngineGhost()->GetRenderAngles();
}

//bool C_PortalGhostRenderable::SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime )
//{
//	return GetEngineGhost()->SetupBones(pBoneToWorldOut, nMaxBones, boneMask, currentTime);
//}

void C_PortalGhostRenderable::GetRenderBounds( Vector& mins, Vector& maxs )
{
	GetEngineGhost()->GetRenderBounds(mins, maxs);
}

void C_PortalGhostRenderable::GetRenderBoundsWorldspace( Vector& mins, Vector& maxs )
{
	GetEngineGhost()->GetRenderBoundsWorldspace(mins, maxs);
}

void C_PortalGhostRenderable::GetShadowRenderBounds( Vector &mins, Vector &maxs, ShadowType_t shadowType )
{
	GetEngineGhost()->GetShadowRenderBounds(mins, maxs, shadowType);
}

/*bool C_PortalGhostRenderable::GetShadowCastDistance( float *pDist, ShadowType_t shadowType ) const
{
	if( m_pGhostedSource == NULL )
		return false;

	return m_pGhostedSource->GetShadowCastDistance( pDist, shadowType );
}

bool C_PortalGhostRenderable::GetShadowCastDirection( Vector *pDirection, ShadowType_t shadowType ) const
{
	if( m_pGhostedSource == NULL )
		return false;

	if( m_pGhostedSource->GetShadowCastDirection( pDirection, shadowType ) )
	{
		if( pDirection )
			*pDirection = m_matGhostTransform.ApplyRotation( *pDirection );

		return true;
	}
	return false;
}*/

//bool C_PortalGhostRenderable::GetAttachment( int number, Vector &origin )
//{
//	if( m_pGhostedSource == NULL )
//		return false;
//
//	if( m_pGhostedSource->GetAttachment( number, origin ) )
//	{
//		origin = m_matGhostTransform * origin;
//		return true;
//	}
//	return false;
//}

int C_PortalGhostRenderable::DrawModel( int flags )
{
	if( GetEngineGhost()->GetSourceIsBaseAnimating() )
	{
		if( m_bLocalPlayer )
		{
			C_Portal_Player *pPlayer = C_Portal_Player::GetLocalPlayer();

			if ( !pPlayer->IsAlive() )
			{
				// Dead player uses a ragdoll to draw, so don't ghost the dead entity
				return 0;
			}
			else if( g_pPortalRender->GetViewRecursionLevel() == 0 )
			{
				if( pPlayer->m_bEyePositionIsTransformedByPortal )
					return 0;
			}
			else if( g_pPortalRender->GetViewRecursionLevel() == 1 )
			{
				if( !pPlayer->m_bEyePositionIsTransformedByPortal )
					return 0;
			}
		}

		return C_BaseAnimating::DrawModel( flags );
	}
	else
	{
		DrawBrushModelMode_t mode = DBM_DRAW_ALL;
		if ( flags & STUDIO_TWOPASS )
		{
			mode = ( flags & STUDIO_TRANSPARENCY ) ? DBM_DRAW_TRANSLUCENT_ONLY : DBM_DRAW_OPAQUE_ONLY;
		}

		render->DrawBrushModelEx(GetEngineGhost()->GetGhostedSource(),
								(model_t *)GetEngineGhost()->GetGhostedSource()->GetEngineObject()->GetModel(),
								GetRenderOrigin(), 
								GetRenderAngles(), 
								mode );
		
		return 1;
	}

	return 0;
}


bool C_PortalGhostRenderable::IsTransparent( void )
{
	if(GetEngineGhost()->GetGhostedSource() == NULL )
		return false;

	return GetEngineGhost()->GetGhostedSource()->IsTransparent();
}

bool C_PortalGhostRenderable::UsesPowerOfTwoFrameBufferTexture()
{
	if(GetEngineGhost()->GetGhostedSource() == NULL )
		return false;

	return GetEngineGhost()->GetGhostedSource()->UsesPowerOfTwoFrameBufferTexture();
}

/*const model_t* C_PortalGhostRenderable::GetModel( ) const
{
	if( m_pGhostedSource == NULL )
		return NULL;

	return m_pGhostedSource->GetModel();
}

int C_PortalGhostRenderable::GetBody()
{
	if( m_pGhostedSource == NULL )
		return 0;

	return m_pGhostedSource->GetBody();
}*/

void C_PortalGhostRenderable::GetColorModulation( float* color )
{
	if(GetEngineGhost()->GetGhostedSource() == NULL )
		return;

	return GetEngineGhost()->GetGhostedSource()->GetColorModulation( color );
}

/*ShadowType_t C_PortalGhostRenderable::ShadowCastType()
{
	if( m_pGhostedSource == NULL )
		return SHADOWS_NONE;

	return m_pGhostedSource->ShadowCastType();
}*/

/*int C_PortalGhostRenderable::GetSkin()
{
	if( m_pGhostedSource == NULL )
		return -1;


	return m_pGhostedSource->GetSkin();
}

bool C_PortalGhostRenderable::IsTwoPass( void )
{
	if( m_pGhostedSource == NULL )
		return false;

	return m_pGhostedSource->IsTwoPass();
}*/









