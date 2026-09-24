export module vw.asset:model.lod;

import std;

import vw.core;

export namespace vw::asset {

// см. docs/lod-plan.md#шаг-в-мешере
inline constexpr int32 lod_level_count = 4;

[[nodiscard]] constexpr auto lod_step_of(int32 level) -> int32 {
    return 1 << level;
}

[[nodiscard]] constexpr auto lod_level_of(int32 step) -> int32 {
    return std::countr_zero(static_cast<uint32>(step));
}

}  // namespace vw::asset
