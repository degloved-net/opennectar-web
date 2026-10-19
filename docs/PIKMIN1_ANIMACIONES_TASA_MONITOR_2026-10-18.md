# Animaciones y tasa de refresco del monitor

Fecha de implementación: 18 de octubre de 2026.

Commit oficial: `5a3e270f868309f2cfb6bb3c77a902b89c07d9af` (`Pikmin 1: interpolación visual a la tasa del monitor`).

Este cambio convierte el modo de FPS «Monitor» en una ruta sincronizada con la pantalla y añade una reconstrucción de pose específica para renderizar animaciones con más suavidad que los datos originales de 30 Hz. También estabiliza el reloj de animación, el cálculo de velocidad al caminar y algunos movimientos secundarios. El commit contiene 743 inserciones y 38 eliminaciones repartidas entre 15 archivos de Pikmin 1.

# Decisión oficial

La pose de un personaje se calcula en dos etapas durante una partida normal en PC:

1. El motor genera primero la pose habitual en el banco de matrices autoritativo.
2. Para dibujar, se genera una segunda pose en un banco de presentación independiente.
3. La segunda pose interpola rotaciones en cuaterniones y traslación/escala mediante una curva sin overshoot.
4. Al terminar `renderall()`, el modelo vuelve a apuntar al banco autoritativo.

La reconstrucción visual no modifica contadores de animación ni claves de eventos. Está desactivada en netplay determinista y en modo speedrun. La ruta determinista conserva sus reglas de 30 Hz.

# Cambios en la tasa de refresco

## Nuevo modo Monitor

El ajuste de FPS admite ahora cuatro valores:

- 30 FPS.
- 60 FPS.
- 120 FPS.
- `Monitor (X Hz)`.

El cuarto valor se guarda como `fpsMode = 3`. Al iniciar gameplay se consulta `pc_window_get_refresh_rate()`, se configura esa frecuencia como tasa alta y se utiliza el clamp especial `0`. La tasa aceptada por el scheduler queda limitada al intervalo de 30 a 360 Hz, por lo que contempla, entre otras, pantallas de 40, 50, 80, 120, 144, 240 y 300 Hz.

Archivos implicados:

- `pc_port/settings/pc_settings.cpp`
- `src/plugPikiColin/newPikiGame.cpp`
- `pc_port/timing/pc_frame_scheduler.cpp`
- `pc_port/timing/pc_frame_scheduler.h`

## VSync real y fallback

En modo Monitor con VSync activado, SDL solicita `SDL_GL_SetSwapInterval(1)`. El intercambio de buffers se convierte en el reloj de presentación y se evita aplicar simultáneamente el limitador por software.

Los modos fijos de 30 y 60 FPS mantienen `SDL_GL_SetSwapInterval(0)` y continúan usando el pacing del puerto. El modo de 120 FPS también usa el scheduler cuando no está activo el bloqueo de VSync del modo Monitor.

Si el driver rechaza el intervalo de intercambio, se desactiva el bloqueo y se vuelve al scheduler por software. Existe además un watchdog de medio segundo: si detecta más ticks de los compatibles con la frecuencia configurada, considera que el swap no está bloqueando y activa el mismo fallback.

Archivos implicados:

- `pc_port/pc_window.cpp`
- `src/sysDolphin/system.cpp`

# Reloj de animación

Se añadió `pc_port/timing/pc_anim_timing.h` como punto común para el timing visual.

`PcAnimClock` mantiene cuatro valores:

- `avg`: promedio lento del delta.
- `debt`: diferencia acumulada respecto al tiempo real.
- `out`: delta suavizado entregado a los animadores.
- `tick`: delta real del tick actual.

Las variaciones moderadas se filtran con un factor de 0,1 y la deuda se devuelve gradualmente para evitar deriva temporal. Un cambio superior al 60 % respecto al promedio se considera una transición real, como un hitch o un cambio de modo, y reinicia el filtro. Pausas, slow motion y escalas aplicadas por el juego se conservan mediante la relación entre el delta solicitado y el delta del tick.

Este reloj se conectó a:

- `AnimContext::animate()` en `src/sysCommon/shapeBase.cpp`.
- `Animator::animate()` en `src/plugPikiColin/animMgr.cpp`.
- `PaniAnimator::animate()` en `src/plugPikiNakata/panianimator.cpp`.
- `System::updateSysClock()` en `src/sysDolphin/system.cpp`.

En netplay determinista, `pc_anim_dt()` devuelve directamente el delta original.

# Reconstrucción de la pose visual

La implementación principal está en `src/sysCommon/shapeBase.cpp`.

## Rotación

Para animaciones DCA se leen las rotaciones Euler de las dos muestras vecinas, se convierten a cuaterniones y se aplica shortest-path slerp. Esto evita interpolar X, Y y Z de forma independiente y reduce giros largos, wobble y discontinuidades angulares.

La conversión reproduce explícitamente el orden usado por `Matrix4f::makeSRT`: `Rz × Ry × Rx`. No se utiliza `Quat::fromEuler()` en esta ruta porque su multiplicación final escribe sobre uno de sus operandos y generaba una rotación incorrecta. Ese defecto produjo durante la primera prueba modelos deformados —incluidos Olimar y las plantas— al acumularse el error por la jerarquía de huesos. La conversión explícita corrigió el problema.

## Traslación y escala

Traslación y escala DCA utilizan una cúbica monotónica tipo Hermite/PCHIP local. Las tangentes se anulan en extremos y cambios de signo, evitando que la interpolación invente valores fuera del intervalo entre dos muestras. Los canales ausentes usan valores neutros: escala 1 y traslación 0.

Las animaciones DCK mantienen su evaluación Hermite existente; su resultado Euler se convierte a quaternion únicamente en el límite de la pose visual.

## Cambios de clip y reinicios

Cada pareja `owner + BaseShape` conserva la última pose mostrada por articulación. Si cambia `AnimData` o el contador retrocede más de medio frame, se inicia un crossfade de 80 ms desde la pose anterior. El peso usa smoothstep y las rotaciones se mezclan mediante slerp.

La transición sólo avanza una vez por frame de motor aunque un modelo tenga varias pasadas de dibujo. Los estados que llevan más de 30 frames sin verse quedan marcados para eliminación; la limpieza del mapa se ejecuta periódicamente, cada 256 frames.

## Bancos de matrices

`Graphics::initRender()` reinicia un banco `present` separado del banco habitual. `BaseShape::updateAnim()` calcula primero la pose normal y sus matrices ponderadas; después, si corresponde, reserva matrices del banco de presentación, reconstruye la jerarquía visual y recalcula los envelopes.

`PlugPikiApp::idle()` llama a `pc_netplay_present_restore_all_shapes()` inmediatamente después de `renderall()`. Así, antes del siguiente update, cada `BaseShape` vuelve a su puntero de matrices original.

Archivos implicados:

- `src/sysCommon/graphics.cpp`
- `src/sysCommon/shapeBase.cpp`
- `src/plugPikiColin/plugPiki.cpp`

# Interpolación DCA general

Además del segundo banco visual, `AnimData::extractSRT()` cambió su muestreo DCA general de lerp lineal a Catmull-Rom con cuatro muestras. Las diferencias angulares se normalizan al intervalo corto alrededor de ±π antes de construir la curva.

Este cambio general no está limitado al banco visual: forma parte de la evaluación DCA habitual. La reconstrucción con PCHIP y quaternion descrita anteriormente sí se ejecuta exclusivamente para la pose de presentación.

# Caminata y movimiento secundario

El cálculo de velocidad que selecciona y acelera las animaciones de caminar ya no utiliza únicamente el desplazamiento del tick actual. Se mantiene un historial de aproximadamente 1/30 s, equivalente a la ventana original a 30 Hz. Los cooldowns que antes duraban cuatro ticks se convierten al número equivalente de ticks de la frecuencia actual.

Se aplica a Olimar y a los Pikmin en:

- `src/plugPikiKando/navi.cpp`
- `src/plugPikiKando/piki.cpp`

También se añadieron resortes amortiguados de presentación:

- Olimar y los Pikmin se inclinan levemente al arrancar, detenerse y girar.
- La hoja, brote o flor del Pikmin recibe un lag visual independiente en la articulación 6.
- La inclinación corporal está limitada aproximadamente a 7 grados.
- El apéndice está limitado aproximadamente a 25 grados.
- No se aplica en netplay determinista, teletransportes, deltas inválidos ni determinados estados de interacción.

La aplicación visual de los resortes está en `navi.cpp` y `viewPiki.cpp`; sus cálculos comunes están en `pc_anim_timing.h`.

# Archivos del commit

| Archivo | Cambio |
| --- | --- |
| `pc_port/pc_window.cpp` | VSync real en modo Monitor, pacing adaptado y fallback. |
| `pc_port/settings/pc_settings.cpp` | Cuarto modo de FPS y persistencia de `fpsMode = 3`. |
| `pc_port/timing/pc_anim_timing.h` | Reloj suavizado, ventana de caminata y resortes visuales. |
| `pc_port/timing/pc_frame_scheduler.cpp` | Tasa alta configurable y estado de bloqueo VSync. |
| `pc_port/timing/pc_frame_scheduler.h` | API pública del modo de tasa alta. |
| `src/plugPikiColin/animMgr.cpp` | `Animator` usa el delta de animación suavizado. |
| `src/plugPikiColin/newPikiGame.cpp` | Activa la tasa del monitor al entrar en gameplay. |
| `src/plugPikiColin/plugPiki.cpp` | Restaura punteros autoritativos después de renderizar. |
| `src/plugPikiKando/navi.cpp` | Ventana de velocidad y movimiento secundario de Olimar. |
| `src/plugPikiKando/piki.cpp` | Ventana de velocidad y cooldowns de los Pikmin. |
| `src/plugPikiKando/viewPiki.cpp` | Inclinación visual del cuerpo y del apéndice del Pikmin. |
| `src/plugPikiNakata/panianimator.cpp` | `PaniAnimator` usa el delta suavizado. |
| `src/sysCommon/graphics.cpp` | Reinicio por frame del banco de matrices de presentación. |
| `src/sysCommon/shapeBase.cpp` | Muestreo DCA, reconstrucción visual y corrección Euler-quaternion. |
| `src/sysDolphin/system.cpp` | Tick ligado a VSync, watchdog y actualización del reloj visual. |

# Compilación y pruebas

Se compiló el objetivo completo:

```text
cmake --build build/pikmin1 --target pikmin_pc -j4
[100%] Built target pikmin_pc
```

El ejecutable resultante fue `build/pikmin1/bin/nectar`.

Se ejecutaron estas cinco pruebas:

- `pc_frame_scheduler_test`
- `pc_render_phase_test`
- `pc_visual_snapshot_test`
- `pc_visual_runtime_test`
- `pc_netplay_present_test`

Resultado: 5 de 5 superadas. `git diff --check` tampoco encontró errores de espacios o formato.

La prueba visual inicial descubrió la deformación causada por la conversión Euler-quaternion. Tras sustituirla por la fórmula explícita `Rz × Ry × Rx`, Olimar y las plantas recuperaron su forma correcta en la comprobación dentro del juego.

# Límites actuales

- Si el equipo sólo produce 40 imágenes por segundo, el sistema no puede mostrar 144 poses diferentes sin generación de frames. Sí mantiene el tiempo de la animación y genera una pose adecuada para cada frame que realmente se dibuja.
- El reloj actual suaviza jitter pequeño y devuelve gradualmente la deuda. Bajo carga irregular todavía puede percibirse stutter del render o una corrección temporal leve.
- La frecuencia alta está limitada actualmente a 360 Hz.
- El modo Monitor depende de la frecuencia reportada por SDL y del comportamiento real del driver o compositor. El fallback evita que un VSync no funcional deje el bucle sin pacing.
- La interpolación visual quaternion/PCHIP está desactivada durante netplay determinista y speedrun.

# Ejecución

El launcher oficial utiliza el binario modificado mediante:

```bash
"/home/sunking/Documentos/antigravity/Open Nectar Fusion/scripts/run-fusion.sh"
```
