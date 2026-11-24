#!/usr/bin/env python3
"""Check that a page's own text cannot command the terminal through a dump.

The subprocess output is captured and never printed: on a terminal that read
it, the bytes under test would be the attack, and this tool's job is to say
whether they are there, not to hand them over.

Each case is one field a tree dump shows — document title, attribute value,
text node, comment — with an instruction in it, run through the real CLI in
both dump formats. The output must contain none of the bytes a terminal acts
on: ESC (0x1b), BEL (0x07), CR (0x0d), the other C0 controls, DEL, or the
8-bit introductions 0x9b (CSI), 0x9d (OSC), 0x90 (DCS). A newline is the dump
writer's own and is allowed. A page's own UTF-8 text must still be there, and a
page with no instruction in it must dump exactly the bytes it always did.
"""
import argparse
import pathlib
import subprocess
import tempfile

# Every byte a terminal acts on.
FORBIDDEN = set(range(0x20)) - {0x0a} | {0x7f, 0x9b, 0x9d, 0x90}

# The one control sequence `--dump-dom` writes itself: its yellow heading line,
# the CLI's own and not the page's. Like the shell's frame it is checked by its
# exact bytes rather than exempted in general, so an injected SGR cannot hide
# inside one.
HEADING = {b'\x1b[1;33m--- [DOM Tree] ---\x1b[0m\n'}

FIELDS = {
    'title': '<!doctype html><html><head><title>{}</title></head><body></body></html>',
    'attribute': '<!doctype html><html><body><p title="{}"></p></body></html>',
    'text': '<!doctype html><html><body><p>{}</p></body></html>',
    'comment': '<!doctype html><html><body><!--{}--></body></html>',
}

# What a terminal would obey, and what a page might plausibly try. The
# attribute and text forms are the same page text; the introduction differs so
# that a fix to one field alone fails on another.
ATTACKS = {
    'title': 'OK&#27;]0;OWNED&#7;X',
    'attribute': 'x&#27;[2Jy',
    'text': 't&#27;]52;c;OWNED&#7;e',
    'comment': 'c&#155;1;1Hd',
}

# Text that must appear untouched beside the removed instruction: the page's
# word is not lost with its escape.
# Comment data is printed exactly as written, `1;1Hd` with no bracket: the
# 8-bit CSI the page used does not become a 7-bit `[`.
SURVIVES = {'title': 'OWNED', 'attribute': '[2Jy', 'text': 'OWNED', 'comment': '1;1Hd'}


def run(binary, path, mode):
    result = subprocess.run([str(binary), mode, str(path)],
                            capture_output=True, timeout=20)
    return result.returncode, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    binary = pathlib.Path(parser.parse_args().binary).resolve()
    passed = 0
    total = 0

    def check(label, condition, detail=''):
        nonlocal passed, total
        total += 1
        passed += bool(condition)
        print(('PASS ' if condition else 'FAIL ') + label + ('' if condition else ' ' + detail))

    with tempfile.TemporaryDirectory(prefix='page-text-term-') as directory:
        directory = pathlib.Path(directory)
        for field, template in FIELDS.items():
            page = directory / (field + '.html')
            page.write_text(template.format(ATTACKS[field]), encoding='utf-8')
            for mode in ('--dump-dom', '--dump-dom-webplatform'):
                status, out = run(binary, page, mode)
                check(f'{field} in {mode} runs', status == 0, f'exit {status}')
                body = out
                if mode == '--dump-dom':
                    first, _, rest = out.partition(b'\n')
                    check(f'{field} in {mode} writes only its own heading',
                          first + b'\n' in HEADING)
                    body = rest
                bad = sorted({b for b in body if b in FORBIDDEN})
                # Report the offset and the byte class, never the bytes.
                check(f'{field} in {mode} prints no control byte',
                      not bad,
                      'found: ' + ', '.join(f'0x{b:02x} at {body.index(bytes([b]))}' for b in bad))
                check(f'{field} in {mode} keeps the page\'s own text',
                      SURVIVES[field].encode() in body)

        # The opposite side: a page with nothing to hide dumps exactly the bytes
        # it always did. This is what `make wpt` and `make diff-trees` read, and
        # a filter that changed them would break both.
        plain = directory / 'plain.html'
        plain.write_text('<!doctype html><html><head><title>T</title></head>'
                         '<body><p title="v">text</p><!-- c --></body></html>', encoding='utf-8')
        for mode, wanted in (
            ('--dump-dom', b'#document (title: "T")\n'),
            ('--dump-dom-webplatform', b'|       "text"\n'),
        ):
            status, out = run(binary, plain, mode)
            check(f'an ordinary page dumps as before in {mode}',
                  status == 0 and wanted in out and b'\xef\xbf\xbd' not in out)
            if mode == '--dump-dom':
                check('an ordinary page gets only the heading it always got',
                      out.partition(b'\n')[0] + b'\n' in HEADING and
                      not [b for b in out.partition(b'\n')[2] if b in FORBIDDEN])

    print(f'page text terminal safety: {passed}/{total} checks pass')
    return int(passed != total)


if __name__ == '__main__':
    raise SystemExit(main())
