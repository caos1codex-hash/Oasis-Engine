#pragma once

#include <cstdint>

#include "core.hpp"

namespace oasis {

struct Runtime {
    Scene* scene = nullptr;
    std::uint64_t tick_count = 0;
    double elapsed_seconds = 0.0;
    bool initialized = false;

    bool init(Scene* s, Error& err);
    bool update(double delta_seconds, Error& err);
    void shutdown();
};

}  // namespace oasis
