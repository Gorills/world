#pragma once

#include "worldsim/types.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace worldsim {

[[nodiscard]] inline std::int64_t floor_to_cell(double meters, std::int32_t cell_size_m) {
    if (!std::isfinite(meters)) {
        throw std::invalid_argument("world coordinate must be finite");
    }
    if (cell_size_m <= 0) {
        throw std::invalid_argument("cell size must be positive");
    }

    const long double scaled = static_cast<long double>(meters) /
                               static_cast<long double>(cell_size_m);
    const long double floored = std::floor(scaled);
    if (floored < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        floored > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        throw std::out_of_range("world coordinate exceeds grid coordinate range");
    }
    return static_cast<std::int64_t>(floored);
}

[[nodiscard]] inline CellCoord world_to_cell(WorldPosition p, std::int32_t cell_size_m) {
    return {floor_to_cell(p.x_m, cell_size_m), floor_to_cell(p.y_m, cell_size_m)};
}

[[nodiscard]] inline WorldPosition cell_center(CellCoord c, std::int32_t cell_size_m) {
    if (cell_size_m <= 0) {
        throw std::invalid_argument("cell size must be positive");
    }
    const auto s = static_cast<double>(cell_size_m);
    return {(static_cast<double>(c.x) + 0.5) * s,
            (static_cast<double>(c.y) + 0.5) * s};
}

[[nodiscard]] inline CellCoord regional_to_climate(CellCoord regional, const WorldConfig& cfg) {
    if (cfg.regional_cell_m <= 0 || cfg.climate_cell_m <= 0 ||
        cfg.climate_cell_m % cfg.regional_cell_m != 0) {
        throw std::invalid_argument("invalid regional/climate hierarchy");
    }
    const auto ratio = static_cast<std::int64_t>(cfg.climate_cell_m / cfg.regional_cell_m);
    const auto floor_div = [](std::int64_t a, std::int64_t b) {
        const std::int64_t q = a / b;
        const std::int64_t r = a % b;
        return q - ((r != 0 && ((r > 0) != (b > 0))) ? 1 : 0);
    };
    return {floor_div(regional.x, ratio), floor_div(regional.y, ratio)};
}

} // namespace worldsim
