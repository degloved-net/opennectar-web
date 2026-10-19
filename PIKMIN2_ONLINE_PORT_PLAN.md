# Investigación y plan de migración del sistema online de Pikmin 1 a Pikmin 2

## Resumen ejecutivo

Portar las funcionalidades online de Pikmin 1 a Pikmin 2 es viable. El cooperativo local de Pikmin 2 ya resuelve buena parte de la integración visual y de controles, incluyendo dos capitanes, dos mandos, pantalla partida, cámara fusionada, propietario de menús y adaptación de cinemáticas.

Lo que todavía falta es prácticamente toda la infraestructura de sesión, determinismo, diagnóstico, guardado online y sincronización de escenas.

La recomendación principal es portar las funcionalidades, pero no copiar literalmente el sistema monolítico de Pikmin 1. Primero debe extraerse un núcleo de sesión común y dejar adaptadores específicos para cada juego.

## 1. Estado actual

### 1.1 Sistemas online existentes en Pikmin 1

Hay dos sistemas distintos dentro de Pikmin 1:

| Sistema | Transporte | Sincronización | Uso actual |
|---|---|---|---|
| Comunitario | GekkoNet + UDP/libjuice ICE, códigos `NPIX3` | Lockstep determinista | Cooperativo y VS; buenas herramientas de diagnóstico |
| `nectar_net` | GNS cifrado, código único `NECTAR1` | Réplica exacta por inputs; modo predictivo experimental | Camino moderno: guardado, reconexión, espectador y HUD |

La selección entre ambos se hace en compilación mediante `PIKI_NECTAR_ONLINE`. Incluso la misma pantalla «Online» ejecuta caminos completamente distintos en `games/pikmin1/pc_port/netplay/pc_online_menu.cpp`.

### 1.2 Estado de Pikmin 2

Pikmin 2 actualmente:

- Tiene la opción `NECTAR_ONLINE`, pero solamente enlaza `nectar_net`. No crea sesión, menú ni hooks de red. Activar la opción produce un binario sin online funcional.
- Ya tiene cooperativo local de historia integrado en unos 35 archivos.
- Ya dispone de dos capitanes, dos mandos, pantalla partida, cámara fusionada, propietario de menús y adaptación de cinemáticas.
- Centraliza la entrada de Louie en `games/pikmin2/pc_port/pc_p2_coop.cpp`.
- Gestiona los modos, cámaras y controladores en `BaseGameSection::setPlayerMode`.
- Separa el tick original de 30 Hz de la presentación a 60/120 FPS mediante `PC_ORIG_TICK()`.
- Admite `NECTAR_SAVE_DIR`, lo que permite aislar el guardado del invitado de forma similar a Pikmin 1.

Por tanto, no hay que inventar el segundo jugador de Pikmin 2. Hay que sustituir el origen del mando de P2 y sincronizar el mundo.

## 2. Arquitectura recomendada

No se recomienda copiar los aproximadamente 20.000 LOC de `games/pikmin1/pc_port/netplay` dentro de Pikmin 2.

La sesión principal de Pikmin 1 ya supera las 5.000 líneas y mezcla:

- lockstep comunitario;
- ICE;
- `nectar_net`;
- réplica exacta;
- modo predictivo;
- guardado;
- reconexión;
- HUD;
- diagnóstico específico de Pikmin 1.

El núcleo debería extraerse primero a una sesión común:

```text
libs/nectar_session/
  session_core
  input_transport
  scene_barrier
  save_transfer
  reconnect
  diagnostics
  hud_model

games/pikmin1/.../P1NetAdapter
games/pikmin2/.../P2NetAdapter
```

El adaptador de cada juego proporcionaría:

- identificación de juego, build y contenido;
- captura e inyección de mandos;
- avance de un tick;
- hash y volcado de estado;
- detección de escena y carga;
- reglas de sesión;
- acceso al guardado;
- estado de pausa, cinemática y menú;
- cámara y jugador local.

La meta oficial sigue siendo host autoritativo con snapshots y predicción, según `docs/netplay/ADR-0001-NETPLAY-ARCHITECTURE.md`.

Sin embargo, el sistema que actualmente replica todo el mundo es la réplica exacta de inputs. Por ello se propone:

1. Portar primero la réplica exacta como beta, porque permite cubrir rápidamente todo el mundo.
2. Añadir checkpoints por día y piso, junto con barreras de escena desde el principio.
3. Mantener después un segundo carril para snapshots autoritativos y predicción, necesario para reducir latencia y garantizar cross-play ARM64/x86-64.

## 3. Plan de implementación

### Fase 0 — Consolidar el diseño

Duración estimada: 3–5 días.

- Decidir que `NECTAR1`/`nectar_net` sea el camino de producto.
- Conservar GekkoNet/ICE como diagnóstico y, opcionalmente, para partidas LAN.
- Actualizar el ADR: actualmente declara lockstep rechazado, pero el modo de mundo completo realmente utilizado es una réplica determinista.
- Eliminar la bifurcación invisible donde la misma UI cambia totalmente según una macro.
- Separar de `pc_netplay_session.cpp` el núcleo común y las funciones específicas de Pikmin 1.

Criterio de salida: Pikmin 1 sigue funcionando sin regresiones usando la sesión común.

### Fase 1 — Esqueleto de netplay de Pikmin 2

Duración estimada: 4–6 días.

Añadir a Pikmin 2:

- `PIKMIN_NETPLAY_BUILD`;
- identidad reproducible de build y contenido;
- flags deterministas: sin FMA, inicialización controlada y libm reproducible;
- implementaciones no-op cuando netplay está desactivado;
- parseo de `--online-host`, `--online-join` y `--online-save`;
- integración en `pc_main`;
- hook de sesión en el bucle principal;
- propiedad del pacing a 30 Hz;
- proveedor de input antes de que `JUTGamePad::read()` llegue a los `Controller`;
- targets de prueba de sesión.

Criterio de salida: dos procesos conectan, negocian `GameId::Pikmin2`, intercambian reloj e input, aunque todavía no jueguen.

### Fase 2 — Control remoto y experiencia de usuario

Duración estimada: 5–8 días.

- El host controla a Olimar.
- El invitado controla a Louie o al Presidente.
- Antes de `DEMO_First_Use_Louie`, el invitado espera y observa.
- Cuando Louie se activa, el host envía `PlayerActivated` con el tick efectivo.
- Capturar todo el input extendido de Pikmin 2:
  - botones y sticks;
  - sprays;
  - apuntado y cursor;
  - lock-on, carga y cámara;
  - propietario de pausa y menús;
  - acciones específicas de Battle y Challenge.
- Portar el menú Crear/Unirse, código al portapapeles, errores y relanzamiento.
- Portar el HUD de ping, jitter, retraso, desconexión, espectador y desync.
- Añadir «dejar cooperativo/reincorporarse» a F1.

Criterio de salida: ambos capitanes se mueven correctamente en superficie con una conexión real.

### Fase 3 — Determinismo y diagnóstico de Pikmin 2

Duración estimada: 2–3 semanas.

Es la fase de mayor riesgo.

Crear `P2StateHash` con orden estable y campos normalizados que cubra:

- estado global, día, hora y curso;
- cueva, piso y semillas;
- RNG del juego y randomizer;
- Olimar, Louie o Presidente, salud, FSM y referencias;
- Pikmin por color, madurez, grupo, estado y propietario;
- Pikmin en cebollas y pod;
- enemigos y bosses;
- tesoros, pellets y cadáveres;
- puentes, muros, agujeros, géiseres y objetos interactivos;
- sprays, Pokos, deuda y flags de historia;
- pausa, menú, película y transición;
- puntuación, llaves y victoria de Challenge y Battle.

También se necesita:

- volcado por objeto con identificador estable;
- grabación y reproducción local de inputs;
- búsqueda de estado dependiente del render, audio o direcciones de memoria;
- barrido de valores sin inicializar y orden no estable de managers;
- pruebas de 30–60 minutos en superficie y varias cuevas.

Criterio de salida: dos ejecuciones locales con el mismo save e input producen hashes idénticos durante varios días y pisos.

### Fase 4 — Réplica exacta online

Duración estimada: 1–2 semanas.

Reutilizar de Pikmin 1:

- `InputBundle`;
- `AppliedInputs`;
- `StateHash`;
- reglas del host;
- semilla;
- pausa;
- espectador;
- pérdida y reordenación simulada;
- copia inicial del guardado;
- carpeta privada del invitado;
- host como único propietario del progreso.

Cambios imprescindibles para Pikmin 2:

- El invitado no debe reproducir toda la campaña desde el tick cero para reconectar.
- Crear un checkpoint al inicio del día y de cada piso.
- Rejoin = checkpoint de escena + inputs posteriores.
- Nunca escribir desde el invitado en su tarjeta real.
- Los resultados, tesoros, Piklopedia, deuda y logros de progreso los confirma el host.

Criterio de salida: varios días de superficie sin desync bajo RTT 150–185 ms, jitter 30 ms y 5 % de pérdida.

### Fase 5 — Cuevas y barreras de escena

Duración estimada: 2–3 semanas.

Cada entrada o cambio de piso incrementará `scene_epoch`:

1. El host congela la simulación.
2. Envía curso, cueva, piso, semillas, reglas y hash del layout.
3. Ambos cargan localmente.
4. El invitado responde `SceneReady`.
5. El host envía el checkpoint inicial.
6. El invitado lo aplica y valida su hash.
7. `SceneStart` inicia el nuevo tick.
8. Los paquetes de la época anterior se descartan.

Cubrir específicamente:

- generación procedural;
- posición de capitanes y grupos;
- tesoros y enemigos generados;
- agujeros y géiseres;
- sprays;
- cinemáticas de descubrimiento;
- salida, resultados y guardado;
- caída de un capitán;
- cambio Louie → Presidente.

El protocolo ya define estos mensajes, pero Pikmin 1 mantiene actualmente `sceneEpoch` constante en 1. En Pikmin 2 deben implementarse de verdad desde el primer piso.

Criterio de salida: recorrido completo de todas las cuevas, repitiendo cambios de piso y reconexiones.

### Fase 6 — Challenge y Battle

Duración estimada: 1–2 semanas.

Challenge es relativamente directo porque ya es cooperativo local.

Battle necesita una decisión adicional: la réplica exacta da una ventaja clara al host. En Pikmin 1 se han medido aproximadamente 104 ms de respuesta local para el invitado y unos 294 ms con RTT de 185 ms.

Para Battle se recomienda:

- lockstep simétrico con delay adaptativo como primera versión; o
- host autoritativo con predicción y reconciliación antes de publicarlo.

No se recomienda publicar Battle con host instantáneo e invitado a `RTT + buffer`.

Criterio de salida: resultados idénticos, pausas y control del menú acordados, y ninguna ventaja estructural no documentada.

### Fase 7 — Endurecimiento

Duración estimada: 2–4 semanas.

- Linux, Windows y Android reales.
- x86-64 ↔ ARM64.
- 30/60/120 FPS mezclados.
- Wi-Fi, datos móviles, IPv4, IPv6, UPnP, PCP, NAT-PMP y reenvío manual.
- Suspensión y reanudación de Android.
- Cambio de red.
- Pérdida, duplicación, reorder y cortes largos.
- ASan, UBSan y TSan.
- Fuzzing de mensajes y guardados transferidos.
- Soak mínimo de ocho horas.
- Campaña completa manual.
- Mensajes claros para CGNAT y firewall.
- Versión de protocolo y migración.

## 4. Estimación global

Para una persona trabajando a tiempo completo:

| Hito | Estimación |
|---|---:|
| Primera demo jugable en superficie Linux | 3–5 semanas |
| Historia completa, superficie y cuevas, misma arquitectura | 7–10 semanas |
| Challenge/Battle y beta multiplataforma | 10–14 semanas |
| Versión robusta con snapshots autoritativos, predicción y cross-play garantizado | 16–24 semanas |

Estas cifras asumen que se extrae el núcleo común. Copiar el archivo monolítico de Pikmin 1 podría dar una demo antes, pero aumentaría notablemente el coste de cuevas, correcciones y mantenimiento.

## 5. Bugs y mejoras encontrados

### 5.1 Prioridad alta

#### 1. La prueba ICE actual falla de forma reproducible

Ejecutada fuera del sandbox, la prueba de loopback no consiguió crear la respuesta ni unir el par y terminó con 79 fallos.

La UDP básica sí pasó 2.000 frames con cero desyncs.

Esto no demuestra que todas las conexiones reales fallen, pero sí que la prueba que promete funcionar sin red externa está rota o depende de un comportamiento de libjuice que ya no se cumple.

Archivo: `games/pikmin1/pc_port/netplay/pc_netplay_ice_test.cpp`.

#### 2. La prueba ICE no hace fail-fast

Cuando falla la conexión, continúa enviando 64 paquetes y esperando varios timeouts. Esto genera decenas de errores derivados y tarda alrededor de 90 segundos.

Debe detener el bloque en el primer fallo de creación o conexión y mostrar el error original.

#### 3. No existe reparación de desync

La réplica exacta detecta hashes distintos, pero sólo puede informar o detenerse. No puede solicitar un checkpoint ni un snapshot correctivo.

Se necesita:

- identificar el grupo de estado divergente;
- solicitar corrección parcial cuando sea posible;
- aplicar checkpoint completo en un punto seguro;
- generar informe y desconectar únicamente si la reparación falla.

#### 4. La reconexión actual no escala

El host guarda hasta cuatro horas de inputs y el invitado los reproduce todos. Además, debe dibujar cada tick porque se comprobó que omitir presentación altera la simulación.

En niveles avanzados se han medido solamente unas 3× de velocidad de puesta al día. Una sesión larga puede requerir una espera enorme.

Debe sustituirse por checkpoints periódicos y un historial corto posterior al checkpoint.

#### 5. La opción `NECTAR_ONLINE` de Pikmin 2 es engañosa

`NECTAR_ONLINE=ON` enlaza la biblioteca, pero no añade sesión ni interfaz.

Hasta que exista el adaptador, sería mejor:

- ocultar la opción; o
- hacer que CMake falle con un mensaje como `Pikmin 2 online adapter not implemented`.

### 5.2 Prioridad media

#### 6. Dos productos distintos bajo la misma UI

Dependiendo de una macro, «Online» significa código `NPIX3` con oferta/respuesta o `NECTAR1` con código único.

Debe existir un único camino de usuario. El sistema legado puede quedar detrás de una opción de desarrollo o diagnóstico.

#### 7. Latencia demasiado alta para acciones rápidas

La réplica exacta es razonable para historia, pero unos 294 ms con RTT de 185 ms se notan al lanzar, silbar o combatir y son especialmente problemáticos en Battle.

La predicción del capitán del invitado debe volver a ser una prioridad.

#### 8. Falta un `scene_epoch` real

El protocolo contempla épocas crecientes, pero Pikmin 1 usa una constante.

Pikmin 2 tiene muchas más cargas y cambios de managers. Aceptar paquetes viejos de otro piso sería especialmente peligroso.

#### 9. Duplicación fiable y no fiable en cada tick

Cada `AppliedInputs` se envía con ocho ticks de redundancia por el canal no fiable y además con una copia fiable por tick.

Aunque funcionó en las pruebas de 30 minutos, la copia fiable continua puede producir head-of-line y tráfico innecesario bajo pérdida sostenida.

Conviene comparar:

- reparación selectiva o NACK;
- copia fiable sólo de huecos;
- ventanas de confirmación;
- métricas de cola fiable.

#### 10. Cross-play real todavía no está demostrado

Hay builds y emulación ARM64, pero no pruebas prolongadas en Windows y Android físicos.

Para réplica exacta esto es un requisito de corrección, no sólo una prueba adicional.

#### 11. Riesgo MITM aceptado

GNS cifra, pero no autentica al peer frente a un atacante activo en ruta. Está documentado como riesgo aceptado en `docs/netplay/ADR-0002-SESSION-AUTHENTICATION.md`.

Para una beta entre amigos puede aceptarse. Para distribución pública debería considerarse `NECTAR2` con Noise o una clave pública fijada por el código.

#### 12. Documentación desactualizada y contradictoria

El plan afirma en unas secciones que sólo se replican capitanes o que falta el gateway Android, mientras secciones posteriores describen réplica completa y gateway terminado.

El plan de cooperativo local de Pikmin 2 también empieza diciendo que no implica implementación, aunque gran parte ya está implementada.

## 6. Pruebas realizadas durante la investigación

### Pikmin 2

- 18 pruebas ejecutadas correctamente.
- La integración de audio se omitió por falta de assets.

### Pikmin 1 y sistema online

- Pasan las pruebas unitarias de hashes, transferencia, input, adaptación, load guard, HUD, cámara y libm.
- UDP ejecutada fuera del sandbox: `PASS`, 2.000 frames y 0 desyncs.
- ICE ejecutada fuera del sandbox: fallo reproducible en la creación del par de loopback.
- La suite completa de Pikmin 1 detectó además un fallo de shader no relacionado con online: los uniforms `uChan0AlphaEn` y `uChan0AlphaDiff` no aparecen en el ubershader.

## 7. Orden recomendado de trabajo

El orden concreto recomendado es:

1. Arreglar la prueba ICE y decidir qué transporte será el camino oficial.
2. Extraer la sesión común de Pikmin 1.
3. Añadir build determinista y hooks vacíos de Pikmin 2.
4. Construir grabación, reproducción y hash de estado de Pikmin 2.
5. Demostrar determinismo en superficie.
6. Conectar dos procesos y controlar a Louie remotamente.
7. Incorporar guardado aislado, HUD, espectador y reconexión por checkpoint.
8. Implementar `scene_epoch` y barreras de cuevas.
9. Completar campaña y matriz de enemigos/objetos.
10. Añadir Challenge.
11. Resolver el modelo de latencia de Battle.
12. Ejecutar la matriz real de plataformas, routers y Android lifecycle.

## 8. Definición de terminado

El port no debe considerarse terminado solamente porque dos ventanas puedan mover a los dos capitanes.

Debe cumplir, como mínimo:

- Crear y unirse mediante código desde la interfaz.
- Continuar una partida sin que el invitado escriba en su guardado real.
- Superficie y todas las cuevas completables.
- Cambios de piso protegidos por `scene_epoch` y barreras.
- Reconexión mediante checkpoint en un tiempo razonable.
- Sin desync silencioso.
- Informe útil cuando haya divergencia.
- Host capaz de continuar si el invitado se desconecta.
- Espectador y reincorporación seguros.
- Challenge funcional.
- Battle sin ventaja de latencia estructural oculta.
- Windows, Linux y Android probados físicamente.
- Cross-play x86-64/ARM64 validado.
- Soak de ocho horas.
- Campaña completa jugada manualmente.
- Errores claros para firewall, CGNAT, incompatibilidad y mala conexión.

## 9. Archivos de referencia

- `ONLINE_COOP_IMPLEMENTATION_PLAN.md`
- `docs/netplay/ADR-0001-NETPLAY-ARCHITECTURE.md`
- `docs/netplay/ADR-0002-SESSION-AUTHENTICATION.md`
- `docs/netplay/PROTOCOL.md`
- `games/pikmin2/docs/PLAN_COOP.md`
- `games/pikmin1/pc_port/netplay/pc_netplay_session.cpp`
- `games/pikmin1/pc_port/netplay/pc_netplay_ice_test.cpp`
- `games/pikmin1/pc_port/netplay/pc_online_menu.cpp`
- `games/pikmin2/pc_port/pc_p2_coop.cpp`
- `games/pikmin2/pikmin2-decomp/src/plugProjectKandoU/baseGameSection.cpp`
- `games/pikmin2/pikmin2-decomp/src/sysGCU/system.cpp`
- `games/pikmin2/CMakeLists.txt`

