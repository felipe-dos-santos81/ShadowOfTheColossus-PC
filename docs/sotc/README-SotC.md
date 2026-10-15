# Shadow of the Colossus — PC (port nativo)

Port nativo do **Shadow of the Colossus** de PS4 (CUSA08809, versão EU 1.01) para Windows.
O código do jogo roda direto na CPU. Isto não é um emulador completo: há um carregador e as
funções de sistema de que o jogo precisa. A GPU usa o núcleo de vídeo Vulkan do fork
shadPS4 SotC. A base é o [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (GPL-2.0).

Este pacote **não contém o jogo nem firmware da Sony**. Você precisa ter os seus.

## Requisitos

- Windows 10/11 64-bit e GPU com Vulkan 1.3.
- Python não é necessário para o `SotC.exe`; só o `SotC.bat` precisa de
  [Python 3](https://www.python.org/downloads/).
- O **seu dump** do jogo CUSA08809 (pasta com `eboot.bin`, `sce_module`, `sce_sys`...).
- Para ter som, `libSceNgs2.sprx` e `libSceUlt.sprx` do **seu próprio console**, decriptados.
  São os mesmos arquivos da pasta `user/sys_modules` do shadPS4; num PS4 com GoldHEN ficam em
  `/system/common/lib/` (FTP na porta 2121). Sem eles o jogo funciona, mas sem som.

## Como jogar

1. Abra `SotC.exe`, o lançador.
2. Na primeira vez ele **procura sozinho** nos discos do PC:
   - a pasta do jogo (CUSA08809);
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
- **Texturas nítidas**: filtro anisotrópico 16x, quase sem custo.
- **Modo PS4 Pro**: o jogo se vê num PS4 Pro e oferece no próprio menu (Opções > Apresentação)
  o Modo Grafismo **Cinematográfico** (1440p, mais detalhe) ou **Desempenho**. É mais pesado:
  ~25 FPS no campo aberto.
- **Efeitos**: vinheta, névoa atmosférica e oclusão ambiente (SSAO) podem ser desligados.
  O motion blur, o brilho e os filtros ficam no menu do próprio jogo.
- **Cvars do motor** (Avançado): variáveis do motor Bluepoint, `NOME=valor`, para quem quer
  experimentar.

Os modos de patch só valem para a versão EU 1.01; em outra versão eles não são aplicados.

## Controles

**Mouse:** clique na janela do jogo para capturar o mouse, que passa a girar a câmera; Esc ou
trocar de janela solta o mouse. Botão esquerdo = R1 (agarrar), direito = □ (atacar),
meio = L1. Sensibilidade e inversão ficam em **Controles** no lançador. Para remapear os botões
do mouse, use `mouse.<entrada>=left,right,middle,x1,x2`.

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
| `SOTC_RECORD_THREAD=0` | grava os comandos Vulkan na própria thread de comandos (padrão: thread própria) |
| `SOTC_INDIRECT_DIRECT=0` | mantém indiretos os draws cujos argumentos estão na CPU |
| `SOTC_WAIT_MARKER=1/2` | esperas pela GPU consultam um marcador escrito por ela (experimental) |
| `SOTC_CMD_STATS=1/2` | draws e dispatches por quadro (2: shaders de compute mais usados) |
| `SOTC_PRESENT_STATS=1` | o que a thread de apresentação faz a cada 5 s: vblanks/s, tempo por quadro, atrasos de ritmo e CPU |
| `SOTC_PRESENT_RT=0` | deixa a thread de apresentação fora do escalonador de tempo real do macOS |
| `SOTC_THREAD_STATS=1` | tempo por thread de renderização a cada 5 s: gravação, execução dos comandos, esperas, submits |
| `SOTC_DRAW_STATS=1` | raio-X dos draws a cada 5 s: fases da chave do pipeline (estágios, flatbuf), preparação, bindings de buffer/textura, estado dinâmico, emissão (amostra 1 em 16) |
| `SOTC_RB_ASYNC=0` | leituras da GPU síncronas (padrão: assíncronas no SotC com o KosmicKrisp; `=1` força em qualquer placa/jogo) |

### Onde o tempo vai (medido)

O quadro, medido no macOS com o KosmicKrisp (`SOTC_THREAD_STATS=1`, `SOTC_DRAW_STATS=1`,
`SOTC_CMD_STATS=1`): a GPU **não** é o muro (fica ~48% do tempo ociosa, ~18 ms de trabalho por
quadro de ~33 ms); o muro é a CPU: a thread de comandos grava cada draw/dispatch em ~5,3 µs
(~2,2 mil draws por quadro no campo aberto).

O raio-X de um draw (µs, amostrado 1 em 16, campo aberto):

| Parte | µs | Dá para pular? |
|---|---|---|
| Percorredor do SRT: relê as tabelas de descritores do jogo na memória do convidado | 0,7–0,9 | não: o motor reescreve as tabelas a cada draw |
| Bindings de buffer (`ObtainBuffer`, cercas de escrita) | 1,2–1,4 | não: o estado vem da memória do convidado |
| Bindings de textura (`FindImage`, amostras, transições de layout) | 0,9–1,1 | não: idem, e as gerações do cache mudam |
| Preenchimento da chave do pipeline | ~0,3 | em parte, mas pouco |
| Estado de renderização (alvos, carga de RT) | 0,26 | em parte |
| Estado dinâmico (viewport, recorte, profundidade, mistura) | 0,5 | ~0,3 com um guarda de dezenas de campos |
| Busca do pipeline no mapa (74–80% das vezes a chave repete) | 0,10 | sim, mas o ganho é invisível |
| Emissão do draw | 0,05 | — |

Tentado e recusado: **reusar o draw anterior quando os registradores repetem**. 72–80% dos draws
repetem a chave do pipeline, mas registradores iguais **não** significam descritores iguais — as
tabelas de descritores (SRT, sharp, T# dinâmicos) vivem na memória do convidado e são reescritas
entre draws — e as únicas partes puláveis somam ~0,3–0,4 µs (~7%) com um guarda de dezenas de
campos onde um esquecido significa estado de GPU errado. O caminho de draw está no piso desta
arquitetura; o que rendeu de verdade e está ligado por padrão: envio periódico à GPU, leituras
assíncronas (a thread de comandos não espera mais a GPU: ~0,5–0,65 s/s de espera a menos),
memorando do `FindImage`, desproteção adiantada das páginas de escrita, leituras "quentes" e
argumentos de indiretos lidos na CPU.

### Achados de uma sessão real de jogo

Duas pistas de uma sessão no modo 60 FPS:

- **Índices de SRT em tempo de execução** (`ComputeOffset: ... Phi` em massa antigamente): shaders
  que indexam as tabelas SRT num laço não têm o deslocamento calculado pelo walker; o port copia
  uma **janela** da tabela e o shader indexa dentro dela. Não é falha: os dois shaders que
  disparavam as 178 linhas resolvem-se em **1 janela de 2 KB por invocação** cada. Agora sai
  **uma** linha por shader compilado: `SRT walker <hash>: N leituras indexadas em tempo de
  execução em M janelas (K KB por invocação)` — K alto explica draws caros daquele shader.
  Leituras realmente descartadas saem à parte, em nível de erro.
- **Trava rara** (`PS4SyncEvent waited 5–15s` + `flip event queue waited 5s`, cena leve a 9 FPS;
  reproduzida uma vez numa corrida de teste, com a janela em primeiro plano e **sem** eventos de
  janela): os **dois** eventos inscritos na fila (flip e vblank) ficaram 5 s sem disparar, ou seja
  o laço de vblank da thread de apresentação congelou por segundos. A janela agora regista
  `Window: focus lost/occluded/minimized` (e o regresso) para descartar o segundo plano, e
  `SOTC_PRESENT_STATS=1` mostra o pior tempo de `present`. Se travar de novo, capture com
  `SOTC_FPS=1 SOTC_PRESENT_STATS=1 SOTC_THREAD_STATS=1`.
