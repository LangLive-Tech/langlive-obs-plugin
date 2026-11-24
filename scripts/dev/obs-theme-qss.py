#!/usr/bin/env python3
"""Compiles an OBS Studio theme into the Qt style sheet OBS applies, for
screenshots that look like the plugin inside OBS (scripts/dev/ui-screenshots.sh).

    obs-theme-qss.py <themes dir> [variant id] [font scale] [padding] > theme.qss

Follows OBSApp_Themes.cpp in obs-studio: variables from @OBSThemeVars blocks of
the base theme and its variant (os_mac_ names override on macOS), var(--x),
calc() / min() / max() with px or pt suffixes, then var(--x) substituted in the
style sheets. Defaults: the default theme (Yami, "Default" variant), font scale
10, padding 4 (OBS's default density).
"""
import pathlib, re, sys

themes = pathlib.Path(sys.argv[1])
variant_id = sys.argv[2] if len(sys.argv) > 2 else "com.obsproject.Yami.Original"
font_scale = float(sys.argv[3]) if len(sys.argv) > 3 else 10
padding = float(sys.argv[4]) if len(sys.argv) > 4 else 4

def meta(text):
    m = re.search(r"@OBSThemeMeta\s*\{(.*?)\}", text, re.S)
    return dict(re.findall(r"(\w+)\s*:\s*'([^']*)'", m.group(1))) if m else {}

files = {}
for path in list(themes.glob("*.obt")) + list(themes.glob("*.ovt")):
    text = path.read_text(encoding="utf-8")
    files[meta(text).get("id")] = text
chain = []
tid = variant_id
while tid in files:
    chain.insert(0, files[tid])
    tid = meta(files[tid]).get("extends")

raw = {}
sheets = []
for text in chain:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    for block in re.findall(r"@OBSThemeVars\s*\{(.*?)\}", text, re.S):
        for key, value in re.findall(r"--([\w-]+)\s*:\s*([^;]+);", block):
            if key.startswith("os_mac_"):
                key = key[len("os_mac_"):]
            raw[key] = value.strip()
    text = re.sub(r"@OBSThemeMeta\s*\{.*?\}", "", text, flags=re.S)
    text = re.sub(r"@OBSThemeVars\s*\{.*?\}", "", text, flags=re.S)
    sheets.append(text)
raw["obsFontScale"] = str(font_scale)
raw["obsPadding"] = str(padding)

NUM = re.compile(r"^(-?\d+(?:\.\d+)?)(px|pt)?$")
cache = {}

def resolve(name, depth=0):
    if name in cache:
        return cache[name]
    value = evaluate(raw.get(name, "0"), depth + 1)
    cache[name] = value
    return value

def evaluate(expr, depth=0):
    expr = expr.strip()
    expr = re.sub(r"var\(--([\w-]+)\)", lambda m: resolve(m.group(1), depth), expr)
    while True:
        m = re.search(r"(calc|min|max)\(([^()]*)\)", expr)
        if not m:
            break
        expr = expr[:m.start()] + math(m.group(1), m.group(2)) + expr[m.end():]
    return expr

def split_num(token):
    m = NUM.match(token.strip())
    return (float(m.group(1)), m.group(2) or "") if m else (None, token)

def fmt(value, suffix):
    return (f"{value:.4f}".rstrip("0").rstrip(".") or "0") + suffix

def math(kind, body):
    if kind in ("min", "max"):
        parts = [split_num(p) for p in body.split(",")]
        if any(v is None for v, _ in parts):
            return f"{kind}({body})"
        pick = (min if kind == "min" else max)(parts, key=lambda p: p[0])
        suffix = next((s for _, s in parts if s), "")
        return fmt(pick[0], suffix)
    m = re.match(r"\s*(\S+)\s*([-+*/])\s*(\S+)\s*$", body)
    if not m:
        return body.strip()
    (a, sa), op, (b, sb) = split_num(m.group(1)), m.group(2), split_num(m.group(3))
    if a is None or b is None:
        return body.strip()
    value = {"+": a + b, "-": a - b, "*": a * b, "/": a / b if b else 0}[op]
    return fmt(value, sa or sb)

qss = "\n".join(sheets)
qss = re.sub(r"var\(--([\w-]+)\)", lambda m: resolve(m.group(1)), qss)
qss = re.sub(r"url\(theme:[^)]*\)", "none", qss)  # icons are not needed here
sys.stdout.write(qss)
