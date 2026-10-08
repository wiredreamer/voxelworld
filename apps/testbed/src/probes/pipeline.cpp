module vw.testbed;

import std;
import vw.core;
import vw.gfx;

namespace vw::testbed {

auto pipeline_probe::collect(
    const gfx::renderer& renderer, bool measuring
) -> void {
    if (!wanted_ || !measuring) {
        return;
    }

    const gfx::frame_probe_stats& frame = renderer.get_frame_probe_stats();

    const vec2<uint32> extent = renderer.get_render_extent();
    screen_pixels             = uint64{extent.x} * uint64{extent.y};
    msaa_samples              = renderer.get_msaa_samples();

    if (frame.pipeline_counted) {
        ++pipeline_frames;
        vertex_runs   += frame.vertex_invocations;
        fragment_runs += frame.fragment_invocations;
        primitives    += frame.clipped_primitives;
    }
    if (frame.cull_counted) {
        ++cull_frames;
        commands_offered += frame.commands_offered;
        commands_drawn   += frame.commands_drawn;
    }
}

auto pipeline_probe::collect_report(
    gfx::report& out
) const -> void {
    if (!wanted_) {
        return;
    }

    auto& section = out.section("pipeline");

    section.value("pipeline_frames", pipeline_frames).value("cull_frames", cull_frames);

    if (pipeline_frames > 0) {
        const auto frames   = static_cast<float64>(pipeline_frames);
        const auto fragment = static_cast<float64>(fragment_runs) / frames;

        section.value("vertex_runs_per_frame", static_cast<float64>(vertex_runs) / frames, 0)
            .value("primitives_per_frame", static_cast<float64>(primitives) / frames, 0)
            .value("fragment_runs_per_frame", fragment, 0)
            .value("screen_pixels", screen_pixels)
            .value("msaa_samples", static_cast<uint64>(msaa_samples))
            .value(
                "fragment_runs_per_pixel",
                screen_pixels > 0 ? fragment / static_cast<float64>(screen_pixels) : 0.0, 3
            );
    }

    if (cull_frames > 0) {
        const auto frames  = static_cast<float64>(cull_frames);
        const auto offered = static_cast<float64>(commands_offered) / frames;
        const auto drawn   = static_cast<float64>(commands_drawn) / frames;

        section.value("commands_offered_per_frame", offered, 0)
            .value("commands_drawn_per_frame", drawn, 0)
            .value("commands_drawn_percent", offered > 0.0 ? 100.0 * drawn / offered : 0.0, 1);
    }
}

}  // namespace vw::testbed
