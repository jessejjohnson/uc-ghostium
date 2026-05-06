#!/usr/bin/env bash
# Run every Ghostium overlay unit-test binary. Assumes the
# ``ghostium_overlay_tests`` meta-target has already been built.
set -euo pipefail

GN_OUT="${GN_OUT:-${CHROMIUM_SRC}/out/Default}"

tests=(
  fingerprint_profile_registry_unittest
  fingerprint_profile_delivery_unittest
  emulation_bridge_unittest
  ghostium_cdp_unittest
  fingerprint_noise_source_unittest
  ghostium_noise_unittest
)

fail=0
for t in "${tests[@]}"; do
  bin="${GN_OUT}/${t}"
  if [ ! -x "${bin}" ]; then
    echo "[run_unit_tests] missing test binary: ${bin}" >&2
    fail=1
    continue
  fi
  echo "[run_unit_tests] running ${t}"
  if ! "${bin}"; then
    fail=1
  fi
done

exit "${fail}"
