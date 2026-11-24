#!/usr/bin/env bash
# Removes the LangLive OBS Plugin from this Mac.
#
# The macOS .pkg installer has no built-in uninstaller, so this does it
# manually. It removes:
#   - the plugin bundle installed for OBS Studio
#   - its saved settings
#   - the saved login token in the login keychain
#   - the .pkg installer receipt
#
# Usage:
#   ./scripts/uninstall-macos.sh            # remove the plugin
#   ./scripts/uninstall-macos.sh --dry-run  # show what would be removed

set -euo pipefail

DRY_RUN=false

for arg in "$@"; do
	case "$arg" in
		--dry-run) DRY_RUN=true ;;
		*)
			echo "Unknown option: $arg" >&2
			echo "Usage: $0 [--dry-run]" >&2
			exit 1
			;;
	esac
done

PLUGIN_NAME="langlive-obs-plugin"
PKG_ID="com.langlive.obs-plugin"

TARGETS=(
	"$HOME/Library/Application Support/obs-studio/plugins/${PLUGIN_NAME}.plugin"
	"$HOME/Library/Application Support/obs-studio/plugin_config/${PLUGIN_NAME}"
	"$HOME/Library/Receipts/${PKG_ID}.plist"
	"$HOME/Library/Receipts/${PKG_ID}.bom"
)

if pgrep -qx "OBS" 2>/dev/null; then
	echo "warning: OBS Studio is running. Quit it first, otherwise it may recreate" >&2
	echo "         the config files or keep the plugin file in use." >&2
fi

found_any=false
for path in "${TARGETS[@]}"; do
	if [ -e "$path" ]; then
		found_any=true
		if $DRY_RUN; then
			echo "would remove: $path"
		else
			echo "removing: $path"
			rm -rf "$path"
		fi
	fi
done

# The login token (src/obs/secure-store-macos.cpp): production builds and QA
# builds each keep their own item.
KEYCHAIN_ACCOUNT="session-token"
for KEYCHAIN_SERVICE in "com.langlive.obs-plugin" "com.langlive.obs-plugin.qa"; do
	if security find-generic-password -s "$KEYCHAIN_SERVICE" -a "$KEYCHAIN_ACCOUNT" >/dev/null 2>&1; then
		found_any=true
		if $DRY_RUN; then
			echo "would remove: keychain item $KEYCHAIN_SERVICE ($KEYCHAIN_ACCOUNT)"
		else
			echo "removing: keychain item $KEYCHAIN_SERVICE ($KEYCHAIN_ACCOUNT)"
			security delete-generic-password -s "$KEYCHAIN_SERVICE" -a "$KEYCHAIN_ACCOUNT" >/dev/null
		fi
	fi
done

if ! $found_any; then
	echo "Nothing to remove: ${PLUGIN_NAME} does not appear to be installed."
	exit 0
fi

if $DRY_RUN; then
	echo ""
	echo "(dry run: nothing was deleted)"
else
	echo ""
	echo "Done. ${PLUGIN_NAME} has been removed."
fi
