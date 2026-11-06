#!/usr/bin/env python3
"""The input pipeline's helpers (meshsim WORKER27). Standard library only.

  merge ROOT                     ROOT/manifest.json listing every ROOT/<p>.manifest.json's programs (as tasks 103/104)
  verify-delivery ROOT PROGRAM   every file of every window in ROOT/PROGRAM.manifest.json, size and SHA-256, against
                                 ROOT/PROGRAM/ (links followed); prints the count; exit 1 on any difference
  canon-converted FILE           ConvertDelivery's "converted" object (from a Map.of, its key order changes from one JVM
                                 run to the next) rewritten with "by" first; prints the file's SHA-256 in both orders,
                                 the canonical one first
  jsoneq A B KEY...              A and B equal as JSON objects with the top-level KEYs left out (paths)
  windows-ok MANIFEST            every window's "replay" is "ok"; prints "N windows, N replayed ok"
"""
import hashlib, json, glob, os, re, sys


def sha256_file(path, buf=1 << 20):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            b = f.read(buf)
            if not b:
                return h.hexdigest()
            h.update(b)


def merge(root):
    progs, base = [], None
    for f in sorted(glob.glob(os.path.join(root, '*.manifest.json'))):
        m = json.load(open(f)); progs += m['programs']; base = m
    if base is None:
        sys.exit('no <program>.manifest.json in ' + root)
    base['programs'] = progs
    tmp = os.path.join(root, 'manifest.json.tmp')
    json.dump(base, open(tmp, 'w'), indent=1)
    os.replace(tmp, os.path.join(root, 'manifest.json'))
    print('root manifest:', [p['name'] for p in progs])


def verify_delivery(root, prog):
    m = json.load(open(os.path.join(root, prog + '.manifest.json')))
    n = bad = 0
    for p in m['programs']:
        if p['name'] != prog:
            continue
        for w in p['window_entries']:
            for name, e in w.get('files', {}).items():
                want = e['sha256'] if isinstance(e, dict) else e
                f = os.path.join(root, prog, 'window_%05d.%s' % (w['index'], name))
                n += 1
                if not os.path.exists(f):
                    print('MISSING', f); bad += 1; continue
                if isinstance(e, dict) and 'bytes' in e and os.path.getsize(f) != e['bytes']:
                    print('SIZE', f, os.path.getsize(f), e['bytes']); bad += 1; continue
                got = sha256_file(f)
                if got != want:
                    print('SHA256', f, got, want); bad += 1
    print('%s: %d files checked against %s.manifest.json, %d differ' % (prog, n, prog, bad))
    sys.exit(1 if bad or n == 0 else 0)


CONV = re.compile(rb'(\n *"converted": )\{"from_manifest_sha256": ("[0-9a-f]{64}"), "by": ("(?:[^"\\]|\\.)*")\}')
CONV_BY = re.compile(rb'(\n *"converted": )\{"by": ("(?:[^"\\]|\\.)*"), "from_manifest_sha256": ("[0-9a-f]{64}")\}')


def canon_converted(path):
    data = open(path, 'rb').read()
    canon = CONV.sub(lambda m: m.group(1) + b'{"by": ' + m.group(3) + b', "from_manifest_sha256": ' + m.group(2) + b'}', data)
    if not CONV_BY.search(canon):
        sys.exit('canon-converted: no "converted" object found in ' + path)
    other = CONV_BY.sub(lambda m: m.group(1) + b'{"from_manifest_sha256": ' + m.group(3) + b', "by": ' + m.group(2) + b'}', canon)
    if canon != data:
        tmp = path + '.tmp'
        open(tmp, 'wb').write(canon); os.replace(tmp, path)
    print(hashlib.sha256(canon).hexdigest(), hashlib.sha256(other).hexdigest())


def jsoneq(a, b, keys):
    ja, jb = json.load(open(a)), json.load(open(b))
    for k in keys:
        ja.pop(k, None); jb.pop(k, None)
    if ja == jb:
        print('equal without', keys); return
    for k in sorted(set(ja) | set(jb)):
        if ja.get(k) != jb.get(k):
            print('differs at', k)
    sys.exit(1)


def windows_ok(path):
    W = json.load(open(path))['programs'][0]['window_entries']
    n = sum(w['replay'] == 'ok' for w in W)
    print(len(W), 'windows,', n, 'replayed ok')
    sys.exit(0 if n == len(W) and n > 0 else 1)


if __name__ == '__main__':
    cmd, args = sys.argv[1], sys.argv[2:]
    {'merge': lambda: merge(*args), 'verify-delivery': lambda: verify_delivery(*args),
     'canon-converted': lambda: canon_converted(*args), 'jsoneq': lambda: jsoneq(args[0], args[1], args[2:]),
     'windows-ok': lambda: windows_ok(*args)}[cmd]()
