# Oasis Skill — manual del agente (v0.2)

Fuente de verdad del motor: `README.md` + `docs/REBUILD_0_2.md`.
Este archivo enseña a un agente a **operar** el motor, no a rediseñarlo.
Histórico v0.1: `docs/legacy_v1.md` (OBSOLETO, no usar como spec).

## 1. Ciclo obligatorio: observar → actuar → verificar

```text
state / get / list
  → decidir el cambio mínimo
    → ejecutar UN comando
      → volver a consultar
        → correcto? continuar : corregir
```

Nunca afirmes que algo funciona porque existe su función o su test.
Solo `stdout` JSON + `exit` + nueva consulta lo prueban.

## 2. Contrato CLI (estable, `schema_version:1`)

* `stdout`: UNA línea JSON. OK: `{"schema_version":1,"ok":true,"result":{...}}`.
  Error: `{"schema_version":1,"ok":false,"error":{"code":"...","message":"..."}}`.
* `stderr`: texto humano. Nunca parses `stderr` como datos.
* Exit: `0` ok · `2` uso inválido (`INVALID_ARG`, `ALREADY_EXISTS`) · `1` runtime
  (`NOT_FOUND`, `IO`, `BAD_FORMAT`, `LIMIT`, `GPU`, `INTERNAL`).
* Códigos de error: `INVALID_ARG, NOT_FOUND, ALREADY_EXISTS, IO, BAD_FORMAT,
  LIMIT, GPU, INTERNAL`.

## 3. Comandos

```powershell
oasis version
oasis project create NOMBRE [--path RUTA]
oasis project open [RUTA | --project RUTA]
oasis scene create NOMBRE [--project RUTA]
oasis scene open NOMBRE [--project RUTA]
oasis scene list [--project RUTA]
oasis scene delete NOMBRE [--project RUTA]   # no borra la activa: abre otra antes
oasis entity create ID [--project RUTA]
oasis entity add-component ID Transform|Mesh|Camera|Light [--project RUTA]
oasis entity set ID Transform.position|Transform.rotation|Transform.scale X Y Z [--project RUTA]
oasis entity set-model ID ASSET [--project RUTA]  # ASSET debe existir: si no, NOT_FOUND
oasis entity get ID [--project RUTA]
oasis entity delete ID [--project RUTA]
oasis asset import ID ARCHIVO.glb [--project RUTA]
oasis asset convert-obj ID ARCHIVO.obj [--project RUTA]
oasis asset list [--project RUTA]
oasis state [--project RUTA]
oasis run [--project RUTA] [--ticks N] [--window] [--modo ventana|completa|barra] [--vsync 0|1]
oasis stop [--project RUTA] [--save]
```

* IDs y nombres de escena/proyecto: `[A-Za-z0-9_-]{1,63}`. `entity set` rechaza
  `nan/inf`, espacios y no-finitos. `--ticks` solo `0..1000000`.
* Cada comando de edición **guarda en disco de inmediato** (atómico `tmp+rename`).
  No hay "guardar" separado, salvo la ventana (`ESC` → Sí, o `stop --save`).
* `run` sin `--window` es determinista: `N` ticks de `1/60 s`. Para automatización
  usa siempre `--ticks N` (cierra solo) en vez de ventana infinita.

## 4. Control de ventana (solo Windows)

* Abrir: `run --project RUTA --window [--modo ventana|completa|barra] [--ticks N]`.
  Sin `--ticks` corre hasta cerrar a mano.
* Cerrar remoto: `stop --project RUTA` (sale **sin guardar**) o `stop --save`
  (equivale a `ESC` → Sí; **verificar con `state` después**, el `stop` no confirma
  el guardado). Idempotente: `stopped:false` si no hay ventana.
* La IA NO ve la ventana del usuario ni puede mandarle clicks/teclas: sesiones
  distintas. Automatiza con headless + `stop`; la ventana es para el humano.
* Controles humanos: `WASD` + `Espacio/C` + `Shift`, `Ctrl` = mouse on/off,
  clic izq. selecciona/arrastra (con `mouse:on` mantener pulsado bajo la mira),
  clic der. = datos, `X/Y/Z` = eje, `ESC` por pasos con confirmación.

## 5. Límites honestos (no prometer más)

* Cap 1024 entidades/escenas/assets (`LIMIT`).
* Renderer: cubos + **primer primitivo GLB** (`POSITION`, índices 16/32,
  `NORMAL`/`TEXCOORD_0` opcionales con fallback, `baseColorFactor` +
  `baseColorTexture` PNG/JPG). Sin metallic/roughness ni PBR completa.
* `WorldMatrix` aplica yaw+pitch+roll a la malla (picking usa la misma matriz).
* Ventana = Windows + D3D11 (dedicada → hardware → WARP). Headless es portable.
* Sin ECS, física, audio, scripting, red, API HTTP. Fuera de v0.2 por diseño.

## 6. Reglas para no romper el proyecto

1. Lee el código antes de cambiarlo. `docs/legacy_v1.md` no es spec.
2. No cambies formato JSON (`format:1`, `schema_version:1`) sin migrador.
3. Prueba sobre proyectos en `%TEMP%`, **no sobre `DemoGame`**.
   Si tocas `DemoGame`: guarda estado → prueba → restaura → `git diff DemoGame/` vacío.
4. No versiones `build/`, `*.exe/obj/pdb`, ni temporales (`*.tmp`).
5. Cambios pequeños, un commit por cambio, `ctest` en verde antes de commit.
6. `stdout` siempre JSON válido; diagnósticos a `stderr`.
7. Tras cambiar el renderer: `cmake --build` + `ctest` + pedir a un humano la
   verificación visual (título: `foco/eventos/raton/mouse/sel/eje`).
