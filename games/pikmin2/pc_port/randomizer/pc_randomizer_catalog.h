#ifndef PC_RANDOMIZER_CATALOG_H
#define PC_RANDOMIZER_CATALOG_H

#include <cstdint>

#include "pc_randomizer.h"

/*
 * Catálogo de ubicaciones de Pikmin 2 (plan §4.1 y §5.2). Los datos salen de
 * tools/randomizer_audit.py --emit-catalog (pc_randomizer_catalog_data.inc):
 * 201 tesoros y 201 ubicaciones (26 de superficie y 175 de cueva), con el
 * Waterwraith como una sola ubicación de 5 entradas.
 */

enum PcP2LocKind : uint8_t {
	PC_P2LOC_SurfaceGround, ///< {pelt} de un generador de superficie
	PC_P2LOC_SurfaceEnemy,  ///< {teki} de superficie con código de tesoro
	PC_P2LOC_CaveItem,      ///< ItemInfo de un subnivel
	PC_P2LOC_CaveTeki,      ///< TekiInfo "Enemigo_tesoro"
	PC_P2LOC_CaveCap,       ///< CapInfo "Enemigo_tesoro"
	PC_P2LOC_BossHardcoded, ///< Titan Dweevil (BigTreasure.cpp)
	PC_P2LOC_MultiEntry,    ///< misma identidad en varias entradas (Waterwraith)
};

/// Tesoro u objeto del Kit (otakara_config.txt / item_config.txt).
struct PcP2Treasure {
	const char* name;
	uint8_t kind;   ///< 3 = tesoro, 4 = objeto del Kit (PelletList::cKind)
	uint8_t index;  ///< posición en su lista
	uint16_t min;   ///< Pikmin mínimos para transportarlo
	uint16_t max;
	uint16_t money; ///< Pokos
};

/// Cueva del modo historia (CaveOtakara de stages.txt).
struct PcP2Cave {
	const char* id;   ///< "t_01"…
	const char* file; ///< caveinfo
	uint8_t course;   ///< 0 Valley of Repose, 1 Awakening Wood, 2 Perplexing Pool, 3 Wistful Wild
	uint8_t floors;
	uint8_t treasures;
};

/// Ubicación de tesoro. La clave estable (plan §4.3) es
/// curso/archivo/generador en superficie y cueva/subnivel/clase/índice en cueva.
struct PcP2Location {
	PcP2LocKind kind;
	int8_t course;
	int8_t cave;          ///< índice en la tabla de cuevas, -1 en superficie
	uint8_t floor;        ///< subnivel (1..N), 0 en superficie
	const char* file;     ///< archivo de generadores o caveinfo
	uint16_t index;       ///< generador (superficie) o índice en su lista (cueva)
	const char* carrier;  ///< enemigo que lo lleva, nullptr si está en el suelo
	int8_t genType;       ///< CaveGenType en cuevas, -1 en superficie
	uint16_t vanillaCode; ///< tipo << 8 | índice
	uint8_t weightMin;    ///< decenas del peso de caveinfo (mínimo garantizado)
	uint8_t entries;      ///< 1, o 5 para el Waterwraith
	float pos[3];         ///< posición del generador (superficie)
};

int pc_randomizer_treasure_count(void);
const PcP2Treasure& pc_randomizer_treasure(int i);
/// Índice en la tabla de tesoros, -1 si no existe.
int pc_randomizer_treasure_by_name(const char* name);
int pc_randomizer_treasure_by_code(uint16_t code);

int pc_randomizer_cave_count(void);
const PcP2Cave& pc_randomizer_cave(int i);
int pc_randomizer_cave_by_id(const char* id);

int pc_randomizer_location_count(void);
const PcP2Location& pc_randomizer_location(int i);

int pc_randomizer_cave_by_file(const char* caveinfoFile);
/// "tutorial", "forest", "yakushima", "last" -> 0..3; -1 si no es un curso.
int pc_randomizer_course_by_name(const char* name);
const char* pc_randomizer_course_name(int course);

/* ─── Especies (plan §2.7 y §7) ───
   Una entrada por EnemyTypeID. La clase decide con quién se baraja; las
   etiquetas, dónde puede ir (sacadas del uso real en los caveinfo de historia
   y de createEnemyGenerator). */
enum PcP2EnemyClass : uint8_t {
	PC_P2E_Small,    ///< terrestre pequeño
	PC_P2E_Large,    ///< terrestre grande (o fijo en el suelo: Fkabuto, FminiHoudai)
	PC_P2E_Flying,
	PC_P2E_Water,    ///< necesita agua
	PC_P2E_Hazard,   ///< géiseres, cables, rocas, bombas, huevos: siempre fijos
	PC_P2E_Flora,
	PC_P2E_Candypop,
	PC_P2E_Boss,
	PC_P2E_Internal, ///< nunca se coloca ni se cambia
};

enum PcP2EnemyTag : uint8_t {
	PC_P2ET_Carrier   = 1u << 0, ///< suelta un tesoro al morir (plan B.2)
	PC_P2ET_Drop      = 1u << 1, ///< cae del techo ($N) en el original
	PC_P2ET_Cap       = 1u << 2, ///< aparece en caps en el original
	PC_P2ET_OwnGen    = 1u << 3, ///< generador de superficie con datos propios
	PC_P2ET_Fixed     = 1u << 4, ///< nunca se cambia ni es destino (BlackMan, BigTreasure, Queen)
};

struct PcP2EnemyInfo {
	const char* name; ///< nombre de gEnemyInfo
	PcP2EnemyClass cls;
	uint8_t tags;
};

#define PC_P2_ENEMY_COUNT (102)
/// nullptr fuera de rango.
const PcP2EnemyInfo* pc_randomizer_enemy(int enemyId);

/// Dónde va un enemigo, para elegir sustituto.
struct PcP2EnemySlot {
	bool cave;          ///< false = superficie
	bool cap;           ///< CapInfo
	bool drops;         ///< $N
	bool carries;       ///< lleva tesoro
	int genType;        ///< CaveGenType (cueva)
	bool earlyCave;     ///< cueva de Valley of Repose: sus Candypops no cambian nunca
};

/// Sustituto de `original` en `slot` según los ajustes (Vanilla devuelve el
/// original). `allowBoss` = aún cabe un jefe más en este subnivel o archivo.
int pc_randomizer_pick_enemy(PcRandomizerRng& rng, uint32_t flags, int original, const PcP2EnemySlot& slot,
                             bool allowBoss);

/* ─── Subnivel de cueva (pc_randomizer_cave.cpp) ───
   Copia plana de las listas de un FloorInfo, para transformarla sin el motor
   (el juego la copia desde y hacia CaveInfo; las pruebas la leen del texto). */
struct PcP2CaveTeki {
	int enemyId;
	int weight;
	int genType;
	int dropMode; ///< 0 = no cae
	int code;     ///< tesoro que lleva (tipo << 8 | índice), 0 si ninguno
};
struct PcP2CaveCap {
	bool hasTeki;
	PcP2CaveTeki teki;
};
struct PcP2CaveItem {
	int code;
	int weight;
};
#define PC_P2_FLOOR_MAX_TEKI (64)
#define PC_P2_FLOOR_MAX_ITEM (32)
#define PC_P2_FLOOR_MAX_CAP  (32)
struct PcP2CaveFloor {
	int tekiCount;
	PcP2CaveTeki teki[PC_P2_FLOOR_MAX_TEKI];
	int itemCount;
	PcP2CaveItem item[PC_P2_FLOOR_MAX_ITEM];
	int capCount;
	PcP2CaveCap cap[PC_P2_FLOOR_MAX_CAP];
};

#define PC_P2_FLOOR_MAX_EXTRA_BOSSES (2)  ///< plan §7.3.8
#define PC_P2_FLOOR_MAX_SPECIES      (13)

/// Aplica tesoros (según `treasureAt`, el manifiesto) y enemigos (según
/// seed/flags) a un subnivel. Función pura. Devuelve cuántos tesoros no se
/// encontraron en el catálogo (se dejan como estaban). `where` etiqueta los avisos.
int pc_randomizer_transform_floor(uint64_t seed, uint32_t flags, const int16_t* treasureAt, int cave, int floor,
                                  PcP2CaveFloor& f, const char* where);

/* ─── Manifiesto (pc_randomizer_logic.cpp) ───
   Se reconstruye desde la semilla y los ajustes de la partida activa. Sin
   Randomizer, cada ubicación conserva su tesoro original. */

/// Hash del manifiesto de la partida activa (0 sin Randomizer).
uint64_t pc_randomizer_manifest_hash(void);
uint32_t pc_randomizer_manifest_attempts(void);
/// False si ningún intento pasó la lógica (se usa el reparto original).
bool pc_randomizer_manifest_ok(void);
/// Tesoro (índice de la tabla) asignado a una ubicación.
int pc_randomizer_treasure_for_location(int loc);
/// Manifiesto de la partida activa: tesoro por ubicación.
const int16_t* pc_randomizer_manifest_active(void);
/// Manifiesto de cualquier semilla y ajustes (pruebas; válido hasta la
/// siguiente llamada).
const int16_t* pc_randomizer_manifest_for(uint64_t seed, uint32_t flags);
void pc_randomizer_invalidate_manifest(void);
/// La semilla construye un mundo completable con estos ajustes.
bool pc_randomizer_seed_is_valid(uint64_t seed, uint32_t flags);
/// Construye `seeds` mundos seguidos y devuelve cuántos fallan (pruebas).
int pc_randomizer_logic_fuzz(uint64_t firstSeed, int seeds, uint32_t flags);

#endif // PC_RANDOMIZER_CATALOG_H
