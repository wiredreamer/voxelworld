#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

using vw::job_lane;
using vw::job_system;
using vw::uint32;

TEST_CASE("every submitted job runs exactly once") {
    job_system jobs{4};

    constexpr uint32 count = 256;
    std::atomic<uint32> ran{0};

    for (uint32 i = 0; i < count; ++i) {
        jobs.submit(job_lane::terrain, [&ran](uint32) { ran.fetch_add(1); });
    }

    jobs.drain(job_lane::terrain);

    REQUIRE(ran.load() == count);
}

TEST_CASE("the worker index stays inside the pool") {
    constexpr uint32 workers = 3;
    job_system jobs{workers};

    std::atomic<uint32> seen{0};

    for (uint32 i = 0; i < 64; ++i) {
        jobs.submit(job_lane::mesh, [&seen](uint32 index) {
            if (index < workers) {
                seen.fetch_add(1);
            }
        });
    }

    jobs.drain(job_lane::mesh);

    REQUIRE(seen.load() == 64);
}

TEST_CASE("a flooded lane does not starve the others") {
    constexpr uint32 flood = 64;
    job_system jobs{2};

    std::atomic<uint32> dug{0};
    std::atomic<uint32> dug_before_the_mesh{flood};

    for (uint32 i = 0; i < flood; ++i) {
        jobs.submit(job_lane::terrain, [&dug](uint32) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            dug.fetch_add(1);
        });
    }

    jobs.submit(job_lane::mesh, [&dug, &dug_before_the_mesh](uint32) {
        dug_before_the_mesh.store(dug.load());
    });

    jobs.drain(job_lane::mesh);
    const uint32 waited_for = dug_before_the_mesh.load();
    jobs.drain(job_lane::terrain);

    REQUIRE(dug.load() == flood);
    REQUIRE(waited_for < flood / 2);
}

TEST_CASE("draining one lane ignores work queued on another") {
    job_system jobs{2};

    std::atomic<bool> release{false};
    std::atomic<uint32> lit{0};

    jobs.submit(job_lane::terrain, [&release](uint32) {
        while (!release.load()) {
            std::this_thread::yield();
        }
    });
    jobs.submit(job_lane::light, [&lit](uint32) { lit.fetch_add(1); });

    jobs.drain(job_lane::light);
    REQUIRE(lit.load() == 1);

    release.store(true);
    jobs.drain(job_lane::terrain);
}

TEST_CASE("a closed lane takes no more work") {
    job_system jobs{2};

    std::atomic<uint32> ran{0};

    jobs.submit(job_lane::mesh, [&ran](uint32) { ran.fetch_add(1); });
    jobs.close(job_lane::mesh);

    REQUIRE(ran.load() == 1);

    jobs.submit(job_lane::mesh, [&ran](uint32) { ran.fetch_add(1); });
    jobs.drain(job_lane::mesh);

    REQUIRE(ran.load() == 1);
}

TEST_CASE("stopping finishes the work already queued") {
    std::atomic<uint32> ran{0};

    {
        job_system jobs{2};
        for (uint32 i = 0; i < 128; ++i) {
            jobs.submit(job_lane::light, [&ran](uint32) { ran.fetch_add(1); });
        }
        jobs.stop();
    }

    REQUIRE(ran.load() == 128);
}

TEST_CASE("lane statistics count the queue and the peak") {
    job_system jobs{1};

    std::atomic<bool> release{false};

    jobs.submit(job_lane::terrain, [&release](uint32) {
        while (!release.load()) {
            std::this_thread::yield();
        }
    });
    for (uint32 i = 0; i < 8; ++i) {
        jobs.submit(job_lane::terrain, [](uint32) {});
    }

    const auto queued = jobs.get_lane_stats(job_lane::terrain);
    REQUIRE(queued.peak >= 8);

    release.store(true);
    jobs.drain(job_lane::terrain);

    const auto drained = jobs.get_lane_stats(job_lane::terrain);
    REQUIRE(drained.queued == 0);
    REQUIRE(drained.running == 0);
    REQUIRE(drained.peak >= 8);
}

TEST_CASE("a pool with no work asked for still has a worker") {
    REQUIRE(job_system::default_worker_count() >= 1);
    REQUIRE(job_system::default_worker_count() <= job_system::worker_ceiling);
}
