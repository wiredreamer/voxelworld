"""Interface hygiene checks that the module migration is supposed to hold.

Run from the repository root; exits non-zero and prints every violation.
Wired into CI so the invariants below cannot rot back in silently.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ["engine", "apps", "tests"]

# Two places may still speak the Vulkan C API: the ImGui backend takes C
# handles and nothing else, and GLFW creates the surface before vk:: exists.
VULKAN_C_ALLOWED = {"engine/gfx/src/render/renderer.cpp", "engine/platform/src/window/window.cpp"}

COMMENT_OR_STRING = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"', re.S)


def code_only(text: str) -> str:
    """Comments and string literals are not API use -- strip them before scanning."""
    return COMMENT_OR_STRING.sub(" ", text)


def sources():
    for top in SOURCES:
        for path in (ROOT / top).rglob("*"):
            if path.suffix in {".cpp", ".cppm"} and path.is_file():
                yield path


def rel(path):
    return path.relative_to(ROOT).as_posix()


def main() -> int:
    problems = []

    # 1. the .inl.h idiom is gone for good
    for path in ROOT.rglob("*.inl.h"):
        if "build" not in rel(path):
            problems.append("%s: .inl.h files are not used any more" % rel(path))

    for path in sources():
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = code_only(raw)
        name = rel(path)

        # 2. nothing includes our own headers -- the engine is reached by import
        for inc in re.findall(r'^\s*#include ("[^"]+"|<vw/[^>]+>)', raw, re.M):
            problems.append("%s: includes our own header %s" % (name, inc))

        # 3. the Vulkan C API stops at the two boundaries above
        if name not in VULKAN_C_ALLOWED:
            for tok in sorted(set(re.findall(
                    r"\bVk[A-Z][A-Za-z0-9]*|\bVK_[A-Z0-9_]+|\bvk[A-Z][A-Za-z0-9]*\(", text))):
                problems.append("%s: Vulkan C API leaked in: %s" % (name, tok))

        # 4. vk:: lives inside vw.gfx only
        if "vk::" in text and not name.startswith("engine/gfx/src"):
            problems.append("%s: names vk:: outside vw.gfx" % name)

        # 5. the dispatcher detail is localised to one file
        if "vk::detail" in text and name != "engine/gfx/src/render/vulkan_context.cpp":
            problems.append("%s: vk::detail belongs to render/vulkan_context.cpp alone" % name)

        # 6. exported interface partitions do not re-export third-party modules
        if path.suffix == ".cppm" and re.search(r"^export import vulkan;", raw, re.M):
            problems.append("%s: re-exports the Vulkan binding" % name)

        # 7. vw.asset knows nothing about the ECS -- a model or a clip outlives
        #    the entity that carries it, and reading a scene into a registry is
        #    the other side of the line, in vw.world:scene.*
        if name.startswith("engine/asset/src"):
            if re.search(r"^\s*(?:export )?import vw\.ecs;", text, re.M):
                problems.append("%s: vw.asset must not import vw.ecs" % name)
            if "vw::ecs" in text or re.search(r"\bnamespace ecs\b", text):
                problems.append("%s: names the ecs namespace inside vw.asset" % name)

        # 8. ...and the other direction: vw::asset is declared in vw.asset alone,
        #    so module and namespace stay the same boundary. Calling into it from
        #    elsewhere is fine -- a using-directive, as vw.gfx does for vw::ecs.
        elif re.search(r"^\s*(?:export )?namespace vw::asset\b", text, re.M):
            problems.append("%s: opens namespace vw::asset outside vw.asset" % name)

    for p in sorted(problems):
        print(p)
    print("%d violation(s)" % len(problems))
    return 1 if problems else 0


sys.exit(main())
