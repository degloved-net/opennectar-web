#pragma once

// Mapas VS de Open Nectar (map.json, formato 1; docs/PLAN_EDITOR_MAPAS.md §3).
//
// Dos niveles:
//  - MapDoc: lo que guarda el JSON, tal cual (el editor lo modifica y lo
//    vuelve a escribir). Con simetría, las listas neutrales solo traen el
//    lado de J1 y los muros pueden ser arcos.
//  - MapData: el mapa ya desplegado (gemelos de J2, arcos en tramos,
//    grados en radianes) que compilan el juego y el editor.
// No depende del juego.

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

// ── Documento (map.json) ───────────────────────────────────────────────────

/// Reacción opcional de un elemento interactuable. El disparador se deduce
/// del dueño: derrota para enemigos, destrucción para puertas y finalización
/// para puentes, rampas y bloques.
struct DocReaction {
	std::string action; ///< "enemy", "pellet", "bomb" o "nectar"; vacío = ninguna
	std::string value;  ///< tipo de enemigo o id de pellet, según action
	int count     = 1;
	double radius = 60; ///< separación alrededor del elemento al generar varios
};

struct DocWall {
	bool isArc = false;
	double ax = 0, az = 0, bx = 0, bz = 0; ///< tramo recto
	double cx = 0, cz = 0, radius = 0, from = 0, to = 0; ///< arco (grados)
	int segments    = 3;
	double thickness = 40, height = 150;
};

struct DocPond {
	double cx = 0, cz = 0, rx = 200, rz = 150, island = 0, depth = 20, surface = -4;
};

struct DocSpot {
	int player  = 0;
	double x = 0, z = 0;
	double face = 0; ///< grados
	DocReaction reaction;
};

struct DocOnion {
	int player = 0;
	int color  = 0; ///< 0 azul, 1 roja, 2 amarilla
	double x = 0, z = 0;
};

struct DocPellet {
	std::string id = "pr01";
	double x = 0, z = 0;
};

struct DocPiece {
	int kind = 0; ///< PcVsPieceKind sin la gorda: small_a, small_b, small_c, guarded, pond
	double x = 0, z = 0;
	std::string part; ///< pieza concreta ("uf03"...); vacía = la que toque por `kind`
};

/// Enemigo o jefe, por su nombre en el juego (enemyTypes()).
struct DocEnemy {
	std::string type = "swallow";
	double x = 0, z = 0, face = 0;
	DocReaction reaction;
};

/// Construcción que funciona como en el juego (objectKinds()): puentes y
/// rampas que construyen los Pikmin, rocas que empujan, puertas que derriban.
struct DocObject {
	std::string kind = "bridge";
	double x = 0, z = 0, face = 0;
	double y    = 0;   ///< altura sobre el suelo (puentes entre mesetas…)
	double push = 300; ///< roca: cuánto se desplaza al empujarla (hacia donde mira)
	int work     = 2;   ///< puente: trabajo por tramo (1 poco, 2 normal, 4 mucho)
	DocReaction reaction;
};

struct DocGuard {
	std::string enemy = "chappy";
	double x = 0, z = 0;
	DocReaction reaction;
};

/// Malla estática de un .mod del juego metida en el mapa.
struct DocProp {
	std::string asset; ///< ruta en dataDir, p. ej. "objects/hinderrock/cube10.mod"
	double x = 0, y = 0, z = 0;
	double rx = 0, ry = 0, rz = 0; ///< grados
	double scale    = 1;
	bool collide    = true;
};

/// Material del suelo: uno de los de un .mod de escenario del juego.
struct DocMaterial {
	std::string source = "courses/practice/practice.mod";
	int index          = 14;
	int attribute      = 2; ///< terreno para el juego: 0 sólido, 1 roca, 2 hierba, 3 madera, 4 barro
	double uvScale     = 0; ///< unidades de UV por unidad de mundo; 0 = la del escenario original
};

/// Terreno editable: una cuadrícula de nx x nz celdas desde (minX, minZ).
/// Todo se guarda por punto ((nx+1) x (nz+1), por filas) y se pinta con
/// valores de 0 a 255: el compilador corta cada triángulo justo donde el
/// valor cruza la mitad, así que los bordes son curvas suaves, no escalones.
/// Sin él (grid = false) el suelo es el rectángulo llano de siempre.
struct DocTerrain {
	bool grid = false;
	int nx = 0, nz = 0;
	std::vector<std::int16_t> heights;
	std::vector<std::uint8_t> floor; ///< hay suelo donde pasa de 128
	std::vector<std::uint8_t> water; ///< hay agua donde pasa de 128 (lago plano a la altura de su orilla más baja)
	std::vector<std::vector<std::uint8_t>> layers; ///< por material de la paleta: cuánto hay (en cada sitio gana el mayor)
	std::vector<DocMaterial> palette;
	DocMaterial border { "courses/practice/practice.mod", 0, 1, 0 }; ///< paredes del contorno

	int vertexIndex(int ix, int iz) const { return iz * (nx + 1) + ix; }
	std::size_t vertexCount() const { return std::size_t(nx + 1) * std::size_t(nz + 1); }
};

/// Generador de un escenario del juego, tal cual (enemigo, planta, hierbas,
/// puente...): el juego lo crea con su propio código. Solo se cambia su
/// posición.
struct DocGen {
	std::string file = "default"; ///< "default" o "plants" (el .gen al que va)
	std::string data;             ///< el generador en crudo, en base64
	double x = 0, y = 0, z = 0;
	std::string label, model; ///< para el editor
};

/// Formas para empezar un terreno.
enum class TerrainShape { Rectangle, Oval, Cross, TwoIslands, Ring };

struct MapDoc {
	std::string name = "Mapa nuevo", author, version = "1.0";
	double minX = -1600, maxX = 1600, minZ = -900, maxZ = 900, wallHeight = 150;
	bool symmetric = true; ///< "rotate180"
	std::string baseStage = "stages/chal0.ini";
	double sun[3]         = { 0.32, -0.92, 0.23 };
	std::string sourceMod = "courses/practice/practice.mod";
	double cell           = 50;
	DocTerrain terrain;
	/// Escenario del juego como base (su .mod tal cual, con sus caminos);
	/// vacío = terreno del editor.
	std::string course;
	std::vector<DocGen> gens;

	std::vector<DocWall> walls;
	std::vector<DocPond> ponds;
	std::vector<DocProp> props;

	double routeStep = 150, routeMargin = 100;
	std::vector<double> barrierX;
	std::vector<std::pair<double, double>> passes;

	DocSpot bases[2], rockets[2], gates[2], bombPiles[2];
	DocOnion onions[2][3];
	std::vector<DocPellet> pellets;
	std::vector<DocPiece> pieces;
	double bigX = 0, bigZ = 0;
	std::vector<DocGuard> guards;
	std::vector<DocEnemy> enemies;
	std::vector<DocObject> objects;
};

/// Lee un map.json (solo la sintaxis y los tipos; lo demás, en expandMap).
bool parseMapDoc(const std::string& text, MapDoc& out, std::string& error);
/// Lo vuelve a escribir (JSON con sangría).
std::string writeMapDoc(const MapDoc& doc);

/// Mapa nuevo con lo mínimo para jugar (bases, cebollas, cohetes...), con
/// terreno editable.
MapDoc defaultMapDoc();

/// Pasa el suelo llano a terreno editable del mismo tamaño (sin relieve).
void makeTerrainGrid(MapDoc& doc);
/// Rehace la forma del terreno (qué celdas tienen suelo) con una plantilla.
void applyTerrainShape(MapDoc& doc, TerrainShape shape);
/// Cambia el tamaño del terreno añadiendo o quitando celdas por cada lado
/// (negativo = quitar). Lo que había se queda donde estaba.
void resizeTerrain(MapDoc& doc, int left, int right, int top, int bottom);

// ── Mapa desplegado ─────────────────────────────────────────────────────────

struct MapWall {
	float ax, az, bx, bz, thickness, height;
};
enum MapReactionKind { MAPREACT_None, MAPREACT_Enemy, MAPREACT_Pellet, MAPREACT_Bomb, MAPREACT_Nectar };
struct MapReaction {
	int kind = MAPREACT_None;
	std::uint32_t pellet = 0;
	int teki = -1, boss = -1;
	int count = 1;
	float radius = 60.0f;
};
struct MapPond {
	float cx, cz, rx, rz, island, depth, surface;
};
struct MapSpot {
	float x, z, face; ///< face en radianes
	MapReaction reaction;
};
struct MapPellet {
	float x, z;
	std::uint32_t id;
};
struct MapPiece {
	float x, z;
	int kind;
	std::uint32_t part; ///< 0 = la que toque por `kind`
};
struct MapEnemy {
	float x, z, face;
	int teki; ///< tipo de enemigo del juego, o -1
	int boss; ///< tipo de jefe (GENBOSS_*), o -1
	MapReaction reaction;
};
enum MapObjectKind { MAPOBJ_Bridge, MAPOBJ_BridgeLong, MAPOBJ_SlopeUp, MAPOBJ_SlopeDown, MAPOBJ_PushRock, MAPOBJ_GateSoft, MAPOBJ_GateHard, MAPOBJ_GateBomb,
                     MAPOBJ_Key, MAPOBJ_KeyDoor, MAPOBJ_Nectar };
struct MapObject {
	int kind;
	float x, z, face, push;
	int work;
	float y = 0.0f; ///< sobre el suelo
	MapReaction reaction;
};
struct MapMaterial {
	std::string source;
	int index;
	std::uint32_t code; ///< código de colisión (atributo en los bits 31-29)
	float uvScale;
};

struct MapProp {
	std::string asset;
	float x, y, z, rx, ry, rz, scale; ///< rotación en radianes
	bool collide;
};

struct MapData {
	std::string name, author, baseStage, sourceMod;
	std::string course;                               ///< ver MapDoc::course
	std::vector<std::uint8_t> genDefault, genPlants; ///< .gen listos (vacíos = sin)
	float minX, maxX, minZ, maxZ, wallHeight, cell;
	float sunDir[3];
	// Terreno editable (grid = false: el rectángulo llano).
	bool grid = false;
	int nx = 0, nz = 0;
	std::vector<float> heights;
	std::vector<std::uint8_t> floor, water; // por punto, como en DocTerrain
	std::vector<std::vector<std::uint8_t>> layers;
	std::vector<MapMaterial> palette;
	MapMaterial border;
	std::vector<MapWall> walls;
	std::vector<MapPond> ponds;
	std::vector<MapProp> props;
	float routeStep, routeMargin;
	std::vector<float> barrierX;
	std::vector<MapSpot> passes;
	MapSpot bases[2], rockets[2], gates[2], bombPiles[2];
	MapSpot onions[2][3];
	std::vector<MapPellet> pellets;
	std::vector<MapPiece> pieces;
	MapSpot bigPiece;
	std::vector<MapSpot> guards;
	std::vector<MapEnemy> enemies;
	std::vector<MapObject> objects;
};

/// Enemigos y jefes que se pueden poner (nombre en el mapa, nombre para la
/// gente, modelo para el editor, tipo en el juego).
struct EnemyInfo {
	const char* id;
	const char* label;
	const char* model;
	int teki, boss;
	const char* hint;
};
const std::vector<EnemyInfo>& enemyTypes();

struct ObjectInfo {
	const char* id;
	const char* label;
	const char* model;
	const char* hint;
};
const std::vector<ObjectInfo>& objectKinds();

/// Las 30 piezas de la nave (datos del juego: parms/pelMgr.bin).
struct ShipPartInfo {
	const char* id;
	const char* model;
	int carryMin, carryMax; ///< Pikmin para levantarla / para llevarla a toda velocidad
};
const std::vector<ShipPartInfo>& shipParts();

/// Despliega y comprueba que el juego lo aguanta (límites, dentro del
/// mapa...). false con el motivo si no.
/// Con `validate` = false solo se comprueba lo imprescindible para construir
/// la geometría (el editor dibuja así mapas aún sin terminar).
bool expandMap(const MapDoc& doc, MapData& out, std::string& error, bool validate = true);

/// Lo de los dos pasos de una vez.
bool parseMap(const std::string& text, MapData& out, std::string& error);

/// Altura del suelo en (x, z): la del terreno (interpolada) o 0 si es llano
/// o está fuera. Sin contar charcas.
float terrainHeight(const MapData& map, float x, float z);
/// True si en (x, z) hay suelo (con terreno, la celda no está vacía).
bool terrainSolid(const MapData& map, float x, float z);

/// Nombres de las piezas y cebollas en el JSON.
const char* pieceKindName(int kind);
const char* onionColorName(int color);

} // namespace nectar
