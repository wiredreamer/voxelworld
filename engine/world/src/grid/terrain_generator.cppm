export module vw.world:terrain.generator;

import std;

import vw.core;
import vw.asset;

export namespace vw::ecs {

struct chunk_data {
    vec3i coord;
    std::shared_ptr<asset::chunk_volume> volume;
};

struct chunk_y_range {
    int32 min_y = 0;
    int32 max_y = 0;
};

struct terrain_context {
    int32 cx;
    int32 cz;
    int32 voxels_per_cell = 1;
    std::function<auto(int32 y) -> chunk_data&> create_chunk;
};

class terrain_generator {
public:
    virtual ~terrain_generator()                       = default;
    virtual auto generate(terrain_context& ctx) -> void = 0;
};

}  // namespace vw::ecs
