# Plan de usabilidad: Nectar Map Maker

Plan reducido para convertir el editor de mapas VS en una herramienta cómoda
para usuarios y modders sin abordar todavía un nuevo formato de mapas, paquetes,
generación procedural ni cambios importantes en las reglas del juego.

Este documento complementa `PLAN_EDITOR_MAPAS.md` y
`PLAN_EDITOR_MAPAS_2.md`. Su objetivo es mejorar el flujo de creación actual,
manteniendo `map.json` formato 1 y la compatibilidad existente con el juego.

---

## 1. Objetivo

La versión resultante debe permitir:

1. Encontrar rápidamente cualquier objeto utilizable.
2. Seleccionar y transformar elementos con precisión.
3. Entender todo lo que contiene el mapa.
4. Detectar por qué un mapa no funciona.
5. Ver la colisión y las rutas reales de los Pikmin.
6. Editar terreno y construir rampas sin ensayo y error constante.

La promesa principal del editor será:

> Encontrar objetos, colocarlos con precisión y saber si el mapa funcionará.

---

## 2. Alcance

### Incluido

- Interfaz adaptable mediante divisores redimensionables.
- Explorador de assets con búsqueda, filtros, favoritos y recientes.
- Jerarquía de todos los elementos colocados.
- Inspector reorganizado y edición múltiple básica.
- Selección rectangular, multiselección y selección de objetos solapados.
- Gizmos 3D para mover, rotar y escalar.
- Validación estructurada con un conjunto limitado de arreglos automáticos.
- Visualización de colisión, rutas, pendientes y agua.
- Informe técnico del mapa y log detallado de compilación.
- Pinceles de terreno con forma y caída configurables.
- Herramienta para crear rampas rectas transitables.

### Fuera de alcance

- Formato `map.json` v2.
- Paquetes `.nmap` o `.nectarmap`.
- Manifiestos, dependencias y assets personalizados.
- Generación procedural de mapas.
- Prefabs persistentes.
- Edición manual de la red de navegación.
- Mapas de calor y analizador competitivo avanzado.
- Reglas de partida almacenadas dentro del mapa.
- Historial visual de acciones.
- Sistema completo de docking.
- Selección por lazo o copia de regiones de terreno.
- Mezcla avanzada, rotación o desplazamiento UV de materiales.

Estas exclusiones son deliberadas: reducen el riesgo y evitan modificar a la
vez el editor, el formato, el juego y el netplay.

---

## 3. Principios técnicos

- Conservar `MapDoc`, `ItemRef` y `map.json` formato 1 durante este plan.
- Seguir compartiendo compilación y validación mediante
  `libs/nectar_mapcore`.
- Todo cambio editable debe seguir funcionando con el undo/redo existente.
- Los textos nuevos deben existir en español e inglés mediante `tr()`.
- Las herramientas visuales nunca deben cambiar silenciosamente el mapa.
- El resultado compilado será la fuente de verdad para colisión y rutas.
- Evitar que `app.cpp` siga creciendo: cada panel nuevo tendrá su módulo.

Organización propuesta:

```text
tools/mapmaker/src/
├── app.cpp
├── app.h
├── ui/
│   ├── asset_browser.cpp
│   ├── hierarchy.cpp
│   ├── inspector.cpp
│   ├── diagnostics.cpp
│   └── technical_report.cpp
├── edit/
│   ├── selection.cpp
│   ├── gizmo.cpp
│   ├── terrain_brush.cpp
│   └── ramp_tool.cpp
└── view/
    ├── picking.cpp
    └── debug_overlays.cpp
```

---

## 4. Orden de implementación

### Fase 0 — Pruebas de regresión

**Duración estimada:** 3-4 días.

Antes de cambiar la interacción:

- Añadir pruebas de abrir, guardar y volver a abrir mapas.
- Probar borrado, duplicación, multiselección y simetría.
- Crear mapas de prueba con terreno, agua, muros, props y escenarios.
- Guardar métricas conocidas de Impact Arena.
- Comprobar que el editor y el juego producen el mismo mapa compilado.

Archivos propuestos:

```text
libs/nectar_mapcore/tests/map_roundtrip_test.cpp
tools/mapmaker/tests/document_test.cpp
tools/mapmaker/tests/fixtures/
```

**Criterio de aceptación:** Impact Arena y los mapas de prueba se abren,
guardan, compilan y juegan sin cambios inesperados.

---

### Fase 1 — Interfaz adaptable

**Duración estimada:** 3-4 días.

**Estado:** en curso. Implementados los divisores laterales redimensionables,
la persistencia de sus tamaños y el menú para ocultar, recuperar o restablecer
los paneles.

No se añadirá docking completo. La disposición seguirá controlada por el
editor, pero tendrá divisores ajustables.

Implementar:

- Divisor entre herramientas y visor.
- Divisor entre visor y panel derecho.
- Divisor vertical para diagnóstico o informe técnico.
- Anchura y altura mínimas de cada panel.
- Botones para ocultar jerarquía, minimapa y diagnóstico.
- Guardado de tamaños y visibilidad en `Config`.
- Escala global de interfaz configurable.

Resoluciones obligatorias:

- 1280 x 720.
- 1600 x 900.
- 1920 x 1080.
- Escalado 125 %, 150 % y 200 %.

**Criterio de aceptación:** ninguna herramienta queda inaccesible y el visor
mantiene un tamaño útil en todas las resoluciones probadas.

---

### Fase 2 — Selección y gizmos 3D

**Duración estimada:** 1-2 semanas.

**Estado:** en curso. Implementadas la multiselección mediante rectángulo, la
selección aditiva con Ctrl, el ciclo de elementos superpuestos con Alt, el
filtro para ignorar decoración y el gizmo de movimiento restringido a los
ejes X/Z. También están implementados el aro de rotación sobre Y y la escala
uniforme para tipos compatibles, con pivote común, ajustes y atajos W/E/R.
El movimiento permite alternar entre ejes globales y ejes locales orientados
según el objeto principal de la selección. La selección compatible también
puede colocarse directamente sobre la altura del terreno.

Es la fase con mayor impacto directo en la comodidad del editor.

#### Selección

- Clic para seleccionar.
- Ctrl para añadir o quitar de la selección.
- Arrastre sobre un espacio vacío para selección rectangular.
- Alt+clic, o clic repetido, para recorrer objetos solapados.
- Filtro para ignorar decoración durante la selección.
- Bloqueo temporal de elementos desde la jerarquía.
- Contorno o resaltado claro de toda la selección.

La primera versión puede conservar el picking por rayos existente. Si no
resulta fiable con objetos pequeños o solapados, se añadirá una pasada de
picking por ID en una textura `GL_R32UI`.

#### Gizmos

Integrar ImGuizmo como dependencia vendorizada y fijada a una versión.

- `W`: mover.
- `E`: rotar.
- `R`: escalar.
- Coordenadas locales o globales.
- Pivote en el centro de la selección.
- Ajuste a la cuadrícula.
- Ajuste angular.
- Colocar sobre el terreno.
- Transformación conjunta de varios elementos.
- Escala únicamente en tipos que la soporten.

Cada arrastre completo debe generar una sola entrada de undo.

**Criterio de aceptación:** se pueden seleccionar varios objetos solapados,
moverlos, rotarlos y deshacer toda la operación de una sola vez.

---

### Fase 3 — Jerarquía

**Duración estimada:** 3-4 días.

**Estado:** en curso. Implementado el árbol por categorías con contadores,
selección sincronizada, multiselección con Ctrl, enfoque con doble clic y
búsqueda instantánea sin distinguir mayúsculas ni acentos. También se pueden
ocultar objetos visuales y bloquear la selección durante la sesión; la
geometría horneada de muros y agua solo admite bloqueo por ahora. El menú
contextual permite seleccionar todos los elementos del mismo tipo.

Añadir un panel que muestre todo el contenido del mapa:

```text
Mapa
├── Jugador 1
├── Jugador 2
├── Objetivos
├── Enemigos
├── Construcciones
├── Muros y agua
├── Decoración
└── Elementos del escenario
```

Funciones:

- Selección sincronizada con el visor.
- Buscar por nombre.
- Ocultar temporalmente.
- Bloquear selección.
- Seleccionar todos los elementos del mismo tipo.
- Doble clic para centrar la cámara.
- Contadores por categoría.

Ocultación y bloqueo serán datos de la sesión del editor y no se escribirán
en `map.json`.

Para mantener `ItemRef` por índice, cualquier borrado deberá llamar a
`pruneSelection()` y reconstruir la lista en el mismo fotograma.

**Criterio de aceptación:** cualquier elemento puede encontrarse y
seleccionarse aunque esté oculto detrás de otro objeto en el visor.

---

### Fase 4 — Inspector de propiedades

**Duración estimada:** 4-5 días.

**Estado:** en curso. Añadida una sección común de transformación para la
selección individual, con edición numérica precisa de posición, rotación y
escala según las capacidades reales de cada tipo de elemento. También permite
copiar, pegar y restablecer transformaciones sin aplicar al destino propiedades
que su tipo no admite. La selección múltiple puede mover numéricamente su
centro y editar altura, giro o escala cuando todos los elementos comparten esa
capacidad; los valores diferentes se indican explícitamente.

Reorganizar el inspector actual en secciones:

1. Transformación.
2. Jugabilidad.
3. Apariencia.
4. Colisión.
5. Simetría.
6. Información técnica.

Añadir:

- Edición numérica de posición, rotación y escala.
- Copiar y pegar transformación.
- Restablecer valores.
- Botón para colocar sobre el terreno.
- Botón para enfocar la selección.
- Edición múltiple de los campos comunes.
- `—` cuando varios elementos tienen valores distintos.
- Ayuda contextual para propiedades específicas.

No se creará todavía un sistema genérico de reflexión de propiedades. Cada
tipo continuará teniendo una función explícita y comprobable.

**Criterio de aceptación:** todas las propiedades existentes siguen siendo
editables y los campos comunes funcionan sobre selecciones múltiples.

---

### Fase 5 — Explorador de assets

**Duración estimada:** 1 semana.

**Estado:** en curso. La decoración permite buscar por nombre amigable, ruta y
categoría sin distinguir mayúsculas ni acentos; alternar entre cuadrícula y
lista; guardar favoritos y recientes; arrastrar al visor; y consultar ruta,
triángulos, colisión y dimensiones de los modelos del catálogo avanzado.
También hay filtros por categoría, coste y presencia de colisión, con avisos
para modelos especialmente grandes o pesados.

Ampliar el catálogo existente, sin sustituir su sistema de caché.

Funciones:

- Búsqueda por nombre amigable, ruta y nombre técnico.
- Búsqueda sin distinguir mayúsculas o acentos.
- Categorías: objetos, plantas, enemigos, piezas, estructuras y otros.
- Filtros por tipo, colisión y coste aproximado.
- Favoritos persistentes.
- Elementos utilizados recientemente.
- Vista de cuadrícula y lista.
- Miniatura, nombre y ruta.
- Número de triángulos, dimensiones y colisión.
- Arrastrar al visor para colocar.
- Aviso para assets muy grandes, pesados o con error.

La indexación y generación de miniaturas debe continuar en segundo plano.

**Criterio de aceptación:** un usuario puede encontrar y colocar cualquier
asset conocido sin navegar manualmente por rutas del juego.

---

### Fase 6 — Diagnósticos y arreglos automáticos

**Duración estimada:** 1 semana.

**Estado:** en curso. Los problemas de elementos fuera del mapa o situados
sobre huecos ofrecen arreglos seguros para llevarlos dentro o buscar el suelo
válido más cercano. Las operaciones son deshacibles y consideran la copia
simétrica.

Separar la detección de problemas de su presentación visual.

Estructura propuesta:

```cpp
enum class DiagnosticSeverity { Info, Warning, Error };

struct Diagnostic {
    DiagnosticSeverity severity;
    std::string summary;
    std::string details;
    ItemRef item;
    bool hasItem;
    FixKind fix;
};
```

Comprobaciones obligatorias:

- Elementos requeridos.
- Elementos fuera del mapa.
- Elementos sin suelo debajo.
- Assets que no existen o no se pueden leer.
- Límites de piezas, pastillas, enemigos y construcciones.
- Escala inválida.
- Copia simétrica problemática.
- Objetivos sin ruta alcanzable.
- Presupuesto de geometría excedido.

Arreglos permitidos:

- Mover dentro del mapa.
- Colocar sobre el terreno.
- Crear suelo debajo.
- Borrar elemento inválido.
- Regenerar su copia simétrica.
- Llevar la cámara al problema.

Cada arreglo debe ser deshacible. No habrá un botón de «arreglar todo» sin
previsualización.

**Criterio de aceptación:** cada error bloqueante indica qué elemento lo
causa y ofrece una acción concreta cuando puede resolverse de forma segura.

---

### Fase 7 — Colisión y navegación

**Duración estimada:** 1 semana.

**Estado:** en curso. El menú Vista ya permite superponer la colisión
compilada, la red real de rutas, las pendientes no transitables, el agua y las
etiquetas de objetos. Las componentes desconectadas se colorean por separado,
la selección muestra el camino desde la base y los objetivos inaccesibles se
incorporan a las comprobaciones.

Usar `PreviewResult::routes` y la geometría compilada como fuentes de verdad.

Modos del visor:

- Visual normal.
- Colisión.
- Rutas.
- Pendientes.
- Agua.

Funciones:

- Dibujar nodos y enlaces de navegación.
- Colorear componentes desconectados.
- Mostrar la ruta desde una base hasta un objetivo seleccionado.
- Marcar piezas y pastillas inaccesibles.
- Señalar el tramo en el que se corta una ruta.
- Mostrar pendientes no transitables.
- Mostrar objetos que aportan colisión.

No se editarán manualmente nodos o enlaces en esta fase.

**Criterio de aceptación:** las rutas mostradas deben ser exactamente las
generadas por el compilador que utiliza el juego.

---

### Fase 8 — Informe técnico y consola

**Duración estimada:** 3-4 días.

**Estado:** en curso. Añadida una pestaña de informe con estado jugable,
presupuestos principales, recuentos del mapa y detalle de compilación copiable.

Mostrar los datos que ya produce `PreviewResult`:

- Vértices.
- Triángulos de colisión.
- Puntos de ruta.
- Props.
- Tamaño compilado.
- Piezas, pastillas, enemigos y construcciones.
- Assets únicos.
- Estado jugable.

Presentar cada presupuesto con estado verde, amarillo o rojo.

Cuando falle la compilación, mostrar:

- Resumen comprensible.
- Detalle técnico desplegable.
- Asset o elemento implicado, si se conoce.
- Ruta del archivo.
- Botón para copiar el log.
- Botón para localizar el elemento.

**Criterio de aceptación:** un modder puede identificar si el problema está
en geometría, navegación, un asset o los límites del juego.

---

### Fase 9 — Pinceles de terreno

**Duración estimada:** 1 semana.

**Estado:** en curso. Los pinceles de suelo, altura, pintura, agua, plataformas
y decoración comparten tamaño, forma circular/cuadrada y borde suave/duro. La
previsualización refleja la forma y Pintar permite tomar material con Alt+clic.

Refactorizar `applyBrush()` alrededor de una configuración común:

```cpp
struct BrushSettings {
    BrushShape shape;
    BrushFalloff falloff;
    float radius;
    float strength;
};
```

Opciones:

- Forma circular o cuadrada.
- Borde duro o suave.
- Tamaño.
- Fuerza.
- Previsualización del área afectada.
- Cuentagotas de material.

Las mismas opciones se reutilizarán en:

- Elevar y bajar.
- Aplanar.
- Suavizar.
- Añadir o quitar suelo.
- Agua.
- Pintura de materiales.

Una pincelada completa debe ser una única operación de undo. Si las copias
completas de `MapDoc` causan problemas de memoria, se implementará un delta
rectangular únicamente para el terreno.

**Criterio de aceptación:** el borde suave permite construir pendientes sin
escalones visibles y el editor permanece fluido con terrenos grandes.

---

### Fase 10 — Herramienta de rampas

**Duración estimada:** 4-5 días.

**Estado:** implementada una primera versión recta. Permite marcar inicio y
final, ajustar anchura, elegir perfil lineal o suavizado, previsualizar el área,
avisar si la pendiente supera el límite transitable y aplicar el cambio como
una única operación deshacible, incluida la simetría del terreno.

Flujo:

1. Marcar el borde inicial.
2. Marcar el borde final.
3. Ajustar la anchura.
4. Mostrar una previsualización.
5. Aplicar la rampa.

La primera versión solo generará rampas:

- Rectas.
- De anchura constante.
- Con perfil lineal o suavizado.
- Integradas con las alturas de ambos extremos.
- Con material existente conservado.

El editor calculará la pendiente y avisará si supera el límite transitable.
La operación completa será deshacible.

No se incluyen rampas curvas, escaleras ni barandillas automáticas.

**Criterio de aceptación:** una rampa marcada como transitable conecta ambos
niveles en la navegación compilada.

---

### Fase 11 — Pruebas, traducción y documentación

**Duración estimada:** 1 semana.

**Estado:** en curso. Todos los textos añadidos tienen traducción española e
inglesa, la ayuda integrada menciona diagnósticos, capas y rampas, y
`PRUEBAS_EDITOR_MAPAS_USABILIDAD.md` reúne la matriz manual por funciones.

- Pruebas del editor y `nectar_mapcore`.
- Pruebas de mapas grandes y selecciones numerosas.
- Windows y Linux.
- Español e inglés.
- Escalado DPI.
- Actualizar tutorial y ayuda.
- Documentar selección, gizmos, rutas y rampas.
- Probar todos los mapas incluidos y varias creaciones de usuarios.

**Criterio de aceptación:** todas las funciones nuevas tienen traducción,
ayuda visible y una prueba manual reproducible.

---

## 5. Cronograma estimado

| Bloque | Duración |
|---|---:|
| Regresión e interfaz adaptable | 1 semana |
| Selección y gizmos | 1-2 semanas |
| Jerarquía e inspector | 1-2 semanas |
| Explorador de assets | 1 semana |
| Diagnósticos, rutas e informe | 2 semanas |
| Pinceles y rampas | 1-2 semanas |
| QA, traducción y documentación | 1 semana |
| **Total estimado** | **8-9 semanas** |

La estimación supone una persona trabajando de forma continuada y no incluye
cambios inesperados en el juego o en los formatos de assets.

---

## 6. Hitos utilizables

### Hito A — Edición precisa

Fases 0-4:

- Interfaz adaptable.
- Selección fiable.
- Gizmos.
- Jerarquía.
- Inspector.

El editor ya será notablemente más cómodo aunque no estén terminadas las
herramientas de diagnóstico.

### Hito B — Editor útil para modders

Fases 5-8:

- Explorador de assets.
- Diagnósticos.
- Navegación y colisión.
- Informe técnico.

En este punto se puede entregar una versión pública intermedia.

### Hito C — Terreno cómodo

Fases 9-11:

- Pinceles mejorados.
- Rampas.
- QA y documentación.

Este hito cierra el plan reducido.

---

## 7. Prioridad si hay que recortar todavía más

Si no fuera posible completar las 8-9 semanas, el mínimo recomendado es:

1. Selección y gizmos 3D.
2. Jerarquía.
3. Explorador de assets.
4. Visualización de rutas y colisión.

Estas cuatro mejoras proporcionan el mayor aumento de utilidad con el menor
impacto sobre el formato del mapa y el juego.

---

## 8. Condiciones para ampliar el alcance

Solo después de completar y estabilizar este plan se debería reconsiderar:

- Formato `map.json` v2 e IDs persistentes.
- Prefabs.
- Paquetes compartibles.
- Generación procedural.
- Reglas por mapa.
- Analizador competitivo y mapas de calor.
- Edición manual de navegación.

Cada una de esas funciones debe tener un plan separado para evitar que el
editor vuelva a crecer como un único bloque difícil de mantener.
