#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

import std;
import vw.core;
import vw.net;

using namespace vw;

TEST_CASE(
    "an address is read from dotted decimal text", "[net][endpoint]"
) {
    CHECK(net::parse_ipv4("127.0.0.1") == net::ipv4_address::loopback());
    CHECK(net::parse_ipv4("0.0.0.0") == net::ipv4_address::any());
    CHECK(net::parse_ipv4("192.168.1.255") == net::ipv4_address{{192, 168, 1, 255}});
}

TEST_CASE(
    "text that is not an address is refused", "[net][endpoint]"
) {
    const std::string_view refused = GENERATE(
        std::string_view{""},
        std::string_view{"127.0.0"},
        std::string_view{"127.0.0.1.5"},
        std::string_view{"256.0.0.1"},
        std::string_view{"127.0.0.01"},
        std::string_view{"127..0.1"},
        std::string_view{"a.b.c.d"},
        std::string_view{"127.0.0.1 "},
        std::string_view{"-1.0.0.1"},
        std::string_view{"localhost"}
    );

    CAPTURE(refused);
    CHECK_FALSE(net::parse_ipv4(refused).has_value());
}

TEST_CASE(
    "an endpoint is an address and a port", "[net][endpoint]"
) {
    CHECK(net::parse_endpoint("127.0.0.1:17800") == net::loopback(17800));
    CHECK(net::parse_endpoint("0.0.0.0:0") == net::any_address(0));

    CHECK_FALSE(net::parse_endpoint("127.0.0.1").has_value());
    CHECK_FALSE(net::parse_endpoint("127.0.0.1:").has_value());
    CHECK_FALSE(net::parse_endpoint("127.0.0.1:65536").has_value());
    CHECK_FALSE(net::parse_endpoint(":80").has_value());
}

TEST_CASE(
    "an endpoint written as text reads back the same", "[net][endpoint]"
) {
    const net::endpoint at{.address = net::ipv4_address{{10, 0, 200, 7}}, .port = 4242};

    CHECK(net::to_string(at) == "10.0.200.7:4242");
    CHECK(net::parse_endpoint(net::to_string(at)) == at);
}

TEST_CASE(
    "the whole 127 network is loopback", "[net][endpoint]"
) {
    CHECK(net::ipv4_address::loopback().is_loopback());
    CHECK(net::ipv4_address{{127, 5, 5, 5}}.is_loopback());
    CHECK_FALSE(net::ipv4_address::any().is_loopback());
    CHECK_FALSE(net::ipv4_address{{192, 168, 0, 1}}.is_loopback());
}
