        DynamicHydrologyParameters snow_params = dynamic_params;
        snow_params.initial_soil_water_mm = 0.0f;
        snow_params.initial_groundwater_mm = 0.0f;
        auto snow_state = make_dynamic_hydrology_tile_state(continent_world, tile, snow_params);
        const auto snow_report = advance_dynamic_hydrology_tile(
            continent_world, tile, snow_state, cold_forcing, {}, 1.0, snow_params);
        double snow_depth_sum = 0.0;
        for (const auto& c : snow_state.cells) if (c.active) snow_depth_sum += c.snow_water_equivalent_mm;
        check(snow_depth_sum > 0.0 && std::abs(snow_report.water_balance_error_m3) < 2.0,
              "sub-freezing precipitation accumulates as snow without breaking water balance");

        auto thaw_forcing = cold_forcing;
        for (auto& f : thaw_forcing) {
            f.precipitation_mm = 0.0f;
            f.mean_air_temperature_c = 6.0f;
            f.potential_evapotranspiration_mm = 0.0f;
        }
        const auto thaw_report = advance_dynamic_hydrology_tile(
            continent_world, tile, snow_state, thaw_forcing, {}, 2.0, snow_params);
        double snow_after_thaw = 0.0;
        double soil_after_thaw = 0.0;
        for (const auto& c : snow_state.cells) if (c.active) {
            snow_after_thaw += c.snow_water_equivalent_mm;
            soil_after_thaw += c.soil_water_mm;
        }
        check(snow_after_thaw < snow_depth_sum && soil_after_thaw > 0.0 &&
              std::abs(thaw_report.water_balance_error_m3) < 2.0,
              "positive temperature melts snow into the liquid/soil water cycle conservatively");

        DynamicHydrologyParameters route_params = dynamic_params;
        route_params.initial_soil_water_mm = 0.0f;
        route_params.initial_groundwater_mm = 0.0f;
        auto route_state = make_dynamic_hydrology_tile_state(continent_world, tile, route_params);
        std::vector<HydrometeorologicalForcing> zero_forcing;
        zero_forcing.reserve(tile.hydrology.cells.size());
        CellCoord injection_coord{};
        bool have_injection = false;
        for (const auto& c : tile.hydrology.cells) {
            zero_forcing.push_back({c.coord, 0.0f, 10.0f, 0.0f});
            if (c.active && !have_injection) { injection_coord = c.coord; have_injection = true; }
        }
        const double injected_m3 = 123'456.0;
        const auto route_report = advance_dynamic_hydrology_tile(
            continent_world, tile, route_state, zero_forcing,
            {{injection_coord, injected_m3}}, 1.0, route_params);
        check(std::abs(route_report.external_inflow_m3 - injected_m3) < 1e-6 &&
              std::abs(route_report.external_outflow_m3 - injected_m3) < 0.1 &&
              std::abs(route_report.water_balance_error_m3) < 0.1,
              "external channel inflow routes to the authoritative tile outlet without water loss");


        auto downstream_dynamic_state = make_dynamic_hydrology_tile_state(
            continent_world, downstream_tile, route_params);
        std::vector<HydrometeorologicalForcing> downstream_zero_forcing;
        downstream_zero_forcing.reserve(downstream_tile.hydrology.cells.size());
        for (const auto& c : downstream_tile.hydrology.cells) {
            downstream_zero_forcing.push_back({c.coord, 0.0f, 10.0f, 0.0f});
        }
        const auto handoff_report = advance_dynamic_hydrology_tile(
            continent_world, downstream_tile, downstream_dynamic_state, downstream_zero_forcing,
            {{external_target, route_report.external_outflow_m3}}, 1.0, route_params);
        check(std::abs(handoff_report.external_inflow_m3 - injected_m3) < 0.1 &&
              std::abs(handoff_report.external_outflow_m3 - injected_m3) < 0.2 &&
              std::abs(handoff_report.water_balance_error_m3) < 0.1,
              "dynamic upstream tile outflow hands off to the authoritative downstream ingress conservatively");

        auto climate_state = make_dynamic_hydrology_tile_state(continent_world, tile, dynamic_params);
        double max_daily_balance_error = 0.0;
        bool nonnegative_stores = true;
        for (int day = 0; day < 365; ++day) {
            auto daily = make_smooth_climatological_forcing(continent_world, tile, static_cast<double>(day), 1.0);
            const auto day_report = advance_dynamic_hydrology_tile(
                continent_world, tile, climate_state, daily, {}, 1.0, dynamic_params);
            max_daily_balance_error = std::max(max_daily_balance_error, std::abs(day_report.water_balance_error_m3));
            for (const auto& c : climate_state.cells) {
                nonnegative_stores = nonnegative_stores && c.snow_water_equivalent_mm >= 0.0f &&
                    c.surface_water_mm >= 0.0f && c.soil_water_mm >= 0.0f && c.groundwater_mm >= 0.0f;
            }
        }
        check(nonnegative_stores && max_daily_balance_error < 3.0 &&
              std::abs(climate_state.simulated_days - 365.0) < 1e-9,
              "one-year daily dynamic hydrology keeps non-negative stores and closes daily water balance");


        bool wrong_world_dynamic_threw = false;
        try {
            auto wrong_cfg = continent_cfg;
            wrong_cfg.seed += 1;
            World wrong_world(wrong_cfg);
            (void)make_smooth_climatological_forcing(wrong_world, tile, 0.0, 1.0);
        } catch (const std::invalid_argument&) { wrong_world_dynamic_threw = true; }
        check(wrong_world_dynamic_threw,
              "dynamic hydrology rejects authoritative topology from a different world");
    }

    bool stale_continent_threw = false;
    try {
        World other(cfg(18));
        (void)other.refine_authoritative_hydrology_tile(continent, refinable_parent);
    } catch (const std::invalid_argument&) { stale_continent_threw = true; }
    check(stale_continent_threw, "regional refinement rejects continental topology from another world");

    bool continent_too_large_threw = false;
    try {
        WorldConfig huge;
        huge.bounds = {0.0, 0.0, 8'200'000.0, 8'200'000.0};
        World huge_world(huge);
        (void)huge_world.analyze_continental_hydrology();
    } catch (const std::invalid_argument&) { continent_too_large_threw = true; }
    check(continent_too_large_threw, "continental hydrology rejects worlds above its bounded L0 allocation limit");

    bool sea_level_invalid_threw = false;
    try {
        auto invalid = cfg();
        invalid.sea_level_m = std::numeric_limits<float>::infinity();
        World invalid_world(invalid);
        (void)invalid_world;
    } catch (const std::invalid_argument&) { sea_level_invalid_threw = true; }
    check(sea_level_invalid_threw, "non-finite sea level is rejected");

    // Backward compatibility: v0.3 loader accepts a minimal v1 save and assigns the old
    // implicit sea-level datum of 0 m.
    const auto v1_save = std::filesystem::temp_directory_path() / "worldsim_v1_compat.ws";
    {
        std::ofstream out(v1_save, std::ios::binary | std::ios::trunc);
        const char magic[8] = {'W','S','I','M','0','0','0','1'};
        const std::uint32_t version = 1;
        const auto old = cfg(333);
        const std::uint64_t patch_count = 0;
        out.write(magic, 8);
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&old.seed), sizeof(old.seed));
        out.write(reinterpret_cast<const char*>(&old.bounds.origin_x_m), sizeof(double));
        out.write(reinterpret_cast<const char*>(&old.bounds.origin_y_m), sizeof(double));
        out.write(reinterpret_cast<const char*>(&old.bounds.width_m), sizeof(double));
        out.write(reinterpret_cast<const char*>(&old.bounds.height_m), sizeof(double));
        out.write(reinterpret_cast<const char*>(&old.local_cell_m), sizeof(old.local_cell_m));
        out.write(reinterpret_cast<const char*>(&old.regional_cell_m), sizeof(old.regional_cell_m));
        out.write(reinterpret_cast<const char*>(&old.climate_cell_m), sizeof(old.climate_cell_m));
        out.write(reinterpret_cast<const char*>(&patch_count), sizeof(patch_count));
    }
    const auto v1_loaded = World::load(v1_save);
    check(near(v1_loaded.config().sea_level_m, 0.0f), "v1 save compatibility restores implicit 0 m sea level");
    std::filesystem::remove(v1_save);

    bool invalid_cfg_threw = false;
    try {
        auto invalid = cfg();
        invalid.bounds.origin_x_m = std::numeric_limits<double>::infinity();
        World invalid_world(invalid);
        (void)invalid_world;
    } catch (const std::invalid_argument&) { invalid_cfg_threw = true; }
    check(invalid_cfg_threw, "non-finite world bounds are rejected");

    bool imprecise_cfg_threw = false;
    try {
        auto invalid = cfg();
        invalid.bounds.origin_x_m = 0x1p58;
        World invalid_world(invalid);
        (void)invalid_world;
    } catch (const std::invalid_argument&) { imprecise_cfg_threw = true; }
    check(imprecise_cfg_threw, "world bounds that exceed local-coordinate precision are rejected");

    // Corrupted saves with duplicate patch coordinates must be rejected instead of silently
    // collapsing data in unordered_map.
    const auto duplicate_save = std::filesystem::temp_directory_path() / "worldsim_duplicate.ws";
    const auto duplicate_bad = std::filesystem::temp_directory_path() / "worldsim_duplicate_bad.ws";
    World duplicate_world(cfg(901));
    (void)duplicate_world.materialize_local_patch({0, 0});
    duplicate_world.save(duplicate_save);
    std::vector<char> duplicate_bytes;
    {
        std::ifstream duplicate_in(duplicate_save, std::ios::binary);
        duplicate_bytes.assign(std::istreambuf_iterator<char>(duplicate_in), {});
    }
    constexpr std::size_t count_offset = 8 + sizeof(std::uint32_t) + sizeof(Seed) +
        sizeof(double) * 4 + sizeof(std::int32_t) * 3 + sizeof(float);
    constexpr std::size_t records_offset = count_offset + sizeof(std::uint64_t);
    check(duplicate_bytes.size() > records_offset, "save fixture contains one patch record");
    if (duplicate_bytes.size() > records_offset) {
        const std::uint64_t duplicate_count = 2;
        std::memcpy(duplicate_bytes.data() + count_offset, &duplicate_count, sizeof(duplicate_count));
        const std::vector<char> duplicate_record(duplicate_bytes.begin() + static_cast<std::ptrdiff_t>(records_offset),
                                                 duplicate_bytes.end());
        duplicate_bytes.insert(duplicate_bytes.end(), duplicate_record.begin(), duplicate_record.end());
        std::ofstream duplicate_out(duplicate_bad, std::ios::binary | std::ios::trunc);
        duplicate_out.write(duplicate_bytes.data(), static_cast<std::streamsize>(duplicate_bytes.size()));
        duplicate_out.close();
        bool duplicate_threw = false;
        try { (void)World::load(duplicate_bad); }
        catch (const std::runtime_error&) { duplicate_threw = true; }
        check(duplicate_threw, "duplicate patch coordinates in save are rejected");
    }
    std::filesystem::remove(duplicate_save);
    std::filesystem::remove(duplicate_bad);

    const auto trailing_save = std::filesystem::temp_directory_path() / "worldsim_trailing.ws";
    World trailing_world(cfg(902));
    trailing_world.save(trailing_save);
    {
        std::ofstream extra(trailing_save, std::ios::binary | std::ios::app);
        const char junk = 'X';
        extra.write(&junk, 1);
    }
    bool trailing_threw = false;
    try { (void)World::load(trailing_save); }
    catch (const std::runtime_error&) { trailing_threw = true; }
    check(trailing_threw, "save loader rejects unexpected trailing bytes");
    std::filesystem::remove(trailing_save);

    bool threw = false;
    try { (void)d.sample_region(WorldPosition{100'000.0, 100'000.0}); }
    catch (const std::out_of_range&) { threw = true; }
    check(threw, "out-of-bounds regional query is rejected");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All WorldSim tests passed\n";
    return 0;
}