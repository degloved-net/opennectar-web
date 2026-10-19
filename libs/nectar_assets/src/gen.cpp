#include "nectar_assets/gen.h"

#include "reader.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace nectar {

namespace {

constexpr std::uint32_t fourcc(const char (&s)[5])
{
	return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) | (std::uint32_t(std::uint8_t(s[2])) << 8)
	     | std::uint8_t(s[3]);
}

// Parameters::read: entradas (id en los 24 bits altos, tamaño en los 8
// bajos) hasta -1.
void readParams(Reader& r, std::vector<GenParam>& out)
{
	while (r.ok && r.left() >= 4) {
		const std::int32_t head = r.s32();
		if (head == -1) return;
		GenParam p;
		p.id                  = std::uint32_t(head) & 0xFFFFFF00u;
		const std::size_t len = std::uint32_t(head) & 0xFFu;
		if (!r.need(len)) return;
		p.bytes.assign(r.data + r.pos, r.data + r.pos + len);
		r.skip(len);
		out.push_back(p);
	}
	r.ok = false; // sin el -1 final
}

// Los que registran las fábricas del juego. Con otro id el juego no lee
// nada de esa parte (getProduct devuelve nulo) y sigue con la siguiente.
bool knownObject(std::uint32_t k)
{
	static const std::uint32_t ids[] = { fourcc("piki"), fourcc("teki"), fourcc("item"), fourcc("pelt"), fourcc("navi"), fourcc("mobj"),
		                                 fourcc("actr"), fourcc("plnt"), fourcc("mpar"), fourcc("work"), fourcc("boss"), fourcc("debg") };
	for (std::uint32_t id : ids) {
		if (k == id) return true;
	}
	return false;
}
bool knownArea(std::uint32_t k) { return k == fourcc("pint") || k == fourcc("circ"); }
bool knownType(std::uint32_t k) { return k == fourcc("1one") || k == fourcc("aton") || k == fourcc("irnd"); }

// GenBase::read: versión, datos propios del tipo y parámetros.
void readObject(Reader& r, GenObject& o)
{
	if (!knownObject(o.kind)) return;
	o.version = r.id();
	switch (o.kind) {
	case fourcc("teki"): {
		const std::uint32_t v = o.version;
		o.type                = v <= 8 ? r.s32() : r.u8();
		// TekiPersonality::read según la versión.
		if (v <= 7) {
			r.skip(8);
			r.skip(4 * 5 + 4 * 5); // ParaMultiParameters: 5 enteros y 5 reales
		} else if (v <= 8) {
			r.skip(8);
			o.pellet = r.id();
			r.skip(4 * 3 + 4 * 4);
		} else if (v <= 9) {
			r.skip(2);
			o.pellet = r.id();
			r.skip(4 * 3 + 4 * 4);
		} else {
			r.skip(2);
			o.pellet = r.id();
			r.skip(4 * 5 + 4 * 5);
		}
		break;
	}
	case fourcc("mobj"):
	case fourcc("actr"):
	case fourcc("plnt"): o.type = r.s32(); break;
	case fourcc("pelt"): o.pellet = r.id(); break;
	case fourcc("item"):
		o.name = r.str();
		if (o.version != fourcc("v0.0")) r.skip(32 + 32); // escena y nombre a mostrar
		break;
	case fourcc("mpar"): {
		o.type = r.s32();
		r.skip(4);
		if (r.s32() == 1) r.skip(4 * 6);
		break;
	}
	case fourcc("work"): {
		o.name = r.str();
		if (o.version == fourcc("v0.2") || o.version == fourcc("v0.3")) r.str(); // forma
		if (o.version == fourcc("v0.3") && o.name == "move stone") r.skip(12); // posición de la roca
		break;
	}
	case fourcc("boss"):
		if (o.version == fourcc("v0.0")) {
			o.type = r.s32();
		} else if (o.version < 2) {
			o.type     = r.s32();
			o.bossItem = r.s32();
			r.skip(4 + 4 + 4);
		} else {
			const std::uint32_t flags = r.u32();
			o.type                    = int(flags & 0xF);
			o.bossItem                = int((flags >> 4) & 0x3);
		}
		break;
	default: // 'piki', 'navi', 'debg': sin datos propios
		break;
	}
	readParams(r, o.params);
}

} // namespace

std::int32_t GenParam::asInt() const
{
	if (bytes.size() < 4) return 0;
	return std::int32_t((std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) | (std::uint32_t(bytes[2]) << 8) | bytes[3]);
}

float GenParam::asFloat() const
{
	const std::int32_t v = asInt();
	float f;
	std::memcpy(&f, &v, 4);
	return f;
}

const GenParam* GenPart::param(std::uint32_t id) const
{
	for (const GenParam& p : params) {
		if (p.id == (id & 0xFFFFFF00u)) return &p;
	}
	return nullptr;
}

std::string genIdString(std::uint32_t id) { return idString(id); }

bool parseGen(const std::uint8_t* data, std::size_t size, GenFile& out, std::string& error)
{
	out = GenFile();
	Reader r(data, size);
	out.version = r.id();
	out.naviPos = { r.f32(), r.f32(), r.f32() };
	if (out.version == fourcc("v0.1")) out.naviDirection = r.f32();
	const std::int32_t n = r.s32();
	if (!r.ok || n < 0 || n > 65536) {
		error = "cabecera rota";
		return false;
	}
	for (int i = 0; i < n; i++) {
		Generator g;
		const std::size_t start = r.pos;
		g.name      = r.id();
		g.version   = r.id();
		r.id(); // _70
		g.carryOver = r.u32();
		for (int k = 0; k < 32; k++) {
			const char c = char(r.u8());
			if (c) g.memo += c;
		}
		g.pos    = { r.f32(), r.f32(), r.f32() };
		g.offset = { r.f32(), r.f32(), r.f32() };

		g.object.kind = r.id();
		readObject(r, g.object);
		g.area.kind = r.id();
		if (knownArea(g.area.kind)) {
			g.area.version = r.id();
			g.area.offset  = { r.f32(), r.f32(), r.f32() };
			readParams(r, g.area.params);
		}
		g.type.kind = r.id();
		if (knownType(g.type.kind)) {
			g.type.version = r.id();
			readParams(r, g.type.params);
		}
		if (!r.ok) {
			error = "generador " + std::to_string(i) + " de " + std::to_string(n) + " cortado o desconocido (" + idString(g.object.kind) + ")";
			return false;
		}
		g.raw.assign(data + start, data + r.pos);
		out.generators.push_back(g);
	}
	return true;
}

std::vector<std::uint8_t> writeGen(const std::vector<std::vector<std::uint8_t>>& generators)
{
	std::vector<std::uint8_t> out;
	auto u32 = [&](std::uint32_t v) {
		for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
	};
	u32(fourcc("1.0v")); // los id van al revés en el archivo ('v0.1')
	for (int k = 0; k < 4; k++) u32(0); // capitán en (0, 0, 0) mirando a 0
	u32(std::uint32_t(generators.size()));
	for (const auto& g : generators) out.insert(out.end(), g.begin(), g.end());
	return out;
}

bool loadGen(const std::string& path, GenFile& out, std::string& error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		error = "no se puede abrir " + path;
		return false;
	}
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return parseGen(bytes.data(), bytes.size(), out, error);
}

} // namespace nectar
