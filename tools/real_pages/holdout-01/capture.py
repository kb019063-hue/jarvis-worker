"""Capture exact source and TodoMVC's two lockfile-pinned runtime packages. Never run publisher code."""
import base64, hashlib, io, json, shutil, subprocess, sys, tarfile
from pathlib import Path
from urllib.request import urlopen

pages = json.loads(Path(__file__).with_name('manifest.json').read_text())['pages']
root = Path(sys.argv[1])
def extract(stream, dest):
    with tarfile.open(fileobj=stream, mode='r|gz') as tf:
        for m in tf:
            if m.isfile():
                path = dest / m.name.split('/', 1)[1]
                path.resolve().relative_to(dest.resolve())
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open('wb') as f:
                    shutil.copyfileobj(tf.extractfile(m), f)

for p in pages:
    id,repo,sha = (p[k] for k in ('id','repo','sha'))
    dest = root/id
    if (dest/'.revision').is_file() and (dest/'.revision').read_text()==sha: continue
    dest.mkdir(parents=True, exist_ok=False)
    if id in ('H1','H2'):
        with urlopen(f'https://codeload.github.com/{repo}/tar.gz/{sha}', timeout=90) as stream: extract(stream, dest)
    else:
        files = [p['path']]
        parent = Path(p['path']).parent
        if id=='H4': files += ['learn.json', parent/'package-lock.json', *(parent/'src'/f'{s}.js' for s in ('helpers','store','model','template','view','controller','app'))]
        for name in files:
            meta=json.loads(subprocess.check_output(['gh','api',f'repos/{repo}/contents/{name}?ref={sha}']))
            path=dest/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(base64.b64decode(meta['content']))
        if id=='H4':
            lock=json.loads((dest/parent/'package-lock.json').read_text())['packages']
            for name in ('todomvc-common','todomvc-app-css'):
                pkg=lock['node_modules/'+name]; url=pkg['resolved']
                assert url.startswith('https://registry.npmjs.org/')
                with urlopen(url,timeout=30) as f: data=f.read()
                alg,digest=pkg['integrity'].split('-',1)
                if base64.b64encode(hashlib.new(alg,data).digest()).decode()!=digest: raise ValueError('package digest mismatch')
                extract(io.BytesIO(data),dest/parent/'node_modules'/name)
    (dest/'.revision').write_text(sha)
    print(id,sha,flush=True)
