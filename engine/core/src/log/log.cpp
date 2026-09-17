module;

#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include <version>

module vw.core;

import std;

namespace vw::log {
namespace {

std::atomic<level> current_level{level::trace};

auto sink_mutex() -> std::mutex& {
    static std::mutex instance;
    return instance;
}

auto file_sink() -> std::ofstream& {
    static std::ofstream instance;
    return instance;
}

auto enable_color() -> bool {
#ifdef _WIN32
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) == 0) {
        return false;
    }
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

auto color_enabled() -> bool {
    static const bool enabled = enable_color();
    return enabled;
}

auto level_name(level lvl) -> std::string_view {
    switch (lvl) {
        case level::trace:    return "trace";
        case level::debug:    return "debug";
        case level::info:     return "info";
        case level::warn:     return "warning";
        case level::error:    return "error";
        case level::critical: return "critical";
        case level::off:      return "off";
    }
    return "unknown";
}

auto level_color(level lvl) -> std::string_view {
    switch (lvl) {
        case level::trace:    return "\x1b[37m";
        case level::debug:    return "\x1b[36m";
        case level::info:     return "\x1b[32m";
        case level::warn:     return "\x1b[33m\x1b[1m";
        case level::error:    return "\x1b[31m\x1b[1m";
        case level::critical: return "\x1b[1m\x1b[41m";
        default:              return "";
    }
}

auto timestamp() -> std::string {
    const auto now = std::chrono::system_clock::now();

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    const auto local = std::chrono::current_zone()->to_local(now);
    return std::format("{:%Y-%m-%d %H:%M:%S}",
                       std::chrono::floor<std::chrono::milliseconds>(local));
#else
    return std::format("{:%Y-%m-%d %H:%M:%S}",
                       std::chrono::floor<std::chrono::milliseconds>(now));
#endif
}

}  // namespace

auto set_level(level lvl) -> void {
    current_level.store(lvl, std::memory_order_relaxed);
}

auto get_level() -> level {
    return current_level.load(std::memory_order_relaxed);
}

auto add_file_sink(std::string_view path) -> void {
    const std::lock_guard lock{sink_mutex()};
    auto& file = file_sink();
    if (file.is_open()) {
        file.close();
    }
    file.clear();
    file.open(std::string{path}, std::ios::out | std::ios::trunc);
}

auto write(level lvl, std::string_view category, std::string_view message) -> void {
    const auto stamp = timestamp();
    const auto body = category.empty()
                          ? std::string{message}
                          : std::format("[{}] {}", category, message);

    const std::lock_guard lock{sink_mutex()};

    if (color_enabled()) {
        std::println("[{}] [{}{}\x1b[m] {}", stamp, level_color(lvl), level_name(lvl), body);
    } else {
        std::println("[{}] [{}] {}", stamp, level_name(lvl), body);
    }

    auto& file = file_sink();
    if (file.is_open()) {
        file << std::format("[{}] [{}] {}\n", stamp, level_name(lvl), body) << std::flush;
    }
}

}  // namespace vw::log
