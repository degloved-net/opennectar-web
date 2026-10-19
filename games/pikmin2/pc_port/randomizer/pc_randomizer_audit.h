#ifndef PC_RANDOMIZER_AUDIT_H
#define PC_RANDOMIZER_AUDIT_H

/*
 * Randomizer P2: enganches en el código del juego.
 *
 *   baseGameSection.cpp  pc_p2_rnd_gen_file (antes de leer cada archivo de
 *                        generadores) y pc_p2_rnd_cave_loaded (tras
 *                        CaveInfo::load, antes de createRandomMap)
 *   gameGenerator.cpp    pc_p2_rnd_gen_index (cada generador del archivo)
 *   genEnemy.cpp         pc_p2_rnd_gen_enemy (especie y tesoro, antes de setUse)
 *   genPellet.cpp        pc_p2_rnd_gen_pellet (tesoro de un pellet)
 *   BigTreasure.cpp      pc_p2_rnd_big_treasure_name (armas del Titan Dweevil)
 *
 * Con PIKI_RND_AUDIT=1, además, vuelcan lo que el juego lee (plan §5.1).
 */

namespace Game {
namespace Cave {
struct CaveInfo;
}
}

extern "C" {
/// Archivo que va a leer un GeneratorMgr (nullptr al terminar).
void pc_p2_rnd_gen_file(const char* path);
/// Índice del generador que se lee dentro del archivo.
void pc_p2_rnd_gen_index(int index);
/// GenObjectEnemy::doRead: especie y código de tesoro, ya sustituibles.
void pc_p2_rnd_gen_enemy(int* enemyId, int* otakaraCode);
/// GenPellet::doRead: gestor (3 tesoro, 4 Kit) e índice, ya sustituibles.
void pc_p2_rnd_gen_pellet(int* mgrId, int* index);
/// Semilla del generador del juego para las salas de un subnivel (opción de
/// cuevas fijas), o 0 si se deja como en el original.
unsigned pc_p2_rnd_cave_layout_seed(const char* caveinfoFile, int floorIndex);
/// Código RND2 de la partida si hay que enseñarlo (juego en pausa).
bool pc_p2_rnd_pause_code(char* out, int outSize);
/// Atajos de prueba (PIKI_RND_DEBUG=1, pc_randomizer_debug.cpp): en el exec
/// de los estados de superficie (0) y cueva (1).
void pc_p2_rnd_debug_exec(int inCave);
/// Línea de ayuda o resultado del último atajo; nullptr si están apagados.
const char* pc_p2_rnd_debug_status(void);
/// Ajusta la deuda del juego a la de la partida activa (al empezar o cargar).
void pc_p2_rnd_sync_debt(void);
/// Nombre del arma `i` (0 elec, 1 fire, 2 gas, 3 water) del Titan Dweevil.
const char* pc_p2_rnd_big_treasure_name(int i, const char* vanilla);
}

/// Tras CaveInfo::load y antes de createRandomMap: aplica el Randomizer al
/// subnivel que se va a generar.
void pc_p2_rnd_cave_loaded(const char* file, Game::Cave::CaveInfo* info, int floorIndex);

/* Interno (pc_randomizer_audit.cpp). */
bool pc_p2_rnd_audit_on(void);
/// Archivo, curso e índice del generador en lectura (nullptr fuera de un archivo).
const char* pc_p2_rnd_gen_context(int* course, int* index);
/// Cambia cada vez que empieza la lectura de un archivo.
int pc_p2_rnd_gen_serial(void);
void pc_p2_rnd_audit_gen(const char* what, int enemyId, int code, int newEnemyId, int newCode);
void pc_p2_rnd_audit_cave(const char* file, Game::Cave::CaveInfo* info, int floorIndex);

#endif // PC_RANDOMIZER_AUDIT_H
