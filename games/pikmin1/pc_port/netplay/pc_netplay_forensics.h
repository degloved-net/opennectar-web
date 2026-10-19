#pragma once
// Netplay desync forensics: engine-free data model, wire formats and diff
// logic. Nothing here touches the simulation; the session fills the
// per-tick sub-hash ring and the state-dump layer fills the per-object
// records, and this header turns them into the two messages the peers
// exchange when GekkoNet reports a desync, the console lines both print, and
// the dump files written into the run folder.
//
// Per-tick sub-hash entry (TickSubs): the seven columns of the state hash
// (navi piki teki item world rng mode), the folded total GekkoNet compares
// (low 32 bits via fold), and `xtra`: a SEPARATE 64-bit hash over simulation
// fields the seven columns do not cover (creature flags, sticking and grab
// links, positions carried between ticks, collision bounding sphere). xtra is
// never part of `total`, never compared by GekkoNet and never printed by the
// state-hash log; it only helps to place a divergence earlier than the tick
// the visible hash caught.
//
// Per-object record (ObjRec): one line of the state dump. `hash` is an FNV-1a
// 64 over exactly the fields the seven-column hash mixes for that object,
// `xhash` over the extra fields above. Records are keyed by (kind, ord): ord
// is the object's index in its manager's walk, so a spawn or despawn shifts
// the later ords of that kind, which the diff reports as a count mismatch
// first.
//
// Messages (little-endian, every field explicit; the session sends them in
// fragments over the handshake channel):
//   kBulkDesyncRing 0x17 (peer -> peer):
//     0   4  magic 'D','S','R','1'
//     4   1  version (1)
//     5   1  role (0 host, 1 joiner)
//     6   2  reserved 0
//     8   4  gekko desync frame (u32)
//     12  4  this peer's gekko checksum at that frame
//     16  4  the peer's checksum as GekkoNet reported it
//     20  4  n entries
//     24  n * 80  TickSubs: tick u64, total u64, subs[7] u64, xtra u64
//   kBulkDesyncObjs 0x18 (peer -> peer):
//     0   4  magic 'D','S','O','1'
//     4   1  version (1)
//     5   1  role
//     6   2  tick count t (0..4)
//     8   ... t blocks, each: tick u64, n u32, then n * 25 bytes
//             { kind u8, ord i32, type i32, hash u64, xhash u64 }
//   kBulkRepairPlan 0x19 (host -> joiner): the partial desync repair
//     0   4  magic 'D','R','P','1'
//     4   1  version (1)
//     5   3  reserved 0
//     8   4  attempt (u32, 1-based, grows per repair)
//     12  8  repair tick (u64): the joiner takes the host's records after it
//   kBulkRepairData 0x1A (host -> joiner): the host's records at that tick
//     0   4  magic 'D','R','D','1'
//     4   1  version (1)
//     5   3  reserved 0
//     8   4  attempt
//     12  8  tick
//     20  4  n records, then n * 101 bytes
//             { kind u8, ord i32, type i32, state i32, aux[4] i32, health f32,
//               f[13] f32, hash u64, xhash u64 }
// Both bounded before any allocation (kMaxRingEntries, kMaxObjsPerTick).
//
// Partial repair (plan_repair): the joiner overwrites the motion fields
// (pos rot vel drive face) and health of the navi, piki and teki whose hash
// differs, when the type, state and aux match. Anything else that differs
// (object counts, states, items, pellets, bosses, onions, world, rng, mode)
// cannot be repaired this way.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pc_netplay_forensics {

constexpr uint8_t kBulkDesyncRing = 0x17;
constexpr uint8_t kBulkDesyncObjs = 0x18;
constexpr uint8_t kBulkRepairPlan = 0x19;
constexpr uint8_t kBulkRepairData = 0x1A;

constexpr int kSubCount = 7;
extern const char* const kSubNames[kSubCount]; // navi piki teki item world rng mode

constexpr size_t kMaxRingEntries = 2048;
constexpr size_t kMaxObjsPerTick = 4096;
constexpr size_t kMaxObjTicks    = 4;
constexpr size_t kTickSubsBytes  = 80;
constexpr size_t kObjKeyBytes    = 25;
constexpr size_t kObjRecBytes    = 101;

struct TickSubs {
	uint64_t tick  = 0;
	uint64_t total = 0;
	uint64_t subs[kSubCount] = { 0, 0, 0, 0, 0, 0, 0 };
	uint64_t xtra  = 0;
};

enum ObjKind : uint8_t {
	kNavi = 0,
	kPiki = 1,
	kTeki = 2,
	kItem = 3,
	kPellet = 4,
	kBoss = 5,
	kOnion = 6,
	kWorld = 7,
	kRng = 8,
	kMode = 9,
	kKindCount = 10,
};
const char* kind_name(uint8_t kind);

struct ObjRec {
	uint8_t kind = 0;
	int32_t ord  = 0;
	int32_t type = 0;
	int32_t state = 0;
	int32_t aux[4] = { 0, 0, 0, 0 };
	float health = 0;
	// pos(3) rot(3) vel(3) drive(3) face(1)
	float f[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint64_t hash  = 0;
	uint64_t xhash = 0;
};

// What a peer sends about one tick's objects (no floats: hashes only).
struct ObjKey {
	uint8_t kind = 0;
	int32_t ord  = 0;
	int32_t type = 0;
	uint64_t hash  = 0;
	uint64_t xhash = 0;
};
struct ObjTick {
	uint64_t tick = 0;
	std::vector<ObjKey> keys;
};

struct RingMsg {
	uint8_t role = 0;
	uint32_t frame = 0;
	uint32_t localChecksum = 0;
	uint32_t remoteChecksum = 0;
	std::vector<TickSubs> ticks;
};
struct ObjsMsg {
	uint8_t role = 0;
	std::vector<ObjTick> ticks;
};

std::vector<uint8_t> encode_ring(const RingMsg& m);
bool decode_ring(const uint8_t* data, size_t len, RingMsg* out);
std::vector<uint8_t> encode_objs(const ObjsMsg& m);
bool decode_objs(const uint8_t* data, size_t len, ObjsMsg* out);

ObjKey key_of(const ObjRec& r);

struct RepairPlanMsg {
	uint32_t attempt = 0;
	uint64_t tick    = 0;
};
struct RepairDataMsg {
	uint32_t attempt = 0;
	uint64_t tick    = 0;
	std::vector<ObjRec> recs;
};
std::vector<uint8_t> encode_repair_plan(const RepairPlanMsg& m);
bool decode_repair_plan(const uint8_t* data, size_t len, RepairPlanMsg* out);
std::vector<uint8_t> encode_repair_data(const RepairDataMsg& m);
bool decode_repair_data(const uint8_t* data, size_t len, RepairDataMsg* out);

// Which host records the joiner must take (indices into `host`), comparing
// both peers' records of the same tick. False with `why` set when the
// difference is not repairable; true with an empty list when they agree.
bool plan_repair(const std::vector<ObjRec>& local, const std::vector<ObjRec>& host, std::vector<size_t>* take,
                 std::string* why);

// Bitmask of the seven sub-hash columns that differ between a and b.
unsigned differing_subs(const TickSubs& a, const TickSubs& b);
// "navi,piki" for a mask, "none" for 0.
std::string subs_mask_text(unsigned mask);

// Lowest tick present in both rings whose `total` differs; false when none.
bool first_diff_total(const std::vector<TickSubs>& a, const std::vector<TickSubs>& b, uint64_t* tick);
// Lowest tick present in both rings whose `xtra` differs; false when none.
bool first_diff_xtra(const std::vector<TickSubs>& a, const std::vector<TickSubs>& b, uint64_t* tick);
const TickSubs* find_tick(const std::vector<TickSubs>& v, uint64_t tick);

struct ObjDiff {
	// Same (kind, ord) on both sides, different hash or xhash.
	struct Changed {
		ObjKey local, remote;
	};
	std::vector<Changed> changed;
	// Present on one side only (per kind: the longer side's tail).
	std::vector<ObjKey> onlyLocal, onlyRemote;
	bool countMismatch = false;
};
ObjDiff diff_objs(const std::vector<ObjKey>& local, const std::vector<ObjKey>& remote);

// One text line per record (the state dump format), and per sub-hash entry.
std::string format_obj(const ObjRec& r);
std::string format_subs(const TickSubs& t);
std::string format_key(const ObjKey& k);

// The header of the sub-hash text files: column names.
const char* subs_header();

} // namespace pc_netplay_forensics
