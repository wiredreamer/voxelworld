module vw.sculptor;

import std;

import vw.core;
import vw.net;

namespace vw::sculptor::mcp {

struct server::impl {
    impl(uint16 listening_port, const editor_bindings& bindings)
        : protocol(make_editor_tools(bindings)),
          listener(net::http::server_options{
              .local              = net::loopback(listening_port),
              .only_local_callers = true,
              .max_head_bytes     = 16 * 1024,
              .max_body_bytes     = 16 * 1024 * 1024,
              .read_timeout       = std::chrono::seconds{10},
              .reply_timeout      = std::chrono::seconds{30},
          }),
          port(listening_port) {}

    struct waiting_call {
        std::shared_ptr<net::http::exchange> exchange;
        std::function<std::optional<net::http::response>()> later;
    };

    dispatcher protocol;
    net::http::server listener;
    uint16 port;
    std::vector<waiting_call> waiting;
};

auto server::compiled_in() -> bool {
    return true;
}

server::server(uint16 port, const editor_bindings& bindings)
    : impl_(std::make_unique<impl>(port, bindings)) {}

server::~server() = default;

auto server::poll() -> void {
    std::erase_if(impl_->waiting, [](impl::waiting_call& call) {
        if (call.exchange->is_abandoned()) {
            return true;
        }

        auto answered = call.later();
        if (!answered) {
            return false;
        }
        call.exchange->answer(std::move(*answered));
        return true;
    });

    for (const auto& exchange : impl_->listener.take_pending()) {
        if (exchange->is_abandoned()) {
            continue;
        }

        auto result = impl_->protocol.handle(exchange->received());
        if (result.ready) {
            exchange->answer(std::move(*result.ready));
            continue;
        }
        impl_->waiting.push_back(
            impl::waiting_call{.exchange = exchange, .later = std::move(result.later)}
        );
    }
}

auto server::status() const -> server_status {
    return server_status{
        .enabled   = true,
        .listening = impl_->listener.is_listening(),
        .port      = impl_->listener.is_listening() ? impl_->listener.local_endpoint().port : impl_->port,
        .requests  = impl_->protocol.request_count(),
        .last_tool = impl_->protocol.last_tool(),
        .failure   = impl_->listener.failure(),
    };
}

}  // namespace vw::sculptor::mcp
