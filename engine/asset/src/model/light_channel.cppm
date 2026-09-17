export module vw.asset:model.light_channel;

import std;

import vw.core;

export namespace vw::asset {

enum class light_channel : uint8 { sky = 0, block = 1 };

[[nodiscard]] constexpr auto shift_of(light_channel channel) -> int32 {
    return channel == light_channel::sky ? 0 : 4;
}

}  // namespace vw::asset
