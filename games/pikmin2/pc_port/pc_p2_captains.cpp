#include "pc_p2_captains.h"

#include <stdlib.h>
#include <ctype.h>

namespace {

struct CharacterInfo {
	const char* name;  // valor de PIKMIN_P2_CAPTAIN1/2
	const char* model; // dentro de pikis.szs
	float scale;
	bool pikmin;
	unsigned char color[3]; // cursor de los Pikmin
	int light;              // 0 luz de Olimar, 1 luz de Louie
};

// Pikmin a la escala de Olimar: así miden más o menos lo mismo que él.
const CharacterInfo kCharacters[PC_P2_CHAR_COUNT] = {
	{ "olimar", "orima_model/orima1.bmd", 1.3f, false, { 255, 255, 255 }, 0 },
	{ "louie", "orima_model/orima3.bmd", 1.5f, false, { 255, 255, 255 }, 1 },
	{ "president", "orima_model/syatyou.bmd", 1.5f, false, { 255, 255, 255 }, 1 },
	{ "red", "piki_model/piki_p2_red.bmd", 1.3f, true, { 255, 60, 40 }, 0 },
	{ "yellow", "piki_model/piki_p2_yellow.bmd", 1.3f, true, { 255, 220, 40 }, 0 },
	{ "blue", "piki_model/piki_p2_blue.bmd", 1.3f, true, { 60, 110, 255 }, 1 },
	{ "purple", "piki_model/piki_p2_black.bmd", 1.3f, true, { 170, 70, 230 }, 0 },
	{ "white", "piki_model/piki_p2_white.bmd", 1.3f, true, { 240, 240, 255 }, 0 },
	{ "bulbmin", "piki_model/piki_kochappy.bmd", 1.3f, true, { 225, 110, 95 }, 0 },
};

bool sameName(const char* a, const char* b)
{
	for (; *a && *b; a++, b++) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
			return false;
		}
	}
	return *a == *b;
}

int sLoaded[2] = { PC_P2_CHAR_OLIMAR, PC_P2_CHAR_LOUIE };
int sChoice[2] = { -1, -1 };

// Solo se comparan, nunca se usan: J3DModel* de cada capitán.
const void* sModels[2][3];

// -1 si la variable no existe o no es un personaje.
int envCharacter(int naviIndex)
{
	const char* value = getenv(naviIndex == 0 ? "PIKMIN_P2_CAPTAIN1" : "PIKMIN_P2_CAPTAIN2");
	if (!value) {
		return -1;
	}
	for (int i = 0; i < PC_P2_CHAR_COUNT; i++) {
		if (sameName(value, kCharacters[i].name)) {
			return i;
		}
	}
	return -1;
}

bool validCharacter(int character) { return character >= 0 && character < PC_P2_CHAR_COUNT; }

} // namespace

void pc_p2_captain_set_choice(int naviIndex, int character)
{
	sChoice[naviIndex == 0 ? 0 : 1] = validCharacter(character) ? character : -1;
}

int pc_p2_captain_choice(int naviIndex) { return sChoice[naviIndex == 0 ? 0 : 1]; }

int pc_p2_captain_character(int naviIndex, bool debtPaid)
{
	int character = envCharacter(naviIndex);
	if (character < 0) {
		character = sChoice[naviIndex == 0 ? 0 : 1];
	}
	if (character >= 0) {
		return character;
	}
	if (naviIndex == 0) {
		return PC_P2_CHAR_OLIMAR;
	}
	return debtPaid ? PC_P2_CHAR_PRESIDENT : PC_P2_CHAR_LOUIE;
}

int pc_p2_captain_loaded_character(int naviIndex) { return sLoaded[naviIndex == 0 ? 0 : 1]; }

void pc_p2_captain_set_loaded_character(int naviIndex, int character)
{
	if (validCharacter(character)) {
		sLoaded[naviIndex == 0 ? 0 : 1] = character;
	}
}

const char* pc_p2_character_model_path(int character)
{
	return kCharacters[validCharacter(character) ? character : PC_P2_CHAR_OLIMAR].model;
}

float pc_p2_character_scale(int character) { return kCharacters[validCharacter(character) ? character : PC_P2_CHAR_OLIMAR].scale; }

void pc_p2_character_color(int character, unsigned char rgb[3])
{
	const CharacterInfo& info = kCharacters[validCharacter(character) ? character : PC_P2_CHAR_OLIMAR];
	for (int c = 0; c < 3; c++) {
		rgb[c] = info.color[c];
	}
}

bool pc_p2_character_is_pikmin(int character) { return validCharacter(character) && kCharacters[character].pikmin; }

// Capitán 2 con el mismo personaje que el 1. Dos Pikmin del mismo color se
// dejan con su color natural.
static bool repeatedCaptain2(int naviIndex)
{
	return naviIndex == 1 && sLoaded[0] == sLoaded[1] && !pc_p2_character_is_pikmin(sLoaded[1]);
}

bool pc_p2_captain_tint(int naviIndex, float rgb[3])
{
	if (!repeatedCaptain2(naviIndex)) {
		return false;
	}
	// Azul, salvo que el personaje ya sea azul: entonces rojo.
	const bool blueAlready = sLoaded[1] == PC_P2_CHAR_LOUIE || sLoaded[1] == PC_P2_CHAR_BLUE;
	rgb[0]                 = (blueAlready ? 255 : 90) / 255.0f;
	rgb[1]                 = (blueAlready ? 120 : 150) / 255.0f;
	rgb[2]                 = (blueAlready ? 110 : 255) / 255.0f;
	return true;
}

void pc_p2_captain_set_models(int naviIndex, const void* body, const void* cursor, const void* marker)
{
	const void** models = sModels[naviIndex == 0 ? 0 : 1];
	models[0]           = body;
	models[1]           = cursor;
	models[2]           = marker;
}

void pc_p2_captain_clear_models(void)
{
	for (int i = 0; i < 2; i++) {
		pc_p2_captain_set_models(i, nullptr, nullptr, nullptr);
	}
}

int pc_p2_captain_light_type(int naviIndex)
{
	int light = kCharacters[pc_p2_captain_loaded_character(naviIndex)].light;
	if (repeatedCaptain2(naviIndex)) {
		light = 1 - light;
	}
	return light;
}

int pc_p2_captain_cursor_anim(int naviIndex)
{
	const int character = pc_p2_captain_loaded_character(naviIndex);
	return character == PC_P2_CHAR_LOUIE || character == PC_P2_CHAR_PRESIDENT ? 1 : 0;
}

void pc_p2_captain_cursor_colour(int naviIndex, unsigned char rgb[3])
{
	float tint[3];
	const int character = pc_p2_captain_loaded_character(naviIndex);
	if (pc_p2_captain_tint(naviIndex, tint)) {
		for (int c = 0; c < 3; c++) {
			rgb[c] = (unsigned char)(tint[c] * 255.0f);
		}
	} else if (pc_p2_character_is_pikmin(character)) {
		for (int c = 0; c < 3; c++) {
			rgb[c] = (unsigned char)kCharacters[character].color[c];
		}
	} else if (pc_p2_captain_cursor_anim(naviIndex) == 1) {
		rgb[0] = 60, rgb[1] = 130, rgb[2] = 255; // Louie y el presidente
	} else {
		rgb[0] = 235, rgb[1] = 70, rgb[2] = 235; // Olimar
	}
}

bool pc_p2_captain_tint_for_model(const void* j3dModel, float rgb[3])
{
	if (!j3dModel) {
		return false;
	}
	for (int navi = 0; navi < 2; navi++) {
		for (int i = 0; i < 3; i++) {
			if (sModels[navi][i] != j3dModel) {
				continue;
			}
			if (i == 0) {
				return pc_p2_captain_tint(navi, rgb);
			}
			// Cursor: el del capitán teñido lleva su tinte y el de un Pikmin su color,
			// en gris + color para que no se mezcle con el magenta o el azul de base.
			const int character = sLoaded[navi];
			if (!pc_p2_captain_tint(navi, rgb)) {
				if (!pc_p2_character_is_pikmin(character)) {
					return false;
				}
				for (int c = 0; c < 3; c++) {
					rgb[c] = kCharacters[character].color[c] / 255.0f;
				}
			}
			// El cursor es oscuro en gris: se aclara para que el color se vea.
			for (int c = 0; c < 3; c++) {
				rgb[c] = -(rgb[c] * 2.2f + 0.001f);
			}
			return true;
		}
	}
	return false;
}

int pc_p2_captain_down_title(int naviIndex)
{
	switch (pc_p2_captain_loaded_character(naviIndex)) {
	case PC_P2_CHAR_OLIMAR: return 1;
	case PC_P2_CHAR_LOUIE: return 2;
	case PC_P2_CHAR_PRESIDENT: return 3;
	default: return 4;
	}
}
