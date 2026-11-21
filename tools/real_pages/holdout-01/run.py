"""Usage: run.py CAPTURE_ROOT FRESH_ORACLE_DIRECTORY NATIVE_DRIVER"""
import json, subprocess, sys, time
from pathlib import Path
here=Path(__file__).resolve().parent
root,out,binary=map(Path,sys.argv[1:]); spec=json.loads((here/'manifest.json').read_text()); steps=json.loads((here/'steps.json').read_text()); helper=(here/'observe.js').read_text().replace('\n',' ')
ref={r['id']:r for r in json.loads((out/'reference.json').read_text())['rows']};rows=[]
for p in spec['pages']:
    tid=p['id'];row={'id':tid,'pass':False,'trace':[]};rows.append(row)
    assert (root/tid/'.revision').read_text()==p['sha']
    for f in out.glob(tid+'-lite-*'): f.unlink()
    if not ref[tid]['pass']:
        row['failure']='reference not validated';continue
    commands=[];checks=[]
    for op,arg,*rest in steps[tid]:
        if op=='read':
            expr,want=rest;checks.append((arg,want));commands.append(f'read\t(()=>{{{helper}return String({expr})}})()')
        else:commands.append(op+'\t'+arg)
    start=time.monotonic(); timed_out=False
    try:
        proc=subprocess.run([binary.resolve(),(root/tid).resolve(),p['path'],out/(tid+'-lite-')],input=('\n'.join(commands)+'\n').encode(),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=spec['native_wall_seconds']);log=proc.stdout;row['exit']=proc.returncode
    except subprocess.TimeoutExpired as e:
        log=e.stdout or b'';timed_out=True
    (out/(tid+'.log')).write_bytes(log);row['seconds']=round(time.monotonic()-start,2);errors=[];idx=0;ended=False
    for line in log.decode(errors='replace').splitlines():
        if line.startswith('READ\t'):
            label,want=checks[idx];idx+=1;got=line[5:];row['trace'].append(dict(check=label,want=want,got=got));
            if got!=want:errors.append(f'{label}: {got!r}, wanted {want!r}')
        elif line.startswith('HIT\t'):row['trace'].append(dict(hit=line[4:]))
        elif line.startswith('BLOCKED\t'):errors.append(line[8:])
        elif line=='END':ended=True
    if timed_out:errors.append('45s wall timeout')
    elif row['exit']!=0:errors.append(f"process exit {row['exit']}")
    if idx!=len(checks) or not ended:errors.append('incomplete trace')
    row['failures']=errors;row['pass']=not errors;print(tid,row['pass'],errors,flush=True)
(out/'litebird.json').write_text(json.dumps(rows,indent=2)+'\n')
