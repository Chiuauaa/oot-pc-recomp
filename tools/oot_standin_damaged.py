"""Find zstd files of a packed RomFS that a damaged dump left unreadable, and give the game stand-ins for them.

A dump that failed to read some blocks stores zeros there and records the SHA-256 of those zeros, so the extraction's
hash check passes. A .zs file whose zstd magic or one of whose 4 KB pages became zeros never decompresses, and Ocarina of
Time's loader then reopens it forever (one such file kept the Great Fairy Fountain loading for good). This tool streams
romfs.bin once (a few minutes on a spinning disk), lists those files, and writes a RomFS overlay (runtime/romfs.c,
manifest.tsv) in which each is replaced by an intact file of the same game:

  *_Nrm.bntx.zs   the game's flat normal map (Tex/Dummy_Flat_Normal_CmnTex_Chara_DetailMap_Nrm.bntx.zs)
  anything else   the intact file in the same folder and with the same suffix whose name shares the most at its start
                  and end

The picture is wrong where a stand-in is drawn (a flat floor, another bake), the game no longer hangs. A fresh dump of
the listed files is the real fix. The overlay is game data: write it under build/ (default: <build>/romfs-patches, which
the runtime loads without TOTK_ROMFS_PATCHES).

    python tools/oot_standin_damaged.py --romfs ../work/romfs-packed --out ../work/build/oot111/romfs-patches
"""
import argparse
import hashlib
import json
import os
import sys

MAGIC = b'\x28\xb5\x2f\xfd'
PAGE = 4096
FLAT_NORMAL = 'Tex/Dummy_Flat_Normal_CmnTex_Chara_DetailMap_Nrm.bntx.zs'


def damaged(romfs, quiet):
    files = json.load(open(os.path.join(romfs, 'index.json')))['files']
    ents = sorted((e['offset'], e['size'], e['path']) for e in files if e['path'].endswith('.zs'))
    zero = bytes(PAGE)
    bad = []
    with open(os.path.join(romfs, 'romfs.bin'), 'rb', buffering=0) as img:
        chunk, at, buf = 64 << 20, 0, b''
        for k, (off, size, path) in enumerate(ents):
            if off < at or off + size > at + len(buf):
                img.seek(off)
                at, buf = off, img.read(max(chunk, size))
            data = buf[off - at:off - at + size]
            a = (-off) % PAGE   # pages aligned in the image, as a failed block read leaves them
            holes = sum(data[p:p + PAGE] == zero for p in range(a, size - PAGE + 1, PAGE))
            if data[:4] != MAGIC or holes:
                bad.append(path)
            if not quiet and k % 20000 == 0:
                print(f'  {k}/{len(ents)} files checked, {len(bad)} damaged', flush=True)
    return files, bad


def standin(path, by_path, bad):
    if path.endswith('_Nrm.bntx.zs') and FLAT_NORMAL in by_path and FLAT_NORMAL not in bad:
        return FLAT_NORMAL
    folder, name = path.rsplit('/', 1) if '/' in path else ('', path)
    suffix = '.' + name.split('.', 1)[1] if '.' in name else ''
    best, best_n = None, -1
    for p in by_path:
        if p == path or p in bad or not p.endswith(suffix):
            continue
        f, n = p.rsplit('/', 1) if '/' in p else ('', p)
        if f != folder:
            continue
        # the shared start and end of the names: a bake of the next room (G_2_0_d0_t0 for G_2_1_d0_t0) before another
        # kind of map of the same room (G_2_1_d1_shgi0_uf3)
        common = len(os.path.commonprefix([name, n])) + len(os.path.commonprefix([name[::-1], n[::-1]]))
        if common > best_n:
            best, best_n = p, common
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--romfs', required=True, help='folder with romfs.bin and index.json')
    ap.add_argument('--out', required=True, help='overlay folder to write (under build/)')
    ap.add_argument('--damaged', nargs='+', metavar='PATH', help='these files, without the scan (paths as in index.json)')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    if a.damaged:
        files, bad = json.load(open(os.path.join(a.romfs, 'index.json')))['files'], a.damaged
    else:
        files, bad = damaged(a.romfs, a.quiet)
    by_path = {e['path']: e for e in files}
    if not bad:
        print('no damaged .zs file')
        return 0
    lines = []
    with open(os.path.join(a.romfs, 'romfs.bin'), 'rb') as img:
        def read(p):
            e = by_path[p]
            img.seek(e['offset'])
            return img.read(e['size'])
        for path in bad:
            src = standin(path, by_path, bad)
            if not src:
                print(f'{path}: damaged, no stand-in found (left as is)')
                continue
            data = read(src)
            dest = os.path.join(a.out, *path.split('/'))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, 'wb') as f:
                f.write(data)
            orig = hashlib.sha256(read(path)).hexdigest()
            lines.append(f'{path}\t{len(data)}\t{hashlib.sha256(data).hexdigest()}\t{orig}\n')
            print(f'{path}: damaged, stands in: {src}')
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, 'manifest.tsv'), 'w') as f:
        f.writelines(lines)
    print(f'{len(lines)} stand-ins written to {a.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
