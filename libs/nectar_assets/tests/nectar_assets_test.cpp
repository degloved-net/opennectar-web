// nectar_assets_test <dataDir> [carpeta para miniaturas de muestra]
//
// Lee todos los assets del dataDir con nectar_assets:
//  - cada .mod sin errores, y el nº de vértices igual al que lee el juego
//    (el contador del bloque 0x10, leído aparte a mano);
//  - cada .gen y cada .ini de escena;
//  - cada .bti (las paletizadas se cuentan aparte: no se soportan);
//  - el árbol de caminos de los mapas.
// Sale con 1 si algo de lo obligatorio falla.

#include "nectar_assets/gen.h"
#include "nectar_assets/image.h"
#include "nectar_assets/ini.h"
#include "nectar_assets/mod.h"
#include "nectar_assets/raster.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> readAll(const fs::path& p)
{
	std::ifstream f(p, std::ios::binary);
	return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::uint32_t be32(const std::vector<std::uint8_t>& d, std::size_t o)
{
	return (std::uint32_t(d[o]) << 24) | (std::uint32_t(d[o + 1]) << 16) | (std::uint32_t(d[o + 2]) << 8) | d[o + 3];
}

// Lo que el juego pone en mVertexCount: el primer entero del bloque 0x10.
long gameVertexCount(const std::vector<std::uint8_t>& d)
{
	std::size_t o = 0;
	while (o + 8 <= d.size()) {
		const std::uint32_t type = be32(d, o), len = be32(d, o + 4);
		if (type == 0x10) return o + 12 <= d.size() ? long(be32(d, o + 8)) : -1;
		if (type == 0xFFFF) break;
		o += 8 + len;
	}
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "uso: %s <dataDir> [carpeta para miniaturas]\n", argv[0]);
		return 2;
	}
	const fs::path root(argv[1]);
	const fs::path thumbs = argc > 2 ? fs::path(argv[2]) : fs::path();
	if (!thumbs.empty()) fs::create_directories(thumbs);

	int mods = 0, modFail = 0, gens = 0, genFail = 0, inis = 0, iniFail = 0, btis = 0, btiFail = 0, routes = 0, routeFail = 0;
	std::size_t tris = 0;
	for (const auto& it : fs::recursive_directory_iterator(root)) {
		if (!it.is_regular_file()) continue;
		const fs::path& p     = it.path();
		const std::string ext = p.extension().string();
		const std::string rel = fs::relative(p, root).generic_string();
		std::string err;
		if (ext == ".mod") {
			mods++;
			const std::vector<std::uint8_t> bytes = readAll(p);
			nectar::Model m;
			if (!nectar::parseMod(bytes.data(), bytes.size(), m, err)) {
				modFail++;
				printf("MOD  %s: %s\n", rel.c_str(), err.c_str());
				continue;
			}
			const long expected = gameVertexCount(bytes);
			if (long(m.positions.size()) != expected) {
				modFail++;
				printf("MOD  %s: %zu vértices, el juego lee %ld\n", rel.c_str(), m.positions.size(), expected);
				continue;
			}
			tris += m.triangleCount();
			if (!m.trailingIni.empty() && m.trailingIni.find("route") != std::string::npos) {
				routes++;
				nectar::Routes r;
				if (!nectar::parseRoutes(m.trailingIni, r, err) || r.points.empty()) {
					routeFail++;
					printf("RUTA %s: %s\n", rel.c_str(), err.empty() ? "sin puntos" : err.c_str());
				}
			}
			if (!thumbs.empty()) {
				const nectar::Image img = nectar::renderModel(m);
				std::string name = rel;
				for (char& c : name) {
					if (c == '/') c = '_';
				}
				if (!img.empty()) nectar::writePng((thumbs / (name + ".png")).string(), img);
			}
		} else if (ext == ".gen") {
			gens++;
			nectar::GenFile g;
			if (!nectar::loadGen(p.string(), g, err)) {
				genFail++;
				printf("GEN  %s: %s\n", rel.c_str(), err.c_str());
			}
		} else if (ext == ".ini" && rel.rfind("stages/", 0) == 0 && rel != "stages/stages.ini") {
			inis++;
			nectar::Scene s;
			if (!nectar::loadScene(p.string(), s, err)) {
				iniFail++;
				printf("INI  %s: %s\n", rel.c_str(), err.c_str());
			}
		} else if (ext == ".bti") {
			btis++;
			nectar::Image img;
			if (!nectar::loadBti(p.string(), img)) btiFail++;
		}
	}
	printf("\n.mod  %d leídos, %d con error (%zu triángulos en total)\n", mods - modFail, modFail, tris);
	printf("rutas %d leídas, %d con error\n", routes - routeFail, routeFail);
	printf(".gen  %d leídos, %d con error\n", gens - genFail, genFail);
	printf(".ini  %d leídos, %d con error\n", inis - iniFail, iniFail);
	printf(".bti  %d leídos, %d sin soporte (paletizadas u otras)\n", btis - btiFail, btiFail);
	return (modFail || genFail || iniFail || routeFail || mods == 0) ? 1 : 0;
}
