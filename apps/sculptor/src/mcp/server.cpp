module vw.sculptor;

import std;

import vw.core;

namespace vw::sculptor {

struct mcp_server::impl {
    impl(uint16 listening_port, const mcp_bindings& bindings)
        : port(listening_port),
          dispatcher(make_editor_tools(bindings)),
          listener(listening_port) {}

    struct waiting_call {
        std::shared_ptr<http_exchange> exchange;
        std::function<std::optional<http_response>()> later;
    };

    uint16 port;
    mcp_dispatcher dispatcher;
    http_listener listener;
    std::vector<waiting_call> waiting;
};

auto mcp_server::compiled_in() -> bool {
    return true;
}

mcp_server::mcp_server(uint16 port, const mcp_bindings& bindings)
    : impl_(std::make_unique<impl>(port, bindings)) {}

mcp_server::~mcp_server() = default;

auto mcp_server::poll() -> void {
    std::erase_if(impl_->waiting, [](impl::waiting_call& call) {
        if (call.exchange->abandoned) {
            return true;
        }

        auto answered = call.later();
        if (!answered) {
            return false;
        }
        call.exchange->reply.set_value(std::move(*answered));
        return true;
    });

    for (const auto& exchange : impl_->listener.take_pending()) {
        if (exchange->abandoned) {
            continue;
        }

        auto result = impl_->dispatcher.handle(exchange->request);
        if (result.ready) {
            exchange->reply.set_value(std::move(*result.ready));
            continue;
        }
        impl_->waiting.push_back(
            impl::waiting_call{.exchange = exchange, .later = std::move(result.later)}
        );
    }
}

auto mcp_server::status() const -> mcp_status {
    return mcp_status{
        .enabled   = true,
        .listening = impl_->listener.is_listening(),
        .port      = impl_->port,
        .requests  = impl_->dispatcher.request_count(),
        .last_tool = impl_->dispatcher.last_tool(),
        .failure   = impl_->listener.failure(),
    };
}

}  // namespace vw::sculptor
