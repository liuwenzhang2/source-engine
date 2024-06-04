//==================================================================================================
//
// Physically Based Rendering pixel shader for brushes and models
//
//==================================================================================================

// STATIC: "FLASHLIGHT"                 "0..1"
// STATIC: "FLASHLIGHTDEPTHFILTERMODE"  "0..2"
// STATIC: "LIGHTMAPPED"                "0..1"
// STATIC: "USEENVAMBIENT"              "0..1"
// STATIC: "EMISSIVE"                   "0..1"
// STATIC: "SPECULAR"                   "0..1"
// STATIC:  "PARALLAXOCCLUSION"         "0..1"

// DYNAMIC: "WRITEWATERFOGTODESTALPHA"  "0..1"
// DYNAMIC: "PIXELFOGTYPE"              "0..1"
// DYNAMIC: "NUM_LIGHTS"                "0..4"
// DYNAMIC: "WRITE_DEPTH_TO_DESTALPHA"  "0..1"
// DYNAMIC: "FLASHLIGHTSHADOWS"         "0..1"

// Can't write fog to alpha if there is no fog
// SKIP: ($PIXELFOGTYPE == 0) && ($WRITEWATERFOGTODESTALPHA != 0)
// We don't care about flashlight depth unless the flashlight is on
// SKIP: ( $FLASHLIGHT == 0 ) && ( $FLASHLIGHTSHADOWS == 1 )
// Flashlight shadow filter mode is irrelevant if there is no flashlight
// SKIP: ( $FLASHLIGHT == 0 ) && ( $FLASHLIGHTDEPTHFILTERMODE != 0 )

#include "common_ps_fxc.hlsli"
#include "common_flashlight_fxc.hlsli"
#include "common_lightmappedgeneric_fxc.hlsli"
#include "shader_constant_register_map.hlsli"

#include "pbr_common_ps2_3_x.hlsli"

const float4 g_DiffuseModulation                : register(PSREG_DIFFUSE_MODULATION);
const float4 g_ShadowTweaks                     : register(PSREG_ENVMAP_TINT__SHADOW_TWEAKS);
const float3 cAmbientCube[6]                    : register(PSREG_AMBIENT_CUBE);
const float4 g_EyePos                           : register(PSREG_EYEPOS_SPEC_EXPONENT);
const float4 g_FogParams                        : register(PSREG_FOG_PARAMS);
const float4 g_FlashlightAttenuationFactors     : register(PSREG_FLASHLIGHT_ATTENUATION);
const float4 g_FlashlightPos                    : register(PSREG_FLASHLIGHT_POSITION_RIM_BOOST);
const float4x4 g_FlashlightWorldToTexture       : register(PSREG_FLASHLIGHT_TO_WORLD_TEXTURE);
PixelShaderLightInfo cLightInfo[3]              : register(PSREG_LIGHT_INFO_ARRAY);         // 2 registers each - 6 registers total (4th light spread across w's)
const float4 g_BaseColor                        : register(PSREG_SELFILLUMTINT);

#if PARALLAXOCCLUSION
const float4 g_ParallaxParms                    : register( c27 );
#define PARALLAX_DEPTH                          g_ParallaxParms.r
#define PARALLAX_CENTER                         g_ParallaxParms.g
#endif

sampler BaseTextureSampler          : register(s0);     // Base map, selfillum in alpha
sampler NormalTextureSampler        : register(s1);     // Normal map
sampler EnvmapSampler               : register(s2);     // Cubemap
sampler ShadowDepthSampler          : register(s4);     // Flashlight shadow depth map sampler
sampler RandRotSampler              : register(s5);     // RandomRotation sampler
sampler FlashlightSampler           : register(s6);     // Flashlight cookie 
sampler LightmapSampler             : register(s7);     // Lightmap
sampler MRAOTextureSampler          : register(s10);    // MRAO texture
#if EMISSIVE
sampler EmissionTextureSampler      : register(s11);    // Emission texture
#endif
#if SPECULAR
sampler SpecularTextureSampler      : register(s12);    // Specular F0 texture
#endif

#define ENVMAPLOD (g_EyePos.a)

struct PS_INPUT
{
    float2 baseTexCoord             : TEXCOORD0;
    float4 lightAtten               : TEXCOORD1;
    float3 worldNormal              : TEXCOORD2;
    float3 worldPos                 : TEXCOORD3;
    float3 projPos                  : TEXCOORD4;
    float4 lightmapTexCoord1And2    : TEXCOORD5; 
    float4 lightmapTexCoord3        : TEXCOORD6;
};

// Entry point
float4 main(PS_INPUT i) : COLOR
{
#if USEENVAMBIENT
    float3 EnvAmbientCube[6];
    setupEnvMapAmbientCube(EnvAmbientCube, EnvmapSampler);
#else
    #define EnvAmbientCube cAmbientCube
#endif

    float3 surfNormal = normalize(i.worldNormal);
    float3 surfTangent;
    float3 surfBase; 
    float flipSign;
    float3x3 normalBasis = compute_tangent_frame(surfNormal, i.worldPos, i.baseTexCoord , surfTangent, surfBase, flipSign);

#if PARALLAXOCCLUSION
    float3 outgoingLightRay = g_EyePos.xyz - i.worldPos;
       float3 outgoingLightDirectionTS = worldToRelative( outgoingLightRay, surfTangent, surfBase, surfNormal);
    float2 correctedTexCoord = parallaxCorrect(i.baseTexCoord, outgoingLightDirectionTS , NormalTextureSampler , PARALLAX_DEPTH , PARALLAX_CENTER);
#else
    float2 correctedTexCoord = i.baseTexCoord;
#endif

    float3 textureNormal = normalize((tex2D( NormalTextureSampler,  correctedTexCoord).xyz - float3(0.5, 0.5, 0.5)) * 2);
    float3 normal = normalize(mul(textureNormal, normalBasis)); // World Normal

    float4 albedo = tex2D(BaseTextureSampler, correctedTexCoord);
    albedo.xyz *= g_BaseColor;

    float3 mrao = tex2D(MRAOTextureSampler, correctedTexCoord).xyz;
    float metalness = mrao.x, roughness = mrao.y, ambientOcclusion = mrao.z;
#if EMISSIVE
    float3 emission = tex2D(EmissionTextureSampler, correctedTexCoord).xyz;
#endif
#if SPECULAR
    float3 specular = tex2D(SpecularTextureSampler, correctedTexCoord).xyz;
#endif
    
    textureNormal.y *= flipSign; // Fixup textureNormal for ambient lighting

    float3 outgoingLightDirection = normalize(g_EyePos.xyz - i.worldPos); // Lo
    float lightDirectionAngle = max(0, dot(normal, outgoingLightDirection)); // cosLo

    float3 specularReflectionVector = 2.0 * lightDirectionAngle * normal - outgoingLightDirection; // Lr

#if SPECULAR
    float3 fresnelReflectance = specular.rgb; // F0
#else
    float3 dielectricCoefficient = 0.04; //F0 dielectric
    float3 fresnelReflectance = lerp(dielectricCoefficient, albedo.rgb, metalness); // F0
#endif

    // Start ambient
    float3 ambientLighting = 0.0;
    if (!FLASHLIGHT)
    {
        float3 diffuseIrradiance = ambientLookup(normal, EnvAmbientCube, textureNormal, i.lightmapTexCoord1And2, i.lightmapTexCoord3, LightmapSampler, g_DiffuseModulation);
        // return float4(diffuseIrradiance, 1); // testing diffuse irraciance
        float3 ambientLightingFresnelTerm = fresnelSchlick(fresnelReflectance, lightDirectionAngle); // F
#if SPECULAR
        float3 diffuseContributionFactor = 1 - ambientLightingFresnelTerm; // kd
#else
        float3 diffuseContributionFactor = lerp(1 - ambientLightingFresnelTerm, 0, metalness); ; // kd
#endif
        float3 diffuseIBL = diffuseContributionFactor * albedo.rgb * diffuseIrradiance;

        float4 specularUV = float4(specularReflectionVector, roughness * ENVMAPLOD);
        float3 lookupHigh = ENV_MAP_SCALE * texCUBElod(EnvmapSampler, specularUV).xyz;
        float3 lookupLow = PixelShaderAmbientLight(specularReflectionVector, EnvAmbientCube);
        float3 specularIrradiance = lerp(lookupHigh, lookupLow, roughness * roughness);
        float3 specularIBL = specularIrradiance * EnvBRDFApprox(fresnelReflectance, roughness, lightDirectionAngle);

        ambientLighting = (diffuseIBL + specularIBL) * ambientOcclusion;
    }
    // End ambient

    // Start direct
    float3 directLighting = 0.0;
    if (!FLASHLIGHT) {
        for (uint n = 0; n < NUM_LIGHTS; ++n)
        {
            float3 LightIn = normalize(PixelShaderGetLightVector(i.worldPos, cLightInfo, n));
            float3 LightColor = PixelShaderGetLightColor(cLightInfo, n) * GetAttenForLight(i.lightAtten, n); // Li

            directLighting += calculateLight(LightIn, LightColor, outgoingLightDirection,
                    normal, fresnelReflectance, roughness, metalness, lightDirectionAngle, albedo.rgb);
        }
    }
    // End direct

    // Start flashlight
    if (FLASHLIGHT)
    {
        float4 flashlightSpacePosition = mul(float4(i.worldPos, 1.0), g_FlashlightWorldToTexture);
        clip( flashlightSpacePosition.w ); // stop projected textures from projecting backwards (only really happens if they have a big FOV because they get frustum culled.)
        float3 vProjCoords = flashlightSpacePosition.xyz / flashlightSpacePosition.w;

        float3 delta = g_FlashlightPos.xyz - i.worldPos;
        float distSquared = dot(delta, delta);
        float dist = sqrt(distSquared);

        float3 flashlightColor = tex2D(FlashlightSampler, vProjCoords.xy);
        flashlightColor *= cFlashlightColor.xyz;

#if FLASHLIGHTSHADOWS
        float flashlightShadow = DoFlashlightShadow(ShadowDepthSampler, RandRotSampler, vProjCoords, i.projPos, FLASHLIGHTDEPTHFILTERMODE, g_ShadowTweaks, true);
        float flashlightAttenuated = lerp(flashlightShadow, 1.0, g_ShadowTweaks.y);         // Blend between fully attenuated and not attenuated
        float fAtten = saturate(dot(g_FlashlightAttenuationFactors.xyz, float3(1.0, 1.0 / dist, 1.0 / distSquared)));
        flashlightShadow = saturate(lerp(flashlightAttenuated, flashlightShadow, fAtten));  // Blend between shadow and above, according to light attenuation

        flashlightColor *= flashlightShadow;
#endif
        float farZ = g_FlashlightAttenuationFactors.w;
        float endFalloffFactor = RemapValClamped(dist, farZ, 0.6 * farZ, 0.0, 1.0);

        float3 flashLightIntensity = flashlightColor * endFalloffFactor;
        
        float3 flashLightIn = normalize(g_FlashlightPos.xyz - i.worldPos);

        directLighting += max(0, calculateLight(flashLightIn, flashLightIntensity, outgoingLightDirection,
                normal, fresnelReflectance, roughness, metalness, lightDirectionAngle, albedo.rgb));
    }
    // End flashlight

float fogFactor = 0.0f;
#if !FLASHLIGHT
fogFactor = CalcPixelFogFactor(PIXELFOGTYPE, g_FogParams, g_EyePos.z, i.worldPos.z, i.projPos.z);
#endif

float alpha = 0.0f;
#if !FLASHLIGHT
#if WRITEWATERFOGTODESTALPHA && (PIXELFOGTYPE == PIXEL_FOG_TYPE_HEIGHT)
    alpha = fogFactor;
#else
    alpha = albedo.a;
#endif
#endif

    bool bWriteDepthToAlpha = (WRITE_DEPTH_TO_DESTALPHA != 0) && (WRITEWATERFOGTODESTALPHA == 0);

    float3 combinedLighting = directLighting + ambientLighting;
#if EMISSIVE && !FLASHLIGHT
    combinedLighting += emission;
#endif

    return FinalOutput(float4(combinedLighting, alpha), fogFactor, PIXELFOGTYPE, TONEMAP_SCALE_LINEAR, bWriteDepthToAlpha, i.projPos.z);
}
