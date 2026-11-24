#!/usr/bin/env bash
# Writes the update test scenarios QA points qa-override.json at, to
# <prefix>/scenarios/ (test/scenarios/ by deploy:qa), for the installers just
# published at <prefix>/<version>/:
#
#   optional.json  99.0.0, min 0.0.0      an update that can wait
#   forced.json    99.0.0, min 99.0.0     an update required before logging in
#   uptodate.json  0.0.1, min 0.0.1       already up to date
#   bad-sha.json   99.0.0, wrong SHA-256  the download is refused
#   broken.json    not JSON               the check fails
#
# 99.0.0 is newer than any QA build, so every build offers it; the
# installers are the real ones, so installing works.
#
#   scripts/ci/publish-scenarios.sh <prefix>
#
# Environment: S3_BUCKET, S3_REGION, AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY
# for the prefix, RELEASE_DIR (default release/).

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

PREFIX="${1:?usage: publish-scenarios.sh <prefix>}"
: "${S3_BUCKET:?S3_BUCKET is not set}"
: "${S3_REGION:?S3_REGION is not set}"
[ -n "${AWS_ACCESS_KEY_ID:-}" ] || fail "AWS_ACCESS_KEY_ID is not set"
[ -n "${AWS_SECRET_ACCESS_KEY:-}" ] || fail "AWS_SECRET_ACCESS_KEY is not set"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NAME="$(jq -r .name "$ROOT/buildspec.json")"
VERSION="$(jq -r .version "$ROOT/buildspec.json")"
DIR="$ROOT/${RELEASE_DIR:-release}"
ORIGIN="https://$S3_BUCKET.s3.$S3_REGION.amazonaws.com/$PREFIX"
PKG="$NAME-macos-universal-v$VERSION.pkg"
EXE="$NAME-windows-x64-v$VERSION.exe"
for file in "$PKG" "$EXE"; do
	[ -f "$DIR/$file" ] || fail "missing $DIR/$file"
done
PKG_SHA="$(shasum -a 256 "$DIR/$PKG" | cut -d' ' -f1)"
EXE_SHA="$(shasum -a 256 "$DIR/$EXE" | cut -d' ' -f1)"
WRONG_SHA="0000000000000000000000000000000000000000000000000000000000000000"
RELEASED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

manifest() { # <version> <min> <pkg sha> <exe sha>
	jq -n --arg version "$1" --arg releasedAt "$RELEASED_AT" --arg min "$2" \
		--arg pkgUrl "$ORIGIN/$VERSION/$PKG" --arg pkgSha "$3" \
		--arg exeUrl "$ORIGIN/$VERSION/$EXE" --arg exeSha "$4" \
		'{version: $version, released_at: $releasedAt, min_supported_version: $min,
		  platforms: {macos: {url: $pkgUrl, sha256: $pkgSha}, "windows-x64": {url: $exeUrl, sha256: $exeSha}}}'
}

put() { # <name> <content>
	local sha
	sha="$(printf '%s' "$2" | shasum -a 256 | cut -d' ' -f1)"
	printf 'user = "%s:%s"\n' "$AWS_ACCESS_KEY_ID" "$AWS_SECRET_ACCESS_KEY" |
		curl -K - --aws-sigv4 "aws:amz:$S3_REGION:s3" -sS --fail-with-body -X PUT --data-binary "$2" \
			-H "x-amz-content-sha256: $sha" -H "Content-Type: application/json" -H "Cache-Control: no-cache" \
			"$ORIGIN/scenarios/$1" >/dev/null
	echo "Published $ORIGIN/scenarios/$1"
}

put optional.json "$(manifest 99.0.0 0.0.0 "$PKG_SHA" "$EXE_SHA")"
put forced.json "$(manifest 99.0.0 99.0.0 "$PKG_SHA" "$EXE_SHA")"
put uptodate.json "$(manifest 0.0.1 0.0.1 "$PKG_SHA" "$EXE_SHA")"
put bad-sha.json "$(manifest 99.0.0 0.0.0 "$WRONG_SHA" "$WRONG_SHA")"
put broken.json '{"version": "99.0.0", "platforms": '
