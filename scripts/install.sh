#!/usr/bin/env bash
# WeMod Enhancer one-liner (Linux / Steam Deck).
#
# Close WeMod first, then paste one line:
#
#   curl -fsSL https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.sh | bash
#
# What it does: downloads the latest release into ~/wemod_enhancer and
# patches ~/wemod-launcher/wemod_data/wemod_bin. Safe to re-run: patching
# starts from the automatic backup, so it never stacks changes.
#
# Variants:
#   curl -fsSL .../install.sh | bash -s -- restore   # undo: originals back
#   curl -fsSL .../install.sh | bash -s -- status    # check: patched or not
#   curl -fsSL .../install.sh | bash -s -- doctor    # diagnose: env + install
#
# Env overrides: WORK_DIR (default ~/wemod_enhancer),
# INSTALL_DIR (default ~/wemod-launcher/wemod_data/wemod_bin).
set -euo pipefail

PACKAGE_URL="https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-llvm-mingw-amd64.tar.xz"
LAUNCHER_URL="https://github.com/DeckCheatz/wemod-launcher"
ACTION="${1:-patch}"
WORK_DIR="${WORK_DIR:-$HOME/wemod_enhancer}"
INSTALL_DIR="${INSTALL_DIR:-$HOME/wemod-launcher/wemod_data/wemod_bin}"

say() { printf '== %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

need() {
  # need <tool> "<install hint>"
  command -v "$1" >/dev/null 2>&1 || die "$1 not found. $2"
}

check_action() {
  case "$ACTION" in
    patch | restore | status | doctor) ;;
    *) die "unknown action '$ACTION'. Use: patch | restore | status | doctor." ;;
  esac
}

ensure_launcher() {
  # The WeMod client only exists after the launcher ran once + login.
  if [ -d "$HOME/wemod-launcher" ]; then
    return 0
  fi
  say "First run: installing wemod-launcher (one-time setup)..."
  need git "Install git with your package manager, then re-run."
  git clone "$LAUNCHER_URL" "$HOME/wemod-launcher"
  chmod +x "$HOME/wemod-launcher/wemod"
  say "Next: launch a game once through wemod-launcher and log in, then re-run this script."
  exit 0
}

download_package() {
  say "Downloading WeMod Enhancer..."
  local tmp_pkg
  tmp_pkg="$(mktemp --suffix=-wemod-enhancer.tar.xz)"
  trap 'rm -f "$tmp_pkg"' EXIT
  curl -fsSL "$PACKAGE_URL" -o "$tmp_pkg"
  mkdir -p "$WORK_DIR"
  tar -xf "$tmp_pkg" -C "$WORK_DIR"
  rm -f "$tmp_pkg"
  trap - EXIT
}

find_patcher() {
  # Prints the patcher path or fails with a fix-it message.
  if [ -f "$WORK_DIR/bin/wemod_enhancer.py" ]; then
    printf '%s' "$WORK_DIR/bin/wemod_enhancer.py"
  elif [ -f "$WORK_DIR/wemod_enhancer.py" ]; then
    printf '%s' "$WORK_DIR/wemod_enhancer.py"
  else
    die "patcher not found inside $WORK_DIR (bad download?). Delete the folder and re-run."
  fi
}

# --- run -------------------------------------------------------------------

check_action

need python3 "Install Python 3.11+ with your package manager (preinstalled on SteamOS), then re-run."
need curl "Install curl with your package manager, then re-run."
need tar "Install tar with your package manager, then re-run."

ensure_launcher
download_package

PATCHER="$(find_patcher)"

if [ "$ACTION" = "patch" ] && [ ! -f "$INSTALL_DIR/resources/app.asar" ]; then
  die "$INSTALL_DIR/resources/app.asar not found. Launch a game once + log in first (wemod_bin appears after first login)."
fi

python3 "$PATCHER" "$ACTION" --install-dir "$INSTALL_DIR"

say "Done ($ACTION). Launch WeMod, Pro is active."
echo "Steam launch options: WINEDLLOVERRIDES=\"version=n,b\" \"\$HOME/wemod-launcher/wemod\" %command%"
