#!/usr/bin/env bash
# Checks, without changing anything on GitHub, that sync:github will be able
# to publish: GITHUB_REPOSITORY exists and GITHUB_PUSH_TOKEN can read it and
# push to it (a dry-run push still needs write access). Run by the manual
# check:github-access job, so a wrong token shows up before a release.
#
# Environment: GITHUB_REPOSITORY (owner/name), GITHUB_PUSH_TOKEN.

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

: "${GITHUB_REPOSITORY:?GITHUB_REPOSITORY (owner/name) is not set}"
: "${GITHUB_PUSH_TOKEN:?GITHUB_PUSH_TOKEN is not set}"
[[ "$GITHUB_REPOSITORY" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || fail "GITHUB_REPOSITORY must be owner/name"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REMOTE="https://github.com/$GITHUB_REPOSITORY.git"
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

status="$(printf 'header = "Authorization: Bearer %s"\n' "$GITHUB_PUSH_TOKEN" |
	curl -K - -sS -o "$TMP" -w '%{http_code}' -H 'Accept: application/vnd.github+json' \
		-H 'X-GitHub-Api-Version: 2022-11-28' "https://api.github.com/repos/$GITHUB_REPOSITORY")"
case "$status" in
200) echo "GitHub: $GITHUB_REPOSITORY is readable ($(jq -r 'if .private then "private" else "public" end' "$TMP"))" ;;
401) fail "GitHub: the token is invalid or expired" ;;
404) fail "GitHub: $GITHUB_REPOSITORY not found, or the token has no access to it (or is not approved by the organization yet)" ;;
*) fail "GitHub: HTTP $status: $(cat "$TMP")" ;;
esac

export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0="http.https://github.com/.extraheader"
export GIT_CONFIG_VALUE_0="Authorization: Basic $(printf 'x-access-token:%s' "$GITHUB_PUSH_TOKEN" | base64 | tr -d '\n')"
git -C "$ROOT" push --dry-run --quiet "$REMOTE" "HEAD:refs/heads/check-github-access" ||
	fail "GitHub: the token cannot push to $GITHUB_REPOSITORY (it needs Contents: read and write)"
echo "GitHub: the token can push to $GITHUB_REPOSITORY (dry run, nothing was pushed)"
