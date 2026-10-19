#!/usr/bin/env python3
"""Index the meshes and textures already available to Brastirion and pick one per generator slot.

index: scans the MO2 modlist (profiles/<profile>/modlist.txt order, first line wins), our
       libraries (--lib, above every mod) and Morrowind/Data Files (below every mod).
find:  ranks meshes for a slot and prints REUSE / RETEXTURE / GENERATE per the asset rule in
       the design journal ("Regola asset").
Archives come from fallback-archive= in the profile's openmw.cfg; loose files beat every archive, a later archive
beats an earlier one.
"""
import argparse, gzip, json, os, re, struct, sys

TEX_EXT = ('.dds', '.tga', '.bmp')
TEX_REF = re.compile(rb'[\w \-\\/.]{1,200}\.(?:dds|tga|bmp)', re.I)


def sources(root, profile, libs):
    out = [(os.path.basename(os.path.normpath(l)), 'lib', l) for l in libs]
    with open(os.path.join(root, 'profiles', profile, 'modlist.txt'), encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if line.startswith('+') and not line.endswith('_separator'):
                out.append((line[1:], 'mod', os.path.join(root, 'mods', line[1:])))
    out.append(('Data Files', 'vanilla', os.path.join(root, 'Morrowind', 'Data Files')))
    return out


def tex_key(path):
    p = path.lower().replace('\\', '/').lstrip('/')
    p = p[p.index('textures/') + 9:] if 'textures/' in p else p
    return os.path.splitext(p)[0]


def nif_textures(data):
    # NIF strings are a uint32 length followed by the characters: use it to find where the path starts
    out = set()
    for m in TEX_REF.finditer(data):
        start = next((s for s in range(max(m.start(), 4), m.end() - 4)
                      if struct.unpack_from('<I', data, s - 4)[0] == m.end() - s), m.start())
        out.add(tex_key(data[start:m.end()].decode('latin-1')))
    return sorted(out)


def tex_size(path, h=None):
    if h is None:
        with open(path, 'rb') as f:
            h = f.read(24)
    if h[:4] == b'DDS ' and len(h) >= 20:
        height, width = struct.unpack_from('<II', h, 12)
    elif path.lower().endswith('.tga') and len(h) >= 16:
        width, height = struct.unpack_from('<HH', h, 12)
    elif h[:2] == b'BM' and len(h) >= 24:
        width, height = struct.unpack_from('<ii', h, 18)
    else:
        return 0
    return max(abs(width), abs(height))


def bsa_files(path):
    # Morrowind BSA: header, (size, offset) per file, name offsets, names, hashes, then the data
    with open(path, 'rb') as f:
        data = f.read()
    _, hash_off, count = struct.unpack_from('<III', data, 0)
    names_at = 12 + count * 12
    base = 12 + hash_off + count * 8
    for i in range(count):
        size, off = struct.unpack_from('<II', data, 12 + i * 8)
        name_off = struct.unpack_from('<I', data, 12 + count * 8 + i * 4)[0]
        end = data.index(b'\0', names_at + name_off)
        yield data[names_at + name_off:end].decode('latin-1').lower().replace('\\', '/'), data[base + off:base + off + size]


def archives(root, profile, srcs):
    cfg = os.path.join(root, 'profiles', profile, 'openmw.cfg')
    if not os.path.isfile(cfg):
        return
    with open(cfg, encoding='utf-8-sig') as f:
        names = [l.split('=', 1)[1].strip() for l in f if l.startswith('fallback-archive=')]
    for name in reversed(names):
        path = next((os.path.join(b, name) for _, _, b in srcs if os.path.isfile(os.path.join(b, name))), None)
        if path:
            yield name, path


def index(args):
    old = load(args.out) if os.path.exists(args.out) else {'meshes': {}, 'textures': {}}
    meshes, textures = {}, {}
    srcs = sources(args.root, args.profile, args.lib)
    for name, tier, base in srcs:
        for kind in ('meshes', 'textures'):
            top = os.path.join(base, kind)
            for dirpath, _, files in os.walk(top):
                for fn in files:
                    full = os.path.join(dirpath, fn)
                    rel = os.path.relpath(full, base).replace('\\', '/').lower()
                    st = os.stat(full)
                    entry = {'src': name, 'tier': tier, 'size': st.st_size, 'mtime': int(st.st_mtime)}
                    if kind == 'meshes' and fn.lower().endswith('.nif') and rel not in meshes:
                        prev = old['meshes'].get(rel)
                        if prev and (prev['src'], prev['size'], prev.get('mtime')) == (name, st.st_size, entry['mtime']):
                            entry['tex'] = prev['tex']
                        else:
                            with open(full, 'rb') as f:
                                entry['tex'] = nif_textures(f.read())
                        meshes[rel] = entry
                    elif kind == 'textures' and fn.lower().endswith(TEX_EXT):
                        key = tex_key(rel)
                        # the engine tries .dds first for every texture path
                        if key not in textures or (fn.lower().endswith('.dds') and textures[key]['path'][-4:] != '.dds'
                                                   and textures[key]['src'] == name):
                            entry['path'], entry['res'] = rel, tex_size(full)
                            textures[key] = entry
    for name, path in archives(args.root, args.profile, srcs):
        for rel, blob in bsa_files(path):
            entry = {'src': name, 'tier': 'bsa', 'size': len(blob)}
            if rel.startswith('meshes/') and rel.endswith('.nif') and rel not in meshes:
                entry['tex'] = nif_textures(blob)
                meshes[rel] = entry
            elif rel.startswith('textures/') and rel.endswith(TEX_EXT) and tex_key(rel) not in textures:
                entry['path'], entry['res'] = rel, tex_size(rel, blob[:24])
                textures[tex_key(rel)] = entry
    with gzip.open(args.out, 'wt', encoding='utf-8') as f:
        json.dump({'meshes': meshes, 'textures': textures}, f)
    print(f'{len(meshes)} meshes, {len(textures)} textures -> {args.out}')


def load(path):
    with gzip.open(path, 'rt', encoding='utf-8') as f:
        return json.load(f)


def find(args):
    idx = load(args.index)
    textures, words = idx['textures'], [w.lower() for w in args.keywords]
    tier_bonus = {'lib': 2, 'mod': 1, 'vanilla': 0, 'bsa': 0}
    ranked = []
    for rel, m in idx['meshes'].items():
        hits = sum(w in rel for w in words)
        if not hits or any(w.lower() in rel for w in args.exclude):
            continue
        found = [textures[t] for t in m['tex'] if t in textures]
        res = min((t['res'] for t in found), default=0)
        normal = bool(found) and all(t + '_n' in textures for t in m['tex'] if t in textures)
        missing = len(m['tex']) - len(found)
        score = hits * 10 + (res >= 512) + (res >= 1024) + (res >= 2048) + normal + tier_bonus[m['tier']] - 2 * missing
        if args.max_kb and m['size'] > args.max_kb * 1024:
            score -= 3
        ranked.append((score, rel, m, res, normal, missing))
    ranked.sort(key=lambda r: -r[0])
    if not ranked:
        verdict = 'GENERATE: no mesh in modlist or libraries matches this slot'
    else:
        _, rel, m, res, normal, missing = ranked[0]
        need = 2048 if args.hero else 1024
        if missing:
            verdict = f'RETEXTURE {rel}: {missing} texture(s) missing'
        elif res >= need and (normal or not args.hero):
            verdict = f'REUSE {rel} ({m["src"]})'
        elif args.hero:
            verdict = f'GENERATE: best match {rel} is {res}px, a hero slot needs {need}px with a normal map'
        else:
            verdict = f'RETEXTURE {rel}: {res}px, needs {need}px'
    print(verdict)
    for score, rel, m, res, normal, missing in ranked[:args.top]:
        print(f'  {score:3d}  {rel}  [{m["src"]}]  {res}px  normal={"y" if normal else "n"}  {m["size"] // 1024}KB')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    i = sub.add_parser('index')
    i.add_argument('--root', required=True, help='MO2 modlist root, e.g. F:\\Modlists\\NerevarMoon')
    i.add_argument('--profile', default='OpenMW')
    i.add_argument('--lib', action='append', default=[], help='our asset library (data folder with meshes/, textures/)')
    i.add_argument('--out', default='asset_index.json.gz')
    f = sub.add_parser('find')
    f.add_argument('keywords', nargs='+', help='path words for the slot, e.g. chest or candle')
    f.add_argument('--index', default='asset_index.json.gz')
    f.add_argument('--exclude', nargs='*', default=[], help='path words to skip, e.g. _x lod')
    f.add_argument('--hero', action='store_true', help='close-up, lore-unique slot: needs 2048px and normal maps')
    f.add_argument('--max-kb', type=int, default=0, help='penalise meshes larger than this (clutter slots)')
    f.add_argument('--top', type=int, default=5)
    args = p.parse_args()
    index(args) if args.cmd == 'index' else find(args)


if __name__ == '__main__':
    sys.exit(main())
