#pragma once
// Character animation at high refresh rates: frame-rate independent helpers
// shared by Olimar (navi.cpp), the Pikmin (piki.cpp, viewPiki.cpp), the
// animators and BaseShape::updateAnim. Everything here is presentation only
// and steps aside in deterministic netplay, where the 30 Hz original rules.

#include "netplay/pc_netplay_det.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Animation clock
//
// A tick's wall-clock dt jitters by about +-10% at 120 Hz while frames reach
// the screen at a steady cadence, so a cycle advanced by the raw dt lurches
// slightly every few frames. Animators advance by a slow average instead,
// plus a correction term so animation time never drifts from real time.
// Large changes (hitches, FPS mode switches) pass straight through.
struct PcAnimClock {
	float avg  = 0.0f;
	float debt = 0.0f;
	float out  = 0.0f;
	float tick = 0.0f; // the tick's own dt, which `out` smooths
};

inline PcAnimClock& pc_anim_clock()
{
	static PcAnimClock clock;
	return clock;
}

// Once per logic tick, with that tick's dt.
inline void pc_anim_clock_tick(float dt)
{
	PcAnimClock& c = pc_anim_clock();
	if (c.avg <= 0.0f || dt > c.avg * 1.6f || dt < c.avg * 0.6f) {
		c.avg  = dt;
		c.debt = 0.0f;
	} else {
		c.avg += (dt - c.avg) * 0.1f;
	}
	c.debt += dt - c.avg;
	const float correction = c.debt * 0.1f;
	c.debt -= correction;
	c.out  = c.avg + correction;
	c.tick = dt;
}

// dt an animator should advance by; `real` is the dt the game passes. Scaled
// by real / tick so anything the game does to the step (0 while frozen, a
// fraction for slow motion) carries through; for a plain tick it is `out`.
inline float pc_anim_dt(float real)
{
	const PcAnimClock& c = pc_anim_clock();
	if (pc_netplay_deterministic() || c.out <= 0.0f || c.tick <= 0.0f) {
		return real;
	}
	return c.out * (real / c.tick);
}

// ---------------------------------------------------------------------------
// Walk animation speed
//
// The walk/run cycle's playback speed follows how far the character moved. At
// 30 Hz that is one tick of travel; at higher rates a single tick is so short
// that physics and collision noise make the speed, and with it the cycle,
// lurch from tick to tick. Measure over the last 1/30 s instead: exactly one
// tick, the original measure, at 30 Hz.
constexpr float kPcWalkWindow = 1.0f / 30.0f;

struct PcWalkSample {
	float x, z, dt;
};

struct PcWalkHistory {
	std::deque<PcWalkSample> samples;
	float lastX = 0.0f, lastZ = 0.0f;
};

inline float pc_anim_walk_speed(const void* owner, float prevX, float prevZ, float x, float z, float dt)
{
	static std::unordered_map<const void*, PcWalkHistory> sHistory;
	PcWalkHistory& h = sHistory[owner];
	// A teleport, or a new owner at a recycled address: start over.
	if (std::fabs(prevX - h.lastX) > 0.001f || std::fabs(prevZ - h.lastZ) > 0.001f) {
		h.samples.clear();
	}
	h.lastX = x;
	h.lastZ = z;
	h.samples.push_front({ prevX, prevZ, dt });
	float time = 0.0f, fromX = prevX, fromZ = prevZ;
	size_t used = 0;
	for (const PcWalkSample& s : h.samples) {
		time += s.dt;
		fromX = s.x;
		fromZ = s.z;
		used++;
		if (time >= kPcWalkWindow - 0.0001f) {
			break;
		}
	}
	h.samples.resize(used);
	const float dx = x - fromX, dz = z - fromZ;
	return time > 0.0f ? std::sqrt(dx * dx + dz * dz) / time : 0.0f;
}

// Motion cooldowns in the walk code count ticks; the originals were tuned for
// 30 Hz. Ticks that make up one 30 Hz tick at this dt.
inline int pc_anim_ticks_per_30hz(float dt)
{
	return dt > 0.0f ? std::max(1, int(kPcWalkWindow / dt + 0.5f)) : 1;
}

// ---------------------------------------------------------------------------
// Secondary motion
//
// A damped spring that follows the character's ground acceleration: the body
// leans into starts, stops and turns (tilting its up axis toward the
// acceleration), and appendages such as the Pikmin leaf lag behind it. The
// output is a tilt vector in world XZ, in radians: tilt the up axis that far
// toward (x, z).
struct PcSpringTilt {
	float tx = 0.0f, tz = 0.0f; // tilt
	float vx = 0.0f, vz = 0.0f; // tilt velocity
};

struct PcSecondaryState {
	float px = 0.0f, pz = 0.0f;   // last position
	float velX = 0.0f, velZ = 0.0f; // smoothed ground velocity
	bool init  = false;
	unsigned tick = 0;            // last tick stepped (draws can repeat in a tick)
	PcSpringTilt lean, leaf;
};

inline void pc_spring_step(PcSpringTilt& s, float targetX, float targetZ, float omega, float zeta, float dt, float limit)
{
	const float k = omega * omega, c = 2.0f * zeta * omega;
	s.vx += (k * (targetX - s.tx) - c * s.vx) * dt;
	s.vz += (k * (targetZ - s.tz) - c * s.vz) * dt;
	s.tx += s.vx * dt;
	s.tz += s.vz * dt;
	const float len = std::sqrt(s.tx * s.tx + s.tz * s.tz);
	if (len > limit) {
		s.tx *= limit / len;
		s.tz *= limit / len;
	}
}

// Advances the owner's springs from its position, once per logic tick
// (`tick`). Returns null in deterministic netplay or for a bad dt; the caller
// then draws as normal.
inline PcSecondaryState* pc_anim_secondary(const void* owner, float x, float z, float dt, unsigned tick)
{
	if (pc_netplay_deterministic() || dt <= 0.0f || dt > 0.1f) {
		return nullptr;
	}
	static std::unordered_map<const void*, PcSecondaryState> sState;
	PcSecondaryState& s = sState[owner];
	if (s.init && s.tick == tick) {
		return &s;
	}
	s.tick = tick;
	const float dx = x - s.px, dz = z - s.pz;
	if (!s.init || dx * dx + dz * dz > 400.0f * 400.0f * dt * dt) {
		// First sight or a teleport (faster than any walk): no acceleration.
		s      = PcSecondaryState();
		s.init = true;
		s.tick = tick;
		s.px   = x;
		s.pz   = z;
		return &s;
	}
	s.px = x;
	s.pz = z;
	// Ground velocity, low-passed: tick-to-tick collision noise is not motion.
	const float alpha = 1.0f - std::exp(-dt / 0.05f);
	const float oldX = s.velX, oldZ = s.velZ;
	s.velX += (dx / dt - s.velX) * alpha;
	s.velZ += (dz / dt - s.velZ) * alpha;
	const float ax = (s.velX - oldX) / dt, az = (s.velZ - oldZ) / dt;
	// Body: lean into the acceleration, slightly underdamped, at most ~7 deg.
	pc_spring_step(s.lean, ax * 0.00012f, az * 0.00012f, 14.0f, 0.65f, dt, 0.12f);
	// Leaf: lags the other way, looser and bouncier, at most ~25 deg.
	pc_spring_step(s.leaf, -ax * 0.0005f, -az * 0.0005f, 11.0f, 0.3f, dt, 0.45f);
	return &s;
}

// Rotation (column-major 3x3 as rows r[i][j]) tilting world up toward (tx, tz)
// by |t| radians.
inline bool pc_tilt_rotation(float tx, float tz, float r[3][3])
{
	const float angle = std::sqrt(tx * tx + tz * tz);
	if (angle < 0.0005f) {
		return false;
	}
	// Axis = up x dir = (dz, 0, -dx).
	const float kx = tz / angle, kz = -tx / angle;
	const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
	r[0][0] = t * kx * kx + c;  r[0][1] = -s * kz;  r[0][2] = t * kx * kz;
	r[1][0] = s * kz;           r[1][1] = c;        r[1][2] = -s * kx;
	r[2][0] = t * kx * kz;      r[2][1] = s * kx;   r[2][2] = t * kz * kz + c;
	return true;
}

// Tilts a world matrix (row-major m[row][col], GX layout) about its own origin:
// the character's feet.
inline void pc_tilt_world(float m[4][4], float tx, float tz)
{
	float r[3][3];
	if (!pc_tilt_rotation(tx, tz, r)) {
		return;
	}
	float out[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out[i][j] = r[i][0] * m[0][j] + r[i][1] * m[1][j] + r[i][2] * m[2][j];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			m[i][j] = out[i][j];
}

// Tilts a view-space joint matrix about the joint's own origin, by a tilt given
// in world space; `look` is the camera's world-to-view matrix.
inline void pc_tilt_view(float m[4][4], const float look[4][4], float tx, float tz)
{
	float r[3][3];
	if (!pc_tilt_rotation(tx, tz, r)) {
		return;
	}
	// View-space rotation: L * R * L^T.
	float lr[3][3], rv[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			lr[i][j] = look[i][0] * r[0][j] + look[i][1] * r[1][j] + look[i][2] * r[2][j];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			rv[i][j] = lr[i][0] * look[j][0] + lr[i][1] * look[j][1] + lr[i][2] * look[j][2];
	float out[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out[i][j] = rv[i][0] * m[0][j] + rv[i][1] * m[1][j] + rv[i][2] * m[2][j];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			m[i][j] = out[i][j];
}
