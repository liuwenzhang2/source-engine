//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shared data definition file for touch links
//
//=============================================================================//

#ifndef TOUCHLINK_H
#define TOUCHLINK_H
#ifdef _WIN32
#pragma once
#endif


//-----------------------------------------------------------------------------
// Purpose: for resolving touch/untouch pairs
//-----------------------------------------------------------------------------
enum touchlink_flags_t
{
	FTOUCHLINK_START_TOUCH = 0x00000001,
};

struct touchlink_t
{
#if defined( CLIENT_DLL )
	C_BaseEntity		*entityTouched = NULL;
#else
	EHANDLE				entityTouched = NULL;
#endif
	int					touchStamp = 0;
	touchlink_t			*nextLink = NULL;
	touchlink_t			*prevLink = NULL;
	int					flags = 0;
};

// means this touchlink is managed external to the main physics system
#define TOUCHSTAMP_EVENT_DRIVEN		-1


#endif // TOUCHLINK_H
