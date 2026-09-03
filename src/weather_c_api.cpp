#include "worldsim/weather_c_api.h"

#include "worldsim/multiresolution_water.hpp"
#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// Opaque definitions intentionally match the owning C ABI translation units.
struct ws_world {
    explicit ws_world(worldsim::WorldConfig cfg) : impl(std::move(cfg)) {}
    explicit ws_world(worldsim::World world) : impl(std::move(world)) {}
    worldsim::World impl;
};

struct ws_multiresolution_water_state {
    explicit ws_multiresolution_water_state(worldsim::MultiresolutionWaterState value)
        : impl(std::move(value)) {}
    worldsim::MultiresolutionWaterState impl;
};

struct ws_weather_state {
    explicit ws_weather_state(worldsim::WeatherState value) : impl(std::move(value)) {}
    worldsim::WeatherState impl;
};

namespace {
thread_local std::string g_weather_last_error;

template <typename F>
int guarded(F&& fn) {
    try {
        fn();
        g_weather_last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        g_weather_last_error = e.what();
        return -1;
    } catch (...) {
        g_weather_last_error = "unknown WorldSim weather error";
        return -1;
    }
}

worldsim::WeatherParameters weather_parameters_from_c(const ws_weather_parameters* p) {
    if (!p) return {};
    worldsim::WeatherParameters out;
    out.temperature_memory = p->temperature_memory;
    out.moisture_memory = p->moisture_memory;
    out.temperature_variability_c = p->temperature_variability_c;
    out.moisture_variability = p->moisture_variability;
    out.storm_threshold = p->storm_threshold;
    out.storm_intensity = p->storm_intensity;
    out.validate();
    return out;
}

void copy_weather_report(const worldsim::WeatherStepReport& in, ws_weather_step_report& out) {
    out.day_before = in.day_before;
    out.day_after = in.day_after;
    out.precipitation_m3 = in.precipitation_m3;
    out.mean_air_temperature_c = in.mean_air_temperature_c;
    out.mean_potential_evapotranspiration_mm = in.mean_potential_evapotranspiration_mm;
    out.wet_area_fraction = in.wet_area_fraction;
}

void copy_water_report(const worldsim::ContinentalWaterStepReport& in,
                       ws_continental_water_step_report& out) {
    out.day_before = in.day_before;
    out.day_after = in.day_after;
    out.storage_before_m3 = in.storage_before_m3;
    out.precipitation_m3 = in.precipitation_m3;
    out.evapotranspiration_m3 = in.evapotranspiration_m3;
    out.terminal_outflow_m3 = in.terminal_outflow_m3;
    out.storage_after_m3 = in.storage_after_m3;
    out.water_balance_error_m3 = in.water_balance_error_m3;
}
} // namespace

extern "C" {

ws_weather_state* ws_world_weather_create(
    ws_world* world,
    const ws_weather_parameters* parameters) {
    try {
        if (!world) throw std::invalid_argument("world is null");
        auto state = worldsim::make_weather_state(world->impl, weather_parameters_from_c(parameters));
        auto out = std::make_unique<ws_weather_state>(std::move(state));
        g_weather_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_weather_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_weather_last_error = "unknown WorldSim weather error";
        return nullptr;
    }
}

void ws_weather_state_destroy(ws_weather_state* state) {
    delete state;
}

uint64_t ws_weather_cell_count(const ws_weather_state* state) {
    return state ? static_cast<uint64_t>(state->impl.cells().size()) : 0;
}

int64_t ws_weather_simulated_day(const ws_weather_state* state) {
    return state ? state->impl.simulated_day() : 0;
}

int ws_weather_sample_cell(
    const ws_weather_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_weather_cell_sample* out_sample) {
    return guarded([&] {
        if (!state || !out_sample) throw std::invalid_argument("state/out_sample is null");
        const auto sample = worldsim::sample_weather(state->impl, {climate_x, climate_y});
        out_sample->cell_x = sample.coord.x;
        out_sample->cell_y = sample.coord.y;
        out_sample->temperature_anomaly_c = sample.temperature_anomaly_c;
        out_sample->moisture_anomaly = sample.moisture_anomaly;
        out_sample->precipitation_mm = sample.precipitation_mm;
        out_sample->mean_air_temperature_c = sample.mean_air_temperature_c;
        out_sample->potential_evapotranspiration_mm = sample.potential_evapotranspiration_mm;
    });
}

int ws_weather_copy_daily_forcing(
    const ws_weather_state* state,
    ws_continental_water_forcing* out_forcing,
    uint64_t capacity) {
    return guarded([&] {
        if (!state || !out_forcing) throw std::invalid_argument("state/out_forcing is null");
        const auto forcing = worldsim::make_weather_daily_forcing(state->impl);
        if (capacity < forcing.size()) throw std::invalid_argument("weather forcing output capacity is too small");
        for (std::size_t i = 0; i < forcing.size(); ++i) {
            out_forcing[i].precipitation_mm = forcing[i].precipitation_mm;
            out_forcing[i].mean_air_temperature_c = forcing[i].mean_air_temperature_c;
            out_forcing[i].potential_evapotranspiration_mm = forcing[i].potential_evapotranspiration_mm;
        }
    });
}

int ws_weather_advance_day(ws_weather_state* state, ws_weather_step_report* out_report) {
    return guarded([&] {
        if (!state || !out_report) throw std::invalid_argument("state/out_report is null");
        const auto report = worldsim::advance_weather_day(state->impl);
        copy_weather_report(report, *out_report);
    });
}

int ws_weather_multiresolution_water_advance_day(
    ws_world* world,
    ws_weather_state* weather,
    ws_multiresolution_water_state* water,
    ws_weather_water_step_report* out_report) {
    return guarded([&] {
        if (!world || !weather || !water || !out_report) {
            throw std::invalid_argument("world/weather/water/out_report is null");
        }
        const auto report = worldsim::advance_weather_multiresolution_water_day(
            world->impl, weather->impl, water->impl);
        copy_weather_report(report.weather, out_report->weather);
        copy_water_report(report.water, out_report->water);
    });
}

int ws_weather_save(const ws_weather_state* state, const char* utf8_path) {
    return guarded([&] {
        if (!state || !utf8_path) throw std::invalid_argument("state/path is null");
        worldsim::save_weather_state(state->impl, utf8_path);
    });
}

ws_weather_state* ws_weather_load(ws_world* world, const char* utf8_path) {
    try {
        if (!world || !utf8_path) throw std::invalid_argument("world/path is null");
        auto state = worldsim::load_weather_state(world->impl, utf8_path);
        auto out = std::make_unique<ws_weather_state>(std::move(state));
        g_weather_last_error.clear();
        return out.release();
    } catch (const std::exception& e) {
        g_weather_last_error = e.what();
        return nullptr;
    } catch (...) {
        g_weather_last_error = "unknown WorldSim weather error";
        return nullptr;
    }
}

const char* ws_weather_last_error(void) {
    return g_weather_last_error.c_str();
}

} // extern "C"
