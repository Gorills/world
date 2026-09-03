#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

long peak_rss_kib() {
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) return usage.ru_maxrss;
#endif
    return -1;
}
} // namespace

int main() {
    using namespace worldsim;

    WorldConfig cfg;
    cfg.seed = 71;
    // 612 x 734 L0 cells = 449,208 cells, matching the project Europe-scale fixture count.
    cfg.bounds = {0.0, 0.0, 5'010'000.0, 6'010'000.0};
    World world(cfg);

    const auto topology_begin = Clock::now();
    const auto topology = world.analyze_continental_hydrology({25.0f});
    const auto topology_end = Clock::now();
    if (topology.cells.size() != 449'208) {
        throw std::runtime_error("Europe-scale benchmark fixture no longer has 449208 L0 cells");
    }

    const auto state_begin = Clock::now();
    auto state = make_multiresolution_water_state(world, topology);
    const auto state_end = Clock::now();

    std::vector<CellCoord> refined_parents;
    refined_parents.reserve(64);
    for (const auto& cell : topology.cells) {
        if (!cell.ocean) refined_parents.push_back(cell.coord);
        if (refined_parents.size() == 64) break;
    }
    if (refined_parents.size() != 64) {
        throw std::runtime_error("Europe-scale benchmark fixture has fewer than 64 land cells");
    }

    const auto refine_begin = Clock::now();
    for (const auto coord : refined_parents) {
        (void)materialize_refined_water_tile(world, topology, state, coord);
    }
    const auto refine_end = Clock::now();
    if (state.refined_tile_count() != refined_parents.size()) {
        throw std::runtime_error("benchmark refinement count mismatch");
    }

    auto forcing = make_smooth_continental_daily_forcing(state.coarse_state());
    const auto step_begin = Clock::now();
    const auto report = advance_multiresolution_water_day(world, state, forcing);
    const auto step_end = Clock::now();
    if (report.day_after != 1 || !std::isfinite(report.water_balance_error_m3)) {
        throw std::runtime_error("benchmark multiresolution day did not complete correctly");
    }

    const auto aggregate_begin = Clock::now();
    for (const auto coord : refined_parents) {
        aggregate_refined_water_tile(world, state, coord);
    }
    const auto aggregate_end = Clock::now();
    if (state.refined_tile_count() != 0) {
        throw std::runtime_error("benchmark aggregation did not release refined ownership");
    }

    std::cout << std::fixed << std::setprecision(3)
              << "benchmark_l0_cells=" << topology.cells.size() << '\n'
              << "benchmark_refined_tiles=64\n"
              << "topology_ms=" << elapsed_ms(topology_begin, topology_end) << '\n'
              << "state_create_ms=" << elapsed_ms(state_begin, state_end) << '\n'
              << "materialize_64_ms=" << elapsed_ms(refine_begin, refine_end) << '\n'
              << "mixed_day_ms=" << elapsed_ms(step_begin, step_end) << '\n'
              << "aggregate_64_ms=" << elapsed_ms(aggregate_begin, aggregate_end) << '\n'
              << "peak_rss_kib=" << peak_rss_kib() << '\n'
              << "water_balance_error_m3=" << report.water_balance_error_m3 << '\n';
    return 0;
}
