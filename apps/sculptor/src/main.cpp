#include <sculptor_version.h>

import std;

import vw.core;
import vw.gfx;
import vw.sculptor;

auto main(int argc, char** argv) -> int {
    const std::vector<std::string_view> arguments(argv + std::min(argc, 1), argv + argc);

    const auto options = vw::sculptor::parse_launch_options(arguments);
    if (!options) {
        std::println(std::cerr, "{}\n{}", options.error(), vw::sculptor::launch_usage());
        return 2;
    }
    if (options->show_usage) {
        std::println("{}", vw::sculptor::launch_usage());
        return 0;
    }

    try {
        const auto title =
            vw::build::titled(std::format("Sculptor {}", vw::sculptor::version_string));
        vw::gfx::engine{1800, 1200, title}.run<vw::sculptor::app>(*options);
    } catch (const std::exception& e) {
        vw::log::error("Ошибка выполнения: {}", e.what());
    }
    return 0;
}
