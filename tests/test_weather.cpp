#include "worldsim/multiresolution_water.hpp"
#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

worldsim::WorldConfig config(worldsim::Seed seed = 1201) {
    worldsim::WorldConfig cfg;
    cfg.seed = seed;
    cfg.bounds = {-121'333.0, -101'777.0, 241'111.0, 201'555.0};
    cfg.sea_level_m = -10'000.0f;
    return cfg;
}

bool same_weather_cell(const worldsim::WeatherCellState& a,
                       const worldsim::WeatherCellState& b) {
    return a.temperature_anomaly_c == b.temperature_anomaly_c &&
           a.moisture_anomaly == b.moisture_anomaly;
}

bool same_forcing(const worldsim::ContinentalWaterForcing& a,
                  const worldsim::ContinentalWaterForcing& b) {
    return a.precipitation_mm == b.precipitation_mm &&
           a.mean_air_temperature_c == b.mean_air_temperature_c &&
           a.potential_evapotranspiration_mm == b.potential_evapotranspiration_mm;
}

std::vector<char> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read weather test file");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write weather test file");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("failed to write weather test file");
}

} // namespace

int main() {
    using namespace worldsim;

    World world(config());
    const auto topology = world.analyze_continental_hydrology({0.1f});
    auto weather = make_weather_state(world);
    auto twin = make_weather_state(world);

    check(weather.simulated_day() == 0, "weather clock starts at day zero");
    check(weather.cells().size() == topology.cells.size() &&
          weather.min_coord() == topology.min_coord &&
          weather.width_cells() == topology.width_cells &&
          weather.height_cells() == topology.height_cells,
          "weather raster exactly aligns with authoritative L0 world raster");
    check(world.materialized_patch_count() == 0,
          "creating whole-world weather does not materialize L2 state");

    bool deterministic_initial = weather.cells().size() == twin.cells().size();
    for (std::size_t i = 0; deterministic_initial && i < weather.cells().size(); ++i) {
        deterministic_initial = same_weather_cell(weather.cells()[i], twin.cells()[i]);
    }
    check(deterministic_initial, "identical world/parameters create identical weather state");

    const auto initial_forcing = make_weather_daily_forcing(weather);
    const auto twin_forcing = make_weather_daily_forcing(twin);
    bool valid_forcing = initial_forcing.size() == weather.cells().size();
    bool forcing_equal = initial_forcing.size() == twin_forcing.size();
    bool any_wet = false;
    bool any_dry = false;
    for (std::size_t i = 0; i < initial_forcing.size(); ++i) {
        const auto& f = initial_forcing[i];
        valid_forcing = valid_forcing && std::isfinite(f.precipitation_mm) && f.precipitation_mm >= 0.0f &&
            std::isfinite(f.mean_air_temperature_c) &&
            std::isfinite(f.potential_evapotranspiration_mm) && f.potential_evapotranspiration_mm >= 0.0f;
        any_wet = any_wet || f.precipitation_mm > 0.0f;
        any_dry = any_dry || f.precipitation_mm == 0.0f;
        forcing_equal = forcing_equal && same_forcing(f, twin_forcing[i]);
    }
    check(valid_forcing, "weather forcing is finite and hydrology-safe");
    check(any_wet && any_dry, "synoptic weather creates simultaneous wet and dry L0 areas");
    check(forcing_equal, "weather forcing is deterministic for identical state");

    double adjacent_difference = 0.0;
    double distant_difference = 0.0;
    std::size_t adjacent_pairs = 0;
    std::size_t distant_pairs = 0;
    const auto width = static_cast<std::size_t>(weather.width_cells());
    const auto height = static_cast<std::size_t>(weather.height_cells());
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto i = y * width + x;
            if (x + 1 < width) {
                adjacent_difference += std::abs(
                    static_cast<double>(weather.cells()[i].temperature_anomaly_c) -
                    weather.cells()[i + 1].temperature_anomaly_c);
                ++adjacent_pairs;
            }
            if (x + 8 < width) {
                distant_difference += std::abs(
                    static_cast<double>(weather.cells()[i].temperature_anomaly_c) -
                    weather.cells()[i + 8].temperature_anomaly_c);
                ++distant_pairs;
            }
        }
    }
    check(adjacent_pairs > 0 && distant_pairs > 0 &&
          adjacent_difference / static_cast<double>(adjacent_pairs) <
              distant_difference / static_cast<double>(distant_pairs),
          "weather anomalies are spatially coherent rather than cell-wise white noise");

    bool temporal_change = false;
    double min_probe_temperature = std::numeric_limits<double>::infinity();
    double max_probe_temperature = -std::numeric_limits<double>::infinity();
    const auto probe = topology.cells[topology.cells.size() / 2].coord;
    for (int day = 0; day < 365; ++day) {
        const auto before = weather.cells();
        const auto report = advance_weather_day(weather);
        const auto twin_report = advance_weather_day(twin);
        check(report.day_before == day && report.day_after == day + 1 &&
              twin_report.day_before == report.day_before && twin_report.day_after == report.day_after,
              "weather advances one exact global day");
        check(report.wet_area_fraction >= 0.0 && report.wet_area_fraction <= 1.0 &&
              std::isfinite(report.precipitation_m3) && report.precipitation_m3 >= 0.0 &&
              std::isfinite(report.mean_air_temperature_c) &&
              std::isfinite(report.mean_potential_evapotranspiration_mm),
              "weather daily report remains finite and bounded");
        for (std::size_t i = 0; i < weather.cells().size(); ++i) {
            temporal_change = temporal_change || !same_weather_cell(before[i], weather.cells()[i]);
            check(same_weather_cell(weather.cells()[i], twin.cells()[i]),
                  "weather evolution stays deterministic cell-for-cell");
        }
        const auto sample = sample_weather(weather, probe);
        min_probe_temperature = std::min(min_probe_temperature,
                                         static_cast<double>(sample.mean_air_temperature_c));
        max_probe_temperature = std::max(max_probe_temperature,
                                         static_cast<double>(sample.mean_air_temperature_c));
    }
    check(temporal_change, "weather has transient day-to-day state rather than repeating climatology");
    check(max_probe_temperature - min_probe_temperature > 10.0,
          "weather retains a substantial seasonal temperature cycle");
    check(world.materialized_patch_count() == 0,
          "365 whole-world weather days do not materialize L1/L2 state");

    bool extreme_index_rejected = false;
    try {
        (void)weather.index_of({std::numeric_limits<std::int64_t>::max(),
                                std::numeric_limits<std::int64_t>::max()});
    } catch (const std::out_of_range&) {
        extreme_index_rejected = true;
    }
    check(extreme_index_rejected, "weather index rejects extreme out-of-range coordinates");

    bool invalid_parameters_rejected = false;
    try {
        auto invalid = WeatherParameters{};
        invalid.temperature_memory = 1.0f;
        (void)make_weather_state(world, invalid);
    } catch (const std::invalid_argument&) {
        invalid_parameters_rejected = true;
    }
    check(invalid_parameters_rejected, "weather rejects unstable parameter ranges");

    World coupled_world(config(1202));
    const auto coupled_topology = coupled_world.analyze_continental_hydrology({0.1f});
    auto coupled_weather = make_weather_state(coupled_world);
    auto coupled_water = make_multiresolution_water_state(coupled_world, coupled_topology);
    if (!coupled_topology.cells.empty()) {
        (void)materialize_refined_water_tile(
            coupled_world, coupled_topology, coupled_water, coupled_topology.cells.front().coord);
    }
    double max_relative_balance = 0.0;
    for (int day = 0; day < 60; ++day) {
        const auto report = advance_weather_multiresolution_water_day(
            coupled_world, coupled_weather, coupled_water);
        check(report.weather.day_before == day && report.weather.day_after == day + 1 &&
              report.water.day_before == day && report.water.day_after == day + 1 &&
              coupled_weather.simulated_day() == coupled_water.simulated_day(),
              "coupled weather/multiresolution water keeps one exact day");
        const double scale = std::max(1.0,
            std::abs(report.water.storage_before_m3) + std::abs(report.water.precipitation_m3));
        max_relative_balance = std::max(max_relative_balance,
            std::abs(report.water.water_balance_error_m3) / scale);
    }
    check(max_relative_balance < 1e-6,
          "weather-driven multiresolution water preserves the existing conservation contract");

    auto mismatched_weather = make_weather_state(coupled_world);
    auto mismatched_water = make_multiresolution_water_state(coupled_world, coupled_topology);
    (void)advance_weather_day(mismatched_weather);
    const auto water_before_day = mismatched_water.simulated_day();
    const auto weather_before_day = mismatched_weather.simulated_day();
    bool mismatch_rejected = false;
    try {
        (void)advance_weather_multiresolution_water_day(
            coupled_world, mismatched_weather, mismatched_water);
    } catch (const std::invalid_argument&) {
        mismatch_rejected = true;
    }
    check(mismatch_rejected && mismatched_water.simulated_day() == water_before_day &&
          mismatched_weather.simulated_day() == weather_before_day,
          "clock mismatch is rejected without mutating either subsystem");

    const auto root = std::filesystem::temp_directory_path() / "worldsim_weather_test";
    const auto valid_path = root.string() + ".bin";
    const auto truncated_path = root.string() + ".truncated.bin";
    const auto trailing_path = root.string() + ".trailing.bin";
    save_weather_state(coupled_weather, valid_path);
    auto loaded = load_weather_state(coupled_world, valid_path);
    check(loaded.simulated_day() == coupled_weather.simulated_day() &&
          loaded.cells().size() == coupled_weather.cells().size(),
          "weather persistence preserves exact clock and raster size");
    bool persisted_equal = loaded.cells().size() == coupled_weather.cells().size();
    for (std::size_t i = 0; persisted_equal && i < loaded.cells().size(); ++i) {
        persisted_equal = same_weather_cell(loaded.cells()[i], coupled_weather.cells()[i]);
    }
    check(persisted_equal, "weather persistence preserves transient anomalies exactly");
    const auto loaded_forcing = make_weather_daily_forcing(loaded);
    const auto original_forcing = make_weather_daily_forcing(coupled_weather);
    bool future_equal = loaded_forcing.size() == original_forcing.size();
    for (std::size_t i = 0; future_equal && i < loaded_forcing.size(); ++i) {
        future_equal = same_forcing(loaded_forcing[i], original_forcing[i]);
    }
    check(future_equal, "reloaded weather produces exactly the same next-day forcing");

    const auto bytes = read_bytes(valid_path);
    auto truncated = bytes;
    truncated.resize(truncated.size() / 2);
    write_bytes(truncated_path, truncated);
    bool truncated_rejected = false;
    try {
        (void)load_weather_state(coupled_world, truncated_path);
    } catch (const std::runtime_error&) {
        truncated_rejected = true;
    }
    check(truncated_rejected, "truncated weather persistence is rejected");

    auto trailing = bytes;
    trailing.push_back(static_cast<char>(0x5a));
    write_bytes(trailing_path, trailing);
    bool trailing_rejected = false;
    try {
        (void)load_weather_state(coupled_world, trailing_path);
    } catch (const std::runtime_error&) {
        trailing_rejected = true;
    }
    check(trailing_rejected, "unexpected weather persistence tail is rejected");

    World wrong_world(config(1203));
    bool wrong_world_rejected = false;
    try {
        (void)load_weather_state(wrong_world, valid_path);
    } catch (const std::runtime_error&) {
        wrong_world_rejected = true;
    }
    check(wrong_world_rejected, "weather persistence rejects a different world identity");

    std::filesystem::remove(valid_path);
    std::filesystem::remove(truncated_path);
    std::filesystem::remove(trailing_path);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All weather tests passed; max_relative_water_balance_error="
              << max_relative_balance << '\n';
    return 0;
}
