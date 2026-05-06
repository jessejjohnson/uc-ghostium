# Agent guidance

The canonical specification for this project is [plan.md](plan.md). Read it
before making any structural changes.

Key constraints that agents must respect:

- Browser-process code lives on `content::BrowserThread::UI`; renderer code
  lives on `blink::Thread::MainThread()`. Every Ghostium entry point opens
  with a `DCHECK_CURRENTLY_ON(BrowserThread::UI)` or `DCHECK(IsMainThread())`
  guard. See plan.md section 2.2.
- The process boundary is Mojo only. No shared pointers cross it.
- Hook patches live in `patches/ghostium/` and must be thin call-site
  insertions only (plan.md section 4.7). All logic lives in
  `ghostium_src/overlay/`.
- Patch `0020-*` is the single GN-touching patch. Do not spread GN changes
  across multiple patches.
- Non-Ghostium contexts must be bitwise identical to upstream
  ungoogled-chromium (plan.md R16). `FingerprintNoiseSource::IsActive()`
  returning `false` means every hook must be a passthrough.
- Do not modify `plan.md` without explicit user request.

## Downstream spec status

| Spec | Status |
|---|---|
| Spec-A (build substrate) | implemented |
| Spec-B (core types + IPC skeleton) | implemented (passthrough) |
| Spec-C (CDP handler) | implemented |
| Spec-D (Canvas / WebGL / Audio noise) | implemented |
| Spec-E (Navigator family) | implemented |
| Spec-F (Screen / timezone) | not started |
| Spec-G (Fonts / MediaDevices / WebRTC) | not started |
