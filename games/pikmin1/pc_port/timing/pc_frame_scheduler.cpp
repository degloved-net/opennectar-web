#include "pc_frame_scheduler.h"

#include <algorithm>
#include <cmath>

PcFrameScheduler::PcFrameScheduler(int maxCatchUpTicks, double suspendThreshold)
    : mInitialised(false)
    , mFrameClamp(1)
    , mMaxCatchUpTicks(std::max(1, maxCatchUpTicks))
    , mSuspendThreshold(std::max(0.0, suspendThreshold))
    , mFixedDelta(1.0 / 60.0)
    , mLastTime(0.0)
    , mAccumulator(0.0)
    , mNextDeadline(0.0)
    , mTotalTicks(0)
    , mDiscardedTicks(0)
{
}

// Tick rate behind the port-only frame clamp 0: 120 Hz, or the display's
// refresh rate in the "monitor" FPS mode.
static double sHighRateHz = 120.0;

void pc_frame_set_high_rate(double hz)
{
	sHighRateHz = std::clamp(hz, 30.0, 360.0);
}

double pc_frame_high_rate(void) { return sHighRateHz; }

static bool sVsyncLock = false;

void pc_frame_set_vsync_lock(bool on) { sVsyncLock = on; }

bool pc_frame_vsync_lock(void) { return sVsyncLock; }

double PcFrameScheduler::deltaForClamp(int frameClamp)
{
	// Special case: frameClamp 0 is the high-rate mode (120 Hz or the monitor's)
	if (frameClamp == 0) return 1.0 / sHighRateHz;
	if (frameClamp == 1) return 1.0 / 60.0;
	if (frameClamp == 2) return 2.0 / 60.0;  // 30 Hz
	return frameClamp / 60.0;
}

void PcFrameScheduler::reset(double now, int frameClamp)
{
	mInitialised   = true;
	mFrameClamp    = frameClamp;
	mFixedDelta    = deltaForClamp(mFrameClamp);
	mLastTime      = now;
	mAccumulator   = 0.0;
	mNextDeadline  = now + mFixedDelta;
}

PcFrameSchedule PcFrameScheduler::advance(double now, int frameClamp)
{
	if (!mInitialised || frameClamp != mFrameClamp || deltaForClamp(frameClamp) != mFixedDelta || now < mLastTime) {
		reset(now, frameClamp);
		return { 0, mFixedDelta, 0.0, mNextDeadline, mDiscardedTicks };
	}

	double realDelta = now - mLastTime;
	mLastTime        = now;
	if (realDelta >= mSuspendThreshold) {
		// Focus loss, debugging and loading pauses do not become simulation debt.
		mAccumulator  = 0.0;
		mNextDeadline = now + mFixedDelta;
		return { 0, mFixedDelta, 0.0, mNextDeadline, mDiscardedTicks };
	}

	mAccumulator += realDelta;
	const int available = static_cast<int>(std::floor((mAccumulator + 1e-12) / mFixedDelta));
	const int ticks     = std::min(available, mMaxCatchUpTicks);
	if (available > ticks) {
		mDiscardedTicks += static_cast<std::uint64_t>(available - ticks);
		mAccumulator -= available * mFixedDelta;
	} else {
		mAccumulator -= ticks * mFixedDelta;
	}
	if (mAccumulator < 0.0) mAccumulator = 0.0;

	mTotalTicks += static_cast<std::uint64_t>(ticks);
	mNextDeadline = now + (mFixedDelta - mAccumulator);
	const double alpha = std::min(1.0, mAccumulator / mFixedDelta);
	return { ticks, mFixedDelta, alpha, mNextDeadline, mDiscardedTicks };
}
