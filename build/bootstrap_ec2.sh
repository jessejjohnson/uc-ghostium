#!/usr/bin/env bash
#
# Ghostium EC2 bootstrap. One script that takes a fresh Ubuntu instance from
# zero to a built/tested Ghostium tree.
#
# Quick start (Ubuntu 22.04+ EC2 instance, with the repo cloned to
# ~/uc-ghostium):
#
#     cd ~/uc-ghostium
#     ./build/bootstrap_ec2.sh
#
# Recommended EC2 sizing
#   * Build:  c6a.16xlarge / c7i.24xlarge   (CPU-bound; 32-96 vCPU)
#   * Link:   ensure >= 32 GiB RAM (r6a.4xlarge if budget-constrained)
#   * Disk:   gp3 EBS, >= 200 GiB           (Chromium checkout ~80 GiB,
#                                            build artefacts ~30 GiB,
#                                            ccache ~20 GiB)
#   * AMI:    Ubuntu 22.04 LTS x86_64       (matches the devcontainer base
#                                            image; the build deps install
#                                            script targets this distro)
#
# Stages (run in order; can be re-run independently or selected via --steps):
#
#   apt           : install host packages (git, python, build-essential, ...)
#   depot         : clone depot_tools and put it on PATH for this run
#   fetch         : ``fetch chromium`` at the version pinned by
#                   config/chromium_version
#   chromium-deps : run Chromium's install-build-deps.sh (apt-installs the
#                   Chromium build dependencies)
#   runhooks      : ``gclient runhooks`` (downloads sysroots etc.)
#   uc-submodule  : git submodule update for third_party/ungoogled-chromium
#   prune         : UC prune_binaries.py + domain_substitution.py
#   overlay       : symlink ghostium_src/overlay into the Chromium tree
#   patches       : apply UC's patch series + Ghostium's series
#   gen           : ``gn gen`` with config/gn_args.gn
#   build         : ``autoninja chrome`` (full Chromium build, hours)
#   test          : build + run the overlay unit_tests
#
# The default ``--steps all`` runs through ``patches`` and ``gen`` but stops
# short of the (expensive) full build. Add ``--steps all,build`` or
# ``--steps build,test`` to invoke those explicitly.
#
# Idempotency: each stage detects work already done and skips. You can
# safely re-run the whole script after a transient failure.

set -euo pipefail

# ----------------------------------------------------------------------------
# Defaults / argument parsing
# ----------------------------------------------------------------------------

GHOSTIUM_ROOT="${GHOSTIUM_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-${HOME}/chromium-build}"
# Allow ``--chromium-root`` to drive the dependent paths even when the
# environment hasn't pre-set them: leave the dependent vars empty here and
# fill them in after argument parsing.
DEPOT_TOOLS_PATH="${DEPOT_TOOLS_PATH:-}"
GN_OUT="${GN_OUT:-}"
CCACHE_DIR="${CCACHE_DIR:-}"
BUILD_TARGET="${BUILD_TARGET:-chrome}"
STEPS_DEFAULT="apt,depot,fetch,chromium-deps,runhooks,uc-submodule,prune,overlay,patches,gen"
STEPS_ALL="apt,depot,fetch,chromium-deps,runhooks,uc-submodule,prune,overlay,patches,gen,build,test"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --ghostium-root PATH   Path to the cloned Ghostium repo
                         (default: \$GHOSTIUM_ROOT or \$(dirname \$0)/..)
  --chromium-root PATH   Where to put the Chromium checkout + depot_tools
                         (default: \$CHROMIUM_ROOT or \$HOME/chromium-build)
  --gn-out PATH          GN output dir (default: \$CHROMIUM_SRC/out/Default)
  --build-target NAME    Build target for the 'build' step (default: chrome)
  --steps LIST           Comma-separated stages to run. Special values:
                           'default' => ${STEPS_DEFAULT}
                           'all'     => ${STEPS_ALL}
                         (default: 'default')
  -h, --help             Show this help

Environment variables (override anytime):
  GHOSTIUM_ROOT, CHROMIUM_ROOT, CHROMIUM_SRC, DEPOT_TOOLS_PATH,
  GN_OUT, BUILD_TARGET, CCACHE_DIR
EOF
}

STEPS_RAW="default"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ghostium-root)  GHOSTIUM_ROOT="$2"; shift 2;;
    --chromium-root)  CHROMIUM_ROOT="$2"; shift 2;;
    --gn-out)         GN_OUT="$2"; shift 2;;
    --build-target)   BUILD_TARGET="$2"; shift 2;;
    --steps)          STEPS_RAW="$2"; shift 2;;
    -h|--help)        usage; exit 0;;
    *) echo "unknown flag: $1" >&2; usage >&2; exit 2;;
  esac
done

# Derive defaults from the (possibly --chromium-root-overridden) root.
CHROMIUM_SRC="${CHROMIUM_ROOT}/src"
: "${DEPOT_TOOLS_PATH:=${CHROMIUM_ROOT}/depot_tools}"
: "${GN_OUT:=${CHROMIUM_SRC}/out/Default}"
: "${CCACHE_DIR:=${CHROMIUM_ROOT}/.ccache}"

case "${STEPS_RAW}" in
  default) STEPS="${STEPS_DEFAULT}";;
  all)     STEPS="${STEPS_ALL}";;
  *)       STEPS="${STEPS_RAW}";;
esac

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------

log() { printf '\033[1;36m[bootstrap]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[bootstrap]\033[0m %s\n' "$*" >&2; }
die() { printf '\033[1;31m[bootstrap]\033[0m %s\n' "$*" >&2; exit 1; }

want_step() {
  # Return 0 if "$1" is in the comma-separated $STEPS list.
  case ",${STEPS}," in *",$1,"*) return 0;; *) return 1;; esac
}

require_cmd() { command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"; }

if [[ $EUID -eq 0 ]]; then
  SUDO=""
else
  SUDO="sudo"
fi

ensure_dirs() {
  mkdir -p "${CHROMIUM_ROOT}" "${CCACHE_DIR}"
}

read_chromium_version() {
  local f="${GHOSTIUM_ROOT}/config/chromium_version"
  [[ -f "$f" ]] || die "missing ${f}; cannot pin Chromium revision"
  tr -d '[:space:]' < "$f"
}

# ----------------------------------------------------------------------------
# Stages
# ----------------------------------------------------------------------------

stage_apt() {
  log "stage: apt - installing host packages"
  if ! command -v apt-get >/dev/null 2>&1; then
    warn "apt-get not found; this script targets Ubuntu/Debian. Skipping."
    return 0
  fi
  export DEBIAN_FRONTEND=noninteractive
  ${SUDO} apt-get update
  ${SUDO} apt-get install -y --no-install-recommends \
    build-essential pkg-config lsb-release sudo \
    python3 python3-pip python3-venv \
    git curl ca-certificates gnupg \
    lld clang clang-tidy clang-format clangd \
    ninja-build cmake ccache \
    file tzdata locales \
    xz-utils unzip \
    rsync
}

stage_depot() {
  log "stage: depot - cloning depot_tools to ${DEPOT_TOOLS_PATH}"
  if [[ ! -d "${DEPOT_TOOLS_PATH}/.git" ]]; then
    mkdir -p "$(dirname "${DEPOT_TOOLS_PATH}")"
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \
      "${DEPOT_TOOLS_PATH}"
  else
    log "depot_tools already present; pulling latest"
    git -C "${DEPOT_TOOLS_PATH}" pull --ff-only || \
      warn "depot_tools fast-forward failed; leaving as-is"
  fi
  export PATH="${DEPOT_TOOLS_PATH}:${PATH}"
  require_cmd fetch
  require_cmd gclient
  require_cmd gn
  require_cmd autoninja
}

stage_fetch() {
  log "stage: fetch - fetching Chromium into ${CHROMIUM_SRC}"
  export PATH="${DEPOT_TOOLS_PATH}:${PATH}"

  local version
  version="$(read_chromium_version)"
  log "pinned Chromium version: ${version}"

  if [[ ! -d "${CHROMIUM_SRC}/.git" ]]; then
    mkdir -p "${CHROMIUM_ROOT}"
    cd "${CHROMIUM_ROOT}"
    fetch --nohooks --no-history chromium
    cd "${CHROMIUM_SRC}"
    git fetch --tags --depth=1 origin "tags/${version}" || \
      git fetch --tags origin "tags/${version}"
    git checkout "tags/${version}" -B "work/${version}"
    gclient sync --with_branch_heads --with_tags -D
  else
    log "Chromium checkout already exists; checking pinned tag"
    cd "${CHROMIUM_SRC}"
    local current
    current="$(git describe --tags --exact-match 2>/dev/null || echo '')"
    if [[ "${current}" != "${version}" ]]; then
      warn "current checkout is at '${current}', expected '${version}'"
      warn "fetching + switching"
      git fetch --tags origin "tags/${version}"
      git checkout "tags/${version}" -B "work/${version}"
      gclient sync --with_branch_heads --with_tags -D
    else
      log "checkout is at the pinned tag"
    fi
  fi
}

stage_chromium_deps() {
  log "stage: chromium-deps - install-build-deps.sh"
  local script="${CHROMIUM_SRC}/build/install-build-deps.sh"
  [[ -x "${script}" ]] || die "Chromium install-build-deps.sh not found at ${script}; did 'fetch' fail?"
  ${SUDO} "${script}" --no-prompt
}

stage_runhooks() {
  log "stage: runhooks - gclient runhooks"
  export PATH="${DEPOT_TOOLS_PATH}:${PATH}"
  cd "${CHROMIUM_SRC}"
  gclient runhooks
}

stage_uc_submodule() {
  log "stage: uc-submodule - syncing third_party/ungoogled-chromium"
  cd "${GHOSTIUM_ROOT}"
  if [[ ! -f .gitmodules ]]; then
    warn ".gitmodules not present; skipping submodule sync"
    return 0
  fi
  git submodule sync --recursive
  git submodule update --init --recursive --depth=1
}

stage_prune() {
  log "stage: prune - UC prune_binaries.py + domain_substitution.py"
  local uc_root="${GHOSTIUM_ROOT}/third_party/ungoogled-chromium"
  local prune="${uc_root}/utils/prune_binaries.py"
  local domsub="${uc_root}/utils/domain_substitution.py"

  if [[ ! -x "${prune}" && ! -f "${prune}" ]]; then
    warn "UC utils/ not present at ${uc_root}/utils/. Run the 'uc-submodule' stage first."
    warn "Skipping prune stage; this is acceptable for a build-only smoke pass"
    warn "but a release build must apply UC's binary prune + domain substitution."
    return 0
  fi

  python3 "${prune}" "${CHROMIUM_SRC}" "${uc_root}/pruning.list"
  python3 "${domsub}" apply \
    -r "${uc_root}/domain_regex.list" \
    -f "${uc_root}/domain_substitution.list" \
    -c "${CHROMIUM_ROOT}/domsubcache.tar.gz" \
    "${CHROMIUM_SRC}"
}

stage_overlay() {
  log "stage: overlay - symlinking ghostium_src/overlay into Chromium tree"
  CHROMIUM_SRC="${CHROMIUM_SRC}" \
    python3 "${GHOSTIUM_ROOT}/build/sync_overlay.py" \
      --chromium-src "${CHROMIUM_SRC}"
}

stage_patches() {
  log "stage: patches - apply UC + Ghostium series"
  CHROMIUM_SRC="${CHROMIUM_SRC}" \
    python3 "${GHOSTIUM_ROOT}/build/apply_patches.py" \
      --chromium-src "${CHROMIUM_SRC}"
}

stage_gen() {
  log "stage: gen - gn gen ${GN_OUT}"
  export PATH="${DEPOT_TOOLS_PATH}:${PATH}"
  local args_file="${GHOSTIUM_ROOT}/config/gn_args.gn"
  [[ -f "${args_file}" ]] || die "missing ${args_file}"
  cd "${CHROMIUM_SRC}"
  gn gen "${GN_OUT}" --args="$(cat "${args_file}")"
}

stage_build() {
  log "stage: build - autoninja -C ${GN_OUT} ${BUILD_TARGET}"
  export PATH="${DEPOT_TOOLS_PATH}:${PATH}"
  export CCACHE_DIR
  cd "${CHROMIUM_SRC}"
  autoninja -C "${GN_OUT}" "${BUILD_TARGET}"
}

stage_test() {
  log "stage: test - build + run overlay unit tests"
  export PATH="${DEPOT_TOOLS_PATH}:${PATH}"
  cd "${CHROMIUM_SRC}"
  autoninja -C "${GN_OUT}" ghostium_overlay_tests
  GN_OUT="${GN_OUT}" CHROMIUM_SRC="${CHROMIUM_SRC}" \
    bash "${GHOSTIUM_ROOT}/build/run_unit_tests.sh"
}

# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------

ensure_dirs

log "ghostium root:   ${GHOSTIUM_ROOT}"
log "chromium root:   ${CHROMIUM_ROOT}"
log "chromium src:    ${CHROMIUM_SRC}"
log "depot_tools:     ${DEPOT_TOOLS_PATH}"
log "gn out:          ${GN_OUT}"
log "ccache dir:      ${CCACHE_DIR}"
log "steps:           ${STEPS}"

# Stages must run in this fixed order so the data flow is correct.
ORDER=(apt depot fetch chromium-deps runhooks uc-submodule prune overlay patches gen build test)
for step in "${ORDER[@]}"; do
  if want_step "${step}"; then
    case "${step}" in
      apt)            stage_apt;;
      depot)          stage_depot;;
      fetch)          stage_fetch;;
      chromium-deps)  stage_chromium_deps;;
      runhooks)       stage_runhooks;;
      uc-submodule)   stage_uc_submodule;;
      prune)          stage_prune;;
      overlay)        stage_overlay;;
      patches)        stage_patches;;
      gen)            stage_gen;;
      build)          stage_build;;
      test)           stage_test;;
    esac
  else
    log "skipping stage: ${step}"
  fi
done

log "done."
