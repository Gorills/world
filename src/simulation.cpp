#include "worldsim/simulation.hpp"

#include "worldsim/vegetation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace worldsim {

SimulationState::SimulationState(
    WorldConfig config,
    const WeatherParameters& weather_parameters,
    const DynamicHydrologyParameters& water_parameters)
    : world_(std::move(config)),
      topology_(world_.analyze_continental_hydrology()),
      weather_(make_weather_state(world_, weather_parameters)),
      water_(make_multiresolution_water_state(world_, topology_, water_parameters)),
      ecosystem_(EcosystemState::create(world_, topology_, water_)) {
    validate_invariants();
}

SimulationState SimulationState::from_world(
    World world,
    const WeatherParameters& weather_parameters,
    const DynamicHydrologyParameters& water_parameters) {
    auto topology = world.analyze_continental_hydrology();
    auto weather = make_weather_state(world, weather_parameters);
    auto water = make_multiresolution_water_state(world, topology, water_parameters);
    return SimulationState(
        std::move(world), std::move(topology), std::move(weather), std::move(water));
}

SimulationState::SimulationState(
    World world,
    ContinentalHydrologyResult topology,
    WeatherState weather,
    MultiresolutionWaterState water,
    SettlementState settlements)
    : world_(std::move(world)),
      topology_(std::move(topology)),
      weather_(std::move(weather)),
      water_(std::move(water)),
      settlements_(std::move(settlements)),
      ecosystem_(EcosystemState::create(world_, topology_, water_, weather_.simulated_day())) {
    validate_invariants();
}

void SimulationState::validate_invariants() const {
    if (ecosystem_.simulated_day() != weather_.simulated_day()) {
        throw std::runtime_error("simulation ecosystem/environment clocks are inconsistent");
    }
    if (weather_.simulated_day() != water_.simulated_day()) {
        throw std::runtime_error("simulation weather/water clocks are inconsistent");
    }
    if (weather_.config().seed != world_.config().seed ||
        water_.config().seed != world_.config().seed ||
        topology_.config.seed != world_.config().seed ||
        weather_.config().bounds.origin_x_m != world_.config().bounds.origin_x_m ||
        weather_.config().bounds.origin_y_m != world_.config().bounds.origin_y_m ||
        weather_.config().bounds.width_m != world_.config().bounds.width_m ||
        weather_.config().bounds.height_m != world_.config().bounds.height_m ||
        water_.config().bounds.origin_x_m != world_.config().bounds.origin_x_m ||
        water_.config().bounds.origin_y_m != world_.config().bounds.origin_y_m ||
        water_.config().bounds.width_m != world_.config().bounds.width_m ||
        water_.config().bounds.height_m != world_.config().bounds.height_m ||
        topology_.config.bounds.origin_x_m != world_.config().bounds.origin_x_m ||
        topology_.config.bounds.origin_y_m != world_.config().bounds.origin_y_m ||
        topology_.config.bounds.width_m != world_.config().bounds.width_m ||
        topology_.config.bounds.height_m != world_.config().bounds.height_m ||
        weather_.config().local_cell_m != world_.config().local_cell_m ||
        weather_.config().regional_cell_m != world_.config().regional_cell_m ||
        weather_.config().climate_cell_m != world_.config().climate_cell_m ||
        weather_.config().sea_level_m != world_.config().sea_level_m ||
        water_.config().local_cell_m != world_.config().local_cell_m ||
        water_.config().regional_cell_m != world_.config().regional_cell_m ||
        water_.config().climate_cell_m != world_.config().climate_cell_m ||
        water_.config().sea_level_m != world_.config().sea_level_m ||
        topology_.config.local_cell_m != world_.config().local_cell_m ||
        topology_.config.regional_cell_m != world_.config().regional_cell_m ||
        topology_.config.climate_cell_m != world_.config().climate_cell_m ||
        topology_.config.sea_level_m != world_.config().sea_level_m) {
        throw std::runtime_error("simulation components belong to different worlds");
    }
}

SettlementId SimulationState::found_settlement(
    CellCoord regional_coord,
    double population) {
    validate_invariants();
    (void)world_.sample_region(regional_coord);
    if (!std::isfinite(population) || population < 0.0) {
        throw std::invalid_argument("settlement population must be finite and non-negative");
    }
    if (settlements_.settlement_at(regional_coord)) {
        throw std::invalid_argument("regional cell already owns a settlement");
    }

    World staged_world = world_;
    (void)staged_world.materialize_local_patch(regional_coord);
    SettlementState staged_settlements = settlements_;
    const auto id = staged_settlements.found(regional_coord, population, simulated_day());

    world_.swap_local_history(staged_world);
    settlements_.swap(staged_settlements);
    validate_invariants();
    return id;
}

SettlementSuitability SimulationState::settlement_suitability(CellCoord regional_coord) const {
    validate_invariants();
    const auto region = world_.sample_region(regional_coord);
    const auto climate_coord = regional_to_climate(regional_coord, world_.config());
    const auto weather_sample = sample_weather(weather_, climate_coord);

    double soil_water_mm = 0.0;
    if (water_.is_refined(climate_coord)) {
        soil_water_mm = static_cast<double>(water_.refined_tile(climate_coord).cell(regional_coord).soil_water_mm);
    } else {
        soil_water_mm = static_cast<double>(water_.coarse_state().cell(climate_coord).soil_water_mm);
    }
    const double capacity_mm = std::max(1.0, static_cast<double>(water_.parameters().soil_capacity_mm));
    const double saturation = std::clamp(soil_water_mm / capacity_mm, 0.0, 1.0);

    double biomass = static_cast<double>(region.forest_potential);
    double disturbance = 0.0;
    if (const auto* patch = world_.find_local_patch(regional_coord)) {
        biomass = 0.0;
        disturbance = 0.0;
        for (const auto& cell : patch->cells) {
            biomass += static_cast<double>(cell.vegetation_biomass);
            disturbance += static_cast<double>(cell.disturbance);
        }
        biomass /= static_cast<double>(patch->cells.size());
        disturbance /= static_cast<double>(patch->cells.size());
    }

    // Simulation-scale heuristics only; these are intentionally bounded and are not
    // empirical demographic calibration.
    SettlementSuitability out;
    out.terrain_factor = std::clamp(
        1.0 - 0.60 * static_cast<double>(region.slope) -
            0.25 * static_cast<double>(region.terrain_roughness),
        0.20, 1.0);
    out.water_factor = std::clamp(0.25 + 1.10 * saturation, 0.25, 1.0);
    out.vegetation_factor = std::clamp(0.35 + 0.85 * biomass, 0.35, 1.0);
    const double temperature_distance =
        std::abs(static_cast<double>(weather_sample.mean_air_temperature_c) - 16.0);
    out.temperature_factor = std::clamp(1.0 - temperature_distance / 35.0, 0.20, 1.0);
    out.disturbance_factor = std::clamp(1.0 - 0.80 * disturbance, 0.20, 1.0);
    constexpr double kBaseCapacity = 2500.0;
    out.environmental_capacity = kBaseCapacity * out.terrain_factor * out.water_factor *
        out.vegetation_factor * out.temperature_factor * out.disturbance_factor;
    if (!std::isfinite(out.environmental_capacity) || out.environmental_capacity < 0.0) {
        throw std::runtime_error("settlement environmental capacity is invalid");
    }
    return out;
}

std::vector<HydrometeorologicalForcing> SimulationState::refined_daily_forcing(
    CellCoord climate_coord) const {
    validate_invariants();
    const auto sample = sample_weather(weather_, climate_coord);
    const ContinentalWaterForcing parent{
        sample.precipitation_mm,
        sample.mean_air_temperature_c,
        sample.potential_evapotranspiration_mm};
    return derive_refined_atmospheric_forcing(world_, water_, climate_coord, parent);
}

SimulationDayReport SimulationState::advance_day_full() {
    validate_invariants();
    auto staged_ecosystem = ecosystem_;
    const auto ecosystem_report = staged_ecosystem.advance_day(world_, weather_, water_);
    const auto et_factors = ecosystem_.evapotranspiration_factors();

    // Vegetation consumes current-day atmosphere/water just like hydrology. Stage the entire
    // sparse local-history map first; if environmental advance rejects, World remains unchanged.
    const auto vegetation_forcing =
        make_materialized_vegetation_forcing(world_, weather_, water_);
    World staged_world = world_;
    const auto vegetation_report =
        staged_world.advance_materialized_vegetation_day(vegetation_forcing);

    SettlementState staged_settlements = settlements_;
    SettlementStepReport settlement_report;
    settlement_report.settlement_count =
        static_cast<std::uint64_t>(staged_settlements.settlements().size());
    for (const auto& value : settlements_.settlements()) {
        settlement_report.population_before += value.population;
        settlement_report.environmental_capacity +=
            settlement_suitability(value.regional_coord).environmental_capacity;
    }
    auto next_values = staged_settlements.settlements();
    for (auto& value : next_values) {
        const double capacity = settlement_suitability(value.regional_coord).environmental_capacity;
        const double raw_delta = (capacity - value.population) * 0.001;
        const double max_growth = std::max(0.25, capacity * 0.002);
        const double max_decline = std::max(0.25, value.population * 0.002);
        value.population = std::max(
            0.0, value.population + std::clamp(raw_delta, -max_decline, max_growth));
        if (!std::isfinite(value.population)) {
            throw std::runtime_error("settlement population evolution became non-finite");
        }
    }
    staged_settlements = SettlementState::from_persisted(
        std::move(next_values), settlements_.next_id());
    settlement_report.population_after = 0.0;
    for (const auto& value : staged_settlements.settlements()) {
        settlement_report.population_after += value.population;
    }

    const auto environment =
        advance_weather_multiresolution_water_day(world_, weather_, water_, et_factors);

    // No-throw swaps close the unified generation only after environment commits.
    world_.swap_local_history(staged_world);
    settlements_.swap(staged_settlements);
    ecosystem_.swap(staged_ecosystem);
    validate_invariants();
    return {environment, vegetation_report, settlement_report, ecosystem_report};
}

WeatherWaterStepReport SimulationState::advance_day() {
    return advance_day_full().environment;
}

const RefinedWaterTileState& SimulationState::materialize_refined_water_tile(
    CellCoord climate_coord) {
    validate_invariants();
    const auto& result = worldsim::materialize_refined_water_tile(
        world_, topology_, water_, climate_coord);
    validate_invariants();
    return result;
}

void SimulationState::aggregate_refined_water_tile(CellCoord climate_coord) {
    validate_invariants();
    worldsim::aggregate_refined_water_tile(world_, water_, climate_coord);
    validate_invariants();
}

std::size_t SimulationState::disturb_surface(
    WorldPosition min,
    WorldPosition max,
    float amount) {
    validate_invariants();
    World staged_world = world_;
    const auto count = staged_world.disturb_surface(min, max, amount);
    auto staged_ecosystem = ecosystem_;
    if (count > 0) staged_ecosystem.apply_local_disturbance(world_, staged_world);
    world_.swap_local_history(staged_world);
    ecosystem_.swap(staged_ecosystem);
    return count;
}

} // namespace worldsim
