# 38. ✅ Host names outside ASCII are converted with UTS #46

> Moved out of `AUDIT.md` — written there as a `## 38. ✅ Host names outside ASCII are converted with UTS #46` section.
> The text below is the section as it stood, unchanged.


**The hole.** The URL parser kept host names with characters outside ASCII
as written: `http://faß.example` stayed `faß.example` instead of becoming
`xn--fa-hia.example`. The web-platform-tests URL corpus said so
(`make url-conformance` was 1,150 of 1,188; every one of the 38 failures
was such a host), and so did every browser the data was recorded from.

**The fix.** The host parser now runs the URL Standard's domain parser:
percent-decode, UTF-8 decode (no BOM), then Unicode's UTS #46
(Nontransitional Processing — the flags the standard mandates:
`CheckBidi=true`, `CheckJoiners=true`, `Transitional=false`,
`VerifyDnsLength=false`), then the standard's forbidden-domain-code-point
check, then the usual `ends in a number` → IPv4 fallback. The UTS #46
machinery (mapping table, recursive canonical decompositions,
composition pairs derived from single-step decompositions and verified
through NFC, bidi classes, joining types) is table-driven and lives in
`src/net/Idna.cpp`; the mapping table and joining types are Unicode 15.1.0
(the `idna` package), while combining marks, combining classes,
decompositions, composition pairs and bidi classes come from the generator
host's `unicodedata` (14.0.0 here) — the generated header states both. Two bugs in the first cut were caught by
diffing against an outside oracle: the Punycode encoder started from code
point 0 instead of 0x80 (RFC 3492 `initial_n`), and the recomposition step
kept the consumed combining mark's class instead of resetting it to 0,
which broke every double-accented precomposed letter (U+01D6 `ǖ`, U+0390
`ΐ`, U+1E08 `Ḉ`); the composition pair table also had to be built from
single-step (not recursive) decompositions. `check_label` no longer
enforces the IDNA2008 CONTEXTO rules (middle dot only between `l`s and so
on): UTS #46 and the URL Standard never had them, and Node's
`domainToASCII` accepts those code points.

**How verified.** `make url-conformance` is 1,188 of 1,188 with a floor of
1,188. Beyond the corpus: every Unicode code point (1,112,063 of them) was
run through `domain_to_ascii` and compared against Node's `domainToASCII` —
99.9% identical, and every divergence accounted for (see
`tests/url/README.md`: IPv4 handling lives one layer up in the host parser
and is checked there, Node is lenient where RFC 5893 forbids mixing
left-to-right and right-to-left, and Node rejects the code points that are
PVALID only because they are unassigned). A 2,263-case sweep of
multi-code-point domains against both Node and Python's `idna` found no
other disagreement, and an exhaustive code point differential against a
Python UTS #46 mirror prints zero real divergences.

**What is still approximate.**

- ẞ (U+1E9E) converts to `xn--zca` (UTS #46 Nontransitional). Node's ICU
  answers `ss`. The WHATWG mapping and the corpus win; the divergence is
  deliberate and documented, never to be "fixed".
- ß (U+00DF) in a domain maps to itself, not `ss` — Nontransitional, as
  required.
- Bidi: litebird implements RFC 5893 §2 exactly (CheckBidi), which rejects
  labels that mix left-to-right and right-to-left characters and labels
  that start with an AN-class digit or a combining mark, where Node accepts
  some of them. The URL Standard's CheckBidi is the authority; if the
  web-platform-tests data ever gains a mixed-direction host case, this is
  the behaviour it must have.
- Unassigned code points that are PVALID (e.g. U+11F00) are accepted; both
  Node and Python's `idna` reject them (they have no bidi data for them).
  UTS #46 accepts them, so does litebird.
- The ASCII fast path (all-ASCII domain returned lowercased, regardless of
  what ToASCII would say) is the standard's web-compatibility rule, not a
  shortcut: an all-ASCII domain with, say, an interior underscore parses
  even though UseSTD3ASCIIRules would call it invalid.
- Combining marks, combining classes, decompositions, composition pairs and
  bidi classes come from the generator host's `unicodedata` (Unicode 14.0.0,
  Python 3.11); the mapping and joining-type tables are Unicode 15.1.0. The
  drift only affects code points assigned after Unicode 14 — currently all
  unassigned PVALID code points, which need no bidi data anyway. The
  generated header states both versions.

---
