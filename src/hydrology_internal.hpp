#pragma once

#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>

namespace worldsim::detail {

inline constexpr double kSecondsPerYear = 365.2425 * 24.0 * 60.0 * 60.0;

inline double turc_aet_mm(double precipitation_mm, double mean_temperature_c) {
    const double t = std::max(0.0, mean_temperature_c);
    const double l = 300.0 + 25.0 * t + 0.05 * t * t * t;
    const double ratio = precipitation_mm / l;
    if (ratio <= 0.316) return precipitation_mm;
    const double aet = precipitation_mm / std::sqrt(0.9 + ratio * ratio);
    return std::clamp(aet, 0.0, precipitation_mm);
}

inline double annual_water_surplus_mm(const ClimateSample& climate, double elevation_m) {
    const double p = static_cast<double>(climate.annual_precipitation_mm);
    const double effective_temperature_c = static_cast<double>(climate.mean_temperature_c) -
                                           0.0065 * std::max(0.0, elevation_m);
    const double aet = turc_aet_mm(p, effective_temperature_c);
    return std::max(0.0, p - aet);
}

inline double water_yield_m3_s(double surplus_mm, double area_m2) {
    return (surplus_mm / 1000.0) * area_m2 / kSecondsPerYear;
}

inline double local_water_yield_m3_s(const World& world, CellCoord regional_coord, double elevation_m) {
    const auto climate = world.sample_climate(regional_to_climate(regional_coord, world.config()));
    const double surplus_mm = annual_water_surplus_mm(climate, elevation_m);
    const double cell_m = static_cast<double>(world.config().regional_cell_m);
    return water_yield_m3_s(surplus_mm, cell_m * cell_m);
}

} // namespace worldsim::detail
