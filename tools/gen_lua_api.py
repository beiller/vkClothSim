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


def split_ann(ann_lines):
    """annotation lines (top to bottom) -> (description, example lines or None).

    Non-indented lines form the description (joined with spaces); the first line
    carrying leading whitespace starts an indented Lua example block, and every
    line after it belongs to the example. Example lines keep their relative
    indentation (dedented by the block's minimum indent).
    """
    desc, ex = [], None
    for ln in ann_lines:
        if ex is None and ln[:1] in (" ", "\t"):
            ex = []
        (ex if ex is not None else desc).append(ln)
    if ex:
        stripped = [ln.rstrip() for ln in ex]
        indents = [len(ln) - len(ln.lstrip()) for ln in stripped if ln.strip()]
        pad = min(indents) if indents else 0
        ex = [ln[pad:] for ln in stripped]
    return " ".join(d.strip() for d in desc), (ex or None)


def parse_handlers(files):
    """symbol -> (doc, body, annotation lines) for every lXxx(lua_State* L) definition.

    `annotation` is the /// block directly above the definition when present
    (the doc string); otherwise []. Leading whitespace after /// is preserved so
    split_ann can separate description from example code.
    """
    handlers = {}
    for f in files:
        text = f.read_text()
        lines = text.splitlines()
        for m in re.finditer(r"^(?:(?:static\s+)?(?:int|void))\s+(l[A-Z]\w*)\s*\(\s*lua_State\s*\*\s*L\s*\)\s*\{", text, re.M):
            sym = m.group(1)
            line_no = text[: m.start()].count("\n")
            # walk up: collect contiguous comment lines; the /// block is the annotation
            doc_lines, ann_lines = [], []
            for ln in range(line_no - 1, -1, -1):
                stripped = lines[ln].strip()
                if stripped.startswith("///"):
                    content = lines[ln].split("///", 1)[1]
                    ann_lines.append(content[1:] if content.startswith(" ") else content)
                elif stripped.startswith("//"):
                    doc_lines.append(stripped[2:].strip())
                elif stripped == "":
                    continue
                else:
                    break
            handlers[sym] = ("\n".join(reversed(doc_lines)), extract_body(text, m.start())[0], list(reversed(ann_lines)))
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
    """contiguous /// or // comment lines directly above the position `pos` (list, top to bottom)"""
    line_no = sum(1 for _ in text_iter_up_to(lines, pos))
    out = []
    for ln in range(line_no - 1, -1, -1):
        stripped = lines[ln].strip()
        if stripped.startswith("///"):
            content = lines[ln].split("///", 1)[1]
            out.append(content[1:] if content.startswith(" ") else content)
        elif stripped.startswith("//"):
            out.append(stripped[2:].strip())
        elif stripped == "":
            continue
        else:
            break
    return list(reversed(out))


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
# The separator is a spaced pipe " | "; return types may contain bare pipes ("table|nil").
ANN_RE = re.compile(r"^(\w+)\.([\w]+)\(([^)]*)\)(?:\s*->\s*(.+?))?\s+\|\s+(.*)$")

# /// script-hook _name(args) | description   and   /// script-instance | description
HOOK_RE = re.compile(r"^script-hook\s+(_\w+)\(([^)]*)\)\s*\|\s*(.+)$")
INST_RE = re.compile(r"^script-instance\s*\|\s*(.+)$")


def parse_script_contract(host_file: Path):
    """(instance_docs, {hook name: (args, desc)}, grabbed hook names) from lua_host.cpp."""
    text = host_file.read_text()
    insts, hooks, grabbed = [], {}, []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("/// script-hook "):
            m = HOOK_RE.match(s[3:].lstrip())
            if m:
                hooks[m.group(1)] = (m.group(2).strip(), m.group(3).strip())
        elif s.startswith("/// script-instance"):
            m = INST_RE.match(s[3:].lstrip())
            if m:
                insts.append(m.group(1).strip())
    grabbed = re.findall(r'grab\s*\(\s*"(_\w+)"\s*,', text)
    return insts, hooks, grabbed


KIND_LUA = {
    "SliderF": "number",
    "SliderI": "int",
    "Checkbox": "bool",
    "Color": "vec3",
    "DragV3": "vec3",
    "Ref": "entity-ref",
    "Str": "string",
    "StrArr": "string[]",
    "Quat": "quat",
    "FloatArr": "number[]",
    "UIntArr": "int[]",
    "EntArr": "entity[]",
}


def parse_components(reflect_file: Path):
    """[(type name, desc, [(field, kind, (lo,hi)|None, doc)])] from reflect.hpp's kTypes.

    A `///` comment line directly above a field entry inside the array is that field's doc.
    """
    text = reflect_file.read_text()
    arrays = {}
    for m in re.finditer(r"static const FieldDesc (k\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        fields = []
        prev_end = 0
        for fm in re.finditer(
            r'\{"(\w+)",\s*[^{]*?FieldKind::(\w+)(?:,\s*(-?[\d.]+f?)\s*,\s*(-?[\d.]+f?))?', m.group(2)
        ):
            lo, hi = fm.group(3), fm.group(4)
            # field doc: the /// line directly above this entry (in the gap since the previous one)
            doc = ""
            gap = m.group(2)[prev_end : fm.start()]
            last = [ln for ln in gap.splitlines() if ln.strip()]
            if last and last[-1].strip().startswith("///"):
                doc = last[-1].strip()[3:].strip()
            fields.append((fm.group(1), fm.group(2), (lo, hi) if lo else None, doc))
            prev_end = fm.end()
        arrays[m.group(1)] = fields
    out = []
    for m in re.finditer(r'\{"(\w+)",\s*(k\w+)\}', text):
        name, arr = m.group(1), m.group(2)
        desc = ""
        am = re.search(r"static const FieldDesc " + arr + r"\[\]", text)
        if am:
            # walk up over the whole contiguous /// block (descriptions may span lines)
            i = text[: am.start()].count("\n") - 1
            block = []
            while i >= 0:
                s = text.splitlines()[i].strip()
                if not s.startswith("///"):
                    break
                block.append(s[3:].strip())
                i -= 1
            dm = re.match(rf"{name}\s*\|\s*(.+)$", " ".join(reversed(block)))
            if dm:
                desc = dm.group(1).strip()
        out.append((name, desc, arrays.get(arr, [])))
    return out


def slug(text: str) -> str:
    """GitHub-style heading anchor."""
    s = text.lower()
    s = re.sub(r"[`*_]", "", s)
    s = re.sub(r"[^\w\s-]", "", s)
    return s.strip().replace(" ", "-")


def build_toc(lines: list) -> list:
    """TOC block from the ##/### headings already in lines."""
    toc = ["## Contents", ""]
    for ln in lines:
        if ln.startswith("## "):
            toc.append(f"- [{ln[3:]}](#{slug(ln[3:])})")
        elif ln.startswith("### "):
            toc.append(f"  - [{ln[4:]}](#{slug(ln[4:])})")
    return toc


def main():
    files = sorted(SCRIPT_DIR.glob("api_*.cpp"))
    handlers = parse_handlers(files)
    regs, lambdas = parse_registrations(files)
    tables = parse_tables(SCRIPT_DIR / "lua_host.cpp")  # keys: 'Components', 'Math', ...

    order = ["scene", "math", "graphics", "physics", "soft", "ui", "engine", "input"]
    lines = [
        "# Lua API reference (auto-generated)",
        "",
        "Generated by `tools/gen_lua_api.py` from the `///` doc strings in `src/script/api_*.cpp` and the component schema in `src/core/reflect.hpp`.",
        "Do not edit by hand; re-run the generator after API changes. Every function must carry a",
        "`/// table.name(args) -> ret | description` line above its handler (or reg() call); further non-indented",
        "`///` lines extend the description, and `///` lines indented with leading whitespace form a Lua example.",
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
                doc, body = "", ""
                ann_lines = lambdas.get(sym, ([], ""))[0]  # the comment above reg() is the annotation itself
            else:
                doc, body, ann_lines = handlers.get(sym, ("", "", []))
            desc, example = split_ann(ann_lines)
            args, ret, adesc = None, None, None
            if desc:
                m = ANN_RE.match(desc)
                if not m:
                    print(f"WARNING: bad annotation for {tname}.{name}: {desc}", file=sys.stderr)
                else:
                    at, aname, aargs, aret, adesc = m.groups()
                    if (at, aname) != (tname, name):
                        print(f"WARNING: annotation says {at}.{aname} but registration is {tname}.{name}", file=sys.stderr)
                    args, ret, adesc = aargs.strip(), (aret or "").strip() or None, adesc.strip()
            if args is None:  # no usable annotation: fall back to heuristics
                undocumented.append(f"{tname}.{name}")
                args = ", ".join(infer_args(body))
                ret = infer_ret(body)
                adesc = doc or ""
            sig = f"{tname}.{name}({args})"
            if ret:
                sig += f" -> {ret}"
            lines.append(f"### `{sig}`")
            lines.append("")
            lines.append(adesc if adesc else "_no description_")
            if example:
                lines += ["", "```lua"] + example + ["```"]
            lines.append("")
        total += len(fns)

    # emplace-able components: schema straight from reflect.hpp (single source of truth)
    comps = parse_components(ROOT / "src" / "core" / "reflect.hpp")
    if comps:
        lines.append("## components (emplace / replace)")
        lines.append("")
        lines.append("_Emplaced via `scene.emplace(e, \"Type\", {fields})`, replaced via `scene.replace(...)`; all fields are optional (defaults come from the C++ struct). Entity refs accept an entity or a named-entity string._")
        lines.append("")
    for name, desc, fields in comps:
        lines.append(f"### {name}")
        if desc:
            lines.append("")
            lines.append(desc)
        if fields:
            lines.append("")
            has_docs = any(d for *_, d in fields)
            head = "| field | type | range | description |" if has_docs else "| field | type | range |"
            sep = "|---|---|---|---|" if has_docs else "|---|---|---|"
            lines.append(head)
            lines.append(sep)
            for f, kind, rng, doc in fields:
                r = f"[{rng[0].rstrip('f')}, {rng[1].rstrip('f')}]" if rng else ""
                lines.append(f"| {f} | {KIND_LUA.get(kind, kind)} | {r} | {doc} |" if has_docs else f"| {f} | {KIND_LUA.get(kind, kind)} | {r} |")
        lines.append("")

    # script module contract (the "magic" hooks called on attached .lua modules)
    insts, hooks, grabbed = parse_script_contract(SCRIPT_DIR / "lua_host.cpp")
    lines.append("## script hooks (attached modules)")
    lines.append("")
    lines.append("_A `.lua` file attached via `scene.attach_script(e, file, params)` must return a table (the module). The engine calls its hooks with a per-attach instance table as `self`._")
    lines.append("")
    if insts:
        lines.append(f"### `inst` — {insts[0]}")
        for extra in insts[1:]:
            lines.append("")
            lines.append(extra)
        lines.append("")
    for name in grabbed:  # machine truth: the names activateScripts() actually looks up
        args, desc = hooks.get(name, ("", ""))
        lines.append(f"### `{name}({args})`")
        lines.append("")
        lines.append(desc if desc else "_no description_")
        lines.append("")

    # table of contents, inserted before the first section
    first = next(i for i, ln in enumerate(lines) if ln.startswith("## "))
    lines[first:first] = build_toc(lines) + [""]

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
