#include "worldsim/dynamic_hydrology.hpp"
#include "worldsim/world.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

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

    std::cout << "Dynamic hydrology extreme-coordinate indexing passed\n";
    return 0;
}
