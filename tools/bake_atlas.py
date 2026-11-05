"""Paletten-Textur eines glTF-Modells (.glb) in einfache Farbmaterialien umrechnen.

Viele Low-Poly-Modelle (z. B. von Quaternius) färben sich über eine kleine Farbtafel ("Atlas.png"): jedes Dreieck liegt
mit seinen Texturkoordinaten auf einem einfarbigen Feld. Das Spiel liest nur Materialfarben (keine Texturen). Dieses
Skript nimmt für jedes Dreieck die Farbe unter seinen Texturkoordinaten, fasst gleiche Farben zusammen und schreibt
für jede Farbe ein eigenes Material (baseColorFactor, linear). Textur, Bild und Sampler fallen weg.

    python tools/bake_atlas.py ein.glb aus.glb

Nur die Standardbibliothek (PNG wird selbst gelesen: 8 Bit, RGB/RGBA/Grau, nicht verschränkt).
"""
import json
import struct
import sys
import zlib


def read_png(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    at, idat, w, h, ctype = 8, b"", 0, 0, 0
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace:
                raise ValueError("only 8-bit, non-interlaced PNGs")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        at += 12 + n
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line

    def pixel(x, y):
        r = rows[y]
        if bpp >= 3:
            return r[x * bpp], r[x * bpp + 1], r[x * bpp + 2]
        return r[x * bpp], r[x * bpp], r[x * bpp]

    return w, h, pixel


def to_linear(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def main(src, dst):
    data = open(src, "rb").read()
    jlen = struct.unpack("<I", data[12:16])[0]
    doc = json.loads(data[20:20 + jlen])
    at = 20 + ((jlen + 3) & ~3)
    blen = struct.unpack("<I", data[at:at + 4])[0]
    binbuf = bytearray(data[at + 8:at + 8 + blen])

    def view_bytes(bv):
        v = doc["bufferViews"][bv]
        o = v.get("byteOffset", 0)
        return bytes(binbuf[o:o + v["byteLength"]])

    def read(acc_index):
        a = doc["accessors"][acc_index]
        v = doc["bufferViews"][a["bufferView"]]
        comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        fmt = {5126: "f", 5125: "I", 5123: "H", 5121: "B"}[a["componentType"]]
        size = struct.calcsize(fmt)
        stride = v.get("byteStride", comps * size)
        base = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        out = []
        for i in range(a["count"]):
            vals = struct.unpack_from("<" + fmt * comps, binbuf, base + i * stride)
            out.append(vals if comps > 1 else vals[0])
        return out

    images = {}
    new_mats = []
    color_index = {}

    def material_for(rgb):
        key = tuple(round(c, 3) for c in rgb)
        for k, idx in color_index.items():  # fast gleiche Farben zusammenlegen
            if max(abs(a - b) for a, b in zip(k, key)) < 0.02:
                return idx
        idx = len(new_mats)
        color_index[key] = idx
        new_mats.append({"name": "Color_%02d" % idx,
                         "pbrMetallicRoughness": {"baseColorFactor": [key[0], key[1], key[2], 1.0],
                                                  "metallicFactor": 0.0, "roughnessFactor": 1.0}})
        return idx

    for mesh in doc["meshes"]:
        prims = []
        for prim in mesh["primitives"]:
            mat = doc["materials"][prim["material"]] if "material" in prim else None
            tex = mat and mat.get("pbrMetallicRoughness", {}).get("baseColorTexture")
            if not tex or "TEXCOORD_0" not in prim["attributes"]:
                if mat is not None:
                    pbr = mat.get("pbrMetallicRoughness", {})
                    prim["material"] = material_for(pbr.get("baseColorFactor", [1, 1, 1, 1])[:3])
                prims.append(prim)
                continue
            img_index = doc["textures"][tex["index"]]["source"]
            if img_index not in images:
                images[img_index] = read_png(view_bytes(doc["images"][img_index]["bufferView"]))
            w, h, pixel = images[img_index]
            factor = mat["pbrMetallicRoughness"].get("baseColorFactor", [1, 1, 1, 1])
            uv = read(prim["attributes"]["TEXCOORD_0"])
            idx = read(prim["indices"]) if "indices" in prim else list(range(len(uv)))
            groups = {}
            for t in range(0, len(idx) - 2, 3):
                tri = idx[t:t + 3]
                # Mitte des Dreiecks auf der Farbtafel (die Felder sind einfarbig)
                u = sum(uv[k][0] for k in tri) / 3.0
                v = sum(uv[k][1] for k in tri) / 3.0
                x = min(w - 1, max(0, int((u % 1.0) * w)))
                y = min(h - 1, max(0, int((v % 1.0) * h)))
                r, g, b = pixel(x, y)
                rgb = (to_linear(r) * factor[0], to_linear(g) * factor[1], to_linear(b) * factor[2])
                groups.setdefault(material_for(rgb), []).extend(tri)
            for m, tris in sorted(groups.items()):
                payload = struct.pack("<%dI" % len(tris), *tris)
                while len(binbuf) % 4:
                    binbuf.append(0)
                doc["bufferViews"].append({"buffer": 0, "byteOffset": len(binbuf), "byteLength": len(payload),
                                           "target": 34963})
                binbuf.extend(payload)
                doc["accessors"].append({"bufferView": len(doc["bufferViews"]) - 1, "componentType": 5125,
                                         "count": len(tris), "type": "SCALAR"})
                p = {k: v for k, v in prim.items() if k not in ("indices", "material")}
                p["attributes"] = dict(prim["attributes"])
                p["indices"] = len(doc["accessors"]) - 1
                p["material"] = m
                prims.append(p)
        mesh["primitives"] = prims

    doc["materials"] = new_mats
    for key in ("textures", "images", "samplers"):
        doc.pop(key, None)
    doc["buffers"][0]["byteLength"] = len(binbuf)

    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    while len(binbuf) % 4:
        binbuf.append(0)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binbuf))
    out += struct.pack("<I4s", len(js), b"JSON") + js
    out += struct.pack("<I4s", len(binbuf), b"BIN\x00") + bytes(binbuf)
    open(dst, "wb").write(out)
    print("%s: %d colors" % (dst, len(new_mats)))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
