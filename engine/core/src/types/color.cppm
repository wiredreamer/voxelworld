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

namespace colors {

constexpr auto empty = color(0x00000000);

constexpr auto blue_0    = color(0x172038FF);
constexpr auto blue_1    = color(0x1E2D4BFF);
constexpr auto blue_2    = color(0x253A5EFF);
constexpr auto blue_3    = color(0x304C74FF);
constexpr auto blue_4    = color(0x3C5E8BFF);
constexpr auto blue_5    = color(0x4676A2FF);
constexpr auto blue_6    = color(0x4F8FBAFF);
constexpr auto blue_7    = color(0x61A6C7FF);
constexpr auto blue_8    = color(0x73BED3FF);
constexpr auto blue_9    = color(0x8CCED7FF);
constexpr auto blue_10   = color(0xA4DDDBFF);

constexpr auto green_0   = color(0x19332DFF);
constexpr auto green_1   = color(0x1F442EFF);
constexpr auto green_2   = color(0x25562EFF);
constexpr auto green_3   = color(0x356C31FF);
constexpr auto green_4   = color(0x468232FF);
constexpr auto green_5   = color(0x5E943AFF);
constexpr auto green_6   = color(0x75A743FF);
constexpr auto green_7   = color(0x8FB84DFF);
constexpr auto green_8   = color(0xA8CA58FF);
constexpr auto green_9   = color(0xBCD276FF);
constexpr auto green_10  = color(0xD0DA91FF);

constexpr auto brown_0   = color(0x4D2B32FF);
constexpr auto brown_1   = color(0x63393AFF);
constexpr auto brown_2   = color(0x7A4841FF);
constexpr auto brown_3   = color(0x935F4CFF);
constexpr auto brown_4   = color(0xAD7757FF);
constexpr auto brown_5   = color(0xB78565FF);
constexpr auto brown_6   = color(0xC09473FF);
constexpr auto brown_7   = color(0xCCA483FF);
constexpr auto brown_8   = color(0xD7B594FF);
constexpr auto brown_9   = color(0xDFC5A3FF);
constexpr auto brown_10  = color(0xE7D5B3FF);

constexpr auto amber_0   = color(0x341C27FF);
constexpr auto amber_1   = color(0x4A242AFF);
constexpr auto amber_2   = color(0x602C2CFF);
constexpr auto amber_3   = color(0x743B2DFF);
constexpr auto amber_4   = color(0x884B2BFF);
constexpr auto amber_5   = color(0xA3612CFF);
constexpr auto amber_6   = color(0xBE772BFF);
constexpr auto amber_7   = color(0xCE8A36FF);
constexpr auto amber_8   = color(0xDE9E41FF);
constexpr auto amber_9   = color(0xE3B05AFF);
constexpr auto amber_10  = color(0xE8C170FF);

constexpr auto red_0     = color(0x241527FF);
constexpr auto red_1     = color(0x32192CFF);
constexpr auto red_2     = color(0x411D31FF);
constexpr auto red_3     = color(0x5B2135FF);
constexpr auto red_4     = color(0x752438FF);
constexpr auto red_5     = color(0x8D2A35FF);
constexpr auto red_6     = color(0xA53030FF);
constexpr auto red_7     = color(0xBA4436FF);
constexpr auto red_8     = color(0xCF573CFF);
constexpr auto red_9     = color(0xD56F3DFF);
constexpr auto red_10    = color(0xDA863EFF);

constexpr auto purple_0  = color(0x1E1D39FF);
constexpr auto purple_1  = color(0x2F2245FF);
constexpr auto purple_2  = color(0x402751FF);
constexpr auto purple_3  = color(0x5C2F66FF);
constexpr auto purple_4  = color(0x7A367BFF);
constexpr auto purple_5  = color(0x8E3A83FF);
constexpr auto purple_6  = color(0xA23E8CFF);
constexpr auto purple_7  = color(0xB44892FF);
constexpr auto purple_8  = color(0xC65197FF);
constexpr auto purple_9  = color(0xD36B9EFF);
constexpr auto purple_10 = color(0xDF84A5FF);

constexpr auto gray_0    = color(0x090A14FF);
constexpr auto gray_1    = color(0x0C0F19FF);
constexpr auto gray_2    = color(0x10141FFF);
constexpr auto gray_3    = color(0x121823FF);
constexpr auto gray_4    = color(0x151D28FF);
constexpr auto gray_5    = color(0x1A252FFF);
constexpr auto gray_6    = color(0x202E37FF);
constexpr auto gray_7    = color(0x2C3C43FF);
constexpr auto gray_8    = color(0x394A50FF);
constexpr auto gray_9    = color(0x485E63FF);
constexpr auto gray_10   = color(0x577277FF);
constexpr auto gray_11   = color(0x6C8486FF);
constexpr auto gray_12   = color(0x819796FF);
constexpr auto gray_13   = color(0x94A6A4FF);
constexpr auto gray_14   = color(0xA8B5B2FF);
constexpr auto gray_15   = color(0xB7C2BFFF);
constexpr auto gray_16   = color(0xC7CFCCFF);
constexpr auto gray_17   = color(0xD9DEDAFF);
constexpr auto gray_18   = color(0xEBEDE9FF);

constexpr auto white    = color(0xFFFFFFFF);

constexpr auto black    = color(0x000000FF);

constexpr std::array all = {
    blue_0, blue_1, blue_2, blue_3, blue_4, blue_5, blue_6, blue_7, blue_8, blue_9, blue_10,
    green_0, green_1, green_2, green_3, green_4, green_5, green_6, green_7, green_8, green_9, green_10,
    brown_0, brown_1, brown_2, brown_3, brown_4, brown_5, brown_6, brown_7, brown_8, brown_9, brown_10,
    amber_0, amber_1, amber_2, amber_3, amber_4, amber_5, amber_6, amber_7, amber_8, amber_9, amber_10,
    red_0, red_1, red_2, red_3, red_4, red_5, red_6, red_7, red_8, red_9, red_10,
    purple_0, purple_1, purple_2, purple_3, purple_4, purple_5, purple_6, purple_7, purple_8, purple_9, purple_10,
    gray_0, gray_1, gray_2, gray_3, gray_4, gray_5, gray_6, gray_7, gray_8, gray_9, gray_10,
    gray_11, gray_12, gray_13, gray_14, gray_15, gray_16, gray_17, gray_18,
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
