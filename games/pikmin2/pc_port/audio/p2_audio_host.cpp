#include "audio/p2_audio_host.h"

#include "audio/pc_aram.h"
#include "audio/pc_dsp_host.h"
#ifndef BEGIN_SCOPE_EXTERN_C
#define BEGIN_SCOPE_EXTERN_C extern "C" {
#define END_SCOPE_EXTERN_C }
#endif
#include "jaudio/dspinterface.h"
#include "port/audio_sink.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <atomic>
#endif

namespace {
bool sOpened = false;
#ifdef __EMSCRIPTEN__
std::atomic<bool> sMovieAudio {false};
SDL_AudioStream* sMovieStream = nullptr;
#endif
}

void pc_p2_audio_init()
{
	pc_aram_init();
	if (!pc_dsp_host_ready()) {
		pc_dsp_host_init();
	}
	pc_dsp_host_set_pitch_unity(0x1000);
	// Called for every DSP frame (~57/s) from JASAudioThread. Without an output
	// device each attempt reconnects to the sound server and stalls the audio
	// thread, so a missing device is retried every couple of seconds only; the
	// mix keeps running and the retrace pacing falls back to the video clock.
	static Uint32 sNextAttempt = 0;
	if (!sOpened && SDL_TICKS_PASSED(SDL_GetTicks(), sNextAttempt)) {
		sNextAttempt = SDL_GetTicks() + 2000;
		sOpened = PikiAudioSinkTryOpen(32000) != 0;
		if (sOpened) {
			PikiAudioSinkResume();
			std::printf("[PC Port] JAS software DSP output enabled\n");
		}
	}
}

void pc_p2_audio_set_dsp_tables(const u16* resample, const u16* adpcm)
{
	pc_p2_audio_init();
	pc_dsp_host_set_tables(reinterpret_cast<const u32*>(resample),
	                       reinterpret_cast<const u32*>(adpcm));
}

void pc_p2_audio_render(void* channels, s16* planarOutput, u32 frameSamples)
{
	pc_p2_audio_init();
	pc_dsp_host_render_frame_planar(reinterpret_cast<DSPchannel_*>(channels), 64,
	                                planarOutput, frameSamples);
}

void pc_p2_audio_queue(const s16* interleaved, size_t frames)
{
#ifdef __EMSCRIPTEN__
	if (sMovieAudio.load()) return;
#endif
	if (sOpened && interleaved && frames) {
		PikiAudioSinkQueue(interleaved, frames);
	}
	static u32 reportCounter = 0;
	if (std::getenv("PIKMIN_AUDIO_LOG") && ++reportCounter >= 57) {
		reportCounter = 0;
		int peak = 0;
		for (size_t i = 0; i < frames * 2; ++i) {
			peak = std::max(peak, std::abs(static_cast<int>(interleaved[i])));
		}
		std::printf("[PC Audio] voices=%u peak=%d aram=%zu bytes\n",
		            pc_dsp_host_active_voices(), peak, pc_aram_bytes_loaded());
	}
	if (std::getenv("PIKMIN_AUDIO_LOG") && interleaved && frames) {
		static u32 nonzeroCounter = 0;
		int peak2 = 0;
		for (size_t i = 0; i < frames * 2; ++i) {
			peak2 = std::max(peak2, std::abs(static_cast<int>(interleaved[i])));
		}
		if (peak2 > 0 && ++nonzeroCounter % 30 == 1) {
			std::printf("[PC Audio] NONZERO peak=%d voices=%u\n", peak2,
			            pc_dsp_host_active_voices());
		}
	}
	if (const char* wavPath = std::getenv("PIKMIN_AUDIO_WAV")) {
		static FILE* wav = nullptr;
		static size_t wavFrames = 0;
		if (!wav && interleaved) {
			wav = std::fopen(wavPath, "wb");
			if (wav) {
				// Minimal placeholder header; patched on dump end (44 bytes).
				u8 hdr[44] = { 0 };
				std::fwrite(hdr, 1, sizeof(hdr), wav);
			}
		}
		if (wav && interleaved && frames) {
			std::fwrite(interleaved, sizeof(s16), frames * 2, wav);
			wavFrames += frames;
		}
		if (wav && (!interleaved || frames == 0)) {
			const u32 dataBytes = (u32)(wavFrames * 2 * sizeof(s16));
			const u32 riffSize  = 36 + dataBytes;
			const u32 byteRate  = 32000 * 2 * sizeof(s16);
			const u16 blockAlign = 2 * sizeof(s16);
			std::fseek(wav, 0, SEEK_SET);
			std::fwrite("RIFF", 1, 4, wav);
			std::fwrite(&riffSize, 4, 1, wav);
			std::fwrite("WAVEfmt ", 1, 8, wav);
			const u32 fmtSize = 16;
			const u16 audioFmt = 1, channels = 2, bits = 16;
			const u32 sampleRate = 32000;
			std::fwrite(&fmtSize, 4, 1, wav);
			std::fwrite(&audioFmt, 2, 1, wav);
			std::fwrite(&channels, 2, 1, wav);
			std::fwrite(&sampleRate, 4, 1, wav);
			std::fwrite(&byteRate, 4, 1, wav);
			std::fwrite(&blockAlign, 2, 1, wav);
			std::fwrite(&bits, 2, 1, wav);
			std::fwrite("data", 1, 4, wav);
			std::fwrite(&dataBytes, 4, 1, wav);
			std::fclose(wav);
			wav = nullptr;
			std::printf("[PC Audio] WAV dump written: %s (%zu frames)\n", wavPath, wavFrames);
		}
	}
}

#ifdef __EMSCRIPTEN__
bool pc_p2_movie_audio_open(int rate)
{
	pc_p2_movie_audio_close();
	if (!sOpened || rate <= 0) return false;
	sMovieStream = SDL_NewAudioStream(AUDIO_S16SYS, 2, rate, AUDIO_S16SYS, 2, 32000);
	if (!sMovieStream) return false;
	sMovieAudio.store(true);
	PikiAudioSinkClear();
	PikiAudioSinkResume();
	return true;
}

void pc_p2_movie_audio_queue(const s16* pcm, size_t frames)
{
	if (!sMovieStream || !pcm || !frames) return;
	if (PikiAudioSinkQueuedFrames() >= 16000) return;
	if (SDL_AudioStreamPut(sMovieStream, pcm, int(frames * 4)) != 0) return;
	s16 converted[2048];
	while (SDL_AudioStreamAvailable(sMovieStream) > 0) {
		int bytes = SDL_AudioStreamGet(sMovieStream, converted, sizeof(converted));
		if (bytes <= 0) break;
		PikiAudioSinkQueue(converted, size_t(bytes) / 4);
	}
}

void pc_p2_movie_audio_close()
{
	if (!sMovieStream) return;
	SDL_FreeAudioStream(sMovieStream);
	sMovieStream = nullptr;
	PikiAudioSinkClear();
	sMovieAudio.store(false);
	PikiAudioSinkResume();
}
#endif
