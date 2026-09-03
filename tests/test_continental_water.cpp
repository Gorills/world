#include "worldsim/continental_water.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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

worldsim::WorldConfig config(worldsim::Seed seed = 71) {
    worldsim::WorldConfig cfg;
    cfg.seed = seed;
    cfg.bounds = {-120'000.0, -100'000.0, 240'000.0, 200'000.0};
    return cfg;
}

bool near(double a, double b, double abs_eps, double rel_eps = 1e-9) {
    return std::abs(a - b) <= std::max(abs_eps, std::max(std::abs(a), std::abs(b)) * rel_eps);
}
} // namespace

int main() {
    using namespace worldsim;

    static_assert(sizeof(ContinentalWaterCellState) <= 32,
                  "L0 dynamic water state must stay compact enough for Europe-scale storage");

    World world(config());
    const auto topology = world.analyze_continental_hydrology({0.1f});
    DynamicHydrologyParameters params;
    auto state = make_continental_water_state(world, topology, params);

    check(state.cells().size() == topology.cells.size(), "state has one record per L0 cell");
    check(state.simulated_day() == 0, "global L0 clock starts at day zero");
    check(world.materialized_patch_count() == 0, "creating continental dynamic state does not materialize L2");

    bool coordinate_alignment = true;
    for (std::size_t i = 0; i < topology.cells.size(); ++i) {
        coordinate_alignment = coordinate_alignment &&
            state.coord_of(i) == topology.cells[i].coord &&
            state.index_of(topology.cells[i].coord) == i;
    }
    check(coordinate_alignment, "continental state preserves topology cell ordering and coordinates");

    auto twin = make_continental_water_state(world, topology, params);
    double max_balance_error = 0.0;
    bool deterministic = true;
    bool nonnegative = true;
    for (std::int64_t day = 0; day < 365; ++day) {
        const auto forcing = make_smooth_continental_daily_forcing(state);
        const auto report = advance_continental_water_day(state, forcing, params);
        const auto twin_report = advance_continental_water_day(twin, forcing, params);
        max_balance_error = std::max(max_balance_error, std::abs(report.water_balance_error_m3));
        deterministic = deterministic &&
            near(report.storage_after_m3, twin_report.storage_after_m3, 1e-6) &&
            near(report.terminal_outflow_m3, twin_report.terminal_outflow_m3, 1e-6) &&
            report.day_after == twin_report.day_after;
        for (const auto& cell : state.cells()) {
            nonnegative = nonnegative && cell.snow_water_equivalent_mm >= 0.0f &&
                cell.surface_water_mm >= 0.0f && cell.soil_water_mm >= 0.0f &&
                cell.groundwater_mm >= 0.0f;
        }
    }
    check(state.simulated_day() == 365 && twin.simulated_day() == 365,
          "all L0 cells share one exact integer global day");
    check(deterministic, "continental daily water stepping is deterministic");
    check(nonnegative, "continental daily water stores remain non-negative");
    check(max_balance_error < 250.0,
          "continental daily water balance closes within float-state rounding tolerance");
    check(world.materialized_patch_count() == 0, "365 L0 days do not materialize L2");

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
    auto route_state = make_continental_water_state(world, topology, route_params);
    std::vector<ContinentalWaterForcing> pulse(topology.cells.size());
    std::size_t source = topology.cells.size();
    for (std::size_t i = 0; i < topology.cells.size(); ++i) {
        if (!topology.cells[i].ocean) {
            source = i;
            break;
        }
    }
    check(source != topology.cells.size(), "fixture contains land");
    if (source != topology.cells.size()) {
        pulse[source] = {100.0f, 10.0f, 0.0f};
        const auto pulse_report = advance_continental_water_day(
            route_state, pulse, route_params);
        check(near(pulse_report.precipitation_m3, pulse_report.terminal_outflow_m3, 0.5, 2e-7),
              "single-cell warm-rain pulse exits through authoritative L0 drainage without loss");
        check(std::abs(pulse_report.water_balance_error_m3) < 0.5,
              "single-cell route fixture closes water balance");
        check(std::abs(pulse_report.storage_after_m3) < 0.5,
              "no-storage route fixture leaves no terrestrial water behind");
    }

    WorldConfig ocean_cfg = config(72);
    ocean_cfg.sea_level_m = 10'000.0f;
    World ocean_world(ocean_cfg);
    const auto ocean_topology = ocean_world.analyze_continental_hydrology();
    auto ocean_state = make_continental_water_state(ocean_world, ocean_topology, params);
    auto ocean_forcing = make_smooth_continental_daily_forcing(ocean_state);
    bool ocean_zero = true;
    for (std::size_t i = 0; i < ocean_topology.cells.size(); ++i) {
        ocean_zero = ocean_zero && ocean_topology.cells[i].ocean &&
            ocean_forcing[i].precipitation_mm == 0.0f &&
            ocean_forcing[i].potential_evapotranspiration_mm == 0.0f &&
            ocean_state.cells()[i].soil_water_mm == 0.0f &&
            ocean_state.cells()[i].groundwater_mm == 0.0f;
    }
    check(ocean_zero, "built-in forcing omits terrestrial precipitation/PET and stores over ocean");
    for (auto& f : ocean_forcing) {
        f.precipitation_mm = 12.0f;
        f.mean_air_temperature_c = 7.0f;
        f.potential_evapotranspiration_mm = 2.0f;
    }
    const auto ocean_report = advance_continental_water_day(
        ocean_state, ocean_forcing, params);
    check(ocean_report.precipitation_m3 == 0.0 && ocean_report.storage_after_m3 == 0.0 &&
          ocean_report.terminal_outflow_m3 == 0.0 && ocean_state.simulated_day() == 1,
          "all-ocean world advances clock without inventing terrestrial water");

    bool wrong_world_threw = false;
    try {
        auto other_cfg = config();
        ++other_cfg.seed;
        World other(other_cfg);
        (void)other;
        auto wrong_topology = other.analyze_continental_hydrology();
        (void)make_continental_water_state(world, wrong_topology, params);
    } catch (const std::invalid_argument&) {
        wrong_world_threw = true;
    }
    check(wrong_world_threw, "continental water state/topology reject a different world identity");

    bool malformed_forcing_threw = false;
    try {
        auto forcing = make_smooth_continental_daily_forcing(state);
        forcing.pop_back();
        (void)advance_continental_water_day(state, forcing, params);
    } catch (const std::invalid_argument&) {
        malformed_forcing_threw = true;
    }
    check(malformed_forcing_threw, "continental daily step rejects mis-sized forcing");

    bool invalid_value_threw = false;
    {
        auto atomic_state = make_continental_water_state(world, topology, params);
        const auto before_cells = atomic_state.cells();
        const auto before_day = atomic_state.simulated_day();
        auto forcing = make_smooth_continental_daily_forcing(atomic_state);
        forcing.back().mean_air_temperature_c = std::numeric_limits<float>::quiet_NaN();
        try {
            (void)advance_continental_water_day(atomic_state, forcing, params);
        } catch (const std::invalid_argument&) {
            invalid_value_threw = true;
        }
        check(atomic_state.simulated_day() == before_day &&
              atomic_state.cells().size() == before_cells.size() &&
              std::memcmp(atomic_state.cells().data(), before_cells.data(),
                          before_cells.size() * sizeof(ContinentalWaterCellState)) == 0,
              "rejected forcing leaves continental state byte-for-byte unchanged");
    }
    check(invalid_value_threw, "continental daily step rejects non-finite forcing atomically");

    bool unsafe_finite_forcing_threw = false;
    {
        auto atomic_state = make_continental_water_state(world, topology, params);
        const auto before_cells = atomic_state.cells();
        const auto before_day = atomic_state.simulated_day();
        auto forcing = make_smooth_continental_daily_forcing(atomic_state);
        for (std::size_t i = 0; i < topology.cells.size(); ++i) {
            if (!topology.cells[i].ocean) {
                forcing[i].precipitation_mm = std::numeric_limits<float>::max();
                forcing[i].mean_air_temperature_c = -20.0f;
                break;
            }
        }
        try {
            (void)advance_continental_water_day(atomic_state, forcing, params);
        } catch (const std::invalid_argument&) {
            unsafe_finite_forcing_threw = true;
        }
        check(atomic_state.simulated_day() == before_day &&
              std::memcmp(atomic_state.cells().data(), before_cells.data(),
                          before_cells.size() * sizeof(ContinentalWaterCellState)) == 0,
              "numerically unsafe finite forcing is rejected before any mutation");
    }
    check(unsafe_finite_forcing_threw,
          "continental daily step rejects finite forcing that can overflow float water state");

    bool unsafe_initial_storage_threw = false;
    try {
        auto unsafe_params = params;
        unsafe_params.soil_capacity_mm = std::numeric_limits<float>::max();
        unsafe_params.field_capacity_mm = 0.0f;
        unsafe_params.wilting_point_mm = 0.0f;
        unsafe_params.initial_soil_water_mm = std::numeric_limits<float>::max();
        unsafe_params.initial_groundwater_mm = 0.0f;
        (void)make_continental_water_state(world, topology, unsafe_params);
    } catch (const std::invalid_argument&) {
        unsafe_initial_storage_threw = true;
    }
    check(unsafe_initial_storage_threw,
          "continental state rejects finite initial storage outside its numerical safety envelope");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All continental water tests passed; max_daily_balance_error_m3="
              << max_balance_error << '\n';
    return 0;
}
