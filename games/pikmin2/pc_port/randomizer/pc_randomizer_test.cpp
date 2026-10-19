// Pruebas de host del Randomizer P2 (sin juego): bloque NRN2, código RND2,
// catálogo, lógica (10.000 semillas), reglas de sustitución de enemigos y, si
// están los assets, la transformación de cada subnivel de las 14 cuevas de
// historia leyendo sus caveinfo reales.

#include "pc_randomizer.h"
#include "pc_randomizer_catalog.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
int sFailures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		sFailures++;
	}
}

void testBlock()
{
	unsigned char block[PC_RANDOMIZER_BLOCK_SIZE];
	// Sin Randomizer: todo a cero y se carga como partida normal.
	pc_randomizer_clear_active();
	std::memset(block, 0xAB, sizeof(block));
	pc_p2_randomizer_write_block(block, sizeof(block));
	bool zero = true;
	for (unsigned char b : block) zero = zero && b == 0;
	check(zero, "bloque vacío sin Randomizer");
	PcRandomizerRules r;
	check(!pc_p2_randomizer_read_block(block, sizeof(block), &r), "bloque vacío no se lee");

	// Partida Randomizer: ida y vuelta.
	pc_randomizer_set_menu_pending(true);
	pc_randomizer_set_pending_seed(0x0123456789ABCDEFull);
	pc_randomizer_set_pending_flags(PC_RANDOMIZER_DEFAULT_FLAGS);
	pc_randomizer_begin_new_run();
	pc_randomizer_set_menu_pending(false);
	const PcRandomizerRules saved = pc_randomizer_rules();
	pc_p2_randomizer_write_block(block, sizeof(block));
	pc_randomizer_clear_active();
	pc_p2_randomizer_adopt_block(block, sizeof(block));
	check(pc_randomizer_active(), "adopta el bloque");
	check(pc_randomizer_rules().seed == saved.seed, "semilla de ida y vuelta");
	check(pc_randomizer_rules().flags == saved.flags, "ajustes de ida y vuelta");
	check(pc_randomizer_rules().manifestHash == saved.manifestHash && saved.manifestHash != 0, "hash del manifiesto");

	// Basura de una partida original (sin número mágico).
	std::memset(block, 0x5A, sizeof(block));
	pc_p2_randomizer_adopt_block(block, sizeof(block));
	check(!pc_randomizer_active(), "basura sin NRN2 = partida normal");
}

void testShareCode()
{
	const uint64_t seeds[] = { 0, 1, 0xFFFFFFFFFFFFFFFFull, 0x0123456789ABCDEFull };
	const uint32_t flags[] = { 0, PC_RANDOMIZER_DEFAULT_FLAGS, 0xFFFF };
	for (uint64_t s : seeds) {
		for (uint32_t f : flags) {
			char code[PC_RANDOMIZER_SHARE_CODE_LEN];
			pc_randomizer_format_share_code(s, f, code, sizeof(code));
			check(std::strncmp(code, "RND2-", 5) == 0 && std::strlen(code) == 27, "formato RND2");
			uint64_t s2 = 0;
			uint32_t f2 = 0;
			check(pc_randomizer_parse_share_code(code, &s2, &f2) && s2 == s && f2 == f, "código de ida y vuelta");
			code[7] = code[7] == 'A' ? 'B' : 'A';
			check(!pc_randomizer_parse_share_code(code, nullptr, nullptr), "código alterado rechazado");
		}
	}
	check(!pc_randomizer_parse_share_code("RND-000000-000000-000000", nullptr, nullptr), "código de P1 rechazado");
}

void testCatalog()
{
	check(pc_randomizer_treasure_count() == 201, "201 tesoros");
	check(pc_randomizer_location_count() == 201, "201 ubicaciones");
	int perCave[32] = {};
	for (int i = 0; i < pc_randomizer_location_count(); i++) {
		const PcP2Location& l = pc_randomizer_location(i);
		check(pc_randomizer_treasure_by_code(l.vanillaCode) >= 0, "código original conocido");
		if (l.cave >= 0) perCave[l.cave]++;
	}
	for (int c = 0; c < pc_randomizer_cave_count(); c++) {
		check(perCave[c] == pc_randomizer_cave(c).treasures, "tesoros por cueva = stages.txt");
	}
}

void testLogic()
{
	const uint32_t base = PC_RANDOMIZER_DEFAULT_FLAGS;
	check(pc_randomizer_logic_fuzz(1, 10000, base) == 0, "10.000 semillas completables");
	check(pc_randomizer_logic_fuzz(2, 2000, base | PC_RND_ShuffleGlobes) == 0, "globos barajados");
	for (uint32_t preset = 1; preset < 4; preset++) {
		check(pc_randomizer_logic_fuzz(3, 2000, pc_rnd_with_debt_preset(base, preset)) == 0, "deudas preajustadas");
	}
}

void testEnemies()
{
	for (uint32_t mode = 0; mode < 3; mode++) {
		const uint32_t flags = pc_rnd_with_boss_mode(pc_rnd_with_enemy_mode(PC_RANDOMIZER_DEFAULT_FLAGS, mode), mode)
		                       | PC_RND_BossEnemyMix | PC_RND_ShufflePlants;
		for (int orig = 0; orig < PC_P2_ENEMY_COUNT; orig++) {
			const PcP2EnemyInfo* o = pc_randomizer_enemy(orig);
			for (int k = 0; k < 64; k++) {
				PcP2EnemySlot slot = {};
				slot.cave          = (k & 1) != 0;
				slot.cap           = (k & 2) != 0 && slot.cave;
				slot.drops         = (k & 4) != 0 && slot.cave;
				slot.carries       = (k & 8) != 0;
				slot.genType       = (k >> 4) & 1 ? 8 : 0;
				PcRandomizerRng rng = pc_randomizer_stream_for(uint64_t(k) * 7919 + orig, PC_RNDCAT_Enemies, 0, 0, 0);
				const int now         = pc_randomizer_pick_enemy(rng, flags, orig, slot, true);
				const PcP2EnemyInfo* n = pc_randomizer_enemy(now);
				if (now == orig) continue;
				check(mode != PC_RNDMODE_Vanilla || o->cls == PC_P2E_Flora, "Vanilla solo cambia flora (con su flag)");
				check(!(o->tags & PC_P2ET_Fixed) && !(n->tags & PC_P2ET_Fixed), "fijos intactos");
				check(o->cls != PC_P2E_Hazard && o->cls != PC_P2E_Internal && n->cls != PC_P2E_Internal, "peligros e internos");
				check(!slot.carries || (n->tags & PC_P2ET_Carrier), "portador suelta el tesoro");
				check(!slot.drops || (n->tags & PC_P2ET_Drop), "$N solo especies que caen");
				check(!slot.cap || (n->tags & PC_P2ET_Cap), "caps solo especies de cap");
				check(slot.cave || !(n->tags & PC_P2ET_OwnGen), "generador propio en superficie");
				check((o->cls == PC_P2E_Water) == (n->cls == PC_P2E_Water), "agua por agua");
			}
		}
	}
}

// ─── Cuevas: lector de caveinfo (las mismas reglas que gameCaveInfo.cpp) ───

int sEnemyChanges = 0;
int sBossMoves    = 0;

struct VanillaFloor {
	int cave;
	int floor;
	PcP2CaveFloor data;
};

std::vector<std::string> caveTokens(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	std::vector<std::string> out;
	std::string line;
	while (std::getline(in, line)) {
		const size_t cut = line.find('#');
		if (cut != std::string::npos) line.resize(cut);
		std::istringstream ss(line);
		std::string tok;
		while (ss >> tok) out.push_back(tok);
	}
	return out;
}

int enemyIdByName(const std::string& name)
{
	// getEnemyID compara sin mayúsculas ("RKabuto" y "Rkabuto").
	for (int id = 0; id < PC_P2_ENEMY_COUNT; id++) {
		const char* a = pc_randomizer_enemy(id)->name;
		if (name.size() == std::strlen(a)) {
			bool same = true;
			for (size_t i = 0; i < name.size() && same; i++) same = std::tolower(name[i]) == std::tolower(a[i]);
			if (same) return id;
		}
	}
	return -1;
}

bool isEnemyNameExact(const std::string& name)
{
	for (int id = 0; id < PC_P2_ENEMY_COUNT; id++) {
		if (name == pc_randomizer_enemy(id)->name) return true;
	}
	return false;
}

/// TekiInfo::read: "$"/"$N" de caída y "Enemigo_tesoro" partido por el primer
/// '_' que sigue a un nombre de enemigo conocido (comparación exacta).
PcP2CaveTeki parseTeki(const std::string& raw, int weight, int type)
{
	PcP2CaveTeki t = {};
	std::string s  = raw;
	if (!s.empty() && s[0] == '$') {
		if (s.size() > 1 && s[1] >= '1' && s[1] <= '9') {
			t.dropMode = s[1] - '0';
			s          = s.substr(2);
		} else {
			t.dropMode = 1;
			s          = s.substr(1);
		}
	}
	std::string enemy, treasure;
	bool inEnemy = true;
	for (char c : s) {
		if (c == '_' && inEnemy && isEnemyNameExact(enemy)) {
			inEnemy = false;
			continue;
		}
		(inEnemy ? enemy : treasure) += c;
	}
	t.enemyId = enemyIdByName(enemy);
	if (!treasure.empty()) {
		const int tr = pc_randomizer_treasure_by_name(treasure.c_str());
		if (tr >= 0) t.code = (pc_randomizer_treasure(tr).kind << 8) | pc_randomizer_treasure(tr).index;
	}
	t.weight  = weight;
	t.genType = type;
	return t;
}

/// ItemInfo: PelletMgr::getCaveID, el primer tesoro cuyo nombre empieza por name.
int itemCode(const std::string& name)
{
	for (int i = 0; i < pc_randomizer_treasure_count(); i++) {
		const PcP2Treasure& t = pc_randomizer_treasure(i);
		if (std::strncmp(t.name, name.c_str(), name.size()) == 0) return (t.kind << 8) | t.index;
	}
	return 0;
}

bool readCave(int cave, std::vector<VanillaFloor>& out)
{
	const std::string path = std::string("assets/user/Mukki/mapunits/caveinfo/") + pc_randomizer_cave(cave).file;
	const std::vector<std::string> tok = caveTokens(path);
	if (tok.empty()) return false;
	size_t i   = 0;
	auto next  = [&]() -> const std::string& {
		static const std::string empty;
		return i < tok.size() ? tok[i++] : empty;
	};
	auto num   = [&]() { return std::atoi(next().c_str()); };
	auto parms = [&](int* f0, int* f1, int* ver) {
		next(); // {
		for (;;) {
			const std::string key = next();
			if (key == "{_eof}" || key.empty()) break;
			next();
			const std::string val = next();
			if (key == "{f000}") *f0 = std::atoi(val.c_str());
			if (key == "{f001}") *f1 = std::atoi(val.c_str());
			if (key == "{f015}") *ver = std::atoi(val.c_str());
		}
		next(); // }
	};
	int dummy0 = 0, dummy1 = 0, dummyV = 0;
	parms(&dummy0, &dummy1, &dummyV);
	const int floors = num();
	for (int fl = 0; fl < floors; fl++) {
		int f0 = 0, f1 = 0, ver = 0;
		parms(&f0, &f1, &ver);
		PcP2CaveFloor d = {};
		next();
		const int nTeki = num();
		for (int k = 0; k < nTeki; k++) {
			const std::string name = next();
			const int w             = num();
			const int type          = num();
			if (d.tekiCount < PC_P2_FLOOR_MAX_TEKI) d.teki[d.tekiCount++] = parseTeki(name, w, type);
		}
		next();
		next();
		const int nItem = num();
		for (int k = 0; k < nItem; k++) {
			const std::string name = next();
			const int w             = num();
			if (d.itemCount < PC_P2_FLOOR_MAX_ITEM) d.item[d.itemCount++] = { itemCode(name), w };
		}
		next();
		next();
		const int nGate = num();
		for (int k = 0; k < nGate; k++) {
			next();
			next();
			next();
		}
		next();
		if (ver >= 1) {
			next();
			const int nCap = num();
			for (int k = 0; k < nCap; k++) {
				PcP2CaveCap c = {};
				c.hasTeki     = num() == 0;
				if (c.hasTeki) {
					const std::string name = next();
					const int w             = num();
					const int type          = num();
					c.teki                  = parseTeki(name, w, type);
				}
				if (d.capCount < PC_P2_FLOOR_MAX_CAP) d.cap[d.capCount++] = c;
			}
			next();
		}
		for (int floor = f0 + 1; floor <= f1 + 1; floor++) out.push_back({ cave, floor, d });
	}
	return true;
}

/// Comprueba un subnivel transformado contra el original.
void checkFloor(const VanillaFloor& v, const PcP2CaveFloor& after, uint32_t flags, char* where)
{
	const bool vor   = pc_randomizer_cave(v.cave).course == 0;
	int extraBosses  = 0;
	std::set<int> species;
	auto checkTeki = [&](const PcP2CaveTeki& a, const PcP2CaveTeki& b, bool cap) {
		check(a.weight == b.weight && a.genType == b.genType && a.dropMode == b.dropMode, "cueva: peso, tipo y caída intactos");
		check((a.code == 0) == (b.code == 0), "cueva: lleva tesoro igual que antes");
		species.insert(b.enemyId);
		if (a.enemyId == b.enemyId) return;
		const PcP2EnemyInfo* o = pc_randomizer_enemy(a.enemyId);
		const PcP2EnemyInfo* n = pc_randomizer_enemy(b.enemyId);
		if (!o || !n) {
			check(false, "cueva: especie desconocida");
			return;
		}
		const int failuresBefore = sFailures;
		sEnemyChanges++;
		if (o->cls == PC_P2E_Boss || n->cls == PC_P2E_Boss) sBossMoves++;
		if (o->cls != PC_P2E_Boss && n->cls == PC_P2E_Boss) extraBosses++;
		check(!(o->tags & PC_P2ET_Fixed) && !(n->tags & PC_P2ET_Fixed), "cueva: fijos intactos");
		check(o->cls != PC_P2E_Hazard && o->cls != PC_P2E_Internal && n->cls != PC_P2E_Internal, "cueva: peligros e internos");
		check(b.code == 0 || (n->tags & PC_P2ET_Carrier), "cueva: el portador suelta el tesoro");
		check(b.dropMode == 0 || (n->tags & PC_P2ET_Drop), "cueva: $N solo especies que caen");
		check(!cap || (n->tags & PC_P2ET_Cap), "cueva: caps solo especies de cap");
		check((o->cls == PC_P2E_Water) == (n->cls == PC_P2E_Water), "cueva: agua por agua");
		check(o->cls != PC_P2E_Flora || (flags & PC_RND_ShufflePlants), "cueva: flora sin su opción");
		check(o->cls != PC_P2E_Candypop || ((flags & PC_RND_ShuffleCandypops) && !vor), "cueva: Candypops sin su opción o en VoR");
		if (pc_rnd_enemy_mode(flags) == PC_RNDMODE_Shuffled && o->cls != PC_P2E_Boss && n->cls != PC_P2E_Boss) {
			check(o->cls == n->cls, "cueva: Shuffled mantiene la clase");
		}
		if (sFailures != failuresBefore) std::printf("  en %s: %s -> %s\n", where, o->name, n->name);
	};
	for (int i = 0; i < after.tekiCount; i++) checkTeki(v.data.teki[i], after.teki[i], false);
	for (int i = 0; i < after.capCount; i++) {
		if (after.cap[i].hasTeki) checkTeki(v.data.cap[i].teki, after.cap[i].teki, true);
	}
	for (int i = 0; i < after.itemCount; i++) check(after.item[i].weight == v.data.item[i].weight, "cueva: peso de item intacto");
	check(int(species.size()) <= PC_P2_FLOOR_MAX_SPECIES, "cueva: máximo 13 especies por subnivel");
	check(extraBosses <= PC_P2_FLOOR_MAX_EXTRA_BOSSES, "cueva: máximo 2 jefes extra por subnivel");
}

void testCaves()
{
	std::vector<VanillaFloor> floors;
	for (int c = 0; c < pc_randomizer_cave_count(); c++) {
		if (!readCave(c, floors)) {
			std::printf("SKIP cuevas: no se encuentran los caveinfo (assets/)\n");
			return;
		}
	}
	std::printf("cuevas: %d subniveles leídos\n", int(floors.size()));

	const uint32_t base = PC_RANDOMIZER_DEFAULT_FLAGS;
	std::vector<uint32_t> flagSets;
	for (uint32_t mode = 0; mode < 3; mode++) {
		const uint32_t f = pc_rnd_with_boss_mode(pc_rnd_with_enemy_mode(base, mode), mode);
		flagSets.push_back(f);
		flagSets.push_back(f | PC_RND_BossEnemyMix | PC_RND_ShufflePlants | PC_RND_ShuffleCandypops | PC_RND_ShuffleGlobes);
	}
	const int kSeeds = 300;
	int checkedFloors = 0;
	for (uint32_t flags : flagSets) {
		sEnemyChanges = sBossMoves = 0;
		for (int s = 0; s < kSeeds; s++) {
			const int before          = sFailures;
			const uint64_t seed       = pc_randomizer_mix64(0xC0FFEEull + uint64_t(s));
			const int16_t* treasureAt = pc_randomizer_manifest_for(seed, flags);
			std::set<int> identities;
			std::vector<std::set<int>> perCave(pc_randomizer_cave_count());
			for (const VanillaFloor& v : floors) {
				char where[64];
				std::snprintf(where, sizeof(where), "%s %d (seed %d, flags %04X)", pc_randomizer_cave(v.cave).id, v.floor, s,
				              unsigned(flags));
				PcP2CaveFloor a = v.data, b = v.data;
				const int misses = pc_randomizer_transform_floor(seed, flags, treasureAt, v.cave, v.floor, a, where);
				pc_randomizer_transform_floor(seed, flags, treasureAt, v.cave, v.floor, b, where);
				check(misses == 0, "cueva: todos los tesoros están en el catálogo");
				check(std::memcmp(&a, &b, sizeof(a)) == 0, "cueva: transformación determinista");
				checkFloor(v, a, flags, where);
				for (int i = 0; i < a.tekiCount; i++) {
					if (a.teki[i].code) perCave[v.cave].insert(a.teki[i].code);
				}
				for (int i = 0; i < a.itemCount; i++) perCave[v.cave].insert(a.item[i].code);
				for (int i = 0; i < a.capCount; i++) {
					if (a.cap[i].hasTeki && a.cap[i].teki.code) perCave[v.cave].insert(a.cap[i].teki.code);
				}
				checkedFloors++;
				if (sFailures > before + 20) return;
			}
			// Tesoros: cada cueva conserva su número y ninguno se repite entre
			// cuevas (el Titan Dweevil lleva 5 fijados fuera de caveinfo).
			int caveTotal = 0;
			for (int c = 0; c < pc_randomizer_cave_count(); c++) {
				int expected = pc_randomizer_cave(c).treasures;
				if (std::strcmp(pc_randomizer_cave(c).id, "l_03") == 0) expected -= 5;
				check(int(perCave[c].size()) == expected, "cueva: número de tesoros = stages.txt");
				for (int code : perCave[c]) {
					check(identities.insert(code).second, "cueva: tesoro repetido entre cuevas");
				}
				caveTotal += int(perCave[c].size());
			}
			check(caveTotal == 175 - 5, "cuevas: 170 tesoros en caveinfo");
		}
		std::printf("cuevas: flags %04X: %d cambios de especie (%d con jefes) en %d semillas\n", unsigned(flags),
		            sEnemyChanges, sBossMoves, kSeeds);
		if (pc_rnd_enemy_mode(flags) == PC_RNDMODE_Vanilla) check(sEnemyChanges == 0 || (flags & PC_RND_ShufflePlants), "cuevas: Vanilla no cambia");
		else check(sEnemyChanges > 0 && sBossMoves > 0, "cuevas: Shuffled/Chaos cambian enemigos y jefes");
	}
	std::printf("cuevas: %d subniveles transformados y comprobados\n", checkedFloors);
}
}

int main()
{
	testBlock();
	testShareCode();
	testCatalog();
	testLogic();
	testEnemies();
	testCaves();
	std::printf("%s (%d failures)\n", sFailures ? "FAILED" : "OK", sFailures);
	return sFailures ? 1 : 0;
}
