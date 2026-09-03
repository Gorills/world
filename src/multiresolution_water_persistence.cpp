#include "worldsim/multiresolution_water.hpp"

#include "worldsim/world.hpp"
#include "soil_hydrology_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace worldsim {
namespace {

constexpr std::array<char, 8> kMagic{'W','S','M','W','0','0','0','1'};
constexpr std::uint32_t kFormatVersion = 3;
constexpr double kMaxWaterDepthMm = 1.0e30;

bool same_bounds(const WorldBounds& a, const WorldBounds& b) {
    return a.origin_x_m == b.origin_x_m && a.origin_y_m == b.origin_y_m &&
           a.width_m == b.width_m && a.height_m == b.height_m;
}

bool same_config_identity(const WorldConfig& a, const WorldConfig& b) {
    return a.seed == b.seed && same_bounds(a.bounds, b.bounds) &&
           a.local_cell_m == b.local_cell_m && a.regional_cell_m == b.regional_cell_m &&
           a.climate_cell_m == b.climate_cell_m && a.sea_level_m == b.sea_level_m;
}

bool finite_non_negative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

bool finite_non_negative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

double stored_depth_mm(const ContinentalWaterCellState& c) {
    return static_cast<double>(c.snow_water_equivalent_mm) + c.surface_water_mm +
           c.soil_water_mm + c.groundwater_mm;
}

double stored_depth_mm(const DynamicHydrologyCellState& c) {
    return static_cast<double>(c.snow_water_equivalent_mm) + c.surface_water_mm +
           c.soil_water_mm + c.groundwater_mm;
}

void validate_cell(const ContinentalWaterCellState& c) {
    if (!finite_non_negative(c.snow_water_equivalent_mm) ||
        !finite_non_negative(c.surface_water_mm) ||
        !finite_non_negative(c.soil_water_mm) ||
        !finite_non_negative(c.groundwater_mm) ||
        !finite_non_negative(c.last_evapotranspiration_mm) ||
        !finite_non_negative(c.last_quick_runoff_mm) ||
        !finite_non_negative(c.last_baseflow_mm) ||
        !finite_non_negative(c.last_routed_discharge_m3_s) ||
        !std::isfinite(stored_depth_mm(c)) || stored_depth_mm(c) > kMaxWaterDepthMm) {
        throw std::runtime_error("multiresolution water file contains invalid coarse cell state");
    }
}

void validate_cell(const DynamicHydrologyCellState& c) {
    if (!finite_non_negative(c.snow_water_equivalent_mm) ||
        !finite_non_negative(c.surface_water_mm) ||
        !finite_non_negative(c.soil_water_mm) ||
        !finite_non_negative(c.groundwater_mm) ||
        !finite_non_negative(c.last_evapotranspiration_mm) ||
        !finite_non_negative(c.last_quick_runoff_mm) ||
        !finite_non_negative(c.last_baseflow_mm) ||
        !finite_non_negative(c.last_routed_discharge_m3_s) ||
        !std::isfinite(stored_depth_mm(c)) || stored_depth_mm(c) > kMaxWaterDepthMm) {
        throw std::runtime_error("multiresolution water file contains invalid refined cell state");
    }
}

template <typename T>
void write_pod(std::ostream& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    if (!out) throw std::runtime_error("failed to write multiresolution water file");
}

template <typename T>
void read_pod(std::istream& in, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    if (!in) throw std::runtime_error("failed to read multiresolution water file");
}

void write_config(std::ostream& out, const WorldConfig& cfg) {
    write_pod(out, cfg.seed);
    write_pod(out, cfg.bounds.origin_x_m);
    write_pod(out, cfg.bounds.origin_y_m);
    write_pod(out, cfg.bounds.width_m);
    write_pod(out, cfg.bounds.height_m);
    write_pod(out, cfg.local_cell_m);
    write_pod(out, cfg.regional_cell_m);
    write_pod(out, cfg.climate_cell_m);
    write_pod(out, cfg.sea_level_m);
}

WorldConfig read_config(std::istream& in) {
    WorldConfig cfg;
    read_pod(in, cfg.seed);
    read_pod(in, cfg.bounds.origin_x_m);
    read_pod(in, cfg.bounds.origin_y_m);
    read_pod(in, cfg.bounds.width_m);
    read_pod(in, cfg.bounds.height_m);
    read_pod(in, cfg.local_cell_m);
    read_pod(in, cfg.regional_cell_m);
    read_pod(in, cfg.climate_cell_m);
    read_pod(in, cfg.sea_level_m);
    cfg.validate();
    return cfg;
}

void write_parameters(std::ostream& out, const DynamicHydrologyParameters& p) {
    write_pod(out, p.soil_capacity_mm);
    write_pod(out, p.field_capacity_mm);
    write_pod(out, p.wilting_point_mm);
    write_pod(out, p.infiltration_capacity_mm_per_day);
    write_pod(out, p.surface_storage_capacity_mm);
    write_pod(out, p.percolation_rate_per_day);
    write_pod(out, p.groundwater_recession_per_day);
    write_pod(out, p.snow_melt_mm_per_c_day);
    write_pod(out, p.initial_soil_water_mm);
    write_pod(out, p.initial_groundwater_mm);
}

DynamicHydrologyParameters read_parameters(std::istream& in) {
    DynamicHydrologyParameters p;
    read_pod(in, p.soil_capacity_mm);
    read_pod(in, p.field_capacity_mm);
    read_pod(in, p.wilting_point_mm);
    read_pod(in, p.infiltration_capacity_mm_per_day);
    read_pod(in, p.surface_storage_capacity_mm);
    read_pod(in, p.percolation_rate_per_day);
    read_pod(in, p.groundwater_recession_per_day);
    read_pod(in, p.snow_melt_mm_per_c_day);
    read_pod(in, p.initial_soil_water_mm);
    read_pod(in, p.initial_groundwater_mm);
    p.validate();
    return p;
}

void write_cell(std::ostream& out, const ContinentalWaterCellState& c) {
    write_pod(out, c.snow_water_equivalent_mm);
    write_pod(out, c.surface_water_mm);
    write_pod(out, c.soil_water_mm);
    write_pod(out, c.groundwater_mm);
    write_pod(out, c.last_evapotranspiration_mm);
    write_pod(out, c.last_quick_runoff_mm);
    write_pod(out, c.last_baseflow_mm);
    write_pod(out, c.last_routed_discharge_m3_s);
}

ContinentalWaterCellState read_coarse_cell(std::istream& in) {
    ContinentalWaterCellState c;
    read_pod(in, c.snow_water_equivalent_mm);
    read_pod(in, c.surface_water_mm);
    read_pod(in, c.soil_water_mm);
    read_pod(in, c.groundwater_mm);
    read_pod(in, c.last_evapotranspiration_mm);
    read_pod(in, c.last_quick_runoff_mm);
    read_pod(in, c.last_baseflow_mm);
    read_pod(in, c.last_routed_discharge_m3_s);
    validate_cell(c);
    return c;
}

void write_cell(std::ostream& out, const DynamicHydrologyCellState& c) {
    write_pod(out, c.coord.x);
    write_pod(out, c.coord.y);
    const std::uint8_t active = c.active ? 1u : 0u;
    write_pod(out, active);
    write_pod(out, c.snow_water_equivalent_mm);
    write_pod(out, c.surface_water_mm);
    write_pod(out, c.soil_water_mm);
    write_pod(out, c.groundwater_mm);
    write_pod(out, c.last_evapotranspiration_mm);
    write_pod(out, c.last_quick_runoff_mm);
    write_pod(out, c.last_baseflow_mm);
    write_pod(out, c.last_routed_discharge_m3_s);
}

DynamicHydrologyCellState read_refined_cell(std::istream& in) {
    DynamicHydrologyCellState c;
    read_pod(in, c.coord.x);
    read_pod(in, c.coord.y);
    std::uint8_t active{};
    read_pod(in, active);
    if (active > 1u) throw std::runtime_error("multiresolution water file contains invalid active flag");
    c.active = active != 0u;
    read_pod(in, c.snow_water_equivalent_mm);
    read_pod(in, c.surface_water_mm);
    read_pod(in, c.soil_water_mm);
    read_pod(in, c.groundwater_mm);
    read_pod(in, c.last_evapotranspiration_mm);
    read_pod(in, c.last_quick_runoff_mm);
    read_pod(in, c.last_baseflow_mm);
    read_pod(in, c.last_routed_discharge_m3_s);
    validate_cell(c);
    return c;
}

bool zero_stores(const ContinentalWaterCellState& c) {
    return c.snow_water_equivalent_mm == 0.0f && c.surface_water_mm == 0.0f &&
           c.soil_water_mm == 0.0f && c.groundwater_mm == 0.0f;
}

} // namespace

void save_multiresolution_water_state(
    const MultiresolutionWaterState& state,
    const std::filesystem::path& path) {
    state.parameters_.validate();
    if (state.simulated_day() < 0) throw std::runtime_error("cannot save a negative multiresolution simulation day");
    if (state.channel_storage_m3_.size() != state.coarse_.cells_.size()) {
        throw std::runtime_error("cannot save inconsistent channel storage shape");
    }
    World soil_world(state.config());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open multiresolution water file for writing: " + path.string());
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    if (!out) throw std::runtime_error("failed to write multiresolution water file magic");
    write_pod(out, kFormatVersion);
    write_config(out, state.config());
    write_parameters(out, state.parameters_);
    write_pod(out, state.coarse_.simulated_day_);
    write_pod(out, state.coarse_.min_coord_.x);
    write_pod(out, state.coarse_.min_coord_.y);
    write_pod(out, state.coarse_.width_cells_);
    write_pod(out, state.coarse_.height_cells_);
    const auto coarse_count = static_cast<std::uint64_t>(state.coarse_.cells_.size());
    write_pod(out, coarse_count);
    for (std::size_t i = 0; i < state.coarse_.cells_.size(); ++i) {
        const auto& cell = state.coarse_.cells_[i];
        validate_cell(cell);
        if ((state.coarse_.metadata_[i].flags & 1u) == 0u) {
            const SoilProperties soil_properties{
                state.coarse_.metadata_[i].soil_storage_capacity_scale,
                state.coarse_.metadata_[i].soil_infiltration_capacity_scale};
            const auto soil = detail::scaled_soil_bucket_parameters(state.parameters_, soil_properties);
            if (!detail::soil_water_within_capacity(cell.soil_water_mm, soil.soil_capacity_mm)) {
                throw std::runtime_error("coarse soil water exceeds saved local soil capacity");
            }
        }
        write_cell(out, cell);
    }

    write_pod(out, coarse_count);
    for (std::size_t i = 0; i < state.channel_storage_m3_.size(); ++i) {
        const double volume = state.channel_storage_m3_[i];
        if (!finite_non_negative(volume) ||
            ((state.coarse_.metadata_[i].flags & 1u) != 0u && volume != 0.0)) {
            throw std::runtime_error("multiresolution channel storage is invalid for persistence");
        }
        write_pod(out, volume);
    }

    std::vector<const MultiresolutionWaterState::RefinedTile*> ordered;
    ordered.reserve(state.refined_.size());
    for (const auto& entry : state.refined_) ordered.push_back(&entry.second);
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        if (a->state.climate_coord.y != b->state.climate_coord.y) {
            return a->state.climate_coord.y < b->state.climate_coord.y;
        }
        return a->state.climate_coord.x < b->state.climate_coord.x;
    });
    const auto refined_count = static_cast<std::uint64_t>(ordered.size());
    write_pod(out, refined_count);
    for (const auto* refined : ordered) {
        if (!same_config_identity(refined->state.config, state.config()) ||
            refined->state.simulated_day != state.simulated_day() || refined->state.cells.size() != 64) {
            throw std::runtime_error("refined water ownership is internally inconsistent");
        }
        const auto parent_index = state.coarse_.index_of(refined->state.climate_coord);
        if (!zero_stores(state.coarse_.cells_[parent_index])) {
            throw std::runtime_error("refined parent still owns independent coarse water");
        }
        write_pod(out, refined->state.climate_coord.x);
        write_pod(out, refined->state.climate_coord.y);
        write_pod(out, refined->state.simulated_day);
        const auto child_count = static_cast<std::uint32_t>(refined->state.cells.size());
        write_pod(out, child_count);
        for (std::size_t i = 0; i < refined->state.cells.size(); ++i) {
            const auto& cell = refined->state.cells[i];
            validate_cell(cell);
            const auto& expected = refined->topology.hydrology.cells[i];
            if (cell.coord != expected.coord || cell.active != expected.active) {
                throw std::runtime_error("refined cell topology does not match its authoritative tile");
            }
            if (cell.active && !expected.ocean) {
                const auto soil = detail::scaled_soil_bucket_parameters(
                    state.parameters_, soil_world.sample_soil(cell.coord));
                if (!detail::soil_water_within_capacity(cell.soil_water_mm, soil.soil_capacity_mm)) {
                    throw std::runtime_error("refined soil water exceeds saved local soil capacity");
                }
            }
            write_cell(out, cell);
        }
    }
}

MultiresolutionWaterState load_multiresolution_water_state(
    const World& world,
    const ContinentalHydrologyResult& topology,
    const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open multiresolution water file for reading: " + path.string());

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) throw std::runtime_error("invalid multiresolution water file magic");
    std::uint32_t version{};
    read_pod(in, version);
    if (version == 1u) {
        throw std::runtime_error(
            "multiresolution water file v1 uses uniform soil-capacity semantics and is not compatible with v2+");
    }
    if (version != 2u && version != kFormatVersion) {
        throw std::runtime_error("unsupported multiresolution water file version");
    }

    const auto saved_config = read_config(in);
    if (!same_config_identity(saved_config, world.config()) ||
        !same_config_identity(topology.config, world.config())) {
        throw std::runtime_error("multiresolution water file belongs to a different world");
    }
    const auto parameters = read_parameters(in);
    auto state = make_multiresolution_water_state(world, topology, parameters);

    std::int64_t day{};
    CellCoord min_coord{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t coarse_count{};
    read_pod(in, day);
    read_pod(in, min_coord.x);
    read_pod(in, min_coord.y);
    read_pod(in, width);
    read_pod(in, height);
    read_pod(in, coarse_count);
    if (day < 0 || min_coord != state.coarse_.min_coord_ || width != state.coarse_.width_cells_ ||
        height != state.coarse_.height_cells_ || coarse_count != state.coarse_.cells_.size()) {
        throw std::runtime_error("multiresolution water file coarse state metadata is inconsistent");
    }
    state.set_simulated_day(day);
    for (std::size_t i = 0; i < state.coarse_.cells_.size(); ++i) {
        auto cell = read_coarse_cell(in);
        if (!topology.cells[i].ocean) {
            const SoilProperties soil_properties{
                state.coarse_.metadata_[i].soil_storage_capacity_scale,
                state.coarse_.metadata_[i].soil_infiltration_capacity_scale};
            const auto soil = detail::scaled_soil_bucket_parameters(parameters, soil_properties);
            if (!detail::soil_water_within_capacity(cell.soil_water_mm, soil.soil_capacity_mm)) {
                throw std::runtime_error("saved coarse soil water exceeds local soil capacity");
            }
        }
        if (topology.cells[i].ocean && !zero_stores(cell)) {
            throw std::runtime_error("saved ocean coarse cell contains terrestrial water");
        }
        state.coarse_cell_mutable(i) = cell;
    }

    // v2 had no retained channel state: all routed volume had either reached a bucket/refined
    // tile or terminal outflow at the checkpoint boundary. Migrating it to zero channel storage
    // is therefore the only non-invented initial channel state.
    if (version >= 3u) {
        std::uint64_t channel_count{};
        read_pod(in, channel_count);
        if (channel_count != coarse_count) {
            throw std::runtime_error("multiresolution water file channel count does not match L0 cells");
        }
        for (std::size_t i = 0; i < state.channel_storage_m3_.size(); ++i) {
            double volume{};
            read_pod(in, volume);
            if (!finite_non_negative(volume) || (topology.cells[i].ocean && volume != 0.0)) {
                throw std::runtime_error("multiresolution water file contains invalid channel storage");
            }
            state.channel_storage_m3_[i] = volume;
        }
    }

    std::uint64_t refined_count{};
    read_pod(in, refined_count);
    if (refined_count > coarse_count) {
        throw std::runtime_error("multiresolution water file refined tile count exceeds L0 cells");
    }
    state.refined_.reserve(static_cast<std::size_t>(refined_count));
    for (std::uint64_t r = 0; r < refined_count; ++r) {
        CellCoord parent{};
        std::int64_t tile_day{};
        std::uint32_t child_count{};
        read_pod(in, parent.x);
        read_pod(in, parent.y);
        read_pod(in, tile_day);
        read_pod(in, child_count);
        if (tile_day != day || child_count != 64) {
            throw std::runtime_error("saved refined tile clock or cell count is invalid");
        }
        const auto parent_index = state.coarse_.index_of(parent);
        if (!zero_stores(state.coarse_.cells_[parent_index])) {
            throw std::runtime_error("saved refined parent also owns coarse water");
        }
        auto authoritative = world.refine_authoritative_hydrology_tile(topology, parent);
        MultiresolutionWaterState::RefinedTile refined;
        refined.topology = std::move(authoritative);
        refined.state.config = world.config();
        refined.state.climate_coord = parent;
        refined.state.simulated_day = tile_day;
        refined.state.cells.resize(child_count);
        for (std::size_t i = 0; i < refined.state.cells.size(); ++i) {
            auto cell = read_refined_cell(in);
            const auto& expected = refined.topology.hydrology.cells[i];
            if (cell.coord != expected.coord || cell.active != expected.active) {
                throw std::runtime_error("saved refined cell does not match authoritative topology");
            }
            if (cell.active && !expected.ocean) {
                const auto soil = detail::scaled_soil_bucket_parameters(
                    parameters, world.sample_soil(expected.coord));
                if (!detail::soil_water_within_capacity(cell.soil_water_mm, soil.soil_capacity_mm)) {
                    throw std::runtime_error("saved refined soil water exceeds local soil capacity");
                }
            }
            if (expected.ocean && stored_depth_mm(cell) != 0.0) {
                throw std::runtime_error("saved refined ocean cell contains terrestrial water");
            }
            refined.state.cells[i] = cell;
        }
        const auto [it, inserted] = state.refined_.emplace(parent, std::move(refined));
        (void)it;
        if (!inserted) throw std::runtime_error("multiresolution water file contains duplicate refined parents");
    }

    char trailing{};
    if (in.read(&trailing, 1)) {
        throw std::runtime_error("multiresolution water file contains unexpected trailing data");
    }
    if (!in.eof()) throw std::runtime_error("failed while validating multiresolution water file end");
    return state;
}

} // namespace worldsim
