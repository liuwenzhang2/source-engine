//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include "studio.h"
#include "datacache/idatacache.h"
#include "datacache/imdlcache.h"
#include "convar.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

mstudioanimdesc_t &studiohdr_t::pAnimdesc( int i ) const
{ 
	if (numincludemodels == 0)
	{
		return *pLocalAnimdesc( i );
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t* pGroup = pVModel->pAnimGroup(i);// &pVModel->m_group[pVModel->m_anim[i].group];
	const studiohdr_t *pStudioHdr = pGroup->GetGroupStudioHdr();
	Assert( pStudioHdr );

	return *pStudioHdr->pLocalAnimdesc( pVModel->pAnimIndex(i) );//m_anim[i].index
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

mstudioanim_t *mstudioanimdesc_t::pAnimBlock( int block, int index ) const
{
	if (block == -1)
	{
		return (mstudioanim_t *)NULL;
	}
	if (block == 0)
	{
		return (mstudioanim_t *)(((byte *)this) + index);
	}

	byte *pAnimBlock = pStudiohdr()->GetAnimBlock( block );
	if ( pAnimBlock )
	{
		return (mstudioanim_t *)(pAnimBlock + index);
	}

	return (mstudioanim_t *)NULL;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

static ConVar mod_load_showstall( "mod_load_showstall", "0", 0, "1 - show hitches , 2 - show stalls" );
mstudioanim_t *mstudioanimdesc_t::pAnim( int *piFrame ) const
{
	float flStall;
	return pAnim( piFrame, flStall );
}

mstudioanim_t *mstudioanimdesc_t::pAnim( int *piFrame, float &flStall ) const
{
	mstudioanim_t *panim = NULL;

	int block = animblock;
	int index = animindex;
	int section = 0;

	if (sectionframes != 0)
	{
		if (numframes > sectionframes && *piFrame == numframes - 1)
		{
			// last frame on long anims is stored separately
			*piFrame = 0;
			section = (numframes / sectionframes) + 1;
		}
		else
		{
			section = *piFrame / sectionframes;
			*piFrame -= section * sectionframes;
		}

		block = pSection( section )->animblock;
		index = pSection( section )->animindex;
	}

	if (block == -1)
	{
		// model needs to be recompiled
		return NULL;
	}

	panim = pAnimBlock( block, index );

	// force a preload on the next block
	if ( sectionframes != 0 )
	{
		int count = ( numframes / sectionframes) + 2;
		for ( int i = section + 1; i < count; i++ )
		{
			if ( pSection( i )->animblock != block )
			{
				pAnimBlock( pSection( i )->animblock, pSection( i )->animindex );
				break;
			}
		}
	}

	if (panim == NULL)
	{
		if (section > 0 && mod_load_showstall.GetInt() > 0)
		{
			Msg("[%8.3f] hitch on %s:%s:%d:%d\n", Plat_FloatTime(), pStudiohdr()->pszName(), pszName(), section, block );
		}
		// back up until a previously loaded block is found
		while (--section >= 0)
		{
			block = pSection( section )->animblock;
			index = pSection( section )->animindex;
			panim = pAnimBlock( block, index );
			if (panim)
			{
				// set it to the last frame in the last valid section
				*piFrame = sectionframes - 1;
				break;
			}
		}
	}

	// try to guess a valid stall time interval (tuned for the X360)
	flStall = 0.0f;
	if (panim == NULL && section <= 0)
	{
		zeroframestalltime = Plat_FloatTime();
		flStall = 1.0f;
	}
	else if (panim != NULL && zeroframestalltime != 0.0f)
	{
		float dt = Plat_FloatTime() - zeroframestalltime;
		if (dt >= 0.0)
		{
			flStall = SimpleSpline( clamp( (0.200f - dt) * 5.0f, 0.0f, 1.0f ) );
		}

		if (flStall == 0.0f)
		{
			// disable stalltime
			zeroframestalltime = 0.0f;
		}
		else if (mod_load_showstall.GetInt() > 1)
		{
			Msg("[%8.3f] stall blend %.2f on %s:%s:%d:%d\n", Plat_FloatTime(), flStall, pStudiohdr()->pszName(), pszName(), section, block );
		}
	}

	if (panim == NULL && mod_load_showstall.GetInt() > 1)
	{
		Msg("[%8.3f] stall on %s:%s:%d:%d\n", Plat_FloatTime(), pStudiohdr()->pszName(), pszName(), section, block );
	}

	return panim;
}

mstudioikrule_t *mstudioanimdesc_t::pIKRule( int i ) const
{
	if (ikruleindex)
	{
		return (mstudioikrule_t *)(((byte *)this) + ikruleindex) + i;
	}
	else if (animblockikruleindex)
	{
		if (animblock == 0)
		{
			return  (mstudioikrule_t *)(((byte *)this) + animblockikruleindex) + i;
		}
		else
		{
			byte *pAnimBlocks = pStudiohdr()->GetAnimBlock( animblock );
			
			if ( pAnimBlocks )
			{
				return (mstudioikrule_t *)(pAnimBlocks + animblockikruleindex) + i;
			}
		}
	}

	return NULL;
}


mstudiolocalhierarchy_t *mstudioanimdesc_t::pHierarchy( int i ) const
{
	if (localhierarchyindex)
	{
		if (animblock == 0)
		{
			return  (mstudiolocalhierarchy_t *)(((byte *)this) + localhierarchyindex) + i;
		}
		else
		{
			byte *pAnimBlocks = pStudiohdr()->GetAnimBlock( animblock );
			
			if ( pAnimBlocks )
			{
				return (mstudiolocalhierarchy_t *)(pAnimBlocks + localhierarchyindex) + i;
			}
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

bool studiohdr_t::SequencesAvailable() const
{
	if (numincludemodels == 0)
	{
		return true;
	}

	return ( GetVirtualModel() != NULL );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::GetNumSeq( void ) const
{
	if (numincludemodels == 0)
	{
		return numlocalseq;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );
	return pVModel->NumSeq();// m_seq.Count();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

mstudioseqdesc_t &studiohdr_t::pSeqdesc( int i ) const
{
	if (numincludemodels == 0)
	{
		return *pLocalSeqdesc( i );
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	if ( !pVModel )
	{
		return *pLocalSeqdesc( i );
	}

	virtualgroup_t* pGroup = pVModel->pSeqGroup(i);// &pVModel->m_group[pVModel->m_seq[i].group];
	const studiohdr_t *pStudioHdr = pGroup->GetGroupStudioHdr();
	Assert( pStudioHdr );

	return *pStudioHdr->pLocalSeqdesc( pVModel->pSeqIndex(i) );// m_seq[i].index
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::iRelativeAnim( int baseseq, int relanim ) const
{
	if (numincludemodels == 0)
	{
		return relanim;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t* pGroup = pVModel->pSeqGroup(baseseq);// & pVModel->m_group[pVModel->m_seq[baseseq].group];

	return pGroup->masterAnim[ relanim ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::iRelativeSeq( int baseseq, int relseq ) const
{
	if (numincludemodels == 0)
	{
		return relseq;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t* pGroup = pVModel->pSeqGroup(baseseq);// &pVModel->m_group[pVModel->m_seq[baseseq].group];

	return pGroup->masterSeq[ relseq ];
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int	studiohdr_t::GetNumPoseParameters( void ) const
{
	if (numincludemodels == 0)
	{
		return numlocalposeparameters;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	return pVModel->NumPose();// m_pose.Count();
}



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

const mstudioposeparamdesc_t &studiohdr_t::pPoseParameter( int i )
{
	if (numincludemodels == 0)
	{
		return *pLocalPoseParameter( i );
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	if ( pVModel->nPoseGroup(i) == 0)// m_pose[i].group
		return *pLocalPoseParameter( pVModel->pPoseIndex(i) );// m_pose[i].index

	virtualgroup_t* pGroup = pVModel->pPoseGroup(i);// &pVModel->m_group[pVModel->m_pose[i].group];

	const studiohdr_t *pStudioHdr = pGroup->GetGroupStudioHdr();
	Assert( pStudioHdr );

	return *pStudioHdr->pLocalPoseParameter( pVModel->pPoseIndex(i) );// m_pose[i].index
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::GetSharedPoseParameter( int iSequence, int iLocalPose ) const
{
	if (numincludemodels == 0)
	{
		return iLocalPose;
	}

	if (iLocalPose == -1)
		return iLocalPose;

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t* pGroup = pVModel->pSeqGroup(iSequence);// &pVModel->m_group[pVModel->m_seq[iSequence].group];

	return pGroup->masterPose[iLocalPose];
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::EntryNode( int iSequence )
{
	mstudioseqdesc_t &seqdesc = pSeqdesc( iSequence );

	if (numincludemodels == 0 || seqdesc.localentrynode == 0)
	{
		return seqdesc.localentrynode;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t* pGroup = pVModel->pSeqGroup(iSequence);// &pVModel->m_group[pVModel->m_seq[iSequence].group];

	return pGroup->masterNode[seqdesc.localentrynode-1]+1;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


int studiohdr_t::ExitNode( int iSequence )
{
	mstudioseqdesc_t &seqdesc = pSeqdesc( iSequence );

	if (numincludemodels == 0 || seqdesc.localexitnode == 0)
	{
		return seqdesc.localexitnode;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t* pGroup = pVModel->pSeqGroup(iSequence);// &pVModel->m_group[pVModel->m_seq[iSequence].group];

	return pGroup->masterNode[seqdesc.localexitnode-1]+1;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int	studiohdr_t::GetNumAttachments( void ) const
{
	if (numincludemodels == 0)
	{
		return numlocalattachments;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	return pVModel->NumAttachment();// m_attachment.Count();
}



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

const mstudioattachment_t &studiohdr_t::pAttachment( int i ) const
{
	if (numincludemodels == 0)
	{
		return *pLocalAttachment( i );
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t* pGroup = pVModel->pAttachmentGroup(i);// &pVModel->m_group[pVModel->m_attachment[i].group];
	const studiohdr_t *pStudioHdr = pGroup->GetGroupStudioHdr();
	Assert( pStudioHdr );

	return *pStudioHdr->pLocalAttachment( pVModel->pAttachmentIndex(i) );// m_attachment[i].index
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int	studiohdr_t::GetAttachmentBone( int i )
{
	const mstudioattachment_t &attachment = pAttachment( i );

	// remap bone
	IVirtualModel *pVModel = GetVirtualModel();
	if (pVModel)
	{
		virtualgroup_t* pGroup = pVModel->pAttachmentGroup(i);// &pVModel->m_group[pVModel->m_attachment[i].group];
		int iBone = pGroup->masterBone[attachment.localbone];
		if (iBone == -1)
			return 0;
		return iBone;
	}
	return attachment.localbone;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void studiohdr_t::SetAttachmentBone( int iAttachment, int iBone )
{
	mstudioattachment_t &attachment = (mstudioattachment_t &)pAttachment( iAttachment );

	// remap bone
	IVirtualModel *pVModel = GetVirtualModel();
	if (pVModel)
	{
		virtualgroup_t* pGroup = pVModel->pAttachmentGroup(iAttachment);// &pVModel->m_group[pVModel->m_attachment[iAttachment].group];
		iBone = pGroup->boneMap[iBone];
	}
	attachment.localbone = iBone;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

char *studiohdr_t::pszNodeName( int iNode )
{
	if (numincludemodels == 0)
	{
		return pszLocalNodeName( iNode );
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	if ( pVModel->NumNode() <= iNode-1 )// m_node.Count()
		return "Invalid node";
	//pVModel->m_group[ pVModel->m_node[iNode-1].group ]
	return pVModel->pNodeGroup(iNode - 1)->GetGroupStudioHdr()->pszLocalNodeName(pVModel->pNodeIndex(iNode - 1));// m_node[iNode - 1].index
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::GetTransition( int iFrom, int iTo ) const
{
	if (numincludemodels == 0)
	{
		return *pLocalTransition( (iFrom-1)*numlocalnodes + (iTo - 1) );
	}

	return iTo;
	/*
	FIXME: not connected
	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	return pVModel->m_transition.Element( iFrom ).Element( iTo );
	*/
}


int	studiohdr_t::GetActivityListVersion( void )
{
	if (numincludemodels == 0)
	{
		return activitylistversion;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	int ActVersion = activitylistversion;

	int i;
	for (i = 1; i < pVModel->NumGroup(); i++)//m_group.Count()
	{
		virtualgroup_t* pGroup = pVModel->pGroup(i);// &pVModel->m_group[i];
		const studiohdr_t *pStudioHdr = pGroup->GetGroupStudioHdr();

		Assert( pStudioHdr );

		ActVersion = min( ActVersion, pStudioHdr->activitylistversion );
	}

	return ActVersion;
}

void studiohdr_t::SetActivityListVersion( int ActVersion ) const
{
	activitylistversion = ActVersion;

	if (numincludemodels == 0)
	{
		return;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	int i;
	for (i = 1; i < pVModel->NumGroup(); i++)//m_group.Count()
	{
		virtualgroup_t* pGroup = pVModel->pGroup(i);// &pVModel->m_group[i];
		const studiohdr_t *pStudioHdr = pGroup->GetGroupStudioHdr();

		Assert( pStudioHdr );

		pStudioHdr->SetActivityListVersion( ActVersion );
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


int studiohdr_t::GetNumIKAutoplayLocks( void ) const
{
	if (numincludemodels == 0)
	{
		return numlocalikautoplaylocks;
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	return pVModel->NumIKlock();// m_iklock.Count();
}

const mstudioiklock_t &studiohdr_t::pIKAutoplayLock( int i )
{
	if (numincludemodels == 0)
	{
		return *pLocalIKAutoplayLock( i );
	}

	IVirtualModel *pVModel = (IVirtualModel *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t* pGroup = pVModel->pIKlockGroup(i);// &pVModel->m_group[pVModel->m_iklock[i].group];
	const studiohdr_t *pStudioHdr = pGroup->GetGroupStudioHdr();
	Assert( pStudioHdr );

	return *pStudioHdr->pLocalIKAutoplayLock( pVModel->pIKlockIndex(i) );// m_iklock[i].index
}

int	studiohdr_t::CountAutoplaySequences() const
{
	int count = 0;
	for (int i = 0; i < GetNumSeq(); i++)
	{
		mstudioseqdesc_t &seqdesc = pSeqdesc( i );
		if (seqdesc.flags & STUDIO_AUTOPLAY)
		{
			count++;
		}
	}
	return count;
}

int	studiohdr_t::CopyAutoplaySequences( unsigned short *pOut, int outCount ) const
{
	int outIndex = 0;
	for (int i = 0; i < GetNumSeq() && outIndex < outCount; i++)
	{
		mstudioseqdesc_t &seqdesc = pSeqdesc( i );
		if (seqdesc.flags & STUDIO_AUTOPLAY)
		{
			pOut[outIndex] = i;
			outIndex++;
		}
	}
	return outIndex;
}

//-----------------------------------------------------------------------------
// Purpose:	maps local sequence bone to global bone
//-----------------------------------------------------------------------------

int	studiohdr_t::RemapSeqBone( int iSequence, int iLocalBone ) const	
{
	// remap bone
	IVirtualModel *pVModel = GetVirtualModel();
	if (pVModel)
	{
		const virtualgroup_t *pSeqGroup = pVModel->pSeqGroup( iSequence );
		return pSeqGroup->masterBone[iLocalBone];
	}
	return iLocalBone;
}

int	studiohdr_t::RemapAnimBone( int iAnim, int iLocalBone ) const
{
	// remap bone
	IVirtualModel *pVModel = GetVirtualModel();
	if (pVModel)
	{
		const virtualgroup_t *pAnimGroup = pVModel->pAnimGroup( iAnim );
		return pAnimGroup->masterBone[iLocalBone];
	}
	return iLocalBone;
}






