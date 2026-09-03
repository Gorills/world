#include "worldsim/c_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(const char* message) {
    fprintf(stderr, "FAIL: %s; api_error=%s\n", message, ws_last_error());
    return 1;
}

int main(void) {
    ws_world_config cfg = {0};
    cfg.seed = 1234;
    cfg.origin_x_m = 0.0;
    cfg.origin_y_m = 0.0;
    cfg.width_m = 128000.0;
    cfg.height_m = 128000.0;

    ws_world* world = ws_world_create(&cfg);
    if (!world) return fail("create world");

    ws_regional_sample sample = {0};
    if (ws_world_sample_region(world, 42000.0, 37000.0, &sample) != 0) {
        ws_world_destroy(world);
        return fail("sample region");
    }
    if (ws_world_materialized_patch_count(world) != 0) {
        ws_world_destroy(world);
        return fail("L1 sample must not materialize L2");
    }

    ws_local_cell cells[WS_LOCAL_PATCH_CELL_COUNT];
    if (ws_world_copy_local_patch(world, sample.cell_x, sample.cell_y, cells, WS_LOCAL_PATCH_CELL_COUNT) != 0) {
        ws_world_destroy(world);
        return fail("copy local patch");
    }
    if (ws_world_materialized_patch_count(world) != 1) {
        ws_world_destroy(world);
        return fail("local copy materializes exactly one patch");
    }

    ws_hydrology_request hreq = {0};
    hreq.min_region_x = 0;
    hreq.min_region_y = 0;
    hreq.width_cells = 64;
    hreq.height_cells = 64;
    hreq.river_threshold_m3_s = 0.2f;
    hreq.lake_min_depth_m = 0.25f;
    ws_hydrology_result* hydrology = ws_world_analyze_hydrology(world, &hreq);
    if (!hydrology) {
        ws_world_destroy(world);
        return fail("analyze hydrology");
    }
    const uint64_t hydro_cells = ws_hydrology_cell_count(hydrology);
    if (hydro_cells != 4096) {
        ws_hydrology_result_destroy(hydrology);
        ws_world_destroy(world);
        return fail("hydrology cell count");
    }
    ws_hydrology_cell* hcells = (ws_hydrology_cell*)calloc((size_t)hydro_cells, sizeof(ws_hydrology_cell));
    if (!hcells || ws_hydrology_copy_cells(hydrology, hcells, hydro_cells) != 0) {
        free(hcells);
        ws_hydrology_result_destroy(hydrology);
        ws_world_destroy(world);
        return fail("copy hydrology cells");
    }
    if (hcells[0].catchment_id == 0) {
        free(hcells);
        ws_hydrology_result_destroy(hydrology);
        ws_world_destroy(world);
        return fail("hydrology catchment ids");
    }
    free(hcells);

    const uint64_t lake_count = ws_hydrology_lake_count(hydrology);
    if (lake_count == 0) {
        ws_hydrology_result_destroy(hydrology);
        ws_world_destroy(world);
        return fail("hydrology lake fixture");
    }
    if (lake_count > 0) {
        ws_lake_info* lakes = (ws_lake_info*)calloc((size_t)lake_count, sizeof(ws_lake_info));
        if (!lakes || ws_hydrology_copy_lakes(hydrology, lakes, lake_count) != 0) {
            free(lakes);
            ws_hydrology_result_destroy(hydrology);
            ws_world_destroy(world);
            return fail("copy hydrology lakes");
        }
        for (uint64_t i = 0; i < lake_count; ++i) {
            if (!lakes[i].has_outflow) {
                free(lakes);
                ws_hydrology_result_destroy(hydrology);
                ws_world_destroy(world);
                return fail("hydrology lake outflow connectivity");
            }
        }
        free(lakes);
    }
    ws_hydrology_result_destroy(hydrology);


    ws_continental_hydrology_result* continent = ws_world_analyze_continental_hydrology(world, 0.1f);
    if (!continent) {
        ws_world_destroy(world);
        return fail("analyze continental hydrology");
    }
    const uint64_t continent_count = ws_continental_hydrology_cell_count(continent);
    if (continent_count == 0 || continent_count > WS_MAX_CONTINENTAL_HYDROLOGY_CELLS) {
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("continental hydrology cell count");
    }
    ws_continental_hydrology_cell* ccells =
        (ws_continental_hydrology_cell*)calloc((size_t)continent_count, sizeof(ws_continental_hydrology_cell));
    if (!ccells || ws_continental_hydrology_copy_cells(continent, ccells, continent_count) != 0) {
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("copy continental hydrology");
    }
    uint64_t refinable = continent_count;
    for (uint64_t i = 0; i < continent_count; ++i) {
        if (!ccells[i].ocean && ccells[i].has_downstream && ccells[i].basin_id != 0) {
            refinable = i;
            break;
        }
    }
    if (refinable == continent_count) {
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("continental fixture must contain a refinable land cell");
    }
    ws_hydrology_result* tile = ws_world_refine_authoritative_hydrology_tile(
        world, continent, ccells[refinable].cell_x, ccells[refinable].cell_y, 0.1f, 0.1f);
    if (!tile || ws_hydrology_cell_count(tile) != 64) {
        ws_hydrology_result_destroy(tile);
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("refine authoritative hydrology tile");
    }
    ws_hydrology_cell tcells[64];
    if (ws_hydrology_copy_cells(tile, tcells, 64) != 0) {
        ws_hydrology_result_destroy(tile);
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("copy authoritative hydrology tile");
    }
    int external_edges = 0;
    for (uint64_t i = 0; i < 64; ++i) {
        if (tcells[i].active && tcells[i].downstream_is_external) ++external_edges;
    }
    if (external_edges != 1) {
        ws_hydrology_result_destroy(tile);
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("authoritative tile external edge contract");
    }

    ws_dynamic_hydrology_state* dynamic = ws_world_dynamic_hydrology_create(
        world, continent, ccells[refinable].cell_x, ccells[refinable].cell_y, NULL);
    if (!dynamic || ws_dynamic_hydrology_cell_count(dynamic) != WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) {
        ws_dynamic_hydrology_state_destroy(dynamic);
        ws_hydrology_result_destroy(tile);
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("create dynamic hydrology state");
    }
    ws_hydrometeorological_forcing forcing[WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT];
    if (ws_dynamic_hydrology_make_smooth_climatological_forcing(
            world, dynamic, 120.0, 1.0, forcing, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) != 0) {
        ws_dynamic_hydrology_state_destroy(dynamic);
        ws_hydrology_result_destroy(tile);
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("make dynamic hydrology forcing");
    }
    ws_hydrology_step_report step_report = {0};
    if (ws_dynamic_hydrology_advance(
            world, dynamic, forcing, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT,
            NULL, 0, 1.0, &step_report) != 0) {
        ws_dynamic_hydrology_state_destroy(dynamic);
        ws_hydrology_result_destroy(tile);
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("advance dynamic hydrology");
    }
    if (fabs(step_report.water_balance_error_m3) >= 3.0 || fabs(ws_dynamic_hydrology_simulated_days(dynamic) - 1.0) > 1e-9) {
        ws_dynamic_hydrology_state_destroy(dynamic);
        ws_hydrology_result_destroy(tile);
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("dynamic hydrology water balance/time");
    }
    ws_dynamic_hydrology_cell_state dynamic_cells[WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT];
    if (ws_dynamic_hydrology_copy_cells(dynamic, dynamic_cells, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) != 0) {
        ws_dynamic_hydrology_state_destroy(dynamic);
        ws_hydrology_result_destroy(tile);
        free(ccells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("copy dynamic hydrology cells");
    }
    for (uint64_t i = 0; i < WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT; ++i) {
        if (dynamic_cells[i].active &&
            (dynamic_cells[i].snow_water_equivalent_mm < 0.0f || dynamic_cells[i].surface_water_mm < 0.0f ||
             dynamic_cells[i].soil_water_mm < 0.0f || dynamic_cells[i].groundwater_mm < 0.0f)) {
            ws_dynamic_hydrology_state_destroy(dynamic);
            ws_hydrology_result_destroy(tile);
            free(ccells);
            ws_continental_hydrology_result_destroy(continent);
            ws_world_destroy(world);
            return fail("dynamic hydrology non-negative stores");
        }
    }
    ws_dynamic_hydrology_state_destroy(dynamic);
    ws_hydrology_result_destroy(tile);
    free(ccells);
    ws_continental_hydrology_result_destroy(continent);

    uint64_t affected = 0;
    if (ws_world_disturb_surface(world, 41900.0, 36900.0, 42220.0, 37120.0, 0.8f, &affected) != 0) {
        ws_world_destroy(world);
        return fail("disturb surface");
    }
    if (affected == 0) {
        ws_world_destroy(world);
        return fail("disturbance should affect local cells");
    }

    const char* path = "/tmp/worldsim_c_api_test.ws";
    if (ws_world_save(world, path) != 0) {
        ws_world_destroy(world);
        return fail("save world");
    }
    ws_world_destroy(world);

    world = ws_world_load(path);
    if (!world) return fail("load world");
    if (ws_world_materialized_patch_count(world) == 0) {
        ws_world_destroy(world);
        return fail("persistent patches survive load");
    }
    ws_world_destroy(world);
    remove(path);
    return 0;
}
