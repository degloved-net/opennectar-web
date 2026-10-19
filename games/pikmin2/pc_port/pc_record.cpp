#include "pc_record.h"

#include <SDL.h>
#include <cstdio>

namespace {

bool sHudHidden = false;
bool sCtrlPrev[10] = {};

} // namespace

extern "C" {

int pc_record_ctrl_pressed(int digit)
{
	if (digit < 1 || digit > 9) return 0;
	int numKeys       = 0;
	const Uint8* keys = SDL_GetKeyboardState(&numKeys);
	// SDL_SCANCODE_1..9 son consecutivos.
	const int sc      = SDL_SCANCODE_1 + (digit - 1);
	const bool ctrl   = (SDL_GetModState() & KMOD_CTRL) != 0;
	const bool down   = ctrl && keys != nullptr && sc < numKeys && keys[sc] != 0;
	const bool edge   = down && !sCtrlPrev[digit];
	sCtrlPrev[digit]  = down;
	return edge ? 1 : 0;
}

int pc_record_hud_hidden(void) { return sHudHidden ? 1 : 0; }

void pc_record_toggle_hud(void)
{
	sHudHidden = !sHudHidden;
	fprintf(stderr, "[RECORD] HUD %s\n", sHudHidden ? "hidden" : "shown");
	fflush(stderr);
}

} // extern "C"
