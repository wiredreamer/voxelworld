module vw.core;

import std;

namespace vw {

auto job_system::default_worker_count() -> uint32 {
    const auto hardware = std::thread::hardware_concurrency();
    if (hardware <= 1) {
        return 1;
    }
    return std::min(hardware - 1, worker_ceiling);
}

job_system::job_system(uint32 workers) {
    const uint32 count = workers != 0 ? workers : default_worker_count();

    workers_.reserve(count);
    for (uint32 index = 0; index < count; ++index) {
        workers_.emplace_back(&job_system::worker_, this, index);
    }
}

job_system::~job_system() {
    stop();
}

auto job_system::set_lane_limit(job_lane lane_id, uint32 limit) -> void {
    {
        const std::scoped_lock guard(mutex_);
        lanes_[static_cast<uint32>(lane_id)].limit = limit;
    }
    ready_.notify_all();
}

auto job_system::submit(job_lane lane_id, job work) -> void {
    {
        const std::scoped_lock guard(mutex_);

        lane& target = lanes_[static_cast<uint32>(lane_id)];
        if (target.closed || !running_) {
            return;
        }

        target.queue.push_back(std::move(work));
        target.peak =
            std::max(target.peak, static_cast<uint32>(target.queue.size()) + target.running);
    }
    ready_.notify_one();
}

auto job_system::take_(job& taken, uint32& lane_index) -> bool {
    std::unique_lock guard(mutex_);

    while (true) {
        for (uint32 step = 0; step < job_lane_count; ++step) {
            const uint32 candidate = (next_lane_ + step) % job_lane_count;
            lane& source           = lanes_[candidate];
            if (source.queue.empty()) {
                continue;
            }
            if (source.limit != 0 && source.running >= source.limit) {
                continue;
            }

            taken = std::move(source.queue.front());
            source.queue.pop_front();
            ++source.running;

            next_lane_ = (candidate + 1) % job_lane_count;
            lane_index = candidate;
            return true;
        }

        if (!running_) {
            return false;
        }

        ready_.wait(guard);
    }
}

auto job_system::worker_(uint32 index) -> void {
    while (true) {
        job work;
        uint32 lane_index = 0;
        if (!take_(work, lane_index)) {
            return;
        }

        work(index);

        {
            const std::scoped_lock guard(mutex_);
            --lanes_[lane_index].running;
        }
        ready_.notify_all();
        idle_.notify_all();
    }
}

auto job_system::drain(job_lane lane_id) -> void {
    std::unique_lock guard(mutex_);

    const lane& target = lanes_[static_cast<uint32>(lane_id)];
    idle_.wait(guard, [&target] { return target.queue.empty() && target.running == 0; });
}

auto job_system::close(job_lane lane_id) -> void {
    {
        const std::scoped_lock guard(mutex_);
        lanes_[static_cast<uint32>(lane_id)].closed = true;
    }
    drain(lane_id);
}

auto job_system::stop() -> void {
    {
        const std::scoped_lock guard(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    ready_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

auto job_system::get_lane_stats(job_lane lane_id) const -> job_lane_stats {
    const std::scoped_lock guard(mutex_);

    const lane& target = lanes_[static_cast<uint32>(lane_id)];
    return job_lane_stats{
        .queued  = static_cast<uint32>(target.queue.size()),
        .running = target.running,
        .peak    = target.peak,
    };
}

}  // namespace vw
