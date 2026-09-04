#pragma once

#include "worldsim/multiresolution_water.hpp"
#include "worldsim/weather.hpp"
#include "worldsim/world.hpp"

#include <vector>

namespace worldsim {

// Builds one deterministic current-day vegetation forcing record for every already-materialized
// regional patch. This is read-only and never materializes L1/L2 state.
[[nodiscard]] std::vector<VegetationForcing> make_materialized_vegetation_forcing(
    const World& world,
    const WeatherState& weather,
    const MultiresolutionWaterState& water);

} // namespace worldsim
