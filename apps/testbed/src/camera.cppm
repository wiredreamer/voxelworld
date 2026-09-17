export module vw.testbed:camera;

import std;

import vw.core;

export namespace vw::testbed {

class testbed_app;

struct camera_hint {
    std::string_view rig = "parked";

    vec3f offset{0.0f, 0.0f, 0.0f};

    float32 pitch = -10.0f;

    float32 yaw = 0.0f;

    float32 degrees_per_frame = 0.25f;
};

class camera_rig {
public:
    explicit camera_rig(testbed_app& stand) : stand_{&stand} {}

    virtual ~camera_rig() = default;

    camera_rig(const camera_rig&)                    = delete;
    auto operator=(const camera_rig&) -> camera_rig& = delete;
    camera_rig(camera_rig&&)                         = delete;
    auto operator=(camera_rig&&) -> camera_rig&      = delete;

    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    [[nodiscard]] virtual auto needs_ground() const -> bool {
        return true;
    }

    virtual auto drive(const camera_hint& hint, float32 delta_time) -> void = 0;

protected:
    [[nodiscard]] auto stand() const -> testbed_app& {
        return *stand_;
    }

private:
    testbed_app* stand_;
};

using camera_factory = std::function<auto(testbed_app&)->std::unique_ptr<camera_rig>>;

}  // namespace vw::testbed
