#include "JSystem/J3D/J3DAnmBase.h"
#include "JSystem/J3D/J3DAnmCluster.h"
#include "JSystem/J3D/J3DAnmColor.h"
#include "JSystem/J3D/J3DAnmTevRegKey.h"
#include "JSystem/J3D/J3DAnmTexPattern.h"
#include "JSystem/J3D/J3DAnmTextureSRTKey.h"
#include "JSystem/J3D/J3DAnmTransform.h"
#include "JSystem/J3D/J3DAnmVtxColor.h"
#include "JSystem/J3D/J3DFrameCtrl.h"
#include "JSystem/J3D/J3DModel.h"
#include "JSystem/JUtility/JUTNameTab.h"
#include "types.h"
#ifdef PIKI_PC_PORT
#include "JSystem/J3D/J3DMtxCalc.h"
#include "JSystem/JMath.h"
#include "timing/pc_anim_timing.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#endif

template <class T>
f32 J3DGetKeyFrameInterpolation(f32, J3DAnmKeyTableBase*, T*);

#ifdef PIKI_PC_PORT
namespace {
Quaternion pcJ3DQuatSlerp(Quaternion a, Quaternion b, f32 t)
{
	f32 dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (dot < 0.0f) {
		b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
		dot = -dot;
	}
	Quaternion out;
	if (dot > 0.9995f) {
		out.x = a.x + (b.x - a.x) * t;
		out.y = a.y + (b.y - a.y) * t;
		out.z = a.z + (b.z - a.z) * t;
		out.w = a.w + (b.w - a.w) * t;
	} else {
		const f32 angle = acosf(std::max(-1.0f, std::min(1.0f, dot)));
		const f32 invSin = 1.0f / sinf(angle);
		const f32 wa = sinf((1.0f - t) * angle) * invSin;
		const f32 wb = sinf(t * angle) * invSin;
		out.x = a.x * wa + b.x * wb;
		out.y = a.y * wa + b.y * wb;
		out.z = a.z * wa + b.z * wb;
		out.w = a.w * wa + b.w * wb;
	}
	const f32 length2 = out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w;
	if (length2 > 0.0000001f) {
		const f32 invLength = 1.0f / sqrtf(length2);
		out.x *= invLength; out.y *= invLength; out.z *= invLength; out.w *= invLength;
	}
	return out;
}

void pcJ3DQuatToEuler(const Quaternion& q, SVec* rotation)
{
	// q = qz * qy * qx, the exact order used by JMAEulerToQuat and J3D.
	const f32 m00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
	const f32 m10 = 2.0f * (q.x * q.y + q.w * q.z);
	const f32 m20 = 2.0f * (q.x * q.z - q.w * q.y);
	const f32 m21 = 2.0f * (q.y * q.z + q.w * q.x);
	const f32 m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
	const f32 m01 = 2.0f * (q.x * q.y - q.w * q.z);
	const f32 m11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
	const f32 ry  = asinf(std::max(-1.0f, std::min(1.0f, -m20)));
	f32 rx, rz;
	if (fabsf(cosf(ry)) > 0.0001f) {
		rx = atan2f(m21, m22);
		rz = atan2f(m10, m00);
	} else {
		rx = atan2f(ry >= 0.0f ? m01 : -m01, m11);
		rz = 0.0f;
	}
	const f32 shortPerRad = 32768.0f / 3.14159265358979323846f;
	rotation->x = (s16)lrintf(rx * shortPerRad);
	rotation->y = (s16)lrintf(ry * shortPerRad);
	rotation->z = (s16)lrintf(rz * shortPerRad);
}

void pcJ3DBlendTransform(const J3DTransformInfo& from, const J3DTransformInfo& to, f32 t, J3DTransformInfo* out)
{
	out->mScale.x = from.mScale.x + (to.mScale.x - from.mScale.x) * t;
	out->mScale.y = from.mScale.y + (to.mScale.y - from.mScale.y) * t;
	out->mScale.z = from.mScale.z + (to.mScale.z - from.mScale.z) * t;
	out->mTranslation.x = from.mTranslation.x + (to.mTranslation.x - from.mTranslation.x) * t;
	out->mTranslation.y = from.mTranslation.y + (to.mTranslation.y - from.mTranslation.y) * t;
	out->mTranslation.z = from.mTranslation.z + (to.mTranslation.z - from.mTranslation.z) * t;
	Quaternion a, b;
	JMAEulerToQuat(from.mRotation.x, from.mRotation.y, from.mRotation.z, &a);
	JMAEulerToQuat(to.mRotation.x, to.mRotation.y, to.mRotation.z, &b);
	const Quaternion q = pcJ3DQuatSlerp(a, b, t);
	pcJ3DQuatToEuler(q, &out->mRotation);
}

struct PcJ3DPoseState {
	const J3DAnmTransform* animation = nullptr;
	f32 frame = 0.0f;
	double time = 0.0;
	f32 blend = 1.0f;
	bool initialized = false;
	J3DTransformInfo from;
	J3DTransformInfo displayed;
};
struct PcJ3DPoseSlot {
	const J3DMtxBuffer* buffer = nullptr;
	u16 joint = 0;
	double lastSeen = 0.0;
	PcJ3DPoseState state;
};

constexpr size_t kPcJ3DPoseSlotCount = 8192; // power of two, enough for all visible model joints
} // namespace

void pcJ3DSmoothTransform(const J3DAnmTransform* animation, u16 joint, J3DTransformInfo* pose)
{
	PcP2AnimClock& clock = pc_p2_anim_clock();
	// DvdThread invokes J3DModel::calc while precomputing enemy caches. It must
	// receive the raw sampled pose and never touch presentation history owned by
	// the main section thread.
	if (!clock.active) return;
	const double now = clock.time;
	J3DMtxBuffer* buffer = J3DMtxCalc::getMtxBuffer();
	if (!buffer) return;

	// Do not allocate here: global new is backed by JKR's current heap, and
	// section transitions free that heap wholesale. A fixed open-addressed
	// table stays valid across title/game/cave heap changes and is main-thread
	// only because of the active-clock guard above.
	static PcJ3DPoseSlot slots[kPcJ3DPoseSlotCount];
	const size_t mask = kPcJ3DPoseSlotCount - 1;
	size_t at = ((reinterpret_cast<uintptr_t>(buffer) >> 4) ^ ((size_t)joint * 0x9e3779b1u)) & mask;
	PcJ3DPoseSlot* slot = nullptr;
	PcJ3DPoseSlot* stale = nullptr;
	for (size_t probe = 0; probe < kPcJ3DPoseSlotCount; ++probe) {
		PcJ3DPoseSlot& candidate = slots[(at + probe) & mask];
		if (candidate.buffer == buffer && candidate.joint == joint) {
			slot = &candidate;
			break;
		}
		if (!candidate.buffer) {
			slot = stale ? stale : &candidate;
			break;
		}
		if (!stale && now - candidate.lastSeen > 30.0) stale = &candidate;
	}
	if (!slot) slot = stale;
	if (!slot) return; // pathological overflow: raw sampled pose is always safe
	if (slot->buffer != buffer || slot->joint != joint) {
		slot->buffer = buffer;
		slot->joint = joint;
		slot->state.animation = nullptr;
		slot->state.frame = 0.0f;
		slot->state.time = now;
		slot->state.blend = 1.0f;
		slot->state.initialized = false;
	}
	slot->lastSeen = now;
	PcJ3DPoseState& state = slot->state;
	const f32 frame = animation->getFrame();
	if (!state.initialized) {
		state.animation = animation;
		state.frame = frame;
		state.time = now;
		state.displayed = *pose;
		state.initialized = true;
		return;
	}

	// A different clip, or a real loop/restart (small sub-frame jitter is not
	// one), starts an 80 ms presentation crossfade from the last visible pose.
	if (animation != state.animation || frame < state.frame - 0.5f) {
		state.from  = state.displayed;
		state.blend = 0.0f;
		state.time  = now;
	}
	if (state.blend < 1.0f) {
		const f32 elapsed = (f32)std::max(0.0, now - state.time);
		state.blend = std::min(1.0f, state.blend + elapsed / 0.080f);
		state.time = now;
		const f32 smooth = state.blend * state.blend * (3.0f - 2.0f * state.blend);
		J3DTransformInfo target = *pose;
		pcJ3DBlendTransform(state.from, target, smooth, pose);
	}
	state.animation = animation;
	state.frame = frame;
	state.displayed = *pose;
}
#endif

/**
 * @note Address: 0x80067678
 * @note Size: 0x30
 */
void J3DFrameCtrl::init(s16 i_end)
{
	mAttribute = J3DAA_UNKNOWN_2;
	mState     = 0;
	mStart     = 0;
	mEnd       = i_end;
	mLoop      = 0;
	mRate      = 1.0f;
	mFrame     = 0.0f;
}

/**
 * @note Address: 0x800676A8
 * @note Size: 0x474
 */
void J3DFrameCtrl::update()
{
	mState = 0;
	mFrame += mRate;
	switch (mAttribute) {
	case J3DAA_UNKNOWN_0:
		if (mFrame < mStart) {
			mFrame = mStart;
			mRate  = 0.0f;
			mState |= 1;
		}
		if (mFrame >= mEnd) {
			mFrame = mEnd - 0.001f;
			mRate  = 0.0f;
			mState |= 1;
		}
		break;
	case J3DAA_UNKNOWN_1:
		if (mFrame < mStart) {
			mFrame = mStart;
			mRate  = 0.0f;
			mState |= 1;
		}
		if (mFrame >= mEnd) {
			mFrame = mStart;
			mRate  = 0.0f;
			mState |= 1;
		}
		break;
	case J3DAA_UNKNOWN_2:
		while (mFrame < mStart) {
			mState |= 2;
			if (mLoop - mStart <= 0.0f) {
				break;
			}
			mFrame += mLoop - mStart;
		}
		while (mFrame >= mEnd) {
			mState |= 2;
			if (mEnd - mLoop <= 0.0f) {
				break;
			}
			mFrame -= mEnd - mLoop;
		}
		break;
	case J3DAA_UNKNOWN_3:
		if (mFrame >= mEnd) {
			mFrame = mEnd - (mFrame - mEnd);
			mRate  = -mRate;
		}
		if (mFrame < mStart) {
			mFrame = mStart - (mFrame - mStart);
			mRate  = 0.0f;
			mState |= 1;
		}
		break;
	case J3DAA_UNKNOWN_4:
		if (mFrame >= mEnd - 1.0f) {
			mFrame = (mEnd - 1.0f) - (mFrame - (mEnd - 1.0f));
			mRate  = -mRate;
		}
		if (mFrame < mStart) {
			mFrame = mStart - (mFrame - mStart);
			mRate  = -mRate;
			mState |= 2;
		}
		break;
	}
}

/**
 * @note Address: 0x80067B1C
 * @note Size: 0x360
 */
void J3DAnmTransformFull::getTransform(u16 idx, J3DTransformInfo* info) const
{
	u16 tableIdx                     = idx * 3;
	J3DAnmTransformFullTable* xTable = &mTable[tableIdx];
	J3DAnmTransformFullTable* yTable = &mTable[tableIdx + 1];
	J3DAnmTransformFullTable* zTable = &mTable[tableIdx + 2];

	if (getFrame() < 0.0f) {
		info->mScale.x = mScaleVals[xTable->mScaleOffset];
		info->mScale.y = mScaleVals[yTable->mScaleOffset];
		info->mScale.z = mScaleVals[zTable->mScaleOffset];

		info->mRotation.x = mRotationVals[xTable->mRotationOffset];
		info->mRotation.y = mRotationVals[yTable->mRotationOffset];
		info->mRotation.z = mRotationVals[zTable->mRotationOffset];

		info->mTranslation.x = mTranslationVals[xTable->mTranslationOffset];
		info->mTranslation.y = mTranslationVals[yTable->mTranslationOffset];
		info->mTranslation.z = mTranslationVals[zTable->mTranslationOffset];

	} else {
#ifdef PIKI_PC_PORT
		// Full animations contain one sample per original 30 Hz frame. The
		// console rounded to a sample, which is visibly stepped on a 60-360 Hz
		// display. Translation and scale use a local monotone Hermite curve (no
		// overshoot); rotation uses shortest-path quaternion slerp. Converting the
		// final quaternion back to Euler preserves J3D's Rz * Ry * Rx hierarchy.
		const f32 frame = getFrame();
		const int i1    = (int)floorf(frame);
		const f32 t     = frame - (f32)i1;

		auto sample = [](const f32* values, u16 offset, u16 count, int at, f32 neutral) -> f32 {
			if (!values || count == 0) return neutral;
			at = std::max(0, std::min(at, (int)count - 1));
			return values[offset + at];
		};
		auto pchip = [&](const f32* values, u16 offset, u16 count, f32 neutral) -> f32 {
			if (!values || count == 0) return neutral;
			if (count == 1 || t <= 0.0f) return sample(values, offset, count, i1, neutral);
			const f32 p0 = sample(values, offset, count, i1 - 1, neutral);
			const f32 p1 = sample(values, offset, count, i1, neutral);
			const f32 p2 = sample(values, offset, count, i1 + 1, neutral);
			const f32 p3 = sample(values, offset, count, i1 + 2, neutral);
			const f32 d0 = p1 - p0, d1 = p2 - p1, d2 = p3 - p2;
			auto tangent = [](f32 a, f32 b) -> f32 {
				return a * b <= 0.0f ? 0.0f : (2.0f * a * b) / (a + b);
			};
			const f32 m1 = tangent(d0, d1), m2 = tangent(d1, d2);
			const f32 t2 = t * t, t3 = t2 * t;
			return (2.0f * t3 - 3.0f * t2 + 1.0f) * p1 + (t3 - 2.0f * t2 + t) * m1
			     + (-2.0f * t3 + 3.0f * t2) * p2 + (t3 - t2) * m2;
		};

		info->mScale.x       = pchip(mScaleVals, xTable->mScaleOffset, xTable->mScaleMaxFrame, 1.0f);
		info->mScale.y       = pchip(mScaleVals, yTable->mScaleOffset, yTable->mScaleMaxFrame, 1.0f);
		info->mScale.z       = pchip(mScaleVals, zTable->mScaleOffset, zTable->mScaleMaxFrame, 1.0f);
		info->mTranslation.x = pchip(mTranslationVals, xTable->mTranslationOffset, xTable->mTranslationMaxFrame, 0.0f);
		info->mTranslation.y = pchip(mTranslationVals, yTable->mTranslationOffset, yTable->mTranslationMaxFrame, 0.0f);
		info->mTranslation.z = pchip(mTranslationVals, zTable->mTranslationOffset, zTable->mTranslationMaxFrame, 0.0f);

		auto rotSample = [&](J3DAnmTransformFullTable* table, int at) -> s16 {
			if (!mRotationVals || table->mRotationMaxFrame == 0) return 0;
			at = std::max(0, std::min(at, (int)table->mRotationMaxFrame - 1));
			return mRotationVals[table->mRotationOffset + at];
		};
		Quaternion q0, q1;
		JMAEulerToQuat(rotSample(xTable, i1), rotSample(yTable, i1), rotSample(zTable, i1), &q0);
		JMAEulerToQuat(rotSample(xTable, i1 + 1), rotSample(yTable, i1 + 1), rotSample(zTable, i1 + 1), &q1);
		const Quaternion q = pcJ3DQuatSlerp(q0, q1, t);
		pcJ3DQuatToEuler(q, &info->mRotation);
		return;
#else
		u32 maxFrame = (int)(getFrame() + 0.5f);

		// X VALUES
		if (maxFrame >= xTable->mScaleMaxFrame) {
			info->mScale.x = mScaleVals[xTable->mScaleMaxFrame - 1 + xTable->mScaleOffset];
		} else {
			info->mScale.x = mScaleVals[xTable->mScaleOffset + maxFrame];
		}

		if (maxFrame >= xTable->mRotationMaxFrame) {
			info->mRotation.x = mRotationVals[xTable->mRotationMaxFrame - 1 + xTable->mRotationOffset];
		} else {
			info->mRotation.x = mRotationVals[xTable->mRotationOffset + maxFrame];
		}

		if (maxFrame >= xTable->mTranslationMaxFrame) {
			info->mTranslation.x = mTranslationVals[xTable->mTranslationMaxFrame - 1 + xTable->mTranslationOffset];
		} else {
			info->mTranslation.x = mTranslationVals[xTable->mTranslationOffset + maxFrame];
		}

		// Y VALUES
		if (maxFrame >= yTable->mScaleMaxFrame) {
			info->mScale.y = mScaleVals[yTable->mScaleMaxFrame - 1 + yTable->mScaleOffset];
		} else {
			info->mScale.y = mScaleVals[yTable->mScaleOffset + maxFrame];
		}

		if (maxFrame >= yTable->mRotationMaxFrame) {
			info->mRotation.y = mRotationVals[yTable->mRotationMaxFrame - 1 + yTable->mRotationOffset];
		} else {
			info->mRotation.y = mRotationVals[yTable->mRotationOffset + maxFrame];
		}

		if (maxFrame >= yTable->mTranslationMaxFrame) {
			info->mTranslation.y = mTranslationVals[yTable->mTranslationMaxFrame - 1 + yTable->mTranslationOffset];
		} else {
			info->mTranslation.y = mTranslationVals[yTable->mTranslationOffset + maxFrame];
		}

		// Z VALUES
		if (maxFrame >= zTable->mScaleMaxFrame) {
			info->mScale.z = mScaleVals[zTable->mScaleMaxFrame - 1 + zTable->mScaleOffset];
		} else {
			info->mScale.z = mScaleVals[zTable->mScaleOffset + maxFrame];
		}

		if (maxFrame >= zTable->mRotationMaxFrame) {
			info->mRotation.z = mRotationVals[zTable->mRotationMaxFrame - 1 + zTable->mRotationOffset];
		} else {
			info->mRotation.z = mRotationVals[zTable->mRotationOffset + maxFrame];
		}

		if (maxFrame >= zTable->mTranslationMaxFrame) {
			info->mTranslation.z = mTranslationVals[zTable->mTranslationMaxFrame - 1 + zTable->mTranslationOffset];
		} else {
			info->mTranslation.z = mTranslationVals[zTable->mTranslationOffset + maxFrame];
		}
#endif
	}
}

/**
 * @note Address: 0x80067E7C
 * @note Size: 0x420
 * TODO: Needs J3DGetKeyFrameAnimation to be defined.
 */
void J3DAnmTransformKey::calcTransform(f32 p1, u16 p2, J3DTransformInfo* info) const
{
	u16 idx                       = p2 * 3;
	J3DAnmTransformKeyTable* xInf = &mTable[idx];
	J3DAnmTransformKeyTable* yInf = &mTable[idx + 1];
	J3DAnmTransformKeyTable* zInf = &mTable[idx + 2];

	switch (xInf->mScaleInfo.mMaxFrame) {
	case 0:
		info->mScale.x = 1;
		break;
	case 1:
		info->mScale.x = mScaleVals[xInf->mScaleInfo.mOffset];
		break;
	default:
		info->mScale.x = J3DGetKeyFrameInterpolation<f32>(p1, &xInf->mScaleInfo, &mScaleVals[xInf->mScaleInfo.mOffset]);
	}

	switch (yInf->mScaleInfo.mMaxFrame) {
	case 0:
		info->mScale.y = 1;
		break;
	case 1:
		info->mScale.y = mScaleVals[yInf->mScaleInfo.mOffset];
		break;
	default:
		info->mScale.y = J3DGetKeyFrameInterpolation<f32>(p1, &yInf->mScaleInfo, &mScaleVals[yInf->mScaleInfo.mOffset]);
	}

	switch (zInf->mScaleInfo.mMaxFrame) {
	case 0:
		info->mScale.z = 1;
		break;
	case 1:
		info->mScale.z = mScaleVals[zInf->mScaleInfo.mOffset];
		break;
	default:
		info->mScale.z = J3DGetKeyFrameInterpolation<f32>(p1, &zInf->mScaleInfo, &mScaleVals[zInf->mScaleInfo.mOffset]);
	}

	switch (xInf->mRotationInfo.mMaxFrame) {
	case 0:
		info->mRotation.x = 0;
		break;
	case 1:
		info->mRotation.x = mRotationVals[xInf->mRotationInfo.mOffset] << _20;
		break;
	default:
		info->mRotation.x
		    = static_cast<s32>(J3DGetKeyFrameInterpolation<s16>(p1, &xInf->mRotationInfo, &mRotationVals[xInf->mRotationInfo.mOffset]))
		   << _20;
	}

	switch (yInf->mRotationInfo.mMaxFrame) {
	case 0:
		info->mRotation.y = 0;
		break;
	case 1:
		info->mRotation.y = mRotationVals[yInf->mRotationInfo.mOffset] << _20;
		break;
	default:
		info->mRotation.y
		    = static_cast<s32>(J3DGetKeyFrameInterpolation<s16>(p1, &yInf->mRotationInfo, &mRotationVals[yInf->mRotationInfo.mOffset]))
		   << _20;
	}

	switch (zInf->mRotationInfo.mMaxFrame) {
	case 0:
		info->mRotation.z = 0;
		break;
	case 1:
		info->mRotation.z = mRotationVals[zInf->mRotationInfo.mOffset] << _20;
		break;
	default:
		info->mRotation.z
		    = static_cast<s32>(J3DGetKeyFrameInterpolation<s16>(p1, &zInf->mRotationInfo, &mRotationVals[zInf->mRotationInfo.mOffset]))
		   << _20;
	}

	switch (xInf->mTranslationInfo.mMaxFrame) {
	case 0:
		info->mTranslation.x = 0;
		break;
	case 1:
		info->mTranslation.x = mTranslationVals[xInf->mTranslationInfo.mOffset];
		break;
	default:
		info->mTranslation.x
		    = J3DGetKeyFrameInterpolation<f32>(p1, &xInf->mTranslationInfo, &mTranslationVals[xInf->mTranslationInfo.mOffset]);
	}

	switch (yInf->mTranslationInfo.mMaxFrame) {
	case 0:
		info->mTranslation.y = 0;
		break;
	case 1:
		info->mTranslation.y = mTranslationVals[yInf->mTranslationInfo.mOffset];
		break;
	default:
		info->mTranslation.y
		    = J3DGetKeyFrameInterpolation<f32>(p1, &yInf->mTranslationInfo, &mTranslationVals[yInf->mTranslationInfo.mOffset]);
	}

	switch (zInf->mTranslationInfo.mMaxFrame) {
	case 0:
		info->mTranslation.z = 0;
		break;
	case 1:
		info->mTranslation.z = mTranslationVals[zInf->mTranslationInfo.mOffset];
		break;
	default:
		info->mTranslation.z
		    = J3DGetKeyFrameInterpolation<f32>(p1, &zInf->mTranslationInfo, &mTranslationVals[zInf->mTranslationInfo.mOffset]);
	}
}

/**
 * @note Address: 0x8006829C
 * @note Size: 0x268
 */
void J3DAnmTextureSRTKey::calcTransform(f32 frame, u16 index, J3DTextureSRTInfo* info) const
{
	u16 idx                       = index * 3;
	J3DAnmTransformKeyTable* xInf = &mTable1[idx];
	J3DAnmTransformKeyTable* yInf = &mTable1[idx + 1];
	J3DAnmTransformKeyTable* zInf = &mTable1[idx + 2];

	switch (xInf->mScaleInfo.mMaxFrame) {
	case 0:
		info->mScaleX = 1;
		break;
	case 1:
		info->mScaleX = mScale1Vals[xInf->mScaleInfo.mOffset];
		break;
	default:
		info->mScaleX = J3DGetKeyFrameInterpolation<f32>(frame, &xInf->mScaleInfo, &mScale1Vals[xInf->mScaleInfo.mOffset]);
	}

	switch (yInf->mScaleInfo.mMaxFrame) {
	case 0:
		info->mScaleY = 1;
		break;
	case 1:
		info->mScaleY = mScale1Vals[yInf->mScaleInfo.mOffset];
		break;
	default:
		info->mScaleY = J3DGetKeyFrameInterpolation<f32>(frame, &yInf->mScaleInfo, &mScale1Vals[yInf->mScaleInfo.mOffset]);
	}

	switch (zInf->mRotationInfo.mMaxFrame) {
	case 0:
		info->mRotation = 0;
		break;
	case 1:
		info->mRotation = mRotation1Vals[zInf->mRotationInfo.mOffset] << mRotationScale;
		break;
	default:
		info->mRotation
		    = static_cast<s32>(J3DGetKeyFrameInterpolation<s16>(frame, &zInf->mRotationInfo, &mRotation1Vals[zInf->mRotationInfo.mOffset]))
		   << mRotationScale;
	}

	switch (xInf->mTranslationInfo.mMaxFrame) {
	case 0:
		info->mTranslationX = 0;
		break;
	case 1:
		info->mTranslationX = mTranslation1Vals[xInf->mTranslationInfo.mOffset];
		break;
	default:
		info->mTranslationX
		    = J3DGetKeyFrameInterpolation<f32>(frame, &xInf->mTranslationInfo, &mTranslation1Vals[xInf->mTranslationInfo.mOffset]);
	}

	switch (yInf->mTranslationInfo.mMaxFrame) {
	case 0:
		info->mTranslationY = 0;
		break;
	case 1:
		info->mTranslationY = mTranslation1Vals[yInf->mTranslationInfo.mOffset];
		break;
	default:
		info->mTranslationY
		    = J3DGetKeyFrameInterpolation<f32>(frame, &yInf->mTranslationInfo, &mTranslation1Vals[yInf->mTranslationInfo.mOffset]);
	}
}

/**
 * @note Address: 0x80068504
 * @note Size: 0x94
 */
f32 J3DAnmClusterFull::getWeight(u16 idx) const
{
	int index      = idx;
	int maxFrame   = (getFrame() + 0.5f);
	int tableFrame = mTables[index].mMaxFrame;
	if (getFrame() < 0.0f) {
		return mWeights[mTables[index].mOffset];
	}
	if (maxFrame >= tableFrame) {
		return mWeights[tableFrame - 1 + mTables[index].mOffset];
	}

	return mWeights[maxFrame + mTables[index].mOffset];
}

/**
 * @note Address: 0x80068598
 * @note Size: 0x84
 * getWeight__16J3DAnmClusterKeyCFUs
 */
f32 J3DAnmClusterKey::getWeight(u16 idx) const
{
	switch (mTables[idx].mMaxFrame) {
	case 0:
		return 1.0f;
	case 1:
		return mWeights[mTables[idx].mOffset];
	}

	return J3DGetKeyFrameInterpolation<f32>(mCurrentFrame, &mTables[idx], &mWeights[mTables[idx].mOffset]);
}

/**
 * @note Address: 0x8006861C
 * @note Size: 0x178
 */
void J3DAnmVtxColorFull::getColor(u8 col, u16 idx, GXColor* outColor) const
{
	int index                   = idx;
	J3DAnmColorFullTable* table = &getAnmTable(col)[idx];
	if (getFrame() < 0.0f) {
		outColor->r = mRedVals[table->mData[J3DAnmColorFullTable::RED][J3DAnmColorFullTable::Offset]];
		outColor->g = mGreenVals[table->mData[J3DAnmColorFullTable::GREEN][J3DAnmColorFullTable::Offset]];
		outColor->b = mBlueVals[table->mData[J3DAnmColorFullTable::BLUE][J3DAnmColorFullTable::Offset]];
		outColor->a = mAlphaVals[table->mData[J3DAnmColorFullTable::ALPHA][J3DAnmColorFullTable::Offset]];
		return;
	}

	int maxFrame = getFrame() + 0.5f;
	int redMax   = table->mData[J3DAnmColorFullTable::RED][J3DAnmColorFullTable::MaxFrame];
	if (maxFrame >= redMax) {
		outColor->r = mRedVals[table->mData[J3DAnmColorFullTable::RED][J3DAnmColorFullTable::Offset] - 1 + redMax];
	} else {
		outColor->r = mRedVals[table->mData[J3DAnmColorFullTable::RED][J3DAnmColorFullTable::Offset] + maxFrame];
	}

	int greenMax = table->mData[J3DAnmColorFullTable::GREEN][J3DAnmColorFullTable::MaxFrame];
	if (maxFrame >= greenMax) {
		outColor->g = mGreenVals[table->mData[J3DAnmColorFullTable::GREEN][J3DAnmColorFullTable::Offset] - 1 + greenMax];
	} else {
		outColor->g = mGreenVals[table->mData[J3DAnmColorFullTable::GREEN][J3DAnmColorFullTable::Offset] + maxFrame];
	}

	int blueMax = table->mData[J3DAnmColorFullTable::BLUE][J3DAnmColorFullTable::MaxFrame];
	if (maxFrame >= blueMax) {
		outColor->b = mBlueVals[table->mData[J3DAnmColorFullTable::BLUE][J3DAnmColorFullTable::Offset] - 1 + blueMax];
	} else {
		outColor->b = mBlueVals[table->mData[J3DAnmColorFullTable::BLUE][J3DAnmColorFullTable::Offset] + maxFrame];
	}

	int alphaMax = table->mData[J3DAnmColorFullTable::ALPHA][J3DAnmColorFullTable::MaxFrame];
	if (maxFrame >= alphaMax) {
		outColor->a = mAlphaVals[table->mData[J3DAnmColorFullTable::ALPHA][J3DAnmColorFullTable::Offset] - 1 + alphaMax];
	} else {
		outColor->a = mAlphaVals[table->mData[J3DAnmColorFullTable::ALPHA][J3DAnmColorFullTable::Offset] + maxFrame];
	}
}

/**
 * @note Address: 0x80068794
 * @note Size: 0x2F4
 */
void J3DAnmVtxColorKey::getColor(u8 col, u16 idx, GXColor* outColor) const
{
	f32 frame;
	u16 index                  = idx * 3;
	J3DAnmColorKeyTable* table = &getAnmTable(col)[idx];
	switch (table->mColorInfo[J3DAnmColorFullTable::RED].mMaxFrame) {
	case 0:
		outColor->r = 0;
		break;
	case 1:
		outColor->r = mRedVals[table->mColorInfo[J3DAnmColorFullTable::RED].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mColorInfo[J3DAnmColorFullTable::RED],
		                                         &mRedVals[table->mColorInfo[J3DAnmColorFullTable::RED].mOffset]);
		if (frame <= 0.0f) {
			outColor->r = 0;
		} else if (frame <= 255.0f) {
			OSf32tou8(&frame, &outColor->r);
		} else {
			outColor->r = 255;
		}
	}

	switch (table->mColorInfo[J3DAnmColorFullTable::GREEN].mMaxFrame) {
	case 0:
		outColor->g = 0;
		break;
	case 1:
		outColor->g = mGreenVals[table->mColorInfo[J3DAnmColorFullTable::GREEN].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mColorInfo[J3DAnmColorFullTable::GREEN],
		                                         &mGreenVals[table->mColorInfo[J3DAnmColorFullTable::GREEN].mOffset]);
		if (frame <= 0.0f) {
			outColor->g = 0;
		} else if (frame <= 255.0f) {
			OSf32tou8(&frame, &outColor->g);
		} else {
			outColor->g = 255;
		}
	}

	switch (table->mColorInfo[J3DAnmColorFullTable::BLUE].mMaxFrame) {
	case 0:
		outColor->b = 0;
		break;
	case 1:
		outColor->b = mBlueVals[table->mColorInfo[J3DAnmColorFullTable::BLUE].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mColorInfo[J3DAnmColorFullTable::BLUE],
		                                         &mBlueVals[table->mColorInfo[J3DAnmColorFullTable::BLUE].mOffset]);
		if (frame <= 0.0f) {
			outColor->b = 0;
		} else if (frame <= 255.0f) {
			OSf32tou8(&frame, &outColor->b);
		} else {
			outColor->b = 255;
		}
	}

	switch (table->mColorInfo[J3DAnmColorFullTable::ALPHA].mMaxFrame) {
	case 0:
		outColor->a = 0;
		break;
	case 1:
		outColor->a = mAlphaVals[table->mColorInfo[J3DAnmColorFullTable::ALPHA].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mColorInfo[J3DAnmColorFullTable::ALPHA],
		                                         &mAlphaVals[table->mColorInfo[J3DAnmColorFullTable::ALPHA].mOffset]);
		if (frame <= 0.0f) {
			outColor->a = 0;
		} else if (frame <= 255.0f) {
			OSf32tou8(&frame, &outColor->a);
		} else {
			outColor->a = 255;
		}
	}
}

/**
 * @note Address: 0x80068A88
 * @note Size: 0xA8
 */
void J3DAnmColor::searchUpdateMaterialID(J3DModelData* data)
{
	for (u16 i = 0; i < mUpdateMaterialNum; i++) {
		JUTNameTab* matNameTable = data->mMaterialTable.mMaterialNames;
		int index                = matNameTable->getIndex(mNameTab.getName(i));
		if (index != -1) {
			mUpdateMaterialID[i] = index;
		} else {
			mUpdateMaterialID[i] = 0xFFFF;
		}
	}
}

/**
 * @note Address: 0x80068B30
 * @note Size: 0x170
 */
void J3DAnmColorFull::getColor(u16 tableIndex, GXColor* color) const
{
	J3DAnmColorFullTable* table = mTable + tableIndex;
	if (getFrame() < 0.0f) {
		color->r = mRedVals[table->mData[J3DAnmColorFullTable::RED][J3DAnmColorFullTable::Offset]];
		color->g = mGreenVals[table->mData[J3DAnmColorFullTable::GREEN][J3DAnmColorFullTable::Offset]];
		color->b = mBlueVals[table->mData[J3DAnmColorFullTable::BLUE][J3DAnmColorFullTable::Offset]];
		color->a = mAlphaVals[table->mData[J3DAnmColorFullTable::ALPHA][J3DAnmColorFullTable::Offset]];
		return;
	}

	int frame  = getFrame() + 0.5f;
	int redMax = table->mData[J3DAnmColorFullTable::RED][J3DAnmColorFullTable::MaxFrame];
	if (frame >= redMax) {
		color->r = mRedVals[table->mData[J3DAnmColorFullTable::RED][J3DAnmColorFullTable::Offset] - 1 + redMax];
	} else {
		color->r = mRedVals[table->mData[J3DAnmColorFullTable::RED][J3DAnmColorFullTable::Offset] + frame];
	}

	int greenMax = table->mData[J3DAnmColorFullTable::GREEN][J3DAnmColorFullTable::MaxFrame];
	if (frame >= greenMax) {
		color->g = mGreenVals[table->mData[J3DAnmColorFullTable::GREEN][J3DAnmColorFullTable::Offset] - 1 + greenMax];
	} else {
		color->g = mGreenVals[table->mData[J3DAnmColorFullTable::GREEN][J3DAnmColorFullTable::Offset] + frame];
	}

	int blueMax = table->mData[J3DAnmColorFullTable::BLUE][J3DAnmColorFullTable::MaxFrame];
	if (frame >= blueMax) {
		color->b = mBlueVals[table->mData[J3DAnmColorFullTable::BLUE][J3DAnmColorFullTable::Offset] - 1 + blueMax];
	} else {
		color->b = mBlueVals[table->mData[J3DAnmColorFullTable::BLUE][J3DAnmColorFullTable::Offset] + frame];
	}

	int alphaMax = table->mData[J3DAnmColorFullTable::ALPHA][J3DAnmColorFullTable::MaxFrame];
	if (frame >= alphaMax) {
		color->a = mAlphaVals[table->mData[J3DAnmColorFullTable::ALPHA][J3DAnmColorFullTable::Offset] - 1 + alphaMax];
	} else {
		color->a = mAlphaVals[table->mData[J3DAnmColorFullTable::ALPHA][J3DAnmColorFullTable::Offset] + frame];
	}
}

/**
 * @note Address: 0x80068CA0
 * @note Size: 0x2CC
 */
void J3DAnmColorKey::getColor(u16 tableIndex, GXColor* outColor) const
{
	f32 frame;
	J3DAnmColorKeyTable* table = mTable + tableIndex;
	switch (table->mColorInfo[J3DAnmColorFullTable::RED].mMaxFrame) {
	case 0:
		outColor->r = 0;
		break;
	case 1:
		outColor->r = mRedValue[table->mColorInfo[J3DAnmColorFullTable::RED].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mColorInfo[J3DAnmColorFullTable::RED],
		                                         &mRedValue[table->mColorInfo[J3DAnmColorFullTable::RED].mOffset]);
		if (frame < 0.0f) {
			outColor->r = 0;
		} else if (frame > 255.0f) {
			outColor->r = 255;
		} else {
			OSf32tou8(&frame, &outColor->r);
		}
	}

	switch (table->mColorInfo[J3DAnmColorFullTable::GREEN].mMaxFrame) {
	case 0:
		outColor->g = 0;
		break;
	case 1:
		outColor->g = mGreenValue[table->mColorInfo[J3DAnmColorFullTable::GREEN].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mColorInfo[J3DAnmColorFullTable::GREEN],
		                                         &mGreenValue[table->mColorInfo[J3DAnmColorFullTable::GREEN].mOffset]);
		if (frame < 0.0f) {
			outColor->g = 0;
		} else if (frame > 255.0f) {
			outColor->g = 255;
		} else {
			OSf32tou8(&frame, &outColor->g);
		}
	}

	switch (table->mColorInfo[J3DAnmColorFullTable::BLUE].mMaxFrame) {
	case 0:
		outColor->b = 0;
		break;
	case 1:
		outColor->b = mBlueValue[table->mColorInfo[J3DAnmColorFullTable::BLUE].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mColorInfo[J3DAnmColorFullTable::BLUE],
		                                         &mBlueValue[table->mColorInfo[J3DAnmColorFullTable::BLUE].mOffset]);
		if (frame < 0.0f) {
			outColor->b = 0;
		} else if (frame > 255.0f) {
			outColor->b = 255;
		} else {
			OSf32tou8(&frame, &outColor->b);
		}
	}

	switch (table->mColorInfo[J3DAnmColorFullTable::ALPHA].mMaxFrame) {
	case 0:
		outColor->a = 0;
		break;
	case 1:
		outColor->a = mAlphaValue[table->mColorInfo[J3DAnmColorFullTable::ALPHA].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mColorInfo[J3DAnmColorFullTable::ALPHA],
		                                         &mAlphaValue[table->mColorInfo[J3DAnmColorFullTable::ALPHA].mOffset]);
		if (frame < 0.0f) {
			outColor->a = 0;
		} else if (frame > 255.0f) {
			outColor->a = 255;
		} else {
			OSf32tou8(&frame, &outColor->a);
		}
	}
}

/**
 * @note Address: 0x80068F6C
 * @note Size: 0xB4
 */
void J3DAnmTexPattern::getTexNo(u16 idx, u16* texNo) const
{
	int index                        = idx;
	J3DAnmTexPatternFullTable* table = mAnmTable;
	u32 maxFrame                     = table[index].mData[0][0];
	if (getFrame() < 0.0f) {
		*texNo = _0C[table[index].mData[0][1]];
		return;
	}

	if (getFrame() >= (f32)maxFrame) {
		*texNo = _0C[maxFrame - 1 + table[index].mData[0][1]];
		return;
	}

	*texNo = _0C[(int)mCurrentFrame + table[index].mData[0][1]];
}

/**
 * @note Address: 0x80069020
 * @note Size: 0x124
 */
void J3DAnmTextureSRTKey::searchUpdateMaterialID(J3DMaterialTable* table)
{
	for (u16 i = 0; i < getUpdateMaterialNum(); i++) {
		s32 materialID = table->getMaterialName()->getIndex(getUpdateMaterialName()->getName(i));
		if (materialID != -1) {
			mUpdateMaterialID[i] = materialID;
		} else {
			mUpdateMaterialID[i] = 0xFFFF;
		}
	}
	for (u16 i = 0; i < getPostUpdateMaterialNum(); i++) {
		s32 materialID = table->getMaterialName()->getIndex(getPostUpdateMaterialName()->getName(i));
		if (materialID != -1) {
			mPostUpdateMaterialID[i] = materialID;
		} else {
			mPostUpdateMaterialID[i] = 0xFFFF;
		}
	}
}

/**
 * @note Address: 0x80069020
 * @note Size: 0x124
 */
void J3DAnmTextureSRTKey::searchUpdateMaterialID(J3DModelData* modelData)
{
	searchUpdateMaterialID(&modelData->getMaterialTable());
}

/**
 * @note Address: 0x80069144
 * @note Size: 0x2CC
 */
void J3DAnmTevRegKey::getTevColorReg(u16 tableIndex, _GXColorS10* outColor) const
{
	f32 frame;
	J3DAnmCRegKeyTable* table = mCRegKeyTable + tableIndex;
	switch (table->mTables[J3DAnmColorFullTable::RED].mMaxFrame) {
	case 0:
		outColor->r = 0;
		break;
	case 1:
		outColor->r = mCRedVals[table->mTables[J3DAnmColorFullTable::RED].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mTables[J3DAnmColorFullTable::RED],
		                                         &mCRedVals[table->mTables[J3DAnmColorFullTable::RED].mOffset]);
		if (frame < -1024.0f) {
			outColor->r = -1024;
		} else if (frame > 1023.0f) {
			outColor->r = 1023;
		} else {
			OSf32tos16(&frame, &outColor->r);
		}
	}

	switch (table->mTables[J3DAnmColorFullTable::GREEN].mMaxFrame) {
	case 0:
		outColor->g = 0;
		break;
	case 1:
		outColor->g = mCGreenVals[table->mTables[J3DAnmColorFullTable::GREEN].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mTables[J3DAnmColorFullTable::GREEN],
		                                         &mCGreenVals[table->mTables[J3DAnmColorFullTable::GREEN].mOffset]);
		if (frame < -1024.0f) {
			outColor->g = -1024;
		} else if (frame > 1023.0f) {
			outColor->g = 1023;
		} else {
			OSf32tos16(&frame, &outColor->g);
		}
	}

	switch (table->mTables[J3DAnmColorFullTable::BLUE].mMaxFrame) {
	case 0:
		outColor->b = 0;
		break;
	case 1:
		outColor->b = mCBlueVals[table->mTables[J3DAnmColorFullTable::BLUE].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mTables[J3DAnmColorFullTable::BLUE],
		                                         &mCBlueVals[table->mTables[J3DAnmColorFullTable::BLUE].mOffset]);
		if (frame < -1024.0f) {
			outColor->b = -1024;
		} else if (frame > 1023.0f) {
			outColor->b = 1023;
		} else {
			OSf32tos16(&frame, &outColor->b);
		}
	}

	switch (table->mTables[J3DAnmColorFullTable::ALPHA].mMaxFrame) {
	case 0:
		outColor->a = 0;
		break;
	case 1:
		outColor->a = mCAlphaVals[table->mTables[J3DAnmColorFullTable::ALPHA].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mTables[J3DAnmColorFullTable::ALPHA],
		                                         &mCAlphaVals[table->mTables[J3DAnmColorFullTable::ALPHA].mOffset]);
		if (frame < -1024.0f) {
			outColor->a = -1024;
		} else if (frame > 1023.0f) {
			outColor->a = 1023;
		} else {
			OSf32tos16(&frame, &outColor->a);
		}
	}
}

/**
 * @note Address: 0x80069410
 * @note Size: 0x2CC
 */
void J3DAnmTevRegKey::getTevKonstReg(u16 tableIndex, GXColor* outColor) const
{
	f32 frame;
	J3DAnmKRegKeyTable* table = mKRegKeyTable + tableIndex;
	switch (table->mTables[J3DAnmColorFullTable::RED].mMaxFrame) {
	case 0:
		outColor->r = 0;
		break;
	case 1:
		outColor->r = mKRedVals[table->mTables[J3DAnmColorFullTable::RED].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mTables[J3DAnmColorFullTable::RED],
		                                         &mKRedVals[table->mTables[J3DAnmColorFullTable::RED].mOffset]);
		if (frame < 0.0f) {
			outColor->r = 0;
		} else if (frame > 255.0f) {
			outColor->r = 255;
		} else {
			OSf32tou8(&frame, &outColor->r);
		}
	}

	switch (table->mTables[J3DAnmColorFullTable::GREEN].mMaxFrame) {
	case 0:
		outColor->g = 0;
		break;
	case 1:
		outColor->g = mKGreenVals[table->mTables[J3DAnmColorFullTable::GREEN].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mTables[J3DAnmColorFullTable::GREEN],
		                                         &mKGreenVals[table->mTables[J3DAnmColorFullTable::GREEN].mOffset]);
		if (frame < 0.0f) {
			outColor->g = 0;
		} else if (frame > 255.0f) {
			outColor->g = 255;
		} else {
			OSf32tou8(&frame, &outColor->g);
		}
	}

	switch (table->mTables[J3DAnmColorFullTable::BLUE].mMaxFrame) {
	case 0:
		outColor->b = 0;
		break;
	case 1:
		outColor->b = mKBlueVals[table->mTables[J3DAnmColorFullTable::BLUE].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mTables[J3DAnmColorFullTable::BLUE],
		                                         &mKBlueVals[table->mTables[J3DAnmColorFullTable::BLUE].mOffset]);
		if (frame < 0.0f) {
			outColor->b = 0;
		} else if (frame > 255.0f) {
			outColor->b = 255;
		} else {
			OSf32tou8(&frame, &outColor->b);
		}
	}

	switch (table->mTables[J3DAnmColorFullTable::ALPHA].mMaxFrame) {
	case 0:
		outColor->a = 0;
		break;
	case 1:
		outColor->a = mKAlphaVals[table->mTables[J3DAnmColorFullTable::ALPHA].mOffset];
		break;
	default:
		frame = J3DGetKeyFrameInterpolation<s16>(mCurrentFrame, &table->mTables[J3DAnmColorFullTable::ALPHA],
		                                         &mKAlphaVals[table->mTables[J3DAnmColorFullTable::ALPHA].mOffset]);
		if (frame < 0.0f) {
			outColor->a = 0;
		} else if (frame > 255.0f) {
			outColor->a = 255;
		} else {
			OSf32tou8(&frame, &outColor->a);
		}
	}
}

/**
 * @note Address: 0x800696DC
 * @note Size: 0x10C
 * searchUpdateMaterialID__15J3DAnmTevRegKeyFP12J3DModelData
 */
void J3DAnmTevRegKey::searchUpdateMaterialID(J3DModelData* data)
{
	u16 i;
	for (i = 0; i < mCRegUpdateMaterialNum; i++) {
		JUTNameTab* nameTable = data->mMaterialTable.mMaterialNames;
		int index             = nameTable->getIndex(mCRegNameTable.getName(i));
		if (index != -1) {
			mCRegUpdateMaterialID[i] = index;
		} else {
			mCRegUpdateMaterialID[i] = 0xFFFF;
		}
	}
	for (i = 0; i < mKRegUpdateMaterialNum; i++) {
		JUTNameTab* nameTable = data->mMaterialTable.mMaterialNames;
		int index             = nameTable->getIndex(mKRegNameTable.getName(i));
		if (index != -1) {
			mKRegUpdateMaterialID[i] = index;
		} else {
			mKRegUpdateMaterialID[i] = 0xFFFF;
		}
	}
}

template <typename T>
f32 J3DGetKeyFrameInterpolation(f32 frame, J3DAnmKeyTableBase* table, T* values)
{
	if (frame < values[0]) {
		return values[1];
	} else {
		if (table->mType == 0) {
			if (values[(table->mMaxFrame - 1) * 3] <= frame) {
				return values[(table->mMaxFrame - 1) * 3 + 1];
			} else {
				u32 tmp = table->mMaxFrame;
				while (tmp > 1) {
					u32 halfTmp = tmp / 2;
					u32 upIdx   = halfTmp * 3;
					if (frame >= values[upIdx]) {
						values = values + upIdx;
						tmp -= halfTmp;
					} else {
						tmp = halfTmp;
					}
				}
				return J3DHermiteInterpolation<T>(frame, &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]);
			}
		} else if (values[(table->mMaxFrame - 1) * 4] <= frame) {
			return values[(table->mMaxFrame - 1) * 4 + 1];
		} else {
			u32 tmp = table->mMaxFrame;
			while (tmp > 1) {
				u32 halfTmp = tmp / 2;
				u32 upIdx   = halfTmp * 4;
				if (frame >= values[upIdx]) {
					values = values + upIdx;
					tmp -= halfTmp;
				} else {
					tmp = halfTmp;
				}
			}
			return J3DHermiteInterpolation<T>(frame, &values[0], &values[1], &values[3], &values[4], &values[5], &values[6]);
		}
	}
}
