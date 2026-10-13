# Collision layers (`.cl`) and overlays (`.co`)

`assets/data/collision_layer_*.cl` say where people can walk and see.
`assets/data/helicopter.co` is the landed helicopter's stamp. The data step
copies both unchanged (`tools/prepare_data.py`, `collision`), because the
port reads them the way the game does. `tools/collision.py` reads them and
draws a layer over its map.

## The layer file

A layer is one zlib stream (zlib 1.2.8 in the `.so`). Inflated, it is a
`width/4` by `height/4` grid with one byte per 4x4 cell, row after row from
the top left.

- `CollisionLayer::CollisionLayer(MapType, id, width, height, cache)`
  (0x0019b164) sets `this+0 = width >> 2` and `this+4 = height >> 2`. It
  loads the file named for the map type:
  - type 1: `data/collision_layer_%d.cl`, by id
  - type 2: `data/collision_layer_map%02d.cl`
  - type 3: a player map, from `PlayerMapCache`, which the port drops
- `Utils::decompressData` (0x00228648) inflates with `Z_FINISH` into
  exactly `w*h` bytes. It throws unless the stream ends and the buffer is
  full.
- Then `this+0x14`, the version, is set to 1.

`MapConfigFactory`'s constructor (0x001a673c) gives the sizes:

| Maps | Type | Size (units) | Grid |
|---|---|---|---|
| `map01`..`map05` | 2 (offline, tiles in the APK) | 4096 square | 1024 square |
| ids 17, 752, 13021, 14016, 66032 | 1 (featured, Google tiles) | 2000 square | 500 square |
| ids 11171, 15961, 56601, 79478, 79818, 85014, 94544, 111850, 114009, 115885 | 1 | 4000 square | 1000 square |

All 20 files in the APK inflate to exactly these sizes.

The offline maps' tiles are zoom 16..20, so zoom 20's pixels are map units
and zoom 18's pixels are cells. The type 1 maps have no imagery in the APK:
the game streamed them from Google Maps.

## Values

`CollisionLayer::getValue(x, y)` (0x0019b3ea) is `data[(y>>2)*w + (x>>2)]`,
with x and y in map units. The bits, as the code tests them:

| Bit | Meaning | Where |
|---|---|---|
| 0x01 | walkable | `Simulation::canWalk`; the start and wander points |
| 0x02 | sight passes | `CollisionLayer::isVisibleFrom`'s line walk |
| 0x04 | overhead cover: no parachute drop | `Simulation::dropSoldier` wants `(v & 5) == 1` |
| 0x18 | both set: indoors | `Simulation::isBuilding` |

`CollisionLayerRenderer::createTexture` (0x0019ba2c) allows nine values and
throws `Invalid collision value: ` on anything else. The table gives what
each one covers on the maps (`tools/collision.py`'s overlay), with the debug
colour the renderer gives it:

| Value | Bits | On the maps | Debug colour |
|---|---|---|---|
| 0x00 | none | walls, fences | `ff0000` |
| 0x03 | walk, sight | open ground, roads | `808080` |
| 0x07 | walk, sight, cover | trees | `008000` |
| 0x0f | walk, sight, cover, 0x08 | doorways | `0040ff` |
| 0x16 | sight, cover, 0x10 | low solid objects; the helicopter's hull | `ffffff` |
| 0x1f | walk, sight, cover, 0x08, 0x10 | inside buildings | `d06000` |
| 0x22 | sight, 0x20 | (type 1 maps only) | `800000` |
| 0x42 | sight, 0x40 | parked vehicles | `ffc000` |
| 0x82 | sight, 0x80 | water | `4090ff` |

`Simulation::explosionShouldLeaveMark` is false on 0x82, so a bomb in the
water leaves no scorch. `Simulation::getValidStartingPoints` skips 0x1f
unless it is asked for indoor points. `getClosestGroundPosition` looks for
0x03.

The renderer draws each colour scaled by 3/8 with alpha 0x60. That is a
premultiplied colour at 96/255, and `tools/collision.py` blends it the same
way.

## Quirks kept

- `applyOverlay` and `removeOverlay` check an overlay's column against the
  layer's *height*, not its width. `explosionShouldLeaveMark` steps rows by
  the height. Every map is square, so neither shows; the port keeps both.
- `isVisibleFrom(x0, y0, x1, y1)` takes its start in cells and its end in
  map units (it shifts only the end).

## Overlays (`.co`)

`CollisionOverlay(path, w, h)` (0x0019bd38) reads the file raw. It must be
exactly `w*h` bytes. `GameNode` loads `data/helicopter.co` as 40x10: the
landed helicopter, as 0x16 hull, 0x0f doors and 0x1f cabin. Zero means
"leave the cell alone".
- `CollisionLayer::canApplyOverlay(o, x, y, v)` checks that the overlay
  fits, centred on cell (x, y), and that every cell under a non-zero byte
  is `v`.
- `applyOverlay` writes the non-zero bytes.
- `removeOverlay` writes `v` back.
- Applying and removing each bump the version, which tells the debug
  renderer to rebuild its texture.
