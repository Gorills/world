#include "worldsim/weather_c_api.h"

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
    const char* save_path = "worldsim_weather_c_api.bin";
    ws_world_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seed = 1301;
    cfg.origin_x_m = -120000.0;
    cfg.origin_y_m = -100000.0;
    cfg.width_m = 240000.0;
    cfg.height_m = 200000.0;
    cfg.sea_level_m = -10000.0f;

    ws_world* world = ws_world_create(&cfg);
    check(world != NULL, "weather C ABI creates world fixture");
    if (!world) return 1;

    ws_continental_hydrology_result* topology =
        ws_world_analyze_continental_hydrology(world, 0.1f);
    check(topology != NULL, "weather C ABI creates L0 topology");
    if (!topology) {
        ws_world_destroy(world);
        return 1;
    }
    const uint64_t count = ws_continental_hydrology_cell_count(topology);
    check(count > 0, "weather C ABI fixture has L0 cells");

    ws_weather_state* weather = ws_world_weather_create(world, NULL);
    check(weather != NULL, "weather C ABI creates authoritative weather state");
    check(weather != NULL && ws_weather_cell_count(weather) == count,
          "weather C ABI raster aligns with continental L0 count");
    check(weather != NULL && ws_weather_simulated_day(weather) == 0,
          "weather C ABI clock starts at zero");

    ws_continental_hydrology_cell* topo_cells =
        (ws_continental_hydrology_cell*)calloc((size_t)count, sizeof(*topo_cells));
    ws_continental_water_forcing* forcing =
        (ws_continental_water_forcing*)calloc((size_t)count, sizeof(*forcing));
    check(topo_cells != NULL && forcing != NULL, "weather C ABI test allocations succeed");
    if (!weather || !topo_cells || !forcing) {
        free(topo_cells);
        free(forcing);
        ws_weather_state_destroy(weather);
        ws_continental_hydrology_result_destroy(topology);
        ws_world_destroy(world);
        return 1;
    }

    check(ws_continental_hydrology_copy_cells(topology, topo_cells, count) == 0,
          "weather C ABI copies L0 topology");
    check(ws_weather_copy_daily_forcing(weather, forcing, count - 1) == -1,
          "weather C ABI rejects undersized forcing output");
    check(strlen(ws_weather_last_error()) > 0,
          "weather C ABI exposes forcing-capacity error text");
    check(ws_weather_copy_daily_forcing(weather, forcing, count) == 0,
          "weather C ABI copies current daily forcing");

    int any_wet = 0;
    int any_dry = 0;
    int finite_forcing = 1;
    for (uint64_t i = 0; i < count; ++i) {
        finite_forcing = finite_forcing && isfinite(forcing[i].precipitation_mm) &&
            forcing[i].precipitation_mm >= 0.0f && isfinite(forcing[i].mean_air_temperature_c) &&
            isfinite(forcing[i].potential_evapotranspiration_mm) &&
            forcing[i].potential_evapotranspiration_mm >= 0.0f;
        any_wet = any_wet || forcing[i].precipitation_mm > 0.0f;
        any_dry = any_dry || forcing[i].precipitation_mm == 0.0f;
    }
    check(finite_forcing, "weather C ABI forcing is finite and hydrology-safe");
    check(any_wet && any_dry, "weather C ABI exposes coherent wet/dry pattern");

    ws_weather_cell_sample sample;
    memset(&sample, 0, sizeof(sample));
    check(ws_weather_sample_cell(weather, topo_cells[0].cell_x, topo_cells[0].cell_y, &sample) == 0,
          "weather C ABI samples one L0 weather cell");
    check(sample.cell_x == topo_cells[0].cell_x && sample.cell_y == topo_cells[0].cell_y &&
          isfinite(sample.mean_air_temperature_c) && sample.precipitation_mm >= 0.0f,
          "weather C ABI cell sample is coordinate-aligned and finite");

    ws_multiresolution_water_state* water =
        ws_world_multiresolution_water_create(world, topology, NULL);
    check(water != NULL, "weather C ABI creates coupled multiresolution water state");
    if (water) {
        check(ws_multiresolution_water_materialize(
                  world, topology, water, topo_cells[0].cell_x, topo_cells[0].cell_y) == 0,
              "weather C ABI fixture materializes one refined water parent");
        for (int day = 0; day < 5; ++day) {
            ws_weather_water_step_report report;
            memset(&report, 0, sizeof(report));
            check(ws_weather_multiresolution_water_advance_day(
                      world, weather, water, &report) == 0,
                  "weather C ABI advances atomic weather-driven multiresolution day");
            check(report.weather.day_before == day && report.weather.day_after == day + 1 &&
                  report.water.day_before == day && report.water.day_after == day + 1 &&
                  ws_weather_simulated_day(weather) == day + 1 &&
                  ws_multiresolution_water_simulated_day(water) == day + 1,
                  "weather C ABI keeps weather and water clocks exact");
            check(isfinite(report.water.water_balance_error_m3),
                  "weather C ABI coupled water report remains finite");
        }
    }

    check(ws_weather_save(weather, save_path) == 0,
          "weather C ABI saves transient atmosphere");
    ws_weather_state* loaded = ws_weather_load(world, save_path);
    check(loaded != NULL, "weather C ABI reloads transient atmosphere");
    if (loaded) {
        check(ws_weather_simulated_day(loaded) == ws_weather_simulated_day(weather) &&
              ws_weather_cell_count(loaded) == ws_weather_cell_count(weather),
              "weather C ABI persistence preserves clock and raster size");
        ws_continental_water_forcing* loaded_forcing =
            (ws_continental_water_forcing*)calloc((size_t)count, sizeof(*loaded_forcing));
        check(loaded_forcing != NULL, "weather C ABI reload forcing allocation succeeds");
        if (loaded_forcing) {
            check(ws_weather_copy_daily_forcing(loaded, loaded_forcing, count) == 0 &&
                  ws_weather_copy_daily_forcing(weather, forcing, count) == 0,
                  "weather C ABI reads forcing from original and reloaded states");
            int equal = 1;
            for (uint64_t i = 0; i < count; ++i) {
                equal = equal && loaded_forcing[i].precipitation_mm == forcing[i].precipitation_mm &&
                    loaded_forcing[i].mean_air_temperature_c == forcing[i].mean_air_temperature_c &&
                    loaded_forcing[i].potential_evapotranspiration_mm ==
                        forcing[i].potential_evapotranspiration_mm;
            }
            check(equal, "weather C ABI reload preserves exact future forcing");
            free(loaded_forcing);
        }
        ws_weather_state_destroy(loaded);
    }

    remove(save_path);
    ws_multiresolution_water_state_destroy(water);
    ws_weather_state_destroy(weather);
    free(topo_cells);
    free(forcing);
    ws_continental_hydrology_result_destroy(topology);
    ws_world_destroy(world);

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("All weather C ABI tests passed\n");
    return 0;
}
