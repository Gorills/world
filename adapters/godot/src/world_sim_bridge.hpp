#pragma once

#include "worldsim/simulation.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <memory>

// One instance owns one simulation. The Godot client calls it on the main thread;
// dictionaries are detached value snapshots and never expose mutable core state.
class WorldSimBridge : public godot::RefCounted {
    GDCLASS(WorldSimBridge, godot::RefCounted)

public:
    bool create_world(std::int64_t seed = 42);
    bool advance_day();
    bool save_world(const godot::String& absolute_path);
    bool load_world(const godot::String& absolute_path);
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] godot::String get_last_error() const noexcept;
    [[nodiscard]] godot::Dictionary get_terrain(std::int64_t resolution = 128);
    [[nodiscard]] godot::Dictionary get_frame();
    [[nodiscard]] godot::Dictionary sample_point(double x_m, double y_m);

protected:
    static void _bind_methods();

private:
    std::unique_ptr<worldsim::SimulationState> simulation_;
    godot::String last_error_;

    [[nodiscard]] const worldsim::SimulationState& require_state() const;
};
