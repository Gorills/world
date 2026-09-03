#include "worldsim/continental_hydrology.hpp"

#include "worldsim/coordinates.hpp"
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
constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDy[8] = {-1,-1,-1,  0, 0,  1, 1, 1};

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

bool same_bounds(const WorldBounds& a, const WorldBounds& b) {
    return a.origin_x_m == b.origin_x_m && a.origin_y_m == b.origin_y_m &&
           a.width_m == b.width_m && a.height_m == b.height_m;
}

bool same_config_identity(const WorldConfig& a, const WorldConfig& b) {
    return a.seed == b.seed && same_bounds(a.bounds, b.bounds) &&
           a.local_cell_m == b.local_cell_m && a.regional_cell_m == b.regional_cell_m &&
           a.climate_cell_m == b.climate_cell_m && a.sea_level_m == b.sea_level_m;
}

bool rect_intersects_world(CellCoord coord, std::int32_t cell_m, const WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = static_cast<double>(coord.x) * s;
    const double y0 = static_cast<double>(coord.y) * s;
    const double x1 = x0 + s;
    const double y1 = y0 + s;
    return x1 > b.origin_x_m && x0 < b.origin_x_m + b.width_m &&
           y1 > b.origin_y_m && y0 < b.origin_y_m + b.height_m;
}

double overlap_area_m2(CellCoord coord, std::int32_t cell_m, const WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) return 0.0;
    return (x1 - x0) * (y1 - y0);
}

WorldPosition overlap_center(CellCoord coord, std::int32_t cell_m, const WorldBounds& b) {
    const double s = static_cast<double>(cell_m);
    const double x0 = std::max(static_cast<double>(coord.x) * s, b.origin_x_m);
    const double y0 = std::max(static_cast<double>(coord.y) * s, b.origin_y_m);
    const double x1 = std::min((static_cast<double>(coord.x) + 1.0) * s, b.origin_x_m + b.width_m);
    const double y1 = std::min((static_cast<double>(coord.y) + 1.0) * s, b.origin_y_m + b.height_m);
    if (!(x1 > x0) || !(y1 > y0)) throw std::out_of_range("cell does not intersect world bounds");
    return {x0 + (x1 - x0) * 0.5, y0 + (y1 - y0) * 0.5};
}

struct GridRange {
    CellCoord min{};
    std::uint32_t width{};
    std::uint32_t height{};
};

GridRange climate_range(const WorldConfig& cfg) {
    const auto& b = cfg.bounds;
    const auto min = world_to_cell({b.origin_x_m, b.origin_y_m}, cfg.climate_cell_m);
    const WorldPosition last{
        std::nextafter(b.origin_x_m + b.width_m, -std::numeric_limits<double>::infinity()),
        std::nextafter(b.origin_y_m + b.height_m, -std::numeric_limits<double>::infinity())};
    const auto max = world_to_cell(last, cfg.climate_cell_m);
    const auto width64 = max.x - min.x + 1;
    const auto height64 = max.y - min.y + 1;
    if (width64 <= 0 || height64 <= 0 ||
        width64 > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) ||
        height64 > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("world climate raster dimensions are not representable");
    }
    const auto width = static_cast<std::uint32_t>(width64);
    const auto height = static_cast<std::uint32_t>(height64);
    const auto count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (count > kMaxContinentalHydrologyCells) {
        throw std::invalid_argument("world exceeds the continental hydrology cell limit");
    }
    return {min, width, height};
}

std::size_t flat(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x;
}

bool in_continent(CellCoord c, const ContinentalHydrologyResult& r) {
    const auto max_x = r.min_coord.x + static_cast<std::int64_t>(r.width_cells) - 1;
    const auto max_y = r.min_coord.y + static_cast<std::int64_t>(r.height_cells) - 1;
    return c.x >= r.min_coord.x && c.y >= r.min_coord.y && c.x <= max_x && c.y <= max_y;
}

std::size_t continent_index(CellCoord c, const ContinentalHydrologyResult& r) {
    if (!in_continent(c, r)) return kNoIndex;
    return flat(static_cast<std::uint32_t>(c.x - r.min_coord.x),
                static_cast<std::uint32_t>(c.y - r.min_coord.y), r.width_cells);
}

CellCoord continent_coord(std::size_t i, const ContinentalHydrologyResult& r) {
    const auto w = static_cast<std::size_t>(r.width_cells);
    return {r.min_coord.x + static_cast<std::int64_t>(i % w),
            r.min_coord.y + static_cast<std::int64_t>(i / w)};
}

bool continent_boundary(std::size_t i, const ContinentalHydrologyResult& r) {
    const auto w = static_cast<std::size_t>(r.width_cells);
    const auto x = i % w;
    const auto y = i / w;
    return x == 0 || y == 0 || x + 1 == r.width_cells || y + 1 == r.height_cells;
}

struct EdgeConnection {
    CellCoord source{};
    CellCoord destination{};
    bool valid{};
};

EdgeConnection choose_tile_connection(const World& world, CellCoord from_climate, CellCoord to_climate) {
    const auto ratio = static_cast<std::int64_t>(world.config().climate_cell_m / world.config().regional_cell_m);
    const auto dx = to_climate.x - from_climate.x;
    const auto dy = to_climate.y - from_climate.y;
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0)) {
        throw std::invalid_argument("continental downstream cells must be D8-adjacent");
    }
    const CellCoord base{from_climate.x * ratio, from_climate.y * ratio};
    EdgeConnection best;
    double best_cost = std::numeric_limits<double>::infinity();

    auto consider = [&](CellCoord src, CellCoord dst) {
        if (!rect_intersects_world(src, world.config().regional_cell_m, world.config().bounds) ||
            !rect_intersects_world(dst, world.config().regional_cell_m, world.config().bounds)) return;
        const auto src_e = world.sample_region(src).elevation_m;
        const auto dst_e = world.sample_region(dst).elevation_m;
        const double cost = std::max(static_cast<double>(src_e), static_cast<double>(dst_e));
        if (!best.valid || cost < best_cost ||
            (cost == best_cost && (src.y < best.source.y ||
             (src.y == best.source.y && src.x < best.source.x)))) {
            best = {src, dst, true};
            best_cost = cost;
        }
    };

    if (dx != 0 && dy != 0) {
        const CellCoord src{base.x + (dx > 0 ? ratio - 1 : 0),
                            base.y + (dy > 0 ? ratio - 1 : 0)};
        consider(src, {src.x + dx, src.y + dy});
    } else if (dx != 0) {
        const auto sx = base.x + (dx > 0 ? ratio - 1 : 0);
        for (std::int64_t j = 0; j < ratio; ++j) {
            const CellCoord src{sx, base.y + j};
            consider(src, {src.x + dx, src.y});
        }
    } else {
        const auto sy = base.y + (dy > 0 ? ratio - 1 : 0);
        for (std::int64_t j = 0; j < ratio; ++j) {
            const CellCoord src{base.x + j, sy};
            consider(src, {src.x, src.y + dy});
        }
    }
    return best;
}

bool regional_cell_touches_world_boundary(CellCoord c, const WorldConfig& cfg) {
    if (!rect_intersects_world(c, cfg.regional_cell_m, cfg.bounds)) return false;
    const double s = static_cast<double>(cfg.regional_cell_m);
    const double x0 = static_cast<double>(c.x) * s;
    const double y0 = static_cast<double>(c.y) * s;
    const double x1 = x0 + s;
    const double y1 = y0 + s;
    const double bx0 = cfg.bounds.origin_x_m;
    const double by0 = cfg.bounds.origin_y_m;
    const double bx1 = bx0 + cfg.bounds.width_m;
    const double by1 = by0 + cfg.bounds.height_m;
    return x0 <= bx0 || y0 <= by0 || x1 >= bx1 || y1 >= by1;
}

std::size_t local_index(CellCoord c, CellCoord min, std::uint32_t width, std::uint32_t height) {
    if (c.x < min.x || c.y < min.y) return kNoIndex;
    const auto x = c.x - min.x;
    const auto y = c.y - min.y;
    if (x >= width || y >= height) return kNoIndex;
    return flat(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), width);
}

} // namespace

void ContinentalHydrologyRequest::validate() const {
    if (!std::isfinite(river_threshold_m3_s) || river_threshold_m3_s < 0.0f) {
        throw std::invalid_argument("continental river threshold must be finite and non-negative");
    }
}

std::size_t ContinentalHydrologyResult::index_of(CellCoord coord) const {
    const auto i = continent_index(coord, *this);
    if (i == kNoIndex) throw std::out_of_range("climate coordinate is outside continental hydrology result");
    return i;
}

const ContinentalHydrologyCell& ContinentalHydrologyResult::cell(CellCoord coord) const {
    return cells.at(index_of(coord));
}

ContinentalHydrologyResult analyze_continental_hydrology(const World& world,
                                                         const ContinentalHydrologyRequest& request) {
    request.validate();
    const auto range = climate_range(world.config());
    const auto count = static_cast<std::size_t>(range.width) * static_cast<std::size_t>(range.height);

    ContinentalHydrologyResult result;
    result.config = world.config();
    result.min_coord = range.min;
    result.width_cells = range.width;
    result.height_cells = range.height;
    result.cells.resize(count);

    std::vector<double> terrain(count);
    std::vector<double> filled(count);
    std::vector<std::size_t> flood_parent(count, kNoIndex);
    std::vector<std::size_t> downstream(count, kNoIndex);
    std::vector<std::size_t> pop_rank(count, kNoIndex);
    std::vector<bool> visited(count, false);

    std::priority_queue<QueueNode, std::vector<QueueNode>, QueueGreater> queue;
    for (std::size_t i = 0; i < count; ++i) {
        const auto c = continent_coord(i, result);
        const auto p = overlap_center(c, world.config().climate_cell_m, world.config().bounds);
        const double h = world.sample_elevation(p);
        const bool ocean = h <= static_cast<double>(world.config().sea_level_m);
        const double surface = ocean ? static_cast<double>(world.config().sea_level_m) : h;
        terrain[i] = h;
        filled[i] = surface;

        auto& out = result.cells[i];
        out.coord = c;
        out.terrain_elevation_m = static_cast<float>(h);
        out.ocean = ocean;
        if (!ocean) {
            const auto climate = world.sample_climate(c);
            const auto area = overlap_area_m2(c, world.config().climate_cell_m, world.config().bounds);
            out.local_water_yield_m3_s = static_cast<float>(
                detail::water_yield_m3_s(detail::annual_water_surplus_mm(climate, h), area));
        }

        if (ocean || continent_boundary(i, result)) {
            visited[i] = true;
            queue.push({filled[i], i});
        }
    }

    std::size_t rank = 0;
    while (!queue.empty()) {
        const auto node = queue.top();
        queue.pop();
        pop_rank[node.index] = rank++;
        const auto c = continent_coord(node.index, result);
        for (int n = 0; n < 8; ++n) {
            const auto ni = continent_index({c.x + kDx[n], c.y + kDy[n]}, result);
            if (ni == kNoIndex || visited[ni]) continue;
            visited[ni] = true;
            flood_parent[ni] = node.index;
            filled[ni] = std::max(terrain[ni], filled[node.index]);
            queue.push({filled[ni], ni});
        }
    }
    if (rank != count) throw std::logic_error("continental Priority-Flood did not visit every cell");

    const double cell_m = static_cast<double>(world.config().climate_cell_m);
    for (std::size_t i = 0; i < count; ++i) {
        if (result.cells[i].ocean) continue;
        const auto c = continent_coord(i, result);
        double best_slope = 0.0;
        std::size_t best = kNoIndex;
        for (int n = 0; n < 8; ++n) {
            const auto ni = continent_index({c.x + kDx[n], c.y + kDy[n]}, result);
            if (ni == kNoIndex) continue;
            const double drop = filled[i] - filled[ni];
            if (!(drop > 0.0)) continue;
            const bool diagonal = kDx[n] != 0 && kDy[n] != 0;
            const double distance = diagonal ? cell_m * std::sqrt(2.0) : cell_m;
            const double slope = drop / distance;
            if (best == kNoIndex || slope > best_slope || (slope == best_slope && ni < best)) {
                best_slope = slope;
                best = ni;
            }
        }
        if (best == kNoIndex) best = flood_parent[i];
        downstream[i] = best; // Boundary land with no lower neighbor remains an external sink.
    }

    std::vector<std::size_t> by_rank(count);
    std::iota(by_rank.begin(), by_rank.end(), std::size_t{0});
    std::sort(by_rank.begin(), by_rank.end(), [&](std::size_t a, std::size_t b) {
        return pop_rank[a] > pop_rank[b];
    });

    std::vector<double> discharge(count);
    for (std::size_t i = 0; i < count; ++i) discharge[i] = result.cells[i].local_water_yield_m3_s;
    for (const auto i : by_rank) {
        if (downstream[i] != kNoIndex) discharge[downstream[i]] += discharge[i];
    }

    // Terminal IDs are coordinate-derived so they are stable for the same world topology.
    std::vector<std::uint64_t> basin(count, 0);
    std::vector<CellCoord> terminal(count);
    auto ascending = by_rank;
    std::reverse(ascending.begin(), ascending.end());
    for (const auto i : ascending) {
        if (downstream[i] == kNoIndex) {
            terminal[i] = result.cells[i].coord;
            basin[i] = static_cast<std::uint64_t>(i) + 1ULL;
        } else {
            terminal[i] = terminal[downstream[i]];
            basin[i] = basin[downstream[i]];
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        auto& out = result.cells[i];
        out.filled_elevation_m = static_cast<float>(filled[i]);
        out.depression_depth_m = static_cast<float>(std::max(0.0, filled[i] - terrain[i]));
        out.accumulated_discharge_m3_s = static_cast<float>(discharge[i]);
        out.terminal_outlet_coord = terminal[i];
        out.basin_id = basin[i];
        out.river = !out.ocean && out.accumulated_discharge_m3_s >= request.river_threshold_m3_s;
        if (downstream[i] != kNoIndex) {
            out.has_downstream = true;
            out.downstream_coord = result.cells[downstream[i]].coord;
        }
    }
    return result;
}

AuthoritativeHydrologyTile refine_authoritative_hydrology_tile(
    const World& world,
    const ContinentalHydrologyResult& continent,
    CellCoord climate_coord,
    float river_threshold_m3_s,
    float lake_min_depth_m) {

    if (!same_config_identity(world.config(), continent.config)) {
        throw std::invalid_argument("continental hydrology belongs to a different world configuration");
    }
    if (!std::isfinite(river_threshold_m3_s) || river_threshold_m3_s < 0.0f ||
        !std::isfinite(lake_min_depth_m) || lake_min_depth_m < 0.0f) {
        throw std::invalid_argument("regional hydrology thresholds must be finite and non-negative");
    }
    const auto& parent = continent.cell(climate_coord);
    const auto ratio_i64 = static_cast<std::int64_t>(world.config().climate_cell_m / world.config().regional_cell_m);
    if (ratio_i64 != 8) throw std::logic_error("authoritative hydrology currently requires an 8x8 L1 tile");
    constexpr std::uint32_t ratio = 8;

    AuthoritativeHydrologyTile tile;
    tile.config = world.config();
    tile.climate_coord = climate_coord;
    auto& result = tile.hydrology;
    result.request.min_coord = {climate_coord.x * ratio_i64, climate_coord.y * ratio_i64};
    result.request.width_cells = ratio;
    result.request.height_cells = ratio;
    result.request.river_threshold_m3_s = river_threshold_m3_s;
    result.request.lake_min_depth_m = lake_min_depth_m;
    result.cells.resize(ratio * ratio);

    const std::size_t count = result.cells.size();
    std::vector<double> terrain(count, 0.0);
    std::vector<double> filled(count, 0.0);
    std::vector<std::size_t> flood_parent(count, kNoIndex);
    std::vector<std::size_t> downstream(count, kNoIndex);
    std::vector<std::size_t> pop_rank(count, kNoIndex);
    std::vector<bool> active(count, false);
    std::vector<bool> visited(count, false);
    std::size_t active_count = 0;

    for (std::uint32_t y = 0; y < ratio; ++y) {
        for (std::uint32_t x = 0; x < ratio; ++x) {
            const auto i = flat(x, y, ratio);
            const CellCoord c{result.request.min_coord.x + x, result.request.min_coord.y + y};
            auto& out = result.cells[i];
            out.coord = c;
            if (!rect_intersects_world(c, world.config().regional_cell_m, world.config().bounds)) continue;
            active[i] = true;
            out.active = true;
            ++active_count;
            const auto sample = world.sample_region(c);
            terrain[i] = sample.elevation_m;
            filled[i] = terrain[i];
            out.terrain_elevation_m = sample.elevation_m;
            out.catchment_id = parent.basin_id;
            out.ocean = parent.ocean;
            if (parent.ocean) {
                filled[i] = world.config().sea_level_m;
                out.filled_elevation_m = world.config().sea_level_m;
            }
        }
    }
    if (active_count == 0) throw std::logic_error("continental cell has no active regional refinement cells");
    if (parent.ocean) return tile;

    std::size_t outlet = kNoIndex;
    CellCoord external_downstream{};
    bool has_external_downstream = false;
    if (parent.has_downstream) {
        const auto edge = choose_tile_connection(world, climate_coord, parent.downstream_coord);
        if (!edge.valid) throw std::logic_error("could not establish regional connection between continental cells");
        outlet = local_index(edge.source, result.request.min_coord, ratio, ratio);
        if (outlet == kNoIndex || !active[outlet]) throw std::logic_error("continental outlet is outside active regional tile");
        external_downstream = edge.destination;
        has_external_downstream = true;
    } else {
        double best_e = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < count; ++i) {
            if (!active[i] || !regional_cell_touches_world_boundary(result.cells[i].coord, world.config())) continue;
            if (outlet == kNoIndex || terrain[i] < best_e || (terrain[i] == best_e && i < outlet)) {
                outlet = i;
                best_e = terrain[i];
            }
        }
        if (outlet == kNoIndex) {
            throw std::logic_error("terminal continental land cell has no regional world-boundary outlet");
        }
    }

    std::priority_queue<QueueNode, std::vector<QueueNode>, QueueGreater> queue;
    visited[outlet] = true;
    queue.push({filled[outlet], outlet});
    std::size_t rank = 0;
    while (!queue.empty()) {
        const auto node = queue.top();
        queue.pop();
        pop_rank[node.index] = rank++;
        const auto c = result.cells[node.index].coord;
        for (int n = 0; n < 8; ++n) {
            const auto ni = local_index({c.x + kDx[n], c.y + kDy[n]}, result.request.min_coord, ratio, ratio);
            if (ni == kNoIndex || !active[ni] || visited[ni]) continue;
            visited[ni] = true;
            flood_parent[ni] = node.index;
            filled[ni] = std::max(terrain[ni], filled[node.index]);
            queue.push({filled[ni], ni});
        }
    }
    if (rank != active_count) throw std::logic_error("regional refinement tile is disconnected");

    const double cell_m = static_cast<double>(world.config().regional_cell_m);
    for (std::size_t i = 0; i < count; ++i) {
        if (!active[i] || i == outlet) continue;
        const auto c = result.cells[i].coord;
        double best_slope = 0.0;
        std::size_t best = kNoIndex;
        for (int n = 0; n < 8; ++n) {
            const auto ni = local_index({c.x + kDx[n], c.y + kDy[n]}, result.request.min_coord, ratio, ratio);
            if (ni == kNoIndex || !active[ni]) continue;
            const double drop = filled[i] - filled[ni];
            if (!(drop > 0.0)) continue;
            const bool diagonal = kDx[n] != 0 && kDy[n] != 0;
            const double distance = diagonal ? cell_m * std::sqrt(2.0) : cell_m;
            const double slope = drop / distance;
            if (best == kNoIndex || slope > best_slope || (slope == best_slope && ni < best)) {
                best = ni;
                best_slope = slope;
            }
        }
        if (best == kNoIndex) best = flood_parent[i];
        downstream[i] = best;
    }

    // Compute regional water-yield weights, then normalize exactly to the authoritative L0
    // local yield. This prevents refinement from creating or destroying water at level boundaries.
    std::vector<double> local_yield(count, 0.0);
    double raw_sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!active[i]) continue;
        const auto c = result.cells[i].coord;
        const auto climate = world.sample_climate(climate_coord);
        const auto area = overlap_area_m2(c, world.config().regional_cell_m, world.config().bounds);
        local_yield[i] = detail::water_yield_m3_s(
            detail::annual_water_surplus_mm(climate, terrain[i]), area);
        raw_sum += local_yield[i];
    }
    if (raw_sum > 0.0) {
        const double scale = static_cast<double>(parent.local_water_yield_m3_s) / raw_sum;
        for (auto& value : local_yield) value *= scale;
    } else if (parent.local_water_yield_m3_s > 0.0f) {
        const double each = static_cast<double>(parent.local_water_yield_m3_s) / static_cast<double>(active_count);
        for (std::size_t i = 0; i < count; ++i) if (active[i]) local_yield[i] = each;
    }

    std::vector<double> discharge = local_yield;
    // Immediate upstream L0 cells inject their complete accumulated discharge at the exact
    // regional edge cell chosen by the same deterministic connection rule.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const CellCoord uc{climate_coord.x + dx, climate_coord.y + dy};
            if (!in_continent(uc, continent)) continue;
            const auto& upstream = continent.cell(uc);
            if (!upstream.has_downstream || upstream.downstream_coord != climate_coord) continue;
            const auto edge = choose_tile_connection(world, uc, climate_coord);
            if (!edge.valid) throw std::logic_error("could not reconstruct upstream regional connection");
            const auto ingress = local_index(edge.destination, result.request.min_coord, ratio, ratio);
            if (ingress == kNoIndex || !active[ingress]) throw std::logic_error("upstream regional ingress is outside tile");
            discharge[ingress] += upstream.accumulated_discharge_m3_s;
        }
    }

    std::vector<std::size_t> by_rank;
    by_rank.reserve(active_count);
    for (std::size_t i = 0; i < count; ++i) if (active[i]) by_rank.push_back(i);
    std::sort(by_rank.begin(), by_rank.end(), [&](std::size_t a, std::size_t b) {
        return pop_rank[a] > pop_rank[b];
    });
    for (const auto i : by_rank) {
        if (downstream[i] != kNoIndex) discharge[downstream[i]] += discharge[i];
    }

    if (has_external_downstream) {
        result.cells[outlet].has_downstream = true;
        result.cells[outlet].downstream_is_external = true;
        result.cells[outlet].downstream_coord = external_downstream;
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (!active[i]) continue;
        auto& out = result.cells[i];
        out.filled_elevation_m = static_cast<float>(filled[i]);
        out.depression_depth_m = static_cast<float>(std::max(0.0, filled[i] - terrain[i]));
        out.local_water_yield_m3_s = static_cast<float>(local_yield[i]);
        out.accumulated_discharge_m3_s = static_cast<float>(discharge[i]);
        if (downstream[i] != kNoIndex) {
            out.has_downstream = true;
            out.downstream_coord = result.cells[downstream[i]].coord;
        }
    }

    // Stable per-tile lakes.
    std::vector<bool> depression(count, false), seen(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        depression[i] = active[i] && result.cells[i].depression_depth_m > 1e-6f;
    }
    std::vector<std::size_t> stack;
    for (std::size_t start = 0; start < count; ++start) {
        if (!depression[start] || seen[start]) continue;
        stack = {start};
        seen[start] = true;
        std::vector<std::size_t> members;
        LakeInfo lake;
        lake.surface_elevation_m = -std::numeric_limits<float>::infinity();
        std::size_t anchor = start;
        while (!stack.empty()) {
            const auto i = stack.back(); stack.pop_back();
            members.push_back(i);
            anchor = std::min(anchor, i);
            ++lake.cell_count;
            const double member_area_m2 = overlap_area_m2(
                result.cells[i].coord, world.config().regional_cell_m, world.config().bounds);
            lake.area_m2 += member_area_m2;
            lake.volume_m3 += static_cast<double>(result.cells[i].depression_depth_m) * member_area_m2;
            lake.max_depth_m = std::max(lake.max_depth_m, result.cells[i].depression_depth_m);
            lake.surface_elevation_m = std::max(lake.surface_elevation_m, result.cells[i].filled_elevation_m);
            const auto c = result.cells[i].coord;
            for (int n = 0; n < 8; ++n) {
                const auto ni = local_index({c.x + kDx[n], c.y + kDy[n]}, result.request.min_coord, ratio, ratio);
                if (ni == kNoIndex || seen[ni] || !depression[ni]) continue;
                if (std::abs(filled[ni] - filled[i]) > 1e-5) continue;
                seen[ni] = true;
                stack.push_back(ni);
            }
        }
        if (lake.max_depth_m + 1e-6f < lake_min_depth_m) continue;
        const auto parent_index = continent.index_of(climate_coord);
        lake.id = static_cast<std::uint64_t>(parent_index) * 64ULL +
                  static_cast<std::uint64_t>(anchor) + 1ULL;
        std::vector<bool> member_mask(count, false);
        for (const auto i : members) { member_mask[i] = true; result.cells[i].lake_id = lake.id; }
        std::size_t lake_outlet = kNoIndex;
        for (const auto i : members) {
            const auto d = downstream[i];
            const bool leaves_local_component = d == kNoIndex || !member_mask[d];
            const bool leaves_external = i == outlet && has_external_downstream;
            if (leaves_local_component || leaves_external) {
                if (lake_outlet == kNoIndex || i < lake_outlet) lake_outlet = i;
            }
        }
        if (lake_outlet == kNoIndex) lake_outlet = members.front();
        lake.outlet_coord = result.cells[lake_outlet].coord;
        if (downstream[lake_outlet] != kNoIndex) {
            lake.has_outflow = true;
            lake.outflow_coord = result.cells[downstream[lake_outlet]].coord;
        } else if (lake_outlet == outlet && has_external_downstream) {
            lake.has_outflow = true;
            lake.outflow_coord = external_downstream;
        }
        result.lakes.push_back(lake);
    }

    for (std::size_t i = 0; i < count; ++i) {
        auto& cell = result.cells[i];
        if (!cell.active) continue;
        cell.river = cell.lake_id == 0 && cell.accumulated_discharge_m3_s >= river_threshold_m3_s;
        if (!cell.river || !cell.has_downstream) continue;
        result.river_segments.push_back({cell.coord, cell.downstream_coord, cell.accumulated_discharge_m3_s});
    }
    return tile;
}

ContinentalHydrologyResult World::analyze_continental_hydrology(const ContinentalHydrologyRequest& request) const {
    return worldsim::analyze_continental_hydrology(*this, request);
}

AuthoritativeHydrologyTile World::refine_authoritative_hydrology_tile(
    const ContinentalHydrologyResult& continent, CellCoord climate_coord,
    float river_threshold_m3_s, float lake_min_depth_m) const {
    return worldsim::refine_authoritative_hydrology_tile(
        *this, continent, climate_coord, river_threshold_m3_s, lake_min_depth_m);
}

} // namespace worldsim
