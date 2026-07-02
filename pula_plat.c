/*
 * Pula Plataformas - jogo de subir pulando entre plataformas até o "céu"
 * Funciona tanto no CPUlator quanto na DE1-SoC real.
 *
 * COMO COMPILAR:
 *   CPUlator:  gcc pula_plataformas.c -o jogo
 *   DE1-SoC:   gcc -DDE1_SOC pula_plataformas.c -o jogo
 *
 * COMO RODAR (na placa, precisa ser root):
 *   sudo ./jogo
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef DE1_SOC
#include <fcntl.h>
#include <sys/mman.h>
int fd;
#endif

#include "sprites/kirby_idle_sprite.h"

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
#define GREEN  0x07E0
#define BLUE   0x001F
#define YELLOW 0xFFE0
#define RED    0xF800

/* ---------------- Ponteiros globais ---------------- */
volatile void     *peripherals;
volatile uint32_t *led_ptr;
volatile uint32_t *hex_ptr;
volatile uint32_t *switch_ptr;
volatile uint32_t *button_ptr;
uint16_t (*tela)[LWIDTH];   /* buffer de vídeo */

/* ---------------- Estruturas do jogo ---------------- */
#define N_PLAT 6

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
    tela[y][x] = cor;
}

void limpa_tela(void)
{
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            tela[y][x] = BLACK;
}

void desenha_retangulo(int x, int y, int w, int h, uint16_t cor)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            plot_pixel(x + i, y + j, cor);
}

/* Desenha um sprite (array w x h de cores RGB565), pulando os pixels que
 * combinam com a cor-chave de transparência. */
void desenha_sprite(int x, int y, const uint16_t *sprite, int w, int h, uint16_t transparente)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            uint16_t cor = sprite[j * w + i];
            if (cor != transparente)
                plot_pixel(x + i, y + j, cor);
        }
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
void gera_plataformas(void)
{
    plataformas[0] = (Plataforma){140, 220, 40, 6};   /* chão */
    plataformas[1] = (Plataforma){40,  190, 40, 6};
    plataformas[2] = (Plataforma){220, 160, 40, 6};
    plataformas[3] = (Plataforma){80,  130, 40, 6};
    plataformas[4] = (Plataforma){220, 100, 40, 6};
    plataformas[5] = (Plataforma){130, 60,  40, 6};   /* topo = objetivo */

    p.x = 150 << 8; p.y = 200 << 8; p.vy = 0;
    p.w = KIRBY_IDLE_W; p.h = KIRBY_IDLE_H;
    p.vidas = 3;
    p.pontos = 0;
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
    const int gravidade = 100;   /* equivale a ~0.4px/frame^2 */
    const int vel_pulo  = -1536; /* equivale a ~-6px/frame */
    const int vel_lat   = 512;   /* equivale a ~2px/frame */

    if (esquerda) p.x -= vel_lat;
    if (direita)  p.x += vel_lat;

    if (pular && p.vy == 0) p.vy = vel_pulo;

    p.vy += gravidade;
    p.y  += p.vy;

    if (p.vy > 0) {
        for (int i = 0; i < N_PLAT; i++) {
            Plataforma *pl = &plataformas[i];
            int px = p.x >> 8, py = p.y >> 8;
            if (px + p.w/256 > pl->x && px < pl->x + pl->w &&
                py + p.h/256 >= pl->y && py + p.h/256 <= pl->y + pl->h + 4) {
                p.y = (pl->y - p.h/256) << 8;
                p.vy = 0;
                if (i == N_PLAT - 1) {
                    printf("Você chegou ao céu! Pontos: %d\n", p.pontos);
                    jogo_rodando = 0;
                }
            }
        }
    }

    if ((p.y >> 8) > HEIGHT) {
        p.vidas--;
        atualiza_leds(p.vidas);
        if (p.vidas <= 0) {
            printf("Fim de jogo. Pontos: %d\n", p.pontos);
            jogo_rodando = 0;
        } else {
            p.x = 150 << 8; p.y = 200 << 8; p.vy = 0;
        }
    }

    if (p.x < 0) p.x = 0;
    if ((p.x >> 8) > WIDTH - p.w) p.x = (WIDTH - p.w) << 8;
}
void renderiza_cena(void)
{
    limpa_tela();
    for (int i = 0; i < N_PLAT; i++)
        desenha_retangulo(plataformas[i].x, plataformas[i].y,
                           plataformas[i].w, plataformas[i].h, GREEN);
    desenha_sprite(p.x >> 8, p.y >> 8, (const uint16_t *)kirby_idle_sprite,
                   KIRBY_IDLE_W, KIRBY_IDLE_H, KIRBY_IDLE_TRANSPARENT);
    atualiza_display(p.pontos);
}

/* ---------------- main ---------------- */
int main(void)
{
    init_io();
    gera_plataformas();
    atualiza_leds(p.vidas);

    while (jogo_rodando) {
        int esquerda, direita, pular;
        ler_entrada(&esquerda, &direita, &pular);
        atualiza_estado(esquerda, direita, pular);
        renderiza_cena();
    }

#ifdef DE1_SOC
    munmap((void *)peripherals, HW_REGS_SPAN);
    close(fd);
#endif
    return 0;
}