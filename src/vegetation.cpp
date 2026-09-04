#include "worldsim/vegetation.hpp"

#include "worldsim/coordinates.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace worldsim {
namespace {

constexpr double kDisturbanceRecoveryDays = 730.0;
constexpr double kVegetationRecoveryDays = 365.0;
constexpr double kVegetationTemperatureLowC = -2.0;
constexpr double kVegetationTemperatureRangeC = 18.0;
constexpr double kVegetationMoistureLow = 0.10;
constexpr double kVegetationMoistureRange = 0.60;

bool same_bounds(const WorldBounds& a, const WorldBounds& b) {
    return a.origin_x_m == b.origin_x_m && a.origin_y_m == b.origin_y_m &&
           a.width_m == b.width_m && a.height_m == b.height_m;
}

bool same_config_identity(const WorldConfig& a, const WorldConfig& b) {
    return a.seed == b.seed && same_bounds(a.bounds, b.bounds) &&
           a.local_cell_m == b.local_cell_m && a.regional_cell_m == b.regional_cell_m &&
           a.climate_cell_m == b.climate_cell_m && a.sea_level_m == b.sea_level_m;
}

double local_overlap_area_m2(
    CellCoord regional_coord,
    std::size_t index,
    const WorldConfig& config) {
    const std::size_t lx = index % kLocalCellsPerAxis;
    const std::size_t ly = index / kLocalCellsPerAxis;
    const double s = static_cast<double>(config.local_cell_m);
    const double region_x0 = static_cast<double>(regional_coord.x) * config.regional_cell_m;
    const double region_y0 = static_cast<double>(regional_coord.y) * config.regional_cell_m;
    const double x0 = region_x0 + static_cast<double>(lx) * s;
    const double y0 = region_y0 + static_cast<double>(ly) * s;
    const double x1 = x0 + s;
    const double y1 = y0 + s;
    const double bx0 = config.bounds.origin_x_m;
    const double by0 = config.bounds.origin_y_m;
    const double bx1 = bx0 + config.bounds.width_m;
    const double by1 = by0 + config.bounds.height_m;
    const double ox0 = std::max(x0, bx0);
    const double oy0 = std::max(y0, by0);
    const double ox1 = std::min(x1, bx1);
    const double oy1 = std::min(y1, by1);
    if (!(ox1 > ox0) || !(oy1 > oy0)) return 0.0;
    return (ox1 - ox0) * (oy1 - oy0);
}

void require_valid_cell(const LocalCell& cell) {
    const auto in_unit = [](float v) {
        return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
    };
    if (!std::isfinite(cell.elevation_m) ||
        !in_unit(cell.terrain_roughness) ||
        !in_unit(cell.forest_potential) ||
        !in_unit(cell.disturbance) ||
        !in_unit(cell.vegetation_biomass)) {
        throw std::invalid_argument("materialized vegetation cell is invalid");
    }
    const double disturbed_capacity =
        static_cast<double>(cell.forest_potential) * (1.0 - cell.disturbance);
    const double tolerance = 2.0e-6;
    if (static_cast<double>(cell.vegetation_biomass) >
        disturbed_capacity + tolerance) {
        throw std::invalid_argument(
            "materialized vegetation biomass exceeds disturbed local potential");
    }
}

bool coord_less(CellCoord a, CellCoord b) {
    return a.y != b.y ? a.y < b.y : a.x < b.x;
}

double coarse_soil_saturation(
    const World& world,
    const MultiresolutionWaterState& water,
    CellCoord climate_coord) {
    const auto i = water.coarse_state().index_of(climate_coord);
    if (water.coarse_state().cells()[i].soil_water_mm <= 0.0f) return 0.0;
    const double capacity =
        static_cast<double>(water.parameters().soil_capacity_mm) *
        static_cast<double>(world.sample_climate_soil(climate_coord).storage_capacity_scale);
    if (!(capacity > 0.0) || !std::isfinite(capacity)) return 0.0;
    return std::clamp(
        static_cast<double>(water.coarse_state().cells()[i].soil_water_mm) / capacity,
        0.0,
        1.0);
}

double regional_soil_saturation(
    const World& world,
    const MultiresolutionWaterState& water,
    CellCoord regional_coord,
    CellCoord climate_coord) {
    if (!water.is_refined(climate_coord)) {
        return coarse_soil_saturation(world, water, climate_coord);
    }
    const auto& tile = water.refined_tile(climate_coord);
    const auto& cell = tile.cell(regional_coord);
    if (!cell.active || cell.soil_water_mm <= 0.0f) return 0.0;
    const double capacity =
        static_cast<double>(water.parameters().soil_capacity_mm) *
        static_cast<double>(world.sample_soil(regional_coord).storage_capacity_scale);
    if (!(capacity > 0.0) || !std::isfinite(capacity)) return 0.0;
    return std::clamp(
        static_cast<double>(cell.soil_water_mm) / capacity,
        0.0,
        1.0);
}

} // namespace

std::vector<CellCoord> World::materialized_patch_coords() const {
    std::vector<CellCoord> out;
    out.reserve(local_patches_.size());
    for (const auto& [coord, patch] : local_patches_) {
        if (coord != patch.regional_coord) {
            throw std::logic_error("materialized local patch coordinate invariant is broken");
        }
        out.push_back(coord);
    }
    std::sort(out.begin(), out.end(), coord_less);
    return out;
}

VegetationStepReport World::advance_materialized_vegetation_day(
    const std::vector<VegetationForcing>& forcing) {
    const auto coords = materialized_patch_coords();
    if (forcing.size() != coords.size()) {
        throw std::invalid_argument(
            "vegetation forcing must contain exactly one record per materialized patch");
    }

    std::vector<VegetationForcing> ordered = forcing;
    std::sort(ordered.begin(), ordered.end(), [](const VegetationForcing& a, const VegetationForcing& b) {
        return coord_less(a.regional_coord, b.regional_coord);
    });
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const auto& f = ordered[i];
        if (f.regional_coord != coords[i]) {
            throw std::invalid_argument(
                "vegetation forcing coordinates do not match materialized patches");
        }
        if (!std::isfinite(f.mean_air_temperature_c) ||
            !std::isfinite(f.soil_saturation) ||
            f.soil_saturation < 0.0f ||
            f.soil_saturation > 1.0f) {
            throw std::invalid_argument("vegetation forcing contains invalid values");
        }
        if (i > 0 && ordered[i - 1].regional_coord == f.regional_coord) {
            throw std::invalid_argument("vegetation forcing contains duplicate patch coordinates");
        }
    }

    struct PendingPatch {
        CellCoord coord{};
        std::array<LocalCell, kLocalCellCount> cells{};
    };
    std::vector<PendingPatch> pending;
    pending.reserve(coords.size());

    VegetationStepReport report;
    report.patch_count = static_cast<std::uint64_t>(coords.size());

    for (std::size_t p = 0; p < coords.size(); ++p) {
        const auto& current = local_patches_.at(coords[p]);
        PendingPatch next;
        next.coord = coords[p];
        next.cells = current.cells;

        const double temperature_factor = std::clamp(
            (static_cast<double>(ordered[p].mean_air_temperature_c) -
             kVegetationTemperatureLowC) /
                kVegetationTemperatureRangeC,
            0.0,
            1.0);
        const double moisture_factor = std::clamp(
            (static_cast<double>(ordered[p].soil_saturation) - kVegetationMoistureLow) /
                kVegetationMoistureRange,
            0.0,
            1.0);
        const double suitability = temperature_factor * moisture_factor;
        const double recovery_fraction =
            1.0 - std::exp(-suitability / kVegetationRecoveryDays);
        const double disturbance_decay =
            std::exp(-1.0 / kDisturbanceRecoveryDays);

        for (std::size_t i = 0; i < current.cells.size(); ++i) {
            const auto& before = current.cells[i];
            require_valid_cell(before);
            auto& after = next.cells[i];
            const double area = local_overlap_area_m2(coords[p], i, config_);
            if (!(area > 0.0)) continue;

            if (!(static_cast<double>(before.elevation_m) >
                  static_cast<double>(config_.sea_level_m))) {
                after.vegetation_biomass = 0.0f;
                continue;
            }

            ++report.land_cell_count;
            report.land_area_m2 += area;
            report.biomass_area_before_m2 +=
                static_cast<double>(before.vegetation_biomass) * area;
            report.disturbance_area_before_m2 +=
                static_cast<double>(before.disturbance) * area;

            const double disturbance_after =
                std::clamp(static_cast<double>(before.disturbance) * disturbance_decay, 0.0, 1.0);
            const double target =
                static_cast<double>(before.forest_potential) * (1.0 - disturbance_after);
            const double biomass_after = std::clamp(
                static_cast<double>(before.vegetation_biomass) +
                    (target - static_cast<double>(before.vegetation_biomass)) * recovery_fraction,
                0.0,
                target);

            after.disturbance = static_cast<float>(disturbance_after);
            after.vegetation_biomass = static_cast<float>(biomass_after);
            require_valid_cell(after);

            report.biomass_area_after_m2 += biomass_after * area;
            report.disturbance_area_after_m2 += disturbance_after * area;
        }
        pending.push_back(std::move(next));
    }

    if (!std::isfinite(report.land_area_m2) ||
        !std::isfinite(report.biomass_area_before_m2) ||
        !std::isfinite(report.biomass_area_after_m2) ||
        !std::isfinite(report.disturbance_area_before_m2) ||
        !std::isfinite(report.disturbance_area_after_m2)) {
        throw std::overflow_error("vegetation report accumulation overflow");
    }

    for (auto& next : pending) {
        local_patches_.at(next.coord).cells = std::move(next.cells);
    }
    return report;
}

void World::swap_local_history(World& other) noexcept {
    local_patches_.swap(other.local_patches_);
}

std::vector<VegetationForcing> make_materialized_vegetation_forcing(
    const World& world,
    const WeatherState& weather,
    const MultiresolutionWaterState& water) {
    if (!same_config_identity(world.config(), weather.config()) ||
        !same_config_identity(world.config(), water.config())) {
        throw std::invalid_argument(
            "vegetation forcing components belong to different worlds");
    }
    if (weather.simulated_day() != water.simulated_day()) {
        throw std::invalid_argument(
            "vegetation forcing weather/water clocks are inconsistent");
    }

    const auto coords = world.materialized_patch_coords();
    std::vector<VegetationForcing> out;
    out.reserve(coords.size());
    for (const auto regional_coord : coords) {
        const auto climate_coord = regional_to_climate(regional_coord, world.config());
        const auto weather_sample = sample_weather(weather, climate_coord);
        VegetationForcing f;
        f.regional_coord = regional_coord;
        f.mean_air_temperature_c = weather_sample.mean_air_temperature_c;
        f.soil_saturation = static_cast<float>(regional_soil_saturation(
            world, water, regional_coord, climate_coord));
        if (!std::isfinite(f.mean_air_temperature_c) ||
            !std::isfinite(f.soil_saturation) ||
            f.soil_saturation < 0.0f ||
            f.soil_saturation > 1.0f) {
            throw std::overflow_error("derived vegetation forcing is invalid");
        }
        out.push_back(f);
    }
    return out;
}

} // namespace worldsim
