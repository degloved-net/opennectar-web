#include "nectar_mapcore/map.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "nectar_mapcore/i18n.h"
#include "nectar_assets/gen.h"

namespace nectar {

using json = nlohmann::json;

namespace {

// Lo que caben en los arrays del juego (gameCoreSection, pc_vs_arena_*_spots).
constexpr std::size_t kMaxPellets = 16, kMaxPieces = 16, kMaxGuards = 4;

const char* const kPieceNames[] = { "small_a", "small_b", "small_c", "guarded", "pond" };
const char* const kOnionNames[] = { "blue", "red", "yellow" };

float deg(float d) { return d * 3.14159265f / 180.0f; }

void pos2(const json& j, double& x, double& z)
{
	const json& p = j.at("pos");
	x             = p.at(0).get<double>();
	z             = p.at(1).get<double>();
}

DocReaction parseReaction(const json& owner)
{
	DocReaction out;
	if (!owner.contains("reaction")) return out;
	const json& j = owner.at("reaction");
	out.action = j.value("action", std::string());
	out.value  = j.value("value", std::string());
	out.count  = j.value("count", 1);
	out.radius = j.value("radius", 60.0);
	if (out.action != "enemy" && out.action != "pellet" && out.action != "bomb" && out.action != "nectar")
		throw std::runtime_error(tr("reacción desconocida: ") + out.action);
	if (out.count < 1 || out.count > 8) throw std::runtime_error(tr("reacción: la cantidad debe estar entre 1 y 8"));
	if (out.radius < 0.0 || out.radius > 1000.0) throw std::runtime_error(tr("reacción: el radio debe estar entre 0 y 1000"));
	return out;
}

json reactionJson(const DocReaction& r)
{
	json j = { { "action", r.action }, { "count", r.count }, { "radius", r.radius } };
	if (!r.value.empty()) j["value"] = r.value;
	return j;
}

int playerOf(const json& j)
{
	const int p = j.at("player").get<int>();
	if (p != 0 && p != 1) throw std::runtime_error(tr("player debe ser 0 o 1"));
	return p;
}

void perPlayer(const json& list, DocSpot out[2], const char* what, bool allowReaction = false)
{
	bool seen[2] = { false, false };
	for (const json& j : list) {
		const int p = playerOf(j);
		DocSpot s;
		s.player = p;
		pos2(j, s.x, s.z);
		s.face  = j.value("face", 0.0);
		if (j.contains("reaction") && !allowReaction)
			throw std::runtime_error(std::string("vs.") + what + tr(": este elemento no admite reacciones"));
		s.reaction = allowReaction ? parseReaction(j) : DocReaction {};
		out[p]  = s;
		seen[p] = true;
	}
	if (!seen[0] || !seen[1]) throw std::runtime_error(std::string("vs.") + what + tr(": falta un jugador"));
}

int pieceKind(const std::string& s)
{
	for (int i = 0; i < 5; i++) {
		if (s == kPieceNames[i]) return i;
	}
	throw std::runtime_error(tr("tipo de pieza desconocido: ") + s);
}

int onionColor(const std::string& s)
{
	for (int i = 0; i < 3; i++) {
		if (s == kOnionNames[i]) return i;
	}
	throw std::runtime_error(tr("color de cebolla desconocido: ") + s);
}

std::uint32_t fourcc(const std::string& s)
{
	return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) | (std::uint32_t(std::uint8_t(s[2])) << 8)
	     | std::uint8_t(s[3]);
}

std::uint32_t pelletId(const std::string& s)
{
	// Las del juego: p + color (r, b, y) + valor (01, 05, 10, 20).
	const bool ok = s.size() == 4 && s[0] == 'p' && s[1] && std::strchr("rby", s[1])
	             && (s.compare(2, 2, "01") == 0 || s.compare(2, 2, "05") == 0 || s.compare(2, 2, "10") == 0 || s.compare(2, 2, "20") == 0);
	if (!ok) throw std::runtime_error(tr("pastilla desconocida: \"") + s + tr("\" (pr01, pb05, py10...)"));
	return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) | (std::uint32_t(std::uint8_t(s[2])) << 8)
	     | std::uint8_t(s[3]);
}

MapReaction expandReaction(const DocReaction& r)
{
	MapReaction out;
	if (r.action.empty()) return out;
	out.count  = std::max(1, std::min(8, r.count));
	out.radius = float(std::max(0.0, std::min(1000.0, r.radius)));
	if (r.action == "pellet") {
		out.kind   = MAPREACT_Pellet;
		out.pellet = pelletId(r.value.empty() ? "pr01" : r.value);
	} else if (r.action == "enemy") {
		out.kind = MAPREACT_Enemy;
		for (const EnemyInfo& e : enemyTypes()) {
			if (r.value == e.id) {
				out.teki = e.teki;
				out.boss = e.boss;
			}
		}
		if (out.teki < 0 && out.boss < 0) throw std::runtime_error(tr("reacción: enemigo desconocido: ") + r.value);
	} else if (r.action == "bomb")
		out.kind = MAPREACT_Bomb;
	else if (r.action == "nectar")
		out.kind = MAPREACT_Nectar;
	return out;
}

// ── Base64 (alturas y celdas del terreno, para que el JSON no se dispare) ──
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toBase64(const std::vector<std::uint8_t>& in)
{
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	for (std::size_t i = 0; i < in.size(); i += 3) {
		const std::uint32_t v = (std::uint32_t(in[i]) << 16) | (i + 1 < in.size() ? std::uint32_t(in[i + 1]) << 8 : 0)
		                      | (i + 2 < in.size() ? in[i + 2] : 0);
		out += kB64[(v >> 18) & 63];
		out += kB64[(v >> 12) & 63];
		out += i + 1 < in.size() ? kB64[(v >> 6) & 63] : '=';
		out += i + 2 < in.size() ? kB64[v & 63] : '=';
	}
	return out;
}

std::vector<std::uint8_t> fromBase64(const std::string& in)
{
	std::vector<std::uint8_t> out;
	std::uint32_t v = 0;
	int bits        = 0;
	for (char c : in) {
		const char* p = std::strchr(kB64, c);
		if (!c || !p) {
			if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
			throw std::runtime_error(tr("terreno: datos en base64 rotos"));
		}
		v = (v << 6) | std::uint32_t(p - kB64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(std::uint8_t(v >> bits));
		}
	}
	return out;
}

DocMaterial parseMaterial(const json& j)
{
	DocMaterial m;
	m.source    = j.at("source").get<std::string>();
	m.index     = j.at("index").get<int>();
	m.attribute = j.value("attribute", 2);
	m.uvScale   = j.value("uvScale", 0.0);
	if (m.index < 0) throw std::runtime_error(tr("terreno: material con índice negativo"));
	if (m.attribute < 0 || m.attribute > 4) throw std::runtime_error(tr("terreno: atributo de material desconocido"));
	return m;
}

json materialJson(const DocMaterial& m)
{
	json j = { { "source", m.source }, { "index", m.index }, { "attribute", m.attribute } };
	if (m.uvScale > 0) j["uvScale"] = m.uvScale;
	return j;
}

void parseDoc(const json& root, MapDoc& m)
{
	if (root.at("format").get<int>() != 1) throw std::runtime_error(tr("versión de formato no soportada"));
	if (root.at("mode").get<std::string>() != "vs") throw std::runtime_error(tr("solo hay mapas \"vs\""));
	const std::string sym = root.value("symmetry", "none");
	if (sym != "none" && sym != "rotate180") throw std::runtime_error(tr("simetría desconocida: ") + sym);
	m.symmetric = sym == "rotate180";

	const json& meta = root.at("meta");
	m.name           = meta.at("name").get<std::string>();
	m.author         = meta.value("author", "");
	m.version        = meta.value("version", "1.0");

	const json& b = root.at("bounds");
	m.minX        = b.at("minX").get<double>();
	m.maxX        = b.at("maxX").get<double>();
	m.minZ        = b.at("minZ").get<double>();
	m.maxZ        = b.at("maxZ").get<double>();
	m.wallHeight  = b.at("wallHeight").get<double>();

	const json& env = root.at("environment");
	m.baseStage     = env.at("baseStage").get<std::string>();
	const json& sun = env.at("sun").at("dir");
	for (int i = 0; i < 3; i++) m.sun[i] = sun.at(i).get<double>();

	const json& t          = root.at("terrain");
	const std::string type = t.at("type").get<std::string>();
	if (type != "flat" && type != "grid") throw std::runtime_error(tr("terreno desconocido: \"") + type + tr("\" (flat o grid)"));
	m.sourceMod = t.at("source").get<std::string>();
	m.cell      = t.at("cell").get<double>();
	if (type == "grid") {
		DocTerrain& g = m.terrain;
		g.grid        = true;
		g.nx          = t.at("size").at(0).get<int>();
		g.nz          = t.at("size").at(1).get<int>();
		if (g.nx < 1 || g.nz < 1 || g.nx > 400 || g.nz > 400) throw std::runtime_error(tr("terreno: tamaño fuera de 1..400 celdas"));
		const std::vector<std::uint8_t> h = fromBase64(t.at("heights").get<std::string>());
		const std::size_t nv              = g.vertexCount();
		if (h.size() != nv * 2) throw std::runtime_error(tr("terreno: las alturas no cuadran con el tamaño"));
		g.heights.resize(nv);
		for (std::size_t i = 0; i < nv; i++) g.heights[i] = std::int16_t(h[2 * i] | (h[2 * i + 1] << 8));
		for (const json& mj : t.value("palette", json::array())) g.palette.push_back(parseMaterial(mj));
		if (g.palette.empty() || g.palette.size() > 250) throw std::runtime_error(tr("terreno: hacen falta entre 1 y 250 materiales"));
		if (t.contains("border")) g.border = parseMaterial(t.at("border"));
		if (t.contains("floor")) {
			// Por punto.
			g.floor = fromBase64(t.at("floor").get<std::string>());
			g.water = t.contains("water") ? fromBase64(t.at("water").get<std::string>()) : std::vector<std::uint8_t>(nv, 0);
			for (const json& lj : t.value("layers", json::array())) g.layers.push_back(fromBase64(lj.get<std::string>()));
			// Mapas guardados con un material añadido sin su capa: capa vacía.
			while (g.layers.size() < g.palette.size()) g.layers.emplace_back(nv, std::uint8_t(0));
			if (g.floor.size() != nv || g.water.size() != nv || g.layers.size() != g.palette.size()) throw std::runtime_error(tr("terreno: los datos no cuadran con el tamaño"));
			for (const auto& l : g.layers) {
				if (l.size() != nv) throw std::runtime_error(tr("terreno: un material no cuadra con el tamaño"));
			}
		} else {
			// Formato por celdas (primeras versiones): cada punto toma la
			// proporción de sus celdas vecinas.
			const std::vector<std::uint8_t> cells = fromBase64(t.at("cells").get<std::string>());
			const std::size_t nc                  = std::size_t(g.nx) * std::size_t(g.nz);
			std::vector<std::uint8_t> wet         = t.contains("water") ? fromBase64(t.at("water").get<std::string>()) : std::vector<std::uint8_t>(nc, 0);
			if (cells.size() != nc || wet.size() != nc) throw std::runtime_error(tr("terreno: las celdas no cuadran con el tamaño"));
			g.floor.assign(nv, 0);
			g.water.assign(nv, 0);
			g.layers.assign(g.palette.size(), std::vector<std::uint8_t>(nv, 0));
			for (int iz = 0; iz <= g.nz; iz++) {
				for (int ix = 0; ix <= g.nx; ix++) {
					int around = 0, solid = 0, water = 0;
					std::vector<int> mats(g.palette.size(), 0);
					for (int k = 0; k < 4; k++) {
						const int cx = ix - 1 + (k & 1), cz = iz - 1 + (k >> 1);
						if (cx < 0 || cz < 0 || cx >= g.nx || cz >= g.nz) continue;
						around++;
						const std::uint8_t c = cells[std::size_t(cz * g.nx + cx)];
						if (c > g.palette.size()) throw std::runtime_error(tr("terreno: una celda usa un material que no está en la paleta"));
						if (!c) continue;
						solid++;
						mats[c - 1]++;
						if (wet[std::size_t(cz * g.nx + cx)]) water++;
					}
					const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
					g.floor[v]          = std::uint8_t(around ? 255 * solid / around : 0);
					g.water[v]          = std::uint8_t(solid ? 255 * water / solid : 0);
					for (std::size_t k = 0; k < mats.size(); k++) g.layers[k][v] = std::uint8_t(solid ? 255 * mats[k] / solid : (k == 0 ? 255 : 0));
				}
			}
		}
		// El tamaño manda: los límites salen de la cuadrícula.
		m.maxX = m.minX + g.nx * m.cell;
		m.maxZ = m.minZ + g.nz * m.cell;
	}

	for (const json& w : root.value("walls", json::array())) {
		DocWall d;
		d.thickness = w.at("thickness").get<double>();
		d.height    = w.at("height").get<double>();
		if (w.contains("arc")) {
			const json& a = w.at("arc");
			d.isArc       = true;
			d.cx          = a.at("center").at(0).get<double>();
			d.cz          = a.at("center").at(1).get<double>();
			d.radius      = a.at("radius").get<double>();
			d.from        = a.at("from").get<double>();
			d.to          = a.at("to").get<double>();
			d.segments    = a.at("segments").get<int>();
		} else {
			d.ax = w.at("a").at(0).get<double>();
			d.az = w.at("a").at(1).get<double>();
			d.bx = w.at("b").at(0).get<double>();
			d.bz = w.at("b").at(1).get<double>();
		}
		m.walls.push_back(d);
	}
	for (const json& p : root.value("ponds", json::array())) {
		DocPond d;
		d.cx      = p.at("center").at(0).get<double>();
		d.cz      = p.at("center").at(1).get<double>();
		d.rx      = p.at("radius").at(0).get<double>();
		d.rz      = p.at("radius").at(1).get<double>();
		d.island  = p.value("island", 0.0);
		d.depth   = p.at("depth").get<double>();
		d.surface = p.at("surface").get<double>();
		m.ponds.push_back(d);
	}
	m.course = root.value("course", std::string());
	for (const json& gj : root.value("gens", json::array())) {
		DocGen g;
		g.file        = gj.value("file", std::string("default"));
		g.data        = gj.at("data").get<std::string>();
		const json& o = gj.at("pos");
		g.x           = o.at(0).get<double>();
		g.y           = o.at(1).get<double>();
		g.z           = o.at(2).get<double>();
		g.label       = gj.value("label", std::string());
		g.model       = gj.value("model", std::string());
		m.gens.push_back(g);
	}
	for (const json& p : root.value("props", json::array())) {
		DocProp d;
		d.asset       = p.at("asset").get<std::string>();
		const json& o = p.at("pos");
		d.x           = o.at(0).get<double>();
		d.y           = o.at(1).get<double>();
		d.z           = o.at(2).get<double>();
		if (p.contains("rot")) {
			d.rx = p["rot"].at(0).get<double>();
			d.ry = p["rot"].at(1).get<double>();
			d.rz = p["rot"].at(2).get<double>();
		}
		d.scale   = p.value("scale", 1.0);
		d.collide = p.value("collide", true);
		m.props.push_back(d);
	}

	const json& r = root.at("routes");
	if (!r.at("auto").get<bool>()) throw std::runtime_error(tr("de momento los caminos solo pueden ser automáticos"));
	m.routeStep   = r.at("step").get<double>();
	m.routeMargin = r.at("margin").get<double>();
	for (const json& x : r.value("barrierX", json::array())) m.barrierX.push_back(x.get<double>());
	for (const json& p : r.value("passes", json::array())) m.passes.push_back({ p.at(0).get<double>(), p.at(1).get<double>() });

	const json& vs = root.at("vs");
	perPlayer(vs.at("bases"), m.bases, "bases");
	perPlayer(vs.at("rockets"), m.rockets, "rockets");
	perPlayer(vs.at("gates"), m.gates, "gates", true);
	perPlayer(vs.at("bombPiles"), m.bombPiles, "bombPiles");
	bool onion[2][3] = {};
	for (const json& j : vs.at("onions")) {
		DocOnion o;
		o.player = playerOf(j);
		o.color  = onionColor(j.at("color").get<std::string>());
		pos2(j, o.x, o.z);
		m.onions[o.player][o.color] = o;
		onion[o.player][o.color]    = true;
	}
	for (int p = 0; p < 2; p++) {
		for (int c = 0; c < 3; c++) {
			if (!onion[p][c]) throw std::runtime_error(tr("vs.onions: falta una cebolla"));
		}
	}
	for (const json& j : vs.value("pellets", json::array())) {
		DocPellet d;
		d.id = j.at("id").get<std::string>();
		pos2(j, d.x, d.z);
		m.pellets.push_back(d);
	}
	for (const json& j : vs.value("pieces", json::array())) {
		DocPiece d;
		d.kind = pieceKind(j.at("kind").get<std::string>());
		d.part = j.value("part", "");
		pos2(j, d.x, d.z);
		m.pieces.push_back(d);
	}
	pos2(vs.at("bigPiece"), m.bigX, m.bigZ);
	for (const json& j : vs.value("guards", json::array())) {
		DocGuard d;
		d.enemy = j.value("enemy", "chappy");
		pos2(j, d.x, d.z);
		d.reaction = parseReaction(j);
		m.guards.push_back(d);
	}
	for (const json& j : vs.value("enemies", json::array())) {
		DocEnemy e;
		e.type = j.at("type").get<std::string>();
		pos2(j, e.x, e.z);
		e.face = j.value("face", 0.0);
		e.reaction = parseReaction(j);
		m.enemies.push_back(e);
	}
	for (const json& j : vs.value("objects", json::array())) {
		DocObject o;
		o.kind = j.at("kind").get<std::string>();
		pos2(j, o.x, o.z);
		o.face = j.value("face", 0.0);
		o.push = j.value("push", 300.0);
		o.work = j.value("work", 2);
		o.y    = j.value("y", 0.0);
		if (o.kind == "nectar" && j.contains("reaction"))
			throw std::runtime_error(tr("el néctar no admite reacciones"));
		o.reaction = parseReaction(j);
		m.objects.push_back(o);
	}
}

// Lo que el JSON puede decir bien escrito pero el juego no aguanta.
void check(const MapData& m)
{
	auto inside = [&](float x, float z, const char* what) {
		if (x <= m.minX || x >= m.maxX || z <= m.minZ || z >= m.maxZ) {
			char buf[128];
			snprintf(buf, sizeof(buf), tr("%s fuera del mapa (%.0f, %.0f)"), what, x, z);
			throw std::runtime_error(buf);
		}
		if (m.course.empty() && !terrainSolid(m, x, z)) {
			char buf[128];
			snprintf(buf, sizeof(buf), tr("%s sin suelo debajo (%.0f, %.0f)"), what, x, z);
			throw std::runtime_error(buf);
		}
	};
	if (m.minX >= m.maxX || m.minZ >= m.maxZ) throw std::runtime_error(tr("bounds vacíos"));
	if (m.cell <= 0.0f) throw std::runtime_error(tr("terrain.cell debe ser positivo"));
	if (m.routeStep <= 0.0f) throw std::runtime_error(tr("routes.step debe ser positivo"));
	for (int p = 0; p < 2; p++) {
		inside(m.bases[p].x, m.bases[p].z, tr("base"));
		inside(m.rockets[p].x, m.rockets[p].z, tr("cohete"));
		inside(m.gates[p].x, m.gates[p].z, tr("compuerta"));
		inside(m.bombPiles[p].x, m.bombPiles[p].z, tr("montón de bombas"));
		for (const MapSpot& o : m.onions[p]) inside(o.x, o.z, tr("cebolla"));
	}
	for (const MapPellet& s : m.pellets) inside(s.x, s.z, tr("pastilla"));
	for (const MapPiece& s : m.pieces) inside(s.x, s.z, tr("pieza"));
	for (const MapSpot& s : m.guards) inside(s.x, s.z, tr("guardián"));
	for (const MapEnemy& s : m.enemies) inside(s.x, s.z, tr("enemigo"));
	for (const MapObject& s : m.objects) inside(s.x, s.z, tr("construcción"));
	if (m.enemies.size() + m.guards.size() > 40) throw std::runtime_error(tr("demasiados enemigos (máximo 40, contando los del otro lado)"));
	int possibleTekis = int(m.guards.size()), possibleBosses = 0;
	for (const MapEnemy& enemy : m.enemies) enemy.boss >= 0 ? ++possibleBosses : ++possibleTekis;
	auto addReactionCapacity = [&](const MapReaction& reaction) {
		if (reaction.kind != MAPREACT_Enemy) return;
		if (reaction.boss >= 0) possibleBosses += reaction.count;
		else possibleTekis += reaction.count;
	};
	for (int p = 0; p < 2; p++) addReactionCapacity(m.gates[p].reaction);
	for (const MapSpot& guard : m.guards) addReactionCapacity(guard.reaction);
	for (const MapEnemy& enemy : m.enemies) addReactionCapacity(enemy.reaction);
	for (const MapObject& object : m.objects) addReactionCapacity(object.reaction);
	if (possibleTekis > 80) throw std::runtime_error(tr("demasiados enemigos posibles con las reacciones (máximo 80)"));
	if (possibleBosses > 40) throw std::runtime_error(tr("demasiados jefes posibles con las reacciones (máximo 40)"));
	if (m.objects.size() > 24) throw std::runtime_error(tr("demasiadas construcciones (máximo 24, contando las del otro lado)"));
	for (const MapProp& s : m.props) inside(s.x, s.z, tr("objeto"));
	inside(m.bigPiece.x, m.bigPiece.z, tr("pieza grande"));
	if (m.pieces.empty()) throw std::runtime_error(tr("hace falta al menos una pieza"));
	if (m.pellets.size() > kMaxPellets) throw std::runtime_error(tr("demasiadas pastillas (máximo 16, contando las de J2)"));
	if (m.pieces.size() > kMaxPieces) throw std::runtime_error(tr("demasiadas piezas (máximo 16, contando las de J2)"));
	if (m.guards.size() > kMaxGuards) throw std::runtime_error(tr("demasiados guardianes (máximo 4, contando los de J2)"));
	for (const MapPond& p : m.ponds) {
		if (p.rx <= 0.0f || p.rz <= 0.0f || p.depth <= 0.0f) throw std::runtime_error(tr("charca con radio u hondura no positivos"));
	}
	for (const MapWall& w : m.walls) {
		if (w.ax == w.bx && w.az == w.bz) throw std::runtime_error(tr("muro de longitud cero"));
	}
	for (const MapProp& p : m.props) {
		if (p.asset.size() < 5 || p.asset.compare(p.asset.size() - 4, 4, ".mod") != 0) throw std::runtime_error(tr("objeto que no es un .mod: ") + p.asset);
		if (p.asset.find("..") != std::string::npos || p.asset[0] == '/') throw std::runtime_error(tr("ruta de objeto no válida: ") + p.asset);
		if (!(p.scale > 0.0f)) throw std::runtime_error(tr("objeto con escala no positiva: ") + p.asset);
	}
}

json spotJson(const DocSpot& s, bool face, bool allowReaction = false)
{
	json j = { { "player", s.player }, { "pos", { s.x, s.z } } };
	if (face) j["face"] = s.face;
	if (allowReaction && !s.reaction.action.empty()) j["reaction"] = reactionJson(s.reaction);
	return j;
}

} // namespace

const char* pieceKindName(int kind) { return kind >= 0 && kind < 5 ? kPieceNames[kind] : "small_a"; }
const char* onionColorName(int color) { return color >= 0 && color < 3 ? kOnionNames[color] : "red"; }

bool parseMapDoc(const std::string& text, MapDoc& out, std::string& error)
{
	try {
		MapDoc m;
		m.walls.clear();
		parseDoc(json::parse(text), m);
		out = std::move(m);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}

bool expandMap(const MapDoc& d, MapData& m, std::string& error, bool validate)
{
	try {
		m           = MapData();
		m.name      = d.name;
		m.author    = d.author;
		m.baseStage = d.baseStage;
		m.sourceMod = d.sourceMod;
		m.course    = d.course;
		{
			// Generadores del escenario: a su .gen, con la posición del editor.
			std::vector<std::vector<std::uint8_t>> def, plants;
			for (const DocGen& g : d.gens) {
				std::vector<std::uint8_t> raw = fromBase64(g.data);
				if (raw.size() < kGenPosOffset + 12) throw std::runtime_error(tr("generador roto"));
				const float pos[3] = { float(g.x), float(g.y), float(g.z) };
				for (int k = 0; k < 3; k++) {
					std::uint32_t v;
					std::memcpy(&v, &pos[k], 4);
					for (int b = 0; b < 4; b++) raw[kGenPosOffset + std::size_t(k) * 4 + std::size_t(b)] = std::uint8_t(v >> (24 - 8 * b));
				}
				(g.file == "plants" ? plants : def).push_back(std::move(raw));
			}
			if (!def.empty()) m.genDefault = writeGen(def);
			if (!plants.empty()) m.genPlants = writeGen(plants);
		}
		m.minX      = float(d.minX);
		m.maxX      = float(d.maxX);
		m.minZ      = float(d.minZ);
		m.maxZ      = float(d.maxZ);
		m.wallHeight = float(d.wallHeight);
		m.cell       = float(d.cell);
		if (d.terrain.grid) {
			const DocTerrain& g = d.terrain;
			const std::size_t nv = g.vertexCount();
			if (g.heights.size() != nv || g.floor.size() != nv || g.water.size() != nv || g.layers.size() != g.palette.size())
				throw std::runtime_error(tr("terreno: los datos no cuadran con el tamaño"));
			m.grid = true;
			m.nx   = g.nx;
			m.nz   = g.nz;
			m.maxX = float(d.minX + g.nx * d.cell);
			m.maxZ = float(d.minZ + g.nz * d.cell);
			m.heights.assign(g.heights.begin(), g.heights.end());
			m.floor  = g.floor;
			m.water  = g.water;
			m.layers = g.layers;
			auto mat = [](const DocMaterial& dm, bool floor) {
				const std::uint32_t attr = std::uint32_t(dm.attribute) << 29;
				// Suelos con el bit 25 como la hierba de practice.mod; paredes sin él.
				return MapMaterial { dm.source, dm.index, floor ? attr | 0x02000000u : attr, float(dm.uvScale) };
			};
			for (const DocMaterial& dm : g.palette) m.palette.push_back(mat(dm, true));
			m.border = mat(g.border, false);
		}
		for (int i = 0; i < 3; i++) m.sunDir[i] = float(d.sun[i]);
		const bool twin = d.symmetric;

		auto addWall = [&](const MapWall& w) {
			m.walls.push_back(w);
			if (twin) m.walls.push_back({ -w.ax, -w.az, -w.bx, -w.bz, w.thickness, w.height });
		};
		for (const DocWall& w : d.walls) {
			const float thick = float(w.thickness), height = float(w.height);
			if (w.isArc) {
				// Arco en tramos rectos (mismas cuentas que la arena de serie).
				const float cx = float(w.cx), cz = float(w.cz), r = float(w.radius), from = float(w.from), span = float(w.to) - from;
				const int segs = w.segments;
				if (segs < 1) throw std::runtime_error(tr("arc.segments debe ser >= 1"));
				for (int k = 0; k < segs; k++) {
					const float a0 = (from + k * span / float(segs)) * 3.14159265f / 180.0f;
					const float a1 = (from + (k + 1) * span / float(segs)) * 3.14159265f / 180.0f;
					addWall({ cx + r * std::cos(a0), cz + r * std::sin(a0), cx + r * std::cos(a1), cz + r * std::sin(a1), thick, height });
				}
			} else {
				addWall({ float(w.ax), float(w.az), float(w.bx), float(w.bz), thick, height });
			}
		}
		for (const DocPond& p : d.ponds) {
			MapPond pond { float(p.cx), float(p.cz), float(p.rx), float(p.rz), float(p.island), float(p.depth), float(p.surface) };
			m.ponds.push_back(pond);
			if (twin) {
				pond.cx = -pond.cx;
				pond.cz = -pond.cz;
				m.ponds.push_back(pond);
			}
		}
		for (const DocProp& p : d.props) {
			MapProp prop { p.asset, float(p.x), float(p.y), float(p.z), deg(float(p.rx)), deg(float(p.ry)), deg(float(p.rz)), float(p.scale), p.collide };
			m.props.push_back(prop);
			if (twin) {
				prop.x  = -prop.x;
				prop.z  = -prop.z;
				prop.ry = deg(float(p.ry) + 180.0f);
				m.props.push_back(prop);
			}
		}

		m.routeStep   = float(d.routeStep);
		m.routeMargin = float(d.routeMargin);
		for (double x : d.barrierX) m.barrierX.push_back(float(x));
		for (const auto& p : d.passes) m.passes.push_back({ float(p.first), float(p.second), 0.0f, MapReaction {} });

		auto spot = [](const DocSpot& s) { return MapSpot { float(s.x), float(s.z), deg(float(s.face)), expandReaction(s.reaction) }; };
		for (int p = 0; p < 2; p++) {
			m.bases[p]     = spot(d.bases[p]);
			m.rockets[p]   = spot(d.rockets[p]);
			m.gates[p]     = spot(d.gates[p]);
			m.bombPiles[p] = spot(d.bombPiles[p]);
			for (int c = 0; c < 3; c++) m.onions[p][c] = { float(d.onions[p][c].x), float(d.onions[p][c].z), deg(0.0f), MapReaction {} };
		}
		for (const DocPellet& s : d.pellets) {
			const MapPellet p { float(s.x), float(s.z), pelletId(s.id) };
			m.pellets.push_back(p);
			if (twin) m.pellets.push_back({ -p.x, -p.z, p.id });
		}
		for (const DocPiece& s : d.pieces) {
			if (s.kind < 0 || s.kind > 4) throw std::runtime_error(tr("tipo de pieza desconocido"));
			std::uint32_t part = 0;
			if (!s.part.empty()) {
				bool known = false;
				for (const ShipPartInfo& sp : shipParts()) known = known || s.part == sp.id;
				if (!known) throw std::runtime_error(tr("pieza de la nave desconocida: ") + s.part);
				part = fourcc(s.part);
			}
			const MapPiece p { float(s.x), float(s.z), s.kind, part };
			m.pieces.push_back(p);
			if (twin) m.pieces.push_back({ -p.x, -p.z, p.kind, p.part });
		}
		for (const DocEnemy& e : d.enemies) {
			const EnemyInfo* info = nullptr;
			for (const EnemyInfo& t : enemyTypes()) {
				if (e.type == t.id) info = &t;
			}
			if (!info) throw std::runtime_error(tr("enemigo desconocido: ") + e.type);
			const MapEnemy me { float(e.x), float(e.z), deg(float(e.face)), info->teki, info->boss, expandReaction(e.reaction) };
			m.enemies.push_back(me);
			if (twin) m.enemies.push_back({ -me.x, -me.z, deg(float(e.face) + 180.0f), me.teki, me.boss, me.reaction });
		}
		for (const DocObject& o : d.objects) {
			int kind = -1;
			for (std::size_t k = 0; k < objectKinds().size(); k++) {
				if (o.kind == objectKinds()[k].id) kind = int(k);
			}
			if (kind < 0) throw std::runtime_error(tr("construcción desconocida: ") + o.kind);
			const MapObject mo { kind, float(o.x), float(o.z), deg(float(o.face)), float(o.push), std::max(1, std::min(9, o.work)), float(o.y), expandReaction(o.reaction) };
			m.objects.push_back(mo);
			if (twin) m.objects.push_back({ kind, -mo.x, -mo.z, deg(float(o.face) + 180.0f), mo.push, mo.work, mo.y, mo.reaction });
		}
		m.bigPiece = { float(d.bigX), float(d.bigZ), deg(0.0f), MapReaction {} };
		for (const DocGuard& s : d.guards) {
			const MapSpot g { float(s.x), float(s.z), deg(0.0f), expandReaction(s.reaction) };
			m.guards.push_back(g);
			if (twin) m.guards.push_back({ -g.x, -g.z, g.face, g.reaction });
		}
		if (validate) check(m);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}

bool parseMap(const std::string& text, MapData& out, std::string& error)
{
	MapDoc doc;
	return parseMapDoc(text, doc, error) && expandMap(doc, out, error);
}

std::string writeMapDoc(const MapDoc& d)
{
	json walls = json::array();
	for (const DocWall& w : d.walls) {
		if (w.isArc) {
			walls.push_back({ { "arc", { { "center", { w.cx, w.cz } }, { "radius", w.radius }, { "from", w.from }, { "to", w.to }, { "segments", w.segments } } },
			                  { "thickness", w.thickness },
			                  { "height", w.height } });
		} else {
			walls.push_back({ { "a", { w.ax, w.az } }, { "b", { w.bx, w.bz } }, { "thickness", w.thickness }, { "height", w.height } });
		}
	}
	json ponds = json::array();
	for (const DocPond& p : d.ponds) {
		ponds.push_back({ { "center", { p.cx, p.cz } }, { "radius", { p.rx, p.rz } }, { "island", p.island }, { "depth", p.depth }, { "surface", p.surface } });
	}
	json props = json::array();
	for (const DocProp& p : d.props) {
		props.push_back({ { "asset", p.asset }, { "pos", { p.x, p.y, p.z } }, { "rot", { p.rx, p.ry, p.rz } }, { "scale", p.scale }, { "collide", p.collide } });
	}
	json passes = json::array();
	for (const auto& p : d.passes) passes.push_back({ p.first, p.second });

	json bases = json::array(), rockets = json::array(), gates = json::array(), bombs = json::array(), onions = json::array();
	for (int p = 0; p < 2; p++) {
		bases.push_back(spotJson(d.bases[p], true));
		rockets.push_back(spotJson(d.rockets[p], true));
		gates.push_back(spotJson(d.gates[p], true, true));
		bombs.push_back(spotJson(d.bombPiles[p], false));
	}
	for (int p = 0; p < 2; p++) {
		for (int c = 0; c < 3; c++) {
			const DocOnion& o = d.onions[p][c];
			onions.push_back({ { "player", p }, { "color", kOnionNames[c] }, { "pos", { o.x, o.z } } });
		}
	}
	json pellets = json::array(), pieces = json::array(), guards = json::array();
	for (const DocPellet& s : d.pellets) pellets.push_back({ { "id", s.id }, { "pos", { s.x, s.z } } });
	for (const DocPiece& s : d.pieces) pieces.push_back({ { "kind", pieceKindName(s.kind) }, { "pos", { s.x, s.z } } });
	for (const DocGuard& s : d.guards) {
		json j = { { "enemy", s.enemy }, { "pos", { s.x, s.z } } };
		if (!s.reaction.action.empty()) j["reaction"] = reactionJson(s.reaction);
		guards.push_back(j);
	}
	json enemies = json::array(), objects = json::array();
	for (const DocEnemy& e : d.enemies) {
		json j = { { "type", e.type }, { "pos", { e.x, e.z } }, { "face", e.face } };
		if (!e.reaction.action.empty()) j["reaction"] = reactionJson(e.reaction);
		enemies.push_back(j);
	}
	for (const DocObject& o : d.objects) {
		json j = { { "kind", o.kind }, { "pos", { o.x, o.z } }, { "face", o.face }, { "push", o.push }, { "work", o.work }, { "y", o.y } };
		if (!o.reaction.action.empty()) j["reaction"] = reactionJson(o.reaction);
		objects.push_back(j);
	}
	for (std::size_t i = 0; i < d.pieces.size(); i++) {
		if (!d.pieces[i].part.empty()) pieces[i]["part"] = d.pieces[i].part;
	}

	json root;
	root["format"]      = 1;
	root["mode"]        = "vs";
	root["meta"]        = { { "name", d.name }, { "author", d.author }, { "version", d.version } };
	root["bounds"]      = { { "minX", d.minX }, { "maxX", d.maxX }, { "minZ", d.minZ }, { "maxZ", d.maxZ }, { "wallHeight", d.wallHeight } };
	root["symmetry"]    = d.symmetric ? "rotate180" : "none";
	root["environment"] = { { "baseStage", d.baseStage }, { "sun", { { "dir", { d.sun[0], d.sun[1], d.sun[2] } } } } };
	root["terrain"]     = { { "type", "flat" }, { "source", d.sourceMod }, { "cell", d.cell } };
	if (d.terrain.grid) {
		const DocTerrain& g = d.terrain;
		std::vector<std::uint8_t> h;
		h.reserve(g.heights.size() * 2);
		for (std::int16_t v : g.heights) {
			h.push_back(std::uint8_t(std::uint16_t(v) & 0xFF));
			h.push_back(std::uint8_t(std::uint16_t(v) >> 8));
		}
		json palette = json::array();
		for (const DocMaterial& m : g.palette) palette.push_back(materialJson(m));
		root["terrain"] = { { "type", "grid" },          { "source", d.sourceMod },     { "cell", d.cell },
			                { "size", { g.nx, g.nz } },  { "heights", toBase64(h) },   { "floor", toBase64(g.floor) },
			                { "palette", palette },      { "border", materialJson(g.border) } };
		json layers = json::array();
		for (const auto& l : g.layers) layers.push_back(toBase64(l));
		root["terrain"]["layers"] = layers;
		if (std::find_if(g.water.begin(), g.water.end(), [](std::uint8_t w) { return w != 0; }) != g.water.end()) root["terrain"]["water"] = toBase64(g.water);
	}
	if (!d.course.empty()) {
		root["course"] = d.course;
		json gens      = json::array();
		for (const DocGen& g : d.gens)
			gens.push_back({ { "file", g.file }, { "data", g.data }, { "pos", { g.x, g.y, g.z } }, { "label", g.label }, { "model", g.model } });
		root["gens"] = gens;
	}
	root["walls"]       = walls;
	root["ponds"]       = ponds;
	root["props"]       = props;
	root["routes"]      = { { "auto", true }, { "step", d.routeStep }, { "margin", d.routeMargin }, { "barrierX", d.barrierX }, { "passes", passes } };
	root["vs"]          = { { "bases", bases },     { "onions", onions },
		                    { "rockets", rockets }, { "gates", gates },
		                    { "bombPiles", bombs }, { "pellets", pellets },
		                    { "pieces", pieces },   { "bigPiece", { { "pos", { d.bigX, d.bigZ } } } },
		                    { "guards", guards },   { "enemies", enemies },
		                    { "objects", objects } };
	return root.dump(2) + "\n";
}

MapDoc defaultMapDoc()
{
	// La disposición de bases de la arena de serie sobre un campo vacío.
	MapDoc d;
	// Muralla curva de cada base (centro detrás del borde del mapa): hueco
	// en medio para la puerta de roca y un paso a cada lado. Los muros ya
	// cortan los caminos, así que no hace falta línea de barrera.
	auto arc = [](double from, double to, int segs) { return DocWall { true, 0, 0, 0, 0, -2300, 0, 1150, from, to, segs, 40, 150 }; };
	d.walls  = { arc(-51.5, -27.3, 6), arc(-18.3, -6.0, 3), arc(6.0, 18.3, 3), arc(27.3, 51.5, 6) };
	for (int p = 0; p < 2; p++) {
		const double s = p == 0 ? 1.0 : -1.0;
		d.bases[p]     = { p, s * -1260, 0, p == 0 ? 90.0 : -90.0, DocReaction {} };
		d.rockets[p]   = { p, s * -1480, 0, p == 0 ? 90.0 : -90.0, DocReaction {} };
		d.gates[p]     = { p, s * -1150, 0, 90.0, DocReaction {} };
		d.bombPiles[p] = { p, s * -1480, s * -650, 0, DocReaction {} };
		const double onion[3][2] = { { -1480, 700 }, { -1320, 420 }, { -1320, -420 } };
		for (int c = 0; c < 3; c++) d.onions[p][c] = { p, c, s * onion[c][0], s * onion[c][1] };
	}
	d.pellets = { { "pr01", -1300, -700 }, { "pb01", -1300, 650 } };
	d.pieces  = { { 0, -1000, -720, "" }, { 1, -760, 380, "" }, { 2, -880, 0, "" } };
	makeTerrainGrid(d);
	return d;
}

// ── Terreno ─────────────────────────────────────────────────────────────────

void makeTerrainGrid(MapDoc& d)
{
	if (d.terrain.grid) return;
	DocTerrain& g = d.terrain;
	g.grid        = true;
	g.nx          = std::max(1, int(std::lround((d.maxX - d.minX) / d.cell)));
	g.nz          = std::max(1, int(std::lround((d.maxZ - d.minZ) / d.cell)));
	d.maxX        = d.minX + g.nx * d.cell;
	d.maxZ        = d.minZ + g.nz * d.cell;
	g.heights.assign(g.vertexCount(), 0);
	g.floor.assign(g.vertexCount(), 255);
	g.water.assign(g.vertexCount(), 0);
	if (g.palette.empty()) g.palette.push_back(DocMaterial {}); // la hierba de Impact Site
	g.layers.assign(g.palette.size(), std::vector<std::uint8_t>(g.vertexCount(), 0));
	std::fill(g.layers[0].begin(), g.layers[0].end(), std::uint8_t(255));
}

void applyTerrainShape(MapDoc& d, TerrainShape shape)
{
	makeTerrainGrid(d);
	DocTerrain& g   = d.terrain;
	const double cx = (d.minX + d.maxX) * 0.5, cz = (d.minZ + d.maxZ) * 0.5;
	const double hw = (d.maxX - d.minX) * 0.5, hh = (d.maxZ - d.minZ) * 0.5;
	// Distancia (aproximada, en unidades del mundo) hasta el borde de la forma:
	// positiva dentro. El suelo vale 128 justo en el borde y se difumina en
	// una celda, así el contorno sale curvo.
	auto rect = [](double x, double z, double rx, double rz) { return std::min(rx - std::fabs(x), rz - std::fabs(z)); };
	auto oval = [](double x, double z, double rx, double rz) { return (1.0 - std::sqrt((x / rx) * (x / rx) + (z / rz) * (z / rz))) * std::min(rx, rz); };
	for (int iz = 0; iz <= g.nz; iz++) {
		for (int ix = 0; ix <= g.nx; ix++) {
			const double x = d.minX + ix * d.cell - cx, z = d.minZ + iz * d.cell - cz;
			double in      = 0;
			switch (shape) {
			case TerrainShape::Rectangle: in = rect(x, z, hw * 0.98, hh * 0.98); break;
			case TerrainShape::Oval: in = oval(x, z, hw * 0.98, hh * 0.98); break;
			case TerrainShape::Cross: in = std::max(rect(x, z, hw * 0.98, hh * 0.38), rect(x, z, hw * 0.22, hh * 0.98)); break;
			case TerrainShape::TwoIslands:
				in = std::max({ oval(x - hw * 0.55, z, hw * 0.43, hh * 0.9), oval(x + hw * 0.55, z, hw * 0.43, hh * 0.9), rect(x, z, hw * 0.6, hh * 0.14) });
				break;
			case TerrainShape::Ring:
				in = std::max(std::min(oval(x, z, hw * 0.98, hh * 0.98), -oval(x, z, hw * 0.44, hh * 0.44)), rect(x, z, hw * 0.98, hh * 0.12));
				break;
			}
			const double t = std::max(0.0, std::min(1.0, 0.5 + in / (2.0 * d.cell)));
			g.floor[std::size_t(g.vertexIndex(ix, iz))] = std::uint8_t(std::lround(t * 255.0));
		}
	}
}

void resizeTerrain(MapDoc& d, int left, int right, int top, int bottom)
{
	makeTerrainGrid(d);
	DocTerrain& g = d.terrain;
	const int nx = g.nx + left + right, nz = g.nz + top + bottom;
	if (nx < 2 || nz < 2 || nx > 400 || nz > 400) return;
	const std::size_t nv = std::size_t(nx + 1) * std::size_t(nz + 1);
	std::vector<std::int16_t> heights(nv, 0);
	std::vector<std::uint8_t> floor(nv, 255), water(nv, 0);
	std::vector<std::vector<std::uint8_t>> layers(g.layers.size(), std::vector<std::uint8_t>(nv, 0));
	if (!layers.empty()) std::fill(layers[0].begin(), layers[0].end(), std::uint8_t(255));
	for (int iz = 0; iz <= nz; iz++) {
		for (int ix = 0; ix <= nx; ix++) {
			const int ox = ix - left, oz = iz - top;
			if (ox < 0 || ox > g.nx || oz < 0 || oz > g.nz) continue;
			const std::size_t to = std::size_t(iz * (nx + 1) + ix), from = std::size_t(g.vertexIndex(ox, oz));
			heights[to] = g.heights[from];
			floor[to]   = g.floor[from];
			water[to]   = g.water[from];
			for (std::size_t k = 0; k < layers.size(); k++) layers[k][to] = g.layers[k][from];
		}
	}
	g.nx      = nx;
	g.nz      = nz;
	g.heights = std::move(heights);
	g.floor   = std::move(floor);
	g.water   = std::move(water);
	g.layers  = std::move(layers);
	d.minX -= left * d.cell;
	d.minZ -= top * d.cell;
	d.maxX = d.minX + nx * d.cell;
	d.maxZ = d.minZ + nz * d.cell;
}

namespace {
// Valor de un campo por punto en (x, z), interpolado como los triángulos del
// terreno (a, b, c) y (b, d, c).
template <typename T> float fieldAt(const MapData& m, const std::vector<T>& f, float x, float z)
{
	const float fx = std::max(0.0f, std::min(float(m.nx), (x - m.minX) / m.cell));
	const float fz = std::max(0.0f, std::min(float(m.nz), (z - m.minZ) / m.cell));
	const int ix = std::min(m.nx - 1, int(fx)), iz = std::min(m.nz - 1, int(fz));
	const float tx = fx - ix, tz = fz - iz;
	auto v         = [&](int a, int b) { return float(f[std::size_t(b * (m.nx + 1) + a)]); };
	if (tx + tz <= 1.0f) return v(ix, iz) + (v(ix + 1, iz) - v(ix, iz)) * tx + (v(ix, iz + 1) - v(ix, iz)) * tz;
	return v(ix + 1, iz + 1) + (v(ix, iz + 1) - v(ix + 1, iz + 1)) * (1.0f - tx) + (v(ix + 1, iz) - v(ix + 1, iz + 1)) * (1.0f - tz);
}
} // namespace

float terrainHeight(const MapData& m, float x, float z)
{
	if (!m.grid || m.nx < 1 || m.nz < 1) return 0.0f;
	return fieldAt(m, m.heights, x, z);
}

bool terrainSolid(const MapData& m, float x, float z)
{
	if (!m.grid) return true;
	if (x < m.minX || z < m.minZ || x > m.maxX || z > m.maxZ) return false;
	return fieldAt(m, m.floor, x, z) >= 128.0f;
}

const std::vector<EnemyInfo>& enemyTypes()
{
	// Números de teki.h (TEKI_*) y de Boss.h (GENBOSS_*). Fuera los que el
	// juego deja rotos o que solo tienen sentido en pareja (almejas, huevo).
	static const std::vector<EnemyInfo> list = {
		{ "chappy", "Bulborb enano", "tekis/chappy/chappy.mod", 3, -1, "Pequeño y rápido. Se come a los Pikmin." },
		{ "swallow", "Bulborb moteado", "tekis/swallow/swallow.mod", 4, -1, "Duerme hasta que pasan cerca. Grande y peligroso." },
		{ "chappb", "Bulbear enano", "tekis/chappb/chappb.mod", 31, -1, "Como el Bulborb enano, pero siempre despierto." },
		{ "swallob", "Bulbear moteado", "tekis/swallob/swallob.mod", 32, -1, "Un Bulborb que no duerme y se pasea." },
		{ "frog", "Wollywog amarillo", "tekis/frog/frog.mod", 0, -1, "Salta y aplasta a los Pikmin." },
		{ "frow", "Wollywog", "tekis/frow/frow.mod", 33, -1, "Salta y aplasta a los Pikmin. Vive cerca del agua." },
		{ "otama", "Wogpole", "tekis/otama/otama.mod", 25, -1, "Renacuajo inofensivo; solo en el agua." },
		{ "namazu", "Water Dumple", "tekis/namazu/namazu.mod", 30, -1, "Pez que se esconde en el agua." },
		{ "collec", "Breadbug", "tekis/collec/collec.mod", 8, -1, "Roba piezas y pastillas para su nido." },
		{ "hollec", "Nido de Breadbug", "tekis/hollec/hollec.mod", 12, -1, "Donde el Breadbug guarda lo que roba." },
		{ "kinoko", "Puffstool", "tekis/kinoko/kinoko.mod", 9, -1, "Seta que convierte a los Pikmin en setas." },
		{ "napkid", "Snitchbug", "tekis/napkid/napkid.mod", 11, -1, "Vuela, coge Pikmin y los planta en otro sitio." },
		{ "qurione", "Honeywisp", "tekis/qurione/qurione.mod", 6, -1, "Vuela tranquilo y suelta néctar." },
		{ "mizigen", "Nido de Honeywisp", "tekis/mizigen/mizigen.mod", 5, -1, "De aquí salen Honeywisps." },
		{ "tank", "Blowhog de fuego", "tekis/tank/tank.mod", 15, -1, "Echa fuego: solo los Pikmin rojos lo aguantan." },
		{ "mar", "Blowhog inflado", "tekis/mar/mar.mod", 16, -1, "Flota y sopla a los Pikmin lejos." },
		{ "beatle", "Escarabajo cañón", "tekis/beatle/beatle.mod", 17, -1, "Dispara rocas. Hay que taparle la boca con una bomba." },
		{ "kabekuiA", "Sheargrub hembra", "tekis/kabekuiA/kabekuiA.mod", 18, -1, "Larva pequeña que sale del suelo." },
		{ "kabekuiB", "Sheargrub macho", "tekis/kabekuiB/kabekuiB.mod", 19, -1, "Larva que muerde a los Pikmin." },
		{ "kabekuiC", "Shearwig", "tekis/kabekuiC/kabekuiC.mod", 20, -1, "Larva que vuela al tocarla." },
		{ "miurin", "Mamuta", "tekis/miurin/miurin.mod", 24, -1, "Planta a los Pikmin aplastándolos (salen flores)." },
		{ "dororo", "Smoky Progg", "tekis/dororo/dororo.mod", 22, -1, "Enorme y venenoso. Muy difícil." },
		{ "iwagon", "Roca rodante", "tekis/iwagon/iwagon.mod", 2, -1, "Rocas que ruedan y aplastan." },
		{ "hibaA", "Géiser de fuego", "tekis/hibaA/hibaA.mod", 23, -1, "Suelta llamaradas: solo los Pikmin rojos lo apagan." },
		{ "palm", "Flor de pastillas", "tekis/palm/palm.mod", 7, -1, "Da una pastilla al tirarla." },
		{ "kogane", "Escarabajo iridiscente", "bosses/kogane/kogane.mod", -1, 4, "Huye, y al golpearlo suelta néctar." },
		{ "pom", "Flor de Pikmin", "bosses/pom/pom.mod", -1, 5, "Al lanzarle Pikmin, cambian de color." },
		{ "kumo", "Patas Largas (jefe)", "bosses/kumo/kumo.mod", -1, 0, "Jefe: cae del cielo y pisa a los Pikmin." },
		{ "snake", "Aguijonero (jefe)", "bosses/snake/snake.mod", -1, 1, "Jefe: sale del suelo y picotea." },
		{ "slime", "Goolix (jefe)", "bosses/slime/slime.mod", -1, 2, "Jefe: una masa que se traga a los Pikmin." },
		{ "king", "Emperador Bulblax (jefe)", "bosses/king/king.mod", -1, 3, "Jefe: enorme, rueda y se traga a los Pikmin." },
	};
	return list;
}

const std::vector<ObjectInfo>& objectKinds()
{
	// En el mismo orden que MapObjectKind.
	static const std::vector<ObjectInfo> list = {
		{ "bridge", "Puente", "objects/bridge/brd_test.mod", "Los Pikmin lo construyen a trozos. Une orillas o salva huecos." },
		{ "bridge_long", "Puente largo", "objects/bridge/brd_long.mod", "Como el puente, pero mucho más largo." },
		{ "slope_up", "Rampa de subida", "objects/bridge/slp_u_4.mod", "Los Pikmin la construyen para subir a un sitio alto." },
		{ "slope_down", "Rampa de bajada", "objects/bridge/slp_d_4.mod", "Los Pikmin la construyen para bajar de un sitio alto." },
		{ "push_rock", "Bloque que se empuja", "objects/hinderrock/cube10.mod", "Hacen falta 10 Pikmin para moverlo. Tapa caminos." },
		{ "gate_soft", "Puerta de papel", "objects/gate/gt_soft.mod", "Los Pikmin la derriban a golpes." },
		{ "gate_hard", "Puerta de piedra", "objects/gate/gt_hard.mod", "Muy resistente: los Pikmin tardan mucho en derribarla." },
		{ "gate_bomb", "Puerta de roca negra", "objects/gate/gt_hard.mod", "Solo se rompe con rocas bomba (Pikmin amarillos)." },
		{ "key", "Llave", "objects/key.mod", "El capitán la coge al tocarla. Sirve para abrir una puerta con cerradura." },
		{ "key_door", "Puerta con cerradura", "objects/door.mod", "Se abre si el capitán la toca llevando una llave (y la gasta)." },
		{ "nectar", "Fuente de néctar", "objects/fl_water/fl_water.mod", "Cae una gota de néctar de vez en cuando: los Pikmin que la beben florecen." },
	};
	return list;
}

const std::vector<ShipPartInfo>& shipParts()
{
	static const std::vector<ShipPartInfo> list = {
		{ "ust1", "objects/ufoparts/soto1.mod", 30, 50 },  { "ust2", "objects/ufoparts/soto2.mod", 50, 70 },
		{ "ust3", "objects/ufoparts/soto3.mod", 40, 60 },  { "ust4", "objects/ufoparts/soto4.mod", 40, 60 },
		{ "ust5", "objects/ufoparts/soto5.mod", 20, 40 },  { "uf01", "objects/ufoparts/fuzoku1.mod", 20, 40 },
		{ "uf02", "objects/ufoparts/fuzoku2.mod", 20, 40 }, { "uf03", "objects/ufoparts/fuzoku3.mod", 20, 40 },
		{ "uf04", "objects/ufoparts/fuzoku4.mod", 20, 40 }, { "uf05", "objects/ufoparts/fuzoku5.mod", 30, 50 },
		{ "uf06", "objects/ufoparts/fuzoku6.mod", 15, 25 }, { "uf07", "objects/ufoparts/fuzoku7.mod", 20, 30 },
		{ "uf08", "objects/ufoparts/fuzoku8.mod", 15, 25 }, { "uf09", "objects/ufoparts/fuzoku9.mod", 30, 50 },
		{ "uf10", "objects/ufoparts/fuzoku10.mod", 15, 25 }, { "uf11", "objects/ufoparts/fuzoku11.mod", 15, 25 },
		{ "un01", "objects/ufoparts/naka1.mod", 30, 50 },   { "un02", "objects/ufoparts/naka2.mod", 25, 45 },
		{ "un03", "objects/ufoparts/naka3.mod", 25, 45 },   { "un04", "objects/ufoparts/naka4.mod", 30, 50 },
		{ "un05", "objects/ufoparts/naka5.mod", 15, 25 },   { "un06", "objects/ufoparts/naka6.mod", 30, 50 },
		{ "un07", "objects/ufoparts/naka7.mod", 30, 50 },   { "un08", "objects/ufoparts/naka8.mod", 20, 40 },
		{ "un09", "objects/ufoparts/naka9.mod", 25, 45 },   { "un10", "objects/ufoparts/naka10.mod", 30, 50 },
		{ "un11", "objects/ufoparts/naka11.mod", 40, 95 },  { "un12", "objects/ufoparts/naka12.mod", 20, 40 },
		{ "un13", "objects/ufoparts/naka13.mod", 20, 40 },  { "un14", "objects/ufoparts/naka14.mod", 10, 25 },
	};
	return list;
}

} // namespace nectar
