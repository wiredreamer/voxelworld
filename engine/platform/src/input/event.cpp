module vw.platform;

import std;
import vw.core;

namespace vw::plat::detail {

auto next_event_id() -> uint32 {
    static std::atomic<uint32> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace vw::plat::detail
