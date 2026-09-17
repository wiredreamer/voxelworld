export module vw.testbed:scenes.standing_lights;

import std;

import vw.core;
import vw.ecs;
import :app;
import :args;
import :camera;
import :scene;

export namespace vw::testbed {

class standing_lights_scene : public scene {
public:
    standing_lights_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "standing-lights";
    }

    auto tick(float32 delta_time) -> void override;

    [[nodiscard]] auto default_camera() const -> camera_hint override {
        return {.rig = "spin", .pitch = -20.0f};
    }

    [[nodiscard]] auto is_ready() const -> bool override;

    auto collect_report(gfx::report& out) const -> void override;
    auto ui() -> void override;

protected:
    static constexpr int32 radius = 96;

    static constexpr int32 hamlet_spread = 10;

    static constexpr float32 steps_per_second = 60.0F;

    [[nodiscard]] static auto spiral_point(int32 i, int32 count, float32 span) -> vec2f;

    [[nodiscard]] auto static_lights() const -> int32 {
        return static_lights_;
    }

    [[nodiscard]] auto hamlets() const -> int32 {
        return hamlets_;
    }

    [[nodiscard]] virtual auto site(int32 i) const -> vec2i;
    [[nodiscard]] virtual auto orbit_home(std::size_t i) const -> vec2f;
    [[nodiscard]] virtual auto orbit_radius(std::size_t i, float32 spread) const -> float32;

    [[nodiscard]] virtual auto layout_text() const -> std::string {
        return "a spiral";
    }

private:
    auto spawn_lights_() -> void;
    auto place_emitters_() -> void;
    auto drive_lights_(float32 delta_time) -> void;

    int32 static_lights_  = 400;
    int32 dynamic_lights_ = 64;
    int32 hamlets_ = 24;
    int32 per_frame_      = 1;

    float32 light_speed_ = 1.0F;

    uint64 placed_  = 0;
    bool seeded_    = false;
    bool standing_  = false;
    float64 phase_  = 0.0;

    std::vector<int32> pending_;
    std::vector<ecs::entity> lights_;

    uint32 visible_peak_   = 0;
    uint64 visible_sum_    = 0;
    uint64 visible_frames_ = 0;
    uint32 capped_frames_  = 0;
};

class clustered_lights_scene final : public standing_lights_scene {
public:
    using standing_lights_scene::standing_lights_scene;

    [[nodiscard]] auto name() const -> std::string_view override {
        return "clustered-lights";
    }

protected:
    [[nodiscard]] auto site(int32 i) const -> vec2i override;
    [[nodiscard]] auto orbit_home(std::size_t i) const -> vec2f override;
    [[nodiscard]] auto orbit_radius(std::size_t i, float32 spread) const -> float32 override;
    [[nodiscard]] auto layout_text() const -> std::string override;

private:
    [[nodiscard]] auto centre_(int32 group) const -> vec2f;
};

}  // namespace vw::testbed
