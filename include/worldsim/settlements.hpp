#pragma once

#include "worldsim/types.hpp"

#include <cstdint>
#include <vector>

namespace worldsim {

using SettlementId = std::uint64_t;

struct Settlement {
    SettlementId id{};
    CellCoord regional_coord{};
    double population{};
    std::int64_t founded_day{};

    friend bool operator==(const Settlement&, const Settlement&) = default;
};

struct SettlementSuitability {
    double terrain_factor{};
    double water_factor{};
    double vegetation_factor{};
    double temperature_factor{};
    double disturbance_factor{};
    double environmental_capacity{};
};

struct SettlementStepReport {
    std::uint64_t settlement_count{};
    double population_before{};
    double population_after{};
    double environmental_capacity{};
};

class SettlementState {
public:
    [[nodiscard]] const std::vector<Settlement>& settlements() const noexcept { return settlements_; }
    [[nodiscard]] const Settlement* settlement(SettlementId id) const noexcept;
    [[nodiscard]] const Settlement* settlement_at(CellCoord regional_coord) const noexcept;
    [[nodiscard]] SettlementId next_id() const noexcept { return next_id_; }

    SettlementId found(CellCoord regional_coord, double population, std::int64_t founded_day);
    void swap(SettlementState& other) noexcept;

    [[nodiscard]] static SettlementState from_persisted(
        std::vector<Settlement> settlements,
        SettlementId next_id);

private:
    std::vector<Settlement> settlements_;
    SettlementId next_id_{1};
};

} // namespace worldsim
