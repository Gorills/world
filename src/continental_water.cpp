#include "worldsim/continental_water.hpp"

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
constexpr double kMaxWaterDepthMm = 1.0e30;
constexpr std::uint32_t kNoDownstream = 0xFFFFFFFFu;
constexpr std::uint32_t kOceanFlag = 1u;

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

bool finite_non_negative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

void validate_continental_parameters(const DynamicHydrologyParameters& parameters) {
    parameters.validate();
    const double initial_storage = static_cast<double>(parameters.initial_soil_water_mm) +
                                   static_cast<double>(parameters.initial_groundwater_mm);
    if (!std::isfinite(initial_storage) || initial_storage > kMaxWaterDepthMm) {
        throw std::invalid_argument("continental initial water storage exceeds numerical safety limit");
    }
}

void validate_topology(const World& world, const ContinentalHydrologyResult& topology) {
    if (!same_config_identity(world.config(), topology.config)) {
        throw std::invalid_argument("continental topology belongs to a different world configuration");
    }
    const auto expected = static_cast<std::uint64_t>(topology.width_cells) * topology.height_cells;
    if (topology.width_cells == 0 || topology.height_cells == 0 || expected != topology.cells.size()) {
        throw std::invalid_argument("continental topology dimensions do not match its cell storage");
    }
    if (topology.cells.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("continental topology exceeds uint32 routing index range");
    }
    for (std::size_t i = 0; i < topology.cells.size(); ++i) {
        const auto& cell = topology.cells[i];
        if (topology.index_of(cell.coord) != i) {
            throw std::invalid_argument("continental topology cell ordering invariant is broken");
        }
        if (cell.has_downstream) {
            const auto d = topology.index_of(cell.downstream_coord);
            if (d == i) throw std::invalid_argument("continental topology contains a self-loop");
        }
    }
}

std::vector<std::uint32_t> make_routing_order(const ContinentalHydrologyResult& topology) {
    const std::size_t count = topology.cells.size();
    std::vector<std::uint32_t> indegree(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& cell = topology.cells[i];
        if (!cell.has_downstream) continue;
        const auto d = topology.index_of(cell.downstream_coord);
        ++indegree[d];
    }

    std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> ready;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(count); ++i) {
        if (indegree[i] == 0) ready.push(i);
    }

    std::vector<std::uint32_t> order;
    order.reserve(count);
    while (!ready.empty()) {
        const auto i = ready.top();
        ready.pop();
        order.push_back(i);
        const auto& cell = topology.cells[i];
        if (!cell.has_downstream) continue;
        const auto d = topology.index_of(cell.downstream_coord);
        if (--indegree[d] == 0) ready.push(static_cast<std::uint32_t>(d));
    }
    if (order.size() != count) {
        throw std::invalid_argument("continental topology contains a drainage cycle");
    }
    return order;
}

} // namespace

double ContinentalWaterState::total_storage_m3() const {
    double total = 0.0;
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        if ((metadata_[i].flags & kOceanFlag) != 0) continue;
        const auto& c = cells_[i];
        const double depth = static_cast<double>(c.snow_water_equivalent_mm) +
                             c.surface_water_mm + c.soil_water_mm + c.groundwater_mm;
        total += depth_to_volume(depth, metadata_[i].area_m2);
    }
    return total;
}

std::size_t ContinentalWaterState::index_of(CellCoord coord) const {
    if (coord.x < min_coord_.x || coord.y < min_coord_.y) {
        throw std::out_of_range("continental water coordinate is outside state");
    }
    const auto dx = coord.x - min_coord_.x;
    const auto dy = coord.y - min_coord_.y;
    if (dx >= static_cast<std::int64_t>(width_cells_) ||
        dy >= static_cast<std::int64_t>(height_cells_)) {
        throw std::out_of_range("continental water coordinate is outside state");
    }
    return static_cast<std::size_t>(dy) * width_cells_ + static_cast<std::size_t>(dx);
}

CellCoord ContinentalWaterState::coord_of(std::size_t index) const {
    if (index >= cells_.size()) throw std::out_of_range("continental water index is outside state");
    const auto width = static_cast<std::size_t>(width_cells_);
    return {min_coord_.x + static_cast<std::int64_t>(index % width),
            min_coord_.y + static_cast<std::int64_t>(index / width)};
}

const ContinentalWaterCellState& ContinentalWaterState::cell(CellCoord coord) const {
    return cells_.at(index_of(coord));
}

ContinentalWaterState make_continental_water_state(
    const World& world,
    const ContinentalHydrologyResult& topology,
    const DynamicHydrologyParameters& parameters) {
    validate_continental_parameters(parameters);
    validate_topology(world, topology);

    ContinentalWaterState state;
    state.config_ = world.config();
    state.min_coord_ = topology.min_coord;
    state.width_cells_ = topology.width_cells;
    state.height_cells_ = topology.height_cells;
    state.cells_.resize(topology.cells.size());
    state.metadata_.resize(topology.cells.size());
    state.routing_order_ = make_routing_order(topology);

    for (std::size_t i = 0; i < state.cells_.size(); ++i) {
        const auto& topo = topology.cells[i];
        auto& meta = state.metadata_[i];
        meta.area_m2 = overlap_area_m2(topo.coord, world.config().climate_cell_m, world.config().bounds);
        if (!(meta.area_m2 > 0.0)) throw std::logic_error("L0 topology cell has zero world overlap area");
        if (topo.has_downstream) {
            meta.downstream_index = static_cast<std::uint32_t>(topology.index_of(topo.downstream_coord));
        }
        if (topo.ocean) {
            meta.flags |= kOceanFlag;
            continue;
        }

        const auto climate = world.sample_climate(topo.coord);
        meta.mean_temperature_at_elevation_c = static_cast<float>(
            static_cast<double>(climate.mean_temperature_c) -
            0.0065 * std::max(0.0f, topo.terrain_elevation_m));
        meta.annual_precipitation_mm = climate.annual_precipitation_mm;
        meta.continentality = climate.continentality;
        state.cells_[i].soil_water_mm = parameters.initial_soil_water_mm;
        state.cells_[i].groundwater_mm = parameters.initial_groundwater_mm;
    }
    return state;
}

std::vector<ContinentalWaterForcing> make_smooth_continental_daily_forcing(
    const ContinentalWaterState& state) {
    std::vector<ContinentalWaterForcing> forcing(state.cells_.size());
    const double day_of_year = std::fmod(static_cast<double>(state.simulated_day_), 365.2425);
    const double phase = 2.0 * kPi * ((day_of_year - 200.0) / 365.2425);
    const double precip_weight = 1.0 + 0.30 * std::cos(phase - 0.7);

    for (std::size_t i = 0; i < state.cells_.size(); ++i) {
        const auto& meta = state.metadata_[i];
        if ((meta.flags & kOceanFlag) != 0) continue;
        const double amplitude = 8.0 + 8.0 * static_cast<double>(meta.continentality);
        const double local_temperature = static_cast<double>(meta.mean_temperature_at_elevation_c) +
                                         amplitude * std::cos(phase);
        auto& out = forcing[i];
        out.precipitation_mm = static_cast<float>(
            static_cast<double>(meta.annual_precipitation_mm) / 365.2425 * precip_weight);
        out.mean_air_temperature_c = static_cast<float>(local_temperature);
        out.potential_evapotranspiration_mm = static_cast<float>(
            std::max(0.0, 0.10 * (local_temperature + 5.0)));
    }
    return forcing;
}

ContinentalWaterStepReport advance_continental_water_day(
    ContinentalWaterState& state,
    const std::vector<ContinentalWaterForcing>& forcing,
    const DynamicHydrologyParameters& parameters) {
    validate_continental_parameters(parameters);
    if (state.simulated_day_ == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("continental water simulation day overflow");
    }
    if (forcing.size() != state.cells_.size()) {
        throw std::invalid_argument("continental forcing must contain exactly one record per L0 cell");
    }

    // Validate the complete forcing and conservative numerical bounds before any mutation.
    // kMaxWaterDepthMm is intentionally many orders above physical use; it only prevents
    // formally finite float inputs from producing infinities in persistent state/diagnostics.
    double maximum_routed_volume_m3 = 0.0;
    for (std::size_t i = 0; i < forcing.size(); ++i) {
        const auto& f = forcing[i];
        if (!finite_non_negative(f.precipitation_mm) ||
            !std::isfinite(f.mean_air_temperature_c) ||
            !finite_non_negative(f.potential_evapotranspiration_mm)) {
            throw std::invalid_argument("continental water forcing contains invalid values");
        }
        if ((state.metadata_[i].flags & kOceanFlag) != 0) continue;

        const auto& c = state.cells_[i];
        if (!finite_non_negative(c.snow_water_equivalent_mm) ||
            !finite_non_negative(c.surface_water_mm) ||
            !finite_non_negative(c.soil_water_mm) ||
            !finite_non_negative(c.groundwater_mm)) {
            throw std::invalid_argument("continental water state contains invalid storage");
        }
        const double available_depth = static_cast<double>(c.snow_water_equivalent_mm) +
            c.surface_water_mm + c.soil_water_mm + c.groundwater_mm + f.precipitation_mm;
        if (!std::isfinite(available_depth) || available_depth > kMaxWaterDepthMm) {
            throw std::invalid_argument("continental water depth exceeds numerical safety limit");
        }
        const double cell_volume = depth_to_volume(available_depth, state.metadata_[i].area_m2);
        if (!std::isfinite(cell_volume) ||
            maximum_routed_volume_m3 > std::numeric_limits<double>::max() - cell_volume) {
            throw std::invalid_argument("continental routed water volume exceeds numerical safety limit");
        }
        maximum_routed_volume_m3 += cell_volume;
    }
    if (maximum_routed_volume_m3 / kSecondsPerDay >
        static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::invalid_argument("continental routed discharge exceeds float diagnostic range");
    }

    ContinentalWaterStepReport report;
    report.day_before = state.simulated_day_;
    report.day_after = state.simulated_day_ + 1;
    report.storage_before_m3 = state.total_storage_m3();

    std::vector<double> routed(state.cells_.size(), 0.0);
    for (std::size_t i = 0; i < state.cells_.size(); ++i) {
        auto& s = state.cells_[i];
        const auto& meta = state.metadata_[i];
        const auto& f = forcing[i];

        s.last_evapotranspiration_mm = 0.0f;
        s.last_quick_runoff_mm = 0.0f;
        s.last_baseflow_mm = 0.0f;
        s.last_routed_discharge_m3_s = 0.0f;
        if ((meta.flags & kOceanFlag) != 0) continue;

        const double precipitation = f.precipitation_mm;
        const double temperature = f.mean_air_temperature_c;
        const double pet = f.potential_evapotranspiration_mm;
        report.precipitation_m3 += depth_to_volume(precipitation, meta.area_m2);

        const double snow_fraction = std::clamp((1.0 - temperature) * 0.5, 0.0, 1.0);
        const double snowfall = precipitation * snow_fraction;
        const double rainfall = precipitation - snowfall;
        s.snow_water_equivalent_mm += static_cast<float>(snowfall);
        const double melt = std::min(static_cast<double>(s.snow_water_equivalent_mm),
            std::max(0.0, temperature) * parameters.snow_melt_mm_per_c_day);
        s.snow_water_equivalent_mm -= static_cast<float>(melt);
        s.surface_water_mm += static_cast<float>(rainfall + melt);

        const double saturation = parameters.soil_capacity_mm > 0.0f
            ? std::clamp(static_cast<double>(s.soil_water_mm) / parameters.soil_capacity_mm, 0.0, 1.0)
            : 1.0;
        const double infiltration_capacity = parameters.infiltration_capacity_mm_per_day *
                                             (0.25 + 0.75 * (1.0 - saturation));
        const double soil_room = std::max(0.0,
            static_cast<double>(parameters.soil_capacity_mm) - s.soil_water_mm);
        const double infiltration = std::min({static_cast<double>(s.surface_water_mm),
                                              infiltration_capacity,
                                              soil_room});
        s.surface_water_mm -= static_cast<float>(infiltration);
        s.soil_water_mm += static_cast<float>(infiltration);

        const double surface_evap = std::min(static_cast<double>(s.surface_water_mm), pet * 0.35);
        s.surface_water_mm -= static_cast<float>(surface_evap);
        const double remaining_pet = pet - surface_evap;
        const double moisture_span = std::max(1e-6,
            static_cast<double>(parameters.field_capacity_mm - parameters.wilting_point_mm));
        const double stress = std::clamp(
            (static_cast<double>(s.soil_water_mm) - parameters.wilting_point_mm) / moisture_span,
            0.0, 1.0);
        const double soil_et = std::min(static_cast<double>(s.soil_water_mm), remaining_pet * stress);
        s.soil_water_mm -= static_cast<float>(soil_et);
        const double et = surface_evap + soil_et;
        s.last_evapotranspiration_mm = static_cast<float>(et);
        report.evapotranspiration_m3 += depth_to_volume(et, meta.area_m2);

        const double excess_soil = std::max(0.0,
            static_cast<double>(s.soil_water_mm) - parameters.field_capacity_mm);
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
        routed[i] = depth_to_volume(quick_runoff + baseflow, meta.area_m2);
    }

    for (const auto index32 : state.routing_order_) {
        const auto i = static_cast<std::size_t>(index32);
        const double volume = routed[i];
        state.cells_[i].last_routed_discharge_m3_s = static_cast<float>(volume / kSecondsPerDay);
        const auto downstream = state.metadata_[i].downstream_index;
        if (downstream != kNoDownstream) {
            routed[downstream] += volume;
        } else {
            report.terminal_outflow_m3 += volume;
        }
    }

    state.simulated_day_ = report.day_after;
    report.storage_after_m3 = state.total_storage_m3();
    report.water_balance_error_m3 = report.storage_before_m3 + report.precipitation_m3 -
        report.evapotranspiration_m3 - report.terminal_outflow_m3 - report.storage_after_m3;
    return report;
}

} // namespace worldsim
