import gzip, struct
from PIL import Image, ImageFilter, ImageDraw

art = Image.open('/home/brendan/workspaces/kindle-grid-game/boot.png')
blurred = art.convert('L').filter(ImageFilter.GaussianBlur(15))

sizes = [(300,400),(320,427),(360,480),(400,533),(450,600),(480,640)]
previews = []
for aw, ah in sizes:
    im = blurred.resize((aw, ah), Image.LANCZOS)
    px = im.load()
    levels = [0]*(600*800)
    x0, y0 = (600-aw)//2, (800-ah)//2
    for y in range(ah):
        for x in range(aw):
            levels[(y0+y)*600 + (x0+x)] = 3 if px[x, y] >= 128 else 0
    img = Image.new('L', (600,800), 0)
    img.putdata([v*85 for v in levels])
    previews.append(img)

# contact sheet 3 rows x 2 cols
sheet = Image.new('L', (1200+40, 800*3+40), 255)
d = ImageDraw.Draw(sheet)
for i, (img, (aw,ah)) in enumerate(zip(previews, sizes)):
    r, c = divmod(i, 2)
    x = c*620+10; y = r*820+10
    sheet.paste(img, (x,y))
    d.text((x, y+802), f'{aw}x{ah}', fill=0)
sheet.save('/home/brendan/workspaces/kindle-grid-game/scratch/small_sizes.png')
print('saved scratch/small_sizes.png')