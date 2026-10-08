export module vw.asset:model.materials;

import std;

import vw.core;
import :model.identity;

export namespace vw::asset {

// см. docs/ENGINE.md#слой-материала
class material_layer {
public:
    using page_cells = std::array<material, voxel_page_volume>;

    explicit material_layer(std::size_t page_count = 0) : page_count_{page_count} {}

    [[nodiscard]] auto all_inert() const -> bool {
        return entries_.empty();
    }

    [[nodiscard]] auto get(std::size_t page, int32 cell) const -> material {
        if (entries_.empty()) {
            return material{};
        }
        const uint32 entry = entries_[page];
        if ((entry & dense_flag) == 0) {
            return material{static_cast<uint8>(entry)};
        }
        return dense_[entry & ~dense_flag][static_cast<std::size_t>(cell)];
    }

    [[nodiscard]] auto whole_page(std::size_t page) const -> std::optional<material> {
        if (entries_.empty()) {
            return material{};
        }
        const uint32 entry = entries_[page];
        if ((entry & dense_flag) != 0) {
            return std::nullopt;
        }
        return material{static_cast<uint8>(entry)};
    }

    [[nodiscard]] auto cells_of(std::size_t page) const -> const page_cells& {
        return dense_[entries_[page] & ~dense_flag];
    }

    auto set(std::size_t page, int32 cell, material value) -> void {
        if (entries_.empty()) {
            if (value == material{}) {
                return;
            }
            entries_.assign(page_count_, 0);
        }

        uint32& entry = entries_[page];
        if ((entry & dense_flag) == 0) {
            const material held{static_cast<uint8>(entry)};
            if (held == value) {
                return;
            }
            const uint32 slot = take_slot_();
            dense_[slot].fill(held);
            entry = dense_flag | slot;
        }
        dense_[entry & ~dense_flag][static_cast<std::size_t>(cell)] = value;
    }

    auto fill_page(std::size_t page, material value) -> void {
        if (entries_.empty()) {
            if (value == material{}) {
                return;
            }
            entries_.assign(page_count_, 0);
        }

        uint32& entry = entries_[page];
        if ((entry & dense_flag) != 0) {
            free_.push_back(entry & ~dense_flag);
        }
        entry = value.value;
    }

    auto fill(material value) -> void {
        entries_.clear();
        dense_.clear();
        free_.clear();
        if (value != material{}) {
            entries_.assign(page_count_, value.value);
        }
    }

    auto fold() -> void {
        bool any = false;
        for (uint32& entry : entries_) {
            if ((entry & dense_flag) != 0) {
                const uint32 slot       = entry & ~dense_flag;
                const page_cells& cells = dense_[slot];
                if (std::ranges::all_of(cells, [&](material m) { return m == cells.front(); })) {
                    entry = cells.front().value;
                    free_.push_back(slot);
                }
            }
            any = any || entry != 0;
        }
        if (!any) {
            fill(material{});
        }
    }

private:
    static constexpr uint32 dense_flag = 1U << 31U;

    [[nodiscard]] auto take_slot_() -> uint32 {
        if (!free_.empty()) {
            const uint32 slot = free_.back();
            free_.pop_back();
            return slot;
        }
        dense_.emplace_back();
        return static_cast<uint32>(dense_.size() - 1);
    }

    std::size_t page_count_ = 0;
    std::vector<uint32> entries_;
    std::vector<page_cells> dense_;
    std::vector<uint32> free_;
};

}  // namespace vw::asset
