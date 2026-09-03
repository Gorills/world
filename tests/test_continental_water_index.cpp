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

    // Continental hydrology is a D8 graph. Caller-constructed public results must not be
    // accepted if an edge skips across the raster, otherwise a water state can be created
    // successfully and only fail later when multiresolution refined ingress needs D8 geometry.
    auto malformed = topology;
    std::size_t source = malformed.cells.size();
    std::size_t terminal = malformed.cells.size();
    for (std::size_t t = 0; t < malformed.cells.size() && source == malformed.cells.size(); ++t) {
        if (malformed.cells[t].has_downstream) continue;
        for (std::size_t s = 0; s < malformed.cells.size(); ++s) {
            if (s == t) continue;
            const auto a = malformed.cells[s].coord;
            const auto b = malformed.cells[t].coord;
            const auto dx = a.x > b.x ? a.x - b.x : b.x - a.x;
            const auto dy = a.y > b.y ? a.y - b.y : b.y - a.y;
            if (dx > 1 || dy > 1) {
                source = s;
                terminal = t;
                break;
            }
        }
    }
    if (source == malformed.cells.size()) {
        std::cerr << "FAIL: fixture has no non-adjacent source/terminal pair\n";
        return 1;
    }
    malformed.cells[source].has_downstream = true;
    malformed.cells[source].downstream_coord = malformed.cells[terminal].coord;

    bool non_d8_rejected = false;
    try {
        (void)worldsim::make_continental_water_state(world, malformed);
    } catch (const std::invalid_argument&) {
        non_d8_rejected = true;
    }
    if (!non_d8_rejected) {
        std::cerr << "FAIL: non-D8 caller-constructed downstream edge was accepted\n";
        return 1;
    }

    std::cout << "Continental water indexing/topology validation passed\n";
    return 0;
}
