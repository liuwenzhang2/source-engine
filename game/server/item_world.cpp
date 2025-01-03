//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Handling for the base world item. Most of this was moved from items.cpp.
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "player.h"
#include "items.h"
#include "gamerules.h"
#include "engine/IEngineSound.h"
#include "game/server/iservervehicle.h"
#include "physics_saverestore.h"
#include "world.h"

#ifdef HL2MP
#include "hl2mp_gamerules.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define ITEM_PICKUP_BOX_BLOAT		24

class CWorldItem : public CBaseAnimating
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWorldItem, CBaseAnimating );

	bool	KeyValue( const char *szKeyName, const char *szValue ); 
	void	Spawn( void );

	int		m_iType;
};

LINK_ENTITY_TO_CLASS(world_items, CWorldItem);

BEGIN_DATADESC( CWorldItem )

DEFINE_FIELD( m_iType, FIELD_INTEGER ),

END_DATADESC()


bool CWorldItem::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "type"))
	{
		m_iType = atoi(szValue);
	}
	else
		return BaseClass::KeyValue( szKeyName, szValue );

	return true;
}

void CWorldItem::Spawn( void )
{
	CBaseEntity *pEntity = NULL;

	switch (m_iType) 
	{
	case 44: // ITEM_BATTERY:
		pEntity = CBaseEntity::Create( "item_battery", GetEngineObject()->GetLocalOrigin(), GetEngineObject()->GetLocalAngles() );
		break;
	case 45: // ITEM_SUIT:
		pEntity = CBaseEntity::Create( "item_suit", GetEngineObject()->GetLocalOrigin(), GetEngineObject()->GetLocalAngles() );
		break;
	}

	if (!pEntity)
	{
		Warning("unable to create world_item %d\n", m_iType );
	}
	else
	{
		pEntity->m_target = m_target;
		pEntity->GetEngineObject()->SetName( STRING(GetEntityName()) );
		pEntity->GetEngineObject()->ClearSpawnFlags();
		pEntity->GetEngineObject()->AddSpawnFlags(GetEngineObject()->GetSpawnFlags() );
	}

	EntityList()->DestroyEntityImmediate( this );
}


BEGIN_DATADESC( CItem )

	DEFINE_FIELD( m_bActivateWhenAtRest,	 FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vOriginalSpawnOrigin, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vOriginalSpawnAngles, FIELD_VECTOR ),
	DEFINE_PHYSPTR( m_pConstraint ),

	// Function Pointers
	DEFINE_TOUCHFUNC( ItemTouch ),
	DEFINE_THINKFUNC( Materialize ),
	DEFINE_THINKFUNC( ComeToRest ),

#if defined( HL2MP ) || defined( TF_DLL )
	DEFINE_FIELD( m_flNextResetCheckTime, FIELD_TIME ),
	DEFINE_THINKFUNC( FallThink ),
#endif

	// Outputs
	DEFINE_OUTPUT( m_OnPlayerTouch, "OnPlayerTouch" ),
	DEFINE_OUTPUT( m_OnCacheInteraction, "OnCacheInteraction" ),

END_DATADESC()


//-----------------------------------------------------------------------------
// Constructor 
//-----------------------------------------------------------------------------
CItem::CItem()
{
	m_bActivateWhenAtRest = false;
}

bool CItem::CreateItemVPhysicsObject( void )
{
	// Create the object in the physics system
	int nSolidFlags = GetEngineObject()->GetSolidFlags() | FSOLID_NOT_STANDABLE;
	if ( !m_bActivateWhenAtRest )
	{
		nSolidFlags |= FSOLID_TRIGGER;
	}

	if (GetEngineObject()->VPhysicsInitNormal( SOLID_VPHYSICS, nSolidFlags, false ) == NULL )
	{
		GetEngineObject()->SetSolid( SOLID_BBOX );
		GetEngineObject()->AddSolidFlags( nSolidFlags );

		// If it's not physical, drop it to the floor
		if (UTIL_DropToFloor(this, MASK_SOLID) == 0)
		{
			Warning( "Item %s fell out of level at %f,%f,%f\n", GetClassname(), GetEngineObject()->GetAbsOrigin().x, GetEngineObject()->GetAbsOrigin().y, GetEngineObject()->GetAbsOrigin().z);
			EntityList()->DestroyEntity( this );
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CItem::Spawn( void )
{
	if ( g_pGameRules->IsAllowedToSpawn( this ) == false )
	{
		EntityList()->DestroyEntity( this );
		return;
	}

	GetEngineObject()->SetMoveType( MOVETYPE_FLYGRAVITY );
	GetEngineObject()->SetSolid( SOLID_BBOX );
	SetBlocksLOS( false );
	GetEngineObject()->AddEFlags( EFL_NO_ROTORWASH_PUSH );
	
	if( IsX360() )
	{
		GetEngineObject()->AddEffects( EF_ITEM_BLINK );
	}

	// This will make them not collide with the player, but will collide
	// against other items + weapons
	GetEngineObject()->SetCollisionGroup( COLLISION_GROUP_WEAPON );
	GetEngineObject()->UseTriggerBounds( true, ITEM_PICKUP_BOX_BLOAT );
	SetTouch(&CItem::ItemTouch);

	if ( CreateItemVPhysicsObject() == false )
		return;

	m_takedamage = DAMAGE_EVENTS_ONLY;

#if !defined( CLIENT_DLL )
	// Constrained start?
	if (GetEngineObject()->HasSpawnFlags( SF_ITEM_START_CONSTRAINED ) )
	{
		//Constrain the weapon in place
		IPhysicsObject *pReferenceObject, *pAttachedObject;

		pReferenceObject = EntityList()->PhysGetWorldObject();
		pAttachedObject = GetEngineObject()->VPhysicsGetObject();

		if ( pReferenceObject && pAttachedObject )
		{
			constraint_fixedparams_t fixed;
			fixed.Defaults();
			fixed.InitWithCurrentObjectState( pReferenceObject, pAttachedObject );

			fixed.constraint.forceLimit	= lbs2kg( 10000 );
			fixed.constraint.torqueLimit = lbs2kg( 10000 );

			m_pConstraint = EntityList()->PhysGetEnv()->CreateFixedConstraint( pReferenceObject, pAttachedObject, NULL, fixed );

			m_pConstraint->SetGameData( (void *) this );
		}
	}
#endif //CLIENT_DLL

#if defined( HL2MP ) || defined( TF_DLL )
	SetThink( &CItem::FallThink );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
#endif
}

unsigned int CItem::PhysicsSolidMaskForEntity( void ) const
{ 
	return BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_PLAYERCLIP;
}

void CItem::Use( IServerEntity *pActivator, IServerEntity *pCaller, USE_TYPE useType, float value )
{
	CBasePlayer *pPlayer = ToBasePlayer( pActivator );

	if ( pPlayer )
	{
		pPlayer->PickupObject( this );
	}
}

extern int gEvilImpulse101;


//-----------------------------------------------------------------------------
// Activate when at rest, but don't allow pickup until then
//-----------------------------------------------------------------------------
void CItem::ActivateWhenAtRest( float flTime /* = 0.5f */ )
{
	GetEngineObject()->RemoveSolidFlags( FSOLID_TRIGGER );
	m_bActivateWhenAtRest = true;
	SetThink( &CItem::ComeToRest );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + flTime );
}


//-----------------------------------------------------------------------------
// Become touchable when we are at rest
//-----------------------------------------------------------------------------
void CItem::OnEntityEvent( EntityEvent_t event, void *pEventData )
{
	BaseClass::OnEntityEvent( event, pEventData );

	switch( event )
	{
	case ENTITY_EVENT_WATER_TOUCH:
		{
			// Delay rest for a sec, to avoid changing collision 
			// properties inside a collision callback.
			SetThink( &CItem::ComeToRest );
			GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );
		}
		break;
	}
}


//-----------------------------------------------------------------------------
// Become touchable when we are at rest
//-----------------------------------------------------------------------------
void CItem::ComeToRest( void )
{
	if ( m_bActivateWhenAtRest )
	{
		m_bActivateWhenAtRest = false;
		GetEngineObject()->AddSolidFlags( FSOLID_TRIGGER );
		SetThink( NULL );
	}
}

#if defined( HL2MP ) || defined( TF_DLL )

//-----------------------------------------------------------------------------
// Purpose: Items that have just spawned run this think to catch them when 
//			they hit the ground. Once we're sure that the object is grounded, 
//			we change its solid type to trigger and set it in a large box that 
//			helps the player get it.
//-----------------------------------------------------------------------------
void CItem::FallThink ( void )
{
	GetEngineObject()->SetNextThink( gpGlobals->curtime + 0.1f );

#if defined( HL2MP )
	bool shouldMaterialize = false;
	IPhysicsObject *pPhysics = GetEngineObject()->VPhysicsGetObject();
	if ( pPhysics )
	{
		shouldMaterialize = pPhysics->IsAsleep();
	}
	else
	{
		shouldMaterialize = (GetEngineObject()->GetFlags() & FL_ONGROUND) ? true : false;
	}

	if ( shouldMaterialize )
	{
		SetThink ( NULL );

		m_vOriginalSpawnOrigin = GetEngineObject()->GetAbsOrigin();
		m_vOriginalSpawnAngles = GetEngineObject()->GetAbsAngles();

		HL2MPRules()->AddLevelDesignerPlacedObject( this );
	}
#endif // HL2MP

#if defined( TF_DLL )
	// We only come here if ActivateWhenAtRest() is never called,
	// which is the case when creating currencypacks in MvM
	if ( !(GetEngineObject()->GetFlags() & FL_ONGROUND ) )
	{
		if ( !GetAbsVelocity().Length() && GetEngineObject()->GetMoveType() == MOVETYPE_FLYGRAVITY )
		{
			// Mr. Game, meet Mr. Hammer.  Mr. Hammer, meet the uncooperative Mr. Physics.
			// Mr. Physics really doesn't want to give our friend the FL_ONGROUND flag.
			// This means our wonderfully helpful radius currency collection code will be sad.
			// So in the name of justice, we ask that this flag be delivered unto him.

			GetEngineObject()->SetMoveType( MOVETYPE_NONE );
			GetEngineObject()->SetGroundEntity( GetWorldEntity()->GetEngineObject());
		}
	}
	else
	{
		SetThink( &CItem::ComeToRest );
	}
#endif // TF
}

#endif // HL2MP, TF

//-----------------------------------------------------------------------------
// Purpose: Used to tell whether an item may be picked up by the player.  This
//			accounts for solid obstructions being in the way.
// Input  : *pItem - item in question
//			*pPlayer - player attempting the pickup
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool UTIL_ItemCanBeTouchedByPlayer( CBaseEntity *pItem, CBasePlayer *pPlayer )
{
	if ( pItem == NULL || pPlayer == NULL )
		return false;

	// For now, always allow a vehicle riding player to pick up things they're driving over
	if ( pPlayer->IsInAVehicle() )
		return true;

	// Get our test positions
	Vector vecStartPos;
	IPhysicsObject *pPhysObj = pItem->GetEngineObject()->VPhysicsGetObject();
	if ( pPhysObj != NULL )
	{
		// Use the physics hull's center
		QAngle vecAngles;
		pPhysObj->GetPosition( &vecStartPos, &vecAngles );
	}
	else
	{
		// Use the generic bbox center
		vecStartPos = pItem->GetEngineObject()->WorldSpaceCenter();
	}

	Vector vecEndPos = pPlayer->EyePosition();

	// FIXME: This is the simple first try solution towards the problem.  We need to take edges and shape more into account
	//		  for this to be fully robust.

	// Trace between to see if we're occluded
	trace_t tr;
	CTraceFilterSkipTwoEntities filter( pPlayer, pItem, COLLISION_GROUP_PLAYER_MOVEMENT );
	UTIL_TraceLine( vecStartPos, vecEndPos, MASK_SOLID, &filter, &tr );

	// Occluded
	// FIXME: For now, we exclude starting in solid because there are cases where this doesn't matter
	if ( tr.fraction < 1.0f )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Whether or not the item can be touched and picked up by the player, taking
//			into account obstructions and other hinderances
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CItem::ItemCanBeTouchedByPlayer( CBasePlayer *pPlayer )
{
	return UTIL_ItemCanBeTouchedByPlayer( this, pPlayer );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pOther - 
//-----------------------------------------------------------------------------
void CItem::ItemTouch( IServerEntity *pOther )
{
	// Vehicles can touch items + pick them up
	if ( pOther->GetServerVehicle() )
	{
		pOther = pOther->GetServerVehicle()->GetPassenger();
		if ( !pOther )
			return;
	}

	// if it's not a player, ignore
	if ( !pOther->IsPlayer() )
		return;

	CBasePlayer *pPlayer = (CBasePlayer *)pOther;

	// Must be a valid pickup scenario (no blocking). Though this is a more expensive
	// check than some that follow, this has to be first Obecause it's the only one
	// that inhibits firing the output OnCacheInteraction.
	if ( ItemCanBeTouchedByPlayer( pPlayer ) == false )
		return;

	m_OnCacheInteraction.FireOutput(pOther, this);

	// Can I even pick stuff up?
	if ( !pPlayer->IsAllowedToPickupWeapons() )
		return;

	// ok, a player is touching this item, but can he have it?
	if ( !g_pGameRules->CanHaveItem( pPlayer, this ) )
	{
		// no? Ignore the touch.
		return;
	}

	if ( MyTouch( pPlayer ) )
	{
		m_OnPlayerTouch.FireOutput(pOther, this);

		SetTouch( NULL );
		SetThink( NULL );

		// player grabbed the item. 
		g_pGameRules->PlayerGotItem( pPlayer, this );
		if ( g_pGameRules->ItemShouldRespawn( this ) == GR_ITEM_RESPAWN_YES )
		{
			Respawn(); 
		}
		else
		{
			EntityList()->DestroyEntity( this );

#ifdef HL2MP
			HL2MPRules()->RemoveLevelDesignerPlacedObject( this );
#endif
		}
	}
	else if (gEvilImpulse101)
	{
		EntityList()->DestroyEntity( this );
	}
}

CBaseEntity* CItem::Respawn( void )
{
	SetTouch( NULL );
	GetEngineObject()->AddEffects( EF_NODRAW );

	GetEngineObject()->VPhysicsDestroyObject();

	GetEngineObject()->SetMoveType( MOVETYPE_NONE );
	GetEngineObject()->SetSolid( SOLID_BBOX );
	GetEngineObject()->AddSolidFlags( FSOLID_TRIGGER );

	UTIL_SetOrigin( this, g_pGameRules->VecItemRespawnSpot( this ) );// blip to whereever you should respawn.
	GetEngineObject()->SetAbsAngles( g_pGameRules->VecItemRespawnAngles( this ) );// set the angles.

#if !defined( TF_DLL )
	UTIL_DropToFloor( this, MASK_SOLID );
#endif

	RemoveAllDecals(); //remove any decals

	SetThink ( &CItem::Materialize );
	GetEngineObject()->SetNextThink( gpGlobals->curtime + g_pGameRules->FlItemRespawnTime( this ) );
	return this;
}

void CItem::Materialize( void )
{
	CreateItemVPhysicsObject();

	if (GetEngineObject()->IsEffectActive( EF_NODRAW ) )
	{
		// changing from invisible state to visible.

#ifdef HL2MP
		const char* soundname = "AlyxEmp.Charge";
		CPASAttenuationFilter filter(this, soundname);

		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = 0.0f;
		params.m_pflSoundDuration = NULL;
		params.m_bWarnOnDirectWaveReference = true;
		g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
#else
		const char* soundname = "Item.Materialize";
		CPASAttenuationFilter filter(this, soundname);

		EmitSound_t params;
		params.m_pSoundName = soundname;
		params.m_flSoundTime = 0.0f;
		params.m_pflSoundDuration = NULL;
		params.m_bWarnOnDirectWaveReference = true;
		g_pSoundEmitterSystem->EmitSound(filter, this->entindex(), params);
#endif
		GetEngineObject()->RemoveEffects( EF_NODRAW );
		GetEngineObject()->DoMuzzleFlash();
	}

	SetTouch( &CItem::ItemTouch );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CItem::Precache()
{
	BaseClass::Precache();

	g_pSoundEmitterSystem->PrecacheScriptSound( "Item.Materialize" );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysGunUser - 
//			PICKED_UP_BY_CANNON - 
//-----------------------------------------------------------------------------
void CItem::OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason )
{
	m_OnCacheInteraction.FireOutput(pPhysGunUser, this);

	if ( reason == PICKED_UP_BY_CANNON )
	{
		// Expand the pickup box
		GetEngineObject()->UseTriggerBounds( true, ITEM_PICKUP_BOX_BLOAT * 2 );

		if( m_pConstraint != NULL )
		{
			EntityList()->PhysGetEnv()->DestroyConstraint( m_pConstraint );
			m_pConstraint = NULL;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysGunUser - 
//			reason - 
//-----------------------------------------------------------------------------
void CItem::OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t reason )
{
	// Restore the pickup box to the original
	GetEngineObject()->UseTriggerBounds( true, ITEM_PICKUP_BOX_BLOAT );
}
