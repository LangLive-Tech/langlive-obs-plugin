#!/usr/bin/env bash
# Deploys a version tag to production: the production installers attached
# to the tag's GitLab Release (scripts/ci/gitlab-release.sh), the same files
# QA's version was built with, go to S3 prod/<version>/, then prod/latest.json
# offers them (scripts/ci/publish-manifest.sh), and the Release is marked
# released. Run by the manual deploy:prod job. Lists the CDN paths it
# replaced in cdn-flush-paths.txt for flush-cdn:prod.
#
# Environment: PROD_* keys as AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY,
# PROD_DOWNLOAD_URL (the CDN, whose root is the bucket's prod/), RELEASE_DIR.

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

: "${CI_COMMIT_TAG:?run in a tag pipeline}"
: "${CI_API_V4_URL:?run in GitLab CI}"
: "${CI_PROJECT_ID:?run in GitLab CI}"
: "${CI_JOB_TOKEN:?run in GitLab CI}"
: "${PROD_DOWNLOAD_URL:?PROD_DOWNLOAD_URL is not set}"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NAME="$(jq -r .name "$ROOT/buildspec.json")"
VERSION="$(jq -r .version "$ROOT/buildspec.json")"
API="$CI_API_V4_URL/projects/$CI_PROJECT_ID"
AUTH="JOB-TOKEN: $CI_JOB_TOKEN"
DIR="$ROOT/${RELEASE_DIR:-release/prod}"

# Released once prod/latest.json offers it: then installers may be on users'
# machines and cached by the CDN for a year, so they are never replaced and
# a fix needs a new version. Before that, a tag pushed again replaces them.
published="$("$ROOT/scripts/ci/published-version.sh" prod)"
[ "$published" != "$VERSION" ] || fail "$VERSION is released already (prod/latest.json); release a new version"

release="$(curl -sS --fail-with-body -H "$AUTH" "$API/releases/$CI_COMMIT_TAG")" ||
	fail "no release for $CI_COMMIT_TAG; the release job makes it"
description="$(jq -r .description <<<"$release")"

mkdir -p "$DIR"
INSTALLERS=("$NAME-macos-universal-v$VERSION.pkg" "$NAME-windows-x64-v$VERSION.exe")
for base in "${INSTALLERS[@]}"; do
	curl -sS --fail-with-body -H "$AUTH" -o "$DIR/$base" "$API/packages/generic/$NAME/$VERSION/$base"
	expected="$(grep -F "| \`$base\` |" <<<"$description" | sed -E 's/.*\| `([0-9a-f]{64})` \|$/\1/')"
	actual="$(shasum -a 256 "$DIR/$base" | cut -d' ' -f1)"
	[ "$actual" = "$expected" ] || fail "$base: SHA-256 $actual, the release lists ${expected:-none}"
	echo "$base: SHA-256 matches the release"
done

"$ROOT/scripts/ci/upload-s3.sh" "prod/$VERSION" --replace
"$ROOT/scripts/ci/publish-manifest.sh" prod "$PROD_DOWNLOAD_URL"
# The CDN's root is prod/. The flush API takes no wildcards.
{
	echo /latest.json
	for base in "${INSTALLERS[@]}"; do echo "/$VERSION/$base"; done
} >"$ROOT/cdn-flush-paths.txt"

released="Status: released $(date -u +%Y-%m-%d) by pipeline ${CI_PIPELINE_URL:-}"
pending="Status: not released yet (deploy:prod)"
description="${description/"$pending"/$released}"
curl -sS --fail-with-body -H "$AUTH" -H 'Content-Type: application/json' -X PUT \
	-d "$(jq -n --arg description "$description" '{description: $description}')" \
	"$API/releases/$CI_COMMIT_TAG" >/dev/null ||
	echo "warning: released, but could not mark the GitLab Release (update its description by hand)" >&2
echo "$CI_COMMIT_TAG is released"
