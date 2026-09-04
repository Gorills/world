#include "worldsim/world.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool same_cell(const worldsim::LocalCell& a, const worldsim::LocalCell& b) {
    return a.elevation_m == b.elevation_m &&
        a.terrain_roughness == b.terrain_roughness &&
        a.forest_potential == b.forest_potential &&
        a.disturbance == b.disturbance &&
        a.vegetation_biomass == b.vegetation_biomass;
}

std::vector<unsigned char> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read vegetation test file");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write vegetation test file");
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write vegetation test bytes");
}

std::uint32_t read_version(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path);
    if (bytes.size() < 12) throw std::runtime_error("vegetation file too small");
    std::uint32_t version{};
    std::memcpy(&version, bytes.data() + 8, sizeof(version));
    return version;
}

void make_v2_from_single_patch_v3(
    const std::filesystem::path& source,
    const std::filesystem::path& target) {
    const auto bytes = read_bytes(source);
    constexpr std::size_t kHeaderBytes = 76;
    constexpr std::size_t kPatchCoordBytes = 16;
    constexpr std::size_t kV3CellBytes = 20;
    constexpr std::size_t kV2CellBytes = 16;
    constexpr std::size_t kCells = worldsim::kLocalCellCount;
    const std::size_t expected =
        kHeaderBytes + kPatchCoordBytes + kCells * kV3CellBytes;
    if (bytes.size() != expected) {
        throw std::runtime_error("unexpected single-patch v3 fixture size");
    }

    std::vector<unsigned char> old;
    old.reserve(kHeaderBytes + kPatchCoordBytes + kCells * kV2CellBytes);
    old.insert(old.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes));
    const std::uint32_t v2 = 2;
    std::memcpy(old.data() + 8, &v2, sizeof(v2));

    const std::size_t patch_start = kHeaderBytes;
    old.insert(
        old.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(patch_start),
        bytes.begin() + static_cast<std::ptrdiff_t>(patch_start + kPatchCoordBytes));
    std::size_t cursor = patch_start + kPatchCoordBytes;
    for (std::size_t i = 0; i < kCells; ++i) {
        old.insert(
            old.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
            bytes.begin() + static_cast<std::ptrdiff_t>(cursor + kV2CellBytes));
        cursor += kV3CellBytes;
    }
    write_bytes(target, old);
}

worldsim::WorldConfig config() {
    worldsim::WorldConfig cfg;
    cfg.seed = 14001;
    cfg.bounds = {0.0, 0.0, 2048.0, 2048.0};
    cfg.sea_level_m = -10'000.0f;
    return cfg;
}
} // namespace

int main() {
    using namespace worldsim;

    World world(config());
    const CellCoord region{0, 0};
    const auto& initial = world.materialize_local_patch(region);
    check(world.materialized_patch_count() == 1,
          "vegetation materialization creates one sparse L2 patch");
    check(initial.cells[0].vegetation_biomass == initial.cells[0].forest_potential,
          "new terrestrial local cell starts at forest potential biomass");

    const float potential = initial.cells[0].forest_potential;
    check(world.disturb_surface({0.0, 0.0}, {64.0, 64.0}, 0.8f) == 1,
          "surface disturbance affects exactly one local vegetation cell");
    const auto* disturbed = world.find_local_patch(region);
    check(disturbed != nullptr, "disturbed vegetation patch remains materialized");
    if (!disturbed) return 1;
    check(disturbed->cells[0].disturbance == 0.8f &&
          disturbed->cells[0].vegetation_biomass <= potential * 0.2f + 1e-6f,
          "disturbance immediately suppresses live biomass");

    const auto before_warm = disturbed->cells;
    const VegetationForcing warm{{0, 0}, 20.0f, 1.0f};
    const auto warm_report = world.advance_materialized_vegetation_day({warm});
    const auto* after_warm = world.find_local_patch(region);
    check(after_warm != nullptr, "warm vegetation advance preserves patch ownership");
    if (!after_warm) return 1;
    check(warm_report.patch_count == 1 && warm_report.land_cell_count == kLocalCellCount &&
          warm_report.land_area_m2 == 1024.0 * 1024.0,
          "vegetation report covers exactly the materialized terrestrial patch");
    check(after_warm->cells[0].disturbance < before_warm[0].disturbance,
          "persistent disturbance decays during vegetation day");
    check(after_warm->cells[0].vegetation_biomass > before_warm[0].vegetation_biomass,
          "warm moist forcing recovers disturbed biomass");
    check(warm_report.biomass_area_after_m2 > warm_report.biomass_area_before_m2 &&
          warm_report.disturbance_area_after_m2 < warm_report.disturbance_area_before_m2,
          "vegetation report records recovery and disturbance decay");

    const float biomass_before_cold = after_warm->cells[0].vegetation_biomass;
    const float disturbance_before_cold = after_warm->cells[0].disturbance;
    const VegetationForcing cold_dry{{0, 0}, -20.0f, 0.0f};
    (void)world.advance_materialized_vegetation_day({cold_dry});
    const auto* after_cold = world.find_local_patch(region);
    check(after_cold != nullptr, "cold vegetation advance preserves patch");
    if (!after_cold) return 1;
    check(after_cold->cells[0].vegetation_biomass == biomass_before_cold &&
          after_cold->cells[0].disturbance < disturbance_before_cold,
          "cold dry day stalls biomass recovery while disturbance still decays");
    check(world.materialized_patch_count() == 1,
          "vegetation advance never materializes additional L2 patches");

    const auto atomic_before = after_cold->cells;
    bool invalid_rejected = false;
    try {
        (void)world.advance_materialized_vegetation_day(
            {{{0, 0}, std::numeric_limits<float>::quiet_NaN(), 0.5f}});
    } catch (const std::invalid_argument&) {
        invalid_rejected = true;
    }
    const auto* atomic_after = world.find_local_patch(region);
    bool atomic_same = atomic_after != nullptr;
    for (std::size_t i = 0; atomic_same && i < kLocalCellCount; ++i) {
        atomic_same = same_cell(atomic_before[i], atomic_after->cells[i]);
    }
    check(invalid_rejected && atomic_same,
          "invalid vegetation forcing is rejected atomically");

    bool missing_rejected = false;
    try {
        (void)world.advance_materialized_vegetation_day({});
    } catch (const std::invalid_argument&) {
        missing_rejected = true;
    }
    check(missing_rejected,
          "vegetation forcing must cover every materialized patch exactly once");

    const auto temp = std::filesystem::temp_directory_path();
    const auto v3_path = temp / "worldsim_vegetation_v3.ws";
    const auto roundtrip_path = temp / "worldsim_vegetation_roundtrip.ws";
    world.save(v3_path);
    check(read_version(v3_path) == 3u,
          "world vegetation persistence writes format v3");
    auto loaded = World::load(v3_path);
    const auto* loaded_patch = loaded.find_local_patch(region);
    bool exact = loaded_patch != nullptr;
    const auto* source_patch = world.find_local_patch(region);
    for (std::size_t i = 0; exact && i < kLocalCellCount; ++i) {
        exact = same_cell(source_patch->cells[i], loaded_patch->cells[i]);
    }
    check(exact, "world v3 vegetation persistence round-trips exact local history");
    loaded.save(roundtrip_path);
    check(read_bytes(v3_path) == read_bytes(roundtrip_path),
          "world v3 vegetation serialization remains canonical");

    World legacy_source(config());
    (void)legacy_source.materialize_local_patch(region);
    check(legacy_source.disturb_surface({0.0, 0.0}, {64.0, 64.0}, 0.6f) == 1,
          "legacy migration fixture records disturbance");
    const auto legacy_v3 = temp / "worldsim_vegetation_legacy_source_v3.ws";
    const auto legacy_v2 = temp / "worldsim_vegetation_legacy_v2.ws";
    legacy_source.save(legacy_v3);
    make_v2_from_single_patch_v3(legacy_v3, legacy_v2);
    check(read_version(legacy_v2) == 2u,
          "test fixture reconstructs a real v2 world layout");
    auto migrated = World::load(legacy_v2);
    const auto* migrated_patch = migrated.find_local_patch(region);
    const auto* legacy_patch = legacy_source.find_local_patch(region);
    check(migrated_patch != nullptr && legacy_patch != nullptr,
          "v2 vegetation migration preserves materialized patch");
    if (migrated_patch && legacy_patch) {
        const auto& m = migrated_patch->cells[0];
        const auto& old = legacy_patch->cells[0];
        check(m.elevation_m == old.elevation_m &&
              m.terrain_roughness == old.terrain_roughness &&
              m.forest_potential == old.forest_potential &&
              m.disturbance == old.disturbance,
              "v2 vegetation migration preserves all pre-existing local state");
        check(std::abs(
                  static_cast<double>(m.vegetation_biomass) -
                  static_cast<double>(m.forest_potential) * (1.0 - m.disturbance)) < 2e-6,
              "v2 migration derives biomass from potential and persisted disturbance");
    }

    std::filesystem::remove(v3_path);
    std::filesystem::remove(roundtrip_path);
    std::filesystem::remove(legacy_v3);
    std::filesystem::remove(legacy_v2);

    if (failures != 0) {
        std::cerr << failures << " vegetation test(s) failed\n";
        return 1;
    }
    std::cout << "All vegetation tests passed\n";
    return 0;
}
