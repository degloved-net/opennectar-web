#include "gl/pc_texpack.h"
#include "web/pc_web_runtime.h"
#include <emscripten/emscripten.h>
#include <cstring>
#include <cstdint>
#include <set>
#include <string>

static std::set<std::string> ready;
static int uploads, levels;
static size_t uploadedBytes;
static uint32_t firstPixel;
extern "C" {
int pc_netplay_present_null_active() { return 0; }
void pc_netplay_present_note_real() {}
const GLubyte* glGetString(GLenum) { return reinterpret_cast<const GLubyte*>(""); }
void glGetIntegerv(GLenum, GLint* value) { *value = 8192; }
GLenum glGetError() { return GL_NO_ERROR; }
void glTexParameteri(GLenum, GLenum, GLint) {}
void glTexImage2D(GLenum, GLint level, GLint, GLsizei width, GLsizei height, GLint,
                  GLenum, GLenum, const void* data) {
    ++uploads;
    if (level == 0) std::memcpy(&firstPixel, data, sizeof(firstPixel));
}
void glCompressedTexImage2D(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*) {
    ++uploads;
}
EMSCRIPTEN_KEEPALIVE void nectarTextureReady(const char* name) { ready.emplace(name); }
EMSCRIPTEN_KEEPALIVE const char* nectarGetTexturePack() { return pc_texpack_selected_pack().c_str(); }
EMSCRIPTEN_KEEPALIVE void nectarSelectTexturePack(const char* id) { pc_texpack_select_pack(id); }
EMSCRIPTEN_KEEPALIVE int nectarApplyTexturePack() {
    if (pc_texpack_selected_pack().empty()) pc_texpack_request_disable(); else pc_texpack_request_enable();
    pc_texpack_init();
    return pc_texpack_selected_pack().empty() || pc_texpack_enabled();
}
EMSCRIPTEN_KEEPALIVE int testRequestTexture(const char* name) {
    return pc_texpack_try_upload(name, 1, &levels, &uploadedBytes);
}
EMSCRIPTEN_KEEPALIVE int testReadyCount() { return ready.size(); }
EMSCRIPTEN_KEEPALIVE int testProcessTexture() {
    if (ready.empty()) return 0;
    const std::string name = *ready.begin(); ready.erase(name);
    uploads = 0;
    const bool result = pc_texpack_try_upload(name.c_str(), 1, &levels, &uploadedBytes);
    pc_web_texture_consumed(name.c_str());
    return result ? uploads : -1;
}
EMSCRIPTEN_KEEPALIVE int testUploadedLevels() { return levels; }
EMSCRIPTEN_KEEPALIVE int testUploadedBytes() { return uploadedBytes; }
EMSCRIPTEN_KEEPALIVE uint32_t testFirstPixel() { return firstPixel; }
}
bool pc_web_texture_ready(const char* name) {
    return EM_ASM_INT({return +Module.nectarTextureRequest(UTF8ToString($0));}, name);
}
void pc_web_texture_activate(const char* id) {
    EM_ASM({Module.nectarTextureActivate(UTF8ToString($0));}, id);
}
void pc_web_texture_consumed(const char* name) {
    EM_ASM({Module.nectarTextureConsumed(UTF8ToString($0));}, name);
}
int main() { return 0; }
