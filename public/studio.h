//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

#ifndef STUDIO_H
#define STUDIO_H

#ifdef _WIN32
#pragma once
#endif

#include "basetypes.h"
#include "mathlib/vector2d.h"
#include "mathlib/vector.h"
#include "mathlib/vector4d.h"
#include "mathlib/compressed_vector.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "mathlib/mathlib.h"
#include "utlvector.h"
#include "utlhash.h"
#include "datamap.h"
#include "generichash.h"
#include "localflexcontroller.h"
#include "utlsymbol.h"

#define STUDIO_ENABLE_PERF_COUNTERS

#define STUDIO_SEQUENCE_ACTIVITY_LOOKUPS_ARE_SLOW 0 
// If this is set to 1, then the activity->sequence mapping inside
// the CStudioHdr will not be initialized until the first call to 
// SelectWeightedSequence() or HaveSequenceForActivity(). If set 
// to zero, the mapping will be initialized from CStudioHdr::Init()
// (itself called from the constructor). 
// As of June 4 2007, this was set to 1 because physics, among other
// systems, extemporaneously declares CStudioHdrs inside local function
// scopes without querying their activity/sequence mapping at all.
#define STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE 1

//-----------------------------------------------------------------------------
// forward declarations
//-----------------------------------------------------------------------------

class IMaterial;
class IMesh;
class IMorph;
struct vertexFileHeader_t;
struct thinModelVertices_t;

namespace OptimizedModel
{
	struct StripHeader_t;
}


/*
==============================================================================

STUDIO MODELS

Studio models are position independent, so the cache manager can move them.
==============================================================================
*/

#define STUDIO_VERSION		49

#ifndef _XBOX
#define MAXSTUDIOTRIANGLES	65536	// TODO: tune this
#define MAXSTUDIOVERTS		65536	// TODO: tune this
#define	MAXSTUDIOFLEXVERTS	10000	// max number of verts that can be flexed per mesh.  TODO: tune this
#else
#define MAXSTUDIOTRIANGLES	65536
#define MAXSTUDIOVERTS		32768
#define	MAXSTUDIOFLEXVERTS	5000
#endif
#define MAXSTUDIOSKINS		32		// total textures
#define MAXSTUDIOBONES		128		// total bones actually used
#define MAXSTUDIOFLEXDESC	1024	// maximum number of low level flexes (actual morph targets)
#define MAXSTUDIOFLEXCTRL	96		// maximum number of flexcontrollers (input sliders)
#define MAXSTUDIOPOSEPARAM	24
#define MAXSTUDIOBONECTRLS	5
#define MAXSTUDIOANIMBLOCKS 256

#define MAXSTUDIOBONEBITS	7		// NOTE: MUST MATCH MAXSTUDIOBONES

// NOTE!!! : Changing this number also changes the vtx file format!!!!!
#define MAX_NUM_BONES_PER_VERT 3

//Adrian - Remove this when we completely phase out the old event system.
#define NEW_EVENT_STYLE ( 1 << 10 )

struct mstudiodata_t
{
	int		count;
	int		offset;
};

#define STUDIO_PROC_AXISINTERP	1
#define STUDIO_PROC_QUATINTERP	2
#define STUDIO_PROC_AIMATBONE	3
#define STUDIO_PROC_AIMATATTACH 4
#define STUDIO_PROC_JIGGLE 5

// If you want to embed a pointer into one of the structures that is serialized, use this class! It will ensure that the pointers consume the 
// right amount of space and work correctly across 32 and 64 bit. It also makes sure that there is no surprise about how large the structure
// is when placed in the middle of another structure, and supports Intel's desired behavior on 64-bit that pointers are always 8-byte aligned.
#pragma pack( push, 4 )
template < class T > 
struct ALIGN4 serializedstudioptr_t
{
	T* m_pData;
#ifndef PLATFORM_64BITS
	int32 padding;
#endif

	serializedstudioptr_t() 
	{ 
		m_pData = nullptr; 
		#if _DEBUG && !defined( PLATFORM_64BITS )
			padding = 0; 
		#endif
	}

	inline operator       T*()             { return m_pData; }
	inline operator const T*() const       { return m_pData; }

	inline       T* operator->( )          { return m_pData; }
	inline const T* operator->( ) const    { return m_pData; }

	inline T* operator=( T* ptr )          { return m_pData = ptr; }
	
} ALIGN4_POST;

#pragma pack( pop )


struct mstudioaxisinterpbone_t
{
	DECLARE_BYTESWAP_DATADESC();
	int				control;// local transformation of this bone used to calc 3 point blend
	int				axis;	// axis to check
	Vector			pos[6];	// X+, X-, Y+, Y-, Z+, Z-
	Quaternion		quat[6];// X+, X-, Y+, Y-, Z+, Z-

	mstudioaxisinterpbone_t() = default;
private:
	// No copy constructors allowed
	mstudioaxisinterpbone_t(const mstudioaxisinterpbone_t& vOther);
};


struct mstudioquatinterpinfo_t
{
	DECLARE_BYTESWAP_DATADESC();
	float			inv_tolerance;	// 1 / radian angle of trigger influence
	Quaternion		trigger;	// angle to match
	Vector			pos;		// new position
	Quaternion		quat;		// new angle

	mstudioquatinterpinfo_t() = default;
private:
	// No copy constructors allowed
	mstudioquatinterpinfo_t(const mstudioquatinterpinfo_t& vOther);
};

struct mstudioquatinterpbone_t
{
	DECLARE_BYTESWAP_DATADESC();
	int				control;// local transformation to check
	int				numtriggers;
	int				triggerindex;
	inline mstudioquatinterpinfo_t *pTrigger( int i ) const { return  (mstudioquatinterpinfo_t *)(((byte *)this) + triggerindex) + i; };

	mstudioquatinterpbone_t() = default;
private:
	// No copy constructors allowed
	mstudioquatinterpbone_t(const mstudioquatinterpbone_t& vOther);
};


#define JIGGLE_IS_FLEXIBLE				0x01
#define JIGGLE_IS_RIGID					0x02
#define JIGGLE_HAS_YAW_CONSTRAINT		0x04
#define JIGGLE_HAS_PITCH_CONSTRAINT		0x08
#define JIGGLE_HAS_ANGLE_CONSTRAINT		0x10
#define JIGGLE_HAS_LENGTH_CONSTRAINT	0x20
#define JIGGLE_HAS_BASE_SPRING			0x40
#define JIGGLE_IS_BOING					0x80		// simple squash and stretch sinusoid "boing"

struct mstudiojigglebone_t
{
	DECLARE_BYTESWAP_DATADESC();

	int				flags;

	// general params
	float			length;					// how from from bone base, along bone, is tip
	float			tipMass;

	// flexible params
	float			yawStiffness;
	float			yawDamping;	
	float			pitchStiffness;
	float			pitchDamping;	
	float			alongStiffness;
	float			alongDamping;	

	// angle constraint
	float			angleLimit;				// maximum deflection of tip in radians
	
	// yaw constraint
	float			minYaw;					// in radians
	float			maxYaw;					// in radians
	float			yawFriction;
	float			yawBounce;
	
	// pitch constraint
	float			minPitch;				// in radians
	float			maxPitch;				// in radians
	float			pitchFriction;
	float			pitchBounce;

	// base spring
	float			baseMass;
	float			baseStiffness;
	float			baseDamping;	
	float			baseMinLeft;
	float			baseMaxLeft;
	float			baseLeftFriction;
	float			baseMinUp;
	float			baseMaxUp;
	float			baseUpFriction;
	float			baseMinForward;
	float			baseMaxForward;
	float			baseForwardFriction;

	// boing
	float			boingImpactSpeed;
	float			boingImpactAngle;
	float			boingDampingRate;
	float			boingFrequency;
	float			boingAmplitude;

private:
	// No copy constructors allowed
	//mstudiojigglebone_t(const mstudiojigglebone_t& vOther);
};

struct mstudioaimatbone_t
{
	DECLARE_BYTESWAP_DATADESC();

	int				parent;
	int				aim;		// Might be bone or attach
	Vector			aimvector;
	Vector			upvector;
	Vector			basepos;

	mstudioaimatbone_t() = default;
private:
	// No copy constructors allowed
	mstudioaimatbone_t(const mstudioaimatbone_t& vOther);
};

// bones
struct mstudiobone_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }
	int		 			parent;		// parent bone
	int					bonecontroller[6];	// bone controller index, -1 == none

	// default values
	Vector				pos;
	Quaternion			quat;
	RadianEuler			rot;
	// compression scale
	Vector				posscale;
	Vector				rotscale;

	matrix3x4_t			poseToBone;
	Quaternion			qAlignment;
	int					flags;
	int					proctype;
	int					procindex;		// procedural rule
	mutable int			physicsbone;	// index into physically simulated bone
	inline void *pProcedure( ) const { if (procindex == 0) return NULL; else return  (void *)(((byte *)this) + procindex); };
	int					surfacepropidx;	// index into string tablefor property name
	inline char * const pszSurfaceProp( void ) const { return ((char *)this) + surfacepropidx; }
	int					contents;		// See BSPFlags.h for the contents flags

	int					unused[8];		// remove as appropriate

	mstudiobone_t() = default;
private:
	// No copy constructors allowed
	mstudiobone_t(const mstudiobone_t& vOther);
};

struct mstudiolinearbone_t	
{
	DECLARE_BYTESWAP_DATADESC();

	int numbones;

	int flagsindex;
	inline int flags( int i ) const { Assert( i >= 0 && i < numbones); return *((int *)(((byte *)this) + flagsindex) + i); };
	inline int *pflags( int i ) { Assert( i >= 0 && i < numbones); return ((int *)(((byte *)this) + flagsindex) + i); };

	int	parentindex;
	inline int parent( int i ) const { Assert( i >= 0 && i < numbones); return *((int *)(((byte *)this) + parentindex) + i); };

	int	posindex;
	inline Vector pos( int i ) const { Assert( i >= 0 && i < numbones); return *((Vector *)(((byte *)this) + posindex) + i); };

	int quatindex;
	inline Quaternion quat( int i ) const { Assert( i >= 0 && i < numbones); return *((Quaternion *)(((byte *)this) + quatindex) + i); };

	int rotindex;
	inline RadianEuler rot( int i ) const { Assert( i >= 0 && i < numbones); return *((RadianEuler *)(((byte *)this) + rotindex) + i); };

	int posetoboneindex;
	inline matrix3x4_t poseToBone( int i ) const { Assert( i >= 0 && i < numbones); return *((matrix3x4_t *)(((byte *)this) + posetoboneindex) + i); };

	int	posscaleindex;
	inline Vector posscale( int i ) const { Assert( i >= 0 && i < numbones); return *((Vector *)(((byte *)this) + posscaleindex) + i); };

	int	rotscaleindex;
	inline Vector rotscale( int i ) const { Assert( i >= 0 && i < numbones); return *((Vector *)(((byte *)this) + rotscaleindex) + i); };

	int	qalignmentindex;
	inline Quaternion qalignment( int i ) const { Assert( i >= 0 && i < numbones); return *((Quaternion *)(((byte *)this) + qalignmentindex) + i); };

	int unused[6];

	mstudiolinearbone_t() = default;
private:
	// No copy constructors allowed
	mstudiolinearbone_t(const mstudiolinearbone_t& vOther);
};


//-----------------------------------------------------------------------------
// The component of the bone used by mstudioboneflexdriver_t
//-----------------------------------------------------------------------------
enum StudioBoneFlexComponent_t
{
	STUDIO_BONE_FLEX_INVALID = -1,	// Invalid
	STUDIO_BONE_FLEX_TX = 0,		// Translate X
	STUDIO_BONE_FLEX_TY = 1,		// Translate Y
	STUDIO_BONE_FLEX_TZ = 2			// Translate Z
};


//-----------------------------------------------------------------------------
// Component is one of Translate X, Y or Z [0,2] (StudioBoneFlexComponent_t)
//-----------------------------------------------------------------------------
struct mstudioboneflexdrivercontrol_t
{
	DECLARE_BYTESWAP_DATADESC();

	int m_nBoneComponent;		// Bone component that drives flex, StudioBoneFlexComponent_t
	int m_nFlexControllerIndex;	// Flex controller to drive
	float m_flMin;				// Min value of bone component mapped to 0 on flex controller
	float m_flMax;				// Max value of bone component mapped to 1 on flex controller

	mstudioboneflexdrivercontrol_t() = default;
private:
	// No copy constructors allowed
	mstudioboneflexdrivercontrol_t( const mstudioboneflexdrivercontrol_t &vOther );
};


//-----------------------------------------------------------------------------
// Drive flex controllers from bone components
//-----------------------------------------------------------------------------
struct mstudioboneflexdriver_t
{
	DECLARE_BYTESWAP_DATADESC();

	int m_nBoneIndex;			// Bone to drive flex controller
	int m_nControlCount;		// Number of flex controllers being driven
	int m_nControlIndex;		// Index into data where controllers are (relative to this)

	inline mstudioboneflexdrivercontrol_t *pBoneFlexDriverControl( int i ) const
	{
		Assert( i >= 0 && i < m_nControlCount );
		return (mstudioboneflexdrivercontrol_t *)(((byte *)this) + m_nControlIndex) + i;
	}

	int unused[3];

	mstudioboneflexdriver_t() = default;
private:
	// No copy constructors allowed
	mstudioboneflexdriver_t( const mstudioboneflexdriver_t &vOther );
};


#define BONE_CALCULATE_MASK			0x1F
#define BONE_PHYSICALLY_SIMULATED	0x01	// bone is physically simulated when physics are active
#define BONE_PHYSICS_PROCEDURAL		0x02	// procedural when physics is active
#define BONE_ALWAYS_PROCEDURAL		0x04	// bone is always procedurally animated
#define BONE_SCREEN_ALIGN_SPHERE	0x08	// bone aligns to the screen, not constrained in motion.
#define BONE_SCREEN_ALIGN_CYLINDER	0x10	// bone aligns to the screen, constrained by it's own axis.

#define BONE_USED_MASK				0x0007FF00
#define BONE_USED_BY_ANYTHING		0x0007FF00
#define BONE_USED_BY_HITBOX			0x00000100	// bone (or child) is used by a hit box
#define BONE_USED_BY_ATTACHMENT		0x00000200	// bone (or child) is used by an attachment point
#define BONE_USED_BY_VERTEX_MASK	0x0003FC00
#define BONE_USED_BY_VERTEX_LOD0	0x00000400	// bone (or child) is used by the toplevel model via skinned vertex
#define BONE_USED_BY_VERTEX_LOD1	0x00000800	
#define BONE_USED_BY_VERTEX_LOD2	0x00001000  
#define BONE_USED_BY_VERTEX_LOD3	0x00002000
#define BONE_USED_BY_VERTEX_LOD4	0x00004000
#define BONE_USED_BY_VERTEX_LOD5	0x00008000
#define BONE_USED_BY_VERTEX_LOD6	0x00010000
#define BONE_USED_BY_VERTEX_LOD7	0x00020000
#define BONE_USED_BY_BONE_MERGE		0x00040000	// bone is available for bone merge to occur against it

#define BONE_USED_BY_VERTEX_AT_LOD(lod) ( BONE_USED_BY_VERTEX_LOD0 << (lod) )
#define BONE_USED_BY_ANYTHING_AT_LOD(lod) ( ( BONE_USED_BY_ANYTHING & ~BONE_USED_BY_VERTEX_MASK ) | BONE_USED_BY_VERTEX_AT_LOD(lod) )

#define MAX_NUM_LODS 8

#define BONE_TYPE_MASK				0x00F00000
#define BONE_FIXED_ALIGNMENT		0x00100000	// bone can't spin 360 degrees, all interpolation is normalized around a fixed orientation

#define BONE_HAS_SAVEFRAME_POS		0x00200000	// Vector48
#define BONE_HAS_SAVEFRAME_ROT		0x00400000	// Quaternion64

// bone controllers
struct mstudiobonecontroller_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					bone;	// -1 == 0
	int					type;	// X, Y, Z, XR, YR, ZR, M
	float				start;
	float				end;
	int					rest;	// byte index value at rest
	int					inputfield;	// 0-3 user set controller, 4 mouth
	int					unused[8];
};

// intersection boxes
struct mstudiobbox_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					bone;
	int					group;				// intersection group
	Vector				bbmin;				// bounding box
	Vector				bbmax;	
	int					szhitboxnameindex;	// offset to the name of the hitbox.
	int					unused[8];

	const char* pszHitboxName()
	{
		if( szhitboxnameindex == 0 )
			return "";

		return ((const char*)this) + szhitboxnameindex;
	}

	mstudiobbox_t() = default;

private:
	// No copy constructors allowed
	mstudiobbox_t(const mstudiobbox_t& vOther);
};

// demand loaded sequence groups
struct mstudiomodelgroup_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					szlabelindex;	// textual name
	inline char * const pszLabel( void ) const { return ((char *)this) + szlabelindex; }
	int					sznameindex;	// file name
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }
};

struct mstudiomodelgrouplookup_t
{
	int					modelgroup;
	int					indexwithingroup;
};

// events
struct mstudioevent_t
{
	DECLARE_BYTESWAP_DATADESC();
	float				cycle;
	int					event;
	int					type;
	inline const char * pszOptions( void ) const { return options; }
	char				options[64];

	int					szeventindex;
	inline char * const pszEventName( void ) const { return ((char *)this) + szeventindex; }
};

#define	ATTACHMENT_FLAG_WORLD_ALIGN 0x10000

// attachment
struct mstudioattachment_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }
	unsigned int		flags;
	int					localbone;
	matrix3x4_t			local; // attachment point
	int					unused[8];
};

#define IK_SELF 1
#define IK_WORLD 2
#define IK_GROUND 3
#define IK_RELEASE 4
#define IK_ATTACHMENT 5
#define IK_UNLATCH 6

struct mstudioikerror_t
{
	DECLARE_BYTESWAP_DATADESC();
	Vector		pos;
	Quaternion	q;

	mstudioikerror_t() = default;

private:
	// No copy constructors allowed
	mstudioikerror_t(const mstudioikerror_t& vOther);
};

union mstudioanimvalue_t;

struct mstudiocompressedikerror_t
{
	DECLARE_BYTESWAP_DATADESC();
	float	scale[6];
	short	offset[6];
	inline mstudioanimvalue_t *pAnimvalue( int i ) const { if (offset[i] > 0) return  (mstudioanimvalue_t *)(((byte *)this) + offset[i]); else return NULL; };
	mstudiocompressedikerror_t() = default;

private:
	// No copy constructors allowed
	mstudiocompressedikerror_t(const mstudiocompressedikerror_t& vOther);
};

struct mstudioikrule_t
{
	DECLARE_BYTESWAP_DATADESC();
	int			index;

	int			type;
	int			chain;

	int			bone;

	int			slot;	// iktarget slot.  Usually same as chain.
	float		height;
	float		radius;
	float		floor;
	Vector		pos;
	Quaternion	q;

	int			compressedikerrorindex;
	inline mstudiocompressedikerror_t *pCompressedError() const { return (mstudiocompressedikerror_t *)(((byte *)this) + compressedikerrorindex); };
	int			unused2;

	int			iStart;
	int			ikerrorindex;
	inline mstudioikerror_t *pError( int i ) const { return  (ikerrorindex) ? (mstudioikerror_t *)(((byte *)this) + ikerrorindex) + (i - iStart) : NULL; };

	float		start;	// beginning of influence
	float		peak;	// start of full influence
	float		tail;	// end of full influence
	float		end;	// end of all influence

	float		unused3;	// 
	float		contact;	// frame footstep makes ground concact
	float		drop;		// how far down the foot should drop when reaching for IK
	float		top;		// top of the foot box

	int			unused6;
	int			unused7;
	int			unused8;

	int			szattachmentindex;		// name of world attachment
	inline char * const pszAttachment( void ) const { return ((char *)this) + szattachmentindex; }

	int			unused[7];

	mstudioikrule_t() = default;

private:
	// No copy constructors allowed
	mstudioikrule_t(const mstudioikrule_t& vOther);
};


struct mstudioiklock_t
{
	DECLARE_BYTESWAP_DATADESC();
	int			chain;
	float		flPosWeight;
	float		flLocalQWeight;
	int			flags;

	int			unused[4];
};


struct mstudiolocalhierarchy_t
{
	DECLARE_BYTESWAP_DATADESC();
	int			iBone;			// bone being adjusted
	int			iNewParent;		// the bones new parent

	float		start;			// beginning of influence
	float		peak;			// start of full influence
	float		tail;			// end of full influence
	float		end;			// end of all influence

	int			iStart;			// first frame 

	int			localanimindex;
	inline mstudiocompressedikerror_t *pLocalAnim() const { return (mstudiocompressedikerror_t *)(((byte *)this) + localanimindex); };

	int			unused[4];
};

	

// animation frames
union mstudioanimvalue_t
{
	struct 
	{
		byte	valid;
		byte	total;
	} num;
	short		value;
};

struct mstudioanim_valueptr_t
{
	DECLARE_BYTESWAP_DATADESC();
	short	offset[3];
	inline mstudioanimvalue_t *pAnimvalue( int i ) const { if (offset[i] > 0) return  (mstudioanimvalue_t *)(((byte *)this) + offset[i]); else return NULL; };
};

#define STUDIO_ANIM_RAWPOS	0x01 // Vector48
#define STUDIO_ANIM_RAWROT	0x02 // Quaternion48
#define STUDIO_ANIM_ANIMPOS	0x04 // mstudioanim_valueptr_t
#define STUDIO_ANIM_ANIMROT	0x08 // mstudioanim_valueptr_t
#define STUDIO_ANIM_DELTA	0x10
#define STUDIO_ANIM_RAWROT2	0x20 // Quaternion64


// per bone per animation DOF and weight pointers
struct mstudioanim_t
{
	DECLARE_BYTESWAP_DATADESC();
	byte				bone;
	byte				flags;		// weighing options


	// valid for animating data only
	inline byte				*pData( void ) const { return (((byte *)this) + sizeof( struct mstudioanim_t )); };
	inline mstudioanim_valueptr_t	*pRotV( void ) const { return (mstudioanim_valueptr_t *)(pData()); };
	inline mstudioanim_valueptr_t	*pPosV( void ) const { return (mstudioanim_valueptr_t *)(pData()) + ((flags & STUDIO_ANIM_ANIMROT) != 0); };

	// valid if animation unvaring over timeline
	inline Quaternion48		*pQuat48( void ) const { return (Quaternion48 *)(pData()); };
	inline Quaternion64		*pQuat64( void ) const { return (Quaternion64 *)(pData()); };
	inline Vector48			*pPos( void ) const { return (Vector48 *)(pData() + ((flags & STUDIO_ANIM_RAWROT) != 0) * sizeof( *pQuat48() ) + ((flags & STUDIO_ANIM_RAWROT2) != 0) * sizeof( *pQuat64() ) ); };

	short				nextoffset;

	inline mstudioanim_t	*pNext( void ) const { if (nextoffset != 0) return  (mstudioanim_t *)(((byte *)this) + nextoffset); else return NULL; };
} ALIGN16;

struct mstudiomovement_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					endframe;				
	int					motionflags;
	float				v0;			// velocity at start of block
	float				v1;			// velocity at end of block
	float				angle;		// YAW rotation at end of this blocks movement
	Vector				vector;		// movement vector relative to this blocks initial angle
	Vector				position;	// relative to start of animation???

	mstudiomovement_t() = default;

private:
	// No copy constructors allowed
	mstudiomovement_t(const mstudiomovement_t& vOther);
};

struct studiohdr_t;

// used for piecewise loading of animation data
struct mstudioanimblock_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					datastart;
	int					dataend;
};

struct mstudioanimsections_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					animblock;
	int					animindex;
};

class IStudioHdr;
struct mstudioanimdesc_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					baseptr;
	inline studiohdr_t	*pStudiohdr( void ) const { return (studiohdr_t *)(((byte *)this) + baseptr); }

	int					sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }

	float				fps;		// frames per second	
	int					flags;		// looping/non-looping flags

	int					numframes;

	// piecewise movement
	int					nummovements;
	int					movementindex;
	inline mstudiomovement_t * const pMovement( int i ) const { return (mstudiomovement_t *)(((byte *)this) + movementindex) + i; };

	int					unused1[6];			// remove as appropriate (and zero if loading older versions)	

	int					animblock;
	int					animindex;	 // non-zero when anim data isn't in sections
	mstudioanim_t *pAnimBlock(const IStudioHdr* pStudioHdr, int block, int index ) const; // returns pointer to a specific anim block (local or external)
	mstudioanim_t *pAnim(const IStudioHdr* pStudioHdr, int *piFrame, float &flStall ) const; // returns pointer to data and new frame index
	mstudioanim_t *pAnim(const IStudioHdr* pStudioHdr, int *piFrame ) const; // returns pointer to data and new frame index

	int					numikrules;
	int					ikruleindex;	// non-zero when IK data is stored in the mdl
	int					animblockikruleindex; // non-zero when IK data is stored in animblock file
	mstudioikrule_t *pIKRule(const IStudioHdr* pStudioHdr, int i ) const;

	int					numlocalhierarchy;
	int					localhierarchyindex;
	mstudiolocalhierarchy_t *pHierarchy(const IStudioHdr* pStudioHdr, int i ) const;

	int					sectionindex;
	int					sectionframes; // number of frames used in each fast lookup section, zero if not used
	inline mstudioanimsections_t * const pSection( int i ) const { return (mstudioanimsections_t *)(((byte *)this) + sectionindex) + i; }

	short				zeroframespan;	// frames per span
	short				zeroframecount; // number of spans
	int					zeroframeindex;
	byte				*pZeroFrameData( ) const { if (zeroframeindex) return (((byte *)this) + zeroframeindex); else return NULL; };
	mutable float		zeroframestalltime;		// saved during read stalls

	mstudioanimdesc_t() = default;
private:
	// No copy constructors allowed
	mstudioanimdesc_t(const mstudioanimdesc_t& vOther);
};

struct mstudioikrule_t;

struct mstudioautolayer_t
{
	DECLARE_BYTESWAP_DATADESC();
//private:
	short				iSequence;
	short				iPose;
//public:
	int					flags;
	float				start;	// beginning of influence
	float				peak;	// start of full influence
	float				tail;	// end of full influence
	float				end;	// end of all influence
};

struct mstudioactivitymodifier_t
{
	DECLARE_BYTESWAP_DATADESC();
	
	int					sznameindex;
	inline char			*pszName() { return (sznameindex) ? (char *)(((byte *)this) + sznameindex ) : NULL; }
};

// sequence descriptions
struct mstudioseqdesc_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					baseptr;
	inline studiohdr_t	*pStudiohdr( void ) const { return (studiohdr_t *)(((byte *)this) + baseptr); }

	int					szlabelindex;
	inline char * const pszLabel( void ) const { return ((char *)this) + szlabelindex; }

	int					szactivitynameindex;
	inline char * const pszActivityName( void ) const { return ((char *)this) + szactivitynameindex; }

	int					flags;		// looping/non-looping flags

	int					activity;	// initialized at loadtime to game DLL values
	int					actweight;

	int					numevents;
	int					eventindex;
	inline mstudioevent_t *pEvent( int i ) const { Assert( i >= 0 && i < numevents); return (mstudioevent_t *)(((byte *)this) + eventindex) + i; };
	
	Vector				bbmin;		// per sequence bounding box
	Vector				bbmax;		

	int					numblends;

	// Index into array of shorts which is groupsize[0] x groupsize[1] in length
	int					animindexindex;

	inline int			anim( int x, int y ) const
	{
		if ( x >= groupsize[0] )
		{
			x = groupsize[0] - 1;
		}

		if ( y >= groupsize[1] )
		{
			y = groupsize[ 1 ] - 1;
		}

		int offset = y * groupsize[0] + x;
		short *blends = (short *)(((byte *)this) + animindexindex);
		int value = (int)blends[ offset ];
		return value;
	}

	int					movementindex;	// [blend] float array for blended movement
	int					groupsize[2];
	int					paramindex[2];	// X, Y, Z, XR, YR, ZR
	float				paramstart[2];	// local (0..1) starting value
	float				paramend[2];	// local (0..1) ending value
	int					paramparent;

	float				fadeintime;		// ideal cross fate in time (0.2 default)
	float				fadeouttime;	// ideal cross fade out time (0.2 default)

	int					localentrynode;		// transition node at entry
	int					localexitnode;		// transition node at exit
	int					nodeflags;		// transition rules

	float				entryphase;		// used to match entry gait
	float				exitphase;		// used to match exit gait
	
	float				lastframe;		// frame that should generation EndOfSequence

	int					nextseq;		// auto advancing sequences
	int					pose;			// index of delta animation between end and nextseq

	int					numikrules;

	int					numautolayers;	//
	int					autolayerindex;
	inline mstudioautolayer_t *pAutolayer( int i ) const { Assert( i >= 0 && i < numautolayers); return (mstudioautolayer_t *)(((byte *)this) + autolayerindex) + i; };

	int					weightlistindex;
	inline float		*pBoneweight( int i ) const { return ((float *)(((byte *)this) + weightlistindex) + i); };
	inline float		weight( int i ) const { return *(pBoneweight( i)); };

	// FIXME: make this 2D instead of 2x1D arrays
	int					posekeyindex;
	float				*pPoseKey( int iParam, int iAnim ) const { return (float *)(((byte *)this) + posekeyindex) + iParam * groupsize[0] + iAnim; }
	float				poseKey( int iParam, int iAnim ) const { return *(pPoseKey( iParam, iAnim )); }

	int					numiklocks;
	int					iklockindex;
	inline mstudioiklock_t *pIKLock( int i ) const { Assert( i >= 0 && i < numiklocks); return (mstudioiklock_t *)(((byte *)this) + iklockindex) + i; };

	// Key values
	int					keyvalueindex;
	int					keyvaluesize;
	inline const char * KeyValueText( void ) const { return keyvaluesize != 0 ? ((char *)this) + keyvalueindex : NULL; }

	int					cycleposeindex;		// index of pose parameter to use as cycle index

	int					activitymodifierindex;
	int					numactivitymodifiers;
	inline mstudioactivitymodifier_t *pActivityModifier( int i ) const { Assert( i >= 0 && i < numactivitymodifiers); return activitymodifierindex != 0 ? (mstudioactivitymodifier_t *)(((byte *)this) + activitymodifierindex) + i : NULL; };

	int					unused[5];		// remove/add as appropriate (grow back to 8 ints on version change!)

	mstudioseqdesc_t() = default;
private:
	// No copy constructors allowed
	mstudioseqdesc_t(const mstudioseqdesc_t& vOther);
};


struct mstudioposeparamdesc_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }
	int					flags;	// ????
	float				start;	// starting value
	float				end;	// ending value
	float				loop;	// looping range, 0 for no looping, 360 for rotations, etc.
};

struct mstudioflexdesc_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					szFACSindex;
	inline char * const pszFACS( void ) const { return ((char *)this) + szFACSindex; }
};



struct mstudioflexcontroller_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					sztypeindex;
	inline char * const pszType( void ) const { return ((char *)this) + sztypeindex; }
	int					sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }
	mutable int			localToGlobal;	// remapped at load time to master list
	float				min;
	float				max;
};


enum FlexControllerRemapType_t
{
	FLEXCONTROLLER_REMAP_PASSTHRU = 0,
	FLEXCONTROLLER_REMAP_2WAY,	// Control 0 -> ramps from 1-0 from 0->0.5. Control 1 -> ramps from 0-1 from 0.5->1
	FLEXCONTROLLER_REMAP_NWAY,	// StepSize = 1 / (control count-1) Control n -> ramps from 0-1-0 from (n-1)*StepSize to n*StepSize to (n+1)*StepSize. A second control is needed to specify amount to use 
	FLEXCONTROLLER_REMAP_EYELID
};


class IStudioHdr;
struct mstudioflexcontrollerui_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }

	// These are used like a union to save space
	// Here are the possible configurations for a UI controller
	//
	// SIMPLE NON-STEREO:	0: control	1: unused	2: unused
	// STEREO:				0: left		1: right	2: unused
	// NWAY NON-STEREO:		0: control	1: unused	2: value
	// NWAY STEREO:			0: left		1: right	2: value

	int					szindex0;
	int					szindex1;
	int					szindex2;

	inline const mstudioflexcontroller_t *pController( void ) const
	{
		return !stereo ? (mstudioflexcontroller_t *)( (char *)this + szindex0 ) : NULL;
	}
	inline char * const	pszControllerName( void ) const { return !stereo ? pController()->pszName() : NULL; }
	inline int			controllerIndex( const IStudioHdr* cStudioHdr ) const;

	inline const mstudioflexcontroller_t *pLeftController( void ) const
	{
		return stereo ? (mstudioflexcontroller_t *)( (char *)this + szindex0 ) : NULL;
	}
	inline char * const	pszLeftName( void ) const { return stereo ? pLeftController()->pszName() : NULL; }
	inline int			leftIndex( const IStudioHdr* cStudioHdr ) const;

	inline const mstudioflexcontroller_t *pRightController( void ) const
	{
		return stereo ? (mstudioflexcontroller_t *)( (char *)this + szindex1 ): NULL;
	}
	inline char * const	pszRightName( void ) const { return stereo ? pRightController()->pszName() : NULL; }
	inline int			rightIndex( const IStudioHdr* cStudioHdr ) const;

	inline const mstudioflexcontroller_t *pNWayValueController( void ) const
	{
		return remaptype == FLEXCONTROLLER_REMAP_NWAY ? (mstudioflexcontroller_t *)( (char *)this + szindex2 ) : NULL;
	}
	inline char * const	pszNWayValueName( void ) const { return remaptype == FLEXCONTROLLER_REMAP_NWAY ? pNWayValueController()->pszName() : NULL; }
	inline int			nWayValueIndex( const IStudioHdr* cStudioHdr ) const;

	// Number of controllers this ui description contains, 1, 2 or 3
	inline int			Count() const { return ( stereo ? 2 : 1 ) + ( remaptype == FLEXCONTROLLER_REMAP_NWAY ? 1 : 0 ); }
	inline const mstudioflexcontroller_t *pController( int index ) const;

	unsigned char		remaptype;	// See the FlexControllerRemapType_t enum
	bool				stereo;		// Is this a stereo control?
	byte				unused[2];
};


// this is the memory image of vertex anims (16-bit fixed point)
struct mstudiovertanim_t
{
	DECLARE_BYTESWAP_DATADESC();
	unsigned short		index;
	byte				speed;	// 255/max_length_in_flex
	byte				side;	// 255/left_right

protected:
	// JasonM changing this type a lot, to prefer fixed point 16 bit...
	union
	{
		short			delta[3];
		float16			flDelta[3];
	};

	union
	{
		short			ndelta[3];
		float16			flNDelta[3];
	};

public:
	inline void ConvertToFixed( float flVertAnimFixedPointScale )
	{
		delta[0] = flDelta[0].GetFloat() / flVertAnimFixedPointScale;
		delta[1] = flDelta[1].GetFloat() / flVertAnimFixedPointScale;
		delta[2] = flDelta[2].GetFloat() / flVertAnimFixedPointScale;
		ndelta[0] = flNDelta[0].GetFloat() / flVertAnimFixedPointScale;
		ndelta[1] = flNDelta[1].GetFloat() / flVertAnimFixedPointScale;
		ndelta[2] = flNDelta[2].GetFloat() / flVertAnimFixedPointScale;
	}

	inline Vector GetDeltaFixed( float flVertAnimFixedPointScale )
	{
		return Vector( delta[0] * flVertAnimFixedPointScale, delta[1] * flVertAnimFixedPointScale, delta[2] * flVertAnimFixedPointScale );
	}
	inline Vector GetNDeltaFixed( float flVertAnimFixedPointScale )
	{
		return Vector( ndelta[0] * flVertAnimFixedPointScale, ndelta[1] * flVertAnimFixedPointScale, ndelta[2] * flVertAnimFixedPointScale );
	}
	inline void GetDeltaFixed4DAligned( Vector4DAligned *vFillIn, float flVertAnimFixedPointScale )
	{
		vFillIn->Set( delta[0] * flVertAnimFixedPointScale, delta[1] * flVertAnimFixedPointScale, delta[2] * flVertAnimFixedPointScale, 0.0f );
	}
	inline void GetNDeltaFixed4DAligned( Vector4DAligned *vFillIn, float flVertAnimFixedPointScale )
	{
		vFillIn->Set( ndelta[0] * flVertAnimFixedPointScale, ndelta[1] * flVertAnimFixedPointScale, ndelta[2] * flVertAnimFixedPointScale, 0.0f );
	}
	inline Vector GetDeltaFloat()
	{
		return Vector (flDelta[0].GetFloat(), flDelta[1].GetFloat(), flDelta[2].GetFloat());
	}
	inline Vector GetNDeltaFloat()
	{
		return Vector (flNDelta[0].GetFloat(), flNDelta[1].GetFloat(), flNDelta[2].GetFloat());
	}
	inline void SetDeltaFixed( const Vector& vInput, float flVertAnimFixedPointScale )
	{
		delta[0] = vInput.x / flVertAnimFixedPointScale;
		delta[1] = vInput.y / flVertAnimFixedPointScale;
		delta[2] = vInput.z / flVertAnimFixedPointScale;
	}
	inline void SetNDeltaFixed( const Vector& vInputNormal, float flVertAnimFixedPointScale )
	{
		ndelta[0] = vInputNormal.x / flVertAnimFixedPointScale;
		ndelta[1] = vInputNormal.y / flVertAnimFixedPointScale;
		ndelta[2] = vInputNormal.z / flVertAnimFixedPointScale;
	}

	// Ick...can also force fp16 data into this structure for writing to file in legacy format...
	inline void SetDeltaFloat( const Vector& vInput )
	{
		flDelta[0].SetFloat( vInput.x );
		flDelta[1].SetFloat( vInput.y );
		flDelta[2].SetFloat( vInput.z );
	}
	inline void SetNDeltaFloat( const Vector& vInputNormal )
	{
		flNDelta[0].SetFloat( vInputNormal.x );
		flNDelta[1].SetFloat( vInputNormal.y );
		flNDelta[2].SetFloat( vInputNormal.z );
	}

	class CSortByIndex
	{
	public:
		bool operator()(const mstudiovertanim_t &left, const mstudiovertanim_t & right)const
		{
			return left.index < right.index;
		}
	};
	friend class CSortByIndex;

	mstudiovertanim_t() = default;
//private:
// No copy constructors allowed, but it's needed for std::sort()
//	mstudiovertanim_t(const mstudiovertanim_t& vOther);
};


// this is the memory image of vertex anims (16-bit fixed point)
struct mstudiovertanim_wrinkle_t : public mstudiovertanim_t
{
	DECLARE_BYTESWAP_DATADESC();

	short	wrinkledelta;

	inline void SetWrinkleFixed( float flWrinkle, float flVertAnimFixedPointScale )
	{
		int nWrinkleDeltaInt = flWrinkle / flVertAnimFixedPointScale;
		wrinkledelta = clamp( nWrinkleDeltaInt, -32767, 32767 );
	}

	inline Vector4D GetDeltaFixed( float flVertAnimFixedPointScale )
	{
		return Vector4D( delta[0] * flVertAnimFixedPointScale, delta[1] * flVertAnimFixedPointScale, delta[2] * flVertAnimFixedPointScale, wrinkledelta * flVertAnimFixedPointScale );
	}

	inline void GetDeltaFixed4DAligned( Vector4DAligned *vFillIn, float flVertAnimFixedPointScale )
	{
		vFillIn->Set( delta[0] * flVertAnimFixedPointScale, delta[1] * flVertAnimFixedPointScale, delta[2] * flVertAnimFixedPointScale, wrinkledelta * flVertAnimFixedPointScale );
	}

	inline float GetWrinkleDeltaFixed( float flVertAnimFixedPointScale )
	{
		return wrinkledelta * flVertAnimFixedPointScale;
	}
};


enum StudioVertAnimType_t
{
	STUDIO_VERT_ANIM_NORMAL = 0,
	STUDIO_VERT_ANIM_WRINKLE,
};

struct mstudioflex_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					flexdesc;	// input value

	float				target0;	// zero
	float				target1;	// one
	float				target2;	// one
	float				target3;	// zero

	int					numverts;
	int					vertindex;

	inline	mstudiovertanim_t *pVertanim( int i ) const { Assert( vertanimtype == STUDIO_VERT_ANIM_NORMAL ); return (mstudiovertanim_t *)(((byte *)this) + vertindex) + i; };
	inline	mstudiovertanim_wrinkle_t *pVertanimWrinkle( int i ) const { Assert( vertanimtype == STUDIO_VERT_ANIM_WRINKLE ); return  (mstudiovertanim_wrinkle_t *)(((byte *)this) + vertindex) + i; };

	inline	byte *pBaseVertanim( ) const { return ((byte *)this) + vertindex; };
	inline	int	VertAnimSizeBytes() const { return ( vertanimtype == STUDIO_VERT_ANIM_NORMAL ) ? sizeof(mstudiovertanim_t) : sizeof(mstudiovertanim_wrinkle_t); }

	int					flexpair;	// second flex desc
	unsigned char		vertanimtype;	// See StudioVertAnimType_t
	unsigned char		unusedchar[3];
	int					unused[6];
};


struct mstudioflexop_t
{
	DECLARE_BYTESWAP_DATADESC();
	int		op;
	union 
	{
		int		index;
		float	value;
	} d;
};

struct mstudioflexrule_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					flex;
	int					numops;
	int					opindex;
	inline mstudioflexop_t *iFlexOp( int i ) const { return  (mstudioflexop_t *)(((byte *)this) + opindex) + i; };
};

// 16 bytes
struct mstudioboneweight_t
{
	DECLARE_BYTESWAP_DATADESC();
	float	weight[MAX_NUM_BONES_PER_VERT];
	char	bone[MAX_NUM_BONES_PER_VERT]; 
	byte	numbones;

//	byte	material;
//	short	firstref;
//	short	lastref;
};

// NOTE: This is exactly 48 bytes
struct mstudiovertex_t
{
	DECLARE_BYTESWAP_DATADESC();
	mstudioboneweight_t	m_BoneWeights;
	Vector				m_vecPosition;
	Vector				m_vecNormal;
	Vector2D			m_vecTexCoord;

	mstudiovertex_t() = default;

private:
	// No copy constructors allowed
	mstudiovertex_t(const mstudiovertex_t& vOther);
};

// skin info
struct mstudiotexture_t
{
	DECLARE_BYTESWAP_DATADESC();
	int						sznameindex;
	inline char * const		pszName( void ) const { return ((char *)this) + sznameindex; }
	int						flags;
	int						used;
    int						unused1;
#if PLATFORM_64BITS
    mutable IMaterial		*material;
    mutable void			*clientmaterial;
    int						unused[8];
#else
	mutable IMaterial		*material;  // fixme: this needs to go away . .isn't used by the engine, but is used by studiomdl
	mutable void			*clientmaterial;	// gary, replace with client material pointer if used
	
	int						unused[10];
#endif
};

// eyeball
struct mstudioeyeball_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }
	int		bone;
	Vector	org;
	float	zoffset;
	float	radius;
	Vector	up;
	Vector	forward;
	int		texture;

	int		unused1;
	float	iris_scale;
	int		unused2;

	int		upperflexdesc[3];	// index of raiser, neutral, and lowerer flexdesc that is set by flex controllers
	int		lowerflexdesc[3];
	float	uppertarget[3];		// angle (radians) of raised, neutral, and lowered lid positions
	float	lowertarget[3];

	int		upperlidflexdesc;	// index of flex desc that actual lid flexes look to
	int		lowerlidflexdesc;
	int		unused[4];			// These were used before, so not guaranteed to be 0
	bool	m_bNonFACS;			// Never used before version 44
	char	unused3[3];
	int		unused4[7];

	mstudioeyeball_t() = default;
private:
	// No copy constructors allowed
	mstudioeyeball_t(const mstudioeyeball_t& vOther);
};


// ikinfo
struct mstudioiklink_t
{
	DECLARE_BYTESWAP_DATADESC();
	int		bone;
	Vector	kneeDir;	// ideal bending direction (per link, if applicable)
	Vector	unused0;	// unused

	mstudioiklink_t() = default;
private:
	// No copy constructors allowed
	mstudioiklink_t(const mstudioiklink_t& vOther);
};

struct mstudioikchain_t
{
	DECLARE_BYTESWAP_DATADESC();
	int				sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }
	int				linktype;
	int				numlinks;
	int				linkindex;
	inline mstudioiklink_t *pLink( int i ) const { return (mstudioiklink_t *)(((byte *)this) + linkindex) + i; };
	// FIXME: add unused entries
};


struct mstudioiface_t
{
	unsigned short a, b, c;		// Indices to vertices
};


struct mstudiomodel_t;

struct mstudio_modelvertexdata_t
{
	DECLARE_BYTESWAP_DATADESC();
	Vector				*Position( int i ) const;
	Vector				*Normal( int i ) const;
	Vector4D			*TangentS( int i ) const;
	Vector2D			*Texcoord( int i ) const;
	mstudioboneweight_t	*BoneWeights( int i ) const;
	mstudiovertex_t		*Vertex( int i ) const;
	bool				HasTangentData( void ) const;
	int					GetGlobalVertexIndex( int i ) const;
	int					GetGlobalTangentIndex( int i ) const;

	// base of external vertex data stores
	serializedstudioptr_t<const void> pVertexData;
	serializedstudioptr_t<const void> pTangentData;

    const void	*GetVertexData() const {
        return pVertexData;
    }
    const void	*GetTangentData() const {
        return pTangentData;
    }
};

#ifdef PLATFORM_64BITS
// 64b - match 32-bit packing
#pragma pack( push, 4 )
#endif

struct mstudio_meshvertexdata_t
{
	DECLARE_BYTESWAP_DATADESC();
	Vector				*Position( int i ) const;
	Vector				*Normal( int i ) const;
	Vector4D			*TangentS( int i ) const;
	Vector2D			*Texcoord( int i ) const;
	mstudioboneweight_t *BoneWeights( int i ) const;
	mstudiovertex_t		*Vertex( int i ) const;
	bool				HasTangentData( void ) const;
	int					GetModelVertexIndex( int i ) const;
	int					GetGlobalVertexIndex( int i ) const;

#ifdef PLATFORM_64BITS
    // MoeMod : fix 64bit ptr size
    int         	    index_ptr_modelvertexdata;
#else
	// indirection to this mesh's model's vertex data
	const mstudio_modelvertexdata_t	*modelvertexdata;
#endif
	// used for fixup calcs when culling top level lods
	// expected number of mesh verts at desired lod
	int					numLODVertexes[MAX_NUM_LODS];

    const mstudio_modelvertexdata_t	*pModelVertexData() const {
#ifdef PLATFORM_64BITS
        return *(const mstudio_modelvertexdata_t **)((byte *)this + index_ptr_modelvertexdata);
#else
        return modelvertexdata;
#endif
    }
};

struct mstudiomesh_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					material;

	int					modelindex;
	mstudiomodel_t *pModel() const; 

	int					numvertices;		// number of unique vertices/normals/texcoords
	int					vertexoffset;		// vertex mstudiovertex_t

	// Access thin/fat mesh vertex data (only one will return a non-NULL result)
	const mstudio_meshvertexdata_t	*GetVertexData(		void *pModelData = NULL );
	const thinModelVertices_t		*GetThinVertexData(	void *pModelData = NULL );
	
	int					numflexes;			// vertex animation
	int					flexindex;
	inline mstudioflex_t *pFlex( int i ) const { return (mstudioflex_t *)(((byte *)this) + flexindex) + i; };

	// special codes for material operations
	int					materialtype;
	int					materialparam;

	// a unique ordinal for this mesh
	int					meshid;

	Vector				center;

    mstudio_meshvertexdata_t vertexdata;

#ifdef PLATFORM_64BITS
    int					unused[6]; // remove as appropriate
    const mstudio_modelvertexdata_t	*real_modelvertexdata;
#else
	int					unused[8]; // remove as appropriate
#endif

	mstudiomesh_t() = default;
private:
	// No copy constructors allowed
	mstudiomesh_t(const mstudiomesh_t& vOther);
};

// studio models
struct mstudiomodel_t
{
	DECLARE_BYTESWAP_DATADESC();
	inline const char * pszName( void ) const { return name; }
	char				name[64];

	int					type;

	float				boundingradius;

	int					nummeshes;	
	int					meshindex;
	inline mstudiomesh_t *pMesh( int i ) const { return (mstudiomesh_t *)(((byte *)this) + meshindex) + i; };

	// cache purposes
	int					numvertices;		// number of unique vertices/normals/texcoords
	int					vertexindex;		// vertex Vector
	int					tangentsindex;		// tangents Vector

	// These functions are defined in application-specific code:
	const vertexFileHeader_t			*CacheVertexData(			void *pModelData );

	// Access thin/fat mesh vertex data (only one will return a non-NULL result)
	const mstudio_modelvertexdata_t		*GetVertexData(		void *pModelData = NULL );
	const thinModelVertices_t			*GetThinVertexData(	void *pModelData = NULL );

	int					numattachments;
	int					attachmentindex;

	int					numeyeballs;
	int					eyeballindex;
	inline  mstudioeyeball_t *pEyeball( int i ) { return (mstudioeyeball_t *)(((byte *)this) + eyeballindex) + i; };

    mstudio_modelvertexdata_t vertexdata; // sizeof(mstudio_modelvertexdata_t) == 16

	int					unused[6];		// remove as appropriate
};

#ifdef PLATFORM_64BITS
#pragma pack( pop )
#endif

inline bool mstudio_modelvertexdata_t::HasTangentData( void ) const 
{
	return (GetTangentData() != NULL);
}

inline int mstudio_modelvertexdata_t::GetGlobalVertexIndex( int i ) const
{
	mstudiomodel_t *modelptr = (mstudiomodel_t *)((byte *)this - offsetof(mstudiomodel_t, vertexdata));
    Assert(&modelptr->vertexdata == this);
	Assert( ( modelptr->vertexindex % sizeof( mstudiovertex_t ) ) == 0 );
	return ( i + ( modelptr->vertexindex / sizeof( mstudiovertex_t ) ) );
}

inline int mstudio_modelvertexdata_t::GetGlobalTangentIndex( int i ) const
{
	mstudiomodel_t *modelptr = (mstudiomodel_t *)((byte *)this - offsetof(mstudiomodel_t, vertexdata));
    Assert(&modelptr->vertexdata == this);
	Assert( ( modelptr->tangentsindex % sizeof( Vector4D ) ) == 0 );
	return ( i + ( modelptr->tangentsindex / sizeof( Vector4D ) ) );
}

inline mstudiovertex_t *mstudio_modelvertexdata_t::Vertex( int i ) const 
{
	return (mstudiovertex_t *)GetVertexData() + GetGlobalVertexIndex( i );
}

inline Vector *mstudio_modelvertexdata_t::Position( int i ) const 
{
	return &Vertex(i)->m_vecPosition;
}

inline Vector *mstudio_modelvertexdata_t::Normal( int i ) const 
{ 
	return &Vertex(i)->m_vecNormal;
}

inline Vector4D *mstudio_modelvertexdata_t::TangentS( int i ) const 
{
	// NOTE: The tangents vector is 16-bytes in a separate array
	// because it only exists on the high end, and if I leave it out
	// of the mstudiovertex_t, the vertex is 64-bytes (good for low end)
	return (Vector4D *)GetTangentData() + GetGlobalTangentIndex( i );
}

inline Vector2D *mstudio_modelvertexdata_t::Texcoord( int i ) const 
{ 
	return &Vertex(i)->m_vecTexCoord;
}

inline mstudioboneweight_t *mstudio_modelvertexdata_t::BoneWeights( int i ) const 
{
	return &Vertex(i)->m_BoneWeights;
}

inline mstudiomodel_t *mstudiomesh_t::pModel() const 
{ 
	return (mstudiomodel_t *)(((byte *)this) + modelindex); 
}

inline bool mstudio_meshvertexdata_t::HasTangentData( void ) const
{
	return pModelVertexData()->HasTangentData();
}

inline const mstudio_meshvertexdata_t *mstudiomesh_t::GetVertexData( void *pModelData )
{
	// get this mesh's model's vertex data (allow for mstudiomodel_t::GetVertexData
	// returning NULL if the data has been converted to 'thin' vertices)
	this->pModel()->GetVertexData( pModelData );
#ifdef PLATFORM_64BITS
	real_modelvertexdata = &( this->pModel()->vertexdata );
	vertexdata.index_ptr_modelvertexdata = (byte *)&real_modelvertexdata - (byte *)&vertexdata;
#else
	vertexdata.modelvertexdata = &( this->pModel()->vertexdata );
#endif

	if ( !vertexdata.pModelVertexData()->GetVertexData() )
		return NULL;

	return &vertexdata;
}

inline const thinModelVertices_t * mstudiomesh_t::GetThinVertexData( void *pModelData )
{
	// get this mesh's model's thin vertex data
	return this->pModel()->GetThinVertexData( pModelData );
}

inline int mstudio_meshvertexdata_t::GetModelVertexIndex( int i ) const
{
	mstudiomesh_t *meshptr = (mstudiomesh_t *)((byte *)this - offsetof(mstudiomesh_t,vertexdata));
    Assert(&meshptr->vertexdata == this);
	return meshptr->vertexoffset + i;
}

inline int mstudio_meshvertexdata_t::GetGlobalVertexIndex( int i ) const
{
	return pModelVertexData()->GetGlobalVertexIndex( GetModelVertexIndex( i ) );
}

inline Vector *mstudio_meshvertexdata_t::Position( int i ) const 
{
	return pModelVertexData()->Position( GetModelVertexIndex( i ) );
};

inline Vector *mstudio_meshvertexdata_t::Normal( int i ) const 
{
	return pModelVertexData()->Normal( GetModelVertexIndex( i ) );
};

inline Vector4D *mstudio_meshvertexdata_t::TangentS( int i ) const
{
	return pModelVertexData()->TangentS( GetModelVertexIndex( i ) );
}

inline Vector2D *mstudio_meshvertexdata_t::Texcoord( int i ) const 
{
	return pModelVertexData()->Texcoord( GetModelVertexIndex( i ) );
};

inline mstudioboneweight_t *mstudio_meshvertexdata_t::BoneWeights( int i ) const 
{
	return pModelVertexData()->BoneWeights( GetModelVertexIndex( i ) );
};

inline mstudiovertex_t *mstudio_meshvertexdata_t::Vertex( int i ) const
{
	return pModelVertexData()->Vertex( GetModelVertexIndex( i ) );
}

// a group of studio model data
enum studiomeshgroupflags_t
{
	MESHGROUP_IS_FLEXED			= 0x1,
	MESHGROUP_IS_HWSKINNED		= 0x2,
	MESHGROUP_IS_DELTA_FLEXED	= 0x4
};


// ----------------------------------------------------------
// runtime stuff
// ----------------------------------------------------------

struct studiomeshgroup_t
{
	IMesh			*m_pMesh;
	int				m_NumStrips;
	int				m_Flags;		// see studiomeshgroupflags_t
	OptimizedModel::StripHeader_t	*m_pStripData;
	unsigned short	*m_pGroupIndexToMeshIndex;
	int				m_NumVertices;
	int				*m_pUniqueTris;	// for performance measurements
	unsigned short	*m_pIndices;
	bool			m_MeshNeedsRestore;
	short			m_ColorMeshID;
	IMorph			*m_pMorph;

	inline unsigned short MeshIndex( int i ) const { return m_pGroupIndexToMeshIndex[m_pIndices[i]]; }
};


// studio model data
struct studiomeshdata_t
{
	int					m_NumGroup;
	studiomeshgroup_t*	m_pMeshGroup;
};

struct studioloddata_t
{
	// not needed - this is really the same as studiohwdata_t.m_NumStudioMeshes
	//int					m_NumMeshes; 
	studiomeshdata_t	*m_pMeshData; // there are studiohwdata_t.m_NumStudioMeshes of these.
	float				m_SwitchPoint;
	// one of these for each lod since we can switch to simpler materials on lower lods.
	int					numMaterials; 
	IMaterial			**ppMaterials; /* will have studiohdr_t.numtextures elements allocated */
	// hack - this needs to go away.
	int					*pMaterialFlags; /* will have studiohdr_t.numtextures elements allocated */

	// For decals on hardware morphing, we must actually do hardware skinning
	// For this to work, we have to hope that the total # of bones used by
	// hw flexed verts is < than the max possible for the dx level we're running under
	int					*m_pHWMorphDecalBoneRemap;
	int					m_nDecalBoneCount;
};

struct studiohwdata_t
{
	int					m_RootLOD = 0;	// calced and clamped, nonzero for lod culling
	int					m_NumLODs = 0;
	studioloddata_t		*m_pLODs = 0;
	int					m_NumStudioMeshes = 0;

	inline float LODMetric( float unitSphereSize ) const { return ( unitSphereSize != 0.0f ) ? (100.0f / unitSphereSize) : 0.0f; }
	inline int GetLODForMetric( float lodMetric ) const
	{
		if ( !m_NumLODs )
			return 0;

		// shadow lod is specified on the last lod with a negative switch
		// never consider shadow lod as viable candidate
		int numLODs = (m_pLODs[m_NumLODs-1].m_SwitchPoint < 0.0f) ? m_NumLODs-1 : m_NumLODs;

		for ( int i = m_RootLOD; i < numLODs-1; i++ )
		{
			if ( m_pLODs[i+1].m_SwitchPoint > lodMetric )
				return i;
		}

		return numLODs-1;
	}
};

// ----------------------------------------------------------
// ----------------------------------------------------------

// body part index
struct mstudiobodyparts_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					sznameindex;
	inline char * const pszName( void ) const { return ((char *)this) + sznameindex; }
	int					nummodels;
	int					base;
	int					modelindex; // index into models array
	inline mstudiomodel_t *pModel( int i ) const { return (mstudiomodel_t *)(((byte *)this) + modelindex) + i; };
};


struct mstudiomouth_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					bone;
	Vector				forward;
	int					flexdesc;

	mstudiomouth_t() = default;
private:
	// No copy constructors allowed
	mstudiomouth_t(const mstudiomouth_t& vOther);
};

struct mstudiohitboxset_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					sznameindex;
	inline char * const	pszName( void ) const { return ((char *)this) + sznameindex; }
	int					numhitboxes;
	int					hitboxindex;
	inline mstudiobbox_t *pHitbox( int i ) const { return (mstudiobbox_t *)(((byte *)this) + hitboxindex) + i; };
};


//-----------------------------------------------------------------------------
// Src bone transforms are transformations that will convert .dmx or .smd-based animations into .mdl-based animations
// NOTE: The operation you should apply is: pretransform * bone transform * posttransform
//-----------------------------------------------------------------------------
struct mstudiosrcbonetransform_t
{
	DECLARE_BYTESWAP_DATADESC();

	int			sznameindex;
	inline const char *pszName( void ) const { return ((char *)this) + sznameindex; }
	matrix3x4_t	pretransform;	
	matrix3x4_t	posttransform;	
};


// ----------------------------------------------------------
// Purpose: Load time results on model compositing
// ----------------------------------------------------------

class virtualgroup_t 
{
public:
	virtualgroup_t( void ) { cache = NULL; };
	// tool dependant.  In engine this is a model_t, in tool it's a direct pointer
	void *cache;

	CUtlVector< int > boneMap;				// maps global bone to local bone
	CUtlVector< int > masterBone;			// maps local bone to global bone
	CUtlVector< int > masterSeq;			// maps local sequence to master sequence
	CUtlVector< int > masterAnim;			// maps local animation to master animation
	CUtlVector< int > masterAttachment;	// maps local attachment to global
	CUtlVector< int > masterPose;			// maps local pose parameter to global
	CUtlVector< int > masterNode;			// maps local transition nodes to global

private:
	friend struct studiohdr_t;
};

struct virtualsequence_t
{
#ifdef _XBOX
	short flags;
	short activity;
	short group;
	short index;
#else
	int	flags;
	int activity;
	int group;
	int index;
#endif
};

struct virtualgeneric_t
{
#ifdef _XBOX
	short group;
	short index;
#else
	int group;
	int index;
#endif
};

class IVirtualModel {
public:
	virtual virtualgroup_t* pAnimGroup(int animation) = 0;
	virtual int pAnimIndex(int animation) = 0;
	virtual virtualgroup_t* pSeqGroup(int sequence) = 0;
	virtual int	pSeqIndex(int animation) = 0;
	virtual int NumSeq() = 0;
	virtual int NumPose() = 0;
	virtual int nPoseGroup(int pose) = 0;
	virtual int pPoseIndex(int pose) = 0;
	virtual virtualgroup_t* pPoseGroup(int pose) = 0;
	virtual int NumAttachment() = 0;
	virtual virtualgroup_t* pAttachmentGroup(int attachment) = 0;
	virtual int pAttachmentIndex(int attachment) = 0;
	virtual int NumNode() = 0;
	virtual virtualgroup_t* pNodeGroup(int node) = 0;
	virtual int pNodeIndex(int node) = 0;
	virtual int NumGroup() = 0;
	virtual virtualgroup_t* pGroup(int group) = 0;
	virtual IStudioHdr* GetIStudioHdr(int group) = 0;
	virtual void* GetUserData(int group) = 0;
	virtual int NumIKlock() = 0;
	virtual virtualgroup_t* pIKlockGroup(int iklock) = 0;
	virtual int pIKlockIndex(int iklock) = 0;

};



// 'thin' vertex data, used to do model decals (see Studio_CreateThinVertexes())
struct thinModelVertices_t
{
	void Init( int numBoneInfluences, Vector *positions, unsigned short *normals, float *boneWeights, char *boneIndices )
	{
		Assert( positions != NULL );
		Assert( normals   != NULL );
		Assert( ( numBoneInfluences >= 0 ) && ( numBoneInfluences <= 3 ) );
		Assert( numBoneInfluences > 0 ? !!boneIndices : !boneIndices );
		Assert( numBoneInfluences > 1 ? !!boneWeights : !boneWeights );
		m_numBoneInfluences	= numBoneInfluences;
		m_vecPositions		= positions;
		m_vecNormals		= normals;
		m_boneWeights		= boneWeights;
		m_boneIndices		= boneIndices;
	}

	void SetPosition( int vertIndex, const Vector & position )
	{
		Assert( m_vecPositions );
		m_vecPositions[ vertIndex ] = position;
	}

	void SetNormal( int vertIndex, const Vector & normal )
	{
		Assert( m_vecNormals );
		unsigned int packedNormal;
		PackNormal_UBYTE4( normal.x, normal.y, normal.z, &packedNormal );
		m_vecNormals[ vertIndex ] = (unsigned short)( 0x0000FFFF & packedNormal );
	}

	void SetBoneWeights( int vertIndex, const mstudioboneweight_t & boneWeights )
	{
		Assert( ( m_numBoneInfluences  >= 1 ) && ( m_numBoneInfluences  <= 3 ) );
		Assert( ( boneWeights.numbones >= 1 ) && ( boneWeights.numbones <= m_numBoneInfluences ) );
		int    numStoredWeights = max( 0, ( m_numBoneInfluences - 1 ) );
		float *pBaseWeight	= m_boneWeights + vertIndex*numStoredWeights;
		char  *pBaseIndex	= m_boneIndices + vertIndex*m_numBoneInfluences;
		for ( int i = 0; i < m_numBoneInfluences; i++ )
		{
			pBaseIndex[i] = boneWeights.bone[i];
		}
		for ( int i = 0; i < numStoredWeights; i++ )
		{
			pBaseWeight[i] = boneWeights.weight[i];
		}
	}

	void GetMeshPosition( mstudiomesh_t *pMesh, int meshIndex, Vector *pPosition ) const
	{
		Assert( pMesh );
		GetPosition( pMesh->vertexdata.GetGlobalVertexIndex( meshIndex ), pPosition );
	}

	void GetMeshNormal( mstudiomesh_t *pMesh, int meshIndex, Vector *pNormal ) const
	{
		Assert( pMesh );
		GetNormal( pMesh->vertexdata.GetGlobalVertexIndex( meshIndex ), pNormal );
	}

	void GetMeshBoneWeights( mstudiomesh_t *pMesh, int meshIndex, mstudioboneweight_t *pBoneWeights ) const
	{
		Assert( pMesh );
		GetBoneWeights( pMesh->vertexdata.GetGlobalVertexIndex( meshIndex ), pBoneWeights );
	}

	void GetModelPosition( mstudiomodel_t *pModel, int modelIndex, Vector *pPosition ) const
	{
		Assert( pModel );
		GetPosition( pModel->vertexdata.GetGlobalVertexIndex( modelIndex ), pPosition );
	}

	void GetModelNormal( mstudiomodel_t *pModel, int modelIndex, Vector *pNormal ) const
	{
		Assert( pModel );
		GetNormal( pModel->vertexdata.GetGlobalVertexIndex( modelIndex ), pNormal );
	}

	void GetModelBoneWeights( mstudiomodel_t *pModel, int modelIndex, mstudioboneweight_t *pBoneWeights ) const
	{
		Assert( pModel );
		GetBoneWeights( pModel->vertexdata.GetGlobalVertexIndex( modelIndex ), pBoneWeights );
	}

private:
	void GetPosition( int vertIndex, Vector *pPosition ) const
	{
		Assert( pPosition );
		Assert( m_vecPositions );
		*pPosition = m_vecPositions[ vertIndex ];
	}

	void GetNormal( int vertIndex, Vector *pNormal ) const
	{
		Assert( pNormal );
		Assert( m_vecNormals );
		unsigned int packedNormal = 0x0000FFFF & m_vecNormals[ vertIndex ];
		UnpackNormal_UBYTE4( &packedNormal, pNormal->Base() );
	}

	void GetBoneWeights( int vertIndex, mstudioboneweight_t *pBoneWeights ) const
	{
		Assert( pBoneWeights );
		Assert( ( m_numBoneInfluences <= 1 ) || ( m_boneWeights != NULL ) );
		Assert( ( m_numBoneInfluences <= 0 ) || ( m_boneIndices != NULL ) );
		int    numStoredWeights = max( 0, ( m_numBoneInfluences - 1 ) );
		float *pBaseWeight	= m_boneWeights + vertIndex*numStoredWeights;
		char  *pBaseIndex	= m_boneIndices + vertIndex*m_numBoneInfluences;
		float  sum			= 0.0f;
		for (int i = 0;i < MAX_NUM_BONES_PER_VERT;i++)
		{
			if ( i < ( m_numBoneInfluences - 1 ) )
				pBoneWeights->weight[i] = pBaseWeight[i];
			else
				pBoneWeights->weight[i] = 1.0f - sum;
			sum += pBoneWeights->weight[i];

			pBoneWeights->bone[i] = ( i < m_numBoneInfluences ) ? pBaseIndex[i] : 0;
		}

		// Treat 'zero weights' as '100% binding to bone zero':
		pBoneWeights->numbones = m_numBoneInfluences ? m_numBoneInfluences : 1;
	}

	int				m_numBoneInfluences;// Number of bone influences per vertex, N
	float			*m_boneWeights;		// This array stores (N-1) weights per vertex (unless N is zero)
	char			*m_boneIndices;		// This array stores N indices per vertex
	Vector			*m_vecPositions;
	unsigned short	*m_vecNormals;		// Normals are compressed into 16 bits apiece (see PackNormal_UBYTE4() )
};

// ----------------------------------------------------------
// Studio Model Vertex Data File
// Position independent flat data for cache manager
// ----------------------------------------------------------

// little-endian "IDSV"
#define MODEL_VERTEX_FILE_ID		(('V'<<24)+('S'<<16)+('D'<<8)+'I')
#define MODEL_VERTEX_FILE_VERSION	4
// this id (IDCV) is used once the vertex data has been compressed (see CMDLCache::CreateThinVertexes)
#define MODEL_VERTEX_FILE_THIN_ID	(('V'<<24)+('C'<<16)+('D'<<8)+'I')

struct vertexFileHeader_t
{
	DECLARE_BYTESWAP_DATADESC();
	int		id;								// MODEL_VERTEX_FILE_ID
	int		version;						// MODEL_VERTEX_FILE_VERSION
	int		checksum;						// same as studiohdr_t, ensures sync
	int		numLODs;						// num of valid lods
	int		numLODVertexes[MAX_NUM_LODS];	// num verts for desired root lod
	int		numFixups;						// num of vertexFileFixup_t
	int		fixupTableStart;				// offset from base to fixup table
	int		vertexDataStart;				// offset from base to vertex block
	int		tangentDataStart;				// offset from base to tangent block

public:

	// Accessor to fat vertex data
	const mstudiovertex_t *GetVertexData() const
	{
		if ( ( id == MODEL_VERTEX_FILE_ID ) && ( vertexDataStart != 0 ) )
			return ( mstudiovertex_t * ) ( vertexDataStart + (byte *)this );
		else
			return NULL;
	}
	// Accessor to (fat) tangent vertex data (tangents aren't stored in compressed data)
	const Vector4D *GetTangentData() const
	{
		if ( ( id == MODEL_VERTEX_FILE_ID ) && ( tangentDataStart != 0 ) )
			return ( Vector4D * ) ( tangentDataStart + (byte *)this );
		else
			return NULL;
	}
	// Accessor to thin vertex data
	const  thinModelVertices_t *GetThinVertexData() const
	{
		if ( ( id == MODEL_VERTEX_FILE_THIN_ID ) && ( vertexDataStart != 0 ) )
			return ( thinModelVertices_t * ) ( vertexDataStart + (byte *)this );
		else
			return NULL;
	}
};

// model vertex data accessor (defined here so vertexFileHeader_t can be used)
inline const mstudio_modelvertexdata_t * mstudiomodel_t::GetVertexData( void *pModelData )
{
	const vertexFileHeader_t * pVertexHdr = CacheVertexData( pModelData );
	if ( !pVertexHdr )
	{
		vertexdata.pVertexData = NULL;
		vertexdata.pTangentData = NULL;
		return NULL;
	}

	vertexdata.pVertexData  = pVertexHdr->GetVertexData();
	vertexdata.pTangentData = pVertexHdr->GetTangentData();

	if ( !vertexdata.GetVertexData() )
		return NULL;

	return &vertexdata;
}

// model thin vertex data accessor (defined here so vertexFileHeader_t can be used)
inline const thinModelVertices_t * mstudiomodel_t::GetThinVertexData( void *pModelData )
{
	const vertexFileHeader_t * pVertexHdr = CacheVertexData( pModelData );
	if ( !pVertexHdr )
		return NULL;

	return pVertexHdr->GetThinVertexData();
}

// apply sequentially to lod sorted vertex and tangent pools to re-establish mesh order
struct vertexFileFixup_t
{
	DECLARE_BYTESWAP_DATADESC();
	int		lod;				// used to skip culled root lod
	int		sourceVertexID;		// absolute index from start of vertex/tangent blocks
	int		numVertexes;
};

// This flag is set if no hitbox information was specified
#define STUDIOHDR_FLAGS_AUTOGENERATED_HITBOX				0x00000001

// NOTE:  This flag is set at loadtime, not mdl build time so that we don't have to rebuild
// models when we change materials.
#define STUDIOHDR_FLAGS_USES_ENV_CUBEMAP					0x00000002

// Use this when there are translucent parts to the model but we're not going to sort it 
#define STUDIOHDR_FLAGS_FORCE_OPAQUE						0x00000004

// Use this when we want to render the opaque parts during the opaque pass
// and the translucent parts during the translucent pass
#define STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS					0x00000008

// This is set any time the .qc files has $staticprop in it
// Means there's no bones and no transforms
#define STUDIOHDR_FLAGS_STATIC_PROP							0x00000010

// NOTE:  This flag is set at loadtime, not mdl build time so that we don't have to rebuild
// models when we change materials.
#define STUDIOHDR_FLAGS_USES_FB_TEXTURE						0x00000020

// This flag is set by studiomdl.exe if a separate "$shadowlod" entry was present
//  for the .mdl (the shadow lod is the last entry in the lod list if present)
#define STUDIOHDR_FLAGS_HASSHADOWLOD						0x00000040

// NOTE:  This flag is set at loadtime, not mdl build time so that we don't have to rebuild
// models when we change materials.
#define STUDIOHDR_FLAGS_USES_BUMPMAPPING					0x00000080

// NOTE:  This flag is set when we should use the actual materials on the shadow LOD
// instead of overriding them with the default one (necessary for translucent shadows)
#define STUDIOHDR_FLAGS_USE_SHADOWLOD_MATERIALS				0x00000100

// NOTE:  This flag is set when we should use the actual materials on the shadow LOD
// instead of overriding them with the default one (necessary for translucent shadows)
#define STUDIOHDR_FLAGS_OBSOLETE							0x00000200

#define STUDIOHDR_FLAGS_UNUSED								0x00000400

// NOTE:  This flag is set at mdl build time
#define STUDIOHDR_FLAGS_NO_FORCED_FADE						0x00000800

// NOTE:  The npc will lengthen the viseme check to always include two phonemes
#define STUDIOHDR_FLAGS_FORCE_PHONEME_CROSSFADE				0x00001000

// This flag is set when the .qc has $constantdirectionallight in it
// If set, we use constantdirectionallightdot to calculate light intensity
// rather than the normal directional dot product
// only valid if STUDIOHDR_FLAGS_STATIC_PROP is also set
#define STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT		0x00002000

// Flag to mark delta flexes as already converted from disk format to memory format
#define STUDIOHDR_FLAGS_FLEXES_CONVERTED					0x00004000

// Indicates the studiomdl was built in preview mode
#define STUDIOHDR_FLAGS_BUILT_IN_PREVIEW_MODE				0x00008000

// Ambient boost (runtime flag)
#define STUDIOHDR_FLAGS_AMBIENT_BOOST						0x00010000

// Don't cast shadows from this model (useful on first-person models)
#define STUDIOHDR_FLAGS_DO_NOT_CAST_SHADOWS					0x00020000

// alpha textures should cast shadows in vrad on this model (ONLY prop_static!)
#define STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS				0x00040000


// flagged on load to indicate no animation events on this model
#define STUDIOHDR_FLAGS_VERT_ANIM_FIXED_POINT_SCALE			0x00200000


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

class IDataCache;
class IMDLCache;

typedef int (*RandomWeightFunc)(int iMinVal, int iMaxVal);

class IStudioHdr {
protected:
	virtual ~IStudioHdr(){}
	//virtual void Init(studiohdr_t* pStudioHdr, IMDLCache* mdlcache = NULL) = 0;
	//virtual void Term() = 0;
public:
	virtual bool IsVirtual(void) = 0;
	virtual bool IsValid(void) = 0;
	virtual bool IsReadyForAccess(void) const = 0;
	virtual IVirtualModel* GetVirtualModel(void) const = 0;
	virtual const studiohdr_t* GetRenderHdr(void) const = 0;
	virtual const IStudioHdr* pSeqStudioHdr(int sequence) const = 0;
	virtual const IStudioHdr* pAnimStudioHdr(int animation) const = 0;
	virtual const IStudioHdr* RealStudioHdr(studiohdr_t* pStudioHdr) const = 0;
	virtual void FreeRealStudioHdr(const IStudioHdr* pStudioHdr) const = 0;
	virtual int			numbones(void) const = 0;
	virtual mstudiobone_t* pBone(int i) const = 0;
	virtual int					RemapAnimBone(int iAnim, int iLocalBone) const = 0;		// maps local animations bone to global bone
	virtual int					RemapSeqBone(int iSequence, int iLocalBone) const = 0;	// maps local sequence bone to global bone

	virtual bool				SequencesAvailable() const = 0;
	virtual int					GetNumSeq(void) const = 0;
	virtual mstudioanimdesc_t& pAnimdesc(int i) = 0;
	virtual mstudioseqdesc_t& pSeqdesc(int iSequence) const = 0;
	virtual int					iRelativeAnim(int baseseq, int relanim) const = 0;	// maps seq local anim reference to global anim index
	virtual int					iRelativeSeq(int baseseq, int relseq) const = 0;		// maps seq local seq reference to global seq index

	//virtual int					GetSequenceActivity(int iSequence) = 0;
	//virtual void				SetSequenceActivity(int iSequence, int iActivity) = 0;
	virtual int					GetActivityListVersion(void) = 0;
	virtual void				SetActivityListVersion(int version) const = 0;
	virtual int					GetEventListVersion(void) = 0;
	virtual void				SetEventListVersion(int version) = 0;

	virtual int					GetNumAttachments(void) const = 0;
	virtual const mstudioattachment_t& pAttachment(int i) = 0;
	virtual int					GetAttachmentBone(int i) = 0;
	// used on my tools in hlmv, not persistant
	virtual void				SetAttachmentBone(int iAttachment, int iBone) = 0;

	virtual int					EntryNode(int iSequence) = 0;
	virtual int					ExitNode(int iSequence) = 0;
	virtual char*				pszNodeName(int iNode) = 0;
	// FIXME: where should this one be?
	virtual int					GetTransition(int iFrom, int iTo) const = 0;

	virtual int					GetNumPoseParameters(void) const = 0;
	virtual const mstudioposeparamdesc_t& pPoseParameter(int i) = 0;
	virtual int					GetSharedPoseParameter(int iSequence, int iLocalPose) const = 0;

	virtual int					GetNumIKAutoplayLocks(void) const = 0;
	virtual const mstudioiklock_t& pIKAutoplayLock(int i) = 0;

	virtual int			CountAutoplaySequences() const = 0;
	virtual int			CopyAutoplaySequences(unsigned short* pOut, int outCount) const = 0;
	virtual int			GetAutoplayList(unsigned short** pOut) const = 0;

	virtual int			GetNumBoneControllers(void) const = 0;
	virtual mstudiobonecontroller_t* pBonecontroller(int i) const = 0;

	virtual int			numikchains() const = 0;
	virtual int			GetNumIKChains(void) const = 0;
	virtual mstudioikchain_t* pIKChain(int i) const = 0;

	virtual int			numflexrules() const = 0;
	virtual mstudioflexrule_t* pFlexRule(int i) const = 0;

	virtual int			numflexdesc() const = 0;
	virtual mstudioflexdesc_t* pFlexdesc(int i) const = 0;

	virtual LocalFlexController_t			numflexcontrollers() const = 0;
	virtual mstudioflexcontroller_t* pFlexcontroller(LocalFlexController_t i) const = 0;

	virtual int			numflexcontrollerui() const = 0;
	virtual mstudioflexcontrollerui_t* pFlexcontrollerUI(int i) const = 0;

	//inline const char	*name() const { return m_pStudioHdr->name; }; // deprecated -- remove after full xbox merge
	virtual const char* pszName() const = 0;

	virtual int			numbonecontrollers() const = 0;

	virtual int			numhitboxsets() const = 0;
	virtual mstudiohitboxset_t* pHitboxSet(int i) const = 0;

	virtual mstudiobbox_t* pHitbox(int i, int set) const = 0;
	virtual int			iHitboxCount(int set) const = 0;

	virtual int			numbodyparts() const = 0;
	virtual mstudiobodyparts_t* pBodypart(int i) const = 0;

	virtual int			numskinfamilies() const = 0;

	virtual Vector		eyeposition() const = 0;

	virtual int&		flags() = 0;

	virtual char* const pszSurfaceProp(void) const = 0;

	virtual float		mass() const = 0;
	virtual int			contents() const = 0;

	virtual const byte* GetBoneTableSortedByName() const = 0;

	virtual Vector		illumposition() const = 0;

	virtual Vector		hull_min() const = 0;		// ideal movement hull size
	virtual Vector		hull_max() const = 0;

	virtual Vector		view_bbmin() const = 0;		// clipping bounding box
	virtual Vector		view_bbmax() const = 0;

	virtual int			numtextures() const = 0;

	virtual int			IllumPositionAttachmentIndex() const = 0;

	virtual float		MaxEyeDeflection() const = 0;

	virtual mstudiolinearbone_t* pLinearBones() const = 0;

	virtual int			BoneFlexDriverCount() const = 0;
	virtual const mstudioboneflexdriver_t* BoneFlexDriver(int i) const = 0;

	virtual float		VertAnimFixedPointScale() const = 0;

public:
	//virtual int IsSequenceLooping(int iSequence) = 0;
	//virtual float GetSequenceCycleRate(int iSequence) = 0;

	virtual void				RunFlexRules(const float* src, float* dest) = 0;


public:
	virtual int boneFlags(int iBone) const = 0;
	virtual int boneParent(int iBone) const = 0;

	virtual int GetPerfAnimationLayers() const = 0;
	virtual int GetPerfAnimatedBones() const = 0;
	virtual int GetPerfUsedBones() const = 0;
	virtual void IncPerfAnimationLayers(void) const = 0;
	virtual void IncPerfAnimatedBones(void) const = 0;
	virtual void IncPerfUsedBones(void) const = 0;
	virtual void ClearPerfCounters(void) = 0;

	virtual bool HaveSequenceForActivity(int activity) = 0;
	virtual int SelectWeightedSequence(int activity, int curSequence, RandomWeightFunc pRandomWeightFunc) = 0;

	virtual int nummouths() = 0;
	virtual int	numskinref() const = 0;
	virtual short* pSkinref(int i) const = 0;
	virtual byte constdirectionallightdot() = 0;
	virtual vertexFileHeader_t* CacheVertexData() = 0;
	virtual mstudiomouth_t* pMouth(int i) const = 0;
	virtual int checksum() = 0;
	virtual byte rootLOD() = 0;
	virtual int textureindex() = 0;
	virtual int numcdtextures() = 0;
	virtual char* pCdtexture(int i) const = 0;
	virtual mstudiotexture_t* pTexture(int i) const = 0;
	virtual unsigned char* GetAnimBlock(int nBlock) const = 0;
	virtual int numincludemodels() const = 0;
	virtual const char* KeyValueText(void) const = 0;
	//virtual int length() = 0;
	//virtual mstudioanimdesc_t* pLocalAnimdesc(int i) const = 0;
	//virtual mstudioseqdesc_t* pLocalSeqdesc(int i) const = 0;
	//virtual mstudioposeparamdesc_t* pLocalPoseParameter(int i) const = 0;
	//virtual mstudioattachment_t* pLocalAttachment(int i) const = 0;
	//virtual char* pszLocalNodeName(int iNode) const = 0;
	//virtual int activitylistversion() const = 0;
	//virtual mstudioiklock_t* pLocalIKAutoplayLock(int i) const = 0;

};



/*
class CModelAccess
{
public:
	CModelAccess(CStudioHdr *pSemaphore)
	 : m_pStudioHdr(pSemaphore)
	{
		m_pStudioHdr->IncrementAccess();
	}
	
	~CModelAccess()
	{
		m_pStudioHdr->DecrementAccess();
	}
	
private:
	CStudioHdr *m_pStudioHdr;
};

#define ENABLE_MODEL_ACCESS( a ) \
	CModelAccess ModelAccess##__LINE__( a->m_pStudioHdr )
*/

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

struct flexweight_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					key;
	float				weight;
	float				influence;
};

struct flexsetting_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					nameindex;

	inline char *pszName( void ) const
	{ 
		return (char *)(((byte *)this) + nameindex); 
	}

	// Leaving this for legacy support
	int					obsolete1;

	// Number of flex settings
	int					numsettings;
	int					index;
	
	// OBSOLETE:  
	int					obsolete2;

	// Index of start of contiguous array of flexweight_t structures
	int					settingindex;

	//-----------------------------------------------------------------------------
	// Purpose: Retrieves a pointer to the flexweight_t, including resolving
	//  any markov chain hierarchy.  Because of this possibility, we return
	//  the number of settings in the weights array returned.  We'll generally
	//  call this function with i == 0
	// Input  : *base - 
	//			i - 
	//			**weights - 
	// Output : int
	//-----------------------------------------------------------------------------
	inline int psetting( byte *base, int i, flexweight_t **weights ) const;
};


struct flexsettinghdr_t
{
	DECLARE_BYTESWAP_DATADESC();
	int					id;
	int					version;

	inline const char * pszName( void ) const { return name; }
	char				name[64];
	int					length;

	int					numflexsettings;
	int					flexsettingindex;
	inline flexsetting_t *pSetting( int i ) const { return (flexsetting_t *)(((byte *)this) + flexsettingindex) + i; };
	int					nameindex;

	// look up flex settings by "index"
	int					numindexes;
	int					indexindex;

	inline flexsetting_t *pIndexedSetting( int index ) const 
	{ 
		if ( index < 0 || index >= numindexes )
		{
			return NULL;
		}

		int i = *((int *)(((byte *)this) + indexindex) + index);
		
		if (i == -1) 
		{
			return NULL;
		}

		return pSetting( i );
	}

	// index names of "flexcontrollers"
	int					numkeys;
	int					keynameindex;
	inline char			*pLocalName( int i ) const { return (char *)(((byte *)this) + *((int *)(((byte *)this) + keynameindex) + i)); };

	int					keymappingindex;
	inline int			*pLocalToGlobal( int i ) const { return (int *)(((byte *)this) + keymappingindex) + i; };
	inline int			LocalToGlobal( int i ) const { return *pLocalToGlobal( i ); };
};

//-----------------------------------------------------------------------------
// Purpose: Retrieves a pointer to the flexweight_t.  
// Input  : *base - flexsettinghdr_t * pointer
//			i - index of flex setting to retrieve
//			**weights - destination for weights array starting at index i.
// Output : int
//-----------------------------------------------------------------------------
inline int flexsetting_t::psetting( byte *base, int i, flexweight_t **weights ) const
{ 
	// Grab array pointer
	*weights = (flexweight_t *)(((byte *)this) + settingindex) + i;
	// Return true number of settings
	return numsettings;
};


//-----------------------------------------------------------------------------
// For a given flex controller ui struct, these return the index of the
// studiohdr_t flex controller that correspond to the the left and right
// flex controllers if the ui controller is a stereo control.
// nWayValueIndex returns the index of the flex controller that is the value
// flex controller for an NWAY combination
// If these functions are called and the ui controller isn't of the type
// specified then -1 is returned
//-----------------------------------------------------------------------------
inline int mstudioflexcontrollerui_t::controllerIndex( const IStudioHdr* cStudioHdr ) const
{
	return !stereo ? pController() - cStudioHdr->pFlexcontroller( (LocalFlexController_t)0 ) : -1;
}


inline int mstudioflexcontrollerui_t::rightIndex( const IStudioHdr* cStudioHdr ) const
{
	return stereo ? pRightController() - cStudioHdr->pFlexcontroller( (LocalFlexController_t)0 ) : -1;
}


inline int mstudioflexcontrollerui_t::leftIndex( const IStudioHdr* cStudioHdr ) const
{
	return stereo ? pLeftController() - cStudioHdr->pFlexcontroller((LocalFlexController_t) 0 ) : -1;
}


inline int mstudioflexcontrollerui_t::nWayValueIndex( const IStudioHdr* cStudioHdr ) const
{
	return remaptype == FLEXCONTROLLER_REMAP_NWAY ? pNWayValueController() - cStudioHdr->pFlexcontroller( (LocalFlexController_t)0 ) : -1;
}


inline const mstudioflexcontroller_t *mstudioflexcontrollerui_t::pController( int index ) const
{
	if ( index < 0 || index > Count() )
		return NULL;

	if ( remaptype == FLEXCONTROLLER_REMAP_NWAY )
	{
		if ( stereo )
			return (mstudioflexcontroller_t *)( ( char * ) this ) + *( &szindex0 + index );

		if ( index == 0 )
			return pController();

		if ( index == 1 )
			return pNWayValueController();

		return NULL;
	}

	if ( index > 1 )
		return NULL;

	if ( stereo )
		return (mstudioflexcontroller_t *)( ( char * ) this ) + *( &szindex0 + index );

	if ( index > 0 )
		return NULL;

	return pController();
}


#define STUDIO_CONST	1	// get float
#define STUDIO_FETCH1	2	// get Flexcontroller value
#define STUDIO_FETCH2	3	// get flex weight
#define STUDIO_ADD		4
#define STUDIO_SUB		5
#define STUDIO_MUL		6
#define STUDIO_DIV		7
#define STUDIO_NEG		8	// not implemented
#define STUDIO_EXP		9	// not implemented
#define STUDIO_OPEN		10	// only used in token parsing
#define STUDIO_CLOSE	11
#define STUDIO_COMMA	12	// only used in token parsing
#define STUDIO_MAX		13
#define STUDIO_MIN		14
#define STUDIO_2WAY_0	15	// Fetch a value from a 2 Way slider for the 1st value RemapVal( 0.0, 0.5, 0.0, 1.0 )
#define STUDIO_2WAY_1	16	// Fetch a value from a 2 Way slider for the 2nd value RemapVal( 0.5, 1.0, 0.0, 1.0 )
#define STUDIO_NWAY		17	// Fetch a value from a 2 Way slider for the 2nd value RemapVal( 0.5, 1.0, 0.0, 1.0 )
#define STUDIO_COMBO	18	// Perform a combo operation (essentially multiply the last N values on the stack)
#define STUDIO_DOMINATE	19	// Performs a combination domination operation
#define STUDIO_DME_LOWER_EYELID 20	// 
#define STUDIO_DME_UPPER_EYELID 21	// 

// motion flags
#define STUDIO_X		0x00000001
#define STUDIO_Y		0x00000002	
#define STUDIO_Z		0x00000004
#define STUDIO_XR		0x00000008
#define STUDIO_YR		0x00000010
#define STUDIO_ZR		0x00000020

#define STUDIO_LX		0x00000040
#define STUDIO_LY		0x00000080
#define STUDIO_LZ		0x00000100
#define STUDIO_LXR		0x00000200
#define STUDIO_LYR		0x00000400
#define STUDIO_LZR		0x00000800

#define STUDIO_LINEAR	0x00001000

#define STUDIO_TYPES	0x0003FFFF
#define STUDIO_RLOOP	0x00040000	// controller that wraps shortest distance

// sequence and autolayer flags
#define STUDIO_LOOPING	0x0001		// ending frame should be the same as the starting frame
#define STUDIO_SNAP		0x0002		// do not interpolate between previous animation and this one
#define STUDIO_DELTA	0x0004		// this sequence "adds" to the base sequences, not slerp blends
#define STUDIO_AUTOPLAY	0x0008		// temporary flag that forces the sequence to always play
#define STUDIO_POST		0x0010		// 
#define STUDIO_ALLZEROS	0x0020		// this animation/sequence has no real animation data
//						0x0040
#define STUDIO_CYCLEPOSE 0x0080		// cycle index is taken from a pose parameter index
#define STUDIO_REALTIME	0x0100		// cycle index is taken from a real-time clock, not the animations cycle index
#define STUDIO_LOCAL	0x0200		// sequence has a local context sequence
#define STUDIO_HIDDEN	0x0400		// don't show in default selection views
#define STUDIO_OVERRIDE	0x0800		// a forward declared sequence (empty)
#define STUDIO_ACTIVITY	0x1000		// Has been updated at runtime to activity index
#define STUDIO_EVENT	0x2000		// Has been updated at runtime to event index
#define STUDIO_WORLD	0x4000		// sequence blends in worldspace
// autolayer flags
//							0x0001
//							0x0002
//							0x0004
//							0x0008
#define STUDIO_AL_POST		0x0010		// 
//							0x0020
#define STUDIO_AL_SPLINE	0x0040		// convert layer ramp in/out curve is a spline instead of linear
#define STUDIO_AL_XFADE		0x0080		// pre-bias the ramp curve to compense for a non-1 weight, assuming a second layer is also going to accumulate
//							0x0100
#define STUDIO_AL_NOBLEND	0x0200		// animation always blends at 1.0 (ignores weight)
//							0x0400
//							0x0800
#define STUDIO_AL_LOCAL		0x1000		// layer is a local context sequence
//							0x2000
#define STUDIO_AL_POSE		0x4000		// layer blends using a pose parameter instead of parent cycle






// Determines allocation requirements for vertexes
inline int Studio_VertexDataSize( const vertexFileHeader_t *pVvdHdr, int rootLOD, bool bNeedsTangentS )
{
	// the quantity of vertexes necessary for root lod and all lower detail lods
	// add one extra vertex to each section
	// the extra vertex allows prefetch hints to read ahead 1 vertex without faulting
	int numVertexes = pVvdHdr->numLODVertexes[rootLOD] + 1;
	int dataLength  = pVvdHdr->vertexDataStart + numVertexes*sizeof(mstudiovertex_t);
	if (bNeedsTangentS)
	{
		dataLength += numVertexes*sizeof(Vector4D);
	}

	// allocate this much
	return dataLength;
}

// Load the minimum quantity of verts and run fixups
inline int Studio_LoadVertexes( const vertexFileHeader_t *pTempVvdHdr, vertexFileHeader_t *pNewVvdHdr, int rootLOD, bool bNeedsTangentS )
{
	int					i;
	int					target;
	int					numVertexes;
	vertexFileFixup_t	*pFixupTable;

	numVertexes = pTempVvdHdr->numLODVertexes[rootLOD];

	// copy all data up to start of vertexes
	memcpy((void*)pNewVvdHdr, (void*)pTempVvdHdr, pTempVvdHdr->vertexDataStart);

	for ( i = 0; i < rootLOD; i++)
	{
		pNewVvdHdr->numLODVertexes[i] = pNewVvdHdr->numLODVertexes[rootLOD];
	}

	// fixup data starts
	if (bNeedsTangentS)
	{
		// tangent data follows possibly reduced vertex data
		pNewVvdHdr->tangentDataStart = pNewVvdHdr->vertexDataStart + numVertexes*sizeof(mstudiovertex_t);
	}
	else
	{
		// no tangent data will be available, mark for identification
		pNewVvdHdr->tangentDataStart = 0;
	}

	if (!pNewVvdHdr->numFixups)
	{		
		// fixups not required
		// transfer vertex data
		memcpy(
			(byte *)pNewVvdHdr+pNewVvdHdr->vertexDataStart, 
			(byte *)pTempVvdHdr+pTempVvdHdr->vertexDataStart,
			numVertexes*sizeof(mstudiovertex_t) );

		if (bNeedsTangentS)
		{
			// transfer tangent data to cache memory
			memcpy(
				(byte *)pNewVvdHdr+pNewVvdHdr->tangentDataStart, 
				(byte *)pTempVvdHdr+pTempVvdHdr->tangentDataStart,
				numVertexes*sizeof(Vector4D) );
		}

		return numVertexes;
	}

	// fixups required
	// re-establish mesh ordered vertexes into cache memory, according to table
	target      = 0;
	pFixupTable = (vertexFileFixup_t *)((byte *)pTempVvdHdr + pTempVvdHdr->fixupTableStart);
	for (i=0; i<pTempVvdHdr->numFixups; i++)
	{
		if (pFixupTable[i].lod < rootLOD)
		{
			// working bottom up, skip over copying higher detail lods
			continue;
		}

		// copy vertexes

		// TODO(nillerusr): That sucks and needs to be fixed
		memcpy(
			(byte*)((mstudiovertex_t *)((byte *)pNewVvdHdr+pNewVvdHdr->vertexDataStart) + target),
			(byte*)((mstudiovertex_t *)((byte *)pTempVvdHdr+pTempVvdHdr->vertexDataStart) + pFixupTable[i].sourceVertexID),
			pFixupTable[i].numVertexes*sizeof(mstudiovertex_t) );

		if (bNeedsTangentS)
		{
			// copy tangents
			memcpy(
				(byte*)((Vector4D *)((byte *)pNewVvdHdr+pNewVvdHdr->tangentDataStart) + target),
				(byte*)((Vector4D *)((byte *)pTempVvdHdr+pTempVvdHdr->tangentDataStart) + pFixupTable[i].sourceVertexID),
				pFixupTable[i].numVertexes*sizeof(Vector4D) );
		}

		// data is placed consecutively
		target += pFixupTable[i].numVertexes;
	}

	pNewVvdHdr->numFixups = 0;

	return target;
}

#endif // STUDIO_H
