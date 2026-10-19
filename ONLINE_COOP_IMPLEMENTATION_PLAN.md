# Plan de implementación del cooperativo online de Open Nectar

Estado (15-10-2026): implementación en curso. `nectar_net` completo en `main`
(transporte, seguridad, apertura de puertos, sala de un código, protocolo v1);
Pikmin 1 tiene una vertical host-autoritativa de capitanes funcionando en
Linux. Pendiente: launcher, pruebas en Windows/Android reales, Fase 5 y Pikmin 2
Alcance: Pikmin 1 y Pikmin 2; Windows, Linux y Android
Modelo elegido: host autoritativo, cliente predictivo, UDP cifrado directo, conexión mediante código autocontenido
Objetivo de calidad: conexión sencilla, ausencia de desincronizaciones silenciosas, recuperación ante pérdidas y compatibilidad cruzada entre plataformas

## 1. Resumen ejecutivo

### 1.1 Base comunitaria integrada el 14 de octubre de 2026

Los PR #89–#92 de `5laric` ya forman parte de `main` y se consideran una
aportación fundacional, no un producto online terminado. Aportan:

- separación de simulación determinista y presentación/cámara/audio local;
- sesión cooperativa y VS determinista mediante GekkoNet sobre UDP;
- delay de input adaptativo, barreras de carga y detección/forensics de desync;
- transporte ICE mediante libjuice, códigos oferta/respuesta y UI Host/Join;
- adopción temporal de reglas del host y transferencia del `map.json` de VS.

Se conservarán estas piezas donde reduzcan riesgo y trabajo. El lockstep queda
como primera vertical funcional y modo diagnóstico. No sustituye la meta de
producción host-autoritativa descrita en este documento, necesaria para reparar
estado y hacer cross-play ARM64/x86-64 sin depender de determinismo perfecto.

Adaptaciones obligatorias sobre la contribución:

- [x] fusionar la serie completa sin perder `nectar_net` ni el plan existente;
- [x] compilar juego y launcher Linux y ejecutar las pruebas UDP/ICE/determinismo;
- [x] sustituir la identidad SHA-256 del ejecutable por un manifiesto de build
  independiente del sistema operativo;
- [x] definir un código autocontenido `NECTAR1` acotado y versionado, con
  endpoints IPv4/IPv6, caducidad, nonce y secreto (congelado; el campo de
  fingerprint se retiró por ADR-0002);
- [x] implementar y probar en Linux un adaptador `GnsTransport` cifrado con
  mensajes fiables/no fiables y un único invitado;
- [x] un único código del host con respuesta automática dentro del protocolo
  (`HostRoom`/`GuestJoin`, modo `--online-host`/`--online-join` de Pikmin 1);
- [ ] sustituir en el launcher el intercambio NPIX2/NPIX1 por ese código
  (bloqueado mientras el launcher tenga cambios locales ajenos sin commit);
- [x] validar el `join_secret` de `NECTAR1` en cuarentena antes de aceptar
  gameplay (ADR-0002; MITM activo en ruta aceptado como riesgo residual);
- [ ] activar el nuevo transporte en Android ARM64 real: compila y pasa sus
  pruebas bajo `qemu-aarch64`; falta dispositivo y la capa Java del gateway;
- [x] primera vertical de autoridad (capitanes) sobre `nectar_net`; el resto
  del mundo (snapshots) en la Fase 5.

### 1.2 Estado técnico verificable al 15 de octubre de 2026

Hay tres capas distintas y no deben confundirse:

1. **Online comunitario (lockstep):** Pikmin 1 de escritorio con GekkoNet, UDP
   o ICE, dos códigos `NPIX2/NPIX1`, reglas del host, VS y forensics. Se
   conserva intacto como herramienta y diagnóstico; las pruebas de 3.000
   ticks siguen dando exactamente los mismos hashes tras todos los cambios.
2. **`nectar_net` (en `main`, común a ambos juegos):** `GnsTransport` cifrado
   con cuarentena del secreto, veto por origen y comprobación de
   compatibilidad (`GatedTransport`); código `NECTAR1` congelado; apertura de
   puerto PCP/NAT-PMP/UPnP/IPv6 (`PortPublisher`); sala de un código
   (`HostRoom`/`GuestJoin`); protocolo v1 de juego (input con redundancia,
   reloj, entidades, escena, replay) y replicación de capitanes
   (predicción, corrección, interpolación). Dependencias fijadas y con huella
   en `scripts/netplay-deps.sh`.
3. **Pikmin 1 online host-autoritativo (vertical de capitanes):** modo
   `--online-host`/`--online-join` con un código; host a 30 Hz sin esperar;
   reglas del host adoptadas; input del host reenviado; pausa replicada;
   espectador y reincorporación; cámara/HUD por jugador.

Validaciones ya ejecutadas (Linux salvo indicación):

- `nectar_net`: 18 pruebas con GNS, también ASan+UBSan+LSan; TSan sin
  carreras de datos; fuzzing de todos los parsers; las mismas pruebas como
  binarios Android ARM64 bajo `qemu-aarch64` y cruce Linux↔Android en dos
  procesos; build MinGW cuyos `.exe` sólo importan DLL del sistema;
- Pikmin 1 con dos procesos reales (`tools/netplay/run_online_pair.py`,
  `SDL_VIDEODRIVER=offscreen`): 30 minutos reales a RTT ~182 ms, jitter
  30 ms y 5 % de pérdida con 99,89 % de input en vivo y sin deriva; con las
  reglas y el input del host reenviados, error medio del capitán del
  invitado de 0,06 unidades bajo esas condiciones;
- pausa, modo espectador y cámaras comprobados en logs y volcado de
  fotogramas.

Limitaciones actuales que la UI no debe ocultar:

- nada probado todavía en un Windows real, un Android real ni un router
  real (`nectar_net_publish_probe` preparado para ello);
- en Android, `/proc/net/route` está vetado: falta pasar el gateway desde Java
  (UPnP funciona sin él);
- sólo se replican los capitanes: Pikmin, objetos, enemigos, cinemáticas,
  menús y transiciones del día siguen siendo locales en el invitado (una
  cinemática disparada a destiempo provocó correcciones bruscas que se
  recuperaron solas);
- el launcher aún no tiene las pantallas Crear/Unirse; el host necesita
  `--online-code-out`;
- MITM activo en la ruta durante el handshake: riesgo aceptado (ADR-0002);
- Pikmin 2 todavía no usa `nectar_net`.

Open Nectar ya tiene casi toda la capa de juego necesaria para dos participantes locales:

- Pikmin 1 crea dos `Navi`, actualiza dos controladores y presenta dos cámaras/HUD sobre un único mundo.
- Pikmin 2 conserva la arquitectura original de dos capitanes y dos controladores; el cooperativo de historia añadido al port reutiliza `BaseGameSection::setPlayerMode(NAVIID_Multiplayer)` y el soporte del modo VS.
- Las tres plataformas producen el mismo formato lógico de mando (`PADStatus`) antes de que los controladores del juego consuman la entrada.
- Ambos juegos ya distinguen el tick lógico de la frecuencia de presentación, permitiendo dibujar a 30, 60 o 120 FPS sin convertir la frecuencia gráfica en protocolo de red.

La solución recomendada no es lockstep puro ni rollback completo. El host será la única autoridad sobre el mundo: IA, física, RNG, daño, creación/destrucción de entidades, transiciones y guardado. El invitado enviará inputs compactos, predecirá únicamente la respuesta inmediata de su capitán y representará snapshots autoritativos del host. Esto evita exigir resultados matemáticamente idénticos entre x86-64 y ARM64.

La experiencia prevista será:

1. El host elige **Online > Crear partida**.
2. El juego intenta publicar un puerto UDP con PCP, NAT-PMP o UPnP IGD; si el host ya tiene IPv6 global, también prueba esa ruta.
3. El juego muestra un código autocontenido para copiar o enseñar como QR. El código incluye el endpoint público y material de autenticación efímero; no consulta ningún servidor.
4. El invitado elige **Online > Unirse**, pega o escanea el código y conecta directamente con el host.
5. Ambos realizan un handshake cifrado y validan versión, juego, región, protocolo, reglas y contenido.
6. El host carga la partida y ambos entran al mismo mundo.

Si la apertura automática no funciona, la interfaz pedirá al host un puerto reenviado manualmente y su dirección pública. En CGNAT, NAT simétrico o muchas redes móviles puede ser imposible alojar una partida sin un relay; este diseño acepta deliberadamente esa limitación para no depender de infraestructura externa ni tener coste operativo.

No existe una forma honesta de garantizar «cero lag» en Internet. Este diseño sí debe garantizar:

- que la entrada local se perciba inmediatamente mediante predicción;
- que el estado definitivo siempre sea el del host;
- que una pérdida de paquetes no congele la simulación;
- que cualquier divergencia se detecte, mida y repare;
- que nunca se escriba en el guardado del invitado como consecuencia de una sesión remota;
- que un paquete malicioso no pueda convertirse en una escritura arbitraria de memoria ni en una orden directa sobre el mundo.

## 2. Objetivos, garantías y exclusiones

### 2.1 Objetivos funcionales

- Cooperativo online de dos jugadores para Pikmin 1.
- Cooperativo online de historia para Pikmin 2.
- Extensión posterior al VS de Pikmin 1, Batalla 2P y Desafío 2P de Pikmin 2 sin cambiar el transporte.
- Cualquier combinación Windows/Linux/Android.
- Crear/unirse mediante un código autocontenido, sin cuentas, señalización ni servidores del proyecto.
- Apertura automática del puerto mediante PCP, NAT-PMP o UPnP cuando el router lo admita, con instrucciones de reenvío manual como alternativa.
- Coste operativo de infraestructura de red igual a cero: no hay STUN, TURN, matchmaking ni servicio de salas.
- Reconexión del invitado durante una ventana breve sin perder la partida del host.
- Host capaz de seguir jugando si el invitado abandona.
- El host elige nivel, slot/save, reglas, mejoras de gameplay, cheats permitidos y límite global de Pikmin; el invitado usa esa configuración sólo durante la sesión.
- P2 puede dejar temporalmente el control, pasar a espectador del host y reincorporarse con una acción explícita sin reconectar.
- Personajes personalizados compatibles pueden transferirse entre peers a una caché aislada, con consentimiento, límites y validación estricta.
- Diagnóstico visible de RTT, pérdida, jitter, dirección usada, método de apertura del puerto y versión de protocolo.

### 2.2 Garantías de consistencia

- Sólo el host decide los resultados del mundo.
- Todas las entidades replicadas tienen una identidad de red estable; nunca se identifica una entidad mediante un puntero.
- Los mensajes son versionados, acotados y validados antes de tocar objetos del juego.
- Los eventos fiables son idempotentes y llevan secuencia; repetirlos no cambia dos veces el mundo.
- Los snapshots llevan tick, época de escena y hash del estado crítico.
- Una divergencia nunca se ignora: produce corrección, resnapshot o desconexión explicada.
- El cliente no puede escribir el save canónico ni concederse objetos, daño, sprays, piezas o tesoros.

### 2.3 Exclusiones iniciales

- Más de dos jugadores.
- Migración de host en la primera versión.
- Espectadores adicionales o conexiones de terceros; el modo espectador de P2 sigue ocupando la única plaza de invitado.
- Matchmaking público, cuentas o lista de amigos.
- Relay de tráfico y garantía de que cualquier usuario pueda alojar desde CGNAT, NAT simétrico o una red que bloquee UDP entrante.
- Ocultar la IP pública entre los dos participantes; una conexión directa la revela necesariamente al otro peer.
- Voz integrada.
- Entrada arbitraria en mitad de cualquier combate durante la primera entrega. La unión inicial se hará desde un lobby; la entrada en curso se añadirá primero en puntos seguros.
- Compatibilidad entre builds cuyo protocolo o contenido crítico no coincida.
- Rollback completo de todo el mundo.

## 3. Auditoría de la base de código actual

### 3.1 Estructura general

Los juegos son dos ejecutables nativos distintos con capas de host similares:

- `games/pikmin1/CMakeLists.txt`: Pikmin 1, C++17, Windows/Linux/Android, SDL2 y `pc_port`.
- `games/pikmin2/CMakeLists.txt`: Pikmin 2, C++17, decompilación vendorizada en `pikmin2-decomp`, capa `pc_port` y targets diferenciados por región.
- `games/pikmin1/pc_port` y `games/pikmin2/pc_port`: ventana, entrada SDL, audio, renderer, ajustes, Android y extensiones nativas.
- `libs`: código ya compartido a nivel de repositorio. Es la ubicación apropiada para una librería de red común.

El nuevo transporte no debe duplicarse dentro de cada juego. Debe vivir en `libs/nectar_net` y exponer una interfaz pequeña y sin tipos del motor. Cada juego tendrá un adaptador propio.

### 3.2 Pikmin 1: puntos de integración confirmados

#### Entrada

`games/pikmin1/pc_port/pc_window.cpp` mantiene dos controladores SDL y rellena `PADStatus pad[0]` y `pad[1]` en `pc_window_poll_events`. Después, `games/pikmin1/pc_port/dolphin_stubs/pad_stubs.cpp` entrega ese estado al juego mediante `PADRead`.

El punto correcto para integrar la red está entre la captura local y la publicación a `PADRead`:

```text
SDL/táctil/teclado
        ↓
PcLocalInputFrame
        ↓
NetInputRouter (local o remoto según rol)
        ↓
PADStatus[0..1]
        ↓
Controller::update
```

No se enviará `PADStatus` en bruto porque puede contener padding y representación dependiente del compilador. Se traducirá a `NetInputFrame` con campos de tamaño explícito.

#### Cooperativo y bucle de juego

- `games/pikmin1/pc_port/pc_coop.cpp/.h` sólo conserva selección y estado local de la sesión.
- `games/pikmin1/src/plugPikiColin/newPikiGame.cpp` crea y actualiza el controlador de P2.
- `games/pikmin1/src/plugPikiKando/gameCoreSection.cpp` crea `mNavi2`, actualiza los managers del mundo y gestiona cámaras/HUD cooperativos.
- `GameCoreSection::updateAI` contiene la secuencia efectiva de actualización de `naviMgr`, `pikiMgr`, `itemMgr`, `workObjectMgr`, `plantMgr`, `tekiMgr`, `pelletMgr` y bosses.
- El render de la segunda vista ya evita avanzar ciertos sistemas dos veces. Es un precedente útil para separar simulación de presentación.
- El cooperativo local ya mantiene un grupo de Pikmin controlable por cada capitán y un límite global compartido. El online debe conservar esta semántica existente, no volver a implementarla; sólo replicará pertenencia, transferencias y conteos como estado autoritativo.

`GameCoreSection::updateAI` será el punto de barrera de tick:

1. consumir input del tick;
2. aplicar mensajes fiables pendientes;
3. simular en el host;
4. capturar snapshot/delta después del update;
5. en el cliente, aplicar estado autoritativo antes de renderizar.

#### Entidades

Pikmin 1 utiliza managers con operaciones `birth`, `createObject`, `update` y `kill`. Los primeros hooks necesarios son:

- `NaviMgr`;
- `PikiMgr`;
- `TekiMgr` y managers de bosses;
- `ItemMgr`, `PikiHeadMgr`, pellets y work objects;
- generadores de mapa y objetos persistentes.

Cada nacimiento autoritativo recibirá un `NetEntityId`; cada muerte generará un evento fiable antes de liberar/reutilizar el slot del manager.

### 3.3 Pikmin 2: puntos de integración confirmados

#### Entrada

`games/pikmin2/pc_port/pc_window.cpp` y `dolphin_stubs/pad_stubs.cpp` presentan el mismo punto de inyección de `PADStatus`. La librería común podrá reutilizar exactamente el mismo `NetInputFrame`.

#### Cooperativo de historia

`games/pikmin2/pc_port/pc_p2_coop.cpp/.h` mantiene el estado de sesión. La activación real ocurre después de `DEMO_First_Use_Louie`; antes de esa cinemática el tutorial continúa con un único capitán.

La integración actual ya toca los puntos importantes:

- `pikmin2-decomp/src/plugProjectKandoU/baseGameSection.cpp` asigna `mControllerP1` y `mControllerP2`, cambia entre un jugador y `NAVIID_Multiplayer` y gestiona el propietario de los menús.
- `singleGameSection.cpp`, `singleGS_MainGame.cpp` y `singleGS_CaveGame.cpp` controlan superficie, cuevas y transiciones.
- `moviePlayer.cpp` fuerza cinemáticas a pantalla completa y restaura después la vista cooperativa.
- `GameSystem::startFrame` mantiene el reloj lógico original de 30 Hz mediante `PC_ORIG_TICK()`.
- `NaviMgr`, `PikiMgr`, enemigos, pellets, `ItemOnyon` y managers de cuevas siguen la arquitectura `ObjectMgr/MonoObjectMgr` del juego.

Pikmin 2 ofrece una ventaja: su modo VS original ya contempla dos controladores, dos cámaras y dos capitanes. La red no debe crear un tercer modo de control; debe sustituir la fuente de `mControllerP2` y conservar las rutas probadas de `BaseGameSection`.

#### Complejidad específica de Pikmin 2

- Generación procedural de cuevas y semillas.
- Transiciones frecuentes entre pisos con descarga/carga de managers.
- Sprays, tesoros, cadáveres transportables y objetos que pueden cambiar de propietario.
- Estados de Olimar/Louie, caída de un capitán y continuación con el otro.
- Modos historia, cueva, desafío y batalla con máquinas de estados diferentes.
- Mayor número de clases de enemigo y estados específicos no expresables sólo con posición/salud.

Por ello, Pikmin 2 reutilizará transporte y protocolo, pero tendrá un esquema de replicación distinto (`P2WorldAdapter`) y se incorporará después de estabilizar la infraestructura con Pikmin 1.

### 3.4 Riesgos de determinismo encontrados

Ambos árboles contienen numerosas llamadas globales a `randFloat`, temporizadores en coma flotante, decisiones de IA dependientes del orden de actualización y código condicional por plataforma/versión. Además:

- Windows/Linux usan x86-64 y Android ARM64.
- El juego puede presentar a 30, 60 o 120 FPS.
- Los managers reutilizan objetos y direcciones.
- El audio y otras tareas pueden ejecutarse de forma cooperativa o concurrente según el host.

Esto descarta como garantía principal «ambos simulan desde la misma semilla y esperamos que coincidan». Una semilla común sirve para generación inicial, pero no basta para mantener el mundo idéntico durante horas.

## 4. Decisión de arquitectura

### 4.1 Alternativas evaluadas

#### Lockstep con retraso de entrada

Ambos equipos reciben los inputs de un tick y sólo entonces lo simulan. Tiene poco ancho de banda y estado idéntico si el motor es determinista. Se rechaza como solución de producción porque:

- añade al control al menos el RTT/redondeo de tick;
- cualquier diferencia ARM/x86, RNG u orden de managers rompe la partida;
- recuperar una divergencia requiere igualmente snapshots completos.

Se conservará un modo lockstep de diagnóstico para medir determinismo, no para usuarios.

#### Rollback completo

Requiere guardar/restaurar el mundo cada tick y reejecutarlo. Es adecuado para mundos pequeños y deterministas, pero aquí implica serializar managers, punteros cruzados, IA, rutas, animaciones, partículas y audio. Se rechaza para la primera versión.

#### Vídeo remoto más input

El host podría renderizar la vista de P2 y transmitir vídeo/audio. Garantiza un solo mundo, pero añade codificación de vídeo, consumo alto, calidad variable, APIs de hardware diferentes y una experiencia que depende completamente del ancho de banda. Puede existir como fallback futuro, pero no será el modo principal.

#### Host autoritativo con replicación

Seleccionado. El host ejecuta el mundo canónico; el invitado ejecuta presentación, predicción limitada y reconciliación. Es el único modelo que combina seguridad, compatibilidad cruzada y reparación de estado sin hacer determinista toda la decompilación.

### 4.2 Roles

```text
Host
  - ejecuta toda la simulación autoritativa
  - valida inputs y peticiones
  - asigna IDs de entidades
  - emite eventos, deltas, snapshots y hashes
  - escribe el único guardado válido

Invitado
  - captura y envía su input
  - predice su capitán y cursor
  - interpola entidades remotas
  - reproduce eventos visuales/sonoros
  - nunca decide consecuencias de juego
  - nunca escribe progreso de historia

Capa local de conexión
  - abre y renueva el mapping del puerto cuando el router lo permite
  - genera y decodifica el código autocontenido
  - autentica el peer antes de aceptar datos de juego
  - no depende de procesos o servicios externos
```

### 4.3 Separación simulación/presentación en el invitado

Éste es el trabajo técnico más importante.

No basta con sobrescribir posiciones mientras la IA local sigue creando, matando o cambiando entidades. Se introducirá un `NetWorldRole`:

- `Offline`: comportamiento actual sin coste relevante.
- `Authority`: host online; simulación normal más captura.
- `Replica`: invitado; no toma decisiones autoritativas.
- `Replay`: pruebas deterministas desde un registro de red.

En modo `Replica`:

- enemigos, Pikmin, objetos, contadores y RNG no ejecutarán mutaciones autoritativas;
- se mantendrán actualizaciones visuales necesarias para animaciones, matrices, efectos y audio;
- el capitán local podrá ejecutar una predicción de movimiento acotada, sin aplicar daño, recoger objetos ni cambiar propietarios;
- cualquier interacción se expresará como input/petición y sólo se materializará al recibir la decisión del host.

Para evitar una modificación masiva sin control, la separación se hará manager por manager. Cada manager tendrá una prueba que asegure que `Replica` no genera eventos de autoridad.

## 5. Estructura actual y propuesta del repositorio

```text
libs/nectar_net/
  CMakeLists.txt
  include/nectar_net/
    byte_io.h
    connection_code.h       # implementado
    gns_transport.h         # implementado, build opcional
    secure_random.h         # implementado
    protocol.h
    transport.h
    limits.h
    # Pendiente:
    session.h
    input.h
    entity_id.h
    snapshot.h
    clock_sync.h
    metrics.h
    port_mapper.h
  src/
    byte_io.cpp
    protocol.cpp
    connection_code.cpp     # implementado
    gns_transport.cpp       # implementado
    secure_random.cpp       # implementado
    # Pendiente:
    session.cpp
    snapshot.cpp
    clock_sync.cpp
    metrics.cpp
    port_mapper.cpp
  tests/
    protocol_test.cpp
    connection_code_test.cpp
    gns_transport_test.cpp
    # Pendiente:
    parser_fuzz.cpp
    fake_transport_test.cpp
    snapshot_test.cpp

games/pikmin1/pc_port/netplay/       # lockstep actual; migración incremental
  p1_net_adapter.cpp/.h
  p1_net_entities.cpp/.h
  p1_net_snapshot.cpp/.h
  p1_net_visual.cpp/.h
  p1_net_test.cpp

games/pikmin2/pc_port/net/           # adaptador autoritativo pendiente
  p2_net_adapter.cpp/.h
  p2_net_entities.cpp/.h
  p2_net_snapshot.cpp/.h
  p2_net_visual.cpp/.h
  p2_net_test.cpp

docs/netplay/
  PROTOCOL.md
  DIRECT_CONNECTION.md
  SECURITY.md
  TEST_MATRIX.md
```

`nectar_net` no incluirá cabeceras de Pikmin. Los adaptadores convierten el
motor a tipos estables de red. Esto permite probar parsers, transporte y
snapshots sin ROM ni assets. No se moverá ni reescribirá masivamente el
directorio `pc_port/netplay` existente: sus piezas útiles se envolverán o
migrarán por unidades con pruebas, manteniendo el lockstep como referencia.

## 6. Transporte y conexión mediante código

### 6.1 Biblioteca de transporte

Se ha seleccionado provisionalmente **GameNetworkingSockets** en el commit
`d534c19aa760df3fb75fd20db13ba1932b8a5463`, con protobuf v21.12
(`f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c`) para el spike. Funciona sobre
UDP, ofrece mensajes fiables y no fiables, fragmentación, control de congestión
y cifrado, y no exige Steam. `nectar_net` ya lo oculta detrás de `ITransport`
mediante `GnsTransport`.

El adaptador Linux funciona, pero la selección no será definitiva hasta cerrar
vendorizado y cross-build. La prueba actual usó OpenSSL 3.5.5 del sistema; los
paquetes finales no podrán depender de una versión instalada globalmente ni de
DLL/SO ausentes.

Advertencia de seguridad: el modo IP abierto de GNS cifra contra escucha
casual, pero la propia API advierte que sin certificados o secreto compartido
no autentica al peer y es vulnerable a MITM. Por tanto, **Connected de GNS no
equivale a Authenticated de Nectar**. Decisión
([ADR-0002](docs/netplay/ADR-0002-SESSION-AUTHENTICATION.md)): la conexión
queda en cuarentena hasta que el invitado presenta el `join_secret` del código
y el host lo valida en tiempo constante. Esto impide que un desconocido entre,
pero no un MITM activo situado en la red de uno de los jugadores; para un juego
entre amigos ese riesgo residual se acepta y queda documentado. Noise con PSK
y clave del host (o certificado GNS fijado) queda como mejora futura.

Antes de fijarla definitivamente se exige un spike que demuestre:

- build nativo Linux GCC/Clang;
- cross-build MinGW usado por los paquetes Windows;
- Android NDK 28 arm64;
- conexión IPv4 e IPv6 directa entre las tres plataformas;
- entrega fiable/no fiable y comportamiento bajo pérdida, reordenación y cambio de red;
- binario/release sin DLLs inesperadas;
- cierre limpio y reconexión sin fugas bajo ASan/TSan cuando corresponda;
- revisión de licencia BSD-3-Clause, dependencias transitivas y atribuciones.

Si el spike de Android o MinGW falla, se evaluará otra biblioteca de transporte
UDP mantenida y auditada. No se implementarán criptografía, fiabilidad ni
control de congestión caseros. La dependencia final debe tener commits,
checksums, licencias, parches locales mínimos y procedimiento de actualización
registrados en `third_party/NETPLAY_VENDOR.md`.

Referencia: [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets).

### 6.2 Publicación directa del host

El host abrirá un puerto UDP aleatorio alto y buscará una ruta alcanzable, sin consultar servicios de Internet:

1. dirección IPv6 global utilizable, si existe y el firewall permite el puerto;
2. PCP, preferido por ser el protocolo moderno de control de mappings;
3. NAT-PMP;
4. UPnP IGD mediante MiniUPnPc;
5. reenvío manual de puerto y dirección pública introducida por el usuario.

PCP y NAT-PMP son protocolos entre el cliente y su gateway; UPnP IGD también se descubre sólo en la red local. El router puede informar del endpoint público al crear el mapping. Si devuelve una dirección privada, compartida (`100.64.0.0/10`) o incoherente, la UI advertirá de probable CGNAT y no afirmará que la sala es accesible.

Referencias: [RFC 6887: PCP](https://www.rfc-editor.org/rfc/rfc6887.html), [RFC 6886: NAT-PMP](https://www.rfc-editor.org/rfc/rfc6886.html) y [MiniUPnP](https://github.com/miniupnp/miniupnp).

### 6.3 Código autocontenido

Sin servidor, el código no puede ser un identificador corto que se resuelva remotamente. Será una representación Base32 con separadores y checksum que contenga, como mínimo:

```text
format_version
endpoint_count
endpoints[]          # familia, dirección IPv4/IPv6, puerto y prioridad
session_nonce
join_secret          # aleatorio, al menos 128 bits
host_identity[32]    # provisional: clave pública o fingerprint verificable
protocol_major
flags
checksum
```

El códec provisional `NECTAR1` ya implementa límite de 384 caracteres, Base32
Crockford agrupado, CRC32, hasta cuatro endpoints IPv4/IPv6, caducidad, nonce,
secreto de 128 bits e identidad de host de 32 bytes. El CRC sólo protege contra
errores de transcripción. Antes de congelar el formato se decidirá si esos 32
bytes son la clave pública completa del host o un fingerprint verificable por
el transporte; si cambia su semántica se incrementará la versión del formato.

El código podrá incluir un endpoint público y, opcionalmente, uno LAN para que
el mismo flujo funcione dentro de la red local. Será sensiblemente más largo
que `NECTAR-K7M4-Q9DX`, especialmente con IPv6. La interfaz ofrecerá **Copiar**,
**Pegar** y **Mostrar/Escanear QR** para que la longitud no afecte al uso normal.
También habrá un modo avanzado `IP:puerto + secreto` para diagnóstico. El modo
normal siempre usa **un único código generado por el host**; la contestación
del invitado viaja automáticamente por la conexión directa. Un segundo código
sólo podrá existir como fallback avanzado explícito y nunca será el flujo
principal.

Propiedades obligatorias:

- se genera un secreto nuevo en cada sala y nunca se escribe en logs;
- el host deja de aceptar nuevos handshakes tras entrar el invitado;
- cerrar/recrear la sala invalida el código localmente y elimina el mapping;
- la reanudación usa un secreto distinto, acordado tras autenticar la conexión inicial;
- el host valida el secreto antes de procesar cualquier mensaje de juego; el MITM activo en ruta es un riesgo aceptado (ADR-0002);
- el checksum sólo detecta errores de escritura; no sustituye autenticación criptográfica.

### 6.4 Ciclo de vida del mapping

- Solicitar exclusivamente un puerto UDP aleatorio alto y una concesión corta.
- Renovar la concesión antes de expirar y detectar cambio de endpoint.
- Si cambia el endpoint antes de conectar, regenerar el código; durante una partida, intentar reconexión y explicar el fallo si el código antiguo ya no llega al host.
- Eliminar el mapping al cerrar la sala o salir limpiamente; tolerar que un cierre abrupto lo deje hasta expirar.
- No habilitar DMZ, no cambiar otras reglas y no reutilizar mappings ajenos.
- Mostrar el método usado (`IPv6`, `PCP`, `NAT-PMP`, `UPnP` o `Manual`) y una prueba de escucha local. Sin un observador externo no se puede certificar desde el propio host que el puerto sea accesible desde Internet.

### 6.5 Canales lógicos sobre UDP

| Canal | Entrega | Uso |
|---|---|---|
| `control` | fiable y ordenada | handshake autenticado, escenas, pausas, carga, eventos críticos, resnapshot |
| `input` | no ordenada, 0 retransmisiones | inputs con redundancia de ticks anteriores |
| `state` | no ordenada, 0 retransmisiones | deltas y snapshots frecuentes reemplazables |
| `bulk` | fiable y ordenada | snapshot completo fragmentado y paquetes de personaje aprobados, sólo en lobby o con mundo pausado/cargando |

Los mensajes no fiables se mantendrán por debajo de aproximadamente 1.100 bytes de payload para evitar fragmentación IP. Si un delta no cabe, se divide por grupos autocontenidos o se aplaza lo no prioritario.

### 6.6 Fallos esperables y mensajes de interfaz

- **No se pudo abrir el puerto automáticamente:** explicar el reenvío manual para UDP y mostrar el puerto exacto.
- **Probable CGNAT:** el usuario puede unirse a otra persona, pero normalmente no alojar; sugerir que aloje quien tenga IPv6 global o una IPv4 pública.
- **Firewall local:** ofrecer instrucciones específicas de Windows/Linux/Android, sin intentar modificar reglas del sistema silenciosamente.
- **Timeout directo:** distinguir código mal copiado, endpoint caducado, host cerrado y bloqueo de red cuando haya evidencia; en caso contrario mostrar un error neutral.
- **Red móvil:** advertir que el operador suele usar CGNAT y que cambiar entre Wi-Fi/datos invalida el endpoint.

### 6.7 Coste e infraestructura

El proyecto no desplegará ni mantendrá servidores de salas, señalización, STUN o TURN. El coste operativo del sistema será **0 €** para el proyecto; todo el tráfico de juego viaja directamente entre los dos jugadores. Las únicas piezas adicionales son bibliotecas integradas en el ejecutable. Esto elimina facturas y dependencia operativa, a cambio de no poder garantizar hosting desde todas las conexiones.

## 7. Protocolo binario

### 7.1 Principios

- Nunca serializar una estructura C++ con `memcpy`.
- Enteros de ancho explícito (`uint8_t`, `uint16_t`, etc.).
- Orden de bytes de red definido.
- Flotantes autoritativos cuantizados cuando sea viable.
- Si se transmite IEEE-754, validar `isfinite`, rango y representación.
- Longitud antes de cada bloque variable y límites globales antes de reservar memoria.
- Versión mayor incompatible; versión menor negociable mediante capabilities.
- Parseo completo en un objeto inmutable antes de aplicarlo al motor.

### 7.2 Cabecera lógica

```text
magic             u32   "NCTR"
protocol_major    u16
protocol_minor    u16
message_type      u8
flags             u8
channel_epoch     u16
session_id        u64
scene_epoch       u32
sequence          u32
ack_sequence      u32
simulation_tick   u64
payload_length    u16
payload           bytes[payload_length]
```

Una vez completado el handshake autenticado y vinculado a `NECTAR1`, el canal
aportará confidencialidad, autenticidad e integridad criptográfica. Hasta
entonces el prototipo GNS sólo ofrece cifrado anónimo y no puede transportar
gameplay público. Un hash XXH3 opcional del payload se utilizará únicamente
para detectar errores de implementación en capturas/replays, nunca como
mecanismo de seguridad.

### 7.3 Handshake de compatibilidad

`ClientHello`/`HostHello` incluirán:

- versión del protocolo;
- juego: Pikmin 1 o Pikmin 2;
- commit/build ID;
- plataforma y arquitectura sólo para diagnóstico;
- región/versión compilada del juego;
- hash del manifiesto de assets que afectan jugabilidad;
- hash de mods/reglas relevantes;
- modo solicitado;
- configuración autoritativa de gameplay propuesta por el host;
- nivel/curso seleccionado y slot de guardado del host;
- manifiesto de personajes personalizados requeridos, con tipo, tamaño y SHA-256;
- capacidades: compresión, esquemas de entidad y reconexión;
- nonce aleatorio de cada extremo.

La región visual/idioma puede diferir si los datos de juego son compatibles. Si el ejecutable contiene lógica condicional distinta por región, la primera versión exigirá coincidencia exacta hasta que las pruebas demuestren compatibilidad.

### 7.4 Input

```text
NetInputFrame
  tick             u64
  buttons_held     u16
  buttons_pressed  u16
  buttons_released u16
  stick_x/y        i8, i8
  cstick_x/y       i8, i8
  trigger_l/r      u8, u8
  cursor_dx/dy     i16, i16   (si el modo lo necesita)
  flags            u8
```

Cada datagrama incluirá el tick actual y los 3–5 frames anteriores. Así, una pérdida aislada se repara sin retransmisión ni bloqueo. El host:

- ignora ticks demasiado antiguos o futuros;
- limita frecuencia y tamaño;
- normaliza valores;
- reconstruye `pressed/released` a partir de `held` cuando detecte inconsistencia;
- mantiene el último estado continuo durante un margen pequeño;
- pone input neutro y muestra desconexión si el margen expira.

### 7.5 Identidad de entidades

`NetEntityId` será un `uint64_t` compuesto por:

```text
scene_epoch : 16 bits
kind        : 8 bits
generation  : 8 bits
counter     : 32 bits
```

- `scene_epoch` cambia al cargar día, nivel o piso de cueva.
- `kind` distingue capitán, Pikmin, enemigo, pellet, item, obstáculo, etc.
- `generation` evita confundir un slot reutilizado.
- `counter` lo asigna exclusivamente el host.

Habrá dos mapas por adaptador: `pointer -> NetEntityId` sólo en el host y `NetEntityId -> pointer/proxy` en el cliente. Las referencias entre entidades siempre usan IDs y se resuelven tras crear el lote completo.

### 7.6 Eventos y snapshots

Eventos fiables:

- crear/destruir entidad;
- transición de escena/estado global;
- iniciar/finalizar cinemática;
- abrir/cerrar menú global;
- entrar/salir de cueva o cambiar de piso;
- recoger pieza/tesoro;
- cambio de propietario/grupo;
- daño/muerte importante;
- inicio/fin de día;
- cambio de regla o jugador;
- guardado confirmado.

Estado frecuente:

- transform y velocidad;
- estado/FSM y temporizador relevante;
- salud y flags;
- animación y frame autoritativo cuando afecte colisiones/eventos;
- propietario/objetivo por `NetEntityId`;
- datos compactos específicos del tipo.

Frecuencias iniciales:

- input: 30 Hz, alineado con tick lógico original;
- capitán local del invitado: confirmación 30 Hz;
- capitanes remotos y entidades cercanas: 20 Hz;
- entidades lejanas/estables: 5–10 Hz;
- hash crítico: 2 Hz;
- keyframe parcial: cada 2–5 segundos;
- snapshot completo: carga/reconexión o reparación, no durante combate normal.

Estas frecuencias serán adaptativas según RTT, pérdida y cola de envío.

## 8. Reloj, latencia y reconciliación

### 8.1 Tick canónico

La red usará un tick de 30 Hz independiente de la frecuencia de render. Es coherente con el reloj original y con `PC_ORIG_TICK()` ya utilizado en Pikmin 2. Un jugador puede renderizar a 120 FPS y otro a 30 FPS sin cambiar la secuencia de red.

El host emite `host_tick` y tiempo monotónico. El cliente estima:

- offset de reloj;
- RTT mínimo y suavizado;
- jitter;
- tick de presentación objetivo.

Nunca se usará el reloj del sistema ni la hora civil para ordenar gameplay.

### 8.2 Predicción del invitado

El invitado guarda una cola circular de inputs todavía no confirmados. Al recibir el estado autoritativo de su capitán:

1. restaura el estado confirmado;
2. descarta inputs reconocidos;
3. reaplica sólo la parte predictiva segura de los inputs restantes;
4. compara posición/orientación;
5. corrige visualmente.

Umbrales iniciales:

- error pequeño: suavizar en 100–150 ms;
- error mediano: corrección acelerada en menos de 250 ms;
- error imposible/teletransporte: snap inmediato con métrica y razón;
- interacción discreta: nunca predecir la consecuencia; sólo feedback visual provisional.

Silbato, lanzamiento, recogida, daño y muerte requieren confirmación. La animación o sonido inicial puede reproducirse como predicción y cancelarse si el host rechaza la acción.

### 8.3 Interpolación

Entidades no controladas localmente se muestran a un tick ligeramente retrasado. El búfer objetivo será dinámico:

```text
buffer = clamp(60 ms + 2 * jitter, 60 ms, 150 ms)
```

Se interpola posición/orientación y se extrapola sólo durante una ventana corta. Agotada esa ventana, se mantiene la última pose en vez de continuar atravesando paredes.

### 8.4 Backpressure y degradación

Si crece la cola de red:

1. descartar snapshots antiguos reemplazables;
2. reducir frecuencia de entidades lejanas;
3. mantener inputs, eventos críticos y estado de capitanes;
4. solicitar keyframe al recuperarse;
5. desconectar explicando «conexión insuficiente» antes de agotar memoria.

Nunca se acumularán segundos de snapshots obsoletos.

## 9. Replicación de mundo

### 9.1 Clasificación

#### Autoritativo y replicado

- capitanes;
- Pikmin y Pikmin plantados;
- enemigos y bosses;
- pellets, piezas, tesoros y cadáveres;
- cebollas/nave/pod;
- puertas, muros, puentes, geysers, agujeros y objetos interactivos;
- contadores de grupo, inventario y progreso;
- reloj del día y estado de escena;
- RNG que afecte gameplay.

#### Derivado localmente

- cámara personal;
- UI y layout;
- partículas no interactivas;
- decals y efectos puramente visuales;
- mezcla de audio;
- animaciones que no disparen gameplay, siempre corregibles por el estado autoritativo.

#### Estático

- geometría del mapa;
- rutas y datos extraídos del disco;
- parámetros idénticos verificados por hash.

### 9.2 Esquemas por entidad

No se intentará diseñar un único blob para todas las clases. Cada tipo tendrá:

- `spawn schema` completo;
- `delta schema` con máscara de campos;
- validación de rangos;
- versión de esquema;
- función `capture`, `apply` y `hashCritical`;
- máximo documentado de bytes.

El registro rechazará un `kind` desconocido salvo que haya sido negociado como extensión opcional.

### 9.3 FSM y estados específicos

Posición/salud no bastan. Un enemigo puede estar tragando, atacando, oculto o muriendo; un Pikmin puede estar agarrado, transportando o lanzado. Para cada familia se auditará:

- ID de estado;
- temporizador/animación que dispara transiciones;
- objetivo y referencias;
- flags de colisión;
- posesión/transporte;
- variables específicas que alteran el siguiente resultado.

Hasta completar el esquema de una familia, esa familia no se declara compatible online. No se ocultarán entidades sin soporte: el handshake del nivel debe fallar con una explicación de desarrollo.

### 9.4 Hash de estado

El hash no se calcula sobre memoria cruda. Se construye con campos normalizados y orden estable:

1. estado global;
2. entidades ordenadas por `NetEntityId`;
3. enteros en orden de red;
4. flotantes cuantizados;
5. referencias como IDs.

El host compara el hash comunicado por el cliente sólo como diagnóstico/alarma. El hash del host es la referencia. Tras varios desacuerdos:

- enviar corrección parcial si se identifica el grupo;
- si persiste, pausar en un punto seguro y enviar snapshot completo;
- si no puede aplicarse, desconectar conservando la partida del host y generar informe.

## 10. Plan específico de Pikmin 1

### 10.1 Primera vertical funcional

La primera demo debe usar un mapa controlado y sólo:

- dos capitanes;
- movimiento y cámara;
- input remoto inyectado en P2;
- host Linux y cliente Linux en procesos distintos;
- estado del capitán confirmado y reconciliado;
- simulador de RTT/pérdida.

No se añaden Pikmin/enemigos hasta que captura, tick, reconexión y errores sean observables.

### 10.1.1 Diseño de la primera vertical (14-10-2026)

Modo nuevo `--online-host` / `--online-join <código>`, separado del lockstep
(que sigue intacto como herramienta), sobre `nectar_net` (GNS +
`GatedTransport` + `HostRoom`/`GuestJoin`):

- **Host:** ticks a 30 Hz con su propio reloj, sin esperar nunca al invitado.
  Pad 0 = input local; pad 1 = `InputReceiver` del invitado (repite el último
  estado ante pérdidas y pasa a neutro si se alarga). Tras cada tick envía el
  estado autoritativo de ambos capitanes (posición, velocidad, orientación,
  estado) por el canal no fiable, y responde a los pings.
- **Invitado:** ticks a 30 Hz alineados con `ClockEstimator`. Su input se
  aplica en el acto a su capitán (predicción) y se envía con redundancia,
  etiquetado con el tick del host en el que debe aplicarse. Guarda un
  histórico de su capitán por tick; al llegar el estado autoritativo de ese
  tick, el error se corrige de forma suave (o de golpe si es imposible). El
  capitán del host se muestra interpolado con el retardo del reloj.
- Mientras sólo se replican capitanes, el resto del mundo del invitado no es
  autoritativo: por eso la vertical usa un mapa controlado y mide sólo los
  capitanes. Pikmin, objetos y enemigos entran en la Fase 5.
- Pruebas con dos procesos reales (`run_pair.py` con `SDL_VIDEODRIVER=
  offscreen`) y degradación de red de GNS (lag/pérdida simulados), midiendo
  el error de posición del capitán a lo largo de la sesión.

### 10.2 Hooks

- Después de `pc_window_poll_events`: construir input local.
- Antes de `Controller::update`: elegir input local/remoto.
- Entrada de `GameCoreSection::updateAI`: `beginTick` y aplicación de control/eventos.
- Después de actualizar managers: `captureDelta` en autoridad.
- Antes de render: interpolación/aplicación visual en réplica.
- `birth/kill` de managers: registro de entidad.
- transiciones de `newPikiGame`/`GameCoreSection`: incrementar `scene_epoch` y ejecutar barrera de carga.
- funciones de guardado: sólo autoridad.

### 10.3 Orden de incorporación de entidades

1. Navi P1/P2.
2. Pikmin libres y en grupo.
3. Pikmin plantados/cebollas.
4. pellets y piezas.
5. objetos de construcción y obstáculos.
6. enemigos básicos de una sola FSM.
7. enemigos con agarres/proyectiles.
8. bosses.
9. cinemáticas y transiciones de día.
10. VS y randomizer.

Cada paso tendrá una lista de especies/objetos soportados, un replay y una prueba de pérdida de paquetes.

### 10.4 Reglas del cooperativo

- El host elige slot/save, nivel, modo, randomizer, mejoras de gameplay, cheats permitidos y reglas.
- P1 permanece en el host y P2 en el invitado en la primera versión.
- El invitado elige capitán/skin; el host valida disponibilidad lógica.
- Cada capitán conserva su ejército/grupo de Pikmin separado, ya implementado por el cooperativo local, mientras ambos consumen el mismo límite global autoritativo. Reclutar, despedir o transferir Pikmin actualiza propietario y conteos en un único tick del host.
- La colisión entre capitanes y cualquier empuje o interacción derivada se resuelve en el host y se replica; el invitado sólo puede predecir su respuesta visual inmediata.
- Cualquiera puede solicitar pausa; el host la concede y replica.
- El host controla salto de cinemática; puede añadirse opción «cualquiera» después.
- P2 puede elegir «Dejar cooperativo»: su input se neutraliza, pasa a ver al host y conserva su plaza. Pulsar Start solicita reincorporación en el siguiente punto seguro o tick permitido.
- Si P2 desconecta, su capitán pasa a neutral/seguro y después a follow según regla definida, sin matar la sesión; esto es distinto del modo espectador voluntario.
- Sólo el host obtiene/escribe progreso. Los logros del invitado que dependan de estado autoritativo se pueden conceder localmente mediante eventos firmados por la sesión, pero no en el primer milestone.

## 11. Plan específico de Pikmin 2

### 11.1 Reutilización de la arquitectura existente

El online debe aprovechar:

- `mControllerP1`/`mControllerP2`;
- `NaviMgr::getAt(NAVIID_Olimar/Louie)`;
- `BaseGameSection::setPlayerMode`;
- `pc_p2_coop_active/session`;
- el tratamiento existente de pausa, menús, caída de capitán, cámaras y cinemáticas.

También debe conservar la semántica local ya existente de un ejército/grupo por capitán y los límites globales del modo. La tarea de red es sincronizar propietario y conteos, no introducir un segundo sistema de ejércitos.

El adaptador de red no reasignará controladores directamente en múltiples sitios. Expondrá un proveedor de input a la capa PAD; `BaseGameSection` seguirá creyendo que tiene dos mandos normales.

### 11.2 Secuencia del tutorial

La sesión online puede crearse desde el título, pero P2 no obtiene control hasta `DEMO_First_Use_Louie`, igual que el cooperativo local actual. Durante ese tramo:

- el invitado ve la sesión/estado de espera;
- recibe progreso de carga y cinemáticas;
- puede desconectarse sin afectar el tutorial;
- al activarse Louie, el host emite `PlayerActivated` y la barrera inicia el input de P2 en un tick definido.

No se debe adelantar artificialmente el flag de Louie: alteraría save y tutorial.

### 11.3 Superficie y cuevas

Cada entrada a una cueva o cambio de piso crea un nuevo `scene_epoch`:

1. host congela gameplay;
2. envía `ScenePrepare` con curso, cueva, piso, semillas y hashes;
3. ambos cargan assets localmente;
4. invitado responde `SceneReady`;
5. host envía snapshot inicial completo;
6. invitado aplica y responde con hash;
7. host emite `SceneStart(tick)`;
8. ambos retoman presentación; sólo el host simula autoridad.

Si el invitado tarda, el host muestra «esperando al jugador» sin avanzar el reloj. Un timeout permite continuar offline.

La semilla de cueva se transmite para producir la misma geometría estática, pero el host sigue siendo autoridad sobre spawns y gameplay. El hash del layout generado debe coincidir antes de crear proxies.

### 11.4 Modos

Orden recomendado:

1. historia en superficie después de Louie;
2. historia en cuevas;
3. transiciones, resultados y guardado;
4. Desafío 2P;
5. Batalla 2P;
6. entrada en curso/reconexión dentro de un piso.

Batalla 2P debe usar las mismas garantías autoritativas. Que el juego original sea local no implica que sea determinista entre máquinas.

### 11.5 Estado adicional de Pikmin 2

El esquema global debe cubrir:

- sprays y contadores;
- dinero/deuda y tesoros;
- día, curso, cueva y piso;
- flags demo relevantes;
- salud de ambos capitanes y `mDeadNaviID`;
- Onion/pod y poblaciones por color/estado;
- piklopedia/tesoros sólo como resultado del host;
- geysers/agujeros y propietario del menú;
- estado de reto/batalla, puntuación, llaves y condición de victoria.

## 12. Menús, carga, guardado y desconexión

### 12.1 Máquina de estados de sesión

```text
Offline
  → CreatingRoom / JoiningRoom
  → PublishingEndpoint / DecodingCode
  → Connecting
  → Authenticating
  → CompatibilityCheck
  → Lobby
  → LoadingScene
  → Synchronizing
  → Playing
  ↔ Spectating
  ↔ Reconnecting
  → Closing
  → Offline
```

Cada transición tiene timeout, razón de error localizada y limpieza idempotente.

### 12.2 Pausa y menús

- Los menús que detienen el mundo son globales y decididos por host.
- El invitado puede pedir apertura; el host valida el estado y replica propietario.
- Menús puramente locales —ajustes gráficos, controles, volumen— no pausan ni se replican.
- Acciones destructivas o de progreso requieren confirmación del host.
- Durante menús globales se mantienen heartbeat, clock sync y el transporte directo.
- Selección de nivel, reinicio, cambio de escena y retorno al título son decisiones del host; el invitado recibe la transición y puede aceptar, abandonar o esperar, pero no imponerla.

### 12.3 Guardado

- Sólo el host abre/escribe la memory card canónica durante sesión online.
- El cliente bloquea llamadas de escritura de progreso y muestra «La partida se guarda en el host».
- Antes de escritura se verifica que el estado no esté en `Reconnecting` ni aplicando snapshot.
- El host conserva copias/flujo atómico existente; la red nunca transmite la imagen completa de la tarjeta.
- Los datos de disco/save privados no se envían al otro peer.
- El invitado consume una vista replicada del progreso necesario para la sesión, pero no copia ni reemplaza su propio archivo de guardado.

### 12.4 Desconexión y reconexión

Al perder conectividad:

- host neutraliza input de P2 tras una tolerancia corta;
- muestra cuenta atrás configurable, por ejemplo 15 segundos;
- el mundo puede pausarse automáticamente fuera de situaciones no pausables;
- invitado conserva `resume_secret`, `session_id`, `scene_epoch` y último tick confirmado;
- el transporte intenta restablecer el enlace con el mismo endpoint mientras siga siendo válido;
- host envía snapshot completo y reanuda en una barrera de tick.

Si expira:

- host continúa offline o vuelve a lobby;
- invitado no guarda mundo parcial;
- se invalida el secreto de reanudación y el host elimina el mapping del puerto al cerrar la sala.

### 12.5 Configuración autoritativa y restauración local

Al autenticar la sesión, el host envía un `SessionRules` versionado con únicamente opciones que afectan al gameplay: mejoras, randomizer, cheats admitidos, límite global de Pikmin, dificultad y reglas del modo. Estas opciones se aplican como una capa temporal en memoria, nunca escribiendo encima de la configuración persistente del invitado.

- Gráficos, audio, idioma, accesibilidad y controles siguen siendo locales salvo que una opción altere realmente la simulación.
- La UI muestra las diferencias antes de entrar y exige confirmación para cheats o reglas no estándar.
- Cualquier cambio del host durante la sesión requiere un mensaje fiable, validación, barrera de tick y actualización visible.
- Al abandonar, desconectar o fallar la carga se destruye la capa temporal; la configuración local reaparece automáticamente incluso tras recuperación de un cierre inesperado.
- El host es la única autoridad para nivel/curso y save. El invitado nunca recibe la imagen completa del save, sólo el estado replicado que necesita el mundo actual.

### 12.6 Espectador y reincorporación de P2

`LeaveCoopRequest` no cierra el transporte. El host confirma `PlayerControlChanged(Spectating, tick)`, neutraliza el input de P2 y decide el estado seguro de su capitán. La cámara del invitado pasa a observar P1 usando la presentación local.

Al pulsar Start, el invitado envía `RejoinRequest`. El host sólo concede control cuando el capitán existe y la escena no está en una transición, cinemática no interrumpible, muerte o estado inseguro. `PlayerControlChanged(Playing, tick)` incluye snapshot del capitán, ejército asignado y cámara para reincorporarse en una barrera de tick. El espectador sigue autenticado, recibe snapshots y ocupa la única plaza; no es un tercer jugador.

### 12.7 Personajes personalizados y caché

Los personajes personalizados son contenido no fiable. La primera versión sólo aceptará formatos declarativos ya soportados por el juego —modelo, texturas, animaciones y metadatos acotados— y rechazará ejecutables, bibliotecas nativas, shaders, scripts, rutas absolutas y referencias fuera del paquete.

Flujo:

1. cada peer anuncia el personaje elegido para su propio jugador y un manifiesto canónico con IDs, tamaños y SHA-256;
2. si falta contenido, la UI muestra origen, tamaño total y tipos de archivo y pide consentimiento explícito;
3. la transferencia usa el canal `bulk`, cuotas por archivo/paquete/sesión y una carpeta temporal sin nombres proporcionados directamente por el peer;
4. cada archivo se valida completamente antes de decodificarlo, se escribe con nombre derivado del hash y sólo después se mueve atómicamente a `online_cache`;
5. parsers de imagen/modelo se ejecutan con límites estrictos de dimensiones, vértices, huesos, animaciones y memoria; un fallo cancela el paquete sin afectar al save;
6. la caché es separada de los mods instalados, tiene límite/LRU, puede vaciarse desde opciones y nunca activa contenido fuera de una sesión compatible.

El hash garantiza que ambos usan los mismos bytes, no que el archivo sea seguro; por eso la allowlist y los límites de parseo son obligatorios. Si la transferencia automática no está disponible o el usuario la rechaza, se muestra incompatibilidad de contenido y no se inicia la partida con sustituciones silenciosas.

## 13. Seguridad

### 13.1 Modelo de amenaza

Se considera al invitado, al host y a todo datagrama entrante potencialmente maliciosos. Riesgos principales:

- filtración o reutilización del código autocontenido;
- escaneo y flood del puerto UDP público;
- amplificación de tráfico antes de autenticar al peer;
- tamaños que causen overflow o reservas enormes;
- IDs fuera de rango o referencias a entidades inexistentes;
- NaN/Inf y coordenadas extremas;
- eventos repetidos o fuera de orden;
- traversal/rutas enviadas por red;
- modificación de guardado del invitado;
- exposición de IP inherente a la conexión directa;
- logs con secretos, direcciones o datos personales;
- compresión maliciosa y bombas de descompresión;
- abuso de reconexión para secuestrar una sala.

### 13.2 Controles obligatorios del cliente

- Parser independiente del motor, probado con fuzzing.
- Longitud máxima por mensaje y por colección.
- Máximo de entidades por categoría coherente con el juego.
- Validación de enum, rango, finitud y época.
- Presupuesto por tick para mensajes y creación/destrucción.
- Colas acotadas; nunca crecer según datos remotos sin límite.
- No aceptar nombres de archivos, rutas, URLs ni comandos desde gameplay.
- No instalar mods ni cargar contenido arbitrario enviado por el peer. La única excepción es el paquete declarativo de personaje de la sección 12.7, aislado en caché y validado antes de decodificar.
- No deserializar punteros, vtables ni memoria del motor.
- Aplicar mensajes sólo en el hilo principal y en una fase conocida del tick.
- El hilo de transporte produce objetos de protocolo inmutables en una cola acotada.
- El cierre de sesión invalida callbacks antes de destruir managers.

### 13.3 Controles del endpoint directo

- No reservar estado costoso ni responder con más bytes de los recibidos antes de validar una cookie/desafío sin estado y el secreto de unión.
- Limitar intentos por dirección/prefijo, frecuencia global y conexiones simultáneas.
- Aceptar un solo invitado autenticado; ignorar otros orígenes hasta cerrar o recrear la sala.
- Separar `join_secret` y `resume_secret`, rotarlos por sala y borrarlos al terminar.
- Vincular la autenticación al transcript, endpoint lógico, versión de protocolo y fingerprint de la clave del host.
- Usar comparaciones de secreto en tiempo constante y RNG criptográfico del sistema.
- No registrar el código completo, secretos ni direcciones salvo diagnóstico explícito y redactado.
- Mantener pequeño el límite de datagrama preautenticado y presupuestos estrictos de CPU/memoria.
- Mapear únicamente el puerto UDP del proceso y eliminarlo al cerrar; nunca abrir DMZ ni tocar reglas ajenas.
- Fuzzear tanto el decodificador del código como el handshake y todos los mensajes posteriores.
- Fuzzear los manifiestos y parsers permitidos para personajes personalizados; comprobar cuotas antes de reservar, descomprimir o decodificar.
- No considerar autenticada una conexión por alcanzar el estado `Connected` de
  GNS; mantenerla en cuarentena hasta validar el `join_secret` y la versión.
- Probar secreto erróneo, mensaje previo al secreto, timeout, reintentos y
  segundo invitado: ninguno debe llegar a gameplay ni generar respuestas
  amplificadas. El MITM activo en ruta se acepta (ADR-0002).
- Borrar con una primitiva resistente a optimización el secreto de unión,
  claves de sesión y secreto de reanudación al cerrar, fallar o reemplazar la
  sala; nunca depender de `memset` ordinario para material secreto.

### 13.4 Privacidad

- La conexión directa revela las IP públicas a ambos peers. La UI debe indicarlo antes de mostrar o aceptar el código.
- No existe modo de ocultar la IP sin añadir un relay, excluido expresamente por este diseño.
- El juego no enviará direcciones, métricas ni identificadores a infraestructura del proyecto.
- No se enviarán nombres reales de dispositivo; se usarán etiquetas elegidas por el usuario y acotadas.
- Informes de error eliminarán códigos, secretos, direcciones y rutas privadas.

## 14. Rendimiento y presupuestos

Objetivos iniciales, sujetos a medición:

- memoria de red adicional: menos de 64 MiB en gameplay normal;
- input local predicho: visible en el siguiente frame;
- snapshot de capitán: 30 Hz;
- snapshot general: 10–20 Hz adaptativo;
- tráfico normal: objetivo inferior a 1 Mbit/s por sentido, techo operativo inicial 3 Mbit/s;
- paquete no fiable: máximo aproximado 1.100 bytes;
- cola de aplicación: menos de 250 ms; descartar reemplazables por encima;
- recuperación de snapshot: menos de 3 segundos en conexión razonable;
- trabajo de captura/aplicación: menos de 2 ms por tick en hardware objetivo, medido por categoría.

No se comprimirán paquetes pequeños. Snapshots completos podrán usar una compresión con límite de salida conocido y diccionario/versionado, aplicada fuera del tick crítico.

Android exige además:

- evitar wakeups innecesarios;
- suspender envío al perder Activity/red e intentar reconectar al volver;
- manejar cambio Wi-Fi/datos móviles como reconexión;
- no bloquear el hilo SDL/JNI;
- medir temperatura, batería y memoria en sesiones largas.

## 15. Observabilidad y diagnóstico

Overlay de desarrollo:

- rol y estado de sesión;
- dirección IPv4/IPv6, método de mapping y protocolo;
- RTT mínimo/promedio, jitter y pérdida;
- tick host/cliente y atraso de presentación;
- bytes/paquetes por canal;
- tamaño de colas;
- entidades creadas/actualizadas;
- correcciones por segundo y error máximo;
- hash host/cliente;
- último evento fiable y último snapshot.

Registro de red opcional:

- cabeceras y métricas, nunca secretos;
- inputs y mensajes de juego normalizados;
- posibilidad de replay offline contra un host simulado;
- versión de esquemas y build ID;
- buffer circular, escrito a disco sólo al solicitar informe o detectar fallo.

Esto permitirá reproducir desyncs sin guardar assets propietarios ni paquetes cifrados sin contexto.

## 16. Estrategia de pruebas

### 16.1 Unitarias

- byte reader/writer para todos los límites y endianess;
- golden vectors compartidos entre plataformas;
- roundtrip de cada mensaje;
- rechazo de truncados, overflow, NaN, enums inválidos e IDs obsoletos;
- wrap de secuencias/ticks;
- deduplicación de eventos;
- clocks, jitter buffer y reconciliación;
- creación/destrucción y reutilización de IDs;
- hashes estables independientemente del orden del manager.
- aplicación/destrucción de la capa temporal `SessionRules` sin modificar preferencias persistentes;
- códec y validación de manifiestos, cuotas, rutas y hashes de personajes personalizados;
- transiciones `Playing ↔ Spectating` e idempotencia de solicitudes repetidas.
- `NECTAR1`: golden vectors, mayúsculas/minúsculas, caracteres ambiguos,
  truncado, padding, checksum, límites, expiración, protocolo, endpoints y RNG.
- `GnsTransport`: segundo invitado, mensajes máximos, error/cierre, orden
  fiable, canal no fiable y ausencia de callbacks tras destruir el objeto.
- handshake: secreto erróneo, sesión antigua, código caducado/revocado,
  downgrade, mensaje previo al secreto, timeout y reintento tras rechazo.

### 16.2 Fuzzing y sanitizers

- libFuzzer/AFL sobre cada parser sin cargar el juego;
- corpus con capturas reales anonimizadas;
- ASan/UBSan para protocolo y adaptadores;
- TSan para transporte/colas;
- límites de tiempo/memoria en CI;
- fuzz de secuencia: mensajes válidos en orden hostil, no sólo bytes aleatorios.

Pikmin 2 ya tiene opciones ASan/TSan en CMake; se ampliarán a `nectar_net`. Pikmin 1 debe recibir opciones equivalentes para las nuevas targets nativas.

### 16.3 Transporte simulado

`FakeTransport` reproducirá:

- 0–300 ms de RTT;
- jitter de 0–100 ms;
- pérdida 0–20 %;
- duplicación y reordenación;
- ráfagas de pérdida;
- ancho de banda limitado;
- desconexión/reconexión;
- cambio de ruta durante sesión.

Las pruebas no dependerán de esperar tiempo real; usarán reloj inyectable.

### 16.4 Integración de dos procesos

- host y cliente headless con inputs grabados;
- comparación de estado observable y métricas;
- barreras de carga;
- muerte/recreación de entidades;
- desconexión en cada transición;
- rejoin con snapshot;
- espectador voluntario y reincorporación en barrera segura;
- restauración de opciones del invitado en cierre normal, timeout y error de carga;
- transferencia de personaje válido, rechazo por consentimiento y paquetes maliciosos/truncados;
- cierre del host/cliente en cualquier estado.

### 16.5 Matriz de plataformas

| Host | Invitado | Pikmin 1 | Pikmin 2 superficie | Pikmin 2 cuevas |
|---|---|---:|---:|---:|
| Linux | Linux | obligatorio | obligatorio | obligatorio |
| Windows | Windows | obligatorio | obligatorio | obligatorio |
| Android | Android | obligatorio final | obligatorio final | obligatorio final |
| Linux | Windows | obligatorio | obligatorio | obligatorio |
| Windows | Android | obligatorio | obligatorio | obligatorio |
| Linux | Android | obligatorio | obligatorio | obligatorio |
| Android | Windows/Linux | obligatorio final | obligatorio final | obligatorio final |

Cada pareja se prueba con IPv4 e IPv6 directos donde estén disponibles, mapping automático y reenvío manual. Android se prueba como invitado en datos móviles y como host en Wi-Fi compatible; alojar mediante datos móviles no es requisito por la prevalencia de CGNAT.

### 16.6 Escenarios de aceptación

- sesión completa de Pikmin 1 desde inicio de día hasta guardado;
- dos horas continuas con 2 % de pérdida y 80 ms RTT;
- ráfaga de 5 segundos de mala red sin corrupción;
- desconexión del invitado y continuación del host;
- reconexión en superficie y entre escenas;
- dejar cooperativo, observar al host durante varios cambios de escena y reincorporarse con Start;
- reglas/mejoras del host activas sólo durante la sesión y preferencias originales intactas al salir;
- personajes personalizados transferidos entre plataformas y eliminables desde la caché, sin aceptar scripts ni librerías;
- dos ejércitos separados, límite global compartido y transferencias de Pikmin correctas bajo pérdida/reordenación;
- todos los bosses y objetos interactivos soportados;
- Pikmin 2: tutorial hasta activación de Louie;
- entrada/salida de cada cueva, todos los pisos y geyser;
- muerte de cualquiera de los dos capitanes;
- final de día, resultados y guardado sólo en host;
- cambio Wi-Fi/datos en Android;
- cliente malicioso enviando cada mensaje inválido del corpus.

## 17. Fases de implementación y puertas de calidad

### Fase 0 — ADR, mediciones y contratos — **COMPLETADA**

Evidencia: [`docs/netplay/ADR-0001-NETPLAY-ARCHITECTURE.md`](docs/netplay/ADR-0001-NETPLAY-ARCHITECTURE.md), [`docs/netplay/PROTOCOL.md`](docs/netplay/PROTOCOL.md), [`docs/netplay/CODE_AUDIT.md`](docs/netplay/CODE_AUDIT.md) y [`docs/netplay/BASELINE.md`](docs/netplay/BASELINE.md). La línea base registra hardware, binarios, suites y metodología de captura interactiva; no inventa cifras de gameplay donde el entorno automatizado no dispone de sesión gráfica.

Entregables:

- ADR de host autoritativo y alternativas rechazadas;
- inventario de ticks, managers, escenas, RNG y guardado de ambos juegos;
- especificación `PROTOCOL.md` v0;
- benchmarks base de frame/tick/memoria;
- tabla completa de entidades por juego.

Puerta: no empezar integración de transporte hasta que estén definidos límites, roles y propiedad del save.

### Fase 1 — Spike de transporte multiplataforma — **EN CURSO (hecho en Linux y emulación; faltan Windows/Android reales)**

Progreso verificable: [`docs/netplay/TRANSPORT_SPIKE.md`](docs/netplay/TRANSPORT_SPIKE.md).

- [x] `nectar_net` mínimo, byte I/O acotado, protocolo hello/ping e interfaz `ITransport`.
- [x] Opción `NECTAR_ONLINE=OFF` por defecto integrada en los dos juegos.
- [x] Build estático de GameNetworkingSockets sin ICE y conexión UDP entre dos procesos en Linux.
- [x] adaptador `GnsTransport` funcional en Linux con conexión cifrada local,
  mensajes fiables/no fiables, límite de un invitado y cierre limpio;
- [x] documentación explícita de que el cifrado GNS anónimo no autentica al
  peer y no puede habilitar gameplay público todavía;
- [x] dependencias reproducibles: `scripts/netplay-deps.sh` descarga versiones
  fijadas con SHA-256 comprobado y compila estático GNS, protobuf y OpenSSL
  3.5.5 (BCrypt en Windows); sin `/tmp` ni paquetes globales. libsodium
  descartado: GNS exige AES por hardware con él;
- [x] build MinGW de `nectar_net` y pruebas: el `.exe` sólo importa DLL del
  sistema (`iphlpapi.dll` falta en la lista blanca de
  `scripts/release-windows.sh`: añadirla al activar online en releases);
- [ ] ejecución en Windows real (no hay Wine ni Windows en este entorno);
- [x] build Android NDK 28 arm64; las 6 pruebas y el cruce de dos procesos
  Linux↔Android pasan bajo `qemu-aarch64` (emulación, no dispositivo);
- [ ] conexión en un dispositivo Android real y entre máquinas distintas.
- [x] `ITransport::stats()`: RTT, calidad local/remota, ritmo y colas
  pendientes, sin tipos de GNS (calidad -1 hasta que GNS la mide);
- [x] Linux: `nectar_net_gns_stress_test` (2000 mensajes fiables de 1 KiB
  completos y en orden con control de cola, 30 ciclos de entrar/salir con
  cierres limpios y abruptos, destrucción del host conectado), segundo
  invitado y reconexión en `gated_gns_test`; ASan+UBSan+LSan sin errores;
  TSan sin carreras de datos (108 avisos de orden de cerrojos, todos internos
  de GNS con `try_lock_for` temporizado, que no puede interbloquear);
- [ ] las mismas pruebas en Windows y Android reales.

Entregables:

- `nectar_net` mínimo con GameNetworkingSockets;
- conexión directa mediante endpoint explícito en Linux;
- builds Windows y Android;
- canales fiable/no fiable;
- métricas RTT/pérdida y cierre seguro.

Puerta: conexión directa Linux↔Windows, Linux↔Android y Windows↔Android en una red controlada, sin fugas ni bloqueo del hilo de juego.

### Fase 2 — Publicación del host, códigos y seguridad base — **EN CURSO (falta router real, gateway en Android e interfaz del launcher)**

Progreso verificable: `libs/nectar_net/include/nectar_net/connection_code.h` y
`libs/nectar_net/tests/connection_code_test.cpp`.

- [x] códec binario provisional y Base32 legible `NECTAR1`;
- [x] límites estrictos, IPv4/IPv6, checksum, caducidad, nonce, secreto de
  unión e identidad provisional del host de 32 bytes;
- [x] generación criptográfica portátil del nonce y secreto efímero mediante
  el CSPRNG del sistema operativo, con fallo cerrado;
- [x] [`ADR-0002`](docs/netplay/ADR-0002-SESSION-AUTHENTICATION.md)
  aceptada: cifrado GNS + comprobación del `join_secret` en cuarentena; el
  MITM activo en ruta se acepta como riesgo residual; Noise queda como mejora
  futura;
- [x] cuarentena `HostJoinGate`/`GuestJoinGate` en `nectar_net`
  (`join_gate.h`): secreto y nonce de sala comparados en tiempo constante,
  sólo por canal fiable, un intento por conexión, timeout de 5 s, respuesta
  nunca mayor que la petición y borrado de secretos (`secure_memory.h`).
  Verificado en Linux 2026-10-14: `nectar_net_join_gate_test` correcto,
  también con ASan+UBSan+LeakSanitizer; sin sockets, independiente de GNS;
- [x] `GatedTransport` envuelve cualquier `ITransport`: sólo informa
  `Connected` tras validar el secreto, rechaza `send()` antes, descarta en
  silencio al peer que falla o calla (el host sigue escuchando) y cuenta los
  rechazos para diagnóstico. `ITransport::disconnectPeer()` añadido;
  `GnsTransport` vuelve a `Listening` cuando se va el invitado. Verificado en
  Linux 2026-10-14: `nectar_net_gated_transport_test` (transporte falso) y
  `nectar_net_gated_gns_test` (UDP real en loopback: secreto erróneo
  rechazado, invitado correcto, segundo invitado rechazado, gameplay y
  reconexión) correctos, también con ASan+UBSan+LeakSanitizer
  (`-fno-sanitize=vptr`: GNS se compila sin RTTI y da falso positivo);
- [x] bloqueo de sala mitigado: tres fallos (secreto erróneo, silencio,
  mensaje fuera de turno o colgar en cuarentena) desde un mismo origen lo
  vetan 30 s, doblando hasta 10 min; tabla acotada a 64 orígenes, sólo en
  memoria. Un amigo incompatible no se penaliza. Riesgo residual: muchos
  orígenes distintos (no realista para una sala entre amigos);
- [x] `NECTAR1` congelado: eliminado el campo de identidad de 32 bytes
  (ADR-0002 no lo usa), caducidad u32, Base32 canónico estricto (se rechazan
  caracteres sobrantes: fallo real encontrado al acortar el formato), vectores
  de referencia; 119 caracteres con un IPv4;
- [x] secretos borrados con `secureWipe` en las puertas y mensajes del
  handshake; comparación en tiempo constante;
- [x] comprobación de compatibilidad tras el secreto (`CompatibilityCheck`,
  integrada en `GatedTransport`): juego, región, build, contenido y
  capabilities; el invitado recibe el campo y la info del host para
  explicarlo; probada con transporte falso y UDP real;
- [x] fuzzing determinista de código, cabecera, compatibilidad y cuarentena
  (`nectar_net_decoder_fuzz_test`, 200 000 iteraciones, también con
  ASan+UBSan): ningún secreto alterado autentica y ninguna oferta
  incompatible pasa;
- [x] publicación del puerto implementada y probada con routers simulados
  (`PortPublisher`): PCP (cliente propio, RFC 6887) → NAT-PMP si el router
  responde "versión no soportada" → UPnP IGD (miniupnpc 2.3.3); IPv6 global y
  endpoint LAN opcional; clasificación `public`/`ipv6_only`/`behind_cgnat`/
  `no_automatic_mapping`; un mapping cuya dirección externa no es pública se
  retira al momento; renovación a mitad de vida con reintento a 30 s y aviso
  de cambio de endpoint; borrado al cerrar. Respuestas del router validadas
  estrictamente (tamaño, opcode, nonce, protocolo, puerto) y fuzzeadas;
- [ ] **EN CURSO (necesita al mantenedor)** prueba con routers reales:
  `nectar_net_publish_probe` abre y cierra un puerto y dice qué método
  funcionó, sin mostrar direcciones salvo `--show-addresses`;
- [ ] Android: `/proc/net/route` está vetado a las apps desde Android 10; el
  gateway lo debe pasar la capa Java (UPnP funciona sin él);
- [x] lógica de sala de un solo código para ambos juegos (`nectar_net/room.h`):
  `HostRoom` elige puerto alto aleatorio (o fijo), publica, emite el código,
  lo reemite si cambia el endpoint y al cerrar borra mapping y secreto;
  `GuestJoin` prueba los endpoints por prioridad con 6 s cada uno y devuelve
  un fallo explicable (`bad_code`, `expired`, `wrong_version`, `unreachable`,
  `refused`, `incompatible` + campo, `room_full`, `lost`). Probada con red
  simulada y con GNS real; las 13 pruebas pasan en Linux y como binarios
  Android ARM64 bajo `qemu-aarch64`; MinGW compila;
- [ ] **BLOQUEADO** pantallas Crear/Unirse en el launcher: `launcher_gui.cpp`
  y `launcher_main.cpp` tienen cambios locales ajenos sin commit;
- [x] **Crear/Unirse dentro del juego** (`pc_online_menu.cpp`): Co-op o VS en
  el título abre «Local / Online»; Online → «Create room» (Co-op: partida
  nueva o continuar archivo 1-3) o «Join with a code». El juego se reinicia
  con los parámetros online (`pc_online_relaunch.cpp`, aparte por los tipos
  Win32 del juego): el host copia el código al portapapeles
  (`--online-code-clipboard`) y el HUD avisa si hay que abrir el puerto o
  hay CGNAT; el invitado lo pega y lo pasa en un fichero 0600 que se borra
  al leerlo (`--online-code-once`). Las reglas llevan `mode=coop|vs` y el
  invitado debe entrar desde el mismo menú; el VS online usa la arena de
  serie en ambos. Android reinicia la actividad con los argumentos
  (`NectarActivity.restartWithArguments`). Un fallo al unirse vuelve al
  título con el motivo (`--online-error`);
- [x] **probado con el juego (2026-10-15), PC Linux ↔ Android:** con la capa
  propia el móvil no llegaba (sin UPnP en el router, ufw activo) y el host
  iba lento; se probó entonces el **lockstep ICE de 5laric** y funciona
  PC ↔ móvil en la misma Wi-Fi. Lo que se añadió para ello:
  - el mismo menú del título, en builds netplay sin `NECTAR_ONLINE`, crea
    sala (`--netplay-host-ice`) o se une (`--netplay-join-ice`) con
    `--netplay-dialogs`: el intercambio de códigos en diálogos del sistema
    (SDL message boxes; en Linux el código va en un campo de zenity/kdialog
    porque Wayland no deja leer/escribir el portapapeles sin foco);
  - códigos v3 `NPIX3-` (oferta 145 caracteres, antes 1207; respuesta 64,
    antes ~300): SDP empaquetado a sus campos y ajustes como valores en el
    orden de claves de la build (hash de 16 bits); v1/v2 se siguen leyendo;
  - la sesión fuerza 30 FPS (el tick es 30 Hz: con 60 FPS el juego iba a
    media velocidad), sin tocar el fichero de ajustes;
  - Android: `WifiLock` de baja latencia durante la partida (el ahorro de
    energía de la Wi-Fi retenía paquetes 100-300 ms), carpeta de sesión
    junto a los datos (`/system/bin` no es escribible), toast/diálogos;
  - `build.gradle`: `-PnectarNetplay=<source id>` (netplay sin capa propia),
    `-PnectarOnlineDeps=<prefijo>` (capa propia), `-PnectarOnlyPikmin1`.
  Pendiente: pantalla de conexión dibujada por el juego (ahora fondo negro
  detrás de los diálogos); host en Android probado; Internet (STUN opt-in).
- [x] respuesta automática del invitado por la conexión y cookie
  antiamplificación (desafío sin estado de GNS);
- [ ] `SessionRules`, `session_id` y `resume_secret` para reanudar;
- [ ] fallback manual y segundo código sólo en modo avanzado.

Entregables:

- integración PCP, NAT-PMP y UPnP IGD con concesiones cortas;
- fallback de reenvío manual e IPv6 global;
- código autocontenido con Base32, checksum, secreto efímero e identidad criptográfica del host;
- handshake autenticado, cookie antiamplificación y rate limits locales;
- limpieza/renovación del mapping y diagnósticos sin secretos;
- pantalla Crear/Unirse y errores.

Puerta: pruebas contra gateways simulados y routers reales; códigos corruptos, payloads grandes, replay, flood preautenticado, expiración y revocación no comprometen el proceso.

### Fase 3 — Protocolo, fake transport y replay — **EN CURSO (base hecha; faltan mensajes de contenido y selección de nivel)**

Progreso verificado en Linux (2026-10-14), formatos en
[`PROTOCOL.md`](docs/netplay/PROTOCOL.md#mensajes-de-juego-v1-implementados-en-nectar_net):

- [x] input v1 con redundancia de 6 ticks y `InputReceiver` (ventana,
  reconstrucción pulsar/soltar, mantenimiento 200 ms, neutro);
- [x] `Ping`/`Pong` y `ClockEstimator` (muestra de menor RTT, jitter, búfer
  de interpolación del plan);
- [x] `NetEntityId`, `WorldDelta` acotado con empaquetado autocontenido en
  ≤ 1.100 bytes y `StateHash`;
- [x] barrera de escena `ScenePrepare`/`SceneReady`/`SceneStart` con épocas
  crecientes y plazo de carga;
- [x] `SimulatedNetwork` (latencia, jitter, pérdida, duplicación,
  desorden; fiable ordenado y sin pérdida) y grabación/replay `NNRC`;
- [x] prueba de resistencia de la puerta de la Fase 4 en la parte de
  transporte: 30 min simulados a RTT 150 ms, jitter 30 ms y 5 % de pérdida a
  través de la cuarentena real: 99,82 % de ticks en vivo (lo esperable con
  ~2 copias a tiempo), 3.291/3.291 pulsaciones sin pérdidas ni dobles, reloj
  con error ≤ 1 tick, sin colas, y replay idéntico de la sesión grabada;
- [x] fuzzing de todos los decodificadores nuevos; ASan+UBSan sin errores;
- [ ] `SessionRules`, selección de nivel, espectador/rejoin y
  manifiesto/transferencia de contenido (dependen de cada juego);
- [ ] esquemas de entidad por familia (Fase 4/5).

Entregables:

- mensajes v1;
- mensajes `SessionRules`, selección de nivel, espectador/rejoin y manifiesto/transferencia de contenido;
- parser acotado;
- `FakeTransport`;
- fuzzer;
- registro/replay;
- clock sync y canales.

Puerta: cobertura de todos los tipos y corpus sin fallos bajo sanitizers.

### Fase 4 — Pikmin 1: capitanes — **EN CURSO (casi completa: falta botón de interfaz y prueba de juego del mantenedor)**

Primera vertical funcionando (14-10-2026, Linux, dos procesos reales con
`tools/netplay/run_online_pair.py` y degradación de GNS):

- [x] modo `--online-host`/`--online-join` con un solo código, sobre la sala
  de `nectar_net` (cuarentena, compatibilidad, puerto); el lockstep sigue
  intacto (misma prueba de 3.000 ticks con hashes idénticos y las mismas
  cifras que antes);
- [x] host autoritativo a 30 Hz sin esperar; input del invitado inyectado en
  P2 desde `InputReceiver`; estado de ambos capitanes cada tick;
- [x] invitado adelantado según `inputLeadTicks`, predicción de su capitán,
  corrección suave/snap (`PredictionCorrector`) y capitán del host
  interpolado (`SnapshotInterpolator`), compartidos para Pikmin 2;
- [x] **puerta de transporte superada**: 30 min reales a RTT ~182 ms
  (lag 75 ms por sentido), jitter 30 ms y 5 % de pérdida: input del
  invitado en vivo en 53.793/53.850 ticks (99,89 %), error medio del
  capitán del invitado 4,9 unidades en 360 muestras sin deriva acumulada,
  RTT estable y sin colas crecientes;
- [x] el código nunca aparece en logs; el archivo del código se crea 0600;
- [ ] limitación conocida: cinemáticas y eventos del mundo aún son locales;
  una cinemática disparada en distinto momento en el invitado (demo04)
  provocó 34 snaps en 20 s y se recuperó sola al terminar (Fase 5:
  escenas/cinemáticas autoritativas);
- [x] cámara/HUD por peer: cada proceso presenta su capitán (presentación
  en dos pasadas del lockstep), comprobado con volcado de fotogramas;
- [x] reglas del host: el invitado conecta al iniciar los ajustes y adopta
  los ajustes de simulación del host (`SessionRules`) antes de leerlos;
- [x] el host reenvía su input y el invitado lo aplica a su copia de P1 con
  retardo: pausa, menús, silbato y lanzamientos se reproducen; con esto y
  las reglas, el error medio del capitán del invitado bajo RTT ~180 ms y 5 %
  de pérdida bajó de 7,6 a 0,06 unidades;
- [x] pausa: el Start del host abre el menú también en el invitado (10 ticks
  después, el retardo de reenvío); el Start del invitado llega al host como
  input de P2. El cierre del menú puede diferir unos ticks porque lo dicta
  el input del invitado: menús autoritativos en la Fase 5;
- [x] espectador y reincorporación (`LeaveCoopRequest`/`RejoinRequest`/
  `PlayerControlChanged`): P2 conserva su plaza con input inactivo y ve la
  cámara del host; Start pide volver y el host lo concede sólo sin pausa,
  menú ni cinemática. Comprobado: capitán de P2 inmóvil en el host durante
  el modo espectador y cámaras correctas en los fotogramas;
- [x] si el invitado se desconecta, el host sigue y P2 queda neutral;
- [ ] entrada de interfaz para "Dejar cooperativo" (hoy API
  `pc_netplay_online_request_leave` y ajustes de prueba);
- [ ] Windows/Android reales y prueba de juego del mantenedor.

Entregables:

- inyección de P2 remoto;
- IDs y snapshot de Navi;
- predicción/reconciliación;
- cámaras/HUD local por peer;
- reglas temporales del host, selección de nivel, pausa/cierre;
- dejar cooperativo, espectador de P1 y reincorporación segura.

Puerta: 30 minutos de movimiento con RTT 150 ms, jitter 30 ms y 5 % de pérdida sin divergencia acumulada ni cola creciente.

### Fase 5 — Pikmin 1: mundo completo — **EN CURSO**

**Decisión del mantenedor (15-10-2026): primero la opción A, "réplica
exacta"; después se intentará la opción B.**

- Medición previa con sólo capitanes replicados (3.000 ticks): mundo y
  Pikmin iguales, pero RNG distinto desde el tick 692 y enemigos/objetos
  desde el 2.533. Replicar sólo capitanes no basta.
- **A — réplica exacta (en curso):** el host envía cada tick los inputs que
  ha simulado (P1 y P2 tal como los aplicó); el invitado reproduce
  exactamente esos ticks. El modo determinista ya está hecho para dar
  resultados idénticos entre máquinas (libm por software, sin FMA,
  `fp_gate`), así que todo el mundo coincide: cinemáticas, menús, Pikmin,
  enemigos, objetos y días; el guardado queda en el host. El host nunca
  espera. Coste: el capitán del invitado responde con ~RTT + 60 ms. Los
  hashes de estado del host se comparan con los del invitado para detectar
  desincronizaciones (aún sin reparación).
- **B — replicación por familias (después):** estado autoritativo por
  familia de entidad con predicción del invitado, para quitar el retraso de
  su capitán y poder reparar desincronizaciones (también la base para
  arquitecturas que no sean bit-exactas). El modo predictivo de la Fase 4 se
  conserva (`--online-predictive`) como punto de partida.

Progreso de la opción A (Linux, dos procesos reales del juego):

- [x] `AppliedInputs` (8 ticks de redundancia no fiable + copia fiable,
  enviados antes de simular) y `StateHash` del host 2 veces por segundo;
- [x] el invitado reproduce a 30 Hz, acelera si se retrasa, colchón de 1–4
  ticks según el jitter; su input viaja adelantado y se muestrea por reloj;
- [x] **mundo completo idéntico**: 3.000/3.000 ticks con hash de estado
  idéntico (capitanes, Pikmin, enemigos, objetos, mundo, RNG y modo) en red
  local y con RTT ~185 ms, jitter 30 ms y 5 % de pérdida, sin atascos;
- [x] menús y pausa idénticos tick a tick; espectador y reincorporación
  deterministas; semilla del host adoptada (`seed=` en `SessionRules`);
- [x] retraso del capitán del invitado: ~104 ms en red local y ~294 ms con
  RTT ~185 ms (medido en el log, `input-latency`);
- [x] partida guardada privada por sesión (`online-sessions/<rol>-<fecha>`):
  ningún jugador escribe en sus partidas reales; la del host conserva la
  sesión;
- [x] **reconexión / entrar con la partida empezada**: el host guarda todos
  los inputs aplicados (hasta 4 h) y se los envía al invitado nuevo, que los
  reproduce a ~365 ticks/s (≈12× tiempo real) con input neutro hasta
  alcanzar al host; probado con caída en el tick 1.200 y 6.000: 9.000/9.000
  ticks idénticos, también con red degradada; si la sala sigue "llena" tras
  un cuelgue, el invitado reintenta 30 s;
- [x] hallazgo: saltarse la pasada de presentación
  (`pc_netplay_present_set_skip_presentation`) **no es neutral** para la
  simulación (estado de objetos distinto desde el tick 12), así que la puesta
  al día dibuja todos los ticks;
- [x] prueba de 20.400 ticks (11 min de juego) con red degradada sin ninguna
  desincronización en 1.359 comprobaciones (detenida por falta de CPU al
  compartir la máquina con compilaciones);
- [x] **30 minutos reales** con RTT ~183 ms, jitter 30 ms y 5 % de pérdida:
  54.000/54.000 ticks con hash idéntico, 3.589 comprobaciones sin
  desincronización, 0 atascos, input del invitado en vivo en el 99,94 %;
- [ ] campaña completa y matriz de objetos/enemigos (necesita una partida
  jugada, no input aleatorio);
- [ ] cross-play real entre arquitecturas (x86-64 ↔ ARM64): la réplica exige
  determinismo bit a bit, que la libm por software y `-ffp-contract=off`
  persiguen pero que sólo se demuestra en dispositivos reales;
- [x] **continuar una partida guardada** (`--online-save <1-3>` en el host):
  el host juega sobre su tarjeta real (sólo él escribe progreso) y envía una
  copia al invitado (`ContentChunk`/`ContentManifest`: nombres sin rutas,
  ≤ 128 archivos, ≤ 4 MiB, SHA-256 del conjunto) que se escribe en su
  carpeta privada antes de que el juego abra la tarjeta; el arranque
  directo carga ese archivo en los dos. Probado con una partida del día 13:
  3.000/3.000 ticks idénticos (también con red degradada) y, con un
  invitado que se cae y vuelve, 4.000/4.000: el host envía siempre la foto
  de la tarjeta del inicio de la sesión, no la de ese momento. En niveles
  avanzados la puesta al día baja a ~88 ticks/s (≈3× tiempo real);
- [x] HUD del modo online (calidad, ping, jitter, retraso real que nota el
  invitado, atascos de 10 s y una línea de estado: esperando invitado,
  poniéndose al día, viendo al host, el invitado se fue, el host se fue,
  desincronización), comprobado con volcado de fotogramas;
- [x] fila "Online co-op: leave / rejoin" en F1 (Data), activa sólo para el
  invitado online; compilada, falta probarla a mano;
- [x] **Windows (MinGW) y Android (NDK 28 arm64) compilan el modo online**:
  el `.exe` sólo importa DLL del sistema; la `.so` sólo librerías del
  sistema y SDL2. En Android se habilitan sesión, GekkoNet, libjuice y la
  libm determinista (raíz cuadrada con `fsqrt`, idéntica bit a bit a
  `sqrtsd`); la capa Java (`NetGateway`) pasa la puerta de enlace al nativo
  (`ACCESS_NETWORK_STATE`). En la `.so` ARM no hay FMA fuera de GNS y de la
  corrección predictiva;
- [x] puerta FP del `.exe` MinGW: los dos `fsqrt` x87 de `pumpAudio` eran
  la RMS en `long double` de los diagnósticos de audio; ahora es `sqrt` en
  `double` (SSE) y `fp_gate.py` pasa;
- [x] `iphlpapi.dll` en la lista blanca de `scripts/release-windows.sh`;
- [ ] APK completo con Gradle y prueba en dispositivo (necesita al
  mantenedor).


Entregables:

- Pikmin, items, obstáculos, enemigos y bosses por lotes;
- separación Authority/Replica;
- escenas/cinemáticas/día;
- snapshot completo y reconexión;
- guardado host-only.
- preservación verificada de ejércitos separados, límite global compartido y colisión entre capitanes.

Puerta: campaña completa y matriz de objetos/enemigos; ninguna familia sin esquema.

### Fase 6 — Endurecimiento y cross-play de Pikmin 1

Entregables:

- todas las combinaciones de plataforma;
- Android lifecycle;
- adaptación de ancho de banda;
- diagnósticos de CGNAT, firewall y reenvío manual;
- transferencia consentida de personajes personalizados a caché aislada, con cuotas, hashes y fuzzing de parsers;
- informes de fallo y UI final;
- beta opt-in.

Puerta: soak de 8 horas, matriz de red y revisión de seguridad.

### Fase 7 — Pikmin 2: superficie

Entregables:

- adaptador P2;
- activación tras Louie;
- capitanes, Pikmin, enemigos y objetos de superficie;
- pausas, cinemáticas y fin de día.

Puerta: varios días completos en historia, cross-platform y desconexión segura.

### Fase 8 — Pikmin 2: cuevas

Entregables:

- protocolo de layout/seed/hash;
- barreras por piso;
- tesoros, sprays, geysers y resultados;
- snapshot/rejoin en cueva.

Puerta: recorrido completo de todas las cuevas con pérdidas/jitter y cambio de piso repetido.

### Fase 9 — Modos adicionales y lanzamiento

Entregables:

- Batalla/Desafío online;
- compatibilidad de release/protocolo;
- capacidad de desactivar online remotamente sólo mediante actualización/configuración firmada, nunca ejecución remota;
- documentación de usuario, privacidad, CGNAT y configuración del router;
- matriz pública de routers/plataformas y errores conocidos.

Puerta: definición de terminado de la sección siguiente.

## 18. Definición de terminado

El online no se considerará terminado por «dos ventanas se mueven». Debe cumplir todo lo siguiente:

- Crear y unirse con código funciona sin infraestructura externa cuando hay una ruta entrante disponible.
- PCP, NAT-PMP, UPnP IGD, IPv6 global y reenvío manual se prueban por separado; el fallo de un método no deja mappings huérfanos.
- La UI no promete conectividad universal y explica claramente CGNAT, firewall, timeout y mala red.
- Windows, Linux y Android interoperan en todas las direcciones soportadas.
- Host e invitado pueden usar FPS de presentación distintos.
- No hay escritura de save en invitado.
- El host controla nivel, save y reglas de gameplay; la configuración local del invitado se restaura íntegramente al terminar.
- Cada capitán mantiene su ejército ya existente, ambos respetan el límite global y la colisión se resuelve autoritativamente.
- P2 puede pasar a espectador y reincorporarse sin reconectar ni crear un tercer jugador.
- Un personaje personalizado ausente se transfiere sólo con consentimiento, hashes, cuotas y formatos permitidos; nunca instala código o mods ejecutables.
- Toda entidad jugable tiene esquema documentado, límites y tests.
- No existe desync silencioso; hash/corrección/resnapshot están operativos.
- Los parsers sobreviven fuzzing y sanitizers.
- Pérdida, reordenación, duplicación y reconexión están automatizadas en pruebas.
- Una desconexión no corrompe ni cierra la partida del host.
- Un tercero sin el secreto no puede entrar ni amplificar tráfico mediante el host (MITM activo en ruta: riesgo aceptado, ADR-0002).
- No se requieren servidores, cuentas, API keys ni credenciales desplegadas.
- Logs e informes no contienen códigos, secretos, direcciones completas ni datos privados.
- Pikmin 1 completa campaña cooperativa.
- Pikmin 2 completa superficie y cuevas cooperativas.
- Se han ejecutado sesiones largas reales en cada combinación de plataforma.

## 19. Decisiones cerradas y pendientes

### 19.1 Decisiones ya cerradas

1. El flujo normal comparte **un único código del host**; el invitado no tiene
   que devolver otro código manualmente.
2. No habrá servidores del proyecto, señalización, matchmaking, STUN ni TURN.
   El coste operativo de infraestructura será 0 € y se acepta que algunas
   conexiones bajo CGNAT/NAT simétrico no podrán alojar.
3. El transporte de producción será directo, cifrado y autenticado; ninguna
   primitiva criptográfica se diseñará desde cero.
4. El host es autoridad del mundo, configuración, nivel y save. El invitado no
   escribe progreso canónico y restaura sus preferencias al salir.
5. Los ejércitos separados y el límite global compartido se preservan; no se
   rediseñarán como parte del transporte.
6. P2 puede pasar a espectador y reincorporarse, ocupando siempre la única
   plaza de invitado.
7. El lockstep comunitario se conserva como vertical experimental, pruebas y
   diagnóstico, pero no define la arquitectura cross-play final.

### 19.2 Decisiones que faltan antes de congelar protocolo/UI

1. ~~Mecanismo de autenticación~~: cerrado en ADR-0002 (cifrado GNS +
   `join_secret`); falta decidir si se elimina o reserva el campo de 32 bytes
   de `NECTAR1`.
2. Política de progreso/logros del invitado.
3. Si el host puede asignar P1/P2 o queda fijo host=P1.
4. Quién puede saltar cinemáticas y abrir menús globales.
5. Si se permite join-in-progress arbitrario o sólo en puntos seguros.
6. Si QR entra en la primera versión; Base32 y prefijo versionado ya están
   decididos, pero el formato no se congela hasta cerrar la identidad del host.
7. Puerto automático aleatorio frente a puerto configurable/persistente para
   facilitar el reenvío manual.
8. Compatibilidad entre regiones USA/PAL después de pruebas de lógica.
9. Soporte online para mods/randomizer, formatos exactos permitidos para
   personajes personalizados y definición del manifest hash.
10. Comportamiento del capitán remoto al desconectarse.
11. Si una versión futura implementará migración de host.

Las decisiones de autenticación e identidad bloquean el formato estable y el
launcher de un código. Las demás no bloquean el transporte multiplataforma.

## 20. Siguiente secuencia de trabajo

Estado al 15-10-2026 (commits en `main` hasta `7c8ad4eb`). El orden evita
conectar gameplay sobre una base insegura:

1. **Checkpoint revisable — HECHO.**
2. **Autenticación — HECHO** ([ADR-0002](docs/netplay/ADR-0002-SESSION-AUTHENTICATION.md),
   aceptada): cuarentena del `join_secret` sobre el cifrado de GNS; Noise
   queda como mejora futura.
3. **Dependencias reproducibles — HECHO:** `scripts/netplay-deps.sh` con
   versiones fijadas y SHA-256 (GNS, protobuf, OpenSSL/BCrypt, miniupnpc).
4. **Cross-build — HECHO en compilación:** Linux, MinGW x86-64 y Android NDK 28
   arm64; ejecución en Linux y Android emulado. **Falta** Windows y Android
   reales.
5. **Endurecer transporte — HECHO en Linux:** métricas, saturación,
   reconexión, veto por origen, ASan/TSan. **Falta** repetirlo en Windows y
   Android reales.
6. **Handshake Nectar — HECHO salvo reanudación:** cuarentena, compatibilidad
   y `SessionRules`. **Falta** `resume_secret` para reconectar sin código nuevo.
7. **Congelar `NECTAR1` — HECHO**, con vectores de referencia.
8. **Publicación directa — HECHA con gateways simulados.** **Falta** router
   real (`nectar_net_publish_probe`) y el gateway en Android desde Java.
9. **Launcher de un código — lógica hecha (`room.h`).** **Falta** la interfaz
   Crear/Unirse (copiar/pegar/QR, mensajes de CGNAT/firewall, "Dejar
   cooperativo"), bloqueada por cambios locales ajenos en el launcher.
10. **Protocolo/fake transport — base HECHA** (input, reloj, entidades,
    escena, red simulada, replay, fuzzing). **Falta** selección de nivel y
    manifiesto/transferencia de contenido.
11. **Migración de Pikmin 1 — vertical de capitanes HECHA** (Fase 4).
    **Siguiente:** Fase 5, empezando por escenas, cinemáticas, menús y
    transiciones del día autoritativas; después Pikmin, objetos y enemigos
    por familias, y guardado sólo en el host.
12. **Pikmin 2:** reutilizar `nectar_net` (incluida `replication.h`),
    adaptador de superficie y después cuevas, sin duplicar transporte,
    handshake ni UI de conexión.

Pruebas que dependen del mantenedor, a agrupar en una sesión: router real,
Windows real, Android real y una partida jugada a mano para valorar la
respuesta del capitán y las correcciones.

No se marcará una fase como completada por compilar en una sola plataforma. La
puerta de cada fase exige sus pruebas de seguridad, cross-play y ciclo de vida.
