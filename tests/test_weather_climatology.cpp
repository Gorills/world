#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

double overlap_area_m2(worldsim::CellCoord coord, std::int32_t cell_m,
                       const worldsim::WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) return 0.0;
    return (x1 - x0) * (y1 - y0);
}
} // namespace

int main() {
    using namespace worldsim;

    WorldConfig cfg;
    cfg.seed = 1401;
    cfg.bounds = {-80'123.0, -77'321.0, 160'777.0, 158'555.0};
    World world(cfg);
    auto weather = make_weather_state(world);

    double annual_climate_volume_m3 = 0.0;
    for (std::size_t i = 0; i < weather.cells().size(); ++i) {
        const auto coord = weather.coord_of(i);
        const auto climate = world.sample_climate(coord);
        const double area = overlap_area_m2(coord, cfg.climate_cell_m, cfg.bounds);
        annual_climate_volume_m3 += static_cast<double>(climate.annual_precipitation_mm) * 0.001 * area;
    }
    check(annual_climate_volume_m3 > 0.0 && std::isfinite(annual_climate_volume_m3),
          "climatology fixture has finite positive annual precipitation volume");

    constexpr int kDays = 3650;
    constexpr double kPi = 3.14159265358979323846;
    double actual_precipitation_m3 = 0.0;
    double climatological_precipitation_m3 = 0.0;
    double wet_fraction_sum = 0.0;
    double anomaly_sum = 0.0;
    std::size_t anomaly_count = 0;

    for (int day = 0; day < kDays; ++day) {
        const double day_of_year = std::fmod(static_cast<double>(day), 365.2425);
        const double phase = 2.0 * kPi * ((day_of_year - 200.0) / 365.2425);
        const double precip_weight = std::max(0.15, 1.0 + 0.30 * std::cos(phase - 0.7));
        climatological_precipitation_m3 +=
            annual_climate_volume_m3 / 365.2425 * precip_weight;

        const auto report = advance_weather_day(weather);
        actual_precipitation_m3 += report.precipitation_m3;
        wet_fraction_sum += report.wet_area_fraction;
        for (const auto& cell : weather.cells()) {
            anomaly_sum += cell.temperature_anomaly_c;
            ++anomaly_count;
        }
    }

    const double precipitation_ratio = actual_precipitation_m3 / climatological_precipitation_m3;
    const double mean_wet_fraction = wet_fraction_sum / static_cast<double>(kDays);
    const double mean_temperature_anomaly = anomaly_sum / static_cast<double>(anomaly_count);
    check(precipitation_ratio >= 0.80 && precipitation_ratio <= 1.20,
          "long-run weather precipitation stays anchored to static climate baseline");
    check(mean_wet_fraction >= 0.25 && mean_wet_fraction <= 0.80,
          "long-run weather retains meaningful wet/dry intermittency");
    check(std::abs(mean_temperature_anomaly) < 1.0,
          "long-run transient temperature anomaly remains centered near climate baseline");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed; precipitation_ratio=" << precipitation_ratio
                  << " mean_wet_fraction=" << mean_wet_fraction
                  << " mean_temperature_anomaly=" << mean_temperature_anomaly << '\n';
        return 1;
    }
    std::cout << "Weather climatology regression passed; precipitation_ratio=" << precipitation_ratio
              << " mean_wet_fraction=" << mean_wet_fraction
              << " mean_temperature_anomaly=" << mean_temperature_anomaly << '\n';
    return 0;
}
