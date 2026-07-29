#!/usr/bin/env bash
# waydroid_setup.sh — Fedora KDE 43, Waydroid + Magisk + ARM + GApps + Frida
# Modes:  check | install | reinstall (default: check)
#
# Examples:
#   ./waydroid_setup.sh check              # diagnostics only, touches nothing
#   ./waydroid_setup.sh install            # install missing pieces, skip what exists
#   ./waydroid_setup.sh reinstall          # nuke everything, full rebuild
#   ./waydroid_setup.sh install --no-frida # skip frida phase
#   ./waydroid_setup.sh check --verbose    # extra debug output
set -euo pipefail

# ── defaults ──

MODE="check"
DO_FRIDA=1
DO_APK=1
DO_MAGISK=1
DO_ARM=1
DO_GAPPS=1
VERBOSE=0
APK_PATH="${HOME}/Downloads/some.apk"
MAGISK_DIR="${HOME}/Downloads/magiskWaydroid"
WAYDROID_SCRIPT_DIR="${HOME}/Downloads/waydroid_script"
FRIDA_HOST="192.168.240.112:27042"
FRIDA_VENV_DIR="${HOME}/.cache/waydroid-frida/.venv"
ARM_BACKEND="libhoudini" # libhoudini | libndk

# ── usage ──

usage() {
  cat <<'EOF'
Usage: waydroid_setup.sh <mode> [flags]

Modes:
  check       diagnostics only (default). read-only, changes nothing.
  install     install missing components. skips already-present phases.
  reinstall   full nuke + clean install from scratch.

Flags:
  --no-frida          skip frida server/client setup
  --no-apk            skip apk installation
  --no-magisk         skip magisk rooting
  --no-arm            skip ARM translation install
  --no-gapps          skip GApps install
  --arm-backend B     libhoudini (default) or libndk
  --apk PATH          path to .apk
  --magisk-dir DIR    magisk installer dir (default: ~/Downloads/magiskWaydroid)
  --script-dir DIR    casualsnek/waydroid_script dir (default: ~/Downloads/waydroid_script)
  --frida-host H:P    frida remote host:port (default: 192.168.240.112:27042)
  --verbose           extra debug output
  -h, --help          this message
EOF
  exit 0
}

# ── parse args ──

[[ $# -ge 1 && "$1" != -* ]] && {
  MODE="$1"
  shift
}

while [[ $# -gt 0 ]]; do
  case "$1" in
  --no-frida) DO_FRIDA=0 ;;
  --no-apk) DO_APK=0 ;;
  --no-magisk) DO_MAGISK=0 ;;
  --no-arm) DO_ARM=0 ;;
  --no-gapps) DO_GAPPS=0 ;;
  --arm-backend)
    ARM_BACKEND="$2"
    shift
    ;;
  --apk)
    APK_PATH="$2"
    shift
    ;;
  --magisk-dir)
    MAGISK_DIR="$2"
    shift
    ;;
  --script-dir)
    WAYDROID_SCRIPT_DIR="$2"
    shift
    ;;
  --frida-host)
    FRIDA_HOST="$2"
    shift
    ;;
  --verbose) VERBOSE=1 ;;
  -h | --help) usage ;;
  *)
    echo "unknown flag: $1" >&2
    usage
    ;;
  esac
  shift
done

# ── helpers ──

err() {
  printf '\e[31mFAIL: %s\e[0m\n' "$1" >&2
  exit 1
}
warn() { printf '\e[33mWARN: %s\e[0m\n' "$1" >&2; }
ok() { printf '\e[32m  OK: %s\e[0m\n' "$1"; }
info() { printf '\e[36m=== %s ===\e[0m\n' "$1"; }
dbg() { ((VERBOSE)) && printf '\e[90m  -- %s\e[0m\n' "$1" || true; }

has() { command -v "$1" &>/dev/null; }
require() { has "$1" || err "'$1' not in PATH"; }

is_container_running() {
  systemctl is-active --quiet waydroid-container 2>/dev/null
}

is_session_running() {
  waydroid status 2>/dev/null | grep -qi "running"
}

shell_prop() {
  sudo waydroid shell -- getprop "$1" 2>/dev/null || true
}

wait_boot() {
  info "waiting for android boot"
  for _ in $(seq 1 30); do
    shell_prop sys.boot_completed | grep -q 1 && return 0
    sleep 2
  done
  err "android did not boot in 60s"
}

ensure_waydroid_script() {
  if [[ ! -d "$WAYDROID_SCRIPT_DIR" ]]; then
    info "cloning casualsnek/waydroid_script"
    git clone --depth=1 https://github.com/casualsnek/waydroid_script "$WAYDROID_SCRIPT_DIR"
  fi
  if [[ ! -f "$WAYDROID_SCRIPT_DIR/venv/bin/python3" ]]; then
    info "setting up waydroid_script venv"
    python3 -m venv "$WAYDROID_SCRIPT_DIR/venv"
    "$WAYDROID_SCRIPT_DIR/venv/bin/pip" install -r "$WAYDROID_SCRIPT_DIR/requirements.txt"
  fi
}

waydroid_script_run() {
  sudo "$WAYDROID_SCRIPT_DIR/venv/bin/python3" "$WAYDROID_SCRIPT_DIR/main.py" "$@"
}

# ── check functions (read-only, return 0/1) ──

check_host_deps() {
  info "host dependencies"
  local rc=0
  for cmd in sudo dnf python3 git wget; do
    if has "$cmd"; then ok "$cmd"; else
      warn "$cmd missing"
      rc=1
    fi
  done
  if has uv; then ok "uv (preferred)"; else warn "uv not found, will fallback to python3 -m venv"; fi
  return $rc
}

check_waydroid_installed() {
  info "waydroid package"
  if has waydroid && [[ -f /var/lib/waydroid/images/system.img ]]; then
    ok "waydroid installed, images present"
    dbg "system.img: $(du -h /var/lib/waydroid/images/system.img 2>/dev/null | cut -f1)"
    return 0
  else
    warn "waydroid not installed or images missing"
    return 1
  fi
}

check_container() {
  info "waydroid container"
  if is_container_running; then
    ok "waydroid-container.service active"
    return 0
  else
    warn "waydroid-container.service not running"
    return 1
  fi
}

check_session() {
  info "waydroid session"
  if is_session_running; then
    ok "session running"
    return 0
  else
    warn "session not running (need 'waydroid show-full-ui')"
    return 1
  fi
}

check_root() {
  info "root (magisk)"
  local uid
  uid="$(sudo waydroid shell -- id 2>/dev/null || true)"
  if grep -q 'uid=0' <<<"$uid"; then
    ok "uid=0 (root)"
    local mgk
    mgk="$(sudo waydroid shell -- pm list packages 2>/dev/null | grep -i magisk || true)"
    [[ -n "$mgk" ]] && ok "magisk package: $mgk"
    return 0
  else
    warn "not root or shell unreachable (id='$uid')"
    return 1
  fi
}

check_arm_translation() {
  info "ARM translation"
  local bridge abi32 abi64
  bridge="$(shell_prop ro.dalvik.vm.native.bridge)"
  abi32="$(shell_prop ro.product.cpu.abilist32)"
  abi64="$(shell_prop ro.product.cpu.abilist64)"

  if [[ -z "$bridge" || "$bridge" == "0" ]]; then
    warn "no ARM translation layer (bridge='$bridge')"
    return 1
  fi
  ok "bridge: $bridge"

  if grep -Eq 'armeabi|arm64' <<<"${abi32} ${abi64}"; then
    ok "abi32=$abi32  abi64=$abi64"
  else
    warn "ARM ABIs not advertised (abi32='$abi32' abi64='$abi64')"
  fi

  if sudo waydroid shell -- ls /system/lib64/libhoudini.so &>/dev/null; then
    ok "/system/lib64/libhoudini.so present"
  elif sudo waydroid shell -- ls /system/lib64/libndk_translation.so &>/dev/null; then
    ok "/system/lib64/libndk_translation.so present"
  else
    warn "no known translation .so found in /system/lib64/"
  fi
  return 0
}

check_gapps() {
  info "GApps"
  local vending gms gsf
  vending="$(sudo waydroid shell -- pm path com.android.vending 2>/dev/null || true)"
  gms="$(sudo waydroid shell -- pm path com.google.android.gms 2>/dev/null || true)"
  gsf="$(sudo waydroid shell -- pm path com.google.android.gsf 2>/dev/null || true)"

  if [[ -n "$vending" && -n "$gms" && -n "$gsf" ]]; then
    ok "play store: $vending"
    ok "gms: $gms"
    ok "gsf: $gsf"
    return 0
  else
    [[ -z "$vending" ]] && warn "com.android.vending (play store) missing"
    [[ -z "$gms" ]] && warn "com.google.android.gms missing"
    [[ -z "$gsf" ]] && warn "com.google.android.gsf missing"
    return 1
  fi
}

check_network() {
  info "network (inside android)"
  local rc=0

  if sudo waydroid shell -- ip route 2>/dev/null | grep -q default; then
    ok "default route present"
    dbg "$(sudo waydroid shell -- ip route 2>/dev/null)"
  else
    warn "no default route (may be false positive if pings pass)"
  fi

  if sudo waydroid shell -- ping -c1 -W3 8.8.8.8 &>/dev/null; then
    ok "ping 8.8.8.8"
  else
    warn "ping 8.8.8.8 failed"
    rc=1
  fi

  if sudo waydroid shell -- ping -c1 -W3 google.com &>/dev/null; then
    ok "ping google.com (DNS works)"
  else
    warn "ping google.com failed (DNS broken?)"
    rc=1
  fi

  local dns1 dns2
  dns1="$(shell_prop net.dns1)"
  dns2="$(shell_prop net.dns2)"
  dbg "net.dns1=$dns1  net.dns2=$dns2"

  return $rc
}

check_frida() {
  info "frida"
  local rc=0
  if [[ -x "${FRIDA_VENV_DIR}/bin/frida" ]]; then
    ok "frida client: $("${FRIDA_VENV_DIR}/bin/frida" --version)"
  else
    warn "frida client not installed in $FRIDA_VENV_DIR"
    rc=1
  fi

  if sudo waydroid shell -- test -x /system/bin/frida-server 2>/dev/null; then
    ok "frida-server present on android"
  else
    warn "frida-server not found on android"
    rc=1
  fi

  if "${FRIDA_VENV_DIR}/bin/frida-ps" -H "$FRIDA_HOST" &>/dev/null; then
    ok "frida-ps -H $FRIDA_HOST reachable"
  else
    warn "frida-ps -H $FRIDA_HOST unreachable (server not running?)"
    rc=1
  fi

  return $rc
}

# ── install functions ──

do_nuke() {
  info "NUKE"
  sudo waydroid session stop 2>/dev/null || true
  sudo waydroid container stop 2>/dev/null || true
  sudo systemctl stop waydroid-container 2>/dev/null || true
  sudo rm -rf /var/lib/waydroid
  sudo rm -rf /etc/waydroid-extra
  sudo rm -rf "$HOME/.local/share/waydroid" 2>/dev/null || true
  sudo rm -f "$HOME/.local/share/applications/"*aydroid* 2>/dev/null || true
  sudo dnf remove -y waydroid 2>/dev/null || true
  ok "nuked"
}

do_install_waydroid() {
  info "INSTALL WAYDROID"
  sudo dnf install -y waydroid lzip sqlite wget xz
  sudo waydroid init -c https://ota.waydro.id/system -v https://ota.waydro.id/vendor
  sudo systemctl enable --now waydroid-container
  ok "waydroid installed"
}

do_boot() {
  info "BOOT"
  if ! is_session_running; then
    waydroid show-full-ui &
    wait_boot
  else
    ok "already running"
  fi
}

do_magisk() {
  info "MAGISK"
  [[ -d "$MAGISK_DIR" ]] || err "magisk dir not found: $MAGISK_DIR"
  [[ -f "$MAGISK_DIR/magisk" ]] || err "magisk script not found in $MAGISK_DIR"

  pushd "$MAGISK_DIR" >/dev/null
  sudo ./magisk install
  popd >/dev/null

  sudo systemctl restart waydroid-container
  waydroid show-full-ui &
  wait_boot
  ok "magisk installed"
}

do_arm_translation() {
  info "ARM TRANSLATION ($ARM_BACKEND)"
  ensure_waydroid_script

  sudo waydroid session stop 2>/dev/null || true

  waydroid_script_run uninstall libhoudini 2>/dev/null || true
  waydroid_script_run uninstall libndk 2>/dev/null || true

  waydroid_script_run install "$ARM_BACKEND"

  sudo systemctl restart waydroid-container
  waydroid show-full-ui &
  wait_boot
  ok "$ARM_BACKEND installed"
}

do_gapps() {
  info "GAPPS"
  ensure_waydroid_script

  sudo waydroid session stop 2>/dev/null || true
  waydroid_script_run install gapps

  sudo systemctl restart waydroid-container
  waydroid show-full-ui &
  wait_boot
  ok "gapps installed"

  echo ""
  echo "  next steps:"
  echo "    1. run: cd $WAYDROID_SCRIPT_DIR && sudo venv/bin/python3 main.py certified"
  echo "    2. register device id at https://google.com/android/uncertified"
  echo "    3. wait 10-20 min"
  echo "    4. run: sudo waydroid shell -- pm clear com.google.android.gms"
  echo "    5. restart waydroid, sign into play store"
  echo ""
}

do_apk() {
  info "APK"
  if [[ -f "$APK_PATH" ]]; then
    waydroid app install "$APK_PATH"
    ok "installed: $(basename "$APK_PATH")"
  else
    warn "apk not found: $APK_PATH"
  fi
  sudo waydroid shell -- pm list packages -3
}

do_frida() {
  info "FRIDA"
  mkdir -p "${HOME}/.cache/waydroid-frida"

  if has uv; then
    uv venv "$FRIDA_VENV_DIR"
    uv pip install --python "$FRIDA_VENV_DIR/bin/python" frida frida-tools
  else
    python3 -m venv "$FRIDA_VENV_DIR"
    "$FRIDA_VENV_DIR/bin/pip" install -U pip
    "$FRIDA_VENV_DIR/bin/pip" install frida frida-tools
  fi

  local FRIDA_BIN="$FRIDA_VENV_DIR/bin/frida"
  local FRIDA_PS_BIN="$FRIDA_VENV_DIR/bin/frida-ps"
  local FRIDA_VER
  FRIDA_VER="$($FRIDA_BIN --version)"
  ok "frida client: $FRIDA_VER"

  local FRIDA_URL="https://github.com/frida/frida/releases/download/${FRIDA_VER}/frida-server-${FRIDA_VER}-android-x86_64.xz"
  local FRIDA_TMP
  FRIDA_TMP="$(mktemp -d)"
  trap 'rm -rf "$FRIDA_TMP"' EXIT

  wget -q "$FRIDA_URL" -O "$FRIDA_TMP/frida-server.xz" || err "download failed: $FRIDA_URL"
  unxz -f "$FRIDA_TMP/frida-server.xz"

  mkdir -p "$HOME/.local/share/waydroid/data/local/tmp"
  sudo cp "$FRIDA_TMP/frida-server" "$HOME/.local/share/waydroid/data/local/tmp/frida-server"

  sudo waydroid shell -- cp /data/local/tmp/frida-server /system/bin/frida-server
  sudo waydroid shell -- chmod 755 /system/bin/frida-server
  sudo waydroid shell -- pkill frida-server 2>/dev/null || true
  sudo waydroid shell -- /system/bin/frida-server -l 0.0.0.0:27042 -D

  sleep 2
  $FRIDA_PS_BIN -H "$FRIDA_HOST" | head -5 && ok "frida: ok" || err "frida unreachable on $FRIDA_HOST"
}

# ── main ──

case "$MODE" in
check)
  check_host_deps || true
  check_waydroid_installed || true
  check_container || true
  if check_session; then
    check_root || true
    check_arm_translation || true
    check_gapps || true
    check_network || true
    ((DO_FRIDA)) && { check_frida || true; }
  fi
  info "CHECK DONE"
  ;;

install)
  check_host_deps || err "install host deps first"
  if ! check_waydroid_installed; then
    do_install_waydroid
  fi
  if ! check_container; then
    sudo systemctl enable --now waydroid-container
  fi
  do_boot
  if ((DO_MAGISK)); then
    check_root || do_magisk
  fi
  if ((DO_ARM)); then
    check_arm_translation || do_arm_translation
  fi
  if ((DO_GAPPS)); then
    check_gapps || do_gapps
  fi
  check_network || warn "no internet inside android"
  ((DO_APK)) && do_apk
  ((DO_FRIDA)) && { check_frida || do_frida; }
  info "INSTALL DONE"
  ;;

reinstall)
  check_host_deps || err "install host deps first"
  do_nuke
  do_install_waydroid
  do_boot
  ((DO_MAGISK)) && do_magisk
  ((DO_ARM)) && do_arm_translation
  ((DO_GAPPS)) && do_gapps
  check_root || warn "root not obtained"
  check_network || warn "no internet inside android"
  ((DO_APK)) && do_apk
  ((DO_FRIDA)) && do_frida
  info "REINSTALL DONE"
  ;;

*)
  echo "unknown mode: $MODE" >&2
  usage
  ;;
esac
