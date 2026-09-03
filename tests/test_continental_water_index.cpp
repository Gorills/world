#include "worldsim/continental_water.hpp"
#include "worldsim/world.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    worldsim::WorldConfig cfg;
    cfg.seed = 601;
    cfg.bounds = {-120'000.0, -100'000.0, 240'000.0, 200'000.0};
    worldsim::World world(cfg);
    const auto topology = world.analyze_continental_hydrology({0.1f});
    const auto state = worldsim::make_continental_water_state(world, topology);

    bool high_rejected = false;
    try {
        (void)state.index_of({std::numeric_limits<std::int64_t>::max(),
                              std::numeric_limits<std::int64_t>::max()});
    } catch (const std::out_of_range&) {
        high_rejected = true;
    }
    if (!high_rejected) {
        std::cerr << "FAIL: extreme positive coordinate was not rejected\n";
        return 1;
    }

    bool low_rejected = false;
    try {
        (void)state.index_of({std::numeric_limits<std::int64_t>::min(),
                              std::numeric_limits<std::int64_t>::min()});
    } catch (const std::out_of_range&) {
        low_rejected = true;
    }
    if (!low_rejected) {
        std::cerr << "FAIL: extreme negative coordinate was not rejected\n";
        return 1;
    }

    for (std::size_t i = 0; i < state.cells().size(); ++i) {
        if (state.index_of(state.coord_of(i)) != i) {
            std::cerr << "FAIL: valid coordinate round trip changed\n";
            return 1;
        }
    }

    std::cout << "Continental water extreme-coordinate indexing passed\n";
    return 0;
}
