# Ocarina of Time rendering investigation

## Scope and current evidence

This work targets Ocarina of Time 1.1.1 on the `oot-port` branch. The menu renders and AV1 video plays, but the user reports a black 3D scene after the opening. Successful compilation and menu rendering do not establish that the 3D renderer works.

The initial live log contained 85 Ampere lowering failures, including 64 reports of `native tessellation stage buffer read mode`, and repeated downstream translation errors. These are log event counts, not counts of unique required shaders. The process continued running without a fatal error. A first offline capture contained 2,837 stages; 2,811 lowered successfully with the existing code. Reproduction subsequently captured a frozen corpus of 6,413 complete stages for the comparison below.

`runtime/gfx_shader.c`, `req_init`, rejects an unconverted NVN 2 control block with `no translator yet`. This message does not mean the translator executable is absent: `runtime/nvn_api_obj.c`, `nvnProgramSetShaders`, first attempts `nvn2_lower`, which can reject the native stage. A separate observed translator rejection concerns storage-candidate atomics limited to 32-bit integer operations.

All native shader captures, translated shader files, logs and replay packets remain local under ignored `build/oot-fix/`. They must not be committed or attached to public reports.

## Upstream branch audit

### Performance comparison in the shared build directory

The executable comparison between `oot-storage-candidates.exe` and the first
ISBEWR build confines changed machine code to the Ampere lowering object.
The former must not be treated as an unmodified principal-branch executable.
The principal branch is `origin/oot-port`. Its memory synchronization,
pipeline-cache and presentation sources are identical to this fix branch.

A controlled runtime comparison uses the principal runtime source snapshot,
the same generated game C, current translator and `build/v111/shader-cache`.
The current `guest_jump.c` and header are retained solely for compatibility
with that generated C. This is a runtime isolation experiment, not a complete
principal-branch build. In the 3D scene it records 15.3 FPS, about 62 ms replay
per frame, without material shader-translation or pipeline-build waits. Thus
the latest ISBEWR fix alone does not explain the slow result. A comparison
with the preserved older translator remains in progress. Logs are private
under `build/oot-fix/performance-compare/`.

The frozen native corpus accepts 6,339 of 6,413 stages before and after the
ISBEWR change. Seven accepted outputs change, all with SPH type 1; no accepted
stage is lost. Acceptance does not establish their Vulkan behavior.

On the user's Ryzen 9 9950X3D, builds should use 32 jobs (16 cores, 32 logical
processors). The comparison's initial 24-job compile took 197.9 seconds and
peaked at about 7 GiB across compiler processes. Its tail consisted of single
large translation units; increasing job count cannot parallelize an individual
Clang process. The subsequent incremental link with 32 jobs took 0.8 seconds.

### Follow-up: ISBEWR register shadowing

Lichii commit `b328477840f24da9238b8ea3c17747e01b9b2a73` reports that
an Ocarina mesh shader wrote a high register through ISBEWR without shadowing
it, producing a NaN at Link's house door that bloom amplified into white glow.
The same omission existed in our lowering and the upstream correction is now
applied. The build succeeds; its effect on this port still needs visual
verification. The user confirmed that the earlier build reached the 3D scene,
but has reported excessive brightness there.

The newer `fix/oot-fog` tip `8390649c0c0699192b35f486f3aac6d9cf88c8b3`
also changes per-tap gather proof for LEA instructions in an upscaler shader.
Our proof differs from upstream and needs a separate semantic comparison.

Repository: [zortos293/Lichii](https://github.com/zortos293/Lichii). Snapshot taken on 2026-11-05. Baseline: `724aede4ea51fae8224cdd02549cdbdf7ceecd25`, recorded by this checkout's `runtime/third_party/eglantine/UPSTREAM_COMMIT` before this investigation.

All 82 advertised branch tips were fetched and screened by commit identity, changed paths and Ampere directory tree identity. They contain 27 distinct Ampere source trees. The complete branch-tip inventory is in [oot-upstream-branches.csv](oot-upstream-branches.csv). This is a source inventory, not a claim that every branch was built or tested. Shallow history is insufficient to establish that a change has been merged; file comparisons are used instead.

Only changes relevant to Ocarina's observed failures are candidates for this port. Branches for unrelated games, packaging and emulator scheduling are outside the implementation scope unless a shared change can be tied to an Ocarina failure.

The 27 distinct lowering header snapshots were also screened for opcode cases absent from the baseline. None adds an explicit `0xc08` handler, one of the remaining local failures. This does not replace end-to-end testing or establish equivalence of their control-flow proofs.

| Branch | Pinned tip | Finding and decision |
| --- | --- | --- |
| `fix/oot-fog` | `82b95a65d46527f0cfad3d5661439228897fee49` | Contains newer per-tap gather offset proofs, half gathers, depth-comparison gathers, constant-buffer F2I modifiers, U32 surface reduction operations and LEA.HI base negation. The gather proofs directly match failures in the local log. Evaluate the lowering update first. Its Vulkan draw probes are diagnostic code for Lichii's renderer, not drop-in code for this runtime. |
| `fix/oot-bloom` | `6c1dd4e92c72a95b6abc09eaab1d5a74e90db795` | Ampere tree matches the baseline. Its texture reinterpretation tree differs from baseline by removing the combined depth/stencil aspect and destination-layout fixes. Do not replace our renderer wholesale with this snapshot or infer a bloom fix merely from the branch name. |
| `fix/oot-boot` | `a7f2ee336c8eafcf31f242c16b8e024ca74ac7b2` | Its lowering snapshot lacks several features present at our baseline. Review individual boot changes if an Ocarina service failure is reproduced; replacing the lowering would lose existing support. |
| `perf/oot-fast-draw` | `5620b58d7dcbd5a3ccb163fafc28cda296bf73fc` | Contains extensive emulator draw batching, dirty-memory tracking and parallel recording. Correct rendering must precede performance work. These systems do not map directly onto our NVN command replay. |
| `fix/shader-sweep`, `fix/shader-sweep-all-games`, integration/release branches | See inventory | Screened as possible carriers of shared Ocarina fixes. They are not supersets of `fix/oot-fog`: tree comparisons also remove newer lowering support. Use individual proven changes rather than merging branch tips. |

Relevant upstream source: Lichii `src/shader_recompiler/frontend/ampere/lower_to_maxwell.h`, `LowerToMaxwellWith`; tests in `src/tests/video_core/ampere_ported_textures.cpp`, `ampere_logged_textures.cpp`, `ampere_compute_lowering.cpp` and `ampere_ported_conversions.cpp`. Backend references include Lichii `TextureCacheRuntime::ReinterpretImage` in `src/video_core/renderer_vulkan/vk_texture_cache.cpp`.

## Implementation plan

1. Preserve the failing log and capture native shaders plus failed translation requests. Freeze one corpus so every comparison uses the same inputs.
2. Finish the branch inventory and inspect Ocarina-relevant deltas before selecting changes. Record exact source commits and exclusions.
3. Import selected Ocarina lowering corrections from `fix/oot-fog`, retaining upstream licensing and updating provenance. Test the upstream guarded proof against our existing local prototype before replacing either control-flow analysis. Do not import a complete header snapshot if it loses supported shaders.
4. Run offline lowering before and after on the frozen corpus. Require that previously accepted stages remain accepted. Check representative positive and negative gather/operand cases; then replay newly accepted stages through our actual Ryujinx-based shader translator. Lowering success alone is insufficient.
5. Rebuild the runtime and reproduce the transition from the rendered menu to the 3D scene. Verify that changed lowering output has a distinct shader input hash; preserve baseline logs rather than indiscriminately clearing caches.
6. Implement the confirmed missing mesh draw path described below, then investigate remaining blockers separately: stage-buffer semantics, register pressure, unsupported native instructions and storage-candidate atomic translation. Use the reference implementation's semantics; do not silence rejection checks or replace shaders with empty programs.
7. Report the observed runtime result, remaining failures and a reproducible build command. Do not mark 3D rendering fixed without observing the scene.

## Build identity

Ocarina must use `--version oot111`, not `--version v111`. The latter selects TotK-specific constructor counts and timing. Existing generated code and output can be reused explicitly:

```powershell
.venv\Scripts\python tools\build_game.py --version oot111 --gen generated\v111 --out build\v111 --expected tools\data\expected-oot111.json --exefs build\extracted\v111\exefs --romfs build\extracted\v111\romfs --jobs 24
```

The executable remains named `build/v111/dkb.exe` by the inherited build script.

## First implemented corrections and validation

The complete `fix/oot-fog` header was evaluated offline before selecting changes. It accepted three previously rejected stages but rejected 18 previously accepted stages. That wholesale import was not retained.

The selected update retains this port's existing control-flow proof and incorporates Lichii's matching-predicate IMAD offset proof, unconditional literal materialization for downstream constant propagation, depth-comparison gathers, F2I constant-buffer modifiers, surface reduction operation preservation and LEA.HI base negation. A local adaptation distinguishes UIMAD destinations in the uniform register bank from GPR destinations and recognizes that outgoing branches do not write GPRs. Predicate changes, incoming branch targets and nonconstant offset writes remain rejection conditions.

| Check | Result |
| --- | --- |
| Frozen native corpus | 6,413 stages |
| Lowered before changes | 6,328 |
| Lowered after selected changes | 6,333 |
| Previously accepted stages lost | 0 |
| Newly accepted stages | 5: four compute, one fragment |
| Previously accepted stages with changed output | 12 |
| New and changed stages through this port's actual `totk_xlate` | 17 succeeded, 0 failed |
| Synthetic operand, predicate, control-flow and encoding checks | 122 passed |
| Existing shader-control/profile guards at `-O0` and `-O2` | 45 checks passed at each optimization level |
| Windows runtime rebuild | Compiled and linked successfully |
| Relaunch smoke check | SDK initialization and AV1 decoding completed; process remained responsive; no fatal error observed |

The synthetic tests in `tests/oot_ampere_lowering.cpp` construct artificial instructions; they contain no captured game shader code. They check descriptor banks, comparison coordinates, depth-enable encoding, guarded literal offsets, offset range rejection, predicate clobbers, separate uniform/GPR banks, incoming control edges, F2I modifiers, LEA.HI negation and surface atomic operations. Build them with MSYS2 UCRT64 Clang, keeping the executable under `build/`.

```powershell
& 'C:\msys64\ucrt64\bin\clang++.exe' -std=c++20 -O1 -fuse-ld=lld -static -I runtime/third_party/eglantine tests/oot_ampere_lowering.cpp -o build/oot-fix/lowering-check.exe
& .\build\oot-fix\lowering-check.exe
.venv\Scripts\python tests\nvn2_shader_control_test.py --cc C:\msys64\ucrt64\bin\clang.exe
```

The runtime was rebuilt with the Ocarina build command above and reopened. A visible 3D scene has not been verified after this first batch, and the mesh draw no-ops remain in place. Baseline binaries and logs are retained under `build/oot-fix/runtime-before/`; native-corpus comparisons and standalone translation outputs are under `build/oot-fix/`.

Native lowering and standalone SPIR-V generation do not prove correct rendering or correct bindings in a live draw. The remaining 80 rejected stages include 64 stage-buffer cases, five uniform-load cases, five unsupported-opcode cases, five register-pressure cases and one call-linkage case. These results must not be described as a complete 3D fix.

## Red and black output: live frame investigation

The user screenshot and a reproduced live capture show conventional geometry, with
red and black output. Missing mesh draws alone do not explain this symptom. A private
capture of frame 11117 contains 184 passes/dispatches. Pass 178 still has a multicolour
1280x720 R11G11B10F scene; compute pass 181 turns its output into red-only values,
including NaNs. Its storage binding requests NVN format `0xb` (R32_UINT) over an
R11G11B10F image. `runtime/vk_exec_draw.c`, `get_view`, previously ignored that format
override and bound the floating-point base format instead.

The implementation now resolves compatible uncompressed colour format overrides,
separates sampled and storage views in the view cache, and restricts each view's usage.
RGB9E5 images gain extended storage usage through a compatible R32_UINT view, following
the approach in Lichii `vk_texture_cache.cpp`, `MakeImage`. This is needed because the
RTX 5080 reports RGB9E5 sampling support but no direct storage-image support.

`answers_for` also now supplies the effective storage image format to the translator.
Previously it supplied dimensions alone, causing unknown integer formats to default
to RGBA8 UNORM. The existing shader key includes these answers, so changed bindings
produce distinct translations. Synthetic compatibility regressions and the Windows
rebuild pass. Subsequent gameplay captures show that these changes alone do not
repair the final image; the following sections record the remaining failures.

The first attempt to load Continue stopped at guest PC `main+0x1661690`. The cause and
repair are recorded below. Raw frames, logs and SPIR-V captures remain private under
`build/oot-fix/red-output/`. Coloured title backgrounds and the introductory movie are
video playback, not evidence of working scene rendering.

## Continue crash: guest setjmp and longjmp

The failing PC is a return address inside the function beginning at `main+0x1661670`.
Its call to PLT stub `main+0x3c44140` resolves through GOT offset `0x4cc0268` to the
imported symbol `setjmp`. A later `longjmp` restores that return address. Ordinary
indirect dispatch only accepts function entries, so dispatching the saved return PC
fails. Treating it as another function start would also fail to unwind the host stack.

`recomp/gen.py`, `Emitter`, now identifies setjmp PLT stubs from symbol relocations and
the AArch64 stub instructions. A direct caller establishes a native continuation in
its own host frame. These callers retain registers and flags in `Cpu`, avoiding C's
indeterminate modified automatic locals after native longjmp. Cleanup removes their
registered continuations on ordinary return. Unresolved computed-branch functions
with setjmp are rejected explicitly until they can share a continuation scope.

`runtime/guest_jump.c` and `runtime/guest_jump.h` manage active continuations and an
override for imported `longjmp`. They restore the saved Cpu, normalize a zero return
value to one, restore host-call tracking depth, and discard contexts of unwound frames.
Contexts also match Cpu because multiple guest fibers can share a native thread.
The guest SDK setjmp still executes on the initial path and populates its guest buffer.
This implementation covers recognized direct PLT calls; arbitrary indirect setjmp
calls are not yet intercepted and must not be described as supported.

Validation so far:

- The synthetic `tests/guest_jump_test.c` passes at `-O2`, including nested unwinding,
  register/SP/SIMD restoration, zero-to-one conversion, negative values and repeated calls.
- Instruction differential tests: 60,000 encodings, 179,931 cases, zero failures.
- SIMD differential tests: 20,000 encodings, 159,976 cases, zero failures.
- Atomic differential tests: 203 tests, 12,992 cases, zero failures.
- Flag helper tests: 2,000,484 operand pairs each at `-O0` and `-O1`, zero mismatches.
- The separate existing exclusive-monitor contention test gets the expected totals,
  but its three ABA checks fail. Repeating it against the committed HEAD CPU/SIMD
  headers reproduces all three failures, confirming that they predate these changes.
- Whole-function differential testing: 415 functions and 2,572 completed cases, zero
  failures; 9,956 attempted cases were skipped by the oracle's safety/step limits.
- The user confirmed that Continue opens the existing save. Live gameplay UI was also
  captured after loading. The 3D output remains visibly corrupted.

The test executable is currently `build/v111/oot-jump.exe`, linked from the rebuilt
objects because Windows kept the previous `dkb.exe` image locked. It uses the same
profile and data paths. Its private test log is `build/oot-fix/continue-trace.log`.

The graphics log still reports sampled float/uint mismatches, including RG16_UINT
bound to the float sampler `cp_t_tcb_7C`. Storage format answers alone do not repair
sampled-image declarations. Enabling explicit storage formats also exposed visual
regressions in the title video background; those changes remain under investigation.
Mesh draws and unsupported native shader stages remain separate unresolved issues.

The next graphics experiment carries sampled uint/sint identity in the shader answers,
uses separate texture/image metadata lookups for both native compute and legacy handle
layouts, and declares integer sampled images with a bitcast of fetched register values.
The patch is `tools/xlate/patches/0020-preserve-integer-sampled-image-types.patch`;
vendor files remain unchanged. The translator builds, its six patch-selection tests
pass, and the private shader translation jobs complete (5 newly accepted and 17 in the
changed-job list). In the live `typed-trace.log`, the previous float/uint component
mismatch errors are absent. The final gameplay image is nevertheless posterized.

A second private capture, frame 1596 (242 passes), shows a valid multicolour HDR scene
at pass 236, followed by integer-packed colour at pass 239. The latter image's base
format is R11G11B10F, but a skipped tensor stage samples an RGB9E5 alias at the same
address. The linear upscale stand-in was choosing the last writer's base image. It
now looks for the tensor input's RGB9E5 alias at that exact address and shape. Merely
selecting the alias did not repair the output in `alias-scene.png`.

`runtime/vk_texture.c`, `entry_of`, copies live alias content when an image is created;
`make_fresh_snap` does not refresh existing GPU-owned aliases. Explicitly refreshing
that compatible alias from the recorded writer through the existing buffer-copy
path produced black gameplay output. That experiment was removed.

Selecting the packing dispatch's unique declared HDR input instead produced an
overexposed image, and the user reported repeated flashes of light. The packing pass
cannot be skipped as a colour-processing step. This diagnostic is now opt-in only
with `OOT_LINEAR_UNPACKED_SOURCE`; ordinary runs leave it disabled. The flashing
test process was stopped. Its private capture is `build/oot-fix/linear-scene.png` and
its log is `build/oot-fix/linear-trace.log`. Do not count the temporary appearance of
geometry or brighter output as a completed rendering fix.

The latest compiled executable is `build/v111/oot.exe`, with its matching translator
and identity alongside it. It includes the Continue repair and leaves the unpacked
diagnostic disabled by default. Further checks pass: 28 compute c0 checks, 45 shader
control/profile checks at each of `-O0` and `-O2`, 19 shader-service/cache tests, and
nine geometry translator tests. A full-tensor run was started with `OOT_RUN_TENSOR=1`,
but its log ended without a fatal message before a checked gameplay capture was
obtained. Whether that window was closed manually is still being established; this
does not count as a successful full-tensor rendering test.

A repeated full-tensor run generated a roughly 21 MB SPIR-V module and failed to
advance beyond the first automatic title capture while compilation continued. The
user reported that the initial menu no longer loaded. That run was stopped, and
the default tensor stand-in was restored for interactive testing. Full tensor
lowering is not currently a usable replacement merely because it translates.

An additional abort was recorded on the previous run's completion thread. Host address
symbolization points into `runtime/vk_sync.c`, `completion_main`, near the fence wait
and deferred callback processing. It is not the Continue dispatch failure and its
specific trigger has not yet been established.

## Confirmed missing mesh draw path

Runtime presentation requirement: the user wants every game execution to keep both
the game window and a live log console visible. Future launches must not hide the
console or redirect the only visible log exclusively to a file. The current live
log watcher is `build/oot-fix/Watch OOT log.ps1`; the reusable visible launcher is
`build/v111/Run OOT with log.ps1`. Both scripts are private build artifacts.

The latest build adds an independently allocated RGB9E5 transfer image for the
linear tensor replacement. The writer's 32-bit image is copied bit-for-bit into it,
then converted by the blit. This avoids both stale pool aliases and bypassing the
packing pass. `tests/vk_packed_hdr_probe.c` verifies that the same GPU operation
returns exactly (0.25, 0.5, 1.0, 1.0) from synthetic packed RGB9E5 data.
The translator also includes patch 0021, declaring StorageImageExtendedFormats
for explicitly typed storage images. The shader patch selection tests and the
private 5/17 shader job lists pass. Gameplay verification of this build is pending.
The user requested a build to test before further investigation of the native
oot-rgb9.exe access violation; crash investigation is deferred accordingly.

The user confirmed that the first RGB9E5 build remained blue/posterized. Its log
contained no independent-RGB9E5 decode message: the new conversion branch was not
being selected. `runtime/vk_texture.c`, `slot_resolve`, could follow a live image
at the same storage address while preserving the slot swizzle but discarding the
slot's format. A logical RGB9E5 descriptor could therefore still report R11G11B10F
as its sampling format. The resolver now preserves compatible colour format
reinterpretation as an implicit view, without overriding an explicit view format.
Implicit swizzle composition also retains that format. The tensor source lookup
now checks the effective view format, rather than the backing image format alone.

`tests/texture_followed_view_test.py` runs the real resolver with synthetic providers
at `-O0` and `-O2`: implicit format plus swizzle, explicit-format precedence,
unchanged-format and incompatible-format cases pass. Existing descriptor-pool
cache tests pass all 60 checks at both optimization levels. The new runtime build
is `build/v111/oot-follow.exe`; visual verification is pending. Failed native
shader translations remain independent unresolved problems, not evidence that
the final colour stage has been repaired.

The `fix/oot-bloom` history was fetched to depth 32 for a commit-level follow-up.
Its tip `6c1dd4e92c72a95b6abc09eaab1d5a74e90db795` changes H0_NH1 to swap
the FP16 halves and negate the new low half. That exact sequence is already in
the vendored baseline; copying the tip again would not change this port.
The preceding `abe66f465c7e72f75b1b55f84a5597f4adb948e5` adds hash-based program
rejection and asynchronous frame-capture diagnostics, rather than establishing
that every bloom failure is fixed. Excessive bloom remains a hypothesis to test
against the pass inputs and output, not a diagnosis inferred from the branch name.

The user reports that the blue output is gone in the followed-view build, but
textures and meshes flicker. The log now confirms that the independent RGB9E5
decode executes. Local captures also show overexposed/white gameplay frames;
therefore the colour repair is partial and visual stability is not established.
The next capture includes the stand-in's source and destination in the same frame,
plus optional raw HDR words (`TOTK_DUMP_RAW_HDR=1`), to distinguish corruption
before conversion from later saturation or stale frame content. These remain
private under `build/oot-fix/color-followed-01/`.

Game launch now uses `build/v111/Play OOT.cmd` with a visible native console,
launched through the desktop shell. This avoids relying on a redirected child
pipeline whose game window had already exited when its state was later checked.
Every launch must verify the live game process/window instead of assuming success
from a returned launch command. The native console log is also mirrored by the
runtime at `build/v111/logs/totk.log`.

The live Ocarina trace explicitly calls both `nvnCommandBufferDrawMeshTasks` and `nvnCommandBufferDrawMeshTasksIndirect`. `runtime/nvn_api_nvn2.c`, `NVN2_NOT_RECORDED`, currently accepts both without recording any draw. This is an independent, concrete reason the renderer cannot produce the mesh geometry; shader acceptance fixes alone cannot repair it.

Lichii `src/video_core/shader_environment.cpp`, the graphics environment constructor, documents that native task/mesh programs occupy vertex and tessellation hardware slots when mesh mode is enabled. Our `runtime/nvn2_lower.cpp`, `lower`, selects tessellation options from the SPH type alone. All 64 remaining stage-buffer failures have SPH shader type 3 and reject ISBERD. This is consistent with the missing mesh context, but the precise program role still needs to be traced through NVN program binding and geometry-processing mode before changing stage classification.

After validating the image-format repair, the remaining mesh implementation must cover
the complete path:

1. Recover and validate the NVN mesh command signatures, task counts, first-task values and indirect-buffer addressing. Record these commands with their program and geometry-processing state instead of accepting them as no-ops.
2. Preserve task/mesh stage identity through program construction, lowering and replay. Compare with Lichii's native graphics environment and stage-buffer pseudo-instructions. Do not reinterpret ISBERD as an ordinary vertex move to bypass a failure.
3. Query and enable Vulkan mesh/task features and limits before device creation. This port's Ryujinx-based translator does not automatically gain Lichii's task/mesh SPIR-V support by updating the Ampere header. Choose and implement a compatible translation route with correct descriptor reflection and output topology.
4. Replay direct and indirect mesh draws inside the appropriate render pass, with task/mesh visibility in resource barriers. Investigate the first-task workgroup offset correction from `fix/oot-fog` only after the basic draw path is working.
5. Validate a visible Ocarina 3D scene and check the native/translator failure logs. Record remaining fog, bloom, texture or atomic issues separately from missing geometry.

The reference changes to CPU/GPU cache flush decisions and draw scheduling are not shortcuts to copy: hardware-visible writes and synchronization must be preserved in this runtime.

## Tracing the white surfaces before post-processing

The private capture `build/oot-fix/color-followed-02/` establishes that excessive
brightness is present before bloom and before the upscale stand-in. Raw HDR words
from pass 0191 (sky), 0205 and 0207 were compared directly. At pixel (600,300),
all three contain `0x98674509`; at (120,680), all contain `0x97a6ecff`.
Other unchanged sky pixels cover parts of Link's cap and shirt. In contrast,
the depth capture contains the room and Link. This supports investigating missing
colour/material writes. It does not prove that every white pixel is caused by a
failed shader, or that the blend operation itself accumulates incorrect values.
The user correctly observed that both presented pass/upscale images were already
too bright; neither image should have been described as visually correct.

The next priority is shader and draw failures, rather than further RGB format
experiments. Native lowering diagnostics now identify the content hash and SPH
type. `runtime/gfx_shader.c`, `req_init`, describes an unlowered native stage as
such, instead of misleadingly reporting that no translator exists. During an
armed frame capture, `runtime/vk_dump.c`, `vk_dump_draw_skip`, groups missing
vertex stages, failed shaders and pending/failed pipelines by program and pass,
including raw native hashes. Successful draws alone cannot expose those omissions.

The first focused shader change accepts indexed unsigned 8/16-bit ULDC, using
the same Maxwell LDC width encoding already used for direct subword loads.
The existing bank, alignment, offset-range and operand guards remain. Synthetic
instruction checks pass (170 checks). The frozen corpus advances from 6,333 to
6,334 accepted stages out of 6,413. Native stage `abccf81b45c5eb50` also passes
the actual translator and `spirv-val --target-env vulkan1.3`. Four other shaders
advance to later rejections (LEA carry/high modes or an out-of-range ULDC offset);
they are not fixed merely because their first failure moved.

The runtime build `build/v111/oot-shader-trace.exe` was launched with a visible
live log console through its own immutable CMD launcher. Its frame 1116 capture
under `build/oot-fix/shader-trace-01/` has no recorded conventional draw skips
from the newly instrumented shader/pipeline rejection paths. The displayed 3D
frame remains white. Consequently, the general shader error list alone is not
evidence that a particular failed conventional draw caused this frame. Mesh
commands still disappear before Vulkan replay, and shaders that compile can
still have incorrect semantics. Both remain active investigation paths.

The next focused change extends `register_pressure.h`, `PrepareRegisterPressure`,
to immediate FMNMX, P2R and indexed LDC. These instructions already had semantic
lowering, but programs using them above the reserved register boundary failed
because their operand shapes were absent from the shadowing table. Five vertex
stages now lower completely (`046361b69e7dc674`, `c57d42c3b5eb1fb7`,
`bde5381230acfe7b`, `7b6ae1bb954d68c9`, `da5ace2b36f8be46`). All five also
translate and pass SPIR-V validation for Vulkan 1.3. Synthetic checks now total
186; the frozen corpus accepts 6,339 of 6,413 stages. No visual improvement is
claimed from that acceptance count. A visible runtime test still showed white.

Inspection of the rejection trace uncovered another concrete bug in
`runtime/vk_exec_draw.c`, `find_stage` and `find_stage_scan`: they read stage
and length at legacy offsets even when failed native lowering left a compact
control. This can read outside that control and misclassify an unlowered
fragment stage as absent, allowing an unintended depth-only draw. Both paths
now use `program_stage_info`, which reads native length at +0x38 and graphics
stage from the native SPH, retaining the stage even when its conversion failed.
`shader_for` rejects the raw stage before calling legacy control/descriptor
helpers. This correction does not implement mesh stage identity; its explicit
geometry-mode context is still required.

`tests/native_program_stage_test.py` exercises the actual production helper at
O0/O2, placing a compact control immediately before an inaccessible guard page.
Native graphics and compute, legacy metadata, missing pointers, zero lengths
and unknown graphics types pass. The new `oot-stage.exe` build includes this
lookup repair and the prior shader fixes; runtime capture remains in progress.

## Compute shaders with indirect global pointers

The live shader-failure capture at `build/oot-fix/shader-shadow-failures/capture-24708-*`
showed repeated NVN 2 compute failures with `No bounded cbuf-pair candidate`.
Replaying `d2fc3173250f602f` with translator-side SSA tracing showed that the
global address is formed through phis and loaded values; its expression graph
contains no recoverable cbuf root. The dispatch still declared four bound SSBOs
in its versioned c0 layout (`c0=[0,4,4,0]`, root counts all zero).

The first fallback experiment added those descriptors after the old unrestricted
expression walk. It then failed the translator's 32-storage-descriptor limit:
the walk had also contributed unrelated cbuf pairs. The corrected path in
`tools/xlate/patches/0023-enumerate-compute-storage-candidates.patch` uses the
compute c0 layout to enumerate only its actually bound SSBO base-address pairs.
`tools/xlate/Program.cs`, `Accessor.QueryStorageBufferCandidateOffset`, accounts
for the 16-byte header, padded root tables, UBO table and 16-byte SSBO records.
The existing guarded helper accepts an access only if its address is inside one
of those descriptors. The descriptor count is capped at the NVN 2 limit of 16.

Offline replay now translates `d2fc3173250f602f` and five further captured
compute shaders that previously failed candidate discovery; all six outputs
pass `spirv-val --target-env vulkan1.3`. The first replay's reflection contains
four guarded SSBO descriptors at c0 word offsets 20, 24, 28 and 32. This
confirms translator output validity, but does not establish that the game renders
correctly. The previous runtime process exited before a fresh render-pass capture
was armed, and no visual improvement has been measured from this change yet.

The latest runtime log also retains separate compute failures for non-32-bit
atomics and subword global loads, along with native tessellation/mesh-stage
rejections. Those remain independent translation paths to investigate.

### UBO candidates and the visible black transition

A later gameplay capture exposed a gap in the candidate list. Shader
`b7f1d8151e7d25e6` declared six UBOs and no SSBOs (`c0` counts 1, 6, 0, 4),
but its global load address came from a c0 buffer descriptor. The SSBO-only
enumeration therefore supplied zero candidates and rejected the shader. The
same pattern appeared in `fd91ff00f8f752db`.

`tools/xlate/Program.cs`, `Accessor.QueryStorageBufferCandidateCount` and
`Accessor.QueryStorageBufferCandidateOffset`, now enumerate the contiguous UBO
and SSBO descriptor records in c0. The guarded range check still decides which
candidate contains each address. Both captured shaders now translate; both
outputs pass `spirv-val --target-env vulkan1.3`. This fixes those translation
failures, but it does not establish that their execution fixes the scene.

The visible runtime test used `build/v111/shader-cache`, the default cache
directory. The prior isolated test cache was merged into it so the next run can
reuse translated shader outputs. The first gameplay transition remains black
while the game compiles pipelines. The latest runtime capture has no repeated
`No bounded cbuf-pair candidate` failures; it still reports 64-bit atomics and
subword loads as unsupported. Visual verification of the playable 3D scene is
still pending.

The earlier process that ended with `FATAL: abort (0x16)` was mapped through
`build/v111/totk.map` and `runtime/gfx_present.c`, `present_image`: the captured
return address follows `vkQueueSubmit2`. The executable stack report omits
frames from the Vulkan driver, so the driver-side cause is not yet known. The
10 Vulkan validation messages in that log were repeats of
`VUID-vkCmdDrawIndexed-None-06479`, about depth-comparison sampling; none
identified that queue submission. Do not treat this separate abort as fixed.

## Mesh draws: what the Lichii ISBEWR fix can and cannot explain here

A second Lichii snapshot on 2026-11-05 (103 branch tips; `nx2` now at
`8813b44794fd3bd1833165f9f8802925a09e3861`, `fix/oot-fog` at
`8390649c0c0699192b35f486f3aac6d9cf88c8b3`) adds these Ocarina-related branches:
`fix/oot-upscaler-gathers`, `exp/oot-k4-lowering`, `test/oot-fog-perf`,
`test/oot-k4-mem` and `test/oot-input`. Their Ocarina shader commits are the same
gather, ISBEWR and LEA changes already reviewed; the rest are Lichii emulator
diagnostics (debug server, frame-dump probes, buffer-cache fences, GPU time scaling).

- ISBEWR shadowing (`b328477`): the NaN it fixes was written by a **mesh** program
  (Lichii hash `8159e91e0097876a`) storing R238/R240 through ISBEWR. In this port
  every SPH type 3 stage still fails at +0 with `native tessellation stage buffer
  read mode` (64 stages in `build/oot-fix/shadow-lowering.txt`), and
  `nvnCommandBufferDrawMeshTasks*` were accepted without being recorded. That
  program therefore never runs here, so the upstream fix cannot change this port's
  brightness. Its seven changed outputs are SPH type 1 task programs, which are
  only used by the same unreplayed mesh draws.
- LEA in the per-tap offset proof (`8390649`): our proof in `lower_to_maxwell.h`
  treats an instruction without a modelled write count as writing one register
  (`write_count(definition).value_or(1)`) and only fails if that register is an
  offset register. LEA/LEA.HI already pass here; no import is needed.
- `EDEN_NX2_HANDLE_ALIGN` (`dfb0b6b`) concerns Lichii's handle programs, and the
  alpha-reference rounding (`ec3a455`) is a pipeline-count optimisation. Neither
  maps onto a known brightness fault in this runtime.

The earlier capture showed sky values left under Link's cap and shirt while the
depth buffer contained the room and Link. If the colour/material pass draws those
objects through task/mesh programs (Eglantine NX2_NOTES row 97 reports high-detail
geometry drawn that way), dropped mesh draws would leave exactly this pattern. That
is a hypothesis to measure, not yet a diagnosis.

### Mesh draw instrumentation

`nvnCommandBufferDrawMeshTasks` and `nvnCommandBufferDrawMeshTasksIndirect` now
record `OP_DRAW_MESH` (`runtime/nvn_api_cmd.c`) with their raw arguments, because
the NVN 2 argument layout is not yet confirmed. The replay still draws nothing for
them. `runtime/vk_exec.c` counts them per frame (`mesh N` in the `TOTK_TRACE=gfx`
frame line), and `runtime/vk_dump.c`, `vk_dump_mesh`, logs each distinct program
once with its stage hashes, arguments, caller and the pass's targets; during an
armed capture the draw is listed with that pass's skipped draws. `nvn_probe.c`
covers the record path. Launcher: `build/v111/Play OOT mesh trace.cmd`.

What to check in the log: how many mesh draws a 3D frame drops, whether their
targets are the scene's HDR/G-buffer targets that later show sky values, and
whether the logged stage hashes match the 64 rejected SPH type 3 stages. If they
do, the next step is the real mesh path (plan above), not further colour work.

## Live NaN isolation on 2026-11-05

The previous session added two environment-only diagnostics in `runtime/vk_dump.c`:
`vk_dump_skip_fs` omits draws whose fragment SPIR-V hash is in `DKB_SKIP_FS_FILE`
(one hexadecimal hash per line, maximum 64, reread every 60 frames). The trigger
path can be recreated for repeated captures; subsequent captures use `capture-N`.
Neither diagnostic is a rendering fix.

The resumed process (PID 54264) produced these private captures under
`build/oot-fix/bright-02/`. Match passes by their targets and ordering, not their
sequence numbers, which change between frames. Counts below are NaN **channel
values**, not pixels (`runtime/vk_dump.c`, `write_images`, increments per channel).

| Capture | Disabled fragment shaders | Material target result |
| --- | --- | --- |
| root, frame 17299 | none | pass 0193 has zero NaNs; pass 0195 has 785181 |
| capture-2, frame 19466 | previous 24 candidates | pass 0168 still has 314361 |
| capture-3, frame 21471 | 25 listed in current baseline material pass | pass 0168 still has 353772 |
| capture-4, frame 22738 | expanded union of 35 | pass 0185 still has 290283 |
| capture-5, frame 23621 | expanded union of 52 | pass 0184 has zero; following pass 0185 has 801513 |
| capture-6 | baseline 25 plus first 13 of the remaining 27 | pass 0184 still has 563670 |
| capture-7, frame 28605 | baseline 25 plus seven of the final 14 | pass 0185 still has 64197; following pass 0186 has 855393 |

The initial 24-shader list is incomplete. `vk_dump_draw` retains only 48 distinct
pipeline/texture combinations per pass (`MAX_ENTS`); later combinations increment
`ents_over` without logging their shader hashes. Omitting listed shaders exposes
further shaders in the next capture. The expanded list and intermediate lists are
private diagnostic files beside the captures, not source assets.

The 52-shader experiment removes NaNs from the material pass but leaves NaNs in
the following pass. Do not use absence of NaNs across the entire frame as the
bisection criterion. This also does not prove a single faulty shader: removing a
draw can suppress either a bad writer or an input consumed by another writer.
The resumed run remained active with recent 60 FPS log samples and no logged
`FATAL` or `DEVICE_LOST` at that check. Capture frames themselves incur readback
stalls and must not be used as normal frame-time measurements. No visual fix is
claimed. Mesh omissions remain a separate known limitation.

### Confirmed contributing shader and control captures

A second isolation method restored half the candidate shaders at a time while
keeping every other shader in the 52-member union disabled. The private script
`build/oot-fix/bright-02/isolate.py` matches the material render pass immediately
after compute SPIR-V `e861c3ee65989925`, then waits for its image statistics and
the capture trigger to be removed. Results and disabled lists are preserved in
`isolation-results.json`. It restores an empty skip list on exit.

| Capture | Restored group size during narrowing | Material NaN channel values |
| --- | --- | --- |
| capture-8 | 26 | 186942 |
| capture-9 | 13 | 29229 |
| capture-10 | 6 | 38802 |
| capture-11 | 3 | 10758 |
| capture-12 | 1 | 84528 |
| capture-13 | same single shader, repeated | 85917 |
| capture-14 | none of the 52 | 0 |
| capture-15 | all except the isolated shader (only that shader disabled) | 958248 |

The isolated fragment SPIR-V is `00fa71076eb7ed90`, native stage
`abc4dfd9710c36d2`. The baseline identifies program 9338, vertex SPIR-V
`6bbf02f61dced541`, native vertex `6a0772c1c603e354`. Re-enabling that fragment
shader is sufficient to reintroduce NaNs with the other 51 omitted. Disabling it
alone is insufficient to remove NaNs from the full material pass. Therefore it is
a confirmed contributing draw group, **not** a unique root cause or a fix.

Its captured SPIR-V passes `spirv-val --target-env vulkan1.3`. Structural validity
does not validate numerical semantics or bound data. Local disassembly contains
reciprocals, texture sampling and square roots. One inspected square root is
preceded by `FClamp(0,1)`, so merely seeing a square root does not establish the
source of a NaN. Eglantine's
`src/shader_recompiler/backend/spirv/emit_spirv_floating_point.cpp` also emits
`OpFClamp`; this observation does not justify changing saturation semantics.
No shader or runtime rendering fix was made during this isolation. Subsequent
work should trace this shader's inputs and first non-finite intermediate, and
isolate additional contributing groups separately. All draws were restored after
the final capture; the game was left running.

### Follow-up correction and numerical experiment

`runtime/vk_dump.c`, `vk_dump_draw` and `vk_dump_pass_end`, now retain a separate
per-pass list of up to 512 distinct fragment hashes. This list is emitted even
when the 48 detailed pipeline/texture entries overflow. It also writes the
corresponding SPIR-V through `shader_hash`; overflow of the independent list is
explicitly reported. This fixes the incomplete candidate enumeration demonstrated
above. The Vulkan dump object compiled and the executable linked successfully;
`git diff --check` and `tools/check_content.py --staged` passed. Live verification
of the new summary is pending a capture from the rebuilt process.

The existing opt-in numeric min/max/SAT translator was built into an ignored test
folder. Its owned GPU fixture passed 36 strict checks with zero mismatches and
zero Vulkan validation errors on the local GPU. This verifies the fixture, not
the game's NaN root cause. Applying that option globally produced a compute
pipeline logged as 20700 KB of SPIR-V, with an 84845 ms pipeline build. A scratch
variant limits the three numeric code-generation branches to fragment stages;
its source and executable remain under `build/oot-fix/defined-float-xlate/`.
Constant folding still uses the numeric opt-in implementation. The scratch
executable uses its own identity (`3747a1f43da7f006` prefix), not the default
translator's cache identity. No default shader semantics have been changed.

The initial Python launches redirected stderr to a file and hid the console;
this was unsuitable for the user's workflow. A pipe-and-tee trial also did not
establish a working menu. The direct CMD launcher restores the original visible
console and the runtime's simultaneous `logs/totk.log` recording. Both the direct
baseline and fragment-only experimental launches advanced beyond frame 600/360,
respectively, with 60 FPS samples. A black loading screen alone is not evidence
that the NaN fix works or that the playable scene has loaded. Preserve live
console visibility on subsequent launches. Scratch launchers and experiment logs
remain private under `build/`.

### Settled-map follow-up and limitations of the numeric experiment

After the user confirmed the map was loaded, performance samples showed zero
pending draws, zero shader waits and zero pipeline builds at about 60 FPS. No
more desktop screenshots were taken; subsequent checks read capture statistics
and shader lists only. Captures must wait for loading/compilation to finish.

The fragment-only experiment maps native fragment `abc4dfd9710c36d2` to SPIR-V
`1a8b24e03070da0a`. The settled-map capture-2 still contains 1118040 NaN channel
values in material pass 0194 and 1571955 in the following pass. The numeric
variant therefore does not fix the full scene. These counts must not be compared
as a controlled improvement/regression against an older camera pose.

Further exclusion captures exposed additional programs entering the material
pass over time, despite zero pending compilations. The complete per-pass hash
summary now makes this visible. Captures 3, 4 and 5 still retained respectively
six, four and two fragment shaders in the material pass; they are **not**
single-shader numerical tests. Capture-5 retained `1a8b24e03070da0a` and
`c6f61bb34dd254a2`, with 11139 NaN channel values. It would be incorrect to
attribute that count exclusively to the originally selected shader.

The bounded confirmation script (`build/oot-fix/bright-numeric/confirm.py`)
requires the latest performance interval to have zero pending draws and zero
shader waits before each capture, records every remaining material shader, and
restores all draws in a finally block. Captures 6 through 11 never achieved a
single remaining shader: the old executable's 64-entry exclusion limit kept
letting different shaders through. The detailed results are private in
`confirm-results.json`; no conclusion about the isolated shader's numerical
semantics follows from those attempts. All draws were restored afterwards.

To remove that diagnostic limitation, `runtime/vk_dump.c`, `skipfs_read`, now
accepts 512 hashes, matching the independent coverage list's capacity. The object
compiled, and a separate `build/v111/dkb-diagnostic.exe` was linked from the same
build objects without closing the running game. That executable contains the
512-entry change; the currently running process still has the older limit.
It is a diagnostic correction, not a lighting fix. Source checks
`git diff --check` and `tools/check_content.py --staged` passed.

A read-only live check of the selected draw's recorded UBO addresses found no
non-finite float words in c3, c4, c5, c6 or c7. The buffers were retained privately
as `live-cN.bin`. This is CPU memory sampled after the captured frame, not proof
of the exact values used by that earlier GPU draw; it does not validate SSBOs,
textures, interpolation or arithmetic. The selected vertex shader explicitly
initializes all its declared output components, so an uninitialized declaration
alone is not evidence for this case. Its transformed normal uses inverse square
root of its squared length; input/normal tracing remains open, and no workaround
for zero normals has been applied.

### First isolated NaN and live numerical probes

The 512-entry executable isolated native fragment `abc4dfd9710c36d2` in
`build/oot-fix/bright-single/capture-4/`: material pass 0189 lists only its numeric
SPIR-V variant `1a8b24e03070da0a` and contains 7602 NaN channel values. This is the
first exclusion capture with a verified single remaining material shader. The
numeric min/max experiment does not remove this case.

An opt-in local probe loader is now available through `DKB_FS_OVERRIDE_HASH`
(original fragment SPIR-V FNV hash) and `DKB_FS_OVERRIDE_FILE` (probe module).
`runtime/gfx_shader.c`, `gfx_shader_debug_override`, checks the original module
prefix and permits only appended private tracing declarations; each variant has
a distinct pipeline key, shares the unchanged resource reflection and stays out
of the ordinary disk shader cache. At most 32 variants remain resident for cached
pipelines/in-flight commands. The file is reread every 60 frames; removing it
restores the original shader. The draw path applies the probe after shader memo
lookup, so a cached original stage does not suppress a probe change.
`tests/gfx_shader_probe_test.c` tests the production declaration guard, including
changed bindings/locations, non-private state, malformed/truncated instruction
streams and missing function bodies. It passes at O0 and O2. The runtime compiled
and linked successfully. Probe binaries are separately validated with spirv-val.

The private first-NaN instrumenter retains the original declarations and records
the first arithmetic/texture operation producing NaN, then encodes that operation
ID in the material colour. Loads and bitcasts are not treated as arithmetic:
packed integer metadata can legitimately have NaN-looking encodings. The probe
instruments 1218 operations and validates for Vulkan 1.3. This is instrumentation,
not a rendering correction; its finite diagnostic output is not evidence that
NaNs have been fixed.

In `build/oot-fix/bright-trace/`, the first isolated probe capture contains only
probe fragment `5c9fc0482f3de9d9`. Its raw material image encodes result ID 4593
in 3055 pixels. The original numeric module has:

- `%4591 = OpLoad %float %297`
- `%4592 = OpLoad %float %668`
- `%4593 = OpFMul %float %4591 %4592`

A second probe reports those two operands. Its isolated capture-3 (probe hash
`8deffdb1c107a6b1`) contains 3062 pixels with NaN in the first operand and zero in
the second. The first operand comes from a uint SSBO load, bitcast to float: the
load at result 2927 supplies the value stored in function variable 297. The source
expression addresses the buffer declared at driver c0+0x530, at base+64. Therefore
changing the multiply or clamping its output would hide an invalid input rather
than establish the cause. The actual offending storage address and its producer
remain to be identified.

A CPU read of the *representative* c0+0x530 binding address printed in the pass
found zero at +64. Do not conclude that this proves a mirror synchronization bug:
detailed capture entries group draws by pipeline/textures and can represent a
different instance/buffer. Inline GPU updates can also differ from later CPU
memory. A live address probe is needed for the actual affected pixels.

Later measurements lost the original affected geometry from view. A listed draw
alone does not establish visible pixel coverage; finite or absent probe values
from those measurements are not a successful correction. The two inspected
render-pass images were from the already loaded map, not desktop/loading-screen
screenshots. Probes were paused by removing the override file and restoring an
empty exclusion list while the camera is repositioned. Preserve both shader
identity and visible coverage for subsequent before/after comparisons.

### Cross-check of the second agent's culling and mesh report

The user supplied another agent's report: the second culling compute usually sees
an empty instance list, and only about 17 of 140 dispatches write a count (roughly
nine instances each). That experiment has not been independently replayed here.
Its suggestion that the workload is vegetation is a hypothesis, not an established
identity. Empty indirect arguments alone are not evidence of a lost GPU store.

The report's missing mesh path is independently confirmed in current source:
`runtime/nvn_api_cmd.c`, `nvnCommandBufferDrawMeshTasks` and
`nvnCommandBufferDrawMeshTasksIndirect`, record `OP_DRAW_MESH` with raw arguments.
`runtime/vk_exec.c`, the `OP_DRAW_MESH` replay case, increments a counter and calls
`vk_dump_mesh` without issuing a Vulkan draw. Other mesh entry points in
`runtime/nvn_api_nvn2.c` remain unrecorded. In the Ampere lowerer's ISBERD/ISBEWR
case, task/mesh stage-buffer forms still reject as tessellation/geometry modes.
Changing the error text or accepting the stage type would not implement their
stage-buffer communication, emitted vertices, primitives or draw replay.

The current process's log contains 111 messages with
`native tessellation stage buffer read mode`. This is an event count. The normal
settled-map capture `build/oot-fix/bright-trace/capture-11/` records at least 34
direct and 41 indirect omitted mesh draws in that frame. These are per-frame
records, so they must not be equated with the other agent's nearly 700 calls
accumulated while entering the scene. Capture limits can omit additional detail.
The final presented image remains white, with HUD/minimap visible; the material
pass 0181 already has 883620 NaN channel values, increasing to 1495308 in pass
0182. No probe or exclusion was active for this normal capture.

The missing mesh implementation explains a definite category of absent geometry.
It does not prove the origin of the independently measured NaN in a conventional
fragment draw. Retain both findings: do not label empty culling counts a write
failure, and do not claim mesh replay is implemented or that implementing it alone
has been shown to remove the NaN.

### Address of the first invalid coefficient and revised coherence hypothesis

The address probe now gates its colour code on first-invalid-operation ID 4593,
so only pixels with that failure report an address. Captures are made after the
loaded-map performance interval reports zero pending shader/pipeline work. Probe
captures 19--22 preserve all other draws to avoid changing the producer workload.
The low-address probe encoded `0x7f40` (3164 pixels); the next address segment
encoded `0x1693` (2194 pixels); a separate c0 high-word probe encoded 2 (13802
pixels). Together these identify the load at `0x25a4c7f40`, base plus 64. Different
pixel counts/locations across these captures prevent pixelwise subtraction; use
the repeated encoded address populations instead. The raw coefficient probe
encoded a NaN upper-bit pattern `0x7fffc000` in 3636 pixels.

A read-only inspection of the current memory index found that address inside an
imported pool `[0x25a4c6000,0x25a4c8000)`, flags `0x24`, with an active VRAM mirror.
This confirms which backing mechanism is used; it does not establish a mirror
bug. Earlier CPU memory samples were finite/zero, but a later sample contains
`0x7fffffff` at coefficient +64, plus positive infinity at +48 and negative
infinity at +80. Several adjacent RGB coefficients share these values. Therefore
CPU and GPU can both contain the invalid data: the earlier zero CPU sample is
not sufficient evidence of stale GPU storage.

A controlled experiment was prepared to rewrite *identical existing zero bytes*
to exercise write tracking. It checked all 192 bytes first and refused because
the CPU contents had changed; **no bytes were written**. No zeroing or saturation
workaround was applied. A 15-second read-only sample of the replay's existing
inline update ring found no covering update, which only excludes observed
inline updates in that interval, not shader/copy/CPU producers.

The next run uses the existing `TOTK_DUMP_BUF=0x25a4c7f00:0xc0` diagnostic to retain
the GPU buffer after every logged pass. The private launcher
`build/v111/oot-nan-buffer.cmd` keeps the live console and `logs/totk.log`, leaves
the fragment probe file absent and all draws enabled, and targets
`build/oot-fix/bright-buffer/`. Its first capture is reserved for the fully loaded
map; no trigger should be written during compilation. `analyze.py` in that private
folder reports the first non-finite buffer words and the corresponding pass.
The producer remains unconfirmed; the fragment multiply is a consumer of already
invalid data, not yet a demonstrated arithmetic-lowering fault.

## First NaN fixed: missing NVN 2 dispatch counts

The fully loaded baseline capture `build/oot-fix/bright-buffer/` establishes the
producer. Buffer `[0x25a4c7f00,+0xc0)` is finite after pass 0000 and remains finite
after pass 0020 (`fa3ede6464e186b3`). After pass 0112, compute SPIR-V
`21238adc0f99baaf`, it contains positive/negative infinity and NaNs, including
`0x7fffffff` at +0x40. That dispatch is 1x1x1 and samples a 256x256 RGBA16F image
whose preceding pass captures contain only finite values. Its output is the
lighting-coefficient buffer consumed by the fragment draw isolated above.

Disassembly identifies an actual host ABI omission: the compute reads workgroup
counts at driver c0+4 and +8, multiplies each by its local width (32), then divides
one by their product for sample normalization. `runtime/vk_exec_draw.c`,
`vk_c0_fill`, packed NVN 2 roots and descriptors but returned with the first 16
bytes zeroed by `nvn2_c0_pack`. Both counts were zero, so this dispatch divided by
zero before accumulating its lighting coefficients. The fragment multiply was
propagating the resulting invalid coefficient, not generating it independently.

The correction fills c0+4/+8/+12 from the actual direct-dispatch workgroup counts
through `runtime/nvn2_c0.h`, `nvn2_c0_dispatch`. `op_dispatch_ind` copies the actual
GPU arguments to those same header words for NVN 2 and retains +0x200 for NVN 1.
Both paths preserve the roots starting at +0x10 and all resource tables; GPU-patched
indirect banks are not reused. Counts are not guessed from image dimensions or
local thread counts. Eglantine's available shader sources and NX2 notes were
consulted first; this reference checkout does not contain its NVN/QMD runtime
implementation, so the concrete offset evidence here is the captured game shader
and measured dispatch. No upstream implementation was copied.

Validation:

- `tests/nvn2_compute_c0_test.py`: 33 checks, zero failures.
- `tests/nvn2_indirect_c0_test.py`: 34 checks, zero failures, exercising the actual
  indirect replay function with CPU Vulkan stubs and preserving NVN 1 offsets.
- `tests/nvn2_dispatch_header_test.py`: the actual production `vk_c0_fill` passes
  at O0 and O2 for both ABIs, roots/descriptors and nonzero normalization count.
- Runtime compilation and link passed. `git diff --check` and
  `tools/check_content.py --staged` passed.

The after capture `build/oot-fix/bright-headfix/` uses the same compute SPIR-V
`21238adc0f99baaf` at pass 0112. Every retained buffer snapshot is finite; +0x40 is
now `0x403bad0e`. Material pass 0193 and subsequent scene pass 0194 report **zero
NaN channel values**, with all draws enabled and the override probe absent.
The environment/coefficient defect is fixed without clamping or discarding draws.
The normal scene is visible again but remains too bright; missing mesh geometry
and presentation/upscaler semantics remain separate issues. The experimental
fragment-only numeric translator was held constant across this before/after
comparison and is not established as necessary for the fix.

### The next apparent NaN is packed data, not another numerical failure

After the header correction, the only nonzero NaN counter in the after capture
is pass 0224: compute `cbf73b50fdccf3cd` writes through an R32_UINT storage view
(NVN override 0xb) over a base R11G11B10F image. Its shader explicitly packs the
square roots of the preceding colour channels as shared-exponent RGB9E5 words.
The dump's base-format interpretation labels 116885 channel values NaN, but this
is not the format the next stage samples.

The private `check_packed.py` independently decoded all 2764800 RGB9E5 channel
values: **zero non-finite values**. The subsequent RGB9E5 source copy preserves
every packed word (zero bit mismatches). Decoded values differ from the square
roots of pass 0221's channels by at most 0.0018527, consistent with packing
quantization. Do not clamp or zero those apparently invalid R11 values: that
would corrupt valid RGB9E5 data. The upscaler stand-in's treatment of that
square-root encoded signal remains a specific brightness investigation target;
no presentation fix is claimed from this observation alone.
