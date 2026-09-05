#include "worldsim/settlements.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace worldsim {

const Settlement* SettlementState::settlement(SettlementId id) const noexcept {
    const auto it = std::find_if(settlements_.begin(), settlements_.end(),
        [id](const Settlement& value) { return value.id == id; });
    return it == settlements_.end() ? nullptr : &*it;
}

const Settlement* SettlementState::settlement_at(CellCoord regional_coord) const noexcept {
    const auto it = std::find_if(settlements_.begin(), settlements_.end(),
        [regional_coord](const Settlement& value) {
            return value.regional_coord == regional_coord;
        });
    return it == settlements_.end() ? nullptr : &*it;
}

SettlementId SettlementState::found(
    CellCoord regional_coord,
    double population,
    std::int64_t founded_day) {
    if (!std::isfinite(population) || population < 0.0) {
        throw std::invalid_argument("settlement population must be finite and non-negative");
    }
    if (founded_day < 0) {
        throw std::invalid_argument("settlement founded day must be non-negative");
    }
    if (settlement_at(regional_coord)) {
        throw std::invalid_argument("regional cell already owns a settlement");
    }
    if (next_id_ == 0 || next_id_ == std::numeric_limits<SettlementId>::max()) {
        throw std::overflow_error("settlement id space exhausted");
    }
    const auto id = next_id_;
    settlements_.push_back({id, regional_coord, population, founded_day});
    ++next_id_;
    return id;
}

void SettlementState::swap(SettlementState& other) noexcept {
    settlements_.swap(other.settlements_);
    std::swap(next_id_, other.next_id_);
}

SettlementState SettlementState::from_persisted(
    std::vector<Settlement> settlements,
    SettlementId next_id) {
    if (next_id == 0) throw std::runtime_error("settlement next id is invalid");

    std::unordered_set<SettlementId> ids;
    std::unordered_set<CellCoord, CellCoordHash> coords;
    SettlementId max_id = 0;
    for (const auto& value : settlements) {
        if (value.id == 0 || !ids.insert(value.id).second) {
            throw std::runtime_error("settlement checkpoint contains duplicate/invalid ids");
        }
        if (!coords.insert(value.regional_coord).second) {
            throw std::runtime_error("settlement checkpoint contains duplicate coordinates");
        }
        if (!std::isfinite(value.population) || value.population < 0.0) {
            throw std::runtime_error("settlement checkpoint contains invalid population");
        }
        if (value.founded_day < 0) {
            throw std::runtime_error("settlement checkpoint contains invalid founded day");
        }
        max_id = std::max(max_id, value.id);
    }
    if (next_id <= max_id) {
        throw std::runtime_error("settlement checkpoint next id does not exceed existing ids");
    }

    SettlementState out;
    out.settlements_ = std::move(settlements);
    out.next_id_ = next_id;
    return out;
}

} // namespace worldsim
