# Opens DK Bananza with the stage skip list. Set TOTK_GPU_NAME to part of your card's name if the PC has more than one GPU.
Set-Location (Join-Path $PSScriptRoot "..\build\dkb100")
if (-not $env:TOTK_GPU_NAME) { $env:TOTK_GPU_NAME = "NVIDIA" }
$env:DKB_SKIP_STAGE = "fb6e80e4f0bd1ac5,140a1eac75f7b5c0,163bd4735277403a,f7d00699dae00a95,0e11b0ec10da5ba9,f97309e8c87bb4de,9d968c53a40627d1,a0c3abb12477b0d6,0935b40c3f9bae0d,3fe2565417ad0bf1,2b4d4b1fc1752cfb,dab38509f781bfc9,b1c4eab4cdef0e08,f5f19569286aa241,837fa78832bcb26c,a69f7bf0ed02424b"
.\dkb.exe
