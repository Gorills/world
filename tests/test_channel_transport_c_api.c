#include "worldsim/multiresolution_water_c_api.h"
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
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static int valid_transport(const ws_channel_transport_properties* p) {
    return isfinite(p->reach_length_m) && p->reach_length_m > 0.0 &&
        isfinite(p->downhill_gradient) && p->downhill_gradient >= 0.0 &&
        isfinite(p->residence_days) && p->residence_days > 0.0 &&
        isfinite(p->release_fraction_per_day) && p->release_fraction_per_day > 0.0 &&
        p->release_fraction_per_day < 1.0;
}

static int same_transport(
    const ws_channel_transport_properties* a,
    const ws_channel_transport_properties* b) {
    return a->reach_length_m == b->reach_length_m &&
        a->downhill_gradient == b->downhill_gradient &&
        a->residence_days == b->residence_days &&
        a->release_fraction_per_day == b->release_fraction_per_day;
}

int main(void) {
    ws_world_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seed = 4211;
    cfg.origin_x_m = 0.0;
    cfg.origin_y_m = 0.0;
    cfg.width_m = 16384.0;
    cfg.height_m = 16384.0;
    cfg.sea_level_m = -10000.0f;

    ws_world* world = ws_world_create(&cfg);
    check(world != NULL, "channel transport C ABI creates world");
    if (!world) return 1;

    ws_continental_hydrology_result* topology =
        ws_world_analyze_continental_hydrology(world, 0.1f);
    check(topology != NULL, "channel transport C ABI creates topology");
    if (!topology) {
        ws_world_destroy(world);
        return 1;
    }

    const uint64_t count = ws_continental_hydrology_cell_count(topology);
    ws_continental_hydrology_cell* cells =
        (ws_continental_hydrology_cell*)calloc((size_t)count, sizeof(*cells));
    check(count > 0 && cells != NULL, "channel transport C ABI allocates topology cells");
    if (!cells || ws_continental_hydrology_copy_cells(topology, cells, count) != 0) {
        free(cells);
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }

    uint64_t land_index = count;
    for (uint64_t i = 0; i < count; ++i) {
        if (!cells[i].ocean) {
            land_index = i;
            break;
        }
    }
    check(land_index != count, "channel transport C ABI fixture has land");
    if (land_index == count) {
        free(cells);
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }

    ws_multiresolution_water_state* water =
        ws_world_multiresolution_water_create(world, topology, NULL);
    check(water != NULL, "channel transport C ABI creates water state");
    if (!water) {
        free(cells);
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }

    ws_channel_transport_properties low_level;
    memset(&low_level, 0, sizeof(low_level));
    check(ws_multiresolution_water_channel_transport(
              water, cells[land_index].cell_x, cells[land_index].cell_y, &low_level) == 0 &&
          valid_transport(&low_level),
          "multiresolution C ABI exposes finite reach-aware transport metadata");
    check(ws_multiresolution_water_channel_transport(
              water, cells[land_index].cell_x, cells[land_index].cell_y, NULL) == -1 &&
          strlen(ws_multiresolution_last_error()) > 0,
          "multiresolution C ABI rejects null transport output with error text");

    ws_simulation_state* simulation = ws_simulation_create(&cfg, NULL, NULL);
    check(simulation != NULL, "channel transport C ABI creates unified simulation");
    if (simulation) {
        ws_channel_transport_properties unified;
        memset(&unified, 0, sizeof(unified));
        check(ws_simulation_channel_transport(
                  simulation, cells[land_index].cell_x, cells[land_index].cell_y, &unified) == 0 &&
              valid_transport(&unified),
              "simulation C ABI exposes reach-aware transport metadata");
        check(same_transport(&low_level, &unified),
              "low-level and unified C ABI expose identical derived transport");
        check(ws_simulation_channel_transport(
                  simulation, INT64_MAX, INT64_MAX, &unified) == -1 &&
              strlen(ws_simulation_last_error()) > 0,
              "simulation C ABI transport query rejects out-of-range coordinate");
        ws_simulation_destroy(simulation);
    }

    ws_multiresolution_water_state_destroy(water);
    free(cells);
    ws_continental_hydrology_result_destroy(topology);
    ws_world_destroy(world);

    if (failures != 0) {
        fprintf(stderr, "%d channel transport C ABI test(s) failed\n", failures);
        return 1;
    }
    printf("All channel transport C ABI tests passed\n");
    return 0;
}
