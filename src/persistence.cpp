#include "worldsim/persistence.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace worldsim::persistence {
namespace {

constexpr std::array<char, 8> kMagic{'W','S','I','M','0','0','0','1'};
constexpr std::uint32_t kFormatVersion = 3;

template <typename T>
void write_pod(std::ostream& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    if (!out) throw std::runtime_error("failed to write world file");
}

template <typename T>
void read_pod(std::istream& in, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    if (!in) throw std::runtime_error("failed to read world file");
}


bool region_intersects_bounds(CellCoord coord, const WorldConfig& config) {
    const double s = static_cast<double>(config.regional_cell_m);
    const double x0 = static_cast<double>(coord.x) * s;
    const double y0 = static_cast<double>(coord.y) * s;
    const double x1 = x0 + s;
    const double y1 = y0 + s;
    const double bx0 = config.bounds.origin_x_m;
    const double by0 = config.bounds.origin_y_m;
    const double bx1 = bx0 + config.bounds.width_m;
    const double by1 = by0 + config.bounds.height_m;
    return x1 > bx0 && x0 < bx1 && y1 > by0 && y0 < by1;
}

bool local_cell_intersects_bounds(
    CellCoord regional_coord,
    std::size_t index,
    const WorldConfig& config) {
    const std::size_t lx = index % kLocalCellsPerAxis;
    const std::size_t ly = index / kLocalCellsPerAxis;
    const double local = static_cast<double>(config.local_cell_m);
    const double region_x0 = static_cast<double>(regional_coord.x) * config.regional_cell_m;
    const double region_y0 = static_cast<double>(regional_coord.y) * config.regional_cell_m;
    const double x0 = region_x0 + static_cast<double>(lx) * local;
    const double y0 = region_y0 + static_cast<double>(ly) * local;
    const double x1 = x0 + local;
    const double y1 = y0 + local;
    return x1 > config.bounds.origin_x_m &&
        x0 < config.bounds.origin_x_m + config.bounds.width_m &&
        y1 > config.bounds.origin_y_m &&
        y0 < config.bounds.origin_y_m + config.bounds.height_m;
}

void validate_cell(const LocalCell& cell, const WorldConfig& config) {
    if (!std::isfinite(cell.elevation_m) || !std::isfinite(cell.terrain_roughness) ||
        !std::isfinite(cell.forest_potential) || !std::isfinite(cell.disturbance) ||
        !std::isfinite(cell.vegetation_biomass)) {
        throw std::runtime_error("world file contains non-finite local cell data");
    }
    const auto in_unit = [](float v) { return v >= 0.0f && v <= 1.0f; };
    if (!in_unit(cell.terrain_roughness) || !in_unit(cell.forest_potential) ||
        !in_unit(cell.disturbance) || !in_unit(cell.vegetation_biomass)) {
        throw std::runtime_error("world file contains invalid normalized local cell data");
    }
    const double disturbed_capacity =
        static_cast<double>(cell.forest_potential) * (1.0 - cell.disturbance);
    if (static_cast<double>(cell.vegetation_biomass) > disturbed_capacity + 2.0e-6) {
        throw std::runtime_error("world file vegetation biomass exceeds disturbed local potential");
    }
    if (cell.elevation_m <= config.sea_level_m && cell.vegetation_biomass != 0.0f) {
        throw std::runtime_error("world file contains vegetation below sea level");
    }
}

} // namespace

void save_world(const std::filesystem::path& path,
                const WorldConfig& config,
                const std::unordered_map<CellCoord, LocalPatch, CellCoordHash>& patches) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open world file for writing: " + path.string());

    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    write_pod(out, kFormatVersion);
    write_pod(out, config.seed);
    write_pod(out, config.bounds.origin_x_m);
    write_pod(out, config.bounds.origin_y_m);
    write_pod(out, config.bounds.width_m);
    write_pod(out, config.bounds.height_m);
    write_pod(out, config.local_cell_m);
    write_pod(out, config.regional_cell_m);
    write_pod(out, config.climate_cell_m);
    write_pod(out, config.sea_level_m);

    const auto count = static_cast<std::uint64_t>(patches.size());
    write_pod(out, count);

    // Canonical ordering keeps identical world states byte-for-byte reproducible.
    using PatchEntry = std::pair<CellCoord, const LocalPatch*>;
    std::vector<PatchEntry> ordered;
    ordered.reserve(patches.size());
    for (const auto& entry : patches) {
        if (entry.first != entry.second.regional_coord) {
            throw std::runtime_error("internal local patch coordinate invariant is broken");
        }
        ordered.push_back({entry.first, &entry.second});
    }
    std::sort(ordered.begin(), ordered.end(), [](const PatchEntry& a, const PatchEntry& b) {
        if (a.first.y != b.first.y) return a.first.y < b.first.y;
        return a.first.x < b.first.x;
    });

    for (const auto& entry : ordered) {
        write_pod(out, entry.first.x);
        write_pod(out, entry.first.y);
        for (const LocalCell& cell : entry.second->cells) {
            write_pod(out, cell.elevation_m);
            write_pod(out, cell.terrain_roughness);
            validate_cell(cell, config);
            write_pod(out, cell.forest_potential);
            write_pod(out, cell.disturbance);
            write_pod(out, cell.vegetation_biomass);
        }
    }
}

void load_world(const std::filesystem::path& path,
                WorldConfig& config,
                std::unordered_map<CellCoord, LocalPatch, CellCoordHash>& patches) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open world file for reading: " + path.string());

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) throw std::runtime_error("invalid WorldSim file magic");

    std::uint32_t version{};
    read_pod(in, version);
    if (version != 1 && version != 2 && version != kFormatVersion) {
        throw std::runtime_error("unsupported WorldSim file version");
    }

    read_pod(in, config.seed);
    read_pod(in, config.bounds.origin_x_m);
    read_pod(in, config.bounds.origin_y_m);
    read_pod(in, config.bounds.width_m);
    read_pod(in, config.bounds.height_m);
    read_pod(in, config.local_cell_m);
    read_pod(in, config.regional_cell_m);
    read_pod(in, config.climate_cell_m);
    if (version >= 2) {
        read_pod(in, config.sea_level_m);
    } else {
        config.sea_level_m = 0.0f;
    }
    config.validate();

    std::uint64_t count{};
    read_pod(in, count);
    const std::uint64_t floats_per_cell = version >= 3u ? 5ULL : 4ULL;
    const std::uint64_t kPatchRecordBytes = sizeof(std::int64_t) * 2ULL +
        static_cast<std::uint64_t>(kLocalCellCount) * sizeof(float) * floats_per_cell;
    const auto file_size = std::filesystem::file_size(path);
    const auto current = in.tellg();
    if (current < 0) throw std::runtime_error("failed to inspect world file");
    const auto current_u = static_cast<std::uint64_t>(current);
    if (current_u > file_size) throw std::runtime_error("world file position exceeds file size");
    const auto remaining = file_size - current_u;
    if (count > remaining / kPatchRecordBytes) {
        throw std::runtime_error("world file patch count exceeds available data");
    }
    if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("world file patch count exceeds addressable container size");
    }

    patches.clear();
    patches.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t p = 0; p < count; ++p) {
        LocalPatch patch;
        read_pod(in, patch.regional_coord.x);
        read_pod(in, patch.regional_coord.y);
        if (!region_intersects_bounds(patch.regional_coord, config)) {
            throw std::runtime_error("world file contains a local patch outside world bounds");
        }
        for (std::size_t i = 0; i < patch.cells.size(); ++i) {
            auto& cell = patch.cells[i];
            read_pod(in, cell.elevation_m);
            read_pod(in, cell.terrain_roughness);
            read_pod(in, cell.forest_potential);
            read_pod(in, cell.disturbance);
            if (version >= 3u) {
                read_pod(in, cell.vegetation_biomass);
            } else {
                cell.vegetation_biomass =
                    local_cell_intersects_bounds(patch.regional_coord, i, config) &&
                    cell.elevation_m > config.sea_level_m
                        ? cell.forest_potential * (1.0f - cell.disturbance)
                        : 0.0f;
            }
            validate_cell(cell, config);
        }
        const auto [it, inserted] = patches.emplace(patch.regional_coord, patch);
        (void)it;
        if (!inserted) throw std::runtime_error("world file contains duplicate local patch coordinates");
    }

    // Versions 1-3 have no optional tail sections. Extra bytes indicate corruption or a file version
    // that this loader does not understand and must not silently accept.
    char trailing{};
    if (in.read(&trailing, 1)) {
        throw std::runtime_error("world file contains unexpected trailing data");
    }
    if (!in.eof()) {
        throw std::runtime_error("failed while validating world file end");
    }
}

} // namespace worldsim::persistence
