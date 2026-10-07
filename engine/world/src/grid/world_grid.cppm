export module vw.world:grid.world_grid;
import :grid.visibility;
import :grid.chunk;
import :grid.cell;
import :terrain.generator;

import std;

import vw.core;
import vw.asset;
import vw.ecs;

export namespace vw::ecs {

// см. docs/world.md#журнал-занятости
struct occupancy_change {
    vec3i chunk{};
    vec3i voxel{};
    bool whole_chunk = true;

    auto operator==(const occupancy_change&) const -> bool = default;
};

class world_grid {
public:
    explicit world_grid(world& w, int32 world_units_per_voxel = default_world_units_per_voxel);
    ~world_grid() = default;

    world_grid(const world_grid&)                    = delete;
    auto operator=(const world_grid&) -> world_grid& = delete;
    world_grid(world_grid&&)                         = delete;
    auto operator=(world_grid&&) -> world_grid&      = delete;

    [[nodiscard]] auto get_voxel(vec3i world_pos) const -> voxel;
    auto set_voxel(vec3i world_pos, voxel v) -> void;

    [[nodiscard]] auto cell_of(const vec3f& world_pos) const -> vec3i;
    [[nodiscard]] auto cell_at(vec3i at) const -> std::optional<cell>;
    auto plant_cover(vec3i at, uint8 form) -> std::expected<void, std::string>;
    auto clear_cell(vec3i at) -> void;

    [[nodiscard]] auto has_chunk(vec3i chunk_coord) const -> bool;
    [[nodiscard]] auto get_chunk(vec3i chunk_coord) -> chunk*;
    [[nodiscard]] auto find_chunk(vec3i chunk_coord) const -> const chunk*;

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

    // см. docs/world.md#журнал-занятости
    [[nodiscard]] auto occupancy_serial() const -> uint64;
    [[nodiscard]] auto occupancy_changes_since(uint64 serial) const
        -> std::optional<std::span<const occupancy_change>>;

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
    [[nodiscard]] auto chunk_holding_(vec3i at) const -> const chunk*;
    [[nodiscard]] auto chunk_holding_(vec3i at) -> chunk*;
    auto set_cover_(vec3i support, uint8 form) -> void;
    auto note_occupancy_change_(const occupancy_change& change) -> void;

    world* world_;
    int32 world_units_per_voxel_{1};
    std::unordered_map<vec3i, std::unique_ptr<chunk>> chunks_;
    std::unordered_map<vec2i, std::vector<int32>> column_chunks_;
    uint32 drawn_chunks_ = 0;

    uint64 occupancy_first_serial_;
    std::vector<occupancy_change> occupancy_log_;
};
}  // namespace vw::ecs
