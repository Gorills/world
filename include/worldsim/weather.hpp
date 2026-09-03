#pragma once

#include "worldsim/continental_water.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace worldsim {

// Synthetic weather-process parameters. Climate remains the long-run baseline; these values
// control only transient daily anomalies and storm intermittency around that baseline.
struct WeatherParameters {
    float temperature_memory{0.86f};
    float moisture_memory{0.78f};
    float temperature_variability_c{5.0f};
    float moisture_variability{0.9f};
    float storm_threshold{-0.15f};
    float storm_intensity{4.64f};

    void validate() const;
};

// Compact authoritative transient state for one L0 climate cell. Actual daily temperature,
// precipitation and PET are deterministic diagnostics derived from this state + climate metadata.
struct WeatherCellState {
    float temperature_anomaly_c{};
    float moisture_anomaly{};
};

struct WeatherCellSample {
    CellCoord coord{};
    float temperature_anomaly_c{};
    float moisture_anomaly{};
    float precipitation_mm{};
    float mean_air_temperature_c{};
    float potential_evapotranspiration_mm{};
};

struct WeatherStepReport {
    std::int64_t day_before{};
    std::int64_t day_after{};
    double precipitation_m3{};
    double mean_air_temperature_c{};
    double mean_potential_evapotranspiration_mm{};
    double wet_area_fraction{};
};

class World;
class MultiresolutionWaterState;

class WeatherState {
public:
    [[nodiscard]] const WorldConfig& config() const noexcept { return config_; }
    [[nodiscard]] const WeatherParameters& parameters() const noexcept { return parameters_; }
    [[nodiscard]] CellCoord min_coord() const noexcept { return min_coord_; }
    [[nodiscard]] std::uint32_t width_cells() const noexcept { return width_cells_; }
    [[nodiscard]] std::uint32_t height_cells() const noexcept { return height_cells_; }
    [[nodiscard]] std::int64_t simulated_day() const noexcept { return simulated_day_; }
    [[nodiscard]] const std::vector<WeatherCellState>& cells() const noexcept { return cells_; }
    [[nodiscard]] std::size_t index_of(CellCoord coord) const;
    [[nodiscard]] CellCoord coord_of(std::size_t index) const;
    [[nodiscard]] const WeatherCellState& cell(CellCoord coord) const;

private:
    struct CellMetadata {
        double area_m2{};
        float mean_temperature_at_elevation_c{};
        float annual_precipitation_mm{};
        float continentality{};
    };

    WorldConfig config_{};
    WeatherParameters parameters_{};
    CellCoord min_coord_{};
    std::uint32_t width_cells_{};
    std::uint32_t height_cells_{};
    std::int64_t simulated_day_{};
    std::vector<WeatherCellState> cells_;
    std::vector<CellMetadata> metadata_;

    friend WeatherState make_weather_state(const World&, const WeatherParameters&);
    friend std::vector<ContinentalWaterForcing> make_weather_daily_forcing(const WeatherState&);
    friend WeatherCellSample sample_weather(const WeatherState&, CellCoord);
    friend WeatherStepReport advance_weather_day(WeatherState&);
    friend struct WeatherPendingDay;
    friend void save_weather_state(const WeatherState&, const std::filesystem::path&);
    friend WeatherState load_weather_state(const World&, const std::filesystem::path&);
};

// Whole-world L0 weather state. Creation is O(number of L0 cells), does not materialize L1/L2,
// and caches only climate/elevation metadata needed for repeated daily forcing generation.
[[nodiscard]] WeatherState make_weather_state(
    const World& world,
    const WeatherParameters& parameters = {});

// Returns forcing for WeatherState::simulated_day() without changing state or clock.
[[nodiscard]] std::vector<ContinentalWaterForcing> make_weather_daily_forcing(
    const WeatherState& state);

[[nodiscard]] WeatherCellSample sample_weather(
    const WeatherState& state,
    CellCoord climate_coord);

// Advances the transient atmosphere by exactly one day. The report describes the forcing for
// day_before (the day being consumed), while the resulting state represents day_after.
[[nodiscard]] WeatherStepReport advance_weather_day(WeatherState& state);

struct WeatherWaterStepReport {
    WeatherStepReport weather;
    ContinentalWaterStepReport water;
};

// Coupled helpers keep atmospheric and hydrologic clocks exact. Weather's next state is prepared
// before the water step and committed only after water succeeds, so rejected hydrology input does
// not leave the two systems on different days.
//
// ContinentalWaterState predates parameter ownership in the C++ state object. Its coupled helper
// therefore requires the caller to pass explicitly the same hydrology parameters used at state
// construction. MultiresolutionWaterState owns its parameter set and has no such external argument.
[[nodiscard]] WeatherWaterStepReport advance_weather_continental_water_day(
    WeatherState& weather,
    ContinentalWaterState& water,
    const DynamicHydrologyParameters& water_parameters);

[[nodiscard]] WeatherWaterStepReport advance_weather_multiresolution_water_day(
    const World& world,
    WeatherState& weather,
    MultiresolutionWaterState& water);

void save_weather_state(
    const WeatherState& state,
    const std::filesystem::path& path);

[[nodiscard]] WeatherState load_weather_state(
    const World& world,
    const std::filesystem::path& path);

} // namespace worldsim
