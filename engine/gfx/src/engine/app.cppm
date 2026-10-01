export module vw.gfx:engine.app;

import vw.core;
import :engine.report;

export namespace vw::gfx {

class engine;

class app {
public:
    using engine_type = engine;

    explicit app(engine_type& eng) : engine_(&eng) {}

    virtual ~app() = default;

    app(const app&)                    = delete;
    auto operator=(const app&) -> app& = delete;

    virtual auto update([[maybe_unused]] float32 delta_time) -> void {}

    virtual auto render([[maybe_unused]] float32 delta_time) -> void {}

    [[nodiscard]] virtual auto is_bench_ready() const -> bool {
        return true;
    }

    virtual auto collect_report([[maybe_unused]] report& out) const -> void {}

protected:
    [[nodiscard]] auto get_engine() const -> engine_type& {
        return *engine_;
    }

private:
    engine_type* engine_ = nullptr;
};

}  // namespace vw::gfx
