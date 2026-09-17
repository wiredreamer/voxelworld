export module vw.testbed:scene;

import std;

import vw.core;
import vw.gfx;
import :camera;

export namespace vw::testbed {

class testbed_app;

class scene {
public:
    explicit scene(testbed_app& stand) : stand_{&stand} {}

    virtual ~scene() = default;

    scene(const scene&)                    = delete;
    auto operator=(const scene&) -> scene& = delete;
    scene(scene&&)                         = delete;
    auto operator=(scene&&) -> scene&      = delete;

    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    virtual auto tick([[maybe_unused]] float32 delta_time) -> void {}

    [[nodiscard]] virtual auto default_camera() const -> camera_hint {
        return {};
    }

    [[nodiscard]] virtual auto is_ready() const -> bool {
        return true;
    }

    virtual auto on_world_ready() -> void {}

    virtual auto collect_report([[maybe_unused]] gfx::report& out) const -> void {}

    virtual auto ui() -> void {}

protected:
    [[nodiscard]] auto stand() const -> testbed_app& {
        return *stand_;
    }

private:
    testbed_app* stand_;
};

using scene_factory = std::function<auto(testbed_app&)->std::unique_ptr<scene>>;

}  // namespace vw::testbed
