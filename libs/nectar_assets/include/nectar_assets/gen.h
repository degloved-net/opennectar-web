#pragma once

// Generadores .gen (GeneratorMgr::read del juego): qué aparece en un mapa,
// dónde y cuándo. Solo lectura de momento; hará falta para el modo historia.

#include "nectar_assets/mod.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// Parámetro con etiqueta (Parameters del juego): id + bytes sin interpretar.
struct GenParam {
	std::uint32_t id = 0;
	std::vector<std::uint8_t> bytes;
	std::int32_t asInt() const;
	float asFloat() const;
};

struct GenPart {
	std::uint32_t kind    = 0; ///< 'teki', 'pelt', 'circ', '1one'...
	std::uint32_t version = 0;
	std::vector<GenParam> params;
	const GenParam* param(std::uint32_t id) const;
};

struct GenObject : GenPart {
	int type = -1;          ///< tipo de enemigo, planta, objeto de mapa...
	std::string name;       ///< objetos con nombre (item, work)
	std::uint32_t pellet = 0; ///< 'pelt': id de la pastilla; 'teki': la que suelta
	int bossItem = -1;      ///< 'boss': objeto que suelta
};

struct GenArea : GenPart {
	Vec3 offset; ///< desplazamiento del área (GenArea::doRead)
};

struct Generator {
	std::uint32_t name = 0, version = 0;
	std::uint32_t carryOver = 0;
	std::string memo;
	Vec3 pos, offset;
	GenObject object;
	GenArea area;
	GenPart type;
	/// El generador tal cual está en el archivo (para reescribirlo).
	std::vector<std::uint8_t> raw;
};

/// Posición (pos) dentro de Generator::raw: tras nombre, versión, _70,
/// carryOver y la nota de 32 bytes.
constexpr std::size_t kGenPosOffset = 48;

struct GenFile {
	std::uint32_t version = 0;
	Vec3 naviPos;
	float naviDirection = 0.0f;
	std::vector<Generator> generators;
};

bool parseGen(const std::uint8_t* data, std::size_t size, GenFile& out, std::string& error);
bool loadGen(const std::string& path, GenFile& out, std::string& error);

/// Archivo .gen con estos generadores en crudo (cabecera v0.1, el capitán en
/// el origen). Cada uno debe empezar como Generator::raw.
std::vector<std::uint8_t> writeGen(const std::vector<std::vector<std::uint8_t>>& generators);

/// 'teki' -> "teki".
std::string genIdString(std::uint32_t id);

} // namespace nectar
