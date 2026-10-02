#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

struct tick_journal {
    std::vector<std::string_view> ticks;
    std::vector<float32> removed_charges;
    uint32 added    = 0;
    uint32 shutdown = 0;
    uint32 deleted  = 0;
};

struct charge_component final {
    float32 amount = 0.0F;
};

struct heat_component final {
    float32 degrees = 0.0F;
};

class first_before_system final {
public:
    static constexpr std::string_view system_name = "first_before";

    first_before_system(world&, tick_journal& journal) : journal_{&journal} {}

    auto update(float32) -> void {
        journal_->ticks.push_back(system_name);
    }

private:
    tick_journal* journal_;
};

class second_before_system final {
public:
    static constexpr std::string_view system_name = "second_before";

    second_before_system(world& w, tick_journal& journal) : world_{&w}, journal_{&journal} {}

    auto update(float32) -> void {
        journal_->ticks.push_back(system_name);
        if (pushed_.is_valid()) {
            world_->system<transform_system>().modify(pushed_).translate(vec3f{1.0F, 0.0F, 0.0F});
        }
    }

    auto push(entity ent) -> void {
        pushed_ = ent;
    }

private:
    world* world_;
    tick_journal* journal_;
    entity pushed_ = invalid_entity;
};

class after_system final {
public:
    static constexpr std::string_view system_name = "after";

    after_system(world& w, tick_journal& journal) : world_{&w}, journal_{&journal} {}

    auto update(float32) -> void {
        journal_->ticks.push_back(system_name);
        moved_this_tick_ = world_->changed<transform_component>().size();
    }

    [[nodiscard]] auto moved_this_tick() const -> std::size_t {
        return moved_this_tick_;
    }

private:
    world* world_;
    tick_journal* journal_;
    std::size_t moved_this_tick_ = 0;
};

class charge_system final {
public:
    static constexpr std::string_view system_name = "charge";

    using observed_components = component_list<charge_component>;

    charge_system(world& w, tick_journal& journal) : world_{&w}, journal_{&journal} {}

    ~charge_system() {
        ++journal_->deleted;
    }

    charge_system(const charge_system&)                    = delete;
    auto operator=(const charge_system&) -> charge_system& = delete;

    auto update(float32) -> void {}

    auto shutdown() -> void {
        ++journal_->shutdown;
    }

    auto set_amount(entity ent, float32 amount) -> void {
        world_->get<charge_component>(ent).amount = amount;
        world_->registry().notify_changed<charge_component>(ent);
    }

    template <std::same_as<charge_component> C>
    auto on_add(entity) -> void {
        ++journal_->added;
    }

    template <std::same_as<charge_component> C>
    auto on_remove(entity ent) -> void {
        journal_->removed_charges.push_back(world_->get<charge_component>(ent).amount);
    }

private:
    world* world_;
    tick_journal* journal_;
};

class charge_reader_system final {
public:
    static constexpr std::string_view system_name = "charge_reader";

    explicit charge_reader_system(world& w) : world_{&w} {}

    auto update(float32) -> void {
        seen_ = world_->changed<charge_component>().size();
    }

    [[nodiscard]] auto seen() const -> std::size_t {
        return seen_;
    }

private:
    world* world_;
    std::size_t seen_ = 0;
};

class charge_writer_system final {
public:
    static constexpr std::string_view system_name = "charge_writer";

    explicit charge_writer_system(world& w) : world_{&w} {}

    auto update(float32) -> void {
        if (target_.is_valid()) {
            world_->registry().notify_changed<charge_component>(target_);
        }
    }

    auto aim(entity ent) -> void {
        target_ = ent;
    }

private:
    world* world_;
    entity target_ = invalid_entity;
};

class heat_system final {
public:
    static constexpr std::string_view system_name = "heat";

    using observed_components = component_list<heat_component>;

    explicit heat_system(world&) {}

    auto update(float32) -> void {}

    template <std::same_as<heat_component> C>
    auto on_add(entity) -> void {}
};

}  // namespace

TEST_CASE("added systems tick around the engine in the order they were added", "[world][ext]") {
    tick_journal journal;
    world w;

    auto& after  = w.add_system<after_system>(tick_stage::after_engine, journal);
    auto& first  = w.add_system<first_before_system>(tick_stage::before_engine, journal);
    auto& second = w.add_system<second_before_system>(tick_stage::before_engine, journal);
    static_cast<void>(first);

    const auto ent = w.create().with<transform_component>().get_entity();
    w.update(0.016F);
    w.clear_changed();

    second.push(ent);
    journal.ticks.clear();
    w.update(0.016F);

    REQUIRE(journal.ticks == std::vector<std::string_view>{"first_before", "second_before", "after"});
    REQUIRE(after.moved_this_tick() == 1);
    REQUIRE(w.get<transform_component>(ent).get_position().x == 1.0F);
    REQUIRE(w.registry().late_changes().empty());
}

TEST_CASE("an added system is reachable by its type", "[world][ext]") {
    tick_journal journal;
    world w;

    REQUIRE(w.try_system<after_system>() == nullptr);
    REQUIRE(w.try_system<transform_system>() == &w.system<transform_system>());

    auto& added = w.add_system<after_system>(tick_stage::after_engine, journal);

    REQUIRE(&w.system<after_system>() == &added);
    REQUIRE(w.try_system<after_system>() == &added);
    REQUIRE(&std::as_const(w).system<after_system>() == &added);
}

TEST_CASE("a system cannot be added to the same world twice", "[world][ext]") {
    tick_journal journal;
    world w;

    w.add_system<after_system>(tick_stage::after_engine, journal);
    REQUIRE_THROWS_AS(
        w.add_system<after_system>(tick_stage::before_engine, journal), std::logic_error
    );

    world other;
    REQUIRE_NOTHROW(other.add_system<after_system>(tick_stage::after_engine, journal));
}

TEST_CASE("an added system hears its components come and go", "[world][ext]") {
    tick_journal journal;
    {
        world w;
        auto& charges = w.add_system<charge_system>(tick_stage::after_engine, journal);

        const auto kept    = w.create().with<charge_component>().get_entity();
        const auto dropped = w.create().with<charge_component>().get_entity();
        const auto killed  = w.create().with<charge_component>().get_entity();
        REQUIRE(journal.added == 3);

        charges.set_amount(kept, 1.0F);
        charges.set_amount(dropped, 2.0F);
        charges.set_amount(killed, 3.0F);

        w.modify(dropped).without<charge_component>();
        REQUIRE(journal.removed_charges == std::vector<float32>{2.0F});

        w.destroy(killed);
        REQUIRE(journal.removed_charges == std::vector<float32>{2.0F, 3.0F});

        const auto batch = w.batch_create(2).with<charge_component>().release_entities();
        REQUIRE(journal.added == 5);
        w.batch_modify(batch).without<charge_component>();
        REQUIRE(journal.removed_charges.size() == 4);

        REQUIRE(journal.shutdown == 0);
        REQUIRE(journal.deleted == 0);
    }

    REQUIRE(journal.removed_charges.size() == 5);
    REQUIRE(journal.removed_charges.back() == 1.0F);
    REQUIRE(journal.shutdown == 1);
    REQUIRE(journal.deleted == 1);
}

TEST_CASE("an observing system must be added before its component is in use", "[world][ext]") {
    world w;
    static_cast<void>(w.create().with<heat_component>().get_entity());

    REQUIRE_THROWS_AS(w.add_system<heat_system>(tick_stage::after_engine), std::logic_error);
}

TEST_CASE("a change is seen in the same tick by a system that runs later", "[world][ext]") {
    world w;
    auto& writer = w.add_system<charge_writer_system>(tick_stage::before_engine);
    auto& reader = w.add_system<charge_reader_system>(tick_stage::after_engine);

    const auto ent = w.create().with<charge_component>().get_entity();
    writer.aim(ent);
    w.update(0.016F);

    REQUIRE(reader.seen() == 1);
    REQUIRE(w.registry().late_changes().empty());

    w.clear_changed();
    writer.aim(invalid_entity);
    w.update(0.016F);

    REQUIRE(reader.seen() == 0);
}

TEST_CASE("a reader that runs before the writer is reported", "[world][ext]") {
    world w;
    auto& reader = w.add_system<charge_reader_system>(tick_stage::before_engine);
    auto& writer = w.add_system<charge_writer_system>(tick_stage::after_engine);

    const auto ent = w.create().with<charge_component>().get_entity();
    writer.aim(ent);
    w.update(0.016F);
    w.clear_changed();
    w.update(0.016F);

    REQUIRE(reader.seen() == 0);

    const auto late = w.registry().late_changes();
    REQUIRE(late.size() == 1);
    REQUIRE(late.front().component_id == component_id_of<charge_component>());
    REQUIRE(late.front().reader != late.front().writer);
}

TEST_CASE("added systems report their timing in tick order", "[world][ext]") {
    tick_journal journal;
    world w;

    w.add_system<after_system>(tick_stage::after_engine, journal);
    w.add_system<first_before_system>(tick_stage::before_engine, journal);
    w.update(0.016F);

    const auto timings = w.get_extension_timings();
    REQUIRE(timings.size() == 2);
    REQUIRE(timings[0].name == "first_before");
    REQUIRE(timings[0].stage == tick_stage::before_engine);
    REQUIRE(timings[1].name == "after");
    REQUIRE(timings[1].stage == tick_stage::after_engine);

    const auto& stats = w.get_update_stats();
    float32 sum       = 0.0F;
    for (const auto ms : stats.ms) {
        sum += ms;
    }
    for (const auto& timing : timings) {
        REQUIRE(timing.ms >= 0.0F);
        sum += timing.ms;
    }
    REQUIRE(stats.total_ms == Catch::Approx(sum));
}
