#include "worldsim/multiresolution_water_c_api.h"

#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// These opaque definitions intentionally match src/c_api.cpp so this translation unit can
// extend the C ABI without exposing C++ implementation types in public headers.
struct ws_world {
    explicit ws_world(worldsim::WorldConfig cfg) : impl(std::move(cfg)) {}
    explicit ws_world(worldsim::World world) : impl(std::move(world)) {}
    worldsim::World impl;
};

struct ws_continental_hydrology_result {
    explicit ws_continental_hydrology_result(worldsim::ContinentalHydrologyResult value) : impl(std::move(value)) {}
    worldsim::ContinentalHydrologyResult impl;
};

struct ws_multiresolution_water_state {
    explicit ws_multiresolution_water_state(worldsim::MultiresolutionWaterState value)
        : impl(std::move(value)) {}
    worldsim::MultiresolutionWaterState impl;
};

namespace {
thread_local std::string g_multiresolution_last_error;

template <typename F>
int guarded(F&& fn) {
    try {
        fn();
        g_multiresolution_last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        g_multiresolution_last_error = e.what();
        return -1;
    } catch (...) {
        g_multiresolution_last_error = "unknown WorldSim multiresolution water error";
        return -1;
    }
}

worldsim::DynamicHydrologyParameters dynamic_parameters_from_c(
    const ws_dynamic_hydrology_parameters* p) {
    if (!p) return {};
    worldsim::DynamicHydrologyParameters out;
    out.soil_capacity_mm = p->soil_capacity_mm;
    out.field_capacity_mm = p->field_capacity_mm;
    out.wilting_point_mm = p->wilting_point_mm;
    out.infiltration_capacity_mm_per_day = p->infiltration_capacity_mm_per_day;
    out.surface_storage_capacity_mm = p->surface_storage_capacity_mm;
    out.percolation_rate_per_day = p->percolation_rate_per_day;
    out.groundwater_recession_per_day = p->groundwater_recession_per_day;
    out.snow_melt_mm_per_c_day = p->snow_melt_mm_per_c_day;
    out.initial_soil_water_mm = p->initial_soil_water_mm;
    out.initial_groundwater_mm = p->initial_groundwater_mm;
    out.validate();
    return out;
}

void copy_coarse_cell(
    const worldsim::ContinentalWaterState& coarse,
    std::size_t index,
    ws_continental_water_cell_state& out) {
    const auto coord = coarse.coord_of(index);
    const auto& in = coarse.cells()[index];
    out.cell_x = coord.x;
    out.cell_y = coord.y;
    out.snow_water_equivalent_mm = in.snow_water_equivalent_mm;
    out.surface_water_mm = in.surface_water_mm;
    out.soil_water_mm = in.soil_water_mm;
    out.groundwater_mm = in.groundwater_mm;
    out.last_evapotranspiration_mm = in.last_evapotranspiration_mm;
    out.last_quick_runoff_mm = in.last_quick_runoff_mm;
    out.last_baseflow_mm = in.last_baseflow_mm;
    out.last_routed_discharge_m3_s = in.last_routed_discharge_m3_s;
}

void copy_refined_cell(
    const worldsim::DynamicHydrologyCellState& in,
    ws_dynamic_hydrology_cell_state& out) {
    out.cell_x = in.coord.x;
    out.cell_y = in.coord.y;
    out.active = in.active ? 1 : 0;
    out.snow_water_equivalent_mm = in.snow_water_equivalent_mm;
    out.surface_water_mm = in.surface_water_mm;
    out.soil_water_mm = in.soil_water_mm;
    out.groundwater_mm = in.groundwater_mm;
    out.last_evapotranspiration_mm = in.last_evapotranspiration_mm;
    out.last_quick_runoff_mm = in.last_quick_runoff_mm;
    out.last_baseflow_mm = in.last_baseflow_mm;
    out.last_routed_discharge_m3_s = in.last_routed_discharge_m3_s;
}
} // namespace

extern "C" {

ws_multiresolution_water_state* ws_world_multiresolution_water_create(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    const ws_dynamic_hydrology_parameters* parameters) {
    try {
        if (!world || !continent) throw std::invalid_argument("world/continent is null");
        const auto params = dynamic_parameters_from_c(parameters);
        auto state = worldsim::make_multiresolution_water_state(world->impl, continent->impl, params);
        auto out = std::make_unique<ws_multiresolution_water_state>(std::move(state));
        g_multiresolution_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_multiresolution_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_multiresolution_last_error = "unknown WorldSim multiresolution water error";
        return nullptr;
    }
}

void ws_multiresolution_water_state_destroy(ws_multiresolution_water_state* state) {
    delete state;
}

int64_t ws_multiresolution_water_simulated_day(const ws_multiresolution_water_state* state) {
    return state ? state->impl.simulated_day() : 0;
}

uint64_t ws_multiresolution_water_refined_tile_count(const ws_multiresolution_water_state* state) {
    return state ? static_cast<uint64_t>(state->impl.refined_tile_count()) : 0;
}

int ws_multiresolution_water_is_refined(
    const ws_multiresolution_water_state* state,
    int64_t climate_x,
    int64_t climate_y) {
    if (!state) {
        g_multiresolution_last_error = "state is null";
        return -1;
    }
    g_multiresolution_last_error.clear();
    return state->impl.is_refined({climate_x, climate_y}) ? 1 : 0;
}

int ws_multiresolution_water_channel_storage_m3(
    const ws_multiresolution_water_state* state,
    int64_t climate_x,
    int64_t climate_y,
    double* out_volume_m3) {
    return guarded([&] {
        if (!state || !out_volume_m3) throw std::invalid_argument("state/out_volume_m3 is null");
        *out_volume_m3 = state->impl.channel_storage_m3({climate_x, climate_y});
    });
}

int ws_multiresolution_water_total_channel_storage_m3(
    const ws_multiresolution_water_state* state,
    double* out_volume_m3) {
    return guarded([&] {
        if (!state || !out_volume_m3) throw std::invalid_argument("state/out_volume_m3 is null");
        *out_volume_m3 = state->impl.total_channel_storage_m3();
    });
}

int ws_multiresolution_water_materialize(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    ws_multiresolution_water_state* state,
    int64_t climate_x,
    int64_t climate_y) {
    return guarded([&] {
        if (!world || !continent || !state) throw std::invalid_argument("world/continent/state is null");
        (void)worldsim::materialize_refined_water_tile(
            world->impl, continent->impl, state->impl, {climate_x, climate_y});
    });
}

int ws_multiresolution_water_aggregate(
    ws_world* world,
    ws_multiresolution_water_state* state,
    int64_t climate_x,
    int64_t climate_y) {
    return guarded([&] {
        if (!world || !state) throw std::invalid_argument("world/state is null");
        worldsim::aggregate_refined_water_tile(world->impl, state->impl, {climate_x, climate_y});
    });
}

int ws_multiresolution_water_copy_coarse_cells(
    const ws_multiresolution_water_state* state,
    ws_continental_water_cell_state* out_cells,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_cells) throw std::invalid_argument("state/out_cells is null");
        const auto& coarse = state->impl.coarse_state();
        if (capacity < coarse.cells().size()) {
            throw std::invalid_argument("multiresolution coarse output capacity is too small");
        }
        for (std::size_t i = 0; i < coarse.cells().size(); ++i) {
            copy_coarse_cell(coarse, i, out_cells[i]);
        }
    });
}

int ws_multiresolution_water_copy_refined_cells(
    const ws_multiresolution_water_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_dynamic_hydrology_cell_state* out_cells,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_cells) throw std::invalid_argument("state/out_cells is null");
        const auto& tile = state->impl.refined_tile({climate_x, climate_y});
        if (capacity < tile.cells.size()) {
            throw std::invalid_argument("multiresolution refined output capacity is too small");
        }
        for (std::size_t i = 0; i < tile.cells.size(); ++i) {
            copy_refined_cell(tile.cells[i], out_cells[i]);
        }
    });
}

int ws_multiresolution_water_make_smooth_daily_forcing(
    const ws_multiresolution_water_state* state,
    ws_continental_water_forcing* out_forcing,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_forcing) throw std::invalid_argument("state/out_forcing is null");
        const auto forcing = worldsim::make_smooth_continental_daily_forcing(state->impl.coarse_state());
        if (capacity < forcing.size()) {
            throw std::invalid_argument("multiresolution forcing output capacity is too small");
        }
        for (std::size_t i = 0; i < forcing.size(); ++i) {
            out_forcing[i].precipitation_mm = forcing[i].precipitation_mm;
            out_forcing[i].mean_air_temperature_c = forcing[i].mean_air_temperature_c;
            out_forcing[i].potential_evapotranspiration_mm = forcing[i].potential_evapotranspiration_mm;
        }
    });
}

int ws_multiresolution_water_advance_day(
    ws_world* world,
    ws_multiresolution_water_state* state,
    const ws_continental_water_forcing* forcing,
    uint64_t forcing_count,
    ws_continental_water_step_report* out_report) {
    return guarded([&] {
        if (!world || !state || !forcing || !out_report) {
            throw std::invalid_argument("world/state/forcing/out_report is null");
        }
        const auto expected = state->impl.coarse_state().cells().size();
        if (forcing_count != expected) {
            throw std::invalid_argument("multiresolution forcing count must equal L0 cell count");
        }
        std::vector<worldsim::ContinentalWaterForcing> cpp_forcing;
        cpp_forcing.reserve(static_cast<std::size_t>(forcing_count));
        for (uint64_t i = 0; i < forcing_count; ++i) {
            cpp_forcing.push_back({forcing[i].precipitation_mm,
                                   forcing[i].mean_air_temperature_c,
                                   forcing[i].potential_evapotranspiration_mm});
        }
        const auto report = worldsim::advance_multiresolution_water_day(
            world->impl, state->impl, cpp_forcing);
        out_report->day_before = report.day_before;
        out_report->day_after = report.day_after;
        out_report->storage_before_m3 = report.storage_before_m3;
        out_report->precipitation_m3 = report.precipitation_m3;
        out_report->evapotranspiration_m3 = report.evapotranspiration_m3;
        out_report->terminal_outflow_m3 = report.terminal_outflow_m3;
        out_report->storage_after_m3 = report.storage_after_m3;
        out_report->water_balance_error_m3 = report.water_balance_error_m3;
    });
}

int ws_multiresolution_water_save(
    const ws_multiresolution_water_state* state,
    const char* utf8_path) {
    return guarded([&] {
        if (!state || !utf8_path) throw std::invalid_argument("state/path is null");
        worldsim::save_multiresolution_water_state(state->impl, utf8_path);
    });
}

ws_multiresolution_water_state* ws_multiresolution_water_load(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    const char* utf8_path) {
    try {
        if (!world || !continent || !utf8_path) throw std::invalid_argument("world/continent/path is null");
        auto state = worldsim::load_multiresolution_water_state(world->impl, continent->impl, utf8_path);
        auto out = std::make_unique<ws_multiresolution_water_state>(std::move(state));
        g_multiresolution_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_multiresolution_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_multiresolution_last_error = "unknown WorldSim multiresolution water error";
        return nullptr;
    }
}

const char* ws_multiresolution_last_error(void) {
    return g_multiresolution_last_error.c_str();
}

} // extern "C"
