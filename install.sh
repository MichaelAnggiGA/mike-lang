#!/bin/sh
# ============================================================================
#  install.sh — pasang compiler Mike sebagai command `mike` (Linux & FreeBSD)
#
#  Pemakaian:
#     ./install.sh              # per-user (tanpa sudo): ~/.local/bin
#     sudo ./install.sh --system   # sistem: /usr/local/bin
#
#  Setelah per-user install, pastikan ~/.local/bin ada di PATH.
# ============================================================================
set -e

SYSTEM=0
[ "$1" = "--system" ] && SYSTEM=1

# --- deteksi OS (informasional) ---
OS="$(uname -s 2>/dev/null || echo unknown)"

# --- lokasi ---
if [ "$SYSTEM" -eq 1 ]; then
    BIN_DIR="/usr/local/bin"
    LIB_DIR="/usr/local/share/mike/lib"
else
    BIN_DIR="$HOME/.local/bin"
    LIB_DIR="$HOME/.mike/lib"
fi

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Mike installer"
echo "  OS terdeteksi   : $OS"
echo "  compiler source : $SRC_DIR/mike2.c"
echo "  binary          -> $BIN_DIR/mike"
echo "  libraries       -> $LIB_DIR"
echo

# --- cek toolchain ---
if ! command -v cc >/dev/null 2>&1; then
    echo "error: 'cc' tidak ditemukan (butuh compiler C: gcc di Linux, clang di FreeBSD)"
    exit 1
fi

echo "Membangun compiler..."
cc -O2 -o "$SRC_DIR/mike" "$SRC_DIR/mike2.c"

# --- pasang binary ---
mkdir -p "$BIN_DIR"
cp "$SRC_DIR/mike" "$BIN_DIR/mike"
chmod +x "$BIN_DIR/mike"

# --- pasang library ---
mkdir -p "$LIB_DIR"
cp "$SRC_DIR"/lib/*.mik "$LIB_DIR/"

echo
echo "Terpasang:"
echo "  $BIN_DIR/mike"
echo "  $(ls "$LIB_DIR"/*.mik | wc -l | tr -d ' ') library di $LIB_DIR"
echo

# --- cek PATH ---
case ":$PATH:" in
    *":$BIN_DIR:"*) : ;;
    *)
        echo "CATATAN: $BIN_DIR belum ada di PATH. Tambahkan dengan:"
        echo "  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.profile"
        echo "  (atau ~/.bashrc di Linux). Lalu buka ulang terminal."
        echo
        ;;
esac

echo "Selesai. Coba:"
echo "  mike hello.mik && ./hello"
