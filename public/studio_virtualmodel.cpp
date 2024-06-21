//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include <limits.h>
#include "studio.h"
#include "tier1/utlmap.h"
#include "tier1/utldict.h"
#include "tier1/utlbuffer.h"
#include "filesystem.h"
#include "tier0/icommandline.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern IFileSystem *		g_pFileSystem;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: Converts radian-euler axis aligned angles to a quaternion
// Input  : *pfAngles - Right-handed Euler angles in radians
//			*outQuat - quaternion of form (i,j,k,real)
//-----------------------------------------------------------------------------
void AngleQuaternion(const RadianEuler& angles, Quaternion& outQuat)
{
	//Assert(s_bMathlibInitialized);
	//	Assert( angles.IsValid() );

#ifdef _VPROF_MATHLIB
	VPROF_BUDGET("AngleQuaternion", "Mathlib");
#endif

	float sr, sp, sy, cr, cp, cy;

#ifdef _X360
	fltx4 radians, scale, sine, cosine;
	radians = LoadUnaligned3SIMD(&angles.x);
	scale = ReplicateX4(0.5f);
	radians = MulSIMD(radians, scale);
	SinCos3SIMD(sine, cosine, radians);

	// NOTE: The ordering here is *different* from the AngleQuaternion below
	// because p, y, r are not in the same locations in QAngle + RadianEuler. Yay!
	sr = SubFloat(sine, 0);	sp = SubFloat(sine, 1);	sy = SubFloat(sine, 2);
	cr = SubFloat(cosine, 0);	cp = SubFloat(cosine, 1);	cy = SubFloat(cosine, 2);
#else
	SinCos(angles.z * 0.5f, &sy, &cy);
	SinCos(angles.y * 0.5f, &sp, &cp);
	SinCos(angles.x * 0.5f, &sr, &cr);
#endif

	// NJS: for some reason VC6 wasn't recognizing the common subexpressions:
	float srXcp = sr * cp, crXsp = cr * sp;
	outQuat.x = srXcp * cy - crXsp * sy; // X
	outQuat.y = crXsp * cy + srXcp * sy; // Y

	float crXcp = cr * cp, srXsp = sr * sp;
	outQuat.z = crXcp * sy - srXsp * cy; // Z
	outQuat.w = crXcp * cy + srXsp * sy; // W (real component)
}

//-----------------------------------------------------------------------------
// Purpose: Converts a quaternion into engine angles
// Input  : *quaternion - q3 + q0.i + q1.j + q2.k
//			*outAngles - PITCH, YAW, ROLL
//-----------------------------------------------------------------------------
void QuaternionAngles(const Quaternion& q, QAngle& angles)
{
	//Assert(s_bMathlibInitialized);
	Assert(q.IsValid());

#ifdef _VPROF_MATHLIB
	VPROF_BUDGET("QuaternionAngles", "Mathlib");
#endif

#if 1
	// FIXME: doing it this way calculates too much data, needs to do an optimized version...
	matrix3x4_t matrix;
	QuaternionMatrix(q, matrix);
	MatrixAngles(matrix, angles);
#else
	float m11, m12, m13, m23, m33;

	m11 = (2.0f * q.w * q.w) + (2.0f * q.x * q.x) - 1.0f;
	m12 = (2.0f * q.x * q.y) + (2.0f * q.w * q.z);
	m13 = (2.0f * q.x * q.z) - (2.0f * q.w * q.y);
	m23 = (2.0f * q.y * q.z) + (2.0f * q.w * q.x);
	m33 = (2.0f * q.w * q.w) + (2.0f * q.z * q.z) - 1.0f;

	// FIXME: this code has a singularity near PITCH +-90
	angles[YAW] = RAD2DEG(atan2(m12, m11));
	angles[PITCH] = RAD2DEG(asin(-m13));
	angles[ROLL] = RAD2DEG(atan2(m23, m33));
#endif

	Assert(angles.IsValid());
}

// transform in1 by the matrix in2
void VectorTransform(const float* in1, const matrix3x4_t& in2, float* out)
{
	//Assert(s_bMathlibInitialized);
	Assert(in1 != out);
	out[0] = DotProduct(in1, in2[0]) + in2[0][3];
	out[1] = DotProduct(in1, in2[1]) + in2[1][3];
	out[2] = DotProduct(in1, in2[2]) + in2[2][3];
}

// assume in2 is a rotation and rotate the input vector
void VectorRotate(const float* in1, const matrix3x4_t& in2, float* out)
{
	//Assert(s_bMathlibInitialized);
	Assert(in1 != out);
	out[0] = DotProduct(in1, in2[0]);
	out[1] = DotProduct(in1, in2[1]);
	out[2] = DotProduct(in1, in2[2]);
}

// rotate by the inverse of the matrix
void VectorIRotate(const float* in1, const matrix3x4_t& in2, float* out)
{
	//Assert(s_bMathlibInitialized);
	Assert(in1 != out);
	out[0] = in1[0] * in2[0][0] + in1[1] * in2[1][0] + in1[2] * in2[2][0];
	out[1] = in1[0] * in2[0][1] + in1[1] * in2[1][1] + in1[2] * in2[2][1];
	out[2] = in1[0] * in2[0][2] + in1[1] * in2[1][2] + in1[2] * in2[2][2];
}

void MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out)
{
	//Assert(s_bMathlibInitialized);
	memcpy(out.Base(), in.Base(), sizeof(float) * 3 * 4);
}

// NOTE: This is just the transpose not a general inverse
void MatrixInvert(const matrix3x4_t& in, matrix3x4_t& out)
{
	//Assert(s_bMathlibInitialized);
	if (&in == &out)
	{
		V_swap(out[0][1], out[1][0]);
		V_swap(out[0][2], out[2][0]);
		V_swap(out[1][2], out[2][1]);
	}
	else
	{
		// transpose the matrix
		out[0][0] = in[0][0];
		out[0][1] = in[1][0];
		out[0][2] = in[2][0];

		out[1][0] = in[0][1];
		out[1][1] = in[1][1];
		out[1][2] = in[2][1];

		out[2][0] = in[0][2];
		out[2][1] = in[1][2];
		out[2][2] = in[2][2];
	}

	// now fix up the translation to be in the other space
	float tmp[3];
	tmp[0] = in[0][3];
	tmp[1] = in[1][3];
	tmp[2] = in[2][3];

	out[0][3] = -DotProduct(tmp, out[0]);
	out[1][3] = -DotProduct(tmp, out[1]);
	out[2][3] = -DotProduct(tmp, out[2]);
}

void MatrixGetColumn(const matrix3x4_t& in, int column, Vector& out)
{
	out.x = in[0][column];
	out.y = in[1][column];
	out.z = in[2][column];
}

/*
================
R_ConcatTransforms
================
*/

const uint32 ALIGN16 g_SIMD_ComponentMask[4][4] ALIGN16_POST =
{
	{ 0xFFFFFFFF, 0, 0, 0 }, { 0, 0xFFFFFFFF, 0, 0 }, { 0, 0, 0xFFFFFFFF, 0 }, { 0, 0, 0, 0xFFFFFFFF }
};

typedef __m128 fltx4;
#define MM_SHUFFLE_REV(a,b,c,d) _MM_SHUFFLE(d,c,b,a)

FORCEINLINE fltx4 LoadUnalignedSIMD(const void* pSIMD)
{
	return _mm_loadu_ps(reinterpret_cast<const float*>(pSIMD));
}

FORCEINLINE fltx4 SplatXSIMD(fltx4 const& a)
{
	return _mm_shuffle_ps(a, a, MM_SHUFFLE_REV(0, 0, 0, 0));
}

FORCEINLINE fltx4 SplatYSIMD(fltx4 const& a)
{
	return _mm_shuffle_ps(a, a, MM_SHUFFLE_REV(1, 1, 1, 1));
}

FORCEINLINE fltx4 SplatZSIMD(fltx4 const& a)
{
	return _mm_shuffle_ps(a, a, MM_SHUFFLE_REV(2, 2, 2, 2));
}

FORCEINLINE fltx4 MulSIMD(const fltx4& a, const fltx4& b)				// a*b
{
	return _mm_mul_ps(a, b);
};

FORCEINLINE fltx4 AddSIMD(const fltx4& a, const fltx4& b)				// a+b
{
	return _mm_add_ps(a, b);
};

FORCEINLINE fltx4 AndSIMD(const fltx4& a, const fltx4& b)				// a & b
{
	return _mm_and_ps(a, b);
}

FORCEINLINE void StoreUnalignedSIMD(float* RESTRICT pSIMD, const fltx4& a)
{
	_mm_storeu_ps(pSIMD, a);
}

void ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
{
#if 0
	// test for ones that'll be 2x faster
	if ((((size_t)&in1) % 16) == 0 && (((size_t)&in2) % 16) == 0 && (((size_t)&out) % 16) == 0)
	{
		ConcatTransforms_Aligned(in1, in2, out);
		return;
	}
#endif

	fltx4 lastMask = *(fltx4*)(&g_SIMD_ComponentMask[3]);
	fltx4 rowA0 = LoadUnalignedSIMD(in1.m_flMatVal[0]);
	fltx4 rowA1 = LoadUnalignedSIMD(in1.m_flMatVal[1]);
	fltx4 rowA2 = LoadUnalignedSIMD(in1.m_flMatVal[2]);

	fltx4 rowB0 = LoadUnalignedSIMD(in2.m_flMatVal[0]);
	fltx4 rowB1 = LoadUnalignedSIMD(in2.m_flMatVal[1]);
	fltx4 rowB2 = LoadUnalignedSIMD(in2.m_flMatVal[2]);

	// now we have the rows of m0 and the columns of m1
	// first output row
	fltx4 A0 = SplatXSIMD(rowA0);
	fltx4 A1 = SplatYSIMD(rowA0);
	fltx4 A2 = SplatZSIMD(rowA0);
	fltx4 mul00 = MulSIMD(A0, rowB0);
	fltx4 mul01 = MulSIMD(A1, rowB1);
	fltx4 mul02 = MulSIMD(A2, rowB2);
	fltx4 out0 = AddSIMD(mul00, AddSIMD(mul01, mul02));

	// second output row
	A0 = SplatXSIMD(rowA1);
	A1 = SplatYSIMD(rowA1);
	A2 = SplatZSIMD(rowA1);
	fltx4 mul10 = MulSIMD(A0, rowB0);
	fltx4 mul11 = MulSIMD(A1, rowB1);
	fltx4 mul12 = MulSIMD(A2, rowB2);
	fltx4 out1 = AddSIMD(mul10, AddSIMD(mul11, mul12));

	// third output row
	A0 = SplatXSIMD(rowA2);
	A1 = SplatYSIMD(rowA2);
	A2 = SplatZSIMD(rowA2);
	fltx4 mul20 = MulSIMD(A0, rowB0);
	fltx4 mul21 = MulSIMD(A1, rowB1);
	fltx4 mul22 = MulSIMD(A2, rowB2);
	fltx4 out2 = AddSIMD(mul20, AddSIMD(mul21, mul22));

	// add in translation vector
	A0 = AndSIMD(rowA0, lastMask);
	A1 = AndSIMD(rowA1, lastMask);
	A2 = AndSIMD(rowA2, lastMask);
	out0 = AddSIMD(out0, A0);
	out1 = AddSIMD(out1, A1);
	out2 = AddSIMD(out2, A2);

	// write to output
	StoreUnalignedSIMD(out.m_flMatVal[0], out0);
	StoreUnalignedSIMD(out.m_flMatVal[1], out1);
	StoreUnalignedSIMD(out.m_flMatVal[2], out2);
}

//-----------------------------------------------------------------------------
// Quaternion sphereical linear interpolation
//-----------------------------------------------------------------------------

void QuaternionSlerp(const Quaternion& p, const Quaternion& q, float t, Quaternion& qt)
{
	Quaternion q2;
	// 0.0 returns p, 1.0 return q.

	// decide if one of the quaternions is backwards
	QuaternionAlign(p, q, q2);

	QuaternionSlerpNoAlign(p, q2, t, qt);
}


void QuaternionSlerpNoAlign(const Quaternion& p, const Quaternion& q, float t, Quaternion& qt)
{
	//Assert(s_bMathlibInitialized);
	float omega, cosom, sinom, sclp, sclq;
	int i;

	// 0.0 returns p, 1.0 return q.

	cosom = p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3];

	if ((1.0f + cosom) > 0.000001f) {
		if ((1.0f - cosom) > 0.000001f) {
			omega = acos(cosom);
			sinom = sin(omega);
			sclp = sin((1.0f - t) * omega) / sinom;
			sclq = sin(t * omega) / sinom;
		}
		else {
			// TODO: add short circuit for cosom == 1.0f?
			sclp = 1.0f - t;
			sclq = t;
		}
		for (i = 0; i < 4; i++) {
			qt[i] = sclp * p[i] + sclq * q[i];
		}
	}
	else {
		Assert(&qt != &q);

		qt[0] = -q[1];
		qt[1] = q[0];
		qt[2] = -q[3];
		qt[3] = q[2];
		sclp = sin((1.0f - t) * (0.5f * M_PI));
		sclq = sin(t * (0.5f * M_PI));
		for (i = 0; i < 3; i++) {
			qt[i] = sclp * p[i] + sclq * qt[i];
		}
	}

	Assert(qt.IsValid());
}

//-----------------------------------------------------------------------------
// Do a piecewise addition of the quaternion elements. This actually makes little 
// mathematical sense, but it's a cheap way to simulate a slerp.
//-----------------------------------------------------------------------------
void QuaternionBlend(const Quaternion& p, const Quaternion& q, float t, Quaternion& qt)
{
	//Assert(s_bMathlibInitialized);
#if ALLOW_SIMD_QUATERNION_MATH
	fltx4 psimd, qsimd, qtsimd;
	psimd = LoadUnalignedSIMD(p.Base());
	qsimd = LoadUnalignedSIMD(q.Base());
	qtsimd = QuaternionBlendSIMD(psimd, qsimd, t);
	StoreUnalignedSIMD(qt.Base(), qtsimd);
#else
	// decide if one of the quaternions is backwards
	Quaternion q2;
	QuaternionAlign(p, q, q2);
	QuaternionBlendNoAlign(p, q2, t, qt);
#endif
}


void QuaternionBlendNoAlign(const Quaternion& p, const Quaternion& q, float t, Quaternion& qt)
{
	//Assert(s_bMathlibInitialized);
	float sclp, sclq;
	int i;

	// 0.0 returns p, 1.0 return q.
	sclp = 1.0f - t;
	sclq = t;
	for (i = 0; i < 4; i++) {
		qt[i] = sclp * p[i] + sclq * q[i];
	}
	QuaternionNormalize(qt);
}

void QuaternionIdentityBlend(const Quaternion& p, float t, Quaternion& qt)
{
	//Assert(s_bMathlibInitialized);
	float sclp;

	sclp = 1.0f - t;

	qt.x = p.x * sclp;
	qt.y = p.y * sclp;
	qt.z = p.z * sclp;
	if (qt.w < 0.0)
	{
		qt.w = p.w * sclp - t;
	}
	else
	{
		qt.w = p.w * sclp + t;
	}
	QuaternionNormalize(qt);
}

void QuaternionScale(const Quaternion& p, float t, Quaternion& q)
{
	//Assert(s_bMathlibInitialized);

#if 0
	Quaternion p0;
	Quaternion q;
	p0.Init(0.0, 0.0, 0.0, 1.0);

	// slerp in "reverse order" so that p doesn't get realigned
	QuaternionSlerp(p, p0, 1.0 - fabs(t), q);
	if (t < 0.0)
	{
		q.w = -q.w;
	}
#else
	float r;

	// FIXME: nick, this isn't overly sensitive to accuracy, and it may be faster to 
	// use the cos part (w) of the quaternion (sin(omega)*N,cos(omega)) to figure the new scale.
	float sinom = sqrt(DotProduct(&p.x, &p.x));
	sinom = min(sinom, 1.f);

	float sinsom = sin(asin(sinom) * t);

	t = sinsom / (sinom + FLT_EPSILON);
	VectorScale(&p.x, t, &q.x);

	// rescale rotation
	r = 1.0f - sinsom * sinsom;

	// Assert( r >= 0 );
	if (r < 0.0f)
		r = 0.0f;
	r = sqrt(r);

	// keep sign of rotation
	if (p.w < 0)
		q.w = -r;
	else
		q.w = r;
#endif

	Assert(q.IsValid());

	return;
}

void QuaternionAlign(const Quaternion& p, const Quaternion& q, Quaternion& qt)
{
	//Assert(s_bMathlibInitialized);

	// FIXME: can this be done with a quat dot product?

	int i;
	// decide if one of the quaternions is backwards
	float a = 0;
	float b = 0;
	for (i = 0; i < 4; i++)
	{
		a += (p[i] - q[i]) * (p[i] - q[i]);
		b += (p[i] + q[i]) * (p[i] + q[i]);
	}
	if (a > b)
	{
		for (i = 0; i < 4; i++)
		{
			qt[i] = -q[i];
		}
	}
	else if (&qt != &q)
	{
		for (i = 0; i < 4; i++)
		{
			qt[i] = q[i];
		}
	}
}

float QuaternionDotProduct(const Quaternion& p, const Quaternion& q)
{
	//Assert(s_bMathlibInitialized);
	Assert(p.IsValid());
	Assert(q.IsValid());

	return p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w;
}

//-----------------------------------------------------------------------------
// Make sure the quaternion is of unit length
//-----------------------------------------------------------------------------
float QuaternionNormalize(Quaternion& q)
{
	//Assert(s_bMathlibInitialized);
	float radius, iradius;

	Assert(q.IsValid());

	radius = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];

	if (radius) // > FLT_EPSILON && ((radius < 1.0f - 4*FLT_EPSILON) || (radius > 1.0f + 4*FLT_EPSILON))
	{
		radius = sqrt(radius);
		iradius = 1.0f / radius;
		q[3] *= iradius;
		q[2] *= iradius;
		q[1] *= iradius;
		q[0] *= iradius;
	}
	return radius;
}

// qt = p * q
void QuaternionMult(const Quaternion& p, const Quaternion& q, Quaternion& qt)
{
	//Assert(s_bMathlibInitialized);
	Assert(p.IsValid());
	Assert(q.IsValid());

	if (&p == &qt)
	{
		Quaternion p2 = p;
		QuaternionMult(p2, q, qt);
		return;
	}

	// decide if one of the quaternions is backwards
	Quaternion q2;
	QuaternionAlign(p, q, q2);

	qt.x = p.x * q2.w + p.y * q2.z - p.z * q2.y + p.w * q2.x;
	qt.y = -p.x * q2.z + p.y * q2.w + p.z * q2.x + p.w * q2.y;
	qt.z = p.x * q2.y - p.y * q2.x + p.z * q2.w + p.w * q2.z;
	qt.w = -p.x * q2.x - p.y * q2.y - p.z * q2.z + p.w * q2.w;
}

void QuaternionMatrix(const Quaternion& q, const Vector& pos, matrix3x4_t& matrix)
{
	if (!HushAsserts())
	{
		Assert(pos.IsValid());
	}

	QuaternionMatrix(q, matrix);

	matrix[0][3] = pos.x;
	matrix[1][3] = pos.y;
	matrix[2][3] = pos.z;
}

void QuaternionMatrix(const Quaternion& q, matrix3x4_t& matrix)
{
	//Assert(s_bMathlibInitialized);
	if (!HushAsserts())
	{
		Assert(q.IsValid());
	}

#ifdef _VPROF_MATHLIB
	VPROF_BUDGET("QuaternionMatrix", "Mathlib");
#endif

	// Original code
	// This should produce the same code as below with optimization, but looking at the assmebly,
	// it doesn't.  There are 7 extra multiplies in the release build of this, go figure.
#if 1
	matrix[0][0] = 1.0 - 2.0 * q.y * q.y - 2.0 * q.z * q.z;
	matrix[1][0] = 2.0 * q.x * q.y + 2.0 * q.w * q.z;
	matrix[2][0] = 2.0 * q.x * q.z - 2.0 * q.w * q.y;

	matrix[0][1] = 2.0f * q.x * q.y - 2.0f * q.w * q.z;
	matrix[1][1] = 1.0f - 2.0f * q.x * q.x - 2.0f * q.z * q.z;
	matrix[2][1] = 2.0f * q.y * q.z + 2.0f * q.w * q.x;

	matrix[0][2] = 2.0f * q.x * q.z + 2.0f * q.w * q.y;
	matrix[1][2] = 2.0f * q.y * q.z - 2.0f * q.w * q.x;
	matrix[2][2] = 1.0f - 2.0f * q.x * q.x - 2.0f * q.y * q.y;

	matrix[0][3] = 0.0f;
	matrix[1][3] = 0.0f;
	matrix[2][3] = 0.0f;
#else
	float wx, wy, wz, xx, yy, yz, xy, xz, zz, x2, y2, z2;

	// precalculate common multiplitcations
	x2 = q.x + q.x;
	y2 = q.y + q.y;
	z2 = q.z + q.z;
	xx = q.x * x2;
	xy = q.x * y2;
	xz = q.x * z2;
	yy = q.y * y2;
	yz = q.y * z2;
	zz = q.z * z2;
	wx = q.w * x2;
	wy = q.w * y2;
	wz = q.w * z2;

	matrix[0][0] = 1.0 - (yy + zz);
	matrix[0][1] = xy - wz;
	matrix[0][2] = xz + wy;
	matrix[0][3] = 0.0f;

	matrix[1][0] = xy + wz;
	matrix[1][1] = 1.0 - (xx + zz);
	matrix[1][2] = yz - wx;
	matrix[1][3] = 0.0f;

	matrix[2][0] = xz - wy;
	matrix[2][1] = yz + wx;
	matrix[2][2] = 1.0 - (xx + yy);
	matrix[2][3] = 0.0f;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Converts an exponential map (ang/axis) to a quaternion
//-----------------------------------------------------------------------------
void AxisAngleQuaternion(const Vector& axis, float angle, Quaternion& q)
{
	float sa, ca;

	SinCos(DEG2RAD(angle) * 0.5f, &sa, &ca);

	q.x = axis.x * sa;
	q.y = axis.y * sa;
	q.z = axis.z * sa;
	q.w = ca;
}

//-----------------------------------------------------------------------------
// Purpose: converts engine euler angles into a matrix
// Input  : vec3_t angles - PITCH, YAW, ROLL
// Output : *matrix - left-handed column matrix
//			the basis vectors for the rotations will be in the columns as follows:
//			matrix[][0] is forward
//			matrix[][1] is left
//			matrix[][2] is up
//-----------------------------------------------------------------------------
void AngleMatrix(RadianEuler const& angles, const Vector& position, matrix3x4_t& matrix)
{
	AngleMatrix(angles, matrix);
	MatrixSetColumn(position, 3, matrix);
}

void AngleMatrix(const RadianEuler& angles, matrix3x4_t& matrix)
{
	QAngle quakeEuler(RAD2DEG(angles.y), RAD2DEG(angles.z), RAD2DEG(angles.x));

	AngleMatrix(quakeEuler, matrix);
}


void AngleMatrix(const QAngle& angles, const Vector& position, matrix3x4_t& matrix)
{
	AngleMatrix(angles, matrix);
	MatrixSetColumn(position, 3, matrix);
}

void AngleMatrix(const QAngle& angles, matrix3x4_t& matrix)
{
#ifdef _VPROF_MATHLIB
	VPROF_BUDGET("AngleMatrix", "Mathlib");
#endif
	//Assert(s_bMathlibInitialized);

	float sr, sp, sy, cr, cp, cy;

#ifdef _X360
	fltx4 radians, scale, sine, cosine;
	radians = LoadUnaligned3SIMD(angles.Base());
	scale = ReplicateX4(M_PI_F / 180.f);
	radians = MulSIMD(radians, scale);
	SinCos3SIMD(sine, cosine, radians);

	sp = SubFloat(sine, 0);	sy = SubFloat(sine, 1);	sr = SubFloat(sine, 2);
	cp = SubFloat(cosine, 0);	cy = SubFloat(cosine, 1);	cr = SubFloat(cosine, 2);
#else
	SinCos(DEG2RAD(angles[YAW]), &sy, &cy);
	SinCos(DEG2RAD(angles[PITCH]), &sp, &cp);
	SinCos(DEG2RAD(angles[ROLL]), &sr, &cr);
#endif

	// matrix = (YAW * PITCH) * ROLL
	matrix[0][0] = cp * cy;
	matrix[1][0] = cp * sy;
	matrix[2][0] = -sp;

	float crcy = cr * cy;
	float crsy = cr * sy;
	float srcy = sr * cy;
	float srsy = sr * sy;
	matrix[0][1] = sp * srcy - crsy;
	matrix[1][1] = sp * srsy + crcy;
	matrix[2][1] = sr * cp;

	matrix[0][2] = (sp * crcy + srsy);
	matrix[1][2] = (sp * crsy - srcy);
	matrix[2][2] = cr * cp;

	matrix[0][3] = 0.0f;
	matrix[1][3] = 0.0f;
	matrix[2][3] = 0.0f;
}

void SetIdentityMatrix(matrix3x4_t& matrix)
{
	memset(matrix.Base(), 0, sizeof(float) * 3 * 4);
	matrix[0][0] = 1.0;
	matrix[1][1] = 1.0;
	matrix[2][2] = 1.0;
}

//-----------------------------------------------------------------------------
// Purpose: Generates Euler angles given a left-handed orientation matrix. The
//			columns of the matrix contain the forward, left, and up vectors.
// Input  : matrix - Left-handed orientation matrix.
//			angles[PITCH, YAW, ROLL]. Receives right-handed counterclockwise
//				rotations in degrees around Y, Z, and X respectively.
//-----------------------------------------------------------------------------

void MatrixAngles(const matrix3x4_t& matrix, RadianEuler& angles, Vector& position)
{
	MatrixGetColumn(matrix, 3, position);
	MatrixAngles(matrix, angles);
}

void MatrixAngles(const matrix3x4_t& matrix, Quaternion& q, Vector& pos)
{
#ifdef _VPROF_MATHLIB
	VPROF_BUDGET("MatrixQuaternion", "Mathlib");
#endif
	float trace;
	trace = matrix[0][0] + matrix[1][1] + matrix[2][2] + 1.0f;
	if (trace > 1.0f + FLT_EPSILON)
	{
		// VPROF_INCREMENT_COUNTER("MatrixQuaternion A",1);
		q.x = (matrix[2][1] - matrix[1][2]);
		q.y = (matrix[0][2] - matrix[2][0]);
		q.z = (matrix[1][0] - matrix[0][1]);
		q.w = trace;
	}
	else if (matrix[0][0] > matrix[1][1] && matrix[0][0] > matrix[2][2])
	{
		// VPROF_INCREMENT_COUNTER("MatrixQuaternion B",1);
		trace = 1.0f + matrix[0][0] - matrix[1][1] - matrix[2][2];
		q.x = trace;
		q.y = (matrix[1][0] + matrix[0][1]);
		q.z = (matrix[0][2] + matrix[2][0]);
		q.w = (matrix[2][1] - matrix[1][2]);
	}
	else if (matrix[1][1] > matrix[2][2])
	{
		// VPROF_INCREMENT_COUNTER("MatrixQuaternion C",1);
		trace = 1.0f + matrix[1][1] - matrix[0][0] - matrix[2][2];
		q.x = (matrix[0][1] + matrix[1][0]);
		q.y = trace;
		q.z = (matrix[2][1] + matrix[1][2]);
		q.w = (matrix[0][2] - matrix[2][0]);
	}
	else
	{
		// VPROF_INCREMENT_COUNTER("MatrixQuaternion D",1);
		trace = 1.0f + matrix[2][2] - matrix[0][0] - matrix[1][1];
		q.x = (matrix[0][2] + matrix[2][0]);
		q.y = (matrix[2][1] + matrix[1][2]);
		q.z = trace;
		q.w = (matrix[1][0] - matrix[0][1]);
	}

	QuaternionNormalize(q);

#if 0
	// check against the angle version
	RadianEuler ang;
	MatrixAngles(matrix, ang);
	Quaternion test;
	AngleQuaternion(ang, test);
	float d = QuaternionDotProduct(q, test);
	Assert(fabs(d) > 0.99 && fabs(d) < 1.01);
#endif

	MatrixGetColumn(matrix, 3, pos);
}

// solve a x^2 + b x + c = 0
bool SolveQuadratic(float a, float b, float c, float& root1, float& root2)
{
	//Assert(s_bMathlibInitialized);
	if (a == 0)
	{
		if (b != 0)
		{
			// no x^2 component, it's a linear system
			root1 = root2 = -c / b;
			return true;
		}
		if (c == 0)
		{
			// all zero's
			root1 = root2 = 0;
			return true;
		}
		return false;
	}

	float tmp = b * b - 4.0f * a * c;

	if (tmp < 0)
	{
		// imaginary number, bah, no solution.
		return false;
	}

	tmp = sqrt(tmp);
	root1 = (-b + tmp) / (2.0f * a);
	root2 = (-b - tmp) / (2.0f * a);
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: basic hermite spline.  t = 0 returns p1, t = 1 returns p2, 
//			d1 and d2 are used to entry and exit slope of curve
// Input  : 
//-----------------------------------------------------------------------------

void Hermite_Spline(
	const Vector& p1,
	const Vector& p2,
	const Vector& d1,
	const Vector& d2,
	float t,
	Vector& output)
{
	//Assert(s_bMathlibInitialized);
	float tSqr = t * t;
	float tCube = t * tSqr;

	Assert(&output != &p1);
	Assert(&output != &p2);
	Assert(&output != &d1);
	Assert(&output != &d2);

	float b1 = 2.0f * tCube - 3.0f * tSqr + 1.0f;
	float b2 = 1.0f - b1; // -2*tCube+3*tSqr;
	float b3 = tCube - 2 * tSqr + t;
	float b4 = tCube - tSqr;

	VectorScale(p1, b1, output);
	VectorMA(output, b2, p2, output);
	VectorMA(output, b3, d1, output);
	VectorMA(output, b4, d2, output);
}

float Hermite_Spline(
	float p1,
	float p2,
	float d1,
	float d2,
	float t)
{
	//Assert(s_bMathlibInitialized);
	float output;
	float tSqr = t * t;
	float tCube = t * tSqr;

	float b1 = 2.0f * tCube - 3.0f * tSqr + 1.0f;
	float b2 = 1.0f - b1; // -2*tCube+3*tSqr;
	float b3 = tCube - 2 * tSqr + t;
	float b4 = tCube - tSqr;

	output = p1 * b1;
	output += p2 * b2;
	output += d1 * b3;
	output += d2 * b4;

	return output;
}

//-----------------------------------------------------------------------------
// Purpose: Converts a quaternion into engine angles
// Input  : *quaternion - q3 + q0.i + q1.j + q2.k
//			*outAngles - PITCH, YAW, ROLL
//-----------------------------------------------------------------------------
void QuaternionAngles(const Quaternion& q, RadianEuler& angles)
{
	//Assert(s_bMathlibInitialized);
	Assert(q.IsValid());

	// FIXME: doing it this way calculates too much data, needs to do an optimized version...
	matrix3x4_t matrix;
	QuaternionMatrix(q, matrix);
	MatrixAngles(matrix, angles);

	Assert(angles.IsValid());
}

void Hermite_Spline(const Vector& p0, const Vector& p1, const Vector& p2, float t, Vector& output)
{
	Vector e10, e21;
	VectorSubtract(p1, p0, e10);
	VectorSubtract(p2, p1, e21);
	Hermite_Spline(p1, p2, e10, e21, t, output);
}

void Hermite_Spline(const Quaternion& q0, const Quaternion& q1, const Quaternion& q2, float t, Quaternion& output)
{
	// cheap, hacked version of quaternions
	Quaternion q0a;
	Quaternion q1a;

	QuaternionAlign(q2, q0, q0a);
	QuaternionAlign(q2, q1, q1a);

	output.x = Hermite_Spline(q0a.x, q1a.x, q2.x, t);
	output.y = Hermite_Spline(q0a.y, q1a.y, q2.y, t);
	output.z = Hermite_Spline(q0a.z, q1a.z, q2.z, t);
	output.w = Hermite_Spline(q0a.w, q1a.w, q2.w, t);

	QuaternionNormalize(output);
}

void MatrixAngles(const matrix3x4_t& matrix, float* angles)
{
#ifdef _VPROF_MATHLIB
	VPROF_BUDGET("MatrixAngles", "Mathlib");
#endif
	//Assert(s_bMathlibInitialized);
	float forward[3];
	float left[3];
	float up[3];

	//
	// Extract the basis vectors from the matrix. Since we only need the Z
	// component of the up vector, we don't get X and Y.
	//
	forward[0] = matrix[0][0];
	forward[1] = matrix[1][0];
	forward[2] = matrix[2][0];
	left[0] = matrix[0][1];
	left[1] = matrix[1][1];
	left[2] = matrix[2][1];
	up[2] = matrix[2][2];

	float xyDist = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);

	// enough here to get angles?
	if (xyDist > 0.001f)
	{
		// (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
		angles[1] = RAD2DEG(atan2f(forward[1], forward[0]));

		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

		// (roll)	z = ATAN( left.z, up.z );
		angles[2] = RAD2DEG(atan2f(left[2], up[2]));
	}
	else	// forward is mostly Z, gimbal lock-
	{
		// (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
		angles[1] = RAD2DEG(atan2f(-left[0], left[1]));

		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

		// Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
		angles[2] = 0;
	}
}

float Hermite_Spline(float p0, float p1, float p2, float t)
{
	return Hermite_Spline(p1, p2, p1 - p0, p2 - p1, t);
}