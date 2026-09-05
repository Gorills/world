#include "worldsim/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace worldsim;
namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void budget(const EcosystemStepReport& r) {
    check(std::abs(r.carbon_balance_error_kg) <= 1.0e-12 * std::max(1.0, r.carbon_before_kg),
        "carbon budget drift");
    check(std::abs(r.nitrogen_balance_error_kg) <= 1.0e-12 * std::max(1.0, r.nitrogen_before_kg),
        "nitrogen budget drift");
}
EcosystemCell initial() { return {0.3, 0.4, 3.0, 0.002, 0.00008, 1.0, 0.03}; }
WorldConfig config(Seed seed) {
    WorldConfig c;
    c.seed = seed;
    c.bounds = {-12000.0, -6000.0, 28000.0, 22000.0};
    c.sea_level_m = -10000;
    return c;
}
std::vector<char> bytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), {}};
}
void save_bytes(const std::filesystem::path& p, const std::vector<char>& b) {
    std::ofstream out(p, std::ios::binary);
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
}
void kernel() {
    const EcosystemHabitat habitat{1.0, {0.65, 1.5, 8.0}, 200};
    auto healthy = initial();
    auto dry = healthy;
    auto frozen = healthy;
    auto dead = EcosystemCell{};
    auto no_nutrients = initial();
    no_nutrients.mineral_nitrogen = no_nutrients.litter_carbon = 0;
    const auto limited = advance_ecosystem_cell(no_nutrients, habitat, {20, 0.7, 0});
    check(limited.photosynthesis_kg == 0, "plants grew without mineral nutrients");
    const double start_n = healthy.mineral_nitrogen +
        (healthy.grass_carbon + healthy.shrub_carbon + healthy.tree_carbon +
        healthy.herbivore_carbon + healthy.carnivore_carbon + healthy.litter_carbon) / 30;
    for (int day = 0; day < 36500; ++day) {
        const auto r = advance_ecosystem_cell(healthy, habitat, {20, 0.7, 0});
        budget(r);
        if (day < 3650) {
            budget(advance_ecosystem_cell(dry, habitat, {40, 0, 0}));
            budget(advance_ecosystem_cell(frozen, habitat, {-40, 0.7, 300}));
        }
    }
    std::cout << "100-year productive cell: grass=" << healthy.grass_carbon
        << " shrubs=" << healthy.shrub_carbon << " trees=" << healthy.tree_carbon
        << " herbivores=" << healthy.herbivore_carbon << " predators=" << healthy.carnivore_carbon << '\n';
    check(healthy.grass_carbon > 1.0e-5 && healthy.shrub_carbon > 0.001 &&
        healthy.tree_carbon > 0.01 && healthy.herbivore_carbon > 1.0e-6 &&
        healthy.carnivore_carbon > 1.0e-7, "productive food web collapsed in 100 years");
    check(dry.tree_carbon < healthy.tree_carbon && frozen.grass_carbon < healthy.grass_carbon,
        "drought/frost did not suppress vegetation");
    check(dry.herbivore_carbon < healthy.herbivore_carbon, "starvation did not reduce animals");
    budget(advance_ecosystem_cell(dead, habitat, {20, 1, 0}));
    check(dead == EcosystemCell{}, "life spontaneously appeared without biomass or immigration");
    auto final = advance_ecosystem_cell(healthy, habitat, {20, 0.7, 0});
    check(std::abs(final.nitrogen_after_kg - start_n) < 1e-11, "century nitrogen inventory drift");
    auto invalid = healthy;
    bool threw = false;
    try { (void)advance_ecosystem_cell(invalid, habitat, {20, std::numeric_limits<double>::quiet_NaN(), 0}); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw && invalid == healthy, "invalid forcing changed ecosystem state");
    auto crowded = initial();
    crowded.herbivore_carbon = 100;
    crowded.carnivore_carbon = 100;
    for (int i = 0; i < 1000; ++i) budget(advance_ecosystem_cell(crowded, habitat, {20, 0.5, 0}));
}
void centuries() {
    for (Seed seed : {Seed{42}, Seed{14002}, Seed{4015}}) {
        SimulationState s(config(seed));
        const auto initial_totals = s.ecosystem().totals();
        double max_water_error = 0;
        for (int day = 0; day < 36500; ++day) {
            const auto r = s.advance_day_full();
            budget(r.ecosystem);
            const auto& w = r.environment.water;
            const double relative = std::abs(w.water_balance_error_m3) /
                std::max(1.0, w.storage_before_m3 + w.precipitation_m3);
            max_water_error = std::max(max_water_error, relative);
            check(relative < 2e-6, "coupled century water balance failed");
        }
        const auto end = s.ecosystem().totals();
        std::cout << "seed=" << seed << " days=" << s.simulated_day()
            << " plants=" << end.plant_carbon_kg << " herbivores=" << end.herbivore_carbon_kg
            << " predators=" << end.carnivore_carbon_kg << " water_error=" << max_water_error << '\n';
        check(end.plant_carbon_kg > 1 && end.herbivore_carbon_kg > 1 && end.carnivore_carbon_kg > 1,
            "unattended world lost a trophic level");
        check(std::abs(end.nitrogen_after_kg - initial_totals.nitrogen_after_kg) <
            1e-10 * initial_totals.nitrogen_after_kg, "world century nitrogen drift");
        check(s.settlements().empty() && s.world().materialized_patch_count() == 0 &&
            s.water().refined_tile_count() == 0, "ecology allocated local patches or humans");
    }
}
void persistence_and_edges() {
    auto cfg = config(4015);
    SimulationState s(cfg);
    auto disturbed = s;
    const auto intact = disturbed.ecosystem().totals();
    check(disturbed.disturb_surface({0, 0}, {1024, 1024}, 0.8f) > 0,
        "disturbance fixture did not affect local cells");
    const auto damage = disturbed.ecosystem().totals();
    check(damage.plant_carbon_kg < intact.plant_carbon_kg &&
        std::abs(damage.carbon_after_kg - intact.carbon_after_kg) < 1e-6 &&
        std::abs(damage.nitrogen_after_kg - intact.nitrogen_after_kg) < 1e-6,
        "disturbance did not conservatively transfer plants into litter");
    const auto once = disturbed.ecosystem().cells();
    check(disturbed.disturb_surface({0, 0}, {1024, 1024}, 0.8f) == 0 &&
        disturbed.ecosystem().cells() == once,
        "repeated disturbance destroyed ecosystem biomass twice");
    const auto before = s.ecosystem().totals();
    (void)s.materialize_refined_water_tile({0, 0});
    check(s.ecosystem().totals().carbon_after_kg == before.carbon_after_kg,
        "water refinement duplicated ecosystem biomass");
    for (int i = 0; i < 90; ++i) budget(s.advance_day_full().ecosystem);
    s.aggregate_refined_water_tile({0, 0});
    (void)s.materialize_refined_water_tile({0, 0});
    const auto temp = std::filesystem::temp_directory_path();
    const auto path = temp / "worldsim_ecosystem_test.wsc";
    const auto canonical = temp / "worldsim_ecosystem_canonical.wsc";
    const auto legacy = temp / "worldsim_ecosystem_legacy.wsc";
    s.save_checkpoint(path);
    auto loaded = SimulationState::load_checkpoint(path);
    check(loaded.ecosystem().cells() == s.ecosystem().cells(), "ecosystem persistence is lossy");
    loaded.save_checkpoint(canonical);
    check(bytes(path) == bytes(canonical), "checkpoint is not canonical");
    for (int i = 0; i < 60; ++i) {
        const auto a = s.advance_day_full();
        const auto b = loaded.advance_day_full();
        budget(a.ecosystem);
        check(s.ecosystem().cells() == loaded.ecosystem().cells() &&
            a.environment.water.storage_after_m3 == b.environment.water.storage_after_m3,
            "ecosystem checkpoint future diverged");
    }
    // Retain the first four sections to emulate a genuine pre-ecology v2 container.
    const auto original = bytes(path);
    constexpr std::size_t base = 24, descriptor = 20;
    std::uint64_t payload = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        std::uint64_t size{};
        std::memcpy(&size, original.data() + base + i * descriptor + 4, sizeof(size));
        payload += size;
    }
    std::vector<char> old(original.begin(), original.begin() + base + 4 * descriptor);
    old.insert(old.end(), original.begin() + base + 5 * descriptor,
        original.begin() + base + 5 * descriptor + static_cast<std::size_t>(payload));
    const std::uint32_t v2 = 2, count = 4;
    std::memcpy(old.data() + 8, &v2, 4); std::memcpy(old.data() + 20, &count, 4);
    save_bytes(legacy, old);
    auto migrated = SimulationState::load_checkpoint(legacy);
    check(migrated.ecosystem().simulated_day() == 90 &&
        migrated.ecosystem().totals().herbivore_carbon_kg > 0, "v2 migration failed to seed ecology");
    budget(migrated.advance_day_full().ecosystem);
    auto corrupt = original;
    corrupt.back() ^= 0x40;
    save_bytes(legacy, corrupt);
    bool threw = false;
    try { (void)SimulationState::load_checkpoint(legacy); }
    catch (const std::exception&) { threw = true; }
    check(threw, "corrupt ecosystem section was accepted");
    // Exercise semantic validation independently of the compound checksum.
    const auto raw_path = temp / "worldsim_ecosystem_semantic.bin";
    s.ecosystem().save(raw_path);
    const auto raw = bytes(raw_path);
    for (double bad : {-1.0, std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity()}) {
        auto malformed = raw;
        std::memcpy(malformed.data() + malformed.size() - sizeof(double), &bad, sizeof(double));
        save_bytes(raw_path, malformed);
        bool rejected = false;
        try { (void)EcosystemState::load(s.world(), s.topology(), s.water(), raw_path); }
        catch (const std::exception&) { rejected = true; }
        check(rejected, "invalid persisted nutrient pool was accepted");
    }
    std::filesystem::remove(raw_path);
    cfg.sea_level_m = 10000;
    SimulationState ocean(cfg);
    check(ocean.ecosystem().totals().carbon_after_kg == 0, "terrestrial life seeded in ocean");
    for (int i = 0; i < 100; ++i) budget(ocean.advance_day_full().ecosystem);
    check(ocean.ecosystem().totals().carbon_after_kg == 0, "terrestrial life appeared in ocean");
    std::filesystem::remove(path); std::filesystem::remove(canonical); std::filesystem::remove(legacy);
}
void canopy_and_atomicity() {
    World world(config(42));
    const auto topology = world.analyze_continental_hydrology();
    auto a = make_multiresolution_water_state(world, topology);
    auto b = a;
    (void)materialize_refined_water_tile(world, topology, a, {0,0});
    (void)materialize_refined_water_tile(world, topology, b, {0,0});
    std::vector<ContinentalWaterForcing> forcing(a.coarse_state().cells().size(), {0, 20, 4});
    std::vector<float> factors(forcing.size(), 0);
    const auto ar = advance_multiresolution_water_day(world, a, forcing);
    const auto br = advance_multiresolution_water_day(world, b, forcing, factors);
    check(ar.evapotranspiration_m3 > 0 && br.evapotranspiration_m3 == 0,
        "canopy multiplier missed coarse or refined PET");
    auto weather = make_weather_state(world);
    auto water = make_multiresolution_water_state(world, topology);
    factors[0] = std::numeric_limits<float>::quiet_NaN();
    bool threw = false;
    try { (void)advance_weather_multiresolution_water_day(world, weather, water, factors); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw && weather.simulated_day() == 0 && water.simulated_day() == 0,
        "rejected canopy input partially advanced environment");
}
}
int main() {
    try { kernel(); centuries(); persistence_and_edges(); canopy_and_atomicity(); }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
    std::cout << "All ecosystem tests passed\n";
}
