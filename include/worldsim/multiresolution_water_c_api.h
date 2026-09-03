#pragma once

#include "worldsim/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws_multiresolution_water_state ws_multiresolution_water_state;

WORLDSIM_API ws_multiresolution_water_state* ws_world_multiresolution_water_create(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    const ws_dynamic_hydrology_parameters* parameters);
WORLDSIM_API void ws_multiresolution_water_state_destroy(ws_multiresolution_water_state* state);
WORLDSIM_API int64_t ws_multiresolution_water_simulated_day(
    const ws_multiresolution_water_state* state);
WORLDSIM_API uint64_t ws_multiresolution_water_refined_tile_count(
    const ws_multiresolution_water_state* state);
WORLDSIM_API int ws_multiresolution_water_is_refined(
    const ws_multiresolution_water_state* state, int64_t climate_x, int64_t climate_y);
WORLDSIM_API int ws_multiresolution_water_materialize(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    ws_multiresolution_water_state* state,
    int64_t climate_x, int64_t climate_y);
WORLDSIM_API int ws_multiresolution_water_aggregate(
    ws_world* world,
    ws_multiresolution_water_state* state,
    int64_t climate_x, int64_t climate_y);
WORLDSIM_API int ws_multiresolution_water_copy_coarse_cells(
    const ws_multiresolution_water_state* state,
    ws_continental_water_cell_state* out_cells,
    uint64_t capacity);
WORLDSIM_API int ws_multiresolution_water_copy_refined_cells(
    const ws_multiresolution_water_state* state,
    int64_t climate_x, int64_t climate_y,
    ws_dynamic_hydrology_cell_state* out_cells,
    uint64_t capacity);
WORLDSIM_API int ws_multiresolution_water_make_smooth_daily_forcing(
    const ws_multiresolution_water_state* state,
    ws_continental_water_forcing* out_forcing,
    uint64_t capacity);
WORLDSIM_API int ws_multiresolution_water_advance_day(
    ws_world* world,
    ws_multiresolution_water_state* state,
    const ws_continental_water_forcing* forcing,
    uint64_t forcing_count,
    ws_continental_water_step_report* out_report);
WORLDSIM_API int ws_multiresolution_water_save(
    const ws_multiresolution_water_state* state,
    const char* utf8_path);
WORLDSIM_API ws_multiresolution_water_state* ws_multiresolution_water_load(
    ws_world* world,
    const ws_continental_hydrology_result* continent,
    const char* utf8_path);
WORLDSIM_API const char* ws_multiresolution_last_error(void);

#ifdef __cplusplus
}
#endif
