// Randomizer P2: atajos de prueba (PIKI_RND_DEBUG=1, solo en historia y fuera
// del Speedrun). Para comprobar en minutos lo que en una partida normal lleva
// horas. Se ejecutan dentro del exec de los estados de juego (superficie y
// cueva), como los atajos del Speedrun.
//
//   F5  20 Pikmin con flor de cada color (rojo, azul, amarillo, morado, blanco)
//   F6  el tesoro u objeto del Kit más cercano entra en la nave o la cápsula
//       (con su escena de entrega)
//   F7  superficie: al lado de la siguiente entrada de cueva de la zona
//   F8  cueva: al lado del agujero (o del géiser en el último subnivel)
// Al llegar a una entrada, A abre su menú como en el juego.

#include "pc_randomizer.h"
#include "pc_randomizer_audit.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Dolphin/rand.h"
#include "Game/Entities/ItemBigFountain.h"
#include "Game/Entities/ItemCave.h"
#include "Game/Entities/ItemHole.h"
#include "Game/Entities/ItemOnyon.h"
#include "Game/GameSystem.h"
#include "Game/MapMgr.h"
#include "Game/Navi.h"
#include "Game/Piki.h"
#include "Game/PikiMgr.h"
#include "Game/gamePlayData.h"
#include "Game/pelletMgr.h"
#include "Game/Interaction.h"
#include "PikiAI.h"
#include "pc_speedrun.h"

using namespace Game;

namespace {
char sStatus[96] = "";
Uint32 sStatusAt = 0;
bool sKeyWas[4]  = { false, false, false, false };
int sCaveCycle   = 0;

void status(const char* fmt, const char* arg = "")
{
	std::snprintf(sStatus, sizeof(sStatus), fmt, arg);
	sStatusAt = SDL_GetTicks();
	std::printf("rnd-debug: %s\n", sStatus);
}

/// Flanco de bajada de F5..F8 (índice 0..3).
bool pressed(int i)
{
	static const SDL_Scancode kKeys[4] = { SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8 };
	const Uint8* keys = SDL_GetKeyboardState(nullptr);
	const bool down   = keys && keys[kKeys[i]];
	const bool edge   = down && !sKeyWas[i];
	sKeyWas[i]        = down;
	return edge;
}

Navi* activeNavi() { return naviMgr ? naviMgr->getActiveNavi() : nullptr; }

/// Lleva al capitán y a sus Pikmin al lado de `pos` (como el salto del Speedrun).
void moveTo(Vector3f pos)
{
	Navi* navi = activeNavi();
	if (!navi) return;
	pos.x += 40.0f;
	if (mapMgr) pos.y = mapMgr->getMinY(pos);
	navi->setPosition(pos, false);
	if (pikiMgr) pikiMgr->moveAllPikmins(pos, 50.0f, nullptr);
}

void spawnPikmin()
{
	Navi* navi = activeNavi();
	if (!navi || !pikiMgr) return;
	static const int kColors[5] = { Red, Blue, Yellow, Purple, White };
	int born = 0;
	for (int c : kColors) {
		playData->setMeetPikmin(c);
		for (int i = 0; i < 20; i++) {
			Piki* piki = pikiMgr->birth();
			if (!piki) break;
			piki->init(nullptr);
			piki->changeShape(c);
			piki->changeHappa(Flower);
			Vector3f pos = navi->getPosition();
			const f32 angle = TAU * randFloat();
			const f32 dist  = 20.0f + 40.0f * randFloat();
			pos.x += dist * sinf(angle);
			pos.z += dist * cosf(angle);
			if (mapMgr) pos.y = mapMgr->getMinY(pos);
			piki->setPosition(pos, false);
			piki->mNavi = navi;
			PikiAI::ActFormationInitArg arg(navi);
			piki->mBrain->start(PikiAI::ACT_Formation, &arg);
			born++;
		}
	}
	char n[16];
	std::snprintf(n, sizeof(n), "%d", born);
	status(born ? "F5: %s Pikmin born" : "F5: %s Pikmin born (already 100 on the field)", n);
}

void collectNearest(bool inCave)
{
	Navi* navi = activeNavi();
	Onyon* goal = ItemOnyon::mgr ? (inCave ? ItemOnyon::mgr->mPod : ItemOnyon::mgr->mUfo) : nullptr;
	if (!navi || !goal) {
		status("F6: no ship or pod here");
		return;
	}
	Pellet* best   = nullptr;
	f32 bestDist   = 0.0f;
	PelletIterator it;
	CI_LOOP(it)
	{
		Pellet* p = *it;
		if (!p->isAlive() || p->mCaptureMatrix) continue;
		const int kind = p->getKind();
		if (kind != PelletType::Treasure && kind != PelletType::Upgrade) continue;
		Vector3f pos      = p->getPosition();
		Vector3f naviPos  = navi->getPosition();
		const f32 d       = pos.sqrDistance(naviPos);
		if (!best || d < bestDist) {
			best     = p;
			bestDist = d;
		}
	}
	if (!best) {
		status("F6: no loose treasure on this map");
		return;
	}
	InteractSuck suck(goal);
	best->stimulate(suck);
	status("F6: %s goes to the ship/pod", best->mConfig ? best->mConfig->mParams.mName.mData : "?");
}

void nextCave()
{
	if (!ItemCave::mgr) {
		status("F7: no cave entrances here");
		return;
	}
	ItemCave::Item* list[16];
	int n = 0;
	Iterator<BaseItem> it(ItemCave::mgr);
	CI_LOOP(it)
	{
		ItemCave::Item* cave = static_cast<ItemCave::Item*>(*it);
		if (cave->isAlive() && n < 16) list[n++] = cave;
	}
	if (n == 0) {
		status("F7: no cave entrances here");
		return;
	}
	ItemCave::Item* cave = list[sCaveCycle++ % n];
	moveTo(cave->getPosition());
	char id[8];
	const u32 cid = cave->mCaveID.getID();
	std::snprintf(id, sizeof(id), "%c%c%c%c", char(cid >> 24), char(cid >> 16), char(cid >> 8), char(cid));
	status("F7: next to cave %s (press A to enter)", id);
}

void nextFloor()
{
	if (ItemHole::mgr) {
		Iterator<BaseItem> it(ItemHole::mgr);
		CI_LOOP(it)
		{
			ItemHole::Item* hole = static_cast<ItemHole::Item*>(*it);
			if (hole->isAlive()) {
				moveTo(hole->getPosition());
				status("F8: next to the hole (press A)%s", hole->mBarrel ? " - covered by a rock" : "");
				return;
			}
		}
	}
	if (ItemBigFountain::mgr) {
		Iterator<BaseItem> it(ItemBigFountain::mgr);
		CI_LOOP(it)
		{
			// Como Navi::checkBigFountain: sin isAlive (el géiser no lo está
			// hasta que se descubre).
			BaseItem* geyser = *it;
			moveTo(geyser->getPosition());
			status("F8: next to the geyser (press A)");
			return;
		}
	}
	status("F8: no hole or geyser yet");
}
}

bool pc_p2_rnd_debug_on(void)
{
	static const int on = [] {
		const char* env = std::getenv("PIKI_RND_DEBUG");
		return (env && *env && std::strcmp(env, "0") != 0) ? 1 : 0;
	}();
	return on != 0 && gameSystem && gameSystem->isStoryMode() && !pc_speedrun_active();
}

extern "C" void pc_p2_rnd_debug_exec(int inCave)
{
	if (!pc_p2_rnd_debug_on() || !playData) return;
	const bool f5 = pressed(0), f6 = pressed(1), f7 = pressed(2), f8 = pressed(3);
	if (f5) spawnPikmin();
	if (f6) collectNearest(inCave != 0);
	if (f7 && !inCave) nextCave();
	if (f8 && inCave) nextFloor();
}

extern "C" const char* pc_p2_rnd_debug_status(void)
{
	if (!pc_p2_rnd_debug_on()) return nullptr;
	if (sStatus[0] && SDL_GetTicks() - sStatusAt < 4000) return sStatus;
	return "Test keys: F5 Pikmin  F6 collect nearest treasure  F7 next cave  F8 next floor";
}
