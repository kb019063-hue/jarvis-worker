"""Fetch the frozen published trees into an untracked directory (Python 3)."""
import json
from pathlib import Path
import sys
import shutil
import tarfile
from urllib.request import urlopen

root = Path(sys.argv[1])
sources = json.loads(Path(__file__).with_name('pages.json').read_text())['sources']
for key, (repo, sha) in sources.items():
    dest = root / key
    if (dest / '.revision').is_file() and (dest / '.revision').read_text() == sha:
        continue
    if dest.exists():
        sys.exit(f'{dest}: stale or incomplete capture; remove before retrying')
    dest.mkdir(parents=True)
    with urlopen(f'https://codeload.github.com/{repo}/tar.gz/{sha}', timeout=90) as stream:
        with tarfile.open(fileobj=stream, mode='r|gz') as tar:
            for member in tar:
                if member.isfile():
                    member.name = member.name.split('/', 1)[1]
                    target = (dest / member.name).resolve()
                    target.relative_to(dest.resolve())
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with target.open('wb') as output:
                        shutil.copyfileobj(tar.extractfile(member), output)
    (dest / '.revision').write_text(sha)
