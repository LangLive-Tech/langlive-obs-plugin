#!/usr/bin/env bash
# Saves every dialog and notice of the plugin as PNGs in each UI language,
# rendered offscreen by the UI test binary, and writes index.html showing the
# languages side by side, to review line breaks and copy without OBS.
#
#   scripts/dev/ui-screenshots.sh [output-dir]     (default: build_ui_tests/screenshots)
#
# Needs the UI tests built (see AGENTS.md). Rendering uses this machine's
# fonts: a Mac shows what macOS streamers see, not what Windows ones do.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build_ui_tests/screenshots}"
BIN="$ROOT/build_ui_tests/langlive-ui-tests"
LOCALES=(zh-TW zh-CN en-US)
VERSION="$(jq -r .version "$ROOT/buildspec.json")"
# OBS's fonts (Open Sans), from the obs-studio sources the plugin builds against.
FONTS="$(ls -d "$ROOT"/.deps/obs-studio-*/frontend/forms/fonts 2>/dev/null | head -1 || true)"
# OBS's default theme, compiled as OBS does, from the installed OBS (or
# OBS_THEMES_DIR): the plugin's dialogs inherit it inside OBS.
THEMES="${OBS_THEMES_DIR:-/Applications/OBS.app/Contents/Resources/themes}"
THEME_QSS=""
if [ -d "$THEMES" ]; then
	THEME_QSS="$(mktemp "${TMPDIR:-/tmp}/obs-theme.XXXXXX")"
	trap 'rm -f "$THEME_QSS"' EXIT
	python3 "$ROOT/scripts/dev/obs-theme-qss.py" "$THEMES" >"$THEME_QSS"
else
	echo "warning: no OBS themes at $THEMES; screenshots without OBS's theme" >&2
fi

[ -x "$BIN" ] || { echo "error: build the UI tests first ($BIN)" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
for locale in "${LOCALES[@]}"; do
	QT_FONT_DPI=72 LANGLIVE_OBS_THEME_QSS="$THEME_QSS" LANGLIVE_OBS_FONTS_DIR="$FONTS" LANGLIVE_SCREENSHOT_VERSION="$VERSION" LANGLIVE_TEST_LOCALE="$locale" LANGLIVE_SCREENSHOT_DIR="$OUT/$locale" \
		"$BIN" -tc='screenshots*' >/dev/null 2>&1
	echo "$locale: $(ls "$OUT/$locale"/*.png | wc -l | tr -d ' ') screenshots"
done

python3 - "$OUT" "$VERSION" "$(git -C "$ROOT" rev-parse --short HEAD)" "$ROOT/scripts/dev/ui-screenshots-notes.tsv" "${LOCALES[@]}" <<'PY'
import html, pathlib, sys, datetime
out, version, commit, notes_path, locales = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:]
names = {"zh-TW": "繁體中文", "zh-CN": "简体中文", "en-US": "English"}
shots = [line.split("\t", 1) for line in (out / locales[0] / "shots.tsv").read_text(encoding="utf-8").splitlines()]
# When each screen shows and what to check (scripts/dev/ui-screenshots-notes.tsv).
notes = {}
for line in pathlib.Path(notes_path).read_text(encoding="utf-8").splitlines():
    name, when, check = line.split("\t")
    notes[name] = (when, check)
rows = []
for name, caption in shots:
    cells = "".join(
        f'<figure><figcaption>{names.get(l, l)}</figcaption>'
        f'<a href="{l}/{name}.png" target="_blank"><img src="{l}/{name}.png" alt="{html.escape(caption)} {l}" loading="lazy"></a></figure>'
        for l in locales)
    when, check = notes.get(name, ("", ""))
    info = (f'<dl><dt>情境</dt><dd>{html.escape(when)}</dd><dt>檢查點</dt><dd>{html.escape(check)}</dd></dl>'
            if when or check else "")
    rows.append(f'<section id="{name}"><h2>{html.escape(caption)} <code>{name}</code></h2>{info}<div class="row">{cells}</div></section>')
toc = "".join(f'<a href="#{n}">{html.escape(c)}</a>' for n, c in shots)
stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
(out / "index.html").write_text(f"""<!doctype html>
<html lang="zh-Hant"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>OBS Plugin UI Text Review</title>
<style>
:root {{ color-scheme: light dark; --bg:#f5f5f7; --fg:#1d1d1f; --card:#fff; --muted:#6e6e73; --line:#d2d2d7; }}
@media (prefers-color-scheme: dark) {{ :root {{ --bg:#161617; --fg:#f5f5f7; --card:#232325; --muted:#a1a1a6; --line:#3a3a3c; }} }}
body {{ margin:0; font-family:-apple-system,"PingFang TC","Microsoft JhengHei",sans-serif; background:var(--bg); color:var(--fg); }}
header {{ padding:20px 24px 8px; }} header p {{ color:var(--muted); margin:4px 0; font-size:14px; }}
nav {{ display:flex; flex-wrap:wrap; gap:6px 12px; padding:8px 24px 16px; font-size:13px; border-bottom:1px solid var(--line); }}
nav a {{ color:var(--muted); text-decoration:none; }} nav a:hover {{ color:var(--fg); }}
section {{ padding:16px 24px; border-bottom:1px solid var(--line); }}
h2 {{ font-size:16px; margin:0 0 12px; }} h2 code {{ font-size:12px; color:var(--muted); font-weight:normal; }}
.row {{ display:flex; gap:16px; overflow-x:auto; align-items:flex-start; }}
dl {{ display:grid; grid-template-columns:max-content 1fr; gap:4px 12px; margin:0 0 12px; font-size:14px; max-width:900px; }}
dt {{ color:var(--muted); }} dd {{ margin:0; }}
figure {{ margin:0; background:var(--card); border-radius:8px; padding:8px; flex:none; }}
figcaption {{ font-size:12px; color:var(--muted); margin-bottom:6px; }}
img {{ display:block; zoom:0.5; }}
</style></head><body>
<header><h1>浪LIVE OBS 插件介面文字檢查</h1>
<p>版本 {html.escape(version)}（{html.escape(commit)}），{stamp} 產生；於 macOS 以無畫面模式繪製，套用 OBS 預設佈景主題與字型；Windows 字型不同，斷行可能略有差異。</p>
<p>每個畫面附「情境」（何時出現）與「檢查點」（請確認的項目）；共通檢查：文字完整未被截斷、斷行位置自然、按鈕文字完整。點圖片可開啟原尺寸。</p></header>
<nav>{toc}</nav>
{''.join(rows)}
</body></html>
""", encoding="utf-8")
print(f"wrote {out / 'index.html'}")
PY
