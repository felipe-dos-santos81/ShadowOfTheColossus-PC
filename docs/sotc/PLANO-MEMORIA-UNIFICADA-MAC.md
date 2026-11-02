# Memória unificada no macOS: a GPU lê a memória do jogo onde ela está — plano por etapas

Pedido do usuário em 01/11/2026, depois da rodada que levou o Santuário (modo 60 FPS, 720p, M3 Max)
de 23 para 35 FPS. O que resta é a thread de desenho (`shadPS4:GpuDraw`): ~19 ms de trabalho real
por quadro, o limite do quadro agora.

## O que foi medido

Com `SOTC_PROFILE_TIMELINE` (amostra a cada 2 ms) o jogo em si fica parado 74% do quadro, esperando
o resultado da GPU; o quadro é a cadeia processador de comandos → thread de desenho → gravador →
Metal. Trabalho real por quadro (descontando as esperas em spin): gravador 14 ms, desenho 19 ms,
processador de comandos 11 ms.

Na thread de desenho, por quadro:

| Parte | Tempo | O que é |
|---|---|---|
| `SynchronizeMemory` + `memmove` | ~3,5 ms | o que o jogo escreveu copiado para a arena (área temporária + cópia da GPU) |
| `ApplyDownloads`/`ApplyDirect` | ~2,3 ms | o que a GPU escreveu copiado de volta para a memória do jogo |
| proteção de páginas (`mprotect`, `UpdatePageWatchersForRegion`) | ~1,2 ms | descobrir o que o jogo escreveu desde o último envio |
| binds, texturas, pipelines, resto | ~12 ms | custo por desenho do núcleo shadPS4 (perfil plano) |

No PS4 a CPU e a GPU usam a mesma memória; no Mac também. As três primeiras linhas (~7 ms, mais de
um terço da thread) existem só porque o port mantém uma **cópia** da memória do jogo na GPU (a
arena). Este plano tira a cópia do caminho de **leitura** da GPU; o de escrita fica como está.

## Viabilidade (01/11)

- O driver (KosmicKrisp) aceita importar memória do host: `VK_EXT_external_memory_host`, feito com
  `newBufferWithBytesNoCopy` (`kk_device_memory.c`). O port já tem a etapa 1 do
  `MODELO-MEMORIA-PC.md`: `renderer_vulkan/sotc_guest_memory.*` importa a visão `backing` da memória
  do jogo em pedaços de 64 MiB sob demanda (`SOTC_PC_MEMORY=1`), e `SotcGuestMemory::Get(host,
  size)` devolve o `VkBuffer` e o deslocamento de um trecho. Falta conferir que a visão `backing`
  do macOS (`runtime_memory_backing_view`, um segundo `mmap` do mesmo arquivo `shm`) importa.
- A arena **não** pode ser ligada à memória importada: o driver implementa os buffers esparsos com
  `updateBufferMappings` sobre um `MTLHeap` (`kk_queue.c`), e a memória importada é um `MTLBuffer`
  sem heap. Logo o caminho é o da etapa 5 do plano antigo: devolver em `ObtainBuffer` o pedaço
  importado em vez da arena, para buffers que a GPU **só lê**.
- `ObtainBuffer` já tem um desvio desse tipo (`SOTC_STREAM_THRESHOLD`: copia para o *stream
  buffer* em vez da arena quando `!is_written && !IsRegionGpuModified`). O novo desvio entra no
  mesmo lugar, sem cópia.
- Escrita da GPU no lugar (etapa 4 do plano antigo) continua fora: exige o rótulo de fim de
  trabalho honesto, e no Mac a latência da cadeia até a GPU (20–30 ms) tornaria isso um travamento
  por quadro. As leituras de volta continuam pelo caminho direto de hoje (`SOTC_RB_DIRECT`).

## Risco principal

A GPU do port roda **atrasada** em relação ao jogo (a thread de desenho fica ~10 ms atrás do envio,
a GPU mais ainda). Hoje a arena recebe uma foto dos dados quando a thread de desenho processa o
desenho; lendo no lugar, a GPU lê a memória quando **executa**, alguns ms depois. Se o jogo
reescrever um buffer nesse intervalo (porque o port avisou o fim do trabalho cedo), a GPU lê dados
do quadro seguinte: vértices e constantes erradas. O jogo foi escrito para memória unificada e
espera pelos rótulos antes de reutilizar buffers, então o risco é a janela extra, não o modelo.
Só a medição diz: capturas do demo de abertura comparadas, Santuário, campo aberto, um colosso.

## Etapas (cada uma medida antes da próxima; tudo atrás de `SOTC_INPLACE_READS`, desligado)

| # | Etapa | Entrega | Medição | Risco |
|---|---|---|---|---|
| 0 | Importação no Mac | `SOTC_PC_MEMORY=1` no macOS: a visão `backing` importada, log do tempo e do tipo de memória | nada muda no jogo; FPS igual | baixo |
| 1 | Contar o que seria lido no lugar | em `ObtainBuffer`, bytes e chamadas por quadro de buffers só lidos, não modificados pela GPU, inteiros num pedaço de 64 MiB (linha `SOTC FPS`) | quanto dos ~3,5 ms de envio some de fato | nenhum |
| 2 | Leitura no lugar | `ObtainBuffer` devolve o pedaço importado (um `Buffer` do port por pedaço) para esses buffers; sem `SynchronizeMemory`, sem marcar a arena | FPS no Santuário e no demo; capturas do demo iguais às de hoje (`attract.sh`) | médio (a janela acima) |
| 3 | Rastreamento só do que precisa | páginas lidas só no lugar deixam de ser protegidas contra escrita (a arena não precisa saber); a proteção fica para imagens e para buffers que a GPU escreve | `mprotect`/s, FPS; falhas de página por segundo | médio |
| 4 | Padrão no Mac | ligado por padrão depois da rota completa do usuário (santuário → cavalo → troca de área → colosso) | relato do usuário | — |

Critérios de corte, como no plano antigo: imagem igual nas capturas do demo, nenhum bloco verde,
final do jogo sem queda; `SOTC_INPLACE_READS=0` volta ao caminho da arena.

## Estado

- 01/11: plano escrito. Viabilidade conferida no código do driver e do port (acima).
- 01/11: **etapa 0 pronta**: `SOTC_PC_MEMORY=1` no M3 Max importa a visão `backing` (10176 MiB,
  159 pedaços de 64 MiB sob demanda, tipo de memória 0, o único do driver: unificado e coerente);
  pedaço 0 em 0,1 ms. FPS igual (31,6 numa amostra, dentro do ruído).
- 01/11: **etapa 1 medida, e ela encerra a etapa 2.** Santuário parado, modo 60 FPS, `SOTC_PC_MEMORY=1
  SOTC_CMD_STATS=1` (linha `SOTC in place`): 66% dos binds só de leitura (~380 mil/s) poderiam ler
  no lugar, mas eles carregam só **5 MB/s dos 321 MB/s de envios** (9,5 MB por quadro). Os envios
  estão nos binds que a GPU também escreve, ou cujo trecho ligado (buffers grandes, ~50 GB/s de
  trechos ligados por segundo) toca alguma região com escrita da GPU pendente; um bind é um só
  `VkBuffer`, então não dá para misturar leitura no lugar com a arena dentro dele. Ler no lugar
  tiraria ~1,5% do custo de envio: a etapa 2 não compensa neste cenário. Os contadores ficam
  (ligados só com `SOTC_PC_MEMORY=1`) para medir outras cenas (colosso, campo aberto).
- O que sobra da thread de desenho (~19 ms por quadro) é o custo por desenho do núcleo shadPS4
  sob Rosetta, espalhado; e os envios de 9,5 MB por quadro que o jogo faz mesmo parado.
