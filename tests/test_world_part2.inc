        const auto dy = std::llabs(segment.from.y - segment.to.y);
        rivers_valid = rivers_valid && source.river && segment.discharge_m3_s >= hydro_request.river_threshold_m3_s &&
                       dx <= 1 && dy <= 1 && (dx + dy) > 0;
    }
    check(rivers_valid, "river graph follows adjacent drainage edges above discharge threshold");

    // Property-style regression across several independent terrains. This is intentionally
    // small enough for the normal suite while making drainage invariants less fixture-specific.
    bool hydro_properties_ok = true;
    for (Seed seed : {3ULL, 17ULL, 99ULL, 501ULL, 7'777ULL}) {
        WorldConfig property_cfg;
        property_cfg.seed = seed;
        property_cfg.bounds = {-64'000.0, -64'000.0, 128'000.0, 128'000.0};
        World property_world(property_cfg);
        HydrologyRequest r;
        r.min_coord = {-20, -18};
        r.width_cells = 41;
        r.height_cells = 37;
        r.river_threshold_m3_s = 0.15f;
        r.lake_min_depth_m = 0.1f;
        const auto h = property_world.analyze_hydrology(r);

        double yield = 0.0;
        double outflow = 0.0;
        for (const auto& cell : h.cells) {
            yield += cell.local_water_yield_m3_s;
            hydro_properties_ok = hydro_properties_ok && cell.catchment_id != 0 &&
                                  cell.filled_elevation_m + 1e-5f >= cell.terrain_elevation_m;
            if (!cell.has_downstream) {
                outflow += cell.accumulated_discharge_m3_s;
            } else {
                const auto& down = h.cell(cell.downstream_coord);
                hydro_properties_ok = hydro_properties_ok &&
                                      down.filled_elevation_m <= cell.filled_elevation_m + 1e-5f;
            }

            const HydrologyCell* cursor = &cell;
            std::size_t steps = 0;
            while (cursor->has_downstream && steps <= h.cells.size()) {
                cursor = &h.cell(cursor->downstream_coord);
                ++steps;
            }
            hydro_properties_ok = hydro_properties_ok && steps <= h.cells.size();
        }
        hydro_properties_ok = hydro_properties_ok &&
            std::abs(yield - outflow) <= std::max(1e-4, yield * 1e-5);
    }
    check(hydro_properties_ok, "hydrology invariants hold across multiple deterministic terrains");

    bool thin_domain_ok = true;
    for (const auto dims : {std::pair<std::uint32_t, std::uint32_t>{1, 1},
                            std::pair<std::uint32_t, std::uint32_t>{1, 17},
                            std::pair<std::uint32_t, std::uint32_t>{19, 1},
                            std::pair<std::uint32_t, std::uint32_t>{2, 2}}) {
        HydrologyRequest r;
        r.min_coord = {-5, -5};
        r.width_cells = dims.first;
        r.height_cells = dims.second;
        const auto h = hydro_world.analyze_hydrology(r);
        thin_domain_ok = thin_domain_ok && h.cells.size() ==
            static_cast<std::size_t>(dims.first) * static_cast<std::size_t>(dims.second);
        for (const auto& cell : h.cells) {
            thin_domain_ok = thin_domain_ok && !cell.has_downstream && cell.catchment_id != 0;
        }
    }
    check(thin_domain_ok, "hydrology handles all-boundary thin domains without cycles");

    bool hydro_oob_threw = false;
    try {
        HydrologyRequest bad = hydro_request;
        bad.min_coord = {120, 120};
        (void)hydro_world.analyze_hydrology(bad);
    } catch (const std::out_of_range&) { hydro_oob_threw = true; }
    check(hydro_oob_threw, "hydrology rejects regions outside world bounds");

    bool hydro_too_large_threw = false;
    try {
        HydrologyRequest too_large;
        too_large.width_cells = 513;
        too_large.height_cells = 513;
        too_large.validate();
    } catch (const std::invalid_argument&) { hydro_too_large_threw = true; }
    check(hydro_too_large_threw, "hydrology rejects a single analysis above its memory-bounding cell limit");



    WorldConfig ocean_cfg;
    ocean_cfg.seed = 42;
    ocean_cfg.bounds = {0.0, 0.0, 16'384.0, 16'384.0};
    ocean_cfg.sea_level_m = 10'000.0f;
    World ocean_world(ocean_cfg);
    const auto ocean_continent = ocean_world.analyze_continental_hydrology();
    bool all_ocean = !ocean_continent.cells.empty();
    for (const auto& cell : ocean_continent.cells) {
        all_ocean = all_ocean && cell.ocean && !cell.has_downstream &&
                    near(cell.local_water_yield_m3_s, 0.0f, 1e-7f) &&
                    near(cell.filled_elevation_m, ocean_cfg.sea_level_m, 1e-4f);
    }
    check(all_ocean, "sea level is an authoritative ocean boundary condition");

    // v0.3 whole-world L0 drainage: topology must be independent of arbitrary regional
    // query rectangles, conserve water, and provide stable basin/outlet identities.
    WorldConfig continent_cfg;
    continent_cfg.seed = 17;
    continent_cfg.bounds = {-120'000.0, -100'000.0, 240'000.0, 200'000.0}; // intentionally not L0-aligned
    continent_cfg.sea_level_m = 0.0f;
    World continent_world(continent_cfg);
    ContinentalHydrologyRequest continent_request;
    continent_request.river_threshold_m3_s = 0.1f;
    const auto continent = continent_world.analyze_continental_hydrology(continent_request);
    const auto continent_again = continent_world.analyze_continental_hydrology(continent_request);
    check(!continent.cells.empty(), "continental hydrology covers configured world");
    check(continent.cells.size() == continent_again.cells.size(), "continental hydrology dimensions are deterministic");
    check(continent_world.materialized_patch_count() == 0, "continental hydrology does not materialize L2");

    double continent_local = 0.0;
    double continent_out = 0.0;
    bool continent_ok = true;
    bool has_land_flow = false;
    CellCoord refinable_parent{};
    std::vector<std::uint64_t> terminal_basin_ids;
    for (std::size_t i = 0; i < continent.cells.size(); ++i) {
        const auto& cell = continent.cells[i];
        const auto& same = continent_again.cells[i];
        continent_local += cell.local_water_yield_m3_s;
        if (!cell.has_downstream) {
            continent_out += cell.accumulated_discharge_m3_s;
            terminal_basin_ids.push_back(cell.basin_id);
        }
        continent_ok = continent_ok && cell.coord == same.coord && cell.basin_id != 0 &&
                       cell.terminal_outlet_coord == same.terminal_outlet_coord &&
                       cell.basin_id == same.basin_id && cell.ocean == same.ocean &&
                       cell.has_downstream == same.has_downstream &&
                       near(cell.accumulated_discharge_m3_s, same.accumulated_discharge_m3_s, 1e-5f);
        if (cell.ocean) {
            continent_ok = continent_ok && !cell.has_downstream &&
                           near(cell.filled_elevation_m, continent_cfg.sea_level_m, 1e-5f) &&
                           near(cell.local_water_yield_m3_s, 0.0f, 1e-7f);
        }
        if (cell.has_downstream) {
            const auto& down = continent.cell(cell.downstream_coord);
            continent_ok = continent_ok && down.filled_elevation_m <= cell.filled_elevation_m + 1e-4f &&
                           down.basin_id == cell.basin_id &&
                           down.terminal_outlet_coord == cell.terminal_outlet_coord;
            if (!cell.ocean && !has_land_flow) {
                has_land_flow = true;
                refinable_parent = cell.coord;
            }
        }

        const ContinentalHydrologyCell* cursor = &cell;
        std::size_t steps = 0;
        while (cursor->has_downstream && steps <= continent.cells.size()) {
            cursor = &continent.cell(cursor->downstream_coord);
            ++steps;
        }
        continent_ok = continent_ok && steps <= continent.cells.size() &&
                       cursor->coord == cell.terminal_outlet_coord && cursor->basin_id == cell.basin_id;
    }
    std::sort(terminal_basin_ids.begin(), terminal_basin_ids.end());
    check(std::adjacent_find(terminal_basin_ids.begin(), terminal_basin_ids.end()) == terminal_basin_ids.end(),
          "terminal continental basins have collision-free IDs");
    check(continent_ok, "continental drainage paths are deterministic, acyclic and basin-consistent");
    check(std::abs(continent_local - continent_out) <= std::max(1e-4, continent_local * 2e-5),
          "continental hydrology conserves climatological water yield");
    check(has_land_flow, "continental fixture contains a refinable land drainage edge");

    if (has_land_flow) {
        const auto tile = continent_world.refine_authoritative_hydrology_tile(continent, refinable_parent, 0.1f, 0.1f);
        const auto tile_again = continent_world.refine_authoritative_hydrology_tile(continent, refinable_parent, 0.1f, 0.1f);
        const auto& parent = continent.cell(refinable_parent);
        double tile_local = 0.0;
        double tile_out = 0.0;
        std::size_t external_edges = 0;
        bool tile_ok = tile.hydrology.cells.size() == 64 && tile.hydrology.cells.size() == tile_again.hydrology.cells.size();
        CellCoord external_target{};
        for (std::size_t i = 0; i < tile.hydrology.cells.size(); ++i) {
            const auto& cell = tile.hydrology.cells[i];
            const auto& same = tile_again.hydrology.cells[i];
            tile_ok = tile_ok && cell.coord == same.coord && cell.active == same.active &&
                      cell.has_downstream == same.has_downstream &&
                      cell.downstream_coord == same.downstream_coord &&
                      cell.downstream_is_external == same.downstream_is_external;
            if (!cell.active) continue;
            tile_local += cell.local_water_yield_m3_s;
            tile_ok = tile_ok && cell.catchment_id == parent.basin_id;
            if (!cell.has_downstream || cell.downstream_is_external) {
                tile_out += cell.accumulated_discharge_m3_s;
            }
            if (cell.downstream_is_external) {
                ++external_edges;
                external_target = cell.downstream_coord;
            }
        }
        check(tile_ok, "authoritative L1 refinement is deterministic and inherits the global basin");
        check(std::abs(tile_local - parent.local_water_yield_m3_s) <= std::max(1e-5, static_cast<double>(parent.local_water_yield_m3_s) * 2e-5),
              "L1 refinement conserves its parent L0 local water yield");
        check(std::abs(tile_out - parent.accumulated_discharge_m3_s) <= std::max(1e-4, static_cast<double>(parent.accumulated_discharge_m3_s) * 2e-5),
              "L1 authoritative outlet carries the parent L0 accumulated discharge");
        check(external_edges == 1, "non-terminal authoritative tile has exactly one external downstream edge");

        const auto target_climate = regional_to_climate(external_target, continent_cfg);
        check(target_climate == parent.downstream_coord, "L1 external edge enters the authoritative downstream L0 tile");
        const auto downstream_tile = continent_world.refine_authoritative_hydrology_tile(continent, parent.downstream_coord, 0.1f, 0.1f);
        const auto& ingress = downstream_tile.hydrology.cell(external_target);
        check(ingress.active, "upstream authoritative outlet targets an active downstream L1 cell");

        // v0.4 dynamic tile water cycle: snow/surface/soil/groundwater stores are explicit,
        // all fluxes remain conservative, and channel inflow follows authoritative topology.
        DynamicHydrologyParameters dynamic_params;
        auto dynamic_state = make_dynamic_hydrology_tile_state(continent_world, tile, dynamic_params);
        check(dynamic_state.cells.size() == 64 && near(static_cast<float>(dynamic_state.simulated_days), 0.0f),
              "dynamic hydrology initializes one state record per authoritative L1 cell");

        auto cold_forcing = make_smooth_climatological_forcing(continent_world, tile, 15.0, 1.0);
        for (auto& f : cold_forcing) {
            f.precipitation_mm = 20.0f;
            f.mean_air_temperature_c = -5.0f;
            f.potential_evapotranspiration_mm = 0.0f;
        }
