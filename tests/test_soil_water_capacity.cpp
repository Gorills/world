#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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

bool near(double a, double b, double abs_eps = 1e-5, double rel_eps = 2e-6) {
    return std::abs(a - b) <= std::max(abs_eps, std::max(std::abs(a), std::abs(b)) * rel_eps);
}

worldsim::WorldConfig partial_land_config() {
    worldsim::WorldConfig cfg;
    cfg.seed = 1009;
    cfg.bounds = {-12'345.0, -9'876.0, 38'765.0, 31'234.0};
    cfg.sea_level_m = -10'000.0f;
    return cfg;
}

double overlap_area_m2(worldsim::CellCoord coord, std::int32_t cell_m,
                       const worldsim::WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) return 0.0;
    return (x1 - x0) * (y1 - y0);
}

double volume_m3(double depth_mm, double area_m2) {
    return depth_mm * 0.001 * area_m2;
}

worldsim::CellCoord find_partial_parent(
    const worldsim::World& world,
    const worldsim::ContinentalHydrologyResult& topology) {
    for (const auto& coarse : topology.cells) {
        if (coarse.ocean) continue;
        const auto tile = world.refine_authoritative_hydrology_tile(topology, coarse.coord);
        std::size_t active = 0;
        for (const auto& child : tile.hydrology.cells) active += child.active ? 1U : 0U;
        if (active >= 2 && active < 64) return coarse.coord;
    }
    throw std::runtime_error("fixture has no partial land parent with multiple L1 children");
}

std::vector<worldsim::HydrometeorologicalForcing> warm_rain_forcing(
    const worldsim::AuthoritativeHydrologyTile& tile,
    float precipitation_mm) {
    std::vector<worldsim::HydrometeorologicalForcing> out;
    out.reserve(tile.hydrology.cells.size());
    for (const auto& cell : tile.hydrology.cells) {
        worldsim::HydrometeorologicalForcing f;
        f.coord = cell.coord;
        if (cell.active && !cell.ocean) {
            f.precipitation_mm = precipitation_mm;
            f.mean_air_temperature_c = 10.0f;
            f.potential_evapotranspiration_mm = 0.0f;
        }
        out.push_back(f);
    }
    return out;
}

} // namespace

int main() {
    using namespace worldsim;

    World world(partial_land_config());
    const auto topology = world.analyze_continental_hydrology({0.1f});
    const CellCoord parent = find_partial_parent(world, topology);
    const auto tile = world.refine_authoritative_hydrology_tile(topology, parent);

    DynamicHydrologyParameters params;
    const auto parent_properties = world.sample_climate_soil(parent);
    const double parent_capacity = static_cast<double>(params.soil_capacity_mm) *
                                   parent_properties.storage_capacity_scale;
    const double expected_parent_initial = static_cast<double>(params.initial_soil_water_mm) *
                                           parent_properties.storage_capacity_scale;

    auto coarse = make_continental_water_state(world, topology, params);
    check(near(coarse.cell(parent).soil_water_mm, expected_parent_initial),
          "L0 initial soil water scales with parent storage capacity");
    check(static_cast<double>(coarse.cell(parent).soil_water_mm) <= parent_capacity + 1e-4,
          "L0 initial soil water stays within local parent capacity");

    auto detailed = make_dynamic_hydrology_tile_state(world, tile, params);
    bool child_storage_varies = false;
    bool have_child = false;
    float first_child_soil = 0.0f;
    for (std::size_t i = 0; i < detailed.cells.size(); ++i) {
        const auto& cell = detailed.cells[i];
        if (!cell.active || tile.hydrology.cells[i].ocean) continue;
        const auto properties = world.sample_soil(cell.coord);
        const double capacity = static_cast<double>(params.soil_capacity_mm) *
                                properties.storage_capacity_scale;
        const double expected_initial = static_cast<double>(params.initial_soil_water_mm) *
                                        properties.storage_capacity_scale;
        check(near(cell.soil_water_mm, expected_initial),
              "L1 initial soil water scales with child storage capacity");
        check(near(static_cast<double>(cell.soil_water_mm) / capacity,
                   static_cast<double>(params.initial_soil_water_mm) / params.soil_capacity_mm,
                   2e-6),
              "L1 initialization preserves reference soil saturation");
        if (!have_child) {
            first_child_soil = cell.soil_water_mm;
            have_child = true;
        } else {
            child_storage_varies = child_storage_varies ||
                std::abs(cell.soil_water_mm - first_child_soil) > 1e-4f;
        }
    }
    check(child_storage_varies, "spatial storage properties create heterogeneous L1 soil depth");

    DynamicHydrologyParameters flux_params;
    flux_params.soil_capacity_mm = 1000.0f;
    flux_params.field_capacity_mm = 1000.0f;
    flux_params.wilting_point_mm = 0.0f;
    flux_params.infiltration_capacity_mm_per_day = 10.0f;
    flux_params.surface_storage_capacity_mm = 1000.0f;
    flux_params.percolation_rate_per_day = 0.0f;
    flux_params.groundwater_recession_per_day = 0.0f;
    flux_params.initial_soil_water_mm = 0.0f;
    flux_params.initial_groundwater_mm = 0.0f;

    auto detailed_flux = make_dynamic_hydrology_tile_state(world, tile, flux_params);
    const auto detailed_forcing = warm_rain_forcing(tile, 100.0f);
    const auto detailed_report = advance_dynamic_hydrology_tile(
        world, tile, detailed_flux, detailed_forcing, {}, 1.0, flux_params);
    check(std::abs(detailed_report.water_balance_error_m3) < 1.0,
          "capacity-aware L1 step closes water balance");
    bool child_infiltration_varies = false;
    bool have_infiltration = false;
    float first_infiltration = 0.0f;
    for (std::size_t i = 0; i < detailed_flux.cells.size(); ++i) {
        const auto& cell = detailed_flux.cells[i];
        if (!cell.active || tile.hydrology.cells[i].ocean) continue;
        const auto properties = world.sample_soil(cell.coord);
        const double expected = static_cast<double>(flux_params.infiltration_capacity_mm_per_day) *
                                properties.infiltration_capacity_scale;
        check(near(cell.soil_water_mm, expected, 2e-5),
              "L1 infiltration uses the child infiltration-capacity scale");
        if (!have_infiltration) {
            first_infiltration = cell.soil_water_mm;
            have_infiltration = true;
        } else {
            child_infiltration_varies = child_infiltration_varies ||
                std::abs(cell.soil_water_mm - first_infiltration) > 1e-4f;
        }
    }
    check(child_infiltration_varies, "spatial infiltration properties change L1 bucket response");

    auto coarse_flux = make_continental_water_state(world, topology, flux_params);
    std::vector<ContinentalWaterForcing> coarse_forcing(topology.cells.size());
    coarse_forcing[topology.index_of(parent)] = {100.0f, 10.0f, 0.0f};
    const auto coarse_report = advance_continental_water_day(coarse_flux, coarse_forcing, flux_params);
    const double expected_parent_infiltration =
        static_cast<double>(flux_params.infiltration_capacity_mm_per_day) *
        parent_properties.infiltration_capacity_scale;
    check(near(coarse_flux.cell(parent).soil_water_mm, expected_parent_infiltration, 2e-5),
          "L0 infiltration uses the parent-equivalent infiltration-capacity scale");
    check(std::abs(coarse_report.water_balance_error_m3) < 1.0,
          "capacity-aware L0 step closes water balance");

    DynamicHydrologyParameters transfer_params;
    transfer_params.soil_capacity_mm = 200.0f;
    transfer_params.field_capacity_mm = 120.0f;
    transfer_params.wilting_point_mm = 20.0f;
    transfer_params.initial_soil_water_mm = 80.0f;
    transfer_params.initial_groundwater_mm = 0.0f;
    auto mixed = make_multiresolution_water_state(world, topology, transfer_params);
    const auto parent_index = mixed.coarse_state().index_of(parent);
    const auto parent_before = mixed.coarse_state().cells()[parent_index];
    const double parent_area = overlap_area_m2(
        parent, world.config().climate_cell_m, world.config().bounds);
    const double transfer_parent_capacity = static_cast<double>(transfer_params.soil_capacity_mm) *
                                            parent_properties.storage_capacity_scale;
    const double parent_saturation = static_cast<double>(parent_before.soil_water_mm) /
                                     transfer_parent_capacity;

    const auto& refined = materialize_refined_water_tile(world, topology, mixed, parent);
    double child_soil_volume = 0.0;
    bool heterogeneous_transfer = false;
    bool first_transfer = true;
    float first_transfer_depth = 0.0f;
    for (std::size_t i = 0; i < refined.cells.size(); ++i) {
        const auto& cell = refined.cells[i];
        if (!cell.active || tile.hydrology.cells[i].ocean) continue;
        const auto properties = world.sample_soil(cell.coord);
        const double child_capacity = static_cast<double>(transfer_params.soil_capacity_mm) *
                                      properties.storage_capacity_scale;
        const double expected = parent_saturation * child_capacity;
        check(near(cell.soil_water_mm, expected, 2e-5),
              "L0 to L1 refinement preserves saturation against heterogeneous child capacity");
        check(static_cast<double>(cell.soil_water_mm) <= child_capacity + 1e-4,
              "L0 to L1 refinement never overfills a child soil bucket");
        const double area = overlap_area_m2(
            cell.coord, world.config().regional_cell_m, world.config().bounds);
        child_soil_volume += volume_m3(cell.soil_water_mm, area);
        if (first_transfer) {
            first_transfer_depth = cell.soil_water_mm;
            first_transfer = false;
        } else {
            heterogeneous_transfer = heterogeneous_transfer ||
                std::abs(cell.soil_water_mm - first_transfer_depth) > 1e-4f;
        }
    }
    const double parent_soil_volume = volume_m3(parent_before.soil_water_mm, parent_area);
    check(near(child_soil_volume, parent_soil_volume, 0.5, 2e-6),
          "saturation-preserving L0 to L1 soil transfer conserves volume on a partial parent");
    check(heterogeneous_transfer, "refinement now produces capacity-driven heterogeneous soil depth");
    aggregate_refined_water_tile(world, mixed, parent);
    check(near(mixed.coarse_state().cell(parent).soil_water_mm, parent_before.soil_water_mm, 2e-5),
          "heterogeneous L1 to L0 soil aggregation restores parent depth");

    auto invalid_detailed = make_dynamic_hydrology_tile_state(world, tile, params);
    std::size_t invalid_index = invalid_detailed.cells.size();
    for (std::size_t i = 0; i < invalid_detailed.cells.size(); ++i) {
        if (invalid_detailed.cells[i].active && !tile.hydrology.cells[i].ocean) {
            invalid_index = i;
            break;
        }
    }
    check(invalid_index != invalid_detailed.cells.size(), "fixture has an active L1 land child");
    if (invalid_index != invalid_detailed.cells.size()) {
        const auto properties = world.sample_soil(invalid_detailed.cells[invalid_index].coord);
        const double capacity = static_cast<double>(params.soil_capacity_mm) *
                                properties.storage_capacity_scale;
        invalid_detailed.cells[invalid_index].soil_water_mm = static_cast<float>(capacity + 10.0);
        const float invalid_before = invalid_detailed.cells[invalid_index].soil_water_mm;
        std::vector<HydrometeorologicalForcing> zero_forcing;
        zero_forcing.reserve(tile.hydrology.cells.size());
        for (const auto& cell : tile.hydrology.cells) {
            HydrometeorologicalForcing f;
            f.coord = cell.coord;
            zero_forcing.push_back(f);
        }
        bool invalid_rejected = false;
        try {
            (void)advance_dynamic_hydrology_tile(
                world, tile, invalid_detailed, zero_forcing, {}, 1.0, params);
        } catch (const std::invalid_argument&) {
            invalid_rejected = true;
        }
        check(invalid_rejected && invalid_detailed.simulated_days == 0.0 &&
              invalid_detailed.cells[invalid_index].soil_water_mm == invalid_before,
              "L1 step rejects over-capacity soil state before mutation");
    }

    const auto path = std::filesystem::temp_directory_path() / "worldsim_soil_capacity_v2.wsmw";
    save_multiresolution_water_state(mixed, path);
    std::uint32_t version = 0;
    {
        std::ifstream in(path, std::ios::binary);
        in.seekg(8, std::ios::beg);
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
    }
    check(version == 2u, "capacity-aware multiresolution persistence writes format v2");
    {
        std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
        const std::uint32_t legacy = 1u;
        io.seekp(8, std::ios::beg);
        io.write(reinterpret_cast<const char*>(&legacy), sizeof(legacy));
    }
    bool legacy_rejected = false;
    try {
        (void)load_multiresolution_water_state(world, topology, path);
    } catch (const std::runtime_error&) {
        legacy_rejected = true;
    }
    check(legacy_rejected, "uniform-capacity multiresolution persistence v1 is rejected explicitly");
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All capacity-aware soil water tests passed\n";
    return 0;
}
