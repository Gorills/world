#pragma once

#include "worldsim/multiresolution_water.hpp"
#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace worldsim {

// Owns the authoritative runtime state that must share one simulation generation.
// Continental topology is derived from World and is intentionally not an independent
// persistence authority.
class SimulationState {
public:
    explicit SimulationState(
        WorldConfig config,
        const WeatherParameters& weather_parameters = {},
        const DynamicHydrologyParameters& water_parameters = {});

    [[nodiscard]] std::int64_t simulated_day() const noexcept { return weather_.simulated_day(); }
    [[nodiscard]] const World& world() const noexcept { return world_; }
    [[nodiscard]] const ContinentalHydrologyResult& topology() const noexcept { return topology_; }
    [[nodiscard]] const WeatherState& weather() const noexcept { return weather_; }
    [[nodiscard]] const MultiresolutionWaterState& water() const noexcept { return water_; }

    [[nodiscard]] WeatherWaterStepReport advance_day();

    [[nodiscard]] const RefinedWaterTileState& materialize_refined_water_tile(
        CellCoord climate_coord);
    void aggregate_refined_water_tile(CellCoord climate_coord);

    std::size_t disturb_surface(WorldPosition min, WorldPosition max, float amount);

    // One compound checkpoint publishes World L2 history, WeatherState and
    // MultiresolutionWaterState as one validated generation.
    void save_checkpoint(const std::filesystem::path& path) const;
    [[nodiscard]] static SimulationState load_checkpoint(const std::filesystem::path& path);

private:
    World world_;
    ContinentalHydrologyResult topology_;
    WeatherState weather_;
    MultiresolutionWaterState water_;

    SimulationState(
        World world,
        ContinentalHydrologyResult topology,
        WeatherState weather,
        MultiresolutionWaterState water);

    void validate_invariants() const;
};

} // namespace worldsim
