#ifndef PC_P2_CAPTAIN_PREVIEW_H
#define PC_P2_CAPTAIN_PREVIEW_H

/*
 * Pikmin 2, selector de capitanes: cada tarjeta dibuja el modelo 3D del
 * personaje con su animación de espera (docs/PLAN_SELECTOR_CAPITANES.md §10.4,
 * como pc_captain_preview de Pikmin 1).
 *
 * Los modelos salen de una copia propia de pikis.szs en un heap propio que no
 * se libera: el juego monta y suelta el suyo en cada sección.
 *
 * Se dibuja después de las placas del menú (que se pintan al final del frame
 * y taparían el modelo): begin, una llamada por tarjeta, end.
 */

/// Reserva el heap de los modelos. Al arrancar (GameFlow::run), antes de que
/// las secciones se queden con todo lo libre.
void pc_p2_captain_preview_reserve(void);

/// Prepara el dibujo (limpia la profundidad). False si no hay modelos.
bool pc_p2_captain_preview_begin(void);

/// Dibuja el personaje (PcP2Character; -1 = Louie, el "Original") dentro del
/// rectángulo (x, y, w, h) de una pantalla screenW x screenH, origen arriba a
/// la izquierda. selected lo hace girar; tint (rgb 0..1) o nullptr.
void pc_p2_captain_preview_draw(int character, int x, int y, int w, int h, int screenW, int screenH, bool selected,
                                const float* tint);

/// Deja la vista, la cámara y las luces como estaban.
void pc_p2_captain_preview_end(void);

/// Pantallas de selección de Desafío y VS: las imágenes con la cara de Olimar
/// pasan al personaje del capitán 1 y las de Louie al del capitán 2.
struct J2DPane;
void pc_p2_captain_icons_apply(J2DPane* root);

#endif
