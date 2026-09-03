#pragma once

#include "worldsim/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws_soil_properties {
    float storage_capacity_scale;
    float infiltration_capacity_scale;
} ws_soil_properties;

WORLDSIM_API int ws_world_sample_soil(
    ws_world* world,
    double x_m,
    double y_m,
    ws_soil_properties* out_properties);

WORLDSIM_API int ws_world_sample_climate_soil(
    ws_world* world,
    int64_t climate_x,
    int64_t climate_y,
    ws_soil_properties* out_properties);

WORLDSIM_API const char* ws_soil_last_error(void);

#ifdef __cplusplus
}
#endif
