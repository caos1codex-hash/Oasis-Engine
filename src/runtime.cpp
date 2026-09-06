#include "runtime.hpp"

#include <cmath>

namespace oasis {

bool Runtime::init(Scene* s, Error& err) {
    err.clear();
    if (s == nullptr) {
        err.set("INVALID_ARG", "Runtime requiere una escena.");
        return false;
    }
    scene = s;
    tick_count = 0;
    elapsed_seconds = 0.0;
    initialized = true;
    return true;
}

bool Runtime::update(double delta_seconds, Error& err) {
    err.clear();
    if (!initialized || scene == nullptr) {
        err.set("INVALID_ARG", "Runtime no inicializado.");
        return false;
    }
    if (std::isfinite(delta_seconds) == 0 || delta_seconds < 0.0 || delta_seconds > 10.0) {
        err.set("INVALID_ARG", "Delta de runtime inválido.");
        return false;
    }
    ++tick_count;
    elapsed_seconds += delta_seconds;
    if (std::isfinite(elapsed_seconds) == 0) {
        err.set("INTERNAL", "Tiempo de runtime fuera de rango.");
        return false;
    }
    return true;
}

void Runtime::shutdown() {
    initialized = false;
    scene = nullptr;
    // tick_count/elapsed se conservan para diagnóstico post-run
}

}  // namespace oasis
