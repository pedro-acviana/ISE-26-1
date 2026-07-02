# ISE 26/1 - Trabalho Final: Jogo Digital na DE1-SoC

Este repositório contém o trabalho final da disciplina **Introdução aos Sistemas Embarcados**,
cujo objetivo é o desenvolvimento de um jogo digital rodando sobre a plataforma **DE1-SoC**
(ou emulado via **CPUlator**), utilizando periféricos de vídeo VGA, mostradores de 7 segmentos
e outros dispositivos de entrada/saída da placa.

## Autores

- Pedro Araujo Cordeiro Viana - matrícula: 202067452
- Bruno Suxberger Valadares Araújo - matrícula: 231012728

## Estado atual do projeto

- `pula_plat.c` — **V1** do jogo, ainda sem sprites (personagem e plataformas são desenhados
  como retângulos coloridos). É a base da lógica do jogo: física de pulo/gravidade, colisão
  com plataformas, contagem de pontos e vidas.

O jogo é compatível tanto com o **CPUlator** quanto com a **DE1-SoC** real, alternando por meio
da flag de compilação `DE1_SOC` (ver seção "Como compilar").

## Como compilar

```bash
# Rodando no CPUlator
gcc pula_plat.c -o jogo

# Rodando na DE1-SoC (acesso direto a /dev/mem)
gcc -DDE1_SOC pula_plat.c -o jogo
```

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
| Chaves (SW)             | Comando de pulo                                            | ✅ |
| Botões (KEY)            | Movimento horizontal (esquerda/direita)                    | ✅ |
| Teclado USB / Mouse / Acelerômetro / PS2 Joystick | —                              | ⬜ não implementado ainda |

## Como jogar (V1)

- **Botão 0**: move o personagem para a direita
- **Botão 1**: move o personagem para a esquerda
- **Chave 0**: faz o personagem pular
- Objetivo: subir pulando de plataforma em plataforma até alcançar o topo ("céu")
- Cair da tela custa uma vida; ao perder todas as vidas, o jogo termina
- A pontuação é exibida nos displays de 7 segmentos e o número de vidas nos LEDs

## Laço principal do jogo

O jogo segue a estrutura clássica de laço de jogo, implementada na função `main`:

```c
while (jogo_rodando) {
    ler_entrada(&esquerda, &direita, &pular);
    atualiza_estado(esquerda, direita, pular);
    renderiza_cena();
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
