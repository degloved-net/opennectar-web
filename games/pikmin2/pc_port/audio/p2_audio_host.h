#ifndef P2_AUDIO_HOST_H
#define P2_AUDIO_HOST_H

#include "types.h"
#include <stddef.h>

void pc_p2_audio_init();
void pc_p2_audio_set_dsp_tables(const u16* resample, const u16* adpcm);
void pc_p2_audio_render(void* channels, s16* planarOutput, u32 frameSamples);
/// Frame del DSP (JASDsp::syncFrame): destino planar y cuántos subframes trae.
void pc_p2_audio_begin_frame(void* channels, s16* planarOutput, u32 frameSamples, u32 subframes);
/// Mezcla el subframe `index` del frame en curso con el estado actual de los canales.
void pc_p2_audio_render_subframe(u32 index);
void pc_p2_audio_queue(const s16* interleaved, size_t frames);
void pc_p2_audio_set_fx_line(u32 index, bool enabled, u32 subframes, const u16 dest[2], const s16 volume[2],
                             const s16 coeffs[8]);

#endif
