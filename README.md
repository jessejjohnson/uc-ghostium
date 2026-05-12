# Ghostium

Ghostium is a privacy-focused [ungoogled-chromium](https://github.com/ungoogled-software/ungoogled-chromium) fork that replaces the runtime behavior of every fingerprintable web API with values supplied by a CDP client, per `BrowserContext`. Session isolation, cookie partitioning, cache segregation, and extension orchestration are delegated entirely to standard CDP surfaces.

- **Target upstream:** ungoogled-chromium, stable Chromium milestone pinned via `config/chromium_version`.
- **Target platform:** Linux x86_64 only.
- **Fork strategy:** ungoogled-chromium fork + Brave-style overlay + thin hook patches applied after UC's patch series.

Ghostium owns exactly two things:

1. Three CDP commands on the `Target` domain: an extended `Target.createBrowserContext` that accepts a `ghostiumFingerprint` parameter, plus `Target.setBrowserContextFingerprint` and `Target.clearBrowserContextFingerprint` for runtime mutation.
2. A deep Blink/V8 hook layer that applies that profile at every surface exposed to untrusted web content.

See [plan.md](plan.md) for the unified architecture specification and downstream implementation spec split (Spec-A through Spec-G).

## Status

All seven downstream specs are implemented (see [AGENTS.md](AGENTS.md) for the status table):

| Spec | Scope | Status |
| --- | --- | --- |
| A | Build substrate (UC integration, GN patch) | implemented |
| B | `FingerprintProfile` + Registry + Delivery + Mojo IDL + NoiseSource | implemented |
| C | CDP handler (`createBrowserContext` + set/clear) + Emulation bridge | implemented |
| D | Canvas 2D + WebGL `readPixels` + WebGL `UNMASKED_*` + AudioBuffer + AnalyserNode noise | implemented |
| E | Navigator family (webdriver, UA, UA-CH, hwc, deviceMemory, languages, plugins) | implemented |
| F | Screen + `devicePixelRatio` + timezone | implemented |
| G | Fonts whitelist + MediaDevices + WebRTC ICE filtering | implemented |

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

## Build

One command, on a fresh Ubuntu 22.04 (or 24.04) x86_64 host:

```bash
git clone <this-repo> ghostium
cd ghostium
./build.sh
```

The script:

- Auto-wraps itself in a tmux session named `ghostium-build` so SSH disconnects do not kill the build. Reattach with `tmux attach -t ghostium-build`.
- Tees all output to `logs/build-YYYYMMDD-HHMMSS.log`.
- Pre-flights disk (≥150 GiB free) and RAM (≥16 GiB), with a warning below 32 GiB.
- Every stage is idempotent — re-run after any transient failure to resume.

### Common invocations

```bash
./build.sh                  # full pipeline: deps → tarball → patches → gn → chrome
./build.sh --no-build       # everything except the final ninja
./build.sh --test           # adds overlay unit tests after build
./build.sh --from build     # resume from a stage (skip earlier stages)
./build.sh --only patches   # run a single stage
./build.sh --help           # full flag + stage list
```

### How it works

The script follows the canonical [ungoogled-chromium build flow](third_party/ungoogled-chromium/docs/building.md):

1. **Tarball fetch.** Chromium source is downloaded via `utils/downloads.py retrieve` — a hash-verified, resumable curl against `chromium-browser-official`. No `depot_tools fetch` / `gclient sync` of the source tree (those are the slow, flaky 80 GiB git operations).
2. **Toolchain.** `depot_tools` is cloned only to provide the `gn` binary and `gclient runhooks`, which downloads Chromium's bundled clang, sysroot, rust, and node.
3. **UC pipeline.** Strict order from UC docs: `prune_binaries.py` → patch series (UC + Ghostium, applied in that order by [`scripts/apply_patches.py`](scripts/apply_patches.py)) → `domain_substitution.py apply`. Domain substitution before patches corrupts the patch context and is rejected.
4. **GN args.** `gn gen` is invoked with UC's `flags.gn` concatenated with [`config/gn_args.gn`](config/gn_args.gn). Ghostium's args take precedence on key collisions.
5. **Build.** `ninja -C out/Default chrome`.

### Recommended sizing

- **Instance:** `c6a.16xlarge` / `c7i.24xlarge` (CPU-bound, 32–96 vCPU).
- **RAM:** ≥32 GiB recommended; the script refuses to start below 16 GiB.
- **Disk:** ≥200 GiB gp3 mounted on `$CHROMIUM_ROOT` (default: `~/chromium-build`). Tarball ~6 GiB + unpacked source ~30 GiB + build artefacts ~30 GiB + ccache ~20 GiB.
- **AMI:** Ubuntu 22.04 LTS x86_64 (24.04 also works).

### Output

The built binary lands at `$CHROMIUM_ROOT/src/out/Default/chrome` (default: `~/chromium-build/src/out/Default/chrome`).

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

Run the whole suite via `scripts/run_unit_tests.sh` (`./build.sh --test` does this automatically after building).

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
.
├── build.sh                          one-shot build driver (tmux + logging + idempotent stages)
├── scripts/                          patch + overlay tooling invoked by build.sh
│   ├── apply_patches.py              UC series + Ghostium series
│   ├── unapply_patches.py            reverse of apply_patches.py
│   ├── sync_overlay.py               symlink overlay into Chromium src
│   └── run_unit_tests.sh             run all overlay unit_tests
├── config/                           pinned versions + GN args
│   ├── chromium_version              pinned Chromium tag
│   ├── ungoogled_chromium_version    pinned UC tag
│   └── gn_args.gn                    Ghostium-specific GN args
├── ghostium_src/overlay/             overlay tree symlinked into Chromium src
│   ├── chrome/browser/ghostium/      browser-process Registry + Delivery + EmulationBridge
│   │   └── cdp/                      CDP handler (parser + Target.* dispatch)
│   ├── public/mojom/ghostium/        Mojo IDL (FingerprintProfile)
│   └── third_party/blink/renderer/modules/ghostium_fp/
│                                     renderer-side FingerprintNoiseSource + noise primitives
├── patches/ghostium/                 20 active hook patches (Specs A–G) + series
├── third_party/ungoogled-chromium/   UC submodule (provides patches/ + utils/)
├── plan.md                           unified architecture spec
├── AGENTS.md                         agent guidance + spec status table
└── README.md                         this file
```

## Licensing

Ghostium inherits the Chromium + ungoogled-chromium license terms. Overlay code under `ghostium_src/overlay/` is BSD-3-Clause unless otherwise noted in individual files.
