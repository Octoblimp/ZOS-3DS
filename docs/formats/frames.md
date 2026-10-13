# Atlas frame tables (`.frames`)

The GUI atlases come with cocos2d `.plist` files (format 2). The `gui`
step of `tools/prepare_data.py` writes each one as a `.frames` text file
beside its `.t3x`, so the 3DS needs no XML reader.

```
# GUI.png 1024 1024
ActionOverlay_ButtonBackground.png 158 888 140 96 0 0 140 96 0
Button_Cancel.png 444 150 112 48 0 0 112 48 0
```

The first line is `# <textureFileName> <width> <height>` from the plist's
`metadata`. Then there is one frame per line, separated by spaces:

| Field | Plist key |
|---|---|
| name | the frame's key, with `.png`, as the game asks for it |
| x y w h | `frame`, in pixels from the atlas's top left |
| offsetX offsetY | `offset` |
| sourceW sourceH | `sourceSize` |
| rotated | `rotated`, 0 or 1 |

No frame name has a space. None of the MDPI frames is rotated or trimmed:
every offset is 0 and every source size equals the frame's. The
SpriteFrameCache layer still honours the fields, as cocos2d does.

| Atlas | Frames | Size |
|---|---|---|
| `GUI` | 48 | 1024 square |
| `HomeScreen` | 19 | 512 square |
| `MapSelect` | 34 | 512 square |
| `Popups` | 30 | 512 square |
