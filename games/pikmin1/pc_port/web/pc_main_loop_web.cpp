#include "pc_main_loop.h"
#include "web/pc_web_runtime.h"
#include <emscripten/emscripten.h>
#if defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten/threading.h>
#else
#include "web/pc_web_threads.h"
#endif
#include <utility>

namespace {
struct WebMainLoop {
    std::function<bool()> tick;
    std::function<void()> stop;
};

void runFrame(void* raw) {
    auto* loop = static_cast<WebMainLoop*>(raw);
    if (pc_web_is_paused()) {
#if defined(__EMSCRIPTEN_PTHREADS__)
        emscripten_main_thread_process_queued_calls();
#endif
        return;
    }
#if !defined(__EMSCRIPTEN_PTHREADS__)
    pc_web_run_ready_threads();
#endif
    if (loop->tick()) {
        return;
    }
    emscripten_cancel_main_loop();
    auto stop = std::move(loop->stop);
    delete loop;
    stop();
}
}

void pc_main_loop_run(std::function<bool()> tick, std::function<void()> stop) {
    auto* loop = new WebMainLoop { std::move(tick), std::move(stop) };
    EM_ASM({ Module['nectarEngineReady'](); });
    emscripten_set_main_loop_arg(runFrame, loop, 0, 1);
}
