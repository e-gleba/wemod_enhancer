#!/usr/bin/env bash
# WeMod Enhancer one-liner (Linux / Steam Deck).
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.sh | bash
#   curl -fsSL .../install.sh | bash -s -- restore
#   curl -fsSL .../install.sh | bash -s -- status
set -euo pipefail

REPO="e-gleba/wemod_enhancer"
ACTION="${1:-patch}"
WORK_DIR="${WORK_DIR:-$HOME/wemod_enhancer}"
INSTALL_DIR="${INSTALL_DIR:-$HOME/wemod-launcher/wemod_data/wemod_bin}"
PACKAGE_URL="https://github.com/${REPO}/releases/latest/download/wemod_enhancer-windows-llvm-mingw-amd64.tar.xz"

log()  { printf '\033[1;36m== %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m%s\033[0m\n' "$*"; }
warn() { printf '\033[1;33mwarning: %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[1;31merror: %s\033[0m\n' "$*" >&2; exit 1; }

command -v python3 >/dev/null 2>&1 || die "python3 not found (need 3.11+). Install it with your package manager, then re-run."
command -v curl >/dev/null 2>&1 || die "curl not found. Install curl, then re-run."
command -v tar >/dev/null 2>&1 || die "tar not found. Install tar, then re-run."

if [ ! -d "$HOME/wemod-launcher" ]; then
  warn "~/wemod-launcher not found - installing wemod-launcher first."
  git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
  chmod +x "$HOME/wemod-launcher/wemod"
  log "launch a game once through wemod-launcher and log in, then re-run this script."
fi

log "download $PACKAGE_URL"
tmp_pkg="$(mktemp --suffix=-wemod-enhancer.tar.xz)"
trap 'rm -f "$tmp_pkg"' EXIT
curl -fsSL "$PACKAGE_URL" -o "$tmp_pkg"
mkdir -p "$WORK_DIR"
tar -xf "$tmp_pkg" -C "$WORK_DIR"
rm -f "$tmp_pkg"
trap - EXIT

PATCHER="$WORK_DIR/bin/wemod_enhancer.py"
[ -f "$PATCHER" ] || PATCHER="$WORK_DIR/wemod_enhancer.py"
[ -f "$PATCHER" ] || die "patcher not found inside $WORK_DIR."

case "$ACTION" in
  patch)
    [ -f "$INSTALL_DIR/resources/app.asar" ] || die "$INSTALL_DIR/resources/app.asar not found. Launch a game once + log in first (wemod_bin appears after first login)."
    python3 "$PATCHER" patch --install-dir "$INSTALL_DIR"
    ;;
  restore)
    python3 "$PATCHER" restore --install-dir "$INSTALL_DIR"
    ;;
  status|doctor)
    python3 "$PATCHER" "$ACTION" --install-dir "$INSTALL_DIR"
    ;;
  *)
    die "unknown action '$ACTION'. Use: patch | restore | status | doctor."
    ;;
esac

ok "done ($ACTION). Launch WeMod - Pro is active."
echo "Steam launch options: WINEDLLOVERRIDES=\"version=n,b\" \"\$HOME/wemod-launcher/wemod\" %command%"
