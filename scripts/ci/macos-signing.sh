# Sourced, not run: prepares this shell for Developer ID signing on the
# obs-mac runner.
#
#   source scripts/ci/macos-signing.sh
#
# Unlocks the login keychain (each SSH or CI session has its own lock
# state), then exports
#   MACOS_KEYCHAIN            the keychain holding the identities
#   MACOS_CODESIGN_IDENTITY   SHA-1 of the one valid "Developer ID Application" identity
#   MACOS_INSTALLER_IDENTITY  SHA-1 of the one valid "Developer ID Installer" identity
# which the package step (cmake/macos/resources/create-package.cmake.in)
# signs with when they are set.
#
# Environment: MACOS_KEYCHAIN_PASSWORD, the keychain's password (a masked,
# protected CI variable: main and version tags only). One-time setup on the
# runner, so signing never waits for a dialog nobody can answer:
#
#   security set-key-partition-list -S apple-tool:,apple:,codesign:,productsign: \
#       -s -k <password> ~/Library/Keychains/login.keychain-db
#
# and "Always Allow" once if macOS still asks (the key's access list).
# Re-importing a certificate or key needs both again.

macos_signing_fail() {
	echo "error: $*" >&2
	return 1
}

# The one valid identity in $MACOS_KEYCHAIN whose name starts with $1;
# prints "<SHA-1> <name>".
macos_signing_identity() {
	local matches count
	matches="$(security find-identity -v "$MACOS_KEYCHAIN" | grep -F "\"$1" || true)"
	[ -n "$matches" ] || macos_signing_fail "no valid \"$1...\" identity in $MACOS_KEYCHAIN" || return 1
	count="$(printf '%s\n' "$matches" | wc -l | tr -d ' ')"
	[ "$count" = 1 ] || macos_signing_fail "$count \"$1...\" identities; expected one:
$matches" || return 1
	printf '%s\n' "$matches" | sed -E 's/^ *[0-9]+\) ([0-9A-F]{40}) "(.*)"$/\1 \2/'
}

macos_signing_setup() {
	[ -n "${MACOS_KEYCHAIN_PASSWORD:-}" ] ||
		macos_signing_fail "MACOS_KEYCHAIN_PASSWORD is not set (protected: only on main and version tags)" || return 1
	MACOS_KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"
	security unlock-keychain -p "$MACOS_KEYCHAIN_PASSWORD" "$MACOS_KEYCHAIN" ||
		macos_signing_fail "cannot unlock $MACOS_KEYCHAIN" || return 1

	local app installer
	app="$(macos_signing_identity "Developer ID Application: ")" || return 1
	installer="$(macos_signing_identity "Developer ID Installer: ")" || return 1
	MACOS_CODESIGN_IDENTITY="${app%% *}"
	MACOS_INSTALLER_IDENTITY="${installer%% *}"
	export MACOS_KEYCHAIN MACOS_CODESIGN_IDENTITY MACOS_INSTALLER_IDENTITY
	echo "Signing with ${app#* } and ${installer#* }"
}

macos_signing_setup
