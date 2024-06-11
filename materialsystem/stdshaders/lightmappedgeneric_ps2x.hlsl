// STATIC: "MASKEDBLENDING"				"0..1"
// STATIC: "BASETEXTURE2"				"0..1"
// STATIC: "DETAILTEXTURE"				"0..1"
// STATIC: "BUMPMAP"					"0..2"
// STATIC: "BUMPMAP2"					"0..1"
// STATIC: "CUBEMAP"					"0..1"
// STATIC: "ENVMAPMASK"					"0..1"
// STATIC: "BASEALPHAENVMAPMASK"		"0..1"
// STATIC: "SELFILLUM"					"0..1"
// STATIC: "NORMALMAPALPHAENVMAPMASK"	"0..1"
// STATIC: "DIFFUSEBUMPMAP"				"0..1"
// STATIC: "BASETEXTURENOENVMAP"		"0..1"
// STATIC: "BASETEXTURE2NOENVMAP"		"0..1"
// STATIC: "WARPLIGHTING"				"0..1"
// STATIC: "FANCY_BLENDING"				"0..1"
// STATIC: "RELIEF_MAPPING"				"0..0"	[ps20b]
// STATIC: "SEAMLESS"					"0..1"
// STATIC: "OUTLINE"                    "0..1"
// STATIC: "SOFTEDGES"                  "0..1"
// STATIC: "BUMPMASK"                   "0..1"
// STATIC: "NORMAL_DECODE_MODE"			"0..0"	[XBOX]
// STATIC: "NORMAL_DECODE_MODE"			"0..0"	[PC]
// STATIC: "NORMALMASK_DECODE_MODE"		"0..0"	[XBOX]
// STATIC: "NORMALMASK_DECODE_MODE"		"0..0"	[PC]
// STATIC: "DETAIL_BLEND_MODE"			"0..11"
// STATIC: "FLASHLIGHT"					"0..1"	[ps20b] [XBOX]

// DYNAMIC: "FASTPATHENVMAPCONTRAST"	"0..1"
// DYNAMIC: "FASTPATH"					"0..1"
// DYNAMIC: "WRITEWATERFOGTODESTALPHA"	"0..1"
// DYNAMIC: "PIXELFOGTYPE"				"0..1"
// DYNAMIC: "LIGHTING_PREVIEW"			"0..2"	[PC]
// DYNAMIC: "LIGHTING_PREVIEW"			"0..0"	[XBOX]
// DYNAMIC: "WRITE_DEPTH_TO_DESTALPHA"	"0..1"	[ps20b] [PC]
// DYNAMIC: "WRITE_DEPTH_TO_DESTALPHA"	"0..0"	[ps20b] [XBOX]

//  SKIP: $SEAMLESS && $RELIEF_MAPPING			[ps20b]

//	SKIP: (! $DETAILTEXTURE) && ( $DETAIL_BLEND_MODE != 0 )

//  SKIP: $SEAMLESS && ( $OUTLINE || $SOFTEDGES)
//  SKIP: $BASETEXTURE2 && ( $OUTLINE || $SOFTEDGES)
//  SKIP: $BUMPMAP2 && ( $OUTLINE || $SOFTEDGES)
//  SKIP: $SELFILLUM && ( $OUTLINE || $SOFTEDGES)
//  SKIP: $MASKEDBLENDING && ( $OUTLINE || $SOFTEDGES)
//  SKIP: $FANCY_BLENDING && ( $OUTLINE || $SOFTEDGES)
//  SKIP: $LIGHTING_PREVIEW && ( $OUTLINE || $SOFTEDGES)
//  SKIP: ($FASTPATH == 0) && ( $OUTLINE || $SOFTEDGES)
//  SKIP: ($DETAILTEXTURE && $BUMPMAP) && ( $OUTLINE || $SOFTEDGES)
//  SKIP: ($WARPLIGHTING) && ( $OUTLINE || $SOFTEDGES)
//  SKIP: ($BUMPMAP) && ( $OUTLINE || $SOFTEDGES)
//  SKIP: ($DETAIL_BLEND_MODE == 2 ) || ($DETAIL_BLEND_MODE == 3 ) || ($DETAIL_BLEND_MODE == 4 )
//  SKIP: ($DETAIL_BLEND_MODE == 5 ) || ($DETAIL_BLEND_MODE == 6 ) || ($DETAIL_BLEND_MODE == 7 )
//  SKIP: ($DETAIL_BLEND_MODE == 8 ) || ($DETAIL_BLEND_MODE == 9 )
//  SKIP ($DETAIL_BLEND_MODE == 10 ) && ($BUMPMAP == 0 )
//  SKIP ($DETAIL_BLEND_MODE == 11 ) && ($BUMPMAP != 0 )

#include "lightmappedgeneric_ps2_3_x.hlsli"
