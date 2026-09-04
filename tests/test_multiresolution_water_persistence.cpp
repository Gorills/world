#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <cmath>
#include <cstdint>
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

bool same_channels(const worldsim::MultiresolutionWaterState& a,
                   const worldsim::MultiresolutionWaterState& b) {
    if (a.coarse_state().cells().size() != b.coarse_state().cells().size()) return false;
    for (std::size_t i = 0; i < a.coarse_state().cells().size(); ++i) {
        const auto coord = a.coarse_state().coord_of(i);
        if (a.channel_storage_m3(coord) != b.channel_storage_m3(coord)) return false;
    }
    return true;
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
    const auto channel_bad_path = root.string() + ".channel-bad.bin";
    const auto channel_overflow_path = root.string() + ".channel-overflow.bin";
    const auto legacy_v5_path = root.string() + ".legacy-source-v5.bin";
    const auto legacy_v2_path = root.string() + ".legacy-v2.bin";

    World world(config());
    const auto topology = world.analyze_continental_hydrology({0.1f});
    auto state = make_multiresolution_water_state(world, topology);
    const auto parent = find_refinable_land(topology);
    (void)materialize_refined_water_tile(world, topology, state, parent);

    for (int day = 0; day < 3; ++day) {
        const auto forcing = make_smooth_continental_daily_forcing(state.coarse_state());
        (void)advance_multiresolution_water_day(world, state, forcing);
    }
    check(state.total_channel_storage_m3() > 0.0,
          "persistence fixture contains nonzero retained channel water");
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
    check(same_channels(state, loaded) &&
          loaded.total_channel_storage_m3() == state.total_channel_storage_m3(),
          "persistence round trip preserves all L0 channel storage exactly");

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
    const double channel_before_aggregate = state.channel_storage_m3(parent);
    aggregate_refined_water_tile(world, original_aggregated, parent);
    aggregate_refined_water_tile(world, loaded_aggregated, parent);
    check(same_cell(original_aggregated.coarse_state().cell(parent),
                    loaded_aggregated.coarse_state().cell(parent)),
          "reloaded refined ownership aggregates to the same authoritative L0 state");
    check(original_aggregated.channel_storage_m3(parent) == channel_before_aggregate &&
          loaded_aggregated.channel_storage_m3(parent) == channel_before_aggregate,
          "aggregation after reload leaves L0 channel storage unchanged");

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

    // Common layout through the coarse array:
    // magic(8) + version(4) + WorldConfig serialized fields(56) + parameters(40) +
    // day(8) + min coord(16) + dimensions(8) + coarse count(8) = 148 bytes.
    constexpr std::size_t kVersionOffset = 8;
    constexpr std::size_t kCoarseCellsOffset = 148;
    constexpr std::size_t kSerializedCoarseCellBytes = sizeof(float) * 8;
    const auto coarse_count = state.coarse_state().cells().size();
    const auto channel_count_offset = kCoarseCellsOffset + coarse_count * kSerializedCoarseCellBytes;
    const auto channel_values_offset = channel_count_offset + sizeof(std::uint64_t);
    const auto refined_count_offset = channel_values_offset + coarse_count * sizeof(double);
    const auto first_parent_offset = refined_count_offset + sizeof(std::uint64_t);
    const auto first_tile_day_offset = first_parent_offset + sizeof(std::int64_t) * 2;

    check(read_pod_at<std::uint64_t>(bytes, channel_count_offset) == coarse_count,
          "v5 persistence stores one channel volume per L0 cell");
    auto bad_channel = bytes;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    write_pod_at(bad_channel, channel_values_offset, nan);
    write_bytes(channel_bad_path, bad_channel);
    bool bad_channel_rejected = false;
    try {
        (void)load_multiresolution_water_state(world, topology, channel_bad_path);
    } catch (const std::runtime_error&) {
        bad_channel_rejected = true;
    }
    check(bad_channel_rejected, "non-finite saved channel storage is rejected");

    std::vector<std::size_t> land_indices;
    for (std::size_t i = 0; i < topology.cells.size() && land_indices.size() < 2; ++i) {
        if (!topology.cells[i].ocean) land_indices.push_back(i);
    }
    check(land_indices.size() == 2, "persistence fixture contains two terrestrial channel cells");
    if (land_indices.size() == 2) {
        auto overflow_channel = bytes;
        const double huge = std::numeric_limits<double>::max();
        write_pod_at(
            overflow_channel, channel_values_offset + land_indices[0] * sizeof(double), huge);
        write_pod_at(
            overflow_channel, channel_values_offset + land_indices[1] * sizeof(double), huge);
        write_bytes(channel_overflow_path, overflow_channel);
        bool overflow_channel_rejected = false;
        try {
            (void)load_multiresolution_water_state(world, topology, channel_overflow_path);
        } catch (const std::exception&) {
            overflow_channel_rejected = true;
        }
        check(overflow_channel_rejected,
              "finite per-cell channels whose authoritative total overflows are rejected on load");
    }

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

    // Construct an actual v2-shaped day-zero fixture from a v5 writer: no retained channel
    // water exists, so removing only the v5 channel block exactly represents the old semantics.
    auto legacy_state = make_multiresolution_water_state(world, topology);
    (void)materialize_refined_water_tile(world, topology, legacy_state, parent);
    save_multiresolution_water_state(legacy_state, legacy_v5_path);
    auto legacy_bytes = read_bytes(legacy_v5_path);
    const auto legacy_coarse_count = legacy_state.coarse_state().cells().size();
    const auto legacy_channel_count_offset =
        kCoarseCellsOffset + legacy_coarse_count * kSerializedCoarseCellBytes;
    const auto legacy_channel_block_bytes = sizeof(std::uint64_t) + legacy_coarse_count * sizeof(double);
    const std::uint32_t v2 = 2;
    write_pod_at(legacy_bytes, kVersionOffset, v2);
    legacy_bytes.erase(
        legacy_bytes.begin() + static_cast<std::ptrdiff_t>(legacy_channel_count_offset),
        legacy_bytes.begin() + static_cast<std::ptrdiff_t>(legacy_channel_count_offset + legacy_channel_block_bytes));
    write_bytes(legacy_v2_path, legacy_bytes);
    auto migrated_v2 = load_multiresolution_water_state(world, topology, legacy_v2_path);
    check(migrated_v2.simulated_day() == 0 && migrated_v2.is_refined(parent) &&
          migrated_v2.total_channel_storage_m3() == 0.0,
          "v2 persistence migrates to current semantics with zero invented channel water");

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
    std::filesystem::remove(channel_bad_path);
    std::filesystem::remove(channel_overflow_path);
    std::filesystem::remove(legacy_v5_path);
    std::filesystem::remove(legacy_v2_path);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All multiresolution water persistence tests passed\n";
    return 0;
}
