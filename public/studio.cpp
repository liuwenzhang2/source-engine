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

static ConVar mod_load_showstall( "mod_load_showstall", "0", 0, "1 - show hitches , 2 - show stalls" );
mstudioanim_t *mstudioanimdesc_t::pAnim(const IStudioHdr* pStudioHdr, int *piFrame ) const
{
	float flStall;
	return pAnim(pStudioHdr, piFrame, flStall );
}

mstudioanim_t *mstudioanimdesc_t::pAnim(const IStudioHdr* pStudioHdr, int *piFrame, float &flStall ) const
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

	panim = pAnimBlock(pStudioHdr, block, index );

	// force a preload on the next block
	if ( sectionframes != 0 )
	{
		int count = ( numframes / sectionframes) + 2;
		for ( int i = section + 1; i < count; i++ )
		{
			if ( pSection( i )->animblock != block )
			{
				pAnimBlock(pStudioHdr, pSection( i )->animblock, pSection( i )->animindex );
				break;
			}
		}
	}

	if (panim == NULL)
	{
		if (section > 0 && mod_load_showstall.GetInt() > 0)
		{
			if (pStudiohdr() != pStudioHdr->GetRenderHdr()) {
				//Error("error");
			}
			const IStudioHdr* pRealStudioHdr = pStudioHdr->RealStudioHdr(pStudiohdr());
			Msg("[%8.3f] hitch on %s:%s:%d:%d\n", Plat_FloatTime(), pRealStudioHdr->pszName(), pszName(), section, block );
			pStudioHdr->FreeRealStudioHdr(pRealStudioHdr);
		}
		// back up until a previously loaded block is found
		while (--section >= 0)
		{
			block = pSection( section )->animblock;
			index = pSection( section )->animindex;
			panim = pAnimBlock(pStudioHdr, block, index );
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
			if (pStudiohdr() != pStudioHdr->GetRenderHdr()) {
				//Error("error");
			}
			const IStudioHdr* pRealStudioHdr = pStudioHdr->RealStudioHdr(pStudiohdr());
			Msg("[%8.3f] stall blend %.2f on %s:%s:%d:%d\n", Plat_FloatTime(), flStall, pRealStudioHdr->pszName(), pszName(), section, block );
			pStudioHdr->FreeRealStudioHdr(pRealStudioHdr);
		}
	}

	if (panim == NULL && mod_load_showstall.GetInt() > 1)
	{
		if (pStudiohdr() != pStudioHdr->GetRenderHdr()) {
			//Error("error");
		}
		const IStudioHdr* pRealStudioHdr = pStudioHdr->RealStudioHdr(pStudiohdr());
		Msg("[%8.3f] stall on %s:%s:%d:%d\n", Plat_FloatTime(), pRealStudioHdr->pszName(), pszName(), section, block );
		pStudioHdr->FreeRealStudioHdr(pRealStudioHdr);
	}

	return panim;
}

mstudioikrule_t *mstudioanimdesc_t::pIKRule(const IStudioHdr* pStudioHdr, int i ) const
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
			if (pStudiohdr() != pStudioHdr->GetRenderHdr()) {
				//Error("error");
			}
			const IStudioHdr* pRealStudioHdr = pStudioHdr->RealStudioHdr(pStudiohdr());
			byte *pAnimBlocks = pRealStudioHdr->GetAnimBlock( animblock );
			pStudioHdr->FreeRealStudioHdr(pRealStudioHdr);
			if ( pAnimBlocks )
			{
				return (mstudioikrule_t *)(pAnimBlocks + animblockikruleindex) + i;
			}
		}
	}

	return NULL;
}


mstudiolocalhierarchy_t *mstudioanimdesc_t::pHierarchy(const IStudioHdr* pStudioHdr, int i ) const
{
	if (localhierarchyindex)
	{
		if (animblock == 0)
		{
			return  (mstudiolocalhierarchy_t *)(((byte *)this) + localhierarchyindex) + i;
		}
		else
		{
			if (pStudiohdr() != pStudioHdr->GetRenderHdr()) {
				//Error("error");
			}
			const IStudioHdr* pRealStudioHdr = pStudioHdr->RealStudioHdr(pStudiohdr());
			byte *pAnimBlocks = pRealStudioHdr->GetAnimBlock( animblock );
			pStudioHdr->FreeRealStudioHdr(pRealStudioHdr);
			if ( pAnimBlocks )
			{
				return (mstudiolocalhierarchy_t *)(pAnimBlocks + localhierarchyindex) + i;
			}
		}
	}

	return NULL;
}

mstudioanim_t* mstudioanimdesc_t::pAnimBlock(const IStudioHdr* pStudioHdr, int block, int index) const
{
	if (block == -1)
	{
		return (mstudioanim_t*)NULL;
	}
	if (block == 0)
	{
		return (mstudioanim_t*)(((byte*)this) + index);
	}
	if (pStudiohdr() != pStudioHdr->GetRenderHdr()) {
		//Error("error");
	}
	const IStudioHdr* pRealStudioHdr = pStudioHdr->RealStudioHdr(pStudiohdr());
	byte* pAnimBlock = pRealStudioHdr->GetAnimBlock(block);
	pStudioHdr->FreeRealStudioHdr(pRealStudioHdr);
	if (pAnimBlock)
	{
		return (mstudioanim_t*)(pAnimBlock + index);
	}

	return (mstudioanim_t*)NULL;
}








