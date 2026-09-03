#pragma once

#include "worldsim/continental_water.hpp"

#include <cstddef>
#include <cstdint>
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

class MultiresolutionWaterState {
public:
    [[nodiscard]] const WorldConfig& config() const noexcept { return coarse_.config(); }
    [[nodiscard]] std::int64_t simulated_day() const noexcept { return coarse_.simulated_day(); }
    [[nodiscard]] const DynamicHydrologyParameters& parameters() const noexcept { return parameters_; }
    [[nodiscard]] const ContinentalWaterState& coarse_state() const noexcept { return coarse_; }
    [[nodiscard]] std::size_t refined_tile_count() const noexcept { return refined_.size(); }
    [[nodiscard]] bool is_refined(CellCoord climate_coord) const noexcept;
    [[nodiscard]] const RefinedWaterTileState& refined_tile(CellCoord climate_coord) const;

private:
    struct RefinedTile {
        AuthoritativeHydrologyTile topology;
        RefinedWaterTileState state;
    };

    ContinentalWaterState coarse_;
    DynamicHydrologyParameters parameters_;
    std::unordered_map<CellCoord, RefinedTile, CellCoordHash> refined_;

    [[nodiscard]] ContinentalWaterCellState& coarse_cell_mutable(std::size_t index) noexcept;
    [[nodiscard]] double coarse_area_m2(std::size_t index) const noexcept;
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
    friend ContinentalWaterStepReport advance_multiresolution_water_day(
        const World&, MultiresolutionWaterState&, const std::vector<ContinentalWaterForcing>&);
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

[[nodiscard]] ContinentalWaterStepReport advance_multiresolution_water_day(
    const World& world,
    MultiresolutionWaterState& state,
    const std::vector<ContinentalWaterForcing>& forcing);

} // namespace worldsim
