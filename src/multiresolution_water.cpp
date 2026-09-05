#include "worldsim/multiresolution_water.hpp"

#include "worldsim/world.hpp"
#include "soil_hydrology_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace worldsim {
namespace {

constexpr double kSecondsPerDay = 86'400.0;
constexpr double kMaxWaterDepthMm = 1.0e30;
constexpr double kSqrt2 = 1.4142135623730950488;
// Deliberately weak simulation-scale transport heuristic. The daily L0 scheduler cannot
// represent real sub-day celerity, so length remains dominant while slope/discharge only
// nudge residence time inside a bounded range.
constexpr double kChannelSlopeFloor = 1.0e-5;
constexpr double kChannelDischargeFloorM3S = 1.0;
constexpr double kChannelReferenceDischargeM3S = 100.0;
constexpr double kChannelSlopeExponent = -0.08;
constexpr double kChannelDischargeExponent = -0.06;
constexpr double kChannelMinResidenceDays = 0.75;
constexpr double kChannelMaxResidenceDays = 3.0;
// Refined atmospheric forcing remains a bounded daily simulation-scale transform. It does
// not create L1 atmospheric state or claim physical windward/leeward precipitation dynamics.
constexpr double kAtmosphericLapseRateCPerM = 0.0065;
constexpr double kAtmosphericMaxTemperatureCorrectionC = 8.0;
constexpr double kOrographicPrecipitationPerKm = 0.15;
constexpr double kOrographicMinMultiplier = 0.75;
constexpr double kOrographicMaxMultiplier = 1.25;
constexpr std::uint32_t kNoDownstream = 0xFFFFFFFFu;
constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

bool finite_non_negative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

bool finite_non_negative(double value) {
    return std::isfinite(value) && value >= 0.0;
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

WorldPosition overlap_center(CellCoord coord, std::int32_t cell_m, const WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) {
        throw std::logic_error("grid cell has zero world overlap");
    }
    return {(x0 + x1) * 0.5, (y0 + y1) * 0.5};
}

double depth_to_volume(double depth_mm, double area_m2) {
    return depth_mm * 0.001 * area_m2;
}

double volume_to_depth(double volume_m3, double area_m2) {
    return volume_m3 * 1000.0 / area_m2;
}

double stored_depth_mm(const ContinentalWaterCellState& c) {
    return static_cast<double>(c.snow_water_equivalent_mm) + c.surface_water_mm +
           c.soil_water_mm + c.groundwater_mm;
}

double stored_depth_mm(const DynamicHydrologyCellState& c) {
    return static_cast<double>(c.snow_water_equivalent_mm) + c.surface_water_mm +
           c.soil_water_mm + c.groundwater_mm;
}

void require_valid_storage(const ContinentalWaterCellState& c) {
    if (!finite_non_negative(c.snow_water_equivalent_mm) ||
        !finite_non_negative(c.surface_water_mm) ||
        !finite_non_negative(c.soil_water_mm) ||
        !finite_non_negative(c.groundwater_mm) ||
        !std::isfinite(stored_depth_mm(c)) || stored_depth_mm(c) > kMaxWaterDepthMm) {
        throw std::invalid_argument("coarse multiresolution water storage is invalid");
    }
}

void require_valid_storage(const DynamicHydrologyCellState& c) {
    if (!finite_non_negative(c.snow_water_equivalent_mm) ||
        !finite_non_negative(c.surface_water_mm) ||
        !finite_non_negative(c.soil_water_mm) ||
        !finite_non_negative(c.groundwater_mm) ||
        !std::isfinite(stored_depth_mm(c)) || stored_depth_mm(c) > kMaxWaterDepthMm) {
        throw std::invalid_argument("refined multiresolution water storage is invalid");
    }
}

ChannelTransportProperties derive_channel_transport(
    const ContinentalHydrologyResult& topology,
    std::size_t index) {
    const auto& cell = topology.cells.at(index);
    if (!std::isfinite(cell.filled_elevation_m) ||
        !finite_non_negative(cell.accumulated_discharge_m3_s)) {
        throw std::invalid_argument(
            "continental topology contains invalid channel transport inputs");
    }

    bool diagonal = false;
    double downstream_filled_elevation_m = static_cast<double>(cell.filled_elevation_m);
    if (cell.has_downstream) {
        const auto& downstream = topology.cell(cell.downstream_coord);
        if (!std::isfinite(downstream.filled_elevation_m)) {
            throw std::invalid_argument("continental topology contains non-finite downstream filled elevation");
        }
        diagonal = cell.coord.x != downstream.coord.x && cell.coord.y != downstream.coord.y;
        downstream_filled_elevation_m = static_cast<double>(downstream.filled_elevation_m);
    }

    const double length_cells = diagonal ? kSqrt2 : 1.0;
    ChannelTransportProperties out;
    out.reach_length_m = length_cells * static_cast<double>(topology.config.climate_cell_m);
    const double drop_m = cell.has_downstream
        ? std::max(0.0, static_cast<double>(cell.filled_elevation_m) - downstream_filled_elevation_m)
        : 0.0;
    out.downhill_gradient = drop_m / out.reach_length_m;

    const double slope_ratio =
        std::max(out.downhill_gradient, kChannelSlopeFloor) / kChannelSlopeFloor;
    const double discharge_m3_s = std::max(
        static_cast<double>(cell.accumulated_discharge_m3_s),
        kChannelDischargeFloorM3S);
    const double discharge_ratio = discharge_m3_s / kChannelReferenceDischargeM3S;
    const double unbounded_residence_days =
        length_cells *
        std::pow(slope_ratio, kChannelSlopeExponent) *
        std::pow(discharge_ratio, kChannelDischargeExponent);
    out.residence_days = std::clamp(
        unbounded_residence_days,
        kChannelMinResidenceDays,
        kChannelMaxResidenceDays);
    out.release_fraction_per_day = 1.0 - std::exp(-1.0 / out.residence_days);
    if (!(out.reach_length_m > 0.0) || !finite_non_negative(out.downhill_gradient) ||
        !(out.residence_days > 0.0) || !std::isfinite(out.residence_days) ||
        !(out.release_fraction_per_day > 0.0) || !(out.release_fraction_per_day < 1.0)) {
        throw std::invalid_argument("continental topology produced invalid channel transport geometry");
    }
    return out;
}

void add_channel_volume(double& target, double volume, const char* message) {
    if (!finite_non_negative(target) || !finite_non_negative(volume) ||
        target > std::numeric_limits<double>::max() - volume) {
        throw std::overflow_error(message);
    }
    target += volume;
}

std::size_t local_index(const HydrologyResult& h, CellCoord c) {
    const auto dx = c.x - h.request.min_coord.x;
    const auto dy = c.y - h.request.min_coord.y;
    if (dx < 0 || dy < 0 || dx >= static_cast<std::int64_t>(h.request.width_cells) ||
        dy >= static_cast<std::int64_t>(h.request.height_cells)) return kNoIndex;
    return static_cast<std::size_t>(dy) * h.request.width_cells + static_cast<std::size_t>(dx);
}

bool rect_intersects_world(CellCoord coord, std::int32_t cell_m, const WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = static_cast<double>(coord.x) * s;
    const double y0 = static_cast<double>(coord.y) * s;
    const double x1 = x0 + s;
    const double y1 = y0 + s;
    return x1 > b.origin_x_m && x0 < b.origin_x_m + b.width_m &&
           y1 > b.origin_y_m && y0 < b.origin_y_m + b.height_m;
}

struct EdgeConnection {
    CellCoord source{};
    CellCoord destination{};
    bool valid{};
};

EdgeConnection choose_tile_connection(const World& world, CellCoord from_climate, CellCoord to_climate) {
    const auto ratio = static_cast<std::int64_t>(world.config().climate_cell_m / world.config().regional_cell_m);
    const auto dx = to_climate.x - from_climate.x;
    const auto dy = to_climate.y - from_climate.y;
    if (ratio != 8 || dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0)) {
        throw std::invalid_argument("multiresolution coarse downstream cells must be D8-adjacent");
    }
    const CellCoord base{from_climate.x * ratio, from_climate.y * ratio};
    EdgeConnection best;
    double best_cost = std::numeric_limits<double>::infinity();

    auto consider = [&](CellCoord src, CellCoord dst) {
        if (!rect_intersects_world(src, world.config().regional_cell_m, world.config().bounds) ||
            !rect_intersects_world(dst, world.config().regional_cell_m, world.config().bounds)) return;
        const auto src_e = world.sample_region(src).elevation_m;
        const auto dst_e = world.sample_region(dst).elevation_m;
        const double cost = std::max(static_cast<double>(src_e), static_cast<double>(dst_e));
        if (!best.valid || cost < best_cost ||
            (cost == best_cost && (src.y < best.source.y ||
             (src.y == best.source.y && src.x < best.source.x)))) {
            best = {src, dst, true};
            best_cost = cost;
        }
    };

    if (dx != 0 && dy != 0) {
        const CellCoord src{base.x + (dx > 0 ? ratio - 1 : 0),
                            base.y + (dy > 0 ? ratio - 1 : 0)};
        consider(src, {src.x + dx, src.y + dy});
    } else if (dx != 0) {
        const auto sx = base.x + (dx > 0 ? ratio - 1 : 0);
        for (std::int64_t j = 0; j < ratio; ++j) {
            const CellCoord src{sx, base.y + j};
            consider(src, {src.x + dx, src.y});
        }
    } else {
        const auto sy = base.y + (dy > 0 ? ratio - 1 : 0);
        for (std::int64_t j = 0; j < ratio; ++j) {
            const CellCoord src{base.x + j, sy};
            consider(src, {src.x, src.y + dy});
        }
    }
    return best;
}

void validate_topology_alignment(const ContinentalHydrologyResult& topology,
                                 const MultiresolutionWaterState& state) {
    if (!same_config_identity(topology.config, state.config()) ||
        topology.min_coord != state.coarse_state().min_coord() ||
        topology.width_cells != state.coarse_state().width_cells() ||
        topology.height_cells != state.coarse_state().height_cells() ||
        topology.cells.size() != state.coarse_state().cells().size()) {
        throw std::invalid_argument("continental topology does not match multiresolution water state");
    }
}

void validate_world_state(const World& world, const MultiresolutionWaterState& state) {
    if (!same_config_identity(world.config(), state.config())) {
        throw std::invalid_argument("multiresolution water state belongs to a different world");
    }
    state.parameters().validate();
}

double advance_coarse_bucket(ContinentalWaterCellState& s,
                             const ContinentalWaterForcing& f,
                             double area_m2,
                             const DynamicHydrologyParameters& parameters,
                             const detail::SoilBucketParameters& soil,
                             ContinentalWaterStepReport& report) {
    s.last_evapotranspiration_mm = 0.0f;
    s.last_quick_runoff_mm = 0.0f;
    s.last_baseflow_mm = 0.0f;
    s.last_routed_discharge_m3_s = 0.0f;

    const double precipitation = f.precipitation_mm;
    const double temperature = f.mean_air_temperature_c;
    const double pet = f.potential_evapotranspiration_mm;
    report.precipitation_m3 += depth_to_volume(precipitation, area_m2);

    const double snow_fraction = std::clamp((1.0 - temperature) * 0.5, 0.0, 1.0);
    const double snowfall = precipitation * snow_fraction;
    const double rainfall = precipitation - snowfall;
    s.snow_water_equivalent_mm += static_cast<float>(snowfall);
    const double melt = std::min(static_cast<double>(s.snow_water_equivalent_mm),
        std::max(0.0, temperature) * parameters.snow_melt_mm_per_c_day);
    s.snow_water_equivalent_mm -= static_cast<float>(melt);
    s.surface_water_mm += static_cast<float>(rainfall + melt);

    const double saturation = soil.soil_capacity_mm > 0.0
        ? std::clamp(static_cast<double>(s.soil_water_mm) / soil.soil_capacity_mm, 0.0, 1.0)
        : 1.0;
    const double infiltration_capacity = soil.infiltration_capacity_mm_per_day *
                                         (0.25 + 0.75 * (1.0 - saturation));
    const double soil_room = std::max(0.0, soil.soil_capacity_mm - s.soil_water_mm);
    const double infiltration = std::min({static_cast<double>(s.surface_water_mm),
                                          infiltration_capacity,
                                          soil_room});
    s.surface_water_mm -= static_cast<float>(infiltration);
    s.soil_water_mm += static_cast<float>(infiltration);

    const double surface_evap = std::min(static_cast<double>(s.surface_water_mm), pet * 0.35);
    s.surface_water_mm -= static_cast<float>(surface_evap);
    const double remaining_pet = pet - surface_evap;
    const double moisture_span = std::max(1e-6, soil.field_capacity_mm - soil.wilting_point_mm);
    const double stress = std::clamp(
        (static_cast<double>(s.soil_water_mm) - soil.wilting_point_mm) / moisture_span,
        0.0, 1.0);
    const double soil_et = std::min(static_cast<double>(s.soil_water_mm), remaining_pet * stress);
    s.soil_water_mm -= static_cast<float>(soil_et);
    const double et = surface_evap + soil_et;
    s.last_evapotranspiration_mm = static_cast<float>(et);
    report.evapotranspiration_m3 += depth_to_volume(et, area_m2);

    const double excess_soil = std::max(0.0,
        static_cast<double>(s.soil_water_mm) - soil.field_capacity_mm);
    const double percolation_fraction = 1.0 - std::exp(-parameters.percolation_rate_per_day);
    const double percolation = excess_soil * percolation_fraction;
    s.soil_water_mm -= static_cast<float>(percolation);
    s.groundwater_mm += static_cast<float>(percolation);

    const double baseflow_fraction = 1.0 - std::exp(-parameters.groundwater_recession_per_day);
    const double baseflow = static_cast<double>(s.groundwater_mm) * baseflow_fraction;
    s.groundwater_mm -= static_cast<float>(baseflow);
    s.last_baseflow_mm = static_cast<float>(baseflow);

    const double quick_runoff = std::max(0.0,
        static_cast<double>(s.surface_water_mm) - parameters.surface_storage_capacity_mm);
    s.surface_water_mm -= static_cast<float>(quick_runoff);
    s.last_quick_runoff_mm = static_cast<float>(quick_runoff);
    return depth_to_volume(quick_runoff + baseflow, area_m2);
}

struct PendingRefined {
    CellCoord climate_coord{};
    std::vector<DynamicHydrologyCellState> cells;
};

} // namespace

std::size_t RefinedWaterTileState::index_of(CellCoord coord) const {
    constexpr std::int64_t ratio = 8;
    if (climate_coord.x > std::numeric_limits<std::int64_t>::max() / ratio ||
        climate_coord.x < std::numeric_limits<std::int64_t>::min() / ratio ||
        climate_coord.y > std::numeric_limits<std::int64_t>::max() / ratio ||
        climate_coord.y < std::numeric_limits<std::int64_t>::min() / ratio) {
        throw std::out_of_range("refined water tile coordinate is not representable");
    }
    const CellCoord min{climate_coord.x * ratio, climate_coord.y * ratio};
    if (coord.x < min.x || coord.y < min.y) {
        throw std::out_of_range("refined water coordinate is outside tile");
    }
    const auto dx = static_cast<std::uint64_t>(coord.x) - static_cast<std::uint64_t>(min.x);
    const auto dy = static_cast<std::uint64_t>(coord.y) - static_cast<std::uint64_t>(min.y);
    if (dx >= static_cast<std::uint64_t>(ratio) || dy >= static_cast<std::uint64_t>(ratio)) {
        throw std::out_of_range("refined water coordinate is outside tile");
    }
    return static_cast<std::size_t>(dy) * 8U + static_cast<std::size_t>(dx);
}

const DynamicHydrologyCellState& RefinedWaterTileState::cell(CellCoord coord) const {
    return cells.at(index_of(coord));
}

bool MultiresolutionWaterState::is_refined(CellCoord climate_coord) const noexcept {
    return refined_.find(climate_coord) != refined_.end();
}

const RefinedWaterTileState& MultiresolutionWaterState::refined_tile(CellCoord climate_coord) const {
    const auto it = refined_.find(climate_coord);
    if (it == refined_.end()) throw std::out_of_range("L0 cell is not refined");
    return it->second.state;
}

double MultiresolutionWaterState::channel_storage_m3(CellCoord climate_coord) const {
    return channel_storage_m3_.at(coarse_.index_of(climate_coord));
}

double MultiresolutionWaterState::total_channel_storage_m3() const noexcept {
    double total = 0.0;
    for (const double volume : channel_storage_m3_) total += volume;
    return total;
}

const ChannelTransportProperties& MultiresolutionWaterState::channel_transport(
    CellCoord climate_coord) const {
    return channel_transport_.at(coarse_.index_of(climate_coord));
}

ContinentalWaterCellState& MultiresolutionWaterState::coarse_cell_mutable(std::size_t index) noexcept {
    return coarse_.cells_[index];
}

double MultiresolutionWaterState::coarse_area_m2(std::size_t index) const noexcept {
    return coarse_.metadata_[index].area_m2;
}

SoilProperties MultiresolutionWaterState::coarse_soil_properties(std::size_t index) const noexcept {
    return {coarse_.metadata_[index].soil_storage_capacity_scale,
            coarse_.metadata_[index].soil_infiltration_capacity_scale};
}

std::uint32_t MultiresolutionWaterState::coarse_downstream_index(std::size_t index) const noexcept {
    return coarse_.metadata_[index].downstream_index;
}

bool MultiresolutionWaterState::coarse_is_ocean(std::size_t index) const noexcept {
    return (coarse_.metadata_[index].flags & 1u) != 0;
}

const std::vector<std::uint32_t>& MultiresolutionWaterState::coarse_routing_order() const noexcept {
    return coarse_.routing_order_;
}

void MultiresolutionWaterState::set_simulated_day(std::int64_t day) noexcept {
    coarse_.simulated_day_ = day;
}

double MultiresolutionWaterState::total_storage_m3(const World& world) const {
    if (channel_storage_m3_.size() != coarse_.cells_.size()) {
        throw std::logic_error("multiresolution channel storage shape is inconsistent");
    }
    double total = coarse_.total_storage_m3();
    std::vector<const RefinedTile*> ordered;
    ordered.reserve(refined_.size());
    for (const auto& entry : refined_) ordered.push_back(&entry.second);
    std::sort(ordered.begin(), ordered.end(), [](const RefinedTile* a, const RefinedTile* b) {
        if (a->state.climate_coord.y != b->state.climate_coord.y) {
            return a->state.climate_coord.y < b->state.climate_coord.y;
        }
        return a->state.climate_coord.x < b->state.climate_coord.x;
    });
    for (const auto* tile : ordered) {
        for (std::size_t i = 0; i < tile->state.cells.size(); ++i) {
            const auto& cell = tile->state.cells[i];
            if (!cell.active || tile->topology.hydrology.cells[i].ocean) continue;
            const double area = overlap_area_m2(
                cell.coord, world.config().regional_cell_m, world.config().bounds);
            const double volume = depth_to_volume(stored_depth_mm(cell), area);
            if (!finite_non_negative(volume) || total > std::numeric_limits<double>::max() - volume) {
                throw std::overflow_error("multiresolution terrestrial storage total overflow");
            }
            total += volume;
        }
    }
    for (std::size_t i = 0; i < channel_storage_m3_.size(); ++i) {
        const double volume = channel_storage_m3_[i];
        if (!finite_non_negative(volume) || (coarse_is_ocean(i) && volume != 0.0) ||
            total > std::numeric_limits<double>::max() - volume) {
            throw std::invalid_argument("multiresolution channel storage is invalid");
        }
        total += volume;
    }
    return total;
}

MultiresolutionWaterState make_multiresolution_water_state(
    const World& world,
    const ContinentalHydrologyResult& topology,
    const DynamicHydrologyParameters& parameters) {
    parameters.validate();
    MultiresolutionWaterState state;
    state.coarse_ = make_continental_water_state(world, topology, parameters);
    state.parameters_ = parameters;
    state.channel_storage_m3_.assign(state.coarse_.cells_.size(), 0.0);
    state.channel_transport_.reserve(topology.cells.size());
    for (std::size_t i = 0; i < topology.cells.size(); ++i) {
        state.channel_transport_.push_back(derive_channel_transport(topology, i));
    }
    return state;
}

const RefinedWaterTileState& materialize_refined_water_tile(
    const World& world,
    const ContinentalHydrologyResult& topology,
    MultiresolutionWaterState& state,
    CellCoord climate_coord) {
    validate_world_state(world, state);
    validate_topology_alignment(topology, state);
    if (const auto it = state.refined_.find(climate_coord); it != state.refined_.end()) {
        if (it->second.state.simulated_day != state.simulated_day()) {
            throw std::logic_error("refined water tile clock does not match global day");
        }
        return it->second.state;
    }

    const auto coarse_index = state.coarse_state().index_of(climate_coord);
    const auto topology_index = topology.index_of(climate_coord);
    if (coarse_index != topology_index || topology.cells[topology_index].coord != climate_coord) {
        throw std::invalid_argument("requested refinement parent does not match continental topology");
    }
    const auto downstream = state.coarse_downstream_index(coarse_index);
    if (topology.cells[topology_index].has_downstream != (downstream != kNoDownstream) ||
        (downstream != kNoDownstream && topology.index_of(topology.cells[topology_index].downstream_coord) != downstream)) {
        throw std::invalid_argument("requested refinement parent has mismatched downstream topology");
    }

    auto refined_topology = world.refine_authoritative_hydrology_tile(topology, climate_coord);
    MultiresolutionWaterState::RefinedTile refined;
    refined.topology = std::move(refined_topology);
    refined.state.config = world.config();
    refined.state.climate_coord = climate_coord;
    refined.state.simulated_day = state.simulated_day();
    refined.state.cells.resize(refined.topology.hydrology.cells.size());

    const auto& source = state.coarse_state().cells()[coarse_index];
    require_valid_storage(source);
    const auto parent_soil = detail::scaled_soil_bucket_parameters(
        state.parameters(), state.coarse_soil_properties(coarse_index));
    if (!detail::soil_water_within_capacity(source.soil_water_mm, parent_soil.soil_capacity_mm)) {
        throw std::logic_error("coarse soil water exceeds parent soil capacity");
    }
    const double parent_saturation = parent_soil.soil_capacity_mm > 0.0
        ? std::clamp(static_cast<double>(source.soil_water_mm) / parent_soil.soil_capacity_mm, 0.0, 1.0)
        : 0.0;

    double child_area = 0.0;
    for (std::size_t i = 0; i < refined.state.cells.size(); ++i) {
        const auto& topo_cell = refined.topology.hydrology.cells[i];
        auto& target = refined.state.cells[i];
        target.coord = topo_cell.coord;
        target.active = topo_cell.active;
        if (!topo_cell.active) continue;
        const double area = overlap_area_m2(
            topo_cell.coord, world.config().regional_cell_m, world.config().bounds);
        if (!(area > 0.0)) throw std::logic_error("active refined cell has zero world overlap");
        child_area += area;
        if (topo_cell.ocean) continue;

        const auto child_soil = detail::scaled_soil_bucket_parameters(
            state.parameters(), world.sample_soil(topo_cell.coord));
        target.snow_water_equivalent_mm = source.snow_water_equivalent_mm;
        target.surface_water_mm = source.surface_water_mm;
        target.soil_water_mm = static_cast<float>(parent_saturation * child_soil.soil_capacity_mm);
        target.groundwater_mm = source.groundwater_mm;
        if (!detail::soil_water_within_capacity(target.soil_water_mm, child_soil.soil_capacity_mm)) {
            throw std::logic_error("refinement produced soil water above child capacity");
        }
    }

    const double parent_area = state.coarse_area_m2(coarse_index);
    const double area_tolerance = std::max(1e-6, parent_area * 1e-12);
    if (std::abs(child_area - parent_area) > area_tolerance) {
        throw std::logic_error("refined child areas do not conserve parent world overlap area");
    }
    if (state.coarse_is_ocean(coarse_index) && stored_depth_mm(source) != 0.0) {
        throw std::logic_error("ocean coarse cell unexpectedly owns terrestrial water");
    }

    const auto [it, inserted] = state.refined_.emplace(climate_coord, std::move(refined));
    if (!inserted) throw std::logic_error("refined water tile insertion unexpectedly collided");

    auto& coarse = state.coarse_cell_mutable(coarse_index);
    coarse = {};
    return it->second.state;
}

void aggregate_refined_water_tile(
    const World& world,
    MultiresolutionWaterState& state,
    CellCoord climate_coord) {
    validate_world_state(world, state);
    const auto it = state.refined_.find(climate_coord);
    if (it == state.refined_.end()) throw std::invalid_argument("cannot aggregate an unrefined L0 cell");
    const auto& refined = it->second;
    if (refined.state.simulated_day != state.simulated_day()) {
        throw std::invalid_argument("refined water tile clock does not match global day");
    }

    const auto coarse_index = state.coarse_state().index_of(climate_coord);
    const auto& coarse_before = state.coarse_state().cells()[coarse_index];
    if (stored_depth_mm(coarse_before) != 0.0) {
        throw std::logic_error("refined parent still owns independent coarse water");
    }

    double snow_m3 = 0.0;
    double surface_m3 = 0.0;
    double soil_m3 = 0.0;
    double groundwater_m3 = 0.0;
    for (std::size_t i = 0; i < refined.state.cells.size(); ++i) {
        const auto& cell = refined.state.cells[i];
        require_valid_storage(cell);
        if (!cell.active || refined.topology.hydrology.cells[i].ocean) continue;
        const auto child_soil = detail::scaled_soil_bucket_parameters(
            state.parameters(), world.sample_soil(cell.coord));
        if (!detail::soil_water_within_capacity(cell.soil_water_mm, child_soil.soil_capacity_mm)) {
            throw std::logic_error("refined soil water exceeds local child capacity");
        }
        const double area = overlap_area_m2(
            cell.coord, world.config().regional_cell_m, world.config().bounds);
        snow_m3 += depth_to_volume(cell.snow_water_equivalent_mm, area);
        surface_m3 += depth_to_volume(cell.surface_water_mm, area);
        soil_m3 += depth_to_volume(cell.soil_water_mm, area);
        groundwater_m3 += depth_to_volume(cell.groundwater_mm, area);
    }

    const double parent_area = state.coarse_area_m2(coarse_index);
    ContinentalWaterCellState aggregated;
    aggregated.snow_water_equivalent_mm = static_cast<float>(volume_to_depth(snow_m3, parent_area));
    aggregated.surface_water_mm = static_cast<float>(volume_to_depth(surface_m3, parent_area));
    aggregated.soil_water_mm = static_cast<float>(volume_to_depth(soil_m3, parent_area));
    aggregated.groundwater_mm = static_cast<float>(volume_to_depth(groundwater_m3, parent_area));
    require_valid_storage(aggregated);
    const auto parent_soil = detail::scaled_soil_bucket_parameters(
        state.parameters(), state.coarse_soil_properties(coarse_index));
    if (!detail::soil_water_within_capacity(aggregated.soil_water_mm, parent_soil.soil_capacity_mm)) {
        throw std::logic_error("aggregated soil water exceeds coarse parent capacity");
    }

    state.coarse_cell_mutable(coarse_index) = aggregated;
    state.refined_.erase(it);
}

std::vector<HydrometeorologicalForcing> derive_refined_atmospheric_forcing(
    const World& world,
    const MultiresolutionWaterState& state,
    CellCoord climate_coord,
    const ContinentalWaterForcing& parent_forcing) {
    validate_world_state(world, state);
    if (!finite_non_negative(parent_forcing.precipitation_mm) ||
        !std::isfinite(parent_forcing.mean_air_temperature_c) ||
        !finite_non_negative(parent_forcing.potential_evapotranspiration_mm)) {
        throw std::invalid_argument("parent atmospheric forcing contains invalid values");
    }

    const auto it = state.refined_.find(climate_coord);
    if (it == state.refined_.end()) {
        throw std::invalid_argument("cannot derive L1 forcing for an unrefined L0 parent");
    }
    const auto& refined = it->second;
    if (refined.state.cells.size() != refined.topology.hydrology.cells.size()) {
        throw std::logic_error("refined forcing topology/state shape is inconsistent");
    }

    std::vector<HydrometeorologicalForcing> out(refined.state.cells.size());
    std::vector<double> areas(out.size(), 0.0);
    std::vector<double> elevations(out.size(), 0.0);
    double total_area = 0.0;
    double elevation_area_sum = 0.0;
    std::size_t correction_index = kNoIndex;
    double correction_area = 0.0;

    for (std::size_t j = 0; j < out.size(); ++j) {
        const auto& cell = refined.state.cells[j];
        const auto& topo = refined.topology.hydrology.cells[j];
        out[j].coord = cell.coord;
        if (cell.coord != topo.coord || cell.active != topo.active) {
            throw std::logic_error("refined forcing topology/state cells are misaligned");
        }
        if (!cell.active || topo.ocean) continue;
        if (!std::isfinite(topo.terrain_elevation_m)) {
            throw std::invalid_argument("refined atmospheric forcing terrain elevation is invalid");
        }
        const double area = overlap_area_m2(
            cell.coord, world.config().regional_cell_m, world.config().bounds);
        if (!(area > 0.0) || !std::isfinite(area)) {
            throw std::logic_error("active refined forcing cell has zero world overlap");
        }
        areas[j] = area;
        elevations[j] = static_cast<double>(topo.terrain_elevation_m);
        total_area += area;
        elevation_area_sum += elevations[j] * area;
        if (area > correction_area) {
            correction_area = area;
            correction_index = j;
        }
    }

    // An ocean parent can be refined for topology/query symmetry but receives no terrestrial forcing.
    if (!(total_area > 0.0)) return out;
    if (!std::isfinite(total_area) || !std::isfinite(elevation_area_sum) ||
        correction_index == kNoIndex) {
        throw std::overflow_error("refined atmospheric forcing area accumulation overflow");
    }

    const double mean_elevation_m = elevation_area_sum / total_area;
    double raw_weight_area_sum = 0.0;
    std::vector<double> raw_weights(out.size(), 0.0);
    for (std::size_t j = 0; j < out.size(); ++j) {
        if (!(areas[j] > 0.0)) continue;
        const double elevation_delta_km = (elevations[j] - mean_elevation_m) / 1000.0;
        raw_weights[j] = std::clamp(
            1.0 + kOrographicPrecipitationPerKm * elevation_delta_km,
            kOrographicMinMultiplier,
            kOrographicMaxMultiplier);
        raw_weight_area_sum += raw_weights[j] * areas[j];
    }
    if (!(raw_weight_area_sum > 0.0) || !std::isfinite(raw_weight_area_sum)) {
        throw std::overflow_error("refined atmospheric precipitation normalization overflow");
    }

    const double parent_reference_elevation_m = static_cast<double>(world.sample_elevation(
        overlap_center(climate_coord, world.config().climate_cell_m, world.config().bounds)));
    if (!std::isfinite(parent_reference_elevation_m)) {
        throw std::invalid_argument("parent atmospheric reference elevation is invalid");
    }
    const double parent_effective_elevation_m = std::max(0.0, parent_reference_elevation_m);
    const double normalization = total_area / raw_weight_area_sum;

    for (std::size_t j = 0; j < out.size(); ++j) {
        if (!(areas[j] > 0.0)) continue;
        const double precipitation_mm =
            static_cast<double>(parent_forcing.precipitation_mm) * raw_weights[j] * normalization;
        const double child_effective_elevation_m = std::max(0.0, elevations[j]);
        const double temperature_correction = std::clamp(
            -kAtmosphericLapseRateCPerM *
                (child_effective_elevation_m - parent_effective_elevation_m),
            -kAtmosphericMaxTemperatureCorrectionC,
            kAtmosphericMaxTemperatureCorrectionC);
        const double temperature_c =
            static_cast<double>(parent_forcing.mean_air_temperature_c) + temperature_correction;
        const double pet_mm = std::max(0.0, 0.10 * (temperature_c + 5.0));
        if (!finite_non_negative(precipitation_mm) || !std::isfinite(temperature_c) ||
            !finite_non_negative(pet_mm) ||
            precipitation_mm > static_cast<double>(std::numeric_limits<float>::max()) ||
            std::abs(temperature_c) > static_cast<double>(std::numeric_limits<float>::max()) ||
            pet_mm > static_cast<double>(std::numeric_limits<float>::max())) {
            throw std::overflow_error("refined atmospheric forcing exceeds finite float range");
        }
        out[j].precipitation_mm = static_cast<float>(precipitation_mm);
        out[j].mean_air_temperature_c = static_cast<float>(temperature_c);
        out[j].potential_evapotranspiration_mm = static_cast<float>(pet_mm);
    }

    // Normalization is performed in double, while the public forcing ABI uses float depths.
    // Correct the largest-overlap child to the closest representable float so the aggregate
    // parent precipitation volume is conserved to float forcing precision, including partial
    // world-boundary parents.
    const double target_depth_area =
        static_cast<double>(parent_forcing.precipitation_mm) * total_area;
    double actual_depth_area = 0.0;
    for (std::size_t j = 0; j < out.size(); ++j) {
        actual_depth_area += static_cast<double>(out[j].precipitation_mm) * areas[j];
    }
    const double residual = target_depth_area - actual_depth_area;
    const double desired = static_cast<double>(out[correction_index].precipitation_mm) +
                           residual / correction_area;
    if (!std::isfinite(desired) || desired < 0.0 ||
        desired > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::overflow_error("refined atmospheric precipitation residual is not representable");
    }

    const float base = static_cast<float>(desired);
    const std::array<float, 3> candidates{
        base,
        std::nextafter(base, std::numeric_limits<float>::infinity()),
        std::nextafter(base, -std::numeric_limits<float>::infinity())};
    float best = out[correction_index].precipitation_mm;
    double best_error = std::numeric_limits<double>::infinity();
    const double without_correction =
        actual_depth_area -
        static_cast<double>(out[correction_index].precipitation_mm) * correction_area;
    for (const float candidate : candidates) {
        if (!finite_non_negative(candidate)) continue;
        const double candidate_total =
            without_correction + static_cast<double>(candidate) * correction_area;
        const double error = std::abs(target_depth_area - candidate_total);
        if (error < best_error) {
            best_error = error;
            best = candidate;
        }
    }
    out[correction_index].precipitation_mm = best;
    return out;
}

ContinentalWaterStepReport advance_multiresolution_water_day(
    const World& world,
    MultiresolutionWaterState& state,
    const std::vector<ContinentalWaterForcing>& forcing) {
    return advance_multiresolution_water_day(world, state, forcing, {});
}

ContinentalWaterStepReport advance_multiresolution_water_day(
    const World& world, MultiresolutionWaterState& state,
    const std::vector<ContinentalWaterForcing>& forcing,
    const std::vector<float>& evapotranspiration_factors) {
    validate_world_state(world, state);
    if (!evapotranspiration_factors.empty() && evapotranspiration_factors.size() != forcing.size()) {
        throw std::invalid_argument("canopy factors must match the L0 grid");
    }
    for (const float factor : evapotranspiration_factors) {
        if (!std::isfinite(factor) || factor < 0.0f || factor > 1.0f)
            throw std::invalid_argument("canopy factor must be finite and in [0,1]");
    }
    if (state.simulated_day() == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("multiresolution water simulation day overflow");
    }
    if (forcing.size() != state.coarse_state().cells().size()) {
        throw std::invalid_argument("multiresolution forcing must contain exactly one record per L0 cell");
    }
    if (state.channel_storage_m3_.size() != forcing.size() ||
        state.channel_transport_.size() != forcing.size()) {
        throw std::logic_error("multiresolution channel state shape is inconsistent");
    }

    ContinentalWaterStepReport report;
    report.day_before = state.simulated_day();
    report.day_after = state.simulated_day() + 1;
    report.storage_before_m3 = state.total_storage_m3(world);
    if (!std::isfinite(report.storage_before_m3)) {
        throw std::invalid_argument("multiresolution storage total is not finite");
    }

    std::vector<detail::SoilBucketParameters> coarse_soil_buckets(forcing.size());
    double precipitation_upper_m3 = 0.0;
    for (std::size_t i = 0; i < forcing.size(); ++i) {
        const auto& f = forcing[i];
        if (!finite_non_negative(f.precipitation_mm) || !std::isfinite(f.mean_air_temperature_c) ||
            !finite_non_negative(f.potential_evapotranspiration_mm)) {
            throw std::invalid_argument("multiresolution water forcing contains invalid values");
        }
        const double channel = state.channel_storage_m3_[i];
        if (!finite_non_negative(channel) || (state.coarse_is_ocean(i) && channel != 0.0)) {
            throw std::invalid_argument("multiresolution channel storage is invalid");
        }
        if (state.coarse_is_ocean(i)) continue;
        coarse_soil_buckets[i] = detail::scaled_soil_bucket_parameters(
            state.parameters(), state.coarse_soil_properties(i));
        const auto coord = state.coarse_state().coord_of(i);
        if (state.is_refined(coord)) {
            const auto& tile = state.refined_.at(coord);
            if (tile.state.simulated_day != state.simulated_day()) {
                throw std::invalid_argument("refined water tile clock does not match global day");
            }
            if (stored_depth_mm(state.coarse_state().cells()[i]) != 0.0) {
                throw std::logic_error("refined parent still owns independent coarse water");
            }
            for (std::size_t j = 0; j < tile.state.cells.size(); ++j) {
                const auto& child = tile.state.cells[j];
                require_valid_storage(child);
                if (!child.active || tile.topology.hydrology.cells[j].ocean) continue;
                const auto child_soil = detail::scaled_soil_bucket_parameters(
                    state.parameters(), world.sample_soil(child.coord));
                if (!detail::soil_water_within_capacity(child.soil_water_mm, child_soil.soil_capacity_mm)) {
                    throw std::invalid_argument("refined soil water exceeds local child capacity");
                }
                if (stored_depth_mm(child) + f.precipitation_mm > kMaxWaterDepthMm) {
                    throw std::invalid_argument("refined water depth exceeds numerical safety limit");
                }
            }
        } else {
            const auto& coarse = state.coarse_state().cells()[i];
            require_valid_storage(coarse);
            if (!detail::soil_water_within_capacity(coarse.soil_water_mm,
                                                    coarse_soil_buckets[i].soil_capacity_mm)) {
                throw std::invalid_argument("coarse soil water exceeds local parent capacity");
            }
            if (stored_depth_mm(coarse) + f.precipitation_mm > kMaxWaterDepthMm) {
                throw std::invalid_argument("coarse water depth exceeds numerical safety limit");
            }
        }
        const double precip_volume = depth_to_volume(f.precipitation_mm, state.coarse_area_m2(i));
        if (!std::isfinite(precip_volume) ||
            precipitation_upper_m3 > std::numeric_limits<double>::max() - precip_volume) {
            throw std::invalid_argument("multiresolution precipitation volume exceeds numerical safety limit");
        }
        precipitation_upper_m3 += precip_volume;
    }
    if ((report.storage_before_m3 + precipitation_upper_m3) / kSecondsPerDay >
        static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::invalid_argument("multiresolution routed discharge exceeds float diagnostic range");
    }

    std::vector<ContinentalWaterCellState> coarse_next = state.coarse_state().cells();
    std::vector<double> local_runoff(coarse_next.size(), 0.0);
    std::vector<double> channel_next = state.channel_storage_m3_;
    std::unordered_map<CellCoord, std::vector<double>, CellCoordHash> ingress;
    ingress.reserve(state.refined_.size());
    for (const auto& entry : state.refined_) {
        ingress.emplace(entry.first, std::vector<double>(entry.second.state.cells.size(), 0.0));
    }
    std::vector<PendingRefined> pending;
    pending.reserve(state.refined_.size());

    // Terrestrial bucket physics runs first. Its runoff is new channel water and therefore
    // cannot be released until a later day.
    for (std::size_t i = 0; i < coarse_next.size(); ++i) {
        const auto coord = state.coarse_state().coord_of(i);
        auto& c = coarse_next[i];
        c.last_evapotranspiration_mm = 0.0f;
        c.last_quick_runoff_mm = 0.0f;
        c.last_baseflow_mm = 0.0f;
        c.last_routed_discharge_m3_s = 0.0f;
        if (state.coarse_is_ocean(i) || state.is_refined(coord)) continue;
        auto effective_forcing = forcing[i];
        if (!evapotranspiration_factors.empty())
            effective_forcing.potential_evapotranspiration_mm *= evapotranspiration_factors[i];
        local_runoff[i] = advance_coarse_bucket(
            c, effective_forcing, state.coarse_area_m2(i), state.parameters(), coarse_soil_buckets[i], report);
    }

    // Release only channel storage that existed at the beginning of this day. Geometry changes
    // the fraction released by each L0 reach, but every released parcel still crosses at most one
    // L0 edge. A release entering a refined parent may traverse that parent's internal L1 graph;
    // its tile outlet returns to the parent's next channel store and cannot cross another L0 edge
    // until the next day.
    for (const auto index32 : state.coarse_routing_order()) {
        const auto i = static_cast<std::size_t>(index32);
        if (state.coarse_is_ocean(i)) continue;
        const auto coord = state.coarse_state().coord_of(i);
        const double old_channel = state.channel_storage_m3_[i];
        const double release = old_channel * state.channel_transport_[i].release_fraction_per_day;
        channel_next[i] -= release;
        coarse_next[i].last_routed_discharge_m3_s = static_cast<float>(release / kSecondsPerDay);
        if (release == 0.0) continue;

        const auto downstream = state.coarse_downstream_index(i);
        if (downstream == kNoDownstream) {
            add_channel_volume(report.terminal_outflow_m3, release, "terminal channel outflow overflow");
            continue;
        }
        const auto downstream_index = static_cast<std::size_t>(downstream);
        if (state.coarse_is_ocean(downstream_index)) {
            add_channel_volume(report.terminal_outflow_m3, release, "terminal channel outflow overflow");
            continue;
        }

        const auto downstream_coord = state.coarse_state().coord_of(downstream_index);
        if (state.is_refined(downstream_coord)) {
            const auto edge = choose_tile_connection(world, coord, downstream_coord);
            if (!edge.valid) throw std::logic_error("could not establish refined ingress connection");
            const auto& downstream_tile = state.refined_.at(downstream_coord);
            const auto child_index = local_index(downstream_tile.topology.hydrology, edge.destination);
            if (child_index == kNoIndex || !downstream_tile.state.cells[child_index].active) {
                throw std::logic_error("refined ingress targets an inactive child cell");
            }
            auto& target = ingress.at(downstream_coord)[child_index];
            add_channel_volume(target, release, "refined channel ingress volume overflow");
        } else {
            add_channel_volume(
                channel_next[downstream_index], release, "downstream channel storage overflow");
        }
    }

    // Current-day coarse runoff enters its own L0 channel after the release phase.
    for (std::size_t i = 0; i < local_runoff.size(); ++i) {
        if (local_runoff[i] > 0.0) {
            add_channel_volume(channel_next[i], local_runoff[i], "local channel storage overflow");
        }
    }

    // Refined parents receive current weather and any one-edge upstream channel releases.
    // Their detailed external outflow becomes new parent L0 channel storage.
    std::vector<const MultiresolutionWaterState::RefinedTile*> ordered_refined;
    ordered_refined.reserve(state.refined_.size());
    for (const auto& entry : state.refined_) ordered_refined.push_back(&entry.second);
    std::sort(ordered_refined.begin(), ordered_refined.end(), [](const auto* a, const auto* b) {
        if (a->state.climate_coord.y != b->state.climate_coord.y) {
            return a->state.climate_coord.y < b->state.climate_coord.y;
        }
        return a->state.climate_coord.x < b->state.climate_coord.x;
    });

    for (const auto* owned_ptr : ordered_refined) {
        const auto coord = owned_ptr->state.climate_coord;
        const auto i = state.coarse_state().index_of(coord);
        if (state.coarse_is_ocean(i)) continue;
        const auto& owned = state.refined_.at(coord);
        DynamicHydrologyTileState detailed;
        detailed.config = owned.state.config;
        detailed.climate_coord = coord;
        detailed.simulated_days = 0.0;
        detailed.cells = owned.state.cells;

        auto detailed_forcing = derive_refined_atmospheric_forcing(
            world, state, coord, forcing[i]);
        if (!evapotranspiration_factors.empty()) {
            for (auto& f : detailed_forcing)
                f.potential_evapotranspiration_mm *= evapotranspiration_factors[i];
        }

        std::vector<ExternalHydrologyInflow> external;
        const auto& by_cell = ingress.at(coord);
        for (std::size_t j = 0; j < by_cell.size(); ++j) {
            if (by_cell[j] > 0.0) external.push_back({detailed.cells[j].coord, by_cell[j]});
        }
        const auto detailed_report = advance_dynamic_hydrology_tile(
            world, owned.topology, detailed, detailed_forcing, external, 1.0, state.parameters());
        report.precipitation_m3 += detailed_report.precipitation_m3;
        report.evapotranspiration_m3 += detailed_report.evapotranspiration_m3;
        add_channel_volume(
            channel_next[i], detailed_report.external_outflow_m3, "refined outlet channel storage overflow");
        pending.push_back({coord, std::move(detailed.cells)});
    }

    double storage_after = 0.0;
    for (std::size_t i = 0; i < coarse_next.size(); ++i) {
        if (state.coarse_is_ocean(i)) continue;
        const double volume = depth_to_volume(stored_depth_mm(coarse_next[i]), state.coarse_area_m2(i));
        if (!finite_non_negative(volume) || storage_after > std::numeric_limits<double>::max() - volume) {
            throw std::overflow_error("coarse storage total overflow");
        }
        storage_after += volume;
    }
    for (const auto& next : pending) {
        const auto& topology = state.refined_.at(next.climate_coord).topology;
        for (std::size_t j = 0; j < next.cells.size(); ++j) {
            const auto& cell = next.cells[j];
            if (!cell.active || topology.hydrology.cells[j].ocean) continue;
            const double area = overlap_area_m2(
                cell.coord, world.config().regional_cell_m, world.config().bounds);
            const double volume = depth_to_volume(stored_depth_mm(cell), area);
            if (!finite_non_negative(volume) || storage_after > std::numeric_limits<double>::max() - volume) {
                throw std::overflow_error("refined storage total overflow");
            }
            storage_after += volume;
        }
    }
    for (std::size_t i = 0; i < channel_next.size(); ++i) {
        const double volume = channel_next[i];
        if (!finite_non_negative(volume) || (state.coarse_is_ocean(i) && volume != 0.0) ||
            storage_after > std::numeric_limits<double>::max() - volume) {
            throw std::overflow_error("channel storage total overflow");
        }
        storage_after += volume;
    }
    report.storage_after_m3 = storage_after;
    report.water_balance_error_m3 = report.storage_before_m3 + report.precipitation_m3 -
        report.evapotranspiration_m3 - report.terminal_outflow_m3 - report.storage_after_m3;

    for (std::size_t i = 0; i < coarse_next.size(); ++i) {
        state.coarse_cell_mutable(i) = coarse_next[i];
    }
    state.channel_storage_m3_ = std::move(channel_next);
    for (auto& next : pending) {
        auto& owned = state.refined_.at(next.climate_coord);
        owned.state.cells = std::move(next.cells);
        owned.state.simulated_day = report.day_after;
    }
    state.set_simulated_day(report.day_after);
    return report;
}

} // namespace worldsim
