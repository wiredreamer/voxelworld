export module vw.asset:model.cover;

import std;

import vw.core;

export namespace vw::asset {

// см. docs/world.md#покров
class cover_layer {
public:
    struct entry {
        vec3i support;
        uint8 form = 0;
    };

    cover_layer();
    explicit cover_layer(std::span<const entry> entries);

    [[nodiscard]] auto form_at(vec3i support) const -> uint8;
    auto set(vec3i support, uint8 form) -> bool;

    [[nodiscard]] auto size() const -> uint32 {
        return static_cast<uint32>(packed_.size());
    }

    [[nodiscard]] auto empty() const -> bool {
        return packed_.empty();
    }

    [[nodiscard]] auto revision() const -> uint64 {
        return revision_;
    }

    template <typename F>
    auto for_each(F&& f) const -> void {
        for (const uint32 packed : packed_) {
            f(entry{.support = support_of_(packed), .form = static_cast<uint8>(packed & 0xFFU)});
        }
    }

private:
    [[nodiscard]] static auto key_of_(vec3i support) -> uint32;
    [[nodiscard]] static auto support_of_(uint32 packed) -> vec3i;

    std::vector<uint32> packed_;
    uint64 revision_ = 0;
};

}  // namespace vw::asset
