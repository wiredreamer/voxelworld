module vw.net;

import std;

import vw.core;

namespace vw::net::http {

namespace {

constexpr std::string_view line_end = "\r\n";

[[nodiscard]] auto reason_of(
    uint32 status
) -> std::string_view {
    switch (status) {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 411:
            return "Length Required";
        case 413:
            return "Content Too Large";
        case 431:
            return "Request Header Fields Too Large";
        case 504:
            return "Gateway Timeout";
        default:
            return "Error";
    }
}

[[nodiscard]] auto trimmed(
    std::string_view text
) -> std::string_view {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

}  // namespace

auto lowered(
    std::string_view text
) -> std::string {
    std::string out{text};
    for (char& symbol : out) {
        if (symbol >= 'A' && symbol <= 'Z') {
            symbol = static_cast<char>(symbol - 'A' + 'a');
        }
    }
    return out;
}

auto request::value_of(
    std::string_view lowercase_name
) const -> std::string_view {
    const auto found = fields.find(lowercase_name);
    return found == fields.end() ? std::string_view{} : std::string_view{found->second};
}

auto plain(
    uint32 status, std::string_view message
) -> response {
    return response{
        .status       = status,
        .content_type = "text/plain; charset=utf-8",
        .fields       = {},
        .body         = std::string{message},
    };
}

auto parse_head(
    std::string_view head
) -> std::optional<request> {
    request parsed;

    bool first_line = true;
    for (const auto line_range : std::views::split(head, line_end)) {
        const std::string_view line{line_range.begin(), line_range.end()};

        if (first_line) {
            first_line = false;

            const auto method_end = line.find(' ');
            const auto target_end = line.rfind(' ');
            if (method_end == std::string_view::npos || target_end <= method_end) {
                return std::nullopt;
            }
            parsed.method = line.substr(0, method_end);
            parsed.target = trimmed(line.substr(method_end + 1, target_end - method_end - 1));
            if (parsed.method.empty() || parsed.target.empty()) {
                return std::nullopt;
            }
            continue;
        }

        if (line.empty()) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        parsed.fields[lowered(trimmed(line.substr(0, colon)))] = trimmed(line.substr(colon + 1));
    }

    return first_line ? std::nullopt : std::optional{std::move(parsed)};
}

auto serialize(
    const response& answer
) -> std::string {
    std::string text = std::format("HTTP/1.1 {} {}\r\n", answer.status, reason_of(answer.status));
    if (!answer.content_type.empty()) {
        text += std::format("Content-Type: {}\r\n", answer.content_type);
    }
    for (const auto& [name, value] : answer.fields) {
        text += std::format("{}: {}\r\n", name, value);
    }
    text += std::format("Content-Length: {}\r\n", answer.body.size());
    text += "Connection: close\r\n\r\n";
    text += answer.body;
    return text;
}

auto is_local_authority(
    std::string_view authority
) -> bool {
    std::string_view host = authority;
    if (host.starts_with('[')) {
        const auto closing = host.find(']');
        host               = closing == std::string_view::npos ? host : host.substr(0, closing + 1);
    } else if (const auto colon = host.rfind(':'); colon != std::string_view::npos) {
        host = host.substr(0, colon);
    }

    const std::string name = lowered(host);
    return name == "127.0.0.1" || name == "localhost" || name == "[::1]";
}

auto is_local_origin(
    std::string_view origin
) -> bool {
    for (const std::string_view scheme : {"http://", "https://"}) {
        if (origin.starts_with(scheme)) {
            return is_local_authority(origin.substr(scheme.size()));
        }
    }
    return false;
}

}  // namespace vw::net::http
