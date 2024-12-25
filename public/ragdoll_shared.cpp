//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

//#include "cbase.h"
#include "ragdoll_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CRagdollCollisionRules : public IVPhysicsKeyHandler
{
public:
	CRagdollCollisionRules( IPhysicsCollisionSet *pSet )
	{
		m_pSet = pSet;
		m_bSelfCollisions = true;
	}
	virtual void ParseKeyValue( void *pData, const char *pKey, const char *pValue )
	{
		if ( !strcmpi( pKey, "selfcollisions" ) )
		{
			// keys disabled by default
			Assert( atoi(pValue) == 0 );
			m_bSelfCollisions = false;
		}
		else if ( !strcmpi( pKey, "collisionpair" ) )
		{
			if ( m_bSelfCollisions )
			{
				char szToken[256];
				const char *pStr = nexttoken(szToken, pValue, ',');
				int index0 = atoi(szToken);
				nexttoken( szToken, pStr, ',' );
				int index1 = atoi(szToken);

				m_pSet->EnableCollisions( index0, index1 );
			}
			else
			{
				Assert(0);
			}
		}
	}
	virtual void SetDefaults( void *pData ) {}

private:
	IPhysicsCollisionSet *m_pSet;
	bool				m_bSelfCollisions;
};

class CRagdollAnimatedFriction : public IVPhysicsKeyHandler
{
public:
	CRagdollAnimatedFriction( ragdoll_t *ragdoll )
	{
		m_ragdoll = ragdoll;
	}
	virtual void ParseKeyValue( void *pData, const char *pKey, const char *pValue )
	{
		if ( !strcmpi( pKey, "animfrictionmin" ) )
		{
			m_ragdoll->animfriction.iMinAnimatedFriction = atoi( pValue );
		}
		else if ( !strcmpi( pKey, "animfrictionmax" ) )
		{
			m_ragdoll->animfriction.iMaxAnimatedFriction = atoi( pValue );
		}
		else if ( !strcmpi( pKey, "animfrictiontimein" ) )
		{
			m_ragdoll->animfriction.flFrictionTimeIn = atof( pValue );
		}
		else if ( !strcmpi( pKey, "animfrictiontimeout" ) )
		{
			m_ragdoll->animfriction.flFrictionTimeOut = atof( pValue );
		}
		else if ( !strcmpi( pKey, "animfrictiontimehold" ) )
		{
			m_ragdoll->animfriction.flFrictionTimeHold = atof( pValue );
		}
	}

	virtual void SetDefaults( void *pData ) {}

private:
	ragdoll_t *m_ragdoll;
};

void RagdollSetupAnimatedFriction( IPhysicsEnvironment *pPhysEnv, ragdoll_t *ragdoll, int iModelIndex )
{
	vcollide_t* pCollide = modelinfo->GetVCollide( iModelIndex );

	if ( pCollide )
	{
		IVPhysicsKeyParser *pParse = EntityList()->PhysGetCollision()->VPhysicsKeyParserCreate( pCollide->pKeyValues );

		while ( !pParse->Finished() )
		{
			const char *pBlock = pParse->GetCurrentBlockName();

			if ( !strcmpi( pBlock, "animatedfriction") ) 
			{
				CRagdollAnimatedFriction friction( ragdoll );
				pParse->ParseCustom( (void*)&friction, &friction );
			}
			else
			{
				pParse->SkipBlock();
			}
		}

		EntityList()->PhysGetCollision()->VPhysicsKeyParserDestroy( pParse );
	}
}

static void RagdollAddSolid( IPhysicsEnvironment *pPhysEnv, ragdoll_t &ragdoll, const ragdollparams_t &params, solid_t &solid )
{
	if ( solid.index >= 0 && solid.index < params.pCollide->solidCount)
	{
		Assert( ragdoll.listCount == solid.index );
		int boneIndex = params.pStudioHdr->Studio_BoneIndexByName( solid.name );
		ragdoll.boneIndex[ragdoll.listCount] = boneIndex;

		if ( boneIndex >= 0 )
		{
			if ( params.fixedConstraints )
			{
				solid.params.mass = 1000.f;
			}

			solid.params.rotInertiaLimit = 0.1;
			solid.params.pGameData = params.pGameData;
			int surfaceData = EntityList()->PhysGetProps()->GetSurfaceIndex( solid.surfaceprop );

			if ( surfaceData < 0 )
				surfaceData = EntityList()->PhysGetProps()->GetSurfaceIndex( "default" );

			solid.params.pName = params.pStudioHdr->pszName();
			ragdoll.list[ragdoll.listCount].pObject = pPhysEnv->CreatePolyObject( params.pCollide->solids[solid.index], surfaceData, vec3_origin, vec3_angle, &solid.params );
			ragdoll.list[ragdoll.listCount].pObject->SetPositionMatrix( params.pCurrentBones[boneIndex], true );
			ragdoll.list[ragdoll.listCount].parentIndex = -1;
			ragdoll.list[ragdoll.listCount].pObject->SetGameIndex( ragdoll.listCount );

			ragdoll.listCount++;
		}
		else
		{
			Msg( "CRagdollProp::CreateObjects:  Couldn't Lookup Bone %s\n", solid.name );
		}
	}
}


static void RagdollAddConstraint( IPhysicsEnvironment *pPhysEnv, ragdoll_t &ragdoll, const ragdollparams_t &params, constraint_ragdollparams_t &constraint )
{
	if( constraint.childIndex == constraint.parentIndex )
	{
		DevMsg( 1, "Bogus constraint on ragdoll %s\n", params.pStudioHdr->pszName() );
		constraint.childIndex = -1;
		constraint.parentIndex = -1;
	}
	if ( constraint.childIndex >= 0 && constraint.parentIndex >= 0 )
	{
		Assert(constraint.childIndex<ragdoll.listCount);


		ragdollelement_t &childElement = ragdoll.list[constraint.childIndex];
		// save parent index
		childElement.parentIndex = constraint.parentIndex;
	
		if ( params.jointFrictionScale > 0 )
		{
			for ( int k = 0; k < 3; k++ )
			{
				constraint.axes[k].torque *= params.jointFrictionScale;
			}
		}
		// this parent/child pair is not usually a parent/child pair in the skeleton.  There
		// are often bones in between that are collapsed for simulation.  So we need to compute
		// the transform.
		params.pStudioHdr->Studio_CalcBoneToBoneTransform( ragdoll.boneIndex[constraint.childIndex], ragdoll.boneIndex[constraint.parentIndex], constraint.constraintToAttached );
		MatrixGetColumn( constraint.constraintToAttached, 3, childElement.originParentSpace );
		// UNDONE: We could transform the constraint limit axes relative to the bone space
		// using this data.  Do we need that feature?
		SetIdentityMatrix( constraint.constraintToReference );
		if ( params.fixedConstraints )
		{
			// Makes the ragdoll a statue...
			constraint_fixedparams_t fixed;
			fixed.Defaults();
			fixed.InitWithCurrentObjectState( childElement.pObject, ragdoll.list[constraint.parentIndex].pObject );
			fixed.constraint.Defaults();
			childElement.pConstraint = pPhysEnv->CreateFixedConstraint( childElement.pObject, ragdoll.list[constraint.parentIndex].pObject, ragdoll.pGroup, fixed );
		}
		else
		{
			childElement.pConstraint = pPhysEnv->CreateRagdollConstraint( childElement.pObject, ragdoll.list[constraint.parentIndex].pObject, ragdoll.pGroup, constraint );
		}
	}
}


static void RagdollCreateObjects( IPhysicsEnvironment *pPhysEnv, ragdoll_t &ragdoll, const ragdollparams_t &params )
{
	ragdoll.listCount = 0;
	ragdoll.pGroup = NULL;
	ragdoll.allowStretch = params.allowStretch;
	memset( ragdoll.list, 0, sizeof(ragdoll.list) );
	memset( &ragdoll.animfriction, 0, sizeof(ragdoll.animfriction) );
	
	if ( !params.pCollide || params.pCollide->solidCount > RAGDOLL_MAX_ELEMENTS )
		return;

	constraint_groupparams_t group;
	group.Defaults();
	ragdoll.pGroup = pPhysEnv->CreateConstraintGroup( group );
 
	IVPhysicsKeyParser *pParse = EntityList()->PhysGetCollision()->VPhysicsKeyParserCreate( params.pCollide->pKeyValues );
	while ( !pParse->Finished() )
	{
		const char *pBlock = pParse->GetCurrentBlockName();
		if ( !strcmpi( pBlock, "solid" ) )
		{
			solid_t solid;

			pParse->ParseSolid( &solid, g_pSolidSetup);
			RagdollAddSolid( pPhysEnv, ragdoll, params, solid );
		}
		else if ( !strcmpi( pBlock, "ragdollconstraint" ) )
		{
			constraint_ragdollparams_t constraint;
			pParse->ParseRagdollConstraint( &constraint, NULL );
			RagdollAddConstraint( pPhysEnv, ragdoll, params, constraint );
		}
		else if ( !strcmpi( pBlock, "collisionrules" ) )
		{
			IPhysicsCollisionSet *pSet = EntityList()->Physics()->FindOrCreateCollisionSet( params.modelIndex, ragdoll.listCount );
			CRagdollCollisionRules rules(pSet);
			pParse->ParseCustom( (void *)&rules, &rules );
		}
		else if ( !strcmpi( pBlock, "animatedfriction") ) 
		{
			CRagdollAnimatedFriction friction( &ragdoll );
			pParse->ParseCustom( (void*)&friction, &friction );
		}
		else
		{
			pParse->SkipBlock();
		}
	}
	EntityList()->PhysGetCollision()->VPhysicsKeyParserDestroy( pParse );
}

void RagdollSetupCollisions( ragdoll_t &ragdoll, vcollide_t *pCollide, int modelIndex )
{
	Assert(pCollide);
	if (!pCollide)
		return;

	IPhysicsCollisionSet *pSet = EntityList()->Physics()->FindCollisionSet( modelIndex );
	if ( !pSet )
	{
		pSet = EntityList()->Physics()->FindOrCreateCollisionSet( modelIndex, ragdoll.listCount );
		if ( !pSet )
			return;

		bool bFoundRules = false;

		IVPhysicsKeyParser *pParse = EntityList()->PhysGetCollision()->VPhysicsKeyParserCreate( pCollide->pKeyValues );
		while ( !pParse->Finished() )
		{
			const char *pBlock = pParse->GetCurrentBlockName();
			if ( !strcmpi( pBlock, "collisionrules" ) )
			{
				IPhysicsCollisionSet *pSet = EntityList()->Physics()->FindOrCreateCollisionSet( modelIndex, ragdoll.listCount );
				CRagdollCollisionRules rules(pSet);
				pParse->ParseCustom( (void *)&rules, &rules );
				bFoundRules = true;
			}
			else
			{
				pParse->SkipBlock();
			}
		}
		EntityList()->PhysGetCollision()->VPhysicsKeyParserDestroy( pParse );

		if ( !bFoundRules )
		{
			// these are the default rules - each piece collides with everything
			// except immediate parent/constrained object.
			int i;
			for ( i = 0; i < ragdoll.listCount; i++ )
			{
				for ( int j = i+1; j < ragdoll.listCount; j++ )
				{
					pSet->EnableCollisions( i, j );
				}
			}
			for ( i = 0; i < ragdoll.listCount; i++ )
			{
  				int parent = ragdoll.list[i].parentIndex;
				if ( parent >= 0 )
				{
  					Assert( ragdoll.list[i].pObject );
  					Assert( ragdoll.list[i].pConstraint );
					pSet->DisableCollisions( i, parent );
				}
 			}
		}
	}
}

void RagdollActivate( ragdoll_t &ragdoll, vcollide_t *pCollide, int modelIndex, bool bForceWake )
{
	RagdollSetupCollisions( ragdoll, pCollide, modelIndex );
	for ( int i = 0; i < ragdoll.listCount; i++ )
	{
		ragdoll.list[i].pObject->SetGameIndex( i );
		PhysSetGameFlags( ragdoll.list[i].pObject, FVPHYSICS_MULTIOBJECT_ENTITY );
		// now that the relationships are set, activate the collision system
		ragdoll.list[i].pObject->EnableCollisions( true );

		if ( bForceWake == true )
		{
			ragdoll.list[i].pObject->Wake();
		}
	}
	if ( ragdoll.pGroup )
	{
		// NOTE: This also wakes the objects
		ragdoll.pGroup->Activate();
		// so if we didn't want that, we'll need to put them back to sleep here
		if ( !bForceWake )
		{
			for ( int i = 0; i < ragdoll.listCount; i++ )
			{
				ragdoll.list[i].pObject->Sleep();
			}

		}
	}
}


bool RagdollCreate( ragdoll_t &ragdoll, const ragdollparams_t &params, IPhysicsEnvironment *pPhysEnv )
{
	RagdollCreateObjects( pPhysEnv, ragdoll, params );

	if ( !ragdoll.listCount )
		return false;

	int forceBone = params.forceBoneIndex;
	
	int i;
	float totalMass = 0;
	for ( i = 0; i < ragdoll.listCount; i++ )
	{
		totalMass += ragdoll.list[i].pObject->GetMass();
	}
	totalMass = MAX(totalMass,1);

	// apply force to the model
	Vector nudgeForce = params.forceVector;
	Vector forcePosition = params.forcePosition;
	// UNDONE: Test scaling the force by total mass on all bones
	
	Assert( forceBone < ragdoll.listCount );

	if ( forceBone >= 0 && forceBone < ragdoll.listCount )
	{
		ragdoll.list[forceBone].pObject->ApplyForceCenter( nudgeForce );
		//nudgeForce *= 0.5;
		ragdoll.list[forceBone].pObject->GetPosition( &forcePosition, NULL );
	}

	for ( i = 0; i < ragdoll.listCount; i++ )
	{
		PhysSetGameFlags( ragdoll.list[i].pObject, FVPHYSICS_PART_OF_RAGDOLL );
	}

	if ( forcePosition != vec3_origin )
	{
		for ( i = 0; i < ragdoll.listCount; i++ )
		{
			if ( forceBone != i )
			{
				float scale = ragdoll.list[i].pObject->GetMass() / totalMass;
				ragdoll.list[i].pObject->ApplyForceOffset( scale * nudgeForce, forcePosition );
			}
		}
	}

	return true;
}


void RagdollApplyAnimationAsVelocity( ragdoll_t &ragdoll, const matrix3x4_t *pPrevBones, const matrix3x4_t *pCurrentBones, float dt )
{
	for ( int i = 0; i < ragdoll.listCount; i++ )
	{
		Vector velocity;
		AngularImpulse angVel;
		int boneIndex = ragdoll.boneIndex[i];
		CalcBoneDerivatives( velocity, angVel, pPrevBones[boneIndex], pCurrentBones[boneIndex], dt );
		
		AngularImpulse localAngVelocity;

		// Angular velocity is always applied in local space in vphysics
		ragdoll.list[i].pObject->WorldToLocalVector( &localAngVelocity, angVel );
		ragdoll.list[i].pObject->AddVelocity( &velocity, &localAngVelocity );
	}
}

void RagdollApplyAnimationAsVelocity( ragdoll_t &ragdoll, const matrix3x4_t *pBoneToWorld )
{
	for ( int i = 0; i < ragdoll.listCount; i++ )
	{
		matrix3x4_t inverse;
		MatrixInvert( pBoneToWorld[i], inverse );
		Quaternion q;
		Vector pos;
		MatrixAngles( inverse, q, pos );

		Vector velocity;
		AngularImpulse angVel;
		float flSpin;

		Vector localVelocity;
		AngularImpulse localAngVelocity;

		QuaternionAxisAngle( q, localAngVelocity, flSpin );
		localAngVelocity *= flSpin;
		localVelocity = pos;

		// move those bone-local coords back to world space using the ragdoll transform
		ragdoll.list[i].pObject->LocalToWorldVector( &velocity, localVelocity );

		ragdoll.list[i].pObject->AddVelocity( &velocity, &localAngVelocity );
	}
}


void RagdollDestroy( ragdoll_t &ragdoll )
{
	if ( !ragdoll.listCount )
		return;

	int i;
	for ( i = 0; i < ragdoll.listCount; i++ )
	{
		EntityList()->PhysGetEnv()->DestroyConstraint(ragdoll.list[i].pConstraint);
		ragdoll.list[i].pConstraint = NULL;
	}
	for ( i = 0; i < ragdoll.listCount; i++ )
	{
		// during level transitions these can get temporarily loaded without physics objects
		// purely for the purpose of testing for PVS of transition.  If they fail they get
		// deleted before the physics objects are loaded.  The list count will be nonzero
		// since that is saved separately.
		if ( ragdoll.list[i].pObject )
		{
			ragdoll.list[i].pObject->SetGameData(NULL);
			EntityList()->PhysGetEnv()->DestroyObject( ragdoll.list[i].pObject );
		}
		ragdoll.list[i].pObject = NULL;
	}
	EntityList()->PhysGetEnv()->DestroyConstraintGroup( ragdoll.pGroup );
	ragdoll.pGroup = NULL;
	ragdoll.listCount = 0;
}

// Parse the ragdoll and obtain the mapping from each physics element index to a bone index
// returns num phys elements
int RagdollExtractBoneIndices( int *boneIndexOut, IStudioHdr *pStudioHdr, vcollide_t *pCollide )
{
	int elementCount = 0;

	IVPhysicsKeyParser *pParse = EntityList()->PhysGetCollision()->VPhysicsKeyParserCreate( pCollide->pKeyValues );
	while ( !pParse->Finished() )
	{
		const char *pBlock = pParse->GetCurrentBlockName();
		if ( !strcmpi( pBlock, "solid" ) )
		{
			solid_t solid;
			pParse->ParseSolid( &solid, NULL );
			if ( elementCount < RAGDOLL_MAX_ELEMENTS )
			{
				boneIndexOut[elementCount] = pStudioHdr->Studio_BoneIndexByName( solid.name );
				elementCount++;
			}
		}
		else
		{
			pParse->SkipBlock();
		}
	}
	EntityList()->PhysGetCollision()->VPhysicsKeyParserDestroy( pParse );

	return elementCount;
}

bool RagdollGetBoneMatrix( const ragdoll_t &ragdoll, CBoneAccessor &pBoneToWorld, int objectIndex )
{
	int boneIndex = ragdoll.boneIndex[objectIndex];
	if ( boneIndex < 0 )
		return false;

	const ragdollelement_t &element = ragdoll.list[objectIndex];

	// during restore if a model has changed since the file was saved, this could be NULL
	if ( !element.pObject )
		return false;
	element.pObject->GetPositionMatrix( &pBoneToWorld.GetBoneForWrite( boneIndex ) );
	if ( element.parentIndex >= 0 && !ragdoll.allowStretch )
	{
		// overwrite the position from physics to force rigid attachment
		// UNDONE: If we support other types of constraints (or multiple constraints per object)
		// make sure these don't fight !
		int parentBoneIndex = ragdoll.boneIndex[element.parentIndex];
		Vector out;
		VectorTransform( element.originParentSpace, pBoneToWorld.GetBone( parentBoneIndex ), out );
		MatrixSetColumn( out, 3, pBoneToWorld.GetBoneForWrite( boneIndex ) );
	}
	return true;
}

void RagdollComputeExactBbox( const ragdoll_t &ragdoll, const Vector &origin, Vector &outMins, Vector &outMaxs )
{
	outMins = origin;
	outMaxs = origin;

	for ( int i = 0; i < ragdoll.listCount; i++ )
	{
		Vector mins, maxs;
		Vector objectOrg;
		QAngle objectAng;
		IPhysicsObject *pObject = ragdoll.list[i].pObject;
		pObject->GetPosition( &objectOrg, &objectAng );
		EntityList()->PhysGetCollision()->CollideGetAABB( &mins, &maxs, pObject->GetCollide(), objectOrg, objectAng );
		for ( int j = 0; j < 3; j++ )
		{
			if ( mins[j] < outMins[j] )
			{
				outMins[j] = mins[j];
			}
			if ( maxs[j] > outMaxs[j] )
			{
				outMaxs[j] = maxs[j];
			}
		}
	}
}

bool RagdollIsAsleep( const ragdoll_t &ragdoll )
{
	for ( int i = 0; i < ragdoll.listCount; i++ )
	{
		if ( ragdoll.list[i].pObject && !ragdoll.list[i].pObject->IsAsleep() )
			return false;
	}

	return true;
}




