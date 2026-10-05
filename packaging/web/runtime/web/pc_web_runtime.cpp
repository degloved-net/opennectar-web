#include "pc_web_runtime.h"

#include "gl/pc_gfx.h"
#include "gl/pc_texpack.h"
#include "mods/pc_hd_model_convert.h"
#include "pc_window.h"
#include "settings/pc_settings.h"

#include <algorithm>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

namespace {
struct WebDisplay {
    int width = 0;
    int height = 0;
    int renderWidth = 0;
    int renderHeight = 0;
    bool paused = false;
    bool relativeMouse = false;
    int appliedWidth = 0;
    int appliedHeight = 0;
    int appliedRenderWidth = -1;
    int appliedRenderHeight = -1;
};

WebDisplay display;
}

extern "C" {
EMSCRIPTEN_KEEPALIVE void nectarTextureReady(const char* name) {
    pc_gfx_texture_ready(name);
}
EMSCRIPTEN_KEEPALIVE void nectarClearInput() {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) return;
    SDL_ResetKeyboard();
    SDL_FlushEvents(SDL_KEYDOWN, SDL_TEXTINPUT);
    SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEWHEEL);
    pc_window_clear_mouse_cursor_delta();
    pc_window_take_mouse_pressed();
    pc_window_discard_button_presses();
}

EMSCRIPTEN_KEEPALIVE void nectarResize(int width, int height) {
    if (width < 1 || height < 1 || width > 4096 || height > 4096) return;
    display.width = width;
    display.height = height;
}

EMSCRIPTEN_KEEPALIVE void nectarSetPaused(int paused) {
    bool nextPaused = paused != 0;
    if (display.paused == nextPaused) {
        return;
    }
    display.paused = nextPaused;
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        nectarClearInput();
        if (nextPaused) {
            display.relativeMouse = SDL_GetRelativeMouseMode() == SDL_TRUE;
            SDL_SetRelativeMouseMode(SDL_FALSE);
        } else if (display.relativeMouse) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
        }
    }
}

EMSCRIPTEN_KEEPALIVE int nectarGetAspectRatioMode() {
    return pc_gfx_get_aspect_ratio_mode();
}

EMSCRIPTEN_KEEPALIVE float nectarGetAspectRatio() {
    return pc_gfx_get_current_aspect_ratio();
}

EMSCRIPTEN_KEEPALIVE void nectarSelectTexturePack(const char* folder) {
    pc_settings_set_texture_pack(folder);
}

EMSCRIPTEN_KEEPALIVE int nectarApplyTexturePack() {
    const char* folder = pc_settings_get_texture_pack();
    pc_texpack_select_pack(folder);
    if (folder[0]) pc_texpack_request_enable();
    else pc_texpack_request_disable();
    pc_gfx_reload_texture_pack();
    return !folder[0] || pc_texpack_enabled();
}

EMSCRIPTEN_KEEPALIVE void nectarFileBegin(int action) {
    pc_settings_web_file_started(action);
}

EMSCRIPTEN_KEEPALIVE void nectarFileProgress(int files, const char* message) {
    pc_texpack_install_progress(files);
    pc_settings_web_notice(false, message);
}

EMSCRIPTEN_KEEPALIVE void nectarFileResult(int action, int result, const char* message) {
    pc_settings_web_file_finished(action, result, message);
}

EMSCRIPTEN_KEEPALIVE void nectarNotice(int error, const char* message) {
    pc_settings_web_notice(error != 0, message);
}

EMSCRIPTEN_KEEPALIVE const char* nectarGetTexturePack() {
    return pc_settings_get_texture_pack();
}

EMSCRIPTEN_KEEPALIVE const char* nectarInstallPack(int kind, const char* path) {
    if (!path || kind < 0 || kind > 5) {
        return "invalid model pack.";
    }
    static char message[256];
    message[0] = '\0';
    if (pc_hd_models_convert_file(path, kind, message, sizeof(message)) > 0) return "";
    return message[0] ? message : "the engine could not install this pack.";
}
}

void pc_web_sync_window(SDL_Window* window, int* width, int* height) {
    if (!window || display.width < 1 || display.height < 1) {
        return;
    }
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    if (windowWidth != display.width || windowHeight != display.height) {
        SDL_SetWindowSize(window, display.width, display.height);
        display.appliedWidth = 0;
    }
    if (display.appliedWidth != display.width || display.appliedHeight != display.height) {
        emscripten_set_canvas_element_size("#canvas", display.width, display.height);
        display.appliedWidth = display.width;
        display.appliedHeight = display.height;
    }
    *width = display.width;
    *height = display.height;
    if (display.renderWidth > 0 && display.renderHeight > 0) {
        int renderWidth = std::min(display.renderWidth, display.width);
        int renderHeight = std::min(display.renderHeight, display.height);
        if (renderWidth != display.appliedRenderWidth || renderHeight != display.appliedRenderHeight) {
            pc_gfx_set_render_resolution(renderWidth, renderHeight);
            display.appliedRenderWidth = renderWidth;
            display.appliedRenderHeight = renderHeight;
        }
    }
}

void pc_web_set_render_size(int width, int height) {
    display.renderWidth = width;
    display.renderHeight = height;
}

void pc_web_set_fullscreen(bool enabled) {
    EM_ASM({ Module['nectarSetFullscreen'](!!$0); }, enabled);
}

bool pc_web_is_fullscreen() {
    return EM_ASM_INT({ return !!document.fullscreenElement; }) != 0;
}

bool pc_web_is_paused() {
    return display.paused;
}

void pc_web_request_file(int action) {
    EM_ASM({ Module['nectarRequestFile']($0); }, action);
}

void pc_web_select_texture_pack(const char* folder) {
    EM_ASM({ Module['nectarChooseTexturePack'](UTF8ToString($0)); }, folder);
}

void pc_web_restart() {
    EM_ASM({ Module['nectarRestart'](); });
}

bool pc_web_texture_ready(const char* name) {
    return MAIN_THREAD_EM_ASM_INT({
        return Module['nectarTextureRequest'] ? +Module['nectarTextureRequest'](UTF8ToString($0)) : 1;
    }, name) != 0;
}

void pc_web_texture_activate(const char* folder) {
    MAIN_THREAD_EM_ASM({ Module['nectarTextureActivate']?.(UTF8ToString($0)); }, folder);
}

void pc_web_texture_consumed(const char* name) {
    MAIN_THREAD_EM_ASM({ Module['nectarTextureConsumed']?.(UTF8ToString($0)); }, name);
}
