#!/usr/bin/env bash
# Checks that this Mac can sign and notarize with its Developer ID
# certificates, without touching a release: unlocks the login keychain, signs
# a copy of /usr/bin/true (hardened runtime, secure timestamp) with the
# Developer ID Application identity and a throwaway package with the
# Developer ID Installer identity, has Apple notarize the package, staples
# the ticket, and checks that Gatekeeper accepts it.
#
#   scripts/ci/check-macos-signing.sh
#
# The same steps as a release build, on throwaway files: the keychain setup
# is in scripts/ci/macos-signing.sh, notarization in
# scripts/ci/notarize-macos.sh (their comments list the one-time setup).

set -euo pipefail

fail() {
	echo "error: $*" >&2
	exit 1
}

# Unlocks the keychain and finds the two identities.
# shellcheck source=scripts/ci/macos-signing.sh
source "$(dirname "$0")/macos-signing.sh"
KEYCHAIN="$MACOS_KEYCHAIN"
APP_HASH="$MACOS_CODESIGN_IDENTITY"
PKG_HASH="$MACOS_INSTALLER_IDENTITY"
APP_NAME="Developer ID Application"
PKG_NAME="Developer ID Installer"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Code: what the plugin bundle gets. --timestamp needs Apple's timestamp
# server, as notarization does.
cp /usr/bin/true "$TMP/true"
codesign --force --options runtime --timestamp --sign "$APP_HASH" "$TMP/true" ||
	fail "codesign failed (errSecInternalComponent: locked keychain or partition list)"
codesign --verify --strict "$TMP/true" || fail "the signed binary does not verify"
codesign -dv "$TMP/true" 2>&1 | grep -E '^(Authority=Developer ID|TeamIdentifier=|Timestamp=|Runtime Version=)' || true
echo "ok    codesign with $APP_NAME"

# Installer: what the .pkg gets.
mkdir -p "$TMP/root"
cp "$TMP/true" "$TMP/root/"
pkgbuild --quiet --root "$TMP/root" --identifier com.langlive.obs-plugin.signing-check --version 1 \
	--install-location /tmp/langlive-signing-check "$TMP/unsigned.pkg"
productsign --timestamp --sign "$PKG_HASH" "$TMP/unsigned.pkg" "$TMP/signed.pkg" || fail "productsign failed"
check="$(pkgutil --check-signature "$TMP/signed.pkg" || true)"
printf '%s\n' "$check" | head -4
printf '%s\n' "$check" | grep -q 'Status: signed by a developer certificate issued by Apple' ||
	fail "the signed package does not verify"
echo "ok    productsign with $PKG_NAME"

"$(dirname "$0")/notarize-macos.sh" "$TMP/signed.pkg"

echo "Signing and notarization work on this Mac"
