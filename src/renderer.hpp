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

}  // namespace oasis
