#pragma once

#include "worldsim/multiresolution_water_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws_weather_state ws_weather_state;

typedef struct ws_weather_parameters {
    float temperature_memory;
    float moisture_memory;
    float temperature_variability_c;
    float moisture_variability;
    float storm_threshold;
    float storm_intensity;
} ws_weather_parameters;

typedef struct ws_weather_cell_sample {
    int64_t cell_x;
    int64_t cell_y;
    float temperature_anomaly_c;
    float moisture_anomaly;
    float precipitation_mm;
    float mean_air_temperature_c;
    float potential_evapotranspiration_mm;
} ws_weather_cell_sample;

typedef struct ws_weather_step_report {
    int64_t day_before;
    int64_t day_after;
    double precipitation_m3;
    double mean_air_temperature_c;
    double mean_potential_evapotranspiration_mm;
    double wet_area_fraction;
} ws_weather_step_report;

typedef struct ws_weather_water_step_report {
    ws_weather_step_report weather;
    ws_continental_water_step_report water;
} ws_weather_water_step_report;

WORLDSIM_API ws_weather_state* ws_world_weather_create(
    ws_world* world,
    const ws_weather_parameters* parameters);
WORLDSIM_API void ws_weather_state_destroy(ws_weather_state* state);
WORLDSIM_API uint64_t ws_weather_cell_count(const ws_weather_state* state);
WORLDSIM_API int64_t ws_weather_simulated_day(const ws_weather_state* state);
WORLDSIM_API int ws_weather_sample_cell(
    const ws_weather_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_weather_cell_sample* out_sample);
WORLDSIM_API int ws_weather_copy_daily_forcing(
    const ws_weather_state* state,
    ws_continental_water_forcing* out_forcing,
    uint64_t capacity);
WORLDSIM_API int ws_weather_advance_day(
    ws_weather_state* state,
    ws_weather_step_report* out_report);
WORLDSIM_API int ws_weather_multiresolution_water_advance_day(
    ws_world* world,
    ws_weather_state* weather,
    ws_multiresolution_water_state* water,
    ws_weather_water_step_report* out_report);
WORLDSIM_API int ws_weather_save(
    const ws_weather_state* state,
    const char* utf8_path);
WORLDSIM_API ws_weather_state* ws_weather_load(
    ws_world* world,
    const char* utf8_path);
WORLDSIM_API const char* ws_weather_last_error(void);

#ifdef __cplusplus
}
#endif
