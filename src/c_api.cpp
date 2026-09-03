#include "worldsim/c_api.h"
#include "worldsim/world.hpp"
#include "worldsim/dynamic_hydrology.hpp"
#include "worldsim/continental_water.hpp"

#include <exception>
#include <memory>
#include <string>

static_assert(WS_MAX_HYDROLOGY_CELLS == worldsim::kMaxHydrologyCells);
static_assert(WS_MAX_CONTINENTAL_HYDROLOGY_CELLS == worldsim::kMaxContinentalHydrologyCells);

struct ws_world {
    explicit ws_world(worldsim::WorldConfig cfg) : impl(std::move(cfg)) {}
    explicit ws_world(worldsim::World world) : impl(std::move(world)) {}
    worldsim::World impl;
};

struct ws_hydrology_result {
    explicit ws_hydrology_result(worldsim::HydrologyResult value) : impl(std::move(value)) {}
    worldsim::HydrologyResult impl;
};

struct ws_continental_hydrology_result {
    explicit ws_continental_hydrology_result(worldsim::ContinentalHydrologyResult value) : impl(std::move(value)) {}
    worldsim::ContinentalHydrologyResult impl;
};

struct ws_continental_water_state {
    ws_continental_water_state(worldsim::ContinentalWaterState value,
                               worldsim::DynamicHydrologyParameters params)
        : impl(std::move(value)), parameters(params) {}
    worldsim::ContinentalWaterState impl;
    worldsim::DynamicHydrologyParameters parameters;
};

struct ws_dynamic_hydrology_state {
    ws_dynamic_hydrology_state(worldsim::AuthoritativeHydrologyTile topology,
                               worldsim::DynamicHydrologyTileState value,
                               worldsim::DynamicHydrologyParameters params)
        : tile(std::move(topology)), impl(std::move(value)), parameters(params) {}
    worldsim::AuthoritativeHydrologyTile tile;
    worldsim::DynamicHydrologyTileState impl;
    worldsim::DynamicHydrologyParameters parameters;
};

namespace {
thread_local std::string g_last_error;

template <typename F>
int guarded(F&& fn) {
    try {
        fn();
        g_last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return -1;
    } catch (...) {
        g_last_error = "unknown WorldSim error";
        return -1;
    }
}

worldsim::DynamicHydrologyParameters dynamic_parameters_from_c(const ws_dynamic_hydrology_parameters* p) {
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
}

extern "C" {

ws_world* ws_world_create(const ws_world_config* config) {
    try {
        if (!config) throw std::invalid_argument("config is null");
        worldsim::WorldConfig cfg;
        cfg.seed = config->seed;
        cfg.bounds = {config->origin_x_m, config->origin_y_m, config->width_m, config->height_m};
        cfg.sea_level_m = config->sea_level_m;
        auto out = std::make_unique<ws_world>(cfg);
        g_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "unknown WorldSim error";
        return nullptr;
    }
}

void ws_world_destroy(ws_world* world) { delete world; }

int ws_world_sample_region(ws_world* world, double x_m, double y_m, ws_regional_sample* out_sample) {
    return guarded([&] {
        if (!world || !out_sample) throw std::invalid_argument("world/out_sample is null");
        const auto s = world->impl.sample_region(worldsim::WorldPosition{x_m, y_m});
        out_sample->cell_x = s.coord.x;
        out_sample->cell_y = s.coord.y;
        out_sample->elevation_m = s.elevation_m;
        out_sample->slope = s.slope;
        out_sample->terrain_roughness = s.terrain_roughness;
        out_sample->bedrock_hardness = s.bedrock_hardness;
        out_sample->forest_potential = s.forest_potential;
    });
}

int ws_world_materialize_region(ws_world* world, int64_t region_x, int64_t region_y) {
    return guarded([&] {
        if (!world) throw std::invalid_argument("world is null");
        (void)world->impl.materialize_local_patch({region_x, region_y});
    });
}

int ws_world_copy_local_patch(ws_world* world, int64_t region_x, int64_t region_y,
                              ws_local_cell* out_cells, uint64_t capacity) {
    return guarded([&] {
        if (!world || !out_cells) throw std::invalid_argument("world/out_cells is null");
        if (capacity < WS_LOCAL_PATCH_CELL_COUNT) throw std::invalid_argument("local patch output capacity must be at least 256");
        const auto& patch = world->impl.materialize_local_patch({region_x, region_y});
        for (std::size_t i = 0; i < worldsim::kLocalCellCount; ++i) {
            out_cells[i].elevation_m = patch.cells[i].elevation_m;
            out_cells[i].terrain_roughness = patch.cells[i].terrain_roughness;
            out_cells[i].forest_potential = patch.cells[i].forest_potential;
            out_cells[i].disturbance = patch.cells[i].disturbance;
        }
    });
}

uint64_t ws_world_materialized_patch_count(ws_world* world) {
    if (!world) return 0;
    return static_cast<uint64_t>(world->impl.materialized_patch_count());
}

ws_continental_hydrology_result* ws_world_analyze_continental_hydrology(
    ws_world* world, float river_threshold_m3_s) {
    try {
        if (!world) throw std::invalid_argument("world is null");
        worldsim::ContinentalHydrologyRequest request;
        request.river_threshold_m3_s = river_threshold_m3_s;
        auto out = std::make_unique<ws_continental_hydrology_result>(
            world->impl.analyze_continental_hydrology(request));
        g_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "unknown WorldSim error";
        return nullptr;
    }
}

void ws_continental_hydrology_result_destroy(ws_continental_hydrology_result* result) { delete result; }

uint64_t ws_continental_hydrology_cell_count(const ws_continental_hydrology_result* result) {
    return result ? static_cast<uint64_t>(result->impl.cells.size()) : 0;
}

int ws_continental_hydrology_copy_cells(
    const ws_continental_hydrology_result* result,
    ws_continental_hydrology_cell* out_cells,
    uint64_t capacity) {
    return guarded([&] {
        if (!result || !out_cells) throw std::invalid_argument("result/out_cells is null");
        if (capacity < result->impl.cells.size()) throw std::invalid_argument("continental hydrology output capacity is too small");
        for (std::size_t i = 0; i < result->impl.cells.size(); ++i) {
            const auto& in = result->impl.cells[i];
            auto& out = out_cells[i];
            out.cell_x = in.coord.x;
            out.cell_y = in.coord.y;
            out.terrain_elevation_m = in.terrain_elevation_m;
            out.filled_elevation_m = in.filled_elevation_m;
            out.depression_depth_m = in.depression_depth_m;
            out.local_water_yield_m3_s = in.local_water_yield_m3_s;
            out.accumulated_discharge_m3_s = in.accumulated_discharge_m3_s;
            out.downstream_x = in.downstream_coord.x;
            out.downstream_y = in.downstream_coord.y;
            out.has_downstream = in.has_downstream ? 1 : 0;
            out.terminal_outlet_x = in.terminal_outlet_coord.x;
            out.terminal_outlet_y = in.terminal_outlet_coord.y;
            out.basin_id = in.basin_id;
            out.ocean = in.ocean ? 1 : 0;
            out.river = in.river ? 1 : 0;
        }
    });
}

ws_hydrology_result* ws_world_refine_authoritative_hydrology_tile(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    int64_t climate_x,
    int64_t climate_y,
    float river_threshold_m3_s,
    float lake_min_depth_m) {
    try {
        if (!world || !continent) throw std::invalid_argument("world/continent is null");
        auto tile = world->impl.refine_authoritative_hydrology_tile(
            continent->impl, {climate_x, climate_y}, river_threshold_m3_s, lake_min_depth_m);
        auto out = std::make_unique<ws_hydrology_result>(std::move(tile.hydrology));
        g_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "unknown WorldSim error";
        return nullptr;
    }
}

ws_continental_water_state* ws_world_continental_water_create(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    const ws_dynamic_hydrology_parameters* parameters) {
    try {
        if (!world || !continent) throw std::invalid_argument("world/continent is null");
        auto params = dynamic_parameters_from_c(parameters);
        auto state = worldsim::make_continental_water_state(world->impl, continent->impl, params);
        auto out = std::make_unique<ws_continental_water_state>(std::move(state), params);
        g_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "unknown WorldSim error";
        return nullptr;
    }
}

void ws_continental_water_state_destroy(ws_continental_water_state* state) { delete state; }

uint64_t ws_continental_water_cell_count(const ws_continental_water_state* state) {
    return state ? static_cast<uint64_t>(state->impl.cells().size()) : 0;
}

int64_t ws_continental_water_simulated_day(const ws_continental_water_state* state) {
    return state ? state->impl.simulated_day() : 0;
}

int ws_continental_water_copy_cells(
    const ws_continental_water_state* state,
    ws_continental_water_cell_state* out_cells,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_cells) throw std::invalid_argument("state/out_cells is null");
        if (capacity < state->impl.cells().size()) {
            throw std::invalid_argument("continental water output capacity is too small");
        }
        for (std::size_t i = 0; i < state->impl.cells().size(); ++i) {
            const auto coord = state->impl.coord_of(i);
            const auto& in = state->impl.cells()[i];
            auto& out = out_cells[i];
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
    });
}

int ws_continental_water_make_smooth_daily_forcing(
    const ws_continental_water_state* state,
    ws_continental_water_forcing* out_forcing,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_forcing) throw std::invalid_argument("state/out_forcing is null");
        const auto forcing = worldsim::make_smooth_continental_daily_forcing(state->impl);
        if (capacity < forcing.size()) throw std::invalid_argument("continental forcing output capacity is too small");
        for (std::size_t i = 0; i < forcing.size(); ++i) {
            out_forcing[i].precipitation_mm = forcing[i].precipitation_mm;
            out_forcing[i].mean_air_temperature_c = forcing[i].mean_air_temperature_c;
            out_forcing[i].potential_evapotranspiration_mm = forcing[i].potential_evapotranspiration_mm;
        }
    });
}

int ws_continental_water_advance_day(
    ws_continental_water_state* state,
    const ws_continental_water_forcing* forcing,
    uint64_t forcing_count,
    ws_continental_water_step_report* out_report) {
    return guarded([&] {
        if (!state || !forcing || !out_report) throw std::invalid_argument("state/forcing/out_report is null");
        if (forcing_count != state->impl.cells().size()) {
            throw std::invalid_argument("continental forcing count must equal state cell count");
        }
        std::vector<worldsim::ContinentalWaterForcing> cpp_forcing;
        cpp_forcing.reserve(static_cast<std::size_t>(forcing_count));
        for (uint64_t i = 0; i < forcing_count; ++i) {
            cpp_forcing.push_back({forcing[i].precipitation_mm,
                                   forcing[i].mean_air_temperature_c,
                                   forcing[i].potential_evapotranspiration_mm});
        }
        const auto report = worldsim::advance_continental_water_day(
            state->impl, cpp_forcing, state->parameters);
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

ws_dynamic_hydrology_state* ws_world_dynamic_hydrology_create(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    int64_t climate_x,
    int64_t climate_y,
    const ws_dynamic_hydrology_parameters* parameters) {
    try {
        if (!world || !continent) throw std::invalid_argument("world/continent is null");
        auto params = dynamic_parameters_from_c(parameters);
        auto tile = world->impl.refine_authoritative_hydrology_tile(
            continent->impl, {climate_x, climate_y}, 0.5f, 0.25f);
        auto state = worldsim::make_dynamic_hydrology_tile_state(world->impl, tile, params);
        auto out = std::make_unique<ws_dynamic_hydrology_state>(std::move(tile), std::move(state), params);
        g_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "unknown WorldSim error";
        return nullptr;
    }
}

void ws_dynamic_hydrology_state_destroy(ws_dynamic_hydrology_state* state) { delete state; }

uint64_t ws_dynamic_hydrology_cell_count(const ws_dynamic_hydrology_state* state) {
    return state ? static_cast<uint64_t>(state->impl.cells.size()) : 0;
}

double ws_dynamic_hydrology_simulated_days(const ws_dynamic_hydrology_state* state) {
    return state ? state->impl.simulated_days : 0.0;
}

int ws_dynamic_hydrology_copy_cells(
    const ws_dynamic_hydrology_state* state,
    ws_dynamic_hydrology_cell_state* out_cells,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_cells) throw std::invalid_argument("state/out_cells is null");
        if (capacity < state->impl.cells.size()) throw std::invalid_argument("dynamic hydrology output capacity is too small");
        for (std::size_t i = 0; i < state->impl.cells.size(); ++i) {
            const auto& in = state->impl.cells[i];
            auto& out = out_cells[i];
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
    });
}

int ws_dynamic_hydrology_make_smooth_climatological_forcing(
    ws_world* world,
    const ws_dynamic_hydrology_state* state,
    double day_of_year,
    double duration_days,
    ws_hydrometeorological_forcing* out_forcing,
    uint64_t capacity) {
    return guarded([&] {
        if (!world || !state || !out_forcing) throw std::invalid_argument("world/state/out_forcing is null");
        const auto forcing = worldsim::make_smooth_climatological_forcing(
            world->impl, state->tile, day_of_year, duration_days);
        if (capacity < forcing.size()) throw std::invalid_argument("forcing output capacity is too small");
        for (std::size_t i = 0; i < forcing.size(); ++i) {
            out_forcing[i].cell_x = forcing[i].coord.x;
            out_forcing[i].cell_y = forcing[i].coord.y;
            out_forcing[i].precipitation_mm = forcing[i].precipitation_mm;
            out_forcing[i].mean_air_temperature_c = forcing[i].mean_air_temperature_c;
            out_forcing[i].potential_evapotranspiration_mm = forcing[i].potential_evapotranspiration_mm;
        }
    });
}

int ws_dynamic_hydrology_advance(
    ws_world* world,
    ws_dynamic_hydrology_state* state,
    const ws_hydrometeorological_forcing* forcing,
    uint64_t forcing_count,
    const ws_external_hydrology_inflow* external_inflows,
    uint64_t external_inflow_count,
    double duration_days,
    ws_hydrology_step_report* out_report) {
    return guarded([&] {
        if (!world || !state || !forcing || !out_report) throw std::invalid_argument("world/state/forcing/out_report is null");
        if (forcing_count != state->impl.cells.size()) throw std::invalid_argument("forcing count must equal dynamic hydrology tile cell count");
        if (external_inflow_count > 0 && !external_inflows) throw std::invalid_argument("external inflow array is null");
        std::vector<worldsim::HydrometeorologicalForcing> f;
        f.reserve(static_cast<std::size_t>(forcing_count));
        for (uint64_t i = 0; i < forcing_count; ++i) {
            f.push_back({{forcing[i].cell_x, forcing[i].cell_y}, forcing[i].precipitation_mm,
                         forcing[i].mean_air_temperature_c, forcing[i].potential_evapotranspiration_mm});
        }
        std::vector<worldsim::ExternalHydrologyInflow> inflows;
        inflows.reserve(static_cast<std::size_t>(external_inflow_count));
        for (uint64_t i = 0; i < external_inflow_count; ++i) {
            inflows.push_back({{external_inflows[i].cell_x, external_inflows[i].cell_y}, external_inflows[i].volume_m3});
        }
        const auto r = worldsim::advance_dynamic_hydrology_tile(
            world->impl, state->tile, state->impl, f, inflows, duration_days, state->parameters);
        out_report->duration_days = r.duration_days;
        out_report->storage_before_m3 = r.storage_before_m3;
        out_report->precipitation_m3 = r.precipitation_m3;
        out_report->external_inflow_m3 = r.external_inflow_m3;
        out_report->evapotranspiration_m3 = r.evapotranspiration_m3;
        out_report->external_outflow_m3 = r.external_outflow_m3;
        out_report->storage_after_m3 = r.storage_after_m3;
        out_report->water_balance_error_m3 = r.water_balance_error_m3;
    });
}

ws_hydrology_result* ws_world_analyze_hydrology(ws_world* world, const ws_hydrology_request* request) {
    try {
        if (!world || !request) throw std::invalid_argument("world/request is null");
        worldsim::HydrologyRequest r;
        r.min_coord = {request->min_region_x, request->min_region_y};
        r.width_cells = request->width_cells;
        r.height_cells = request->height_cells;
        r.river_threshold_m3_s = request->river_threshold_m3_s;
        r.lake_min_depth_m = request->lake_min_depth_m;
        auto out = std::make_unique<ws_hydrology_result>(world->impl.analyze_hydrology(r));
        g_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "unknown WorldSim error";
        return nullptr;
    }
}

void ws_hydrology_result_destroy(ws_hydrology_result* result) { delete result; }

uint64_t ws_hydrology_cell_count(const ws_hydrology_result* result) {
    return result ? static_cast<uint64_t>(result->impl.cells.size()) : 0;
}

uint64_t ws_hydrology_lake_count(const ws_hydrology_result* result) {
    return result ? static_cast<uint64_t>(result->impl.lakes.size()) : 0;
}

uint64_t ws_hydrology_river_segment_count(const ws_hydrology_result* result) {
    return result ? static_cast<uint64_t>(result->impl.river_segments.size()) : 0;
}

int ws_hydrology_copy_cells(const ws_hydrology_result* result, ws_hydrology_cell* out_cells, uint64_t capacity) {
    return guarded([&] {
        if (!result || !out_cells) throw std::invalid_argument("result/out_cells is null");
        if (capacity < result->impl.cells.size()) throw std::invalid_argument("hydrology cell output capacity is too small");
        for (std::size_t i = 0; i < result->impl.cells.size(); ++i) {
            const auto& in = result->impl.cells[i];
            auto& out = out_cells[i];
            out.cell_x = in.coord.x;
            out.cell_y = in.coord.y;
            out.active = in.active ? 1 : 0;
            out.ocean = in.ocean ? 1 : 0;
            out.terrain_elevation_m = in.terrain_elevation_m;
            out.filled_elevation_m = in.filled_elevation_m;
            out.depression_depth_m = in.depression_depth_m;
            out.local_water_yield_m3_s = in.local_water_yield_m3_s;
            out.accumulated_discharge_m3_s = in.accumulated_discharge_m3_s;
            out.downstream_x = in.downstream_coord.x;
            out.downstream_y = in.downstream_coord.y;
            out.has_downstream = in.has_downstream ? 1 : 0;
            out.downstream_is_external = in.downstream_is_external ? 1 : 0;
            out.catchment_id = in.catchment_id;
            out.lake_id = in.lake_id;
            out.river = in.river ? 1 : 0;
        }
    });
}

int ws_hydrology_copy_lakes(const ws_hydrology_result* result, ws_lake_info* out_lakes, uint64_t capacity) {
    return guarded([&] {
        if (!result || (!out_lakes && !result->impl.lakes.empty())) throw std::invalid_argument("result/out_lakes is null");
        if (capacity < result->impl.lakes.size()) throw std::invalid_argument("lake output capacity is too small");
        for (std::size_t i = 0; i < result->impl.lakes.size(); ++i) {
            const auto& in = result->impl.lakes[i];
            auto& out = out_lakes[i];
            out.id = in.id;
            out.outlet_x = in.outlet_coord.x;
            out.outlet_y = in.outlet_coord.y;
            out.outflow_x = in.outflow_coord.x;
            out.outflow_y = in.outflow_coord.y;
            out.has_outflow = in.has_outflow ? 1 : 0;
            out.cell_count = in.cell_count;
            out.area_m2 = in.area_m2;
            out.volume_m3 = in.volume_m3;
            out.surface_elevation_m = in.surface_elevation_m;
            out.max_depth_m = in.max_depth_m;
        }
    });
}

int ws_hydrology_copy_river_segments(const ws_hydrology_result* result, ws_river_segment* out_segments, uint64_t capacity) {
    return guarded([&] {
        if (!result || (!out_segments && !result->impl.river_segments.empty())) throw std::invalid_argument("result/out_segments is null");
        if (capacity < result->impl.river_segments.size()) throw std::invalid_argument("river segment output capacity is too small");
        for (std::size_t i = 0; i < result->impl.river_segments.size(); ++i) {
            const auto& in = result->impl.river_segments[i];
            auto& out = out_segments[i];
            out.from_x = in.from.x;
            out.from_y = in.from.y;
            out.to_x = in.to.x;
            out.to_y = in.to.y;
            out.discharge_m3_s = in.discharge_m3_s;
        }
    });
}

int ws_world_disturb_surface(ws_world* world, double min_x_m, double min_y_m,
                             double max_x_m, double max_y_m, float amount,
                             uint64_t* out_affected_cells) {
    return guarded([&] {
        if (!world) throw std::invalid_argument("world is null");
        const auto n = world->impl.disturb_surface({min_x_m, min_y_m}, {max_x_m, max_y_m}, amount);
        if (out_affected_cells) *out_affected_cells = static_cast<uint64_t>(n);
    });
}

int ws_world_save(ws_world* world, const char* utf8_path) {
    return guarded([&] {
        if (!world || !utf8_path) throw std::invalid_argument("world/path is null");
        world->impl.save(utf8_path);
    });
}

ws_world* ws_world_load(const char* utf8_path) {
    try {
        if (!utf8_path) throw std::invalid_argument("path is null");
        auto out = std::make_unique<ws_world>(worldsim::World::load(utf8_path));
        g_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "unknown WorldSim error";
        return nullptr;
    }
}

const char* ws_last_error(void) { return g_last_error.c_str(); }

} // extern "C"
