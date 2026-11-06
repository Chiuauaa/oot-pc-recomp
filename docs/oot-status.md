# Ocarina of Time port: status and findings

Newest first. Title 0400486024DEC000, update 1.1.1, build `oot111`.

## 6 November 2026: brightness transition and mesh-shadow mask isolated

- Correlate the user-identified capture 8 pass 125 and capture 17 pass 153:
  the visible lift occurs at the shared bloom/tone-map shader, then display
  encoding. Same-frame CPU replay and separate live bloom/scene controls pass;
  a duplicated sRGB conversion is not measured.
- Trace omitted mesh shadow draws to a screen-mask input of the terrain shader.
  Correct alpha-channel endpoints show strong attenuation of ambient and direct
  light. This proves an input dependency, not the absent meshes' coverage or
  that their omission explains the whole reported brightness.
- Restore normal rendering after capture 17. Record mesh packet arguments in
  future command captures; runtime compilation passes. No new visual fix is
  claimed. See the rendering investigation handoff for numbers and caveats.

## 6 November 2026: task/mesh bindings and actual shared allocations tested

- Map TS/MS to separate NVN resource stages and driver storage tables
  c0+0x110/c0+0x310; keep six independent cached driver banks. Production
  mapping/fill/resolution tests pass 78 checks at O0 and O2.
- Derive shared-memory bytes from emitted SPIR-V, including compiler ISBE
  scratch. Check both standalone shared and joint payload/shared limits.
  Largest retained allocation is 16,916 bytes; 41 modules validate.
- Contract tests now pass 52 checks per optimization; indirect compute passes
  34 checks after updating its fixture for the earlier optional probe hook.
- Mesh pipeline/draw execution is still pending; no residual brightness fix
  is claimed. The handoff records the next contracts to validate.

## 6 November 2026: task/mesh metadata matched to emitted modules

- Add mesh contract version 1; reject reflection that disagrees with SPIR-V
  stage, literal local sizes, output counts/topology or flat payload array.
- Add a tested device-limit/payload compatibility helper for future pipelines.
  It is not wired into replay; shared/output memory validation still remains.
- 45 synthetic contract checks pass at O0/O2; all 41 retained scene modules
  satisfy the production validator and `spirv-val`; runtime compilation passes.
- Mesh draw execution remains pending. The remaining brightness is unresolved.

## 6 November 2026: task/mesh compiler client continued and bounded

- Retain task/mesh native inputs and stage-6/7 resource bindings; add an
  experimental native compiler client and Vulkan capability detection.
- Correct the remaining six-element answer cache, bound resource request
  serialization, validate exact reply keys, and test the production client.
- Client O0/O2, shader-control, native-stage and mesh-recorder checks pass;
  runtime compilation succeeds; all 41 retained compiler fixtures validate.
- Mesh pipelines/draw execution and metadata validation are still missing.
  This is partial integration, not a confirmed brightness or missing-mesh fix.
- The investigation handoff also records atmospheric upload/sample comparisons
  through capture 63 and the limits of those measurements.

## 6 November 2026: dynamic ambient contribution isolated; normalization checked

- Suppressing additions only in compute `21238adc0f99baaf` reduces terrain
  term B substantially; restoring it reproduces baseline. This is diagnostic,
  not a correction. Normal FS and CS are restored in capture 54.
- An independent same-frame CPU integration matches the GPU's L0 magnitude
  within one R11/R10 diagnostic storage ULP. No large new normalization error
  is established. Continue through its atmospheric 3D input and the separate
  baked/visibility texture; do not disable ambient light as a permanent fix.
- The investigation handoff records exact values, native VS disassembly,
  artifact paths and the latest upstream review. No new brightness fix is claimed.

## 6 November 2026: terrain texture is dark before lighting

- New live probes find dark blended/final material albedo near RGB
  [0.101,0.092,0.028]. GPU-side range tests find no large blend-weight
  extrapolation in 673,768 marked, depth-matched terrain pixels.
- Red environment/material terms B/D dominate direct-loop term A in this view.
  Trace their cubemap and ambient inputs next; their magnitude alone is not a
  confirmed defect. The investigation document records masks and caveats.
- Follow-up control-flow-correct probes narrow about 96% of red term B to
  evaluated vertex lighting coefficients, about half of total terrain red in
  this view. Its cubemap-diffuse gain is zero; uniform ambient is small.
  Trace VS coefficient reads/normalization next. The handoff explicitly rejects
  three earlier component probes that missed the conditional multiplier.
- No new production brightness correction is claimed.

## 6 November 2026: darker appearance is the user's original-game reference

- The user confirms the darker tonality is closer to the original, which has much
  less glow. Diagnose excessive brightening; do not brighten stones to match the
  washed-out landscape. This does not certify every earlier dark intermediate pass.


## 6 November 2026: shared tonality and postprocessing checked in one frame

- The user clarifies dark stones were an observation of PC captures, not a console
  reference. Do not assume stones are correct or limit the fault to landscape.
- Fresh depth-matched landscape coverage excludes destination-colour accumulation:
  zero source red produces zero red at every tested pixel. Linear depth matches
  master geometry depth; near-ground fog adds little in this view.
- Passes 126?134 barely change that region's mean. Pass 151's tone-map/bloom math
  matches CPU replay within 1.077 HDR storage ULP. Its source reaches the upscaler
  unchanged, and final output matches one sRGB encode, not two. Original exposure
  and lighting correctness remain unresolved; no arbitrary scale was added.
- Evidence and exact figures are in the investigation handoff. Diagnostic probes
  are removed; the confirmed flicker correction remains enabled.

## 6 November 2026: continuous landscape material identified

- Coverage probes identify native FS `78e6bf688b49647c` for continuous soil,
  separate from stone paths. Its environment-cube/material terms contribute
  alongside shadowed lighting, but erroneous emission is not established.
- The normal-length probe remains near one. Four isolated colour terms are finite;
  no speculative normalization, roughness or brightness change is installed.
- In user-identified capture 27, all 56,767 sky pixels remaining at pass 175 are
  replaced at pass 176. The earlier depth pass 173 uses a different attachment.
  These intermediate holes do not prove final missing terrain. Excessive final
  brightness is still open. The user supplied a better landscape-dominant angle
  for a fresh mask and a controlled material-blending test.

## 6 November 2026: solar contribution isolated in the fog producer

- Accepted compute probes in the loaded map show direct solar terms dominate the
  remaining scattering. A same-dispatch red-component probe measures approximately
  99% direct solar, with a small remainder. This localizes arithmetic to inspect;
  it does not prove the game's sun intensity needs scaling.
- A no-history sample is close to normal and two visibility factors are not stuck
  at all-visible. No temporal runaway is measured. Diagnostic acceptance and a
  corrected blue-channel selection are documented, with invalid samples excluded.
- Original compute is verified restored in capture 12. No fragment/compute probe
  is active, conditions are normal, and the flicker correction remains enabled.
  Excessive brightness is still unresolved; no speculative lighting patch added.

## 6 November 2026: remaining light increase isolated inside fog arithmetic

- Accepted component probes show the scene-input pass reproduces all 921,600 pixels
  exactly. Pre-fog changes little; fog transmission stays at or below 1, while its
  scattering term adds mean RGB [0.3348,0.2920,0.2509]. No duplicate destination
  blend, wrong live near/far constants or temporal runaway is measured here.
- These findings localize the increase, not a confirmed brightness correction.
  The scattering producer's sunlight, ambient/local light and history remain the
  next targets. Private component statistics and acceptance caveats are documented.
- Added opt-in compute probes sharing the fragment declaration guard and restore
  logic. Control/profile tests and the declaration guard pass; runtime build/link
  pass. Prepared producer probes preserve declarations and pass spirv-val. No
  permanent density/exposure multiplier or shader suppression is enabled.
- Audited a fork claiming a fog fix: its documented recommendation rejects fog
  shaders. No suppression workaround was imported.

## 6 November 2026: remaining washed-out surfaces are a separate investigation

- The user confirms the ground stopped flickering, then clarifies that approaching
  remaining bright surfaces reveals textures. Continue tracing lighting and
  distance-dependent composition rather than assuming white texture replacements.
- Within one corrected frame, the full-screen composite raises mean HDR RGB from
  [0.4006, 0.3239, 0.2429] to [0.9042, 0.7056, 0.4805]. This localizes an increase,
  not a proven duplicate accumulation bug. Strong highlights also remain in a
  temporary composite-skip capture. Different camera positions prevent a final
  image numerical A/B. The skip file is restored and no brightness scale is added.

## 6 November 2026: unfinished occlusion counters were read as stale completed counts

- Three loaded-map command captures confirm the 256-report ring forgets unfinished
  sample reports: the frames contain roughly 1,880 reports. Hundreds of material
  conditions then use stale counts, including zero counts that skip stone draws.
- Replaced the ring with a growing table retaining the newest report per address
  until completion. Timestamp-zero and conservative pending-sample semantics stay
  intact. The correction applies to every material and uses no shader hash.
- Production helper/reader tests pass 248,199 checks at each of O0/O2; compile/link
  pass. The user confirms flicker is gone. Three corrected captures show all 17
  stone draws present, zero lost pending sample reports, no sky words in the road
  ROI, and timestamp-guarded depth clears still disabled. Evidence is under
  `bright-coverage-fixed-14/pending-fix-validation.json`.
- Some meshes remain white according to the user. The remaining brightness and
  geometry faults are not considered solved; preserve this confirmed correction.

## 6 November 2026: conditional replay diagnostics include skipped ground draws

- Active captures now add bounded `commands.txt` with operation order, conditional
  counter inputs/decisions, attachment addresses, draw identity and depth clears.
  The previous pass summaries omitted conditionally skipped draws entirely.
- This does not change conditional-render decisions. Incremental compile/link
  pass; the second startup reaches a responding window with visible console and
  file log. `bright-coverage-13` captured three loaded-map frames and exposed the counter
  lifetime defect above. No capture during loading or shader compilation was taken.

## 6 November 2026: ground flicker narrowed to material coverage

- In normal-condition captures 2 and 4 of `bright-subword-12`, the same road ROI
  changes from zero to 25,969 exact sky-colour words out of 27,000 pixels, despite
  valid depth and an identical stone texture. This is evidence of missing material
  coverage; the exact conditional/geometry/raster cause remains unresolved.
- Captures 5?7 were a separate force-all-conditions experiment. They also allow
  conditional clears, leaving the material depth attachment entirely 1.0 and the
  entire material output exactly equal to the preceding sky. This diagnostic
  failure starts before the stone material, not as a white texture replacement.
- Normal conditions are restored. A single pending-query-skip capture and a normal
  control both retain road coverage; neither proves a permanent fix. See the
  investigation handoff for measurements, caveats and evidence paths.

## 6 November 2026: shared subword-load translation implemented

- Guarded global U8/S8/U16/S16 loads now use the existing byte/halfword extraction
  with correct alignment and descriptor bounds. No shader hash or game-specific
  lighting multiplier is involved; vendor source remains unchanged.
- Fourteen CPU/translation tests, 34 subword GPU fixtures and six scalar/vector GPU
  regressions pass. Replaying 60 captured failures recovers 23 request variants
  across three distinct compute-code blobs; the rest reach the descriptor limit.
- The new translator and matching identity are installed beside the runtime, and
  the game is reopened with a direct visible console and normal host file log.
  Startup retries were also needed with the retained old translator; the captured
  fault is a host Cpu-register spill with an ARM return address as its pointer,
  before compute execution. The map is now loaded; comparisons above still show
  brightness/coverage faults.

## 6 November 2026: transport correction covers related shader variants

- The linear tensor replacement selects pre-transport colour by declared image
  writes, backing/view formats, dimensions and input uniqueness. The shader-hash
  gate is removed; ambiguous, missing or incompatible inputs keep the prior path.
- Production source-selection tests pass in 16 cases at O0 and O2. Build/link pass
  with 31 jobs. Loaded-map capture confirms an exact source match and a destination
  within 1.049 storage ULPs of bilinear resizing, without the extra brightness lift.
- Stone-path probes find variable first-light visibility, rather than universally
  missing shadows. A related material reads the same global light block. Material
  brightness and missing colour coverage remain under investigation.

## 6 November 2026: one additional brightness contribution corrected

- The user's pass comparison isolated a real brightness lift after tone-mapping:
  the linear replacement for the tensor upscaler displayed square-root-encoded
  RGB9E5 transport values. For the identified native packer, it now uses the unique
  pre-encoding floating-point colour input. The newer entry generalises selection
  by the resource contract, removing the initial shader-hash gate.
- Live capture verifies an exact source match and output within 1.193 storage ULPs
  of bilinear resizing, with no extra brightness lift or device loss. The user
  reports fewer blown-out areas. Production selection tests pass at O0 and O2.
- Earlier overexposure and missing colour coverage remain. Omitting the sky draws
  exposes black areas where material colour is absent; these should be investigated
  separately from the corrected upscaler. See the investigation document's opening
  section for evidence and limitations.

## 6 November 2026: white areas and brightness narrowed down (Windows, RTX 5080)

- **Frame rate:** 60 fps in the world without `TOTK_VK_VALIDATION`; the earlier 15 fps
  came from the validation layer in the test launchers, not from the code.
- **White world:** mostly the Bananza light-quad NVIDIA filter, which matched ordinary
  Ocarina materials; it is now opt-in (`TOTK_FIX_VS_FAULT=1`).
- **Remaining white patches:** terrain colour skipped by occlusion conditions. Native
  SDK audit disproves the earlier type-16 samples-passed hypothesis: it is a timestamp;
  samples passed remains type 1. Predicate macro semantics and GPU-visible records
  need checking before a production fix (details and next steps in
  `docs/oot-rendering-investigation.md`, "Handoff").
- **Still too bright:** exposure is a constant 1.0 outdoors; the fog chain is under
  investigation. Mesh draws are still not replayed.
- Translator patch 0024 (float atomic add), Lichii #144 and the FSEL/SHF lowering fixes
  are in; production lowering accepts 6391 of 6455 captured stages.

## 5 November 2026 (afternoon): the world renders on Linux

- **State:** Zuriel-Zurichi's `fix/oot-rendering-3d` (c3ad87b: NVN view formats, RGB9E5, followed-texture formats,
  integer sampled types, lowering fixes, guest setjmp/longjmp; docs/oot-rendering-investigation.md) merged over our
  memory work (8 MiB unsized storage cap, windowed shadows), without its translator patches 0022/0023. On a
  GTX 1070 the intro video plays, the save loads and the world shows at a good frame rate. The generator changed
  (setjmp), so `recomp/gen.py` must be run again before `tools/build_game.py`; the translator must be rebuilt
  (`tools/build_xlate.py`, 21 patches).
- **Why the full merge froze on entering the world:** patches 0022/0023 made a compute shader (key `66c9e4a2…`)
  enumerate all 22 uniform/storage records of its c0 bank as guarded candidates. The 495 KB of SPIR-V took the
  NVIDIA compiler 122 s and then 95 s inside `vkCreateComputePipelines` on the replay thread, with 2.6 GB of heap
  (jemalloc profile: `nvvm` / `nvGetCompilerInterface`), so the process passed 10 GB and the PC ran out of memory.
  An RTX 5080 on Windows absorbs it. Without the patches that shader is skipped again; the enumeration can come back
  with a bound on candidates or with compute pipeline builds off the replay thread.
- **Still open: the world's light is blown out and flickers.** The scene is already white in the HDR target before
  bloom and upscale (Zuriel's capture and ours). Leads: GPU-computed values whose compute passes are skipped
  (exposure, 128x72x96 light volumes: 64-bit atomics the translator rejects), and mesh draws.
- **Mesh draws are not replayed at all.** `DrawMeshTasks*` are accepted and dropped (nvn_api_nvn2.c); there is no
  `VK_EXT_mesh_shader` path, and a GTX 1070 has none. `OOT_MESH_LOG=1` found ~30 programs (4034-4095) drawing through
  `DrawMeshTasksIndirect` from the title on, with a mesh stage in the vertex slot, a fragment stage, and in some a
  third (task) stage that does not lower; their indirect arguments live in the buffers the frame's first compute
  passes write (GPU-driven culling). Replaying them needs `VK_EXT_mesh_shader` where present and task/mesh stages run
  as compute (writing vertices and an indirect draw) elsewhere.
- **The neural upscaler** (HMMA/LDSM compute stages) is still replaced by a linear blit; it ran only in the video
  scenes in our captures (the world's upscale into the HUD layer is program 109, an ordinary per-tap gather shader).
  On GPUs without tensor cores, FSR 1 (already in the runtime for the output) is the cheap stand-in; FSR 2/3 would
  need the game's motion vectors.
- Lichii b8c812b18 (#112) lowers Ocarina's 1080p upscaler gathers (LEA in the per-tap offset scan, F16 and guarded
  offsets, depth-compare gathers); most of it already came with c3ad87b.

## 5 November 2026 (morning)

- **2 fps in the world was a 1.4 GB write-back every submission.** Once gameplay starts the game creates a 2.4 GB
  CPU-cached pool (`0x1bf470000+0x98000000`, flags 0x24) and a compute pass binds a guarded NVN 2 storage buffer at
  `0x200000000` with no declared size. `bind_range` bound it to the end of the pool, 1.4 GB, marked written, so each
  submission copied 1.4 GB from the VRAM mirror back to guest memory: a ~120 ms GPU part per frame of ~650 ms, PCIe at
  ~13 GB/s, the SMs mostly idle (`nvidia-smi dmon`), which is why CPU and GPU use looked low. Storage buffers with no
  size are now capped at 8 MiB, as yuzu/Eden do (`OOT_SBO_UNSIZED_MB`, 0 = old behaviour); the bind is now 8 MiB.
- **The same pool doubled RAM and VRAM; fixed with windowed shadows.** Its first buffer use made a whole-pool shadow
  (2.4 GB of host memory, `shadow_make`) and a whole-pool VRAM mirror (2.4 GB, `mirror_create`), though the GPU uses
  one 8 MiB window of it: the process went from 5.3 to 10.6 GB and VRAM to 8.1 of 8.2 GB within ~20 s of entering the
  world. A deferred-shadow pool of `OOT_POOL_WINDOW_MB` (default 256) or more now gets 64 MiB windows every 32 MiB,
  made on first use, with no VRAM mirror (vk_memory.c "windowed shadows"). The world run (win1, 170 s with
  `TOTK_RESOLVE_CHECK=1`) made one window, held ~8.6 GB with 5 GB of the PC still free, and found no resolve-cache
  mismatch.
- The `NVN 2 program (Ampere code): no translator yet` lines start after the memory jump, not before. They come from
  `req_init` in gfx_shader.c refusing an NVN 2 program on the Maxwell path before the failure cache key exists, so
  the same draws retry (and are skipped) twice a frame. Cheap; those objects are missing on screen.
- Diagnostics: `OOT_ALLOC_LOG=<MiB>` logs each device-memory allocation of that size or more with its memory type,
  the live total per type and the host return addresses (vk_headroom.c); `OOT_BIG_POOL_LOG=1` logs the first 60 binds
  inside pools with 256 MiB or more past the address (vk_exec_draw.c `bind_range`).

## 5 November 2026 (night)

- **Running out of memory with the GPU on** (c4b436d). With the GPU the process grew past 7.4 GB in 25 s at the title
  screen (4.1 GB headless) and a 16 GB PC ran out. 4 GB of it were `/dev/nvidiactl` mappings: in shadow mode (the Linux
  default) every CPU-visible pool got a host-visible copy of its whole size when created, and the game creates ~2150
  pools, 2.9 GB of them CPU-cached texture pools (flags 0x24) the GPU only reads through images. Shadows are now made
  at a pool's first buffer use: 19 MB of them at the title, the process stays near 5.2 GB. `TOTK_SHADOW=0` (imports)
  was no help on NVIDIA: importing pins and fills the guest pages, 7 GB in 9 s.
- **Gameplay still needs ~9 GB.** Headless, the process reaches 6.9 GB once Kokiri Forest loads (boot134), ~5.3 GB of
  it guest memory the game fills while loading (a 2.6 GB jump in about 2 s). With the GPU that is ~1.5-2 GB more; the
  PC had ~10.5 GB available with Discord and Brave open (~3 GB together), and the run (boot133) was stopped by the
  watchdog at 1.5 GB available, during the load. Next: close those, or look at the GPU-side host memory (anonymous
  driver maps ~1.2 GB, 256 MiB host-visible VMA blocks).
- `tools/oot_run_watchdog.sh <name> <seconds> [ENV=...]` runs the game with screenshots, memory samples and a
  MemAvailable floor (`WD_KB`, default 2.5 GB).

- **Slow gameplay loading was the hard disk** (299f32e). The game opens thousands of files per area (Kokiri Forest
  ~8000, 2.5 GB), each at its own place in the 25 GB `romfs.bin`. On the spinning disk that holds the image it was
  seek-bound: 99% busy at 10-25 MB/s, ~45 files/s, minutes of black loading screen. Measured with `OOT_FS_STATS=1` and
  /proc/diskstats; the guest scheduler was ruled out first (`TOTK_TRACE=sched` with the GPU on: ~1 of 6 virtual cores
  busy, the loader threads 80-96% in host calls at 1% CPU). Fix: whole-file read-ahead on open, and
  `runtime/romfs_cache.c`, which copies each file the game reads to `~/.cache/oot-pc-recomp/romfs` (8 GB at most, keeps
  10 GB free; `OOT_ROMFS_CACHE=0` turns it off). Cold start to the world clock: 4.5 s instead of 16-50 s; 4500 files in
  20 s instead of ~800.
- **The endless reload of the Great Fairy Fountain files is a damaged dump.** The loader thread (main+0x504ff8 ->
  main+0xe8e80 -> open at main+0x4bbfc) reopens `Land_FairyFountain_Base_A_01.bfres.zs` and
  `CmnTex_Map_Soil_FairyFountain_Floor_D_Nrm.bntx.zs` forever. The second file's first 12 KB are zeros in the `.nup`
  itself (its SHA-256 record matches the zeros), so it never decompresses. A scan of all 102185 `.zs` files found 3
  damaged ones:
  - `Tex/CmnTex_Map_Soil_FairyFountain_Floor_D_Nrm.bntx.zs`
  - `Tex/RBake_Scene_GanonStage_G_2_1_d0_t0.bntx.zs`
  - `Tex/Rock_WaterTemple_Gate_M_01_Nrm.bntx.zs`
  `tools/oot_standin_damaged.py` finds them (streams `romfs.bin` once) and writes stand-ins to
  `<build>/romfs-patches`, which the runtime loads by itself: the game's flat normal map for the two `_Nrm` files and
  `RBake_Scene_GanondorfStage_G_2_1_d0_t0` for the bake. With them the save loads Kokiri Forest in ~40 s and the
  reloading stops. The real fix is a fresh dump of those files. Debug aids: `OOT_FS_REPEAT=1` logs the guest stack of
  the 20th open of one path.
- After loading, the game sometimes gets `LimitReached` from MapPhysicalMemory (alias ~2.8 GB + heap 6.3 GB against
  the 9 GiB pool, `TOTK_APP_POOL`, an estimate). It treats it as recoverable; not investigated further yet.
- A run with the GPU on was stopped by Claude Code when the PC ran low on memory (15.9 GB RAM): watch the game's
  footprint.

## Open work, by priority

1. The world's 3D scene: see docs/oot-rendering-investigation.md (storage/sampled view formats, lowering fixes,
   overexposed HDR scene still open).
2. The composite fills only the top-left 1706x960 of 2560x1440: the dropped neural upscaler did that scaling. The
   stand-in blit (vk_exec_draw.c `upscale_standin`) only finds compute-written sources; in gameplay the source is a
   render-pass output. `OOT_TENSOR_LOG=1` lists what each dropped stage binds.
3. 62 tessellation shaders fail lowering ("native tessellation stage buffer read mode", ISBERD 0x923).
4. Frame rate.
