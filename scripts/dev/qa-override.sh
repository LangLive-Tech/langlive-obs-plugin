#!/usr/bin/env bash
# Points a QA build of the plugin at another update manifest by writing
# qa-override.json into the plugin's config folder (macOS, or Git Bash on
# Windows). Production builds ignore the file.
#
#   qa-override.sh optional|forced|uptodate|bad-sha|broken   use a published scenario
#   qa-override.sh https://.../manifest.json                use any manifest
#   qa-override.sh status                                   show the current override
#   qa-override.sh clear                                    remove it (back to the built-in manifest)
#
# Scenario names need the scenarios folder URL (the one that holds
# optional.json, ...), from LANGLIVE_QA_SCENARIOS_URL or the file
# ~/.config/langlive-obs-plugin/qa-scenarios-url. The URL is not in this
# repository; ask the team for it.
#
# The plugin reads the file on every update check: no need to restart OBS,
# just click "Check for updates" again.
set -euo pipefail

scenarios="optional forced uptodate bad-sha broken"
url_file="$HOME/.config/langlive-obs-plugin/qa-scenarios-url"

usage() {
	sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
	exit "${1:-0}"
}

config_dir() {
	if [[ -n "${APPDATA:-}" ]]; then
		# Git Bash on Windows
		echo "$(cygpath -u "$APPDATA" 2>/dev/null || echo "$APPDATA")/obs-studio/plugin_config/langlive-obs-plugin"
	else
		echo "$HOME/Library/Application Support/obs-studio/plugin_config/langlive-obs-plugin"
	fi
}

scenarios_url() {
	local base="${LANGLIVE_QA_SCENARIOS_URL:-}"
	if [[ -z "$base" && -f "$url_file" ]]; then
		base="$(tr -d '[:space:]' <"$url_file")"
	fi
	if [[ -z "$base" ]]; then
		echo "error: set LANGLIVE_QA_SCENARIOS_URL or write the scenarios folder URL to $url_file" >&2
		exit 1
	fi
	echo "${base%/}"
}

[[ $# -eq 1 ]] || usage 1
file="$(config_dir)/qa-override.json"

case "$1" in
-h | --help) usage ;;
status)
	if [[ -f "$file" ]]; then
		echo "$file:"
		cat "$file"
		echo
	else
		echo "no override: the plugin uses its built-in manifest"
	fi
	exit 0
	;;
clear)
	rm -f "$file"
	echo "removed $file"
	exit 0
	;;
https://*) url="$1" ;;
*)
	if [[ " $scenarios " != *" $1 "* ]]; then
		echo "error: unknown scenario '$1' (one of: $scenarios, status, clear, or an https:// URL)" >&2
		exit 1
	fi
	url="$(scenarios_url)/$1.json"
	;;
esac

# Check the manifest can be downloaded before switching ("broken" downloads
# fine; only its contents are invalid).
status_code="$(curl -s -o /dev/null -w '%{http_code}' "$url" || true)"
if [[ "$status_code" != "200" ]]; then
	echo "error: $url answered HTTP $status_code" >&2
	exit 1
fi

mkdir -p "$(dirname "$file")"
printf '{"update_manifest_url": "%s"}\n' "$url" >"$file"
echo "wrote $file"
echo "  -> $url"
echo "In OBS, click LangLive > Check for updates."
