#!/usr/bin/env python3
"""Embed exact launcher PNG pixels in compact indexed PAX buffers (requires Pillow)."""
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
arrays=[]
palettes=[]
for n in range(1,7):
    with Image.open(root/f'components/spacebattles/assets/keys/f{n}.png') as image:
        assert image.size==(32,32)
        values=[(a<<24)|(r<<16)|(g<<8)|b for r,g,b,a in image.convert('RGBA').getdata()]
    palette=list(dict.fromkeys(values))
    assert len(palette)<=256
    palettes.append(palette)
    arrays.append([palette.index(v) for v in values])
text='// SPDX-License-Identifier: MIT\n// Exact indexed launcher icons. Copyright (c) 2025 Nicolai Electronics.\n// See assets/keys/LICENSE.\n'
text+='static const uint8_t key_pixels[6][1024]={\n'+''.join('{'+','.join(map(str,a))+'},\n' for a in arrays)+'};\n'
text+=f'static const pax_col_t key_palette[6][{max(map(len,palettes))}]={{\n'+''.join('{'+','.join(f'0x{v:08x}' for v in p)+'},\n' for p in palettes)+'};\n'
(root/'components/spacebattles/render/key_icons.h').write_text(text)
