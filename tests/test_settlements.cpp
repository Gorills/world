#include "worldsim/simulation.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
std::vector<char> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(in)), {});
}
void write_bytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
bool same_settlements(const worldsim::SimulationState& a, const worldsim::SimulationState& b) {
    return a.settlements() == b.settlements() &&
        a.settlement_state().next_id() == b.settlement_state().next_id();
}
} // namespace

int main() {
    using namespace worldsim;
    try {
        WorldConfig cfg;
        cfg.seed = 4015;
        cfg.bounds = {-32'768.0, -32'768.0, 65'536.0, 65'536.0};
        cfg.sea_level_m = -10'000.0f;
        SimulationState state(cfg);

        check(state.settlements().empty(), "simulation starts with sparse empty settlement authority");
        const auto patches_before_query = state.world().materialized_patch_count();
        const auto tiles_before_query = state.water().refined_tile_count();
        const auto probe = state.settlement_suitability({0, 0});
        check(std::isfinite(probe.environmental_capacity) && probe.environmental_capacity >= 0.0,
              "read-only suitability is finite and non-negative");
        check(state.world().materialized_patch_count() == patches_before_query &&
              state.water().refined_tile_count() == tiles_before_query,
              "read-only settlement query does not materialize L1/L2 state");

        const auto id1 = state.found_settlement({0, 0}, 120.0);
        const auto id2 = state.found_settlement({1, 0}, 80.0);
        check(id1 == 1 && id2 == 2, "settlement ids are deterministic and monotonic");
        check(state.settlements().size() == 2 && state.world().materialized_patch_count() == 2,
              "founding creates sparse entities and their local vegetation history");
        check(state.settlement(id1) && state.settlement(id1)->regional_coord == CellCoord{0, 0} &&
              state.settlement(id1)->population == 120.0 &&
              state.settlement(id1)->founded_day == 0,
              "settlement query returns exact persistent identity");

        bool duplicate_threw = false;
        try { (void)state.found_settlement({0, 0}, 10.0); }
        catch (const std::invalid_argument&) { duplicate_threw = true; }
        check(duplicate_threw && state.settlements().size() == 2,
              "duplicate regional ownership is rejected without adding an entity");

        bool invalid_population_threw = false;
        try { (void)state.found_settlement({2, 0}, std::numeric_limits<double>::quiet_NaN()); }
        catch (const std::invalid_argument&) { invalid_population_threw = true; }
        check(invalid_population_threw && state.settlements().size() == 2,
              "non-finite founding population is rejected");

        const auto before_disturbance = state.settlement_suitability({0, 0});
        const double s = static_cast<double>(cfg.regional_cell_m);
        check(state.disturb_surface({0.0, 0.0}, {s, s}, 0.8f) > 0,
              "settlement fixture applies persistent disturbance");
        const auto after_disturbance = state.settlement_suitability({0, 0});
        check(after_disturbance.disturbance_factor < before_disturbance.disturbance_factor &&
              after_disturbance.vegetation_factor <= before_disturbance.vegetation_factor &&
              after_disturbance.environmental_capacity < before_disturbance.environmental_capacity,
              "disturbance and suppressed biomass reduce settlement suitability");

        const auto water_factor_before = after_disturbance.water_factor;
        for (int i = 0; i < 5; ++i) (void)state.advance_day_full();
        const auto water_factor_after = state.settlement_suitability({0, 0}).water_factor;
        check(water_factor_before >= 0.25 && water_factor_before <= 1.0 &&
              water_factor_after >= 0.25 && water_factor_after <= 1.0,
              "authoritative water contribution remains bounded");
        for (const auto& value : state.settlements()) {
            check(std::isfinite(value.population) && value.population >= 0.0,
                  "population evolution remains finite and non-negative");
        }

        const auto checkpoint = std::filesystem::temp_directory_path() / "worldsim_settlements_v2.bin";
        const auto legacy = std::filesystem::temp_directory_path() / "worldsim_settlements_v1.bin";
        state.save_checkpoint(checkpoint);
        auto loaded = SimulationState::load_checkpoint(checkpoint);
        check(same_settlements(state, loaded), "settlement checkpoint round-trips exact entity state");
        const auto future_a = state.advance_day_full();
        const auto future_b = loaded.advance_day_full();
        check(future_a.settlements.settlement_count == future_b.settlements.settlement_count &&
              future_a.settlements.population_before == future_b.settlements.population_before &&
              future_a.settlements.population_after == future_b.settlements.population_after &&
              future_a.settlements.environmental_capacity == future_b.settlements.environmental_capacity &&
              same_settlements(state, loaded),
              "settlement checkpoint preserves exact deterministic future");

        // Convert a v2 compound container to the exact pre-settlement v1 shape:
        // same first three sections, legacy version/count, no fourth descriptor/payload.
        auto bytes = read_bytes(checkpoint);
        constexpr std::size_t magic_bytes = 8;
        constexpr std::size_t version_offset = magic_bytes;
        constexpr std::size_t day_bytes = sizeof(std::int64_t);
        constexpr std::size_t count_offset = version_offset + sizeof(std::uint32_t) + day_bytes;
        constexpr std::size_t descriptors_offset = count_offset + sizeof(std::uint32_t);
        constexpr std::size_t descriptor_bytes =
            sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t);
        std::uint64_t first_three_payload = 0;
        for (std::size_t i = 0; i < 3; ++i) {
            std::uint64_t size{};
            std::memcpy(&size,
                bytes.data() + descriptors_offset + i * descriptor_bytes + sizeof(std::uint32_t),
                sizeof(size));
            first_three_payload += size;
        }
        const std::size_t v2_header = descriptors_offset + 4 * descriptor_bytes;
        const std::size_t v1_header = descriptors_offset + 3 * descriptor_bytes;
        std::vector<char> legacy_bytes;
        legacy_bytes.reserve(v1_header + static_cast<std::size_t>(first_three_payload));
        legacy_bytes.insert(legacy_bytes.end(), bytes.begin(), bytes.begin() + v1_header);
        legacy_bytes.insert(
            legacy_bytes.end(), bytes.begin() + v2_header,
            bytes.begin() + v2_header + static_cast<std::size_t>(first_three_payload));
        const std::uint32_t version1 = 1;
        const std::uint32_t count3 = 3;
        std::memcpy(legacy_bytes.data() + version_offset, &version1, sizeof(version1));
        std::memcpy(legacy_bytes.data() + count_offset, &count3, sizeof(count3));
        write_bytes(legacy, legacy_bytes);
        auto migrated = SimulationState::load_checkpoint(legacy);
        check(migrated.settlements().empty() && migrated.settlement_state().next_id() == 1,
              "pre-settlement compound checkpoint migrates to empty settlement authority");

        std::filesystem::remove(checkpoint);
        std::filesystem::remove(legacy);
    } catch (const std::exception& e) {
        ++failures;
        std::cerr << "UNCAUGHT settlement test exception: " << e.what() << '\n';
    }
    if (failures) {
        std::cerr << failures << " settlement test(s) failed\n";
        return 1;
    }
    std::cout << "All settlement tests passed\n";
    return 0;
}
