#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
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
    return std::abs(a - b) <= std::max(abs_eps, std::max(std::abs(a), std::abs(b)) * rel_eps);
}

worldsim::WorldConfig partial_land_config(worldsim::Seed seed = 91) {
    worldsim::WorldConfig cfg;
    cfg.seed = seed;
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

double volume(double depth_mm, double area_m2) {
    return depth_mm * 0.001 * area_m2;
}

worldsim::CellCoord find_partial_parent(const worldsim::ContinentalHydrologyResult& topology,
                                        const worldsim::WorldConfig& cfg) {
    const double full = static_cast<double>(cfg.climate_cell_m) * cfg.climate_cell_m;
    for (const auto& cell : topology.cells) {
        const double area = overlap_area_m2(cell.coord, cfg.climate_cell_m, cfg.bounds);
        if (!cell.ocean && area > 0.0 && area < full) return cell.coord;
    }
    throw std::runtime_error("fixture has no partial land parent");
}

worldsim::CellCoord find_routed_source(const worldsim::ContinentalHydrologyResult& topology) {
    for (const auto& cell : topology.cells) {
        if (!cell.ocean && cell.has_downstream) return cell.coord;
    }
    throw std::runtime_error("fixture has no routed land source");
}

bool same_refined_cell(const worldsim::DynamicHydrologyCellState& a,
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

void advance_days(const worldsim::World& world,
                  worldsim::MultiresolutionWaterState& state,
                  int days) {
    for (int day = 0; day < days; ++day) {
        const auto forcing = worldsim::make_smooth_continental_daily_forcing(state.coarse_state());
        (void)worldsim::advance_multiresolution_water_day(world, state, forcing);
    }
}
} // namespace

int main() {
    using namespace worldsim;

    World world(partial_land_config());
    const auto topology = world.analyze_continental_hydrology({0.1f});
    auto state = make_multiresolution_water_state(world, topology);
    auto twin = make_multiresolution_water_state(world, topology);
    advance_days(world, state, 12);
    advance_days(world, twin, 12);

    const CellCoord partial_parent = find_partial_parent(topology, world.config());
    const auto parent_index = state.coarse_state().index_of(partial_parent);
    const auto parent_before = state.coarse_state().cells()[parent_index];
    const double parent_area = overlap_area_m2(
        partial_parent, world.config().climate_cell_m, world.config().bounds);

    const auto& refined = materialize_refined_water_tile(world, topology, state, partial_parent);
    const auto& refined_twin = materialize_refined_water_tile(world, topology, twin, partial_parent);
    check(state.refined_tile_count() == 1 && state.is_refined(partial_parent),
          "materialization creates exactly one sparse refined owner");
    check(refined.simulated_day == state.simulated_day(),
          "refined tile inherits the exact global integer day");

    const auto& coarse_after_materialize = state.coarse_state().cell(partial_parent);
    check(coarse_after_materialize.snow_water_equivalent_mm == 0.0f &&
          coarse_after_materialize.surface_water_mm == 0.0f &&
          coarse_after_materialize.soil_water_mm == 0.0f &&
          coarse_after_materialize.groundwater_mm == 0.0f,
          "refined parent has no independent coarse water stores");

    double child_area = 0.0;
    double snow_m3 = 0.0;
    double surface_m3 = 0.0;
    double soil_m3 = 0.0;
    double groundwater_m3 = 0.0;
    bool deterministic_refinement = refined.cells.size() == refined_twin.cells.size();
    for (std::size_t i = 0; i < refined.cells.size(); ++i) {
        const auto& cell = refined.cells[i];
        deterministic_refinement = deterministic_refinement && same_refined_cell(cell, refined_twin.cells[i]);
        if (!cell.active) continue;
        const double area = overlap_area_m2(cell.coord, world.config().regional_cell_m, world.config().bounds);
        child_area += area;
        snow_m3 += volume(cell.snow_water_equivalent_mm, area);
        surface_m3 += volume(cell.surface_water_mm, area);
        soil_m3 += volume(cell.soil_water_mm, area);
        groundwater_m3 += volume(cell.groundwater_mm, area);
    }
    check(near(child_area, parent_area, 1e-6),
          "partial-world L1 child overlap areas sum to the L0 parent overlap area");
    check(near(snow_m3, volume(parent_before.snow_water_equivalent_mm, parent_area), 1e-5) &&
          near(surface_m3, volume(parent_before.surface_water_mm, parent_area), 1e-5) &&
          near(soil_m3, volume(parent_before.soil_water_mm, parent_area), 0.5, 2e-6) &&
          near(groundwater_m3, volume(parent_before.groundwater_mm, parent_area), 1e-5),
          "L0 to L1 refinement conserves every water store on a partial parent");
    check(deterministic_refinement, "L0 to L1 refinement is deterministic");

    const auto& repeated = materialize_refined_water_tile(world, topology, state, partial_parent);
    check(&repeated == &state.refined_tile(partial_parent) && state.refined_tile_count() == 1,
          "repeated materialization is idempotent and cannot double-count water");

    aggregate_refined_water_tile(world, state, partial_parent);
    check(!state.is_refined(partial_parent) && state.refined_tile_count() == 0,
          "aggregation releases detailed ownership");
    const auto& parent_roundtrip = state.coarse_state().cell(partial_parent);
    check(near(parent_roundtrip.snow_water_equivalent_mm, parent_before.snow_water_equivalent_mm, 1e-5) &&
          near(parent_roundtrip.surface_water_mm, parent_before.surface_water_mm, 1e-5) &&
          near(parent_roundtrip.soil_water_mm, parent_before.soil_water_mm, 1e-5) &&
          near(parent_roundtrip.groundwater_mm, parent_before.groundwater_mm, 1e-5),
          "L0 to L1 to L0 round trip preserves all four parent depths");
    (void)materialize_refined_water_tile(world, topology, state, partial_parent);
    aggregate_refined_water_tile(world, state, partial_parent);
    check(state.refined_tile_count() == 0,
          "materialize/dematerialize can be repeated without retained duplicate ownership");

    bool wrong_world_threw = false;
    try {
        auto other_cfg = partial_land_config(92);
        World other(other_cfg);
        (void)materialize_refined_water_tile(other, topology, state, partial_parent);
    } catch (const std::invalid_argument&) {
        wrong_world_threw = true;
    }
    check(wrong_world_threw, "refinement rejects a different world identity");

    bool wrong_parent_threw = false;
    try {
        auto malformed = topology;
        auto& parent = malformed.cells[malformed.index_of(partial_parent)];
        parent.has_downstream = true;
        parent.downstream_coord = partial_parent;
        (void)materialize_refined_water_tile(world, malformed, state, partial_parent);
    } catch (const std::invalid_argument&) {
        wrong_parent_threw = true;
    }
    check(wrong_parent_threw, "refinement rejects a parent whose topology does not match owned L0 routing");

    DynamicHydrologyParameters route_params;
    route_params.soil_capacity_mm = 0.0f;
    route_params.field_capacity_mm = 0.0f;
    route_params.wilting_point_mm = 0.0f;
    route_params.infiltration_capacity_mm_per_day = 0.0f;
    route_params.surface_storage_capacity_mm = 0.0f;
    route_params.percolation_rate_per_day = 0.0f;
    route_params.groundwater_recession_per_day = 0.0f;
    route_params.initial_soil_water_mm = 0.0f;
    route_params.initial_groundwater_mm = 0.0f;

    auto route_state = make_multiresolution_water_state(world, topology, route_params);
    auto route_twin = make_multiresolution_water_state(world, topology, route_params);
    const CellCoord source = find_routed_source(topology);
    const CellCoord downstream = topology.cell(source).downstream_coord;
    (void)materialize_refined_water_tile(world, topology, route_state, downstream);
    (void)materialize_refined_water_tile(world, topology, route_twin, downstream);
    std::vector<ContinentalWaterForcing> pulse(topology.cells.size());
    const auto source_index = topology.index_of(source);
    pulse[source_index] = {100.0f, 10.0f, 0.0f};
    const auto route_report = advance_multiresolution_water_day(world, route_state, pulse);
    const auto route_twin_report = advance_multiresolution_water_day(world, route_twin, pulse);
    check(near(route_report.precipitation_m3, route_report.terminal_outflow_m3, 0.5, 2e-7) &&
          near(route_report.storage_after_m3, 0.0, 0.5) &&
          std::abs(route_report.water_balance_error_m3) < 0.5,
          "coarse upstream to refined ingress to coarse downstream routes water exactly once");
    check(near(route_report.terminal_outflow_m3, route_twin_report.terminal_outflow_m3, 1e-6) &&
          near(route_report.storage_after_m3, route_twin_report.storage_after_m3, 1e-6),
          "coupled coarse/refined stepping is deterministic");
    bool refined_route_observed = false;
    for (const auto& cell : route_state.refined_tile(downstream).cells) {
        refined_route_observed = refined_route_observed || cell.last_routed_discharge_m3_s > 0.0f;
    }
    check(refined_route_observed, "coarse upstream volume traverses the refined L1 drainage graph");
    check(route_state.simulated_day() == 1 && route_state.refined_tile(downstream).simulated_day == 1,
          "coarse and refined ownership advance one exact shared day");

    auto atomic_state = make_multiresolution_water_state(world, topology);
    (void)materialize_refined_water_tile(world, topology, atomic_state, partial_parent);
    const auto coarse_before_invalid = atomic_state.coarse_state().cells();
    const auto detailed_before_invalid = atomic_state.refined_tile(partial_parent).cells;
    const auto day_before_invalid = atomic_state.simulated_day();
    auto invalid_forcing = make_smooth_continental_daily_forcing(atomic_state.coarse_state());
    invalid_forcing[topology.index_of(partial_parent)].precipitation_mm =
        std::numeric_limits<float>::max();
    bool invalid_threw = false;
    try {
        (void)advance_multiresolution_water_day(world, atomic_state, invalid_forcing);
    } catch (const std::invalid_argument&) {
        invalid_threw = true;
    }
    bool detailed_unchanged = detailed_before_invalid.size() == atomic_state.refined_tile(partial_parent).cells.size();
    for (std::size_t i = 0; i < detailed_before_invalid.size() && detailed_unchanged; ++i) {
        detailed_unchanged = same_refined_cell(
            detailed_before_invalid[i], atomic_state.refined_tile(partial_parent).cells[i]);
    }
    check(invalid_threw && atomic_state.simulated_day() == day_before_invalid &&
          std::memcmp(coarse_before_invalid.data(), atomic_state.coarse_state().cells().data(),
                      coarse_before_invalid.size() * sizeof(ContinentalWaterCellState)) == 0 &&
          detailed_unchanged,
          "invalid coupled forcing is rejected without partial coarse or refined mutation");

    WorldConfig ocean_cfg = partial_land_config(93);
    ocean_cfg.sea_level_m = 10'000.0f;
    World ocean_world(ocean_cfg);
    const auto ocean_topology = ocean_world.analyze_continental_hydrology();
    auto ocean_state = make_multiresolution_water_state(ocean_world, ocean_topology);
    const CellCoord ocean_parent = ocean_topology.cells.front().coord;
    (void)materialize_refined_water_tile(ocean_world, ocean_topology, ocean_state, ocean_parent);
    std::vector<ContinentalWaterForcing> ocean_forcing(ocean_topology.cells.size());
    for (auto& f : ocean_forcing) f = {12.0f, 7.0f, 2.0f};
    const auto ocean_report = advance_multiresolution_water_day(ocean_world, ocean_state, ocean_forcing);
    check(ocean_report.precipitation_m3 == 0.0 && ocean_report.storage_after_m3 == 0.0 &&
          ocean_report.terminal_outflow_m3 == 0.0 && ocean_state.simulated_day() == 1,
          "refined ocean ownership advances clock without creating terrestrial water");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All multiresolution water tests passed\n";
    return 0;
}