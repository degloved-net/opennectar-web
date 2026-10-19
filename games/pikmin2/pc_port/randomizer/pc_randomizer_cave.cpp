// Randomizer P2: transformación de un subnivel de cueva (plan §5.4 y §7). Sin
// dependencias del motor: la usan el juego (pc_randomizer_apply.cpp, sobre una
// copia del FloorInfo) y las pruebas de host (sobre los caveinfo reales).
//
// Es una función pura de la semilla, los ajustes y la ubicación: el caveinfo
// se vuelve a cargar al entrar en cada subnivel y tiene que salir igual.

#include "pc_randomizer_catalog.h"

#include <cstdio>
#include <cstring>

namespace {
bool isTreasureCode(int code) { return (code >> 8) == 3 || (code >> 8) == 4; }

int findCave(int cave, int floor, PcP2LocKind kind, int index, int code)
{
	for (int i = 0; i < pc_randomizer_location_count(); i++) {
		const PcP2Location& l = pc_randomizer_location(i);
		if (l.cave == cave && l.floor == floor && l.kind == kind && l.index == index) return i;
	}
	// Misma identidad en varias entradas (Waterwraith): una sola ubicación.
	for (int i = 0; i < pc_randomizer_location_count(); i++) {
		const PcP2Location& l = pc_randomizer_location(i);
		if (l.cave == cave && l.kind == PC_P2LOC_MultiEntry && l.vanillaCode == code) return i;
	}
	return -1;
}

/// Tesoro nuevo, o el mismo si la ubicación no existe o no coincide con el
/// catálogo (assets cambiados).
int substitute(const int16_t* treasureAt, int loc, int code, const char* where, int* misses)
{
	if (loc < 0 || pc_randomizer_location(loc).vanillaCode != code) {
		std::printf("randomizer: WARNING %s: treasure 0x%03X not in catalog, kept\n", where, unsigned(code));
		(*misses)++;
		return code;
	}
	const int t = treasureAt[loc];
	if (t < 0) return code;
	const PcP2Treasure& tr = pc_randomizer_treasure(t);
	return (tr.kind << 8) | tr.index;
}

struct Species {
	int ids[PC_P2_ENEMY_COUNT];
	int count;
	bool has(int id) const
	{
		for (int i = 0; i < count; i++) {
			if (ids[i] == id) return true;
		}
		return false;
	}
	void add(int id)
	{
		if (!has(id) && count < PC_P2_ENEMY_COUNT) ids[count++] = id;
	}
};

void randomizeTeki(uint64_t seed, uint32_t flags, int cave, int floor, bool cap, int listIndex, PcP2CaveTeki& t,
                   Species& species, int& extraBosses)
{
	const int was      = t.enemyId;
	PcP2EnemySlot slot = {};
	slot.cave          = true;
	slot.cap           = cap;
	slot.drops         = t.dropMode != 0;
	slot.carries       = t.code != 0;
	slot.genType       = t.genType;
	slot.earlyCave     = pc_randomizer_cave(cave).course == 0;
	PcRandomizerRng rng = pc_randomizer_stream_for(seed, PC_RNDCAT_Enemies, 100 + cave, uint32_t(floor),
	                                               (cap ? 0x10000u : 0u) | uint32_t(listIndex));
	int now = pc_randomizer_pick_enemy(rng, flags, was, slot, extraBosses < PC_P2_FLOOR_MAX_EXTRA_BOSSES);
	if (now != was && !species.has(now) && species.count >= PC_P2_FLOOR_MAX_SPECIES) now = was;
	const PcP2EnemyInfo* a = pc_randomizer_enemy(was);
	const PcP2EnemyInfo* b = pc_randomizer_enemy(now);
	if (!a || !b) return;
	if (a->cls != PC_P2E_Boss && b->cls == PC_P2E_Boss) extraBosses++;
	species.add(now);
	t.enemyId = now;
}
}

int pc_randomizer_transform_floor(uint64_t seed, uint32_t flags, const int16_t* treasureAt, int cave, int floor,
                                  PcP2CaveFloor& f, const char* where)
{
	int misses = 0;

	// Tesoros.
	for (int i = 0; i < f.tekiCount; i++) {
		PcP2CaveTeki& t = f.teki[i];
		if (isTreasureCode(t.code)) t.code = substitute(treasureAt, findCave(cave, floor, PC_P2LOC_CaveTeki, i, t.code), t.code, where, &misses);
	}
	for (int i = 0; i < f.itemCount; i++) {
		PcP2CaveItem& it = f.item[i];
		it.code = substitute(treasureAt, findCave(cave, floor, PC_P2LOC_CaveItem, i, it.code), it.code, where, &misses);
	}
	for (int i = 0; i < f.capCount; i++) {
		PcP2CaveTeki& t = f.cap[i].teki;
		if (f.cap[i].hasTeki && isTreasureCode(t.code)) {
			t.code = substitute(treasureAt, findCave(cave, floor, PC_P2LOC_CaveCap, i, t.code), t.code, where, &misses);
		}
	}

	// Enemigos y jefes (plan §7). Las especies originales cuentan para el tope.
	Species species = {};
	int extraBosses = 0;
	for (int i = 0; i < f.tekiCount; i++) species.add(f.teki[i].enemyId);
	for (int i = 0; i < f.capCount; i++) {
		if (f.cap[i].hasTeki) species.add(f.cap[i].teki.enemyId);
	}
	for (int i = 0; i < f.tekiCount; i++) randomizeTeki(seed, flags, cave, floor, false, i, f.teki[i], species, extraBosses);
	for (int i = 0; i < f.capCount; i++) {
		if (f.cap[i].hasTeki) randomizeTeki(seed, flags, cave, floor, true, i, f.cap[i].teki, species, extraBosses);
	}
	return misses;
}
