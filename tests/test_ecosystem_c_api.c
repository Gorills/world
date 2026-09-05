#include "worldsim/simulation_c_api.h"
#include <math.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, ws_simulation_last_error()); return 1; } } while (0)
int main(void) {
    ws_world_config cfg = {0};
    cfg.seed = 42;
    cfg.width_m = cfg.height_m = 16384;
    cfg.sea_level_m = -10000;
    ws_simulation_state* state = ws_simulation_create(&cfg, NULL, NULL);
    CHECK(state != NULL);
    ws_ecosystem_cell cell;
    CHECK(ws_simulation_ecosystem_cell(state, 0, 0, &cell) == 0);
    CHECK(cell.grass_carbon > 0 && cell.herbivore_carbon > 0 && cell.carnivore_carbon > 0);
    ws_ecosystem_step_report totals;
    CHECK(ws_simulation_ecosystem_totals(state, &totals) == 0);
    CHECK(totals.plant_carbon_kg > 0);
    CHECK(ws_simulation_advance_day_v4(state, NULL) == -1);
    CHECK(ws_simulation_simulated_day(state) == 0);
    ws_simulation_day_report_v4 report;
    CHECK(ws_simulation_advance_day_v4(state, &report) == 0);
    CHECK(report.ecosystem.day_after == 1 && report.environment.water.day_after == 1);
    CHECK(fabs(report.ecosystem.nitrogen_balance_error_kg) < 1e-6);
    CHECK(ws_simulation_ecosystem_cell(state, INT64_MAX, 0, &cell) == -1);
    CHECK(ws_simulation_ecosystem_cell(NULL, 0, 0, &cell) == -1);
    ws_weather_water_step_report old;
    CHECK(ws_simulation_advance_day(state, &old) == 0);
    CHECK(ws_simulation_ecosystem_totals(state, &totals) == 0 && totals.day_after == 2);
    CHECK(ws_simulation_materialized_patch_count(state) == 0);
    ws_simulation_destroy(state);
    puts("All ecosystem C ABI tests passed");
    return 0;
}
