#define HDRTYPE HDR_TYPE_NONE
#include "common_ps_fxc.hlsli"

struct PS_INPUT
{
	float4 vMorphWeights		: TEXCOORD0;
};

HALF4 main( PS_INPUT i ) : COLOR
{
	return i.vMorphWeights;
}

