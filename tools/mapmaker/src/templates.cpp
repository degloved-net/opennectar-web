#include "templates.h"

#include "nectar_mapcore/i18n.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace nm {

using nectar::tr;
using namespace nectar;

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
double smooth(double e0, double e1, double x)
{
	const double t = clamp01((x - e0) / (e1 - e0));
	return t * t * (3.0 - 2.0 * t);
}
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double len(double x, double z) { return std::sqrt(x * x + z * z); }

// Materiales de suelo de los escenarios del juego (ver el explorador del editor).
struct Mat {
	const char* source;
	int index, attribute;
};
const Mat kGrass  = { "courses/practice/practice.mod", 14, 2 };
const Mat kMoss   = { "courses/stage3/yakusima.mod", 15, 2 };
const Mat kLush   = { "courses/laststage/garden.mod", 28, 2 };
const Mat kDirt   = { "courses/stage1/forest.mod", 4, 0 };
const Mat kForest = { "courses/stage1/forest.mod", 0, 2 };
const Mat kEarth  = { "courses/stage2/cave.mod", 20, 4 };
const Mat kSand   = { "courses/stage2/cave.mod", 73, 0 };
const Mat kOrange = { "courses/laststage/garden.mod", 32, 0 };
const Mat kPebble = { "courses/practice/practice.mod", 22, 1 };
const Mat kStones = { "courses/practice/practice.mod", 25, 1 };
const Mat kRock   = { "courses/stage2/cave.mod", 13, 1 };

/// Lo que vale el terreno en un punto.
struct Pt {
	double in  = 1000;  ///< >0 hay suelo (distancia aproximada al borde)
	double h   = 0;     ///< altura
	double wet = -1000; ///< >0 agua
	std::vector<double> w; ///< peso de cada material de la paleta
};

/// Ayuda para montar una plantilla: parte del mapa por defecto (bases,
/// cebollas, cohetes y murallas curvas) y se rellena el terreno y las cosas.
struct Builder {
	MapDoc d;
	unsigned seed = 1;
	std::function<void(double, double, Pt&)> field;

	Builder(const char* name, const char* stage)
	{
		d           = defaultMapDoc();
		d.name      = tr(name);
		d.author    = "Open Nectar";
		d.baseStage = stage;
		d.pellets.clear();
		d.pieces.clear();
		d.terrain.palette.clear();
		d.terrain.layers.clear();
	}
	int mat(const Mat& m)
	{
		DocMaterial dm;
		dm.source    = m.source;
		dm.index     = m.index;
		dm.attribute = m.attribute;
		d.terrain.palette.push_back(dm);
		d.terrain.layers.emplace_back(d.terrain.vertexCount(), std::uint8_t(0));
		return int(d.terrain.palette.size()) - 1;
	}
	Pt sample(double x, double z) const
	{
		Pt p;
		p.w.assign(d.terrain.palette.size(), 0.0);
		if (!p.w.empty()) p.w[0] = 1;
		field(x, z, p);
		return p;
	}
	/// Pasa `field` a los campos por punto del terreno.
	void bake()
	{
		DocTerrain& g = d.terrain;
		for (int iz = 0; iz <= g.nz; iz++) {
			for (int ix = 0; ix <= g.nx; ix++) {
				const double x = d.minX + ix * d.cell, z = d.minZ + iz * d.cell;
				const Pt p     = sample(x, z);
				const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
				g.floor[v]          = std::uint8_t(std::lround(clamp01(0.5 + p.in / (2.0 * d.cell)) * 255.0));
				g.water[v]          = std::uint8_t(std::lround(clamp01(0.5 + p.wet / (2.0 * d.cell)) * 255.0));
				g.heights[v]        = std::int16_t(std::lround(std::max(-2000.0, std::min(2000.0, p.h))));
				double sum          = 0;
				for (double w : p.w) sum += std::max(0.0, w);
				for (std::size_t k = 0; k < g.layers.size(); k++)
					g.layers[k][v] = std::uint8_t(std::lround(sum > 0 ? std::max(0.0, p.w[k]) / sum * 255.0 : (k == 0 ? 255.0 : 0.0)));
			}
		}
	}
	double rnd()
	{
		seed = seed * 1664525u + 1013904223u;
		return double((seed >> 8) & 0xFFFF) / 65535.0;
	}
	/// Decoración esparcida en la mitad de J1 (el espejo pone la otra), en
	/// suelo seco y llano donde `where` lo permita.
	void scatter(std::vector<const char*> set, int count, double spacing, std::function<bool(double, double)> where, double sizeVar = 0.25)
	{
		int placed = 0;
		for (int attempt = 0; attempt < count * 30 && placed < count; attempt++) {
			const double x = lerp(d.minX + 60, 0.0, rnd()), z = lerp(d.minZ + 60, d.maxZ - 60, rnd());
			if (!where(x, z)) continue;
			const Pt p = sample(x, z);
			if (p.in < 40 || p.wet > -30) continue;
			bool steep = false;
			for (int k = 0; k < 4 && !steep; k++) {
				const double ox = k == 0 ? 25 : k == 1 ? -25 : 0, oz = k == 2 ? 25 : k == 3 ? -25 : 0;
				steep = std::fabs(sample(x + ox, z + oz).h - p.h) > 12;
			}
			if (steep) continue;
			bool crowded = false;
			for (const DocProp& o : d.props) crowded = crowded || len(o.x - x, o.z - z) < spacing || len(-o.x - x, -o.z - z) < spacing;
			// Lejos de lo que hay que coger o pisar.
			for (const DocPiece& o : d.pieces) crowded = crowded || len(o.x - x, o.z - z) < 90;
			for (const DocPellet& o : d.pellets) crowded = crowded || len(o.x - x, o.z - z) < 90;
			if (crowded) continue;
			DocProp pr;
			pr.asset   = set[std::size_t(rnd() * double(set.size())) % set.size()];
			pr.x       = x;
			pr.z       = z;
			pr.y       = p.h;
			pr.ry      = rnd() * 360.0;
			pr.scale   = 1.0 + (rnd() * 2.0 - 1.0) * sizeVar;
			pr.collide = false;
			d.props.push_back(pr);
			placed++;
		}
	}
	void enemy(const char* type, double x, double z, double face = 90) { d.enemies.push_back({ type, x, z, face }); }
	void object(const char* kind, double x, double z, double face, double y = 0)
	{
		DocObject o;
		o.kind = kind;
		o.x    = x;
		o.z    = z;
		o.face = face;
		o.y    = y;
		d.objects.push_back(o);
	}
	void arcWall(double cx, double cz, double r, double from, double to, double height = 90)
	{
		DocWall w;
		w.isArc    = true;
		w.cx       = cx;
		w.cz       = cz;
		w.radius   = r;
		w.from     = from;
		w.to       = to;
		w.segments = std::max(3, int((to - from) / 30.0));
		w.height   = height;
		d.walls.push_back(w);
	}
	/// Rincón amurallado cuya entrada (mirando a -X, hacia la base de J1) tapa
	/// un bloque: al empujarlo hacia dentro, queda libre el paso.
	void rockPocket(double cx, double cz, double r)
	{
		arcWall(cx, cz, r, -162, 162); // hueco de ±18° en 180°: lo justo para el bloque
		DocObject o;
		o.kind = "push_rock";
		o.x    = cx - r - 40;
		o.z    = cz;
		o.face = 90;
		o.push = r * 0.9;
		d.objects.push_back(o);
	}
	/// Rincón amurallado con una puerta de papel en la entrada (que mira a +Z
	/// si `up`, o a -Z).
	void gatePocket(double cx, double cz, double r, bool up)
	{
		const double gap = up ? 90 : -90;
		arcWall(cx, cz, r, gap + 40, gap + 320);
		object("gate_soft", cx, cz + (up ? r : -r), 0);
	}
	MapDoc done()
	{
		bake();
		// Lo que esté en J1 se repite en J2 (mapa espejo).
		return d;
	}
};

/// Zona de las bases (|x| > 1100): llana, seca y con suelo.
double baseZone(double x) { return smooth(1000, 1150, std::fabs(x)); }

const std::vector<const char*> kFlowers = { "objects/plants/tanpopo.mod", "objects/plants/ooinu_s.mod", "objects/plants/ooinu_l.mod", "objects/plants/clover.mod" };
const std::vector<const char*> kMeadow  = { "objects/weeds/weed1.mod", "objects/plants/hutaba.mod", "objects/plants/hae.mod" };
const std::vector<const char*> kStonesD = { "objects/weeds/stone1.mod", "objects/weeds/stone2.mod", "objects/weeds/stone3.mod" };
const std::vector<const char*> kShore   = { "objects/plants/mizukusa.mod", "objects/plants/ine.mod", "objects/plants/wakame_s.mod" };
const std::vector<const char*> kShrooms = { "objects/plants/kinokolt.mod", "objects/plants/hae.mod", "objects/plants/chidome.mod" };

// ── 1. Prado tranquilo: sencillo ────────────────────────────────────────────
MapDoc meadow()
{
	Builder b("Prado tranquilo", "stages/chal0.ini");
	const int grass = b.mat(kGrass), dirt = b.mat(kDirt);
	b.field = [=](double x, double z, Pt& p) {
		const double hill = 35.0 * std::cos(x / 420.0) * std::cos(z / 360.0);
		p.h               = hill * (1.0 - baseZone(x));
		const double path = smooth(110, 50, std::fabs(z - 90.0 * std::sin(x / 500.0)));
		p.w[grass]        = 1 - path;
		p.w[dirt]         = path;
	};
	b.d.pellets = { { "pr01", -1300, -700 }, { "pb01", -1300, 650 }, { "py05", -700, 500 } };
	b.d.pieces  = { { 0, -900, -560, "" }, { 1, -620, 380, "" }, { 2, -300, -150, "" } };
	b.enemy("chappy", -450, 560);
	b.scatter(kFlowers, 26, 110, [](double, double) { return true; });
	b.scatter(kMeadow, 30, 70, [](double, double) { return true; });
	return b.done();
}

// ── 2. Río y puentes: un río que solo cruzan los azules o los puentes ───────
MapDoc river()
{
	Builder b("Río y puentes", "stages/chal0.ini");
	const int grass = b.mat(kLush), sand = b.mat(kSand), dirt = b.mat(kDirt);
	auto riverX     = [](double z) { return 140.0 * std::sin(z / 320.0); }; // simétrico respecto al centro
	// Medio ancho del río: se estrecha donde cruzan los puentes (el corto
	// llega a 180 de largo y el largo a 360).
	auto bump  = [](double z, double c) { return std::max(smooth(170, 50, std::fabs(z - c)), smooth(170, 50, std::fabs(z + c))); };
	auto halfW = [=](double z) { return 170.0 - 100.0 * bump(z, 520) - 35.0 * bump(z, 160); };
	b.field    = [=](double x, double z, Pt& p) {
        const double d = std::fabs(x - riverX(z)), hw = halfW(z);
        p.wet          = hw - d;
        p.h            = -18.0 * smooth(hw + 90, hw - 50, d);
        const double bank = smooth(hw + 130, hw + 10, d);
        const double road = smooth(90, 40, std::fabs(z - 520)) + smooth(90, 40, std::fabs(z + 520));
        p.w[grass]        = 1 - bank;
        p.w[sand]         = bank;
        p.w[dirt]         = std::min(1.0, road) * (1 - bank);
	};
	// Puentes de J1 (el espejo pone el otro): se construyen de la orilla de
	// J1 a la del rival.
	b.object("bridge", riverX(-520) - halfW(-520) - 15, -520, 90);
	b.object("bridge_long", riverX(160) - halfW(160) - 15, 160, 90);
	// Rincones con premio: uno tapado por un bloque que se empuja y otro
	// cerrado por una puerta de papel.
	b.rockPocket(-760, 560, 190);
	b.gatePocket(-640, -690, 150, true);
	b.d.pellets = { { "pr05", -1300, -700 }, { "pb10", -260, 640 }, { "py10", -740, 560 } };
	b.d.pieces  = { { 0, 520, -300, "" }, { 1, -640, -700, "" }, { 4, riverX(700) + 60, 700, "" } }; // la última, en el agua
	b.d.guards  = { { "chappy", 380, -520 } };
	b.enemy("chappy", -460, 300);
	auto clear = [](double x, double z) { return len(x + 760, z - 560) > 230 && len(x + 640, z + 690) > 190; };
	b.scatter(kShore, 24, 90, [=](double x, double z) { return std::fabs(x - riverX(z)) < 300 && clear(x, z); });
	b.scatter(kMeadow, 22, 80, [=](double x, double z) { return std::fabs(x - riverX(z)) > 350 && clear(x, z); });
	return b.done();
}

// ── 3. Bosque de setas: colinas, setas y bichos que roban ───────────────────
MapDoc mushrooms()
{
	Builder b("Bosque de setas", "stages/chal2.ini");
	const int moss = b.mat(kForest), earth = b.mat(kEarth), grass = b.mat(kMoss);
	b.field = [=](double x, double z, Pt& p) {
		const double hills = 70.0 * std::sin(x / 300.0) * std::sin(z / 260.0) + 30.0 * std::cos((x + z) / 210.0);
		const double flat  = std::max(baseZone(x), smooth(260, 160, len(x, z))); // centro llano para la pieza grande
		p.h                = hills * (1 - flat);
		const double low   = smooth(10, -40, p.h);
		p.w[moss]          = 0.7;
		p.w[earth]         = low;
		p.w[grass]         = smooth(30, 70, p.h);
	};
	b.d.pellets = { { "pr05", -1300, -700 }, { "pb05", -1300, 650 }, { "py10", -420, -620 } };
	b.d.pieces  = { { 0, -760, 600, "" }, { 1, -520, -300, "" }, { 3, -260, 520, "" } };
	b.d.guards  = { { "chappy", -200, 560 } };
	b.enemy("kinoko", -380, 0);
	b.enemy("collec", -650, -620);
	b.enemy("hollec", -820, -700);
	b.enemy("qurione", -600, 200);
	b.scatter(kShrooms, 40, 80, [](double, double) { return true; });
	b.scatter({ "objects/weeds/stone1.mod", "objects/weeds/stone2.mod" }, 10, 140, [](double, double) { return true; });
	return b.done();
}

// ── 4. Lagos azules: agua por todas partes ──────────────────────────────────
MapDoc lakes()
{
	Builder b("Lagos azules", "stages/chal3.ini");
	const int moss = b.mat(kMoss), sand = b.mat(kSand), grass = b.mat(kGrass);
	auto lake      = [](double x, double z) {
        // Lago central con una isla y dos charcas (una por jugador).
        const double c    = (1.0 - len(x / 720.0, z / 440.0)) * 440.0;
        const double isle = 150.0 - len(x, z);
        const double p1   = (1.0 - len((x + 560.0) / 260.0, (z - 560.0) / 170.0)) * 170.0;
        const double p2   = (1.0 - len((x - 560.0) / 260.0, (z + 560.0) / 170.0)) * 170.0;
        return std::max({ std::min(c, -isle), p1, p2 });
	};
	b.field = [=](double x, double z, Pt& p) {
		const double w = lake(x, z);
		p.wet          = w;
		p.h            = -14.0 * smooth(-90, 0, w);
		const double s = smooth(-120, -20, w);
		p.w[moss]      = 1 - s;
		p.w[sand]      = s;
		p.w[grass]     = baseZone(x);
	};
	b.d.pellets = { { "pb05", -1300, -700 }, { "pb10", -1300, 650 }, { "pr05", 0, 60 }, { "py05", -880, -240 } };
	b.d.bigX    = 0;
	b.d.bigZ    = -60;
	b.d.pieces  = { { 4, -380, 120, "" }, { 4, -560, 560, "" }, { 0, -860, 300, "" } };
	b.enemy("frow", -700, -420);
	b.enemy("namazu", -300, -220);
	b.enemy("otama", -520, 600);
	b.enemy("frog", -200, 700);
	b.scatter(kShore, 40, 75, [=](double x, double z) { return lake(x, z) > -160; });
	b.scatter(kMeadow, 14, 90, [=](double x, double z) { return lake(x, z) < -200; });
	return b.done();
}

// ── 5. Volcán: fuego para los Pikmin rojos ──────────────────────────────────
MapDoc volcano()
{
	Builder b("Volcán", "stages/chal4.ini");
	const int orange = b.mat(kOrange), earth = b.mat(kEarth), rock = b.mat(kRock), grass = b.mat(kGrass);
	b.field = [=](double x, double z, Pt& p) {
		const double r = len(x, z);
		// Cono con cráter: borde a 290 en r = 260, fondo a 110 dentro.
		double cone = r < 260 ? lerp(110.0, 290.0, smooth(150, 260, r)) : 290.0 * clamp01(1.0 - (r - 260.0) / 700.0);
		// Dos pasos en cuesta suave hasta el fondo, uno por cada base.
		const double lane = smooth(170, 110, std::fabs(z));
		const double ramp = r < 150 ? 110.0 : std::min(cone, 110.0 + (std::fabs(x) - 150.0) * 0.4);
		p.h               = lerp(cone, std::min(cone, ramp), lane) * (1 - baseZone(x));
		p.w[orange]       = 1.0;
		p.w[earth]        = smooth(500, 760, r) * 0.8;
		p.w[rock]         = smooth(280, 200, r) * (1 - lane);
		p.w[grass]        = baseZone(x) * 2.0;
	};
	b.d.bigX    = 0;
	b.d.bigZ    = 0;
	b.d.pellets = { { "pr05", -1300, -700 }, { "pr10", -1300, 650 }, { "pr20", -60, 90 } };
	b.d.pieces  = { { 0, -560, -520, "" }, { 3, -380, 380, "" }, { 1, -200, -60, "" } };
	b.d.guards  = { { "chappy", -330, 330 } };
	b.enemy("hibaA", -90, -90);
	b.enemy("hibaA", -640, 120);
	b.enemy("tank", -520, -300, 90);
	b.enemy("iwagon", -760, 560, 90);
	b.scatter(kStonesD, 34, 90, [](double, double) { return true; }, 0.5);
	b.scatter({ "objects/plants/hae.mod" }, 10, 120, [](double x, double z) { return len(x, z) > 650; });
	return b.done();
}

// ── 6. Mesetas amarillas: alturas para lanzar amarillos ─────────────────────
MapDoc plateaus()
{
	Builder b("Mesetas amarillas", "stages/chal1.ini");
	const int lush = b.mat(kLush), dirt = b.mat(kDirt), pebble = b.mat(kPebble), rock = b.mat(kRock);
	auto box  = [](double x, double z, double hx, double hz) { return std::min(hx - std::fabs(x), hz - std::fabs(z)); };
	auto tier = [=](double x, double z) {
		// Meseta media (130) y la alta del centro (260), con rampas suaves:
		// una por jugador a cada nivel (las de J2, en el lado contrario).
		double h       = 130.0 * smooth(-30, 20, box(x, z, 820, 640));
		h              = std::max(h, 260.0 * smooth(-30, 20, box(x, z, 360, 300)));
		auto rampAlong = [](double t, double t0, double t1, double h0, double h1) { return lerp(h0, h1, clamp01((t - t0) / (t1 - t0))); };
		for (int s = -1; s <= 1; s += 2) {
			const double X = x * s, Z = z * s; // J1 (s = 1) y su espejo
			if (std::fabs(Z - 460) < 90 && X > -1150 && X < -780) h = std::max(h, rampAlong(X, -1130, -800, 0, 130));
			if (std::fabs(Z + 160) < 90 && X > -720 && X < -330) h = std::max(h, rampAlong(X, -700, -360, 130, 260));
		}
		return h;
	};
	b.field = [=](double x, double z, Pt& p) {
		p.h = tier(x, z);
		// Roca en los cortados (donde la altura cambia de golpe).
		const double gx = std::fabs(tier(x + 30, z) - tier(x - 30, z)), gz = std::fabs(tier(x, z + 30) - tier(x, z - 30));
		const double cliff = smooth(25, 60, std::max(gx, gz));
		p.w[lush]          = smooth(40, 0, p.h);
		p.w[dirt]          = smooth(60, 120, p.h) * smooth(220, 160, p.h);
		p.w[pebble]        = smooth(200, 250, p.h);
		p.w[rock]          = cliff * 3.0;
	};
	b.d.bigX    = 0;
	b.d.bigZ    = 0;
	b.d.pellets = { { "py05", -1300, -700 }, { "py10", -600, 300 }, { "pr05", -1300, 650 }, { "py20", -180, 200 } };
	b.d.pieces  = { { 0, -650, -480, "" }, { 1, -260, -220, "" }, { 2, -900, -200, "" } };
	// Puerta de roca negra arriba de cada rampa alta: solo con bombas.
	b.object("gate_bomb", -330, -160, 90, 0);
	b.enemy("kabekuiA", -560, 420);
	b.enemy("kabekuiB", -300, 520);
	b.enemy("mar", -150, -80);
	b.scatter(kStonesD, 18, 110, [](double, double) { return true; });
	b.scatter(kMeadow, 22, 90, [=](double x, double z) { return tier(x, z) < 20; });
	return b.done();
}

// ── 7. Fortaleza: de todo, para lucirse ─────────────────────────────────────
MapDoc fortress()
{
	Builder b("Fortaleza", "stages/chal0.ini");
	const int grass = b.mat(kGrass), stones = b.mat(kStones), sand = b.mat(kSand), dirt = b.mat(kDirt);
	auto causeway   = [](double z) { return smooth(150, 90, std::fabs(z)); }; // calzadas a lo largo de x
	b.field         = [=](double x, double z, Pt& p) {
        const double r    = len(x, z);
        const double cw   = causeway(z);
        // Foso entre r 430 y 570; la fortaleza dentro, a 150 de alto.
        const double moat = std::min(r - 430.0, 570.0 - r);
        p.wet             = moat * (1 - cw) - cw * 100;
        const double fort = 150.0 * smooth(440, 410, r);
        // Las calzadas suben en cuesta suave desde fuera (r 900) hasta dentro (r 330).
        const double up   = 150.0 * clamp01((900.0 - r) / 570.0);
        p.h               = lerp(fort, std::max(fort, up), cw) - 16.0 * smooth(-40, 0, p.wet);
        p.w[grass]        = 1.0;
        p.w[stones]       = smooth(450, 400, r) * 2.0;
        p.w[sand]         = smooth(-90, -10, p.wet);
        p.w[dirt]         = cw * smooth(950, 850, r);
	};
	// Murallas sobre el borde de la fortaleza, con hueco en cada calzada.
	auto arc = [](double from, double to) {
		DocWall w;
		w.isArc    = true;
		w.cx       = 0;
		w.cz       = 0;
		w.radius   = 405;
		w.from     = from;
		w.to       = to;
		w.segments = 8;
		w.height   = 120;
		return w;
	};
	b.d.walls.push_back(arc(-160, -20)); // el espejo pone el otro lado
	// Cada calzada: puerta con cerradura al pie; la llave, tras una puerta de papel.
	// Puerta con cerradura en medio de la calzada, sobre el foso, y muretes a
	// los lados para que no se pueda rodear.
	b.object("key_door", -500, 0, 90);
	for (int s = -1; s <= 1; s += 2) {
		DocWall w;
		w.ax     = -500;
		w.bx     = -500;
		w.az     = s * 58;
		w.bz     = s * 220;
		w.height = 240; // salen del foso y deben sobrepasar la calzada
		b.d.walls.push_back(w);
	}
	// La llave, en un rincón cerrado por una puerta de papel.
	{
		DocWall w;
		w.isArc    = true;
		w.cx       = -900;
		w.cz       = -720;
		w.radius   = 120;
		w.from     = 130;
		w.to       = 410;
		w.segments = 7;
		w.height   = 90;
		b.d.walls.push_back(w);
	}
	b.object("gate_soft", -900, -600, 0);
	b.object("key", -900, -720, 0);
	b.object("nectar", 0, 180, 0);
	b.rockPocket(-800, 680, 150);
	b.d.bigX    = 0;
	b.d.bigZ    = -120;
	b.d.pellets = { { "pr05", -1300, -700 }, { "pb05", -1300, 650 }, { "py10", -790, 680 }, { "pr10", -120, 220 } };
	b.d.pieces  = { { 0, -700, 420, "" }, { 4, -400, -300, "" }, { 3, -220, -200, "" }, { 1, -950, 200, "" } };
	b.d.guards  = { { "chappy", -260, -260 } };
	b.enemy("swallob", -800, -260, 90);
	b.enemy("mizigen", -700, -470);
	b.enemy("palm", -560, 420);
	b.scatter(kFlowers, 14, 120, [](double x, double z) { return len(x, z) > 650; });
	b.scatter(kStonesD, 10, 130, [](double x, double z) { return len(x, z) < 380; });
	b.scatter(kShore, 14, 90, [](double x, double z) { return len(x, z) > 380 && len(x, z) < 640 && std::fabs(z) > 180; });
	return b.done();
}

} // namespace

const std::vector<TemplateInfo>& mapTemplates()
{
	static const std::vector<TemplateInfo> list = {
		{ "meadow", "Prado tranquilo", "Sencillo: colinas suaves, flores y lo justo para jugar.", meadow },
		{ "river", "Río y puentes", "Un río que solo cruzan los azules o los puentes que se construyen.", river },
		{ "mushrooms", "Bosque de setas", "Colinas oscuras con Puffstools, Breadbugs y setas.", mushrooms },
		{ "lakes", "Lagos azules", "Agua por todas partes: piezas sumergidas y bichos de charca.", lakes },
		{ "volcano", "Volcán", "Fuego: un cráter con géiseres y Blowhogs. Los rojos mandan.", volcano },
		{ "plateaus", "Mesetas amarillas", "Tres alturas, acantilados y puertas de roca negra: lanza amarillos.", plateaus },
		{ "fortress", "Fortaleza", "De todo: foso, calzadas, llaves, puertas con cerradura, néctar y murallas.", fortress },
	};
	return list;
}

} // namespace nm
