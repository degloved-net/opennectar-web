// nectar_compile_map <dataDir> <map.json> <salida.mod> <salida.ini>
// Compila un mapa VS fuera del juego, con el mismo código que usa el juego.

#include "nectar_mapcore/compile.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

int main(int argc, char** argv)
{
	if (argc < 5) {
		fprintf(stderr, "uso: %s <dataDir> <map.json> <salida.mod> <salida.ini>\n", argv[0]);
		return 2;
	}
	const std::string dataDir = argv[1];
	std::ifstream in(argv[2], std::ios::binary);
	if (!in) {
		fprintf(stderr, "no se puede abrir %s\n", argv[2]);
		return 1;
	}
	std::ostringstream ss;
	ss << in.rdbuf();

	std::string error;
	nectar::MapData map;
	if (!nectar::parseMap(ss.str(), map, error)) {
		fprintf(stderr, "mapa no válido: %s\n", error.c_str());
		return 1;
	}
	auto read = [&](const std::string& path, std::vector<std::uint8_t>& out) {
		std::ifstream f(dataDir + "/" + path, std::ios::binary);
		if (!f) return false;
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	};
	nectar::CompiledMap out;
	if (!nectar::compileMap(map, read, "courses/vsarena/arena.mod", out, error)) {
		fprintf(stderr, "no se puede compilar: %s\n", error.c_str());
		return 1;
	}
	std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char*>(out.mod.data()), std::streamsize(out.mod.size()));
	std::ofstream(argv[4], std::ios::binary).write(reinterpret_cast<const char*>(out.ini.data()), std::streamsize(out.ini.size()));
	printf("%s: %zu bytes, %zu vértices, %zu triángulos de colisión, %zu puntos de camino, %zu objetos\n", map.name.c_str(), out.mod.size(),
	       out.vertices, out.collisionTris, out.routePoints, out.props);
	return 0;
}
