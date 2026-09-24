export module vw.gfx:mesh_pool;

import std;

import vw.core;
import vw.asset;
import vw.world;
import :meshing;
import vulkan;

export namespace vw::gfx {


class vulkan_context;

struct mesh_generation_task {
    vw::asset::model_identity identity;
    std::weak_ptr<vw::asset::model> model_ref;
    std::weak_ptr<vw::asset::chunk_volume> chunk_ref;
    std::shared_ptr<const vw::asset::model_boundary> boundary;
    std::shared_ptr<const vw::asset::light_field> sky;
    std::shared_ptr<const vw::asset::light_field> block;
    std::promise<mesh> promise;
    mesh_options opts;

    mesh_generation_task(
        vw::asset::model_identity identity,
        std::weak_ptr<vw::asset::model> model_ref,
        std::weak_ptr<vw::asset::chunk_volume> chunk_ref,
        std::shared_ptr<const vw::asset::model_boundary> boundary,
        std::shared_ptr<const vw::asset::light_field> sky,
        std::shared_ptr<const vw::asset::light_field> block,
        mesh_options opts
    )
        : identity(identity)
        , model_ref(std::move(model_ref))
        , chunk_ref(std::move(chunk_ref))
        , boundary(std::move(boundary))
        , sky(std::move(sky))
        , block(std::move(block))
        , opts(opts) {}
};

struct mesh_gen_stats {
    uint64 chunks       = 0;
    uint64 quads        = 0;
    float32 total_ms    = 0.0f;
    float32 mean_us     = 0.0f;
    float32 p50_us      = 0.0f;
    float32 p99_us      = 0.0f;
    float32 max_us      = 0.0f;
    uint32 queue_depth  = 0;
    uint32 queue_peak   = 0;
    uint32 held         = 0;
    uint32 held_peak    = 0;
    uint64 blind        = 0;
    uint64 partial      = 0;
    std::array<uint64, face_direction_count> missing{};
};

class mesh_pool final {
public:
    explicit mesh_pool(const voxel_registry& registry, vw::job_system& jobs);
    ~mesh_pool();

    mesh_pool(const mesh_pool&)                    = delete;
    auto operator=(const mesh_pool&) -> mesh_pool& = delete;
    mesh_pool(mesh_pool&&)                         = delete;
    auto operator=(mesh_pool&&) -> mesh_pool&      = delete;

    auto stop_gen_threads() -> void;
    [[nodiscard]] auto has(const vw::asset::model_identity& identity, int32 lod_step = 1) const
        -> bool;
    [[nodiscard]] auto is_pending(
        const vw::asset::model_identity& identity, int32 lod_step = 1
    ) const -> bool;
    auto request_mesh(
        const std::shared_ptr<vw::asset::model>& model_ptr,
        const std::shared_ptr<vw::asset::chunk_volume>& chunk_ptr,
        mesh_options opts = {}
    ) -> void;
    [[nodiscard]] auto get(const vw::asset::model_identity& identity, int32 lod_step = 1) const
        -> const mesh*;
    auto remove(const vw::asset::model_identity& identity) -> void;
    auto evict(const vw::asset::model_identity& identity, int32 lod_step = 1) -> void;
    auto process_completed(uint32 max_meshes) -> void;
    [[nodiscard]] auto get_pending_count() const -> uint32;
    [[nodiscard]] auto get_gen_stats() const -> mesh_gen_stats;

private:
    // см. docs/optimization.md#что-измерено-и-переоткрывать-не-надо
    // см. docs/lod-plan.md#одна-модель-на-двух-расстояниях
    struct mesh_slot {
        mesh_key key{};
        bool live         = false;
        uint32 generation = 0;
        bool has_mesh     = false;
        mesh data;
        std::weak_ptr<vw::asset::model> model_ref;
        std::weak_ptr<vw::asset::chunk_volume> chunk_ref;
        std::future<mesh> pending;
    };

    auto generate_(mesh_generation_task& task, mesh_generation_storage& storage) -> void;
    auto sweep_orphaned_() -> void;
    auto record_chunk_(uint64 elapsed_ns, uint64 quads) -> void;

    [[nodiscard]] auto live_slot_(mesh_key key, uint32 generation) -> mesh_slot*;
    [[nodiscard]] auto live_slot_(mesh_key key, uint32 generation) const -> const mesh_slot*;
    [[nodiscard]] auto acquire_slot_(mesh_key key, uint32 generation) -> uint32;
    auto retire_stale_(uint32 model_index, uint32 generation) -> void;
    auto release_slot_(uint32 slot_index) -> void;
    auto drop_mesh_(mesh_slot& slot) -> void;
    auto drop_pending_(uint32 slot_index) -> void;

    const voxel_registry* registry_;

    std::vector<mesh_slot> slots_;
    std::unordered_map<mesh_key, uint32> slot_of_;
    std::vector<uint32> free_slots_;
    std::vector<uint32> pending_slots_;
    uint32 held_ = 0;

    vw::job_system* jobs_;
    std::vector<mesh_generation_storage> storage_;
    std::size_t sweep_cursor_ = 0;
    static constexpr std::size_t sweep_slots_per_frame_   = 512;
    static constexpr std::size_t sweep_orphans_per_frame_ = 32;

    mutable std::mutex stats_mutex_;
    vw::latency_histogram gen_latency_;
    uint64 gen_quads_ = 0;
    uint64 blind_requests_ = 0;
    uint64 partial_requests_ = 0;
    std::array<uint64, face_direction_count> missing_faces_{};
    uint32 held_peak_ = 0;
};

}  // namespace vw::gfx
