#include "audio/pc_audio_drops.h"

#include <atomic>
#include <cmath>
#include <mutex>
#include <cstdio>
#include <cstdlib>

namespace {

bool enabled()
{
	static const bool on = [] {
		const char* v = std::getenv("PIKMIN_AUDIO_DROPS");
		return v && v[0] && v[0] != '0';
	}();
	return on;
}

const char* const kNames[PCAD_Count] = {
	"se-pedidos",      "se-cancelados", "se-prioridad",   "se-sin-objeto", "notas",          "sin-banco",
	"sin-instrumento", "sin-region",    "sin-banco-ondas", "sin-onda",     "sin-info-onda",  "onda-no-cargada",
	"sin-JASChannel",  "sin-canal-DSP", "carga-aw-fallida", "lectura-fuera-ARAM", "voces-iniciadas",
	"voces-cortas<40ms", "voces-inaudibles", "corta:fin-muestra", "corta:juego-la-para", "corta:canal-apagado",
	"inaudible:volumen-del-juego", "inaudible:DSP",
};

std::atomic<u32> sCounts[PCAD_Count];
std::atomic<u32> sTotals[PCAD_Count];

// Ondas que han fallado (motivo << 16 | onda), sin repetir.
constexpr int kMaxWaves = 48;
std::atomic<u32> sWaves[kMaxWaves];
std::atomic<int> sWaveCount { 0 };

} // namespace

void pc_audio_drop(int reason)
{
	if (!enabled() || reason < 0 || reason >= PCAD_Count) return;
	sCounts[reason]++;
	sTotals[reason]++;
}

void pc_audio_drop_wave(int reason, u32 waveId)
{
	if (!enabled()) return;
	pc_audio_drop(reason);
	const u32 key = (static_cast<u32>(reason) << 16) | (waveId & 0xFFFF);
	const int n   = sWaveCount.load();
	for (int i = 0; i < n && i < kMaxWaves; ++i) {
		if (sWaves[i].load() == key) return;
	}
	const int slot = sWaveCount.fetch_add(1);
	if (slot < kMaxWaves) sWaves[slot] = key;
}

namespace {
struct LevelStats {
	double sumSquares = 0.0;
	int peak          = 0;
	unsigned samples  = 0;
	unsigned clipped  = 0;
	unsigned blocks   = 0;
	int maxGain       = 0;
};
std::mutex sLevelMutex;
LevelStats sLevels[PCAL_Count];
const char* const kLevelNames[PCAL_Count] = { "efectos3D", "musica/2D", "salidaDSP", "stream", "final" };

double toDb(double amplitude) { return amplitude > 0.0 ? 20.0 * std::log10(amplitude / 32768.0) : -99.0; }

void reportLevels()
{
	std::lock_guard<std::mutex> lock(sLevelMutex);
	std::fprintf(stderr, "[audio-levels] 5s (dBFS):");
	for (int i = 0; i < PCAL_Count; ++i) {
		LevelStats& l = sLevels[i];
		if (l.samples == 0) {
			std::fprintf(stderr, " %s=silencio", kLevelNames[i]);
		} else {
			std::fprintf(stderr, " %s rms=%.1f pico=%.1f", kLevelNames[i], toDb(std::sqrt(l.sumSquares / l.samples)), toDb(l.peak));
			if (i <= PCAL_Voice2D) {
				std::fprintf(stderr, " voces~%.1f gananciaMax=%.2f", l.blocks / (285.0 * 7.0), l.maxGain / 32768.0);
			}
			if (l.clipped) std::fprintf(stderr, " SATURA=%u", l.clipped);
		}
		std::fprintf(stderr, ";");
		l = LevelStats();
	}
	std::fprintf(stderr, "\n");
}
} // namespace

bool pc_audio_levels_enabled() { return enabled(); }

void pc_audio_level_block(int source, double sumSquares, int peak, unsigned samples, unsigned clipped)
{
	if (!enabled() || source < 0 || source >= PCAL_Count || samples == 0) return;
	std::lock_guard<std::mutex> lock(sLevelMutex);
	LevelStats& l = sLevels[source];
	l.sumSquares += sumSquares;
	if (peak > l.peak) l.peak = peak;
	l.samples += samples;
	l.clipped += clipped;
	l.blocks++;
}

void pc_audio_level_gain(int source, int gainQ15)
{
	if (!enabled() || source < 0 || source >= PCAL_Count) return;
	std::lock_guard<std::mutex> lock(sLevelMutex);
	if (gainQ15 > sLevels[source].maxGain) sLevels[source].maxGain = gainQ15;
}

void pc_audio_drop_report()
{
	if (!enabled()) return;
	static u32 frames = 0;
	if (++frames < 285) return; // ~5 s de frames DSP (57/s)
	frames = 0;
	std::fprintf(stderr, "[audio-drops] 5s:");
	for (int i = 0; i < PCAD_Count; ++i) {
		const u32 c = sCounts[i].exchange(0);
		if (c) std::fprintf(stderr, " %s=%u", kNames[i], c);
	}
	std::fprintf(stderr, " | total:");
	for (int i = 0; i < PCAD_Count; ++i) {
		const u32 c = sTotals[i].load();
		if (c) std::fprintf(stderr, " %s=%u", kNames[i], c);
	}
	const int n = sWaveCount.load();
	if (n > 0) {
		std::fprintf(stderr, " | ondas:");
		for (int i = 0; i < n && i < kMaxWaves; ++i) {
			const u32 key = sWaves[i].load();
			std::fprintf(stderr, " %s:%u", kNames[key >> 16], key & 0xFFFF);
		}
	}
	std::fprintf(stderr, "\n");
	reportLevels();
}
