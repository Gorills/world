#pragma once

#include "worldsim/multiresolution_water_c_api.h"
#include "worldsim/weather_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws_simulation_state ws_simulation_state;

typedef struct ws_simulation_day_report_v2 {
    ws_weather_water_step_report environment;
    ws_vegetation_step_report vegetation;
} ws_simulation_day_report_v2;

WORLDSIM_API ws_simulation_state* ws_simulation_create(
    const ws_world_config* config,
    const ws_weather_parameters* weather_parameters,
    const ws_dynamic_hydrology_parameters* water_parameters);
WORLDSIM_API void ws_simulation_destroy(ws_simulation_state* state);

WORLDSIM_API int64_t ws_simulation_simulated_day(const ws_simulation_state* state);
WORLDSIM_API uint64_t ws_simulation_l0_cell_count(const ws_simulation_state* state);
WORLDSIM_API uint64_t ws_simulation_materialized_patch_count(const ws_simulation_state* state);
WORLDSIM_API uint64_t ws_simulation_refined_tile_count(const ws_simulation_state* state);
WORLDSIM_API int ws_simulation_is_refined(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y);
WORLDSIM_API int ws_simulation_channel_storage_m3(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y,
    double* out_volume_m3);
WORLDSIM_API int ws_simulation_total_channel_storage_m3(
    const ws_simulation_state* state,
    double* out_volume_m3);
WORLDSIM_API int ws_simulation_channel_transport(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_channel_transport_properties* out_properties);

WORLDSIM_API int ws_simulation_sample_region(
    const ws_simulation_state* state,
    double x_m,
    double y_m,
    ws_regional_sample* out_sample);
WORLDSIM_API int ws_simulation_sample_weather(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_weather_cell_sample* out_sample);
WORLDSIM_API int ws_simulation_copy_coarse_water_cells(
    const ws_simulation_state* state,
    ws_continental_water_cell_state* out_cells,
    uint64_t capacity);
WORLDSIM_API int ws_simulation_copy_refined_water_cells(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_dynamic_hydrology_cell_state* out_cells,
    uint64_t capacity);
WORLDSIM_API int ws_simulation_copy_refined_daily_forcing(
    const ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y,
    ws_hydrometeorological_forcing* out_forcing,
    uint64_t capacity);
WORLDSIM_API int ws_simulation_copy_local_vegetation(
    const ws_simulation_state* state,
    int64_t region_x,
    int64_t region_y,
    ws_local_vegetation_cell* out_cells,
    uint64_t capacity);

WORLDSIM_API int ws_simulation_advance_day(
    ws_simulation_state* state,
    ws_weather_water_step_report* out_report);
WORLDSIM_API int ws_simulation_advance_day_v2(
    ws_simulation_state* state,
    ws_simulation_day_report_v2* out_report);
WORLDSIM_API int ws_simulation_materialize_refined_water_tile(
    ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y);
WORLDSIM_API int ws_simulation_aggregate_refined_water_tile(
    ws_simulation_state* state,
    int64_t climate_x,
    int64_t climate_y);
WORLDSIM_API int ws_simulation_disturb_surface(
    ws_simulation_state* state,
    double min_x_m,
    double min_y_m,
    double max_x_m,
    double max_y_m,
    float amount,
    uint64_t* out_affected_cells);

WORLDSIM_API int ws_simulation_save_checkpoint(
    const ws_simulation_state* state,
    const char* utf8_path);
WORLDSIM_API ws_simulation_state* ws_simulation_load_checkpoint(const char* utf8_path);
WORLDSIM_API const char* ws_simulation_last_error(void);

#ifdef __cplusplus
}
#endif
