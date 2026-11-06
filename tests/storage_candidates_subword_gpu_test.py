"""Functional GPU checks for guarded U8/S8/U16/S16 loads, using original instruction fixtures.

No game data or device addresses are used. The helper's descriptors reference owned buffers only.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess

import storage_candidates_fixture as fixture
import storage_candidates_gpu_test as gpu

ROOT = Path(__file__).resolve().parents[1]


def code(size, offset):
    ops = fixture.instructions(True)
    for i, op in enumerate(ops):
        if op >> 51 == 0xEED0000000000000 >> 51:
            ops[i] = (op & ~(7 << 48)) | (size << 48) | ((offset & 0xffffff) << 20)
    words = []
    for i in range(0, len(ops), 3):
        batch = ops[i:i + 3]
        words.extend([fixture.SCHED, *batch, *([0] * (3 - len(batch)))])
    return struct.pack('<' + 'Q' * len(words), *words)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translator', required=True, type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/tests/storage-subword-gpu')
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = out / 'runner.exe'
    subprocess.run(['C:/msys64/ucrt64/bin/clang.exe', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-I', 'C:/msys64/ucrt64/include', str(ROOT / 'tests/storage_candidates_gpu.c'),
                    'C:/msys64/ucrt64/lib/libvulkan-1.dll.a', '-o', str(runner)], check=True)
    cases, requests = [], []
    for carry in (False, True):
        for size in range(4):
            width = 1 if size < 2 else 2
            for offset in range(4):
                data = gpu.case_data('inline', carry)
                source = data['source_offset']
                struct.pack_into('<I', data['buffers'][0], gpu.GUARD + source, 0x80FF7F01)
                value = int.from_bytes(bytes.fromhex('017fff80')[offset:offset + width], 'little', signed=bool(size & 1))
                if width == 2 and offset & 1:
                    value = 0  # guarded ABI deliberately rejects non-natural halfword addresses
                key = f's{size}_lane{offset}_carry{int(carry)}'
                folder = out / key
                folder.mkdir(exist_ok=True)
                binary = folder / 'original.bin'
                binary.write_bytes(code(size, offset))
                data['expected_values'] = (value & 0xffffffff,)
                job = dict(key=key, stage='cs', code=str(binary), debug=False,
                           c1=(struct.pack('<Q', 32) + bytes(248)).hex(), local=[1, 1, 1], shared=0,
                           c0=[0, 0, 0, 0], c0_version=2, c0_roots=[8, 0, 0, 0, 0, 0, 0, 0],
                           sbuf_align=gpu.ALIGN, storage_candidates_version=1)
                cases.append((key, folder, data, job))
                requests.append(dict(job=job, out=str(folder)))
    # A byte at the final descriptor word is readable; addresses beyond the descriptor return zero.
    for extra, expected in ((gpu.POOL_BYTES - 33, 0x80), (gpu.POOL_BYTES - 32, 0)):
        key = 'boundary_' + str(extra)
        folder = out / key
        folder.mkdir(exist_ok=True)
        data = gpu.case_data('inline', False)
        data['buffers'][0][gpu.GUARD + gpu.POOL_BYTES - 1] = 0x80
        data['expected_values'] = (expected,)
        binary = folder / 'original.bin'
        binary.write_bytes(code(0, extra))
        job = dict(cases[0][3], key=key, code=str(binary))
        cases.append((key, folder, data, job))
        requests.append(dict(job=job, out=str(folder)))
    env = dict(os.environ)
    env['VK_LAYER_PATH'] = 'C:/msys64/ucrt64/bin'
    translated = subprocess.run([str(args.translator.resolve()), '--serve'], input=''.join(json.dumps(r) + '\n' for r in requests),
                                capture_output=True, text=True, env=env, timeout=120)
    (out / 'translation.log').write_text(translated.stdout + translated.stderr)
    gpu.require(translated.returncode == 0 and translated.stdout.splitlines() == ['ok ' + c[0] for c in cases], translated.stdout)
    if args.baseline:
        baseline_out = out / 'baseline'
        baseline_out.mkdir(exist_ok=True)
        request = dict(requests[0], out=str(baseline_out))
        old = subprocess.run([str(args.baseline.resolve()), '--serve'], input=json.dumps(request) + '\n', capture_output=True,
                             text=True, env=env, timeout=60)
        gpu.require(old.returncode == 0 and old.stdout.startswith('fail ') and 'global loads only' in old.stdout, old.stdout)
        (out / 'baseline.log').write_text(old.stdout + old.stderr)
    report = dict(gpu_requested=args.run, cases=[])
    for key, folder, data, job in cases:
        spv = folder / (key + '.spv')
        subprocess.run(['C:/msys64/ucrt64/bin/spirv-val.exe', '--target-env', 'vulkan1.3', str(spv)], check=True)
        reflection = json.loads((folder / (key + '.res.json')).read_text())
        packet = folder / 'packet.bin'
        gpu.packet(packet, reflection, data, gpu.spirv_requirements(spv), 1)
        row = dict(key=key, expected=data['expected_values'][0])
        if args.run:
            readback = folder / 'readback.bin'
            run = subprocess.run([str(runner), str(spv), str(packet), str(readback), '', '1'],
                                 capture_output=True, text=True, timeout=90, env=env)
            (folder / 'stdout.log').write_text(run.stdout)
            (folder / 'stderr.log').write_text(run.stderr)
            device = [json.loads(l) for l in run.stdout.splitlines() if l.startswith('{')]
            check = gpu.compare_readback(readback, data['buffers'], data['expected_values']) if readback.exists() else {}
            row.update(exit=run.returncode, readback=check, device=device[-1] if device else None)
            row['passed'] = bool(run.returncode == 0 and check.get('semantic_pass') and device and
                                 device[-1].get('robust_buffer_access_enabled') and device[-1].get('validation_errors') == 0)
        report['cases'].append(row)
        (out / 'manifest.json').write_text(json.dumps(report, indent=2))
        if args.run:
            gpu.require(row['passed'], f'{key}: GPU semantic/validation failure; see {folder}')
    print(json.dumps(dict(prepared=len(cases), gpu_executed=args.run,
                         failures=sum(not r['passed'] for r in report['cases']) if args.run else None)))


if __name__ == '__main__':
    main()
