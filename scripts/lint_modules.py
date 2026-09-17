import ast
import io
import pathlib
import re
import sys
import tokenize

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ["engine", "apps", "tests"]

VULKAN_C_ALLOWED = {"engine/gfx/src/render/renderer.cpp", "engine/platform/src/window/window.cpp"}

COMMENT_OR_STRING = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"', re.S)

SHADER_SUFFIXES = {".vert", ".frag", ".comp", ".glsl"}
DOC_LINK = re.compile(r"см\. docs/\S+")
C_SERVICE_COMMENT = re.compile(r"clang-format (?:off|on)|NOLINT\S*(?:\(.*\))?")


def code_only(text: str) -> str:
    return COMMENT_OR_STRING.sub(" ", text)


SCOPE_TOKEN = re.compile(r"\b(export\s+)?namespace\s*([\w:]*)\s*\{|\bexport\s*\{|\bexport\b|[{}]")


def exported_details(text: str):
    code = COMMENT_OR_STRING.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)
    leaks = []
    stack = []
    for m in SCOPE_TOKEN.finditer(code):
        exported, in_detail = stack[-1] if stack else (False, False)
        token = m.group(0)
        line = code.count("\n", 0, m.start()) + 1
        if token == "}":
            if stack:
                stack.pop()
        elif token == "{":
            stack.append((exported, in_detail))
        elif "namespace" in token:
            exported = exported or m.group(1) is not None
            is_detail = re.search(r"(?:^|::)detail(?:::|$)", m.group(2)) is not None
            if (is_detail and exported) or (in_detail and m.group(1)):
                leaks.append(line)
            stack.append((exported, in_detail or is_detail))
        elif token.endswith("{"):
            if in_detail:
                leaks.append(line)
            stack.append((True, in_detail))
        elif in_detail:
            leaks.append(line)
    return leaks


def identifier_before(text, i):
    start = i
    while start > 0 and (text[start - 1].isalnum() or text[start - 1] in "_."):
        start -= 1
    return text[start:i]


def skip_quoted(text, i, quote):
    i += 1
    while i < len(text) and text[i] not in (quote, "\n"):
        i += 2 if text[i] == "\\" else 1
    return i + 1


def c_comments(text):
    i, n = 0, len(text)
    while i < n:
        pair = text[i:i + 2]
        if pair in ("//", "/*"):
            end = text.find("\n", i) if pair == "//" else text.find("*/", i + 2)
            end = n if end < 0 else end + (2 if pair == "/*" else 0)
            yield i, pair, text[i + 2:end - (2 if pair == "/*" else 0)]
            i = end
        elif text[i] == '"':
            if identifier_before(text, i) in ("R", "u8R", "uR", "UR", "LR"):
                delimiter = re.match(r'"([^(\s]*)\(', text[i:]).group(1)
                i = text.find(")" + delimiter + '"', i) + len(delimiter) + 2
            else:
                i = skip_quoted(text, i, '"')
        elif text[i] == "'" and not identifier_before(text, i)[:1].isdigit():
            i = skip_quoted(text, i, "'")
        else:
            i += 1


def c_comment_allowed(text, start, kind, body):
    before = text[text.rfind("\n", 0, start) + 1:start].strip()
    body = body.strip()
    if kind == "//" and not body and before:
        return True
    if before == "}" and body.startswith("namespace"):
        return True
    return DOC_LINK.fullmatch(body) is not None or C_SERVICE_COMMENT.fullmatch(body) is not None


def cmake_comments(text):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        bracket = re.match(r"#?\[(=*)\[", text[i:])
        if c == "\\":
            i += 2
        elif c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
            i += 1
        elif bracket:
            end = text.find("]" + bracket.group(1) + "]", i)
            end = n if end < 0 else end + len(bracket.group(1)) + 2
            if c == "#":
                yield i, text[i + 1:end]
            i = end
        elif c == "#":
            end = text.find("\n", i)
            end = n if end < 0 else end
            yield i, text[i + 1:end]
            i = end
        else:
            i += 1


def yaml_comments(text):
    offset = 0
    for line in text.split("\n"):
        quote = None
        for k, ch in enumerate(line):
            if quote:
                quote = None if ch == quote else quote
            elif ch in "\"'" and (k == 0 or line[k - 1] in " \t:[{,-"):
                quote = ch
            elif ch == "#" and (k == 0 or line[k - 1] in " \t"):
                yield offset + k, line[k + 1:]
                break
        offset += len(line) + 1


def line_comments(text):
    offset = 0
    for line in text.split("\n"):
        if line.lstrip().startswith("#"):
            yield offset + len(line) - len(line.lstrip()), line.lstrip()[1:]
        offset += len(line) + 1


def python_comments(text):
    for token in tokenize.generate_tokens(io.StringIO(text).readline):
        if token.type == tokenize.COMMENT:
            yield token.start[0], token.string[1:]
    for node in ast.walk(ast.parse(text)):
        if isinstance(node, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            body = node.body
            if body and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant) \
                    and isinstance(body[0].value.value, str):
                yield body[0].lineno, "docstring"


def commented_files():
    for path in sources():
        yield path, "c"
    for path in sorted((ROOT / "shaders").glob("*")):
        if path.suffix in SHADER_SUFFIXES:
            yield path, "c"
    yield ROOT / "CMakeLists.txt", "cmake"
    for top in SOURCES:
        for path in sorted((ROOT / top).rglob("CMakeLists.txt")):
            yield path, "cmake"
    for path in sorted((ROOT / "cmake").rglob("*.cmake")):
        yield path, "cmake"
    for path in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
        yield path, "yaml"
    for name in (".clang-format", ".clang-tidy"):
        yield ROOT / name, "yaml"
    yield ROOT / ".gitignore", "lines"
    for path in sorted((ROOT / "scripts").glob("*.py")):
        yield path, "python"


def comment_lines(path, kind):
    text = path.read_text(encoding="utf-8", errors="replace")
    if kind == "python":
        return [line for line, _ in python_comments(text)]
    if kind == "c":
        found = [(start, body) for start, pair, body in c_comments(text)
                 if not c_comment_allowed(text, start, pair, body)]
    else:
        scan = {"cmake": cmake_comments, "yaml": yaml_comments, "lines": line_comments}[kind]
        found = [(start, body) for start, body in scan(text)
                 if DOC_LINK.fullmatch(body.strip()) is None]
    lines = sorted({text.count("\n", 0, start) + 1 for start, _ in found})
    return [line for line in lines if line - 1 not in lines]


def sources():
    for top in SOURCES:
        for path in (ROOT / top).rglob("*"):
            if path.suffix in {".cpp", ".cppm"} and path.is_file():
                yield path


def rel(path):
    return path.relative_to(ROOT).as_posix()


def main() -> int:
    problems = []

    for path in ROOT.rglob("*.inl.h"):
        if "build" not in rel(path):
            problems.append("%s: .inl.h files are not used any more" % rel(path))

    for path in sources():
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = code_only(raw)
        name = rel(path)

        for inc in re.findall(r'^\s*#include ("[^"]+"|<vw/[^>]+>)', raw, re.M):
            problems.append("%s: includes our own header %s" % (name, inc))

        if name not in VULKAN_C_ALLOWED:
            for tok in sorted(set(re.findall(
                    r"\bVk[A-Z][A-Za-z0-9]*|\bVK_[A-Z0-9_]+|\bvk[A-Z][A-Za-z0-9]*\(", text))):
                problems.append("%s: Vulkan C API leaked in: %s" % (name, tok))

        if "vk::" in text and not name.startswith("engine/gfx/src"):
            problems.append("%s: names vk:: outside vw.gfx" % name)

        if "vk::detail" in text and name != "engine/gfx/src/render/vulkan_context.cpp":
            problems.append("%s: vk::detail belongs to render/vulkan_context.cpp alone" % name)

        if path.suffix == ".cppm" and re.search(r"^export import vulkan;", raw, re.M):
            problems.append("%s: re-exports the Vulkan binding" % name)

        if name.startswith("engine/asset/src"):
            if re.search(r"^\s*(?:export )?import vw\.ecs;", text, re.M):
                problems.append("%s: vw.asset must not import vw.ecs" % name)
            if "vw::ecs" in text or re.search(r"\bnamespace ecs\b", text):
                problems.append("%s: names the ecs namespace inside vw.asset" % name)

        elif re.search(r"^\s*(?:export )?namespace vw::asset\b", text, re.M):
            problems.append("%s: opens namespace vw::asset outside vw.asset" % name)

        if path.suffix == ".cppm":
            for line in exported_details(raw):
                problems.append("%s:%d: detail is exported -- declare it outside the export block"
                                % (name, line))

    for path, kind in commented_files():
        if path.is_file():
            for line in comment_lines(path, kind):
                problems.append("%s:%d: comment -- let a name say it, or docs/ behind `см. docs/...`"
                                % (rel(path), line))

    for p in sorted(problems):
        print(p)
    print("%d violation(s)" % len(problems))
    return 1 if problems else 0


sys.exit(main())
