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

static int forcing_safe(const ws_hydrometeorological_forcing* f) {
    return isfinite(f->precipitation_mm) && f->precipitation_mm >= 0.0f &&
        isfinite(f->mean_air_temperature_c) &&
        isfinite(f->potential_evapotranspiration_mm) &&
        f->potential_evapotranspiration_mm >= 0.0f;
}

int main(void) {
    ws_world_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seed = 13014;
    cfg.origin_x_m = -32768.0;
    cfg.origin_y_m = -32768.0;
    cfg.width_m = 65536.0;
    cfg.height_m = 65536.0;
    cfg.sea_level_m = -10000.0f;

    ws_world* world = ws_world_create(&cfg);
    check(world != NULL, "refined forcing C ABI creates world");
    if (!world) return 1;

    ws_continental_hydrology_result* topology =
        ws_world_analyze_continental_hydrology(world, 0.1f);
    check(topology != NULL, "refined forcing C ABI creates topology");
    if (!topology) {
        ws_world_destroy(world);
        return 1;
    }

    const uint64_t count = ws_continental_hydrology_cell_count(topology);
    ws_continental_hydrology_cell* cells =
        (ws_continental_hydrology_cell*)calloc((size_t)count, sizeof(*cells));
    check(cells != NULL, "refined forcing C ABI allocates topology cells");
    if (!cells) {
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }
    check(ws_continental_hydrology_copy_cells(topology, cells, count) == 0,
          "refined forcing C ABI copies topology");

    uint64_t parent = count;
    for (uint64_t i = 0; i < count; ++i) {
        if (!cells[i].ocean) {
            parent = i;
            break;
        }
    }
    check(parent != count, "refined forcing C ABI fixture has terrestrial parent");
    if (parent == count) {
        free(cells);
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }

    const int64_t px = cells[parent].cell_x;
    const int64_t py = cells[parent].cell_y;
    ws_multiresolution_water_state* water =
        ws_world_multiresolution_water_create(world, topology, NULL);
    check(water != NULL, "refined forcing C ABI creates multiresolution water");
    check(water != NULL &&
          ws_multiresolution_water_materialize(world, topology, water, px, py) == 0,
          "refined forcing C ABI materializes explicit parent");

    ws_continental_water_forcing parent_forcing;
    parent_forcing.precipitation_mm = 20.0f;
    parent_forcing.mean_air_temperature_c = 10.0f;
    parent_forcing.potential_evapotranspiration_mm = 1.5f;
    ws_hydrometeorological_forcing derived[WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT];
    memset(derived, 0, sizeof(derived));

    const int64_t day_before = ws_multiresolution_water_simulated_day(water);
    check(ws_multiresolution_water_derive_refined_forcing(
              world, water, px, py, &parent_forcing, derived,
              WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT - 1) == -1,
          "standalone C ABI rejects undersized refined forcing output");
    check(strlen(ws_multiresolution_last_error()) > 0,
          "standalone C ABI exposes refined forcing capacity error");
    check(ws_multiresolution_water_derive_refined_forcing(
              world, water, px, py, &parent_forcing, derived,
              WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) == 0,
          "standalone C ABI derives refined atmospheric forcing");
    check(ws_multiresolution_water_simulated_day(water) == day_before,
          "standalone refined forcing query does not advance water clock");

    ws_dynamic_hydrology_cell_state refined[WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT];
    memset(refined, 0, sizeof(refined));
    check(ws_multiresolution_water_copy_refined_cells(
              water, px, py, refined, WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) == 0,
          "standalone C ABI copies refined state for forcing alignment");
    int safe = 1;
    int varied = 0;
    float first_temp = 0.0f;
    int have_first = 0;
    for (uint64_t i = 0; i < WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT; ++i) {
        if (!refined[i].active) continue;
        safe = safe && forcing_safe(&derived[i]) &&
            derived[i].cell_x == refined[i].cell_x &&
            derived[i].cell_y == refined[i].cell_y;
        if (!have_first) {
            first_temp = derived[i].mean_air_temperature_c;
            have_first = 1;
        } else if (derived[i].mean_air_temperature_c != first_temp) {
            varied = 1;
        }
    }
    check(safe, "standalone refined forcing C ABI is finite and coordinate-aligned");
    check(varied, "standalone refined forcing C ABI exposes terrain-dependent temperature");

    parent_forcing.precipitation_mm = NAN;
    check(ws_multiresolution_water_derive_refined_forcing(
              world, water, px, py, &parent_forcing, derived,
              WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) == -1 &&
          ws_multiresolution_water_simulated_day(water) == day_before,
          "standalone refined forcing C ABI rejects NaN without clock mutation");

    ws_simulation_state* simulation = ws_simulation_create(&cfg, NULL, NULL);
    check(simulation != NULL, "unified refined forcing C ABI creates simulation");
    if (simulation) {
        check(ws_simulation_materialize_refined_water_tile(simulation, px, py) == 0,
              "unified refined forcing C ABI materializes parent");
        const int64_t simulation_day = ws_simulation_simulated_day(simulation);
        memset(derived, 0, sizeof(derived));
        check(ws_simulation_copy_refined_daily_forcing(
                  simulation, px, py, derived,
                  WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT - 1) == -1,
              "unified C ABI rejects undersized refined daily forcing output");
        check(ws_simulation_copy_refined_daily_forcing(
                  simulation, px, py, derived,
                  WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT) == 0,
              "unified C ABI exposes current WeatherState-derived L1 forcing");
        check(ws_simulation_simulated_day(simulation) == simulation_day &&
              ws_simulation_materialized_patch_count(simulation) == 0,
              "unified refined forcing query is clock-pure and non-materializing");

        ws_weather_cell_sample parent_weather;
        memset(&parent_weather, 0, sizeof(parent_weather));
        check(ws_simulation_sample_weather(simulation, px, py, &parent_weather) == 0,
              "unified C ABI reads authoritative parent weather");
        safe = 1;
        varied = 0;
        have_first = 0;
        for (uint64_t i = 0; i < WS_DYNAMIC_HYDROLOGY_TILE_CELL_COUNT; ++i) {
            if (!forcing_safe(&derived[i])) {
                // Inactive slots are zero and therefore safe too.
                safe = 0;
            }
            if (derived[i].precipitation_mm > 0.0f ||
                derived[i].potential_evapotranspiration_mm > 0.0f ||
                derived[i].mean_air_temperature_c != 0.0f) {
                if (!have_first) {
                    first_temp = derived[i].mean_air_temperature_c;
                    have_first = 1;
                } else if (derived[i].mean_air_temperature_c != first_temp) {
                    varied = 1;
                }
            }
        }
        check(safe && isfinite(parent_weather.mean_air_temperature_c),
              "unified refined daily forcing remains finite");
        check(varied,
              "unified refined daily forcing resolves sub-parent terrain temperature");
        ws_simulation_destroy(simulation);
    }

    if (water) ws_multiresolution_water_state_destroy(water);
    free(cells);
    ws_continental_hydrology_result_destroy(topology);
    ws_world_destroy(world);

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("All refined atmospheric forcing C ABI tests passed\n");
    return 0;
}
