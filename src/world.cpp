#include "worldsim/world.hpp"
#include "worldsim/persistence.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace worldsim {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}

std::uint64_t coord_hash(Seed seed, std::int64_t x, std::int64_t y, std::uint64_t salt = 0) {
    const auto ux = std::bit_cast<std::uint64_t>(x);
    const auto uy = std::bit_cast<std::uint64_t>(y);
    return mix64(seed ^ mix64(ux + salt) ^ std::rotl(mix64(uy ^ salt), 29));
}

double hash_unit(Seed seed, std::int64_t x, std::int64_t y, std::uint64_t salt = 0) {
    const std::uint64_t h = coord_hash(seed, x, y, salt);
    return static_cast<double>(h >> 11U) * (1.0 / 9007199254740992.0);
}

double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double value_noise(Seed seed, double x, double y, std::uint64_t salt) {
    const auto x0 = static_cast<std::int64_t>(std::floor(x));
    const auto y0 = static_cast<std::int64_t>(std::floor(y));
    const auto x1 = x0 + 1;
    const auto y1 = y0 + 1;
    const double tx = smoothstep(x - static_cast<double>(x0));
    const double ty = smoothstep(y - static_cast<double>(y0));

    const double a = hash_unit(seed, x0, y0, salt);
    const double b = hash_unit(seed, x1, y0, salt);
    const double c = hash_unit(seed, x0, y1, salt);
    const double d = hash_unit(seed, x1, y1, salt);
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty) * 2.0 - 1.0;
}

double fbm(Seed seed, double x, double y, std::uint64_t salt, int octaves) {
    double sum = 0.0;
    double amplitude = 0.5;
    double frequency = 1.0;
    double norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * value_noise(seed, x * frequency, y * frequency, salt + static_cast<std::uint64_t>(i) * 0x10001ULL);
        norm += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return sum / norm;
}

double terrain_height(Seed seed, double x_m, double y_m) {
    // Broad continental shape + regional relief + sparse ridged mountains.
    const double continental = fbm(seed, x_m / 500'000.0, y_m / 500'000.0, 0xC011ULL, 5);
    const double regional = fbm(seed, x_m / 90'000.0, y_m / 90'000.0, 0xA11CEULL, 5);
    const double ridge_source = fbm(seed, x_m / 180'000.0, y_m / 180'000.0, 0xBEEF01ULL, 4);
    const double ridge = std::pow(std::max(0.0, 1.0 - std::abs(ridge_source) * 1.8), 3.0);

    // Sea level is intentionally not modeled yet. Values represent synthetic elevation datum.
    return 220.0 + continental * 520.0 + regional * 180.0 + ridge * 1'700.0;
}

float clamp01(double v) {
    return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

} // namespace

std::size_t CellCoordHash::operator()(const CellCoord& c) const noexcept {
    return static_cast<std::size_t>(coord_hash(0x574f524c4453494dULL, c.x, c.y));
}

void WorldConfig::validate() const {
    if (local_cell_m != 64 || regional_cell_m != 1024 || climate_cell_m != 8192) {
        throw std::invalid_argument("WorldSim currently requires hierarchy 64m / 1024m / 8192m");
    }
    if (regional_cell_m % local_cell_m != 0 || climate_cell_m % regional_cell_m != 0) {
        throw std::invalid_argument("cell hierarchy must divide exactly");
    }
    if (regional_cell_m / local_cell_m != static_cast<std::int32_t>(kLocalCellsPerAxis)) {
        throw std::invalid_argument("regional/local ratio must equal local patch dimension");
    }

    if (!std::isfinite(sea_level_m)) {
        throw std::invalid_argument("sea level must be finite");
    }

    if (!std::isfinite(bounds.origin_x_m) || !std::isfinite(bounds.origin_y_m) ||
        !std::isfinite(bounds.width_m) || !std::isfinite(bounds.height_m)) {
        throw std::invalid_argument("world bounds must be finite");
    }
    if (!(bounds.width_m > 0.0) || !(bounds.height_m > 0.0)) {
        throw std::invalid_argument("world bounds must have positive width and height");
    }
    if (!std::isfinite(bounds.origin_x_m + bounds.width_m) ||
        !std::isfinite(bounds.origin_y_m + bounds.height_m)) {
        throw std::invalid_argument("world bounds overflow finite coordinates");
    }
    const double end_x = bounds.origin_x_m + bounds.width_m;
    const double end_y = bounds.origin_y_m + bounds.height_m;

    // World positions are doubles and L2 cells are 64 m. Above 2^57 m, double spacing
    // reaches 32 m and local-cell centers/edges begin losing the precision this hierarchy
    // assumes. This precision bound is intentionally far stricter than int64_t's formal
    // range and still exceeds any planetary world by many orders of magnitude.
    constexpr double kMaxAbsWorldCoordinateM = 0x1p57;
    if (std::abs(bounds.origin_x_m) > kMaxAbsWorldCoordinateM ||
        std::abs(bounds.origin_y_m) > kMaxAbsWorldCoordinateM ||
        std::abs(end_x) > kMaxAbsWorldCoordinateM ||
        std::abs(end_y) > kMaxAbsWorldCoordinateM) {
        throw std::invalid_argument("world bounds exceed the supported coordinate precision range");
    }
}

World::World(WorldConfig config) : config_(config) {
    config_.validate();
}

float World::sample_elevation(WorldPosition position) const {
    if (!config_.bounds.contains(position)) {
        throw std::out_of_range("world position is outside configured bounds");
    }
    return static_cast<float>(terrain_height(config_.seed, position.x_m, position.y_m));
}

ClimateSample World::sample_climate(CellCoord coord) const {
    require_climate_in_bounds(coord);
    const auto center = cell_center(coord, config_.climate_cell_m);
    const double broad = fbm(config_.seed, center.x_m / 900'000.0, center.y_m / 900'000.0, 0xC11A7EULL, 4);
    const double wet = fbm(config_.seed, center.x_m / 600'000.0, center.y_m / 600'000.0, 0xA17ULL, 4);
    const double latitude_proxy = std::sin(center.y_m / 6'000'000.0 * kPi * 0.5);

    ClimateSample out;
    out.coord = coord;
    out.mean_temperature_c = static_cast<float>(10.0 - 9.0 * latitude_proxy + 3.5 * broad);
    out.annual_precipitation_mm = static_cast<float>(std::clamp(750.0 + 420.0 * wet - 110.0 * broad, 180.0, 2200.0));
    out.continentality = clamp01(0.5 + 0.5 * fbm(config_.seed, center.x_m / 1'200'000.0, center.y_m / 1'200'000.0, 0xC017ULL, 3));
    return out;
}

RegionalSample World::sample_region(CellCoord coord) const {
    require_region_in_bounds(coord);
    const auto center = cell_center(coord, config_.regional_cell_m);
    const double h = terrain_height(config_.seed, center.x_m, center.y_m);
    const double step = static_cast<double>(config_.regional_cell_m);
    const double hx0 = terrain_height(config_.seed, center.x_m - step, center.y_m);
    const double hx1 = terrain_height(config_.seed, center.x_m + step, center.y_m);
    const double hy0 = terrain_height(config_.seed, center.x_m, center.y_m - step);
    const double hy1 = terrain_height(config_.seed, center.x_m, center.y_m + step);
    const double dx = (hx1 - hx0) / (2.0 * step);
    const double dy = (hy1 - hy0) / (2.0 * step);
    const double slope = std::sqrt(dx * dx + dy * dy);

    const auto climate = sample_climate(regional_to_climate(coord, config_));
    const double temp_factor = std::clamp((static_cast<double>(climate.mean_temperature_c) + 5.0) / 20.0, 0.0, 1.0);
    const double rain_factor = std::clamp((static_cast<double>(climate.annual_precipitation_mm) - 250.0) / 900.0, 0.0, 1.0);
    const double elevation_penalty = std::clamp((h - 1200.0) / 1600.0, 0.0, 1.0);

    RegionalSample out;
    out.coord = coord;
    out.elevation_m = static_cast<float>(h);
    out.slope = static_cast<float>(slope);
    out.terrain_roughness = clamp01(std::abs(fbm(config_.seed, center.x_m / 30'000.0, center.y_m / 30'000.0, 0xBAD5EEDULL, 4)));
    out.bedrock_hardness = clamp01(0.5 + 0.5 * fbm(config_.seed, center.x_m / 220'000.0, center.y_m / 220'000.0, 0xB0C4ULL, 3));
    out.forest_potential = clamp01(temp_factor * rain_factor * (1.0 - 0.75 * elevation_penalty) * (1.0 - std::clamp(slope * 3.0, 0.0, 0.7)));
    return out;
}

RegionalSample World::sample_region(WorldPosition position) const {
    if (!config_.bounds.contains(position)) {
        throw std::out_of_range("world position is outside configured bounds");
    }
    return sample_region(world_to_cell(position, config_.regional_cell_m));
}

LocalPatch World::generate_local_patch(CellCoord regional_coord) const {
    require_region_in_bounds(regional_coord);
    LocalPatch patch;
    patch.regional_coord = regional_coord;

    const double region_x0 = static_cast<double>(regional_coord.x) * config_.regional_cell_m;
    const double region_y0 = static_cast<double>(regional_coord.y) * config_.regional_cell_m;
    const RegionalSample parent = sample_region(regional_coord);

    for (std::size_t ly = 0; ly < kLocalCellsPerAxis; ++ly) {
        for (std::size_t lx = 0; lx < kLocalCellsPerAxis; ++lx) {
            const std::size_t i = ly * kLocalCellsPerAxis + lx;
            const double x = region_x0 + (static_cast<double>(lx) + 0.5) * config_.local_cell_m;
            const double y = region_y0 + (static_cast<double>(ly) + 0.5) * config_.local_cell_m;
            const double fine = fbm(config_.seed, x / 4'000.0, y / 4'000.0, 0x10CA1ULL, 4);
            const double micro = fbm(config_.seed, x / 900.0, y / 900.0, 0x51A11ULL, 3);

            auto& cell = patch.cells[i];
            cell.elevation_m = static_cast<float>(terrain_height(config_.seed, x, y) + micro * 10.0);
            cell.terrain_roughness = clamp01(0.65 * parent.terrain_roughness + 0.35 * std::abs(micro));
            cell.forest_potential = clamp01(static_cast<double>(parent.forest_potential) + fine * 0.18 - std::abs(micro) * 0.05);
            cell.disturbance = 0.0f;
        }
    }

    return patch;
}

LocalPatch& World::materialize_local_patch_mutable(CellCoord regional_coord) {
    require_region_in_bounds(regional_coord);
    auto it = local_patches_.find(regional_coord);
    if (it != local_patches_.end()) {
        return it->second;
    }
    auto [inserted, ok] = local_patches_.emplace(regional_coord, generate_local_patch(regional_coord));
    (void)ok;
    return inserted->second;
}

const LocalPatch& World::materialize_local_patch(CellCoord regional_coord) {
    return materialize_local_patch_mutable(regional_coord);
}

const LocalPatch* World::find_local_patch(CellCoord regional_coord) const noexcept {
    const auto it = local_patches_.find(regional_coord);
    return it == local_patches_.end() ? nullptr : &it->second;
}

std::size_t World::disturb_surface(WorldPosition min, WorldPosition max, float amount) {
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f) {
        throw std::invalid_argument("disturbance amount must be in [0,1]");
    }
    if (!std::isfinite(min.x_m) || !std::isfinite(min.y_m) ||
        !std::isfinite(max.x_m) || !std::isfinite(max.y_m)) {
        throw std::invalid_argument("disturbance rectangle coordinates must be finite");
    }
    if (!(min.x_m < max.x_m) || !(min.y_m < max.y_m)) {
        throw std::invalid_argument("disturbance rectangle must have positive area");
    }
    if (amount == 0.0f) return 0;

    const WorldPosition clipped_min{
        std::max(min.x_m, config_.bounds.origin_x_m),
        std::max(min.y_m, config_.bounds.origin_y_m)};
    const WorldPosition clipped_max{
        std::min(max.x_m, config_.bounds.origin_x_m + config_.bounds.width_m),
        std::min(max.y_m, config_.bounds.origin_y_m + config_.bounds.height_m)};
    if (!(clipped_min.x_m < clipped_max.x_m) || !(clipped_min.y_m < clipped_max.y_m)) {
        return 0;
    }

    const auto rmin = world_to_cell(clipped_min, config_.regional_cell_m);
    // Nextafter keeps a max edge exactly on a cell boundary from spilling into the next cell.
    const WorldPosition inclusive_max{
        std::nextafter(clipped_max.x_m, -std::numeric_limits<double>::infinity()),
        std::nextafter(clipped_max.y_m, -std::numeric_limits<double>::infinity())};
    const auto rmax = world_to_cell(inclusive_max, config_.regional_cell_m);

    std::size_t affected = 0;
    for (std::int64_t ry = rmin.y; ry <= rmax.y; ++ry) {
        for (std::int64_t rx = rmin.x; rx <= rmax.x; ++rx) {
            LocalPatch& patch = materialize_local_patch_mutable({rx, ry});
            const double region_x0 = static_cast<double>(rx) * config_.regional_cell_m;
            const double region_y0 = static_cast<double>(ry) * config_.regional_cell_m;
            for (std::size_t ly = 0; ly < kLocalCellsPerAxis; ++ly) {
                for (std::size_t lx = 0; lx < kLocalCellsPerAxis; ++lx) {
                    const double cx0 = region_x0 + static_cast<double>(lx) * config_.local_cell_m;
                    const double cy0 = region_y0 + static_cast<double>(ly) * config_.local_cell_m;
                    const double cx1 = cx0 + config_.local_cell_m;
                    const double cy1 = cy0 + config_.local_cell_m;
                    const bool intersects = cx1 > clipped_min.x_m && cx0 < clipped_max.x_m &&
                                            cy1 > clipped_min.y_m && cy0 < clipped_max.y_m;
                    if (intersects) {
                        auto& cell = patch.cells[ly * kLocalCellsPerAxis + lx];
                        if (amount > cell.disturbance) {
                            cell.disturbance = amount;
                            ++affected;
                        }
                    }
                }
            }
        }
    }
    return affected;
}

void World::save(const std::filesystem::path& path) const {
    persistence::save_world(path, config_, local_patches_);
}

World World::load(const std::filesystem::path& path) {
    WorldConfig cfg;
    std::unordered_map<CellCoord, LocalPatch, CellCoordHash> patches;
    persistence::load_world(path, cfg, patches);
    World world(cfg);
    world.local_patches_ = std::move(patches);
    return world;
}

void World::require_region_in_bounds(CellCoord coord) const {
    const double s = static_cast<double>(config_.regional_cell_m);
    const double x0 = static_cast<double>(coord.x) * s;
    const double y0 = static_cast<double>(coord.y) * s;
    const double x1 = x0 + s;
    const double y1 = y0 + s;
    const double bx0 = config_.bounds.origin_x_m;
    const double by0 = config_.bounds.origin_y_m;
    const double bx1 = bx0 + config_.bounds.width_m;
    const double by1 = by0 + config_.bounds.height_m;
    if (!(x1 > bx0 && x0 < bx1 && y1 > by0 && y0 < by1)) {
        throw std::out_of_range("regional cell is outside configured world bounds");
    }
}

void World::require_climate_in_bounds(CellCoord coord) const {
    const double s = static_cast<double>(config_.climate_cell_m);
    const double x0 = static_cast<double>(coord.x) * s;
    const double y0 = static_cast<double>(coord.y) * s;
    const double x1 = x0 + s;
    const double y1 = y0 + s;
    const double bx0 = config_.bounds.origin_x_m;
    const double by0 = config_.bounds.origin_y_m;
    const double bx1 = bx0 + config_.bounds.width_m;
    const double by1 = by0 + config_.bounds.height_m;
    if (!(x1 > bx0 && x0 < bx1 && y1 > by0 && y0 < by1)) {
        throw std::out_of_range("climate cell is outside configured world bounds");
    }
}

} // namespace worldsim
