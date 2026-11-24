#!/usr/bin/env bash
# Has Apple notarize a signed installer, staples the ticket to it, and checks
# that Gatekeeper accepts it. Run after the package step signed it
# (source scripts/ci/macos-signing.sh first).
#
#   scripts/ci/notarize-macos.sh <installer.pkg>
#
# Uses the notarytool credentials saved in the login keychain under
# $NOTARY_PROFILE (default langlive-notary), once on the runner:
#
#   xcrun notarytool store-credentials langlive-notary --apple-id <Apple ID> \
#       --team-id <team> --keychain ~/Library/Keychains/login.keychain-db
#
# Without --keychain, newer macOS saves the profile where --keychain below
# does not look. The keychain must be unlocked (scripts/ci/macos-signing.sh).

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

PKG="${1:?usage: notarize-macos.sh <installer.pkg>}"
[ -f "$PKG" ] || fail "no such file: $PKG"
KEYCHAIN="${MACOS_KEYCHAIN:-$HOME/Library/Keychains/login.keychain-db}"
NOTARY_PROFILE="${NOTARY_PROFILE:-langlive-notary}"

pkgutil --check-signature "$PKG" | grep -q 'Status: signed by a developer certificate issued by Apple' ||
	fail "$PKG is not signed with a Developer ID Installer certificate"

# Usually a few minutes.
echo "Notarizing $(basename "$PKG")..."
result="$(xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --keychain "$KEYCHAIN" \
	--wait --timeout 30m --output-format json)" || true
status="$(jq -r '.status // empty' <<<"$result" 2>/dev/null || true)"
id="$(jq -r '.id // empty' <<<"$result" 2>/dev/null || true)"
if [ "$status" != Accepted ]; then
	printf '%s\n' "$result"
	# Apple's reasons, e.g. a binary without the hardened runtime.
	[ -n "$id" ] && xcrun notarytool log "$id" --keychain-profile "$NOTARY_PROFILE" --keychain "$KEYCHAIN" || true
	fail "notarization: ${status:-no answer}"
fi
echo "Notarized (submission $id)"

# The ticket in the package, so Gatekeeper accepts it offline too.
xcrun stapler staple -q "$PKG" || fail "cannot staple the ticket to $PKG"
xcrun stapler validate -q "$PKG" || fail "the stapled ticket does not validate"
spctl --assess --type install -vv "$PKG" 2>&1 || fail "Gatekeeper rejects $PKG"
echo "$(basename "$PKG"): signed, notarized, stapled"
