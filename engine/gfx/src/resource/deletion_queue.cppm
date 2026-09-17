export module vw.gfx:resource.deletion_queue;

import std;

import vw.core;
import :resource.buffer;
import vulkan;

namespace vw::gfx {

class deletion_queue final {
public:
    deletion_queue()  = default;
    ~deletion_queue() = default;

    deletion_queue(const deletion_queue&)                    = delete;
    auto operator=(const deletion_queue&) -> deletion_queue& = delete;
    deletion_queue(deletion_queue&&)                         = delete;
    auto operator=(deletion_queue&&) -> deletion_queue&      = delete;

    auto set_frame(uint64 frame) -> void;
    auto retire(std::unique_ptr<buffer> victim) -> void;
    auto collect(uint64 completed_frame) -> void;

    [[nodiscard]] auto pending_count() const -> uint32;

private:
    struct entry {
        uint64 frame;
        std::unique_ptr<buffer> victim;
    };

    std::vector<entry> entries_;
    uint64 current_frame_ = 0;
};

}  // namespace vw::gfx
