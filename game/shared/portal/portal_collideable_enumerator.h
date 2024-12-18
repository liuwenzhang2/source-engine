//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef PORTAL_COLLIDEABLE_ENUMERATOR_H
#define PORTAL_COLLIDEABLE_ENUMERATOR_H

#ifdef _WIN32
#pragma once
#endif

#include "ispatialpartition.h"

#ifdef CLIENT_DLL
class C_Prop_Portal;
typedef C_Prop_Portal CProp_Portal;
#else
class CProp_Portal;
#endif
class CPortalSimulator;





#endif //#ifndef PORTAL_COLLIDEABLE_ENUMERATOR_H