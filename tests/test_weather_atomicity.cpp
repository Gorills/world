#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"

#include <cstring>
#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
} // namespace

int main() {
    using namespace worldsim;

    WorldConfig cfg;
    cfg.seed = 1501;
    cfg.bounds = {-64'000.0, -48'000.0, 128'000.0, 96'000.0};
    cfg.sea_level_m = -10'000.0f;
    World world(cfg);
    const auto topology = world.analyze_continental_hydrology({0.1f});

    auto weather = make_weather_state(world);
    DynamicHydrologyParameters valid_parameters;
    auto water = make_continental_water_state(world, topology, valid_parameters);

    const auto weather_before = weather.cells();
    const auto water_before = water.cells();
    const auto weather_day_before = weather.simulated_day();
    const auto water_day_before = water.simulated_day();

    auto invalid_parameters = valid_parameters;
    invalid_parameters.field_capacity_mm = invalid_parameters.soil_capacity_mm + 1.0f;

    bool rejected = false;
    try {
        (void)advance_weather_continental_water_day(weather, water, invalid_parameters);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    const bool weather_unchanged = weather.cells().size() == weather_before.size() &&
        std::memcmp(weather.cells().data(), weather_before.data(),
                    weather_before.size() * sizeof(WeatherCellState)) == 0;
    const bool water_unchanged = water.cells().size() == water_before.size() &&
        std::memcmp(water.cells().data(), water_before.data(),
                    water_before.size() * sizeof(ContinentalWaterCellState)) == 0;

    check(rejected, "coupled step propagates a water-parameter rejection");
    check(weather.simulated_day() == weather_day_before && weather_unchanged,
          "water rejection leaves prepared weather state and clock unchanged");
    check(water.simulated_day() == water_day_before && water_unchanged,
          "water rejection leaves continental water byte-for-byte unchanged");

    const auto report = advance_weather_continental_water_day(weather, water, valid_parameters);
    check(report.weather.day_before == 0 && report.weather.day_after == 1 &&
          report.water.day_before == 0 && report.water.day_after == 1 &&
          weather.simulated_day() == 1 && water.simulated_day() == 1,
          "same weather/water pair advances exactly one day after the rejected attempt");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "Weather/water atomicity regression passed\n";
    return 0;
}
