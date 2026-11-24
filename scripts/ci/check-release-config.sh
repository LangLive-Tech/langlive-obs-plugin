#!/usr/bin/env bash
# Refuses to publish a version tag's build that is not fit to ship: the tag
# must name buildspec.json's version (the installers are named and
# versioned from it), and the build must have had an API URL (without one the
# plugin uses its mock backend, where any login succeeds) and an update
# manifest URL (without one it could never update).
#
# Environment: CI_COMMIT_TAG, and LANGLIVE_API_BASE_URL /
# LANGLIVE_UPDATE_MANIFEST_URL as the build job had them.

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

: "${CI_COMMIT_TAG:?run in a tag pipeline}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(jq -r .version "$ROOT/buildspec.json")"

[ "$CI_COMMIT_TAG" = "v$VERSION" ] || fail "tag $CI_COMMIT_TAG does not match buildspec.json version $VERSION"
[ -n "${LANGLIVE_API_BASE_URL:-}" ] || fail "the API URL is not set for this environment"
[ -n "${LANGLIVE_UPDATE_MANIFEST_URL:-}" ] || fail "the update manifest URL is not set for this environment"
echo "Release config ok: $CI_COMMIT_TAG"
