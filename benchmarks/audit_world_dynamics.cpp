#include "worldsim/simulation.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Standalone diagnostic; uses the existing simulation library without changing its model.
// c++ -std=c++20 -O2 -Iinclude benchmarks/audit_world_dynamics.cpp build-godot/libworldsim.a -o /tmp/worldsim_audit
// /tmp/worldsim_audit [seed=42] [days=3650] > audit.csv
namespace {
constexpr double kDaysPerYear = 365.2425; // Same seasonal year as WeatherState.
constexpr std::int64_t kMaxDays = 36500;

template<class T>
T integer_argument(std::string_view text, T maximum, const char* name) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || text.front() == '-' || error != std::errc{} ||
        end != text.data() + text.size() || value > maximum) {
        throw std::invalid_argument(std::string(name) + " must be an integer in [0, " +
            std::to_string(maximum) + "]");
    }
    return value;
}

double finite(double value) {
    if (!std::isfinite(value)) throw std::runtime_error("non-finite audit statistic");
    return value;
}

worldsim::CellCoord select_pristine_patch(const worldsim::World& world) {
    const auto& config = world.config();
    // This diagnostic deliberately retains WorldConfig's complete 128 x 128 km domain.
    const auto width = static_cast<std::int64_t>(config.bounds.width_m / config.regional_cell_m);
    const auto height = static_cast<std::int64_t>(config.bounds.height_m / config.regional_cell_m);
    for (std::int64_t y = 0; y < height; ++y) {
        for (std::int64_t x = 0; x < width; ++x) {
            const worldsim::CellCoord coord{x, y};
            const auto region = world.sample_region(coord);
            if (region.elevation_m <= config.sea_level_m || region.forest_potential <= 0.0f) continue;
            worldsim::World candidate(config);
            const auto& patch = candidate.materialize_local_patch(coord);
            if (std::any_of(patch.cells.begin(), patch.cells.end(), [&](const auto& cell) {
                return cell.elevation_m > config.sea_level_m && cell.vegetation_biomass > 0.0f;
            })) return coord;
        }
    }
    throw std::runtime_error("seed has no vegetated land patch in the default domain");
}

struct AuditHistory {
    double precipitation_m3{};
    double evapotranspiration_m3{};
    double terminal_outflow_m3{};
    double max_relative_water_error{};
    double max_relative_carbon_error{};
    double max_relative_nitrogen_error{};

    void add(const worldsim::SimulationDayReport& report) {
        const auto& w = report.environment.water;
        const auto& e = report.ecosystem;
        precipitation_m3 = finite(precipitation_m3 + w.precipitation_m3);
        evapotranspiration_m3 = finite(evapotranspiration_m3 + w.evapotranspiration_m3);
        terminal_outflow_m3 = finite(terminal_outflow_m3 + w.terminal_outflow_m3);
        max_relative_water_error = std::max(max_relative_water_error,
            finite(std::abs(w.water_balance_error_m3) /
                std::max(1.0, w.storage_before_m3 + w.precipitation_m3)));
        max_relative_carbon_error = std::max(max_relative_carbon_error,
            finite(std::abs(e.carbon_balance_error_kg) / std::max(1.0, e.carbon_before_kg)));
        max_relative_nitrogen_error = std::max(max_relative_nitrogen_error,
            finite(std::abs(e.nitrogen_balance_error_kg) / std::max(1.0, e.nitrogen_before_kg)));
    }
};

void write_row(const worldsim::SimulationState& simulation,
    const worldsim::LocalPatch& initial_patch, const AuditHistory& history) {
    const auto& ecosystem = simulation.ecosystem();
    const auto& water = simulation.water().coarse_state();
    double area = 0.0, grass = 0.0, shrubs = 0.0, trees = 0.0;
    double soil = 0.0, snow = 0.0, groundwater = 0.0, max_discharge = 0.0;
    for (std::size_t i = 0; i < ecosystem.cells().size(); ++i) {
        const double a = ecosystem.habitats().at(i).area_m2;
        const auto& e = ecosystem.cells()[i];
        const auto& w = water.cells().at(i);
        area += a;
        grass += a * e.grass_carbon;
        shrubs += a * e.shrub_carbon;
        trees += a * e.tree_carbon;
        soil += a * w.soil_water_mm;
        snow += a * w.snow_water_equivalent_mm;
        groundwater += a * w.groundwater_mm;
        max_discharge = std::max(max_discharge, finite(w.last_routed_discharge_m3_s));
    }
    if (!(area > 0.0)) throw std::runtime_error("seed has no L0 land area");
    const auto* patch = simulation.world().find_local_patch(initial_patch.regional_coord);
    if (!patch) throw std::runtime_error("audit's pristine patch disappeared");
    std::size_t land_cells = 0, identical_cells = 0;
    double cover = 0.0;
    for (std::size_t i = 0; i < patch->cells.size(); ++i) {
        if (patch->cells[i].elevation_m <= simulation.world().config().sea_level_m) continue;
        ++land_cells;
        identical_cells += patch->cells[i].vegetation_biomass == initial_patch.cells[i].vegetation_biomass;
        cover += patch->cells[i].vegetation_biomass;
    }
    if (land_cells == 0) throw std::runtime_error("audit's pristine patch has no land cells");
    const auto day = simulation.simulated_day();
    // Cumulative depth per elapsed year, over L0 land area. This includes day-zero spin-up.
    const double annualize = day == 0 ? 0.0 : 1000.0 * kDaysPerYear / (area * static_cast<double>(day));
    std::cout << simulation.world().config().seed << ',' << day << ',' << finite(area)
        << ',' << initial_patch.regional_coord.x << ',' << initial_patch.regional_coord.y;
    for (const double value : {
        grass / area, shrubs / area, trees / area,
        simulation.water().total_channel_storage_m3(), max_discharge,
        soil / area, snow / area, groundwater / area,
        history.precipitation_m3 * annualize,
        history.evapotranspiration_m3 * annualize,
        history.terminal_outflow_m3 * annualize,
        history.max_relative_water_error, history.max_relative_carbon_error,
        history.max_relative_nitrogen_error, cover / static_cast<double>(land_cells)}) {
        std::cout << ',' << finite(value);
    }
    std::cout << ',' << land_cells << ',' << identical_cells << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cerr << "Usage: " << argv[0] << " [seed=42] [days=3650, range 0..36500]\n"
                << "CSV: default WorldConfig, unrefined water, one pristine L2 patch.\n"
                << "Means use L0 land area; discharge is the last completed day's maximum.\n"
                << "Cumulative annualized fluxes include spin-up and use 365.2425 days/year.\n";
            return 0;
        }
        if (argc > 3) throw std::invalid_argument("usage: worldsim_audit [seed] [days]");
        worldsim::WorldConfig config;
        config.seed = argc > 1
            ? integer_argument<worldsim::Seed>(argv[1], std::numeric_limits<worldsim::Seed>::max(), "seed")
            : 42;
        const auto days = argc > 2 ? integer_argument<std::int64_t>(argv[2], kMaxDays, "days") : 3650;
        worldsim::World world(config);
        const auto patch_coord = select_pristine_patch(world);
        const auto initial_patch = world.materialize_local_patch(patch_coord);
        auto simulation = worldsim::SimulationState::from_world(std::move(world));
        AuditHistory history;
        std::cout.exceptions(std::ios::badbit | std::ios::failbit);
        std::cout << std::setprecision(12)
            << "seed,day,land_area_m2,patch_x,patch_y,grass_kgC_m2,shrub_kgC_m2,tree_kgC_m2,"
               "channel_m3,max_discharge_m3_s,soil_mm,snow_mm,groundwater_mm,"
               "cumulative_precip_mm_year,cumulative_ET_mm_year,cumulative_outflow_mm_year,"
               "max_relative_water_error,max_relative_C_error,max_relative_N_error,"
               "l2_mean_cover,l2_land_cells,l2_identical_land_cells\n";
        constexpr std::array<std::int64_t, 5> checkpoints{0, 30, 90, 365, 3650};
        for (std::int64_t day = 0; day <= days; ++day) {
            if (day == days || std::find(checkpoints.begin(), checkpoints.end(), day) != checkpoints.end()) {
                write_row(simulation, initial_patch, history);
            }
            if (day < days) history.add(simulation.advance_day_full());
        }
        std::cout.flush();
    } catch (const std::exception& error) {
        // cerr flushes its tied cout; a failed CSV sink must not throw again in this handler.
        std::cout.exceptions(std::ios::goodbit);
        std::cerr << "World dynamics audit failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
