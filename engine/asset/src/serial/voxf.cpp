module vw.asset;

import std;
import vw.core;

namespace vw::asset {

namespace detail {
constexpr log::log_category voxf_lc{"voxf"};

auto parse_number(std::string_view text, float32& out) -> bool {
    // true и false — это те же ноль и единица, но в файле про grounded читается
    // именно так, и писатель возвращает их обратно словами.
    if (text == "true") {
        out = 1.0F;
        return true;
    }
    if (text == "false") {
        out = 0.0F;
        return true;
    }

    std::istringstream iss{std::string{text}};
    iss >> out;
    return !iss.fail();
}

auto parse_compare(std::string_view text) -> std::optional<fsm_compare> {
    if (text == "==") {
        return fsm_compare::equal;
    }
    if (text == "!=") {
        return fsm_compare::not_equal;
    }
    if (text == "<") {
        return fsm_compare::less;
    }
    if (text == "<=") {
        return fsm_compare::less_equal;
    }
    if (text == ">") {
        return fsm_compare::greater;
    }
    if (text == ">=") {
        return fsm_compare::greater_equal;
    }

    return std::nullopt;
}

auto compare_to_text(fsm_compare compare) -> std::string_view {
    switch (compare) {
        case fsm_compare::equal: return "==";
        case fsm_compare::not_equal: return "!=";
        case fsm_compare::less: return "<";
        case fsm_compare::less_equal: return "<=";
        case fsm_compare::greater: return ">";
        case fsm_compare::greater_equal: return ">=";
    }

    return "==";
}

auto parse_loop_mode(std::string_view text) -> std::optional<animation_loop_mode> {
    if (text == "once") {
        return animation_loop_mode::once;
    }
    if (text == "loop") {
        return animation_loop_mode::loop;
    }
    if (text == "ping_pong") {
        return animation_loop_mode::ping_pong;
    }

    return std::nullopt;
}

auto loop_mode_to_text(animation_loop_mode mode) -> std::string_view {
    switch (mode) {
        case animation_loop_mode::once: return "once";
        case animation_loop_mode::loop: return "loop";
        case animation_loop_mode::ping_pong: return "ping_pong";
    }

    return "loop";
}

auto parse_param_type(std::string_view text) -> std::optional<voxf_param_type> {
    if (text == "float") {
        return voxf_param_type::real;
    }
    if (text == "int") {
        return voxf_param_type::integer;
    }
    if (text == "bool") {
        return voxf_param_type::boolean;
    }
    if (text == "trigger") {
        return voxf_param_type::trigger;
    }

    return std::nullopt;
}

auto param_type_to_text(voxf_param_type type) -> std::string_view {
    switch (type) {
        case voxf_param_type::real: return "float";
        case voxf_param_type::integer: return "int";
        case voxf_param_type::boolean: return "bool";
        case voxf_param_type::trigger: return "trigger";
    }

    return "float";
}

auto split_words(std::string_view text) -> std::vector<std::string_view> {
    std::vector<std::string_view> words;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto start = text.find_first_not_of(" \t", pos);
        if (start == std::string_view::npos) {
            break;
        }

        const auto end = text.find_first_of(" \t", start);
        words.push_back(text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start
        ));
        pos = end == std::string_view::npos ? text.size() : end;
    }

    return words;
}

// Кривая и касательные — хвост, который дописывается по надобности: у линейного
// перехода в файле стоит одна длительность, и читать её глазами приятнее.
auto parse_transition(std::span<const std::string_view> words, transition& out) -> bool {
    if (words.empty() || !parse_number(words[0], out.duration)) {
        return false;
    }

    if (words.size() > 1) {
        out.interp = interp_from_text(words[1]);
    }

    if (words.size() > 3) {
        return parse_number(words[2], out.tangent_in) && parse_number(words[3], out.tangent_out);
    }

    return true;
}

auto transition_to_text(const transition& value) -> std::string {
    auto text = std::format("{:.6g}", value.duration);
    if (value.interp == math::interpolation_type::linear) {
        return text;
    }

    text += std::format(" {}", interp_to_text(value.interp));
    if (value.interp == math::interpolation_type::cubic_bezier) {
        text += std::format(" {:.6g} {:.6g}", value.tangent_in, value.tangent_out);
    }

    return text;
}

}  // namespace detail

auto build_fsm(
    const voxf_data& data, const voxf_clip_resolver& resolve
) -> animation_fsm {
    const auto known_target = [&data](const animation_fsm::transition_rule& rule) -> bool {
        return std::ranges::any_of(data.states, [&rule](const voxf_state& state) {
            return state.name == rule.target_state;
        });
    };

    animation_fsm fsm;

    for (const auto& state : data.states) {
        for (const auto& rule : state.transitions) {
            if (!known_target(rule)) {
                log::warn(
                    detail::voxf_lc, "state {} transitions to unknown state {}", state.name,
                    rule.target_state
                );
            }
        }

        std::shared_ptr<animation_clip> clip;
        if (!state.clip.empty() && resolve) {
            clip = resolve(state.clip);
        }

        fsm.add_state({
            .name            = state.name,
            .clip            = std::move(clip),
            .loop_mode       = state.loop_mode,
            .playback_speed  = state.rate,
            .layer_blend_in  = state.fade_in,
            .layer_blend_out = state.fade_out,
            .transitions     = state.transitions,
        });
    }

    for (const auto& rule : data.any_transitions) {
        if (!known_target(rule)) {
            log::warn(detail::voxf_lc, "transition from any state to unknown state {}",
                      rule.target_state);
        }
        fsm.add_any_transition(rule);
    }

    fsm.set_entry_state(data.entry_state);

    return fsm;
}

auto apply_defaults(
    const voxf_data& data, fsm_blackboard& board
) -> void {
    for (const auto& param : data.params) {
        // Триггер живёт не на доске, а в наборе сработавших: значения у него нет.
        if (param.type != voxf_param_type::trigger) {
            board.set(param.name, param.value);
        }
    }
}

voxf_serializer::voxf_serializer(
    const voxf_data& data
)
    : data_(&data) {}

auto voxf_serializer::serialize(
    const std::filesystem::path& filepath
) -> std::expected<void, error_type> {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        log::warn(detail::voxf_lc, "failed to open file for writing: {}", filepath.string());
        return std::unexpected(error_type::file_open_failed);
    }

    return serialize(file);
}

auto voxf_serializer::serialize(
    std::ostream& output
) -> std::expected<void, error_type> {
    write_header_(output);

    for (const auto& state : data_->states) {
        write_state_(output, state);
    }

    if (!data_->any_transitions.empty()) {
        output << "any\n";
        for (const auto& rule : data_->any_transitions) {
            write_rule_(output, rule, "\t");
        }
    }

    if (output.fail()) {
        return std::unexpected(error_type::write_failed);
    }

    return {};
}

auto voxf_serializer::write_header_(
    std::ostream& output
) -> void {
    output << std::format("# Voxf File Version {}\n", voxf_file_version);

    if (!data_->rig.empty()) {
        output << std::format("rig {}\n", data_->rig);
    }
    if (!data_->entry_state.empty()) {
        output << std::format("entry {}\n", data_->entry_state);
    }

    for (const auto& param : data_->params) {
        output << std::format("param {} {}", param.name, detail::param_type_to_text(param.type));
        if (param.type == voxf_param_type::boolean && param.value != 0.0F) {
            output << " true";
        } else if (param.value != 0.0F) {
            output << std::format(" {:.6g}", param.value);
        }
        output << '\n';
    }
}

auto voxf_serializer::write_state_(
    std::ostream& output, const voxf_state& state
) -> void {
    output << std::format("state {}\n", state.name);

    if (!state.clip.empty()) {
        output << std::format("\tclip {}\n", state.clip.str());
    }

    output << std::format("\tplayback {}\n", detail::loop_mode_to_text(state.loop_mode));

    if (state.rate != 1.0F) {
        output << std::format("\trate {:.6g}\n", state.rate);
    }
    if (state.fade_in.duration > 0.0F) {
        output << std::format("\tfade_in {}\n", detail::transition_to_text(state.fade_in));
    }
    if (state.fade_out.duration > 0.0F) {
        output << std::format("\tfade_out {}\n", detail::transition_to_text(state.fade_out));
    }

    for (const auto& rule : state.transitions) {
        write_rule_(output, rule, "\t");
    }
}

auto voxf_serializer::write_rule_(
    std::ostream& output, const animation_fsm::transition_rule& rule, std::string_view indent
) -> void {
    output << std::format("{}to {}\n", indent, rule.target_state);

    const auto prop = std::format("{}\t", indent);

    if (!rule.trigger_name.empty()) {
        output << std::format("{}on {}\n", prop, rule.trigger_name);
    }

    for (const auto& condition : rule.conditions) {
        // Объявленное как bool пишется словом: «grounded == false» — то же, что
        // «grounded == 0», но читается без сверки с объявлением.
        const auto it = std::ranges::find(data_->params, condition.parameter, &voxf_param::name);
        const bool as_bool = it != data_->params.end() && it->type == voxf_param_type::boolean;

        const auto value =
            as_bool ? std::string{condition.value != 0.0F ? "true" : "false"}
                    : std::format("{:.6g}", condition.value);

        output << std::format(
            "{}when {} {} {}\n", prop, condition.parameter,
            detail::compare_to_text(condition.compare), value
        );
    }

    if (rule.blend.duration > 0.0F) {
        output << std::format("{}blend {}\n", prop, detail::transition_to_text(rule.blend));
    }
    if (rule.wait_until_end) {
        output << std::format("{}wait end\n", prop);
    }
    if (rule.wait_until_blend) {
        output << std::format("{}wait blend\n", prop);
    }
}

auto voxf_deserializer::deserialize(
    const std::filesystem::path& filepath
) -> std::expected<voxf_data, error_type> {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        log::warn(detail::voxf_lc, "failed to open file: {}", filepath.string());
        return std::unexpected(error_type::file_open_failed);
    }

    auto result = deserialize(file);
    if (!result) {
        log::warn(detail::voxf_lc, "parse error in file: {}", filepath.string());
    }
    return result;
}

auto voxf_deserializer::deserialize(
    std::istream& input
) -> std::expected<voxf_data, error_type> {
    data_        = {};
    state_index_ = no_index;
    rule_index_  = no_index;
    in_any_      = false;
    error_       = std::nullopt;

    std::string line;
    while (std::getline(input, line)) {
        if (error_.has_value()) {
            break;
        }

        process_line_(line);
    }

    if (error_.has_value()) {
        return std::unexpected(*error_);
    }

    return std::move(data_);
}

auto voxf_deserializer::process_line_(
    std::string_view line
) -> void {
    const auto parsed = detail::split_line(line);
    if (!parsed.has_value()) {
        return;
    }

    if (parsed->name == "#") {
        process_version_(parsed->value);
        return;
    }

    // Три уровня те же, что и у .vox: шапка и состояния, теги состояния,
    // свойства перехода.
    switch (parsed->depth) {
        case 0: process_top_(parsed->name, parsed->value); return;
        case 1: process_state_tag_(parsed->name, parsed->value); return;
        default: process_rule_prop_(parsed->name, parsed->value); return;
    }
}

auto voxf_deserializer::process_version_(
    std::string_view text
) -> void {
    std::istringstream iss{std::string{text}};

    const auto version = detail::read_header_version(iss);
    if (!version.has_value()) {
        return;
    }

    if (detail::major_version_differs(*version, voxf_file_version)) {
        log::warn(
            detail::voxf_lc, "unsupported voxf version {}, expected {}", *version,
            voxf_file_version
        );
        error_ = error_type::unsupported_version;
    }
}

auto voxf_deserializer::process_top_(
    std::string_view name, std::string_view value
) -> void {
    if (name == "any") {
        in_any_      = true;
        state_index_ = no_index;
        rule_index_  = no_index;
        return;
    }

    if (value.empty()) {
        error_ = error_type::parse_error;
        return;
    }

    if (name == "rig") {
        data_.rig = std::string{value};
        return;
    }

    if (name == "entry") {
        data_.entry_state = std::string{value};
        return;
    }

    if (name == "param") {
        process_param_(value);
        return;
    }

    if (name != "state") {
        // Словарь автомата закрыт — его целиком знает vw.asset, — поэтому
        // незнакомый тег здесь не расширение формата, а опечатка.
        log::warn(detail::voxf_lc, "unknown top-level command: {}", name);
        return;
    }

    data_.states.push_back(voxf_state{.name = std::string{value}});
    state_index_ = data_.states.size() - 1;
    rule_index_  = no_index;
    in_any_      = false;
}

auto voxf_deserializer::process_param_(
    std::string_view value
) -> void {
    const auto words = detail::split_words(value);
    if (words.size() < 2) {
        error_ = error_type::parse_error;
        return;
    }

    const auto type = detail::parse_param_type(words[1]);
    if (!type.has_value()) {
        log::warn(detail::voxf_lc, "unknown parameter type: {}", words[1]);
        error_ = error_type::parse_error;
        return;
    }

    voxf_param param{.name = std::string{words[0]}, .type = *type};
    if (words.size() > 2 && !detail::parse_number(words[2], param.value)) {
        error_ = error_type::parse_error;
        return;
    }

    data_.params.push_back(std::move(param));
}

auto voxf_deserializer::current_rules_() -> std::vector<animation_fsm::transition_rule>* {
    if (in_any_) {
        return &data_.any_transitions;
    }

    return state_index_ == no_index ? nullptr : &data_.states[state_index_].transitions;
}

auto voxf_deserializer::process_state_tag_(
    std::string_view name, std::string_view value
) -> void {
    auto* rules = current_rules_();
    if (rules == nullptr) {
        error_ = error_type::parse_error;
        return;
    }

    if (name == "to") {
        if (value.empty()) {
            error_ = error_type::parse_error;
            return;
        }

        rules->push_back({.target_state = std::string{value}});
        rule_index_ = rules->size() - 1;
        return;
    }

    // В блоке any состояния нет, и его теги там означали бы неизвестно что.
    if (in_any_ || state_index_ == no_index) {
        log::warn(detail::voxf_lc, "tag {} is not allowed inside any", name);
        return;
    }

    rule_index_ = no_index;
    auto& state = data_.states[state_index_];

    if (name == "clip") {
        state.clip = asset_ref{value};
        return;
    }

    if (name == "playback") {
        const auto mode = detail::parse_loop_mode(value);
        if (!mode.has_value()) {
            log::warn(detail::voxf_lc, "unknown playback mode: {}", value);
            error_ = error_type::parse_error;
            return;
        }
        state.loop_mode = *mode;
        return;
    }

    if (name == "rate") {
        if (!detail::parse_number(value, state.rate)) {
            error_ = error_type::parse_error;
        }
        return;
    }

    if (name == "fade_in" || name == "fade_out") {
        const auto words = detail::split_words(value);
        auto& target     = name == "fade_in" ? state.fade_in : state.fade_out;
        if (!detail::parse_transition(words, target)) {
            error_ = error_type::parse_error;
        }
        return;
    }

    log::warn(detail::voxf_lc, "unknown state tag: {}", name);
}

auto voxf_deserializer::process_rule_prop_(
    std::string_view name, std::string_view value
) -> void {
    auto* rules = current_rules_();
    if (rules == nullptr || rule_index_ == no_index) {
        error_ = error_type::parse_error;
        return;
    }

    auto& rule = (*rules)[rule_index_];

    if (name == "on") {
        rule.trigger_name = std::string{value};
        return;
    }

    if (name == "when") {
        const auto words = detail::split_words(value);
        if (words.size() != 3) {
            error_ = error_type::parse_error;
            return;
        }

        const auto compare = detail::parse_compare(words[1]);
        fsm_condition condition{.parameter = std::string{words[0]}};
        if (!compare.has_value() || !detail::parse_number(words[2], condition.value)) {
            log::warn(detail::voxf_lc, "malformed condition: {}", value);
            error_ = error_type::parse_error;
            return;
        }

        condition.compare = *compare;
        rule.conditions.push_back(std::move(condition));
        return;
    }

    if (name == "blend") {
        if (!detail::parse_transition(detail::split_words(value), rule.blend)) {
            error_ = error_type::parse_error;
        }
        return;
    }

    if (name == "wait") {
        if (value == "end") {
            rule.wait_until_end = true;
        } else if (value == "blend") {
            rule.wait_until_blend = true;
        } else {
            log::warn(detail::voxf_lc, "unknown wait kind: {}", value);
        }
        return;
    }

    log::warn(detail::voxf_lc, "unknown transition property: {}", name);
}

}  // namespace vw::asset
