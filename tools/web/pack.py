#!/usr/bin/env python3
"""Packs the files the browser build needs (the original .dat files and data/*.json) for web/static/packs.js.

    pack.py OUT_DIR [extra files hashed into the version: the .js, .wasm, pages...]

Writes OUT_DIR/files/<name>.<hash>.pack and OUT_DIR/version.json. One pack per folder (game, data), cut at
about 1 MB. A pack's name carries a hash of its bytes: an unchanged pack keeps its name and browsers that
already have it don't download it again. Only the data files the engine reads are packed (no .exe, .com or .bat).

Pack format: b"DFPK", uint32 LE length of the JSON index, the index ([[path, offset, size], ...], offsets from
the end of the index), then the files' bytes.
"""
import hashlib
import json
import os
import sys
import time

ROOT = os.environ.get('DOF_ROOT') or os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))   # (install.py points DOF_ROOT at its staging folder)
LIMIT = 1 << 20


def files_under(folder, mount, keep):
    out = []
    base = os.path.join(ROOT, folder)
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames.sort()
        for f in sorted(filenames):
            if not keep(f):
                continue
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, base).replace(os.sep, '/')
            out.append((mount + '/' + rel, full))
    return out


def make_pack(entries):
    index, blobs, at = [], [], 0
    for path, full in entries:
        data = open(full, 'rb').read()
        index.append([path, at, len(data)])
        blobs.append(data)
        at += len(data)
    head = json.dumps(index, separators=(',', ':')).encode()
    return b'DFPK' + len(head).to_bytes(4, 'little') + head + b''.join(blobs)


def main():
    out, extras = sys.argv[1], sys.argv[2:]
    files_dir = os.path.join(out, 'files')
    os.makedirs(files_dir, exist_ok=True)
    for f in os.listdir(files_dir):
        os.remove(os.path.join(files_dir, f))
    groups = {
        'game': files_under('game', '/game', lambda f: f.endswith('.dat')),
        'data': files_under('data', '/data', lambda f: f.endswith('.json')),
    }
    chunks = []
    for name in sorted(groups):
        chunk, size, n = [], 0, 0
        for e in groups[name]:
            s = os.path.getsize(e[1])
            if chunk and size + s > LIMIT:
                chunks.append((f'{name}-{n}', chunk))
                chunk, size, n = [], 0, n + 1
            chunk.append(e)
            size += s
        if chunk:
            chunks.append((f'{name}-{n}' if n else name, chunk))
    packs, version = [], hashlib.sha1()
    for name, entries in chunks:
        data = make_pack(entries)
        h = hashlib.sha1(data).hexdigest()[:12]
        file = f'{name}.{h}.pack'
        with open(os.path.join(files_dir, file), 'wb') as f:
            f.write(data)
        packs.append({'name': name, 'file': file, 'size': len(data), 'late': False})
        version.update(h.encode())
    for e in extras:
        version.update(hashlib.sha1(open(e, 'rb').read()).digest())
    notes = []
    changes = os.path.join(ROOT, 'web', 'CHANGES.txt')
    if os.path.exists(changes):
        notes = [l.strip() for l in open(changes, encoding='utf-8') if l.strip() and not l.startswith('#')][:20]
    m = {'version': version.hexdigest()[:12], 'built': time.strftime('%Y-%m-%d %H:%M'), 'packs': packs, 'notes': notes}
    for e in extras:
        if e.endswith('.wasm'):
            m['wasm_size'] = os.path.getsize(e)
    with open(os.path.join(out, 'version.json'), 'w') as f:
        json.dump(m, f, indent=1)
    total = sum(p['size'] for p in packs)
    print(f'packs: {len(packs)} ({total / 1e6:.2f} MB), version {m["version"]}')


if __name__ == '__main__':
    main()
