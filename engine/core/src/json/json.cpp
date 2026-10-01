module vw.core;

import std;

namespace vw::json {

namespace {

constexpr std::string_view byte_order_mark       = "\xEF\xBB\xBF";
constexpr std::string_view replacement_character = "\xEF\xBF\xBD";

[[nodiscard]] auto is_continuation(uint8 byte, uint8 lowest = 0x80, uint8 highest = 0xBF) -> bool {
    return byte >= lowest && byte <= highest;
}

[[nodiscard]] auto utf8_sequence_length(std::string_view rest) -> std::size_t {
    const auto byte_at = [rest](std::size_t index) -> uint8 {
        return index < rest.size() ? static_cast<uint8>(rest[index]) : uint8{0};
    };

    const uint8 lead = byte_at(0);

    if (lead < 0x80) {
        return rest.empty() ? 0 : 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        return is_continuation(byte_at(1)) ? 2 : 0;
    }
    if (lead >= 0xE0 && lead <= 0xEF) {
        const uint8 lowest  = lead == 0xE0 ? uint8{0xA0} : uint8{0x80};
        const uint8 highest = lead == 0xED ? uint8{0x9F} : uint8{0xBF};
        return is_continuation(byte_at(1), lowest, highest) && is_continuation(byte_at(2)) ? 3 : 0;
    }
    if (lead >= 0xF0 && lead <= 0xF4) {
        const uint8 lowest  = lead == 0xF0 ? uint8{0x90} : uint8{0x80};
        const uint8 highest = lead == 0xF4 ? uint8{0x8F} : uint8{0xBF};
        return is_continuation(byte_at(1), lowest, highest) && is_continuation(byte_at(2)) &&
                       is_continuation(byte_at(3))
                   ? 4
                   : 0;
    }
    return 0;
}

auto append_utf8(std::string& out, uint32 code_point) -> void {
    if (code_point < 0x80) {
        out.push_back(static_cast<char>(code_point));
        return;
    }
    if (code_point < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        return;
    }
    if (code_point < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        return;
    }
    out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
}

[[nodiscard]] auto is_digit(char symbol) -> bool {
    return symbol >= '0' && symbol <= '9';
}

[[nodiscard]] auto hex_digit_value(char symbol) -> std::optional<uint32> {
    if (symbol >= '0' && symbol <= '9') {
        return static_cast<uint32>(symbol - '0');
    }
    if (symbol >= 'a' && symbol <= 'f') {
        return static_cast<uint32>(symbol - 'a') + 10;
    }
    if (symbol >= 'A' && symbol <= 'F') {
        return static_cast<uint32>(symbol - 'A') + 10;
    }
    return std::nullopt;
}

class parser final {
public:
    parser(std::string_view text, const parse_options& options)
        : text_(text),
          max_depth_(options.max_depth) {}

    [[nodiscard]] auto run() -> std::expected<value, parse_error> {
        if (text_.starts_with(byte_order_mark)) {
            at_ = byte_order_mark.size();
        }

        auto root = parse_value_(0);
        if (!root) {
            return std::unexpected(locate_(root.error()));
        }

        skip_whitespace_();
        if (!at_end_()) {
            return std::unexpected(locate_(parse_fault::trailing_characters));
        }
        return std::move(*root);
    }

private:
    using parsed = std::expected<value, parse_fault>;

    [[nodiscard]] auto at_end_() const -> bool {
        return at_ >= text_.size();
    }

    [[nodiscard]] auto peek_() const -> char {
        return at_end_() ? '\0' : text_[at_];
    }

    [[nodiscard]] auto peek_is_(char symbol) const -> bool {
        return !at_end_() && text_[at_] == symbol;
    }

    auto skip_whitespace_() -> void {
        while (!at_end_()) {
            const char symbol = text_[at_];
            if (symbol != ' ' && symbol != '\t' && symbol != '\n' && symbol != '\r') {
                return;
            }
            ++at_;
        }
    }

    auto skip_digits_() -> void {
        while (!at_end_() && is_digit(text_[at_])) {
            ++at_;
        }
    }

    [[nodiscard]] auto locate_(parse_fault fault) const -> parse_error {
        const auto before    = text_.substr(0, std::min(at_, text_.size()));
        const auto line_feed = before.rfind('\n');
        const auto line_from = line_feed == std::string_view::npos ? 0 : line_feed + 1;

        return parse_error{
            .fault  = fault,
            .offset = before.size(),
            .line   = static_cast<uint32>(std::ranges::count(before, '\n')) + 1,
            .column = static_cast<uint32>(before.size() - line_from) + 1,
        };
    }

    [[nodiscard]] auto parse_value_(uint32 depth) -> parsed {
        skip_whitespace_();
        if (at_end_()) {
            return std::unexpected(parse_fault::unexpected_end);
        }

        const char symbol = text_[at_];
        switch (symbol) {
            case '{':
                return parse_object_(depth);
            case '[':
                return parse_array_(depth);
            case '"':
                return parse_string_().transform([](std::string&& text) {
                    return value{std::move(text)};
                });
            case 't':
                return parse_literal_("true", value{true});
            case 'f':
                return parse_literal_("false", value{false});
            case 'n':
                return parse_literal_("null", value{});
            default:
                break;
        }

        if (symbol == '-' || is_digit(symbol)) {
            return parse_number_();
        }
        return std::unexpected(parse_fault::unexpected_character);
    }

    [[nodiscard]] auto parse_literal_(std::string_view word, value result) -> parsed {
        if (!text_.substr(at_).starts_with(word)) {
            return std::unexpected(parse_fault::invalid_literal);
        }
        at_ += word.size();
        return result;
    }

    [[nodiscard]] auto parse_array_(uint32 depth) -> parsed {
        if (depth >= max_depth_) {
            return std::unexpected(parse_fault::depth_limit_exceeded);
        }
        ++at_;

        array items;

        skip_whitespace_();
        if (peek_is_(']')) {
            ++at_;
            return value{std::move(items)};
        }

        while (true) {
            auto item = parse_value_(depth + 1);
            if (!item) {
                return item;
            }
            items.push_back(std::move(*item));

            skip_whitespace_();
            if (at_end_()) {
                return std::unexpected(parse_fault::unexpected_end);
            }
            if (peek_is_(',')) {
                ++at_;
                continue;
            }
            if (peek_is_(']')) {
                ++at_;
                return value{std::move(items)};
            }
            return std::unexpected(parse_fault::unexpected_character);
        }
    }

    [[nodiscard]] auto parse_object_(uint32 depth) -> parsed {
        if (depth >= max_depth_) {
            return std::unexpected(parse_fault::depth_limit_exceeded);
        }
        ++at_;

        object members;

        skip_whitespace_();
        if (peek_is_('}')) {
            ++at_;
            return value{std::move(members)};
        }

        while (true) {
            skip_whitespace_();
            if (at_end_()) {
                return std::unexpected(parse_fault::unexpected_end);
            }
            if (!peek_is_('"')) {
                return std::unexpected(parse_fault::unexpected_character);
            }

            auto key = parse_string_();
            if (!key) {
                return std::unexpected(key.error());
            }

            skip_whitespace_();
            if (at_end_()) {
                return std::unexpected(parse_fault::unexpected_end);
            }
            if (!peek_is_(':')) {
                return std::unexpected(parse_fault::unexpected_character);
            }
            ++at_;

            auto item = parse_value_(depth + 1);
            if (!item) {
                return item;
            }
            members.set(std::move(*key), std::move(*item));

            skip_whitespace_();
            if (at_end_()) {
                return std::unexpected(parse_fault::unexpected_end);
            }
            if (peek_is_(',')) {
                ++at_;
                continue;
            }
            if (peek_is_('}')) {
                ++at_;
                return value{std::move(members)};
            }
            return std::unexpected(parse_fault::unexpected_character);
        }
    }

    [[nodiscard]] auto parse_string_() -> std::expected<std::string, parse_fault> {
        ++at_;

        std::string text;

        while (true) {
            if (at_end_()) {
                return std::unexpected(parse_fault::unexpected_end);
            }

            const auto byte = static_cast<uint8>(text_[at_]);
            if (byte == '"') {
                ++at_;
                return text;
            }
            if (byte == '\\') {
                if (const auto escaped = parse_escape_(text); !escaped) {
                    return std::unexpected(escaped.error());
                }
                continue;
            }
            if (byte < 0x20) {
                return std::unexpected(parse_fault::control_character_in_string);
            }

            const auto length = utf8_sequence_length(text_.substr(at_));
            if (length == 0) {
                return std::unexpected(parse_fault::invalid_utf8);
            }
            text.append(text_.substr(at_, length));
            at_ += length;
        }
    }

    [[nodiscard]] auto parse_escape_(std::string& text) -> std::expected<void, parse_fault> {
        ++at_;
        if (at_end_()) {
            return std::unexpected(parse_fault::unexpected_end);
        }

        const char symbol = text_[at_];
        switch (symbol) {
            case '"':
            case '\\':
            case '/':
                text.push_back(symbol);
                break;
            case 'b':
                text.push_back('\b');
                break;
            case 'f':
                text.push_back('\f');
                break;
            case 'n':
                text.push_back('\n');
                break;
            case 'r':
                text.push_back('\r');
                break;
            case 't':
                text.push_back('\t');
                break;
            case 'u':
                return parse_unicode_escape_(text);
            default:
                return std::unexpected(parse_fault::invalid_escape);
        }

        ++at_;
        return {};
    }

    [[nodiscard]] auto parse_unicode_escape_(std::string& text) -> std::expected<void, parse_fault> {
        ++at_;

        const auto first = parse_code_unit_();
        if (!first) {
            return std::unexpected(first.error());
        }

        uint32 code_point = *first;

        if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
            return std::unexpected(parse_fault::invalid_unicode_escape);
        }

        if (code_point >= 0xD800 && code_point <= 0xDBFF) {
            if (!text_.substr(at_).starts_with("\\u")) {
                return std::unexpected(
                    at_end_() ? parse_fault::unexpected_end : parse_fault::invalid_unicode_escape
                );
            }
            at_ += 2;

            const auto second = parse_code_unit_();
            if (!second) {
                return std::unexpected(second.error());
            }
            if (*second < 0xDC00 || *second > 0xDFFF) {
                return std::unexpected(parse_fault::invalid_unicode_escape);
            }
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (*second - 0xDC00);
        }

        append_utf8(text, code_point);
        return {};
    }

    [[nodiscard]] auto parse_code_unit_() -> std::expected<uint32, parse_fault> {
        uint32 unit = 0;
        for (uint32 digit = 0; digit < 4; ++digit) {
            if (at_end_()) {
                return std::unexpected(parse_fault::unexpected_end);
            }
            const auto nibble = hex_digit_value(text_[at_]);
            if (!nibble) {
                return std::unexpected(parse_fault::invalid_unicode_escape);
            }
            unit = (unit << 4) | *nibble;
            ++at_;
        }
        return unit;
    }

    [[nodiscard]] auto parse_number_() -> parsed {
        const std::size_t start = at_;

        if (peek_is_('-')) {
            ++at_;
        }

        if (peek_is_('0')) {
            ++at_;
        } else if (is_digit(peek_())) {
            skip_digits_();
        } else {
            return std::unexpected(
                at_end_() ? parse_fault::unexpected_end : parse_fault::invalid_number
            );
        }

        bool integral = true;

        if (peek_is_('.')) {
            integral = false;
            ++at_;
            if (!is_digit(peek_())) {
                return std::unexpected(
                    at_end_() ? parse_fault::unexpected_end : parse_fault::invalid_number
                );
            }
            skip_digits_();
        }

        if (peek_is_('e') || peek_is_('E')) {
            integral = false;
            ++at_;
            if (peek_is_('+') || peek_is_('-')) {
                ++at_;
            }
            if (!is_digit(peek_())) {
                return std::unexpected(
                    at_end_() ? parse_fault::unexpected_end : parse_fault::invalid_number
                );
            }
            skip_digits_();
        }

        const char* first = text_.data() + start;
        const char* last  = text_.data() + at_;

        if (integral) {
            int64 whole = 0;
            if (std::from_chars(first, last, whole).ec == std::errc{}) {
                return value{whole};
            }
        }

        float64 real = 0.0;
        if (std::from_chars(first, last, real).ec != std::errc{}) {
            at_ = start;
            return std::unexpected(parse_fault::number_out_of_range);
        }
        return value{real};
    }

    std::string_view text_;
    std::size_t at_ = 0;
    uint32 max_depth_;
};

class dumper final {
public:
    explicit dumper(const dump_options& options)
        : indent_(options.indent) {}

    [[nodiscard]] auto run(const value& root) -> std::string {
        write_value_(root, 0);
        return std::move(out_);
    }

private:
    auto write_value_(const value& item, uint32 level) -> void {
        switch (item.kind()) {
            case value_kind::null:
                out_ += "null";
                return;
            case value_kind::boolean:
                out_ += *item.as_bool() ? "true" : "false";
                return;
            case value_kind::integer:
                write_integer_(*item.as_integer());
                return;
            case value_kind::real:
                write_real_(*item.as_number());
                return;
            case value_kind::string:
                write_string_(*item.as_string());
                return;
            case value_kind::array:
                write_array_(*item.as_array(), level);
                return;
            case value_kind::object:
                write_object_(*item.as_object(), level);
                return;
        }
    }

    auto write_integer_(int64 whole) -> void {
        std::array<char, 24> digits{};
        const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), whole);
        out_.append(digits.data(), written.ptr);
    }

    auto write_real_(float64 real) -> void {
        if (!std::isfinite(real)) {
            out_ += "null";
            return;
        }

        std::array<char, 32> digits{};
        const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), real);
        const std::string_view token{digits.data(), written.ptr};

        out_ += token;
        if (token.find_first_of(".e") == std::string_view::npos) {
            out_ += ".0";
        }
    }

    auto write_string_(std::string_view text) -> void {
        constexpr std::string_view hex_digits = "0123456789abcdef";

        out_.push_back('"');

        std::size_t at = 0;
        while (at < text.size()) {
            const auto byte = static_cast<uint8>(text[at]);

            switch (byte) {
                case '"':
                    out_ += "\\\"";
                    ++at;
                    continue;
                case '\\':
                    out_ += "\\\\";
                    ++at;
                    continue;
                case '\b':
                    out_ += "\\b";
                    ++at;
                    continue;
                case '\f':
                    out_ += "\\f";
                    ++at;
                    continue;
                case '\n':
                    out_ += "\\n";
                    ++at;
                    continue;
                case '\r':
                    out_ += "\\r";
                    ++at;
                    continue;
                case '\t':
                    out_ += "\\t";
                    ++at;
                    continue;
                default:
                    break;
            }

            if (byte < 0x20) {
                out_ += "\\u00";
                out_.push_back(hex_digits[byte >> 4]);
                out_.push_back(hex_digits[byte & 0x0F]);
                ++at;
                continue;
            }

            const auto length = utf8_sequence_length(text.substr(at));
            if (length == 0) {
                out_ += replacement_character;
                ++at;
                continue;
            }
            out_.append(text.substr(at, length));
            at += length;
        }

        out_.push_back('"');
    }

    auto write_array_(const array& items, uint32 level) -> void {
        if (items.empty()) {
            out_ += "[]";
            return;
        }

        out_.push_back('[');
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (index > 0) {
                out_.push_back(',');
            }
            break_line_(level + 1);
            write_value_(items[index], level + 1);
        }
        break_line_(level);
        out_.push_back(']');
    }

    auto write_object_(const object& members, uint32 level) -> void {
        if (members.empty()) {
            out_ += "{}";
            return;
        }

        out_.push_back('{');
        bool first = true;
        for (const auto& [key, item] : members.members()) {
            if (!first) {
                out_.push_back(',');
            }
            first = false;

            break_line_(level + 1);
            write_string_(key);
            out_.push_back(':');
            if (indent_ > 0) {
                out_.push_back(' ');
            }
            write_value_(item, level + 1);
        }
        break_line_(level);
        out_.push_back('}');
    }

    auto break_line_(uint32 level) -> void {
        if (indent_ == 0) {
            return;
        }
        out_.push_back('\n');
        out_.append(static_cast<std::size_t>(level) * indent_, ' ');
    }

    std::string out_;
    uint32 indent_;
};

[[nodiscard]] auto kind_name(value_kind kind) -> std::string_view {
    switch (kind) {
        case value_kind::null:
            return "null";
        case value_kind::boolean:
            return "boolean";
        case value_kind::integer:
            return "integer";
        case value_kind::real:
            return "real";
        case value_kind::string:
            return "string";
        case value_kind::array:
            return "array";
        case value_kind::object:
            return "object";
    }
    return "unknown";
}

}  // namespace

object::object() = default;

object::object(std::initializer_list<member> members) {
    members_.reserve(members.size());
    for (const auto& [key, item] : members) {
        set(key, item);
    }
}

object::object(const object& other)     = default;
object::object(object&& other) noexcept = default;
object::~object()                       = default;

auto object::operator=(const object& other) -> object&     = default;
auto object::operator=(object&& other) noexcept -> object& = default;

auto object::find(std::string_view key) const -> const value* {
    const auto found = std::ranges::find(members_, key, &member::key);
    return found == members_.end() ? nullptr : &found->item;
}

auto object::find(std::string_view key) -> value* {
    const auto found = std::ranges::find(members_, key, &member::key);
    return found == members_.end() ? nullptr : &found->item;
}

auto object::contains(std::string_view key) const -> bool {
    return find(key) != nullptr;
}

auto object::set(std::string key, value item) -> value& {
    if (auto* existing = find(key); existing != nullptr) {
        *existing = std::move(item);
        return *existing;
    }
    return members_.emplace_back(std::move(key), std::move(item)).item;
}

auto object::erase(std::string_view key) -> bool {
    const auto found = std::ranges::find(members_, key, &member::key);
    if (found == members_.end()) {
        return false;
    }
    members_.erase(found);
    return true;
}

auto object::members() const -> std::span<const member> {
    return members_;
}

auto object::size() const -> std::size_t {
    return members_.size();
}

auto object::empty() const -> bool {
    return members_.empty();
}

auto object::operator==(const object& other) const -> bool {
    if (members_.size() != other.members_.size()) {
        return false;
    }
    return std::ranges::all_of(members_, [&other](const member& own) {
        const auto* theirs = other.find(own.key);
        return theirs != nullptr && *theirs == own.item;
    });
}

value::value(std::nullptr_t) noexcept {}

value::value(bool flag) noexcept
    : data_(flag) {}

value::value(std::string text) noexcept
    : data_(std::move(text)) {}

value::value(std::string_view text)
    : data_(std::string{text}) {}

value::value(const char* text)
    : data_(std::string{text}) {}

value::value(array items) noexcept
    : data_(std::move(items)) {}

value::value(object members) noexcept
    : data_(std::move(members)) {}

auto value::kind() const noexcept -> value_kind {
    return static_cast<value_kind>(data_.index());
}

auto value::is_null() const noexcept -> bool {
    return kind() == value_kind::null;
}

auto value::is_bool() const noexcept -> bool {
    return kind() == value_kind::boolean;
}

auto value::is_integer() const noexcept -> bool {
    return kind() == value_kind::integer;
}

auto value::is_real() const noexcept -> bool {
    return kind() == value_kind::real;
}

auto value::is_number() const noexcept -> bool {
    return is_integer() || is_real();
}

auto value::is_string() const noexcept -> bool {
    return kind() == value_kind::string;
}

auto value::is_array() const noexcept -> bool {
    return kind() == value_kind::array;
}

auto value::is_object() const noexcept -> bool {
    return kind() == value_kind::object;
}

auto value::as_bool() const noexcept -> std::optional<bool> {
    const auto* flag = std::get_if<bool>(&data_);
    return flag == nullptr ? std::nullopt : std::optional{*flag};
}

auto value::as_integer() const noexcept -> std::optional<int64> {
    const auto* whole = std::get_if<int64>(&data_);
    return whole == nullptr ? std::nullopt : std::optional{*whole};
}

auto value::as_number() const noexcept -> std::optional<float64> {
    if (const auto* whole = std::get_if<int64>(&data_); whole != nullptr) {
        return static_cast<float64>(*whole);
    }
    const auto* real = std::get_if<float64>(&data_);
    return real == nullptr ? std::nullopt : std::optional{*real};
}

auto value::as_string() const noexcept -> const std::string* {
    return std::get_if<std::string>(&data_);
}

auto value::as_array() const noexcept -> const array* {
    return std::get_if<array>(&data_);
}

auto value::as_array() noexcept -> array* {
    return std::get_if<array>(&data_);
}

auto value::as_object() const noexcept -> const object* {
    return std::get_if<object>(&data_);
}

auto value::as_object() noexcept -> object* {
    return std::get_if<object>(&data_);
}

auto value::find(std::string_view key) const -> const value* {
    const auto* members = as_object();
    return members == nullptr ? nullptr : members->find(key);
}

auto value::operator==(const value& other) const -> bool {
    if (is_number() && other.is_number() && kind() != other.kind()) {
        return *as_number() == *other.as_number();
    }
    return data_ == other.data_;
}

auto parse(std::string_view text, const parse_options& options)
    -> std::expected<value, parse_error> {
    return parser{text, options}.run();
}

auto dump(const value& root, const dump_options& options) -> std::string {
    return dumper{options}.run(root);
}

auto describe(parse_fault fault) -> std::string_view {
    switch (fault) {
        case parse_fault::unexpected_end:
            return "unexpected end of input";
        case parse_fault::unexpected_character:
            return "unexpected character";
        case parse_fault::invalid_literal:
            return "invalid literal";
        case parse_fault::invalid_number:
            return "invalid number";
        case parse_fault::number_out_of_range:
            return "number out of range";
        case parse_fault::invalid_escape:
            return "invalid escape sequence";
        case parse_fault::invalid_unicode_escape:
            return "invalid unicode escape";
        case parse_fault::control_character_in_string:
            return "control character in string";
        case parse_fault::invalid_utf8:
            return "invalid UTF-8";
        case parse_fault::depth_limit_exceeded:
            return "nesting is too deep";
        case parse_fault::trailing_characters:
            return "characters after the value";
    }
    return "unknown fault";
}

auto describe(const parse_error& error) -> std::string {
    return std::format("line {}, column {}: {}", error.line, error.column, describe(error.fault));
}

auto describe(const access_error& error) -> std::string {
    return std::format("{}: {}", error.path, error.message);
}

cursor::cursor(const value& root, std::string path)
    : target_(&root),
      path_(std::move(path)) {}

cursor::cursor(const value* target, std::string path)
    : target_(target),
      path_(std::move(path)) {}

auto cursor::operator[](std::string_view key) const -> cursor {
    const value* child = target_ == nullptr ? nullptr : target_->find(key);
    return cursor{child, std::format("{}.{}", path_, key)};
}

auto cursor::operator[](std::size_t index) const -> cursor {
    const array* items = target_ == nullptr ? nullptr : target_->as_array();
    const value* child = items != nullptr && index < items->size() ? &(*items)[index] : nullptr;
    return cursor{child, std::format("{}[{}]", path_, index)};
}

auto cursor::exists() const noexcept -> bool {
    return target_ != nullptr;
}

auto cursor::get() const noexcept -> const value* {
    return target_;
}

auto cursor::path() const noexcept -> const std::string& {
    return path_;
}

auto cursor::mismatch_(std::string_view expected) const -> access_error {
    if (target_ == nullptr) {
        return access_error{.path = path_, .message = std::format("missing, expected {}", expected)};
    }
    return access_error{
        .path    = path_,
        .message = std::format("expected {}, found {}", expected, kind_name(target_->kind())),
    };
}

auto cursor::boolean() const -> std::expected<bool, access_error> {
    if (target_ != nullptr) {
        if (const auto flag = target_->as_bool(); flag) {
            return *flag;
        }
    }
    return std::unexpected(mismatch_("boolean"));
}

auto cursor::integer() const -> std::expected<int64, access_error> {
    if (target_ != nullptr) {
        if (const auto whole = target_->as_integer(); whole) {
            return *whole;
        }
    }
    return std::unexpected(mismatch_("integer"));
}

auto cursor::number() const -> std::expected<float64, access_error> {
    if (target_ != nullptr) {
        if (const auto real = target_->as_number(); real) {
            return *real;
        }
    }
    return std::unexpected(mismatch_("number"));
}

auto cursor::string() const -> std::expected<std::string_view, access_error> {
    if (target_ != nullptr) {
        if (const auto* text = target_->as_string(); text != nullptr) {
            return std::string_view{*text};
        }
    }
    return std::unexpected(mismatch_("string"));
}

auto cursor::elements() const -> std::expected<std::vector<cursor>, access_error> {
    const array* items = target_ == nullptr ? nullptr : target_->as_array();
    if (items == nullptr) {
        return std::unexpected(mismatch_("array"));
    }

    std::vector<cursor> children;
    children.reserve(items->size());
    for (std::size_t index = 0; index < items->size(); ++index) {
        children.push_back((*this)[index]);
    }
    return children;
}

auto cursor::fields() const
    -> std::expected<std::vector<std::pair<std::string_view, cursor>>, access_error> {
    const object* members = target_ == nullptr ? nullptr : target_->as_object();
    if (members == nullptr) {
        return std::unexpected(mismatch_("object"));
    }

    std::vector<std::pair<std::string_view, cursor>> children;
    children.reserve(members->size());
    for (const auto& [key, item] : members->members()) {
        children.emplace_back(key, cursor{&item, std::format("{}.{}", path_, key)});
    }
    return children;
}

}  // namespace vw::json
