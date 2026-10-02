#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;
import vw.net;

using namespace vw;

namespace {

constexpr std::chrono::milliseconds patience{2000};

[[nodiscard]] auto bytes_of(
    std::string_view text
) -> std::span<const std::byte> {
    return std::as_bytes(std::span{text});
}

[[nodiscard]] auto read_exactly(
    net::tcp_stream& stream, std::size_t count
) -> std::string {
    std::string text;
    while (text.size() < count && stream.wait_readable(patience)) {
        std::array<char, 256> chunk{};
        const auto received = stream.receive(std::as_writable_bytes(std::span{chunk}));
        if (!received || *received == 0) {
            break;
        }
        text.append(chunk.data(), *received);
    }
    return text;
}

}  // namespace

TEST_CASE(
    "a listener on port zero is given a port", "[net][socket]"
) {
    auto listener = net::tcp_listener::open(net::loopback(0));
    REQUIRE(listener.has_value());

    const net::endpoint local = listener->local_endpoint();
    CHECK(local.address == net::ipv4_address::loopback());
    CHECK(local.port != 0);
}

TEST_CASE(
    "a port that is listened on cannot be taken again", "[net][socket]"
) {
    auto first = net::tcp_listener::open(net::loopback(0));
    REQUIRE(first.has_value());

    auto second = net::tcp_listener::open(first->local_endpoint());
    REQUIRE_FALSE(second.has_value());

    CHECK(second.error().step == net::socket_step::bind);
    CHECK(second.error().is_address_in_use);
    CHECK(net::describe(second.error()) == "the address is taken");
}

TEST_CASE(
    "bytes cross a stream both ways", "[net][socket]"
) {
    auto listener = net::tcp_listener::open(net::loopback(0));
    REQUIRE(listener.has_value());

    auto client = net::tcp_stream::connect(listener->local_endpoint());
    REQUIRE(client.has_value());

    REQUIRE(listener->wait_readable(patience));
    auto served = listener->accept();
    REQUIRE(served.has_value());

    REQUIRE(client->send_all(bytes_of("ping")).has_value());
    CHECK(read_exactly(*served, 4) == "ping");

    REQUIRE(served->send_all(bytes_of("pong!")).has_value());
    CHECK(read_exactly(*client, 5) == "pong!");
}

TEST_CASE(
    "a stream that finished sending reads as closed", "[net][socket]"
) {
    auto listener = net::tcp_listener::open(net::loopback(0));
    REQUIRE(listener.has_value());

    auto client = net::tcp_stream::connect(listener->local_endpoint());
    REQUIRE(client.has_value());
    REQUIRE(listener->wait_readable(patience));
    auto served = listener->accept();
    REQUIRE(served.has_value());

    client->finish_sending();

    REQUIRE(served->wait_readable(patience));
    std::array<std::byte, 16> chunk{};
    const auto received = served->receive(chunk);
    REQUIRE(received.has_value());
    CHECK(*received == 0);
}

TEST_CASE(
    "nobody listening refuses a connection", "[net][socket]"
) {
    net::endpoint vacated;
    {
        auto listener = net::tcp_listener::open(net::loopback(0));
        REQUIRE(listener.has_value());
        vacated = listener->local_endpoint();
    }

    const auto client = net::tcp_stream::connect(vacated);
    REQUIRE_FALSE(client.has_value());
    CHECK(client.error().step == net::socket_step::connect);
}

TEST_CASE(
    "a listener with nobody calling has nothing to read", "[net][socket]"
) {
    auto listener = net::tcp_listener::open(net::loopback(0));
    REQUIRE(listener.has_value());

    CHECK_FALSE(listener->wait_readable(std::chrono::milliseconds{20}));
}

TEST_CASE(
    "a datagram arrives whole and names its sender", "[net][socket]"
) {
    auto receiver = net::udp_socket::open(net::loopback(0));
    auto sender   = net::udp_socket::open(net::loopback(0));
    REQUIRE(receiver.has_value());
    REQUIRE(sender.has_value());

    const auto sent = sender->send_to(receiver->local_endpoint(), bytes_of("state 42"));
    REQUIRE(sent.has_value());
    CHECK(*sent == 8);

    REQUIRE(receiver->wait_readable(patience));
    std::array<char, 64> chunk{};
    const auto arrived = receiver->receive_from(std::as_writable_bytes(std::span{chunk}));
    REQUIRE(arrived.has_value());

    CHECK(std::string_view{chunk.data(), arrived->size} == "state 42");
    CHECK(arrived->sender == sender->local_endpoint());
}

TEST_CASE(
    "datagrams keep their boundaries", "[net][socket]"
) {
    auto receiver = net::udp_socket::open(net::loopback(0));
    auto sender   = net::udp_socket::open(net::loopback(0));
    REQUIRE(receiver.has_value());
    REQUIRE(sender.has_value());

    REQUIRE(sender->send_to(receiver->local_endpoint(), bytes_of("one")).has_value());
    REQUIRE(sender->send_to(receiver->local_endpoint(), bytes_of("three")).has_value());

    std::vector<std::string> arrived;
    while (arrived.size() < 2 && receiver->wait_readable(patience)) {
        std::array<char, 64> chunk{};
        const auto packet = receiver->receive_from(std::as_writable_bytes(std::span{chunk}));
        REQUIRE(packet.has_value());
        arrived.emplace_back(chunk.data(), packet->size);
    }

    CHECK(arrived == std::vector<std::string>{"one", "three"});
}

TEST_CASE(
    "a moved-from socket leaves the port to its new owner", "[net][socket]"
) {
    auto listener = net::tcp_listener::open(net::loopback(0));
    REQUIRE(listener.has_value());
    const net::endpoint local = listener->local_endpoint();

    net::tcp_listener moved = std::move(*listener);
    CHECK(moved.local_endpoint() == local);

    auto client = net::tcp_stream::connect(local);
    CHECK(client.has_value());
}
