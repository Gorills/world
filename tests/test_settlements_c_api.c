#include "worldsim/simulation_c_api.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(int condition, const char* message) {
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s; error=%s\n", message, ws_simulation_last_error());
    }
}

int main(void) {
    ws_world_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seed = 4016;
    cfg.origin_x_m = -32768.0;
    cfg.origin_y_m = -32768.0;
    cfg.width_m = 65536.0;
    cfg.height_m = 65536.0;
    cfg.sea_level_m = -10000.0f;

    ws_simulation_state* state = ws_simulation_create(&cfg, NULL, NULL);
    check(state != NULL, "C ABI creates simulation");
    if (!state) return 1;

    ws_settlement_suitability suitability;
    memset(&suitability, 0, sizeof(suitability));
    check(ws_simulation_settlement_suitability(state, 0, 0, &suitability) == 0 &&
          isfinite(suitability.environmental_capacity) &&
          ws_simulation_settlement_count(state) == 0 &&
          ws_simulation_materialized_patch_count(state) == 0,
          "C ABI suitability is read-only and non-materializing");

    uint64_t id = 0;
    check(ws_simulation_found_settlement(state, 0, 0, 100.0, &id) == 0 && id == 1,
          "C ABI founds settlement with deterministic id");
    check(ws_simulation_settlement_count(state) == 1,
          "C ABI exposes settlement count");

    ws_settlement value;
    memset(&value, 0, sizeof(value));
    check(ws_simulation_settlement(state, id, &value) == 0 &&
          value.id == id && value.regional_x == 0 && value.regional_y == 0 &&
          value.population == 100.0 && value.founded_day == 0,
          "C ABI queries settlement by id");
    check(ws_simulation_copy_settlements(state, &value, 0) == -1,
          "C ABI rejects undersized settlement list output");
    check(ws_simulation_copy_settlements(state, &value, 1) == 0 && value.id == id,
          "C ABI lists settlements");

    ws_simulation_day_report_v3 report;
    memset(&report, 0, sizeof(report));
    check(ws_simulation_advance_day_v3(state, &report) == 0 &&
          report.environment.weather.day_after == 1 &&
          report.settlements.settlement_count == 1 &&
          isfinite(report.settlements.population_after) &&
          report.settlements.population_after >= 0.0,
          "C ABI v3 advances unified environment/vegetation/settlements");

    ws_simulation_day_report_v2 old_report;
    memset(&old_report, 0, sizeof(old_report));
    check(ws_simulation_advance_day_v2(state, &old_report) == 0 &&
          old_report.environment.weather.day_after == 2,
          "C ABI v2 remains compatible");

    ws_simulation_destroy(state);
    if (failures) return 1;
    printf("All settlement C ABI tests passed\n");
    return 0;
}
