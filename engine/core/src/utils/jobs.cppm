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

// см. docs/ENGINE.md#фоновые-задачи
class job final {
public:
    job() = default;

    template <typename Work>
        requires(!std::same_as<std::remove_cvref_t<Work>, job>) &&
                std::invocable<std::remove_cvref_t<Work>&, uint32>
    job(Work&& work)
        : held_{std::make_unique<holder<std::remove_cvref_t<Work>>>(std::forward<Work>(work))} {}

    job(job&&) noexcept                    = default;
    auto operator=(job&&) noexcept -> job& = default;
    job(const job&)                        = delete;
    auto operator=(const job&) -> job&     = delete;
    ~job()                                 = default;

    auto operator()(uint32 worker) -> void {
        held_->run(worker);
    }

    [[nodiscard]] explicit operator bool() const {
        return held_ != nullptr;
    }

private:
    struct base {
        base()                               = default;
        base(const base&)                    = delete;
        auto operator=(const base&) -> base& = delete;
        virtual ~base()                      = default;
        virtual auto run(uint32 worker) -> void = 0;
    };

    template <typename Work>
    struct holder final : base {
        template <typename Given>
        explicit holder(Given&& given) : work{std::forward<Given>(given)} {}

        auto run(uint32 worker) -> void override {
            work(worker);
        }

        Work work;
    };

    std::unique_ptr<base> held_;
};

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
