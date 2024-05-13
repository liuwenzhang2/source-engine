//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef PREDICTABLE_ENTITY_H
#define PREDICTABLE_ENTITY_H
#ifdef _WIN32
#pragma once
#endif

// For introspection
#include "tier0/platform.h"
#include "predictioncopy.h"
#include "shared_classnames.h"

#ifndef NO_ENTITY_PREDICTION
#define UsePrediction() 1
#else
#define UsePrediction() 0
#endif

// CLIENT DLL includes
#if defined( CLIENT_DLL )

#include "iclassmap.h"
#include "recvproxy.h"

class SendTable;

// Game DLL includes
#else

#include "sendproxy.h"

#endif  // !CLIENT_DLL

//#if defined( CLIENT_DLL )

//#else

//#endif

//#if defined( CLIENT_DLL )

// On the client .dll this creates a mapping between a classname and
//  a client side class.  Probably could be templatized at some point.

//#define LINK_ENTITY_TO_CLASS( localName, className )						\
//	static C_BaseEntity *C##className##Factory( void )						\
//	{																		\
//		return static_cast< C_BaseEntity * >( new className );				\
//	};																		\
//	class C##localName##Foo													\
//	{																		\
//	public:																	\
//		C##localName##Foo( void )											\
//		{																	\
//			GetClassMap().Add( #localName, #className, sizeof( className ),	\
//				&C##className##Factory );									\
//		}																	\
//	};																		\
//	static C##localName##Foo g_C##localName##Foo;

//#else

//#endif																	



#endif // PREDICTABLE_ENTITY_H
