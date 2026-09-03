#include "worldsim/simulation_c_api.h"

#include "worldsim/simulation.hpp"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

struct ws_simulation_state {
    explicit ws_simulation_state(worldsim::SimulationState value) : impl(std::move(value)) {}
    worldsim::SimulationState impl;
};

namespace {
thread_local std::string g_simulation_last_error;

template <typename F>
int guarded(F&& fn) {
    try {
        fn();
        g_simulation_last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        g_simulation_last_error = e.what();
        return -1;
    } catch (...) {
        g_simulation_last_error = "unknown WorldSim simulation error";
        return -1;
    }
}

worldsim::WorldConfig world_config_from_c(const ws_world_config& in) {
    worldsim::WorldConfig out;
    out.seed = in.seed;
    out.bounds = {in.origin_x_m, in.origin_y_m, in.width_m, in.height_m};
    out.sea_level_m = in.sea_level_m;
    out.validate();
    return out;
}

worldsim::WeatherParameters weather_parameters_from_c(const ws_weather_parameters* in) {
    if (!in) return {};
    worldsim::WeatherParameters out;
    out.temperature_memory = in->temperature_memory;
    out.moisture_memory = in->moisture_memory;
    out.temperature_variability_c = in->temperature_variability_c;
    out.moisture_variability = in->moisture_variability;
    out.storm_threshold = in->storm_threshold;
    out.storm_intensity = in->storm_intensity;
    out.validate();
    return out;
}

worldsim::DynamicHydrologyParameters water_parameters_from_c(
    const ws_dynamic_hydrology_parameters* in) {
    if (!in) return {};
    worldsim::DynamicHydrologyParameters out;
    out.soil_capacity_mm = in->soil_capacity_mm;
    out.field_capacity_mm = in->field_capacity_mm;
    out.wilting_point_mm = in->wilting_point_mm;
    out.infiltration_capacity_mm_per_day = in->infiltration_capacity_mm_per_day;
    out.surface_storage_capacity_mm = in->surface_storage_capacity_mm;
    out.percolation_rate_per_day = in->percolation_rate_per_day;
    out.groundwater_recession_per_day = in->groundwater_recession_per_day;
    out.snow_melt_mm_per_c_day = in->snow_melt_mm_per_c_day;
    out.initial_soil_water_mm = in->initial_soil_water_mm;
    out.initial_groundwater_mm = in->initial_groundwater_mm;
    out.validate();
    return out;
}

void copy_weather_report(const worldsim::WeatherStepReport& in, ws_weather_step_report& out) {
    out.day_before = in.day_before;
    out.day_after = in.day_after;
    out.precipitation_m3 = in.precipitation_m3;
    out.mean_air_temperature_c = in.mean_air_temperature_c;
    out.mean_potential_evapotranspiration_mm = in.mean_potential_evapotranspiration_mm;
    out.wet_area_fraction = in.wet_area_fraction;
}

void copy_water_report(
    const worldsim::ContinentalWaterStepReport& in,
    ws_continental_water_step_report& out) {
    out.day_before = in.day_before;
    out.day_after = in.day_after;
    out.storage_before_m3 = in.storage_before_m3;
    out.precipitation_m3 = in.precipitation_m3;
    out.evapotranspiration_m3 = in.evapotranspiration_m3;
    out.terminal_outflow_m3 = in.terminal_outflow_m3;
    out.storage_after_m3 = in.storage_after_m3;
    out.water_balance_error_m3 = in.water_balance_error_m3;
}

void copy_weather_sample(const worldsim::WeatherCellSample& in, ws_weather_cell_sample& out) {
    out.cell_x = in.coord.x;
    out.cell_y = in.coord.y;
    out.temperature_anomaly_c = in.temperature_anomaly_c;
    out.moisture_anomaly = in.moisture_anomaly;
    out.precipitation_mm = in.precipitation_mm;
    out.mean_air_temperature_c = in.mean_air_temperature_c;
    out.potential_evapotranspiration_mm = in.potential_evapotranspiration_mm;
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

ws_simulation_state* ws_simulation_create(
    const ws_world_config* config,
    const ws_weather_parameters* weather_parameters,
    const ws_dynamic_hydrology_parameters* water_parameters) {
    try {
        if (!config) throw std::invalid_argument("config is null");
        auto state = worldsim::SimulationState(
            world_config_from_c(*config),
            weather_parameters_from_c(weather_parameters),
            water_parameters_from_c(water_parameters));
        auto out = std::make_unique<ws_simulation_state>(std::move(state));
        g_simulation_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_simulation_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_simulation_last_error = "unknown WorldSim simulation error";
        return nullptr;
    }
}

void ws_simulation_destroy(ws_simulation_state* state) {
    delete state;
}

int64_t ws_simulation_simulated_day(const ws_simulation_state* state) {
    return state ? state->impl.simulated_day() : 0;
}

uint64_t ws_simulation_l0_cell_count(const ws_simulation_state* state) {
    if (!state) return 0;
    return static_cast<uint64_t>(state->impl.water().coarse_state().cells().size());
}

uint64_t ws_simulation_materialized_patch_count(const ws_simulation_state* state) {
    if (!state) return 0;
    return static_cast<uint64_t>(state->impl.world().materialized_patch_count());
}

uint64_t ws_simulation_refined_tile_count(const ws_simulation_state* state) {
    if (!state) return 0;
    return static_cast<uint64_t>(state->impl.water().refined_tile_count());
}

int ws_simulation_is_refined(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y) {
    if (!state) {
        g_simulation_last_error = "state is null";
        return -1;
    }
    g_simulation_last_error.clear();
    return state->impl.water().is_refined({climate_x, climate_y}) ? 1 : 0;
}

int ws_simulation_sample_region(
    const ws_simulation_state* state,
    double x_m,
    double y_m,
    ws_regional_sample* out_sample) {
    return guarded([&] {
        if (!state || !out_sample) throw std::invalid_argument("state/out_sample is null");
        const auto sample = state->impl.world().sample_region({x_m, y_m});
        out_sample->cell_x = sample.coord.x;
        out_sample->cell_y = sample.coord.y;
        out_sample->elevation_m = sample.elevation_m;
        out_sample->slope = sample.slope;
        out_sample->terrain_roughness = sample.terrain_roughness;
        out_sample->bedrock_hardness = sample.bedrock_hardness;
        out_sample->forest_potential = sample.forest_potential;
    });
}

int ws_simulation_sample_weather(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_weather_cell_sample* out_sample) {
    return guarded([&] {
        if (!state || !out_sample) throw std::invalid_argument("state/out_sample is null");
        copy_weather_sample(
            worldsim::sample_weather(state->impl.weather(), {climate_x, climate_y}),
            *out_sample);
    });
}

int ws_simulation_copy_coarse_water_cells(
    const ws_simulation_state* state,
    ws_continental_water_cell_state* out_cells,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_cells) throw std::invalid_argument("state/out_cells is null");
        const auto& coarse = state->impl.water().coarse_state();
        if (capacity < coarse.cells().size()) {
            throw std::invalid_argument("simulation coarse output capacity is too small");
        }
        for (std::size_t i = 0; i < coarse.cells().size(); ++i) {
            copy_coarse_cell(coarse, i, out_cells[i]);
        }
    });
}

int ws_simulation_copy_refined_water_cells(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_dynamic_hydrology_cell_state* out_cells,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_cells) throw std::invalid_argument("state/out_cells is null");
        const auto& tile = state->impl.water().refined_tile({climate_x, climate_y});
        if (capacity < tile.cells.size()) {
            throw std::invalid_argument("simulation refined output capacity is too small");
        }
        for (std::size_t i = 0; i < tile.cells.size(); ++i) {
            copy_refined_cell(tile.cells[i], out_cells[i]);
        }
    });
}

int ws_simulation_advance_day(
    ws_simulation_state* state,
    ws_weather_water_step_report* out_report) {
    return guarded([&] {
        if (!state || !out_report) throw std::invalid_argument("state/out_report is null");
        const auto report = state->impl.advance_day();
        copy_weather_report(report.weather, out_report->weather);
        copy_water_report(report.water, out_report->water);
    });
}

int ws_simulation_materialize_refined_water_tile(
    ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y) {
    return guarded([&] {
        if (!state) throw std::invalid_argument("state is null");
        (void)state->impl.materialize_refined_water_tile({climate_x, climate_y});
    });
}

int ws_simulation_aggregate_refined_water_tile(
    ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y) {
    return guarded([&] {
        if (!state) throw std::invalid_argument("state is null");
        state->impl.aggregate_refined_water_tile({climate_x, climate_y});
    });
}

int ws_simulation_disturb_surface(
    ws_simulation_state* state,
    double min_x_m,
    double min_y_m,
    double max_x_m,
    double max_y_m,
    float amount,
    uint64_t* out_affected_cells) {
    return guarded([&] {
        if (!state || !out_affected_cells) {
            throw std::invalid_argument("state/out_affected_cells is null");
        }
        *out_affected_cells = static_cast<uint64_t>(state->impl.disturb_surface(
            {min_x_m, min_y_m}, {max_x_m, max_y_m}, amount));
    });
}

int ws_simulation_save_checkpoint(
    const ws_simulation_state* state,
    const char* utf8_path) {
    return guarded([&] {
        if (!state || !utf8_path) throw std::invalid_argument("state/path is null");
        state->impl.save_checkpoint(utf8_path);
    });
}

ws_simulation_state* ws_simulation_load_checkpoint(const char* utf8_path) {
    try {
        if (!utf8_path) throw std::invalid_argument("path is null");
        auto state = worldsim::SimulationState::load_checkpoint(utf8_path);
        auto out = std::make_unique<ws_simulation_state>(std::move(state));
        g_simulation_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_simulation_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_simulation_last_error = "unknown WorldSim simulation error";
        return nullptr;
    }
}

const char* ws_simulation_last_error(void) {
    return g_simulation_last_error.c_str();
}

} // extern "C"
