//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//


//#include "cbase.h"
#include "cdll_client_int.h"
#include "PortalRender.h"
#include "clienteffectprecachesystem.h"
#include "c_pixel_visibility.h"
#include "glow_overlay.h"
#include "materialsystem/itexture.h"
#include "toolframework/itoolframework.h"
#include "tier1/KeyValues.h"
#include "view_scene.h"
#include "viewrender.h"
#include "vprof.h"

CLIENTEFFECT_REGISTER_BEGIN( PrecachePortalDrawingMaterials )
CLIENTEFFECT_MATERIAL( "shadertest/wireframe" )
CLIENTEFFECT_MATERIAL( "engine/writez_model" )
CLIENTEFFECT_MATERIAL( "engine/TranslucentVertexColor" )
CLIENTEFFECT_REGISTER_END()




void IncreasePortalViewIDChildLinkCount( PortalViewIDNode_t *pNode )
{
	for( int i = pNode->ChildNodes.Count(); --i >= 0; )
	{
		if( pNode->ChildNodes[i] != NULL )
			IncreasePortalViewIDChildLinkCount( pNode->ChildNodes[i] );
	}
	pNode->ChildNodes.AddToTail( NULL );
}

void RemovePortalViewIDChildLinkIndex( PortalViewIDNode_t *pNode, int iRemoveIndex )
{
	Assert( pNode->ChildNodes.Count() > iRemoveIndex );

	if( pNode->ChildNodes[iRemoveIndex] != NULL )
	{
		g_pViewRender->FreePortalViewIDNode( pNode->ChildNodes[iRemoveIndex] );
		pNode->ChildNodes[iRemoveIndex] = NULL;
	}

	//I know the current behavior for CUtlVector::FastRemove() is to move the tail into the removed index. But I need that behavior to be true in the future as well so I'm doing it explicitly
	pNode->ChildNodes[iRemoveIndex] = pNode->ChildNodes.Tail();
	pNode->ChildNodes.Remove( pNode->ChildNodes.Count() - 1 );

	for( int i = pNode->ChildNodes.Count(); --i >= 0; )
	{
		if( pNode->ChildNodes[i] )
			RemovePortalViewIDChildLinkIndex( pNode->ChildNodes[i], iRemoveIndex );
	}
}

//-----------------------------------------------------------------------------
//
// Active Portal class 
//
//-----------------------------------------------------------------------------
CPortalRenderable::CPortalRenderable( void ) : 	
	m_bIsPlaybackPortal( false )
{
	//m_matrixThisToLinked.Identity();
	
	//Portal view ID indexing setup
	IncreasePortalViewIDChildLinkCount( &g_pViewRender->GetHeadPortalViewIDNode() );
	m_iPortalViewIDNodeIndex = g_pViewRender->GetAllPortals().AddToTail(this);
}

CPortalRenderable::~CPortalRenderable( void )
{
	int iLast = g_pViewRender->GetAllPortals().Count() - 1;

	//update the soon-to-be-transplanted portal's index
	g_pViewRender->GetAllPortals()[iLast]->m_iPortalViewIDNodeIndex = m_iPortalViewIDNodeIndex;

	//I know the current behavior for CUtlVector::FastRemove() is to move the tail into the removed index. But I need that behavior to be true in the future as well so I'm doing it explicitly
	g_pViewRender->GetAllPortals()[m_iPortalViewIDNodeIndex] = g_pViewRender->GetAllPortals().Tail();
	g_pViewRender->GetAllPortals().Remove(iLast);
	
	RemovePortalViewIDChildLinkIndex( &g_pViewRender->GetHeadPortalViewIDNode(), m_iPortalViewIDNodeIndex); //does the same transplant operation as above to all portal view id nodes
}


void CPortalRenderable::BeginPortalPixelVisibilityQuery( void )
{
#ifndef TEMP_DISABLE_PORTAL_VIS_QUERY
	return;
#endif

	if( g_pViewRender->ShouldUseStencilsToRenderPortals() ) //this function exists because we require help in texture mode, we need no assistance in stencil mode. Moreover, doing the query twice will probably fubar the results
		return;

	PortalViewIDNode_t *pCurrentPortalViewNode = g_pViewRender->GetPortalViewIDNodeChain()[g_pViewRender->GetViewRecursionLevel()]->ChildNodes[m_iPortalViewIDNodeIndex];
	
	if( pCurrentPortalViewNode )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->BeginOcclusionQueryDrawing( pCurrentPortalViewNode->occlusionQueryHandle );

		int iX, iY, iWidth, iHeight;
		pRenderContext->GetViewport( iX, iY, iWidth, iHeight );

		pCurrentPortalViewNode->iWindowPixelsAtQueryTime = iWidth * iHeight;
	}
}

void CPortalRenderable::EndPortalPixelVisibilityQuery( void )
{
#ifndef TEMP_DISABLE_PORTAL_VIS_QUERY
	return;
#endif

	if( g_pViewRender->ShouldUseStencilsToRenderPortals() ) //this function exists because we require help in texture mode, we need no assistance in stencil mode. Moreover, doing the query twice will probably fubar the results
		return;

	PortalViewIDNode_t *pCurrentPortalViewNode = g_pViewRender->GetPortalViewIDNodeChain()[g_pViewRender->GetViewRecursionLevel()]->ChildNodes[m_iPortalViewIDNodeIndex];
	
	if( pCurrentPortalViewNode )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->EndOcclusionQueryDrawing( pCurrentPortalViewNode->occlusionQueryHandle );
	}
}

void CPortalRenderable::ShiftFogForExitPortalView() const
{
	CMatRenderContextPtr pRenderContext( materials );
	float fFogStart, fFogEnd, fFogZ;
	pRenderContext->GetFogDistances( &fFogStart, &fFogEnd, &fFogZ );

	Vector vFogOrigin = GetFogOrigin();
	Vector vCameraToExitPortal = vFogOrigin - g_pViewRender->CurrentViewOrigin();
	float fDistModifier = vCameraToExitPortal.Dot(g_pViewRender->CurrentViewForward() );

	fFogStart += fDistModifier;
	fFogEnd += fDistModifier;
	//fFogZ += something; //FIXME: find out what the hell to do with this

	pRenderContext->FogStart( fFogStart );
	pRenderContext->FogEnd( fFogEnd );
	pRenderContext->SetFogZ( fFogZ );
}








