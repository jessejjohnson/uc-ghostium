# Ghostium

Ghostium is a privacy focused [ungoogled-chromium](https://github.com/ungoogled-software/ungoogled-chromium) fork that replaces the runtime behavior of every fingerprintable web API with values supplied by a CDP client per `BrowserContext`. Session isolation, cookie partitioning, cache segregation, and extension orchestration are delegated entirely to standard CDP surfaces.

- **Target upstream:** ungoogled-chromium, stable Chromium milestone pinned via `config/chromium_version`.
- **Target platform:** Linux x86_64 only.
- **Fork strategy:** ungoogled-chromium fork + Brave-style overlay + thin hook patches applied after UC's patch series.

Ghostium owns exactly two things:

1. Three CDP commands on the `Target` domain: an extended `Target.createBrowserContext` that accepts a `ghostiumFingerprint` parameter, plus `Target.setBrowserContextFingerprint` and `Target.clearBrowserContextFingerprint` for runtime mutation.
2. A deep Blink/V8 hook layer that applies that profile at every surface exposed to untrusted web content.

See [plan.md](plan.md) for the unified architecture specification and downstream implementation spec split (Spec-A through Spec-G).

## Current status

All seven downstream specs are implemented (see [AGENTS.md](AGENTS.md) for the status table):

| Spec | Scope | Status |
| --- | --- | --- |
| A | Build substrate (devcontainer, UC integration, GN patch, CI) | :white_check_mark: |
| B | `FingerprintProfile` + Registry + Delivery + Mojo IDL + NoiseSource | :white_check_mark: |
| C | CDP handler (`createBrowserContext` + set/clear) + Emulation bridge | :white_check_mark: |
| D | Canvas 2D + WebGL `readPixels` + WebGL `UNMASKED_*` + AudioBuffer + AnalyserNode noise | :white_check_mark: |
| E | Navigator family (webdriver, UA, UA-CH, hwc, deviceMemory, languages, plugins) | :white_check_mark: |
| F | Screen + `devicePixelRatio` + timezone | :white_check_mark: |
| G | Fonts whitelist + MediaDevices + WebRTC ICE filtering | :white_check_mark: |

20 hook patches are active in [`patches/ghostium/series`](patches/ghostium/series). Non-Ghostium `BrowserContext`s remain bitwise identical to upstream ungoogled-chromium (R16): every override predicate returns false and every `Apply*Noise` call is a no-op when the renderer-side `FingerprintNoiseSource` has not received a profile.

## Surfaces covered

The fingerprint profile (defined in [`fingerprint_profile.h`](ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h)) drives the following surfaces:

- **Canvas 2D** — `toDataURL` / `toBlob` / `getImageData` ±1 LSB noise on R/G/B (alpha untouched), keyed by `canvasSeed`.
- **WebGL** — `readPixels` ±1 LSB on RGBA8 + 1-ULP toggle on `GL_FLOAT`, keyed by `webglSeed`. `UNMASKED_VENDOR_WEBGL` / `UNMASKED_RENDERER_WEBGL` return profile values.
- **Web Audio** — `AudioBuffer.getChannelData` / `copyFromChannel` ±1e-7 PCM perturbation; `AnalyserNode.getFloat/Byte*FrequencyData` and `getFloat/ByteTimeDomainData` deterministic per `audioSeed`.
- **Navigator** — `userAgent`, `userAgentData` (low-entropy hints + `getHighEntropyValues`), `platform`, `webdriver`, `hardwareConcurrency`, `deviceMemory`, `language`, `languages`, `plugins`, `mimeTypes`.
- **Screen / Window** — `screen.{width,height,availWidth,availHeight,colorDepth,pixelDepth}`, `window.devicePixelRatio`.
- **Timezone** — `Intl.DateTimeFormat().resolvedOptions().timeZone`, `Date.prototype.getTimezoneOffset` / `toLocaleString`.
- **Fonts** — `document.fonts` enumeration + CSS `@font-face` matching filtered by whitelist (generic families always available).
- **MediaDevices** — `enumerateDevices()` returns the profile's spec list (empty list is meaningful: no devices).
- **WebRTC** — `RTCPeerConnection` ICE policy clamp + mdns-only candidate filter.
- **UA / UA-CH headers** — `User-Agent`, `Accept-Language`, `Sec-CH-UA-*` routed through `WebContents::SetUserAgentOverride` by the CDP handler's Emulation bridge (Spec-C).

All Mulberry32-keyed noise is deterministic per profile + input.

## Quickstart

Two supported paths.

### A) Devcontainer (local development)

#### Prerequisites

- Docker Desktop or a compatible container runtime
- VS Code with the Dev Containers extension, or the `devcontainer` CLI
- ~100 GB free disk space for the Chromium checkout + build outputs

#### First-time setup

```bash
git clone <this-repo> ghostium
cd ghostium
git submodule update --init --recursive
devcontainer up --workspace-folder .
```

The devcontainer's `post-create.sh` clones `depot_tools`, fetches Chromium at the pinned tag, runs `install-build-deps.sh` + `gclient runhooks`, applies UC's prune + domain substitution, symlinks the overlay, and applies the combined patch series. Depending on bandwidth and hardware this takes 40 minutes to a few hours.

#### Build + test

```bash
# Inside the devcontainer:
cd "$CHROMIUM_SRC"
gn gen out/Default --args="$(cat /workspaces/ghostium/config/gn_args.gn)"
autoninja -C out/Default chrome

# Overlay unit tests:
autoninja -C out/Default ghostium_overlay_tests
bash /workspaces/ghostium/build/run_unit_tests.sh
```

### B) EC2 bootstrap (one-shot)

For full Chromium builds on cloud hardware. SSH into a fresh Ubuntu 22.04 LTS EC2 instance and run:

```bash
git clone <this-repo> uc-ghostium
cd uc-ghostium
./build/bootstrap_ec2.sh                  # default: deps + checkout + patches + gn gen
./build/bootstrap_ec2.sh --steps all      # adds full chrome build + unit tests
./build/bootstrap_ec2.sh --steps build    # rebuild only (after edits)
```

The script is idempotent and stage-based; see [`build/bootstrap_ec2.sh`](build/bootstrap_ec2.sh) for the full stage list.

**Recommended sizing:** `c6a.16xlarge` / `c7i.24xlarge` (CPU-bound link is the bottleneck), ≥32 GiB RAM (link can spike higher), ≥200 GiB gp3 EBS (checkout ~80 GiB + build artefacts ~30 GiB + ccache ~20 GiB), Ubuntu 22.04 LTS x86_64 AMI.

## Overlay test targets

The `ghostium_overlay_tests` GN group aggregates:

| Target | Coverage |
| --- | --- |
| `fingerprint_profile_registry_unittest` | Set / Update / Clear / Get, observer fan-out, multi-context isolation |
| `fingerprint_profile_delivery_unittest` | RenderFrameCreated push, no-push for non-Ghostium contexts |
| `emulation_bridge_unittest` | `WebContents::SetUserAgentOverride` propagation incl. UA-CH metadata |
| `ghostium_cdp_unittest` | Profile parser (every field type + seed encodings), Target.create/set/clear handlers |
| `fingerprint_noise_source_unittest` | All override predicates + Apply\*Noise wiring + Spec-D/E/F/G roundtrips |
| `ghostium_noise_unittest` | Pure-function noise primitives (canvas / webgl / audio) |

Run the whole suite via `build/run_unit_tests.sh`.

## CDP usage example

```jsonc
// Create a fingerprinted BrowserContext
{
  "method": "Target.createBrowserContext",
  "params": {
    "disposeOnDetach": true,
    "ghostiumFingerprint": {
      "userAgent": "Mozilla/5.0 (X11; Linux x86_64) ...",
      "userAgentMetadata": {
        "brands": [{ "brand": "Chromium", "version": "135" }],
        "platform": "Linux", "mobile": false
      },
      "platform": "Linux x86_64",
      "hardwareConcurrency": 8,
      "deviceMemory": 8,
      "languages": ["en-US", "en"],
      "screen": {
        "width": 1920, "height": 1080,
        "availWidth": 1920, "availHeight": 1040,
        "colorDepth": 24, "pixelDepth": 24
      },
      "devicePixelRatio": 1.0,
      "timezone": "America/Los_Angeles",
      "canvasSeed": 42,
      "webglSeed": { "hex": "0xdeadbeefcafef00d" },
      "audioSeed": 99,
      "webglVendor": "Intel Inc.",
      "webglRenderer": "Intel Iris Xe Graphics",
      "fontsWhitelist": ["Roboto", "Inter"],
      "plugins": [],
      "webrtcMdnsOnly": true
    }
  }
}
```

Subsequent `Target.createTarget` calls in that context inherit the profile; `Target.setBrowserContextFingerprint` mutates it at runtime; `Target.clearBrowserContextFingerprint` restores upstream behavior.

## Repository layout

```text
/
├── .devcontainer/                  devcontainer + Dockerfile + post-create
├── .github/workflows/              pr-lint + nightly-build
├── build/                          patch + overlay + EC2 bootstrap tooling
│   ├── bootstrap_ec2.sh            one-shot EC2 setup (apt → fetch → patches → build → test)
│   ├── apply_patches.py            UC series + Ghostium series
│   ├── sync_overlay.py             symlink overlay into Chromium src
│   ├── ci_patch_dryrun.py          structural patch validator
│   ├── ci_pdl_lint.py              PDL hunk linter
│   ├── ci_mojom_lint.py            mojom linter
│   └── run_unit_tests.sh           run all overlay unit_tests
├── config/                         pinned versions + GN args
├── ghostium_src/overlay/           overlay tree symlinked into Chromium src
│   ├── chrome/browser/ghostium/    browser-process Registry + Delivery + EmulationBridge
│   │   └── cdp/                    CDP handler (parser + Target.* dispatch)
│   ├── public/mojom/ghostium/      Mojo IDL (FingerprintProfile)
│   └── third_party/blink/renderer/modules/ghostium_fp/
│       │                           renderer-side FingerprintNoiseSource
│       ├── canvas2d_noise.{h,cc}   Mulberry32-keyed RGB ±1 LSB
│       ├── webgl_noise.{h,cc}      RGBA8 ±1 LSB + GL_FLOAT 1-ULP toggle
│       └── audio_noise.{h,cc}      ±1e-7 PCM + ±1 LSB byte-domain
├── patches/ghostium/               20 active hook patches (Specs A-G) + series
├── third_party/ungoogled-chromium/ UC submodule (provides patches/ + utils/)
├── plan.md                         unified architecture spec
├── AGENTS.md                       agent guidance + spec status table
└── README.md                       this file
```

## Licensing

Ghostium inherits the Chromium + ungoogled-chromium license terms. Overlay code under `ghostium_src/overlay/` is under the BSD-3-Clause license unless otherwise noted in individual files.
