# System cursor pack

SVG masters under `/System/Cursors/Masters/` and a single `roboticela.rcur` binary the shell paints from (Phase 44.7).

## Apple sizing rule

Cursor size is **logical points**, not a screen-resolution guess:

```text
physical pixels = logical_pt × device_scale
```

The pack authors at `logical_pt = 16` and ships PNG frames at 32 / 48 / 64 so 1×–4× displays pick a nearest authored size. Hotspots are stored at the 32 px frame and scaled with the chosen bitmap.

## Formats

| Format | Role |
|---|---|
| SVG masters | Authoring, Desktop Lab CSS `cursor: url(data:…)`, web previews |
| `.rcur` | Runtime paint: light/dark themes, multi-resolution PNGs, wait/progress animation |

The freestanding OS has no SVG rasterizer; the shell always draws from `.rcur`. Host Desktop Lab uses the SVG masters (or data URIs) so every state is hover-testable without a binary pack.

## Wiring

1. `DesktopCursorPackCreateFromRcur` parses the blob.
2. `DesktopAttachCursorPack` hands ownership to the shell.
3. `DesktopInput` syncs `InputCursorType` onto `Desktop.cursorShape`.
4. `DesktopChromeDrawCursorFor` resolves theme / scale / anim frame and blits.

## Evidence

- `Scripts/Test/TestDesktop.ps1 -Target Host` → `desktop.cursor.*`
- `Build/Cursors-Gallery.png`
- Desktop Lab route `/cursors`
