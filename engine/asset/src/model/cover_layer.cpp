module vw.asset;

import std;
import vw.core;

namespace vw::asset {

namespace {

constexpr int32 axis_limit = 256;

auto next_revision() -> uint64 {
    static std::atomic<uint64> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

auto fits(vec3i support) -> bool {
    return support.x >= 0 && support.y >= 0 && support.z >= 0 && support.x < axis_limit &&
           support.y < axis_limit && support.z < axis_limit;
}

}  // namespace

cover_layer::cover_layer() : revision_{next_revision()} {}

cover_layer::cover_layer(
    std::span<const entry> entries
)
    : revision_{next_revision()} {
    packed_.reserve(entries.size());
    for (const entry& e : entries) {
        if (e.form != 0 && fits(e.support)) {
            packed_.push_back((key_of_(e.support) << 8U) | e.form);
        }
    }
    std::ranges::sort(packed_);
    const auto same_key = [](uint32 a, uint32 b) -> bool { return (a >> 8U) == (b >> 8U); };
    const auto [first, last] = std::ranges::unique(packed_, same_key);
    packed_.erase(first, last);
}

auto cover_layer::key_of_(
    vec3i support
) -> uint32 {
    return (static_cast<uint32>(support.z) << 16U) | (static_cast<uint32>(support.y) << 8U) |
           static_cast<uint32>(support.x);
}

auto cover_layer::support_of_(
    uint32 packed
) -> vec3i {
    const uint32 key = packed >> 8U;
    return {
        static_cast<int32>(key & 0xFFU), static_cast<int32>((key >> 8U) & 0xFFU),
        static_cast<int32>((key >> 16U) & 0xFFU)
    };
}

auto cover_layer::form_at(
    vec3i support
) const -> uint8 {
    if (!fits(support)) {
        return 0;
    }
    const uint32 low = key_of_(support) << 8U;
    const auto it    = std::ranges::lower_bound(packed_, low);
    if (it == packed_.end() || (*it >> 8U) != (low >> 8U)) {
        return 0;
    }
    return static_cast<uint8>(*it & 0xFFU);
}

auto cover_layer::set(
    vec3i support, uint8 form
) -> bool {
    if (!fits(support)) {
        return false;
    }
    const uint32 low = key_of_(support) << 8U;
    const auto it    = std::ranges::lower_bound(packed_, low);
    const bool found = it != packed_.end() && (*it >> 8U) == (low >> 8U);

    if (form == 0) {
        if (!found) {
            return false;
        }
        packed_.erase(it);
    } else if (found) {
        if ((*it & 0xFFU) == form) {
            return false;
        }
        *it = low | form;
    } else {
        packed_.insert(it, low | form);
    }

    revision_ = next_revision();
    return true;
}

}  // namespace vw::asset
