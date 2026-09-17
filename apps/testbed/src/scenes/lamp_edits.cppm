export module vw.testbed:scenes.lamp_edits;

import std;

import vw.core;
import :app;
import :args;
import :scene;

export namespace vw::testbed {

class lamp_edits_scene final : public scene {
public:
    lamp_edits_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "lamp-edits";
    }

    auto tick(float32 delta_time) -> void override;
    auto collect_report(gfx::report& out) const -> void override;
    auto ui() -> void override;

private:
    static constexpr int32 side    = 16;
    static constexpr int32 spacing = 4;
    static constexpr int32 cells   = side * side;

    auto start_() -> void;

    int32 per_frame_ = 1;

    bool inert_ = false;

    int32 cursor_  = 0;
    uint64 placed_ = 0;
    bool started_  = false;

    uint64 mesh_base_        = 0;
    uint64 quads_base_       = 0;
    uint64 relight_base_     = 0;
    uint64 relit_chunk_base_ = 0;
    uint64 columns_base_     = 0;
    float32 flood_base_ms_   = 0.0f;
    float32 bake_base_ms_    = 0.0f;

    float64 quads_per_chunk_base_ = 0.0;
};

}  // namespace vw::testbed
