export module vw.world:grid.world_grid;
import :grid.visibility;
import :grid.chunk;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :light;

export namespace vw::ecs {

struct world_light {
    float32 sky   = 1.0F;
    float32 block = 0.0F;

    auto operator==(const world_light&) const -> bool = default;
};

class world_grid {
public:
    explicit world_grid(world& w, int32 world_units_per_voxel = 8);
    ~world_grid() = default;

    world_grid(const world_grid&)                    = delete;
    auto operator=(const world_grid&) -> world_grid& = delete;
    world_grid(world_grid&&)                         = delete;
    auto operator=(world_grid&&) -> world_grid&      = delete;

    [[nodiscard]] auto get_voxel(vec3i world_pos) const -> voxel;
    auto set_voxel(vec3i world_pos, voxel v) -> void;

    [[nodiscard]] auto light_at(const vec3f& world_pos) const -> world_light;

    [[nodiscard]] auto has_chunk(vec3i chunk_coord) const -> bool;
    [[nodiscard]] auto get_chunk(vec3i chunk_coord) -> chunk*;

    [[nodiscard]] auto get_surface_voxel_y(int32 voxel_x, int32 voxel_z) const
        -> std::optional<int32>;
    [[nodiscard]] auto has_column(vec2i coord) const -> bool;

    [[nodiscard]] auto column_levels(vec2i coord) const -> std::span<const int32>;
    [[nodiscard]] auto column_count() const -> uint32;
    [[nodiscard]] auto chunk_count() const -> uint32;

    [[nodiscard]] auto drawn_chunk_count() const -> uint32;

    auto place_chunk(vec3i chunk_coord, std::shared_ptr<asset::chunk_volume> volume)
        -> chunk*;
    auto register_column(vec2i coord, std::vector<int32> y_levels) -> void;
    auto unload_column(vec2i coord) -> void;

    auto refresh_chunk(vec3i chunk_coord) -> void;

    auto remesh_drawn_chunk(vec3i chunk_coord) -> void;

    [[nodiscard]] auto take_light_dirty() -> std::vector<vec2i>;

    [[nodiscard]] auto world_units_per_voxel() const -> int32;

    template <typename F>
    auto for_each_chunk(F&& f) const -> void {
        for (const auto& [coord, ptr] : chunks_) {
            f(coord, *ptr);
        }
    }

    [[nodiscard]] auto world_to_chunk_coord(vec3i world_pos) const -> vec3i;
    [[nodiscard]] auto world_to_local_coord(vec3i world_pos) const -> vec3i;
    [[nodiscard]] auto chunk_to_world_coord(vec3i chunk_coord) const -> vec3i;

private:
    auto mark_light_dirty_(vec3i chunk_coord, vec3i local) -> void;

    world* world_;
    int32 world_units_per_voxel_{1};
    std::unordered_map<vec3i, std::unique_ptr<chunk>> chunks_;
    std::unordered_map<vec2i, std::vector<int32>> column_chunks_;
    std::unordered_set<vec2i> light_dirty_;
    uint32 drawn_chunks_ = 0;
};
}  // namespace vw::ecs
