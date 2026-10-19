#pragma once

// Escenarios del juego como base de un mapa VS: el .mod del nivel tal cual
// y sus generadores (enemigos, plantas, hierbas, puentes...) como cosas que
// se pueden mover o quitar. Las bases de VS se colocan solas sobre la red de
// caminos del nivel, lo más lejos posible una de otra.

#include "nectar_mapcore/map.h"

#include <string>
#include <vector>

namespace nm {

struct CourseChoice {
	const char* stageIni; ///< "stages/stage1.ini"
	const char* genDir;   ///< "stages/stage1/"
	const char* name;     ///< en español (se traduce al mostrarlo)
};
const std::vector<CourseChoice>& storyCourses();
const std::vector<CourseChoice>& challengeCourses();

bool importCourse(const std::string& dataDir, const CourseChoice& c, nectar::MapDoc& out, std::string& error);

/// Altura del suelo de un escenario (lo más alto que se pisa en cada punto),
/// para colocar cosas en el editor.
struct CourseField {
	std::string course; ///< de qué .mod es
	float minX = 0, minZ = 0, cell = 20;
	int nx = 0, nz = 0;
	std::vector<float> h; ///< NaN = sin suelo
	bool at(float x, float z, float& y) const;
};
bool buildCourseField(const std::string& dataDir, const std::string& course, CourseField& out);

} // namespace nm
