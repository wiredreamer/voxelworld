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
    std::promise<mesh> promise;
    mesh_options opts;

    mesh_generation_task(
        vw::asset::model_identity identity,
        std::weak_ptr<vw::asset::model> model_ref,
        std::weak_ptr<vw::asset::chunk_volume> chunk_ref,
        mesh_options opts
    )
        : identity(identity)
        , model_ref(std::move(model_ref))
        , chunk_ref(std::move(chunk_ref))
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
};

class mesh_pool final {
public:
    explicit mesh_pool(vulkan_context& context, const voxel_registry& registry,
                       uint32 workers = 0);
    ~mesh_pool();

    mesh_pool(const mesh_pool&)                    = delete;
    auto operator=(const mesh_pool&) -> mesh_pool& = delete;
    mesh_pool(mesh_pool&&)                         = delete;
    auto operator=(mesh_pool&&) -> mesh_pool&      = delete;

    auto stop_gen_threads() -> void;
    [[nodiscard]] auto has(const vw::asset::model_identity& identity) const -> bool;
    [[nodiscard]] auto is_pending(const vw::asset::model_identity& identity) const -> bool;
    auto request_mesh(
        const std::shared_ptr<vw::asset::model>& model_ptr,
        const std::shared_ptr<vw::asset::chunk_volume>& chunk_ptr,
        mesh_options opts = {}
    ) -> void;
    [[nodiscard]] auto get(const vw::asset::model_identity& identity) const -> std::shared_ptr<mesh>;
    auto remove(const vw::asset::model_identity& identity) -> void;
    auto evict(const vw::asset::model_identity& identity) -> void;
    auto process_completed() -> void;
    [[nodiscard]] auto get_pending_count() const -> uint32;
    [[nodiscard]] auto get_gen_stats() const -> mesh_gen_stats;

private:
    auto gen_thread_function() -> void;
    auto sweep_orphaned_() -> void;
    auto record_chunk_(uint64 elapsed_ns, uint64 quads) -> void;

    vulkan_context* context_;
    const voxel_registry* registry_;
    std::unordered_map<vw::asset::model_identity, std::shared_ptr<mesh>> meshes_;
    std::unordered_map<vw::asset::model_identity, std::weak_ptr<vw::asset::model>> model_refs_;
    std::unordered_map<vw::asset::model_identity, std::weak_ptr<vw::asset::chunk_volume>>
        chunk_refs_;
    std::unordered_map<vw::asset::model_identity, std::future<mesh>> pending_meshes_;
    std::unordered_set<uint32> pending_indices_;

    std::vector<std::thread> gen_threads_;
    std::queue<std::unique_ptr<mesh_generation_task>> gen_queue_;
    mutable std::mutex gen_mutex_;
    std::condition_variable gen_cv_;
    bool gen_running_ = true;
    std::size_t sweep_bucket_ = 0;
    static constexpr std::size_t sweep_buckets_per_frame_ = 512;
    static constexpr std::size_t sweep_orphans_per_frame_ = 32;

    mutable std::mutex stats_mutex_;
    vw::latency_histogram gen_latency_;
    uint64 gen_quads_      = 0;
    uint32 gen_queue_peak_ = 0;
};

}  // namespace vw::gfx
