# ADR-0001: arquitectura del cooperativo online

- Estado: aceptado
- Fecha: 2026-10-14
- Alcance: Pikmin 1 y Pikmin 2 en Linux, Windows y Android

## Decisión

El online utilizará un host autoritativo y un único invitado. El host ejecuta la simulación canónica, valida input e interacciones, asigna identidades de red y es el único que escribe el guardado. El invitado predice sólo su capitán y cursor, interpola el resto y corrige contra snapshots del host.

El transporte será UDP directo y cifrado, oculto detrás de `nectar_net::ITransport`. No habrá servicios de señalización, matchmaking, STUN ni TURN. El host publicará un puerto mediante IPv6 global, PCP, NAT-PMP, UPnP IGD o reenvío manual. El código de invitación será autocontenido y contendrá endpoints, versión, nonce, secreto de unión y fingerprint del host.

## Razones

- La decompilación usa RNG global, coma flotante, orden de managers y rutas distintas entre x86-64 y ARM64. Lockstep no puede garantizar determinismo prolongado.
- El rollback completo exigiría serializar y restaurar todo el grafo del mundo, audio, partículas, scripts y managers; no es una primera entrega viable.
- La autoridad del host permite reparar divergencias, conservar el save en un único lugar y limitar trampas del invitado.
- La conexión directa cumple el requisito de coste operativo cero, aceptando que un host detrás de CGNAT o una red restrictiva puede no ser alcanzable.

## Propiedad del estado

| Estado | Autoridad | Invitado |
|---|---|---|
| Mundo, IA, RNG, daño y spawns | Host | Réplica/interpolación |
| Input de P2 | Host valida | Invitado origina |
| Capitán local de P2 | Host confirma | Predicción visual acotada |
| Nivel, reglas y mejoras | Host | Capa temporal en memoria |
| Guardado de historia | Host | Nunca escribe por la sesión |
| Gráficos, audio, idioma y controles | Cada peer | Local |
| Personajes personalizados | Manifiesto acordado | Caché aislada y consentida |

## Alternativas rechazadas

### Lockstep determinista

Rechazado como modelo principal por divergencias de RNG, orden, temporización y coma flotante entre plataformas. Se conserva replay de inputs como herramienta de diagnóstico.

### Rollback completo

Rechazado inicialmente por coste de snapshots integrales, efectos secundarios y complejidad de cuevas/transiciones. Puede reevaluarse para modos competitivos acotados.

### Servidor dedicado o relay

Rechazado por coste y dependencia operativa. No se promete hosting universal ni ocultación de IP.

### Sincronizar sólo transformaciones

Rechazado porque no cubre FSM, propiedad, transporte, muertes, recompensas, escenas ni guardado y produciría desincronizaciones silenciosas.

## Consecuencias

- El protocolo debe versionar cada mensaje y usar IDs estables, nunca punteros.
- `Replica` no puede ejecutar mutaciones autoritativas de managers.
- La UI debe explicar CGNAT, firewall, exposición de IP y fallback manual.
- Las pruebas deben cubrir pérdida, duplicación, reordenación, reconexión y plataformas cruzadas.
- Todo contenido recibido se considera hostil; no se aceptan scripts ni binarios.

## Criterios para revisar esta decisión

Reabrir el ADR sólo si las pruebas demuestran que el modelo autoritativo no cumple los presupuestos de ancho de banda/latencia, o si se aprueba infraestructura externa que cambie expresamente el requisito de coste cero.
