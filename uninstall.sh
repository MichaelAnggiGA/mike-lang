#!/bin/sh
# Remove a Mike installation (both per-user and system locations).
#   ./uninstall.sh           # remove per-user install
#   sudo ./uninstall.sh --system   # remove system install
set -e

SYSTEM=0
[ "$1" = "--system" ] && SYSTEM=1

if [ "$SYSTEM" -eq 1 ]; then
    rm -f  /usr/local/bin/mike
    rm -rf /usr/local/share/mike
    echo "Removed system install (/usr/local/bin/mike, /usr/local/share/mike)."
else
    rm -f  "$HOME/.local/bin/mike"
    rm -rf "$HOME/.mike"
    echo "Removed per-user install (~/.local/bin/mike, ~/.mike)."
fi
