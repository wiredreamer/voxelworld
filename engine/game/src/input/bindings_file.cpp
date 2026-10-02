module vw.game;

import std;
import vw.core;

namespace vw::game {
namespace {

constexpr std::string_view actions_key = "actions";
constexpr std::string_view move_key    = "move";

auto failure(const json::cursor& at, std::string_view message) -> std::unexpected<std::string> {
    return std::unexpected{std::format("{}: {}", at.path(), message)};
}

template <typename Add>
auto read_inputs(const json::cursor& list, Add&& add) -> std::expected<void, std::string> {
    const auto inputs = list.elements();
    if (!inputs) {
        return std::unexpected{json::describe(inputs.error())};
    }

    for (const auto& input : *inputs) {
        const auto name = input.string();
        if (!name) {
            return std::unexpected{json::describe(name.error())};
        }
        if (!add(*name)) {
            return failure(input, std::format("'{}' is neither a key nor a mouse button", *name));
        }
    }
    return {};
}

auto read_actions(const json::cursor& section, input_bindings& out)
    -> std::expected<void, std::string> {
    const auto fields = section.fields();
    if (!fields) {
        return std::unexpected{json::describe(fields.error())};
    }

    for (const auto& [name, list] : *fields) {
        const auto action = input_action_from_name(name);
        if (!action) {
            return failure(list, std::format("'{}' is not an action", name));
        }

        const auto read = read_inputs(list, [&](std::string_view input) {
            if (const auto key = keyboard::key_from_name(input)) {
                out.key_actions.push_back({*key, *action});
                return true;
            }
            if (const auto button = mouse::button_from_name(input)) {
                out.button_actions.push_back({*button, *action});
                return true;
            }
            return false;
        });
        if (!read) {
            return read;
        }
    }
    return {};
}

auto read_moves(const json::cursor& section, input_bindings& out)
    -> std::expected<void, std::string> {
    const auto fields = section.fields();
    if (!fields) {
        return std::unexpected{json::describe(fields.error())};
    }

    for (const auto& [name, list] : *fields) {
        const auto direction = move_direction_from_name(name);
        if (!direction) {
            return failure(list, std::format("'{}' is not a direction", name));
        }

        const auto read = read_inputs(list, [&](std::string_view input) {
            if (const auto key = keyboard::key_from_name(input)) {
                out.key_moves.push_back({*key, *direction});
                return true;
            }
            return false;
        });
        if (!read) {
            return read;
        }
    }
    return {};
}

}  // namespace

auto parse_input_bindings(std::string_view text) -> std::expected<input_bindings, std::string> {
    const auto document = json::parse(text);
    if (!document) {
        return std::unexpected{json::describe(document.error())};
    }

    const json::cursor root{*document};

    const auto sections = root.fields();
    if (!sections) {
        return std::unexpected{json::describe(sections.error())};
    }

    input_bindings out;

    for (const auto& [name, section] : *sections) {
        std::expected<void, std::string> read;

        if (name == actions_key) {
            read = read_actions(section, out);
        } else if (name == move_key) {
            read = read_moves(section, out);
        } else {
            return failure(section, "unknown section, expected 'actions' or 'move'");
        }

        if (!read) {
            return std::unexpected{read.error()};
        }
    }

    return out;
}

auto load_input_bindings(const std::filesystem::path& file)
    -> std::expected<input_bindings, std::string> {
    std::ifstream stream{file, std::ios::binary};
    if (!stream) {
        return std::unexpected{std::format("cannot open {}", file.generic_string())};
    }

    const std::string text{std::istreambuf_iterator<char>{stream}, {}};

    auto bindings = parse_input_bindings(text);
    if (!bindings) {
        return std::unexpected{std::format("{}: {}", file.generic_string(), bindings.error())};
    }
    return bindings;
}

auto dump_input_bindings(const input_bindings& bindings) -> std::string {
    json::object actions;
    for (std::size_t i = 0; i < input_action_count; ++i) {
        const auto action = static_cast<input_action>(i);

        json::array inputs;
        for (const auto& binding : bindings.key_actions) {
            if (binding.action == action) {
                inputs.emplace_back(keyboard::key_name(binding.key));
            }
        }
        for (const auto& binding : bindings.button_actions) {
            if (binding.action == action) {
                inputs.emplace_back(mouse::button_name(binding.button));
            }
        }
        actions.set(std::string{input_action_name(action)}, std::move(inputs));
    }

    json::object moves;
    for (std::size_t i = 0; i < move_direction_count; ++i) {
        const auto direction = static_cast<move_direction>(i);

        json::array inputs;
        for (const auto& binding : bindings.key_moves) {
            if (binding.direction == direction) {
                inputs.emplace_back(keyboard::key_name(binding.key));
            }
        }
        moves.set(std::string{move_direction_name(direction)}, std::move(inputs));
    }

    json::object root;
    root.set(std::string{actions_key}, std::move(actions));
    root.set(std::string{move_key}, std::move(moves));

    return json::dump(json::value{std::move(root)}, {.indent = 2});
}

}  // namespace vw::game
