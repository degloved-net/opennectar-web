// Randomizer P2: contexto de lectura de generadores y volcado en partida
// (plan §5.1). Con PIKI_RND_AUDIT=1 imprime lo que el juego lee de verdad
// (archivo, generador, enemigo, código de tesoro y listas de caveinfo) para
// contrastarlo con tools/randomizer_audit.py. El contexto de lectura lo usa
// también la sustitución (pc_randomizer_apply.cpp).

#include "pc_randomizer_audit.h"
#include "pc_randomizer_catalog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Game/Cave/Info.h"

namespace {
const char* sGenFile = nullptr; // archivo que está leyendo un GeneratorMgr
char sGenFileBuf[256];
int sGenCourse = -1;
int sGenIndex  = -1;
int sGenSerial = 0;

/// "user/Abe/map/tutorial/nonloop/0-1.txt" -> "nonloop/0-1.txt", como en el catálogo.
const char* shortGenPath(const char* path)
{
	const char* base = std::strrchr(path, '/');
	if (!base) return path;
	for (const char* sub : { "/nonloop/", "/loop/", "/day/" }) {
		const char* p = std::strstr(path, sub);
		if (p) return p + 1;
	}
	return base + 1;
}

/// Curso a partir de ".../map/<curso>/...".
int courseOfPath(const char* path)
{
	const char* p = std::strstr(path, "/map/");
	if (!p) return -1;
	p += 5;
	char name[32];
	int n = 0;
	while (p[n] && p[n] != '/' && n < 31) {
		name[n] = p[n];
		n++;
	}
	name[n] = '\0';
	return pc_randomizer_course_by_name(name);
}

const char* treasureName(int code)
{
	const int t = pc_randomizer_treasure_by_code(uint16_t(code));
	return t >= 0 ? pc_randomizer_treasure(t).name : "?";
}
}

bool pc_p2_rnd_audit_on(void)
{
	static const int on = [] {
		const char* env = std::getenv("PIKI_RND_AUDIT");
		return (env && *env && std::strcmp(env, "0") != 0) ? 1 : 0;
	}();
	return on != 0;
}

extern "C" void pc_p2_rnd_gen_file(const char* path)
{
	sGenIndex = -1;
	if (!path) {
		sGenFile   = nullptr;
		sGenCourse = -1;
		return;
	}
	sGenSerial++;
	std::snprintf(sGenFileBuf, sizeof(sGenFileBuf), "%s", shortGenPath(path));
	sGenFile   = sGenFileBuf;
	sGenCourse = courseOfPath(path);
	if (pc_p2_rnd_audit_on()) std::printf("rnd-audit: gen file %s\n", path);
}

extern "C" void pc_p2_rnd_gen_index(int index) { sGenIndex = index; }

int pc_p2_rnd_gen_serial(void) { return sGenSerial; }

const char* pc_p2_rnd_gen_context(int* course, int* index)
{
	if (course) *course = sGenCourse;
	if (index) *index = sGenIndex;
	return sGenFile;
}

void pc_p2_rnd_audit_gen(const char* what, int enemyId, int code, int newEnemyId, int newCode)
{
	if (!pc_p2_rnd_audit_on() || !sGenFile) return;
	if ((code >> 8) != 3 && (code >> 8) != 4) return;
	std::printf("rnd-audit: gen %s #%d %s %d code 0x%03X %s", sGenFile, sGenIndex, what, enemyId, code, treasureName(code));
	if (newCode != code || newEnemyId != enemyId) {
		std::printf(" -> %s %d code 0x%03X %s", what, newEnemyId, newCode, treasureName(newCode));
	}
	std::printf("\n");
}

void pc_p2_rnd_audit_cave(const char* file, Game::Cave::CaveInfo* info, int floorIndex)
{
	if (!pc_p2_rnd_audit_on() || !info) return;
	using namespace Game::Cave;
	std::printf("rnd-audit: cave %s floor %d (of %d)\n", file ? file : "?", floorIndex + 1, info->getFloorMax());
	FloorInfo* floor = info->getFloorInfo(floorIndex);
	if (!floor) return;
	int i = 0;
	FOREACH_NODE(TekiInfo, floor->mTekiInfo.mChild, teki)
	{
		const int code = teki->mOtakaraItemCode.mValue;
		std::printf("rnd-audit:   teki #%d enemy %d weight %d type %d drop %d code 0x%03X %s\n", i, int(teki->mEnemyID),
		            teki->mWeight, int(teki->mType), int(teki->mDropMode), code, code ? treasureName(code) : "-");
		i++;
	}
	i = 0;
	FOREACH_NODE(ItemInfo, floor->mItemInfo.mChild, item)
	{
		// mCaveID = PelletMgr::encode(tipo, índice) = tipo << 24 | índice.
		const int code = ((item->mCaveID >> 24) & 0xFF) << 8 | (item->mCaveID & 0xFF);
		std::printf("rnd-audit:   item #%d weight %d code 0x%03X %s\n", i, item->mWeight, code, treasureName(code));
		i++;
	}
	i = 0;
	FOREACH_NODE(CapInfo, floor->mCapInfo.mChild, cap)
	{
		TekiInfo* teki = cap->getTekiInfo();
		if (teki) {
			const int code = teki->mOtakaraItemCode.mValue;
			std::printf("rnd-audit:   cap #%d enemy %d weight %d type %d drop %d code 0x%03X %s\n", i, int(teki->mEnemyID),
			            teki->mWeight, int(teki->mType), int(teki->mDropMode), code, code ? treasureName(code) : "-");
		}
		i++;
	}
}
