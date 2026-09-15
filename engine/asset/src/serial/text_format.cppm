module vw.asset:serial.text;

import std;

import vw.core;

// Общее у текстовых форматов семейства: строка — это «тег [аргумент]», а
// глубину вложенности задаёт отступ табуляциями. Что значит тег и сколько чисел
// в его аргументе, знает формат; здесь — разбор строки на части и словари,
// которые иначе разошлись бы по разборщикам копиями.
//
// Партиция внутренняя: наружу это не интерфейс, а договор между разборщиками.
namespace vw::asset::detail {

inline auto trim(std::string_view text) -> std::string_view {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }

    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

struct text_line {
    std::size_t depth = 0;
    std::string_view name;
    std::string_view value;
};

// Пустая строка ответа не имеет: тега в ней нет. Строка шапки приходит тегом с
// именем «#», и её значение — вся строка целиком: версию из неё достаёт
// read_header_version, которому нужен весь текст.
inline auto split_line(std::string_view line) -> std::optional<text_line> {
    std::size_t depth = 0;
    while (depth < line.size() && line[depth] == '\t') {
        ++depth;
    }

    const auto body = trim(line.substr(depth));
    if (body.empty()) {
        return std::nullopt;
    }

    if (body.front() == '#') {
        return text_line{.depth = depth, .name = body.substr(0, 1), .value = body};
    }

    const auto space = body.find_first_of(" \t");
    if (space == std::string_view::npos) {
        return text_line{.depth = depth, .name = body};
    }

    return text_line{
        .depth = depth,
        .name  = body.substr(0, space),
        .value = trim(body.substr(space + 1)),
    };
}

inline auto interp_to_text(math::interpolation_type interp) -> std::string_view {
    switch (interp) {
        case math::interpolation_type::linear: return "linear";
        case math::interpolation_type::step: return "step";
        case math::interpolation_type::ease_in: return "ease_in";
        case math::interpolation_type::ease_out: return "ease_out";
        case math::interpolation_type::ease_in_out: return "ease_in_out";
        case math::interpolation_type::cubic_bezier: return "cubic_bezier";
    }

    return "linear";
}

// Неизвестное слово читается как linear: кривая — это про ощущение перехода, и
// ломать из-за неё загрузку не за что.
inline auto interp_from_text(std::string_view text) -> math::interpolation_type {
    if (text == "step") {
        return math::interpolation_type::step;
    }
    if (text == "ease_in") {
        return math::interpolation_type::ease_in;
    }
    if (text == "ease_out") {
        return math::interpolation_type::ease_out;
    }
    if (text == "ease_in_out") {
        return math::interpolation_type::ease_in_out;
    }
    if (text == "cubic_bezier") {
        return math::interpolation_type::cubic_bezier;
    }

    return math::interpolation_type::linear;
}

}  // namespace vw::asset::detail
