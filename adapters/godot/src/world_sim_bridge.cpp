#include "world_sim_bridge.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

// A Dictionary per L0 cell is suitable for the desktop observer's bounded
// snapshots. Larger simulations remain supported by the independent core/CLI.
constexpr std::size_t kMaxObserverCells = 4096;

void require_observable_world(const worldsim::SimulationState& simulation) {
    if (simulation.topology().cells.size() > kMaxObserverCells) {
        throw std::length_error(
            "Observer supports at most 4096 L0 cells; the core CLI supports larger worlds");
    }
}

// All bound operations that can invoke C++ code share one exception boundary.
// Neither core exceptions nor partially constructed snapshots reach GDScript.
template <typename Result, typename Function>
Result guarded(godot::String& error, Function&& function) {
    try {
        auto result = std::forward<Function>(function)();
        error = godot::String();
        return result;
    } catch (const std::exception& exception) {
        error = godot::String::utf8(exception.what());
    } catch (...) {
        error = "Unknown native simulation error";
    }
    return Result{};
}

std::filesystem::path checkpoint_path(const godot::String& value) {
    const auto utf8 = value.utf8();
    const std::string bytes(utf8.get_data(), static_cast<std::size_t>(utf8.length()));
    if (bytes.empty() || bytes.find('\0') != std::string::npos) {
        throw std::invalid_argument("Checkpoint path must be nonempty and contain no NUL");
    }
    const std::filesystem::path path(std::u8string(bytes.begin(), bytes.end()));
    if (!path.is_absolute()) {
        throw std::invalid_argument("Checkpoint path must be absolute; use ProjectSettings.globalize_path");
    }
    return path;
}

void add_bounds(godot::Dictionary& result, const worldsim::WorldConfig& config) {
    result["origin_x_m"] = config.bounds.origin_x_m;
    result["origin_y_m"] = config.bounds.origin_y_m;
    result["width_m"] = config.bounds.width_m;
    result["height_m"] = config.bounds.height_m;
    result["sea_level_m"] = config.sea_level_m;
}

double overlap_area(worldsim::CellCoord coord, std::int32_t cell_size_m,
                    const worldsim::WorldBounds& bounds) {
    const double size = static_cast<double>(cell_size_m);
    const double x = static_cast<double>(coord.x) * size;
    const double y = static_cast<double>(coord.y) * size;
    const double width = std::max(0.0, std::min(x + size, bounds.origin_x_m + bounds.width_m) -
                                      std::max(x, bounds.origin_x_m));
    const double height = std::max(0.0, std::min(y + size, bounds.origin_y_m + bounds.height_m) -
                                       std::max(y, bounds.origin_y_m));
    return width * height;
}

worldsim::WorldPosition clipped_center(worldsim::CellCoord coord, std::int32_t cell_size_m,
                                       const worldsim::WorldBounds& bounds) {
    const double size = static_cast<double>(cell_size_m);
    const double x = static_cast<double>(coord.x) * size;
    const double y = static_cast<double>(coord.y) * size;
    return {
        std::min(std::max(x, bounds.origin_x_m) +
                     (std::min(x + size, bounds.origin_x_m + bounds.width_m) -
                      std::max(x, bounds.origin_x_m)) * 0.5,
                 std::nextafter(bounds.origin_x_m + bounds.width_m, bounds.origin_x_m)),
        std::min(std::max(y, bounds.origin_y_m) +
                     (std::min(y + size, bounds.origin_y_m + bounds.height_m) -
                      std::max(y, bounds.origin_y_m)) * 0.5,
                 std::nextafter(bounds.origin_y_m + bounds.height_m, bounds.origin_y_m))};
}

struct ObservedWater {
    double soil{};
    double surface{};
    double snow{};
    double groundwater{};
    double saturation{};
};

ObservedWater observe_water(const worldsim::SimulationState& simulation,
                            worldsim::CellCoord coord) {
    const auto& world = simulation.world();
    const auto& water = simulation.water();
    ObservedWater result;
    if (water.is_refined(coord)) {
        double total_area = 0.0;
        for (const auto& child : water.refined_tile(coord).cells) {
            if (!child.active) continue;
            const double area = overlap_area(child.coord, world.config().regional_cell_m,
                                             world.config().bounds);
            if (area <= 0.0) continue;
            const double capacity = static_cast<double>(water.parameters().soil_capacity_mm) *
                world.sample_soil(child.coord).storage_capacity_scale;
            result.soil += area * child.soil_water_mm;
            result.surface += area * child.surface_water_mm;
            result.snow += area * child.snow_water_equivalent_mm;
            result.groundwater += area * child.groundwater_mm;
            result.saturation += area * std::clamp(child.soil_water_mm / std::max(1.0e-12, capacity),
                                                   0.0, 1.0);
            total_area += area;
        }
        if (total_area > 0.0) {
            result.soil /= total_area;
            result.surface /= total_area;
            result.snow /= total_area;
            result.groundwater /= total_area;
            result.saturation /= total_area;
        }
    } else {
        const auto& coarse = water.coarse_state().cell(coord);
        const double capacity = static_cast<double>(water.parameters().soil_capacity_mm) *
            world.sample_climate_soil(coord).storage_capacity_scale;
        result = {coarse.soil_water_mm, coarse.surface_water_mm, coarse.snow_water_equivalent_mm,
                  coarse.groundwater_mm,
                  std::clamp(coarse.soil_water_mm / std::max(1.0e-12, capacity), 0.0, 1.0)};
    }
    return result;
}

void add_water(godot::Dictionary& result, const ObservedWater& water) {
    result["soil_water_mm"] = water.soil;
    result["surface_water_mm"] = water.surface;
    result["snow_water_mm"] = water.snow;
    result["groundwater_mm"] = water.groundwater;
    result["soil_saturation"] = water.saturation;
}

godot::Dictionary observe_cell(const worldsim::SimulationState& simulation,
                               const worldsim::ContinentalHydrologyCell& topology,
                               const worldsim::ContinentalWaterForcing& weather) {
    const auto& config = simulation.world().config();
    const auto coord = topology.coord;
    const auto position = clipped_center(coord, config.climate_cell_m, config.bounds);
    const auto& ecosystem = simulation.ecosystem().cell(coord);
    godot::Dictionary result;
    result["cell_x"] = coord.x;
    result["cell_y"] = coord.y;
    result["x_m"] = position.x_m;
    result["y_m"] = position.y_m;
    result["elevation_m"] = topology.terrain_elevation_m;
    result["ocean"] = topology.ocean;
    result["river"] = topology.river;
    result["has_downstream"] = topology.has_downstream;
    const auto downstream = topology.has_downstream
        ? clipped_center(topology.downstream_coord, config.climate_cell_m, config.bounds)
        : position;
    result["downstream_x_m"] = downstream.x_m;
    result["downstream_y_m"] = downstream.y_m;
    result["temperature_c"] = weather.mean_air_temperature_c;
    result["precipitation_mm"] = weather.precipitation_mm;
    add_water(result, observe_water(simulation, coord));
    result["water_refined"] = simulation.water().is_refined(coord);
    result["channel_storage_m3"] = simulation.water().channel_storage_m3(coord);
    // The persisted diagnostic is the mean release over the last completed day,
    // including for refined parents whose channel authority remains at L0.
    result["channel_discharge_m3_s"] =
        simulation.water().coarse_state().cell(coord).last_routed_discharge_m3_s;
    const auto& transport = simulation.water().channel_transport(coord);
    result["channel_residence_days"] = transport.residence_days;
    result["channel_reach_length_m"] = transport.reach_length_m;
    result["grass_carbon"] = ecosystem.grass_carbon;
    result["shrub_carbon"] = ecosystem.shrub_carbon;
    result["tree_carbon"] = ecosystem.tree_carbon;
    result["herbivore_carbon"] = ecosystem.herbivore_carbon;
    result["carnivore_carbon"] = ecosystem.carnivore_carbon;
    return result;
}

} // namespace

void WorldSimBridge::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;
    ClassDB::bind_method(D_METHOD("create_world", "seed"), &WorldSimBridge::create_world, DEFVAL(42));
    ClassDB::bind_method(D_METHOD("advance_day"), &WorldSimBridge::advance_day);
    ClassDB::bind_method(D_METHOD("save_world", "absolute_path"), &WorldSimBridge::save_world);
    ClassDB::bind_method(D_METHOD("load_world", "absolute_path"), &WorldSimBridge::load_world);
    ClassDB::bind_method(D_METHOD("is_ready"), &WorldSimBridge::is_ready);
    ClassDB::bind_method(D_METHOD("get_last_error"), &WorldSimBridge::get_last_error);
    ClassDB::bind_method(D_METHOD("get_terrain", "resolution"), &WorldSimBridge::get_terrain, DEFVAL(128));
    ClassDB::bind_method(D_METHOD("get_frame"), &WorldSimBridge::get_frame);
    ClassDB::bind_method(D_METHOD("sample_point", "x_m", "y_m"), &WorldSimBridge::sample_point);
}

const worldsim::SimulationState& WorldSimBridge::require_state() const {
    if (!simulation_) throw std::logic_error("Create or load a world first");
    return *simulation_;
}

bool WorldSimBridge::is_ready() const noexcept {
    return static_cast<bool>(simulation_);
}

godot::String WorldSimBridge::get_last_error() const noexcept {
    return last_error_;
}

bool WorldSimBridge::create_world(std::int64_t seed) {
    return guarded<bool>(last_error_, [&] {
        if (seed < 0) throw std::invalid_argument("Seed must be non-negative");
        worldsim::WorldConfig config;
        config.seed = static_cast<worldsim::Seed>(seed);
        auto next = std::make_unique<worldsim::SimulationState>(config);
        require_observable_world(*next);
        simulation_.swap(next);
        return true;
    });
}

bool WorldSimBridge::advance_day() {
    return guarded<bool>(last_error_, [&] {
        (void)require_state();
        (void)simulation_->advance_day_full();
        return true;
    });
}

bool WorldSimBridge::save_world(const godot::String& absolute_path) {
    return guarded<bool>(last_error_, [&] {
        require_state().save_checkpoint(checkpoint_path(absolute_path));
        return true;
    });
}

bool WorldSimBridge::load_world(const godot::String& absolute_path) {
    return guarded<bool>(last_error_, [&] {
        auto next = std::make_unique<worldsim::SimulationState>(
            worldsim::SimulationState::load_checkpoint(checkpoint_path(absolute_path)));
        require_observable_world(*next);
        simulation_.swap(next);
        return true;
    });
}

godot::Dictionary WorldSimBridge::get_terrain(std::int64_t resolution) {
    return guarded<godot::Dictionary>(last_error_, [&] {
        const auto& world = require_state().world();
        if (resolution < 1 || resolution > 256) {
            throw std::invalid_argument("Terrain resolution must be between 1 and 256");
        }
        const auto& bounds = world.config().bounds;
        const auto side = resolution + 1;
        godot::PackedFloat32Array heights;
        if (heights.resize(side * side) != godot::OK) {
            throw std::runtime_error("Cannot allocate terrain snapshot");
        }
        const double max_x = std::nextafter(bounds.origin_x_m + bounds.width_m, bounds.origin_x_m);
        const double max_y = std::nextafter(bounds.origin_y_m + bounds.height_m, bounds.origin_y_m);
        auto* output = heights.ptrw();
        for (std::int64_t y = 0; y <= resolution; ++y) {
            for (std::int64_t x = 0; x <= resolution; ++x) {
                const worldsim::WorldPosition point{
                    std::min(max_x, bounds.origin_x_m + bounds.width_m *
                                     (static_cast<double>(x) / static_cast<double>(resolution))),
                    std::min(max_y, bounds.origin_y_m + bounds.height_m *
                                     (static_cast<double>(y) / static_cast<double>(resolution)))};
                output[y * side + x] = world.sample_elevation(point);
            }
        }
        godot::Dictionary result;
        add_bounds(result, world.config());
        result["resolution"] = resolution;
        result["heights"] = heights;
        return result;
    });
}

godot::Dictionary WorldSimBridge::get_frame() {
    return guarded<godot::Dictionary>(last_error_, [&] {
        const auto& simulation = require_state();
        require_observable_world(simulation);
        const auto& world = simulation.world();
        const auto& topology = simulation.topology();
        // The batch API validates weather once; per-cell sample_weather would
        // repeatedly validate the entire world and make a frame quadratic.
        const auto weather = worldsim::make_weather_daily_forcing(simulation.weather());
        godot::Array cells;
        if (cells.resize(static_cast<std::int64_t>(topology.cells.size())) != godot::OK) {
            throw std::runtime_error("Cannot allocate world snapshot");
        }
        double land_area = 0.0;
        double soil_volume = 0.0;
        double surface_volume = 0.0;
        double snow_volume = 0.0;
        double groundwater_volume = 0.0;
        double terminal_discharge = 0.0;
        double max_discharge = 0.0;
        for (std::size_t i = 0; i < topology.cells.size(); ++i) {
            const auto& cell = topology.cells[i];
            auto value = observe_cell(simulation, cell, weather.at(i));
            cells[static_cast<std::int64_t>(i)] = value;
            if (cell.ocean) continue;
            const double area = overlap_area(cell.coord, world.config().climate_cell_m,
                                              world.config().bounds);
            land_area += area;
            const double volume_per_mm = area / 1000.0;
            soil_volume += static_cast<double>(value["soil_water_mm"]) * volume_per_mm;
            surface_volume += static_cast<double>(value["surface_water_mm"]) * volume_per_mm;
            snow_volume += static_cast<double>(value["snow_water_mm"]) * volume_per_mm;
            groundwater_volume += static_cast<double>(value["groundwater_mm"]) * volume_per_mm;
            const double discharge = static_cast<double>(value["channel_discharge_m3_s"]);
            max_discharge = std::max(max_discharge, discharge);
            // Sum only exits: summing all reaches would count the same water repeatedly.
            if (!cell.has_downstream || topology.cell(cell.downstream_coord).ocean) {
                terminal_discharge += discharge;
            }
        }
        godot::Array settlements;
        for (const auto& settlement : simulation.settlements()) {
            const auto position = clipped_center(settlement.regional_coord,
                world.config().regional_cell_m, world.config().bounds);
            godot::Dictionary value;
            value["id"] = godot::String(std::to_string(settlement.id).c_str());
            value["regional_x"] = settlement.regional_coord.x;
            value["regional_y"] = settlement.regional_coord.y;
            value["x_m"] = position.x_m;
            value["y_m"] = position.y_m;
            value["elevation_m"] = world.sample_elevation(position);
            value["population"] = settlement.population;
            value["founded_day"] = settlement.founded_day;
            settlements.push_back(value);
        }
        const auto ecosystem = simulation.ecosystem().totals();
        godot::Dictionary totals;
        totals["plant_carbon_kg"] = ecosystem.plant_carbon_kg;
        totals["herbivore_carbon_kg"] = ecosystem.herbivore_carbon_kg;
        totals["carnivore_carbon_kg"] = ecosystem.carnivore_carbon_kg;
        totals["channel_storage_m3"] = simulation.water().total_channel_storage_m3();
        totals["land_area_m2"] = land_area;
        totals["soil_water_m3"] = soil_volume;
        totals["surface_water_m3"] = surface_volume;
        totals["snow_water_m3"] = snow_volume;
        totals["groundwater_m3"] = groundwater_volume;
        totals["terminal_outflow_m3_s"] = terminal_discharge;
        totals["max_channel_discharge_m3_s"] = max_discharge;
        godot::Dictionary result;
        add_bounds(result, world.config());
        result["day"] = simulation.simulated_day();
        result["seed"] = godot::String(std::to_string(world.config().seed).c_str());
        result["min_cell_x"] = topology.min_coord.x;
        result["min_cell_y"] = topology.min_coord.y;
        result["grid_width"] = static_cast<std::int64_t>(topology.width_cells);
        result["grid_height"] = static_cast<std::int64_t>(topology.height_cells);
        result["cells"] = cells;
        result["settlements"] = settlements;
        result["totals"] = totals;
        result["materialized_patches"] = static_cast<std::int64_t>(world.materialized_patch_count());
        result["refined_tiles"] = static_cast<std::int64_t>(simulation.water().refined_tile_count());
        return result;
    });
}

godot::Dictionary WorldSimBridge::sample_point(double x_m, double y_m) {
    return guarded<godot::Dictionary>(last_error_, [&] {
        const auto& simulation = require_state();
        const auto& world = simulation.world();
        const auto& config = world.config();
        const worldsim::WorldPosition point{x_m, y_m};
        if (!std::isfinite(x_m) || !std::isfinite(y_m) || !config.bounds.contains(point)) {
            throw std::out_of_range("Sample position must be finite and inside the world bounds");
        }
        const auto climate = worldsim::world_to_cell(point, config.climate_cell_m);
        const auto regional = worldsim::world_to_cell(point, config.regional_cell_m);
        const auto weather = worldsim::sample_weather(simulation.weather(), climate);
        auto result = observe_cell(simulation, simulation.topology().cell(climate),
            {weather.precipitation_mm, weather.mean_air_temperature_c, weather.potential_evapotranspiration_mm});
        result["x_m"] = x_m;
        result["y_m"] = y_m;
        result["elevation_m"] = world.sample_elevation(point);
        result["regional_x"] = regional.x;
        result["regional_y"] = regional.y;
        result["weather_resolution_m"] = config.climate_cell_m;
        result["ecosystem_resolution_m"] = config.climate_cell_m;
        const bool refined = simulation.water().is_refined(climate);
        result["water_resolution_m"] = refined ? config.regional_cell_m : config.climate_cell_m;
        if (refined) {
            const auto& child = simulation.water().refined_tile(climate).cell(regional);
            const double capacity = static_cast<double>(simulation.water().parameters().soil_capacity_mm) *
                world.sample_soil(regional).storage_capacity_scale;
            add_water(result, {child.soil_water_mm, child.surface_water_mm,
                child.snow_water_equivalent_mm, child.groundwater_mm,
                std::clamp(child.soil_water_mm / std::max(1.0e-12, capacity), 0.0, 1.0)});
        }
        const auto* patch = world.find_local_patch(regional);
        result["local_materialized"] = patch != nullptr;
        if (patch) {
            const auto local = worldsim::world_to_cell(point, config.local_cell_m);
            constexpr auto side = static_cast<std::int64_t>(worldsim::kLocalCellsPerAxis);
            const auto x = local.x - regional.x * side;
            const auto y = local.y - regional.y * side;
            const auto& cell = patch->cells.at(static_cast<std::size_t>(y * side + x));
            result["local_vegetation_biomass"] = cell.vegetation_biomass;
            result["local_disturbance"] = cell.disturbance;
        }
        return result;
    });
}
