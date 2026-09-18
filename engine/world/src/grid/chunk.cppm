export module vw.world:grid.chunk;
import :grid.visibility;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :light;

export namespace vw::ecs {

class chunk {
public:
    static constexpr int32 size   = 64;
    static constexpr int32 volume = size * size * size;

    chunk(world& w, vec3i coord, std::shared_ptr<asset::chunk_volume> content,
          int32 world_units_per_voxel = 1);
    ~chunk();

    chunk(const chunk&)                    = delete;
    auto operator=(const chunk&) -> chunk& = delete;
    chunk(chunk&& other) noexcept;
    auto operator=(chunk&& other) noexcept -> chunk&;

    [[nodiscard]] auto get_voxel(int32 x, int32 y, int32 z) const -> voxel;
    [[nodiscard]] auto get_voxel(vec3i local) const -> voxel;
    auto set_voxel(int32 x, int32 y, int32 z, voxel v) -> void;
    auto set_voxel(vec3i local, voxel v) -> void;
    [[nodiscard]] auto is_empty(int32 x, int32 y, int32 z) const -> bool;

    [[nodiscard]] auto get_model() const -> const std::shared_ptr<asset::model>&;
    [[nodiscard]] auto get_volume() const -> const std::shared_ptr<asset::chunk_volume>&;

    [[nodiscard]] auto get_entity() const -> entity;
    [[nodiscard]] auto is_drawn() const -> bool;
    [[nodiscard]] auto is_solid() const -> bool;

    [[nodiscard]] auto known_neighbors() const -> uint8;

    auto ensure_entity() -> bool;

    [[nodiscard]] static constexpr auto contains(int32 x, int32 y, int32 z) -> bool {
        return x >= 0 && x < size && y >= 0 && y < size && z >= 0 && z < size;
    }

    auto set_known_neighbors(uint8 mask) -> void;

private:
    auto create_entity_() -> void;

    world* world_;
    vec3i coord_{};
    int32 world_units_per_voxel_{1};
    entity ent_;
    std::shared_ptr<asset::chunk_volume> volume_;
    asset::model_fill fill_ = asset::model_fill::mixed;
    uint8 known_neighbors_  = 0;
};

}  // namespace vw::ecs
