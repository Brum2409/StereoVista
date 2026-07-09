# SLPK test data

Sample Scene Layer Packages for the SLPK/I3S support work
(`docs/SLPK_IMPLEMENTATION_PLAN.md`). The packages themselves are
**gitignored** — fetch or regenerate them into this directory as below,
then open them via **File → Open Scene Layer (.slpk)…** or by dropping
them onto the window.

## Real package (v1.7, 3D Object)

- **DA12_subset.slpk** (~0.6 MB, lower Manhattan buildings, WGS84,
  nodePages + `@specialIndexFileHASH128@` hash index — exercises the O(1)
  open fast path):

  ```
  curl -L -o DA12_subset.slpk https://raw.githubusercontent.com/visgl/loaders.gl/master/modules/i3s/test/data/DA12_subset.slpk
  ```

  (From the loaders.gl test suite, github.com/visgl/loaders.gl.)

## Real package (PCSL, Point Cloud)

- **SMALL_AUTZEN_LAS_All.slpk** (~10 KB, 106 points, WGS84, single leaf
  node, LEPCC-encoded xyz + intensity + raw class-code/flag columns +
  per-attribute statistics with class labels — a PCSL 2.0 store that uses
  the 1.x-style `.bin.pccxyz`/`.pccint` blob naming, so it exercises that
  probe and the lepcc decode path for milestone M3):

  ```
  curl -L -o SMALL_AUTZEN_LAS_All.slpk https://raw.githubusercontent.com/Esri/lepcc/master/testData/SMALL_AUTZEN_LAS_All.slpk
  ```

  (From the Esri/lepcc repository's own test data.)

## Synthetic packages (v1.6 mesh, v1.7 textured mesh, PCSL 2.0)

No small public v1.6 / textured / point-cloud `.slpk` could be found for
download, so those parser + decoder paths are covered by spec-faithful
generated packages:

```
python3 make_synthetic_slpk.py . [path-to-make_lepcc_blobs]
```

writes

- **synthetic_16_object.slpk** — v1.6 3DObject: `store.rootNode` +
  per-node `3dNodeIndexDocument.json.gz` tree, root bounds from MBS (no
  OBB), no hash index → exercises the central-directory fallback. Since M1
  it carries REAL raw geometry (header + PerAttributeArray streams with
  degree deltas, per-feature id/faceRange), per-node
  `shared/sharedResource.json` materials, and PNG textures (extensionless
  1.6 hrefs).
- **synthetic_17_textured.slpk** — v1.7 3DObject with a RAW-ONLY geometry
  buffer (offset-8 header) + glTF-style `materialDefinitions` + PNG
  `textureSetDefinitions`: covers the 1.7 legacy-buffer decode + texture +
  material path (DA12 covers draco but has no textures).
- **synthetic_pcsl20.slpk** — PCSL 2.0 point cloud: `store.index` paging
  (4 nodes/page → multi-page), implicit `firstChild`/`childCount` ranges,
  hash index written the ArcGIS way (md5 of stored path, last entry).
  With the optional second argument (M3) every one of the 7 nodes carries
  **REAL LEPCC xyz/rgb/intensity blobs** + a raw CLASS_CODE column +
  per-attribute `statistics/<key>.json.gz` (with class labels), and a
  `synthetic_pcsl20.expected.bin` sidecar records the storage-order point
  data the harness compares decodes against. Without the tool the package
  keeps a placeholder blob (parse tests only).

The lepcc encoder tool is the committed `make_lepcc_blobs.cpp`, built ad hoc
against the vendored lepcc sources (any C++17 compiler; it is a test utility,
NOT part of the app build / vcxproj):

```
g++ -O2 -std=c++17 -I ../headers/libs \
    make_lepcc_blobs.cpp ../headers/libs/lepcc/*.cpp -o make_lepcc_blobs
```

## KTX2 / Basis texture test data (M2)

The M2 texture paths (`I3STexture::decodeBasis` KTX2/basis → BC7, and the
ktx2-preferred-over-jpg selection in `loadNodeTexture`) are exercised
against encoder-produced files. Build the pinned basis_universal encoder
(the SAME tag the vendored transcoder is pinned to, plan §3):

```
git clone --depth 1 --branch v2_1_0r https://github.com/BinomialLLC/basis_universal
cd basis_universal && cmake -B build -DSSE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j        # encoder lands at bin/basisu
```

then generate the test files (PNGs with known mip-downsample expectations,
ETC1S+BasisLZ / UASTC+zstd KTX2, a legacy `.basis`, and
**synthetic_17_ktx2.slpk** — `synthetic_17_textured.slpk` rewritten to carry
BOTH png and ktx2 texture entries with png listed first in
`textureSetDefinitions`, so only a loader that correctly prefers compressed
formats lands on BC7):

```
python3 make_ktx2_testdata.py ../path/to/basis_universal/bin/basisu .
```

Everything it writes is gitignored, like the packages.

## Larger real packages (for the visual gates)

For the M0/M1 acceptance gates on Windows, additionally use any
ArcGIS-Pro-cooked `.slpk` you have, or download a public one — search
ArcGIS Online content (arcgis.com → Search → Content, filter "slpk" /
"scene layer package"; Esri publishes several city-scale integrated-mesh
and 3D-object samples as downloadable items). Multi-GB integrated-mesh
packages are the interesting stress case for open time and (from M2)
streaming.
