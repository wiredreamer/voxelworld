module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

operation_manager::operation_manager(
    app_state& st
)
    : state_(&st) {}

auto operation_manager::execute(
    std::unique_ptr<base_operation> op
) -> void {
    op->context_ = state_->ctx.stack;
    op->execute();
    redo_.clear();
    undo_.emplace_back(std::move(op));
}

auto operation_manager::clear() -> void {
    undo_.clear();
    redo_.clear();
}

auto operation_manager::is_undo_empty() const -> bool {
    return undo_.empty();
}

auto operation_manager::undo() -> void {
    if (undo_.empty()) {
        return;
    }

    auto& op = undo_.back();

    state_->ctx.stack = op->context_;
    op->undo();
    redo_.emplace_back(std::move(op));
    undo_.pop_back();
}

auto operation_manager::is_redo_empty() const -> bool {
    return redo_.empty();
}

auto operation_manager::redo() -> void {
    if (redo_.empty()) {
        return;
    }

    auto& op = redo_.back();
    state_->ctx.stack = op->context_;
    op->execute();
    undo_.emplace_back(std::move(op));
    redo_.pop_back();
}

}  // namespace vw::sculptor
