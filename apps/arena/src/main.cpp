import std;

import vw.core;
import vw.gfx;
import vw.arena;

auto main() -> int {
    try {
        vw::log::add_file_sink("arena.log");
        std::make_unique<vw::gfx::engine>(1280, 720, vw::build::titled("Voxel World - Arena"))
            ->run<vw::arena::arena_app>();
    } catch (const std::exception& e) {
        vw::log::error("Error: {}", e.what());
        return 1;
    }

    return 0;
}
