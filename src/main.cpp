#include "worldsim/world.hpp"
#include "worldsim/continental_water.hpp"
#include "worldsim/weather.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace {

void usage() {
    std::cout <<
        "WorldSim CLI v0.9\n"
        "Usage:\n"
        "  worldsim_cli demo [save.ws]\n"
        "  worldsim_cli inspect <save.ws> <x_m> <y_m>\n"
        "  worldsim_cli hydrology <save.ws> <min_region_x> <min_region_y> <width> <height> [river_threshold_m3_s]\n"
        "  worldsim_cli continent <save.ws> [river_threshold_m3_s]\n"
        "  worldsim_cli tile <save.ws> <climate_x> <climate_y> [river_threshold_m3_s]\n"
        "  worldsim_cli watercycle <save.ws> <climate_x> <climate_y> <days>\n"
        "  worldsim_cli continental-water <save.ws> <days>\n"
        "  worldsim_cli weather-water <save.ws> <days>\n";
}

void print_region(const worldsim::RegionalSample& s) {
    std::cout << std::fixed << std::setprecision(3)
              << "region=(" << s.coord.x << ',' << s.coord.y << ")\n"
              << "elevation_m=" << s.elevation_m << "\n"
              << "slope=" << s.slope << "\n"
              << "roughness=" << s.terrain_roughness << "\n"
              << "bedrock_hardness=" << s.bedrock_hardness << "\n"
              << "forest_potential=" << s.forest_potential << "\n";
}

void print_hydrology_summary(const worldsim::HydrologyResult& h) {
    double total_yield = 0.0;
    double total_outflow = 0.0;
    float max_discharge = 0.0f;
    float max_depression = 0.0f;
    std::size_t river_cells = 0;
    for (const auto& cell : h.cells) {
        total_yield += cell.local_water_yield_m3_s;
        if (!cell.has_downstream || cell.downstream_is_external) total_outflow += cell.accumulated_discharge_m3_s;
        max_discharge = std::max(max_discharge, cell.accumulated_discharge_m3_s);
        max_depression = std::max(max_depression, cell.depression_depth_m);
        if (cell.river) ++river_cells;
    }

    std::cout << std::fixed << std::setprecision(6)
              << "cells=" << h.cells.size() << "\n"
              << "lakes=" << h.lakes.size() << "\n"
              << "river_cells=" << river_cells << "\n"
              << "river_segments=" << h.river_segments.size() << "\n"
              << "total_local_water_yield_m3_s=" << total_yield << "\n"
              << "result_outflow_m3_s=" << total_outflow << "\n"
              << "max_accumulated_discharge_m3_s=" << max_discharge << "\n"
              << "max_depression_depth_m=" << max_depression << "\n";
}

void print_continental_summary(const worldsim::ContinentalHydrologyResult& h) {
    double total_yield = 0.0;
    double total_outflow = 0.0;
    float max_discharge = 0.0f;
    std::size_t ocean_cells = 0;
    std::size_t river_cells = 0;
    std::unordered_set<std::uint64_t> basins;
    for (const auto& cell : h.cells) {
        total_yield += cell.local_water_yield_m3_s;
        if (!cell.has_downstream) total_outflow += cell.accumulated_discharge_m3_s;
        max_discharge = std::max(max_discharge, cell.accumulated_discharge_m3_s);
        if (cell.ocean) ++ocean_cells;
        if (cell.river) ++river_cells;
        basins.insert(cell.basin_id);
    }
    std::cout << std::fixed << std::setprecision(6)
              << "climate_cells=" << h.cells.size() << "\n"
              << "ocean_cells=" << ocean_cells << "\n"
              << "basins=" << basins.size() << "\n"
              << "river_cells=" << river_cells << "\n"
              << "total_local_water_yield_m3_s=" << total_yield << "\n"
              << "terminal_outflow_m3_s=" << total_outflow << "\n"
              << "max_accumulated_discharge_m3_s=" << max_discharge << "\n";
}

std::uint32_t parse_u32(const char* text) {
    const auto value = std::stoull(text);
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range("value exceeds uint32 range");
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 0;
        }

        const std::string_view cmd = argv[1];
        if (cmd == "demo") {
            worldsim::WorldConfig cfg;
            cfg.seed = 42;
            cfg.bounds = {0.0, 0.0, 128'000.0, 128'000.0};
            worldsim::World world(cfg);

            std::cout << "Materialized patches before query: " << world.materialized_patch_count() << "\n";
            print_region(world.sample_region(worldsim::WorldPosition{42'000.0, 37'000.0}));
            std::cout << "Materialized patches after L1 query: " << world.materialized_patch_count() << "\n";

            const auto affected = world.disturb_surface({41'900.0, 36'900.0}, {42'220.0, 37'120.0}, 0.8f);
            std::cout << "Changed local cells: " << affected << "\n";
            std::cout << "Materialized patches after mutation: " << world.materialized_patch_count() << "\n";

            const std::filesystem::path save = argc >= 3 ? argv[2] : "demo.ws";
            world.save(save);
            std::cout << "Saved: " << save.string() << "\n";
            return 0;
        }

        if (cmd == "inspect" && argc == 5) {
            auto world = worldsim::World::load(argv[2]);
            const double x = std::stod(argv[3]);
            const double y = std::stod(argv[4]);
            print_region(world.sample_region(worldsim::WorldPosition{x, y}));
            std::cout << "materialized_patches=" << world.materialized_patch_count() << "\n";
            return 0;
        }

        if (cmd == "hydrology" && (argc == 7 || argc == 8)) {
            auto world = worldsim::World::load(argv[2]);
            worldsim::HydrologyRequest request;
            request.min_coord = {std::stoll(argv[3]), std::stoll(argv[4])};
            request.width_cells = parse_u32(argv[5]);
            request.height_cells = parse_u32(argv[6]);
            if (argc == 8) request.river_threshold_m3_s = std::stof(argv[7]);
            print_hydrology_summary(world.analyze_hydrology(request));
            return 0;
        }

        if (cmd == "continent" && (argc == 3 || argc == 4)) {
            auto world = worldsim::World::load(argv[2]);
            worldsim::ContinentalHydrologyRequest request;
            if (argc == 4) request.river_threshold_m3_s = std::stof(argv[3]);
            print_continental_summary(world.analyze_continental_hydrology(request));
            return 0;
        }

        if (cmd == "tile" && (argc == 5 || argc == 6)) {
            auto world = worldsim::World::load(argv[2]);
            auto continent = world.analyze_continental_hydrology();
            const worldsim::CellCoord climate{std::stoll(argv[3]), std::stoll(argv[4])};
            const float threshold = argc == 6 ? std::stof(argv[5]) : 0.5f;
            const auto tile = world.refine_authoritative_hydrology_tile(continent, climate, threshold, 0.25f);
            std::cout << "climate_cell=(" << climate.x << ',' << climate.y << ")\n";
            print_hydrology_summary(tile.hydrology);
            return 0;
        }

        if (cmd == "continental-water" && argc == 4) {
            auto world = worldsim::World::load(argv[2]);
            const int days = std::stoi(argv[3]);
            if (days <= 0 || days > 100'000) throw std::invalid_argument("days must be in [1, 100000]");
            const auto continent = world.analyze_continental_hydrology();
            auto water = worldsim::make_continental_water_state(world, continent);
            double precipitation_m3 = 0.0;
            double et_m3 = 0.0;
            double outflow_m3 = 0.0;
            double max_abs_balance_error_m3 = 0.0;
            double initial_storage_m3 = 0.0;
            double final_storage_m3 = 0.0;
            for (int day = 0; day < days; ++day) {
                const auto forcing = worldsim::make_smooth_continental_daily_forcing(water);
                const auto report = worldsim::advance_continental_water_day(water, forcing);
                if (day == 0) initial_storage_m3 = report.storage_before_m3;
                final_storage_m3 = report.storage_after_m3;
                precipitation_m3 += report.precipitation_m3;
                et_m3 += report.evapotranspiration_m3;
                outflow_m3 += report.terminal_outflow_m3;
                max_abs_balance_error_m3 = std::max(
                    max_abs_balance_error_m3, std::abs(report.water_balance_error_m3));
            }
            std::cout << std::fixed << std::setprecision(6)
                      << "l0_cells=" << water.cells().size() << "\n"
                      << "simulated_day=" << water.simulated_day() << "\n"
                      << "precipitation_m3=" << precipitation_m3 << "\n"
                      << "evapotranspiration_m3=" << et_m3 << "\n"
                      << "terminal_outflow_m3=" << outflow_m3 << "\n"
                      << "storage_initial_m3=" << initial_storage_m3 << "\n"
                      << "storage_final_m3=" << final_storage_m3 << "\n"
                      << "max_abs_daily_balance_error_m3=" << max_abs_balance_error_m3 << "\n";
            return 0;
        }

        if (cmd == "weather-water" && argc == 4) {
            auto world = worldsim::World::load(argv[2]);
            const int days = std::stoi(argv[3]);
            if (days <= 0 || days > 100'000) throw std::invalid_argument("days must be in [1, 100000]");
            const auto continent = world.analyze_continental_hydrology();
            auto weather = worldsim::make_weather_state(world);
            worldsim::DynamicHydrologyParameters water_parameters;
            auto water = worldsim::make_continental_water_state(world, continent, water_parameters);

            double precipitation_m3 = 0.0;
            double et_m3 = 0.0;
            double outflow_m3 = 0.0;
            double wet_fraction_sum = 0.0;
            double max_abs_balance_error_m3 = 0.0;
            double initial_storage_m3 = 0.0;
            double final_storage_m3 = 0.0;
            for (int day = 0; day < days; ++day) {
                const auto report = worldsim::advance_weather_continental_water_day(
                    weather, water, water_parameters);
                if (day == 0) initial_storage_m3 = report.water.storage_before_m3;
                final_storage_m3 = report.water.storage_after_m3;
                precipitation_m3 += report.water.precipitation_m3;
                et_m3 += report.water.evapotranspiration_m3;
                outflow_m3 += report.water.terminal_outflow_m3;
                wet_fraction_sum += report.weather.wet_area_fraction;
                max_abs_balance_error_m3 = std::max(
                    max_abs_balance_error_m3, std::abs(report.water.water_balance_error_m3));
            }
            std::cout << std::fixed << std::setprecision(6)
                      << "l0_cells=" << water.cells().size() << "\n"
                      << "simulated_day=" << water.simulated_day() << "\n"
                      << "weather_day=" << weather.simulated_day() << "\n"
                      << "mean_wet_area_fraction=" << wet_fraction_sum / static_cast<double>(days) << "\n"
                      << "precipitation_m3=" << precipitation_m3 << "\n"
                      << "evapotranspiration_m3=" << et_m3 << "\n"
                      << "terminal_outflow_m3=" << outflow_m3 << "\n"
                      << "storage_initial_m3=" << initial_storage_m3 << "\n"
                      << "storage_final_m3=" << final_storage_m3 << "\n"
                      << "max_abs_daily_balance_error_m3=" << max_abs_balance_error_m3 << "\n";
            return 0;
        }

        if (cmd == "watercycle" && argc == 6) {
            auto world = worldsim::World::load(argv[2]);
            auto continent = world.analyze_continental_hydrology();
            const worldsim::CellCoord climate{std::stoll(argv[3]), std::stoll(argv[4])};
            const int days = std::stoi(argv[5]);
            if (days <= 0 || days > 100'000) throw std::invalid_argument("days must be in [1, 100000]");
            const auto tile = world.refine_authoritative_hydrology_tile(continent, climate, 0.5f, 0.25f);
            worldsim::DynamicHydrologyParameters params;
            auto state = worldsim::make_dynamic_hydrology_tile_state(world, tile, params);

            double precipitation_m3 = 0.0;
            double et_m3 = 0.0;
            double outflow_m3 = 0.0;
            double max_abs_balance_error_m3 = 0.0;
            double initial_storage_m3 = 0.0;
            double final_storage_m3 = 0.0;
            for (int day = 0; day < days; ++day) {
                const double doy = std::fmod(static_cast<double>(day), 365.2425);
                const auto forcing = worldsim::make_smooth_climatological_forcing(world, tile, doy, 1.0);
                const auto report = worldsim::advance_dynamic_hydrology_tile(
                    world, tile, state, forcing, {}, 1.0, params);
                if (day == 0) initial_storage_m3 = report.storage_before_m3;
                final_storage_m3 = report.storage_after_m3;
                precipitation_m3 += report.precipitation_m3;
                et_m3 += report.evapotranspiration_m3;
                outflow_m3 += report.external_outflow_m3;
                max_abs_balance_error_m3 = std::max(max_abs_balance_error_m3, std::abs(report.water_balance_error_m3));
            }

            double snow_mm = 0.0;
            double surface_mm = 0.0;
            double soil_mm = 0.0;
            double groundwater_mm = 0.0;
            std::size_t active = 0;
            for (const auto& cell : state.cells) {
                if (!cell.active) continue;
                ++active;
                snow_mm += cell.snow_water_equivalent_mm;
                surface_mm += cell.surface_water_mm;
                soil_mm += cell.soil_water_mm;
                groundwater_mm += cell.groundwater_mm;
            }
            const double denom = active == 0 ? 1.0 : static_cast<double>(active);
            std::cout << std::fixed << std::setprecision(6)
                      << "climate_cell=(" << climate.x << ',' << climate.y << ")\n"
                      << "simulated_days=" << state.simulated_days << "\n"
                      << "precipitation_m3=" << precipitation_m3 << "\n"
                      << "evapotranspiration_m3=" << et_m3 << "\n"
                      << "external_outflow_m3=" << outflow_m3 << "\n"
                      << "storage_initial_m3=" << initial_storage_m3 << "\n"
                      << "storage_final_m3=" << final_storage_m3 << "\n"
                      << "mean_snow_water_equivalent_mm=" << snow_mm / denom << "\n"
                      << "mean_surface_water_mm=" << surface_mm / denom << "\n"
                      << "mean_soil_water_mm=" << soil_mm / denom << "\n"
                      << "mean_groundwater_mm=" << groundwater_mm / denom << "\n"
                      << "max_abs_daily_balance_error_m3=" << max_abs_balance_error_m3 << "\n";
            return 0;
        }

        usage();
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
