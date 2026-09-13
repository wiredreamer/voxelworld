module vw.asset;

import std;
import vw.core;

namespace vw::asset {

auto voxel_batch::apply_to(model& target) const -> void {
    model_writer writer{target};
    writer.apply(*this);
}

}  // namespace vw::asset
