module vw.gfx;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {


mesh_pool::mesh_pool(
    vulkan_context& context, const voxel_registry& registry, uint32 workers
)
    : context_{&context}, registry_{&registry} {
    auto count = workers != 0 ? workers : std::min(std::thread::hardware_concurrency(), 4u);
    if (count == 0) {
        count = 1;
    }
    for (uint32 i = 0; i < count; ++i) {
        gen_threads_.emplace_back(&mesh_pool::gen_thread_function, this);
    }
}

mesh_pool::~mesh_pool() {
    stop_gen_threads();
}

auto mesh_pool::stop_gen_threads() -> void {
    {
        std::scoped_lock lock(gen_mutex_);
        if (!gen_running_) {
            return;
        }
        gen_running_ = false;
    }
    gen_cv_.notify_all();

    for (auto& t : gen_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

[[nodiscard]] auto mesh_pool::has(
    const vw::asset::model_identity& identity
) const -> bool {
    return meshes_.contains(identity);
}

[[nodiscard]] auto mesh_pool::is_pending(
    const vw::asset::model_identity& identity
) const -> bool {
    return pending_meshes_.contains(identity);
}

auto mesh_pool::request_mesh(
    const std::shared_ptr<vw::asset::model>& model_ptr,
    const std::shared_ptr<vw::asset::chunk_volume>& chunk_ptr,
    mesh_options opts
) -> void {
    vw::asset::model_identity identity = model_ptr->get_identity();

    if (has(identity) || is_pending(identity)) {
        return;
    }

    if (pending_indices_.contains(identity.index)) {
        for (auto it = pending_meshes_.begin(); it != pending_meshes_.end(); ++it) {
            if (it->first.index == identity.index) {
                pending_meshes_.erase(it);
                break;
            }
        }
    }
    pending_indices_.insert(identity.index);

    model_refs_[identity] = model_ptr;
    chunk_refs_[identity] = chunk_ptr;

    {
        auto task =
            std::make_unique<mesh_generation_task>(identity, model_ptr, chunk_ptr, opts);
        auto future = task->promise.get_future();

        std::scoped_lock lock(gen_mutex_);
        pending_meshes_[identity] = std::move(future);
        gen_queue_.push(std::move(task));
        gen_queue_peak_ = std::max(gen_queue_peak_, static_cast<uint32>(gen_queue_.size()));
    }
    gen_cv_.notify_one();
}

[[nodiscard]] auto mesh_pool::get(
    const vw::asset::model_identity& identity
) const -> std::shared_ptr<mesh> {
    auto iter = meshes_.find(identity);
    return iter != meshes_.end() ? iter->second : nullptr;
}

auto mesh_pool::remove(
    const vw::asset::model_identity& identity
) -> void {
    meshes_.erase(identity);
    model_refs_.erase(identity);
    chunk_refs_.erase(identity);
    pending_meshes_.erase(identity);
    pending_indices_.erase(identity.index);
}

auto mesh_pool::evict(
    const vw::asset::model_identity& identity
) -> void {
    meshes_.erase(identity);
    pending_indices_.erase(identity.index);
}

auto mesh_pool::sweep_orphaned_() -> void {
    const auto buckets = model_refs_.bucket_count();
    if (buckets == 0) {
        sweep_bucket_ = 0;
        return;
    }

    if (sweep_bucket_ >= buckets) {
        sweep_bucket_ = 0;
    }

    const auto limit  = std::min(buckets, sweep_bucket_ + sweep_buckets_per_frame_);
    std::size_t freed = 0;

    while (sweep_bucket_ < limit) {
        for (auto it = model_refs_.begin(sweep_bucket_);
             it != model_refs_.end(sweep_bucket_) && freed < sweep_orphans_per_frame_;) {
            if (!it->second.expired()) {
                ++it;
                continue;
            }

            const auto identity = it->first;
            ++it;

            meshes_.erase(identity);
            pending_meshes_.erase(identity);
            pending_indices_.erase(identity.index);
            model_refs_.erase(identity);
            chunk_refs_.erase(identity);
            ++freed;
        }

        if (freed >= sweep_orphans_per_frame_) {
            break;
        }
        ++sweep_bucket_;
    }
}

auto mesh_pool::process_completed() -> void {
    sweep_orphaned_();

    constexpr uint32 max_meshes_per_frame = 4;
    uint32 completed = 0;

    for (auto iter = pending_meshes_.begin();
         iter != pending_meshes_.end() && completed < max_meshes_per_frame;) {
        const auto status = iter->second.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready) {
            auto identity = iter->first;
            auto data     = iter->second.get();

            meshes_[identity] = std::make_shared<mesh>(std::move(data));

            if (const auto ref = chunk_refs_.find(identity); ref != chunk_refs_.end()) {
                if (const auto chunk = ref->second.lock()) {
                    chunk->release_boundary();
                }
            }

            iter = pending_meshes_.erase(iter);
            ++completed;
        } else {
            ++iter;
        }
    }
}

auto mesh_pool::get_pending_count() const -> uint32 {
    return static_cast<uint32>(pending_meshes_.size());
}

auto mesh_pool::merge_worker_stats_(
    mesh_gen_worker_stats& worker
) -> void {
    if (worker.chunks == 0) {
        return;
    }

    gen_totals_.chunks += worker.chunks;
    gen_totals_.nanos += worker.nanos;
    gen_totals_.quads += worker.quads;
    gen_totals_.micros.insert(
        gen_totals_.micros.end(), worker.micros.begin(), worker.micros.end()
    );

    worker.chunks = 0;
    worker.nanos  = 0;
    worker.quads  = 0;
    worker.micros.clear();
}

auto mesh_pool::get_gen_stats() const -> mesh_gen_stats {
    std::scoped_lock lock(gen_mutex_);

    mesh_gen_stats out{};
    out.chunks      = gen_totals_.chunks;
    out.quads       = gen_totals_.quads;
    out.total_ms    = static_cast<float32>(static_cast<float64>(gen_totals_.nanos) / 1.0e6);
    out.queue_depth = static_cast<uint32>(gen_queue_.size());
    out.queue_peak  = gen_queue_peak_;

    if (gen_totals_.micros.empty()) {
        return out;
    }

    auto samples = gen_totals_.micros;
    std::ranges::sort(samples);

    const auto at = [&samples](float32 quantile) -> float32 {
        const auto count = static_cast<float32>(samples.size());
        const auto rank  = static_cast<uint64>(std::ceil(quantile * count));
        const auto index = std::clamp<uint64>(rank, 1, samples.size()) - 1;
        return static_cast<float32>(samples[index]);
    };

    out.mean_us = static_cast<float32>(
        static_cast<float64>(gen_totals_.nanos) / 1000.0 / static_cast<float64>(gen_totals_.chunks)
    );
    out.p50_us = at(0.50f);
    out.p99_us = at(0.99f);
    out.max_us = at(1.00f);

    return out;
}

auto mesh_pool::gen_thread_function() -> void {
    mesh_generation_storage storage;
    mesh_gen_worker_stats local;

    while (true) {
        std::unique_ptr<mesh_generation_task> task;

        {
            std::unique_lock lock(gen_mutex_);

            merge_worker_stats_(local);

            gen_cv_.wait(lock, [this] -> bool { return !gen_queue_.empty() || !gen_running_; });

            if (!gen_running_ && gen_queue_.empty()) {
                break;
            }

            if (!gen_queue_.empty()) {
                task = std::move(gen_queue_.front());
                gen_queue_.pop();
            }
        }

        if (task) {
            auto model_ptr = task->model_ref.lock();
            if (!model_ptr || model_ptr->get_identity() != task->identity) {
                task->promise.set_value(mesh{});
                continue;
            }

            const auto chunk_ptr = task->chunk_ref.lock();
            const mesh_source source{.voxels = *model_ptr, .chunk = chunk_ptr.get()};

            try {
                const auto started = std::chrono::steady_clock::now();
                mesh data = greedy_mesh_generator::generate_mesh_data(
                    storage, source, *registry_, task->opts
                );
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started
                );
                local.record(static_cast<uint64>(elapsed.count()), data.quads.size());

                task->promise.set_value(std::move(data));
            } catch (const std::exception&) {
                task->promise.set_exception(std::current_exception());
            }
        }
    }
}

}  // namespace vw::gfx
