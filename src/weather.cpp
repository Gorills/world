#include "worldsim/weather.hpp"

#include "worldsim/coordinates.hpp"
#include "worldsim/multiresolution_water.hpp"
#include "worldsim/world.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace worldsim {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::int64_t kSynopticScaleCells = 4;
constexpr double kSpatialMemoryBlend = 0.25;
constexpr std::uint64_t kTemperatureSalt = 0x5745415448455254ULL;
constexpr std::uint64_t kMoistureSalt = 0x4d4f495354555245ULL;
constexpr std::uint64_t kStormSalt = 0x53544f524d464945ULL;

bool same_bounds(const WorldBounds& a, const WorldBounds& b) {
    return a.origin_x_m == b.origin_x_m && a.origin_y_m == b.origin_y_m &&
           a.width_m == b.width_m && a.height_m == b.height_m;
}

bool same_config_identity(const WorldConfig& a, const WorldConfig& b) {
    return a.seed == b.seed && same_bounds(a.bounds, b.bounds) &&
           a.local_cell_m == b.local_cell_m && a.regional_cell_m == b.regional_cell_m &&
           a.climate_cell_m == b.climate_cell_m && a.sea_level_m == b.sea_level_m;
}

struct GridRange {
    CellCoord min{};
    std::uint32_t width{};
    std::uint32_t height{};
};

GridRange climate_range(const WorldConfig& cfg) {
    const auto& b = cfg.bounds;
    const auto min = world_to_cell({b.origin_x_m, b.origin_y_m}, cfg.climate_cell_m);
    const WorldPosition last{
        std::nextafter(b.origin_x_m + b.width_m, -std::numeric_limits<double>::infinity()),
        std::nextafter(b.origin_y_m + b.height_m, -std::numeric_limits<double>::infinity())};
    const auto max = world_to_cell(last, cfg.climate_cell_m);
    const auto width64 = max.x - min.x + 1;
    const auto height64 = max.y - min.y + 1;
    if (width64 <= 0 || height64 <= 0 ||
        width64 > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) ||
        height64 > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("world weather raster dimensions are not representable");
    }
    const auto width = static_cast<std::uint32_t>(width64);
    const auto height = static_cast<std::uint32_t>(height64);
    const auto count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (count > kMaxContinentalHydrologyCells) {
        throw std::invalid_argument("world exceeds the L0 weather cell limit");
    }
    return {min, width, height};
}

double overlap_area_m2(CellCoord coord, std::int32_t cell_m, const WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) return 0.0;
    return (x1 - x0) * (y1 - y0);
}

WorldPosition overlap_center(CellCoord coord, std::int32_t cell_m, const WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) {
        throw std::out_of_range("weather cell does not intersect world bounds");
    }
    return {x0 + (x1 - x0) * 0.5, y0 + (y1 - y0) * 0.5};
}

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}

std::uint64_t weather_hash(Seed seed, std::int64_t x, std::int64_t y,
                           std::int64_t day, std::uint64_t salt) {
    const auto ux = std::bit_cast<std::uint64_t>(x);
    const auto uy = std::bit_cast<std::uint64_t>(y);
    const auto ud = std::bit_cast<std::uint64_t>(day);
    return mix64(seed ^ mix64(ux + salt) ^ std::rotl(mix64(uy ^ salt), 23) ^
                 std::rotl(mix64(ud + salt), 41));
}

double hash_unit(Seed seed, std::int64_t x, std::int64_t y,
                 std::int64_t day, std::uint64_t salt) {
    const auto h = weather_hash(seed, x, y, day, salt);
    return static_cast<double>(h >> 11U) * (1.0 / 9007199254740992.0);
}

std::pair<std::int64_t, std::int64_t> floor_div_rem(std::int64_t value, std::int64_t divisor) {
    auto q = value / divisor;
    auto r = value % divisor;
    if (r < 0) {
        --q;
        r += divisor;
    }
    return {q, r};
}

double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// One innovation field is bilinearly interpolated on a 4x4-L0 synoptic lattice (~32 km at
// the fixed hierarchy). The day participates in the lattice hash, while AR memory below gives
// temporal persistence. This avoids cell-wise white-noise forcing without storing a fine raster.
double synoptic_noise(Seed seed, CellCoord coord, std::int64_t day, std::uint64_t salt) {
    const auto [x0, rx] = floor_div_rem(coord.x, kSynopticScaleCells);
    const auto [y0, ry] = floor_div_rem(coord.y, kSynopticScaleCells);
    const auto x1 = x0 + 1;
    const auto y1 = y0 + 1;
    const double tx = smoothstep(static_cast<double>(rx) / static_cast<double>(kSynopticScaleCells));
    const double ty = smoothstep(static_cast<double>(ry) / static_cast<double>(kSynopticScaleCells));
    const double a = hash_unit(seed, x0, y0, day, salt);
    const double b = hash_unit(seed, x1, y0, day, salt);
    const double c = hash_unit(seed, x0, y1, day, salt);
    const double d = hash_unit(seed, x1, y1, day, salt);
    return (lerp(lerp(a, b, tx), lerp(c, d, tx), ty) * 2.0) - 1.0;
}

ContinentalWaterForcing forcing_for(
    const WorldConfig& config,
    const WeatherParameters& parameters,
    CellCoord coord,
    std::int64_t day,
    float mean_temperature_at_elevation_c,
    float annual_precipitation_mm,
    float continentality,
    const WeatherCellState& state) {
    const double day_of_year = std::fmod(static_cast<double>(day), 365.2425);
    const double phase = 2.0 * kPi * ((day_of_year - 200.0) / 365.2425);
    const double amplitude = 8.0 + 8.0 * static_cast<double>(continentality);
    const double temperature = static_cast<double>(mean_temperature_at_elevation_c) +
                               amplitude * std::cos(phase) + state.temperature_anomaly_c;

    const double precip_weight = std::max(0.15, 1.0 + 0.30 * std::cos(phase - 0.7));
    const double climatological_daily = static_cast<double>(annual_precipitation_mm) /
                                         365.2425 * precip_weight;
    const double normalized_moisture = parameters.moisture_variability > 0.0f
        ? static_cast<double>(state.moisture_anomaly) / parameters.moisture_variability
        : 0.0;
    const double instant_storm = synoptic_noise(config.seed, coord, day, kStormSalt);
    const double storm_signal = std::clamp(0.75 * normalized_moisture + 0.25 * instant_storm,
                                           -1.0, 1.0);
    const double storm_excess = std::max(0.0, storm_signal - parameters.storm_threshold);
    const double precipitation = climatological_daily * parameters.storm_intensity * storm_excess;
    const double pet = std::max(0.0, 0.10 * (temperature + 5.0));

    if (!std::isfinite(temperature) || !std::isfinite(precipitation) || precipitation < 0.0 ||
        !std::isfinite(pet) || pet < 0.0 ||
        precipitation > static_cast<double>(std::numeric_limits<float>::max()) ||
        std::abs(temperature) > static_cast<double>(std::numeric_limits<float>::max()) ||
        pet > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::overflow_error("weather forcing exceeds finite float range");
    }
    return {static_cast<float>(precipitation), static_cast<float>(temperature), static_cast<float>(pet)};
}

void validate_state_shape(const WeatherState& state) {
    state.parameters().validate();
    if (state.simulated_day() < 0) {
        throw std::invalid_argument("weather simulated day must be non-negative");
    }
    const auto expected = static_cast<std::uint64_t>(state.width_cells()) * state.height_cells();
    if (state.width_cells() == 0 || state.height_cells() == 0 || expected != state.cells().size()) {
        throw std::invalid_argument("weather state dimensions do not match cell storage");
    }
    for (const auto& cell : state.cells()) {
        if (!std::isfinite(cell.temperature_anomaly_c) || !std::isfinite(cell.moisture_anomaly)) {
            throw std::invalid_argument("weather state contains non-finite anomalies");
        }
    }
}

} // namespace

void WeatherParameters::validate() const {
    if (!std::isfinite(temperature_memory) || temperature_memory < 0.0f || temperature_memory >= 1.0f ||
        !std::isfinite(moisture_memory) || moisture_memory < 0.0f || moisture_memory >= 1.0f ||
        !std::isfinite(temperature_variability_c) || temperature_variability_c < 0.0f ||
        temperature_variability_c > 50.0f ||
        !std::isfinite(moisture_variability) || moisture_variability < 0.0f || moisture_variability > 5.0f ||
        !std::isfinite(storm_threshold) || storm_threshold <= -1.0f || storm_threshold >= 1.0f ||
        !std::isfinite(storm_intensity) || storm_intensity <= 0.0f || storm_intensity > 20.0f) {
        throw std::invalid_argument("weather parameters are outside supported finite ranges");
    }
}

std::size_t WeatherState::index_of(CellCoord coord) const {
    if (coord.x < min_coord_.x || coord.y < min_coord_.y) {
        throw std::out_of_range("weather coordinate is outside state");
    }
    const auto dx = static_cast<std::uint64_t>(coord.x) - static_cast<std::uint64_t>(min_coord_.x);
    const auto dy = static_cast<std::uint64_t>(coord.y) - static_cast<std::uint64_t>(min_coord_.y);
    if (dx >= static_cast<std::uint64_t>(width_cells_) ||
        dy >= static_cast<std::uint64_t>(height_cells_)) {
        throw std::out_of_range("weather coordinate is outside state");
    }
    return static_cast<std::size_t>(dy) * width_cells_ + static_cast<std::size_t>(dx);
}

CellCoord WeatherState::coord_of(std::size_t index) const {
    if (index >= cells_.size()) throw std::out_of_range("weather index is outside state");
    const auto width = static_cast<std::size_t>(width_cells_);
    return {min_coord_.x + static_cast<std::int64_t>(index % width),
            min_coord_.y + static_cast<std::int64_t>(index / width)};
}

const WeatherCellState& WeatherState::cell(CellCoord coord) const {
    return cells_.at(index_of(coord));
}

WeatherState make_weather_state(const World& world, const WeatherParameters& parameters) {
    parameters.validate();
    const auto range = climate_range(world.config());
    const auto count = static_cast<std::size_t>(range.width) * range.height;

    WeatherState state;
    state.config_ = world.config();
    state.parameters_ = parameters;
    state.min_coord_ = range.min;
    state.width_cells_ = range.width;
    state.height_cells_ = range.height;
    state.cells_.resize(count);
    state.metadata_.resize(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto coord = state.coord_of(i);
        const auto climate = world.sample_climate(coord);
        const auto center = overlap_center(coord, world.config().climate_cell_m, world.config().bounds);
        const auto elevation = world.sample_elevation(center);
        auto& meta = state.metadata_[i];
        meta.area_m2 = overlap_area_m2(coord, world.config().climate_cell_m, world.config().bounds);
        if (!(meta.area_m2 > 0.0)) throw std::logic_error("weather L0 cell has zero world overlap area");
        meta.mean_temperature_at_elevation_c = static_cast<float>(
            static_cast<double>(climate.mean_temperature_c) -
            0.0065 * std::max(0.0f, elevation));
        meta.annual_precipitation_mm = climate.annual_precipitation_mm;
        meta.continentality = climate.continentality;

        auto& cell = state.cells_[i];
        cell.temperature_anomaly_c = static_cast<float>(
            static_cast<double>(parameters.temperature_variability_c) *
            synoptic_noise(world.config().seed, coord, 0, kTemperatureSalt));
        cell.moisture_anomaly = static_cast<float>(
            static_cast<double>(parameters.moisture_variability) *
            synoptic_noise(world.config().seed, coord, 0, kMoistureSalt));
    }
    return state;
}

std::vector<ContinentalWaterForcing> make_weather_daily_forcing(const WeatherState& state) {
    validate_state_shape(state);
    if (state.metadata_.size() != state.cells_.size()) {
        throw std::invalid_argument("weather metadata dimensions do not match state");
    }
    std::vector<ContinentalWaterForcing> forcing;
    forcing.reserve(state.cells_.size());
    for (std::size_t i = 0; i < state.cells_.size(); ++i) {
        const auto& meta = state.metadata_[i];
        forcing.push_back(forcing_for(state.config_, state.parameters_, state.coord_of(i),
                                      state.simulated_day_, meta.mean_temperature_at_elevation_c,
                                      meta.annual_precipitation_mm, meta.continentality,
                                      state.cells_[i]));
    }
    return forcing;
}

WeatherCellSample sample_weather(const WeatherState& state, CellCoord climate_coord) {
    validate_state_shape(state);
    const auto i = state.index_of(climate_coord);
    if (state.metadata_.size() != state.cells_.size()) {
        throw std::invalid_argument("weather metadata dimensions do not match state");
    }
    const auto& meta = state.metadata_[i];
    const auto forcing = forcing_for(state.config_, state.parameters_, climate_coord,
                                     state.simulated_day_, meta.mean_temperature_at_elevation_c,
                                     meta.annual_precipitation_mm, meta.continentality,
                                     state.cells_[i]);
    WeatherCellSample out;
    out.coord = climate_coord;
    out.temperature_anomaly_c = state.cells_[i].temperature_anomaly_c;
    out.moisture_anomaly = state.cells_[i].moisture_anomaly;
    out.precipitation_mm = forcing.precipitation_mm;
    out.mean_air_temperature_c = forcing.mean_air_temperature_c;
    out.potential_evapotranspiration_mm = forcing.potential_evapotranspiration_mm;
    return out;
}

struct WeatherPendingDay {
    std::vector<ContinentalWaterForcing> forcing;
    std::vector<WeatherCellState> next_cells;
    WeatherStepReport report;

    static WeatherPendingDay prepare(const WeatherState& state) {
        validate_state_shape(state);
        if (state.simulated_day_ == std::numeric_limits<std::int64_t>::max()) {
            throw std::overflow_error("weather simulation day overflow");
        }
        if (state.metadata_.size() != state.cells_.size()) {
            throw std::invalid_argument("weather metadata dimensions do not match state");
        }

        WeatherPendingDay pending;
        pending.forcing.reserve(state.cells_.size());
        pending.next_cells.resize(state.cells_.size());
        pending.report.day_before = state.simulated_day_;
        pending.report.day_after = state.simulated_day_ + 1;

        double total_area = 0.0;
        double temperature_area_sum = 0.0;
        double pet_area_sum = 0.0;
        double wet_area = 0.0;
        for (std::size_t i = 0; i < state.cells_.size(); ++i) {
            const auto coord = state.coord_of(i);
            const auto& meta = state.metadata_[i];
            const auto forcing_value = forcing_for(
                state.config_, state.parameters_, coord, state.simulated_day_,
                meta.mean_temperature_at_elevation_c, meta.annual_precipitation_mm,
                meta.continentality, state.cells_[i]);
            pending.forcing.push_back(forcing_value);
            total_area += meta.area_m2;
            temperature_area_sum += static_cast<double>(forcing_value.mean_air_temperature_c) * meta.area_m2;
            pet_area_sum += static_cast<double>(forcing_value.potential_evapotranspiration_mm) * meta.area_m2;
            pending.report.precipitation_m3 +=
                static_cast<double>(forcing_value.precipitation_mm) * 0.001 * meta.area_m2;
            if (forcing_value.precipitation_mm > 0.0f) wet_area += meta.area_m2;
        }
        if (!(total_area > 0.0) || !std::isfinite(pending.report.precipitation_m3)) {
            throw std::logic_error("weather area/precipitation total is invalid");
        }
        pending.report.mean_air_temperature_c = temperature_area_sum / total_area;
        pending.report.mean_potential_evapotranspiration_mm = pet_area_sum / total_area;
        pending.report.wet_area_fraction = wet_area / total_area;

        const auto width = static_cast<std::size_t>(state.width_cells_);
        const auto height = static_cast<std::size_t>(state.height_cells_);
        const auto neighbor_mean = [&](std::size_t index, bool temperature) {
            const auto x = index % width;
            const auto y = index / width;
            double sum = 0.0;
            std::size_t count = 0;
            const auto add = [&](std::size_t nx, std::size_t ny) {
                const auto& c = state.cells_[ny * width + nx];
                sum += temperature ? static_cast<double>(c.temperature_anomaly_c)
                                   : static_cast<double>(c.moisture_anomaly);
                ++count;
            };
            if (x > 0) add(x - 1, y);
            if (x + 1 < width) add(x + 1, y);
            if (y > 0) add(x, y - 1);
            if (y + 1 < height) add(x, y + 1);
            if (count == 0) {
                return temperature ? static_cast<double>(state.cells_[index].temperature_anomaly_c)
                                   : static_cast<double>(state.cells_[index].moisture_anomaly);
            }
            return sum / static_cast<double>(count);
        };

        const double temp_memory = state.parameters_.temperature_memory;
        const double moisture_memory = state.parameters_.moisture_memory;
        const double temp_innovation_scale = state.parameters_.temperature_variability_c *
            std::sqrt(std::max(0.0, 1.0 - temp_memory * temp_memory));
        const double moisture_innovation_scale = state.parameters_.moisture_variability *
            std::sqrt(std::max(0.0, 1.0 - moisture_memory * moisture_memory));
        const double temp_limit = static_cast<double>(state.parameters_.temperature_variability_c) * 4.0;
        const double moisture_limit = static_cast<double>(state.parameters_.moisture_variability) * 4.0;

        for (std::size_t i = 0; i < state.cells_.size(); ++i) {
            const auto coord = state.coord_of(i);
            const auto& previous = state.cells_[i];
            const double temp_spatial = (1.0 - kSpatialMemoryBlend) * previous.temperature_anomaly_c +
                                        kSpatialMemoryBlend * neighbor_mean(i, true);
            const double moisture_spatial = (1.0 - kSpatialMemoryBlend) * previous.moisture_anomaly +
                                            kSpatialMemoryBlend * neighbor_mean(i, false);
            double next_temp = temp_memory * temp_spatial + temp_innovation_scale *
                synoptic_noise(state.config_.seed, coord, pending.report.day_after, kTemperatureSalt);
            double next_moisture = moisture_memory * moisture_spatial + moisture_innovation_scale *
                synoptic_noise(state.config_.seed, coord, pending.report.day_after, kMoistureSalt);
            next_temp = std::clamp(next_temp, -temp_limit, temp_limit);
            next_moisture = std::clamp(next_moisture, -moisture_limit, moisture_limit);
            if (!std::isfinite(next_temp) || !std::isfinite(next_moisture)) {
                throw std::overflow_error("weather anomaly update is not finite");
            }
            pending.next_cells[i].temperature_anomaly_c = static_cast<float>(next_temp);
            pending.next_cells[i].moisture_anomaly = static_cast<float>(next_moisture);
        }
        return pending;
    }

    static void commit(WeatherState& state, WeatherPendingDay&& pending) noexcept {
        state.cells_ = std::move(pending.next_cells);
        state.simulated_day_ = pending.report.day_after;
    }
};

WeatherStepReport advance_weather_day(WeatherState& state) {
    auto pending = WeatherPendingDay::prepare(state);
    const auto report = pending.report;
    WeatherPendingDay::commit(state, std::move(pending));
    return report;
}

WeatherWaterStepReport advance_weather_continental_water_day(
    WeatherState& weather,
    ContinentalWaterState& water,
    const DynamicHydrologyParameters& water_parameters) {
    if (!same_config_identity(weather.config(), water.config()) ||
        weather.min_coord() != water.min_coord() ||
        weather.width_cells() != water.width_cells() ||
        weather.height_cells() != water.height_cells()) {
        throw std::invalid_argument("weather and continental water grids belong to different worlds");
    }
    if (weather.simulated_day() != water.simulated_day()) {
        throw std::invalid_argument("weather and continental water clocks must match exactly");
    }

    auto pending = WeatherPendingDay::prepare(weather);
    const auto water_report = advance_continental_water_day(water, pending.forcing, water_parameters);
    if (water_report.day_after != pending.report.day_after) {
        throw std::logic_error("weather/water coupled step produced inconsistent next day");
    }
    const auto weather_report = pending.report;
    WeatherPendingDay::commit(weather, std::move(pending));
    return {weather_report, water_report};
}

WeatherWaterStepReport advance_weather_multiresolution_water_day(
    const World& world,
    WeatherState& weather,
    MultiresolutionWaterState& water) {
    const auto& coarse = water.coarse_state();
    if (!same_config_identity(world.config(), weather.config()) ||
        !same_config_identity(weather.config(), water.config()) ||
        weather.min_coord() != coarse.min_coord() ||
        weather.width_cells() != coarse.width_cells() ||
        weather.height_cells() != coarse.height_cells()) {
        throw std::invalid_argument("world/weather/multiresolution water grids are not aligned");
    }
    if (weather.simulated_day() != water.simulated_day()) {
        throw std::invalid_argument("weather and multiresolution water clocks must match exactly");
    }

    auto pending = WeatherPendingDay::prepare(weather);
    const auto water_report = advance_multiresolution_water_day(world, water, pending.forcing);
    if (water_report.day_after != pending.report.day_after) {
        throw std::logic_error("weather/multiresolution water step produced inconsistent next day");
    }
    const auto weather_report = pending.report;
    WeatherPendingDay::commit(weather, std::move(pending));
    return {weather_report, water_report};
}

} // namespace worldsim
