# ISE 26/1 - Trabalho Final: Jogo Digital na DE1-SoC

Este repositório contém o trabalho final da disciplina **Introdução aos Sistemas Embarcados**,
cujo objetivo é o desenvolvimento de um jogo digital rodando sobre a plataforma **DE1-SoC**
(ou emulado via **CPUlator**), utilizando periféricos de vídeo VGA, mostradores de 7 segmentos
e outros dispositivos de entrada/saída da placa.

## Autores

- Pedro Araujo Cordeiro Viana - matrícula: 202067452
- Bruno Suxberger Valadares Araújo - matrícula: 231012728

## Estado atual do projeto

- `pula_plat.c` — física de pulo/gravidade, colisão com plataformas, contagem de pontos e
  vidas (3), telas de início e game over, e um vilão-nave que patrulha o topo da tela
  atirando projéteis no Kirby (com mecânica de parry). O personagem já usa sprites (Kirby); as
  plataformas usam um sprite de gelo.
- `sprites/` — imagens de sprites baixadas e os headers `.h` já convertidos para uso no jogo.
- `tools/png2c.py` — script que converte um PNG (ou um recorte dele) em um array C RGB565,
  tratando cor(es) de fundo como transparência. Usado para gerar os arquivos em `sprites/*.h`.

O jogo é compatível tanto com o **CPUlator** quanto com a **DE1-SoC** real, alternando por meio
da flag de compilação `DE1_SOC` (ver seção "Como compilar").

## Sprites: como adicionar novos

As sprites usadas vêm de sprite sheets baixadas (ex: spriters-resource), que não têm canal
alpha — o fundo é uma cor sólida. O fluxo para extrair um frame:

1. Coloque a sprite sheet em `sprites/`.
2. Descubra a posição (x, y, largura, altura) do frame desejado dentro da sheet.
3. Gere o header C:
   ```bash
   python tools/png2c.py "sprites/sheet.png" --crop X,Y,W,H \
       --bg R,G,B [--bg R2,G2,B2 ...] --nome nome_do_sprite > sprites/nome_do_sprite_sprite.h
   ```
   `--bg` pode ser passado mais de uma vez se a sheet tiver mais de um tom de fundo (ex: cor de
   fundo da sheet e cor de destaque de célula).
4. Inclua o header gerado em `pula_plat.c` e desenhe com `desenha_sprite(x, y, sprite, W, H, TRANSPARENT)`.

Para fundos full-screen (telas de início/game over) com proporção diferente de 320x240, use
`--letterbox 320,240` em vez de `--resize`: ele redimensiona preservando a proporção original da
imagem e preenche as sobras com preto, em vez de esticar/distorcer.

Exemplo já pronto: `sprites/kirby_idle_sprite.h`, gerado a partir de
`sprites/NES - Kirby's Adventure - Playable Characters - Kirby.png` e usado como sprite do
personagem em `renderiza_cena()`.

## Como compilar

```bash
# Rodando localmente (Linux) ou como base para a DE1-SoC
gcc pula_plat.c -o jogo

# Rodando na DE1-SoC (acesso direto a /dev/mem)
gcc -DDE1_SOC pula_plat.c -o jogo
```

### Rodando no Windows via MSYS2 (SDL2)

O jogo acessa endereços de memória fixos (VGA, LEDs, etc.) que só existem dentro do
CPUlator ou da DE1-SoC real — nem compilar com `-DDE1_SOC` funciona fora de Linux
(`/dev/mem` não existe no Windows), e sem essa flag o programa tentaria ler/escrever
em endereços inválidos e travaria. Por isso existe o modo `SDL_SIM`, que troca a
VGA por uma janela SDL2 e os switches/botões pelo teclado, sem mudar a lógica do jogo.

1. Abra o terminal **"MSYS2 MinGW 64-bit"** (não o "MSYS2 MSYS" genérico — esse aqui
   gera binários nativos do Windows).
2. Instale o compilador e a SDL2:
   ```bash
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2
   ```
3. Compile:
   ```bash
   gcc -DSDL_SIM pula_plat.c -o jogo.exe -lSDL2 -lm
   ```
4. Rode (de dentro do mesmo terminal MinGW 64-bit, que já tem o `SDL2.dll` no PATH):
   ```bash
   ./jogo.exe
   ```
   Se quiser rodar `jogo.exe` fora desse terminal (ex: dando duplo-clique no Explorer),
   copie `C:\msys64\mingw64\bin\SDL2.dll` pra mesma pasta do `jogo.exe` antes.

Controles no modo SDL: setas ou WASD (esquerda/direita), espaço ou seta pra cima
(pular), shift (parry). Pontos e vidas aparecem no título da janela (sem display de
7 segmentos/LEDs de verdade).

### Rodando no CPUlator

O CPUlator (https://cpulator.01xz.net) é um simulador no navegador que só aceita **um único
arquivo `.c`**, sem suporte a múltiplos arquivos/pastas. Como o projeto agora usa
`#include "sprites/kirby_idle_sprite.h"`, é preciso gerar uma versão com tudo inlinado antes
de colar no CPUlator:

```bash
python tools/build_cpulator.py pula_plat.c > pula_plat_cpulator.c
```

Depois é só abrir `pula_plat_cpulator.c`, copiar todo o conteúdo e colar no editor do
CPUlator. Esse arquivo gerado não deve ser editado diretamente — qualquer mudança deve ser
feita em `pula_plat.c` e regerada com o script sempre que for testar no CPUlator.

## Como executar

```bash
# No CPUlator, basta rodar o binário normalmente
./jogo

# Na DE1-SoC real, é necessário rodar como root (acesso a /dev/mem)
sudo ./jogo
```

## Periféricos utilizados

| Periférico            | Uso no jogo                                              | Status |
|------------------------|-----------------------------------------------------------|--------|
| Vídeo VGA (obrigatório) | Renderização do cenário, plataformas e personagem         | ✅ |
| Displays de 7 segmentos (obrigatório) | Exibição da pontuação                       | ✅ |
| LEDs                    | Exibição do número de vidas restantes                     | ✅ |
| Chaves (SW)             | Chave 0 = pulo                                             | ✅ |
| Botões (KEY)            | Botões 0/1 = movimento horizontal, botão 2 = parry (reflete o projétil do vilão); também usados nas telas de início/game over | ✅ |
| Teclado USB / Mouse / Acelerômetro / PS2 Joystick | —                              | ⬜ não implementado ainda |

## Como jogar

- **Tela de início**: botão 0 inicia o jogo
- **Botão 0**: move o personagem para a direita
- **Botão 1**: move o personagem para a esquerda
- **Chave 0**: faz o personagem pular
- **Botão 2**: segura pra entrar em postura de parry — se o projétil do vilão atingir o Kirby
  nesse instante, ele é refletido de volta. Enquanto o parry está segurado, o Kirby para no
  lugar e os botões 0/1 escolhem a direção da devolução (reto, diagonal p/ direita ou p/
  esquerda) em vez de mover o personagem — uma seta vermelha acima do Kirby mostra a direção
  escolhida em tempo real
- Objetivo: subir pulando de plataforma em plataforma até alcançar o topo ("céu"), desviando
  (ou refletindo) os ataques do vilão-nave que patrulha o topo da tela
- A cada 10 plataformas geradas o céu muda de nível (até 8 níveis, depois repete o último)
- Toda plataforma derrete com o tempo: dura 9s desde que é gerada, ficando visivelmente menor a
  cada 3s (sprites de "derretendo" 1/2/3), até sumir de vez - se o Kirby demorar demais em cima
  de uma, ela some embaixo dele
- Levar um tiro custa uma vida e deixa o Kirby atordoado por 2s; cair da tela também custa uma
  vida; ao perder as 5 vidas, o jogo vai pra tela de game over (botão 1 tenta de novo, botão 0
  sai). As vidas do Kirby aparecem tanto nos LEDs quanto em pips vermelhos no canto inferior
  direito da tela
- O vilão tem 6 vidas, mostradas como ícones reduzidos do próprio sprite no topo da tela; ao
  perdê-las todas, ele explode, some pelo resto da partida e o Kirby comemora
- A pontuação vai até 1000 (exibida nos displays de 7 segmentos, usando os 4 dígitos); ao
  alcançar o topo, o Kirby também comemora com o sprite de campeão

## Laço principal do jogo

O jogo segue a estrutura clássica de laço de jogo, implementada na função `main`, com um
pequeno estado de tela por cima (início / jogando / game over):

```c
while (programa_rodando) {
    ler_entrada(&esquerda, &direita, &pular, &parry);
    switch (estado_jogo) {
    case ESTADO_INICIO:    renderiza_tela_inicio(); /* botão 0 -> ESTADO_JOGANDO */ break;
    case ESTADO_JOGANDO:   atualiza_estado(...); atualiza_vilao(); atualiza_projetil(parry);
                           renderiza_cena(...); break;
    case ESTADO_GAME_OVER: renderiza_tela_game_over(); /* botão 1 retry, botão 0 sai */ break;
    }
}
```

## Próximos passos

Itens previstos pelo enunciado do trabalho ainda pendentes nesta versão:

- Sprites para personagem e cenário (substituindo os retângulos atuais)
- Deslocamento horizontal estilo "Mário" com obstáculos/inimigos (a V1 atual é um protótipo
  vertical de "pular entre plataformas")
- Detecção de obstáculos nocivos e obstáculos destrutíveis (atirar/bater)
- Elementos opcionais para pontuação extra: inimigos ativos, gaps, plataformas suspensas,
  tesouros, múltiplos níveis, double buffering, interrupções, acelerômetro, joystick,
  segundo dispositivo de entrada, configuração de jogo
- Modelagem UML do jogo
- Documentação final com resumo da implementação e comentários por função

## Enunciado do trabalho

<details>
<summary>Clique para expandir as instruções completas da disciplina</summary>

### Jogos Digitais na DE1-SoC

#### Introdução

O trabalho final da disciplina de Introdução aos Sistemas Embarcados será o desenvolvimento de
um jogo na plataforma DE1-SoC.

Como regra geral, os jogos devem ser desenvolvidos em C ou C++ no ambiente Linux, e devem
utilizar alguns periféricos da placa. Os periféricos a serem considerados para os jogos são:

- vídeo VGA (obrigatório)
- mostradores de 7 segmentos (obrigatório)
- chaves
- botões
- teclado USB
- LEDS
- acelerômetro
- PS2 - joystick
- mouse USB

#### Característica do Jogo

- Jogo de plataforma, estilo Mário. Um personagem se desloca horizontalmente em um cenário
  bidimensional, encontrando no caminho elementos do jogo, como obstáculos, inimigos, etc.
  Sua movimentação deve incluir saltos para pular obstáculos ou buracos. Pode atirar ou bater
  nos inimigos, etc.
- O vídeo VGA é obrigatório
- É obrigatório apresentar alguma realimentação (pontuação, por exemplo) nos displays 7 seg.
- Outra realimentação desejada é apresentar o número de vidas nos LEDs.
- É necessário no mínimo mais um dispositivo de entrada: chaves, botões, teclado ou mouse
- Configuração do jogo (opcional): configurar parâmetros do jogo, como velocidade, nível de
  dificuldade, etc.

Laço principal: um jogo é tipicamente organizado em um laço com as seguintes ações:

```c
while (jogo_rodando) {
    lerEntrada();
    atualizarEstado(deltaTime);
    renderizarCena();
}
```

É um fluxo contínuo onde as entradas são lidas, o estado do jogo é atualizado e o resultado é
renderizado na tela VGA.

#### Critérios de avaliação

A avaliação será feita a partir de um valor de referência mais acréscimos opcionais, sendo o
total limitado a 10.

- O jogo mínimo (5,0 pts) deve incluir um personagem descrito com sprites, com simulação de
  movimento para um lado e para outro, saltos e obstáculos, com deteção de colisão. Alguma
  forma de destruir obstáculos passivos (bater, atirar). Obstáculos nocivos causam perda de
  vida ao jogador.
- Em acréscimo podemos ter:
  - inimigos ativos, que perseguem o jogador, eventualmente atirando nele
  - gaps, ou abismos que o jogador deve saltar
  - níveis de poder dos inimigos
  - plataformas suspensas
  - tesouros para adquirir poder, vida, etc
  - mais de um nível de jogo
  - quaisquer inovações criativas
- Modelagem UML (1 pt): deve-se fazer um modelo UML do jogo. Mesmo que o jogo seja
  implementado em C, sem orientação a objetos, uma classe pode ser implementada por dados e
  funções separadamente.
- Funcionamento correto (2 pts): execução sem falhas, implementação completa das funções
  especificadas para o jogo
- Desempenho e qualidade (2 pts): qualidade da interface - gráficos, realismo - e execução
  fluída
- Originalidade e desafios (1 pt): quaisquer diferenciais introduzidos na mecânica do jogo,
  forma de interação, etc.
- Documentação e organização (1 pt):
  - instruções claras para compilar, executar e jogar
  - fazer um resumo da implementação indicando como funciona o código - comentar cada função

Elementos adicionais de avaliação:

- Uso de double buffering: 1,0 pt
- Uso de interrupções: 2,0 pts
- Uso do acelerômetro: 2,0 pts
- Uso de comunicação em rede: 2,0 pts
- Uso de 2 ou mais dispositivos de entrada: 1,0 pts
- Uso de Joystick ou outro sensor conectado à placa: 2,0 pts
- Configuração do jogo: 1,0 pts

</details>
