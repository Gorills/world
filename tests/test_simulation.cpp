#include "worldsim/simulation.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    if (!in) throw std::runtime_error("cannot read test checkpoint");
    return std::vector<char>((std::istreambuf_iterator<char>(in)), {});
}

void write_bytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write test checkpoint");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("failed to write test checkpoint");
}

bool same_weather(const worldsim::WeatherState& a, const worldsim::WeatherState& b) {
    if (a.simulated_day() != b.simulated_day() || a.cells().size() != b.cells().size()) return false;
    for (std::size_t i = 0; i < a.cells().size(); ++i) {
        if (a.cells()[i].temperature_anomaly_c != b.cells()[i].temperature_anomaly_c ||
            a.cells()[i].moisture_anomaly != b.cells()[i].moisture_anomaly) {
            return false;
        }
    }
    return true;
}

bool same_coarse_water(
    const worldsim::MultiresolutionWaterState& a,
    const worldsim::MultiresolutionWaterState& b) {
    const auto& ac = a.coarse_state();
    const auto& bc = b.coarse_state();
    if (a.simulated_day() != b.simulated_day() || ac.cells().size() != bc.cells().size()) return false;
    for (std::size_t i = 0; i < ac.cells().size(); ++i) {
        const auto& x = ac.cells()[i];
        const auto& y = bc.cells()[i];
        if (x.snow_water_equivalent_mm != y.snow_water_equivalent_mm ||
            x.surface_water_mm != y.surface_water_mm ||
            x.soil_water_mm != y.soil_water_mm ||
            x.groundwater_mm != y.groundwater_mm ||
            x.last_evapotranspiration_mm != y.last_evapotranspiration_mm ||
            x.last_quick_runoff_mm != y.last_quick_runoff_mm ||
            x.last_baseflow_mm != y.last_baseflow_mm ||
            x.last_routed_discharge_m3_s != y.last_routed_discharge_m3_s) {
            return false;
        }
    }
    return true;
}

bool same_refined_water(
    const worldsim::MultiresolutionWaterState& a,
    const worldsim::MultiresolutionWaterState& b,
    worldsim::CellCoord parent) {
    if (!a.is_refined(parent) || !b.is_refined(parent)) return false;
    const auto& at = a.refined_tile(parent);
    const auto& bt = b.refined_tile(parent);
    if (at.simulated_day != bt.simulated_day || at.cells.size() != bt.cells.size()) return false;
    for (std::size_t i = 0; i < at.cells.size(); ++i) {
        const auto& x = at.cells[i];
        const auto& y = bt.cells[i];
        if (x.coord != y.coord || x.active != y.active ||
            x.snow_water_equivalent_mm != y.snow_water_equivalent_mm ||
            x.surface_water_mm != y.surface_water_mm ||
            x.soil_water_mm != y.soil_water_mm ||
            x.groundwater_mm != y.groundwater_mm ||
            x.last_evapotranspiration_mm != y.last_evapotranspiration_mm ||
            x.last_quick_runoff_mm != y.last_quick_runoff_mm ||
            x.last_baseflow_mm != y.last_baseflow_mm ||
            x.last_routed_discharge_m3_s != y.last_routed_discharge_m3_s) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    using namespace worldsim;

    try {
        WorldConfig cfg;
        cfg.seed = 1701;
        cfg.bounds = {-120'000.0, -100'000.0, 240'123.0, 200'321.0};
        cfg.sea_level_m = -10'000.0f;

        SimulationState state(cfg);
        check(state.simulated_day() == 0, "simulation starts at global day zero");
        check(state.weather().simulated_day() == state.water().simulated_day(),
              "simulation starts with aligned weather/water clocks");
        check(state.world().materialized_patch_count() == 0,
              "simulation construction does not materialize L2 history");

        CellCoord parent{};
        bool have_parent = false;
        for (const auto& cell : state.topology().cells) {
            if (!cell.ocean) {
                parent = cell.coord;
                have_parent = true;
                break;
            }
        }
        check(have_parent, "simulation fixture contains refinable land");
        if (!have_parent) return 1;

        (void)state.materialize_refined_water_tile(parent);
        check(state.water().refined_tile_count() == 1 && state.water().is_refined(parent),
              "simulation owns sparse refined water through one command boundary");
        check(state.world().materialized_patch_count() == 0,
              "water refinement does not materialize L2 history");

        for (std::int64_t day = 0; day < 3; ++day) {
            const auto report = state.advance_day();
            check(report.weather.day_before == day && report.weather.day_after == day + 1 &&
                  report.water.day_before == day && report.water.day_after == day + 1 &&
                  state.simulated_day() == day + 1,
                  "simulation advances weather and water by one exact global day");
            check(std::isfinite(report.water.water_balance_error_m3),
                  "simulation daily water balance remains finite");
        }
        check(state.world().materialized_patch_count() == 0,
              "daily simulation stepping remains allocation-free for L2 history");

        check(state.disturb_surface({0.0, 0.0}, {64.0, 64.0}, 0.6f) == 1,
              "simulation routes persistent surface mutation through its world owner");
        check(state.world().materialized_patch_count() == 1,
              "simulation persistent mutation materializes one L2 patch");

        const auto temp = std::filesystem::temp_directory_path();
        const auto checkpoint = temp / "worldsim_simulation_checkpoint.bin";
        const auto canonical = temp / "worldsim_simulation_checkpoint_canonical.bin";
        const auto corrupt = temp / "worldsim_simulation_checkpoint_corrupt.bin";
        const auto truncated = temp / "worldsim_simulation_checkpoint_truncated.bin";
        const auto bad_day = temp / "worldsim_simulation_checkpoint_bad_day.bin";

        state.save_checkpoint(checkpoint);
        const auto original_bytes = read_bytes(checkpoint);
        check(!original_bytes.empty(), "simulation checkpoint writes a non-empty compound file");

        auto loaded = SimulationState::load_checkpoint(checkpoint);
        check(loaded.simulated_day() == state.simulated_day(),
              "compound checkpoint restores one global day");
        check(loaded.world().materialized_patch_count() == state.world().materialized_patch_count(),
              "compound checkpoint restores persistent L2 history");
        check(loaded.water().refined_tile_count() == 1 && loaded.water().is_refined(parent),
              "compound checkpoint restores refined water ownership");
        check(same_weather(state.weather(), loaded.weather()),
              "compound checkpoint restores exact weather state");
        check(same_coarse_water(state.water(), loaded.water()) &&
              same_refined_water(state.water(), loaded.water(), parent),
              "compound checkpoint restores exact coarse/refined water state");

        loaded.save_checkpoint(canonical);
        check(read_bytes(canonical) == original_bytes,
              "identical simulation state serializes byte-for-byte canonically");

        const auto future_a = state.advance_day();
        const auto future_b = loaded.advance_day();
        check(future_a.weather.precipitation_m3 == future_b.weather.precipitation_m3 &&
              future_a.weather.mean_air_temperature_c == future_b.weather.mean_air_temperature_c &&
              future_a.water.precipitation_m3 == future_b.water.precipitation_m3 &&
              future_a.water.evapotranspiration_m3 == future_b.water.evapotranspiration_m3 &&
              future_a.water.terminal_outflow_m3 == future_b.water.terminal_outflow_m3 &&
              future_a.water.storage_after_m3 == future_b.water.storage_after_m3 &&
              same_weather(state.weather(), loaded.weather()) &&
              same_coarse_water(state.water(), loaded.water()) &&
              same_refined_water(state.water(), loaded.water(), parent),
              "checkpoint reload has exact deterministic future evolution");

        loaded.save_checkpoint(checkpoint);
        auto replaced = SimulationState::load_checkpoint(checkpoint);
        check(replaced.simulated_day() == loaded.simulated_day(),
              "checkpoint publication atomically replaces an existing generation");

        auto corrupt_bytes = original_bytes;
        corrupt_bytes.back() ^= 0x5a;
        write_bytes(corrupt, corrupt_bytes);
        bool corrupt_threw = false;
        try { (void)SimulationState::load_checkpoint(corrupt); }
        catch (const std::runtime_error&) { corrupt_threw = true; }
        check(corrupt_threw, "compound checkpoint rejects section checksum corruption");

        auto truncated_bytes = original_bytes;
        truncated_bytes.resize(truncated_bytes.size() / 2);
        write_bytes(truncated, truncated_bytes);
        bool truncated_threw = false;
        try { (void)SimulationState::load_checkpoint(truncated); }
        catch (const std::runtime_error&) { truncated_threw = true; }
        check(truncated_threw, "compound checkpoint rejects truncation before exposing state");

        auto bad_day_bytes = original_bytes;
        constexpr std::size_t day_offset = 8 + sizeof(std::uint32_t);
        check(bad_day_bytes.size() >= day_offset + sizeof(std::int64_t),
              "checkpoint fixture contains global day header");
        if (bad_day_bytes.size() >= day_offset + sizeof(std::int64_t)) {
            const std::int64_t mismatched_day = state.simulated_day() + 7;
            std::memcpy(bad_day_bytes.data() + day_offset, &mismatched_day, sizeof(mismatched_day));
            write_bytes(bad_day, bad_day_bytes);
            bool day_threw = false;
            try { (void)SimulationState::load_checkpoint(bad_day); }
            catch (const std::runtime_error&) { day_threw = true; }
            check(day_threw, "compound checkpoint rejects a global/component clock mismatch");
        }

        std::filesystem::remove(checkpoint);
        std::filesystem::remove(canonical);
        std::filesystem::remove(corrupt);
        std::filesystem::remove(truncated);
        std::filesystem::remove(bad_day);
    } catch (const std::exception& e) {
        ++failures;
        std::cerr << "UNCAUGHT simulation test exception: " << e.what() << '\n';
    }

    if (failures != 0) {
        std::cerr << failures << " simulation test(s) failed\n";
        return 1;
    }
    std::cout << "All simulation checkpoint tests passed\n";
    return 0;
}
