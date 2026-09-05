#pragma once

#include "worldsim/continental_water.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace worldsim {

struct RefinedWaterTileState {
    WorldConfig config{};
    CellCoord climate_coord{};
    std::int64_t simulated_day{};
    std::vector<DynamicHydrologyCellState> cells;

    [[nodiscard]] std::size_t index_of(CellCoord coord) const;
    [[nodiscard]] const DynamicHydrologyCellState& cell(CellCoord coord) const;
};

// Topology-derived L0 channel transport. Reach length remains dominant while filled-elevation
// slope and accumulated discharge weakly modify a bounded simulation-scale residence heuristic.
// This is reconstructed and intentionally not an independent persistence authority.
struct ChannelTransportProperties {
    double reach_length_m{};
    double downhill_gradient{};
    double residence_days{};
    double release_fraction_per_day{};
};

class MultiresolutionWaterState {
public:
    [[nodiscard]] const WorldConfig& config() const noexcept { return coarse_.config(); }
    [[nodiscard]] std::int64_t simulated_day() const noexcept { return coarse_.simulated_day(); }
    [[nodiscard]] const DynamicHydrologyParameters& parameters() const noexcept { return parameters_; }
    [[nodiscard]] const ContinentalWaterState& coarse_state() const noexcept { return coarse_; }
    [[nodiscard]] std::size_t refined_tile_count() const noexcept { return refined_.size(); }
    [[nodiscard]] bool is_refined(CellCoord climate_coord) const noexcept;
    [[nodiscard]] const RefinedWaterTileState& refined_tile(CellCoord climate_coord) const;

    // Conserved L0 channel water. It is connectivity state, not an independently refined
    // bucket store: refinement changes terrestrial bucket ownership while channel storage
    // remains attached to the L0 drainage parent.
    [[nodiscard]] double channel_storage_m3(CellCoord climate_coord) const;
    [[nodiscard]] double total_channel_storage_m3() const noexcept;
    [[nodiscard]] const ChannelTransportProperties& channel_transport(
        CellCoord climate_coord) const;

private:
    struct RefinedTile {
        AuthoritativeHydrologyTile topology;
        RefinedWaterTileState state;
    };

    ContinentalWaterState coarse_;
    DynamicHydrologyParameters parameters_;
    std::unordered_map<CellCoord, RefinedTile, CellCoordHash> refined_;
    std::vector<double> channel_storage_m3_;
    std::vector<ChannelTransportProperties> channel_transport_;

    [[nodiscard]] ContinentalWaterCellState& coarse_cell_mutable(std::size_t index) noexcept;
    [[nodiscard]] double coarse_area_m2(std::size_t index) const noexcept;
    [[nodiscard]] SoilProperties coarse_soil_properties(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t coarse_downstream_index(std::size_t index) const noexcept;
    [[nodiscard]] bool coarse_is_ocean(std::size_t index) const noexcept;
    [[nodiscard]] const std::vector<std::uint32_t>& coarse_routing_order() const noexcept;
    void set_simulated_day(std::int64_t day) noexcept;
    [[nodiscard]] double total_storage_m3(const World& world) const;

    friend MultiresolutionWaterState make_multiresolution_water_state(
        const World&, const ContinentalHydrologyResult&, const DynamicHydrologyParameters&);
    friend const RefinedWaterTileState& materialize_refined_water_tile(
        const World&, const ContinentalHydrologyResult&, MultiresolutionWaterState&, CellCoord);
    friend void aggregate_refined_water_tile(const World&, MultiresolutionWaterState&, CellCoord);
    friend std::vector<HydrometeorologicalForcing> derive_refined_atmospheric_forcing(
        const World&, const MultiresolutionWaterState&, CellCoord, const ContinentalWaterForcing&);
    friend ContinentalWaterStepReport advance_multiresolution_water_day(
        const World&, MultiresolutionWaterState&, const std::vector<ContinentalWaterForcing>&);
    friend ContinentalWaterStepReport advance_multiresolution_water_day(
        const World&, MultiresolutionWaterState&, const std::vector<ContinentalWaterForcing>&,
        const std::vector<float>&);
    friend void save_multiresolution_water_state(
        const MultiresolutionWaterState&, const std::filesystem::path&);
    friend MultiresolutionWaterState load_multiresolution_water_state(
        const World&, const ContinentalHydrologyResult&, const std::filesystem::path&);
};

[[nodiscard]] MultiresolutionWaterState make_multiresolution_water_state(
    const World& world,
    const ContinentalHydrologyResult& topology,
    const DynamicHydrologyParameters& parameters = {});

[[nodiscard]] const RefinedWaterTileState& materialize_refined_water_tile(
    const World& world,
    const ContinentalHydrologyResult& topology,
    MultiresolutionWaterState& state,
    CellCoord climate_coord);

void aggregate_refined_water_tile(
    const World& world,
    MultiresolutionWaterState& state,
    CellCoord climate_coord);

// Stateless L0 -> L1 atmospheric forcing transform for an already-refined terrestrial parent.
// Weather remains L0-authoritative: this derives child temperature/PET from local terrain and
// redistributes parent precipitation conservatively over actual child/world overlap area.
[[nodiscard]] std::vector<HydrometeorologicalForcing> derive_refined_atmospheric_forcing(
    const World& world,
    const MultiresolutionWaterState& state,
    CellCoord climate_coord,
    const ContinentalWaterForcing& parent_forcing);

[[nodiscard]] ContinentalWaterStepReport advance_multiresolution_water_day(
    const World& world,
    MultiresolutionWaterState& state,
    const std::vector<ContinentalWaterForcing>& forcing);

// Canopy multiplier applied once to coarse and derived fine PET. Empty means 1.
[[nodiscard]] ContinentalWaterStepReport advance_multiresolution_water_day(
    const World& world, MultiresolutionWaterState& state,
    const std::vector<ContinentalWaterForcing>& forcing,
    const std::vector<float>& evapotranspiration_factors);

// Dynamic water remains an explicit simulation state rather than becoming implicit World state.
// This versioned file persists its exact global day, terrestrial stores, L0 channel storage and
// sparse refined ownership. Channel transport geometry is derived from topology on create/load.
void save_multiresolution_water_state(
    const MultiresolutionWaterState& state,
    const std::filesystem::path& path);

[[nodiscard]] MultiresolutionWaterState load_multiresolution_water_state(
    const World& world,
    const ContinentalHydrologyResult& topology,
    const std::filesystem::path& path);

} // namespace worldsim
