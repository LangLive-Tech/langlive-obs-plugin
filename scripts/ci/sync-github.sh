#!/usr/bin/env bash
# Publishes a released version on the public GitHub mirror: one commit per
# release on GitHub's main, tagged vX.Y.Z, and the tag's GitHub Release with
# the production installers. Run by the sync:github job after deploy:prod;
# safe to run again (what is already there is skipped).
#
# GitHub gets the released source, not this repository's history: the commit
# for vX.Y.Z has exactly the tree of this repository's vX.Y.Z (checked, so
# "the source of X.Y.Z is tag vX.Y.Z" holds on both), the previous release's
# commit as its parent, and the tag's tagger and date, so running again makes
# the very same commit. Commit hashes therefore differ from this repository's.
#
# Only released versions go to GitHub: until production offers a version, its
# tag may be published again (scripts/release/retag.sh), and a tag on a public
# mirror cannot be taken back. So this refuses unless the tag's GitLab Release
# says released (scripts/ci/deploy-prod.sh marks it), refuses a version older
# than GitHub's latest, and never moves a tag or force-pushes main.
#
# The tag is SYNC_TAG if set (the manual sync:github:tag job on main, for a
# version released before this job existed), else the pipeline's tag. Name
# and version come from that tag's buildspec.json, not the checkout's.
#
# Environment: GITHUB_REPOSITORY (owner/name), GITHUB_PUSH_TOKEN (a
# fine-grained token for that repository only, with Contents read and write);
# in GitLab CI also CI_API_V4_URL, CI_PROJECT_ID, CI_JOB_TOKEN, and
# CI_COMMIT_TAG or SYNC_TAG.

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

TAG="${SYNC_TAG:-${CI_COMMIT_TAG:-}}"
[ -n "$TAG" ] || fail "no tag: run in a tag pipeline, or set SYNC_TAG"
[[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "$TAG is not a version tag (vX.Y.Z)"
: "${CI_API_V4_URL:?run in GitLab CI}"
: "${CI_PROJECT_ID:?run in GitLab CI}"
: "${CI_JOB_TOKEN:?run in GitLab CI}"
: "${GITHUB_REPOSITORY:?GITHUB_REPOSITORY (owner/name) is not set}"
: "${GITHUB_PUSH_TOKEN:?GITHUB_PUSH_TOKEN is not set}"
[[ "$GITHUB_REPOSITORY" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || fail "GITHUB_REPOSITORY must be owner/name"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
g() { git -C "$ROOT" "$@"; }
g rev-parse -q --verify "refs/tags/$TAG" >/dev/null || g fetch --quiet origin "+refs/tags/$TAG:refs/tags/$TAG" ||
	fail "no tag $TAG in this repository"
NAME="$(g show "refs/tags/$TAG:buildspec.json" | jq -r .name)"
VERSION="$(g show "refs/tags/$TAG:buildspec.json" | jq -r .version)"
[ "$TAG" = "v$VERSION" ] || fail "tag $TAG does not match its buildspec.json's version $VERSION"
API="$CI_API_V4_URL/projects/$CI_PROJECT_ID"
AUTH="JOB-TOKEN: $CI_JOB_TOKEN"
GH_API="https://api.github.com/repos/$GITHUB_REPOSITORY"
GH_UPLOADS="https://uploads.github.com/repos/$GITHUB_REPOSITORY"
REMOTE="https://github.com/$GITHUB_REPOSITORY.git"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT

# --- The GitLab Release: released? Then fetch its production installers. ---

release="$(curl -sS --fail-with-body -H "$AUTH" "$API/releases/$TAG")" ||
	fail "no GitLab Release for $TAG; the release job makes it"
description="$(jq -r .description <<<"$release")"
grep -q '^Status: released ' <<<"$description" ||
	fail "$TAG is not released (its GitLab Release does not say so); deploy:prod releases it"

INSTALLERS=("$NAME-macos-universal-v$VERSION.pkg" "$NAME-windows-x64-v$VERSION.exe")
SHAS=() # in INSTALLERS order; macOS bash 3.2 has no associative arrays
for base in "${INSTALLERS[@]}"; do
	curl -sS --fail-with-body -H "$AUTH" -o "$DIR/$base" "$API/packages/generic/$NAME/$VERSION/$base"
	expected="$(grep -F "| \`$base\` |" <<<"$description" | sed -E 's/.*\| `([0-9a-f]{64})` \|$/\1/')"
	actual="$(shasum -a 256 "$DIR/$base" | cut -d' ' -f1)"
	[ "$actual" = "$expected" ] || fail "$base: SHA-256 $actual, the GitLab Release lists ${expected:-none}"
	SHAS+=("$actual")
done

# --- Git: the release commit on GitHub's main, and its tag. The token goes in
# through the environment, never on a command line or in a remote URL, so it
# cannot end up in the log.

export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0="http.https://github.com/.extraheader"
export GIT_CONFIG_VALUE_0="Authorization: Basic $(printf 'x-access-token:%s' "$GITHUB_PUSH_TOKEN" | base64 | tr -d '\n')"
tree="$(g rev-parse "refs/tags/$TAG^{tree}")"

remote_tag="$(g ls-remote "$REMOTE" "refs/tags/$TAG" | cut -f1)"
remote_main="$(g ls-remote "$REMOTE" refs/heads/main | cut -f1)"

if [ -n "$remote_tag" ]; then
	# Published before: it must be this exact source. A tag is never moved.
	g fetch --quiet "$REMOTE" "refs/tags/$TAG"
	[ "$(g rev-parse 'FETCH_HEAD^{tree}')" = "$tree" ] ||
		fail "GitHub's $TAG has other source than this repository's $TAG; released tags are never moved"
	echo "GitHub: $TAG is there already, with the same source"
else
	parent=()
	if [ -n "$remote_main" ]; then
		g fetch --quiet "$REMOTE" refs/heads/main
		latest="$(g show FETCH_HEAD:buildspec.json | jq -r .version)" ||
			fail "GitHub's main has no readable buildspec.json; not adding to it"
		[ "$(printf '%s\n%s\n' "$latest" "$VERSION" | sort -V | tail -n1)" = "$VERSION" ] && [ "$latest" != "$VERSION" ] ||
			fail "GitHub's main is at $latest already; versions go to GitHub in order (publish older ones first)"
		parent=(-p "$remote_main")
	fi
	# Tagger and date of the annotated tag (or the commit's committer for a
	# lightweight one), so the commit is the same however often this runs.
	ident="$(g for-each-ref --format='%(if)%(taggername)%(then)%(taggername)|%(taggeremail:trim)|%(taggerdate:raw)%(else)%(committername)|%(committeremail:trim)|%(committerdate:raw)%(end)' "refs/tags/$TAG")"
	IFS='|' read -r who email when <<<"$ident"
	release_commit="$(
		GIT_AUTHOR_NAME="$who" GIT_AUTHOR_EMAIL="$email" GIT_AUTHOR_DATE="$when" \
			GIT_COMMITTER_NAME="$who" GIT_COMMITTER_EMAIL="$email" GIT_COMMITTER_DATE="$when" \
			g commit-tree "$tree" "${parent[@]+"${parent[@]}"}" -m "LangLive OBS Plugin $VERSION" \
			-m "The source of the official $VERSION installers."
	)"
	# One atomic push: main and the tag move together or not at all. Without
	# --force, a main that changed meanwhile refuses it.
	g push --quiet --atomic "$REMOTE" "$release_commit:refs/heads/main" "$release_commit:refs/tags/$TAG"
	echo "GitHub: main is now $TAG ($release_commit)"
fi

# --- The GitHub Release with the installers. ---

gh() {
	printf 'header = "Authorization: Bearer %s"\n' "$GITHUB_PUSH_TOKEN" |
		curl -K - -sS -H 'Accept: application/vnd.github+json' -H 'X-GitHub-Api-Version: 2022-11-28' "$@"
}

status="$(gh -o "$DIR/release.json" -w '%{http_code}' "$GH_API/releases/tags/$TAG")"
if [ "$status" = 404 ]; then
	body="$(
		printf 'LangLive OBS Plugin %s.\n\n' "$VERSION"
		printf 'The installers are built from the source at tag `%s`.\n\n' "$TAG"
		printf '| Installer | SHA-256 |\n|---|---|\n'
		for i in "${!INSTALLERS[@]}"; do printf '| `%s` | `%s` |\n' "${INSTALLERS[$i]}" "${SHAS[$i]}"; done
	)"
	payload="$(jq -n --arg tag "$TAG" --arg name "LangLive OBS Plugin $VERSION" --arg body "$body" \
		'{tag_name: $tag, name: $name, body: $body, draft: false, prerelease: false, make_latest: "legacy"}')"
	status="$(gh -o "$DIR/release.json" -w '%{http_code}' -X POST -d "$payload" "$GH_API/releases")"
	[ "$status" = 201 ] || fail "GitHub: creating the release returned HTTP $status: $(cat "$DIR/release.json")"
	echo "GitHub: created release $TAG"
elif [ "$status" = 200 ]; then
	echo "GitHub: release $TAG is there already"
else
	fail "GitHub: reading release $TAG returned HTTP $status: $(cat "$DIR/release.json")"
fi
release_id="$(jq -r .id "$DIR/release.json")"

for base in "${INSTALLERS[@]}"; do
	size="$(jq -r --arg n "$base" '.assets[] | select(.name == $n) | .size' "$DIR/release.json")"
	if [ -n "$size" ]; then
		[ "$size" = "$(wc -c <"$DIR/$base" | tr -d ' ')" ] ||
			fail "GitHub: release $TAG has a $base of another size; delete that asset and run again"
		echo "GitHub: $base is attached already"
		continue
	fi
	status="$(gh -o "$DIR/asset.json" -w '%{http_code}' -X POST -H 'Content-Type: application/octet-stream' \
		--data-binary "@$DIR/$base" "$GH_UPLOADS/releases/$release_id/assets?name=$base")"
	[ "$status" = 201 ] || fail "GitHub: uploading $base returned HTTP $status: $(cat "$DIR/asset.json")"
	echo "GitHub: attached $base"
done

echo "$TAG is on GitHub: https://github.com/$GITHUB_REPOSITORY/releases/tag/$TAG"
