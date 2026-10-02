export module vw.game:input.bindings_file;

import std;

import vw.core;
import :input.mapper;

// см. docs/ENGINE.md#файл-раскладки
export namespace vw::game {

[[nodiscard]] auto parse_input_bindings(std::string_view text)
    -> std::expected<input_bindings, std::string>;

[[nodiscard]] auto load_input_bindings(const std::filesystem::path& file)
    -> std::expected<input_bindings, std::string>;

[[nodiscard]] auto dump_input_bindings(const input_bindings& bindings) -> std::string;

}  // namespace vw::game
