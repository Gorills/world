#include "worldsim/weather.hpp"

#include "worldsim/world.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace worldsim {
namespace {

constexpr std::array<char, 8> kMagic{'W','S','W','E','0','0','0','1'};
constexpr std::uint32_t kFormatVersion = 1;

bool same_bounds(const WorldBounds& a, const WorldBounds& b) {
    return a.origin_x_m == b.origin_x_m && a.origin_y_m == b.origin_y_m &&
           a.width_m == b.width_m && a.height_m == b.height_m;
}

bool same_config_identity(const WorldConfig& a, const WorldConfig& b) {
    return a.seed == b.seed && same_bounds(a.bounds, b.bounds) &&
           a.local_cell_m == b.local_cell_m && a.regional_cell_m == b.regional_cell_m &&
           a.climate_cell_m == b.climate_cell_m && a.sea_level_m == b.sea_level_m;
}

template <typename T>
void write_pod(std::ostream& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    if (!out) throw std::runtime_error("failed to write weather file");
}

template <typename T>
void read_pod(std::istream& in, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    if (!in) throw std::runtime_error("failed to read weather file");
}

void write_config(std::ostream& out, const WorldConfig& cfg) {
    write_pod(out, cfg.seed);
    write_pod(out, cfg.bounds.origin_x_m);
    write_pod(out, cfg.bounds.origin_y_m);
    write_pod(out, cfg.bounds.width_m);
    write_pod(out, cfg.bounds.height_m);
    write_pod(out, cfg.local_cell_m);
    write_pod(out, cfg.regional_cell_m);
    write_pod(out, cfg.climate_cell_m);
    write_pod(out, cfg.sea_level_m);
}

WorldConfig read_config(std::istream& in) {
    WorldConfig cfg;
    read_pod(in, cfg.seed);
    read_pod(in, cfg.bounds.origin_x_m);
    read_pod(in, cfg.bounds.origin_y_m);
    read_pod(in, cfg.bounds.width_m);
    read_pod(in, cfg.bounds.height_m);
    read_pod(in, cfg.local_cell_m);
    read_pod(in, cfg.regional_cell_m);
    read_pod(in, cfg.climate_cell_m);
    read_pod(in, cfg.sea_level_m);
    cfg.validate();
    return cfg;
}

void write_parameters(std::ostream& out, const WeatherParameters& p) {
    write_pod(out, p.temperature_memory);
    write_pod(out, p.moisture_memory);
    write_pod(out, p.temperature_variability_c);
    write_pod(out, p.moisture_variability);
    write_pod(out, p.storm_threshold);
    write_pod(out, p.storm_intensity);
}

WeatherParameters read_parameters(std::istream& in) {
    WeatherParameters p;
    read_pod(in, p.temperature_memory);
    read_pod(in, p.moisture_memory);
    read_pod(in, p.temperature_variability_c);
    read_pod(in, p.moisture_variability);
    read_pod(in, p.storm_threshold);
    read_pod(in, p.storm_intensity);
    p.validate();
    return p;
}

void validate_cell(const WeatherCellState& cell, const WeatherParameters& p) {
    const double temp_limit = static_cast<double>(p.temperature_variability_c) * 4.0 + 1e-5;
    const double moisture_limit = static_cast<double>(p.moisture_variability) * 4.0 + 1e-5;
    if (!std::isfinite(cell.temperature_anomaly_c) || !std::isfinite(cell.moisture_anomaly) ||
        std::abs(static_cast<double>(cell.temperature_anomaly_c)) > temp_limit ||
        std::abs(static_cast<double>(cell.moisture_anomaly)) > moisture_limit) {
        throw std::runtime_error("weather file contains invalid anomaly state");
    }
}

} // namespace

void save_weather_state(const WeatherState& state, const std::filesystem::path& path) {
    state.parameters_.validate();
    if (state.simulated_day_ < 0) throw std::runtime_error("cannot save a negative weather day");
    const auto expected = static_cast<std::uint64_t>(state.width_cells_) * state.height_cells_;
    if (state.width_cells_ == 0 || state.height_cells_ == 0 || expected != state.cells_.size() ||
        state.metadata_.size() != state.cells_.size()) {
        throw std::runtime_error("weather state dimensions are internally inconsistent");
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open weather file for writing: " + path.string());
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    if (!out) throw std::runtime_error("failed to write weather file magic");
    write_pod(out, kFormatVersion);
    write_config(out, state.config_);
    write_parameters(out, state.parameters_);
    write_pod(out, state.simulated_day_);
    write_pod(out, state.min_coord_.x);
    write_pod(out, state.min_coord_.y);
    write_pod(out, state.width_cells_);
    write_pod(out, state.height_cells_);
    const auto count = static_cast<std::uint64_t>(state.cells_.size());
    write_pod(out, count);
    for (const auto& cell : state.cells_) {
        validate_cell(cell, state.parameters_);
        write_pod(out, cell.temperature_anomaly_c);
        write_pod(out, cell.moisture_anomaly);
    }
}

WeatherState load_weather_state(const World& world, const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open weather file for reading: " + path.string());

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) throw std::runtime_error("invalid weather file magic");
    std::uint32_t version{};
    read_pod(in, version);
    if (version != kFormatVersion) throw std::runtime_error("unsupported weather file version");

    const auto saved_config = read_config(in);
    if (!same_config_identity(saved_config, world.config())) {
        throw std::runtime_error("weather file belongs to a different world");
    }
    const auto parameters = read_parameters(in);
    auto state = make_weather_state(world, parameters);

    std::int64_t day{};
    CellCoord min_coord{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t count{};
    read_pod(in, day);
    read_pod(in, min_coord.x);
    read_pod(in, min_coord.y);
    read_pod(in, width);
    read_pod(in, height);
    read_pod(in, count);
    if (day < 0 || min_coord != state.min_coord_ || width != state.width_cells_ ||
        height != state.height_cells_ || count != state.cells_.size()) {
        throw std::runtime_error("weather file raster metadata is inconsistent");
    }

    state.simulated_day_ = day;
    for (auto& cell : state.cells_) {
        read_pod(in, cell.temperature_anomaly_c);
        read_pod(in, cell.moisture_anomaly);
        validate_cell(cell, parameters);
    }

    char trailing{};
    if (in.read(&trailing, 1)) throw std::runtime_error("weather file contains unexpected trailing data");
    if (!in.eof()) throw std::runtime_error("failed while validating weather file end");
    return state;
}

} // namespace worldsim
