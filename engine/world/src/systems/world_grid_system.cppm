export module vw.world:systems.world_grid;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;
import :grid;
import :spatial;
import :light;
import :terrain;

export namespace vw::ecs {

class world;

struct world_grid_system_stats {
    float32 integrate_ms       = 0.0F;
    float32 stage_ms           = 0.0F;
    float32 boundary_from_ms   = 0.0F;
    float32 chunk_create_ms    = 0.0F;
    float32 request_columns_ms = 0.0F;
    float32 rebuild_active_ms  = 0.0F;
    float32 unload_ms          = 0.0F;
    uint32 active_count        = 0;
    uint32 pending_count       = 0;
    uint32 loaded_count        = 0;

    uint32 drawn_count = 0;

    uint32 staged_count = 0;

    uint32 lighting_count = 0;

    float32 light_apply_ms = 0.0F;

    uint32 relight_backlog = 0;
    uint64 relit_columns   = 0;

    uint64 relit_chunks = 0;
};

class world_grid_system {
public:
    static constexpr std::string_view system_name = "world_grid";

    explicit world_grid_system(world& w);
    ~world_grid_system();

    world_grid_system(const world_grid_system&)                    = delete;
    auto operator=(const world_grid_system&) -> world_grid_system& = delete;
    world_grid_system(world_grid_system&&) noexcept;
    auto operator=(world_grid_system&&) noexcept -> world_grid_system&;

    auto set_grid(std::unique_ptr<world_grid> grid) -> void;
    auto set_loader(std::unique_ptr<chunk_loader> loader, job_system& jobs) -> void;

    [[nodiscard]] auto grid() -> world_grid*;
    [[nodiscard]] auto grid() const -> const world_grid*;
    [[nodiscard]] auto loader() -> chunk_loader*;
    [[nodiscard]] auto loader() const -> const chunk_loader*;
    [[nodiscard]] auto has_grid() const -> bool;
    [[nodiscard]] auto has_loader() const -> bool;

    auto update(float32 dt) -> void;
    auto shutdown() -> void;

    [[nodiscard]] auto get_stats() const -> const world_grid_system_stats&;
    [[nodiscard]] auto get_loader_stats() const -> column_gen_stats;
    [[nodiscard]] auto get_light_stats() const -> light_stats;

    class view_modifier {
    public:
        auto set_view_distance(uint32 distance) -> view_modifier&;

    private:
        friend class world_grid_system;
        view_modifier(world_grid_system* system, entity ent);

        world_grid_system* system_;
        entity entity_;
    };

    auto modify_view(entity ent) -> view_modifier;

private:
    static constexpr int32 apron_columns = 1;

    auto process_dirty_entity_(entity ent) -> bool;
    auto process_dirty_entities_() -> bool;
    auto stage_completed_columns_() -> void;
    auto collect_lit_columns_() -> void;
    auto relight_dirty_columns_() -> void;
    auto apply_relit_column_(light_result& result) -> void;
    auto integrate_completed_columns_() -> void;
    auto dispatch_light_(vec2i coord) -> bool;
    [[nodiscard]] auto column_stack_(vec2i coord, int32 bottom)
        -> std::vector<std::shared_ptr<asset::model>>;
    [[nodiscard]] auto column_bottom_(vec2i coord) -> std::optional<int32>;
    [[nodiscard]] static auto already_lit_(gen_column& col) -> bool;
    [[nodiscard]] auto column_available_(vec2i coord) const -> bool;
    [[nodiscard]] auto column_ready_(vec2i coord) const -> bool;
    [[nodiscard]] auto within_draw_(vec2i coord) const -> bool;
    [[nodiscard]] auto model_at_(vec3i chunk_coord) const -> asset::model*;
    auto queue_if_ready_(vec2i coord) -> void;
    auto demote_column_(vec2i coord) -> void;
    auto dispatch_column_requests_() -> void;
    auto update_grid_stats_() -> void;
    auto rebuild_active_set_() -> vec2i;
    auto unload_inactive_columns_() -> void;
    auto rebuild_pending_requests_(vec2i camera_column) -> void;
    auto clear_grid_transient_state_() -> void;
    auto clear_loader_transient_state_() -> void;

    struct column_layer {
        int32 voxels_per_cell = 1;

        std::unique_ptr<world_grid> grid;
        std::unique_ptr<chunk_loader> loader;

        std::unordered_set<vec2i> active_columns;
        std::unordered_set<vec2i> pending_active_columns;
        std::vector<vec2i> pending_requests;
        std::unordered_map<vec2i, std::unique_ptr<gen_column>> staged_columns;
        std::vector<vec2i> ready_columns;

        std::unordered_set<vec2i> light_dirty;
    };

    world* world_;
    column_layer near_;
    std::unique_ptr<light_baker> baker_;

    vec2i camera_column_{};
    int32 draw_distance_ = 0;

    world_grid_system_stats stats_;
};
}  // namespace vw::ecs
