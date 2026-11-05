# DKB Recomp no Linux

Porta Linux do [DKB Recomp](../README.md): o mesmo runtime, lifter e back end Vulkan do
Windows, usando os arquivos `*_sdl.c` (janela, input e menu em SDL3) e
`runtime/platform/linux.c` (threads, futex, arquivos POSIX e o write-watch com
`userfaultfd`/`PAGEMAP_SCAN` do Linux 6.7+).

O upstream já traz essa camada de portabilidade; esta pasta reúne o caminho de build
específico do **Donkey Kong Bananza 1.0.0** (`main` + `sdk`, versão `dkb100`), que não é
o mesmo do TotK Recomp (`main` + `subsdk0` + `sdk`).

> **Nada do jogo está no repositório.** Você precisa do dump da sua própria cópia.
> O executável, o C gerado e o cache de shaders derivam do jogo e **não podem ser
> compartilhados**.

## Status verificado nesta máquina

| Etapa | Resultado |
| --- | --- |
| `runtime/` (102 objetos, incluindo `nvn2_lower.cpp` e `eglantine`) | compila com clang 23 no Linux |
| tradutor de shaders (`tools/build_xlate.py`, NativeAOT linux-x64) | compila e gera `build/xlate/totk_xlate` |
| `tools/check_platform.py` (nenhum Win32 fora de `runtime/platform/`) | passa, 0 usos |
| link completo do runtime + `dkb --selftest` (tabela de módulos sintética, sem jogo) | passa, 0 falhas |
| build completo do jogo (`main` + `sdk`) | passou com o dump base 1.0.0; o executável Linux e o `--selftest` passaram |

 O runtime e as ferramentas já são portáveis e compilam no Linux, mas uma partida longa
de Bananza no Linux ainda não foi relatada. O tutorial 3D é o ponto de partida esperado;
travadas e efeitos faltando são os mesmos problemas conhecidos do port Windows (veja
"Status" no README).

## Requisitos

- Linux 6.7 ou mais novo (write-watch com proteção de escrita via `userfaultfd`).
  SteamOS 3.8, Arch, Fedora 40+, Ubuntu 24.04+ servem.
- CPU x86-64 com AVX2; 16 GB de RAM (o jogo usa ~13 GB).
- GPU com Vulkan 1.3 e `VK_EXT_external_memory_host` (RADV ou NVIDIA proprietário).
- Pacotes (exemplo Arch):

  ```sh
  sudo pacman -S --needed clang lld ninja make pkgconf sdl3 opus libvpx zstd \
      vulkan-headers vulkan-icd-loader vulkan-validation-layers shaderc spirv-tools \
      dotnet-sdk uv git
  ```

  Debian/Ubuntu: `clang lld ninja-build make pkg-config libsdl3-dev libopus-dev libvpx-dev
  libzstd-dev libvulkan-dev vulkan-validationlayers-dev glslang-tools spirv-tools dotnet-sdk-10.0`.
  O `vk_mem_alloc.h` (Vulkan Memory Allocator) é baixado uma vez para `toolchain/vma/` se
  não estiver no sistema.

## Build

```sh
chmod +x linux/build.sh linux/run.sh

# a partir de um container .nup da sua cópia (base 1.0.0)
./linux/build.sh --nup /caminho/bananza.nup

# ou de uma pasta que já tem exefs/ e romfs/ (ou base/exefs e base/romfs)
./linux/build.sh --dump /caminho/do/dump

# ou de pastas escolhidas
./linux/build.sh --exefs /dump/exefs --romfs /dump/romfs
```

### Formatos aceitos

| Entrada | Como |
| --- | --- |
| `.nup` (container do Eglantine/Eden) | `--nup arquivo.nup`; o script chama `tools/extract_nup.py` |
| pasta `exefs/` + `romfs/` já extraídas | `--dump DIR` ou `--exefs DIR --romfs DIR` |
| `romfs.bin` + `index.json` já empacotados | `--exefs DIR --romfs DIR`; o script não repacota |
| `.nsp` / `.xci` / `.nsz` / `.xcz` | **não há extrator de Bananza** no repositório para esses formatos (`tools/extract.py` é do TotK/Switch 1). Extraia o ExeFS e o RomFS com uma ferramenta que entenda Switch 2 e passe as pastas com `--exefs/--romfs` |

O `tools/extract_nup.py` também aceita `--list` (mostra os pacotes do container) e
`--package N` (escolhe outro pacote). Para o Bananza só o pacote base 1.0.0 é suportado;
a atualização 458752 é recusada pelo hash do ExeFS.

O script:

1. cria `.venv` (Python 3.12) com `numpy`, `lz4` e `zstandard`;
2. extrai o `.nup` (quando usado) e confere o ExeFS contra
   `tools/data/expected-dkb100.json` — atualização 458752 é recusada;
3. empacota o RomFS em `build/extracted/dkb100/romfs` (`romfs.bin` + `index.json`);
4. compila o tradutor de shaders em `build/xlate/totk_xlate` (uma vez, ~40 s);
5. traduz `main` e `sdk` de ARM64 para C em `generated/dkb100`, com
   `recomp/patches-dkb100.txt` (o patch do controle é obrigatório);
6. compila e linka `build/dkb100/dkb`.

Opções úteis: `--jobs N`, `--skip-lift` (reaproveita o C gerado), `--skip-build`,
`--rebuild-xlate`.

### Alternativa: instalador

O instalador do repositório também roda no Linux em modo dev (ele usa `clang`, `lld` e
`ninja` do PATH e as bibliotecas do sistema). Depois de compilar o runtime e o tradutor
uma vez:

```sh
.venv/bin/python tools/build_game.py --make-runtime-lib --out build/install-rtlib \
    --version dkb100 --gen generated/dkb100 --expected tools/data/expected-dkb100.json
.venv/bin/python -m installer --game bananza \
    --exefs /dump/exefs --romfs /dump/romfs --dest "$HOME/Games/DKBRecomp"
```

O instalador grava `dkb`, `romfs-index.bin`, o perfil e o `totk.ini` na pasta de destino e
roda `dkb --selftest` no fim.

## Rodar

```sh
./linux/run.sh
# ou
build/dkb100/dkb
```

Rode da raiz do repositório: o jogo encontra `build/xlate/totk_xlate` subindo a partir de
`build/dkb100/`. O primeiro boot traduz shaders e demora; os seguintes usam o cache.

Variáveis úteis (as mesmas do Windows; cada uma aceita o prefixo `DKB_` no lugar de
`TOTK_`):

| Variável | Efeito |
| --- | --- |
| `TOTK_PROFILE_DIR=<pasta>` | pasta de save/configuração (uma pasta vazia = começo limpo) |
| `TOTK_GPU_NAME=<texto>` | escolhe a GPU pelo nome em máquinas com mais de uma |
| `TOTK_GPU=null` | roda sem vídeo (separa bug de CPU/OS de bug gráfico) |
| `TOTK_TRACE=perf` | loga tempo de frame e gargalos a cada 60 frames |
| `TOTK_XLATE_PROCS=<n>` | processos do tradutor em tempo de execução (padrão 2) |

Em Wayland por SSH, exporte `XDG_RUNTIME_DIR` e `WAYLAND_DISPLAY` (ou `DISPLAY=:0` no X11),
senão a janela não abre.

## Solução de problemas

- **Travadas de 1–15 s ao entrar numa área nova** — é o primeiro encontro com cada shader
  e cada pipeline. O log mostra `shader translation` e `pipeline build` altos nesse frame
  (ex.: `worst frame 2235: 15376.5 ms; shader translation 3682.0, pipeline build 10918.9`).
  Nada falhou: o runtime grava `shader-answers.bin`, `shader-programs.bin`,
  `pipeline-keys.bin` e `shader-cache/*.spv` em `build/dkb100/`. Feche e abra o jogo de
  novo: o próximo boot aquece os pipelines de `pipeline-keys.bin` antes do jogo começar
  (`vk_pipeline: prewarm: N records ...`) e as travadas em jogo somem. Para conferir:
  `grep -E "prewarm|warm list|0 failed" build/dkb100/logs/totk.log`.
  `TOTK_PIPE_PREBUILD=0` desliga esse aquecimento; `TOTK_PIPE_SKIP=fullscreen` (padrão)
  pula desenhos full-screen enquanto o pipeline constrói.
- **Avisos `无 PC 类型, 按 2D`** — o tradutor não recebeu o tipo de uma textura e assume
  2D. É um fallback, não um erro de compilação; aparece em poucos shaders (13 de ~540 no
  primeiro tutorial) e só pode afetar o sampling desses shaders.
- **`gpu 0 skipped: no graphics+compute+present queue family`** — sessão gráfica
  inacessível (SSH/headless) ou driver sem Vulkan 1.3.
- **`userfaultfd`/write-watch falha** — kernel < 6.7 ou `vm.unprivileged_userfaultfd=0`.
  Rode com o kernel do sistema atualizado; em alguns casos é preciso `sysctl
  vm.unprivileged_userfaultfd=1`.
- **Travamento alguns minutos depois do primeiro filme** — problema conhecido de GPU
  NVIDIA no upstream (issue #9), não é específico do Linux.
- **Texturas xadrez / terreno preto** — issues #5 e #45 do upstream.

## Diferenças em relação ao Windows

- O executável é `build/dkb100/dkb` (sem `.exe`) e o tradutor é `build/xlate/totk_xlate`.
- O write-watch usa `userfaultfd`; pools visíveis à CPU ganham uma cópia-sombra
  (`runtime/vk_memory.c`), com ~0,2 ms por frame de custo e ~1,1 GB a mais de RAM.
- O link é `-no-pie` com rpath `$ORIGIN`, para o `libSDL3.so.0` que estiver ao lado do
  executável ter prioridade sobre o do sistema (útil para uma pasta portátil).
- Para gerar um AppImage do instalador, `tools/make_release_linux.py` é o caminho do
  TotK; para Bananza ainda é preciso adaptar os nomes (`dkb`, `expected-dkb100.json`,
  `patches-dkb100.txt`). O build manual acima não depende dele.
