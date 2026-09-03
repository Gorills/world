#pragma once

#include "worldsim/continental_hydrology.hpp"
#include "worldsim/dynamic_hydrology.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace worldsim {

struct ContinentalWaterForcing {
    float precipitation_mm{};
    float mean_air_temperature_c{};
    float potential_evapotranspiration_mm{};
};

struct ContinentalWaterCellState {
    float snow_water_equivalent_mm{};
    float surface_water_mm{};
    float soil_water_mm{};
    float groundwater_mm{};

    float last_evapotranspiration_mm{};
    float last_quick_runoff_mm{};
    float last_baseflow_mm{};
    float last_routed_discharge_m3_s{};
};

struct ContinentalWaterStepReport {
    std::int64_t day_before{};
    std::int64_t day_after{};
    double storage_before_m3{};
    double precipitation_m3{};
    double evapotranspiration_m3{};
    double terminal_outflow_m3{};
    double storage_after_m3{};
    double water_balance_error_m3{};
};

class World;
class MultiresolutionWaterState;

void save_multiresolution_water_state(
    const MultiresolutionWaterState&, const std::filesystem::path&);
[[nodiscard]] MultiresolutionWaterState load_multiresolution_water_state(
    const World&, const ContinentalHydrologyResult&, const std::filesystem::path&);

// Authoritative coarse dynamic water state for the complete L0 world.
// Mutable internals are intentionally encapsulated: a caller can observe state but cannot
// alter the global clock, routing DAG or stores without going through a simulation command.
class ContinentalWaterState {
public:
    [[nodiscard]] const WorldConfig& config() const noexcept { return config_; }
    [[nodiscard]] CellCoord min_coord() const noexcept { return min_coord_; }
    [[nodiscard]] std::uint32_t width_cells() const noexcept { return width_cells_; }
    [[nodiscard]] std::uint32_t height_cells() const noexcept { return height_cells_; }
    [[nodiscard]] std::int64_t simulated_day() const noexcept { return simulated_day_; }
    [[nodiscard]] const std::vector<ContinentalWaterCellState>& cells() const noexcept { return cells_; }
    [[nodiscard]] std::size_t index_of(CellCoord coord) const;
    [[nodiscard]] CellCoord coord_of(std::size_t index) const;
    [[nodiscard]] const ContinentalWaterCellState& cell(CellCoord coord) const;

private:
    struct CellMetadata {
        double area_m2{};
        float mean_temperature_at_elevation_c{};
        float annual_precipitation_mm{};
        float continentality{};
        float soil_storage_capacity_scale{1.0f};
        float soil_infiltration_capacity_scale{1.0f};
        std::uint32_t downstream_index{0xFFFFFFFFu};
        std::uint32_t flags{}; // bit 0 = ocean
    };

    WorldConfig config_{};
    CellCoord min_coord_{};
    std::uint32_t width_cells_{};
    std::uint32_t height_cells_{};
    std::int64_t simulated_day_{};
    std::vector<ContinentalWaterCellState> cells_;
    std::vector<CellMetadata> metadata_;
    std::vector<std::uint32_t> routing_order_;

    [[nodiscard]] double total_storage_m3() const;

    friend class MultiresolutionWaterState;
    friend MultiresolutionWaterState make_multiresolution_water_state(
        const World&, const ContinentalHydrologyResult&, const DynamicHydrologyParameters&);
    friend void save_multiresolution_water_state(
        const MultiresolutionWaterState&, const std::filesystem::path&);
    friend MultiresolutionWaterState load_multiresolution_water_state(
        const World&, const ContinentalHydrologyResult&, const std::filesystem::path&);
    friend ContinentalWaterState make_continental_water_state(
        const World&, const ContinentalHydrologyResult&, const DynamicHydrologyParameters&);
    friend std::vector<ContinentalWaterForcing> make_smooth_continental_daily_forcing(
        const ContinentalWaterState&);
    friend ContinentalWaterStepReport advance_continental_water_day(
        ContinentalWaterState&, const std::vector<ContinentalWaterForcing>&,
        const DynamicHydrologyParameters&);
};

[[nodiscard]] ContinentalWaterState make_continental_water_state(
    const World& world,
    const ContinentalHydrologyResult& topology,
    const DynamicHydrologyParameters& parameters = {});

// Legacy deterministic climate-only forcing provider retained for controlled tests and older
// integrations. v0.9 WeatherState is the authoritative transient atmospheric source and supplies
// the same ContinentalWaterForcing records through the stable forcing boundary.
[[nodiscard]] std::vector<ContinentalWaterForcing> make_smooth_continental_daily_forcing(
    const ContinentalWaterState& state);

// Advances exactly one global day along the immutable L0 drainage snapshot captured at state
// creation. Every L0 cell shares this clock; there are no independently drifting tile clocks.
[[nodiscard]] ContinentalWaterStepReport advance_continental_water_day(
    ContinentalWaterState& state,
    const std::vector<ContinentalWaterForcing>& forcing,
    const DynamicHydrologyParameters& parameters = {});

} // namespace worldsim
