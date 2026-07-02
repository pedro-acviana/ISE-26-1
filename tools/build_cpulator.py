#!/usr/bin/env python3
"""
Gera uma versao "tudo em um arquivo so" do jogo, para colar no CPUlator
(que nao suporta multiplos arquivos / #include de headers locais).

Uso:
    python tools/build_cpulator.py pula_plat.c > pula_plat_cpulator.c
"""
import re
import sys
import os


def inline_includes(caminho, base_dir):
    with open(caminho, encoding="utf-8") as f:
        texto = f.read()

    def substitui(m):
        rel = m.group(1)
        caminho_incluido = os.path.join(base_dir, rel)
        with open(caminho_incluido, encoding="utf-8") as f:
            conteudo = f.read()
        return (f"/* ---- inicio de {rel} (inlined p/ CPUlator) ---- */\n"
                f"{conteudo}\n"
                f"/* ---- fim de {rel} ---- */")

    return re.sub(r'#include\s+"([^"]+)"', substitui, texto)


def main():
    if len(sys.argv) != 2:
        print("uso: python build_cpulator.py <arquivo.c>", file=sys.stderr)
        sys.exit(1)

    caminho = sys.argv[1]
    base_dir = os.path.dirname(os.path.abspath(caminho))
    sys.stdout.reconfigure(encoding="utf-8")
    print(inline_includes(caminho, base_dir))


if __name__ == "__main__":
    main()
