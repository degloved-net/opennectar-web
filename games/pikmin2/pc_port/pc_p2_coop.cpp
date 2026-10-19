#include "pc_p2_coop.h"
#include "Game/GameSystem.h"
#include "Game/gamePlayData.h"
#include "settings/pc_settings.h"

namespace {
bool sCoopPending   = false;
int sCoopMenuPlayer = 0;
bool sCoopMovieFull = false;
bool sCoopSplit     = false;
float sCoopBlend    = 1.0f;
int sCoopP1Side     = 0;
} // namespace

extern "C" void pc_p2_coop_set_pending(int twoPlayers) { sCoopPending = twoPlayers != 0; }

extern "C" int pc_p2_coop_pending(void) { return sCoopPending ? 1 : 0; }

extern "C" int pc_p2_coop_session(void)
{
	return sCoopPending && Game::gameSystem && Game::gameSystem->isStoryMode() ? 1 : 0;
}

// El tutorial se juega como en el original, con un solo capitán y el cambio
// con Y; el co-op empieza con la cinemática de Louie (x05_louiestart, el
// primer cambio a él), que pone DEMO_First_Use_Louie. Antes, P2 espera.
extern "C" int pc_p2_coop_active(void)
{
	return pc_p2_coop_session() && Game::playData && Game::playData->isDemoFlag(Game::DEMO_First_Use_Louie) ? 1 : 0;
}

extern "C" void pc_p2_coop_set_menu_player(int player) { sCoopMenuPlayer = player == 1 ? 1 : 0; }

extern "C" int pc_p2_coop_menu_player(void) { return sCoopMenuPlayer; }

extern "C" void pc_p2_coop_set_movie_full(int on) { sCoopMovieFull = on != 0; }

extern "C" int pc_p2_coop_movie_full(void) { return sCoopMovieFull ? 1 : 0; }

extern "C" void pc_p2_coop_set_split(int on) { sCoopSplit = on != 0; }

extern "C" int pc_p2_coop_split(void) { return sCoopSplit ? 1 : 0; }

extern "C" int pc_p2_coop_vertical(void) { return pc_p2_coop_active() && pc_settings_get_coop_split() == 0 ? 1 : 0; }

extern "C" int pc_p2_coop_merge_on(void)
{
	return pc_p2_coop_active() && sCoopSplit && !sCoopMovieFull && pc_settings_get_coop_merge_camera() ? 1 : 0;
}

extern "C" void pc_p2_coop_set_split_blend(float blend) { sCoopBlend = blend < 0.0f ? 0.0f : (blend > 1.0f ? 1.0f : blend); }

extern "C" float pc_p2_coop_split_blend(void) { return pc_p2_coop_merge_on() ? sCoopBlend : 1.0f; }

extern "C" void pc_p2_coop_set_p1_side(int side) { sCoopP1Side = side == 1 ? 1 : 0; }

extern "C" int pc_p2_coop_view_side(int view)
{
	const int p1 = pc_p2_coop_merge_on() ? sCoopP1Side : 0;
	return view == 0 ? p1 : 1 - p1;
}
