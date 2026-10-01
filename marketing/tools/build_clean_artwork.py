"""Compose simple, light store artwork from inspected real captures with Pillow.

The JSON config and full source/build/crop audit belong outside the repository.
This tool does not capture an app, create interface elements or publish files.
It accepts only explicitly supplied scenes and refuses existing output folders.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
from PIL import Image, ImageDraw, ImageFont, __version__ as PILLOW_VERSION

WHITE, INK, MUTED, LINE, BLUE = '#ffffff', '#20252a', '#68727c', '#dce1e5', '#2875b4'
sha = lambda data: hashlib.sha256(data).hexdigest()


def typefaces(normal, bold):
    directory = Path(os.environ.get('WINDIR', 'C:/Windows'))/'Fonts'
    normal, bold = normal or directory/'segoeui.ttf', bold or directory/'segoeuib.ttf'
    if not normal.is_file() or not bold.is_file():
        raise ValueError('Supply --font and --font-bold when Segoe UI is unavailable')
    return normal, bold


def label(draw, xy, value, font, color=INK, anchor='lt', width=None):
    if width is not None and draw.textbbox((0,0), value, font=font)[2] > width:
        raise ValueError(f'Label exceeds its reserved area: {value}')
    draw.text(xy, value, font=font, fill=color, anchor=anchor)


def source(record):
    path = Path(record['path'])
    content = path.read_bytes()
    expected = record['sha256']
    if not re.fullmatch('[0-9a-fA-F]{64}', expected) or sha(content) != expected.lower():
        raise ValueError('Source hash differs from the inspected image')
    if record['kind'] not in ('application-capture', 'native-score-export'):
        raise ValueError('Only real app captures or explicitly identified native score exports are allowed')
    if not record.get('build') or not record.get('reviewed'):
        raise ValueError('Each source requires a build and an explicit prior inspection record')
    with Image.open(io.BytesIO(content)) as image:
        if image.format not in ('PNG', 'JPEG', 'WEBP') or getattr(image,'n_frames',1) != 1:
            raise ValueError('Supply a single-frame original PNG, JPEG or WebP capture')
        detected_format = image.format
        image.load()
        if image.width*image.height > 32_000_000:
            raise ValueError('Source exceeds 32 million pixels')
        if image.convert('RGBA').getchannel('A').getextrema() != (255,255):
            raise ValueError('Real source images must be opaque')
        copied = image.convert('RGB')
    rectangle = tuple(record.get('crop',(0,0,*copied.size)))
    if (len(rectangle)!=4 or any(type(n) is not int for n in rectangle)
        or rectangle[0]<0 or rectangle[1]<0 or rectangle[2]>copied.width or rectangle[3]>copied.height
        or rectangle[2]-rectangle[0]<160 or rectangle[3]-rectangle[1]<160):
        raise ValueError('Crop must be an in-bounds rectangle of at least 160 by 160 source pixels')
    return copied,rectangle,dict(path=str(path.resolve()),sha256=expected.lower(),detected_format=detected_format,kind=record['kind'],
                                 build=record['build'],executable_sha256=record.get('executable_sha256'),
                                 original_size=list(copied.size),crop=list(rectangle),reviewed=record['reviewed'])


def place(canvas, image, rectangle, available):
    cropped = image.crop(rectangle)
    left,top,right,bottom=available
    scale=min((right-left)/cropped.width,(bottom-top)/cropped.height,1.0)
    size=(max(1,int(cropped.width*scale)),max(1,int(cropped.height*scale)))
    if size != cropped.size:
        cropped=cropped.resize(size,Image.Resampling.LANCZOS)
    position=(left+((right-left)-size[0])//2,top+((bottom-top)-size[1])//2)
    canvas.paste(cropped,position)
    draw=ImageDraw.Draw(canvas)
    draw.rectangle((position[0]-1,position[1]-1,position[0]+size[0],position[1]+size[1]),outline=LINE,width=1)
    return dict(available=list(available),placed=[*position,position[0]+size[0],position[1]+size[1]],
                rendered_size=list(size),scale=scale,resampling='Lanczos' if scale<1 else 'none',
                fit='contain; no stretching or enlargement; no unrecorded crop')


def compose(record, normal, bold, cover=False):
    image,crop,audit=source(record)
    size=(630,500) if cover else (1600,1000)
    canvas=Image.new('RGB',size,WHITE)
    draw=ImageDraw.Draw(canvas)
    regular=lambda pixels:ImageFont.truetype(str(normal),pixels)
    heavy=lambda pixels:ImageFont.truetype(str(bold),pixels)
    if cover:
        label(draw,(28,26),'MillerScore',heavy(40),width=574)
        label(draw,(30,80),'Notation + built-in DAW',regular(21),MUTED,width=570)
        draw.line((30,117,600,117),fill=LINE,width=1)
        geometry=place(canvas,image,crop,(29,140,601,435))
        label(draw,(30,467),'Early Access',regular(16),BLUE,width=270)
        label(draw,(600,467),'Windows x64',regular(16),MUTED,anchor='rt',width=270)
    else:
        label(draw,(40,29),record['title'],heavy(36),width=1150)
        label(draw,(1560,35),'MillerScore',heavy(25),BLUE,anchor='rt',width=350)
        draw.line((40,92,1560,92),fill=LINE,width=1)
        geometry=place(canvas,image,crop,(39,116,1561,934))
        label(draw,(40,969),'Early Access',regular(17),MUTED,width=600)
        context='Native score export' if record['kind']=='native-score-export' else 'Actual application capture'
        label(draw,(1560,969),context,regular(17),MUTED,anchor='rt',width=800)
    audit['composition']=geometry
    audit['output_dimensions']=list(size)
    return canvas,audit


def png_bytes(image):
    stream=io.BytesIO()
    image.save(stream,format='PNG',compress_level=9,optimize=False)
    return stream.getvalue()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,required=True,help='Private inspected-source JSON')
    parser.add_argument('--output',type=Path,required=True,help='New artwork directory')
    parser.add_argument('--audit',type=Path,required=True,help='Private full manifest outside this repository')
    parser.add_argument('--font',type=Path)
    parser.add_argument('--font-bold',type=Path)
    options=parser.parse_args()
    repo=Path(__file__).resolve().parents[2]
    if options.audit.resolve().is_relative_to(repo) or options.config.resolve().is_relative_to(repo):
        raise ValueError('Source config and full audit must remain outside the public repository')
    if options.output.exists() or options.output.is_symlink():
        raise ValueError('Output already exists; select a new directory to preserve previous artwork')
    config=json.loads(options.config.read_text(encoding='utf-8'))
    scenes=config['scenes']
    if not 1 <= len(scenes) <= 5:
        raise ValueError('Provide one to five real scenes; unavailable scenes must not become placeholders')
    normal,bold=typefaces(options.font,options.font_bold)
    renders=[]
    specifications=[('cover-630x500.png',config['cover'],True)]
    for index,record in enumerate(scenes,1):
        slug=record['slug']
        if not re.fullmatch('[a-z0-9]+(?:-[a-z0-9]+)*',slug):
            raise ValueError('Scene slug must be simple English lowercase words')
        specifications.append((f'{index:02d}-{slug}-1600x1000.png',record,False))
    for name,record,cover in specifications:
        image,audit=compose(record,normal,bold,cover)
        content=png_bytes(image)
        repeated,_=compose(record,normal,bold,cover)
        if content!=png_bytes(repeated):
            raise ValueError('Repeated composition was not deterministic')
        audit.update(output=name,sha256_output=sha(content),bytes=len(content),deterministic_repeat=True)
        renders.append((name,content,audit))
    options.output.mkdir(parents=True,exist_ok=False)
    for name,content,audit in renders:
        (options.output/name).write_bytes(content)
    full=dict(style='Plain light surfaces, real source pixels, clean typography; no fictional UI, glow, gradients, illustration or imagegen',
              config_path=str(options.config.resolve()),config_sha256=sha(options.config.read_bytes()),
              generator_sha256=sha(Path(__file__).read_bytes()),pillow_version=PILLOW_VERSION,
              font_sha256=sha(normal.read_bytes()),font_bold_sha256=sha(bold.read_bytes()),
              output_directory=str(options.output.resolve()),artifacts=[audit for _,_,audit in renders],
              unrepresented_scenes=config.get('unrepresented_scenes',[]),scope='Local files only; no publication or app capture; unavailable scenes omitted')
    options.audit.parent.mkdir(parents=True,exist_ok=True)
    options.audit.write_text(json.dumps(full,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps({'files':[name for name,_,_ in renders],'private_audit':str(options.audit)},indent=2))


if __name__=='__main__':
    main()
