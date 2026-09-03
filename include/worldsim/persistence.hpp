#pragma once

#include "worldsim/types.hpp"

#include <filesystem>
#include <unordered_map>

namespace worldsim::persistence {

void save_world(const std::filesystem::path& path,
                const WorldConfig& config,
                const std::unordered_map<CellCoord, LocalPatch, CellCoordHash>& patches);

void load_world(const std::filesystem::path& path,
                WorldConfig& config,
                std::unordered_map<CellCoord, LocalPatch, CellCoordHash>& patches);

} // namespace worldsim::persistence
