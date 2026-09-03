#include "worldsim/coordinates.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(float a, float b, float eps = 1e-6f) {
    return std::abs(a - b) <= eps;
}

worldsim::WorldConfig cfg(worldsim::Seed seed = 42) {
    worldsim::WorldConfig c;
    c.seed = seed;
    c.bounds = {-64'000.0, -64'000.0, 128'000.0, 128'000.0};
    return c;
}
}

int main() {
    using namespace worldsim;

    static_assert(std::is_const_v<std::remove_reference_t<
        decltype(std::declval<World&>().materialize_local_patch(CellCoord{}))>>,
        "materialized persistent state must not be directly mutable by callers");

    check(world_to_cell({0.0, 0.0}, 1024) == CellCoord{0,0}, "origin maps to zero cell");
    check(world_to_cell({-0.1, -0.1}, 1024) == CellCoord{-1,-1}, "negative coordinates use floor semantics");
    check(regional_to_climate({7,7}, cfg()) == CellCoord{0,0}, "regional 7 maps to climate 0");
    check(regional_to_climate({8,8}, cfg()) == CellCoord{1,1}, "regional 8 maps to climate 1");
    check(regional_to_climate({-1,-1}, cfg()) == CellCoord{-1,-1}, "negative hierarchy mapping is floor-correct");

    bool bad_coord_threw = false;
    try { (void)floor_to_cell(std::numeric_limits<double>::infinity(), 1024); }
    catch (const std::invalid_argument&) { bad_coord_threw = true; }
    check(bad_coord_threw, "non-finite direct coordinate conversion is rejected");

    bool bad_cell_size_threw = false;
    try { (void)floor_to_cell(0.0, 0); }
    catch (const std::invalid_argument&) { bad_cell_size_threw = true; }
    check(bad_cell_size_threw, "non-positive direct coordinate cell size is rejected");

    bool bad_hierarchy_threw = false;
    try {
        auto bad = cfg();
        bad.climate_cell_m = 1000;
        (void)regional_to_climate({0, 0}, bad);
    } catch (const std::invalid_argument&) { bad_hierarchy_threw = true; }
    check(bad_hierarchy_threw, "direct hierarchy conversion rejects invalid cell ratios");

    World a(cfg(123));
    World b(cfg(123));
    const auto a1 = a.sample_region(WorldPosition{10'000.0, 12'000.0});
    const auto b1 = b.sample_region(WorldPosition{10'000.0, 12'000.0});
    check(near(a1.elevation_m, b1.elevation_m), "same seed is deterministic");
    check(near(a1.forest_potential, b1.forest_potential), "derived regional state is deterministic");
    check(a.materialized_patch_count() == 0, "L1 sampling remains allocation-free");

    World c(cfg(124));
    const auto c1 = c.sample_region(WorldPosition{10'000.0, 12'000.0});
    check(!near(a1.elevation_m, c1.elevation_m, 1e-3f), "different seed changes terrain");

    bool climate_oob_threw = false;
    try { (void)a.sample_climate({1000, 1000}); }
    catch (const std::out_of_range&) { climate_oob_threw = true; }
    check(climate_oob_threw, "L0 climate query respects configured world bounds");

    auto& patch = a.materialize_local_patch(a1.coord);
    check(a.materialized_patch_count() == 1, "materialization persists one patch");
    check(patch.regional_coord == a1.coord, "patch has requested parent coord");
    check(a.materialize_local_patch(a1.coord).cells.data() == patch.cells.data(), "repeat materialization reuses persistent patch");

    auto d_cfg = cfg(77);
    d_cfg.sea_level_m = 12.5f;
    World d(d_cfg);
    const auto zero_affected = d.disturb_surface({0.0, 0.0}, {64.0, 64.0}, 0.0f);
    check(zero_affected == 0, "zero disturbance is a no-op");
    check(d.materialized_patch_count() == 0, "zero disturbance does not materialize L2");

    const auto affected = d.disturb_surface({0.0, 0.0}, {64.0, 64.0}, 0.75f);
    check(affected == 1, "exact 64m rectangle changes one local cell");
    const auto* dpatch = d.find_local_patch({0,0});
    check(dpatch != nullptr, "mutation materializes patch");
    check(dpatch && near(dpatch->cells[0].disturbance, 0.75f), "mutation is recorded in local cell");
    check(d.disturb_surface({0.0, 0.0}, {64.0, 64.0}, 0.5f) == 0,
          "weaker repeated disturbance reports no changed cells");

    const std::filesystem::path save = std::filesystem::temp_directory_path() / "worldsim_test.ws";
    d.save(save);
    auto loaded = World::load(save);
    check(loaded.config().seed == d.config().seed, "save/load preserves seed");
    check(near(loaded.config().sea_level_m, 12.5f), "save/load preserves sea level");
    check(loaded.materialized_patch_count() == 1, "save/load preserves persistent patch count");
    const auto* loaded_patch = loaded.find_local_patch({0,0});
    check(loaded_patch && near(loaded_patch->cells[0].disturbance, 0.75f), "save/load preserves mutation");
    const auto before = d.sample_region(WorldPosition{2'000.0, 2'000.0});
    const auto after = loaded.sample_region(WorldPosition{2'000.0, 2'000.0});
    check(near(before.elevation_m, after.elevation_m), "procedural state reproduces after load");
    std::filesystem::remove(save);


    // Europe-scale bounds are supported without eager allocation.
    WorldConfig europe_cfg;
    europe_cfg.seed = 999;
    europe_cfg.bounds = {-3'000'000.0, -2'500'000.0, 6'000'000.0, 5'000'000.0};
    World europe(europe_cfg);
    (void)europe.sample_region(WorldPosition{2'750'000.0, 2'000'000.0});
    check(europe.materialized_patch_count() == 0, "Europe-scale query does not eagerly allocate local detail");

    // Save ordering must be canonical regardless of patch insertion order.
    World x(cfg(88));
    World y(cfg(88));
    (void)x.materialize_local_patch({2, 1});
    (void)x.materialize_local_patch({-2, 3});
    (void)y.materialize_local_patch({-2, 3});
    (void)y.materialize_local_patch({2, 1});
    const auto sx = std::filesystem::temp_directory_path() / "worldsim_order_x.ws";
    const auto sy = std::filesystem::temp_directory_path() / "worldsim_order_y.ws";
    x.save(sx); y.save(sy);
    {
        std::ifstream fx(sx, std::ios::binary), fy(sy, std::ios::binary);
        const std::vector<char> bx((std::istreambuf_iterator<char>(fx)), {});
        const std::vector<char> by((std::istreambuf_iterator<char>(fy)), {});
        check(bx == by, "identical state serializes byte-for-byte identically");
    }
    std::filesystem::remove(sx);
    std::filesystem::remove(sy);

    // Regional hydrology is deterministic, allocation-free with respect to L2, conserves
    // effective water yield, drains every interior cell, and extracts lakes/rivers.
    WorldConfig hydro_cfg;
    hydro_cfg.seed = 42;
    hydro_cfg.bounds = {-128'000.0, -128'000.0, 256'000.0, 256'000.0};
    World hydro_world(hydro_cfg);
    HydrologyRequest hydro_request;
    hydro_request.min_coord = {-32, -32};
    hydro_request.width_cells = 64;
    hydro_request.height_cells = 64;
    hydro_request.river_threshold_m3_s = 0.2f;
    hydro_request.lake_min_depth_m = 0.25f;
    const auto hydro = hydro_world.analyze_hydrology(hydro_request);
    const auto hydro_again = hydro_world.analyze_hydrology(hydro_request);
    check(hydro.cells.size() == 4096, "hydrology returns requested raster size");
    check(hydro_world.materialized_patch_count() == 0, "hydrology analysis does not materialize L2");
    check(!hydro.lakes.empty(), "fixed hydrology fixture contains extracted lakes");
    check(!hydro.river_segments.empty(), "fixed hydrology fixture contains extracted river graph");
    check(hydro.cells.size() == hydro_again.cells.size() && hydro.lakes.size() == hydro_again.lakes.size() &&
          hydro.river_segments.size() == hydro_again.river_segments.size(), "hydrology topology is deterministic");

    double total_yield = 0.0;
    double total_outflow = 0.0;
    bool flow_ok = true;
    bool all_paths_exit = true;
    bool deterministic_cells = true;
    for (std::size_t i = 0; i < hydro.cells.size(); ++i) {
        const auto& cell = hydro.cells[i];
        const auto& same = hydro_again.cells[i];
        total_yield += cell.local_water_yield_m3_s;
        if (!cell.has_downstream) total_outflow += cell.accumulated_discharge_m3_s;
        if (cell.filled_elevation_m + 1e-5f < cell.terrain_elevation_m) flow_ok = false;
        if (cell.has_downstream) {
            const auto& down = hydro.cell(cell.downstream_coord);
            if (down.filled_elevation_m > cell.filled_elevation_m + 1e-5f) flow_ok = false;
        }
        deterministic_cells = deterministic_cells &&
            cell.coord == same.coord && near(cell.filled_elevation_m, same.filled_elevation_m) &&
            near(cell.accumulated_discharge_m3_s, same.accumulated_discharge_m3_s) &&
            cell.downstream_coord == same.downstream_coord && cell.has_downstream == same.has_downstream &&
            cell.catchment_id == same.catchment_id && cell.lake_id == same.lake_id && cell.river == same.river;

        const HydrologyCell* cursor = &cell;
        std::size_t steps = 0;
        while (cursor->has_downstream && steps <= hydro.cells.size()) {
            cursor = &hydro.cell(cursor->downstream_coord);
            ++steps;
        }
        if (steps > hydro.cells.size()) all_paths_exit = false;
    }
    check(flow_ok, "filled-surface flow never goes uphill");
    check(all_paths_exit, "all hydrology flow paths are acyclic and reach a boundary outlet");
    check(std::abs(total_yield - total_outflow) <= std::max(1e-4, total_yield * 1e-5),
          "hydrology conserves effective water yield at domain outlets");
    check(deterministic_cells, "repeated hydrology analysis is cell-for-cell deterministic");

    bool lakes_valid = true;
    for (const auto& lake : hydro.lakes) {
        lakes_valid = lakes_valid && lake.id != 0 && lake.cell_count > 0 && lake.area_m2 > 0.0 &&
                      lake.volume_m3 > 0.0 && lake.max_depth_m >= hydro_request.lake_min_depth_m &&
                      lake.has_outflow;
        if (lake.has_outflow) {
            const auto& outlet = hydro.cell(lake.outlet_coord);
            const auto& outflow = hydro.cell(lake.outflow_coord);
            lakes_valid = lakes_valid && outlet.has_downstream && outlet.downstream_coord == lake.outflow_coord &&
                          outflow.lake_id != lake.id;
        }
    }
    check(lakes_valid, "lake records have valid geometry and explicit downstream connectivity");

    bool rivers_valid = true;
    for (const auto& segment : hydro.river_segments) {
        const auto& source = hydro.cell(segment.from);
        const auto dx = std::llabs(segment.from.x - segment.to.x);