#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>

namespace {

bool near(double a, double b, double abs_eps = 1e-6, double rel_eps = 2e-6) {
    return std::abs(a - b) <= std::max(abs_eps, std::max(std::abs(a), std::abs(b)) * rel_eps);
}

double overlap_area_m2(worldsim::CellCoord coord, std::int32_t cell_m,
                       const worldsim::WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) return 0.0;
    return (x1 - x0) * (y1 - y0);
}

} // namespace

int main() {
    using namespace worldsim;

    WorldConfig cfg;
    cfg.seed = 901;
    cfg.bounds = {-12'345.0, -9'876.0, 38'765.0, 31'234.0};
    cfg.sea_level_m = -10'000.0f;
    World world(cfg);
    World twin(cfg);

    const auto topology = world.analyze_continental_hydrology({0.1f});
    if (topology.cells.empty()) {
        std::cerr << "FAIL: soil fixture has no climate cells\n";
        return 1;
    }

    CellCoord parent{};
    AuthoritativeHydrologyTile tile;
    bool found = false;
    for (const auto& coarse : topology.cells) {
        auto candidate = world.refine_authoritative_hydrology_tile(topology, coarse.coord);
        std::size_t active = 0;
        for (const auto& child : candidate.hydrology.cells) active += child.active ? 1U : 0U;
        if (active >= 2) {
            parent = coarse.coord;
            tile = std::move(candidate);
            found = true;
            break;
        }
    }
    if (!found) {
        std::cerr << "FAIL: soil fixture has no parent with multiple active L1 cells\n";
        return 1;
    }

    if (world.materialized_patch_count() != 0) {
        std::cerr << "FAIL: soil fixture unexpectedly starts materialized\n";
        return 1;
    }

    const auto parent_soil = world.sample_climate_soil(parent);
    const auto parent_soil_twin = twin.sample_climate_soil(parent);
    if (parent_soil.storage_capacity_scale != parent_soil_twin.storage_capacity_scale ||
        parent_soil.infiltration_capacity_scale != parent_soil_twin.infiltration_capacity_scale) {
        std::cerr << "FAIL: parent soil sampling is not deterministic\n";
        return 1;
    }

    double total_area = 0.0;
    double storage_weighted = 0.0;
    double infiltration_weighted = 0.0;
    bool observed_storage_variation = false;
    bool observed_infiltration_variation = false;
    bool have_first = false;
    SoilProperties first{};

    for (const auto& child : tile.hydrology.cells) {
        if (!child.active) continue;
        const auto soil = world.sample_soil(child.coord);
        const auto twin_soil = twin.sample_soil(child.coord);
        if (soil.storage_capacity_scale != twin_soil.storage_capacity_scale ||
            soil.infiltration_capacity_scale != twin_soil.infiltration_capacity_scale) {
            std::cerr << "FAIL: regional soil sampling is not deterministic\n";
            return 1;
        }
        if (!std::isfinite(soil.storage_capacity_scale) || soil.storage_capacity_scale <= 0.0f ||
            !std::isfinite(soil.infiltration_capacity_scale) || soil.infiltration_capacity_scale <= 0.0f) {
            std::cerr << "FAIL: regional soil scales must be finite and positive\n";
            return 1;
        }

        if (!have_first) {
            first = soil;
            have_first = true;
        } else {
            observed_storage_variation = observed_storage_variation ||
                std::abs(soil.storage_capacity_scale - first.storage_capacity_scale) > 1e-5f;
            observed_infiltration_variation = observed_infiltration_variation ||
                std::abs(soil.infiltration_capacity_scale - first.infiltration_capacity_scale) > 1e-5f;
        }

        const double area = overlap_area_m2(child.coord, cfg.regional_cell_m, cfg.bounds);
        total_area += area;
        storage_weighted += static_cast<double>(soil.storage_capacity_scale) * area;
        infiltration_weighted += static_cast<double>(soil.infiltration_capacity_scale) * area;
    }

    if (!(total_area > 0.0)) {
        std::cerr << "FAIL: soil fixture active cells have zero area\n";
        return 1;
    }
    const double storage_mean = storage_weighted / total_area;
    const double infiltration_mean = infiltration_weighted / total_area;
    if (!near(storage_mean, parent_soil.storage_capacity_scale) ||
        !near(infiltration_mean, parent_soil.infiltration_capacity_scale)) {
        std::cerr << "FAIL: area-weighted L1 soil does not reproduce L0 parent-equivalent properties\n";
        return 1;
    }
    if (!observed_storage_variation || !observed_infiltration_variation) {
        std::cerr << "FAIL: soil fixture did not produce genuine L1 heterogeneity\n";
        return 1;
    }

    WorldConfig other_cfg = cfg;
    other_cfg.seed = cfg.seed + 1;
    World other(other_cfg);
    bool seed_changes_field = false;
    for (const auto& child : tile.hydrology.cells) {
        if (!child.active) continue;
        const auto a = world.sample_soil(child.coord);
        const auto b = other.sample_soil(child.coord);
        seed_changes_field = seed_changes_field ||
            a.storage_capacity_scale != b.storage_capacity_scale ||
            a.infiltration_capacity_scale != b.infiltration_capacity_scale;
    }
    if (!seed_changes_field) {
        std::cerr << "FAIL: changing world seed did not change sampled soil field\n";
        return 1;
    }

    const WorldPosition p{cfg.bounds.origin_x_m + 1.0, cfg.bounds.origin_y_m + 1.0};
    const auto by_position = world.sample_soil(p);
    const auto by_coord = world.sample_soil(world_to_cell(p, cfg.regional_cell_m));
    if (by_position.storage_capacity_scale != by_coord.storage_capacity_scale ||
        by_position.infiltration_capacity_scale != by_coord.infiltration_capacity_scale) {
        std::cerr << "FAIL: position and coordinate soil sampling disagree\n";
        return 1;
    }

    bool outside_rejected = false;
    try {
        (void)world.sample_soil({cfg.bounds.origin_x_m - 1.0, cfg.bounds.origin_y_m});
    } catch (const std::out_of_range&) {
        outside_rejected = true;
    }
    if (!outside_rejected) {
        std::cerr << "FAIL: out-of-world soil position was not rejected\n";
        return 1;
    }

    if (world.materialized_patch_count() != 0) {
        std::cerr << "FAIL: soil sampling materialized persistent L2 state\n";
        return 1;
    }

    std::cout << "Spatial soil property tests passed\n";
    return 0;
}
