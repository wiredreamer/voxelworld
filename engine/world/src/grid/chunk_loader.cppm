export module vw.world:terrain.loader;
import :terrain.generator;
import :terrain.column;

import std;

import vw.core;

export namespace vw::ecs {

class chunk_loader {
public:
    explicit chunk_loader(std::unique_ptr<terrain_generator> generator, uint32 workers = 0);
    ~chunk_loader();

    chunk_loader(const chunk_loader&)                    = delete;
    auto operator=(const chunk_loader&) -> chunk_loader& = delete;
    chunk_loader(chunk_loader&&)                         = delete;
    auto operator=(chunk_loader&&) -> chunk_loader&      = delete;

    auto request(vec2i coord) -> bool;

    [[nodiscard]] auto try_pop_completed() -> std::unique_ptr<gen_column>;
    [[nodiscard]] auto is_pending(vec2i coord) const -> bool;
    [[nodiscard]] auto pending_count() const -> uint32;
    [[nodiscard]] auto get_gen_stats() const -> column_gen_stats;

private:
    auto gen_thread_function_() -> void;
    auto record_column_(uint64 elapsed_ns, uint64 chunks) -> void;

    struct gen_task {
        vec2i coord;
    };

    std::unique_ptr<terrain_generator> generator_;
    std::vector<std::thread> gen_threads_;
    std::queue<gen_task> gen_queue_;
    std::queue<std::unique_ptr<gen_column>> completed_queue_;
    mutable std::mutex gen_mutex_;
    mutable std::mutex completed_mutex_;
    std::condition_variable gen_cv_;
    bool gen_running_ = true;
    std::unordered_set<vec2i> pending_columns_;

    mutable std::mutex stats_mutex_;
    latency_histogram gen_latency_;
    uint64 gen_chunks_     = 0;
    uint32 gen_queue_peak_ = 0;
};

}  // namespace vw::ecs
