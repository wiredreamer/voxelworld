export module vw.testbed:scenes.terrain;

import std;

import vw.core;
import :app;
import :args;
import :scene;

export namespace vw::testbed {

class terrain_scene final : public scene {
public:
    terrain_scene(testbed_app& stand, const arg_reader&) : scene{stand} {}

    [[nodiscard]] auto name() const -> std::string_view override {
        return "terrain";
    }
};

}  // namespace vw::testbed
