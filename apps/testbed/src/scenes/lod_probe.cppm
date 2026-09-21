export module vw.testbed:scenes.lod_probe;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import :app;
import :args;
import :camera;
import :scene;

export namespace vw::testbed {

class lod_probe_scene final : public scene {
public:
    lod_probe_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "lod-probe";
    }

    auto tick(float32 delta_time) -> void override;

    [[nodiscard]] auto default_camera() const -> camera_hint override {
        return {.rig = "spin", .pitch = -20.0f, .degrees_per_frame = 0.0625f};
    }

    [[nodiscard]] auto is_ready() const -> bool override {
        return placed_;
    }

    auto ui() -> void override;

    auto collect_report(gfx::report& out) const -> void override;

private:
    struct step {
        int32 voxels_per_cell = 1;
        int32 columns         = 0;
        int32 chunks          = 0;
        uint32 quads          = 0;
        float32 build_ms      = 0.0f;
    };

    auto place_() -> void;

    auto build_step_(int32 voxels_per_cell, float32 offset_x) -> step;

    static constexpr int32 cells_per_chunk = 64;

    int32 coarsest_ = 4;
    int32 span_     = 2;
    bool placed_    = false;

    std::vector<step> steps_;
};

}  // namespace vw::testbed
