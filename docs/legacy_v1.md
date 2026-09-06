# OBSOLETO — Oasis Engine v0.1 — NO UTILIZAR COMO ESPECIFICACIÓN ACTUAL

> **OBSOLETO — Oasis Engine v0.1. Fuente histórica únicamente.**
> **La especificación actual es `README.md` + `docs/REBUILD_0_2.md`.**
> **Implementación actual: C++17 (`src/main.cpp`, `src/core.cpp`, `src/assets.cpp`,
> `src/runtime.cpp`, `src/renderer_win32_d3d11.cpp`) + cJSON vendored.**
> **Este archivo conserva la visión v0.1 (C11) y auditorías 2026-09-05 como historia.
> No describe el código actual.**

# Oasis Engine — Documentación Viva (HISTÓRICA v0.1)

> **Fuente de verdad desde 2026-09-05:** la auditoría técnica al final de este archivo prevalece sobre las secciones conceptuales históricas y sobre cualquier etiqueta anterior de “IMPLEMENTADO”. Conserva la visión útil, pero describe el estado real comprobado en código y ejecución.

> Documento maestro de contexto, arquitectura y dirección del proyecto.
>
> Este archivo es la fuente principal de contexto para cualquier IA o desarrollador que trabaje en Oasis Engine.
>
> **Importante:** este documento es vivo. No se deben crear múltiples documentos de contexto para cada funcionalidad. A medida que Oasis Engine crezca, se deben agregar o actualizar secciones dentro de este mismo archivo.

---

# 1. ¿Qué es Oasis Engine?

**Oasis Engine** es un motor de videojuegos/engine gráfico diseñado desde el principio para poder ser utilizado tanto por humanos como por **agentes de inteligencia artificial**.

La idea central no es competir inicialmente con motores enormes como Unreal Engine o Unity.

El objetivo es construir un motor relativamente pequeño, controlable mediante interfaces programáticas y suficientemente estructurado para que una IA pueda:

- crear proyectos;
- crear escenas;
- crear entidades;
- modificar entidades;
- agregar componentes;
- importar assets;
- consultar el estado del proyecto;
- ejecutar el juego;
- comprobar resultados;
- modificar nuevamente el proyecto;
- y repetir el ciclo.

La característica diferencial de Oasis Engine es que la IA no debe tratar al motor como una caja negra.

Debe poder **observar, actuar y verificar**.

---

# 2. Idea principal

La filosofía fundamental de Oasis Engine es:

> **Un motor gráfico cuyo mundo puede ser entendido, inspeccionado y modificado mediante herramientas diseñadas para agentes de IA.**

Un agente debería poder trabajar de una forma similar a un agente de programación.

Ejemplo conceptual:

```text
1. Inspeccionar el proyecto.
2. Entender su estado actual.
3. Decidir qué necesita cambiar.
4. Ejecutar una operación.
5. Consultar nuevamente el estado.
6. Verificar que el cambio ocurrió correctamente.
7. Corregir si es necesario.
8. Continuar con la siguiente tarea.
```

No queremos que una IA simplemente genere archivos y espere que funcionen.

Queremos que pueda interactuar con el estado real de Oasis Engine.

---

# 3. Objetivo inicial

El objetivo inicial es construir una **V0.1 extremadamente básica pero funcional**.

No intentar construir un motor completo.

La primera versión debe demostrar que el concepto funciona.

La V0.1 debería poder:

1. Crear un proyecto.
2. Abrir un proyecto.
3. Crear una escena.
4. Crear entidades.
5. Agregar componentes.
6. Modificar componentes.
7. Guardar el estado del proyecto.
8. Cargar el proyecto.
9. Renderizar una escena básica.
10. Ejecutar el juego.
11. Consultar el estado del proyecto.
12. Modificar el estado mediante CLI/API.
13. Permitir que una IA opere sobre el motor.
14. Proporcionar información suficiente para que la IA pueda verificar sus acciones.

---

# 4. Lo que Oasis Engine NO debe intentar hacer todavía

Durante las primeras versiones NO se debe intentar construir:

- un reemplazo de Unreal Engine;
- un reemplazo de Unity;
- un editor visual completo;
- un sistema de física avanzado;
- multiplayer;
- networking complejo;
- iluminación avanzada;
- ray tracing;
- sistema de animación avanzado;
- editor de materiales avanzado;
- sistema de partículas complejo;
- scripting extremadamente complejo;
- cientos de formatos de assets;
- un sistema de plugins enorme;
- un ECS excesivamente complejo;
- herramientas profesionales de producción AAA.

Si una funcionalidad no es necesaria para demostrar el concepto principal, debe considerarse secundaria.

La prioridad es:

> **funcionalidad real > cantidad de funcionalidades.**

---

# 5. Principio de realismo

Oasis Engine debe construirse de manera realista.

No asumir que algo es fácil simplemente porque conceptualmente parece sencillo.

Cuando una idea sea demasiado grande para el estado actual del proyecto:

1. indicarlo claramente;
2. explicar por qué;
3. dividirla en partes;
4. implementar primero una versión alcanzable.

No se debe decir que una idea es buena o viable simplemente para complacer al usuario.

La IA que trabaje en Oasis Engine debe priorizar:

- viabilidad;
- simplicidad;
- estabilidad;
- mantenibilidad;
- pruebas;
- progreso incremental.

---

# 6. Arquitectura conceptual

La arquitectura inicial puede seguir aproximadamente este modelo:

```text
                    AI AGENT
                       │
                       ▼
                ┌─────────────┐
                │ Oasis Skill │
                └──────┬──────┘
                       │
                       ▼
              ┌─────────────────┐
              │ CLI / API / MCP │
              └────────┬────────┘
                       │
                       ▼
              ┌─────────────────┐
              │   OASIS CORE    │
              └────────┬────────┘
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
       Scenes        Assets       Runtime
          │            │            │
          └────────────┼────────────┘
                       ▼
                  Renderer
                       │
                       ▼
                      GPU
```

La arquitectura exacta puede cambiar durante el desarrollo.

Este diagrama representa la dirección conceptual, no una obligación de implementación exacta.

---

# 7. AI como ciudadano de primera clase

La IA no debe ser una característica añadida posteriormente.

Debe considerarse un usuario de primera clase del motor.

Oasis debería permitir que un agente pueda realizar operaciones como:

```text
crear proyecto
crear escena
crear entidad
agregar componente
modificar componente
eliminar entidad
guardar escena
cargar escena
consultar escena
ejecutar proyecto
detener proyecto
consultar errores
```

La interfaz concreta puede evolucionar.

---

# 8. Estado del juego/proyecto

Una parte fundamental del proyecto es permitir que la IA pueda consultar el estado.

Por ejemplo:

```json
{
  "scene": "main",
  "entities": [
    {
      "id": "player",
      "components": [
        "Transform",
        "Mesh",
        "Camera"
      ],
      "transform": {
        "position": [0, 0, 0],
        "rotation": [0, 0, 0],
        "scale": [1, 1, 1]
      }
    }
  ]
}
```

Esto es solamente un ejemplo conceptual.

El formato real debe definirse durante la implementación.

La regla importante es:

> La IA debe poder consultar información estructurada del estado actual.

---

# 9. Modelo mental: observar → actuar → verificar

El flujo principal para un agente debería ser:

```text
OBSERVE
   │
   ▼
THINK
   │
   ▼
ACT
   │
   ▼
OBSERVE
   │
   ▼
VERIFY
   │
   ├── correcto ──► continuar
   │
   └── incorrecto ─► corregir
```

Ejemplo:

```text
IA:
"Necesito crear un jugador."

↓

Consulta el estado.

↓

No existe player.

↓

Crea player.

↓

Consulta nuevamente.

↓

player existe.

↓

Agrega Transform.

↓

Consulta nuevamente.

↓

Transform existe.

↓

Continúa.
```

Este patrón debe influir en el diseño de la API.

---

# 10. CLI

La CLI será una de las interfaces principales de Oasis Engine.

La sintaxis todavía puede cambiar, pero conceptualmente podría ser:

```bash
oasis project create my-game
oasis project open my-game

oasis scene create main
oasis scene open main

oasis entity create player
oasis entity add-component player transform

oasis entity set player.position 0 0 0

oasis scene save

oasis run
oasis build

oasis state
```

Estos comandos son ejemplos conceptuales.

No deben implementarse todos inmediatamente.

Primero debe construirse la infraestructura necesaria para que una pequeña cantidad de comandos funcione correctamente.

---

# 11. API

Además de CLI, Oasis Engine debería tener una interfaz programática.

La razón es que una IA puede trabajar mejor con operaciones estructuradas que con comandos destinados únicamente a humanos.

Conceptualmente:

```text
create_project()
create_scene()
create_entity()
add_component()
remove_component()
set_component()
get_state()
save()
load()
run()
stop()
```

La implementación concreta dependerá de la arquitectura elegida.

---

# 12. Oasis Skill

Uno de los componentes importantes del proyecto será **Oasis Skill**.

La Skill debe proporcionar a un agente el conocimiento necesario para utilizar Oasis Engine correctamente.

No debe ser solamente una lista de comandos.

Debe enseñar al agente:

- qué puede hacer;
- cómo inspeccionar un proyecto;
- cómo consultar el estado;
- cómo modificarlo;
- cómo verificar cambios;
- cómo reaccionar ante errores;
- cuáles son las limitaciones actuales;
- cuáles son las operaciones disponibles;
- qué estructura utiliza Oasis.

Ejemplo conceptual:

```text
Antes de modificar un proyecto:

1. Consulta el estado actual.
2. Identifica las entidades existentes.
3. Comprueba los componentes disponibles.
4. Ejecuta la modificación.
5. Consulta nuevamente el estado.
6. Verifica el resultado.
```

La Skill debe evolucionar junto con el motor.

---

# 13. Documentación

La documentación debe mantenerse en **un único archivo maestro**.

Este archivo.

No crear un archivo independiente de documentación para cada nueva funcionalidad durante la etapa inicial.

Cuando Oasis Engine incorpore una nueva característica:

- actualizar una sección existente si corresponde;
- o agregar una nueva sección al final.

Ejemplo:

```markdown
## 18. Physics
```

Después:

```markdown
## 19. Audio
```

Después:

```markdown
## 20. Animation
```

El documento debe crecer junto con Oasis Engine.

---

# 14. Documentación como fuente de verdad

La documentación debe describir el comportamiento público real de Oasis Engine.

No documentar funcionalidades que todavía no existen como si fueran funcionales.

Si algo está planeado pero todavía no implementado:

```text
Estado: PLANIFICADO
```

Si está parcialmente implementado:

```text
Estado: EN DESARROLLO
```

Si funciona:

```text
Estado: IMPLEMENTADO
```

Si fue descartado:

```text
Estado: DESCARTADO
```

Esto evita que una IA futura confunda planes con funcionalidades reales.

---

# 15. Arquitectura interna inicial

La arquitectura interna debe mantenerse simple.

Una posible separación conceptual:

```text
Oasis
│
├── Core
│   ├── Project
│   ├── Scene
│   ├── Entity
│   └── Component
│
├── Runtime
│
├── Renderer
│
├── Assets
│
├── CLI
│
├── API
│
└── AI
    └── Skill
```

Esto es una guía inicial.

No crear capas innecesarias simplemente por seguir una arquitectura teórica.

---

# 16. Project

Un proyecto de Oasis Engine representa un juego.

Debe contener como mínimo la información necesaria para:

- identificar el proyecto;
- guardar escenas;
- guardar configuración;
- localizar assets;
- ejecutar el proyecto.

Ejemplo conceptual:

```text
my-game/
├── oasis.project
├── scenes/
├── assets/
└── ...
```

La estructura definitiva debe decidirse durante la implementación.

---

# 17. Scene

Una escena contiene el estado de un mundo.

Conceptualmente:

```text
Scene
│
├── Entity
│   ├── Component
│   ├── Component
│   └── Component
│
├── Entity
│   └── ...
│
└── ...
```

La primera escena no necesita ser compleja.

Una escena básica con:

- cámara;
- luz;
- objeto;

ya sería suficiente para comenzar a demostrar el renderer.

---

# 18. Entity

Una entidad representa un objeto dentro del mundo.

Ejemplo:

```text
player
enemy
camera
light
cube
environment
```

La entidad inicialmente debe ser sencilla.

No agregar complejidad innecesaria.

---

# 19. Components

Los componentes contienen propiedades o comportamientos asociados a una entidad.

Los primeros componentes podrían incluir:

```text
Transform
Mesh
Camera
Light
```

Más adelante podrían aparecer:

```text
Material
Collider
RigidBody
AudioSource
Script
Animation
```

Pero no deben implementarse hasta que sean necesarios.

---

# 20. Transform

Transform debería representar como mínimo:

```text
position
rotation
scale
```

Ejemplo conceptual:

```json
{
  "position": [0, 0, 0],
  "rotation": [0, 0, 0],
  "scale": [1, 1, 1]
}
```

---

# 21. Renderer

El renderer inicial debe ser extremadamente simple.

El objetivo inicial no es crear gráficos impresionantes.

El objetivo es demostrar:

```text
Scene
  ↓
Entities
  ↓
Components
  ↓
Renderer
  ↓
GPU
  ↓
Image
```

Una primera escena con primitivas simples es suficiente.

Por ejemplo:

```text
Cube
Camera
Light
```

Si esto funciona correctamente, ya existe una base gráfica real.

---

# 22. Assets

Oasis Engine eventualmente deberá manejar:

- modelos;
- texturas;
- materiales;
- sonidos;
- escenas;
- otros recursos.

Pero el sistema inicial debe soportar únicamente lo necesario para demostrar el motor.

No intentar soportar todos los formatos desde el comienzo.

---

# 23. Runtime

El runtime es la parte que ejecuta el juego.

Conceptualmente:

```text
Load Project
     ↓
Load Scene
     ↓
Initialize Runtime
     ↓
Initialize Renderer
     ↓
Game Loop
     ↓
Render
     ↓
Update
     ↓
Repeat
```

La implementación inicial debe ser sencilla.

---

# 24. Game Loop

El loop inicial puede seguir una estructura tradicional:

```text
while running:

    process_input()

    update()

    render()
```

No agregar sistemas complejos hasta que exista una razón real para hacerlo.

---

# 25. Error handling

Los errores deben ser comprensibles para humanos y agentes.

Ejemplo malo:

```text
Error 0x839201
```

Preferible:

```text
Entity 'player' does not exist.

Available entities:
- camera
- cube
- light
```

Esto es especialmente importante porque una IA necesita información útil para poder corregirse.

---

# 26. Estado consultable

El motor debería permitir obtener información estructurada sobre:

```text
Proyecto
Escena
Entidades
Componentes
Assets
Errores
Runtime
```

La información debe estar diseñada pensando tanto en:

- humanos;
- scripts;
- agentes de IA.

---

# 27. Persistencia

Los cambios realizados por CLI/API deben poder guardarse.

Ejemplo:

```text
AI
 ↓
create entity
 ↓
modify transform
 ↓
save
 ↓
project files updated
```

Al reiniciar Oasis, el estado debe poder recuperarse.

---

# 28. Seguridad

Durante la fase inicial, la prioridad es construir el motor y validar el concepto.

No se debe diseñar una arquitectura de seguridad excesivamente compleja antes de que exista el producto básico.

Sin embargo, cualquier operación destructiva o peligrosa deberá poder recibir controles posteriormente.

La seguridad será una preocupación importante antes de permitir agentes externos o ejecución de código arbitrario en entornos reales.

---

# 29. Filosofía de desarrollo

Oasis Engine debe construirse de forma incremental.

Cada etapa debería producir algo funcional.

Ejemplo:

```text
V0.0.1
→ proyecto básico

V0.0.2
→ escenas

V0.0.3
→ entidades/componentes

V0.0.4
→ renderer

V0.0.5
→ CLI

V0.0.6
→ state inspection

V0.0.7
→ API

V0.0.8
→ AI Skill
```

Las versiones son solamente un ejemplo.

No es obligatorio seguir exactamente esta numeración.

---

# 30. Regla de implementación

Antes de implementar una funcionalidad nueva:

1. comprobar el estado real del código;
2. comprobar qué sistemas ya existen;
3. evitar duplicar funcionalidades;
4. reutilizar componentes existentes cuando sea apropiado;
5. implementar la versión mínima;
6. probarla;
7. actualizar esta documentación.

Nunca asumir que algo está implementado solamente porque aparece mencionado en este documento.

El código real es la autoridad sobre el estado de implementación.

---

# 31. Regla contra la sobreingeniería

No implementar una arquitectura de producción AAA para resolver un problema que todavía no existe.

Preferir:

```text
simple
→ funcional
→ probado
→ extensible
```

en lugar de:

```text
complejo
→ abstracto
→ difícil de probar
→ todavía sin uso
```

---

# 32. Cómo debe trabajar Luna con Oasis Engine

Cuando una IA trabaje en Oasis Engine debe seguir aproximadamente este proceso:

```text
1. Leer este documento.

2. Inspeccionar el proyecto real.

3. Determinar qué existe realmente.

4. Comparar el estado real con este documento.

5. Identificar el siguiente objetivo pequeño.

6. Diseñar la implementación mínima.

7. Implementar.

8. Ejecutar pruebas.

9. Corregir errores.

10. Verificar que la funcionalidad funciona realmente.

11. Actualizar este documento.

12. Continuar con el siguiente objetivo.
```

No saltar directamente a implementar grandes sistemas sin inspeccionar primero el estado actual.

---

# 33. Regla importante para la IA

La IA debe distinguir siempre entre:

```text
EXISTE
```

```text
ESTÁ PARCIALMENTE IMPLEMENTADO
```

```text
ESTÁ PLANIFICADO
```

```text
NO EXISTE
```

Nunca presentar una funcionalidad planificada como implementada.

---

# 34. Roadmap inicial

## Fase 0 — Base

Objetivo:

Crear la estructura mínima del proyecto.

Debe existir:

- proyecto;
- configuración;
- ejecución básica;
- estructura interna limpia.

---

## Fase 1 — Scene System

Implementar:

- Scene;
- Entity;
- Component;
- Transform;
- guardar;
- cargar.

---

## Fase 2 — Renderer

Implementar un renderer básico.

Objetivo mínimo:

```text
Window
+
Camera
+
Cube
+
Light
```

---

## Fase 3 — CLI

Permitir controlar el motor desde terminal.

Ejemplo:

```bash
oasis project create
oasis scene create
oasis entity create
oasis entity set
oasis state
oasis run
```

---

## Fase 4 — State Interface

Permitir consultar el estado de forma estructurada.

Ejemplo:

```text
get_scene_state()
get_entity()
get_component()
get_project_state()
```

---

## Fase 5 — API

Exponer operaciones programáticas.

La API debe permitir que herramientas externas interactúen con Oasis.

---

## Fase 6 — Oasis Skill

Crear la Skill oficial para agentes.

Debe contener:

- capacidades;
- comandos;
- formatos;
- workflow;
- reglas;
- verificación;
- manejo de errores.

---

## Fase 7 — Agent Workflow

Probar un agente real realizando tareas como:

```text
"Create a scene with a cube."

"Move the cube to x=5."

"Create a camera."

"Add a light."

"Run the game."

"Inspect the current scene."
```

El objetivo es comprobar que el concepto completo funciona.

---

# 35. Primer objetivo demostrable

Antes de agregar características grandes, Oasis Engine debería poder lograr algo parecido a esto:

```text
Usuario:

"Crea un proyecto llamado TestGame."

↓

Oasis crea el proyecto.

↓

"Crea una escena llamada Main."

↓

Oasis crea Main.

↓

"Crea un cubo."

↓

Oasis crea Cube.

↓

"Pon el cubo en 0, 0, 0."

↓

Oasis modifica Transform.

↓

"Ejecuta el juego."

↓

Oasis abre el runtime.

↓

La escena muestra el cubo.
```

Después:

```text
IA:
"Consulta el estado."

↓

Oasis:

Scene: Main

Entities:
- Camera
- Cube
- Light

Cube:
Position: [0,0,0]
```

Ese flujo demostraría la idea central de Oasis Engine.

---

# 36. Qué hace especial a Oasis Engine

El objetivo no es simplemente:

> "crear otro motor gráfico."

El objetivo es crear:

> **un entorno de creación de juegos que pueda ser operado directamente por agentes de IA.**

La diferencia está en que el motor debe ser:

```text
Programable
+
Observable
+
Estructurado
+
Determinista cuando sea posible
+
Controlable mediante herramientas
+
Comprensible para agentes
```

---

# 37. Principio fundamental

La arquitectura de Oasis Engine debe facilitar este ciclo:

```text
        ┌───────────────┐
        │    OBSERVE    │
        └───────┬───────┘
                ↓
        ┌───────────────┐
        │     THINK     │
        └───────┬───────┘
                ↓
        ┌───────────────┐
        │      ACT      │
        └───────┬───────┘
                ↓
        ┌───────────────┐
        │    VERIFY     │
        └───────┬───────┘
                │
          ┌─────┴─────┐
          ↓           ↓
       Correcto     Error
          │           │
          ↓           └──────► corregir
       Continuar
```

Este ciclo debe considerarse una de las ideas centrales de Oasis Engine.

---

# 38. Estado actual del proyecto

**Estado conceptual:**

Oasis Engine está en la etapa inicial de diseño y construcción.

La prioridad actual es crear una base mínima y funcional.

No asumir que las características descritas anteriormente ya están implementadas.

A medida que se construya el proyecto, esta sección debe actualizarse.

Formato recomendado:

```text
Estado:
EN DESARROLLO

Versión:
V0.2

Objetivo actual:
[describir objetivo]

Última funcionalidad implementada:
[describir]

Siguiente objetivo:
[describir]

Problemas conocidos:
[describir]
```

---

# 39. Registro de cambios

Cada modificación importante de arquitectura o funcionalidad puede registrarse aquí.

Formato:

```text
## [FECHA] — [CAMBIO]

### Añadido
- ...

### Modificado
- ...

### Eliminado
- ...

### Motivo
- ...
```

No registrar absolutamente cada pequeño cambio de código.

Registrar únicamente cambios que afecten la arquitectura, API, comportamiento o dirección del proyecto.

---

# 40. Reglas para mantener este documento

1. Este archivo es el documento maestro.
2. No crear documentos de contexto duplicados.
3. Actualizar las secciones existentes cuando sea posible.
4. Crear una nueva sección únicamente cuando realmente sea necesaria.
5. No documentar funcionalidades inexistentes como implementadas.
6. Mantener separadas las ideas futuras de las funcionalidades actuales.
7. Mantener el documento actualizado después de cambios importantes.
8. Si una decisión de arquitectura cambia, actualizar la documentación anterior.
9. Si una funcionalidad se elimina, registrar el cambio.
10. Mantener este documento comprensible para otra IA que nunca haya visto el proyecto.

---

# 41. Visión a largo plazo

Si la base funciona correctamente, Oasis Engine podría evolucionar progresivamente hacia un entorno donde un agente pueda realizar tareas de desarrollo de videojuegos de extremo a extremo.

Por ejemplo:

```text
Usuario:
"Create a small platformer."

↓

AI Agent

↓

Oasis Skill

↓

Oasis API

↓

Create Project
Create Scenes
Create Entities
Add Components
Import Assets
Configure Game
Run
Inspect
Test
Fix
Repeat

↓

Juego funcional
```

Pero esta visión pertenece al largo plazo.

La prioridad actual sigue siendo:

> **Construir una base pequeña, real, funcional y extensible.**

---

# 42. Regla final

Cuando exista una decisión entre:

**A)** construir una funcionalidad grande y compleja;

o

**B)** construir una versión pequeña que funcione realmente;

Oasis Engine debe preferir:

**B.**

La complejidad puede agregarse después.

La base debe funcionar primero.

---

---

# 43. Estado real de implementación — V0.2

**Estado:** IMPLEMENTADO

La versión utilizable está implementada en C11. El binario se construye con CMake y Visual Studio Build Tools, sin dependencias de terceros.

Incluye:

- creación y apertura de proyectos;
- creación y apertura de escenas;
- creación de entidades;
- componentes `Transform`, `Mesh`, `Camera` y `Light`;
- modificación de propiedades mediante CLI;
- persistencia JSON en `oasis.project` y `scenes/*.scene.json`;
- consulta estructurada del estado;
- runtime nativo verificable mediante `run`;
- renderer visual Win32 de cubos alámbricos;
- mensajes de error orientados a humanos y agentes;
- prueba automatizada del flujo vertical principal mediante CTest.

El renderer actual es intencionalmente mínimo: una ventana Win32 y cubos alámbricos animados. No incluye aún un backend GPU moderno, importación de assets, física o scripting.

## 43.1 Uso rápido

Construcción desde la carpeta del motor:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Para medir rendimiento, construir y ejecutar `Release`:

```bash
cmake --build build --config Release
.\build\Release\oasis.exe run --project .\DemoGame --window
```

`Debug` se reserva para depuración; `Release` habilita las optimizaciones del compilador.

Uso:

```bash
.\build\Debug\oasis.exe project create TestGame --path .\TestGame
.\build\Debug\oasis.exe scene create Main --project .\TestGame
.\build\Debug\oasis.exe entity create Cube --project .\TestGame
.\build\Debug\oasis.exe entity add-component Cube Transform --project .\TestGame
.\build\Debug\oasis.exe entity add-component Cube Mesh --project .\TestGame
.\build\Debug\oasis.exe entity set Cube Transform.position 5 0 0 --project .\TestGame
.\build\Debug\oasis.exe state --project .\TestGame
.\build\Debug\oasis.exe run --project .\TestGame --window
```

También se puede consultar un proyecto existente:

```bash
.\build\Debug\oasis.exe project open .\TestGame
```

Los valores de `Transform` se validan como tres números. Por ejemplo, `5 0 0` se guarda como `[5, 0, 0]`.

## 43.2 Verificación

La prueba del flujo principal se ejecuta con:

```bash
ctest --test-dir build -C Debug --output-on-failure
```

Resultado actual: 1 prueba pasada.

## 43.3 Siguiente objetivo

Diseñar un backend de renderer GPU y sustituir el cubo alámbrico Win32 sin romper la CLI ni el formato de proyecto existentes. No ampliar todavía el sistema de componentes ni el soporte de assets hasta que ese flujo gráfico esté probado.

---

# 44. Renderer visual experimental

**Estado:** IMPLEMENTADO

El comando `run` acepta `--window` y abre una ventana con un renderer alámbrico experimental basado en Win32/GDI. Las entidades que tienen un componente `Mesh` se dibujan como cubos; `Transform.position` y `Transform.scale` afectan su representación. La rotación es únicamente una animación visual del renderer.

Ejemplo:

```bash
.\build\Debug\oasis.exe run --project .\DemoGame --window
```

Este renderer es una demostración visual de la cadena escena → entidad → componentes → imagen. No es todavía un renderer GPU de producción.

## [2026-09-05] — Corrección de carga de Transform

### Modificado

- La carga JSON de `Transform` ahora interpreta de forma independiente `position`, `rotation` y `scale`.
- La prueba nativa comprueba que la escala inicial `[1, 1, 1]` persiste tras guardar y cargar una escena.

### Motivo

Un error de selección de propiedad hacía que `scale` tomara el valor de `position`. Con una posición `[0, 0, 0]`, el cubo se colapsaba visualmente en un punto.

---

# 45. Renderer 3D por GPU — Direct3D 11

**Estado:** IMPLEMENTADO

El renderer Win32/GDI fue reemplazado por Direct3D 11 escrito en C. El motor crea exclusivamente un dispositivo `D3D_DRIVER_TYPE_HARDWARE`: si el adaptador gráfico no puede inicializarse, informa el error y no cambia a un renderizador software.

Incluye:

- cubo 3D compuesto por vértices e índices en buffers de GPU;
- vertex shader y pixel shader HLSL compilados al iniciar;
- matriz de mundo y proyección perspectiva;
- buffer de profundidad;
- presentación inmediata sin VSync ni limitador de FPS;
- contador de FPS reales en el título de la ventana;
- identificación del adaptador gráfico seleccionado.

La ejecución validada en este equipo usa:

```text
Renderer: Direct3D 11
GPU: NVIDIA GeForce RTX 3050 6GB Laptop GPU
FPS: sin límite artificial
```

El título mide los frames efectivamente renderizados contra el reloj de alta resolución. No hay VSync, espera ni límite artificial: la tasa queda limitada únicamente por CPU, driver y GPU. Esto puede producir *tearing*, que es aceptable para la etapa actual de diagnóstico de rendimiento.

## [2026-09-05] — Selección de GPU dedicada

### Modificado

- El renderer enumera los adaptadores DXGI y crea Direct3D 11 sobre el adaptador de hardware con mayor VRAM dedicada.
- Se exportan las preferencias de alto rendimiento para NVIDIA Optimus y AMD PowerXpress.

### Motivo

En equipos con gráficos híbridos, delegar la selección a Windows puede ejecutar una aplicación en la GPU integrada. Oasis ahora selecciona explícitamente la GPU dedicada; la composición final de la ventana aún puede aparecer en la GPU integrada por la arquitectura de pantalla del portátil.

---

# 46. Auditoría técnica verificada — 2026-09-05

## 46.1 Estado actual

**HECHO VERIFICADO:** Oasis Engine es un prototipo C11 para Windows, no todavía un motor general. Tiene núcleo de proyecto/escena JSON, CLI para editarlo y una demo experimental Direct3D 11. La compilación Debug y la única prueba automatizada pasan; `run` sin ventana carga el demo y termina.

**NO VERIFICABLE EN ESTE ENTORNO:** la salida visual de `run --window`. No había una superficie nativa para observar la ventana/GPU; por tanto el renderer no puede considerarse validado extremo a extremo.

Conclusión: existe un MVP de **edición de escena JSON por CLI + demo gráfica rígida**, no un motor de juego utilizable y extensible aún.

## 46.2 Objetivo realista y arquitectura actual

El objetivo viable es un motor pequeño controlable por CLI y posteriormente API, que permita a una persona o agente inspeccionar y cambiar un proyecto con resultados estructurados. La infraestructura mínima necesaria es proyecto/escena fiable, operaciones deterministas, runtime con ticks y renderer opcional.

```text
CLI (src/main.c)
  ├─ core (src/oasis_core.c / src/oasis.h)
  │   ├─ oasis.project JSON
  │   └─ scenes/<nombre>.scene.json
  └─ renderer (src/oasis_renderer_win32.c; sólo `run --window`)
      └─ Win32 + Direct3D 11 + estado global temporal
```

El entrypoint es `main` en `src/main.c`. Cada comando carga desde disco; los comandos de entidad reescriben toda la escena. Sin `--window`, `run` sólo informa que cargó la escena. Con ventana, el renderer dibuja cubos. El estado persistente vive en JSON; el movimiento/cámara vive como globales del renderer y se pierde al cerrar.

No hay ECS, scheduler, bus de eventos, API, scripting, cargador de assets ni runtime headless con update.

## 46.3 Estructura y sistemas existentes

| Sistema/ruta | Estado | Qué existe realmente | Evidencia / límites |
| --- | --- | --- | --- |
| `CMakeLists.txt` | ✅ FUNCIONAL | C11, biblioteca `oasis_core`, ejecutable y CTest | Build Debug correcto; sin CI ni build limpio auditado |
| `src/oasis_core.c` | 🟡 PARCIAL | Crear/cargar proyecto y escena; editar entidades/Transform; persistir | Funciona con archivos propios; JSON se parsea manualmente |
| `src/main.c` | 🟡 PARCIAL | CLI de creación, edición, `state`, `run` | Sin contrato estable de salida/error para máquinas |
| `src/oasis.h` | 🟡 PARCIAL | 128 entidades fijas, flags Transform/Mesh/Camera/Light | Camera/Light sólo son flags, no sistemas |
| `src/oasis_renderer_win32.c` | 🧪 EXPERIMENTAL | D3D11, cubo, shader embebido, WASD/ratón | Ruta conectada pero visual no verificada; comportamiento por IDs |
| `tests/test_core.c` | 🟡 PARCIAL | Recorrido feliz de persistencia | CTest 1/1 pasa; no prueba errores, CLI ni renderer |
| `DemoGame/` | 🟡 PARCIAL | Floor, Player, Cube | Demo depende de esos nombres especiales |
| `assets/` | ⚪ NO IMPLEMENTADO | Directorio creado | Ningún código lo lee |

No se encontraron imports rotos, duplicación significativa de funciones, TODO/FIXME/stubs textuales ni dependencias de terceros. No existe repositorio Git, `.gitignore`, README, licencia ni CI en la raíz auditada.

## 46.4 Flujo de ejecución

1. `main` interpreta argumentos sin biblioteca de CLI.
2. El core carga `oasis.project` y la escena activa mediante búsqueda textual en JSON.
3. Los comandos de edición cambian el modelo en memoria y sobrescriben el archivo de escena.
4. `state` imprime nombre/IDs/flags, pero no Transform completo ni propiedades.
5. `run` carga e informa proyecto, escena y conteo; sólo `--window` entra al bucle Win32/D3D11.
6. El renderer dibuja cada `Mesh` como el mismo cubo. `Player` da la posición de cámara/movimiento; `Cube` rota; `Floor`/`Player` reciben colores por nombre.

## 46.5 Problemas críticos

### [CRÍTICO] El parser no implementa JSON ni valida el documento

**Ubicación:** `src/oasis_core.c:112-130`, `194-276`  
**Qué ocurre:** el cargador usa `strstr`/`strchr` y búsquedas de rangos; no respeta JSON general, escapes ni esquema. Ignora los fallos de `parse_vec3`.  
**Evidencia:** `parse_vec3` se descarta en líneas 265-267; la detección recorre cada `"id"` global en línea 252.  
**Impacto:** una escena modificada externamente puede cargarse parcial o erróneamente sin error.  
**Solución recomendada:** integrar un parser JSON C pequeño y mantenido; validar el esquema mínimo y rechazar el documento completo ante cualquier inconsistencia. No desarrollar un parser propio.  
**Prioridad:** CRÍTICO.

### [CRÍTICO] `run` no ejecuta un runtime normal

**Ubicación:** `src/main.c:118-123`  
**Qué ocurre:** sin `--window`, `run` imprime la carga y sale; no hay update, systems ni ticks headless.  
**Evidencia:** la única ruta de loop está en el renderer y queda condicionada a `--window`.  
**Impacto:** el motor no puede ejecutar una simulación ni automatizarla.  
**Solución recomendada:** runtime mínimo `init/update/shutdown` con número explícito de ticks headless; renderer como adaptador opcional.  
**Prioridad:** CRÍTICO.

### [CRÍTICO] El documento histórico sobredeclaraba implementación

**Ubicación:** secciones 43-45 históricas; código auditado.  
**Qué ocurre:** sus etiquetas “IMPLEMENTADO” no distinguían demo de sistemas generales verificables.  
**Evidencia:** Camera, Light, assets y API carecen de implementación runtime; renderer visual no verificable aquí.  
**Impacto:** otro agente podría trabajar sobre premisas falsas.  
**Solución recomendada:** esta sección auditada prevalece y debe actualizarse con pruebas en cada cambio.  
**Prioridad:** CRÍTICO.

## 46.6 Hallazgos altos, deuda y sobreingeniería

### [ALTO] Renderer acoplado a IDs de demo y a una única malla

**Ubicación:** `src/oasis_renderer_win32.c:111-119`, `255-257`, `272-278`  
**Qué ocurre:** colores, cámara, movimiento y animación dependen literalmente de `Floor`, `Player` y `Cube`; todo Mesh usa buffers estáticos de cubo.  
**Impacto:** escenas generadas por CLI no tienen comportamiento semántico por componentes.  
**Solución recomendada:** conservar sólo `primitive: cube` en el MVP, pero seleccionar cámara/default explícito y sacar color/animación de IDs.  
**Prioridad:** ALTO.

### [ALTO] Camera, Light y assets son interfaz sin semántica

**Ubicación:** `src/oasis.h:25-28`, `src/oasis_core.c:297-299`, `src/oasis_renderer_win32.c:245-262`  
**Qué ocurre:** Camera/Light se serializan pero renderer no los consulta; `assets/` no se usa.  
**Impacto:** expone capacidades inexistentes.  
**Solución recomendada:** retirarlas temporalmente de la CLI o implementar una semántica mínima comprobada antes de ofrecerlas.  
**Prioridad:** ALTO.

### [ALTO] Renderer sin fallback de GPU

**Ubicación:** `src/oasis_renderer_win32.c:188-214`, `227`  
**Qué ocurre:** escoge sólo adaptador no software con más VRAM dedicada y falla si no lo hay; no prueba hardware por defecto ni WARP.  
**Impacto:** demo no arranca en iGPU, VM o CI.  
**Solución recomendada:** intentar hardware por defecto y WARP para desarrollo, informando el backend seleccionado.  
**Prioridad:** ALTO.

### [ALTO] Persistencia no atómica

**Ubicación:** `src/oasis_core.c:76-91`, `279-304`  
**Qué ocurre:** abre archivos directamente con `wb`.  
**Impacto:** interrupción durante guardado puede truncar el proyecto/escena.  
**Solución recomendada:** temporal en el mismo directorio, cerrar/comprobar y rename atómico.  
**Prioridad:** ALTO.

### [ALTO] Interfaz para IA insuficiente

**Ubicación:** `src/main.c`, `src/oasis_core.c:352-372`  
**Qué ocurre:** `state` omite transforms; éxito JSON no tiene schema estable y los errores son texto. No hay listados, delete ni conexión al runtime.  
**Solución recomendada:** CLI con envelope `{ok,result,error,schema_version}`, `scene list`, `entity get`, `entity delete`; API local sólo tras estabilizarlo.  
**Prioridad:** ALTO.

### [MEDIO] Las versiones persistidas y ejecutables divergen sin validación

**Ubicación:** `src/oasis.h:7`, `DemoGame/oasis.project:4`, `src/oasis_core.c:147-162`  
**Qué ocurre:** el binario declara `0.2.0`, mientras el demo persiste `0.1.0`; al cargar, el core ignora `format`, `engine` y `version`.  
**Evidencia:** las rutas verificadas cargaron el demo y anunciaron `Oasis Runtime 0.2.0` sin diagnosticar el archivo `0.1.0`.  
**Impacto:** futuras migraciones no podrán detectar incompatibilidades ni explicar datos antiguos.  
**Solución recomendada:** validar formato/engine/versión de schema y definir migración explícita; no usar la versión del binario como sustituto de schema.  
**Prioridad:** MEDIO.

**Código huérfano/funcionalmente muerto:** `assets/` vacío, flags Camera/Light y estado global del renderer sin propietario core. `build/` es generado, no fuente.  
**Sobreingeniería detectada:** no hay exceso de capas; el problema es superficie pública prematura. GPU dedicada/exportaciones NVIDIA/AMD y una futura API son complejidad anticipada. Alternativa: core pequeño + CLI JSON + renderer D3D11 por defecto con fallback.

## 46.7 MVP, riesgos y recomendaciones

### MVP real

1. Parser JSON validado y escritura atómica.
2. CLI JSON estable que cree, liste, consulte, edite y elimine entidades/escenas.
3. Runtime determinista con `init/update/shutdown` y ticks headless.
4. Renderer opcional con fallback, cámara explícita/default y cubos sin IDs especiales.
5. Pruebas de integración CLI, JSON inválido y smoke test del demo.

**Después del MVP:** input desacoplado, cámara real, color/material simple, Mesh con propiedades persistentes.  
**Futuro, no necesario ahora:** ECS dinámico, física, audio, animación, editor, plugins, red y API HTTP.

Riesgos: corrupción por escritura directa/parser permisivo; rutas Windows y D3D11 no portables; input/tiempo no deterministas; límite fijo de 128 entidades; `strtof` puede aceptar no finitos y producir JSON inválido; no hay test gráfico.

Mantener C11/CMake y no añadir ECS. Separar sólo `core` (modelo/persistencia), `runtime` (ticks) y frontends (CLI/renderer). El core debe devolver estructuras; la CLI las serializa, permitiendo a una futura API reutilizar exactamente la semántica.

## 46.8 Roadmap recomendado

| Etapa | Objetivo | Sistemas | Criterio de finalización |
| --- | --- | --- | --- |
| 1 | Almacenamiento fiable | core + tests | JSON inválido/escape/límites/I-O probados; escritura temporal+rename |
| 2 | CLI para agentes | main + core + tests de proceso | JSON con schema, list/get/delete, stdout/stderr/código probados |
| 3 | Runtime reproducible | módulo runtime pequeño | ticks headless y estado post-run verificables; sin IDs mágicos |
| 4 | Vertical slice visual | renderer + demo | cámara/default y fallback; ejecución visual manual en hardware documentada |
| 5 | Evaluar API | CLI ya estable | cliente local completa create/query/edit/ticks con mismo JSON |

## 46.9 Estado de implementación

- [x] Build Debug CMake Windows verificado.
- [x] Prueba happy-path del núcleo verificada.
- [x] Proyecto/escena/entidad/Transform/Mesh básicos por CLI.
- [~] Renderer D3D11 conectado, sin confirmación visual.
- [~] Estado CLI, incompleto para automatización robusta.
- [ ] Parser JSON real y guardado atómico.
- [ ] Runtime con update/ticks headless.
- [ ] Camera, Light, assets funcionales.
- [ ] API de control.
- [!] Comprobación visual bloqueada por ausencia de superficie nativa en el entorno de auditoría.

## 46.10 Registro de auditoría y verificaciones

Ejecutado:

```powershell
cmake --build build --config Debug --parallel 2
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\oasis.exe version
.\build\Debug\oasis.exe state --project .\DemoGame
.\build\Debug\oasis.exe run --project .\DemoGame
```

Resultado: build correcto; CTest 1/1 correcto; versión `0.2.0`; demo con tres entidades cargado; `run` sin ventana correcto. Esto no valida el renderer visual ni input.

No se modificó código fuente. Un smoke test de creación dejó `TempAuditProject/`; la limpieza automática fue rechazada por la política de ejecución del entorno. Es un proyecto de prueba seguro para eliminar y no parte del motor.

# 47. Etapa 1 — Parser JSON fiable (2026-09-05)

**Estado: IMPLEMENTADO Y VERIFICADO.** Esta sección es posterior a la auditoría 46 y actualiza su estado.

El núcleo ya no extrae datos JSON con búsquedas de texto. `oasis_core` integra [cJSON 1.7.19](https://github.com/DaveGamble/cJSON), un parser C mantenido, incluido localmente en `third_party/cjson/` y compilado por CMake.

## 47.1 Validación aplicada

- `oasis.project` debe ser JSON válido y un objeto con `format: 1`, `engine: "Oasis Engine"`, `version` string, `name` válido y `active_scene` string válido o `null`.
- Una escena debe ser JSON válido con `format: 1`, `name` válido y `entities` array de hasta 128 elementos.
- Cada entidad debe contener `id` válido y único, además de un objeto `components`.
- `Transform`, cuando existe, exige `position`, `rotation` y `scale` como arrays de exactamente tres números finitos. `Mesh`, cuando existe, exige `{ "primitive": "cube" }`. `Camera` y `Light` exigen objetos.
- Campos obligatorios duplicados, JSON sintácticamente inválido, tipos incompatibles y datos fuera del límite numérico se rechazan con un error explícito.
- La carga se realiza primero en una escena temporal; si falla la validación, la escena suministrada por el llamador permanece intacta. No hay carga parcial.

La escritura conserva el formato JSON existente. La escritura atómica todavía no existe y sigue siendo trabajo pendiente de estabilización.

## 47.2 Pruebas verificadas

`tests/test_core.c` conserva el recorrido de crear/cargar/guardar/modificar y añade pruebas de:

- JSON sintácticamente inválido;
- campo obligatorio ausente;
- tipo incorrecto en `Transform.position`;
- garantía de que una carga fallida no modifica la escena previamente cargada.

Verificado en Windows con:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\oasis.exe state --project .\DemoGame
.\build\Debug\oasis.exe run --project .\DemoGame
```

Resultado: compilación Debug correcta; CTest `1/1` correcto; DemoGame carga tres entidades y `run` headless conserva su comportamiento anterior (carga e informa, aún no ejecuta ticks).

## 47.3 Próxima etapa

La siguiente etapa es el modelo de escena fiable: completar la semántica mínima persistida de `Transform`, `Mesh`, `Camera` y `Light` sin ECS. Runtime, desacoplamiento del renderer y expansión de CLI permanecen sin implementar y no se declaran como existentes.

# FIN DEL DOCUMENTO MAESTRO

---

# 48. Etapa 2 — Semántica mínima de escena y runtime headless (2026-09-05)

**Estado: IMPLEMENTADO Y VERIFICADO.** Esta sección es posterior a las secciones 46 y 47 y prevalece sobre sus notas de trabajo pendiente para estos puntos concretos.

## 48.1 Modelo persistido

Los componentes públicos ya tienen datos mínimos persistidos, sin introducir ECS:

- `Transform`: `position`, `rotation` y `scale` (tres números finitos cada uno).
- `Mesh`: `primitive: "cube"` y `color` RGB.
- `Camera`: `fov_degrees` entre 1 y 179; el valor por defecto es 60.
- `Light`: `color` RGB e `intensity` finita no negativa; el valor por defecto es 1.

Al crear una entidad se inicializan valores seguros. Al guardar y volver a cargar, el núcleo conserva esos valores. El renderer D3D11 consulta la primera `Camera` y `Light` existentes; si no hay cámara, usa una cámara por defecto. Los cubos usan el color de su propio `Mesh`, modulado por la primera luz. Ya no depende de los IDs de demostración `Floor`, `Player` o `Cube` para elegir cámara, color o animación.

La salida visual de `run --window` sigue requiriendo comprobación manual en hardware Windows; este cambio está verificado por compilación y pruebas de núcleo, no por inspección gráfica automatizada.

## 48.2 Runtime determinista mínimo

Existe `src/oasis_runtime.c` con las operaciones `init`, `update` y `shutdown`. Cada actualización válida incrementa `tick_count` y acumula el tiempo transcurrido. El comando:

```powershell
.\build\Debug\oasis.exe run --project .\DemoGame --ticks 2
```

ejecuta dos ticks headless de `1/60` segundos y termina de forma determinista. Sin `--ticks`, el modo headless ejecuta uno. Con `--window`, el renderer actualiza ese mismo runtime en cada frame y `--ticks N` permite cerrarlo tras `N` ticks.

## 48.3 Verificación realizada

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\oasis.exe run --project .\DemoGame --ticks 2
```

Resultado: compilación correcta, tres pruebas CTest aprobadas (`oasis_core`, `oasis_cli_version` y `oasis_cli_run`) y ejecución headless correcta con dos ticks.

## 48.4 Próximo objetivo

La siguiente etapa pendiente es una interfaz CLI estable para agentes: salida JSON con contrato versionado, `state` que incluya los datos de componentes, y operaciones de listado/consulta/eliminación. La escritura atómica sigue pendiente y debe abordarse antes de ampliar la superficie de edición.

---

# 49. Etapa 3 — Persistencia segura y CLI para agentes (2026-09-05)

**Estado: IMPLEMENTADO Y VERIFICADO.** Esta sección actualiza la previsión de la sección 48.4.

## 49.1 Guardado seguro

`oasis.project` y las escenas se guardan ahora en un archivo temporal situado en el mismo directorio (`.tmp`). Sólo después de cerrar correctamente la escritura, el temporal reemplaza el archivo final mediante una operación atómica del sistema operativo. Si la escritura o el reemplazo falla, el archivo temporal se elimina y el archivo anterior se conserva.

Esto protege las operaciones normales de creación, apertura y edición contra archivos truncados por una interrupción durante la escritura. No sustituye una estrategia de recuperación de energía a nivel de sistema de archivos ni bloqueo entre procesos; esos problemas no forman parte del MVP actual.

## 49.2 Contrato de la CLI

Los comandos operativos de la CLI escriben una única respuesta JSON en stdout. El contrato actual es:

```json
{"schema_version":1,"ok":true,"result":{}}
```

o, ante un error de operación:

```json
{"schema_version":1,"ok":false,"error":{"message":"..."}}
```

`state` devuelve el proyecto, escena, entidades y los datos completos de sus componentes. `entity get ID` devuelve una entidad completa. `run --ticks N` devuelve los ticks y el tiempo acumulado. Los identificadores se limitan a letras, números, `_` y `-`, de modo que el JSON persistido y las respuestas de entidades no incorporan texto no escapado de usuario.

Comandos añadidos:

```powershell
.\build\Debug\oasis.exe scene list --project .\DemoGame
.\build\Debug\oasis.exe scene delete OtraEscena --project .\DemoGame
.\build\Debug\oasis.exe entity get Cube --project .\DemoGame
.\build\Debug\oasis.exe entity delete Cube --project .\DemoGame
```

No se puede eliminar la escena activa; primero hay que abrir otra. Esto evita dejar al proyecto apuntando a un archivo ausente.

## 49.3 Verificación

La prueba de núcleo comprueba el listado ordenado de escenas, la eliminación de escena no activa, la eliminación persistida de entidades y que el guardado no deje archivos temporales. CTest ejecuta además las rutas de CLI `version`, `run`, `state`, `scene list` y `entity get` sobre `DemoGame`.

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Resultado: seis pruebas aprobadas.

## 49.4 Próximo objetivo

Con la persistencia y la observabilidad mínima resueltas, el siguiente incremento debe ampliar las pruebas de proceso para validar también el contenido JSON, los códigos de salida y los flujos de crear/editar/eliminar desde un binario externo. Sólo después conviene exponer una API local que reutilice exactamente este contrato. El renderer permanece opcional y no debe dictar la semántica del core.

---

# 50. Base de assets 3D — GLB y descriptor Oasis (2026-09-05)

**Estado: IMPLEMENTADO COMO PIPELINE DE IMPORTACIÓN; RENDERIZADO DE MALLAS: PENDIENTE.**

Oasis adopta `GLB` (glTF 2.0 binario) como formato fuente inicial para modelos. Es el formato recomendado para modelos exportados por un editor o generados por IA porque puede encapsular geometría, materiales PBR, texturas, esqueletos y animaciones en un solo archivo.

El comando actual es:

```powershell
.\build\Debug\oasis.exe asset import Firefox .\Firefox.glb --project .\DemoGame
.\build\Debug\oasis.exe asset list --project .\DemoGame
```

La importación valida el encabezado de GLB 2.0 y su primer bloque JSON, copia la fuente al proyecto, calcula una huella FNV-1a de 64 bits y genera:

```text
assets/
  oasis.assets.json             # manifiesto versionado
  source/Firefox.glb            # fuente reimportable
  cooked/Firefox.oasisasset     # descriptor de runtime versionado
```

Las rutas del manifiesto son relativas y usan `/`, por lo que son independientes del separador de Windows. Cada entrada contiene un ID estable, la fuente, el descriptor generado, la huella y el tamaño. Reimportar con el mismo ID actualiza esa entrada.

`oasisasset` es por ahora un **descriptor de trazabilidad**, no una malla optimizada. El renderer todavía sólo dibuja cubos: no debe afirmarse que Oasis ya abre o muestra modelos GLB. El siguiente incremento de assets será leer los buffers de malla y materiales desde el GLB para convertirlos a buffers del renderer, conservando el formato fuente y el manifiesto ya definidos.

La prueba de núcleo genera un GLB 2.0 mínimo válido y comprueba la importación, el manifiesto, el archivo fuente y el descriptor. Build Debug y las siete pruebas CTest pasan.

---

# 51. Diagnóstico reproducible de ventana, GLB e input (2026-09-05)

**Estado: implementación presente; interacción manual todavía pendiente de una verificación visible del usuario.** Esta sección sustituye cualquier afirmación anterior de que WASD o la visualización de un modelo estén comprobados extremo a extremo.

## Hechos comprobados

- `asset convert-obj Firefox ...LP_Firefox.obj` produjo `DemoGame/assets/source/Firefox.glb` y el manifiesto correspondiente.
- `Mesh` admite ahora `primitive: "asset"` y `asset_id`; la entidad `Firefox` de `DemoGame/scenes/Main.scene.json` usa esa referencia.
- El renderer lee el primer primitivo GLB con `POSITION` e índices de 16 o 32 bits y crea buffers Direct3D 11 para él. Los materiales, UV, normales, texturas y PBR siguen fuera de alcance.
- Una primera ejecución de ventana cerraba con `0xC0000005`: el cargador suponía incorrectamente que `accessor.byteOffset` siempre estaba presente. glTF permite omitirlo para expresar cero. Se corrigió usando cero por defecto. Después, `run --window --ticks 2` terminó con código `0` e informó la RTX 3050.
- La suite automatizada se ejecutó con éxito: `ctest --test-dir build -C Debug --output-on-failure` (7/7). No prueba teclado ni píxeles de una ventana nativa.

## Input: implementación y evidencia disponible

La implementación anterior no tenía una ruta de input. La implementación actual registra `WM_KEYDOWN` y `WM_KEYUP` en `window_proc` para `W`, `A`, `S`, `D`, `Q`, `E` y `Ctrl`. `update_camera_controls` consume ese estado para mover el primer componente `Camera` de la escena:

- `W`/`S`: avanzar/retroceder según el yaw de cámara.
- `A`/`D`: desplazamiento lateral.
- `Q`/`E`: bajar/subir.
- `Ctrl`: velocidad 12 unidades/s; sin Ctrl: 4 unidades/s.

La ventana pide foco explícitamente al crearse (`SetForegroundWindow` y `SetFocus`) y limpia teclas al recibir `WM_KILLFOCUS`. Para no confundir “el código existe” con “el evento llegó”, el título de la ventana muestra cada segundo:

```text
Oasis | <FPS> FPS | foco:si|no eventos:<N> | cam:[X,Y,Z]
```

`eventos` sólo aumenta cuando el proceso recibe mensajes de teclado. `cam` sólo cambia cuando un movimiento se consume. Por lo tanto, la prueba manual mínima y observable es: abrir `run --window`, esperar `foco:si`, mantener `W` un segundo y comprobar que aumentan `eventos` y la coordenada Z. Con `Ctrl+W` el cambio de Z debe ser aproximadamente triple. Si `foco:no`, el diagnóstico es foco de ventana; si `foco:si` y `eventos` no cambia, Windows no entrega input a la ventana; si eventos aumenta pero `cam` no cambia, hay un defecto en el procesamiento que debe corregirse con esos valores concretos.

## Límites y siguiente decisión

No hay una superficie de automatización nativa disponible en esta sesión para inyectar teclas y observar los píxeles de la ventana; por eso no se declara el input como validado sólo por compilación. El próximo cambio no debe ser otro ajuste especulativo: debe usar los tres valores del título como evidencia, añadir una prueba de integración de input Win32 cuando haya una superficie de escritorio disponible, y sólo entonces declarar los controles verificados.

**Observación de ejecución de esta sesión:** tras lanzar el proceso desde el entorno de automatización, el título reportó `foco:no eventos:0 cam:[0.0,3.0,-10.0]`. Esto confirma que Windows no concedió foco a esa instancia iniciada en segundo plano; por ello no puede recibir `WM_KEYDOWN`. No demuestra aún que falle el controlador una vez que el usuario activa la ventana, y tampoco autoriza a afirmar que WASD funciona. El siguiente dato necesario es el título después de que el usuario active manualmente la ventana: conservar `foco:no` indica un problema de activación/desktop; mostrar `foco:si` con eventos crecientes pero cámara constante indica un problema distinto en el movimiento.

## 51.1 Investigación de una regresión reportada de WASD

**Petición:** analizar la pérdida de controles sin introducir otra solución especulativa.

### Evidencia recuperable

1. Al inicio de esta sesión, antes de cualquier modificación de input, `src/oasis_renderer_win32.c` tenía una función `window_proc` que sólo atendía cierre/destrucción y un bucle de render que no procesaba teclado, ratón, controlador ni movimiento de cámara. No había referencias a `WM_KEYDOWN`, `GetAsyncKeyState`, WASD ni input en el árbol fuente.
2. El binario actual `build/Debug/oasis.exe` se recompiló desde ese árbol durante la sesión y su marca de tiempo se actualizó. Una recompilación reemplaza el ejecutable previo de la misma ruta.
3. Existen dos salidas temporales de una ejecución anterior (`oasis-runtime-*.out`, 02:24), pero sólo contienen la ayuda de una CLI anterior; no contienen un binario, símbolos ni código de input recuperable.
4. El directorio no es un repositorio Git y no se hallaron copias `.bak`, `.orig`, `.old`, parche, archivo comprimido ni historial local de `oasis_renderer_win32.c`. Por ello no hay una versión conocida contra la que hacer un diff fiable.
5. La documentación histórica menciona “WASD/ratón”, pero otra sección de la misma documentación afirma que el input y la salida visual no estaban verificados. La documentación no es evidencia de que el binario que funcionaba contuviera ese código.

### Conclusión acotada

La explicación respaldada por la evidencia es una **desalineación entre un binario anterior que el usuario recuerda como funcional y el código fuente disponible al inicio de esta sesión**. Al recompilar, ese posible binario anterior fue sustituido por el ejecutable construido desde una fuente que no contenía input. No es posible afirmar qué cambio exacto eliminó el controlador ni quién lo realizó sin recuperar una copia del binario o fuente anterior.

Las modificaciones de input posteriores a esta investigación son intentos nuevos y no una restauración del comportamiento anterior; no deben presentarse como la causa histórica ni como solución validada. La observación de foco `no` es un segundo problema de la instancia iniciada por automatización, no prueba de la regresión original.

### Qué permitiría cerrar el diagnóstico

- Un `oasis.exe` que el usuario haya ejecutado cuando WASD funcionaba, o una copia anterior de `src/oasis_renderer_win32.c`, permitiría una comparación binaria/fuente y un diff exacto.
- Sin una de esas copias, el único siguiente paso responsable es preservar el estado actual y no seguir modificando controles hasta que exista una referencia conocida como buena.
