"""The fallback must still paint with scripting disabled. No screenshot tolerance."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as tmp:
    page = Path(tmp) / 'fallback.html'
    page.write_text('<!doctype html><body><noscript><b>FALLBACK_SENTINEL</b></noscript><div>ALWAYS_VISIBLE</div>')
    for args, fallback in [([], False), (['--scripting=off'], True)]:
        log = subprocess.check_output([Path(sys.argv[1]).resolve(), '--dump-paint', *args, page], text=True, timeout=15)
        if 'ALWAYS_VISIBLE' not in log or ('FALLBACK_SENTINEL' in log) != fallback:
            sys.exit(f'unexpected paint output for {args}: {log}')
    print('2/2 noscript paint checks passed')
