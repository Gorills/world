#include "worldsim/multiresolution_water.hpp"
#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
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
    cfg.bounds = {0.0, 0.0, 5'010'000.0, 6'010'000.0};
    World world(cfg);

    const auto topology_begin = Clock::now();
    const auto topology = world.analyze_continental_hydrology({25.0f});
    const auto topology_end = Clock::now();
    if (topology.cells.size() != 449'208) {
        throw std::runtime_error("Europe-scale weather benchmark fixture no longer has 449208 L0 cells");
    }

    const auto weather_begin = Clock::now();
    auto weather = make_weather_state(world);
    const auto weather_end = Clock::now();
    if (weather.cells().size() != topology.cells.size() || world.materialized_patch_count() != 0) {
        throw std::runtime_error("weather benchmark raster/materialization invariant failed");
    }

    const auto water_begin = Clock::now();
    auto water = make_multiresolution_water_state(world, topology);
    const auto water_end = Clock::now();

    std::vector<CellCoord> refined_parents;
    refined_parents.reserve(64);
    for (const auto& cell : topology.cells) {
        if (!cell.ocean) refined_parents.push_back(cell.coord);
        if (refined_parents.size() == 64) break;
    }
    if (refined_parents.size() != 64) {
        throw std::runtime_error("Europe-scale weather benchmark has fewer than 64 land cells");
    }
    for (const auto coord : refined_parents) {
        (void)materialize_refined_water_tile(world, topology, water, coord);
    }

    constexpr int kDays = 30;
    double cumulative_weather_precipitation_m3 = 0.0;
    double max_relative_balance_error = 0.0;
    double mean_wet_area_fraction = 0.0;
    const auto coupled_begin = Clock::now();
    for (int day = 0; day < kDays; ++day) {
        const auto report = advance_weather_multiresolution_water_day(world, weather, water);
        if (report.weather.day_after != day + 1 || report.water.day_after != day + 1 ||
            weather.simulated_day() != water.simulated_day()) {
            throw std::runtime_error("weather benchmark coupled clocks diverged");
        }
        cumulative_weather_precipitation_m3 += report.weather.precipitation_m3;
        mean_wet_area_fraction += report.weather.wet_area_fraction;
        const double scale = std::max(
            1.0,
            std::abs(report.water.storage_before_m3) + std::abs(report.water.precipitation_m3) +
                std::abs(report.water.evapotranspiration_m3) +
                std::abs(report.water.terminal_outflow_m3) +
                std::abs(report.water.storage_after_m3));
        const double relative_error = std::abs(report.water.water_balance_error_m3) / scale;
        if (!std::isfinite(relative_error)) {
            throw std::runtime_error("weather benchmark water balance is not finite");
        }
        max_relative_balance_error = std::max(max_relative_balance_error, relative_error);
    }
    const auto coupled_end = Clock::now();
    if (max_relative_balance_error > 1.0e-6) {
        throw std::runtime_error("weather-driven Europe water balance exceeds tolerance");
    }
    if (world.materialized_patch_count() != 0) {
        throw std::runtime_error("weather benchmark unexpectedly materialized L2 state");
    }

    mean_wet_area_fraction /= static_cast<double>(kDays);
    const double coupled_total_ms = elapsed_ms(coupled_begin, coupled_end);
    std::cout << std::fixed << std::setprecision(3)
              << "benchmark_l0_cells=" << topology.cells.size() << '\n'
              << "benchmark_refined_tiles=64\n"
              << "benchmark_days=" << kDays << '\n'
              << "topology_ms=" << elapsed_ms(topology_begin, topology_end) << '\n'
              << "weather_state_create_ms=" << elapsed_ms(weather_begin, weather_end) << '\n'
              << "water_state_create_ms=" << elapsed_ms(water_begin, water_end) << '\n'
              << "coupled_30_days_ms=" << coupled_total_ms << '\n'
              << "coupled_mean_day_ms=" << coupled_total_ms / static_cast<double>(kDays) << '\n'
              << "peak_rss_kib=" << peak_rss_kib() << '\n'
              << "mean_wet_area_fraction=" << mean_wet_area_fraction << '\n'
              << "weather_precipitation_30d_m3=" << cumulative_weather_precipitation_m3 << '\n'
              << std::scientific
              << "max_relative_water_balance_error=" << max_relative_balance_error << '\n';
    return 0;
}
