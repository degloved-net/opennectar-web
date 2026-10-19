#include "pc_randomizer_catalog.h"

#include <cstring>

namespace {
#define PC_P2_TREASURE_TABLE_BEGIN const PcP2Treasure kTreasures[] = {
#define PC_P2_TREASURE_TABLE_END   };
#define PC_P2_CAVE_TABLE_BEGIN     const PcP2Cave kCaves[] = {
#define PC_P2_CAVE_TABLE_END       };
#define PC_P2_LOCATION_TABLE_BEGIN const PcP2Location kLocations[] = {
#define PC_P2_LOCATION_TABLE_END   };
#include "pc_randomizer_catalog_data.inc"

template <typename T, int N> constexpr int countOf(const T (&)[N]) { return N; }

static_assert(countOf(kTreasures) == 201, "P2 tiene 201 tesoros");
static_assert(countOf(kLocations) == 201, "una ubicación por tesoro");
static_assert(countOf(kCaves) == 14, "14 cuevas de historia");
}

int pc_randomizer_treasure_count(void) { return countOf(kTreasures); }

const PcP2Treasure& pc_randomizer_treasure(int i) { return kTreasures[i]; }

int pc_randomizer_treasure_by_name(const char* name)
{
	if (!name) return -1;
	for (int i = 0; i < countOf(kTreasures); i++) {
		if (std::strcmp(kTreasures[i].name, name) == 0) return i;
	}
	return -1;
}

int pc_randomizer_treasure_by_code(uint16_t code)
{
	for (int i = 0; i < countOf(kTreasures); i++) {
		if (((kTreasures[i].kind << 8) | kTreasures[i].index) == code) return i;
	}
	return -1;
}

int pc_randomizer_cave_count(void) { return countOf(kCaves); }

const PcP2Cave& pc_randomizer_cave(int i) { return kCaves[i]; }

int pc_randomizer_cave_by_id(const char* id)
{
	if (!id) return -1;
	for (int i = 0; i < countOf(kCaves); i++) {
		if (std::strcmp(kCaves[i].id, id) == 0) return i;
	}
	return -1;
}

int pc_randomizer_location_count(void) { return countOf(kLocations); }

const PcP2Location& pc_randomizer_location(int i) { return kLocations[i]; }

int pc_randomizer_cave_by_file(const char* caveinfoFile)
{
	if (!caveinfoFile) return -1;
	for (int i = 0; i < countOf(kCaves); i++) {
		if (std::strcmp(kCaves[i].file, caveinfoFile) == 0) return i;
	}
	return -1;
}

namespace {
const char* const kCourseNames[] = { "tutorial", "forest", "yakushima", "last" };
}

int pc_randomizer_course_by_name(const char* name)
{
	if (!name) return -1;
	for (int i = 0; i < 4; i++) {
		if (std::strcmp(kCourseNames[i], name) == 0) return i;
	}
	return -1;
}

const char* pc_randomizer_course_name(int course) { return (course >= 0 && course < 4) ? kCourseNames[course] : "?"; }
