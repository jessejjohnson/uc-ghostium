#!/usr/bin/env bash
# Thin wrapper: sync_overlay.py -> apply_patches.py -> gn gen -> autoninja.
#
# Intended to be invoked inside the devcontainer once the initial Chromium
# checkout exists. Environment:
#   CHROMIUM_SRC  - required, path to the Chromium checkout
#   GN_OUT        - optional, defaults to $CHROMIUM_SRC/out/Default
set -euo pipefail

GHOSTIUM_ROOT="/workspaces/ghostium"
GN_OUT="${GN_OUT:-${CHROMIUM_SRC}/out/Default}"

python3 "${GHOSTIUM_ROOT}/build/sync_overlay.py"
python3 "${GHOSTIUM_ROOT}/build/apply_patches.py"

cd "${CHROMIUM_SRC}"
gn gen "${GN_OUT}" --args="$(cat "${GHOSTIUM_ROOT}/config/gn_args.gn")"
autoninja -C "${GN_OUT}" chrome
