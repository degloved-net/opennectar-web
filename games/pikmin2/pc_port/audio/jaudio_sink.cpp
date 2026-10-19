// SDL2 adapter for the NextOS JAudio host renderer. Native S16 stereo PCM.
#include "audio/pc_audio_drops.h"
#include "port/audio_sink.h"
#include "audio/pc_ast_stream.h"

extern "C" void pc_thp_mix(int16_t* out, size_t frames, int rate);
#include <SDL.h>
#include <algorithm>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <type_traits>
namespace {
template<class Function> auto onBrowserAudioThread(Function function) -> decltype(function())
{
    using Result = decltype(function());
    using Storage = std::conditional_t<std::is_void_v<Result>, bool, Result>;
    struct Call { Function function; Storage result {}; } call {function};
    emscripten_proxy_sync(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(),
        [](void* raw) {
            auto& call = *static_cast<Call*>(raw);
            if constexpr (std::is_void_v<Result>) call.function();
            else call.result = call.function();
        }, &call);
    if constexpr (!std::is_void_v<Result>) return call.result;
}
}
#endif

namespace {
SDL_AudioDeviceID device = 0;
uint64_t submitted = 0;
int outRate = 0;
bool ownsAudio = false;
// The device is fed from the audio thread (Queue) and queried, reopened and
// closed from the main thread. Every entry point holds this lock so a close
// or SDL shutdown never lands in the middle of SDL_QueueAudio. Recursive:
// the entry points call each other (TryOpen -> Close, GetProgress -> Queued).
std::recursive_mutex sSinkMutex;
using SinkLock = std::lock_guard<std::recursive_mutex>;
}
namespace {
// Bringing the audio subsystem up connects SDL's client to the system daemon.
// Retries must not repeat that: only the device open is retried, so keep the
// subsystem alive between attempts and tear it down once, at shutdown.
bool ensureSubsystem()
{
    if (ownsAudio) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    ownsAudio = true;
    return true;
}
}
extern "C" {
void PikiAudioSinkClose()
{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkClose(); });
#endif

    SinkLock lock(sSinkMutex);
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
    submitted = 0;
}
void PikiAudioSinkShutdown()
{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkShutdown(); });
#endif

    SinkLock lock(sSinkMutex);
    PikiAudioSinkClose();
    if (ownsAudio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ownsAudio = false;
}
int PikiAudioSinkTryOpen(int rate)
{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkTryOpen(rate); });
#endif

    SinkLock lock(sSinkMutex);
    PikiAudioSinkClose();
    if (rate <= 0 || !ensureSubsystem()) return 0;
    SDL_AudioSpec spec {};
    spec.freq = rate;
    spec.format = AUDIO_S16SYS;
    spec.channels = 2;
    spec.samples = 1024;
    device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (!device) { PikiAudioSinkClose(); return 0; }
    outRate = rate;
    std::fprintf(stderr, "[jaudio] NextOS DSP, SDL2 S16 stereo %d Hz\n", rate);
    return 1;
}
int PikiAudioSinkOpen(int rate) { return PikiAudioSinkTryOpen(rate); }
int PikiAudioSinkQueue(const int16_t* pcm, size_t frames)
{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkQueue(pcm, frames); });
#endif

    SinkLock lock(sSinkMutex);
    if (!device || !pcm || !frames || frames > std::numeric_limits<Uint32>::max() / 4) return 0;
    // Musica en stream (AST): se suma sobre la mezcla del DSP.
    // malloc, no STL: el new/delete global del port va al heap del juego.
    static int16_t* mixed = nullptr;
    static size_t mixedFrames = 0;
    if (frames > mixedFrames) {
        int16_t* grown = static_cast<int16_t*>(std::realloc(mixed, frames * 4));
        if (!grown) return 0;
        mixed = grown;
        mixedFrames = frames;
    }
    std::memcpy(mixed, pcm, frames * 4);
    pc_ast_mix(mixed, frames, outRate);
    pc_thp_mix(mixed, frames, outRate); // vídeo sin dispositivo propio (Android)
    if (pc_audio_levels_enabled()) {
        double sum = 0.0;
        int peak = 0;
        unsigned clipped = 0;
        for (size_t i = 0; i < frames * 2; ++i) {
            const int v = mixed[i];
            sum += 0.5 * double(v) * v;
            peak = std::max(peak, std::abs(v));
            clipped += (v >= 32767 || v <= -32768) ? 1u : 0u;
        }
        pc_audio_level_block(PCAL_Final, sum, peak, static_cast<unsigned>(frames), clipped);
    }
    if (SDL_QueueAudio(device, mixed, static_cast<Uint32>(frames * 4)) != 0) return 0;
    submitted += frames;
    return 1;
}
int PikiAudioSinkResume() {
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkResume(); });
#endif
    SinkLock lock(sSinkMutex);
    if (!device) return 0;
    SDL_PauseAudioDevice(device, 0);
    return 1;
}
void PikiAudioSinkPause() {
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkPause(); });
#endif
    SinkLock lock(sSinkMutex);
    if (device) SDL_PauseAudioDevice(device, 1);
}
int PikiAudioSinkFlush() { SinkLock lock(sSinkMutex); return device != 0; }

uint64_t PikiAudioSinkSubmittedFrames() { SinkLock lock(sSinkMutex); return submitted; }
uint64_t PikiAudioSinkQueuedFrames() {
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkQueuedFrames(); });
#endif
    SinkLock lock(sSinkMutex);
    return device ? SDL_GetQueuedAudioSize(device) / 4 : 0;
}
uint64_t PikiAudioSinkPlayedFrames() {
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkPlayedFrames(); });
#endif
    SinkLock lock(sSinkMutex);
    const auto queued = PikiAudioSinkQueuedFrames();
    return queued < submitted ? submitted - queued : 0;
}
int PikiAudioSinkDrained() { return PikiAudioSinkQueuedFrames() == 0; }
int PikiAudioSinkGetProgress(uint64_t* total, uint64_t* queued)
{
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) return onBrowserAudioThread([=] { return PikiAudioSinkGetProgress(total, queued); });
#endif

    SinkLock lock(sSinkMutex);
    if (!device) return 0;
    if (total) *total = submitted;
    if (queued) *queued = PikiAudioSinkQueuedFrames();
    return 1;
}
}
