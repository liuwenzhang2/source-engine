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

#include "tier1/convar.h"
#include "ivrenderview.h"
#include "materialsystem/MaterialSystemUtil.h"

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
class CPortalRenderable;

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

//-----------------------------------------------------------------------------
// Portal rendering materials
//-----------------------------------------------------------------------------
struct PortalRenderingMaterials_t
{
	CMaterialReference	m_Wireframe;
	CMaterialReference	m_WriteZ_Model;
	CMaterialReference	m_TranslucentVertexColor;
};

struct PortalViewIDNode_t
{
	CUtlVector<PortalViewIDNode_t*> ChildNodes; //links will only be non-null if they're useful (can see through the portal at that depth and view setup)
	int iPrimaryViewID;
	//skybox view id is always primary + 1

	//In stencil mode this wraps CPortalRenderable::DrawStencilMask() and gives previous frames' results to CPortalRenderable::RenderPortalViewToBackBuffer()
	//In texture mode there's no good spot to auto-wrap occlusion tests. So you'll need to wrap it yourself for that.
	OcclusionQueryObjectHandle_t occlusionQueryHandle;
	int iWindowPixelsAtQueryTime;
	int iOcclusionQueryPixelsRendered;
	float fScreenFilledByPortalSurfaceLastFrame_Normalized;
};

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
struct ViewCustomVisibility_t
{
	ViewCustomVisibility_t()
	{
		m_nNumVisOrigins = 0;
		m_VisData.m_fDistToAreaPortalTolerance = FLT_MAX;
		m_iForceViewLeaf = -1;
	}

	void AddVisOrigin(const Vector& origin)
	{
		// Don't allow them to write past array length
		AssertMsg(m_nNumVisOrigins < MAX_VIS_LEAVES, "Added more origins than will fit in the array!");

		// If the vis origin count is greater than the size of our array, just fail to add this origin
		if (m_nNumVisOrigins >= MAX_VIS_LEAVES)
			return;

		m_rgVisOrigins[m_nNumVisOrigins++] = origin;
	}

	void ForceVisOverride(VisOverrideData_t& visData)
	{
		m_VisData = visData;
	}

	void ForceViewLeaf(int iViewLeaf)
	{
		m_iForceViewLeaf = iViewLeaf;
	}

	// Set to true if you want to use multiple origins for doing client side map vis culling
	// NOTE:  In generaly, you won't want to do this, and by default the 3d origin of the camera, as above,
	//  will be used as the origin for vis, too.
	int				m_nNumVisOrigins;
	// Array of origins
	Vector			m_rgVisOrigins[MAX_VIS_LEAVES];

	// The view data overrides for visibility calculations with area portals
	VisOverrideData_t m_VisData;

	// The starting leaf to determing which area to start in when performing area portal culling on the engine
	// Default behavior is to use the leaf the camera position is in.
	int				m_iForceViewLeaf;
};

typedef CPortalRenderable* (*PortalRenderableCreationFunc)(void);

abstract_class IViewRender
{
public:
	// SETUP
	// Initialize view renderer
	virtual void		Init( void ) = 0;

	// Clear any systems between levels
	virtual void		LevelInit( void ) = 0;
	// Inherited from IGameSystem
	virtual void		LevelInitPreEntity() = 0;
	virtual void		LevelShutdownPreEntity() = 0;
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

	virtual void		CopyToCurrentView(const CViewSetup& viewSetup) = 0;
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

	virtual void		UpdateScreenEffectTexture(void) = 0;
	virtual void		UpdateScreenEffectTexture(int textureIndex, int x, int y, int w, int h, bool bDestFullScreen = false, Rect_t* pActualRect = NULL) = 0;
	virtual void		UpdateFullScreenDepthTexture(void) = 0;
	virtual void		DrawScreenEffectMaterial(IMaterial* pMaterial, int x, int y, int w, int h) = 0;

	virtual void		UpdateRefractTexture(bool bForceUpdate = false) = 0;
	virtual void		UpdateRefractTexture(int x, int y, int w, int h, bool bForceUpdate = false) = 0;

	virtual void		UpdateFrontBufferTexturesForMaterial(IMaterial* pMaterial, bool bForce = false) = 0;

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
#ifdef _DEBUG
	virtual void SetRenderingCameraView(bool bRenderingCameraView) = 0;
#endif

	// tests if the parameter ID is being used by portal pixel vis queries
	virtual bool IsPortalViewID(view_id_t id) = 0;

	virtual PortalViewIDNode_t* AllocPortalViewIDNode(int iChildLinkCount) = 0;
	virtual void FreePortalViewIDNode(PortalViewIDNode_t* pNode) = 0;
	virtual PortalViewIDNode_t& GetHeadPortalViewIDNode() = 0;
	virtual PortalViewIDNode_t** GetPortalViewIDNodeChain() = 0;
	virtual int	GetViewRecursionLevel() = 0;
	virtual void SetViewRecursionLevel(int iViewRecursionLevel) = 0;
	virtual void SetRemainingPortalViewDepth(int iRemainingPortalViewDepth) = 0;
	virtual void SetRenderingViewForPortal(CPortalRenderable* pRenderingViewForPortal) = 0;
	virtual void SetRenderingViewExitPortal(CPortalRenderable* pRenderingViewExitPortal) = 0;
	virtual const PortalRenderingMaterials_t& GetMaterialsAccess() = 0;
	virtual CUtlVector<VPlane>* GetRecursiveViewComplexFrustums() = 0;
	virtual CUtlVector<CPortalRenderable*>& GetAllPortals() = 0;

	// Are we currently rendering a portal?
	virtual bool IsRenderingPortal() const = 0;

	// Returns the current View IDs. Portal View IDs will change often (especially with recursive views) and should not be cached
	virtual int GetCurrentViewId() const = 0;
	virtual int GetCurrentSkyboxViewId() const = 0;

	// Returns view recursion level
	virtual int GetViewRecursionLevel() const = 0;

	virtual void UpdatePortalPixelVisibility(void) = 0;
	virtual float GetPixelVisilityForPortalSurface(const CPortalRenderable* pPortal) const = 0; //normalized for how many of the screen's possible pixels it takes up, less than zero indicates a lack of data from last frame

	// Returns the remaining number of portals to render within other portals
	// lets portals know that they should do "end of the line" kludges to cover up that portals don't go infinitely recursive
	virtual int	GetRemainingPortalViewDepth() const = 0;

	virtual CPortalRenderable* GetCurrentViewEntryPortal(void) const = 0; //if rendering a portal view, this is the portal the current view enters into
	virtual CPortalRenderable* GetCurrentViewExitPortal(void) const = 0; //if rendering a portal view, this is the portal the current view exits from

	// true if the rendering path for portals uses stencils instead of textures
	virtual bool ShouldUseStencilsToRenderPortals() const = 0;

	// Intended for use in the middle of another ViewDrawScene call, this allows stencils to be drawn after opaques but before translucents are drawn in the main view.
	virtual void ViewDrawScene_PortalStencil(const CViewSetup& view, ViewCustomVisibility_t* pCustomVisibility) = 0;
	virtual void Draw3dSkyboxworld_Portal(const CViewSetup& view, int& nClearFlags, bool& bDrew3dSkybox, SkyboxVisibility_t& nSkyboxVisible, ITexture* pRenderTarget = NULL) = 0;
	virtual void ViewDrawScene(bool bDrew3dSkybox, SkyboxVisibility_t nSkyboxVisible, const CViewSetup& view, int nClearFlags, view_id_t viewID, bool bDrawViewModel = false, int baseDrawFlags = 0, ViewCustomVisibility_t* pCustomVisibility = NULL) = 0;
	//it's a good idea to force cheaper water when the ratio of performance gain to noticability is high
	//0 = force no reflection/refraction
	//1/2 = downgrade to simple/world reflections as seen in advanced video options
	//3 = no downgrade
	virtual int ShouldForceCheaperWaterLevel() const = 0;

	virtual bool ShouldObeyStencilForClears() const = 0;

	//sometimes we have to tweak some systems to render water properly with portals
	virtual void WaterRenderingHandler_PreReflection() const = 0;
	virtual void WaterRenderingHandler_PostReflection() const = 0;
	virtual void WaterRenderingHandler_PreRefraction() const = 0;
	virtual void WaterRenderingHandler_PostRefraction() const = 0;

	// return value indicates that something was done, and render lists should be rebuilt afterwards
	virtual bool DrawPortalsUsingStencils() = 0;

	virtual void DrawPortalsToTextures(const CViewSetup& cameraView) = 0; //updates portal textures
	virtual void OverlayPortalRenderTargets(float w, float h) = 0;

	virtual void UpdateDepthDoublerTexture(const CViewSetup& viewSetup) = 0; //our chance to update all depth doubler texture before the view model is added to the back buffer

	virtual void EnteredPortal(CPortalRenderable* pEnteredPortal) = 0; //does a bit of internal maintenance whenever the player/camera has logically passed the portal threshold

	// adds, removes a portal to the set of renderable portals
	virtual void AddPortal(CPortalRenderable* pPortal) = 0;
	virtual void RemovePortal(CPortalRenderable* pPortal) = 0;

	// Methods to query about the exit portal associated with the currently rendering portal
	virtual void ShiftFogForExitPortalView() const = 0;
	virtual const Vector& GetExitPortalFogOrigin() const = 0;
	virtual SkyboxVisibility_t IsSkyboxVisibleFromExitPortal() const = 0;
	virtual bool DoesExitPortalViewIntersectWaterPlane(float waterZ, int leafWaterDataID) const = 0;

	virtual void HandlePortalPlaybackMessage(KeyValues* pKeyValues) = 0;

	virtual CPortalRenderable* FindRecordedPortal(IClientRenderable* pRenderable) = 0;
	virtual void AddPortalCreationFunc(const char* szPortalType, PortalRenderableCreationFunc creationFunc) = 0;

	virtual CPortalRenderable* FindRecordedPortal(int nPortalId) = 0;

};

extern IViewRender *g_pViewRender;

// near and far Z it uses to render the world.
#define VIEW_NEARZ	3
//#define VIEW_FARZ	28400
#define TEMP_DISABLE_PORTAL_VIS_QUERY

static inline int WireFrameMode(void)
{
	ConVarRef sv_cheats("sv_cheats");
	ConVarRef mat_wireframe("mat_wireframe");
	if (sv_cheats.IsValid() && sv_cheats.GetBool())
		return mat_wireframe.GetInt();
	else
		return 0;
}

static inline bool ShouldDrawInWireFrameMode(void)
{
	ConVarRef sv_cheats("sv_cheats");
	ConVarRef mat_wireframe("mat_wireframe");
	if (sv_cheats.IsValid() && sv_cheats.GetBool())
		return (mat_wireframe.GetInt() != 0);
	else
		return false;
}

static inline float ScaleFOVByWidthRatio(float fovDegrees, float ratio)
{
	float halfAngleRadians = fovDegrees * (0.5f * M_PI / 180.0f);
	float t = tan(halfAngleRadians);
	t *= ratio;
	float retDegrees = (180.0f / M_PI) * atan(t);
	return retDegrees * 2.0f;
}

#endif // IVIEWRENDER_H
