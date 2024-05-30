//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef VCOLLIDE_H
#define VCOLLIDE_H
#ifdef _WIN32
#pragma once
#endif

class CPhysCollide;

struct vcollide_t
{
	unsigned short solidCount : 15;
	unsigned short isPacked : 1;
	unsigned short descSize = 0;
	// VPhysicsSolids
	CPhysCollide** solids = NULL;;
	char			*pKeyValues = NULL;
};

#endif // VCOLLIDE_H
