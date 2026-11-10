# dkr360 — estado e como continuar

Diddy Kong Racing para Xbox 360 (RGH/Aurora), port por **Matherzin**, sobre o
[Golden Balloon](https://github.com/akratch/goldenballoon) (port nativo de PC do akratch, MIT, em cima da decompilação
DavidSM64/Diddy-Kong-Racing). Não existe port de PS Vita do DKR que eu tenha achado; o Golden Balloon é o port de PC, e
**não usa libultraship**: tem o próprio renderizador fast3d em C (OpenGL/WebGPU), áudio próprio em software, entrada
SDL2, 60 fps por interpolação de apresentação, widescreen, tela dividida até 4 e lê tudo da ROM em tempo de execução
(não há extração de assets: o "dado do jogo" é só a ROM em `.z64`).

## Como montar

```bash
cd ~/dkr360-port
bash platform/xbox360/build.sh                                  # ~/dkr360-work/build/dkr360.xex
bash platform/xbox360/make-game-data.sh "<rom>.n64" <pasta>     # <pasta>/dkr.z64 (aceita .z64/.n64/.v64)
```

- `build.sh` clona o Golden Balloon no commit de `patches/base-commits.txt` (2b69e748), aplica
  `patches/goldenballoon-xsdk.patch` e compila com o CMake próprio de `platform/xbox360/project` (não roda o CMake do
  Golden Balloon). Árvore de trabalho já com o patch aplicado: `~/dkr360-work/goldenballoon`. Para mudar o patch: edite
  lá e rode `git -C ~/dkr360-work/goldenballoon diff > patches/goldenballoon-xsdk.patch`.
- O sysroot (`~/dkr360-work/sysroot`) hoje é um link para `~/starfox-work/sysroot` (só o SDL2 = sdl2-compat sobre
  SDL-xenon é usado). Sem ele, `deps/build-all.sh` (do Star Fox) monta um.
- Hook de pre-commit em `.githooks/` (`git config core.hooksPath .githooks`) recusa ROM, `.xex`, saves e arquivos > 4 MB.
- No console: `dkr360.xex` + `dkr.z64` na mesma pasta. Configurações e saves vão para `dkr360\` ao lado; log em
  `dkr360.log` (o da execução anterior fica em `dkr360-anterior.log`).

## ROM

A ROM do usuário é **USA 1.0** (us.v77, SHA-1 `0cb115d8716dbbc2922fda38e533b9fe63bb9670`; o arquivo `.n64` dele é na
verdade byte-swapped `.v64`). O Golden Balloon é compilado para 1.1 e recusa 1.0 por padrão, mas reaponta a tabela de
assets por revisão e documenta que 1.0 termina um Time Trial (docs/ROM_REVISIONS.md); `dkr360_main.c` liga
`MDKR_ROM_ANY_REVISION=1`. Diferença real achada: a tabela de Magic Codes da 1.0 tem 2 textos por código (1.1 tem 3);
o patch a reconstrói no layout da 1.1 (`gMdkrMagicCodesV77`, objects.c/menu.c). Outras diferenças 1.0×1.1 de código
(menu.c tem muitos `#if VERSION`) seguem o comportamento da 1.1.

## O que o patch muda (resumo)

- **Big-endian**: o `asset_swap.c` do GB já vira no-op em BE; mas havia trocas incondicionais no código do jogo
  (game_text.c, audio.c ×2, tracks.c BSP, objects.c misc words/halfwords/shield/objmap). Todas ficaram atrás de
  `#ifndef MDKR_HOST_BIG_ENDIAN` (definido no prelude).
- **Mapa de endereços do xsdk** (`dkr_resolve`, gfx_pc_dkr.c): o caminho ILP32 do GB supõe globais abaixo de 16 MB
  (wasm). No 360 a imagem fica em 0x82000000 (token K0 de global = 0x02xxxxxx, colide com segmentos) e o heap começa
  perto de 0. A imagem (`[0x82000000, xsdk_jit_end)`) é decidida antes da arena; importante porque no replay da
  interpolação a "arena" é a cópia retida, que mora no heap baixo.
- **Renderizador**: `platform/xbox360/src/gfx_xenos.cpp` implementa o `GfxRenderingAPI` (C) do GB com o backend Xenos
  dos outros ports (combinador por constantes, 256 variantes do pixel shader, pools de memória GPU). Um alvo só (a tela),
  limpo de preto a cada quadro, resolve no front buffer no fim do quadro. Clip no near plane é feito pela CPU no GB;
  far clamp por software ligado no 360. `platform_sdl_min.c`: sem janela/GL no xsdk (`dkr360_gl_compat.h`), SDL só com
  GAMECONTROLLER|EVENTS.
- **Interpolação (60 fps)**: o GB copia a arena de 16 MB a cada quadro do jogo; no 360 isso seria ~9 ms. Agora copia só
  até o fim dos blocos vivos do pool principal (`mdkr_mempool_main_used_bytes`, ~3,5 MB) e loga a média.
- **4:3**: barras pretas (o GB pintava as margens com a cor de limpeza da pista).
- **Controles**: SDL (como no GB) e, se o SDL não abrir um controle que o XAM vê, leitura direta do XAM com os mesmos
  mapeamentos (fallback em platform_sdl_min.c). Back é do menu do console; enquanto ele está aberto o jogo não recebe
  entrada.
- **Áudio**: thread do callback movida para o hardware thread 4; jogo no 2 (16 MB de pilha). Pico de áudio no log.
- Stubs: harfbuzz (nomes de personagens custom), flock/fstatat/rmdir/posix_spawn, lstat/utime (prelude).

## Menu do console (`dkr360_menu.cpp`)

Em inglês (pedido do usuário), estilo do menu do Star Fox via Dear ImGui (a do GB, 1.92, com texturas Alpha8).
Back abre/fecha; D-pad/analógico, A, B, LB/RB. Abas: General (FPS counter, Rumble, Restore defaults, Quit), Video
(16:9/4:3, 60/30 fps, HUD edges/4:3, FOV, draw distance, speedometer), Audio (master/music/effects), Game (opponent
skill, camera avoid walls, Adventure Party 2–4 jogadores, bonus racers), Credits ("Xbox 360 port: Matherzin" e no
cabeçalho "Port by Matherzin"). As opções são as do próprio GB (`mdkr_video_config_runtime_set`, salvas em
`dkr360\video.ini`); só o contador de FPS fica em `dkr360\console.ini`. Primeira execução: 60 fps, HUD nas bordas,
master 85% (a mix satura a 100%). Cuidado: variáveis `MDKR_*` de ambiente **travam** a opção correspondente (por isso
`MDKR_VIDEO_MODE` não é mais setado). O idioma do jogo fica no Options do próprio jogo.

## Testado no Xenia (Canary, sem display — ver tools/xenia/README.md)

- Boot, intro, título, menus, Time Trial e corrida em Ancient Lake com piloto automático: imagem correta por captura
  (widescreen com mais campo de visão nas laterais, HUD nas bordas, textos), 60 fps estáveis, sem `[DL]`/crash.
- Tela dividida 4 jogadores (`race_4p_split.txt`): 60 fps no Xenia.
- 4:3 com barras pretas, 30 fps original.
- Áudio: dump analisado (sinal coerente em BE, pico ~19000 com master 85%).
- Saves: `dkr360\saves\eeprom.bin` e autosaves gravados.
- Menu: renderizado aberto por captura (`dkr360_menu_open.txt`).

## Falta testar no console (nada disso rodou em hardware)

1. Se abre e mostra imagem (o display só abre no console).
2. **Controles** (o Xenia não tinha controle): SDL-xenon + fallback XAM; o log mostra
   `controllers on XAM ports 1--- , SDL sees N`. Testar 2–4 controles na tela dividida.
3. **Desempenho**: jogo + render HLE num thread só; 60 fps com interpolação desenha cada lista duas vezes. Ver
   `alive: ... fps` no log e `replay copies of the arena`. Se não segurar, o menu tem 30 fps; próximo passo seria uma
   thread de render (como no Star Fox/OoT) ou VMX no transform de vértices do gfx_pc_dkr.
4. Som no hardware (buffer de 256 amostras a 22050 Hz pode ser pequeno para o SDL-xenon; aumentar
   `DKR_AUDIO_DEVICE_PERIOD` se estalar).
5. Rumble, saves no HDD/USB, sair pelo menu.

## Teste 2 (depois do primeiro teste no console)

O usuário confirmou no console: roda bem, sem problemas visuais, mas o FPS cai até 20 em certos momentos.

**Desempenho** (perfil em `dkr360_prof.c`, linha `prof:` a cada 5 s; slots aninhados explicados no `.h`). No Xenia,
o walk do HLE (+ o replay da interpolação, outro walk inteiro) era ~380 ms/s na cena mais pesada; G_TRIN dominava.
Mudanças (todas `__XSDK__`):
- cache de texturas: tabela de dicas na frente da busca linear de 1024 entradas (bind 52→15 ms/s);
- combinador: último lookup memorizado e features decodificadas só quando os ids mudam;
- `DKR360_ORIGINAL`: o walk real não procura mais nas dependências retidas (só o replay lê cópias);
- triângulos agrupados entre G_TRINs (o GB dava flush no fim de cada um); flush antes de troca de textura/sampler e
  em todo miss do cache (`dkr_bind_tile`); o resto do estado já fazia flush ao mudar;
- caminho rápido sem cópias de `LoadedVertex` para triângulo sem clipping;
- `c / 255.0f` por tabela (int→float é load-hit-store no Xenon, invisível no Xenia);
- posição no mundo e normal suave por vértice só com Remaster FX;
- backend: vértices para a GPU com o `memcpy` VMX do xsdk; `-O3`; **3 front buffers** (antes 2: pacer do GB +
  espera de vblank podiam somar e virar 33/50 ms; suspeita forte para o "cai até 20", que o Xenia sem display não
  mostra). Log novo `xenos: display ... missed ... longest buffer wait`.
Resultado no Xenia: ~380→~260 ms/s na cena pesada, imagem idêntica (1P e 4P). **Falta o log do console** para ver o
efeito real e o que sobra. Próximo passo grande se precisar: thread de render (o N64 já roda RSP em paralelo à CPU e o
DKR usa buffers duplos para isso; o desafio são os registros que o código do jogo faz durante a montagem da lista e a
máquina de replay da interpolação).

**Idiomas**: menu em EN/PT/ES (`L(en, pt, es)`; as abas são reconstruídas ao trocar). Geral > Idioma (primeiro item:
Automático/English/Português (Brasil)/Español, salvo em `dkr360\console.ini` `Language=`). O cartucho US só tem
inglês/francês; PT/ES vêm de ROMs traduzidas `dkr_pt.z64`/`dkr_es.z64` ao lado do `.xex`
(`make-game-data.sh <rom> <pasta> pt|es`). `dkr360_main.c` escolhe a ROM no boot; "Textos do jogo" mostra a ROM
carregada e A reinicia (`xsdk_launch` do próprio `.xex`). Testado no Xenia: escolha da ROM e saída com
`launching game:\dkr360.xex` (o Xenia não reabre; reinício só confirmável no console). **As ROMs traduzidas ainda não
foram testadas** (o usuário ia mandar): conferir se o Golden Balloon aceita (CRC diferente → aviso, não recusa;
tamanho/LUT podem mudar se a tradução expandiu a ROM).

Ganchos de teste novos: `dkr360_test_restart.txt` (reinicia no quadro 300).


## Teste 3

Logs do console (teste 2) leram: o jogo+render fica a ~95% de uma CPU nas cenas pesadas (walk ~300 ms/s, replay ~260,
endframe ~130, audio ~85-100, game ~170); `missed` vblanks 100+/600 nas cenas pesadas; presente/buffers sem espera.
O endframe era em boa parte a cópia da arena (agora medida: slot `copy`), audio ~4x mais lento que no Xenia.
Mudanças (todas `__XSDK__`): `dkr360_emit_core` (gfx_pc_dkr.c): transformada de UV por triângulo (sem divisões por
vértice), entradas de cor constantes lidas uma vez, escrita no VBO por ponteiro local (o contador global recarregava a
cada float), sem contadores com divisão; caminho sem cópia de `LoadedVertex` para triângulos sem clipping, com sinal
da área por determinante (sem 6 divisões); `-ffp-contract=fast` só no gfx_pc_dkr.c; menu não roda ImGui fechado.
Xenia: poly 100-125 -> 55-80 ms/s, menu 10 -> 1. Ainda pendente: thread de audio (~10%), cópia da arena com
dcbz (~12 ms/s), render thread (grande, ver nota do teste 2).

Idioma: browse (preview) + A confirma + pergunta de reiniciar; B sem confirmar volta. ROM traduzida é "modificada":
`dkr360_main.c` liga `MDKR_ROM_ALLOW_MODIFIED=1` só quando carrega dkr_pt/dkr_es. ROM es do usuário: Rev 1 +
T-Spa carlosxpx2 1.0 (sha1 9655d78a...); parece traduzir só diálogos (sem OPCIONES/AVENTURA no binário).
Gancho de teste: `dkr360_menu_open.txt` = "aba [idioma [1]]" (idioma 0-3, 1 = apertar A).

## Próximos passos sugeridos

- Thread de render separada se o console ficar abaixo de 60.
- Mapear Back+Start para sair, como nos outros ports.
- Testar Adventure (hub, Taj, chefes) e todas as pistas; a ROM 1.0 só foi testada no caminho Time Trial/Tracks.


## Teste 4 — português (PT-BR) funcionando

ROM PT-BR = a tradução Dream64 "V1.0 Rev 7" aplicada sobre a USA 1.0 (sha1 do resultado `ec14eece...`, 14,3 MB, cabeçalho
"DKR BR V1.0 - Rev7"). `tools/vcdiff_apply.py <rom 1.0.z64> <patch.xdelta> <saida>` aplica o xdelta sem precisar do
xdelta3 (confere o adler32 de cada janela). O usuário já tem o resultado em `E:\Roms - Isos\Nintendo 64\Diddy Kong Racing (PT-BR).z64`.
`make-game-data.sh <rom pt> <pasta> pt` agora completa com 0xFF até 16 MB.

Por que o espanhol só traduzia parte e o português traduz tudo: o Golden Balloon roda o **código C da decompilação**, não o
código da ROM; só os *assets* (textos de diálogo/menu, fontes, texturas) vêm da ROM. A ROM ES (Rev 1 + carlosxpx2) só mexe
nos diálogos. A PT-BR mexe também nas texturas de menu, na fonte (acentos) e no asset de nomes de pistas.

O que o patch do GB ganhou para a PT-BR (todas as mudanças são só para ROM traduzida/estendida):
- `rom_validation.[ch]`, `rom_io.c`: aceitam 12 MB **ou 16 MB**; imagem de 16 MB tem `romEnd = tamanho` (a tabela de assets da
  PT-BR cresce: texturas 3D/2D e fontes; a última entrada da LUT aponta além do arquivo).
- `game.c`: a tabela de nomes de pistas da PT-BR só tem 1 idioma por pista e entradas finais velhas (2785, 3387 > 1152):
  as entradas são limitadas ao tamanho da seção e blocos vazios reaproveitam o nome anterior.
- `rom_io.c`: `gMdkrPtBr` (título "DKR BR"), `mdkr_pt_text()`: a ROM PT-BR troca por código textos que o C do jogo tem
  literais (créditos, 1º..8º, "NÃO PODE SALVAR FANTASMA", CORRIDA/VOLTA...). Aplicada em `render_text_string` e
  `get_text_width` (font.c). Os acentos são a codificação própria da tradução (`$`=ç, `@`+letra = til, `)` `(` `<` `&` `*`).
- Verificado no Xenia: título (INICIAR JOGO / OPÇÕES), "ESCOLHA O JOGADOR", aviso do Rumble Pak, seleção de pista/veículo
  (LAGO ANTIGO, MEL.TEMPO, CARRO/HOVER/AVIÃO), HUD (VOLTA 1/3, TEMPO). Não verificado: créditos, tela de resultados
  (1º..), "NÃO PODE SALVAR FANTASMA", diálogos (Taj, Tricky...), Adventure inteiro, textos "VIDEO OPTIONS" do próprio GB.
- Não portado: o scroller "EQUIPE DREAM 64" e a tela "Intro64" (rodam no código da ROM, não existem no GB).

Teste: `tools/runtest.ps1 -Seconds 130` copia o .xex do WSL para `xenia-teste`, roda o Xenia e mostra o log;
`tools/ppm2png.py xenia-teste work/shots` converte as capturas.

### Desempenho (log do console, ROM ES, teste 3)
Cena pesada (pista, 5 s): walk 100–140 ms/s, replay 126–162, copy da arena 70–100, audio 65–90 (thread 4), jogo ~80–160;
60 fps na maior parte, mas 37–39 vblanks perdidos a cada 600 na pior cena e travadas de 145–660 ms (carregamento de pista).
Draw distance já está em 100% (original). Nada foi mudado no desempenho no teste 4. O ganho que falta é o grande:
**thread de render** — `sched_dispatch_task` (stubs_dkr.c) roda gfx_start_frame/gfx_run/gfx_end_frame inline na thread do
jogo; no N64 o RSP corria em paralelo e o DKR já usa lista/vértices/matrizes duplicados. Plano: copiar a arena para o
replay no momento do envio (thread do jogo), mandar o walk + replays + present para uma thread no hardware thread 1/3 e
sinalizar `s_gfxDone` ao fim; cuidar de todo estado global do renderizador (só a thread de render pode tocá-lo) e do pacer.


## Teste 5 — cópia da arena fora do caminho crítico

Análise do pacer (stubs_dkr.c, `osRecvMesg` da fila de vídeo): por tick de 30 Hz a thread do jogo faz lógica -> walk
autoritativo -> cópia da arena -> present do endpoint (que precisa sair no vblank do tick) e só depois, na outra metade,
o replay a alfa 1/2 e seu present. O vblank perdido é o tick cuja soma lógica+walk+cópia passa de ~16,7 ms; o replay e a
espera ficam folgados. Por isso uma thread de render completa **não** foi feita: o replay (câmeras interpoladas, task
retida, `present_sched_*`) e o pacer usam estado do jogo/renderizador de forma entrelaçada e a lógica do próximo tick só
começa no vencimento do tick; tirar o walk da thread do jogo só ajuda se o present/replay inteiro migrar junto, o que muda
o pacer de ~7000 linhas sem console para validar. Ficou o que dá para provar:
- `gfx_retained_task.c`: a cópia da arena para o replay vai para uma thread auxiliar (hardware thread 1) disparada em
  `capture_begin` (início do walk, que só lê a arena) e junta em `capture_commit`; se o pool cresceu durante o walk, o
  resto copia inline. Slot `copy` e `endframe` no log caem a ~0 na thread do jogo.
- Xenia (1P, mesma rota/autopilot, quadros 5000/6000/7000 byte a byte idênticos ao teste 4): `endframe` 15–19 -> 1,5–1,9 ms/s,
  `copy` 13–18 -> 0,0; walk/replay iguais. 4P tela dividida roda estável, imagem correta. No console a cópia custava
  70–100 ms/s (log do teste 3), ou seja ~3 ms a menos por tick no caminho do endpoint.
- Falta medir no console (`prof:` e `missed` do dkr360.log). Se ainda perder vblanks: próximo passo é encurtar o walk
  (VMX no G_VTX/G_TRIN) ou mover o present/replay inteiro + o pacer para uma thread (tarefa grande).
- `tools/runtest.ps1 -Xex <caminho> -Filter "prof:"` roda o Xenia com outro .xex e filtra o log.


## Teste 6 — endpoint lead no pacer

Log do console (teste 5, hub do Adventure, level 0): `copy` 0 e `endframe` ~10 (a cópia em thread auxiliar funcionou), mas
walk 200–270 ms/s, replay 200–285, jogo 130–220; janelas com 20–30 (até 86) vblanks perdidos em 600 (~57 fps); corridas
normais 0 perdidos. Janela de compute do tick = 1 vblank (16,7 ms). `platform_sdl_min.c`: o "endpoint lead" do WebGPU agora
existe também no xsdk (`s_xsdkEndpointSlotNs`): o wake que torna o tick devido roda `lead` (padrão 7 ms,
`MDKR_PRESENT_ENDPOINT_LEAD_US` / arquivo `dkr360_lead.txt`, 0–12000) antes, o relógio ganha o crédito (`now += lead`) e
`platform_present_endpoint_gate` segura a imagem até o slot. Log `lead:` a cada 300 ticks mostra quantos estouraram o lead.
Xenia: 60 fps, quadro de corrida igual (tempo de corrida ±0,1 s). Não medido no console.
Se o hub ainda perder vblanks: diminuir walk/replay (VMX no G_VTX/G_TRIN; replay compartilhando cálculo do walk).


## Teste 7 — lead 11 ms

Log do teste 6 (hub e pistas): `lead:` mostra 300/300 ticks estourando 7 ms, média de estouro 6–8 ms nas janelas boas (0
vblanks perdidos) e 13–15 ms nas ruins (22–38 perdidos): o compute do tick no hub é ~20 ms. Lead padrão subiu para 11 ms
(máx. 14, `dkr360_lead.txt`). Janelas com `worst` de 46–590 ms são carregamento de pista, não do pacer. Perdidos no log do
teste 6: 755/46376 vblanks (1,6%, em sessão com mais pistas pesadas que a do teste 5, 0,8%). Medir o teste 7 igual.


## Teste 8 — buscas lineares viraram hash

Log do teste 7 (console): nível 28 e hub com walk 370 + replay 290 ms/s e jogo 220 (pace ~20: CPU saturada, 52 fps); ~1,3 mil
ciclos por triângulo. Achado no código: `gfx_shadow_matrix_lookup`/registro (gfx_shadow_frame.c, entradas de ~300 B,
uma busca linear por G_MTX de cada walk/replay e uma por registro de matriz por quadro = O(N²)) e as listas do
`gfx_presentation_packet.c` (`list_lookup`, `lookup_live_vertex`, `list_register`, `has_*_vertex`) varriam tudo por G_VTX.
Agora índice hash por ponteiro (chaves únicas), reconstruído quando a lista é zerada/restaurada (`s_midx_dirty`,
`LIST_CLEAR`). Xenia, hub: µs por walk 4190 -> 3369 (-20%), jogo 65 -> 55 ms/s, imagem do hub igual (inspeção visual; o hub
não é determinístico entre execuções). Não resolvido: `scan` (varredura da DL do tick, ~100 ms/s no console), busca linear em
`uv_scroll_find`/`deformation_find` (pequenas), carregamento de pista (travadas de 0,5 s), custo por triângulo.
Teste de hub no Xenia: autopad = `tests/input_scripts/adventure_hub_drive.txt` sem comentários; ele grava save em
`dkr360\saves` e a segunda execução trava no menu se o save existir (mover a pasta antes). `tools/profsum.py <log> N`.
