#!/usr/bin/env bash
# Publishes a version tag again: after a failed release, or to rebuild a
# version QA is still testing. For maintainers, with glab logged in to the
# GitLab instance of the `origin` remote.
#
#   scripts/release/retag.sh vX.Y.Z [<commit>]     default commit: HEAD
#
# Refuses a version production offers or offered before: the same as, or
# older than, the version in production's manifest (the
# PROD_UPDATE_MANIFEST_URL CI variable), or one whose GitLab Release
# deploy:prod marked released. Released versions are final; a fix is a new
# version. Stops when production's version cannot be read (no answer, an
# error status, not a manifest), rather than taking that for "nothing
# released": the script deletes things. Otherwise
# deletes the tag's GitLab Release and generic package, deletes the tag (v*
# tags are protected, so through the API), then tags <commit> and pushes it.
# The new tag pipeline rebuilds both, recreates the Release and replaces the
# test/ files, and the prod/ ones after deploy:prod.

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

TAG="${1:?usage: retag.sh vX.Y.Z [<commit>]}"
COMMIT="${2:-HEAD}"
[[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "$TAG is not a vX.Y.Z tag"
VERSION="${TAG#v}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

[ "$(git show "$COMMIT:buildspec.json" | jq -r .version)" = "$VERSION" ] ||
	fail "buildspec.json at $COMMIT is not version $VERSION"

# git@host:group/project.git or ssh://git@host[:port]/group/project.git
REMOTE="$(git remote get-url origin)"
HOST="$(sed -E 's#^(ssh://)?[^@]+@([^:/]+).*#\2#' <<<"$REMOTE")"
PROJECT="$(sed -E 's#^(ssh://[^/]+/|[^:]+:)##; s#\.git$##' <<<"$REMOTE")"
PROJECT_ID="${PROJECT//\//%2F}"
api() { glab api --hostname "$HOST" "$@"; }

# RETAG_MANIFEST_URL: for testing this check only.
MANIFEST_URL="${RETAG_MANIFEST_URL:-$(api "projects/$PROJECT_ID/variables/PROD_UPDATE_MANIFEST_URL" | jq -r '.value // empty')}"
[[ "$MANIFEST_URL" == https://* ]] ||
	fail "cannot read the PROD_UPDATE_MANIFEST_URL CI variable (got '$MANIFEST_URL'); not retagging"
manifest="$(curl -sS --fail --max-time 30 "$MANIFEST_URL")" ||
	fail "cannot download production's manifest ($MANIFEST_URL); not retagging without knowing what is released"
released="$(jq -r '.version // empty' <<<"$manifest" 2>/dev/null || true)"
[[ "$released" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
	fail "production's manifest ($MANIFEST_URL) has no X.Y.Z version (got '$released'); not retagging"
[ "$released" != "$VERSION" ] || fail "$VERSION is released already ($MANIFEST_URL); release a new version"
older="$(printf '%s\n%s\n' "$VERSION" "$released" | sort -t. -k1,1n -k2,2n -k3,3n | head -1)"
[ "$older" != "$VERSION" ] ||
	fail "$VERSION is older than the released $released, so it was released or skipped; release a new version"

# deploy:prod marks the Release; a second check in case the manifest moved on.
if release="$(api "projects/$PROJECT_ID/releases/$TAG" 2>/dev/null)"; then
	! grep -q '^Status: released' <<<"$(jq -r '.description // empty' <<<"$release")" ||
		fail "the GitLab Release $TAG says it is released; release a new version"
fi

echo "Publishing $TAG again at $(git rev-parse --short "$COMMIT") (production offers $released)"
read -r -p "Continue? [y/N] " answer
[ "$answer" = y ] || exit 1

if api "projects/$PROJECT_ID/releases/$TAG" >/dev/null 2>&1; then
	api -X DELETE "projects/$PROJECT_ID/releases/$TAG" >/dev/null
	echo "Deleted the GitLab Release $TAG"
fi
NAME="$(jq -r .name buildspec.json)"
for id in $(api "projects/$PROJECT_ID/packages?package_type=generic&package_name=$NAME&package_version=$VERSION" |
	jq -r ".[] | select(.version == \"$VERSION\") | .id"); do
	api -X DELETE "projects/$PROJECT_ID/packages/$id" >/dev/null
	echo "Deleted package $NAME $VERSION"
done
if api "projects/$PROJECT_ID/repository/tags/$TAG" >/dev/null 2>&1; then
	api -X DELETE "projects/$PROJECT_ID/repository/tags/$TAG" >/dev/null
	echo "Deleted the tag $TAG"
fi
git tag -d "$TAG" >/dev/null 2>&1 || true

git tag -a "$TAG" -m "LangLive OBS Plugin $VERSION" "$COMMIT"
git push origin "$TAG"
echo "Pushed $TAG; its pipeline publishes it to test again"
