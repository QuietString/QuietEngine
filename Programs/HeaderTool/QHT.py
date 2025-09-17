#!/usr/bin/env python3
# QuietHeaderTool (QHT): per-module header generator for QPROPERTY/QFUNCTION markers.
# Emits a single header per module with inline invokers and a QHT_Register_<Unit>(Registry&).

import argparse, re

from Utils import *
from pathlib import Path

CLASS_RE = re.compile(r"\b(class|struct)\s+(?P<name>[A-Za-z_]\w*)\s*(?:\:(?P<bases>[^{]+))?\{", re.M)
COMMENT_SL = re.compile(r"//.*?$", re.M)
COMMENT_ML = re.compile(r"/\*.*?\*/", re.S)
PROP_RE = re.compile(r"QPROPERTY\s*(?:\((?P<meta>[^)]*)\))?\s*(?P<decl>[^;{]+);", re.M)
FUNC_RE = re.compile(
    r"QFUNCTION\s*(?:\((?P<meta>[^)]*)\))?\s*(?P<ret>[~\w:\s*&<>,]+?)\s+(?P<name>[A-Za-z_]\w*)\s*"
    r"\((?P<params>[^)]*)\)\s*(?:const\s*)?(?:;|\{)", re.M)
PARAM_SPLIT = re.compile(r",(?![^<]*>)")

def _canonical_as_type(t: str) -> str:
    s = t.strip()
    # remove leading/trailing const
    s = re.sub(r'^\s*const\s+', '', s)
    s = re.sub(r'\s+const\s*$', '', s)
    # remove reference and rvalue ref
    s = s.replace('&', '').replace('&&', '')
    # collapse spaces
    s = re.sub(r'\s+', ' ', s).strip()
    return s

def strip_comments(src: str) -> str:
    src = COMMENT_ML.sub("\n", src)
    src = COMMENT_SL.sub("", src)
    return src

def parse_meta_list(meta: str):
    result = []
    if not meta:
        return result
    tokens = [m.group(0) for m in re.finditer(r"\w+\s*=\s*\"[^\"]*\"|\w+\s*=\s*[^,]+|\w+", meta)]
    for t in tokens:
        if "=" in t:
            k, v = t.split("=", 1)
            result.append((k.strip(), v.strip().strip('"')))
        else:
            result.append((t.strip(), "true"))
    return result

class Property:
    def __init__(self, type_str, name, meta_items):
        self.type = type_str.strip()
        self.name = name.strip()
        self.meta = meta_items

class Function:
    def __init__(self, ret_type, name, params, meta_items):
        self.ret = ret_type.strip()
        self.name = name.strip()
        self.params = params
        self.meta = meta_items

class ClassInfo:
    def __init__(self, name, src_path=None, bases=""):
        self.name = name
        self.src_path = src_path
        self.bases = bases or ""
        self.properties = []
        self.functions = []
    def has_any_marks(self):
        return bool(self.properties or self.functions)
    def is_qobject(self):
        # After resolve_qobject_flags() is called, use cached result.
        if hasattr(self, "_is_qobject_cache"):
            return bool(self._is_qobject_cache)
        # Fallback: direct token check (legacy)
        b = self.bases.replace("public", " ").replace("protected", " ").replace("private", " ")
        return "QObject" in b or "::QObject" in b

def _extract_base_names(bases: str):
    if not bases:
        return []
    # Drop access specifiers and 'virtual'
    b = bases.replace("public", " ").replace("protected", " ").replace("private", " ").replace("virtual", " ")
    # Split by commas at top-level (ignore templates)
    parts = [p.strip() for p in re.split(r",(?![^<]*>)", b) if p.strip()]
    out = []
    for p in parts:
        # Remove trailing qualifiers and ptr/ref
        p = re.sub(r"\b(final|noexcept|override)\b", " ", p)
        p = p.replace("&", " ").replace("*", " ")
        p = re.sub(r"\s+", " ", p).strip()
        p = re.sub(r"\b(class|struct)\s+", "", p)
        out.append(p.strip())
    return out

def _rel_include_for(ci, bases):
    if not ci.src_path:
        return None
    sp = ci.src_path.resolve()
    for b in bases:
        try:
            rp = sp.relative_to(Path(b).resolve())
            return str(rp).replace('\\', '/')
        except Exception:
            pass
    return ci.src_path.name

def append_factories(out_path: Path, unit: str, classes_for_factories, src_bases):
    with out_path.open("a", encoding="utf-8") as out:
        out.write("\n// ===== Auto-generated factories (QHT) =====\n")

        seen = set()
        for ci in classes_for_factories:
            inc = _rel_include_for(ci, src_bases)
            if inc and inc not in seen:
                out.write(f'#include "{inc}"\n')
                seen.add(inc)

        out.write('#include "EngineGlobals.h"\n')
        out.write('#include "GarbageCollector.h"\n')

        ns = f"qht_factories_gen_{unit}"
        out.write(f"namespace {ns} {{\n")
        out.write(f"    static void RegisterFactories_{unit}()\n")
        out.write( "    {\n")
        for ci in classes_for_factories:
            if ci.name in ("QObject", "QObjectBase"):
                continue
            out.write(f'        qht_factories::RegisterIfCreatable<{ci.name}>("{ci.name}");\n')
        out.write( "    }\n")
        out.write(f"    struct FAutoReg_{unit} {{ FAutoReg_{unit}() {{ RegisterFactories_{unit}(); }} }};\n")
        out.write(f"    static FAutoReg_{unit} GAutoReg_{unit};\n")
        out.write( "}\n")

def resolve_qobject_flags(class_list):
    # Build map name -> ClassInfo
    cmap = {c.name: c for c in class_list}
    base_cache = {}
    def is_qobj_name(name, stack=None):
        # Direct anchor
        if name == "QObject" or name.endswith("::QObject"):
            return True
        ci = cmap.get(name)
        if not ci:
            return False
        if name in base_cache:
            return base_cache[name]
        if name == "QObject":
            base_cache[name] = True
            return True
        stack = stack or set()
        if name in stack:
            # Break cycles conservatively
            base_cache[name] = False
            return False
        stack.add(name)
        bases = _extract_base_names(ci.bases)
        res = False
        for bname in bases:
            if bname == "QObject" or bname.endswith("::QObject"):
                res = True
                break
            if is_qobj_name(bname, stack):
                res = True
                break
        base_cache[name] = res
        return res
    # Attach a cache flag for each class
    for c in class_list:
        c._is_qobject_cache = is_qobj_name(c.name) or any(
            bn == "QObject" or bn.endswith("::QObject") for bn in _extract_base_names(c.bases)
        )
    return class_list

def split_params(params_str: str):
    params_str = params_str.strip()
    if not params_str:
        return []
    parts = [p.strip() for p in PARAM_SPLIT.split(params_str)]
    out = []
    for p in parts:
        p = re.sub(r"\s*=.*$", "", p)
        if not p:
            continue
        toks = p.split()
        if len(toks) == 1:
            out.append((toks[0], ""))
        else:
            name = toks[-1]
            type_str = p[:p.rfind(name)].strip()
            out.append((type_str, name))
    return out

def find_classes(src: str):
    pos = 0
    while True:
        m = CLASS_RE.search(src, pos)
        if not m:
            break
        name = m.group('name')
        bases = m.group('bases') or ""
        # find matching closing brace for class body
        start = m.end()
        brace = 1
        i = start
        while i < len(src) and brace > 0:
            if src[i] == '{':
                brace += 1
            elif src[i] == '}':
                brace -= 1
            i += 1
        body = src[start:i-1]
        yield name, bases, body
        pos = i

def scan_file(path: Path):
    raw = path.read_text(encoding='utf-8', errors='ignore')
    src = strip_comments(raw)
    classes = []
    for cname, bases, body in find_classes(src):
        ci = ClassInfo(cname, src_path=path, bases=bases)
        # Properties
        for pm in PROP_RE.finditer(body):
            meta_s = pm.group('meta')
            decl = pm.group('decl').strip()

            # Strip initializer like "= 0", "= {}", "= SomeCtor(args)" at end of declaration
            decl_no_init = re.sub(r"\s*=\s*[^;]+$", "", decl).strip()

            # Extract name and type
            name = decl_no_init.split()[-1]
            type_str = decl_no_init[:decl_no_init.rfind(name)].strip()

            ci.properties.append(Property(type_str, name, parse_meta_list(meta_s)))

        # Functions
        for fm in FUNC_RE.finditer(body):
            meta_s = fm.group('meta')
            ret = fm.group('ret')
            fname = fm.group('name')
            params = split_params(fm.group('params') or "")
            ci.functions.append(Function(ret, fname, params, parse_meta_list(meta_s)))
        # Append all classes; filter later after resolving inheritance
        classes.append(ci)
    return classes

def make_rel_include(p: Path, bases):
    pr = p.resolve()
    for b in bases:
        try:
            br = Path(b).resolve()
            if str(pr).startswith(str(br)):
                return str(pr)[len(str(br))+1:].replace("\\", "/")
        except Exception:
            pass
    return p.name  # fallback

HEADER_H = r"""// Auto-generated by QHT. Do not edit.
#pragma once
#include <cstddef>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include "qmeta_runtime.h"
using namespace qmeta;
"""

def emit_header(classes, out_path: Path, unit: str, bases, qobject_names: set[str]):
    lines = [HEADER_H, f"// Unit: {unit}\n\n"]

    class_map = {c.name: c for c in classes}

    # Unique includes
    seen = set()
    incs = []
    for ci in classes:
        if getattr(ci, "src_path", None):
            inc = make_rel_include(ci.src_path, bases)
            if inc not in seen:
                seen.add(inc)
                incs.append(inc)
    for inc in sorted(incs):
        lines.append(f'#include "{inc}"\n')
    lines.append("\n")

    # Emit invokers
    for ci in classes:
        cname = ci.name
        for fn in ci.functions:
            inv_name = f"_qmeta_invoke_{cname}_{fn.name}"
            args_extract = []
            call_args = []
            for idx, (pt, pn) in enumerate(fn.params):
                var = f"_a{idx}"
                as_t = _canonical_as_type(pt)
                args_extract.append(f"    auto {var} = args[{idx}].as<{as_t}>();\n")
                call_args.append(var)

            need_count = len(fn.params)
            argc_guard = ""
            if need_count > 0:
                argc_guard = (
                    f"    if (argc < {need_count}) "
                    f"throw std::runtime_error(\"{cname}::{fn.name} requires {need_count} args\");\n"
                )

            if fn.ret.strip() == "void":
                body = (
                        f"static Variant {inv_name}(void* Self, const Variant* args, size_t argc) {{\n"
                        f"    (void)argc; auto* self = static_cast<{cname}*>(Self);\n"
                        f"{argc_guard}"
                        + "".join(args_extract) +
                        f"    self->{fn.name}({', '.join(call_args)});\n"
                        f"    return Variant();\n}}\n\n"
                )
            else:
                body = (
                        f"static Variant {inv_name}(void* Self, const Variant* args, size_t argc) {{\n"
                        f"    (void)argc; auto* self = static_cast<{cname}*>(Self);\n"
                        f"{argc_guard}"
                        + "".join(args_extract) +
                        f"    auto _ret = self->{fn.name}({', '.join(call_args)});\n"
                        f"    return Variant(_ret);\n}}\n\n"
                )
            lines.append(body)

    # Emit registry adder
    lines.append(f"inline void QHT_Register_{unit}(Registry& R) {{\n")
    for ci in classes:
        cname = ci.name
        lines.append(f"    TypeInfo& T_{cname} = R.add_type(\"{cname}\", sizeof({cname}));\n")
        lines.append(f'    T_{cname}.meta = MetaMap{{ std::make_pair(std::string("Module"), std::string("{unit}")) }};\n')

        bases = _extract_base_names(ci.bases)
        if bases:
            base_name = bases[0]
            lines.append(f'    T_{cname}.base_name = "{base_name}";\n')

        for p in ci.properties:
            meta_items = ", ".join([f"std::make_pair(std::string(\"{k}\"), std::string(\"{v}\"))" for k,v in p.meta])
            meta_code = f"MetaMap{{ {meta_items} }}" if meta_items else "MetaMap{}"
            mask = classify_gc_flags(p.type, qobject_names)
            flags_code = gcflags_expr(mask)
            lines.append(
                f"    T_{cname}.properties.push_back(MetaProperty{{\"{p.name}\", \"{p.type}\", "
                f"offsetof({cname}, {p.name}), {meta_code}, {flags_code} }});\n"
            )
        for f in ci.functions:
            params_vec = ", ".join([f"MetaParam{{\"{pn}\", \"{pt}\"}}" for (pt,pn) in f.params])
            meta_items = ", ".join([f"std::make_pair(std::string(\"{k}\"), std::string(\"{v}\"))" for k,v in f.meta])
            meta_code = f"MetaMap{{ {meta_items} }}" if meta_items else "MetaMap{}"
            inv_name = f"_qmeta_invoke_{cname}_{f.name}"
            lines.append("    {\n")
            lines.append( "        MetaFunction F;\n")
            lines.append(f"        F.name = \"{f.name}\";\n")
            lines.append(f"        F.return_type = \"{f.ret}\";\n")
            lines.append(f"        F.invoker = &{inv_name};\n")
            lines.append(f"        F.params = std::vector<MetaParam>{{ {params_vec} }};\n")
            lines.append(f"        F.meta = {meta_code};\n")
            lines.append( "        T_{cname}.functions.push_back(std::move(F));\n".replace("{cname}", cname))
            lines.append( "    }\n")
    lines.append("}\n")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("".join(lines), encoding="utf-8")

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--unit', required=True, help='Module name: Engine or Game')
    ap.add_argument('--src', required=True, help='Semicolon-separated source dirs to scan')
    ap.add_argument('--out', required=True, help='Output header path (*.qht.gen.hpp)')
    ap.add_argument('--emit', required=False, default=None, help='Semicolon-separated dirs: only classes under these dirs will be emitted (ancestry can be resolved from all --src dirs)')
    args = ap.parse_args()

    src_dirs = [Path(p) for p in args.src.split(';') if p]
    emit_dirs = [Path(p) for p in (args.emit.split(';') if args.emit else []) if p]

    # 1) Scan all headers
    classes_all = []
    for sd in src_dirs:
        for ext in (".h", ".hpp", ".hh"):
            for p in sd.rglob(f"*{ext}"):
                classes_all.extend(scan_file(p))

    # 2) Resolve QObject ancestry globally
    resolve_qobject_flags(classes_all)

    # Collect all QObject-derived names from ALL scanned classes (Engine+Game)
    qobject_names = {c.name for c in classes_all if getattr(c, "_is_qobject_cache", False)}
    qobject_names.add("QObject")

    # 3) Filter: QObject-derived (direct/indirect), or QObject itself
    classes = []
    for ci in classes_all:
        if not (ci.is_qobject() and (ci.has_any_marks() or ci.name == "QObject")):
            continue
        if emit_dirs:
            sp = ci.src_path.resolve() if ci.src_path else None
            if not sp:
                continue
            inside = any(str(sp).startswith(str(d.resolve())) for d in emit_dirs)
            if not inside:
                continue
        classes.append(ci)

    # 4) Emit
    emit_header(classes, Path(args.out), args.unit, bases=[str(p) for p in src_dirs], qobject_names=qobject_names)

    # 5) Register object factories
    classes_factories = []
    for ci in classes_all:
        if not ci.is_qobject():
            continue
        if emit_dirs:
            sp = ci.src_path.resolve() if ci.src_path else None
            if not sp:
                continue
            inside = any(str(sp).startswith(str(d.resolve())) for d in emit_dirs)
            if not inside:
                continue
        classes_factories.append(ci)

    append_factories(Path(args.out), args.unit, classes_factories, src_dirs)
