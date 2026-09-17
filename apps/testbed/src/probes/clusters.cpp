module vw.testbed;

import std;
import vw.core;
import vw.gfx;

namespace vw::testbed {

auto cluster_probe::collect(gfx::renderer& renderer, bool measuring) -> void {
    for (const gfx::cull_list kind : {gfx::cull_list::sources, gfx::cull_list::blobs}) {
        auto frame = renderer.take_cluster_readback(kind);

        if (frame && measuring) {
            account_(kind, *frame);
        }
    }
}

auto cluster_probe::account_(gfx::cull_list kind, const gfx::cluster_readback& frame) -> void {
    const uint32 clusters = frame.grid.cluster_count();

    if (frame.counts.size() < static_cast<std::size_t>(clusters) + 1) {
        return;
    }

    tally& counted = tally_[static_cast<std::size_t>(kind)];

    ++counted.frames;
    counted.grid = frame.grid;
    counted.cap  = frame.cap;

    uint64 assignments = 0;
    uint64 lit         = 0;
    uint32 peak        = 0;

    for (uint32 cluster = 0; cluster < clusters; ++cluster) {
        const uint32 listed = frame.counts[cluster];

        assignments += listed;
        lit += (listed > 0) ? 1 : 0;
        peak = std::max(peak, listed);
    }

    counted.assignments += assignments;
    counted.lit += lit;
    counted.peak = std::max(counted.peak, peak);

    const uint32 overflow = frame.counts[clusters];

    counted.overflow += overflow;
    counted.overflow_frames += (overflow > 0) ? 1 : 0;

    if (verify_every_ == 0 || frame.indices.empty() || (counted.frames % verify_every_) != 0) {
        return;
    }

    verify_frame_(kind, frame);
}

auto cluster_probe::verify_frame_(gfx::cull_list kind, const gfx::cluster_readback& frame)
    -> void {
    const auto slot = static_cast<std::size_t>(kind);

    auto& reference = reference_[slot];

    if (!reference || reference->get_grid() != frame.grid || reference->get_cap() != frame.cap) {
        reference = std::make_unique<spatial::cluster_lights>(frame.grid, frame.cap);
    } else {
        reference->clear();
    }

    for (std::size_t i = 0; i < frame.columns.size(); ++i) {
        reference->add(static_cast<uint32>(i), frame.columns[i]);
    }

    const auto check = spatial::check_clusters(*reference, frame.counts, frame.indices);

    tally& counted = tally_[slot];

    ++counted.verified;
    counted.clusters += check.clusters_compared;
    counted.bad_counts += check.count_mismatches;
    counted.bad_sets += check.set_mismatches;

    if (check.ok()) {
        return;
    }

    ++counted.bad_frames;

    if (counted.bad_frames > 1) {
        return;
    }

    counted.worst = check;

    log::warn(
        "verify-lights ({}): {} counts and {} lists disagree with the reference, "
        "overflow tally {}; first at cluster {}, {} on the GPU and {} expected",
        kind == gfx::cull_list::sources ? "sources" : "bodies",
        check.count_mismatches, check.set_mismatches,
        check.overflow_matches ? "matches" : "does not match", check.first_bad,
        check.actual_count, check.reference_count
    );
}

auto cluster_probe::collect_report(gfx::report& out) const -> void {
    report_list_(out, gfx::cull_list::sources, "sources");
    report_list_(out, gfx::cull_list::blobs, "bodies");
}

auto cluster_probe::report_list_(gfx::report& out, gfx::cull_list kind, std::string_view what) const
    -> void {
    const tally& counted = tally_[static_cast<std::size_t>(kind)];

    if (counted.frames == 0) {
        return;
    }

    const auto frames = static_cast<float64>(counted.frames);
    const auto lit    = static_cast<float64>(counted.lit);
    const auto total  = static_cast<float64>(counted.grid.cluster_count());

    auto& section = out.section(std::format("clusters ({})", what));

    section.value("tiles_x", static_cast<uint64>(counted.grid.tiles_x()))
        .value("tiles_y", static_cast<uint64>(counted.grid.tiles_y()))
        .value("tile_px", static_cast<uint64>(counted.grid.tile_size))
        .value("slices", static_cast<uint64>(counted.grid.slices))
        .value("cap", static_cast<uint64>(counted.cap))
        .value("clusters", static_cast<uint64>(counted.grid.cluster_count()))
        .value("frames", counted.frames)
        .value("assignments_per_frame", static_cast<float64>(counted.assignments) / frames, 0)
        .value("lit_percent", total > 0.0 ? 100.0 * lit / (frames * total) : 0.0, 1)
        .value("mean_list", lit > 0.0 ? static_cast<float64>(counted.assignments) / lit : 0.0, 1)
        .value("peak_list", static_cast<uint64>(counted.peak))
        .value("overflow_per_frame", static_cast<float64>(counted.overflow) / frames, 1)
        .value("overflow_frames", counted.overflow_frames);

    if (verify_every_ == 0) {
        return;
    }

    auto& verify = section.sub("verify");

    verify.value("frames_checked", counted.verified)
        .value("frames_disagreed", counted.bad_frames)
        .value("bad_counts", counted.bad_counts)
        .value("bad_sets", counted.bad_sets)
        .value("clusters_compared", counted.clusters);

    if (counted.bad_frames == 0) {
        return;
    }

    verify.value("first_bad_cluster", static_cast<uint64>(counted.worst.first_bad))
        .value("first_bad_gpu_count", static_cast<uint64>(counted.worst.actual_count))
        .value("first_bad_reference_count", static_cast<uint64>(counted.worst.reference_count))
        .value("overflow_matches", counted.worst.overflow_matches);
}

}  // namespace vw::testbed
