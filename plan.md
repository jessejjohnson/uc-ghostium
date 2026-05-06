# Ghostium - CDP-Driven Fingerprint Profile Fork: Unified Architecture Spec

- **Created:** 2026-04-23
- **Scope type:** Unified architecture spec (downstream implementation specs split from this)
- **Target upstream:** ungoogled-chromium, stable Chromium milestone pinned via `config/chromium_version`
- **Target platform:** Linux x86_64 only
- **Fork strategy:** ungoogled-chromium fork + Brave-style overlay + thin hook patches applied after UC's patch series

## 0. Position

Ghostium is a privacy-focused Chromium fork that replaces the runtime behavior of every fingerprintable web API with values supplied by a CDP client per `BrowserContext`. Session isolation, cookie partitioning, cache segregation, and extension orchestration are delegated entirely to standard CDP surfaces (`Target.createBrowserContext`, the `Storage` domain, the `Extensions` domain). Ghostium owns exactly two things:

1. An extended `Target.createBrowserContext` command that accepts a Ghostium-specific fingerprint profile structure, plus a companion `Target.setBrowserContextFingerprint` command for runtime mutation.
2. A deep Blink/V8 hook layer that applies that profile at every surface exposed to untrusted web content.

This spec defines boundaries, contracts, and the build substrate for both. Downstream implementation specs (Spec-A through Spec-G) live under this one and must respect its contracts.

## 1. Architecture overview

### 1.1 Component diagram

```text
┌─────────────────────────────────────────────────────────────────────┐
│  Browser Process                                                     │
│                                                                      │
│  CDP Layer (UI thread)                                               │
│    TargetHandler (patched)                                           │
│      - CreateBrowserContext(params, ghostiumFingerprint?)            │
│      - SetBrowserContextFingerprint(ctx_id, ghostiumFingerprint)     │
│                  │                                                   │
│                  ▼                                                   │
│  FingerprintProfileRegistry (UI thread, KeyedService)                │
│    - SetProfile(BrowserContext*, FingerprintProfile)                 │
│    - UpdateProfile(BrowserContext*, FingerprintProfile)              │
│    - GetProfile(BrowserContext*) -> optional<FingerprintProfile>     │
│    - AddObserver / RemoveObserver                                    │
│                  │                                                   │
│                  │ notifies on update                                │
│                  ▼                                                   │
│  FingerprintProfileDelivery (UI thread, WebContentsObserver)         │
│    - RenderFrameCreated(rfh) → push profile via Mojo                 │
│    - OnProfileUpdated(ctx) → re-push to every live rfh in ctx        │
└─────────────────────────────────────────────────────────────────────┘

            ▲ Mojo (blink.mojom.FingerprintProfileReceiver)
            │   SetProfile(FingerprintProfile)
            │
┌───────────┴─────────────────────────────────────────────────────────┐
│  Renderer Process (per BrowserContext, per site)                    │
│                                                                     │
│  FingerprintNoiseSource (Blink main thread, Supplement<Window>)     │
│    - SetProfile(FingerprintProfile)  ← Mojo                         │
│    - IsActive() → bool                                              │
│    - CurrentProfile() → const FingerprintProfile&                   │
│                                                                     │
│  Consumed by patched hooks at:                                      │
│    Canvas2D readback    (toDataURL / toBlob / getImageData)         │
│    WebGL readPixels                                                 │
│    WebGL getParameter   (UNMASKED_VENDOR/RENDERER + caps quantize)  │
│    AudioBuffer readback (getChannelData / copyFromChannel)          │
│    AnalyserNode readback (getFloat/Byte*FrequencyData)              │
│    navigator.webdriver                                              │
│    navigator.userAgent / platform / appVersion                      │
│    NavigatorUAData.getHighEntropyValues / brands / mobile           │
│    navigator.hardwareConcurrency / deviceMemory                     │
│    navigator.languages / navigator.language                         │
│    navigator.plugins / navigator.mimeTypes                          │
│    screen.* + window.devicePixelRatio                               │
│    Intl.DateTimeFormat.resolvedOptions().timeZone                   │
│    Date.prototype.getTimezoneOffset / toLocaleString                │
│    document.fonts / CSS font face matching                          │
│    MediaDevices.enumerateDevices                                    │
│    RTCPeerConnection ICE candidate surfacing                        │
└─────────────────────────────────────────────────────────────────────┘
```

### 1.2 Component responsibility table

| Module | Responsibility (one line) | Owner | Thread | Location |
|---|---|---|---|---|
| CDP Handler Extension | Parse `ghostiumFingerprint` params, dispatch to Registry | `TargetHandler` (patched) | UI | `patches/` + `ghostium_src/overlay/content/browser/devtools/` |
| FingerprintProfileRegistry | Store FingerprintProfile per BrowserContext, emit change events | `BrowserContext` (KeyedService) | UI | `ghostium_src/overlay/chrome/browser/ghostium/` |
| FingerprintProfileDelivery | Mojo push profile to every live frame on create + update | `Profile` (KeyedService owns per-WebContents observers) | UI | `ghostium_src/overlay/chrome/browser/ghostium/` |
| FingerprintNoiseSource | Cache profile in renderer, apply at every hook point | `LocalDOMWindow` (Supplement) | Renderer main | `ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/` |
| Hook patches | Thin call-site insertions only; all logic lives in overlay | upstream files | UI or Renderer main | `patches/ghostium/` |

### 1.3 Primary data flows

**Flow 1: Create a BrowserContext with a fingerprint profile**

```text
CDP client → Target.createBrowserContext {
  disposeOnDetach: true,
  ghostiumFingerprint: { userAgent, platform, hardwareConcurrency,
                         canvasSeed, webglSeed, audioSeed, ... }
}
  → TargetHandler::CreateBrowserContext (patched)
     ├─ Creates OTR Profile via standard path
     ├─ FingerprintProfileRegistry::GetForProfile(p)->SetProfile(p, parsed)
     └─ Returns {browserContextId}

Subsequent Target.createTarget in that context
  → WebContents created inside OTR Profile
  → WebContentsObserver::RenderFrameCreated in FingerprintProfileDelivery
  → Registry::GetProfile(rfh->GetBrowserContext()) returns populated
  → Mojo: FingerprintProfileReceiver::SetProfile(profile) to renderer
  → FingerprintNoiseSource caches; all hooks become active
```

**Flow 2: Runtime profile mutation**

```text
CDP client → Target.setBrowserContextFingerprint {
  browserContextId, ghostiumFingerprint: { ... new values ... }
}
  → TargetHandler::SetBrowserContextFingerprint (new command)
     ├─ Resolves browserContextId → BrowserContext*
     └─ Registry::UpdateProfile(ctx, new_profile)
         └─ Notifies observers
             └─ FingerprintProfileDelivery::OnProfileUpdated(ctx)
                 └─ For every live WebContents in ctx, re-push via Mojo
                     → Renderer: FingerprintNoiseSource::SetProfile replaces cache
```

**Flow 3: Canvas 2D readback with noise**

```text
JS: canvas.toDataURL() | toBlob() | getImageData()
  → CanvasRenderingContext2D::GetImagePixels (patched)
  → [hook] FingerprintNoiseSource::ApplyCanvas2DNoise(image_data)
     ├─ If !IsActive() return (non-Ghostium context, identity passthrough)
     ├─ seed32 = low32(profile.canvasSeed)
     └─ For each pixel index i:
        Mulberry32(seed32 ^ i) → three 1-bit deltas for R, G, B channels
        byte = clamp(byte + delta * ±1, 0, 255)
  → Encoded → returned to JS
```

**Flow 4: UA + UA-CH propagation**

Ghostium does not patch the HTTP stack for UA / Accept-Language / Sec-CH-UA-*. The CDP handler translates `profile.userAgent` and `profile.userAgentMetadata` into a call to the existing `DevToolsAgentHost::SetUserAgentOverride` for every target created in the context, which already handles both the network layer (`User-Agent`, `Accept-Language`, `Sec-CH-UA-*` headers) and Blink exposure (`navigator.userAgent`, `navigator.userAgentData`). Ghostium's own hooks cover only the surfaces `Emulation.*` does not reach (Canvas, WebGL, Audio, webdriver, screen.*, hardwareConcurrency when no active override, fonts, plugins, MediaDevices, WebRTC, timezone fine-grained control).

## 2. Thread model and lifecycle

### 2.1 Thread assignment matrix

| Concern | Thread | Created | Destroyed |
|---|---|---|---|
| FingerprintProfileRegistry | UI | Profile construction (KeyedServiceFactory) | Profile shutdown |
| FingerprintProfileDelivery | UI | Profile construction (KeyedService owns WebContents observers) | Profile shutdown |
| CDP handler extension | UI | DevTools session attach | DevTools session detach |
| FingerprintNoiseSource | Renderer main | `ExecutionContext` creation | `ExecutionContext` destruction |
| Profile Mojo delivery | UI send → Renderer main receive | `RenderFrameCreated` and on every profile update | Once per push |
| Canvas / WebGL / Audio noise | Renderer main | Readback call | Per readback |
| Navigator / Screen / Intl getters | Renderer main | Getter call | Per getter |
| WebRTC ICE filter | UI (policy) + Renderer main (enumeration) | PeerConnection creation | PeerConnection close |

### 2.2 Thread invariants (enforced by DCHECK)

- Browser-process work runs on `content::BrowserThread::UI`. Every Ghostium browser entry point opens with `DCHECK_CURRENTLY_ON(BrowserThread::UI)`.
- Renderer-process work runs on `blink::Thread::MainThread()`. Every hook entry opens with `DCHECK(IsMainThread())`.
- Cross-thread or cross-process handoff is Mojo only. No shared pointers cross the process boundary.
- `FingerprintProfile` is a Mojo-serialized value type. No pointers, no refs to browser-process objects survive the trip.
- Noise seeds are derived from profile fields inside the renderer; the browser never ships derived seeds, only the three scalar roots (`canvasSeed`, `webglSeed`, `audioSeed`).
- IO thread is never touched directly. All network / storage work goes through `content/` public APIs.

### 2.3 Major lifecycles

- **Profile attach:** `TargetHandler::CreateBrowserContext` synchronously creates the OTR Profile, then calls `Registry::SetProfile`. The CDP command completes before returning `browserContextId` to the client; any immediate `Target.createTarget` call sees a fully populated profile.
- **Frame attach:** `FingerprintProfileDelivery` is a `WebContentsObserver` instantiated per WebContents in a Ghostium profile. `RenderFrameCreated` fires for main frame and every subframe (including cross-origin subframes in their own RenderProcessHost). Each receives the same profile via `mojo::AssociatedRemote<FingerprintProfileReceiver>` bound to the frame.
- **Profile update:** `Registry::UpdateProfile` is synchronous on the UI thread; observer fan-out is synchronous; Mojo push is asynchronous but ordered per-frame. A racing `toDataURL()` on the renderer may see the old profile, which is acceptable (CDP client cannot assume atomic global update across N frames).
- **Profile detach:** On `Profile` destruction, `Registry` is torn down first by KeyedService dependency ordering. Any in-flight Mojo message is dropped (frames are also going away). `FingerprintProfile` values held by renderer-side `FingerprintNoiseSource` outlive the browser-side record because they are full value copies.
- **Non-Ghostium contexts:** For any BrowserContext the client never called `createBrowserContext` with a fingerprint on (default Profile, Incognito opened via UI, etc), `Registry::GetProfile` returns `std::nullopt`. `FingerprintProfileDelivery::RenderFrameCreated` short-circuits and never binds the Mojo remote, so the renderer's `FingerprintNoiseSource` stays in passthrough mode (every hook is a no-op). Upstream behavior is bitwise identical on non-Ghostium contexts.

### 2.4 Scope exclusions (declared positively)

- **Worker-thread Canvas (OffscreenCanvas on Worker).** Initial scope covers main-thread Canvas and WebGL only. OffscreenCanvas + WebGL on DedicatedWorker / SharedWorker is a subsequent spec.
- **WebGPU.** Not in initial scope; deferred to a later spec once the WebGPU readback story stabilizes upstream.
- **WASM-based fingerprinting (SharedArrayBuffer timing, performance.now precision).** Not covered; standard Chromium timer quantization is assumed sufficient.
- **Extension-surface fingerprinting.** Extensions are out of scope; the fingerprint profile does not propagate into extension processes.
- **TLS / network-layer fingerprinting (JA3, HTTP/2 SETTINGS order).** Not covered; upstream BoringSSL / net defaults apply.
- **Font fallback rendering differences at the Skia layer.** The `profile.fonts` whitelist controls enumeration and `@font-face` matching; it does not rewrite glyph rasterization metrics.

## 3. Repository structure and build system

### 3.1 Directory structure

Ghostium forks ungoogled-chromium directly. UC's tree is inherited unchanged; Ghostium additions live in named subdirs that do not collide with UC.

```text
/ (project root, git repo "ghostium", forked from ungoogled-software/ungoogled-chromium)
├── .devcontainer/
│   ├── devcontainer.json
│   ├── Dockerfile
│   └── post-create.sh
├── .github/workflows/
│   ├── pr-lint.yml
│   └── nightly-build.yml
├── patches/                        # UC's patches (inherited unchanged)
│   └── series                      # UC's series (inherited)
├── patches/ghostium/               # Ghostium additions, applied AFTER UC's series
│   ├── 0001-content-devtools-target-handler-hook.patch
│   ├── 0002-blink-navigator-webdriver-hook.patch
│   ├── 0003-blink-canvas2d-readback-hook.patch
│   ├── 0004-blink-webgl-readpixels-hook.patch
│   ├── 0005-blink-webgl-getparameter-hook.patch
│   ├── 0006-blink-audio-buffer-readback-hook.patch
│   ├── 0007-blink-analyser-node-readback-hook.patch
│   ├── 0008-blink-navigator-ua-hook.patch
│   ├── 0009-blink-navigator-uadata-hook.patch
│   ├── 0010-blink-navigator-hwc-devmem-hook.patch
│   ├── 0011-blink-navigator-languages-hook.patch
│   ├── 0012-blink-navigator-plugins-hook.patch
│   ├── 0013-blink-screen-hook.patch
│   ├── 0014-blink-window-devicepixelratio-hook.patch
│   ├── 0015-v8-intl-timezone-hook.patch
│   ├── 0016-v8-date-timezone-offset-hook.patch
│   ├── 0017-blink-fonts-enumeration-hook.patch
│   ├── 0018-blink-mediadevices-enumerate-hook.patch
│   ├── 0019-blink-rtcpeerconnection-ice-hook.patch
│   ├── 0020-content-browser-build-overlay-hook.patch
│   └── series
├── patches/ghostium.series         # concatenated after patches/series at apply time
├── ghostium_src/
│   └── overlay/
│       ├── chrome/browser/ghostium/
│       │   ├── fingerprint_profile.{h,cc}
│       │   ├── fingerprint_profile_registry.{h,cc}
│       │   ├── fingerprint_profile_registry_factory.{h,cc}
│       │   ├── fingerprint_profile_delivery.{h,cc}
│       │   ├── cdp/
│       │   │   ├── target_handler_ghostium.{h,cc}
│       │   │   └── profile_parser.{h,cc}
│       │   └── BUILD.gn
│       ├── third_party/blink/renderer/modules/ghostium_fp/
│       │   ├── fingerprint_noise_source.{h,cc}
│       │   ├── canvas2d_noise.{h,cc}
│       │   ├── webgl_noise.{h,cc}
│       │   ├── audio_noise.{h,cc}
│       │   ├── mulberry32.h
│       │   └── BUILD.gn
│       └── public/mojom/ghostium/
│           ├── fingerprint_profile.mojom
│           └── BUILD.gn
├── build/
│   ├── apply_patches.py            # extends UC's patch application with ghostium.series
│   ├── unapply_patches.py
│   ├── sync_overlay.py             # symlinks ghostium_src/overlay into Chromium src tree
│   ├── overlay_gn.patch            # the single GN-touching patch
│   ├── build_wrapper.sh
│   └── run_unit_tests.sh
├── config/
│   ├── chromium_version            # e.g. 135.0.7049.95 (matches UC pin)
│   ├── ungoogled_chromium_version  # UC tag pin
│   └── gn_args.gn
├── docs/
│   └── superpowers/
│       ├── specs/
│       │   └── 2026-04-23-ghostium-cdp-fingerprint-fork-design.md
│       └── plans/
└── README.md
```

### 3.2 Devcontainer (`.devcontainer/devcontainer.json`)

```jsonc
{
  "name": "ghostium-dev",
  "build": { "dockerfile": "Dockerfile" },
  // runArgs rationale:
  //   --cap-add=SYS_PTRACE       : gdb/lldb attach to renderer + gpu children
  //   --security-opt seccomp=... : Chromium installs its own seccomp sandbox;
  //                                host default conflicts and blocks launch
  //   --shm-size=2g              : Chromium /dev/shm usage, especially for
  //                                multiple renderers in testing
  "runArgs": [
    "--cap-add=SYS_PTRACE",
    "--security-opt", "seccomp=unconfined",
    "--shm-size=2g"
  ],
  "mounts": [
    "source=ghostium-src,target=/workspaces/chromium/src,type=volume",
    "source=ghostium-out,target=/workspaces/chromium/out,type=volume",
    "source=ghostium-depot,target=/workspaces/chromium/depot_tools,type=volume",
    "source=ghostium-ccache,target=/workspaces/chromium/.ccache,type=volume"
  ],
  "workspaceMount": "source=${localWorkspaceFolder},target=/workspaces/ghostium,type=bind",
  "workspaceFolder": "/workspaces/ghostium",
  "containerEnv": {
    "CHROMIUM_SRC": "/workspaces/chromium/src",
    "CHROMIUM_OUT": "/workspaces/chromium/out/Default",
    "DEPOT_TOOLS_PATH": "/workspaces/chromium/depot_tools",
    "PATH": "/workspaces/chromium/depot_tools:${PATH}",
    "CCACHE_DIR": "/workspaces/chromium/.ccache"
  },
  "postCreateCommand": ".devcontainer/post-create.sh",
  "customizations": {
    "vscode": {
      "extensions": [
        "llvm-vs-code-extensions.vscode-clangd",
        "ms-vscode.cpptools"
      ],
      "settings": {
        "clangd.arguments": ["--compile-commands-dir=/workspaces/chromium/out/Default"]
      }
    }
  }
}
```

### 3.3 Dockerfile

```dockerfile
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential pkg-config lsb-release sudo \
      python3 python3-pip python3-venv \
      git curl ca-certificates gnupg \
      lld clang clang-tidy clang-format clangd \
      ninja-build cmake ccache \
      file tzdata locales \
 && rm -rf /var/lib/apt/lists/*
ARG USER_UID=1000
ARG USER_GID=1000
RUN groupadd --gid ${USER_GID} dev \
 && useradd --uid ${USER_UID} --gid ${USER_GID} -m -s /bin/bash dev \
 && echo 'dev ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/dev
USER dev
WORKDIR /workspaces/ghostium
```

### 3.4 post-create.sh

```bash
#!/usr/bin/env bash
set -euo pipefail

if [ ! -d "${DEPOT_TOOLS_PATH}/.git" ]; then
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \
      "${DEPOT_TOOLS_PATH}"
fi

if [ ! -d "${CHROMIUM_SRC}/.git" ]; then
  mkdir -p "$(dirname "${CHROMIUM_SRC}")"
  cd "$(dirname "${CHROMIUM_SRC}")"
  fetch --nohooks --no-history chromium
  cd src
  CHROMIUM_VERSION=$(cat /workspaces/ghostium/config/chromium_version)
  git checkout "tags/${CHROMIUM_VERSION}" -b "work/${CHROMIUM_VERSION}"
  gclient sync --with_branch_heads --with_tags -D
  build/install-build-deps.sh --no-prompt
  gclient runhooks
fi

# UC's prune + domain substitution first
python3 /workspaces/ghostium/utils/prune_binaries.py "${CHROMIUM_SRC}" \
  /workspaces/ghostium/pruning.list
python3 /workspaces/ghostium/utils/domain_substitution.py apply \
  -r /workspaces/ghostium/domain_regex.list \
  -f /workspaces/ghostium/domain_substitution.list \
  -c /workspaces/ghostium/domsubcache.tar.gz \
  "${CHROMIUM_SRC}"

# Then Ghostium overlay + patches
python3 /workspaces/ghostium/build/sync_overlay.py
python3 /workspaces/ghostium/build/apply_patches.py
```

### 3.5 GN integration: `build/overlay_gn.patch`

One patch, one file touched, minimal diff. Appends a Ghostium dep group to the main `chrome/browser` and `third_party/blink/renderer/modules` source sets.

**`//chrome/browser/BUILD.gn`:**

```gn
group("ghostium_overlay_all") {
  deps = [
    "//ghostium_src/overlay/chrome/browser/ghostium",
  ]
}

source_set("browser") {
  # ... existing ...
  deps += [ ":ghostium_overlay_all" ]
}
```

**`//third_party/blink/renderer/modules/BUILD.gn`:**

```gn
deps += [ "//ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp" ]
```

**`//third_party/blink/public/mojom/BUILD.gn`:** add the ghostium mojom target to the public mojom `component` so renderer code can depend on it.

### 3.6 Overlay injection: `build/sync_overlay.py`

GN cannot reference paths outside `//src/...`. Ghostium's overlay is symlinked into the Chromium source tree at `$CHROMIUM_SRC/ghostium_src/overlay`. Editing stays in one place (the Ghostium repo); GN sees it as if it were native.

```python
OVERLAY_ROOT = Path("/workspaces/ghostium/ghostium_src/overlay")
CHROMIUM_SRC = Path(os.environ["CHROMIUM_SRC"])
TARGET = CHROMIUM_SRC / "ghostium_src" / "overlay"

if TARGET.is_symlink():
    TARGET.unlink()
elif TARGET.exists():
    shutil.rmtree(TARGET)
TARGET.parent.mkdir(exist_ok=True, parents=True)
TARGET.symlink_to(OVERLAY_ROOT)
```

### 3.7 Patch application: `build/apply_patches.py`

UC's own patch series is applied first (inherited from UC's build process). Ghostium's series is concatenated after.

```python
UC_SERIES = Path("patches/series").read_text().splitlines()
GHOSTIUM_SERIES = Path("patches/ghostium/series").read_text().splitlines()

for patch in UC_SERIES:
    subprocess.run(
        ["git", "apply", "--3way", str(Path("patches") / patch)],
        cwd=os.environ["CHROMIUM_SRC"],
        check=True,
    )

for patch in GHOSTIUM_SERIES:
    subprocess.run(
        ["git", "apply", "--3way", str(Path("patches/ghostium") / patch)],
        cwd=os.environ["CHROMIUM_SRC"],
        check=True,
    )
```

### 3.8 Rebase strategy (monthly)

1. Bump `config/chromium_version` and `config/ungoogled_chromium_version` to the new UC release.
2. Pull UC's new `patches/` directory wholesale (UC has already resolved upstream drift for their patches).
3. `gclient sync`.
4. `apply_patches.py` runs UC's series first; failures here are UC's to fix and we wait for the UC release, never edit UC's patches ourselves.
5. For Ghostium's own series: `git apply --3way` each in turn. Failures get hand-refreshed with `quilt refresh` or `git am --3way`.
6. `sync_overlay.py` is immutable (always a symlink).
7. `autoninja -C out/Default chrome` → `run_unit_tests.sh` → commit as `chore(rebase): M<ver>`.

**Patch minimization directives:**

- Every Ghostium hook patch is a thin call-site insertion: one or two lines that call into overlay code. Upstream logic is never rewritten.
- Patches target stable anchor points (function entry, existing comment markers, the end of existing `if` branches) to reduce drift.
- Patch size budget: each hook patch under 20 lines except `overlay_gn.patch` (which is the single GN-touching patch).

## 4. Primary C++ interfaces

> **About the diff placeholders:** `@@ -XXX,6 +XXX,10 @@` `XXX` values are upstream-version-dependent. Downstream implementation specs resolve them against the pinned `config/chromium_version`.

### 4.1 FingerprintProfile value type

```cpp
// ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile.h
#ifndef GHOSTIUM_OVERLAY_FINGERPRINT_PROFILE_H_
#define GHOSTIUM_OVERLAY_FINGERPRINT_PROFILE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ghostium {

// All fields optional. Absent means "pass through upstream default".
// This is the canonical in-process representation. Mojo struct in
// //ghostium_src/overlay/public/mojom/ghostium/fingerprint_profile.mojom
// is a field-for-field mirror.
struct FingerprintProfile {
  // Navigator / UA
  std::optional<std::string> user_agent;
  struct UserAgentBrand {
    std::string brand;
    std::string version;
  };
  std::optional<std::vector<UserAgentBrand>> ua_brands;
  std::optional<std::vector<UserAgentBrand>> ua_full_version_list;
  std::optional<bool> ua_mobile;
  std::optional<std::string> ua_platform;
  std::optional<std::string> ua_platform_version;
  std::optional<std::string> ua_architecture;
  std::optional<std::string> ua_bitness;
  std::optional<std::string> ua_model;
  std::optional<std::string> ua_full_version;
  std::optional<bool> ua_wow64;

  // Navigator scalars
  std::optional<std::string> platform;
  std::optional<uint32_t> hardware_concurrency;
  std::optional<double> device_memory;  // GiB rounded to 0.25, 0.5, 1, 2, 4, 8
  std::optional<std::vector<std::string>> languages;

  // Webdriver
  std::optional<bool> webdriver;  // default false when profile is present

  // Screen + window
  struct ScreenSpec {
    uint32_t width;
    uint32_t height;
    uint32_t avail_width;
    uint32_t avail_height;
    uint32_t color_depth;   // typically 24 or 30
    uint32_t pixel_depth;   // equals color_depth
  };
  std::optional<ScreenSpec> screen;
  std::optional<double> device_pixel_ratio;

  // Timezone + locale
  std::optional<std::string> timezone;  // IANA, e.g. "America/Los_Angeles"
  std::optional<std::string> primary_language;  // e.g. "en-US"

  // Canvas / WebGL / Audio noise seeds
  std::optional<uint64_t> canvas_seed;
  std::optional<uint64_t> webgl_seed;
  std::optional<uint64_t> audio_seed;

  // WebGL debug renderer info
  std::optional<std::string> webgl_vendor;    // UNMASKED_VENDOR_WEBGL
  std::optional<std::string> webgl_renderer;  // UNMASKED_RENDERER_WEBGL

  // Fonts: if present, document.fonts and CSS @font-face matching
  // restrict enumeration to this whitelist (plus the always-present
  // generic families: serif, sans-serif, monospace, cursive, fantasy).
  std::optional<std::vector<std::string>> fonts_whitelist;

  // Plugins / MIME types: if present, navigator.plugins returns exactly
  // these entries. Empty vector returns an empty plugin list.
  struct PluginSpec {
    std::string name;
    std::string description;
    std::string filename;
    std::vector<std::string> mime_types;
  };
  std::optional<std::vector<PluginSpec>> plugins;

  // WebRTC ICE policy
  enum class WebRTCPolicy {
    kDefault,                     // upstream default
    kDefaultPublicInterfaceOnly,  // only default public interface
    kDisableNonProxiedUdp,        // force proxy for UDP
    kDisableBoth,                 // no UDP at all
  };
  std::optional<WebRTCPolicy> webrtc_policy;
  std::optional<bool> webrtc_mdns_only;  // surface only mdns candidates

  // MediaDevices enumeration override
  struct MediaDeviceSpec {
    std::string device_id;
    std::string kind;   // "audioinput" | "audiooutput" | "videoinput"
    std::string label;
    std::string group_id;
  };
  std::optional<std::vector<MediaDeviceSpec>> media_devices;
};

}  // namespace ghostium
#endif
```

### 4.2 FingerprintProfileRegistry

```cpp
// ghostium_src/overlay/chrome/browser/ghostium/fingerprint_profile_registry.h
class FingerprintProfileRegistry : public KeyedService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnProfileSet(content::BrowserContext* ctx,
                              const FingerprintProfile& profile) {}
    virtual void OnProfileUpdated(content::BrowserContext* ctx,
                                  const FingerprintProfile& profile) {}
    virtual void OnProfileCleared(content::BrowserContext* ctx) {}
  };

  explicit FingerprintProfileRegistry(Profile* profile);
  ~FingerprintProfileRegistry() override;
  FingerprintProfileRegistry(const FingerprintProfileRegistry&) = delete;
  FingerprintProfileRegistry& operator=(const FingerprintProfileRegistry&) = delete;

  void SetProfile(content::BrowserContext* ctx, FingerprintProfile profile);
  void UpdateProfile(content::BrowserContext* ctx, FingerprintProfile profile);
  void ClearProfile(content::BrowserContext* ctx);
  std::optional<FingerprintProfile> GetProfile(
      content::BrowserContext* ctx) const;
  bool HasProfile(content::BrowserContext* ctx) const;

  void AddObserver(Observer* obs);
  void RemoveObserver(Observer* obs);

 private:
  // Key: raw BrowserContext*. Registry is per-Profile; entries are cleared on
  // Profile shutdown. For OTR BrowserContexts created via CDP, the entry is
  // cleared when disposeOnDetach triggers teardown.
  base::flat_map<content::BrowserContext*, FingerprintProfile> profiles_;
  raw_ptr<Profile> owning_profile_;
  base::ObserverList<Observer> observers_;
};
```

**Factory:** `FingerprintProfileRegistryFactory` uses `ProfileSelections::BuildForRegularAndIncognito()` so `GetForProfile(p) != nullptr` is an invariant across every Profile kind a CDP session can attach to. Callers on the hot path (`FingerprintProfileDelivery::RenderFrameCreated`, CDP handler) assert with `CHECK(registry)`.

### 4.3 Mojo IDL

```mojom
// ghostium_src/overlay/public/mojom/ghostium/fingerprint_profile.mojom
module blink.mojom;

struct GhostiumUserAgentBrand {
  string brand;
  string version;
};

struct GhostiumScreenSpec {
  uint32 width;
  uint32 height;
  uint32 avail_width;
  uint32 avail_height;
  uint32 color_depth;
  uint32 pixel_depth;
};

struct GhostiumPluginSpec {
  string name;
  string description;
  string filename;
  array<string> mime_types;
};

struct GhostiumMediaDeviceSpec {
  string device_id;
  string kind;
  string label;
  string group_id;
};

enum GhostiumWebRTCPolicy {
  kDefault,
  kDefaultPublicInterfaceOnly,
  kDisableNonProxiedUdp,
  kDisableBoth,
};

struct GhostiumFingerprintProfile {
  // All fields mirror FingerprintProfile in ghostium_src.
  // Optionals use nullable primitive wrappers per Mojo conventions.
  string? user_agent;
  array<GhostiumUserAgentBrand>? ua_brands;
  array<GhostiumUserAgentBrand>? ua_full_version_list;
  bool? ua_mobile;
  string? ua_platform;
  string? ua_platform_version;
  string? ua_architecture;
  string? ua_bitness;
  string? ua_model;
  string? ua_full_version;
  bool? ua_wow64;
  string? platform;
  uint32? hardware_concurrency;
  double? device_memory;
  array<string>? languages;
  bool? webdriver;
  GhostiumScreenSpec? screen;
  double? device_pixel_ratio;
  string? timezone;
  string? primary_language;
  uint64? canvas_seed;
  uint64? webgl_seed;
  uint64? audio_seed;
  string? webgl_vendor;
  string? webgl_renderer;
  array<string>? fonts_whitelist;
  array<GhostiumPluginSpec>? plugins;
  GhostiumWebRTCPolicy? webrtc_policy;
  bool? webrtc_mdns_only;
  array<GhostiumMediaDeviceSpec>? media_devices;
};

interface FingerprintProfileReceiver {
  SetProfile(GhostiumFingerprintProfile profile);
};
```

### 4.4 CDP handler extension

**PDL additions (`third_party/blink/public/devtools_protocol/browser_protocol.pdl`):**

```
domain Target
  # ... existing ...

  # Extended parameter (additive) on the existing command.
  command createBrowserContext
    parameters
      optional boolean disposeOnDetach
      optional string proxyServer
      optional string proxyBypassList
      optional array of string originsWithUniversalNetworkAccess
      # Ghostium addition:
      optional GhostiumFingerprintProfile ghostiumFingerprint
    returns
      BrowserContextID browserContextId

  # New command (Ghostium extension).
  command setBrowserContextFingerprint
    description Ghostium: replace the fingerprint profile for a live context.
      Observers propagate the new profile to every live target in that context.
    parameters
      BrowserContextID browserContextId
      GhostiumFingerprintProfile ghostiumFingerprint

  # New command (Ghostium extension).
  command clearBrowserContextFingerprint
    description Ghostium: clear the fingerprint profile for a context, restoring
      upstream default behavior for every live target in that context.
    parameters
      BrowserContextID browserContextId

  type GhostiumFingerprintProfile extends object
    properties
      optional string userAgent
      optional GhostiumUserAgentMetadata userAgentMetadata
      optional string platform
      optional integer hardwareConcurrency
      optional number deviceMemory
      optional array of string languages
      optional boolean webdriver
      optional GhostiumScreenSpec screen
      optional number devicePixelRatio
      optional string timezone
      optional string primaryLanguage
      # Seeds are 64-bit unsigned; CDP uses JSON number (double), so the
      # caller must supply a value representable exactly as a double
      # (up to 2^53). Larger values MUST be sent as hex strings - see
      # GhostiumFingerprintSeed type below.
      optional GhostiumFingerprintSeed canvasSeed
      optional GhostiumFingerprintSeed webglSeed
      optional GhostiumFingerprintSeed audioSeed
      optional string webglVendor
      optional string webglRenderer
      optional array of string fontsWhitelist
      optional array of GhostiumPluginSpec plugins
      optional string webrtcPolicy
      optional boolean webrtcMdnsOnly
      optional array of GhostiumMediaDeviceSpec mediaDevices

  type GhostiumFingerprintSeed extends object
    description 64-bit unsigned seed, encoded either as a JSON integer (if
      <= 2^53) or a lowercase hex string ("0x" prefixed) for full range.
    properties
      optional integer int
      optional string hex

  # ... GhostiumUserAgentMetadata, GhostiumScreenSpec, GhostiumPluginSpec,
  # GhostiumMediaDeviceSpec mirror the Mojo shapes above ...
```

**Handler patch anchor (`content/browser/devtools/protocol/target_handler.cc`):**

```diff
--- a/content/browser/devtools/protocol/target_handler.cc
+++ b/content/browser/devtools/protocol/target_handler.cc
@@ -XXX,6 +XXX,12 @@ Response TargetHandler::CreateBrowserContext(
     Maybe<String> proxy_bypass_list,
     Maybe<Array<String>> origins_with_universal_network_access,
+    Maybe<protocol::Target::GhostiumFingerprintProfile> ghostium_fingerprint,
     String* out_context_id) {
   // ... existing creation path ...
   BrowserContext* ctx = ...;
+  if (ghostium_fingerprint.isJust()) {
+    ghostium::cdp::ApplyCreateFingerprint(
+        ctx, std::move(*ghostium_fingerprint.takeJust()));
+  }
   *out_context_id = ...;
   return Response::Success();
 }
```

Two fresh commands added as overlay-defined methods wired through the generated CDP binding:

```cpp
// ghostium_src/overlay/chrome/browser/ghostium/cdp/target_handler_ghostium.{h,cc}
namespace ghostium::cdp {

// Called synchronously from the patched CreateBrowserContext body.
void ApplyCreateFingerprint(
    content::BrowserContext* ctx,
    std::unique_ptr<protocol::Target::GhostiumFingerprintProfile> raw);

// SetBrowserContextFingerprint / ClearBrowserContextFingerprint
// are registered as new method dispatchers on TargetHandler via a
// separate overlay class. The patch anchor lives in
// patches/ghostium/0001-content-devtools-target-handler-hook.patch:
// it forwards the two new method IDs to these free functions.
Response HandleSetBrowserContextFingerprint(
    const std::string& browser_context_id,
    std::unique_ptr<protocol::Target::GhostiumFingerprintProfile> raw);

Response HandleClearBrowserContextFingerprint(
    const std::string& browser_context_id);

}  // namespace ghostium::cdp
```

`ApplyCreateFingerprint` parses the CDP-level struct into a `FingerprintProfile`, calls `Registry::SetProfile`, and also calls into existing `DevToolsAgentHost::SetUserAgentOverride` + `Emulation.setTimezoneOverride` + `Emulation.setLocaleOverride` + `Emulation.setDeviceMetricsOverride` + `Emulation.setHardwareConcurrencyOverride` equivalents for every target subsequently created in the context, so the surfaces covered by upstream emulation are handled by upstream code and Ghostium's own hooks only cover what upstream emulation misses.

### 4.5 FingerprintProfileDelivery

```cpp
class FingerprintProfileDelivery
    : public KeyedService,
      public FingerprintProfileRegistry::Observer {
 public:
  explicit FingerprintProfileDelivery(Profile* profile);
  ~FingerprintProfileDelivery() override;

  // FingerprintProfileRegistry::Observer
  void OnProfileSet(content::BrowserContext* ctx,
                    const FingerprintProfile&) override;
  void OnProfileUpdated(content::BrowserContext* ctx,
                        const FingerprintProfile&) override;
  void OnProfileCleared(content::BrowserContext* ctx) override;

 private:
  class FrameObserver;  // per-WebContents; implements WebContentsObserver

  // Called on every RenderFrameCreated for a WebContents in a context
  // that has a live profile.
  void PushProfileToFrame(content::RenderFrameHost* rfh,
                          const FingerprintProfile& profile);

  raw_ptr<Profile> profile_;
  std::vector<std::unique_ptr<FrameObserver>> frame_observers_;
};
```

`PushProfileToFrame` acquires the frame's `mojo::AssociatedRemote<FingerprintProfileReceiver>` via `rfh->GetRemoteAssociatedInterfaces()` and calls `SetProfile(mojo_profile)`. The remote is fire-and-forget; the renderer caches and the browser holds no receiver state.

### 4.6 FingerprintNoiseSource (Blink)

```cpp
// ghostium_src/overlay/third_party/blink/renderer/modules/ghostium_fp/
//   fingerprint_noise_source.h
namespace blink {

class CORE_EXPORT FingerprintNoiseSource final
    : public GarbageCollected<FingerprintNoiseSource>,
      public Supplement<LocalDOMWindow>,
      public mojom::FingerprintProfileReceiver {
 public:
  static const char kSupplementName[];
  static FingerprintNoiseSource& From(LocalDOMWindow& window);
  static FingerprintNoiseSource* FromIfExists(LocalDOMWindow& window);

  explicit FingerprintNoiseSource(LocalDOMWindow& window);

  // mojom::FingerprintProfileReceiver
  void SetProfile(mojom::GhostiumFingerprintProfilePtr profile) override;

  bool IsActive() const { return profile_received_; }

  // Applied at hook sites.
  void ApplyCanvas2DNoise(ImageData*) const;
  void ApplyWebGLReadPixelsNoise(base::span<uint8_t> pixels,
                                 GLenum format, GLenum type) const;
  void ApplyAudioNoise(base::span<float> samples) const;

  // Overrides consulted at hook sites.
  bool WebDriverOverride(bool* out) const;
  bool UserAgentOverride(String* out) const;
  bool PlatformOverride(String* out) const;
  bool HardwareConcurrencyOverride(uint32_t* out) const;
  bool DeviceMemoryOverride(double* out) const;
  bool LanguagesOverride(Vector<String>* out) const;
  bool ScreenWidthOverride(uint32_t* out) const;
  bool ScreenHeightOverride(uint32_t* out) const;
  bool ScreenAvailWidthOverride(uint32_t* out) const;
  bool ScreenAvailHeightOverride(uint32_t* out) const;
  bool ScreenColorDepthOverride(uint32_t* out) const;
  bool ScreenPixelDepthOverride(uint32_t* out) const;
  bool DevicePixelRatioOverride(double* out) const;
  bool WebGLVendorOverride(String* out) const;
  bool WebGLRendererOverride(String* out) const;
  bool FontAllowed(const String& family) const;
  bool PluginsOverride(HeapVector<Member<DOMPluginSpec>>* out) const;

  void Trace(Visitor*) const override;

 private:
  bool profile_received_ = false;
  mojom::GhostiumFingerprintProfilePtr profile_;
  HeapMojoAssociatedReceiver<mojom::FingerprintProfileReceiver,
                             FingerprintNoiseSource> receiver_;
};

}  // namespace blink
```

**Mulberry32 (allocation-free, deterministic, 32-bit state):**

```cpp
class Mulberry32 {
 public:
  explicit Mulberry32(uint32_t seed) : state_(seed) {}
  uint32_t Next() {
    uint32_t z = (state_ += 0x6D2B79F5u);
    z = (z ^ (z >> 15)) * (z | 1u);
    z ^= z + (z ^ (z >> 7)) * (z | 61u);
    return z ^ (z >> 14);
  }
 private:
  uint32_t state_;
};
```

**Canvas 2D noise:**

```cpp
void FingerprintNoiseSource::ApplyCanvas2DNoise(ImageData* data) const {
  if (!profile_received_ || !profile_->canvas_seed) return;
  const uint32_t base = static_cast<uint32_t>(*profile_->canvas_seed);
  auto bytes = data->data()->Data();
  const size_t len = data->data()->length();
  for (size_t i = 0; i < len; i += 4) {
    Mulberry32 rng(base ^ static_cast<uint32_t>(i));
    for (int c = 0; c < 3; ++c) {  // R, G, B only (alpha untouched)
      const int delta = static_cast<int>(rng.Next() & 1u) * 2 - 1;
      const int v = static_cast<int>(bytes[i + c]) + delta;
      bytes[i + c] = static_cast<uint8_t>(std::clamp(v, 0, 255));
    }
  }
}
```

**WebGL readPixels noise:** Same Mulberry32 pattern keyed by `profile_->webgl_seed`. Format-aware: RGBA8 gets the same ±1 LSB perturbation per channel; FLOAT formats get a ±ULP*1 perturbation in the mantissa low bit.

**Audio noise:** For PCM float samples returned from `AudioBuffer::getChannelData` and analyser readback, apply a deterministic `±1e-7` perturbation keyed by `profile_->audio_seed ^ sample_index`. Amplitude is below human hearing threshold and below the noise floor of any consumer microphone, but sufficient to randomize `dynamicsCompressor` and FFT-based fingerprint probes.

### 4.7 Hook patch inventory

| # | File | Lines | Purpose |
|---|---|---|---|
| 0001 | `content/browser/devtools/protocol/target_handler.cc` | ~15 | CDP command dispatch: `createBrowserContext` extended param + two new commands |
| 0002 | `third_party/blink/renderer/core/frame/navigator.cc` | ~5 | `Navigator::webdriver()` consults FingerprintNoiseSource |
| 0003 | `third_party/blink/renderer/modules/canvas/canvas2d/canvas_rendering_context_2d.cc` | ~8 | Post-readback hook on `GetImagePixels` path |
| 0004 | `third_party/blink/renderer/modules/webgl/webgl_rendering_context_base.cc` | ~8 | Post-`readPixels` hook |
| 0005 | `third_party/blink/renderer/modules/webgl/webgl_debug_renderer_info.cc` | ~6 | `UNMASKED_VENDOR_WEBGL` / `UNMASKED_RENDERER_WEBGL` overrides |
| 0006 | `third_party/blink/renderer/modules/webaudio/audio_buffer.cc` | ~6 | `getChannelData` / `copyFromChannel` noise |
| 0007 | `third_party/blink/renderer/modules/webaudio/analyser_node.cc` | ~8 | `getFloat/ByteFrequencyData` + `getFloat/ByteTimeDomainData` noise |
| 0008 | `third_party/blink/renderer/core/frame/navigator_id.cc` | ~10 | `userAgent`, `platform`, `appVersion` when no emulation active |
| 0009 | `third_party/blink/renderer/core/frame/navigator_ua_data.cc` | ~12 | `getHighEntropyValues`, `brands`, `mobile`, `platform` |
| 0010 | `third_party/blink/renderer/core/frame/navigator_concurrent_hardware.cc` + `navigator.cc` (deviceMemory) | ~8 | `hardwareConcurrency`, `deviceMemory` when no emulation active |
| 0011 | `third_party/blink/renderer/core/frame/navigator_language.cc` | ~6 | `language`, `languages` when no emulation active |
| 0012 | `third_party/blink/renderer/modules/plugins/dom_plugin_array.cc` | ~10 | `navigator.plugins` whitelist or empty list |
| 0013 | `third_party/blink/renderer/core/frame/screen.cc` | ~12 | All six `screen.*` getters when no emulation active |
| 0014 | `third_party/blink/renderer/core/frame/local_dom_window.cc` | ~5 | `devicePixelRatio` when no emulation active |
| 0015 | `v8/src/objects/js-date-time-format.cc` | ~6 | `Intl.DateTimeFormat().resolvedOptions().timeZone` default when no emulation |
| 0016 | `v8/src/date/date.cc` | ~6 | `Date.prototype.getTimezoneOffset` default when no emulation |
| 0017 | `third_party/blink/renderer/modules/font_access/font_manager.cc` + `css/font_face_set_document.cc` | ~15 | Font enumeration + `@font-face` matching filtered by whitelist |
| 0018 | `third_party/blink/renderer/modules/mediastream/media_devices.cc` | ~10 | `enumerateDevices` override |
| 0019 | `third_party/blink/renderer/modules/peerconnection/rtc_peer_connection.cc` + `rtc_ice_candidate.cc` | ~12 | ICE candidate filtering (mdns-only / policy) |
| 0020 | `chrome/browser/BUILD.gn` + `third_party/blink/renderer/modules/BUILD.gn` + `third_party/blink/public/mojom/BUILD.gn` | ~15 | GN wiring (the single GN-touching patch) |

**Upstream delta total: ~180 lines.** Larger than Chromeleon's ~65-line budget because Ghostium covers many more surfaces, but still entirely call-site insertions. All logic (noise application, PRNG, override lookups) lives in the overlay. Rebase refresh on a monthly UC bump is expected to hit 2-4 patches; the rest ride through `--3way` cleanly.

Patches 0015 and 0016 are the only ones that touch V8 directly. Both are last-resort fallbacks: `Emulation.setTimezoneOverride` already routes through these paths via ICU overrides installed at isolate init, so the hooks are only active when the profile sets `timezone` but the CDP handler's translation step skipped the Emulation call (which is itself a bug, but the hooks make the system robust to it).

## 5. Build and test

### 5.1 Initial setup

```bash
# Host
git clone <ghostium-repo> && cd ghostium
devcontainer up --workspace-folder .

# Inside devcontainer, post-create.sh runs automatically:
#   - depot_tools clone
#   - chromium/src fetch at tagged version
#   - build/install-build-deps.sh
#   - UC prune + domain substitution
#   - sync_overlay.py symlink injection
#   - apply_patches.py (UC series + ghostium series)
# Total: 40 minutes to 2 hours depending on bandwidth.
```

### 5.2 Build

```bash
cd $CHROMIUM_SRC
gn gen out/Default --args="$(cat /workspaces/ghostium/config/gn_args.gn)"
autoninja -C out/Default chrome
out/Default/chrome \
  --remote-debugging-port=9222 \
  --user-data-dir=/tmp/ghostium-profile
```

**`config/gn_args.gn`:**

```gn
is_debug = false
is_component_build = true
symbol_level = 1
enable_nacl = false
use_goma = false
use_remoteexec = false
cc_wrapper = "ccache"
blink_symbol_level = 1
is_official_build = false
proprietary_codecs = false
ffmpeg_branding = "Chromium"
# UC-inherited:
chrome_pgo_phase = 0
```

### 5.3 Tests

```bash
# Full overlay unit_test suite
autoninja -C out/Default ghostium_overlay_tests
bash /workspaces/ghostium/build/run_unit_tests.sh

# Individual
autoninja -C out/Default fingerprint_profile_registry_unittest
out/Default/fingerprint_profile_registry_unittest
```

**Per-module unit_test coverage:**

| Module | Test targets | Fakes used |
|---|---|---|
| FingerprintProfileRegistry | Set/Update/Clear/Get, observer fan-out, ProfileKeyedService teardown, multiple concurrent BrowserContexts | `TestingProfile`, `MockBrowserContext` |
| CDP handler | Valid profile parse, malformed JSON rejection, seed hex-vs-int handling, roundtrip through Emulation domain equivalents, `setBrowserContextFingerprint` on unknown context returns error | `FakeDevToolsClient`, `TestingProfile` |
| FingerprintProfileDelivery | Push on frame creation, re-push on update, no-push for non-Ghostium context, correct Mojo binding lifetime | `TestWebContents`, `MockRenderFrameHost` |
| FingerprintNoiseSource (Canvas) | Same profile + same input → same output; different profile → different output; alpha channel untouched; clamping at 0 and 255 | `V8TestingScope`, `ImageData` fixtures |
| FingerprintNoiseSource (WebGL) | readPixels noise for RGBA8 + FLOAT formats; UNMASKED_* returns profile values | `V8TestingScope`, mock GL context |
| FingerprintNoiseSource (Audio) | getChannelData noise deterministic per seed; amplitude bounded; analyser frequency data differs across seeds | `V8TestingScope`, synthetic AudioBuffer |
| FingerprintNoiseSource (Navigator) | UA, platform, HWC, deviceMemory, languages, plugins return profile values; passthrough when profile absent | `V8TestingScope` |
| FingerprintNoiseSource (Screen) | All six properties + devicePixelRatio; passthrough when absent | `V8TestingScope` |
| FingerprintNoiseSource (WebRTC) | ICE candidate filter; mdns-only suppresses host candidates with raw IPs | `FakePeerConnection` |

### 5.4 Static analysis

```bash
git -C $CHROMIUM_SRC cl format --upstream-to=HEAD~1
tools/clang/scripts/run_tool.py --tool clang-tidy --all ghostium_src/overlay/
gn check out/Default //ghostium_src/overlay/...
```

### 5.5 GitHub Actions: `pr-lint.yml`

Same shape as Chromeleon's. Lint-only, `ubuntu-slim` runner.

```yaml
name: pr-lint
on:
  pull_request: { branches: [master] }
  push:         { branches: [master] }
jobs:
  lint:
    runs-on: ubuntu-slim
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: "3.12" }
      - name: Install clang-format
        run: sudo apt-get install -y clang-format-15
      - name: clang-format check (overlay)
        run: |
          find ghostium_src/overlay \( -name '*.cc' -o -name '*.h' \) -print0 |
            xargs -0 --no-run-if-empty clang-format-15 --dry-run --Werror
      - name: Patch dry-run (UC series + ghostium series)
        run: python3 build/ci_patch_dryrun.py
      - name: Python lint
        run: pip install ruff && ruff check build/
      - name: Spec markdown lint
        run: npx markdownlint-cli2 'docs/**/*.md'
      - name: Mojom lint
        run: python3 build/ci_mojom_lint.py ghostium_src/overlay/public/mojom/
      - name: PDL lint
        run: python3 build/ci_pdl_lint.py patches/ghostium/0001-*.patch
```

### 5.6 GitHub Actions: `nightly-build.yml`

Self-hosted runner, full Chromium build + overlay tests. Branded `ghostium-builder`.

```yaml
name: nightly-build
on:
  schedule: [ { cron: "0 10 * * *" } ]  # PT 02:00 / 03:00 DST
  workflow_dispatch: {}
jobs:
  build-and-test:
    runs-on: [self-hosted, linux, ghostium-builder]
    timeout-minutes: 360
    steps:
      - uses: actions/checkout@v4
      - name: gclient sync
        run: cd $CHROMIUM_SRC && gclient sync -D --force
      - name: UC prune + domain substitution
        run: |
          python3 utils/prune_binaries.py $CHROMIUM_SRC pruning.list
          python3 utils/domain_substitution.py apply \
            -r domain_regex.list -f domain_substitution.list \
            -c domsubcache.tar.gz $CHROMIUM_SRC
      - name: Sync overlay + apply patches
        run: |
          python3 build/sync_overlay.py
          python3 build/apply_patches.py
      - name: GN gen
        run: |
          cd $CHROMIUM_SRC
          gn gen out/Default --args="$(cat ../../config/gn_args.gn)"
      - name: Build overlay unit tests
        run: cd $CHROMIUM_SRC && autoninja -C out/Default ghostium_overlay_tests
      - name: Run unit tests
        run: bash build/run_unit_tests.sh
      - name: Build full chrome (smoke)
        run: cd $CHROMIUM_SRC && autoninja -C out/Default chrome
      - uses: actions/upload-artifact@v4
        if: always()
        with: { name: logs, path: ${{ env.CHROMIUM_SRC }}/out/Default/*.log }
```

## 6. Acceptance criteria

All must be true for release.

| ID | Requirement | Verification |
|---|---|---|
| R1 | `Target.createBrowserContext` accepts `ghostiumFingerprint` param; command returns `browserContextId`; Registry records the profile for the created BrowserContext | `cdp_target_handler_browsertest.cc` + `fingerprint_profile_registry_unittest.cc` |
| R2 | `Target.setBrowserContextFingerprint` replaces the profile for a live context; `Target.clearBrowserContextFingerprint` removes it | `cdp_target_handler_browsertest.cc` |
| R3 | `navigator.webdriver` returns `false` in a Ghostium context; returns upstream default (typically undefined/false) in non-Ghostium contexts | `navigator_webdriver_unittest.cc` + E2E via CDP |
| R4 | Canvas: same profile + same input → identical hash; different profile → different hash; alpha channel never touched | `canvas2d_noise_unittest.cc` |
| R5 | WebGL `readPixels`: per-profile determinism; RGBA8 and FLOAT formats both covered; `UNMASKED_VENDOR/RENDERER_WEBGL` return profile values | `webgl_noise_unittest.cc` + E2E |
| R6 | AudioContext readback: `getChannelData`, `AnalyserNode.getFloatFrequencyData`, `AnalyserNode.getByteTimeDomainData` all deterministic per `audioSeed`; amplitude perturbation below 1e-6 | `audio_noise_unittest.cc` |
| R7 | `navigator.userAgent`, `navigator.platform`, `navigator.hardwareConcurrency`, `navigator.deviceMemory`, `navigator.languages` all match profile (either via Ghostium hook or via `Emulation.setUserAgentOverride` which the CDP handler dispatches) | `navigator_ghostium_unittest.cc` + E2E |
| R8 | `navigator.userAgentData.brands`, `navigator.userAgentData.mobile`, full set of high-entropy hints via `getHighEntropyValues()` all match profile; Sec-CH-UA-* headers match profile | `navigator_ua_data_unittest.cc` + network log assertion |
| R9 | `screen.width / height / availWidth / availHeight / colorDepth / pixelDepth` and `window.devicePixelRatio` match profile | `screen_ghostium_unittest.cc` |
| R10 | `Intl.DateTimeFormat().resolvedOptions().timeZone` and `new Date().getTimezoneOffset()` match profile timezone | `intl_timezone_ghostium_unittest.cc` |
| R11 | `document.fonts` iteration and CSS `@font-face` matching restricted to `fontsWhitelist`; generic families always available | `fonts_ghostium_unittest.cc` |
| R12 | `navigator.plugins` returns exactly the profile's plugin list (or empty when set to `[]`) | `plugins_ghostium_unittest.cc` |
| R13 | `RTCPeerConnection` surfaces only mdns candidates when `webrtcMdnsOnly=true`; policy string respected when `webrtcPolicy` set | `webrtc_ghostium_unittest.cc` |
| R14 | `MediaDevices.enumerateDevices()` returns exactly the profile's device list | `media_devices_ghostium_unittest.cc` |
| R15 | Runtime mutation: `Target.setBrowserContextFingerprint` propagates to every live frame in the context within one event loop turn; subsequent API calls observe new values | E2E harness (CDP-driven) |
| R16 | Non-Ghostium BrowserContexts are bitwise-identical to upstream UC: `Registry::HasProfile` returns false, `FingerprintNoiseSource::IsActive` returns false, every hook becomes a passthrough | `ghostium_passthrough_browsertest.cc` |
| R17 | Rebase: monthly UC bump applies cleanly; any Ghostium patch drift is limited to 2-4 patches needing `--3way` refresh | Monthly rebase runbook |
| R18 | Build reproducibility: devcontainer-only setup, no host-side Chromium deps required | Fresh-environment CI smoke |

## 7. Downstream implementation spec split

This unified spec is the foundation. Implementation proceeds as independent downstream specs, each inheriting contracts above.

| Spec | Target | Depends on |
|---|---|---|
| Spec-A | Devcontainer + UC integration + skeleton overlay + GN patch + `apply_patches.py` + both CI workflows | (none) |
| Spec-B | `FingerprintProfile` struct + `FingerprintProfileRegistry` + `FingerprintProfileDelivery` + Mojo IDL + `FingerprintNoiseSource` skeleton with passthrough semantics | Spec-A |
| Spec-C | CDP handler extension: `createBrowserContext` extended param + `setBrowserContextFingerprint` + `clearBrowserContextFingerprint` + bridge to existing `Emulation.*` commands | Spec-B |
| Spec-D | Canvas 2D + WebGL `readPixels` + WebGL `UNMASKED_VENDOR/RENDERER` + AudioBuffer + AnalyserNode noise hooks (patches 0003, 0004, 0005, 0006, 0007) | Spec-B |
| Spec-E | Navigator family: webdriver, UA, UA-CH, platform, hardwareConcurrency, deviceMemory, languages, plugins (patches 0002, 0008, 0009, 0010, 0011, 0012) | Spec-B |
| Spec-F | Screen + devicePixelRatio + timezone (patches 0013, 0014, 0015, 0016) | Spec-B |
| Spec-G | Font enumeration + MediaDevices + WebRTC ICE filtering (patches 0017, 0018, 0019) | Spec-B |

Specs D through G can proceed in parallel once Spec-B lands. Spec-C should land before full E2E testing of D-G becomes tractable, but unit-test level work on D-G only needs Spec-B's `FingerprintNoiseSource::SetProfile` fixture.

## 8. Design decisions summary

- Session isolation is delegated entirely to CDP `Target.createBrowserContext`. Ghostium owns no partition, tab, or window management code.
- Every Ghostium subsystem lives in `ghostium_src/overlay/` or in `patches/ghostium/`. The single GN-touching patch is `0020`.
- `FingerprintProfile` is the sole carrier of state; it crosses the process boundary as a Mojo value type.
- CDP handler translates profile fields that map onto existing `Emulation.*` commands (UA, locale, timezone, device metrics, hardware concurrency) into calls against those commands. Ghostium's own hooks only cover surfaces `Emulation.*` misses.
- Every Blink/V8 hook is a thin call-site insertion; noise logic, PRNG, and override lookups live in overlay code.
- Noise is deterministic per seed (Mulberry32) and applied at every readback call. Non-Ghostium contexts see zero behavior change.
- UI thread owns Registry + Delivery; Blink main thread owns NoiseSource; the process boundary is Mojo only.
- Fork base is ungoogled-chromium. UC's patches apply first, then Ghostium's series.
- Upstream patch delta is ~180 lines, spread across ~20 files. All are stable call-site anchors to minimize rebase drift.
- Initial scope covers main-thread Canvas/WebGL/Audio and the navigator/screen/timezone/fonts/MediaDevices/WebRTC surfaces. OffscreenCanvas-on-Worker and WebGPU deferred.
- Linux x86_64 only for v1. macOS ARM64 is a subsequent spec.