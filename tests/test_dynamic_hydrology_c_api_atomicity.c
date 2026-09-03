#include "worldsim/c_api.h"

#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kTileCellCount = 64 };

static int fail(const char* message) {
    fprintf(stderr, "FAIL: %s; api_error=%s\n", message, ws_last_error());
    return 1;
}

int main(void) {
    ws_world_config cfg = {0};
    cfg.seed = 921;
    cfg.origin_x_m = -120000.0;
    cfg.origin_y_m = -100000.0;
    cfg.width_m = 240000.0;
    cfg.height_m = 200000.0;
    cfg.sea_level_m = -10000.0f;

    ws_world* world = ws_world_create(&cfg);
    if (!world) return fail("create world");

    ws_continental_hydrology_result* continent =
        ws_world_analyze_continental_hydrology(world, 0.1f);
    if (!continent) {
        ws_world_destroy(world);
        return fail("analyze continental hydrology");
    }

    const uint64_t count = ws_continental_hydrology_cell_count(continent);
    ws_continental_hydrology_cell* cells =
        (ws_continental_hydrology_cell*)calloc((size_t)count, sizeof(*cells));
    if (!cells || ws_continental_hydrology_copy_cells(continent, cells, count) != 0) {
        free(cells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("copy continental hydrology");
    }

    uint64_t parent = count;
    for (uint64_t i = 0; i < count; ++i) {
        if (!cells[i].ocean) {
            parent = i;
            break;
        }
    }
    if (parent == count) {
        free(cells);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("find terrestrial parent");
    }

    ws_dynamic_hydrology_state* state = ws_world_dynamic_hydrology_create(
        world, continent, cells[parent].cell_x, cells[parent].cell_y, NULL);
    free(cells);
    if (!state) {
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("create dynamic hydrology state");
    }

    ws_hydrometeorological_forcing forcing[kTileCellCount];
    if (ws_dynamic_hydrology_make_smooth_climatological_forcing(
            world, state, 120.0, 1.0, forcing, kTileCellCount) != 0) {
        ws_dynamic_hydrology_state_destroy(state);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("make forcing");
    }

    ws_dynamic_hydrology_cell_state before[kTileCellCount];
    ws_dynamic_hydrology_cell_state after[kTileCellCount];
    if (ws_dynamic_hydrology_copy_cells(state, before, kTileCellCount) != 0) {
        ws_dynamic_hydrology_state_destroy(state);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("copy initial state");
    }

    uint64_t active = kTileCellCount;
    for (uint64_t i = 0; i < kTileCellCount; ++i) {
        if (before[i].active) {
            active = i;
            break;
        }
    }
    if (active == kTileCellCount) {
        ws_dynamic_hydrology_state_destroy(state);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("find active L1 cell");
    }

    const double day_before = ws_dynamic_hydrology_simulated_days(state);
    forcing[active].precipitation_mm = FLT_MAX;
    ws_hydrology_step_report report = {0};
    if (ws_dynamic_hydrology_advance(
            world, state, forcing, kTileCellCount, NULL, 0, 1.0, &report) == 0) {
        ws_dynamic_hydrology_state_destroy(state);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("unsafe finite forcing was accepted");
    }
    if (ws_dynamic_hydrology_simulated_days(state) != day_before ||
        ws_dynamic_hydrology_copy_cells(state, after, kTileCellCount) != 0 ||
        memcmp(before, after, sizeof(before)) != 0) {
        ws_dynamic_hydrology_state_destroy(state);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("rejected C ABI forcing changed state");
    }

    if (ws_dynamic_hydrology_make_smooth_climatological_forcing(
            world, state, 120.0, 1.0, forcing, kTileCellCount) != 0) {
        ws_dynamic_hydrology_state_destroy(state);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("restore forcing");
    }

    ws_external_hydrology_inflow inflows[2];
    inflows[0].cell_x = before[active].cell_x;
    inflows[0].cell_y = before[active].cell_y;
    inflows[0].volume_m3 = DBL_MAX * 0.75;
    inflows[1] = inflows[0];
    memset(&report, 0, sizeof(report));
    if (ws_dynamic_hydrology_advance(
            world, state, forcing, kTileCellCount, inflows, 2, 1.0, &report) == 0) {
        ws_dynamic_hydrology_state_destroy(state);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("overflowing external inflow was accepted");
    }
    if (ws_dynamic_hydrology_simulated_days(state) != day_before ||
        ws_dynamic_hydrology_copy_cells(state, after, kTileCellCount) != 0 ||
        memcmp(before, after, sizeof(before)) != 0) {
        ws_dynamic_hydrology_state_destroy(state);
        ws_continental_hydrology_result_destroy(continent);
        ws_world_destroy(world);
        return fail("rejected C ABI inflow changed state");
    }

    ws_dynamic_hydrology_state_destroy(state);
    ws_continental_hydrology_result_destroy(continent);
    ws_world_destroy(world);
    return 0;
}
