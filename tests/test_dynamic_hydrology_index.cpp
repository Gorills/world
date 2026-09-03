#include "worldsim/dynamic_hydrology.hpp"
#include "worldsim/world.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

bool same_cells(const worldsim::DynamicHydrologyTileState& a,
                const worldsim::DynamicHydrologyTileState& b) {
    if (a.cells.size() != b.cells.size()) return false;
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        const auto& x = a.cells[i];
        const auto& y = b.cells[i];
        if (x.coord != y.coord || x.active != y.active ||
            x.snow_water_equivalent_mm != y.snow_water_equivalent_mm ||
            x.surface_water_mm != y.surface_water_mm ||
            x.soil_water_mm != y.soil_water_mm ||
            x.groundwater_mm != y.groundwater_mm ||
            x.last_evapotranspiration_mm != y.last_evapotranspiration_mm ||
            x.last_quick_runoff_mm != y.last_quick_runoff_mm ||
            x.last_baseflow_mm != y.last_baseflow_mm ||
            x.last_routed_discharge_m3_s != y.last_routed_discharge_m3_s) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    using namespace worldsim;

    DynamicHydrologyTileState direct;
    direct.climate_coord = {-1, -1};
    direct.cells.resize(64);
    for (std::int64_t y = 0; y < 8; ++y) {
        for (std::int64_t x = 0; x < 8; ++x) {
            const auto i = static_cast<std::size_t>(y * 8 + x);
            direct.cells[i].coord = {-8 + x, -8 + y};
        }
    }

    bool high_rejected = false;
    try {
        (void)direct.index_of({std::numeric_limits<std::int64_t>::max(),
                               std::numeric_limits<std::int64_t>::max()});
    } catch (const std::out_of_range&) {
        high_rejected = true;
    }
    if (!high_rejected) {
        std::cerr << "FAIL: extreme positive dynamic-hydrology coordinate was not rejected\n";
        return 1;
    }

    bool low_rejected = false;
    try {
        (void)direct.index_of({std::numeric_limits<std::int64_t>::min(),
                               std::numeric_limits<std::int64_t>::min()});
    } catch (const std::out_of_range&) {
        low_rejected = true;
    }
    if (!low_rejected) {
        std::cerr << "FAIL: extreme negative dynamic-hydrology coordinate was not rejected\n";
        return 1;
    }

    for (std::size_t i = 0; i < direct.cells.size(); ++i) {
        if (direct.index_of(direct.cells[i].coord) != i) {
            std::cerr << "FAIL: valid dynamic-hydrology coordinate/index round trip changed\n";
            return 1;
        }
    }

    WorldConfig cfg;
    cfg.seed = 811;
    cfg.bounds = {-120'000.0, -100'000.0, 240'000.0, 200'000.0};
    cfg.sea_level_m = -10'000.0f;
    World world(cfg);
    const auto continent = world.analyze_continental_hydrology({0.1f});

    CellCoord parent{};
    bool found = false;
    for (const auto& cell : continent.cells) {
        if (!cell.ocean) {
            parent = cell.coord;
            found = true;
            break;
        }
    }
    if (!found) {
        std::cerr << "FAIL: fixture contains no refinable land parent\n";
        return 1;
    }

    auto tile = world.refine_authoritative_hydrology_tile(continent, parent);
    auto state = make_dynamic_hydrology_tile_state(world, tile);
    auto forcing = make_smooth_climatological_forcing(world, tile, 120.0, 1.0);

    std::size_t active_land = state.cells.size();
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        if (state.cells[i].active && !tile.hydrology.cells[i].ocean) {
            active_land = i;
            break;
        }
    }
    if (active_land == state.cells.size()) {
        std::cerr << "FAIL: fixture contains no active terrestrial L1 cell\n";
        return 1;
    }

    {
        auto unsafe_state = make_dynamic_hydrology_tile_state(world, tile);
        const auto before = unsafe_state;
        auto unsafe_forcing = forcing;
        unsafe_forcing[active_land].precipitation_mm = std::numeric_limits<float>::max();
        bool rejected = false;
        try {
            (void)advance_dynamic_hydrology_tile(world, tile, unsafe_state, unsafe_forcing, {}, 1.0);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected || unsafe_state.simulated_days != before.simulated_days ||
            !same_cells(unsafe_state, before)) {
            std::cerr << "FAIL: numerically unsafe finite precipitation was not rejected atomically\n";
            return 1;
        }
    }

    {
        auto unsafe_state = make_dynamic_hydrology_tile_state(world, tile);
        const auto before = unsafe_state;
        const double large = std::numeric_limits<double>::max() * 0.75;
        const std::vector<ExternalHydrologyInflow> inflows{
            {unsafe_state.cells[active_land].coord, large},
            {unsafe_state.cells[active_land].coord, large}};
        bool rejected = false;
        try {
            (void)advance_dynamic_hydrology_tile(world, tile, unsafe_state, forcing, inflows, 1.0);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected || unsafe_state.simulated_days != before.simulated_days ||
            !same_cells(unsafe_state, before)) {
            std::cerr << "FAIL: overflowing external-inflow accumulation was not rejected atomically\n";
            return 1;
        }
    }

    {
        auto unsafe_state = make_dynamic_hydrology_tile_state(world, tile);
        const auto before = unsafe_state;
        const double diagnostic_overflow =
            static_cast<double>(std::numeric_limits<float>::max()) * 86'400.0 * 2.0;
        const std::vector<ExternalHydrologyInflow> inflows{
            {unsafe_state.cells[active_land].coord, diagnostic_overflow}};
        bool rejected = false;
        try {
            (void)advance_dynamic_hydrology_tile(world, tile, unsafe_state, forcing, inflows, 1.0);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected || unsafe_state.simulated_days != before.simulated_days ||
            !same_cells(unsafe_state, before)) {
            std::cerr << "FAIL: float-discharge overflow was not rejected atomically\n";
            return 1;
        }
    }

    {
        auto unsafe_state = make_dynamic_hydrology_tile_state(world, tile);
        unsafe_state.simulated_days = std::numeric_limits<double>::max();
        const auto before = unsafe_state;
        bool rejected = false;
        try {
            (void)advance_dynamic_hydrology_tile(world, tile, unsafe_state, forcing, {}, 1.0);
        } catch (const std::overflow_error&) {
            rejected = true;
        }
        if (!rejected || unsafe_state.simulated_days != before.simulated_days ||
            !same_cells(unsafe_state, before)) {
            std::cerr << "FAIL: unrepresentable dynamic-hydrology clock advance was not rejected atomically\n";
            return 1;
        }
    }

    bool malformed_route_rejected = false;
    for (auto& cell : tile.hydrology.cells) {
        if (!cell.active || cell.ocean) continue;
        cell.has_downstream = true;
        cell.downstream_is_external = false;
        cell.downstream_coord = {std::numeric_limits<std::int64_t>::max(),
                                 std::numeric_limits<std::int64_t>::max()};
        try {
            (void)advance_dynamic_hydrology_tile(world, tile, state, forcing, {}, 1.0);
        } catch (const std::invalid_argument&) {
            malformed_route_rejected = true;
        }
        break;
    }
    if (!malformed_route_rejected) {
        std::cerr << "FAIL: malformed extreme downstream coordinate was not rejected\n";
        return 1;
    }

    AuthoritativeHydrologyTile impossible;
    impossible.config = world.config();
    impossible.climate_coord = {std::numeric_limits<std::int64_t>::max(), 0};
    impossible.hydrology.request.width_cells = 8;
    impossible.hydrology.request.height_cells = 8;
    impossible.hydrology.cells.resize(64);
    bool impossible_parent_rejected = false;
    try {
        (void)make_dynamic_hydrology_tile_state(world, impossible);
    } catch (const std::out_of_range&) {
        impossible_parent_rejected = true;
    }
    if (!impossible_parent_rejected) {
        std::cerr << "FAIL: unrepresentable dynamic-hydrology parent coordinate was not rejected\n";
        return 1;
    }

    std::cout << "Dynamic hydrology extreme-coordinate and numerical safety tests passed\n";
    return 0;
}
