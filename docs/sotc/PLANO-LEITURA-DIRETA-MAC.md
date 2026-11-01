# Leitura direta na memória unificada (macOS): plano

Pedido do usuário em 01/11/2026, depois do keep-alive da GPU (`60be716`) e do spin no mutex
(`83e7c29`): o gameplay no Mac está em 25–27 FPS no Santuário (save do usuário, M3 Max, modo
30 FPS, 1280x720) e o objetivo é chegar aos 30 FPS travados.

**Este documento é só o plano.** Nada foi implementado. O usuário aprova a etapa 0 antes de
qualquer código.

## 1. O que limita o quadro hoje (medido em 01/11)

Gameplay parado no Santuário, ~5700 comandos por quadro, GPU já no clock máximo:

- A thread do jogo (`Shadow Game Update`) passa ~90% do tempo esperando os lotes de jobs.
- Os 4 workers do jogo (`BPE JobWorkerTh`) ficam ~43% do tempo bloqueados, e ~36% do tempo total
  está dentro de leituras da GPU disparadas por faltas de página
  (`_fault` → `BufferCache::ReadMemory` → espera da GPU).
- `SOTC_DRAW_SPLIT=1`: **~35 esperas de leitura por quadro, ~46 ms de espera somando as
  threads**:

| parte da espera | ms por quadro (soma das threads) | o que é |
|---|---|---|
| queue | 2,2 | o pedido esperando a thread de desenho (`BeginAsyncReadback`) |
| submit | 9,7 | o command buffer com a cópia esperando ser enviado |
| gpu | 27,4 | a GPU terminando tudo o que veio antes da cópia, e a cópia |
| finish | 6,8 | aplicar os bytes de volta na thread de desenho (`FinishAsyncReadback`) |

`SOTC_RB_STATS` no mesmo cenário:

- **Leitura: ~830 faltas por segundo, quase todas leituras de dados que a GPU escreveu.** As
  instruções são as mesmas do Windows (`MODELO-MEMORIA-PC.md`, 27/10): `eboot+0x1a6d60` (culling
  por visibilidade, regiões `0x7075330000`–`0x7075370000`), `eboot+0x101a98` e `eboot+0x11d4a6`.
  É dependência de verdade: o jogo precisa do resultado da GPU.
- Escrita: ~5300 faltas por segundo, espalhadas (`eboot+0x101ae4`, `eboot+0x11ce37`, ...); a
  maioria não precisa de leitura da GPU.
- Leituras da GPU: ~1100–1240 por segundo (finish ~1060, early ~190, heap ~28).

Conclusão: o que pesa não é falso compartilhamento de páginas, e sim o **caminho** de cada
leitura. Ela entra na fila depois de todo o trabalho já gravado (`scheduler.CurrentTick()`),
espera o envio, faz uma cópia arena → staging na GPU e volta para a thread de desenho para
aplicar. Cerca de 40% da espera (queue + submit + finish) é custo do port, não da GPU.

## 2. A ideia: ler o resultado da GPU onde ele está

No Apple Silicon a memória é uma só. O KosmicKrisp anuncia **um único tipo de memória**, ao mesmo
tempo `DEVICE_LOCAL`, `HOST_VISIBLE`, `HOST_COHERENT` e `HOST_CACHED` (`kk_physical_device.c`).
Os blocos das arenas de buffer (`arena_memory_type_index`) são dessa memória, então a CPU pode
mapeá-los.

No Mac, uma leitura da GPU passa a ser assim:

1. Achar os intervalos escritos pela GPU na janela pedida (`GatherDownloads`, como hoje).
2. Saber **qual trabalho da GPU** escreveu esses intervalos por último: um tick por bloco da
   arena, gravado onde os intervalos entram em `gpu_modified_ranges` (`buffer_cache.cpp:1096`).
3. Se a GPU já passou desse tick, nada de espera. Se não passou, esperar **esse** tick (forçando
   o envio só se ele ainda não foi enviado), e não o tick atual com tudo o que veio depois.
4. Copiar os bytes direto do bloco mapeado da arena para a memória do jogo, **pela mesma
   `ApplyDownloads`** (guarda do heap do BPE intocada), sem comando de cópia, sem staging e sem
   `FinishAsyncReadback`.

O que deve cair: o "submit" (~10 ms) e a cópia, quase todo o "finish" (~7 ms) e a parte do
"gpu" que é trabalho posterior aos dados lidos. O culling, por exemplo, é calculado cedo no
quadro e lido mais tarde.

## 3. Etapas

Tudo atrás de `SOTC_RB_DIRECT`, desligado por padrão até a etapa 3. Com a chave desligada, o
comportamento é idêntico ao `83e7c29`.

| # | Etapa | Entrega | Medição | Pronto quando |
|---|---|---|---|---|
| 0 | Medir o ganho possível | tick da última escrita da GPU por bloco da arena; em cada leitura, contar se o tick já terminou, e quanto antes do tick atual ele está (contador em `SOTC_DRAW_SPLIT`) | 3 rodadas no Santuário + o demo da abertura | sabemos que fração das ~35 leituras/quadro não precisaria esperar, e quanto da espera "gpu" é trabalho posterior |
| 1 | Esperar só o tick certo | `ReadMemory` espera o tick dos dados, ainda com a cópia na GPU | as mesmas rodadas; "gpu" por quadro | "gpu" cai na proporção prevista pela etapa 0, sem imagem errada |
| 2 | Cópia direta do bloco mapeado | blocos da arena mapeados (`vkMapMemory`) ao serem ligados; a aplicação lê deles pela `ApplyDownloads` | "submit" e "finish" ~0; FPS | 30 FPS no Santuário ou ganho medido em 5 pares A/B |
| 3 | Ligar no Mac | `SOTC_RB_DIRECT` padrão 1 quando o tipo de memória da arena é `HOST_VISIBLE` | rota completa pelo usuário: santuário → cavalo → troca de área → colosso | nenhuma imagem errada, nenhuma queda, final do jogo sem queda |

**Decisão depois da etapa 0:** se menos de ~30% das leituras tiverem os dados prontos ou bem
antes do tick atual, as etapas 1 e 2 juntas tiram no máximo o custo do port (queue + submit +
finish). Nesse caso a etapa 2 vem antes da 1, e a meta passa a ser ~28 FPS, não 30.

## 4. Pontos delicados (para quem implementar)

- **Escrita da GPU depois do tick lido:** um desenho gravado depois pode escrever o mesmo
  intervalo antes da aplicação. A aplicação confere o tick do bloco de novo; se ele mudou, volta
  ao caminho de hoje (como `readback.dirty` faz com `MarkInflightReadbacksDirty`).
- **Blocos esparsos:** um bloco pode ser religado ou despejado (`SubmitPendingArenaBinds`, despejo
  da arena). O ponteiro mapeado vem da ligação atual, lida sob a mesma trava das ligações, e o
  mapeamento só é desfeito depois que nenhuma aplicação o usa.
- **Onde aplicar:** hoje a aplicação roda na thread de desenho, que é dona de
  `gpu_modified_ranges` e da guarda do heap. A primeira versão continua lá (só some a cópia). Tirar
  esse salto de thread é outra etapa, com outro risco, e não faz parte deste plano.
- **Coerência:** a memória é `HOST_COHERENT`; depois da espera no semáforo da GPU não precisa de
  `vkInvalidateMappedMemoryRanges`, mas a etapa 0 confere com uma comparação byte a byte contra o
  caminho de hoje (`SOTC_RB_DIRECT_CHECK=1`).
- **Leituras de escrita** (`is_write`): mesma via; `MarkWrittenByGuest` continua igual.

## 5. Restrições

- A guarda dos heaps do BPE (`ApplyDownloads`, `BpeHeapGuard`), o `POOL_FIX` e a regra do pool não
  mudam de comportamento: toda aplicação continua passando por `ApplyDownloads`.
- Windows intocado: o caminho novo só existe quando a memória da arena é `HOST_VISIBLE`, e liga
  sozinho só no Apple (`__APPLE__`). Nas placas dedicadas a memória `DEVICE_LOCAL` não é visível
  para a CPU (a não ser com ReBAR, fora deste plano).
- Nenhum código no KosmicKrisp/Mesa: tudo no núcleo de vídeo vendorizado (`buffer_cache`), com
  comentários `macos:`.
- Cada etapa é medida antes da próxima, com A/B intercalado (o ruído entre rodadas é ±1 FPS).

## 6. O que fica fora

- Rastrear escritas da GPU em pedaços menores que a página, ou adiar a leitura nas faltas de
  escrita: no gameplay as leituras de verdade pesam mais que o falso compartilhamento.
- A trava da fila de jobs do jogo (~37% do tempo de execução dos workers mesmo com o spin): é do
  próprio jogo (`eboot+0x38700`), não do port.

## Estado

- 01/11: plano escrito (`de4558f`).
- 01/11: **etapa 0 feita** (`SOTC_RB_DIRECT_PROBE=1`: tick da última escrita da GPU por bloco de
  64 KiB, gravado em `AddGpuModified`; cada leitura que espera é classificada em
  `BeginAsyncReadback`). Resultado, por janela de 5 s:

  | cena | leituras | dados já prontos | envio anterior ainda rodando | command buffer aberto |
  |---|---|---|---|---|
  | Santuário, rodada 1 | ~4400–4570 | **77–79%** | 14–15% (~2,5 ticks antes) | 7–8% |
  | Santuário, rodada 2 | ~4740–4900 | **69–70%** | 22% (~2,8 ticks antes) | 8–9% |
  | abertura (60 FPS) | ~2200–2550 | **62–67%** | 22–29% (~2,4 ticks antes) | 8–12% |

  A conta é conservadora (bloco de 64 KiB: o tick do bloco é o da escrita mais nova nele). Ou
  seja, **7 em cada 10 leituras esperam uma cópia na fila atrás de todo o trabalho atual, quando
  os dados já estavam prontos.** Bem acima do limite de ~30% da seção 3: as etapas 1 e 2 seguem
  como planejado.
- 01/11, achado no caminho: o keep-alive da GPU com despachos de ~15 ms custava 7–16% nas cenas em
  que a GPU limita (abertura a 60 FPS: 720p 36,2 contra 43,3 FPS sem ele; 1080p 32,9 contra
  35,4). Com despachos de ~1,5 ms o custo some (46,8 contra 46,0 FPS) e o gameplay continua com a
  GPU a 1380 MHz (25–27 FPS).
