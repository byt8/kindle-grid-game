import zipfile, sys
z = zipfile.ZipFile(sys.argv[1])
names = z.namelist()
print("total entries:", len(names))
syms = [n for n in names if (z.getinfo(n).external_attr >> 16) & 0o170000 == 0o120000]
print("symlinks:", syms[:20], "count", len(syms))
top = sorted(set(n.split('/')[0] for n in names if '/' in n))
print("top dirs:", top)
for n in names:
    if n.startswith('koreader/koreader.sh') or n.startswith('extensions/koreader'):
        print("  ", n)
