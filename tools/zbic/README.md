# zbic_dec — decoding ZBIC-compressed NSO segments

`tools/nso_plain.py` converts the ZBIC NSOs of an ExeFS (NSO flag bit 7, firmware 22.0.0+) to plain NSOs the
lifter reads. For each compressed segment it shells out to `build/zbic/zbic_dec`, which this folder builds.
(Ocarina's `main` ships this way; Donkey Kong Bananza does not.)

## Build

`zbic_dec.c` links against [hexkyz/zstd](https://github.com/hexkyz/zstd), a zstd fork that adds the ZBIC frame
format (compression and decompression) behind `ZSTD_ZBIC_SUPPORT`. Stock zstd cannot decode these frames; the
decoder itself is not reimplemented here.

```sh
# 1) the fork, at the commit zbic_dec was built and tested against
git clone https://github.com/hexkyz/zstd
cd zstd
git fetch --depth 1 origin 02a8f10c6f4acfcfba2e31e034fbae9f07938f96
git checkout FETCH_HEAD

# 2) the library, with ZBIC support enabled (it defaults to 0 in lib/zstd.h)
make -C lib libzstd.a CPPFLAGS=-DZSTD_ZBIC_SUPPORT=1 -j"$(nproc)"

# 3) the decoder, into build/zbic/ where nso_plain.py looks for it
#    (run from the repository root; ZSTD points at the fork above)
ZSTD=/path/to/hexkyz-zstd
cc -O2 -I "$ZSTD/lib" tools/zbic/zbic_dec.c "$ZSTD/lib/libzstd.a" -o build/zbic/zbic_dec
```

On Windows the file is `build/zbic/zbic_dec.exe`; the same commands work in MSYS2 UCRT64 with `gcc`.

`zbic_dec <in> <expected_size> <out>` decodes one segment and exits 0 only when the frame decodes to exactly
`expected_size` bytes. `nso_plain.py` checks each decoded segment against the SHA-256 in the NSO header
(offset 0xA0) — the loader's own check — so a wrong decode cannot pass.
