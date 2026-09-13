export module vw.asset:model.light_channel;

import std;

import vw.core;

export namespace vw::asset {

// К какому из двух светов относится уровень. Оба живут в одном байте — небо в
// младшем полубайте, свет блоков в старшем — потому что уровень укладывается в
// 0..15, а массив и так был байтом на воксель с пустой половиной. Второй массив
// стоил бы пяти мегабайт на воркер впустую.
enum class light_channel : uint8 { sky = 0, block = 1 };

[[nodiscard]] constexpr auto shift_of(light_channel channel) -> int32 {
    return channel == light_channel::sky ? 0 : 4;
}

}  // namespace vw::asset
