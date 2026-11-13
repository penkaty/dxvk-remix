# Wetness convention (`__wet.*`)

Game values a host sets with `remixapi_SetGameValue` to steer the wetness model (`rtx.wet.*`).

## `__wet.roadMask`

Where standing water may form. The wetness model puts puddles on any wet surface that is flat and exposed to rain.
A host that knows where the road is can limit them to it, while the rest of the ground keeps the wet look.

Value: `<originU> <originV> <cellSize> <cells> <hex>`

| Field | Meaning |
|---|---|
| `originU`, `originV` | World position of the corner of cell (0, 0) in the plane perpendicular to the scene up axis: (x, y) with `rtx.zUp`, (x, z) without. |
| `cellSize` | Edge of a square cell, in world units. |
| `cells` | Cells per side, 1 to 512. |
| `hex` | `cells * cells` bits, row by row (V outer, U inner), four cells per hex digit, most significant bit first. 1 = road. |

- The mask is read when the rain map is traced (`rtx.wet.puddleQuality` 2). Points outside the mask take its nearest cell.
- Send it again when the camera has moved far enough that the rain map window (`rtx.wet.mapExtent`) leaves the mask.
- An empty value removes the mask: puddles form on every wet surface again.
- Surfaces in `rtx.wetNoPuddleTextures` never hold water, with or without a mask.
