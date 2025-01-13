//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef VIEW_SCENE_H
#define VIEW_SCENE_H
#ifdef _WIN32
#pragma once
#endif


#include "convar.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "rendertexture.h"
#include "materialsystem/itexture.h"

// Transform into view space (translate and rotate the camera into the origin).
void ViewTransform( const Vector &worldSpace, Vector &viewSpace );

// Transform a world point into normalized screen space (X and Y from -1 to 1).
// Returns 0 if the point is behind the viewer.
int ScreenTransform( const Vector& point, Vector& screen );
int HudTransform( const Vector& point, Vector& screen );

// reset the tonem apping to a constant value, and clear the filter bank
void ResetToneMapping(float value);


#endif // VIEW_SCENE_H
