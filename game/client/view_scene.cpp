//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Responsible for drawing the scene
//
//===========================================================================//

#include "cbase.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "rendertexture.h"
#include "view_scene.h"
#include "iviewrender.h"
#include "sourcevr/isourcevirtualreality.h"
#include "client_virtualreality.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"



void ViewTransform( const Vector &worldSpace, Vector &viewSpace )
{
	const VMatrix &viewMatrix = engine->WorldToViewMatrix();
	Vector3DMultiplyPosition( viewMatrix, worldSpace, viewSpace );
}



//-----------------------------------------------------------------------------
// Purpose: Transforms a world-space position into a 2D position inside a supplied frustum.
//-----------------------------------------------------------------------------
int FrustumTransform( const VMatrix &worldToSurface, const Vector& point, Vector& screen )
{
	// UNDONE: Clean this up some, handle off-screen vertices
	float w;

	screen.x = worldToSurface[0][0] * point[0] + worldToSurface[0][1] * point[1] + worldToSurface[0][2] * point[2] + worldToSurface[0][3];
	screen.y = worldToSurface[1][0] * point[0] + worldToSurface[1][1] * point[1] + worldToSurface[1][2] * point[2] + worldToSurface[1][3];
	//	z		 = worldToSurface[2][0] * point[0] + worldToSurface[2][1] * point[1] + worldToSurface[2][2] * point[2] + worldToSurface[2][3];
	w		 = worldToSurface[3][0] * point[0] + worldToSurface[3][1] * point[1] + worldToSurface[3][2] * point[2] + worldToSurface[3][3];

	// Just so we have something valid here
	screen.z = 0.0f;

	bool behind;
	if( w < 0.001f )
	{
		behind = true;
		screen.x *= 100000;
		screen.y *= 100000;
	}
	else
	{
		behind = false;
		float invw = 1.0f / w;
		screen.x *= invw;
		screen.y *= invw;
	}

	return behind;
}


//-----------------------------------------------------------------------------
// Purpose: UNDONE: Clean this up some, handle off-screen vertices
// Input  : *point - 
//			*screen - 
// Output : int
//-----------------------------------------------------------------------------
int ScreenTransform( const Vector& point, Vector& screen )
{
	// UNDONE: Clean this up some, handle off-screen vertices
	return FrustumTransform ( engine->WorldToScreenMatrix(), point, screen );
}

//-----------------------------------------------------------------------------
// Purpose: Same as ScreenTransform, but transforms to HUD space.
//			These are totally different things in VR mode!
//-----------------------------------------------------------------------------
int HudTransform( const Vector& point, Vector& screen )
{
	if ( UseVR() )
	{
		return FrustumTransform ( g_ClientVirtualReality.GetHudProjectionFromWorld(), point, screen );
	}
	else
	{
		return FrustumTransform ( engine->WorldToScreenMatrix(), point, screen );
	}
}