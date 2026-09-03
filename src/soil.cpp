#include "worldsim/world.hpp"

#include "worldsim/coordinates.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace worldsim {
namespace {

constexpr std::uint64_t kStorageParentSalt = 0x50115A01ULL;
constexpr std::uint64_t kStorageChildSalt = 0x50115A02ULL;
constexpr std::uint64_t kInfiltrationParentSalt = 0x1AF11701ULL;
constexpr std::uint64_t kInfiltrationChildSalt = 0x1AF11702ULL;

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}

std::uint64_t coord_hash(Seed seed, CellCoord c, std::uint64_t salt) {
    const auto ux = std::bit_cast<std::uint64_t>(c.x);
    const auto uy = std::bit_cast<std::uint64_t>(c.y);
    return mix64(seed ^ mix64(ux + salt) ^ std::rotl(mix64(uy ^ salt), 29));
}

double hash_unit(Seed seed, CellCoord c, std::uint64_t salt) {
    const std::uint64_t h = coord_hash(seed, c, salt);
    return static_cast<double>(h >> 11U) * (1.0 / 9007199254740992.0);
}

double ranged_hash(Seed seed, CellCoord c, std::uint64_t salt, double low, double high) {
    return low + (high - low) * hash_unit(seed, c, salt);
}

double overlap_area_m2(CellCoord coord, std::int32_t cell_m, const WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) return 0.0;
    return (x1 - x0) * (y1 - y0);
}

CellCoord regional_origin(CellCoord climate_coord, std::int64_t ratio) {
    if (ratio <= 0 ||
        climate_coord.x > std::numeric_limits<std::int64_t>::max() / ratio ||
        climate_coord.x < std::numeric_limits<std::int64_t>::min() / ratio ||
        climate_coord.y > std::numeric_limits<std::int64_t>::max() / ratio ||
        climate_coord.y < std::numeric_limits<std::int64_t>::min() / ratio) {
        throw std::out_of_range("soil parent coordinate is not representable at regional resolution");
    }
    return {climate_coord.x * ratio, climate_coord.y * ratio};
}

} // namespace

SoilProperties World::sample_climate_soil(CellCoord climate_coord) const {
    require_climate_in_bounds(climate_coord);

    SoilProperties out;
    // Parent-scale variation stays intentionally moderate. These are modifiers for configurable
    // hydrology reference parameters, not absolute pedological measurements.
    out.storage_capacity_scale = static_cast<float>(
        ranged_hash(config_.seed, climate_coord, kStorageParentSalt, 0.85, 1.15));
    out.infiltration_capacity_scale = static_cast<float>(
        ranged_hash(config_.seed, climate_coord, kInfiltrationParentSalt, 0.75, 1.25));
    return out;
}

SoilProperties World::sample_soil(CellCoord regional_coord) const {
    require_region_in_bounds(regional_coord);

    const auto climate_coord = regional_to_climate(regional_coord, config_);
    const auto parent = sample_climate_soil(climate_coord);
    const auto ratio = static_cast<std::int64_t>(config_.climate_cell_m / config_.regional_cell_m);
    if (ratio != 8) throw std::logic_error("spatial soil field requires the fixed 8x8 L1 hierarchy");
    const auto origin = regional_origin(climate_coord, ratio);

    double total_area = 0.0;
    double storage_weighted = 0.0;
    double infiltration_weighted = 0.0;
    double target_storage_raw = 0.0;
    double target_infiltration_raw = 0.0;
    bool found_target = false;

    for (std::int64_t y = 0; y < ratio; ++y) {
        for (std::int64_t x = 0; x < ratio; ++x) {
            const CellCoord child{origin.x + x, origin.y + y};
            const double area = overlap_area_m2(child, config_.regional_cell_m, config_.bounds);
            if (!(area > 0.0)) continue;

            const double storage_raw = ranged_hash(
                config_.seed, child, kStorageChildSalt, 0.80, 1.20);
            const double infiltration_raw = ranged_hash(
                config_.seed, child, kInfiltrationChildSalt, 0.75, 1.25);
            total_area += area;
            storage_weighted += storage_raw * area;
            infiltration_weighted += infiltration_raw * area;

            if (child == regional_coord) {
                target_storage_raw = storage_raw;
                target_infiltration_raw = infiltration_raw;
                found_target = true;
            }
        }
    }

    if (!found_target || !(total_area > 0.0) || !(storage_weighted > 0.0) ||
        !(infiltration_weighted > 0.0)) {
        throw std::logic_error("regional soil sample could not reconstruct its parent overlap");
    }

    const double storage_mean = storage_weighted / total_area;
    const double infiltration_mean = infiltration_weighted / total_area;

    SoilProperties out;
    out.storage_capacity_scale = static_cast<float>(
        static_cast<double>(parent.storage_capacity_scale) * target_storage_raw / storage_mean);
    out.infiltration_capacity_scale = static_cast<float>(
        static_cast<double>(parent.infiltration_capacity_scale) * target_infiltration_raw / infiltration_mean);
    if (!std::isfinite(out.storage_capacity_scale) || !(out.storage_capacity_scale > 0.0f) ||
        !std::isfinite(out.infiltration_capacity_scale) || !(out.infiltration_capacity_scale > 0.0f)) {
        throw std::logic_error("derived regional soil properties are invalid");
    }
    return out;
}

SoilProperties World::sample_soil(WorldPosition position) const {
    if (!config_.bounds.contains(position)) {
        throw std::out_of_range("world position is outside configured bounds");
    }
    return sample_soil(world_to_cell(position, config_.regional_cell_m));
}

} // namespace worldsim
