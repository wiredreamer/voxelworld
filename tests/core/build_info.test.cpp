#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

TEST_CASE("the build names its configuration", "[build_info]") {
    CHECK_FALSE(vw::build::config.empty());
    CHECK(vw::build::config.find(' ') == std::string_view::npos);
}

TEST_CASE("a title carries the configuration of the build", "[build_info]") {
    const std::string expected = std::format("Sculptor 0.2.0 [{}]", vw::build::config);

    CHECK(vw::build::titled("Sculptor 0.2.0") == expected);
}
