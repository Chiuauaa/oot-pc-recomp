# Ocarina of Time port: status and findings

Newest first. Title 0400486024DEC000, update 1.1.1, build `oot111`.

## 5 November 2026 (night)

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

1. Confirm on screen that gameplay shows (screenshots after loading).
2. The composite fills only the top-left 1706x960 of 2560x1440: the dropped neural upscaler did that scaling. The
   stand-in blit (vk_exec_draw.c `upscale_standin`) only finds compute-written sources; in gameplay the source is a
   render-pass output. `OOT_TENSOR_LOG=1` lists what each dropped stage binds.
3. 62 tessellation shaders fail lowering ("native tessellation stage buffer read mode", ISBERD 0x923).
4. Frame rate.
