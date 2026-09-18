export module vw.core:jobs;

import std;

import :types;

export namespace vw {

enum class job_lane : uint32 {
    terrain = 0,
    light   = 1,
    mesh    = 2,
};

inline constexpr uint32 job_lane_count = 3;

struct job_lane_stats {
    uint32 queued  = 0;
    uint32 running = 0;
    uint32 peak    = 0;
};

using job = std::move_only_function<void(uint32)>;

class job_system final {
public:
    // см. docs/ENGINE.md#фоновые-задачи
    static constexpr uint32 worker_ceiling = 32;

    explicit job_system(uint32 workers = 0);
    ~job_system();

    job_system(const job_system&)                    = delete;
    auto operator=(const job_system&) -> job_system& = delete;
    job_system(job_system&&)                         = delete;
    auto operator=(job_system&&) -> job_system&      = delete;

    [[nodiscard]] auto worker_count() const -> uint32 {
        return static_cast<uint32>(workers_.size());
    }

    auto set_lane_limit(job_lane lane, uint32 limit) -> void;

    auto submit(job_lane lane, job work) -> void;

    auto drain(job_lane lane) -> void;
    auto close(job_lane lane) -> void;
    auto stop() -> void;

    [[nodiscard]] auto get_lane_stats(job_lane lane) const -> job_lane_stats;

    [[nodiscard]] static auto default_worker_count() -> uint32;

private:
    struct lane {
        std::deque<job> queue;
        uint32 running = 0;
        uint32 peak    = 0;
        uint32 limit   = 0;
        bool closed    = false;
    };

    auto worker_(uint32 index) -> void;
    [[nodiscard]] auto take_(job& taken, uint32& lane_index) -> bool;

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::condition_variable idle_;
    std::array<lane, job_lane_count> lanes_;
    uint32 next_lane_ = 0;
    bool running_     = true;
    std::vector<std::thread> workers_;
};

}  // namespace vw
