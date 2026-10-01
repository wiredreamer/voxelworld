export module vw.core:json;

import std;

import :types;

export namespace vw::json {

class value;
struct member;

using array = std::vector<value>;

class object final {
public:
    object();
    object(std::initializer_list<member> members);
    object(const object& other);
    object(object&& other) noexcept;
    ~object();

    auto operator=(const object& other) -> object&;
    auto operator=(object&& other) noexcept -> object&;

    [[nodiscard]] auto find(std::string_view key) const -> const value*;
    [[nodiscard]] auto find(std::string_view key) -> value*;
    [[nodiscard]] auto contains(std::string_view key) const -> bool;

    auto set(std::string key, value item) -> value&;
    auto erase(std::string_view key) -> bool;

    [[nodiscard]] auto members() const -> std::span<const member>;
    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto empty() const -> bool;

    [[nodiscard]] auto operator==(const object& other) const -> bool;

private:
    std::vector<member> members_;
};

enum class value_kind : uint8 {
    null,
    boolean,
    integer,
    real,
    string,
    array,
    object,
};

class value final {
public:
    value() noexcept = default;
    value(std::nullptr_t) noexcept;
    value(bool flag) noexcept;
    value(std::string text) noexcept;
    value(std::string_view text);
    value(const char* text);
    value(array items) noexcept;
    value(object members) noexcept;

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    value(T number) noexcept {
        if constexpr (std::unsigned_integral<T>) {
            if (static_cast<uint64>(number) > static_cast<uint64>(std::numeric_limits<int64>::max())) {
                data_ = static_cast<float64>(number);
                return;
            }
        }
        data_ = static_cast<int64>(number);
    }

    template <std::floating_point T>
    value(T number) noexcept
        : data_(static_cast<float64>(number)) {}

    template <typename T>
    value(T* pointer) = delete;

    [[nodiscard]] auto kind() const noexcept -> value_kind;

    [[nodiscard]] auto is_null() const noexcept -> bool;
    [[nodiscard]] auto is_bool() const noexcept -> bool;
    [[nodiscard]] auto is_integer() const noexcept -> bool;
    [[nodiscard]] auto is_real() const noexcept -> bool;
    [[nodiscard]] auto is_number() const noexcept -> bool;
    [[nodiscard]] auto is_string() const noexcept -> bool;
    [[nodiscard]] auto is_array() const noexcept -> bool;
    [[nodiscard]] auto is_object() const noexcept -> bool;

    [[nodiscard]] auto as_bool() const noexcept -> std::optional<bool>;
    [[nodiscard]] auto as_integer() const noexcept -> std::optional<int64>;
    [[nodiscard]] auto as_number() const noexcept -> std::optional<float64>;
    [[nodiscard]] auto as_string() const noexcept -> const std::string*;
    [[nodiscard]] auto as_array() const noexcept -> const array*;
    [[nodiscard]] auto as_array() noexcept -> array*;
    [[nodiscard]] auto as_object() const noexcept -> const object*;
    [[nodiscard]] auto as_object() noexcept -> object*;

    [[nodiscard]] auto find(std::string_view key) const -> const value*;

    [[nodiscard]] auto operator==(const value& other) const -> bool;

private:
    std::variant<std::monostate, bool, int64, float64, std::string, array, object> data_;
};

struct member {
    std::string key;
    value item;
};

enum class parse_fault : uint8 {
    unexpected_end,
    unexpected_character,
    invalid_literal,
    invalid_number,
    number_out_of_range,
    invalid_escape,
    invalid_unicode_escape,
    control_character_in_string,
    invalid_utf8,
    depth_limit_exceeded,
    trailing_characters,
};

struct parse_error {
    parse_fault fault  = parse_fault::unexpected_end;
    std::size_t offset = 0;
    uint32 line        = 1;
    uint32 column      = 1;

    [[nodiscard]] auto operator==(const parse_error&) const -> bool = default;
};

struct parse_options {
    uint32 max_depth = 128;
};

struct dump_options {
    uint32 indent = 0;
};

[[nodiscard]] auto parse(std::string_view text, const parse_options& options = {})
    -> std::expected<value, parse_error>;

[[nodiscard]] auto dump(const value& root, const dump_options& options = {}) -> std::string;

[[nodiscard]] auto describe(parse_fault fault) -> std::string_view;
[[nodiscard]] auto describe(const parse_error& error) -> std::string;

struct access_error {
    std::string path;
    std::string message;

    [[nodiscard]] auto operator==(const access_error&) const -> bool = default;
};

[[nodiscard]] auto describe(const access_error& error) -> std::string;

class cursor final {
public:
    explicit cursor(const value& root, std::string path = "$");

    [[nodiscard]] auto operator[](std::string_view key) const -> cursor;
    [[nodiscard]] auto operator[](std::size_t index) const -> cursor;

    [[nodiscard]] auto exists() const noexcept -> bool;
    [[nodiscard]] auto get() const noexcept -> const value*;
    [[nodiscard]] auto path() const noexcept -> const std::string&;

    [[nodiscard]] auto boolean() const -> std::expected<bool, access_error>;
    [[nodiscard]] auto integer() const -> std::expected<int64, access_error>;
    [[nodiscard]] auto number() const -> std::expected<float64, access_error>;
    [[nodiscard]] auto string() const -> std::expected<std::string_view, access_error>;
    [[nodiscard]] auto elements() const -> std::expected<std::vector<cursor>, access_error>;
    [[nodiscard]] auto fields() const
        -> std::expected<std::vector<std::pair<std::string_view, cursor>>, access_error>;

private:
    cursor(const value* target, std::string path);

    [[nodiscard]] auto mismatch_(std::string_view expected) const -> access_error;

    const value* target_ = nullptr;
    std::string path_;
};

}  // namespace vw::json
