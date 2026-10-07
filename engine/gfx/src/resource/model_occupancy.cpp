module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.asset;

namespace vw::gfx {

model_occupancy_buffer::model_occupancy_buffer(vulkan_context& context)
    : words_{std::make_unique<buffer>(
          context, byte_size(), vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
      )}
    , mapped_{static_cast<uint32*>(words_->map())} {}

model_occupancy_buffer::~model_occupancy_buffer() {
    words_->unmap();
}

auto model_occupancy_buffer::words_for_(vec3i size) -> uint32 {
    return asset::bit_row_words(size.x) * static_cast<uint32>(size.y) *
           static_cast<uint32>(size.z);
}

auto model_occupancy_buffer::corners_of_(const volume& held) -> instance_corners {
    const auto packed = static_cast<uint32>(held.size.x) |
                        (static_cast<uint32>(held.size.y) << 8) |
                        (static_cast<uint32>(held.size.z) << 16);
    return {
        .packed_size = static_cast<float32>(packed),
        .word_offset = static_cast<float32>(held.offset),
    };
}

auto model_occupancy_buffer::take_range_(uint32 words) -> std::optional<uint32> {
    for (std::size_t index = 0; index < freed_.size(); ++index) {
        auto& range = freed_[index];
        if (range.ripe_at > frame_ || range.words < words) {
            continue;
        }

        const uint32 offset = range.offset;
        range.offset += words;
        range.words -= words;
        if (range.words == 0) {
            freed_[index] = freed_.back();
            freed_.pop_back();
        }
        return offset;
    }

    if (words > capacity_words - high_water_) {
        return std::nullopt;
    }

    const uint32 offset = high_water_;
    high_water_ += words;
    return offset;
}

auto model_occupancy_buffer::give_back_(const volume& held) -> void {
    freed_.push_back({
        .offset  = held.offset,
        .words   = held.words,
        .ripe_at = frame_ + frames_in_flight + 1,
    });
    stats_.words_used -= held.words;
}

auto model_occupancy_buffer::write_(const asset::model& voxels, const volume& held) -> void {
    voxels.build_bit_rows(std::span<uint32>{mapped_ + held.offset, held.words});
}

auto model_occupancy_buffer::acquire(const asset::model& voxels) -> instance_corners {
    const uint32 model_index = voxels.get_identity().index;

    if (const auto found = volumes_.find(model_index); found != volumes_.end()) {
        ++found->second.holders;
        return corners_of_(found->second);
    }

    const vec3i size = voxels.size();
    if (size.x > longest_side || size.y > longest_side || size.z > longest_side) {
        ++stats_.refused;
        return instance_corners::baked();
    }

    const uint32 words = words_for_(size);
    const auto offset  = take_range_(words);
    if (!offset) {
        ++stats_.refused;
        return instance_corners::baked();
    }

    const volume held{.offset = *offset, .words = words, .size = size, .holders = 1};
    write_(voxels, held);

    volumes_.emplace(model_index, held);
    ++stats_.volumes;
    stats_.words_used += words;

    return corners_of_(held);
}

auto model_occupancy_buffer::refresh(const asset::model& voxels) -> instance_corners {
    const auto found = volumes_.find(voxels.get_identity().index);
    if (found == volumes_.end()) {
        return instance_corners::baked();
    }

    volume& held = found->second;

    const vec3i size = voxels.size();
    if (size != held.size) {
        const uint32 words = words_for_(size);
        const bool fits    = size.x <= longest_side && size.y <= longest_side &&
                             size.z <= longest_side;
        const auto offset  = fits ? take_range_(words) : std::nullopt;
        if (!offset) {
            return corners_of_(held);
        }

        give_back_(held);
        held.offset = *offset;
        held.words  = words;
        held.size   = size;
        stats_.words_used += words;
    }

    write_(voxels, held);
    return corners_of_(held);
}

auto model_occupancy_buffer::release(uint32 model_index) -> void {
    const auto found = volumes_.find(model_index);
    if (found == volumes_.end()) {
        return;
    }

    if (--found->second.holders > 0) {
        return;
    }

    give_back_(found->second);
    volumes_.erase(found);
    --stats_.volumes;
}

auto model_occupancy_buffer::next_frame() -> void {
    ++frame_;
}

}  // namespace vw::gfx
