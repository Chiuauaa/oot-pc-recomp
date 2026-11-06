# Ocarina of Time rendering investigation

## Handoff: state on 2026-11-06

Read this section first; the rest of the file is the dated log it summarises.

### User-identified pass 149: separate tone curve and bloom tested live

The user identifies `bright-culling-18/capture-14/pass_0149_t0` as excessively
bright and considers the preceding appearance close to original. Pass 149 is
FS `65156d0c69e40c69`, native `73320e720a317817`, the same bloom/tone-map shader
identified in previous captures. Its full-resolution scene input comes from
pass 132, not the numerically adjacent pass 148 (a 240x135 bloom level).
Capture 14 is restored production rendering, without the BRDF probes.

Reconstruction of this exact frame reproduces the output within 1.055 storage
ULP using gain 1 and bloom exponent 1.5. Over 762,984 matching terrain pixels,
scene mean RGB [0.26717,0.26659,0.19571] becomes [0.40688,0.42349,0.30135] after
the composite. `capture14-tonemap-ledger.json` retains the per-stage values.
The observed brightening is real; arithmetic agreement is not proof that the
original exposure, input radiance or displayed appearance is correct.

A new live test isolates both operations through the final display pipeline:

| Capture | Variant | Display sRGB mean RGB |
| --- | --- | --- |
| 18 | Bypass curve, retain bloom/gain | [0.56456,0.57391,0.47662] |
| 19 | Bypass curve and bloom, retain graded scene | [0.52713,0.53841,0.45161] |
| 20 | Retain curve, omit bloom | [0.61884,0.63271,0.52766] |
| 21 | Restore production shader | [0.65415,0.66594,0.55690] |

These are 710,248 exact-depth-matched terrain samples, mapped through the
1280x720-to-1706x960 display blit, with HUD/minimap regions excluded. Capture
18 still shows a blue/grey veil despite a lower mean; reducing global exposure
or bypassing the curve is not equivalent to removing the material specular
component selected by the user. Keep both the material input and the final
composition in the investigation rather than explaining the entire symptom
with bloom, gamma, or missing grass alone. Probe declarations are unchanged,
spirv-val and captured hashes pass, and all overrides are removed afterward.

A constant-pattern scan of retained original shader modules finds the four
curve constants (2.51,2.43,0.59,0.14) together only in `65156d0c69e40c69`, used
once in capture 14. This is a screening result, not proof that another shader
cannot apply a different curve. `tone-curve-module-scan.json` records that limit.
`tone-curve-statistics.json` and `terrain-probes/tone-curve-captures.json` retain
the experiment under ignored `build/oot-fix/bright-culling-18`.

The preceding incident-light probes (capture 15 colour, capture 16 direction,
capture 17 restored) measure the VS Location4 input as mean RGB
[1.06781,1.44355,2.32622], median [1.140625,1.546875,2.5]. The incoming field is
blue-weighted before the BRDF and final tone mapping. The encoded direction's
median length is 0.98982 after R11/R10 quantization; there is no large direction
length amplification in these samples. Raw HDR incident radiance exceeding one
is not alone an error. This guides further native/SH input comparison without
asserting that the field should be clamped or deleting its specular response.

### User selects the no-specular result; first BRDF factor probes measured

The user confirms `bright-culling-18/capture-3` (only material specular omitted)
is closer to the original, whereas restored capture 6 remains too bright.
Retain capture 3 as a visual comparison target. This supports following the
material specular contribution; it does not justify deleting ordinary specular
lighting or applying a hash-specific material correction.

With the same loaded map and no grass changes, captures 10-13 expose the BRDF
angles, shape, shared gains and roughness compensation; capture 14 restores the
normal shader. All modules pass spirv-val and the runner verifies the exact
captured identity after 75 or more presented frames. Per-probe depth matching
retains about 758,000-763,000 terrain pixels. Measurements are stored in
`bright-culling-18/brdf-statistics.json`:

- N.H / N.V / V.H mean [0.91896, 0.95194, 0.94439]; respective medians
  [0.9375, 0.96875, 0.953125]. These do not show a widespread zero half-vector
  dot product that would force Fresnel toward one.
- Material roughness mean 0.56412, compensated alpha mean 0.32197, BRDF factor
  mean 0.03657. The roughness-squared probe averages 0.32184. These results do
  not show a large roughness compensation multiplier in this view.
- Distance fade mean 0.99998, shared specular visibility/gain mean 0.93991,
  cubemap BRDF gain mean 0.01629. Median values are [1, 0.9765625, 0.0161133].

This narrows the next check to the VS Location4 incident-light colour feeding
the dominant specular branch, its SH/visibility inputs and native evaluation.
It does not prove that every BRDF operation is correct, or establish original
hardware exposure. No production specular scale or branch exclusion was added.
The live game is restored and remains open; numerical override files are gone.

### Brightness takes priority over vegetation; material reflection removal compared

The user explicitly directs us to resolve brightness before enabling grass,
because restored coverage could hide the landscape defect. Defer mesh/grass
execution work while keeping the existing visible terrain for diagnosis. This
supersedes treating vegetation restoration as the next visual test.

The game was closed normally, rebuilt and reopened with visible console/file
logging. Before that steering, capture 1 under `bright-culling-18` measured
11 indirect compute prefixes as [0,1,1] and one as [1200,1,1], plus the previous
41 zero mesh prefixes. The nonzero compute control verifies that the readback
is not simply returning zeros. This does not identify a culling defect. No
further culling work is required for the current brightness priority. A private
managed translator diagnostic was built but not installed; its temporary source
edit was restored, leaving the installed translator unchanged.

After map readiness, live numerical probes preserve the landscape shader's
resource declarations and replace only its final RGB sum. Capture 2 tags the
landscape. Captures 3, 4 and 5 respectively omit term D (specular), term B
(ambient) and term C (material fog contribution); capture 6 restores production.
The full remaining pipeline, including tone mapping and display encoding, still
runs. This tests the presented image rather than judging an isolated linear-HDR
PNG as if it were display encoded. All probes pass spirv-val and their captured
shader identities are verified after at least 75 presented frames.

The blue/white veil is substantially reduced when D is omitted. Removing B
makes the terrain dark but leaves a strong blue reflection; omitting C changes
little in this near-ground view. A second reversible test removes only D's
sky/sun BRDF factor (`%2488`, capture 7), then only its cubemap BRDF factor
(`%2490`, capture 8), and restores the original shader in capture 9. These are
diagnostic exclusions, not a justified production fix or a global light scale.
No mesh/grass draws were enabled.

Across 705,565 exact depth-matched terrain pixels, excluding HUD/minimap areas,
the material and final display means are:

| Diagnostic | Material RGB | Display sRGB |
| --- | --- | --- |
| Normal restored | [0.26151, 0.26015, 0.18826] | [0.65370, 0.66575, 0.55608] |
| Without D | [0.20697, 0.19262, 0.08661] | [0.58431, 0.58231, 0.36258] |
| Without B | [0.11397, 0.10897, 0.11783] | [0.39763, 0.41087, 0.42168] |
| Without C | [0.24151, 0.24551, 0.17763] | [0.63870, 0.65357, 0.54061] |
| Without sky/sun specular | [0.22297, 0.20799, 0.10473] | [0.60851, 0.60631, 0.40986] |
| Without cubemap specular | [0.24375, 0.24301, 0.16910] | [0.63297, 0.64692, 0.52626] |

The matching display samples follow the game's 1280x720 to 1706x960 blit and
exclude overlays; they are comparisons across stationary-camera captures, not
exact same-frame summands. Character/foliage animation and any changing light
remain limits. `whitening-statistics.json`, the manifests/capture records and
`analyze_whitening.py` retain the private evidence in `bright-culling-18`.

The dominant blue specular contribution is therefore the sky/sun BRDF branch,
not the cubemap branch or C. It reads VS Location4 colour and multiplies it by
the material BRDF/distance fade and `%2488 = %1622 * %2342 * c6[68].x`. The next
brightness work is to check those shared lighting inputs and BRDF factors against
native shader arithmetic, preserving the material's ordinary lighting and
shadows. Removing the whole branch is not proof that the branch is incorrectly
translated. Both numerical overrides and their hash sidecars are removed after
capture 9; the game remains open in production rendering.

### Mesh pipeline construction implemented; indirect GPU arguments measured

`VkPipeState` now has separate mesh/task shader pointers after its frozen legacy
prefix. `vk_pipeline.c::pre_raster`, `gkey_fill`, `pipe_miss` and
`create_graphics` accept mesh+fragment or task+mesh+fragment. Mesh/task/fragment
resources occupy consecutive binding spans with binding zero shared; module
rebinding and layout coverage use the same offsets. These variants use a
monolithic layout and pipeline; legacy graphics-pipeline-library and persisted
VS/FS key records exclude them. Vertex input/assembly and their dynamic states
are absent, and the ordinary provoking-vertex extension chain is omitted.
Traditional VS/FS and VS/GS/FS construction retain their existing policies.

`vk_mesh_output_limits` validates flat output arrays, effective scalar component
counts, per-vertex/per-primitive allocation granularity, total output memory and
combined payload/output memory against device properties. Primitive index
arrays are excluded from attribute storage. Blocks, packed components and
per-view outputs are rejected by this bounded implementation; ordinary
per-vertex fragment varyings must have matching scalar types/width and enough
components. The rules follow the Vulkan specification's Mesh Shader Output
formula: https://docs.vulkan.org/spec/latest/chapters/interfaces.html . This
is separate from the existing local-size/shared-memory/payload checks. The
private corpus's 36 mesh output interfaces pass under the recorded fixed test
budgets; that check is not actual-device admission for each captured program.

Validation of the new code uses original, public GLSL fixtures, not game code:
`tests/mesh_pipeline_test.py` compiles them, validates their SPIR-V and exercises
the production builder with CPU Vulkan spies (400 checks at O0 and O2). It
checks stage composition, resource rebinding, absence of vertex-only state and
several output/interface rejection cases. `--gpu` builds the same production
builder into a headless executable and creates both pipeline shapes on the
NVIDIA GeForce RTX 5080 with Khronos validation enabled: 15 checks pass and zero
validation errors. This GPU check **creates pipelines only**; it does not draw
or validate native task payload semantics. The ordinary geometry pipeline
suite passes 6,289 checks at both O0 and O2. Binding/client/metadata suites pass
(78 binding and 52 metadata checks per optimization). Runtime compilation and
linking pass. No native mesh draw has been enabled or visual fix claimed.

A new opt-in `DKB_MESH_ARGS_READBACK=1` probe in `vk_dump.c::vk_dump_mesh` reads
only the first two words of each indirect mesh argument buffer during an armed
capture. It uses `vk_exec_read_buffer`, resolving the GPU-visible backing and
performing an ordered copy and split/wait. This perturbs capture timing and
must not be confused with a passive capture or enabled in normal runs. The
raw words are logged without asserting their still-unverified native layout.
The ordinary command log continues to record each direct first/count and caller.
`vk_dump.c::nvn_stage` also now maps GS/TCS/TES/task/mesh resources to their actual
NVN stages instead of labeling every non-VS/FS stage as compute.

After the user confirmed the map and stationary camera, the new session's
`build/oot-fix/bright-mesh-17/commands.txt` contains 41 ordered GPU reads from
25 unique indirect buffers: every prefix is [0,0]. Thus the earlier CPU-zero
observation is now independently reproduced on GPU-visible storage at the draw
position. It still does not prove a failed store: an empty culling input or
legitimate culling could produce this result. The same frame has 13 direct mesh
calls, all first=0, with nonzero task counts between 2 and 32. The game remains
open with console/file logging; no capture was taken during loading.

Remaining work: connect mesh shader lookup/resource binding and draw replay;
validate native first-task and task payload semantics; establish indirect record
layout and trace actual culling inputs/outputs. In particular, `0x0x0` in existing
indirect compute pass headers is a logging placeholder from `op_dispatch_ind`,
which calls `vk_dump_dispatch` with zero dimensions after `vkCmdDispatchIndirect`.
It must **not** be treated as evidence that Vulkan dispatched zero workgroups.
The zero mesh prefixes make the culling producer worth tracing before expecting
that enabling indirect mesh commands alone will restore terrain coverage.

### Same-frame brightness transition and mesh-shadow dependency measured

The user identifies `bright-landscape-16/capture-8/pass_0125_t0` as close to the
intended terrain appearance, and capture 17's pass 153 as the point where it
looks washed out. Capture 8 is restored production rendering: landscape FS
`87657d96d845342b`, native `78e6bf688b49647c`, with no numerical override. This
is a user-provided visual reference, not an original-console measurement.

Pass numbers vary between frames. Capture 8 pass 152 and capture 17 pass 153
are the same fullscreen program 13367, FS `65156d0c69e40c69`, native
`73320e720a317817`. Its inputs are the 1280x720 scene at `0x80003c0200` and the
240x135 bloom image at `0x8000924800`; output is at `0x80010e0c00`. This stage
combines bloom and applies the ACES-like curve. It is not a mesh draw.

A new coverage probe in capture 2 and exact depth matching retain 777,509
landscape pixels in capture 8. Mean linear RGB along that same frame is:

| Stage | Mean RGB |
| --- | --- |
| Material, pass 125 | [0.25760, 0.25704, 0.18781] |
| Last scene input, pass 135 | [0.26182, 0.26046, 0.19193] |
| Tone-map output, pass 152 | [0.39891, 0.41466, 0.29565] |
| CPU tone map without bloom | [0.35514, 0.37256, 0.26584] |
| Predicted display encoding of pass 152 | [0.65149, 0.66368, 0.55782] |

Captured constants are gain 1 and bloom power 1.5. CPU reconstruction of the
unmodified tone map agrees within 1.065 target-storage ULP. The final pass 161
is a sampled copy into sRGB: mean error over its active 1706x960 rectangle is
0.001001 for one sRGB encoding, 0.191128 for two and 0.238076 for none. The
progressively brighter PNGs thus include measured tone mapping and display
encoding; they do not by themselves prove repeated additive blending. These
checks establish agreement with the translated arithmetic, not correct native
lighting/exposure. Do not fix the symptom by disabling required display encoding.

Live controls repeat the separation in this view: capture 9 removes only the
bloom contribution, capture 10 removes only the scene contribution, and capture
11 restores the production shader. Both variants pass `spirv-val`, their hashes
appear in the captures, and their respective CPU equations agree within 1.001
and 1.093 storage ULP. Bloom contributes but does not explain the entire lift.

The missing-mesh hypothesis has a specific input path. In the retained capture
53 from `bright-fog-source-15`, mesh draws are omitted in the three 1600x1600
D16 shadow layers (passes 115-117), as well as scene-colour passes. In the new
capture the shadow array is at `0x80013eda00`; FS `1549cf5eb1f1b52f` samples it
and contributes to the screen mask at `0x8002329c00`, sampled by the landscape
as FS slot 17 (`fp_t_tcb_2A`). Shader inspection distinguishes its channels:
the producer explicitly writes blue = 1; the shadow result reaches alpha.
The initial blue-channel endpoints (captures 3-8) measure an attenuation input
but must **not** be cited as proof of a missing-mesh shadow effect.

Correct alpha-channel endpoints follow in captures 12-17, preserving all
resources and the other sampled channels. Across 763,022 depth-matched terrain
pixels, normal sampled alpha averages 0.96397 (median 0.984375). Forcing alpha
0 versus 1 changes total material mean RGB from [0.05950, 0.04066, 0.01468]
to [0.26154, 0.26179, 0.19135]; the ambient component alone changes from
[0.00593, 0.00423, 0.00092] to [0.14856, 0.15247, 0.07143]. This demonstrates
that this mask can attenuate ambient as well as direct light. It does not
establish the absent meshes' coverage, their actual indirect counts, or how
much shadow the original scene should have. Capture 17 restores production
rendering; both override files and hash sidecars are removed.

Private reproducible evidence is under `build/oot-fix/bright-landscape-16`:
`post-ledger.json`, `post-probe-cpu-check.json`, `mesh-shadow-statistics.json`,
`terrain-probes/*-captures.json` and the probe generation/analysis scripts.
Do not reuse the initial root capture as a same-camera numeric baseline:
only 25,479 pixels retain exact depth when intersected with the full later
sequence. Endpoint comparisons use the matching restored capture instead;
restoration is verified by shader identity, not a self-comparison statistic.

To retain actionable draw evidence in later captures, `vk_dump.c::vk_dump_replay_op`
now records each mesh draw's direct first/count or indirect-buffer address,
plus caller, in `commands.txt`. The previous generic `draw-mesh` line lost
these arguments; the once-per-program startup log may describe another scene.
The change reads packet fields only, without waiting or reading GPU-owned
indirect contents from a stale CPU copy. Runtime compilation passes. This is
a diagnostic improvement, not mesh execution or a new visual correction.

### Task/mesh resource tables and generated Workgroup memory checked

The next integration pass extends stage mappings before enabling execution:
`vk_exec_draw.c::nvn_stage_of` maps translated TS/MS to separate NVN stages 6/7;
`vk_c0_fill` writes their storage descriptor tables at c0+0x110 / c0+0x310;
`ssbo_slot` resolves those same locations against the matching stage's bound
resources. These offsets follow Eglantine's
`shader_recompiler/frontend/maxwell/translate_program.cpp::AddNVNStorageBuffers`
and the local compiler's `CapturedEnvironment::IsCbufStorageBufferPointer`.
They are not an assertion that the rest of the native task/mesh driver header
or first-group semantics is known. c0 memo storage expands to six independent
banks (VS/FS/CS/GS/TS/MS), avoiding reuse between these stage-specific tables.
Pipeline stage-flag mapping now recognizes TASK_BIT_EXT and MESH_BIT_EXT; the
pipeline construction/draw path remains unimplemented.

`tests/mesh_shader_bindings_test.py` exercises the production fill, stage map,
cache-index map and storage-slot resolver: 78 checks at O0 and O2 cover all
16 slots, distinct stage addresses, preserved sizes, misaligned table offsets,
non-driver constant buffers and rejection of the compute-only UBO alias for
mesh. Dispatch-header regressions pass. The indirect-dispatch fixture needed
an inert numerical-override provider and frame counter because the production
function acquired that diagnostic hook in an earlier change; after updating
the fixture its 34 checks pass. No indirect replay semantics were changed for
that fixture correction.

A separate memory admission gap was found: the compiler emits additional ISBE
scratch arrays as Workgroup variables. The native requested shared-memory size
alone omits these allocations. `gfx_shader.c::mesh_spirv` now derives
`GfxShaderMesh.shared_bytes` from the emitted scalar/vector/array declarations
and Workgroup variables, checking for arithmetic overflow. Three-component
vectors receive conservative four-component storage alignment. Unsupported
Workgroup aggregate/pointer layouts fail instead of being assigned zero size.
The emitted non-explicit Eglantine layout is currently flat arrays; this is not
a general SPIR-V layout calculator or support for explicit aliasing.

`vk_mesh_execution_limits` checks shared-memory limits and combined
payload/shared-memory limits with 64-bit arithmetic. Its tests now include
emitted Workgroup scratch and joint-limit violations: 52 checks pass at both
optimizations. All 41 retained scene modules pass the updated validator.
The largest measured Workgroup allocation is 16,916 bytes with a 128-byte
payload (`222d6e81642f3ce8`); other examples include 13,588 and 12,564 bytes.
Thus compiler scratch is material to device admission even where source shared
memory appears small. Private figures are in
`build/oot-fix/mesh-compiler/runtime-shared-sizes.json` and
`runtime-contract-shared-check.json`.

Still required before executing draws: mesh/task module pipeline creation;
per-vertex/per-primitive output memory/interface limits; task/mesh payload
semantics beyond size equality; correct direct/indirect group arguments; and
runtime GPU validation. The limit helper is still not a replay path. These
changes fix prerequisites, not the reported residual brightness, and do not
justify disabling or scaling illumination. No new visual fix is claimed.

### Task/mesh execution metadata now checked against emitted SPIR-V

The runtime now requires mesh metadata version 1. `gfx_shader.c::mesh_json`
parses exact integer fields and Boolean provenance; malformed/duplicate/missing
fields fail. `mesh_spirv` requires one `main` entry point with the correct EXT
execution model, MeshShadingEXT capability, exact literal LocalSize and mesh
output vertex/primitive counts and triangle topology. It also checks the
compiler's current payload ABI: at most one TaskPayloadWorkgroupEXT variable
pointing to an array of unsigned 32-bit words of the declared size. Unknown,
duplicate or mismatched execution modes fail. `gfx_shader_mesh` exposes the
validated metadata to the future pipeline path.

`vk_mesh_contract.h::vk_mesh_execution_limits` provides admission checks for
per-axis and total workgroup sizes, mesh output counts, payload byte limits,
and task/mesh payload size agreement. It is a tested helper, **not yet called
by a mesh pipeline**, because that path still does not exist. It does not yet
check shared memory/output memory bytes or interfaces; those must be reported
and validated before execution is enabled.

Tests: `tests/mesh_shader_contract_test.py` passes 45 original synthetic checks
at O0 and O2, including malformed framing/JSON, mismatched stage/metadata,
workgroup limits, output limits and payload sizes. The same production metadata
validator accepts all 41 regenerated retained scene modules; all 41 also pass
`spirv-val`. Shader-control tests retain 45 checks at each optimization and the
runtime compiles. These checks establish consistency between compiler output
and its reflection; they do not verify the inferred native control ABI or the
visual result. No draw has been newly enabled and no residual brightness fix
is claimed.

### Task/mesh service integration resumed; execution still pending

The next agent's partial implementation preserves original task/mesh stage
pointers in `prog_t::mesh_native`, retains NVN resource bindings for stages 6/7,
enables supported task/mesh Vulkan device features, and adds the experimental
`tools/mesh_xlate/mesh_xlate.cpp` compiler and `gfx_shader_get_mesh` service client.
The client hashes compiler identity, native control/code, stage and resource
answers; translation results use the existing disk/memory reflection path.

Follow-up corrections in this handoff:

- `vk_exec_draw.c::answers_for` indexes AC by NVN stage. AC still had six
  elements after resource tables grew to eight. Resize it to `VK_EXEC_STAGES`
  before allowing task/mesh answer queries.
- A 2048-byte resource JSON buffer could overflow when serializing 32 image
  descriptions. Enlarge it and check every appended resource and the complete
  request length. Reject truncated requests rather than sending malformed JSON.
- Reject null answer pointers and control characters in JSON path strings;
  preserve backslash/quote escaping. Require the exact `ok <requested-key>`
  response before loading a result; a generic `ok` prefix is insufficient.

Validation: production client fixture passes at O0/O2 with full descriptors,
reply mismatch, pipe failure and stage mismatch cases; shader-control tests pass
45 checks at both optimizations; native-stage guard-page and production mesh
recorder tests pass. Runtime compilation and final linking succeed. Implementation commit: `81026bd`. The local compiler target
builds, and replaying 41 task/mesh stages from the retained scene produces 41
SPIR-V modules accepted by `spirv-val`, including 31 stages associated with the
landscape program range. This is translation validation, not visual validation.

**Still missing:** parsing/validating mesh execution metadata against actual
SPIR-V and device limits; a task/mesh pipeline key/layout/module path; resource
binding and support-buffer mapping for the native ABI; direct and indirect draw
execution and task-group first/count semantics; a repeatable clean-checkout
compiler build/package workflow. Inferred control-register interpretation and
payload agreement are not yet verified. `OP_DRAW_MESH` remains skipped. Do not
claim these changes fix brightness or render missing meshes. Preserve the
confirmed flicker fix and continue the material/illumination investigation.

The previous atmospheric investigation also reached capture 63: CPU storage
of the 256x32x16 RGBA16F lookup texture, deswizzled with production functions,
is byte-identical to its GPU dump (1,048,576 bytes, zero differing halfwords).
More precise UV probes reconstruct XY from half high/residual pairs and Z from
11/11/10-bit chunks. CPU trilinear sample RGB means are
[0.449596,0.783614,1.118659], GPU [0.449600,0.783502,1.118636]. Mean absolute RGB
errors are [0.0000832,0.0001981,0.0003381]. No large sampling amplification is
established. Separate frames, coordinate/output quantization and filtering
precision prevent a bitwise parity claim. Captures 55/56 modified an inactive
branch: their accepted shader hashes do not mean the exported measurements ran;
discard them as numerical probes. Evidence is in
`bright-fog-source-15/terrain-probes/sky-lut-upload-reference.json`,
`sky-lut-precise-reference.json` and associated scripts. Capture 63 restored the
original shader and removed probes. No causal residual brightness fix follows
from these checks alone.



### User clarification: darker tonality is closer to the original

The user subsequently clarifies that the darker appearance is closer to the
original game and that the original does not have this much glow. Treat excessive
brightening as the reported defect; do not propose brightening the rocks to match
the washed-out terrain. This is the user's visual reference, not a captured
console measurement or proof that every value in an earlier dark debug pass is
correct. Preserve shadow/detail contrast while locating excessive illumination,
bloom or exposure, and distinguish a causal renderer fix from an arbitrary dark
filter. Earlier notes about lack of an independent console capture still apply.

### Terrain: albedo is correct, the whitening is added by lighting

The user's screenshot shows the stone with correct colour and the landscape milky
and flat. Probes in captures 64-71 (fresh magenta tag in each run; terrain FS
`87657d96d845342b`; generators `make_sky_fraction.py`, `make_specular_probes.py`,
runner `run_sky_fraction.py <set>`):

- **Albedo.** The blended landscape albedo (`%1332/%1338/%1336`), encoded to sRGB
  for viewing (`albedo-srgb.png`, from capture 33), is saturated olive grass with
  dark soil, at the same tonality as the rocks. The texture entering lighting is
  not washed out, so the milky look is added afterwards.
- **Baked sky visibility works.** The terrain VS multiplies the dynamic sky SH by a
  baked visibility SH (BC6H layers 2/3, x2) and adds baked layers 0/1 and a layer-4
  term (`terrain-vs.spvasm`, outputs `loc0.w/loc1.w/loc2.w` = ambient L0 per
  channel). `loc*.w / fp_s2 sky L0` over 752,424 depth-matched pixels (capture 65)
  has mean [0.910,0.609,0.339], spatially smooth and falling toward the rocks
  (`sky-fraction.png`). It is not stuck at open sky.
- **Specular (term D) is about 20% of red.** Over about 751,000 pixels (captures
  68-70): sky reflection red `%2494*%2506` = 0.017, sun specular red `%2432*%2488`
  = 0.040, roughness `%1556` = 0.56, `(1-r)^4` = 0.037, cube multiplier `%2506` =
  7.5 (`c33[2].y` = 8 times `%1622` = 0.94). The cubemap is a game-rendered
  `R11G11B10F`, so its alpha reads 1 on hardware too. Restored terrain mean in
  this view: [0.263,0.262,0.193].

**Per-term split in one view** (second game session: tag in the folder root,
probes in `capture-3`..`capture-8`; the first session's `capture-2`..`capture-12`
were renamed `capture-N-s1`, see `renamed-captures.json`, because the dumper
numbers captures per session and overwrites). Over 740,604 pixels the four terms
sum to the final image within 0.006:

| Term | Mean RGB |
| --- | --- |
| A, direct sun (mostly shaded here) | [0.039,0.027,0.006] |
| B, ambient (tinted by albedo, 1.6-2.7x) | [0.145,0.149,0.070] |
| C, fog | [0.016,0.012,0.008] |
| D, specular, **not** tinted by albedo | [0.057,0.071,0.107] |
| Final | [0.257,0.258,0.191] |
| Albedo | [0.100,0.091,0.028] |

D is a uniform bluish-grey sheet over the ground (`view-grid.png`: albedo, final,
B, D) and makes 56% of the final blue: that is the milky veil. Its two parts:

- Cube reflection: cube 753 holds about [0.16,0.12,0.07] (warm, stored at 1/8 of
  radiance, written per face by passes 70-85) and is scaled back by `c7[0x24]` = 8
  (cbuf 7 row 2 = [8,8,0,0], game data). Not bluish.
- GGX specular lit by the VS sky-SH colour (`loc4`) with a 100-150 distance fade
  (native 0x9260-0x9730: Schlick `exp2((-5.55473 VoH - 6.98316) VoH)`, F0 0.04, GGX
  with 1/pi, Smith). The native disassembly (`terrain-fs-native.sass`, nvdisasm
  SM86, 2608 instructions) matches the translated structure; the size is
  physically plausible for a dielectric under this sky (about 0.04 x sky radiance x
  visibility).

So the terrain shader looks faithful. What differs from the original is likely
what is **not drawn over it**: in this frame (capture-8) the landscape's
task/mesh programs 4034-4095, numbered next to the terrain's program 4022, are
dropped as "mesh tasks indirect not replayed" in the HDR scene passes 96 (3),
97 (8) and 130 (13, just before terrain pass 131), and in the shadow maps 110-112.
In a forest scene this is consistent with missing ground cover (grass) and its
shadows, which would hide the bare, slightly glossy soil. Most of their mesh
stages already emit valid SPIR-V in the offline prototype (`candidate-sweep.json`;
`10eff7e5af4f0329` and `6ffa56c9dbe1e819` fail). The runtime side (Vulkan mesh
features, pipelines, resource reflection, replay) is open item 3 below.

Confirmation of what these programs draw (capture-8, native fragment stages
disassembled with nvdisasm SM86):

- Passes 96/97: fragment stages use `KILL` with 1-2 textures, an alpha-tested
  depth pre-pass (cut-out foliage).
- Pass 130, just before the terrain: the same family with full material
  fragments (9-11 textures, no `KILL`), their colour pass.
- Passes 110-112: the same programs in the shadow maps.
- Two program shapes: fragment + mesh (kind 7 already lowered, for example 4053
  `f7487f1d1a380d92`) and fragment + task (kind 6) + mesh (kind 7 unlowered, for
  example 4095). All landscape draws are indirect. Their argument buffers read
  zero from CPU memory (for example 4053 at `0x2a01e4000`): they are written on the
  GPU by the culling chain at the start of the frame (passes 0-3, programs
  13467-13475, logged as `dispatch 0x0x0`), which binds the same buffers as storage.

Implementation stages, each testable on its own:

1. Translation: turn the prototype into a mesh/task translator that also emits
   the runtime's reflection JSON (cbufs, sbufs, textures, images with handles), as
   `totk_xlate` does, and route SPH type 3 stages of mesh programs to it.
2. Device: query and enable `VK_EXT_mesh_shader` (task and mesh) and its limits.
3. Pipelines: build task/mesh/fragment pipelines without vertex input, with task
   and mesh stages in descriptor layouts and barriers.
4. Replay: `OP_DRAW_MESH` to `vkCmdDrawMeshTasksEXT` / `...IndirectEXT`, after
   confirming NVN 2's indirect argument layout against the GPU buffer contents.
5. Validate in this forest view: grass present, shadows, and the terrain veil.

**Stage 1 done: `tools/mesh_xlate/mesh_xlate.cpp`.** It speaks `totk_xlate`'s
`--serve` protocol with stages `ts`/`ms` (job: key, stage, control, code, tex,
img) and writes `<key>.spv` plus a `.res.json` in the runtime's reflection format,
with an extra `mesh` object (workgroup size, payload words, max vertices and
primitives, topology, the inferred registers). Bindings start at 1 in Eglantine's
emission order (unified binding, no descriptor aliasing). Texture types come from
the job's slot tables: `ReadCbufValue` on c2 returns a tag carrying the offset,
and `ReadTextureType` maps it to `totk_xlate`'s slot. Separate texture/sampler
pairs and texture/image buffers are rejected for now. It builds against the
prototype's private dependency tree (`build/oot-fix/mesh-compiler/CMakeLists.txt`,
target `mesh_xlate`). `build/oot-fix/mesh-compiler/check_mesh_xlate.py` translates
every task/mesh stage of capture-8's dropped draws: **41 of 41 valid under
`spirv-val --target-env vulkan1.3`**, including all 31 landscape stages and
`10eff7e5af4f0329` (program 4070), which the prototype rejected because it
samples a texture. The landscape mesh stages read 3-6 constant buffers, some one
storage buffer or one texture; workgroups of 32, up to 160 vertices and 128
primitives.

### Sky input chain checked against the native shader

The sky image (texture 744) that feeds the dynamic SH producer is now checked
end to end, from game bytes to the rendered output. All three checks agree;
none of them shows a translation or upload error.

1. **Upload.** The live texture registry gives texture 742's descriptor
   (3D RGBA16F 256x32x16, one level, tile depth 4) and its CPU-backed storage at
   `0x25f47b000`, read without writes to the process. Deswizzling those bytes with
   the production `nvn_deswizzle` gives exactly the bytes the GPU pass sampled
   (SHA-256 `f0eb8860…`, 0 differing half words). This shares the production
   layout code, so it does not independently prove the NVN hardware layout.
2. **Sampling.** Probes that export the translated shader's fetch coordinates and
   sample (captures 58/59; XY rebuilt from half-precision high/residual pairs and Z
   rebuilt bit-exactly from 11/11/10-bit chunks in captures 61/62) agree with CPU
   trilinear filtering of the same LUT: mean error 0.01-0.03%, relative p99 0.37%.
   The bit-exact Z, 0.0331039, corresponds to a camera about 120 m above the
   6360 km ground radius.
3. **Whole shader.** `emulate_sky_native.py` transcribes the native SASS
   (`5638f0cfa2897d74`, nvdisasm, `sky-input-native.sass`) into float32 numpy.
   The vertex shader writes constant attributes (`r`, the horizon cosine and the
   horizon cosine + 1/256), so the native program can be evaluated from pixel
   position, the printed UBO rows and the captured LUT. The shader is a Bruneton-style
   precomputed inscatter lookup: two 3D fetches interpolated along the view-sun
   angle, Mie rebuilt as `C.rgb*C.a/max(C.r,1e-4)*betaR.r/betaR`, Rayleigh phase
   `3/(16pi)(1+nu^2)*c4[0x14]`, Cornette-Shanks Mie phase `*c4[0x18]` with
   `g = c4[0x10]*max(sat(2u-0.25), sat(2v-0.25))`, then a light colour. Against
   capture 53:

   | Pass | LUT | Colour path | GPU mean RGB | Native emulation | Rel. error p50 / p99 | Alpha |
   | --- | --- | --- | --- | --- | --- | --- |
   | 70 | 742 | `c4[0x78]` set: ramps rows 8-14 x `c4[0x2c]` | [30.409,21.926,15.328] | [30.476,21.964,15.342] | 0.2% / 0.37% | exact |
   | 71 | 748 | `c4[0x78]` clear: row 3 | [30.409,21.924,15.405] | [30.476,21.962,15.419] | 0.2% / 0.38% | exact |

   The +0.2% bias is consistent with the six-digit printed `c4[3].w`: it gives
   Z = 0.033119 against the bit-exact 0.0331039.

The sky's brightness therefore comes from the game's own inputs, chiefly the
light colour of about 78 (`c4[0x2c]` and row 3 = [78.125,76.5625,71.875]) and the
Mie scale 50, not from the translator. Combined with the SH check below, the path
LUT -> sky image -> SH L0 reproduces the native arithmetic. Do not scale the sky,
the LUT or the SH to darken the scene. The remaining suspects are downstream:
how the terrain material weights the SH and baked terms, and the exposure and
bloom passes. Whether the game sets these UBO values the same way on hardware
is not checked: there is no console capture, and the values were printed at
record time.

Artifacts in `bright-fog-source-15/terrain-probes`: `sky-lut-live-descriptor.json`,
`sky-lut-upload-reference.json`, `sky-lut-reference.json`,
`sky-lut-precise-reference.json`, `emulate_sky_native.py`,
`sky-native-emulation-capture-53-70.json` and `-71.json`. The running game
was not modified; no production change comes from this step.

### Dynamic SH producer isolated and independently checked

Captures 49-54 continue from the coefficient localization. A compute override
for translated `21238adc0f99baaf` replaces the desired value in both generated
atomic-CAS helpers with the compared value. Successful CAS then retains the
old bits: the dispatch executes but contributes no float additions. This is a
reversible diagnostic, not a renderer correction. The terrain FS is held at
its term-B-only probe throughout captures 49-51.

| Capture | State | Mean term-B RGB on depth-matched terrain |
| --- | --- | --- |
| 49 | Original producer | [0.142634,0.147088,0.068654] |
| 50 | CAS additions suppressed | [0.062233,0.038134,0.003512] |
| 51 | Original producer restored | [0.142690,0.147151,0.068671] |

Each retains about 675,000 pixels from the capture-32 mask with identical depth.
Both FS and CS replacement hashes are checked in `passes.txt`. The restoration
reproduces the baseline closely. Thus this producer causally contributes a
large part of B, but this does not prove the contribution is erroneous. The
baked/other terms persist when it is disabled. Capture 52 restores normal FS/CS.

Capture 53 reads L0 RGB directly through the material's existing `fp_s2` buffer
at words 12/13/14 (bytes +0x30/+0x34/+0x38). The median, which rejects a small
number of later overwritten pixels, is [6.4375,8.375,12.75] in R11G11B10F.
A CPU calculation independently reconstructs the producer's 1,024 Fibonacci
sphere directions, source UVs and bilinear samples from **the same capture's**
256x256 RGBA16F input, applies RGB*alpha, and integrates L0 with
4*pi*0.282094985. Its 8-bit-subtexel-filter reference is
[6.4939748,8.3870776,12.9123989]. Differences are
[0.904,0.097,0.650] units of the diagnostic output's R11/R10 precision. The
ideal-filter reference gives essentially the same conclusion.

This checks magnitude and normalization against captured inputs. It does not
prove bitwise F32 agreement, correct original radiance, or instruction-level
parity with the native shader: printed UBO values are rounded, trigonometry and
filter precision differ, and the diagnostic output is quantized. There is no
large new normalization error established here, and no justification for
removing the producer or adding a global SH multiplier.

Trace upstream from this point:

- The 1x1x1 producer samples texture 744, RGBA16F 256x256, address
  `0x25f585000`. Captured constants c3 rows 0-3 specify direction
  [-0.283634,0.774503,0.565418], gain 1 and zero alternate-colour weight.
- Capture 53 pass 70 produces that image using FS `87db63664e509b73`, native
  `5638f0cfa2897d74`, from a 3D RGBA16F atmospheric lookup texture 742,
  256x32x16 at `0x25f47b000`. It is CPU-backed and had one upload. The image's
  full mean is [30.4092,21.9256,15.3284,0.503906]; alpha is in [0,1].
  These unweighted means are not the integrated sky mean: the sphere samples
  and alpha weighting are essential.
- The terrain VS also reads a separate six-layer BC6H signed-float texture,
  260x260. Its baked lighting and visibility must be distinguished from the
  dynamic coefficient buffer before any material fix.

NVIDIA nvdisasm 13.0.85 independently disassembles the captured terrain VS after
removing its 0x80-byte SPH and stopping after EXIT (the following bytes are
constant data, not instructions). `terrain-vs-native.sass` retains this audit.
No native/translated arithmetic discrepancy has yet been demonstrated by it.

Artifacts in `bright-fog-source-15/terrain-probes`: `dynamic-sh-manifest.json`,
`dynamic-sh-captures.json`, `dynamic-sh-statistics.json`, `global-sh-captures.json`,
`global-sh-statistics.json`, `compute_sh_reference.py`, `ambient-sky-input.spvasm`.
Capture 53 contains `global-sh-cpu-reference.json` and
`global-sh-reference-comparison.json`. Capture 54 confirms the original terrain
shader is restored. All live probe files are removed; the pending-counter fix
remains active. No additional production brightness fix is claimed.

Upstream recheck: `nx2` has new commits `b06d1818eeec` (scoped performance,
synchronization and optional FSR3 changes) and `55532f2d8142` (built-in Dynamic FPS
patch). `fix/oot-fog` remains at `8390649c0c06`; `oot-port` still returns 404.
These are not a demonstrated correction for the measured SH/landscape problem.
Metadata and reviewed file lists are stored in `upstream-fog-audit/latest-*.json`
and `commit-*.json`. Primary sources:
https://github.com/zortos293/Lichii/commit/b06d1818eeec
https://github.com/zortos293/Lichii/commit/55532f2d8142

### Term B narrowed to evaluated vertex lighting coefficients

Follow-up captures 40-48 separate term B's sources. The valid range/gain probe
(capture 41) exports 2492=1 everywhere, 2484=0 everywhere, and 1622 in
[0.1953125,1]. Thus the cubemap-diffuse product in term B has a zero multiplier
in this configuration. This does not exclude the separate reflection term D.

**Discard the component decompositions in captures 40, 43 and 45.** Capture 40
uses 2554 instead of 2548; captures 43/45 still apply 2548 unconditionally even
though the original multiplies by it only inside the branch controlled by 1410.
These flawed probes falsely zero the component when that branch is inactive.
Do not use them to conclude that SH is absent or uniform ambient dominates.
The standalone capture-41 gains remain valid.

Capture 47 fixes that control flow: select 2548 when 1410 is true, otherwise 1.
It exports red `1486*2480*1606*selected_multiplier`, green `2518-red`, and blue
2480. Over 673,483 depth-matched terrain pixels, channel means are
[0.1363423,0.00596735,0.9332554]. The first product accounts for approximately
96% of the decomposed red B contribution and about half of total terrain red
in this view. The residual includes the uniform hemisphere term; the cubemap
product is disabled by the independently measured 2484=0. Separate-frame
comparisons and R11/R10 rounding prevent exact component-sum claims.

This is a source location, **not proof that those coefficients are incorrect**.
The forward trace to investigate next is:

- FS 1486 is an adjusted red SH-related evaluation. Its construction uses
  1384, 1420, 1442 and 1468; 1384 includes input location 0.w times 0.282095,
  while 1420 includes a vector length times 0.162868.
- VS translated `ccd5309a814e89c6`, native `6b8da6ef1d54cdaa`, constructs output
  locations 0/1/2/4 from coefficient variables 194-236, with SH basis factors
  0.282094806 and 0.325735092. It reads 12 scalar words through `vp_s0`, plus
  uniform/texture inputs. Trace these coefficient inputs and their producer,
  rather than scaling the final fragment shader.
- FS 2480 = 1624 * 2342 * c6[68].x. This measured scale averages 0.9333 and
  stays at or below 1; no factor above one was measured here. FS 1606 is the
  red material response. Verify coefficient decoding/normalization and the
  relation between the native and translated instructions before any fix.

An optional read-only CPU sample of the capture-46 c6 address shows row 68
[1,1,0,0], row 73 [0.0625,0.0492647,0.0350490,0.0625], row 74
[0.015625,0.015625,0.015625,0.0625]. This is **later CPU memory**, not the GPU
snapshot or proof of what a particular draw consumed. It must not replace a
shader-side measurement.

Evidence: `terrain-probes/environment-captures.json`,
`environment-statistics.json` (invalid split explicitly marked),
`ambient-corrected-captures.json`, `ambient-corrected-statistics.json`,
`ambient-cpu-rows.json`, and `terrain-vs.spvasm`. Capture 48 restores the original
terrain shader; all probe files are removed. No production brightness change
has been made from this localization alone.

### Landscape albedo, blend weights and lighting inputs measured live

Captures 32-39 extend the material investigation without restarting the loaded
game. Every replacement SPIR-V validates, preserves resource declarations, and
is confirmed by its captured shader hash. Capture 32 tags 678,417 terrain pixels;
subsequent comparisons retain only identical master-depth words. This is a fresh
mask; the earlier capture-29 mask must not be reused after camera changes.

- Capture 33 exports blended texture RGB (variables 1332/1338/1336). Over 677,895
  depth-matched pixels, mean is [0.100791,0.091659,0.027797], maximum
  [0.402344,0.292969,0.230469], with no nonfinite values. The terrain texture
  entering lighting is dark in this view; it is not already white.
- Capture 36 exports the later material RGB (1506/1502/1508). Its mean is
  [0.100768,0.091636,0.027806] over 672,841 pixels. These separate-frame means
  are nearly unchanged; they do not establish bitwise equality or correct
  original albedo sampling.
- Capture 34's encoded weights show small negative decoded values near zero
  because of R11/R10 storage precision. Some tagged pixels also become covered
  by other draws despite unchanged depth. Do not treat its raw extrema or
  outside-[0,1] count as proof of invalid weights.
- Capture 38 instead evaluates two weight-range predicates inside the shader,
  before output quantization (1244/1260 outside [-0.001,1.001]), and exports a
  blue-one marker. All 673,768 depth-matched pixels retaining the marker and
  Boolean-valued channels report **zero invalid weights**. This excludes large
  blend extrapolation in these tested pixels; it is not exact normalization.
- Capture 37 packs the red contributions A, B and D into RGB. Means are
  [0.037623,0.142268,0.060624] over 672,348 pixels. Restored capture 39 has mean
  [0.250479,0.255225,0.195421] over its depth-matched terrain. Thus B/D, which
  include environment/material terms, are the next useful source to trace;
  their size alone does not prove excess or incorrect cubemap values. Do not
  call B pure cubemap diffuse: it also includes uniform/other ambient terms.

Artifacts: `bright-fog-source-15/terrain-probes/albedo-captures.json`,
`albedo-statistics.json`, `lighting-input-captures.json`,
`lighting-input-statistics.json`, and the SPIR-V probes and runner scripts.
Diagnostic variants are temporary; restored captures 35/39 confirm the original
terrain shader. The pending-counter fix stays active. No production brightness
scale or material-hash workaround was added.

### Initial landscape texture colour-space audit

Capture 31 binds landscape albedo texture 705 as a 90-layer 1365x1365
`VK_FORMAT_R8G8B8A8_SRGB` array, from NVN ASTC sRGB format `0x8e`.
The parameter/normal array 706 is separately bound as RGBA8 UNORM from NVN
format `0x80`. `runtime/vk_formats.c::tex_vk_format` preserves this distinction
when decoding ASTC, and `runtime/vk_texture.c::upload` passes `astc_srgb` into
the decoder. This initial descriptor/source audit does not show albedo being
bound as linear UNORM. It is not a numerical validation of sampled albedo,
array-layer selection, mip filtering or the decoded texture contents; those
remain possible inputs to examine before changing material illumination.

### Same-frame interval and display audit; rock darkness is not a reference

The user clarified that the observation about dark rocks came from these PC
captures, not a console comparison. They describe the original scene's tonality
as consistent across ground and stone. Do not assume the rocks are correct, or
that landscape alone must be scaled down; compare shared lighting/exposure and
material handling without assigning either material a reference truth.

New-angle captures 28?31 establish a fresh landscape mask of 770,628 tagged
pixels. Depth identity retains 770,180 of these pixels in the zero-red probe
(capture 30): **every retained pixel has exactly zero red**. The ordinary red
mean is about 0.267?0.268 in captures 28/31. This excludes retained destination
red/additive framebuffer blending for these landscape pixels. The nonzero red
reported under the obsolete capture-15 mask was not evidence of blending.

For capture 31's 769,843 depth-matched landscape pixels, mean HDR RGB is:

| Pass | Mean RGB | Role |
| --- | --- | --- |
| 124 | [0.26836,0.26413,0.18542] | Landscape material already drawn |
| 126 | [0.27402,0.26877,0.19081] | Fullscreen composition |
| 130 | [0.27411,0.26885,0.19077] | Later scene effects |
| 132 | [0.27412,0.26887,0.19076] | Later scene effects |
| 134 | [0.27418,0.26901,0.19090] | Last HDR scene input |
| 151 | [0.41457,0.42586,0.29285] | Tone-map output |

The same frame's normalized linear-depth producer (pass 93, FS
`3a8b1e7127262cef`) reads the master depth at `0x8000000000`. Decoding its output
with the captured near/far parameters gives landscape distances 4.26?9.86,
median 5.97. They match `near*far/(far-depth*(far-near))` from the landscape's
master depth, with maximum absolute error 1.60e-5. Thus no stale far-plane depth
is measured on these pixels; the earlier different-attachment suspicion is not
confirmed here. The composition increases their mean channels by about 0.005,
so broad fog statistics from a different view cannot explain all perceived
landscape brightness in this near-ground view.

A same-frame CPU replay of pass 151 reproduces captured output within 1.077 HDR
storage ULP. Gain=1, bloom power=1.5. Over this mask, the predicted tone-map mean
without bloom is [0.36947,0.38296,0.26359]; bloom adds approximately
[0.04742,0.04534,0.03252]. No masked pixel has all output channels above 0.9.
The stage does brighten midtones, but the measured arithmetic matches the
translated shader. This does not establish correct original exposure or
pre-tonemap input radiance. Linear-HDR debug PNGs and tone-mapped debug PNGs
are not equally display-ready images, so their visual brightness difference is
not sufficient evidence of an extra additive pass.

The linear upscaler's source snapshot is byte-identical to the pass-151 tone-map
output in this frame. The final display shader `3444fa67f4a6dd2e` is a sampled
copy. Comparing its final 8-bit PNG to pass 159's HDR source over the active
1706x960 rectangle gives mean channel error 0.001001 for **one sRGB encode**,
versus 0.19083 for two encodes and 0.23678 for no encode. Maximum one-encode
error is 0.002353. There is no measured duplicate display gamma here. Host colour
settings are default (brightness=0, contrast/saturation=100, gamma=1).

Evidence under `bright-fog-source-15/terrain-probes`:
`new-angle-blending-statistics.json`, `new-angle-depth-and-composition.json`,
`interval-126-151-landscape.json`, `check_current_tonemap.py`; capture 31 also
contains `landscape-tonemap-bloom-reference.json` and
`display-encoding-reference.json`. The user-provided TIGSource terrain-blending
reference https://forums.tigsource.com/index.php?topic=67068.0 returns HTTP 403
through both browser and direct read. Its contents have not been verified and
must not be cited as support for this game's blending technique.

All diagnostic variants and skip lists are restored. The pending-counter fix
remains active. No extra illumination/exposure/gamma correction is justified by
this audit alone; remaining work is to establish wrong shared lighting/material
inputs or a native-to-translated arithmetic mismatch, rather than treating the
rocks as ground truth or scaling a single landscape shader by hash.

### Landscape shader and the user-identified passes 173 / 175 / 176

The user narrowed the remaining washed-out appearance to landscape and described
an emissive-like contribution while shadows remain visible. Coverage probes confirm
native FS `78e6bf688b49647c`, translated `87657d96d845342b`, program 4022 (VS native
`6b8da6ef1d54cdaa`) covers continuous soil between stone paths and rocks. It reads
1365x1365, 90-layer albedo/parameter arrays and 260x260 page textures; this is a
separate path from the stone material. Capture 13 had too little terrain visible.
After the user repositioned and confirmed readiness, capture 15 tags 349,193
pixels magenta while preserving output alpha; capture 16 restores the material.

The final colour sums four terms. Separate-frame probes 17?20 yield approximate
RGB means over that tag mask: A [0.25750,0.16530,0.03822], B
[0.20157,0.17568,0.10685], C [0.03411,0.02521,0.01634], D
[0.07049,0.08204,0.11583]. Original capture 21 averages
[0.56072,0.44458,0.27557]. A/C include per-light loop outputs; B/D include
normal/reflection-direction environment-cube samples, material/ambient constants
and other terms. These are not proven pure diffuse/specular/emission categories.
Environment light can coexist with direct shadows, consistent with the user's
observation, but its size alone does not prove a material bug. No term is
uniformly white or nonfinite. Do not add separate-frame probe values as exact
per-pixel summands. Their manifests and statistics are in
`bright-fog-source-15/terrain-probes/`.

Capture 22 probes transformed normal length squared and private material inputs
1510/1514. Their means are [0.99046,0.75456,0.49669], with length-squared 99th
percentile 1.0. There is no measured normal explosion. Semantic names of the
material inputs are not established; do not infer an erroneous metalness=0.5.
Original output is restored in capture 23. Later zero-red captures 24/25 and
composite-skip captures 26/27 have different framing from capture 15. Results
under that old tag mask cannot prove destination blending or exposure changes.
No permanent material multiplier or emission suppression is applied.

The user specifically identified `capture-27/pass_0175_t0` and `pass_0173_td`.
A same-capture comparison shows all 56,767 HDR words retaining sky pass 159 in
pass 175 are replaced in pass 176; no exact sky word remains after 176 in this
view. All replaced pixels have depth below 1 in pass 176. Of these pixels, 21,549
had clear depth 1 in the earlier pass 173. Importantly, pass 173 attaches depth
at `0x8000960e00`, while pass 176 attaches **a different depth image** at
`0x8000000000`. Pass 176 contains four landscape draws plus other geometry.
Thus the earlier white holes are an incomplete intermediate stage, not proof
of a permanently missing final terrain mesh or an emissive output. This does
not refute the user's separate observation of excessive final brightness.
Private exact-word evidence: `capture-27/landscape-transition-175-176.json`.

The user has now found a framing covering roughly 90% landscape. New captures
will establish a fresh coverage mask before testing source-red=0 so stale masks
cannot turn other objects into false evidence of additive blending.

A separate check revisits the prior reasoning about the one-group compute:
reading only 64 texels does not itself rule out exposure estimation. Actual
SPIR-V of CS `8d148ebc33d1bdc1` fetches **alpha**, multiplies by 1000, converts to
integer, reduces and atomically updates a signed minimum in the buffer at
`0x25a4c7fc0`; it does not read RGB or log luminance. That dataflow, rather than
sample count, supports excluding this particular compute from auto-exposure.

### Compute producer probes: solar term dominates the remaining scattering

The user confirmed the loaded map for `bright-fog-source-15`. Original and
probe dispatch identities were checked in each accepted capture. Compute probe
selection initially matched a cached instance used before reaching the map;
the live map used a different GfxShader object with identical SPIR-V. Capture 2
therefore retained the original shader and is not an accepted no-history probe.
Read-only inspection of the diagnostic context confirmed the old base pointer.
Changing the hash sidecar away and back after at least 75 actual presented frames
retargeted the active map instance without restarting. This is a limitation of
the current pointer-based diagnostic selection, not evidence of a lighting fault.

Accepted source-volume results (RGB means, before slice integration):

| Capture | Producer output | Mean RGB |
| --- | --- | --- |
| root | Normal with temporal interpolation | [0.010089, 0.008547, 0.007238] |
| 3 | Current scattering without history | [0.009111, 0.007711, 0.006672] |
| 7 | Direct-solar expressions | [0.009279, 0.007668, 0.006311] |
| 8 | Current scattering minus direct solar | [0.0001143, 0.0002856, 0.0005675] |
| 10 | Original producer restored | [0.009798, 0.008245, 0.007038] |

All these volume outputs are finite. Captures are distinct frames and temporal
probes feed later histories, so do not claim that table rows form an exact RGB
sum, that a small mean difference is a causal effect, or that history has been
proven correct. The no-history sample is close to normal; no temporal runaway is
measured. Captures 4 and 5 selected an incomplete blue solar expression (%13019
instead of complete FMA %13020); their blue measurements are withdrawn and the
corrected probes were revalidated with spirv-val for captures 7 and 8.

To avoid adding different frames, capture 9 writes the red direct-solar term,
red remainder and red total into the three channels of the same volume. Their
means are [0.00928786, 0.000115538, 0.00935147]. Channels have unequal mantissa
precision in R11G11B10F, so the quantized means do not add exactly. Direct solar
is about 99% of the red source in this sample. This identifies the dominant
arithmetic terms, not a proof that the game's solar intensity is incorrect.

Capture 11 writes two shared-memory visibility-like factors and the solar
attenuation input. Their means are [0.0991201, 0.492033, 0.510734], maxima
[1,1,0.984375]. Neither visibility factor is stuck at all-visible. Static tracing
finds the +16 and +32 shared factors are each averages of two clamped one-minus
values. The solar term also reads the CPU-provided sun RGB and atmospheric
coefficients, and includes the expected 1/(4*pi) coefficient in the surrounding
phase-function arithmetic. No arbitrary sun/density clamp is justified by these
results. The exact defect producing excessive solar scattering remains open.

Capture 12 verifies original compute hash `87d0408af1b43d55` after removing the
probe, and both fragment/compute probe files and sidecars are absent. The normal
condition file remains 0; fragment skip list is empty. The game remains open.
No permanent brightness patch was implemented by these diagnostic variants.
The previously verified pending-counter correction remains enabled.

Evidence: `bright-fog-source-15/compute-component-captures.json`,
`compute-component-statistics.json`, `run_components_corrected.py` and accepted
capture folders. Private binary probes preserve original declarations and live
only in ignored `build/`. Next trace the direct-solar phase/density coefficients
against native instructions or a console reference, and examine shadow caster
coverage; do not infer a missing-shadow root cause solely from absent foliage
or from the numerical dominance of the solar term.

### Live decomposition: fog adds light; source tracking is the next unresolved stage

Accepted fragment probes preserve every original declaration and numeric ID by
editing instruction words in ignored diagnostic SPIR-V, then pass spirv-val.
Early assembler-generated variants renumbered declarations and were rejected by
`gfx_probe_declarations`; captures 5?12 are **not** component measurements.
Capture 14 still used the prior scene probe because fewer than 60 presented
frames elapsed between edits; its requested pre-fog measurement is also invalid.
Subsequent captures wait at least 75 actual presented frames, read passively
from `gfx_counters.frames` through read-only process memory, and require the
expected probe hash in passes.txt before recording success. Waits poll below one
second; wall-clock sleep alone is insufficient at low frame rate. All probes
are removed in finally blocks and no generated game code is edited.

Accepted evidence under `bright-coverage-fixed-14/fog-components`:

- Capture 13: returning only the sampled scene gives all 921,600 HDR output words
  exactly equal to the preceding same-frame scene output. This rules out an
  additional destination blend or wrong screen coordinate for this probe.
- Capture 15: pre-fog RGB mean [0.68498, 0.47278, 0.20839], against its same-frame
  input [0.68806, 0.47247, 0.20742]. Thus earlier internal terms change little here.
- Capture 16: post-fog RGB mean [0.98515, 0.73474, 0.43921], against same-frame input
  [0.68119, 0.46776, 0.20522]. These are separate probes, not same-frame summands.
- Capture 17: the final layer-alpha gates are both exactly 1; the fog branch flag
  is true at 97.94% of pixels. Two half-resolution composite inputs in capture 22
  have zero RGB and alpha 1 at their preceding attachment writes.
- Captures 18/19: live volume addressing constants are near=1, far=500,
  exponent=1 and normalized extent multipliers [1,1,1]. No wrong c4 row is found.
- Capture 20: RGB transmission means [0.93304, 0.92496, 0.90941], maxima 1, minima
  [0.43359, 0.63281, 0.54688]. No transmission amplification above 1 is measured.
- Capture 21: the additive scattering term, computed in the same shader as
  `postFog - transmission * preFog`, has RGB mean [0.33480, 0.29202, 0.25092] and
  maxima [1.6875,1.390625,1.125]. It explains the increase without a duplicate
  framebuffer blend. A correct console reference for this term is still absent.
- Capture 22: the scalar surface-distance input ranges approximately 2.47?52.5
  in this camera position; the comparison distance ranges 4.375?25088. Values are
  rounded by the diagnostic HDR output, not exact guest float constants.

Nine accepted captures' integrated-scattering red means remain around 0.305?0.329.
There is no measured temporal runaway in these samples. The compute shader's
history expression is a weighted interpolation, not simply current+history.
The producer's sunlight, ambient/local terms and history still require isolation;
none of the above proves physical fog brightness correct or identifies a
permanent brightness fix. Sampling integrated producer outputs and tracing their
arithmetic is the next step, not arbitrarily scaling density/exposure.

For that step, `gfx_shader.c::shader_debug_override` now shares the existing
capture-only declaration guard, bounded resident variants and file-removal restore
logic between independent fragment and compute probe contexts. New opt-in
`DKB_CS_OVERRIDE_HASH` / `DKB_CS_OVERRIDE_FILE` hooks apply in direct and indirect
compute replay before pipeline lookup; default execution is unchanged. Original
reflection is retained, so probes must preserve descriptors, stage declarations
and execution modes. Compute variants inherit original shader/c0 metadata, just
like fragment variants. The file hash must be verified in the captured dispatch.
Three validated private compute probes are prepared: current scattering without
history, direct-sun terms, and the remaining ambient/local term. They are diagnostic
variants of `87d0408af1b43d55`, not committed shader replacements.

Validation: shader control/profile tests pass 45 checks at both O0 and O2;
`tests/gfx_shader_probe_test.c` declaration-guard test passes with warnings as
errors; incremental runtime compile and link pass. The next launcher is
`Play OOT fog source 15.cmd`, output `bright-fog-source-15`, with normal conditions,
no active probe and visible console/file logging. Loaded-map confirmation is
required again after installing that diagnostic build. The first startup exited before
measurement with no probe active; its log is retained as
`bright-fog-source-15/startup-attempt-1.log`. The second launch has a responding
DKB Recomp window. This recurring startup instability is not attributed to the
new compute override, which is inactive until a probe file is installed.

Upstream audit: the `oot-port` branch query returns 404 at this check; existing
`fix/oot-fog` head is `8390649c0c06`. Shakoquo's release text claims the white veil
is fixed, but its actual `SHARKIIX.md` at `4243c082f593` recommends rejecting ground
mist and the fog/light-shaft composite. That is a suppression workaround, not a
causal correction to import. No code was copied. Audit files are under
`build/oot-fix/upstream-fog-audit`; source links:
https://github.com/Shakoquo/Lichii-sharkiix-version/blob/4243c082f593/SHARKIIX.md
https://github.com/zortos293/Lichii/tree/fix/oot-fog

### Remaining brightness after the counter correction

The user clarifies that the remaining issue appears to accumulate brightness:
approaching a surface reveals its texture, still washed out. Treat this as a
remaining distance/lighting/postprocess problem, not proof of a white texture or
missing mesh. The confirmed counter-lifetime correction stays enabled.

In the first corrected capture, the main material output (pass 196) contains sky
in areas that are filled by the following world/translucent pass 197. Pass 197
has finite HDR RGB means [0.400573, 0.323909, 0.242899], with 18,007 pixels above 1
in all channels. The full-screen composition pass 199 (native FS
`2407c9a14d62a1d6`, translated FS `ed76bdf7145110dc`) changes these means to
[0.904237, 0.705576, 0.480452], with 49,757 pixels above 1 in all channels.
Later pre-bloom passes 203, 205 and 207 change the means only slightly. These
are successive outputs of one captured frame, not a comparison of different
camera positions. The numerical increase identifies a useful next stage to
trace, but does not by itself prove duplicate fog application or incorrect
exposure: physically intended scattering also adds radiance.

A temporary isolation capture 4 omits only that full-screen fragment shader,
using the existing hot-reloaded skip file. The shader is restored immediately
in a finally block. Textures and some strong ground highlights remain visible
without it. The camera/scene changed between captures 1?3 and 4, so their final
images must not be used as a numerical A/B of fog strength. No persistent fog
suppression, exposure multiplier or material-specific fix was applied. The shader
also samples the current scene, two half-resolution RGBA16F inputs and integrated
fog volumes; its final expression combines these inputs, so attribution must
separate the composite from the producers of those inputs. Existing notes on
material-local fog application remain relevant.

Private evidence: `bright-coverage-fixed-14/remaining-brightness-pass-stats.json`,
`fog-composite.spvasm` and `capture-4`. Both `cond.txt` and `skip-fs.txt` are back
to normal/empty. The ground flicker fix is verified; accumulated brightness remains
open and should be investigated without reverting it.

### Confirmed shared defect: pending-counter ring evicts unfinished sample reports

Loaded-map captures in `bright-coverage-13` validate the new command log and
identify a concrete lifetime error in `vk_exec_xfer.c::counter_word`. A fixed
256-report ring forgets sample reports from the submission still being recorded.
An evicted address falls through to guest memory and reads an older count as
known, rather than treating the current query as pending. The scene records
1,877?1,882 reports per captured frame. This defect is separate from the earlier
unproven interpretation of counter type 16; these ground conditions follow type
1 (SAMPLES_PASSED) reports.

For example, the first capture reports samples to `0x15284ff00` at fence 24039.
The material later tests that address after 809 counter reports, while the done
fence is only 24035. Nevertheless the old reader reports status 1 (known) and
uses stale zero, disabling the indexed stone draw (912 indices, buffer
`0x595e55000`). Another capture enables the corresponding stone geometry using
a nonzero old count. In the three captures, 857, 857 and 859 sample-condition
inputs are marked known despite a preceding same-capture report at an unfinished
fence. The machine-readable evidence also lists 16 known timestamp inputs per
capture; those are intentionally known zero and are not sample-query failures.
Current-frame reports explain the false decisions directly; texture/hash changes
are unnecessary to reproduce them. Filtering actual stone draw intents (rather than condition context alone) finds
17 draws in each capture: 13, seven and 11 are skipped, respectively. All 17 in
each capture depend on an evicted, unfinished samples-passed report. The material
depth attachment has one unconditional initial clear and three disabled conditional
clears whose preceding reports are type 16. This confirms why forcing all
conditions damaged depth, while retaining pending sample records can address the
stone skips without enabling those timestamp-guarded clears.


The shared correction replaces the report ring with a replay-owned hash table
keyed by counter address. It retains the latest report and its fence/type,
grows while incomplete addresses remain live, and removes completed entries only
when rebuilding. Repeated writes update the same entry. Shutdown frees the table.
`counter_word` retains existing semantics: unfinished samples are unknown and
render conservatively; unfinished timestamp counter fields are known zero;
completed/unreported words use guest memory. No shader/material hash, exposure
change or force-all-conditions mode is involved. This fixes lost dependency
tracking, not full GPU execution of conditional rendering.

`tests/pending_counter_test.py` compiles the production table helpers and
`counter_word` at O0 and O2, with warnings as errors. Each run passes 248,199 checks:
2,048 simultaneously unfinished addresses (including stale zero/nonzero words),
completion fallback, same-address sample/timestamp replacement, unreadable inputs,
and 120,000 deterministic operations checked against an independent latest-report
model with monotonically advancing completion. Pending accesses never fall through
to guest reads. An initial randomized test used a completion fence that moved
backward, which is invalid for a timeline; it failed and was corrected before
counting passing results. Runtime incremental compilation passes.

Private evidence: `bright-coverage-13/commands.txt`, its next two capture folders,
`stone-conditional-draws.json`, `pending-ring-overflow-evidence.json`, and
`pending-ring-stone-and-clears.json`.
The corrected build compiles/links and is reopened with visible console and file
logging. The user confirms the ground no longer flickers. Three loaded-map captures
in `bright-coverage-fixed-14/pending-fix-validation.json` independently confirm:

- The material pass has 837 draws in all three captures. All 17 stone draws render,
  with zero conditional skips (previously 13, seven and 11 skipped).
- No unfinished sample report is misread as known in any capture.
- The 27,000-pixel road ROI has zero retained sky words and zero clear-depth pixels
  in all three captures.
- The three timestamp-guarded depth clears remain disabled. The initial
  unconditional clear still runs; the correction does not force every condition.

The user also reports some meshes remain white. Preserve the counter correction;
this is not a claim that all brightness, missing geometry or fog faults are fixed.
At material-pass completion, some remaining white areas still exactly retain sky
colour, including pixels with valid depth. Some of these are overwritten by the
next world/translucent pass, so judge a suspected missing mesh across subsequent
passes and the final scene before attributing it to shader brightness or mesh-task
support. Native mesh-task replay remains incomplete, but a particular residual
white patch has not yet been proven to depend on it.

### Capture instrumentation for conditional draws and depth clears

`runtime/vk_dump.c::vk_dump_replay_note` now writes a bounded `commands.txt`
beside each active frame capture. `vk_exec.c::walk` records draw/clear/report
intent in replay order, including operations whose render-enable state skips
them. Entries carry the next pass index, fence, program serial, condition
address/mode, attachment guest addresses, VS c4/c5, FS c6 and texture-slot-0
handle. Indexed draws retain index-buffer address and base vertex/instance.
`vk_exec_xfer.c::cond_enabled` records the actual effective counter words and
statuses it used (0 unreadable, 1 known, 2 pending samples), followed by the
result after evaluation. `vk_exec_draw.c::prepare_draw` adds conditional skips
to the existing per-pass skipped-program list and records successful draw
preparation. These are diagnostics, not a changed conditional-render policy.

The logger reads existing replay state only. It does not resolve targets, read
counter memory a second time, or synchronize the GPU. File I/O during a captured
frame can still perturb CPU timing; do not treat it as a timing-neutral recording.
A separate file preserves existing pass aggregation and numbering. Each capture
is capped at 100,000 command entries with an explicit truncation marker; the file
closes at capture completion and reopens in the next capture directory. The
`cond` field is the last conditional command, even after an explicit render-enable
command; the explicit command and `disabled` field must be checked in sequence.

The incremental compile and link pass. The first link attempt preceded completion
of normal window shutdown and encountered the running executable's file lock;
retry after process exit succeeds. The first reopened process exited during
startup before capture; its log is preserved under `bright-coverage-13`. A second
launch reaches a responding DKB Recomp window with visible console and normal
`build/v111/logs/totk.log`. Live command-log validation succeeded after the user confirmed the loaded map;
no loading/compilation screenshot was taken. Capture output is
`build/oot-fix/bright-coverage-13`, with normal conditions and no fragment override.

### Ground flicker: distinguish normal coverage loss from the force-condition experiment

The user identified stone texture 3067 in `bright-subword-12/capture-7`, pass
208, and contrasted it with capture 2. All captures 2 through 9 contain exactly
the same dumped texture PNG (SHA-256 starts `810040bfdb5c31cd`). The stone material
uses native FS `413d85cd84a24ef0`, translated FS `4c9d908d2bbfff4d`, with depth
EQUAL and no depth writes. A texture dump listed in a pass is an input resource,
not an image of that material's individual draw result; the pass includes many
other materials.

**The experiment changed conditions between captures.** Captures 2?4 use normal
conditions (mode 0); captures 5?7 force every condition to render (mode 2).
The latter also permits conditional attachment clears in `vk_exec.c::clear_ds`.
Do not interpret capture 7 as an uncontrolled example of ordinary flicker or
conclude that forcing all conditions is a fix. The control file is restored to 0.

In capture 7, pass 208 records 839 draws, including 17 in the stone texture's
pipeline group, yet all 921,600 output HDR words exactly equal the earlier sky
pass 192. Its depth attachment at `0x8000960e00` is entirely 1.0. That attachment
is already all 1.0 at pass 196, long before the stone material pass, whose EQUAL
test therefore has no usable scene depth. Captures 5 and 6 show the same exact
sky retention and all-clear depth. These measurements localize this diagnostic
failure upstream of material shading; they do not establish the exact clear or
its condition address, since current dumps do not record individual clear ops.

There is also real variation **within the normal mode**, with a stationary camera.
For road ROI x=500..799, y=610..699 (27,000 pixels):

| Capture | Material pass | Total draws | Stone group draws | Exact sky words in road ROI | Clear-depth pixels in road ROI |
| --- | --- | --- | --- | --- | --- |
| 2 | 196 | 447 | 9 | 0 | 0 |
| 3 | 195 | 463 | 6 | 645 | 0 |
| 4 | 195 | 428 | 7 | 25,969 | 0 |

Capture 2 and 4 have bit-identical depth at 25,326 ROI pixels. Thus the normal
road can retain the sky despite valid scene depth, while its input texture stays
identical. This supports missing material coverage rather than explaining this
particular white area solely as bloom or increased material radiance. It does
not yet distinguish conditional draw suppression, a mismatch of prepass and
colour geometry, raster state, or shader discard. Captures are separated by
roughly 140 game frames and synchronous dump stalls; they are not adjacent frames.

A follow-up capture 8 with pending sample conditions set to skip (mode 1) records
461 material-pass draws, nine in the stone group, and no sky words in this ROI.
Capture 9 after restoring normal mode records 463 draws, seven in the stone group,
and also no sky words in this ROI. One sample of each does not prove pending
sample queries cause the flicker or that mode 1 fixes it. Retain normal mode.
The next useful instrumentation is to associate conditional decisions and clears
with individual depth/material draws, preserving attachment identity and command
order, rather than introducing a shader hash fix or changing brightness globally.

Private machine-readable evidence is in `build/oot-fix/bright-subword-12`:
`temporal-capture-modes.json`, `temporal-material-summary.json`,
`temporal-road-depth-equality.json`, `temporal-depth-comparison.json`, and
`temporal-sky-retention.json`. The new subword translator is installed and the
map has been reached; these captures still show rendering faults, so successful
translation must not be reported as a verified brightness/flicker correction.

### Shared translator correction: guarded byte and halfword loads

`tools/xlate/patches/0025-guarded-subword-global-loads.patch` removes an explicit
rejection of U8/S8/U16/S16 global loads in the opt-in storage-candidate path.
The existing storage lowering already extracts bytes/halfwords and sign-extends
signed results. The missing pieces were accepting these load kinds in the
emitter and guarded helper, and using the correct alignment mask: 0 for bytes,
1 for halfwords, 3 for full words. The complete underlying uint word remains
bounded by descriptor ArrayLength and the existing 64-bit address subtraction
with borrow. Halfwords at odd addresses and inaccessible words return zero under
the guarded ABI. Subword stores/atomics and unsupported widths still reject.
Legacy non-candidate translation is unchanged. Vendor source is not edited.

Validation before deployment:

- `tests/xlate_storage_candidates_test.py`: 14 tests pass, including branch guards,
  signed extraction, resource identity, unsupported widths and legacy behaviour.
- `tests/storage_candidates_subword_gpu_test.py`: 34 original-instruction fixtures
  pass on the GPU, covering every byte lane, signed/unsigned halfwords, address
  carry, invalid halfword alignment, final-byte and out-of-range accesses. Output
  values and all non-result bytes/canaries match; robust access is enabled and
  Khronos validation reports zero errors and warnings. The old translator rejects
  the baseline U8 fixture with the same exception seen in the game log.
- Existing scalar/B64/B128 GPU fixtures: six further cases pass, including address
  carry, with validation. No failed GPU result is counted as a pass: initial runs
  could not find validation until VK_LAYER_PATH pointed to MSYS2's layer directory.
- Replay of 60 unique captured requests that failed the subword-load restriction:
  23 now translate and pass spirv-val (three distinct compute-code blobs, with
  different job metadata). The other 37 advance to the existing descriptor-count
  limit; this patch does not claim to solve that limit or table-loaded pointers.

Private evidence: `build/tests/storage-subword-cpu`,
`build/tests/storage-subword-gpu/manifest.json`,
`build/tests/storage-subword-wide-regression-validated/manifest.json`, and
`build/oot-fix/subword-universal-replay/summary.json`. Rebuild:
`tools/build_xlate.py --out build/xlate-subword-universal`; the builder now expects
23 available patches (the numeric-float patch remains opt-in). NativeAOT build
succeeds. The installed translator identity starts `6a10b532ece5b3b7`.

The game was closed normally, its prior translator retained in
`build/oot-fix/bright-subword-12/translator-before.exe` and `.id`, and the new
translator plus identity copied beside `dkb.exe` with matching executable hashes.
`Play OOT subword loads 12.cmd` reopens the same runtime, normal conditions and
every fragment draw enabled. First load re-keys the shader cache; wait for map
readiness before measuring. The loaded-map captures above still show brightness
and coverage faults; shader acceptance is not evidence that they are fixed.

Startup validation required further retries. Three new-translator attempts exited
before the map; a control attempt with the retained old translator also exited.
The launcher now writes directly to the visible console and retains the normal
`build/v111/logs/totk.log`, rather than piping through PowerShell Tee-Object.
The next launch with the new translator remained responding and reached shader
translation. This does not prove the pipe caused the crashes. No restart loop
or SDK workaround was added. The current identity again starts `6a10b532`.

A useful precise crash record is preserved in `bright-subword-12`:
`startup-attempt-3.log` and `startup-attempt-3-host.log`. It faults at executable
RVA `0xeee8f9b`, reported in lifted `nn::os::LockMutex` (`sdk+0x1647f0`), writing
VA `0x7005364864`. Read-only disassembly identifies the host instruction as
`mov qword ptr [rsi + 0x40], rax`: a Cpu register spill, not the guest mutex store.
The implied Cpu pointer `rsi=0x7005364824` is instead an ARM return location in that
function. The frame reports zero compute dispatches before the fault. Investigate
host context/nonvolatile preservation separately; do not label this as proof of
an invalid guest mutex or a new subword compute shader writing guest memory.
Baseline-control logs are retained as `startup-baseline-translator.log` and
`startup-baseline-host.log`. The final direct-console launch has a live host log;
its old `console.log` is an archived failed attempt, not the current output.

### Shared correction: integer transport in the linear tensor fallback

The user explicitly requested a correction covering related shaders instead of
one shader hash. `runtime/vk_exec_draw.c`, `upscale_source_note`, now chooses by
resource contract. A declared storage-image write through R32_UINT over a packed
HDR image is an integer transport candidate; its unique, separate, same-size 2D
R11G11B10F sampled colour input is used by the linear tensor replacement.
Explicit non-float input views are rejected. No native or translated shader hash
participates. Missing or ambiguous colour inputs retain the prior source; duplicate
bindings of the same image do not create false ambiguity. Read-only storage-image
bindings are excluded using Ryujinx TextureUsageFlags.ImageStore (bit 2).

This is a general policy for the existing linear replacement, not a complete
implementation of neural reconstruction. It intentionally bypasses the transport
stage's network-specific preprocessing. Actual tensor execution is unchanged.
`OOT_LINEAR_UNPACKED_SOURCE=0` keeps the prior packed path for comparisons; unset
or 1 uses the resource selection. The former native-hash gate was removed.

`tests/oot_upscale_source_test.py` now executes the production function in 16 cases
at both O0 and O2, with warnings treated as errors. Cases include integer input
views, valid float views, incompatible dimensions/formats, non-2D images,
read-only output, absent program metadata, RGB9E5 backing, duplicate bindings,
ambiguous inputs, missing input, and self-input. All 32 runs pass. Incremental
compile and link pass with 31 jobs. The game was reopened with normal conditions,
all fragment shaders enabled, no probe file and no forced source override, using
`Play OOT universal transport 11.cmd` with visible console and file logging.
After the user confirmed map readiness, `bright-universal-11` validates the
new default with no forced source override and no probe. All 921600 source words
match the preceding tone-map target (pass 0221) exactly. The 1706x960 destination
matches CPU bilinear resampling within 1.049 destination storage ULPs, all finite;
RGB means change from [0.941736, 0.933566, 0.885410] to
[0.940771, 0.932711, 0.883707]. The process remains responding. Evidence:
`bright-universal-11/linear-upscale-check.json`. This confirms the rule for the
live pipeline; tests exercise other resource configurations, not other live scenes.

The stone-material investigation now separates the first light's visibility from
the maximum across lights. Captures 14/15/16 in `bright-material-10` measure
first-light visibility, local-light visibility and first-light attenuation. Road
medians are 0.359375, 0 and 0.19140625 respectively; first-light visibility ranges
from 0 to 0.84375 over the whole tagged sample. Therefore the previous maximum 1
was insufficient evidence that shadows were ignored. No arbitrary light or shadow
multiplier was applied. Probes were removed after collection. See private
`separate-visibility-check.json` for values and sample sizes.

A related material, FS `910c71c08da0b3a2`, shares VS `b477fbbc49e360d2` with the
stone path and reads the same c6[5].rgb light colour and c6[23].xyz direction.
This establishes a shared input to investigate, not that those constants are
wrong. Private comparison: `related-global-inputs.json`. Brightness from the
material/coverage stages remains open independently of the transport correction.

### Latest material isolation: the stone path

The user identified stone albedo tex 3067 in `bright-bloom-09/capture-3`.
Native FS `413d85cd84a24ef0` (SPIR-V FNV `4c9d908d2bbfff4d`), VS
`b477fbbc49e360d2`, program 8044, uses it in eight depth-EQUAL draws.
Its ASTC 6x6 sRGB view becomes RGBA8_SRGB; sampling retains sRGB.

Private evidence under `build/oot-fix/bright-material-10`:

| Capture | Probe | Result |
| --- | --- | --- |
| root | Normal | Baseline |
| capture-2 | Albedo RGB, original alpha | Recognisable path colour without lighting |
| capture-3 | Magenta coverage, original alpha | Shows covered pixels without changing depth or conditions |
| capture-4..7 | Final additive red terms A/B/C/D | A dominates; B small, C occasional highlights, D zero in sample |
| capture-8 | Squared transformed-normal length | 0.9921875 or 1; no gross normal-scale error |
| capture-9 | Light-loop count | 51542 tagged pixels report 1, 2993 report 2, 3643 report 3 |
| capture-10 | Optional final red bias | Zero on 25403 tagged pixels |
| capture-11 | Maximum visibility per light | 1 on 18594 tagged pixels |
| capture-12 | Maximum red intensity per light | 101 on 153057 tagged pixels |
| capture-13 | Remove term A from RGB | Path darkens, preserving texture/specular; white elsewhere persists |

A is the light-loop result, including diffuse and specular, not pure diffuse.
Captured variables: A=`1208/1210/1212`, B=`2038/2040/2042`,
C=`1938/1940/1942`, D=`2046/2048/2044`. All probes pass SPIR-V validation,
preserve declarations and alpha, and were removed after capture. Frame/draw
counts differ, so measurements across probes cannot be summed as one pixel.

Refinement: maximum visibility 1 is inconclusive. The first light deliberately
bypasses variable `1184` (flag `1316` is loop index != 0); it uses `1182`
separately. The intensity 101 agrees with compact-float rounding of c6[5].r
101.709 (RGB 101.709/73.8359/51.1316, intensity .w 78.125). Neither value proves
corruption or justifies arbitrary scaling. Position already has Invariant.
Next measure first-light visibility and compare related material stages.

Evidence: `stone-pipeline.txt`, `stone.spvasm`, `component-summary.json`,
`normal-check.json`, `lighting-loop-check.json`, `red-bias-check.json`, and
`light-factor-check.json`. No lighting multiplier or suppression is enabled.
User requirement: shader-specific probes isolate faults; production fixes must
address shared operations/resources and cover related shaders.

The opt-in `gfx_shader_debug_override` now accepts a `<probe-file>.hash` sidecar,
polled every 60 frames, to switch targets without restarting. Changing the target
restores the previous shader; removing the sidecar returns to the environment
target. A live target switch and restoration passed. Incremental compile/link
and the existing shader probe declaration-guard test passed.

### Confirmed correction: the 222 to 226 brightness transition

The user's manual pass comparison prompted a focused check of baseline frame
19253 in `cond-record-07`. Pass 0222 is the tone-mapped image. Pass 0225 writes
square-root-encoded RGB9E5 through an integer view; the replacement upscaler then
decodes the packed format and linearly blits the **encoded** values into the HUD
target before pass 0226. The final presentation shader simply samples that target.
All 921600 packed pixels are bit-identical to the upscaler source copy; all decoded
channels are finite, and their maximum deviation from `sqrt(pass222)` is 0.001853.
RGB medians rise from `[0.929688, 0.906250, 0.843750]` before encoding to
`[0.964844, 0.951172, 0.917969]` after it. This is a measured additional lift in
brightness, separate from any earlier sky/material coverage problem.

The prior inverse-signal compute prototype caused a device loss and stays removed.
The successful live test uses the already existing unpacked-input path: select the
packing dispatch's unique, same-size floating-point colour input for the linear
replacement. It avoids both integer reinterpretation and another GPU shader.
After the user confirmed map readiness and reported fewer blown-out areas, capture
`bright-isolate-08` verified an exact match between the tone-map target (pass 0235)
and the replacement upscaler's source. Its 1706x960 destination agrees with CPU
bilinear resampling within 1.193 destination storage ULPs; RGB means change from
`[0.854851, 0.830393, 0.735865]` to `[0.852619, 0.828953, 0.732857]`, rather than
receiving another square-root lift. All destination channels are finite, and the
run remained responding without a device loss. Minor differences include packed
float rounding and interpolation precision. The camera differs slightly between
runs, so these within-frame comparisons are stronger evidence than whole-frame
brightness differences between screenshots.

The first implementation selected native packing stage `6637ac1d052d22d4`.
That hash gate is superseded by the resource-contract correction at the start
of this handoff. Ambiguous/missing inputs retain the prior path. `OOT_LINEAR_UNPACKED_SOURCE=0` selects the old packed path
for comparison; `1` forces the existing diagnostic choice. The real tensor path
is unaffected. `tests/oot_upscale_source_test.py` exercises the production selector
at O0 and O2: nine cases each, including unknown stages, opt-out, ambiguous inputs,
duplicate views, incompatible sizes/formats and a missing program. Both pass.
No exposure multiplier or game shader was changed.

Private evidence: `Play OOT upscale linear 08.cmd`,
`bright-isolate-08/transition-222-226.json`, and `linear-upscale-check.json`.
This fixes one added brightness contribution; the earlier sky/material coverage
and lighting problems remain separate investigations.

The final presentation stage was checked separately in the same frame. Its shader
`3444fa67f4a6dd2e` copies the sampled colour; the sRGB attachment encodes it once.
The final PNG agrees with the standard sRGB encoding of the preceding HUD target
with mean absolute channel error 0.000934 and maximum 0.002353 (8-bit output).
Do not remove this expected output encoding to compensate for an upstream defect.
The corrected executable was built and linked successfully with 31 jobs, then
reopened without `OOT_LINEAR_UNPACKED_SOURCE` so the native-stage default is used.

### Follow-up: bloom passes 0220 and 0221

The user's next visual report identifies pass 0221 after 0220 in `bright-isolate-08`.
Pass numbers differ from `cond-record-07`: these are the bloom pyramid, not that
capture's tone-map/upscaler stages. Always include the capture directory and shader
identity when comparing stages.

- Pass 0220 (`ec2dab2193e086e8`) downsamples 480x270 to 240x135. All output channels
  agree with a CPU 2x2 average within 0.977 destination storage ULPs. RGB means go
  from `[0.329872, 0.317456, 0.336487]` to `[0.328670, 0.316250, 0.334310]`.
- Pass 0221 (`0ebebd848b5e2815`) applies a horizontal five-fetch Gaussian filter,
  using bilinear offsets approximately `[-3.23077, -1.38460, 0, 1.38460, 3.23077]`
  and weights `[0.07027027, 0.31621623, 0.22702703, 0.31621623, 0.07027027]`.
  Their sum is 1.000000029. The complete image agrees with CPU filtering within
  1.297 storage ULPs, including interpolation precision. Output RGB means are
  `[0.326872, 0.314437, 0.330598]`; channel maxima do not increase. The blur is real,
  but this stage does not create a global radiance gain in this capture.
- The initially supplied 0220 filename ended in `_tm.png`, while 0221 did not.
  `runtime/vk_dump.c`, `write_images`, compresses `_tm` values with `x/(1+x)`;
  the plain PNG clamps HDR values above 1. Compare matching variants or raw data.

This does not settle whether bloom is too strong when recombined with the scene.
Two private probes of tone-map FS `65156d0c69e40c69` are prepared in
`build/oot-fix/bright-bloom-09/`: `scene-only.spv` zeroes the bloom RGB contribution;
`bloom-only.spv` zeroes the scene RGB contribution. Each replaces one function
instruction and preserves every original declaration byte-for-byte. Both pass
`spirv-val --target-env vulkan1.2`. The launcher `Play OOT bloom isolate 09.cmd`
points the existing hot-reload override at `probe.spv`; that file is initially
absent, so normal rendering is retained until map readiness and a baseline capture.
Removing it restores the original shader. The planned test changes no exposure
constants and requires no more restarts for probe swaps. Numerical evidence:
`bright-isolate-08/bloom-pass220-check.json` and `bloom-pass221-check.json`.

The live test is now complete. After map readiness, `bright-bloom-09` captured
normal rendering, `capture-2` captured scene-only, `capture-3` captured bloom-only,
and `capture-4` captured restored normal rendering. Both variants loaded, and the
runtime logged restoration after `probe.spv` was removed. The process remained
responding. Scene-only removes the halo but leaves sharp white regions on Link,
terrain and architecture. Bloom-only contains the broad blurred glow. This
separates the spreading of the fault from the already-white scene it receives;
disabling bloom alone does not repair those regions.

CPU reconstruction of the three bloom recombination passes in `bright-isolate-08`
matches `destination_before + bilinear(source)` within 1.027 storage ULPs over
the complete outputs. Thus these stages add each level once in this capture;
there is no measured duplicate-add defect. The tone-map's actual combination is
ACES of `(graded_scene + abs(bloom)^1.5) * 1.0`, using the captured c3 constants.
Replaying that complete equation matches pass 0235 within 1.027 storage ULPs.
For that same frame the CPU scene-only counterfactual predicts 61301 fully
saturated RGB pixels, compared with 111430 in the actual combined image; the
bloom lift exceeds 0.1 in at least one channel on 504400 pixels. This quantifies
amplification, not a proof that the game's bloom constants are wrong.

The later live probe captures have changing upstream inputs despite a stationary
camera: the first 240x135 bloom level's mean red varies from 0.4981 to 0.7250.
Consequently their whole-image means/saturation counts are not a controlled
numerical A/B of bloom strength. Prefer the same-frame CPU comparisons and the
qualitative separation of sharp missing-colour regions from the blurred halo.
Private evidence: `bright-isolate-08/bloom-recombination-check.json`,
`check_tonemap_bloom.py`, `tonemap-bloom-reference.json`, and
`bright-bloom-09/probe-summary.json`. No bloom suppression is enabled in the
normal runtime. Next isolate why those pre-bloom surfaces retain sky colour,
including the omitted mesh colour programs and conditional draw decisions.

Immediately preceding shader-isolation tests used the same loaded scene: omitting
only sky FS `6ebc82e7c1dbab6f` reduced the white significantly but left washed-out
materials; omitting the full-screen fog/composition FS `ed76bdf7145110dc` made the
white worse. These are diagnostics, not fixes. A split of the 26 main-material
fragment shaders was interrupted to pursue the user's narrower transition finding;
its skip list was cleared before reopening. Captures are the root, `capture-2`,
and `capture-3` of `cond-record-07`, respectively. The attempted desktop screenshot
included the overlapping console and is not rendering evidence; use internal dumps.

The sky test also limits the additive-accumulation hypothesis for the main material
pass: 371075 baseline material pixels retain the exact sky word. Of these, 226821
are black when the sky draw is omitted. Among 81854 nonblack no-sky material pixels,
none matches `sky + material` within 0.05 in every channel, while 49744 match the
overwrite result within that tolerance. The two captures are separated in time
and conditional draw counts differ (472 versus 451), so this is not a complete
proof of every pixel's blend state. It does not support a general additive blend
error in that pass; missing colour coverage remains the stronger next target.
Private evidence: `bright-isolate-08/sky-coverage-check.json`.

### Fixed or settled

| Symptom | Cause | Where | Evidence |
| --- | --- | --- | --- |
| Slow 3D scene (15 fps) | `TOTK_VK_VALIDATION=1` in every private launcher, not the code | launchers under `build/v111/` | Same exe: 15.6 fps with validation, 60 fps steady without (3D scene, RTX 5080) |
| 65 s freeze on entering the world | Translator patches 0022/0023 built a 239 KB compute pipeline on the replay thread | removed in `429b02d`; rebuild `totk_xlate` after a merge | `took 65075 ms to build` in the log of the old translator |
| White world, sky where materials belong | Bananza light-quad NVIDIA filter (`vk_exec_draw.c`, `light_quad_fault_skip`) matched Ocarina's ordinary materials | now opt-in with `TOTK_FIX_VS_FAULT=1` | Old runs log "light-quad draws ... skipped on NVIDIA"; captures without it draw path, rocks, trees |
| NaN in the HDR target, bloom spreading white | NVN 2 dispatch header without workgroup counts (division by zero) | `e87a446` | No NaN in any HDR image of captures `bright-03` to `bright-06` |
| Light/exposure compute passes skipped | Translator rejected `RED.ADD.F32` (float atomic add) | patch `0024-f32-global-atomic-add-cas-loop` (CAS loop, as Eden's `f32_add_cas`) | 33 previously failing shaders translate; no 128x72 pass skipped |
| Rejected native stages | FSEL with uniform source; register SHF shadowing; Lichii #144 compute forms | `lower_to_maxwell.h`, `register_pressure.h` | Production lowering 6381 to 6391 of 6455 stages, none lost or changed; 230 synthetic checks |
| Additional brightness between tone-map and HUD | Linear tensor replacement blitted square-root-encoded RGB9E5 transport values | `vk_exec_draw.c`, `upscale_source_note` | Known packing stage now uses its pre-encoding colour input; exact source match and destination within 1.193 storage ULPs of bilinear sampling |

### Open, with what is known

1. **Terrain colour culled by occlusion conditions (white patches).** About 375 colour
   draws per frame are skipped by `OP_RENDER_COND`. Terrain is in the depth buffer but
   gets no colour. Forcing every condition on (`DKB_COND_PENDING_FILE` = `2`) draws the
   terrain but also draws what the game keeps off (haze over Link), so correct counts
   are needed. Every disabling condition compares a counter written by
   **ReportCounter type 16** with one nothing writes (both read 0, mode "render if
   different"). **Correction after auditing the native SDK:** type 16 is a timestamp
   release, and type 1 is samples passed, as in NVN 1. The current live log also
   contains type 1; the earlier sample was incomplete. Diagnostic mode `3` is retired.
   Needed: decode the driver's conditional-render macro and compare the condition's
   CPU bytes with its GPU-visible backing in command order. GPU shader writes are
   absent from the current last-writer diagnostic, so a timestamp report alone does
   not establish who last changed the predicate. Mode `4` provides bounded readback.
2. **Overexposure of what is drawn.** The tonemap uses a CPU-written exposure of 1.0
   (`c3` offset 0x38, the constant Lichii's temporary workaround scales by 0.65); no GPU
   luminance pass feeds it outdoors. The froxel fog (passes "16x9x12" and "16x9x1",
   96 slices from 1 to 500 m) has correct slice arithmetic and an intended per-frame
   volume ping-pong. Its `r16f` volume is a 0..1 visibility factor, not a density; an
   earlier "25 times too dense" reading was withdrawn. Next: trace the pass that applies
   the integrated fog and the factor's inputs, and compare with Lichii, which is also
   too bright (the Eglantine lowering is the only shared component).
3. **Mesh draws are not replayed** (54 to 81 per frame, `OP_DRAW_MESH` recorded and
   counted). They account for ground missing from the depth buffer too. The native
   Task/Mesh prototype under `build/oot-fix/mesh-compiler/` produces 100 of 111 valid
   SPIR-V modules; production lowering still rejects all SPH type 3 stages.
4. **Skipped compute dispatches** (GPU culling, 169x1x1 and indirect): "No bounded
   cbuf-pair candidate", subword global loads, and "descriptor resource limit" in the
   translator. Byte/halfword loads are now implemented and tested (see above);
   descriptor limits and table-loaded pointers remain. Lichii `nx2` #150 addressed
   table-loaded pointers in its different backend.
5. **Intermittent startup crash**: access violation in recompiled SDK code
   (`f_70053689c0`, `sdk_0010.o`) before the first frame, twice; the next launch runs.

### Diagnostics added (all off by default)

| Variable | Effect |
| --- | --- |
| `TOTK_TRACE=gfx` | Frame line now has `mesh N` and `cond pending/evaluated pending/off off/skipped skipped` |
| `DKB_COND_PENDING_FILE=<file>` | Reread every 60 frames. `1`: pending conditions skip; `2`: every condition renders; `4`: synchronously read back 32 bytes at the first 48 distinct pending/disabling conditions, logging CPU and GPU words without overriding the decisions. Mode `3` retired after SDK audit. Leave `4` off for timing. |
| `DKB_SKIP_FS_FILE=<file>` | Draws whose fragment SPIR-V hash (the `fs` hash of `passes.txt`) is listed are skipped; reread every 60 frames |
| `TOTK_DUMP_TRIGGER=<file>` | Now re-armable: delete and recreate the file for `capture-N` subfolders |
| `TOTK_DUMP_UBO_ROWS=<n>` | Rows of each uniform block printed in `passes.txt` |
| `OOT_MESH_LOG=1` | Record-time log of mesh draw programs (from `ffb30b4`) |

Counter types the game reports are logged once ("counter type N first reported").

### Working practice

- Builds are incremental (seconds). Never build another runtime variant into
  `build/v111`: a different compile command recompiles all 489 chunks (about 4.5 min).
  `tools/build_game.py` now defaults to one job per logical processor but one and
  starts the slowest chunks first (from `.ninja_log`).
  This port's build version is `oot111`, even though its output is `build/v111`:
  `--version oot111 --gen generated/v111 --out build/v111 --expected tools/data/expected-oot111.json --exefs build/extracted/v111/exefs --romfs build/extracted/v111/romfs`.
  Using `--version v111` regenerates the wrong ExeFS hashes and default save profile;
  the resulting executable exits at the NPDM hash check. Relink with the command above.
- After every build, open the game through a private `build/v111/Play OOT *.cmd`
  launcher with the console visible, and confirm the `dkb` window. Use launchers
  without `TOTK_VK_VALIDATION` for anything visual or timed.
- A translator change re-keys the whole shader cache: the first session retranslates
  and stutters. Copy `build/xlate/totk_xlate.exe` and `.id` next to `dkb.exe`; the game
  build does not.
- The mesh prototype compiles copies of the lowering headers in
  `build/oot-fix/mesh-compiler/overlay/`; copy `lower_to_maxwell.h` and
  `register_pressure.h` there before `ninja -C obj` and `sweep.py`.
- Captures cost about 800 MB each (`build/oot-fix/bright-0N/`); everything under
  `build/` is private game-derived data.

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

## Upstream recheck and brightness coverage after the NaN fix

A fresh remote-head query on 2026-11-05 advertises 121 Lichii branches. The audit
was fetched into `build/oot-fix/lichii-latest.git`; `refs/eglantine` was left
unchanged. `nx2` is now `99ac3d5955f1e7e07e3c07f8b01829e1fe4e3f0b`.
Since the previously recorded `8813b447`, its two added commits are an Event/MWAITX
deadline correction (`9feec26`) and a built-in maximum-resolution game patch
(`#124`, `99ac3d5`). Neither is a measured fix for this port's remaining brightness.
`fix/oot-fog` remains `8390649c0c0699192b35f486f3aac6d9cf88c8b3` and
`fix/oot-upscaler-gathers` remains `7459f10f11105367c55ca262c0966fbcced84c6b`.
The `fix/oot-bloom` tip `6c1dd4e92c72a95b6abc09eaab1d5a74e90db795` repairs
H0_NH1 half swapping/negation; comparison with the current local lowering confirms
that correction is already present. The max-resolution feature branch is
`1ceb8c972a4e98f7372f02dd42c24df3556d9943`; it is not a shader fix.

The new experimental branch `exp/oot-visual-workaround` at
`4aa1e429a41b58617977ef75c1593f02d4902948` explicitly describes itself as a temporary
workaround, not an emulator correction. Its `docs/OOT_VISUAL_WORKAROUND.md` says
the haze root cause remains open. It scales fog slice distance by 0.25 in compute
`780c0536b89fc89e` and exposure by 0.65 in fragment `bded5ce917183214`; it is
opt-in, and its own documentation still requires indoor/outdoor A/B validation.
No such scale factors were imported here. They identify useful investigation
stages but are not evidence of incorrect hardware constants in this runtime.

The after-fix `bright-headfix` capture already looks too bright at pass 0221,
before square-root/RGB9E5 packing. Consequently undoing only the upscaler input
encoding cannot fix the dominant scene defect. A raw-word comparison of sky pass
0191, material pass 0193 and final colour pass 0221 gives:

- 522361 of 921600 pixels are bit-identical between sky and material colour.
- 240668 pixels have all three tone-mapped channels saturated to 1.
- 240425 of those saturated pixels (99.899%) still have the exact sky word after
  the material pass.

This narrows the white-surface symptom to missing colour coverage, consistent
with the earlier pre-NaN investigation. It does not prove each particular mesh
draw's screen coverage. The material pass lists 17 distinct omitted mesh-program
groups and no successful pipeline with an absent fragment shader. Reducing global
exposure would hide this evidence and darken the correctly drawn surfaces too.

The fog integration dispatch in the same frame is `a4550cb09fe72248` (pass 0097).
It accumulates extinction, scattering and density volumes produced by pass 0096.
The existing PNGs quantize and rearrange the slices, so numerical validation needs
raw 3D data. `runtime/vk_dump.c`, `write_images`, now retains 3D float and packed HDR
volumes with `TOTK_DUMP_RAW_HDR`; raw layout is z/y/x, not the PNG's side-by-side
layout. `TOTK_DUMP_UBO_ROWS` selects up to 1024 vec4 rows (default remains 16), so
the same capture can retain fog constants at c3 rows 24 and 32. These diagnostics
are opt-in and compile successfully. In the loaded-map `bright-fog` capture the
integration is pass 0102. Its constants are near 1, far 500, power 1 and volume
size 128x72x96. The private `check_integration.py` replay compares all 96 slices
on rays (20,20), (64,36) and (100,50): extinction and scattering stay within one
storage ULP of the captured RGB volumes; density stays within 1.256 storage ULPs
of the R16 volume. This limited sample does not establish correct upstream fog
inputs, but does not support a factor-of-four integration arithmetic error.

## Ocarina port update: the inherited NVIDIA draw filter

On 2026-11-05, `origin/oot-port` advanced to
`92529cb739ea3f9a246ea2f6533912b2b1669d0c`, "Stop skipping Ocarina's world draws as
Bananza light quads on NVIDIA". This branch belongs to Chiuauaa/oot-pc-recomp;
it is distinct from zortos293/Lichii's Ocarina-related branches audited above.

The change identifies an overlooked rejection before graphics pipeline binding:
`runtime/vk_exec_draw.c`, `light_quad_fault_skip`. The Bananza GPU-fault mitigation
matches vertex input Location 9 plus fragment SSBO c0+0x530, or one named vertex
stage. Ocarina's normal world materials also match. `rt_game_version == 0` selects
both NVN 2 games and cannot distinguish them. The old default-enabled `cfg_bool`
therefore removed Ocarina material draws on NVIDIA. The sky left underneath these
surfaces explains the earlier measured missing-colour coverage. Missing mesh draws
remain a separate real limitation; their presence was insufficient evidence that
they caused the observed white surfaces.

The local port now matches the upstream policy: this filter runs only when
`TOTK_FIX_VS_FAULT` explicitly begins with `1`, with the existing NVN 2/vendor
checks retained. The NVN 2 dispatch-header NaN correction remains in place.
The commit's ISBEWR register-shadowing correction is already present locally.
Its additional changes are depth-state dump diagnostics and opt-in fog/exposure
SPIR-V scaling. Those scales were not imported. Although it adds a sky-recognition
helper, the final dynamic-state code only references that helper with `(void)`;
this commit does not actually enable a sky depth override.

A separate local experiment to square the decoded RGB9E5 signal passed a synthetic
GPU test (0.25/0.5/1 became 0.0625/0.25/1), but the live run lost the Vulkan device
after activation, before its second capture completed. The prototype was removed
from runtime sources, with its files retained only in ignored
`build/oot-fix/bright-signal/rejected-experiment`. No presentation correction or
live A/B success is claimed. Baseline frame 4561 is retained in `bright-signal`.

The draw-filter correction compiled and linked successfully. The replacement run
uses `build/v111/oot-worldfix-check.cmd`, with the visible log console and automatic
file logging retained. Its capture directory is `build/oot-fix/bright-worldfix`.
After the user confirmed the loaded map, frame 6884 captured successfully with
zero pending shaders/pipelines. The scene has more floor and character detail,
but remains visibly washed out. In `bright-worldfix`, the main material pass is
0194 (464 draws), the sky draw is 0178 (FS `8c8eae5ad16c9222`, native fragment
`fd5ed85c82643039`), and final pre-pack colour is 0222. The earlier `bright-signal`
frame 4561 has corresponding passes 0193 (446 draws), 0177 and 0221. This is a
cross-run comparison with similar framing, not a controlled pixel-identical A/B.

All-three-channel saturation at or above 1 falls from 275470 to 215652 of 921600
pixels. Of the latter 215652, **215284 (99.829%) still equal the sky's original
packed word after the material pass**. The same count results when comparing
against pre-material pass 0192, which also contains intermediate draws; that
pass should not be described as the sky draw itself. This establishes retained
sky colour in the remaining bright pixels without attributing it to any one
remaining skipped draw. The normalized depth PNG shows 203166 saturated pixels
with depth strictly below its maximum (a conservative indication of geometry
in front of the far plane); full raw depth is not retained in this capture.

Every retained non-packed image reports zero NaN channels. The only apparent
NaNs are again the RGB9E5 payload interpreted as base R11 in pass 0225, not an
established new arithmetic failure. Therefore the inherited filter correction
is retained as a partial material-coverage fix; complete brightness correction
is not claimed.


### Live isolation of the remaining sky contribution

With the same process still on the loaded map, only fragment `8c8eae5ad16c9222`
was temporarily listed in `bright-worldfix/skip-fs.txt`. Capture 2 (frame 11948)
completed; the final pre-pack colour has **1 saturated pixel instead of 215652**.
Link, the other character, the floor and buildings become visible, although the
image remains hazy and some surfaces have dark silhouettes. This isolates the
sky draw's contribution; discarding a required sky draw is not a rendering fix.
The skip file was cleared immediately after capture, restoring all draws.

The normal after-fix frame's packed RGB9E5 payload was independently checked:
all 2764800 decoded colour channels are finite, the copied source has zero word
mismatches, and maximum deviation from the square root of pre-pack colour is
0.0015832. There is no measured new NaN to clamp in that path.

The next diagnostic extends `vk_dump_draw` entries with depth test/write/function
and stencil enable, and includes those dynamic depth fields in entry grouping
rather than combining distinct states behind one pipeline. `TOTK_DUMP_RAW_HDR`
also retains D32F readbacks so the comparison does not rely on normalized depth
PNGs. This changes capture data only. The next run uses `bright-depth`; no
forced sky depth state or exposure/fog scale is enabled.


### Raw depth and material return probe

After the user confirmed the re-opened loaded map, `bright-depth` captured frame
5315. Sky pass 0178 explicitly records depth test 0, write 0, function 3 (EQUAL).
The material pass 0194 records 473 draws; its 48 retained pipeline/binding entries
all use depth test 1, write 0, function 3. The sky and material depth attachments
are different guest images, at 0x8000000000 and 0x8000960e00. A texture copy after
pass 0140 supplies the latter; it is bit-identical between passes 0182 and 0194.
It differs from the sky pass's depth at 250903 pixels, after intervening rendering.
That comparison alone is not evidence that the earlier copy is incorrect.

Of 921600 colour pixels, 554020 retain the sky word through the material pass;
486810 of those have finite geometry depth strictly below 1 in the material's raw
D32F attachment. The renderer must therefore account for material coverage in
front of this retained sky. The depth-function mapping in `runtime/vk_pipeline.c`,
`compare_op`, maps NVN EQUAL (3) directly to Vulkan EQUAL (2); no enum error was
found. The disabled sky test is recorded guest state, so forcing it on is not yet
a justified emulation correction.

A fragment return probe preserves the interface and arithmetic of
`abc4dfd9710c36d2` / SPIR-V `1a8b24e03070da0a`, then stores constant magenta at
its return. An initial composite-constant declaration was rejected by the runtime
guard; capture 2 is not a successful probe result. Moving that construction inside
the function passed SPIR-V validation and the guard. Capture 3 (frame 14457)
contains 2637 exact-magenta material pixels, bounding box x=662..981, y=0..166.
This shader reaches colour output in that upper-scene object; it is not a probe
of every missing surface or of Link specifically. Its original shader has no
OpKill. The override file was removed after capture, restoring the original.

The next opt-in diagnostic is `runtime/vk_exec_draw.c`, `depth_coverage_probe`.
`OOT_DEPTH_PROBE_FILE` names a private text file with a hexadecimal colour-target
address and NVN function 3, 4 or 8. It changes only depth-tested, non-depth-writing
EQUAL draws into that target, applying the change to the local pipeline-state
copy. The guest command state is preserved. It rereads every 60 frames; missing
or invalid files restore the guest function. This permits a live EQUAL/LEQUAL/
ALWAYS coverage comparison after one restart; it is not a shipped rendering fix.


### Live EQUAL / LEQUAL / ALWAYS coverage comparison

On the loaded, stationary map, `bright-coverage` captured the original EQUAL
state, then LEQUAL, ALWAYS and finally the restored guest state in one process.
The inverse camera/projection rows c3[0..13] in the sky shader are unchanged
between the initial EQUAL and LEQUAL captures. Shader and pipeline pending counts
were zero before triggering each test. No fog, exposure or sky-depth override
was enabled, and the fragment override file was absent.

| State | Frame | Main material draws | Sky words retained at geometry depth | Saturated pre-pack pixels |
| --- | ---: | ---: | ---: | ---: |
| EQUAL, original | 6971 | 464 | 306344 | 136726 |
| LEQUAL | 8977 | 426 | 462902 | 220073 |
| ALWAYS | 12207 | 590 | 97167 | 87066 |
| EQUAL, restored | 13698 | 520 | 356454 | 180791 |

Material depth-state entries confirm functions 3, 4, 8 and 3 respectively. LEQUAL
is not a brightness fix. ALWAYS recovers coverage but visibly draws objects
through one another and still leaves bright regions; it is not an acceptable
rendering correction. The probe also matches the smaller read-only EQUAL passes
0192 and 0195 into the same colour target, not only the main material pass.
Draw counts and approximately 68000-70000 depth words differ across these frames,
including after restoration, so these totals must not be interpreted as a
pixel-identical A/B or proof that EQUAL alone causes the missing coverage.

The depth probe file was deleted and the runtime logged target 0 / function 0;
capture 4 confirms that original EQUAL is restored. The game remains open with
the visible console and file log. Every capture's only nonzero NaN row is the
already identified integer-packed RGB9E5 payload under its base R11 interpretation;
there is no new measured scene-colour NaN.

The restored material pass still lists **17 omitted mesh-program groups, 23 mesh
calls** (6 direct groups and 11 indirect groups). These remain a concrete renderer
limitation, but their individual screen coverage has not been established.
The upstream port head was rechecked and remains `92529cb`; there is no newer
port correction to import at this point. Eglantine's reference implementation in
`backend/spirv/emit_spirv_mesh.cpp` handles task payload and mesh attribute/map
regions explicitly, using task payload and workgroup storage plus a mesh epilogue.
This port currently vendors only the Ampere lowering headers and routes ordinary
stages through the Maxwell translator. Reusing tessellation handling or simply
turning OP_DRAW_MESH into a conventional draw would not implement those semantics.
No native mesh rendering or complete brightness fix is claimed by these tests.


## Mesh ABI and native control classification follow-up

The first concrete change in this follow-up corrects recorded mesh arguments,
not rendering. Local Capstone analysis of the supplied decompressed Ocarina main
module confirms `DrawMeshTasks(cmd, first, count)` at main+0x273d20: the caller
sets w1=0 and w2=(object_count+31)>>5, without setting x3. The two sampled indirect
call sites, main+0x1865f4 and main+0x2be2dbc, prepare x0 and the GPU argument-buffer
address in x1; later registers are not prepared. Disassembly stays private under
`bright-coverage/mesh-abi/`. REA tools were unavailable in the active session;
read-only doctor reported absent Codex registration, so no REA configuration was
changed and these are local Capstone observations, not REA Evidence records.

`runtime/nvn_api_cmd.c`, the two mesh recorders, now store uint32 first/count
and zero the unused third field for direct calls; indirect calls retain the full
GPU address and zero the two unused fields. The existing 40-byte packet and
caller offset are preserved. `vk_dump_mesh` labels first_task/task_count or
argument_buffer explicitly. The previous third-word values such as 0x230 and
0x660 were stale CPU registers, not confirmed Y/Z launch dimensions.

A separate corpus check identifies the native control discriminator at +0x56.
The 6455 retained native pairs contain the following graphics classes:

| Native control kind | Programs | SPH types | Independent stage-buffer evidence |
| --- | ---: | --- | --- |
| 0 | 2469 | 1 | no ISBE instructions |
| 1 | 3115 | 5 | no ISBE instructions |
| 6 (Task) | 7 | 1 | writes mesh count to ATTR[RZ+4]; no MAP writes |
| 7 (Mesh) | 104 | 1 (40), 3 (64) | writes primitive count to MAP[RZ+3], attributes and indices |

The remaining 760 pairs are not graphics headers in this check. Task and Mesh
identity is corroborated by their instructions, not just a correlation with SPH
numbering. `runtime/nvn2_lower.cpp` now retains the native kind alongside a
lowered stage, exposed by `nvn2_native_kind`; skipped-draw and mesh diagnostics
report it for both compact-native and lowered controls. This does **not** yet
select or execute a native task/mesh pipeline.

Program array index is not an NVN stage slot. In the frequently omitted program,
array element 0 (`70bc73b2dc14c99c`) is a fragment stage, element 1
(`2e3145f73b5ebed4`) is Task, and element 2 (`c049e63027bf4000`) is Mesh. Analyses
that label element 1 a fragment merely because it is second in the array are
incorrect. SPH type 3 is reused by these Mesh programs; treating it as ordinary
tessellation explains the misleading lowering-failure messages.

Launch/output metadata remains partly inferred. All 111 Task/Mesh controls have
32 in the low 16 bits at +0x58. The upper half takes multiples of 128; interpreting
it as payload bytes is not independently verified. The high byte at +0x5c is 1
for two programs that each write one U8 MAP index, and 3 for the other 102 meshes,
which write index triples. +0x60 contains plausible vertex/primitive limits (for
example 64/126), but the SPH maximum-vertex field is zero throughout and cannot
independently validate those limits. These observations are not permission to
hard-code every mesh as a 32-thread triangle pipeline.

Eglantine `Maxwell::TranslateProgram` additionally consumes SET_MESH_SHADER_A/B,
SET_MESH_INIT_SHADER and a separate geometry-slot primitive-output header.
Its `ShaderCache::MeshStateHash` includes those three words and all twenty header
words. Bounded upstream source searches did not find the conversion from this
NVN control blob to those GPU registers: Eglantine observes the registers after
the native driver has executed. Building its SPIR-V backend also needs the full
IR/frontend/optimization source and common/fmt/Sirit dependencies, beyond the
eight locally vendored lowering headers.

A relevant upstream limitation: Eglantine `RasterizerVulkan::Draw` launches direct
mesh workgroups with DrawMeshTasksEXT(count,1,1), but its indirect path explicitly
reports unsupported indirect task/mesh and returns. Reusing its direct path would
not by itself implement this port's eleven omitted indirect program groups.

Validation: production recorder tests pass at O0/O2, including polluted argument
registers, a zero count, a 64-bit indirect address, immutable CPU state and packet
compatibility. A synthetic test linked against the production lowering object
checks raw/cached Task and Mesh identity, SPH1/SPH3 reuse, original-control mutation,
unknown code and short controls. Runtime compilation and a full alternate-output
link to `build/v111/dkb-mesh-abi-check.exe` pass, allowing the existing visible game
to stay open. `git diff --check` and the staged content check pass. No mesh draw or
additional brightness correction is claimed from this recording/metadata work.


## Native task/mesh compiler prototype and uniform FSEL correction

The full Eglantine shader compiler now builds under ignored
`build/oot-fix/mesh-compiler/`. Sources were extracted from reference commit
`724aede4ea51fae8224cdd02549cdbdf7ceecd25` without changing `refs/`. Its pinned fmt
12.1.0, Sirit v1.0.5, SPIR-V Headers vulkan-sdk-1.4.341.0 and Boost 1.92.0 archives
were SHA-512 verified against upstream `cpmfile.json`. The offline logging/settings
adapters retain default resolution, debug and loop-safety settings. The Ampere
header overlay uses the repository's existing lowering fixes, excluding the
minimal common-type shim in favour of the full upstream common types.

`mesh_compile.exe` takes control/code paths, output path and three **explicit**
mesh register words. It selects Task/Mesh by native kind, retains ISBE operations
through lowering, then invokes `TranslateProgram` / `EmitSPIRV`. Missing static
constants or texture properties raise errors instead of inventing resource data.
Task `2e3145f73b5ebed4` and Mesh `c049e63027bf4000` emit TaskEXT/MeshEXT SPIR-V
validated with `spirv-val --target-env vulkan1.2`. The Mesh output uses
OpSetMeshOutputsEXT. This is an offline compiler, not a live rendering pipeline.

The private `sweep.py` uses **candidate**, not fully driver-verified, register
values from the control-field hypotheses. Initially 96 of 111 Task/Mesh programs
emit valid SPIR-V; thirteen fail translation and two require unsupported point
output. Validation does not verify live resource bindings or declared limits.

### One lowering failure fixed: uniform-B FSEL

Three Mesh failures stop at opcode 0xc08. The existing lowering accepted FSEL
register/immediate/cbuf forms 0x208/0x808/0xa08 and uniform-B integer SEL 0xc07,
but omitted uniform-B floating select. Mesa NAK `sm70_encode.rs`,
`SM70Encoder::encode_alu_base`, confirms form 6 for a uniform second source.
The current remote nx2 lowering was rechecked and also lacks case 0xc08.
The local fix extends FSEL and uniform-bank detection, preserving floating
modifiers and the selector, and shadows only the GPR destination/A operands.
No clamp, exposure factor or draw rejection was added.

Validation:

- 206 synthetic lowering checks pass at O0 and O2: uniform zero/high registers,
  execution/selector predicates, modifiers and overlapping high-GPR shadowing.
- Native Task/Mesh results increase from **96 to 99 valid SPIR-V**. All 96 earlier
  successful outputs remain byte-identical (SHA-256 comparison).
- Production lowering of all 6455 stages increases from **6381 to 6386**, compared
  with HEAD headers, with zero newly rejected stages. Newly accepted fragments:
  `4b740172b74fbc0e`, `8d5cd627ea116371`, `8f06677899372a7d`; meshes:
  `797bdcac7bdbb4eb`, `9b15ebf7d86d59a9`. The third native-compiler Mesh success
  remains blocked by the production tessellation route.
- The active runtime log explicitly rejected all three fragment hashes above
  before the fix. Their visual impact still needs a loaded-map comparison.

Remaining native-compiler failures: eight missing static texture-handle captures
at c2+0x20, one register-shadowing form, one return-linkage form, and two point
meshes. Native Vulkan feature enablement, resource reflection, pipeline creation
and mesh draw execution remain unimplemented. No complete brightness fix is
claimed. Private source, binaries, SPIR-V and before/after results stay under
`build/oot-fix/mesh-compiler/`; copied-code/dependency notices were recorded.


An additional independent check used NVIDIA nvdisasm 13.0.85 from the official
CUDA 13.0.2 redistribution manifest (archive SHA-256 verified before extraction,
licence retained locally). It decodes the captured opcode as
`FSEL R86, RZ, UR10, !P4`. Clearing bit 91 makes that native encoding illegal;
bit 91 is the uniform-source encoding flag here, not a different selector bank.
Synthetic FSEL tests now include that required bit. All 206 checks still pass at
O0/O2. This check confirms the instruction interpretation independently of our
existing decoder. The redistribution manifest and disassembly stay private under
`mesh-compiler/nvdisasm-package.json` and `fsel-bit91-on.sass`.

The corrected runtime compiled and linked. Production mesh-recorder, native-kind
round-trip and dispatch-header regression tests also pass. The visible old game
was closed normally; `build/v111/oot-fsel-check.cmd` reopened the corrected build
with file logging and the console retained. The depth override and fragment
probe are disabled. Capture directory: `build/oot-fix/bright-fsel/`. Its first
loaded-map trigger remains pending the user's readiness confirmation; no visual
brightness result is claimed before that capture.

## Register SHF shadowing, Lichii #144 and the Lichii branch review (2026-11-06)

### Register SHF above the reserved registers

Native mesh `16671ffd3a7db9da` failed in the native Task/Mesh prototype at `+0x750`
with "native register shadowing mode". nvdisasm SM86 decodes the instruction as
`SHF.R.W.U32.HI R228, RZ, R228, R25` (opcode `0x219`). The lowering already handled
this form, but `register_pressure.h`, `PrepareRegisterPressure`, had no operand
shape for it, so a high destination or source could not be shadowed. The table now
lists `0x219` with its destination (bit 16) and three sources (bits 24, 32, 64).

- Synthetic lowering checks: 218 at O0 and O2 after this change.
- Native Task/Mesh prototype: 99 to **100** valid SPIR-V outputs; the 99 earlier
  outputs are byte-identical. The prototype builds from its own copy of the headers
  (`build/oot-fix/mesh-compiler/overlay/`), which must be synchronised by hand.
- Production lowering is unaffected (it still rejects SPH type 3 earlier), as
  expected: 6386 of 6455 stages before and after, zero outputs changed.

### Lichii #144: remaining Ocarina compute forms

Lichii `nx2` `f356d39b` (#144) accepts UIADD3/ULEA carries in any UPn, indexed ULDC
with any immediate and signed offsets, F2IP.U8.F32 with a second byte source,
I2F.U8 from a uniform register, and counts P2R as one write in the call proof. Its
ULDC hunk overlapped our local subword change and was merged by hand; the other
hunks applied unchanged.

The indexed ULDC offset is signed: nvdisasm SM86 prints `0xffff` as
`c[0x4][UR15+-0x1]`, and the translator adds the Maxwell LDC offset with sign
(`InstEmitMemory.Ldc`, `Imm16ToSInt`). Our earlier synthetic test assumed an
unsigned offset and expected `0x8000` to be rejected; it now checks that the
indexed form keeps the signed bits and that the direct form past `0x7fff` is still
rejected.

- Synthetic lowering checks: 230 at O0 and O2.
- Production lowering: 6386 to **6391** of 6455 stages, zero lost, zero changed.
  New compute stages: `102f5b0abb0f1e63`, `5b9ba94d1c822e80`, `6344c53d07706e30`,
  `7650519f88e59b19`, `78ccf14779ec3efb` (the three former "LEA carry/high mode"
  rejections among them). All five translate with the current translator and pass
  `spirv-val --target-env vulkan1.3`.
- The remaining 64 rejections are all SPH type 3 "native tessellation stage buffer
  read mode" (the task/mesh stages).
- The runtime built incrementally; `--probe-nvn` (1011 checks), the shader-control,
  native-stage, followed-view and mesh-recorder tests pass. Visual effect not yet
  measured.

Provenance: `UPSTREAM_COMMIT` and `THIRD_PARTY_NOTICES.md` list `f356d39b`.

### Lichii branch review

Snapshot 2026-11-06: `nx2` at `5a50f50e`, 40 Ocarina branches. Items that bear on
this port, and what they mean here:

| Lichii change | Their finding | Meaning for this runtime |
| --- | --- | --- |
| `nx2` #150, sky ambient fold | Sky ambient compute `b76d7b3e9e93093d` atomically adds 12 floats (sky SH) into a buffer whose pointer (c0+0x20) hides behind `IMAD.WIDE ptr * 1`; unfolded, the binding was null and the materials and fog read zero ambient. | It uses `RED.ADD.F32`, which patch 0024 now emits. Whether our guarded candidate walk finds c0+0x20 through the multiply by 1 is not yet checked. Check its dispatch and binding in a capture. |
| `nx2` #150, query flush before MME DMA reads | The predicate macro read a stale "visible" record: half-resolution depth clears ran every frame, fog was drawn over Link and the terrain, and leaves and lights flickered. | Closest match to the reported white and flicker. `runtime/vk_exec_xfer.c`, `cond_enabled`, enables rendering whenever a samples-passed report is not yet done. Measure how often that happens in the 3D scene before changing it. |
| `nx2` #150, loaded pointers | The GPU culling pass reads instance transforms through pointers loaded from tables; unbound, every instance was culled (trees, leaves, fog, sunlight hidden). | Same class as our skipped "No bounded cbuf-pair candidate" dispatches (169x1x1 and indirect culling passes). Needs table-pointer support in the translator. |
| `nx2` #150, pointer-table pools up to 64 MiB | Grass instance records lie 11.7 to 17.7 MiB past a table pointer; the 8 MiB cap made every blade read zero. | Our commit `a0b333a` uses the same 8 MiB cap for unsized storage buffers. Relevant once mesh draws are replayed. |
| `nx2` #150, native uniform bank re-upload | Native programs' 64 KiB uniform bank can go stale. | Not yet compared with our c1/inline-constant path. |
| `exp/oot-visual-workaround` `4aa1e429` | Opt-in shader edits (fog slice distance x0.25, exposure x0.65); the haze root cause is still open there. | Not a fix; not imported. Confirms that even Lichii shows excess outdoor brightness. |
| `perf/oot-texture-writes` and the other `perf/oot-*` branches | Buffer-tracking and GPU-thread throughput work in Eden's buffer cache and Maxwell3D method path. | No equivalent here (NVN-level replay); this runtime already runs the 3D scene at 60 fps without validation. Not imported. |

### Occlusion-conditioned colour draws (2026-11-06)

Pass captures `build/oot-fix/bright-03/` and `bright-04/` (private) locate the
remaining white areas. The HDR targets contain no NaN after `e87a446`. The first
HDR pass fills the screen with sky and haze (maximum about 43). In the main colour
pass (~470 draws) the right-hand mound, the wooden structure and the tree canopies
are present in the depth buffer but receive no colour: they stay sky-white or come
out black. Only the lower corners have no depth at all (ground that is missing
entirely, consistent with the dropped mesh draws).

`TOTK_TRACE=gfx` now prints `cond <pending>/<evaluated> pending/<off> off/<skipped>
skipped` per frame (`runtime/vk_exec.c`, `vk_exec_frame_log`). In the 3D scene each
frame evaluates about 890 conditions; about 380 disable rendering and about 375
draws are skipped; only 4 are pending (a samples-passed count not done yet), so
the pending-renders rule alone does not explain the image.

`DKB_COND_PENDING_FILE=<file>` (`runtime/vk_exec_xfer.c`, `cond_mode`) switches
the rule while the game runs: `2` makes every condition render. In the same scene
the main colour pass then records 835 draws instead of 471, and the terrain is
drawn with its grass and ground colours where it was white. Forcing every
condition is not a fix: it also enables draws the game keeps off (the image then
covers Link, the path and the rocks with haze), the same symptom Lichii `nx2`
#150 traced to a stale visibility record.

Every condition logged as disabling rendering (mode 1, render if the two counters
differ) read **0 and 0**. Occlusion counting is active ("counting every draw from
now on"), so either nothing counted the proxies or the counters are written by
something the CPU-side evaluation does not see. `cond_note` now also names the
last replayed writer of each counter (ReportCounter type, ReportValue/ClearBuffer,
or nothing). The next step depends on that result: correct counts if the reports
are ours, or GPU-side evaluation if the GPU writes the counters.

A separate intermittent startup crash was seen twice: an access violation in
recompiled SDK code (`f_70053689c0`, `sdk_0010.o`) before the first frame. The
next launch of the same executable runs normally. Not yet investigated.

Writer tracking identified a replayed API writer, but does not track shader writes. In the 3D scene all 21 logged
disabling conditions (mode 1, render if the counters differ) compare a counter
written by **ReportCounter type 16** with the counter 16 bytes later, which nothing
in the replay writes (it stays 0). The game reports only counter types 0 and 16;
it never reports type 1, the NVN 1 SAMPLES_PASSED that `report_counter` counts with
occlusion queries. In NVN 1 type 16 is TIMESTAMP_TOP, whose counter word is always
0 (deko3d `dk_variable.cpp`), which would make every such condition permanently
false; the "counter differs from 0" pattern is occlusion culling. Type 16 is
therefore probably a samples-passed counter in NVN 2's numbering. This is an
inference from the usage pattern, not a confirmed enum value.

`DKB_COND_PENDING_FILE` mode `3` counts type 16 like SAMPLES_PASSED
(`samples_type`). It did not give a usable image: 67 of 103 conditions per frame
were then pending, because the game reports the proxy count and tests the
condition in the same command buffer, and the CPU-side evaluation runs when the
op is replayed, before the GPU has counted anything. Pending renders, so mode 3
looks like mode 2 (everything drawn, haze over Link). On the hardware the
condition is evaluated by the GPU in command order.

The original provisional conclusion (superseded by the SDK audit below) was: the white terrain comes from occlusion conditions that
this runtime cannot evaluate correctly, on two counts: (1) the samples-passed
counter type of NVN 2 is not recognised; (2) even when counted, the result is
needed on the GPU within the same submission. The fix is GPU-side conditional
rendering: copy the occlusion query results into a buffer in command order
(`vkCmdCopyQueryPoolResults`), derive the NVN comparison of the two counters into a
32-bit predicate outside the render pass, and gate the draws with
`VK_EXT_conditional_rendering`, keeping the CPU path for counters that are already
resolved. Captures: `build/oot-fix/bright-04/` (modes 0 and 2) and
`build/oot-fix/bright-05/` (modes 0 and 3); 2x2 final-frame comparison
`build/oot-fix/cond-compare.png`.

### Native counter ABI audit: rejected type-16 hypothesis (2026-11-06)

The preceding claim that NVN 2 renumbered samples passed to type 16 is false.
The decompressed native SDK's `CommandBufferReportCounter` uses a lookup table
for ordinary counter types: type 1 requests `ZPassPixelCount64`. Type 16 takes a
separate branch emitting a semaphore release with zero payload and a timestamp;
type 0 also has a zero report payload. These agree with the existing NVN 1 enum.
The current live log reports types 16, 0 **and 1**, with type-1 conditions pending.
The previous "never reports type 1" assertion described an incomplete sample.

`CommandBufferSetRenderEnableConditional` submits the unchanged supplied address,
its high and low halves, and the comparison mode to MME macro method `0xe2c`.
This is a macro invocation, not a new fixed conditional-render register. Until
that macro is decoded, the driver's address binding alone does not prove the
runtime's `addr` versus `addr + 16` interpretation. Counter zeroes in CPU memory
also do not rule out writes to a GPU mirror. `cond_writer_note` records API reports
and small clears only, not storage writes by compute or draw shaders.

`runtime/vk_exec_xfer.c`, `samples_type`, no longer permits diagnostic mode 3 to
misclassify timestamps. New mode 4 uses `vk_exec_read_buffer` to compare all 32
record bytes with GPU-visible memory at the replay position. It is opt-in, bounded
to 48 addresses per process, and does not force a condition true or false. It
submits and waits for prefixes, so its frame times are not performance evidence.
Do not arm it until the user confirms that the map and shader compilation are ready.
Disassembly and future readback logs remain private under `build/oot-fix/`.

Validation: the incremental MSYS2 build and link pass with 31 jobs. This diagnostic
does not change shader translation and does not require another shader-cache key.
The live comparison is still pending map readiness; do not describe it as a verified
rendering fix. The private launcher `Play OOT condition records 07.cmd` starts with
mode 0, a visible console, the normal host file log, and a second complete console
log at `build/oot-fix/cond-record-07/console.log` to retain startup exceptions.

Live result after the user confirmed a stationary, fully loaded map: all 48
records were readable and all four 64-bit words matched between CPU memory and
an ordered GPU readback. Of these, 43 had `[0, nonzero timestamp, 0, 0]`, three
had `[1, 0, 0, 0]`, and two had nonzero sample payloads plus timestamps. Four
evaluations had a pending samples-passed report. This sample does **not** support
stale GPU mirrors as the cause of the zero payloads. A pending report can still
make `counter_word` return a provisional zero even when the backing contains 1;
the readback records the backing, not the pending-report override. The diagnostic
was returned to mode 0 immediately after collection; the game remained responding.
Private evidence: `build/oot-fix/cond-record-07/readback-lines.txt` and
`readback-summary.json`. The conditional MME macro and command-order side effects
still need resolving before choosing a production change.

The native SDK audit is reproducible with the private
`build/oot-fix/counter-abi/audit_sdk.py`. Evidence ID:
`static-sdk-counter-abi-3ae3cd4cf340d72b`. The type-1 selector and type-16 release
are confirmed; the conditional macro body is not recovered. A bounded scan of
SDK read-only data found no matching macro, but lacked DMA FIFO reads and prior
MME state, so it does not establish that the macro is absent.

### Where the excess brightness comes from (2026-11-06)

Separate from the culled terrain, the drawn image is overexposed. In capture
`bright-03` the tonemap (the first pass whose output is at most 1) reads only the
HDR scene, the bloom chain and CPU-written constants. Its exposure constant
(`c3` offset 0x38, the value Lichii's temporary workaround scales by 0.65) is
exactly 1.0, and no GPU luminance measurement feeds it in this outdoor area (the
single-group compute after the colour pass reads only 64 texels and is not an
average). The sky LUT (`rgba16f` 256x256, 0.6 to 104) and the sky pass (maximum
about 43) carry physical radiance with the game's sun intensity 78.125.

The froxel fog explains the white horizon. Pass 0096 (16x9x12 groups) writes
per-froxel scattering (maximum 0.89). Pass 0097 (16x9x1 groups) integrates the 96
slices: in-scattering reaches 8.5 and the optical depth (`r16f`) reaches **53**, so
far geometry is fully replaced by bright haze. Its SPIR-V computes the slice
thickness exactly as Lichii's workaround finds it:
`near * (far/near)^(t^k)` evaluated at `(i +/- 0.5) / N`, subtracted, with near,
far and k in `c3` row 24 (`.x`, `.y`, `.w`) and the volume extents from another
`c3` row. The arithmetic is correct, so a too-thick slice would come from those
inputs or from the density written by pass 0096 (a 17,500-line shader reading
`c3` rows up to 794). Both Lichii and this port share the Eglantine lowering, the
only common component that could make both emulators too bright. The next capture
prints 64 rows of every uniform block (`TOTK_DUMP_UBO_ROWS=64`) to read row 24.

Capture `bright-06` (64 uniform rows) gives the fog constants: `c3` row 24 is
(near 1, far 500, 29.95, exponent 1), so the 96 slices span 1 to 500 m on a log
scale; row 30 holds (0.00294, 0.004, 55, 0). Passes 0096 and 0097 use two sets of
three volumes and swap them every frame (0x2689ef000/0x2690b1000/0x269773000 and
0x268d50000/0x269412000/0x269924000 alternate across captures), so pass 0096's
history read of the volume that 0097 overwrites later in the same frame is the
intended ping-pong, not aliasing.

Pass 0096 blends its result with that history (`cur + (history - cur) * 0.8`).
Tracing its SPIR-V (`build/oot-fix/spv_trace.py`, private) shows that the `r16f`
value is a 0..1 factor built in shared memory, `0.5 * (clamp(1 - a) + clamp(1 - b))`
(two visibility-like samples), scaled by `1 + (1 - x) * (c5 value - 1)`; it is not
an extinction coefficient. Its raw volume has a median of 0.15 to 0.36 in slices
8 to 16 and falls to 0 beyond slice 48. Pass 0097 accumulates it times the slice
thickness, so its 53 is a weighted distance in metres. An earlier reading of that
value as an optical depth 25 times too large is withdrawn: whether the haze is
too thick depends on how the apply pass combines it with the row 30 densities,
which has not been traced yet. The excess brightness is therefore still open.
Next: trace the pass that applies the integrated volumes to the scene, and the
inputs `a`, `b` and `x` of pass 0096, against the same shaders in Lichii.

### Fog application traced to material shaders (2026-11-06)

Tracing the existing `bright-06` capture identifies application in the material
fragment shaders, rather than a single final full-screen fog composite. The
small variant `73182473e0a7b3ca` in pass 0188 samples two RGB volumes only:
slot 14 is the integrated RGB extinction at `0x2690b1000`, and slot 15 is the
integrated in-scattering at `0x2689ef000`. For its ordinary distance branch the
per-channel operation is `base * exp2(-tau * w) + scattering * w`, where
`w = clamp(distance / c15[95].x, 0, 1)`. Its volume depth coordinate uses the
logarithmic distance interval from `c15[95].xy`, inverse power `1/c15[95].z`,
and the scale in `c15[96].z`. A separate branch handles distances beyond 1e6.

The ordinary material variant `b8b8365003eb165b` in pass 0200 also ends with
this RGB attenuation/scattering operation. Its R16 sample (slot 23) follows a
different path: with `d` denoting the negation of that path's signed distance,
it computes `clamp((R16 + max(0, d - far)) / d, 0, 1)` before additional lighting
terms. Thus the integrated R16 quantity is divided by distance and clamped;
its raw maximum near 53 is not an exponential optical depth of 53. This further
rejects the earlier proposal to infer fully opaque haze from that value alone.
The trace establishes shader data flow, not correctness of its input constants,
source scattering, or the complete material's final lighting.

An asynchronous read-only process-memory check at the previously captured c15
address obtains row 95 `[1, 500, 1, 1]` and row 96 `[1, 1, 1, 0]`. These agree
with the integration's near/far/power and full volume dimensions. This is a CPU
snapshot of a known address, not proof of the currently bound block at a particular
draw; an ordered dump is still needed to establish that stronger claim.

Decoding all three integrated raw volumes in `bright-06` finds only finite values.
The final slice's median RGB transmission `exp2(-tau)` is approximately
`[0.865, 0.848, 0.823]`, rather than near zero. The RGB in-scattering median is
`[0.266, 0.232, 0.207]`, with channel maxima `[8.625, 6.375, 4.125]`.
Dividing the final-slice R16 integral by 500 gives a maximum of 0.106 and a median
of 0.00490. These statistics describe this volume, not the spatial distribution
of pixels in the image. The private numerical summary is
`cond-record-07/fog-apply-volume-summary.json`.

No fog multiplier or exposure adjustment was applied. Next isolate the RGB
source-volume extinction and scattering, and confirm the material's c15 rows
95/96 at the same replay position; the existing 64-row dump stops before them.
Private disassemblies: `bright-06/fog-apply-small.spvasm`,
`fog-apply-7fab.spvasm`, and `fog-apply-material.spvasm`.

### Bananza light-quad filter (largest share of the white)

`runtime/vk_exec_draw.c`, `light_quad_fault_skip`, is a Bananza mitigation for an
NVIDIA device fault (#9): it skips draws whose vertex stage reads an input at
Location 9 and whose fragment stage reads the storage buffer at c0+0x530. In HEAD
it is on by default on NVIDIA (`cfg_bool("TOTK_FIX_VS_FAULT")` is true when unset).
In Ocarina the same shape matches ordinary world materials, so those draws were
dropped and the sky showed in their place. Runs of 2026-11-05
(`perf-with-validation.log`, `perf-no-validation-pre-f32.log`,
`f32-atomic/run-01.log`) log "light-quad draws with ten instance streams are
skipped on NVIDIA"; capture `bright-01` (same day) shows the whole world white
except Link before the material pass.

The working copy now enables the filter only with `TOTK_FIX_VS_FAULT=1`
(uncommitted). The captures from `bright-03` on were taken with it off: the
warning is absent and the path, rocks, characters and trees are drawn. The white
areas left in those captures are the occlusion-culled terrain described above
(present in depth, conditional colour draws skipped with counters 0 and 0), and
the overall overexposure of what is drawn remains open.
