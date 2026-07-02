#!/usr/bin/env python3
"""
Converte um PNG (ou um recorte dele) em um array C (RGB565) para uso direto
no VGA da DE1-SoC.

Uso:
    # imagem já com fundo transparente (alpha)
    python png2c.py sprites/personagem.png > personagem_sprite.h

    # sprite sheet sem alpha, com fundo de cor sólida (ex: sheets do
    # spriters-resource) -- recorta uma região e trata a cor de fundo
    # como transparente
    python png2c.py sprites/sheet.png --crop 6,49,14,21 --bg 84,110,140 \
        --nome kirby_idle > kirby_idle_sprite.h

Pixels transparentes (alpha == 0, ou que combinem com --bg dentro da
tolerância) viram a cor-chave TRANSPARENT_COLOR (magenta, 0xF81F), que deve
ser ignorada ao desenhar o sprite.
"""
import argparse
from PIL import Image

TRANSPARENT_COLOR = 0xF81F  # magenta - usado como marcador de transparência


def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def parse_tupla(txt, tipo=int):
    return tuple(tipo(v) for v in txt.split(","))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("arquivo", help="PNG de entrada")
    ap.add_argument("--crop", help="x,y,w,h -- recorta essa região antes de converter")
    ap.add_argument("--bg", action="append",
                     help="r,g,b -- cor de fundo a ser tratada como transparente "
                          "(pode ser passado mais de uma vez, se houver mais de um tom de fundo)")
    ap.add_argument("--tolerancia", type=int, default=10,
                     help="tolerância de diferença de cor para o --bg (padrão: 10)")
    ap.add_argument("--nome", help="nome do sprite / macro (padrão: nome do arquivo)")
    args = ap.parse_args()

    img = Image.open(args.arquivo).convert("RGBA")

    if args.crop:
        x, y, w, h = parse_tupla(args.crop)
        img = img.crop((x, y, x + w, y + h))

    w, h = img.size
    nome = args.nome or args.arquivo.split("/")[-1].split("\\")[-1].rsplit(".", 1)[0]
    nome = "".join(c if c.isalnum() else "_" for c in nome)

    bgs = [parse_tupla(b) for b in args.bg] if args.bg else []

    print(f"/* Gerado automaticamente a partir de {args.arquivo} */")
    print(f"#ifndef {nome.upper()}_SPRITE_H")
    print(f"#define {nome.upper()}_SPRITE_H")
    print()
    print(f"#define {nome.upper()}_W {w}")
    print(f"#define {nome.upper()}_H {h}")
    print(f"#define {nome.upper()}_TRANSPARENT 0x{TRANSPARENT_COLOR:04X}")
    print()
    print(f"static const unsigned short {nome}_sprite[{h}][{w}] = {{")
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            transparente = a == 0
            if not transparente:
                transparente = any(
                    abs(r - bg[0]) <= args.tolerancia and
                    abs(g - bg[1]) <= args.tolerancia and
                    abs(b - bg[2]) <= args.tolerancia
                    for bg in bgs
                )
            cor = TRANSPARENT_COLOR if transparente else rgb888_to_rgb565(r, g, b)
            row.append(f"0x{cor:04X}")
        print("    {" + ", ".join(row) + "},")
    print("};")
    print()
    print(f"#endif /* {nome.upper()}_SPRITE_H */")


if __name__ == "__main__":
    main()
