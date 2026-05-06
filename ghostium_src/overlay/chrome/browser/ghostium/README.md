# chrome/browser/ghostium/

Ghostium browser-process overlay. Contains the `FingerprintProfileRegistry`
(per-`BrowserContext` fingerprint store, KeyedService) and the
`FingerprintProfileDelivery` (per-WebContents Mojo pusher).

See [plan.md](../../../../../plan.md) section 1.2 "Component responsibility
table" for ownership boundaries and thread invariants.
