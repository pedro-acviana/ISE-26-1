/*
 * Pula Plataformas - jogo de subir pulando entre plataformas até o "céu"
 * Funciona no CPUlator, na DE1-SoC real, ou localmente via SDL2 (p/ testar
 * no PC, ex. MSYS2 no Windows, sem precisar da placa).
 *
 * COMO COMPILAR (precisa de -lm por causa do sqrt usado no gerador de
 * plataformas e na mira do projétil do vilão):
 *   CPUlator:  gcc pula_plataformas.c -o jogo -lm
 *   DE1-SoC:   gcc -DDE1_SOC pula_plataformas.c -o jogo -lm
 *   SDL (PC):  gcc -DSDL_SIM pula_plataformas.c -o jogo -lSDL2 -lm
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

#ifdef SDL_SIM
#define SDL_MAIN_HANDLED   /* mantém nosso int main(void) em vez do main(argc,argv) do SDL */
#include <SDL2/SDL.h>
SDL_Window   *sdl_window;
SDL_Renderer *sdl_renderer;
SDL_Texture  *sdl_texture;
/* backing "registradores" simulados: os ponteiros de periférico apontam pra
 * cá em vez de endereços de hardware, então atualiza_leds/atualiza_display
 * continuam funcionando sem precisar mudar essas funções. */
static uint32_t sdl_led_reg, sdl_hex_reg, sdl_switch_reg, sdl_button_reg;
#endif

#include "sprites/kirby_idle_sprite.h"
#include "sprites/kirby_neutro_sprite.h"
#include "sprites/kirby_parado2_sprite.h"
#include "sprites/kirby_caindo_reto_sprite.h"
#include "sprites/kirby_caindo_diagonal_sprite.h"
#include "sprites/kirby_atingido_sprite.h"
#include "sprites/kirby_campeao_sprite.h"
#include "sprites/kirby_parry_sprite.h"
#include "sprites/kirby_inicial1_sprite.h"
#include "sprites/kirby_inicial2_sprite.h"
#include "sprites/kirby_inicial3_sprite.h"
#include "sprites/kirby_inicial4_sprite.h"
#include "sprites/vilao_parado_sprite.h"
#include "sprites/vilao_atingido_sprite.h"
#include "sprites/vilao_explodido_sprite.h"
#include "sprites/projetil_vilao_sprite.h"
#include "sprites/ceu_nuvens_sprite.h"
#include "sprites/cloud_bg2_sprite.h"
#include "sprites/cloud_bg3_sprite.h"
#include "sprites/cloud_bg4_sprite.h"
#include "sprites/cloud_bg5_sprite.h"
#include "sprites/cloud_bg6_sprite.h"
#include "sprites/cloud_bg7_sprite.h"
#include "sprites/cloud_bg8_sprite.h"
#include "sprites/plataforma_derr1_sprite.h"
#include "sprites/plataforma_derr2_sprite.h"
#include "sprites/plataforma_derr3_sprite.h"
#include "sprites/tela_inicio_sprite.h"
#include "sprites/tela_game_over_sprite.h"

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

/* ---------------- Temporizadores (em frames) ---------------- */
/* O loop principal não tem delay fixo (roda na velocidade do CPUlator/placa),
 * então "frames" aqui é só uma referência de duração aproximada, no mesmo
 * espírito dos outros ajustes de física do arquivo (GRAVIDADE, VEL_PULO...). */
#define FPS_ESTIMADO     30
#define FRAMES_2S        (FPS_ESTIMADO * 2)   /* duração dos estados de "atingido"/"explodindo" */
#define IDLE_ANIM_FRAMES 20                    /* velocidade da respiração/alternância parado */

/* ---------------- Estruturas do jogo ---------------- */
#define N_PLAT 8
#define CHAO_Y 220   /* altura (mundo) da plataforma inicial, usada de referência p/ pontuação */

#define KIRBY_VIDAS_MAX 5
#define VILAO_VIDAS_MAX 6
#define PONTOS_MAX      1000

typedef struct {
    int x, y, w, h;
    int melt_timer;   /* frames desde que foi gerada - ver atualiza_derretimento/plataforma_visual */
} Plataforma;

typedef struct {
    int x, y;      /* posição do personagem */
    int vy;        /* velocidade vertical */
    int   w, h;
    int   vidas;
    int   pontos;
} Player;

typedef enum { ESTADO_INICIO, ESTADO_JOGANDO, ESTADO_GAME_OVER } EstadoJogo;

typedef enum { VILAO_NORMAL, VILAO_ATINGIDO, VILAO_EXPLODINDO, VILAO_MORTO } EstadoVilao;

/* O vilão voa em coordenadas de tela (não de mundo): fica sempre no topo da
 * tela, imune ao scroll da câmera, só ricocheteando nas bordas laterais. */
typedef struct {
    int x;
    int dir;             /* 1 = direita, -1 = esquerda */
    int vidas;
    int cooldown_tiro;
    EstadoVilao estado;
    int timer;            /* frames restantes em ATINGIDO/EXPLODINDO */
} Vilao;

/* Posição/velocidade em ponto fixo 24.8, pra mirar o Kirby em linha reta
 * mesmo na diagonal. */
typedef struct {
    int ativo;
    int refletido;   /* 0 = indo pro Kirby, 1 = voltando pro vilão (pós-parry) */
    int x, y;
    int vx, vy;
} Projetil;

Plataforma plataformas[N_PLAT];
Player p;
int direcao_h = 1;   /* 1 = olhando p/ direita, -1 = p/ esquerda (última direção andada) */

EstadoJogo estado_jogo = ESTADO_INICIO;
int programa_rodando = 1;

Vilao vilao;
Projetil projetil;
int kirby_hit_timer = 0;     /* >0 = Kirby atordoado mostrando kirby_atingido, sem controle */
int idle_anim_timer = 0;     /* frames parado seguidos, pra alternar os sprites de respiração */
int contador_plataformas = 0;
int nivel_nuvem = 1;         /* 1..8: sobe a cada 10 gerações de plataforma */
int frame_intro = 0;         /* animação dos kirbys na tela de início */

/* ---------------- Inicialização de hardware ---------------- */
void init_io(void)
{
#ifdef SDL_SIM
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("Erro no SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }
    /* janela em 2x pra não ficar minúscula num monitor moderno */
    sdl_window = SDL_CreateWindow("Pula Plataformas",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   WIDTH * 2, HEIGHT * 2, SDL_WINDOW_SHOWN);
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    sdl_texture  = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGB565,
                                      SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    if (!sdl_window || !sdl_renderer || !sdl_texture) {
        printf("Erro ao criar janela/renderer/textura SDL: %s\n", SDL_GetError());
        exit(1);
    }

    led_ptr    = &sdl_led_reg;
    hex_ptr    = &sdl_hex_reg;
    switch_ptr = &sdl_switch_reg;
    button_ptr = &sdl_button_reg;
#elif defined(DE1_SOC)
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

    led_ptr    = (uint32_t *)(peripherals + LED_OFFSET);
    hex_ptr    = (uint32_t *)(peripherals + HEX_LOW_OFFSET);
    switch_ptr = (uint32_t *)(peripherals + SWITCH_OFFSET);
    button_ptr = (uint32_t *)(peripherals + BUTTON_OFFSET);
#else
    /* No CPUlator os endereços já são acessíveis diretamente */
    peripherals = (void *)HW_REGS_BASE;
    tela = (uint16_t (*)[LWIDTH]) VGA_BASE;

    led_ptr    = (uint32_t *)(peripherals + LED_OFFSET);
    hex_ptr    = (uint32_t *)(peripherals + HEX_LOW_OFFSET);
    switch_ptr = (uint32_t *)(peripherals + SWITCH_OFFSET);
    button_ptr = (uint32_t *)(peripherals + BUTTON_OFFSET);
#endif
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
#ifdef SDL_SIM
    SDL_UpdateTexture(sdl_texture, NULL, back_buffer, WIDTH * sizeof(uint16_t));
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer);
#else
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            tela[y][x] = back_buffer[y][x];
#endif
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

/* Igual a desenha_sprite, mas amostra o sprite original (sw x sh) reduzido
 * pra um tamanho menor (dw x dh) por nearest-neighbor. Usado nos ícones de
 * vida do vilão (ver desenha_vidas_vilao), pra não precisar gerar um sprite
 * novo só pra versão pequena. */
void desenha_sprite_reduzido(int x, int y, const uint16_t *sprite, int sw, int sh,
                              uint16_t transparente, int dw, int dh)
{
    for (int j = 0; j < dh; j++)
        for (int i = 0; i < dw; i++) {
            int sx = i * sw / dw, sy = j * sh / dh;
            uint16_t cor = sprite[sy * sw + sx];
            if (cor != transparente)
                plot_pixel(x + i, y + j, cor);
        }
}

/* Um sprite de céu por nível de altitude (nivel_nuvem 1..8), trocado a cada
 * 10 gerações de plataforma (ver gera_plataforma_seguinte). O último nível
 * fica valendo pra sempre depois disso. */
static const uint16_t *const ceu_niveis[8] = {
    (const uint16_t *)ceu_nuvens_sprite,
    (const uint16_t *)cloud_bg2_sprite,
    (const uint16_t *)cloud_bg3_sprite,
    (const uint16_t *)cloud_bg4_sprite,
    (const uint16_t *)cloud_bg5_sprite,
    (const uint16_t *)cloud_bg6_sprite,
    (const uint16_t *)cloud_bg7_sprite,
    (const uint16_t *)cloud_bg8_sprite,
};

/* Desenha o fundo "infinito": um único sprite (céu + nuvens já combinados
 * numa imagem só) repetido em grade por toda a tela, sem transparência - a
 * imagem cobre o tile inteiro, então não sobra nenhum buraco entre eles.
 * A fase vertical acompanha camera_y, então o padrão rola junto conforme o
 * jogador sobe, em vez de ficar "grudado" na tela. */
void desenha_fundo(void)
{
    const uint16_t *ceu = ceu_niveis[nivel_nuvem - 1];
    int fase = camera_y % CEU_NUVENS_H;
    if (fase < 0) fase += CEU_NUVENS_H;

    for (int y = -CEU_NUVENS_H + fase; y < HEIGHT; y += CEU_NUVENS_H)
        for (int x = -CEU_NUVENS_W; x < WIDTH; x += CEU_NUVENS_W)
            desenha_sprite(x, y, ceu, CEU_NUVENS_W, CEU_NUVENS_H, CEU_NUVENS_TRANSPARENT, 0);
}

/* ---------------- Tabela de dígitos p/ display 7 seg ---------------- */
const uint8_t seg[10] = {
    0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F
};

void atualiza_display(int pontos)
{
#ifdef SDL_SIM
    /* sem display de 7 seg de verdade: mostra pontos/vidas no título da janela */
    char titulo[64];
    snprintf(titulo, sizeof(titulo), "Pula Plataformas - Pontos: %d  Vidas: %d", pontos, p.vidas);
    SDL_SetWindowTitle(sdl_window, titulo);
#else
    int milhar = (pontos / 1000) % 10;
    int centena = (pontos / 100) % 10;
    int dez = (pontos / 10) % 10;
    int uni = pontos % 10;
    *hex_ptr = seg[milhar] << 24 | seg[centena] << 16 | seg[dez] << 8 | seg[uni];
#endif
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

    /* a cada 10 gerações, sobe um nível de céu (ver ceu_niveis); satura no
     * último nível em vez de voltar ao início. */
    contador_plataformas++;
    nivel_nuvem = 1 + contador_plataformas / 10;
    if (nivel_nuvem > 8) nivel_nuvem = 8;
}

int sorteia_dy(void)
{
    int faixa = N_PLAT_ALTURA_MAX - N_PLAT_ALTURA_MIN + 1;
    return N_PLAT_ALTURA_MIN + rand() % faixa;
}

/* ---------------- Vilão e projétil ---------------- */
#define VILAO_Y             14   /* posição fixa no topo da tela (coordenada de tela, não de mundo) */
#define VILAO_VEL_PX        1
#define VILAO_COOLDOWN_TIRO (FPS_ESTIMADO * 3)
#define PROJETIL_VEL        384.0   /* ponto fixo 24.8 -> ~1.5px/frame */

void reseta_vilao(void)
{
    vilao.x = WIDTH / 2 - VILAO_PARADO_W / 2;
    vilao.dir = 1;
    vilao.vidas = VILAO_VIDAS_MAX;
    vilao.cooldown_tiro = VILAO_COOLDOWN_TIRO;
    vilao.estado = VILAO_NORMAL;
    vilao.timer = 0;
    projetil.ativo = 0;
}

/* Mira no Kirby no instante do disparo (vetor normalizado * velocidade
 * fixa); depois disso o projétil só anda em linha reta, sem re-mirar. */
void dispara_projetil(void)
{
    int origem_x = vilao.x + VILAO_PARADO_W / 2;
    int origem_y = VILAO_Y + VILAO_PARADO_H;
    double alvo_x = (p.x >> 8) + p.w / 2.0;
    double alvo_y = ((p.y >> 8) - camera_y) + p.h / 2.0;
    double dx = alvo_x - origem_x;
    double dy = alvo_y - origem_y;
    double dist = sqrt(dx * dx + dy * dy);
    if (dist < 1.0) dist = 1.0;

    projetil.ativo = 1;
    projetil.refletido = 0;
    projetil.x = origem_x << 8;
    projetil.y = origem_y << 8;
    projetil.vx = (int)(dx / dist * PROJETIL_VEL);
    projetil.vy = (int)(dy / dist * PROJETIL_VEL);
}

/* Nave inimiga: patrulha lateralmente no topo da tela e atira periodicamente.
 * Enquanto ATINGIDO/EXPLODINDO fica parada mostrando o sprite do estado até
 * o timer zerar; depois de EXPLODINDO ela morre de vez (MORTO), derrotada
 * pelo resto da partida. */
void atualiza_vilao(void)
{
    if (vilao.estado == VILAO_MORTO) return;

    if (vilao.estado == VILAO_ATINGIDO) {
        if (--vilao.timer <= 0)
            vilao.estado = (vilao.vidas > 0) ? VILAO_NORMAL : VILAO_EXPLODINDO;
        if (vilao.estado == VILAO_EXPLODINDO)
            vilao.timer = FRAMES_2S;
        return;
    }

    if (vilao.estado == VILAO_EXPLODINDO) {
        if (--vilao.timer <= 0)
            vilao.estado = VILAO_MORTO;
        return;
    }

    vilao.x += vilao.dir * VILAO_VEL_PX;
    if (vilao.x < N_PLAT_MARGEM) { vilao.x = N_PLAT_MARGEM; vilao.dir = 1; }
    if (vilao.x > WIDTH - VILAO_PARADO_W - N_PLAT_MARGEM) { vilao.x = WIDTH - VILAO_PARADO_W - N_PLAT_MARGEM; vilao.dir = -1; }

    if (!projetil.ativo && --vilao.cooldown_tiro <= 0) {
        dispara_projetil();
        vilao.cooldown_tiro = VILAO_COOLDOWN_TIRO;
    }
}

/* Ângulo de devolução do parry (ver atualiza_projetil): reflexo reto pra
 * cima por padrão, ou na diagonal escolhida seguando botão 0/1 (mira_dir/
 * mira_esq) no instante em que o projétil toca o Kirby. */
#define MIRA_DIAGONAL 0.70710678   /* sen/cos de 45 graus */

/* Move o projétil ativo e resolve colisão: contra o Kirby enquanto vai na
 * ida (refletido == 0), contra o vilão depois de refletido pelo parry. Sair
 * da tela em qualquer direção também desativa o projétil.
 * mira_esq/mira_dir = botão 1/0 segurados junto com o parry, escolhendo pra
 * que lado devolver o projétil (ver desenha_seta_mira). */
void atualiza_projetil(int parry, int mira_esq, int mira_dir)
{
    if (!projetil.ativo) return;

    projetil.x += projetil.vx;
    projetil.y += projetil.vy;

    int px = projetil.x >> 8, py = projetil.y >> 8;
    if (px < -PROJETIL_VILAO_W || px > WIDTH ||
        py < -PROJETIL_VILAO_H || py > HEIGHT) {
        projetil.ativo = 0;
        return;
    }

    if (!projetil.refletido) {
        int kx = p.x >> 8, ky = (p.y >> 8) - camera_y;
        int colide = px + PROJETIL_VILAO_W > kx && px < kx + p.w &&
                     py + PROJETIL_VILAO_H > ky && py < ky + p.h;
        if (colide && kirby_hit_timer <= 0) {
            if (parry) {
                projetil.refletido = 1;
                if (mira_dir && !mira_esq) {
                    projetil.vx = (int)(PROJETIL_VEL * MIRA_DIAGONAL);
                    projetil.vy = (int)(-PROJETIL_VEL * MIRA_DIAGONAL);
                } else if (mira_esq && !mira_dir) {
                    projetil.vx = (int)(-PROJETIL_VEL * MIRA_DIAGONAL);
                    projetil.vy = (int)(-PROJETIL_VEL * MIRA_DIAGONAL);
                } else {
                    projetil.vx = 0;
                    projetil.vy = (int)(-PROJETIL_VEL);
                }
            } else {
                p.vidas--;
                atualiza_leds(p.vidas);
                kirby_hit_timer = FRAMES_2S;
                projetil.ativo = 0;
                if (p.vidas <= 0) {
                    printf("Fim de jogo. Pontos: %d\n", p.pontos);
                    estado_jogo = ESTADO_GAME_OVER;
                }
            }
        }
    } else if (vilao.estado == VILAO_NORMAL) {
        int colide = px + PROJETIL_VILAO_W > vilao.x && px < vilao.x + VILAO_PARADO_W &&
                     py + PROJETIL_VILAO_H > VILAO_Y && py < VILAO_Y + VILAO_PARADO_H;
        if (colide) {
            vilao.vidas--;
            vilao.estado = VILAO_ATINGIDO;
            vilao.timer = FRAMES_2S;
            projetil.ativo = 0;
        }
    }
}

void desenha_vilao(void)
{
    const uint16_t *sprite; int sw, sh; uint16_t transp;

    switch (vilao.estado) {
    case VILAO_MORTO:
        return;
    case VILAO_ATINGIDO:
        sprite = (const uint16_t *)vilao_atingido_sprite;
        sw = VILAO_ATINGIDO_W; sh = VILAO_ATINGIDO_H; transp = VILAO_ATINGIDO_TRANSPARENT;
        break;
    case VILAO_EXPLODINDO:
        sprite = (const uint16_t *)vilao_explodido_sprite;
        sw = VILAO_EXPLODIDO_W; sh = VILAO_EXPLODIDO_H; transp = VILAO_EXPLODIDO_TRANSPARENT;
        break;
    default:
        sprite = (const uint16_t *)vilao_parado_sprite;
        sw = VILAO_PARADO_W; sh = VILAO_PARADO_H; transp = VILAO_PARADO_TRANSPARENT;
        break;
    }

    int cx = vilao.x + VILAO_PARADO_W / 2;
    int cy = VILAO_Y + VILAO_PARADO_H / 2;
    desenha_sprite(cx - sw / 2, cy - sh / 2, sprite, sw, sh, transp, vilao.dir < 0);
}

/* Vidas do vilão (VILAO_VIDAS_MAX), em versões reduzidas do próprio sprite
 * parado, enfileiradas no topo da tela - acima da faixa de voo do vilão
 * (VILAO_Y), então não fica por cima dele. */
#define VIDA_VILAO_W 11
#define VIDA_VILAO_H 10
#define VIDA_VILAO_GAP 2

void desenha_vidas_vilao(void)
{
    for (int i = 0; i < vilao.vidas; i++) {
        int x = 4 + i * (VIDA_VILAO_W + VIDA_VILAO_GAP);
        desenha_sprite_reduzido(x, 2, (const uint16_t *)vilao_parado_sprite,
                                 VILAO_PARADO_W, VILAO_PARADO_H, VILAO_PARADO_TRANSPARENT,
                                 VIDA_VILAO_W, VIDA_VILAO_H);
    }
}

void desenha_projetil(void)
{
    if (!projetil.ativo) return;
    desenha_sprite(projetil.x >> 8, projetil.y >> 8,
                    (const uint16_t *)projetil_vilao_sprite,
                    PROJETIL_VILAO_W, PROJETIL_VILAO_H, PROJETIL_VILAO_TRANSPARENT, 0);
}

/* Seta vermelha mostrada durante o parry, indicando a direção (esquerda/
 * reto/direita, conforme botão 1/0 segurado junto) escolhida pra devolver o
 * projétil do vilão - ver mira_esq/mira_dir em atualiza_projetil. Desenhada
 * "à mão" (triângulo via plot_pixel) pra não precisar de um sprite novo. */
void desenha_seta_mira(int mira_esq, int mira_dir)
{
    int dir = (mira_dir && !mira_esq) ? 1 : (mira_esq && !mira_dir ? -1 : 0);
    int cx = (p.x >> 8) + p.w / 2;
    int cy = (p.y >> 8) - camera_y - 6;

    for (int i = 0; i <= 8; i++) {
        int ponta_x = cx + dir * 6 * i / 8;
        int meia_largura = 3 * (8 - i) / 8;
        for (int x = ponta_x - meia_largura; x <= ponta_x + meia_largura; x++)
            plot_pixel(x, cy - i, RED);
    }
}

/* Vidas do Kirby (KIRBY_VIDAS_MAX), como pips vermelhos no canto inferior
 * direito da tela - mesmo valor também sai nos LEDs (ver atualiza_leds). */
#define VIDA_KIRBY_TAM 6
#define VIDA_KIRBY_GAP 3

void desenha_vidas_kirby(void)
{
    int total_w = KIRBY_VIDAS_MAX * VIDA_KIRBY_TAM + (KIRBY_VIDAS_MAX - 1) * VIDA_KIRBY_GAP;
    int x0 = WIDTH - 4 - total_w;
    int y0 = HEIGHT - 4 - VIDA_KIRBY_TAM;
    for (int i = 0; i < p.vidas; i++)
        desenha_retangulo(x0 + i * (VIDA_KIRBY_TAM + VIDA_KIRBY_GAP), y0,
                           VIDA_KIRBY_TAM, VIDA_KIRBY_TAM, RED);
}

void gera_plataformas(void)
{
    contador_plataformas = 0;
    nivel_nuvem = 1;

    plataformas[0] = (Plataforma){140, CHAO_Y, N_PLAT_LARGURA, 6};   /* chão */

    for (int i = 1; i < N_PLAT; i++)
        gera_plataforma_seguinte(&plataformas[i - 1], &plataformas[i], sorteia_dy());

    p.x = plataformas[0].x << 8;
    p.y = (plataformas[0].y - KIRBY_IDLE_H) << 8;
    p.vy = 0;
    p.w = KIRBY_IDLE_W; p.h = KIRBY_IDLE_H;
    p.vidas = KIRBY_VIDAS_MAX;
    p.pontos = 0;
    camera_y = 0;

    kirby_hit_timer = 0;
    idle_anim_timer = 0;
    reseta_vilao();
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

/* ---------------- Derretimento das plataformas ---------------- */
/* Dificuldade extra: toda plataforma dura só PLAT_VIDA_FRAMES (9s) desde que
 * foi gerada. A cada PLAT_ESTAGIO_FRAMES (3s) ela passa a usar um sprite de
 * "derretendo" menor (1 -> 2 -> 3), dando cada vez menos espaço pra pousar,
 * até sumir de vez e ser reaproveitada (mesmo mecanismo de recicla_plataformas
 * acima, então o jogador some com o chão embaixo dele se demorar demais). */
#define PLAT_ESTAGIO_FRAMES (FPS_ESTIMADO * 3)
#define PLAT_VIDA_FRAMES    (FPS_ESTIMADO * 9)

void plataforma_visual(const Plataforma *pl, const uint16_t **sprite,
                        int *sw, int *sh, uint16_t *transp)
{
    int estagio = pl->melt_timer / PLAT_ESTAGIO_FRAMES;
    switch (estagio) {
    case 0:
        *sprite = (const uint16_t *)plataforma_derr1_sprite;
        *sw = PLATAFORMA_DERR1_W; *sh = PLATAFORMA_DERR1_H; *transp = PLATAFORMA_DERR1_TRANSPARENT;
        break;
    case 1:
        *sprite = (const uint16_t *)plataforma_derr2_sprite;
        *sw = PLATAFORMA_DERR2_W; *sh = PLATAFORMA_DERR2_H; *transp = PLATAFORMA_DERR2_TRANSPARENT;
        break;
    default:
        *sprite = (const uint16_t *)plataforma_derr3_sprite;
        *sw = PLATAFORMA_DERR3_W; *sh = PLATAFORMA_DERR3_H; *transp = PLATAFORMA_DERR3_TRANSPARENT;
        break;
    }
}

void atualiza_derretimento(void)
{
    for (int i = 0; i < N_PLAT; i++) {
        plataformas[i].melt_timer++;
        if (plataformas[i].melt_timer >= PLAT_VIDA_FRAMES) {
            Plataforma *topo = plataforma_mais_alta();
            gera_plataforma_seguinte(topo, &plataformas[i], sorteia_dy());
        }
    }
}

void ler_entrada(int *esquerda, int *direita, int *pular, int *parry)
{
#ifdef SDL_SIM
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT) programa_rodando = 0;

    const uint8_t *tecla = SDL_GetKeyboardState(NULL);
    *esquerda = tecla[SDL_SCANCODE_LEFT]  || tecla[SDL_SCANCODE_A];   /* botão 1 */
    *direita  = tecla[SDL_SCANCODE_RIGHT] || tecla[SDL_SCANCODE_D];   /* botão 0 */
    *pular    = tecla[SDL_SCANCODE_SPACE] || tecla[SDL_SCANCODE_UP];  /* chave 0 */
    *parry    = tecla[SDL_SCANCODE_LSHIFT] || tecla[SDL_SCANCODE_RSHIFT]; /* botão 2 */
#else
    uint32_t sw = *switch_ptr;
    uint32_t bt = *button_ptr;
    *esquerda = bt & 0x2;    /* botão 1 */
    *direita  = bt & 0x1;    /* botão 0 */
    *pular    = sw & 0x1;    /* chave 0 */
    *parry    = bt & 0x4;    /* botão 2 - segurar p/ postura de parry */
#endif
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
            const uint16_t *sprite_pl; int sw, sh; uint16_t transp_pl;
            plataforma_visual(pl, &sprite_pl, &sw, &sh, &transp_pl);
            int px = p.x >> 8, py = p.y >> 8;
            if (px + p.w > pl->x && px < pl->x + sw &&
                py + p.h >= pl->y && py + p.h <= pl->y + sh + 4) {
                p.y = (pl->y - p.h) << 8;
                p.vy = 0;
            }
        }
    }

    /* pontuação = altura recorde alcançada (em mundo), 10px por ponto, até o
     * teto de PONTOS_MAX (ver renderiza_cena p/ o sprite de campeão nesse ponto) */
    int altura = CHAO_Y - (p.y >> 8);
    if (altura / 10 > p.pontos) {
        p.pontos = altura / 10;
        if (p.pontos > PONTOS_MAX) p.pontos = PONTOS_MAX;
    }

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
            estado_jogo = ESTADO_GAME_OVER;
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

    /* respiração do Kirby parado: reinicia sempre que ele sai do chão, pra
     * alternância dos dois sprites (ver renderiza_cena) recomeçar do zero a
     * cada novo pulo. */
    if (p.vy == 0) idle_anim_timer++;
    else idle_anim_timer = 0;
}

/* Desenha o sprite do Kirby ancorado no centro-base do seu hitbox (p.x/p.y,
 * p.w/p.h), em vez do canto superior esquerdo direto - assim sprites de
 * tamanho bem diferente (atingido, campeão, parry) não "saltam" de posição
 * quando substituem o sprite normal. */
void desenha_kirby(const uint16_t *sprite, int sw, int sh, uint16_t transp, int espelhado)
{
    int base_x = (p.x >> 8) + p.w / 2 - sw / 2;
    int base_y = (p.y >> 8) - camera_y + p.h - sh;
    desenha_sprite(base_x, base_y, sprite, sw, sh, transp, espelhado);
}

/* Escolhe o sprite do Kirby de acordo com o estado do frame ANTERIOR à
 * atualização de física (parado_antes vem de antes de atualiza_estado
 * rodar) e o vy já atualizado (subindo/caindo). Direção esquerda/direita só
 * espelha o mesmo desenho, não precisa de arte separada por lado.
 *
 * Prioridade (maior pra menor): atordoado (atingido) > vilão explodindo
 * (campeão) > parry segurado > parado (respirando, alterna 2 sprites) >
 * subindo > caindo reto/diagonal.
 *
 * mira_esq/mira_dir: só usados durante o parry (ver atualiza_projetil e
 * desenha_seta_mira), pra mostrar a seta de direção de devolução do tiro.
 */
void renderiza_cena(int esquerda, int direita, int parado_antes, int parry,
                     int mira_esq, int mira_dir)
{
    if (esquerda) direcao_h = -1;
    else if (direita) direcao_h = 1;

    const uint16_t *sprite; int sw, sh; uint16_t transp;

    if (kirby_hit_timer > 0) {
        sprite = (const uint16_t *)kirby_atingido_sprite;
        sw = KIRBY_ATINGIDO_W; sh = KIRBY_ATINGIDO_H; transp = KIRBY_ATINGIDO_TRANSPARENT;
    } else if (vilao.estado == VILAO_EXPLODINDO || p.pontos >= PONTOS_MAX) {
        sprite = (const uint16_t *)kirby_campeao_sprite;
        sw = KIRBY_CAMPEAO_W; sh = KIRBY_CAMPEAO_H; transp = KIRBY_CAMPEAO_TRANSPARENT;
    } else if (parry) {
        sprite = (const uint16_t *)kirby_parry_sprite;
        sw = KIRBY_PARRY_W; sh = KIRBY_PARRY_H; transp = KIRBY_PARRY_TRANSPARENT;
    } else if (parado_antes) {
        if ((idle_anim_timer / IDLE_ANIM_FRAMES) % 2 == 0) {
            sprite = (const uint16_t *)kirby_neutro_sprite;
            sw = KIRBY_NEUTRO_W; sh = KIRBY_NEUTRO_H; transp = KIRBY_NEUTRO_TRANSPARENT;
        } else {
            sprite = (const uint16_t *)kirby_parado2_sprite;
            sw = KIRBY_PARADO2_W; sh = KIRBY_PARADO2_H; transp = KIRBY_PARADO2_TRANSPARENT;
        }
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
    for (int i = 0; i < N_PLAT; i++) {
        const uint16_t *sprite_pl; int sw_pl, sh_pl; uint16_t transp_pl;
        plataforma_visual(&plataformas[i], &sprite_pl, &sw_pl, &sh_pl, &transp_pl);
        desenha_sprite(plataformas[i].x, plataformas[i].y - camera_y,
                        sprite_pl, sw_pl, sh_pl, transp_pl, 0);
    }
    desenha_vilao();
    desenha_projetil();
    desenha_kirby(sprite, sw, sh, transp, direcao_h < 0);
    if (parry) desenha_seta_mira(mira_esq, mira_dir);
    desenha_vidas_vilao();
    desenha_vidas_kirby();
    atualiza_display(p.pontos);
    atualiza_tela();
}

/* ---------------- Telas de início / game over ---------------- */
#define INTRO_FRAME1 (FPS_ESTIMADO * 1)
#define INTRO_FRAME2 (FPS_ESTIMADO * 1)

/* Fundo (320x240, resolução nativa da tela, com letterbox preto onde a
 * imagem original não preenche 4:3 - ver --letterbox em tools/png2c.py) com
 * os 4 kirbys da pasta de sprites: 1 e 2 aparecem uma vez cada, depois
 * alterna 3/4 pra sempre - efeito de "acordando" seguido de respiração. */
void renderiza_tela_inicio(void)
{
    desenha_sprite(0, 0, (const uint16_t *)tela_inicio_sprite, TELA_INICIO_W, TELA_INICIO_H,
                    TELA_INICIO_TRANSPARENT, 0);

    const uint16_t *sprite; int sw, sh; uint16_t transp;
    if (frame_intro < INTRO_FRAME1) {
        sprite = (const uint16_t *)kirby_inicial1_sprite;
        sw = KIRBY_INICIAL1_W; sh = KIRBY_INICIAL1_H; transp = KIRBY_INICIAL1_TRANSPARENT;
    } else if (frame_intro < INTRO_FRAME1 + INTRO_FRAME2) {
        sprite = (const uint16_t *)kirby_inicial2_sprite;
        sw = KIRBY_INICIAL2_W; sh = KIRBY_INICIAL2_H; transp = KIRBY_INICIAL2_TRANSPARENT;
    } else if (((frame_intro - INTRO_FRAME1 - INTRO_FRAME2) / IDLE_ANIM_FRAMES) % 2 == 0) {
        sprite = (const uint16_t *)kirby_inicial3_sprite;
        sw = KIRBY_INICIAL3_W; sh = KIRBY_INICIAL3_H; transp = KIRBY_INICIAL3_TRANSPARENT;
    } else {
        sprite = (const uint16_t *)kirby_inicial4_sprite;
        sw = KIRBY_INICIAL4_W; sh = KIRBY_INICIAL4_H; transp = KIRBY_INICIAL4_TRANSPARENT;
    }
    frame_intro++;

    desenha_sprite(WIDTH / 2 - sw / 2, HEIGHT / 2 - sh / 2, sprite, sw, sh, transp, 0);
    atualiza_tela();
}

/* Fundo de game over já traz as instruções ("botão 1 tentar de novo, botão 0
 * sair") desenhadas na própria imagem; só soma o kirby_atingido num canto
 * pra reforçar visualmente o motivo do fim de jogo. */
void renderiza_tela_game_over(void)
{
    desenha_sprite(0, 0, (const uint16_t *)tela_game_over_sprite, TELA_GAME_OVER_W, TELA_GAME_OVER_H,
                    TELA_GAME_OVER_TRANSPARENT, 0);
    desenha_sprite(WIDTH - KIRBY_ATINGIDO_W - 8, HEIGHT - KIRBY_ATINGIDO_H - 4,
                   (const uint16_t *)kirby_atingido_sprite,
                   KIRBY_ATINGIDO_W, KIRBY_ATINGIDO_H, KIRBY_ATINGIDO_TRANSPARENT, 0);
    atualiza_tela();
}

/* ---------------- main ---------------- */
int main(void)
{
    srand((unsigned) time(NULL));
    init_io();

    while (programa_rodando) {
#ifdef SDL_SIM
        Uint32 frame_inicio = SDL_GetTicks();
#endif
        int esquerda, direita, pular, parry;
        ler_entrada(&esquerda, &direita, &pular, &parry);

        switch (estado_jogo) {
        case ESTADO_INICIO:
            renderiza_tela_inicio();
            if (direita) {   /* botão 0 = iniciar */
                gera_plataformas();
                atualiza_leds(p.vidas);
                estado_jogo = ESTADO_JOGANDO;
            }
            break;

        case ESTADO_JOGANDO: {
            int parado_antes = (p.vy == 0);
            if (kirby_hit_timer > 0) {
                kirby_hit_timer--;
                esquerda = direita = pular = 0;
            }
            /* durante o parry, botão 1/0 não move o Kirby - escolhem a
             * direção de devolução do projétil (ver desenha_seta_mira) */
            int mov_esquerda = parry ? 0 : esquerda;
            int mov_direita  = parry ? 0 : direita;
            atualiza_estado(mov_esquerda, mov_direita, pular);
            atualiza_vilao();
            atualiza_projetil(parry, esquerda, direita);
            atualiza_derretimento();
            if (estado_jogo == ESTADO_GAME_OVER)
                break;
            renderiza_cena(mov_esquerda, mov_direita, parado_antes, parry, esquerda, direita);
            break;
        }

        case ESTADO_GAME_OVER:
            renderiza_tela_game_over();
            if (esquerda) {          /* botão 1 = tentar novamente */
                gera_plataformas();
                atualiza_leds(p.vidas);
                estado_jogo = ESTADO_JOGANDO;
            } else if (direita) {    /* botão 0 = sair */
                programa_rodando = 0;
            }
            break;
        }
#ifdef SDL_SIM
        /* a física (GRAVIDADE/VEL_PULO/...) foi calibrada assumindo
         * FPS_ESTIMADO=30; sem isso o loop rodaria sem limite de velocidade. */
        Uint32 duracao = SDL_GetTicks() - frame_inicio;
        Uint32 alvo = 1000 / FPS_ESTIMADO;
        if (duracao < alvo) SDL_Delay(alvo - duracao);
#endif
    }

#ifdef SDL_SIM
    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
#elif defined(DE1_SOC)
    munmap((void *)peripherals, HW_REGS_SPAN);
    close(fd);
#endif
    return 0;
}
