#pragma once

#include "worldsim/continental_hydrology.hpp"

#include <array>
#include <filesystem>
#include <vector>

namespace worldsim {

class World;
class WeatherState;
class MultiresolutionWaterState;

// Carbon densities in kg C/m²; mineral nitrogen in kg N/m². All organic pools
// use a common C:N ratio of 30 in this deliberately aggregated food-web model.
struct EcosystemCell {
    double grass_carbon{};
    double shrub_carbon{};
    double tree_carbon{};
    double herbivore_carbon{};
    double carnivore_carbon{};
    double litter_carbon{};
    double mineral_nitrogen{};
    bool operator==(const EcosystemCell&) const = default;
};

struct EcosystemForcing {
    double temperature_c{};
    double soil_saturation{};
    double snow_water_mm{};
};

struct EcosystemHabitat {
    double area_m2{}; // zero for ocean; clipped at world edges
    std::array<double, 3> plant_capacity{};
    double soil_capacity_mm{};
};

struct EcosystemStepReport {
    std::int64_t day_before{};
    std::int64_t day_after{};
    double carbon_before_kg{};
    double carbon_after_kg{};
    double photosynthesis_kg{};
    double respiration_kg{};
    double nitrogen_before_kg{};
    double nitrogen_after_kg{};
    double herbivory_kg{};
    double predation_kg{};
    double plant_carbon_kg{};
    double herbivore_carbon_kg{};
    double carnivore_carbon_kg{};
    double carbon_balance_error_kg{};
    double nitrogen_balance_error_kg{};
};

// Positivity-preserving local process kernel, also usable for controlled experiments.
// Commits only after all inputs and resulting pools have been validated.
[[nodiscard]] EcosystemStepReport advance_ecosystem_cell(
    EcosystemCell& cell, const EcosystemHabitat& habitat, const EcosystemForcing& forcing);

class EcosystemState {
public:
    [[nodiscard]] std::int64_t simulated_day() const noexcept { return day_; }
    [[nodiscard]] const std::vector<EcosystemCell>& cells() const noexcept { return cells_; }
    [[nodiscard]] const EcosystemCell& cell(CellCoord coord) const;
    [[nodiscard]] const std::vector<EcosystemHabitat>& habitats() const noexcept { return habitats_; }
    [[nodiscard]] EcosystemStepReport totals() const;
    [[nodiscard]] std::vector<float> evapotranspiration_factors() const;
    [[nodiscard]] EcosystemStepReport advance_day(
        const World& world, const WeatherState& weather, const MultiresolutionWaterState& water);
    void apply_local_disturbance(const World& before, const World& after);
    void swap(EcosystemState& other) noexcept;
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static EcosystemState create(
        const World& world, const ContinentalHydrologyResult& topology,
        const MultiresolutionWaterState& water, std::int64_t day = 0);
    [[nodiscard]] static EcosystemState load(
        const World& world, const ContinentalHydrologyResult& topology,
        const MultiresolutionWaterState& water, const std::filesystem::path& path);
private:
    WorldConfig config_{};
    CellCoord min_coord_{};
    std::uint32_t width_{};
    std::int64_t day_{};
    std::vector<EcosystemCell> cells_;
    std::vector<EcosystemHabitat> habitats_;
};

} // namespace worldsim
