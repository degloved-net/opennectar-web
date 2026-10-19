#pragma once

// Presentation clock for character animation. The simulation keeps using its
// own delta; this only removes small frame-to-frame timing noise from the
// amount passed to SysShape::Animator while paying the difference back so the
// animation never drifts from game time.
struct PcP2AnimClock {
	float average = 0.0f;
	float debt    = 0.0f;
	float output  = 0.0f;
	float tick    = 0.0f;
	double time   = 0.0;
	bool active   = false;
};

inline PcP2AnimClock& pc_p2_anim_clock()
{
	// Pikmin 2 also evaluates J3D models on DvdThread while constructing enemy
	// matrix caches. Animation presentation state belongs only to the section
	// thread; keeping the clock thread-local prevents that loader from sharing
	// or racing the live animation clock.
	static thread_local PcP2AnimClock clock;
	return clock;
}

inline void pc_p2_anim_clock_tick(float dt)
{
	PcP2AnimClock& clock = pc_p2_anim_clock();
	clock.active = true;
	if (dt <= 0.0f) {
		clock.output = clock.tick = dt;
		return;
	}

	// Treat a large discontinuity as a real hitch or refresh-mode change. Small
	// jitter is averaged and its time debt is returned gradually.
	if (clock.average <= 0.0f || dt > clock.average * 1.6f || dt < clock.average * 0.6f) {
		clock.average = dt;
		clock.debt    = 0.0f;
	} else {
		clock.average += (dt - clock.average) * 0.1f;
	}
	clock.debt += dt - clock.average;
	const float correction = clock.debt * 0.1f;
	clock.debt -= correction;
	clock.output = clock.average + correction;
	clock.tick   = dt;
	clock.time  += clock.output;
}

inline float pc_p2_anim_step(float requested)
{
	const PcP2AnimClock& clock = pc_p2_anim_clock();
	if (clock.tick <= 0.0f || clock.output <= 0.0f) {
		return requested;
	}
	return requested * (clock.output / clock.tick);
}
