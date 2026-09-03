#include "worldsim/simulation_c_api.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* message) {
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s; api_error=%s\n", message, ws_simulation_last_error());
    }
}

static int same_coarse_cell(
    const ws_continental_water_cell_state* a,
    const ws_continental_water_cell_state* b) {
    return a->cell_x == b->cell_x && a->cell_y == b->cell_y &&
        a->snow_water_equivalent_mm == b->snow_water_equivalent_mm &&
        a->surface_water_mm == b->surface_water_mm &&
        a->soil_water_mm == b->soil_water_mm &&
        a->groundwater_mm == b->groundwater_mm &&
        a->last_evapotranspiration_mm == b->last_evapotranspiration_mm &&
        a->last_quick_runoff_mm == b->last_quick_runoff_mm &&
        a->last_baseflow_mm == b->last_baseflow_mm &&
        a->last_routed_discharge_m3_s == b->last_routed_discharge_m3_s;
}

static int same_refined_cell(
    const ws_dynamic_hydrology_cell_state* a,
    const ws_dynamic_hydrology_cell_state* b) {
    return a->cell_x == b->cell_x && a->cell_y == b->cell_y && a->active == b->active &&
        a->snow_water_equivalent_mm == b->snow_water_equivalent_mm &&
        a->surface_water_mm == b->surface_water_mm &&
        a->soil_water_mm == b->soil_water_mm &&
        a->groundwater_mm == b->groundwater_mm &&
        a->last_evapotranspiration_mm == b->last_evapotranspiration_mm &&
        a->last_quick_runoff_mm == b->last_quick_runoff_mm &&
        a->last_baseflow_mm == b->last_baseflow_mm &&
        a->last_routed_discharge_m3_s == b->last_routed_discharge_m3_s;
}

static int same_weather_sample(
    const ws_weather_cell_sample* a,
    const ws_weather_cell_sample* b) {
    return a->cell_x == b->cell_x && a->cell_y == b->cell_y &&
        a->temperature_anomaly_c == b->temperature_anomaly_c &&
        a->moisture_anomaly == b->moisture_anomaly &&
        a->precipitation_mm == b->precipitation_mm &&
        a->mean_air_temperature_c == b->mean_air_temperature_c &&
        a->potential_evapotranspiration_mm == b->potential_evapotranspiration_mm;
}

static int same_step_report(
    const ws_weather_water_step_report* a,
    const ws_weather_water_step_report* b) {
    return a->weather.day_before == b->weather.day_before &&
        a->weather.day_after == b->weather.day_after &&
        a->weather.precipitation_m3 == b->weather.precipitation_m3 &&
        a->weather.mean_air_temperature_c == b->weather.mean_air_temperature_c &&
        a->weather.mean_potential_evapotranspiration_mm ==
            b->weather.mean_potential_evapotranspiration_mm &&
        a->weather.wet_area_fraction == b->weather.wet_area_fraction &&
        a->water.day_before == b->water.day_before &&
        a->water.day_after == b->water.day_after &&
        a->water.storage_before_m3 == b->water.storage_before_m3 &&
        a->water.precipitation_m3 == b->water.precipitation_m3 &&
        a->water.evapotranspiration_m3 == b->water.evapotranspiration_m3 &&
        a->water.terminal_outflow_m3 == b->water.terminal_outflow_m3 &&
        a->water.storage_after_m3 == b->water.storage_after_m3 &&
        a->water.water_balance_error_m3 == b->water.water_balance_error_m3;
}

int main(void) {
    const char* checkpoint_path = "worldsim_simulation_c_api.bin";
    ws_world_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seed = 2203;
    cfg.origin_x_m = -32768.0;
    cfg.origin_y_m = -32768.0;
    cfg.width_m = 65536.0;
    cfg.height_m = 65536.0;
    cfg.sea_level_m = -10000.0f;

    ws_simulation_state* simulation = ws_simulation_create(&cfg, NULL, NULL);
    check(simulation != NULL, "simulation C ABI creates unified owner");
    if (!simulation) return 1;

    const uint64_t l0_count = ws_simulation_l0_cell_count(simulation);
    check(l0_count > 0, "simulation C ABI exposes L0 cell count");
    check(ws_simulation_simulated_day(simulation) == 0,
          "simulation C ABI global clock starts at zero");
    check(ws_simulation_materialized_patch_count(simulation) == 0,
          "simulation construction does not materialize L2");

    ws_regional_sample region;
    memset(&region, 0, sizeof(region));
    check(ws_simulation_sample_region(simulation, 0.0, 0.0, &region) == 0,
          "simulation C ABI samples regional state");
    check(ws_simulation_materialized_patch_count(simulation) == 0,
          "simulation regional sampling remains non-materializing");

    check(ws_simulation_materialize_refined_water_tile(simulation, 0, 0) == 0,
          "simulation C ABI materializes refined water through unified owner");
    check(ws_simulation_refined_tile_count(simulation) == 1 &&
          ws_simulation_is_refined(simulation, 0, 0) == 1,
          "simulation C ABI tracks refined ownership");

    ws_dynamic_hydrology_cell_state refined[WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT];
    memset(refined, 0, sizeof(refined));
    check(ws_simulation_copy_refined_water_cells(
              simulation, 0, 0, refined, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT - 1) == -1,
          "simulation C ABI rejects undersized refined output");
    check(strlen(ws_simulation_last_error()) > 0,
          "simulation C ABI exposes refined capacity error");
    check(ws_simulation_copy_refined_water_cells(
              simulation, 0, 0, refined, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) == 0,
          "simulation C ABI copies refined authoritative water");

    uint64_t affected = 0;
    check(ws_simulation_disturb_surface(
              simulation, -512.0, -512.0, 512.0, 512.0, 0.7f, &affected) == 0 &&
          affected > 0,
          "simulation C ABI mutates persistent surface through unified owner");
    const uint64_t materialized_after_disturb =
        ws_simulation_materialized_patch_count(simulation);
    check(materialized_after_disturb > 0,
          "simulation C ABI disturbance owns persistent L2 history");

    for (int day = 0; day < 4; ++day) {
        ws_weather_water_step_report report;
        memset(&report, 0, sizeof(report));
        check(ws_simulation_advance_day(simulation, &report) == 0,
              "simulation C ABI advances one unified day");
        check(report.weather.day_before == day && report.weather.day_after == day + 1 &&
              report.water.day_before == day && report.water.day_after == day + 1 &&
              ws_simulation_simulated_day(simulation) == day + 1,
              "simulation C ABI exposes one exact weather/water/global clock");
        check(isfinite(report.water.water_balance_error_m3),
              "simulation C ABI water report remains finite");
    }

    check(ws_simulation_copy_refined_water_cells(
              simulation, 0, 0, refined, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) == 0,
          "simulation C ABI snapshots current refined state before checkpoint");

    ws_weather_cell_sample weather_before;
    memset(&weather_before, 0, sizeof(weather_before));
    check(ws_simulation_sample_weather(simulation, 0, 0, &weather_before) == 0,
          "simulation C ABI samples authoritative weather");

    ws_continental_water_cell_state* coarse_before =
        (ws_continental_water_cell_state*)calloc((size_t)l0_count, sizeof(*coarse_before));
    ws_continental_water_cell_state* coarse_loaded =
        (ws_continental_water_cell_state*)calloc((size_t)l0_count, sizeof(*coarse_loaded));
    check(coarse_before != NULL && coarse_loaded != NULL,
          "simulation C ABI coarse comparison allocations succeed");
    if (!coarse_before || !coarse_loaded) {
        free(coarse_before);
        free(coarse_loaded);
        ws_simulation_destroy(simulation);
        return 1;
    }
    check(ws_simulation_copy_coarse_water_cells(simulation, coarse_before, l0_count) == 0,
          "simulation C ABI copies coarse authoritative water");

    check(ws_simulation_save_checkpoint(simulation, checkpoint_path) == 0,
          "simulation C ABI publishes compound checkpoint");
    ws_simulation_state* loaded = ws_simulation_load_checkpoint(checkpoint_path);
    check(loaded != NULL, "simulation C ABI reloads compound checkpoint");
    if (loaded) {
        check(ws_simulation_simulated_day(loaded) == ws_simulation_simulated_day(simulation) &&
              ws_simulation_l0_cell_count(loaded) == l0_count,
              "simulation C ABI reload preserves global clock and L0 shape");
        check(ws_simulation_materialized_patch_count(loaded) == materialized_after_disturb,
              "simulation C ABI checkpoint preserves persistent L2 ownership");
        check(ws_simulation_refined_tile_count(loaded) == 1 &&
              ws_simulation_is_refined(loaded, 0, 0) == 1,
              "simulation C ABI checkpoint preserves refined ownership");

        ws_weather_cell_sample weather_loaded;
        memset(&weather_loaded, 0, sizeof(weather_loaded));
        check(ws_simulation_sample_weather(loaded, 0, 0, &weather_loaded) == 0 &&
              same_weather_sample(&weather_before, &weather_loaded),
              "simulation C ABI checkpoint preserves exact weather state");

        check(ws_simulation_copy_coarse_water_cells(loaded, coarse_loaded, l0_count) == 0,
              "simulation C ABI reads reloaded coarse water");
        int same_coarse = 1;
        for (uint64_t i = 0; i < l0_count; ++i) {
            same_coarse = same_coarse && same_coarse_cell(&coarse_before[i], &coarse_loaded[i]);
        }
        check(same_coarse,
              "simulation C ABI checkpoint preserves exact coarse water state");

        ws_dynamic_hydrology_cell_state refined_loaded[WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT];
        memset(refined_loaded, 0, sizeof(refined_loaded));
        check(ws_simulation_copy_refined_water_cells(
                  loaded, 0, 0, refined_loaded, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) == 0,
              "simulation C ABI reads reloaded refined water");
        int same_refined = 1;
        for (uint64_t i = 0; i < WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT; ++i) {
            same_refined = same_refined && same_refined_cell(&refined[i], &refined_loaded[i]);
        }
        check(same_refined,
              "simulation C ABI checkpoint preserves exact refined water state");

        ws_weather_water_step_report original_next;
        ws_weather_water_step_report loaded_next;
        memset(&original_next, 0, sizeof(original_next));
        memset(&loaded_next, 0, sizeof(loaded_next));
        check(ws_simulation_advance_day(simulation, &original_next) == 0 &&
              ws_simulation_advance_day(loaded, &loaded_next) == 0 &&
              same_step_report(&original_next, &loaded_next),
              "simulation C ABI reload preserves exact future evolution");

        check(ws_simulation_save_checkpoint(loaded, checkpoint_path) == 0,
              "simulation C ABI atomically replaces an existing checkpoint");
        check(ws_simulation_materialize_refined_water_tile(loaded, INT64_MAX, INT64_MAX) == -1 &&
              ws_simulation_refined_tile_count(loaded) == 1,
              "simulation C ABI rejected refinement leaves ownership unchanged");
        check(ws_simulation_aggregate_refined_water_tile(loaded, 0, 0) == 0 &&
              ws_simulation_refined_tile_count(loaded) == 0,
              "simulation C ABI aggregates refined ownership through unified owner");
        ws_simulation_destroy(loaded);
    }

    check(ws_simulation_save_checkpoint(simulation, NULL) == -1 &&
          strlen(ws_simulation_last_error()) > 0,
          "simulation C ABI rejects null checkpoint path with error text");

    free(coarse_before);
    free(coarse_loaded);
    ws_simulation_destroy(simulation);
    remove(checkpoint_path);

    if (failures != 0) {
        fprintf(stderr, "%d simulation C ABI test(s) failed\n", failures);
        return 1;
    }
    printf("All simulation C ABI tests passed\n");
    return 0;
}
