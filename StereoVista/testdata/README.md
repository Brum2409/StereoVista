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

## Synthetic packages (v1.6 mesh + PCSL 2.0)

No small public v1.6 or point-cloud `.slpk` could be found for download, so
those parser paths are covered by spec-faithful generated packages:

```
python3 make_synthetic_slpk.py .
```

writes

- **synthetic_16_object.slpk** — v1.6 3DObject: `store.rootNode` +
  per-node `3dNodeIndexDocument.json.gz` tree, root bounds from MBS (no
  OBB), no hash index → exercises the central-directory fallback.
- **synthetic_pcsl20.slpk** — PCSL 2.0 point cloud: `store.index` paging
  (4 nodes/page → multi-page), implicit `firstChild`/`childCount` ranges,
  hash index written the ArcGIS way (md5 of stored path, last entry).

## Larger real packages (for the visual gates)

For the M0/M1 acceptance gates on Windows, additionally use any
ArcGIS-Pro-cooked `.slpk` you have, or download a public one — search
ArcGIS Online content (arcgis.com → Search → Content, filter "slpk" /
"scene layer package"; Esri publishes several city-scale integrated-mesh
and 3D-object samples as downloadable items). Multi-GB integrated-mesh
packages are the interesting stress case for open time and (from M2)
streaming.
