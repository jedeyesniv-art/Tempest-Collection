"""Contact sheet: a grid of BMP/PNG shots, each labelled with its file name.

python -I tools/contact_sheet.py out.png cols scale shot1.bmp shot2.bmp ...
"""
import os
import sys

from PIL import Image, ImageDraw


def main():
    out, cols, scale = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
    files = sys.argv[4:]
    ims = [Image.open(f).convert('RGB') for f in files]
    w, h = int(ims[0].width * scale), int(ims[0].height * scale)
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w, rows * h), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, (im, f) in enumerate(zip(ims, files)):
        x, y = (i % cols) * w, (i // cols) * h
        sheet.paste(im.resize((w, h)), (x, y))
        d.text((x + 4, y + 2), os.path.splitext(os.path.basename(f))[0], fill=(255, 255, 0))
    sheet.save(out)


if __name__ == '__main__':
    main()
