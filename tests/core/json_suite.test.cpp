#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

using vw::uint32;

namespace {

auto read_bytes(const std::filesystem::path& path) -> std::string {
    std::ifstream file(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("json parser agrees with JSONTestSuite", "[json][suite]") {
    const std::filesystem::path suite{VW_JSON_SUITE_DIR};
    REQUIRE(std::filesystem::is_directory(suite));

    uint32 accepted_cases = 0;
    uint32 rejected_cases = 0;
    uint32 free_cases     = 0;

    for (const auto& entry : std::filesystem::directory_iterator{suite}) {
        const auto name = entry.path().filename().string();
        const auto text = read_bytes(entry.path());

        INFO(name);

        const auto parsed = vw::json::parse(text);

        if (name.starts_with("y_")) {
            ++accepted_cases;
            REQUIRE(parsed.has_value());

            const auto reparsed = vw::json::parse(vw::json::dump(*parsed));
            REQUIRE(reparsed.has_value());
            CHECK(*reparsed == *parsed);
        } else if (name.starts_with("n_")) {
            ++rejected_cases;
            CHECK_FALSE(parsed.has_value());
        } else {
            ++free_cases;
            if (parsed.has_value()) {
                const auto reparsed = vw::json::parse(vw::json::dump(*parsed));
                REQUIRE(reparsed.has_value());
                CHECK(*reparsed == *parsed);
            }
        }
    }

    CHECK(accepted_cases == 95);
    CHECK(rejected_cases == 188);
    CHECK(free_cases == 35);
}
