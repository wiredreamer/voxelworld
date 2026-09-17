module vw.sculptor;

import std;

import vw.core;

namespace vw::sculptor {

composite_operation::composite_operation(
    std::vector<std::unique_ptr<base_operation>> parts
)
    : parts_(std::move(parts)) {}

auto composite_operation::execute() -> void {
    for (auto& part : parts_) {
        part->execute();
    }
}

auto composite_operation::undo() -> void {
    for (auto& part : parts_ | std::views::reverse) {
        part->undo();
    }
}

}  // namespace vw::sculptor
