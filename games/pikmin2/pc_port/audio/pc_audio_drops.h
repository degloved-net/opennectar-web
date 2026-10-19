#ifndef PC_AUDIO_DROPS_H
#define PC_AUDIO_DROPS_H

#include "types.h"

// Diagnóstico de sonidos que se pierden (PIKMIN_AUDIO_DROPS=1): cuenta, por
// motivo, cada efecto o nota que el juego pide y no llega a sonar, y cada
// pocos segundos lo resume por stderr. Sin la variable no cuesta nada.
enum PcAudioDrop {
	PCAD_SeRequest,     ///< storeSeBuffer: efecto pedido
	PCAD_SeCancelled,   ///< categoría cancelada (seEntryCancel)
	PCAD_SeLowPriority, ///< categoría llena y prioridad menor
	PCAD_SeNoObject,    ///< sin JAISe libre en la categoría
	PCAD_NoteOn,        ///< JASBankMgr::noteOn: nota pedida
	PCAD_NoBank,
	PCAD_NoInst,
	PCAD_NoInstParam,   ///< el instrumento no cubre esa tecla/velocidad
	PCAD_NoWaveBank,
	PCAD_NoWaveHandle,  ///< onda no registrada en el banco
	PCAD_NoWaveInfo,
	PCAD_NoWavePtr,     ///< onda registrada pero su archivo no está cargado
	PCAD_NoChannel,     ///< sin JASChannel en el pool
	PCAD_PlayFail,      ///< sin canal DSP libre (prioridad)
	PCAD_WaveLoadFail,  ///< copia de un .aw a ARAM fallida
	PCAD_AramMiss,      ///< el DSP leyó una onda fuera de la ARAM
	PCAD_VoiceStart,    ///< voces que arrancan en el DSP
	PCAD_VoiceShort,    ///< voces que acabaron en menos de 40 ms
	PCAD_VoiceInaudible,///< voces enteras por debajo de -50 dB de ganancia
	PCAD_ShortSampleEnd,  ///< corta: se acabó la muestra (no hace bucle)
	PCAD_ShortEndRequest, ///< corta: el juego pidió pararla
	PCAD_ShortDisabled,   ///< corta: la CPU apagó el canal
	PCAD_InaudibleGame,   ///< inaudible porque el juego pidió ese volumen
	PCAD_InaudibleDsp,    ///< el juego pidió volumen y el DSP no lo dio
	PCAD_Count
};

void pc_audio_drop(int reason);
/// Igual, apuntando la onda (las primeras distintas se listan en el resumen).
void pc_audio_drop_wave(int reason, u32 waveId);
/// Una vez por frame del DSP; imprime el resumen cada ~5 s.
void pc_audio_drop_report();

// Niveles (misma variable PIKMIN_AUDIO_DROPS): RMS y pico por fuente, para ver
// si algo suena desproporcionado o si la suma satura.
enum PcAudioLevel {
	PCAL_Voice3D, ///< voces del DSP con auto-mixer (efectos posicionados)
	PCAL_Voice2D, ///< resto de voces del DSP (secuencias: música, efectos 2D)
	PCAL_DspOut,  ///< salida del DSP tras el volumen maestro
	PCAL_Stream,  ///< música en stream (AST)
	PCAL_Final,   ///< lo que se manda a la tarjeta
	PCAL_Count
};
bool pc_audio_levels_enabled();
/// Un bloque ya medido: suma de cuadrados, pico, muestras y cuántas saturaron.
void pc_audio_level_block(int source, double sumSquares, int peak, unsigned samples, unsigned clipped);
/// Ganancia efectiva (Q15, 32768 = 1.0) de una voz; se guarda la mayor.
void pc_audio_level_gain(int source, int gainQ15);

#endif
