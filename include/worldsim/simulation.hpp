#pragma once

#include "worldsim/multiresolution_water.hpp"
#include "worldsim/settlements.hpp"
#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"
#include "worldsim/ecosystem.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace worldsim {

struct SimulationDayReport {
    WeatherWaterStepReport environment;
    VegetationStepReport vegetation;
    SettlementStepReport settlements;
    EcosystemStepReport ecosystem;
};

// Owns the authoritative runtime state that must share one simulation generation.
// Continental topology is derived from World and is intentionally not an independent
// persistence authority.
class SimulationState {
public:
    explicit SimulationState(
        WorldConfig config,
        const WeatherParameters& weather_parameters = {},
        const DynamicHydrologyParameters& water_parameters = {});

    // Migration path for pre-v0.10 World saves. Existing persistent L2 history becomes the
    // World authority of a new day-zero unified simulation; weather and water start from the
    // deterministic initial state derived from that exact World.
    [[nodiscard]] static SimulationState from_world(
        World world,
        const WeatherParameters& weather_parameters = {},
        const DynamicHydrologyParameters& water_parameters = {});

    [[nodiscard]] std::int64_t simulated_day() const noexcept { return weather_.simulated_day(); }
    [[nodiscard]] const World& world() const noexcept { return world_; }
    [[nodiscard]] const ContinentalHydrologyResult& topology() const noexcept { return topology_; }
    [[nodiscard]] const WeatherState& weather() const noexcept { return weather_; }
    [[nodiscard]] const MultiresolutionWaterState& water() const noexcept { return water_; }
    [[nodiscard]] const EcosystemState& ecosystem() const noexcept { return ecosystem_; }
    [[nodiscard]] const SettlementState& settlement_state() const noexcept { return settlements_; }
    [[nodiscard]] const std::vector<Settlement>& settlements() const noexcept {
        return settlements_.settlements();
    }
    [[nodiscard]] const Settlement* settlement(SettlementId id) const noexcept {
        return settlements_.settlement(id);
    }

    SettlementId found_settlement(CellCoord regional_coord, double population = 100.0);
    [[nodiscard]] SettlementSuitability settlement_suitability(CellCoord regional_coord) const;

    // Read-only current-day L1 forcing for an already-refined parent. This does not advance
    // weather, mutate water or materialize persistent state.
    [[nodiscard]] std::vector<HydrometeorologicalForcing> refined_daily_forcing(
        CellCoord climate_coord) const;

    [[nodiscard]] SimulationDayReport advance_day_full();
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
    SettlementState settlements_;
    EcosystemState ecosystem_;

    SimulationState(
        World world,
        ContinentalHydrologyResult topology,
        WeatherState weather,
        MultiresolutionWaterState water,
        SettlementState settlements = {});

    void validate_invariants() const;
};

} // namespace worldsim
