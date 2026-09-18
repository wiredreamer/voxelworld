module vw.gfx;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {


mesh_pool::mesh_pool(
    vulkan_context& context, const voxel_registry& registry, vw::job_system& jobs
)
    : context_{&context}, registry_{&registry}, jobs_{&jobs}, storage_(jobs.worker_count()) {}

mesh_pool::~mesh_pool() {
    stop_gen_threads();
}

auto mesh_pool::stop_gen_threads() -> void {
    jobs_->close(vw::job_lane::mesh);
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
        auto task = std::make_unique<mesh_generation_task>(
            identity, model_ptr, chunk_ptr,
            chunk_ptr ? chunk_ptr->share_boundary() : nullptr,
            chunk_ptr ? chunk_ptr->share_sky_light() : nullptr,
            chunk_ptr ? chunk_ptr->share_block_light() : nullptr, opts
        );
        pending_meshes_[identity] = task->promise.get_future();

        jobs_->submit(
            vw::job_lane::mesh,
            [this, task = std::move(task)](uint32 worker) { generate_(*task, storage_[worker]); }
        );
    }
}

[[nodiscard]] auto mesh_pool::get(
    const vw::asset::model_identity& identity
) const -> const mesh* {
    auto iter = meshes_.find(identity);
    return iter != meshes_.end() ? &iter->second : nullptr;
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

            meshes_.insert_or_assign(identity, std::move(data));

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

auto mesh_pool::record_chunk_(uint64 elapsed_ns, uint64 quads) -> void {
    std::scoped_lock lock(stats_mutex_);
    gen_latency_.record(elapsed_ns);
    gen_quads_ += quads;
}

auto mesh_pool::get_gen_stats() const -> mesh_gen_stats {
    mesh_gen_stats out{};

    {
        std::scoped_lock lock(stats_mutex_);
        const auto summary = gen_latency_.summarize();

        out.chunks   = summary.count;
        out.quads    = gen_quads_;
        out.total_ms = summary.total_ms;
        out.mean_us  = summary.mean_us;
        out.p50_us   = summary.p50_us;
        out.p99_us   = summary.p99_us;
        out.max_us   = summary.max_us;
    }

    const auto lane = jobs_->get_lane_stats(vw::job_lane::mesh);
    out.queue_depth = lane.queued;
    out.queue_peak  = lane.peak;

    return out;
}

auto mesh_pool::generate_(
    mesh_generation_task& task, mesh_generation_storage& storage
) -> void {
    auto model_ptr = task.model_ref.lock();
    if (!model_ptr || model_ptr->get_identity() != task.identity) {
        task.promise.set_value(mesh{});
        return;
    }

    const mesh_source source{
        .voxels   = *model_ptr,
        .boundary = task.boundary.get(),
        .sky      = task.sky.get(),
        .block    = task.block.get()
    };

    try {
        const auto started = std::chrono::steady_clock::now();
        mesh data = greedy_mesh_generator::generate_mesh_data(
            storage, source, *registry_, task.opts
        );
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started
        );
        record_chunk_(static_cast<uint64>(elapsed.count()), data.quads.size());

        task.promise.set_value(std::move(data));
    } catch (const std::exception&) {
        task.promise.set_exception(std::current_exception());
    }
}

}  // namespace vw::gfx
