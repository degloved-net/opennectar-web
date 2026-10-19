# Auditoría de integración para netplay

Revisión inicial: commit `f0aaac03`, árbol local con cambios de usuario no relacionados.

## Fronteras compartidas

- Ambos juegos producen `PADStatus` mediante `pc_port/pc_window.cpp` y lo publican por `pc_port/dolphin_stubs/pad_stubs.cpp` → `PADRead` → `Controller`.
- El formato remoto se convertirá a un `NetInputFrame` de tamaños explícitos antes de reconstruir `PADStatus`.
- La biblioteca común vivirá en `libs/nectar_net` y no incluirá headers de ningún juego.
- Cada juego tendrá un adaptador en `pc_port/net`.

## Pikmin 1

| Área | Punto confirmado | Uso previsto |
|---|---|---|
| Captura | `pc_port/pc_window.cpp` | Crear input local normalizado |
| Inyección | `pc_port/dolphin_stubs/pad_stubs.cpp` | Elegir mando local/remoto |
| Tick | `GameCoreSection::updateAI` | Barrera begin/apply/simulate/capture |
| Sesión coop | `pc_port/pc_coop.cpp/.h` | Mantener selección local; no transportar aquí |
| Dos capitanes | `newPikiGame.cpp`, `gameCoreSection.cpp` | Reutilizar cámaras, HUD y controllers |
| Guardado | flujos de card/save del juego | Bloquear escritura en rol Replica |

Managers autoritativos iniciales: `NaviMgr`, `PikiMgr`, `TekiMgr`, bosses, `ItemMgr`, `PikiHeadMgr`, pellets, work objects, plantas y generadores persistentes.

## Pikmin 2

| Área | Punto confirmado | Uso previsto |
|---|---|---|
| Captura/inyección | mismos límites PAD del port | Reutilizar `NetInputFrame` |
| Sesión coop | `pc_port/pc_p2_coop.cpp/.h` | Espera hasta `DEMO_First_Use_Louie` |
| Dos capitanes | `BaseGameSection::setPlayerMode` | Sustituir fuente de `mControllerP2` |
| Superficie | `singleGameSection.cpp`, `singleGS_MainGame.cpp` | Épocas de escena y snapshots |
| Cuevas | `singleGS_CaveGame.cpp` | Seed + hash de layout + barrera por piso |
| Tick | `PC_ORIG_TICK()` / `GameSystem::startFrame` | Tick lógico canónico a 30 Hz |

Managers iniciales: `NaviMgr`, `PikiMgr`, `generalEnemyMgr`, `PelletMgr`, `ItemMgr`, Onion/pod, managers de mapa/cueva, tesoros y objetos de salida.

## Riesgos confirmados

- RNG global (`randFloat` y variantes) dentro de IA y spawns.
- Temporizadores y estado en coma flotante.
- Orden de iteración y reutilización de slots de managers.
- Rutas condicionales por región, plataforma y modo.
- Transiciones destruyen y recrean managers, invalidando punteros.
- Efectos audiovisuales no deben mutar el mundo dos veces en Replica.

## Contratos antes de gameplay

1. `NetWorldRole`: Offline, Authority, Replica y Replay.
2. `NetEntityId`: tipo/familia, slot lógico, generación y época de escena.
3. Ningún paquete contiene punteros ni índices sin generación.
4. Sólo el hilo principal aplica objetos de protocolo ya validados.
5. El save sólo se abre para escritura en Offline/Authority.
6. Las reglas del host son una capa temporal, no cambios persistentes del invitado.

## Familias de entidades (inventario v0)

| Familia | P1 | P2 | Prioridad | Estado mínimo inicial |
|---|---:|---:|---:|---|
| Capitán | sí | sí | 1 | transform, velocidad, salud, FSM, animación, jugador |
| Pikmin | sí | sí | 2 | tipo, madurez, propietario, acción, objetivo, flags |
| Pikmin plantado | sí | sí | 3 | tipo, madurez, posición, estado |
| Pellet/cadáver/tesoro/pieza | sí | sí | 4 | tipo, transform, portadores, destino, progreso |
| Onion/pod/UFO | sí | sí | 4 | poblaciones, animación, depósitos y flags |
| Obstáculo/construcción | sí | sí | 5 | tipo, progreso, salud/estado |
| Enemigo básico | sí | sí | 6 | especie, transform, salud, FSM, objetivo |
| Enemigo con agarre/proyectil | sí | sí | 7 | relaciones y subestado específico |
| Boss | sí | sí | 8 | esquema específico por especie |
| Planta/efecto de gameplay | sí | sí | 8 | sólo los que alteran simulación |
| Salida/geyser/agujero | no | sí | 8 | activación, propietario, transición |
| Estado global de escena | sí | sí | 1 | día, tiempo, curso, modo, epoch, reglas |

La lista por especie se ampliará durante las fases 5, 7 y 8; ninguna familia entra en producción sin esquema, límites y replay.
