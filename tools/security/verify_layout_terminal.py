#!/usr/bin/env python3
"""Capture layout-dump terminal attacks without printing their bytes."""
import argparse
import pathlib
import subprocess
import tempfile


HEADING = b'\x1b[1;33m--- [Layout Box Tree] ---\x1b[0m\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    binary = pathlib.Path(parser.parse_args().binary).resolve()
    checks = []

    def check(name, condition):
        checks.append((name, bool(condition)))

    with tempfile.TemporaryDirectory(prefix='layout-terminal-') as directory:
        root = pathlib.Path(directory)
        page = root / 'page.html'
        page.write_text('<!doctype html><meta charset="utf-8">'
                        '<p id="Idě&#27;]0;OWN&#7;&#13;\u009bZ">'
                        'Textě&#27;]52;X&#7;&#13;\u009bZ</p>', encoding='utf-8')
        result = subprocess.run([str(binary), '--dump-layout', '--render-bmp', str(root / 'out.bmp'), str(page)],
                                capture_output=True, timeout=30)
        check('real layout dump completes', result.returncode == 0 and result.stderr == b'')
        check('program-owned layout heading is unchanged', result.stdout.startswith(HEADING))
        dump = result.stdout[len(HEADING):].split(b'\n\n', 1)[0] if result.stdout.startswith(HEADING) else b''
        id_text = 'Idě�]0;OWN���Z'.encode()
        # Normal layout collapses the text's CR to a space before dumping it.
        text = 'Textě�]52;X� �Z'.encode()
        check('element ID is filtered without losing Unicode', b'#' + id_text + b' [' in dump)
        check('laid-out text is filtered without losing Unicode', b'text="' + text + b'"' in dump)
        try:
            chars = dump.decode('utf-8', 'strict')
        except UnicodeDecodeError:
            chars = ''
        check('layout body contains no terminal controls', bool(chars) and
              all((ord(c) in (9, 10) or (ord(c) >= 32 and not 0x7f <= ord(c) <= 0x9f)) for c in chars))
        check('normal geometry and box structure survive', b'BlockBox' in dump and b'TextBox' in dump and b' [x=' in dump)

        # Human layout filtering does not affect the separate html5lib data
        # format, which must retain the page's original controls verbatim.
        tree = subprocess.run([str(binary), '--dump-dom-webplatform', str(page)], capture_output=True, timeout=30)
        original_id = 'Idě\x1b]0;OWN\x07\r\u009bZ'.encode()
        original_text = 'Textě\x1b]52;X\x07\r\u009bZ'.encode()
        check('webplatform DOM keeps original ID and text',
              tree.returncode == 0 and original_id in tree.stdout and original_text in tree.stdout)

        script = root / 'check.js'
        script.write_text("var p=document.querySelector('p'); var i=p.id, t=p.textContent; "
                          "console.log('ID:'+i.charCodeAt(3)+':'+i.charCodeAt(10)+':'+i.charCodeAt(11)+':'+i.charCodeAt(12) "
                          "+' TEXT:'+t.charCodeAt(5)+':'+t.charCodeAt(11)+':'+t.charCodeAt(12)+':'+t.charCodeAt(13));", encoding='utf-8')
        visible = subprocess.run([str(binary), '--run-script', str(script), str(page)], capture_output=True, timeout=30)
        check('JavaScript reads original page controls', visible.returncode == 0 and
              b'ID:27:7:13:155 TEXT:27:7:13:155\n' in visible.stdout)

        normal = root / 'normal.html'
        normal.write_text('<!doctype html><meta charset="utf-8"><p id="plaině">hello Ω</p>', encoding='utf-8')
        ordinary = subprocess.run([str(binary), '--dump-layout', '--render-bmp', str(root / 'normal.bmp'), str(normal)],
                                  capture_output=True, timeout=30)
        check('ordinary layout text remains unchanged', ordinary.returncode == 0 and
              '#plaině'.encode() in ordinary.stdout and 'hello Ω'.encode() in ordinary.stdout and
              '�'.encode() not in ordinary.stdout)

    for name, ok in checks:
        print(('PASS ' if ok else 'FAIL ') + name)
    passed = sum(ok for _, ok in checks)
    print(f'Layout terminal safety: {passed}/{len(checks)} checks pass')
    return int(passed != len(checks))


if __name__ == '__main__':
    raise SystemExit(main())
