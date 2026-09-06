# Oasis Engine v0.2 (reconstrucción)

Motor pequeño AI-first: núcleo escena JSON + CLI con contrato JSON estable +
runtime headless determinista + renderer D3D11 opcional.

> Fuente de verdad: este README + `docs/REBUILD_0_2.md`.
> Agentes IA: leer primero `docs/SKILL.md`.
> `documentacion.md` es legado v0.1 y queda como `docs/legacy_v1.md` de referencia histórica.

## Stack

* C++17, CMake >= 3.20, MSVC 2022 (`/W4 /WX`) o GCC/Clang (`-Wall -Wextra -Wpedantic -Werror`)
* Sin dependencias externas salvo `third_party/cjson` vendored (C, compilado sin warnings-as-errors)
* `std::filesystem` para rutas portables, escritura atómica `tmp + flush + rename`
* JSON validado con cJSON + validadores estrictos propios

## Layout

```text
src/
  core.hpp / core.cpp         # Project, Scene, Entity, persistencia
  assets.hpp / assets.cpp     # import GLB, convert OBJ, manifiesto
  runtime.hpp / runtime.cpp   # init/update/shutdown, ticks 1/60
  renderer.hpp                # IRenderer + backend D3D11 opcional
  renderer_win32_d3d11.cpp    # solo Windows, hardware -> default -> WARP
  main.cpp                    # CLI: parse estricto, stdout JSON, stderr humano
tests/
  test_core.cpp               # asserts sin framework, happy + negativos
DemoGame/                     # demo versionada 0.2.0
```

## Build (Windows)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## CLI — contrato estable

* `stdout`: una sola línea JSON `{"schema_version":1,"ok":true,"result":{...}}` o `{"schema_version":1,"ok":false,"error":{"code":"...","message":"..."}}`
* `stderr`: texto humano. `stdout` nunca lleva texto plano.
* Exit: `0 ok`, `2 uso inválido`, `1 error runtime` (not-found, IO, GPU...).
* Códigos de error: `INVALID_ARG, NOT_FOUND, ALREADY_EXISTS, IO, BAD_FORMAT, LIMIT, GPU, INTERNAL`.

```powershell
.\build\Debug\oasis.exe version
.\build\Debug\oasis.exe project create TestGame --path .\TestGame
.\build\Debug\oasis.exe scene create Main --project .\TestGame
.\build\Debug\oasis.exe entity create Cube --project .\TestGame
.\build\Debug\oasis.exe entity add-component Cube Transform --project .\TestGame
.\build\Debug\oasis.exe entity add-component Cube Mesh --project .\TestGame
.\build\Debug\oasis.exe entity set Cube Transform.position 5 0 0 --project .\TestGame
.\build\Debug\oasis.exe state --project .\TestGame
.\build\Debug\oasis.exe run --project .\TestGame --ticks 2
.\build\Debug\oasis.exe run --project .\DemoGame --window --ticks 120
.\build\Debug\oasis.exe run --project .\DemoGame --window --modo completa
.\build\Debug\oasis.exe run --project .\DemoGame --window --modo barra
```

Modos de ventana (`run --window`): `--modo ventana` (marco 800x600, por defecto),
`--modo completa` (ventana con botones -/□/X y TOPMOST a monitor completo, tapa la barra) y
`--modo barra` (ventana con botones maximizada, respeta la barra de tareas).
`--fullscreen` sigue como alias de `--modo completa`.

Controles en ventana: `WASD` moverse, `Espacio` subir, `C` bajar
(`E`/`Q` siguen como alias), `Shift` rápido.
`ESC` por pasos: primero cierra paneles flotantes; después pide confirmar
(Sí = guarda la escena y sale, No = sale sin guardar, Cancelar = quedarse).
Ratón: `Ctrl` activa/desactiva el pointer-lock (cursor oculto, bloqueado al centro,
ver `mouse:on|off` en el título); con el mouse activo, moverlo mira (yaw/pitch
de la primera `Camera`).
Rendimiento: VSync activado por defecto (`Present(1,0)`, ritmo del monitor, sin
busy-loop al 100%); `--vsync 0` lo desactiva. El título muestra FPS + ms medios.
Calidad automática (`--min-fps 144` por defecto, `0` la desactiva): si los FPS
bajan del mínimo, renderiza al 85/70/55/40% y reescala (ver `res:%` en el título);
con >20 FPS de margen durante 3 s recupera. En pantallas de 60 Hz con VSync el
techo físico impide llegar a 144: usa `--min-fps 60` o `0` en ese caso.
Si el dispositivo GPU se pierde, el proceso termina con error `GPU` en vez de colgarse.
Al redimensionar/maximizar, el swapchain se recrea y se redibuja en vivo en cada
`WM_SIZE` (sin estirado ni parpadeo GDI).
Crosshair `+` estilo Minecraft en el centro: blanco = nada, verde = objetivo,
amarillo = selección. Click izq. selecciona; con el botón mantenido, mover el ratón
arrastra (mueve en plano de vista) tanto con `mouse:off` como con `mouse:on` bajo la mira;
click der. abre la ventana flotante con los datos de la entidad (`sel:` en el título).
Al arrastrar, la cámara sigue el movimiento con el mismo delta (no pierdes el objeto).
Gizmo en la selección (X rojo, Y verde, Z azul, con contorno negro para contraste).
Con `X`/`Y`/`Z` bloqueas el arrastre a ese eje (pulsa de nuevo para liberar,
`eje:` en el título) y sale una línea roja fina a lo largo del eje.
Esquina superior derecha: widget de orientación con los 3 ejes mundo (mitad
positiva brillante, negativa atenuada) que rota con la cámara.
El título muestra `raton:N` y `rot:[yaw,pitch]` para comprobar que llegan eventos.
Perder el foco desactiva el mouse: pulsa `Ctrl` de nuevo.
Cielo HDRI: `asset import-sky ID .hdr` + `scene set-sky ID` (fondo equirect con
tonemap ACES; la luz de escena sigue direccional).
Control total IA: `oasis stop --project RUTA` cierra la ventana de ese proyecto
(sale sin guardar; idempotente: `stopped:false` si no hay ventana). Con `--save`
guarda la escena antes de salir (equivale a `ESC` → Sí; verificar con `state`
después). En Windows usa un evento nombrado `Global\` (visible entre sesiones
con admin) con fallback a `Local\`; `--ticks N` también auto-cierra tras N ticks
para automatización.

## Buenas prácticas aplicadas

* Sin globales mutables. `RendererContext` por instancia, `Runtime` RAII.
* `std::vector` en vez de arrays fijos de 128 en stack. Cap lógica con error `LIMIT`.
* `filesystem::path` con `/` en manifiestos. Sin `\\` hardcodeado.
* `snprintf` con chequeo `>= sizeof`. Sin truncados silenciosos.
* `strtof/strtoull` estrictos: rechazan `nan/inf`, `-1` para ticks, espacios.
* `atomic_write` con nombre `*.tmp.<pid>` + `flush + rename`. Sin `.tmp` determinista con race.
* Tests: negativos JSON, traversal `../`, límites, carga fallida no muta, CLI envelope + exit codes.
