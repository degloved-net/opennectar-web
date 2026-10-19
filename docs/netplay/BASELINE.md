# Línea base previa a netplay

Captura: 2026-10-14, commit `f0aaac03`, árbol local con cambios no relacionados.

## Máquina

- Linux 7.0.0-38-generic x86-64.
- Intel Core i7-7700HQ @ 2.80 GHz, 8 CPUs lógicas.
- Builds `RelWithDebInfo`, GCC `/usr/bin/c++`.
- Pikmin 1: `VERSION_GPIE01_01`.
- Pikmin 2: `VERSION_GPVP01`.

## Artefactos existentes

| Artefacto | Tamaño |
|---|---:|
| `build/pikmin1/bin/nectar` | 69.003.120 bytes |
| `build/pikmin2/pikmin2_pc` | 110.887.640 bytes |
| `build/pikmin1/libpikmin_legacy.a` | 92.009.956 bytes |
| `build/pikmin2/libpikmin2_legacy.a` | 274.747.208 bytes |

## Pruebas base

| Suite | Resultado | Tiempo real |
|---|---|---:|
| Pikmin 1 | 26/27 pasan | 12,60 s |
| Pikmin 2 | 17 pasan, 1 omitida, 0 fallos | 0,07 s |

Fallo base conocido de Pikmin 1: `pc_tev_shader_test` informa que `uChan0AlphaEn` y `uChan0AlphaDiff` no existen en el ubershader. Es anterior a `nectar_net` y queda registrado para no atribuírselo al online. `p2_jaudio_integration_test` se omite por su backend actual.

## Tick, frame y memoria

Ambos juegos ya integran `pc_tick_profiler`, con una ventana acotada de 2.048 muestras y métricas mean/p50/p95/p99/worst para tick, update, render y simulación. La referencia lógica es 30 Hz (33,33 ms por tick); la presentación puede ser 30/60/120 FPS.

La captura interactiva reproducible se realizará antes y después de cada integración de gameplay con:

```text
PIKMIN_TICK_STATS=1 <juego>
```

Escenarios fijos:

1. título durante 60 s;
2. mapa de prueba con dos capitanes y 100 Pikmin durante 5 min;
3. combate de carga alta durante 5 min;
4. Pikmin 2 superficie y un piso de cueva durante 5 min cada uno.

Registrar por escenario: tick whole/update/world-sim p50/p95/p99, FPS de presentación, RSS pico y memoria de `nectar_net`. El entorno actual no ofrece una sesión gráfica automatizable, por lo que este documento conserva la instrumentación, hardware, artefactos y pruebas base; no inventa cifras de gameplay.

## Presupuestos para aceptar netplay

- Captura/aplicación de red: p95 menor de 2 ms por tick en el hardware objetivo.
- Memoria incremental normal: menor de 64 MiB.
- Ninguna cola crece sin límite.
- Tráfico objetivo: menor de 1 Mbit/s por sentido; techo inicial 3 Mbit/s.
- El modo Offline no debe cambiar su resultado observable y su sobrecoste debe quedar bajo el ruido de la medición.
