#!/usr/bin/env python3
"""Check human CSS/paint dumps and their shared text filter without displaying attacks."""
import argparse
import pathlib
import random
import subprocess
import tempfile

REPLACEMENT = '\ufffd'.encode('utf-8')
CSS_HEAD = b'\x1b[1;33m--- [CSS Stylesheets] ---\x1b[0m\n'
PAINT_HEAD = b'\x1b[1;33m--- [Display List Paint Commands] ---\x1b[0m\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', default='build/litebird')
    ap.add_argument('--probe', default='build/terminal_text_probe')
    args = ap.parse_args()
    binary = pathlib.Path(args.binary).resolve()
    probe = pathlib.Path(args.probe).resolve()
    checks = []

    def check(label, condition):
        checks.append((label, bool(condition)))

    def filtered(data):
        p = subprocess.run([str(probe)], input=data, capture_output=True, timeout=20)
        return p.returncode == 0, p.stdout

    ascii_input = bytes(range(128))
    ascii_expected = b''.join(bytes([c]) if c in (9, 10) or 0x20 <= c <= 0x7e else REPLACEMENT
                              for c in range(128))
    ok, out = filtered(ascii_input)
    check('ASCII controls and permitted whitespace', ok and out == ascii_expected)
    ok, out = filtered(bytes(range(0x80, 0xa0)))
    check('raw C1 bytes', ok and out == REPLACEMENT * 32)
    ok, out = filtered(''.join(chr(c) for c in range(0x80, 0xa0)).encode())
    check('UTF-8 encoded C1 characters', ok and out == REPLACEMENT * 32)
    bad = b'\xc0\x9b\xed\xa0\x80\xf4\x90\x80\x80\xe2\x82A'
    ok, out = filtered(bad)
    check('overlong, surrogate, out-of-range and truncated UTF-8', ok and out == REPLACEMENT * 11 + b'A')
    good = 'café ě Ω 😀\tline\n'.encode()
    ok, out = filtered(good)
    check('valid UTF-8 including a 0x9b continuation survives', ok and out == good)

    def reference(data):
        # Python's strict UTF-8 decoder independently decides if the next
        # sequence is a Unicode scalar; malformed input advances by one byte.
        result = bytearray()
        i = 0
        while i < len(data):
            c = data[i]
            if c < 128:
                result.extend(bytes([c]) if c in (9, 10) or 0x20 <= c <= 0x7e else REPLACEMENT)
                i += 1
                continue
            length = 2 if 0xc2 <= c <= 0xdf else 3 if 0xe0 <= c <= 0xef else 4 if 0xf0 <= c <= 0xf4 else 0
            candidate = data[i:i + length] if length else b''
            try:
                if not length or len(candidate) != length:
                    raise ValueError('incomplete')
                char = candidate.decode('utf-8', 'strict')
            except (UnicodeDecodeError, ValueError):
                result.extend(REPLACEMENT)
                i += 1
                continue
            result.extend(REPLACEMENT if 0x80 <= ord(char) <= 0x9f else candidate)
            i += length
        return bytes(result)

    rng = random.Random(379)
    corpus = [bytes(rng.randrange(256) for _ in range(rng.randrange(1, 180))) + good for _ in range(64)]
    check('64 deterministic byte strings agree with Python UTF-8',
          all(ok and actual == reference(data) for data in corpus for ok, actual in [filtered(data)]))

    with tempfile.TemporaryDirectory(prefix='css-paint-terminal-') as directory:
        root = pathlib.Path(directory)
        page = root / 'attack.html'
        bmp = root / 'out.bmp'
        page.write_bytes(
            b'<html><head><meta charset="utf-8"><style>p {'
            b'--k\x1b]0;abc\x07: val; '
            b'--probe:"css\xce\xa9\x1b]0;OWNED\x07X\xc2\x9bY"; color:red;'
            b'}</style></head><body><p>paint\xce\xa9&#27;]52;c;OWNED&#7;X\xc2\x9bY</p></body></html>')
        result = subprocess.run([str(binary), '--dump-css', '--dump-paint', '--render-bmp', str(bmp), str(page)],
                                capture_output=True, timeout=30)
        check('real CLI completes', result.returncode == 0 and result.stderr == b'')
        out = result.stdout
        has_css = out.count(CSS_HEAD) == 1
        has_paint = out.count(PAINT_HEAD) == 1
        check('original dump headings remain intact', has_css and has_paint)
        css = out.split(CSS_HEAD, 1)[1].split(b'\n\n', 1)[0] if has_css else b''
        paint = out.split(PAINT_HEAD, 1)[1].split(b'\n\n', 1)[0] if has_paint else b''
        check('CSS property is filtered', b'abc' + REPLACEMENT + b': val;' in css)
        check('CSS value retains text but not controls',
              b'css\xce\xa9' + REPLACEMENT + b']0;OWNED' + REPLACEMENT + b'X' + REPLACEMENT + b'Y' in css)
        check('paint text retains text but not controls',
              b'paint\xce\xa9' + REPLACEMENT + b']52;c;OWNED' + REPLACEMENT + b'X' + REPLACEMENT + b'Y' in paint)
        check('neither human dump contains raw ESC, BEL or C1',
              all(b not in section for section in (css, paint) for b in (b'\x1b', b'\x07', b'\xc2\x9b')))
        # This is a machine-readable corpus format, not a human dump.
        tree = subprocess.run([str(binary), '--dump-dom-webplatform', str(page)], capture_output=True, timeout=30)
        check('webplatform tree data remains unchanged', tree.returncode == 0 and b'paint\xce\xa9\x1b]52;c;OWNED\x07X\xc2\x9bY' in tree.stdout)

    for label, ok in checks:
        print(('PASS ' if ok else 'FAIL ') + label)
    passed = sum(ok for _, ok in checks)
    print(f'CSS/paint terminal safety: {passed}/{len(checks)} checks pass')
    return int(passed != len(checks))


if __name__ == '__main__':
    raise SystemExit(main())
