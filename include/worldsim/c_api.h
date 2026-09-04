#pragma once

#include <stdint.h>

#ifdef _WIN32
  #if defined(WORLDSIM_BUILD_SHARED)
    #define WORLDSIM_API __declspec(dllexport)
  #elif defined(WORLDSIM_USE_SHARED)
    #define WORLDSIM_API __declspec(dllimport)
  #else
    #define WORLDSIM_API
  #endif
#else
  #define WORLDSIM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws_world ws_world;
typedef struct ws_hydrology_result ws_hydrology_result;
typedef struct ws_continental_hydrology_result ws_continental_hydrology_result;
typedef struct ws_dynamic_hydrology_state ws_dynamic_hydrology_state;
typedef struct ws_continental_water_state ws_continental_water_state;

typedef struct ws_world_config {
    uint64_t seed;
    double origin_x_m;
    double origin_y_m;
    double width_m;
    double height_m;
    float sea_level_m;
} ws_world_config;

typedef struct ws_regional_sample {
    int64_t cell_x;
    int64_t cell_y;
    float elevation_m;
    float slope;
    float terrain_roughness;
    float bedrock_hardness;
    float forest_potential;
} ws_regional_sample;

typedef struct ws_local_cell {
    float elevation_m;
    float terrain_roughness;
    float forest_potential;
    float disturbance;
} ws_local_cell;

typedef struct ws_local_vegetation_cell {
    uint32_t local_x;
    uint32_t local_y;
    float forest_potential;
    float disturbance;
    float vegetation_biomass;
} ws_local_vegetation_cell;

typedef struct ws_vegetation_forcing {
    int64_t regional_x;
    int64_t regional_y;
    float mean_air_temperature_c;
    float soil_saturation;
} ws_vegetation_forcing;

typedef struct ws_vegetation_step_report {
    uint64_t patch_count;
    uint64_t land_cell_count;
    double land_area_m2;
    double biomass_area_before_m2;
    double biomass_area_after_m2;
    double disturbance_area_before_m2;
    double disturbance_area_after_m2;
} ws_vegetation_step_report;

typedef struct ws_hydrology_request {
    int64_t min_region_x;
    int64_t min_region_y;
    uint32_t width_cells;
    uint32_t height_cells;
    float river_threshold_m3_s;
    float lake_min_depth_m;
} ws_hydrology_request;

typedef struct ws_hydrology_cell {
    int64_t cell_x;
    int64_t cell_y;
    int32_t active;
    int32_t ocean;
    float terrain_elevation_m;
    float filled_elevation_m;
    float depression_depth_m;
    float local_water_yield_m3_s;
    float accumulated_discharge_m3_s;
    int64_t downstream_x;
    int64_t downstream_y;
    int32_t has_downstream;
    int32_t downstream_is_external;
    uint64_t catchment_id;
    uint64_t lake_id;
    int32_t river;
} ws_hydrology_cell;

typedef struct ws_lake_info {
    uint64_t id;
    int64_t outlet_x;
    int64_t outlet_y;
    int64_t outflow_x;
    int64_t outflow_y;
    int32_t has_outflow;
    uint64_t cell_count;
    double area_m2;
    double volume_m3;
    float surface_elevation_m;
    float max_depth_m;
} ws_lake_info;

typedef struct ws_continental_hydrology_cell {
    int64_t cell_x;
    int64_t cell_y;
    float terrain_elevation_m;
    float filled_elevation_m;
    float depression_depth_m;
    float local_water_yield_m3_s;
    float accumulated_discharge_m3_s;
    int64_t downstream_x;
    int64_t downstream_y;
    int32_t has_downstream;
    int64_t terminal_outlet_x;
    int64_t terminal_outlet_y;
    uint64_t basin_id;
    int32_t ocean;
    int32_t river;
} ws_continental_hydrology_cell;

typedef struct ws_river_segment {
    int64_t from_x;
    int64_t from_y;
    int64_t to_x;
    int64_t to_y;
    float discharge_m3_s;
} ws_river_segment;

typedef struct ws_continental_water_forcing {
    float precipitation_mm;
    float mean_air_temperature_c;
    float potential_evapotranspiration_mm;
} ws_continental_water_forcing;

typedef struct ws_continental_water_cell_state {
    int64_t cell_x;
    int64_t cell_y;
    float snow_water_equivalent_mm;
    float surface_water_mm;
    float soil_water_mm;
    float groundwater_mm;
    float last_evapotranspiration_mm;
    float last_quick_runoff_mm;
    float last_baseflow_mm;
    float last_routed_discharge_m3_s;
} ws_continental_water_cell_state;

typedef struct ws_continental_water_step_report {
    int64_t day_before;
    int64_t day_after;
    double storage_before_m3;
    double precipitation_m3;
    double evapotranspiration_m3;
    double terminal_outflow_m3;
    double storage_after_m3;
    double water_balance_error_m3;
} ws_continental_water_step_report;

typedef struct ws_dynamic_hydrology_parameters {
    float soil_capacity_mm;
    float field_capacity_mm;
    float wilting_point_mm;
    float infiltration_capacity_mm_per_day;
    float surface_storage_capacity_mm;
    float percolation_rate_per_day;
    float groundwater_recession_per_day;
    float snow_melt_mm_per_c_day;
    float initial_soil_water_mm;
    float initial_groundwater_mm;
} ws_dynamic_hydrology_parameters;

typedef struct ws_hydrometeorological_forcing {
    int64_t cell_x;
    int64_t cell_y;
    float precipitation_mm;
    float mean_air_temperature_c;
    float potential_evapotranspiration_mm;
} ws_hydrometeorological_forcing;

typedef struct ws_external_hydrology_inflow {
    int64_t cell_x;
    int64_t cell_y;
    double volume_m3;
} ws_external_hydrology_inflow;

typedef struct ws_dynamic_hydrology_cell_state {
    int64_t cell_x;
    int64_t cell_y;
    int32_t active;
    float snow_water_equivalent_mm;
    float surface_water_mm;
    float soil_water_mm;
    float groundwater_mm;
    float last_evapotranspiration_mm;
    float last_quick_runoff_mm;
    float last_baseflow_mm;
    float last_routed_discharge_m3_s;
} ws_dynamic_hydrology_cell_state;

typedef struct ws_hydrology_step_report {
    double duration_days;
    double storage_before_m3;
    double precipitation_m3;
    double external_inflow_m3;
    double evapotranspiration_m3;
    double external_outflow_m3;
    double storage_after_m3;
    double water_balance_error_m3;
} ws_hydrology_step_report;

#define WS_LOCAL_PATCH_CELL_COUNT 256u
#define WS_MAX_HYDROLOGY_CELLS 262144u
#define WS_MAX_CONTINENTAL_HYDROLOGY_CELLS 1000000u
#define WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT 64u

WORLDSIM_API ws_world* ws_world_create(const ws_world_config* config);
WORLDSIM_API void ws_world_destroy(ws_world* world);
WORLDSIM_API int ws_world_sample_region(ws_world* world, double x_m, double y_m, ws_regional_sample* out_sample);
WORLDSIM_API int ws_world_materialize_region(ws_world* world, int64_t region_x, int64_t region_y);
WORLDSIM_API int ws_world_copy_local_patch(ws_world* world, int64_t region_x, int64_t region_y,
                                            ws_local_cell* out_cells, uint64_t capacity);
WORLDSIM_API int ws_world_copy_local_vegetation(
    ws_world* world,
    int64_t region_x,
    int64_t region_y,
    ws_local_vegetation_cell* out_cells,
    uint64_t capacity);
WORLDSIM_API int ws_world_advance_materialized_vegetation_day(
    ws_world* world,
    const ws_vegetation_forcing* forcing,
    uint64_t forcing_count,
    ws_vegetation_step_report* out_report);
WORLDSIM_API uint64_t ws_world_materialized_patch_count(ws_world* world);

WORLDSIM_API ws_continental_hydrology_result* ws_world_analyze_continental_hydrology(
    ws_world* world, float river_threshold_m3_s);
WORLDSIM_API void ws_continental_hydrology_result_destroy(ws_continental_hydrology_result* result);
WORLDSIM_API uint64_t ws_continental_hydrology_cell_count(const ws_continental_hydrology_result* result);
WORLDSIM_API int ws_continental_hydrology_copy_cells(
    const ws_continental_hydrology_result* result, ws_continental_hydrology_cell* out_cells, uint64_t capacity);
WORLDSIM_API ws_hydrology_result* ws_world_refine_authoritative_hydrology_tile(
    ws_world* world, const ws_continental_hydrology_result* continent,
    int64_t climate_x, int64_t climate_y, float river_threshold_m3_s, float lake_min_depth_m);

WORLDSIM_API ws_continental_water_state* ws_world_continental_water_create(
    ws_world* world, const ws_continental_hydrology_result* continent,
    const ws_dynamic_hydrology_parameters* parameters);
WORLDSIM_API void ws_continental_water_state_destroy(ws_continental_water_state* state);
WORLDSIM_API uint64_t ws_continental_water_cell_count(const ws_continental_water_state* state);
WORLDSIM_API int64_t ws_continental_water_simulated_day(const ws_continental_water_state* state);
WORLDSIM_API int ws_continental_water_copy_cells(
    const ws_continental_water_state* state, ws_continental_water_cell_state* out_cells, uint64_t capacity);
WORLDSIM_API int ws_continental_water_make_smooth_daily_forcing(
    const ws_continental_water_state* state,
    ws_continental_water_forcing* out_forcing, uint64_t capacity);
WORLDSIM_API int ws_continental_water_advance_day(
    ws_continental_water_state* state,
    const ws_continental_water_forcing* forcing, uint64_t forcing_count,
    ws_continental_water_step_report* out_report);

WORLDSIM_API ws_dynamic_hydrology_state* ws_world_dynamic_hydrology_create(
    ws_world* world, const ws_continental_hydrology_result* continent,
    int64_t climate_x, int64_t climate_y, const ws_dynamic_hydrology_parameters* parameters);
WORLDSIM_API void ws_dynamic_hydrology_state_destroy(ws_dynamic_hydrology_state* state);
WORLDSIM_API uint64_t ws_dynamic_hydrology_cell_count(const ws_dynamic_hydrology_state* state);
WORLDSIM_API double ws_dynamic_hydrology_simulated_days(const ws_dynamic_hydrology_state* state);
WORLDSIM_API int ws_dynamic_hydrology_copy_cells(
    const ws_dynamic_hydrology_state* state, ws_dynamic_hydrology_cell_state* out_cells, uint64_t capacity);
WORLDSIM_API int ws_dynamic_hydrology_make_smooth_climatological_forcing(
    ws_world* world, const ws_dynamic_hydrology_state* state,
    double day_of_year, double duration_days,
    ws_hydrometeorological_forcing* out_forcing, uint64_t capacity);
WORLDSIM_API int ws_dynamic_hydrology_advance(
    ws_world* world, ws_dynamic_hydrology_state* state,
    const ws_hydrometeorological_forcing* forcing, uint64_t forcing_count,
    const ws_external_hydrology_inflow* external_inflows, uint64_t external_inflow_count,
    double duration_days, ws_hydrology_step_report* out_report);

WORLDSIM_API ws_hydrology_result* ws_world_analyze_hydrology(ws_world* world, const ws_hydrology_request* request);
WORLDSIM_API void ws_hydrology_result_destroy(ws_hydrology_result* result);
WORLDSIM_API uint64_t ws_hydrology_cell_count(const ws_hydrology_result* result);
WORLDSIM_API uint64_t ws_hydrology_lake_count(const ws_hydrology_result* result);
WORLDSIM_API uint64_t ws_hydrology_river_segment_count(const ws_hydrology_result* result);
WORLDSIM_API int ws_hydrology_copy_cells(const ws_hydrology_result* result, ws_hydrology_cell* out_cells, uint64_t capacity);
WORLDSIM_API int ws_hydrology_copy_lakes(const ws_hydrology_result* result, ws_lake_info* out_lakes, uint64_t capacity);
WORLDSIM_API int ws_hydrology_copy_river_segments(const ws_hydrology_result* result, ws_river_segment* out_segments, uint64_t capacity);
WORLDSIM_API int ws_world_disturb_surface(ws_world* world, double min_x_m, double min_y_m,
                                          double max_x_m, double max_y_m, float amount,
                                          uint64_t* out_affected_cells);
WORLDSIM_API int ws_world_save(ws_world* world, const char* utf8_path);
WORLDSIM_API ws_world* ws_world_load(const char* utf8_path);
WORLDSIM_API const char* ws_last_error(void);

#ifdef __cplusplus
}
#endif
