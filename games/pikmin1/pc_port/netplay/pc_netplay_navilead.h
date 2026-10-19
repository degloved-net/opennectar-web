#pragma once
// Netplay lead (instant) captain: presentation-only prediction of this peer's
// own captain.
//
// In the online exact replica the guest's input reaches its own sim only once
// the host has applied it (about RTT + buffer later), so its captain answers
// the stick late. The presentation pass draws the guest's captain, and its
// whistle cursor, moved ahead by the inputs it has sent and the sim has not
// applied yet: per pending input the velocity Navi::makeVelocity would make
// (stick, reviseController, control yaw, walk or run speed), one sim frame
// each.
//
// Contract:
//   - presentation only: begin saves the captain's position, rotation, world,
//     move and cursor matrices; end puts them back, before anything of the sim
//     runs again. The sim pass, the hash and the inputs never see the lead;
//   - only while the captain is in its walk state, on the ground and not on a
//     rope; the lead shrinks when the captain is blocked (its sim speed far
//     under its drive) and is dropped across a big height step (a wall or a
//     ledge);
//   - inert until the session sets pending inputs; PIKMIN_NETPLAY_NAVI_LEAD=0
//     turns it off.

#include <cstdint>

struct PcNaviLeadInput {
	int8_t stickX   = 0;
	int8_t stickY   = 0;
	uint16_t yaw    = 0; // quantised control yaw (pc_input_log_yaw_sincos)
};

// Session side, every turn: the local captain (`player`) and the inputs sent
// for ticks the sim has not run yet, oldest first. count 0 clears the lead.
void pc_netplay_navilead_set(int player, const PcNaviLeadInput* pending, int count);

// Engine side, around the presentation pass's renderall().
void pc_netplay_navilead_begin_presentation(void);
void pc_netplay_navilead_end_presentation(void);
