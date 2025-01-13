//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef PORTALRENDER_H
#define PORTALRENDER_H

#ifdef _WIN32
#pragma once
#endif

#include "iviewrender.h"
#include "view_shared.h"
#include "viewrender.h"

class CPortalRenderable
{
public:
	CPortalRenderable( void );
	virtual ~CPortalRenderable( void );

	virtual IClientEntity* GetClientEntity() = 0;
	virtual CPortalRenderable* GetLinkedPortal() = 0;
	virtual const VMatrix& MatrixThisToLinked() const = 0;
	//----------------------------------------------------------------------------
	//Stencil-based drawing helpers, these are ONLY used in stencil drawing mode
	//----------------------------------------------------------------------------
	virtual void	DrawPreStencilMask( void ) { }; //Do whatever drawing you need before cutting the stencil hole
	virtual void	DrawStencilMask( void ) { }; //Draw to wherever you should see through the portal. The mask will later be filled with the portal view.
	virtual void	DrawPostStencilFixes( void ) { }; //After done drawing to the portal mask, we need to fix the depth buffer as well as fog. So draw your mesh again, writing to z and with the fog color alpha'd in by distance
   

	//----------------------------------------------------------------------------
	//Rendering of views beyond the portal
	//----------------------------------------------------------------------------
	virtual void	RenderPortalViewToBackBuffer( CViewRender *pViewRender, const CViewSetup &cameraView ) { };
	virtual void	RenderPortalViewToTexture( CViewRender *pViewRender, const CViewSetup &cameraView ) { };


	//----------------------------------------------------------------------------
	//Visibility through portals
	//----------------------------------------------------------------------------
	virtual bool	DoesExitViewIntersectWaterPlane( float waterZ, int leafWaterDataID ) const { return false; };
	virtual SkyboxVisibility_t	SkyBoxVisibleFromPortal( void ) { return SKYBOX_NOT_VISIBLE; };

	//-----------------------------------------------------------------------------
	//Fog workarounds
	//-----------------------------------------------------------------------------
	virtual const Vector&	GetFogOrigin( void ) const { return vec3_origin; };
	virtual void			ShiftFogForExitPortalView() const;

	//-----------------------------------------------------------------------------
	//Portal visibility testing
	//-----------------------------------------------------------------------------
	//Based on view, will the camera be able to see through the portal this frame? This will allow the stencil mask to start being tested for pixel visibility.
	virtual bool	ShouldUpdatePortalView_BasedOnView( const CViewSetup &currentView, CUtlVector<VPlane> &currentComplexFrustum ) { return false; }; 
	
	//Stencil mode only: You stated the portal was visible based on view, and this is how much of the screen your stencil mask took up last frame. Still want to draw this frame? Values less than zero indicate a lack of data from last frame
	virtual bool	ShouldUpdatePortalView_BasedOnPixelVisibility( float fScreenFilledByStencilMaskLastFrame_Normalized ) { return (fScreenFilledByStencilMaskLastFrame_Normalized != 0.0f); }; // < 0 is unknown visibility, > 0 is known to be partially visible


	//-----------------------------------------------------------------------------
	// Misc
	//-----------------------------------------------------------------------------
	virtual bool	ShouldUpdateDepthDoublerTexture( const CViewSetup &viewSetup ) { return false; };
	virtual void	DrawPortal( void ) { }; //sort of like what you'd expect to happen in C_BaseAnimating::DrawModel() if portals were fully compatible with models

	//VMatrix			m_matrixThisToLinked; //Always going to need a matrix


	//-----------------------------------------------------------------------------
	//SFM related
	//-----------------------------------------------------------------------------
	bool			m_bIsPlaybackPortal;
	virtual void	GetToolRecordingState( bool bActive, KeyValues *msg ) { };
	virtual void	HandlePortalPlaybackMessage( KeyValues *pKeyValues ) { };

protected:
	//-----------------------------------------------------------------------------
	// Wrap the draw of the surface that makes use of your portal render targets with these. Only required for texture mode, but won't hurt stencil mode.
	//   Using these will allow you to know whether it's worth drawing the other side of a portal next frame.
	//-----------------------------------------------------------------------------
	void BeginPortalPixelVisibilityQuery( void );
	void EndPortalPixelVisibilityQuery( void );

	CPortalRenderable *FindRecordedPortal( int nPortalId ); //routed through here to get friend access to CPortalRender

	//routed through here to get friend access to CViewRender
	void CopyToCurrentView( CViewRender *pViewRender, const CViewSetup &viewSetup ); 
	void ViewDrawScene_PortalStencil( CViewRender *pViewRender, const CViewSetup &viewIn, ViewCustomVisibility_t *pCustomVisibility );
	void Draw3dSkyboxworld_Portal( CViewRender *pViewRender, const CViewSetup &viewIn, int &nClearFlags, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible, ITexture *pRenderTarget = NULL );
	void ViewDrawScene( CViewRender *pViewRender, bool bDrew3dSkybox, SkyboxVisibility_t nSkyboxVisible, const CViewSetup &viewIn, int nClearFlags, view_id_t viewID, bool bDrawViewModel = false, int baseDrawFlags = 0, ViewCustomVisibility_t *pCustomVisibility = NULL );
	void SetViewRecursionLevel( int iViewRecursionLevel );
	void SetRemainingViewDepth( int iRemainingViewDepth );
	void SetViewEntranceAndExitPortals( CPortalRenderable *pEntryPortal, CPortalRenderable *pExitPortal );

private:
	int m_iPortalViewIDNodeIndex; //each PortalViewIDNode_t has a child node link for each CPortalRenderable in CPortalRender::m_ActivePortals. This portal follows the same indexed link from each node
	friend class CPortalRender;
	friend class CViewRender;
};

//-----------------------------------------------------------------------------
// inline state querying methods
//-----------------------------------------------------------------------------
//inline const VMatrix& CPortalRenderable::MatrixThisToLinked() const
//{
//	return m_matrixThisToLinked;
//}



inline CPortalRenderable *CPortalRenderable::FindRecordedPortal( int nPortalId )
{ 
	return g_pViewRender->FindRecordedPortal( nPortalId );
}


class CPortalRenderableCreator //create one of these as a global and ensure you register exactly once
{
public:
	CPortalRenderableCreator( const char *szPortalType, PortalRenderableCreationFunc creationFunction )
	{
		if(g_pViewRender) g_pViewRender->AddPortalCreationFunc( szPortalType, creationFunction );
	}
};



//-----------------------------------------------------------------------------
// inline friend access redirects
//-----------------------------------------------------------------------------
inline void CPortalRenderable::CopyToCurrentView( CViewRender *pViewRender, const CViewSetup &viewSetup )
{
	pViewRender->m_CurrentView = viewSetup;
}

inline void CPortalRenderable::ViewDrawScene_PortalStencil( CViewRender *pViewRender, const CViewSetup &viewIn, ViewCustomVisibility_t *pCustomVisibility )
{
	pViewRender->ViewDrawScene_PortalStencil( viewIn, pCustomVisibility );
}

inline void CPortalRenderable::Draw3dSkyboxworld_Portal( CViewRender *pViewRender, const CViewSetup &viewIn, int &nClearFlags, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible, ITexture *pRenderTarget )
{
	pViewRender->Draw3dSkyboxworld_Portal( viewIn, nClearFlags, bDrew3dSkybox, nSkyboxVisible, pRenderTarget );
}

inline void CPortalRenderable::ViewDrawScene( CViewRender *pViewRender, bool bDrew3dSkybox, SkyboxVisibility_t nSkyboxVisible, const CViewSetup &viewIn, int nClearFlags, view_id_t viewID, bool bDrawViewModel, int baseDrawFlags, ViewCustomVisibility_t *pCustomVisibility )
{
	pViewRender->ViewDrawScene( bDrew3dSkybox, nSkyboxVisible, viewIn, nClearFlags, viewID, bDrawViewModel, baseDrawFlags, pCustomVisibility );
}

inline void CPortalRenderable::SetViewRecursionLevel( int iViewRecursionLevel )
{
	g_pViewRender->SetViewRecursionLevel(iViewRecursionLevel);
}

inline void CPortalRenderable::SetRemainingViewDepth( int iRemainingViewDepth )
{
	g_pViewRender->SetRemainingPortalViewDepth(iRemainingViewDepth);
}

inline void CPortalRenderable::SetViewEntranceAndExitPortals( CPortalRenderable *pEntryPortal, CPortalRenderable *pExitPortal )
{
	g_pViewRender->SetRenderingViewForPortal(pEntryPortal);
	g_pViewRender->SetRenderingViewExitPortal(pExitPortal);
}

#endif //#ifndef PORTALRENDER_H

