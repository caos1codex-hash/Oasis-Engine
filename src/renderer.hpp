#pragma once

#include <cstdint>
#include <string>

#include "core.hpp"
#include "runtime.hpp"

namespace oasis {

enum class WindowMode {
    Windowed,    // ventana con marco, tamaño cfg.width/height
    Fullscreen,  // ventana con botones -/□/X y TOPMOST a monitor completo (tapa la barra)
    WorkArea,    // ventana con botones maximizada al área de trabajo (respeta la barra)
};

struct RenderConfig {
    int width = 800;
    int height = 600;
    std::uint64_t max_ticks = 0;  // 0 = hasta cerrar ventana
    WindowMode mode = WindowMode::Windowed;
    bool vsync = true;  // Present(1,0): ritmo del monitor, sin busy-loop al 100%
};

// Retorna 0 ok, 1 error (err con code GPU/INVALID_ARG/INTERNAL).
// out_backend describe el adaptador seleccionado ("hardware-dedicado: ...", "hardware", "warp").
int RendererRun(const Project& proj, Runtime& rt, const RenderConfig& cfg, std::string& out_backend,
                Error& err);

// Canal de control IA -> ventana (solo Windows; en otras plataformas informa error).
// Nombre estable del evento de parada para una raíz de proyecto (canoniza la ruta).
std::string StopEventNameForRoot(const std::filesystem::path& project_root);
// Señala parada a la ventana de ese proyecto. out_signaled=true si había instancia
// escuchando; false si no hay ventana (idempotente, exit 0 en ambos casos).
// Con save=true la ventana guarda la escena antes de salir (equivale a ESC -> Sí).
// Nunca toca stdout JSON del llamante más allá del resultado; no hay confirmación
// de guardado: verificar con `state` después.
bool RequestStopForRoot(const std::filesystem::path& project_root, bool& out_signaled, Error& err,
                        bool save = false);

}  // namespace oasis
