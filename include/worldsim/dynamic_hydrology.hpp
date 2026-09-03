#pragma once

#include "worldsim/continental_hydrology.hpp"

#include <cstddef>
#include <vector>

namespace worldsim {

// Dynamic water-cycle parameters for one authoritative 8x8 L1 hydrology tile.
// This is deliberately a compact bucket model: every term has a conserved water
// store/flux, while soil chemistry and vegetation feedback remain future layers.
struct DynamicHydrologyParameters {
    float soil_capacity_mm{260.0f};
    float field_capacity_mm{160.0f};
    float wilting_point_mm{45.0f};
    float infiltration_capacity_mm_per_day{24.0f};
    float surface_storage_capacity_mm{8.0f};
    float percolation_rate_per_day{0.08f};
    float groundwater_recession_per_day{0.015f};
    float snow_melt_mm_per_c_day{3.0f};
    float initial_soil_water_mm{120.0f};
    float initial_groundwater_mm{40.0f};

    void validate() const;
};

struct HydrometeorologicalForcing {
    CellCoord coord{};
    // Total depth over the requested step, not a rate.
    float precipitation_mm{};
    float mean_air_temperature_c{};
    float potential_evapotranspiration_mm{};
};

struct ExternalHydrologyInflow {
    CellCoord coord{};
    // Total channel volume injected at this L1 cell during the requested step.
    double volume_m3{};
};

struct DynamicHydrologyCellState {
    CellCoord coord{};
    bool active{};
    float snow_water_equivalent_mm{};
    float surface_water_mm{};
    float soil_water_mm{};
    float groundwater_mm{};

    // Flux diagnostics from the most recent advance call.
    float last_evapotranspiration_mm{};
    float last_quick_runoff_mm{};
    float last_baseflow_mm{};
    float last_routed_discharge_m3_s{};
};

struct DynamicHydrologyTileState {
    WorldConfig config{};
    CellCoord climate_coord{};
    double simulated_days{};
    std::vector<DynamicHydrologyCellState> cells;

    [[nodiscard]] std::size_t index_of(CellCoord coord) const;
    [[nodiscard]] const DynamicHydrologyCellState& cell(CellCoord coord) const;
};

struct HydrologyStepReport {
    double duration_days{};
    double storage_before_m3{};
    double precipitation_m3{};
    double external_inflow_m3{};
    double evapotranspiration_m3{};
    double external_outflow_m3{};
    double storage_after_m3{};
    double water_balance_error_m3{};
};

class World;

[[nodiscard]] DynamicHydrologyTileState make_dynamic_hydrology_tile_state(
    const World& world,
    const AuthoritativeHydrologyTile& tile,
    const DynamicHydrologyParameters& parameters = {});

// A deterministic, smooth climate-derived forcing useful before a weather system exists.
// It is a forcing provider, not the weather model: callers can replace it cell-for-cell.
[[nodiscard]] std::vector<HydrometeorologicalForcing> make_smooth_climatological_forcing(
    const World& world,
    const AuthoritativeHydrologyTile& tile,
    double day_of_year,
    double duration_days);

// Advances conserved snow/surface/soil/groundwater stores and routes quickflow/baseflow
// through the tile's authoritative drainage graph. Internally substeps at <= 1 day.
[[nodiscard]] HydrologyStepReport advance_dynamic_hydrology_tile(
    const World& world,
    const AuthoritativeHydrologyTile& tile,
    DynamicHydrologyTileState& state,
    const std::vector<HydrometeorologicalForcing>& forcing,
    const std::vector<ExternalHydrologyInflow>& external_inflows,
    double duration_days,
    const DynamicHydrologyParameters& parameters = {});

} // namespace worldsim
