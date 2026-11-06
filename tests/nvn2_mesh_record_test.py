"""Exercise production mesh recorders with polluted unused CPU registers."""
import argparse
from pathlib import Path
import subprocess

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/clang.exe')
    args = parser.parse_args()
    production = (ROOT / 'runtime/nvn_api_cmd.c').read_text()
    packet = next(line for line in (ROOT / 'runtime/nvn_api.h').read_text().splitlines()
                  if line.startswith('typedef struct { uint64_t arg[3];'))
    source = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint64_t x[31]; } Cpu;
''' + packet + r'''
static p_draw_mesh recorded;
static unsigned notes, records;
static const char *last_note;
enum { OP_DRAW_MESH = 123 };
#define NVN_API(name) static void name(Cpu *c)
static uint64_t arg_x(const Cpu *c, unsigned i) { return c->x[i]; }
static void *cmd_of(const Cpu *c) { return (void *)(uintptr_t)c->x[0]; }
static void *record(void *cmd, unsigned op) {
    assert(cmd == (void *)(uintptr_t)0x1234 && op == OP_DRAW_MESH);
    memset(&recorded, 0xa5, sizeof recorded);
    records++;
    return &recorded;
}
#define REC(cmd, op, type) ((type *)record(cmd, op))
static void nvn2_mesh_note(const char *name, const Cpu *c) {
    assert(c->x[0] == 0x1234);
    last_note = name;
    notes++;
}
''' + function(production, 'NVN_API(nvnCommandBufferDrawMeshTasks)') + '\n' + function(
        production, 'NVN_API(nvnCommandBufferDrawMeshTasksIndirect)') + r'''
int main(void) {
    _Static_assert(sizeof(p_draw_mesh) == 40, "packet size stays compatible");
    _Static_assert(offsetof(p_draw_mesh, caller) == 32, "caller offset stays compatible");
    Cpu c = {.x = {0x1234, UINT64_C(0xfeedface00000004), UINT64_C(0xdeadbeef0000000c),
                   UINT64_C(0x777788889999aaaa)}};
    c.x[30] = UINT64_C(0x7000000040);
    Cpu before = c;
    nvnCommandBufferDrawMeshTasks(&c);
    assert(recorded.arg[0] == 4 && recorded.arg[1] == 12 && recorded.arg[2] == 0);
    assert(!recorded.indirect && !recorded.pad && recorded.caller == c.x[30]);
    assert(!strcmp(last_note, "DrawMeshTasks") && !memcmp(&c, &before, sizeof c));
    c.x[2] = 0;
    nvnCommandBufferDrawMeshTasks(&c);
    assert(recorded.arg[0] == 4 && !recorded.arg[1] && !recorded.arg[2]);
    c.x[1] = UINT64_C(0x8000123400);
    c.x[2] = UINT64_C(0xffffeeee11112222);
    before = c;
    nvnCommandBufferDrawMeshTasksIndirect(&c);
    assert(recorded.arg[0] == UINT64_C(0x8000123400) && !recorded.arg[1] && !recorded.arg[2]);
    assert(recorded.indirect == 1 && !recorded.pad && recorded.caller == c.x[30]);
    assert(!strcmp(last_note, "DrawMeshTasksIndirect") && !memcmp(&c, &before, sizeof c));
    assert(records == 3 && notes == 3);
    puts("Production mesh recorders: direct ABI, indirect ABI and packet compatibility passed");
}
'''
    out = ROOT / 'build/test/nvn2_mesh_record'
    out.mkdir(parents=True, exist_ok=True)
    src = out / 'mesh_record.c'
    src.write_text(source)
    for optimization in ('-O0', '-O2'):
        exe = out / ('mesh_record' + optimization[-1] + '.exe')
        subprocess.run([args.cc, '-std=c11', optimization, '-Wall', '-Wextra', '-Werror',
                        str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
