# Spike de transporte directo

Estado: Fase 1 en curso; adaptador Linux funcional, empaquetado multiplataforma pendiente.

## Resultado Linux

- GameNetworkingSockets: commit `d534c19aa760df3fb75fd20db13ba1932b8a5463`.
- Licencia: BSD-3-Clause.
- Protobuf usado en el spike aislado: v21.12 (`f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c`).
- Criptografía: OpenSSL 3.5.5 del sistema.
- Configuración: biblioteca estática, examples/tests/tools desactivados, `ENABLE_ICE=OFF`.
- Resultado: `GameNetworkingSockets_s` compila correctamente con GCC 15.2.
- Prueba: servidor y cliente en procesos separados conectaron por UDP a `127.0.0.1:28960`, intercambiaron el handshake de GNS y cerraron limpiamente.

La prueba se hizo íntegramente bajo `/tmp`; no instaló paquetes ni copió dependencias al repositorio.

## Dependencias confirmadas

GNS necesita una implementación criptográfica y protobuf. `protoc` y el runtime deben coincidir. Desactivar ICE evita WebRTC/señalización, pero no elimina protobuf. La integración de release deberá fijar y construir de forma reproducible:

1. protobuf para el target y `protoc` para el host;
2. OpenSSL o una alternativa auditada compatible con cada plataforma;
3. GameNetworkingSockets con ICE, shared library, tools y examples desactivados.

No se aceptará que el ejecutable dependa de una DLL/.so no incluida en el paquete.

## Estado por plataforma

| Plataforma | Configuración | Build | Conexión directa | Estado |
|---|---:|---:|---:|---|
| Linux x86-64 | sí | sí | localhost sí | 13/13 pruebas, binario sin `.so` de OpenSSL/protobuf/miniupnpc |
| Windows x86-64/MinGW | sí | sí | no ejecutado | sin Wine ni Windows aquí; los `.exe` sólo importan DLL del sistema |
| Android arm64/NDK 28 | sí | sí | bajo `qemu-aarch64` sí | 13/13 pruebas y cruce con Linux; falta dispositivo real |

Dependencias reproducibles (2026-10-14): `scripts/netplay-deps.sh` descarga
las versiones fijadas, comprueba SHA-256 y compila estático para las tres
plataformas (registro en `games/pikmin1/third_party/NETPLAY_VENDOR.md`).
Cifrado de GNS: OpenSSL 3.5.5 en Linux/Android y BCrypt en Windows.

Cruce de arquitecturas con `nectar_net_interop_peer` (dos procesos, UDP real
en loopback, el binario Android ARM64 bajo `qemu-aarch64`): host Linux ←
invitado Android y host Android ← invitado Linux autentican e intercambian
mensajes fiables/no fiables; con contraseña errónea, ambos hosts rechazan y el
invitado recibe `refused`. Es emulación de CPU, no un dispositivo Android.

El README upstream afirma que la biblioteca se usa en plataformas móviles, pero también advierte que parte del código multiplataforma de Valve no se distribuye en el repositorio abierto. Por ello no se asumirá soporte Android hasta compilar, enlazar y conectar en un dispositivo real.

## Integración ya añadida al repositorio

- `libs/nectar_net`: biblioteca C++17 independiente del motor.
- `ITransport`: contrato de listen/connect/send/poll/close.
- Byte I/O big-endian acotado.
- Cabecera y tipos v0, decoder transaccional y errores explícitos.
- Golden vector y pruebas de truncado, versión, tipo, flags, tamaño y longitud.
- `NECTAR_ONLINE=OFF` por defecto en ambos juegos; al activarlo se enlaza `nectar_net`.
- `GnsTransport` detrás de `ITransport`, activable con
  `NECTAR_NET_ENABLE_GNS=ON`, sin exponer cabeceras de GNS al juego.
- Recepción por eventos, un único invitado, límites de 4 MiB, entrega fiable
  ordenada y no fiable sin espera, cierre explícito y rechazo de un segundo
  invitado sin expulsar al primero.

Validación ejecutada:

- configuración y build standalone de `nectar_net`: correcto;
- `nectar_net_protocol_test`: 1/1 correcto;
- ASan+UBSan: 1/1 correcto con detección de fugas desactivada porque LeakSanitizer no funciona bajo el `ptrace` del sandbox;
- configuración `NECTAR_ONLINE=ON` y build del target `nectar_net` desde Pikmin 1 y Pikmin 2: correcto.
- build del adaptador contra el GNS/protobuf fijado por el spike: correcto;
- conexión cifrada en `127.0.0.1`, envío fiable invitado→host, envío no fiable
  host→invitado y cierre de ambos extremos: correcto.

El test de sockets necesita ejecutarse fuera del sandbox de desarrollo, que
bloquea incluso los binds UDP de loopback. En ese entorno autorizado pasa
completo; el fallo dentro del sandbox fue reproducido también con el transporte
UDP existente y no pertenece a GNS.

## Trabajo pendiente para cerrar la Fase 1

1. fijar la estrategia reproducible de dependencias sin instalar paquetes globales;
2. añadir métricas RTT/pérdida y pruebas de saturación/reconexión;
3. compilar y conectar Linux↔Windows, Linux↔Android y Windows↔Android;
4. revisar tamaño final, licencias, cierre, fugas y ciclo de vida Android.
