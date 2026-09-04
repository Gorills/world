#include "worldsim/simulation_c_api.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* message) {
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    ws_world_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seed = 14003;
    cfg.origin_x_m = 0.0;
    cfg.origin_y_m = 0.0;
    cfg.width_m = 2048.0;
    cfg.height_m = 2048.0;
    cfg.sea_level_m = -10000.0f;

    ws_world* world = ws_world_create(&cfg);
    check(world != NULL, "vegetation C ABI creates world");
    if (!world) return 1;

    ws_local_vegetation_cell cells[WS_LOCAL_PATCH_CELL_COUNT];
    memset(cells, 0, sizeof(cells));
    check(ws_world_copy_local_vegetation(
              world, 0, 0, cells, WS_LOCAL_PATCH_CELL_COUNT - 1) == -1,
          "vegetation C ABI rejects undersized local output");
    check(ws_world_copy_local_vegetation(
              world, 0, 0, cells, WS_LOCAL_PATCH_CELL_COUNT) == 0,
          "vegetation C ABI materializes and copies local vegetation");
    check(cells[0].local_x == 0 && cells[0].local_y == 0 &&
          cells[0].vegetation_biomass == cells[0].forest_potential,
          "vegetation C ABI exposes initialized biomass");

    uint64_t affected = 0;
    check(ws_world_disturb_surface(
              world, 0.0, 0.0, 64.0, 64.0, 0.8f, &affected) == 0 &&
          affected == 1,
          "vegetation C ABI disturbance affects one local cell");
    check(ws_world_copy_local_vegetation(
              world, 0, 0, cells, WS_LOCAL_PATCH_CELL_COUNT) == 0 &&
          cells[0].disturbance == 0.8f &&
          cells[0].vegetation_biomass <= cells[0].forest_potential * 0.2f + 1e-6f,
          "vegetation C ABI exposes immediate biomass damage");

    ws_vegetation_forcing forcing;
    forcing.regional_x = 0;
    forcing.regional_y = 0;
    forcing.mean_air_temperature_c = 20.0f;
    forcing.soil_saturation = 1.0f;
    ws_vegetation_step_report report;
    memset(&report, 0, sizeof(report));
    check(ws_world_advance_materialized_vegetation_day(
              world, &forcing, 1, &report) == 0 &&
          report.patch_count == 1 &&
          report.biomass_area_after_m2 >= report.biomass_area_before_m2 &&
          report.disturbance_area_after_m2 < report.disturbance_area_before_m2,
          "vegetation C ABI advances sparse recovery");

    forcing.mean_air_temperature_c = NAN;
    check(ws_world_advance_materialized_vegetation_day(
              world, &forcing, 1, &report) == -1 &&
          strlen(ws_last_error()) > 0,
          "vegetation C ABI rejects invalid forcing with error text");

    ws_world_destroy(world);

    ws_simulation_state* simulation = ws_simulation_create(&cfg, NULL, NULL);
    check(simulation != NULL, "vegetation unified C ABI creates simulation");
    if (!simulation) {
        return failures == 0 ? 1 : failures;
    }

    memset(cells, 0, sizeof(cells));
    check(ws_simulation_copy_local_vegetation(
              simulation, 0, 0, cells, WS_LOCAL_PATCH_CELL_COUNT) == -1 &&
          ws_simulation_materialized_patch_count(simulation) == 0,
          "simulation vegetation query does not materialize missing L2 history");

    affected = 0;
    check(ws_simulation_disturb_surface(
              simulation, 0.0, 0.0, 64.0, 64.0, 0.7f, &affected) == 0 &&
          affected == 1 &&
          ws_simulation_materialized_patch_count(simulation) == 1,
          "simulation C ABI disturbance materializes vegetation history");
    check(ws_simulation_copy_local_vegetation(
              simulation, 0, 0, cells, WS_LOCAL_PATCH_CELL_COUNT) == 0,
          "simulation C ABI copies existing local vegetation");

    const int64_t day_before = ws_simulation_simulated_day(simulation);
    ws_simulation_day_report_v2 full;
    memset(&full, 0, sizeof(full));
    check(ws_simulation_advance_day_v2(simulation, &full) == 0 &&
          ws_simulation_simulated_day(simulation) == day_before + 1 &&
          full.environment.weather.day_before == day_before &&
          full.environment.water.day_after == day_before + 1 &&
          full.vegetation.patch_count == 1,
          "simulation v2 C ABI advances environment and sparse vegetation together");
    check(full.vegetation.disturbance_area_after_m2 <
              full.vegetation.disturbance_area_before_m2,
          "simulation v2 C ABI reports vegetation recovery");

    ws_simulation_destroy(simulation);

    if (failures != 0) {
        fprintf(stderr, "%d vegetation C ABI test(s) failed\n", failures);
        return 1;
    }
    printf("All vegetation C ABI tests passed\n");
    return 0;
}
