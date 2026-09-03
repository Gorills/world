#pragma once

#include "worldsim/dynamic_hydrology.hpp"
#include "worldsim/types.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace worldsim::detail {

struct SoilBucketParameters {
    double soil_capacity_mm{};
    double field_capacity_mm{};
    double wilting_point_mm{};
    double infiltration_capacity_mm_per_day{};
};

inline SoilBucketParameters scaled_soil_bucket_parameters(
    const DynamicHydrologyParameters& parameters,
    const SoilProperties& soil) {
    if (!std::isfinite(soil.storage_capacity_scale) || !(soil.storage_capacity_scale > 0.0f) ||
        !std::isfinite(soil.infiltration_capacity_scale) || !(soil.infiltration_capacity_scale > 0.0f)) {
        throw std::invalid_argument("soil capacity scales must be finite and positive");
    }

    const double storage_scale = static_cast<double>(soil.storage_capacity_scale);
    const double infiltration_scale = static_cast<double>(soil.infiltration_capacity_scale);
    SoilBucketParameters out;
    out.soil_capacity_mm = static_cast<double>(parameters.soil_capacity_mm) * storage_scale;
    out.field_capacity_mm = static_cast<double>(parameters.field_capacity_mm) * storage_scale;
    out.wilting_point_mm = static_cast<double>(parameters.wilting_point_mm) * storage_scale;
    out.infiltration_capacity_mm_per_day =
        static_cast<double>(parameters.infiltration_capacity_mm_per_day) * infiltration_scale;
    if (!std::isfinite(out.soil_capacity_mm) || !std::isfinite(out.field_capacity_mm) ||
        !std::isfinite(out.wilting_point_mm) || !std::isfinite(out.infiltration_capacity_mm_per_day)) {
        throw std::invalid_argument("scaled soil bucket parameters exceed numerical range");
    }
    return out;
}

inline double scaled_initial_soil_water_mm(
    const DynamicHydrologyParameters& parameters,
    const SoilProperties& soil) {
    if (!std::isfinite(soil.storage_capacity_scale) || !(soil.storage_capacity_scale > 0.0f)) {
        throw std::invalid_argument("soil storage capacity scale must be finite and positive");
    }
    const double value = static_cast<double>(parameters.initial_soil_water_mm) *
                         static_cast<double>(soil.storage_capacity_scale);
    if (!std::isfinite(value)) {
        throw std::invalid_argument("scaled initial soil water exceeds numerical range");
    }
    return value;
}

inline double soil_capacity_tolerance_mm(double capacity_mm) {
    return std::max(1e-6, std::abs(capacity_mm) * 1e-6);
}

inline bool soil_water_within_capacity(float water_mm, double capacity_mm) {
    return std::isfinite(water_mm) && water_mm >= 0.0f &&
           static_cast<double>(water_mm) <= capacity_mm + soil_capacity_tolerance_mm(capacity_mm);
}

} // namespace worldsim::detail
