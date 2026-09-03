#include "worldsim/continental_hydrology.hpp"
#include "worldsim/hydrology.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

template <typename Fn>
void expect_invalid_argument(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return;
    } catch (...) {
        throw std::runtime_error(message);
    }
    throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        worldsim::HydrologyResult local;
        local.request.min_coord = {0, 0};
        local.request.width_cells = 0;
        local.request.height_cells = 1;
        expect_invalid_argument(
            [&] { (void)local.index_of({0, 0}); },
            "HydrologyResult::index_of accepted an invalid zero-width result");

        worldsim::HydrologyResult overflowing_local;
        overflowing_local.request.min_coord = {std::numeric_limits<std::int64_t>::max(), 0};
        overflowing_local.request.width_cells = 2;
        overflowing_local.request.height_cells = 1;
        overflowing_local.cells.resize(2);
        expect_invalid_argument(
            [&] { (void)overflowing_local.index_of({std::numeric_limits<std::int64_t>::max(), 0}); },
            "HydrologyResult::index_of accepted an unrepresentable coordinate range");

        worldsim::ContinentalHydrologyResult continental;
        continental.min_coord = {0, 0};
        continental.width_cells = 0;
        continental.height_cells = 1;
        expect_invalid_argument(
            [&] { (void)continental.index_of({0, 0}); },
            "ContinentalHydrologyResult::index_of accepted an invalid zero-width result");

        worldsim::ContinentalHydrologyResult overflowing_continental;
        overflowing_continental.min_coord = {std::numeric_limits<std::int64_t>::max(), 0};
        overflowing_continental.width_cells = 2;
        overflowing_continental.height_cells = 1;
        overflowing_continental.cells.resize(2);
        expect_invalid_argument(
            [&] { (void)overflowing_continental.index_of({std::numeric_limits<std::int64_t>::max(), 0}); },
            "ContinentalHydrologyResult::index_of accepted an unrepresentable coordinate range");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
