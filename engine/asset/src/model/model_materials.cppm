export module vw.asset:model.materials;

import std;

import vw.core;
import :model.identity;

export namespace vw::asset {

// см. docs/ENGINE.md#слой-материала
template <typename Value>
    requires(sizeof(Value) == 1 && std::is_trivially_copyable_v<Value>)
class voxel_layer {
public:
    using page_cells = std::array<Value, voxel_page_volume>;

    explicit voxel_layer(std::size_t page_count = 0) : page_count_{page_count} {}

    [[nodiscard]] auto blank() const -> bool {
        return entries_.empty();
    }

    [[nodiscard]] auto get(std::size_t page, int32 cell) const -> Value {
        if (entries_.empty()) {
            return Value{};
        }
        const uint32 entry = entries_[page];
        if ((entry & dense_flag) == 0) {
            return unpacked_(entry);
        }
        return dense_[entry & ~dense_flag][static_cast<std::size_t>(cell)];
    }

    [[nodiscard]] auto whole_page(std::size_t page) const -> std::optional<Value> {
        if (entries_.empty()) {
            return Value{};
        }
        const uint32 entry = entries_[page];
        if ((entry & dense_flag) != 0) {
            return std::nullopt;
        }
        return unpacked_(entry);
    }

    [[nodiscard]] auto cells_of(std::size_t page) const -> const page_cells& {
        return dense_[entries_[page] & ~dense_flag];
    }

    auto set(std::size_t page, int32 cell, Value value) -> void {
        if (entries_.empty()) {
            if (value == Value{}) {
                return;
            }
            entries_.assign(page_count_, 0);
        }

        uint32& entry = entries_[page];
        if ((entry & dense_flag) == 0) {
            const Value held = unpacked_(entry);
            if (held == value) {
                return;
            }
            const uint32 slot = take_slot_();
            dense_[slot].fill(held);
            entry = dense_flag | slot;
        }
        dense_[entry & ~dense_flag][static_cast<std::size_t>(cell)] = value;
    }

    auto fill_page(std::size_t page, Value value) -> void {
        if (entries_.empty()) {
            if (value == Value{}) {
                return;
            }
            entries_.assign(page_count_, 0);
        }

        uint32& entry = entries_[page];
        if ((entry & dense_flag) != 0) {
            free_.push_back(entry & ~dense_flag);
        }
        entry = packed_(value);
    }

    auto fill(Value value) -> void {
        entries_.clear();
        dense_.clear();
        free_.clear();
        if (value != Value{}) {
            entries_.assign(page_count_, packed_(value));
        }
    }

    auto fold() -> void {
        bool any = false;
        for (uint32& entry : entries_) {
            if ((entry & dense_flag) != 0) {
                const uint32 slot       = entry & ~dense_flag;
                const page_cells& cells = dense_[slot];
                if (std::ranges::all_of(cells, [&](Value held) { return held == cells.front(); })) {
                    entry = packed_(cells.front());
                    free_.push_back(slot);
                }
            }
            any = any || entry != 0;
        }
        if (!any) {
            fill(Value{});
        }
    }

private:
    static constexpr uint32 dense_flag = 1U << 31U;

    [[nodiscard]] static auto packed_(Value value) -> uint32 {
        return std::bit_cast<uint8>(value);
    }

    [[nodiscard]] static auto unpacked_(uint32 entry) -> Value {
        return std::bit_cast<Value>(static_cast<uint8>(entry));
    }

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

using material_layer = voxel_layer<material>;

// см. docs/ENGINE.md#слой-состояния
using state_layer = voxel_layer<voxel_state>;

}  // namespace vw::asset
