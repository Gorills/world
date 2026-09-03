#pragma once

#include "worldsim/types.hpp"
#include "worldsim/hydrology.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace worldsim {

inline constexpr std::size_t kMaxContinentalHydrologyCells = 1'000'000;

struct ContinentalHydrologyRequest {
    float river_threshold_m3_s{25.0f};

    void validate() const;
};

struct ContinentalHydrologyCell {
    CellCoord coord{};
    float terrain_elevation_m{};
    float filled_elevation_m{};
    float depression_depth_m{};
    float local_water_yield_m3_s{};
    float accumulated_discharge_m3_s{};
    CellCoord downstream_coord{};
    bool has_downstream{};
    CellCoord terminal_outlet_coord{};
    std::uint64_t basin_id{};
    bool ocean{};
    bool river{};
};

struct ContinentalHydrologyResult {
    WorldConfig config{};
    CellCoord min_coord{};
    std::uint32_t width_cells{};
    std::uint32_t height_cells{};
    std::vector<ContinentalHydrologyCell> cells;

    [[nodiscard]] std::size_t index_of(CellCoord coord) const;
    [[nodiscard]] const ContinentalHydrologyCell& cell(CellCoord coord) const;
};

struct AuthoritativeHydrologyTile {
    WorldConfig config{};
    CellCoord climate_coord{};
    HydrologyResult hydrology;
};

class World;

// Whole-world L0 drainage. Ocean cells and the configured world boundary are the only
// external sinks; the result therefore does not depend on an arbitrary analysis rectangle.
[[nodiscard]] ContinentalHydrologyResult analyze_continental_hydrology(
    const World& world, const ContinentalHydrologyRequest& request = {});

// Refines one L0 cell into its fixed 8x8 L1 tile. The L0 downstream relation determines
// the tile outlet, so the topology is stable regardless of which neighboring tiles are queried.
[[nodiscard]] AuthoritativeHydrologyTile refine_authoritative_hydrology_tile(
    const World& world,
    const ContinentalHydrologyResult& continent,
    CellCoord climate_coord,
    float river_threshold_m3_s = 0.5f,
    float lake_min_depth_m = 0.25f);

} // namespace worldsim
