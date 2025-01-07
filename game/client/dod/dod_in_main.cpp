//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: TF2 specific input handling
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "kbutton.h"
#include "input.h"

//-----------------------------------------------------------------------------
// Purpose: TF Input interface
//-----------------------------------------------------------------------------
class CDODInput : public CUserInput
{
public:
};

static CDODInput g_Input;

// Expose this interface
IUserInput *input = ( IUserInput * )&g_Input;

