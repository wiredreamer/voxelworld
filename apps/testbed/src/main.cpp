import std;

import vw.core;
import vw.gfx;
import vw.testbed;

using namespace vw;

namespace {

auto bench_from(const testbed::arg_reader& args) -> gfx::bench_config {
    gfx::bench_config bench;

    bench.workers = args.count("--workers", 0);

    if (!args.flag("--bench")) {
        return bench;
    }

    bench.measure_frames      = args.count("--bench-frames", 2000);
    bench.warmup_frames       = args.count("--bench-warmup", 200);
    bench.fixed_delta_seconds = 1.0f / 60.0f;

    if (const auto path = args.text("--bench-out")) {
        bench.report_path = std::string{*path};
    }
    if (const auto path = args.text("--bench-json")) {
        bench.json_path = std::string{*path};
    }

    return bench;
}

auto joined(const std::vector<std::string_view>& names) -> std::string {
    std::string all;
    for (const auto name : names) {
        if (!all.empty()) {
            all += ", ";
        }
        all += name;
    }
    return all;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const testbed::arg_reader args{argc, argv};

    const auto wanted_scene = args.text("--scene").value_or("terrain");

    const auto scene = testbed::find_scene(wanted_scene, args);
    if (!scene) {
        log::error(
            "unknown scene '{}'; known scenes are: {}", wanted_scene,
            joined(testbed::scene_names())
        );
        return 1;
    }

    const bool benching        = args.flag("--bench");
    const auto wanted_camera   = args.text("--camera");

    if (benching && wanted_camera == "free") {
        log::error("--bench needs a camera on rails: a hand-flown path differs every run");
        return 1;
    }

    testbed::camera_factory camera;
    if (wanted_camera || !benching) {
        const auto name = wanted_camera.value_or("free");

        const auto found = testbed::find_camera(name);
        if (!found) {
            log::error(
                "unknown camera '{}'; known cameras are: {}", name,
                joined(testbed::camera_names())
            );
            return 1;
        }

        camera = *found;
    }

    try {
        log::add_file_sink("testbed.log");
        std::make_unique<gfx::engine>(1280, 720, "Voxel World - Testbed", bench_from(args))
            ->run<testbed::testbed_app>(args, *scene, camera);
    } catch (const std::exception& e) {
        log::error("Error: {}", e.what());
        return 1;
    }

    return 0;
}
