"""Capture the pinned mdn/dom-examples snapshot. Never runs upstream code."""
import json, shutil, sys, tarfile
from pathlib import Path
from urllib.request import urlopen

pages = json.loads(Path(__file__).with_name('manifest.json').read_text())['pages']
root = Path(sys.argv[1])
wanted = {  # task id -> paths inside the repository to carry over
    'H1': ['popover-api/basic-declarative'],
    'H2': ['web-workers/simple-web-worker'],
    'H3': ['insert-adjacent'],
    'H4': ['url-params'],
}
target_dirs = {id: [root / id / d for d in dirs] for id, dirs in wanted.items()}

with urlopen('https://codeload.github.com/mdn/dom-examples/tar.gz/' + pages[0]['sha'], timeout=180) as stream:
    tar = tarfile.open(fileobj=stream, mode='r|gz')
    for m in tar:
        if not m.isfile():
            continue
        name = m.name.split('/', 1)[1]  # strip the repo-sha prefix
        for id, dirs in wanted.items():
            if any(name == d or name.startswith(d + '/') for d in dirs):
                target = root / id / name
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('wb') as f:
                    shutil.copyfileobj(tar.extractfile(m), f)
for p in pages:
    (root / p['id'] / '.revision').write_text(p['sha'])
    print(p['id'], p['sha'], flush=True)
