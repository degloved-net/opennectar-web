# Nectar Net Protocol v0

Estado: contrato previo a implementación. Los números son provisionales hasta congelar v1.

## Invariantes

- Orden de bytes de red (big-endian) para enteros multibyte.
- No se serializan structs C++, punteros, padding ni enums sin ancho.
- Todo mensaje se parsea y valida completamente antes de aplicarlo al juego.
- Longitud máxima de datagrama no fiable: 1.100 bytes.
- Longitud máxima de mensaje lógico fiable: 1 MiB; `bulk` fragmenta con un máximo total explícito.
- Un mensaje pertenece a una `session_id`, `scene_epoch` y versión de protocolo.
- El host es la única autoridad del mundo y del save.

## Cabecera v0

| Campo | Tipo | Validación |
|---|---:|---|
| magic | u32 | `0x4e435452` (`NCTR`) |
| protocol_major | u16 | igual en ambos peers |
| protocol_minor | u16 | negociado por capabilities |
| message_type | u8 | valor conocido |
| flags | u8 | bits desconocidos rechazados en v0 |
| channel_epoch | u16 | no menor que la época activa |
| session_id | u64 | igual a la sesión autenticada |
| scene_epoch | u32 | igual o transición permitida |
| sequence | u32 | ventana acotada por canal |
| ack_sequence | u32 | no futuro fuera de ventana |
| simulation_tick | u64 | ventana temporal acotada |
| payload_length | u16 | coincide con bytes restantes y límite del tipo |

## Canales lógicos

| Canal | Entrega | Mensajes iniciales |
|---|---|---|
| control | fiable/ordenada | hello, reglas, escenas, pausa, espectador, eventos críticos |
| input | no fiable/no ordenada | input actual más 3–5 ticks anteriores |
| state | no fiable/no ordenada | deltas y snapshots reemplazables |
| bulk | fiable/ordenada | snapshot completo y contenido aprobado |

## Handshake

Implementado en `nectar_net` (`GatedTransport`, ADR-0002):

1. El invitado decodifica el código `NECTAR1` y conecta a un endpoint. GNS
   resuelve el desafío sin estado (cookie antiamplificación) y cifra el canal.
2. Cuarentena: el invitado envía `ClientHello` (nonce de sala 16 + `join_secret`
   16) por el canal fiable. El host compara ambos en tiempo constante y
   responde `HostHello` (nonce 16 + veredicto 1); un rechazo lleva el nonce a
   ceros para no revelar la sala a quien no tiene el secreto. Un solo intento por conexión,
   5 s de plazo; cualquier otro mensaje antes del veredicto cierra la conexión.
   Tres fallos desde un mismo origen lo vetan 30 s, doblando hasta 10 min.
3. Compatibilidad: el invitado envía `CompatibilityOffer` con su
   `CompatibilityInfo`; el host responde `CompatibilityResult` con el primer
   campo distinto (juego, región, build, contenido, capabilities) y su propia
   info, 10 s de plazo. El invitado vuelve a comparar y trata como violación un
   veredicto que contradiga la info del host. Un invitado incompatible no se
   penaliza: conocía el secreto.
4. Sólo entonces ambos lados informan `Connected` y aceptan gameplay.
5. Pendiente: `SessionRules`, `session_id` y `resume_secret` distinto.

Riesgo aceptado (ADR-0002): un atacante activo en la ruta durante el handshake
puede suplantar a cualquiera de los dos. El código nunca es un token de sesión
permanente; el checksum sólo detecta errores de copia.

### `CompatibilityInfo` (76 bytes, big endian)

| Offset | Campo | Bytes |
|---:|---|---:|
| 0 | protocol_minor | 2 |
| 2 | game (1 Pikmin 1, 2 Pikmin 2) | 1 |
| 3 | region (código del juego) | 1 |
| 4 | build digest | 32 |
| 36 | content digest (datos y mods de lógica) | 32 |
| 68 | capabilities | 4 |
| 72 | required capabilities | 4 |

`CompatibilityResult` = campo (1) + `CompatibilityInfo` del host.

### Código `NECTAR1` (congelado el 14-10-2026)

`NECTAR1-` + Crockford Base32 canónico en grupos de 5 de:
magic `NCJC` | versión 1 | protocol_major u16 | flags u16 | nº endpoints u8 |
caducidad u32 (Unix) | nonce 16 | join_secret 16 | por endpoint: familia u8,
puerto u16, prioridad u8, 4 o 16 bytes | CRC32. Máximo 4 endpoints y 384
caracteres. Con un IPv4 ocupa 119 caracteres. Se rechazan relleno no canónico,
caracteres sobrantes y datos tras el CRC. Vectores de referencia en
`libs/nectar_net/tests/connection_code_test.cpp`; cambiarlos rompe códigos ya
compartidos y exige un formato nuevo (`NECTAR2`).

## Mensajes de juego v1 (implementados en `nectar_net`)

Todos big endian, con tamaños exactos o acotados y validados antes de reservar
memoria. Tick canónico: 30 Hz.

| Mensaje | Payload | Canal |
|---|---|---|
| `InputBundle` | tick más nuevo u64, nº frames u8 (1–6), frames del más nuevo al más viejo: held, pressed, released u16; stick x/y, cstick x/y i8; triggers u8×2; cursor dx/dy i16; flags u8 (17 bytes) | no fiable |
| `Ping` | id u32, hora del emisor u64 (12 bytes) | no fiable |
| `Pong` | id u32, eco u64, tick del host u64, fase del tick u32 < 34 ms (24 bytes) | no fiable |
| `WorldDelta`/`WorldSnapshot` | flags u8 (bit0 keyframe), nº registros u16, registros: id u64, esquema u8 (≠0), longitud u16, datos | no fiable / bulk |
| `StateHash` | tick u64, hash u64, nº entidades u32 (20 bytes) | control |
| `AppliedInputs` | tick más nuevo u64, nº ticks u8 (1–8), tamaño u8 (1–64), bloques del más nuevo al más viejo (Pikmin 1: P1+P2, 32 bytes) | no fiable con 8 de redundancia + copia fiable |
| `SessionRules` | `seed=<u32>\n` + texto `clave=valor;` de los ajustes de simulación del host (≤ 8 KiB, imprimible) | control |
| `ScenePrepare` | época u32 (≠0), escena u32, semilla u64 | control |
| `SceneReady` | época u32, escena u32 | control |
| `SceneStart` | época u32, tick de inicio u64 | control |

Reglas:
- `InputBundle`: ticks consecutivos; botones y flags fuera de la máscara del
  juego y sticks a -128 se rechazan. El host descarta ticks ya consumidos o
  más de 30 por delante, reconstruye pulsar/soltar desde `held`, repite el
  último estado 6 ticks (sin repetir eventos de un solo uso) y después pasa a
  input neutro.
- Reloj: el invitado estima el tick del host con la muestra de menor RTT de
  las 16 últimas; búfer de interpolación `clamp(60 + 2·jitter, 60, 150)` ms.
- `NetEntityId` = época de escena (16 bits bajos) | tipo | generación |
  contador ≠ 0. Un delta con registros de otra época o ids repetidos se
  rechaza entero; los datagramas no fiables se empaquetan autocontenidos en
  ≤ 1.100 bytes.
- Escena: las épocas sólo crecen; mensajes de una escena anterior se ignoran.
- Réplica exacta (Pikmin 1, Fase 5 opción A): el host envía antes de
  simular cada tick los inputs que aplicará; el invitado reproduce
  exactamente esos ticks a 30 Hz (acelera si se retrasa, colchón de 1–4
  ticks según el jitter) y compara el `StateHash` del host dos veces por
  segundo. Su propio input viaja en `InputBundle` adelantado `lead` ticks.
- Grabaciones (`NNRC` v1): sólo tráfico posterior al handshake, tope de
  tamaño, parser estricto y reproducción determinista con `ReplayTransport`.

## Tipos v0 reservados

| ID | Nombre | Canal | Dirección |
|---:|---|---|---|
| 1 | ClientProbe | control/preauth | C→H |
| 2 | HostCookie | control/preauth | H→C |
| 3 | ClientHello | control | C→H |
| 4 | HostHello | control | H→C |
| 5 | SessionRules | control | H→C |
| 6 | CompatibilityResult | control | ambas |
| 7 | Ping | control | ambas |
| 8 | Pong | control | ambas |
| 9 | CompatibilityOffer | control | C→H |
| 16 | InputBundle | input | C→H |
| 32 | WorldDelta | state | H→C |
| 33 | WorldSnapshot | bulk | H→C |
| 34 | StateHash | control | ambas |
| 48 | ScenePrepare | control | H→C |
| 49 | SceneReady | control | C→H |
| 50 | SceneStart | control | H→C |
| 64 | LeaveCoopRequest | control | C→H |
| 65 | RejoinRequest | control | C→H |
| 66 | PlayerControlChanged | control | H→C |
| 80 | ContentManifest | control | ambas |
| 81 | ContentDecision | control | C→H |
| 82 | ContentChunk | bulk | ambas |
| 96 | Disconnect | control | ambas |

## Límites v0

| Recurso | Límite inicial |
|---|---:|
| Peers autenticados | 2 total |
| Inputs por bundle | 6 |
| Datagrama no fiable | 1.100 bytes |
| Mensaje fiable normal | 64 KiB |
| Snapshot completo | 8 MiB comprimido / 32 MiB expandido |
| Paquete de personaje | 64 MiB total |
| Archivo de personaje | 32 MiB |
| Archivos por personaje | 128 |
| String de UI | 256 bytes UTF-8 |
| Intentos preauth por IP | presupuesto configurable y acotado |

Todos los límites se centralizarán en `nectar_net/limits.h` y reducirlos será compatible dentro de la misma versión menor; aumentarlos requiere capability.

## Compatibilidad

- `protocol_major` distinto: rechazo.
- Build o región distintos: rechazo inicial salvo pareja probada y allowlist explícita.
- Manifest de gameplay distinto: transferencia sólo para personajes declarativos admitidos; mods de lógica requieren coincidencia exacta.
- Campos desconocidos no se ignoran en v0 salvo que el mensaje declare una sección TLV opcional y acotada.

## Pendiente para v1

- Asignación definitiva de IDs y representación cuantizada.
- Esquemas por familia de entidad.
- Ventanas de secuencia, tick y reanudación medidas en pruebas de red.
