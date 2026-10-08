export module vw.gfx:engine.frame_recorder;

import std;

import vw.core;
import :engine.report;
import vw.world;
import :engine.stats;
import :renderer;

namespace vw::gfx {

struct frame_sample {
    engine_stats engine{};
    render_timing_stats render{};
    ecs::world_grid_system_stats grid{};
    ecs::world_update_stats systems{};

    uint32 light_bricks     = 0;
    uint32 light_waiting    = 0;
    uint32 occupancy_packed = 0;
    uint32 meshes_pending   = 0;
};

class frame_recorder final {
public:
    explicit frame_recorder(uint32 capacity);

    auto record(const frame_sample& sample) -> void;

    [[nodiscard]] auto sample_count() const -> uint32;
    [[nodiscard]] auto report() const -> std::string;

    auto collect(gfx::report& out) const -> void;

    // см. docs/optimization.md#покадровый-ряд
    auto write_series(std::ostream& out) const -> void;

private:
    using stage_getter = auto (*)(const frame_sample&) -> float32;

    [[nodiscard]] auto percentile_of(stage_getter get, float32 quantile) const -> float32;

    std::vector<frame_sample> samples_;
    mutable std::vector<float32> scratch_;
};

}  // namespace vw::gfx
