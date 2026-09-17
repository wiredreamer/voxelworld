module vw.gfx;

import std;
import vw.core;

namespace vw::gfx {
namespace {

auto indent(std::string& out, int32 depth) -> void {
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
}

auto quote(std::string& out, std::string_view text) -> void {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::format_to(std::back_inserter(out), "\\u{:04x}",
                                   static_cast<unsigned>(static_cast<unsigned char>(c)));
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

}  // namespace

auto report_section::write_text(std::string& out, int32 depth) const -> void {
    indent(out, depth);
    out.append(name_);
    out.append(":\n");

    for (const auto& item : entries_) {
        indent(out, depth + 1);
        std::format_to(std::back_inserter(out), "{}: {}\n", item.key, item.text);
    }

    for (const auto& child : subs_) {
        child.write_text(out, depth + 1);
    }
}

auto report_section::write_json(std::string& out, int32 depth) const -> void {
    indent(out, depth);
    quote(out, name_);
    out.append(": {\n");

    bool first = true;
    for (const auto& item : entries_) {
        if (!first) {
            out.append(",\n");
        }
        first = false;
        indent(out, depth + 1);
        quote(out, item.key);
        out.append(": ");
        if (item.quoted) {
            quote(out, item.text);
        } else {
            out.append(item.text);
        }
    }

    for (const auto& child : subs_) {
        if (!first) {
            out.append(",\n");
        }
        first = false;
        child.write_json(out, depth + 1);
    }

    out.push_back('\n');
    indent(out, depth);
    out.push_back('}');
}

auto report::to_text() const -> std::string {
    std::string out;
    for (const auto& item : sections_) {
        if (item.empty()) {
            continue;
        }
        out.push_back('\n');
        item.write_text(out, 0);
    }
    return out;
}

auto report::to_json() const -> std::string {
    std::string out = "{\n";

    bool first = true;
    for (const auto& item : sections_) {
        if (item.empty()) {
            continue;
        }
        if (!first) {
            out.append(",\n");
        }
        first = false;
        item.write_json(out, 1);
    }

    out.append("\n}\n");
    return out;
}

}  // namespace vw::gfx
