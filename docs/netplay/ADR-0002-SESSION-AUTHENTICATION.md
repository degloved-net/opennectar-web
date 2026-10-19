# ADR-0002: autenticación de la sesión directa

- Estado: **aceptado** (2026-10-14) — opción C; Noise queda como mejora futura
- Fecha: 2026-10-14
- Alcance: `nectar_net`, código `NECTAR1`, Pikmin 1 y Pikmin 2 en Linux, Windows y Android
- Depende de: [ADR-0001](ADR-0001-NETPLAY-ARCHITECTURE.md), [TRANSPORT_SPIKE.md](TRANSPORT_SPIKE.md)

## Problema

`GnsTransport` conecta por IP con `IP_AllowWithoutAuth`. GNS cifra el tráfico,
pero sin certificado firmado por una CA de confianza no autentica al peer: un
atacante activo en ruta puede terminar dos conexiones GNS y retransmitir entre
ellas. Añadir después un HMAC sobre `join_secret` no lo arregla, porque el MITM
puede reenviar ese HMAC tal cual. Necesitamos que las claves que protegen el
gameplay dependan de material que sólo conocen el host real y el invitado que
recibió el código.

## Evidencia revisada (GNS `d534c19a`)

- `steamnetworkingsockets_certstore.cpp`: la única raíz de confianza se carga
  con `SlamHardcodedRootCA()` desde la macro de compilación
  `STEAMNETWORKINGSOCKETS_HARDCODED_ROOT_CA_KEY`. `CertStore_AddCertFromBase64`
  es interna (no está en la API pública) y sólo añade certificados encadenados
  a una clave ya confiada.
- `steamnetworkingsockets_connections.cpp` (~L1400): con certificado no firmado
  la identidad del cert no se verifica contra nada; la API pública no expone la
  clave pública remota tras conectar, así que no se puede fijar desde fuera.
- `steamnetworkingsockets_udp.cpp`: el listen socket responde a
  `ChallengeRequest` acolchado con un `ChallengeReply` sin estado. Esto ya
  aporta cookie antiamplificación a nivel de transporte, antes de reservar
  conexión.
- libsodium 1.0.12/1.0.13 fue auditado en 2017 por Matthew Green
  (Cryptography Engineering) sin hallazgos críticos; compila en Windows/MinGW,
  Linux y Android (ISC).
- No se ha encontrado auditoría publicada de una implementación Noise en C/C++:
  ni de `noise-c` (rweather, MIT, "reference implementation") ni de
  `hydro_kx_*` de libhydrogen (primitiva Gimli, no estándar). `snow` (Rust)
  declara no estar auditado. El *framework* Noise sí tiene análisis formal
  (Girol et al., USENIX Security 2020).

## Opciones

### A. Certificado GNS con identidad del host fijada por el código

El host generaría una CA efímera, firmaría su certificado y el código llevaría
la clave de esa CA; el invitado la instalaría como raíz.

- (+) Un solo handshake y un solo cifrado; GNS ya liga clave efímera y cert.
- (−) Requiere parchear GNS para aceptar raíces en tiempo de ejecución o
  exponer la clave remota: parche local en la ruta de validación de
  certificados, justo la zona donde un error rompe toda la seguridad.
- (−) Ese parche hay que mantenerlo en cada actualización de upstream y en las
  tres plataformas; upstream no lo ofrece como caso de uso.
- (−) Ata la autenticación a GNS; si Android/MinGW obligan a cambiar de
  transporte (riesgo abierto en Fase 1), se pierde todo.
- (−) El código tendría que llevar además el `join_secret` para autenticar al
  invitado, con un segundo mecanismo propio igualmente.

### B. Noise con clave estática del host y PSK, extremo a extremo sobre GNS

Patrón candidato: `Noise_NKpsk2_25519_ChaChaPoly_BLAKE2b`. El invitado conoce
la clave estática efímera-por-sala del host (los 32 bytes de `NECTAR1`) y el
PSK derivado del `join_secret`. Los mensajes del handshake viajan por el canal
fiable de GNS; después **todo** mensaje de aplicación (fiable y no fiable) se
protege con las claves de transporte de Noise. GNS queda como transporte
(fiabilidad, fragmentación, congestión, cookie) y su cifrado como defensa
adicional.

- (+) Un MITM que termine las dos conexiones GNS no puede derivar las claves:
  necesita la clave privada del host (`es`) y el PSK.
- (+) `ee` aporta secreto hacia delante; un handshake repetido falla porque el
  host aporta efímera nueva.
- (+) Independiente del transporte: sobrevive a un cambio de GNS.
- (+) Prologue de Noise permite vincular versión de protocolo, juego, nonce de
  sesión y endpoint lógico al transcript sin construcciones propias.
- (+) Respuesta del invitado automática por la conexión: encaja con un único
  código.
- (−) No hay biblioteca Noise auditada verificada. Las opciones reales son
  `noise-c` con backend libsodium, o una `HandshakeState` mínima sobre
  primitivas libsodium validada con los vectores oficiales (cacophony/snow).
  Ambas exigen revisión externa antes de una release pública.
- (−) Mensajes no fiables necesitan nonce explícito de 8 bytes y ventana
  anti-replay (como WireGuard): +24 bytes por mensaje con la etiqueta AEAD.
- (−) Noise espera PSK de 32 bytes de alta entropía. El `join_secret` actual es
  de 128 bits: o se amplía a 256 bits en el código (≈ +26 caracteres) o se
  expande con BLAKE2b; la ampliación es más simple de razonar.

## Decisión del mantenedor: opción C

Para un juego entre amigos que comparten un código, un MITM activo exige
controlar la red de uno de los dos (wifi pública, router, operador) y sólo
ganaría ver o alterar una partida. Se acepta ese riesgo residual. Se adopta:

- cifrado anónimo de GNS (`IP_AllowWithoutAuth`) como canal;
- el invitado envía el `join_secret` como primer mensaje fiable; el host lo
  compara en tiempo constante y, si falla o tarda, cierra la conexión;
- hasta validar el secreto la conexión está en cuarentena: ningún mensaje de
  juego se procesa;
- un solo invitado por sala; cerrar la sala invalida el código y el mapping;
- la defensa principal es la validación estricta de todos los mensajes y la
  cookie antiamplificación propia de GNS;
- el campo de 32 bytes de `NECTAR1` se eliminó al congelar el formato
  (14-10-2026); volver a Noise exigirá un formato nuevo (`NECTAR2`).

Riesgo aceptado y documentado: un atacante en ruta durante el handshake podría
retransmitir el secreto y suplantar a cualquiera de los dos. Las opciones A y B
quedan descritas abajo por si en el futuro el riesgo cambia (p. ej. partidas
públicas o cuentas).

## Recomendación original (no adoptada por ahora)

**Opción B**, con libsodium como único backend criptográfico y GNS como
transporte. Motivos: no toca la validación de certificados de GNS, separa
autenticación de transporte y resiste el MITM de doble terminación que la
opción A sólo evita con un fork.

Antes de aceptarla falta decidir, con el mantenedor:

1. `noise-c` (dependencia vendorizada, sin auditoría, mantenimiento a
   verificar) frente a `HandshakeState` mínima propia sobre libsodium limitada
   a un único patrón y comprobada con vectores oficiales. Ninguna es "inventar
   criptografía", pero ambas son código de protocolo que necesita revisión.
2. `join_secret` de 256 bits (nueva versión del formato `NECTAR2`) o
   expansión de 128 bits.
3. Si el campo de 32 bytes del código pasa a ser la **clave pública X25519
   completa** del host (recomendado: sin fingerprint intermedio).

## Consecuencias si se acepta

- `GnsTransport::Connected` se reporta como "transporte listo"; la sesión
  Nectar sólo pasa a `Authenticated` al completar Noise. Ningún mensaje de
  juego se procesa antes.
- La clave estática del host se genera por sala y se borra con
  `sodium_memzero` junto con PSK y claves de sesión.
- Prueba obligatoria: proxy MITM que termina dos `GnsTransport`, retransmite,
  altera y reordena; el handshake debe fallar sin revelar gameplay.
- Se añade libsodium a `third_party/NETPLAY_VENDOR.md` con commit, checksum y
  licencia; GNS puede seguir usando OpenSSL o pasar a libsodium (GNS soporta
  `USE_CRYPTO=libsodium`, a validar en el paso de vendorizado).

## Qué no se hace hasta cerrar la decisión

- No se conecta gameplay a `GnsTransport`.
- No se congela `NECTAR1`.
- No se escribe código criptográfico de handshake.

## Referencias

- [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)
- [Noise Protocol Framework](https://noiseprotocol.org/noise.html)
- [Libsodium audit results (PIA, 2017)](https://www.privateinternetaccess.com/blog/libsodium-audit-results/)
- [A Spectral Analysis of Noise (eprint 2024/1226)](https://eprint.iacr.org/2024/1226)
