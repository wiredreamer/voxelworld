export module vw.core:types;

import std;

export namespace vw {

using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

using int8  = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using float32 = float;
using float64 = double;

struct string_hash {
    using is_transparent = void;

    [[nodiscard]] auto operator()(std::string_view text) const noexcept -> std::size_t {
        return std::hash<std::string_view>{}(text);
    }
};

template <typename T>
using string_map = std::unordered_map<std::string, T, string_hash, std::equal_to<>>;

using string_set = std::unordered_set<std::string, string_hash, std::equal_to<>>;

}  // namespace vw
