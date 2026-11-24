# Phase 253 — an honest media element

Task: the self-contained `HTMLMediaElement` state/constant/range-error cases
of the media oracle (Phase 229's row). Finding 257.

## 1. What was wrong

`tests/conformance/media_webgl_codecs.json` grades WHATWG HTML §4.8.11 with
19 `known_failure`s. Six of them asked nothing of a codec stack — they are
what a fresh `<audio>` element must answer before anything loads:

```js
HTMLMediaElement.NETWORK_EMPTY;   // undefined  (0)
a.volume = 2;                     // accepted silently (IndexSizeError)
a.volume = 0.25; a.volume;        // 1
a.muted = true; a.error;          // true; undefined (should be null)
a.buffered.length;                // undefined — TimeRanges did not exist
a.play().then(...);               // resolved a playback nobody would run
a.canPlayType('audio/mpeg');      // ""
```

`muted` read the `muted` **content attribute**; the spec keeps the live
muted state per element and `defaultMuted` is the attribute's reflection.

## 2. What changed

All in `src/js/DomHtmlBindings.cpp`:

- **Constants** — `NETWORK_EMPTY/IDLE/LOADING/NO_SOURCE` (0–3),
  `HAVE_NOTHING..HAVE_ENOUGH_DATA` (0–4), on the constructor and the
  prototype like `Event.NONE`, so subclasses see them too.
- **Per-element state** in the wrapper the realm caches for the element,
  under hidden `%`-names (`mediaVolume`, `mediaMuted`, `mediaCurrentTime`,
  `mediaPlaybackRate`). `volume` is the default `1`; `playbackRate` the
  default `1`; `currentTime` the default `0`; `muted` the default `false`.
- **`volume`** — a finite double outside `[0, 1]` is refused with
  `IndexSizeError` (HTML §4.8.11.3), and a non-finite double with
  `TypeError` (the WebIDL `double` rule browsers apply for `NaN`/`Infinity`).
  A non-finite value for `currentTime`/`playbackRate` is the same
  `TypeError`. A refused write never lands: the old value stays.
- **`muted`** is the live state and never touches the attribute.
  `defaultMuted` keeps reflecting it (`b:defaultMuted:muted` unchanged).
- **`error`** answers `null` (nothing ever loads, so there is never an
  `MediaError`). **`buffered`/`seekable`/`played`** answer an empty
  `TimeRanges`: `length` is `0` and `start(i)`/`end(i)` throw the
  `IndexSizeError` the spec requires of an out-of-range index — which is
  every index in an empty list.
- **`TimeRanges`/`MediaError`** are interfaces: constructible by name from
  a script (typeof `'function'`), not constructible by `new`, matching
  browsers. `MediaError` carries `MEDIA_ERR_ABORTED` (1),
  `MEDIA_ERR_NETWORK` (2), `MEDIA_ERR_DECODE` (3),
  `MEDIA_ERR_SRC_NOT_SUPPORTED` (4).
- **`canPlayType`** — `maybe` for the container/codec pairs every browser
  ships (the answer is honest: this engine has no codec stack, so nothing
  here may ever say `probably`), `""` for anything else or for a codec the
  type does not carry. The parsing is minimal but real: the MIME type is
  lower-cased and split off the parameters, a `codecs=` list may be quoted
  and is comma-split, and a codec matches a known prefix with a dot-suffix
  (`avc1.42E01E` matches `avc1`, `mp4a.40.2` matches `mp4a`,
  `vp09.00.10.08` matches `vp09`).
- **`play()`** — rejects with `NotSupportedError`. Nothing in this engine
  can fetch, demux, decode or output a resource, so resolving a promise
  that promises playback was a false statement; browsers reject with
  exactly this error for a source they cannot play.

## 3. Verification

`python3 tools/conformance/verify_media_webgl_codecs.py`:

**6/25 → 12/25 pass** (13 known, 0 fixed, 0 fail; 5/5 negative controls).
`audio_video` **4/12 → 10/12**: `media_element_constants`,
`media_element_volume_setter_and_range_error`,
`media_element_muted_current_time_and_playback_rate`,
`media_element_time_ranges_and_media_error`,
`media_element_can_play_type_standard_codecs` and
`media_element_play_without_source_rejects` are promoted from
`known_failure`. `webcodecs` 0/5 and `webgl` 2/8 are unchanged, as are the
WAV data-URI and MediaSource/Web Audio cases (they need a decoder and
APIs). `make test` **all 394 suites pass** (new suite:
`tests/suites/253-html-media-element-state.cpp`). `make dom-conformance`
678/678 (everyday 68/68); `make js-conformance` 1149/1158 (everyday
90/90), unchanged; `make check-numbers` and `make check-docs` 0 failures.

## 4. What is still open

- The load algorithm itself: `networkState` never leaves `0`, `readyState`
  stays `HAVE_NOTHING`, `currentSrc` stays `""`, no `load`/`error`/`canplay`
  events ever fire, `paused` cannot change, `ended` stays false.
- Fetch and demux of any resource — including the 100 ms PCM WAV data URI
  case (`media_element_wav_data_uri_loads_metadata`, still KNOWN).
- `currentTime` does not seek (there is nothing to seek in); it stores the
  position, which is what an empty element does in browsers.
- Seeking semantics, `playbackRate === 0` (and negative rates), and
  `defaultPlaybackRate`.
- `MediaSource`, Web Audio, WebCodecs, WebGL — every other row of the
  Phase 229 oracle, untouched.
- `canPlayType` never answers `probably`; when a codec stack lands, the
  answers for the types we truly decode should be revisited.
- The media state lives in the element's script wrapper for the realm
  asking, not on the tree: a second realm over the same element keeps its
  own copy (form state lives on the tree for this reason). Nothing reads
  media state from the tree today, so the divergence cannot be observed in
  one realm; recording it so it is not discovered as a bug later.
