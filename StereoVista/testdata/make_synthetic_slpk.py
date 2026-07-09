#!/usr/bin/env python3
"""Generates spec-faithful synthetic SLPKs to exercise the parser + decoder
paths a public sample was not found for:
  * synthetic_16_object.slpk — v1.6 3DObject, per-node 3dNodeIndexDocument
    tree (root without obb -> MBS synthesis), NO hash index (CD fallback),
    REAL raw geometry (header + PerAttributeArray streams, degree deltas),
    per-node sharedResource.json materials and PNG textures.
  * synthetic_17_textured.slpk — v1.7 3DObject with a RAW-ONLY geometry
    buffer (offset-8 header) + glTF-style materialDefinitions + PNG texture
    sets: covers the 1.7 legacy-buffer decode + texture + material path
    (DA12 covers the draco path but has no textures).
  * synthetic_pcsl20.slpk — PCSL 2.0 point cloud, store.index paging,
    implicit firstChild/childCount ranges, WITH a hash index written the way
    ArcGIS writes it (md5-of-stored-path, sorted, last entry). With the
    optional make_lepcc_blobs tool (second argument; see the .cpp next to
    this script) every node carries REAL LEPCC xyz/rgb/intensity blobs plus
    a raw CLASS_CODE column and per-attribute statistics, and a
    synthetic_pcsl20.expected.bin sidecar records the storage-order point
    data for the harness to compare decodes against. Without the tool the
    package keeps a placeholder geometry blob (parse tests only).
Structures follow the Esri i3s-spec 1.6 / 1.7 / PCSL 2.0 docs. Geometry is
validated by the gcc test harness (see testdata/README.md).

    python3 make_synthetic_slpk.py [outdir] [path-to-make_lepcc_blobs]
"""
import gzip, hashlib, json, math, os, random, struct, subprocess, sys
import tempfile, zipfile, zlib

OUT = sys.argv[1] if len(sys.argv) > 1 else "."
LEPCC_TOOL = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None

# A city block near Zurich (lon, lat), heights in meters.
LON, LAT = 8.5417, 47.3769
MLAT = 111132.95
MLON = 111319.49 * math.cos(math.radians(LAT))


def gz(data: bytes) -> bytes:
    return gzip.compress(data, 6)


def jgz(obj) -> bytes:
    return gz(json.dumps(obj).encode())


def store_zip(path, entries, with_hash_index=False):
    """entries: list of (name, bytes). STORE-only zip; optionally append the
    @specialIndexFileHASH128@ index as the final entry (v1.7+ convention)."""
    zf = zipfile.ZipFile(path, "w", zipfile.ZIP_STORED)
    offsets = {}
    for name, data in entries:
        zi = zipfile.ZipInfo(name)
        zf.writestr(zi, data)
    # header offsets are known after writing
    for zi in zf.infolist():
        offsets[zi.filename] = zi.header_offset
    if with_hash_index:
        recs = sorted(
            (hashlib.md5(name.encode()).digest(), off) for name, off in offsets.items()
        )
        blob = b"".join(md5 + struct.pack("<Q", off) for md5, off in recs)
        zf.writestr(zipfile.ZipInfo("@specialIndexFileHASH128@"), blob)
    zf.close()


def enu_quat(lon=LON, lat=LAT):
    """Quaternion [x,y,z,w] of the rotation whose columns are the local
    East/North/Up unit vectors in ECEF — an OBB with this quaternion is
    ENU-axis-aligned at (lon, lat). Real cookers (ArcGIS) write ECEF-frame
    quaternions exactly like this; identity would mean ECEF-axis-aligned."""
    lo, la = math.radians(lon), math.radians(lat)
    sl, cl = math.sin(lo), math.cos(lo)
    sp, cp = math.sin(la), math.cos(la)
    # column-major rows m[r][c]: columns = east, north, up (in ECEF)
    m = [[-sl, -sp * cl, cp * cl],
         [cl, -sp * sl, cp * sl],
         [0.0, cp, sp]]
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        w = 0.25 * s
        x = (m[2][1] - m[1][2]) / s
        y = (m[0][2] - m[2][0]) / s
        z = (m[1][0] - m[0][1]) / s
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        w = (m[2][1] - m[1][2]) / s
        x = 0.25 * s
        y = (m[0][1] + m[1][0]) / s
        z = (m[0][2] + m[2][0]) / s
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        w = (m[0][2] - m[2][0]) / s
        x = (m[0][1] + m[1][0]) / s
        y = 0.25 * s
        z = (m[1][2] + m[2][1]) / s
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        w = (m[1][0] - m[0][1]) / s
        x = (m[0][2] + m[2][0]) / s
        y = (m[1][2] + m[2][1]) / s
        z = 0.25 * s
    return (x, y, z, w)


def obb(dx_m, dy_m, dz_m, half, quat=None):
    # meters -> degrees around the anchor (approx; fine for synthetic data)
    return {
        "center": [LON + dx_m / MLON, LAT + dy_m / MLAT, dz_m],
        "halfSize": list(half),
        # geographic-layer quaternions live in the ECEF frame; default to
        # "ENU-axis-aligned here" like real cookers do
        "quaternion": list(quat if quat is not None else enu_quat()),
    }


def mbs(dx_m, dy_m, dz_m, r):
    return [LON + dx_m / MLON, LAT + dy_m / MLAT, dz_m, r]


# ---------------- tiny PNG writer (RGBA8, no deps) ----------------

def make_png(width, height, pixel_fn):
    def chunk(tag, payload):
        data = tag + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(
            ">I", zlib.crc32(data) & 0xFFFFFFFF)

    raw = b""
    for y in range(height):
        raw += b"\x00"  # filter: none
        for x in range(width):
            raw += bytes(pixel_fn(x, y))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def checker_png(size=64, a=(230, 60, 40, 255), b=(240, 235, 220, 255)):
    return make_png(size, size,
                    lambda x, y: a if ((x // 8 + y // 8) % 2) == 0 else b)


# ---------------- raw geometry builder ----------------
# A box as triangle soup: positions = (dLon deg, dLat deg, dz m) deltas from
# the node center, normals in node-local ENU, CCW winding seen from outside.

AXES = [(1, 0, 0), (0, 1, 0), (0, 0, 1)]


def box_soup(size_m=20.0, height_offset=0.0):
    """returns (positions_deg, normals, uvs) lists of 36 vertices each."""
    h = size_m * 0.5
    positions, normals, uvs = [], [], []
    for axis in range(3):
        for sign in (1.0, -1.0):
            n = [0.0, 0.0, 0.0]
            n[axis] = sign
            t = [0.0, 0.0, 0.0]
            b = [0.0, 0.0, 0.0]
            ta, ba = (axis + 1) % 3, (axis + 2) % 3
            if sign > 0:
                t[ta], b[ba] = 1.0, 1.0
            else:  # mirrored so cross(t, b) == n stays outward
                t[ba], b[ta] = 1.0, 1.0
            corners = []
            for (du, dv) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = [n[i] * h + t[i] * h * du + b[i] * h * dv for i in range(3)]
                corners.append(p)
            uvq = [(0, 0), (1, 0), (1, 1), (0, 1)]
            for tri in ((0, 1, 2), (0, 2, 3)):
                for ci in tri:
                    p = corners[ci]
                    positions.append((p[0] / MLON, p[1] / MLAT,
                                      p[2] + height_offset))
                    normals.append(tuple(n))
                    uvs.append(uvq[ci])
    return positions, normals, uvs


def raw_geometry_16(feature_id=1):
    """1.6 buffer: header(vertexCount, featureCount) + position + normal +
    uv0 + color streams + per-feature id/faceRange."""
    positions, normals, uvs = box_soup()
    vc = len(positions)
    buf = struct.pack("<II", vc, 1)
    for p in positions:
        buf += struct.pack("<3f", *p)
    for n in normals:
        buf += struct.pack("<3f", *n)
    for uv in uvs:
        buf += struct.pack("<2f", *uv)
    buf += bytes((200, 200, 210, 255)) * vc  # non-white: tests the detector
    buf += struct.pack("<Q", feature_id)
    buf += struct.pack("<II", 0, vc // 3 - 1)
    return buf


def raw_geometry_17(feature_id=77):
    """1.7 legacy buffer: offset-8 header + position/normal/uv0/color +
    featureId/faceRange (same shape DA12 uses)."""
    return raw_geometry_16(feature_id)


# ---------------- v1.6 3DObject ----------------

def make16():
    layer = {
        "id": 0,
        "name": "Synthetic16",
        "layerType": "3DObject",
        "version": "e8c41d1f-89ec-4a23-a284-095a2b734b70",
        "spatialReference": {"wkid": 4326, "latestWkid": 4326},
        "store": {
            "id": "s0",
            "profile": "meshpyramids",
            "version": "1.6",
            "rootNode": "./nodes/root",
            "extent": [LON - 0.01, LAT - 0.01, LON + 0.01, LAT + 0.01],
            "normalReferenceFrame": "east-north-up",
            "textureEncoding": ["image/png"],
            "lodType": "MeshPyramid",
            "defaultGeometrySchema": {
                "geometryType": "triangles",
                "topology": "PerAttributeArray",
                "header": [
                    {"property": "vertexCount", "type": "UInt32"},
                    {"property": "featureCount", "type": "UInt32"},
                ],
                "ordering": ["position", "normal", "uv0", "color"],
                "vertexAttributes": {
                    "position": {"valueType": "Float32", "valuesPerElement": 3},
                    "normal": {"valueType": "Float32", "valuesPerElement": 3},
                    "uv0": {"valueType": "Float32", "valuesPerElement": 2},
                    "color": {"valueType": "UInt8", "valuesPerElement": 4},
                },
                "featureAttributeOrder": ["id", "faceRange"],
                "featureAttributes": {
                    "id": {"valueType": "UInt64", "valuesPerElement": 1},
                    "faceRange": {"valueType": "UInt32", "valuesPerElement": 2},
                },
            },
        },
        "attributeStorageInfo": [
            {"key": "f_0", "name": "OBJECTID",
             "header": [{"property": "count", "valueType": "UInt32"}],
             "ordering": ["attributeValues"],
             "attributeValues": {"valueType": "Oid32", "valuesPerElement": 1}},
        ],
    }

    shared = {
        "materialDefinitions": {
            "Mat0": {
                "type": "standard",
                "params": {
                    "vertexColors": True,
                    "diffuse": [0.9, 0.9, 0.9],
                    "transparency": 0,
                    "cullFace": "none",
                },
            }
        },
        "textureDefinitions": {
            "tex0": {
                "encoding": ["image/png"],
                "wrap": ["none", "none"],
                "atlas": False,
                "uvSet": "uv0",
                "channels": ["rgba"],
                "images": [{"id": "i0", "size": 64, "href": ["../textures/0_0"]}],
            }
        },
    }

    def nodedoc(nid, level, parent, children, with_obb=True, quat=(0, 0, 0, 1)):
        d = {
            "id": nid,
            "level": level,
            "mbs": mbs(0, 0, 60, 300 / (level + 1)),
            "lodSelection": [
                {"metricType": "maxScreenThreshold", "maxError": 500 / (level + 1)},
                {"metricType": "screenSpaceRelative", "maxError": 0.5},
            ],
        }
        if with_obb:
            d["obb"] = obb(0, 0, 60, (200 / (level + 1), 150 / (level + 1), 60), quat)
        if parent:
            d["parentNode"] = {"id": parent, "href": "../" + parent}
        if children:
            d["children"] = [
                {"id": c, "href": "../" + c, "mbs": mbs(0, 0, 60, 150)}
                for c in children
            ]
        else:
            d["children"] = []
            d["geometryData"] = [{"href": "./geometries/0"}]
            d["textureData"] = [{"href": "./textures/0_0"}]
            d["featureData"] = [{"href": "./features/0"}]
            d["vertexCount"] = 36
            d["featureCount"] = 1
        return d

    png = checker_png()
    entries = [
        ("3dSceneLayer.json.gz", jgz(layer)),
        ("metadata.json", json.dumps({"folderPattern": "BASIC",
                                      "ArchiveCompressionType": "STORE",
                                      "ResourceCompressionType": "GZIP",
                                      "I3SVersion": "1.6"}).encode()),
        ("nodes/root/3dNodeIndexDocument.json.gz",
         # root deliberately without obb -> MBS-synthesis path
         jgz(nodedoc("root", 0, None, ["0", "1"], with_obb=False))),
        ("nodes/0/3dNodeIndexDocument.json.gz",
         jgz(nodedoc("0", 1, "root", ["0-0"]))),
        ("nodes/1/3dNodeIndexDocument.json.gz",
         jgz(nodedoc("1", 1, "root", None, quat=(0, 0, 0.3826834, 0.9238795)))),
        ("nodes/0-0/3dNodeIndexDocument.json.gz",
         jgz(nodedoc("0-0", 2, "0", None))),
        ("nodes/0-0/geometries/0.bin.gz", gz(raw_geometry_16(1))),
        ("nodes/1/geometries/0.bin.gz", gz(raw_geometry_16(2))),
        ("nodes/0-0/shared/sharedResource.json.gz", jgz(shared)),
        ("nodes/1/shared/sharedResource.json.gz", jgz(shared)),
        ("nodes/0-0/textures/0_0", png),  # 1.6 texture: raw, no extension
        ("nodes/1/textures/0_0", png),
    ]
    store_zip(os.path.join(OUT, "synthetic_16_object.slpk"), entries,
              with_hash_index=False)


# ---------------- v1.7 textured (raw-only buffer) ----------------

def make17_textured():
    layer = {
        "id": 0,
        "name": "Synthetic17Tex",
        "layerType": "3DObject",
        "spatialReference": {"wkid": 4326, "latestWkid": 4326},
        "store": {
            "id": "s0",
            "profile": "meshSceneLayer",
            "version": "1.7",
            "extent": [LON - 0.01, LAT - 0.01, LON + 0.01, LAT + 0.01],
            "normalReferenceFrame": "east-north-up",
        },
        "nodePages": {
            "nodesPerPage": 64,
            "lodSelectionMetricType": "maxScreenThresholdSQ",
        },
        "materialDefinitions": [
            {
                "doubleSided": True,
                "pbrMetallicRoughness": {
                    "baseColorFactor": [1, 1, 1, 1],
                    "baseColorTexture": {"textureSetDefinitionId": 0},
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.9,
                },
            }
        ],
        "textureSetDefinitions": [
            {"formats": [{"name": "0", "format": "png"}], "atlas": False}
        ],
        "geometryDefinitions": [
            {
                "geometryBuffers": [
                    {
                        "offset": 8,
                        "position": {"type": "Float32", "component": 3},
                        "normal": {"type": "Float32", "component": 3},
                        "uv0": {"type": "Float32", "component": 2},
                        "color": {"type": "UInt8", "component": 4},
                        "featureId": {"type": "UInt64", "component": 1,
                                      "binding": "per-feature"},
                        "faceRange": {"type": "UInt32", "component": 2,
                                      "binding": "per-feature"},
                    }
                ]
            }
        ],
    }

    # 3 nodes: root(coarse box) -> two children (smaller boxes, offset).
    nodes = [
        {
            "index": 0, "parentIndex": -1, "lodThreshold": 80000.0,
            "obb": obb(0, 0, 10, (40, 40, 12)),
            "children": [1, 2],
            "mesh": {
                "material": {"definition": 0, "resource": 0},
                "geometry": {"definition": 0, "resource": 0,
                             "vertexCount": 36, "featureCount": 1},
            },
        },
        {
            "index": 1, "parentIndex": 0, "lodThreshold": 0.0,
            "obb": obb(-15, 0, 10, (12, 12, 12)),
            "children": [],
            "mesh": {
                "material": {"definition": 0, "resource": 1},
                "geometry": {"definition": 0, "resource": 1,
                             "vertexCount": 36, "featureCount": 1},
            },
        },
        {
            "index": 2, "parentIndex": 0, "lodThreshold": 0.0,
            "obb": obb(15, 0, 10, (12, 12, 12)),
            "children": [],
            "mesh": {
                "material": {"definition": 0, "resource": 2},
                "geometry": {"definition": 0, "resource": 2,
                             "vertexCount": 36, "featureCount": 1},
            },
        },
    ]

    png = checker_png()
    entries = [
        ("3dSceneLayer.json.gz", jgz(layer)),
        ("nodepages/0.json.gz", jgz({"nodes": nodes})),
        ("nodes/0/geometries/0.bin.gz", gz(raw_geometry_17(10))),
        ("nodes/1/geometries/0.bin.gz", gz(raw_geometry_17(11))),
        ("nodes/2/geometries/0.bin.gz", gz(raw_geometry_17(12))),
        ("nodes/0/textures/0.png", png),  # jpg/png stored raw (spec)
        ("nodes/1/textures/0.png", png),
        ("nodes/2/textures/0.png", png),
    ]
    store_zip(os.path.join(OUT, "synthetic_17_textured.slpk"), entries,
              with_hash_index=True)


# ---------------- PCSL 2.0 ----------------

def lepcc_encode(xyz, rgb, intensity, class_codes):
    """Runs the make_lepcc_blobs tool: returns (xyzBlob, rgbBlob, intBlob,
    sortedXyz, sortedRgb, sortedIntensity, sortedClass) — the encoder resorts
    points, so every column and the expected sidecar follow storage order."""
    n = len(xyz) // 3
    with tempfile.TemporaryDirectory() as tmp:
        req = os.path.join(tmp, "in.bin")
        rsp = os.path.join(tmp, "out.bin")
        with open(req, "wb") as f:
            f.write(struct.pack("<I", n))
            f.write(struct.pack("<%dd" % len(xyz), *xyz))
            f.write(bytes(rgb))
            f.write(struct.pack("<%dH" % n, *intensity))
            f.write(bytes(class_codes))
        subprocess.run([LEPCC_TOOL, req, rsp], check=True)
        with open(rsp, "rb") as f:
            data = f.read()
    off = 4
    blobs = []
    for _ in range(3):
        size = struct.unpack_from("<I", data, off)[0]
        off += 4
        blobs.append(data[off:off + size])
        off += size
    sxyz = struct.unpack_from("<%dd" % (n * 3), data, off); off += n * 24
    srgb = data[off:off + n * 3]; off += n * 3
    sint = struct.unpack_from("<%dH" % n, data, off); off += n * 2
    scls = data[off:off + n]
    return blobs[0], blobs[1], blobs[2], sxyz, srgb, sint, scls


def stats_doc(name, values, labels=None):
    doc = {"attribute": name,
           "stats": {"min": float(min(values)), "max": float(max(values)),
                     "count": float(len(values))}}
    if labels:
        doc["labels"] = {"labels": [{"value": float(v), "label": s}
                                    for v, s in labels]}
    return doc


def make_pcsl():
    layer = {
        "id": 0,
        "name": "SyntheticPCSL",
        "layerType": "PointCloud",
        "spatialReference": {"wkid": 4326, "latestWkid": 4326},
        "store": {
            "id": "s0",
            "profile": "PointCloud",
            "version": "2.0",
            "extent": [LON - 0.01, LAT - 0.01, LON + 0.01, LAT + 0.01],
            "index": {
                "nodeVersion": 2,
                "boundingVolumeType": "obb",
                "nodesPerPage": 4,  # tiny -> exercises multi-page reading
                "lodSelectionMetricType": "density-threshold",
            },
            "defaultGeometrySchema": {
                "geometryType": "points",
                "topology": "PerAttributeArray",
                "encoding": "lepcc-xyz",
                "vertexAttributes": {
                    "position": {"valueType": "Float64", "valuesPerElement": 3}},
            },
        },
        "attributeStorageInfo": [
            {"key": "1", "name": "ELEVATION", "encoding": "embedded-elevation"},
            {"key": "2", "name": "RGB", "encoding": "lepcc-rgb",
             "attributeValues": {"valueType": "UInt8", "valuesPerElement": 3}},
            {"key": "4", "name": "INTENSITY", "encoding": "lepcc-intensity",
             "attributeValues": {"valueType": "UInt16", "valuesPerElement": 1}},
            {"key": "8", "name": "CLASS_CODE",
             "attributeValues": {"valueType": "UInt8", "valuesPerElement": 1}},
        ],
        "statisticsInfo": [
            {"key": "1", "name": "ELEVATION", "href": "./statistics/1"},
            {"key": "2", "name": "RGB", "href": "./statistics/2"},
            {"key": "4", "name": "INTENSITY", "href": "./statistics/4"},
            {"key": "8", "name": "CLASS_CODE", "href": "./statistics/8"},
        ],
    }
    # 7 nodes: root(0) -> 1,2,3 ; 1 -> 4,5 ; 2 -> 6   (firstChild/childCount)
    nodes = []
    def pnode(res, first, count, half, vtx):
        nodes.append({
            "resourceId": res,
            "firstChild": first,
            "childCount": count,
            "obb": obb(res * 15.0, 0, 50, half),
            "vertexCount": vtx,
            # spec note: effective 2D area; a top-down footprint is plausible
            "lodThreshold": 4.0 * half[0] * half[1],
        })
    # Real (small) point counts when encoding; the old inflated counts stay
    # for the placeholder package so parse expectations don't depend on the
    # tool being present.
    counts = [400, 300, 300, 250, 200, 200, 200] if LEPCC_TOOL else \
             [100000, 60000, 60000, 50000, 30000, 30000, 30000]
    pnode(0, 1, 3, (400, 400, 100), counts[0])
    pnode(1, 4, 2, (200, 200, 50), counts[1])
    pnode(2, 6, 1, (200, 200, 50), counts[2])
    pnode(3, 0, 0, (200, 200, 50), counts[3])
    pnode(4, 0, 0, (100, 100, 25), counts[4])
    pnode(5, 0, 0, (100, 100, 25), counts[5])
    pnode(6, 0, 0, (100, 100, 25), counts[6])

    entries = [
        ("3dSceneLayer.json.gz", jgz(layer)),
        ("nodepages/0.json.gz", jgz({"nodes": nodes[0:4]})),
        ("nodepages/1.json.gz", jgz({"nodes": nodes[4:7]})),
    ]

    if not LEPCC_TOOL:
        entries.append(("nodes/0/geometries/0.bin.gz", gz(b"\x00" * 32)))
        store_zip(os.path.join(OUT, "synthetic_pcsl20.slpk"), entries,
                  with_hash_index=True)
        return

    # Real LEPCC blobs per node (deterministic points inside 0.9x the OBB;
    # the OBBs are ENU-axis-aligned at the anchor, so the linear deg<->m
    # approximation keeps everything comfortably inside). Storage naming
    # covers the 2.0 style: gzip-transparent .bin.gz for EVERY column (the
    # real SMALL_AUTZEN package covers the 1.x .pccxyz/.pccint tagging).
    rng = random.Random(0x5EED)
    sidecar = [struct.pack("<I", len(nodes))]
    all_int, all_cls = [], []
    class_values = (1, 2, 3, 6)
    for node in nodes:
        n = node["vertexCount"]
        cx, cy, cz = node["obb"]["center"]
        hx, hy, hz = node["obb"]["halfSize"]
        xyz, rgb, intensity, cls = [], [], [], []
        for i in range(n):
            xyz += [cx + rng.uniform(-0.9, 0.9) * hx / MLON,
                    cy + rng.uniform(-0.9, 0.9) * hy / MLAT,
                    cz + rng.uniform(-0.9, 0.9) * hz]
            rgb += [(i * 7) % 256, (i * 13) % 256, (i * 29) % 256]
            intensity.append((i * 37) % 4096)
            cls.append(class_values[i % len(class_values)])
        xyzB, rgbB, intB, sxyz, srgb, sint, scls = \
            lepcc_encode(xyz, rgb, intensity, cls)
        res = node["resourceId"]
        entries.append((f"nodes/{res}/geometries/0.bin.gz", gz(xyzB)))
        entries.append((f"nodes/{res}/attributes/2.bin.gz", gz(rgbB)))
        entries.append((f"nodes/{res}/attributes/4.bin.gz", gz(intB)))
        entries.append((f"nodes/{res}/attributes/8.bin.gz", gz(bytes(scls))))
        all_int += sint
        all_cls += scls
        sidecar.append(struct.pack("<II", res, n))
        sidecar.append(struct.pack("<%dd" % (n * 3), *sxyz))
        sidecar.append(bytes(srgb))
        sidecar.append(struct.pack("<%dH" % n, *sint))
        sidecar.append(bytes(scls))

    entries.append(("statistics/1.json.gz", jgz(stats_doc("ELEVATION",
                    [0.0, 150.0]))))
    entries.append(("statistics/2.json.gz", jgz(stats_doc("RGB", [0, 255]))))
    entries.append(("statistics/4.json.gz", jgz(stats_doc("INTENSITY",
                    all_int))))
    entries.append(("statistics/8.json.gz", jgz(stats_doc("CLASS_CODE",
                    all_cls, labels=[(1, "Unclassified"), (2, "Ground"),
                                     (3, "Low Vegetation"), (6, "Building")]))))

    store_zip(os.path.join(OUT, "synthetic_pcsl20.slpk"), entries,
              with_hash_index=True)
    with open(os.path.join(OUT, "synthetic_pcsl20.expected.bin"), "wb") as f:
        f.write(b"".join(sidecar))


make16()
make17_textured()
make_pcsl()
print("wrote synthetic_16_object.slpk + synthetic_17_textured.slpk + "
      "synthetic_pcsl20.slpk to", OUT)
