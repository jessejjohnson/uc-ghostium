#!/usr/bin/env bash
# Ghostium devcontainer post-create bootstrap.
#
# Idempotent: safe to re-run. Exits non-zero on any step failure so that the
# devcontainer build surfaces problems up to the user.
set -euo pipefail

GHOSTIUM_ROOT="/workspaces/ghostium"
UC_ROOT="${GHOSTIUM_ROOT}/third_party/ungoogled-chromium"

log() { printf '[post-create] %s\n' "$*"; }

# 1. Ensure the UC submodule is populated.
if [ ! -d "${UC_ROOT}/utils" ]; then
  log "Initializing ungoogled-chromium submodule"
  git -C "${GHOSTIUM_ROOT}" submodule update --init --recursive \
    third_party/ungoogled-chromium
fi

# 2. Clone depot_tools if missing.
if [ ! -d "${DEPOT_TOOLS_PATH}/.git" ]; then
  log "Cloning depot_tools"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \
      "${DEPOT_TOOLS_PATH}"
fi

# 3. Fetch Chromium at the pinned tag.
if [ ! -d "${CHROMIUM_SRC}/.git" ]; then
  log "Fetching Chromium source tree (this will take a while)"
  mkdir -p "$(dirname "${CHROMIUM_SRC}")"
  cd "$(dirname "${CHROMIUM_SRC}")"
  fetch --nohooks --no-history chromium
  cd src
  CHROMIUM_VERSION="$(cat "${GHOSTIUM_ROOT}/config/chromium_version")"
  git checkout "tags/${CHROMIUM_VERSION}" -b "work/${CHROMIUM_VERSION}"
  gclient sync --with_branch_heads --with_tags -D
  build/install-build-deps.sh --no-prompt
  gclient runhooks
fi

# 4. UC prune + domain substitution (from the submodule).
log "Applying UC prune + domain substitution"
python3 "${UC_ROOT}/utils/prune_binaries.py" \
  "${CHROMIUM_SRC}" \
  "${UC_ROOT}/pruning.list"
python3 "${UC_ROOT}/utils/domain_substitution.py" apply \
  -r "${UC_ROOT}/domain_regex.list" \
  -f "${UC_ROOT}/domain_substitution.list" \
  -c "${UC_ROOT}/domsubcache.tar.gz" \
  "${CHROMIUM_SRC}"

# 5. Ghostium overlay + patches.
log "Syncing Ghostium overlay into Chromium source tree"
python3 "${GHOSTIUM_ROOT}/build/sync_overlay.py"

log "Applying UC patch series + Ghostium patch series"
python3 "${GHOSTIUM_ROOT}/build/apply_patches.py"

log "Bootstrap complete. Next step: gn gen + autoninja."
