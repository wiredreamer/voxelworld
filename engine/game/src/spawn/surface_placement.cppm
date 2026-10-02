export module vw.game:spawn.surface_placement;

import std;

import vw.core;
import vw.ecs;
import vw.world;

export namespace vw::game {

class surface_placement_system;

struct surface_placement_component final {
    [[nodiscard]] auto get_world_xz() const -> const vec2f& {
        return world_xz_;
    }

    [[nodiscard]] auto get_clearance_voxels() const -> float32 {
        return clearance_voxels_;
    }

private:
    friend class surface_placement_system;

    vec2f world_xz_{0.0f, 0.0f};
    float32 clearance_voxels_ = 0.0f;
};

class surface_placement_system final {
public:
    static constexpr std::string_view system_name = "surface_place";

    static constexpr float32 default_clearance_voxels = 6.0f;

    explicit surface_placement_system(ecs::world& w);

    auto update(float32 delta_time) -> void;

    auto place(
        ecs::entity ent, const vec2f& world_xz,
        float32 clearance_voxels = default_clearance_voxels
    ) -> void;

    [[nodiscard]] auto is_waiting(ecs::entity ent) const -> bool;

private:
    ecs::world* world_;
    std::vector<ecs::entity> placed_;
};

}  // namespace vw::game
