//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//

#ifndef DECALS_H
#define DECALS_H
#ifdef _WIN32
#pragma once
#endif

abstract_class IDecalEmitterSystem
{
public:
	virtual int	GetDecalIndexForName( char const *decalname ) = 0;
	virtual const char *GetDecalNameForIndex( int nIndex ) = 0;
	virtual char const *TranslateDecalForGameMaterial( char const *decalName, unsigned char gamematerial ) = 0;
};

extern IDecalEmitterSystem *decalsystem;

#endif // DECALS_H
