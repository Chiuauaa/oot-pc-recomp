#!/usr/bin/env bash
# Roda o DKB Recomp compilado no Linux. Argumentos extras vão para o jogo
#   (ex.: ./linux/run.sh --seconds 30 --shot /tmp/frame.png).
# Bananza's destructible terrain needs the native NVN 2 passthrough geometry path (#45).
# Keep it enabled by default; set DKB_NATIVE_GS=0 to disable it for diagnosis.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE="$ROOT/build/dkb100/dkb"
[[ -x "$EXE" ]] || { echo "erro: $EXE não existe; rode ./linux/build.sh primeiro" >&2; exit 1; }

# O jogo procura o tradutor de shaders subindo a partir do próprio executável.
# Rodar da raiz também deixa logs/ e o perfil padrão previsíveis.
cd "$ROOT"
exec env DKB_NATIVE_GS="${DKB_NATIVE_GS:-1}" "$EXE" "$@"
