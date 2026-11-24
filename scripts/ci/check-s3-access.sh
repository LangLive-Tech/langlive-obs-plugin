#!/usr/bin/env bash
# Checks the S3 credentials without publishing anything: each key pair must
# be able to write and read a small marker under its own prefix and must be
# refused under the other's.
#
#   scripts/ci/check-s3-access.sh [test|prod]     default: both
#
# Environment: S3_BUCKET, S3_REGION, TEST_ACCESS_ID / TEST_ACCESS_SECRET_KEY
# (dev/, test/) and PROD_ACCESS_ID / PROD_ACCESS_SECRET_KEY (prod/). In CI each
# key pair is scoped to its environments, so a job checks one of them.
#
# The marker, <prefix>/.ci-access-check, is overwritten on every run and
# deleted afterwards when the key may delete.

set -euo pipefail

: "${S3_BUCKET:?S3_BUCKET is not set}"
: "${S3_REGION:?S3_REGION is not set}"
ENDPOINT="https://$S3_BUCKET.s3.$S3_REGION.amazonaws.com"
MARKER=".ci-access-check"
BODY="access check from pipeline ${CI_PIPELINE_ID:-local}"
BODY_SHA256="$(printf '%s' "$BODY" | shasum -a 256 | cut -d' ' -f1)"
EMPTY_SHA256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
failures=0

# s3 <id> <secret> <curl args...>: prints the HTTP status. Credentials go
# through stdin, never the command line.
s3() {
	local id="$1" secret="$2"
	shift 2
	printf 'user = "%s:%s"\n' "$id" "$secret" |
		curl -K - -s -o /dev/null -w '%{http_code}' --aws-sigv4 "aws:amz:$S3_REGION:s3" "$@"
}

put() { # <id> <secret> <prefix>
	s3 "$1" "$2" -X PUT --data-binary "$BODY" -H "x-amz-content-sha256: $BODY_SHA256" "$ENDPOINT/$3/$MARKER"
}

expect() { # <description> <expected status pattern> <actual status>
	if [[ "$3" =~ ^($2)$ ]]; then
		echo "ok    $1 (HTTP $3)"
	else
		echo "FAIL  $1: HTTP $3, expected $2"
		failures=$((failures + 1))
	fi
}

check() { # <name> <id> <secret> <own prefix> <other prefix>
	local name="$1" id="$2" secret="$3" own="$4" other="$5"
	if [ -z "$id" ] || [ -z "$secret" ]; then
		echo "FAIL  $name: credentials not set in this pipeline"
		failures=$((failures + 1))
		return
	fi
	expect "$name writes $own/" 200 "$(put "$id" "$secret" "$own")"
	# The release reads back prod/latest.json, which only the CDN serves
	# publicly.
	expect "$name reads $own/" 200 \
		"$(s3 "$id" "$secret" -H "x-amz-content-sha256: $EMPTY_SHA256" "$ENDPOINT/$own/$MARKER")"
	expect "$name is refused in $other/" 403 "$(put "$id" "$secret" "$other")"
	local status
	status="$(s3 "$id" "$secret" -X DELETE -H "x-amz-content-sha256: $EMPTY_SHA256" "$ENDPOINT/$own/$MARKER")"
	case "$status" in
	204) echo "ok    $name deleted its marker" ;;
	403) echo "note  $name may not delete; $own/$MARKER stays" ;;
	*) echo "note  deleting $own/$MARKER: HTTP $status" ;;
	esac
}

KEYS="${1:-both}"
case "$KEYS" in test | prod | both) ;; *)
	echo "usage: check-s3-access.sh [test|prod]" >&2
	exit 2
	;;
esac
if [ "$KEYS" != prod ]; then
	check TEST "${TEST_ACCESS_ID:-}" "${TEST_ACCESS_SECRET_KEY:-}" dev prod
	check TEST "${TEST_ACCESS_ID:-}" "${TEST_ACCESS_SECRET_KEY:-}" test prod
fi
if [ "$KEYS" != test ]; then
	check PROD "${PROD_ACCESS_ID:-}" "${PROD_ACCESS_SECRET_KEY:-}" prod dev
fi

[ "$failures" -eq 0 ] || {
	echo "$failures check(s) failed" >&2
	exit 1
}
echo "S3 credentials ok"
