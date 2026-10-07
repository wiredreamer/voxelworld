export module vw.testbed:scenes.voxel_edits;

import std;

import vw.core;
import :app;
import :args;
import :scene;

export namespace vw::testbed {

class voxel_edits_scene final : public scene {
public:
    voxel_edits_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "voxel-edits";
    }

    auto tick(float32 delta_time) -> void override;
    auto collect_report(gfx::report& out) const -> void override;
    auto ui() -> void override;

private:
    static constexpr int32 side  = 32;
    static constexpr int32 cells = side * side * side;

    auto start_() -> void;

    int32 per_frame_ = 1;

    int32 cursor_    = 0;
    int32 top_voxel_ = 0;
    uint64 edits_    = 0;
    bool started_    = false;

    uint64 mesh_base_ = 0;
};

}  // namespace vw::testbed
