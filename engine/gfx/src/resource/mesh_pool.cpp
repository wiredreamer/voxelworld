module vw.gfx;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {


mesh_pool::mesh_pool(
    const voxel_registry& registry, vw::job_system& jobs
)
    : registry_{&registry}, jobs_{&jobs}, storage_(jobs.worker_count()) {}

mesh_pool::~mesh_pool() {
    stop_gen_threads();
}

auto mesh_pool::stop_gen_threads() -> void {
    jobs_->close(vw::job_lane::mesh);
}

auto mesh_pool::live_slot_(
    mesh_key key, uint32 generation
) -> mesh_slot* {
    const auto it = slot_of_.find(key);
    if (it == slot_of_.end()) {
        return nullptr;
    }

    auto& slot = slots_[it->second];
    return slot.generation == generation ? &slot : nullptr;
}

auto mesh_pool::live_slot_(
    mesh_key key, uint32 generation
) const -> const mesh_slot* {
    const auto it = slot_of_.find(key);
    if (it == slot_of_.end()) {
        return nullptr;
    }

    const auto& slot = slots_[it->second];
    return slot.generation == generation ? &slot : nullptr;
}

auto mesh_pool::drop_mesh_(
    mesh_slot& slot
) -> void {
    if (!slot.has_mesh) {
        return;
    }

    slot.has_mesh = false;
    slot.data     = mesh{};
    --held_;
}

auto mesh_pool::drop_pending_(
    uint32 slot_index
) -> void {
    if (slot_index >= slots_.size() || !slots_[slot_index].pending.valid()) {
        return;
    }

    slots_[slot_index].pending = {};
    std::erase(pending_slots_, slot_index);
}

auto mesh_pool::release_slot_(
    uint32 slot_index
) -> void {
    auto& slot = slots_[slot_index];
    if (!slot.live) {
        return;
    }

    drop_mesh_(slot);
    drop_pending_(slot_index);

    slot_of_.erase(slot.key);
    slot.live = false;
    slot.key  = {};
    slot.model_ref.reset();
    slot.chunk_ref.reset();

    free_slots_.push_back(slot_index);
}

auto mesh_pool::retire_stale_(
    uint32 model_index, uint32 generation
) -> void {
    for (int32 level = 0; level < lod_level_count; ++level) {
        const auto it = slot_of_.find(mesh_key{model_index, static_cast<uint32>(level)});
        if (it == slot_of_.end() || slots_[it->second].generation == generation) {
            continue;
        }
        release_slot_(it->second);
    }
}

auto mesh_pool::acquire_slot_(
    mesh_key key, uint32 generation
) -> uint32 {
    retire_stale_(key.model_index, generation);

    if (const auto it = slot_of_.find(key); it != slot_of_.end()) {
        return it->second;
    }

    uint32 slot_index = 0;
    if (!free_slots_.empty()) {
        slot_index = free_slots_.back();
        free_slots_.pop_back();
        slots_[slot_index] = mesh_slot{};
    } else {
        slot_index = static_cast<uint32>(slots_.size());
        slots_.emplace_back();
    }

    auto& slot      = slots_[slot_index];
    slot.key        = key;
    slot.live       = true;
    slot.generation = generation;

    slot_of_[key] = slot_index;
    return slot_index;
}

[[nodiscard]] auto mesh_pool::has(
    const vw::asset::model_identity& identity, int32 lod_step
) const -> bool {
    const auto* slot = live_slot_(mesh_key_of(identity, lod_step), identity.generation);
    return slot != nullptr && slot->has_mesh;
}

[[nodiscard]] auto mesh_pool::is_pending(
    const vw::asset::model_identity& identity, int32 lod_step
) const -> bool {
    const auto* slot = live_slot_(mesh_key_of(identity, lod_step), identity.generation);
    return slot != nullptr && slot->pending.valid();
}

auto mesh_pool::request_mesh(
    const std::shared_ptr<vw::asset::model>& model_ptr,
    const std::shared_ptr<vw::asset::chunk_volume>& chunk_ptr,
    mesh_options opts
) -> void {
    const vw::asset::model_identity identity = model_ptr->get_identity();

    if (has(identity, opts.lod_step) || is_pending(identity, opts.lod_step)) {
        return;
    }

    const auto slot_index = acquire_slot_(mesh_key_of(identity, opts.lod_step), identity.generation);

    auto& slot     = slots_[slot_index];
    slot.model_ref = model_ptr;
    slot.chunk_ref = chunk_ptr;

    if (chunk_ptr != nullptr) {
        std::scoped_lock lock(stats_mutex_);
        bool whole = true;
        for (const face_direction face : all_face_directions) {
            if (!chunk_ptr->has_boundary_slice(face)) {
                ++missing_faces_[static_cast<std::size_t>(face)];
                whole = false;
            }
        }
        if (chunk_ptr->share_boundary() == nullptr) {
            ++blind_requests_;
        } else if (!whole) {
            ++partial_requests_;
        }
    }

    auto task = std::make_unique<mesh_generation_task>(
        identity, model_ptr, chunk_ptr,
        chunk_ptr ? chunk_ptr->share_boundary() : nullptr, opts
    );

    slot.pending = task->promise.get_future();
    pending_slots_.push_back(slot_index);

    jobs_->submit(
        vw::job_lane::mesh,
        [this, task = std::move(task)](uint32 worker) { generate_(*task, storage_[worker]); }
    );
}

[[nodiscard]] auto mesh_pool::get(
    const vw::asset::model_identity& identity, int32 lod_step
) const -> const mesh* {
    const auto* slot = live_slot_(mesh_key_of(identity, lod_step), identity.generation);
    return slot != nullptr && slot->has_mesh ? &slot->data : nullptr;
}

auto mesh_pool::remove(
    const vw::asset::model_identity& identity
) -> void {
    for (int32 level = 0; level < lod_level_count; ++level) {
        const auto it = slot_of_.find(mesh_key{identity.index, static_cast<uint32>(level)});
        if (it == slot_of_.end()) {
            continue;
        }
        release_slot_(it->second);
    }
}

auto mesh_pool::evict(
    const vw::asset::model_identity& identity, int32 lod_step
) -> void {
    if (auto* slot = live_slot_(mesh_key_of(identity, lod_step), identity.generation)) {
        drop_mesh_(*slot);
    }
}

auto mesh_pool::sweep_orphaned_() -> void {
    if (slots_.empty()) {
        sweep_cursor_ = 0;
        return;
    }

    if (sweep_cursor_ >= slots_.size()) {
        sweep_cursor_ = 0;
    }

    const auto limit  = std::min(slots_.size(), sweep_cursor_ + sweep_slots_per_frame_);
    std::size_t freed = 0;

    while (sweep_cursor_ < limit && freed < sweep_orphans_per_frame_) {
        const auto index = static_cast<uint32>(sweep_cursor_);
        auto& slot       = slots_[sweep_cursor_];
        ++sweep_cursor_;

        if (!slot.live || !slot.model_ref.expired()) {
            continue;
        }

        release_slot_(index);
        ++freed;
    }
}

auto mesh_pool::process_completed(
    uint32 max_meshes
) -> void {
    sweep_orphaned_();

    uint32 completed = 0;

    for (std::size_t i = 0; i < pending_slots_.size() && completed < max_meshes;) {
        const auto index = pending_slots_[i];
        auto& slot       = slots_[index];

        const bool ready = slot.pending.valid() &&
            slot.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready;

        if (slot.pending.valid() && !ready) {
            ++i;
            continue;
        }

        if (ready) {
            auto data    = slot.pending.get();
            slot.pending = {};

            if (!slot.has_mesh) {
                ++held_;
            }
            slot.has_mesh = true;
            slot.data     = std::move(data);
            held_peak_    = std::max(held_peak_, held_);

            if (const auto chunk = slot.chunk_ref.lock()) {
                chunk->release_boundary();
            }

            ++completed;
        }

        pending_slots_[i] = pending_slots_.back();
        pending_slots_.pop_back();
    }
}

auto mesh_pool::get_pending_count() const -> uint32 {
    return static_cast<uint32>(pending_slots_.size());
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
        out.blind    = blind_requests_;
        out.partial  = partial_requests_;
        out.missing  = missing_faces_;
    }

    const auto lane = jobs_->get_lane_stats(vw::job_lane::mesh);
    out.queue_depth = lane.queued;
    out.queue_peak  = lane.peak;
    out.held        = held_;
    out.held_peak   = held_peak_;

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

    const mesh_source source{.voxels = *model_ptr, .boundary = task.boundary.get()};

    try {
        const auto started = std::chrono::steady_clock::now();
        mesh data = greedy_mesh_generator::generate_mesh_data(
            storage, source, task.opts
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
