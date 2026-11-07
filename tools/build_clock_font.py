#!/usr/bin/env python3
"""Rasterize the bundled DejaVu Sans digits (license in assets/fonts)."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[1]
font=ImageFont.truetype(str(root/'assets/fonts/DejaVuSans.ttf'),108)
chars='0123456789:'
w,h=38,78
lines=['#pragma once','#include <Arduino.h>','// DejaVu Sans digit rasterization; see assets/fonts/LICENSE_DEJAVU.',f'constexpr int CLOCK_GLYPH_W={w},CLOCK_GLYPH_H={h};']
for n,ch in enumerate(chars):
 box=font.getbbox(ch);im=Image.new('L',(box[2]-box[0],box[3]-box[1]));ImageDraw.Draw(im).text((-box[0],-box[1]),ch,font=font,fill=255)
 if ch!=':':im=im.resize((w,h),Image.Resampling.LANCZOS)
 else:im=im.resize((10,43),Image.Resampling.LANCZOS)
 cell=Image.new('L',(w,h));cell.paste(im,((w-im.width)//2,(h-im.height)//2))
 pix=[round(v/17) for v in cell.getdata()];data=[(pix[i]<<4)|pix[i+1] for i in range(0,len(pix),2)]
 lines.append(f'static const uint8_t CLOCK_GLYPH_{n}[] PROGMEM={{'+','.join(map(str,data))+'};')
lines.append('static const uint8_t* const CLOCK_GLYPHS[]={'+','.join(f'CLOCK_GLYPH_{n}' for n in range(11))+'};')
(root/'include/clock_font.h').write_text('\n'.join(lines)+'\n')
print('Clock: 78px high, antialiased tabular digits')
