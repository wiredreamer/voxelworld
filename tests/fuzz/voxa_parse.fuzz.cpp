import std;

import vw.core;
import vw.asset;

namespace {

volatile vw::uint32 sink = 0;

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const vw::uint8* data, std::size_t size) -> int {
    static const bool log_silenced = [] {
        vw::log::set_level(vw::log::level::off);
        return true;
    }();
    static_cast<void>(log_silenced);

    std::istringstream stream(std::string(reinterpret_cast<const char*>(data), size));

    vw::asset::voxa_deserializer deserializer;
    const auto result = deserializer.deserialize(stream);

    if (result.has_value() && *result) {
        sink = static_cast<vw::uint32>((*result)->get_tracks().size());
    }

    return 0;
}
