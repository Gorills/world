#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>

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

struct Fixture {
    worldsim::Seed seed;
    worldsim::WorldBounds bounds;
};
} // namespace

int main() {
    using namespace worldsim;

    constexpr std::array<Fixture, 4> fixtures{{
        {17, {-91'337.0, -73'119.0, 151'777.0, 143'555.0}},
        {1401, {-80'123.0, -77'321.0, 160'777.0, 158'555.0}},
        {88'321, {13'579.0, -101'113.0, 173'333.0, 149'777.0}},
        {9'999'991, {-177'777.0, 41'321.0, 167'111.0, 181'555.0}},
    }};
    constexpr int kDays = 3650;
    constexpr double kPi = 3.14159265358979323846;

    double aggregate_actual_m3 = 0.0;
    double aggregate_climatological_m3 = 0.0;

    for (const auto& fixture : fixtures) {
        WorldConfig cfg;
        cfg.seed = fixture.seed;
        cfg.bounds = fixture.bounds;
        World world(cfg);
        auto weather = make_weather_state(world);

        double annual_climate_volume_m3 = 0.0;
        for (std::size_t i = 0; i < weather.cells().size(); ++i) {
            const auto coord = weather.coord_of(i);
            const auto climate = world.sample_climate(coord);
            const double area = overlap_area_m2(coord, cfg.climate_cell_m, cfg.bounds);
            annual_climate_volume_m3 +=
                static_cast<double>(climate.annual_precipitation_mm) * 0.001 * area;
        }
        check(annual_climate_volume_m3 > 0.0 && std::isfinite(annual_climate_volume_m3),
              "matrix fixture has finite positive climate precipitation");

        double actual_m3 = 0.0;
        double climatological_m3 = 0.0;
        double wet_fraction_sum = 0.0;
        double temperature_anomaly_sum = 0.0;
        std::size_t temperature_anomaly_count = 0;

        for (int day = 0; day < kDays; ++day) {
            const double day_of_year = std::fmod(static_cast<double>(day), 365.2425);
            const double phase = 2.0 * kPi * ((day_of_year - 200.0) / 365.2425);
            const double precip_weight = std::max(0.15, 1.0 + 0.30 * std::cos(phase - 0.7));
            climatological_m3 += annual_climate_volume_m3 / 365.2425 * precip_weight;

            const auto report = advance_weather_day(weather);
            actual_m3 += report.precipitation_m3;
            wet_fraction_sum += report.wet_area_fraction;
            for (const auto& cell : weather.cells()) {
                temperature_anomaly_sum += cell.temperature_anomaly_c;
                ++temperature_anomaly_count;
            }
        }

        const double ratio = actual_m3 / climatological_m3;
        const double wet_fraction = wet_fraction_sum / static_cast<double>(kDays);
        const double mean_temperature_anomaly =
            temperature_anomaly_sum / static_cast<double>(temperature_anomaly_count);

        check(ratio >= 0.80 && ratio <= 1.20,
              "each world identity remains reasonably anchored to climate precipitation");
        check(wet_fraction >= 0.20 && wet_fraction <= 0.85,
              "each world identity retains wet/dry intermittency");
        check(std::abs(mean_temperature_anomaly) < 1.2,
              "each world identity keeps transient temperature centered near climate");

        aggregate_actual_m3 += actual_m3;
        aggregate_climatological_m3 += climatological_m3;
        std::cout << "seed=" << fixture.seed << " precipitation_ratio=" << ratio
                  << " wet_fraction=" << wet_fraction
                  << " mean_temperature_anomaly=" << mean_temperature_anomaly << '\n';
    }

    const double aggregate_ratio = aggregate_actual_m3 / aggregate_climatological_m3;
    check(aggregate_ratio >= 0.90 && aggregate_ratio <= 1.10,
          "multi-world aggregate precipitation remains close to static climate baseline");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed; aggregate_precipitation_ratio="
                  << aggregate_ratio << '\n';
        return 1;
    }
    std::cout << "Weather climatology matrix passed; aggregate_precipitation_ratio="
              << aggregate_ratio << '\n';
    return 0;
}
