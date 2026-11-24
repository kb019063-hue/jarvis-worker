# Finding 257 — a media element kept nothing

**Severity:** an observable contract broken, not a crash. `document.createElement('audio')` was a
shell: its settings were accepted and dropped, its lists did not exist,
and its one promise made a promise the engine could not keep.

**Found by:** the media oracle (`tools/conformance/verify_media_webgl_codecs.py`,
Phase 229) — six of its 19 `known_failure` entries asked for exactly this
state, nothing of a codec stack, and were found fixable by taking the row
at its word.

## What it was

`setup_media` defined eight getters and setters as constants and no-ops:
`volume` read `1` and accepted any write, `muted` read the content
attribute, `currentTime` read `0`, `playbackRate` read `1`, and none of
them kept anything. `NETWORK_*`/`HAVE_*` were absent, so
`HTMLMediaElement.NETWORK_EMPTY` was `undefined`. `TimeRanges` did not
exist, so `a.buffered` was `undefined` instead of an empty list, and
`a.error` was `undefined` instead of `null`. `play()` resolved an
undefined promise whether or not any source existed — a promise that
reported a playback that never starts. `canPlayType` returned `""` for
every type, including standards everywhere on the machine.

## What the fix holds

- State lives on the wrapper the realm caches for the element, in hidden
  slots (`%mediaVolume`, …); nothing enumerates it and two realms keep
  separate state, which nothing can yet observe (see the phase doc).
- The write a page refuses does not land: `a.volume = 2` throws
  `IndexSizeError` and `a.volume` is unchanged. `NaN`/`Infinity` are a
  `TypeError` (the WebIDL `double` rule), distinct from out-of-range.
- `muted` is live state; `defaultMuted` is the attribute's reflection.
  Setting `muted` never sets the attribute — the bug that made
  `hasAttribute('muted')` answer true.
- An empty `TimeRanges` throws `IndexSizeError` from `start`/`end` for
  every index, as the spec requires of an out-of-range index; `length`
  answers `0`; the interfaces are not constructible (`new TimeRanges()` is
  the plain illegal-constructor `TypeError`).
- `play()` rejects with `NotSupportedError`: an engine with no fetch,
  demux, codec or output must not hand back a resolved promise for a
  playback nobody will run.
- `canPlayType` says `maybe` only for container/codec pairs every browser
  ships, and never `probably` (this engine cannot decode them); a `codecs=`
  list is comma-split, quoted or not, and a dotted codec id matches its
  prefix (`avc1.42E01E`).

## Still open

The load algorithm (nothing fetches; `networkState`, `readyState`,
`currentSrc` never change), seeking, `defaultPlaybackRate`, the media
events (`play`, `pause`, `timeupdate`, `ended`, …), MediaSource, Web
Audio, WebCodecs, WebGL. The WAV data-URI case remains KNOWN until a
decoder lands. Per-realm state divergence is recorded in
`docs/phases/253-an-honest-media-element.md` §4.
