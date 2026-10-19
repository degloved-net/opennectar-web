#ifndef PC_RANDOMIZER_H
#define PC_RANDOMIZER_H

#include <cstdint>

/*
 * Randomizer de Pikmin 2 (games/pikmin2/docs/PLAN_RANDOMIZER.md). Núcleo
 * copiado de P1 (games/pikmin1/pc_port/randomizer/): los dos juegos son
 * proyectos CMake independientes y no comparten pc_port.
 *
 * Una partida randomizada es una propiedad de su fichero: se elige en la
 * entrada "Randomizer" del título al crearlo, viaja en el bloque NRN2 de la
 * partida (los 60 bytes libres tras el fichero, dentro del checksum) y se
 * adopta de nuevo al cargarla.
 *
 * Toda decisión aleatoria sale de la semilla mediante un PRNG propio y un
 * subflujo independiente por categoría/escenario/ubicación, así que la misma
 * semilla, algoritmo y catálogo reconstruyen siempre el mismo mundo.
 */

/// Se incrementa cuando un cambio construiría otro mundo con la misma semilla.
#define PC_RANDOMIZER_ALGORITHM_VERSION (3)
/// Se incrementa cuando cambia el catálogo de ubicaciones.
#define PC_RANDOMIZER_CATALOG_VERSION (1)

/* Ajustes de la partida (plan §4.2), elegidos en "Randomizer Settings". Los
   modos de enemigos y jefes ocupan dos bits cada uno (PcRandomizerMode). */
enum PcRandomizerMode : uint32_t {
	PC_RNDMODE_Vanilla  = 0,
	PC_RNDMODE_Shuffled = 1, ///< dentro de su clase y tipo de punto de aparición
	PC_RNDMODE_Chaos    = 2, ///< cualquiera donde el tipo de punto lo permita
};

enum PcRandomizerFlag : uint32_t {
	PC_RND_ShuffleTreasures  = 1u << 0,
	PC_RND_EnemyModeShift    = 1,         ///< bits 1-2: PcRandomizerMode de enemigos
	PC_RND_BossModeShift     = 3,         ///< bits 3-4: PcRandomizerMode de jefes
	PC_RND_BossEnemyMix      = 1u << 5,   ///< jefes donde había enemigos y al revés
	PC_RND_ShuffleGlobes     = 1u << 6,   ///< Spherical Atlas y Geographic Projection (con lógica)
	PC_RND_ShuffleCandypops  = 1u << 7,   ///< con garantías de colores
	PC_RND_FixedCaveLayout   = 1u << 8,   ///< distribución de salas fija por semilla
	PC_RND_ShuffleCaveEntry  = 1u << 9,   ///< qué caveinfo abre cada agujero
	PC_RND_ShufflePlants     = 1u << 10,  ///< la flora se baraja entre sí
	PC_RND_WriteSpoilerLog   = 1u << 11,
	PC_RND_DebtShift         = 12,        ///< bits 12-15: preajuste de deuda (0 = 10.100)
};

inline uint32_t pc_rnd_enemy_mode(uint32_t flags) { return (flags >> PC_RND_EnemyModeShift) & 3u; }
inline uint32_t pc_rnd_boss_mode(uint32_t flags) { return (flags >> PC_RND_BossModeShift) & 3u; }
inline uint32_t pc_rnd_debt_preset(uint32_t flags) { return (flags >> PC_RND_DebtShift) & 0xFu; }
/// Deuda en Pokos de cada preajuste (0 = la original).
inline uint32_t pc_rnd_debt_amount(uint32_t flags)
{
	static const uint32_t kDebt[] = { 10100, 5000, 15000, 20000 };
	const uint32_t preset = pc_rnd_debt_preset(flags);
	return preset < 4 ? kDebt[preset] : kDebt[0];
}
inline uint32_t pc_rnd_with_debt_preset(uint32_t flags, uint32_t preset)
{
	return (flags & ~(0xFu << PC_RND_DebtShift)) | ((preset & 0xFu) << PC_RND_DebtShift);
}
inline uint32_t pc_rnd_with_enemy_mode(uint32_t flags, uint32_t mode)
{
	return (flags & ~(3u << PC_RND_EnemyModeShift)) | ((mode & 3u) << PC_RND_EnemyModeShift);
}
inline uint32_t pc_rnd_with_boss_mode(uint32_t flags, uint32_t mode)
{
	return (flags & ~(3u << PC_RND_BossModeShift)) | ((mode & 3u) << PC_RND_BossModeShift);
}

/// Ajustes por defecto: tesoros barajados, enemigos y jefes en caos y spoiler
/// log; el resto de opciones (fase 6) apagadas.
#define PC_RANDOMIZER_DEFAULT_FLAGS                                                         \
	(PC_RND_ShuffleTreasures | (PC_RNDMODE_Chaos << PC_RND_EnemyModeShift)                  \
	 | (PC_RNDMODE_Chaos << PC_RND_BossModeShift) | PC_RND_WriteSpoilerLog)

/// What a save file records about its randomizer. Plain data so the save
/// block code can serialise it without depending on this module.
struct PcRandomizerRules {
	bool enabled;
	uint64_t seed;
	uint16_t algorithmVersion;
	uint16_t catalogVersion;
	uint32_t flags;
	uint64_t manifestHash;
};

/* Title menu → file select. True from the moment "Randomizer" is picked on the
   title screen until the player leaves for another mode; the file screen then
   creates or continues randomized files only. */
void pc_randomizer_set_menu_pending(bool on);
bool pc_randomizer_menu_pending(void);
/// PIKI_RANDOMIZER: Story entra como Randomizer (pruebas).
bool pc_randomizer_env_forced(void);

/* Seed and settings offered on "Randomizer Settings". */
void     pc_randomizer_reroll_pending_seed(void);
void     pc_randomizer_set_pending_seed(uint64_t seed);
uint64_t pc_randomizer_pending_seed(void);
void     pc_randomizer_set_pending_flags(uint32_t flags);
uint32_t pc_randomizer_pending_flags(void);

/// Adopts the pending rules for the run that is starting (randomizer on only
/// when the file was created from the Randomizer menu).
void pc_randomizer_begin_new_run(void);
/// Adopts the rules read from a save file.
void pc_randomizer_adopt_loaded(const PcRandomizerRules& rules);
/// Leaves randomizer off (normal files, Challenge Mode, VS).
void pc_randomizer_clear_active(void);

bool pc_randomizer_active(void);
const PcRandomizerRules& pc_randomizer_rules(void);
bool pc_randomizer_flag(uint32_t flag);
uint32_t pc_randomizer_enemy_mode(void); ///< PcRandomizerMode, Vanilla si no hay Randomizer
uint32_t pc_randomizer_boss_mode(void);

/* Per-slot marks for the file-select screen, filled while listing the card. */
void pc_randomizer_note_slot(int slot, const PcRandomizerRules& rules);
bool pc_randomizer_slot(int slot);
uint64_t pc_randomizer_slot_seed(int slot);
void pc_randomizer_clear_slots(void);

/// "XXXX-XXXX-XXXX-XXXX" (hex, 20 bytes with terminator).
void pc_randomizer_format_seed(uint64_t seed, char* out, int outSize);
/// Text seeds: FNV-1a 64 followed by SplitMix64. Hex codes in the format above
/// parse back to their own value.
uint64_t pc_randomizer_seed_from_text(const char* text);

/* ─── Código compartible (plan §12) ───
   "RND2-XXXXX-XXXXX-XXXXX-XXXX": versión (4) + ajustes (16) + semilla (64) +
   verificación (8) en Crockford base32 (sin I/L/O/U; se toleran leídas como
   1/1/0). Con un código basta para construir el mismo mundo. Un código de P1
   ("RND-…") se rechaza. */
#define PC_RANDOMIZER_SHARE_CODE_LEN (28) ///< 27 caracteres + terminador
#define PC_RANDOMIZER_FLAG_MASK      (0xFFFFu)
void pc_randomizer_format_share_code(uint64_t seed, uint32_t flags, char* out, int outSize);
/// True if text is a valid code for this algorithm version (case, spaces and
/// dashes do not matter).
bool pc_randomizer_parse_share_code(const char* text, uint64_t* seed, uint32_t* flags);

/* ─── Deterministic PRNG ─── */

/// SplitMix64 step, also used to mix substream keys.
uint64_t pc_randomizer_mix64(uint64_t x);

/// Independent substream: seed + category + stage + source + location key +
/// algorithm version. Adding draws to one category never moves another.
enum PcRandomizerCategory : uint32_t {
	PC_RNDCAT_Treasures = 1,
	PC_RNDCAT_Enemies   = 2,
	PC_RNDCAT_Bosses    = 3,
	PC_RNDCAT_CaveLayout = 4,
};

/// xoroshiro128** seeded through SplitMix64.
struct PcRandomizerRng {
	uint64_t s0, s1;
	uint64_t next(void);
	/// Uniform in [0, bound), unbiased. bound 0 returns 0.
	uint32_t below(uint32_t bound);
};

PcRandomizerRng pc_randomizer_stream(uint32_t category, int stageId, uint32_t sourceTag, uint32_t locationKey);
/// Igual con una semilla explícita (validar una semilla antes de crear la partida).
PcRandomizerRng pc_randomizer_stream_for(uint64_t seed, uint32_t category, int stageId, uint32_t sourceTag,
                                         uint32_t locationKey);

/* ─── Bloque NRN2 de la partida (plan §10) ───
   Los 60 bytes de PlayerInfo entre el fichero (0x40 + PLAYER_FILE_SIZE =
   0xBFC0) y el checksum (0xBFFC). Big-endian, como el resto de la tarjeta:

     u32 magic 'NRN2', u32 version, u32 flags, u32 algoritmo<<16 | catálogo,
     u32 seedHigh, u32 seedLow, u32 manifestHashHigh, u32 manifestHashLow,
     u8 reservado[28]

   Una partida original o anterior no tiene el número mágico y se carga como
   partida normal. Interfaz C: la usa pikmin2MemoryCardMgr.cpp. */
#define PC_RANDOMIZER_BLOCK_SIZE (60)

extern "C" {
/// Escribe el bloque de la partida en curso (todo a cero si no es Randomizer).
void pc_p2_randomizer_write_block(unsigned char* block, int size);
/// Lee el bloque de la partida cargada y adopta (o apaga) el Randomizer.
void pc_p2_randomizer_adopt_block(const unsigned char* block, int size);
/// Decodifica un bloque sin adoptarlo. False si no lleva NRN2.
bool pc_p2_randomizer_read_block(const unsigned char* block, int size, PcRandomizerRules* out);
}

#endif // PC_RANDOMIZER_H
