#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace worldsim {

using Seed = std::uint64_t;

struct WorldPosition {
    double x_m{};
    double y_m{};
};

struct CellCoord {
    std::int64_t x{};
    std::int64_t y{};

    friend constexpr bool operator==(const CellCoord&, const CellCoord&) = default;
};

struct CellCoordHash {
    std::size_t operator()(const CellCoord& c) const noexcept;
};

struct WorldBounds {
    double origin_x_m{0.0};
    double origin_y_m{0.0};
    double width_m{128'000.0};
    double height_m{128'000.0};

    [[nodiscard]] bool contains(WorldPosition p) const noexcept {
        return p.x_m >= origin_x_m && p.y_m >= origin_y_m &&
               p.x_m < origin_x_m + width_m && p.y_m < origin_y_m + height_m;
    }
};

struct WorldConfig {
    Seed seed{1};
    WorldBounds bounds{};

    // Hierarchy is intentionally fixed to powers of two in v0.2.
    std::int32_t local_cell_m{64};
    std::int32_t regional_cell_m{1024};
    std::int32_t climate_cell_m{8192};

    // Authoritative ocean datum for continental drainage.
    float sea_level_m{0.0f};

    void validate() const;
};

struct ClimateSample {
    CellCoord coord{};
    float mean_temperature_c{};
    float annual_precipitation_mm{};
    float continentality{};
};

struct RegionalSample {
    CellCoord coord{};
    float elevation_m{};
    float slope{};               // dimensionless rise/run approximation
    float terrain_roughness{};   // 0..1
    float bedrock_hardness{};    // 0..1, static geological proxy
    float forest_potential{};    // 0..1, placeholder for later vegetation system
};

// Deterministic static soil modifiers. They scale the existing hydrology parameter values
// rather than replacing those configurable reference parameters. A value of 1 means the
// reference capacity/rate is unchanged.
struct SoilProperties {
    float storage_capacity_scale{1.0f};
    float infiltration_capacity_scale{1.0f};
};

struct LocalCell {
    float elevation_m{};
    float terrain_roughness{};
    float forest_potential{};
    float disturbance{};         // persistent 0..1 human/natural surface disturbance
};

constexpr std::size_t kLocalCellsPerAxis = 16;
constexpr std::size_t kLocalCellCount = kLocalCellsPerAxis * kLocalCellsPerAxis;

struct LocalPatch {
    CellCoord regional_coord{};
    std::array<LocalCell, kLocalCellCount> cells{};
};

} // namespace worldsim
