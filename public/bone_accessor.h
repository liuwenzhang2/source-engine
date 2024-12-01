//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef BONE_ACCESSOR_H
#define BONE_ACCESSOR_H
#ifdef _WIN32
#pragma once
#endif


#include "studio.h"


class IEngineObjectClient;
class IEngineObjectServer;


class CBoneAccessor : public IBoneAccessor
{
public:
	
	CBoneAccessor();
	CBoneAccessor( matrix3x4_t *pBones ); // This can be used to allow access to all bones.
	
	// Initialize.
#if defined( CLIENT_DLL )
	void Init( const IEngineObjectClient *pAnimating, matrix3x4_t *pBones );
#endif
#ifdef GAME_DLL
	void Init(const IEngineObjectServer* pAnimating, matrix3x4_t* pBones);
#endif // GAME_DLL

	
	int GetReadableBones();
	void SetReadableBones( int flags );

	int GetWritableBones();
	void SetWritableBones( int flags );

	// Get bones for read or write access.
	const matrix3x4_t&	GetBone( int iBone ) const;
	const matrix3x4_t&	operator[]( int iBone ) const;
	matrix3x4_t&		GetBoneForWrite( int iBone );

	matrix3x4_t			*GetBoneArrayForWrite( ) const;

private:

#if defined( CLIENT_DLL ) && defined( _DEBUG )
	void SanityCheckBone( int iBone, bool bReadable ) const;
#endif

#if defined( CLIENT_DLL ) 
	// Only used in the client DLL for debug verification.
	const IEngineObjectClient *m_pAnimating;
#endif
#ifdef GAME_DLL
	const IEngineObjectServer* m_pAnimating;
#endif // GAME_DLL

	matrix3x4_t *m_pBones;

	int m_ReadableBones;		// Which bones can be read.
	int m_WritableBones;		// Which bones can be written.
};


inline CBoneAccessor::CBoneAccessor()
{
#if defined( CLIENT_DLL ) || defined( GAME_DLL )
	m_pAnimating = NULL;
#endif
	m_pBones = NULL;
	m_ReadableBones = m_WritableBones = 0;
}

inline CBoneAccessor::CBoneAccessor( matrix3x4_t *pBones )
{
#if defined( CLIENT_DLL ) || defined( GAME_DLL )
	m_pAnimating = NULL;
#endif
	m_pBones = pBones;
}

#if defined( CLIENT_DLL )
	inline void CBoneAccessor::Init( const IEngineObjectClient *pAnimating, matrix3x4_t *pBones )
	{
		m_pAnimating = pAnimating;
		m_pBones = pBones;
	}
#endif

#ifdef GAME_DLL
	inline void CBoneAccessor::Init(const IEngineObjectServer* pAnimating, matrix3x4_t* pBones)
	{
		m_pAnimating = pAnimating;
		m_pBones = pBones;
	}
#endif // GAME_DLL


inline int CBoneAccessor::GetReadableBones()
{
	return m_ReadableBones;
}

inline void CBoneAccessor::SetReadableBones( int flags )
{
	m_ReadableBones = flags;
}

inline int CBoneAccessor::GetWritableBones()
{
	return m_WritableBones;
}

inline void CBoneAccessor::SetWritableBones( int flags )
{
	m_WritableBones = flags;
}

inline const matrix3x4_t& CBoneAccessor::GetBone( int iBone ) const
{
#if defined( CLIENT_DLL ) && defined( _DEBUG )
	SanityCheckBone( iBone, true );
#endif
	return m_pBones[iBone];
}

inline const matrix3x4_t& CBoneAccessor::operator[]( int iBone ) const
{
#if defined( CLIENT_DLL ) && defined( _DEBUG )
	SanityCheckBone( iBone, true );
#endif
	return m_pBones[iBone];
}

inline matrix3x4_t& CBoneAccessor::GetBoneForWrite( int iBone )
{
#if defined( CLIENT_DLL ) && defined( _DEBUG )
	SanityCheckBone( iBone, false );
#endif
	return m_pBones[iBone];
}

inline matrix3x4_t *CBoneAccessor::GetBoneArrayForWrite( void ) const
{
	return m_pBones;
}

#endif // BONE_ACCESSOR_H
