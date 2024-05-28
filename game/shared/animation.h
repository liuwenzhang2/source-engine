//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#ifndef ANIMATION_H
#define ANIMATION_H

struct animevent_t;
struct studiohdr_t;
class IStudioHdr;
struct mstudioseqdesc_t;

int ExtractBbox( IStudioHdr *pstudiohdr, int sequence, Vector& mins, Vector& maxs );

void IndexModelSequences( IStudioHdr *pstudiohdr );
void ResetActivityIndexes( IStudioHdr *pstudiohdr );
void VerifySequenceIndex( IStudioHdr *pstudiohdr );
int SelectWeightedSequence( IStudioHdr *pstudiohdr, int activity, int curSequence = -1 );
int SelectHeaviestSequence( IStudioHdr *pstudiohdr, int activity );
void SetEventIndexForSequence( mstudioseqdesc_t &seqdesc );
void BuildAllAnimationEventIndexes( IStudioHdr *pstudiohdr );
void ResetEventIndexes( IStudioHdr *pstudiohdr );

void GetEyePosition( IStudioHdr *pstudiohdr, Vector &vecEyePosition );

int LookupActivity( IStudioHdr *pstudiohdr, const char *label );
int LookupSequence( IStudioHdr *pstudiohdr, const char *label );

#define NOMOTION 99999
void GetSequenceLinearMotion( IStudioHdr *pstudiohdr, int iSequence, const float poseParameter[], Vector *pVec );

const char *GetSequenceName( IStudioHdr *pstudiohdr, int sequence );
const char *GetSequenceActivityName( IStudioHdr *pstudiohdr, int iSequence );

int GetSequenceFlags( IStudioHdr *pstudiohdr, int sequence );
int GetAnimationEvent( IStudioHdr *pstudiohdr, int sequence, animevent_t *pNPCEvent, float flStart, float flEnd, int index );
bool HasAnimationEventOfType( IStudioHdr *pstudiohdr, int sequence, int type );

int FindTransitionSequence( IStudioHdr *pstudiohdr, int iCurrentSequence, int iGoalSequence, int *piDir );
bool GotoSequence( IStudioHdr *pstudiohdr, int iCurrentSequence, float flCurrentCycle, float flCurrentRate, int iGoalSequence, int &nNextSequence, float &flNextCycle, int &iNextDir );

void SetBodygroup( IStudioHdr *pstudiohdr, int& body, int iGroup, int iValue );
int GetBodygroup( IStudioHdr *pstudiohdr, int body, int iGroup );

const char *GetBodygroupName( IStudioHdr *pstudiohdr, int iGroup );
int FindBodygroupByName( IStudioHdr *pstudiohdr, const char *name );
int GetBodygroupCount( IStudioHdr *pstudiohdr, int iGroup );
int GetNumBodyGroups( IStudioHdr *pstudiohdr );

int GetSequenceActivity( IStudioHdr *pstudiohdr, int sequence, int *pweight = NULL );

void GetAttachmentLocalSpace( IStudioHdr *pstudiohdr, int attachIndex, matrix3x4_t &pLocalToWorld );

float SetBlending( IStudioHdr *pstudiohdr, int sequence, int *pblendings, int iBlender, float flValue );

int FindHitboxSetByName( IStudioHdr *pstudiohdr, const char *name );
const char *GetHitboxSetName( IStudioHdr *pstudiohdr, int setnumber );
int GetHitboxSetCount( IStudioHdr *pstudiohdr );

#endif	//ANIMATION_H
