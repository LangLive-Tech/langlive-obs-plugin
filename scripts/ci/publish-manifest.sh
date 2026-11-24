#!/usr/bin/env bash
# Writes the update manifest (docs/UPDATE-MANIFEST.md) for the installers in
# $RELEASE_DIR (default release/) and uploads it to S3 as <prefix>/latest.json, after the
# installers are at <prefix>/<version>/ (scripts/ci/upload-s3.sh).
#
#   scripts/ci/publish-manifest.sh <prefix> [<download URL>]
#
# <download URL> is where clients get <prefix>/ from: for prod the CDN, whose
# root is the bucket's prod/; by default the bucket's own HTTPS URL (test and
# dev have no CDN).
#
# Environment: S3_BUCKET, S3_REGION, AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY
# for the prefix.
#
# min_supported_version follows the version policy: a new major version is a
# forced update (min_supported_version becomes this version); a minor or
# patch version is optional (the published manifest's value is kept). The
# first manifest uses this version. UPDATE_MIN_SUPPORTED_VERSION overrides
# it, e.g. to force a patch. A version older than the published one is
# refused.
#
# released_at is when this version was first published here (UTC): now for a
# new version, kept when the same version is published again (e.g. to force
# it with UPDATE_MIN_SUPPORTED_VERSION).
#
# The manifest is the only file replaced in place, so it gets a short
# Cache-Control, which a CDN also honors. The published one is read with the
# prefix's credentials, since prod/ is readable only through the CDN.

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

CHANNEL="${1:?usage: publish-manifest.sh <prefix> [<download URL>]}"
: "${S3_BUCKET:?S3_BUCKET is not set}"
: "${S3_REGION:?S3_REGION is not set}"
[ -n "${AWS_ACCESS_KEY_ID:-}" ] || fail "AWS_ACCESS_KEY_ID is not set"
[ -n "${AWS_SECRET_ACCESS_KEY:-}" ] || fail "AWS_SECRET_ACCESS_KEY is not set"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NAME="$(jq -r .name "$ROOT/buildspec.json")"
VERSION="$(jq -r .version "$ROOT/buildspec.json")"
ENDPOINT="https://$S3_BUCKET.s3.$S3_REGION.amazonaws.com"
ORIGIN="$ENDPOINT/$CHANNEL" # the bucket, never cached
DOWNLOADS="${2:-$ORIGIN}"
DOWNLOADS="${DOWNLOADS%/}"
EMPTY_SHA256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855

s3() {
	printf 'user = "%s:%s"\n' "$AWS_ACCESS_KEY_ID" "$AWS_SECRET_ACCESS_KEY" |
		curl -K - --aws-sigv4 "aws:amz:$S3_REGION:s3" "$@"
}

# The published manifest into $1; returns the HTTP status (200, 404).
read_manifest() {
	s3 -s -o "$1" -w '%{http_code}' -H "x-amz-content-sha256: $EMPTY_SHA256" "$ORIGIN/latest.json"
}
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

DIR="$ROOT/${RELEASE_DIR:-release}"
PKG="$DIR/$NAME-macos-universal-v$VERSION.pkg"
EXE="$DIR/$NAME-windows-x64-v$VERSION.exe"
for file in "$PKG" "$EXE"; do
	[ -f "$file" ] || fail "missing $file"
done

# Each installer must already be uploaded, or clients would be sent to a
# missing file. Checked at the bucket, not the CDN: a request through the CDN
# would cache a file that a failed release may still replace.
for file in "$PKG" "$EXE"; do
	key="$CHANNEL/$VERSION/$(basename "$file")"
	status="$(s3 -s -o /dev/null -I -w '%{http_code}' -H "x-amz-content-sha256: $EMPTY_SHA256" "$ENDPOINT/$key")"
	[ "$status" = 200 ] || fail "$key: HTTP $status; upload the installers first"
done

newer() { # true if $1 > $2
	[ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]
}

# Unreadable is not "none": treating it as the first manifest would force
# every older version to update.
status="$(read_manifest "$TMP")"
case "$status" in
200)
	published_version="$(jq -r '.version // empty' "$TMP")"
	published_min="$(jq -r '.min_supported_version // empty' "$TMP")"
	published_released_at="$(jq -r '.released_at // empty' "$TMP")"
	[ -n "$published_version" ] && [ -n "$published_min" ] || fail "the published manifest is incomplete"
	newer "$published_version" "$VERSION" &&
		fail "$ORIGIN/latest.json already offers $published_version, newer than $VERSION"
	if [ "${VERSION%%.*}" != "${published_version%%.*}" ]; then
		min="$VERSION" # a new major version: forced
	else
		min="$published_min"
	fi
	echo "Published: $published_version (min $published_min)"
	;;
404) min="$VERSION" ;; # the first manifest
403)
	# Without s3:ListBucket, S3 answers 403 for a missing object too. If the
	# same key can read the installer just uploaded, the manifest is missing.
	probe="$(s3 -s -o /dev/null -w '%{http_code}' -I -H "x-amz-content-sha256: $EMPTY_SHA256" \
		"$ORIGIN/$VERSION/$(basename "$PKG")")"
	[ "$probe" = 200 ] || fail "cannot read $ORIGIN/latest.json (HTTP 403, and the installers: HTTP $probe)"
	min="$VERSION" # the first manifest
	;;
*) fail "cannot read $ORIGIN/latest.json (HTTP $status)" ;;
esac
min="${UPDATE_MIN_SUPPORTED_VERSION:-$min}"
if [ "${published_version:-}" = "$VERSION" ] && [ -n "${published_released_at:-}" ]; then
	released_at="$published_released_at" # the same version again
else
	released_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
fi
# min_supported_version above the new version would lock everyone out.
[ "$(printf '%s\n%s\n' "$min" "$VERSION" | sort -V | tail -1)" = "$VERSION" ] ||
	fail "min_supported_version $min is newer than $VERSION"

manifest="$(jq -n --arg version "$VERSION" --arg releasedAt "$released_at" --arg min "$min" \
	--arg pkgUrl "$DOWNLOADS/$VERSION/$(basename "$PKG")" \
	--arg pkgSha "$(shasum -a 256 "$PKG" | cut -d' ' -f1)" \
	--arg exeUrl "$DOWNLOADS/$VERSION/$(basename "$EXE")" \
	--arg exeSha "$(shasum -a 256 "$EXE" | cut -d' ' -f1)" \
	'{version: $version, released_at: $releasedAt, min_supported_version: $min,
	  platforms: {macos: {url: $pkgUrl, sha256: $pkgSha}, "windows-x64": {url: $exeUrl, sha256: $exeSha}}}')"
echo "$manifest"

sha="$(printf '%s' "$manifest" | shasum -a 256 | cut -d' ' -f1)"
s3 -sS --fail-with-body -X PUT --data-binary "$manifest" \
	-H "x-amz-content-sha256: $sha" \
	-H "Content-Type: application/json" \
	-H "Cache-Control: public, max-age=60" \
	"$ORIGIN/latest.json"

status="$(read_manifest "$TMP")"
[ "$status" = 200 ] || fail "cannot read back $ORIGIN/latest.json (HTTP $status)"
[ "$(jq -S . "$TMP")" = "$(jq -S . <<<"$manifest")" ] || fail "$ORIGIN/latest.json differs from what was uploaded"
echo "Published $DOWNLOADS/latest.json: version $VERSION, released_at $released_at, min_supported_version $min"
