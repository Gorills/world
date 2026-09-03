#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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

worldsim::WorldConfig config(worldsim::Seed seed = 401) {
    worldsim::WorldConfig cfg;
    cfg.seed = seed;
    cfg.bounds = {-121'333.0, -101'777.0, 241'111.0, 201'555.0};
    return cfg;
}

bool same_cell(const worldsim::ContinentalWaterCellState& a,
               const worldsim::ContinentalWaterCellState& b) {
    return a.snow_water_equivalent_mm == b.snow_water_equivalent_mm &&
           a.surface_water_mm == b.surface_water_mm &&
           a.soil_water_mm == b.soil_water_mm &&
           a.groundwater_mm == b.groundwater_mm &&
           a.last_evapotranspiration_mm == b.last_evapotranspiration_mm &&
           a.last_quick_runoff_mm == b.last_quick_runoff_mm &&
           a.last_baseflow_mm == b.last_baseflow_mm &&
           a.last_routed_discharge_m3_s == b.last_routed_discharge_m3_s;
}

bool same_cell(const worldsim::DynamicHydrologyCellState& a,
               const worldsim::DynamicHydrologyCellState& b) {
    return a.coord == b.coord && a.active == b.active &&
           a.snow_water_equivalent_mm == b.snow_water_equivalent_mm &&
           a.surface_water_mm == b.surface_water_mm &&
           a.soil_water_mm == b.soil_water_mm &&
           a.groundwater_mm == b.groundwater_mm &&
           a.last_evapotranspiration_mm == b.last_evapotranspiration_mm &&
           a.last_quick_runoff_mm == b.last_quick_runoff_mm &&
           a.last_baseflow_mm == b.last_baseflow_mm &&
           a.last_routed_discharge_m3_s == b.last_routed_discharge_m3_s;
}

std::vector<char> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read persistence test file");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write persistence test file");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("failed to write persistence test file");
}

template <typename T>
T read_pod_at(const std::vector<char>& bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        throw std::runtime_error("persistence test offset outside file");
    }
    T value{};
    const auto* src = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
    auto* dst = reinterpret_cast<unsigned char*>(&value);
    for (std::size_t i = 0; i < sizeof(T); ++i) dst[i] = src[i];
    return value;
}

template <typename T>
void write_pod_at(std::vector<char>& bytes, std::size_t offset, const T& value) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        throw std::runtime_error("persistence test offset outside file");
    }
    const auto* src = reinterpret_cast<const unsigned char*>(&value);
    auto* dst = reinterpret_cast<unsigned char*>(bytes.data() + offset);
    for (std::size_t i = 0; i < sizeof(T); ++i) dst[i] = src[i];
}

worldsim::CellCoord find_refinable_land(const worldsim::ContinentalHydrologyResult& topology) {
    for (const auto& cell : topology.cells) {
        if (!cell.ocean) return cell.coord;
    }
    throw std::runtime_error("persistence fixture has no land cell");
}

} // namespace

int main() {
    using namespace worldsim;

    const auto root = std::filesystem::temp_directory_path() / "worldsim_multires_water_persistence_test";
    const auto valid_path = root.string() + ".bin";
    const auto truncated_path = root.string() + ".truncated.bin";
    const auto trailing_path = root.string() + ".trailing.bin";
    const auto clock_path = root.string() + ".clock.bin";
    const auto duplicate_path = root.string() + ".duplicate.bin";

    World world(config());
    const auto topology = world.analyze_continental_hydrology({0.1f});
    auto state = make_multiresolution_water_state(world, topology);
    const auto parent = find_refinable_land(topology);
    (void)materialize_refined_water_tile(world, topology, state, parent);

    for (int day = 0; day < 3; ++day) {
        const auto forcing = make_smooth_continental_daily_forcing(state.coarse_state());
        (void)advance_multiresolution_water_day(world, state, forcing);
    }
    save_multiresolution_water_state(state, valid_path);
    auto loaded = load_multiresolution_water_state(world, topology, valid_path);

    check(loaded.simulated_day() == state.simulated_day(), "persistence preserves exact global day");
    check(loaded.refined_tile_count() == state.refined_tile_count() && loaded.is_refined(parent),
          "persistence preserves sparse refined ownership");
    check(loaded.coarse_state().cells().size() == state.coarse_state().cells().size(),
          "persistence preserves coarse state dimensions");
    bool coarse_equal = loaded.coarse_state().cells().size() == state.coarse_state().cells().size();
    if (coarse_equal) {
        for (std::size_t i = 0; i < state.coarse_state().cells().size(); ++i) {
            coarse_equal = coarse_equal && same_cell(loaded.coarse_state().cells()[i], state.coarse_state().cells()[i]);
        }
    }
    check(coarse_equal, "persistence round trip preserves all coarse stores and diagnostics exactly");

    const auto& before_tile = state.refined_tile(parent);
    const auto& after_tile = loaded.refined_tile(parent);
    bool refined_equal = before_tile.simulated_day == after_tile.simulated_day &&
                         before_tile.cells.size() == after_tile.cells.size();
    if (refined_equal) {
        for (std::size_t i = 0; i < before_tile.cells.size(); ++i) {
            refined_equal = refined_equal && same_cell(before_tile.cells[i], after_tile.cells[i]);
        }
    }
    check(refined_equal, "persistence round trip preserves refined tile state exactly");

    auto original_aggregated = state;
    auto loaded_aggregated = loaded;
    aggregate_refined_water_tile(world, original_aggregated, parent);
    aggregate_refined_water_tile(world, loaded_aggregated, parent);
    check(same_cell(original_aggregated.coarse_state().cell(parent),
                    loaded_aggregated.coarse_state().cell(parent)),
          "reloaded refined ownership aggregates to the same authoritative L0 state");

    const auto bytes = read_bytes(valid_path);
    check(bytes.size() > 32, "persistence fixture produced a nontrivial file");

    auto truncated = bytes;
    truncated.resize(truncated.size() / 2);
    write_bytes(truncated_path, truncated);
    bool truncated_rejected = false;
    try {
        (void)load_multiresolution_water_state(world, topology, truncated_path);
    } catch (const std::runtime_error&) {
        truncated_rejected = true;
    }
    check(truncated_rejected, "truncated multiresolution water save is rejected");

    auto trailing = bytes;
    trailing.push_back(static_cast<char>(0x5a));
    write_bytes(trailing_path, trailing);
    bool trailing_rejected = false;
    try {
        (void)load_multiresolution_water_state(world, topology, trailing_path);
    } catch (const std::runtime_error&) {
        trailing_rejected = true;
    }
    check(trailing_rejected, "unexpected persistence tail data is rejected");

    // Fixed v1 layout through the coarse array:
    // magic(8) + version(4) + WorldConfig serialized fields(56) + parameters(40) +
    // day(8) + min coord(16) + dimensions(8) + coarse count(8) = 148 bytes.
    constexpr std::size_t kCoarseCellsOffset = 148;
    constexpr std::size_t kSerializedCoarseCellBytes = sizeof(float) * 8;
    const auto coarse_count = state.coarse_state().cells().size();
    const auto refined_count_offset = kCoarseCellsOffset + coarse_count * kSerializedCoarseCellBytes;
    const auto first_parent_offset = refined_count_offset + sizeof(std::uint64_t);
    const auto first_tile_day_offset = first_parent_offset + sizeof(std::int64_t) * 2;

    auto wrong_clock = bytes;
    const std::int64_t different_day = state.simulated_day() + 1;
    write_pod_at(wrong_clock, first_tile_day_offset, different_day);
    write_bytes(clock_path, wrong_clock);
    bool clock_rejected = false;
    try {
        (void)load_multiresolution_water_state(world, topology, clock_path);
    } catch (const std::runtime_error&) {
        clock_rejected = true;
    }
    check(clock_rejected, "refined tile/global clock mismatch is rejected");

    auto duplicate = bytes;
    const auto refined_count = read_pod_at<std::uint64_t>(duplicate, refined_count_offset);
    check(refined_count == 1, "persistence duplicate-parent fixture has exactly one refined tile");
    if (refined_count == 1) {
        const std::vector<char> first_record(duplicate.begin() + static_cast<std::ptrdiff_t>(first_parent_offset),
                                             duplicate.end());
        const std::uint64_t two = 2;
        write_pod_at(duplicate, refined_count_offset, two);
        duplicate.insert(duplicate.end(), first_record.begin(), first_record.end());
        write_bytes(duplicate_path, duplicate);
        bool duplicate_rejected = false;
        try {
            (void)load_multiresolution_water_state(world, topology, duplicate_path);
        } catch (const std::runtime_error&) {
            duplicate_rejected = true;
        }
        check(duplicate_rejected, "duplicate refined parent ownership is rejected");
    }

    auto wrong_cfg = config();
    ++wrong_cfg.seed;
    World wrong_world(wrong_cfg);
    const auto wrong_topology = wrong_world.analyze_continental_hydrology({0.1f});
    bool wrong_world_rejected = false;
    try {
        (void)load_multiresolution_water_state(wrong_world, wrong_topology, valid_path);
    } catch (const std::runtime_error&) {
        wrong_world_rejected = true;
    }
    check(wrong_world_rejected, "saved multiresolution state rejects a different world identity");

    std::filesystem::remove(valid_path);
    std::filesystem::remove(truncated_path);
    std::filesystem::remove(trailing_path);
    std::filesystem::remove(clock_path);
    std::filesystem::remove(duplicate_path);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All multiresolution water persistence tests passed\n";
    return 0;
}
