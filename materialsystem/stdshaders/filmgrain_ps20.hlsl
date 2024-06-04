#define HDRTYPE HDR_TYPE_NONE
#include "common_ps_fxc.hlsli"

sampler FilmGrainSampler   : register( s0 );

const float4 vNoiseScale : register( c0 );

struct PS_INPUT
{
	float2 grainTexCoord	: TEXCOORD0;
};
 
float4 main( PS_INPUT i ) : COLOR
{
   float4 noise = tex2D( FilmGrainSampler, i.grainTexCoord );

   noise.w = noise.x*(1.0-vNoiseScale.w) + vNoiseScale.w;
   noise.xyz *= vNoiseScale.xyz;

   return float4( noise.x, noise.y, noise.z, noise.w );
}
