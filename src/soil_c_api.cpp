#include "worldsim/soil_c_api.h"

#include "worldsim/world.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

// This opaque definition intentionally matches src/c_api.cpp so the extension can reuse
// the existing world handle without exposing C++ implementation types in public headers.
struct ws_world {
    explicit ws_world(worldsim::WorldConfig cfg) : impl(std::move(cfg)) {}
    explicit ws_world(worldsim::World world) : impl(std::move(world)) {}
    worldsim::World impl;
};

namespace {
thread_local std::string g_soil_last_error;

template <typename F>
int guarded(F&& fn) {
    try {
        fn();
        g_soil_last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        g_soil_last_error = e.what();
        return -1;
    } catch (...) {
        g_soil_last_error = "unknown WorldSim soil error";
        return -1;
    }
}

void copy_properties(const worldsim::SoilProperties& in, ws_soil_properties& out) {
    out.storage_capacity_scale = in.storage_capacity_scale;
    out.infiltration_capacity_scale = in.infiltration_capacity_scale;
}
} // namespace

extern "C" {

int ws_world_sample_soil(
    ws_world* world,
    double x_m,
    double y_m,
    ws_soil_properties* out_properties) {
    return guarded([&] {
        if (!world || !out_properties) throw std::invalid_argument("world/out_properties is null");
        copy_properties(world->impl.sample_soil({x_m, y_m}), *out_properties);
    });
}

int ws_world_sample_climate_soil(
    ws_world* world,
    int64_t climate_x,
    int64_t climate_y,
    ws_soil_properties* out_properties) {
    return guarded([&] {
        if (!world || !out_properties) throw std::invalid_argument("world/out_properties is null");
        copy_properties(world->impl.sample_climate_soil({climate_x, climate_y}), *out_properties);
    });
}

const char* ws_soil_last_error(void) {
    return g_soil_last_error.c_str();
}

} // extern "C"
