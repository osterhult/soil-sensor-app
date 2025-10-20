 #!/usr/bin/env bash
# Try west build for every NCS v>=3.0 workspace + toolchain combination
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="${REPO_ROOT}/nrfconnect"
BUILD_ROOT="${APP_DIR}/build"
PRJ_BUILD="${BUILD_ROOT}/nrfconnect"
BOARD="nrf7002dk/nrf5340/cpuapp"
CHIP_ROOT="${REPO_ROOT}/connectedhomeip_stable"

# Roots to scan for west workspaces and toolchains
SDK_SCAN_ROOTS=(
  "/opt/nordic/ncs"
  "/Users/osterhult/Kod/Matter/ncs"
  "/Users/osterhult/Kod/Matter/ncs/ncs_stable"
  "/Users/osterhult/Kod/Matter/ncs-chip"
)
TOOLCHAIN_SCAN_ROOTS=(
  "/opt/nordic/ncs/toolchains"
)

ensure_overlay() {
  local overlay="${APP_DIR}/overlay-disable-external-rng.conf"
  [[ -f "$overlay" ]] && return
  cat >"$overlay" <<'EOF'
CONFIG_MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG=n
CONFIG_MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG_ALLOW_NON_CSPRNG=n
EOF
}

find_workspaces() {
  local dir
  for dir in "${SDK_SCAN_ROOTS[@]}"; do
    [[ -d "$dir" ]] || continue
    find "$dir" -type f -path "*/.west/config" 2>/dev/null
  done |
  sed 's#/.west/config$##' |
  awk -F/ '{
    for (i=NF; i>0; --i) {
      if ($i ~ /^v[0-9]+\.[0-9]+/) {
        split(substr($i,2),a,".");
        if (a[1] >= 3) { print; break; }
        else break;
      }
    }
  }' |
  sort -u
}

find_toolchains() {
  local dir
  for dir in "${TOOLCHAIN_SCAN_ROOTS[@]}"; do
    [[ -d "$dir" ]] || continue
    find "$dir" -maxdepth 2 -type d -name "opt" 2>/dev/null
  done |
  sed 's#/opt$##' |
  awk -F/ '{
    tag=$(NF);
    if (tag ~ /^v[0-9]+\.[0-9]+/) {
      split(substr(tag,2),a,".");
      if (a[1] >= 3) print;
    } else if (tag ~ /^[A-Za-z0-9]+$/) {
      print;
    }
  }' |
  sort -u
}

run_build() {
  local workspace="$1"
  local toolchain="$2"

  [[ -d "$workspace/zephyr" ]] || return 1
  [[ -d "$toolchain/opt/zephyr-sdk" ]] || return 1
  [[ -x "$toolchain/bin/python3" ]] || return 1

  local python_version
  python_version="$("$toolchain/bin/python3" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null || echo "0.0")"
  local python_major=${python_version%%.*}
  local python_minor=${python_version#*.}
  if [[ "$python_major" -lt 3 || ( "$python_major" -eq 3 && "$python_minor" -lt 10 ) ]]; then
    echo "[INFO] Skipping toolchain $toolchain (python $python_version < 3.10)"
    return 1
  fi

  echo
  echo "===================================================================="
  echo "Attempting west build with:"
  echo "  SDK workspace  : $workspace"
  echo "  Toolchain root : $toolchain"
  echo "===================================================================="

  rm -rf "$BUILD_ROOT"

  export CHIP_ROOT
  export ZEPHYR_BASE="$workspace/zephyr"
  export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
  export ZEPHYR_SDK_INSTALL_DIR="$toolchain/opt/zephyr-sdk"
  export WEST_PYTHON="$toolchain/bin/python3"
  export PATH="$toolchain/bin:${PATH}"
  unset PYENV_VERSION

  export ZEPHYR_KCONFIG_WARNINGS_FATAL=0
  export KCONFIG_WARNINGS_FATAL=0
  export SYSBUILD_KCONFIG_WARNINGS_FATAL=0

  ensure_overlay
  export PYTHONPATH="${APP_DIR}/python_shims"

  pushd "$workspace" >/dev/null
  if "$WEST_PYTHON" -m west build -p -b "$BOARD" --sysbuild \
      "$APP_DIR" -- \
        -DOVERLAY_CONFIG="prj.conf;overlay-disable-external-rng.conf" \
        -DKCONFIG_WARNINGS_FATAL=OFF \
        -DSYSBUILD_KCONFIG_WARNINGS_FATAL=OFF \
        -DZEPHYR_KCONFIG_WARNINGS_FATAL=OFF \
      2>&1 | tee /tmp/west_build.log; then
    popd >/dev/null
    echo
    echo "===================================================================="
    echo "SUCCESS with:"
    echo "  SDK       : $workspace"
    echo "  Toolchain : $toolchain"
    echo "  Output    : $PRJ_BUILD"
    echo "===================================================================="
    exit 0
  fi
  popd >/dev/null

  echo "[WARN] Build failed for sdk=$workspace toolchain=$toolchain"
  echo "       See /tmp/west_build.log for details."
  return 1
}

main() {
  local workspaces=($(find_workspaces))
  local toolchains=($(find_toolchains))

  if [[ ${#workspaces[@]} -eq 0 ]]; then
    echo "[ERROR] No west workspace (v>=3.0) found under:"
    printf '  %s\n' "${SDK_SCAN_ROOTS[@]}"
    exit 1
  fi
  if [[ ${#toolchains[@]} -eq 0 ]]; then
    echo "[ERROR] No toolchains found under:"
    printf '  %s\n' "${TOOLCHAIN_SCAN_ROOTS[@]}"
    exit 1
  fi

  local attempts=()
  for ws in "${workspaces[@]}"; do
    for tc in "${toolchains[@]}"; do
      attempts+=("$ws :: $tc")
      if run_build "$ws" "$tc"; then
        exit 0
      fi
    done
  done

  echo
  echo "===================================================================="
  echo "All combinations failed. Tried:"
  printf '  - %s\n' "${attempts[@]}"
  echo
  echo "Last failure log: /tmp/west_build.log"
  echo "Adjust SDK/toolchain paths if needed, then run this script again."
  echo "===================================================================="
  exit 1
}

main "$@"
