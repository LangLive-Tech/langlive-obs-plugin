#!/usr/bin/env bash
# Prints the version that <prefix>/latest.json offers, or nothing when there
# is no manifest yet.
#
#   scripts/ci/published-version.sh <prefix>        e.g. prod
#
# Environment: S3_BUCKET, S3_REGION, and the prefix's credentials in
# AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY (prod/ is readable only through
# the CDN, whose copy may be a minute old).

set -euo pipefail

PREFIX="${1:?usage: published-version.sh <prefix>}"
: "${S3_BUCKET:?S3_BUCKET is not set}"
: "${S3_REGION:?S3_REGION is not set}"
ENDPOINT="https://$S3_BUCKET.s3.$S3_REGION.amazonaws.com"
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

status="$(printf 'user = "%s:%s"\n' "${AWS_ACCESS_KEY_ID:?}" "${AWS_SECRET_ACCESS_KEY:?}" |
	curl -K - --aws-sigv4 "aws:amz:$S3_REGION:s3" -s -o "$TMP" -w '%{http_code}' \
		-H "x-amz-content-sha256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" \
		"$ENDPOINT/$PREFIX/latest.json")"
case "$status" in
200) jq -r '.version // empty' "$TMP" ;;
403 | 404) ;; # without s3:ListBucket, a missing object is 403 too
*)
	echo "error: cannot read $PREFIX/latest.json (HTTP $status)" >&2
	exit 1
	;;
esac
