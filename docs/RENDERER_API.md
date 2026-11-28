# Stealth renderer plug-in interface

Soulbringer uses the "Stealth" engine. Its renderer is a plug-in DLL (`D3d7Lib.dll`, `D3d6Lib.dll`, `sft8lib.dll`) exporting 76 `cdecl` functions. The loader at `0x694D50` stores the function pointers in the renderer object at `[0x35767D4]`. Calls look like:

```
mov  eax, [0x35767D4]
call [eax + offset]
```

## Drawing model

- The game transforms and projects everything on the CPU. The `Set*Transform` functions exist, but the game never calls them.
- `AddTri(bucket, material, nTris, ptr)` returns a pointer to `3*nTris` vertices. The game fills them in, then calls `CommitTri(bucket, material, nTris)`.
- `AddTriStrip` returns `nTris+2` vertices, `AddLine` returns `2*n`.
- Vertex format, 32 bytes: `float x, y, z, rhw; DWORD diffuse, specular; float u, v` (`D3DFVF_XYZRHW|DIFFUSE|SPECULAR|TEX1`). 2D elements use `rhw=1` and a tiny, but not exactly zero, `z`.
- Frame: `StartScene`, `StartFrame` … `RenderAllBuffers` (several times) … `EndFrame`, `EndScene`.
- **Deferred drawing:** `CommitTri` only adds to the vertex count of the bucket (`[slot+0xE8]+0x48`). Vertices stay in the renderer's buffer and are read at `RenderAllBuffers` / `EndFrame`. Until then, a proxy can adjust them in place.
- First argument = bucket: in practice 0 = full-screen art, 2 = sprites, 3 = text. Second = material (texture handle; handles are reused between screens).
- Depth conventions seen in the game: flat 2D UI `rhw≈1`; **UI 3D models** (inventory items, right-column icons) `rhw≈0.333`; the world `rhw < 0.002`. Classify by `rhw` (the proxy uses `rhw < 0.1` = world); never test `z == 0`.

## Who draws what (return addresses in SoulbringeVCnoeax.exe)

| Caller | Content |
|---|---|
| `0x004B686A` | Full-screen art and large HUD tiles (menu backgrounds in 6 tiles of 213.33×240 canvas units; HUD column and bottom bar tiles) |
| `0x004A9195` | Sprites: HUD ornaments, sliders, bulbs, portraits, **mouse cursor** (material `0x52`, 32×32 px) |
| `0x0042B0BC` | Text, in batches of ≤10 glyphs per font material (`0x4E`–`0x51`), inside `0x42B020` |
| `0x00456ABD` | Hover highlights and small helper quads |
| `0x0048EA4B` | 3D meshes: world objects **and** UI item models (z=0) |
| `0x0050E8F1`, `0x0050ED97`, `0x0050EFE6`, `0x0042C76B`, `0x0043B4F1`, `0x00506CBE` | World geometry (terrain, buildings, characters) |
| `0x0048ECAB`, `0x0048EEEB`, `0x0041E4F5`, `0x0055950E`, `0x0053E5BB` | Effects, particles, sky, small 3D UI |

## Function table

"Used" means the exe calls it during normal play. Offsets are into the renderer object.

| Offset | Function | Used |
|---|---|---|
| `0x14` | `Detect` | yes |
| `0x18` | `Initialise` | yes |
| `0x1c` | `SelectDevice` | yes |
| `0x20` | `OpenMode` | yes |
| `0x24` | `CloseMode` | yes |
| `0x28` | `SetBufferClear` |  |
| `0x2c` | `StartScene` | yes |
| `0x30` | `StartFrame` | yes |
| `0x34` | `EndFrame` | yes |
| `0x38` | `EndScene` | yes |
| `0x3c` | `FlipScene` | yes |
| `0x40` | `BufferLock` |  |
| `0x44` | `BufferUnLock` |  |
| `0x48` | `RenderAllBuffers` | yes |
| `0x4c` | `GetStatusData` |  |
| `0x50` | `SetRenderTarget` |  |
| `0x54` | `BeginTextureCreation` | yes |
| `0x58` | `CreateTexture` | yes |
| `0x5c` | `EndTextureCreation` | yes |
| `0x60` | `OverWriteTexture` | yes |
| `0x64` | `DestroyAllTextures` | yes |
| `0x68` | `SetGlobalPalette` | yes |
| `0x6c` | `SetUpFog` |  |
| `0x70` | `SetFog` |  |
| `0x74` | `RenderStateSetStage` |  |
| `0x78` | `RenderStateSet` | yes |
| `0x7c` | `RenderStateValidate` | yes |
| `0x80` | `SetLocalToWorldTransform` |  |
| `0x84` | `SetWorldToViewTransform` |  |
| `0x88` | `SetViewToScreenTransform` |  |
| `0x8c` | `CreateLightSlot` |  |
| `0x90` | `AddLight` |  |
| `0x94` | `SetLightState` |  |
| `0x98` | `SetLightPosition` |  |
| `0x9c` | `SetLightDirection` |  |
| `0xa0` | `SetLightColour` |  |
| `0xa4` | `DestroyLight` |  |
| `0xa8` | `DestroyLightSlot` |  |
| `0xac` | `SetCurrentLights` |  |
| `0xb0` | `SetMaterial` |  |
| `0xb4` | `AddDot` |  |
| `0xb8` | `AddLine` | yes |
| `0xbc` | `AddTri` | yes |
| `0xc0` | `AddTriStrip` | yes |
| `0xc4` | `AddDotToOrderTable` |  |
| `0xc8` | `AddLineToOrderTable` |  |
| `0xcc` | `AddTriToOrderTable` | yes |
| `0xd0` | `AddTriStripToOrderTable` |  |
| `0xd4` | `CommitDot` |  |
| `0xd8` | `CommitLine` | yes |
| `0xdc` | `CommitTri` | yes |
| `0xe0` | `CommitTriStrip` | yes |
| `0xe4` | `CommitDotToOrderTable` |  |
| `0xe8` | `CommitLineToOrderTable` |  |
| `0xec` | `CommitTriToOrderTable` | yes |
| `0xf0` | `CommitTriStripToOrderTable` |  |
| `0xf4` | `AddPrim` |  |
| `0xf8` | `CommitPrim` |  |
| `0xfc` | `AddIndexedPrim` |  |
| `0x100` | `CommitIndexedPrim` |  |
| `0x104` | `GetVertexBuffer` |  |
| `0x108` | `ReturnVertexBuffer` |  |
| `0x10c` | `LockVertexBuffer` |  |
| `0x110` | `UnLockVertexBuffer` |  |
| `0x114` | `GetIndexBuffer` |  |
| `0x118` | `ReturnIndexBuffer` |  |
| `0x11c` | `DirectCommitPrim` |  |

Offsets above `0x11c` (`DumpScene`, `SetLightingModel`, `StealthExtension`, …) follow in export order. The exe calls `StealthExtension` (`0x140`) with arguments 1 and 4 after `OpenMode`.

## Exports of D3d7Lib.dll (ordinal order)

`AddDot`, `AddDotToOrderTable`, `AddIndexedPrim`, `AddLight`, `AddLine`, `AddLineToOrderTable`, `AddPrim`, `AddTri`, `AddTriStrip`, `AddTriStripToOrderTable`, `AddTriToOrderTable`, `BeginTextureCreation`, `BufferLock`, `BufferUnLock`, `CloseMode`, `CommitDot`, `CommitDotToOrderTable`, `CommitIndexedPrim`, `CommitLine`, `CommitLineToOrderTable`, `CommitPrim`, `CommitTri`, `CommitTriStrip`, `CommitTriStripToOrderTable`, `CommitTriToOrderTable`, `CreateLightSlot`, `CreateTexture`, `DestroyAllTextures`, `DestroyLight`, `DestroyLightSlot`, `Detect`, `DirectCommitPrim`, `DirectIndexedCommitPrim`, `DumpScene`, `EndFrame`, `EndScene`, `EndTextureCreation`, `FlipScene`, `GetDllDescription`, `GetIndexBuffer`, `GetStatusData`, `GetVertexBuffer`, `Initialise`, `LockVertexBuffer`, `OpenMode`, `OsSetWindowHandle`, `OsTaskSwitch`, `OverWriteTexture`, `RenderAllBuffers`, `RenderLogo`, `RenderStateSet`, `RenderStateSetStage`, `RenderStateValidate`, `RenderTest`, `ReturnIndexBuffer`, `ReturnVertexBuffer`, `SelectDevice`, `SetBufferClear`, `SetCurrentLights`, `SetFog`, `SetGlobalPalette`, `SetLightColour`, `SetLightDirection`, `SetLightPosition`, `SetLightState`, `SetLocalToWorldTransform`, `SetMaterial`, `SetRenderTarget`, `SetUpFog`, `SetViewToScreenTransform`, `SetWorldToViewTransform`, `StartFrame`, `StartScene`, `StealthExtension`, `UnLockVertexBuffer`, `ViewTexture`

## Textures (from the font page research, 2026-11-22)

| Call | Arguments | Notes |
|---|---|---|
| `CreateTexture(slot, desc)` | `desc+4` width, `+8` height, `+0xC` 1, `+0x10` format 4, `+0x14` flags (0x40 for the font pages), `+0x2C` 2, `+0x30` 0x10, `+0x34` palette pointer | ~113 slots at startup (`0x678A5C`, `0x678ACC`), all 256×256 |
| `OverWriteTexture(slot, 0, pixels)` | source pixels, 8-bit palette indices for the font pages (index 0 = empty) | cdecl, 3 args; game callers `0x492906`, `0x4929A2`, `0x492A26`, `0x4C426D`… |
| `SetGlobalPalette(0, 256, rgb)` | 256 × RGB at `0x1FFDBCC` | from `0x5C3D70` and `0x597444` |

Font pages: slots **0x4E–0x51** = the font materials of the `.fnt` records. Texture coordinates reach the renderer as fractions (texel × 1/256 in the glyph flush `0x42B024`), so a page created larger at the same layout needs no change in the game.

Inside `D3d7Lib_orig.dll` (base 0x10000000): renderer object `[0x100786CC]`; slot record `[obj + 0x1B54 + 4*slot]`: `+0` source width, `+4` source height, `+8` surface size, `+0xC` bytes per source pixel, `+0x20`/`+0x24` source/surface format codes (1 / 4 for the font pages), `+0x34` mip levels, `+0x54` the `IDirectDrawSurface7`. `OverWriteTexture` = `0x1000F380`: lock (`+0x64`), expand the source to the surface size with nearest sampling if they differ (`0x1000F880`), convert the format (`0x1000F9D0`), unlock (`+0x80`).

Texture cache (step 4 finding, 2026-11-22): slot surfaces live in system memory; drawing uses a video-memory cache entry `[obj + 0x1F54 + 4*i]` (device texture at entry `+0x3C`, bound with `SetTexture`, device vtable `+0x8C`). The slot record `+0x60` holds the cache index (-1 after `CreateTexture`, assigned at `EndTextureCreation`); `OverWriteTexture` refreshes the cache from the surface after unlocking (`0x1000E7F0(slot, index)`). The refresh is `IDirect3DDevice7::Load(cache texture, NULL, surface, NULL, 0)` (device `[0x10021FC8]`, vtable `+0xAC`, at `0x1000E8B5`). So writing to the surface after an upload reaches the screen only after the same `Load`. An A8R8G8B8 *source* (code 0x80) is not a way around it: the renderer reads its bytes as A,R,G,B and keeps 1-bit alpha.

Format codes and surfaces (surface probe, session pid 16880, and static, 2026-11-22):
- **Format codes are bit flags**: `0x1000F7D0` returns the bit index; the conversion `0x1000F9D0` decodes the source with converter `[obj + 0x102C + 4*bit(src code)]` into an intermediate buffer and encodes with `[obj + 0x10AC + 4*bit(surface code)]`.
- `CreateTexture` (`0x1000E930`) copies `desc+0..+0x38` into the record at `+0x14` (so record `+0x24` = `desc+0x10`, the **requested surface format code**) and picks the DirectDraw pixel format by looking that code up in the device's texture-format list (`0x100026B0`: device `[0x100785E4]`, entries of 0x28 bytes from `+0x3D0C`, count `+0x44DC`, code at entry `+0x24`; result in record `+0x50`). A second list of the same shape is at `+0x3538` (count `+0x3D08`).
- Surface size: `0x1000F800(w, h, flags)`, next power of two; flags from `desc+0x30` (`0x20` → doubles, `0x80` → clamps sizes ≥ 512 to 256). The font pages have `desc+0x30` = 0x10, so a larger page should not be clamped (to confirm in step 3).
- **Measured on the user's PC (DDrawCompat):** all 113 slots, code 1 (8-bit indices) and code 4 (16-bit) sources alike, get **A1R5G5B5** surfaces (R `7C00`, G `03E0`, B `001F`, **A `8000`: 1-bit alpha**), 256×256, pitch 512, one level, caps `00401808` (texture, **system memory**, mipmap, complex), no colour key. Font pages: index 0 → `0x0000` (transparent), any other index → `0x8000 | (r>>3)<<10 | (g>>3)<<5 | b>>3` (truncation): matches every texel of all four pages.

Texture formats the renderer offers (device list, user's PC with DDrawCompat, session pid 12556). The code is what `desc+0x10` asks for; "bit" is what `0x1000F7D0` returns (highest set bit + 1), the index into the converter tables:

| Code | Bit | Format | Converter |
|---|---|---|---|
| 0x1 | 1 | 8-bit palette indices (source only, not in the list) | decoder only |
| 0x2 | 2 | **A4R4G4B4** | both |
| 0x4 | 3 | A1R5G5B5 (all game textures today) | both |
| 0x8 | 4 | X1R5G5B5 | both |
| 0x10 | 5 | R5G6B5 | both |
| 0x40 | 7 | X8R8G8B8 | both |
| 0x80 | 8 | **A8R8G8B8** | both |
| 0x200 / 0x400 | 10 / 11 | L8 / A8L8 | both |
| 0x2000 / 0x4000 | 14 / 15 | bump-map formats (V8U8 / X8L8V8U8-style) | both |
| 0x10000–0x100000 | 17–21 | DXT1–DXT5 | — |

The font slot's record `+0x50` points at list entry 3 (code 4), which confirms the entry layout (DDPIXELFORMAT at `+0`, bits per pixel at `+0x20`, code at `+0x24`). Bytes per source pixel by bit (`orig+0x2060C`): 0 1 2 2 2 2 3 4 4 1 1 2 1 2 2 3 2.


## Other engine facts used by the proxy

| What | Where |
|---|---|
| Screen width / height (pixels) | `[0x367370C]` / `[0x3673708]` |
| Projection scales (computed at `0x4DC540`) | x `0x2A98708`, y `0x2A98710`; zoom `[0x6B83FC]`; widescreen patch at `0x4DC5CD` |
| Text: string layout (UTF-16, `[[ecx]+i*2]`) | `0x4345A4`, called from `0x432300` |
| Text: glyph flush per text object | `0x42B024`, called from `0x635462`; list at `[[0x1E4CFBC]+4]+8`, next at `+0x510` |
| Game text files | `lang\english\*.uni` (UTF-16) |
| DirectInput (`DirectInputCreateA`, version 0x700) | object `[0x3673628]`; device wrappers `[0x367361C]`, `[0x3673620]`, device at wrapper+8 |
