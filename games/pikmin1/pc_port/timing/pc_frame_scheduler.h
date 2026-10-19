#pragma once

#include <cstdint>

// Tick rate used by frame clamp 0 (default 120 Hz; the monitor FPS mode sets
// the display's refresh rate). Clamped to 30..360 Hz.
void pc_frame_set_high_rate(double hz);
double pc_frame_high_rate(void);
// Monitor FPS mode: with vsync on, run one tick per display refresh instead of
// pacing ticks off the wall clock (System::run, updateSysClock).
void pc_frame_set_vsync_lock(bool on);
bool pc_frame_vsync_lock(void);

struct PcFrameSchedule {
	int logicalTicks;
	double fixedDelta;
	double interpolationAlpha;
	double nextDeadline;
	std::uint64_t discardedTicks;
};

// Platform-independent fixed-step scheduler. Times are monotonic seconds from
// an injected clock; the class deliberately has no SDL or rendering dependency.
class PcFrameScheduler {
public:
	explicit PcFrameScheduler(int maxCatchUpTicks = 4, double suspendThreshold = 0.5);

	void reset(double now, int frameClamp);
	PcFrameSchedule advance(double now, int frameClamp);

	std::uint64_t totalTicks() const { return mTotalTicks; }
	std::uint64_t discardedTicks() const { return mDiscardedTicks; }
	double fixedDelta() const { return mFixedDelta; }
	double nextDeadline() const { return mNextDeadline; }

private:
	static double deltaForClamp(int frameClamp);

	bool mInitialised;
	int mFrameClamp;
	int mMaxCatchUpTicks;
	double mSuspendThreshold;
	double mFixedDelta;
	double mLastTime;
	double mAccumulator;
	double mNextDeadline;
	std::uint64_t mTotalTicks;
	std::uint64_t mDiscardedTicks;
};
