# Pruebas manuales de Nectar Map Maker

Este documento comprueba las mejoras de usabilidad, precisión y diagnóstico
del editor de mapas VS. Las pruebas están agrupadas por función para que se
puedan repartir entre varias sesiones.

Las casillas marcadas corresponden a pruebas que ya se realizaron y fueron
confirmadas durante la implementación. Las casillas pendientes incluyen las
funciones del último lote y comprobaciones de regresión más específicas.

## 1. Preparación

Compilar:

```bash
cmake --build build/mapmaker --parallel 4
cmake --build build/pikmin1 --target pikmin_pc --parallel 4
```

Abrir Impact Arena:

```bash
./build/mapmaker/bin/nectar-mapmaker games/pikmin1/maps/vs/impact_arena/map.json
```

Antes de realizar cambios destructivos, usar **Guardar** y crear una copia del
mapa con otro nombre.

## 2. Disposición de la interfaz

- [x] Arrastrar el divisor izquierdo y comprobar que cambia el ancho de las herramientas.
- [x] Arrastrar el divisor derecho y comprobar que cambia el ancho del inspector.
- [x] Verificar que aparece el cursor de redimensionado sobre ambos divisores.
- [x] Hacer doble clic sobre cada divisor y comprobar que recupera su tamaño predeterminado.
- [x] Ocultar y mostrar ambos paneles desde **Vista**.
- [x] Usar **Vista > Restablecer disposición**.
- [x] Cerrar y volver a abrir el editor; los anchos y la visibilidad deben conservarse.
- [ ] Comprobar que el visor sigue siendo utilizable a 1280 × 720.

## 3. Selección en el visor

- [x] Un clic selecciona un único elemento.
- [x] `Ctrl + clic` añade un elemento a la selección.
- [x] `Ctrl + clic` sobre un elemento ya seleccionado lo quita.
- [x] Arrastrar desde un espacio vacío crea un rectángulo de selección.
- [x] Mantener `Ctrl` al crear el rectángulo combina la selección anterior.
- [x] `Alt + clic` recorre objetos superpuestos sin quedarse siempre con el primero.
- [x] Activar **Ignorar decoración al seleccionar** y seleccionar un objetivo situado detrás de una planta.
- [x] Todos los elementos seleccionados muestran su resaltado.

## 4. Gizmos y atajos de transformación

- [x] `W` activa movimiento; la flecha roja restringe a X y la azul a Z.
- [x] `E` activa rotación; arrastrar el aro amarillo gira la selección.
- [x] `R` activa escala; el control violeta aparece únicamente para selecciones compatibles.
- [x] Activar y desactivar **Ejes locales** y comprobar que las flechas siguen el giro del objeto.
- [x] Activar el ajuste y verificar los pasos de posición y ángulo.
- [x] Mover, girar y escalar una selección múltiple alrededor de su pivote común.
- [x] Cada arrastre completo se deshace con una sola pulsación de `Ctrl+Z`.
- [x] `Ctrl+Y` vuelve a aplicar la transformación.
- [x] **Colocar sobre el terreno** corrige decoración, elementos de escenario y construcciones compatibles.

## 5. Inspector de una selección

- [x] Seleccionar una decoración y editar numéricamente posición X/Z, posición Y, rotación X/Y/Z y escala.
- [x] Hacer doble clic en un número y escribir un valor exacto.
- [ ] Seleccionar un enemigo; solo deben aparecer X/Z y rotación Y.
- [ ] Seleccionar un muro; no deben aparecer controles verticales que el formato no guarda.
- [ ] Seleccionar un puente; su coordenada vertical debe llamarse **Altura sobre el suelo**.
- [x] Copiar la transformación de una decoración y pegarla en otra.
- [x] Pegar una transformación de decoración en un enemigo; no debe aplicarle escala ni rotaciones incompatibles.
- [x] Restablecer una transformación y comprobar posición/rotación 0 y escala 1.
- [x] Deshacer pegar y restablecer, una pulsación por operación.
- [ ] Abrir **Información técnica**, comprobar la referencia interna y copiar la ruta del asset.
- [ ] Revisar que las propiedades específicas siguen siendo editables: pellet, pieza, construcción, muro, charca y colisión de decoración.

## 6. Inspector de selección múltiple

- [ ] Seleccionar varios objetos y modificar **Centro X/Z**; deben conservar sus distancias relativas.
- [ ] Mover el centro sobre terreno irregular; la decoración debe conservar su altura relativa al suelo.
- [ ] Seleccionar varias decoraciones con escalas distintas; debe aparecer **— valores distintos**.
- [ ] Introducir una escala y comprobar que se aplica a todas.
- [ ] Seleccionar varios elementos girables con ángulos distintos; debe aparecer el aviso de valores diferentes.
- [ ] Introducir un giro Y común y comprobar que se aplica a todos.
- [ ] Mezclar tipos incompatibles; no deben aparecer altura, giro o escala si no son comunes a toda la selección.
- [ ] Copiar, pegar y restablecer transformación deben estar deshabilitados con selección múltiple.
- [ ] Duplicar y borrar deben afectar a todos los elementos removibles seleccionados.
- [ ] Con simetría activa, las parejas no seleccionadas deben actualizarse correctamente.

## 7. Jerarquía

- [x] Abrir todas las categorías y comparar sus contadores con el mapa.
- [x] Hacer clic en una fila y comprobar la selección sincronizada en el visor.
- [x] Usar `Ctrl` para combinar filas.
- [x] Hacer doble clic para enfocar la cámara.
- [x] Buscar sin distinguir mayúsculas ni acentos.
- [x] Pulsar `V` en una fila: el elemento desaparece del visor, etiquetas, minimapa y picking.
- [x] Pulsar `B`: el elemento sigue visible pero ya no se puede seleccionar desde el visor.
- [x] Comprobar que ocultar o bloquear no modifica `map.json`.
- [x] Clic derecho y **Seleccionar todos los del mismo tipo**.
- [x] Verificar coincidencia exacta por especie, modelo, construcción, color de Onion o tipo de pellet.
- [x] Los elementos ocultos o bloqueados no deben incluirse en esa selección.

## 8. Explorador de assets

- [ ] Abrir **Poner cosas > Decoración**.
- [ ] Buscar por nombre amigable.
- [ ] Buscar por ruta técnica.
- [ ] Buscar una palabra con y sin tilde; los resultados deben coincidir.
- [ ] Alternar entre **Cuadrícula** y **Lista**.
- [ ] Filtrar por plantas, objetos y otros; comprobar que las categorías no se mezclan.
- [ ] Activar **Solo con colisión** y comprobar los datos técnicos de los resultados.
- [ ] Probar coste **Ligero**, **Medio** y sin límite.
- [ ] Hacer clic derecho en un asset y añadirlo a favoritos.
- [ ] Activar **Favoritos** y comprobar que solo aparecen los marcados.
- [ ] Quitar un favorito y comprobar que desaparece del filtro.
- [ ] Elegir varios assets y activar **Recientes**; el último debe aparecer primero.
- [ ] Reiniciar el editor y comprobar que favoritos y recientes persisten.
- [ ] Abrir **Más cosas del juego (avanzado)** y buscar un modelo por ruta o categoría.
- [ ] Mantener el ratón sobre un modelo avanzado y revisar ruta, triángulos, colisión y dimensiones.
- [ ] Localizar un modelo muy grande o pesado y comprobar que aparece el aviso correspondiente.
- [ ] Arrastrar un asset desde el explorador al visor y comprobar que se coloca donde se suelta.
- [ ] El asset arrastrado debe añadirse también a recientes.

## 9. Capas del visor

- [ ] Abrir **Vista > Capas del visor**.
- [ ] Activar **Colisión compilada**; debe aparecer una superposición roja translúcida.
- [ ] Activar **Rutas de los Pikmin**; deben aparecer enlaces verdes y nodos amarillos.
- [ ] Si hay redes desconectadas, deben utilizar colores distintos.
- [ ] Seleccionar una pieza o pellet; debe resaltarse en blanco la ruta desde la base.
- [ ] Seleccionar un objetivo en una red desconectada; su nodo debe marcarse en rojo.
- [ ] Editar terreno u objetos y esperar la recompilación; las rutas deben actualizarse.
- [ ] Activar **Pendientes no transitables** sin seleccionar la herramienta de altura.
- [ ] Desactivar **Superficie del agua** y comprobar que desaparece únicamente la capa azul auxiliar.
- [ ] Desactivar **Nombres de elementos** y comprobar que desaparecen las etiquetas, no los objetos.
- [ ] Combinar varias capas y comprobar que la cámara y la selección siguen respondiendo.

## 10. Diagnósticos y arreglos

- [ ] Mover un elemento fuera de los límites del mapa.
- [ ] Abrir **Comprobaciones** y pulsar **Llévame allí**.
- [ ] Pulsar **Mover dentro**; debe quedar dentro y seleccionado.
- [ ] Deshacer el arreglo con una sola pulsación de `Ctrl+Z`.
- [ ] Colocar un elemento sobre un hueco.
- [ ] Pulsar **Buscar suelo cercano**; debe ir al punto válido más próximo.
- [ ] Deshacer, repetir el caso y pulsar **Crear suelo debajo**; debe aparecer una pequeña zona sólida sin mover el elemento.
- [ ] Con mapa espejo, comprobar que el suelo elegido también es válido para la copia opuesta.
- [ ] Deshacer y rehacer el arreglo.
- [ ] Confirmar que siguen apareciendo límites de piezas, pellets y Bulborbs.
- [ ] Confirmar que un mapa sin piezas continúa marcado como no jugable.
- [ ] Separar la red de caminos de una pieza o pellet y comprobar el diagnóstico de ruta inaccesible.
- [ ] Introducir manualmente en una copia del JSON una escala de decoración inválida; debe ofrecer **Normalizar escala**.
- [ ] Introducir una ruta de asset inexistente; debe ofrecer **Borrar elemento**.

## 11. Informe técnico

- [ ] Abrir la pestaña **Informe**.
- [ ] Mientras compila debe mostrar un estado de espera, no datos antiguos como actuales.
- [ ] Revisar estado jugable, vértices, triángulos de colisión, rutas, props y tamaño.
- [ ] Comparar los recuentos de piezas, pellets, enemigos y construcciones con la jerarquía.
- [ ] Añadir dos decoraciones con el mismo modelo; **Assets únicos** solo debe aumentar una vez.
- [ ] Provocar un error de compilación y comprobar el detalle técnico.
- [ ] Pulsar **Copiar detalle** y pegarlo en un editor de texto.

## 12. Guardado, simetría y regresión

- [ ] Guardar, cerrar y volver a abrir el mapa; las transformaciones y propiedades deben conservarse.
- [ ] Favoritos y recientes deben persistir, pero ocultos y bloqueos temporales no.
- [ ] Activar **Mapa espejo** y mover elementos con pareja desde gizmo e inspector.
- [ ] Desactivar el movimiento simultáneo y comprobar que deja de actualizar la pareja explícita.
- [ ] Probar copiar, pegar, duplicar y borrar objetos.
- [ ] Confirmar que borrar limpia estados temporales sin bloquear por error otro índice.
- [ ] Probar `Ctrl+S`, `Ctrl+Z`, `Ctrl+Y`, `Ctrl+D`, `Supr`, `F` y `F5`.
- [ ] Abrir el mapa en el juego y recorrerlo con ambos jugadores.
- [ ] Verificar que piezas, pellets, enemigos, puentes, rampas, bloques, puertas y agua funcionan.

## 13. Pinceles de terreno

- [ ] En suelo, altura, pintura, agua, plataformas y decoración deben aparecer los ajustes comunes de pincel.
- [ ] Comparar pincel **Circular** y **Cuadrado**; la previsualización debe coincidir con el área modificada.
- [ ] Comparar borde **Suave** y **Duro** al elevar terreno.
- [ ] Repetir la comparación al añadir suelo, pintar material y poner agua.
- [ ] Con decoración dispersa, el pincel cuadrado debe cubrir un cuadrado y el suave debe perder densidad hacia el borde.
- [ ] Activar **Pintar el suelo**, mantener `Alt` y hacer clic; debe seleccionar el material dominante bajo el cursor sin pintar.
- [ ] Pintar una pincelada completa y comprobar que `Ctrl+Z` la deshace de una vez.
- [ ] Activar mapa espejo y verificar que el pincel produce el mismo cambio en el lado contrario.

## 14. Herramienta de rampas

- [ ] Crear dos plataformas a alturas diferentes.
- [ ] Elegir **Rampas**, marcar primero el lado bajo y después el alto.
- [ ] Comprobar que el visor etiqueta claramente **INICIO** y **FINAL · CLIC PARA CREAR** durante los dos pasos.
- [ ] Antes del segundo clic debe aparecer el contorno de la rampa.
- [ ] Marcar un final demasiado cercano; debe mostrar un aviso y permitir elegir otro punto, sin cancelar la operación.
- [ ] Marcar ambos extremos a la misma altura; el rectángulo debe verse amarillo y explicar que falta desnivel.
- [ ] Cambiar la anchura y comprobar que cambia la previsualización.
- [ ] Una pendiente válida debe verse verde; una excesiva, roja y con aviso porcentual.
- [ ] Terminar una pendiente válida; debe aparecer **Rampa creada** y el terreno debe cambiar inmediatamente.
- [ ] Crear una rampa con perfil lineal y otra con perfil suavizado; comparar sus extremos.
- [ ] Comprobar que la rampa añade suelo si había pequeñas discontinuidades dentro de su área.
- [ ] `Ctrl+Z` debe deshacer toda la rampa con una sola pulsación.
- [ ] Pulsar **Cancelar rampa** después de marcar el inicio; no debe cambiar el terreno.
- [ ] Con simetría activa debe aparecer una rampa equivalente al lado opuesto.
- [ ] Activar rutas y pendientes, esperar la recompilación y comprobar que una rampa verde conecta ambos niveles.

## 15. Idiomas y presentación

- [ ] Cambiar a español y recorrer inspector, jerarquía, assets, Vista, comprobaciones e informe.
- [ ] Cambiar a inglés y repetir; no deben aparecer claves españolas nuevas.
- [ ] Comprobar textos largos con el panel derecho en su ancho mínimo.
- [ ] Comprobar que los botones no se solapan a escalado de interfaz alto.

## 16. Prueba de estrés

- [ ] Crear una selección rectangular grande y moverla, girarla y deshacerla.
- [ ] Añadir suficiente decoración para recorrer el catálogo y cargar muchas miniaturas.
- [ ] Activar simultáneamente colisión, rutas, pendientes, agua y etiquetas.
- [ ] Editar repetidamente mientras la previsualización compila en segundo plano.
- [ ] Confirmar que la interfaz no se bloquea y que el informe termina mostrando la generación actual.
- [ ] Guardar y probar el mapa resultante en el juego.

## 17. Eventos de reacción de elementos interactuables

### Configuración y persistencia en el editor

- [ ] Seleccionar un enemigo, guardián, compuerta, puente, rampa, roca empujable, llave y puerta con llave; cada uno debe mostrar **Reacción al interactuar** y describir su disparador correcto.
- [ ] Seleccionar una base, cohete, Onion, montón de bombas, pellet, pieza, charca, muro, decoración y fuente de néctar; ninguno debe mostrar reacciones que no pueda activar.
- [ ] Dejar **No ocurre nada**, guardar el mapa y comprobar que no se escribe un bloque `reaction` innecesario.
- [ ] Configurar **Aparecen enemigos**, elegir una especie, una cantidad de 1 a 8 y separación; guardar, cerrar y volver a abrir el mapa.
- [ ] Repetir la persistencia con pellets rojos, azules y amarillos de valores 1, 5, 10 y 20.
- [ ] Repetir con rocas bomba y néctar.
- [ ] Duplicar un elemento configurado; la copia debe conservar su reacción.
- [ ] Deshacer y rehacer una edición de la reacción; todos sus campos deben recuperar el valor correcto.
- [ ] En una compuerta de jugador con mapa espejo y **Editar también su pareja** activo, cambiar la reacción y comprobar que se copia a la otra compuerta.
- [ ] Desactivar **Editar también su pareja** y comprobar que ambas compuertas pueden tener reacciones distintas.
- [ ] En un elemento que el mapa duplique por simetría, comprobar que ambas copias conservan la misma configuración y que el inspector explica que se activan por separado.

### Activación dentro de una partida VS

- [ ] Derrotar un enemigo con reacción; el contenido debe aparecer una sola vez alrededor de su última posición.
- [ ] Derrotar un guardián con reacción y comprobar el mismo comportamiento.
- [ ] Destruir cada tipo de compuerta: blanda, dura y de rocas bomba. El evento debe dispararse al quedar destruida, una sola vez.
- [ ] Destruir una de las dos compuertas fijas de base y comprobar que solo se activa la reacción de esa compuerta.
- [ ] Terminar un puente corto, un puente largo y ambas orientaciones de rampa; el evento debe dispararse al completar la construcción.
- [ ] Terminar de empujar una roca; el evento debe aparecer en la posición final de la roca.
- [ ] Recoger una llave; el evento debe aparecer donde estaba la llave y no repetirse.
- [ ] Abrir una puerta con llave; el evento debe dispararse al empezar a desaparecer la puerta.
- [ ] Configurar cantidades 2 y 8; las apariciones deben distribuirse en círculo con la separación elegida, sin superponerse todas en el centro.
- [ ] Probar separación 0; todos los resultados deben aparecer en el punto del elemento sin bloquear ni cerrar el juego.
- [ ] Generar cada acción posible: enemigo normal, jefe, pellet, roca bomba y néctar.
- [ ] Hacer que un evento genere un enemigo y derrotarlo; no debe producir otro evento salvo que ese enemigo hubiera sido colocado originalmente con uno propio.
- [ ] Activar dos elementos distintos casi al mismo tiempo; ambos eventos deben ejecutarse exactamente una vez.
- [ ] Repetir un caso en un mapa simétrico; cada copia debe poder activar su evento independientemente.

### Errores, capacidad y multijugador

- [ ] Cargar una copia del JSON con acción, enemigo, pellet, cantidad o radio inválidos; el editor/juego debe rechazarla con un mensaje comprensible.
- [ ] Añadir manualmente una reacción a néctar, base, cohete o montón de bombas; debe rechazarse en vez de ignorarse silenciosamente.
- [ ] Superar mediante reacciones los límites de 80 enemigos normales o 40 jefes posibles; las comprobaciones deben impedir probar el mapa y explicar el límite.
- [ ] Probar varias reacciones de 8 elementos en una misma partida y vigilar estabilidad, rendimiento y límites de los gestores del juego.
- [ ] En netplay, activar el mismo evento y comprobar que aparecen exactamente los mismos objetos en ambos equipos.
- [ ] En netplay, activar dos eventos en fotogramas cercanos y comprobar que no se produce desincronización.
- [ ] Terminar una partida con varios eventos activados, iniciar otra y comprobar que no queda ningún evento marcado o pendiente de la partida anterior.

## 18. Registro de incidencias

Para cada fallo anotar:

- Apartado y casilla concreta.
- Pasos mínimos para reproducirlo.
- Resultado esperado y resultado observado.
- Mapa utilizado.
- Sistema operativo, resolución y escala de interfaz.
- Captura de pantalla y detalle técnico, si existe.
