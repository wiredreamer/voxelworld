export module vw.world:light.baker;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :light.column;

export namespace vw::ecs {

struct light_request {
    vec2i coord;
    int32 bottom_y = 0;
    std::array<std::vector<std::shared_ptr<asset::model>>, 9> around;
};

struct light_result {
    vec2i coord;
    int32 bottom_y = 0;
    std::vector<asset::light_field> sky;
    std::vector<asset::light_field> block;
};

struct light_stats {
    uint64 columns     = 0;
    float32 rows_ms    = 0.0F;
    float32 flood_ms   = 0.0F;
    float32 bake_ms    = 0.0F;
    float32 mean_us    = 0.0F;
    float32 p50_us     = 0.0F;
    float32 p99_us     = 0.0F;
    float32 max_us     = 0.0F;
    uint32 queue_depth = 0;
    uint32 queue_peak  = 0;
};

class light_baker {
public:
    // см. docs/lighting.md#потолок-дорожки-света
    static constexpr uint32 default_concurrency = 6;

    explicit light_baker(
        const voxel_registry& voxel_types, job_system& jobs,
        uint32 concurrency = default_concurrency
    );
    ~light_baker();

    light_baker(const light_baker&)                    = delete;
    auto operator=(const light_baker&) -> light_baker& = delete;
    light_baker(light_baker&&)                         = delete;
    auto operator=(light_baker&&) -> light_baker&      = delete;

    auto request(light_request job) -> bool;

    [[nodiscard]] auto try_pop_completed() -> std::optional<light_result>;
    [[nodiscard]] auto is_pending(vec2i coord) const -> bool;
    [[nodiscard]] auto pending_count() const -> uint32;
    [[nodiscard]] auto get_stats() const -> light_stats;

private:
    struct worker_scratch {
        std::vector<std::vector<asset::chunk_occupancy>> held{9};
        std::vector<std::vector<const asset::chunk_occupancy*>> pointers{9};
        std::vector<std::vector<const asset::model*>> emitters{9};
        light_scratch flood;
    };

    auto bake_(light_request job, worker_scratch& scratch) -> void;
    [[nodiscard]] auto acquire_scratch_() -> std::unique_ptr<worker_scratch>;
    auto release_scratch_(std::unique_ptr<worker_scratch> scratch) -> void;
    auto record_column_(uint64 rows_nanos, uint64 flood_nanos, uint64 bake_nanos) -> void;

    job_system* jobs_;
    std::queue<light_result> completed_;
    mutable std::mutex completed_mutex_;
    std::unordered_set<vec2i> pending_;
    std::mutex scratch_mutex_;
    std::vector<std::unique_ptr<worker_scratch>> free_scratch_;

    mutable std::mutex stats_mutex_;
    latency_histogram latency_;
    uint64 rows_nanos_  = 0;
    uint64 flood_nanos_ = 0;
    uint64 bake_nanos_  = 0;

    asset::emission_table emission_;
};

}  // namespace vw::ecs
