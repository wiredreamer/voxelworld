export module vw.world:components.lod;

import std;

import vw.core;
import vw.ecs;

export namespace vw::ecs {

class lod_system;

// см. docs/lod-plan.md#уровень-живёт-рядом-с-моделью
struct lod_component final {
    [[nodiscard]] auto get_base_distance() const -> float32 {
        return base_distance_;
    }

private:
    friend class lod_system;

    float32 base_distance_ = 0.0F;
};

}  // namespace vw::ecs
