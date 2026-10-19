#ifndef PC_RECORD_H
#define PC_RECORD_H

/*
 * Atajos para grabar vídeo (tráiler), como en Pikmin 1. Van con Cheats >
 * Debug Keys y usan Ctrl+número para no pisar las teclas F:
 *
 *   Ctrl+1  ocultar / mostrar el HUD
 *   Ctrl+2  gráficos originales / mejorados (sin tocar el .conf)
 *   Ctrl+3  luz de amanecer     Ctrl+4  mediodía     Ctrl+5  atardecer
 *   Ctrl+6  +50 Pikmin en el grupo del capitán (hasta que no quepan más)
 *
 * Pikmin 2 no tiene modo foto en el juego, así que no hay Ctrl+7/8.
 */

#ifdef __cplusplus
extern "C" {
#endif

/// Flanco de Ctrl+<digit> (1-9). Llamar una sola vez por fotograma y dígito.
int pc_record_ctrl_pressed(int digit);

int pc_record_hud_hidden(void);
void pc_record_toggle_hud(void);

#ifdef __cplusplus
}
#endif

#endif
