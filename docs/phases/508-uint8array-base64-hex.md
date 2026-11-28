# Phase 508 — `Uint8Array` base64 and hex (`toBase64`/`fromBase64`/`setFromBase64`/`toHex`/`fromHex`/`setFromHex`)

**Finding:** [`docs/findings/508-uint8array-base64-hex.md`](../findings/508-uint8array-base64-hex.md)

TC39 `proposal-arraybuffer-base64` (stage 3) gives `Uint8Array` six methods: `toBase64`/`fromBase64`/`setFromBase64` on the base64 family and `toHex`/`fromHex`/`setFromHex` on the hex family. None existed — `typeof Uint8Array.prototype.toHex` was `undefined`.

## What was wrong

All six names were `undefined` on `%Uint8Array%` and its prototype. Scripts using the proposal threw `TypeError: … is not a function`.

## What changed

`src/js/TypedArray.cpp`:

- `Uint8Array.prototype.toHex()` — validates `Uint8Array` brand and out-of-bounds, then emits `0123456789abcdef` per byte.
- `Uint8Array.prototype.toBase64(options?)` — `alphabet: "base64" | "base64url"` (default `base64`), `omitPadding` boolean. Encodes with RFC 4648 alphabet, `omitPadding` drops trailing `=`; `base64url` uses `-_` in place of `+/`.
- `Uint8Array.prototype.setFromHex(string)` / `Uint8Array.fromHex(string)` — hex decode. Odd-length input throws `SyntaxError`; non-hex digit throws `SyntaxError`. `fromHex` allocates a new `Uint8Array`, `setFromHex` writes into the receiver up to `byteLength` and returns `{read, written}`.
- `Uint8Array.prototype.setFromBase64(string, options?)` / `Uint8Array.fromBase64(string, options?)` — base64 decode with `alphabet` and `lastChunkHandling: "loose" | "strict" | "stop-before-partial"` (default `loose`). Whitespace (ASCII `\t\n\f\r `) is skipped. `strict` rejects a final chunk whose extra bits are non-zero and any non-multiple-of-4 length without padding; `loose` decodes a final `2`/`3`-char chunk, `stop-before-partial` drops it; `=`/`==` padding is recognised and trailing data after padding throws. `base64url` rejects `+`/`/` and maps `-_` to `+`/` `maxLength` truncation (receiver length for `setFrom*`, unbounded for `from*`) uses the spec's remaining-bytes guard.

Options use `GetOptionsObject` semantics: `undefined` → empty object, non-object → `TypeError`, unknown `alphabet`/`lastChunkHandling` → `TypeError`, first argument non-string → `TypeError`.

## What it measured

- `tests/suites/508-uint8array-base64-hex.cpp` — 48 assertions: `toHex`/`fromHex` round-trip, `toBase64`/`fromBase64` with padding and `omitPadding`, `base64url` translation and rejection, `setFrom*` `{read,written}` and truncation, whitespace tolerance, `strict` extra-bits and `stop-before-partial`, odd-length and non-hex `SyntaxError`, option validation, brand and `TypeError` on non-string.

- `make test` 545/545 (544 + new suite). No test262 delta expected (proposal not in pinned corpus).

## Still open

- No `ArrayBuffer` base64/hex (proposal also defines them on `Uint8Array` only in this engine).
- `SharedArrayBuffer` not implemented, so detached/shared semantics not exercised.
