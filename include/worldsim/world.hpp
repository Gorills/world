#pragma once

#include "worldsim/coordinates.hpp"
#include "worldsim/continental_hydrology.hpp"
#include "worldsim/dynamic_hydrology.hpp"
#include "worldsim/hydrology.hpp"
#include "worldsim/types.hpp"

#include <cstddef>
#include <filesystem>
#include <unordered_map>

namespace worldsim {

class World {
public:
    explicit World(WorldConfig config);

    [[nodiscard]] const WorldConfig& config() const noexcept { return config_; }

    [[nodiscard]] float sample_elevation(WorldPosition position) const;
    [[nodiscard]] ClimateSample sample_climate(CellCoord coord) const;
    [[nodiscard]] RegionalSample sample_region(CellCoord coord) const;
    [[nodiscard]] RegionalSample sample_region(WorldPosition position) const;
    [[nodiscard]] HydrologyResult analyze_hydrology(const HydrologyRequest& request) const;
    [[nodiscard]] ContinentalHydrologyResult analyze_continental_hydrology(
        const ContinentalHydrologyRequest& request = {}) const;
    [[nodiscard]] AuthoritativeHydrologyTile refine_authoritative_hydrology_tile(
        const ContinentalHydrologyResult& continent, CellCoord climate_coord,
        float river_threshold_m3_s = 0.5f, float lake_min_depth_m = 0.25f) const;

    // Materialization creates persistent L2 state. Merely sampling L0/L1 never allocates it.
    // Callers only get a const view: persistent truth must be mutated through commands.
    [[nodiscard]] const LocalPatch& materialize_local_patch(CellCoord regional_coord);
    [[nodiscard]] const LocalPatch* find_local_patch(CellCoord regional_coord) const noexcept;
    [[nodiscard]] std::size_t materialized_patch_count() const noexcept { return local_patches_.size(); }

    // First persistent mutation primitive. A later vegetation system will consume disturbance.
    // Returns the number of local cells whose stored disturbance actually increased.
    std::size_t disturb_surface(WorldPosition min, WorldPosition max, float amount);

    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static World load(const std::filesystem::path& path);

private:
    WorldConfig config_;
    std::unordered_map<CellCoord, LocalPatch, CellCoordHash> local_patches_;

    [[nodiscard]] LocalPatch generate_local_patch(CellCoord regional_coord) const;
    LocalPatch& materialize_local_patch_mutable(CellCoord regional_coord);
    void require_climate_in_bounds(CellCoord coord) const;
    void require_region_in_bounds(CellCoord coord) const;
};

} // namespace worldsim
