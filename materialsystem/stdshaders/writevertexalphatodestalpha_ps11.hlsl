//====== Copyright ?1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "common_ps_fxc.hlsli"


struct PS_INPUT
{
	float4 vColor : COLOR0;
};

float4 main( PS_INPUT i ) : COLOR
{
	return float4( 1.0f, 1.0f, 1.0f, 1-i.vColor.a );
}
