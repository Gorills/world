#include "worldsim/simulation.hpp"

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
      water_(make_multiresolution_water_state(world_, topology_, water_parameters)) {
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
    MultiresolutionWaterState water)
    : world_(std::move(world)),
      topology_(std::move(topology)),
      weather_(std::move(weather)),
      water_(std::move(water)) {
    validate_invariants();
}

void SimulationState::validate_invariants() const {
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

WeatherWaterStepReport SimulationState::advance_day() {
    validate_invariants();
    const auto report = advance_weather_multiresolution_water_day(world_, weather_, water_);
    validate_invariants();
    return report;
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
    return world_.disturb_surface(min, max, amount);
}

} // namespace worldsim
