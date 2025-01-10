//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//
#if !defined( IVIEWRENDER_H )
#define IVIEWRENDER_H
#ifdef _WIN32
#pragma once
#endif


#include "ivrenderview.h"


// These are set as it draws reflections, refractions, etc, so certain effects can avoid 
// drawing themselves in reflections.
enum DrawFlags_t
{
	DF_RENDER_REFRACTION	= 0x1,
	DF_RENDER_REFLECTION	= 0x2,

	DF_CLIP_Z				= 0x4,
	DF_CLIP_BELOW			= 0x8,

	DF_RENDER_UNDERWATER	= 0x10,
	DF_RENDER_ABOVEWATER	= 0x20,
	DF_RENDER_WATER			= 0x40,

	DF_SSAO_DEPTH_PASS		= 0x100,
	DF_WATERHEIGHT			= 0x200,
	DF_DRAW_SSAO			= 0x400,
	DF_DRAWSKYBOX			= 0x800,

	DF_FUDGE_UP				= 0x1000,

	DF_DRAW_ENTITITES		= 0x2000,
	DF_UNUSED3				= 0x4000,

	DF_UNUSED4				= 0x8000,

	DF_UNUSED5				= 0x10000,
	DF_SAVEGAMESCREENSHOT	= 0x20000,
	DF_CLIP_SKYBOX			= 0x40000,

	DF_SHADOW_DEPTH_MAP		= 0x100000	// Currently rendering a shadow depth map
};


//-----------------------------------------------------------------------------
// Purpose: View setup and rendering
//-----------------------------------------------------------------------------
class CViewSetup;
struct vrect_t;
struct WriteReplayScreenshotParams_t;
class IReplayScreenshotSystem;

//-----------------------------------------------------------------------------
// Data specific to intro mode to control rendering.
//-----------------------------------------------------------------------------
struct IntroDataBlendPass_t
{
	int m_BlendMode;
	float m_Alpha; // in [0.0f,1.0f]  This needs to add up to 1.0 for all passes, unless you are fading out.
};

struct IntroData_t
{
	bool	m_bDrawPrimary;
	Vector	m_vecCameraView;
	QAngle	m_vecCameraViewAngles;
	float	m_playerViewFOV;
	CUtlVector<IntroDataBlendPass_t> m_Passes;

	// Fade overriding for the intro
	float	m_flCurrentFadeColor[4];
};

abstract_class IViewRender
{
public:
	// SETUP
	// Initialize view renderer
	virtual void		Init( void ) = 0;

	// Clear any systems between levels
	virtual void		LevelInit( void ) = 0;
	virtual void		LevelShutdown( void ) = 0;

	// Shutdown
	virtual void		Shutdown( void ) = 0;

	// RENDERING
	// Called right before simulation. It must setup the view model origins and angles here so 
	// the correct attachment points can be used during simulation.	
	virtual void		OnRenderStart() = 0;

	// Called to render the entire scene
	virtual	void		Render( vrect_t *rect ) = 0;

	// Called to render just a particular setup ( for timerefresh and envmap creation )
	virtual void		RenderView( const CViewSetup &view, int nClearFlags, int whatToDraw ) = 0;

	// What are we currently rendering? Returns a combination of DF_ flags.
	virtual int GetDrawFlags() = 0;

	// MISC
	// Start and stop pitch drifting logic
	virtual void		StartPitchDrift( void ) = 0;
	virtual void		StopPitchDrift( void ) = 0;

	// This can only be called during rendering (while within RenderView).
	virtual VPlane*		GetFrustum() = 0;

	virtual bool		ShouldDrawBrushModels( void ) = 0;

	virtual const CViewSetup *GetPlayerViewSetup( void ) const = 0;
	virtual const CViewSetup *GetViewSetup( void ) const = 0;

	virtual void		DisableVis( void ) = 0;

	virtual int			BuildWorldListsNumber() const = 0;

	virtual void		SetCheapWaterStartDistance( float flCheapWaterStartDistance ) = 0;
	virtual void		SetCheapWaterEndDistance( float flCheapWaterEndDistance ) = 0;

	virtual void		GetWaterLODParams( float &flCheapWaterStartDistance, float &flCheapWaterEndDistance ) = 0;

	virtual void		DriftPitch (void) = 0;

	virtual void		SetScreenOverlayMaterial( IMaterial *pMaterial ) = 0;
	virtual IMaterial	*GetScreenOverlayMaterial( ) = 0;

	virtual void		WriteSaveGameScreenshot( const char *pFilename ) = 0;
	virtual void		WriteSaveGameScreenshotOfSize( const char *pFilename, int width, int height, bool bCreatePowerOf2Padded = false, bool bWriteVTF = false ) = 0;

	virtual void		WriteReplayScreenshot( WriteReplayScreenshotParams_t &params ) = 0;
	virtual void		UpdateReplayScreenshotCache() = 0;

	// Draws another rendering over the top of the screen
	virtual void		QueueOverlayRenderView( const CViewSetup &view, int nClearFlags, int whatToDraw ) = 0;

	// Returns znear and zfar
	virtual float		GetZNear() = 0;
	virtual float		GetZFar() = 0;

	virtual void		GetScreenFadeDistances( float *min, float *max ) = 0;

	virtual IClientEntity *GetCurrentlyDrawingEntity() = 0;
	virtual void		SetCurrentlyDrawingEntity( IClientEntity *pEnt ) = 0;

	virtual bool		UpdateShadowDepthTexture( ITexture *pRenderTarget, ITexture *pDepthTexture, const CViewSetup &shadowView ) = 0;

	virtual void		FreezeFrame( float flFreezeTime ) = 0;

	virtual IReplayScreenshotSystem *GetReplayScreenshotSystem() = 0;

	virtual const Vector& PrevMainViewOrigin() = 0;
	virtual const QAngle& PrevMainViewAngles() = 0;
	virtual const Vector& MainViewOrigin() = 0;
	virtual const QAngle& MainViewAngles() = 0;
	virtual const VMatrix& MainWorldToViewMatrix() = 0;
	virtual const Vector& MainViewForward() = 0;
	virtual const Vector& MainViewRight() = 0;
	virtual const Vector& MainViewUp() = 0;

	virtual void AllowCurrentViewAccess(bool allow) = 0;
	virtual bool IsCurrentViewAccessAllowed() = 0;
	virtual view_id_t CurrentViewID() = 0;
	virtual const Vector& CurrentViewOrigin() = 0;
	virtual const QAngle& CurrentViewAngles() = 0;
	virtual const VMatrix& CurrentWorldToViewMatrix() = 0;
	virtual const Vector& CurrentViewForward() = 0;
	virtual const Vector& CurrentViewRight() = 0;
	virtual const Vector& CurrentViewUp() = 0;
	virtual bool DrawingShadowDepthView(void) = 0;
	virtual bool DrawingMainView() = 0;

	virtual bool IsRenderingScreenshot() = 0;
	virtual IntroData_t* GetIntroData() = 0;
	virtual void SetIntroData(IntroData_t* pIntroData) = 0;
	virtual void SetFreezeFlash(float flFreezeFlash) = 0;
};

extern IViewRender *g_pViewRender;

#endif // IVIEWRENDER_H
