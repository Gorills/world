#include "worldsim/soil_c_api.h"

#include <math.h>
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
    cfg.seed = 907;
    cfg.origin_x_m = -12345.0;
    cfg.origin_y_m = -9876.0;
    cfg.width_m = 38765.0;
    cfg.height_m = 31234.0;
    cfg.sea_level_m = -10000.0f;

    ws_world* world = ws_world_create(&cfg);
    check(world != NULL, "soil C ABI creates world fixture");
    if (!world) return 1;

    ws_soil_properties a;
    ws_soil_properties b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    check(ws_world_sample_soil(world, cfg.origin_x_m + 1.0, cfg.origin_y_m + 1.0, &a) == 0,
          "soil C ABI samples regional properties");
    check(isfinite(a.storage_capacity_scale) && a.storage_capacity_scale > 0.0f &&
          isfinite(a.infiltration_capacity_scale) && a.infiltration_capacity_scale > 0.0f,
          "soil C ABI returns finite positive scales");
    check(ws_world_sample_soil(world, cfg.origin_x_m + 1.0, cfg.origin_y_m + 1.0, &b) == 0 &&
          a.storage_capacity_scale == b.storage_capacity_scale &&
          a.infiltration_capacity_scale == b.infiltration_capacity_scale,
          "soil C ABI sampling is deterministic");

    ws_soil_properties parent;
    memset(&parent, 0, sizeof(parent));
    check(ws_world_sample_climate_soil(world, 0, 0, &parent) == 0,
          "soil C ABI samples parent-equivalent climate properties");
    check(isfinite(parent.storage_capacity_scale) && parent.storage_capacity_scale > 0.0f &&
          isfinite(parent.infiltration_capacity_scale) && parent.infiltration_capacity_scale > 0.0f,
          "soil C ABI parent properties are finite and positive");

    check(ws_world_sample_soil(world, cfg.origin_x_m - 1.0, cfg.origin_y_m, &a) == -1,
          "soil C ABI rejects out-of-world position");
    check(strlen(ws_soil_last_error()) > 0,
          "soil C ABI exposes rejection error text");
    check(ws_world_sample_soil(world, cfg.origin_x_m + 1.0, cfg.origin_y_m + 1.0, NULL) == -1,
          "soil C ABI rejects null output");

    ws_world_destroy(world);

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("All soil C ABI tests passed\n");
    return 0;
}
