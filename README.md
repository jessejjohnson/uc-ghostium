# Ghostium

Ghostium is a privacy-focused Chromium fork that replaces the runtime behavior
of every fingerprintable web API with values supplied by a CDP client per
`BrowserContext`. Session isolation, cookie partitioning, cache segregation,
and extension orchestration are delegated entirely to standard CDP surfaces.
Ghostium owns exactly two things:

1. An extended `Target.createBrowserContext` command that accepts a
   Ghostium-specific fingerprint profile structure, plus a companion
   `Target.setBrowserContextFingerprint` command for runtime mutation.
2. A deep Blink/V8 hook layer that applies that profile at every surface
   exposed to untrusted web content.

- **Target upstream:** ungoogled-chromium, stable Chromium milestone pinned
  via `config/chromium_version`.
- **Target platform:** Linux x86_64 only.
- **Fork strategy:** ungoogled-chromium fork + Brave-style overlay + thin hook
  patches applied after UC's patch series.

See [plan.md](plan.md) for the unified architecture specification and
downstream implementation spec split (Spec-A through Spec-G).

## Current status

This repository currently implements **Spec-A** (devcontainer, UC integration,
skeleton overlay, GN patch, patch tooling, CI workflows) and **Spec-B** (core
`FingerprintProfile` value type, `FingerprintProfileRegistry`,
`FingerprintProfileDelivery`, Mojo IDL, renderer-side `FingerprintNoiseSource`
skeleton with passthrough semantics).

Specs C through G (CDP handler, per-surface Blink hooks, noise
implementations) are not yet implemented. The `FingerprintNoiseSource` ships
in passthrough mode: non-Ghostium contexts are bitwise identical to upstream
ungoogled-chromium.

## Quickstart

### Prerequisites

- Docker Desktop or a compatible container runtime
- VS Code with the Dev Containers extension, or the `devcontainer` CLI
- ~100 GB free disk space for the Chromium checkout + build outputs
- Host network connectivity for `depot_tools` / `gclient sync`

### First-time setup

```bash
git clone <this-repo> ghostium
cd ghostium
git submodule update --init --recursive
devcontainer up --workspace-folder .
```

The devcontainer's `post-create.sh` will:

1. Clone `depot_tools`.
2. `fetch` the Chromium tree at the tag pinned in
   [config/chromium_version](config/chromium_version).
3. Run `build/install-build-deps.sh` and `gclient runhooks`.
4. Apply ungoogled-chromium's pruning + domain substitution.
5. Symlink Ghostium's overlay tree into the Chromium source tree.
6. Apply UC's patch series, followed by Ghostium's patch series.

Depending on your bandwidth and hardware this takes 40 minutes to a few
hours.

### Build

Inside the devcontainer:

```bash
cd "$CHROMIUM_SRC"
gn gen out/Default --args="$(cat /workspaces/ghostium/config/gn_args.gn)"
autoninja -C out/Default chrome
```

### Run unit tests

```bash
autoninja -C out/Default ghostium_overlay_tests
bash /workspaces/ghostium/build/run_unit_tests.sh
```

## Repository layout

```text
/
├── .devcontainer/                  devcontainer + Dockerfile + post-create
├── .github/workflows/              pr-lint + nightly-build
├── build/                          patch + overlay tooling
├── config/                         pinned versions + GN args
├── ghostium_src/overlay/           overlay tree symlinked into Chromium src
│   ├── chrome/browser/ghostium/    browser-process registry + delivery
│   ├── public/mojom/ghostium/      mojom IDL
│   └── third_party/blink/renderer/modules/ghostium_fp/
│                                   renderer-side FingerprintNoiseSource
├── patches/ghostium/               Ghostium patch series (applied after UC's)
├── third_party/ungoogled-chromium/ UC submodule
├── plan.md                         unified architecture spec
└── README.md                       this file
```

## Licensing

Ghostium inherits the Chromium + ungoogled-chromium license terms. Overlay
code under `ghostium_src/overlay/` is under the BSD-3-Clause license unless
otherwise noted in individual files.
