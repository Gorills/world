#pragma once

#include "worldsim/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace worldsim {

// A single regional solve is intentionally bounded. Stable continent-scale drainage will
// be a separate coarser layer rather than one enormous L1 allocation.
inline constexpr std::size_t kMaxHydrologyCells = 262'144;

struct HydrologyRequest {
    CellCoord min_coord{};
    std::uint32_t width_cells{64};
    std::uint32_t height_cells{64};
    float river_threshold_m3_s{0.5f};
    float lake_min_depth_m{0.25f};

    void validate() const;
};

struct HydrologyCell {
    CellCoord coord{};
    bool active{};
    bool ocean{};
    float terrain_elevation_m{};
    float filled_elevation_m{};
    float depression_depth_m{};
    float local_water_yield_m3_s{};
    float accumulated_discharge_m3_s{};
    CellCoord downstream_coord{};
    bool has_downstream{};
    bool downstream_is_external{};
    std::uint64_t catchment_id{};
    std::uint64_t lake_id{}; // 0 means no lake.
    bool river{};
};

struct LakeInfo {
    std::uint64_t id{};
    CellCoord outlet_coord{};
    CellCoord outflow_coord{};
    bool has_outflow{};
    std::uint64_t cell_count{};
    double area_m2{};
    double volume_m3{};
    float surface_elevation_m{};
    float max_depth_m{};
};

struct RiverSegment {
    CellCoord from{};
    CellCoord to{};
    float discharge_m3_s{};
};

struct HydrologyResult {
    HydrologyRequest request{};
    std::vector<HydrologyCell> cells;
    std::vector<LakeInfo> lakes;
    std::vector<RiverSegment> river_segments;

    [[nodiscard]] std::size_t index_of(CellCoord coord) const;
    [[nodiscard]] const HydrologyCell& cell(CellCoord coord) const;
};

class World;

// Computes a climatological regional drainage solution for a finite L1 rectangle.
// Boundary cells are outlets. The result is deterministic and derived, not persisted.
[[nodiscard]] HydrologyResult analyze_hydrology(const World& world, const HydrologyRequest& request);

} // namespace worldsim
