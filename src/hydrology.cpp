#include "worldsim/hydrology.hpp"

#include "worldsim/world.hpp"
#include "hydrology_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace worldsim {
namespace {

constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

struct QueueNode {
    double elevation{};
    std::size_t index{};
};

struct QueueGreater {
    bool operator()(const QueueNode& a, const QueueNode& b) const noexcept {
        if (a.elevation != b.elevation) return a.elevation > b.elevation;
        return a.index > b.index;
    }
};

bool checked_cell_count(const HydrologyRequest& request, std::size_t& out) {
    const auto w = static_cast<std::size_t>(request.width_cells);
    const auto h = static_cast<std::size_t>(request.height_cells);
    if (w != 0 && h > std::numeric_limits<std::size_t>::max() / w) return false;
    out = w * h;
    return true;
}

std::size_t flat_index(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

bool in_grid(std::int64_t x, std::int64_t y, const HydrologyRequest& r) {
    const auto max_x = r.min_coord.x + static_cast<std::int64_t>(r.width_cells - 1U);
    const auto max_y = r.min_coord.y + static_cast<std::int64_t>(r.height_cells - 1U);
    return x >= r.min_coord.x && y >= r.min_coord.y && x <= max_x && y <= max_y;
}

std::size_t index_for_coord(CellCoord c, const HydrologyRequest& r) {
    if (!in_grid(c.x, c.y, r)) return kNoIndex;
    const auto x = static_cast<std::uint32_t>(c.x - r.min_coord.x);
    const auto y = static_cast<std::uint32_t>(c.y - r.min_coord.y);
    return flat_index(x, y, r.width_cells);
}

CellCoord coord_for_index(std::size_t index, const HydrologyRequest& r) {
    const auto w = static_cast<std::size_t>(r.width_cells);
    const auto y = index / w;
    const auto x = index % w;
    return {r.min_coord.x + static_cast<std::int64_t>(x),
            r.min_coord.y + static_cast<std::int64_t>(y)};
}

bool is_boundary(std::uint32_t x, std::uint32_t y, const HydrologyRequest& r) {
    return x == 0 || y == 0 || x + 1 == r.width_cells || y + 1 == r.height_cells;
}

std::uint64_t boundary_catchment_id(std::size_t boundary_index) {
    // IDs only need to be stable within the requested raster. 0 is reserved for "unset".
    return static_cast<std::uint64_t>(boundary_index) + 1ULL;
}


} // namespace

void HydrologyRequest::validate() const {
    if (width_cells == 0 || height_cells == 0) {
        throw std::invalid_argument("hydrology rectangle must contain at least one cell");
    }
    std::size_t count{};
    if (!checked_cell_count(*this, count)) {
        throw std::invalid_argument("hydrology rectangle is too large");
    }
    if (count > kMaxHydrologyCells) {
        throw std::invalid_argument("hydrology rectangle exceeds the per-analysis cell limit");
    }
    if (!std::isfinite(river_threshold_m3_s) || river_threshold_m3_s < 0.0f) {
        throw std::invalid_argument("river threshold must be finite and non-negative");
    }
    if (!std::isfinite(lake_min_depth_m) || lake_min_depth_m < 0.0f) {
        throw std::invalid_argument("lake minimum depth must be finite and non-negative");
    }
    const auto x_span = static_cast<std::int64_t>(width_cells - 1U);
    const auto y_span = static_cast<std::int64_t>(height_cells - 1U);
    if (min_coord.x > std::numeric_limits<std::int64_t>::max() - x_span ||
        min_coord.y > std::numeric_limits<std::int64_t>::max() - y_span) {
        throw std::invalid_argument("hydrology coordinate range overflows");
    }
}

std::size_t HydrologyResult::index_of(CellCoord coord) const {
    const auto i = index_for_coord(coord, request);
    if (i == kNoIndex) throw std::out_of_range("hydrology coordinate is outside result");
    return i;
}

const HydrologyCell& HydrologyResult::cell(CellCoord coord) const {
    return cells.at(index_of(coord));
}

HydrologyResult analyze_hydrology(const World& world, const HydrologyRequest& request) {
    request.validate();

    std::size_t count{};
    if (!checked_cell_count(request, count)) {
        throw std::invalid_argument("hydrology rectangle is too large");
    }

    HydrologyResult result;
    result.request = request;
    result.cells.resize(count);

    std::vector<double> terrain(count);
    std::vector<double> filled(count);
    std::vector<std::size_t> flood_parent(count, kNoIndex);
    std::vector<std::size_t> downstream(count, kNoIndex);
    std::vector<std::size_t> pop_rank(count, kNoIndex);
    std::vector<bool> visited(count, false);

    for (std::size_t i = 0; i < count; ++i) {
        const CellCoord c = coord_for_index(i, request);
        const auto sample = world.sample_region(c); // Also validates world bounds.
        terrain[i] = static_cast<double>(sample.elevation_m);
        filled[i] = terrain[i];

        auto& out = result.cells[i];
        out.coord = c;
        out.active = true;
        out.terrain_elevation_m = sample.elevation_m;
        out.local_water_yield_m3_s = static_cast<float>(detail::local_water_yield_m3_s(world, c, terrain[i]));
    }

    std::priority_queue<QueueNode, std::vector<QueueNode>, QueueGreater> queue;
    for (std::uint32_t y = 0; y < request.height_cells; ++y) {
        for (std::uint32_t x = 0; x < request.width_cells; ++x) {
            if (!is_boundary(x, y, request)) continue;
            const auto i = flat_index(x, y, request.width_cells);
            if (visited[i]) continue;
            visited[i] = true;
            queue.push({filled[i], i});
        }
    }

    constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int kDy[8] = {-1,-1,-1,  0, 0,  1, 1, 1};

    std::size_t rank = 0;
    while (!queue.empty()) {
        const QueueNode node = queue.top();
        queue.pop();
        pop_rank[node.index] = rank++;

        const auto c = coord_for_index(node.index, request);
        for (int n = 0; n < 8; ++n) {
            const CellCoord nc{c.x + kDx[n], c.y + kDy[n]};
            const auto ni = index_for_coord(nc, request);
            if (ni == kNoIndex || visited[ni]) continue;
            visited[ni] = true;
            flood_parent[ni] = node.index;
            filled[ni] = std::max(terrain[ni], filled[node.index]);
            queue.push({filled[ni], ni});
        }
    }

    // Select steepest descent on the depression-filled surface. On exact flats, use the
    // Priority-Flood parent; that guarantees an acyclic path to a boundary outlet.
    const double cell_m = static_cast<double>(world.config().regional_cell_m);
    for (std::size_t i = 0; i < count; ++i) {
        const auto c = coord_for_index(i, request);
        const auto local_x = static_cast<std::uint32_t>(c.x - request.min_coord.x);
        const auto local_y = static_cast<std::uint32_t>(c.y - request.min_coord.y);
        if (is_boundary(local_x, local_y, request)) continue;

        double best_slope = 0.0;
        std::size_t best = kNoIndex;
        for (int n = 0; n < 8; ++n) {
            const CellCoord nc{c.x + kDx[n], c.y + kDy[n]};
            const auto ni = index_for_coord(nc, request);
            if (ni == kNoIndex) continue;
            const double drop = filled[i] - filled[ni];
            if (!(drop > 0.0)) continue;
            const bool diagonal = kDx[n] != 0 && kDy[n] != 0;
            const double distance = diagonal ? cell_m * std::sqrt(2.0) : cell_m;
            const double slope = drop / distance;
            if (slope > best_slope || (slope == best_slope && ni < best)) {
                best_slope = slope;
                best = ni;
            }
        }
        if (best == kNoIndex) best = flood_parent[i];
        downstream[i] = best;
    }

    // Priority-Flood parent/rank invariants make every interior downstream rank smaller.
    // Accumulating in reverse rank therefore conserves the effective water yield exactly
    // except at boundary outlets, where it leaves the requested analysis domain.
    std::vector<std::size_t> by_rank(count);
    std::iota(by_rank.begin(), by_rank.end(), std::size_t{0});
    std::sort(by_rank.begin(), by_rank.end(), [&](std::size_t a, std::size_t b) {
        return pop_rank[a] > pop_rank[b];
    });

    std::vector<double> discharge(count);
    for (std::size_t i = 0; i < count; ++i) {
        discharge[i] = static_cast<double>(result.cells[i].local_water_yield_m3_s);
    }
    for (const std::size_t i : by_rank) {
        if (downstream[i] != kNoIndex) discharge[downstream[i]] += discharge[i];
    }

    // Catchment identity is the terminal boundary outlet. Boundary IDs are stable for the
    // same requested rectangle and deterministic across query order/thread scheduling.
    std::vector<std::uint64_t> catchment(count, 0);
    std::vector<std::size_t> ascending = by_rank;
    std::reverse(ascending.begin(), ascending.end());
    for (const std::size_t i : ascending) {
        if (downstream[i] == kNoIndex) {
            catchment[i] = boundary_catchment_id(i);
        } else {
            catchment[i] = catchment[downstream[i]];
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        auto& out = result.cells[i];
        out.filled_elevation_m = static_cast<float>(filled[i]);
        out.depression_depth_m = static_cast<float>(std::max(0.0, filled[i] - terrain[i]));
        out.accumulated_discharge_m3_s = static_cast<float>(discharge[i]);
        out.catchment_id = catchment[i];
        if (downstream[i] != kNoIndex) {
            out.has_downstream = true;
            out.downstream_coord = coord_for_index(downstream[i], request);
        }
    }

    // Connected components on the same filled spill surface define depression basins.
    // The depth threshold decides whether a basin becomes a lake record; it does not cut
    // shallow shoreline cells out of an otherwise valid lake.
    std::vector<bool> depression_cell(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        depression_cell[i] = result.cells[i].depression_depth_m > 1e-6f;
    }
    std::vector<bool> lake_seen(count, false);
    std::vector<std::size_t> stack;
    const double cell_area_m2 = cell_m * cell_m;
    std::uint64_t next_lake_id = 1;

    for (std::size_t start = 0; start < count; ++start) {
        if (!depression_cell[start] || lake_seen[start]) continue;
        stack.clear();
        stack.push_back(start);
        lake_seen[start] = true;
        std::vector<std::size_t> members;

        LakeInfo lake;
        lake.surface_elevation_m = -std::numeric_limits<float>::infinity();
        while (!stack.empty()) {
            const auto i = stack.back();
            stack.pop_back();
            members.push_back(i);
            lake.cell_count += 1;
            lake.area_m2 += cell_area_m2;
            lake.volume_m3 += static_cast<double>(result.cells[i].depression_depth_m) * cell_area_m2;
            lake.max_depth_m = std::max(lake.max_depth_m, result.cells[i].depression_depth_m);
            lake.surface_elevation_m = std::max(lake.surface_elevation_m, result.cells[i].filled_elevation_m);

            const auto c = coord_for_index(i, request);
            for (int n = 0; n < 8; ++n) {
                const auto ni = index_for_coord({c.x + kDx[n], c.y + kDy[n]}, request);
                if (ni == kNoIndex || lake_seen[ni] || !depression_cell[ni]) continue;
                // Touching depressions with different spill elevations are distinct basins.
                if (std::abs(filled[ni] - filled[i]) > 1e-5) continue;
                lake_seen[ni] = true;
                stack.push_back(ni);
            }
        }

        if (lake.max_depth_m + 1e-6f < request.lake_min_depth_m) continue;

        const std::uint64_t lake_id = next_lake_id++;
        lake.id = lake_id;
        for (const auto i : members) result.cells[i].lake_id = lake_id;

        // Pick the lake member whose drainage edge first leaves this component.
        // Tie-break on flattened index for deterministic output.
        std::size_t outlet = kNoIndex;
        for (const auto i : members) {
            const auto d = downstream[i];
            if (d == kNoIndex || result.cells[d].lake_id != lake_id) {
                if (outlet == kNoIndex || i < outlet) outlet = i;
            }
        }
        if (outlet == kNoIndex) outlet = members.front();
        lake.outlet_coord = coord_for_index(outlet, request);
        const auto lake_downstream = downstream[outlet];
        if (lake_downstream != kNoIndex) {
            lake.has_outflow = true;
            lake.outflow_coord = coord_for_index(lake_downstream, request);
        }
        result.lakes.push_back(lake);
    }

    // A river is an accumulated effective-water-yield path above the configured threshold.
    // Lake cells are excluded from the channel graph; inflowing/outflowing channel edges remain.
    for (std::size_t i = 0; i < count; ++i) {
        auto& cell = result.cells[i];
        cell.river = cell.lake_id == 0 && cell.accumulated_discharge_m3_s >= request.river_threshold_m3_s;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!result.cells[i].river || downstream[i] == kNoIndex) continue;
        result.river_segments.push_back({result.cells[i].coord,
                                         result.cells[downstream[i]].coord,
                                         result.cells[i].accumulated_discharge_m3_s});
    }

    return result;
}

HydrologyResult World::analyze_hydrology(const HydrologyRequest& request) const {
    return worldsim::analyze_hydrology(*this, request);
}

} // namespace worldsim
