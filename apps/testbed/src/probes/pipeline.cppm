export module vw.testbed:probes.pipeline;

import std;

import vw.core;
import vw.gfx;

export namespace vw::testbed {

// см. docs/rendering.md#приборы-кадра
class pipeline_probe {
public:
    explicit pipeline_probe(bool wanted) : wanted_{wanted} {}

    [[nodiscard]] auto wanted() const -> bool {
        return wanted_;
    }

    auto collect(const gfx::renderer& renderer, bool measuring) -> void;

    auto collect_report(gfx::report& out) const -> void;

private:
    bool wanted_ = false;

    uint64 pipeline_frames = 0;
    uint64 vertex_runs     = 0;
    uint64 fragment_runs   = 0;
    uint64 primitives      = 0;

    uint64 cull_frames      = 0;
    uint64 commands_offered = 0;
    uint64 commands_drawn   = 0;

    uint64 screen_pixels = 0;
    uint32 msaa_samples  = 1;
};

}  // namespace vw::testbed
