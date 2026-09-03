#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
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

bool near(double a, double b, double abs_eps = 1e-9, double rel_eps = 1e-12) {
    return std::abs(a - b) <= std::max(abs_eps, std::max(std::abs(a), std::abs(b)) * rel_eps);
}

worldsim::WorldConfig config() {
    worldsim::WorldConfig cfg;
    cfg.seed = 3107;
    cfg.bounds = {0.0, 0.0,
                  2.0 * static_cast<double>(cfg.climate_cell_m),
                  2.0 * static_cast<double>(cfg.climate_cell_m)};
    cfg.sea_level_m = -10'000.0f;
    return cfg;
}

worldsim::ContinentalHydrologyCell cell(
    worldsim::CellCoord coord,
    float filled_elevation_m,
    bool has_downstream,
    worldsim::CellCoord downstream) {
    worldsim::ContinentalHydrologyCell out;
    out.coord = coord;
    out.terrain_elevation_m = filled_elevation_m;
    out.filled_elevation_m = filled_elevation_m;
    out.downstream_coord = downstream;
    out.has_downstream = has_downstream;
    out.terminal_outlet_coord = {1, 0};
    out.basin_id = 1;
    return out;
}

worldsim::ContinentalHydrologyResult topology(const worldsim::WorldConfig& cfg) {
    worldsim::ContinentalHydrologyResult out;
    out.config = cfg;
    out.min_coord = {0, 0};
    out.width_cells = 2;
    out.height_cells = 2;
    out.cells = {
        cell({0, 0}, 100.0f, true, {1, 0}),   // flat cardinal: legacy one-day residence
        cell({1, 0}, 100.0f, false, {}),      // terminal outlet: nominal one-cell residence
        cell({0, 1}, 110.0f, true, {1, 0}),   // downhill diagonal: longer than cardinal
        cell({1, 1}, 120.0f, true, {1, 0}),   // downhill cardinal: faster than flat
    };
    return out;
}

worldsim::DynamicHydrologyParameters routing_parameters() {
    worldsim::DynamicHydrologyParameters p;
    p.soil_capacity_mm = 0.0f;
    p.field_capacity_mm = 0.0f;
    p.wilting_point_mm = 0.0f;
    p.infiltration_capacity_mm_per_day = 0.0f;
    p.surface_storage_capacity_mm = 0.0f;
    p.percolation_rate_per_day = 0.0f;
    p.groundwater_recession_per_day = 0.0f;
    p.initial_soil_water_mm = 0.0f;
    p.initial_groundwater_mm = 0.0f;
    return p;
}

std::vector<worldsim::ContinentalWaterForcing> pulse_forcing() {
    std::vector<worldsim::ContinentalWaterForcing> forcing(4);
    forcing[0] = {100.0f, 10.0f, 0.0f};
    forcing[2] = {100.0f, 10.0f, 0.0f};
    forcing[3] = {100.0f, 10.0f, 0.0f};
    return forcing;
}

bool same_transport(const worldsim::ChannelTransportProperties& a,
                    const worldsim::ChannelTransportProperties& b) {
    return a.reach_length_m == b.reach_length_m &&
           a.downhill_gradient == b.downhill_gradient &&
           a.residence_days == b.residence_days &&
           a.release_fraction_per_day == b.release_fraction_per_day;
}

std::uint32_t read_version(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read channel transport persistence fixture");
    in.seekg(8);
    std::uint32_t version{};
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!in) throw std::runtime_error("cannot read channel transport persistence version");
    return version;
}

void rewrite_version(const std::filesystem::path& path, std::uint32_t version) {
    std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!io) throw std::runtime_error("cannot rewrite channel transport persistence version");
    io.seekp(8);
    io.write(reinterpret_cast<const char*>(&version), sizeof(version));
    if (!io) throw std::runtime_error("cannot write channel transport persistence version");
}
} // namespace

int main() {
    using namespace worldsim;

    const auto cfg = config();
    World world(cfg);
    const auto topo = topology(cfg);
    auto state = make_multiresolution_water_state(world, topo, routing_parameters());

    const auto& flat = state.channel_transport({0, 0});
    const auto& terminal = state.channel_transport({1, 0});
    const auto& diagonal = state.channel_transport({0, 1});
    const auto& steep = state.channel_transport({1, 1});
    const double legacy_release = 1.0 - std::exp(-1.0);
    const double diagonal_length = std::sqrt(2.0) * static_cast<double>(cfg.climate_cell_m);
    const double diagonal_gradient = 10.0 / diagonal_length;
    const double diagonal_residence = std::sqrt(2.0) / (1.0 + diagonal_gradient);

    check(flat.reach_length_m == static_cast<double>(cfg.climate_cell_m) &&
          flat.downhill_gradient == 0.0 && flat.residence_days == 1.0 &&
          near(flat.release_fraction_per_day, legacy_release),
          "flat cardinal reach preserves the legacy one-day linear reservoir");
    check(terminal.reach_length_m == static_cast<double>(cfg.climate_cell_m) &&
          terminal.downhill_gradient == 0.0 && terminal.residence_days == 1.0 &&
          near(terminal.release_fraction_per_day, legacy_release),
          "terminal outlet preserves the nominal one-cell legacy residence");
    check(near(diagonal.reach_length_m, diagonal_length) &&
          near(diagonal.downhill_gradient, diagonal_gradient) &&
          near(diagonal.residence_days, diagonal_residence) &&
          diagonal.residence_days > flat.residence_days &&
          diagonal.release_fraction_per_day < flat.release_fraction_per_day,
          "diagonal reach length deterministically increases residence");
    check(steep.reach_length_m == static_cast<double>(cfg.climate_cell_m) &&
          steep.downhill_gradient > 0.0 && steep.residence_days < flat.residence_days &&
          steep.release_fraction_per_day > flat.release_fraction_per_day,
          "downhill cardinal gradient deterministically decreases residence");

    auto malformed = topo;
    malformed.cells[0].filled_elevation_m = std::numeric_limits<float>::quiet_NaN();
    bool non_finite_rejected = false;
    try {
        (void)make_multiresolution_water_state(world, malformed, routing_parameters());
    } catch (const std::invalid_argument&) {
        non_finite_rejected = true;
    }
    check(non_finite_rejected,
          "channel transport rejects caller topology with non-finite filled elevation");

    const auto pulse = pulse_forcing();
    const auto day1 = advance_multiresolution_water_day(world, state, pulse);
    const double flat_day1 = state.channel_storage_m3({0, 0});
    const double diagonal_day1 = state.channel_storage_m3({0, 1});
    const double steep_day1 = state.channel_storage_m3({1, 1});
    check(day1.terminal_outflow_m3 == 0.0 && flat_day1 > 0.0 &&
          flat_day1 == diagonal_day1 && flat_day1 == steep_day1 &&
          state.channel_storage_m3({1, 0}) == 0.0,
          "new runoff remains in its source channel for the pulse day");

    const std::vector<ContinentalWaterForcing> dry(4);
    const auto day2 = advance_multiresolution_water_day(world, state, dry);
    const double flat_day2 = state.channel_storage_m3({0, 0});
    const double diagonal_day2 = state.channel_storage_m3({0, 1});
    const double steep_day2 = state.channel_storage_m3({1, 1});
    check(near(flat_day1 - flat_day2, flat_day1 * flat.release_fraction_per_day, 1e-6) &&
          near(diagonal_day1 - diagonal_day2,
               diagonal_day1 * diagonal.release_fraction_per_day, 1e-6) &&
          near(steep_day1 - steep_day2, steep_day1 * steep.release_fraction_per_day, 1e-6),
          "daily routing uses the exposed per-reach release fractions");
    check(diagonal_day2 > flat_day2 && flat_day2 > steep_day2,
          "equal source storage drains at reach-aware relative rates");
    check(day2.terminal_outflow_m3 == 0.0 && state.channel_storage_m3({1, 0}) > 0.0,
          "reach-aware releases still cross at most one L0 edge per day");
    check(std::abs(day2.water_balance_error_m3) < 0.5,
          "reach-aware channel routing conserves water");

    const auto root = std::filesystem::temp_directory_path() / "worldsim_channel_transport";
    const auto v4_path = root.string() + ".v4.bin";
    const auto v3_path = root.string() + ".v3.bin";
    auto persistence_source = make_multiresolution_water_state(world, topo, routing_parameters());
    (void)advance_multiresolution_water_day(world, persistence_source, pulse);
    save_multiresolution_water_state(persistence_source, v4_path);
    check(read_version(v4_path) == 4u,
          "reach-aware channel persistence writes semantic format v4");
    std::filesystem::copy_file(
        v4_path, v3_path, std::filesystem::copy_options::overwrite_existing);
    rewrite_version(v3_path, 3u);

    auto loaded_v4 = load_multiresolution_water_state(world, topo, v4_path);
    auto migrated_v3 = load_multiresolution_water_state(world, topo, v3_path);
    bool migration_exact = loaded_v4.simulated_day() == migrated_v3.simulated_day();
    for (const auto& topo_cell : topo.cells) {
        migration_exact = migration_exact &&
            loaded_v4.channel_storage_m3(topo_cell.coord) ==
                migrated_v3.channel_storage_m3(topo_cell.coord) &&
            same_transport(
                loaded_v4.channel_transport(topo_cell.coord),
                migrated_v3.channel_transport(topo_cell.coord));
    }
    check(migration_exact,
          "v3 migration preserves water state and derives identical v4 transport metadata");

    const auto v4_next = advance_multiresolution_water_day(world, loaded_v4, dry);
    const auto v3_next = advance_multiresolution_water_day(world, migrated_v3, dry);
    bool future_exact = v4_next.storage_after_m3 == v3_next.storage_after_m3 &&
                        v4_next.terminal_outflow_m3 == v3_next.terminal_outflow_m3 &&
                        v4_next.water_balance_error_m3 == v3_next.water_balance_error_m3;
    for (const auto& topo_cell : topo.cells) {
        future_exact = future_exact &&
            loaded_v4.channel_storage_m3(topo_cell.coord) ==
                migrated_v3.channel_storage_m3(topo_cell.coord);
    }
    check(future_exact,
          "v3 migrated checkpoint follows the same deterministic reach-aware future evolution");

    std::filesystem::remove(v4_path);
    std::filesystem::remove(v3_path);

    if (failures != 0) {
        std::cerr << failures << " channel transport test(s) failed\n";
        return 1;
    }
    std::cout << "All reach-aware channel transport tests passed\n";
    return 0;
}
