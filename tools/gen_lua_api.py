#!/usr/bin/env python3
"""Generate docs/LUA_API.md from the C++ Lua API registration code.

Parses (no code changes required):
  - src/script/lua_host.cpp : which table each luaRegister* fills
  - src/script/api_*.cpp    : reg(L, host, "name", lFn) lines + handler bodies
Argument/return types are inferred from the luaL_check*/luaRead*/luaPush* calls.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT_DIR = ROOT / "src" / "script"

# (regex, type label); group 1 must be the argument index
ARG_PATTERNS = [
    (r"luaL_checkstring\s*\(\s*L\s*,\s*(\d+)\s*\)", "string"),
    (r"luaL_optstring\s*\(\s*L\s*,\s*(\d+)\s*,", "string?"),
    (r"luaL_checknumber\s*\(\s*L\s*,\s*(\d+)\s*\)", "number"),
    (r"luaL_optnumber\s*\(\s*L\s*,\s*(\d+)\s*,", "number?"),
    (r"luaL_checkinteger\s*\(\s*L\s*,\s*(\d+)\s*\)", "int"),
    (r"luaL_optinteger\s*\(\s*L\s*,\s*(\d+)\s*,", "int?"),
    (r"luaReadEnt\s*\(\s*L\s*,\s*(\d+)\s*\)", "entity"),
    (r"entityOrName\s*\(\s*w\s*,\s*L\s*,\s*(\d+)\s*\)", "entity|string"),
    (r"luaReadV3\s*\(\s*L\s*,\s*(\d+)(?:\s*,[^)]*)?\)", "vec3"),
    (r"luaReadQuat\s*\(\s*L\s*,\s*(\d+)\s*\)", "quat"),
    (r"lua_istable\s*\(\s*L\s*,\s*(\d+)\s*\)", "table?"),
    (r"luaL_checktype\s*\(\s*L\s*,\s*(\d+)\s*,\s*LUA_TFUNCTION", "function"),
    (r"luaReadShapesTable\s*\(\s*L\s*,\s*(\d+)\s*,", "table"),
]

# first push wins; order matters (specific before generic)
RET_PATTERNS = [
    (r"lua_createtable\s*\(", "table"),
    (r"lua_newtable\s*\(", "table"),
    (r"luaPushEnt\s*\(", "entity"),
    (r"lua_pushinteger\s*\(", "int"),
    (r"lua_pushnumber\s*\(", "number"),
    (r"lua_pushboolean\s*\(", "bool"),
    (r"luaPushV3\s*\(", "vec3"),
    (r"luaPushV4\s*\(", "vec4"),
    (r"luaPushQuat\s*\(", "quat"),
    (r"lua_pushstring\s*\(", "string"),
]


def extract_body(text: str, start: int):
    """Return (body, body_start) for the function starting at/after `start`."""
    i = text.index("{", start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i : j + 1], i
    raise ValueError("unbalanced braces")


def parse_handlers(files):
    """symbol -> (doc, body, annotation) for every lXxx(lua_State* L) definition.

    `annotation` is the /// line directly above the definition when present
    (the Option-B doc string); otherwise "".
    """
    handlers = {}
    for f in files:
        text = f.read_text()
        lines = text.splitlines()
        for m in re.finditer(r"^(?:int|void)\s+(l[A-Z]\w*)\s*\(\s*lua_State\s*\*\s*L\s*\)\s*\{", text, re.M):
            sym = m.group(1)
            line_no = text[: m.start()].count("\n")
            # walk up: collect contiguous comment lines; the /// one is the annotation
            doc_lines, ann = [], ""
            for ln in range(line_no - 1, -1, -1):
                stripped = lines[ln].strip()
                if stripped.startswith("///"):
                    ann = stripped[3:].strip()
                    break  # the /// line is the only one we take
                if stripped.startswith("//"):
                    doc_lines.append(stripped[2:].strip())
                elif stripped == "":
                    continue
                else:
                    break
            handlers[sym] = ("\n".join(reversed(doc_lines)), extract_body(text, m.start())[0], ann)
    return handlers


def parse_registrations(files):
    """register fn -> [(lua name, handler symbol or None)] in declaration order.

    Two call conventions exist:
      reg(L, host, "name", lFn)          -> named handler (symbol)
      reg("name", [](lua_State* L) {...}) -> inline lambda (math table); the
                                            body is captured via brace matching
    """
    regs = {}
    lambdas = {}  # sym -> (doc, body)
    for f in files:
        text = f.read_text()
        lines = text.splitlines()
        for m in re.finditer(r"void\s+(luaRegister\w+)\s*\(\s*lua_State\s*\*\s*L\s*,\s*SceneScript\s*\*\s*host\s*\)\s*\{", text):
            body, body_start = extract_body(text, m.start())
            entries = []
            for e in re.finditer(r'reg\s*\(\s*L\s*,\s*host\s*,\s*"([^"]+)"\s*,\s*(l[A-Z]\w*)\s*\)', body):
                entries.append((e.group(1), e.group(2)))
            # inline lambdas: reg("name", [](lua_State* L) { ... })
            for e in re.finditer(r'reg\s*\(\s*"([^"]+)"\s*,\s*\[\]\s*\(\s*lua_State\s*\*\s*L\s*\)\s*\{', body):
                sym = f"__lambda_{e.group(1)}"
                entries.append((e.group(1), sym))
                lambdas[sym] = (comment_above(lines, body_start + e.start()), extract_body(text, body_start + e.start())[0])
            regs[m.group(1)] = entries
    return regs, lambdas


def comment_above(lines, pos):
    """contiguous /// or // comment lines directly above the position `pos`"""
    line_no = sum(1 for _ in text_iter_up_to(lines, pos))
    out = []
    for ln in range(line_no - 1, -1, -1):
        stripped = lines[ln].strip()
        if stripped.startswith("//"):
            out.append(stripped.lstrip("/").strip())
        elif stripped == "":
            continue
        else:
            break
    return "\n".join(reversed(out))


def text_iter_up_to(lines, pos):
    """yield line indices up to (not including) the line containing `pos`"""
    idx = 0
    for i, ln in enumerate(lines):
        if idx + len(ln) + 1 > pos:
            return
        idx += len(ln) + 1
        yield i


def parse_tables(host_file: Path):
    """register fn -> lua global table name.

    A table is opened by `lua_newtable(L)` (name arrives at the following
    `lua_setglobal`) or by `lua_getglobal(L, "math")` (name known up front).
    """
    text = host_file.read_text()
    init, _ = extract_body(text, text.index("void SceneScript::init"))
    out, pending, cur = {}, [], None
    for line in init.splitlines():
        line = line.strip()
        if (m := re.match(r"lua_newtable\s*\(\s*L\s*\)", line)):
            pending, cur = [], None
        elif (m := re.match(r'lua_getglobal\s*\(\s*L\s*,\s*"(\w+)"\s*\)', line)):
            pending, cur = [], m.group(1)
        elif (m := re.match(r"luaRegister(\w+)\s*\(\s*L\s*,\s*this\s*\)", line)):
            if cur is not None:
                out[m.group(1)] = cur
            else:
                pending.append(m.group(1))
        elif (m := re.match(r'lua_setglobal\s*\(\s*L\s*,\s*"(\w+)"\s*\)', line)):
            for fn in pending:
                out[fn] = m.group(1)
            pending = []
    return out


def infer_args(body: str):
    found = {}
    ent_idx = set()
    for pat, label in ARG_PATTERNS:
        for m in re.finditer(pat, body):
            idx = int(m.group(1))
            if label == "entity":
                ent_idx.add(idx)
            if idx not in found or label.endswith("?") and not found[idx].endswith("?"):
                found[idx] = label
    # an index read both as entity (luaReadEnt) and string is entity-or-name
    for idx in ent_idx:
        if found.get(idx) == "string":
            found[idx] = "entity|string"
    return [f"{found[i]}" for i in sorted(found)]


def infer_ret(body: str) -> str:
    # strip the outermost function braces, then look at pushes in order
    inner = body[1:-1]
    best, best_pos = "none", len(inner)
    for pat, label in RET_PATTERNS:
        m = re.search(pat, inner)
        if m and m.start() < best_pos:
            best, best_pos = label, m.start()
    return best


# /// <table>.<name>(args) -> ret | description   (the -> ret part is optional)
ANN_RE = re.compile(r"^(\w+)\.([\w]+)\(([^)]*)\)(?:\s*->\s*([^|]+?))?\s*\|\s*(.+)$")

# /// script-hook _name(args) | description   and   /// script-instance | description
HOOK_RE = re.compile(r"^script-hook\s+(_\w+)\(([^)]*)\)\s*\|\s*(.+)$")
INST_RE = re.compile(r"^script-instance\s*\|\s*(.+)$")


def parse_script_contract(host_file: Path):
    """(instance_doc, {hook name: (args, desc)}, grabbed hook names) from lua_host.cpp."""
    text = host_file.read_text()
    inst, hooks, grabbed = "", {}, []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("/// script-hook "):
            m = HOOK_RE.match(s[3:].lstrip())
            if m:
                hooks[m.group(1)] = (m.group(2).strip(), m.group(3).strip())
        elif s.startswith("/// script-instance"):
            m = INST_RE.match(s[3:].lstrip())
            if m:
                inst = m.group(1).strip()
    grabbed = re.findall(r'grab\s*\(\s*"(_\w+)"\s*,', text)
    return inst, hooks, grabbed


def main():
    files = sorted(SCRIPT_DIR.glob("api_*.cpp"))
    handlers = parse_handlers(files)
    regs, lambdas = parse_registrations(files)
    tables = parse_tables(SCRIPT_DIR / "lua_host.cpp")  # keys: 'Components', 'Math', ...

    order = ["scene", "math", "graphics", "physics", "soft", "ui", "input"]
    lines = [
        "# Lua API reference (auto-generated)",
        "",
        "Generated by `tools/gen_lua_api.py` from the `///` doc strings in `src/script/api_*.cpp`.",
        "Do not edit by hand; re-run the generator after API changes. Every function must carry a",
        "`/// table.name(args) -> ret | description` line above its handler (or reg() call).",
        "",
    ]
    total = 0
    undocumented = []
    for tname in order:
        fns = []
        for fn, entries in regs.items():
            if tables.get(fn.removeprefix("luaRegister")) == tname:
                fns.extend(entries)
        lines.append(f"## {tname}")
        lines.append("")
        if tname == "math":
            lines.append("_Vector/quaternion helpers added on top of Lua's standard `math` library_")
            lines.append("")
        for name, sym in fns:
            if sym.startswith("__lambda_"):
                doc, body = lambdas.get(sym, ("", ""))
                ann = doc  # for lambdas the comment above reg() is the annotation itself
            else:
                doc, body, ann = handlers.get(sym, ("", "", ""))
            args, ret, desc = None, None, None
            if ann:
                m = ANN_RE.match(ann)
                if not m:
                    print(f"WARNING: bad annotation for {tname}.{name}: {ann}", file=sys.stderr)
                else:
                    at, aname, aargs, aret, adesc = m.groups()
                    if (at, aname) != (tname, name):
                        print(f"WARNING: annotation says {at}.{aname} but registration is {tname}.{name}", file=sys.stderr)
                    args, ret, desc = aargs.strip(), (aret or "").strip() or None, adesc.strip()
            if args is None:  # no usable annotation: fall back to heuristics
                undocumented.append(f"{tname}.{name}")
                args = ", ".join(infer_args(body))
                ret = infer_ret(body)
                desc = doc or ""
            sig = f"{tname}.{name}({args})"
            if ret:
                sig += f" -> {ret}"
            lines.append(f"### `{sig}`")
            lines.append("")
            lines.append(desc if desc else "_no description_")
            lines.append("")
        total += len(fns)

    # script module contract (the "magic" hooks called on attached .lua modules)
    inst, hooks, grabbed = parse_script_contract(SCRIPT_DIR / "lua_host.cpp")
    lines.append("## script hooks (attached modules)")
    lines.append("")
    lines.append("_A `.lua` file attached via `scene.attach_script(e, file, params)` must return a table (the module). The engine calls its hooks with a per-attach instance table as `self`._")
    lines.append("")
    if inst:
        lines.append(f"### `inst` — {inst}")
        lines.append("")
    for name in grabbed:  # machine truth: the names activateScripts() actually looks up
        args, desc = hooks.get(name, ("", ""))
        lines.append(f"### `{name}({args})`")
        lines.append("")
        lines.append(desc if desc else "_no description_")
        lines.append("")

    out = ROOT / "docs" / "LUA_API.md"
    out.parent.mkdir(exist_ok=True)
    out.write_text("\n".join(lines))
    per = {t: sum(1 for fn, e in regs.items() if tables.get(fn.removeprefix("luaRegister")) == t for _ in e) for t in order}
    print(f"wrote {out} ({total} functions): " + ", ".join(f"{k}={v}" for k, v in per.items()))
    missing = [s for fns in regs.values() for _, s in fns if not s.startswith("__lambda_") and s not in handlers]
    if missing:
        print(f"WARNING: no handler definition found for: {sorted(set(missing))}", file=sys.stderr)
        return 1
    if undocumented:
        print(f"WARNING: {len(undocumented)} functions without /// annotation (heuristic fallback): {', '.join(undocumented)}", file=sys.stderr)
        return 1
    for name in grabbed:
        if name not in hooks:
            print(f"WARNING: script hook {name} is looked up in lua_host.cpp but has no /// script-hook doc", file=sys.stderr)
            return 1
    for name in hooks:
        if name not in grabbed:
            print(f"WARNING: stale /// script-hook doc for {name} (not looked up in lua_host.cpp)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
