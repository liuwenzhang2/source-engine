//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//

#if !defined( VIEWRENDER_H )
#define VIEWRENDER_H
#ifdef _WIN32
#pragma once
#endif

#include "networkvar.h"
#include "shareddefs.h"
#include "tier1/utlstack.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "replay/ireplayscreenshotsystem.h"
#include "cdll_int.h"
#include "clientleafsystem.h"
#include "PortalRender.h"

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class ConVar;
class CClientRenderablesList;
class IClientVehicle;
class C_PointCamera;
class IScreenSpaceEffect;
class CClientViewSetup;
class CViewRender;
struct ClientWorldListInfo_t;
struct WriteReplayScreenshotParams_t;
class CReplayScreenshotTaker;

#ifdef HL2_EPISODIC
	class CStunEffect;
#endif // HL2_EPISODIC

//-----------------------------------------------------------------------------
// Purpose: Stored pitch drifting variables
//-----------------------------------------------------------------------------
class CPitchDrift
{
public:
	float		pitchvel;
	bool		nodrift;
	float		driftmove;
	double		laststop;
};

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
struct WaterRenderInfo_t
{
	bool m_bCheapWater : 1;
	bool m_bReflect : 1;
	bool m_bRefract : 1;
	bool m_bReflectEntities : 1;
	bool m_bDrawWaterSurface : 1;
	bool m_bOpaqueWater : 1;

};

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
class CBase3dView : public CRefCounted<>,
					protected CViewSetup
{
	DECLARE_CLASS_NOBASE( CBase3dView );
public:
	CBase3dView( CViewRender *pMainView );

	VPlane *		GetFrustum();
	virtual int		GetDrawFlags() { return 0; }

	virtual	void	EnableWorldFog() {};

protected:
	// @MULTICORE (toml 8/11/2006): need to have per-view frustum. Change when move view stack to client
	VPlane			*m_Frustum;
	CViewRender *m_pMainView;
};

//-----------------------------------------------------------------------------
// Base class for 3d views
//-----------------------------------------------------------------------------
class CRendering3dView : public CBase3dView
{
	DECLARE_CLASS( CRendering3dView, CBase3dView );
public:
	CRendering3dView( CViewRender *pMainView );
	virtual ~CRendering3dView() { ReleaseLists(); }

	void Setup( const CViewSetup &setup );

	// What are we currently rendering? Returns a combination of DF_ flags.
	virtual int		GetDrawFlags();

	virtual void	Draw() {};

protected:

	// Fog setup
	void			EnableWorldFog( void );
	void			SetFogVolumeState( const VisibleFogVolumeInfo_t &fogInfo, bool bUseHeightFog );

	// Draw setup
	void			SetupRenderablesList( int viewID );

	void			UpdateRenderablesOpacity();

	// If iForceViewLeaf is not -1, then it uses the specified leaf as your starting area for setting up area portal culling.
	// This is used by water since your reflected view origin is often in solid space, but we still want to treat it as though
	// the first portal we're looking out of is a water portal, so our view effectively originates under the water.
	void			BuildWorldRenderLists( bool bDrawEntities, int iForceViewLeaf = -1, bool bUseCacheIfEnabled = true, bool bShadowDepth = false, float *pReflectionWaterHeight = NULL );

	// Purpose: Builds render lists for renderables. Called once for refraction, once for over water
	void			BuildRenderableRenderLists( int viewID );

	// More concise version of the above BuildRenderableRenderLists().  Called for shadow depth map rendering
	void			BuildShadowDepthRenderableRenderLists();

	void			DrawWorld( float waterZAdjust );

	// Draws all opaque/translucent renderables in leaves that were rendered
	void			DrawOpaqueRenderables( ERenderDepthMode DepthMode );
	void			DrawTranslucentRenderables( bool bInSkybox, bool bShadowDepth );

	// Renders all translucent entities in the render list
	void			DrawTranslucentRenderablesNoWorld( bool bInSkybox );

	// Draws translucent renderables that ignore the Z buffer
	void			DrawNoZBufferTranslucentRenderables( void );

	// Renders all translucent world surfaces in a particular set of leaves
	void			DrawTranslucentWorldInLeaves( bool bShadowDepth );

	// Renders all translucent world + detail objects in a particular set of leaves
	void			DrawTranslucentWorldAndDetailPropsInLeaves( int iCurLeaf, int iFinalLeaf, int nEngineDrawFlags, int &nDetailLeafCount, LeafIndex_t* pDetailLeafList, bool bShadowDepth );

	// Purpose: Computes the actual world list info based on the render flags
	void			PruneWorldListInfo();

#ifdef PORTAL
	virtual bool	ShouldDrawPortals() { return true; }
#endif

	void ReleaseLists();

	//-----------------------------------------------
	// Combination of DF_ flags.
	int m_DrawFlags;
	int m_ClearFlags;

	IWorldRenderList *m_pWorldRenderList;
	CClientRenderablesList *m_pRenderablesList;
	ClientWorldListInfo_t *m_pWorldListInfo;
	ViewCustomVisibility_t *m_pCustomVisibility;
};


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------

class CRenderExecutor
{
	DECLARE_CLASS_NOBASE( CRenderExecutor );
public:
	virtual void AddView( CRendering3dView *pView ) = 0;
	virtual void Execute() = 0;

protected:
	CRenderExecutor( CViewRender *pMainView ) : m_pMainView( pMainView ) {}
	CViewRender *m_pMainView;
};

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------

class CSimpleRenderExecutor : public CRenderExecutor
{
	DECLARE_CLASS( CSimpleRenderExecutor, CRenderExecutor );
public:
	CSimpleRenderExecutor( CViewRender *pMainView ) : CRenderExecutor( pMainView ) {}

	void AddView( CRendering3dView *pView );
	void Execute() {}
};

struct RecordedPortalInfo_t
{
	CPortalRenderable* m_pActivePortal;
	int m_nPortalId;
	IClientRenderable* m_pPlaybackRenderable;
};

struct PortalRenderableCreationFunction_t
{
	CUtlString portalType;
	PortalRenderableCreationFunc creationFunc;
};

//-----------------------------------------------------------------------------
// Purpose: Implements the interface to view rendering for the client .dll
//-----------------------------------------------------------------------------

class CViewRender : public IViewRender,
					public IReplayScreenshotSystem
{
	friend class CRendering3dView;
	friend class CSkyboxView;
	friend class CBaseWorldView;
	friend class CAboveWaterView;
	friend class CShadowDepthView;
	friend class CPortalSkyboxView;
	friend class CFreezeFrameView;
	friend class CSimpleWorldView;
	DECLARE_CLASS_NOBASE( CViewRender );
public:
	virtual void	Init( void );
	virtual void	Shutdown( void );

	const CViewSetup *GetPlayerViewSetup( ) const;

	virtual void	StartPitchDrift( void );
	virtual void	StopPitchDrift( void );

	virtual float	GetZNear();
	virtual float	GetZFar();

	virtual void	OnRenderStart();
	void			DriftPitch (void);

	//static CViewRender *	GetMainView() { return assert_cast<CViewRender *>( view ); }

	void			AddViewToScene( CRendering3dView *pView ) { m_SimpleExecutor.AddView( pView ); }
protected:
	// Sets up the view parameters for all views (left, middle and right eyes).
    void            SetUpViews();

	// Sets up the view parameters of map overview mode (cl_leveloverview)
	void			SetUpOverView();

	// generates a low-res screenshot for save games
	virtual void	WriteSaveGameScreenshotOfSize( const char *pFilename, int width, int height, bool bCreatePowerOf2Padded = false, bool bWriteVTF = false );
	void			WriteSaveGameScreenshot( const char *filename );

	virtual IReplayScreenshotSystem *GetReplayScreenshotSystem() { return this; }

	// IReplayScreenshot implementation
	virtual void	WriteReplayScreenshot( WriteReplayScreenshotParams_t &params );
	virtual void	UpdateReplayScreenshotCache();

    StereoEye_t		GetFirstEye() const;
    StereoEye_t		GetLastEye() const;
    CViewSetup &    GetView(StereoEye_t eEye);
    const CViewSetup &    GetView(StereoEye_t eEye) const ;


	// This stores all of the view setup parameters that the engine needs to know about.
    // Best way to pick the right one is with ::GetView(), rather than directly.
	CViewSetup		m_View;         // mono <- in stereo mode, this will be between the two eyes and is the "main" view.
	CViewSetup		m_ViewLeft;     // left (unused for mono)
	CViewSetup		m_ViewRight;    // right (unused for mono)

	// Pitch drifting data
	CPitchDrift		m_PitchDrift;

public:
					CViewRender();
	virtual			~CViewRender( void ) {}

// Implementation of IViewRender interface
public:

	void			SetupVis( const CViewSetup& view, unsigned int &visFlags, ViewCustomVisibility_t *pCustomVisibility = NULL );


	// Render functions
	virtual	void	Render( vrect_t *rect );
	virtual void	RenderView( const CViewSetup &view, int nClearFlags, int whatToDraw );
	virtual void	RenderPlayerSprites();
	virtual void	Render2DEffectsPreHUD( const CViewSetup &view );
	virtual void	Render2DEffectsPostHUD( const CViewSetup &view );
	virtual void	CopyToCurrentView(const CViewSetup& viewSetup);

	void			DisableFog( void );

	// Called once per level change
	void			LevelInit( void );
	void			LevelInitPreEntity();
	void			LevelShutdownPreEntity();
	void			LevelShutdown( void );

	// Add entity to transparent entity queue

	bool			ShouldDrawEntities( void );
	bool			ShouldDrawBrushModels( void );

	const CViewSetup *GetViewSetup( ) const;
	
	void			DisableVis( void );

	// Sets up the view model position relative to the local player
	void			MoveViewModels( );

	// Gets the abs origin + angles of the view models
	void			GetViewModelPosition( int nIndex, Vector *pPos, QAngle *pAngle );

	void			SetCheapWaterStartDistance( float flCheapWaterStartDistance );
	void			SetCheapWaterEndDistance( float flCheapWaterEndDistance );

	void			GetWaterLODParams( float &flCheapWaterStartDistance, float &flCheapWaterEndDistance );

	virtual void	QueueOverlayRenderView( const CViewSetup &view, int nClearFlags, int whatToDraw );

	virtual void	GetScreenFadeDistances( float *min, float *max );

	virtual IClientEntity *GetCurrentlyDrawingEntity();
	virtual void		  SetCurrentlyDrawingEntity( IClientEntity *pEnt );

	virtual bool		UpdateShadowDepthTexture( ITexture *pRenderTarget, ITexture *pDepthTexture, const CViewSetup &shadowView );

	int GetBaseDrawFlags() { return m_BaseDrawFlags; }
	virtual bool ShouldForceNoVis()  { return m_bForceNoVis; }
	int				BuildRenderablesListsNumber() const { return m_BuildRenderableListsNumber; }
	int				IncRenderablesListsNumber()  { return ++m_BuildRenderableListsNumber; }

	int				BuildWorldListsNumber() const;
	int				IncWorldListsNumber() { return ++m_BuildWorldListsNumber; }

	virtual VPlane*	GetFrustum() { return ( m_pActiveRenderer ) ? m_pActiveRenderer->GetFrustum() : m_Frustum; }

	// What are we currently rendering? Returns a combination of DF_ flags.
	virtual int		GetDrawFlags() { return ( m_pActiveRenderer ) ? m_pActiveRenderer->GetDrawFlags() : 0; }

	CBase3dView *	GetActiveRenderer() { return m_pActiveRenderer; }
	CBase3dView *	SetActiveRenderer( CBase3dView *pActiveRenderer ) { CBase3dView *pPrevious = m_pActiveRenderer; m_pActiveRenderer =  pActiveRenderer; return pPrevious; }

	void			FreezeFrame( float flFreezeTime );

	void SetWaterOverlayMaterial( IMaterial *pMaterial )
	{
		m_UnderWaterOverlayMaterial.Init( pMaterial );
	}

//-----------------------------------------------------------------------------
// There's a difference between the 'current view' and the 'main view'
// The 'main view' is where the player is sitting. Current view is just
// what's currently being rendered, which, owing to monitors or water,
// could be just about anywhere.
//-----------------------------------------------------------------------------
	const Vector& PrevMainViewOrigin();
	const QAngle& PrevMainViewAngles();
	const Vector& MainViewOrigin();
	const QAngle& MainViewAngles();
	const VMatrix& MainWorldToViewMatrix();
	const Vector& MainViewForward();
	const Vector& MainViewRight();
	const Vector& MainViewUp();

	void AllowCurrentViewAccess(bool allow);
	bool IsCurrentViewAccessAllowed();
	view_id_t CurrentViewID();
	const Vector& CurrentViewOrigin();
	const QAngle& CurrentViewAngles();
	const VMatrix& CurrentWorldToViewMatrix();
	const Vector& CurrentViewForward();
	const Vector& CurrentViewRight();
	const Vector& CurrentViewUp();
	bool DrawingShadowDepthView(void);
	bool DrawingMainView();

	bool IsRenderingScreenshot() {
		return g_bRenderingScreenshot;
	}

	IntroData_t* GetIntroData() {
		return g_pIntroData;
	}
	void SetIntroData(IntroData_t* pIntroData) {
		g_pIntroData = pIntroData;
	}

	void SetFreezeFlash(float flFreezeFlash) {
		g_flFreezeFlash = flFreezeFlash;
	}

#ifdef _DEBUG
	void SetRenderingCameraView(bool bRenderingCameraView) {
		g_bRenderingCameraView = bRenderingCameraView;
	}
#endif

	// tests if the parameter ID is being used by portal pixel vis queries
	bool IsPortalViewID(view_id_t id);
	PortalViewIDNode_t* AllocPortalViewIDNode(int iChildLinkCount);
	void FreePortalViewIDNode(PortalViewIDNode_t* pNode);
	PortalViewIDNode_t& GetHeadPortalViewIDNode() { return m_HeadPortalViewIDNode; }
	PortalViewIDNode_t** GetPortalViewIDNodeChain() { return m_PortalViewIDNodeChain; }
	int	GetViewRecursionLevel() { return m_iViewRecursionLevel; }
	void SetViewRecursionLevel(int iViewRecursionLevel) { m_iViewRecursionLevel = iViewRecursionLevel; }
	void SetRemainingPortalViewDepth(int iRemainingPortalViewDepth) { m_iRemainingPortalViewDepth = iRemainingPortalViewDepth; }
	void SetRenderingViewForPortal(CPortalRenderable* pRenderingViewForPortal) { m_pRenderingViewForPortal = pRenderingViewForPortal; }
	void SetRenderingViewExitPortal(CPortalRenderable* pRenderingViewExitPortal) { m_pRenderingViewExitPortal = pRenderingViewExitPortal; }
	const PortalRenderingMaterials_t& GetMaterialsAccess() { return m_MaterialsAccess; }
	CUtlVector<VPlane>* GetRecursiveViewComplexFrustums() { return m_RecursiveViewComplexFrustums; }
	CUtlVector<CPortalRenderable*>& GetAllPortals() { return m_AllPortals; }

	// Are we currently rendering a portal?
	bool IsRenderingPortal() const;

	// Returns the current View IDs. Portal View IDs will change often (especially with recursive views) and should not be cached
	int GetCurrentViewId() const;
	int GetCurrentSkyboxViewId() const;

	// Returns view recursion level
	int GetViewRecursionLevel() const;

	float GetPixelVisilityForPortalSurface(const CPortalRenderable* pPortal) const; //normalized for how many of the screen's possible pixels it takes up, less than zero indicates a lack of data from last frame

	// Returns the remaining number of portals to render within other portals
	// lets portals know that they should do "end of the line" kludges to cover up that portals don't go infinitely recursive
	int	GetRemainingPortalViewDepth() const;

	inline CPortalRenderable* GetCurrentViewEntryPortal(void) const { return m_pRenderingViewForPortal; }; //if rendering a portal view, this is the portal the current view enters into
	inline CPortalRenderable* GetCurrentViewExitPortal(void) const { return m_pRenderingViewExitPortal; }; //if rendering a portal view, this is the portal the current view exits from

	// true if the rendering path for portals uses stencils instead of textures
	bool ShouldUseStencilsToRenderPortals() const;

	//it's a good idea to force cheaper water when the ratio of performance gain to noticability is high
	//0 = force no reflection/refraction
	//1/2 = downgrade to simple/world reflections as seen in advanced video options
	//3 = no downgrade
	int ShouldForceCheaperWaterLevel() const;

	bool ShouldObeyStencilForClears() const;

	//sometimes we have to tweak some systems to render water properly with portals
	void WaterRenderingHandler_PreReflection() const;
	void WaterRenderingHandler_PostReflection() const;
	void WaterRenderingHandler_PreRefraction() const;
	void WaterRenderingHandler_PostRefraction() const;

	// return value indicates that something was done, and render lists should be rebuilt afterwards
	bool DrawPortalsUsingStencils();

	void DrawPortalsToTextures(const CViewSetup& cameraView); //updates portal textures
	void OverlayPortalRenderTargets(float w, float h);

	void UpdateDepthDoublerTexture(const CViewSetup& viewSetup); //our chance to update all depth doubler texture before the view model is added to the back buffer

	void EnteredPortal(CPortalRenderable* pEnteredPortal); //does a bit of internal maintenance whenever the player/camera has logically passed the portal threshold

	// adds, removes a portal to the set of renderable portals
	void AddPortal(CPortalRenderable* pPortal);
	void RemovePortal(CPortalRenderable* pPortal);

	// Methods to query about the exit portal associated with the currently rendering portal
	void ShiftFogForExitPortalView() const;
	const Vector& GetExitPortalFogOrigin() const;
	SkyboxVisibility_t IsSkyboxVisibleFromExitPortal() const;
	bool DoesExitPortalViewIntersectWaterPlane(float waterZ, int leafWaterDataID) const;

	void HandlePortalPlaybackMessage(KeyValues* pKeyValues);

	CPortalRenderable* FindRecordedPortal(IClientRenderable* pRenderable);
	void AddPortalCreationFunc(const char* szPortalType, PortalRenderableCreationFunc creationFunc);

	CViewSetup m_RecursiveViewSetups[MAX_PORTAL_RECURSIVE_VIEWS]; //before we recurse into a view, we backup the view setup here for reference

	void UpdatePortalPixelVisibility(void); //updates pixel visibility for portal surfaces
private:
	bool IsMainView(view_id_t id);

	void SetupCurrentView(const Vector& vecOrigin, const QAngle& angles, view_id_t viewID);
	void FinishCurrentView()
	{
		s_bCanAccessCurrentView = false;
	}

	bool DoesViewPlaneIntersectWater(float waterZ, int leafWaterDataID);

	// Handles a portal update message
	void HandlePortalUpdateMessage(KeyValues* pKeyValues);

	// Finds a recorded portal
	int FindRecordedPortalIndex(int nPortalId);
	CPortalRenderable* FindRecordedPortal(int nPortalId);

	void DrawOpaqueRenderables_DrawStaticProps(CClientRenderablesList::CEntry* pEntitiesBegin, CClientRenderablesList::CEntry* pEntitiesEnd, ERenderDepthMode DepthMode);
	void DrawOpaqueRenderables_DrawBrushModels(CClientRenderablesList::CEntry* pEntitiesBegin, CClientRenderablesList::CEntry* pEntitiesEnd, ERenderDepthMode DepthMode);
	void DrawOpaqueRenderable(IClientRenderable* pEnt, bool bTwoPass, ERenderDepthMode DepthMode, int nDefaultFlags = 0);
	void DrawOpaqueRenderables_Range(CClientRenderablesList::CEntry* pEntitiesBegin, CClientRenderablesList::CEntry* pEntitiesEnd, ERenderDepthMode DepthMode);
	void DrawTranslucentRenderable(IClientRenderable* pEnt, bool twoPass, bool bShadowDepth, bool bIgnoreDepth);
	void DrawClippedDepthBox(IClientRenderable* pEnt, float* pClipPlane);

	int				m_BuildWorldListsNumber;


	// General draw methods
	// baseDrawFlags is a combination of DF_ defines. DF_MONITOR is passed into here while drawing a monitor.
	void			ViewDrawScene( bool bDrew3dSkybox, SkyboxVisibility_t nSkyboxVisible, const CViewSetup &view, int nClearFlags, view_id_t viewID, bool bDrawViewModel = false, int baseDrawFlags = 0, ViewCustomVisibility_t *pCustomVisibility = NULL );

	void			DrawMonitors( const CViewSetup &cameraView );

	bool			DrawOneMonitor( ITexture *pRenderTarget, int cameraNum, C_PointCamera *pCameraEnt, const CViewSetup &cameraView, IClientEntity *localPlayer, 
						int x, int y, int width, int height );

	// Drawing primitives
	bool			ShouldDrawViewModel( bool drawViewmodel );
	void			DrawViewModels( const CViewSetup &view, bool drawViewmodel );

	void			PerformScreenSpaceEffects( int x, int y, int w, int h );

	// Overlays
	void			SetScreenOverlayMaterial( IMaterial *pMaterial );
	IMaterial		*GetScreenOverlayMaterial( );
	void			PerformScreenOverlay( int x, int y, int w, int h );

	void DrawUnderwaterOverlay( void );

	// Water-related methods
	void			DrawWorldAndEntities( bool drawSkybox, const CViewSetup &view, int nClearFlags, ViewCustomVisibility_t *pCustomVisibility = NULL );

	virtual void			ViewDrawScene_Intro( const CViewSetup &view, int nClearFlags, const IntroData_t &introData );

	// Intended for use in the middle of another ViewDrawScene call, this allows stencils to be drawn after opaques but before translucents are drawn in the main view.
	void			ViewDrawScene_PortalStencil( const CViewSetup &view, ViewCustomVisibility_t *pCustomVisibility );
	void			Draw3dSkyboxworld_Portal( const CViewSetup &view, int &nClearFlags, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible, ITexture *pRenderTarget = NULL );

	// Determines what kind of water we're going to use
	void			DetermineWaterRenderInfo( const VisibleFogVolumeInfo_t &fogVolumeInfo, WaterRenderInfo_t &info );

	bool			UpdateRefractIfNeededByList( CUtlVector< IClientRenderable * > &list );
	void			DrawRenderablesInList( CUtlVector< IClientRenderable * > &list, int flags = 0 );

	// Sets up, cleans up the main 3D view
	void			SetupMain3DView( const CViewSetup &view, int &nClearFlags );
	void			CleanupMain3DView( const CViewSetup &view );


	// This stores the current view
 	CViewSetup		m_CurrentView;

	// VIS Overrides
	// Set to true to turn off client side vis ( !!!! rendering will be slow since everything will draw )
	bool			m_bForceNoVis;	

	// Some cvars needed by this system
	const ConVar	*m_pDrawEntities;
	const ConVar	*m_pDrawBrushModels;

	// Some materials used...
	CMaterialReference	m_TranslucentSingleColor;
	CMaterialReference	m_ModulateSingleColor;
	CMaterialReference	m_ScreenOverlayMaterial;
	CMaterialReference m_UnderWaterOverlayMaterial;

	Vector			m_vecLastFacing;
	float			m_flCheapWaterStartDistance;
	float			m_flCheapWaterEndDistance;

	CViewSetup			m_OverlayViewSetup;
	int					m_OverlayClearFlags;
	int					m_OverlayDrawFlags;
	bool				m_bDrawOverlay;

	int					m_BaseDrawFlags;	// Set in ViewDrawScene and OR'd into m_DrawFlags as it goes.
	IClientEntity		*m_pCurrentlyDrawingEntity;

#if defined( CSTRIKE_DLL )
	float				m_flLastFOV;
#endif

	//friend class CPortalRender; //portal drawing needs muck with views in weird ways
	//friend class CPortalRenderable;
	int				m_BuildRenderableListsNumber;

	friend class CBase3dView;

	Frustum m_Frustum;

	CBase3dView *m_pActiveRenderer;
	CSimpleRenderExecutor m_SimpleExecutor;

	bool			m_rbTakeFreezeFrame[ STEREO_EYE_MAX ];
	float			m_flFreezeFrameUntil;

#if defined( REPLAY_ENABLED )
	CReplayScreenshotTaker	*m_pReplayScreenshotTaker;
#endif

	//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
	Vector g_vecCurrentRenderOrigin = Vector(0, 0, 0);
	QAngle g_vecCurrentRenderAngles = QAngle(0, 0, 0);
	Vector g_vecCurrentVForward = Vector(0, 0, 0);
	Vector g_vecCurrentVRight = Vector(0, 0, 0);
	Vector g_vecCurrentVUp = Vector(0, 0, 0);
	VMatrix g_matCurrentCamInverse;
	int g_CurrentViewID = VIEW_NONE;
	bool s_bCanAccessCurrentView = false;

	// These are the vectors for the "main" view - the one the player is looking down.
// For stereo views, they are the vectors for the middle eye.
	Vector g_vecPrevRenderOrigin = Vector(0, 0, 0);	// Last frame's render origin
	QAngle g_vecPrevRenderAngles = QAngle(0, 0, 0); // Last frame's render angles
	Vector g_vecRenderOrigin = Vector(0, 0, 0);
	QAngle g_vecRenderAngles = QAngle(0, 0, 0);
	Vector g_vecVForward = Vector(0, 0, 0);
	Vector g_vecVRight = Vector(0, 0, 0);
	Vector g_vecVUp = Vector(0, 0, 0);
	VMatrix g_matCamInverse;

	bool g_bRenderingView = false;			// For debugging...
	bool g_bRenderingScreenshot = false;
#if _DEBUG
	bool g_bRenderingCameraView = false;
#endif
	float g_flFreezeFlash = 0.0f;

	// Robin, make this point at something to get intro mode.
	IntroData_t* g_pIntroData = NULL;

	CMaterialReference g_material_WriteZ; //init'ed on by CViewRender::Init()

	//-------------------------------------------
//Portal View ID Node helpers
//-------------------------------------------
	CUtlVector<int> s_iFreedViewIDs; //when a view id node gets freed, it's primary view id gets added here

	PortalViewIDNode_t m_HeadPortalViewIDNode; //pseudo node. Primary view id will be VIEW_MAIN. The child links are what we really care about
	PortalViewIDNode_t* m_PortalViewIDNodeChain[MAX_PORTAL_RECURSIVE_VIEWS]; //the view id node chain we're following, 0 always being &m_HeadPortalViewIDNode (offsetting by 1 seems like it'd cause bugs in the long run)

	CUtlVector<PortalRenderableCreationFunction_t> m_PortalRenderableCreators; //for SFM compatibility

	PortalRenderingMaterials_t	m_Materials;
	int							m_iViewRecursionLevel;
	int							m_iRemainingPortalViewDepth; //let's portals know that they should do "end of the line" kludges to cover up that portals don't go infinitely recursive

	CPortalRenderable* m_pRenderingViewForPortal; //the specific pointer for the portal that we're rending a view for
	CPortalRenderable* m_pRenderingViewExitPortal; //the specific pointer for the portal that our view exits from

	CUtlVector<CPortalRenderable*>		m_AllPortals; //All portals currently in memory, active or not
	CUtlVector<CPortalRenderable*>		m_ActivePortals;
	CUtlVector< RecordedPortalInfo_t >	m_RecordedPortals;

	//frustums with more (or less) than 6 planes. Store each recursion level's custom frustum here so further recursions can be better optimized.
	//When going into further recursions, if you've failed to fill in a complex frustum, the standard frustum will be copied in.
	//So all parent levels are guaranteed to contain valid data
	CUtlVector<VPlane>					m_RecursiveViewComplexFrustums[MAX_PORTAL_RECURSIVE_VIEWS];
	const PortalRenderingMaterials_t& m_MaterialsAccess;
};

extern ConVar mat_wireframe;


#endif // VIEWRENDER_H
