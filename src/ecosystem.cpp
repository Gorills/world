#include "worldsim/ecosystem.hpp"
#include "worldsim/world.hpp"
#include "worldsim/weather.hpp"
#include "worldsim/multiresolution_water.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace worldsim {
namespace {
constexpr double kCN = 30.0;
using Pool = double EcosystemCell::*;
constexpr std::array<Pool, 7> kPools{
    &EcosystemCell::grass_carbon, &EcosystemCell::shrub_carbon,
    &EcosystemCell::tree_carbon, &EcosystemCell::herbivore_carbon,
    &EcosystemCell::carnivore_carbon, &EcosystemCell::litter_carbon,
    &EcosystemCell::mineral_nitrogen};

double carbon(const EcosystemCell& c) {
    return c.grass_carbon + c.shrub_carbon + c.tree_carbon +
        c.herbivore_carbon + c.carnivore_carbon + c.litter_carbon;
}
void validate(const EcosystemCell& c) {
    for (auto p : kPools) {
        if (!std::isfinite(c.*p) || c.*p < 0.0 || c.*p > 1.0e6)
            throw std::invalid_argument("ecosystem pool is negative, non-finite or outside supported density");
    }
}
void account(EcosystemStepReport& r, const EcosystemCell& c, double area, bool before) {
    const double mass = carbon(c) * area;
    const double nitrogen = (carbon(c) / kCN + c.mineral_nitrogen) * area;
    if (before) {
        r.carbon_before_kg += mass;
        r.nitrogen_before_kg += nitrogen;
    } else {
        r.carbon_after_kg += mass;
        r.nitrogen_after_kg += nitrogen;
        r.plant_carbon_kg += (c.grass_carbon + c.shrub_carbon + c.tree_carbon) * area;
        r.herbivore_carbon_kg += c.herbivore_carbon * area;
        r.carnivore_carbon_kg += c.carnivore_carbon * area;
    }
}
void balance(EcosystemStepReport& r) {
    r.carbon_balance_error_kg = r.carbon_before_kg + r.photosynthesis_kg -
        r.respiration_kg - r.carbon_after_kg;
    r.nitrogen_balance_error_kg = r.nitrogen_before_kg - r.nitrogen_after_kg;
}
double overlap(double lo, double hi, double a, double b) {
    return std::max(0.0, std::min(hi, b) - std::max(lo, a));
}
double area_of(CellCoord coord, double size, const WorldBounds& b) {
    const double x = static_cast<double>(coord.x) * size;
    const double y = static_cast<double>(coord.y) * size;
    return overlap(x, x + size, b.origin_x_m, b.origin_x_m + b.width_m) *
        overlap(y, y + size, b.origin_y_m, b.origin_y_m + b.height_m);
}
bool same_config(const WorldConfig& a, const WorldConfig& b) {
    return a.seed == b.seed && a.bounds.origin_x_m == b.bounds.origin_x_m &&
        a.bounds.origin_y_m == b.bounds.origin_y_m && a.bounds.width_m == b.bounds.width_m &&
        a.bounds.height_m == b.bounds.height_m && a.sea_level_m == b.sea_level_m &&
        a.climate_cell_m == b.climate_cell_m && a.regional_cell_m == b.regional_cell_m &&
        a.local_cell_m == b.local_cell_m;
}
template<class T> void write(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!out) throw std::runtime_error("failed to write ecosystem checkpoint");
}
template<class T> void read(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in) throw std::runtime_error("truncated ecosystem checkpoint");
}
} // namespace

EcosystemStepReport advance_ecosystem_cell(
    EcosystemCell& cell, const EcosystemHabitat& h, const EcosystemForcing& f) {
    validate(cell);
    if (!std::isfinite(h.area_m2) || h.area_m2 < 0 || h.area_m2 > 1.0e12 ||
        !std::isfinite(f.temperature_c) || !std::isfinite(f.soil_saturation) ||
        f.soil_saturation < 0 || f.soil_saturation > 1 ||
        !std::isfinite(f.snow_water_mm) || f.snow_water_mm < 0)
        throw std::invalid_argument("invalid ecosystem habitat or forcing");
    for (double capacity : h.plant_capacity) {
        if (!std::isfinite(capacity) || capacity < 0 || capacity > 100)
            throw std::invalid_argument("invalid ecosystem plant capacity");
    }
    if (h.area_m2 == 0 && (carbon(cell) != 0 || cell.mineral_nitrogen != 0))
        throw std::invalid_argument("ocean cannot own terrestrial ecosystem pools");
    EcosystemStepReport r;
    account(r, cell, h.area_m2, true);
    auto next = cell;
    const double warmth = std::clamp((f.temperature_c + 3.0) / 23.0, 0.0, 1.0) *
        std::clamp((48.0 - f.temperature_c) / 18.0, 0.0, 1.0);
    const double moisture = std::clamp((f.soil_saturation - 0.08) / 0.52, 0.0, 1.0);
    const double season = warmth * moisture;
    const double snow_access = 1.0 / (1.0 + f.snow_water_mm / 30.0);
    const double drought = std::clamp((0.12 - f.soil_saturation) / 0.12, 0.0, 1.0);
    const double heat = std::clamp((f.temperature_c - 38.0) / 15.0, 0.0, 1.0);
    const double frost = std::clamp((-15.0 - f.temperature_c) / 30.0, 0.0, 1.0);
    const auto respire = [&](double& pool, double fraction) {
        const double loss = pool * (-std::expm1(-fraction));
        pool -= loss;
        next.mineral_nitrogen += loss / kCN;
        r.respiration_kg += loss * h.area_m2;
    };
    // Decomposition mineralizes organic N. Its carbon exits as respiration.
    respire(next.litter_carbon, 0.004 * warmth * (0.15 + 0.85 * moisture));
    constexpr std::array<double, 3> growth{0.055, 0.015, 0.0025};
    constexpr std::array<double, 3> turnover{0.0025, 0.0006, 0.00012};
    std::array<double, 3> demand{};
    double total_demand = 0;
    for (std::size_t j = 0; j < 3; ++j) {
        double& plant = next.*kPools[j];
        const double dead = plant * (-std::expm1(-turnover[j] -
            (0.004 * drought + 0.006 * heat + 0.002 * frost) / (1.0 + static_cast<double>(j))));
        plant -= dead;
        next.litter_carbon += dead;
        const double shade = j == 0 ? 1.0 / (1.0 + next.tree_carbon * 0.6 + next.shrub_carbon * 0.3)
            : (j == 1 ? 1.0 / (1.0 + next.tree_carbon * 0.2) : 1.0);
        const double capacity = h.plant_capacity[j] * shade;
        // Exponential bounded gain, zero recruitment without living biomass/immigration.
        const double room = std::max(0.0, capacity - plant);
        demand[j] = capacity > 0 ? std::min(room, plant *
            std::expm1(growth[j] * season * room / capacity)) : 0.0;
        total_demand += demand[j];
    }
    const double nutrient_scale = total_demand > 0
        ? std::min(1.0, next.mineral_nitrogen * kCN / total_demand) : 0.0;
    double uptake = 0;
    for (std::size_t j = 0; j < 3; ++j) {
        const double gain = demand[j] * nutrient_scale;
        next.*kPools[j] += gain;
        uptake += gain;
    }
    next.mineral_nitrogen = std::max(0.0, next.mineral_nitrogen - uptake / kCN);
    r.photosynthesis_kg = uptake * h.area_m2;

    // Holling-type saturating ingestion; every transfer is capped by available food.
    // Snow buries ground forage; woody browse remains accessible above it.
    const double accessible_grass = next.grass_carbon * snow_access;
    const double edible = accessible_grass + 0.2 * next.shrub_carbon;
    const double eaten = std::min(edible, next.herbivore_carbon * 0.025 *
        edible / (edible + 0.04 + 2.0 * next.herbivore_carbon));
    if (edible > 0) {
        const double grass_eaten = std::min(next.grass_carbon, eaten * accessible_grass / edible);
        next.grass_carbon -= grass_eaten;
        next.shrub_carbon -= eaten - grass_eaten;
    }
    next.herbivore_carbon += eaten * 0.3;
    next.litter_carbon += eaten * 0.7;
    r.herbivory_kg = eaten * h.area_m2;
    // Type-III hunting response: sparse prey are harder to find; territorial
    // interference limits intake per predator as predator density rises.
    const double prey_density = next.herbivore_carbon;
    const double encounter = prey_density * prey_density /
        (prey_density * prey_density + 0.001 * 0.001);
    const double prey = std::min(prey_density, next.carnivore_carbon * 0.02 *
        encounter / (1.0 + next.carnivore_carbon / 0.0003));
    next.herbivore_carbon -= prey;
    next.carnivore_carbon += prey * 0.6;
    next.litter_carbon += prey * 0.4;
    r.predation_kg = prey * h.area_m2;
    // Maintenance consumes reserves; food intake produces growth/reproduction above it.
    respire(next.herbivore_carbon, 0.0025 + 0.001 * frost);
    respire(next.carnivore_carbon, 0.003 + 0.001 * frost);
    for (auto p : {&EcosystemCell::herbivore_carbon, &EcosystemCell::carnivore_carbon}) {
        double& animal = next.*p;
        const double dead = animal * (-std::expm1(-0.0007));
        animal -= dead;
        next.litter_carbon += dead;
    }
    validate(next);
    account(r, next, h.area_m2, false);
    balance(r);
    cell = next;
    return r;
}

EcosystemState EcosystemState::create(const World& world,
    const ContinentalHydrologyResult& topology, const MultiresolutionWaterState& water,
    std::int64_t day) {
    if (day < 0 || day != water.simulated_day() || !same_config(world.config(), topology.config) ||
        !same_config(world.config(), water.config()) ||
        topology.cells.size() != water.coarse_state().cells().size() ||
        topology.min_coord != water.coarse_state().min_coord() ||
        topology.width_cells != water.coarse_state().width_cells() ||
        topology.height_cells != water.coarse_state().height_cells())
        throw std::invalid_argument("ecosystem creation requires aligned world/water/topology");
    EcosystemState out;
    out.config_ = world.config();
    out.min_coord_ = topology.min_coord;
    out.width_ = topology.width_cells;
    out.day_ = day;
    out.cells_.resize(topology.cells.size());
    out.habitats_.resize(topology.cells.size());
    for (std::size_t i = 0; i < out.cells_.size(); ++i) {
        const auto& terrain = topology.cells[i];
        if (terrain.ocean) continue;
        auto& h = out.habitats_[i];
        h.area_m2 = area_of(terrain.coord, out.config_.climate_cell_m, out.config_.bounds);
        const auto climate = world.sample_climate(terrain.coord);
        const double mean_t = climate.mean_temperature_c -
            std::max(0.0f, terrain.terrain_elevation_m) * 0.0065;
        const double productive = std::clamp((mean_t + 15.0) / 30.0, 0.02, 1.0) *
            std::clamp(climate.annual_precipitation_mm / 900.0, 0.02, 1.0);
        h.plant_capacity = {0.35 + 0.3 * productive, 0.3 + 1.2 * productive, 0.1 + 7.9 * productive};
        h.soil_capacity_mm = water.parameters().soil_capacity_mm *
            world.sample_climate_soil(terrain.coord).storage_capacity_scale;
        auto& c = out.cells_[i];
        c.grass_carbon = h.plant_capacity[0] * 0.45;
        c.shrub_carbon = h.plant_capacity[1] * 0.35;
        c.tree_carbon = h.plant_capacity[2] * 0.5;
        c.herbivore_carbon = 0.002 * productive;
        c.carnivore_carbon = 0.00008 * productive;
        c.litter_carbon = 0.4 + 0.6 * productive;
        c.mineral_nitrogen = 0.03;
    }
    out.apply_local_disturbance(World(world.config()), world);
    return out;
}

const EcosystemCell& EcosystemState::cell(CellCoord coord) const {
    if (width_ == 0 || cells_.empty() || coord.x < min_coord_.x || coord.y < min_coord_.y ||
        coord.x >= min_coord_.x + width_ ||
        coord.y >= min_coord_.y + static_cast<std::int64_t>(cells_.size() / width_))
        throw std::out_of_range("ecosystem coordinate is outside the world");
    return cells_.at(static_cast<std::size_t>(coord.y - min_coord_.y) * width_ +
        static_cast<std::size_t>(coord.x - min_coord_.x));
}

EcosystemStepReport EcosystemState::totals() const {
    EcosystemStepReport r;
    r.day_before = day_;
    r.day_after = day_;
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        account(r, cells_[i], habitats_[i].area_m2, true);
        account(r, cells_[i], habitats_[i].area_m2, false);
    }
    return r;
}

std::vector<float> EcosystemState::evapotranspiration_factors() const {
    std::vector<float> result;
    result.reserve(cells_.size());
    for (const auto& c : cells_) {
        const double cover = 1.0 - std::exp(-(c.grass_carbon * 3.0 +
            c.shrub_carbon * 1.5 + c.tree_carbon * 0.5));
        result.push_back(static_cast<float>(0.35 + 0.65 * cover));
    }
    return result;
}

EcosystemStepReport EcosystemState::advance_day(
    const World& world, const WeatherState& weather, const MultiresolutionWaterState& water) {
    if (day_ == std::numeric_limits<std::int64_t>::max())
        throw std::overflow_error("ecosystem day overflow");
    if (day_ != weather.simulated_day() || day_ != water.simulated_day() ||
        !same_config(config_, world.config()) || !same_config(config_, weather.config()) ||
        !same_config(config_, water.config()) || cells_.size() != weather.cells().size())
        throw std::invalid_argument("ecosystem/environment identity or clock mismatch");
    // Batch forcing validates the atmosphere once, avoiding a full-grid scan per cell.
    const auto atmosphere = make_weather_daily_forcing(weather);
    auto pending = cells_;
    auto report = totals();
    report.day_after = day_ + 1;
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto& h = habitats_[i];
        if (h.area_m2 == 0) continue;
        const auto coord = weather.coord_of(i);
        const auto& w = atmosphere[i];
        EcosystemForcing f{w.mean_air_temperature_c, 0.0, 0.0};
        if (water.is_refined(coord)) {
            double land_area = 0;
            for (const auto& child : water.refined_tile(coord).cells) {
                if (!child.active) continue;
                const double a = area_of(child.coord, config_.regional_cell_m, config_.bounds);
                const double capacity = water.parameters().soil_capacity_mm *
                    world.sample_soil(child.coord).storage_capacity_scale;
                f.soil_saturation += a * std::clamp(child.soil_water_mm / std::max(1.0e-12, capacity), 0.0, 1.0);
                f.snow_water_mm += a * child.snow_water_equivalent_mm;
                land_area += a;
            }
            if (land_area > 0) {
                f.soil_saturation /= land_area;
                f.snow_water_mm /= land_area;
            }
        } else {
            const auto& wet = water.coarse_state().cells()[i];
            f.soil_saturation = std::clamp(wet.soil_water_mm / std::max(1.0e-12, h.soil_capacity_mm), 0.0, 1.0);
            f.snow_water_mm = wet.snow_water_equivalent_mm;
        }
        const auto local = advance_ecosystem_cell(pending[i], h, f);
        report.photosynthesis_kg += local.photosynthesis_kg;
        report.respiration_kg += local.respiration_kg;
        report.herbivory_kg += local.herbivory_kg;
        report.predation_kg += local.predation_kg;
    }
    // Simultaneous conservative dispersal, each undirected edge once. Fluxes use
    // post-reaction snapshots and min(area) so partial cells cannot be overdrawn.
    auto dispersed = pending;
    constexpr std::array<double, 5> rates{0.0002, 0.00005, 0.00001, 0.01, 0.02};
    const auto exchange = [&](std::size_t i, std::size_t j) {
        const double ai = habitats_[i].area_m2;
        const double aj = habitats_[j].area_m2;
        if (ai == 0 || aj == 0) return;
        for (std::size_t p = 0; p < rates.size(); ++p) {
            const double flux = (pending[i].*kPools[p] - pending[j].*kPools[p]) *
                std::min(ai, aj) * rates[p];
            dispersed[i].*kPools[p] -= flux / ai;
            dispersed[j].*kPools[p] += flux / aj;
        }
    };
    for (std::size_t i = 0; i < pending.size(); ++i) {
        if (i % width_ + 1 < width_) exchange(i, i + 1);
        if (i + width_ < pending.size()) exchange(i, i + width_);
    }
    report.carbon_after_kg = report.nitrogen_after_kg = 0;
    report.plant_carbon_kg = report.herbivore_carbon_kg = report.carnivore_carbon_kg = 0;
    for (std::size_t i = 0; i < dispersed.size(); ++i) {
        validate(dispersed[i]);
        account(report, dispersed[i], habitats_[i].area_m2, false);
    }
    balance(report);
    cells_.swap(dispersed);
    ++day_;
    return report;
}

void EcosystemState::apply_local_disturbance(const World& before, const World& after) {
    if (!same_config(config_, before.config()) || !same_config(config_, after.config()))
        throw std::invalid_argument("disturbance world identity mismatch");
    std::vector<double> fractions(cells_.size(), 0.0);
    for (const auto coord : after.materialized_patch_coords()) {
        const auto* old = before.find_local_patch(coord);
        const auto* patch = after.find_local_patch(coord);
        const auto parent = regional_to_climate(coord, config_);
        const auto index = static_cast<std::size_t>(parent.y - min_coord_.y) * width_ +
            static_cast<std::size_t>(parent.x - min_coord_.x);
        const double area = habitats_.at(index).area_m2;
        if (area == 0) continue;
        for (std::size_t j = 0; j < patch->cells.size(); ++j) {
            const double prev = old ? old->cells[j].disturbance : 0.0;
            const double now = patch->cells[j].disturbance;
            if (now <= prev || prev >= 1.0 || patch->cells[j].elevation_m <= config_.sea_level_m) continue;
            const CellCoord local{coord.x * 16 + static_cast<std::int64_t>(j % 16),
                coord.y * 16 + static_cast<std::int64_t>(j / 16)};
            fractions[index] += area_of(local, config_.local_cell_m, config_.bounds) / area *
                (now - prev) / (1.0 - prev);
        }
    }
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const double fraction = std::clamp(fractions[i], 0.0, 1.0);
        for (std::size_t p = 0; p < 3; ++p) {
            double& plant = cells_[i].*kPools[p];
            const double dead = plant * fraction;
            plant -= dead;
            cells_[i].litter_carbon += dead;
        }
    }
}

void EcosystemState::swap(EcosystemState& other) noexcept {
    using std::swap;
    swap(config_, other.config_); swap(min_coord_, other.min_coord_);
    swap(width_, other.width_); swap(day_, other.day_);
    cells_.swap(other.cells_); habitats_.swap(other.habitats_);
}

void EcosystemState::save(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create ecosystem checkpoint");
    write(out, std::uint64_t{0x315943454F535357ULL});
    write(out, std::uint32_t{1}); write(out, day_);
    write(out, config_.seed);
    write(out, config_.bounds.origin_x_m); write(out, config_.bounds.origin_y_m);
    write(out, config_.bounds.width_m); write(out, config_.bounds.height_m);
    write(out, config_.sea_level_m);
    write(out, static_cast<std::uint64_t>(cells_.size()));
    for (const auto& c : cells_) {
        validate(c);
        for (auto p : kPools) write(out, c.*p);
    }
    out.flush();
    if (!out) throw std::runtime_error("failed to flush ecosystem checkpoint");
}

EcosystemState EcosystemState::load(const World& world,
    const ContinentalHydrologyResult& topology, const MultiresolutionWaterState& water,
    const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open ecosystem checkpoint");
    std::uint64_t magic{}, count{};
    std::uint32_t version{};
    std::int64_t day{};
    WorldConfig config;
    read(in, magic); read(in, version); read(in, day);
    read(in, config.seed);
    read(in, config.bounds.origin_x_m); read(in, config.bounds.origin_y_m);
    read(in, config.bounds.width_m); read(in, config.bounds.height_m);
    read(in, config.sea_level_m); read(in, count);
    if (magic != 0x315943454F535357ULL || version != 1 ||
        day != water.simulated_day() || !same_config(config, world.config()) ||
        count != topology.cells.size())
        throw std::runtime_error("ecosystem checkpoint identity, version, count or day mismatch");
    const auto header = in.tellg();
    if (header < 0 || std::filesystem::file_size(path) !=
        static_cast<std::uint64_t>(header) + count * 7 * sizeof(double))
        throw std::runtime_error("ecosystem checkpoint payload length mismatch");
    auto result = create(world, topology, water, day);
    for (std::size_t i = 0; i < result.cells_.size(); ++i) {
        auto& c = result.cells_[i];
        for (auto p : kPools) read(in, c.*p);
        validate(c);
        if (result.habitats_[i].area_m2 == 0 && (carbon(c) != 0 || c.mineral_nitrogen != 0))
            throw std::runtime_error("ecosystem checkpoint contains life in ocean cells");
    }
    return result;
}
} // namespace worldsim
