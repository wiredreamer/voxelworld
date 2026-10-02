module vw.asset;

import std;
import vw.core;

namespace vw::asset {

namespace {

constexpr std::array all_properties{
    animation_property::position,
    animation_property::rotation,
    animation_property::scale,
};

template <animation_property Prop>
auto put_in_channel(
    animation_track& track, const pose_key& key,
    const typename animation_property_traits<Prop>::type& value
) -> void {
    using value_type = typename animation_property_traits<Prop>::type;

    if (!track.has_channel(Prop)) {
        track.add<Prop>(make_animation_channel<Prop>());
    }

    auto& channel = std::get<animation_channel<value_type>>(*track.get_channel_mut(Prop));

    std::vector<keyframe<value_type>> keys = channel.get_keyframes();
    std::erase_if(keys, [&key](const keyframe<value_type>& present) {
        return std::abs(present.time - key.time) < same_instant_seconds;
    });

    keyframe<value_type> placed{key.time, value};
    placed.interp      = key.interp;
    placed.tangent_in  = key.tangent_in;
    placed.tangent_out = key.tangent_out;
    keys.push_back(placed);

    channel.set_keyframes(std::move(keys));
}

auto drop_in_channel(
    animation_track& track, animation_property property, float32 from, float32 to
) -> uint32 {
    auto* channel_var = track.get_channel_mut(property);
    if (channel_var == nullptr) {
        return 0;
    }

    const uint32 dropped = std::visit(
        [from, to](auto& channel) -> uint32 {
            auto keys         = channel.get_keyframes();
            const auto before = keys.size();

            std::erase_if(keys, [from, to](const auto& present) {
                return present.time > from - same_instant_seconds &&
                       present.time < to + same_instant_seconds;
            });

            const auto removed = static_cast<uint32>(before - keys.size());
            if (removed > 0) {
                channel.set_keyframes(std::move(keys));
            }
            return removed;
        },
        *channel_var
    );

    const bool emptied = std::visit(
        [](const auto& channel) { return channel.is_empty(); }, *track.get_channel(property)
    );
    if (emptied) {
        track.remove_channel(property);
    }
    return dropped;
}

}  // namespace

auto put_key(animation_track& track, const pose_key& key) -> void {
    if (key.position) {
        put_in_channel<animation_property::position>(track, key, *key.position);
    }
    if (key.rotation) {
        put_in_channel<animation_property::rotation>(track, key, *key.rotation);
    }
    if (key.scale) {
        put_in_channel<animation_property::scale>(track, key, *key.scale);
    }
    track.mark_dirty();
}

auto drop_keys(
    animation_track& track, std::optional<animation_property> property, float32 from, float32 to
) -> uint32 {
    uint32 dropped = 0;
    for (const animation_property candidate : all_properties) {
        if (!property || *property == candidate) {
            dropped += drop_in_channel(track, candidate, from, to);
        }
    }

    if (dropped > 0) {
        track.mark_dirty();
    }
    return dropped;
}

auto count_keys(const animation_track& track) -> std::size_t {
    std::size_t count = 0;
    for (const animation_channel_variant& channel_var : track.get_channels()) {
        count += std::visit(
            [](const auto& channel) { return channel.keyframe_count(); }, channel_var
        );
    }
    return count;
}

}  // namespace vw::asset
