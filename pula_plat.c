/*
 * Pula Plataformas - jogo de subir pulando entre plataformas até o "céu"
 * Funciona tanto no CPUlator quanto na DE1-SoC real.
 *
 * COMO COMPILAR (precisa de -lm por causa do sqrt usado no gerador de
 * plataformas):
 *   CPUlator:  gcc pula_plataformas.c -o jogo -lm
 *   DE1-SoC:   gcc -DDE1_SOC pula_plataformas.c -o jogo -lm
 *
 * COMO RODAR (na placa, precisa ser root):
 *   sudo ./jogo
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#ifdef DE1_SOC
#include <fcntl.h>
#include <sys/mman.h>
int fd;
#endif

#include "sprites/kirby_idle_sprite.h"
#include "sprites/kirby_neutro_sprite.h"
#include "sprites/kirby_caindo_reto_sprite.h"
#include "sprites/kirby_caindo_diagonal_sprite.h"
#include "sprites/ceu_nuvens_sprite.h"
#include "sprites/plataforma_gelo_sprite.h"

/* ---------------- Endereços dos periféricos ---------------- */
#define HW_REGS_BASE   0xFF200000
#define HW_REGS_SPAN   0x00200000
#define LED_OFFSET     0x00
#define HEX_LOW_OFFSET 0x20   /* HEX3-HEX0 */
#define SWITCH_OFFSET  0x40
#define BUTTON_OFFSET  0x50

#define VGA_BASE 0xC8000000
#define WIDTH    320
#define LWIDTH   512   /* stride da memória de vídeo */
#define HEIGHT   240

/* Cores RGB565 */
#define BLACK  0x0000
#define WHITE  0xFFFF
#define BLUE   0x001F
#define YELLOW 0xFFE0
#define RED    0xF800

/* ---------------- Ponteiros globais ---------------- */
volatile void     *peripherals;
volatile uint32_t *led_ptr;
volatile uint32_t *hex_ptr;
volatile uint32_t *switch_ptr;
volatile uint32_t *button_ptr;
uint16_t (*tela)[LWIDTH];   /* memória de vídeo real (VGA) */

/* Double buffering: tudo é desenhado nesse buffer comum (RAM), e só no fim
 * do frame ele é copiado de uma vez para a memória de vídeo (atualiza_tela).
 * Sem isso, cada plot_pixel apareceria na tela na hora, e dava pra ver o
 * fundo/plataformas/personagem sendo desenhados em sequência (flicker). */
uint16_t back_buffer[HEIGHT][WIDTH];

/* ---------------- Física do pulo ---------------- */
/* valores em ponto fixo 24.8 (1 pixel = 256), por frame */
#define GRAVIDADE 400   /* equivale a ~1.6px/frame^2 (pulo 2x mais rápido, mesma altura) */
#define VEL_PULO  (-3072) /* equivale a ~-12px/frame (2x mais rápido, mesma altura) */
#define VEL_LAT   512   /* equivale a ~2px/frame */

/* ---------------- Câmera ---------------- */
/* camera_y é a coordenada de mundo (em pixels) que aparece no topo da tela.
 * Como o jogador só sobe (y de mundo diminui), a câmera nunca desce: ela só
 * é reajustada quando o jogador cruza CAMERA_LIMIAR, empurrando o "mundo"
 * (plataformas e fundo) para baixo na tela e dando a sensação de escalada
 * contínua. Posição de tela = posição de mundo - camera_y. */
#define CAMERA_LIMIAR (HEIGHT / 3)
int camera_y = 0;

/* ---------------- Estruturas do jogo ---------------- */
#define N_PLAT 8
#define CHAO_Y 220   /* altura (mundo) da plataforma inicial, usada de referência p/ pontuação */

typedef struct {
    int x, y, w, h;
} Plataforma;

typedef struct {
    int x, y;      /* posição do personagem */
    int vy;        /* velocidade vertical */
    int   w, h;
    int   vidas;
    int   pontos;
} Player;

Plataforma plataformas[N_PLAT];
Player p;
int jogo_rodando = 1;
int direcao_h = 1;   /* 1 = olhando p/ direita, -1 = p/ esquerda (última direção andada) */

/* ---------------- Inicialização de hardware ---------------- */
void init_io(void)
{
#ifdef DE1_SOC
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) { printf("Erro ao abrir /dev/mem\n"); exit(1); }

    peripherals = mmap(NULL, HW_REGS_SPAN, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, HW_REGS_BASE);
    if (peripherals == MAP_FAILED) { printf("Erro no mmap\n"); exit(1); }

    /* memória de vídeo mapeada à parte */
    void *vga_mem = mmap(NULL, WIDTH * HEIGHT * 4, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, VGA_BASE);
    if (vga_mem == MAP_FAILED) { printf("Erro no mmap VGA\n"); exit(1); }
    tela = (uint16_t (*)[LWIDTH]) vga_mem;
#else
    /* No CPUlator os endereços já são acessíveis diretamente */
    peripherals = (void *)HW_REGS_BASE;
    tela = (uint16_t (*)[LWIDTH]) VGA_BASE;
#endif

    led_ptr    = (uint32_t *)(peripherals + LED_OFFSET);
    hex_ptr    = (uint32_t *)(peripherals + HEX_LOW_OFFSET);
    switch_ptr = (uint32_t *)(peripherals + SWITCH_OFFSET);
    button_ptr = (uint32_t *)(peripherals + BUTTON_OFFSET);
}

/* ---------------- Funções de desenho ---------------- */
void plot_pixel(int x, int y, uint16_t cor)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    back_buffer[y][x] = cor;
}

void limpa_tela(void)
{
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            back_buffer[y][x] = BLACK;
}

/* Copia o back_buffer inteiro para a memória de vídeo de uma vez só. É o
 * único lugar que escreve em `tela` - garante que a tela sempre mostra um
 * frame completo e pronto, nunca um desenho pela metade. */
void atualiza_tela(void)
{
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            tela[y][x] = back_buffer[y][x];
}

void desenha_retangulo(int x, int y, int w, int h, uint16_t cor)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            plot_pixel(x + i, y + j, cor);
}

/* Desenha um sprite (array w x h de cores RGB565), pulando os pixels que
 * combinam com a cor-chave de transparência. Se espelhado != 0, desenha
 * invertido horizontalmente (usado pra reaproveitar o mesmo sprite virado
 * pra direita também virado pra esquerda, sem precisar de arte duplicada). */
void desenha_sprite(int x, int y, const uint16_t *sprite, int w, int h,
                     uint16_t transparente, int espelhado)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int sx = espelhado ? (w - 1 - i) : i;
            uint16_t cor = sprite[j * w + sx];
            if (cor != transparente)
                plot_pixel(x + i, y + j, cor);
        }
}

/* Desenha o fundo "infinito": um único sprite (céu + nuvens já combinados
 * numa imagem só) repetido em grade por toda a tela, sem transparência - a
 * imagem cobre o tile inteiro, então não sobra nenhum buraco entre eles.
 * A fase vertical acompanha camera_y, então o padrão rola junto conforme o
 * jogador sobe, em vez de ficar "grudado" na tela. */
void desenha_fundo(void)
{
    int fase = camera_y % CEU_NUVENS_H;
    if (fase < 0) fase += CEU_NUVENS_H;

    for (int y = -CEU_NUVENS_H + fase; y < HEIGHT; y += CEU_NUVENS_H)
        for (int x = -CEU_NUVENS_W; x < WIDTH; x += CEU_NUVENS_W)
            desenha_sprite(x, y, (const uint16_t *)ceu_nuvens_sprite,
                           CEU_NUVENS_W, CEU_NUVENS_H, CEU_NUVENS_TRANSPARENT, 0);
}

/* ---------------- Tabela de dígitos p/ display 7 seg ---------------- */
const uint8_t seg[10] = {
    0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F
};

void atualiza_display(int pontos)
{
    int dez = (pontos / 10) % 10;
    int uni = pontos % 10;
    *hex_ptr = seg[dez] << 8 | seg[uni];
}

void atualiza_leds(int vidas)
{
    *led_ptr = (1 << vidas) - 1;  /* acende N leds = N vidas */
}

/* ---------------- Lógica do jogo ---------------- */

/* A colisão só é testada quando o jogador está caindo (p.vy > 0, ver
 * atualiza_estado), ou seja ele sempre pousa numa plataforma na DESCIDA do
 * pulo, depois de passar pelo ápice. Dado um desnível vertical dy_px (em
 * pixels; positivo = plataforma mais alta que a atual), esta função calcula
 * quantos pixels horizontais o jogador percorre até cruzar essa altura na
 * descida - ou seja, o alcance horizontal máximo alcançável nesse pulo.
 * Usada para gerar plataformas a distâncias compatíveis com a física do
 * pulo (ver GRAVIDADE/VEL_PULO/VEL_LAT). */
int alcance_horizontal(int dy_px)
{
    double v0 = -VEL_PULO;                       /* velocidade inicial do pulo (positiva) */
    double alvo = -(double)dy_px * 256.0;         /* desnível em ponto fixo (y cresce p/ baixo) */
    double delta = v0 * v0 + 2.0 * GRAVIDADE * alvo;

    if (delta < 0) return 0;   /* mais alto do que o pulo consegue alcançar */

    double t = (v0 + sqrt(delta)) / GRAVIDADE;    /* frames até cruzar a altura na descida */
    double alcance = (VEL_LAT / 256.0) * t;

    return (int)(alcance * 0.8);   /* margem de segurança */
}

#define N_PLAT_ALTURA_MIN 15  /* px - deve ficar bem abaixo do pulo máximo (~46px) */
#define N_PLAT_ALTURA_MAX 40
#define N_PLAT_LARGURA    40
#define N_PLAT_MARGEM     10  /* px de margem lateral da tela */

/* Sorteia a próxima plataforma a partir da "distância entre pontas": a
 * distância horizontal entre a borda da plataforma anterior (na direção do
 * salto) e a borda da nova plataforma mais próxima dela.
 *
 *  - Mínimo (mais fácil): sobreposição até a metade da plataforma anterior,
 *    ou seja, a ponta da nova plataforma alinhada com o centro da de baixo
 *    (gap = -largura_anterior/2, um valor negativo = sobreposição).
 *  - Máximo (mais difícil): quase o alcance físico do pulo calculado por
 *    alcance_horizontal(dy), que já embute uma margem de segurança.
 *
 * A direção (esquerda/direita) também é sorteada.
 */
void gera_plataforma_seguinte(Plataforma *anterior, Plataforma *nova, int dy)
{
    int dir = (rand() % 2) ? 1 : -1;

    int gap_min = -(anterior->w / 2);
    int gap_max = alcance_horizontal(dy);
    if (gap_max < gap_min) gap_max = gap_min;

    int gap = gap_min + rand() % (gap_max - gap_min + 1);

    int novo_x;
    if (dir > 0)
        novo_x = anterior->x + anterior->w + gap;
    else
        novo_x = anterior->x - gap - N_PLAT_LARGURA;

    if (novo_x < N_PLAT_MARGEM)
        novo_x = N_PLAT_MARGEM;
    if (novo_x > WIDTH - N_PLAT_LARGURA - N_PLAT_MARGEM)
        novo_x = WIDTH - N_PLAT_LARGURA - N_PLAT_MARGEM;

    *nova = (Plataforma){novo_x, anterior->y - dy, N_PLAT_LARGURA, 6};
}

int sorteia_dy(void)
{
    int faixa = N_PLAT_ALTURA_MAX - N_PLAT_ALTURA_MIN + 1;
    return N_PLAT_ALTURA_MIN + rand() % faixa;
}

void gera_plataformas(void)
{
    plataformas[0] = (Plataforma){140, CHAO_Y, N_PLAT_LARGURA, 6};   /* chão */

    for (int i = 1; i < N_PLAT; i++)
        gera_plataforma_seguinte(&plataformas[i - 1], &plataformas[i], sorteia_dy());

    p.x = plataformas[0].x << 8;
    p.y = (plataformas[0].y - KIRBY_IDLE_H) << 8;
    p.vy = 0;
    p.w = KIRBY_IDLE_W; p.h = KIRBY_IDLE_H;
    p.vidas = 3;
    p.pontos = 0;
    camera_y = 0;
}

/* Retorna a plataforma mais alta (menor y de mundo) atualmente ativa, usada
 * como referência para encaixar a próxima plataforma gerada acima dela. */
Plataforma *plataforma_mais_alta(void)
{
    Plataforma *melhor = &plataformas[0];
    for (int i = 1; i < N_PLAT; i++)
        if (plataformas[i].y < melhor->y)
            melhor = &plataformas[i];
    return melhor;
}

/* Qualquer plataforma que já saiu de tela por baixo (rolou para fora da
 * câmera) é reaproveitada: vira uma plataforma nova, gerada acima da mais
 * alta que existir no momento. É assim que a escalada fica infinita sem
 * precisar de um array sem limite de plataformas. */
void recicla_plataformas(void)
{
    for (int i = 0; i < N_PLAT; i++) {
        if (plataformas[i].y - camera_y > HEIGHT + N_PLAT_ALTURA_MAX) {
            Plataforma *topo = plataforma_mais_alta();
            gera_plataforma_seguinte(topo, &plataformas[i], sorteia_dy());
        }
    }
}

void ler_entrada(int *esquerda, int *direita, int *pular)
{
    uint32_t sw = *switch_ptr;
    uint32_t bt = *button_ptr;
    *esquerda = bt & 0x2;    /* botão 1 */
    *direita  = bt & 0x1;    /* botão 0 */
    *pular    = sw & 0x1;    /* chave 0 */
}

void atualiza_estado(int esquerda, int direita, int pular)
{
    if (esquerda) p.x -= VEL_LAT;
    if (direita)  p.x += VEL_LAT;

    if (pular && p.vy == 0) p.vy = VEL_PULO;

    p.vy += GRAVIDADE;
    p.y  += p.vy;

    if (p.vy > 0) {
        for (int i = 0; i < N_PLAT; i++) {
            Plataforma *pl = &plataformas[i];
            int px = p.x >> 8, py = p.y >> 8;
            if (px + p.w > pl->x && px < pl->x + pl->w &&
                py + p.h >= pl->y && py + p.h <= pl->y + pl->h + 4) {
                p.y = (pl->y - p.h) << 8;
                p.vy = 0;
            }
        }
    }

    /* pontuação = altura recorde alcançada (em mundo), 10px por ponto */
    int altura = CHAO_Y - (p.y >> 8);
    if (altura / 10 > p.pontos)
        p.pontos = altura / 10;

    /* câmera só sobe: se o jogador passou do limiar perto do topo da tela,
     * empurra a câmera pra cima, o que visualmente empurra o mundo (fundo e
     * plataformas) pra baixo - sensação de escalada contínua. */
    int py_tela = (p.y >> 8) - camera_y;
    if (py_tela < CAMERA_LIMIAR)
        camera_y = (p.y >> 8) - CAMERA_LIMIAR;

    recicla_plataformas();

    if ((p.y >> 8) - camera_y > HEIGHT) {
        p.vidas--;
        atualiza_leds(p.vidas);
        if (p.vidas <= 0) {
            printf("Fim de jogo. Pontos: %d\n", p.pontos);
            jogo_rodando = 0;
        } else {
            /* reaparece bem no limiar da câmera, não no topo, senão o
             * reajuste de câmera do próximo frame o empurraria de novo */
            p.x = (WIDTH / 2 - p.w / 2) << 8;
            p.y = (camera_y + CAMERA_LIMIAR) << 8;
            p.vy = 0;
        }
    }

    if (p.x < 0) p.x = 0;
    if ((p.x >> 8) > WIDTH - p.w) p.x = (WIDTH - p.w) << 8;
}
/* Escolhe o sprite do Kirby de acordo com o estado do frame ANTERIOR à
 * atualização de física (parado_antes vem de antes de atualiza_estado
 * rodar) e o vy já atualizado (subindo/caindo). Direção esquerda/direita só
 * espelha o mesmo desenho, não precisa de arte separada por lado:
 *
 *  - parado                        -> kirby_neutro
 *  - subindo (reto ou na diagonal) -> kirby_idle (o sprite original)
 *  - caindo reto (sem direção)     -> kirby_caindo_reto
 *  - caindo na diagonal            -> kirby_caindo_diagonal
 */
void renderiza_cena(int esquerda, int direita, int parado_antes)
{
    if (esquerda) direcao_h = -1;
    else if (direita) direcao_h = 1;

    const uint16_t *sprite; int sw, sh; uint16_t transp;

    if (parado_antes) {
        sprite = (const uint16_t *)kirby_neutro_sprite;
        sw = KIRBY_NEUTRO_W; sh = KIRBY_NEUTRO_H; transp = KIRBY_NEUTRO_TRANSPARENT;
    } else if (p.vy < 0) {
        sprite = (const uint16_t *)kirby_idle_sprite;
        sw = KIRBY_IDLE_W; sh = KIRBY_IDLE_H; transp = KIRBY_IDLE_TRANSPARENT;
    } else if (esquerda || direita) {
        sprite = (const uint16_t *)kirby_caindo_diagonal_sprite;
        sw = KIRBY_CAINDO_DIAGONAL_W; sh = KIRBY_CAINDO_DIAGONAL_H; transp = KIRBY_CAINDO_DIAGONAL_TRANSPARENT;
    } else {
        sprite = (const uint16_t *)kirby_caindo_reto_sprite;
        sw = KIRBY_CAINDO_RETO_W; sh = KIRBY_CAINDO_RETO_H; transp = KIRBY_CAINDO_RETO_TRANSPARENT;
    }

    desenha_fundo();
    for (int i = 0; i < N_PLAT; i++)
        desenha_sprite(plataformas[i].x, plataformas[i].y - camera_y,
                        (const uint16_t *)plataforma_gelo_sprite,
                        PLATAFORMA_GELO_W, PLATAFORMA_GELO_H,
                        PLATAFORMA_GELO_TRANSPARENT, 0);
    desenha_sprite(p.x >> 8, (p.y >> 8) - camera_y, sprite, sw, sh, transp, direcao_h < 0);
    atualiza_display(p.pontos);
    atualiza_tela();
}

/* ---------------- main ---------------- */
int main(void)
{
    srand((unsigned) time(NULL));
    init_io();
    gera_plataformas();
    atualiza_leds(p.vidas);

    while (jogo_rodando) {
        int esquerda, direita, pular;
        ler_entrada(&esquerda, &direita, &pular);
        int parado_antes = (p.vy == 0);
        atualiza_estado(esquerda, direita, pular);
        renderiza_cena(esquerda, direita, parado_antes);
    }

#ifdef DE1_SOC
    munmap((void *)peripherals, HW_REGS_SPAN);
    close(fd);
#endif
    return 0;
}