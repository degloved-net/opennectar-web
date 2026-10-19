#include "course_import.h"

#include "nectar_assets/gen.h"
#include "nectar_assets/ini.h"
#include "nectar_assets/mod.h"
#include "nectar_mapcore/i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

namespace nm {

using nectar::tr;
namespace fs = std::filesystem;

const std::vector<CourseChoice>& storyCourses()
{
	static const std::vector<CourseChoice> list = {
		{ "stages/practice.ini", "stages/practice/", "Zona del Impacto" },
		{ "stages/stage1.ini", "stages/stage1/", "Bosque de la Esperanza" },
		{ "stages/stage2.ini", "stages/stage2/", "El Ombligo del Bosque" },
		{ "stages/stage3.ini", "stages/stage3/", "La Fuente Lejana" },
		{ "stages/last.ini", "stages/last/", "La Prueba Final" },
	};
	return list;
}

const std::vector<CourseChoice>& challengeCourses()
{
	static const std::vector<CourseChoice> list = {
		{ "stages/chal0.ini", "stages/chal0/", "Zona del Impacto" },
		{ "stages/chal1.ini", "stages/chal1/", "Bosque de la Esperanza" },
		{ "stages/chal2.ini", "stages/chal2/", "El Ombligo del Bosque" },
		{ "stages/chal3.ini", "stages/chal3/", "La Fuente Lejana" },
		{ "stages/chal4.ini", "stages/chal4/", "La Prueba Final" },
	};
	return list;
}

namespace {

std::string base64(const std::vector<std::uint8_t>& in)
{
	static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (std::size_t i = 0; i < in.size(); i += 3) {
		const std::uint32_t n = (std::uint32_t(in[i]) << 16) | (i + 1 < in.size() ? std::uint32_t(in[i + 1]) << 8 : 0u)
		                      | (i + 2 < in.size() ? std::uint32_t(in[i + 2]) : 0u);
		out += k[(n >> 18) & 63];
		out += k[(n >> 12) & 63];
		out += i + 1 < in.size() ? k[(n >> 6) & 63] : '=';
		out += i + 2 < in.size() ? k[n & 63] : '=';
	}
	return out;
}

std::string idStr(std::uint32_t id) { return nectar::genIdString(id); }

const char* const kPlantModels[] = { "clover", "chidome", "hutaba", "ine", "tanpopo", "ooinu_l", "ooinu_s", "mizukusa", "wakame_l", "wakame_s", "kinokolt", "hae" };
const char* const kPlantNames[]  = { "Trébol",        "Planta rastrera", "Brote",         "Espiga",      "Diente de león", "Flor azul grande",
	                                 "Flor azul pequeña", "Planta de agua", "Alga grande", "Alga pequeña", "Setas",          "Moscas" };

/// Nombre y modelo para el editor. false = no se importa (lo pone VS).
bool describe(const nectar::Generator& g, nectar::DocGen& out)
{
	const std::string kind = idStr(g.object.kind);
	const int type         = g.object.type;
	if (kind == "teki") {
		for (const nectar::EnemyInfo& e : nectar::enemyTypes()) {
			if (e.teki == type) {
				out.label = tr(e.label);
				out.model = e.model;
				return true;
			}
		}
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%s %d", tr("Enemigo"), type);
		out.label = buf;
		return true;
	}
	if (kind == "boss") {
		for (const nectar::EnemyInfo& e : nectar::enemyTypes()) {
			if (e.boss == type) {
				out.label = tr(e.label);
				out.model = e.model;
				return true;
			}
		}
		static const char* const models[] = { "", "", "", "", "", "", "bosses/kingback/kingback.mod", "bosses/nucleus/nucleus.mod",
			                                  "bosses/core/core.mod", "bosses/snake/snake.mod", "bosses/mizu/mizu.mod", "tekis/hibaA/hibaA.mod" };
		static const char* const names[]  = { "", "", "", "", "", "", "Emperador Bulblax (jefe)", "Núcleo", "Núcleo", "Aguijonero (jefe)", "Géiser de agua",
			                                  "Géiser de fuego" };
		if (type >= 6 && type <= 11) {
			out.label = tr(names[type]);
			out.model = models[type];
		} else {
			out.label = tr("Jefe");
		}
		return true;
	}
	if (kind == "plnt") {
		if (type >= 0 && type < 12) {
			out.label = tr(kPlantNames[type]);
			out.model = std::string("objects/plants/") + kPlantModels[type] + ".mod";
		} else {
			out.label = tr("Planta");
		}
		return true;
	}
	if (kind == "pelt") {
		const std::string id = idStr(g.object.pellet);
		if (id.size() == 4 && id[0] == 'p') {
			const char* colour = id[1] == 'b' ? tr("azul") : id[1] == 'y' ? tr("amarilla") : tr("roja");
			char buf[64];
			std::snprintf(buf, sizeof(buf), tr("Pastilla %s de %d"), colour, std::atoi(id.c_str() + 2));
			out.label = buf;
			const int n = std::atoi(id.c_str() + 2);
			out.model   = n >= 20 ? "objects/pellets/white4.mod" : n >= 10 ? "objects/pellets/white3.mod" : n >= 5 ? "objects/pellets/white2.mod" : "objects/pellets/white1.mod";
			return true;
		}
		return false; // piezas de la nave: pasan a piezas de VS
	}
	if (kind == "piki") {
		out.label = tr("Pikmin salvajes");
		out.model = "pikis/redModel.mod";
		return true;
	}
	if (kind == "work") {
		if (g.object.name.find("bridge") != std::string::npos) {
			out.label = tr("Puente");
			out.model = "objects/bridge/brd_test.mod";
		} else {
			out.label = tr("Bloque que se empuja");
			out.model = "objects/hinderrock/cube10.mod";
		}
		return true;
	}
	if (kind == "item") {
		const std::string& n = g.object.name;
		if (n == "goal" || n == "ufo") return false; // cebollas y nave: las de VS
		struct Item {
			const char* key;
			const char* label;
			const char* model;
		};
		static const Item items[] = {
			{ "bomb gen", "Rocas bomba", "objects/bomb/bomb.mod" },
			{ "bomb", "Roca bomba", "objects/bomb/bomb.mod" },
			{ "grassgen", "Hierbas", "objects/weeds/weed1.mod" },
			{ "kusa", "Hierba que se arranca", "objects/weeds/weed1.mod" },
			{ "rockgen", "Piedras", "objects/weeds/stone2.mod" },
			{ "weeds", "Hierbajos", "objects/weeds/weed1.mod" },
			{ "sluice soft", "Puerta de papel", "objects/gate/gt_soft.mod" },
			{ "sluice hard", "Puerta de piedra", "objects/gate/gt_hard.mod" },
			{ "sluice bombhard", "Puerta de roca negra", "objects/gate/gt_hard.mod" },
			{ "sluice bomb", "Puerta de roca", "objects/gate/gt_hard.mod" },
			{ "water", "Néctar", "objects/water/water.mod" },
		};
		for (const Item& it : items) {
			if (n == it.key) {
				out.label = tr(it.label);
				out.model = it.model;
				return true;
			}
		}
		out.label = n;
		return true;
	}
	return false; // capitán, actores de demo...
}

/// Archivos del día con el que se empieza en el nivel: "N.gen" y "A-B.gen"
/// que lo incluyen, más default.gen, init.gen y plants.gen.
std::vector<std::string> genFiles(const std::string& dir)
{
	struct Range {
		std::string file;
		int a, b;
	};
	std::vector<Range> ranges;
	std::vector<std::string> out;
	std::error_code ec;
	for (const auto& e : fs::directory_iterator(dir, ec)) {
		if (e.path().extension() != ".gen") continue;
		const std::string stem = e.path().stem().string();
		if (stem == "default" || stem == "init" || stem == "plants") {
			out.push_back(e.path().filename().string());
			continue;
		}
		int a = 0, b = 0;
		if (std::sscanf(stem.c_str(), "%d-%d", &a, &b) == 2) ranges.push_back({ e.path().filename().string(), a, b });
		else if (std::sscanf(stem.c_str(), "%d", &a) == 1) ranges.push_back({ e.path().filename().string(), a, a });
	}
	if (!ranges.empty()) {
		int day = std::numeric_limits<int>::max();
		for (const Range& r : ranges) day = std::min(day, r.a);
		for (const Range& r : ranges) {
			if (r.a <= day && day <= r.b) out.push_back(r.file);
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

float dist2(const nectar::Vec3& a, float x, float z) { return (a.x - x) * (a.x - x) + (a.z - z) * (a.z - z); }

} // namespace

bool CourseField::at(float x, float z, float& y) const
{
	if (nx < 2 || nz < 2) return false;
	const float fx = (x - minX) / cell, fz = (z - minZ) / cell;
	const int ix = int(std::floor(fx)), iz = int(std::floor(fz));
	if (ix < 0 || iz < 0 || ix >= nx - 1 || iz >= nz - 1) return false;
	// La más alta de las cuatro esquinas con suelo (así no se cuela en los bordes).
	float best = -1e30f;
	bool any   = false;
	for (int k = 0; k < 4; k++) {
		const float v = h[std::size_t(iz + k / 2) * std::size_t(nx) + std::size_t(ix + k % 2)];
		if (std::isnan(v)) continue;
		best = std::max(best, v);
		any  = true;
	}
	if (any) y = best;
	return any;
}

bool buildCourseField(const std::string& dataDir, const std::string& course, CourseField& out)
{
	out = CourseField();
	nectar::Model m;
	std::string err;
	if (!nectar::loadMod(dataDir + "/" + course, m, err) || m.collision.empty()) return false;
	float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
	for (const nectar::ModCollTri& t : m.collision) {
		for (int k = 0; k < 3; k++) {
			const nectar::Vec3& p = m.positions[std::size_t(t.v[k])];
			x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
		}
	}
	out.course = course;
	out.cell   = std::max(10.0f, std::max(x1 - x0, z1 - z0) / 400.0f);
	out.minX   = x0;
	out.minZ   = z0;
	out.nx     = int((x1 - x0) / out.cell) + 2;
	out.nz     = int((z1 - z0) / out.cell) + 2;
	out.h.assign(std::size_t(out.nx) * std::size_t(out.nz), std::numeric_limits<float>::quiet_NaN());
	for (const nectar::ModCollTri& t : m.collision) {
		if (t.normal.y < 0.3f) continue; // paredes y techos no se pisan
		const nectar::Vec3 &a = m.positions[std::size_t(t.v[0])], &b = m.positions[std::size_t(t.v[1])], &c = m.positions[std::size_t(t.v[2])];
		const int ix0 = std::max(0, int(std::floor((std::min({ a.x, b.x, c.x }) - out.minX) / out.cell)));
		const int ix1 = std::min(out.nx - 1, int(std::ceil((std::max({ a.x, b.x, c.x }) - out.minX) / out.cell)));
		const int iz0 = std::max(0, int(std::floor((std::min({ a.z, b.z, c.z }) - out.minZ) / out.cell)));
		const int iz1 = std::min(out.nz - 1, int(std::ceil((std::max({ a.z, b.z, c.z }) - out.minZ) / out.cell)));
		const float den = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
		if (std::fabs(den) < 1e-6f) continue;
		for (int iz = iz0; iz <= iz1; iz++) {
			for (int ix = ix0; ix <= ix1; ix++) {
				const float x = out.minX + ix * out.cell, z = out.minZ + iz * out.cell;
				const float l0 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / den;
				const float l1 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / den;
				const float l2 = 1.0f - l0 - l1;
				if (l0 < -0.02f || l1 < -0.02f || l2 < -0.02f) continue;
				const float y = l0 * a.y + l1 * b.y + l2 * c.y;
				float& cur    = out.h[std::size_t(iz) * std::size_t(out.nx) + std::size_t(ix)];
				if (std::isnan(cur) || y > cur) cur = y;
			}
		}
	}
	return true;
}

bool importCourse(const std::string& dataDir, const CourseChoice& c, nectar::MapDoc& d, std::string& error)
{
	nectar::Scene scene;
	if (!nectar::loadScene(dataDir + "/" + c.stageIni, scene, error)) return false;
	nectar::Model model;
	if (!nectar::loadMod(dataDir + "/" + scene.mapFile, model, error)) return false;
	nectar::Routes routes;
	std::string why;
	nectar::parseRoutes(model.trailingIni, routes, why);
	if (routes.points.size() < 8) {
		error = tr("ese escenario no tiene caminos para los Pikmin");
		return false;
	}

	d           = nectar::defaultMapDoc();
	d.name      = tr(c.name);
	d.course    = scene.mapFile;
	d.sourceMod = scene.mapFile;
	d.baseStage = c.stageIni;
	d.terrain   = nectar::DocTerrain();
	d.symmetric = false;
	d.walls.clear();
	d.ponds.clear();
	d.props.clear();
	d.pellets.clear();
	d.pieces.clear();
	d.guards.clear();
	d.barrierX.clear();
	d.passes.clear();

	// Límites: los de la colisión, con un margen.
	float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
	for (const nectar::ModCollTri& t : model.collision) {
		for (int k = 0; k < 3; k++) {
			const nectar::Vec3& p = model.positions[std::size_t(t.v[k])];
			x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
		}
	}
	d.minX = x0 - 50, d.maxX = x1 + 50, d.minZ = z0 - 50, d.maxZ = z1 + 50;

	// Generadores del nivel.
	nectar::Vec3 naviPos { scene.naviStart.x, 0, scene.naviStart.y };
	for (const std::string& file : genFiles(dataDir + "/" + c.genDir)) {
		nectar::GenFile gf;
		if (!nectar::loadGen(dataDir + "/" + c.genDir + file, gf, why)) continue;
		if (file == "default.gen" || file == "init.gen") naviPos = gf.naviPos;
		for (const nectar::Generator& g : gf.generators) {
			const nectar::Vec3 pos { g.pos.x + g.offset.x, g.pos.y + g.offset.y, g.pos.z + g.offset.z };
			if (idStr(g.object.kind) == "pelt") {
				const std::string id = idStr(g.object.pellet);
				if (!id.empty() && id[0] == 'u') {
					nectar::DocPiece p;
					p.part = id;
					p.x    = pos.x;
					p.z    = pos.z;
					d.pieces.push_back(p);
					continue;
				}
			}
			nectar::DocGen dg;
			if (!describe(g, dg)) continue;
			dg.file = file == "plants.gen" ? "plants" : "default";
			dg.data = base64(g.raw);
			dg.x    = g.pos.x;
			dg.y    = g.pos.y;
			dg.z    = g.pos.z;
			d.gens.push_back(dg);
		}
	}

	// Bases de VS: J1 junto a donde empieza el capitán, J2 en el punto de
	// camino más lejano. Lo demás de cada jugador, alrededor de su base.
	const auto& P = routes.points;
	auto nearest  = [&](float x, float z) {
        std::size_t best = 0;
        for (std::size_t i = 1; i < P.size(); i++)
            if (dist2(P[i].pos, x, z) < dist2(P[best].pos, x, z)) best = i;
        return best;
	};
	const std::size_t b1 = nearest(naviPos.x, naviPos.z);
	std::size_t b2       = b1;
	for (std::size_t i = 0; i < P.size(); i++)
		if (dist2(P[i].pos, P[b1].pos.x, P[b1].pos.z) > dist2(P[b2].pos, P[b1].pos.x, P[b1].pos.z)) b2 = i;
	std::vector<std::size_t> used { b1, b2 };
	// Punto de camino a entre `lo` y `hi` de `from`, lo más lejos posible de lo ya usado.
	auto around = [&](std::size_t from, float lo, float hi) {
		std::size_t best = from;
		float bestScore  = -1;
		for (std::size_t i = 0; i < P.size(); i++) {
			const float dd = std::sqrt(dist2(P[i].pos, P[from].pos.x, P[from].pos.z));
			if (dd < lo || dd > hi) continue;
			float near = 1e30f;
			for (std::size_t u : used) near = std::min(near, dist2(P[i].pos, P[u].pos.x, P[u].pos.z));
			if (near > bestScore) bestScore = near, best = i;
		}
		used.push_back(best);
		return best;
	};
	const std::size_t base[2] = { b1, b2 };
	for (int p = 0; p < 2; p++) {
		const nectar::Vec3& me = P[base[p]].pos;
		const nectar::Vec3& rv = P[base[1 - p]].pos;
		const double face      = std::atan2(rv.x - me.x, rv.z - me.z) * 180.0 / 3.14159265358979;
		d.bases[p]             = { p, me.x, me.z, face };
		const nectar::Vec3& r  = P[around(base[p], 150, 400)].pos;
		d.rockets[p]           = { p, r.x, r.z, face };
		for (int k = 0; k < 3; k++) {
			const nectar::Vec3& o = P[around(base[p], 120, 450)].pos;
			d.onions[p][k]        = { p, k, o.x, o.z };
		}
		const nectar::Vec3& g = P[around(base[p], 350, 700)].pos;
		d.gates[p]            = { p, g.x, g.z, face };
		const nectar::Vec3& b = P[around(base[p], 100, 400)].pos;
		d.bombPiles[p]        = { p, b.x, b.z, 0 };
	}
	const std::size_t mid = nearest((P[b1].pos.x + P[b2].pos.x) * 0.5f, (P[b1].pos.z + P[b2].pos.z) * 0.5f);
	d.bigX                = P[mid].pos.x;
	d.bigZ                = P[mid].pos.z;
	// Sin piezas de la nave en el nivel (desafío): tres por el medio.
	if (d.pieces.empty()) {
		used.push_back(mid);
		for (int k = 0; k < 3; k++) {
			const nectar::Vec3& q = P[around(mid, 150, 900)].pos;
			d.pieces.push_back({ k, q.x, q.z, "" });
		}
	}
	while (d.pieces.size() > 16) d.pieces.pop_back();
	return true;
}

} // namespace nm
