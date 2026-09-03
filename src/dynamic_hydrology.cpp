#include "worldsim/dynamic_hydrology.hpp"

#include "worldsim/coordinates.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>
#include <stdexcept>
#include <vector>

namespace worldsim {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSecondsPerDay = 86'400.0;
constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

bool finite_non_negative(float v) {
    return std::isfinite(v) && v >= 0.0f;
}

bool same_bounds(const WorldBounds& a, const WorldBounds& b) {
    return a.origin_x_m == b.origin_x_m && a.origin_y_m == b.origin_y_m &&
           a.width_m == b.width_m && a.height_m == b.height_m;
}

bool same_config_identity(const WorldConfig& a, const WorldConfig& b) {
    return a.seed == b.seed && same_bounds(a.bounds, b.bounds) &&
           a.local_cell_m == b.local_cell_m && a.regional_cell_m == b.regional_cell_m &&
           a.climate_cell_m == b.climate_cell_m && a.sea_level_m == b.sea_level_m;
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

double depth_to_volume(double depth_mm, double area_m2) {
    return depth_mm * 0.001 * area_m2;
}

void validate_tile(const World& world, const AuthoritativeHydrologyTile& tile) {
    if (!same_config_identity(world.config(), tile.config)) {
        throw std::invalid_argument("authoritative hydrology tile belongs to a different world configuration");
    }
    const auto& h = tile.hydrology;
    if (h.request.width_cells != 8 || h.request.height_cells != 8 || h.cells.size() != 64) {
        throw std::invalid_argument("dynamic hydrology requires an authoritative 8x8 L1 tile");
    }
    if (h.request.min_coord != CellCoord{tile.climate_coord.x * 8, tile.climate_coord.y * 8}) {
        throw std::invalid_argument("authoritative tile coordinate invariant is broken");
    }
}

std::size_t local_index(const HydrologyResult& h, CellCoord c) {
    const auto dx = c.x - h.request.min_coord.x;
    const auto dy = c.y - h.request.min_coord.y;
    if (dx < 0 || dy < 0 || dx >= static_cast<std::int64_t>(h.request.width_cells) ||
        dy >= static_cast<std::int64_t>(h.request.height_cells)) return kNoIndex;
    return static_cast<std::size_t>(dy) * h.request.width_cells + static_cast<std::size_t>(dx);
}

std::vector<std::size_t> routing_order(const HydrologyResult& h) {
    const std::size_t count = h.cells.size();
    std::vector<std::size_t> indegree(count, 0);
    std::size_t active_count = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& c = h.cells[i];
        if (!c.active) continue;
        ++active_count;
        if (!c.has_downstream || c.downstream_is_external) continue;
        const auto d = local_index(h, c.downstream_coord);
        if (d == kNoIndex || !h.cells[d].active) {
            throw std::invalid_argument("authoritative tile has an invalid internal downstream edge");
        }
        ++indegree[d];
    }

    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
    for (std::size_t i = 0; i < count; ++i) {
        if (h.cells[i].active && indegree[i] == 0) ready.push(i);
    }

    std::vector<std::size_t> order;
    order.reserve(active_count);
    while (!ready.empty()) {
        const auto i = ready.top();
        ready.pop();
        order.push_back(i);
        const auto& c = h.cells[i];
        if (!c.has_downstream || c.downstream_is_external) continue;
        const auto d = local_index(h, c.downstream_coord);
        if (--indegree[d] == 0) ready.push(d);
    }
    if (order.size() != active_count) {
        throw std::invalid_argument("authoritative tile drainage graph contains a cycle");
    }
    return order;
}

void validate_state(const World& world, const AuthoritativeHydrologyTile& tile, const DynamicHydrologyTileState& state) {
    validate_tile(world, tile);
    if (!same_config_identity(state.config, tile.config) || state.climate_coord != tile.climate_coord ||
        state.cells.size() != tile.hydrology.cells.size()) {
        throw std::invalid_argument("dynamic hydrology state belongs to a different tile");
    }
    if (!std::isfinite(state.simulated_days) || state.simulated_days < 0.0) {
        throw std::invalid_argument("dynamic hydrology simulated time is invalid");
    }
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const auto& s = state.cells[i];
        const auto& t = tile.hydrology.cells[i];
        if (s.coord != t.coord || s.active != t.active) {
            throw std::invalid_argument("dynamic hydrology state cell topology does not match tile");
        }
        if (!finite_non_negative(s.snow_water_equivalent_mm) || !finite_non_negative(s.surface_water_mm) ||
            !finite_non_negative(s.soil_water_mm) || !finite_non_negative(s.groundwater_mm)) {
            throw std::invalid_argument("dynamic hydrology state contains invalid water storage");
        }
    }
}

double total_storage_m3(const World& world, const DynamicHydrologyTileState& state) {
    double total = 0.0;
    for (const auto& c : state.cells) {
        if (!c.active) continue;
        const double area = overlap_area_m2(c.coord, world.config().regional_cell_m, world.config().bounds);
        const double depth = static_cast<double>(c.snow_water_equivalent_mm) + c.surface_water_mm +
                             c.soil_water_mm + c.groundwater_mm;
        total += depth_to_volume(depth, area);
    }
    return total;
}

} // namespace

void DynamicHydrologyParameters::validate() const {
    if (!finite_non_negative(soil_capacity_mm) || !finite_non_negative(field_capacity_mm) ||
        !finite_non_negative(wilting_point_mm) || !finite_non_negative(infiltration_capacity_mm_per_day) ||
        !finite_non_negative(surface_storage_capacity_mm) || !finite_non_negative(percolation_rate_per_day) ||
        !finite_non_negative(groundwater_recession_per_day) || !finite_non_negative(snow_melt_mm_per_c_day) ||
        !finite_non_negative(initial_soil_water_mm) || !finite_non_negative(initial_groundwater_mm)) {
        throw std::invalid_argument("dynamic hydrology parameters must be finite and non-negative");
    }
    if (!(wilting_point_mm <= field_capacity_mm && field_capacity_mm <= soil_capacity_mm)) {
        throw std::invalid_argument("dynamic hydrology requires wilting <= field capacity <= soil capacity");
    }
    if (initial_soil_water_mm > soil_capacity_mm) {
        throw std::invalid_argument("initial soil water exceeds soil capacity");
    }
}

std::size_t DynamicHydrologyTileState::index_of(CellCoord coord) const {
    const CellCoord min{climate_coord.x * 8, climate_coord.y * 8};
    const auto dx = coord.x - min.x;
    const auto dy = coord.y - min.y;
    if (dx < 0 || dy < 0 || dx >= 8 || dy >= 8) {
        throw std::out_of_range("dynamic hydrology coordinate is outside tile");
    }
    return static_cast<std::size_t>(dy) * 8U + static_cast<std::size_t>(dx);
}

const DynamicHydrologyCellState& DynamicHydrologyTileState::cell(CellCoord coord) const {
    return cells.at(index_of(coord));
}

DynamicHydrologyTileState make_dynamic_hydrology_tile_state(
    const World& world,
    const AuthoritativeHydrologyTile& tile,
    const DynamicHydrologyParameters& parameters) {
    parameters.validate();
    validate_tile(world, tile);

    DynamicHydrologyTileState state;
    state.config = world.config();
    state.climate_coord = tile.climate_coord;
    state.cells.resize(tile.hydrology.cells.size());
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const auto& source = tile.hydrology.cells[i];
        auto& target = state.cells[i];
        target.coord = source.coord;
        target.active = source.active;
        if (!source.active || source.ocean) continue;
        // Initial conditions are explicit model parameters; they are not generated water fluxes.
        target.soil_water_mm = parameters.initial_soil_water_mm;
        target.groundwater_mm = parameters.initial_groundwater_mm;
        const double area = overlap_area_m2(target.coord, world.config().regional_cell_m, world.config().bounds);
        if (!(area > 0.0)) throw std::logic_error("active hydrology cell does not overlap world");
    }
    return state;
}

std::vector<HydrometeorologicalForcing> make_smooth_climatological_forcing(
    const World& world,
    const AuthoritativeHydrologyTile& tile,
    double day_of_year,
    double duration_days) {
    validate_tile(world, tile);
    if (!std::isfinite(day_of_year) || !std::isfinite(duration_days) || !(duration_days > 0.0)) {
        throw std::invalid_argument("climatological forcing time must be finite and duration positive");
    }

    const auto climate = world.sample_climate(tile.climate_coord);
    const double phase = 2.0 * kPi * ((day_of_year - 200.0) / 365.2425);
    const double amplitude = 8.0 + 8.0 * static_cast<double>(climate.continentality);
    const double temperature = static_cast<double>(climate.mean_temperature_c) + amplitude * std::cos(phase);
    // Smooth seasonal precipitation weighting with mean exactly 1 over a full year.
    const double precip_weight = std::max(0.15, 1.0 + 0.30 * std::cos(phase - 0.7));
    const double precipitation = static_cast<double>(climate.annual_precipitation_mm) /
                                 365.2425 * precip_weight * duration_days;
    // PET remains a deliberately simple forcing until radiation/humidity/wind exist.

    std::vector<HydrometeorologicalForcing> out;
    out.reserve(tile.hydrology.cells.size());
    for (const auto& cell : tile.hydrology.cells) {
        HydrometeorologicalForcing forcing;
        forcing.coord = cell.coord;
        if (cell.active && !cell.ocean) {
            forcing.precipitation_mm = static_cast<float>(precipitation);
            const double local_temperature = temperature -
                0.0065 * std::max(0.0f, cell.terrain_elevation_m);
            forcing.mean_air_temperature_c = static_cast<float>(local_temperature);
            forcing.potential_evapotranspiration_mm = static_cast<float>(
                std::max(0.0, 0.10 * (local_temperature + 5.0)) * duration_days);
        }
        out.push_back(forcing);
    }
    return out;
}

HydrologyStepReport advance_dynamic_hydrology_tile(
    const World& world,
    const AuthoritativeHydrologyTile& tile,
    DynamicHydrologyTileState& state,
    const std::vector<HydrometeorologicalForcing>& forcing,
    const std::vector<ExternalHydrologyInflow>& external_inflows,
    double duration_days,
    const DynamicHydrologyParameters& parameters) {
    parameters.validate();
    validate_state(world, tile, state);
    if (!std::isfinite(duration_days) || !(duration_days > 0.0) || duration_days > 366.0) {
        throw std::invalid_argument("dynamic hydrology duration must be in (0, 366] days");
    }
    if (forcing.size() != state.cells.size()) {
        throw std::invalid_argument("dynamic hydrology forcing must contain exactly one record per tile cell");
    }
    for (std::size_t i = 0; i < forcing.size(); ++i) {
        const auto& f = forcing[i];
        if (f.coord != state.cells[i].coord || !finite_non_negative(f.precipitation_mm) ||
            !std::isfinite(f.mean_air_temperature_c) || !finite_non_negative(f.potential_evapotranspiration_mm)) {
            throw std::invalid_argument("dynamic hydrology forcing is invalid or not aligned with tile cells");
        }
    }

    std::vector<double> inflow_by_cell(state.cells.size(), 0.0);
    for (const auto& in : external_inflows) {
        if (!std::isfinite(in.volume_m3) || in.volume_m3 < 0.0) {
            throw std::invalid_argument("external hydrology inflow must be finite and non-negative");
        }
        const auto i = state.index_of(in.coord);
        if (!state.cells[i].active) throw std::invalid_argument("external hydrology inflow targets an inactive cell");
        inflow_by_cell[i] += in.volume_m3;
    }

    const auto order = routing_order(tile.hydrology);
    const int steps = static_cast<int>(std::ceil(duration_days));
    const double sub_days = duration_days / static_cast<double>(steps);
    const double sub_seconds = sub_days * kSecondsPerDay;

    HydrologyStepReport report;
    report.duration_days = duration_days;
    report.storage_before_m3 = total_storage_m3(world, state);
    std::vector<double> routed_volume_total(state.cells.size(), 0.0);

    for (auto& c : state.cells) {
        c.last_evapotranspiration_mm = 0.0f;
        c.last_quick_runoff_mm = 0.0f;
        c.last_baseflow_mm = 0.0f;
        c.last_routed_discharge_m3_s = 0.0f;
    }

    for (int step = 0; step < steps; ++step) {
        std::vector<double> routed(state.cells.size(), 0.0);

        for (std::size_t i = 0; i < state.cells.size(); ++i) {
            auto& s = state.cells[i];
            const auto& topo = tile.hydrology.cells[i];
            if (!s.active || topo.ocean) continue;
            const auto& f = forcing[i];
            const double area = overlap_area_m2(s.coord, world.config().regional_cell_m, world.config().bounds);
            const double precipitation = static_cast<double>(f.precipitation_mm) / steps;
            const double pet = static_cast<double>(f.potential_evapotranspiration_mm) / steps;
            const double temperature = static_cast<double>(f.mean_air_temperature_c);
            report.precipitation_m3 += depth_to_volume(precipitation, area);

            // Linear rain/snow transition from -1 C (all snow) to +1 C (all rain).
            const double snow_fraction = std::clamp((1.0 - temperature) * 0.5, 0.0, 1.0);
            const double snowfall = precipitation * snow_fraction;
            const double rainfall = precipitation - snowfall;
            s.snow_water_equivalent_mm += static_cast<float>(snowfall);
            const double melt = std::min(static_cast<double>(s.snow_water_equivalent_mm),
                std::max(0.0, temperature) * parameters.snow_melt_mm_per_c_day * sub_days);
            s.snow_water_equivalent_mm -= static_cast<float>(melt);
            s.surface_water_mm += static_cast<float>(rainfall + melt);

            const double saturation = parameters.soil_capacity_mm > 0.0f
                ? std::clamp(static_cast<double>(s.soil_water_mm) / parameters.soil_capacity_mm, 0.0, 1.0)
                : 1.0;
            const double infiltration_capacity = static_cast<double>(parameters.infiltration_capacity_mm_per_day) *
                                                 (0.25 + 0.75 * (1.0 - saturation)) * sub_days;
            const double soil_room = std::max(0.0, static_cast<double>(parameters.soil_capacity_mm) - s.soil_water_mm);
            const double infiltration = std::min({static_cast<double>(s.surface_water_mm), infiltration_capacity, soil_room});
            s.surface_water_mm -= static_cast<float>(infiltration);
            s.soil_water_mm += static_cast<float>(infiltration);

            // Open water evaporates first; remaining atmospheric demand is soil-water limited.
            const double surface_evap = std::min(static_cast<double>(s.surface_water_mm), pet * 0.35);
            s.surface_water_mm -= static_cast<float>(surface_evap);
            const double remaining_pet = pet - surface_evap;
            const double denom = std::max(1e-6, static_cast<double>(parameters.field_capacity_mm - parameters.wilting_point_mm));
            const double stress = std::clamp((static_cast<double>(s.soil_water_mm) - parameters.wilting_point_mm) / denom, 0.0, 1.0);
            const double soil_et = std::min(static_cast<double>(s.soil_water_mm), remaining_pet * stress);
            s.soil_water_mm -= static_cast<float>(soil_et);
            const double et = surface_evap + soil_et;
            s.last_evapotranspiration_mm += static_cast<float>(et);
            report.evapotranspiration_m3 += depth_to_volume(et, area);

            const double excess_soil = std::max(0.0, static_cast<double>(s.soil_water_mm) - parameters.field_capacity_mm);
            const double percolation_fraction = 1.0 - std::exp(-static_cast<double>(parameters.percolation_rate_per_day) * sub_days);
            const double percolation = excess_soil * percolation_fraction;
            s.soil_water_mm -= static_cast<float>(percolation);
            s.groundwater_mm += static_cast<float>(percolation);

            const double baseflow_fraction = 1.0 - std::exp(-static_cast<double>(parameters.groundwater_recession_per_day) * sub_days);
            const double baseflow = static_cast<double>(s.groundwater_mm) * baseflow_fraction;
            s.groundwater_mm -= static_cast<float>(baseflow);
            s.last_baseflow_mm += static_cast<float>(baseflow);

            const double quick_runoff = std::max(0.0,
                static_cast<double>(s.surface_water_mm) - parameters.surface_storage_capacity_mm);
            s.surface_water_mm -= static_cast<float>(quick_runoff);
            s.last_quick_runoff_mm += static_cast<float>(quick_runoff);

            routed[i] += depth_to_volume(quick_runoff + baseflow, area);
        }

        // External inflow is channel water and therefore bypasses local soil/surface stores.
        for (std::size_t i = 0; i < inflow_by_cell.size(); ++i) {
            const double v = inflow_by_cell[i] / steps;
            routed[i] += v;
            report.external_inflow_m3 += v;
        }

        for (const auto i : order) {
            const auto volume = routed[i];
            routed_volume_total[i] += volume;
            const auto& topo = tile.hydrology.cells[i];
            if (!topo.has_downstream || topo.downstream_is_external) {
                report.external_outflow_m3 += volume;
                continue;
            }
            const auto d = local_index(tile.hydrology, topo.downstream_coord);
            if (d == kNoIndex) throw std::logic_error("internal route unexpectedly leaves tile");
            routed[d] += volume;
        }
        (void)sub_seconds;
    }

    const double total_seconds = duration_days * kSecondsPerDay;
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        if (state.cells[i].active) {
            state.cells[i].last_routed_discharge_m3_s = static_cast<float>(routed_volume_total[i] / total_seconds);
        }
    }

    state.simulated_days += duration_days;
    report.storage_after_m3 = total_storage_m3(world, state);
    report.water_balance_error_m3 = report.storage_before_m3 + report.precipitation_m3 + report.external_inflow_m3 -
                                    report.evapotranspiration_m3 - report.external_outflow_m3 - report.storage_after_m3;
    return report;
}

} // namespace worldsim
