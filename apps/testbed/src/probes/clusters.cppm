export module vw.testbed:probes.clusters;

import std;

import vw.core;
import vw.gfx;

export namespace vw::testbed {

class cluster_probe {
public:
    cluster_probe(bool stats, uint32 verify_every)
        : stats_{stats}, verify_every_{verify_every} {}

    [[nodiscard]] auto wanted() const -> bool {
        return stats_ || verify_every_ > 0;
    }

    [[nodiscard]] auto readback_level() const -> gfx::cluster_readback_level {
        return verify_every_ > 0 ? gfx::cluster_readback_level::full
                                 : gfx::cluster_readback_level::counts;
    }

    auto collect(gfx::renderer& renderer, bool measuring) -> void;

    auto collect_report(gfx::report& out) const -> void;

private:
    struct tally {
        spatial::cluster_grid grid{};
        uint32 cap = 0;

        uint64 frames          = 0;
        uint64 assignments     = 0;
        uint64 lit             = 0;
        uint32 peak            = 0;
        uint64 overflow        = 0;
        uint64 overflow_frames = 0;

        uint64 verified   = 0;
        uint64 bad_frames = 0;
        uint64 clusters   = 0;
        uint64 bad_counts = 0;
        uint64 bad_sets   = 0;
        spatial::cluster_check worst{};
    };

    auto account_(gfx::cull_list kind, const gfx::cluster_readback& frame) -> void;

    auto verify_frame_(gfx::cull_list kind, const gfx::cluster_readback& frame) -> void;

    auto report_list_(gfx::report& out, gfx::cull_list kind, std::string_view what) const -> void;

    bool stats_           = false;
    uint32 verify_every_  = 0;

    std::array<tally, gfx::cull_list_count> tally_{};
    std::array<std::unique_ptr<spatial::cluster_lights>, gfx::cull_list_count> reference_;
};

}  // namespace vw::testbed
