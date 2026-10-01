#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

using vw::float64;
using vw::int32;
using vw::int64;
using vw::uint64;
using vw::uint8;

namespace json = vw::json;

namespace {

auto parsed(std::string_view text) -> json::value {
    auto result = json::parse(text);
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto fault_of(std::string_view text) -> json::parse_fault {
    const auto result = json::parse(text);
    REQUIRE_FALSE(result.has_value());
    return result.error().fault;
}

constexpr char backslash = '\\';

auto code_units(std::initializer_list<std::string_view> hex_quads) -> std::string {
    std::string escapes;
    for (const std::string_view quad : hex_quads) {
        escapes.push_back(backslash);
        escapes.push_back('u');
        escapes.append(quad);
    }
    return escapes;
}

auto in_quotes(std::string_view body) -> std::string {
    return std::format("\"{}\"", body);
}

}  // namespace

TEST_CASE("json value is null until given something", "[json]") {
    const json::value empty;

    CHECK(empty.is_null());
    CHECK(empty.kind() == json::value_kind::null);
    CHECK(json::value{nullptr}.is_null());
}

TEST_CASE("json value keeps the kind it was built from", "[json]") {
    CHECK(json::value{true}.kind() == json::value_kind::boolean);
    CHECK(json::value{int32{-7}}.kind() == json::value_kind::integer);
    CHECK(json::value{uint8{7}}.kind() == json::value_kind::integer);
    CHECK(json::value{1.5F}.kind() == json::value_kind::real);
    CHECK(json::value{"text"}.kind() == json::value_kind::string);
    CHECK(json::value{std::string_view{"text"}}.kind() == json::value_kind::string);
    CHECK(json::value{std::string{"text"}}.kind() == json::value_kind::string);
    CHECK(json::value{json::array{}}.kind() == json::value_kind::array);
    CHECK(json::value{json::object{}}.kind() == json::value_kind::object);
}

TEST_CASE("json value built from a string literal is a string, not a boolean", "[json]") {
    const json::value text{"false"};

    REQUIRE(text.as_string() != nullptr);
    CHECK(*text.as_string() == "false");
    CHECK_FALSE(text.as_bool().has_value());
}

TEST_CASE("json value turns an unsigned too large for int64 into a real", "[json]") {
    const json::value fits{static_cast<uint64>(std::numeric_limits<int64>::max())};
    const json::value overflows{std::numeric_limits<uint64>::max()};

    CHECK(fits.as_integer() == std::numeric_limits<int64>::max());
    CHECK(overflows.is_real());
    CHECK(overflows.as_number() == static_cast<float64>(std::numeric_limits<uint64>::max()));
}

TEST_CASE("json value hands out only the kind it holds", "[json]") {
    const json::value whole{int64{3}};
    const json::value real{2.5};

    CHECK(whole.as_integer() == 3);
    CHECK(whole.as_number() == 3.0);
    CHECK_FALSE(whole.as_bool().has_value());
    CHECK(whole.as_string() == nullptr);
    CHECK(whole.as_array() == nullptr);
    CHECK(whole.as_object() == nullptr);

    CHECK_FALSE(real.as_integer().has_value());
    CHECK(real.as_number() == 2.5);
}

TEST_CASE("json numbers compare by value across integer and real", "[json]") {
    CHECK(json::value{int64{1}} == json::value{1.0});
    CHECK_FALSE(json::value{int64{1}} == json::value{1.5});
    CHECK_FALSE(json::value{int64{1}} == json::value{true});
    CHECK_FALSE(json::value{int64{0}} == json::value{});
}

TEST_CASE("json object keeps members in the order they were set", "[json]") {
    json::object members;
    members.set("zeta", 1);
    members.set("alpha", 2);
    members.set("mid", 3);

    const auto listed = members.members();

    REQUIRE(listed.size() == 3);
    CHECK(listed[0].key == "zeta");
    CHECK(listed[1].key == "alpha");
    CHECK(listed[2].key == "mid");
}

TEST_CASE("json object replaces a member set twice and keeps its place", "[json]") {
    json::object members{{"first", 1}, {"second", 2}};
    members.set("first", "again");

    REQUIRE(members.size() == 2);
    CHECK(members.members()[0].key == "first");
    CHECK(*members.find("first") == json::value{"again"});
}

TEST_CASE("json object finds, reports and erases members", "[json]") {
    json::object members{{"kept", true}, {"dropped", false}};

    CHECK(members.contains("kept"));
    CHECK(members.find("absent") == nullptr);
    CHECK(members.erase("dropped"));
    CHECK_FALSE(members.erase("dropped"));
    CHECK(members.size() == 1);
    CHECK_FALSE(members.empty());
}

TEST_CASE("json objects are equal whatever the order of their members", "[json]") {
    const json::object left{{"a", 1}, {"b", 2}};
    const json::object right{{"b", 2}, {"a", 1}};
    const json::object other{{"a", 1}, {"b", 3}};
    const json::object longer{{"a", 1}, {"b", 2}, {"c", 3}};

    CHECK(left == right);
    CHECK_FALSE(left == other);
    CHECK_FALSE(left == longer);
}

TEST_CASE("json parser reads every kind of value", "[json]") {
    CHECK(parsed("null").is_null());
    CHECK(parsed("true") == json::value{true});
    CHECK(parsed("false") == json::value{false});
    CHECK(parsed("42").as_integer() == 42);
    CHECK(parsed("-17").as_integer() == -17);
    CHECK(parsed("2.5").as_number() == 2.5);
    CHECK(parsed("\"text\"") == json::value{"text"});
    CHECK(parsed("[]") == json::value{json::array{}});
    CHECK(parsed("{}") == json::value{json::object{}});
}

TEST_CASE("json parser reads nested containers", "[json]") {
    const auto root = parsed(R"({"node": "head", "position": [0, 9.5, 0], "visible": true})");

    const json::value expected{json::object{
        {"node", "head"},
        {"position", json::array{0, 9.5, 0}},
        {"visible", true},
    }};

    CHECK(root == expected);
}

TEST_CASE("json parser allows whitespace around every token", "[json]") {
    const auto root = parsed(" \t\r\n{ \"a\" :\n[ 1 ,\t2 ] }\n ");

    CHECK(root == json::value{json::object{{"a", json::array{1, 2}}}});
}

TEST_CASE("json parser tells integers from reals", "[json]") {
    CHECK(parsed("10").is_integer());
    CHECK(parsed("-0").is_integer());
    CHECK(parsed("10.0").is_real());
    CHECK(parsed("1e2").is_real());
    CHECK(parsed("1E+2").as_number() == 100.0);
    CHECK(parsed("25e-1").as_number() == 2.5);
}

TEST_CASE("json parser keeps the whole range of int64", "[json]") {
    CHECK(parsed("9223372036854775807").as_integer() == std::numeric_limits<int64>::max());
    CHECK(parsed("-9223372036854775808").as_integer() == std::numeric_limits<int64>::min());
}

TEST_CASE("json parser reads an integer past int64 as a real", "[json]") {
    const auto huge = parsed("9223372036854775808");

    CHECK(huge.is_real());
    CHECK(huge.as_number() == 9223372036854775808.0);
}

TEST_CASE("json parser refuses a number too large for a real", "[json]") {
    CHECK(fault_of("1e400") == json::parse_fault::number_out_of_range);
}

TEST_CASE("json parser refuses numbers outside the grammar", "[json]") {
    CHECK(fault_of("01") == json::parse_fault::trailing_characters);
    CHECK(fault_of("-") == json::parse_fault::unexpected_end);
    CHECK(fault_of("-x") == json::parse_fault::invalid_number);
    CHECK(fault_of("1.") == json::parse_fault::unexpected_end);
    CHECK(fault_of("1.x") == json::parse_fault::invalid_number);
    CHECK(fault_of("1e") == json::parse_fault::unexpected_end);
    CHECK(fault_of("1e+") == json::parse_fault::unexpected_end);
    CHECK(fault_of(".5") == json::parse_fault::unexpected_character);
    CHECK(fault_of("+1") == json::parse_fault::unexpected_character);
    CHECK(fault_of("NaN") == json::parse_fault::unexpected_character);
    CHECK(fault_of("Infinity") == json::parse_fault::unexpected_character);
}

TEST_CASE("json parser decodes escapes", "[json]") {
    const auto text = parsed("\"quote \\\" slash \\\\ solidus \\/ \\b\\f\\n\\r\\t\"");

    CHECK(*text.as_string() == "quote \" slash \\ solidus / \b\f\n\r\t");
}

TEST_CASE("json parser decodes unicode escapes into UTF-8", "[json]") {
    const auto latin    = parsed(in_quotes(code_units({"0041"})));
    const auto accented = parsed(in_quotes(code_units({"00e9"})));
    const auto euro     = parsed(in_quotes(code_units({"20AC"})));
    const auto pair     = parsed(in_quotes(code_units({"d83d", "de00"})));

    CHECK(*latin.as_string() == "A");
    CHECK(*accented.as_string() == "\xC3\xA9");
    CHECK(*euro.as_string() == "\xE2\x82\xAC");
    CHECK(*pair.as_string() == "\xF0\x9F\x98\x80");
}

TEST_CASE("json parser keeps an escaped null inside the string", "[json]") {
    const auto text = parsed(in_quotes("a" + code_units({"0000"}) + "b"));

    CHECK(*text.as_string() == std::string{"a\0b", 3});
}

TEST_CASE("json parser refuses broken escapes", "[json]") {
    const std::string unknown_escape  = in_quotes(std::string{backslash} + "x");
    const std::string not_hex         = in_quotes(code_units({"12g4"}));
    const std::string lone_high       = in_quotes(code_units({"d83d"}));
    const std::string high_then_plain = in_quotes(code_units({"d83d", "0041"}));
    const std::string lone_low        = in_quotes(code_units({"de00"}));
    const std::string cut_short       = "\"" + code_units({"12"});

    CHECK(fault_of(unknown_escape) == json::parse_fault::invalid_escape);
    CHECK(fault_of(not_hex) == json::parse_fault::invalid_unicode_escape);
    CHECK(fault_of(lone_high) == json::parse_fault::invalid_unicode_escape);
    CHECK(fault_of(high_then_plain) == json::parse_fault::invalid_unicode_escape);
    CHECK(fault_of(lone_low) == json::parse_fault::invalid_unicode_escape);
    CHECK(fault_of(cut_short) == json::parse_fault::unexpected_end);
}

TEST_CASE("json parser passes UTF-8 through and refuses bytes that are not", "[json]") {
    CHECK(*parsed("\"\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xB0\"").as_string() ==
          "\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xB0");

    CHECK(fault_of("\"\xFF\"") == json::parse_fault::invalid_utf8);
    CHECK(fault_of("\"\xC3\"") == json::parse_fault::invalid_utf8);
    CHECK(fault_of("\"\xC0\xAF\"") == json::parse_fault::invalid_utf8);
    CHECK(fault_of("\"\xED\xA0\x80\"") == json::parse_fault::invalid_utf8);
    CHECK(fault_of("\"\xF4\x90\x80\x80\"") == json::parse_fault::invalid_utf8);
}

TEST_CASE("json parser refuses a raw control character in a string", "[json]") {
    CHECK(fault_of("\"line\nbreak\"") == json::parse_fault::control_character_in_string);
    CHECK(fault_of("\"tab\there\"") == json::parse_fault::control_character_in_string);
}

TEST_CASE("json parser refuses malformed structure", "[json]") {
    CHECK(fault_of("") == json::parse_fault::unexpected_end);
    CHECK(fault_of("   ") == json::parse_fault::unexpected_end);
    CHECK(fault_of("[1, 2") == json::parse_fault::unexpected_end);
    CHECK(fault_of("[1,]") == json::parse_fault::unexpected_character);
    CHECK(fault_of("[1 2]") == json::parse_fault::unexpected_character);
    CHECK(fault_of("{\"a\" 1}") == json::parse_fault::unexpected_character);
    CHECK(fault_of("{\"a\": 1,}") == json::parse_fault::unexpected_character);
    CHECK(fault_of("{a: 1}") == json::parse_fault::unexpected_character);
    CHECK(fault_of("{\"a\"") == json::parse_fault::unexpected_end);
    CHECK(fault_of("\"open") == json::parse_fault::unexpected_end);
    CHECK(fault_of("tru") == json::parse_fault::invalid_literal);
    CHECK(fault_of("nul") == json::parse_fault::invalid_literal);
    CHECK(fault_of("1 2") == json::parse_fault::trailing_characters);
    CHECK(fault_of("{} x") == json::parse_fault::trailing_characters);
}

TEST_CASE("json parser reports where the input went wrong", "[json]") {
    const auto result = json::parse("{\n  \"a\": 1,\n  \"b\": ?\n}");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().fault == json::parse_fault::unexpected_character);
    CHECK(result.error().offset == 19);
    CHECK(result.error().line == 3);
    CHECK(result.error().column == 8);
    CHECK(json::describe(result.error()) == "line 3, column 8: unexpected character");
}

TEST_CASE("json parser keeps the last of two members with one key", "[json]") {
    const auto root = parsed(R"({"a": 1, "b": 2, "a": 3})");

    REQUIRE(root.as_object() != nullptr);
    CHECK(root.as_object()->size() == 2);
    CHECK(root.find("a")->as_integer() == 3);
}

TEST_CASE("json parser skips a byte order mark", "[json]") {
    CHECK(parsed("\xEF\xBB\xBF[1]") == json::value{json::array{1}});
    CHECK(fault_of("\xEF\xBB\xBF") == json::parse_fault::unexpected_end);
}

TEST_CASE("json parser stops at the depth limit instead of the stack", "[json]") {
    const std::string at_limit   = std::string(16, '[') + std::string(16, ']');
    const std::string past_limit = std::string(17, '[') + std::string(17, ']');

    CHECK(json::parse(at_limit, {.max_depth = 16}).has_value());

    const auto refused = json::parse(past_limit, {.max_depth = 16});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().fault == json::parse_fault::depth_limit_exceeded);
    CHECK(refused.error().offset == 16);

    const std::string bottomless(200'000, '[');
    CHECK(fault_of(bottomless) == json::parse_fault::depth_limit_exceeded);
}

TEST_CASE("json dump writes compact text by default", "[json]") {
    const json::value root{json::object{
        {"node", "head"},
        {"position", json::array{0, 9.5, -1}},
        {"visible", true},
        {"parent", nullptr},
    }};

    const std::string expected =
        R"({"node":"head","position":[0,9.5,-1],"visible":true,"parent":null})";

    CHECK(json::dump(root) == expected);
}

TEST_CASE("json dump indents nested containers and keeps empty ones closed", "[json]") {
    const json::value root{json::object{
        {"items", json::array{1, json::object{{"a", true}}}},
        {"none", json::array{}},
        {"empty", json::object{}},
    }};

    const std::string expected =
        "{\n"
        "  \"items\": [\n"
        "    1,\n"
        "    {\n"
        "      \"a\": true\n"
        "    }\n"
        "  ],\n"
        "  \"none\": [],\n"
        "  \"empty\": {}\n"
        "}";

    CHECK(json::dump(root, {.indent = 2}) == expected);
}

TEST_CASE("json dump keeps a real a real", "[json]") {
    CHECK(json::dump(json::value{1.0}) == "1.0");
    CHECK(json::dump(json::value{-0.5}) == "-0.5");
    CHECK(json::dump(json::value{1e300}) == "1e+300");
    CHECK(json::dump(json::value{int64{1}}) == "1");
    CHECK(parsed(json::dump(json::value{3.0})).is_real());
}

TEST_CASE("json dump may write an exponent, and the parser reads it back", "[json]") {
    CHECK(json::dump(json::value{100000.0}) == "1e+05");
    CHECK(json::dump(json::value{12345.0}) == "12345.0");
    CHECK(json::dump(json::value{1e-7}) == "1e-07");
    CHECK(json::dump(json::value{0.001}) == "0.001");

    const std::array<float64, 5> samples{100000.0, 1e-7, 1e21, -2.5e-12, 6.02214076e23};

    for (const float64 sample : samples) {
        const auto back = parsed(json::dump(json::value{sample}));
        CHECK(back.is_real());
        CHECK(back.as_number() == sample);
    }
}

TEST_CASE("json dump writes the shortest text that reads back exactly", "[json]") {
    const std::array<float64, 6> samples{
        0.1, 1.0 / 3.0, 9.5, 1.5707963267948966, 5e-324, 1.7976931348623157e308,
    };

    for (const float64 sample : samples) {
        const auto back = parsed(json::dump(json::value{sample}));
        CHECK(back.as_number() == sample);
    }

    CHECK(json::dump(json::value{0.1}) == "0.1");
    CHECK(json::dump(json::value{0.1F}) != "0.1");
}

TEST_CASE("json dump has no text for a number that is not finite", "[json]") {
    CHECK(json::dump(json::value{std::numeric_limits<float64>::quiet_NaN()}) == "null");
    CHECK(json::dump(json::value{std::numeric_limits<float64>::infinity()}) == "null");
}

TEST_CASE("json dump escapes what a string cannot hold raw", "[json]") {
    const json::value text{"\" \\ / \b\f\n\r\t \x01 \x1F"};

    const std::string expected = in_quotes(
        "\\\" \\\\ / \\b\\f\\n\\r\\t " + code_units({"0001"}) + " " + code_units({"001f"})
    );

    CHECK(json::dump(text) == expected);
}

TEST_CASE("json dump passes UTF-8 through and replaces bytes that are not", "[json]") {
    CHECK(json::dump(json::value{"\xD0\xB3\xD0\xBE"}) == "\"\xD0\xB3\xD0\xBE\"");
    CHECK(json::dump(json::value{"a\xFF" "b"}) == "\"a\xEF\xBF\xBD" "b\"");
    CHECK(parsed(json::dump(json::value{"\xC3"})).as_string() != nullptr);
}

TEST_CASE("json text survives a round trip through dump and parse", "[json]") {
    const json::value root{json::object{
        {"name", "p_humanoid"},
        {"rig", "humanoid"},
        {"nodes",
         json::array{
             json::object{{"name", "head"}, {"transform", json::array{0, 9.5, 0}}},
             json::object{{"name", "body"}, {"model", nullptr}},
         }},
        {"count", int64{6}},
        {"scale", 0.99},
        {"escaped", "tab\there \"quoted\""},
    }};

    CHECK(parsed(json::dump(root)) == root);
    CHECK(parsed(json::dump(root, {.indent = 4})) == root);
}

TEST_CASE("json cursor reads typed values along a path", "[json]") {
    const auto root = parsed(
        R"({"node": "head", "position": [1, 2.5, 3], "visible": true, "count": 6})"
    );
    const json::cursor at{root};

    CHECK(at["node"].string() == std::string_view{"head"});
    CHECK(at["visible"].boolean() == true);
    CHECK(at["count"].integer() == 6);
    CHECK(at["count"].number() == 6.0);
    CHECK(at["position"][1].number() == 2.5);
    CHECK(at["position"][0].integer() == 1);
}

TEST_CASE("json cursor names the path and the kinds when the type is wrong", "[json]") {
    const auto root = parsed(R"({"arguments": {"position": [1, "two", 3]}})");
    const json::cursor at{root};

    const auto wrong = at["arguments"]["position"][1].integer();

    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().path == "$.arguments.position[1]");
    CHECK(wrong.error().message == "expected integer, found string");
    CHECK(json::describe(wrong.error()) ==
          "$.arguments.position[1]: expected integer, found string");
}

TEST_CASE("json cursor says what is missing instead of failing early", "[json]") {
    const auto root = parsed(R"({"arguments": {}})");
    const json::cursor at{root};

    const auto absent = at["arguments"]["node"]["name"];
    const auto read   = absent.string();

    CHECK_FALSE(absent.exists());
    CHECK(absent.get() == nullptr);
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().path == "$.arguments.node.name");
    CHECK(read.error().message == "missing, expected string");

    CHECK_FALSE(at["arguments"][3].exists());
    CHECK(at["arguments"].exists());
}

TEST_CASE("json cursor does not read a real as an integer", "[json]") {
    const auto root = parsed(R"({"count": 1.5})");

    const auto read = json::cursor{root}["count"].integer();

    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().message == "expected integer, found real");
}

TEST_CASE("json cursor walks elements and fields with their paths", "[json]") {
    const auto root = parsed(R"({"keys": [{"time": 0}, {"time": 1.2}], "rig": "humanoid"})");
    const json::cursor at{root, "params"};

    const auto keys = at["keys"].elements();
    REQUIRE(keys.has_value());
    REQUIRE(keys->size() == 2);
    CHECK((*keys)[1].path() == "params.keys[1]");
    CHECK((*keys)[1]["time"].number() == 1.2);

    const auto fields = at.fields();
    REQUIRE(fields.has_value());
    REQUIRE(fields->size() == 2);
    CHECK((*fields)[0].first == "keys");
    CHECK((*fields)[1].first == "rig");
    CHECK((*fields)[1].second.path() == "params.rig");

    const auto not_array = at["rig"].elements();
    REQUIRE_FALSE(not_array.has_value());
    CHECK(not_array.error().message == "expected array, found string");

    const auto not_object = at["keys"].fields();
    REQUIRE_FALSE(not_object.has_value());
    CHECK(not_object.error().message == "expected object, found array");
}
