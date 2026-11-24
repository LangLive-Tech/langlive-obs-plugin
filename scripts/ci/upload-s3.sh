#!/usr/bin/env bash
# Uploads the installers in $RELEASE_DIR (default release/) to S3 under a
# prefix:
#
#   scripts/ci/upload-s3.sh <prefix> [--replace | --snapshot]
#
#   (none)         never replaces a file with other content
#   --replace      replaces files: a version not released yet, built again
#                  (prod/<version> before prod/latest.json offers it)
#   --snapshot     replaces files and marks them not to be cached (dev, test)
#   --snapshot-name  as --snapshot, and names the files ...-vX.Y.Z-SNAPSHOT
#                  (dev): version X.Y.Z plus the changes since, one pair of
#                  files per version
#
# Environment: S3_BUCKET, S3_REGION, and the prefix's credentials in
# AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY. Uses curl's SigV4 signing, so the
# runner needs no AWS CLI.
#
# Released files are named by version and never change, so they get a long
# Cache-Control. Replacing them is only safe before anyone can have them:
# the callers decide that (see scripts/ci/deploy-prod.sh).

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

PREFIX="${1:?usage: upload-s3.sh <prefix> [--replace | --snapshot | --snapshot-name]}"
PREFIX="${PREFIX%/}"
shift
REPLACE=false
SNAPSHOT_NAME=false
CACHE_CONTROL="public, max-age=31536000, immutable"
for option in "$@"; do
	case "$option" in
	--replace) REPLACE=true ;;
	--snapshot)
		REPLACE=true
		CACHE_CONTROL="no-cache"
		;;
	--snapshot-name)
		REPLACE=true
		SNAPSHOT_NAME=true
		CACHE_CONTROL="no-cache"
		;;
	*) fail "unknown option $option" ;;
	esac
done
: "${S3_BUCKET:?S3_BUCKET is not set}"
: "${S3_REGION:?S3_REGION is not set}"
[ -n "${AWS_ACCESS_KEY_ID:-}" ] || fail "AWS_ACCESS_KEY_ID is not set (are this pipeline's credentials protected?)"
[ -n "${AWS_SECRET_ACCESS_KEY:-}" ] || fail "AWS_SECRET_ACCESS_KEY is not set"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENDPOINT="https://$S3_BUCKET.s3.$S3_REGION.amazonaws.com"
EMPTY_SHA256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855

# The credentials go through a config on stdin, not the command line, which
# other processes on the runner could read.
s3() {
	printf 'user = "%s:%s"\n' "$AWS_ACCESS_KEY_ID" "$AWS_SECRET_ACCESS_KEY" |
		curl -K - --aws-sigv4 "aws:amz:$S3_REGION:s3" "$@"
}

shopt -s nullglob
DIR="$ROOT/${RELEASE_DIR:-release}"
FILES=("$DIR"/*.pkg "$DIR"/*.exe)
[ ${#FILES[@]} -gt 0 ] || fail "no installers in $DIR"

for file in "${FILES[@]}"; do
	name="$(basename "$file")"
	$SNAPSHOT_NAME && name="$(sed -E 's/(-v[0-9]+\.[0-9]+\.[0-9]+)(\.[a-z]+)$/\1-SNAPSHOT\2/' <<<"$name")"
	key="$PREFIX/$name"
	sha="$(shasum -a 256 "$file" | cut -d' ' -f1)"

	if $REPLACE; then
		echo "Uploading $key ($sha), replacing"
		s3 -sS --fail-with-body --upload-file "$file" -H "x-amz-content-sha256: $sha" \
			-H "Cache-Control: $CACHE_CONTROL" "$ENDPOINT/$key"
		continue
	fi

	headers="$(s3 -s -I -H "x-amz-content-sha256: $EMPTY_SHA256" "$ENDPOINT/$key" | tr -d '\r')"
	status="$(printf '%s\n' "$headers" | awk 'NR == 1 { print $2 }')"
	case "$status" in
	200)
		# A retried job finds its own upload: S3's ETag of a single-part
		# upload is the file's MD5.
		etag="$(printf '%s\n' "$headers" | awk -F'"' 'tolower($0) ~ /^etag:/ { print $2 }')"
		if [ "$etag" = "$(md5 -q "$file")" ]; then
			echo "$key: already uploaded, same file"
			continue
		fi
		fail "$key already exists with other content; a published version is never replaced"
		;;
	404 | 403) ;; # 403: the credentials may write but not read
	*) fail "checking $key: HTTP ${status:-no response}" ;;
	esac

	echo "Uploading $key ($sha)"
	s3 -sS --fail-with-body --upload-file "$file" \
		-H "x-amz-content-sha256: $sha" \
		-H "Cache-Control: $CACHE_CONTROL" \
		"$ENDPOINT/$key"
done
echo "Uploaded ${#FILES[@]} installers to $PREFIX/"
