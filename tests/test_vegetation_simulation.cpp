#include "worldsim/simulation.hpp"
#include "worldsim/vegetation.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool same_patch(const worldsim::LocalPatch& a, const worldsim::LocalPatch& b) {
    if (a.regional_coord != b.regional_coord) return false;
    for (std::size_t i = 0; i < worldsim::kLocalCellCount; ++i) {
        const auto& x = a.cells[i];
        const auto& y = b.cells[i];
        if (x.elevation_m != y.elevation_m ||
            x.terrain_roughness != y.terrain_roughness ||
            x.forest_potential != y.forest_potential ||
            x.disturbance != y.disturbance ||
            x.vegetation_biomass != y.vegetation_biomass) {
            return false;
        }
    }
    return true;
}

worldsim::WorldConfig config() {
    worldsim::WorldConfig cfg;
    cfg.seed = 14002;
    cfg.bounds = {0.0, 0.0, 32'768.0, 32'768.0};
    cfg.sea_level_m = -10'000.0f;
    return cfg;
}

worldsim::CellCoord select_region(const worldsim::SimulationState& state) {
    for (std::int64_t y = 0; y < 32; ++y) {
        for (std::int64_t x = 0; x < 32; ++x) {
            const worldsim::CellCoord c{x, y};
            if (state.world().sample_region(c).forest_potential > 0.15f) return c;
        }
    }
    throw std::runtime_error("vegetation simulation fixture has no useful region");
}
} // namespace

int main() {
    using namespace worldsim;

    SimulationState state(config());
    const auto empty = state.advance_day_full();
    check(empty.vegetation.patch_count == 0 &&
          state.world().materialized_patch_count() == 0 &&
          state.simulated_day() == 1,
          "unmaterialized simulation advances without eager vegetation state");

    const auto region = select_region(state);
    const double x0 = static_cast<double>(region.x) * state.world().config().regional_cell_m;
    const double y0 = static_cast<double>(region.y) * state.world().config().regional_cell_m;
    check(state.disturb_surface(
              {x0, y0},
              {x0 + state.world().config().regional_cell_m,
               y0 + state.world().config().regional_cell_m},
              0.7f) == kLocalCellCount,
          "simulation disturbance materializes and damages one full L2 patch");
    check(state.world().materialized_patch_count() == 1,
          "simulation owns one sparse vegetation patch");

    const auto forcing = make_materialized_vegetation_forcing(
        state.world(), state.weather(), state.water());
    check(forcing.size() == 1 && forcing[0].regional_coord == region &&
          forcing[0].soil_saturation >= 0.0f && forcing[0].soil_saturation <= 1.0f,
          "simulation derives finite current-day vegetation forcing without materialization");

    const auto* before = state.world().find_local_patch(region);
    check(before != nullptr, "simulation vegetation patch is queryable");
    if (!before) return 1;
    const auto before_copy = *before;

    const auto day = state.advance_day_full();
    const auto* after = state.world().find_local_patch(region);
    check(after != nullptr, "vegetation survives unified day advance");
    if (!after) return 1;
    check(day.environment.weather.day_after == 2 &&
          day.environment.water.day_after == 2 &&
          state.simulated_day() == 2,
          "vegetation-aware full day preserves exact environment clocks");
    check(day.vegetation.patch_count == 1 &&
          day.vegetation.land_cell_count == kLocalCellCount &&
          day.vegetation.disturbance_area_after_m2 <
              day.vegetation.disturbance_area_before_m2 &&
          day.vegetation.biomass_area_after_m2 >=
              day.vegetation.biomass_area_before_m2,
          "unified day advances sparse vegetation with current environment");
    check(state.world().materialized_patch_count() == 1,
          "vegetation-aware unified day remains sparse");

    bool disturbance_decreased = false;
    for (std::size_t i = 0; i < kLocalCellCount; ++i) {
        if (after->cells[i].disturbance < before_copy.cells[i].disturbance) {
            disturbance_decreased = true;
            break;
        }
    }
    check(disturbance_decreased,
          "unified vegetation day mutates persistent local recovery state");

    const auto temp = std::filesystem::temp_directory_path();
    const auto checkpoint = temp / "worldsim_vegetation_simulation.wsc";
    state.save_checkpoint(checkpoint);
    auto loaded = SimulationState::load_checkpoint(checkpoint);
    const auto* loaded_patch = loaded.world().find_local_patch(region);
    check(loaded_patch != nullptr && same_patch(*after, *loaded_patch),
          "compound checkpoint restores exact vegetation history");

    const auto future_a = state.advance_day_full();
    const auto future_b = loaded.advance_day_full();
    const auto* future_patch_a = state.world().find_local_patch(region);
    const auto* future_patch_b = loaded.world().find_local_patch(region);
    check(future_a.environment.weather.precipitation_m3 ==
              future_b.environment.weather.precipitation_m3 &&
          future_a.environment.water.storage_after_m3 ==
              future_b.environment.water.storage_after_m3 &&
          future_a.vegetation.biomass_area_after_m2 ==
              future_b.vegetation.biomass_area_after_m2 &&
          future_a.vegetation.disturbance_area_after_m2 ==
              future_b.vegetation.disturbance_area_after_m2 &&
          future_patch_a != nullptr && future_patch_b != nullptr &&
          same_patch(*future_patch_a, *future_patch_b),
          "checkpoint reload has exact deterministic vegetation future evolution");

    std::filesystem::remove(checkpoint);

    if (failures != 0) {
        std::cerr << failures << " vegetation simulation test(s) failed\n";
        return 1;
    }
    std::cout << "All vegetation simulation tests passed\n";
    return 0;
}
