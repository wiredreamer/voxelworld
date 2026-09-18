#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <intrin.h>
#include <new>
#include <thread>

namespace {

constexpr int max_threads = 64;

struct alignas(128) slot {
    std::uint64_t small_ticks = 0;
    std::uint64_t small_count = 0;
    std::uint64_t large_ticks = 0;
    std::uint64_t large_count = 0;
    std::uint64_t free_ticks  = 0;
    std::uint64_t free_count  = 0;
    std::uint64_t bytes       = 0;
    char pad[128 - (7 * sizeof(std::uint64_t)) % 128]{};
};

slot slots[max_threads];
std::atomic<int> next_slot{0};

auto my_slot() -> slot& {
    static thread_local const int index = next_slot.fetch_add(1) % max_threads;
    return slots[index];
}

const std::uint64_t start_tsc = __rdtsc();
const std::chrono::steady_clock::time_point start_wall = std::chrono::steady_clock::now();

struct dumper {
    ~dumper() {
        const auto wall =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start_wall).count();
        const double hz = static_cast<double>(__rdtsc() - start_tsc) / wall;

        std::uint64_t st = 0;
        std::uint64_t sc = 0;
        std::uint64_t lt = 0;
        std::uint64_t lc = 0;
        std::uint64_t ft = 0;
        std::uint64_t fc = 0;
        std::uint64_t by = 0;
        int threads = 0;
        for (const slot& s : slots) {
            if (s.small_count == 0 && s.large_count == 0 && s.free_count == 0) { continue; }
            ++threads;
            st += s.small_ticks;
            sc += s.small_count;
            lt += s.large_ticks;
            lc += s.large_count;
            ft += s.free_ticks;
            fc += s.free_count;
            by += s.bytes;
        }

        std::FILE* f = std::fopen("alloc_probe.txt", "w");
        if (f == nullptr) { return; }
        const auto ns = [hz](std::uint64_t ticks, std::uint64_t n) {
            return n == 0 ? 0.0 : static_cast<double>(ticks) / hz * 1e9 / static_cast<double>(n);
        };
        const auto ms = [hz](std::uint64_t ticks) {
            return static_cast<double>(ticks) / hz * 1000.0;
        };
        std::fprintf(f, "wall %.3f s, tsc %.3f GHz, threads %d\n", wall, hz / 1e9, threads);
        std::fprintf(f, "small n=%llu ms=%.3f avg_ns=%.1f\n",
                     static_cast<unsigned long long>(sc), ms(st), ns(st, sc));
        std::fprintf(f, "large n=%llu ms=%.3f avg_ns=%.1f\n",
                     static_cast<unsigned long long>(lc), ms(lt), ns(lt, lc));
        std::fprintf(f, "free  n=%llu ms=%.3f avg_ns=%.1f\n",
                     static_cast<unsigned long long>(fc), ms(ft), ns(ft, fc));
        std::fprintf(f, "bytes %llu\n", static_cast<unsigned long long>(by));
        std::fprintf(f, "slot0 small=%llu large=%llu free=%llu\n",
                     static_cast<unsigned long long>(slots[0].small_count),
                     static_cast<unsigned long long>(slots[0].large_count),
                     static_cast<unsigned long long>(slots[0].free_count));
        std::fclose(f);
    }
};

dumper the_dumper;

}  // namespace

auto operator new(std::size_t size) -> void* {
    slot& s = my_slot();
    const std::uint64_t t0 = __rdtsc();
    void* p = std::malloc(size == 0 ? 1 : size);
    const std::uint64_t spent = __rdtsc() - t0;

    s.bytes += size;
    if (size > 65536) {
        s.large_ticks += spent;
        ++s.large_count;
    } else {
        s.small_ticks += spent;
        ++s.small_count;
    }

    if (p == nullptr) { throw std::bad_alloc{}; }
    return p;
}

auto operator new[](std::size_t size) -> void* {
    return ::operator new(size);
}

auto operator new(std::size_t size, const std::nothrow_t&) noexcept -> void* {
    return std::malloc(size == 0 ? 1 : size);
}

auto operator new[](std::size_t size, const std::nothrow_t& tag) noexcept -> void* {
    return ::operator new(size, tag);
}

auto operator delete(void* p) noexcept -> void {
    if (p == nullptr) { return; }
    slot& s = my_slot();
    const std::uint64_t t0 = __rdtsc();
    std::free(p);
    s.free_ticks += __rdtsc() - t0;
    ++s.free_count;
}

auto operator delete(void* p, std::size_t) noexcept -> void {
    ::operator delete(p);
}

auto operator delete[](void* p) noexcept -> void {
    ::operator delete(p);
}

auto operator delete[](void* p, std::size_t) noexcept -> void {
    ::operator delete(p);
}

auto operator delete(void* p, const std::nothrow_t&) noexcept -> void {
    ::operator delete(p);
}

auto operator delete[](void* p, const std::nothrow_t&) noexcept -> void {
    ::operator delete(p);
}

auto operator new(std::size_t size, std::align_val_t align) -> void* {
    void* p = _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(align));
    if (p == nullptr) { throw std::bad_alloc{}; }
    return p;
}

auto operator new[](std::size_t size, std::align_val_t align) -> void* {
    return ::operator new(size, align);
}

auto operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept
    -> void* {
    return _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(align));
}

auto operator delete(void* p, std::align_val_t) noexcept -> void {
    _aligned_free(p);
}

auto operator delete(void* p, std::size_t, std::align_val_t align) noexcept -> void {
    ::operator delete(p, align);
}

auto operator delete[](void* p, std::align_val_t align) noexcept -> void {
    ::operator delete(p, align);
}

auto operator delete[](void* p, std::size_t, std::align_val_t align) noexcept -> void {
    ::operator delete(p, align);
}

auto operator delete(void* p, std::align_val_t align, const std::nothrow_t&) noexcept -> void {
    ::operator delete(p, align);
}
