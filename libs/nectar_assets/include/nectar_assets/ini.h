#pragma once

// Archivos de texto del juego con bloques { }: el .ini de escena
// (stages/*.ini) y los caminos que van al final de los .mod de mapa.

#include "nectar_assets/mod.h"

#include <string>
#include <vector>

namespace nectar {

/// Una línea "clave valor valor..." o un bloque "clave args { ... }".
struct IniNode {
	std::string key;
	std::vector<std::string> args;
	std::vector<IniNode> children;
	bool block = false;

	const IniNode* find(const std::string& k) const;
	float num(std::size_t i, float def = 0.0f) const;
};

/// Árbol de un texto así; los comentarios // se quitan. false si las llaves
/// no cuadran.
bool parseIniTree(const std::string& text, IniNode& root, std::string& error);

struct SceneLight {
	int type   = 0; ///< 1 paralela, 3 foco...
	int attach = 0;
	float fov  = 0.0f;
	Vec3 position, direction;
	Rgba8 colour;
};

/// Un momento del día (noche, mañana, día, tarde...).
struct SceneTime {
	std::vector<SceneLight> lights;
	Rgba8 ambient;
	Rgba8 fogColour;
	float fogNear = 0.0f, fogFar = 0.0f;
};

struct Scene {
	std::string mapFile; ///< .mod del escenario
	Vec2 naviStart;
	float dayMultiply = 1.0f;
	std::vector<SceneTime> times;
};

bool parseScene(const std::string& text, Scene& out, std::string& error);
bool loadScene(const std::string& path, Scene& out, std::string& error);

struct RoutePoint {
	int index = 0;
	Vec3 pos;
	float width = 0.0f;
};

/// Red de caminos de los Pikmin (el texto tras el final de un .mod de mapa).
struct Routes {
	std::vector<RoutePoint> points;
	std::vector<std::pair<int, int>> links;
};
bool parseRoutes(const std::string& text, Routes& out, std::string& error);

} // namespace nectar
