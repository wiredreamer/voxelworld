export module vw.core:color;

import std;

import :types;

export namespace vw {
struct color {
    uint32 value;

    constexpr color() : value(0U) {}
    constexpr explicit color(
        uint32 value_
    )
        : value(value_) {}
    constexpr color(
        uint8 r, uint8 g, uint8 b, uint8 a = 255
    )
        : value((static_cast<uint32>(r) << 24) |
                (static_cast<uint32>(g) << 16) |
                (static_cast<uint32>(b) << 8) |
                static_cast<uint32>(a)) {}

    constexpr color(const color&)                    = default;
    constexpr auto operator=(const color&) -> color& = default;
    constexpr color(color&&)                         = default;
    constexpr auto operator=(color&&) -> color&      = default;

    constexpr auto operator==(const color&) const -> bool = default;
    constexpr auto operator!=(const color&) const -> bool = default;

    [[nodiscard]] constexpr auto is_empty() const -> bool;

    [[nodiscard]] constexpr auto r() const -> uint8;
    [[nodiscard]] constexpr auto g() const -> uint8;
    [[nodiscard]] constexpr auto b() const -> uint8;
    [[nodiscard]] constexpr auto a() const -> uint8;
};

// Палитра Apollo: https://lospec.com/palette-list/apollo
// Шесть рамп по шесть шагов, серая на десять, плюс чистые белый и чёрный.
// Номер — шаг рампы от тёмного к светлому. Коричневых две: brown холодная и
// уходит в кремовый, amber тёплая и уходит в золото.
namespace colors {

// Не цвет, а его отсутствие: полностью прозрачный.
constexpr auto empty = color(0x00000000);

constexpr auto blue_0   = color(0x172038FF);
constexpr auto blue_1   = color(0x253A5EFF);
constexpr auto blue_2   = color(0x3C5E8BFF);
constexpr auto blue_3   = color(0x4F8FBAFF);
constexpr auto blue_4   = color(0x73BED3FF);
constexpr auto blue_5   = color(0xA4DDDBFF);

constexpr auto green_0  = color(0x19332DFF);
constexpr auto green_1  = color(0x25562EFF);
constexpr auto green_2  = color(0x468232FF);
constexpr auto green_3  = color(0x75A743FF);
constexpr auto green_4  = color(0xA8CA58FF);
constexpr auto green_5  = color(0xD0DA91FF);

constexpr auto brown_0  = color(0x4D2B32FF);
constexpr auto brown_1  = color(0x7A4841FF);
constexpr auto brown_2  = color(0xAD7757FF);
constexpr auto brown_3  = color(0xC09473FF);
constexpr auto brown_4  = color(0xD7B594FF);
constexpr auto brown_5  = color(0xE7D5B3FF);

constexpr auto amber_0  = color(0x341C27FF);
constexpr auto amber_1  = color(0x602C2CFF);
constexpr auto amber_2  = color(0x884B2BFF);
constexpr auto amber_3  = color(0xBE772BFF);
constexpr auto amber_4  = color(0xDE9E41FF);
constexpr auto amber_5  = color(0xE8C170FF);

constexpr auto red_0    = color(0x241527FF);
constexpr auto red_1    = color(0x411D31FF);
constexpr auto red_2    = color(0x752438FF);
constexpr auto red_3    = color(0xA53030FF);
constexpr auto red_4    = color(0xCF573CFF);
constexpr auto red_5    = color(0xDA863EFF);

constexpr auto purple_0 = color(0x1E1D39FF);
constexpr auto purple_1 = color(0x402751FF);
constexpr auto purple_2 = color(0x7A367BFF);
constexpr auto purple_3 = color(0xA23E8CFF);
constexpr auto purple_4 = color(0xC65197FF);
constexpr auto purple_5 = color(0xDF84A5FF);

constexpr auto gray_0   = color(0x090A14FF);
constexpr auto gray_1   = color(0x10141FFF);
constexpr auto gray_2   = color(0x151D28FF);
constexpr auto gray_3   = color(0x202E37FF);
constexpr auto gray_4   = color(0x394A50FF);
constexpr auto gray_5   = color(0x577277FF);
constexpr auto gray_6   = color(0x819796FF);
constexpr auto gray_7   = color(0xA8B5B2FF);
constexpr auto gray_8   = color(0xC7CFCCFF);
constexpr auto gray_9   = color(0xEBEDE9FF);

constexpr auto white    = color(0xFFFFFFFF);

constexpr auto black    = color(0x000000FF);

constexpr std::array all = {
    blue_0, blue_1, blue_2, blue_3, blue_4, blue_5,
    green_0, green_1, green_2, green_3, green_4, green_5,
    brown_0, brown_1, brown_2, brown_3, brown_4, brown_5,
    amber_0, amber_1, amber_2, amber_3, amber_4, amber_5,
    red_0, red_1, red_2, red_3, red_4, red_5,
    purple_0, purple_1, purple_2, purple_3, purple_4, purple_5,
    gray_0, gray_1, gray_2, gray_3, gray_4, gray_5, gray_6, gray_7, gray_8, gray_9,
    white,
    black,
};

}  // namespace colors
}  // namespace vw

namespace vw {

constexpr auto color::is_empty() const -> bool {
    return value == 0;
}

constexpr auto color::r() const -> uint8 {
    return static_cast<uint8>((value >> 24) & 0xFF);
}

constexpr auto color::g() const -> uint8 {
    return static_cast<uint8>((value >> 16) & 0xFF);
}

constexpr auto color::b() const -> uint8 {
    return static_cast<uint8>((value >> 8) & 0xFF);
}

constexpr auto color::a() const -> uint8 {
    return static_cast<uint8>(value & 0xFF);
}
}  // namespace vw
