import std;

import vw.core;
import vw.asset;

namespace {

volatile vw::uint32 sink = 0;

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const vw::uint8* data, std::size_t size) -> int {
    static const vw::voxel_registry registry;

    static const bool log_silenced = [] {
        vw::log::set_level(vw::log::level::off);
        return true;
    }();
    static_cast<void>(log_silenced);

    std::istringstream stream(std::string(reinterpret_cast<const char*>(data), size));

    vw::asset::vox_parser_plain parser;
    const auto result = parser.parse(stream);

    if (result.has_value()) {
        sink = static_cast<vw::uint32>(result->entities.size());
    }

    return 0;
}
