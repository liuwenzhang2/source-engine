//======= Copyright 1996-2006, Valve Corporation, All rights reserved. ======
#define CONVERT_TO_SRGB 0

#include "common_ps_fxc.hlsli"

sampler TexSampler	: register( s0 );

struct PS_INPUT
{
	HALF2 baseTexCoord : TEXCOORD0; // Base texture coordinate
};

float4 main( PS_INPUT i ) : COLOR
{
	float4 result = tex2D( TexSampler, i.baseTexCoord );
	result.a = 1.0f;
	return result; //FinalOutput( result, 0, PIXEL_FOG_TYPE_NONE, TONEMAP_SCALE_NONE );
}
