#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    using namespace worldsim;

    WorldConfig cfg;
    cfg.seed = 701;
    cfg.bounds = {-120'000.0, -100'000.0, 240'000.0, 200'000.0};
    World world(cfg);
    const auto topology = world.analyze_continental_hydrology({0.1f});
    auto state = make_multiresolution_water_state(world, topology);

    CellCoord parent{};
    bool found = false;
    for (const auto& cell : topology.cells) {
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

    const auto& refined = materialize_refined_water_tile(world, topology, state, parent);

    bool high_rejected = false;
    try {
        (void)refined.index_of({std::numeric_limits<std::int64_t>::max(),
                                std::numeric_limits<std::int64_t>::max()});
    } catch (const std::out_of_range&) {
        high_rejected = true;
    }
    if (!high_rejected) {
        std::cerr << "FAIL: extreme positive refined coordinate was not rejected\n";
        return 1;
    }

    bool low_rejected = false;
    try {
        (void)refined.index_of({std::numeric_limits<std::int64_t>::min(),
                                std::numeric_limits<std::int64_t>::min()});
    } catch (const std::out_of_range&) {
        low_rejected = true;
    }
    if (!low_rejected) {
        std::cerr << "FAIL: extreme negative refined coordinate was not rejected\n";
        return 1;
    }

    for (std::size_t i = 0; i < refined.cells.size(); ++i) {
        if (refined.index_of(refined.cells[i].coord) != i) {
            std::cerr << "FAIL: valid refined coordinate/index round trip changed\n";
            return 1;
        }
    }

    std::cout << "Refined water extreme-coordinate indexing passed\n";
    return 0;
}
