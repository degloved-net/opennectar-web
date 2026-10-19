// Randomizer P2: especies y elección de sustitutos (plan §6 y §7). Sin
// dependencias del motor.
//
// Reglas, en todos los modos:
//   - peligros, Candypops (sin su flag, y nunca en las cuevas de VoR) y flora
//     (sin su flag) no cambian;
//   - agua solo por agua;
//   - una especie con generador de superficie propio (Pelplant, Qurione, Rock,
//     ElecHiba) no cambia en superficie ni es destino allí: sus datos dependen
//     de la especie (createEnemyGenerator);
//   - si lleva tesoro, el sustituto tiene que soltarlo al morir;
//   - si cae del techo ($N), el sustituto tiene que caer en el original;
//   - en caps, solo especies que aparecen en caps en el original;
//   - BlackMan, BigTreasure y Queen, fijos (plan §7.3.7); también Imomushi,
//     que busca plantas de bayas (ItemPlant, que no existe en las cuevas).

#include "pc_randomizer_catalog.h"

namespace {
#define S  PC_P2E_Small
#define L  PC_P2E_Large
#define F  PC_P2E_Flying
#define W  PC_P2E_Water
#define H  PC_P2E_Hazard
#define FL PC_P2E_Flora
#define C  PC_P2E_Candypop
#define B  PC_P2E_Boss
#define I  PC_P2E_Internal
#define CR PC_P2ET_Carrier
#define DR PC_P2ET_Drop
#define CP PC_P2ET_Cap
#define OG PC_P2ET_OwnGen
#define FX PC_P2ET_Fixed

// clang-format off
const PcP2EnemyInfo kEnemies[PC_P2_ENEMY_COUNT] = {
	/*   0 */ { "Pelplant",       FL, OG },
	/*   1 */ { "Kochappy",       S,  CR },
	/*   2 */ { "Chappy",         L,  CR },
	/*   3 */ { "BluePom",        C,  CP },
	/*   4 */ { "RedPom",         C,  CP },
	/*   5 */ { "YellowPom",      C,  CP },
	/*   6 */ { "BlackPom",       C,  CP },
	/*   7 */ { "WhitePom",       C,  CP },
	/*   8 */ { "RandPom",        C,  CP | DR },
	/*   9 */ { "Kogane",         S,  CP },
	/*  10 */ { "Wealthy",        S,  CR | CP },
	/*  11 */ { "Fart",           S,  CR | CP },
	/*  12 */ { "UjiA",           S,  CR },
	/*  13 */ { "UjiB",           S,  CR },
	/*  14 */ { "Tobi",           F,  CR },
	/*  15 */ { "Armor",          S,  CR },
	/*  16 */ { "Qurione",        F,  OG | CP | DR },
	/*  17 */ { "Frog",           L,  CR },
	/*  18 */ { "MaroFrog",       L,  CR | CP | DR },
	/*  19 */ { "Rock",           H,  OG },
	/*  20 */ { "Hiba",           H,  0 },
	/*  21 */ { "GasHiba",        H,  0 },
	/*  22 */ { "ElecHiba",       H,  OG },
	/*  23 */ { "Sarai",          F,  CR | CP | DR },
	/*  24 */ { "Tank",           L,  CR | CP | DR },
	/*  25 */ { "Wtank",          L,  CR },
	/*  26 */ { "Catfish",        W,  CR | DR },
	/*  27 */ { "Tadpole",        W,  CR | CP | DR },
	/*  28 */ { "ElecBug",        S,  CR | CP | DR },
	/*  29 */ { "Mar",            F,  CR },
	/*  30 */ { "Queen",          B,  CR | FX },
	/*  31 */ { "Baby",           I,  0 },
	/*  32 */ { "Demon",          F,  CR | CP | DR },
	/*  33 */ { "FireChappy",     L,  CR },
	/*  34 */ { "SnakeCrow",      B,  CR },
	/*  35 */ { "KumaChappy",     L,  CR | DR },
	/*  36 */ { "Bomb",           H,  CP | DR },
	/*  37 */ { "Egg",            H,  CP | DR },
	/*  38 */ { "PanModoki",      S,  CR },
	/*  39 */ { "PanModokiNest",  I,  0 },
	/*  40 */ { "OoPanModoki",    B,  CR },
	/*  41 */ { "Fuefuki",        S,  CR },
	/*  42 */ { "BlueChappy",     L,  CR | CP },
	/*  43 */ { "YellowChappy",   L,  CR },
	/*  44 */ { "BlueKochappy",   S,  CR | CP | DR },
	/*  45 */ { "YellowKochappy", S,  CR },
	/*  46 */ { "Tanpopo",        FL, 0 },
	/*  47 */ { "Clover",         FL, CP },
	/*  48 */ { "HikariKinoko",   FL, CP },
	/*  49 */ { "Ooinu_s",        FL, 0 },
	/*  50 */ { "Ooinu_l",        FL, 0 },
	/*  51 */ { "Wakame_s",       FL, 0 },
	/*  52 */ { "Wakame_l",       FL, 0 },
	/*  53 */ { "KingChappy",     B,  CR },
	/*  54 */ { "Miulin",         B,  CR },
	/*  55 */ { "Hanachirashi",   F,  CR | CP | DR },
	/*  56 */ { "Damagumo",       B,  CR },
	/*  57 */ { "Kurage",         F,  CR | CP | DR },
	/*  58 */ { "BombSarai",      F,  CR | DR },
	/*  59 */ { "FireOtakara",    S,  CR | DR },
	/*  60 */ { "WaterOtakara",   S,  CR },
	/*  61 */ { "GasOtakara",     S,  CR | CP | DR },
	/*  62 */ { "ElecOtakara",    S,  CR | CP | DR },
	/*  63 */ { "Jigumo",         S,  CR },
	/*  64 */ { "JigumoNest",     I,  0 },
	/*  65 */ { "Imomushi",       S,  CR | FX }, // vive en las plantas de bayas (ItemPlant): no en cuevas
	/*  66 */ { "Houdai",         B,  CR },
	/*  67 */ { "LeafChappy",     S,  CR },
	/*  68 */ { "TamagoMushi",    S,  CP },
	/*  69 */ { "BigFoot",        B,  CR },
	/*  70 */ { "SnakeWhole",     B,  CR },
	/*  71 */ { "UmiMushi",       B,  CR },
	/*  72 */ { "OniKurage",      F,  CR },
	/*  73 */ { "BigTreasure",    B,  CR | FX },
	/*  74 */ { "Stone",          I,  0 },
	/*  75 */ { "Kabuto",         L,  CR | DR },
	/*  76 */ { "KumaKochappy",   S,  CR },
	/*  77 */ { "ShijimiChou",    F,  0 },
	/*  78 */ { "MiniHoudai",     L,  CR },
	/*  79 */ { "Sokkuri",        S,  CR | CP },
	/*  80 */ { "Tukushi",        FL, 0 },
	/*  81 */ { "Watage",         FL, 0 },
	/*  82 */ { "Pom",            I,  0 },
	/*  83 */ { "PanHouse",       I,  0 },
	/*  84 */ { "Hana",           S,  CR },
	/*  85 */ { "DaiodoRed",      FL, 0 },
	/*  86 */ { "DaiodoGreen",    FL, 0 },
	/*  87 */ { "Magaret",        FL, 0 },
	/*  88 */ { "Nekojarashi",    FL, 0 },
	/*  89 */ { "Chiyogami",      FL, 0 },
	/*  90 */ { "Zenmai",         FL, 0 },
	/*  91 */ { "KareOoinu_s",    FL, CP },
	/*  92 */ { "KareOoinu_l",    FL, 0 },
	/*  93 */ { "BombOtakara",    S,  CR | CP | DR },
	/*  94 */ { "DangoMushi",     B,  CR },
	/*  95 */ { "Rkabuto",        L,  CR },
	/*  96 */ { "Fkabuto",        L,  CR },
	/*  97 */ { "FminiHoudai",    L,  CR },
	/*  98 */ { "Tyre",           I,  0 },
	/*  99 */ { "BlackMan",       B,  CR | FX },
	/* 100 */ { "UmiMushiBase",   I,  0 },
	/* 101 */ { "UmiMushiBlind",  L,  CR },
};
// clang-format on

#undef S
#undef L
#undef F
#undef W
#undef H
#undef FL
#undef C
#undef B
#undef I
#undef CR
#undef DR
#undef CP
#undef OG
#undef FX

bool isMobile(PcP2EnemyClass c) { return c == PC_P2E_Small || c == PC_P2E_Large || c == PC_P2E_Flying; }

/// La especie original no se toca.
bool keepOriginal(const PcP2EnemyInfo& e, uint32_t flags, const PcP2EnemySlot& slot)
{
	if (e.tags & PC_P2ET_Fixed) return true;
	if (!slot.cave && (e.tags & PC_P2ET_OwnGen)) return true;
	switch (e.cls) {
	case PC_P2E_Hazard:
	case PC_P2E_Internal:
		return true;
	case PC_P2E_Flora:
		return !(flags & PC_RND_ShufflePlants);
	case PC_P2E_Candypop:
		// Los de VoR son la primera fuente de morados y blancos (plan §7.4).
		return !(flags & PC_RND_ShuffleCandypops) || slot.earlyCave;
	case PC_P2E_Boss:
		return pc_rnd_boss_mode(flags) == PC_RNDMODE_Vanilla;
	default:
		return pc_rnd_enemy_mode(flags) == PC_RNDMODE_Vanilla;
	}
}

/// `cand` puede sustituir a `orig` en `slot`.
bool compatible(const PcP2EnemyInfo& orig, const PcP2EnemyInfo& cand, uint32_t flags, const PcP2EnemySlot& slot,
                bool allowBoss)
{
	if (cand.tags & PC_P2ET_Fixed) return false;
	if (!slot.cave && (cand.tags & PC_P2ET_OwnGen)) return false;
	if (slot.carries && !(cand.tags & PC_P2ET_Carrier)) return false;
	if (slot.drops && !(cand.tags & PC_P2ET_Drop)) return false;
	if (slot.cap && !(cand.tags & PC_P2ET_Cap)) return false;

	switch (orig.cls) {
	case PC_P2E_Flora:
	case PC_P2E_Candypop:
	case PC_P2E_Water:
		return cand.cls == orig.cls;
	case PC_P2E_Boss: {
		if (cand.cls == PC_P2E_Boss) return true;
		// Un jefe puede pasar a enemigo normal con la mezcla.
		return (flags & PC_RND_BossEnemyMix) && isMobile(cand.cls) && pc_rnd_enemy_mode(flags) == PC_RNDMODE_Chaos;
	}
	default: {
		if (cand.cls == PC_P2E_Boss) {
			// Jefes donde había enemigos: solo con la mezcla, en puntos de
			// enemigo grande o especial y con cupo.
			return (flags & PC_RND_BossEnemyMix) && allowBoss && pc_rnd_boss_mode(flags) != PC_RNDMODE_Vanilla
			       && !slot.cap && !slot.drops && (!slot.cave || slot.genType == 1 || slot.genType == 8);
		}
		if (pc_rnd_enemy_mode(flags) == PC_RNDMODE_Shuffled) return cand.cls == orig.cls;
		return isMobile(cand.cls);
	}
	}
}
}

const PcP2EnemyInfo* pc_randomizer_enemy(int enemyId)
{
	return (enemyId >= 0 && enemyId < PC_P2_ENEMY_COUNT) ? &kEnemies[enemyId] : nullptr;
}

int pc_randomizer_pick_enemy(PcRandomizerRng& rng, uint32_t flags, int original, const PcP2EnemySlot& slot,
                             bool allowBoss)
{
	const PcP2EnemyInfo* orig = pc_randomizer_enemy(original);
	if (!orig || keepOriginal(*orig, flags, slot)) return original;
	int pool[PC_P2_ENEMY_COUNT];
	int n = 0;
	for (int id = 0; id < PC_P2_ENEMY_COUNT; id++) {
		if (compatible(*orig, kEnemies[id], flags, slot, allowBoss)) pool[n++] = id;
	}
	if (n == 0) return original;
	return pool[rng.below(uint32_t(n))];
}
