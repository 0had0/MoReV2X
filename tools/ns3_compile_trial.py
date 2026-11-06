"""Type-check MoReV2X sources against a newer ns-3. Executes nothing from either tree.

Used for docs/notebooks/06_ns3_migration.ipynb. Set NS3_TRIAL_DIR to a folder with:
  ns349/    sparse checkout of ns-3.49, the /src/**/*.h files only
  carlos/   git archive of fork-carlosrisma/main, for the "carlos" trial
3.42 headers are read from the built groupcast tree. Run: ns3_compile_trial.py c342|c349|carlos
"""
import collections, os, re, subprocess, sys, shutil, glob
from concurrent.futures import ThreadPoolExecutor
S = os.environ.get('NS3_TRIAL_DIR', os.path.dirname(os.path.abspath(__file__)))
R = '/Users/hadih/Documents/MoReV2X-hadih'
G = '/Users/hadih/Documents/groupcast-adaptation-ns3/ns-3-dev'

def hdrs_349(inc):
    src = S + '/ns349/src'
    for path in glob.glob(src + '/*/**/*.h', recursive=True):
        if '/test/' in path or '/examples/' in path: continue
        link = inc + '/' + os.path.basename(path)
        if not os.path.lexists(link): os.symlink(path, link)
    for mod in os.listdir(src):
        heads = sorted(os.path.basename(p) for p in glob.glob(f'{src}/{mod}/*/*.h') if '/test/' not in p and '/examples/' not in p and 'win32' not in p and '-module' not in p)
        with open(f'{inc}/{mod}-module.h', 'w') as f:
            f.writelines(f'#include "{h}"\n' for h in heads)
    for mod in os.listdir(src):
        macro = mod.replace('-', '_').upper() + '_EXPORT'
        with open(f'{inc}/{mod}-export.h', 'w') as f:
            f.write(f'#define {macro}\n#define NS3_{mod.replace("-", "_").upper()}_DEPRECATED\n')
    for gen in ('core-config.h', 'config-store-config.h', 'version-defines.h'):
        p = G + '/build/include/ns3/' + gen
        if os.path.exists(p): shutil.copy(p, inc)

def trial(name, base, srcdir, scratch, std):
    out = f'{S}/{name}'
    shutil.rmtree(out, ignore_errors=True)
    inc = out + '/inc/ns3'; os.makedirs(inc)
    for h in glob.glob(srcdir + '/**/*.h', recursive=True) + glob.glob(R + '/src/traci/*/*.h'):
        shutil.copy(h, inc)
    open(inc + '/MoReV2X-module.h', 'w').write(''.join(f'#include "{os.path.basename(h)}"\n' for h in glob.glob(srcdir + '/*/*.h')))
    includes = ['-I', out + '/inc']
    if base == '349':
        os.makedirs(out + '/base/ns3'); hdrs_349(out + '/base/ns3'); includes += ['-I', out + '/base']
    else:
        includes += ['-I', G + '/build/include']
    built = set(re.findall(r"'((?:model|helper)/[^']+\.cc)'", open(srcdir + '/wscript', encoding='latin-1').read()))
    files = sorted(srcdir + '/' + b for b in built) + [scratch] + sorted(glob.glob(R + '/src/traci/model/*.cc') + glob.glob(R + '/src/traci/helper/*.cc'))
    def one(f):
        cmd = ['/usr/bin/clang++', '-std=' + std, '-fsyntax-only', '-w', '-ferror-limit=0', '-DNS3_LOG_ENABLE', '-DNS3_ASSERT_ENABLE',
               '-I', os.path.dirname(f), '-I/opt/local/include'] + includes + [f]
        p = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=500)
        return f, [l for l in p.stderr.splitlines() if ' error: ' in l or l.startswith('fatal')]
    with ThreadPoolExecutor(5) as pool:
        results = list(pool.map(one, files))
    total = sum(len(e) for _, e in results); bad = [(f, e) for f, e in results if e]
    print(f'== {name}: {len(files)} files, {len(bad)} with errors, {total} errors')
    msgs = collections.Counter(); perfile = {}
    for f, errs in results:
        perfile[os.path.basename(f)] = len(errs)
        for e in errs:
            m = e.split(' error: ', 1)[-1]
            m = re.sub(r"'[^']*'", 'X', m) if 0 else m
            msgs[m[:115]] += 1
    open(out + '/errors.txt', 'w').write('\n'.join(l for _, e in results for l in e))
    print(' worst files:', sorted(((n, f) for f, n in perfile.items() if n), reverse=True)[:12])
    for m, n in msgs.most_common(40): print(f'{n:5d}  {m}')
    return perfile
import json
which = sys.argv[1]
if which == 'carlos':
    trial('carlos349', '349', S + '/carlos/src/morev2x', S + '/carlos/src/morev2x/examples/HIGHWAY.cc', 'c++23')
if which == 'c342': trial('ours342', '342', R + '/src/MoReV2X', R + '/scratch/HIGHWAY.cc', 'c++20')
if which == 'c349': trial('ours349', '349', R + '/src/MoReV2X', R + '/scratch/HIGHWAY.cc', 'c++23')
