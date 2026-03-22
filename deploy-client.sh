#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────
#  deploy-client.sh  —  Build and install NetCommand Client
#
#  Usage:
#    sudo ./deploy-client.sh <server-ip> [port]
#
#  What it does:
#    1. Detects platform (macOS / Linux)
#    2. Installs system dependencies (if possible)
#    3. Downloads stb_image_write.h
#    4. Builds the client binary
#    5. Installs it to /usr/local/bin
#    6. Registers it as a system daemon (launchd / systemd)
#       so it starts automatically on boot
# ─────────────────────────────────────────────────────────────
set -e

SERVER_IP="${1:-}"
PORT="${2:-7890}"

if [[ -z "$SERVER_IP" ]]; then
  echo "Usage: sudo $0 <server-ip> [port]"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR"
BINARY_NAME="netcommand-client"
INSTALL_PATH="/usr/local/bin/$BINARY_NAME"

echo "========================================"
echo " NetCommand Client — Deploy Script"
echo " Server: $SERVER_IP:$PORT"
echo "========================================"

# ── 1. Platform ────────────────────────────────────────────
OS="$(uname -s)"
echo "[1/5] Platform: $OS"

# ── 2. Dependencies ────────────────────────────────────────
echo "[2/5] Checking dependencies..."

if [[ "$OS" == "Linux" ]]; then
  if command -v apt-get &>/dev/null; then
    apt-get install -y --quiet g++ libx11-dev libxtst-dev make curl
  elif command -v dnf &>/dev/null; then
    dnf install -y gcc-c++ libX11-devel libXtst-devel make curl
  elif command -v pacman &>/dev/null; then
    pacman -S --noconfirm gcc libx11 libxtst make curl
  else
    echo "  Warning: Could not auto-install deps. Ensure g++, libX11, libXtst are installed."
  fi
elif [[ "$OS" == "Darwin" ]]; then
  if ! xcode-select -p &>/dev/null; then
    echo "  Installing Xcode Command Line Tools..."
    xcode-select --install
    echo "  Re-run this script after installation completes."
    exit 0
  fi
fi

# ── 3. Download stb ────────────────────────────────────────
echo "[3/5] Fetching stb_image_write.h..."
if [[ ! -f "$CLIENT_DIR/stb_image_write.h" ]]; then
  curl -sL \
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" \
    -o "$CLIENT_DIR/stb_image_write.h"
  echo "  Downloaded."
else
  echo "  Already present."
fi

# ── 4. Build ───────────────────────────────────────────────
echo "[4/5] Building..."
cd "$CLIENT_DIR"
make clean 2>/dev/null || true
make -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"
echo "  Build successful: $BINARY_NAME"

# ── 5. Install + Register daemon ──────────────────────────
echo "[5/5] Installing daemon..."
cp "$BINARY_NAME" "$INSTALL_PATH"
chmod 755 "$INSTALL_PATH"

"$INSTALL_PATH" --install "$SERVER_IP" "$PORT"

echo ""
echo "========================================"
echo " Done! NetCommand Client is running."
echo " Server: $SERVER_IP:$PORT"
if [[ "$OS" == "Linux" ]]; then
  echo " Status: systemctl status netcommand-client"
  echo " Logs:   journalctl -u netcommand-client -f"
elif [[ "$OS" == "Darwin" ]]; then
  echo " Status: launchctl list | grep netcommand"
  echo " Remove: sudo $INSTALL_PATH --uninstall"
fi
echo "========================================"
