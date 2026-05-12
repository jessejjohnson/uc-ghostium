#!/usr/bin/env bash
#
# Ghostium one-shot build.
#
# SSH into a fresh Ubuntu 22.04 (or 24.04) x86_64 host and run:
#
#     git clone https://github.com/jessejjohnson/uc-ghostium.git ghostium && cd ghostium
#     ./build.sh
#
# The script self-wraps in a tmux session named "ghostium-build" so SSH
# disconnects do not kill the build. Reattach with:
#
#     tmux attach -t ghostium-build
#
# All output is tee'd to logs/build-YYYYMMDD-HHMMSS.log.
#
# Build flow (follows third_party/ungoogled-chromium/docs/building.md):
#
#   apt          : minimal host packages to bootstrap
#   submodule    : git submodule update --init    (UC submodule)
#   download     : utils/downloads.py retrieve    (resumable tarball + hash)
#   unpack       : utils/downloads.py unpack      (atomic, hash-verified)
#   depot        : clone depot_tools              (only for gn + gclient runhooks)
#   runhooks     : write .gclient + gclient runhooks (clang, sysroot, rust, node)
#   build-deps   : src/build/install-build-deps.sh --no-prompt
#   prune        : UC prune_binaries.py           (binary blob removal)
#   overlay      : scripts/sync_overlay.py        (symlink Ghostium overlay)
#   patches      : scripts/apply_patches.py       (UC series + Ghostium series)
#   domsub       : UC domain_substitution.py apply
#   gn           : gn gen out/Default --args="<UC flags.gn> <config/gn_args.gn>"
#   build        : ninja -C out/Default chrome
#   test         : build + run overlay unit tests
#
# UC requires the order prune -> patches -> domsub. Substituting domains
# before patches corrupts the patch context. See UC docs/building.md.
#
# Recommended sizing:
#   * c6a.16xlarge / c7i.24xlarge  (CPU-bound, 32-96 vCPU)
#   * >= 32 GiB RAM
#   * >= 200 GiB gp3 EBS mounted on $CHROMIUM_ROOT
#
# Idempotent: every stage detects "already done" and skips. After a
# transient failure, just re-run the script.

set -euo pipefail

# ----------------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------------

GHOSTIUM_ROOT="${GHOSTIUM_ROOT:-$(cd "$(dirname "$0")" && pwd)}"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-${HOME}/chromium-build}"
CHROMIUM_SRC="${CHROMIUM_ROOT}/src"
DOWNLOAD_CACHE="${DOWNLOAD_CACHE:-${CHROMIUM_ROOT}/downloads}"
DEPOT_TOOLS_DIR="${DEPOT_TOOLS_DIR:-${CHROMIUM_ROOT}/depot_tools}"
CCACHE_DIR="${CCACHE_DIR:-${CHROMIUM_ROOT}/.ccache}"
GN_OUT="${GN_OUT:-${CHROMIUM_SRC}/out/Default}"
BUILD_TARGET="${BUILD_TARGET:-chrome}"
TMUX_SESSION="${TMUX_SESSION:-ghostium-build}"
LOG_DIR="${LOG_DIR:-${GHOSTIUM_ROOT}/logs}"
MIN_FREE_GIB_DISK="${MIN_FREE_GIB_DISK:-150}"
MIN_RAM_GIB="${MIN_RAM_GIB:-16}"
WARN_RAM_GIB="${WARN_RAM_GIB:-32}"

UC_ROOT="${GHOSTIUM_ROOT}/third_party/ungoogled-chromium"

STAGES_DEFAULT=(apt submodule download unpack depot runhooks build-deps prune overlay patches domsub gn build)
STAGES_ALL=(apt submodule download unpack depot runhooks build-deps prune overlay patches domsub gn build test)

# ----------------------------------------------------------------------------
# Argument parsing
# ----------------------------------------------------------------------------

RUN_TEST=0
NO_BUILD=0
FROM_STAGE=""
ONLY_STAGE=""
NO_TMUX="${GHOSTIUM_NO_TMUX:-0}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Default: run the full pipeline (apt -> ... -> build), inside tmux.

Options:
  --no-build           Stop after 'gn'. Skip the long 'build' stage.
  --test               Also run overlay unit tests after build.
  --from STAGE         Resume: skip stages before STAGE.
  --only STAGE         Run only STAGE.
  --no-tmux            Do not self-wrap in tmux (run inline).
  --chromium-root P    Override checkout root (default: ${CHROMIUM_ROOT}).
  --build-target T     Override ninja target (default: ${BUILD_TARGET}).
  -h, --help           Show this help.

Stages (in order):
  ${STAGES_ALL[*]}

Environment overrides:
  GHOSTIUM_ROOT, CHROMIUM_ROOT, DOWNLOAD_CACHE, DEPOT_TOOLS_DIR,
  CCACHE_DIR, GN_OUT, BUILD_TARGET, TMUX_SESSION, LOG_DIR
EOF
}

ORIGINAL_ARGS=("$@")

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-build)        NO_BUILD=1; shift;;
    --test)            RUN_TEST=1; shift;;
    --from)            FROM_STAGE="$2"; shift 2;;
    --only)            ONLY_STAGE="$2"; shift 2;;
    --no-tmux)         NO_TMUX=1; shift;;
    --chromium-root)   CHROMIUM_ROOT="$2"; CHROMIUM_SRC="${CHROMIUM_ROOT}/src"; shift 2;;
    --build-target)    BUILD_TARGET="$2"; shift 2;;
    -h|--help)         usage; exit 0;;
    *) echo "unknown flag: $1" >&2; usage >&2; exit 2;;
  esac
done

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------

log()  { printf '\033[1;36m[bootstrap %s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*"; }
warn() { printf '\033[1;33m[bootstrap %s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*" >&2; }
die()  { printf '\033[1;31m[bootstrap %s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*" >&2; exit 1; }

SUDO=""
[[ $EUID -ne 0 ]] && SUDO="sudo"

ensure_dirs() {
  mkdir -p "${CHROMIUM_ROOT}" "${DOWNLOAD_CACHE}" "${CCACHE_DIR}" "${LOG_DIR}"
}

with_path() {
  export PATH="${DEPOT_TOOLS_DIR}:${PATH}"
}

# ----------------------------------------------------------------------------
# tmux self-wrap
# ----------------------------------------------------------------------------
#
# If not inside a tmux session, install tmux (if needed) and re-exec under one.
# The build is long; SSH disconnects must not kill it.

if [[ -z "${TMUX:-}" && "${NO_TMUX}" != "1" ]]; then
  if ! command -v tmux >/dev/null 2>&1; then
    log "tmux not installed; installing (this is the only pre-script apt action)"
    ${SUDO} apt-get update -qq
    ${SUDO} apt-get install -y --no-install-recommends tmux
  fi

  if tmux has-session -t "${TMUX_SESSION}" 2>/dev/null; then
    log "tmux session '${TMUX_SESSION}' already exists; attaching"
    exec tmux attach -t "${TMUX_SESSION}"
  fi

  log "Starting build in tmux session: ${TMUX_SESSION}"
  log "Detach with: Ctrl-b d   Reattach later: tmux attach -t ${TMUX_SESSION}"

  # Re-exec self inside tmux. GHOSTIUM_NO_TMUX prevents infinite recursion.
  # We quote each original argv element so spaces / metachars survive.
  # Guarded for empty-array under bash<4.4 (set -u + "${arr[@]}" was unsafe).
  quoted=""
  if [[ ${#ORIGINAL_ARGS[@]} -gt 0 ]]; then
    for arg in "${ORIGINAL_ARGS[@]}"; do
      quoted+=" $(printf '%q' "${arg}")"
    done
  fi
  exec tmux new-session -s "${TMUX_SESSION}" \
    "GHOSTIUM_NO_TMUX=1 bash $(printf '%q' "$0")${quoted}; \
     echo; echo '[bootstrap] build script finished. Press any key to close.'; read -n1"
fi

# ----------------------------------------------------------------------------
# Logging: tee everything to a timestamped log file
# ----------------------------------------------------------------------------

ensure_dirs
LOG_FILE="${LOG_DIR}/build-$(date +%Y%m%d-%H%M%S).log"
log "Logging to ${LOG_FILE}"

# Redirect both stdout and stderr through tee to the log file. We use
# `exec` so every subsequent command in this script (including subshells)
# inherits the redirection.
exec > >(tee -a "${LOG_FILE}") 2>&1

# ----------------------------------------------------------------------------
# Pre-flight checks
# ----------------------------------------------------------------------------

preflight() {
  log "pre-flight checks"

  # OS check
  if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    case "${ID:-}" in
      ubuntu|debian) ;;
      *) warn "host is '${ID:-unknown}', not ubuntu/debian; install-build-deps.sh may fail";;
    esac
    log "host OS: ${PRETTY_NAME:-unknown}"
  fi

  # Disk space on the chromium-root mount
  local target_dir="${CHROMIUM_ROOT}"
  [[ -d "${target_dir}" ]] || target_dir="$(dirname "${CHROMIUM_ROOT}")"
  local free_gib
  free_gib=$(df -BG --output=avail "${target_dir}" | tail -1 | tr -dc '0-9')
  log "free disk on $(df --output=target "${target_dir}" | tail -1): ${free_gib} GiB"
  if [[ ${free_gib:-0} -lt ${MIN_FREE_GIB_DISK} ]]; then
    die "need >= ${MIN_FREE_GIB_DISK} GiB free under ${target_dir}; have ${free_gib} GiB"
  fi

  # RAM
  local ram_kib ram_gib
  ram_kib=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)
  ram_gib=$((ram_kib / 1024 / 1024))
  log "RAM: ${ram_gib} GiB"
  if [[ ${ram_gib} -lt ${MIN_RAM_GIB} ]]; then
    die "need >= ${MIN_RAM_GIB} GiB RAM; have ${ram_gib} GiB (linker will OOM)"
  fi
  if [[ ${ram_gib} -lt ${WARN_RAM_GIB} ]]; then
    warn "RAM ${ram_gib} GiB is below recommended ${WARN_RAM_GIB} GiB; expect OOM during link"
  fi

  # CPU
  log "CPU count: $(nproc)"

  # Versions of key tooling we depend on
  log "kernel: $(uname -r)   bash: ${BASH_VERSION}"
}

# ----------------------------------------------------------------------------
# Stages
# ----------------------------------------------------------------------------

stage_apt() {
  log "[apt] installing host packages"
  if ! command -v apt-get >/dev/null 2>&1; then
    warn "no apt-get; skipping (not ubuntu/debian)"
    return 0
  fi
  export DEBIAN_FRONTEND=noninteractive
  ${SUDO} apt-get update -qq
  ${SUDO} apt-get install -y --no-install-recommends \
    build-essential pkg-config sudo lsb-release ca-certificates \
    python3 python3-pip python3-venv \
    git curl wget xz-utils tar unzip zip \
    ccache ninja-build \
    file tzdata locales rsync tmux
}

stage_submodule() {
  log "[submodule] syncing UC submodule"
  cd "${GHOSTIUM_ROOT}"
  if [[ ! -f .gitmodules ]]; then
    warn ".gitmodules missing; skipping"
    return 0
  fi
  git submodule sync --recursive
  git submodule update --init --recursive --depth=1
  [[ -f "${UC_ROOT}/utils/downloads.py" ]] || die "UC utils/ missing after submodule update; check ${UC_ROOT}"
}

stage_download() {
  log "[download] retrieving Chromium tarball (utils/downloads.py retrieve)"
  cd "${UC_ROOT}"
  # downloads.py uses curl -fL -C - so re-running resumes a partial download.
  python3 utils/downloads.py retrieve \
    -c "${DOWNLOAD_CACHE}" \
    -i downloads.ini
}

stage_unpack() {
  log "[unpack] unpacking Chromium tarball into ${CHROMIUM_SRC}"

  # downloads.py unpack writes into the target path; it will fail if the
  # destination already contains the unpacked tree. We make it idempotent
  # by skipping if the marker file is present.
  if [[ -f "${CHROMIUM_SRC}/.ghostium_unpacked" ]]; then
    log "already unpacked (marker present); skipping"
    return 0
  fi

  if [[ -d "${CHROMIUM_SRC}" && -n "$(ls -A "${CHROMIUM_SRC}" 2>/dev/null)" ]]; then
    die "${CHROMIUM_SRC} exists and is non-empty but lacks the unpack marker;
move it aside or rm -rf it before re-running this stage."
  fi

  mkdir -p "${CHROMIUM_SRC}"
  cd "${UC_ROOT}"
  # `-i/--ini` is nargs='+' (greedy). The `--` separator stops flag parsing
  # so ${CHROMIUM_SRC} is taken as the positional `output`, not a second
  # value for --ini. Matches UC docs/building.md.
  python3 utils/downloads.py unpack \
    -c "${DOWNLOAD_CACHE}" \
    -i downloads.ini \
    -- "${CHROMIUM_SRC}"

  touch "${CHROMIUM_SRC}/.ghostium_unpacked"
  log "unpack complete"
}

stage_depot() {
  log "[depot] cloning depot_tools to ${DEPOT_TOOLS_DIR}"
  if [[ ! -d "${DEPOT_TOOLS_DIR}/.git" ]]; then
    mkdir -p "$(dirname "${DEPOT_TOOLS_DIR}")"
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "${DEPOT_TOOLS_DIR}"
  else
    log "depot_tools already present; pulling latest"
    git -C "${DEPOT_TOOLS_DIR}" pull --ff-only || warn "depot_tools ff failed; continuing"
  fi
  with_path
  command -v gn       >/dev/null 2>&1 || die "gn not on PATH after depot_tools install"
  command -v gclient  >/dev/null 2>&1 || die "gclient not on PATH after depot_tools install"
  command -v autoninja >/dev/null 2>&1 || warn "autoninja not on PATH; falling back to ninja"
}

stage_runhooks() {
  log "[runhooks] writing .gclient and running gclient runhooks (clang, sysroot, ...)"
  with_path

  # depot_tools' gclient runhooks does not require a .git tree, but does
  # require a .gclient file at the parent of `src/`. We write a minimal
  # one declaring src/ as an unmanaged solution.
  local gclient_file="${CHROMIUM_ROOT}/.gclient"
  if [[ ! -f "${gclient_file}" ]]; then
    cat > "${gclient_file}" <<'GCLIENT_EOF'
solutions = [
  {
    "name": "src",
    "url": None,
    "managed": False,
    "custom_deps": {},
    "custom_vars": {
      "checkout_pgo_profiles": False,
    },
  },
]
target_os = ["linux"]
GCLIENT_EOF
    log "wrote ${gclient_file}"
  fi

  # gclient runhooks shells out to `git diff ... -G "Subproject commit"`
  # inside src/ to check for submodule pointer changes, even when the
  # solution is declared `managed: False`. The tarball-extracted tree has
  # no .git/, so git aborts ("Not a git repository") and gclient bails.
  # Stub it with an empty `git init`: the diff call now produces empty
  # output and exits 0 (only tracked files appear in `git diff`, and we
  # add none). The actual hooks we need (clang, sysroot, rust, node) do
  # not use git.
  if [[ ! -d "${CHROMIUM_SRC}/.git" ]]; then
    log "git init ${CHROMIUM_SRC} (stub to satisfy gclient's diff probe)"
    git -C "${CHROMIUM_SRC}" init -q
    git -C "${CHROMIUM_SRC}" config user.email "build@ghostium.local"
    git -C "${CHROMIUM_SRC}" config user.name "ghostium-build"
  fi

  # Some hooks (notably `lastchange`) shell out to git in src/. The -lite
  # tarball ships a pre-generated LASTCHANGE file; if it is missing, write
  # a synthetic one so the hook does not abort the run.
  if [[ ! -f "${CHROMIUM_SRC}/build/util/LASTCHANGE" ]]; then
    log "synthesizing LASTCHANGE (tarball is a non-git checkout)"
    local ver
    ver=$(tr -d '[:space:]' < "${GHOSTIUM_ROOT}/config/chromium_version")
    cat > "${CHROMIUM_SRC}/build/util/LASTCHANGE" <<EOF
LASTCHANGE=tarball-${ver}
LASTCHANGE_YEAR=$(date +%Y)
EOF
  fi

  cd "${CHROMIUM_ROOT}"
  gclient runhooks
}

stage_build_deps() {
  log "[build-deps] running src/build/install-build-deps.sh"
  local script="${CHROMIUM_SRC}/build/install-build-deps.sh"
  [[ -x "${script}" ]] || die "install-build-deps.sh missing or not executable: ${script}"
  ${SUDO} "${script}" --no-prompt
}

stage_prune() {
  log "[prune] UC prune_binaries.py"
  local prune="${UC_ROOT}/utils/prune_binaries.py"
  [[ -f "${prune}" ]] || die "${prune} missing; run --only submodule first"

  # UC's prune logs use cwd-relative paths but deletes via the absolute
  # arg. Run from CHROMIUM_SRC so the logs reflect the actual tree.
  cd "${CHROMIUM_SRC}"
  python3 "${prune}" "${CHROMIUM_SRC}" "${UC_ROOT}/pruning.list"
}

stage_overlay() {
  log "[overlay] symlinking ghostium_src/overlay into Chromium src"
  CHROMIUM_SRC="${CHROMIUM_SRC}" \
    python3 "${GHOSTIUM_ROOT}/scripts/sync_overlay.py" \
      --chromium-src "${CHROMIUM_SRC}"
}

stage_patches() {
  log "[patches] applying UC series + Ghostium series"
  CHROMIUM_SRC="${CHROMIUM_SRC}" \
    python3 "${GHOSTIUM_ROOT}/scripts/apply_patches.py" \
      --chromium-src "${CHROMIUM_SRC}"
}

stage_domsub() {
  log "[domsub] UC domain_substitution.py apply"
  local domsub="${UC_ROOT}/utils/domain_substitution.py"
  [[ -f "${domsub}" ]] || die "${domsub} missing; run --only submodule first"
  python3 "${domsub}" apply \
    -r "${UC_ROOT}/domain_regex.list" \
    -f "${UC_ROOT}/domain_substitution.list" \
    -c "${CHROMIUM_ROOT}/domsubcache.tar.gz" \
    "${CHROMIUM_SRC}"
}

stage_gn() {
  log "[gn] gn gen ${GN_OUT}"
  with_path

  local ghostium_args="${GHOSTIUM_ROOT}/config/gn_args.gn"
  local uc_flags="${UC_ROOT}/flags.gn"
  [[ -f "${ghostium_args}" ]] || die "missing ${ghostium_args}"
  [[ -f "${uc_flags}" ]]      || die "missing ${uc_flags}"

  # Concatenate UC's flags.gn with Ghostium's gn_args.gn. UC's flags drive
  # the privacy-relevant feature toggles (mdns off, fieldtrial testing
  # config off, safe browsing off, etc.); Ghostium's args set the build
  # shape (component build, ccache, no proprietary codecs, ...). Ours win
  # on key collisions because they are concatenated second.
  local combined
  combined="$(cat "${uc_flags}" "${ghostium_args}")"

  cd "${CHROMIUM_SRC}"
  gn gen "${GN_OUT}" --args="${combined}"

  log "[gn] effective args.gn written to ${GN_OUT}/args.gn"
}

stage_build() {
  log "[build] ninja -C ${GN_OUT} ${BUILD_TARGET}"
  with_path
  export CCACHE_DIR
  export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-20G}"
  ccache -M "${CCACHE_MAXSIZE}" >/dev/null 2>&1 || true
  ccache -s 2>/dev/null | head -10 || true

  cd "${CHROMIUM_SRC}"
  # autoninja picks the right -j based on remote/local. Fall back to
  # plain ninja with -j$(nproc) if it's missing.
  if command -v autoninja >/dev/null 2>&1; then
    autoninja -C "${GN_OUT}" "${BUILD_TARGET}"
  else
    ninja -C "${GN_OUT}" -j"$(nproc)" "${BUILD_TARGET}"
  fi

  log "[build] done"
  ccache -s 2>/dev/null | head -10 || true
}

stage_test() {
  log "[test] building + running overlay unit tests"
  with_path
  cd "${CHROMIUM_SRC}"
  if command -v autoninja >/dev/null 2>&1; then
    autoninja -C "${GN_OUT}" ghostium_overlay_tests
  else
    ninja -C "${GN_OUT}" -j"$(nproc)" ghostium_overlay_tests
  fi
  GN_OUT="${GN_OUT}" CHROMIUM_SRC="${CHROMIUM_SRC}" \
    bash "${GHOSTIUM_ROOT}/scripts/run_unit_tests.sh"
}

# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------

run_stage() {
  case "$1" in
    apt)         stage_apt;;
    submodule)   stage_submodule;;
    download)    stage_download;;
    unpack)      stage_unpack;;
    depot)       stage_depot;;
    runhooks)    stage_runhooks;;
    build-deps)  stage_build_deps;;
    prune)       stage_prune;;
    overlay)     stage_overlay;;
    patches)     stage_patches;;
    domsub)      stage_domsub;;
    gn)          stage_gn;;
    build)       stage_build;;
    test)        stage_test;;
    *) die "unknown stage: $1";;
  esac
}

# Compute the effective stage list.
STAGES=("${STAGES_DEFAULT[@]}")
[[ ${RUN_TEST} -eq 1 ]] && STAGES=("${STAGES_ALL[@]}")
[[ ${NO_BUILD} -eq 1 ]] && STAGES=("${STAGES[@]/build}") && STAGES=("${STAGES[@]/test}")

if [[ -n "${ONLY_STAGE}" ]]; then
  STAGES=("${ONLY_STAGE}")
elif [[ -n "${FROM_STAGE}" ]]; then
  # Drop everything before FROM_STAGE
  filtered=()
  seen=0
  for s in "${STAGES[@]}"; do
    [[ "$s" == "${FROM_STAGE}" ]] && seen=1
    [[ ${seen} -eq 1 ]] && filtered+=("$s")
  done
  [[ ${#filtered[@]} -gt 0 ]] || die "--from '${FROM_STAGE}' not in stage list"
  STAGES=("${filtered[@]}")
fi

# Strip empties (introduced by NO_BUILD substitutions above)
final=()
for s in "${STAGES[@]}"; do
  [[ -n "$s" ]] && final+=("$s")
done
STAGES=("${final[@]}")

log "================================================================"
log "Ghostium EC2 build"
log "  ghostium root: ${GHOSTIUM_ROOT}"
log "  chromium root: ${CHROMIUM_ROOT}"
log "  chromium src:  ${CHROMIUM_SRC}"
log "  downloads:     ${DOWNLOAD_CACHE}"
log "  depot_tools:   ${DEPOT_TOOLS_DIR}"
log "  gn out:        ${GN_OUT}"
log "  ccache:        ${CCACHE_DIR}"
log "  log file:      ${LOG_FILE}"
log "  stages:        ${STAGES[*]}"
log "================================================================"

preflight

for s in "${STAGES[@]}"; do
  log ">>>>>>>>>> stage: ${s}"
  start=$(date +%s)
  run_stage "$s"
  log "<<<<<<<<<< stage ${s} done in $(( $(date +%s) - start ))s"
done

log "all stages completed. binary: ${GN_OUT}/${BUILD_TARGET}"
