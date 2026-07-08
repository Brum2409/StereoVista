#!/usr/bin/env python3
"""Generates spec-faithful synthetic SLPKs to exercise the parser paths a
public sample was not found for:
  * synthetic_16_object.slpk — v1.6 3DObject, per-node 3dNodeIndexDocument
    tree (root without obb -> MBS synthesis), NO hash index (CD fallback).
  * synthetic_pcsl20.slpk — PCSL 2.0 point cloud, store.index paging,
    implicit firstChild/childCount ranges, WITH a hash index written the way
    ArcGIS writes it (md5-of-stored-path, sorted, last entry before the CD).
Structures follow the Esri i3s-spec 1.6 / PCSL 2.0 docs.
"""
import gzip, hashlib, json, os, struct, sys, zipfile

OUT = sys.argv[1] if len(sys.argv) > 1 else "."

# A city block near Zurich (lon, lat), heights in meters.
LON, LAT = 8.5417, 47.3769


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


def obb(dx_m, dy_m, dz_m, half, quat=(0, 0, 0, 1)):
    # meters -> degrees around the anchor (approx; fine for synthetic data)
    import math
    mlat = 111132.95
    mlon = 111319.49 * math.cos(math.radians(LAT))
    return {
        "center": [LON + dx_m / mlon, LAT + dy_m / mlat, dz_m],
        "halfSize": list(half),
        "quaternion": list(quat),
    }


def mbs(dx_m, dy_m, dz_m, r):
    import math
    mlat = 111132.95
    mlon = 111319.49 * math.cos(math.radians(LAT))
    return [LON + dx_m / mlon, LAT + dy_m / mlat, dz_m, r]


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
            "textureEncoding": ["image/jpeg"],
            "lodType": "MeshPyramid",
            "defaultGeometrySchema": {
                "geometryType": "triangles",
                "topology": "PerAttributeArray",
                "ordering": ["position", "normal", "uv0", "color"],
                "vertexAttributes": {
                    "position": {"valueType": "Float32", "valuesPerElement": 3},
                    "normal": {"valueType": "Float32", "valuesPerElement": 3},
                    "uv0": {"valueType": "Float32", "valuesPerElement": 2},
                    "color": {"valueType": "UInt8", "valuesPerElement": 4},
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
            d["vertexCount"] = 1234
            d["featureCount"] = 7
        return d

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
        ("nodes/0-0/geometries/0.bin.gz", gz(b"\x00" * 64)),
        ("nodes/1/geometries/0.bin.gz", gz(b"\x00" * 64)),
    ]
    store_zip(os.path.join(OUT, "synthetic_16_object.slpk"), entries,
              with_hash_index=False)


# ---------------- PCSL 2.0 ----------------

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
            {"key": "1", "name": "ELEVATION"},
            {"key": "2", "name": "RGB",
             "attributeValues": {"valueType": "UInt8", "valuesPerElement": 3}},
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
            "lodThreshold": 10000.0 / (res + 1),
        })
    pnode(0, 1, 3, (400, 400, 100), 100000)
    pnode(1, 4, 2, (200, 200, 50), 60000)
    pnode(2, 6, 1, (200, 200, 50), 60000)
    pnode(3, 0, 0, (200, 200, 50), 50000)
    pnode(4, 0, 0, (100, 100, 25), 30000)
    pnode(5, 0, 0, (100, 100, 25), 30000)
    pnode(6, 0, 0, (100, 100, 25), 30000)

    entries = [
        ("3dSceneLayer.json.gz", jgz(layer)),
        ("nodepages/0.json.gz", jgz({"nodes": nodes[0:4]})),
        ("nodepages/1.json.gz", jgz({"nodes": nodes[4:7]})),
        ("nodes/0/geometries/0.bin.gz", gz(b"\x00" * 32)),
    ]
    store_zip(os.path.join(OUT, "synthetic_pcsl20.slpk"), entries,
              with_hash_index=True)


make16()
make_pcsl()
print("wrote synthetic_16_object.slpk + synthetic_pcsl20.slpk to", OUT)
