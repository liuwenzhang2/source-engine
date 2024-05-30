//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: StudioMDL byteswapping functions.
//
// $NoKeywords: $
//=============================================================================
#ifndef STUDIOBYTESWAP_H
#define STUDIOBYTESWAP_H

#if defined(_WIN32)
#pragma once
#endif

#include "byteswap.h"
#include "datacache/imdlcache.h"
class IPhysicsCollision;
extern IMDLCache* g_pMDLCache;
// NOTE! Next time we up the .mdl file format, remove studiohdr2_t
// and insert all fields in this structure into studiohdr_t.
struct studiohdr2_t
{
	// NOTE: For forward compat, make sure any methods in this struct
	// are also available in studiohdr_t so no leaf code ever directly references
	// a studiohdr2_t structure
	DECLARE_BYTESWAP_DATADESC();
	int numsrcbonetransform;
	int srcbonetransformindex;

	int	illumpositionattachmentindex;
	inline int			IllumPositionAttachmentIndex() const { return illumpositionattachmentindex; }

	float flMaxEyeDeflection;
	inline float		MaxEyeDeflection() const { return flMaxEyeDeflection != 0.0f ? flMaxEyeDeflection : 0.866f; } // default to cos(30) if not set

	int linearboneindex;
	inline mstudiolinearbone_t* pLinearBones() const { return (linearboneindex) ? (mstudiolinearbone_t*)(((byte*)this) + linearboneindex) : NULL; }

	int sznameindex;
	inline char* pszName() { return (sznameindex) ? (char*)(((byte*)this) + sznameindex) : NULL; }

	int m_nBoneFlexDriverCount;
	int m_nBoneFlexDriverIndex;
	inline mstudioboneflexdriver_t* pBoneFlexDriver(int i) const { Assert(i >= 0 && i < m_nBoneFlexDriverCount); return (mstudioboneflexdriver_t*)(((byte*)this) + m_nBoneFlexDriverIndex) + i; }

	mutable serializedstudioptr_t< void	> virtualModel;
	mutable serializedstudioptr_t< void	> animblockModel;

	serializedstudioptr_t< void> pVertexBase;
	serializedstudioptr_t< void> pIndexBase;

	int reserved[48];
};

class CVirtualModel;
struct studiohdr_t
{
	DECLARE_BYTESWAP_DATADESC();
	studiohdr_t() = default;

	int					id;
	int					version;

	int					checksum;		// this has to be the same in the phy and vtx files to load!

	inline const char* pszName(void) const { if (studiohdr2index && pStudioHdr2()->pszName()) return pStudioHdr2()->pszName(); else return name; }
	char				name[64];
	int					length;


	Vector				eyeposition;	// ideal eye position

	Vector				illumposition;	// illumination center

	Vector				hull_min;		// ideal movement hull size
	Vector				hull_max;

	Vector				view_bbmin;		// clipping bounding box
	Vector				view_bbmax;

	int					flags;

	int					numbones;			// bones
	int					boneindex;
	inline mstudiobone_t* pBone(int i) const { Assert(i >= 0 && i < numbones); return (mstudiobone_t*)(((byte*)this) + boneindex) + i; };
	int					RemapSeqBone(int iSequence, int iLocalBone) const;	// maps local sequence bone to global bone
	int					RemapAnimBone(int iAnim, int iLocalBone) const;		// maps local animations bone to global bone

	int					numbonecontrollers;		// bone controllers
	int					bonecontrollerindex;
	inline mstudiobonecontroller_t* pBonecontroller(int i) const { Assert(i >= 0 && i < numbonecontrollers); return (mstudiobonecontroller_t*)(((byte*)this) + bonecontrollerindex) + i; };

	int					numhitboxsets;
	int					hitboxsetindex;

	// Look up hitbox set by index
	mstudiohitboxset_t* pHitboxSet(int i) const
	{
		Assert(i >= 0 && i < numhitboxsets);
		return (mstudiohitboxset_t*)(((byte*)this) + hitboxsetindex) + i;
	};

	// Calls through to hitbox to determine size of specified set
	inline mstudiobbox_t* pHitbox(int i, int set) const
	{
		mstudiohitboxset_t const* s = pHitboxSet(set);
		if (!s)
			return NULL;

		return s->pHitbox(i);
	};

	// Calls through to set to get hitbox count for set
	inline int			iHitboxCount(int set) const
	{
		mstudiohitboxset_t const* s = pHitboxSet(set);
		if (!s)
			return 0;

		return s->numhitboxes;
	};

	// file local animations? and sequences
//private:
	int					numlocalanim;			// animations/poses
	int					localanimindex;		// animation descriptions
	inline mstudioanimdesc_t* pLocalAnimdesc(int i) const { if (i < 0 || i >= numlocalanim) i = 0; return (mstudioanimdesc_t*)(((byte*)this) + localanimindex) + i; };

	int					numlocalseq;				// sequences
	int					localseqindex;
	inline mstudioseqdesc_t* pLocalSeqdesc(int i) const { if (i < 0 || i >= numlocalseq) i = 0; return (mstudioseqdesc_t*)(((byte*)this) + localseqindex) + i; };

	//public:
	bool				SequencesAvailable() const;
	int					GetNumSeq() const;
	mstudioanimdesc_t& pAnimdesc(int i) const;
	mstudioseqdesc_t& pSeqdesc(int i) const;
	int					iRelativeAnim(int baseseq, int relanim) const;	// maps seq local anim reference to global anim index
	int					iRelativeSeq(int baseseq, int relseq) const;		// maps seq local seq reference to global seq index

	//private:
	mutable int			activitylistversion;	// initialization flag - have the sequences been indexed?
	mutable int			eventsindexed;
	//public:
		//int					GetSequenceActivity( int iSequence );
		//void				SetSequenceActivity( int iSequence, int iActivity );
	int					GetActivityListVersion(void);
	void				SetActivityListVersion(int version) const;
	int					GetEventListVersion(void);
	void				SetEventListVersion(int version);

	// raw textures
	int					numtextures;
	int					textureindex;
	inline mstudiotexture_t* pTexture(int i) const { Assert(i >= 0 && i < numtextures); return (mstudiotexture_t*)(((byte*)this) + textureindex) + i; };


	// raw textures search paths
	int					numcdtextures;
	int					cdtextureindex;
	inline char* pCdtexture(int i) const { return (((char*)this) + *((int*)(((byte*)this) + cdtextureindex) + i)); };

	// replaceable textures tables
	int					numskinref;
	int					numskinfamilies;
	int					skinindex;
	inline short* pSkinref(int i) const { return (short*)(((byte*)this) + skinindex) + i; };

	int					numbodyparts;
	int					bodypartindex;
	inline mstudiobodyparts_t* pBodypart(int i) const { return (mstudiobodyparts_t*)(((byte*)this) + bodypartindex) + i; };

	// queryable attachable points
//private:
	int					numlocalattachments;
	int					localattachmentindex;
	inline mstudioattachment_t* pLocalAttachment(int i) const { Assert(i >= 0 && i < numlocalattachments); return (mstudioattachment_t*)(((byte*)this) + localattachmentindex) + i; };
	//public:
	int					GetNumAttachments(void) const;
	const mstudioattachment_t& pAttachment(int i) const;
	int					GetAttachmentBone(int i);
	// used on my tools in hlmv, not persistant
	void				SetAttachmentBone(int iAttachment, int iBone);

	// animation node to animation node transition graph
//private:
	int					numlocalnodes;
	int					localnodeindex;
	int					localnodenameindex;
	inline char* pszLocalNodeName(int iNode) const { Assert(iNode >= 0 && iNode < numlocalnodes); return (((char*)this) + *((int*)(((byte*)this) + localnodenameindex) + iNode)); }
	inline byte* pLocalTransition(int i) const { Assert(i >= 0 && i < (numlocalnodes * numlocalnodes)); return (byte*)(((byte*)this) + localnodeindex) + i; };

	//public:
	int					EntryNode(int iSequence);
	int					ExitNode(int iSequence);
	char* pszNodeName(int iNode);
	int					GetTransition(int iFrom, int iTo) const;

	int					numflexdesc;
	int					flexdescindex;
	inline mstudioflexdesc_t* pFlexdesc(int i) const { Assert(i >= 0 && i < numflexdesc); return (mstudioflexdesc_t*)(((byte*)this) + flexdescindex) + i; };

	int					numflexcontrollers;
	int					flexcontrollerindex;
	inline mstudioflexcontroller_t* pFlexcontroller(LocalFlexController_t i) const { Assert(numflexcontrollers == 0 || (i >= 0 && i < numflexcontrollers)); return (mstudioflexcontroller_t*)(((byte*)this) + flexcontrollerindex) + i; };

	int					numflexrules;
	int					flexruleindex;
	inline mstudioflexrule_t* pFlexRule(int i) const { Assert(i >= 0 && i < numflexrules); return (mstudioflexrule_t*)(((byte*)this) + flexruleindex) + i; };

	int					numikchains;
	int					ikchainindex;
	inline mstudioikchain_t* pIKChain(int i) const { Assert(i >= 0 && i < numikchains); return (mstudioikchain_t*)(((byte*)this) + ikchainindex) + i; };

	int					nummouths;
	int					mouthindex;
	inline mstudiomouth_t* pMouth(int i) const { Assert(i >= 0 && i < nummouths); return (mstudiomouth_t*)(((byte*)this) + mouthindex) + i; };

	//private:
	int					numlocalposeparameters;
	int					localposeparamindex;
	inline mstudioposeparamdesc_t* pLocalPoseParameter(int i) const { Assert(i >= 0 && i < numlocalposeparameters); return (mstudioposeparamdesc_t*)(((byte*)this) + localposeparamindex) + i; };
	//public:
	int					GetNumPoseParameters(void) const;
	const mstudioposeparamdesc_t& pPoseParameter(int i);
	int					GetSharedPoseParameter(int iSequence, int iLocalPose) const;

	int					surfacepropindex;
	inline char* const pszSurfaceProp(void) const { return ((char*)this) + surfacepropindex; }

	// Key values
	int					keyvalueindex;
	int					keyvaluesize;
	inline const char* KeyValueText(void) const { return keyvaluesize != 0 ? ((char*)this) + keyvalueindex : NULL; }

	int					numlocalikautoplaylocks;
	int					localikautoplaylockindex;
	inline mstudioiklock_t* pLocalIKAutoplayLock(int i) const { Assert(i >= 0 && i < numlocalikautoplaylocks); return (mstudioiklock_t*)(((byte*)this) + localikautoplaylockindex) + i; };

	int					GetNumIKAutoplayLocks(void) const;
	const mstudioiklock_t& pIKAutoplayLock(int i);
	int					CountAutoplaySequences() const;
	int					CopyAutoplaySequences(unsigned short* pOut, int outCount) const;
	//int					GetAutoplayList( unsigned short **pOut ) const;

	// The collision model mass that jay wanted
	float				mass;
	int					contents;

	// external animations, models, etc.
	int					numincludemodels;
	int					includemodelindex;
	inline mstudiomodelgroup_t* pModelGroup(int i) const { Assert(i >= 0 && i < numincludemodels); return (mstudiomodelgroup_t*)(((byte*)this) + includemodelindex) + i; };
	// implementation specific call to get a named model
	const studiohdr_t* FindModel(void** cache, char const* modelname) const;

	// implementation specific back pointer to virtual data
	int                 unused_virtualModel;
	CVirtualModel*		GetVirtualModel(void) const;

	// for demand loaded animation blocks
	int					szanimblocknameindex;
	inline char* const pszAnimBlockName(void) const { return ((char*)this) + szanimblocknameindex; }
	int					numanimblocks;
	int					animblockindex;
	inline mstudioanimblock_t* pAnimBlock(int i) const { Assert(i > 0 && i < numanimblocks); return (mstudioanimblock_t*)(((byte*)this) + animblockindex) + i; };

	int                 unused_animblockModel;
	byte* GetAnimBlock(int i) const;

	int					bonetablebynameindex;
	inline const byte* GetBoneTableSortedByName() const { return (byte*)this + bonetablebynameindex; }

	// used by tools only that don't cache, but persist mdl's peer data
	// engine uses virtualModel to back link to cache pointers
	int                 unused_pVertexBase;
	int                 unused_pIndexBase;

	// if STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT is set,
	// this value is used to calculate directional components of lighting 
	// on static props
	byte				constdirectionallightdot;

	// set during load of mdl data to track *desired* lod configuration (not actual)
	// the *actual* clamped root lod is found in studiohwdata
	// this is stored here as a global store to ensure the staged loading matches the rendering
	byte				rootLOD;

	// set in the mdl data to specify that lod configuration should only allow first numAllowRootLODs
	// to be set as root LOD:
	//	numAllowedRootLODs = 0	means no restriction, any lod can be set as root lod.
	//	numAllowedRootLODs = N	means that lod0 - lod(N-1) can be set as root lod, but not lodN or lower.
	byte				numAllowedRootLODs;

	byte				unused[1];

	int					unused4; // zero out if version < 47

	int					numflexcontrollerui;
	int					flexcontrolleruiindex;
	mstudioflexcontrollerui_t* pFlexControllerUI(int i) const { Assert(i >= 0 && i < numflexcontrollerui); return (mstudioflexcontrollerui_t*)(((byte*)this) + flexcontrolleruiindex) + i; }

	float				flVertAnimFixedPointScale;
	inline float		VertAnimFixedPointScale() const { return (flags & STUDIOHDR_FLAGS_VERT_ANIM_FIXED_POINT_SCALE) ? flVertAnimFixedPointScale : 1.0f / 4096.0f; }

	int					unused3[1];

	// FIXME: Remove when we up the model version. Move all fields of studiohdr2_t into studiohdr_t.
	int					studiohdr2index;
	studiohdr2_t* pStudioHdr2() const { return (studiohdr2_t*)(((byte*)this) + studiohdr2index); }

	// Src bone transforms are transformations that will convert .dmx or .smd-based animations into .mdl-based animations
	int					NumSrcBoneTransforms() const { return studiohdr2index ? pStudioHdr2()->numsrcbonetransform : 0; }
	const mstudiosrcbonetransform_t* SrcBoneTransform(int i) const { Assert(i >= 0 && i < NumSrcBoneTransforms()); return (mstudiosrcbonetransform_t*)(((byte*)this) + pStudioHdr2()->srcbonetransformindex) + i; }

	inline int			IllumPositionAttachmentIndex() const { return studiohdr2index ? pStudioHdr2()->IllumPositionAttachmentIndex() : 0; }

	inline float		MaxEyeDeflection() const { return studiohdr2index ? pStudioHdr2()->MaxEyeDeflection() : 0.866f; } // default to cos(30) if not set

	inline mstudiolinearbone_t* pLinearBones() const { return studiohdr2index ? pStudioHdr2()->pLinearBones() : NULL; }

	inline int			BoneFlexDriverCount() const { return studiohdr2index ? pStudioHdr2()->m_nBoneFlexDriverCount : 0; }
	inline const mstudioboneflexdriver_t* BoneFlexDriver(int i) const { Assert(i >= 0 && i < BoneFlexDriverCount()); return studiohdr2index > 0 ? pStudioHdr2()->pBoneFlexDriver(i) : NULL; }

	void* VirtualModel() const { return studiohdr2index ? (void*)(pStudioHdr2()->virtualModel) : nullptr; }
	void				SetVirtualModel(void* ptr) { Assert(studiohdr2index); if (studiohdr2index) { pStudioHdr2()->virtualModel = ptr; } else { Msg("go fuck urself!\n"); } }
	void* VertexBase() const { return studiohdr2index ? (void*)(pStudioHdr2()->pVertexBase) : nullptr; }
	void				SetVertexBase(void* pVertexBase) const { Assert(studiohdr2index); if (studiohdr2index) { pStudioHdr2()->pVertexBase = pVertexBase; } }
	void* IndexBase() const { return studiohdr2index ? (void*)(pStudioHdr2()->pIndexBase) : nullptr; }
	void				SetIndexBase(void* pIndexBase) const { Assert(studiohdr2index); if (studiohdr2index) { pStudioHdr2()->pIndexBase = pIndexBase; } }

	// NOTE: No room to add stuff? Up the .mdl file format version 
	// [and move all fields in studiohdr2_t into studiohdr_t and kill studiohdr2_t],
	// or add your stuff to studiohdr2_t. See NumSrcBoneTransforms/SrcBoneTransform for the pattern to use.
	int					unused2[1];
private:
	// No copy constructors allowed
	studiohdr_t(const studiohdr_t& vOther);

	friend class CVirtualModel;
};

// must be run to fixup with specified rootLOD
inline void Studio_SetRootLOD(studiohdr_t* pStudioHdr, int rootLOD)
{
	// honor studiohdr restriction of root lod in case requested root lod exceeds restriction.
	if (pStudioHdr->numAllowedRootLODs > 0 &&
		rootLOD >= pStudioHdr->numAllowedRootLODs)
	{
		rootLOD = pStudioHdr->numAllowedRootLODs - 1;
	}

	Assert(rootLOD >= 0 && rootLOD < MAX_NUM_LODS);
	Clamp(rootLOD, 0, MAX_NUM_LODS - 1);

	// run the lod fixups that culls higher detail lods
	// vertexes are external, fixups ensure relative offsets and counts are cognizant of shrinking data
	// indexes are built in lodN..lod0 order so higher detail lod data can be truncated at load
	// the fixup lookup arrays are filled (or replicated) to ensure all slots valid
	int vertexindex = 0;
	int tangentsindex = 0;
	int bodyPartID;
	for (bodyPartID = 0; bodyPartID < pStudioHdr->numbodyparts; bodyPartID++)
	{
		mstudiobodyparts_t* pBodyPart = pStudioHdr->pBodypart(bodyPartID);
		int modelID;
		for (modelID = 0; modelID < pBodyPart->nummodels; modelID++)
		{
			mstudiomodel_t* pModel = pBodyPart->pModel(modelID);
			int totalMeshVertexes = 0;
			int meshID;
			for (meshID = 0; meshID < pModel->nummeshes; meshID++)
			{
				mstudiomesh_t* pMesh = pModel->pMesh(meshID);

				// get the fixup, vertexes are reduced
				pMesh->numvertices = pMesh->vertexdata.numLODVertexes[rootLOD];
				pMesh->vertexoffset = totalMeshVertexes;
				totalMeshVertexes += pMesh->numvertices;
			}

			// stay in sync
			pModel->numvertices = totalMeshVertexes;
			pModel->vertexindex = vertexindex;
			pModel->tangentsindex = tangentsindex;

			vertexindex += totalMeshVertexes * sizeof(mstudiovertex_t);
			tangentsindex += totalMeshVertexes * sizeof(Vector4D);
		}
	}

	// track the set desired configuration
	pStudioHdr->rootLOD = rootLOD;
}

// Insert this code anywhere that you need to allow for conversion from an old STUDIO_VERSION
// to a new one.
// If we only support the current version, this function should be empty.
inline bool Studio_ConvertStudioHdrToNewVersion(studiohdr_t* pStudioHdr)
{
	COMPILE_TIME_ASSERT(STUDIO_VERSION == 49); //  put this to make sure this code is updated upon changing version.

	int version = pStudioHdr->version;
	if (version == STUDIO_VERSION)
		return true;

	bool bResult = true;

	if (version < 46)
	{
		// some of the anim index data is incompatible
		for (int i = 0; i < pStudioHdr->numlocalanim; i++)
		{
			mstudioanimdesc_t* pAnim = (mstudioanimdesc_t*)pStudioHdr->pLocalAnimdesc(i);

			// old ANI files that used sections (v45 only) are not compatible
			if (pAnim->sectionframes != 0)
			{
				// zero most everything out
				memset(&(pAnim->numframes), 0, (byte*)(pAnim + 1) - (byte*)&(pAnim->numframes));

				pAnim->numframes = 1;
				pAnim->animblock = -1; // disable animation fetching
				bResult = false;
			}
		}
	}

	if (version < 47)
	{
		// used to contain zeroframe cache data
		if (pStudioHdr->unused4 != 0)
		{
			pStudioHdr->unused4 = 0;
			bResult = false;
		}
		for (int i = 0; i < pStudioHdr->numlocalanim; i++)
		{
			mstudioanimdesc_t* pAnim = (mstudioanimdesc_t*)pStudioHdr->pLocalAnimdesc(i);
			pAnim->zeroframeindex = 0;
			pAnim->zeroframespan = 0;
		}
	}
	else if (version == 47)
	{
		for (int i = 0; i < pStudioHdr->numlocalanim; i++)
		{
			mstudioanimdesc_t* pAnim = (mstudioanimdesc_t*)pStudioHdr->pLocalAnimdesc(i);
			if (pAnim->zeroframeindex != 0)
			{
				pAnim->zeroframeindex = 0;
				pAnim->zeroframespan = 0;
				bResult = false;
			}
		}
	}

	// for now, just slam the version number since they're compatible

	// nillerusr: that's stupid, comment this shit
	//pStudioHdr->version = STUDIO_VERSION;

	return bResult;
}

class CVirtualModel : public IVirtualModel
{
public:
	void AppendSequences(int group, const studiohdr_t* pStudioHdr);
	void AppendAnimations(int group, const studiohdr_t* pStudioHdr);
	void AppendAttachments(int ground, const studiohdr_t* pStudioHdr);
	void AppendPoseParameters(int group, const studiohdr_t* pStudioHdr);
	void AppendBonemap(int group, const studiohdr_t* pStudioHdr);
	void AppendNodes(int group, const studiohdr_t* pStudioHdr);
	//void AppendTransitions( int group, const studiohdr_t *pStudioHdr );
	void AppendIKLocks(int group, const studiohdr_t* pStudioHdr);
	void AppendModels(int group, const studiohdr_t* pStudioHdr);
	void UpdateAutoplaySequences(const studiohdr_t* pStudioHdr);

	virtualgroup_t* pAnimGroup(int animation) { return &m_group[m_anim[animation].group]; } // Note: user must manage mutex for this
	int				pAnimIndex(int animation) { return m_anim[animation].index; }
	virtualgroup_t* pSeqGroup(int sequence)
	{
		// Check for out of range access that is causing crashes on some servers.
		// Perhaps caused by sourcemod bugs. Typical sequence in these cases is ~292
		// when the count is 234. Using unsigned math allows for free range
		// checking against zero.
		if ((unsigned)sequence >= (unsigned)m_seq.Count())
		{
			Assert(0);
			return 0;
		}
		return &m_group[m_seq[sequence].group];
	} // Note: user must manage mutex for this
	int	pSeqIndex(int animation) { return m_seq[animation].index; }
	int NumSeq() { return m_seq.Count(); }
	int NumPose() { return m_pose.Count(); }
	int nPoseGroup(int pose) { return m_pose[pose].group; }
	int pPoseIndex(int pose) { return m_pose[pose].index; }
	virtualgroup_t* pPoseGroup(int pose) { return &m_group[m_pose[pose].group]; }
	int NumAttachment() { return m_attachment.Count(); }
	virtualgroup_t* pAttachmentGroup(int attachment) { return &m_group[m_attachment[attachment].group]; }
	int pAttachmentIndex(int attachment) { return m_attachment[attachment].index; }
	int NumNode() { return m_node.Count(); }
	virtualgroup_t* pNodeGroup(int node) { return &m_group[m_node[node].group]; }
	int pNodeIndex(int node) { return m_node[node].index; }
	int NumGroup() { return m_group.Count(); }
	virtualgroup_t* pGroup(int group) { return &m_group[group]; }
	IStudioHdr* GetIStudioHdr(int group) { return g_pMDLCache->GetIStudioHdr(VoidPtrToMDLHandle(m_group[group].cache)); }
	void* GetUserData(int group) { return g_pMDLCache->GetUserData(VoidPtrToMDLHandle(m_group[group].cache)); }
	int NumIKlock() { return m_iklock.Count(); }
	virtualgroup_t* pIKlockGroup(int iklock) { return &m_group[m_iklock[iklock].group]; }
	int pIKlockIndex(int iklock) { return m_iklock[iklock].index; }
	const studiohdr_t* GetGroupStudioHdr(virtualgroup_t* pGroup) const;

	CThreadFastMutex m_Lock;

	CUtlVector< virtualsequence_t > m_seq;
	CUtlVector< virtualgeneric_t > m_anim;
	CUtlVector< virtualgeneric_t > m_attachment;
	CUtlVector< virtualgeneric_t > m_pose;
	CUtlVector< virtualgroup_t > m_group;
	CUtlVector< virtualgeneric_t > m_node;
	CUtlVector< virtualgeneric_t > m_iklock;
	CUtlVector< unsigned short > m_autoplaySequences;
};

class CStudioHdr : public IStudioHdr
{
	friend struct studiohdr_t;
	friend class CMDLCache;
	friend class virtualgroup_t;
public:
	CStudioHdr(void);
	CStudioHdr(studiohdr_t* pStudioHdr, IMDLCache* mdlcache = NULL);
	~CStudioHdr() { Term(); }

	void Init(studiohdr_t* pStudioHdr, IMDLCache* mdlcache = NULL);
	void Term();

public:
	inline bool IsVirtual(void) { return (m_pVModel != NULL); };
	inline bool IsValid(void) { return (m_pStudioHdr != NULL); };
	inline bool IsReadyForAccess(void) const { return (m_pStudioHdr != NULL); };
	inline CVirtualModel* GetVirtualModel(void) const { return m_pVModel; };
	inline const studiohdr_t* GetRenderHdr(void) const { return m_pStudioHdr; };
	const IStudioHdr* pSeqStudioHdr(int sequence) const;
	const IStudioHdr* pAnimStudioHdr(int animation) const;
	const IStudioHdr* RealStudioHdr(studiohdr_t* pStudioHdr) const;
	void FreeRealStudioHdr(const IStudioHdr* pStudioHdr) const;

private:
	mutable studiohdr_t* m_pStudioHdr = NULL;
	mutable CVirtualModel* m_pVModel = NULL;

	const CVirtualModel* ResetVModel(const CVirtualModel* pVModel) const;
	const CStudioHdr* GroupStudioHdr(int group) const;
	mutable CUtlVector< CStudioHdr* > m_pStudioHdrCache;

	mutable int			m_nFrameUnlockCounter = 0;
	int* m_pFrameUnlockCounter = 0;
	CThreadFastMutex	m_FrameUnlockCounterMutex;

public:
	inline int			numbones(void) const { return m_pStudioHdr->numbones; };
	inline mstudiobone_t* pBone(int i) const { return m_pStudioHdr->pBone(i); };
	int					RemapAnimBone(int iAnim, int iLocalBone) const;		// maps local animations bone to global bone
	int					RemapSeqBone(int iSequence, int iLocalBone) const;	// maps local sequence bone to global bone

	bool				SequencesAvailable() const;
	int					GetNumSeq(void) const;
	mstudioanimdesc_t& pAnimdesc(int i);
	mstudioseqdesc_t& pSeqdesc(int iSequence);
	int					iRelativeAnim(int baseseq, int relanim) const;	// maps seq local anim reference to global anim index
	int					iRelativeSeq(int baseseq, int relseq) const;		// maps seq local seq reference to global seq index

	//int					GetSequenceActivity( int iSequence );
	//void				SetSequenceActivity( int iSequence, int iActivity );
	int					GetActivityListVersion(void);
	void				SetActivityListVersion(int version) const;
	int					GetEventListVersion(void);
	void				SetEventListVersion(int version);

	int					GetNumAttachments(void) const;
	const mstudioattachment_t& pAttachment(int i);
	int					GetAttachmentBone(int i);
	// used on my tools in hlmv, not persistant
	void				SetAttachmentBone(int iAttachment, int iBone);

	int					EntryNode(int iSequence);
	int					ExitNode(int iSequence);
	char* pszNodeName(int iNode);
	// FIXME: where should this one be?
	int					GetTransition(int iFrom, int iTo) const;

	int					GetNumPoseParameters(void) const;
	const mstudioposeparamdesc_t& pPoseParameter(int i);
	int					GetSharedPoseParameter(int iSequence, int iLocalPose) const;

	int					GetNumIKAutoplayLocks(void) const;
	const mstudioiklock_t& pIKAutoplayLock(int i);

	inline int			CountAutoplaySequences() const { return m_pStudioHdr->CountAutoplaySequences(); };
	inline int			CopyAutoplaySequences(unsigned short* pOut, int outCount) const { return m_pStudioHdr->CopyAutoplaySequences(pOut, outCount); };
	inline int			GetAutoplayList(unsigned short** pOut) const { return g_pMDLCache->GetAutoplayList(VoidPtrToMDLHandle(m_pStudioHdr->VirtualModel()), pOut); };

	inline int			GetNumBoneControllers(void) const { return m_pStudioHdr->numbonecontrollers; };
	inline mstudiobonecontroller_t* pBonecontroller(int i) const { return m_pStudioHdr->pBonecontroller(i); };

	inline int			numikchains() const { return m_pStudioHdr->numikchains; };
	inline int			GetNumIKChains(void) const { return m_pStudioHdr->numikchains; };
	inline mstudioikchain_t* pIKChain(int i) const { return m_pStudioHdr->pIKChain(i); };

	inline int			numflexrules() const { return m_pStudioHdr->numflexrules; };
	inline mstudioflexrule_t* pFlexRule(int i) const { return m_pStudioHdr->pFlexRule(i); };

	inline int			numflexdesc() const { return m_pStudioHdr->numflexdesc; };
	inline mstudioflexdesc_t* pFlexdesc(int i) const { return m_pStudioHdr->pFlexdesc(i); };

	inline LocalFlexController_t			numflexcontrollers() const { return (LocalFlexController_t)m_pStudioHdr->numflexcontrollers; };
	inline mstudioflexcontroller_t* pFlexcontroller(LocalFlexController_t i) const { return m_pStudioHdr->pFlexcontroller(i); };

	inline int			numflexcontrollerui() const { return m_pStudioHdr->numflexcontrollerui; };
	inline mstudioflexcontrollerui_t* pFlexcontrollerUI(int i) const { return m_pStudioHdr->pFlexControllerUI(i); };

	//inline const char	*name() const { return m_pStudioHdr->name; }; // deprecated -- remove after full xbox merge
	inline const char* pszName() const { return m_pStudioHdr->pszName(); };

	inline int			numbonecontrollers() const { return m_pStudioHdr->numbonecontrollers; };

	inline int			numhitboxsets() const { return m_pStudioHdr->numhitboxsets; };
	inline mstudiohitboxset_t* pHitboxSet(int i) const { return m_pStudioHdr->pHitboxSet(i); };

	inline mstudiobbox_t* pHitbox(int i, int set) const { return m_pStudioHdr->pHitbox(i, set); };
	inline int			iHitboxCount(int set) const { return m_pStudioHdr->iHitboxCount(set); };

	inline int			numbodyparts() const { return m_pStudioHdr->numbodyparts; };
	inline mstudiobodyparts_t* pBodypart(int i) const { return m_pStudioHdr->pBodypart(i); };

	inline int			numskinfamilies() const { return m_pStudioHdr->numskinfamilies; }

	inline Vector		eyeposition() const { return m_pStudioHdr->eyeposition; };

	inline int& flags() { return m_pStudioHdr->flags; };

	inline char* const pszSurfaceProp(void) const { return m_pStudioHdr->pszSurfaceProp(); };

	inline float		mass() const { return m_pStudioHdr->mass; };
	inline int			contents() const { return m_pStudioHdr->contents; }

	inline const byte* GetBoneTableSortedByName() const { return m_pStudioHdr->GetBoneTableSortedByName(); };

	inline Vector		illumposition() const { return m_pStudioHdr->illumposition; };

	inline Vector		hull_min() const { return m_pStudioHdr->hull_min; };		// ideal movement hull size
	inline Vector		hull_max() const { return m_pStudioHdr->hull_max; };

	inline Vector		view_bbmin() const { return m_pStudioHdr->view_bbmin; };		// clipping bounding box
	inline Vector		view_bbmax() const { return m_pStudioHdr->view_bbmax; };

	inline int			numtextures() const { return m_pStudioHdr->numtextures; };

	inline int			IllumPositionAttachmentIndex() const { return m_pStudioHdr->IllumPositionAttachmentIndex(); }

	inline float		MaxEyeDeflection() const { return m_pStudioHdr->MaxEyeDeflection(); }

	inline mstudiolinearbone_t* pLinearBones() const { return m_pStudioHdr->pLinearBones(); }

	inline int			BoneFlexDriverCount() const { return m_pStudioHdr->BoneFlexDriverCount(); }
	inline const mstudioboneflexdriver_t* BoneFlexDriver(int i) const { return m_pStudioHdr->BoneFlexDriver(i); }

	inline float		VertAnimFixedPointScale() const { return m_pStudioHdr->VertAnimFixedPointScale(); }

public:
	//int IsSequenceLooping( int iSequence );
	//float GetSequenceCycleRate( int iSequence );

	void				RunFlexRules(const float* src, float* dest);

	int nummouths() { return m_pStudioHdr->nummouths; }
	int numskinref() const { return m_pStudioHdr->numskinref; }
	short* pSkinref(int i) const { return  m_pStudioHdr->pSkinref(i); }
	byte constdirectionallightdot() { return m_pStudioHdr->constdirectionallightdot; }
	vertexFileHeader_t* CacheVertexData() { return g_pMDLCache->GetVertexData(VoidPtrToMDLHandle(m_pStudioHdr->VirtualModel())); }
	mstudiomouth_t* pMouth(int i) const { return m_pStudioHdr->pMouth(i); }
	int checksum() { return m_pStudioHdr->checksum; }
	byte rootLOD() { return m_pStudioHdr->rootLOD; }
	int numcdtextures() { return m_pStudioHdr->numcdtextures; }
	int textureindex() { return m_pStudioHdr->textureindex; }
	char* pCdtexture(int i) const { return m_pStudioHdr->pCdtexture(i); }
	mstudiotexture_t* pTexture(int i) const { return m_pStudioHdr->pTexture(i); }
	unsigned char* GetAnimBlock(int nBlock) const { return g_pMDLCache->GetAnimBlock(VoidPtrToMDLHandle(m_pStudioHdr->VirtualModel()), nBlock); }
	int numincludemodels() const { return m_pStudioHdr->numincludemodels; }
	const char* KeyValueText(void) const { return m_pStudioHdr->KeyValueText(); }
	int length() { return m_pStudioHdr->length; }
	mstudioanimdesc_t* pLocalAnimdesc(int i) const { return m_pStudioHdr->pLocalAnimdesc(i); }
	mstudioseqdesc_t* pLocalSeqdesc(int i) const { return m_pStudioHdr->pLocalSeqdesc(i); }
	mstudioposeparamdesc_t* pLocalPoseParameter(int i) const { return m_pStudioHdr->pLocalPoseParameter(i); }
	mstudioattachment_t* pLocalAttachment(int i) const { return m_pStudioHdr->pLocalAttachment(i); }
	char* pszLocalNodeName(int iNode) const { return m_pStudioHdr->pszLocalNodeName(iNode); }
	int activitylistversion() const { return m_pStudioHdr->activitylistversion; }
	mstudioiklock_t* pLocalIKAutoplayLock(int i) const { return m_pStudioHdr->pLocalIKAutoplayLock(i); }

public:
	inline int boneFlags(int iBone) const { return m_boneFlags[iBone]; }
	inline int boneParent(int iBone) const { return m_boneParent[iBone]; }

private:
	CUtlVector< int >  m_boneFlags;
	CUtlVector< int >  m_boneParent;

public:

	// This class maps an activity to sequences allowed for that activity, accelerating the resolution
	// of SelectWeightedSequence(), especially on PowerPC. Iterating through every sequence
	// attached to a model turned out to be a very destructive cache access pattern on 360.
	// 
	// I've encapsulated this behavior inside a nested class for organizational reasons; there is
	// no particular programmatic or efficiency benefit to it. It just makes clearer what particular
	// code in the otherwise very complicated StudioHdr class has to do with this particular
	// optimization, and it lets you collapse the whole definition down to a single line in Visual
	// Studio.
	class CActivityToSequenceMapping /* final */
	{
	public:
		// A tuple of a sequence and its corresponding weight. Lists of these correspond to activities.
		struct SequenceTuple
		{
			short		seqnum;
			short		weight; // the absolute value of the weight from the sequence header
			CUtlSymbol* pActivityModifiers;		// list of activity modifier symbols
			int			iNumActivityModifiers;
		};

		// The type of the hash's stored data, a composite of both key and value
		// (because that's how CUtlHash works):
		// key: an int, the activity #
		// values: an index into the m_pSequenceTuples array, a count of the
		// total sequences present for an activity, and the sum of their
		// weights.
		// Note this struct is 128-bits wide, exactly coincident to a PowerPC 
		// cache line and VMX register. Please consider very carefully the
		// performance implications before adding any additional fields to this.
		// You could probably do away with totalWeight if you really had to.
		struct HashValueType
		{
			// KEY (hashed)
			int activityIdx;

			// VALUE (not hashed)
			int startingIdx;
			int count;
			int totalWeight;

			HashValueType(int _actIdx, int _stIdx, int _ct, int _tW) :
				activityIdx(_actIdx), startingIdx(_stIdx), count(_ct), totalWeight(_tW) {}

			// default constructor (ought not to be actually used)
			HashValueType() : activityIdx(-1), startingIdx(-1), count(-1), totalWeight(-1)
			{
				AssertMsg(false, "Don't use default HashValueType()!");
			}


			class HashFuncs
			{
			public:
				// dummy constructor (gndn)
				HashFuncs(int) {}

				// COMPARE
				// compare two entries for uniqueness. We should never have two different
				// entries for the same activity, so we only compare the activity index;
				// this allows us to use the utlhash as a dict by constructing dummy entries
				// as hash lookup keys.
				bool operator()(const HashValueType& lhs, const HashValueType& rhs) const
				{
					return lhs.activityIdx == rhs.activityIdx;
				}

				// HASH
				// We only hash on the activity index; everything else is data.
				unsigned int operator()(const HashValueType& item) const
				{
					return HashInt(item.activityIdx);
				}
			};
		};

		typedef CUtlHash<HashValueType, HashValueType::HashFuncs, HashValueType::HashFuncs> ActivityToValueIdxHash;

		// These must be here because IFM does not compile/link studio.cpp (?!?)

		// ctor
		CActivityToSequenceMapping(void)
			: m_pSequenceTuples(NULL), m_iSequenceTuplesCount(0), m_ActToSeqHash(8, 0, 0), m_expectedPStudioHdr(NULL), m_expectedVModel(NULL)
#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
			, m_bIsInitialized(false)
#endif
		{};

		// dtor -- not virtual because this class has no inheritors
		~CActivityToSequenceMapping()
		{
			if (m_pSequenceTuples != NULL)
			{
				if (m_pSequenceTuples->pActivityModifiers != NULL)
				{
					delete[] m_pSequenceTuples->pActivityModifiers;
				}
				delete[] m_pSequenceTuples;
			}
		}

		/// Get the list of sequences for an activity. Returns the pointer to the
		/// first sequence tuple. Output parameters are a count of sequences present,
		/// and the total weight of all the sequences. (it would be more LHS-friendly
		/// to return these on registers, if only C++ offered more than one return 
		/// value....)
		const SequenceTuple* GetSequences(int forActivity, int* outSequenceCount, int* outTotalWeight);

		/// The number of sequences available for an activity.
		int NumSequencesForActivity(int forActivity);

#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
		inline bool IsInitialized(void) { return m_bIsInitialized; }
#endif

	private:

		/// Allocate my internal array. (It is freed in the destructor.) Also,
		/// build the hash of activities to sequences and populate m_pSequenceTuples.
		void Initialize(CStudioHdr* pstudiohdr);

		/// Force Initialize() to occur again, even if it has already occured.
		void Reinitialize(CStudioHdr* pstudiohdr);

		/// A more efficient version of the old SelectWeightedSequence() function in animation.cpp. 
		int SelectWeightedSequence(CStudioHdr* pstudiohdr, int activity, int curSequence, RandomWeightFunc pRandomWeightFunc);

		// selects the sequence with the most matching modifiers
		//int SelectWeightedSequenceFromModifiers( CStudioHdr *pstudiohdr, int activity, CUtlSymbol *pActivityModifiers, int iModifierCount );

		// Actually a big array, into which the hash values index.
		SequenceTuple* m_pSequenceTuples;
		unsigned int m_iSequenceTuplesCount; // (size of the whole array)
#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
		bool m_bIsInitialized;
#endif

		// we don't store an outer pointer because we can't initialize it at construction time
		// (warning c4355) -- there are ways around this but it's easier to just pass in a 
		// pointer to the CStudioHdr when we need it, since this class isn't supposed to 
		// export its interface outside the studio header anyway.
		// CStudioHdr * const m_pOuter;

		ActivityToValueIdxHash m_ActToSeqHash;

		// we store these so we can know if the contents of the studiohdr have changed
		// from underneath our feet (this is an emergency data integrity check)
		const void* m_expectedPStudioHdr;
		const void* m_expectedVModel;

		// double-check that the data I point to hasn't changed
		bool ValidateAgainst(const CStudioHdr* RESTRICT pstudiohdr) RESTRICT;
		void SetValidationPair(const CStudioHdr* RESTRICT pstudiohdr) RESTRICT;

		friend class CStudioHdr;
	};

	CActivityToSequenceMapping m_ActivityToSequence;

	/// A more efficient version of the old SelectWeightedSequence() function in animation.cpp. 
	/// Returns -1 on failure to find a sequence
	inline int SelectWeightedSequence(int activity, int curSequence, RandomWeightFunc pRandomWeightFunc)
	{
#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
		// We lazy-initialize the header on demand here, because CStudioHdr::Init() is
		// called from the constructor, at which time the this pointer is illegitimate.
		if (!m_ActivityToSequence.IsInitialized())
		{
			m_ActivityToSequence.Initialize(this);
		}
#endif
		return m_ActivityToSequence.SelectWeightedSequence(this, activity, curSequence, pRandomWeightFunc);
	}

	//	inline int SelectWeightedSequenceFromModifiers( int activity, CUtlSymbol *pActivityModifiers, int iModifierCount )
	//	{
	//#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
	//		// We lazy-initialize the header on demand here, because CStudioHdr::Init() is
	//		// called from the constructor, at which time the this pointer is illegitimate.
	//		if ( !m_ActivityToSequence.IsInitialized() )
	//		{
	//			m_ActivityToSequence.Initialize( this );
	//		}
	//#endif
	//		return m_ActivityToSequence.SelectWeightedSequenceFromModifiers( this, activity, pActivityModifiers, iModifierCount );
	//	}

		/// True iff there is at least one sequence for the given activity.
	inline bool HaveSequenceForActivity(int activity)
	{
#if STUDIO_SEQUENCE_ACTIVITY_LAZY_INITIALIZE
		if (!m_ActivityToSequence.IsInitialized())
		{
			m_ActivityToSequence.Initialize(this);
		}
#endif
		return (m_ActivityToSequence.NumSequencesForActivity(activity) > 0);
	}

	// Force this CStudioHdr's activity-to-sequence mapping to be reinitialized
	inline void ReinitializeSequenceMapping(void)
	{
		m_ActivityToSequence.Reinitialize(this);
	}

#ifdef STUDIO_ENABLE_PERF_COUNTERS
public:

	int GetPerfAnimationLayers() const {
		return m_nPerfAnimationLayers;
	}
	int GetPerfAnimatedBones() const {
		return m_nPerfAnimatedBones;
	}
	int GetPerfUsedBones() const {
		return m_nPerfUsedBones;
	}

	void IncPerfAnimationLayers(void) const {
		m_nPerfAnimationLayers++;
	}
	void IncPerfAnimatedBones(void) const {
		m_nPerfAnimatedBones++;
	}
	void IncPerfUsedBones(void) const {
		m_nPerfUsedBones++;
	}
	inline void ClearPerfCounters(void)
	{
		m_nPerfAnimatedBones = 0;
		m_nPerfUsedBones = 0;
		m_nPerfAnimationLayers = 0;
	};

	// timing info
	mutable	int			m_nPerfAnimatedBones;
	mutable	int			m_nPerfUsedBones;
	mutable	int			m_nPerfAnimationLayers;
#endif


};

namespace StudioByteSwap
{
typedef bool (*CompressFunc_t)( const void *pInput, int inputSize, void **pOutput, int *pOutputSize );

//void SetTargetBigEndian( bool bigEndian );
void	ActivateByteSwapping( bool bActivate );
void	SourceIsNative( bool bActivate );
void	SetVerbose( bool bVerbose );
void	SetCollisionInterface( IPhysicsCollision *pPhysicsCollision );

int		ByteswapStudioFile( const char *pFilename, void *pOutBase, const void *pFileBase, int fileSize, studiohdr_t *pHdr, CompressFunc_t pCompressFunc = NULL );
int		ByteswapPHY( void *pOutBase, const void *pFileBase, int fileSize );
int		ByteswapANI( studiohdr_t* pHdr, void *pOutBase, const void *pFileBase, int filesize );
int		ByteswapVVD( void *pOutBase, const void *pFileBase, int fileSize );
int		ByteswapVTX( void *pOutBase, const void *pFileBase, int fileSize );
int		ByteswapMDL( void *pOutBase, const void *pFileBase, int fileSize );

#define BYTESWAP_ALIGNMENT_PADDING		4096
}

#endif // STUDIOBYTESWAP_H