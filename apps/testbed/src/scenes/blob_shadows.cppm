export module vw.testbed:scenes.blob_shadows;

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

class blob_shadows_scene final : public scene {
public:
    blob_shadows_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "blob-shadows";
    }

    auto tick(float32 delta_time) -> void override;

    [[nodiscard]] auto default_camera() const -> camera_hint override {
        return {.rig = "spin", .pitch = -25.0f, .degrees_per_frame = 0.0625f};
    }

    [[nodiscard]] auto is_ready() const -> bool override {
        return seeded_ && pending_.empty();
    }

    auto ui() -> void override;

private:
    static constexpr int32 ring = 24;

    struct bob {
        ecs::entity ent;
        float32 x         = 0.0f;
        float32 z         = 0.0f;
        float32 ground    = 0.0f;
        float32 amplitude = 0.0f;
        float32 speed     = 0.0f;
        float32 phase     = 0.0f;
    };

    auto spawn_() -> void;

    int32 bodies_asked_ = 8;

    bool seeded_      = false;
    uint32 bob_frame_ = 0;
    std::vector<bob> bodies_;

    std::vector<int32> pending_;
    std::shared_ptr<asset::model> model_;
};

}  // namespace vw::testbed
