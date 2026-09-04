#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(double a, double b, double abs_eps, double rel_eps = 1e-8) {
    return std::abs(a - b) <=
        std::max(abs_eps, std::max(std::abs(a), std::abs(b)) * rel_eps);
}

worldsim::WorldConfig config() {
    worldsim::WorldConfig cfg;
    cfg.seed = 13013;
    cfg.bounds = {-12'345.0, -9'876.0, 38'765.0, 31'234.0};
    cfg.sea_level_m = -10'000.0f;
    return cfg;
}

double overlap_area_m2(
    worldsim::CellCoord coord,
    std::int32_t cell_m,
    const worldsim::WorldBounds& bounds) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, bounds.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, bounds.origin_y_m);
    const double x1 = std::min(
        (static_cast<double>(coord.x) + 1.0) * s,
        bounds.origin_x_m + bounds.width_m);
    const double y1 = std::min(
        (static_cast<double>(coord.y) + 1.0) * s,
        bounds.origin_y_m + bounds.height_m);
    if (!(x1 > x0) || !(y1 > y0)) return 0.0;
    return (x1 - x0) * (y1 - y0);
}

worldsim::WorldPosition overlap_center(
    worldsim::CellCoord coord,
    std::int32_t cell_m,
    const worldsim::WorldBounds& bounds) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, bounds.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, bounds.origin_y_m);
    const double x1 = std::min(
        (static_cast<double>(coord.x) + 1.0) * s,
        bounds.origin_x_m + bounds.width_m);
    const double y1 = std::min(
        (static_cast<double>(coord.y) + 1.0) * s,
        bounds.origin_y_m + bounds.height_m);
    if (!(x1 > x0) || !(y1 > y0)) {
        throw std::runtime_error("test cell has zero world overlap");
    }
    return {(x0 + x1) * 0.5, (y0 + y1) * 0.5};
}

worldsim::CellCoord find_partial_land_parent(
    const worldsim::ContinentalHydrologyResult& topology,
    const worldsim::WorldConfig& cfg) {
    const double full =
        static_cast<double>(cfg.climate_cell_m) * cfg.climate_cell_m;
    for (const auto& cell : topology.cells) {
        const double area =
            overlap_area_m2(cell.coord, cfg.climate_cell_m, cfg.bounds);
        if (!cell.ocean && area > 0.0 && area < full) return cell.coord;
    }
    throw std::runtime_error("fixture has no partial terrestrial parent");
}

bool same_forcing(
    const worldsim::HydrometeorologicalForcing& a,
    const worldsim::HydrometeorologicalForcing& b) {
    return a.coord == b.coord &&
        a.precipitation_mm == b.precipitation_mm &&
        a.mean_air_temperature_c == b.mean_air_temperature_c &&
        a.potential_evapotranspiration_mm == b.potential_evapotranspiration_mm;
}

bool same_refined_cell(
    const worldsim::DynamicHydrologyCellState& a,
    const worldsim::DynamicHydrologyCellState& b) {
    return a.coord == b.coord && a.active == b.active &&
        a.snow_water_equivalent_mm == b.snow_water_equivalent_mm &&
        a.surface_water_mm == b.surface_water_mm &&
        a.soil_water_mm == b.soil_water_mm &&
        a.groundwater_mm == b.groundwater_mm &&
        a.last_evapotranspiration_mm == b.last_evapotranspiration_mm &&
        a.last_quick_runoff_mm == b.last_quick_runoff_mm &&
        a.last_baseflow_mm == b.last_baseflow_mm &&
        a.last_routed_discharge_m3_s == b.last_routed_discharge_m3_s;
}

std::uint32_t read_version(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read refined forcing persistence fixture");
    in.seekg(8);
    std::uint32_t version{};
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!in) throw std::runtime_error("cannot read refined forcing persistence version");
    return version;
}

void rewrite_version(const std::filesystem::path& path, std::uint32_t version) {
    std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!io) throw std::runtime_error("cannot rewrite refined forcing persistence fixture");
    io.seekp(8);
    io.write(reinterpret_cast<const char*>(&version), sizeof(version));
    if (!io) throw std::runtime_error("cannot rewrite refined forcing persistence version");
}
} // namespace

int main() {
    using namespace worldsim;

    World world(config());
    const auto topology = world.analyze_continental_hydrology({0.1f});
    const auto parent = find_partial_land_parent(topology, world.config());

    auto state = make_multiresolution_water_state(world, topology);
    (void)materialize_refined_water_tile(world, topology, state, parent);
    check(world.materialized_patch_count() == 0,
          "refined forcing setup does not materialize L2 state");

    const ContinentalWaterForcing parent_forcing{20.0f, 10.0f, 1.5f};
    const auto forcing =
        derive_refined_atmospheric_forcing(world, state, parent, parent_forcing);
    const auto& refined = state.refined_tile(parent);
    check(forcing.size() == refined.cells.size() && forcing.size() == 64,
          "derived forcing has one aligned record per refined child");

    const double parent_area = overlap_area_m2(
        parent, world.config().climate_cell_m, world.config().bounds);
    double active_area = 0.0;
    double precipitation_m3 = 0.0;
    double min_precip = std::numeric_limits<double>::infinity();
    double max_precip = -std::numeric_limits<double>::infinity();
    double min_elevation = std::numeric_limits<double>::infinity();
    double max_elevation = -std::numeric_limits<double>::infinity();
    double temperature_at_min_elevation = 0.0;
    double temperature_at_max_elevation = 0.0;
    std::size_t active_count = 0;

    const double parent_elevation = std::max(
        0.0,
        static_cast<double>(world.sample_elevation(overlap_center(
            parent, world.config().climate_cell_m, world.config().bounds))));

    for (std::size_t i = 0; i < forcing.size(); ++i) {
        const auto& f = forcing[i];
        const auto& cell = refined.cells[i];
        check(f.coord == cell.coord, "derived forcing coordinate matches refined cell");
        if (!cell.active) {
            check(f.precipitation_mm == 0.0f &&
                  f.mean_air_temperature_c == 0.0f &&
                  f.potential_evapotranspiration_mm == 0.0f,
                  "inactive refined child receives zero atmospheric forcing");
            continue;
        }

        const double area = overlap_area_m2(
            cell.coord, world.config().regional_cell_m, world.config().bounds);
        if (!(area > 0.0)) continue;
        ++active_count;
        active_area += area;
        precipitation_m3 +=
            static_cast<double>(f.precipitation_mm) * 0.001 * area;
        check(std::isfinite(f.precipitation_mm) && f.precipitation_mm >= 0.0f &&
              std::isfinite(f.mean_air_temperature_c) &&
              std::isfinite(f.potential_evapotranspiration_mm) &&
              f.potential_evapotranspiration_mm >= 0.0f,
              "active refined forcing remains finite and hydrology-safe");

        const double elevation =
            static_cast<double>(world.sample_region(cell.coord).elevation_m);
        const double expected_correction = std::clamp(
            -0.0065 * (std::max(0.0, elevation) - parent_elevation),
            -8.0,
            8.0);
        const double expected_temperature =
            static_cast<double>(parent_forcing.mean_air_temperature_c) +
            expected_correction;
        const double expected_pet =
            std::max(0.0, 0.10 * (expected_temperature + 5.0));
        check(near(f.mean_air_temperature_c, expected_temperature, 2e-5) &&
              near(f.potential_evapotranspiration_mm, expected_pet, 2e-5),
              "L1 temperature lapse and PET proxy match the bounded documented transform");

        min_precip = std::min(min_precip, static_cast<double>(f.precipitation_mm));
        max_precip = std::max(max_precip, static_cast<double>(f.precipitation_mm));
        if (elevation < min_elevation) {
            min_elevation = elevation;
            temperature_at_min_elevation = f.mean_air_temperature_c;
        }
        if (elevation > max_elevation) {
            max_elevation = elevation;
            temperature_at_max_elevation = f.mean_air_temperature_c;
        }
    }

    const double target_precipitation_m3 =
        static_cast<double>(parent_forcing.precipitation_mm) * 0.001 * parent_area;
    check(active_count > 1 && near(active_area, parent_area, 1e-6),
          "partial-parent active L1 overlap area exactly covers the terrestrial L0 parent");
    check(near(precipitation_m3, target_precipitation_m3, 0.05, 2e-8),
          "L1 precipitation redistribution conserves parent precipitation volume to float precision");
    check(max_precip > min_precip,
          "terrain redistribution produces non-uniform precipitation across refined children");
    check(max_elevation > min_elevation &&
          temperature_at_max_elevation < temperature_at_min_elevation,
          "higher refined terrain receives lower derived air temperature");
    check(world.materialized_patch_count() == 0,
          "derived forcing query remains stateless and non-materializing");

    const auto day_before_invalid = state.simulated_day();
    auto invalid = parent_forcing;
    invalid.precipitation_mm = std::numeric_limits<float>::quiet_NaN();
    bool invalid_rejected = false;
    try {
        (void)derive_refined_atmospheric_forcing(world, state, parent, invalid);
    } catch (const std::invalid_argument&) {
        invalid_rejected = true;
    }
    check(invalid_rejected && state.simulated_day() == day_before_invalid,
          "invalid parent forcing is rejected without mutating refined water or clocks");

    bool unrefined_rejected = false;
    for (const auto& cell : topology.cells) {
        if (!cell.ocean && cell.coord != parent && !state.is_refined(cell.coord)) {
            try {
                (void)derive_refined_atmospheric_forcing(
                    world, state, cell.coord, parent_forcing);
            } catch (const std::invalid_argument&) {
                unrefined_rejected = true;
            }
            break;
        }
    }
    check(unrefined_rejected,
          "derived forcing query requires existing refined ownership");

    auto integration = make_multiresolution_water_state(world, topology);
    (void)materialize_refined_water_tile(world, topology, integration, parent);
    std::vector<ContinentalWaterForcing> l0_forcing(topology.cells.size());
    l0_forcing[integration.coarse_state().index_of(parent)] = parent_forcing;
    const auto report =
        advance_multiresolution_water_day(world, integration, l0_forcing);
    check(near(report.precipitation_m3, target_precipitation_m3, 0.05, 2e-8),
          "multiresolution scheduler consumes the conservative L1 forcing transform");
    check(integration.simulated_day() == 1 &&
          integration.refined_tile(parent).simulated_day == 1,
          "derived refined forcing preserves the exact global/refined day contract");

    const auto root =
        std::filesystem::temp_directory_path() / "worldsim_refined_atmospheric_forcing";
    const auto v6_path = root.string() + ".v6.wsmw";
    const auto v5_path = root.string() + ".v5.wsmw";
    save_multiresolution_water_state(integration, v6_path);
    check(read_version(v6_path) == 6u,
          "refined forcing semantics write multiresolution persistence v6");
    std::filesystem::copy_file(
        v6_path, v5_path, std::filesystem::copy_options::overwrite_existing);
    rewrite_version(v5_path, 5u);

    auto loaded_v6 = load_multiresolution_water_state(world, topology, v6_path);
    auto migrated_v5 = load_multiresolution_water_state(world, topology, v5_path);
    const auto forcing_v6 =
        derive_refined_atmospheric_forcing(world, loaded_v6, parent, parent_forcing);
    const auto forcing_v5 =
        derive_refined_atmospheric_forcing(world, migrated_v5, parent, parent_forcing);
    bool migration_forcing_equal = forcing_v6.size() == forcing_v5.size();
    for (std::size_t i = 0; migration_forcing_equal && i < forcing_v6.size(); ++i) {
        migration_forcing_equal = same_forcing(forcing_v6[i], forcing_v5[i]);
    }
    check(migration_forcing_equal,
          "v5 migration preserves water while deriving identical current v6 L1 forcing");

    std::vector<ContinentalWaterForcing> future(topology.cells.size());
    future[loaded_v6.coarse_state().index_of(parent)] = {7.0f, 3.0f, 0.8f};
    const auto next_v6 =
        advance_multiresolution_water_day(world, loaded_v6, future);
    const auto next_v5 =
        advance_multiresolution_water_day(world, migrated_v5, future);
    bool exact_future =
        next_v6.precipitation_m3 == next_v5.precipitation_m3 &&
        next_v6.evapotranspiration_m3 == next_v5.evapotranspiration_m3 &&
        next_v6.terminal_outflow_m3 == next_v5.terminal_outflow_m3 &&
        next_v6.storage_after_m3 == next_v5.storage_after_m3 &&
        next_v6.water_balance_error_m3 == next_v5.water_balance_error_m3;
    const auto& tile_v6 = loaded_v6.refined_tile(parent);
    const auto& tile_v5 = migrated_v5.refined_tile(parent);
    for (std::size_t i = 0; exact_future && i < tile_v6.cells.size(); ++i) {
        exact_future = same_refined_cell(tile_v6.cells[i], tile_v5.cells[i]);
    }
    check(exact_future,
          "v5 migrated refined checkpoint follows exact deterministic v6 future evolution");

    std::filesystem::remove(v6_path);
    std::filesystem::remove(v5_path);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All refined atmospheric forcing tests passed\n";
    return 0;
}
