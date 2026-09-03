#include "worldsim/multiresolution_water_c_api.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* message) {
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    const char* save_path = "/tmp/worldsim_multiresolution_water_c_api.bin";
    ws_world_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seed = 503;
    cfg.origin_x_m = -120000.0;
    cfg.origin_y_m = -100000.0;
    cfg.width_m = 240000.0;
    cfg.height_m = 200000.0;
    cfg.sea_level_m = 0.0f;

    ws_world* world = ws_world_create(&cfg);
    check(world != NULL, "C ABI creates world fixture");
    if (!world) return 1;

    ws_continental_hydrology_result* topology =
        ws_world_analyze_continental_hydrology(world, 0.1f);
    check(topology != NULL, "C ABI creates continental topology");
    if (!topology) {
        ws_world_destroy(world);
        return 1;
    }

    const uint64_t count = ws_continental_hydrology_cell_count(topology);
    check(count > 0, "C ABI continental topology contains cells");
    ws_continental_hydrology_cell* topo_cells =
        (ws_continental_hydrology_cell*)calloc((size_t)count, sizeof(*topo_cells));
    ws_continental_water_cell_state* coarse =
        (ws_continental_water_cell_state*)calloc((size_t)count, sizeof(*coarse));
    ws_continental_water_forcing* forcing =
        (ws_continental_water_forcing*)calloc((size_t)count, sizeof(*forcing));
    check(topo_cells != NULL && coarse != NULL && forcing != NULL, "C ABI test allocations succeed");
    if (!topo_cells || !coarse || !forcing) {
        free(topo_cells);
        free(coarse);
        free(forcing);
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }
    check(ws_continental_hydrology_copy_cells(topology, topo_cells, count) == 0,
          "C ABI copies continental topology cells");

    uint64_t parent_index = count;
    for (uint64_t i = 0; i < count; ++i) {
        if (!topo_cells[i].ocean) {
            parent_index = i;
            break;
        }
    }
    check(parent_index != count, "C ABI fixture contains refinable land");
    if (parent_index == count) {
        free(topo_cells);
        free(coarse);
        free(forcing);
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }
    const int64_t parent_x = topo_cells[parent_index].cell_x;
    const int64_t parent_y = topo_cells[parent_index].cell_y;

    ws_multiresolution_water_state* state =
        ws_world_multiresolution_water_create(world, topology, NULL);
    check(state != NULL, "C ABI creates multiresolution water state");
    if (!state) {
        free(topo_cells);
        free(coarse);
        free(forcing);
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }
    check(ws_multiresolution_water_simulated_day(state) == 0,
          "C ABI multiresolution clock starts at zero");
    check(ws_multiresolution_water_materialize(world, topology, state, parent_x, parent_y) == 0,
          "C ABI materializes refined water ownership");
    check(ws_multiresolution_water_refined_tile_count(state) == 1,
          "C ABI tracks one sparse refined tile");
    check(ws_multiresolution_water_is_refined(state, parent_x, parent_y) == 1,
          "C ABI reports refined parent ownership");

    check(ws_multiresolution_water_copy_coarse_cells(state, coarse, count) == 0,
          "C ABI copies coarse mirror state");
    check(coarse[parent_index].snow_water_equivalent_mm == 0.0f &&
          coarse[parent_index].surface_water_mm == 0.0f &&
          coarse[parent_index].soil_water_mm == 0.0f &&
          coarse[parent_index].groundwater_mm == 0.0f,
          "refined parent has no independent C ABI coarse water stores");

    ws_dynamic_hydrology_cell_state refined[WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT];
    memset(refined, 0, sizeof(refined));
    check(ws_multiresolution_water_copy_refined_cells(
              state, parent_x, parent_y, refined, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) == 0,
          "C ABI copies refined 8x8 water state");
    double refined_storage_depth_sum = 0.0;
    for (uint64_t i = 0; i < WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT; ++i) {
        if (refined[i].active) {
            refined_storage_depth_sum += refined[i].snow_water_equivalent_mm +
                                         refined[i].surface_water_mm +
                                         refined[i].soil_water_mm +
                                         refined[i].groundwater_mm;
        }
    }
    check(refined_storage_depth_sum > 0.0,
          "C ABI refined state owns transferred parent water");

    check(ws_multiresolution_water_make_smooth_daily_forcing(state, forcing, count) == 0,
          "C ABI creates aligned multiresolution forcing");
    const int64_t day_before_invalid = ws_multiresolution_water_simulated_day(state);
    forcing[parent_index].mean_air_temperature_c = NAN;
    ws_continental_water_step_report report;
    memset(&report, 0, sizeof(report));
    check(ws_multiresolution_water_advance_day(world, state, forcing, count, &report) == -1,
          "C ABI rejects invalid multiresolution forcing");
    check(ws_multiresolution_water_simulated_day(state) == day_before_invalid,
          "C ABI rejected forcing leaves global clock unchanged");
    check(strlen(ws_multiresolution_last_error()) > 0,
          "C ABI exposes multiresolution error text");

    check(ws_multiresolution_water_make_smooth_daily_forcing(state, forcing, count) == 0,
          "C ABI regenerates valid forcing after rejection");
    check(ws_multiresolution_water_advance_day(world, state, forcing, count, &report) == 0,
          "C ABI advances coupled coarse/refined day");
    check(report.day_before == 0 && report.day_after == 1 &&
          ws_multiresolution_water_simulated_day(state) == 1,
          "C ABI preserves one exact global day");
    check(isfinite(report.water_balance_error_m3),
          "C ABI reports finite coupled water balance");

    check(ws_multiresolution_water_save(state, save_path) == 0,
          "C ABI saves authoritative multiresolution ownership");
    ws_multiresolution_water_state* loaded =
        ws_multiresolution_water_load(world, topology, save_path);
    check(loaded != NULL, "C ABI reloads multiresolution ownership");
    if (loaded) {
        check(ws_multiresolution_water_simulated_day(loaded) == 1 &&
              ws_multiresolution_water_refined_tile_count(loaded) == 1 &&
              ws_multiresolution_water_is_refined(loaded, parent_x, parent_y) == 1,
              "C ABI reload preserves clock and refined ownership");
        check(ws_multiresolution_water_aggregate(world, loaded, parent_x, parent_y) == 0,
              "C ABI aggregates refined water back into L0");
        check(ws_multiresolution_water_refined_tile_count(loaded) == 0 &&
              ws_multiresolution_water_is_refined(loaded, parent_x, parent_y) == 0,
              "C ABI releases refined ownership after aggregation");
        memset(coarse, 0, (size_t)count * sizeof(*coarse));
        check(ws_multiresolution_water_copy_coarse_cells(loaded, coarse, count) == 0,
              "C ABI copies aggregated L0 state");
        check(coarse[parent_index].snow_water_equivalent_mm +
              coarse[parent_index].surface_water_mm +
              coarse[parent_index].soil_water_mm +
              coarse[parent_index].groundwater_mm > 0.0f,
              "C ABI aggregation returns authoritative water to parent L0 cell");
        ws_multiresolution_water_state_destroy(loaded);
    }

    check(ws_multiresolution_water_materialize(
              world, topology, state, INT64_MAX, INT64_MAX) == -1,
          "C ABI rejects a parent coordinate outside the topology");
    check(ws_multiresolution_water_refined_tile_count(state) == 1,
          "invalid C ABI materialization leaves ownership unchanged");

    remove(save_path);
    ws_multiresolution_water_state_destroy(state);
    free(topo_cells);
    free(coarse);
    free(forcing);
    ws_continental_hydrology_result_destroy(topology);
    ws_world_destroy(world);

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("All multiresolution water C ABI tests passed\n");
    return 0;
}
