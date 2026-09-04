#include "worldsim/simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
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

bool same_report(
    const worldsim::WeatherWaterStepReport& a,
    const worldsim::WeatherWaterStepReport& b) {
    return a.weather.day_before == b.weather.day_before &&
        a.weather.day_after == b.weather.day_after &&
        a.weather.precipitation_m3 == b.weather.precipitation_m3 &&
        a.weather.mean_air_temperature_c == b.weather.mean_air_temperature_c &&
        a.weather.mean_potential_evapotranspiration_mm ==
            b.weather.mean_potential_evapotranspiration_mm &&
        a.weather.wet_area_fraction == b.weather.wet_area_fraction &&
        a.water.day_before == b.water.day_before &&
        a.water.day_after == b.water.day_after &&
        a.water.storage_before_m3 == b.water.storage_before_m3 &&
        a.water.precipitation_m3 == b.water.precipitation_m3 &&
        a.water.evapotranspiration_m3 == b.water.evapotranspiration_m3 &&
        a.water.terminal_outflow_m3 == b.water.terminal_outflow_m3 &&
        a.water.storage_after_m3 == b.water.storage_after_m3 &&
        a.water.water_balance_error_m3 == b.water.water_balance_error_m3;
}

bool same_weather_cell(
    const worldsim::WeatherState& a,
    const worldsim::WeatherState& b,
    worldsim::CellCoord coord) {
    const auto& x = a.cell(coord);
    const auto& y = b.cell(coord);
    return x.temperature_anomaly_c == y.temperature_anomaly_c &&
        x.moisture_anomaly == y.moisture_anomaly;
}

bool same_refined_tile(
    const worldsim::MultiresolutionWaterState& a,
    const worldsim::MultiresolutionWaterState& b,
    worldsim::CellCoord parent) {
    const auto& x = a.refined_tile(parent);
    const auto& y = b.refined_tile(parent);
    if (x.simulated_day != y.simulated_day || x.cells.size() != y.cells.size()) return false;
    for (std::size_t i = 0; i < x.cells.size(); ++i) {
        const auto& xc = x.cells[i];
        const auto& yc = y.cells[i];
        if (xc.coord != yc.coord || xc.active != yc.active ||
            xc.snow_water_equivalent_mm != yc.snow_water_equivalent_mm ||
            xc.surface_water_mm != yc.surface_water_mm ||
            xc.soil_water_mm != yc.soil_water_mm ||
            xc.groundwater_mm != yc.groundwater_mm ||
            xc.last_evapotranspiration_mm != yc.last_evapotranspiration_mm ||
            xc.last_quick_runoff_mm != yc.last_quick_runoff_mm ||
            xc.last_baseflow_mm != yc.last_baseflow_mm ||
            xc.last_routed_discharge_m3_s != yc.last_routed_discharge_m3_s) {
            return false;
        }
    }
    return true;
}

bool same_channels(
    const worldsim::ContinentalHydrologyResult& topology,
    const worldsim::MultiresolutionWaterState& a,
    const worldsim::MultiresolutionWaterState& b) {
    if (a.total_channel_storage_m3() != b.total_channel_storage_m3()) return false;
    for (const auto& cell : topology.cells) {
        if (a.channel_storage_m3(cell.coord) != b.channel_storage_m3(cell.coord)) return false;
    }
    return true;
}

bool same_vegetation_report(
    const worldsim::VegetationStepReport& a,
    const worldsim::VegetationStepReport& b) {
    return a.patch_count == b.patch_count &&
        a.land_cell_count == b.land_cell_count &&
        a.land_area_m2 == b.land_area_m2 &&
        a.biomass_area_before_m2 == b.biomass_area_before_m2 &&
        a.biomass_area_after_m2 == b.biomass_area_after_m2 &&
        a.disturbance_area_before_m2 == b.disturbance_area_before_m2 &&
        a.disturbance_area_after_m2 == b.disturbance_area_after_m2;
}

bool same_local_patch(
    const worldsim::World& a,
    const worldsim::World& b,
    worldsim::CellCoord coord) {
    const auto* x = a.find_local_patch(coord);
    const auto* y = b.find_local_patch(coord);
    if (!x || !y || x->regional_coord != y->regional_coord) return false;
    for (std::size_t i = 0; i < worldsim::kLocalCellCount; ++i) {
        const auto& xc = x->cells[i];
        const auto& yc = y->cells[i];
        if (xc.elevation_m != yc.elevation_m ||
            xc.terrain_roughness != yc.terrain_roughness ||
            xc.forest_potential != yc.forest_potential ||
            xc.disturbance != yc.disturbance ||
            xc.vegetation_biomass != yc.vegetation_biomass) {
            return false;
        }
    }
    return true;
}

bool same_all_local_patches(
    const worldsim::World& a,
    const worldsim::World& b,
    const std::vector<worldsim::CellCoord>& coords) {
    if (a.materialized_patch_count() != b.materialized_patch_count()) return false;
    for (const auto coord : coords) {
        if (!same_local_patch(a, b, coord)) return false;
    }
    return true;
}
} // namespace

int main() {
    using namespace worldsim;

    WorldConfig cfg;
    cfg.seed = 71;
    cfg.bounds = {0.0, 0.0, 5'010'000.0, 6'010'000.0};

    const auto create_begin = Clock::now();
    SimulationState simulation(cfg);
    const auto create_end = Clock::now();
    const auto l0_count = simulation.topology().cells.size();
    if (l0_count != 449'208 ||
        simulation.water().coarse_state().cells().size() != l0_count ||
        simulation.weather().cells().size() != l0_count) {
        throw std::runtime_error("Europe-scale simulation fixture no longer has 449208 aligned L0 cells");
    }
    if (simulation.world().materialized_patch_count() != 0) {
        throw std::runtime_error("simulation construction unexpectedly materialized L2 state");
    }

    std::vector<CellCoord> refined_parents;
    refined_parents.reserve(64);
    for (const auto& cell : simulation.topology().cells) {
        if (!cell.ocean) refined_parents.push_back(cell.coord);
        if (refined_parents.size() == 64) break;
    }
    if (refined_parents.size() != 64) {
        throw std::runtime_error("Europe-scale simulation fixture has fewer than 64 land cells");
    }

    const auto refine_begin = Clock::now();
    std::vector<CellCoord> vegetation_regions;
    vegetation_regions.reserve(refined_parents.size());
    for (const auto coord : refined_parents) {
        const auto& tile = simulation.materialize_refined_water_tile(coord);
        const auto active = std::find_if(
            tile.cells.begin(), tile.cells.end(), [](const DynamicHydrologyCellState& cell) {
                return cell.active;
            });
        if (active == tile.cells.end()) {
            throw std::runtime_error("refined benchmark parent has no active regional child");
        }
        vegetation_regions.push_back(active->coord);
    }
    const auto refine_end = Clock::now();
    if (simulation.water().refined_tile_count() != 64 ||
        vegetation_regions.size() != 64) {
        throw std::runtime_error("simulation benchmark did not retain 64 refined parents");
    }

    std::size_t vegetation_disturbed_cells = 0;
    for (const auto region : vegetation_regions) {
        const double s = static_cast<double>(cfg.regional_cell_m);
        const WorldPosition min{
            static_cast<double>(region.x) * s,
            static_cast<double>(region.y) * s};
        const WorldPosition max{min.x_m + s, min.y_m + s};
        vegetation_disturbed_cells += simulation.disturb_surface(min, max, 0.5f);
    }
    if (vegetation_disturbed_cells == 0 ||
        simulation.world().materialized_patch_count() != vegetation_regions.size()) {
        throw std::runtime_error(
            "simulation benchmark failed to create 64 sparse vegetation patches");
    }

    constexpr int kWarmupDays = 5;
    double max_relative_balance_error = 0.0;
    VegetationStepReport last_vegetation;
    const auto advance_begin = Clock::now();
    for (int day = 0; day < kWarmupDays; ++day) {
        const auto report = simulation.advance_day_full();
        if (report.environment.weather.day_after != day + 1 ||
            report.environment.water.day_after != day + 1 ||
            simulation.simulated_day() != day + 1) {
            throw std::runtime_error("simulation benchmark global clock diverged");
        }
        if (report.vegetation.patch_count != vegetation_regions.size() ||
            report.vegetation.land_cell_count == 0 ||
            !std::isfinite(report.vegetation.biomass_area_after_m2) ||
            !(report.vegetation.disturbance_area_after_m2 <
              report.vegetation.disturbance_area_before_m2)) {
            throw std::runtime_error("simulation benchmark vegetation lifecycle is invalid");
        }
        last_vegetation = report.vegetation;
        const double scale = std::max(
            1.0,
            std::abs(report.environment.water.storage_before_m3) +
                std::abs(report.environment.water.precipitation_m3) +
                std::abs(report.environment.water.evapotranspiration_m3) +
                std::abs(report.environment.water.terminal_outflow_m3) +
                std::abs(report.environment.water.storage_after_m3));
        const double relative_error =
            std::abs(report.environment.water.water_balance_error_m3) / scale;
        if (!std::isfinite(relative_error)) {
            throw std::runtime_error("simulation benchmark water balance is not finite");
        }
        max_relative_balance_error = std::max(max_relative_balance_error, relative_error);
    }
    const auto advance_end = Clock::now();
    if (max_relative_balance_error > 1.0e-6) {
        throw std::runtime_error("simulation Europe water balance exceeds tolerance");
    }
    const double warmup_channel_storage_m3 = simulation.water().total_channel_storage_m3();
    if (!(warmup_channel_storage_m3 > 0.0) || !std::isfinite(warmup_channel_storage_m3)) {
        throw std::runtime_error("simulation Europe fixture did not exercise persistent channel storage");
    }

    const auto checkpoint = std::filesystem::temp_directory_path() /
        "worldsim_europe_simulation_checkpoint.bin";
    const auto save_begin = Clock::now();
    simulation.save_checkpoint(checkpoint);
    const auto save_end = Clock::now();
    const auto checkpoint_bytes = std::filesystem::file_size(checkpoint);
    if (checkpoint_bytes == 0) {
        throw std::runtime_error("simulation benchmark produced an empty checkpoint");
    }

    const auto load_begin = Clock::now();
    auto loaded = SimulationState::load_checkpoint(checkpoint);
    const auto load_end = Clock::now();
    if (loaded.simulated_day() != simulation.simulated_day() ||
        loaded.topology().cells.size() != l0_count ||
        loaded.water().refined_tile_count() != 64 ||
        loaded.world().materialized_patch_count() != simulation.world().materialized_patch_count()) {
        throw std::runtime_error("simulation benchmark checkpoint ownership metadata did not round-trip");
    }
    if (!same_weather_cell(simulation.weather(), loaded.weather(), refined_parents.front()) ||
        !same_refined_tile(simulation.water(), loaded.water(), refined_parents.front()) ||
        !same_channels(simulation.topology(), simulation.water(), loaded.water()) ||
        !same_all_local_patches(simulation.world(), loaded.world(), vegetation_regions)) {
        throw std::runtime_error("simulation benchmark checkpoint state did not round-trip exactly");
    }

    const auto future_begin = Clock::now();
    const auto future_a = simulation.advance_day_full();
    const auto future_b = loaded.advance_day_full();
    const auto future_end = Clock::now();
    if (!same_report(future_a.environment, future_b.environment) ||
        !same_vegetation_report(future_a.vegetation, future_b.vegetation) ||
        !same_weather_cell(simulation.weather(), loaded.weather(), refined_parents.front()) ||
        !same_refined_tile(simulation.water(), loaded.water(), refined_parents.front()) ||
        !same_channels(simulation.topology(), simulation.water(), loaded.water()) ||
        !same_all_local_patches(simulation.world(), loaded.world(), vegetation_regions)) {
        throw std::runtime_error("simulation benchmark checkpoint changed deterministic future evolution");
    }

    std::filesystem::remove(checkpoint);

    std::cout << std::fixed << std::setprecision(3)
              << "benchmark_l0_cells=" << l0_count << '\n'
              << "benchmark_refined_tiles=64\n"
              << "benchmark_vegetation_patches=" << vegetation_regions.size() << '\n'
              << "benchmark_vegetation_disturbed_cells=" << vegetation_disturbed_cells << '\n'
              << "benchmark_warmup_days=" << kWarmupDays << '\n'
              << "simulation_create_ms=" << elapsed_ms(create_begin, create_end) << '\n'
              << "materialize_64_ms=" << elapsed_ms(refine_begin, refine_end) << '\n'
              << "advance_5_days_ms=" << elapsed_ms(advance_begin, advance_end) << '\n'
              << "checkpoint_save_ms=" << elapsed_ms(save_begin, save_end) << '\n'
              << "checkpoint_load_ms=" << elapsed_ms(load_begin, load_end) << '\n'
              << "future_pair_ms=" << elapsed_ms(future_begin, future_end) << '\n'
              << "checkpoint_bytes=" << checkpoint_bytes << '\n'
              << "warmup_channel_storage_m3=" << warmup_channel_storage_m3 << '\n'
              << "vegetation_land_cells=" << last_vegetation.land_cell_count << '\n'
              << "vegetation_biomass_area_after_m2=" << last_vegetation.biomass_area_after_m2 << '\n'
              << "vegetation_disturbance_area_after_m2=" << last_vegetation.disturbance_area_after_m2 << '\n'
              << "peak_rss_kib=" << peak_rss_kib() << '\n'
              << std::scientific
              << "max_relative_water_balance_error=" << max_relative_balance_error << '\n';
    return 0;
}
