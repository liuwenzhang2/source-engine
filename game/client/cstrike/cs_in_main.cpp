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
class CCSInput : public CUserInput
{
public:
};

static CCSInput g_Input;

// Expose this interface
IUserInput *input = ( IUserInput * )&g_Input;

