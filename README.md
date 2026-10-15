# Shadow of the Colossus — PC (port nativo / native port) · v0.57

**[Português](#português)** · **[English](#english)** · [Releases](../../releases) · [Changelog](CHANGELOG.md)

> **Nenhum arquivo do jogo nem da Sony está incluído / No game or Sony files are included.**
> Você precisa do seu próprio dump do jogo e, para ter som, de dois módulos do seu próprio
> console. Este projeto não tem ligação com a Sony nem com a Bluepoint Games. / You need your own
> game dump and, for sound, two modules from your own console. This project is not affiliated with
> Sony Interactive Entertainment or Bluepoint Games.

> ### Este fork / This fork
> Sobre o carregador e o lançador do bbport, este fork porta o **Shadow of the Colossus** de PS4
> para **Windows 10/11** e **macOS no Apple Silicon**, acrescenta **HDR** automático numa tela que
> o anuncia (`BB_HDR`) e, no macOS, desliga a serialização do quadro: o **envio periódico de
> trabalho à GPU** e as **leituras da GPU assíncronas** — a espera da GPU sai da thread de comandos
> (`SOTC_RB_ASYNC=0` devolve a antiga) — ligam por padrão. O campo aberto saiu de 12–15 para
> 20–26 FPS no padrão de 60 Hz (chegando ao teto de 30 FPS do jogo em medições limpas) e até
> 34–44 FPS com `BB_VBLANK_HZ=120` nas medições mais recentes.
> Para o estudo do ritmo em vblank alto, `SOTC_PRESENT_STATS=1` imprime o que a thread de
> apresentação faz a cada 5 s (vblanks/s, tempo por quadro, atrasos, CPU) e `SOTC_PRESENT_RT=0` a
> tira do escalonador de tempo real do macOS.
> / On top of bbport's loader and launcher, this fork ports the PS4 **Shadow of the Colossus** to
> **Windows 10/11** and **macOS on Apple Silicon**, adds automatic **HDR** on a display that
> reports it (`BB_HDR`), and on macOS stops the frame from serialising: **periodic GPU submission**
> and **asynchronous GPU readbacks** (the GPU wait leaves the command thread; `SOTC_RB_ASYNC=0`
> restores it) are on by default. The open world went from 12–15 to 20–26 FPS at the 60 Hz default
> (reaching the game's 30 FPS cap in clean measurements) and up to 34–44 FPS with
> `BB_VBLANK_HZ=120` in the latest measurements. For the high-vblank
> pacing study, `SOTC_PRESENT_STATS=1` prints what the present thread does every 5 s (vblanks/s,
> per-frame time, lateness, CPU) and `SOTC_PRESENT_RT=0` takes it off macOS's real-time scheduler.

---

## Português

Port nativo do **Shadow of the Colossus** de PS4 (Bluepoint, 2018; CUSA08809, versão EU 1.01) para
**Windows 10/11** e **macOS no Apple Silicon**. O código x86-64 do próprio jogo roda direto na CPU
e a GPU do PS4 é traduzida para Vulkan. Não é um emulador completo: é um carregador com as funções
de sistema de que o jogo precisa, no estilo do
[bloodborne_pc](https://github.com/Supermedo/bloodborne_pc).

### Começando pelo código-fonte

```bash
make deps     # dependências de compilação: brew no macOS, pacman no MSYS2
make build    # out/bb-probe e out/bb-gpu-capabilities
make play     # prepara a imagem do jogo e abre a janela
```

No macOS o renderizador precisa do driver **KosmicKrisp** (`make driver`). O MoltenVK só serve para
os testes de capacidades: ele não expõe *buffer device address*, então `vkGetBufferDeviceAddress`
devolve 0 e o renderizador para. `make driver` compila o Mesa (20–40 minutos) ou, se você apontar
`BB_KOSMICKRISP_URL` (com `BB_KOSMICKRISP_SHA256`) para o `.tar.gz` de [Releases](../../releases),
instala o driver pronto em segundos; `make driver-dist` empacota o driver que você compilou.
`make verify` roda o jogo por 30 s e confere o log; `make help` lista todos os alvos.

Para jogar com o pacote pronto, baixe o `SotC-PC.zip` de [Releases](../../releases),
descompacte e abra o **`SotC.exe`**. Na primeira vez o lançador procura sozinho nos discos do PC
a pasta do jogo, os módulos de som de um shadPS4 instalado (em `user/sys_modules`), um save do
shadPS4 que ele oferece importar e as DLCs. Depois é só conferir **Jogo e saves** e clicar em
**JOGAR**. O pacote não precisa de Python.

#### Os arquivos que você precisa ter

| O quê | De onde | Para quê |
|---|---|---|
| Dump do jogo CUSA08809 (EU 1.01 de preferência) | o seu PS4, pelo GoldHEN | o jogo |
| `libSceNgs2.sprx` e `libSceUlt.sprx` decriptados | o seu PS4 (`/system/common/lib/`, FTP do GoldHEN), ou o `user/sys_modules` do shadPS4 | som |

Os dois `.sprx` são **firmware da Sony** e o port nunca os distribui. Aponte a variável
`SOTC_SYS_MODULES` para a pasta que os contém. Sem eles o jogo roda mudo e o começo do log avisa:
`SOTC_SYS_MODULES not set: no sound`. Os modos de patch (60 FPS, filtro 16x) só valem para a
versão EU 1.01.

### O que o port faz

**Jogo.** Boot com o carregador do bbport (imports por NID, fibras, patches de CPU SSE4a, imagem
abaixo de 2 GiB — o jogo guarda ponteiros em 31 bits), com o núcleo de vídeo do fork shadPS4 SotC:
logos, abertura, menus, campo aberto sem polígonos pretos e sombras corretas. Streaming de
texturas, água com reflexos por *probe*, respingos e anéis, e gravação, carregamento e exclusão
de saves pela lista na tela.

**Lançador.** Biblioteca de jogos com busca automática e importação de saves do shadPS4; saída de
720p a 4K (o jogo renderiza em 1080p e o FSR 1 + RCAS amplia); modo PS4 Pro; 60 FPS e filtro 16x;
controle (DualShock 4, DualSense, Xbox) com câmera no mouse e troca de teclas; cvars do motor. O
registro de cada sessão fica em `user/last_run.log`.

**HDR.** Numa tela que anuncia HDR o port liga a saída HDR sozinho e avisa o jogo que a saída faz
BT.2020 PQ — é isso que faz o ajuste de HDR do próprio jogo aparecer no menu dele. Os quadros saem
em Rec.2020 + PQ, apresentados no macOS com *extended dynamic range*. `BB_HDR=0` força SDR; na
mão contrária, `BB_HDR=1` liga o HDR mesmo numa tela que o sistema não anuncia como HDR, e
`BB_HDR=1 make verify` confere a cadeia inteira.

Em modo 60 FPS o demo da tela de título rende **37–45 → ~67 FPS** numa RTX 2060 SUPER, com
**~17 → 29–34 FPS** na ponte do cânion. Só Vulkan: qualquer placa com DirectX 12 moderno tem
Vulkan 1.3.

### Problemas conhecidos

- Na primeira abertura depois de atualizar, os shaders são compilados de novo: o primeiro
  carregamento demora e engasga.
- Placas **AMD**: a 0.57 corrige as cores verdes/rosas ([#7](../../issues/7)), ainda sem teste
  numa AMD. Se persistir, anexe o seu `user/last_run.log` na issue.
- Com 8 GB de memória de vídeo, o uso fica perto do limite em áreas grandes.

### Novidades

| Versão | Melhorias |
|---|---|
| **0.57** | **AMD:** correção das cores verdes/rosas na névoa e nos personagens. **Opção nova** no lançador para desligar o acesso direto à memória: ~10% mais FPS e água distante mais lisa. |
| **0.56** | **Água com ondas:** lagos e rios deixam de ficar lisos como espelho e o lago de cima ondula e reflete até longe. Árvores distantes sem "lixo colorido". |

Versões anteriores no [CHANGELOG](CHANGELOG.md).

### Perguntas comuns

- **DirectX 11/12?** Não. O núcleo de vídeo traduz a GPU do PS4 para Vulkan, com shaders em SPIR-V.
- **AMD e Intel?** Quase todas as otimizações valem para qualquer placa. As leituras antecipadas
  ligam sozinhas só na NVIDIA; o envio periódico à GPU liga sozinho na NVIDIA e no macOS (driver
  KosmicKrisp). Em **Imagem e FPS > Envio de trabalho à GPU**, escolha **Ligado** para usá-las numa
  AMD ou Intel. Se a tela ficar preta, volte para **Automático**.
- **Por que não vem o som pronto?** Os dois `.sprx` são firmware da Sony; distribuí-los não é
  permitido.
- **O jogo fechou sozinho.** Abra uma *issue* com o arquivo `user/last_run.log`.

---

## English

A native port of the PS4 **Shadow of the Colossus** (Bluepoint, 2018; CUSA08809, EU 1.01) for
**Windows 10/11** and **macOS on Apple Silicon**. The game's own x86-64 code runs directly on the
CPU and the PS4 GPU is translated to Vulkan. It is not a full emulator: it is a loader with the
system functions the game needs, in the style of
[bloodborne_pc](https://github.com/Supermedo/bloodborne_pc).

### From source

```bash
make deps     # build dependencies: brew on macOS, pacman on MSYS2
make build    # out/bb-probe and out/bb-gpu-capabilities
make play     # prepare the game image and open the window
```

On macOS the renderer needs Mesa's **KosmicKrisp** driver (`make driver`). MoltenVK only serves the
capability checks: it does not expose buffer device address, so `vkGetBufferDeviceAddress` returns 0
and the renderer stops. `make driver` builds Mesa (20–40 minutes) or, pointed at the `.tar.gz` from
[Releases](../../releases) with `BB_KOSMICKRISP_URL` (and `BB_KOSMICKRISP_SHA256`), installs a
prebuilt driver in seconds; `make driver-dist` packages the one you built. `make verify` runs the
game for 30 s and checks the log; `make help` lists every target.

To play with the packaged build, download `SotC-PC.zip` from [Releases](../../releases), unzip it
and run **`SotC.exe`**. On first start the launcher searches the PC's drives for the game folder,
the sound modules of an installed shadPS4 (in `user/sys_modules`), a shadPS4 save to import and
the game's DLCs. Then check **Game and saves** and click **PLAY**. The package needs no Python.

#### Files you must supply

| What | Where from | What for |
|---|---|---|
| CUSA08809 game dump (EU 1.01 preferred) | your PS4, through GoldHEN | the game |
| decrypted `libSceNgs2.sprx` and `libSceUlt.sprx` | your PS4 (`/system/common/lib/`, GoldHEN FTP), or shadPS4's `user/sys_modules` | sound |

Both `.sprx` files are **Sony firmware**, and the port never distributes them. Point
`SOTC_SYS_MODULES` at the folder that holds them. Without them the game is silent and the top of
the log says `SOTC_SYS_MODULES not set: no sound`. The patch modes (60 FPS, 16x filtering) apply
to EU 1.01 only.

### What the port does

**Game.** Boots with the bbport loader (imports by NID, fibers, SSE4a CPU patches, an image below
2 GiB because the game packs pointers into 31 bits), with the shadPS4 SotC fork's video core:
logos, intro, menus, open fields without black polygons and correct shadows. Texture streaming,
screen-space water with per-probe reflections, splashes and rings while swimming, and save
create/load/delete from the in-game list.

**Launcher.** Game library with automatic search and shadPS4 save import; 720p–4K output (the game
renders at 1080p, upscaled with FSR 1 + RCAS); PS4 Pro mode; 60 FPS and 16x AF; controllers
(DualShock 4, DualSense, Xbox) with mouse camera and key remapping; engine cvars. Each session
logs to `user/last_run.log`.

**HDR.** On a display that reports HDR the port enables HDR output on its own and tells the game
the output can do BT.2020 PQ — which is what makes the game's own HDR setting appear in its menu.
Frames go out as Rec.2020 + PQ, presented on macOS with extended dynamic range. `BB_HDR=0` forces
SDR; the other way, `BB_HDR=1` enables HDR even on a display the system does not report as HDR,
and `BB_HDR=1 make verify` checks the whole chain.

In 60 FPS mode the title-screen attract demo averages **37–45 → ~67 FPS** on an RTX 2060 SUPER,
with **~17 → 29–34 FPS** in its heaviest section (the canyon bridge). Vulkan only: every card with
modern DirectX 12 also has Vulkan 1.3.

### Known issues

- The first start after updating rebuilds the shaders: the first load is slow and stutters.
- **AMD** GPUs: 0.57 fixes the green/pink tint ([#7](../../issues/7)), not yet tested on an AMD
  card. If it persists, attach your `user/last_run.log` to the issue.
- With 8 GB of video memory, usage gets close to the limit in large areas.

### What's new

| Version | Improvements |
|---|---|
| **0.57** | **AMD:** fixed the green/pink fog and characters. **New launcher option** to turn off direct memory access: ~10% more FPS and flatter distant water. |
| **0.56** | **Water with waves:** lakes and rivers are no longer flat as a mirror, and the upper lake ripples and reflects far away. Distant trees without "colored garbage". |

Earlier versions are in the [CHANGELOG](CHANGELOG.md) (in Portuguese).

### Common questions

- **DirectX 11/12?** No. The video core translates the PS4 GPU to Vulkan, with SPIR-V shaders.
- **AMD and Intel?** Almost every optimization works on any card. Readbacks ahead enable themselves
  on NVIDIA only; periodic GPU submission enables itself on NVIDIA and on macOS (the KosmicKrisp
  driver). Under **Image and FPS > GPU work submission**, choose **On** to use them on AMD or
  Intel. If the screen goes black, go back to **Automatic**.
- **Why no ready-made sound?** The two `.sprx` files are Sony firmware, and shipping them is not
  allowed.
- **The game closed by itself?** Open an issue with `user/last_run.log`.

---

## Compilar / Building

Todas as plataformas usam os mesmos fontes e os mesmos alvos / every platform uses the same
sources and the same targets:

| Alvo / Target | O que faz / What it does |
|---|---|
| `make deps` | Dependências de compilação / Build dependencies (brew, pacman) |
| `make build` | `out/bb-probe` e `out/bb-gpu-capabilities` |
| `make test` | Compila e roda todos os testes / Builds and runs every test |
| `make play` | Prepara a imagem do jogo e joga / Prepares the game image and plays |
| `make launcher` | Lançador de ajustes, macOS/POSIX / Settings launcher, macOS/POSIX |
| `make driver` | Driver KosmicKrisp do renderizador, macOS / Renderer's KosmicKrisp driver, macOS |
| `make driver-dist` | Empacota o driver para release / Packages the driver as a release asset |
| `make verify` | Portão de aceitação do macOS / The macOS acceptance gate |
| `make package` | `dist/SotC-PC.zip` (Windows) |
| `make clean` | Remove as saídas de build / Removes build outputs |
| `make distclean` | Remove tudo que foi gerado / Removes everything generated |

**Windows** com [MSYS2](https://www.msys2.org/) CLANG64: pacotes em
[packaging/windows/README.md](packaging/windows/README.md). Para congelar o `SotC.exe` /
to freeze `SotC.exe`: um Python do Windows com PyInstaller e Pillow / a Windows Python with
PyInstaller and Pillow.

**macOS (Apple Silicon).** Build de x86-64 que roda sob o Rosetta 2, com o Xcode Command Line
Tools / an x86-64 build that runs under Rosetta 2, with Xcode Command Line Tools. `make verify`
roda o jogo por 30 s e confere o log, dizendo qual item faltou quando falha / runs the game for
30 s and checks the log, naming the missing item when it fails.

O empacotamento recusa qualquer `.sprx`/`.prx` / packaging refuses any `.sprx`/`.prx`: Sony
firmware never goes into the package. Ferramentas de teste / test tools: `tools/`. Variáveis de
diagnóstico / diagnostic variables: [docs/sotc/README-SotC.md](docs/sotc/README-SotC.md).

## Créditos / Credits

- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (Supermedo, deadinside28 and
  forks): carregador, runtime nativo e lançador / loader, native runtime and launcher. README
  original: [docs/bbport](docs/bbport/README.md).
- [shadPS4](https://github.com/shadps4-emu/shadPS4) e o fork / and the shadPS4 SotC fork:
  núcleo de vídeo Vulkan / Vulkan video core.
- [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs): cvars de culling /
  culling cvars, BPE heap guard, envio periódico / periodic submission, gravador de comandos em
  thread própria / command recording thread, correções de shader / shader fixes (fetch-shader
  constants, `V_ALIGNBIT`, `V_CVT_PK_U8_F32`), tabelas de texturas indexadas / dynamic T# tables
  (`dynamic_tsharp_array_size`), a abordagem das estatísticas de LOD / the LOD statistics
  approach e o acesso direto à memória para o SotC / and direct memory access for SotC.
- Patch de 60 FPS / 60 FPS patch: illusion (GoldHEN cheats).

Licença / License: **GPL-2.0-or-later** ([LICENSE](LICENSE)).
