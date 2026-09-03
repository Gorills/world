#include "worldsim/simulation.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
} // namespace

int main() {
    using namespace worldsim;

    try {
        WorldConfig cfg;
        cfg.seed = 2301;
        cfg.bounds = {-32'768.0, -32'768.0, 65'536.0, 65'536.0};
        cfg.sea_level_m = -10'000.0f;

        World legacy(cfg);
        check(legacy.disturb_surface({0.0, 0.0}, {64.0, 64.0}, 0.75f) == 1,
              "legacy world fixture materializes one disturbed local cell");
        check(legacy.materialized_patch_count() == 1,
              "legacy world fixture owns one persistent L2 patch");
        const auto* legacy_patch = legacy.find_local_patch({0, 0});
        check(legacy_patch != nullptr && legacy_patch->cells[0].disturbance == 0.75f,
              "legacy world fixture stores exact disturbance history");

        auto simulation = SimulationState::from_world(std::move(legacy));
        check(simulation.simulated_day() == 0 &&
              simulation.weather().simulated_day() == 0 &&
              simulation.water().simulated_day() == 0,
              "legacy World migration creates one aligned day-zero simulation");
        check(simulation.world().materialized_patch_count() == 1,
              "legacy World migration preserves persistent L2 ownership");
        const auto* migrated_patch = simulation.world().find_local_patch({0, 0});
        check(migrated_patch != nullptr && migrated_patch->cells[0].disturbance == 0.75f,
              "legacy World migration preserves exact L2 disturbance history");

        const auto checkpoint = std::filesystem::temp_directory_path() /
            "worldsim_simulation_migration.bin";
        simulation.save_checkpoint(checkpoint);
        auto loaded = SimulationState::load_checkpoint(checkpoint);
        check(loaded.world().materialized_patch_count() == 1,
              "migrated World history survives compound checkpoint reload");
        const auto* loaded_patch = loaded.world().find_local_patch({0, 0});
        check(loaded_patch != nullptr && loaded_patch->cells[0].disturbance == 0.75f,
              "compound checkpoint preserves migrated L2 values exactly");
        std::filesystem::remove(checkpoint);
    } catch (const std::exception& e) {
        ++failures;
        std::cerr << "UNCAUGHT simulation migration exception: " << e.what() << '\n';
    }

    if (failures != 0) {
        std::cerr << failures << " simulation migration test(s) failed\n";
        return 1;
    }
    std::cout << "All simulation migration tests passed\n";
    return 0;
}
