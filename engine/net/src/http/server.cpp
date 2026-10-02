module vw.net;

import std;

import vw.core;

namespace vw::net::http {

namespace {

constexpr log::log_category lc_{"http"};

constexpr std::chrono::milliseconds accept_tick{200};
constexpr std::chrono::milliseconds reply_tick{20};

constexpr std::string_view head_end = "\r\n\r\n";

auto send_text(
    tcp_stream& stream, std::string_view text
) -> void {
    static_cast<void>(stream.send_all(std::as_bytes(std::span{text})));
}

class connection final {
public:
    connection(
        tcp_stream stream, const server_options& options, std::stop_token stop
    )
        : stream_(std::move(stream)), options_(&options), stop_(std::move(stop)) {}

    [[nodiscard]] auto read_request() -> std::expected<request, response> {
        const auto deadline = std::chrono::steady_clock::now() + options_->read_timeout;

        std::size_t head_size = std::string::npos;
        while (head_size == std::string::npos) {
            if (buffer_.size() > options_->max_head_bytes) {
                return std::unexpected(plain(431, "request head is too large"));
            }
            if (!receive_more_(deadline)) {
                return std::unexpected(plain(400, "incomplete request"));
            }
            head_size = buffer_.find(head_end);
        }

        auto head = parse_head(std::string_view{buffer_}.substr(0, head_size));
        if (!head) {
            return std::unexpected(plain(400, "malformed request head"));
        }

        if (options_->only_local_callers) {
            const auto host = head->value_of("host");
            if (!host.empty() && !is_local_authority(host)) {
                return std::unexpected(plain(403, "only local hosts are served"));
            }
            const auto origin = head->value_of("origin");
            if (!origin.empty() && !is_local_origin(origin)) {
                return std::unexpected(plain(403, "only local origins are served"));
            }
        }
        if (!head->value_of("transfer-encoding").empty()) {
            return std::unexpected(plain(411, "send Content-Length, chunked bodies are not read"));
        }

        std::size_t body_size = 0;
        if (const auto length = head->value_of("content-length"); !length.empty()) {
            const auto* end   = length.data() + length.size();
            const auto parsed = std::from_chars(length.data(), end, body_size);
            if (parsed.ec != std::errc{} || parsed.ptr != end) {
                return std::unexpected(plain(400, "malformed Content-Length"));
            }
        }
        if (body_size > options_->max_body_bytes) {
            return std::unexpected(plain(413, "request body is too large"));
        }

        if (lowered(head->value_of("expect")) == "100-continue") {
            send_text(stream_, "HTTP/1.1 100 Continue\r\n\r\n");
        }

        const std::size_t body_from = head_size + head_end.size();
        while (buffer_.size() < body_from + body_size) {
            if (!receive_more_(deadline)) {
                return std::unexpected(plain(400, "incomplete request body"));
            }
        }

        head->body = buffer_.substr(body_from, body_size);
        return std::move(*head);
    }

    auto respond(
        const response& answer
    ) -> void {
        send_text(stream_, serialize(answer));
        stream_.finish_sending();
    }

private:
    [[nodiscard]] auto receive_more_(
        std::chrono::steady_clock::time_point deadline
    ) -> bool {
        while (!stream_.wait_readable(accept_tick)) {
            if (stop_.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
        }

        std::array<char, 8192> chunk{};
        const auto received = stream_.receive(std::as_writable_bytes(std::span{chunk}));
        if (!received || *received == 0) {
            return false;
        }
        buffer_.append(chunk.data(), *received);
        return true;
    }

    tcp_stream stream_;
    const server_options* options_;
    std::stop_token stop_;
    std::string buffer_;
};

[[nodiscard]] auto describe_failure(
    const socket_error& error, const endpoint& local
) -> std::string {
    if (error.is_address_in_use) {
        return std::format("port {} is taken", local.port);
    }
    if (error.step == socket_step::start) {
        return describe(error);
    }
    return std::format("{} on port {}", describe(error), local.port);
}

}  // namespace

struct server::impl {
    explicit impl(
        const server_options& serving
    )
        : options(serving) {}

    server_options options;
    std::optional<tcp_listener> listening;
    endpoint local;
    std::string failure;

    std::mutex mutex;
    std::vector<std::shared_ptr<exchange>> pending;

    std::jthread worker;

    auto serve(
        const std::stop_token& stop
    ) -> void {
        while (!stop.stop_requested()) {
            if (!listening->wait_readable(accept_tick)) {
                continue;
            }

            auto client = listening->accept();
            if (!client) {
                continue;
            }
            answer(std::move(*client), stop);
        }
    }

    auto answer(
        tcp_stream client, const std::stop_token& stop
    ) -> void {
        connection peer{std::move(client), options, stop};

        auto asked = peer.read_request();
        if (!asked) {
            peer.respond(asked.error());
            return;
        }

        const auto waiting = std::make_shared<exchange>(std::move(*asked));

        {
            const std::scoped_lock lock{mutex};
            pending.push_back(waiting);
        }

        const auto deadline = std::chrono::steady_clock::now() + options.reply_timeout;
        while (true) {
            if (auto reply = waiting->wait_answer_(reply_tick)) {
                peer.respond(*reply);
                return;
            }
            if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                waiting->abandon_();
                peer.respond(plain(504, "the request was not answered in time"));
                return;
            }
        }
    }
};

exchange::exchange(
    request received
)
    : received_(std::move(received)) {}

auto exchange::received() const -> const request& {
    return received_;
}

auto exchange::is_abandoned() const -> bool {
    return abandoned_;
}

auto exchange::answer(
    response reply
) -> void {
    {
        const std::scoped_lock lock{mutex_};
        reply_ = std::move(reply);
    }
    answered_.notify_one();
}

auto exchange::wait_answer_(
    std::chrono::milliseconds timeout
) -> std::optional<response> {
    std::unique_lock lock{mutex_};
    answered_.wait_for(lock, timeout, [this] { return reply_.has_value(); });
    return std::exchange(reply_, std::nullopt);
}

auto exchange::abandon_() -> void {
    abandoned_ = true;
}

server::server(
    const server_options& options
)
    : impl_(std::make_unique<impl>(options)) {
    auto listening = tcp_listener::open(options.local);
    if (!listening) {
        impl_->failure = describe_failure(listening.error(), options.local);
        return;
    }

    impl_->local = listening->local_endpoint();
    impl_->listening.emplace(std::move(*listening));
    impl_->worker =
        std::jthread{[state = impl_.get()](const std::stop_token& stop) { state->serve(stop); }};

    log::info(lc_, "listening on {}", to_string(impl_->local));
}

server::~server() {
    if (impl_->worker.joinable()) {
        impl_->worker.request_stop();
        impl_->worker.join();
    }
}

auto server::is_listening() const -> bool {
    return impl_->listening.has_value();
}

auto server::failure() const -> const std::string& {
    return impl_->failure;
}

auto server::local_endpoint() const -> endpoint {
    return impl_->local;
}

auto server::take_pending() -> std::vector<std::shared_ptr<exchange>> {
    const std::scoped_lock lock{impl_->mutex};
    return std::exchange(impl_->pending, {});
}

}  // namespace vw::net::http
