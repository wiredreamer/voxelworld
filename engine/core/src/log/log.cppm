module;

// Global module fragment уцелел здесь по одной причине: опция сборки доходит до
// кода макросом и обязана быть раскрыта до того, как её назовёт purview.
// Стандартных заголовков тут нет — они приходят через `import std`.
#ifndef VW_LOG_MIN_LEVEL
#define VW_LOG_MIN_LEVEL trace
#endif

export module vw.core:log;

import std;
import :types;

export namespace vw::log {

enum class level : uint8 {
    trace,
    debug,
    info,
    warn,
    error,
    critical,
    off,
};

inline constexpr auto min_level = level::VW_LOG_MIN_LEVEL;

auto set_level(level lvl) -> void;
[[nodiscard]] auto get_level() -> level;
auto add_file_sink(std::string_view path) -> void;
auto write(level lvl, std::string_view category, std::string_view message) -> void;

// Категория — отдельный тип, чтобы log::info(cat, fmt, ...) нельзя было спутать
// с log::info(fmt, ...), когда первый аргумент строка.
struct log_category {
    std::string_view value;

    constexpr explicit log_category(std::string_view cat) : value(cat) {}
    constexpr explicit log_category(const char* cat) : value(cat) {}
};

}  // namespace vw::log

namespace vw::log::detail {

template <level Lvl, typename... Args>
auto emit(std::string_view category, std::format_string<Args...> fmt, Args&&... args) -> void {
    if constexpr (Lvl >= min_level) {
        if (Lvl >= get_level()) {
            write(Lvl, category, std::format(fmt, std::forward<Args>(args)...));
        }
    }
}

}  // namespace vw::log::detail

export namespace vw::log {

template <typename... Args>
auto trace(std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::trace>({}, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto trace(log_category cat, std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::trace>(cat.value, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto debug(std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::debug>({}, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto debug(log_category cat, std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::debug>(cat.value, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto info(std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::info>({}, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto info(log_category cat, std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::info>(cat.value, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto warn(std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::warn>({}, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto warn(log_category cat, std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::warn>(cat.value, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto error(std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::error>({}, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto error(log_category cat, std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::error>(cat.value, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto critical(std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::critical>({}, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
auto critical(log_category cat, std::format_string<Args...> fmt, Args&&... args) -> void {
    detail::emit<level::critical>(cat.value, fmt, std::forward<Args>(args)...);
}

}  // namespace vw::log
