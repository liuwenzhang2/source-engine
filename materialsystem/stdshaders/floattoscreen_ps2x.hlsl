// STATIC: "CONVERT_TO_SRGB" "0..1"	[ps20b][= g_pHardwareConfig->NeedsShaderSRGBConversion()] [PC]
// STATIC: "CONVERT_TO_SRGB" "0..0"	[= 0] [XBOX]

#define HDRTYPE HDR_TYPE_FLOAT
#define HDRENABLED 1
#include "common_ps_fxc.hlsli"

sampler FBSampler	: register( s0 );

struct PS_INPUT
{
	float2 texCoord				: TEXCOORD0;
};


float4 main( PS_INPUT i ) : COLOR
{
	float4 fbSample = tex2D( FBSampler, i.texCoord ) * float4(cLightScale.xyz, 1);

	return FinalOutput( fbSample, 0, PIXEL_FOG_TYPE_NONE, TONEMAP_SCALE_NONE );
}
