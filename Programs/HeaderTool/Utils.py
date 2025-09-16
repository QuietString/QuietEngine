import re

def _strip_cvref(s: str) -> str:
    s = re.sub(r'^\s*(const|volatile)\s+', '', s)
    s = re.sub(r'\s+(const|volatile)\s*$', '', s)
    s = s.replace('&', '').replace('&&', '')
    s = re.sub(r'\s+', ' ', s).strip()
    return s

def _remove_trailing_ptr(s: str):
    cnt = 0
    while s.endswith('*'):
        s = s[:-1].rstrip()
        cnt += 1
    return s, cnt

def _first_template_arg(tmpl: str):
    m = re.match(r'^([A-Za-z_]\w*::)*[A-Za-z_]\w*\s*<(.+)>$', tmpl.strip())
    if not m:
        return None
    inside = m.group(2)
    depth = 0; token = []
    for ch in inside:
        if ch == '<': depth += 1
        elif ch == '>': depth -= 1
        elif ch == ',' and depth == 0:
            break
        token.append(ch)
    return "".join(token).strip()

def _unqual_name(t: str) -> str:
    t = re.sub(r'\s+', ' ', t.strip())
    return t.split('::')[-1]

def _is_qobject_by_name(name: str, qset: set[str]) -> bool:
    n = _unqual_name(name)
    # direct QObject or any known QObject-derived collected from all scanned headers
    return (n == "QObject") or (n in qset)

def classify_gc_flags(type_str: str, qset: set[str]) -> int:
    PF_None = 0
    PF_Raw  = 1 << 0
    PF_Vec  = 1 << 1

    s = type_str.strip()

    # std::vector<T*>
    if s.startswith('std::vector'):
        elem = _first_template_arg(s)
        if elem:
            elem = _strip_cvref(elem)
            base, stars = _remove_trailing_ptr(elem)
            if stars >= 1 and _is_qobject_by_name(base, qset):
                return PF_Vec
        return PF_None

    # raw T*
    base = _strip_cvref(s)
    base, stars = _remove_trailing_ptr(base)
    if stars == 1 and _is_qobject_by_name(base, qset):
        return PF_Raw

    return PF_None

def gcflags_expr(mask: int) -> str:
    if mask == 0: return "PF_None"
    parts = []
    if mask & (1 << 0): parts.append("PF_RawQObjectPtr")
    if mask & (1 << 1): parts.append("PF_VectorOfQObjectPtr")
    return " | ".join(parts)
