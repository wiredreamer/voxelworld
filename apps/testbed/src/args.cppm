export module vw.testbed:args;

import std;

import vw.core;

export namespace vw::testbed {

class arg_reader {
public:
    arg_reader(int argc, char** argv) {
        args_.reserve(static_cast<std::size_t>(std::max(argc - 1, 0)));
        for (int i = 1; i < argc; ++i) {
            args_.emplace_back(argv[i]);
        }
    }

    [[nodiscard]] auto flag(std::string_view name) const -> bool {
        return std::ranges::find(args_, name) != args_.end();
    }

    [[nodiscard]] auto text(std::string_view name) const -> std::optional<std::string_view> {
        for (const auto arg : args_) {
            if (arg.starts_with(name) && arg.size() > name.size() && arg[name.size()] == '=') {
                return arg.substr(name.size() + 1);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto integer(std::string_view name, int32 fallback) const -> int32 {
        return static_cast<int32>(count(name, static_cast<uint32>(std::max(fallback, 0))));
    }

    [[nodiscard]] auto count(std::string_view name, uint32 fallback) const -> uint32 {
        const auto value = text(name);
        if (!value) {
            return fallback;
        }

        uint32 parsed   = 0;
        const auto* end = value->data() + value->size();
        const auto done = std::from_chars(value->data(), end, parsed);

        return (done.ec == std::errc{} && done.ptr == end) ? parsed : fallback;
    }

    [[nodiscard]] auto real(std::string_view name, float32 fallback) const -> float32 {
        const auto value = text(name);
        if (!value) {
            return fallback;
        }

        float32 parsed  = 0.0F;
        const auto* end = value->data() + value->size();
        const auto done = std::from_chars(value->data(), end, parsed);

        return (done.ec == std::errc{} && done.ptr == end) ? parsed : fallback;
    }

private:
    std::vector<std::string_view> args_;
};

}  // namespace vw::testbed
