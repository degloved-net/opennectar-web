#include "CardUtil.h"
#include "gameflow.h"
#include "Dolphin/dvd.h"
#include "web/pc_web_threads.h"
#include <emscripten/emscripten.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
bool pc_speedrun_active() { return false; }
#if PIKI_P2_HOST
static thread_local bool hostAllocation = false;
bool pc_host_alloc_active() { return hostAllocation; }
void pc_host_alloc_set(bool active) { hostAllocation = active; }
#endif

static OSThread mainThread;
alignas(GameFlow) unsigned char testGameFlow[sizeof(GameFlow)] __asm__("gameflow") = {};
extern CardUtilControl CardControl;
extern CardUtilThread CardThread;
extern "C" {
OSThread* __OSCurrentThread = &mainThread;
void OSPanic(const char* file, int line, const char* format, ...) {
    std::fprintf(stderr, "%s:%d: ", file, line);
    va_list args; va_start(args, format); std::vfprintf(stderr, format, args); va_end(args);
    std::abort();
}
DVDDiskID* DVDGetCurrentDiskID() {
    static DVDDiskID id {}; std::memcpy(id.gameName, "GPIE", 4); std::memcpy(id.company, "01", 2); return &id;
}
void DCStoreRange(void*, u32) {}
void DCFlushRange(void*, u32) {}
}

static void finished() {
    CardUtilIdleWhileBusy();
    assert(CardUtilResultCode() == CARD_RESULT_READY);
}
static void saveRoundTrip() {
    CardUtilMount(0, nullptr); finished();
    CARDStat state {}; std::strcpy(state.fileName, "synthetic-save");
    state.length = 0x26000;
    state.commentAddr = state.iconAddr = state.offsetData = 0xffffffff;
    std::vector<unsigned char> data(state.length, 0);
    CardUtilSave(0, &state, data.data()); finished();
    CARDFileInfo file {}; assert(CARDOpen(0, state.fileName, &file) == 0);
    std::vector<unsigned char> game(0x8000), options(0x2000), actual(state.length);
    for (int day = 1; day <= 3; ++day) {
        for (size_t i = 0; i < game.size(); ++i) game[i] = (i + day) % 251;
        for (size_t i = 0; i < options.size(); ++i) options[i] = (i * day) % 239;
        CardUtilMount(0, nullptr); finished();
        CardUtilWrite(0, file.fileNo, game.data(), 0x6000, game.size()); finished();
        CardUtilUnmount(0); finished();
        CardUtilMount(0, nullptr); finished();
        CardUtilWrite(0, file.fileNo, options.data(), 0x2000, options.size()); finished();
        CardUtilUnmount(0); finished();
        CardUtilOpen(0, file.fileNo, actual.data()); finished();
        assert(std::memcmp(actual.data() + 0x6000, game.data(), game.size()) == 0);
        assert(std::memcmp(actual.data() + 0x2000, options.data(), options.size()) == 0);
    }
    std::puts("single-threaded card save tests passed");
}
int main() {
    assert(EM_ASM_INT({ return HEAPU8.buffer instanceof ArrayBuffer; }));
    CARDInit();
    CardControl.mChannel = -1;
    CardUtilInit(nullptr, 0, 14);
    pc_web_run_ready_threads();
    emscripten_async_call([](void*) {
        saveRoundTrip();
        OSCancelThread(&CardThread.mThread);
    }, nullptr, 0);
}
