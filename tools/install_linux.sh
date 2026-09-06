#!/usr/bin/env bash
#
# Installs VitaSync for the current user: no root needed, no system
# packages touched. Copies the portable bundle that ships alongside this
# script into ~/.local/share, adds a desktop menu entry and a launcher on
# PATH, and does not touch anything outside the current user's home.
#
# This is the tracked source copy. At release time it is copied next to the
# portable bundle, where "linux-portable/" sits beside it; run straight from
# a bundle directory it finds the launcher next to itself instead.
#
# Usage: ./install_linux.sh [--uninstall]

set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
if [[ -x "$HERE/linux-portable/vitasync" ]]; then
    BUNDLE_SRC="$HERE/linux-portable"
else
    BUNDLE_SRC="$HERE"
fi

INSTALL_DIR="$HOME/.local/share/vitasync"
DESKTOP_FILE="$HOME/.local/share/applications/vitasync.desktop"
ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
ICON_FILE="$ICON_DIR/vitasync.png"
BIN_LINK="$HOME/.local/bin/vitasync"

if [[ "${1:-}" == "--uninstall" ]]; then
    echo "Removing VitaSync..."
    rm -rf "$INSTALL_DIR"
    rm -f "$DESKTOP_FILE" "$ICON_FILE" "$BIN_LINK"
    command -v update-desktop-database >/dev/null 2>&1 \
        && update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
    echo "Done."
    exit 0
fi

if [[ ! -x "$BUNDLE_SRC/vitasync" ]]; then
    echo "error: no vitasync launcher found next to this script." >&2
    echo "Run this from inside the extracted release folder." >&2
    exit 1
fi

echo "Installing VitaSync to $INSTALL_DIR ..."
mkdir -p "$(dirname "$INSTALL_DIR")"
rm -rf "$INSTALL_DIR"
cp -r "$BUNDLE_SRC" "$INSTALL_DIR"

mkdir -p "$ICON_DIR" "$(dirname "$DESKTOP_FILE")" "$(dirname "$BIN_LINK")"

if [[ -f "$HERE/vitasync.png" ]]; then
    cp "$HERE/vitasync.png" "$ICON_FILE"
fi

cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=VitaSync
Comment=Companion app for a jailbroken PS Vita over Wi-Fi or USB
Exec=$INSTALL_DIR/vitasync
Icon=vitasync
Terminal=false
Categories=Utility;Game;
StartupWMClass=vitasync
EOF
chmod +x "$DESKTOP_FILE"

ln -sf "$INSTALL_DIR/vitasync" "$BIN_LINK"

command -v update-desktop-database >/dev/null 2>&1 \
    && update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

echo "Done."
echo
echo "Launch it from your application menu, or run: vitasync"
if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
    echo "(Note: $HOME/.local/bin is not on your PATH yet — add it to your shell profile, or use the menu entry.)"
fi
echo "To remove it later: $0 --uninstall"
