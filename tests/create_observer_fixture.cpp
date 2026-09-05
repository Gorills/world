#include "worldsim/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {
using namespace worldsim;

// Only fixed field names and finite arithmetic values are emitted; no JSON dependency
// or string escaping is needed for this integration fixture.
class JsonObject {
public:
    explicit JsonObject(std::ostream& out) : out_(out) { out_ << '{'; }

    template <typename T>
    void number(std::string_view key, T value) {
        if (!std::isfinite(static_cast<double>(value))) {
            throw std::runtime_error("fixture contains a non-finite number");
        }
        key_prefix(key);
        out_ << value;
    }

    void boolean(std::string_view key, bool value) {
        key_prefix(key);
        out_ << (value ? "true" : "false");
    }

    void identifier(std::string_view key, std::uint64_t value) {
        key_prefix(key);
        out_ << '\"' << value << '\"';
    }

    void key_prefix(std::string_view key) {
        if (!first_) out_ << ',';
        first_ = false;
        out_ << '\"' << key << "\":";
    }

    void finish() { out_ << '}'; }

private:
    std::ostream& out_;
    bool first_{true};
};

struct ClippedCell {
    WorldPosition min;
    WorldPosition max;

    [[nodiscard]] double area() const {
        return std::max(0.0, max.x_m - min.x_m) *
               std::max(0.0, max.y_m - min.y_m);
    }
    [[nodiscard]] WorldPosition center() const {
        return {(min.x_m + max.x_m) * 0.5, (min.y_m + max.y_m) * 0.5};
    }
};

ClippedCell clipped_cell(CellCoord coord, std::int32_t size, const WorldBounds& bounds) {
    const double x = static_cast<double>(coord.x) * size;
    const double y = static_cast<double>(coord.y) * size;
    return {{std::max(x, bounds.origin_x_m), std::max(y, bounds.origin_y_m)},
            {std::min(x + size, bounds.origin_x_m + bounds.width_m),
             std::min(y + size, bounds.origin_y_m + bounds.height_m)}};
}

struct WaterValues {
    double soil{};
    double surface{};
    double snow{};
    double groundwater{};
    double saturation{};
};

template <typename Cell>
WaterValues water_values(const Cell& cell, double capacity) {
    return {cell.soil_water_mm, cell.surface_water_mm, cell.snow_water_equivalent_mm,
            cell.groundwater_mm,
            std::clamp(cell.soil_water_mm / std::max(1.0e-12, capacity), 0.0, 1.0)};
}

WaterValues observe_water(const SimulationState& state, CellCoord climate,
                          std::optional<CellCoord> region) {
    const auto& world = state.world();
    const auto& water = state.water();
    const double reference_capacity = water.parameters().soil_capacity_mm;
    if (!water.is_refined(climate)) {
        return water_values(water.coarse_state().cell(climate), reference_capacity *
                            world.sample_climate_soil(climate).storage_capacity_scale);
    }
    const auto& tile = water.refined_tile(climate);
    if (region) {
        return water_values(tile.cell(*region), reference_capacity *
                            world.sample_soil(*region).storage_capacity_scale);
    }

    // L0 observations integrate the real child owners, including clipped edge areas.
    // Saturation is the area mean of child saturations, as consumed by the ecosystem.
    WaterValues result;
    double total_area = 0.0;
    for (const auto& child : tile.cells) {
        if (!child.active) continue;
        const double area = clipped_cell(child.coord, world.config().regional_cell_m,
                                         world.config().bounds).area();
        const auto value = water_values(child, reference_capacity *
            world.sample_soil(child.coord).storage_capacity_scale);
        result.soil += area * value.soil;
        result.surface += area * value.surface;
        result.snow += area * value.snow;
        result.groundwater += area * value.groundwater;
        result.saturation += area * value.saturation;
        total_area += area;
    }
    if (!(total_area > 0.0)) throw std::runtime_error("refined fixture has no active area");
    result.soil /= total_area;
    result.surface /= total_area;
    result.snow /= total_area;
    result.groundwater /= total_area;
    result.saturation /= total_area;
    return result;
}

void write_observation(std::ostream& out, const SimulationState& state, CellCoord climate,
                       std::optional<WorldPosition> point = std::nullopt) {
    const auto& world = state.world();
    const auto& cfg = world.config();
    const auto& terrain = state.topology().cell(climate);
    const auto position = point.value_or(clipped_cell(climate, cfg.climate_cell_m,
                                                      cfg.bounds).center());
    const auto region = world_to_cell(position, cfg.regional_cell_m);
    const auto weather = sample_weather(state.weather(), climate);
    const auto& ecosystem = state.ecosystem().cell(climate);
    const auto wet = observe_water(state, climate,
                                  point ? std::optional<CellCoord>(region) : std::nullopt);
    JsonObject object(out);
    object.number("cell_x", climate.x);
    object.number("cell_y", climate.y);
    object.number("x_m", position.x_m);
    object.number("y_m", position.y_m);
    object.number("elevation_m", point ? world.sample_elevation(position)
                                        : terrain.terrain_elevation_m);
    object.boolean("ocean", terrain.ocean);
    object.boolean("river", terrain.river);
    object.number("soil_water_mm", wet.soil);
    object.number("surface_water_mm", wet.surface);
    object.number("snow_water_mm", wet.snow);
    object.number("groundwater_mm", wet.groundwater);
    object.number("soil_saturation", wet.saturation);
    object.number("temperature_c", weather.mean_air_temperature_c);
    object.number("precipitation_mm", weather.precipitation_mm);
    object.number("channel_storage_m3", state.water().channel_storage_m3(climate));
    object.number("channel_discharge_m3_s", state.water().coarse_state().cell(climate).last_routed_discharge_m3_s);
    object.number("channel_residence_days", state.water().channel_transport(climate).residence_days);
    object.number("channel_reach_length_m", state.water().channel_transport(climate).reach_length_m);
    object.number("grass_carbon", ecosystem.grass_carbon);
    object.number("shrub_carbon", ecosystem.shrub_carbon);
    object.number("tree_carbon", ecosystem.tree_carbon);
    object.number("herbivore_carbon", ecosystem.herbivore_carbon);
    object.number("carnivore_carbon", ecosystem.carnivore_carbon);
    if (point) {
        object.number("regional_x", region.x);
        object.number("regional_y", region.y);
        object.number("weather_resolution_m", cfg.climate_cell_m);
        object.boolean("water_refined", state.water().is_refined(climate));
        const auto* patch = world.find_local_patch(region);
        object.boolean("local_materialized", patch != nullptr);
        if (patch) {
            const auto local = world_to_cell(position, cfg.local_cell_m);
            const auto ratio = cfg.regional_cell_m / cfg.local_cell_m;
            const auto local_x = local.x - region.x * ratio;
            const auto local_y = local.y - region.y * ratio;
            const auto index = static_cast<std::size_t>(local_y * ratio + local_x);
            const auto& cell = patch->cells.at(index);
            object.number("local_vegetation_biomass", cell.vegetation_biomass);
            object.number("local_disturbance", cell.disturbance);
        }
    }
    object.finish();
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: worldsim_observer_fixture OUTPUT_CHECKPOINT\n";
        return 2;
    }
    try {
        WorldConfig cfg;
        cfg.seed = 42;
        cfg.bounds = {-16'384.0, -8'192.0, 25'000.0, 19'000.0};
        SimulationState state(cfg);

        // A partial northern-edge L0 parent with negative x exercises clipping and
        // floor division together. Selecting through topology avoids guessing land.
        const auto parent_it = std::find_if(state.topology().cells.rbegin(),
            state.topology().cells.rend(), [&](const ContinentalHydrologyCell& cell) {
                const auto area = clipped_cell(cell.coord, cfg.climate_cell_m, cfg.bounds).area();
                return !cell.ocean && cell.coord.x < 0 && area > 0.0 &&
                       area < static_cast<double>(cfg.climate_cell_m) * cfg.climate_cell_m;
            });
        if (parent_it == state.topology().cells.rend()) {
            throw std::runtime_error("fixture requires a partial terrestrial L0 parent");
        }
        const CellCoord parent = parent_it->coord;
        const auto& tile = state.materialize_refined_water_tile(parent);
        const auto child_it = std::find_if(tile.cells.rbegin(), tile.cells.rend(),
            [](const DynamicHydrologyCellState& child) { return child.active; });
        if (child_it == tile.cells.rend()) throw std::runtime_error("fixture has no active child");
        const CellCoord region = child_it->coord;
        const WorldPosition point = clipped_cell(region, cfg.regional_cell_m, cfg.bounds).center();
        const auto local = world_to_cell(point, cfg.local_cell_m);
        const auto local_bounds = clipped_cell(local, cfg.local_cell_m, cfg.bounds);
        if (state.disturb_surface(local_bounds.min, local_bounds.max, 0.75f) != 1) {
            throw std::runtime_error("fixture disturbance must affect exactly one L2 cell");
        }
        (void)state.found_settlement(region, 123.0);
        SimulationDayReport last_report;
        for (int day = 0; day < 3; ++day) last_report = state.advance_day_full();
        if (state.world().materialized_patch_count() != 1 || state.water().refined_tile_count() != 1 ||
            state.water().coarse_state().cell(parent).soil_water_mm != 0.0f ||
            state.water().refined_tile(parent).cell(region).soil_water_mm <= 0.0f) {
            throw std::runtime_error("fixture does not distinguish refined ownership from coarse zeros");
        }

        const std::filesystem::path checkpoint(argv[1]);
        state.save_checkpoint(checkpoint);
        auto json_path = checkpoint;
        json_path += ".json";
        std::ofstream out(json_path, std::ios::trunc);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10);
        JsonObject root(out);
        root.number("day", state.simulated_day());
        root.number("water_storage_m3", last_report.environment.water.storage_after_m3);
        root.number("terminal_outflow_m3_s", last_report.environment.water.terminal_outflow_m3 / 86400.0);
        root.key_prefix("bounds");
        JsonObject bounds(out);
        bounds.number("origin_x_m", cfg.bounds.origin_x_m);
        bounds.number("origin_y_m", cfg.bounds.origin_y_m);
        bounds.number("width_m", cfg.bounds.width_m);
        bounds.number("height_m", cfg.bounds.height_m);
        bounds.finish();
        root.key_prefix("counts");
        JsonObject counts(out);
        counts.number("local_patches", state.world().materialized_patch_count());
        counts.number("refined_tiles", state.water().refined_tile_count());
        counts.number("settlements", state.settlements().size());
        counts.finish();
        root.key_prefix("point");
        write_observation(out, state, parent, point);
        root.key_prefix("frames");
        out << '[';
        bool first = true;
        for (const auto& cell : state.topology().cells) {
            if (!first) out << ',';
            first = false;
            write_observation(out, state, cell.coord);
        }
        out << ']';
        root.key_prefix("settlements");
        out << '[';
        first = true;
        for (const auto& value : state.settlements()) {
            if (!first) out << ',';
            first = false;
            JsonObject settlement(out);
            settlement.identifier("id", value.id);
            settlement.number("regional_x", value.regional_coord.x);
            settlement.number("regional_y", value.regional_coord.y);
            settlement.number("population", value.population);
            settlement.number("founded_day", value.founded_day);
            settlement.finish();
        }
        out << ']';
        root.finish();
        out << '\n';
        out.close();
        // A valid core checkpoint beyond the observer's allocation budget must be
        // rejected by the adapter without replacing its current generation.
        WorldConfig large_config;
        large_config.seed = 42;
        large_config.bounds.width_m = 65.0 * large_config.climate_cell_m;
        large_config.bounds.height_m = 65.0 * large_config.climate_cell_m;
        SimulationState large_world(large_config);
        auto oversized_path = checkpoint;
        oversized_path += ".oversized.wsc";
        large_world.save_checkpoint(oversized_path);
        std::cout << "Observer fixture saved: " << checkpoint << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Observer fixture failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
