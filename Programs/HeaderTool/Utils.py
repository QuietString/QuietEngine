import re

def _strip_cvref(s: str) -> str:
    # remove leading/trailing const/volatile and references
    s = re.sub(r'^\s*(const|volatile)\s+', '', s)
    s = re.sub(r'\s+(const|volatile)\s*$', '', s)
    s = s.replace('&', '').replace('&&', '')
    s = re.sub(r'\s+', ' ', s).strip()
    return s

def _remove_trailing_ptr(s: str) -> tuple[str, int]:
    # remove trailing '*' tokens and return (base, count)
    cnt = 0
    while s.endswith('*'):
        s = s[:-1].rstrip()
        cnt += 1
    return s, cnt

def _first_template_arg(tmpl: str) -> str | None:
    """
    Extract first template argument (handles 'std::vector<T*>' or 'std::vector<T*, Alloc>').
    Returns None if not a template or malformed.
    """
    m = re.match(r'^([A-Za-z_]\w*::)*[A-Za-z_]\w*\s*<(.+)>$', tmpl.strip())
    if not m:
        return None
    inside = m.group(2)
    # split by comma at top-level (ignore nested templates)
    depth = 0; token = []
    for i, ch in enumerate(inside):
        if ch == '<': depth += 1
        elif ch == '>': depth -= 1
        elif ch == ',' and depth == 0:
            break
        token.append(ch)
    return "".join(token).strip()

def _unqual_name(t: str) -> str:
    # get last identifier without namespace qualifiers
    t = t.strip()
    t = re.sub(r'\s+', ' ', t)
    if '::' in t:
        t = t.split('::')[-1]
    return t

def classify_gc_flags(type_str: str, class_map: dict[str, 'ClassInfo']) -> int:
    """
    Returns PF_* bitmask as int.
    PF_RawQObjectPtr      if 'T*' and T : QObject
    PF_VectorOfQObjectPtr if 'std::vector<U*>' and U : QObject
    """
    PF_None = 0
    PF_Raw  = 1 << 0
    PF_Vec  = 1 << 1

    s = type_str.strip()

    # 1) std::vector<...> case
    if s.startswith('std::vector'):
        elem = _first_template_arg(s)
        if elem:
            elem = _strip_cvref(elem)
            base, stars = _remove_trailing_ptr(elem)
            if stars >= 1:
                qname = _unqual_name(base)
                ci = class_map.get(qname)
                if ci and getattr(ci, "_is_qobject_cache", False):
                    return PF_Vec
        return PF_None

    # 2) Raw pointer T*
    base = _strip_cvref(s)
    base, stars = _remove_trailing_ptr(base)
    if stars == 1:
        qname = _unqual_name(base)
        ci = class_map.get(qname)
        if ci and getattr(ci, "_is_qobject_cache", False):
            return PF_Raw

    return PF_None

def gcflags_expr(mask: int) -> str:
    if mask == 0: return "PF_None"
    parts = []
    if mask & (1 << 0): parts.append("PF_RawQObjectPtr")
    if mask & (1 << 1): parts.append("PF_VectorOfQObjectPtr")
    return " | ".join(parts)