#!/usr/bin/env bash
# Creates the GitLab Release of a version tag with both builds' installers:
# release/qa/ (QA, named "...-vX.Y.Z-qa.*") and release/prod/ (production).
# Run by the CI release job after the four builds.
#
# The installers go to the project's generic package registry, which keeps
# them (job artifacts expire); deploy:prod takes the production ones from
# there. The description lists their SHA-256 and the production status, which
# scripts/ci/deploy-prod.sh updates when the version is released.

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

: "${CI_COMMIT_TAG:?run in a tag pipeline}"
: "${CI_API_V4_URL:?run in GitLab CI}"
: "${CI_PROJECT_ID:?run in GitLab CI}"
: "${CI_JOB_TOKEN:?run in GitLab CI}"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NAME="$(jq -r .name "$ROOT/buildspec.json")"
VERSION="$(jq -r .version "$ROOT/buildspec.json")"
API="$CI_API_V4_URL/projects/$CI_PROJECT_ID"
AUTH="JOB-TOKEN: $CI_JOB_TOKEN"
PACKAGE_URL="$API/packages/generic/$NAME/$VERSION"

status="$(curl -s -o /dev/null -w '%{http_code}' -H "$AUTH" "$API/releases/$CI_COMMIT_TAG")"
[ "$status" = 404 ] ||
	fail "the release $CI_COMMIT_TAG exists (HTTP $status); to publish the tag again use scripts/release/retag.sh"

# Uploads one build's installers; adds to $links and to the named rows variable.
links='[]'
prod_rows=''
qa_rows=''
attach() { # <env dir> <suffix> <rows variable>
	local dir="$ROOT/release/$1" suffix="$2" base name row
	for base in "$NAME-macos-universal-v$VERSION.pkg" "$NAME-windows-x64-v$VERSION.exe"; do
		[ -f "$dir/$base" ] || fail "missing $dir/$base (from the build jobs' artifacts)"
		name="${base%.*}$suffix.${base##*.}"
		echo "Uploading $name"
		curl -sS --fail-with-body -H "$AUTH" --upload-file "$dir/$base" "$PACKAGE_URL/$name" >/dev/null
		links="$(jq -c --arg name "$name" --arg url "$PACKAGE_URL/$name" \
			'. + [{name: $name, url: $url, link_type: "package", direct_asset_path: ("/" + $name)}]' <<<"$links")"
		row="| \`$name\` | \`$(shasum -a 256 "$dir/$base" | cut -d' ' -f1)\` |"
		printf -v "$3" '%s%s\n' "${!3}" "$row"
	done
}
attach prod "" prod_rows
attach qa -qa qa_rows
[ "$(jq length <<<"$links")" = 4 ] || fail "expected 4 installers"

description="Installers of $VERSION, built by pipeline ${CI_PIPELINE_URL:-} from $CI_COMMIT_SHORT_SHA.
The macOS installers are signed with a Developer ID and notarized by Apple; the Windows installers are not
code-signed yet.

### Production

Status: not released yet (deploy:prod)

| File | SHA-256 |
|---|---|
$prod_rows

### QA (test)

The QA backend and website; not for streamers.

| File | SHA-256 |
|---|---|
$qa_rows"

body="$(jq -n --arg tag "$CI_COMMIT_TAG" --arg name "$NAME $CI_COMMIT_TAG" --arg description "$description" \
	--argjson links "$links" '{tag_name: $tag, name: $name, description: $description, assets: {links: $links}}')"
curl -sS --fail-with-body -H "$AUTH" -H 'Content-Type: application/json' -d "$body" "$API/releases" >/dev/null
echo "Created the release $CI_COMMIT_TAG with $(jq length <<<"$links") installers"
