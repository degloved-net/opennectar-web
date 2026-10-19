// Netplay lead captain. See pc_netplay_navilead.h.

#include "netplay/pc_netplay_navilead.h"

#include "netplay/pc_input_log.h"

#include "CPlate.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "nlib/Geometry.h"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

constexpr float kFrameSec      = 1.0f / 30.0f;
constexpr float kMaxLead       = 90.0f; // world units: about 10 frames of running
constexpr float kMaxStepY      = 25.0f; // larger height step: a wall or a ledge, no lead
constexpr float kSmoothing     = 0.5f;  // per presented frame, toward the new lead
constexpr int kMaxPending      = 32;

int sPlayer = -1;
std::vector<PcNaviLeadInput> sPending;
Vector3f sLead(0.0f, 0.0f, 0.0f); // smoothed offset of the last presented frame

struct Saved {
	Navi* navi = nullptr;
	Vector3f pos, rot;
	Matrix4f world, move, cursor;
};
Saved sSaved;

bool enabled()
{
	static int on = -1;
	if (on < 0) {
		const char* v = std::getenv("PIKMIN_NETPLAY_NAVI_LEAD");
		on            = (v != nullptr && v[0] == '0' && v[1] == '\0') ? 0 : 1;
	}
	return on == 1;
}

Navi* find_navi(int player)
{
	if (naviMgr == nullptr) return nullptr;
	Iterator it(naviMgr);
	CI_LOOP(it)
	{
		Navi* n = static_cast<Navi*>(*it);
		if (n != nullptr && n->mNaviID == player) return n;
	}
	return nullptr;
}

// Navi::makeVelocity's drive for one input, without its sim side effects.
Vector3f drive_of(Navi* n, const PcNaviLeadInput& in)
{
	Vector3f stick(in.stickX / 74.0f, 0.0f, -in.stickY / 74.0f);
	if (stick.x > 1.0f) stick.x = 1.0f;
	if (stick.x < -1.0f) stick.x = -1.0f;
	if (stick.z > 1.0f) stick.z = 1.0f;
	if (stick.z < -1.0f) stick.z = -1.0f;
	n->reviseController(stick);
	float s = 0.0f, c = 1.0f;
	pc_input_log_yaw_sincos(in.yaw, &s, &c);
	NTransform3D transform = NTransform3D();
	transform.makeRotate(Vector3f(0.0f, 1.0f, 0.0f), s, c);
	transform.transform(stick);
	const float speed = n->mPlateMgr->canNaviRunFast() ? C_NAVI_PARM(n, mRunSpeed) : C_NAVI_PARM(n, mMoveSpeed);
	return stick * speed;
}

} // namespace

void pc_netplay_navilead_set(int player, const PcNaviLeadInput* pending, int count)
{
	if (count <= 0 || pending == nullptr || !enabled()) {
		sPlayer = -1;
		sPending.clear();
		return;
	}
	if (count > kMaxPending) count = kMaxPending;
	sPlayer = player;
	sPending.assign(pending, pending + count);
}

void pc_netplay_navilead_begin_presentation(void)
{
	sSaved.navi = nullptr;
	Navi* n     = sPlayer >= 0 ? find_navi(sPlayer) : nullptr;
	Vector3f want(0.0f, 0.0f, 0.0f);
	Vector3f lastDrive(0.0f, 0.0f, 0.0f);
	const bool free = n != nullptr && !n->isPellet() && n->mRope == nullptr && n->mPlateMgr != nullptr
	               && n->mStateMachine->getCurrID(n) == NAVISTATE_Walk;
	if (free) {
		for (const PcNaviLeadInput& in : sPending) {
			lastDrive = drive_of(n, in);
			want      = want + lastDrive * kFrameSec;
		}
		// Blocked (a wall, a crowd): the sim moves far slower than it drives.
		const float drive = std::sqrt(n->mTargetVelocity.x * n->mTargetVelocity.x
		                              + n->mTargetVelocity.z * n->mTargetVelocity.z);
		const float moved = std::sqrt(n->mVelocity.x * n->mVelocity.x + n->mVelocity.z * n->mVelocity.z);
		if (drive > 1.0f && moved < 0.5f * drive) want = want * (moved / drive);
		const float len = std::sqrt(want.x * want.x + want.z * want.z);
		if (len > kMaxLead) want = want * (kMaxLead / len);
	}
	sLead = sLead + (want - sLead) * kSmoothing;
	if (!free || (std::fabs(sLead.x) < 0.01f && std::fabs(sLead.z) < 0.01f)) {
		if (!free) sLead.set(0.0f, 0.0f, 0.0f);
		return;
	}
	Vector3f pos = n->mSRT.t + Vector3f(sLead.x, 0.0f, sLead.z);
	pos.y        = mapMgr != nullptr ? mapMgr->getMinY(pos.x, pos.z, true) : n->mSRT.t.y;
	if (std::fabs(pos.y - n->mSRT.t.y) > kMaxStepY) {
		sLead.set(0.0f, 0.0f, 0.0f);
		return;
	}
	sSaved.navi   = n;
	sSaved.pos    = n->mSRT.t;
	sSaved.rot    = n->mSRT.r;
	sSaved.world  = n->mWorldMtx;
	sSaved.move   = n->mConstrainedMoveMtx;
	sSaved.cursor = n->mPcCursorMtx;
	const Vector3f delta = pos - n->mSRT.t;
	n->mSRT.t            = pos;
	if (lastDrive.x * lastDrive.x + lastDrive.z * lastDrive.z > 1.0f)
		n->mSRT.r.y = std::atan2(lastDrive.x, lastDrive.z);
	// The cursor ring is drawn from the matrix the sim pass stored.
	n->mPcCursorMtx.mMtx[0][3] += delta.x;
	n->mPcCursorMtx.mMtx[1][3] += delta.y;
	n->mPcCursorMtx.mMtx[2][3] += delta.z;
}

void pc_netplay_navilead_end_presentation(void)
{
	Navi* n = sSaved.navi;
	if (n == nullptr) return;
	sSaved.navi             = nullptr;
	n->mSRT.t               = sSaved.pos;
	n->mSRT.r               = sSaved.rot;
	n->mWorldMtx            = sSaved.world;
	n->mConstrainedMoveMtx  = sSaved.move;
	n->mPcCursorMtx         = sSaved.cursor;
}
