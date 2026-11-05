#!/usr/bin/env bash
# DKB Recomp — build de Donkey Kong Bananza 1.0.0 no Linux.
#
# Uso:
#   ./linux/build.sh --nup  /caminho/bananza.nup
#   ./linux/build.sh --exefs /dump/exefs --romfs /dump/romfs
#
#   --nup FILE        container .nup (base 1.0.0); o script extrai exefs/ e romfs/ sozinho
#   --dump DIR        pasta com exefs/ e romfs/ (ou base/exefs e base/romfs)
#   --exefs DIR       pasta ExeFS já extraída (main, sdk, main.npdm, rtld)
#   --romfs DIR       pasta RomFS solta já extraída, ou já empacotada (romfs.bin + index.json)
#   --jobs N          paralelismo do lifter/compilador (padrão: núcleos - 1)
#   --rebuild-xlate   recompila o tradutor de shaders mesmo que build/xlate/totk_xlate exista
#   --skip-lift       reaproveita generated/dkb100
#   --skip-build      para depois de gerar o C
#   -h, --help
#
# Requisitos (docs/building.md, seção Linux): Linux 6.7+, clang, lld, ninja, make,
# SDL3, Opus, libvpx, zstd, Vulkan 1.3, .NET 10 SDK e uv. O jogo em si NÃO vem no
# repositório: use o dump da sua própria cópia.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="dkb100"
DISPLAY="1.0.0"
GEN="$ROOT/generated/$VERSION"
BUILD="$ROOT/build/$VERSION"
EXTRACTED="$ROOT/build/extracted/$VERSION"
PROFILE="$ROOT/build/profile-$VERSION"
PY="$ROOT/.venv/bin/python"

NUP=""
DUMP=""
EXEFS=""
ROMFS=""
JOBS=""
REBUILD_XLATE=0
SKIP_LIFT=0
SKIP_BUILD=0

die()  { printf 'erro: %s\n' "$*" >&2; exit 1; }
note() { printf '==> %s\n' "$*"; }

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

while (($#)); do
  case "$1" in
    --nup)          NUP="${2:?--nup precisa de um arquivo}"; shift 2 ;;
    --dump)         DUMP="${2:?--dump precisa de uma pasta}"; shift 2 ;;
    --exefs)        EXEFS="${2:?--exefs precisa de uma pasta}"; shift 2 ;;
    --romfs)        ROMFS="${2:?--romfs precisa de uma pasta}"; shift 2 ;;
    --jobs)         JOBS="${2:?--jobs precisa de um número}"; shift 2 ;;
    --rebuild-xlate) REBUILD_XLATE=1; shift ;;
    --skip-lift)    SKIP_LIFT=1; shift ;;
    --skip-build)   SKIP_BUILD=1; shift ;;
    -h|--help)      usage; exit 0 ;;
    *)              die "opção desconhecida: $1 (use --help)" ;;
  esac
done

[[ -n "$NUP" || -n "$DUMP" || ( -n "$EXEFS" && -n "$ROMFS" ) ]] || \
  die "passe --nup FILE, --dump DIR ou --exefs DIR --romfs DIR"

# --dump: aceita <DIR>/{exefs,romfs} ou <DIR>/base/{exefs,romfs}.
if [[ -n "$DUMP" ]]; then
  if [[ -d "$DUMP/exefs" && -d "$DUMP/romfs" ]]; then
    EXEFS="$DUMP/exefs"; ROMFS="$DUMP/romfs"
  elif [[ -d "$DUMP/base/exefs" && -d "$DUMP/base/romfs" ]]; then
    EXEFS="$DUMP/base/exefs"; ROMFS="$DUMP/base/romfs"
  else
    die "$DUMP não tem exefs/ e romfs/ nem base/exefs e base/romfs"
  fi
fi

# ---------------------------------------------------------------- ferramentas
for tool in clang lld ninja make; do
  command -v "$tool" >/dev/null 2>&1 || die "'$tool' não encontrado no PATH (docs/building.md)"
done
command -v dotnet >/dev/null 2>&1 || die ".NET 10 SDK não encontrado (dotnet)"
command -v pkg-config >/dev/null 2>&1 || die "pkg-config não encontrado"

# AVX2: o runtime e o código gerado usam -march=x86-64-v3.
grep -qw avx2 /proc/cpuinfo || die "a CPU não expõe AVX2"

if [[ -z "$JOBS" ]]; then
  JOBS=$(( ($(nproc) > 2 ? $(nproc) - 1 : 1) ))
fi

# ------------------------------------------------------------- Python venv
if [[ ! -x "$PY" ]]; then
  command -v uv >/dev/null 2>&1 || die "uv não encontrado (https://astral.sh/uv) para criar .venv"
  note "criando .venv (Python 3.12)"
  uv venv --python 3.12 "$ROOT/.venv"
  # Só o necessário para o lifter, o extrator .nup e o empacotador de RomFS.
  uv pip install --python "$PY" numpy lz4 zstandard
elif ! "$PY" - <<'PY' >/dev/null 2>&1
import numpy, lz4, zstandard
PY
then
  note "instalando dependências no .venv"
  command -v uv >/dev/null 2>&1 || die "uv não encontrado para completar o .venv"
  uv pip install --python "$PY" numpy lz4 zstandard
fi

# --------------------------------------------------------------- entrada
if [[ -n "$NUP" ]]; then
  [[ -f "$NUP" ]] || die "arquivo .nup não encontrado: $NUP"
  note "extraindo $NUP"
  "$PY" tools/extract_nup.py "$NUP" --out "$EXTRACTED-dump"
  EXEFS="$EXTRACTED-dump/exefs"
  ROMFS="$EXTRACTED-dump/romfs"
fi
[[ -d "$EXEFS" ]] || die "ExeFS não encontrado: $EXEFS"
[[ -d "$ROMFS" ]] || die "RomFS não encontrado: $ROMFS"
for f in main sdk main.npdm; do
  [[ -f "$EXEFS/$f" ]] || die "$EXEFS/$f não existe (dump base 1.0.0 incompleto)"
done

note "conferindo o ExeFS contra tools/data/expected-dkb100.json"
"$PY" - "$EXEFS" <<'PY'
import hashlib, json, sys
from pathlib import Path
exefs, expected = Path(sys.argv[1]), Path('tools/data/expected-dkb100.json')
data = json.loads(expected.read_text())
bad = []
for name, want in data['exefs_sha256'].items():
    p = exefs / name
    got = hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None
    if got != want:
        bad.append(f'{name}: {"faltando" if got is None else got[:16] + "..."}')
if bad:
    sys.exit('ExeFS não corresponde ao base 1.0.0 (atualização 458752 não é suportada):\n  ' + '\n  '.join(bad))
print(f'ExeFS ok: {data["title_id"]} {data.get("display_version", "1.0.0")}')
PY

# ---------------------------------------------------------- RomFS empacotado
# Um dump já empacotado (romfs.bin + index.json) é usado no lugar; senão o script
# empacota a pasta solta em build/extracted/dkb100/romfs.
PACKED="$EXTRACTED/romfs"
if [[ -f "$ROMFS/romfs.bin" && -f "$ROMFS/index.json" ]]; then
  PACKED="$(cd "$ROMFS" && pwd)"
  note "RomFS já empacotado em $PACKED"
elif [[ -f "$PACKED/romfs.bin" && -f "$PACKED/index.json" ]]; then
  note "RomFS já empacotado em $PACKED"
else
  note "empacotando o RomFS (pode levar alguns minutos)"
  "$PY" tools/dkb_pack_romfs.py --dir "$ROMFS" --out "$PACKED"
fi

# --------------------------------------------------------- tradutor de shaders
XLATE="$ROOT/build/xlate/totk_xlate"
if (( REBUILD_XLATE )) || [[ ! -x "$XLATE" ]]; then
  note "compilando o tradutor de shaders (dotnet publish, uma vez)"
  "$PY" tools/build_xlate.py
else
  note "tradutor de shaders já existe: $XLATE"
fi

# ------------------------------------------------------------------- lifter
if (( SKIP_LIFT )) && [[ -f "$GEN/layout.json" ]]; then
  note "reaproveitando $GEN"
else
  note "traduzindo ARM64 -> C em $GEN"
  "$PY" recomp/gen.py --exefs "$EXEFS" --out "$GEN" --modules main,sdk \
    --patches recomp/patches-dkb100.txt --jobs "$JOBS"
fi

(( SKIP_BUILD )) && { note "parando antes da compilação (--skip-build)"; exit 0; }

# ---------------------------------------------------------------- compilação
note "compilando e linkando (clang/lld/ninja)"
"$PY" tools/build_game.py \
  --version "$VERSION" \
  --expected tools/data/expected-dkb100.json \
  --gen "$GEN" \
  --out "$BUILD" \
  --modules main,sdk \
  --exefs "$EXEFS" \
  --romfs "$PACKED" \
  --profile "$PROFILE" \
  --jobs "$JOBS"

cat <<EOF

Pronto: $BUILD/dkb

Rode a partir da raiz do repositório (ele acha build/xlate/totk_xlate subindo a partir de si):
  ./linux/run.sh
ou:
  $BUILD/dkb

O primeiro boot traduz shaders e é lento. Estado do port: o jogo chega ao tutorial e
ainda pode travar (README, "Status"). Não compartilhe o executável, o C gerado nem o
cache de shaders: tudo isso deriva do dump da sua cópia.
EOF
