//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef SEQUENCE_TRANSITIONER_H
#define SEQUENCE_TRANSITIONER_H
#ifdef _WIN32
#pragma once
#endif

class CAnimationData {
	friend class CSequenceTransitioner;
public:

	float GetFadeout(float flCurTime) const
	{
		float s;

		if (m_flLayerFadeOuttime <= 0.0f)
		{
			s = 0;
		}
		else
		{
			// blend in over 0.2 seconds
			s = 1.0 - (flCurTime - m_flLayerAnimtime) / m_flLayerFadeOuttime;
			if (s > 0 && s <= 1.0)
			{
				// do a nice spline curve
				s = 3 * s * s - 2 * s * s * s;
			}
			else if (s > 1.0f)
			{
				// Shouldn't happen, but maybe curtime is behind animtime?
				s = 1.0f;
			}
		}
		return s;
	}

	int		GetSequence() const { return m_nSequence; }
	float	GetCycle() const { return m_flCycle; }
	float	GetPlaybackRate() const { return m_flPlaybackRate; }
	float	GetWeight() const { return m_flWeight; }
	float	GetLayerAnimtime() const { return m_flLayerAnimtime; }
	float	GetLayerFadeOuttime() const { return m_flLayerFadeOuttime; }
private:
	int		m_nSequence;
	float	m_flCycle;
	float	m_flPlaybackRate;
	float	m_flWeight;
	float	m_flLayerAnimtime;
	float	m_flLayerFadeOuttime;
};

// ------------------------------------------------------------------------------------------------ //
// CSequenceTransitioner declaration.
// ------------------------------------------------------------------------------------------------ //
class CSequenceTransitioner
{
public:
	void CheckForSequenceChange( 
		// Describe the current animation state with these parameters.
		IStudioHdr *hdr,
		int nCurSequence, 

		// Even if the sequence hasn't changed, you can force it to interpolate from the previous
		// spot in the same sequence to the current spot in the same sequence by setting this to true.
		bool bForceNewSequence,

		// Follows EF_NOINTERP.
		bool bInterpolate
		);

	void UpdateCurrentSequence(
		// Describe the current animation state with these parameters.
		IStudioHdr *hdr,
		int nCurSequence, 
		float flCurCycle,
		float flCurPlaybackRate,
		float flCurTime
		);

	void RemoveAll( void ) { m_animationQueue.RemoveAll(); };

	int GetAnimationDataCount() { return m_animationQueue.Count(); }
	const CAnimationData& GetAnimationData(int index) { return m_animationQueue[index]; }
private:
	CUtlVector< CAnimationData >	m_animationQueue;
};

#endif // SEQUENCE_TRANSITIONER_H
