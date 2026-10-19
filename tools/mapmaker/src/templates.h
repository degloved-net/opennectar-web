#pragma once

// Plantillas: mapas completos ya hechos para empezar (o para jugar tal cual).
// Se generan por código, así siempre cuadran con el formato actual.

#include "nectar_mapcore/map.h"

#include <string>
#include <vector>

namespace nm {

struct TemplateInfo {
	const char* id;    ///< para la miniatura en caché
	const char* name;  ///< en español (se traduce al mostrarlo)
	const char* blurb; ///< una línea: tema y dificultad
	nectar::MapDoc (*build)();
};
const std::vector<TemplateInfo>& mapTemplates();

} // namespace nm
