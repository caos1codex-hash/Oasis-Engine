# Rebuild v0.2 — notas de ingeniería

Reconstrucción desde 0 en C++17. Mantiene el contrato CLI/JSON (`schema_version:1`,
`format:1`) pero con implementación nueva.

## Qué cambió respecto a v0.1 (C11)

* `src/*.c/*.h` eliminados. Nueva base: `core.hpp/cpp`, `assets.hpp/cpp`,
  `runtime.hpp/cpp`, `renderer.hpp` + `renderer_win32_d3d11.cpp`, `main.cpp`.
* Rutas con `std::filesystem`. Manifiestos siempre con `/`. Sin `\\` hardcodeado.
* Escritura atómica con temporal único `*.tmp.<pid>.<ctr>` + `flush + rename`.
  Sin `.tmp` determinista con race.
* `snprintf` con chequeo de truncado en `JsonEscape` y backend GPU.
* CLI estricto: `strtof` rechaza `nan/inf` y espacios; `--ticks` solo `0..1000000`
  dígitos; `--project` no acepta flags como valor; `exit 0 ok / 2 uso / 1 runtime`.
* `stdout` solo JSON. Humanos a `stderr`. `usage` también emite JSON.
* Sin globales mutables en renderer. `Context` por instancia vía `GWLP_USERDATA`.
  Input filtra auto-repeat (bit 30), exige foco para mover, `ESC` sale,
  `Ctrl/Shift` rápido.
* GPU con fallback: dedicada max-VRAM → hardware por defecto → WARP.
  Sin fallback = error `GPU` explícito, no crash silencioso.
* Matemáticas con guardas: `ViewMatrix`/`ProjectionMatrix` retornan `false` con
  `eye==target`, `pitch ±90°`, `aspect<=0`, `fov` fuera de rango. Frame degradado
  a `Present` sin dibujar en vez de `NaN` al shader.
* `asset_geometry` con caps 64MB/1M verts/4M idx, valida `componentType`,
  `count` entero, `stride>=12`, índices `< vertex_count`, `ByteWidth` sin wrap.
  `model==nullptr` se omite y avisa una vez por `stderr` (sin romper `stdout` JSON).
* `Runtime::update` valida `0<=dt<=10` y `isfinite`. `shutdown` nullea escena.
* Tests negativos: JSON inválido, campo ausente, tipo malo, `../`, `NaN`,
  carga fallida no muta, reimport idempotente, sin `.tmp` residual.

## Límites honestos que siguen

* Cap lógica 1024 entidades/escenas/assets (vector dinámico, no array en stack).
* Renderer: primer primitivo GLB con `POSITION` + índices 16/32; `NORMAL` y
  `TEXCOORD_0` opcionales (sin `NORMAL` calcula normales suavizadas por área);
  material `baseColorFactor` + `baseColorTexture` (PNG/JPG embebido, sin mipmaps).
  Sin metallic/roughness, UV2, ni PBR completa.
* `WorldMatrix` aplica yaw+pitch+roll (`R = Ry*Rx*Rz`, misma matriz en picking).
* VSync ON por defecto (`Present(1,0)`, ritmo del monitor). `--vsync 0` lo desactiva
  (`Present(0,0)` con tearing, solo para diagnóstico). Sin limitador FPS adicional.
* Sin ECS, física, audio, scripting, red. Fuera de v0.2 por diseño.

## Control IA (stop) — verificado 2026-09-06

* `oasis stop --project RUTA` cierra la ventana de ese proyecto (sin guardar).
* Verificado extremo a extremo: ventana en escritorio del usuario cerrada por
  señal remota (`stopped:true`), además de ciclo abrir/cerrar en misma sesión.

## Migración DemoGame

* `oasis.project` `version` pasa de `0.1.0` a `0.2.0`. Loader acepta cualquier
  `version` string pero exige `format:1` + `engine:"Oasis Engine"`.
* Escenas sin cambios de formato. `Firefox` sigue como `Mesh asset`.
