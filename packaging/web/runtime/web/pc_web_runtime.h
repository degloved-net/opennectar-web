#ifndef PC_WEB_RUNTIME_H
#define PC_WEB_RUNTIME_H

#include <SDL2/SDL.h>

void pc_web_sync_window(SDL_Window* window, int* width, int* height);
void pc_web_set_render_size(int width, int height);
void pc_web_set_fullscreen(bool enabled);
bool pc_web_is_fullscreen();
bool pc_web_is_paused();
void pc_web_request_file(int action);
void pc_web_select_texture_pack(const char* folder);
void pc_web_restart();
bool pc_web_texture_ready(const char* name);
void pc_web_texture_activate(const char* folder);
void pc_web_texture_consumed(const char* name);

#endif
