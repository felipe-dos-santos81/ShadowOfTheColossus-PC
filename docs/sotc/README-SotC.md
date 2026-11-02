# Shadow of the Colossus — PC (port nativo)

Port nativo do **Shadow of the Colossus** de PS4 (CUSA08809, versão EU 1.01, ou CUSA08034,
versão US 1.00) para Windows.
O código do jogo roda direto na CPU. Isto não é um emulador completo: há um carregador e as
funções de sistema de que o jogo precisa. A GPU usa o núcleo de vídeo Vulkan do fork
shadPS4 SotC. A base é o [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (GPL-2.0).

Este pacote **não contém o jogo nem firmware da Sony**. Você precisa ter os seus.

## Requisitos

- Windows 10/11 64-bit e GPU com Vulkan 1.3.
- Python não é necessário para o `SotC.exe`; só o `SotC.bat` precisa de
  [Python 3](https://www.python.org/downloads/).
- O **seu dump** do jogo CUSA08809 ou CUSA08034 (pasta com `eboot.bin`, `sce_module`, `sce_sys`...).
- Para ter som, `libSceNgs2.sprx` e `libSceUlt.sprx` do **seu próprio console**, decriptados.
  São os mesmos arquivos da pasta `user/sys_modules` do shadPS4; num PS4 com GoldHEN ficam em
  `/system/common/lib/` (FTP na porta 2121). Sem eles o jogo funciona, mas sem som.

## Como jogar

1. Abra `SotC.exe`, o lançador.
2. Na primeira vez ele **procura sozinho** nos discos do PC:
   - a pasta do jogo (CUSA08809 ou CUSA08034);
   - os dois `.sprx` de som de um shadPS4 instalado (pasta `user/sys_modules`), que ele copia
     para a pasta de dados do port;
   - um save do jogo no shadPS4, que ele oferece importar;
   - as DLCs do jogo (pasta `addcont` do shadPS4 ou uma pasta com uma subpasta por DLC).
3. Confira em **Jogo e saves**. A **biblioteca de jogos** funciona como no shadPS4: adicione as
   pastas onde ficam os seus jogos e escolha o jogo na lista (duplo clique). **Procurar no PC**
   repete a busca automática.
4. Clique em **JOGAR**.

A tela **Jogar** confere tudo antes: a versão do jogo, os módulos de som, os saves e a placa de vídeo.
Na primeira vez a imagem do jogo é preparada, o que leva alguns segundos. Isso só se repete
quando o jogo ou os módulos mudam. Os saves e o cache de shaders ficam em `user/`.

Sem o lançador, `SotC.bat` faz o mesmo lendo `sotc.ini`. `SotC.exe --play` abre o jogo direto
com as últimas opções do lançador, o que serve para atalhos e para o Steam.

## Imagem e desempenho

Tudo isto está no lançador, na página **Imagem e FPS** (e os principais em **Jogar**):

- **Saída** 720p, 1080p, 1440p ou 4K. O jogo renderiza em 1080p, como no PS4, e a imagem é
  ampliada para a saída com **FSR 1** + nitidez (RCAS) ou bilinear. A janela nunca passa de 90%
  da tela; a tela cheia usa o monitor inteiro.
- **60 FPS** (experimental): tira o limite de 30 FPS do jogo (patch GoldHEN de illusion), com
  vblank a 120 Hz. Mediu 38 FPS no campo aberto e ~25 no templo.
- **Reutilização de binds** (desligada por padrão desde a 0.60.6; `SOTC_FAST_BIND=1` liga): pula
  a remontagem dos descritores quando o desenho seguinte usa o mesmo pipeline, recursos e
  uniforms. Medido no Santuário: ~25 → 30 FPS. Na 0.60.5 deixava pontos pretos em grade no chão de
  areia; a correção aguarda teste.
- **Texturas nítidas**: filtro anisotrópico 16x, quase sem custo.
- **Modo PS4 Pro**: o jogo se vê num PS4 Pro e oferece no próprio menu (Opções > Apresentação)
  o Modo Grafismo **Cinematográfico** (1440p, mais detalhe) ou **Desempenho**. É mais pesado:
  ~25 FPS no campo aberto.
- **Efeitos**: vinheta, névoa atmosférica e oclusão ambiente (SSAO) podem ser desligados.
  O motion blur, o brilho e os filtros ficam no menu do próprio jogo.
- **Cvars do motor** (Avançado): variáveis do motor Bluepoint, `NOME=valor`, para quem quer
  experimentar.

Os modos de patch valem para a versão EU 1.01 e para a US 1.00 (o executável é o mesmo); em
outra versão eles não são aplicados. Cada versão tem os seus saves (`user/savedata/1/CUSA08809`
ou `CUSA08034`) e só usa DLCs da mesma região.

## Controles

**Mouse:** clique na janela do jogo para capturar o mouse, que passa a girar a câmera; Esc ou
trocar de janela solta o mouse. Botão esquerdo = R1 (agarrar), direito = □ (atacar),
meio = L1. Sensibilidade e inversão ficam em **Controles** no lançador. Para remapear os botões
do mouse, use `mouse.<entrada>=left,right,middle,x1,x2`.

No macOS o mouse não é capturado: o cursor fica livre e a câmera gira enquanto o botão esquerdo
está pressionado (arrastar). Esse botão só gira a câmera; para agarrar (R1), use a tecla 3 ou
outro botão (`mouse.r1=x1`, por exemplo). `SOTC_MOUSE_LOOK_BUTTON=right` (ou `middle`, `x1`, `x2`)
troca o botão de olhar; `SOTC_MOUSE_CAPTURE=1` volta à captura do Windows (e `=0` no Windows usa o
arrastar).

No macOS a janela abre em 1280x720 e, com saída abaixo de 1080p, o jogo renderiza no tamanho da
saída (`SOTC_RENDER_RES`) em vez de 1080p reduzido: ~20% mais FPS onde a GPU limita. As threads
do jogo usam a QoS interativa (núcleos de desempenho); `SOTC_QOS=0` desliga.

No macOS a GPU só sobe o clock quando nunca fica ociosa, e o jogo deixa pequenas pausas entre
os trabalhos: ela ficava no clock mínimo (17–23 FPS no Santuário num M3 Max). Com o Mac na
tomada, o port mantém um trabalho mínimo sempre na fila da GPU, que fica no clock máximo
(26–27 FPS ali), ao custo de ~4–6 W a mais. Na bateria isso não roda; `SOTC_GPU_KEEPALIVE=1`
liga também na bateria e `=0` desliga sempre.

No macOS a CPU e a GPU usam a mesma memória: quando o jogo lê algo que a GPU já terminou de
escrever, o port lê direto, sem esperar uma cópia da GPU (30 FPS travados no Santuário, antes
25–27). Se aparecer algum defeito de imagem, `SOTC_RB_DIRECT=0` volta ao caminho antigo para
comparar.

No macOS o driver Vulkan (KosmicKrisp, Vulkan sobre Metal) é compilado por `tools/macos_deps.sh`
com o LLVM do Homebrew (`brew install llvm`), não com o clang da Apple: este transforma cópias de
tamanho fixo em `rep movsb`, que o Rosetta executa 16 vezes mais devagar que um `memcpy`, e o
driver faz duas dessas cópias por desenho (~30% da thread que grava os comandos). Com o driver
refeito, o modo 60 FPS no Santuário passou de ~32 para ~35 FPS; o modo normal segue travado em 30.
Depois de atualizar o Homebrew LLVM ou o script, `rm out/deps-x86_64/.stamp-kosmickrisp` e
`bash tools/macos_deps.sh` refazem o driver.

Um controle (DualShock 4, DualSense, Xbox...) é usado automaticamente. Com vários conectados,
escolha um em **Controles** no lançador (ou `BB_GAMEPAD=` no `sotc.ini`). O teclado funciona
junto com o controle:

| Teclado | PS4 |
|---|---|
| W A S D | analógico esquerdo |
| setas | analógico direito (câmera) |
| Espaço | ✕ |
| Shift esquerdo | ○ |
| E | □ |
| Q | △ |
| 1 / 3 | L1 / R1 |
| R / F | L2 / R2 |
| Z / C | L3 / R3 |
| Enter | Options |
| I J K L | direcional |
| Tab / Backspace | touchpad (esquerda / direita) |

As teclas podem ser trocadas em **Controles** no lançador: clique na ação e aperte a nova tecla.
O controle e o mouse também podem ser remapeados no `sotc.ini`, por exemplo `pad.circle=b`.
As entradas são:

- botões: `cross circle square triangle l1 r1 l2 r2 l3 r3 options touchpad touchpad_right`;
- direcional: `up down left right`;
- no teclado, os analógicos: `move_up` `move_down` `move_left` `move_right` e
  `look_up` `look_down` `look_left` `look_right`.

## Estado

Funcionam: logos, abertura, menus com texto, carregar save, exploração a cavalo,
som (com os módulos do usuário).

Desempenho medido no demo da tela de título, modo 60 FPS, i9-13900K + RTX 2060 SUPER:
média de ~67 FPS; o trecho mais pesado (a ponte do cânion) fica em 29–34 FPS. No modo
normal o jogo roda travado em 30 FPS, como no PS4.

## Perguntas comuns

- **DirectX 11/12?** Não. O núcleo de vídeo (shadPS4) traduz a GPU do PS4 para Vulkan; os
  shaders do jogo viram SPIR-V. Toda placa de vídeo com DirectX 12 recente também tem Vulkan
  1.3: atualize o driver. Um modo DirectX exigiria reescrever o núcleo de vídeo inteiro.
- **Placas AMD e Intel?** As otimizações valem para qualquer placa, menos duas que só ligam
  sozinhas na NVIDIA (o envio periódico de trabalho à GPU e as leituras antecipadas).
  Em **Imagem e FPS > Envio de trabalho à GPU**, escolha **Ligado** para testá-las na AMD ou
  Intel; se a tela ficar preta, volte para **Automático**.
- **Por que o som precisa de arquivos meus?** `libSceNgs2.sprx` e `libSceUlt.sprx` são firmware
  da Sony e não podem ser distribuídos. Eles saem do seu PS4 (ou do shadPS4 que você já usa).

## Créditos

- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (bbport, deadinside28 e forks): carregador,
  runtime nativo e lançador, base deste port.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) e o fork shadPS4 SotC: núcleo de vídeo Vulkan.
- [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs): as cvars de culling, a proteção
  do heap do jogo contra leituras da GPU (`bpe_heap_guard`), o flush periódico, as leituras antecipadas,
  o marcador de espera e o gravador de comandos Vulkan em thread própria (`cp_record_thread`).

## Diagnóstico

Estas variáveis de ambiente ajudam a investigar problemas:

| Variável | Efeito |
|---|---|
| `SOTC_FPS=1` | taxa de quadros a cada 5 s |
| `BB_FAULT_RING=arquivo` | grava as últimas exceções (sobrevive a quedas) |
| `BB_STACKS_TRIGGER=arquivo` | criar o arquivo despeja as pilhas das threads |
| `BB_READBACKS=0/1/2` | leituras da GPU; o padrão 2 é o exigido pelo jogo |
| `SOTC_FAST_BIND=1` | reaproveita binds de descritores entre desenhos iguais (padrão: desligado desde a 0.60.6) |
| `SOTC_RECORD_THREAD=0` | grava os comandos Vulkan na própria thread de comandos (padrão: thread própria) |
| `SOTC_INDIRECT_DIRECT=0` | mantém indiretos os draws cujos argumentos estão na CPU |
| `SOTC_WAIT_MARKER=1/2` | esperas pela GPU consultam um marcador escrito por ela (experimental) |
| `SOTC_CMD_STATS=1/2` | draws e dispatches por quadro (2: shaders de compute mais usados) |
| `SOTC_TSC=qpc/native` | relógio do jogo: `qpc` sempre usa o QPC do Windows, `native` sempre o TSC do processador (padrão: QPC só quando os núcleos discordam no TSC) |
| `SOTC_MUTEX_STATS=1` | macOS: por ponto de chamada, quantos locks de mutex do jogo encontraram o mutex ocupado, quantos dormiram no kernel e os tempos de espera e de posse, a cada 5 s |
| `SOTC_PROFILE_TIMELINE=arquivo` | macOS, com `SOTC_PROFILE=*`: a cada ~2 ms, o estado e a pilha das threads do jogo e da GPU (`tools/profile_timeline.py` resume) |
| `SOTC_PUSH_DESC=0` | associa os descritores a partir de um heap em vez de empurrá-los a cada desenho (move trabalho da thread de gravação para a de desenho) |
| `SOTC_UI_HIRES=0` | com DLSS, a interface (textos, logos, HUD) volta a ser desenhada na resolução de renderização e ampliada, em vez de na resolução de saída |
