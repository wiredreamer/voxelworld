export module vw.world:terrain.regions;
import :terrain.noise;

import std;

import vw.core;

export namespace vw::ecs {

struct region_sample {
    vec2i cell{};
    float64 site_x        = 0.0;
    float64 site_z        = 0.0;
    float64 edge_distance = 0.0;
};

class region_map {
public:
    struct params {
        uint32 seed               = 42;
        float64 spacing_voxels    = 2048.0;
        float64 jitter            = 0.35;
        float64 warp_voxels       = 60.0;
        float64 warp_frequency    = 0.004;
    };

    explicit region_map(params p);

    [[nodiscard]] auto sample(float64 x, float64 z) const -> region_sample;
    [[nodiscard]] auto site_of(vec2i cell) const -> std::pair<float64, float64>;

    [[nodiscard]] static auto is_home(vec2i cell) -> bool {
        return cell.x == 0 && cell.y == 0;
    }

    [[nodiscard]] auto get_params() const -> const params& {
        return params_;
    }

private:
    params params_;
    perlin_noise warp_;
};

}  // namespace vw::ecs
