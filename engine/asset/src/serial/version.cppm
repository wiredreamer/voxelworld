module vw.asset:serial.version;

import std;

import vw.core;

// Разбор шапки, общий всем текстовым форматам ассетов. Партиция внутренняя:
// наружу это не интерфейс, а договор между разборщиками.
namespace vw::asset::detail {

// Версия лежит в шапке-комментарии — «# Vox File Version 2.0». Её отсутствие
// ошибкой не считается: без версии писались и файлы первых дней, и тексты в
// тестах, и буфер из фаззера.
inline auto read_header_version(std::istringstream& iss) -> std::optional<std::string> {
    std::string token;
    while (iss >> token) {
        if (token != "Version") {
            continue;
        }

        std::string version;
        if (!(iss >> version)) {
            return std::nullopt;
        }
        return version;
    }
    return std::nullopt;
}

// Сравниваются старшие номера: младший поднимают при добавлении команд, а
// неизвестную команду разборщик и так переживает.
inline auto major_version_differs(std::string_view version, std::string_view expected) -> bool {
    const auto major_of = [](std::string_view text) -> std::string_view {
        return text.substr(0, text.find('.'));
    };
    return major_of(version) != major_of(expected);
}

}  // namespace vw::asset::detail
