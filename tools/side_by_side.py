# side_by_side.py <rmshot.bmp> <out.png> [x0 y0 x1 y1 in original-picture pixels]
# The original picture (<rmshot>.orig.bmp, scaled up to match) left, our --rmshot right, optionally cropped.
import sys
from PIL import Image

ours = Image.open(sys.argv[1]).convert('RGB')
orig = Image.open(sys.argv[1] + '.orig.bmp').convert('RGB')
scale = ours.width // orig.width
orig = orig.resize(ours.size, Image.NEAREST)
if len(sys.argv) >= 7:
    box = tuple(int(v) * scale for v in sys.argv[3:7])
    ours, orig = ours.crop(box), orig.crop(box)
out = Image.new('RGB', (ours.width * 2 + 8, ours.height), (60, 0, 60))
out.paste(orig, (0, 0))
out.paste(ours, (ours.width + 8, 0))
out.save(sys.argv[2])
print(scale, out.size)
