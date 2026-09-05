#include "worldsim/simulation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace worldsim {
namespace {

constexpr std::array<char, 8> kMagic{'W','S','S','I','M','0','0','1'};
constexpr std::uint32_t kLegacyFormatVersion = 1;
constexpr std::uint32_t kFormatVersion = 3;
constexpr std::uint32_t kLegacySectionCount = 3;
constexpr std::uint32_t kSectionCount = 5;
constexpr std::uint32_t kEcosystemSection = 5;
constexpr std::uint32_t kWorldSection = 1;
constexpr std::uint32_t kWeatherSection = 2;
constexpr std::uint32_t kWaterSection = 3;
constexpr std::uint32_t kSettlementSection = 4;
constexpr std::array<char, 8> kSettlementMagic{'W','S','S','E','T','0','0','1'};
constexpr std::uint32_t kSettlementFormatVersion = 1;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kIoBufferBytes = 64 * 1024;
constexpr std::uint64_t kSettlementRecordBytes =
    sizeof(SettlementId) + sizeof(std::int64_t) * 3 + sizeof(double);

struct SectionDescriptor {
    std::uint32_t id{};
    std::uint64_t size{};
    std::uint64_t checksum{};
};

struct CheckpointHeader {
    std::uint32_t version{};
    std::int64_t simulated_day{};
    std::vector<SectionDescriptor> sections;
};

class TempFiles {
public:
    ~TempFiles() {
        for (const auto& path : paths_) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
    void add(std::filesystem::path path) { paths_.push_back(std::move(path)); }
private:
    std::vector<std::filesystem::path> paths_;
};

template <typename T>
void write_pod(std::ostream& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    if (!out) throw std::runtime_error("failed to write simulation checkpoint");
}

template <typename T>
void read_pod(std::istream& in, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    if (!in) throw std::runtime_error("failed to read simulation checkpoint");
}

std::uint64_t process_id() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::filesystem::path unique_auxiliary_path(const char* tag) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::temp_directory_path() /
        (std::string("worldsim.") + tag + "." + std::to_string(process_id()) + "." +
         std::to_string(tick) + "." + std::to_string(serial) + ".tmp");
}

std::filesystem::path unique_publish_path(const std::filesystem::path& target) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed);
    const auto parent = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
    return parent / (std::string(".worldsim-checkpoint.") + std::to_string(process_id()) + "." +
        std::to_string(tick) + "." + std::to_string(serial) + ".tmp");
}

std::uint64_t update_checksum(std::uint64_t hash, const char* data, std::size_t size) noexcept {
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t checksum_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open checkpoint section for checksum: " + path.string());
    std::array<char, kIoBufferBytes> buffer{};
    std::uint64_t hash = kFnvOffset;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0) hash = update_checksum(hash, buffer.data(), static_cast<std::size_t>(count));
    }
    if (!in.eof()) throw std::runtime_error("failed while checksumming checkpoint section");
    return hash;
}

void append_file(std::ostream& out, const std::filesystem::path& path, std::uint64_t expected_size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot reopen checkpoint section: " + path.string());
    std::array<char, kIoBufferBytes> buffer{};
    std::uint64_t copied = 0;
    while (copied < expected_size) {
        const auto remaining = expected_size - copied;
        const auto chunk = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        in.read(buffer.data(), chunk);
        const auto count = in.gcount();
        if (count <= 0) throw std::runtime_error("checkpoint section was truncated during publish");
        out.write(buffer.data(), count);
        if (!out) throw std::runtime_error("failed while assembling simulation checkpoint");
        copied += static_cast<std::uint64_t>(count);
    }
    char extra{};
    if (in.read(&extra, 1)) throw std::runtime_error("checkpoint section grew during publish");
    if (!in.eof()) throw std::runtime_error("failed while validating checkpoint section end");
}

void save_settlement_state(const SettlementState& state, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create settlement checkpoint section");
    out.write(kSettlementMagic.data(), static_cast<std::streamsize>(kSettlementMagic.size()));
    write_pod(out, kSettlementFormatVersion);
    const auto count = static_cast<std::uint64_t>(state.settlements().size());
    write_pod(out, count);
    write_pod(out, state.next_id());
    for (const auto& value : state.settlements()) {
        write_pod(out, value.id);
        write_pod(out, value.regional_coord.x);
        write_pod(out, value.regional_coord.y);
        write_pod(out, value.population);
        write_pod(out, value.founded_day);
    }
    out.flush();
    if (!out) throw std::runtime_error("failed to flush settlement checkpoint section");
}

SettlementState load_settlement_state(
    const World& world,
    std::int64_t simulated_day,
    const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open settlement checkpoint section");
    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kSettlementMagic) throw std::runtime_error("invalid settlement checkpoint magic");
    std::uint32_t version{};
    read_pod(in, version);
    if (version != kSettlementFormatVersion) throw std::runtime_error("unsupported settlement checkpoint version");
    std::uint64_t count{};
    SettlementId next_id{};
    read_pod(in, count);
    read_pod(in, next_id);
    const auto file_size = std::filesystem::file_size(path);
    constexpr std::uint64_t kHeaderBytes = 8 + sizeof(std::uint32_t) +
        sizeof(std::uint64_t) + sizeof(SettlementId);
    if (file_size < kHeaderBytes ||
        count > (file_size - kHeaderBytes) / kSettlementRecordBytes ||
        count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("settlement checkpoint count is invalid");
    }
    std::vector<Settlement> values;
    values.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        Settlement value;
        read_pod(in, value.id);
        read_pod(in, value.regional_coord.x);
        read_pod(in, value.regional_coord.y);
        read_pod(in, value.population);
        read_pod(in, value.founded_day);
        if (value.founded_day > simulated_day) {
            throw std::runtime_error("settlement checkpoint contains a future founded day");
        }
        (void)world.sample_region(value.regional_coord);
        values.push_back(value);
    }
    char trailing{};
    if (in.read(&trailing, 1)) throw std::runtime_error("settlement checkpoint contains trailing data");
    if (!in.eof()) throw std::runtime_error("failed while validating settlement checkpoint end");
    return SettlementState::from_persisted(std::move(values), next_id);
}

CheckpointHeader read_header(std::istream& in, const std::filesystem::path& path) {
    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) throw std::runtime_error("invalid simulation checkpoint magic");

    CheckpointHeader header;
    read_pod(in, header.version);
    if (header.version < kLegacyFormatVersion || header.version > kFormatVersion) {
        throw std::runtime_error("unsupported simulation checkpoint version");
    }
    read_pod(in, header.simulated_day);
    if (header.simulated_day < 0) throw std::runtime_error("simulation checkpoint has a negative global day");

    std::uint32_t count{};
    read_pod(in, count);
    const auto expected = header.version == kLegacyFormatVersion ? kLegacySectionCount : (header.version == 2 ? 4u : kSectionCount);
    if (count != expected) throw std::runtime_error("simulation checkpoint section count is invalid");
    header.sections.resize(count);
    for (auto& section : header.sections) {
        read_pod(in, section.id);
        read_pod(in, section.size);
        read_pod(in, section.checksum);
        if (section.size == 0) throw std::runtime_error("simulation checkpoint contains an empty section");
    }
    if (header.sections[0].id != kWorldSection ||
        header.sections[1].id != kWeatherSection ||
        header.sections[2].id != kWaterSection ||
        (header.version >= 2 && header.sections[3].id != kSettlementSection) ||
        (header.version >= 3 && header.sections[4].id != kEcosystemSection)) {
        throw std::runtime_error("simulation checkpoint section order is invalid");
    }

    const auto position = in.tellg();
    if (position < 0) throw std::runtime_error("failed to inspect simulation checkpoint header");
    const auto file_size = std::filesystem::file_size(path);
    const auto header_size = static_cast<std::uint64_t>(position);
    if (header_size > file_size) throw std::runtime_error("simulation checkpoint header exceeds file size");
    const auto payload_size = file_size - header_size;
    std::uint64_t declared_size = 0;
    for (const auto& section : header.sections) {
        if (declared_size > payload_size || section.size > payload_size - declared_size) {
            throw std::runtime_error("simulation checkpoint section lengths exceed file size");
        }
        declared_size += section.size;
    }
    if (declared_size != payload_size) {
        throw std::runtime_error("simulation checkpoint contains unexpected trailing data");
    }
    return header;
}

void consume_section(std::istream& in, const SectionDescriptor& section, std::ostream* extracted) {
    std::array<char, kIoBufferBytes> buffer{};
    std::uint64_t remaining = section.size;
    std::uint64_t hash = kFnvOffset;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        in.read(buffer.data(), chunk);
        const auto count = in.gcount();
        if (count <= 0) throw std::runtime_error("simulation checkpoint section is truncated");
        hash = update_checksum(hash, buffer.data(), static_cast<std::size_t>(count));
        if (extracted) {
            extracted->write(buffer.data(), count);
            if (!*extracted) throw std::runtime_error("failed to extract simulation checkpoint section");
        }
        remaining -= static_cast<std::uint64_t>(count);
    }
    if (hash != section.checksum) throw std::runtime_error("simulation checkpoint section checksum mismatch");
}

CheckpointHeader validate_container(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open simulation checkpoint: " + path.string());
    const auto header = read_header(in, path);
    for (const auto& section : header.sections) consume_section(in, section, nullptr);
    char trailing{};
    if (in.read(&trailing, 1)) throw std::runtime_error("simulation checkpoint contains unexpected trailing data");
    if (!in.eof()) throw std::runtime_error("failed while validating simulation checkpoint end");
    return header;
}

void extract_section(
    std::istream& in,
    const SectionDescriptor& section,
    const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create temporary checkpoint section: " + path.string());
    consume_section(in, section, &out);
    out.flush();
    if (!out) throw std::runtime_error("failed to flush temporary checkpoint section");
}

void sync_file(const std::filesystem::path& path) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open completed checkpoint for flush");
    const BOOL ok = FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!ok) throw std::runtime_error("failed to flush completed checkpoint");
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("cannot open completed checkpoint for fsync");
    const int result = ::fsync(fd);
    ::close(fd);
    if (result != 0) throw std::runtime_error("failed to fsync completed checkpoint");
#endif
}

void publish_atomically(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("failed to publish simulation checkpoint atomically");
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) throw std::runtime_error("failed to publish simulation checkpoint atomically: " + ec.message());
#endif
}

} // namespace

void SimulationState::save_checkpoint(const std::filesystem::path& path) const {
    validate_invariants();
    if (path.empty()) throw std::invalid_argument("simulation checkpoint path is empty");

    TempFiles cleanup;
    const auto world_path = unique_auxiliary_path("world");
    const auto weather_path = unique_auxiliary_path("weather");
    const auto water_path = unique_auxiliary_path("water");
    const auto settlement_path = unique_auxiliary_path("settlements");
    const auto ecosystem_path = unique_auxiliary_path("ecosystem");
    cleanup.add(ecosystem_path);
    const auto publish_path = unique_publish_path(path);
    cleanup.add(world_path); cleanup.add(weather_path); cleanup.add(water_path);
    cleanup.add(settlement_path); cleanup.add(publish_path);

    world_.save(world_path);
    save_weather_state(weather_, weather_path);
    save_multiresolution_water_state(water_, water_path);
    save_settlement_state(settlements_, settlement_path);
    ecosystem_.save(ecosystem_path);

    const std::array<std::filesystem::path, kSectionCount> section_paths{
        world_path, weather_path, water_path, settlement_path, ecosystem_path};
    std::array<SectionDescriptor, kSectionCount> sections{};
    sections[0].id = kWorldSection;
    sections[1].id = kWeatherSection;
    sections[2].id = kWaterSection;
    sections[3].id = kSettlementSection;
    sections[4].id = kEcosystemSection;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        sections[i].size = std::filesystem::file_size(section_paths[i]);
        if (sections[i].size == 0) throw std::runtime_error("cannot checkpoint an empty simulation section");
        sections[i].checksum = checksum_file(section_paths[i]);
    }

    {
        std::ofstream out(publish_path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot create temporary simulation checkpoint: " + publish_path.string());
        out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
        if (!out) throw std::runtime_error("failed to write simulation checkpoint magic");
        write_pod(out, kFormatVersion);
        write_pod(out, simulated_day());
        write_pod(out, kSectionCount);
        for (const auto& section : sections) {
            write_pod(out, section.id); write_pod(out, section.size); write_pod(out, section.checksum);
        }
        for (std::size_t i = 0; i < sections.size(); ++i) append_file(out, section_paths[i], sections[i].size);
        out.flush();
        if (!out) throw std::runtime_error("failed to flush temporary simulation checkpoint");
    }

    const auto validated = validate_container(publish_path);
    if (validated.simulated_day != simulated_day() || validated.version != kFormatVersion) {
        throw std::runtime_error("temporary simulation checkpoint changed generation metadata");
    }
    sync_file(publish_path);
    publish_atomically(publish_path, path);
}

SimulationState SimulationState::load_checkpoint(const std::filesystem::path& path) {
    if (path.empty()) throw std::invalid_argument("simulation checkpoint path is empty");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open simulation checkpoint: " + path.string());
    const auto header = read_header(in, path);

    TempFiles cleanup;
    const auto world_path = unique_auxiliary_path("load-world");
    const auto weather_path = unique_auxiliary_path("load-weather");
    const auto water_path = unique_auxiliary_path("load-water");
    const auto settlement_path = unique_auxiliary_path("load-settlements");
    const auto ecosystem_path = unique_auxiliary_path("load-ecosystem");
    cleanup.add(ecosystem_path);
    cleanup.add(world_path); cleanup.add(weather_path); cleanup.add(water_path); cleanup.add(settlement_path);

    extract_section(in, header.sections[0], world_path);
    extract_section(in, header.sections[1], weather_path);
    extract_section(in, header.sections[2], water_path);
    if (header.version >= 2) extract_section(in, header.sections[3], settlement_path);
    if (header.version >= 3) extract_section(in, header.sections[4], ecosystem_path);
    char trailing{};
    if (in.read(&trailing, 1)) throw std::runtime_error("simulation checkpoint contains unexpected trailing data");
    if (!in.eof()) throw std::runtime_error("failed while validating simulation checkpoint end");

    auto world = World::load(world_path);
    auto topology = world.analyze_continental_hydrology();
    auto weather = load_weather_state(world, weather_path);
    auto water = load_multiresolution_water_state(world, topology, water_path);
    if (weather.simulated_day() != header.simulated_day ||
        water.simulated_day() != header.simulated_day) {
        throw std::runtime_error("simulation checkpoint global day does not match component clocks");
    }
    auto settlements = header.version >= 2
        ? load_settlement_state(world, header.simulated_day, settlement_path)
        : SettlementState{};

    auto result = SimulationState(
        std::move(world), std::move(topology), std::move(weather), std::move(water),
        std::move(settlements));
    if (header.version >= 3) {
        result.ecosystem_ = EcosystemState::load(
            result.world_, result.topology_, result.water_, ecosystem_path);
    }
    result.validate_invariants();
    return result;
}

} // namespace worldsim
